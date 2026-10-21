// SPDX-License-Identifier: GPL-2.0-only

//! Abstractions for the faux bus.
//!
//! This module provides bindings for working with faux devices in kernel modules.
//!
//! C header: [`include/linux/device/faux.h`](srctree/include/linux/device/faux.h)

use crate::{
    bindings,
    device,
    prelude::*,
    types::Opaque, //
};
use core::{
    marker::PhantomData,
    ptr::{
        null,
        null_mut,
        NonNull, //
    },
};

/// A faux device.
///
/// A faux device is a virtual device backed by the faux bus, primarily used for scenarios where a
/// real hardware device is not available or for testing.
///
/// # Invariants
///
/// The underlying `struct faux_device` is valid.
#[repr(transparent)]
pub struct Device<Ctx: device::DeviceContext = device::Normal>(
    Opaque<bindings::faux_device>,
    PhantomData<Ctx>,
);

impl<Ctx: device::DeviceContext> Device<Ctx> {
    #[inline]
    fn as_raw(&self) -> *mut bindings::faux_device {
        self.0.get()
    }

    /// # Safety
    ///
    /// `ptr` must be a valid pointer to a `struct faux_device`.
    #[inline]
    unsafe fn from_raw<'a>(ptr: *mut bindings::faux_device) -> &'a Self {
        // SAFETY: `Device` is a transparent wrapper of `Opaque<bindings::faux_device>`.
        unsafe { &*ptr.cast() }
    }
}

impl<Ctx: device::DeviceContext> AsRef<device::Device<Ctx>> for Device<Ctx> {
    #[inline]
    fn as_ref(&self) -> &device::Device<Ctx> {
        // SAFETY: By the type invariant of `Self`, `self.as_raw()` is a pointer to a valid
        // `struct faux_device`. `dev` points to a valid `struct device`.
        unsafe { device::Device::from_raw(&raw mut (*self.as_raw()).dev) }
    }
}

// SAFETY: `faux::Device` is a transparent wrapper of `struct faux_device`.
// The offset is guaranteed to point to a valid device field inside `faux::Device`.
unsafe impl<Ctx: device::DeviceContext> device::AsBusDevice<Ctx> for Device<Ctx> {
    const OFFSET: usize = core::mem::offset_of!(bindings::faux_device, dev);
}

/// The registration of a faux device.
///
/// This type represents the registration of a [`struct faux_device`]. When an instance of this type
/// is dropped, its respective faux device will be unregistered from the system.
///
/// # Invariants
///
/// - `self.0` always holds a valid pointer to an initialized and registered [`struct faux_device`].
/// - This object is proof that the object described by this `Registration` is bound to a device.
///
/// [`struct faux_device`]: srctree/include/linux/device/faux.h
pub struct Registration(NonNull<bindings::faux_device>);

impl Registration {
    /// Create and register a new faux device with the given name.
    #[inline]
    pub fn new(name: &CStr, parent: Option<&device::Device>) -> Result<Self> {
        // SAFETY:
        // - `name` is copied by this function into its own storage
        // - `faux_ops` is safe to leave NULL according to the C API
        // - `parent` can be either NULL or a pointer to a `struct device`, and `faux_device_create`
        //   will take a reference to `parent` using `device_add` - ensuring that it remains valid
        //   for the lifetime of the faux device.
        let dev = unsafe {
            bindings::faux_device_create(
                name.as_char_ptr(),
                parent.map_or(null_mut(), |p| p.as_raw()),
                null(),
            )
        };

        // The above function will return either a valid device, or NULL on failure
        // INVARIANT: The device will remain registered until faux_device_destroy() is called, which
        // happens in our Drop implementation.
        Ok(Self(NonNull::new(dev).ok_or(ENODEV)?))
    }

    fn as_raw(&self) -> *mut bindings::faux_device {
        self.0.as_ptr()
    }
}

impl AsRef<Device<device::Bound>> for Registration {
    #[inline]
    fn as_ref(&self) -> &Device<device::Bound> {
        // SAFETY:
        // - The underlying `struct faux_device` is guaranteed by the C API to be a valid
        //   initialized `device`.
        // - `faux_match()` always returns 1, and probe runs synchronously
        //   (PROBE_FORCE_SYNCHRONOUS).
        // - `suppress_bind_attrs = true` on faux_driver prevents userspace-triggered unbind via
        //   sysfs.
        // - `mem::forget(Registration)` is not a problem; if the `Registration` is leaked, the faux
        //   device stays bound forever.
        unsafe { Device::from_raw(self.as_raw()) }
    }
}

impl Drop for Registration {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: `self.0` is a valid registered faux_device via our type invariants.
        unsafe { bindings::faux_device_destroy(self.as_raw()) }
    }
}

// SAFETY: The faux device API is thread-safe as guaranteed by the device core, as long as
// faux_device_destroy() is guaranteed to only be called once - which is guaranteed by our type not
// having Copy/Clone.
unsafe impl Send for Registration {}

// SAFETY: The faux device API is thread-safe as guaranteed by the device core, as long as
// faux_device_destroy() is guaranteed to only be called once - which is guaranteed by our type not
// having Copy/Clone.
unsafe impl Sync for Registration {}
