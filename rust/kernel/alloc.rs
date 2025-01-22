// SPDX-License-Identifier: GPL-2.0

//! Implementation of the kernel's memory allocation infrastructure.

#[cfg(not(any(test, testlib)))]
pub mod allocator;
pub mod kbox;
pub mod kvec;
pub mod layout;

#[cfg(any(test, testlib))]
pub mod allocator_test;

#[cfg(any(test, testlib))]
pub use self::allocator_test as allocator;

pub use self::kbox::Box;
pub use self::kbox::KBox;
pub use self::kbox::KVBox;
pub use self::kbox::VBox;

pub use self::kvec::IntoIter;
pub use self::kvec::KVVec;
pub use self::kvec::KVec;
pub use self::kvec::VVec;
pub use self::kvec::Vec;

/// Indicates an allocation error.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct AllocError;
use core::{alloc::Layout, ptr::NonNull};

/// Flags to be used when allocating memory.
///
/// They can be combined with the operators `|`, `&`, and `!`.
///
/// Values can be used from the [`flags`] module.
#[derive(Clone, Copy, PartialEq)]
pub struct Flags(u32);

impl Flags {
    /// Get the raw representation of this flag.
    pub(crate) fn as_raw(self) -> u32 {
        self.0
    }

    /// Check whether `flags` is contained in `self`.
    pub fn contains(self, flags: Flags) -> bool {
        (self & flags) == flags
    }
}

impl core::ops::BitOr for Flags {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

impl core::ops::BitAnd for Flags {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self::Output {
        Self(self.0 & rhs.0)
    }
}

impl core::ops::Not for Flags {
    type Output = Self;
    fn not(self) -> Self::Output {
        Self(!self.0)
    }
}

/// Allocation flags.
///
/// These are meant to be used in functions that can allocate memory.
pub mod flags {
    use super::Flags;

    /// Zeroes out the allocated memory.
    ///
    /// This is normally or'd with other flags.
    pub const __GFP_ZERO: Flags = Flags(bindings::__GFP_ZERO);

    /// Allow the allocation to be in high memory.
    ///
    /// Allocations in high memory may not be mapped into the kernel's address space, so this can't
    /// be used with `kmalloc` and other similar methods.
    ///
    /// This is normally or'd with other flags.
    pub const __GFP_HIGHMEM: Flags = Flags(bindings::__GFP_HIGHMEM);

    /// Users can not sleep and need the allocation to succeed.
    ///
    /// A lower watermark is applied to allow access to "atomic reserves". The current
    /// implementation doesn't support NMI and few other strict non-preemptive contexts (e.g.
    /// raw_spin_lock). The same applies to [`GFP_NOWAIT`].
    pub const GFP_ATOMIC: Flags = Flags(bindings::GFP_ATOMIC);

    /// Typical for kernel-internal allocations. The caller requires ZONE_NORMAL or a lower zone
    /// for direct access but can direct reclaim.
    pub const GFP_KERNEL: Flags = Flags(bindings::GFP_KERNEL);

    /// The same as [`GFP_KERNEL`], except the allocation is accounted to kmemcg.
    pub const GFP_KERNEL_ACCOUNT: Flags = Flags(bindings::GFP_KERNEL_ACCOUNT);

    /// For kernel allocations that should not stall for direct reclaim, start physical IO or
    /// use any filesystem callback.  It is very likely to fail to allocate memory, even for very
    /// small allocations.
    pub const GFP_NOWAIT: Flags = Flags(bindings::GFP_NOWAIT);

    /// Suppresses allocation failure reports.
    ///
    /// This is normally or'd with other flags.
    pub const __GFP_NOWARN: Flags = Flags(bindings::__GFP_NOWARN);
}

/// The kernel's [`Allocator`] trait.
///
/// An implementation of [`Allocator`] can allocate, re-allocate and free memory buffers described
/// via [`Layout`].
///
/// [`Allocator`] is designed to be implemented as a ZST; [`Allocator`] functions do not operate on
/// an object instance.
///
/// In order to be able to support `#[derive(CoercePointee)]` later on, we need to avoid a design
/// that requires an `Allocator` to be instantiated, hence its functions must not contain any kind
/// of `self` parameter.
///
/// # Safety
///
/// - A memory allocation returned from an allocator must remain valid until it is explicitly freed.
///
/// - Any pointer to a valid memory allocation must be valid to be passed to any other [`Allocator`]
///   function of the same type.
///
/// - Implementers must ensure that all trait functions abide by the guarantees documented in the
///   `# Guarantees` sections.
pub unsafe trait Allocator {
    /// Allocate memory based on `layout` and `flags`.
    ///
    /// On success, returns a buffer represented as `NonNull<[u8]>` that satisfies the layout
    /// constraints (i.e. minimum size and alignment as specified by `layout`).
    ///
    /// This function is equivalent to `realloc` when called with `None`.
    ///
    /// # Guarantees
    ///
    /// When the return value is `Ok(ptr)`, then `ptr` is
    /// - valid for reads and writes for `layout.size()` bytes, until it is passed to
    ///   [`Allocator::free`] or [`Allocator::realloc`],
    /// - aligned to `layout.align()`,
    ///
    /// Additionally, `Flags` are honored as documented in
    /// <https://docs.kernel.org/core-api/mm-api.html#mm-api-gfp-flags>.
    fn alloc(layout: Layout, flags: Flags) -> Result<NonNull<[u8]>, AllocError> {
        // SAFETY: Passing `None` to `realloc` is valid by its safety requirements and asks for a
        // new memory allocation.
        unsafe { Self::realloc(None, layout, Layout::new::<()>(), flags) }
    }

    /// Re-allocate an existing memory allocation to satisfy the requested `layout`.
    ///
    /// If the requested size is zero, `realloc` behaves equivalent to `free`.
    ///
    /// If the requested size is larger than the size of the existing allocation, a successful call
    /// to `realloc` guarantees that the new or grown buffer has at least `Layout::size` bytes, but
    /// may also be larger.
    ///
    /// If the requested size is smaller than the size of the existing allocation, `realloc` may or
    /// may not shrink the buffer; this is implementation specific to the allocator.
    ///
    /// On allocation failure, the existing buffer, if any, remains valid.
    ///
    /// The buffer is represented as `NonNull<[u8]>`.
    ///
    /// # Safety
    ///
    /// - If `ptr == Some(p)`, then `p` must point to an existing and valid memory allocation
    ///   created by this [`Allocator`]; if `old_layout` is zero-sized `p` does not need to be a
    ///   pointer returned by this [`Allocator`].
    /// - `ptr` is allowed to be `None`; in this case a new memory allocation is created and
    ///   `old_layout` is ignored.
    /// - `old_layout` must match the `Layout` the allocation has been created with.
    ///
    /// # Guarantees
    ///
    /// This function has the same guarantees as [`Allocator::alloc`]. When `ptr == Some(p)`, then
    /// it additionally guarantees that:
    /// - the contents of the memory pointed to by `p` are preserved up to the lesser of the new
    ///   and old size, i.e. `ret_ptr[0..min(layout.size(), old_layout.size())] ==
    ///   p[0..min(layout.size(), old_layout.size())]`.
    /// - when the return value is `Err(AllocError)`, then `ptr` is still valid.
    unsafe fn realloc(
        ptr: Option<NonNull<u8>>,
        layout: Layout,
        old_layout: Layout,
        flags: Flags,
    ) -> Result<NonNull<[u8]>, AllocError>;

    /// Free an existing memory allocation.
    ///
    /// # Safety
    ///
    /// - `ptr` must point to an existing and valid memory allocation created by this [`Allocator`];
    ///   if `old_layout` is zero-sized `p` does not need to be a pointer returned by this
    ///   [`Allocator`].
    /// - `layout` must match the `Layout` the allocation has been created with.
    /// - The memory allocation at `ptr` must never again be read from or written to.
    unsafe fn free(ptr: NonNull<u8>, layout: Layout) {
        // SAFETY: The caller guarantees that `ptr` points at a valid allocation created by this
        // allocator. We are passing a `Layout` with the smallest possible alignment, so it is
        // smaller than or equal to the alignment previously used with this allocation.
        let _ = unsafe { Self::realloc(Some(ptr), Layout::new::<()>(), layout, Flags(0)) };
    }
}

/// Returns a properly aligned dangling pointer from the given `layout`.
pub(crate) fn dangling_from_layout(layout: Layout) -> NonNull<u8> {
    let ptr = layout.align() as *mut u8;

    // SAFETY: `layout.align()` (and hence `ptr`) is guaranteed to be non-zero.
    unsafe { NonNull::new_unchecked(ptr) }
}
