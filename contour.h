
enum {
	Fset = 1,
	Fcont = 2,
	Ffix = 4,
};

typedef unsigned char uchar;
typedef struct Contour Contour;

struct Contour {
	uchar *img;
	int off;
	int end;
	int moore[16];
	short width;
	short height;
};

void initcontour(Contour *cp, uchar *img, int width, int height);
int nextcontour(Contour *cp, short *pt, int apt, int fillrule);
void resetcontour(Contour *cp);
void erodecontour(Contour *cp);
void setcontour(Contour *cp, short *pt, int npt, int bit);
void clearcontour(Contour *cp, short *pt, int npt, int bit);
