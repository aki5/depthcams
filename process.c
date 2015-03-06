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

	apt = 32768;
	pt = malloc(2 * apt * sizeof pt[0]);

	uchar poscolor[4] = { 0x50, 0xff, 0x30, 0xff };
	uchar negcolor[4] = { 0xff, 0x30, 0x50, 0xff };
	uchar color[4];

	enum { MaxError = 4 };

	cliph = h < height ? h : height;
	memset(framebuffer, 0, stride*cliph);

	//fprintf(stderr, "## start polygen\n");
	Tess postess;
	inittess(&postess);
	initcontour(&contr, cimg, w, h);
	for(j = 0; j < 1; j++){
		resetcontour(&contr);
		int k = 0;
		while((npt = nextcontour(&contr, pt, apt)) != -1){
			short *a, *b;
			short orig[2] = { -1, -1 };
			int poly[256];
			int npoly;

			if(npt == apt){
				fprintf(stderr, "out of points!\n");
				continue;
			}

			if(ptarea(pt, npt, orig) > 0){
				npoly = fitpoly(poly, nelem(poly), pt, npt, MaxError);
				if(npoly == nelem(poly)){
					fprintf(stderr, "out of poly space!\n");
					continue;
				}
				if(polyarea(pt, poly, npoly, orig) < 0){
					fprintf(stderr, "bugger! orientation of poly different from pt!\n");
					continue;
				}
				tessaddpoly(&postess, pt, poly, npoly);
				setcontour(&contr, pt, npt, Ffix);
				if(0)if(drawpoly(framebuffer, width, cliph, pt, poly, npoly, poscolor) == -1){
					for(i = 0; i < npt; i++){
						int off = pt[2*i+1]*stride + 4*pt[2*i+0];
						framebuffer[off+0] = 0xff;
						framebuffer[off+1] = 0xff;
						framebuffer[off+2] = 0xff;
						framebuffer[off+3] = 0xff;
					}
				}
			} else {

				for(i = 0; i < npt/2; i++){
					short tmp;
					tmp = pt[2*i+0];
					pt[2*i+0] = pt[2*(npt-1-i)+0];
					pt[2*(npt-1-i)+0] = tmp;
					tmp = pt[2*i+1];
					pt[2*i+1] = pt[2*(npt-1-i)+1];
					pt[2*(npt-1-i)+1] = tmp;
				}
				npoly = fitpoly(poly, nelem(poly), pt, npt, MaxError);
				if(npoly == nelem(poly) || npoly < 3)
					continue;
				if(polyarea(pt, poly, npoly, orig) < 0){
					fprintf(stderr, "bugger! orientation of poly different from pt!\n");
					continue;
				}
				for(i = 0; i < npoly/2; i++){
					int tmp;
					tmp = poly[npoly-1-i];
					poly[npoly-1-i] = poly[i];
					poly[i] = tmp;
				}
				tessaddpoly(&postess, pt, poly, npoly);
				continue;
			}

		}
		if(k == 0)
			break;
		erodecontour(&contr);
	}

	free(pt);
	free(dimg);
	free(cimg);

	int ntris;
	if((ntris = tesstris(&postess, &pt)) != -1){
		for(i = 0; i < ntris; i++){
			idx2color(i, color);
			drawtri(framebuffer, width, cliph, pt+6*i+0, pt+6*i+2, pt+6*i+4, color);
		}
		free(pt);
	} else {
		fprintf(stderr, "tesstris fail\n");
	}

	freetess(&postess);
/*
*/
/*

	int ntris;
	ntris = tesstris(&postess, &pt);
	for(i = 0; i < ntris; i++)
		drawtri(framebuffer, width, cliph, pt+6*i+0, pt+6*i+2, pt+6*i+4, poscolor);

	free(pt);
	ntris = tesstris(&negtess, &pt);
	for(i = 0; i < ntris; i++)
		drawtri(framebuffer, width, cliph, pt+6*i+0, pt+6*i+2, pt+6*i+4, negcolor);
	free(pt);
	freetess(&negtess);
*/

/*
	initcontour(&contr, dimg, w, h);
	for(j = 0; j < 10; j++){
		resetcontour(&contr);
		int k = 0;
		while((npt = nextcontour(&contr, pt, apt)) != -1){
			short *a, *b, *c;
			int poly[64];
			int npoly;
			long long area;

			if(npt == apt || npt >2*(w+h)-16)
				continue;
			npoly = fitpoly(poly, nelem(poly), pt, npt, 5);
			if(npoly == nelem(poly) || npoly < 3)
				continue;

			area = 0;
			a = pt + 2*poly[0];
			for(i = 1; i < npoly-1; i++){
				b = pt + 2*poly[i];
				c = pt + 2*poly[i+1];
				area += ori2i(b, c, a);
			}
			if(area <= 0)
				continue;
			if(npoly >= 3){
				setcontour(&contr, pt, npt, Ffix);
				if(1)if(drawpoly(framebuffer, width, cliph, pt, poly, npoly, negcolor) == -1){
					for(i = 0; i < npt; i++){
						int off = pt[2*i+1]*stride + 4*pt[2*i+0];
						framebuffer[off+0] = 0xff;
						framebuffer[off+1] = 0xff;
						framebuffer[off+2] = 0xff;
						framebuffer[off+3] = 0xff;
					}
				}
			}
			k++;
			//fprintf(stderr, "found %d negative polygons\n", k);
		}
		erodecontour(&contr);
	}
*/

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
