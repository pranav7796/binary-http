#ifndef FILES_H
#define FILES_H
#include <stddef.h>

/* Returns an open regular file, -1 for missing/inaccessible, -2 for bad path. */
int open_document(int root_fd, const unsigned char *path, size_t len);
const char *content_type(const unsigned char *path, size_t len);
#endif
