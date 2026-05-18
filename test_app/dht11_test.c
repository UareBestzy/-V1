#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [count] [interval_ms] [device]\n", prog);
    fprintf(stderr, "  count default: 1\n");
    fprintf(stderr, "  interval_ms default: 2000\n");
    fprintf(stderr, "  device default: /dev/querydht11\n");
}

int main(int argc, char *argv[])
{
    const char *dev = "/dev/querydht11";
    int count = 1;
    int interval_ms = 2000;
    unsigned char data[4];
    int fd;
    int i;

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

    if (argc >= 3) {
        interval_ms = atoi(argv[2]);
        if (interval_ms <= 0) {
            usage(argv[0]);
            return 1;
        }
    }

    if (argc == 4)
        dev = argv[3];

    fd = open(dev, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", dev, strerror(errno));
        return 1;
    }

    for (i = 0; i < count; i++) {
        ssize_t len = read(fd, data, sizeof(data));

        if (len != (ssize_t)sizeof(data)) {
            fprintf(stderr, "read %s failed: %s\n", dev, strerror(errno));
            close(fd);
            return 1;
        }

        printf("[%d] humidity=%u.%u%% temperature=%u.%uC\n",
               i + 1, data[0], data[1], data[2], data[3]);

        if (i + 1 < count)
            usleep(interval_ms * 1000);
    }

    close(fd);
    return 0;
}
