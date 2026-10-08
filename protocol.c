#include "protocol.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *names[10] = {
    "content-type", "content-length", "host", "accept", "user-agent",
    "server", "cache-control", "etag", "last-modified", "connection"
};

uint16_t get16(const unsigned char *p) { return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }
uint32_t get32(const unsigned char *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
void put16(unsigned char *p, uint16_t n) { p[0]=(unsigned char)(n>>8); p[1]=(unsigned char)n; }
void put32(unsigned char *p, uint32_t n) { p[0]=(unsigned char)(n>>24); p[1]=(unsigned char)(n>>16); p[2]=(unsigned char)(n>>8); p[3]=(unsigned char)n; }

static void make_header(Frame *f) {
    f->header[0]='B'; f->header[1]='H'; f->header[2]=1;
    f->header[3]=f->type; f->header[4]=f->flags; f->header[5]=0;
    put32(f->header+6,f->length); put16(f->header+10,f->id);
}

void frame_free(Frame *f) { free(f->payload); f->payload=NULL; }

int frame_read(int fd, Frame *f) {
    memset(f,0,sizeof *f);
    int r=read_full(fd,f->header,12);
    if (r != 1) return r;
    f->type=f->header[3]; f->flags=f->header[4];
    f->length=get32(f->header+6); f->id=get16(f->header+10);
    if (f->header[0]!='B' || f->header[1]!='H' || f->header[2]!=1 ||
        f->header[5]!=0 || f->length>BHP_MAX_PAYLOAD) return -2;
    if (f->length) {
        f->payload=malloc(f->length);
        if (!f->payload || read_full(fd,f->payload,f->length)!=1) {
            frame_free(f); return -1;
        }
    }
    return 1;
}

void frame_dump(const char *direction, const Frame *f) {
    fprintf(stderr,"%s frame type=%u flags=0x%02x id=%u length=%u\n",
            direction,f->type,f->flags,f->id,f->length);
    size_t total=12+(size_t)f->length;
    for (size_t pos=0;pos<total;pos+=16) {
        fprintf(stderr,"  %04zx  ",pos);
        size_t end=pos+16<total?pos+16:total;
        for (size_t j=pos;j<pos+16;j++) {
            if (j<end) fprintf(stderr,"%02x ",j<12?f->header[j]:f->payload[j-12]);
            else fputs("   ",stderr);
        }
        fputs(" |",stderr);
        for (size_t j=pos;j<end;j++) {
            unsigned char c=j<12?f->header[j]:f->payload[j-12];
            fputc(c>=32&&c<=126?c:'.',stderr);
        }
        fputs("|\n",stderr);
    }
}

int frame_send(int fd, uint8_t type, uint8_t flags, uint16_t id,
               const void *payload, uint32_t length, int verbose) {
    if (length>BHP_MAX_PAYLOAD || (length && !payload)) return -1;
    Frame f={.type=type,.flags=flags,.id=id,.length=length,.payload=(unsigned char *)payload};
    make_header(&f);
    if (verbose) frame_dump("send",&f);
    if (write_full(fd,f.header,12)<0) return -1;
    if (length && write_full(fd,payload,length)<0) return -1;
    return 0;
}

int encode_headers(unsigned char *out, size_t cap, size_t *used,
                   const Header *headers, size_t count) {
    if (count>255 || cap<1) return -1;
    size_t pos=0; out[pos++]=(unsigned char)count;
    for (size_t i=0;i<count;i++) {
        size_t nl=strlen(headers[i].name), vl=strlen(headers[i].value);
        if (!nl || nl>255 || vl>65535) return -1;
        unsigned code=0;
        for (unsigned j=0;j<10;j++) if (!strcmp(headers[i].name,names[j])) {code=j+1;break;}
        size_t need=1+(code?0:1+nl)+2+vl;
        if (need>cap-pos) return -1;
        out[pos++]=(unsigned char)code;
        if (!code) { out[pos++]=(unsigned char)nl; memcpy(out+pos,headers[i].name,nl);pos+=nl; }
        put16(out+pos,(uint16_t)vl);pos+=2;
        memcpy(out+pos,headers[i].value,vl);pos+=vl;
    }
    *used=pos;return 0;
}

int validate_headers(const unsigned char *p, size_t len, size_t *used) {
    if (!len) return -1;
    size_t pos=1;
    for (unsigned i=0;i<p[0];i++) {
        if (pos>=len) return -1;
        unsigned code=p[pos++];
        if (code>10) return -1;
        if (code==0) {
            if (pos>=len || p[pos]==0) return -1;
            unsigned nl=p[pos++];
            if (nl>len-pos) return -1;
            for (unsigned j=0;j<nl;j++) {
                unsigned char c=p[pos+j];
                if (!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='-')) return -1;
            }
            pos+=nl;
        }
        if (len-pos<2) return -1;
        unsigned vl=get16(p+pos);pos+=2;
        if (vl>len-pos) return -1;
        for (unsigned j=0;j<vl;j++) if (p[pos+j]==0 || p[pos+j]=='\r' || p[pos+j]=='\n') return -1;
        pos+=vl;
    }
    *used=pos;return 0;
}
