// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Memory management.
//!
//! This module deals with managing the address space of userspace processes. Each process has an
//! instance of [`Mm`], which keeps track of multiple VMAs (virtual memory areas). Each VMA
//! corresponds to a region of memory that the userspace process can access, and the VMA lets you
//! control what happens when userspace reads or writes to that region of memory.
//!
//! C header: [`include/linux/mm.h`](srctree/include/linux/mm.h)
#![cfg(CONFIG_MMU)]

use crate::{
    bindings,
    types::{ARef, AlwaysRefCounted, NotThreadSafe, Opaque},
};
use core::{ops::Deref, ptr::NonNull};

/// A wrapper for the kernel's `struct mm_struct`.
///
/// This represents the address space of a userspace process, so each process has one `Mm`
/// instance. It may hold many VMAs internally.
///
/// There is a counter called `mm_users` that counts the users of the address space; this includes
/// the userspace process itself, but can also include kernel threads accessing the address space.
/// Once `mm_users` reaches zero, this indicates that the address space can be destroyed. To access
/// the address space, you must prevent `mm_users` from reaching zero while you are accessing it.
/// The [`MmWithUser`] type represents an address space where this is guaranteed, and you can
/// create one using [`mmget_not_zero`].
///
/// The `ARef<Mm>` smart pointer holds an `mmgrab` refcount. Its destructor may sleep.
///
/// # Invariants
///
/// Values of this type are always refcounted using `mmgrab`.
///
/// [`mmget_not_zero`]: Mm::mmget_not_zero
#[repr(transparent)]
pub struct Mm {
    mm: Opaque<bindings::mm_struct>,
}

// SAFETY: It is safe to call `mmdrop` on another thread than where `mmgrab` was called.
unsafe impl Send for Mm {}
// SAFETY: All methods on `Mm` can be called in parallel from several threads.
unsafe impl Sync for Mm {}

// SAFETY: By the type invariants, this type is always refcounted.
unsafe impl AlwaysRefCounted for Mm {
    #[inline]
    fn inc_ref(&self) {
        // SAFETY: The pointer is valid since self is a reference.
        unsafe { bindings::mmgrab(self.as_raw()) };
    }

    #[inline]
    unsafe fn dec_ref(obj: NonNull<Self>) {
        // SAFETY: The caller is giving up their refcount.
        unsafe { bindings::mmdrop(obj.cast().as_ptr()) };
    }
}

/// A wrapper for the kernel's `struct mm_struct`.
///
/// This type is like [`Mm`], but with non-zero `mm_users`. It can only be used when `mm_users` can
/// be proven to be non-zero at compile-time, usually because the relevant code holds an `mmget`
/// refcount. It can be used to access the associated address space.
///
/// The `ARef<MmWithUser>` smart pointer holds an `mmget` refcount. Its destructor may sleep.
///
/// # Invariants
///
/// Values of this type are always refcounted using `mmget`. The value of `mm_users` is non-zero.
#[repr(transparent)]
pub struct MmWithUser {
    mm: Mm,
}

// SAFETY: It is safe to call `mmput` on another thread than where `mmget` was called.
unsafe impl Send for MmWithUser {}
// SAFETY: All methods on `MmWithUser` can be called in parallel from several threads.
unsafe impl Sync for MmWithUser {}

// SAFETY: By the type invariants, this type is always refcounted.
unsafe impl AlwaysRefCounted for MmWithUser {
    #[inline]
    fn inc_ref(&self) {
        // SAFETY: The pointer is valid since self is a reference.
        unsafe { bindings::mmget(self.as_raw()) };
    }

    #[inline]
    unsafe fn dec_ref(obj: NonNull<Self>) {
        // SAFETY: The caller is giving up their refcount.
        unsafe { bindings::mmput(obj.cast().as_ptr()) };
    }
}

// Make all `Mm` methods available on `MmWithUser`.
impl Deref for MmWithUser {
    type Target = Mm;

    #[inline]
    fn deref(&self) -> &Mm {
        &self.mm
    }
}

// These methods are safe to call even if `mm_users` is zero.
impl Mm {
    /// Returns a raw pointer to the inner `mm_struct`.
    #[inline]
    pub fn as_raw(&self) -> *mut bindings::mm_struct {
        self.mm.get()
    }

    /// Obtain a reference from a raw pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` points at an `mm_struct`, and that it is not deallocated
    /// during the lifetime 'a.
    #[inline]
    pub unsafe fn from_raw<'a>(ptr: *const bindings::mm_struct) -> &'a Mm {
        // SAFETY: Caller promises that the pointer is valid for 'a. Layouts are compatible due to
        // repr(transparent).
        unsafe { &*ptr.cast() }
    }

    /// Calls `mmget_not_zero` and returns a handle if it succeeds.
    #[inline]
    pub fn mmget_not_zero(&self) -> Option<ARef<MmWithUser>> {
        // SAFETY: The pointer is valid since self is a reference.
        let success = unsafe { bindings::mmget_not_zero(self.as_raw()) };

        if success {
            // SAFETY: We just created an `mmget` refcount.
            Some(unsafe { ARef::from_raw(NonNull::new_unchecked(self.as_raw().cast())) })
        } else {
            None
        }
    }
}

// These methods require `mm_users` to be non-zero.
impl MmWithUser {
    /// Obtain a reference from a raw pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` points at an `mm_struct`, and that `mm_users` remains
    /// non-zero for the duration of the lifetime 'a.
    #[inline]
    pub unsafe fn from_raw<'a>(ptr: *const bindings::mm_struct) -> &'a MmWithUser {
        // SAFETY: Caller promises that the pointer is valid for 'a. The layout is compatible due
        // to repr(transparent).
        unsafe { &*ptr.cast() }
    }

    /// Lock the mmap read lock.
    #[inline]
    pub fn mmap_read_lock(&self) -> MmapReadGuard<'_> {
        // SAFETY: The pointer is valid since self is a reference.
        unsafe { bindings::mmap_read_lock(self.as_raw()) };

        // INVARIANT: We just acquired the read lock.
        MmapReadGuard {
            mm: self,
            _nts: NotThreadSafe,
        }
    }

    /// Try to lock the mmap read lock.
    #[inline]
    pub fn mmap_read_trylock(&self) -> Option<MmapReadGuard<'_>> {
        // SAFETY: The pointer is valid since self is a reference.
        let success = unsafe { bindings::mmap_read_trylock(self.as_raw()) };

        if success {
            // INVARIANT: We just acquired the read lock.
            Some(MmapReadGuard {
                mm: self,
                _nts: NotThreadSafe,
            })
        } else {
            None
        }
    }
}

/// A guard for the mmap read lock.
///
/// # Invariants
///
/// This `MmapReadGuard` guard owns the mmap read lock.
pub struct MmapReadGuard<'a> {
    mm: &'a MmWithUser,
    // `mmap_read_lock` and `mmap_read_unlock` must be called on the same thread
    _nts: NotThreadSafe,
}

impl Drop for MmapReadGuard<'_> {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: We hold the read lock by the type invariants.
        unsafe { bindings::mmap_read_unlock(self.mm.as_raw()) };
    }
}
