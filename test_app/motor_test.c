#ifndef DEFAULT_MOTOR_DEVICE
#define DEFAULT_MOTOR_DEVICE "/dev/motor"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <1|2> [device]\n", prog);
    fprintf(stderr, "  1: one direction (actual direction depends on wiring)\n");
    fprintf(stderr, "  2: opposite direction (actual direction depends on wiring)\n");
    fprintf(stderr, "  device default: /dev/motor\n");
}

int main(int argc, char *argv[])
{
    const char *dev = DEFAULT_MOTOR_DEVICE;
    unsigned char cmd;
    int fd;
    ssize_t written;

    if (argc != 2 && argc != 3) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "1") == 0) {
        cmd = 1;
    } else if (strcmp(argv[1], "2") == 0) {
        cmd = 2;
    } else {
        usage(argv[0]);
        return 1;
    }

    if (argc == 3) {
        dev = argv[2];
    }

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", dev, strerror(errno));
        return 1;
    }

    written = write(fd, &cmd, 1);
    if (written != 1) {
        fprintf(stderr, "write %s failed: %s\n", dev, strerror(errno));
        close(fd);
        return 1;
    }

    printf("motor command %u sent to %s\n", cmd, dev);
    close(fd);
    return 0;
}
