
lib/libturbojpeg.a: libjpeg-turbo-build/Makefile
	make -C libjpeg-turbo-build install

libjpeg-turbo-build/Makefile: libjpeg-turbo-1.4.0/configure
	mkdir libjpeg-turbo-build && cd libjpeg-turbo-build && ../libjpeg-turbo-1.4.0/configure --disable-shared --prefix=$(CURDIR)

libjpeg-turbo-1.4.0/configure:
	tar xvf libjpeg-turbo-1.4.0.tar.gz

clean:
	rm -rf libjpeg-turbo-1.4.0 libjpeg-turbo-build bin lib include share man


