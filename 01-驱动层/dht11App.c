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
    unsigned char data[2];

    if(argc != 2) {printf("Error Usage!\r\n"); return -1;}

    fd = open("/dev/dht11", O_RDONLY);
    if (fd < 0) { printf("can't open %s\r\n", argv[1]); return -1;}

    while (1)
    {
        ret = read(fd, data, 2);
        if(ret == 2)
            printf("humidity = %d%%  temperature= %doc\r\n", data[0], data[1]);
        else
            printf("read failed: %d\r\n", ret);
        sleep(2);
    }
    close(fd);
    return 0;
}