// SPDX-License-Identifier: GPL-2.0 OR MIT

//! DRM driver core.
//!
//! C header: [`include/drm/drm_drv.h`](srctree/include/drm/drm_drv.h)

use crate::{
    bindings,
    device,
    drm,
    error::to_result,
    prelude::*,
    sync::aref::ARef, //
};
use core::ptr::NonNull;

/// Driver use the GEM memory manager. This should be set for all modern drivers.
pub(crate) const FEAT_GEM: u32 = bindings::drm_driver_feature_DRIVER_GEM;
/// Driver supports render nodes, i.e.: /dev/dri/renderDXX devices.
pub(crate) const FEAT_RENDER: u32 = bindings::drm_driver_feature_DRIVER_RENDER;

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
    /// The [`Driver`] implementation for this [`AllocImpl`].
    type Driver: drm::Driver;

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

    /// Data owned by the [`Registration`] and accessible within a
    /// [`RegistrationGuard`](drm::RegistrationGuard) critical section via
    /// [`Device::registration_data_with()`](drm::Device::registration_data_with).
    ///
    /// The lifetime parameter is tied to the [`Registration`] scope, which is enclosed in the
    /// parent bus device binding scope but may be shorter.
    type RegistrationData<'a>: Send + Sync + 'a;

    /// The type used to manage memory for this driver.
    type Object: AllocImpl;

    /// The type used to represent a DRM File (client)
    type File: drm::file::DriverFile;

    /// The bus device type of the parent device that the DRM device is associated with.
    type ParentDevice<Ctx: device::DeviceContext>: device::AsBusDevice<Ctx>;

    /// Driver metadata
    const INFO: DriverInfo;

    /// IOCTL list. See `kernel::drm::ioctl::declare_drm_ioctls!{}`.
    const IOCTLS: &'static [drm::ioctl::DrmIoctlDescriptor];

    /// Sets the `DRIVER_RENDER` feature for this driver.
    ///
    /// When enabled, the driver exposes `/dev/dri/renderDXX` render nodes to
    /// userspace. The render node is an alternate low-privilege way to access
    /// the driver, which is enforced on a per-ioctl level. Userspace processes
    /// that open the render node can only invoke ioctls explicitly listed as
    /// usable from the render node (i.e. marked DRM_RENDER_ALLOW), whereas
    /// userspace processes using the master node can invoke any ioctl.
    const FEAT_RENDER: bool = false;
}

/// The registration type of a `drm::Device`.
///
/// Once the `Registration` structure is dropped, the device is unregistered.
pub struct Registration<'a, T: Driver> {
    drm: ARef<drm::Device<T>>,
    _reg_data: Pin<KBox<T::RegistrationData<'a>>>,
}

impl<'a, T: Driver> Registration<'a, T> {
    /// Register a new [`UnregisteredDevice`](drm::UnregisteredDevice) with userspace.
    ///
    /// # Safety
    ///
    /// The caller must not `mem::forget()` the returned [`Registration`] or otherwise prevent its
    /// [`Drop`] implementation from running, since the registration data may contain borrowed
    /// references that become invalid after `'a` ends.
    pub unsafe fn new<E>(
        dev: &'a device::Device<device::Bound>,
        drm: drm::UnregisteredDevice<T>,
        reg_data: impl PinInit<T::RegistrationData<'a>, E>,
        flags: usize,
    ) -> Result<Self>
    where
        Error: From<E>,
    {
        let parent = drm.as_ref();
        if parent.as_ref().as_raw() != dev.as_raw() {
            return Err(EINVAL);
        }

        let reg_data: Pin<KBox<T::RegistrationData<'a>>> = KBox::pin_init(reg_data, GFP_KERNEL)?;

        // Store the registration data pointer in the device before registration, so that it is
        // visible once ioctls can be called.
        let ptr: NonNull<T::RegistrationData<'static>> =
            NonNull::from(Pin::get_ref(reg_data.as_ref())).cast();

        // SAFETY: No concurrent access; the device is not yet registered.
        unsafe { *drm.registration_data.get() = ptr };

        // SAFETY: `drm` is a valid, initialized but not yet registered DRM device.
        let ret = unsafe { bindings::drm_dev_register(drm.as_raw(), flags) };
        if let Err(e) = to_result(ret) {
            // SAFETY: `drm_dev_register()` synchronizes SRCU on failure, so no concurrent
            // access to `registration_data` is possible at this point.
            unsafe { *drm.registration_data.get() = NonNull::dangling() };
            return Err(e);
        }

        Ok(Self {
            drm: (&*drm).into(),
            _reg_data: reg_data,
        })
    }

    /// Returns a reference to the `Device` instance for this registration.
    pub fn device(&self) -> &drm::Device<T> {
        &self.drm
    }
}

// SAFETY: `Registration` doesn't offer any methods or access to fields when shared between
// threads, hence it's safe to share it.
unsafe impl<T: Driver> Sync for Registration<'_, T> {}

// SAFETY: Registration with and unregistration from the DRM subsystem can happen from any thread.
unsafe impl<T: Driver> Send for Registration<'_, T> {}

impl<T: Driver> Drop for Registration<'_, T> {
    fn drop(&mut self) {
        // Use `drm_dev_unplug` rather than `drm_dev_unregister` to ensure that existing
        // `drm_dev_enter()` critical sections complete before unregistration proceeds. This
        // is required for the safety of `RegistrationGuard`, which relies on the SRCU barrier in
        // `drm_dev_unplug()` to guarantee that the parent device is still bound within the
        // critical section.
        //
        // SAFETY: Safe by the invariant of `ARef<drm::Device<T>>`. The existence of this
        // `Registration` also guarantees that this `drm::Device` is actually registered.
        unsafe { bindings::drm_dev_unplug(self.drm.as_raw()) };
        // After drm_dev_unplug(), the SRCU barrier guarantees that all RegistrationGuard critical
        // sections have completed, so no one holds a reference to reg_data anymore.
        // reg_data is dropped here automatically.
    }
}
