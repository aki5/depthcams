
#include "fitpoly.h"

static inline int
ptptdst2(short *a, short *b)
{
	int abx, aby;
	abx = b[0]-a[0];
	aby = b[1]-a[1];
	return abx*abx + aby*aby;
}

static inline int
muldiv(int a, int p, int q)
{
	unsigned int ua, up, uq, ur;
	int as, ps, qs, rs;
	as = a>>31; // take sign
	ps = p>>31;
	qs = q>>31;
	ua = (a ^ as) - as; // absolute value
	up = (p ^ ps) - ps;
	uq = (q ^ qs) - qs;
	rs = as^ps^qs;
	ur = (up*ua + uq/2) / uq;
	return rs < 0 ? -ur : ur;
}

static inline int
lnptdst2(short *a, short *b, short *p)
{
	short ap[2], ab[2];
	short ortpr[2];
	int tp;
	int tq;

	tq = ptptdst2(a, b);
	if(tq == 0)
		return ptptdst2(a, p);

	ap[0] = p[0] - a[0];
	ap[1] = p[1] - a[1];

	ab[0] = b[0] - a[0];
	ab[1] = b[1] - a[1];

	tp = ap[0]*ab[0] + ap[1]*ab[1];
	if(tp < 0)
		return ptptdst2(a, p);
	if(tp > tq)
		return ptptdst2(b, p);

	ortpr[0] = a[0] + muldiv(ab[0], tp, tq);
	ortpr[1] = a[1] + muldiv(ab[1], tp, tq);

	return ptptdst2(ortpr, p);
}

static inline void
sortput(int *tab, int len, int val)
{
	int j;
	for(j = len-1; j >= 0 && val < tab[j]; j--)
		tab[j+1] = tab[j];
	tab[j+1] = val;
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

/*
 *	do iterative refinement instead of iterative simplification
 *	works well when apoly is small.
 *
 *	1. find farthest point(1) from the center of points.
 *	2. find farthest point(2) from point(1). 
 *	3. we now have a (degenerate) polygon of two points
 *	4. repeatedly add the point with maximum distance from its corresponding polygon segment
 */ 
int
fitpoly(int *poly, int apoly, short *pt, int npt, int dstthr)
{
	int i, j, npoly, pi;
	int dst2, maxdst2, maxi;
	int a, b;
	short cntr[2], *cpt = cntr;

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

	while(npoly < apoly){
		maxi = -1;
		maxdst2 = -1;
		pi = npoly-1;
		a = poly[pi];
		pi = 0;
		b = poly[pi];
		for(i = 0; i < npt; i++){
			if(i == b){
				// step the lineseg we are measuring from
				pi = pi == npoly-1 ? 0 : pi + 1;
				a = b;
				b = poly[pi];
			}
			dst2 = lnptdst2(pt + 2*a, pt + 2*b, pt + 2*i);
			if(dst2 > maxdst2){
				maxdst2 = dst2;
				maxi = i;
			}
		}
		if(maxdst2 < dstthr*dstthr)
			 goto out;
		sortput(poly, npoly, maxi);
		npoly++;
	}
out:
	return npoly;
}
