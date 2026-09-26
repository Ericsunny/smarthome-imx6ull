/*
 * facetest —— 人脸识别链路诊断工具（独立于 QT, 串口终端直接跑）
 *
 * 用法:
 *   ./facetest enroll    采 10 张入 /face_db(每秒 1 张, 需人脸>=90px), 训练后自动自检
 *   ./facetest selftest  只做自检: 用库里的图回测训练好的模型
 *   ./facetest           实时识别: 每次检测打印距离/人脸宽/裁剪亮度
 *
 * 每步都打印数据, 拿输出就能定位问题在哪一层。
 */
#include <opencv2/opencv.hpp>
#include <opencv2/face.hpp>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
#include <linux/videodev2.h>

struct CamFormat { __u32 fourcc; int w, h; };

static int g_fd = -1;
static bool g_mjpeg = false;
static int g_w = 0, g_h = 0;
static char *g_buf[4];
static unsigned g_buflen[4];

/* ---------- V4L2: 找 UVC 节点 ---------- */
static std::string findUvcNode()
{
    for (int n = 0; n < 10; n++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/video%d", n);
        int fd = open(path, O_RDWR);
        if (fd < 0)
            continue;
        struct v4l2_capability cap;
        memset(&cap, 0, sizeof(cap));
        bool isUvc = false;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 &&
            std::string((char *)cap.driver).find("uvc") != std::string::npos)
            isUvc = true;
        close(fd);
        if (isUvc) {
            printf("UVC 节点: %s (driver=%s)\n", path, cap.driver);
            return std::string(path);
        }
    }
    return "";
}

static std::vector<CamFormat> enumerateFormats(int fd)
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
            if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE)
                out.push_back({ fd_.pixelformat,
                                (int)fs.discrete.width,
                                (int)fs.discrete.height });
            else {
                out.push_back({ fd_.pixelformat,
                                (int)fs.stepwise.min_width,
                                (int)fs.stepwise.min_height });
                break;
            }
        }
    }
    return out;
}

/* ---------- V4L2: 打开并按策略选格式 ---------- */
static bool openCam()
{
    std::string node = findUvcNode();
    if (node.empty()) {
        printf("!! 没找到 UVC 摄像头\n");
        return false;
    }
    g_fd = open(node.c_str(), O_RDWR);
    if (g_fd < 0)
        return false;

    std::vector<CamFormat> list = enumerateFormats(g_fd);
    CamFormat pick = { 0, 0, 0 };
    for (int pass = 0; pass < 2 && pick.fourcc == 0; pass++) {
        __u32 want = (pass == 0) ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_MJPEG;
        for (size_t i = 0; i < list.size(); i++) {
            if (list[i].fourcc != want || list[i].h < 240 || list[i].w < list[i].h)
                continue;
            if (pick.fourcc == 0 ||
                list[i].w * list[i].h < pick.w * pick.h)
                pick = list[i];
        }
    }
    if (pick.fourcc == 0 && !list.empty())
        pick = list[0];

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = pick.w;
    fmt.fmt.pix.height = pick.h;
    fmt.fmt.pix.pixelformat = pick.fourcc;
    if (ioctl(g_fd, VIDIOC_S_FMT, &fmt) < 0) {
        printf("!! S_FMT failed\n");
        return false;
    }
    g_mjpeg = (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG);
    g_w = fmt.fmt.pix.width;
    g_h = fmt.fmt.pix.height;
    printf("采集格式: %s %dx%d\n", g_mjpeg ? "MJPEG" : "YUYV", g_w, g_h);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.count = 4;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(g_fd, VIDIOC_REQBUFS, &req) < 0) {
        printf("!! REQBUFS failed\n");
        return false;
    }
    for (unsigned int i = 0; i < req.count; i++) {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = req.type;
        b.memory = req.memory;
        b.index = i;
        ioctl(g_fd, VIDIOC_QUERYBUF, &b);
        g_buflen[i] = b.length;
        g_buf[i] = (char *)mmap(NULL, b.length, PROT_READ | PROT_WRITE,
                                MAP_SHARED, g_fd, b.m.offset);
        ioctl(g_fd, VIDIOC_QBUF, &b);
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(g_fd, VIDIOC_STREAMON, &type);
    return true;
}

/* 取一帧转 BGR */
static bool grabFrame(cv::Mat &bgr)
{
    struct pollfd pfd = { g_fd, POLLIN, 0 };
    if (poll(&pfd, 1, 1000) <= 0)
        return false;
    struct v4l2_buffer b;
    memset(&b, 0, sizeof(b));
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    if (ioctl(g_fd, VIDIOC_DQBUF, &b) < 0)
        return false;
    if (g_mjpeg) {
        std::vector<uchar> jpg((uchar *)g_buf[b.index],
                               (uchar *)g_buf[b.index] + b.bytesused);
        bgr = cv::imdecode(jpg, cv::IMREAD_COLOR);
    } else {
        cv::Mat yuyv(g_h, g_w, CV_8UC2, (uchar *)g_buf[b.index]);
        cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
    }
    ioctl(g_fd, VIDIOC_QBUF, &b);
    return !bgr.empty();
}

/* 检测: 缩半图上跑, 返回全图坐标的最大人脸 */
static bool detectFace(cv::CascadeClassifier &cas, const cv::Mat &gray, cv::Rect &out)
{
    cv::Mat small;
    cv::resize(gray, small, cv::Size(gray.cols / 2, gray.rows / 2));
    cv::equalizeHist(small, small);
    std::vector<cv::Rect> faces;
    cas.detectMultiScale(small, faces, 1.1, 3, 0, cv::Size(60, 60));
    if (faces.empty())
        return false;
    size_t big = 0;
    for (size_t i = 1; i < faces.size(); i++)
        if (faces[i].width * faces[i].height > faces[big].width * faces[big].height)
            big = i;
    cv::Rect r = faces[big];
    r.x *= 2; r.y *= 2; r.width *= 2; r.height *= 2;   /* 回全图坐标 */
    r &= cv::Rect(0, 0, gray.cols, gray.rows);
    out = r;
    return true;
}

/* 裁 112x112 灰度 + 均衡化(和 QT 版完全一致的处理) */
static cv::Mat cropFace(const cv::Mat &gray, const cv::Rect &r)
{
    cv::Mat f;
    cv::resize(gray(r), f, cv::Size(112, 112));
    cv::equalizeHist(f, f);
    return f;
}

/* 从 /face_db 加载全部人脸图 */
static int loadDb(std::vector<cv::Mat> &imgs)
{
    DIR *dir = opendir("/face_db");
    if (!dir)
        return 0;
    struct dirent *e;
    int n = 0;
    while ((e = readdir(dir)) != nullptr) {
        std::string fn(e->d_name);
        if (fn.size() < 4 || fn.substr(fn.size() - 4) != ".jpg")
            continue;
        cv::Mat m = cv::imread(std::string("/face_db/") + fn, cv::IMREAD_GRAYSCALE);
        if (!m.empty()) {
            imgs.push_back(m);
            n++;
        }
    }
    closedir(dir);
    return n;
}

int main(int argc, char **argv)
{
    std::string mode = argc > 1 ? argv[1] : "test";

    cv::CascadeClassifier cascade;
    bool casOk = cascade.load("/haarcascade_frontalface_alt2.xml");
    printf("级联文件加载: %s\n", casOk ? "OK" : "!! 失败");
    if (!casOk)
        return 1;

    if (mode == "selftest") {
        std::vector<cv::Mat> imgs;
        int n = loadDb(imgs);
        printf("人脸库: %d 张\n", n);
        if (n == 0)
            return 1;
        std::vector<int> labels(n, 0);
        cv::Ptr<cv::face::LBPHFaceRecognizer> rec =
            cv::face::LBPHFaceRecognizer::create(1, 8, 8, 8, 1e9);
        rec->train(imgs, labels);
        printf("== 用训练图自己回测(距离应 <20) ==\n");
        for (int i = 0; i < n; i++) {
            int label = -1;
            double conf = 0;
            rec->predict(imgs[i], label, conf);
            cv::Scalar mean, dev;
            cv::meanStdDev(imgs[i], mean, dev);
            printf("  图%02d  距离=%.1f  亮度=%.0f\n",
                   i, conf, mean[0]);
        }
        return 0;
    }

    if (!openCam())
        return 1;

    if (mode == "enroll") {
        mkdir("/face_db", 0755);
        int frame = 0, saved = 0, lastSaved = -1000;
        printf("== 录入: 对准镜头, 缓慢转头, 采 10 张(每秒1张) ==\n");
        while (saved < 10) {
            cv::Mat bgr;
            if (!grabFrame(bgr))
                continue;
            cv::Mat gray;
            cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
            frame++;
            if (frame % 3)
                continue;
            cv::Rect r;
            if (!detectFace(cascade, gray, r)) {
                if (frame % 30 == 0)
                    printf("  ...没检测到人脸\n");
                continue;
            }
            if (r.width < 90) {
                if (frame % 30 == 0)
                    printf("  人脸太小(%dpx), 靠近一点\n", r.width);
                continue;
            }
            if (frame - lastSaved < 12)
                continue;
            lastSaved = frame;
            cv::Mat f = cropFace(gray, r);
            char path[64];
            snprintf(path, sizeof(path), "/face_db/diag_%02d.jpg", saved);
            cv::imwrite(path, f);
            cv::Scalar mean, dev;
            cv::meanStdDev(f, mean, dev);
            printf("  保存 %s  人脸宽=%dpx  亮度=%.0f\n",
                   path, r.width, mean[0]);
            saved++;
        }
        /* 立即自检 */
        std::vector<cv::Mat> imgs;
        int n = loadDb(imgs);
        std::vector<int> labels(n, 0);
        cv::Ptr<cv::face::LBPHFaceRecognizer> rec =
            cv::face::LBPHFaceRecognizer::create(1, 8, 8, 8, 1e9);
        rec->train(imgs, labels);
        printf("== 自检(训练图回测) ==\n");
        for (int i = 0; i < n; i++) {
            int label = -1;
            double conf = 0;
            rec->predict(imgs[i], label, conf);
            printf("  图%02d  距离=%.1f\n", i, conf);
        }
        printf("录入完成\n");
        return 0;
    }

    /* 实时识别模式 */
    std::vector<cv::Mat> imgs;
    int n = loadDb(imgs);
    printf("人脸库: %d 张\n", n);
    if (n == 0) {
        printf("!! 库是空的, 先 ./facetest enroll\n");
        return 1;
    }
    std::vector<int> labels(n, 0);
    cv::Ptr<cv::face::LBPHFaceRecognizer> rec =
        cv::face::LBPHFaceRecognizer::create(1, 8, 8, 8, 1e9);
    rec->train(imgs, labels);
    printf("== 实时识别(Ctrl+C 退出) ==\n");
    int frame = 0;
    while (1) {
        cv::Mat bgr;
        if (!grabFrame(bgr))
            continue;
        cv::Mat gray;
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        frame++;
        if (frame % 3)
            continue;
        cv::Rect r;
        if (!detectFace(cascade, gray, r))
            continue;
        cv::Mat f = cropFace(gray, r);
        int label = -1;
        double conf = 0;
        rec->predict(f, label, conf);
        cv::Scalar mean, dev;
        cv::meanStdDev(f, mean, dev);
        printf("人脸宽=%dpx  距离=%.1f  亮度=%.0f  %s\n",
               r.width, conf, mean[0],
               conf < 95 ? "<== 主人" : "<== 陌生人");
    }
    return 0;
}
