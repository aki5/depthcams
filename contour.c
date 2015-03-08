
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
ptappend(short *pt, int apt, int *nptp, int x, int y)
{
	int npt = *nptp;
	if(npt < apt){
		pt[2*npt+0] = x;
		pt[2*npt+1] = y;
		npt++;
		*nptp = npt;
		return 0;
	}
	return -1;
}

/* regular moore contouring */
int
nextcontour(Contour *cp, short *pt, int apt, int fillrule)
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

	// do one step to set up stopping and dir.
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
			if(fillrule){
				ptappend(pt, apt, &npt, xcoord, ycoord);
				ptappend(pt, apt, &npt, xcoord, ycoord+1);
				ptappend(pt, apt, &npt, xcoord+1, ycoord);
			} else {
				ptappend(pt, apt, &npt, xcoord, ycoord);
			}
		}
		cp->off++;
	} else {
		// found something to trace
		int stop, stopdir;
		stop = off;
		stopdir = dir;
		for(;;){
			int idir = dir;
			int odir = dir;
			for(i = 0; i < 8; i++){
				cur = off + cp->moore[++dir];
				if((img[cur] & Fset) == Fset){
					img[cur] |= Fcont;
					off = cur;
					odir = dir&7;
					dir = (dir+4)&7;
					break;
				}
			}
/* this should be moved to the above loop as an "else" branch */
			if(npt < apt){
				int xcoord, ycoord;
				ycoord = off / width;
				xcoord = off % width;
				if(fillrule){
					int topleft = 0;
					for(idir += 1; (idir&7) != odir; idir++){
						if(!topleft && idir == 0){
							ptappend(pt, apt, &npt, xcoord, ycoord);
							topleft = 1;
						}
						if(idir == 2){
							ptappend(pt, apt, &npt, xcoord, ycoord+1);
							topleft = 0;
						}
						if(idir == 4){
							ptappend(pt, apt, &npt, xcoord+1, ycoord);
							topleft = 0;
						}
						if(!topleft && idir == 6){
							ptappend(pt, apt, &npt, xcoord, ycoord);
							topleft = 1;
						}
					}
				} else {
					ptappend(pt, apt, &npt, xcoord, ycoord);
				}
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
		cp->off++;
	} 

	return npt;
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
