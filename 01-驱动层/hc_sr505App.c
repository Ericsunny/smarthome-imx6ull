#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "linux/ioctl.h"


int main(int argc, char *argv[])
{
    int fd;
    int ret = 0;
    char *filename;
    int state;
    if(argc != 2) {
        printf("Error Usage!\r\n");
        return -1;
    }

    filename = argv[1];
    fd = open("/dev/hc_sr505" , O_RDONLY);
    if (fd < 0) {
        printf("can't open file %s\r\n", filename);
        return -1;
    }
    while(1) {
        ret = read(fd, &state, sizeof(state));
        if (ret < 0) {
            printf("read error: %d\n", ret);
            break;
        }
        printf("Motion Detected:%s\r\n", state ? "YES" : "NO");
    }
    close(fd);
    return ret;
}