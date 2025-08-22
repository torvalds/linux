// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Kernel IO callbacks.
//!
//! C headers: [`include/linux/fs.h`](srctree/include/linux/fs.h)

use core::marker::PhantomData;
use core::ptr::NonNull;
use kernel::types::ForeignOwnable;

/// Wrapper for the kernel's `struct kiocb`.
///
/// Currently this abstractions is incomplete and is essentially just a tuple containing a
/// reference to a file and a file position.
///
/// The type `T` represents the filesystem or driver specific data associated with the file.
///
/// # Invariants
///
/// `inner` points at a valid `struct kiocb` whose file has the type `T` as its private data.
pub struct Kiocb<'a, T> {
    inner: NonNull<bindings::kiocb>,
    _phantom: PhantomData<&'a T>,
}

impl<'a, T: ForeignOwnable> Kiocb<'a, T> {
    /// Create a `Kiocb` from a raw pointer.
    ///
    /// # Safety
    ///
    /// The pointer must reference a valid `struct kiocb` for the duration of `'a`. The private
    /// data of the file must be `T`.
    pub unsafe fn from_raw(kiocb: *mut bindings::kiocb) -> Self {
        Self {
            // SAFETY: If a pointer is valid it is not null.
            inner: unsafe { NonNull::new_unchecked(kiocb) },
            _phantom: PhantomData,
        }
    }

    /// Access the underlying `struct kiocb` directly.
    pub fn as_raw(&self) -> *mut bindings::kiocb {
        self.inner.as_ptr()
    }

    /// Get the filesystem or driver specific data associated with the file.
    pub fn file(&self) -> <T as ForeignOwnable>::Borrowed<'a> {
        // SAFETY: We have shared access to this kiocb and hence the underlying file, so we can
        // read the file's private data.
        let private = unsafe { (*(*self.as_raw()).ki_filp).private_data };
        // SAFETY: The kiocb has shared access to the private data.
        unsafe { <T as ForeignOwnable>::borrow(private) }
    }

    /// Gets the current value of `ki_pos`.
    pub fn ki_pos(&self) -> i64 {
        // SAFETY: We have shared access to the kiocb, so we can read its `ki_pos` field.
        unsafe { (*self.as_raw()).ki_pos }
    }

    /// Gets a mutable reference to the `ki_pos` field.
    pub fn ki_pos_mut(&mut self) -> &mut i64 {
        // SAFETY: We have exclusive access to the kiocb, so we can write to `ki_pos`.
        unsafe { &mut (*self.as_raw()).ki_pos }
    }
}
