#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "errno.h"
#include "sys/ioctl.h"

/* 与驱动一致: ioctl(cmd=0, int*) 返回距离, 单位 cm */
#define HC_SR04_CMD_GET_DIST 0

int main(int argc, char *argv[])
{
    int fd, ret, dist;

    if (argc != 2) {
        printf("Usage:\r\n\t./%s /dev/hc_sr04\r\n", argv[0]);
        return -1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        printf("can't open file %s: %s\r\n", argv[1], strerror(errno));
        return -1;
    }

    while (1) {
        ret = ioctl(fd, HC_SR04_CMD_GET_DIST, &dist);
        if (ret < 0)
            printf("get distance failed: %s\r\n", strerror(errno));
        else
            printf("distance = %d cm\r\n", dist);
        sleep(1);   /* 模块要求两次测量间隔 > 60ms */
    }

    close(fd);
    return 0;
}
