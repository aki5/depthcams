
.PHONY: libs

CFLAGS=-O3 -fomit-frame-pointer -ffast-math -W -Wall -Ilibs/include
#CFLAGS=-g -W -Wall -Ilibs/include

LIBS=\
	-lX11\
	 -lXext\
	 -lm\
	 -lturbojpeg\

OBJS=\
	ds325.o\
	x11.o\

ds325: $(OBJS)
	$(CC) -g -o $@ $(OBJS) -Llibs/lib $(LIBS)

clean:
	rm -f *.o ds325 perf.data perf.data.old

nuke: clean
	make -C libs nuke

libs:
	make -C libs 
