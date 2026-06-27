CC = gcc
CFLAGS = -std=c11 -O3 -Wall -Wno-unused -fPIC
LDFLAGS = -lm

all: libkdtree.so libkdtree.a

2d/kd.o: 2d/kd.c 2d/kd.h
	$(CC) $(CFLAGS) -c 2d/kd.c -o $@

2d/kd_64.o: 2d/kd.c 2d/kd.h
	$(CC) $(CFLAGS) -DCOORD_64 -c 2d/kd.c -o $@

2d/kd_128.o: 2d/kd.c 2d/kd.h
	$(CC) $(CFLAGS) -DCOORD_128 -c 2d/kd.c -o $@

3d/kd.o: 3d/kd.c 3d/kd.h
	$(CC) $(CFLAGS) -c 3d/kd.c -o $@

3d/kd_64.o: 3d/kd.c 3d/kd.h
	$(CC) $(CFLAGS) -DCOORD_64 -c 3d/kd.c -o $@

3d/kd_128.o: 3d/kd.c 3d/kd.h
	$(CC) $(CFLAGS) -DCOORD_128 -c 3d/kd.c -o $@

libkdtree.so: 2d/kd.o 2d/kd_64.o 2d/kd_128.o 3d/kd.o 3d/kd_64.o 3d/kd_128.o
	$(CC) -shared -o $@ $^ $(LDFLAGS)

libkdtree.a: 2d/kd.o 2d/kd_64.o 2d/kd_128.o 3d/kd.o 3d/kd_64.o 3d/kd_128.o
	ar rcs $@ $^

clean:
	rm -f 2d/*.o 3d/*.o libkdtree.so libkdtree.a

install: libkdtree.so libkdtree.a
	install -D -m 755 libkdtree.so $(DESTDIR)/usr/lib64/libkdtree.so
	install -D -m 644 libkdtree.a $(DESTDIR)/usr/lib64/libkdtree.a
	install -D -m 644 include/kdtree.h $(DESTDIR)/usr/include/kdtree/kdtree.h

test:
	echo Done.
