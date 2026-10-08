#ifndef NET_H
#define NET_H
#include <stddef.h>

/* Returns 1 on success, 0 on clean EOF before any byte, -1 on error/truncation. */
int read_full(int fd, void *buf, size_t len);
int write_full(int fd, const void *buf, size_t len);
/* Bound each blocking socket read/write so an idle peer cannot hang a process. */
int set_socket_timeout(int fd, int seconds);
#endif
