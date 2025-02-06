// SPDX-License-Identifier: GPL-2.0

//! Generic support for drivers of different buses (e.g., PCI, Platform, Amba, etc.).
//!
//! Each bus / subsystem is expected to implement [`RegistrationOps`], which allows drivers to
//! register using the [`Registration`] class.

use crate::error::{Error, Result};
use crate::{device, init::PinInit, of, str::CStr, try_pin_init, types::Opaque, ThisModule};
use core::pin::Pin;
use macros::{pin_data, pinned_drop};

/// The [`RegistrationOps`] trait serves as generic interface for subsystems (e.g., PCI, Platform,
/// Amba, etc.) to provide the corresponding subsystem specific implementation to register /
/// unregister a driver of the particular type (`RegType`).
///
/// For instance, the PCI subsystem would set `RegType` to `bindings::pci_driver` and call
/// `bindings::__pci_register_driver` from `RegistrationOps::register` and
/// `bindings::pci_unregister_driver` from `RegistrationOps::unregister`.
///
/// # Safety
///
/// A call to [`RegistrationOps::unregister`] for a given instance of `RegType` is only valid if a
/// preceding call to [`RegistrationOps::register`] has been successful.
pub unsafe trait RegistrationOps {
    /// The type that holds information about the registration. This is typically a struct defined
    /// by the C portion of the kernel.
    type RegType: Default;

    /// Registers a driver.
    ///
    /// # Safety
    ///
    /// On success, `reg` must remain pinned and valid until the matching call to
    /// [`RegistrationOps::unregister`].
    unsafe fn register(
        reg: &Opaque<Self::RegType>,
        name: &'static CStr,
        module: &'static ThisModule,
    ) -> Result;

    /// Unregisters a driver previously registered with [`RegistrationOps::register`].
    ///
    /// # Safety
    ///
    /// Must only be called after a preceding successful call to [`RegistrationOps::register`] for
    /// the same `reg`.
    unsafe fn unregister(reg: &Opaque<Self::RegType>);
}

/// A [`Registration`] is a generic type that represents the registration of some driver type (e.g.
/// `bindings::pci_driver`). Therefore a [`Registration`] must be initialized with a type that
/// implements the [`RegistrationOps`] trait, such that the generic `T::register` and
/// `T::unregister` calls result in the subsystem specific registration calls.
///
///Once the `Registration` structure is dropped, the driver is unregistered.
#[pin_data(PinnedDrop)]
pub struct Registration<T: RegistrationOps> {
    #[pin]
    reg: Opaque<T::RegType>,
}

// SAFETY: `Registration` has no fields or methods accessible via `&Registration`, so it is safe to
// share references to it with multiple threads as nothing can be done.
unsafe impl<T: RegistrationOps> Sync for Registration<T> {}

// SAFETY: Both registration and unregistration are implemented in C and safe to be performed from
// any thread, so `Registration` is `Send`.
unsafe impl<T: RegistrationOps> Send for Registration<T> {}

impl<T: RegistrationOps> Registration<T> {
    /// Creates a new instance of the registration object.
    pub fn new(name: &'static CStr, module: &'static ThisModule) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            reg <- Opaque::try_ffi_init(|ptr: *mut T::RegType| {
                // SAFETY: `try_ffi_init` guarantees that `ptr` is valid for write.
                unsafe { ptr.write(T::RegType::default()) };

                // SAFETY: `try_ffi_init` guarantees that `ptr` is valid for write, and it has
                // just been initialised above, so it's also valid for read.
                let drv = unsafe { &*(ptr as *const Opaque<T::RegType>) };

                // SAFETY: `drv` is guaranteed to be pinned until `T::unregister`.
                unsafe { T::register(drv, name, module) }
            }),
        })
    }
}

#[pinned_drop]
impl<T: RegistrationOps> PinnedDrop for Registration<T> {
    fn drop(self: Pin<&mut Self>) {
        // SAFETY: The existence of `self` guarantees that `self.reg` has previously been
        // successfully registered with `T::register`
        unsafe { T::unregister(&self.reg) };
    }
}

/// Declares a kernel module that exposes a single driver.
///
/// It is meant to be used as a helper by other subsystems so they can more easily expose their own
/// macros.
#[macro_export]
macro_rules! module_driver {
    (<$gen_type:ident>, $driver_ops:ty, { type: $type:ty, $($f:tt)* }) => {
        type Ops<$gen_type> = $driver_ops;

        #[$crate::prelude::pin_data]
        struct DriverModule {
            #[pin]
            _driver: $crate::driver::Registration<Ops<$type>>,
        }

        impl $crate::InPlaceModule for DriverModule {
            fn init(
                module: &'static $crate::ThisModule
            ) -> impl $crate::init::PinInit<Self, $crate::error::Error> {
                $crate::try_pin_init!(Self {
                    _driver <- $crate::driver::Registration::new(
                        <Self as $crate::ModuleMetadata>::NAME,
                        module,
                    ),
                })
            }
        }

        $crate::prelude::module! {
            type: DriverModule,
            $($f)*
        }
    }
}

/// The bus independent adapter to match a drivers and a devices.
///
/// This trait should be implemented by the bus specific adapter, which represents the connection
/// of a device and a driver.
///
/// It provides bus independent functions for device / driver interactions.
pub trait Adapter {
    /// The type holding driver private data about each device id supported by the driver.
    type IdInfo: 'static;

    /// The [`of::IdTable`] of the corresponding driver.
    fn of_id_table() -> Option<of::IdTable<Self::IdInfo>>;

    /// Returns the driver's private data from the matching entry in the [`of::IdTable`], if any.
    ///
    /// If this returns `None`, it means there is no match with an entry in the [`of::IdTable`].
    #[cfg(CONFIG_OF)]
    fn of_id_info(dev: &device::Device) -> Option<&'static Self::IdInfo> {
        let table = Self::of_id_table()?;

        // SAFETY:
        // - `table` has static lifetime, hence it's valid for read,
        // - `dev` is guaranteed to be valid while it's alive, and so is `pdev.as_ref().as_raw()`.
        let raw_id = unsafe { bindings::of_match_device(table.as_ptr(), dev.as_raw()) };

        if raw_id.is_null() {
            None
        } else {
            // SAFETY: `DeviceId` is a `#[repr(transparent)` wrapper of `struct of_device_id` and
            // does not add additional invariants, so it's safe to transmute.
            let id = unsafe { &*raw_id.cast::<of::DeviceId>() };

            Some(table.info(<of::DeviceId as crate::device_id::RawDeviceId>::index(id)))
        }
    }

    #[cfg(not(CONFIG_OF))]
    #[allow(missing_docs)]
    fn of_id_info(_dev: &device::Device) -> Option<&'static Self::IdInfo> {
        None
    }

    /// Returns the driver's private data from the matching entry of any of the ID tables, if any.
    ///
    /// If this returns `None`, it means that there is no match in any of the ID tables directly
    /// associated with a [`device::Device`].
    fn id_info(dev: &device::Device) -> Option<&'static Self::IdInfo> {
        let id = Self::of_id_info(dev);
        if id.is_some() {
            return id;
        }

        None
    }
}
