// SPDX-License-Identifier: GPL-2.0

//! Clock abstractions.
//!
//! C header: [`include/linux/clk.h`](srctree/include/linux/clk.h)
//!
//! Reference: <https://docs.kernel.org/driver-api/clk.html>

use crate::ffi::c_ulong;

/// The frequency unit.
///
/// Represents a frequency in hertz, wrapping a [`c_ulong`] value.
///
/// # Examples
///
/// ```
/// use kernel::clk::Hertz;
///
/// let hz = 1_000_000_000;
/// let rate = Hertz(hz);
///
/// assert_eq!(rate.as_hz(), hz);
/// assert_eq!(rate, Hertz(hz));
/// assert_eq!(rate, Hertz::from_khz(hz / 1_000));
/// assert_eq!(rate, Hertz::from_mhz(hz / 1_000_000));
/// assert_eq!(rate, Hertz::from_ghz(hz / 1_000_000_000));
/// ```
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct Hertz(pub c_ulong);

impl Hertz {
    const KHZ_TO_HZ: c_ulong = 1_000;
    const MHZ_TO_HZ: c_ulong = 1_000_000;
    const GHZ_TO_HZ: c_ulong = 1_000_000_000;

    /// Create a new instance from kilohertz (kHz)
    pub const fn from_khz(khz: c_ulong) -> Self {
        Self(khz * Self::KHZ_TO_HZ)
    }

    /// Create a new instance from megahertz (MHz)
    pub const fn from_mhz(mhz: c_ulong) -> Self {
        Self(mhz * Self::MHZ_TO_HZ)
    }

    /// Create a new instance from gigahertz (GHz)
    pub const fn from_ghz(ghz: c_ulong) -> Self {
        Self(ghz * Self::GHZ_TO_HZ)
    }

    /// Get the frequency in hertz
    pub const fn as_hz(&self) -> c_ulong {
        self.0
    }

    /// Get the frequency in kilohertz
    pub const fn as_khz(&self) -> c_ulong {
        self.0 / Self::KHZ_TO_HZ
    }

    /// Get the frequency in megahertz
    pub const fn as_mhz(&self) -> c_ulong {
        self.0 / Self::MHZ_TO_HZ
    }

    /// Get the frequency in gigahertz
    pub const fn as_ghz(&self) -> c_ulong {
        self.0 / Self::GHZ_TO_HZ
    }
}

impl From<Hertz> for c_ulong {
    fn from(freq: Hertz) -> Self {
        freq.0
    }
}

#[cfg(CONFIG_COMMON_CLK)]
mod common_clk {
    use super::Hertz;
    use crate::{
        device::Device,
        error::{from_err_ptr, to_result, Result},
        prelude::*,
    };

    use core::{ops::Deref, ptr};

    /// A reference-counted clock.
    ///
    /// Rust abstraction for the C [`struct clk`].
    ///
    /// # Invariants
    ///
    /// A [`Clk`] instance holds either a pointer to a valid [`struct clk`] created by the C
    /// portion of the kernel or a NULL pointer.
    ///
    /// Instances of this type are reference-counted. Calling [`Clk::get`] ensures that the
    /// allocation remains valid for the lifetime of the [`Clk`].
    ///
    /// # Examples
    ///
    /// The following example demonstrates how to obtain and configure a clock for a device.
    ///
    /// ```
    /// use kernel::c_str;
    /// use kernel::clk::{Clk, Hertz};
    /// use kernel::device::Device;
    /// use kernel::error::Result;
    ///
    /// fn configure_clk(dev: &Device) -> Result {
    ///     let clk = Clk::get(dev, Some(c_str!("apb_clk")))?;
    ///
    ///     clk.prepare_enable()?;
    ///
    ///     let expected_rate = Hertz::from_ghz(1);
    ///
    ///     if clk.rate() != expected_rate {
    ///         clk.set_rate(expected_rate)?;
    ///     }
    ///
    ///     clk.disable_unprepare();
    ///     Ok(())
    /// }
    /// ```
    ///
    /// [`struct clk`]: https://docs.kernel.org/driver-api/clk.html
    #[repr(transparent)]
    pub struct Clk(*mut bindings::clk);

    impl Clk {
        /// Gets [`Clk`] corresponding to a [`Device`] and a connection id.
        ///
        /// Equivalent to the kernel's [`clk_get`] API.
        ///
        /// [`clk_get`]: https://docs.kernel.org/core-api/kernel-api.html#c.clk_get
        pub fn get(dev: &Device, name: Option<&CStr>) -> Result<Self> {
            let con_id = name.map_or(ptr::null(), |n| n.as_ptr());

            // SAFETY: It is safe to call [`clk_get`] for a valid device pointer.
            //
            // INVARIANT: The reference-count is decremented when [`Clk`] goes out of scope.
            Ok(Self(from_err_ptr(unsafe {
                bindings::clk_get(dev.as_raw(), con_id)
            })?))
        }

        /// Obtain the raw [`struct clk`] pointer.
        #[inline]
        pub fn as_raw(&self) -> *mut bindings::clk {
            self.0
        }

        /// Enable the clock.
        ///
        /// Equivalent to the kernel's [`clk_enable`] API.
        ///
        /// [`clk_enable`]: https://docs.kernel.org/core-api/kernel-api.html#c.clk_enable
        #[inline]
        pub fn enable(&self) -> Result {
            // SAFETY: By the type invariants, self.as_raw() is a valid argument for
            // [`clk_enable`].
            to_result(unsafe { bindings::clk_enable(self.as_raw()) })
        }

        /// Disable the clock.
        ///
        /// Equivalent to the kernel's [`clk_disable`] API.
        ///
        /// [`clk_disable`]: https://docs.kernel.org/core-api/kernel-api.html#c.clk_disable
        #[inline]
        pub fn disable(&self) {
            // SAFETY: By the type invariants, self.as_raw() is a valid argument for
            // [`clk_disable`].
            unsafe { bindings::clk_disable(self.as_raw()) };
        }

        /// Prepare the clock.
        ///
        /// Equivalent to the kernel's [`clk_prepare`] API.
        ///
        /// [`clk_prepare`]: https://docs.kernel.org/core-api/kernel-api.html#c.clk_prepare
        #[inline]
        pub fn prepare(&self) -> Result {
            // SAFETY: By the type invariants, self.as_raw() is a valid argument for
            // [`clk_prepare`].
            to_result(unsafe { bindings::clk_prepare(self.as_raw()) })
        }

        /// Unprepare the clock.
        ///
        /// Equivalent to the kernel's [`clk_unprepare`] API.
        ///
        /// [`clk_unprepare`]: https://docs.kernel.org/core-api/kernel-api.html#c.clk_unprepare
        #[inline]
        pub fn unprepare(&self) {
            // SAFETY: By the type invariants, self.as_raw() is a valid argument for
            // [`clk_unprepare`].
            unsafe { bindings::clk_unprepare(self.as_raw()) };
        }

        /// Prepare and enable the clock.
        ///
        /// Equivalent to calling [`Clk::prepare`] followed by [`Clk::enable`].
        #[inline]
        pub fn prepare_enable(&self) -> Result {
            // SAFETY: By the type invariants, self.as_raw() is a valid argument for
            // [`clk_prepare_enable`].
            to_result(unsafe { bindings::clk_prepare_enable(self.as_raw()) })
        }

        /// Disable and unprepare the clock.
        ///
        /// Equivalent to calling [`Clk::disable`] followed by [`Clk::unprepare`].
        #[inline]
        pub fn disable_unprepare(&self) {
            // SAFETY: By the type invariants, self.as_raw() is a valid argument for
            // [`clk_disable_unprepare`].
            unsafe { bindings::clk_disable_unprepare(self.as_raw()) };
        }

        /// Get clock's rate.
        ///
        /// Equivalent to the kernel's [`clk_get_rate`] API.
        ///
        /// [`clk_get_rate`]: https://docs.kernel.org/core-api/kernel-api.html#c.clk_get_rate
        #[inline]
        pub fn rate(&self) -> Hertz {
            // SAFETY: By the type invariants, self.as_raw() is a valid argument for
            // [`clk_get_rate`].
            Hertz(unsafe { bindings::clk_get_rate(self.as_raw()) })
        }

        /// Set clock's rate.
        ///
        /// Equivalent to the kernel's [`clk_set_rate`] API.
        ///
        /// [`clk_set_rate`]: https://docs.kernel.org/core-api/kernel-api.html#c.clk_set_rate
        #[inline]
        pub fn set_rate(&self, rate: Hertz) -> Result {
            // SAFETY: By the type invariants, self.as_raw() is a valid argument for
            // [`clk_set_rate`].
            to_result(unsafe { bindings::clk_set_rate(self.as_raw(), rate.as_hz()) })
        }
    }

    impl Drop for Clk {
        fn drop(&mut self) {
            // SAFETY: By the type invariants, self.as_raw() is a valid argument for [`clk_put`].
            unsafe { bindings::clk_put(self.as_raw()) };
        }
    }

    /// A reference-counted optional clock.
    ///
    /// A lightweight wrapper around an optional [`Clk`]. An [`OptionalClk`] represents a [`Clk`]
    /// that a driver can function without but may improve performance or enable additional
    /// features when available.
    ///
    /// # Invariants
    ///
    /// An [`OptionalClk`] instance encapsulates a [`Clk`] with either a valid [`struct clk`] or
    /// `NULL` pointer.
    ///
    /// Instances of this type are reference-counted. Calling [`OptionalClk::get`] ensures that the
    /// allocation remains valid for the lifetime of the [`OptionalClk`].
    ///
    /// # Examples
    ///
    /// The following example demonstrates how to obtain and configure an optional clock for a
    /// device. The code functions correctly whether or not the clock is available.
    ///
    /// ```
    /// use kernel::c_str;
    /// use kernel::clk::{OptionalClk, Hertz};
    /// use kernel::device::Device;
    /// use kernel::error::Result;
    ///
    /// fn configure_clk(dev: &Device) -> Result {
    ///     let clk = OptionalClk::get(dev, Some(c_str!("apb_clk")))?;
    ///
    ///     clk.prepare_enable()?;
    ///
    ///     let expected_rate = Hertz::from_ghz(1);
    ///
    ///     if clk.rate() != expected_rate {
    ///         clk.set_rate(expected_rate)?;
    ///     }
    ///
    ///     clk.disable_unprepare();
    ///     Ok(())
    /// }
    /// ```
    ///
    /// [`struct clk`]: https://docs.kernel.org/driver-api/clk.html
    pub struct OptionalClk(Clk);

    impl OptionalClk {
        /// Gets [`OptionalClk`] corresponding to a [`Device`] and a connection id.
        ///
        /// Equivalent to the kernel's [`clk_get_optional`] API.
        ///
        /// [`clk_get_optional`]:
        /// https://docs.kernel.org/core-api/kernel-api.html#c.clk_get_optional
        pub fn get(dev: &Device, name: Option<&CStr>) -> Result<Self> {
            let con_id = name.map_or(ptr::null(), |n| n.as_ptr());

            // SAFETY: It is safe to call [`clk_get_optional`] for a valid device pointer.
            //
            // INVARIANT: The reference-count is decremented when [`OptionalClk`] goes out of
            // scope.
            Ok(Self(Clk(from_err_ptr(unsafe {
                bindings::clk_get_optional(dev.as_raw(), con_id)
            })?)))
        }
    }

    // Make [`OptionalClk`] behave like [`Clk`].
    impl Deref for OptionalClk {
        type Target = Clk;

        fn deref(&self) -> &Clk {
            &self.0
        }
    }
}

#[cfg(CONFIG_COMMON_CLK)]
pub use common_clk::*;
