#include "net.h"
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

int set_socket_timeout(int fd, int seconds) {
    struct timeval limit = {.tv_sec = seconds, .tv_usec = 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit) < 0) return -1;
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof limit);
}

int read_full(int fd, void *buf, size_t len) {
    size_t pos = 0;
    while (pos < len) {
        ssize_t n = read(fd, (unsigned char *)buf + pos, len - pos);
        if (n == 0) return pos == 0 ? 0 : -1;
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        pos += (size_t)n;
    }
    return 1;
}

int write_full(int fd, const void *buf, size_t len) {
    size_t pos = 0;
    while (pos < len) {
        ssize_t n = write(fd, (const unsigned char *)buf + pos, len - pos);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return -1;
        pos += (size_t)n;
    }
    return 0;
}
