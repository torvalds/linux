// SPDX-License-Identifier: GPL-2.0

//! A container that can be initialized at most once.

use super::atomic::{
    ordering::{Acquire, Relaxed, Release},
    Atomic,
};
use core::{cell::UnsafeCell, mem::MaybeUninit};

/// A container that can be populated at most once. Thread safe.
///
/// Once the a [`SetOnce`] is populated, it remains populated by the same object for the
/// lifetime `Self`.
///
/// # Invariants
///
/// - `init` may only increase in value.
/// - `init` may only assume values in the range `0..=2`.
/// - `init == 0` if and only if `value` is uninitialized.
/// - `init == 1` if and only if there is exactly one thread with exclusive
///   access to `self.value`.
/// - `init == 2` if and only if `value` is initialized and valid for shared
///   access.
///
/// # Example
///
/// ```
/// # use kernel::sync::SetOnce;
/// let value = SetOnce::new();
/// assert_eq!(None, value.as_ref());
///
/// let status = value.populate(42u8);
/// assert_eq!(true, status);
/// assert_eq!(Some(&42u8), value.as_ref());
/// assert_eq!(Some(42u8), value.copy());
///
/// let status = value.populate(101u8);
/// assert_eq!(false, status);
/// assert_eq!(Some(&42u8), value.as_ref());
/// assert_eq!(Some(42u8), value.copy());
/// ```
pub struct SetOnce<T> {
    init: Atomic<u32>,
    value: UnsafeCell<MaybeUninit<T>>,
}

impl<T> Default for SetOnce<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> SetOnce<T> {
    /// Create a new [`SetOnce`].
    ///
    /// The returned instance will be empty.
    pub const fn new() -> Self {
        // INVARIANT: The container is empty and we initialize `init` to `0`.
        Self {
            value: UnsafeCell::new(MaybeUninit::uninit()),
            init: Atomic::new(0),
        }
    }

    /// Get a reference to the contained object.
    ///
    /// Returns [`None`] if this [`SetOnce`] is empty.
    pub fn as_ref(&self) -> Option<&T> {
        if self.init.load(Acquire) == 2 {
            // SAFETY: By the type invariants of `Self`, `self.init == 2` means that `self.value`
            // is initialized and valid for shared access.
            Some(unsafe { &*self.value.get().cast() })
        } else {
            None
        }
    }

    /// Populate the [`SetOnce`].
    ///
    /// Returns `true` if the [`SetOnce`] was successfully populated.
    pub fn populate(&self, value: T) -> bool {
        // INVARIANT: If the swap succeeds:
        //  - We increase `init`.
        //  - We write the valid value `1` to `init`.
        //  - Only one thread can succeed in this write, so we have exclusive access after the
        //    write.
        if let Ok(0) = self.init.cmpxchg(0, 1, Relaxed) {
            // SAFETY: By the type invariants of `Self`, the fact that we succeeded in writing `1`
            // to `self.init` means we obtained exclusive access to `self.value`.
            unsafe { core::ptr::write(self.value.get().cast(), value) };
            // INVARIANT:
            //  - We increase `init`.
            //  - We write the valid value `2` to `init`.
            //  - We release our exclusive access to `self.value` and it is now valid for shared
            //    access.
            self.init.store(2, Release);
            true
        } else {
            false
        }
    }

    /// Get a copy of the contained object.
    ///
    /// Returns [`None`] if the [`SetOnce`] is empty.
    pub fn copy(&self) -> Option<T>
    where
        T: Copy,
    {
        self.as_ref().copied()
    }
}

impl<T> Drop for SetOnce<T> {
    fn drop(&mut self) {
        if *self.init.get_mut() == 2 {
            let value = self.value.get_mut();
            // SAFETY: By the type invariants of `Self`, `self.init == 2` means that `self.value`
            // contains a valid value. We have exclusive access, as we hold a `mut` reference to
            // `self`.
            unsafe { value.assume_init_drop() };
        }
    }
}

// SAFETY: `SetOnce` can be transferred across thread boundaries iff the data it contains can.
unsafe impl<T: Send> Send for SetOnce<T> {}

// SAFETY: `SetOnce` synchronises access to the inner value via atomic operations,
// so shared references are safe when `T: Sync`. Since the inner `T` may be dropped
// on any thread, we also require `T: Send`.
unsafe impl<T: Send + Sync> Sync for SetOnce<T> {}
