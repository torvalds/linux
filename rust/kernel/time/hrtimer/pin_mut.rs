// SPDX-License-Identifier: GPL-2.0

use super::{
    HasHrTimer, HrTimer, HrTimerCallback, HrTimerCallbackContext, HrTimerHandle, HrTimerMode,
    RawHrTimerCallback, UnsafeHrTimerPointer,
};
use core::{marker::PhantomData, pin::Pin, ptr::NonNull};

/// A handle for a `Pin<&mut HasHrTimer>`. When the handle exists, the timer might
/// be running.
pub struct PinMutHrTimerHandle<'a, T>
where
    T: HasHrTimer<T>,
{
    pub(crate) inner: NonNull<T>,
    _p: PhantomData<&'a mut T>,
}

// SAFETY: We cancel the timer when the handle is dropped. The implementation of
// the `cancel` method will block if the timer handler is running.
unsafe impl<'a, T> HrTimerHandle for PinMutHrTimerHandle<'a, T>
where
    T: HasHrTimer<T>,
{
    fn cancel(&mut self) -> bool {
        let self_ptr = self.inner.as_ptr();

        // SAFETY: As we got `self_ptr` from a reference above, it must point to
        // a valid `T`.
        let timer_ptr = unsafe { <T as HasHrTimer<T>>::raw_get_timer(self_ptr) };

        // SAFETY: As `timer_ptr` is derived from a reference, it must point to
        // a valid and initialized `HrTimer`.
        unsafe { HrTimer::<T>::raw_cancel(timer_ptr) }
    }
}

impl<'a, T> Drop for PinMutHrTimerHandle<'a, T>
where
    T: HasHrTimer<T>,
{
    fn drop(&mut self) {
        self.cancel();
    }
}

// SAFETY: We capture the lifetime of `Self` when we create a
// `PinMutHrTimerHandle`, so `Self` will outlive the handle.
unsafe impl<'a, T> UnsafeHrTimerPointer for Pin<&'a mut T>
where
    T: Send + Sync,
    T: HasHrTimer<T>,
    T: HrTimerCallback<Pointer<'a> = Self>,
{
    type TimerMode = <T as HasHrTimer<T>>::TimerMode;
    type TimerHandle = PinMutHrTimerHandle<'a, T>;

    unsafe fn start(
        mut self,
        expires: <<T as HasHrTimer<T>>::TimerMode as HrTimerMode>::Expires,
    ) -> Self::TimerHandle {
        // SAFETY:
        // - We promise not to move out of `self`. We only pass `self`
        //   back to the caller as a `Pin<&mut self>`.
        // - The return value of `get_unchecked_mut` is guaranteed not to be null.
        let self_ptr = unsafe { NonNull::new_unchecked(self.as_mut().get_unchecked_mut()) };

        // SAFETY:
        //  - As we derive `self_ptr` from a reference above, it must point to a
        //    valid `T`.
        //  - We keep `self` alive by wrapping it in a handle below.
        unsafe { T::start(self_ptr.as_ptr(), expires) };

        PinMutHrTimerHandle {
            inner: self_ptr,
            _p: PhantomData,
        }
    }
}

impl<'a, T> RawHrTimerCallback for Pin<&'a mut T>
where
    T: HasHrTimer<T>,
    T: HrTimerCallback<Pointer<'a> = Self>,
{
    type CallbackTarget<'b> = Self;

    unsafe extern "C" fn run(ptr: *mut bindings::hrtimer) -> bindings::hrtimer_restart {
        // `HrTimer` is `repr(C)`
        let timer_ptr = ptr.cast::<HrTimer<T>>();

        // SAFETY: By the safety requirement of this function, `timer_ptr`
        // points to a `HrTimer<T>` contained in an `T`.
        let receiver_ptr = unsafe { T::timer_container_of(timer_ptr) };

        // SAFETY:
        //  - By the safety requirement of this function, `timer_ptr`
        //    points to a `HrTimer<T>` contained in an `T`.
        //  - As per the safety requirements of the trait `HrTimerHandle`, the
        //    `PinMutHrTimerHandle` associated with this timer is guaranteed to
        //    be alive until this method returns. That handle borrows the `T`
        //    behind `receiver_ptr` mutably thus guaranteeing the validity of
        //    the reference created below.
        let receiver_ref = unsafe { &mut *receiver_ptr };

        // SAFETY: `receiver_ref` only exists as pinned, so it is safe to pin it
        // here.
        let receiver_pin = unsafe { Pin::new_unchecked(receiver_ref) };

        // SAFETY:
        // - By C API contract `timer_ptr` is the pointer that we passed when queuing the timer, so
        //   it is a valid pointer to a `HrTimer<T>` embedded in a `T`.
        // - We are within `RawHrTimerCallback::run`
        let context = unsafe { HrTimerCallbackContext::from_raw(timer_ptr) };

        T::run(receiver_pin, context).into_c()
    }
}
