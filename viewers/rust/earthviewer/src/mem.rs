/// Allocates via raylib's own `MemAlloc` rather than Rust's global
/// allocator. This matters because dropping a raylib-rs `Mesh` calls the
/// real C `UnloadMesh`, which frees every non-null buffer pointer via
/// `RL_FREE` (= MemFree, plain `free()` by default) - allocating with the
/// matching `MemAlloc` up front guarantees that free is legitimate, rather
/// than relying on Rust's allocator happening to be free()-compatible.
pub(crate) unsafe fn mem_alloc<T>(count: usize) -> *mut T {
    let bytes = (count * std::mem::size_of::<T>()) as u32;
    let ptr = raylib::ffi::MemAlloc(bytes) as *mut T;
    assert!(!ptr.is_null(), "MemAlloc failed allocating {bytes} bytes");
    ptr
}
