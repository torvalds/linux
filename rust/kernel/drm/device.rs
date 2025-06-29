// SPDX-License-Identifier: GPL-2.0 OR MIT

//! DRM device.
//!
//! C header: [`include/linux/drm/drm_device.h`](srctree/include/linux/drm/drm_device.h)

use crate::{
    bindings, device, drm,
    drm::driver::AllocImpl,
    error::from_err_ptr,
    error::Result,
    prelude::*,
    types::{ARef, AlwaysRefCounted, Opaque},
};
use core::{mem, ops::Deref, ptr, ptr::NonNull};

#[cfg(CONFIG_DRM_LEGACY)]
macro_rules! drm_legacy_fields {
    ( $($field:ident: $val:expr),* $(,)? ) => {
        bindings::drm_driver {
            $( $field: $val ),*,
            firstopen: None,
            preclose: None,
            dma_ioctl: None,
            dma_quiescent: None,
            context_dtor: None,
            irq_handler: None,
            irq_preinstall: None,
            irq_postinstall: None,
            irq_uninstall: None,
            get_vblank_counter: None,
            enable_vblank: None,
            disable_vblank: None,
            dev_priv_size: 0,
        }
    }
}

#[cfg(not(CONFIG_DRM_LEGACY))]
macro_rules! drm_legacy_fields {
    ( $($field:ident: $val:expr),* $(,)? ) => {
        bindings::drm_driver {
            $( $field: $val ),*
        }
    }
}

/// A typed DRM device with a specific `drm::Driver` implementation.
///
/// The device is always reference-counted.
///
/// # Invariants
///
/// `self.dev` is a valid instance of a `struct device`.
#[repr(C)]
#[pin_data]
pub struct Device<T: drm::Driver> {
    dev: Opaque<bindings::drm_device>,
    #[pin]
    data: T::Data,
}

impl<T: drm::Driver> Device<T> {
    const VTABLE: bindings::drm_driver = drm_legacy_fields! {
        load: None,
        open: Some(drm::File::<T::File>::open_callback),
        postclose: Some(drm::File::<T::File>::postclose_callback),
        unload: None,
        release: Some(Self::release),
        master_set: None,
        master_drop: None,
        debugfs_init: None,
        gem_create_object: T::Object::ALLOC_OPS.gem_create_object,
        prime_handle_to_fd: T::Object::ALLOC_OPS.prime_handle_to_fd,
        prime_fd_to_handle: T::Object::ALLOC_OPS.prime_fd_to_handle,
        gem_prime_import: T::Object::ALLOC_OPS.gem_prime_import,
        gem_prime_import_sg_table: T::Object::ALLOC_OPS.gem_prime_import_sg_table,
        dumb_create: T::Object::ALLOC_OPS.dumb_create,
        dumb_map_offset: T::Object::ALLOC_OPS.dumb_map_offset,
        show_fdinfo: None,
        fbdev_probe: None,

        major: T::INFO.major,
        minor: T::INFO.minor,
        patchlevel: T::INFO.patchlevel,
        name: T::INFO.name.as_char_ptr() as *mut _,
        desc: T::INFO.desc.as_char_ptr() as *mut _,

        driver_features: drm::driver::FEAT_GEM,
        ioctls: T::IOCTLS.as_ptr(),
        num_ioctls: T::IOCTLS.len() as i32,
        fops: &Self::GEM_FOPS as _,
    };

    const GEM_FOPS: bindings::file_operations = drm::gem::create_fops();

    /// Create a new `drm::Device` for a `drm::Driver`.
    pub fn new(dev: &device::Device, data: impl PinInit<T::Data, Error>) -> Result<ARef<Self>> {
        // SAFETY:
        // - `VTABLE`, as a `const` is pinned to the read-only section of the compilation,
        // - `dev` is valid by its type invarants,
        let raw_drm: *mut Self = unsafe {
            bindings::__drm_dev_alloc(
                dev.as_raw(),
                &Self::VTABLE,
                mem::size_of::<Self>(),
                mem::offset_of!(Self, dev),
            )
        }
        .cast();
        let raw_drm = NonNull::new(from_err_ptr(raw_drm)?).ok_or(ENOMEM)?;

        // SAFETY: `raw_drm` is a valid pointer to `Self`.
        let raw_data = unsafe { ptr::addr_of_mut!((*raw_drm.as_ptr()).data) };

        // SAFETY:
        // - `raw_data` is a valid pointer to uninitialized memory.
        // - `raw_data` will not move until it is dropped.
        unsafe { data.__pinned_init(raw_data) }.inspect_err(|_| {
            // SAFETY: `__drm_dev_alloc()` was successful, hence `raw_drm` must be valid and the
            // refcount must be non-zero.
            unsafe { bindings::drm_dev_put(ptr::addr_of_mut!((*raw_drm.as_ptr()).dev).cast()) };
        })?;

        // SAFETY: The reference count is one, and now we take ownership of that reference as a
        // `drm::Device`.
        Ok(unsafe { ARef::from_raw(raw_drm) })
    }

    pub(crate) fn as_raw(&self) -> *mut bindings::drm_device {
        self.dev.get()
    }

    /// # Safety
    ///
    /// `ptr` must be a valid pointer to a `struct device` embedded in `Self`.
    unsafe fn from_drm_device(ptr: *const bindings::drm_device) -> *mut Self {
        let ptr: *const Opaque<bindings::drm_device> = ptr.cast();

        // SAFETY: By the safety requirements of this function `ptr` is a valid pointer to a
        // `struct drm_device` embedded in `Self`.
        unsafe { crate::container_of!(ptr, Self, dev) }.cast_mut()
    }

    /// Not intended to be called externally, except via declare_drm_ioctls!()
    ///
    /// # Safety
    ///
    /// Callers must ensure that `ptr` is valid, non-null, and has a non-zero reference count,
    /// i.e. it must be ensured that the reference count of the C `struct drm_device` `ptr` points
    /// to can't drop to zero, for the duration of this function call and the entire duration when
    /// the returned reference exists.
    ///
    /// Additionally, callers must ensure that the `struct device`, `ptr` is pointing to, is
    /// embedded in `Self`.
    #[doc(hidden)]
    pub unsafe fn as_ref<'a>(ptr: *const bindings::drm_device) -> &'a Self {
        // SAFETY: By the safety requirements of this function `ptr` is a valid pointer to a
        // `struct drm_device` embedded in `Self`.
        let ptr = unsafe { Self::from_drm_device(ptr) };

        // SAFETY: `ptr` is valid by the safety requirements of this function.
        unsafe { &*ptr.cast() }
    }

    extern "C" fn release(ptr: *mut bindings::drm_device) {
        // SAFETY: `ptr` is a valid pointer to a `struct drm_device` and embedded in `Self`.
        let this = unsafe { Self::from_drm_device(ptr) };

        // SAFETY:
        // - When `release` runs it is guaranteed that there is no further access to `this`.
        // - `this` is valid for dropping.
        unsafe { core::ptr::drop_in_place(this) };
    }
}

impl<T: drm::Driver> Deref for Device<T> {
    type Target = T::Data;

    fn deref(&self) -> &Self::Target {
        &self.data
    }
}

// SAFETY: DRM device objects are always reference counted and the get/put functions
// satisfy the requirements.
unsafe impl<T: drm::Driver> AlwaysRefCounted for Device<T> {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference guarantees that the refcount is non-zero.
        unsafe { bindings::drm_dev_get(self.as_raw()) };
    }

    unsafe fn dec_ref(obj: NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is non-zero.
        unsafe { bindings::drm_dev_put(obj.cast().as_ptr()) };
    }
}

impl<T: drm::Driver> AsRef<device::Device> for Device<T> {
    fn as_ref(&self) -> &device::Device {
        // SAFETY: `bindings::drm_device::dev` is valid as long as the DRM device itself is valid,
        // which is guaranteed by the type invariant.
        unsafe { device::Device::as_ref((*self.as_raw()).dev) }
    }
}

// SAFETY: A `drm::Device` can be released from any thread.
unsafe impl<T: drm::Driver> Send for Device<T> {}

// SAFETY: A `drm::Device` can be shared among threads because all immutable methods are protected
// by the synchronization in `struct drm_device`.
unsafe impl<T: drm::Driver> Sync for Device<T> {}
