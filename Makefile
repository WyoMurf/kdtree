CC = gcc
CFLAGS = -std=c11 -O3 -Wall -Wno-unused -fPIC
LDFLAGS = -lm

TESTS = kd_test_soft kd_test_hard kd_test_nearest

.PHONY: all test clean install lib

all: lib $(TESTS)

lib: libkdtree.so libkdtree.a

kd.o: kd.c kd.h
	$(CC) $(CFLAGS) -c kd.c

libkdtree.so: kd.o
	$(CC) -shared -o $@ kd.o $(LDFLAGS)

libkdtree.a: kd.o
	ar rcs $@ kd.o

kd_test_soft: kd.o kd_test_soft.c kd.h
	$(CC) $(CFLAGS) -o $@ kd.o kd_test_soft.c $(LDFLAGS)

kd_test_hard: kd.o kd_test_hard.c kd.h
	$(CC) $(CFLAGS) -o $@ kd.o kd_test_hard.c $(LDFLAGS)

kd_test_nearest: kd.o kd_test_nearest.c kd.h
	$(CC) $(CFLAGS) -o $@ kd.o kd_test_nearest.c $(LDFLAGS)

test: $(TESTS)
	@echo "=== Running tests ==="
	./kd_test_soft && ./kd_test_hard && ./kd_test_nearest

clean:
	rm -f $(TESTS) libkdtree.so libkdtree.a *.o

install: lib
	install -D -m 755 libkdtree.so $(DESTDIR)/usr/lib64/libkdtree.so
	install -D -m 644 libkdtree.a $(DESTDIR)/usr/lib64/libkdtree.a
	install -D -m 644 kd.h $(DESTDIR)/usr/include/kdtree/kd.h
