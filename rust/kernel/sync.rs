// SPDX-License-Identifier: GPL-2.0

//! Synchronisation primitives.
//!
//! This module contains the kernel APIs related to synchronisation that have been ported or
//! wrapped for usage by Rust code in the kernel.

use crate::pin_init;
use crate::prelude::*;
use crate::types::Opaque;

mod arc;
mod condvar;
pub mod lock;
mod locked_by;
pub mod poll;
pub mod rcu;

pub use arc::{Arc, ArcBorrow, UniqueArc};
pub use condvar::{new_condvar, CondVar, CondVarTimeoutResult};
pub use lock::global::{global_lock, GlobalGuard, GlobalLock, GlobalLockBackend, GlobalLockedBy};
pub use lock::mutex::{new_mutex, Mutex, MutexGuard};
pub use lock::spinlock::{new_spinlock, SpinLock, SpinLockGuard};
pub use locked_by::LockedBy;

/// Represents a lockdep class. It's a wrapper around C's `lock_class_key`.
#[repr(transparent)]
#[pin_data(PinnedDrop)]
pub struct LockClassKey {
    #[pin]
    inner: Opaque<bindings::lock_class_key>,
}

// SAFETY: `bindings::lock_class_key` is designed to be used concurrently from multiple threads and
// provides its own synchronization.
unsafe impl Sync for LockClassKey {}

impl LockClassKey {
    /// Initializes a dynamically allocated lock class key. In the common case of using a
    /// statically allocated lock class key, the static_lock_class! macro should be used instead.
    ///
    /// # Example
    /// ```
    /// # use kernel::{c_str, stack_pin_init};
    /// # use kernel::alloc::KBox;
    /// # use kernel::types::ForeignOwnable;
    /// # use kernel::sync::{LockClassKey, SpinLock};
    ///
    /// let key = KBox::pin_init(LockClassKey::new_dynamic(), GFP_KERNEL)?;
    /// let key_ptr = key.into_foreign();
    ///
    /// {
    ///     stack_pin_init!(let num: SpinLock<u32> = SpinLock::new(
    ///         0,
    ///         c_str!("my_spinlock"),
    ///         // SAFETY: `key_ptr` is returned by the above `into_foreign()`, whose
    ///         // `from_foreign()` has not yet been called.
    ///         unsafe { <Pin<KBox<LockClassKey>> as ForeignOwnable>::borrow(key_ptr) }
    ///     ));
    /// }
    ///
    /// // SAFETY: We dropped `num`, the only use of the key, so the result of the previous
    /// // `borrow` has also been dropped. Thus, it's safe to use from_foreign.
    /// unsafe { drop(<Pin<KBox<LockClassKey>> as ForeignOwnable>::from_foreign(key_ptr)) };
    ///
    /// # Ok::<(), Error>(())
    /// ```
    pub fn new_dynamic() -> impl PinInit<Self> {
        pin_init!(Self {
            // SAFETY: lockdep_register_key expects an uninitialized block of memory
            inner <- Opaque::ffi_init(|slot| unsafe { bindings::lockdep_register_key(slot) })
        })
    }

    pub(crate) fn as_ptr(&self) -> *mut bindings::lock_class_key {
        self.inner.get()
    }
}

#[pinned_drop]
impl PinnedDrop for LockClassKey {
    fn drop(self: Pin<&mut Self>) {
        // SAFETY: self.as_ptr was registered with lockdep and self is pinned, so the address
        // hasn't changed. Thus, it's safe to pass to unregister.
        unsafe { bindings::lockdep_unregister_key(self.as_ptr()) }
    }
}

/// Defines a new static lock class and returns a pointer to it.
#[doc(hidden)]
#[macro_export]
macro_rules! static_lock_class {
    () => {{
        static CLASS: $crate::sync::LockClassKey =
            // SAFETY: lockdep expects uninitialized memory when it's handed a statically allocated
            // lock_class_key
            unsafe { ::core::mem::MaybeUninit::uninit().assume_init() };
        $crate::prelude::Pin::static_ref(&CLASS)
    }};
}

/// Returns the given string, if one is provided, otherwise generates one based on the source code
/// location.
#[doc(hidden)]
#[macro_export]
macro_rules! optional_name {
    () => {
        $crate::c_str!(::core::concat!(::core::file!(), ":", ::core::line!()))
    };
    ($name:literal) => {
        $crate::c_str!($name)
    };
}
