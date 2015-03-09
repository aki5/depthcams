
#include "os.h"
#include "fitpoly.h"
#include "draw3.h"

static inline int
ptptdst2(short *a, short *b)
{
	int abx, aby;
	abx = b[0]-a[0];
	aby = b[1]-a[1];
	return abx*abx + aby*aby;
}

static inline void
ptsegdst2(short *p, short *a, short *b, int *dstp2, int *dstq2)
{
	short ap[2], ab[2];
	int tp, tq, det;

	tq = ptptdst2(a, b);
	if(tq == 0){
		*dstp2 = ptptdst2(a, p);
		*dstq2 = 1;
		return;
	}
	ap[0] = p[0] - a[0];
	ap[1] = p[1] - a[1];
	ab[0] = b[0] - a[0];
	ab[1] = b[1] - a[1];
	tp = dot2i(ab, ap);

	if(tp < 0){
		*dstp2 = ptptdst2(a, p);
		*dstq2 = 1;
		return;
	}
	if(tp > tq){
		*dstp2 = ptptdst2(b, p);
		*dstq2 = 1;
		return;
	}

	det = det2i(ap[0], ap[1], ab[0], ab[1]);
	*dstp2 = det*det;
	*dstq2 = tq;
	return;
/*
	short ortpr[2];
	ortpr[0] = a[0] + muldiv(ab[0], tp, tq);
	ortpr[1] = a[1] + muldiv(ab[1], tp, tq);
*/
}

static inline int
sortput(int *tab, int len, int val)
{
	int j;
	for(j = len-1; j >= 0 && val < tab[j]; j--)
		tab[j+1] = tab[j];
	tab[j+1] = val;
	return j+1;
}

static inline void
findcenter(short *points, int npoints, short *c)
{
	int i, c0, c1;

	c0 = points[0];
	c1 = points[1];
	for(i = 1; i < npoints; i++){
		c0 += points[2*i+0];
		c1 += points[2*i+1];
	}
	c[0] = c0 / npoints;
	c[1] = c1 / npoints;	
}

static inline int
findmaxdst2(short *pt, int npt, int a, int b, int *maxip, int *maxdst2p)
{
	int dstp2, dstq2;
	int maxdstp2, maxdstq2;
	int i, maxi;

	maxdstp2 = -1;
	maxdstq2 = 1;
	maxi = -1;

	for(i = a+1; i < npt; i++){
		if(i == b)
			break;
		ptsegdst2(pt + 2*i, pt + 2*a, pt + 2*b, &dstp2, &dstq2);
		if((long long)dstp2*maxdstq2 > (long long)maxdstp2*dstq2){
			maxdstp2 = dstp2;
			maxdstq2 = dstq2;
			maxi = i;
		}
	}
	if(i == npt){
		for(i = 0; i != b; i++){
			ptsegdst2(pt + 2*i, pt + 2*a, pt + 2*b, &dstp2, &dstq2);
			if((long long)dstp2*maxdstq2 > (long long)maxdstp2*dstq2){
				maxdstp2 = dstp2;
				maxdstq2 = dstq2;
				maxi = i;
			}
		}
	}
	*maxdst2p = maxdstp2 / maxdstq2;
	*maxip = maxi;
	return maxi == -1 ? -1 : 0;
}

/*
 *	do iterative refinement instead of iterative simplification
 *	works well for small apoly.
 *
 *	1. find farthest point(1) from the center of points.
 *	2. find farthest point(2) from point(1). 
 *	3. we now have a (degenerate) polygon of two points
 *	4. repeatedly add the point with maximum distance to its current polygon segment,
 *	   but the generated segments must not intersect any of the old segments
 */ 
int
fitpoly(int *poly, int apoly, short *pt, int npt, int dstthr)
{
	int i, j, npoly, pi, ni;
	int dst2, maxdst2, maxi;
	short cntr[2], *cpt = cntr;

	int polymaxi[apoly];
	int polymaxdst2[apoly];

	if(npt < 2)
		return -1;

	findcenter(pt, npt, cpt);
	npoly = 0;
	for(j = 0; j < 2; j++){
		maxi = -1;
		maxdst2 = -1;
		for(i = 0; i < npt; i++){
			dst2 = ptptdst2(pt + 2*i,  cpt);
			if(dst2 > maxdst2){
				maxdst2 = dst2;
				maxi = i;
			}
		}
		if(maxdst2 < dstthr*dstthr || npoly+1 >= apoly)
			 goto out;
		sortput(poly, npoly, maxi);
		npoly++;
		cpt = pt + 2*maxi;
	}

	findmaxdst2(pt, npt, poly[1], poly[0], polymaxi+1, polymaxdst2+1);
	findmaxdst2(pt, npt, poly[0], poly[1], polymaxi+0, polymaxdst2+0);

	while(npoly < apoly){

		int maxi2 = -1;
		int maxd2 = -1;
		for(i = 0; i < npoly; i++){
			if(polymaxdst2[i] > maxd2){
				maxd2 = polymaxdst2[i];
				maxi2 = polymaxi[i];
			}
		}
		maxi = maxi2;
		maxdst2 = maxd2;

		if(maxdst2 == -1)
			return -1;
		if(maxdst2 < dstthr*dstthr)
			 goto out;

		i = sortput(poly, npoly, maxi);
		memmove(polymaxi+i+1, polymaxi+i, (npoly-i) * sizeof polymaxi[0]);
		memmove(polymaxdst2+i+1, polymaxdst2+i, (npoly-i) * sizeof polymaxdst2[0]);
		npoly++;

		pi = (i+npoly-1) % npoly;
		ni = (i+1) % npoly;
		findmaxdst2(pt, npt, poly[pi], poly[i], polymaxi+pi, polymaxdst2+pi);
		findmaxdst2(pt, npt, poly[i], poly[ni], polymaxi+i, polymaxdst2+i);
	}
out:
	return npoly;
}
