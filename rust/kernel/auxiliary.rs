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
    devres::Devres,
    driver,
    error::{
        from_result,
        to_result, //
    },
    prelude::*,
    types::Opaque,
    ThisModule, //
};
use core::{
    marker::PhantomData,
    mem::offset_of,
    ptr::{
        addr_of_mut,
        NonNull, //
    },
};

/// An adapter for the registration of auxiliary drivers.
pub struct Adapter<T: Driver>(T);

// SAFETY:
// - `bindings::auxiliary_driver` is a C type declared as `repr(C)`.
// - `T` is the type of the driver's device private data.
// - `struct auxiliary_driver` embeds a `struct device_driver`.
// - `DEVICE_DRIVER_OFFSET` is the correct byte offset to the embedded `struct device_driver`.
unsafe impl<T: Driver + 'static> driver::DriverLayout for Adapter<T> {
    type DriverType = bindings::auxiliary_driver;
    type DriverData = T;
    const DEVICE_DRIVER_OFFSET: usize = core::mem::offset_of!(Self::DriverType, driver);
}

// SAFETY: A call to `unregister` for a given instance of `DriverType` is guaranteed to be valid if
// a preceding call to `register` has been successful.
unsafe impl<T: Driver + 'static> driver::RegistrationOps for Adapter<T> {
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

impl<T: Driver + 'static> Adapter<T> {
    extern "C" fn probe_callback(
        adev: *mut bindings::auxiliary_device,
        id: *const bindings::auxiliary_device_id,
    ) -> c_int {
        // SAFETY: The auxiliary bus only ever calls the probe callback with a valid pointer to a
        // `struct auxiliary_device`.
        //
        // INVARIANT: `adev` is valid for the duration of `probe_callback()`.
        let adev = unsafe { &*adev.cast::<Device<device::CoreInternal>>() };

        // SAFETY: `DeviceId` is a `#[repr(transparent)`] wrapper of `struct auxiliary_device_id`
        // and does not add additional invariants, so it's safe to transmute.
        let id = unsafe { &*id.cast::<DeviceId>() };
        let info = T::ID_TABLE.info(id.index());

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
        let adev = unsafe { &*adev.cast::<Device<device::CoreInternal>>() };

        // SAFETY: `remove_callback` is only ever called after a successful call to
        // `probe_callback`, hence it's guaranteed that `Device::set_drvdata()` has been called
        // and stored a `Pin<KBox<T>>`.
        let data = unsafe { adev.as_ref().drvdata_borrow::<T>() };

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

    fn index(&self) -> usize {
        self.0.driver_data
    }
}

/// IdTable type for auxiliary drivers.
pub type IdTable<T> = &'static dyn kernel::device_id::IdTable<DeviceId, T>;

/// Create a auxiliary `IdTable` with its alias for modpost.
#[macro_export]
macro_rules! auxiliary_device_table {
    ($table_name:ident, $module_table_name:ident, $id_info_type: ty, $table_data: expr) => {
        const $table_name: $crate::device_id::IdArray<
            $crate::auxiliary::DeviceId,
            $id_info_type,
            { $table_data.len() },
        > = $crate::device_id::IdArray::new($table_data);

        $crate::module_device_table!("auxiliary", $module_table_name, $table_name);
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

    /// The table of device ids supported by the driver.
    const ID_TABLE: IdTable<Self::IdInfo>;

    /// Auxiliary driver probe.
    ///
    /// Called when an auxiliary device is matches a corresponding driver.
    fn probe(dev: &Device<device::Core>, id_info: &Self::IdInfo) -> impl PinInit<Self, Error>;

    /// Auxiliary driver unbind.
    ///
    /// Called when a [`Device`] is unbound from its bound [`Driver`]. Implementing this callback
    /// is optional.
    ///
    /// This callback serves as a place for drivers to perform teardown operations that require a
    /// `&Device<Core>` or `&Device<Bound>` reference. For instance, drivers may try to perform I/O
    /// operations to gracefully tear down the device.
    ///
    /// Otherwise, release operations for driver resources should be performed in `Self::drop`.
    fn unbind(dev: &Device<device::Core>, this: Pin<&Self>) {
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

/// The registration of an auxiliary device.
///
/// This type represents the registration of a [`struct auxiliary_device`]. When its parent device
/// is unbound, the corresponding auxiliary device will be unregistered from the system.
///
/// # Invariants
///
/// `self.0` always holds a valid pointer to an initialized and registered
/// [`struct auxiliary_device`].
pub struct Registration(NonNull<bindings::auxiliary_device>);

impl Registration {
    /// Create and register a new auxiliary device.
    pub fn new<'a>(
        parent: &'a device::Device<device::Bound>,
        name: &'a CStr,
        id: u32,
        modname: &'a CStr,
    ) -> impl PinInit<Devres<Self>, Error> + 'a {
        pin_init::pin_init_scope(move || {
            let boxed = KBox::new(Opaque::<bindings::auxiliary_device>::zeroed(), GFP_KERNEL)?;
            let adev = boxed.get();

            // SAFETY: It's safe to set the fields of `struct auxiliary_device` on initialization.
            unsafe {
                (*adev).dev.parent = parent.as_raw();
                (*adev).dev.release = Some(Device::release);
                (*adev).name = name.as_char_ptr();
                (*adev).id = id;
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
                // SAFETY: `adev` is guaranteed to be a valid pointer to a
                // `struct auxiliary_device`, which has been initialized.
                unsafe { bindings::auxiliary_device_uninit(adev) };

                return Err(Error::from_errno(ret));
            }

            // INVARIANT: The device will remain registered until `auxiliary_device_delete()` is
            // called, which happens in `Self::drop()`.
            Ok(Devres::new(
                parent,
                // SAFETY: `adev` is guaranteed to be non-null, since the `KBox` was allocated
                // successfully.
                Self(unsafe { NonNull::new_unchecked(adev) }),
            ))
        })
    }
}

impl Drop for Registration {
    fn drop(&mut self) {
        // SAFETY: By the type invariant of `Self`, `self.0.as_ptr()` is a valid registered
        // `struct auxiliary_device`.
        unsafe { bindings::auxiliary_device_delete(self.0.as_ptr()) };

        // This drops the reference we acquired through `auxiliary_device_init()`.
        //
        // SAFETY: By the type invariant of `Self`, `self.0.as_ptr()` is a valid registered
        // `struct auxiliary_device`.
        unsafe { bindings::auxiliary_device_uninit(self.0.as_ptr()) };
    }
}

// SAFETY: A `Registration` of a `struct auxiliary_device` can be released from any thread.
unsafe impl Send for Registration {}

// SAFETY: `Registration` does not expose any methods or fields that need synchronization.
unsafe impl Sync for Registration {}
