#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_CONFIG_ETC "/etc/huawei_cloud.conf"
#define DEFAULT_CONFIG_LOCAL "./huawei_cloud.conf"

#define DEFAULT_HUAWEI_HOST "46a22f6f08.st1.iotda-device.cn-north-4.myhuaweicloud.com"
#define DEFAULT_HUAWEI_DEVICE_ID "6a082ae5cbb0cf6bb95b6975_imx6ull"
#define DEFAULT_HUAWEI_DEVICE_SECRET "wobujidemima1"
#define DEFAULT_HUAWEI_PORT 8883

#define DHT_DEVICE "/dev/querydht11"
#define SR501_DEVICE "/dev/mysr501"
#define RD03_GPIO_DEVICE "/dev/myrd03"
#define RD03_SERIAL_DEVICE "/dev/ttymxc5"
#define FAN_DEVICE "/dev/fanmotor"
#define SERVO_DEVICE "/dev/sg90"

#define MQTT_PKT_CONNECT 0x10
#define MQTT_PKT_CONNACK 0x20
#define MQTT_PKT_PUBLISH 0x30
#define MQTT_PKT_PUBACK 0x40
#define MQTT_PKT_SUBSCRIBE 0x82
#define MQTT_PKT_SUBACK 0x90
#define MQTT_PKT_PINGREQ 0xC0
#define MQTT_PKT_PINGRESP 0xD0
#define MQTT_PKT_DISCONNECT 0xE0

#define RD03_FRAME_MAX 128

static volatile sig_atomic_t g_running = 1;

struct Config {
    char server_address[256];
    char device_id[128];
    char device_secret[256];
    char service_id[64];
    char host[256];
    int port;
    int use_tls;
    int connect_timeout_ms;
    int report_interval;
    int keepalive;
    char property_up_topic[256];
    char property_down_topic[256];
    char dht_device[128];
    char sr501_device[128];
    char rd03_gpio_device[128];
    char rd03_serial_device[128];
    char fan_device[128];
    char servo_device[128];
};

struct Transport {
    int fd;
    int use_tls;
    SSL_CTX *ssl_ctx;
    SSL *ssl;
};

struct DeviceFds {
    int dht;
    int sr501;
    int rd03_gpio;
    int rd03_serial;
    int fan;
    int servo;
};

struct Rd03Parser {
    uint8_t frame[RD03_FRAME_MAX];
    int pos;
};

struct SensorData {
    double temperature;
    double humidity;
    int dht_valid;
    int sr501_present;
    int rd03_gpio_present;
    int rd03_valid;
    int rd03_present;
    unsigned int rd03_distance_cm;
    int fan_speed;
    int curtain_open;
};

static void log_msg(const char *level, const char *fmt, ...)
{
    va_list ap;
    char timebuf[32];
    time_t now = time(NULL);
    struct tm tm_now;

    localtime_r(&now, &tm_now);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm_now);
    fprintf(stderr, "[%s] %s ", timebuf, level);

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void on_signal(int signo)
{
    (void)signo;
    g_running = 0;
}

static void trim(char *s)
{
    char *p = s;
    char *end;

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }

    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
}

static void config_defaults(struct Config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->server_address, sizeof(cfg->server_address), "%s", DEFAULT_HUAWEI_HOST);
    snprintf(cfg->device_id, sizeof(cfg->device_id), "%s", DEFAULT_HUAWEI_DEVICE_ID);
    snprintf(cfg->device_secret, sizeof(cfg->device_secret), "%s", DEFAULT_HUAWEI_DEVICE_SECRET);
    snprintf(cfg->host, sizeof(cfg->host), "%s", DEFAULT_HUAWEI_HOST);
    cfg->port = DEFAULT_HUAWEI_PORT;
    cfg->use_tls = 1;
    cfg->connect_timeout_ms = 8000;
    cfg->report_interval = 1;
    cfg->keepalive = 120;
    snprintf(cfg->service_id, sizeof(cfg->service_id), "%s", "smart_home");
    snprintf(cfg->dht_device, sizeof(cfg->dht_device), "%s", DHT_DEVICE);
    snprintf(cfg->sr501_device, sizeof(cfg->sr501_device), "%s", SR501_DEVICE);
    snprintf(cfg->rd03_gpio_device, sizeof(cfg->rd03_gpio_device), "%s", RD03_GPIO_DEVICE);
    snprintf(cfg->rd03_serial_device, sizeof(cfg->rd03_serial_device), "%s", RD03_SERIAL_DEVICE);
    snprintf(cfg->fan_device, sizeof(cfg->fan_device), "%s", FAN_DEVICE);
    snprintf(cfg->servo_device, sizeof(cfg->servo_device), "%s", SERVO_DEVICE);
}

static void set_config_value(struct Config *cfg, const char *key, const char *value)
{
#define SET_STR(name) do { snprintf(cfg->name, sizeof(cfg->name), "%s", value); } while (0)
    if (strcmp(key, "server_address") == 0) SET_STR(server_address);
    else if (strcmp(key, "device_id") == 0) SET_STR(device_id);
    else if (strcmp(key, "device_secret") == 0) SET_STR(device_secret);
    else if (strcmp(key, "service_id") == 0) SET_STR(service_id);
    else if (strcmp(key, "host") == 0) SET_STR(host);
    else if (strcmp(key, "port") == 0) cfg->port = atoi(value);
    else if (strcmp(key, "use_tls") == 0) cfg->use_tls = atoi(value);
    else if (strcmp(key, "connect_timeout_ms") == 0) cfg->connect_timeout_ms = atoi(value);
    else if (strcmp(key, "report_interval") == 0) cfg->report_interval = atoi(value);
    else if (strcmp(key, "keepalive") == 0) cfg->keepalive = atoi(value);
    else if (strcmp(key, "property_up_topic") == 0) SET_STR(property_up_topic);
    else if (strcmp(key, "property_down_topic") == 0) SET_STR(property_down_topic);
    else if (strcmp(key, "dht_device") == 0) SET_STR(dht_device);
    else if (strcmp(key, "sr501_device") == 0) SET_STR(sr501_device);
    else if (strcmp(key, "rd03_gpio_device") == 0) SET_STR(rd03_gpio_device);
    else if (strcmp(key, "rd03_serial_device") == 0) SET_STR(rd03_serial_device);
    else if (strcmp(key, "fan_device") == 0) SET_STR(fan_device);
    else if (strcmp(key, "servo_device") == 0) SET_STR(servo_device);
#undef SET_STR
}

static void finalize_config(struct Config *cfg)
{
    if (cfg->host[0] == '\0' && cfg->server_address[0] != '\0') {
        snprintf(cfg->host, sizeof(cfg->host), "%s", cfg->server_address);
    }
    if (cfg->property_up_topic[0] == '\0') {
        snprintf(cfg->property_up_topic, sizeof(cfg->property_up_topic),
                 "$oc/devices/%s/sys/properties/report", cfg->device_id);
    }
    if (cfg->property_down_topic[0] == '\0') {
        snprintf(cfg->property_down_topic, sizeof(cfg->property_down_topic),
                 "$oc/devices/%s/sys/properties/set/#", cfg->device_id);
    }
    if (cfg->report_interval <= 0) {
        cfg->report_interval = 1;
    }
    if (cfg->keepalive <= 0) {
        cfg->keepalive = 120;
    }
    if (cfg->connect_timeout_ms <= 0) {
        cfg->connect_timeout_ms = 8000;
    }
    if (cfg->port == 8883 && cfg->use_tls == 0) {
        cfg->use_tls = 1;
    }
}

static int load_config_file(struct Config *cfg, const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[512];
    int lineno = 0;

    if (!fp) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *eq;
        char *comment;
        ++lineno;
        comment = strchr(line, '#');
        if (comment) {
            *comment = '\0';
        }
        trim(line);
        if (line[0] == '\0') {
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) {
            log_msg("WARN", "忽略配置文件第 %d 行：缺少 '='", lineno);
            continue;
        }
        *eq = '\0';
        trim(line);
        trim(eq + 1);
        set_config_value(cfg, line, eq + 1);
    }
    fclose(fp);
    finalize_config(cfg);
    return 0;
}

static int load_config(struct Config *cfg, const char *path)
{
    config_defaults(cfg);
    if (path) {
        if (load_config_file(cfg, path) < 0) {
            log_msg("ERROR", "读取配置文件失败：%s", path);
            return -1;
        }
        return 0;
    }
    if (load_config_file(cfg, DEFAULT_CONFIG_ETC) == 0) {
        return 0;
    }
    if (load_config_file(cfg, DEFAULT_CONFIG_LOCAL) == 0) {
        return 0;
    }
    finalize_config(cfg);
    log_msg("INFO", "未找到配置文件，使用源码内置华为云连接参数");
    return 0;
}

static int validate_config(const struct Config *cfg)
{
    if (cfg->device_id[0] == '\0' ||
        cfg->device_secret[0] == '\0' || cfg->host[0] == '\0') {
        log_msg("ERROR", "配置文件必须填写 server_address、device_id 和 device_secret");
        return -1;
    }
    return 0;
}

static void hex_encode(const uint8_t *in, size_t len, char *out, size_t out_size)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (out_size == 0) {
        return;
    }
    for (i = 0; i < len && (i * 2 + 1) < out_size; ++i) {
        out[i * 2] = hex[in[i] >> 4];
        out[i * 2 + 1] = hex[in[i] & 0x0f];
    }
    out[i * 2] = '\0';
}

static int make_huawei_auth(const struct Config *cfg,
                            char *client_id, size_t client_id_size,
                            char *username, size_t username_size,
                            char *password, size_t password_size,
                            char *timestamp_out, size_t timestamp_out_size)
{
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    char timestamp[16];
    time_t now = time(NULL);
    struct tm tm_utc;

    gmtime_r(&now, &tm_utc);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d%H", &tm_utc);

    snprintf(client_id, client_id_size, "%s_0_0_%s", cfg->device_id, timestamp);
    snprintf(username, username_size, "%s", cfg->device_id);
    if (timestamp_out_size > 0) {
        snprintf(timestamp_out, timestamp_out_size, "%s", timestamp);
    }

    if (!HMAC(EVP_sha256(), timestamp, strlen(timestamp),
              (const unsigned char *)cfg->device_secret, strlen(cfg->device_secret),
              digest, &digest_len)) {
        return -1;
    }

    hex_encode(digest, digest_len, password, password_size);
    return 0;
}

static int connect_with_timeout(int fd, const struct sockaddr *addr,
                                socklen_t addrlen, int timeout_ms)
{
    int flags = fcntl(fd, F_GETFL, 0);
    int err = 0;
    socklen_t errlen = sizeof(err);
    struct pollfd pfd;

    if (flags < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }

    if (connect(fd, addr, addrlen) == 0) {
        fcntl(fd, F_SETFL, flags);
        return 0;
    }
    if (errno != EINPROGRESS) {
        return -1;
    }

    pfd.fd = fd;
    pfd.events = POLLOUT;
    if (poll(&pfd, 1, timeout_ms) <= 0) {
        errno = ETIMEDOUT;
        return -1;
    }
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0) {
        return -1;
    }
    if (err != 0) {
        errno = err;
        return -1;
    }

    fcntl(fd, F_SETFL, flags);
    return 0;
}

static int connect_tcp(const char *host, int port, int timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *p;
    char portstr[16];
    int fd = -1;
    int last_errno = 0;
    int gai_ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);

    gai_ret = getaddrinfo(host, portstr, &hints, &res);
    if (gai_ret != 0) {
        log_msg("ERROR", "DNS 解析失败 %s:%s：%s", host, portstr, gai_strerror(gai_ret));
        errno = EHOSTUNREACH;
        return -1;
    }

    for (p = res; p; p = p->ai_next) {
        char addrbuf[NI_MAXHOST] = "?";
        char servbuf[NI_MAXSERV] = "?";

        getnameinfo(p->ai_addr, p->ai_addrlen,
                    addrbuf, sizeof(addrbuf), servbuf, sizeof(servbuf),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        log_msg("INFO", "尝试 TCP 连接 %s:%s，超时 %d ms", addrbuf, servbuf, timeout_ms);

        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) {
            last_errno = errno;
            continue;
        }
        if (connect_with_timeout(fd, p->ai_addr, p->ai_addrlen, timeout_ms) == 0) {
            break;
        }
        last_errno = errno;
        log_msg("WARN", "TCP 连接 %s:%s 失败：%s", addrbuf, servbuf, strerror(errno));
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    if (fd < 0 && last_errno != 0) {
        errno = last_errno;
    }
    return fd;
}

static void log_ssl_error(const char *what)
{
    char errbuf[256];
    unsigned long err = ERR_get_error();

    if (err == 0) {
        log_msg("ERROR", "%s 失败", what);
        return;
    }
    ERR_error_string_n(err, errbuf, sizeof(errbuf));
    log_msg("ERROR", "%s 失败：%s", what, errbuf);
}

static int wait_fd_event(int fd, short events, int timeout_ms)
{
    struct pollfd pfd;

    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    if (poll(&pfd, 1, timeout_ms) <= 0) {
        return -1;
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        return -1;
    }
    return 0;
}

static void transport_close(struct Transport *transport)
{
    if (transport->ssl) {
        SSL_shutdown(transport->ssl);
        SSL_free(transport->ssl);
        transport->ssl = NULL;
    }
    if (transport->ssl_ctx) {
        SSL_CTX_free(transport->ssl_ctx);
        transport->ssl_ctx = NULL;
    }
    if (transport->fd >= 0) {
        close(transport->fd);
        transport->fd = -1;
    }
}

static int transport_connect(struct Transport *transport, const struct Config *cfg)
{
    memset(transport, 0, sizeof(*transport));
    transport->fd = -1;
    transport->use_tls = cfg->use_tls;

    transport->fd = connect_tcp(cfg->host, cfg->port, cfg->connect_timeout_ms);
    if (transport->fd < 0) {
        return -1;
    }

    if (!transport->use_tls) {
        return 0;
    }

    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    transport->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!transport->ssl_ctx) {
        log_ssl_error("创建 TLS 上下文");
        transport_close(transport);
        return -1;
    }

    SSL_CTX_set_verify(transport->ssl_ctx, SSL_VERIFY_NONE, NULL);
    transport->ssl = SSL_new(transport->ssl_ctx);
    if (!transport->ssl) {
        log_ssl_error("创建 TLS 会话");
        transport_close(transport);
        return -1;
    }

    if (SSL_set_tlsext_host_name(transport->ssl, cfg->host) != 1) {
        log_ssl_error("设置 TLS SNI");
        transport_close(transport);
        return -1;
    }
    if (SSL_set_fd(transport->ssl, transport->fd) != 1) {
        log_ssl_error("绑定 TLS 套接字");
        transport_close(transport);
        return -1;
    }
    if (SSL_connect(transport->ssl) != 1) {
        log_ssl_error("TLS 握手");
        transport_close(transport);
        return -1;
    }

    log_msg("INFO", "TLS 握手成功");
    return 0;
}

static int transport_write_all(struct Transport *transport, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t done = 0;

    while (done < len) {
        ssize_t n;

        if (transport->use_tls) {
            int ret = SSL_write(transport->ssl, p + done, (int)(len - done));
            if (ret <= 0) {
                int ssl_err = SSL_get_error(transport->ssl, ret);
                if (ssl_err == SSL_ERROR_WANT_READ) {
                    if (wait_fd_event(transport->fd, POLLIN, 5000) == 0) {
                        continue;
                    }
                    log_msg("ERROR", "TLS 写入等待可读超时");
                    return -1;
                }
                if (ssl_err == SSL_ERROR_WANT_WRITE) {
                    if (wait_fd_event(transport->fd, POLLOUT, 5000) == 0) {
                        continue;
                    }
                    log_msg("ERROR", "TLS 写入等待可写超时");
                    return -1;
                }
                if (ssl_err == SSL_ERROR_ZERO_RETURN) {
                    log_msg("ERROR", "TLS 写入时连接已关闭");
                    return -1;
                }
                log_ssl_error("TLS 写入");
                return -1;
            }
            n = ret;
        } else {
            n = write(transport->fd, p + done, len - done);
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

static int transport_read_exact_timeout(struct Transport *transport, void *data,
                                        size_t len, int timeout_ms)
{
    uint8_t *p = (uint8_t *)data;
    size_t done = 0;

    while (done < len) {
        ssize_t n;
        if (transport->use_tls) {
            int ret = SSL_read(transport->ssl, p + done, (int)(len - done));
            if (ret <= 0) {
                int ssl_err = SSL_get_error(transport->ssl, ret);
                if (ssl_err == SSL_ERROR_WANT_READ) {
                    if (wait_fd_event(transport->fd, POLLIN, timeout_ms) == 0) {
                        continue;
                    }
                    log_msg("ERROR", "TLS 读取等待可读超时");
                    return -1;
                }
                if (ssl_err == SSL_ERROR_WANT_WRITE) {
                    if (wait_fd_event(transport->fd, POLLOUT, timeout_ms) == 0) {
                        continue;
                    }
                    return -1;
                }
                if (ssl_err == SSL_ERROR_ZERO_RETURN) {
                    log_msg("ERROR", "TLS 读取时连接已关闭");
                    return -1;
                }
                log_msg("ERROR", "TLS 读取失败，ssl_error=%d", ssl_err);
                log_ssl_error("TLS 读取");
                return -1;
            }
            n = ret;
        } else {
            if (wait_fd_event(transport->fd, POLLIN, timeout_ms) < 0) {
                return -1;
            }
            n = read(transport->fd, p + done, len - done);
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

static int mqtt_write_remaining(uint8_t *buf, size_t *pos, size_t remaining)
{
    do {
        uint8_t encoded = remaining % 128;
        remaining /= 128;
        if (remaining > 0) {
            encoded |= 128;
        }
        buf[(*pos)++] = encoded;
    } while (remaining > 0);
    return 0;
}

static int mqtt_write_string(uint8_t *buf, size_t buf_size, size_t *pos, const char *s)
{
    size_t len = strlen(s);

    if (len > 65535 || *pos + 2 + len > buf_size) {
        return -1;
    }
    buf[(*pos)++] = (uint8_t)(len >> 8);
    buf[(*pos)++] = (uint8_t)(len & 0xff);
    memcpy(buf + *pos, s, len);
    *pos += len;
    return 0;
}

static int mqtt_send_connect(struct Transport *transport, const char *client_id, const char *username,
                             const char *password, int keepalive)
{
    uint8_t pkt[2048];
    uint8_t vh_payload[1800];
    size_t p = 0;
    size_t q = 0;

    if (mqtt_write_string(vh_payload, sizeof(vh_payload), &q, "MQTT") < 0) return -1;
    vh_payload[q++] = 4;
    vh_payload[q++] = 0xC2;
    vh_payload[q++] = (uint8_t)(keepalive >> 8);
    vh_payload[q++] = (uint8_t)(keepalive & 0xff);
    if (mqtt_write_string(vh_payload, sizeof(vh_payload), &q, client_id) < 0) return -1;
    if (mqtt_write_string(vh_payload, sizeof(vh_payload), &q, username) < 0) return -1;
    if (mqtt_write_string(vh_payload, sizeof(vh_payload), &q, password) < 0) return -1;

    pkt[p++] = MQTT_PKT_CONNECT;
    mqtt_write_remaining(pkt, &p, q);
    memcpy(pkt + p, vh_payload, q);
    p += q;
    return transport_write_all(transport, pkt, p);
}

static int mqtt_read_packet(struct Transport *transport, uint8_t *type, uint8_t *payload,
                            size_t payload_size, size_t *payload_len, int timeout_ms)
{
    uint8_t header;
    size_t multiplier = 1;
    size_t remaining = 0;
    uint8_t encoded;

    if (transport_read_exact_timeout(transport, &header, 1, timeout_ms) < 0) {
        return -1;
    }

    do {
        if (transport_read_exact_timeout(transport, &encoded, 1, timeout_ms) < 0) {
            return -1;
        }
        remaining += (encoded & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128 * 128 * 128) {
            return -1;
        }
    } while (encoded & 128);

    if (remaining > payload_size) {
        return -1;
    }
    if (transport_read_exact_timeout(transport, payload, remaining, timeout_ms) < 0) {
        return -1;
    }

    *type = header;
    *payload_len = remaining;
    return 0;
}

static int mqtt_expect_connack(struct Transport *transport)
{
    uint8_t type;
    uint8_t payload[8];
    size_t len;

    if (mqtt_read_packet(transport, &type, payload, sizeof(payload), &len, 15000) < 0) {
        log_msg("ERROR", "等待 MQTT CONNACK 超时或读取失败");
        return -1;
    }
    if ((type & 0xF0) != MQTT_PKT_CONNACK || len < 2 || payload[1] != 0) {
        log_msg("ERROR", "MQTT 连接确认失败，type=0x%02x code=%u", type, len >= 2 ? payload[1] : 255);
        return -1;
    }
    return 0;
}

static int mqtt_send_subscribe(struct Transport *transport, const char *topic, uint16_t packet_id)
{
    uint8_t pkt[1024];
    uint8_t body[900];
    size_t p = 0;
    size_t q = 0;

    body[q++] = (uint8_t)(packet_id >> 8);
    body[q++] = (uint8_t)(packet_id & 0xff);
    if (mqtt_write_string(body, sizeof(body), &q, topic) < 0) return -1;
    body[q++] = 0;

    pkt[p++] = MQTT_PKT_SUBSCRIBE;
    mqtt_write_remaining(pkt, &p, q);
    memcpy(pkt + p, body, q);
    p += q;
    return transport_write_all(transport, pkt, p);
}

static int mqtt_send_publish_qos0(struct Transport *transport, const char *topic, const char *payload)
{
    uint8_t pkt[2048];
    size_t p = 0;
    size_t payload_len = strlen(payload);
    size_t remaining = 2 + strlen(topic) + payload_len;

    if (remaining + 5 > sizeof(pkt)) {
        return -1;
    }
    pkt[p++] = MQTT_PKT_PUBLISH;
    mqtt_write_remaining(pkt, &p, remaining);
    if (mqtt_write_string(pkt, sizeof(pkt), &p, topic) < 0) return -1;
    memcpy(pkt + p, payload, payload_len);
    p += payload_len;
    return transport_write_all(transport, pkt, p);
}

static int mqtt_send_ping(struct Transport *transport)
{
    uint8_t pkt[2] = { MQTT_PKT_PINGREQ, 0 };
    return transport_write_all(transport, pkt, sizeof(pkt));
}

static void mqtt_send_disconnect(struct Transport *transport)
{
    uint8_t pkt[2] = { MQTT_PKT_DISCONNECT, 0 };
    transport_write_all(transport, pkt, sizeof(pkt));
}

static uint16_t le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int open_rd03_serial(const char *path)
{
    struct termios tio;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (fd < 0) {
        return -1;
    }
    if (tcgetattr(fd, &tio) < 0) {
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
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

static void open_devices(const struct Config *cfg, struct DeviceFds *fds)
{
    fds->dht = open(cfg->dht_device, O_RDONLY);
    fds->sr501 = open(cfg->sr501_device, O_RDONLY);
    fds->rd03_gpio = open(cfg->rd03_gpio_device, O_RDONLY);
    fds->rd03_serial = open_rd03_serial(cfg->rd03_serial_device);
    fds->fan = open(cfg->fan_device, O_RDWR);
    fds->servo = open(cfg->servo_device, O_RDWR);

    log_msg("INFO", "设备节点 dht=%s sr501=%s rd03_gpio=%s rd03_uart=%s fan=%s servo=%s",
            fds->dht >= 0 ? "正常" : "失败",
            fds->sr501 >= 0 ? "正常" : "失败",
            fds->rd03_gpio >= 0 ? "正常" : "失败",
            fds->rd03_serial >= 0 ? "正常" : "失败",
            fds->fan >= 0 ? "正常" : "失败",
            fds->servo >= 0 ? "正常" : "失败");
}

static void close_devices(struct DeviceFds *fds)
{
    if (fds->dht >= 0) close(fds->dht);
    if (fds->sr501 >= 0) close(fds->sr501);
    if (fds->rd03_gpio >= 0) close(fds->rd03_gpio);
    if (fds->rd03_serial >= 0) close(fds->rd03_serial);
    if (fds->fan >= 0) close(fds->fan);
    if (fds->servo >= 0) close(fds->servo);
    memset(fds, -1, sizeof(*fds));
}

static int read_binary_fd(int fd)
{
    uint8_t value;

    if (fd < 0) {
        return -1;
    }
    if (lseek(fd, 0, SEEK_SET) < 0 && errno != ESPIPE) {
    }
    if (read(fd, &value, 1) != 1) {
        return -1;
    }
    return value ? 1 : 0;
}

static int read_dht(int fd, double *humidity, double *temperature)
{
    uint8_t data[4];

    if (fd < 0) {
        return -1;
    }
    if (lseek(fd, 0, SEEK_SET) < 0 && errno != ESPIPE) {
    }
    if (read(fd, data, sizeof(data)) != (ssize_t)sizeof(data)) {
        return -1;
    }
    *humidity = (double)data[0] + (double)data[1] / 10.0;
    *temperature = (double)data[2] + (double)data[3] / 10.0;
    return 0;
}

static int feed_rd03(struct Rd03Parser *parser, uint8_t byte,
                     int *present, unsigned int *distance_cm)
{
    uint16_t payload_len;
    int total;

    if (parser->pos == 0 && byte != 0xF4) return 0;
    if (parser->pos == 1 && byte != 0xF3) {
        parser->pos = (byte == 0xF4) ? 1 : 0;
        return 0;
    }
    if (parser->pos == 2 && byte != 0xF2) {
        parser->pos = 0;
        return 0;
    }
    if (parser->pos == 3 && byte != 0xF1) {
        parser->pos = 0;
        return 0;
    }
    if (parser->pos >= RD03_FRAME_MAX) {
        parser->pos = 0;
        return 0;
    }

    parser->frame[parser->pos++] = byte;
    if (parser->pos < 6) return 0;

    payload_len = le16(&parser->frame[4]);
    total = 4 + 2 + payload_len + 4;
    if (total > RD03_FRAME_MAX) {
        parser->pos = 0;
        return 0;
    }
    if (parser->pos < total) return 0;

    if (parser->frame[total - 4] != 0xF8 || parser->frame[total - 3] != 0xF7 ||
        parser->frame[total - 2] != 0xF6 || parser->frame[total - 1] != 0xF5) {
        parser->pos = 0;
        return 0;
    }

    if (payload_len == 35) {
        const uint8_t *payload = &parser->frame[6];
        *present = payload[0] ? 1 : 0;
        *distance_cm = le16(&payload[1]);
        parser->pos = 0;
        return 1;
    }

    parser->pos = 0;
    return 0;
}

static void read_rd03_serial(int fd, struct Rd03Parser *parser, struct SensorData *data)
{
    uint8_t buf[128];

    if (fd < 0) {
        return;
    }

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        ssize_t i;
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                break;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        for (i = 0; i < n; ++i) {
            int present;
            unsigned int distance;
            if (feed_rd03(parser, buf[i], &present, &distance)) {
                data->rd03_valid = 1;
                data->rd03_present = present;
                data->rd03_distance_cm = distance;
            }
        }
    }
}

static void read_sensors(const struct DeviceFds *fds, struct Rd03Parser *parser,
                         struct SensorData *data)
{
    double h;
    double t;
    int v;

    if (read_dht(fds->dht, &h, &t) == 0) {
        data->dht_valid = 1;
        data->humidity = h;
        data->temperature = t;
    } else {
        data->dht_valid = 0;
    }

    v = read_binary_fd(fds->sr501);
    if (v >= 0) data->sr501_present = v;

    v = read_binary_fd(fds->rd03_gpio);
    if (v >= 0) data->rd03_gpio_present = v;

    read_rd03_serial(fds->rd03_serial, parser, data);
}

static int write_fan(const struct DeviceFds *fds, int speed)
{
    uint8_t cmd[2];

    if (fds->fan < 0 || speed < 0 || speed > 3) {
        return -1;
    }
    cmd[0] = 1;
    cmd[1] = (uint8_t)speed;
    if (write(fds->fan, cmd, sizeof(cmd)) != (ssize_t)sizeof(cmd)) {
        return -1;
    }
    return 0;
}

static int write_servo(const struct DeviceFds *fds, int open_value)
{
    uint8_t cmd;

    if (fds->servo < 0) {
        return -1;
    }
    cmd = open_value ? 1 : 0;
    if (write(fds->servo, &cmd, 1) != 1) {
        return -1;
    }
    return 0;
}

static int json_get_int(const char *json, const char *key, int *value)
{
    char pattern[64];
    const char *p;
    char *end;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p) {
        return -1;
    }
    p = strchr(p + strlen(pattern), ':');
    if (!p) {
        return -1;
    }
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '"') {
        ++p;
    }
    if (strncmp(p, "true", 4) == 0) {
        *value = 1;
        return 0;
    }
    if (strncmp(p, "false", 5) == 0) {
        *value = 0;
        return 0;
    }
    errno = 0;
    *value = (int)strtol(p, &end, 10);
    if (errno || end == p) {
        return -1;
    }
    return 0;
}

static void handle_downlink(const struct DeviceFds *fds, struct SensorData *data,
                            const char *payload)
{
    int value;

    if (json_get_int(payload, "fan_speed", &value) == 0) {
        if (value >= 0 && value <= 3 && write_fan(fds, value) == 0) {
            data->fan_speed = value;
            log_msg("INFO", "云端设置 fan_speed=%d", value);
        } else {
            log_msg("WARN", "设置 fan_speed=%d 失败", value);
        }
    }

    if (json_get_int(payload, "curtain_open", &value) == 0) {
        if (write_servo(fds, value ? 1 : 0) == 0) {
            data->curtain_open = value ? 1 : 0;
            log_msg("INFO", "云端设置 curtain_open=%d", data->curtain_open);
        } else {
            log_msg("WARN", "设置 curtain_open=%d 失败", value);
        }
    }
}

static int build_report_json(const struct SensorData *data, unsigned long seq,
                             const char *service_id,
                             char *out, size_t out_size)
{
    char dht_part[160];

    if (data->dht_valid) {
        snprintf(dht_part, sizeof(dht_part),
                 "\"temperature\":%.1f,\"humidity\":%.1f,",
                 data->temperature, data->humidity);
    } else {
        snprintf(dht_part, sizeof(dht_part),
                 "\"temperature\":0,\"humidity\":0,");
    }

    return snprintf(out, out_size,
                    "{\"services\":[{\"service_id\":\"%s\","
                    "\"properties\":{%s"
                    "\"sr501_present\":%d,"
                    "\"rd03_gpio_present\":%d,"
                    "\"rd03_present\":%d,"
                    "\"rd03_distance_cm\":%u,"
                    "\"fan_speed\":%d,"
                    "\"curtain_open\":%d},"
                    "\"event_time\":null}],\"seq\":%lu}",
                    service_id,
                    dht_part,
                    data->sr501_present,
                    data->rd03_gpio_present,
                    data->rd03_valid ? data->rd03_present : 0,
                    data->rd03_valid ? data->rd03_distance_cm : 0,
                    data->fan_speed,
                    data->curtain_open,
                    seq);
}

static void handle_mqtt_packet(uint8_t type, const uint8_t *payload, size_t len,
                               const struct DeviceFds *fds, struct SensorData *data)
{
    uint8_t packet_type = type & 0xF0;

    if (packet_type == MQTT_PKT_PUBLISH && len >= 2) {
        uint16_t topic_len = ((uint16_t)payload[0] << 8) | payload[1];
        const uint8_t *msg;
        size_t msg_len;
        char text[1024];

        if ((size_t)2 + topic_len > len) {
            return;
        }
        msg = payload + 2 + topic_len;
        msg_len = len - 2 - topic_len;
        if (msg_len >= sizeof(text)) {
            msg_len = sizeof(text) - 1;
        }
        memcpy(text, msg, msg_len);
        text[msg_len] = '\0';
        log_msg("INFO", "收到云端下发：%s", text);
        handle_downlink(fds, data, text);
    }
}

static int mqtt_session(const struct Config *cfg, const struct DeviceFds *fds)
{
    char client_id[256];
    char username[384];
    char password[256];
    char timestamp[16];
    struct Transport transport;
    time_t last_report = 0;
    time_t last_ping = 0;
    unsigned long seq = 1;
    struct Rd03Parser rd03;
    struct SensorData data;

    memset(&rd03, 0, sizeof(rd03));
    memset(&data, 0, sizeof(data));

    if (make_huawei_auth(cfg, client_id, sizeof(client_id),
                         username, sizeof(username),
                         password, sizeof(password),
                         timestamp, sizeof(timestamp)) < 0) {
        return -1;
    }

    log_msg("INFO", "连接 MQTT%s %s:%d client_id=%s",
            cfg->use_tls ? "S" : "", cfg->host, cfg->port, client_id);
    log_msg("INFO", "MQTT 鉴权 username=%s timestamp=%s password_prefix=%.8s",
            username, timestamp, password);
    if (transport_connect(&transport, cfg) < 0) {
        log_msg("ERROR", "%s 连接失败：%s",
                cfg->use_tls ? "MQTTS" : "TCP", strerror(errno));
        return -1;
    }

    if (mqtt_send_connect(&transport, client_id, username, password, cfg->keepalive) < 0) {
        log_msg("ERROR", "MQTT CONNECT 发送失败");
        transport_close(&transport);
        return -1;
    }
    log_msg("INFO", "MQTT CONNECT 已发送，等待 CONNACK");

    if (mqtt_expect_connack(&transport) < 0) {
        transport_close(&transport);
        return -1;
    }

    log_msg("INFO", "MQTT 已连接，订阅 %s", cfg->property_down_topic);
    mqtt_send_subscribe(&transport, cfg->property_down_topic, 1);

    while (g_running) {
        struct pollfd pfd;
        time_t now = time(NULL);

        pfd.fd = transport.fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN)) {
            uint8_t type;
            uint8_t payload[1536];
            size_t len;
            if (mqtt_read_packet(&transport, &type, payload, sizeof(payload), &len, 1000) < 0) {
                log_msg("ERROR", "MQTT 读取失败");
                break;
            }
            handle_mqtt_packet(type, payload, len, fds, &data);
        }

        read_sensors(fds, &rd03, &data);

        now = time(NULL);
        if (now - last_report >= cfg->report_interval) {
            char json[1024];
            int n = build_report_json(&data, seq++, cfg->service_id, json, sizeof(json));
            if (n <= 0 || (size_t)n >= sizeof(json) ||
                mqtt_send_publish_qos0(&transport, cfg->property_up_topic, json) < 0) {
                log_msg("ERROR", "属性上报失败");
                break;
            }
            log_msg("INFO", "已上报：%s", json);
            last_report = now;
        }

        if (now - last_ping >= cfg->keepalive / 2) {
            if (mqtt_send_ping(&transport) < 0) {
                log_msg("ERROR", "MQTT 心跳发送失败");
                break;
            }
            last_ping = now;
        }
    }

    mqtt_send_disconnect(&transport);
    transport_close(&transport);
    return 0;
}

int main(int argc, char **argv)
{
    struct Config cfg;
    struct DeviceFds fds;
    const char *config_path = argc > 1 ? argv[1] : NULL;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    memset(&fds, -1, sizeof(fds));

    if (load_config(&cfg, config_path) < 0 || validate_config(&cfg) < 0) {
        fprintf(stderr, "用法: %s [/path/to/huawei_cloud.conf]\n", argv[0]);
        return 1;
    }

    open_devices(&cfg, &fds);

    while (g_running) {
        if (mqtt_session(&cfg, &fds) == 0) {
            break;
        }
        if (g_running) {
            log_msg("INFO", "5 秒后重新连接");
            sleep(5);
        }
    }

    close_devices(&fds);
    return 0;
}
