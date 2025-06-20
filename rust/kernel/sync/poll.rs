// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Utilities for working with `struct poll_table`.

use crate::{
    bindings,
    fs::File,
    prelude::*,
    sync::{CondVar, LockClassKey},
};
use core::{marker::PhantomData, ops::Deref};

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
        // SAFETY: The pointers `self.table` and `file` are valid for the duration of this call.
        // The `cv.wait_queue_head` pointer must be valid until an rcu grace period after the
        // waiter is removed. The `PollCondVar` is pinned, so before `cv.wait_queue_head` can be
        // destroyed, the destructor must run. That destructor first removes all waiters, and then
        // waits for an rcu grace period. Therefore, `cv.wait_queue_head` is valid for long enough.
        unsafe { bindings::poll_wait(file.as_ptr(), cv.wait_queue_head.get(), self.table) }
    }
}

/// A wrapper around [`CondVar`] that makes it usable with [`PollTable`].
///
/// [`CondVar`]: crate::sync::CondVar
#[pin_data(PinnedDrop)]
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
    fn drop(self: Pin<&mut Self>) {
        // Clear anything registered using `register_wait`.
        //
        // SAFETY: The pointer points at a valid `wait_queue_head`.
        unsafe { bindings::__wake_up_pollfree(self.inner.wait_queue_head.get()) };

        // Wait for epoll items to be properly removed.
        //
        // SAFETY: Just an FFI call.
        unsafe { bindings::synchronize_rcu() };
    }
}
