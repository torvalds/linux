// SPDX-License-Identifier: GPL-2.0

//! Abstractions for the platform bus.
//!
//! C header: [`include/linux/platform_device.h`](srctree/include/linux/platform_device.h)

use crate::{
    bindings, container_of, device, driver,
    error::{to_result, Result},
    of,
    prelude::*,
    str::CStr,
    types::{ForeignOwnable, Opaque},
    ThisModule,
};

use core::{
    marker::PhantomData,
    ptr::{addr_of_mut, NonNull},
};

/// An adapter for the registration of platform drivers.
pub struct Adapter<T: Driver>(T);

// SAFETY: A call to `unregister` for a given instance of `RegType` is guaranteed to be valid if
// a preceding call to `register` has been successful.
unsafe impl<T: Driver + 'static> driver::RegistrationOps for Adapter<T> {
    type RegType = bindings::platform_driver;

    unsafe fn register(
        pdrv: &Opaque<Self::RegType>,
        name: &'static CStr,
        module: &'static ThisModule,
    ) -> Result {
        let of_table = match T::OF_ID_TABLE {
            Some(table) => table.as_ptr(),
            None => core::ptr::null(),
        };

        // SAFETY: It's safe to set the fields of `struct platform_driver` on initialization.
        unsafe {
            (*pdrv.get()).driver.name = name.as_char_ptr();
            (*pdrv.get()).probe = Some(Self::probe_callback);
            (*pdrv.get()).remove = Some(Self::remove_callback);
            (*pdrv.get()).driver.of_match_table = of_table;
        }

        // SAFETY: `pdrv` is guaranteed to be a valid `RegType`.
        to_result(unsafe { bindings::__platform_driver_register(pdrv.get(), module.0) })
    }

    unsafe fn unregister(pdrv: &Opaque<Self::RegType>) {
        // SAFETY: `pdrv` is guaranteed to be a valid `RegType`.
        unsafe { bindings::platform_driver_unregister(pdrv.get()) };
    }
}

impl<T: Driver + 'static> Adapter<T> {
    extern "C" fn probe_callback(pdev: *mut bindings::platform_device) -> kernel::ffi::c_int {
        // SAFETY: The platform bus only ever calls the probe callback with a valid pointer to a
        // `struct platform_device`.
        //
        // INVARIANT: `pdev` is valid for the duration of `probe_callback()`.
        let pdev = unsafe { &*pdev.cast::<Device<device::Core>>() };

        let info = <Self as driver::Adapter>::id_info(pdev.as_ref());
        match T::probe(pdev, info) {
            Ok(data) => {
                // Let the `struct platform_device` own a reference of the driver's private data.
                // SAFETY: By the type invariant `pdev.as_raw` returns a valid pointer to a
                // `struct platform_device`.
                unsafe { bindings::platform_set_drvdata(pdev.as_raw(), data.into_foreign() as _) };
            }
            Err(err) => return Error::to_errno(err),
        }

        0
    }

    extern "C" fn remove_callback(pdev: *mut bindings::platform_device) {
        // SAFETY: `pdev` is a valid pointer to a `struct platform_device`.
        let ptr = unsafe { bindings::platform_get_drvdata(pdev) };

        // SAFETY: `remove_callback` is only ever called after a successful call to
        // `probe_callback`, hence it's guaranteed that `ptr` points to a valid and initialized
        // `KBox<T>` pointer created through `KBox::into_foreign`.
        let _ = unsafe { KBox::<T>::from_foreign(ptr) };
    }
}

impl<T: Driver + 'static> driver::Adapter for Adapter<T> {
    type IdInfo = T::IdInfo;

    fn of_id_table() -> Option<of::IdTable<Self::IdInfo>> {
        T::OF_ID_TABLE
    }
}

/// Declares a kernel module that exposes a single platform driver.
///
/// # Examples
///
/// ```ignore
/// kernel::module_platform_driver! {
///     type: MyDriver,
///     name: "Module name",
///     authors: ["Author name"],
///     description: "Description",
///     license: "GPL v2",
/// }
/// ```
#[macro_export]
macro_rules! module_platform_driver {
    ($($f:tt)*) => {
        $crate::module_driver!(<T>, $crate::platform::Adapter<T>, { $($f)* });
    };
}

/// The platform driver trait.
///
/// Drivers must implement this trait in order to get a platform driver registered.
///
/// # Example
///
///```
/// # use kernel::{bindings, c_str, device::Core, of, platform};
///
/// struct MyDriver;
///
/// kernel::of_device_table!(
///     OF_TABLE,
///     MODULE_OF_TABLE,
///     <MyDriver as platform::Driver>::IdInfo,
///     [
///         (of::DeviceId::new(c_str!("test,device")), ())
///     ]
/// );
///
/// impl platform::Driver for MyDriver {
///     type IdInfo = ();
///     const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);
///
///     fn probe(
///         _pdev: &platform::Device<Core>,
///         _id_info: Option<&Self::IdInfo>,
///     ) -> Result<Pin<KBox<Self>>> {
///         Err(ENODEV)
///     }
/// }
///```
pub trait Driver: Send {
    /// The type holding driver private data about each device id supported by the driver.
    ///
    /// TODO: Use associated_type_defaults once stabilized:
    ///
    /// type IdInfo: 'static = ();
    type IdInfo: 'static;

    /// The table of OF device ids supported by the driver.
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>>;

    /// Platform driver probe.
    ///
    /// Called when a new platform device is added or discovered.
    /// Implementers should attempt to initialize the device here.
    fn probe(dev: &Device<device::Core>, id_info: Option<&Self::IdInfo>)
        -> Result<Pin<KBox<Self>>>;
}

/// The platform device representation.
///
/// This structure represents the Rust abstraction for a C `struct platform_device`. The
/// implementation abstracts the usage of an already existing C `struct platform_device` within Rust
/// code that we get passed from the C side.
///
/// # Invariants
///
/// A [`Device`] instance represents a valid `struct platform_device` created by the C portion of
/// the kernel.
#[repr(transparent)]
pub struct Device<Ctx: device::DeviceContext = device::Normal>(
    Opaque<bindings::platform_device>,
    PhantomData<Ctx>,
);

impl<Ctx: device::DeviceContext> Device<Ctx> {
    fn as_raw(&self) -> *mut bindings::platform_device {
        self.0.get()
    }
}

// SAFETY: `Device` is a transparent wrapper of a type that doesn't depend on `Device`'s generic
// argument.
kernel::impl_device_context_deref!(unsafe { Device });
kernel::impl_device_context_into_aref!(Device);

// SAFETY: Instances of `Device` are always reference-counted.
unsafe impl crate::types::AlwaysRefCounted for Device {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference guarantees that the refcount is non-zero.
        unsafe { bindings::get_device(self.as_ref().as_raw()) };
    }

    unsafe fn dec_ref(obj: NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is non-zero.
        unsafe { bindings::platform_device_put(obj.cast().as_ptr()) }
    }
}

impl<Ctx: device::DeviceContext> AsRef<device::Device<Ctx>> for Device<Ctx> {
    fn as_ref(&self) -> &device::Device<Ctx> {
        // SAFETY: By the type invariant of `Self`, `self.as_raw()` is a pointer to a valid
        // `struct platform_device`.
        let dev = unsafe { addr_of_mut!((*self.as_raw()).dev) };

        // SAFETY: `dev` points to a valid `struct device`.
        unsafe { device::Device::as_ref(dev) }
    }
}

impl<Ctx: device::DeviceContext> TryFrom<&device::Device<Ctx>> for &Device<Ctx> {
    type Error = kernel::error::Error;

    fn try_from(dev: &device::Device<Ctx>) -> Result<Self, Self::Error> {
        // SAFETY: By the type invariant of `Device`, `dev.as_raw()` is a valid pointer to a
        // `struct device`.
        if !unsafe { bindings::dev_is_platform(dev.as_raw()) } {
            return Err(EINVAL);
        }

        // SAFETY: We've just verified that the bus type of `dev` equals
        // `bindings::platform_bus_type`, hence `dev` must be embedded in a valid
        // `struct platform_device` as guaranteed by the corresponding C code.
        let pdev = unsafe { container_of!(dev.as_raw(), bindings::platform_device, dev) };

        // SAFETY: `pdev` is a valid pointer to a `struct platform_device`.
        Ok(unsafe { &*pdev.cast() })
    }
}

// SAFETY: A `Device` is always reference-counted and can be released from any thread.
unsafe impl Send for Device {}

// SAFETY: `Device` can be shared among threads because all methods of `Device`
// (i.e. `Device<Normal>) are thread safe.
unsafe impl Sync for Device {}
