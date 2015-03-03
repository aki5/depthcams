
typedef unsigned char uchar;

static inline int
det2i(
	short a, short b,
	short c, short d
){
	return a*d - b*c;
}

static inline int
ori2i(short *a, short *b, short *c)
{
	short acx, bcx, acy, bcy;
	acx = a[0] - c[0];
	bcx = b[0] - c[0];
	acy = a[1] - c[1];
	bcy = b[1] - c[1];
	return det2i(acx, bcx, acy, bcy);
}

static inline short
min(short a, short b)
{
	return a < b ? a : b;
}

static inline short
max(short a, short b)
{
	return a > b ? a : b;
}

static inline int
topleft_ccw(short *a, short *b)
{
	if(a[1] == b[1] && a[0] > b[0]) /* top: horizontal, goes left */
		return 0;
	if(a[1] != b[1] && a[1] > b[1]) /* left: non-horizontal, goes down (up in the stupid inverted y screen space) */
		return 0;
	return -1;
}

static inline void
drawpixel(uchar *img, int width, short x, short y, uchar *color)
{
	int off = y*width+x;
	*((unsigned int *)img+off) = *(unsigned int *)color;
}

void
drawtri(uchar *img, int width, int height, short *tris, uchar *colors, int ntris)
{
	short azero, bzero, czero;
	short xstart, xend;
	short ystart, yend;
	short p[2];
	int i;
	for(i = 0; i < ntris; i++){
		short *a, *b, *c;

		a = tris+6*i+0;
		b = tris+6*i+2;
		c = tris+6*i+4;

		xstart = max(0, min(a[0], min(b[0], c[0])));
		ystart = max(0, min(a[1], min(b[1], c[1])));
		xend = min(width-1, max(a[0], max(b[0], c[0]))) + 1;
		yend = min(width-1, max(a[1], max(b[1], c[1]))) + 1;

		azero = topleft_ccw(a, b);
		bzero = topleft_ccw(b, c);
		czero = topleft_ccw(c, a);

		for(p[1] = ystart; p[1] < yend; p[1]++)
			for(p[0] = xstart; p[0] < xend; p[0]++)
				if(ori2i(a, b, p) <= azero && ori2i(b, c, p) <= bzero && ori2i(c, a, p) <= czero)
					drawpixel(img, width, p[0], p[1], colors+4*i);
	}
}
