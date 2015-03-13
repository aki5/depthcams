
#include <string.h>
#include "contour.h"

typedef unsigned int u32int;

void
initcontour(Contour *cp, uchar *img, int width, int height, int st)
{
	int i;

	cp->img = img;
	cp->width = width;
	cp->height = height;
	cp->stride = st;
	cp->off = st+1;
	cp->end = st*(height-1);

	cp->moore[0] = -1;
	cp->moore[1] = st-1;
	cp->moore[2] = st;
	cp->moore[3] = st+1;
	cp->moore[4] = 1;
	cp->moore[5] = -st+1;
	cp->moore[6] = -st;
	cp->moore[7] = -st-1;

	cp->moore[8] = cp->moore[0];
	cp->moore[9] = cp->moore[1];
	cp->moore[10] = cp->moore[2];
	cp->moore[11] = cp->moore[3];
	cp->moore[12] = cp->moore[4];
	cp->moore[13] = cp->moore[5];
	cp->moore[14] = cp->moore[6];
	cp->moore[15] = cp->moore[7];

	memset(img, Fcont|Fid, width);
	memset(img + (height-1)*width, Fcont|Fid, width);
	for(i = 1; i < height-1; i++){
		img[i*st] = Fcont|Fid;
		img[i*st+width-1] = Fcont|Fid;
	}
}

void
resetcontour(Contour *cp)
{
	cp->off = 0;
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

int
nextcontour(Contour *cp, short *pt, int apt, int fillrule, int *idp)
{
	uchar *img;
	int cur, off, end;
	int i, dir;
	int npt;
	int width;
	int fid;

	if(cp->width != cp->stride)
		abort();

	width = cp->width;
	img = cp->img;
	off = cp->off;
	end = cp->end;

	fid = -1;
	while(off < end){
		int flags = img[off];
		fid = flags&Fid;
		if((flags&Fcont) == 0)
			break;
		do {
			if((img[++off] & Fid) != fid)
				break;
		} while(off < end);
		//off++;
	}
	cp->off = off;
	if(off == end)
		return -1;

	// do one step to set up stopping and dir.
	dir = 0;
	for(i = 0; i < 8; i++){
		cur = off + cp->moore[++dir];
		if((img[cur] & Fid) == fid){
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
				if((img[cur] & Fid) == fid){
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
			if((img[++off] & Fid) != fid)
				break;
		}
		cp->off++;
	} 

	*idp = fid;
	return npt;
}

