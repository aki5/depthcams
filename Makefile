
CFLAGS=-O3 -fomit-frame-pointer -ffast-math -W -Wall

OBJS=\
	ds325.o\
	x11.o\

ds325: $(OBJS)
	$(CC) -o $@ $(OBJS) -lX11 -lm -lpthread

clean:
	rm -f *.o ds325

