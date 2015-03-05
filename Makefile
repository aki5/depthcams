
.PHONY: libs

CFLAGS=-O3 -fomit-frame-pointer -W -Wall -Ilibs/include -I../libdraw3
#CFLAGS=-g -W -Wall -Ilibs/include -I../libdraw3
#CFLAGS=-O2 -W -Wall -Ilibs/include -I../libdraw3

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

ds325: $(OBJS)
	$(CC) -g -o $@ $(OBJS) -Llibs/lib $(LIBS)

../libdraw3/libdraw3.a:
	make -C ../libdraw3 libdraw3.a

clean:
	rm -f *.o ds325 perf.data perf.data.old

nuke: clean
	make -C libs nuke

libs:
	make -C libs 
