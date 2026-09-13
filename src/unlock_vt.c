#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <linux/vt.h>
#include <unistd.h>
#include <errno.h>

int main() {
    if (geteuid() != 0) {
        fprintf(stderr, "Error: This program needs root.\n");
        return 1;
    }
    int fd = open("/dev/tty0", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot Open /dev/tty0 (tty control)\n");
        return 1;
    }
    if (ioctl(fd, VT_UNLOCKSWITCH) < 0) {
        fprintf(stderr, "Error: Cannot Unlock VT Switch: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}
