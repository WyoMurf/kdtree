.PHONY: all clean install test

CC = gcc
CFLAGS = -std=c11 -g -Wall -Wno-unused -fPIC
LDFLAGS = -lm

all: libkdtree.so libkdtree.a viewer

viewer: viewer.c
	$(CC) $(CFLAGS) -std=gnu11 viewer.c -o $@ -I/home/murf/external-sources/raylib/src -L/home/murf/external-sources/raylib/src -Wl,-rpath,/home/murf/external-sources/raylib/src -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

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
	rm -f 2d/*.o 3d/*.o libkdtree.so libkdtree.a 2d/test_* 3d/test_* viewer

install: libkdtree.so libkdtree.a
	install -D -m 755 libkdtree.so $(DESTDIR)/usr/lib64/libkdtree.so
	install -D -m 644 libkdtree.a $(DESTDIR)/usr/lib64/libkdtree.a
	install -D -m 644 include/kdtree.h $(DESTDIR)/usr/include/kdtree/kdtree.h

TEST_TYPES = soft hard nearest serialize
TESTS_2D = $(patsubst %, 2d/test_%, $(TEST_TYPES))
TESTS_3D = $(patsubst %, 3d/test_%, $(TEST_TYPES))
TESTS_2D_64 = $(patsubst %, 2d/test_%_64, $(TEST_TYPES))
TESTS_3D_64 = $(patsubst %, 3d/test_%_64, $(TEST_TYPES))
TESTS_2D_128 = $(patsubst %, 2d/test_%_128, $(TEST_TYPES))
TESTS_3D_128 = $(patsubst %, 3d/test_%_128, $(TEST_TYPES))

ALL_TESTS = $(TESTS_2D) $(TESTS_3D) $(TESTS_2D_64) $(TESTS_3D_64) $(TESTS_2D_128) $(TESTS_3D_128)

2d/test_%: 2d/kd_test_%.c 2d/kd.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

2d/test_%_64: 2d/kd_test_%.c 2d/kd_64.o
	$(CC) $(CFLAGS) -DCOORD_64 -o $@ $^ $(LDFLAGS)

2d/test_%_128: 2d/kd_test_%.c 2d/kd_128.o
	$(CC) $(CFLAGS) -DCOORD_128 -o $@ $^ $(LDFLAGS)

3d/test_%: 3d/kd_test_%.c 3d/kd.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

3d/test_%_64: 3d/kd_test_%.c 3d/kd_64.o
	$(CC) $(CFLAGS) -DCOORD_64 -o $@ $^ $(LDFLAGS)

3d/test_%_128: 3d/kd_test_%.c 3d/kd_128.o
	$(CC) $(CFLAGS) -DCOORD_128 -o $@ $^ $(LDFLAGS)

test: $(ALL_TESTS)
	@for t in $(ALL_TESTS); do \
		echo "Running $$t..."; \
		./$$t || exit 1; \
	done
	@echo "All tests passed."
