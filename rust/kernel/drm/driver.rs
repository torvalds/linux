// SPDX-License-Identifier: GPL-2.0 OR MIT

//! DRM driver core.
//!
//! C header: [`include/linux/drm/drm_drv.h`](srctree/include/linux/drm/drm_drv.h)

use crate::{bindings, drm, str::CStr};
use macros::vtable;

/// Driver use the GEM memory manager. This should be set for all modern drivers.
#[expect(unused)]
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
    #[expect(unused)]
    pub(crate) gem_create_object: Option<
        unsafe extern "C" fn(
            dev: *mut bindings::drm_device,
            size: usize,
        ) -> *mut bindings::drm_gem_object,
    >,
    #[expect(unused)]
    pub(crate) prime_handle_to_fd: Option<
        unsafe extern "C" fn(
            dev: *mut bindings::drm_device,
            file_priv: *mut bindings::drm_file,
            handle: u32,
            flags: u32,
            prime_fd: *mut core::ffi::c_int,
        ) -> core::ffi::c_int,
    >,
    #[expect(unused)]
    pub(crate) prime_fd_to_handle: Option<
        unsafe extern "C" fn(
            dev: *mut bindings::drm_device,
            file_priv: *mut bindings::drm_file,
            prime_fd: core::ffi::c_int,
            handle: *mut u32,
        ) -> core::ffi::c_int,
    >,
    #[expect(unused)]
    pub(crate) gem_prime_import: Option<
        unsafe extern "C" fn(
            dev: *mut bindings::drm_device,
            dma_buf: *mut bindings::dma_buf,
        ) -> *mut bindings::drm_gem_object,
    >,
    #[expect(unused)]
    pub(crate) gem_prime_import_sg_table: Option<
        unsafe extern "C" fn(
            dev: *mut bindings::drm_device,
            attach: *mut bindings::dma_buf_attachment,
            sgt: *mut bindings::sg_table,
        ) -> *mut bindings::drm_gem_object,
    >,
    #[expect(unused)]
    pub(crate) dumb_create: Option<
        unsafe extern "C" fn(
            file_priv: *mut bindings::drm_file,
            dev: *mut bindings::drm_device,
            args: *mut bindings::drm_mode_create_dumb,
        ) -> core::ffi::c_int,
    >,
    #[expect(unused)]
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
pub trait AllocImpl: super::private::Sealed {
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

    /// Driver metadata
    const INFO: DriverInfo;

    /// IOCTL list. See `kernel::drm::ioctl::declare_drm_ioctls!{}`.
    const IOCTLS: &'static [drm::ioctl::DrmIoctlDescriptor];
}
