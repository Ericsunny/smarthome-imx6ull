#include "devices.h"

#ifdef __linux__
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace monitor {
namespace {
int openCharacter(const std::string &path, int flags)
{
    int fd = open(path.c_str(), flags | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISCHR(st.st_mode)) {
        close(fd); errno = ENODEV; return -1;
    }
    return fd;
}
int writeSwitch(const std::string &path, bool on)
{
    int fd = openCharacter(path, O_WRONLY);
    if (fd < 0) return errno;
    const unsigned char value = on ? 1 : 0;
    ssize_t written;
    do { written = write(fd, &value, 1); } while (written < 0 && errno == EINTR);
    const int error = written == 1 ? 0 : (written < 0 ? errno : EIO);
    close(fd);
    return error;
}
class LinuxDevices : public Devices {
public:
    explicit LinuxDevices(const std::string &root) : root_(root) {}
    ~LinuxDevices() override { if (pirFd_ >= 0) close(pirFd_); }
    DhtReading readDht() override
    {
        int fd = openCharacter(root_ + "/dht11", O_RDONLY);
        if (fd < 0) return DhtReading(false, 0, 0, errno);
        unsigned char data[2] = {0, 0};
        const ssize_t n = read(fd, data, sizeof(data));
        const int error = n == 2 ? 0 : (n < 0 ? errno : EIO);
        close(fd);
        return DhtReading(n == 2, data[1], data[0], error);
    }
    LightReading readLight() override
    {
        int fd = openCharacter(root_ + "/ap3216c", O_RDONLY);
        if (fd < 0) return LightReading(false, 0, errno);
        unsigned short raw = 0;
        // Existing BSP ioctl interface, not the independent read(6) experiment.
        const int rc = ioctl(fd, _IOR('D', 1, unsigned short), &raw);
        const int error = rc < 0 ? errno : 0;
        close(fd);
        return LightReading(rc >= 0, raw / 1.2, error);
    }
    PirReading waitPir(int timeoutMs) override
    {
        if (pirFd_ < 0) {
            pirFd_ = openCharacter(root_ + "/hc_sr505", O_RDONLY | O_NONBLOCK);
            if (pirFd_ < 0) return PirReading(PirKind::Offline, false, errno);
        }
        struct pollfd pfd = {pirFd_, POLLIN, 0};
        const int rc = poll(&pfd, 1, timeoutMs);
        if (rc == 0 || (rc < 0 && errno == EINTR)) return PirReading();
        if (rc < 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
            return pirFailure(rc < 0 ? errno : ENODEV);
        if (!(pfd.revents & POLLIN)) return pirFailure(EPROTO);
        int state = -1;
        const ssize_t n = read(pirFd_, &state, sizeof(state));
        // Old driver without .poll reports readiness then read returns EAGAIN.
        // Fail explicitly and retry at the sampler's offline interval, never spin.
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return pirFailure(EPROTO);
        if (n != sizeof(state) || (state != 0 && state != 1))
            return pirFailure(n < 0 ? errno : EIO);
        return PirReading(PirKind::Event, state != 0);
    }
    OutputResult applyOutputs(bool led, bool beep) override
    {
        const int le = writeSwitch(root_ + "/led", led);
        const int be = writeSwitch(root_ + "/beep", beep);
        return OutputResult(le == 0, be == 0, le, be);
    }
private:
    PirReading pirFailure(int error)
    {
        close(pirFd_); pirFd_ = -1;
        return PirReading(PirKind::Offline, false, error);
    }
    std::string root_;
    int pirFd_ = -1;
};
}
std::unique_ptr<Devices> linuxDevices(const std::string &root)
{
    return std::unique_ptr<Devices>(new LinuxDevices(root));
}
}
#else
namespace monitor {
std::unique_ptr<Devices> linuxDevices(const std::string &)
{
    return std::unique_ptr<Devices>();
}
}
#endif
