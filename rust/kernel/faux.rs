// SPDX-License-Identifier: GPL-2.0-only

//! Abstractions for the faux bus.
//!
//! This module provides bindings for working with faux devices in kernel modules.
//!
//! C header: [`include/linux/device/faux.h`]

use crate::{bindings, device, error::code::*, prelude::*};
use core::ptr::{addr_of_mut, null, null_mut, NonNull};

/// The registration of a faux device.
///
/// This type represents the registration of a [`struct faux_device`]. When an instance of this type
/// is dropped, its respective faux device will be unregistered from the system.
///
/// # Invariants
///
/// `self.0` always holds a valid pointer to an initialized and registered [`struct faux_device`].
///
/// [`struct faux_device`]: srctree/include/linux/device/faux.h
#[repr(transparent)]
pub struct Registration(NonNull<bindings::faux_device>);

impl Registration {
    /// Create and register a new faux device with the given name.
    pub fn new(name: &CStr) -> Result<Self> {
        // SAFETY:
        // - `name` is copied by this function into its own storage
        // - `faux_ops` is safe to leave NULL according to the C API
        let dev = unsafe { bindings::faux_device_create(name.as_char_ptr(), null_mut(), null()) };

        // The above function will return either a valid device, or NULL on failure
        // INVARIANT: The device will remain registered until faux_device_destroy() is called, which
        // happens in our Drop implementation.
        Ok(Self(NonNull::new(dev).ok_or(ENODEV)?))
    }

    fn as_raw(&self) -> *mut bindings::faux_device {
        self.0.as_ptr()
    }
}

impl AsRef<device::Device> for Registration {
    fn as_ref(&self) -> &device::Device {
        // SAFETY: The underlying `device` in `faux_device` is guaranteed by the C API to be
        // a valid initialized `device`.
        unsafe { device::Device::as_ref(addr_of_mut!((*self.as_raw()).dev)) }
    }
}

impl Drop for Registration {
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
