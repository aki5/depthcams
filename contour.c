
#include <string.h>
#include "contour.h"

typedef unsigned int u32int;

void
initcontour(Contour *cp, uchar *img, int width, int height)
{
	int i;

	cp->img = img;
	cp->width = width;
	cp->height = height;
	cp->off = width+1;
	cp->end = width*height-width;

	cp->moore[0] = -1;
	cp->moore[1] = width-1;
	cp->moore[2] = width;
	cp->moore[3] = width+1;
	cp->moore[4] = 1;
	cp->moore[5] = -width+1;
	cp->moore[6] = -width;
	cp->moore[7] = -width-1;

	cp->moore[8] = cp->moore[0];
	cp->moore[9] = cp->moore[1];
	cp->moore[10] = cp->moore[2];
	cp->moore[11] = cp->moore[3];
	cp->moore[12] = cp->moore[4];
	cp->moore[13] = cp->moore[5];
	cp->moore[14] = cp->moore[6];
	cp->moore[15] = cp->moore[7];

	memset(img, 0, width);
	memset(img + (height-1)*width, 0,  width);
	for(i = 1; i < height-1; i++){
		img[i*width] = 0;
		img[i*width+width-1] = 0;
	}
}

void
resetcontour(Contour *cp)
{
	cp->off = 0;
}

static inline int
skip8_unset(uchar *img, int off, int end)
{
	unsigned long long octa;
	while(off < end-7){
		octa = *(unsigned long long *)(img+off);
		octa |= octa >> 32;
		octa |= octa >> 16;
		octa |= octa >> 8;
		if((octa & Fset) == Fset)
			break;
		off += 8;
	}
	return off;
}

static inline int
skip8_set(uchar *img, int off, int end)
{
	unsigned long long octa;
	while(off < end-7){
		octa = *(unsigned long long *)(img+off);
		octa &= octa >> 32;
		octa &= octa >> 16;
		octa &= octa >> 8;
		if((octa & Fset) == 0)
			break;
		off += 8;
	}
	return off;
}

static inline int
skip4_unset(uchar *img, int off, int end)
{
	unsigned int quad;
	while(off < end-3){
		//if((off&7) == 0) return skip8_unset(img, off, end);
		quad = *(unsigned int *)(img+off);
		quad |= quad >> 16;
		quad |= quad >> 8;
		if((quad & Fset) == Fset)
			break;
		off += 4;
	}
	return off;
}

static inline int
skip4_set(uchar *img, int off, int end)
{
	unsigned int quad;
	while(off < end-3){
		//if((off&7) == 0) return skip8_set(img, off, end);
		quad = *(unsigned int *)(img+off);
		quad &= quad >> 16;
		quad &= quad >> 8;
		if((quad & Fset) == 0)
			break;
		off += 4;
	}
	return off;
}

int
nextcontour(Contour *cp, short *pt, int apt)
{
	uchar *img;
	int cur, off, end;
	int i, dir;
	int npt;
	int width;

	width = cp->width;
	img = cp->img;
	off = cp->off;
	end = cp->end;

	while(off < end){
		int flags = img[off] & (Fset|Fcont);
 		if(flags == Fset)
			break;
		if(flags == (Fset|Fcont))
			while(off < end){
				if((off&3) == 3) off = skip4_set(img, off+1, end); else
				if((img[++off] & Fset) == 0)
					break;
			}
		if((off&3) == 3) off = skip4_unset(img, off+1, end); else
		off++;
	}
	cp->off = off;
	if(off == end)
		return -1;

	// do one step to get a solid stopping criterion
	dir = 0;
	for(i = 0; i < 8; i++){
		cur = off + cp->moore[++dir];
		if(img[cur] == 1){
			off = cur;
			dir = (dir+4) & 7;
			break;
		}
	}
	npt = 0;
	if(i == 8) {
		// found an isolated pixel
		img[off] |= Fcont;
		if(npt < apt){
			int xcoord, ycoord;
			ycoord = off / width;
			xcoord = off % width;
			pt[2*npt+0] = xcoord;
			pt[2*npt+1] = ycoord;
			npt++;
		}		cp->off++;
	} else {
		// found something to trace
		int stop, stopdir;
		stop = off;
		stopdir = dir;
		for(;;){

			for(i = 0; i < 8; i++){
				cur = off + cp->moore[++dir];
				if((img[cur] & Fset) == Fset){
					img[cur] |= Fcont;
					off = cur;
					dir = (dir+4) & 7;
					break;
				}
			}
			if(npt < apt){
				int xcoord, ycoord;
				ycoord = off / width;
				xcoord = off % width;
				pt[2*npt+0] = xcoord;
				pt[2*npt+1] = ycoord;
				npt++;
			}
			if(off == stop && dir == stopdir)
				break;
		}
		off = cp->off;
		while(off < end){
			if((off&3) == 3) off = skip4_set(img, off+1, end); else
			if((img[++off] & Fset) == 0)
				break;
		}
		cp->off = off + 1;
	} 

	return npt;
}

void
erodecontour(Contour *cp)
{
	int i, len;
	u32int *img;
	const u32int
		fixmask = 0x01010101 * Ffix,
		contmask = 0x01010101 * Fcont;

	/* gcc seems to vectorize this for sse, impressive! */
	img = (u32int *)cp->img;
	len = cp->width*cp->height / sizeof img[0];
	for(i = 0; i < len; i++){
		u32int val = img[i];
		u32int fix = (val & fixmask) / Ffix;
		u32int ncont = ((val & contmask) / Fcont) ^ 0x01010101;

		fix |= fix<<1;
		fix |= fix<<2;
		fix |= fix<<4;

		ncont |= ncont<<1;
		ncont |= ncont<<2;
		ncont |= ncont<<4;

		img[i] = val & (fix | ncont);
	}
#if 0
	uchar *img;
	img = cp->img;
	len = cp->width*cp->height;
	for(i = 0; i < len; i++)
		if((img[i] & (Ffix|Fcont|Fset)) == (Fcont|Fset))
			img[i] = 0;
#endif
}

void
setcontour(Contour *cp, short *pt, int npt, int bit)
{
	uchar *img;
	int i, off, width;
	img = cp->img;
	width = cp->width;
	for(i = 0; i < npt; i++){
		off = pt[2*i+1]*width + pt[2*i+0];
		img[off] |= bit;
	}
}

void
clearcontour(Contour *cp, short *pt, int npt, int bit)
{
	uchar *img;
	int i, off, width;
	img = cp->img;
	width = cp->width;
	for(i = 0; i < npt; i++){
		off = pt[2*i+1]*width + pt[2*i+0];
		img[off] &= ~bit;
	}
}
