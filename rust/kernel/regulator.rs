// SPDX-License-Identifier: GPL-2.0

//! Regulator abstractions, providing a standard kernel interface to control
//! voltage and current regulators.
//!
//! The intention is to allow systems to dynamically control regulator power
//! output in order to save power and prolong battery life. This applies to both
//! voltage regulators (where voltage output is controllable) and current sinks
//! (where current limit is controllable).
//!
//! C header: [`include/linux/regulator/consumer.h`](srctree/include/linux/regulator/consumer.h)
//!
//! Regulators are modeled in Rust with a collection of states. Each state may
//! enforce a given invariant, and they may convert between each other where applicable.
//!
//! See [Voltage and current regulator API](https://docs.kernel.org/driver-api/regulator.html)
//! for more information.

use crate::{
    bindings,
    device::{Bound, Device},
    error::{from_err_ptr, to_result, Result},
    prelude::*,
};

use core::{marker::PhantomData, mem::ManuallyDrop, ptr::NonNull};

mod private {
    pub trait Sealed {}

    impl Sealed for super::Enabled {}
    impl Sealed for super::Disabled {}
}

/// A trait representing the different states a [`Regulator`] can be in.
pub trait RegulatorState: private::Sealed + 'static {
    /// Whether the regulator should be disabled when dropped.
    const DISABLE_ON_DROP: bool;
}

/// A state where the [`Regulator`] is known to be enabled.
///
/// The `enable` reference count held by this state is decremented when it is
/// dropped.
pub struct Enabled;

/// A state where this [`Regulator`] handle has not specifically asked for the
/// underlying regulator to be enabled. This means that this reference does not
/// own an `enable` reference count, but the regulator may still be on.
pub struct Disabled;

impl RegulatorState for Enabled {
    const DISABLE_ON_DROP: bool = true;
}

impl RegulatorState for Disabled {
    const DISABLE_ON_DROP: bool = false;
}

/// A trait that abstracts the ability to check if a [`Regulator`] is enabled.
pub trait IsEnabled: RegulatorState {}
impl IsEnabled for Disabled {}

/// An error that can occur when trying to convert a [`Regulator`] between states.
pub struct Error<State: RegulatorState> {
    /// The error that occurred.
    pub error: kernel::error::Error,

    /// The regulator that caused the error, so that the operation may be retried.
    pub regulator: Regulator<State>,
}
/// Obtains and enables a [`devres`]-managed regulator for a device.
///
/// This calls [`regulator_disable()`] and [`regulator_put()`] automatically on
/// driver detach.
///
/// This API is identical to `devm_regulator_get_enable()`, and should be
/// preferred over the [`Regulator<T: RegulatorState>`] API if the caller only
/// cares about the regulator being enabled.
///
/// [`devres`]: https://docs.kernel.org/driver-api/driver-model/devres.html
/// [`regulator_disable()`]: https://docs.kernel.org/driver-api/regulator.html#c.regulator_disable
/// [`regulator_put()`]: https://docs.kernel.org/driver-api/regulator.html#c.regulator_put
pub fn devm_enable(dev: &Device<Bound>, name: &CStr) -> Result {
    // SAFETY: `dev` is a valid and bound device, while `name` is a valid C
    // string.
    to_result(unsafe { bindings::devm_regulator_get_enable(dev.as_raw(), name.as_ptr()) })
}

/// Same as [`devm_enable`], but calls `devm_regulator_get_enable_optional`
/// instead.
///
/// This obtains and enables a [`devres`]-managed regulator for a device, but
/// does not print a message nor provides a dummy if the regulator is not found.
///
/// This calls [`regulator_disable()`] and [`regulator_put()`] automatically on
/// driver detach.
///
/// [`devres`]: https://docs.kernel.org/driver-api/driver-model/devres.html
/// [`regulator_disable()`]: https://docs.kernel.org/driver-api/regulator.html#c.regulator_disable
/// [`regulator_put()`]: https://docs.kernel.org/driver-api/regulator.html#c.regulator_put
pub fn devm_enable_optional(dev: &Device<Bound>, name: &CStr) -> Result {
    // SAFETY: `dev` is a valid and bound device, while `name` is a valid C
    // string.
    to_result(unsafe { bindings::devm_regulator_get_enable_optional(dev.as_raw(), name.as_ptr()) })
}

/// A `struct regulator` abstraction.
///
/// # Examples
///
/// ## Enabling a regulator
///
/// This example uses [`Regulator<Enabled>`], which is suitable for drivers that
/// enable a regulator at probe time and leave them on until the device is
/// removed or otherwise shutdown.
///
/// These users can store [`Regulator<Enabled>`] directly in their driver's
/// private data struct.
///
/// ```
/// # use kernel::prelude::*;
/// # use kernel::c_str;
/// # use kernel::device::Device;
/// # use kernel::regulator::{Voltage, Regulator, Disabled, Enabled};
/// fn enable(dev: &Device, min_voltage: Voltage, max_voltage: Voltage) -> Result {
///     // Obtain a reference to a (fictitious) regulator.
///     let regulator: Regulator<Disabled> = Regulator::<Disabled>::get(dev, c_str!("vcc"))?;
///
///     // The voltage can be set before enabling the regulator if needed, e.g.:
///     regulator.set_voltage(min_voltage, max_voltage)?;
///
///     // The same applies for `get_voltage()`, i.e.:
///     let voltage: Voltage = regulator.get_voltage()?;
///
///     // Enables the regulator, consuming the previous value.
///     //
///     // From now on, the regulator is known to be enabled because of the type
///     // `Enabled`.
///     //
///     // If this operation fails, the `Error` will contain the regulator
///     // reference, so that the operation may be retried.
///     let regulator: Regulator<Enabled> =
///         regulator.try_into_enabled().map_err(|error| error.error)?;
///
///     // The voltage can also be set after enabling the regulator, e.g.:
///     regulator.set_voltage(min_voltage, max_voltage)?;
///
///     // The same applies for `get_voltage()`, i.e.:
///     let voltage: Voltage = regulator.get_voltage()?;
///
///     // Dropping an enabled regulator will disable it. The refcount will be
///     // decremented.
///     drop(regulator);
///
///     // ...
///
///     Ok(())
/// }
/// ```
///
/// A more concise shortcut is available for enabling a regulator. This is
/// equivalent to `regulator_get_enable()`:
///
/// ```
/// # use kernel::prelude::*;
/// # use kernel::c_str;
/// # use kernel::device::Device;
/// # use kernel::regulator::{Voltage, Regulator, Enabled};
/// fn enable(dev: &Device) -> Result {
///     // Obtain a reference to a (fictitious) regulator and enable it.
///     let regulator: Regulator<Enabled> = Regulator::<Enabled>::get(dev, c_str!("vcc"))?;
///
///     // Dropping an enabled regulator will disable it. The refcount will be
///     // decremented.
///     drop(regulator);
///
///     // ...
///
///     Ok(())
/// }
/// ```
///
/// If a driver only cares about the regulator being on for as long it is bound
/// to a device, then it should use [`devm_enable`] or [`devm_enable_optional`].
/// This should be the default use-case unless more fine-grained control over
/// the regulator's state is required.
///
/// [`devm_enable`]: crate::regulator::devm_enable
/// [`devm_optional`]: crate::regulator::devm_enable_optional
///
/// ```
/// # use kernel::prelude::*;
/// # use kernel::c_str;
/// # use kernel::device::{Bound, Device};
/// # use kernel::regulator;
/// fn enable(dev: &Device<Bound>) -> Result {
///     // Obtain a reference to a (fictitious) regulator and enable it. This
///     // call only returns whether the operation succeeded.
///     regulator::devm_enable(dev, c_str!("vcc"))?;
///
///     // The regulator will be disabled and put when `dev` is unbound.
///     Ok(())
/// }
/// ```
///
/// ## Disabling a regulator
///
/// ```
/// # use kernel::prelude::*;
/// # use kernel::device::Device;
/// # use kernel::regulator::{Regulator, Enabled, Disabled};
/// fn disable(dev: &Device, regulator: Regulator<Enabled>) -> Result {
///     // We can also disable an enabled regulator without reliquinshing our
///     // refcount:
///     //
///     // If this operation fails, the `Error` will contain the regulator
///     // reference, so that the operation may be retried.
///     let regulator: Regulator<Disabled> =
///         regulator.try_into_disabled().map_err(|error| error.error)?;
///
///     // The refcount will be decremented when `regulator` is dropped.
///     drop(regulator);
///
///     // ...
///
///     Ok(())
/// }
/// ```
///
/// # Invariants
///
/// - `inner` is a non-null wrapper over a pointer to a `struct
///   regulator` obtained from [`regulator_get()`].
///
/// [`regulator_get()`]: https://docs.kernel.org/driver-api/regulator.html#c.regulator_get
pub struct Regulator<State>
where
    State: RegulatorState,
{
    inner: NonNull<bindings::regulator>,
    _phantom: PhantomData<State>,
}

impl<T: RegulatorState> Regulator<T> {
    /// Sets the voltage for the regulator.
    ///
    /// This can be used to ensure that the device powers up cleanly.
    pub fn set_voltage(&self, min_voltage: Voltage, max_voltage: Voltage) -> Result {
        // SAFETY: Safe as per the type invariants of `Regulator`.
        to_result(unsafe {
            bindings::regulator_set_voltage(
                self.inner.as_ptr(),
                min_voltage.as_microvolts(),
                max_voltage.as_microvolts(),
            )
        })
    }

    /// Gets the current voltage of the regulator.
    pub fn get_voltage(&self) -> Result<Voltage> {
        // SAFETY: Safe as per the type invariants of `Regulator`.
        let voltage = unsafe { bindings::regulator_get_voltage(self.inner.as_ptr()) };

        to_result(voltage).map(|()| Voltage::from_microvolts(voltage))
    }

    fn get_internal(dev: &Device, name: &CStr) -> Result<Regulator<T>> {
        // SAFETY: It is safe to call `regulator_get()`, on a device pointer
        // received from the C code.
        let inner = from_err_ptr(unsafe { bindings::regulator_get(dev.as_raw(), name.as_ptr()) })?;

        // SAFETY: We can safely trust `inner` to be a pointer to a valid
        // regulator if `ERR_PTR` was not returned.
        let inner = unsafe { NonNull::new_unchecked(inner) };

        Ok(Self {
            inner,
            _phantom: PhantomData,
        })
    }

    fn enable_internal(&self) -> Result {
        // SAFETY: Safe as per the type invariants of `Regulator`.
        to_result(unsafe { bindings::regulator_enable(self.inner.as_ptr()) })
    }

    fn disable_internal(&self) -> Result {
        // SAFETY: Safe as per the type invariants of `Regulator`.
        to_result(unsafe { bindings::regulator_disable(self.inner.as_ptr()) })
    }
}

impl Regulator<Disabled> {
    /// Obtains a [`Regulator`] instance from the system.
    pub fn get(dev: &Device, name: &CStr) -> Result<Self> {
        Regulator::get_internal(dev, name)
    }

    /// Attempts to convert the regulator to an enabled state.
    pub fn try_into_enabled(self) -> Result<Regulator<Enabled>, Error<Disabled>> {
        // We will be transferring the ownership of our `regulator_get()` count to
        // `Regulator<Enabled>`.
        let regulator = ManuallyDrop::new(self);

        regulator
            .enable_internal()
            .map(|()| Regulator {
                inner: regulator.inner,
                _phantom: PhantomData,
            })
            .map_err(|error| Error {
                error,
                regulator: ManuallyDrop::into_inner(regulator),
            })
    }
}

impl Regulator<Enabled> {
    /// Obtains a [`Regulator`] instance from the system and enables it.
    ///
    /// This is equivalent to calling `regulator_get_enable()` in the C API.
    pub fn get(dev: &Device, name: &CStr) -> Result<Self> {
        Regulator::<Disabled>::get_internal(dev, name)?
            .try_into_enabled()
            .map_err(|error| error.error)
    }

    /// Attempts to convert the regulator to a disabled state.
    pub fn try_into_disabled(self) -> Result<Regulator<Disabled>, Error<Enabled>> {
        // We will be transferring the ownership of our `regulator_get()` count
        // to `Regulator<Disabled>`.
        let regulator = ManuallyDrop::new(self);

        regulator
            .disable_internal()
            .map(|()| Regulator {
                inner: regulator.inner,
                _phantom: PhantomData,
            })
            .map_err(|error| Error {
                error,
                regulator: ManuallyDrop::into_inner(regulator),
            })
    }
}

impl<T: IsEnabled> Regulator<T> {
    /// Checks if the regulator is enabled.
    pub fn is_enabled(&self) -> bool {
        // SAFETY: Safe as per the type invariants of `Regulator`.
        unsafe { bindings::regulator_is_enabled(self.inner.as_ptr()) != 0 }
    }
}

impl<T: RegulatorState> Drop for Regulator<T> {
    fn drop(&mut self) {
        if T::DISABLE_ON_DROP {
            // SAFETY: By the type invariants, we know that `self` owns a
            // reference on the enabled refcount, so it is safe to relinquish it
            // now.
            unsafe { bindings::regulator_disable(self.inner.as_ptr()) };
        }
        // SAFETY: By the type invariants, we know that `self` owns a reference,
        // so it is safe to relinquish it now.
        unsafe { bindings::regulator_put(self.inner.as_ptr()) };
    }
}

// SAFETY: It is safe to send a `Regulator<T>` across threads. In particular, a
// Regulator<T> can be dropped from any thread.
unsafe impl<T: RegulatorState> Send for Regulator<T> {}

// SAFETY: It is safe to send a &Regulator<T> across threads because the C side
// handles its own locking.
unsafe impl<T: RegulatorState> Sync for Regulator<T> {}

/// A voltage.
///
/// This type represents a voltage value in microvolts.
#[repr(transparent)]
#[derive(Copy, Clone, PartialEq, Eq)]
pub struct Voltage(i32);

impl Voltage {
    /// Creates a new `Voltage` from a value in microvolts.
    pub fn from_microvolts(uv: i32) -> Self {
        Self(uv)
    }

    /// Returns the value of the voltage in microvolts as an [`i32`].
    pub fn as_microvolts(self) -> i32 {
        self.0
    }
}
