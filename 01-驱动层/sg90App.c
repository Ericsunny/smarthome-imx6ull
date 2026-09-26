#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "errno.h"
#include "sys/ioctl.h"

/* 与驱动一致: ioctl SG90_CMD_SET_ANGLE _IOW('S', 1, int) 设定角度 0~180 */
#define SG90_CMD_SET_ANGLE _IOW('S', 1, int)

int main(int argc, char *argv[])
{
    int fd, angle;

    if (argc != 3) {
        printf("Usage:\r\n\t./%s /dev/sg90 <angle 0~180>\r\n", argv[0]);
        printf("Example:\r\n\t./%s /dev/sg90 90\r\n", argv[0]);
        return -1;
    }

    angle = atoi(argv[2]);
    if (angle < 0 || angle > 180) {
        printf("angle must be 0~180\r\n");
        return -1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        printf("can't open file %s: %s\r\n", argv[1], strerror(errno));
        return -1;
    }

    if (ioctl(fd, SG90_CMD_SET_ANGLE, &angle) < 0) {
        printf("set angle failed: %s\r\n", strerror(errno));
        close(fd);
        return -1;
    }
    printf("servo angle = %d\r\n", angle);

    close(fd);
    return 0;
}
