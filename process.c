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
process_contour(Image *img, Rect clipr, uchar *cimg, int w, int h)
{
	int i;
	short *pt;
	int npt, apt;
	int cliph;

	Contour contr;

	apt = 32768;
	pt = malloc(2 * apt * sizeof pt[0]);

	uchar poscolor[4] = { 0x50, 0xff, 0x30, 0xff };
	uchar negcolor[4] = { 0xff, 0x30, 0x50, 0xff };
	uchar color[4];

	enum { MaxError = 3 };

	cliph = h < height - clipr.y0 ? h : height - clipr.y0;
	memset(framebuffer + clipr.y0*stride, 0, stride*cliph);

	Tess postess, negtess;

	inittess(&postess);
	inittess(&negtess);

	/* negative polygon needs to have something to subtract from */
	tessaddpoly(
		&negtess,
		(short[]){
			0,0,
			0,cliph-1,
			width-1,cliph-1,
			width-1,0
		},
		(int[]){0, 1, 2, 3},
		4
	);
	initcontour(&contr, cimg, w, h);
	resetcontour(&contr);
	while((npt = nextcontour(&contr, pt, apt, 1)) != -1){
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
			if(npoly == nelem(poly)){
				fprintf(stderr, "out of poly space!\n");
				continue;
			}
			if(polyarea(pt, poly, npoly, orig) < 0){
				fprintf(stderr, "bugger! orientation of poly different from pt!\n");
				continue;
			}
			tessaddpoly(&postess, pt, poly, npoly);
			polyreverse(poly, npoly);
			tessaddpoly(&negtess, pt, poly, npoly);
		} else {
			ptreverse(pt, npt);
			npoly = fitpoly(poly, nelem(poly), pt, npt, MaxError);
			if(npoly == nelem(poly) || npoly < 3)
				continue;
			if(polyarea(pt, poly, npoly, orig) < 0){
				fprintf(stderr, "bugger! orientation of poly different from pt!\n");
				continue;
			}
			tessaddpoly(&negtess, pt, poly, npoly);
			polyreverse(poly, npoly);
			tessaddpoly(&postess, pt, poly, npoly);
		}

	}


	free(pt);
	int ntris, tottris = 0;

	if((ntris = tesstris(&postess, &pt)) != -1){
		for(i = 0; i < ntris; i++){
			//idx2color(i, color);
			memcpy(color, poscolor, sizeof color);
			pt[6*i+0+0] += clipr.x0;
			pt[6*i+0+1] += clipr.y0;
			pt[6*i+2+0] += clipr.x0;
			pt[6*i+2+1] += clipr.y0;
			pt[6*i+4+0] += clipr.x0;
			pt[6*i+4+1] += clipr.y0;
			drawtri(img, clipr, pt+6*i+0, pt+6*i+2, pt+6*i+4, color);
		}
		free(pt);
		tottris = ntris;
	} else {
		fprintf(stderr, "tesstris fail\n");
	}

	if((ntris = tesstris(&negtess, &pt)) != -1){
		for(i = 0; i < ntris; i++){
			//idx2color(i, color);
			memcpy(color, negcolor, sizeof color);
			pt[6*i+0+0] += clipr.x0;
			pt[6*i+0+1] += clipr.y0;
			pt[6*i+2+0] += clipr.x0;
			pt[6*i+2+1] += clipr.y0;
			pt[6*i+4+0] += clipr.x0;
			pt[6*i+4+1] += clipr.y0;
			drawtri(img, clipr, pt+6*i+0, pt+6*i+2, pt+6*i+4, color);
		}
		free(pt);
		tottris += ntris;
	} else {
		fprintf(stderr, "tesstris fail\n");
	}

	freetess(&postess);
	freetess(&negtess);

	return 0;
}

int
process_depth(uchar *dmap, int w, int h)
{
	int i, j;
	uchar *img;

	img = malloc(w*h);
	for(i = 0; i < h; i++){
		for(j = 0; j < w; j++){
			int pix, off;
			off = i*w+j;
			pix = getu16(dmap + 2*off);
			img[off] = (pix > 0) ? Fset : 0;
		}
	}

	process_contour(&screen, rect(0,0,w,h), img, w, h);
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

void
process_color(uchar *buf, int w, int h)
{

	int i, j;
	uchar *img;

	img = malloc(w*h);
	for(i = 0; i < h; i++){
		for(j = 0; j < w; j++){
			int pix, off;
			off = i*w+j;
			pix =buf[2*off];
			img[off] = (pix > 128) ? Fset : 0;
		}
	}

	process_contour(&screen, rect(0,480,w,480+h), img, w, h);
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
