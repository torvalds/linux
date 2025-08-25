// SPDX-License-Identifier: GPL-2.0

//! Generic support for drivers of different buses (e.g., PCI, Platform, Amba, etc.).
//!
//! This documentation describes how to implement a bus specific driver API and how to align it with
//! the design of (bus specific) devices.
//!
//! Note: Readers are expected to know the content of the documentation of [`Device`] and
//! [`DeviceContext`].
//!
//! # Driver Trait
//!
//! The main driver interface is defined by a bus specific driver trait. For instance:
//!
//! ```ignore
//! pub trait Driver: Send {
//!     /// The type holding information about each device ID supported by the driver.
//!     type IdInfo: 'static;
//!
//!     /// The table of OF device ids supported by the driver.
//!     const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = None;
//!
//!     /// The table of ACPI device ids supported by the driver.
//!     const ACPI_ID_TABLE: Option<acpi::IdTable<Self::IdInfo>> = None;
//!
//!     /// Driver probe.
//!     fn probe(dev: &Device<device::Core>, id_info: &Self::IdInfo) -> Result<Pin<KBox<Self>>>;
//!
//!     /// Driver unbind (optional).
//!     fn unbind(dev: &Device<device::Core>, this: Pin<&Self>) {
//!         let _ = (dev, this);
//!     }
//! }
//! ```
//!
//! For specific examples see [`auxiliary::Driver`], [`pci::Driver`] and [`platform::Driver`].
//!
//! The `probe()` callback should return a `Result<Pin<KBox<Self>>>`, i.e. the driver's private
//! data. The bus abstraction should store the pointer in the corresponding bus device. The generic
//! [`Device`] infrastructure provides common helpers for this purpose on its
//! [`Device<CoreInternal>`] implementation.
//!
//! All driver callbacks should provide a reference to the driver's private data. Once the driver
//! is unbound from the device, the bus abstraction should take back the ownership of the driver's
//! private data from the corresponding [`Device`] and [`drop`] it.
//!
//! All driver callbacks should provide a [`Device<Core>`] reference (see also [`device::Core`]).
//!
//! # Adapter
//!
//! The adapter implementation of a bus represents the abstraction layer between the C bus
//! callbacks and the Rust bus callbacks. It therefore has to be generic over an implementation of
//! the [driver trait](#driver-trait).
//!
//! ```ignore
//! pub struct Adapter<T: Driver>;
//! ```
//!
//! There's a common [`Adapter`] trait that can be implemented to inherit common driver
//! infrastructure, such as finding the ID info from an [`of::IdTable`] or [`acpi::IdTable`].
//!
//! # Driver Registration
//!
//! In order to register C driver types (such as `struct platform_driver`) the [adapter](#adapter)
//! should implement the [`RegistrationOps`] trait.
//!
//! This trait implementation can be used to create the actual registration with the common
//! [`Registration`] type.
//!
//! Typically, bus abstractions want to provide a bus specific `module_bus_driver!` macro, which
//! creates a kernel module with exactly one [`Registration`] for the bus specific adapter.
//!
//! The generic driver infrastructure provides a helper for this with the [`module_driver`] macro.
//!
//! # Device IDs
//!
//! Besides the common device ID types, such as [`of::DeviceId`] and [`acpi::DeviceId`], most buses
//! may need to implement their own device ID types.
//!
//! For this purpose the generic infrastructure in [`device_id`] should be used.
//!
//! [`auxiliary::Driver`]: kernel::auxiliary::Driver
//! [`Core`]: device::Core
//! [`Device`]: device::Device
//! [`Device<Core>`]: device::Device<device::Core>
//! [`Device<CoreInternal>`]: device::Device<device::CoreInternal>
//! [`DeviceContext`]: device::DeviceContext
//! [`device_id`]: kernel::device_id
//! [`module_driver`]: kernel::module_driver
//! [`pci::Driver`]: kernel::pci::Driver
//! [`platform::Driver`]: kernel::platform::Driver

use crate::error::{Error, Result};
use crate::{acpi, device, of, str::CStr, try_pin_init, types::Opaque, ThisModule};
use core::pin::Pin;
use pin_init::{pin_data, pinned_drop, PinInit};

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
            ) -> impl ::pin_init::PinInit<Self, $crate::error::Error> {
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

    /// The [`acpi::IdTable`] of the corresponding driver
    fn acpi_id_table() -> Option<acpi::IdTable<Self::IdInfo>>;

    /// Returns the driver's private data from the matching entry in the [`acpi::IdTable`], if any.
    ///
    /// If this returns `None`, it means there is no match with an entry in the [`acpi::IdTable`].
    fn acpi_id_info(dev: &device::Device) -> Option<&'static Self::IdInfo> {
        #[cfg(not(CONFIG_ACPI))]
        {
            let _ = dev;
            None
        }

        #[cfg(CONFIG_ACPI)]
        {
            let table = Self::acpi_id_table()?;

            // SAFETY:
            // - `table` has static lifetime, hence it's valid for read,
            // - `dev` is guaranteed to be valid while it's alive, and so is `dev.as_raw()`.
            let raw_id = unsafe { bindings::acpi_match_device(table.as_ptr(), dev.as_raw()) };

            if raw_id.is_null() {
                None
            } else {
                // SAFETY: `DeviceId` is a `#[repr(transparent)]` wrapper of `struct acpi_device_id`
                // and does not add additional invariants, so it's safe to transmute.
                let id = unsafe { &*raw_id.cast::<acpi::DeviceId>() };

                Some(table.info(<acpi::DeviceId as crate::device_id::RawDeviceIdIndex>::index(id)))
            }
        }
    }

    /// The [`of::IdTable`] of the corresponding driver.
    fn of_id_table() -> Option<of::IdTable<Self::IdInfo>>;

    /// Returns the driver's private data from the matching entry in the [`of::IdTable`], if any.
    ///
    /// If this returns `None`, it means there is no match with an entry in the [`of::IdTable`].
    fn of_id_info(dev: &device::Device) -> Option<&'static Self::IdInfo> {
        #[cfg(not(CONFIG_OF))]
        {
            let _ = dev;
            None
        }

        #[cfg(CONFIG_OF)]
        {
            let table = Self::of_id_table()?;

            // SAFETY:
            // - `table` has static lifetime, hence it's valid for read,
            // - `dev` is guaranteed to be valid while it's alive, and so is `dev.as_raw()`.
            let raw_id = unsafe { bindings::of_match_device(table.as_ptr(), dev.as_raw()) };

            if raw_id.is_null() {
                None
            } else {
                // SAFETY: `DeviceId` is a `#[repr(transparent)]` wrapper of `struct of_device_id`
                // and does not add additional invariants, so it's safe to transmute.
                let id = unsafe { &*raw_id.cast::<of::DeviceId>() };

                Some(
                    table.info(<of::DeviceId as crate::device_id::RawDeviceIdIndex>::index(
                        id,
                    )),
                )
            }
        }
    }

    /// Returns the driver's private data from the matching entry of any of the ID tables, if any.
    ///
    /// If this returns `None`, it means that there is no match in any of the ID tables directly
    /// associated with a [`device::Device`].
    fn id_info(dev: &device::Device) -> Option<&'static Self::IdInfo> {
        let id = Self::acpi_id_info(dev);
        if id.is_some() {
            return id;
        }

        let id = Self::of_id_info(dev);
        if id.is_some() {
            return id;
        }

        None
    }
}
