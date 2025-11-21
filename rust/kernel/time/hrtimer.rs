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

use super::{ClockSource, Delta, Instant};
use crate::{prelude::*, types::Opaque};
use core::{marker::PhantomData, ptr::NonNull};
use pin_init::PinInit;

/// A type-alias to refer to the [`Instant<C>`] for a given `T` from [`HrTimer<T>`].
///
/// Where `C` is the [`ClockSource`] of the [`HrTimer`].
pub type HrTimerInstant<T> = Instant<<<T as HasHrTimer<T>>::TimerMode as HrTimerMode>::Clock>;

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
    pub fn new() -> impl PinInit<Self>
    where
        T: HrTimerCallback,
        T: HasHrTimer<T>,
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
                        <<T as HasHrTimer<T>>::TimerMode as HrTimerMode>::Clock::ID,
                        <T as HasHrTimer<T>>::TimerMode::C_MODE,
                    );
                }
            }),
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
        unsafe { Opaque::cast_into(core::ptr::addr_of!((*this).timer)) }
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

    /// Forward the timer expiry for a given timer pointer.
    ///
    /// # Safety
    ///
    /// - `self_ptr` must point to a valid `Self`.
    /// - The caller must either have exclusive access to the data pointed at by `self_ptr`, or be
    ///   within the context of the timer callback.
    #[inline]
    unsafe fn raw_forward(self_ptr: *mut Self, now: HrTimerInstant<T>, interval: Delta) -> u64
    where
        T: HasHrTimer<T>,
    {
        // SAFETY:
        // * The C API requirements for this function are fulfilled by our safety contract.
        // * `self_ptr` is guaranteed to point to a valid `Self` via our safety contract
        unsafe {
            bindings::hrtimer_forward(Self::raw_get(self_ptr), now.as_nanos(), interval.as_nanos())
        }
    }

    /// Conditionally forward the timer.
    ///
    /// If the timer expires after `now`, this function does nothing and returns 0. If the timer
    /// expired at or before `now`, this function forwards the timer by `interval` until the timer
    /// expires after `now` and then returns the number of times the timer was forwarded by
    /// `interval`.
    ///
    /// This function is mainly useful for timer types which can provide exclusive access to the
    /// timer when the timer is not running. For forwarding the timer from within the timer callback
    /// context, see [`HrTimerCallbackContext::forward()`].
    ///
    /// Returns the number of overruns that occurred as a result of the timer expiry change.
    pub fn forward(self: Pin<&mut Self>, now: HrTimerInstant<T>, interval: Delta) -> u64
    where
        T: HasHrTimer<T>,
    {
        // SAFETY: `raw_forward` does not move `Self`
        let this = unsafe { self.get_unchecked_mut() };

        // SAFETY: By existence of `Pin<&mut Self>`, the pointer passed to `raw_forward` points to a
        // valid `Self` that we have exclusive access to.
        unsafe { Self::raw_forward(this, now, interval) }
    }

    /// Conditionally forward the timer.
    ///
    /// This is a variant of [`forward()`](Self::forward) that uses an interval after the current
    /// time of the base clock for the [`HrTimer`].
    pub fn forward_now(self: Pin<&mut Self>, interval: Delta) -> u64
    where
        T: HasHrTimer<T>,
    {
        self.forward(HrTimerInstant::<T>::now(), interval)
    }

    /// Return the time expiry for this [`HrTimer`].
    ///
    /// This value should only be used as a snapshot, as the actual expiry time could change after
    /// this function is called.
    pub fn expires(&self) -> HrTimerInstant<T>
    where
        T: HasHrTimer<T>,
    {
        // SAFETY: `self` is an immutable reference and thus always points to a valid `HrTimer`.
        let c_timer_ptr = unsafe { HrTimer::raw_get(self) };

        // SAFETY:
        // - Timers cannot have negative ktime_t values as their expiration time.
        // - There's no actual locking here, a racy read is fine and expected
        unsafe {
            Instant::from_ktime(
                // This `read_volatile` is intended to correspond to a READ_ONCE call.
                // FIXME(read_once): Replace with `read_once` when available on the Rust side.
                core::ptr::read_volatile(&raw const ((*c_timer_ptr).node.expires)),
            )
        }
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
    /// The operational mode associated with this timer.
    ///
    /// This defines how the expiration value is interpreted.
    type TimerMode: HrTimerMode;

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
    fn start(self, expires: <Self::TimerMode as HrTimerMode>::Expires) -> Self::TimerHandle;
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
    /// The operational mode associated with this timer.
    ///
    /// This defines how the expiration value is interpreted.
    type TimerMode: HrTimerMode;

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
    unsafe fn start(self, expires: <Self::TimerMode as HrTimerMode>::Expires) -> Self::TimerHandle;
}

/// A trait for stack allocated timers.
///
/// # Safety
///
/// Implementers must ensure that `start_scoped` does not return until the
/// timer is dead and the timer handler is not running.
pub unsafe trait ScopedHrTimerPointer {
    /// The operational mode associated with this timer.
    ///
    /// This defines how the expiration value is interpreted.
    type TimerMode: HrTimerMode;

    /// Start the timer to run after `expires` time units and immediately
    /// after call `f`. When `f` returns, the timer is cancelled.
    fn start_scoped<T, F>(self, expires: <Self::TimerMode as HrTimerMode>::Expires, f: F) -> T
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
    type TimerMode = T::TimerMode;

    fn start_scoped<U, F>(
        self,
        expires: <<T as UnsafeHrTimerPointer>::TimerMode as HrTimerMode>::Expires,
        f: F,
    ) -> U
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
    fn run(
        this: <Self::Pointer<'_> as RawHrTimerCallback>::CallbackTarget<'_>,
        ctx: HrTimerCallbackContext<'_, Self>,
    ) -> HrTimerRestart
    where
        Self: Sized,
        Self: HasHrTimer<Self>;
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
    ///
    /// Returns `true` if the timer was running.
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
    /// The operational mode associated with this timer.
    ///
    /// This defines how the expiration value is interpreted.
    type TimerMode: HrTimerMode;

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
    unsafe fn start(this: *const Self, expires: <Self::TimerMode as HrTimerMode>::Expires) {
        // SAFETY: By function safety requirement, `this` is a valid `Self`.
        unsafe {
            bindings::hrtimer_start_range_ns(
                Self::c_timer_ptr(this).cast_mut(),
                expires.as_nanos(),
                0,
                <Self::TimerMode as HrTimerMode>::C_MODE,
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

/// Time representations that can be used as expiration values in [`HrTimer`].
pub trait HrTimerExpires {
    /// Converts the expiration time into a nanosecond representation.
    ///
    /// This value corresponds to a raw ktime_t value, suitable for passing to kernel
    /// timer functions. The interpretation (absolute vs relative) depends on the
    /// associated [HrTimerMode] in use.
    fn as_nanos(&self) -> i64;
}

impl<C: ClockSource> HrTimerExpires for Instant<C> {
    #[inline]
    fn as_nanos(&self) -> i64 {
        Instant::<C>::as_nanos(self)
    }
}

impl HrTimerExpires for Delta {
    #[inline]
    fn as_nanos(&self) -> i64 {
        Delta::as_nanos(*self)
    }
}

mod private {
    use crate::time::ClockSource;

    pub trait Sealed {}

    impl<C: ClockSource> Sealed for super::AbsoluteMode<C> {}
    impl<C: ClockSource> Sealed for super::RelativeMode<C> {}
    impl<C: ClockSource> Sealed for super::AbsolutePinnedMode<C> {}
    impl<C: ClockSource> Sealed for super::RelativePinnedMode<C> {}
    impl<C: ClockSource> Sealed for super::AbsoluteSoftMode<C> {}
    impl<C: ClockSource> Sealed for super::RelativeSoftMode<C> {}
    impl<C: ClockSource> Sealed for super::AbsolutePinnedSoftMode<C> {}
    impl<C: ClockSource> Sealed for super::RelativePinnedSoftMode<C> {}
    impl<C: ClockSource> Sealed for super::AbsoluteHardMode<C> {}
    impl<C: ClockSource> Sealed for super::RelativeHardMode<C> {}
    impl<C: ClockSource> Sealed for super::AbsolutePinnedHardMode<C> {}
    impl<C: ClockSource> Sealed for super::RelativePinnedHardMode<C> {}
}

/// Operational mode of [`HrTimer`].
pub trait HrTimerMode: private::Sealed {
    /// The C representation of hrtimer mode.
    const C_MODE: bindings::hrtimer_mode;

    /// Type representing the clock source.
    type Clock: ClockSource;

    /// Type representing the expiration specification (absolute or relative time).
    type Expires: HrTimerExpires;
}

/// Timer that expires at a fixed point in time.
pub struct AbsoluteMode<C: ClockSource>(PhantomData<C>);

impl<C: ClockSource> HrTimerMode for AbsoluteMode<C> {
    const C_MODE: bindings::hrtimer_mode = bindings::hrtimer_mode_HRTIMER_MODE_ABS;

    type Clock = C;
    type Expires = Instant<C>;
}

/// Timer that expires after a delay from now.
pub struct RelativeMode<C: ClockSource>(PhantomData<C>);

impl<C: ClockSource> HrTimerMode for RelativeMode<C> {
    const C_MODE: bindings::hrtimer_mode = bindings::hrtimer_mode_HRTIMER_MODE_REL;

    type Clock = C;
    type Expires = Delta;
}

/// Timer with absolute expiration time, pinned to its current CPU.
pub struct AbsolutePinnedMode<C: ClockSource>(PhantomData<C>);
impl<C: ClockSource> HrTimerMode for AbsolutePinnedMode<C> {
    const C_MODE: bindings::hrtimer_mode = bindings::hrtimer_mode_HRTIMER_MODE_ABS_PINNED;

    type Clock = C;
    type Expires = Instant<C>;
}

/// Timer with relative expiration time, pinned to its current CPU.
pub struct RelativePinnedMode<C: ClockSource>(PhantomData<C>);
impl<C: ClockSource> HrTimerMode for RelativePinnedMode<C> {
    const C_MODE: bindings::hrtimer_mode = bindings::hrtimer_mode_HRTIMER_MODE_REL_PINNED;

    type Clock = C;
    type Expires = Delta;
}

/// Timer with absolute expiration, handled in soft irq context.
pub struct AbsoluteSoftMode<C: ClockSource>(PhantomData<C>);
impl<C: ClockSource> HrTimerMode for AbsoluteSoftMode<C> {
    const C_MODE: bindings::hrtimer_mode = bindings::hrtimer_mode_HRTIMER_MODE_ABS_SOFT;

    type Clock = C;
    type Expires = Instant<C>;
}

/// Timer with relative expiration, handled in soft irq context.
pub struct RelativeSoftMode<C: ClockSource>(PhantomData<C>);
impl<C: ClockSource> HrTimerMode for RelativeSoftMode<C> {
    const C_MODE: bindings::hrtimer_mode = bindings::hrtimer_mode_HRTIMER_MODE_REL_SOFT;

    type Clock = C;
    type Expires = Delta;
}

/// Timer with absolute expiration, pinned to CPU and handled in soft irq context.
pub struct AbsolutePinnedSoftMode<C: ClockSource>(PhantomData<C>);
impl<C: ClockSource> HrTimerMode for AbsolutePinnedSoftMode<C> {
    const C_MODE: bindings::hrtimer_mode = bindings::hrtimer_mode_HRTIMER_MODE_ABS_PINNED_SOFT;

    type Clock = C;
    type Expires = Instant<C>;
}

/// Timer with absolute expiration, pinned to CPU and handled in soft irq context.
pub struct RelativePinnedSoftMode<C: ClockSource>(PhantomData<C>);
impl<C: ClockSource> HrTimerMode for RelativePinnedSoftMode<C> {
    const C_MODE: bindings::hrtimer_mode = bindings::hrtimer_mode_HRTIMER_MODE_REL_PINNED_SOFT;

    type Clock = C;
    type Expires = Delta;
}

/// Timer with absolute expiration, handled in hard irq context.
pub struct AbsoluteHardMode<C: ClockSource>(PhantomData<C>);
impl<C: ClockSource> HrTimerMode for AbsoluteHardMode<C> {
    const C_MODE: bindings::hrtimer_mode = bindings::hrtimer_mode_HRTIMER_MODE_ABS_HARD;

    type Clock = C;
    type Expires = Instant<C>;
}

/// Timer with relative expiration, handled in hard irq context.
pub struct RelativeHardMode<C: ClockSource>(PhantomData<C>);
impl<C: ClockSource> HrTimerMode for RelativeHardMode<C> {
    const C_MODE: bindings::hrtimer_mode = bindings::hrtimer_mode_HRTIMER_MODE_REL_HARD;

    type Clock = C;
    type Expires = Delta;
}

/// Timer with absolute expiration, pinned to CPU and handled in hard irq context.
pub struct AbsolutePinnedHardMode<C: ClockSource>(PhantomData<C>);
impl<C: ClockSource> HrTimerMode for AbsolutePinnedHardMode<C> {
    const C_MODE: bindings::hrtimer_mode = bindings::hrtimer_mode_HRTIMER_MODE_ABS_PINNED_HARD;

    type Clock = C;
    type Expires = Instant<C>;
}

/// Timer with relative expiration, pinned to CPU and handled in hard irq context.
pub struct RelativePinnedHardMode<C: ClockSource>(PhantomData<C>);
impl<C: ClockSource> HrTimerMode for RelativePinnedHardMode<C> {
    const C_MODE: bindings::hrtimer_mode = bindings::hrtimer_mode_HRTIMER_MODE_REL_PINNED_HARD;

    type Clock = C;
    type Expires = Delta;
}

/// Privileged smart-pointer for a [`HrTimer`] callback context.
///
/// Many [`HrTimer`] methods can only be called in two situations:
///
/// * When the caller has exclusive access to the `HrTimer` and the `HrTimer` is guaranteed not to
///   be running.
/// * From within the context of an `HrTimer`'s callback method.
///
/// This type provides access to said methods from within a timer callback context.
///
/// # Invariants
///
/// * The existence of this type means the caller is currently within the callback for an
///   [`HrTimer`].
/// * `self.0` always points to a live instance of [`HrTimer<T>`].
pub struct HrTimerCallbackContext<'a, T: HasHrTimer<T>>(NonNull<HrTimer<T>>, PhantomData<&'a ()>);

impl<'a, T: HasHrTimer<T>> HrTimerCallbackContext<'a, T> {
    /// Create a new [`HrTimerCallbackContext`].
    ///
    /// # Safety
    ///
    /// This function relies on the caller being within the context of a timer callback, so it must
    /// not be used anywhere except for within implementations of [`RawHrTimerCallback::run`]. The
    /// caller promises that `timer` points to a valid initialized instance of
    /// [`bindings::hrtimer`].
    ///
    /// The returned `Self` must not outlive the function context of [`RawHrTimerCallback::run`]
    /// where this function is called.
    pub(crate) unsafe fn from_raw(timer: *mut HrTimer<T>) -> Self {
        // SAFETY: The caller guarantees `timer` is a valid pointer to an initialized
        // `bindings::hrtimer`
        // INVARIANT: Our safety contract ensures that we're within the context of a timer callback
        // and that `timer` points to a live instance of `HrTimer<T>`.
        Self(unsafe { NonNull::new_unchecked(timer) }, PhantomData)
    }

    /// Conditionally forward the timer.
    ///
    /// This function is identical to [`HrTimer::forward()`] except that it may only be used from
    /// within the context of a [`HrTimer`] callback.
    pub fn forward(&mut self, now: HrTimerInstant<T>, interval: Delta) -> u64 {
        // SAFETY:
        // - We are guaranteed to be within the context of a timer callback by our type invariants
        // - By our type invariants, `self.0` always points to a valid `HrTimer<T>`
        unsafe { HrTimer::<T>::raw_forward(self.0.as_ptr(), now, interval) }
    }

    /// Conditionally forward the timer.
    ///
    /// This is a variant of [`HrTimerCallbackContext::forward()`] that uses an interval after the
    /// current time of the base clock for the [`HrTimer`].
    pub fn forward_now(&mut self, duration: Delta) -> u64 {
        self.forward(HrTimerInstant::<T>::now(), duration)
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
        {
            mode : $mode:ty,
            field : self.$field:ident $(,)?
        }
        $($rest:tt)*
    ) => {
        // SAFETY: This implementation of `raw_get_timer` only compiles if the
        // field has the right type.
        unsafe impl$(<$($generics)*>)? $crate::time::hrtimer::HasHrTimer<$timer_type> for $self {
            type TimerMode = $mode;

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
