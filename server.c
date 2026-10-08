#include "files.h"
#include "net.h"
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static int response_start(int fd, uint16_t id, unsigned status,
                          const char *mime, uint64_t size) {
    unsigned char payload[512];
    char number[32];snprintf(number,sizeof number,"%llu",(unsigned long long)size);
    Header headers[]={{"content-type",mime},{"content-length",number},{"server","bserve/1"}};
    put16(payload,(uint16_t)status);
    size_t used;
    if (encode_headers(payload+2,sizeof payload-2,&used,headers,3)<0) return -1;
    return frame_send(fd,BHP_RESPONSE,0,id,payload,(uint32_t)(used+2),0);
}

static int error_response(int fd, uint16_t id, unsigned status) {
    const char *body=status==404?"404 Not Found\n":"400 Bad Request\n";
    size_t n=strlen(body);
    if (response_start(fd,id,status,"text/plain; charset=utf-8",n)<0) return -1;
    return frame_send(fd,BHP_DATA,BHP_END,id,body,(uint32_t)n,0);
}

static int serve_file(int fd, int file, uint16_t id,
                      const unsigned char *path, size_t len) {
    struct stat st;
    if (fstat(file,&st)<0 || st.st_size<0) return error_response(fd,id,404);
    if (response_start(fd,id,200,content_type(path,len),(uint64_t)st.st_size)<0) return -1;
    unsigned char buf[BHP_DATA_CHUNK];
    uint64_t left=(uint64_t)st.st_size;
    if (!left) return frame_send(fd,BHP_DATA,BHP_END,id,NULL,0,0);
    while (left) {
        size_t want=left<sizeof buf?(size_t)left:sizeof buf;
        ssize_t n=read(file,buf,want);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return -1;
        left-=(uint64_t)n;
        if (frame_send(fd,BHP_DATA,left?0:BHP_END,id,buf,(uint32_t)n,0)<0) return -1;
    }
    return 0;
}

static int request_path(const Frame *f, const unsigned char **path, size_t *len) {
    if (f->id==0 || f->flags!=BHP_END || f->length<5 || f->payload[0]!=1) return -1;
    *len=get16(f->payload+1);
    if (*len==0 || *len>BHP_MAX_PATH || *len>f->length-3) return -1;
    *path=f->payload+3;
    size_t used;
    if (validate_headers(f->payload+3+*len,f->length-3-*len,&used)<0 ||
        used!=f->length-3-*len) return -1;
    return 0;
}

static void handle_client(int fd, int root) {
    for (;;) {
        Frame f;
        int r=frame_read(fd,&f);
        if (r==0) return;
        if (r==-2) {error_response(fd,0,400);return;}
        if (r<0) return;
        if (f.type!=BHP_REQUEST && f.type!=BHP_RESPONSE && f.type!=BHP_DATA) {
            frame_free(&f);continue; /* declared payload has already been consumed */
        }
        const unsigned char *path=NULL;size_t len=0;
        int bad=f.type!=BHP_REQUEST || request_path(&f,&path,&len)<0;
        int file=-1;
        if (!bad) {
            file=open_document(root,path,len);
            if (file==-2) bad=1;
        }
        int result=bad?error_response(fd,f.id,400):
                   file<0?error_response(fd,f.id,404):serve_file(fd,file,f.id,path,len);
        if (file>=0) close(file);
        frame_free(&f);
        if (result<0) return;
    }
}

int main(int argc, char **argv) {
    if (argc!=3) {fprintf(stderr,"usage: %s DOCUMENT_ROOT PORT\n",argv[0]);return 2;}
    char *end;long port=strtol(argv[2],&end,10);
    if (!*argv[2] || *end || port<1 || port>65535) {fputs("invalid port\n",stderr);return 2;}
    int root=open(argv[1],O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (root<0) {perror("document root");return 2;}
    signal(SIGPIPE,SIG_IGN);
    struct addrinfo hints={.ai_family=AF_UNSPEC,.ai_socktype=SOCK_STREAM,.ai_flags=AI_PASSIVE};
    struct addrinfo *list;
    if (getaddrinfo(NULL,argv[2],&hints,&list)!=0) {fputs("address lookup failed\n",stderr);close(root);return 2;}
    int listener=-1;
    for (struct addrinfo *a=list;a;a=a->ai_next) {
        int s=socket(a->ai_family,a->ai_socktype,a->ai_protocol);
        if (s<0) continue;
        int yes=1;setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);
        if (bind(s,a->ai_addr,a->ai_addrlen)==0 && listen(s,16)==0) {listener=s;break;}
        close(s);
    }
    freeaddrinfo(list);
    if (listener<0) {perror("listen");close(root);return 2;}
    for (;;) {
        int client=accept(listener,NULL,NULL);
        if (client<0) {if (errno==EINTR) continue;perror("accept");break;}
        if (set_socket_timeout(client,3)==0) handle_client(client,root);
        close(client);
    }
    close(listener);close(root);return 1;
}
