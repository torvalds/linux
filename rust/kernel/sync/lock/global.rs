// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Support for defining statics containing locks.

use crate::{
    str::CStr,
    sync::lock::{Backend, Guard, Lock},
    sync::{LockClassKey, LockedBy},
    types::Opaque,
};
use core::{
    cell::UnsafeCell,
    marker::{PhantomData, PhantomPinned},
    pin::Pin,
};

/// Trait implemented for marker types for global locks.
///
/// See [`global_lock!`] for examples.
pub trait GlobalLockBackend {
    /// The name for this global lock.
    const NAME: &'static CStr;
    /// Item type stored in this global lock.
    type Item: 'static;
    /// The backend used for this global lock.
    type Backend: Backend + 'static;
    /// The class for this global lock.
    fn get_lock_class() -> Pin<&'static LockClassKey>;
}

/// Type used for global locks.
///
/// See [`global_lock!`] for examples.
pub struct GlobalLock<B: GlobalLockBackend> {
    inner: Lock<B::Item, B::Backend>,
}

impl<B: GlobalLockBackend> GlobalLock<B> {
    /// Creates a global lock.
    ///
    /// # Safety
    ///
    /// * Before any other method on this lock is called, [`Self::init`] must be called.
    /// * The type `B` must not be used with any other lock.
    pub const unsafe fn new(data: B::Item) -> Self {
        Self {
            inner: Lock {
                state: Opaque::uninit(),
                data: UnsafeCell::new(data),
                _pin: PhantomPinned,
            },
        }
    }

    /// Initializes a global lock.
    ///
    /// # Safety
    ///
    /// Must not be called more than once on a given lock.
    pub unsafe fn init(&'static self) {
        // SAFETY: The pointer to `state` is valid for the duration of this call, and both `name`
        // and `key` are valid indefinitely. The `state` is pinned since we have a `'static`
        // reference to `self`.
        //
        // We have exclusive access to the `state` since the caller of `new` promised to call
        // `init` before using any other methods. As `init` can only be called once, all other
        // uses of this lock must happen after this call.
        unsafe {
            B::Backend::init(
                self.inner.state.get(),
                B::NAME.as_char_ptr(),
                B::get_lock_class().as_ptr(),
            )
        }
    }

    /// Lock this global lock.
    pub fn lock(&'static self) -> GlobalGuard<B> {
        GlobalGuard {
            inner: self.inner.lock(),
        }
    }

    /// Try to lock this global lock.
    pub fn try_lock(&'static self) -> Option<GlobalGuard<B>> {
        Some(GlobalGuard {
            inner: self.inner.try_lock()?,
        })
    }
}

/// A guard for a [`GlobalLock`].
///
/// See [`global_lock!`] for examples.
pub struct GlobalGuard<B: GlobalLockBackend> {
    inner: Guard<'static, B::Item, B::Backend>,
}

impl<B: GlobalLockBackend> core::ops::Deref for GlobalGuard<B> {
    type Target = B::Item;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl<B: GlobalLockBackend> core::ops::DerefMut for GlobalGuard<B> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.inner
    }
}

/// A version of [`LockedBy`] for a [`GlobalLock`].
///
/// See [`global_lock!`] for examples.
pub struct GlobalLockedBy<T: ?Sized, B: GlobalLockBackend> {
    _backend: PhantomData<B>,
    value: UnsafeCell<T>,
}

// SAFETY: The same thread-safety rules as `LockedBy` apply to `GlobalLockedBy`.
unsafe impl<T, B> Send for GlobalLockedBy<T, B>
where
    T: ?Sized,
    B: GlobalLockBackend,
    LockedBy<T, B::Item>: Send,
{
}

// SAFETY: The same thread-safety rules as `LockedBy` apply to `GlobalLockedBy`.
unsafe impl<T, B> Sync for GlobalLockedBy<T, B>
where
    T: ?Sized,
    B: GlobalLockBackend,
    LockedBy<T, B::Item>: Sync,
{
}

impl<T, B: GlobalLockBackend> GlobalLockedBy<T, B> {
    /// Create a new [`GlobalLockedBy`].
    ///
    /// The provided value will be protected by the global lock indicated by `B`.
    pub fn new(val: T) -> Self {
        Self {
            value: UnsafeCell::new(val),
            _backend: PhantomData,
        }
    }
}

impl<T: ?Sized, B: GlobalLockBackend> GlobalLockedBy<T, B> {
    /// Access the value immutably.
    ///
    /// The caller must prove shared access to the lock.
    pub fn as_ref<'a>(&'a self, _guard: &'a GlobalGuard<B>) -> &'a T {
        // SAFETY: The lock is globally unique, so there can only be one guard.
        unsafe { &*self.value.get() }
    }

    /// Access the value mutably.
    ///
    /// The caller must prove shared exclusive to the lock.
    pub fn as_mut<'a>(&'a self, _guard: &'a mut GlobalGuard<B>) -> &'a mut T {
        // SAFETY: The lock is globally unique, so there can only be one guard.
        unsafe { &mut *self.value.get() }
    }

    /// Access the value mutably directly.
    ///
    /// The caller has exclusive access to this `GlobalLockedBy`, so they do not need to hold the
    /// lock.
    pub fn get_mut(&mut self) -> &mut T {
        self.value.get_mut()
    }
}

/// Defines a global lock.
///
/// The global mutex must be initialized before first use. Usually this is done by calling
/// [`GlobalLock::init`] in the module initializer.
///
/// # Examples
///
/// A global counter:
///
/// ```
/// # mod ex {
/// # use kernel::prelude::*;
/// kernel::sync::global_lock! {
///     // SAFETY: Initialized in module initializer before first use.
///     unsafe(uninit) static MY_COUNTER: Mutex<u32> = 0;
/// }
///
/// fn increment_counter() -> u32 {
///     let mut guard = MY_COUNTER.lock();
///     *guard += 1;
///     *guard
/// }
///
/// impl kernel::Module for MyModule {
///     fn init(_module: &'static ThisModule) -> Result<Self> {
///         // SAFETY: Called exactly once.
///         unsafe { MY_COUNTER.init() };
///
///         Ok(MyModule {})
///     }
/// }
/// # struct MyModule {}
/// # }
/// ```
///
/// A global mutex used to protect all instances of a given struct:
///
/// ```
/// # mod ex {
/// # use kernel::prelude::*;
/// use kernel::sync::{GlobalGuard, GlobalLockedBy};
///
/// kernel::sync::global_lock! {
///     // SAFETY: Initialized in module initializer before first use.
///     unsafe(uninit) static MY_MUTEX: Mutex<()> = ();
/// }
///
/// /// All instances of this struct are protected by `MY_MUTEX`.
/// struct MyStruct {
///     my_counter: GlobalLockedBy<u32, MY_MUTEX>,
/// }
///
/// impl MyStruct {
///     /// Increment the counter in this instance.
///     ///
///     /// The caller must hold the `MY_MUTEX` mutex.
///     fn increment(&self, guard: &mut GlobalGuard<MY_MUTEX>) -> u32 {
///         let my_counter = self.my_counter.as_mut(guard);
///         *my_counter += 1;
///         *my_counter
///     }
/// }
///
/// impl kernel::Module for MyModule {
///     fn init(_module: &'static ThisModule) -> Result<Self> {
///         // SAFETY: Called exactly once.
///         unsafe { MY_MUTEX.init() };
///
///         Ok(MyModule {})
///     }
/// }
/// # struct MyModule {}
/// # }
/// ```
#[macro_export]
macro_rules! global_lock {
    {
        $(#[$meta:meta])* $pub:vis
        unsafe(uninit) static $name:ident: $kind:ident<$valuety:ty> = $value:expr;
    } => {
        #[doc = ::core::concat!(
            "Backend type used by [`",
            ::core::stringify!($name),
            "`](static@",
            ::core::stringify!($name),
            ")."
        )]
        #[allow(non_camel_case_types, unreachable_pub)]
        $pub enum $name {}

        impl $crate::sync::lock::GlobalLockBackend for $name {
            const NAME: &'static $crate::str::CStr = $crate::c_str!(::core::stringify!($name));
            type Item = $valuety;
            type Backend = $crate::global_lock_inner!(backend $kind);

            fn get_lock_class() -> Pin<&'static $crate::sync::LockClassKey> {
                $crate::static_lock_class!()
            }
        }

        $(#[$meta])*
        $pub static $name: $crate::sync::lock::GlobalLock<$name> = {
            // Defined here to be outside the unsafe scope.
            let init: $valuety = $value;

            // SAFETY:
            // * The user of this macro promises to initialize the macro before use.
            // * We are only generating one static with this backend type.
            unsafe { $crate::sync::lock::GlobalLock::new(init) }
        };
    };
}
pub use global_lock;

#[doc(hidden)]
#[macro_export]
macro_rules! global_lock_inner {
    (backend Mutex) => {
        $crate::sync::lock::mutex::MutexBackend
    };
    (backend SpinLock) => {
        $crate::sync::lock::spinlock::SpinLockBackend
    };
}
