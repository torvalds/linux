// SPDX-License-Identifier: GPL-2.0

//! Interrupt controls
//!
//! This module allows Rust code to annotate areas of code where local processor interrupts should
//! be disabled, along with actually disabling local processor interrupts.
//!
//! # ⚠️ Warning! ⚠️
//!
//! The usage of this module can be more complicated than meets the eye, especially surrounding
//! [preemptible kernels]. It's recommended to take care when using the functions and types defined
//! here and familiarize yourself with the various documentation we have before using them, along
//! with the various documents we link to here.
//!
//! # Reading material
//!
//! - [Software interrupts and realtime (LWN)](https://lwn.net/Articles/520076)
//!
//! [preemptible kernels]: https://www.kernel.org/doc/html/latest/locking/preempt-locking.html

use crate::types::NotThreadSafe;

/// A guard that represents local processor interrupt disablement on preemptible kernels.
///
/// [`LocalInterruptDisabled`] is a guard type that represents that local processor interrupts have
/// been disabled on a preemptible kernel.
///
/// Certain functions take an immutable reference of [`LocalInterruptDisabled`] in order to require
/// that they may only be run in local-interrupt-disabled contexts on preemptible kernels.
///
/// This is a marker type; it has no size, and is simply used as a compile-time guarantee that local
/// processor interrupts are disabled on preemptible kernels. Note that no guarantees about the
/// state of interrupts are made by this type on non-preemptible kernels.
///
/// # Invariants
///
/// Local processor interrupts are disabled on preemptible kernels for as long as an object of this
/// type exists.
pub struct LocalInterruptDisabled(NotThreadSafe);

/// Disable local processor interrupts on a preemptible kernel.
///
/// This function disables local processor interrupts on a preemptible kernel, and returns a
/// [`LocalInterruptDisabled`] token as proof of this. On non-preemptible kernels, this function is
/// a no-op.
///
/// **Usage of this function is discouraged** unless you are absolutely sure you know what you are
/// doing, as kernel interfaces for Rust that deal with interrupt state will typically handle local
/// processor interrupt state management on their own and managing this by hand is quite error
/// prone.
#[inline]
pub fn local_interrupt_disable() -> LocalInterruptDisabled {
    // SAFETY: It's always safe to call `local_interrupt_disable()`.
    unsafe { bindings::local_interrupt_disable() };

    LocalInterruptDisabled(NotThreadSafe)
}

impl Drop for LocalInterruptDisabled {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: Per type invariants, a `local_interrupt_disable()` must be called to create this
        // object, hence calling the corresponding `local_interrupt_enable()` is safe.
        unsafe { bindings::local_interrupt_enable() };
    }
}

impl LocalInterruptDisabled {
    /// Assume that local processor interrupts are disabled on preemptible kernels.
    ///
    /// This can be used for annotating code that is known to be run in contexts where local
    /// processor interrupts are disabled on preemptible kernels. It makes no changes to the local
    /// interrupt state on its own.
    ///
    /// # Safety
    ///
    /// For the whole life `'a`, local interrupts must be disabled on preemptible kernels. This
    /// could be a context like, for example, an interrupt handler.
    #[inline]
    pub unsafe fn assume_disabled<'a>() -> &'a LocalInterruptDisabled {
        const ASSUME_DISABLED: &LocalInterruptDisabled = &LocalInterruptDisabled(NotThreadSafe);

        // Confirm they're actually disabled if lockdep is available
        // SAFETY: It's always safe to call `lockdep_assert_irqs_disabled()`.
        unsafe { bindings::lockdep_assert_irqs_disabled() };

        ASSUME_DISABLED
    }
}
