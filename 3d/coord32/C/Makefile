CC = gcc
CFLAGS = -std=c11 -O3 -Wall -Wno-unused -fPIC
LDFLAGS = -lm

TESTS = kd_test_soft kd_test_hard kd_test_nearest benchmark

.PHONY: all test clean install lib

all: lib $(TESTS)

lib: libkdtree32.so libkdtree32.a

kd.o: kd.c kd.h
	$(CC) $(CFLAGS) -c kd.c

libkdtree32.so: kd.o
	$(CC) -shared -o $@ kd.o $(LDFLAGS)

libkdtree32.a: kd.o
	ar rcs $@ kd.o

kd_test_soft: kd.o kd_test_soft.c kd.h
	$(CC) $(CFLAGS) -o $@ kd.o kd_test_soft.c $(LDFLAGS)

kd_test_hard: kd.o kd_test_hard.c kd.h
	$(CC) $(CFLAGS) -o $@ kd.o kd_test_hard.c $(LDFLAGS)

kd_test_nearest: kd.o kd_test_nearest.c kd.h
	$(CC) $(CFLAGS) -o $@ kd.o kd_test_nearest.c $(LDFLAGS)

benchmark: kd.o benchmark.c kd.h
	$(CC) $(CFLAGS) -o $@ kd.o benchmark.c $(LDFLAGS)

test: $(TESTS)
	@echo "=== Running 32-bit tests ==="
	./kd_test_soft && ./kd_test_hard && ./kd_test_nearest && ./benchmark

clean:
	rm -f $(TESTS) libkdtree32.so libkdtree32.a *.o

install: lib
	install -D -m 755 libkdtree32.so $(DESTDIR)/usr/lib64/libkdtree32.so
	install -D -m 644 libkdtree32.a $(DESTDIR)/usr/lib64/libkdtree32.a
	install -D -m 644 kd.h $(DESTDIR)/usr/include/kdtree32/kd.h
