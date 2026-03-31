// SPDX-License-Identifier: GPL-2.0

//! DRM GEM shmem helper objects
//!
//! C header: [`include/linux/drm/drm_gem_shmem_helper.h`](srctree/include/drm/drm_gem_shmem_helper.h)

// TODO:
// - There are a number of spots here that manually acquire/release the DMA reservation lock using
//   dma_resv_(un)lock(). In the future we should add support for ww mutex, expose a method to
//   acquire a reference to the WwMutex, and then use that directly instead of the C functions here.

use crate::{
    container_of,
    drm::{
        device,
        driver,
        gem,
        private::Sealed, //
    },
    error::to_result,
    prelude::*,
    types::{
        ARef,
        Opaque, //
    }, //
};
use core::{
    ops::{
        Deref,
        DerefMut, //
    },
    ptr::NonNull,
};
use gem::{
    BaseObjectPrivate,
    DriverObject,
    IntoGEMObject, //
};

/// A struct for controlling the creation of shmem-backed GEM objects.
///
/// This is used with [`Object::new()`] to control various properties that can only be set when
/// initially creating a shmem-backed GEM object.
#[derive(Default)]
pub struct ObjectConfig<'a, T: DriverObject> {
    /// Whether to set the write-combine map flag.
    pub map_wc: bool,

    /// Reuse the DMA reservation from another GEM object.
    ///
    /// The newly created [`Object`] will hold an owned refcount to `parent_resv_obj` if specified.
    pub parent_resv_obj: Option<&'a Object<T>>,
}

/// A shmem-backed GEM object.
///
/// # Invariants
///
/// `obj` contains a valid initialized `struct drm_gem_shmem_object` for the lifetime of this
/// object.
#[repr(C)]
#[pin_data]
pub struct Object<T: DriverObject> {
    #[pin]
    obj: Opaque<bindings::drm_gem_shmem_object>,
    /// Parent object that owns this object's DMA reservation object.
    parent_resv_obj: Option<ARef<Object<T>>>,
    #[pin]
    inner: T,
}

super::impl_aref_for_gem_obj!(impl<T> for Object<T> where T: DriverObject);

// SAFETY: All GEM objects are thread-safe.
unsafe impl<T: DriverObject> Send for Object<T> {}

// SAFETY: All GEM objects are thread-safe.
unsafe impl<T: DriverObject> Sync for Object<T> {}

impl<T: DriverObject> Object<T> {
    /// `drm_gem_object_funcs` vtable suitable for GEM shmem objects.
    const VTABLE: bindings::drm_gem_object_funcs = bindings::drm_gem_object_funcs {
        free: Some(Self::free_callback),
        open: Some(super::open_callback::<T>),
        close: Some(super::close_callback::<T>),
        print_info: Some(bindings::drm_gem_shmem_object_print_info),
        export: None,
        pin: Some(bindings::drm_gem_shmem_object_pin),
        unpin: Some(bindings::drm_gem_shmem_object_unpin),
        get_sg_table: Some(bindings::drm_gem_shmem_object_get_sg_table),
        vmap: Some(bindings::drm_gem_shmem_object_vmap),
        vunmap: Some(bindings::drm_gem_shmem_object_vunmap),
        mmap: Some(bindings::drm_gem_shmem_object_mmap),
        status: None,
        rss: None,
        #[allow(unused_unsafe, reason = "Safe since Rust 1.82.0")]
        // SAFETY: `drm_gem_shmem_vm_ops` is a valid, static const on the C side.
        vm_ops: unsafe { &raw const bindings::drm_gem_shmem_vm_ops },
        evict: None,
    };

    /// Return a raw pointer to the embedded drm_gem_shmem_object.
    fn as_raw_shmem(&self) -> *mut bindings::drm_gem_shmem_object {
        self.obj.get()
    }

    /// Create a new shmem-backed DRM object of the given size.
    ///
    /// Additional config options can be specified using `config`.
    pub fn new(
        dev: &device::Device<T::Driver>,
        size: usize,
        config: ObjectConfig<'_, T>,
        args: T::Args,
    ) -> Result<ARef<Self>> {
        let new: Pin<KBox<Self>> = KBox::try_pin_init(
            try_pin_init!(Self {
                obj <- Opaque::init_zeroed(),
                parent_resv_obj: config.parent_resv_obj.map(|p| p.into()),
                inner <- T::new(dev, size, args),
            }),
            GFP_KERNEL,
        )?;

        // SAFETY: `obj.as_raw()` is guaranteed to be valid by the initialization above.
        unsafe { (*new.as_raw()).funcs = &Self::VTABLE };

        // SAFETY: The arguments are all valid via the type invariants.
        to_result(unsafe { bindings::drm_gem_shmem_init(dev.as_raw(), new.as_raw_shmem(), size) })?;

        // SAFETY: We never move out of `self`.
        let new = KBox::into_raw(unsafe { Pin::into_inner_unchecked(new) });

        // SAFETY: We're taking over the owned refcount from `drm_gem_shmem_init`.
        let obj = unsafe { ARef::from_raw(NonNull::new_unchecked(new)) };

        // Start filling out values from `config`
        if let Some(parent_resv) = config.parent_resv_obj {
            // SAFETY: We have yet to expose the new gem object outside of this function, so it is
            // safe to modify this field.
            unsafe { (*obj.obj.get()).base.resv = parent_resv.raw_dma_resv() };
        }

        // SAFETY: We have yet to expose this object outside of this function, so we're guaranteed
        // to have exclusive access - thus making this safe to hold a mutable reference to.
        let shmem = unsafe { &mut *obj.as_raw_shmem() };
        shmem.set_map_wc(config.map_wc);

        Ok(obj)
    }

    /// Returns the `Device` that owns this GEM object.
    pub fn dev(&self) -> &device::Device<T::Driver> {
        // SAFETY: `dev` will have been initialized in `Self::new()` by `drm_gem_shmem_init()`.
        unsafe { device::Device::from_raw((*self.as_raw()).dev) }
    }

    extern "C" fn free_callback(obj: *mut bindings::drm_gem_object) {
        // SAFETY:
        // - DRM always passes a valid gem object here
        // - We used drm_gem_shmem_create() in our create_gem_object callback, so we know that
        //   `obj` is contained within a drm_gem_shmem_object
        let this = unsafe { container_of!(obj, bindings::drm_gem_shmem_object, base) };

        // SAFETY:
        // - We're in free_callback - so this function is safe to call.
        // - We won't be using the gem resources on `this` after this call.
        unsafe { bindings::drm_gem_shmem_release(this) };

        // SAFETY:
        // - We verified above that `obj` is valid, which makes `this` valid
        // - This function is set in AllocOps, so we know that `this` is contained within a
        //   `Object<T>`
        let this = unsafe { container_of!(Opaque::cast_from(this), Self, obj) }.cast_mut();

        // SAFETY: We're recovering the Kbox<> we created in gem_create_object()
        let _ = unsafe { KBox::from_raw(this) };
    }
}

impl<T: DriverObject> Deref for Object<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl<T: DriverObject> DerefMut for Object<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.inner
    }
}

impl<T: DriverObject> Sealed for Object<T> {}

impl<T: DriverObject> gem::IntoGEMObject for Object<T> {
    fn as_raw(&self) -> *mut bindings::drm_gem_object {
        // SAFETY:
        // - Our immutable reference is proof that this is safe to dereference.
        // - `obj` is always a valid drm_gem_shmem_object via our type invariants.
        unsafe { &raw mut (*self.obj.get()).base }
    }

    unsafe fn from_raw<'a>(obj: *mut bindings::drm_gem_object) -> &'a Object<T> {
        // SAFETY: The safety contract of from_gem_obj() guarantees that `obj` is contained within
        // `Self`
        unsafe {
            let obj = Opaque::cast_from(container_of!(obj, bindings::drm_gem_shmem_object, base));

            &*container_of!(obj, Object<T>, obj)
        }
    }
}

impl<T: DriverObject> driver::AllocImpl for Object<T> {
    type Driver = T::Driver;

    const ALLOC_OPS: driver::AllocOps = driver::AllocOps {
        gem_create_object: None,
        prime_handle_to_fd: None,
        prime_fd_to_handle: None,
        gem_prime_import: None,
        gem_prime_import_sg_table: Some(bindings::drm_gem_shmem_prime_import_sg_table),
        dumb_create: Some(bindings::drm_gem_shmem_dumb_create),
        dumb_map_offset: None,
    };
}
