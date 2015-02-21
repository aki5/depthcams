
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

void
x11bltdmap(uchar *dmap, int w, int h)
{

	int i, j, k, l;
	int shmoff;
	float I, Q;
	float intens;
	float phase;
	float dist;

	uchar *shmdata;
	shmdata = (uchar *)shmimg->data;
	float qsum = 0.0f, isum = 0.0f;
	for(i = 0; i < h; i++){
		for(j = 0, l = 0; l < w; l += 32){
			for(k = 0; k < 16; k += 2, j++){
				shmoff = i*shmimg->bytes_per_line + (j<<bypp);

				I = (float)getshort(dmap + i*w+l+k);
				Q = (float)getshort(dmap + i*w+l+k+16);

				qsum += Q;
				isum += I;

				intens = hypotf(Q, I);
				Q /= intens;
				I /= intens;
				phase = atan2f(Q, I);
				if(phase < 0.0f)
					dist = -phase * 0.5f * 299792458.0f / (2.0f*M_PI*50e6f);
				else
					dist = (2.0f*M_PI-phase) * 0.5f * 299792458.0f / (2.0f*M_PI*50e6f);

				false4f(shmdata + shmoff, dist*(767.0f/3.0f));
				hot4f(shmdata + shmoff + (320<<bypp), intens);
			}
		}
	}

	intens = hypotf(qsum, isum);
	qsum /= intens;
	isum /= intens;
	phase = atan2f(qsum, isum);
	if(phase < 0.0f)
		dist = -phase * 0.5f * 299792458.0f / (2.0f*M_PI*50e6f);
	else
		dist = (2.0f*M_PI-phase) * 0.5f * 299792458.0f / (2.0f*M_PI*50e6f);
	fprintf(stderr, "average dist %.2fm %.2fin qsum %f isum %f\n", dist, 100.0f*dist/2.54f, qsum, isum);

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
	default: bypp = 0; break;
	case 15:
	case 16: bypp = 1; break;
	case 24: bypp = 2; break;
	case 32: bypp = 2; break;
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
	fprintf(stderr, "x11init: bypp %d\n", 1<<bypp);
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
