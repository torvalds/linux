// SPDX-License-Identifier: GPL-2.0

//! Generic support for drivers of different buses (e.g., PCI, Platform, Amba, etc.).
//!
//! Each bus / subsystem is expected to implement [`RegistrationOps`], which allows drivers to
//! register using the [`Registration`] class.

use crate::error::{Error, Result};
use crate::{init::PinInit, str::CStr, try_pin_init, types::Opaque, ThisModule};
use core::pin::Pin;
use macros::{pin_data, pinned_drop};

/// The [`RegistrationOps`] trait serves as generic interface for subsystems (e.g., PCI, Platform,
/// Amba, etc.) to provide the corresponding subsystem specific implementation to register /
/// unregister a driver of the particular type (`RegType`).
///
/// For instance, the PCI subsystem would set `RegType` to `bindings::pci_driver` and call
/// `bindings::__pci_register_driver` from `RegistrationOps::register` and
/// `bindings::pci_unregister_driver` from `RegistrationOps::unregister`.
pub trait RegistrationOps {
    /// The type that holds information about the registration. This is typically a struct defined
    /// by the C portion of the kernel.
    type RegType: Default;

    /// Registers a driver.
    ///
    /// On success, `reg` must remain pinned and valid until the matching call to
    /// [`RegistrationOps::unregister`].
    fn register(
        reg: &Opaque<Self::RegType>,
        name: &'static CStr,
        module: &'static ThisModule,
    ) -> Result;

    /// Unregisters a driver previously registered with [`RegistrationOps::register`].
    fn unregister(reg: &Opaque<Self::RegType>);
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

                T::register(drv, name, module)
            }),
        })
    }
}

#[pinned_drop]
impl<T: RegistrationOps> PinnedDrop for Registration<T> {
    fn drop(self: Pin<&mut Self>) {
        T::unregister(&self.reg);
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
