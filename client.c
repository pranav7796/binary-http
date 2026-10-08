#include "net.h"
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct { char host[256], port[6]; const char *path; } Target;

static int parse_target(const char *s, Target *t) {
    const char *slash=strchr(s,'/');
    if (!slash) return -1;
    const char *colon=NULL;
    if (s[0]=='[') {
        const char *close=strchr(s,']');
        if (!close || close>=slash || close[1]!=':') return -1;
        size_t n=(size_t)(close-s-1);
        if (!n || n>=sizeof t->host) return -1;
        memcpy(t->host,s+1,n);t->host[n]=0;colon=close+1;
    } else {
        colon=memchr(s,':',(size_t)(slash-s));
        if (!colon || memchr(colon+1,':',(size_t)(slash-colon-1))) return -1;
        size_t n=(size_t)(colon-s);
        if (!n || n>=sizeof t->host) return -1;
        memcpy(t->host,s,n);t->host[n]=0;
    }
    size_t pn=(size_t)(slash-colon-1);
    if (!pn || pn>=sizeof t->port) return -1;
    memcpy(t->port,colon+1,pn);t->port[pn]=0;
    for (size_t i=0;i<pn;i++) if (t->port[i]<'0'||t->port[i]>'9') return -1;
    long port=strtol(t->port,NULL,10);
    if (port<1 || port>65535) return -1;
    t->path=slash;
    if (strlen(slash)>BHP_MAX_PATH) return -1;
    return 0;
}

static int connect_once(const Target *t) {
    struct addrinfo hints={.ai_family=AF_UNSPEC,.ai_socktype=SOCK_STREAM};
    struct addrinfo *list;
    int rc=getaddrinfo(t->host,t->port,&hints,&list);
    if (rc) {fprintf(stderr,"lookup: %s\n",gai_strerror(rc));return -1;}
    int fd=-1;
    for (struct addrinfo *a=list;a;a=a->ai_next) {
        int s=socket(a->ai_family,a->ai_socktype,a->ai_protocol);
        if (s<0) continue;
        int flags=fcntl(s,F_GETFL,0);
        if (flags<0 || fcntl(s,F_SETFL,flags|O_NONBLOCK)<0) {close(s);continue;}
        int rc=connect(s,a->ai_addr,a->ai_addrlen);
        if (rc<0 && errno==EINPROGRESS) {
            struct pollfd ready={.fd=s,.events=POLLOUT};
            do {rc=poll(&ready,1,3000);} while (rc<0 && errno==EINTR);
            if (rc>0) {
                int error=0;socklen_t size=sizeof error;
                if (getsockopt(s,SOL_SOCKET,SO_ERROR,&error,&size)<0) rc=-1;
                else if (error) {errno=error;rc=-1;}
                else rc=0;
            } else if (rc==0) {errno=ETIMEDOUT;rc=-1;}
        }
        if (rc==0 && fcntl(s,F_SETFL,flags)==0 && set_socket_timeout(s,3)==0) {
            fd=s;break;
        }
        close(s);
    }
    freeaddrinfo(list);
    if (fd<0) perror("connect");
    return fd;
}

static int send_request(int fd, const Target *t, uint16_t id, int verbose) {
    unsigned char payload[8192];
    size_t len=strlen(t->path), used;
    payload[0]=1;put16(payload+1,(uint16_t)len);
    memcpy(payload+3,t->path,len);
    Header headers[]={{"host",t->host},{"accept","*/*"},{"user-agent","bcurl/1"}};
    if (encode_headers(payload+3+len,sizeof payload-3-len,&used,headers,3)<0) return -1;
    return frame_send(fd,BHP_REQUEST,BHP_END,id,payload,(uint32_t)(3+len+used),verbose);
}

static int response_length(const unsigned char *p, size_t len, uint64_t *out, int *present) {
    size_t pos=1;
    *present=0;
    for (unsigned i=0;i<p[0];i++) {
        unsigned code=p[pos++];
        if (code==0) {unsigned nl=p[pos++];pos+=nl;}
        unsigned vl=get16(p+pos);pos+=2;
        if (code==2) {
            if (*present || !vl) return -1;
            uint64_t n=0;
            for (unsigned j=0;j<vl;j++) {
                unsigned char c=p[pos+j];
                if (c<'0'||c>'9'||n>(UINT64_MAX-(c-'0'))/10) return -1;
                n=n*10+c-'0';
            }
            *out=n;*present=1;
        }
        pos+=vl;
    }
    return pos==len?0:-1;
}

static int read_response(int fd, uint16_t id, int verbose, int *http_error) {
    int have_head=0;
    uint64_t declared=0, actual=0;
    int has_length=0;
    for (;;) {
        Frame f;
        int rc=frame_read(fd,&f);
        if (rc!=1) {fputs("truncated or invalid response\n",stderr);return -1;}
        if (verbose) frame_dump("recv",&f);
        if (f.type!=BHP_REQUEST && f.type!=BHP_RESPONSE && f.type!=BHP_DATA) {
            frame_free(&f);continue;
        }
        if (f.id!=id) {frame_free(&f);fputs("unexpected response ID\n",stderr);return -1;}
        if (!have_head) {
            size_t used;
            if (f.type!=BHP_RESPONSE || f.flags!=0 || f.length<3 ||
                validate_headers(f.payload+2,f.length-2,&used)<0 || used!=f.length-2 ||
                response_length(f.payload+2,used,&declared,&has_length)<0) {
                frame_free(&f);fputs("malformed response headers\n",stderr);return -1;
            }
            unsigned status=get16(f.payload);
            if (status<100 || status>599) {frame_free(&f);return -1;}
            if (status>=400) *http_error=1;
            have_head=1;
            frame_free(&f);continue;
        }
        if (f.type!=BHP_DATA || (f.flags & ~BHP_END)) {
            frame_free(&f);fputs("unexpected response frame\n",stderr);return -1;
        }
        if (UINT64_MAX-actual<f.length || (has_length && actual+f.length>declared) ||
            (f.length && fwrite(f.payload,1,f.length,stdout)!=f.length)) {
            frame_free(&f);fputs("body write or length error\n",stderr);return -1;
        }
        actual+=f.length;
        int done=(f.flags&BHP_END)!=0;
        frame_free(&f);
        if (done) {
            if (has_length && actual!=declared) {fputs("body length mismatch\n",stderr);return -1;}
            return 0;
        }
    }
}

int main(int argc, char **argv) {
    int verbose=argc>1 && !strcmp(argv[1],"-v");
    int first=verbose?2:1;
    if (argc<=first) {fprintf(stderr,"usage: %s [-v] HOST:PORT/PATH [HOST:PORT/PATH ...]\n",argv[0]);return 2;}
    Target original, current;
    if (parse_target(argv[first],&original)<0) {fputs("invalid target\n",stderr);return 2;}
    for (int i=first+1;i<argc;i++) {
        if (parse_target(argv[i],&current)<0 || strcmp(current.host,original.host) || strcmp(current.port,original.port)) {
            fputs("all targets must use the same host and port\n",stderr);return 2;
        }
    }
    signal(SIGPIPE,SIG_IGN);
    int fd=connect_once(&original);
    if (fd<0) return 2;
    int http_error=0, result=0;
    for (int i=first;i<argc;i++) {
        parse_target(argv[i],&current);
        uint16_t id=(uint16_t)(i-first+1);
        if (id==0 || send_request(fd,&current,id,verbose)<0 ||
            read_response(fd,id,verbose,&http_error)<0) {result=2;break;}
    }
    if (fflush(stdout)!=0) result=2;
    close(fd);
    return result?result:(http_error?1:0);
}
