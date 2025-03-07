// SPDX-License-Identifier: GPL-2.0

//! Generic kernel lock and guard.
//!
//! It contains a generic Rust lock and guard that allow for different backends (e.g., mutexes,
//! spinlocks, raw spinlocks) to be provided with minimal effort.

use super::LockClassKey;
use crate::{
    init::PinInit,
    pin_init,
    str::CStr,
    types::{NotThreadSafe, Opaque, ScopeGuard},
};
use core::{cell::UnsafeCell, marker::PhantomPinned, pin::Pin};
use macros::pin_data;

pub mod mutex;
pub mod spinlock;

pub(super) mod global;
pub use global::{GlobalGuard, GlobalLock, GlobalLockBackend, GlobalLockedBy};

/// The "backend" of a lock.
///
/// It is the actual implementation of the lock, without the need to repeat patterns used in all
/// locks.
///
/// # Safety
///
/// - Implementers must ensure that only one thread/CPU may access the protected data once the lock
///   is owned, that is, between calls to [`lock`] and [`unlock`].
/// - Implementers must also ensure that [`relock`] uses the same locking method as the original
///   lock operation.
///
/// [`lock`]: Backend::lock
/// [`unlock`]: Backend::unlock
/// [`relock`]: Backend::relock
pub unsafe trait Backend {
    /// The state required by the lock.
    type State;

    /// The state required to be kept between [`lock`] and [`unlock`].
    ///
    /// [`lock`]: Backend::lock
    /// [`unlock`]: Backend::unlock
    type GuardState;

    /// Initialises the lock.
    ///
    /// # Safety
    ///
    /// `ptr` must be valid for write for the duration of the call, while `name` and `key` must
    /// remain valid for read indefinitely.
    unsafe fn init(
        ptr: *mut Self::State,
        name: *const crate::ffi::c_char,
        key: *mut bindings::lock_class_key,
    );

    /// Acquires the lock, making the caller its owner.
    ///
    /// # Safety
    ///
    /// Callers must ensure that [`Backend::init`] has been previously called.
    #[must_use]
    unsafe fn lock(ptr: *mut Self::State) -> Self::GuardState;

    /// Tries to acquire the lock.
    ///
    /// # Safety
    ///
    /// Callers must ensure that [`Backend::init`] has been previously called.
    unsafe fn try_lock(ptr: *mut Self::State) -> Option<Self::GuardState>;

    /// Releases the lock, giving up its ownership.
    ///
    /// # Safety
    ///
    /// It must only be called by the current owner of the lock.
    unsafe fn unlock(ptr: *mut Self::State, guard_state: &Self::GuardState);

    /// Reacquires the lock, making the caller its owner.
    ///
    /// # Safety
    ///
    /// Callers must ensure that `guard_state` comes from a previous call to [`Backend::lock`] (or
    /// variant) that has been unlocked with [`Backend::unlock`] and will be relocked now.
    unsafe fn relock(ptr: *mut Self::State, guard_state: &mut Self::GuardState) {
        // SAFETY: The safety requirements ensure that the lock is initialised.
        *guard_state = unsafe { Self::lock(ptr) };
    }

    /// Asserts that the lock is held using lockdep.
    ///
    /// # Safety
    ///
    /// Callers must ensure that [`Backend::init`] has been previously called.
    unsafe fn assert_is_held(ptr: *mut Self::State);
}

/// A mutual exclusion primitive.
///
/// Exposes one of the kernel locking primitives. Which one is exposed depends on the lock
/// [`Backend`] specified as the generic parameter `B`.
#[repr(C)]
#[pin_data]
pub struct Lock<T: ?Sized, B: Backend> {
    /// The kernel lock object.
    #[pin]
    state: Opaque<B::State>,

    /// Some locks are known to be self-referential (e.g., mutexes), while others are architecture
    /// or config defined (e.g., spinlocks). So we conservatively require them to be pinned in case
    /// some architecture uses self-references now or in the future.
    #[pin]
    _pin: PhantomPinned,

    /// The data protected by the lock.
    pub(crate) data: UnsafeCell<T>,
}

// SAFETY: `Lock` can be transferred across thread boundaries iff the data it protects can.
unsafe impl<T: ?Sized + Send, B: Backend> Send for Lock<T, B> {}

// SAFETY: `Lock` serialises the interior mutability it provides, so it is `Sync` as long as the
// data it protects is `Send`.
unsafe impl<T: ?Sized + Send, B: Backend> Sync for Lock<T, B> {}

impl<T, B: Backend> Lock<T, B> {
    /// Constructs a new lock initialiser.
    pub fn new(t: T, name: &'static CStr, key: Pin<&'static LockClassKey>) -> impl PinInit<Self> {
        pin_init!(Self {
            data: UnsafeCell::new(t),
            _pin: PhantomPinned,
            // SAFETY: `slot` is valid while the closure is called and both `name` and `key` have
            // static lifetimes so they live indefinitely.
            state <- Opaque::ffi_init(|slot| unsafe {
                B::init(slot, name.as_char_ptr(), key.as_ptr())
            }),
        })
    }
}

impl<B: Backend> Lock<(), B> {
    /// Constructs a [`Lock`] from a raw pointer.
    ///
    /// This can be useful for interacting with a lock which was initialised outside of Rust.
    ///
    /// # Safety
    ///
    /// The caller promises that `ptr` points to a valid initialised instance of [`State`] during
    /// the whole lifetime of `'a`.
    ///
    /// [`State`]: Backend::State
    pub unsafe fn from_raw<'a>(ptr: *mut B::State) -> &'a Self {
        // SAFETY:
        // - By the safety contract `ptr` must point to a valid initialised instance of `B::State`
        // - Since the lock data type is `()` which is a ZST, `state` is the only non-ZST member of
        //   the struct
        // - Combined with `#[repr(C)]`, this guarantees `Self` has an equivalent data layout to
        //   `B::State`.
        unsafe { &*ptr.cast() }
    }
}

impl<T: ?Sized, B: Backend> Lock<T, B> {
    /// Acquires the lock and gives the caller access to the data protected by it.
    pub fn lock(&self) -> Guard<'_, T, B> {
        // SAFETY: The constructor of the type calls `init`, so the existence of the object proves
        // that `init` was called.
        let state = unsafe { B::lock(self.state.get()) };
        // SAFETY: The lock was just acquired.
        unsafe { Guard::new(self, state) }
    }

    /// Tries to acquire the lock.
    ///
    /// Returns a guard that can be used to access the data protected by the lock if successful.
    pub fn try_lock(&self) -> Option<Guard<'_, T, B>> {
        // SAFETY: The constructor of the type calls `init`, so the existence of the object proves
        // that `init` was called.
        unsafe { B::try_lock(self.state.get()).map(|state| Guard::new(self, state)) }
    }
}

/// A lock guard.
///
/// Allows mutual exclusion primitives that implement the [`Backend`] trait to automatically unlock
/// when a guard goes out of scope. It also provides a safe and convenient way to access the data
/// protected by the lock.
#[must_use = "the lock unlocks immediately when the guard is unused"]
pub struct Guard<'a, T: ?Sized, B: Backend> {
    pub(crate) lock: &'a Lock<T, B>,
    pub(crate) state: B::GuardState,
    _not_send: NotThreadSafe,
}

// SAFETY: `Guard` is sync when the data protected by the lock is also sync.
unsafe impl<T: Sync + ?Sized, B: Backend> Sync for Guard<'_, T, B> {}

impl<'a, T: ?Sized, B: Backend> Guard<'a, T, B> {
    /// Returns the lock that this guard originates from.
    ///
    /// # Examples
    ///
    /// The following example shows how to use [`Guard::lock_ref()`] to assert the corresponding
    /// lock is held.
    ///
    /// ```
    /// # use kernel::{new_spinlock, stack_pin_init, sync::lock::{Backend, Guard, Lock}};
    ///
    /// fn assert_held<T, B: Backend>(guard: &Guard<'_, T, B>, lock: &Lock<T, B>) {
    ///     // Address-equal means the same lock.
    ///     assert!(core::ptr::eq(guard.lock_ref(), lock));
    /// }
    ///
    /// // Creates a new lock on the stack.
    /// stack_pin_init!{
    ///     let l = new_spinlock!(42)
    /// }
    ///
    /// let g = l.lock();
    ///
    /// // `g` originates from `l`.
    /// assert_held(&g, &l);
    /// ```
    pub fn lock_ref(&self) -> &'a Lock<T, B> {
        self.lock
    }

    pub(crate) fn do_unlocked<U>(&mut self, cb: impl FnOnce() -> U) -> U {
        // SAFETY: The caller owns the lock, so it is safe to unlock it.
        unsafe { B::unlock(self.lock.state.get(), &self.state) };

        let _relock = ScopeGuard::new(||
                // SAFETY: The lock was just unlocked above and is being relocked now.
                unsafe { B::relock(self.lock.state.get(), &mut self.state) });

        cb()
    }
}

impl<T: ?Sized, B: Backend> core::ops::Deref for Guard<'_, T, B> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: The caller owns the lock, so it is safe to deref the protected data.
        unsafe { &*self.lock.data.get() }
    }
}

impl<T: ?Sized, B: Backend> core::ops::DerefMut for Guard<'_, T, B> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        // SAFETY: The caller owns the lock, so it is safe to deref the protected data.
        unsafe { &mut *self.lock.data.get() }
    }
}

impl<T: ?Sized, B: Backend> Drop for Guard<'_, T, B> {
    fn drop(&mut self) {
        // SAFETY: The caller owns the lock, so it is safe to unlock it.
        unsafe { B::unlock(self.lock.state.get(), &self.state) };
    }
}

impl<'a, T: ?Sized, B: Backend> Guard<'a, T, B> {
    /// Constructs a new immutable lock guard.
    ///
    /// # Safety
    ///
    /// The caller must ensure that it owns the lock.
    pub unsafe fn new(lock: &'a Lock<T, B>, state: B::GuardState) -> Self {
        // SAFETY: The caller can only hold the lock if `Backend::init` has already been called.
        unsafe { B::assert_is_held(lock.state.get()) };

        Self {
            lock,
            state,
            _not_send: NotThreadSafe,
        }
    }
}
