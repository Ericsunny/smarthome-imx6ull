#ifndef CAMERAWORKER_H
#define CAMERAWORKER_H

#include <QThread>
#include <QImage>
#include <QString>
#include <QVector>
#include <opencv2/opencv.hpp>
#include <opencv2/face.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <linux/videodev2.h>
#include <cstring>

/* 人脸库目录: 录入的照片(名字_N.jpg)都存这里, 启动时从这里重训练 */
static const char FACE_DB_DIR[] = "/face_db";

/* ---- 摄像头格式协商: 不同 USB 摄像头支持的格式差异很大, 必须先枚举 ---- */
struct CamFormat { __u32 fourcc; int w, h; };

static std::vector<CamFormat> camEnumerate(int fd)
{
    std::vector<CamFormat> out;
    for (int i = 0; ; i++) {
        struct v4l2_fmtdesc fd_;
        memset(&fd_, 0, sizeof(fd_));
        fd_.index = i;
        fd_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_ENUM_FMT, &fd_) < 0)
            break;
        for (int j = 0; ; j++) {
            struct v4l2_frmsizeenum fs;
            memset(&fs, 0, sizeof(fs));
            fs.index = j;
            fs.pixel_format = fd_.pixelformat;
            if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) < 0)
                break;
            if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                out.push_back({ fd_.pixelformat,
                                (int)fs.discrete.width,
                                (int)fs.discrete.height });
            } else {
                out.push_back({ fd_.pixelformat,
                                (int)fs.stepwise.min_width,
                                (int)fs.stepwise.min_height });
                out.push_back({ fd_.pixelformat,
                                (int)fs.stepwise.max_width,
                                (int)fs.stepwise.max_height });
                break;
            }
        }
    }
    return out;
}

/* YUYV(免解码)优先于 MJPEG, 同格式挑 240p 以上的最小尺寸 */
static bool camPick(const std::vector<CamFormat> &list, CamFormat &pick)
{
    for (int pass = 0; pass < 2; pass++) {
        __u32 want = (pass == 0) ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_MJPEG;
        int best = -1;
        for (size_t i = 0; i < list.size(); i++) {
            if (list[i].fourcc != want || list[i].h < 240)
                continue;
            if (list[i].w < list[i].h)
                continue;    /* 竖屏跳过(这颗摄像头有横竖两套尺寸) */
            if (best < 0 || list[i].w * list[i].h < list[best].w * list[best].h)
                best = (int)i;
        }
        if (best >= 0) { pick = list[best]; return true; }
    }
    if (!list.empty()) { pick = list[0]; return true; }
    return false;
}

/*
 * 摄像头采集+人脸识别线程：
 *   V4L2 mmap 抓帧 -> OpenCV 转 BGR -> 每 3 帧一次 Haar 人脸检测 ->
 *   LBPH 识别(和 /face_db 里录入的人脸比对) -> 画框+名字 -> QImage 发 UI。
 *
 * 识别逻辑：
 *   - 录入: startEnroll("名字") 后连续抓 10 张人脸存 /face_db/名字_N.jpg，
 *     自动重新训练 LBPH（启动时也会从 jpg 重训练，不用存模型文件）
 *   - 识别: LBPH predict 距离 < 60 判定为已录入的人，否则是陌生人
 *   - 陌生人连续出现会发 strangerAlarm 信号，UI 接现有报警链
 *
 * 为什么用 LBPH 不用 SeetaFace：LBPH 就在 OpenCV contrib 的 face 模块里，
 * 板子本地跑完"检测+识别"全流程，一张 112x112 人脸预测只要几毫秒，
 * 不依赖 PC 端服务器和模型文件——单核小板上的务实选择。
 */
class CameraWorker : public QThread
{
    Q_OBJECT
public:
    explicit CameraWorker(QObject *parent = nullptr)
        : QThread(parent), m_stop(false), m_fd(-1), m_mjpeg(false),
          m_width(640), m_height(480), m_frameCnt(0), m_faces(0),
          m_cascadeOk(false), m_dbReady(false),
          m_enrollMode(false), m_enrollCnt(0), m_threshold(95)
    {
        memset(m_bufAddr, 0, sizeof(m_bufAddr));
        memset(m_bufLen, 0, sizeof(m_bufLen));
    }
    void stop() { m_stop = true; }

    /* UI 调用: 开始录入当前对准摄像头的人脸(采 10 张后自动训练) */
    void startEnroll(const QString &name)
    {
        m_enrollName = name.toStdString();
        m_enrollCnt = 0;
        m_enrollMode = true;
    }

protected:
    void run() override
    {
        cv::CascadeClassifier cascade;
        m_cascadeOk = cascade.load("/haarcascade_frontalface_alt2.xml");

        /* 启动时从 /face_db 的录入照片重训练识别器。
         * create 的阈值参数只影响 predict 内部收集器的过滤(超过就不记录,
         * 返回 DBL_MAX 看不到真实距离), 开成 1e9 让真实距离透出来,
         * 是否主人在 predict() 里拿真实距离和 m_threshold 比 */
        m_recognizer = cv::face::LBPHFaceRecognizer::create(1, 8, 8, 8, 1e9);
        retrain();

        while (!m_stop) {
            if (m_fd < 0) {
                if (openDevice() < 0) {
                    emit statusChanged("摄像头离线");
                    for (int i = 0; i < 20 && !m_stop; ++i)
                        msleep(100);
                    continue;
                }
                emit statusChanged("监控中");
            }

            struct pollfd pfd;
            pfd.fd = m_fd;
            pfd.events = POLLIN;
            if (poll(&pfd, 1, 300) <= 0)
                continue;

            struct v4l2_buffer b;
            memset(&b, 0, sizeof(b));
            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;
            if (ioctl(m_fd, VIDIOC_DQBUF, &b) < 0)
                continue;

            cv::Mat bgr;
            if (m_mjpeg) {
                std::vector<uchar> jpg((uchar *)m_bufAddr[b.index],
                                       (uchar *)m_bufAddr[b.index] + b.bytesused);
                bgr = cv::imdecode(jpg, cv::IMREAD_COLOR);
            } else {
                cv::Mat yuyv(m_height, m_width, CV_8UC2,
                             (uchar *)m_bufAddr[b.index]);
                cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
            }
            ioctl(m_fd, VIDIOC_QBUF, &b);

            if (bgr.empty())
                continue;

            m_frameCnt++;
            cv::Mat gray;
            cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

            if (m_cascadeOk && (m_frameCnt % 3 == 0)) {
                /* 检测在缩半的灰度图上跑，全图太慢 */
                cv::Mat small;
                cv::resize(gray, small, cv::Size(gray.cols / 2, gray.rows / 2));
                cv::equalizeHist(small, small);
                std::vector<cv::Rect> faces;
                cascade.detectMultiScale(small, faces, 1.1, 3, 0,
                                         cv::Size(60, 60));
                m_faces = (int)faces.size();
                m_rects.clear();
                int biggest = -1, biggestArea = 0;
                for (size_t i = 0; i < faces.size(); i++) {
                    cv::Rect r = faces[i];
                    m_rects.push_back(cv::Rect(r.x * 2, r.y * 2,
                                               r.width * 2, r.height * 2));
                    int area = r.width * r.height;
                    if (area > biggestArea) { biggestArea = area; biggest = (int)i; }
                }
                m_biggest = biggest;

                /* 取最大的人脸做识别/录入。
                 * 注意: faces 是缩半小图上的坐标, 必须乘 2 换算回全图再裁剪,
                 * 否则只截到人脸左上角的一小块, LBPH 直方图随头部位置漂移 */
                if (biggest >= 0) {
                    cv::Rect r = faces[biggest];
                    r.x *= 2;
                    r.y *= 2;
                    r.width *= 2;
                    r.height *= 2;
                    r &= cv::Rect(0, 0, gray.cols, gray.rows);
                    cv::Mat face112;
                    cv::resize(gray(r), face112, cv::Size(112, 112));
                    cv::equalizeHist(face112, face112);   /* 光照归一化, 录入/识别一致 */

                    if (m_enrollMode) {
                        /* 采样必须拉开间隔(~1秒/张)且人脸够大:
                         * 连拍 10 张几乎一样的图 = 训练集没有多样性,
                         * 光照/姿态稍变就识别不回来 */
                        if (r.width >= 90 && m_frameCnt - m_lastEnrollFrame >= 12) {
                            m_lastEnrollFrame = m_frameCnt;
                            saveEnroll(face112);
                        }
                    } else if (m_dbReady) {
                        /* 距离门限: 人脸太小(<170px)时距离会虚高, 不判陌生人,
                         * 提示靠近——实测远距时本人的距离也能到 102+ */
                        if (r.width >= 170)
                            predict(face112);
                        else
                            emit recognitionUpdate("距离太远");
                    }
                }
            }

            /* 画框和标注 */
            for (int i = 0; i < m_rects.size(); i++)
                cv::rectangle(bgr, m_rects[i], cv::Scalar(0, 255, 0), 3);
            cv::putText(bgr, std::to_string(m_faces) + std::string(" face(s)"),
                        cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0,
                        cv::Scalar(0, 255, 255), 2);

            cv::Mat rgb;
            cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
            QImage img(rgb.data, rgb.cols, rgb.rows, (int)rgb.step,
                       QImage::Format_RGB888);
            emit frameReady(img.copy(), m_faces);
        }
        if (m_fd >= 0)
            closeDevice();
    }

signals:
    void frameReady(const QImage &img, int faces);
    void statusChanged(const QString &s);
    void recognitionUpdate(const QString &name);   /* 识别结果(空串=没认出人) */
    void enrollProgress(int done, int total);
    void enrollFinished(bool ok);
    void strangerAlarm();                          /* 陌生人出现(连续两次) */

private:
    /* 扫描 /face_db 重训练。文件名: 名字_N.jpg */
    void retrain()
    {
        DIR *dir = opendir(FACE_DB_DIR);
        if (!dir) {
            mkdir(FACE_DB_DIR, 0755);
            emit recognitionUpdate("未录入人脸");
            return;
        }
        std::vector<cv::Mat> imgs;
        std::vector<int> labels;
        struct dirent *e;
        while ((e = readdir(dir)) != nullptr) {
            QString fn = QString::fromLocal8Bit(e->d_name);
            if (!fn.endsWith(".jpg"))
                continue;
            QString path = QString(FACE_DB_DIR) + "/" + fn;
            cv::Mat face = cv::imread(path.toStdString(), cv::IMREAD_GRAYSCALE);
            if (face.empty())
                continue;
            imgs.push_back(face);
            labels.push_back(0);          /* 演示阶段单人库, 标签固定 0 */
        }
        closedir(dir);
        if (!imgs.empty()) {
            m_recognizer->train(imgs, labels);
            m_dbReady = true;
        } else {
            emit recognitionUpdate("未录入人脸");
        }
    }

    void saveEnroll(const cv::Mat &face112)
    {
        mkdir(FACE_DB_DIR, 0755);
        char path[128];
        snprintf(path, sizeof(path), "%s/%s_%02d.jpg",
                 FACE_DB_DIR, m_enrollName.c_str(), m_enrollCnt);
        cv::imwrite(path, face112);
        m_enrollCnt++;
        emit enrollProgress(m_enrollCnt, 10);
        if (m_enrollCnt >= 10) {
            m_enrollMode = false;
            retrain();
            if (m_dbReady)
                emit enrollFinished(true);
        }
    }

    void predict(const cv::Mat &face112)
    {
        int label = -1;
        double conf = 0;
        m_recognizer->predict(face112, label, conf);

        static int strangerCnt = 0;
        if (conf < m_threshold) {
            strangerCnt = 0;
            emit recognitionUpdate(QString("主人(%1)").arg((int)conf));
        } else {
            strangerCnt++;
            emit recognitionUpdate(QString("陌生人(%1)").arg((int)conf));
            if (strangerCnt >= 2) {       /* 连续两次检测都是陌生人才报警 */
                strangerCnt = 0;
                emit strangerAlarm();
            }
        }
    }

    int openDevice()
    {
        /* UVC 节点可能不在 video0(板上 CSI 驱动 mx6s 会占号), 扫描找 uvcvideo */
        m_fd = -1;
        for (int n = 0; n < 10 && m_fd < 0; n++) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/video%d", n);
            int fd = open(path, O_RDWR);
            if (fd < 0)
                continue;
            struct v4l2_capability cap;
            memset(&cap, 0, sizeof(cap));
            if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 &&
                std::string((char *)cap.driver).find("uvc") != std::string::npos)
                m_fd = fd;
            else
                close(fd);
        }
        if (m_fd < 0)
            return -1;

        /* 枚举支持的格式, 按策略自动选 */
        std::vector<CamFormat> list = camEnumerate(m_fd);
        CamFormat pick;
        if (!camPick(list, pick)) {
            close(m_fd);
            m_fd = -1;
            return -1;
        }

        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = pick.w;
        fmt.fmt.pix.height = pick.h;
        fmt.fmt.pix.pixelformat = pick.fourcc;
        if (ioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
            close(m_fd);
            m_fd = -1;
            return -1;
        }
        /* 回读实际生效值(驱动可能调整尺寸/格式) */
        m_mjpeg = (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG);
        m_width = fmt.fmt.pix.width;
        m_height = fmt.fmt.pix.height;

        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.count = 4;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(m_fd, VIDIOC_REQBUFS, &req) < 0 ||
            req.count < 2u) {
            close(m_fd);
            m_fd = -1;
            return -1;
        }
        for (int i = 0; i < (int)req.count; i++) {
            struct v4l2_buffer b;
            memset(&b, 0, sizeof(b));
            b.type = req.type;
            b.memory = req.memory;
            b.index = i;
            if (ioctl(m_fd, VIDIOC_QUERYBUF, &b) < 0)
                return -1;
            m_bufLen[i] = b.length;
            m_bufAddr[i] = (char *)mmap(NULL, b.length,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED, m_fd, b.m.offset);
            if (m_bufAddr[i] == MAP_FAILED)
                return -1;
            ioctl(m_fd, VIDIOC_QBUF, &b);
        }
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(m_fd, VIDIOC_STREAMON, &type) < 0)
            return -1;
        return 0;
    }

    void closeDevice()
    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(m_fd, VIDIOC_STREAMOFF, &type);
        for (int i = 0; i < 4; i++) {
            if (m_bufAddr[i]) {
                munmap(m_bufAddr[i], m_bufLen[i]);
                m_bufAddr[i] = nullptr;
            }
        }
        close(m_fd);
        m_fd = -1;
    }

    volatile bool m_stop;
    int m_fd;
    bool m_mjpeg;
    int m_width, m_height;
    char *m_bufAddr[4];
    unsigned int m_bufLen[4];
    int m_frameCnt;
    int m_faces;
    bool m_cascadeOk;
    QVector<cv::Rect> m_rects;
    int m_biggest;

    cv::Ptr<cv::face::LBPHFaceRecognizer> m_recognizer;
    bool m_dbReady;
    bool m_enrollMode;
    int m_enrollCnt;
    int m_lastEnrollFrame = -1000;      /* 上次采样的帧号(拉开采样间隔) */
    std::string m_enrollName;
    int m_threshold;                    /* LBPH 判"同一个人"的距离阈值。
                                           实测(2026-09-21): 本人近距 73~88,
                                           远距飙到 102+; 故 95 分层 + 距离门限 */
};

#endif /* CAMERAWORKER_H */
