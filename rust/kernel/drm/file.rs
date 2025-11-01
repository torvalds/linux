// SPDX-License-Identifier: GPL-2.0 OR MIT

//! DRM File objects.
//!
//! C header: [`include/drm/drm_file.h`](srctree/include/drm/drm_file.h)

use crate::{bindings, drm, error::Result, prelude::*, types::Opaque};
use core::marker::PhantomData;
use core::pin::Pin;

/// Trait that must be implemented by DRM drivers to represent a DRM File (a client instance).
pub trait DriverFile {
    /// The parent `Driver` implementation for this `DriverFile`.
    type Driver: drm::Driver;

    /// Open a new file (called when a client opens the DRM device).
    fn open(device: &drm::Device<Self::Driver>) -> Result<Pin<KBox<Self>>>;
}

/// An open DRM File.
///
/// # Invariants
///
/// `self.0` is a valid instance of a `struct drm_file`.
#[repr(transparent)]
pub struct File<T: DriverFile>(Opaque<bindings::drm_file>, PhantomData<T>);

impl<T: DriverFile> File<T> {
    #[doc(hidden)]
    /// Not intended to be called externally, except via declare_drm_ioctls!()
    ///
    /// # Safety
    ///
    /// `raw_file` must be a valid pointer to an open `struct drm_file`, opened through `T::open`.
    pub unsafe fn from_raw<'a>(ptr: *mut bindings::drm_file) -> &'a File<T> {
        // SAFETY: `raw_file` is valid by the safety requirements of this function.
        unsafe { &*ptr.cast() }
    }

    pub(super) fn as_raw(&self) -> *mut bindings::drm_file {
        self.0.get()
    }

    fn driver_priv(&self) -> *mut T {
        // SAFETY: By the type invariants of `Self`, `self.as_raw()` is always valid.
        unsafe { (*self.as_raw()).driver_priv }.cast()
    }

    /// Return a pinned reference to the driver file structure.
    pub fn inner(&self) -> Pin<&T> {
        // SAFETY: By the type invariant the pointer `self.as_raw()` points to a valid and opened
        // `struct drm_file`, hence `driver_priv` has been properly initialized by `open_callback`.
        unsafe { Pin::new_unchecked(&*(self.driver_priv())) }
    }

    /// The open callback of a `struct drm_file`.
    pub(crate) extern "C" fn open_callback(
        raw_dev: *mut bindings::drm_device,
        raw_file: *mut bindings::drm_file,
    ) -> core::ffi::c_int {
        // SAFETY: A callback from `struct drm_driver::open` guarantees that
        // - `raw_dev` is valid pointer to a `struct drm_device`,
        // - the corresponding `struct drm_device` has been registered.
        let drm = unsafe { drm::Device::from_raw(raw_dev) };

        // SAFETY: `raw_file` is a valid pointer to a `struct drm_file`.
        let file = unsafe { File::<T>::from_raw(raw_file) };

        let inner = match T::open(drm) {
            Err(e) => {
                return e.to_errno();
            }
            Ok(i) => i,
        };

        // SAFETY: This pointer is treated as pinned, and the Drop guarantee is upheld in
        // `postclose_callback()`.
        let driver_priv = KBox::into_raw(unsafe { Pin::into_inner_unchecked(inner) });

        // SAFETY: By the type invariants of `Self`, `self.as_raw()` is always valid.
        unsafe { (*file.as_raw()).driver_priv = driver_priv.cast() };

        0
    }

    /// The postclose callback of a `struct drm_file`.
    pub(crate) extern "C" fn postclose_callback(
        _raw_dev: *mut bindings::drm_device,
        raw_file: *mut bindings::drm_file,
    ) {
        // SAFETY: This reference won't escape this function
        let file = unsafe { File::<T>::from_raw(raw_file) };

        // SAFETY: `file.driver_priv` has been created in `open_callback` through `KBox::into_raw`.
        let _ = unsafe { KBox::from_raw(file.driver_priv()) };
    }
}

impl<T: DriverFile> super::private::Sealed for File<T> {}
