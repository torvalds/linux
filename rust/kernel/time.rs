// SPDX-License-Identifier: GPL-2.0

//! Time related primitives.
//!
//! This module contains the kernel APIs related to time and timers that
//! have been ported or wrapped for usage by Rust code in the kernel.

/// The time unit of Linux kernel. One jiffy equals (1/HZ) second.
pub type Jiffies = core::ffi::c_ulong;

/// The millisecond time unit.
pub type Msecs = core::ffi::c_uint;

/// Converts milliseconds to jiffies.
#[inline]
pub fn msecs_to_jiffies(msecs: Msecs) -> Jiffies {
    // SAFETY: The `__msecs_to_jiffies` function is always safe to call no
    // matter what the argument is.
    unsafe { bindings::__msecs_to_jiffies(msecs) }
}
