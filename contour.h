
enum {
	Fid = 0x7f,
//	Fset = 0x0f,
	Fcont = 0x80,
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
	short stride;
};

void initcontour(Contour *cp, uchar *img, int width, int height, int stride);
int nextcontour(Contour *cp, short *pt, int apt, int fillrule, int *idp);
void resetcontour(Contour *cp);
