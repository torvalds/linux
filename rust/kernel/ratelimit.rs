// SPDX-License-Identifier: GPL-2.0

//! Rate limiting support.
//!
//! C header: [`include/linux/ratelimit.h`](srctree/include/linux/ratelimit.h)

use crate::{
    bindings,
    prelude::*,
    types::Opaque, //
};

/// Defines a `static` containing a [`Ratelimit`].
#[macro_export]
macro_rules! ratelimit_state_init {
    ($name:ident, $interval:expr, $burst:expr $(,)?) => {
        #[allow(non_upper_case_globals)]
        static $name: $crate::ratelimit::Ratelimit = {
            let name = $crate::c_str!(::core::stringify!($name));
            let interval = $interval;
            let burst = $burst;
            // SAFETY: This will be stored in static memory.
            unsafe { $crate::ratelimit::Ratelimit::new_static(name, interval, burst) }
        };
    };
}
pub use ratelimit_state_init;

/// Rate limiter state.
///
/// # Invariants
///
/// The `inner` field contains an initialized `struct ratelimit_state`.
#[pin_data(PinnedDrop)]
#[repr(transparent)]
pub struct Ratelimit {
    #[pin]
    inner: Opaque<bindings::ratelimit_state>,
}

// SAFETY: `Ratelimit` is safe to be sent to any task.
unsafe impl Send for Ratelimit {}

// SAFETY: `Ratelimit` is safe to be accessed concurrently as it is protected by an internal
// spinlock.
unsafe impl Sync for Ratelimit {}

impl Ratelimit {
    /// Constructs a [`Ratelimit`] with the specified configuration.
    ///
    /// If `interval` is zero, then no rate limit is applied.
    #[inline]
    pub fn new(interval: i32, burst: i32) -> impl PinInit<Self> {
        // INVARIANT: This creates a `Ratelimit` containing an initialized `struct ratelimit_state`
        pin_init!(Self {
            inner <- Opaque::ffi_init(|slot: *mut bindings::ratelimit_state| {
                // SAFETY: `slot` is a valid pointer to an uninitialized `struct ratelimit_state`.
                // The memory is pinned so it remains valid until `ratelimit_state_exit` is called.
                unsafe { bindings::ratelimit_state_init(slot, interval, burst) };
            }),
        })
    }

    /// Constructs a [`Ratelimit`] with the default configuration.
    #[inline]
    pub fn new_default() -> impl PinInit<Self> {
        Ratelimit::new(Ratelimit::DEFAULT_INTERVAL, Ratelimit::DEFAULT_BURST)
    }

    /// Constructs a [`Ratelimit`] with the specified configuration.
    ///
    /// The name will be used for the lockdep name of the internal spinlock. See [`Self::new`] for
    /// the meaning of `interval` and `burst`.
    ///
    /// # Safety
    ///
    /// The resulting value must be stored in static memory.
    #[inline]
    pub const unsafe fn new_static(name: &'static CStr, interval: i32, burst: i32) -> Self {
        // INVARIANT: This creates a `Ratelimit` containing an initialized `struct ratelimit_state`.
        Self {
            inner: Opaque::new(bindings::ratelimit_state {
                lock: kernel::sync::lock::spinlock::raw_spin_lock_unlocked(name),
                interval,
                burst,
                ..pin_init::zeroed()
            }),
        }
    }

    /// The default interval used for rate limiting.
    pub const DEFAULT_INTERVAL: i32 = bindings::DEFAULT_RATELIMIT_INTERVAL as i32;

    /// The default burst size.
    pub const DEFAULT_BURST: i32 = bindings::DEFAULT_RATELIMIT_BURST as i32;

    /// Check if an action should be rate-limited.
    ///
    /// Returns [`true`] if the action is allowed, and [`false`] if it should be suppressed.
    #[inline]
    pub fn ratelimit(&self, func: &CStr) -> bool {
        // SAFETY: `self.inner.get()` is a valid pointer to a `struct ratelimit_state`.
        // The lifetime of `func` ensures the pointer remains valid for the duration of the call.
        // The C function `___ratelimit` handles its own internal locking, so it is safe to call
        // concurrently.
        unsafe { bindings::___ratelimit(self.inner.get(), func.as_char_ptr()) != 0 }
    }
}

#[pinned_drop]
impl PinnedDrop for Ratelimit {
    #[inline]
    fn drop(self: Pin<&mut Self>) {
        // SAFETY: By the type invariants, this struct contains an initialized `struct
        // ratelimit_state`.
        unsafe { bindings::ratelimit_state_exit(self.inner.get()) };
    }
}

/// Helper macro to implement ratelimited printing.
#[macro_export]
#[doc(hidden)]
macro_rules! print_ratelimited {
    ($print_macro:ident, $($arg:tt)*) => {{
        $crate::ratelimit::ratelimit_state_init!(
            _rs,
            $crate::ratelimit::Ratelimit::DEFAULT_INTERVAL,
            $crate::ratelimit::Ratelimit::DEFAULT_BURST,
        );
        let loc = $crate::c_str!(::core::concat!(::core::file!(), ":", ::core::line!()));
        if $crate::ratelimit::Ratelimit::ratelimit(&_rs, loc) {
            $crate::$print_macro!($($arg)*);
        }
    }};
}

/// Prints an emergency-level message (level 0) if allowed by a rate limiter.
#[macro_export]
macro_rules! pr_emerg_ratelimited (
    ($($arg:tt)*) => (
        $crate::print_ratelimited!(pr_emerg, $($arg)*)
    )
);

/// Prints an alert-level message (level 1) if allowed by a rate limiter.
#[macro_export]
macro_rules! pr_alert_ratelimited (
    ($($arg:tt)*) => (
        $crate::print_ratelimited!(pr_alert, $($arg)*)
    )
);

/// Prints a critical-level message (level 2) if allowed by a rate limiter.
#[macro_export]
macro_rules! pr_crit_ratelimited (
    ($($arg:tt)*) => (
        $crate::print_ratelimited!(pr_crit, $($arg)*)
    )
);

/// Prints an error-level message (level 3) if allowed by a rate limiter.
#[macro_export]
macro_rules! pr_err_ratelimited (
    ($($arg:tt)*) => (
        $crate::print_ratelimited!(pr_err, $($arg)*)
    )
);

/// Prints a warning-level message (level 4) if allowed by a rate limiter.
#[macro_export]
macro_rules! pr_warn_ratelimited (
    ($($arg:tt)*) => (
        $crate::print_ratelimited!(pr_warn, $($arg)*)
    )
);

/// Prints a notice-level message (level 5) if allowed by a rate limiter.
#[macro_export]
macro_rules! pr_notice_ratelimited (
    ($($arg:tt)*) => (
        $crate::print_ratelimited!(pr_notice, $($arg)*)
    )
);

/// Prints an info-level message (level 6) if allowed by a rate limiter.
#[macro_export]
macro_rules! pr_info_ratelimited (
    ($($arg:tt)*) => (
        $crate::print_ratelimited!(pr_info, $($arg)*)
    )
);

/// Prints a debug-level message (level 7) if allowed by a rate limiter.
#[macro_export]
macro_rules! pr_debug_ratelimited (
    ($($arg:tt)*) => (
        if cfg!(debug_assertions) {
            $crate::print_ratelimited!(pr_debug, $($arg)*)
        }
    )
);
