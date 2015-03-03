
typedef struct Trilist Trilist;
struct Trilist {
	short *tris;
	uchar *colors;
	int ntris;
	int atris;
};

static inline void
inittrilist(Trilist *tris)
{
	memset(tris, 0, sizeof tris[0]);
}

static inline void
addtrilist(Trilist *tris, short *a, short *b, short *c, uchar *col)
{
	int ntris, atris;
	short *tri;
	uchar *color;

	ntris = tris->ntris;
	atris = tris->atris;
	if(tris->ntris >= atris){
		atris += atris == 0 ? 512 : atris;
		tris->tris = realloc(tris->tris, 6*atris * sizeof tris->tris[0]);
		tris->colors = realloc(tris->colors, 4*atris * sizeof tris->colors[0]);
		tris->atris = atris;
	}

	tri = tris->tris + 6*ntris;
	color = tris->colors + 4*ntris;

	tri[0] = a[0];
	tri[1] = a[1];
	tri[2] = b[0];
	tri[3] = b[1];
	tri[4] = c[0];
	tri[5] = c[1];

	color[0] = col[0];
	color[1] = col[1];
	color[2] = col[2];
	color[3] = col[3];

	tris->ntris = ntris + 1;
}

static inline void
freetrilist(Trilist *tris)
{
	if(tris->atris != 0){
		free(tris->tris);
		free(tris->colors);
	}
}

