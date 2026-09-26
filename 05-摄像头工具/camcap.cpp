/*
 * camcap v2 —— USB 摄像头抓图工具(自动适配版)
 *
 * 先枚举驱动支持的格式和分辨率(不同 USB 摄像头差异很大), 再按
 * "YUYV 优先 > MJPEG, 最小 240p 起"的策略自动选一组, 抓一帧存 JPG。
 *
 * 用法:
 *   ./camcap list              只打印支持的全部格式/尺寸
 *   ./camcap [dev] [out.jpg]   自动选格式抓一帧
 */
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <cstring>
#include <cstdio>
#include <string>

struct CamFormat { __u32 fourcc; int w, h; };

/* 板上可能同时有 CSI 接口驱动(mx6s)和 USB 摄像头(uvcvideo)都注册成 videoN,
 * 靠 QUERYCAP 的驱动名认出哪个是 UVC 节点 */
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
        printf("  %s -> driver=%s%s\n", path, cap.driver,
               isUvc ? "  <== UVC 摄像头" : "");
        if (isUvc)
            return std::string(path);
    }
    return "";
}

static std::string fourccStr(__u32 f)
{
    char s[5] = { (char)(f & 0xff), (char)((f >> 8) & 0xff),
                  (char)((f >> 16) & 0xff), (char)((f >> 24) & 0xff), 0 };
    return std::string(s);
}

/* 列出驱动支持的全部"格式+尺寸"组合 */
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
            if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                out.push_back({ fd_.pixelformat,
                                (int)fs.discrete.width,
                                (int)fs.discrete.height });
            } else {
                /* 连续/步进尺寸: 记最小和最大两个代表值 */
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

/* 选择策略: YUYV(免解码) > MJPEG, 同格式里挑 240p 以上的最小尺寸 */
static bool pickFormat(const std::vector<CamFormat> &list, CamFormat &pick)
{
    for (int pass = 0; pass < 2; pass++) {
        __u32 want = (pass == 0) ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_MJPEG;
        int best = -1;
        for (size_t i = 0; i < list.size(); i++) {
            if (list[i].fourcc != want)
                continue;
            if (list[i].h < 240)
                continue;
            if (best < 0 || list[i].w * list[i].h < list[best].w * list[best].h)
                best = (int)i;
        }
        if (best >= 0) { pick = list[best]; return true; }
    }
    if (!list.empty()) { pick = list[0]; return true; }
    return false;
}

int main(int argc, char **argv)
{
    std::string devAuto;
    const char *dev = nullptr;
    const char *out = "/tmp/capture.jpg";
    bool listOnly = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "list")
            listOnly = true;
        else if (std::string(argv[i]).find("video") != std::string::npos)
            dev = argv[i];
        else
            out = argv[i];
    }
    if (dev == nullptr) {
        printf("扫描 video 节点:\n");
        devAuto = findUvcNode();
        if (devAuto.empty()) {
            printf("没找到 UVC 摄像头节点!\n");
            return 1;
        }
        dev = devAuto.c_str();
        printf("使用 %s\n", dev);
    }

    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        printf("open %s failed\n", dev);
        return 1;
    }

    std::vector<CamFormat> list = enumerateFormats(fd);
    printf("支持的格式/尺寸:\n");
    for (size_t i = 0; i < list.size(); i++)
        printf("  %s  %dx%d\n", fourccStr(list[i].fourcc).c_str(),
               list[i].w, list[i].h);
    if (listOnly) { close(fd); return 0; }

    CamFormat pick;
    if (!pickFormat(list, pick)) {
        printf("枚举不到任何格式!\n");
        return 1;
    }
    printf("选用: %s %dx%d\n", fourccStr(pick.fourcc).c_str(),
           pick.w, pick.h);

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = pick.w;
    fmt.fmt.pix.height = pick.h;
    fmt.fmt.pix.pixelformat = pick.fourcc;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        printf("S_FMT failed\n");
        return 1;
    }
    __u32 got = fmt.fmt.pix.pixelformat;   /* 回读实际生效值(驱动可能调整) */
    printf("生效: %s %ux%u sizeimage=%u\n", fourccStr(got).c_str(),
           fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.sizeimage);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.count = 2;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        printf("REQBUFS failed\n");
        return 1;
    }
    static char *bufAddr[4];
    static unsigned int bufLen[4];
    for (unsigned int i = 0; i < req.count; i++) {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = req.type;
        b.memory = req.memory;
        b.index = i;
        ioctl(fd, VIDIOC_QUERYBUF, &b);
        bufLen[i] = b.length;
        bufAddr[i] = (char *)mmap(NULL, b.length, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, b.m.offset);
        ioctl(fd, VIDIOC_QBUF, &b);
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMON, &type);
    struct v4l2_buffer b;
    memset(&b, 0, sizeof(b));
    b.type = req.type;
    b.memory = req.memory;
    if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) {
        printf("DQBUF failed\n");
        return 1;
    }

    if (got == V4L2_PIX_FMT_MJPEG) {
        FILE *f = fopen(out, "wb");
        fwrite(bufAddr[b.index], 1, b.bytesused, f);
        fclose(f);
        printf("mjpeg frame %u bytes -> %s\n", b.bytesused, out);
    } else {
        cv::Mat yuyv((int)fmt.fmt.pix.height, (int)fmt.fmt.pix.width,
                     CV_8UC2, bufAddr[b.index]);
        cv::Mat bgr;
        cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
        cv::imwrite(out, bgr);
        printf("yuyv frame converted -> %s\n", out);
    }

    ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (unsigned int i = 0; i < req.count; i++)
        munmap(bufAddr[i], bufLen[i]);
    close(fd);
    return 0;
}
