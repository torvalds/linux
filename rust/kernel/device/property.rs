// SPDX-License-Identifier: GPL-2.0

//! Unified device property interface.
//!
//! C header: [`include/linux/property.h`](srctree/include/linux/property.h)

use core::ptr;

use crate::{
    bindings,
    str::CStr,
    types::{ARef, Opaque},
};

/// A reference-counted fwnode_handle.
///
/// This structure represents the Rust abstraction for a
/// C `struct fwnode_handle`. This implementation abstracts the usage of an
/// already existing C `struct fwnode_handle` within Rust code that we get
/// passed from the C side.
///
/// # Invariants
///
/// A `FwNode` instance represents a valid `struct fwnode_handle` created by the
/// C portion of the kernel.
///
/// Instances of this type are always reference-counted, that is, a call to
/// `fwnode_handle_get` ensures that the allocation remains valid at least until
/// the matching call to `fwnode_handle_put`.
#[repr(transparent)]
pub struct FwNode(Opaque<bindings::fwnode_handle>);

impl FwNode {
    /// # Safety
    ///
    /// Callers must ensure that:
    /// - The reference count was incremented at least once.
    /// - They relinquish that increment. That is, if there is only one
    ///   increment, callers must not use the underlying object anymore -- it is
    ///   only safe to do so via the newly created `ARef<FwNode>`.
    #[expect(dead_code)]
    unsafe fn from_raw(raw: *mut bindings::fwnode_handle) -> ARef<Self> {
        // SAFETY: As per the safety requirements of this function:
        // - `NonNull::new_unchecked`:
        //   - `raw` is not null.
        // - `ARef::from_raw`:
        //   - `raw` has an incremented refcount.
        //   - that increment is relinquished, i.e. it won't be decremented
        //     elsewhere.
        // CAST: It is safe to cast from a `*mut fwnode_handle` to
        // `*mut FwNode`, because `FwNode` is  defined as a
        // `#[repr(transparent)]` wrapper around `fwnode_handle`.
        unsafe { ARef::from_raw(ptr::NonNull::new_unchecked(raw.cast())) }
    }

    /// Obtain the raw `struct fwnode_handle *`.
    pub(crate) fn as_raw(&self) -> *mut bindings::fwnode_handle {
        self.0.get()
    }

    /// Checks if property is present or not.
    pub fn property_present(&self, name: &CStr) -> bool {
        // SAFETY: By the invariant of `CStr`, `name` is null-terminated.
        unsafe { bindings::fwnode_property_present(self.as_raw().cast_const(), name.as_char_ptr()) }
    }
}

// SAFETY: Instances of `FwNode` are always reference-counted.
unsafe impl crate::types::AlwaysRefCounted for FwNode {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference guarantees that the
        // refcount is non-zero.
        unsafe { bindings::fwnode_handle_get(self.as_raw()) };
    }

    unsafe fn dec_ref(obj: ptr::NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is
        // non-zero.
        unsafe { bindings::fwnode_handle_put(obj.cast().as_ptr()) }
    }
}
