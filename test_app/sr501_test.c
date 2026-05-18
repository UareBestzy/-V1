#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [count] [interval_ms] [device]\n", prog);
    fprintf(stderr, "  count  default: 10\n");
    fprintf(stderr, "  interval_ms default: 100\n");
    fprintf(stderr, "  device default: /dev/mysr501\n");
}

int main(int argc, char *argv[])
{
    const char *dev = "/dev/mysr501";
    int count = 10;
    int interval_ms = 100;
    int fd;
    int i;
    unsigned char data;
    ssize_t len;

    if (argc > 4) {
        usage(argv[0]);
        return 1;
    }

    if (argc >= 2) {
        count = atoi(argv[1]);
        if (count <= 0) {
            usage(argv[0]);
            return 1;
        }
    }

    if (argc == 3) {
        interval_ms = atoi(argv[2]);
        if (interval_ms <= 0) {
            usage(argv[0]);
            return 1;
        }
    }

    if (argc == 4) {
        interval_ms = atoi(argv[2]);
        if (interval_ms <= 0) {
            usage(argv[0]);
            return 1;
        }
        dev = argv[3];
    }

    fd = open(dev, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", dev, strerror(errno));
        return 1;
    }

    for (i = 0; i < count; i++) {
        len = read(fd, &data, 1);
        if (len != 1) {
            fprintf(stderr, "read %s failed: %s\n", dev, strerror(errno));
            close(fd);
            return 1;
        }

        printf("[%d] sr501 status: %s\n", i + 1, data ? "person move" : "nobody");
        usleep(interval_ms * 1000);
    }

    close(fd);
    return 0;
}
