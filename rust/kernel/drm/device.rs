// SPDX-License-Identifier: GPL-2.0 OR MIT

//! DRM device.
//!
//! C header: [`include/drm/drm_device.h`](srctree/include/drm/drm_device.h)

use crate::{
    alloc::allocator::Kmalloc,
    bindings,
    device,
    drm::{
        self,
        driver::AllocImpl,
        private::Sealed, //
    },
    error::from_err_ptr,
    prelude::*,
    sync::aref::{
        ARef,
        AlwaysRefCounted, //
    },
    types::{
        NotThreadSafe,
        Opaque, //
    },
    workqueue::{
        HasDelayedWork,
        HasWork,
        Work,
        WorkItem, //
    }, //
};
use core::{
    alloc::Layout,
    cell::UnsafeCell,
    marker::PhantomData,
    mem,
    ops::Deref,
    ptr::{
        self,
        NonNull, //
    },
};

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

/// A trait implemented by all possible contexts a [`Device`] can be used in.
///
/// A [`Device`] can be in one of the following contexts:
///
/// - [`Normal`]: The general-purpose, reference-counted context. A [`Device`] in this context may
///   or may not be registered with userspace.
/// - [`Ioctl`]: The device has been registered with userspace at some point; used in ioctl
///   dispatch context.
/// - [`Registered`]: The device is currently registered with userspace and the parent bus device
///   is bound.
///
/// Both `Device<T, Ioctl>` and `Device<T, Registered>` dereference to `Device<T>` ([`Normal`]),
/// so any method available on a [`Normal`] device is also available in the other contexts.
pub trait DeviceContext: Sealed + Send + Sync + 'static {}

/// The general-purpose, reference-counted [`DeviceContext`].
///
/// A [`Device`] in this context may or may not be registered with userspace. This context is used
/// for reference-counted device handles and during device setup via [`UnregisteredDevice`].
///
/// [`AlwaysRefCounted`] is only implemented for `Device<T, Normal>`, making this the required
/// context for [`ARef`]-based device handles.
pub struct Normal;

impl Sealed for Normal {}
impl DeviceContext for Normal {}

/// The [`DeviceContext`] of a [`Device`] that is currently registered with userspace.
///
/// A [`Device`] in this context is guaranteed to be registered and its parent bus device is
/// guaranteed to be bound. This is enforced at runtime by [`RegistrationGuard`], which holds a
/// `drm_dev_enter()` / `drm_dev_exit()` SRCU critical section.
///
/// # Invariants
///
/// The parent bus device is bound for the duration of any reference to a `Device<T, Registered>`.
pub struct Registered;

impl Sealed for Registered {}
impl DeviceContext for Registered {}

/// The [`DeviceContext`] of a [`Device`] that has been registered with userspace previously.
///
/// A [`Device`] in this context has been registered at some point, but may be concurrently
/// unregistering or already unregistered. `drm_dev_enter()` can guard against this, ensuring the
/// device remains registered for the duration of the critical section.
///
/// # Invariants
///
/// A [`Device`] in this context has been registered with userspace via `drm_dev_register()` at
/// some point.
pub struct Ioctl;

impl Sealed for Ioctl {}
impl DeviceContext for Ioctl {}

/// A [`Device`] which is known at compile-time to be unregistered with userspace.
///
/// This type allows performing operations which are only safe to do before userspace registration,
/// and can be used to create a [`Registration`](drm::driver::Registration) once the driver is ready
/// to register the device with userspace.
///
/// Since DRM device initialization must be single-threaded, this object is not thread-safe.
///
/// # Invariants
///
/// The device in `self.0` is guaranteed to be a newly created [`Device`] that has not yet been
/// registered with userspace until this type is dropped.
pub struct UnregisteredDevice<T: drm::Driver>(ARef<Device<T, Normal>>, NotThreadSafe);

impl<T: drm::Driver> Deref for UnregisteredDevice<T> {
    type Target = Device<T, Normal>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl<T: drm::Driver> UnregisteredDevice<T> {
    const fn compute_features() -> u32 {
        let mut features = drm::driver::FEAT_GEM;

        if T::FEAT_RENDER {
            features |= drm::driver::FEAT_RENDER;
        }

        features
    }

    const VTABLE: bindings::drm_driver = drm_legacy_fields! {
        load: None,
        open: Some(drm::File::<T::File>::open_callback),
        postclose: Some(drm::File::<T::File>::postclose_callback),
        unload: None,
        release: Some(Device::<T>::release),
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
        name: crate::str::as_char_ptr_in_const_context(T::INFO.name).cast_mut(),
        desc: crate::str::as_char_ptr_in_const_context(T::INFO.desc).cast_mut(),

        driver_features: Self::compute_features(),
        ioctls: T::IOCTLS.as_ptr(),
        num_ioctls: T::IOCTLS.len() as i32,
        fops: &Self::GEM_FOPS,
    };

    const GEM_FOPS: bindings::file_operations = drm::gem::create_fops();

    /// Create a new `UnregisteredDevice` for a `drm::Driver`.
    ///
    /// This can be used to create a [`Registration`](kernel::drm::Registration).
    pub fn new(
        dev: &T::ParentDevice<device::Bound>,
        data: impl PinInit<T::Data, Error>,
    ) -> Result<Self> {
        // `__drm_dev_alloc` uses `kmalloc()` to allocate memory, hence ensure a `kmalloc()`
        // compatible `Layout`.
        let layout = Kmalloc::aligned_layout(Layout::new::<Device<T, Normal>>());

        // Use a temporary vtable without a `release` callback until `data` is initialized, so
        // init failure can release the DRM device without dropping uninitialized fields.
        let alloc_vtable = bindings::drm_driver {
            release: None,
            ..Self::VTABLE
        };

        // SAFETY:
        // - `alloc_vtable` reference remains valid until no longer used,
        // - `dev` is valid by its type invarants,
        let raw_drm: *mut Device<T, Normal> = unsafe {
            bindings::__drm_dev_alloc(
                dev.as_ref().as_raw(),
                &alloc_vtable,
                layout.size(),
                mem::offset_of!(Device<T, Normal>, dev),
            )
        }
        .cast();
        let raw_drm = NonNull::new(from_err_ptr(raw_drm)?).ok_or(ENOMEM)?;

        // SAFETY: `raw_drm` is a valid pointer to `Self`, given that `__drm_dev_alloc` was
        // successful.
        let drm_dev = unsafe { Device::into_drm_device(raw_drm) };

        // SAFETY: `raw_drm` is a valid pointer to `Self`.
        let raw_data = unsafe { ptr::addr_of_mut!((*raw_drm.as_ptr()).data) };

        // SAFETY:
        // - `raw_data` is a valid pointer to uninitialized memory.
        // - `raw_data` will not move until it is dropped.
        unsafe { data.__pinned_init(raw_data) }.inspect_err(|_| {
            // SAFETY: `__drm_dev_alloc()` was successful, hence `drm_dev` must be valid and the
            // refcount must be non-zero.
            unsafe { bindings::drm_dev_put(drm_dev) };
        })?;

        // SAFETY: `drm_dev` is still private to this function.
        unsafe { (*drm_dev).driver = const { &Self::VTABLE } };

        // SAFETY: `raw_drm` is valid; no concurrent access before registration.
        unsafe { (*raw_drm.as_ptr()).registration_data = UnsafeCell::new(NonNull::dangling()) };

        // SAFETY: The reference count is one, and now we take ownership of that reference as a
        // `drm::Device`.
        // INVARIANT: We just created the device above, but have yet to call `drm_dev_register`.
        // `Self` cannot be copied or sent to another thread - ensuring that `drm_dev_register`
        // won't be called during its lifetime and that the device is unregistered.
        Ok(Self(unsafe { ARef::from_raw(raw_drm) }, NotThreadSafe))
    }
}

/// A typed DRM device with a specific [`drm::Driver`] implementation and [`DeviceContext`].
///
/// A device in the [`Registered`] context is currently registered with userspace and its parent
/// bus device is bound. The [`Normal`] context is the general-purpose, reference-counted context.
///
/// # Invariants
///
/// * `self.dev` is a valid instance of a `struct device`.
/// * The data layout of `Self` remains the same across all implementations of `C`.
/// * Any invariants for `C` also apply.
#[repr(C)]
pub struct Device<T: drm::Driver, C: DeviceContext = Normal> {
    dev: Opaque<bindings::drm_device>,
    data: T::Data,
    pub(super) registration_data: UnsafeCell<NonNull<T::RegistrationData<'static>>>,
    _ctx: PhantomData<C>,
}

impl<T: drm::Driver, C: DeviceContext> Device<T, C> {
    pub(crate) fn as_raw(&self) -> *mut bindings::drm_device {
        self.dev.get()
    }

    /// # Safety
    ///
    /// `ptr` must be a valid pointer to a `struct device` embedded in `Self`.
    unsafe fn from_drm_device(ptr: *const bindings::drm_device) -> *mut Self {
        // SAFETY: By the safety requirements of this function `ptr` is a valid pointer to a
        // `struct drm_device` embedded in `Self`.
        unsafe { crate::container_of!(Opaque::cast_from(ptr), Self, dev) }.cast_mut()
    }

    /// # Safety
    ///
    /// `ptr` must be a valid pointer to `Self`.
    unsafe fn into_drm_device(ptr: NonNull<Self>) -> *mut bindings::drm_device {
        // SAFETY: By the safety requirements of this function, `ptr` is a valid pointer to `Self`.
        unsafe { &raw mut (*ptr.as_ptr()).dev }.cast()
    }

    /// Not intended to be called externally, except via declare_drm_ioctls!()
    ///
    /// # Safety
    ///
    /// * Callers must ensure that `ptr` is valid, non-null, and has a non-zero reference count,
    ///   i.e. it must be ensured that the reference count of the C `struct drm_device` `ptr` points
    ///   to can't drop to zero, for the duration of this function call and the entire duration when
    ///   the returned reference exists.
    /// * Additionally, callers must ensure that the `struct device`, `ptr` is pointing to, is
    ///   embedded in `Self`.
    /// * Callers promise that any type invariants of `C` will be upheld.
    #[doc(hidden)]
    pub unsafe fn from_raw<'a>(ptr: *const bindings::drm_device) -> &'a Self {
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

    /// Change the [`DeviceContext`] for a [`Device`].
    ///
    /// # Safety
    ///
    /// The caller promises that `self` fulfills all of the guarantees provided by the given
    /// [`DeviceContext`].
    pub(crate) unsafe fn assume_ctx<NewCtx: DeviceContext>(&self) -> &Device<T, NewCtx> {
        // SAFETY: The data layout is identical via our type invariants.
        unsafe { mem::transmute(self) }
    }
}

impl<T: drm::Driver> Device<T, Ioctl> {
    /// Guard against the parent bus device being unbound.
    ///
    /// Returns a [`RegistrationGuard`] if the device has not been unplugged, [`None`] otherwise.
    ///
    /// While [`RegistrationGuard`] is held the parent device is guaranteed to be bound.
    #[must_use]
    pub fn registration_guard(&self) -> Option<RegistrationGuard<'_, T>> {
        let mut idx: i32 = 0;
        // SAFETY: `self.as_raw()` is a valid pointer to a `struct drm_device`.
        if unsafe { bindings::drm_dev_enter(self.as_raw(), &mut idx) } {
            // INVARIANT:
            // - `idx` is the SRCU index from the successful `drm_dev_enter()` above.
            // - The parent bus device is bound: `drm_dev_enter()` succeeded, meaning
            //   `drm_dev_unplug()` has not completed; since it is only called from
            //   `Registration::drop()` during parent unbind, the parent is still bound.
            Some(RegistrationGuard {
                // SAFETY: See INVARIANT above; the `Registered` context invariant holds.
                dev: unsafe { self.assume_ctx() },
                idx,
                _not_send: NotThreadSafe,
            })
        } else {
            None
        }
    }
}

/// A guard proving the DRM device is registered and the parent bus device is bound.
///
/// The guard dereferences to [`Device<T, Registered>`], providing access to the DRM device with
/// the guarantee that the parent bus device is bound for the entire duration of the critical
/// section.
///
/// Internally this is backed by a `drm_dev_enter()` / `drm_dev_exit()` SRCU critical section.
///
/// # Invariants
///
/// - `idx` is the SRCU read lock index returned by a successful `drm_dev_enter()` call.
/// - The parent bus device of `dev` is bound for the lifetime of this guard.
#[must_use]
pub struct RegistrationGuard<'a, T: drm::Driver> {
    dev: &'a Device<T, Registered>,
    idx: i32,
    _not_send: NotThreadSafe,
}

impl<T: drm::Driver> Device<T, Registered> {
    /// Returns a reference to the registration data with lifetime shortened from `'static`.
    ///
    /// # Safety
    ///
    /// The returned reference must not be exposed to code that can choose a concrete lifetime for
    /// it, as that would be unsound for types that are invariant over their lifetime parameter
    /// (e.g. it must be passed through an HRTB-bounded closure).
    #[inline]
    unsafe fn registration_data_unchecked(&self) -> &T::RegistrationData<'_> {
        // SAFETY:
        // - `Registered` guarantees the parent bus device is bound, hence the pointer is valid.
        // - The pointer cast from `Of<'static>` to `Of<'_>` is layout-compatible since lifetimes
        //   are erased at runtime.
        // - Caller guarantees the reference is only used behind an HRTB, making the lifetime
        //   shortening sound regardless of variance.
        unsafe { (*self.registration_data.get()).cast::<_>().as_ref() }
    }

    /// Access the registration data through a closure, with the lifetime tied to the closure
    /// scope.
    ///
    /// The data is owned by [`Registration`](drm::Registration) and is guaranteed to remain valid
    /// as long as the device is registered, since [`Registration`](drm::Registration)'s `drop`
    /// calls `drm_dev_unplug()` which waits for all `drm_dev_enter()` critical sections to
    /// complete.
    #[inline]
    pub fn registration_data_with<R, F>(&self, f: F) -> R
    where
        F: for<'a> FnOnce(&'a T::RegistrationData<'a>) -> R,
    {
        // SAFETY: `Registered` guarantees the device is registered and the parent bus device is
        // bound. The closure's HRTB `for<'a>` prevents the caller from smuggling in references
        // with a concrete short lifetime, satisfying the lifetime requirement of
        // `registration_data_unchecked`.
        f(unsafe { self.registration_data_unchecked() })
    }
}

impl<T: drm::Driver> Deref for RegistrationGuard<'_, T> {
    type Target = Device<T, Registered>;

    #[inline]
    fn deref(&self) -> &Self::Target {
        self.dev
    }
}

impl<T: drm::Driver> Drop for RegistrationGuard<'_, T> {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: `self.idx` was returned by a successful `drm_dev_enter()` call, as guaranteed
        // by the type invariants of `RegistrationGuard`.
        unsafe { bindings::drm_dev_exit(self.idx) };
    }
}

impl<T: drm::Driver> Deref for Device<T> {
    type Target = T::Data;

    fn deref(&self) -> &Self::Target {
        &self.data
    }
}

impl<T: drm::Driver> Deref for Device<T, Registered> {
    type Target = Device<T>;

    #[inline]
    fn deref(&self) -> &Self::Target {
        // SAFETY: The caller holds a `Device<T, Registered>`, which guarantees all invariants
        // of the weaker `Normal` context.
        unsafe { self.assume_ctx() }
    }
}

impl<T: drm::Driver> Deref for Device<T, Ioctl> {
    type Target = Device<T>;

    #[inline]
    fn deref(&self) -> &Self::Target {
        // SAFETY: The caller holds a `Device<T, Ioctl>`, which guarantees all invariants
        // of the weaker `Normal` context.
        unsafe { self.assume_ctx() }
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
        // SAFETY: `obj` is a valid pointer to `Self`.
        let drm_dev = unsafe { Self::into_drm_device(obj) };

        // SAFETY: The safety requirements guarantee that the refcount is non-zero.
        unsafe { bindings::drm_dev_put(drm_dev) };
    }
}

impl<T: drm::Driver> AsRef<T::ParentDevice<device::Normal>> for Device<T> {
    fn as_ref(&self) -> &T::ParentDevice<device::Normal> {
        // SAFETY: `bindings::drm_device::dev` is valid as long as the DRM device itself is valid,
        // which is guaranteed by the type invariant.
        let dev = unsafe { device::Device::from_raw((*self.as_raw()).dev) };

        // SAFETY: The DRM device was constructed in `UnregisteredDevice::new()` with a parent
        // device of type `T::ParentDevice`, hence `dev` is contained in a `T::ParentDevice`.
        unsafe { device::AsBusDevice::from_device(dev) }
    }
}

impl<T: drm::Driver> AsRef<T::ParentDevice<device::Bound>> for Device<T, Registered> {
    #[inline]
    fn as_ref(&self) -> &T::ParentDevice<device::Bound> {
        let dev = (**self).as_ref().as_ref();

        // SAFETY: A `Device<T, Registered>` guarantees that the parent device is bound.
        let dev = unsafe { dev.as_bound() };

        // SAFETY: The DRM device was constructed in `UnregisteredDevice::new()` with a parent
        // device of type `T::ParentDevice`, hence `dev` is contained in a `T::ParentDevice`.
        unsafe { device::AsBusDevice::from_device(dev) }
    }
}

// SAFETY: A `drm::Device` can be released from any thread.
unsafe impl<T: drm::Driver, C: DeviceContext> Send for Device<T, C> {}

// SAFETY: A `drm::Device` can be shared among threads because all immutable methods are protected
// by the synchronization in `struct drm_device`.
unsafe impl<T: drm::Driver, C: DeviceContext> Sync for Device<T, C> {}

impl<T: drm::Driver, const ID: u64> WorkItem<ID> for Device<T>
where
    T::Data: WorkItem<ID, Pointer = ARef<Self>>,
    T::Data: HasWork<Self, ID>,
{
    type Pointer = ARef<Self>;

    fn run(ptr: ARef<Self>) {
        T::Data::run(ptr);
    }
}

// SAFETY:
//
// - `raw_get_work` and `work_container_of` return valid pointers by relying on
// `T::Data::raw_get_work` and `container_of`. In particular, `T::Data` is
// stored inline in `drm::Device`, so the `container_of` call is valid.
//
// - The two methods are true inverses of each other: given `ptr: *mut
// Device<T, C>`, `raw_get_work` will return a `*mut Work<Device<T, C>, ID>` through
// `T::Data::raw_get_work` and given a `ptr: *mut Work<Device<T, C>, ID>`,
// `work_container_of` will return a `*mut Device<T, C>` through `container_of`.
unsafe impl<T, C, const ID: u64> HasWork<Self, ID> for Device<T, C>
where
    T: drm::Driver,
    T::Data: HasWork<Self, ID>,
    C: DeviceContext,
{
    unsafe fn raw_get_work(ptr: *mut Self) -> *mut Work<Self, ID> {
        // SAFETY: The caller promises that `ptr` points to a valid `Device<T, C>`.
        let data_ptr = unsafe { &raw mut (*ptr).data };

        // SAFETY: `data_ptr` is a valid pointer to `T::Data`.
        unsafe { T::Data::raw_get_work(data_ptr) }
    }

    unsafe fn work_container_of(ptr: *mut Work<Self, ID>) -> *mut Self {
        // SAFETY: The caller promises that `ptr` points at a `Work` field in
        // `T::Data`.
        let data_ptr = unsafe { T::Data::work_container_of(ptr) };

        // SAFETY: `T::Data` is stored as the `data` field in `Device<T, C>`.
        unsafe { crate::container_of!(data_ptr, Self, data) }
    }
}

// SAFETY: Our `HasWork<T, ID>` implementation returns a `work_struct` that is
// stored in the `work` field of a `delayed_work` with the same access rules as
// the `work_struct` owing to the bound on `T::Data: HasDelayedWork<Device<T, C>,
// ID>`, which requires that `T::Data::raw_get_work` return a `work_struct` that
// is inside a `delayed_work`.
unsafe impl<T, C, const ID: u64> HasDelayedWork<Self, ID> for Device<T, C>
where
    T: drm::Driver,
    T::Data: HasDelayedWork<Self, ID>,
    C: DeviceContext,
{
}
