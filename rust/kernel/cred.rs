// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Credentials management.
//!
//! C header: [`include/linux/cred.h`](srctree/include/linux/cred.h).
//!
//! Reference: <https://www.kernel.org/doc/html/latest/security/credentials.html>

use crate::{
    bindings,
    types::{AlwaysRefCounted, Opaque},
};

/// Wraps the kernel's `struct cred`.
///
/// Credentials are used for various security checks in the kernel.
///
/// Most fields of credentials are immutable. When things have their credentials changed, that
/// happens by replacing the credential instead of changing an existing credential. See the [kernel
/// documentation][ref] for more info on this.
///
/// # Invariants
///
/// Instances of this type are always ref-counted, that is, a call to `get_cred` ensures that the
/// allocation remains valid at least until the matching call to `put_cred`.
///
/// [ref]: https://www.kernel.org/doc/html/latest/security/credentials.html
#[repr(transparent)]
pub struct Credential(Opaque<bindings::cred>);

// SAFETY:
// - `Credential::dec_ref` can be called from any thread.
// - It is okay to send ownership of `Credential` across thread boundaries.
unsafe impl Send for Credential {}

// SAFETY: It's OK to access `Credential` through shared references from other threads because
// we're either accessing properties that don't change or that are properly synchronised by C code.
unsafe impl Sync for Credential {}

impl Credential {
    /// Creates a reference to a [`Credential`] from a valid pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid and remains valid for the lifetime of the
    /// returned [`Credential`] reference.
    pub unsafe fn from_ptr<'a>(ptr: *const bindings::cred) -> &'a Credential {
        // SAFETY: The safety requirements guarantee the validity of the dereference, while the
        // `Credential` type being transparent makes the cast ok.
        unsafe { &*ptr.cast() }
    }

    /// Get the id for this security context.
    pub fn get_secid(&self) -> u32 {
        let mut secid = 0;
        // SAFETY: The invariants of this type ensures that the pointer is valid.
        unsafe { bindings::security_cred_getsecid(self.0.get(), &mut secid) };
        secid
    }

    /// Returns the effective UID of the given credential.
    pub fn euid(&self) -> bindings::kuid_t {
        // SAFETY: By the type invariant, we know that `self.0` is valid. Furthermore, the `euid`
        // field of a credential is never changed after initialization, so there is no potential
        // for data races.
        unsafe { (*self.0.get()).euid }
    }
}

// SAFETY: The type invariants guarantee that `Credential` is always ref-counted.
unsafe impl AlwaysRefCounted for Credential {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference means that the refcount is nonzero.
        unsafe { bindings::get_cred(self.0.get()) };
    }

    unsafe fn dec_ref(obj: core::ptr::NonNull<Credential>) {
        // SAFETY: The safety requirements guarantee that the refcount is nonzero. The cast is okay
        // because `Credential` has the same representation as `struct cred`.
        unsafe { bindings::put_cred(obj.cast().as_ptr()) };
    }
}
