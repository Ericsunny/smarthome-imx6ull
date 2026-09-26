#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "errno.h"
#include "sys/ioctl.h"

/* 与驱动一致: ioctl _IOW('T',1,int) 带符号步数
 * 正=正转(开窗帘) 负=反转(关窗帘), 半步 8 拍, 4096 拍=一圈 */
#define STEP_CMD_RUN _IOW('T', 1, int)

int main(int argc, char *argv[])
{
    int fd, steps;

    if (argc != 3) {
        printf("Usage:\r\n\t./%s /dev/step28byj48 <steps>\r\n", argv[0]);
        printf("  正数=正转(开窗帘)  负数=反转(关窗帘)  4096 拍=一圈约 8 秒\r\n");
        printf("Example:\r\n\t./%s /dev/step28byj48 1024\r\n\t./%s /dev/step28byj48 -4096\r\n",
               argv[0], argv[0]);
        return -1;
    }

    steps = atoi(argv[2]);

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        printf("can't open file %s: %s\r\n", argv[1], strerror(errno));
        return -1;
    }

    printf("stepping %d ... (约 %d 秒)\r\n", steps, abs(steps) * 2 / 1000);
    if (ioctl(fd, STEP_CMD_RUN, &steps) < 0) {
        printf("run steps failed: %s\r\n", strerror(errno));
        close(fd);
        return -1;
    }
    printf("done\r\n");

    close(fd);
    return 0;
}
