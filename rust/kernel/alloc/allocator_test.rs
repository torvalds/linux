// SPDX-License-Identifier: GPL-2.0

//! So far the kernel's `Box` and `Vec` types can't be used by userspace test cases, since all users
//! of those types (e.g. `CString`) use kernel allocators for instantiation.
//!
//! In order to allow userspace test cases to make use of such types as well, implement the
//! `Cmalloc` allocator within the allocator_test module and type alias all kernel allocators to
//! `Cmalloc`. The `Cmalloc` allocator uses libc's `realloc()` function as allocator backend.

#![allow(missing_docs)]

use super::{flags::*, AllocError, Allocator, Flags};
use core::alloc::Layout;
use core::cmp;
use core::ptr;
use core::ptr::NonNull;

/// The userspace allocator based on libc.
pub struct Cmalloc;

pub type Kmalloc = Cmalloc;
pub type Vmalloc = Kmalloc;
pub type KVmalloc = Kmalloc;

extern "C" {
    #[link_name = "aligned_alloc"]
    fn libc_aligned_alloc(align: usize, size: usize) -> *mut crate::ffi::c_void;

    #[link_name = "free"]
    fn libc_free(ptr: *mut crate::ffi::c_void);
}

// SAFETY:
// - memory remains valid until it is explicitly freed,
// - passing a pointer to a valid memory allocation created by this `Allocator` is always OK,
// - `realloc` provides the guarantees as provided in the `# Guarantees` section.
unsafe impl Allocator for Cmalloc {
    unsafe fn realloc(
        ptr: Option<NonNull<u8>>,
        layout: Layout,
        old_layout: Layout,
        flags: Flags,
    ) -> Result<NonNull<[u8]>, AllocError> {
        let src = match ptr {
            Some(src) => {
                if old_layout.size() == 0 {
                    ptr::null_mut()
                } else {
                    src.as_ptr()
                }
            }
            None => ptr::null_mut(),
        };

        if layout.size() == 0 {
            // SAFETY: `src` is either NULL or was previously allocated with this `Allocator`
            unsafe { libc_free(src.cast()) };

            return Ok(NonNull::slice_from_raw_parts(
                crate::alloc::dangling_from_layout(layout),
                0,
            ));
        }

        // SAFETY: Returns either NULL or a pointer to a memory allocation that satisfies or
        // exceeds the given size and alignment requirements.
        let dst = unsafe { libc_aligned_alloc(layout.align(), layout.size()) } as *mut u8;
        let dst = NonNull::new(dst).ok_or(AllocError)?;

        if flags.contains(__GFP_ZERO) {
            // SAFETY: The preceding calls to `libc_aligned_alloc` and `NonNull::new`
            // guarantee that `dst` points to memory of at least `layout.size()` bytes.
            unsafe { dst.as_ptr().write_bytes(0, layout.size()) };
        }

        if !src.is_null() {
            // SAFETY:
            // - `src` has previously been allocated with this `Allocator`; `dst` has just been
            //   newly allocated, hence the memory regions do not overlap.
            // - both` src` and `dst` are properly aligned and valid for reads and writes
            unsafe {
                ptr::copy_nonoverlapping(
                    src,
                    dst.as_ptr(),
                    cmp::min(layout.size(), old_layout.size()),
                )
            };
        }

        // SAFETY: `src` is either NULL or was previously allocated with this `Allocator`
        unsafe { libc_free(src.cast()) };

        Ok(NonNull::slice_from_raw_parts(dst, layout.size()))
    }
}
