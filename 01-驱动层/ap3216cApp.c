#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"

int main(int argc, char *argv[])
{
    int fd;
    int ret;
    unsigned short data[3];

    if(argc != 2) {printf("Error Usage!\r\n"); return -1;}

    fd = open(argv[1], O_RDWR);
    if (fd < 0) { printf("can't open %s\r\n", argv[1]); return -1;}

    while (1)
    {
        ret = read(fd, data, sizeof(data));
        if(ret == sizeof(data))
            printf("ir = %d, als = %d, ps = %d\r\n", data[0], data[1], data[2]);
        usleep(200000);
    }
    close(fd);
    return 0;
}