// SPDX-License-Identifier: GPL-2.0

//! Delay and sleep primitives.
//!
//! This module contains the kernel APIs related to delay and sleep that
//! have been ported or wrapped for usage by Rust code in the kernel.
//!
//! C header: [`include/linux/delay.h`](srctree/include/linux/delay.h).

use super::Delta;
use crate::prelude::*;

/// Sleeps for a given duration at least.
///
/// Equivalent to the C side [`fsleep()`], flexible sleep function,
/// which automatically chooses the best sleep method based on a duration.
///
/// `delta` must be within `[0, i32::MAX]` microseconds;
/// otherwise, it is erroneous behavior. That is, it is considered a bug
/// to call this function with an out-of-range value, in which case the function
/// will sleep for at least the maximum value in the range and may warn
/// in the future.
///
/// The behavior above differs from the C side [`fsleep()`] for which out-of-range
/// values mean "infinite timeout" instead.
///
/// This function can only be used in a nonatomic context.
///
/// [`fsleep()`]: https://docs.kernel.org/timers/delay_sleep_functions.html#c.fsleep
pub fn fsleep(delta: Delta) {
    // The maximum value is set to `i32::MAX` microseconds to prevent integer
    // overflow inside fsleep, which could lead to unintentional infinite sleep.
    const MAX_DELTA: Delta = Delta::from_micros(i32::MAX as i64);

    let delta = if (Delta::ZERO..=MAX_DELTA).contains(&delta) {
        delta
    } else {
        // TODO: Add WARN_ONCE() when it's supported.
        MAX_DELTA
    };

    // SAFETY: It is always safe to call `fsleep()` with any duration.
    unsafe {
        // Convert the duration to microseconds and round up to preserve
        // the guarantee; `fsleep()` sleeps for at least the provided duration,
        // but that it may sleep for longer under some circumstances.
        bindings::fsleep(delta.as_micros_ceil() as c_ulong)
    }
}
