// SPDX-License-Identifier: GPL-2.0

//! Intrusive high resolution timers.
//!
//! Allows running timer callbacks without doing allocations at the time of
//! starting the timer. For now, only one timer per type is allowed.
//!
//! # Vocabulary
//!
//! States:
//!
//! - Stopped: initialized but not started, or cancelled, or not restarted.
//! - Started: initialized and started or restarted.
//! - Running: executing the callback.
//!
//! Operations:
//!
//! * Start
//! * Cancel
//! * Restart
//!
//! Events:
//!
//! * Expire
//!
//! ## State Diagram
//!
//! ```text
//!                                                   Return NoRestart
//!                       +---------------------------------------------------------------------+
//!                       |                                                                     |
//!                       |                                                                     |
//!                       |                                                                     |
//!                       |                                         Return Restart              |
//!                       |                                      +------------------------+     |
//!                       |                                      |                        |     |
//!                       |                                      |                        |     |
//!                       v                                      v                        |     |
//!           +-----------------+      Start      +------------------+           +--------+-----+--+
//!           |                 +---------------->|                  |           |                 |
//! Init      |                 |                 |                  |  Expire   |                 |
//! --------->|    Stopped      |                 |      Started     +---------->|     Running     |
//!           |                 |     Cancel      |                  |           |                 |
//!           |                 |<----------------+                  |           |                 |
//!           +-----------------+                 +---------------+--+           +-----------------+
//!                                                     ^         |
//!                                                     |         |
//!                                                     +---------+
//!                                                      Restart
//! ```
//!
//!
//! A timer is initialized in the **stopped** state. A stopped timer can be
//! **started** by the `start` operation, with an **expiry** time. After the
//! `start` operation, the timer is in the **started** state. When the timer
//! **expires**, the timer enters the **running** state and the handler is
//! executed. After the handler has returned, the timer may enter the
//! **started* or **stopped** state, depending on the return value of the
//! handler. A timer in the **started** or **running** state may be **canceled**
//! by the `cancel` operation. A timer that is cancelled enters the **stopped**
//! state.
//!
//! A `cancel` or `restart` operation on a timer in the **running** state takes
//! effect after the handler has returned and the timer has transitioned
//! out of the **running** state.
//!
//! A `restart` operation on a timer in the **stopped** state is equivalent to a
//! `start` operation.

use super::ClockId;
use crate::{prelude::*, types::Opaque};
use core::marker::PhantomData;
use pin_init::PinInit;

/// A Rust wrapper around a `ktime_t`.
// NOTE: Ktime is going to be removed when hrtimer is converted to Instant/Delta.
#[repr(transparent)]
#[derive(Copy, Clone, PartialEq, PartialOrd, Eq, Ord)]
pub struct Ktime {
    inner: bindings::ktime_t,
}

impl Ktime {
    /// Returns the number of nanoseconds.
    #[inline]
    pub fn to_ns(self) -> i64 {
        self.inner
    }
}

/// A timer backed by a C `struct hrtimer`.
///
/// # Invariants
///
/// * `self.timer` is initialized by `bindings::hrtimer_setup`.
#[pin_data]
#[repr(C)]
pub struct HrTimer<T> {
    #[pin]
    timer: Opaque<bindings::hrtimer>,
    mode: HrTimerMode,
    _t: PhantomData<T>,
}

// SAFETY: Ownership of an `HrTimer` can be moved to other threads and
// used/dropped from there.
unsafe impl<T> Send for HrTimer<T> {}

// SAFETY: Timer operations are locked on the C side, so it is safe to operate
// on a timer from multiple threads.
unsafe impl<T> Sync for HrTimer<T> {}

impl<T> HrTimer<T> {
    /// Return an initializer for a new timer instance.
    pub fn new(mode: HrTimerMode, clock: ClockId) -> impl PinInit<Self>
    where
        T: HrTimerCallback,
    {
        pin_init!(Self {
            // INVARIANT: We initialize `timer` with `hrtimer_setup` below.
            timer <- Opaque::ffi_init(move |place: *mut bindings::hrtimer| {
                // SAFETY: By design of `pin_init!`, `place` is a pointer to a
                // live allocation. hrtimer_setup will initialize `place` and
                // does not require `place` to be initialized prior to the call.
                unsafe {
                    bindings::hrtimer_setup(
                        place,
                        Some(T::Pointer::run),
                        clock.into_c(),
                        mode.into_c(),
                    );
                }
            }),
            mode: mode,
            _t: PhantomData,
        })
    }

    /// Get a pointer to the contained `bindings::hrtimer`.
    ///
    /// This function is useful to get access to the value without creating
    /// intermediate references.
    ///
    /// # Safety
    ///
    /// `this` must point to a live allocation of at least the size of `Self`.
    unsafe fn raw_get(this: *const Self) -> *mut bindings::hrtimer {
        // SAFETY: The field projection to `timer` does not go out of bounds,
        // because the caller of this function promises that `this` points to an
        // allocation of at least the size of `Self`.
        unsafe { Opaque::raw_get(core::ptr::addr_of!((*this).timer)) }
    }

    /// Cancel an initialized and potentially running timer.
    ///
    /// If the timer handler is running, this function will block until the
    /// handler returns.
    ///
    /// Note that the timer might be started by a concurrent start operation. If
    /// so, the timer might not be in the **stopped** state when this function
    /// returns.
    ///
    /// Users of the `HrTimer` API would not usually call this method directly.
    /// Instead they would use the safe [`HrTimerHandle::cancel`] on the handle
    /// returned when the timer was started.
    ///
    /// This function is useful to get access to the value without creating
    /// intermediate references.
    ///
    /// # Safety
    ///
    /// `this` must point to a valid `Self`.
    pub(crate) unsafe fn raw_cancel(this: *const Self) -> bool {
        // SAFETY: `this` points to an allocation of at least `HrTimer` size.
        let c_timer_ptr = unsafe { HrTimer::raw_get(this) };

        // If the handler is running, this will wait for the handler to return
        // before returning.
        // SAFETY: `c_timer_ptr` is initialized and valid. Synchronization is
        // handled on the C side.
        unsafe { bindings::hrtimer_cancel(c_timer_ptr) != 0 }
    }
}

/// Implemented by pointer types that point to structs that contain a [`HrTimer`].
///
/// `Self` must be [`Sync`] because it is passed to timer callbacks in another
/// thread of execution (hard or soft interrupt context).
///
/// Starting a timer returns a [`HrTimerHandle`] that can be used to manipulate
/// the timer. Note that it is OK to call the start function repeatedly, and
/// that more than one [`HrTimerHandle`] associated with a [`HrTimerPointer`] may
/// exist. A timer can be manipulated through any of the handles, and a handle
/// may represent a cancelled timer.
pub trait HrTimerPointer: Sync + Sized {
    /// A handle representing a started or restarted timer.
    ///
    /// If the timer is running or if the timer callback is executing when the
    /// handle is dropped, the drop method of [`HrTimerHandle`] should not return
    /// until the timer is stopped and the callback has completed.
    ///
    /// Note: When implementing this trait, consider that it is not unsafe to
    /// leak the handle.
    type TimerHandle: HrTimerHandle;

    /// Start the timer with expiry after `expires` time units. If the timer was
    /// already running, it is restarted with the new expiry time.
    fn start(self, expires: Ktime) -> Self::TimerHandle;
}

/// Unsafe version of [`HrTimerPointer`] for situations where leaking the
/// [`HrTimerHandle`] returned by `start` would be unsound. This is the case for
/// stack allocated timers.
///
/// Typical implementers are pinned references such as [`Pin<&T>`].
///
/// # Safety
///
/// Implementers of this trait must ensure that instances of types implementing
/// [`UnsafeHrTimerPointer`] outlives any associated [`HrTimerPointer::TimerHandle`]
/// instances.
pub unsafe trait UnsafeHrTimerPointer: Sync + Sized {
    /// A handle representing a running timer.
    ///
    /// # Safety
    ///
    /// If the timer is running, or if the timer callback is executing when the
    /// handle is dropped, the drop method of [`Self::TimerHandle`] must not return
    /// until the timer is stopped and the callback has completed.
    type TimerHandle: HrTimerHandle;

    /// Start the timer after `expires` time units. If the timer was already
    /// running, it is restarted at the new expiry time.
    ///
    /// # Safety
    ///
    /// Caller promises keep the timer structure alive until the timer is dead.
    /// Caller can ensure this by not leaking the returned [`Self::TimerHandle`].
    unsafe fn start(self, expires: Ktime) -> Self::TimerHandle;
}

/// A trait for stack allocated timers.
///
/// # Safety
///
/// Implementers must ensure that `start_scoped` does not return until the
/// timer is dead and the timer handler is not running.
pub unsafe trait ScopedHrTimerPointer {
    /// Start the timer to run after `expires` time units and immediately
    /// after call `f`. When `f` returns, the timer is cancelled.
    fn start_scoped<T, F>(self, expires: Ktime, f: F) -> T
    where
        F: FnOnce() -> T;
}

// SAFETY: By the safety requirement of [`UnsafeHrTimerPointer`], dropping the
// handle returned by [`UnsafeHrTimerPointer::start`] ensures that the timer is
// killed.
unsafe impl<T> ScopedHrTimerPointer for T
where
    T: UnsafeHrTimerPointer,
{
    fn start_scoped<U, F>(self, expires: Ktime, f: F) -> U
    where
        F: FnOnce() -> U,
    {
        // SAFETY: We drop the timer handle below before returning.
        let handle = unsafe { UnsafeHrTimerPointer::start(self, expires) };
        let t = f();
        drop(handle);
        t
    }
}

/// Implemented by [`HrTimerPointer`] implementers to give the C timer callback a
/// function to call.
// This is split from `HrTimerPointer` to make it easier to specify trait bounds.
pub trait RawHrTimerCallback {
    /// Type of the parameter passed to [`HrTimerCallback::run`]. It may be
    /// [`Self`], or a pointer type derived from [`Self`].
    type CallbackTarget<'a>;

    /// Callback to be called from C when timer fires.
    ///
    /// # Safety
    ///
    /// Only to be called by C code in the `hrtimer` subsystem. `this` must point
    /// to the `bindings::hrtimer` structure that was used to start the timer.
    unsafe extern "C" fn run(this: *mut bindings::hrtimer) -> bindings::hrtimer_restart;
}

/// Implemented by structs that can be the target of a timer callback.
pub trait HrTimerCallback {
    /// The type whose [`RawHrTimerCallback::run`] method will be invoked when
    /// the timer expires.
    type Pointer<'a>: RawHrTimerCallback;

    /// Called by the timer logic when the timer fires.
    fn run(this: <Self::Pointer<'_> as RawHrTimerCallback>::CallbackTarget<'_>) -> HrTimerRestart
    where
        Self: Sized;
}

/// A handle representing a potentially running timer.
///
/// More than one handle representing the same timer might exist.
///
/// # Safety
///
/// When dropped, the timer represented by this handle must be cancelled, if it
/// is running. If the timer handler is running when the handle is dropped, the
/// drop method must wait for the handler to return before returning.
///
/// Note: One way to satisfy the safety requirement is to call `Self::cancel` in
/// the drop implementation for `Self.`
pub unsafe trait HrTimerHandle {
    /// Cancel the timer. If the timer is in the running state, block till the
    /// handler has returned.
    ///
    /// Note that the timer might be started by a concurrent start operation. If
    /// so, the timer might not be in the **stopped** state when this function
    /// returns.
    fn cancel(&mut self) -> bool;
}

/// Implemented by structs that contain timer nodes.
///
/// Clients of the timer API would usually safely implement this trait by using
/// the [`crate::impl_has_hr_timer`] macro.
///
/// # Safety
///
/// Implementers of this trait must ensure that the implementer has a
/// [`HrTimer`] field and that all trait methods are implemented according to
/// their documentation. All the methods of this trait must operate on the same
/// field.
pub unsafe trait HasHrTimer<T> {
    /// Return a pointer to the [`HrTimer`] within `Self`.
    ///
    /// This function is useful to get access to the value without creating
    /// intermediate references.
    ///
    /// # Safety
    ///
    /// `this` must be a valid pointer.
    unsafe fn raw_get_timer(this: *const Self) -> *const HrTimer<T>;

    /// Return a pointer to the struct that is containing the [`HrTimer`] pointed
    /// to by `ptr`.
    ///
    /// This function is useful to get access to the value without creating
    /// intermediate references.
    ///
    /// # Safety
    ///
    /// `ptr` must point to a [`HrTimer<T>`] field in a struct of type `Self`.
    unsafe fn timer_container_of(ptr: *mut HrTimer<T>) -> *mut Self
    where
        Self: Sized;

    /// Get pointer to the contained `bindings::hrtimer` struct.
    ///
    /// This function is useful to get access to the value without creating
    /// intermediate references.
    ///
    /// # Safety
    ///
    /// `this` must be a valid pointer.
    unsafe fn c_timer_ptr(this: *const Self) -> *const bindings::hrtimer {
        // SAFETY: `this` is a valid pointer to a `Self`.
        let timer_ptr = unsafe { Self::raw_get_timer(this) };

        // SAFETY: timer_ptr points to an allocation of at least `HrTimer` size.
        unsafe { HrTimer::raw_get(timer_ptr) }
    }

    /// Start the timer contained in the `Self` pointed to by `self_ptr`. If
    /// it is already running it is removed and inserted.
    ///
    /// # Safety
    ///
    /// - `this` must point to a valid `Self`.
    /// - Caller must ensure that the pointee of `this` lives until the timer
    ///   fires or is canceled.
    unsafe fn start(this: *const Self, expires: Ktime) {
        // SAFETY: By function safety requirement, `this` is a valid `Self`.
        unsafe {
            bindings::hrtimer_start_range_ns(
                Self::c_timer_ptr(this).cast_mut(),
                expires.to_ns(),
                0,
                (*Self::raw_get_timer(this)).mode.into_c(),
            );
        }
    }
}

/// Restart policy for timers.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
#[repr(u32)]
pub enum HrTimerRestart {
    /// Timer should not be restarted.
    NoRestart = bindings::hrtimer_restart_HRTIMER_NORESTART,
    /// Timer should be restarted.
    Restart = bindings::hrtimer_restart_HRTIMER_RESTART,
}

impl HrTimerRestart {
    fn into_c(self) -> bindings::hrtimer_restart {
        self as bindings::hrtimer_restart
    }
}

/// Operational mode of [`HrTimer`].
// NOTE: Some of these have the same encoding on the C side, so we keep
// `repr(Rust)` and convert elsewhere.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum HrTimerMode {
    /// Timer expires at the given expiration time.
    Absolute,
    /// Timer expires after the given expiration time interpreted as a duration from now.
    Relative,
    /// Timer does not move between CPU cores.
    Pinned,
    /// Timer handler is executed in soft irq context.
    Soft,
    /// Timer handler is executed in hard irq context.
    Hard,
    /// Timer expires at the given expiration time.
    /// Timer does not move between CPU cores.
    AbsolutePinned,
    /// Timer expires after the given expiration time interpreted as a duration from now.
    /// Timer does not move between CPU cores.
    RelativePinned,
    /// Timer expires at the given expiration time.
    /// Timer handler is executed in soft irq context.
    AbsoluteSoft,
    /// Timer expires after the given expiration time interpreted as a duration from now.
    /// Timer handler is executed in soft irq context.
    RelativeSoft,
    /// Timer expires at the given expiration time.
    /// Timer does not move between CPU cores.
    /// Timer handler is executed in soft irq context.
    AbsolutePinnedSoft,
    /// Timer expires after the given expiration time interpreted as a duration from now.
    /// Timer does not move between CPU cores.
    /// Timer handler is executed in soft irq context.
    RelativePinnedSoft,
    /// Timer expires at the given expiration time.
    /// Timer handler is executed in hard irq context.
    AbsoluteHard,
    /// Timer expires after the given expiration time interpreted as a duration from now.
    /// Timer handler is executed in hard irq context.
    RelativeHard,
    /// Timer expires at the given expiration time.
    /// Timer does not move between CPU cores.
    /// Timer handler is executed in hard irq context.
    AbsolutePinnedHard,
    /// Timer expires after the given expiration time interpreted as a duration from now.
    /// Timer does not move between CPU cores.
    /// Timer handler is executed in hard irq context.
    RelativePinnedHard,
}

impl HrTimerMode {
    fn into_c(self) -> bindings::hrtimer_mode {
        use bindings::*;
        match self {
            HrTimerMode::Absolute => hrtimer_mode_HRTIMER_MODE_ABS,
            HrTimerMode::Relative => hrtimer_mode_HRTIMER_MODE_REL,
            HrTimerMode::Pinned => hrtimer_mode_HRTIMER_MODE_PINNED,
            HrTimerMode::Soft => hrtimer_mode_HRTIMER_MODE_SOFT,
            HrTimerMode::Hard => hrtimer_mode_HRTIMER_MODE_HARD,
            HrTimerMode::AbsolutePinned => hrtimer_mode_HRTIMER_MODE_ABS_PINNED,
            HrTimerMode::RelativePinned => hrtimer_mode_HRTIMER_MODE_REL_PINNED,
            HrTimerMode::AbsoluteSoft => hrtimer_mode_HRTIMER_MODE_ABS_SOFT,
            HrTimerMode::RelativeSoft => hrtimer_mode_HRTIMER_MODE_REL_SOFT,
            HrTimerMode::AbsolutePinnedSoft => hrtimer_mode_HRTIMER_MODE_ABS_PINNED_SOFT,
            HrTimerMode::RelativePinnedSoft => hrtimer_mode_HRTIMER_MODE_REL_PINNED_SOFT,
            HrTimerMode::AbsoluteHard => hrtimer_mode_HRTIMER_MODE_ABS_HARD,
            HrTimerMode::RelativeHard => hrtimer_mode_HRTIMER_MODE_REL_HARD,
            HrTimerMode::AbsolutePinnedHard => hrtimer_mode_HRTIMER_MODE_ABS_PINNED_HARD,
            HrTimerMode::RelativePinnedHard => hrtimer_mode_HRTIMER_MODE_REL_PINNED_HARD,
        }
    }
}

/// Use to implement the [`HasHrTimer<T>`] trait.
///
/// See [`module`] documentation for an example.
///
/// [`module`]: crate::time::hrtimer
#[macro_export]
macro_rules! impl_has_hr_timer {
    (
        impl$({$($generics:tt)*})?
            HasHrTimer<$timer_type:ty>
            for $self:ty
        { self.$field:ident }
        $($rest:tt)*
    ) => {
        // SAFETY: This implementation of `raw_get_timer` only compiles if the
        // field has the right type.
        unsafe impl$(<$($generics)*>)? $crate::time::hrtimer::HasHrTimer<$timer_type> for $self {

            #[inline]
            unsafe fn raw_get_timer(
                this: *const Self,
            ) -> *const $crate::time::hrtimer::HrTimer<$timer_type> {
                // SAFETY: The caller promises that the pointer is not dangling.
                unsafe { ::core::ptr::addr_of!((*this).$field) }
            }

            #[inline]
            unsafe fn timer_container_of(
                ptr: *mut $crate::time::hrtimer::HrTimer<$timer_type>,
            ) -> *mut Self {
                // SAFETY: As per the safety requirement of this function, `ptr`
                // is pointing inside a `$timer_type`.
                unsafe { ::kernel::container_of!(ptr, $timer_type, $field) }
            }
        }
    }
}

mod arc;
pub use arc::ArcHrTimerHandle;
mod pin;
pub use pin::PinHrTimerHandle;
mod pin_mut;
pub use pin_mut::PinMutHrTimerHandle;
// `box` is a reserved keyword, so prefix with `t` for timer
mod tbox;
pub use tbox::BoxHrTimerHandle;
