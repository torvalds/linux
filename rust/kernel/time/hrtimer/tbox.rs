// SPDX-License-Identifier: GPL-2.0

use super::HasHrTimer;
use super::HrTimer;
use super::HrTimerCallback;
use super::HrTimerCallbackContext;
use super::HrTimerHandle;
use super::HrTimerMode;
use super::HrTimerPointer;
use super::RawHrTimerCallback;
use crate::prelude::*;
use core::ptr::NonNull;

/// A handle for a [`Box<HasHrTimer<T>>`] returned by a call to
/// [`HrTimerPointer::start`].
///
/// # Invariants
///
/// - `self.inner` comes from a `Box::into_raw` call.
pub struct BoxHrTimerHandle<T, A>
where
    T: HasHrTimer<T>,
    A: crate::alloc::Allocator,
{
    pub(crate) inner: NonNull<T>,
    _p: core::marker::PhantomData<A>,
}

// SAFETY: We implement drop below, and we cancel the timer in the drop
// implementation.
unsafe impl<T, A> HrTimerHandle for BoxHrTimerHandle<T, A>
where
    T: HasHrTimer<T>,
    A: crate::alloc::Allocator,
{
    fn cancel(&mut self) -> bool {
        // SAFETY: As we obtained `self.inner` from a valid reference when we
        // created `self`, it must point to a valid `T`.
        let timer_ptr = unsafe { <T as HasHrTimer<T>>::raw_get_timer(self.inner.as_ptr()) };

        // SAFETY: As `timer_ptr` points into `T` and `T` is valid, `timer_ptr`
        // must point to a valid `HrTimer` instance.
        unsafe { HrTimer::<T>::raw_cancel(timer_ptr) }
    }
}

impl<T, A> Drop for BoxHrTimerHandle<T, A>
where
    T: HasHrTimer<T>,
    A: crate::alloc::Allocator,
{
    fn drop(&mut self) {
        self.cancel();
        // SAFETY: By type invariant, `self.inner` came from a `Box::into_raw`
        // call.
        drop(unsafe { Box::<T, A>::from_raw(self.inner.as_ptr()) })
    }
}

impl<T, A> HrTimerPointer for Pin<Box<T, A>>
where
    T: 'static,
    T: Send + Sync,
    T: HasHrTimer<T>,
    T: for<'a> HrTimerCallback<Pointer<'a> = Pin<Box<T, A>>>,
    A: crate::alloc::Allocator,
{
    type TimerMode = <T as HasHrTimer<T>>::TimerMode;
    type TimerHandle = BoxHrTimerHandle<T, A>;

    fn start(
        self,
        expires: <<T as HasHrTimer<T>>::TimerMode as HrTimerMode>::Expires,
    ) -> Self::TimerHandle {
        // SAFETY:
        //  - We will not move out of this box during timer callback (we pass an
        //    immutable reference to the callback).
        //  - `Box::into_raw` is guaranteed to return a valid pointer.
        let inner =
            unsafe { NonNull::new_unchecked(Box::into_raw(Pin::into_inner_unchecked(self))) };

        // SAFETY:
        //  - We keep `self` alive by wrapping it in a handle below.
        //  - Since we generate the pointer passed to `start` from a valid
        //    reference, it is a valid pointer.
        unsafe { T::start(inner.as_ptr(), expires) };

        // INVARIANT: `inner` came from `Box::into_raw` above.
        BoxHrTimerHandle {
            inner,
            _p: core::marker::PhantomData,
        }
    }
}

impl<T, A> RawHrTimerCallback for Pin<Box<T, A>>
where
    T: 'static,
    T: HasHrTimer<T>,
    T: for<'a> HrTimerCallback<Pointer<'a> = Pin<Box<T, A>>>,
    A: crate::alloc::Allocator,
{
    type CallbackTarget<'a> = Pin<&'a mut T>;

    unsafe extern "C" fn run(ptr: *mut bindings::hrtimer) -> bindings::hrtimer_restart {
        // `HrTimer` is `repr(C)`
        let timer_ptr = ptr.cast::<super::HrTimer<T>>();

        // SAFETY: By C API contract `ptr` is the pointer we passed when
        // queuing the timer, so it is a `HrTimer<T>` embedded in a `T`.
        let data_ptr = unsafe { T::timer_container_of(timer_ptr) };

        // SAFETY:
        //  - As per the safety requirements of the trait `HrTimerHandle`, the
        //   `BoxHrTimerHandle` associated with this timer is guaranteed to
        //   be alive until this method returns. That handle owns the `T`
        //   behind `data_ptr` thus guaranteeing the validity of
        //   the reference created below.
        // - As `data_ptr` comes from a `Pin<Box<T>>`, only pinned references to
        //   `data_ptr` exist.
        let data_mut_ref = unsafe { Pin::new_unchecked(&mut *data_ptr) };

        // SAFETY:
        // - By C API contract `timer_ptr` is the pointer that we passed when queuing the timer, so
        //   it is a valid pointer to a `HrTimer<T>` embedded in a `T`.
        // - We are within `RawHrTimerCallback::run`
        let context = unsafe { HrTimerCallbackContext::from_raw(timer_ptr) };

        T::run(data_mut_ref, context).into_c()
    }
}
