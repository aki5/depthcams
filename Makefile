
.PHONY: libs

CFLAGS=-O3 -W -Wall

LIBS=\
	-lX11\
	 -lXext\
	 -lm\

OBJS=\
	contour.o\
	fitpoly.o\
	ds325.o\
	process.o\
	../libdraw3/libdraw3.a\
	../libtess2/libtess3.a\

%.o: %.c
	$(CC) $(CFLAGS) -I../libtess2 -I../libdraw3 -c -o $@ $<

ds325: $(OBJS)
	$(CC) -g -o $@ $(OBJS) -Llibs/lib $(LIBS)

clean:
	rm -f *.o ds325 perf.data perf.data.old *.a
