# 2D KD-Tree (C Implementation)

This directory contains the unified C implementation of a 2D KD-Tree, supporting both 32-bit and 64-bit coordinate data types dynamically at compile time.

## Compilation & Configuration

The bit width of the internal KD-Tree coordinates (`coord_t`) is determined by the `WIDTH` variable during the `make` build process. It defaults to 32-bit.

To build the 32-bit static and shared libraries (`libkdtree32.so` / `libkdtree32.a`):
```bash
make
# OR
make WIDTH=32
```

To build the 64-bit libraries (`libkdtree64.so` / `libkdtree64.a`):
```bash
make WIDTH=64
```

## Function Naming & Linking

Because C lacks native function overloading, the functions are exposed with dimension and bit-width prefixes/suffixes to avoid linker collisions if you choose to link both 32-bit and 64-bit variants into the same application. 

**Examples of exported functions:**
- `kd_2d_32_create()` / `kd_2d_64_create()`
- `kd_2d_32_insert()` / `kd_2d_64_insert()`
- `kd_2d_32_nearest()` / `kd_2d_64_nearest()`

You can use the unified macro-based names (e.g., `kd_create`, `kd_insert`) in your source code, and the compiler will automatically route them to the correct bit-width implementation as long as the corresponding `COORD_64` macro is set correctly during inclusion.
