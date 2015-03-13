#include "os.h"
#include "contour.h"
#include "fitpoly.h"
#include "draw3.h"
#include "tess3.h"

static inline u16int 
getu16(uchar *p)
{
	return (p[1]<<8)|p[0];
}

static void
polyreverse(int *poly, int npoly)
{
	int i, tmp;
	for(i = 0; i < npoly/2; i++){
		tmp = poly[npoly-1-i];
		poly[npoly-1-i] = poly[i];
		poly[i] = tmp;
	}
}

static void
ptreverse(short *pt, int npt)
{
	int i;
	for(i = 0; i < npt/2; i++){
		short tmp;
		tmp = pt[2*i+0];
		pt[2*i+0] = pt[2*(npt-1-i)+0];
		pt[2*(npt-1-i)+0] = tmp;
		tmp = pt[2*i+1];
		pt[2*i+1] = pt[2*(npt-1-i)+1];
		pt[2*(npt-1-i)+1] = tmp;
	}
}

int
process_contour(Image *img, Rect clipr, uchar *cimg, int w, int h, int st, uchar *colors)
{
	int i;
	short *pt;
	int npt, apt;

	Contour contr;

	apt = 32768;
	pt = malloc(2 * apt * sizeof pt[0]);

	uchar color[4];

	enum { MaxError = 5 };

	/* highlight cracks */
	drawrect(img, clipr, color(0x00, 0xff, 0x00, 0xff));

	Tess tess[16];
	for(i = 0; i < nelem(tess); i++)
		inittess(tess+i);

	initcontour(&contr, cimg, w, h, st);
	int fid;
	while((npt = nextcontour(&contr, pt, apt, 0, &fid)) != -1){
		short orig[2] = { -1, -1 };
		int area;
		int poly[4096];
		int npoly;

		if(npt == apt){
			fprintf(stderr, "out of points!\n");
			continue;
		}

		area = ptarea(pt, npt, orig);
		if(area > 0){
			if((npoly = fitpoly(poly, nelem(poly), pt, npt, MaxError)) == -1)
				continue; /* not enough points */
			if(npoly == nelem(poly) || npoly < 3)
				continue;
			if(polyarea(pt, poly, npoly, orig) < 0){
				continue;
			}
			tessaddpoly(tess+fid, pt, poly, npoly);
		} else {
			ptreverse(pt, npt);
			npoly = fitpoly(poly, nelem(poly), pt, npt, MaxError);
			if(npoly == nelem(poly) || npoly < 3)
				continue;
			if(polyarea(pt, poly, npoly, orig) < 0){
				continue;
			}
			polyreverse(poly, npoly);
			tessaddpoly(tess+fid, pt, poly, npoly);
		}
	}


	free(pt);
	int ntris;
	int j;
	for(j = 0; j < nelem(tess); j++){
		if((ntris = tesstris(tess+j, &pt)) != -1){
			memcpy(color, colors+4*j, sizeof color);
			for(i = 0; i < ntris; i++){
				//idx2color(j, color);
				//memcpy(color, poscolor, sizeof color);
				pt[6*i+0+0] += clipr.u0;
				pt[6*i+0+1] += clipr.v0;
				pt[6*i+2+0] += clipr.u0;
				pt[6*i+2+1] += clipr.v0;
				pt[6*i+4+0] += clipr.u0;
				pt[6*i+4+1] += clipr.v0;
				drawtri(img, clipr, pt+6*i+0, pt+6*i+2, pt+6*i+4, color);
			}
			free(pt);
		}
		freetess(tess+j);
	}

	return 0;
}

int
process_depth(uchar *dmap, int w, int h)
{
	int i, j;
	int st;
	uchar colors[] = {
		0x00, 0x00, 0x00, 0xff,
		0xff, 0xff, 0xff, 0xff
	};
	uchar *img;

	st = w;
	img = malloc(h*st);
	for(i = 0; i < h; i++){
		for(j = 0; j < w; j++){
			int pix, off;
			off = i*w+j;
			pix = getu16(dmap + 2*off);
			img[i*st+j] = (pix > 0) ? 1 : 0;
		}
	}

	process_contour(&screen, rect(0,0,w,h), img, w, h, st, colors);
	free(img);
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

int
sobel2(uchar *buf, int x, int y, int st)
{
	int off;
	int lx, ly;

	off = y*st+2*x;

	lx = -1*buf[off]		+	buf[off+4] +
		-2*buf[off+st]		+	2*buf[off+st+4] +
		-1*buf[off+2*st]	+	buf[off+2*st+4];

	ly = buf[off]			+	2*buf[off+2]		+ buf[off+4] +
		-1*buf[off+2*st]	+	-2*buf[off+2*st+2]	+ -1*buf[off+2*st+4];

	return lx*lx + ly*ly;
}

void
process_color(uchar *buf, int w, int h)
{
	enum { Ncolors = 8 };
	int i, j, st;
	uchar *img;
	uchar colors[Ncolors*4];

	for(i = 0; i < Ncolors; i++){
		colors[4*i+0] = 0xff*i/(Ncolors-1);
		colors[4*i+1] = 0xff*i/(Ncolors-1);
		colors[4*i+2] = 0xff*i/(Ncolors-1);
		colors[4*i+3] = 0xff;
	}

	st = w;
	img = malloc(h*st);
	for(i = 0; i < h; i++){
		for(j = 0; j < w; j++){
			int pix, off;
			off = i*w+j;
			pix = buf[2*off];
			img[i*st+j] = ((Ncolors-1)*pix)/255;
		}
	}

	process_contour(&screen, rect(0,480,w,480+h), img, w, h, st, colors);
	free(img);
	return;

#if 0
	uchar *dp;
	int off;
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
}
