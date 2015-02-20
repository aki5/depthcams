
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <turbojpeg.h>

#include "x11.h"

static Display *display;
static Visual *visual;
static Window window;

static XImage *shmimg;
static XShmSegmentInfo shminfo;

static int width = 640;
static int height = 240+480;
static int depth;
static int bypp;

static int color_imgw;
static int color_imgh;
static uchar *color_img;

void
x11serve(int fd)
{
	if(fd != XConnectionNumber(display))
		fprintf(stderr, "x11handle: passed fd does not match display\n");
	while(XPending(display)){
		XEvent ev;
		XNextEvent(display, &ev);
		switch(ev.type){
		case Expose:
			XShmPutImage(display, window, DefaultGC(display, 0), shmimg, 0, 0, 0, 0, width, height, False);
			break;
		case ButtonPress:
			exit(0);
		}
	}
}

unsigned int
false16(unsigned int val, unsigned int bits)
{
	/* black - red - yellow - white */
	val >>= (bits-7);
	if(val < 32) // 32
		return ((31-val) << 11) | val;
	val -= 32;
	if(val < 64) // 64
		return (val << 5) | (31-val/2);
	val -= 64;
	if(val < 32) // 32
		return (val << 11) | ((63-val*2) << 5);
	val -= 32;
	return 0x0000;
}
unsigned int
hot16(unsigned int val, unsigned int bits)
{
	/* black - red - yellow - white */
	val >>= (bits-8);
	if(val < 32) // 32
		return val;
	val -= 32;
	if(val < 64) // 64
		return (val << 5) | (31-val/2);
	val -= 64;
	if(val < 32) // 32
		return 0x07e0 | (val << 11);
	val -= 32;
	if(val < 32) // 32
		return 0xffe0 | val;
	return 0x0000;
}

static unsigned short hot16tab[256];
static unsigned short false16tab[128];

static inline unsigned short
hot16f(float val)
{
	int x = (int)val;
	return hot16tab[x & 255];
}

static inline unsigned short
false16f(float val)
{
	int x = (int)val;
	return false16tab[x & 127];
}

void
hot16init(void)
{
	int i;
	for(i = 0; i < 256; i++)
		hot16tab[i] = hot16(i, 8);
}

void
false16init(void)
{
	int i;
	for(i = 0; i < 128; i++)
		false16tab[i] = false16(i, 7);
}

static inline long
getshort(uchar *p)
{
	short val;
	val = (p[1]<<8) | p[0];
	return (long)val;
}

static inline void
putshort(uchar *p, short val)
{
	p[0] = val & 0xff;
	p[1] = val >> 8;
}

void
memsetf(float *p, float val, int len)
{
	int i;
	for(i = 0; i < len; i++)
		p[i] = val;
}


void
x11bltdmap(uchar *dmap, int w, int h)
{
	static uchar *img0;
	static uchar *img1;
	static float **qimg;
	static float **iimg;
	static float *isum;
	static float *qsum;
	static int hi;
	int nhist = 1;

	int i, j, k, l;
	int iw = w/4;
	float dstoff = M_PI;
	float dstfac = 255.0 / (2.0*M_PI); // [0..2PI] -> [0..255]
	float conf;

	if(img0 == NULL)
		img0 = malloc(2*iw*h);

	if(img1 == NULL)
		img1 = malloc(2*iw*h);

	if(qimg == NULL){
		qimg = malloc(nhist * sizeof qimg[0]);
		for(i = 0; i < nhist; i++){
			qimg[i] = malloc(iw*h*sizeof qimg[0][0]);
			memsetf(qimg[i], 0.0f, iw*h);
		}
		qsum = malloc(iw*h*sizeof qsum[0]);
		memsetf(qsum, 0.0f, iw*h);
	}

	if(iimg == NULL){
		iimg = malloc(nhist * sizeof iimg[0]);
		for(i = 0; i < nhist; i++){
			iimg[i] = malloc(iw*h*sizeof iimg[0][0]);
			memsetf(iimg[i], 0.0f, iw*h);
		}
		isum = malloc(iw*h*sizeof isum[0]);
		memsetf(isum, 0.0f, iw*h);
	}

	uchar *shmdata;
	shmdata = (uchar *)shmimg->data;
	for(i = 0; i < h; i++){
		for(j = 0, l = 0; l < w; l += 32){
			for(k = 0; k < 16; k += 2, j++){
				long dst;
				float I, Q;
				int imgoff = i*iw+j;
				int shmoff = i*shmimg->bytes_per_line + j*bypp;

				I = (float)getshort(dmap + i*w+l+k);
				Q = (float)getshort(dmap + i*w+l+k+16);

				isum[imgoff] -= iimg[hi][imgoff];
				qsum[imgoff] -= qimg[hi][imgoff];
				iimg[hi][imgoff] = I;
				qimg[hi][imgoff] = Q;
				isum[imgoff] += I;
				qsum[imgoff] += Q;
				I = isum[imgoff];
				Q = qsum[imgoff];

				/* depth and confidence */
				dst = (atan2f(Q, I) + dstoff) * dstfac;
				conf = sqrtf(I*I+Q*Q);
				putshort(shmdata + shmoff, hot16f(dst));
				putshort(shmdata + shmoff + 320*bypp, hot16f(conf));

				/* raw I and Q
				putshort(shmdata + shmoff, hot16f(fabsf(I)));
				putshort(shmdata + shmoff + 320*bypp, hot16f(fabsf(Q)));
				*/

			}
		}
	}
	hi++;
	if(hi >= nhist)
		hi = 0;

	XShmPutImage(display, window, DefaultGC(display, 0), shmimg, 0, 0, 0, 0, width, height/2, False);

}

static tjhandle tjdec;
void
x11jpegframe(uchar *buf, int len)
{
	int subsamp;
	uchar *dp, *dep, *sp, *ep;

	tjDecompressHeader2(tjdec, buf, len, &color_imgw, &color_imgh, &subsamp);
	if(color_imgw == 0 || color_imgh == 0)
		return;
	if(color_img == NULL)
		color_img = malloc(3 * color_imgw * color_imgh);
	tjDecompress2(tjdec, buf, len, color_img, color_imgw, 0/*pitch they say*/, color_imgh, TJPF_RGB, TJFLAG_FASTDCT);

	fprintf(stderr, "x11jpegframe: width %d height %d\n", color_imgw, color_imgh);

	ep = color_img + 3 * color_imgw * color_imgh;
	dp = (uchar *)shmimg->data + 240*shmimg->bytes_per_line;
	dep = (uchar *)shmimg->data + shmimg->height*shmimg->bytes_per_line;
	if(bypp == 2){
		for(sp = color_img; sp < ep && dp < dep; sp += 3){
			unsigned short r, g, b;
			unsigned short pix16;
			r = sp[0] >> (8-5);
			g = sp[1] >> (8-6);
			b = sp[2] >> (8-5);
			pix16 = (r << 11) | (g << 5) | b;
			dp[0] = pix16 & 0xff;
			dp[1] = pix16 >> 8;
			dp += 2;
		}
	} else if(bypp == 4){
		for(sp = color_img; sp < ep && dp < dep; sp += 3){
			dp[0] = sp[0];
			dp[1] = sp[1];
			dp[2] = sp[2];
			dp[3] = 0xff;
			dp += 4;
		}
	} else {
		fprintf(stderr,"x11jpegframe: bypp (bytes per pixel) %d unsupported\n", bypp);
		abort();
	}
	XShmPutImage(display, window, DefaultGC(display, 0), shmimg, 0, 240, 0, 240, width, 480, False);
}

int
x11init(void)
{
	hot16init();
	false16init();
 	tjdec = tjInitDecompress();
	//tjDestroy(tjdec);

	if((display = XOpenDisplay(NULL)) == NULL){
		fprintf(stderr, "cannot open display!\n");
		return 1;
	}

	visual = DefaultVisual(display, 0);
	window = XCreateSimpleWindow(display, RootWindow(display, 0), 0, 0, width, height, 1, 0, 0);
	depth = DefaultDepth(display, 0);

	switch(depth){
	default: bypp = 1; break;
	case 15:
	case 16: bypp = 2; break;
	case 24: bypp = 4; break;
	case 32: bypp = 4; break;
	}

	if(visual->class != TrueColor){
		fprintf(stderr, "Cannot handle non true color visual ...\n");
		return -1;
	}

	shmimg = XShmCreateImage(display, visual, depth, ZPixmap, NULL, &shminfo, width, height);
	if(shmimg == NULL){
		fprintf(stderr, "x11init: cannot create shmimg\n");
		return -1;
	}
	fprintf(stderr, "x11init: bypp %d\n", bypp);
	fprintf(stderr, "x11init: bytes_per_line %d\n", shmimg->bytes_per_line);
	shminfo.shmid = shmget(IPC_PRIVATE, shmimg->bytes_per_line*shmimg->height, IPC_CREAT | 0777);
	if(shminfo.shmid == -1){
		fprintf(stderr, "x11init: shmget fail\n");
		return -1;
	}
	shminfo.shmaddr = shmimg->data = shmat(shminfo.shmid, 0, 0);
	if(shminfo.shmaddr == (void *)-1){
		fprintf(stderr, "x11init: shmat fail\n");
		return -1;
	}
	shminfo.readOnly = False;
	XShmAttach(display, &shminfo);

	XSelectInput(display, window, ButtonPressMask|ExposureMask);
	XMapWindow(display, window);


	return XConnectionNumber(display);
}
