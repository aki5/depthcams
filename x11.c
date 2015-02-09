
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <X11/Xlib.h>

#include "x11.h"

static Display *display;
static Visual *visual;
static Window window;
static int width = 640;
static int height = 240+480;

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
			//XPutImage(display, window, DefaultGC(display, 0), ximage, 0, 0, 0, 0, width, height);
			break;
		case ButtonPress:
			exit(0);
		}
	}
	fprintf(stderr, "x11serve done\n");
}

unsigned int
false16(unsigned int val, unsigned int bits)
{
	unsigned int mask;
	/* black - red - yellow - white */

	mask = (1ull << bits) - 1;
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
	//return 0xffff;
}
unsigned int
hot16(unsigned int val, unsigned int bits)
{
	unsigned int mask;
	/* black - red - yellow - white */

	mask = (1ull << bits) - 1;
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
	//return 0xffff;
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
	float dstfac = .5f/M_PI * 127.0;
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

	for(i = 0; i < h; i++){
		for(j = 0, l = 0; l < w; l += 32){
			for(k = 0; k < 16; k += 2, j++){
				long dst, dst2;
				float I, Q;
				int imgoff = i*iw+j;

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

				/* depth and confidence
				dst = (atan2f(Q, I) + dstoff) * dstfac;
				conf = sqrtf(I*I+Q*Q);
				putshort(img0 + 2*imgoff, false16f(dst));
				putshort(img1 + 2*imgoff, hot16f(conf));
				*/

				/* raw I and Q 
				*/
				putshort(img0 + 2*imgoff, hot16f(fabsf(I)));
				putshort(img1 + 2*imgoff, hot16f(fabsf(Q)));
			}
		}
	}
	hi++;
	if(hi >= nhist)
		hi = 0;

	XImage *ximage0;
	ximage0 = XCreateImage(display, visual, 16, ZPixmap, 0, img0, iw, h, 16, 0);
	XPutImage(display, window, DefaultGC(display, 0), ximage0, 0, 0, 0, 0, iw, h);

	XImage *ximage1;
	ximage1 = XCreateImage(display, visual, 16, ZPixmap, 0, img1, iw, h, 16, 0);
	XPutImage(display, window, DefaultGC(display, 0), ximage1, 0, 0, j, 0, iw, h);

	ximage0->data = NULL;
	XDestroyImage(ximage0);
	ximage1->data = NULL;
	XDestroyImage(ximage1);

	XFlush(display);
	//XSync(display, 1);
}

#ifdef TURBOJPEG
#include <turbojpeg.h>
void
x11jpegframe(uchar *buf, int len)
{
	int subsamp;
	uchar *dp, *sp, *ep;

	tjhandle tjdec = tjInitDecompress();
	tjDecompressHeader2(tjdec, buf, len, &color_imgw, &color_imgh, &subsamp);
	if(color_imgw == 0 || color_imgh == 0)
		return;
	if(color_img == NULL)
		color_img = malloc(3 * color_imgw * color_imgh);
	tjDecompress2(tjdec, buf, len, color_img, color_imgw, 0/*pitch they say*/, color_imgh, TJPF_RGB, TJFLAG_FASTDCT);
	tjDestroy(tjdec);

	/* in-place convert to 16bit */
	ep = color_img + 3 * color_imgw * color_imgh;
	dp = color_img;
	for(sp = color_img; sp < ep; sp += 3){
		unsigned short r, g, b;
		unsigned short pix16;
		r = sp[0] >> (8-5);
		g = sp[1] >> (8-6);
		b = sp[2] >> (8-5);
		pix16 = (r << 11) | (g << 5) | b;
		*dp++ = pix16 & 0xff;
		*dp++ = pix16 >> 8;
	}

	XImage *ximage;
	ximage = XCreateImage(display, visual, 16, ZPixmap, 0, color_img, color_imgw, color_imgh, 16, 0);
	XPutImage(display, window, DefaultGC(display, 0), ximage, 0, 0, 0, 240, color_imgw, color_imgh);
	ximage->data = NULL;
	XDestroyImage(ximage);
	//XFlush(display);
}
#else
void
x11jpegframe(uchar *buf, int len)
{
}
#endif

int
x11init(void)
{
	XInitThreads();
	if((display = XOpenDisplay(NULL)) == NULL){
		fprintf(stderr, "cannot open display!\n");
		return 1;
	}

	visual = DefaultVisual(display, 0);
	window = XCreateSimpleWindow(display, RootWindow(display, 0), 0, 0, width, height, 1, 0, 0);

	if(visual->class != TrueColor){
		fprintf(stderr, "Cannot handle non true color visual ...\n");
		exit(1);
	}

	XSelectInput(display, window, ButtonPressMask|ExposureMask);
	XMapWindow(display, window);
	//XSync(display, 0);

	hot16init();
	false16init();

	return XConnectionNumber(display);
}
