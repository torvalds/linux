// SPDX-License-Identifier: GPL-2.0

//! Abstractions for the auxiliary bus.
//!
//! C header: [`include/linux/auxiliary_bus.h`](srctree/include/linux/auxiliary_bus.h)

use crate::{
    bindings,
    container_of,
    device,
    device_id::{
        RawDeviceId,
        RawDeviceIdIndex, //
    },

    driver,
    error::{
        from_result,
        to_result, //
    },
    prelude::*,
    types::{
        CovariantForLt,
        ForLt,
        ForeignOwnable,
        Opaque, //
    },
    ThisModule, //
};
use core::{
    any::TypeId,
    marker::PhantomData,
    mem::offset_of,
    pin::Pin,
    ptr::{
        addr_of_mut,
        NonNull, //
    },
};

/// An adapter for the registration of auxiliary drivers.
pub struct Adapter<T: Driver>(T);

// SAFETY:
// - `bindings::auxiliary_driver` is a C type declared as `repr(C)`.
// - `T::Data` is the type of the driver's device private data.
// - `struct auxiliary_driver` embeds a `struct device_driver`.
// - `DEVICE_DRIVER_OFFSET` is the correct byte offset to the embedded `struct device_driver`.
unsafe impl<T: Driver> driver::DriverLayout for Adapter<T> {
    type DriverType = bindings::auxiliary_driver;
    type DriverData<'bound> = T::Data<'bound>;
    const DEVICE_DRIVER_OFFSET: usize = core::mem::offset_of!(Self::DriverType, driver);
}

// SAFETY: A call to `unregister` for a given instance of `DriverType` is guaranteed to be valid if
// a preceding call to `register` has been successful.
unsafe impl<T: Driver> driver::RegistrationOps for Adapter<T> {
    unsafe fn register(
        adrv: &Opaque<Self::DriverType>,
        name: &'static CStr,
        module: &'static ThisModule,
    ) -> Result {
        // SAFETY: It's safe to set the fields of `struct auxiliary_driver` on initialization.
        unsafe {
            (*adrv.get()).name = name.as_char_ptr();
            (*adrv.get()).probe = Some(Self::probe_callback);
            (*adrv.get()).remove = Some(Self::remove_callback);
            (*adrv.get()).id_table = T::ID_TABLE.as_ptr();
        }

        // SAFETY: `adrv` is guaranteed to be a valid `DriverType`.
        to_result(unsafe {
            bindings::__auxiliary_driver_register(adrv.get(), module.0, name.as_char_ptr())
        })
    }

    unsafe fn unregister(adrv: &Opaque<Self::DriverType>) {
        // SAFETY: `adrv` is guaranteed to be a valid `DriverType`.
        unsafe { bindings::auxiliary_driver_unregister(adrv.get()) }
    }
}

impl<T: Driver> Adapter<T> {
    extern "C" fn probe_callback(
        adev: *mut bindings::auxiliary_device,
        id: *const bindings::auxiliary_device_id,
    ) -> c_int {
        // SAFETY: The auxiliary bus only ever calls the probe callback with a valid pointer to a
        // `struct auxiliary_device`.
        //
        // INVARIANT: `adev` is valid for the duration of `probe_callback()`.
        let adev = unsafe { &*adev.cast::<Device<device::CoreInternal<'_>>>() };

        // SAFETY: `DeviceId` is a `#[repr(transparent)`] wrapper of `struct auxiliary_device_id`
        // and does not add additional invariants, so it's safe to transmute.
        let id = unsafe { &*id.cast::<DeviceId>() };

        // SAFETY: `id` comes from `T::ID_TABLE` which is of type `IdArray<_, T::IdInfo>`.
        let info = unsafe { id.info_unchecked::<T::IdInfo>() };

        from_result(|| {
            let data = T::probe(adev, info);

            adev.as_ref().set_drvdata(data)?;
            Ok(0)
        })
    }

    extern "C" fn remove_callback(adev: *mut bindings::auxiliary_device) {
        // SAFETY: The auxiliary bus only ever calls the probe callback with a valid pointer to a
        // `struct auxiliary_device`.
        //
        // INVARIANT: `adev` is valid for the duration of `remove_callback()`.
        let adev = unsafe { &*adev.cast::<Device<device::CoreInternal<'_>>>() };

        // SAFETY: `remove_callback` is only ever called after a successful call to
        // `probe_callback`, hence it's guaranteed that `Device::set_drvdata()` has been called
        // and stored a `Pin<KBox<T::Data<'_>>>`.
        let data = unsafe { adev.as_ref().drvdata_borrow::<T::Data<'_>>() };

        T::unbind(adev, data);
    }
}

/// Declares a kernel module that exposes a single auxiliary driver.
#[macro_export]
macro_rules! module_auxiliary_driver {
    ($($f:tt)*) => {
        $crate::module_driver!(<T>, $crate::auxiliary::Adapter<T>, { $($f)* });
    };
}

/// Abstraction for `bindings::auxiliary_device_id`.
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct DeviceId(bindings::auxiliary_device_id);

impl DeviceId {
    /// Create a new [`DeviceId`] from name.
    pub const fn new(modname: &'static CStr, name: &'static CStr) -> Self {
        let name = name.to_bytes_with_nul();
        let modname = modname.to_bytes_with_nul();

        let mut id: bindings::auxiliary_device_id = pin_init::zeroed();
        let mut i = 0;
        while i < modname.len() {
            id.name[i] = modname[i];
            i += 1;
        }

        // Reuse the space of the NULL terminator.
        id.name[i - 1] = b'.';

        let mut j = 0;
        while j < name.len() {
            id.name[i] = name[j];
            i += 1;
            j += 1;
        }

        Self(id)
    }
}

// SAFETY: `DeviceId` is a `#[repr(transparent)]` wrapper of `auxiliary_device_id` and does not add
// additional invariants, so it's safe to transmute to `RawType`.
unsafe impl RawDeviceId for DeviceId {
    type RawType = bindings::auxiliary_device_id;
}

// SAFETY: `DRIVER_DATA_OFFSET` is the offset to the `driver_data` field.
unsafe impl RawDeviceIdIndex for DeviceId {
    const DRIVER_DATA_OFFSET: usize =
        core::mem::offset_of!(bindings::auxiliary_device_id, driver_data);
}

/// IdTable type for auxiliary drivers.
pub type IdTable<T> = &'static dyn kernel::device_id::IdTable<DeviceId, T>;

/// Create a auxiliary `IdTable` with its alias for modpost.
#[macro_export]
macro_rules! auxiliary_device_table {
    ($($tt:tt)*) => {
        $crate::module_device_table!("auxiliary", $crate::auxiliary::DeviceId, $($tt)*);
    };
}

/// The auxiliary driver trait.
///
/// Drivers must implement this trait in order to get an auxiliary driver registered.
pub trait Driver {
    /// The type holding information about each device id supported by the driver.
    ///
    /// TODO: Use associated_type_defaults once stabilized:
    ///
    /// type IdInfo: 'static = ();
    type IdInfo: 'static;

    /// The type of the driver's bus device private data.
    type Data<'bound>: Send + 'bound;

    /// The table of device ids supported by the driver.
    const ID_TABLE: IdTable<Self::IdInfo>;

    /// Auxiliary driver probe.
    ///
    /// Called when an auxiliary device is matches a corresponding driver.
    fn probe<'bound>(
        dev: &'bound Device<device::Core<'_>>,
        id_info: &'bound Self::IdInfo,
    ) -> impl PinInit<Self::Data<'bound>, Error> + 'bound;

    /// Auxiliary driver unbind.
    ///
    /// Called when a [`Device`] is unbound from its bound [`Driver`]. Implementing this callback
    /// is optional.
    ///
    /// This callback serves as a place for drivers to perform teardown operations that require a
    /// `&Device<Core>` or `&Device<Bound>` reference. For instance, drivers may try to perform I/O
    /// operations to gracefully tear down the device.
    ///
    /// Otherwise, release operations for driver resources should be performed in `Drop`.
    fn unbind<'bound>(dev: &'bound Device<device::Core<'_>>, this: Pin<&Self::Data<'bound>>) {
        let _ = (dev, this);
    }
}

/// The auxiliary device representation.
///
/// This structure represents the Rust abstraction for a C `struct auxiliary_device`. The
/// implementation abstracts the usage of an already existing C `struct auxiliary_device` within
/// Rust code that we get passed from the C side.
///
/// # Invariants
///
/// A [`Device`] instance represents a valid `struct auxiliary_device` created by the C portion of
/// the kernel.
#[repr(transparent)]
pub struct Device<Ctx: device::DeviceContext = device::Normal>(
    Opaque<bindings::auxiliary_device>,
    PhantomData<Ctx>,
);

impl<Ctx: device::DeviceContext> Device<Ctx> {
    fn as_raw(&self) -> *mut bindings::auxiliary_device {
        self.0.get()
    }

    /// Returns the auxiliary device' id.
    pub fn id(&self) -> u32 {
        // SAFETY: By the type invariant `self.as_raw()` is a valid pointer to a
        // `struct auxiliary_device`.
        unsafe { (*self.as_raw()).id }
    }
}

impl Device<device::Bound> {
    /// Returns a bound reference to the parent [`device::Device`].
    pub fn parent(&self) -> &device::Device<device::Bound> {
        let parent = (**self).parent();

        // SAFETY: A bound auxiliary device always has a bound parent device.
        unsafe { parent.as_bound() }
    }

    /// Returns the stored registration data as a pinned reference.
    ///
    /// Performs null and [`TypeId`] checks, then borrows the stored [`KBox`].
    ///
    /// # Safety
    ///
    /// Callers must ensure that the lifetime shortening from the original `'static` storage to
    /// `'_` is sound, e.g. via an HRTB closure or [`CovariantForLt`] guarantee.
    unsafe fn registration_data_pinned<F: ForLt + 'static>(&self) -> Result<Pin<&F::Of<'_>>> {
        // SAFETY: By the type invariant, `self.as_raw()` is a valid `struct auxiliary_device`.
        let ptr = unsafe { (*self.as_raw()).registration_data_rust };
        if ptr.is_null() {
            dev_warn!(
                self.as_ref(),
                "No registration data set; parent is not a Rust driver.\n"
            );
            return Err(ENOENT);
        }

        // SAFETY: `ptr` is non-null and was set via `into_foreign()` in `Registration::new()`;
        // `RegistrationData` is `#[repr(C)]` with `type_id` at offset 0, so reading a `TypeId`
        // at the start of the allocation is valid regardless of `F`.
        let type_id = unsafe { ptr.cast::<TypeId>().read() };
        if type_id != TypeId::of::<F>() {
            return Err(EINVAL);
        }

        // SAFETY: The `TypeId` check above confirms that the stored type matches `F`'s
        // encoding; lifetimes are erased at runtime, so borrowing as `F::Of<'_>` is
        // layout-compatible with the stored `F::Of<'static>`. `ptr` remains valid until
        // `Registration::drop()` calls `from_foreign()`.
        let wrapper = unsafe { Pin::<KBox<RegistrationData<F::Of<'_>>>>::borrow(ptr) };

        // SAFETY: `data` is a structurally pinned field of `RegistrationData`.
        Ok(unsafe { wrapper.map_unchecked(|w| &w.data) })
    }

    /// Access the registration data set by the registering (parent) driver through a closure.
    ///
    /// `F` is the [`ForLt`](trait@ForLt) encoding of the data type. The closure receives a pinned
    /// reference to the registration data.
    ///
    /// For covariant types that implement [`trait@CovariantForLt`], prefer
    /// [`registration_data`](Self::registration_data) which returns a direct reference.
    ///
    /// Returns [`EINVAL`] if `F` does not match the type used by the parent driver when calling
    /// [`Registration::new()`].
    ///
    /// Returns [`ENOENT`] if no registration data has been set, e.g. when the device was
    /// registered by a C driver.
    #[inline]
    pub fn registration_data_with<F: ForLt + 'static, R>(
        &self,
        f: impl for<'a> FnOnce(Pin<&'a F::Of<'a>>) -> R,
    ) -> Result<R> {
        // SAFETY: The HRTB closure prevents the caller from smuggling in references with a
        // concrete short lifetime, making the round-trip from `'static` sound regardless of
        // variance.
        let pinned = unsafe { self.registration_data_pinned::<F>()? };

        Ok(f(pinned))
    }

    /// Returns a pinned reference to the registration data set by the registering (parent) driver.
    ///
    /// This method is only available when `F` implements [`trait@CovariantForLt`], which guarantees
    /// that the lifetime shortening is sound.
    ///
    /// For non-covariant types, use the closure-based [`Self::registration_data_with`].
    ///
    /// Returns [`EINVAL`] if `F` does not match the type used by the parent driver when calling
    /// [`Registration::new()`].
    ///
    /// Returns [`ENOENT`] if no registration data has been set, e.g. when the device was
    /// registered by a C driver.
    #[inline]
    pub fn registration_data<F: CovariantForLt + 'static>(&self) -> Result<Pin<&F::Of<'_>>> {
        // SAFETY: `CovariantForLt` guarantees covariance, which makes the lifetime shortening
        // from `'static` to `'_` performed by `registration_data_pinned` sound.
        unsafe { self.registration_data_pinned::<F>() }
    }
}

impl Device {
    /// Returns a reference to the parent [`device::Device`].
    pub fn parent(&self) -> &device::Device {
        // SAFETY: A `struct auxiliary_device` always has a parent.
        unsafe { self.as_ref().parent().unwrap_unchecked() }
    }

    extern "C" fn release(dev: *mut bindings::device) {
        // SAFETY: By the type invariant `self.0.as_raw` is a pointer to the `struct device`
        // embedded in `struct auxiliary_device`.
        let adev = unsafe { container_of!(dev, bindings::auxiliary_device, dev) };

        // SAFETY: `adev` points to the memory that has been allocated in `Registration::new`, via
        // `KBox::new(Opaque::<bindings::auxiliary_device>::zeroed(), GFP_KERNEL)`.
        let _ = unsafe { KBox::<Opaque<bindings::auxiliary_device>>::from_raw(adev.cast()) };
    }
}

// SAFETY: `auxiliary::Device` is a transparent wrapper of `struct auxiliary_device`.
// The offset is guaranteed to point to a valid device field inside `auxiliary::Device`.
unsafe impl<Ctx: device::DeviceContext> device::AsBusDevice<Ctx> for Device<Ctx> {
    const OFFSET: usize = offset_of!(bindings::auxiliary_device, dev);
}

// SAFETY: `Device` is a transparent wrapper of a type that doesn't depend on `Device`'s generic
// argument.
kernel::impl_device_context_deref!(unsafe { Device });
kernel::impl_device_context_into_aref!(Device);

// SAFETY: Instances of `Device` are always reference-counted.
unsafe impl crate::sync::aref::AlwaysRefCounted for Device {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference guarantees that the refcount is non-zero.
        unsafe { bindings::get_device(self.as_ref().as_raw()) };
    }

    unsafe fn dec_ref(obj: NonNull<Self>) {
        // CAST: `Self` a transparent wrapper of `bindings::auxiliary_device`.
        let adev: *mut bindings::auxiliary_device = obj.cast().as_ptr();

        // SAFETY: By the type invariant of `Self`, `adev` is a pointer to a valid
        // `struct auxiliary_device`.
        let dev = unsafe { addr_of_mut!((*adev).dev) };

        // SAFETY: The safety requirements guarantee that the refcount is non-zero.
        unsafe { bindings::put_device(dev) }
    }
}

impl<Ctx: device::DeviceContext> AsRef<device::Device<Ctx>> for Device<Ctx> {
    fn as_ref(&self) -> &device::Device<Ctx> {
        // SAFETY: By the type invariant of `Self`, `self.as_raw()` is a pointer to a valid
        // `struct auxiliary_device`.
        let dev = unsafe { addr_of_mut!((*self.as_raw()).dev) };

        // SAFETY: `dev` points to a valid `struct device`.
        unsafe { device::Device::from_raw(dev) }
    }
}

// SAFETY: A `Device` is always reference-counted and can be released from any thread.
unsafe impl Send for Device {}

// SAFETY: `Device` can be shared among threads because all methods of `Device`
// (i.e. `Device<Normal>) are thread safe.
unsafe impl Sync for Device {}

// SAFETY: Same as `Device<Normal>` -- the underlying `struct auxiliary_device` is the same;
// `Bound` is a zero-sized type-state marker that does not affect thread safety.
unsafe impl Sync for Device<device::Bound> {}

/// Wrapper that stores a [`TypeId`] alongside the registration data for runtime type checking.
#[repr(C)]
#[pin_data]
struct RegistrationData<T> {
    type_id: TypeId,
    #[pin]
    data: T,
}

/// The registration of an auxiliary device.
///
/// This type represents the registration of a [`struct auxiliary_device`]. When its parent device
/// is unbound, the corresponding auxiliary device will be unregistered from the system.
///
/// The type parameter `F` is a [`ForLt`](trait@ForLt) encoding of the registration
/// data type. For non-lifetime-parameterized types, use [`ForLt!(T)`](macro@ForLt).
///
/// The data can be accessed by the auxiliary driver through [`Device::registration_data()`] and
/// [`Device::registration_data_with()`].
///
/// # Invariants
///
/// `self.adev` always holds a valid pointer to an initialized and registered
/// [`struct auxiliary_device`] whose `registration_data_rust` field points to a
/// valid `Pin<KBox<RegistrationData<F::Of<'static>>>>`.
pub struct Registration<'a, F: ForLt + 'static> {
    adev: NonNull<bindings::auxiliary_device>,
    _phantom: PhantomData<F::Of<'a>>,
}

impl<'a, F: ForLt> Registration<'a, F>
where
    for<'b> F::Of<'b>: Send + Sync,
{
    /// Create and register a new auxiliary device with the given registration data.
    ///
    /// The `data` is owned by the registration and can be accessed through the auxiliary device
    /// via [`Device::registration_data()`].
    ///
    /// # Safety
    ///
    /// The caller must not `mem::forget()` the returned [`Registration`] or otherwise prevent its
    /// [`Drop`] implementation from running, since the registration data may contain borrowed
    /// references that become invalid after `'a` ends.
    ///
    /// If the registration data is `'static`, use the safe [`Registration::new()`] instead.
    pub unsafe fn new_with_lt<E>(
        parent: &'a device::Device<device::Bound>,
        name: &CStr,
        id: u32,
        modname: &CStr,
        data: impl PinInit<F::Of<'a>, E>,
    ) -> Result<Self>
    where
        Error: From<E>,
    {
        let data = KBox::pin_init::<Error>(
            try_pin_init!(RegistrationData {
                type_id: TypeId::of::<F>(),
                data <- data,
            }),
            GFP_KERNEL,
        )?;

        // SAFETY: `'a` is invariant (via `Registration`'s `PhantomData`). Lifetimes do not
        // affect layout, so RegistrationData<F::Of<'a>> and RegistrationData<F::Of<'static>>
        // have identical representation.
        let data: Pin<KBox<RegistrationData<F::Of<'static>>>> =
            unsafe { core::mem::transmute(data) };

        let boxed: KBox<Opaque<bindings::auxiliary_device>> = KBox::zeroed(GFP_KERNEL)?;
        let adev = boxed.get();

        // SAFETY: It's safe to set the fields of `struct auxiliary_device` on initialization.
        unsafe {
            (*adev).dev.parent = parent.as_raw();
            (*adev).dev.release = Some(Device::release);
            (*adev).name = name.as_char_ptr();
            (*adev).id = id;
            (*adev).registration_data_rust = data.into_foreign();
        }

        // SAFETY: `adev` is guaranteed to be a valid pointer to a `struct auxiliary_device`,
        // which has not been initialized yet.
        unsafe { bindings::auxiliary_device_init(adev) };

        // Now that `adev` is initialized, leak the `Box`; the corresponding memory will be
        // freed by `Device::release` when the last reference to the `struct auxiliary_device`
        // is dropped.
        let _ = KBox::into_raw(boxed);

        // SAFETY:
        // - `adev` is guaranteed to be a valid pointer to a `struct auxiliary_device`, which
        //   has been initialized,
        // - `modname.as_char_ptr()` is a NULL terminated string.
        let ret = unsafe { bindings::__auxiliary_device_add(adev, modname.as_char_ptr()) };
        if ret != 0 {
            // SAFETY: `registration_data` was set above via `into_foreign()`.
            drop(unsafe {
                Pin::<KBox<RegistrationData<F::Of<'static>>>>::from_foreign(
                    (*adev).registration_data_rust,
                )
            });

            // SAFETY: `adev` is guaranteed to be a valid pointer to a
            // `struct auxiliary_device`, which has been initialized.
            unsafe { bindings::auxiliary_device_uninit(adev) };

            return Err(Error::from_errno(ret));
        }

        // INVARIANT: The device will remain registered until `auxiliary_device_delete()` is
        // called, which happens in `Self::drop()`.
        Ok(Self {
            // SAFETY: `adev` is guaranteed to be non-null, since the `KBox` was allocated
            // successfully.
            adev: unsafe { NonNull::new_unchecked(adev) },
            _phantom: PhantomData,
        })
    }

    /// Create and register a new auxiliary device with `'static` registration data.
    ///
    /// Safe variant of [`Registration::new_with_lt()`] for registration data that does not contain
    /// borrowed references.
    pub fn new<E>(
        parent: &'a device::Device<device::Bound>,
        name: &CStr,
        id: u32,
        modname: &CStr,
        data: impl PinInit<F::Of<'a>, E>,
    ) -> Result<Self>
    where
        F::Of<'a>: 'static,
        Error: From<E>,
    {
        // SAFETY: `F::Of<'a>: 'static` guarantees the data contains no borrowed references,
        // so forgetting the `Registration` cannot cause use-after-free.
        unsafe { Self::new_with_lt(parent, name, id, modname, data) }
    }
}

impl<F: ForLt> Drop for Registration<'_, F> {
    fn drop(&mut self) {
        // SAFETY: By the type invariant of `Self`, `self.adev.as_ptr()` is a valid registered
        // `struct auxiliary_device`.
        unsafe { bindings::auxiliary_device_delete(self.adev.as_ptr()) };

        // SAFETY: `registration_data` was set in `new()` via `into_foreign()`.
        drop(unsafe {
            Pin::<KBox<RegistrationData<F::Of<'static>>>>::from_foreign(
                (*self.adev.as_ptr()).registration_data_rust,
            )
        });

        // This drops the reference we acquired through `auxiliary_device_init()`.
        //
        // SAFETY: By the type invariant of `Self`, `self.adev.as_ptr()` is a valid registered
        // `struct auxiliary_device`.
        unsafe { bindings::auxiliary_device_uninit(self.adev.as_ptr()) };
    }
}

// SAFETY: A `Registration` of a `struct auxiliary_device` can be released from any thread.
unsafe impl<F: ForLt> Send for Registration<'_, F> where for<'a> F::Of<'a>: Send {}

// SAFETY: `Registration` does not expose any methods or fields that need synchronization.
unsafe impl<F: ForLt> Sync for Registration<'_, F> where for<'a> F::Of<'a>: Send {}
