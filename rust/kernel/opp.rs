// SPDX-License-Identifier: GPL-2.0

//! Operating performance points.
//!
//! This module provides rust abstractions for interacting with the OPP subsystem.
//!
//! C header: [`include/linux/pm_opp.h`](srctree/include/linux/pm_opp.h)
//!
//! Reference: <https://docs.kernel.org/power/opp.html>

use crate::{
    clk::Hertz,
    device::Device,
    error::{code::*, to_result, Result},
    ffi::c_ulong,
    types::{ARef, AlwaysRefCounted, Opaque},
};

use core::ptr;

/// The voltage unit.
///
/// Represents voltage in microvolts, wrapping a [`c_ulong`] value.
///
/// ## Examples
///
/// ```
/// use kernel::opp::MicroVolt;
///
/// let raw = 90500;
/// let volt = MicroVolt(raw);
///
/// assert_eq!(usize::from(volt), raw);
/// assert_eq!(volt, MicroVolt(raw));
/// ```
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct MicroVolt(pub c_ulong);

impl From<MicroVolt> for c_ulong {
    #[inline]
    fn from(volt: MicroVolt) -> Self {
        volt.0
    }
}

/// The power unit.
///
/// Represents power in microwatts, wrapping a [`c_ulong`] value.
///
/// ## Examples
///
/// ```
/// use kernel::opp::MicroWatt;
///
/// let raw = 1000000;
/// let power = MicroWatt(raw);
///
/// assert_eq!(usize::from(power), raw);
/// assert_eq!(power, MicroWatt(raw));
/// ```
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct MicroWatt(pub c_ulong);

impl From<MicroWatt> for c_ulong {
    #[inline]
    fn from(power: MicroWatt) -> Self {
        power.0
    }
}

/// Handle for a dynamically created [`OPP`].
///
/// The associated [`OPP`] is automatically removed when the [`Token`] is dropped.
///
/// ## Examples
///
/// The following example demonstrates how to create an [`OPP`] dynamically.
///
/// ```
/// use kernel::clk::Hertz;
/// use kernel::device::Device;
/// use kernel::error::Result;
/// use kernel::opp::{Data, MicroVolt, Token};
/// use kernel::types::ARef;
///
/// fn create_opp(dev: &ARef<Device>, freq: Hertz, volt: MicroVolt, level: u32) -> Result<Token> {
///     let data = Data::new(freq, volt, level, false);
///
///     // OPP is removed once token goes out of scope.
///     data.add_opp(dev)
/// }
/// ```
pub struct Token {
    dev: ARef<Device>,
    freq: Hertz,
}

impl Token {
    /// Dynamically adds an [`OPP`] and returns a [`Token`] that removes it on drop.
    fn new(dev: &ARef<Device>, mut data: Data) -> Result<Self> {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        to_result(unsafe { bindings::dev_pm_opp_add_dynamic(dev.as_raw(), &mut data.0) })?;
        Ok(Self {
            dev: dev.clone(),
            freq: data.freq(),
        })
    }
}

impl Drop for Token {
    fn drop(&mut self) {
        // SAFETY: The requirements are satisfied by the existence of [`Device`] and its safety
        // requirements.
        unsafe { bindings::dev_pm_opp_remove(self.dev.as_raw(), self.freq.into()) };
    }
}

/// OPP data.
///
/// Rust abstraction for the C `struct dev_pm_opp_data`, used to define operating performance
/// points (OPPs) dynamically.
///
/// ## Examples
///
/// The following example demonstrates how to create an [`OPP`] with [`Data`].
///
/// ```
/// use kernel::clk::Hertz;
/// use kernel::device::Device;
/// use kernel::error::Result;
/// use kernel::opp::{Data, MicroVolt, Token};
/// use kernel::types::ARef;
///
/// fn create_opp(dev: &ARef<Device>, freq: Hertz, volt: MicroVolt, level: u32) -> Result<Token> {
///     let data = Data::new(freq, volt, level, false);
///
///     // OPP is removed once token goes out of scope.
///     data.add_opp(dev)
/// }
/// ```
#[repr(transparent)]
pub struct Data(bindings::dev_pm_opp_data);

impl Data {
    /// Creates a new instance of [`Data`].
    ///
    /// This can be used to define a dynamic OPP to be added to a device.
    pub fn new(freq: Hertz, volt: MicroVolt, level: u32, turbo: bool) -> Self {
        Self(bindings::dev_pm_opp_data {
            turbo,
            freq: freq.into(),
            u_volt: volt.into(),
            level,
        })
    }

    /// Adds an [`OPP`] dynamically.
    ///
    /// Returns a [`Token`] that ensures the OPP is automatically removed
    /// when it goes out of scope.
    #[inline]
    pub fn add_opp(self, dev: &ARef<Device>) -> Result<Token> {
        Token::new(dev, self)
    }

    /// Returns the frequency associated with this OPP data.
    #[inline]
    fn freq(&self) -> Hertz {
        Hertz(self.0.freq)
    }
}

/// A reference-counted Operating performance point (OPP).
///
/// Rust abstraction for the C `struct dev_pm_opp`.
///
/// # Invariants
///
/// The pointer stored in `Self` is non-null and valid for the lifetime of the [`OPP`].
///
/// Instances of this type are reference-counted. The reference count is incremented by the
/// `dev_pm_opp_get` function and decremented by `dev_pm_opp_put`. The Rust type `ARef<OPP>`
/// represents a pointer that owns a reference count on the [`OPP`].
///
/// A reference to the [`OPP`], &[`OPP`], isn't refcounted by the Rust code.
#[repr(transparent)]
pub struct OPP(Opaque<bindings::dev_pm_opp>);

/// SAFETY: It is okay to send the ownership of [`OPP`] across thread boundaries.
unsafe impl Send for OPP {}

/// SAFETY: It is okay to access [`OPP`] through shared references from other threads because we're
/// either accessing properties that don't change or that are properly synchronised by C code.
unsafe impl Sync for OPP {}

/// SAFETY: The type invariants guarantee that [`OPP`] is always refcounted.
unsafe impl AlwaysRefCounted for OPP {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference means that the refcount is nonzero.
        unsafe { bindings::dev_pm_opp_get(self.0.get()) };
    }

    unsafe fn dec_ref(obj: ptr::NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is nonzero.
        unsafe { bindings::dev_pm_opp_put(obj.cast().as_ptr()) }
    }
}

impl OPP {
    /// Creates an owned reference to a [`OPP`] from a valid pointer.
    ///
    /// The refcount is incremented by the C code and will be decremented by `dec_ref` when the
    /// [`ARef`] object is dropped.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid and the refcount of the [`OPP`] is incremented.
    /// The caller must also ensure that it doesn't explicitly drop the refcount of the [`OPP`], as
    /// the returned [`ARef`] object takes over the refcount increment on the underlying object and
    /// the same will be dropped along with it.
    pub unsafe fn from_raw_opp_owned(ptr: *mut bindings::dev_pm_opp) -> Result<ARef<Self>> {
        let ptr = ptr::NonNull::new(ptr).ok_or(ENODEV)?;

        // SAFETY: The safety requirements guarantee the validity of the pointer.
        //
        // INVARIANT: The reference-count is decremented when [`OPP`] goes out of scope.
        Ok(unsafe { ARef::from_raw(ptr.cast()) })
    }

    /// Creates a reference to a [`OPP`] from a valid pointer.
    ///
    /// The refcount is not updated by the Rust API unless the returned reference is converted to
    /// an [`ARef`] object.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid and remains valid for the duration of `'a`.
    #[inline]
    pub unsafe fn from_raw_opp<'a>(ptr: *mut bindings::dev_pm_opp) -> Result<&'a Self> {
        // SAFETY: The caller guarantees that the pointer is not dangling and stays valid for the
        // duration of 'a. The cast is okay because [`OPP`] is `repr(transparent)`.
        Ok(unsafe { &*ptr.cast() })
    }

    #[inline]
    fn as_raw(&self) -> *mut bindings::dev_pm_opp {
        self.0.get()
    }

    /// Returns the frequency of an [`OPP`].
    pub fn freq(&self, index: Option<u32>) -> Hertz {
        let index = index.unwrap_or(0);

        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe to
        // use it.
        Hertz(unsafe { bindings::dev_pm_opp_get_freq_indexed(self.as_raw(), index) })
    }

    /// Returns the voltage of an [`OPP`].
    #[inline]
    pub fn voltage(&self) -> MicroVolt {
        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe to
        // use it.
        MicroVolt(unsafe { bindings::dev_pm_opp_get_voltage(self.as_raw()) })
    }

    /// Returns the level of an [`OPP`].
    #[inline]
    pub fn level(&self) -> u32 {
        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe to
        // use it.
        unsafe { bindings::dev_pm_opp_get_level(self.as_raw()) }
    }

    /// Returns the power of an [`OPP`].
    #[inline]
    pub fn power(&self) -> MicroWatt {
        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe to
        // use it.
        MicroWatt(unsafe { bindings::dev_pm_opp_get_power(self.as_raw()) })
    }

    /// Returns the required pstate of an [`OPP`].
    #[inline]
    pub fn required_pstate(&self, index: u32) -> u32 {
        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe to
        // use it.
        unsafe { bindings::dev_pm_opp_get_required_pstate(self.as_raw(), index) }
    }

    /// Returns true if the [`OPP`] is turbo.
    #[inline]
    pub fn is_turbo(&self) -> bool {
        // SAFETY: By the type invariants, we know that `self` owns a reference, so it is safe to
        // use it.
        unsafe { bindings::dev_pm_opp_is_turbo(self.as_raw()) }
    }
}
