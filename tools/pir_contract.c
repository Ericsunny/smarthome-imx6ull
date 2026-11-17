/* Target-only check. Reads GPIO state; never changes outputs or loads modules. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/hc_sr505";
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return 1; }
    struct pollfd p = {fd, POLLIN, 0};
    int state = -1;
    int initial = poll(&p, 1, 1000);
    if (initial != 1 || !(p.revents & POLLIN) ||
        read(fd, &state, sizeof(state)) != sizeof(state) || (state != 0 && state != 1)) {
        fprintf(stderr, "initial snapshot/poll failed: %s\n", strerror(errno));
        close(fd); return 1;
    }
    int second = open(path, O_RDONLY | O_NONBLOCK);
    const int exclusive = second < 0 && errno == EBUSY;
    if (second >= 0) close(second);
    /* Keep the PIR input stable for this test. A real new edge invalidates it. */
    p.revents = 0;
    const int idle = poll(&p, 1, 200);
    int noEvent = read(fd, &state, sizeof(state));
    const int nonblocking = noEvent < 0 && errno == EAGAIN;
    printf("{\"initial_snapshot\":true,\"exclusive\":%s,\"idle_timeout\":%s,"
           "\"empty_read_eagain\":%s}\n", exclusive ? "true" : "false",
           idle == 0 ? "true" : "false", nonblocking ? "true" : "false");
    close(fd);
    return exclusive && idle == 0 && nonblocking ? 0 : 1;
}
