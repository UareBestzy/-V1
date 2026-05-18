#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define RD03_REPORT_PAYLOAD_LEN 35
#define RD03_FRAME_MAX 128

struct rd03_report {
    int present;
    unsigned int distance_cm;
    uint16_t energy[16];
};

static const uint8_t CMD_OPEN_MODE[] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00,
    0x01, 0x00, 0x04, 0x03, 0x02, 0x01
};

static const uint8_t CMD_SET_REPORT_MODE[] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0x12, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x03,
    0x02, 0x01
};

static const uint8_t CMD_CLOSE_MODE[] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00,
    0x04, 0x03, 0x02, 0x01
};

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -s, --serial DEV       serial device, default: /dev/ttymxc5\n"
        "  -g, --gpio DEV         OT2 gpio device, default: /dev/myrd03\n"
        "  -c, --count N          print N reports then exit, default: unlimited\n"
        "  -m, --set-report-mode  switch RD-03 to report mode before reading\n"
        "  -h, --help             show this help\n",
        prog);
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static double elapsed_seconds(const struct timespec *start,
                  const struct timespec *now)
{
    return (double)(now->tv_sec - start->tv_sec) +
           (double)(now->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static int serial_open(const char *dev)
{
    struct termios tio;
    int fd;

    fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", dev, strerror(errno));
        return -1;
    }

    if (tcgetattr(fd, &tio) < 0) {
        fprintf(stderr, "tcgetattr %s failed: %s\n", dev, strerror(errno));
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        fprintf(stderr, "tcsetattr %s failed: %s\n", dev, strerror(errno));
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

static int write_all(int fd, const uint8_t *buf, size_t len)
{
    size_t done = 0;

    while (done < len) {
        ssize_t ret = write(fd, buf + done, len - done);
        if (ret < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            return -1;
        }
        done += (size_t)ret;
    }

    return 0;
}

static int set_report_mode(int fd)
{
    if (write_all(fd, CMD_OPEN_MODE, sizeof(CMD_OPEN_MODE)) < 0) {
        return -1;
    }
    usleep(100000);
    tcflush(fd, TCIOFLUSH);

    if (write_all(fd, CMD_OPEN_MODE, sizeof(CMD_OPEN_MODE)) < 0) {
        return -1;
    }
    usleep(100000);
    tcflush(fd, TCIFLUSH);

    if (write_all(fd, CMD_SET_REPORT_MODE, sizeof(CMD_SET_REPORT_MODE)) < 0) {
        return -1;
    }
    usleep(100000);
    tcflush(fd, TCIFLUSH);

    if (write_all(fd, CMD_CLOSE_MODE, sizeof(CMD_CLOSE_MODE)) < 0) {
        return -1;
    }
    usleep(100000);
    tcflush(fd, TCIFLUSH);
    return 0;
}

static int read_gpio_present(int fd)
{
    unsigned char data;
    ssize_t ret;

    if (fd < 0) {
        return -1;
    }

    ret = read(fd, &data, 1);
    if (ret != 1) {
        return -1;
    }

    return data ? 1 : 0;
}

static int parse_report_payload(const uint8_t *payload, uint16_t len,
                struct rd03_report *report)
{
    int i;

    if (len != RD03_REPORT_PAYLOAD_LEN) {
        return -1;
    }

    report->present = payload[0] ? 1 : 0;
    report->distance_cm = get_le16(&payload[1]);
    for (i = 0; i < 16; i++) {
        report->energy[i] = get_le16(&payload[3 + i * 2]);
    }

    return 0;
}

static int feed_report_parser(uint8_t byte, struct rd03_report *report)
{
    static uint8_t frame[RD03_FRAME_MAX];
    static size_t pos;
    uint16_t len;
    size_t total;

    if (pos == 0 && byte != 0xF4) {
        return 0;
    }
    if (pos == 1 && byte != 0xF3) {
        pos = (byte == 0xF4) ? 1 : 0;
        return 0;
    }
    if (pos == 2 && byte != 0xF2) {
        pos = 0;
        return 0;
    }
    if (pos == 3 && byte != 0xF1) {
        pos = 0;
        return 0;
    }

    frame[pos++] = byte;
    if (pos < 6) {
        return 0;
    }

    len = get_le16(&frame[4]);
    total = 4 + 2 + len + 4;
    if (total > sizeof(frame)) {
        pos = 0;
        return 0;
    }

    if (pos < total) {
        return 0;
    }

    if (frame[total - 4] != 0xF8 || frame[total - 3] != 0xF7 ||
        frame[total - 2] != 0xF6 || frame[total - 1] != 0xF5) {
        pos = 0;
        return 0;
    }

    pos = 0;
    return parse_report_payload(&frame[6], len, report) == 0 ? 1 : 0;
}

int main(int argc, char *argv[])
{
    const char *serial_dev = "/dev/ttymxc5";
    const char *gpio_dev = "/dev/myrd03";
    int count = 0;
    int set_mode = 0;
    int serial_fd;
    int gpio_fd;
    int printed = 0;
    struct timespec start;

    static const struct option long_opts[] = {
        { "serial", required_argument, NULL, 's' },
        { "gpio", required_argument, NULL, 'g' },
        { "count", required_argument, NULL, 'c' },
        { "set-report-mode", no_argument, NULL, 'm' },
        { "help", no_argument, NULL, 'h' },
        {}
    };

    for (;;) {
        int opt = getopt_long(argc, argv, "s:g:c:mh", long_opts, NULL);
        if (opt == -1) {
            break;
        }

        switch (opt) {
        case 's':
            serial_dev = optarg;
            break;
        case 'g':
            gpio_dev = optarg;
            break;
        case 'c':
            count = atoi(optarg);
            if (count <= 0) {
                usage(argv[0]);
                return 1;
            }
            break;
        case 'm':
            set_mode = 1;
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    serial_fd = serial_open(serial_dev);
    if (serial_fd < 0) {
        return 1;
    }

    gpio_fd = open(gpio_dev, O_RDONLY);
    if (gpio_fd < 0) {
        fprintf(stderr, "warning: open %s failed: %s\n", gpio_dev, strerror(errno));
    }

    if (set_mode && set_report_mode(serial_fd) < 0) {
        fprintf(stderr, "set RD-03 report mode failed: %s\n", strerror(errno));
        close(serial_fd);
        if (gpio_fd >= 0) {
            close(gpio_fd);
        }
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    printf("time_s,present,distance_m,distance_cm,ot2\n");

    while (count == 0 || printed < count) {
        struct pollfd pfd = {
            .fd = serial_fd,
            .events = POLLIN,
        };
        int ret = poll(&pfd, 1, 1000);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "poll %s failed: %s\n", serial_dev, strerror(errno));
            break;
        }

        if (ret == 0) {
            continue;
        }

        if (pfd.revents & POLLIN) {
            uint8_t buf[64];
            ssize_t len = read(serial_fd, buf, sizeof(buf));
            ssize_t i;

            if (len < 0) {
                if (errno == EINTR || errno == EAGAIN) {
                    continue;
                }
                fprintf(stderr, "read %s failed: %s\n", serial_dev, strerror(errno));
                break;
            }

            for (i = 0; i < len; i++) {
                struct rd03_report report;
                int parsed = feed_report_parser(buf[i], &report);

                if (parsed == 1) {
                    struct timespec now;
                    int ot2 = read_gpio_present(gpio_fd);
                    int present = report.present || ot2 == 1;

                    clock_gettime(CLOCK_MONOTONIC, &now);
                    printf("%.2f,%s,%.2f,%u,%s\n",
                           elapsed_seconds(&start, &now),
                           present ? "person" : "nobody",
                           present ? report.distance_cm / 100.0 : 0.0,
                           present ? report.distance_cm : 0,
                           ot2 < 0 ? "unknown" : (ot2 ? "high" : "low"));
                    fflush(stdout);
                    printed++;
                }
            }
        }
    }

    close(serial_fd);
    if (gpio_fd >= 0) {
        close(gpio_fd);
    }
    return 0;
}
