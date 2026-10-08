#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

#define BHP_MAX_PAYLOAD 1048576u
#define BHP_MAX_PATH 4096u
#define BHP_DATA_CHUNK 65536u
#define BHP_REQUEST 1
#define BHP_RESPONSE 2
#define BHP_DATA 3
#define BHP_END 1

typedef struct {
    uint8_t type, flags;
    uint16_t id;
    uint32_t length;
    unsigned char header[12];
    unsigned char *payload;
} Frame;

/* 1 frame; 0 clean EOF; -1 I/O/truncation; -2 invalid fixed header. */
int frame_read(int fd, Frame *f);
int frame_send(int fd, uint8_t type, uint8_t flags, uint16_t id,
               const void *payload, uint32_t length, int verbose);
void frame_free(Frame *f);
void frame_dump(const char *direction, const Frame *f);
uint16_t get16(const unsigned char *p);
uint32_t get32(const unsigned char *p);
void put16(unsigned char *p, uint16_t n);
void put32(unsigned char *p, uint32_t n);

/* Header blocks use a count byte, then name selector and 16-bit value length. */
typedef struct { const char *name; const char *value; } Header;
int encode_headers(unsigned char *out, size_t capacity, size_t *used,
                   const Header *headers, size_t count);
int validate_headers(const unsigned char *data, size_t length, size_t *used);
#endif
