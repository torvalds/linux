// SPDX-License-Identifier: GPL-2.0

//! Allocator support.

use super::{flags::*, Flags};
use core::alloc::{GlobalAlloc, Layout};
use core::ptr;

struct KernelAllocator;

/// Calls `krealloc` with a proper size to alloc a new object aligned to `new_layout`'s alignment.
///
/// # Safety
///
/// - `ptr` can be either null or a pointer which has been allocated by this allocator.
/// - `new_layout` must have a non-zero size.
pub(crate) unsafe fn krealloc_aligned(ptr: *mut u8, new_layout: Layout, flags: Flags) -> *mut u8 {
    // Customized layouts from `Layout::from_size_align()` can have size < align, so pad first.
    let layout = new_layout.pad_to_align();

    let mut size = layout.size();

    if layout.align() > bindings::ARCH_SLAB_MINALIGN {
        // The alignment requirement exceeds the slab guarantee, thus try to enlarge the size
        // to use the "power-of-two" size/alignment guarantee (see comments in `kmalloc()` for
        // more information).
        //
        // Note that `layout.size()` (after padding) is guaranteed to be a multiple of
        // `layout.align()`, so `next_power_of_two` gives enough alignment guarantee.
        size = size.next_power_of_two();
    }

    // SAFETY:
    // - `ptr` is either null or a pointer returned from a previous `k{re}alloc()` by the
    //   function safety requirement.
    // - `size` is greater than 0 since it's either a `layout.size()` (which cannot be zero
    //   according to the function safety requirement) or a result from `next_power_of_two()`.
    unsafe { bindings::krealloc(ptr as *const core::ffi::c_void, size, flags.0) as *mut u8 }
}

unsafe impl GlobalAlloc for KernelAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // SAFETY: `ptr::null_mut()` is null and `layout` has a non-zero size by the function safety
        // requirement.
        unsafe { krealloc_aligned(ptr::null_mut(), layout, GFP_KERNEL) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        unsafe {
            bindings::kfree(ptr as *const core::ffi::c_void);
        }
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        // SAFETY:
        // - `new_size`, when rounded up to the nearest multiple of `layout.align()`, will not
        //   overflow `isize` by the function safety requirement.
        // - `layout.align()` is a proper alignment (i.e. not zero and must be a power of two).
        let layout = unsafe { Layout::from_size_align_unchecked(new_size, layout.align()) };

        // SAFETY:
        // - `ptr` is either null or a pointer allocated by this allocator by the function safety
        //   requirement.
        // - the size of `layout` is not zero because `new_size` is not zero by the function safety
        //   requirement.
        unsafe { krealloc_aligned(ptr, layout, GFP_KERNEL) }
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        // SAFETY: `ptr::null_mut()` is null and `layout` has a non-zero size by the function safety
        // requirement.
        unsafe { krealloc_aligned(ptr::null_mut(), layout, GFP_KERNEL | __GFP_ZERO) }
    }
}

#[global_allocator]
static ALLOCATOR: KernelAllocator = KernelAllocator;

// See <https://github.com/rust-lang/rust/pull/86844>.
#[no_mangle]
static __rust_no_alloc_shim_is_unstable: u8 = 0;
