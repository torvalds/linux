// SPDX-License-Identifier: GPL-2.0

//! A kernel mutex.
//!
//! This module allows Rust code to use the kernel's `struct mutex`.

use crate::bindings;

/// Creates a [`Mutex`] initialiser with the given name and a newly-created lock class.
///
/// It uses the name if one is given, otherwise it generates one based on the file name and line
/// number.
#[macro_export]
macro_rules! new_mutex {
    ($inner:expr $(, $name:literal)? $(,)?) => {
        $crate::sync::Mutex::new(
            $inner, $crate::optional_name!($($name)?), $crate::static_lock_class!())
    };
}
pub use new_mutex;

/// A mutual exclusion primitive.
///
/// Exposes the kernel's [`struct mutex`]. When multiple threads attempt to lock the same mutex,
/// only one at a time is allowed to progress, the others will block (sleep) until the mutex is
/// unlocked, at which point another thread will be allowed to wake up and make progress.
///
/// Since it may block, [`Mutex`] needs to be used with care in atomic contexts.
///
/// Instances of [`Mutex`] need a lock class and to be pinned. The recommended way to create such
/// instances is with the [`pin_init`](crate::pin_init) and [`new_mutex`] macros.
///
/// # Examples
///
/// The following example shows how to declare, allocate and initialise a struct (`Example`) that
/// contains an inner struct (`Inner`) that is protected by a mutex.
///
/// ```
/// use kernel::sync::{new_mutex, Mutex};
///
/// struct Inner {
///     a: u32,
///     b: u32,
/// }
///
/// #[pin_data]
/// struct Example {
///     c: u32,
///     #[pin]
///     d: Mutex<Inner>,
/// }
///
/// impl Example {
///     fn new() -> impl PinInit<Self> {
///         pin_init!(Self {
///             c: 10,
///             d <- new_mutex!(Inner { a: 20, b: 30 }),
///         })
///     }
/// }
///
/// // Allocate a boxed `Example`.
/// let e = Box::pin_init(Example::new())?;
/// assert_eq!(e.c, 10);
/// assert_eq!(e.d.lock().a, 20);
/// assert_eq!(e.d.lock().b, 30);
/// # Ok::<(), Error>(())
/// ```
///
/// The following example shows how to use interior mutability to modify the contents of a struct
/// protected by a mutex despite only having a shared reference:
///
/// ```
/// use kernel::sync::Mutex;
///
/// struct Example {
///     a: u32,
///     b: u32,
/// }
///
/// fn example(m: &Mutex<Example>) {
///     let mut guard = m.lock();
///     guard.a += 10;
///     guard.b += 20;
/// }
/// ```
///
/// [`struct mutex`]: srctree/include/linux/mutex.h
pub type Mutex<T> = super::Lock<T, MutexBackend>;

/// A kernel `struct mutex` lock backend.
pub struct MutexBackend;

// SAFETY: The underlying kernel `struct mutex` object ensures mutual exclusion.
unsafe impl super::Backend for MutexBackend {
    type State = bindings::mutex;
    type GuardState = ();

    unsafe fn init(
        ptr: *mut Self::State,
        name: *const core::ffi::c_char,
        key: *mut bindings::lock_class_key,
    ) {
        // SAFETY: The safety requirements ensure that `ptr` is valid for writes, and `name` and
        // `key` are valid for read indefinitely.
        unsafe { bindings::__mutex_init(ptr, name, key) }
    }

    unsafe fn lock(ptr: *mut Self::State) -> Self::GuardState {
        // SAFETY: The safety requirements of this function ensure that `ptr` points to valid
        // memory, and that it has been initialised before.
        unsafe { bindings::mutex_lock(ptr) };
    }

    unsafe fn unlock(ptr: *mut Self::State, _guard_state: &Self::GuardState) {
        // SAFETY: The safety requirements of this function ensure that `ptr` is valid and that the
        // caller is the owner of the mutex.
        unsafe { bindings::mutex_unlock(ptr) };
    }
}
