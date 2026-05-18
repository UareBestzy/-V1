#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <1|2> <0-3> [device]\n", prog);
    fprintf(stderr, "  dir 1: forward\n");
    fprintf(stderr, "  dir 2: reverse\n");
    fprintf(stderr, "  speed 0: stop, 1: low, 2: medium, 3: high\n");
    fprintf(stderr, "  device default: /dev/fanmotor\n");
}

int main(int argc, char *argv[])
{
    const char *dev = "/dev/fanmotor";
    unsigned char cmd[2];
    int fd;
    ssize_t written;

    if (argc != 3 && argc != 4) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "1") == 0) {
        cmd[0] = 1;
    } else if (strcmp(argv[1], "2") == 0) {
        cmd[0] = 2;
    } else {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[2], "0") == 0) {
        cmd[1] = 0;
    } else if (strcmp(argv[2], "1") == 0) {
        cmd[1] = 1;
    } else if (strcmp(argv[2], "2") == 0) {
        cmd[1] = 2;
    } else if (strcmp(argv[2], "3") == 0) {
        cmd[1] = 3;
    } else {
        usage(argv[0]);
        return 1;
    }

    if (argc == 4) {
        dev = argv[3];
    }

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", dev, strerror(errno));
        return 1;
    }

    written = write(fd, cmd, sizeof(cmd));
    if (written != (ssize_t)sizeof(cmd)) {
        fprintf(stderr, "write %s failed: %s\n", dev, strerror(errno));
        close(fd);
        return 1;
    }

    printf("fan command dir=%u speed=%u sent to %s\n", cmd[0], cmd[1], dev);
    close(fd);
    return 0;
}
