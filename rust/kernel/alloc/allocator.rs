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

use crate::alloc::{AllocError, Allocator, NumaNode};
use crate::bindings;
use crate::page;

const ARCH_KMALLOC_MINALIGN: usize = bindings::ARCH_KMALLOC_MINALIGN;

mod iter;
pub use self::iter::VmallocPageIter;

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

/// # Invariants
///
/// One of the following: `krealloc_node_align`, `vrealloc_node_align`, `kvrealloc_node_align`.
struct ReallocFunc(
    unsafe extern "C" fn(
        *const crate::ffi::c_void,
        usize,
        crate::ffi::c_ulong,
        u32,
        crate::ffi::c_int,
    ) -> *mut crate::ffi::c_void,
);

impl ReallocFunc {
    // INVARIANT: `krealloc_node_align` satisfies the type invariants.
    const KREALLOC: Self = Self(bindings::krealloc_node_align);

    // INVARIANT: `vrealloc_node_align` satisfies the type invariants.
    const VREALLOC: Self = Self(bindings::vrealloc_node_align);

    // INVARIANT: `kvrealloc_node_align` satisfies the type invariants.
    const KVREALLOC: Self = Self(bindings::kvrealloc_node_align);

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
        nid: NumaNode,
    ) -> Result<NonNull<[u8]>, AllocError> {
        let size = layout.size();
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
            self.0(ptr.cast(), size, layout.align(), flags.0, nid.0).cast()
        };

        let ptr = if size == 0 {
            crate::alloc::dangling_from_layout(layout)
        } else {
            NonNull::new(raw_ptr).ok_or(AllocError)?
        };

        Ok(NonNull::slice_from_raw_parts(ptr, size))
    }
}

impl Kmalloc {
    /// Returns a [`Layout`] that makes [`Kmalloc`] fulfill the requested size and alignment of
    /// `layout`.
    pub fn aligned_layout(layout: Layout) -> Layout {
        // Note that `layout.size()` (after padding) is guaranteed to be a multiple of
        // `layout.align()` which together with the slab guarantees means that `Kmalloc` will return
        // a properly aligned object (see comments in `kmalloc()` for more information).
        layout.pad_to_align()
    }
}

// SAFETY: `realloc` delegates to `ReallocFunc::call`, which guarantees that
// - memory remains valid until it is explicitly freed,
// - passing a pointer to a valid memory allocation is OK,
// - `realloc` satisfies the guarantees, since `ReallocFunc::call` has the same.
unsafe impl Allocator for Kmalloc {
    const MIN_ALIGN: usize = ARCH_KMALLOC_MINALIGN;

    #[inline]
    unsafe fn realloc(
        ptr: Option<NonNull<u8>>,
        layout: Layout,
        old_layout: Layout,
        flags: Flags,
        nid: NumaNode,
    ) -> Result<NonNull<[u8]>, AllocError> {
        let layout = Kmalloc::aligned_layout(layout);

        // SAFETY: `ReallocFunc::call` has the same safety requirements as `Allocator::realloc`.
        unsafe { ReallocFunc::KREALLOC.call(ptr, layout, old_layout, flags, nid) }
    }
}

impl Vmalloc {
    /// Convert a pointer to a [`Vmalloc`] allocation to a [`page::BorrowedPage`].
    ///
    /// # Examples
    ///
    /// ```
    /// # use core::ptr::{NonNull, from_mut};
    /// # use kernel::{page, prelude::*};
    /// use kernel::alloc::allocator::Vmalloc;
    ///
    /// let mut vbox = VBox::<[u8; page::PAGE_SIZE]>::new_uninit(GFP_KERNEL)?;
    ///
    /// {
    ///     // SAFETY: By the type invariant of `Box` the inner pointer of `vbox` is non-null.
    ///     let ptr = unsafe { NonNull::new_unchecked(from_mut(&mut *vbox)) };
    ///
    ///     // SAFETY:
    ///     // `ptr` is a valid pointer to a `Vmalloc` allocation.
    ///     // `ptr` is valid for the entire lifetime of `page`.
    ///     let page = unsafe { Vmalloc::to_page(ptr.cast()) };
    ///
    ///     // SAFETY: There is no concurrent read or write to the same page.
    ///     unsafe { page.fill_zero_raw(0, page::PAGE_SIZE)? };
    /// }
    /// # Ok::<(), Error>(())
    /// ```
    ///
    /// # Safety
    ///
    /// - `ptr` must be a valid pointer to a [`Vmalloc`] allocation.
    /// - `ptr` must remain valid for the entire duration of `'a`.
    pub unsafe fn to_page<'a>(ptr: NonNull<u8>) -> page::BorrowedPage<'a> {
        // SAFETY: `ptr` is a valid pointer to `Vmalloc` memory.
        let page = unsafe { bindings::vmalloc_to_page(ptr.as_ptr().cast()) };

        // SAFETY: `vmalloc_to_page` returns a valid pointer to a `struct page` for a valid pointer
        // to `Vmalloc` memory.
        let page = unsafe { NonNull::new_unchecked(page) };

        // SAFETY:
        // - `page` is a valid pointer to a `struct page`, given that by the safety requirements of
        //   this function `ptr` is a valid pointer to a `Vmalloc` allocation.
        // - By the safety requirements of this function `ptr` is valid for the entire lifetime of
        //   `'a`.
        unsafe { page::BorrowedPage::from_raw(page) }
    }
}

// SAFETY: `realloc` delegates to `ReallocFunc::call`, which guarantees that
// - memory remains valid until it is explicitly freed,
// - passing a pointer to a valid memory allocation is OK,
// - `realloc` satisfies the guarantees, since `ReallocFunc::call` has the same.
unsafe impl Allocator for Vmalloc {
    const MIN_ALIGN: usize = kernel::page::PAGE_SIZE;

    #[inline]
    unsafe fn realloc(
        ptr: Option<NonNull<u8>>,
        layout: Layout,
        old_layout: Layout,
        flags: Flags,
        nid: NumaNode,
    ) -> Result<NonNull<[u8]>, AllocError> {
        // SAFETY: If not `None`, `ptr` is guaranteed to point to valid memory, which was previously
        // allocated with this `Allocator`.
        unsafe { ReallocFunc::VREALLOC.call(ptr, layout, old_layout, flags, nid) }
    }
}

// SAFETY: `realloc` delegates to `ReallocFunc::call`, which guarantees that
// - memory remains valid until it is explicitly freed,
// - passing a pointer to a valid memory allocation is OK,
// - `realloc` satisfies the guarantees, since `ReallocFunc::call` has the same.
unsafe impl Allocator for KVmalloc {
    const MIN_ALIGN: usize = ARCH_KMALLOC_MINALIGN;

    #[inline]
    unsafe fn realloc(
        ptr: Option<NonNull<u8>>,
        layout: Layout,
        old_layout: Layout,
        flags: Flags,
        nid: NumaNode,
    ) -> Result<NonNull<[u8]>, AllocError> {
        // `KVmalloc` may use the `Kmalloc` backend, hence we have to enforce a `Kmalloc`
        // compatible layout.
        let layout = Kmalloc::aligned_layout(layout);

        // SAFETY: If not `None`, `ptr` is guaranteed to point to valid memory, which was previously
        // allocated with this `Allocator`.
        unsafe { ReallocFunc::KVREALLOC.call(ptr, layout, old_layout, flags, nid) }
    }
}

#[macros::kunit_tests(rust_allocator)]
mod tests {
    use super::*;
    use core::mem::MaybeUninit;
    use kernel::prelude::*;

    #[test]
    fn test_alignment() -> Result {
        const TEST_SIZE: usize = 1024;
        const TEST_LARGE_ALIGN_SIZE: usize = kernel::page::PAGE_SIZE * 4;

        // These two structs are used to test allocating aligned memory.
        // they don't need to be accessed, so they're marked as dead_code.
        #[expect(dead_code)]
        #[repr(align(128))]
        struct Blob([u8; TEST_SIZE]);
        #[expect(dead_code)]
        #[repr(align(8192))]
        struct LargeAlignBlob([u8; TEST_LARGE_ALIGN_SIZE]);

        struct TestAlign<T, A: Allocator>(Box<MaybeUninit<T>, A>);
        impl<T, A: Allocator> TestAlign<T, A> {
            fn new() -> Result<Self> {
                Ok(Self(Box::<_, A>::new_uninit(GFP_KERNEL)?))
            }

            fn is_aligned_to(&self, align: usize) -> bool {
                assert!(align.is_power_of_two());

                let addr = self.0.as_ptr() as usize;
                addr & (align - 1) == 0
            }
        }

        let ta = TestAlign::<Blob, Kmalloc>::new()?;
        assert!(ta.is_aligned_to(128));

        let ta = TestAlign::<LargeAlignBlob, Kmalloc>::new()?;
        assert!(ta.is_aligned_to(8192));

        let ta = TestAlign::<Blob, Vmalloc>::new()?;
        assert!(ta.is_aligned_to(128));

        let ta = TestAlign::<LargeAlignBlob, Vmalloc>::new()?;
        assert!(ta.is_aligned_to(8192));

        let ta = TestAlign::<Blob, KVmalloc>::new()?;
        assert!(ta.is_aligned_to(128));

        let ta = TestAlign::<LargeAlignBlob, KVmalloc>::new()?;
        assert!(ta.is_aligned_to(8192));

        Ok(())
    }
}
