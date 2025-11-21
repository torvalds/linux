// SPDX-License-Identifier: GPL-2.0 OR MIT

//! DRM GEM API
//!
//! C header: [`include/drm/drm_gem.h`](srctree/include/drm/drm_gem.h)

use crate::{
    alloc::flags::*,
    bindings, drm,
    drm::driver::{AllocImpl, AllocOps},
    error::{to_result, Result},
    prelude::*,
    sync::aref::{ARef, AlwaysRefCounted},
    types::Opaque,
};
use core::{ops::Deref, ptr::NonNull};

/// A type alias for retrieving a [`Driver`]s [`DriverFile`] implementation from its
/// [`DriverObject`] implementation.
///
/// [`Driver`]: drm::Driver
/// [`DriverFile`]: drm::file::DriverFile
pub type DriverFile<T> = drm::File<<<T as DriverObject>::Driver as drm::Driver>::File>;

/// GEM object functions, which must be implemented by drivers.
pub trait DriverObject: Sync + Send + Sized {
    /// Parent `Driver` for this object.
    type Driver: drm::Driver;

    /// Create a new driver data object for a GEM object of a given size.
    fn new(dev: &drm::Device<Self::Driver>, size: usize) -> impl PinInit<Self, Error>;

    /// Open a new handle to an existing object, associated with a File.
    fn open(_obj: &<Self::Driver as drm::Driver>::Object, _file: &DriverFile<Self>) -> Result {
        Ok(())
    }

    /// Close a handle to an existing object, associated with a File.
    fn close(_obj: &<Self::Driver as drm::Driver>::Object, _file: &DriverFile<Self>) {}
}

/// Trait that represents a GEM object subtype
pub trait IntoGEMObject: Sized + super::private::Sealed + AlwaysRefCounted {
    /// Returns a reference to the raw `drm_gem_object` structure, which must be valid as long as
    /// this owning object is valid.
    fn as_raw(&self) -> *mut bindings::drm_gem_object;

    /// Converts a pointer to a `struct drm_gem_object` into a reference to `Self`.
    ///
    /// # Safety
    ///
    /// - `self_ptr` must be a valid pointer to `Self`.
    /// - The caller promises that holding the immutable reference returned by this function does
    ///   not violate rust's data aliasing rules and remains valid throughout the lifetime of `'a`.
    unsafe fn from_raw<'a>(self_ptr: *mut bindings::drm_gem_object) -> &'a Self;
}

// SAFETY: All gem objects are refcounted.
unsafe impl<T: IntoGEMObject> AlwaysRefCounted for T {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference guarantees that the refcount is non-zero.
        unsafe { bindings::drm_gem_object_get(self.as_raw()) };
    }

    unsafe fn dec_ref(obj: NonNull<Self>) {
        // SAFETY: We either hold the only refcount on `obj`, or one of many - meaning that no one
        // else could possibly hold a mutable reference to `obj` and thus this immutable reference
        // is safe.
        let obj = unsafe { obj.as_ref() }.as_raw();

        // SAFETY:
        // - The safety requirements guarantee that the refcount is non-zero.
        // - We hold no references to `obj` now, making it safe for us to potentially deallocate it.
        unsafe { bindings::drm_gem_object_put(obj) };
    }
}

extern "C" fn open_callback<T: DriverObject>(
    raw_obj: *mut bindings::drm_gem_object,
    raw_file: *mut bindings::drm_file,
) -> core::ffi::c_int {
    // SAFETY: `open_callback` is only ever called with a valid pointer to a `struct drm_file`.
    let file = unsafe { DriverFile::<T>::from_raw(raw_file) };

    // SAFETY: `open_callback` is specified in the AllocOps structure for `DriverObject<T>`,
    // ensuring that `raw_obj` is contained within a `DriverObject<T>`
    let obj = unsafe { <<T::Driver as drm::Driver>::Object as IntoGEMObject>::from_raw(raw_obj) };

    match T::open(obj, file) {
        Err(e) => e.to_errno(),
        Ok(()) => 0,
    }
}

extern "C" fn close_callback<T: DriverObject>(
    raw_obj: *mut bindings::drm_gem_object,
    raw_file: *mut bindings::drm_file,
) {
    // SAFETY: `open_callback` is only ever called with a valid pointer to a `struct drm_file`.
    let file = unsafe { DriverFile::<T>::from_raw(raw_file) };

    // SAFETY: `close_callback` is specified in the AllocOps structure for `Object<T>`, ensuring
    // that `raw_obj` is indeed contained within a `Object<T>`.
    let obj = unsafe { <<T::Driver as drm::Driver>::Object as IntoGEMObject>::from_raw(raw_obj) };

    T::close(obj, file);
}

impl<T: DriverObject> IntoGEMObject for Object<T> {
    fn as_raw(&self) -> *mut bindings::drm_gem_object {
        self.obj.get()
    }

    unsafe fn from_raw<'a>(self_ptr: *mut bindings::drm_gem_object) -> &'a Self {
        // SAFETY: `obj` is guaranteed to be in an `Object<T>` via the safety contract of this
        // function
        unsafe { &*crate::container_of!(Opaque::cast_from(self_ptr), Object<T>, obj) }
    }
}

/// Base operations shared by all GEM object classes
pub trait BaseObject: IntoGEMObject {
    /// Returns the size of the object in bytes.
    fn size(&self) -> usize {
        // SAFETY: `self.as_raw()` is guaranteed to be a pointer to a valid `struct drm_gem_object`.
        unsafe { (*self.as_raw()).size }
    }

    /// Creates a new handle for the object associated with a given `File`
    /// (or returns an existing one).
    fn create_handle<D, F>(&self, file: &drm::File<F>) -> Result<u32>
    where
        Self: AllocImpl<Driver = D>,
        D: drm::Driver<Object = Self, File = F>,
        F: drm::file::DriverFile<Driver = D>,
    {
        let mut handle: u32 = 0;
        // SAFETY: The arguments are all valid per the type invariants.
        to_result(unsafe {
            bindings::drm_gem_handle_create(file.as_raw().cast(), self.as_raw(), &mut handle)
        })?;
        Ok(handle)
    }

    /// Looks up an object by its handle for a given `File`.
    fn lookup_handle<D, F>(file: &drm::File<F>, handle: u32) -> Result<ARef<Self>>
    where
        Self: AllocImpl<Driver = D>,
        D: drm::Driver<Object = Self, File = F>,
        F: drm::file::DriverFile<Driver = D>,
    {
        // SAFETY: The arguments are all valid per the type invariants.
        let ptr = unsafe { bindings::drm_gem_object_lookup(file.as_raw().cast(), handle) };
        if ptr.is_null() {
            return Err(ENOENT);
        }

        // SAFETY:
        // - A `drm::Driver` can only have a single `File` implementation.
        // - `file` uses the same `drm::Driver` as `Self`.
        // - Therefore, we're guaranteed that `ptr` must be a gem object embedded within `Self`.
        // - And we check if the pointer is null befoe calling from_raw(), ensuring that `ptr` is a
        //   valid pointer to an initialized `Self`.
        let obj = unsafe { Self::from_raw(ptr) };

        // SAFETY:
        // - We take ownership of the reference of `drm_gem_object_lookup()`.
        // - Our `NonNull` comes from an immutable reference, thus ensuring it is a valid pointer to
        //   `Self`.
        Ok(unsafe { ARef::from_raw(obj.into()) })
    }

    /// Creates an mmap offset to map the object from userspace.
    fn create_mmap_offset(&self) -> Result<u64> {
        // SAFETY: The arguments are valid per the type invariant.
        to_result(unsafe { bindings::drm_gem_create_mmap_offset(self.as_raw()) })?;

        // SAFETY: The arguments are valid per the type invariant.
        Ok(unsafe { bindings::drm_vma_node_offset_addr(&raw mut (*self.as_raw()).vma_node) })
    }
}

impl<T: IntoGEMObject> BaseObject for T {}

/// A base GEM object.
///
/// Invariants
///
/// - `self.obj` is a valid instance of a `struct drm_gem_object`.
/// - `self.dev` is always a valid pointer to a `struct drm_device`.
#[repr(C)]
#[pin_data]
pub struct Object<T: DriverObject + Send + Sync> {
    obj: Opaque<bindings::drm_gem_object>,
    dev: NonNull<drm::Device<T::Driver>>,
    #[pin]
    data: T,
}

impl<T: DriverObject> Object<T> {
    const OBJECT_FUNCS: bindings::drm_gem_object_funcs = bindings::drm_gem_object_funcs {
        free: Some(Self::free_callback),
        open: Some(open_callback::<T>),
        close: Some(close_callback::<T>),
        print_info: None,
        export: None,
        pin: None,
        unpin: None,
        get_sg_table: None,
        vmap: None,
        vunmap: None,
        mmap: None,
        status: None,
        vm_ops: core::ptr::null_mut(),
        evict: None,
        rss: None,
    };

    /// Create a new GEM object.
    pub fn new(dev: &drm::Device<T::Driver>, size: usize) -> Result<ARef<Self>> {
        let obj: Pin<KBox<Self>> = KBox::pin_init(
            try_pin_init!(Self {
                obj: Opaque::new(bindings::drm_gem_object::default()),
                data <- T::new(dev, size),
                // INVARIANT: The drm subsystem guarantees that the `struct drm_device` will live
                // as long as the GEM object lives.
                dev: dev.into(),
            }),
            GFP_KERNEL,
        )?;

        // SAFETY: `obj.as_raw()` is guaranteed to be valid by the initialization above.
        unsafe { (*obj.as_raw()).funcs = &Self::OBJECT_FUNCS };

        // SAFETY: The arguments are all valid per the type invariants.
        to_result(unsafe { bindings::drm_gem_object_init(dev.as_raw(), obj.obj.get(), size) })?;

        // SAFETY: We never move out of `Self`.
        let ptr = KBox::into_raw(unsafe { Pin::into_inner_unchecked(obj) });

        // SAFETY: `ptr` comes from `KBox::into_raw` and hence can't be NULL.
        let ptr = unsafe { NonNull::new_unchecked(ptr) };

        // SAFETY: We take over the initial reference count from `drm_gem_object_init()`.
        Ok(unsafe { ARef::from_raw(ptr) })
    }

    /// Returns the `Device` that owns this GEM object.
    pub fn dev(&self) -> &drm::Device<T::Driver> {
        // SAFETY: The DRM subsystem guarantees that the `struct drm_device` will live as long as
        // the GEM object lives, hence the pointer must be valid.
        unsafe { self.dev.as_ref() }
    }

    fn as_raw(&self) -> *mut bindings::drm_gem_object {
        self.obj.get()
    }

    extern "C" fn free_callback(obj: *mut bindings::drm_gem_object) {
        let ptr: *mut Opaque<bindings::drm_gem_object> = obj.cast();

        // SAFETY: All of our objects are of type `Object<T>`.
        let this = unsafe { crate::container_of!(ptr, Self, obj) };

        // SAFETY: The C code only ever calls this callback with a valid pointer to a `struct
        // drm_gem_object`.
        unsafe { bindings::drm_gem_object_release(obj) };

        // SAFETY: All of our objects are allocated via `KBox`, and we're in the
        // free callback which guarantees this object has zero remaining references,
        // so we can drop it.
        let _ = unsafe { KBox::from_raw(this) };
    }
}

impl<T: DriverObject> super::private::Sealed for Object<T> {}

impl<T: DriverObject> Deref for Object<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.data
    }
}

impl<T: DriverObject> AllocImpl for Object<T> {
    type Driver = T::Driver;

    const ALLOC_OPS: AllocOps = AllocOps {
        gem_create_object: None,
        prime_handle_to_fd: None,
        prime_fd_to_handle: None,
        gem_prime_import: None,
        gem_prime_import_sg_table: None,
        dumb_create: None,
        dumb_map_offset: None,
    };
}

pub(super) const fn create_fops() -> bindings::file_operations {
    // SAFETY: As by the type invariant, it is safe to initialize `bindings::file_operations`
    // zeroed.
    let mut fops: bindings::file_operations = unsafe { core::mem::zeroed() };

    fops.owner = core::ptr::null_mut();
    fops.open = Some(bindings::drm_open);
    fops.release = Some(bindings::drm_release);
    fops.unlocked_ioctl = Some(bindings::drm_ioctl);
    #[cfg(CONFIG_COMPAT)]
    {
        fops.compat_ioctl = Some(bindings::drm_compat_ioctl);
    }
    fops.poll = Some(bindings::drm_poll);
    fops.read = Some(bindings::drm_read);
    fops.llseek = Some(bindings::noop_llseek);
    fops.mmap = Some(bindings::drm_gem_mmap);
    fops.fop_flags = bindings::FOP_UNSIGNED_OFFSET;

    fops
}
