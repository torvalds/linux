// SPDX-License-Identifier: GPL-2.0

//! Time related primitives.
//!
//! This module contains the kernel APIs related to time and timers that
//! have been ported or wrapped for usage by Rust code in the kernel.
//!
//! C header: [`include/linux/jiffies.h`](srctree/include/linux/jiffies.h).
//! C header: [`include/linux/ktime.h`](srctree/include/linux/ktime.h).

pub mod hrtimer;

/// The number of nanoseconds per millisecond.
pub const NSEC_PER_MSEC: i64 = bindings::NSEC_PER_MSEC as i64;

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

/// A Rust wrapper around a `ktime_t`.
#[repr(transparent)]
#[derive(Copy, Clone)]
pub struct Ktime {
    inner: bindings::ktime_t,
}

impl Ktime {
    /// Create a `Ktime` from a raw `ktime_t`.
    #[inline]
    pub fn from_raw(inner: bindings::ktime_t) -> Self {
        Self { inner }
    }

    /// Get the current time using `CLOCK_MONOTONIC`.
    #[inline]
    pub fn ktime_get() -> Self {
        // SAFETY: It is always safe to call `ktime_get` outside of NMI context.
        Self::from_raw(unsafe { bindings::ktime_get() })
    }

    /// Divide the number of nanoseconds by a compile-time constant.
    #[inline]
    fn divns_constant<const DIV: i64>(self) -> i64 {
        self.to_ns() / DIV
    }

    /// Returns the number of nanoseconds.
    #[inline]
    pub fn to_ns(self) -> i64 {
        self.inner
    }

    /// Returns the number of milliseconds.
    #[inline]
    pub fn to_ms(self) -> i64 {
        self.divns_constant::<NSEC_PER_MSEC>()
    }
}

/// Returns the number of milliseconds between two ktimes.
#[inline]
pub fn ktime_ms_delta(later: Ktime, earlier: Ktime) -> i64 {
    (later - earlier).to_ms()
}

impl core::ops::Sub for Ktime {
    type Output = Ktime;

    #[inline]
    fn sub(self, other: Ktime) -> Ktime {
        Self {
            inner: self.inner - other.inner,
        }
    }
}

/// An identifier for a clock. Used when specifying clock sources.
///
///
/// Selection of the clock depends on the use case. In some cases the usage of a
/// particular clock is mandatory, e.g. in network protocols, filesystems.In other
/// cases the user of the clock has to decide which clock is best suited for the
/// purpose. In most scenarios clock [`ClockId::Monotonic`] is the best choice as it
/// provides a accurate monotonic notion of time (leap second smearing ignored).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u32)]
pub enum ClockId {
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
    RealTime = bindings::CLOCK_REALTIME,
    /// A monotonically increasing clock.
    ///
    /// A nonsettable system-wide clock that represents monotonic time since—as
    /// described by POSIX—"some unspecified point in the past". On Linux, that
    /// point corresponds to the number of seconds that the system has been
    /// running since it was booted.
    ///
    /// The CLOCK_MONOTONIC clock is not affected by discontinuous jumps in the
    /// CLOCK_REAL (e.g., if the system administrator manually changes the
    /// clock), but is affected by frequency adjustments. This clock does not
    /// count time that the system is suspended.
    Monotonic = bindings::CLOCK_MONOTONIC,
    /// A monotonic that ticks while system is suspended.
    ///
    /// A nonsettable system-wide clock that is identical to CLOCK_MONOTONIC,
    /// except that it also includes any time that the system is suspended. This
    /// allows applications to get a suspend-aware monotonic clock without
    /// having to deal with the complications of CLOCK_REALTIME, which may have
    /// discontinuities if the time is changed using settimeofday(2) or similar.
    BootTime = bindings::CLOCK_BOOTTIME,
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
    TAI = bindings::CLOCK_TAI,
}

impl ClockId {
    fn into_c(self) -> bindings::clockid_t {
        self as bindings::clockid_t
    }
}
