
typedef unsigned char uchar;

int x11init(void);
void x11serve(int fd);
void x11blt32(uchar *img, int w, int h);
void x11bltdmap(uchar *dmap, int w, int h);
void x11jpegframe(uchar *buf, int len);
