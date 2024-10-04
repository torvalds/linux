// SPDX-License-Identifier: GPL-2.0

//! Allocator support.
//!
//! Documentation for the kernel's memory allocators can found in the "Memory Allocation Guide"
//! linked below. For instance, this includes the concept of "get free page" (GFP) flags and the
//! typical application of the different kernel allocators.
//!
//! Reference: <https://docs.kernel.org/core-api/memory-allocation.html>

use super::{flags::*, Flags};
use core::alloc::{GlobalAlloc, Layout};
use core::ptr;
use core::ptr::NonNull;

use crate::alloc::{AllocError, Allocator};
use crate::bindings;
use crate::pr_warn;

/// The contiguous kernel allocator.
///
/// `Kmalloc` is typically used for physically contiguous allocations up to page size, but also
/// supports larger allocations up to `bindings::KMALLOC_MAX_SIZE`, which is hardware specific.
///
/// For more details see [self].
pub struct Kmalloc;

/// The virtually contiguous kernel allocator.
///
/// `Vmalloc` allocates pages from the page level allocator and maps them into the contiguous kernel
/// virtual space. It is typically used for large allocations. The memory allocated with this
/// allocator is not physically contiguous.
///
/// For more details see [self].
pub struct Vmalloc;

/// Returns a proper size to alloc a new object aligned to `new_layout`'s alignment.
fn aligned_size(new_layout: Layout) -> usize {
    // Customized layouts from `Layout::from_size_align()` can have size < align, so pad first.
    let layout = new_layout.pad_to_align();

    // Note that `layout.size()` (after padding) is guaranteed to be a multiple of `layout.align()`
    // which together with the slab guarantees means the `krealloc` will return a properly aligned
    // object (see comments in `kmalloc()` for more information).
    layout.size()
}

/// Calls `krealloc` with a proper size to alloc a new object aligned to `new_layout`'s alignment.
///
/// # Safety
///
/// - `ptr` can be either null or a pointer which has been allocated by this allocator.
/// - `new_layout` must have a non-zero size.
pub(crate) unsafe fn krealloc_aligned(ptr: *mut u8, new_layout: Layout, flags: Flags) -> *mut u8 {
    let size = aligned_size(new_layout);

    // SAFETY:
    // - `ptr` is either null or a pointer returned from a previous `k{re}alloc()` by the
    //   function safety requirement.
    // - `size` is greater than 0 since it's from `layout.size()` (which cannot be zero according
    //   to the function safety requirement)
    unsafe { bindings::krealloc(ptr as *const core::ffi::c_void, size, flags.0) as *mut u8 }
}

/// # Invariants
///
/// One of the following: `krealloc`, `vrealloc`, `kvrealloc`.
struct ReallocFunc(
    unsafe extern "C" fn(*const core::ffi::c_void, usize, u32) -> *mut core::ffi::c_void,
);

impl ReallocFunc {
    // INVARIANT: `krealloc` satisfies the type invariants.
    const KREALLOC: Self = Self(bindings::krealloc);

    // INVARIANT: `vrealloc` satisfies the type invariants.
    const VREALLOC: Self = Self(bindings::vrealloc);

    /// # Safety
    ///
    /// This method has the same safety requirements as [`Allocator::realloc`].
    ///
    /// # Guarantees
    ///
    /// This method has the same guarantees as `Allocator::realloc`. Additionally
    /// - it accepts any pointer to a valid memory allocation allocated by this function.
    /// - memory allocated by this function remains valid until it is passed to this function.
    unsafe fn call(
        &self,
        ptr: Option<NonNull<u8>>,
        layout: Layout,
        old_layout: Layout,
        flags: Flags,
    ) -> Result<NonNull<[u8]>, AllocError> {
        let size = aligned_size(layout);
        let ptr = match ptr {
            Some(ptr) => {
                if old_layout.size() == 0 {
                    ptr::null()
                } else {
                    ptr.as_ptr()
                }
            }
            None => ptr::null(),
        };

        // SAFETY:
        // - `self.0` is one of `krealloc`, `vrealloc`, `kvrealloc` and thus only requires that
        //   `ptr` is NULL or valid.
        // - `ptr` is either NULL or valid by the safety requirements of this function.
        //
        // GUARANTEE:
        // - `self.0` is one of `krealloc`, `vrealloc`, `kvrealloc`.
        // - Those functions provide the guarantees of this function.
        let raw_ptr = unsafe {
            // If `size == 0` and `ptr != NULL` the memory behind the pointer is freed.
            self.0(ptr.cast(), size, flags.0).cast()
        };

        let ptr = if size == 0 {
            crate::alloc::dangling_from_layout(layout)
        } else {
            NonNull::new(raw_ptr).ok_or(AllocError)?
        };

        Ok(NonNull::slice_from_raw_parts(ptr, size))
    }
}

// SAFETY: `realloc` delegates to `ReallocFunc::call`, which guarantees that
// - memory remains valid until it is explicitly freed,
// - passing a pointer to a valid memory allocation is OK,
// - `realloc` satisfies the guarantees, since `ReallocFunc::call` has the same.
unsafe impl Allocator for Kmalloc {
    #[inline]
    unsafe fn realloc(
        ptr: Option<NonNull<u8>>,
        layout: Layout,
        old_layout: Layout,
        flags: Flags,
    ) -> Result<NonNull<[u8]>, AllocError> {
        // SAFETY: `ReallocFunc::call` has the same safety requirements as `Allocator::realloc`.
        unsafe { ReallocFunc::KREALLOC.call(ptr, layout, old_layout, flags) }
    }
}

// SAFETY: TODO.
unsafe impl GlobalAlloc for Kmalloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // SAFETY: `ptr::null_mut()` is null and `layout` has a non-zero size by the function safety
        // requirement.
        unsafe { krealloc_aligned(ptr::null_mut(), layout, GFP_KERNEL) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        // SAFETY: TODO.
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

// SAFETY: `realloc` delegates to `ReallocFunc::call`, which guarantees that
// - memory remains valid until it is explicitly freed,
// - passing a pointer to a valid memory allocation is OK,
// - `realloc` satisfies the guarantees, since `ReallocFunc::call` has the same.
unsafe impl Allocator for Vmalloc {
    #[inline]
    unsafe fn realloc(
        ptr: Option<NonNull<u8>>,
        layout: Layout,
        old_layout: Layout,
        flags: Flags,
    ) -> Result<NonNull<[u8]>, AllocError> {
        // TODO: Support alignments larger than PAGE_SIZE.
        if layout.align() > bindings::PAGE_SIZE {
            pr_warn!("Vmalloc does not support alignments larger than PAGE_SIZE yet.\n");
            return Err(AllocError);
        }

        // SAFETY: If not `None`, `ptr` is guaranteed to point to valid memory, which was previously
        // allocated with this `Allocator`.
        unsafe { ReallocFunc::VREALLOC.call(ptr, layout, old_layout, flags) }
    }
}

#[global_allocator]
static ALLOCATOR: Kmalloc = Kmalloc;

// See <https://github.com/rust-lang/rust/pull/86844>.
#[no_mangle]
static __rust_no_alloc_shim_is_unstable: u8 = 0;
