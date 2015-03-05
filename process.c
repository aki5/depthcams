#include "os.h"
#include "contour.h"
#include "fitpoly.h"
#include "draw3.h"

static inline u16int 
getu16(uchar *p)
{
	return (p[1]<<8)|p[0];
}

int
process_depth(uchar *dmap, int w, int h)
{
	int i, j;
	short *pt;
	int npt, apt;
	int cliph;

	Contour contr;
	uchar *cimg, *dimg;
	int bugger = 0;

	cimg = malloc(w*h);
	dimg = malloc(w*h);
	for(i = 0; i < h; i++){
		for(j = 0; j < w; j++){
			int pix, off;
			off = i*w+j;
			pix = getu16(dmap + 2*off);
			cimg[off] = (pix > 0) ? Fset : 0;
			dimg[off] = (pix > 0) ? 0 : Fset;
		}
	}

	apt = 16384;
	pt = malloc(2 * apt * sizeof pt[0]);

	uchar poscolor[4] = { 0x50, 0xff, 0x30, 0xff };
	uchar negcolor[4] = { 0xff, 0x30, 0x50, 0xff };

	enum { Nloops = 10 };

	cliph = h < height ? h : height;
	memset(framebuffer, 0, stride*cliph);

	initcontour(&contr, cimg, w, h);
	for(j = 0; j < Nloops; j++){
		resetcontour(&contr);
		while((npt = nextcontour(&contr, pt, apt)) != -1){
			short *a, *b, *c;
			int poly[64];
			int npoly;

			long long area;
			if(npt == apt || npt > 2*(w+h)-16){
				//fprintf(stderr, "too long a contour!\n");
				continue;
			}
			npoly = fitpoly(poly, nelem(poly), pt, npt, 5);
			if(npoly == nelem(poly) || npoly < 2)
				continue;

			area = 0;
			a = pt + 2*poly[0];
			for(i = 1; i < npoly-1; i++){
				b = pt + 2*poly[i];
				c = pt + 2*poly[i+1];
				area += ori2i(b, c, a);
			}
#if 0
			if(npoly == 4){
				a = pt + 2*poly[0];
				b = pt + 2*poly[1];
				c = pt + 2*poly[2];
				d = pt + 2*poly[3];
				if(ori2i(a, b, c) < 0 && ori2i(b, c, d) < 0 && ori2i(c, d, a) < 0 && ori2i(d, a, b) < 0){
					fixcontour(&contr, pt, npt);
					drawpoly(framebuffer, width, cliph, pt, poly, npoly, poscolor);
				}
			}
#else

			if(area > 0 && npoly >= 3){
				fixcontour(&contr, pt, npt);
				if(1)if(drawpoly(framebuffer, width, cliph, pt, poly, npoly, poscolor) == -1){
					for(i = 0; i < npt; i++){
						int off = pt[2*i+1]*stride + 4*pt[2*i+0];
						framebuffer[off+0] = 0xff;
						framebuffer[off+1] = 0xff;
						framebuffer[off+2] = 0xff;
						framebuffer[off+3] = 0xff;
					}
				}
			}
#endif
		}
		erodecontour(&contr);
	}

	initcontour(&contr, dimg, w, h);
	for(j = 0; j < Nloops; j++){
		resetcontour(&contr);
		while((npt = nextcontour(&contr, pt, apt)) != -1){
			short *a, *b, *c;
			int poly[64];
			int npoly;
			long long area;

			if(npt == apt || npt > w+h){
				//fprintf(stderr, "too long a contour!\n");
				continue;
			}
			npoly = fitpoly(poly, nelem(poly), pt, npt, 5);
			if(npoly == nelem(poly) || npoly < 2)
				continue;

			area = 0;
			a = pt + 2*poly[0];
			for(i = 1; i < npoly-1; i++){
				b = pt + 2*poly[i];
				c = pt + 2*poly[i+1];
				area += ori2i(b, c, a);
			}
#if 0
			if(npoly == 4){
				short *a, *b, *c, *d;
				a = pt + 2*poly[0];
				b = pt + 2*poly[1];
				c = pt + 2*poly[2];
				d = pt + 2*poly[3];
				if(ori2i(a, b, c) < 0 && ori2i(b, c, d) < 0 && ori2i(c, d, a) < 0 && ori2i(d, a, b) < 0){
					fixcontour(&contr, pt, npt);
					drawtri(framebuffer, width, cliph, a, b, c, negcolor);
					drawtri(framebuffer, width, cliph, a, c, d, negcolor);
				}
			}
#else
			if(area > 0 && npoly >= 3){
				fixcontour(&contr, pt, npt);
				if(0)if(drawpoly(framebuffer, width, cliph, pt, poly, npoly, negcolor) == -1){
					for(i = 0; i < npt; i++){
						int off = pt[2*i+1]*stride + 4*pt[2*i+0];
						framebuffer[off+0] = 0xff;
						framebuffer[off+1] = 0xff;
						framebuffer[off+2] = 0xff;
						framebuffer[off+3] = 0xff;
					}
				}
			}
#endif
		}
		erodecontour(&contr);
	}

	free(pt);
	free(dimg);
	free(cimg);

	if(bugger)
		return -1;
	return 0;
#if 0
	int i, j, k, l;
	int shmoff, off;
	float I, Q;
	float intens;
	float phase;
	float dist;

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

void
process_color(uchar *buf, int w, int h)
{
	uchar *dp;

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
}
