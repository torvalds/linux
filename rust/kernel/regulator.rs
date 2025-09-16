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
    device::Device,
    error::{from_err_ptr, to_result, Result},
    prelude::*,
};

use core::{marker::PhantomData, mem::ManuallyDrop, ptr::NonNull};

mod private {
    pub trait Sealed {}

    impl Sealed for super::Enabled {}
    impl Sealed for super::Disabled {}
    impl Sealed for super::Dynamic {}
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

/// A state that models the C API. The [`Regulator`] can be either enabled or
/// disabled, and the user is in control of the reference count. This is also
/// the default state.
///
/// Use [`Regulator::is_enabled`] to check the regulator's current state.
pub struct Dynamic;

impl RegulatorState for Enabled {
    const DISABLE_ON_DROP: bool = true;
}

impl RegulatorState for Disabled {
    const DISABLE_ON_DROP: bool = false;
}

impl RegulatorState for Dynamic {
    const DISABLE_ON_DROP: bool = false;
}

/// A trait that abstracts the ability to check if a [`Regulator`] is enabled.
pub trait IsEnabled: RegulatorState {}
impl IsEnabled for Disabled {}
impl IsEnabled for Dynamic {}

/// An error that can occur when trying to convert a [`Regulator`] between states.
pub struct Error<State: RegulatorState> {
    /// The error that occurred.
    pub error: kernel::error::Error,

    /// The regulator that caused the error, so that the operation may be retried.
    pub regulator: Regulator<State>,
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
/// ## Using [`Regulator<Dynamic>`]
///
/// This example mimics the behavior of the C API, where the user is in
/// control of the enabled reference count. This is useful for drivers that
/// might call enable and disable to manage the `enable` reference count at
/// runtime, perhaps as a result of `open()` and `close()` calls or whatever
/// other driver-specific or subsystem-specific hooks.
///
/// ```
/// # use kernel::prelude::*;
/// # use kernel::c_str;
/// # use kernel::device::Device;
/// # use kernel::regulator::{Regulator, Dynamic};
/// struct PrivateData {
///     regulator: Regulator<Dynamic>,
/// }
///
/// // A fictictious probe function that obtains a regulator and sets it up.
/// fn probe(dev: &Device) -> Result<PrivateData> {
///     // Obtain a reference to a (fictitious) regulator.
///     let mut regulator = Regulator::<Dynamic>::get(dev, c_str!("vcc"))?;
///
///     Ok(PrivateData { regulator })
/// }
///
/// // A fictictious function that indicates that the device is going to be used.
/// fn open(dev: &Device, data: &mut PrivateData) -> Result {
///     // Increase the `enabled` reference count.
///     data.regulator.enable()?;
///
///     Ok(())
/// }
///
/// fn close(dev: &Device, data: &mut PrivateData) -> Result {
///     // Decrease the `enabled` reference count.
///     data.regulator.disable()?;
///
///     Ok(())
/// }
///
/// fn remove(dev: &Device, data: PrivateData) -> Result {
///     // `PrivateData` is dropped here, which will drop the
///     // `Regulator<Dynamic>` in turn.
///     //
///     // The reference that was obtained by `regulator_get()` will be
///     // released, but it is up to the user to make sure that the number of calls
///     // to `enable()` and `disabled()` are balanced before this point.
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
pub struct Regulator<State = Dynamic>
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
        if voltage < 0 {
            Err(kernel::error::Error::from_errno(voltage))
        } else {
            Ok(Voltage::from_microvolts(voltage))
        }
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

    fn enable_internal(&mut self) -> Result {
        // SAFETY: Safe as per the type invariants of `Regulator`.
        to_result(unsafe { bindings::regulator_enable(self.inner.as_ptr()) })
    }

    fn disable_internal(&mut self) -> Result {
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
        let mut regulator = ManuallyDrop::new(self);

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
        let mut regulator = ManuallyDrop::new(self);

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

impl Regulator<Dynamic> {
    /// Obtains a [`Regulator`] instance from the system. The current state of
    /// the regulator is unknown and it is up to the user to manage the enabled
    /// reference count.
    ///
    /// This closely mimics the behavior of the C API and can be used to
    /// dynamically manage the enabled reference count at runtime.
    pub fn get(dev: &Device, name: &CStr) -> Result<Self> {
        Regulator::get_internal(dev, name)
    }

    /// Increases the `enabled` reference count.
    pub fn enable(&mut self) -> Result {
        self.enable_internal()
    }

    /// Decreases the `enabled` reference count.
    pub fn disable(&mut self) -> Result {
        self.disable_internal()
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
