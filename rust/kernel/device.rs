// SPDX-License-Identifier: GPL-2.0

//! Generic devices that are part of the kernel's driver model.
//!
//! C header: [`include/linux/device.h`](srctree/include/linux/device.h)

use crate::{
    bindings,
    types::{ARef, Opaque},
};
use core::ptr;

/// A reference-counted device.
///
/// This structure represents the Rust abstraction for a C `struct device`. This implementation
/// abstracts the usage of an already existing C `struct device` within Rust code that we get
/// passed from the C side.
///
/// An instance of this abstraction can be obtained temporarily or permanent.
///
/// A temporary one is bound to the lifetime of the C `struct device` pointer used for creation.
/// A permanent instance is always reference-counted and hence not restricted by any lifetime
/// boundaries.
///
/// For subsystems it is recommended to create a permanent instance to wrap into a subsystem
/// specific device structure (e.g. `pci::Device`). This is useful for passing it to drivers in
/// `T::probe()`, such that a driver can store the `ARef<Device>` (equivalent to storing a
/// `struct device` pointer in a C driver) for arbitrary purposes, e.g. allocating DMA coherent
/// memory.
///
/// # Invariants
///
/// A `Device` instance represents a valid `struct device` created by the C portion of the kernel.
///
/// Instances of this type are always reference-counted, that is, a call to `get_device` ensures
/// that the allocation remains valid at least until the matching call to `put_device`.
///
/// `bindings::device::release` is valid to be called from any thread, hence `ARef<Device>` can be
/// dropped from any thread.
#[repr(transparent)]
pub struct Device(Opaque<bindings::device>);

impl Device {
    /// Creates a new reference-counted abstraction instance of an existing `struct device` pointer.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `ptr` is valid, non-null, and has a non-zero reference count,
    /// i.e. it must be ensured that the reference count of the C `struct device` `ptr` points to
    /// can't drop to zero, for the duration of this function call.
    ///
    /// It must also be ensured that `bindings::device::release` can be called from any thread.
    /// While not officially documented, this should be the case for any `struct device`.
    pub unsafe fn from_raw(ptr: *mut bindings::device) -> ARef<Self> {
        // SAFETY: By the safety requirements, ptr is valid.
        // Initially increase the reference count by one to compensate for the final decrement once
        // this newly created `ARef<Device>` instance is dropped.
        unsafe { bindings::get_device(ptr) };

        // CAST: `Self` is a `repr(transparent)` wrapper around `bindings::device`.
        let ptr = ptr.cast::<Self>();

        // SAFETY: `ptr` is valid by the safety requirements of this function. By the above call to
        // `bindings::get_device` we also own a reference to the underlying `struct device`.
        unsafe { ARef::from_raw(ptr::NonNull::new_unchecked(ptr)) }
    }

    /// Obtain the raw `struct device *`.
    pub(crate) fn as_raw(&self) -> *mut bindings::device {
        self.0.get()
    }

    /// Convert a raw C `struct device` pointer to a `&'a Device`.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `ptr` is valid, non-null, and has a non-zero reference count,
    /// i.e. it must be ensured that the reference count of the C `struct device` `ptr` points to
    /// can't drop to zero, for the duration of this function call and the entire duration when the
    /// returned reference exists.
    pub unsafe fn as_ref<'a>(ptr: *mut bindings::device) -> &'a Self {
        // SAFETY: Guaranteed by the safety requirements of the function.
        unsafe { &*ptr.cast() }
    }
}

// SAFETY: Instances of `Device` are always reference-counted.
unsafe impl crate::types::AlwaysRefCounted for Device {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference guarantees that the refcount is non-zero.
        unsafe { bindings::get_device(self.as_raw()) };
    }

    unsafe fn dec_ref(obj: ptr::NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is non-zero.
        unsafe { bindings::put_device(obj.cast().as_ptr()) }
    }
}

// SAFETY: As by the type invariant `Device` can be sent to any thread.
unsafe impl Send for Device {}

// SAFETY: `Device` can be shared among threads because all immutable methods are protected by the
// synchronization in `struct device`.
unsafe impl Sync for Device {}
