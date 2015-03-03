
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <turbojpeg.h>
#include <xmmintrin.h>
#include "contour.h"
#include "fitpoly.h"
#include "draw3.h"

#define nelem(x) (sizeof(x)/sizeof(x[0]))

typedef struct Pt2 Pt2;
struct Pt2 {
	short x;
	short y;
};

static int color_imgw;
static int color_imgh;
static uchar *color_img;

static uchar *contour_img;

static float verts[4*320*240];

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

void
false4(unsigned int val, uchar *pix)
{
	if(val < 256){
		pix[0] = val;
		pix[1] = 0;
		pix[2] = 255-val;
		pix[3] = 255;
		return;
	}
	val -= 256;
	if(val < 256){
		pix[0] = 255-val;
		pix[1] = val;
		pix[2] = 0;
		pix[3] = 255;
		return;
	}
	val -= 256;
	if(val < 256){
		pix[0] = 0;
		pix[1] = 255-val;
		pix[2] = val;
		pix[3] = 255;
		return;
	}
}

void
hot4(unsigned int val, uchar *pix)
{
	if(val < 256){
		pix[0] = val;
		pix[1] = 0;
		pix[2] = 0;
		pix[3] = 255;
		return;
	}
	val -= 256;
	if(val < 256){
		pix[0] = 255-val;
		pix[1] = val;
		pix[2] = 0;
		pix[3] = 255;
		return;
	}
	val -= 256;
	if(val < 256){
		pix[0] = 0;
		pix[1] = 255;
		pix[2] = val;
		pix[3] = 255;
		return;
	}
	val -= 256;
	if(val < 256){
		pix[0] = val;
		pix[1] = 255;
		pix[2] = 255;
		pix[3] = 255;
		return;
	}
	pix[0] = 255;
	pix[1] = 255;
	pix[2] = 255;
	pix[3] = 255;
}

static uchar false4tab[4*768];
static uchar hot4tab[4*1024];

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

static inline void
hot4f(uchar *dp, float val)
{
	uchar *sp;
	int x = (int)val;
	sp = hot4tab + 4*(x%1024);
	dp[0] = sp[0];
	dp[1] = sp[1];
	dp[2] = sp[2];
	dp[3] = sp[3];
}

static inline void
false4f(uchar *dp, float val)
{
	uchar *sp;
	int x = (int)val;
	sp = false4tab + 4*(x%768);
	dp[0] = sp[0];
	dp[1] = sp[1];
	dp[2] = sp[2];
	dp[3] = sp[3];
}

static inline void
false4i(uchar *dp, int val)
{
	uchar *sp;
	sp = false4tab + 4*(val%768);
	dp[0] = sp[0];
	dp[1] = sp[1];
	dp[2] = sp[2];
	dp[3] = sp[3];
}


void
hot16init(void)
{
	int i;
	for(i = 0; i < 256; i++)
		hot16tab[i] = hot16(i, 8);
	for(i = 0; i < 1024; i++)
		hot4(i, hot4tab + 4*i);
}

void
false16init(void)
{
	int i;
	for(i = 0; i < 128; i++)
		false16tab[i] = false16(i, 7);
	for(i = 0; i < 768; i++)
		false4(i, false4tab + 4*i);

}


static inline int
getu16(uchar *p)
{
	return (p[1]<<8) | p[0];
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

float
fhypotf(float a, float b)
{
	return b + 0.428f * a * a / b; // lousy 1% approx.
}

float
fatan2f(float y, float x)
{
	float coeff_1 = M_PI/4.0f;
	float abs_y = fabs(y)+1e-10f;
	float r, angle;
	if(x >= 0.0f){
		r = (x - abs_y) / (x + abs_y);
		angle = coeff_1 - coeff_1 * r;
	} else {
		r = (x + abs_y) / (abs_y - x);
		angle = 3*coeff_1 - coeff_1 * r;
	}
	return y < 0.0f ? -angle : angle;
}

static inline int
vecprod(short *a, short *b, short *c)
{
	return a[0]*(b[1]-c[1]) + b[0]*(c[1]-a[1]) + c[0]*(a[1]-b[1]);
}

void
process_depth(uchar *dmap, int w, int h)
{

	int i, j, k, l;
	int shmoff, off;
	float I, Q;
	float intens;
	float phase;
	float dist;
	short *pt;
	int npt, apt;
	int cliph;

	if(drawbusy())
		return;

	Contour contr;
	uchar *cimg, *dimg;

	cimg = malloc(w*h);
	dimg = malloc(w*h);
	for(i = 0; i < h; i++){
		for(j = 0; j < w; j++){
			int pix;
			off = i*w+j;
			pix = getu16(dmap + 2*off);
			cimg[off] = (pix > 0) ? Fset : 0;
			dimg[off] = (pix > 0) ? 0 : Fset;
		}
	}

	apt = 8192;
	pt = malloc(2 * apt * sizeof pt[0]);

	const uchar white[4] = { 0xff, 0xff, 0xff, 0xff };
	const uchar poscolor[4] = { 0x50, 0xff, 0x30, 0xff };
	const uchar negcolor[4] = { 0xff, 0x30, 0x50, 0xff };

	memset(framebuffer, 0, stride*height);

	enum { Nloops = 10 };

	cliph = h < height ? h : height;

	initcontour(&contr, cimg, w, h);
	for(j = 0; j < Nloops; j++){
		resetcontour(&contr);
		while((npt = nextcontour(&contr, pt, apt)) != -1){
			int poly[5];
			int npoly;
			if(npt > h)
				continue;
			npoly = fitpoly(poly, nelem(poly), pt, npt, 5);
			if(npoly == 4){
				short *a, *b, *c, *d;
				a = pt + 2*poly[0];
				b = pt + 2*poly[1];
				c = pt + 2*poly[2];
				d = pt + 2*poly[3];
				if(vecprod(a, b, c) < 0){
					fixcontour(&contr, pt, npt);
					drawtri(framebuffer, width, cliph, a, b, c, poscolor);
					drawtri(framebuffer, width, cliph, a, c, d, poscolor);
				}
			}
		}
		erodecontour(&contr);
	}

	initcontour(&contr, dimg, w, h);
	for(j = 0; j < Nloops; j++){
		resetcontour(&contr);
		while((npt = nextcontour(&contr, pt, apt)) != -1){
			int poly[5];
			int npoly;
			if(npt > h)
				continue;
			npoly = fitpoly(poly, nelem(poly), pt, npt, 5);
			if(npoly == 4){
				short *a, *b, *c, *d;
				a = pt + 2*poly[0];
				b = pt + 2*poly[1];
				c = pt + 2*poly[2];
				d = pt + 2*poly[3];
				if(vecprod(a, b, c) < 0){
					fixcontour(&contr, pt, npt);
					drawtri(framebuffer, width, cliph, a, b, c, negcolor);
					drawtri(framebuffer, width, cliph, a, c, d, negcolor);
				}
			}
		}
		erodecontour(&contr);
	}

	free(pt);
	free(dimg);
	free(cimg);


#if 0
	float qsum = 0.0f, isum = 0.0f;
	for(i = 0; i < h; i++){
		for(j = 0, l = 0; l < w; l += 32){
			for(k = 0; k < 16; k += 2, j++){
				shmoff = i*shmimg->bytes_per_line + (j<<bypp);

				I = (float)getshort(dmap + i*w+l+k);
				Q = (float)getshort(dmap + i*w+l+k+16);

				qsum += Q;
				isum += I;

				intens = sqrtf(Q*Q+I*I);
				phase = atan2f(Q, I);
				phase = phase < 0.0f ? -phase : 2.0f*M_PI-phase;
				dist = phase * (0.5f * 299792458.0f / (2.0f*M_PI*50e6f));

				false4f(shmdata + shmoff, dist*(767.0f/3.0f));
				hot4f(shmdata + shmoff + (320<<bypp), intens);

			}
		}
	}
#endif
}

static inline float
clampf(float a, float min, float max)
{
	a = a > min ? a : min;
	a = a < max ? a : max;
	return a;
}

static tjhandle tjdec;
void
process_color(uchar *buf, int w, int h)
{
	int subsamp;
	uchar *dp, *dep, *sp, *ep;

	if(drawbusy())
		return;

#if 0
	tjDecompressHeader2(tjdec, buf, len, &color_imgw, &color_imgh, &subsamp);
	if(color_imgw == 0 || color_imgh == 0)
		return;
	if(color_img == NULL)
		color_img = malloc(3 * color_imgw * color_imgh);
	tjDecompress2(tjdec, buf, len, color_img, color_imgw, 0/*pitch they say*/, color_imgh, TJPF_RGB, TJFLAG_FASTDCT);

	ep = color_img + 3 * color_imgw * color_imgh;
	dp = (uchar *)shmimg->data + 240*shmimg->bytes_per_line;
	dep = (uchar *)shmimg->data + shmimg->height*shmimg->bytes_per_line;
	if(bypp == 1){
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
	} else if(bypp == 2){
		for(sp = color_img; sp < ep && dp < dep; sp += 3){
			dp[0] = sp[2];
			dp[1] = sp[1];
			dp[2] = sp[0];
			dp[3] = 0xff;
			dp += 4;
		}
	} else {
		fprintf(stderr,"x11jpegframe: bypp (bytes per pixel) %d unsupported\n", (1<<bypp));
		abort();
	}
#else
//	if(len > 4*640*(shmimg->height-480))
//		len = 4*640*(shmimg->height-480);
	int i, j, off;
	for(j = 0; j < h && (j+480) < height; j++){
		dp = framebuffer + (j+480)*stride;
		for(i = 0; i < w && i < width; i += 2){
			float y0, y1, u, v;

			off = 2*(j*w+i);

			y0 = (float)buf[off];
			u = (float)buf[off+1];
			y1 = (float)buf[off+2];
			v = (float)buf[off+3];

			dp[0] = clampf(y0 + 1.732446f * (u-128.0f), 0.0f, 255.0f);
			dp[1] = clampf(y0 - 0.698001f * (v-128.0f) - (0.337633f * (u-128.0f)), 0.0f, 255.0f);
			dp[2] = clampf(y0 + 1.370705f * (v-128.0f), 0.0f, 255.0f);
			dp[3] = 0xff;

			dp[4] = clampf(y1 + 1.732446f * (u-128.0f), 0.0f, 255.0f);
			dp[5] = clampf(y1 - 0.698001f * (v-128.0f) - (0.337633f * (u-128.0f)), 0.0f, 255.0f);
			dp[6] = clampf(y1 + 1.370705f * (v-128.0f), 0.0f, 255.0f);
			dp[7] = 0xff;

			dp += 8;
		}
	}
#endif
	drawflush();
//	XShmPutImage(display, window, DefaultGC(display, 0), shmimg, 0, 480, 0, 480, width, 480, False);
}
