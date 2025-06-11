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

    /// Returns an object that implements [`Display`](core::fmt::Display) for
    /// printing the name of a node.
    ///
    /// This is an alternative to the default `Display` implementation, which
    /// prints the full path.
    pub fn display_name(&self) -> impl core::fmt::Display + '_ {
        struct FwNodeDisplayName<'a>(&'a FwNode);

        impl core::fmt::Display for FwNodeDisplayName<'_> {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
                // SAFETY: `self` is valid by its type invariant.
                let name = unsafe { bindings::fwnode_get_name(self.0.as_raw()) };
                if name.is_null() {
                    return Ok(());
                }
                // SAFETY:
                // - `fwnode_get_name` returns null or a valid C string.
                // - `name` was checked to be non-null.
                let name = unsafe { CStr::from_char_ptr(name) };
                write!(f, "{name}")
            }
        }

        FwNodeDisplayName(self)
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

enum Node<'a> {
    Borrowed(&'a FwNode),
    Owned(ARef<FwNode>),
}

impl core::fmt::Display for FwNode {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        // The logic here is the same as the one in lib/vsprintf.c
        // (fwnode_full_name_string).

        // SAFETY: `self.as_raw()` is valid by its type invariant.
        let num_parents = unsafe { bindings::fwnode_count_parents(self.as_raw()) };

        for depth in (0..=num_parents).rev() {
            let fwnode = if depth == 0 {
                Node::Borrowed(self)
            } else {
                // SAFETY: `self.as_raw()` is valid.
                let ptr = unsafe { bindings::fwnode_get_nth_parent(self.as_raw(), depth) };
                // SAFETY:
                // - The depth passed to `fwnode_get_nth_parent` is
                //   within the valid range, so the returned pointer is
                //   not null.
                // - The reference count was incremented by
                //   `fwnode_get_nth_parent`.
                // - That increment is relinquished to
                //   `FwNode::from_raw`.
                Node::Owned(unsafe { FwNode::from_raw(ptr) })
            };
            // Take a reference to the owned or borrowed `FwNode`.
            let fwnode: &FwNode = match &fwnode {
                Node::Borrowed(f) => f,
                Node::Owned(f) => f,
            };

            // SAFETY: `fwnode` is valid by its type invariant.
            let prefix = unsafe { bindings::fwnode_get_name_prefix(fwnode.as_raw()) };
            if !prefix.is_null() {
                // SAFETY: `fwnode_get_name_prefix` returns null or a
                // valid C string.
                let prefix = unsafe { CStr::from_char_ptr(prefix) };
                write!(f, "{prefix}")?;
            }
            write!(f, "{}", fwnode.display_name())?;
        }

        Ok(())
    }
}
