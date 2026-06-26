CC = gcc
CFLAGS = -std=c11 -O3 -Wall -Wno-unused -fPIC
LDFLAGS = -lm

WIDTH ?= 32

ifeq ($(WIDTH), 64)
    CFLAGS += -DCOORD_64
endif

LIBNAME = libkdtree$(WIDTH)
TESTS = kd_test_soft kd_test_hard kd_test_nearest benchmark

.PHONY: all test clean install lib

all: lib $(TESTS)

lib: $(LIBNAME).so $(LIBNAME).a

kd.o: kd.c kd.h
	$(CC) $(CFLAGS) -c kd.c

$(LIBNAME).so: kd.o
	$(CC) -shared -o $@ kd.o $(LDFLAGS)

$(LIBNAME).a: kd.o
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
	@echo "=== Running 3D C tests (WIDTH=$(WIDTH)) ==="
	./kd_test_soft && ./kd_test_hard && ./kd_test_nearest && ./benchmark

clean:
	rm -f $(TESTS) libkdtree32.so libkdtree32.a libkdtree64.so libkdtree64.a *.o

install: lib
	install -D -m 755 $(LIBNAME).so $(DESTDIR)/usr/lib64/$(LIBNAME).so
	install -D -m 644 $(LIBNAME).a $(DESTDIR)/usr/lib64/$(LIBNAME).a
	install -D -m 644 kd.h $(DESTDIR)/usr/include/$(LIBNAME)/kd.h
