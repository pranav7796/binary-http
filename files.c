#include "files.h"
#include "protocol.h"
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int open_document(int root_fd, const unsigned char *path, size_t len) {
    if (len==0 || len>BHP_MAX_PATH || path[0]!='/' ||
        (len>1 && path[len-1]=='/')) return -2;
    for (size_t i=0;i<len;i++) {
        unsigned char c=path[i];
        if (c<33 || c>126 || c=='\\' || c=='?' || c=='#' || c=='%') return -2;
    }
    char buf[BHP_MAX_PATH+16];
    if (len==1) { memcpy(buf,"index.html",11);len=10; }
    else { memcpy(buf,path+1,len-1);len--;buf[len]=0; }
    size_t start=0;
    for (size_t i=0;i<=len;i++) {
        if (i<len && buf[i]!='/') continue;
        size_t n=i-start;
        if (!n || (n==1 && buf[start]=='.') ||
            (n==2 && buf[start]=='.' && buf[start+1]=='.')) return -2;
        start=i+1;
    }
    int dir=dup(root_fd);
    if (dir<0) return -1;
    start=0;
    for (size_t i=0;i<=len;i++) {
        if (i<len && buf[i]!='/') continue;
        char save=buf[i];buf[i]=0;
        const char *part=buf+start;
        int next=openat(dir,part,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK|
                        (i<len?O_DIRECTORY:0));
        buf[i]=save;
        close(dir);
        if (next<0) return -1;
        dir=next;start=i+1;
    }
    struct stat st;
    if (fstat(dir,&st)<0 || !S_ISREG(st.st_mode)) {close(dir);return -1;}
    return dir;
}

const char *content_type(const unsigned char *path, size_t len) {
    const char *p=(const char *)path;
    if (len==1 && p[0]=='/') return "text/html; charset=utf-8";
    if (len>=5 && !memcmp(p+len-5,".html",5)) return "text/html; charset=utf-8";
    if (len>=4 && !memcmp(p+len-4,".htm",4)) return "text/html; charset=utf-8";
    if (len>=4 && !memcmp(p+len-4,".txt",4)) return "text/plain; charset=utf-8";
    if (len>=4 && !memcmp(p+len-4,".css",4)) return "text/css; charset=utf-8";
    if (len>=3 && !memcmp(p+len-3,".js",3)) return "text/javascript; charset=utf-8";
    if (len>=4 && !memcmp(p+len-4,".png",4)) return "image/png";
    if (len>=4 && !memcmp(p+len-4,".jpg",4)) return "image/jpeg";
    if (len>=5 && !memcmp(p+len-5,".jpeg",5)) return "image/jpeg";
    return "application/octet-stream";
}
