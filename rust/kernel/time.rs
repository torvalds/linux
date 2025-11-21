// SPDX-License-Identifier: GPL-2.0

//! Time related primitives.
//!
//! This module contains the kernel APIs related to time and timers that
//! have been ported or wrapped for usage by Rust code in the kernel.
//!
//! There are two types in this module:
//!
//! - The [`Instant`] type represents a specific point in time.
//! - The [`Delta`] type represents a span of time.
//!
//! Note that the C side uses `ktime_t` type to represent both. However, timestamp
//! and timedelta are different. To avoid confusion, we use two different types.
//!
//! A [`Instant`] object can be created by calling the [`Instant::now()`] function.
//! It represents a point in time at which the object was created.
//! By calling the [`Instant::elapsed()`] method, a [`Delta`] object representing
//! the elapsed time can be created. The [`Delta`] object can also be created
//! by subtracting two [`Instant`] objects.
//!
//! A [`Delta`] type supports methods to retrieve the duration in various units.
//!
//! C header: [`include/linux/jiffies.h`](srctree/include/linux/jiffies.h).
//! C header: [`include/linux/ktime.h`](srctree/include/linux/ktime.h).

use core::marker::PhantomData;
use core::ops;

pub mod delay;
pub mod hrtimer;

/// The number of nanoseconds per microsecond.
pub const NSEC_PER_USEC: i64 = bindings::NSEC_PER_USEC as i64;

/// The number of nanoseconds per millisecond.
pub const NSEC_PER_MSEC: i64 = bindings::NSEC_PER_MSEC as i64;

/// The number of nanoseconds per second.
pub const NSEC_PER_SEC: i64 = bindings::NSEC_PER_SEC as i64;

/// The time unit of Linux kernel. One jiffy equals (1/HZ) second.
pub type Jiffies = crate::ffi::c_ulong;

/// The millisecond time unit.
pub type Msecs = crate::ffi::c_uint;

/// Converts milliseconds to jiffies.
#[inline]
pub fn msecs_to_jiffies(msecs: Msecs) -> Jiffies {
    // SAFETY: The `__msecs_to_jiffies` function is always safe to call no
    // matter what the argument is.
    unsafe { bindings::__msecs_to_jiffies(msecs) }
}

/// Trait for clock sources.
///
/// Selection of the clock source depends on the use case. In some cases the usage of a
/// particular clock is mandatory, e.g. in network protocols, filesystems. In other
/// cases the user of the clock has to decide which clock is best suited for the
/// purpose. In most scenarios clock [`Monotonic`] is the best choice as it
/// provides a accurate monotonic notion of time (leap second smearing ignored).
pub trait ClockSource {
    /// The kernel clock ID associated with this clock source.
    ///
    /// This constant corresponds to the C side `clockid_t` value.
    const ID: bindings::clockid_t;

    /// Get the current time from the clock source.
    ///
    /// The function must return a value in the range from 0 to `KTIME_MAX`.
    fn ktime_get() -> bindings::ktime_t;
}

/// A monotonically increasing clock.
///
/// A nonsettable system-wide clock that represents monotonic time since as
/// described by POSIX, "some unspecified point in the past". On Linux, that
/// point corresponds to the number of seconds that the system has been
/// running since it was booted.
///
/// The CLOCK_MONOTONIC clock is not affected by discontinuous jumps in the
/// CLOCK_REAL (e.g., if the system administrator manually changes the
/// clock), but is affected by frequency adjustments. This clock does not
/// count time that the system is suspended.
pub struct Monotonic;

impl ClockSource for Monotonic {
    const ID: bindings::clockid_t = bindings::CLOCK_MONOTONIC as bindings::clockid_t;

    fn ktime_get() -> bindings::ktime_t {
        // SAFETY: It is always safe to call `ktime_get()` outside of NMI context.
        unsafe { bindings::ktime_get() }
    }
}

/// A settable system-wide clock that measures real (i.e., wall-clock) time.
///
/// Setting this clock requires appropriate privileges. This clock is
/// affected by discontinuous jumps in the system time (e.g., if the system
/// administrator manually changes the clock), and by frequency adjustments
/// performed by NTP and similar applications via adjtime(3), adjtimex(2),
/// clock_adjtime(2), and ntp_adjtime(3). This clock normally counts the
/// number of seconds since 1970-01-01 00:00:00 Coordinated Universal Time
/// (UTC) except that it ignores leap seconds; near a leap second it may be
/// adjusted by leap second smearing to stay roughly in sync with UTC. Leap
/// second smearing applies frequency adjustments to the clock to speed up
/// or slow down the clock to account for the leap second without
/// discontinuities in the clock. If leap second smearing is not applied,
/// the clock will experience discontinuity around leap second adjustment.
pub struct RealTime;

impl ClockSource for RealTime {
    const ID: bindings::clockid_t = bindings::CLOCK_REALTIME as bindings::clockid_t;

    fn ktime_get() -> bindings::ktime_t {
        // SAFETY: It is always safe to call `ktime_get_real()` outside of NMI context.
        unsafe { bindings::ktime_get_real() }
    }
}

/// A monotonic that ticks while system is suspended.
///
/// A nonsettable system-wide clock that is identical to CLOCK_MONOTONIC,
/// except that it also includes any time that the system is suspended. This
/// allows applications to get a suspend-aware monotonic clock without
/// having to deal with the complications of CLOCK_REALTIME, which may have
/// discontinuities if the time is changed using settimeofday(2) or similar.
pub struct BootTime;

impl ClockSource for BootTime {
    const ID: bindings::clockid_t = bindings::CLOCK_BOOTTIME as bindings::clockid_t;

    fn ktime_get() -> bindings::ktime_t {
        // SAFETY: It is always safe to call `ktime_get_boottime()` outside of NMI context.
        unsafe { bindings::ktime_get_boottime() }
    }
}

/// International Atomic Time.
///
/// A system-wide clock derived from wall-clock time but counting leap seconds.
///
/// This clock is coupled to CLOCK_REALTIME and will be set when CLOCK_REALTIME is
/// set, or when the offset to CLOCK_REALTIME is changed via adjtimex(2). This
/// usually happens during boot and **should** not happen during normal operations.
/// However, if NTP or another application adjusts CLOCK_REALTIME by leap second
/// smearing, this clock will not be precise during leap second smearing.
///
/// The acronym TAI refers to International Atomic Time.
pub struct Tai;

impl ClockSource for Tai {
    const ID: bindings::clockid_t = bindings::CLOCK_TAI as bindings::clockid_t;

    fn ktime_get() -> bindings::ktime_t {
        // SAFETY: It is always safe to call `ktime_get_tai()` outside of NMI context.
        unsafe { bindings::ktime_get_clocktai() }
    }
}

/// A specific point in time.
///
/// # Invariants
///
/// The `inner` value is in the range from 0 to `KTIME_MAX`.
#[repr(transparent)]
#[derive(PartialEq, PartialOrd, Eq, Ord)]
pub struct Instant<C: ClockSource> {
    inner: bindings::ktime_t,
    _c: PhantomData<C>,
}

impl<C: ClockSource> Clone for Instant<C> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<C: ClockSource> Copy for Instant<C> {}

impl<C: ClockSource> Instant<C> {
    /// Get the current time from the clock source.
    #[inline]
    pub fn now() -> Self {
        // INVARIANT: The `ClockSource::ktime_get()` function returns a value in the range
        // from 0 to `KTIME_MAX`.
        Self {
            inner: C::ktime_get(),
            _c: PhantomData,
        }
    }

    /// Return the amount of time elapsed since the [`Instant`].
    #[inline]
    pub fn elapsed(&self) -> Delta {
        Self::now() - *self
    }

    #[inline]
    pub(crate) fn as_nanos(&self) -> i64 {
        self.inner
    }

    /// Create an [`Instant`] from a `ktime_t` without checking if it is non-negative.
    ///
    /// # Panics
    ///
    /// On debug builds, this function will panic if `ktime` is not in the range from 0 to
    /// `KTIME_MAX`.
    ///
    /// # Safety
    ///
    /// The caller promises that `ktime` is in the range from 0 to `KTIME_MAX`.
    #[inline]
    pub(crate) unsafe fn from_ktime(ktime: bindings::ktime_t) -> Self {
        debug_assert!(ktime >= 0);

        // INVARIANT: Our safety contract ensures that `ktime` is in the range from 0 to
        // `KTIME_MAX`.
        Self {
            inner: ktime,
            _c: PhantomData,
        }
    }
}

impl<C: ClockSource> ops::Sub for Instant<C> {
    type Output = Delta;

    // By the type invariant, it never overflows.
    #[inline]
    fn sub(self, other: Instant<C>) -> Delta {
        Delta {
            nanos: self.inner - other.inner,
        }
    }
}

impl<T: ClockSource> ops::Add<Delta> for Instant<T> {
    type Output = Self;

    #[inline]
    fn add(self, rhs: Delta) -> Self::Output {
        // INVARIANT: With arithmetic over/underflow checks enabled, this will panic if we overflow
        // (e.g. go above `KTIME_MAX`)
        let res = self.inner + rhs.nanos;

        // INVARIANT: With overflow checks enabled, we verify here that the value is >= 0
        #[cfg(CONFIG_RUST_OVERFLOW_CHECKS)]
        assert!(res >= 0);

        Self {
            inner: res,
            _c: PhantomData,
        }
    }
}

impl<T: ClockSource> ops::Sub<Delta> for Instant<T> {
    type Output = Self;

    #[inline]
    fn sub(self, rhs: Delta) -> Self::Output {
        // INVARIANT: With arithmetic over/underflow checks enabled, this will panic if we overflow
        // (e.g. go above `KTIME_MAX`)
        let res = self.inner - rhs.nanos;

        // INVARIANT: With overflow checks enabled, we verify here that the value is >= 0
        #[cfg(CONFIG_RUST_OVERFLOW_CHECKS)]
        assert!(res >= 0);

        Self {
            inner: res,
            _c: PhantomData,
        }
    }
}

/// A span of time.
///
/// This struct represents a span of time, with its value stored as nanoseconds.
/// The value can represent any valid i64 value, including negative, zero, and
/// positive numbers.
#[derive(Copy, Clone, PartialEq, PartialOrd, Eq, Ord, Debug)]
pub struct Delta {
    nanos: i64,
}

impl ops::Add for Delta {
    type Output = Self;

    #[inline]
    fn add(self, rhs: Self) -> Self {
        Self {
            nanos: self.nanos + rhs.nanos,
        }
    }
}

impl ops::AddAssign for Delta {
    #[inline]
    fn add_assign(&mut self, rhs: Self) {
        self.nanos += rhs.nanos;
    }
}

impl ops::Sub for Delta {
    type Output = Self;

    #[inline]
    fn sub(self, rhs: Self) -> Self::Output {
        Self {
            nanos: self.nanos - rhs.nanos,
        }
    }
}

impl ops::SubAssign for Delta {
    #[inline]
    fn sub_assign(&mut self, rhs: Self) {
        self.nanos -= rhs.nanos;
    }
}

impl ops::Mul<i64> for Delta {
    type Output = Self;

    #[inline]
    fn mul(self, rhs: i64) -> Self::Output {
        Self {
            nanos: self.nanos * rhs,
        }
    }
}

impl ops::MulAssign<i64> for Delta {
    #[inline]
    fn mul_assign(&mut self, rhs: i64) {
        self.nanos *= rhs;
    }
}

impl ops::Div for Delta {
    type Output = i64;

    #[inline]
    fn div(self, rhs: Self) -> Self::Output {
        #[cfg(CONFIG_64BIT)]
        {
            self.nanos / rhs.nanos
        }

        #[cfg(not(CONFIG_64BIT))]
        {
            // SAFETY: This function is always safe to call regardless of the input values
            unsafe { bindings::div64_s64(self.nanos, rhs.nanos) }
        }
    }
}

impl Delta {
    /// A span of time equal to zero.
    pub const ZERO: Self = Self { nanos: 0 };

    /// Create a new [`Delta`] from a number of microseconds.
    ///
    /// The `micros` can range from -9_223_372_036_854_775 to 9_223_372_036_854_775.
    /// If `micros` is outside this range, `i64::MIN` is used for negative values,
    /// and `i64::MAX` is used for positive values due to saturation.
    #[inline]
    pub const fn from_micros(micros: i64) -> Self {
        Self {
            nanos: micros.saturating_mul(NSEC_PER_USEC),
        }
    }

    /// Create a new [`Delta`] from a number of milliseconds.
    ///
    /// The `millis` can range from -9_223_372_036_854 to 9_223_372_036_854.
    /// If `millis` is outside this range, `i64::MIN` is used for negative values,
    /// and `i64::MAX` is used for positive values due to saturation.
    #[inline]
    pub const fn from_millis(millis: i64) -> Self {
        Self {
            nanos: millis.saturating_mul(NSEC_PER_MSEC),
        }
    }

    /// Create a new [`Delta`] from a number of seconds.
    ///
    /// The `secs` can range from -9_223_372_036 to 9_223_372_036.
    /// If `secs` is outside this range, `i64::MIN` is used for negative values,
    /// and `i64::MAX` is used for positive values due to saturation.
    #[inline]
    pub const fn from_secs(secs: i64) -> Self {
        Self {
            nanos: secs.saturating_mul(NSEC_PER_SEC),
        }
    }

    /// Return `true` if the [`Delta`] spans no time.
    #[inline]
    pub fn is_zero(self) -> bool {
        self.as_nanos() == 0
    }

    /// Return `true` if the [`Delta`] spans a negative amount of time.
    #[inline]
    pub fn is_negative(self) -> bool {
        self.as_nanos() < 0
    }

    /// Return the number of nanoseconds in the [`Delta`].
    #[inline]
    pub const fn as_nanos(self) -> i64 {
        self.nanos
    }

    /// Return the smallest number of microseconds greater than or equal
    /// to the value in the [`Delta`].
    #[inline]
    pub fn as_micros_ceil(self) -> i64 {
        #[cfg(CONFIG_64BIT)]
        {
            self.as_nanos().saturating_add(NSEC_PER_USEC - 1) / NSEC_PER_USEC
        }

        #[cfg(not(CONFIG_64BIT))]
        // SAFETY: It is always safe to call `ktime_to_us()` with any value.
        unsafe {
            bindings::ktime_to_us(self.as_nanos().saturating_add(NSEC_PER_USEC - 1))
        }
    }

    /// Return the number of milliseconds in the [`Delta`].
    #[inline]
    pub fn as_millis(self) -> i64 {
        #[cfg(CONFIG_64BIT)]
        {
            self.as_nanos() / NSEC_PER_MSEC
        }

        #[cfg(not(CONFIG_64BIT))]
        // SAFETY: It is always safe to call `ktime_to_ms()` with any value.
        unsafe {
            bindings::ktime_to_ms(self.as_nanos())
        }
    }

    /// Return `self % dividend` where `dividend` is in nanoseconds.
    ///
    /// The kernel doesn't have any emulation for `s64 % s64` on 32 bit platforms, so this is
    /// limited to 32 bit dividends.
    #[inline]
    pub fn rem_nanos(self, dividend: i32) -> Self {
        #[cfg(CONFIG_64BIT)]
        {
            Self {
                nanos: self.as_nanos() % i64::from(dividend),
            }
        }

        #[cfg(not(CONFIG_64BIT))]
        {
            let mut rem = 0;

            // SAFETY: `rem` is in the stack, so we can always provide a valid pointer to it.
            unsafe { bindings::div_s64_rem(self.as_nanos(), dividend, &mut rem) };

            Self {
                nanos: i64::from(rem),
            }
        }
    }
}
