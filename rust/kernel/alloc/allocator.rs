// SPDX-License-Identifier: GPL-2.0

//! Allocator support.
//!
//! Documentation for the kernel's memory allocators can found in the "Memory Allocation Guide"
//! linked below. For instance, this includes the concept of "get free page" (GFP) flags and the
//! typical application of the different kernel allocators.
//!
//! Reference: <https://docs.kernel.org/core-api/memory-allocation.html>

use super::Flags;
use core::alloc::Layout;
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

/// The kvmalloc kernel allocator.
///
/// `KVmalloc` attempts to allocate memory with `Kmalloc` first, but falls back to `Vmalloc` upon
/// failure. This allocator is typically used when the size for the requested allocation is not
/// known and may exceed the capabilities of `Kmalloc`.
///
/// For more details see [self].
pub struct KVmalloc;

/// Returns a proper size to alloc a new object aligned to `new_layout`'s alignment.
fn aligned_size(new_layout: Layout) -> usize {
    // Customized layouts from `Layout::from_size_align()` can have size < align, so pad first.
    let layout = new_layout.pad_to_align();

    // Note that `layout.size()` (after padding) is guaranteed to be a multiple of `layout.align()`
    // which together with the slab guarantees means the `krealloc` will return a properly aligned
    // object (see comments in `kmalloc()` for more information).
    layout.size()
}

/// # Invariants
///
/// One of the following: `krealloc`, `vrealloc`, `kvrealloc`.
struct ReallocFunc(
    unsafe extern "C" fn(*const crate::ffi::c_void, usize, u32) -> *mut crate::ffi::c_void,
);

impl ReallocFunc {
    // INVARIANT: `krealloc` satisfies the type invariants.
    const KREALLOC: Self = Self(bindings::krealloc);

    // INVARIANT: `vrealloc` satisfies the type invariants.
    const VREALLOC: Self = Self(bindings::vrealloc);

    // INVARIANT: `kvrealloc` satisfies the type invariants.
    const KVREALLOC: Self = Self(bindings::kvrealloc);

    /// # Safety
    ///
    /// This method has the same safety requirements as [`Allocator::realloc`].
    ///
    /// # Guarantees
    ///
    /// This method has the same guarantees as `Allocator::realloc`. Additionally
    /// - it accepts any pointer to a valid memory allocation allocated by this function.
    /// - memory allocated by this function remains valid until it is passed to this function.
    #[inline]
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

// SAFETY: `realloc` delegates to `ReallocFunc::call`, which guarantees that
// - memory remains valid until it is explicitly freed,
// - passing a pointer to a valid memory allocation is OK,
// - `realloc` satisfies the guarantees, since `ReallocFunc::call` has the same.
unsafe impl Allocator for KVmalloc {
    #[inline]
    unsafe fn realloc(
        ptr: Option<NonNull<u8>>,
        layout: Layout,
        old_layout: Layout,
        flags: Flags,
    ) -> Result<NonNull<[u8]>, AllocError> {
        // TODO: Support alignments larger than PAGE_SIZE.
        if layout.align() > bindings::PAGE_SIZE {
            pr_warn!("KVmalloc does not support alignments larger than PAGE_SIZE yet.\n");
            return Err(AllocError);
        }

        // SAFETY: If not `None`, `ptr` is guaranteed to point to valid memory, which was previously
        // allocated with this `Allocator`.
        unsafe { ReallocFunc::KVREALLOC.call(ptr, layout, old_layout, flags) }
    }
}
