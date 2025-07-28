// SPDX-License-Identifier: GPL-2.0 OR MIT

//! DRM driver core.
//!
//! C header: [`include/linux/drm/drm_drv.h`](srctree/include/linux/drm/drm_drv.h)

use crate::{
    bindings, device,
    devres::Devres,
    drm,
    error::{to_result, Result},
    prelude::*,
    types::ARef,
};
use macros::vtable;

/// Driver use the GEM memory manager. This should be set for all modern drivers.
pub(crate) const FEAT_GEM: u32 = bindings::drm_driver_feature_DRIVER_GEM;

/// Information data for a DRM Driver.
pub struct DriverInfo {
    /// Driver major version.
    pub major: i32,
    /// Driver minor version.
    pub minor: i32,
    /// Driver patchlevel version.
    pub patchlevel: i32,
    /// Driver name.
    pub name: &'static CStr,
    /// Driver description.
    pub desc: &'static CStr,
}

/// Internal memory management operation set, normally created by memory managers (e.g. GEM).
pub struct AllocOps {
    pub(crate) gem_create_object: Option<
        unsafe extern "C" fn(
            dev: *mut bindings::drm_device,
            size: usize,
        ) -> *mut bindings::drm_gem_object,
    >,
    pub(crate) prime_handle_to_fd: Option<
        unsafe extern "C" fn(
            dev: *mut bindings::drm_device,
            file_priv: *mut bindings::drm_file,
            handle: u32,
            flags: u32,
            prime_fd: *mut core::ffi::c_int,
        ) -> core::ffi::c_int,
    >,
    pub(crate) prime_fd_to_handle: Option<
        unsafe extern "C" fn(
            dev: *mut bindings::drm_device,
            file_priv: *mut bindings::drm_file,
            prime_fd: core::ffi::c_int,
            handle: *mut u32,
        ) -> core::ffi::c_int,
    >,
    pub(crate) gem_prime_import: Option<
        unsafe extern "C" fn(
            dev: *mut bindings::drm_device,
            dma_buf: *mut bindings::dma_buf,
        ) -> *mut bindings::drm_gem_object,
    >,
    pub(crate) gem_prime_import_sg_table: Option<
        unsafe extern "C" fn(
            dev: *mut bindings::drm_device,
            attach: *mut bindings::dma_buf_attachment,
            sgt: *mut bindings::sg_table,
        ) -> *mut bindings::drm_gem_object,
    >,
    pub(crate) dumb_create: Option<
        unsafe extern "C" fn(
            file_priv: *mut bindings::drm_file,
            dev: *mut bindings::drm_device,
            args: *mut bindings::drm_mode_create_dumb,
        ) -> core::ffi::c_int,
    >,
    pub(crate) dumb_map_offset: Option<
        unsafe extern "C" fn(
            file_priv: *mut bindings::drm_file,
            dev: *mut bindings::drm_device,
            handle: u32,
            offset: *mut u64,
        ) -> core::ffi::c_int,
    >,
}

/// Trait for memory manager implementations. Implemented internally.
pub trait AllocImpl: super::private::Sealed + drm::gem::IntoGEMObject {
    /// The C callback operations for this memory manager.
    const ALLOC_OPS: AllocOps;
}

/// The DRM `Driver` trait.
///
/// This trait must be implemented by drivers in order to create a `struct drm_device` and `struct
/// drm_driver` to be registered in the DRM subsystem.
#[vtable]
pub trait Driver {
    /// Context data associated with the DRM driver
    type Data: Sync + Send;

    /// The type used to manage memory for this driver.
    type Object: AllocImpl;

    /// The type used to represent a DRM File (client)
    type File: drm::file::DriverFile;

    /// Driver metadata
    const INFO: DriverInfo;

    /// IOCTL list. See `kernel::drm::ioctl::declare_drm_ioctls!{}`.
    const IOCTLS: &'static [drm::ioctl::DrmIoctlDescriptor];
}

/// The registration type of a `drm::Device`.
///
/// Once the `Registration` structure is dropped, the device is unregistered.
pub struct Registration<T: Driver>(ARef<drm::Device<T>>);

impl<T: Driver> Registration<T> {
    /// Creates a new [`Registration`] and registers it.
    fn new(drm: &drm::Device<T>, flags: usize) -> Result<Self> {
        // SAFETY: `drm.as_raw()` is valid by the invariants of `drm::Device`.
        to_result(unsafe { bindings::drm_dev_register(drm.as_raw(), flags) })?;

        Ok(Self(drm.into()))
    }

    /// Same as [`Registration::new`}, but transfers ownership of the [`Registration`] to
    /// [`Devres`].
    pub fn new_foreign_owned(
        drm: &drm::Device<T>,
        dev: &device::Device<device::Bound>,
        flags: usize,
    ) -> Result {
        if drm.as_ref().as_raw() != dev.as_raw() {
            return Err(EINVAL);
        }

        let reg = Registration::<T>::new(drm, flags)?;
        Devres::new_foreign_owned(dev, reg, GFP_KERNEL)
    }

    /// Returns a reference to the `Device` instance for this registration.
    pub fn device(&self) -> &drm::Device<T> {
        &self.0
    }
}

// SAFETY: `Registration` doesn't offer any methods or access to fields when shared between
// threads, hence it's safe to share it.
unsafe impl<T: Driver> Sync for Registration<T> {}

// SAFETY: Registration with and unregistration from the DRM subsystem can happen from any thread.
unsafe impl<T: Driver> Send for Registration<T> {}

impl<T: Driver> Drop for Registration<T> {
    fn drop(&mut self) {
        // SAFETY: Safe by the invariant of `ARef<drm::Device<T>>`. The existence of this
        // `Registration` also guarantees the this `drm::Device` is actually registered.
        unsafe { bindings::drm_dev_unregister(self.0.as_raw()) };
    }
}
