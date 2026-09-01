// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Utilities for working with `struct poll_table`.

use crate::{
    alloc::AllocError,
    bindings,
    fs::File,
    prelude::*,
    sync::{
        rcu::synchronize_rcu,
        CondVar,
        LockClassKey, //
    }, //
    types::Opaque, //
};
use core::{
    marker::PhantomData,
    mem::ManuallyDrop,
    ops::Deref, //
};

/// Creates a [`PollCondVar`] initialiser with the given name and a newly-created lock class.
#[macro_export]
macro_rules! new_poll_condvar {
    ($($name:literal)?) => {
        $crate::sync::poll::PollCondVar::new(
            $crate::optional_name!($($name)?), $crate::static_lock_class!()
        )
    };
}

/// Wraps the kernel's `poll_table`.
///
/// # Invariants
///
/// The pointer must be null or reference a valid `poll_table`.
#[repr(transparent)]
pub struct PollTable<'a> {
    table: *mut bindings::poll_table,
    _lifetime: PhantomData<&'a bindings::poll_table>,
}

impl<'a> PollTable<'a> {
    /// Creates a [`PollTable`] from a valid pointer.
    ///
    /// # Safety
    ///
    /// The pointer must be null or reference a valid `poll_table` for the duration of `'a`.
    pub unsafe fn from_raw(table: *mut bindings::poll_table) -> Self {
        // INVARIANTS: The safety requirements are the same as the struct invariants.
        PollTable {
            table,
            _lifetime: PhantomData,
        }
    }

    /// Register this [`PollTable`] with the provided [`PollCondVar`], so that it can be notified
    /// using the condition variable.
    pub fn register_wait(&self, file: &File, cv: &PollCondVar) {
        // SAFETY:
        // * `file.as_ptr()` references a valid file for the duration of this call.
        // * `self.table` is null or references a valid poll_table for the duration of this call.
        // * Since `PollCondVar` is pinned, its destructor is guaranteed to run before the memory
        //   containing `cv.wait_queue_head` is invalidated. Since the destructor clears all
        //   waiters and then waits for an rcu grace period, it's guaranteed that
        //   `cv.wait_queue_head` remains valid for at least an rcu grace period after the removal
        //   of the last waiter.
        unsafe { bindings::poll_wait(file.as_ptr(), cv.wait_queue_head.get(), self.table) }
    }
}

/// A wrapper around [`CondVar`] that makes it usable with [`PollTable`].
///
/// [`CondVar`]: crate::sync::CondVar
#[pin_data(PinnedDrop)]
#[repr(transparent)]
pub struct PollCondVar {
    #[pin]
    inner: CondVar,
}

impl PollCondVar {
    /// Constructs a new condvar initialiser.
    pub fn new(name: &'static CStr, key: Pin<&'static LockClassKey>) -> impl PinInit<Self> {
        pin_init!(Self {
            inner <- CondVar::new(name, key),
        })
    }
}

// Make the `CondVar` methods callable on `PollCondVar`.
impl Deref for PollCondVar {
    type Target = CondVar;

    fn deref(&self) -> &CondVar {
        &self.inner
    }
}

#[pinned_drop]
impl PinnedDrop for PollCondVar {
    #[inline]
    fn drop(self: Pin<&mut Self>) {
        // Clear anything registered using `register_wait`.
        //
        // SAFETY: The pointer points at a valid `wait_queue_head`.
        unsafe { bindings::__wake_up_pollfree(self.inner.wait_queue_head.get()) };

        // Wait for epoll items to be properly removed.
        synchronize_rcu();
    }
}

/// A [`KBox<PollCondVar>`] that uses `kfree_rcu`.
///
/// [`KBox<PollCondVar>`]: PollCondVar
pub struct PollCondVarBox {
    inner: ManuallyDrop<Pin<KBox<PollCondVarBoxInner>>>,
}

#[pin_data]
#[repr(C)]
struct PollCondVarBoxInner {
    #[pin]
    inner: PollCondVar,
    rcu: Opaque<bindings::kvfree_rcu_head>,
}

// SAFETY: PollCondVar is Send
unsafe impl Send for PollCondVarBoxInner {}
// SAFETY: PollCondVar is Sync
unsafe impl Sync for PollCondVarBoxInner {}

impl PollCondVarBox {
    /// Constructs a new boxed [`PollCondVar`].
    pub fn new(name: &'static CStr, key: Pin<&'static LockClassKey>) -> Result<Self, AllocError> {
        let b = KBox::pin_init(
            pin_init!(PollCondVarBoxInner {
                inner <- PollCondVar::new(name, key),
                rcu: Opaque::uninit(),
            }),
            GFP_KERNEL,
        )
        .map_err(|_| AllocError)?;

        Ok(PollCondVarBox {
            inner: ManuallyDrop::new(b),
        })
    }
}

impl Deref for PollCondVarBox {
    type Target = PollCondVar;
    fn deref(&self) -> &PollCondVar {
        &self.inner.inner
    }
}

impl Drop for PollCondVarBox {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: ManuallyDrop::take ok because not already taken.
        let boxed = unsafe { ManuallyDrop::take(&mut self.inner) };

        // SAFETY: The code below frees the box without calling the actual destructor of the type,
        // but it's okay because it re-implements the destructor using `kfree_rcu()` in place of
        // `synchronize_rcu()`.
        let ptr = KBox::into_raw(unsafe { Pin::into_inner_unchecked(boxed) });

        // SAFETY: The pointer points at a valid `wait_queue_head`.
        unsafe { bindings::__wake_up_pollfree((*ptr).inner.inner.wait_queue_head.get()) };

        // SAFETY: This was allocated using `KBox::pin_init`, so it can be freed with `kvfree`.
        unsafe { bindings::kvfree_call_rcu((*ptr).rcu.get(), ptr.cast::<ffi::c_void>()) };
    }
}
