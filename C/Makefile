.PHONY: all clean install test

CC = gcc
CFLAGS = -std=c11 -g -Wall -Wno-unused -fPIC
LDFLAGS = -lm

# Machine-specific path to a locally-built raylib checkout (see viewer/
# earth_viewer below, and the CI note on `all` below).
RAYLIB_DIR = /home/murf/external-sources/raylib/src

# viewer is deliberately not part of `all`: it links against a locally-built
# raylib via a machine-specific path (see the viewer target below), which
# CI has no reason to install - `make test` never touches viewer, it only
# exercises the kdtree library itself. Build it explicitly with `make viewer`
# wherever raylib is actually available.
all: libkdtree.so libkdtree.a fits2kd csv2fits kd2lod build_metatree

viewer: viewer.c include/kdtree.h include/kd2lod.h
	$(CC) $(CFLAGS) -std=gnu11 viewer.c -o $@ -Iinclude -I$(RAYLIB_DIR) -L$(RAYLIB_DIR) -Wl,-rpath,$(RAYLIB_DIR) -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

# earth_viewer is excluded from `all` for the same reason as viewer above: it
# links against a locally-built raylib CI has no reason to install.
#
# The JPEG check below is a build-time nudge, not a hard requirement:
# earth_daymap.jpg is optional (see README-cities.md), and earth_viewer
# falls back to a plain sphere at runtime if raylib can't load it -- this
# just surfaces *why* that fallback might trigger before it's a surprise at
# runtime, since SUPPORT_FILEFORMAT_JPG is off in a default raylib build.
earth_viewer: earth_viewer.c include/kdtree.h
	@if ! grep -Eq '^[[:space:]]*#define[[:space:]]+SUPPORT_FILEFORMAT_JPG[[:space:]]+1' $(RAYLIB_DIR)/config.h 2>/dev/null; then \
		echo "Note: $(RAYLIB_DIR)/config.h doesn't have SUPPORT_FILEFORMAT_JPG enabled."; \
		echo "      earth_viewer will still build, but a real earth_daymap.jpg texture"; \
		echo "      (see README-cities.md) won't load -- it'll fall back to a plain"; \
		echo "      sphere. Set SUPPORT_FILEFORMAT_JPG to 1 there and rebuild raylib"; \
		echo "      to fix, or supply a PNG texture instead (enabled by default)."; \
	fi
	$(CC) $(CFLAGS) -std=gnu11 earth_viewer.c -o $@ -Iinclude -I$(RAYLIB_DIR) -L$(RAYLIB_DIR) -Wl,-rpath,$(RAYLIB_DIR) -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

fits2kd: fits2kd.c libkdtree.a
	$(CC) $(CFLAGS) -std=gnu11 -O3 fits2kd.c libkdtree.a -o $@ -Iinclude -lcfitsio -lm

kd2lod: kd2lod.c include/kdtree.h include/kd2lod.h
	$(CC) $(CFLAGS) -std=gnu11 -O3 kd2lod.c -o $@ -Iinclude -lm

build_metatree: build_metatree.c libkdtree.a
	$(CC) $(CFLAGS) -std=gnu11 -O3 build_metatree.c libkdtree.a -o $@ -Iinclude -lpthread -lm

geonames2kd: geonames2kd.c libkdtree.a
	$(CC) $(CFLAGS) -std=gnu11 -O3 geonames2kd.c libkdtree.a -o $@ -Iinclude -lm

build_city_metatree: build_city_metatree.c libkdtree.a
	$(CC) $(CFLAGS) -std=gnu11 -O3 build_city_metatree.c libkdtree.a -o $@ -Iinclude -lm

csv2fits: csv2fits.c
	$(CC) $(CFLAGS) -std=gnu11 -O3 csv2fits.c -o $@ -lcfitsio -lz -lm

geo_utils.o: geo_utils.c include/geo_utils.h
	$(CC) $(CFLAGS) -Iinclude -c geo_utils.c -o $@

2d/kd.o: 2d/kd.c 2d/kd.h
	$(CC) $(CFLAGS) -c 2d/kd.c -o $@

2d/kd_64.o: 2d/kd.c 2d/kd.h
	$(CC) $(CFLAGS) -DCOORD_64 -c 2d/kd.c -o $@

2d/kd_128.o: 2d/kd.c 2d/kd.h
	$(CC) $(CFLAGS) -DCOORD_128 -c 2d/kd.c -o $@

2d/kd_f64.o: 2d/kd.c 2d/kd.h
	$(CC) $(CFLAGS) -DCOORD_F64 -c 2d/kd.c -o $@

3d/kd.o: 3d/kd.c 3d/kd.h
	$(CC) $(CFLAGS) -c 3d/kd.c -o $@

3d/kd_64.o: 3d/kd.c 3d/kd.h
	$(CC) $(CFLAGS) -DCOORD_64 -c 3d/kd.c -o $@

3d/kd_128.o: 3d/kd.c 3d/kd.h
	$(CC) $(CFLAGS) -DCOORD_128 -c 3d/kd.c -o $@

3d/kd_f64.o: 3d/kd.c 3d/kd.h
	$(CC) $(CFLAGS) -DCOORD_F64 -c 3d/kd.c -o $@

libkdtree.so: 2d/kd.o 2d/kd_64.o 2d/kd_128.o 2d/kd_f64.o 3d/kd.o 3d/kd_64.o 3d/kd_128.o 3d/kd_f64.o geo_utils.o
	$(CC) -shared -o $@ 2d/kd.o 2d/kd_64.o 2d/kd_128.o 2d/kd_f64.o 3d/kd.o 3d/kd_64.o 3d/kd_128.o 3d/kd_f64.o geo_utils.o $(LDFLAGS)

libkdtree.a: 2d/kd.o 2d/kd_64.o 2d/kd_128.o 2d/kd_f64.o 3d/kd.o 3d/kd_64.o 3d/kd_128.o 3d/kd_f64.o geo_utils.o
	ar rcs $@ 2d/kd.o 2d/kd_64.o 2d/kd_128.o 2d/kd_f64.o 3d/kd.o 3d/kd_64.o 3d/kd_128.o 3d/kd_f64.o geo_utils.o

clean:
	rm -f 2d/*.o 3d/*.o geo_utils.o libkdtree.so libkdtree.a 2d/test_* 3d/test_* viewer fits2kd csv2fits kd2lod build_metatree geo_utils_test earth_viewer geonames2kd build_city_metatree

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
TESTS_2D_F64 = $(patsubst %, 2d/test_%_f64, $(TEST_TYPES))
TESTS_3D_F64 = $(patsubst %, 3d/test_%_f64, $(TEST_TYPES))

ALL_TESTS = $(TESTS_2D) $(TESTS_3D) $(TESTS_2D_64) $(TESTS_3D_64) $(TESTS_2D_128) $(TESTS_3D_128) $(TESTS_2D_F64) $(TESTS_3D_F64) geo_utils_test

2d/test_%: 2d/kd_test_%.c 2d/kd.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

2d/test_%_64: 2d/kd_test_%.c 2d/kd_64.o
	$(CC) $(CFLAGS) -DCOORD_64 -o $@ $^ $(LDFLAGS)

2d/test_%_128: 2d/kd_test_%.c 2d/kd_128.o
	$(CC) $(CFLAGS) -DCOORD_128 -o $@ $^ $(LDFLAGS)

2d/test_%_f64: 2d/kd_test_%.c 2d/kd_f64.o
	$(CC) $(CFLAGS) -DCOORD_F64 -o $@ $^ $(LDFLAGS)

3d/test_%: 3d/kd_test_%.c 3d/kd.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

3d/test_%_64: 3d/kd_test_%.c 3d/kd_64.o
	$(CC) $(CFLAGS) -DCOORD_64 -o $@ $^ $(LDFLAGS)

3d/test_%_128: 3d/kd_test_%.c 3d/kd_128.o
	$(CC) $(CFLAGS) -DCOORD_128 -o $@ $^ $(LDFLAGS)

3d/test_%_f64: 3d/kd_test_%.c 3d/kd_f64.o
	$(CC) $(CFLAGS) -DCOORD_F64 -o $@ $^ $(LDFLAGS)

geo_utils_test: geo_utils_test.c geo_utils.o
	$(CC) $(CFLAGS) -Iinclude -o $@ $^ $(LDFLAGS)

test: $(ALL_TESTS)
	@for t in $(ALL_TESTS); do \
		echo "Running $$t..."; \
		./$$t || exit 1; \
	done
	@echo "All tests passed."
