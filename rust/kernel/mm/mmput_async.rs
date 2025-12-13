// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Version of `MmWithUser` using `mmput_async`.
//!
//! This is a separate file from `mm.rs` due to the dependency on `CONFIG_MMU=y`.
#![cfg(CONFIG_MMU)]

use crate::{
    bindings,
    mm::MmWithUser,
    sync::aref::{ARef, AlwaysRefCounted},
};
use core::{ops::Deref, ptr::NonNull};

/// A wrapper for the kernel's `struct mm_struct`.
///
/// This type is identical to `MmWithUser` except that it uses `mmput_async` when dropping a
/// refcount. This means that the destructor of `ARef<MmWithUserAsync>` is safe to call in atomic
/// context.
///
/// # Invariants
///
/// Values of this type are always refcounted using `mmget`. The value of `mm_users` is non-zero.
#[repr(transparent)]
pub struct MmWithUserAsync {
    mm: MmWithUser,
}

// SAFETY: It is safe to call `mmput_async` on another thread than where `mmget` was called.
unsafe impl Send for MmWithUserAsync {}
// SAFETY: All methods on `MmWithUserAsync` can be called in parallel from several threads.
unsafe impl Sync for MmWithUserAsync {}

// SAFETY: By the type invariants, this type is always refcounted.
unsafe impl AlwaysRefCounted for MmWithUserAsync {
    #[inline]
    fn inc_ref(&self) {
        // SAFETY: The pointer is valid since self is a reference.
        unsafe { bindings::mmget(self.as_raw()) };
    }

    #[inline]
    unsafe fn dec_ref(obj: NonNull<Self>) {
        // SAFETY: The caller is giving up their refcount.
        unsafe { bindings::mmput_async(obj.cast().as_ptr()) };
    }
}

// Make all `MmWithUser` methods available on `MmWithUserAsync`.
impl Deref for MmWithUserAsync {
    type Target = MmWithUser;

    #[inline]
    fn deref(&self) -> &MmWithUser {
        &self.mm
    }
}

impl MmWithUser {
    /// Use `mmput_async` when dropping this refcount.
    #[inline]
    pub fn into_mmput_async(me: ARef<MmWithUser>) -> ARef<MmWithUserAsync> {
        // SAFETY: The layouts and invariants are compatible.
        unsafe { ARef::from_raw(ARef::into_raw(me).cast()) }
    }
}
