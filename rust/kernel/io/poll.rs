// SPDX-License-Identifier: GPL-2.0

//! IO polling.
//!
//! C header: [`include/linux/iopoll.h`](srctree/include/linux/iopoll.h).

use crate::{
    error::{code::*, Result},
    processor::cpu_relax,
    task::might_sleep,
    time::{delay::fsleep, Delta, Instant, Monotonic},
};

/// Polls periodically until a condition is met, an error occurs,
/// or the timeout is reached.
///
/// The function repeatedly executes the given operation `op` closure and
/// checks its result using the condition closure `cond`.
///
/// If `cond` returns `true`, the function returns successfully with
/// the result of `op`. Otherwise, it waits for a duration specified
/// by `sleep_delta` before executing `op` again.
///
/// This process continues until either `op` returns an error, `cond`
/// returns `true`, or the timeout specified by `timeout_delta` is
/// reached.
///
/// This function can only be used in a nonatomic context.
///
/// # Errors
///
/// If `op` returns an error, then that error is returned directly.
///
/// If the timeout specified by `timeout_delta` is reached, then
/// `Err(ETIMEDOUT)` is returned.
///
/// # Examples
///
/// ```no_run
/// use kernel::io::{Io, poll::read_poll_timeout};
/// use kernel::time::Delta;
///
/// const HW_READY: u16 = 0x01;
///
/// fn wait_for_hardware<const SIZE: usize>(io: &Io<SIZE>) -> Result<()> {
///     match read_poll_timeout(
///         // The `op` closure reads the value of a specific status register.
///         || io.try_read16(0x1000),
///         // The `cond` closure takes a reference to the value returned by `op`
///         // and checks whether the hardware is ready.
///         |val: &u16| *val == HW_READY,
///         Delta::from_millis(50),
///         Delta::from_secs(3),
///     ) {
///         Ok(_) => {
///             // The hardware is ready. The returned value of the `op` closure
///             // isn't used.
///             Ok(())
///         }
///         Err(e) => Err(e),
///     }
/// }
/// ```
#[track_caller]
pub fn read_poll_timeout<Op, Cond, T>(
    mut op: Op,
    mut cond: Cond,
    sleep_delta: Delta,
    timeout_delta: Delta,
) -> Result<T>
where
    Op: FnMut() -> Result<T>,
    Cond: FnMut(&T) -> bool,
{
    let start: Instant<Monotonic> = Instant::now();

    // Unlike the C version, we always call `might_sleep()` unconditionally,
    // as conditional calls are error-prone. We clearly separate
    // `read_poll_timeout()` and `read_poll_timeout_atomic()` to aid
    // tools like klint.
    might_sleep();

    loop {
        let val = op()?;
        if cond(&val) {
            // Unlike the C version, we immediately return.
            // We know the condition is met so we don't need to check again.
            return Ok(val);
        }

        if start.elapsed() > timeout_delta {
            // Unlike the C version, we immediately return.
            // We have just called `op()` so we don't need to call it again.
            return Err(ETIMEDOUT);
        }

        if !sleep_delta.is_zero() {
            fsleep(sleep_delta);
        }

        // `fsleep()` could be a busy-wait loop so we always call `cpu_relax()`.
        cpu_relax();
    }
}
