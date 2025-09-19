// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Linux Security Modules (LSM).
//!
//! C header: [`include/linux/security.h`](srctree/include/linux/security.h).

use crate::{
    bindings,
    cred::Credential,
    error::{to_result, Result},
    fs::File,
};

/// Calls the security modules to determine if the given task can become the manager of a binder
/// context.
#[inline]
pub fn binder_set_context_mgr(mgr: &Credential) -> Result {
    // SAFETY: `mrg.0` is valid because the shared reference guarantees a nonzero refcount.
    to_result(unsafe { bindings::security_binder_set_context_mgr(mgr.as_ptr()) })
}

/// Calls the security modules to determine if binder transactions are allowed from task `from` to
/// task `to`.
#[inline]
pub fn binder_transaction(from: &Credential, to: &Credential) -> Result {
    // SAFETY: `from` and `to` are valid because the shared references guarantee nonzero refcounts.
    to_result(unsafe { bindings::security_binder_transaction(from.as_ptr(), to.as_ptr()) })
}

/// Calls the security modules to determine if task `from` is allowed to send binder objects
/// (owned by itself or other processes) to task `to` through a binder transaction.
#[inline]
pub fn binder_transfer_binder(from: &Credential, to: &Credential) -> Result {
    // SAFETY: `from` and `to` are valid because the shared references guarantee nonzero refcounts.
    to_result(unsafe { bindings::security_binder_transfer_binder(from.as_ptr(), to.as_ptr()) })
}

/// Calls the security modules to determine if task `from` is allowed to send the given file to
/// task `to` (which would get its own file descriptor) through a binder transaction.
#[inline]
pub fn binder_transfer_file(from: &Credential, to: &Credential, file: &File) -> Result {
    // SAFETY: `from`, `to` and `file` are valid because the shared references guarantee nonzero
    // refcounts.
    to_result(unsafe {
        bindings::security_binder_transfer_file(from.as_ptr(), to.as_ptr(), file.as_ptr())
    })
}

/// A security context string.
///
/// # Invariants
///
/// The `ctx` field corresponds to a valid security context as returned by a successful call to
/// `security_secid_to_secctx`, that has not yet been released by `security_release_secctx`.
pub struct SecurityCtx {
    ctx: bindings::lsm_context,
}

impl SecurityCtx {
    /// Get the security context given its id.
    #[inline]
    pub fn from_secid(secid: u32) -> Result<Self> {
        // SAFETY: `struct lsm_context` can be initialized to all zeros.
        let mut ctx: bindings::lsm_context = unsafe { core::mem::zeroed() };

        // SAFETY: Just a C FFI call. The pointer is valid for writes.
        to_result(unsafe { bindings::security_secid_to_secctx(secid, &mut ctx) })?;

        // INVARIANT: If the above call did not fail, then we have a valid security context.
        Ok(Self { ctx })
    }

    /// Returns whether the security context is empty.
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.ctx.len == 0
    }

    /// Returns the length of this security context.
    #[inline]
    pub fn len(&self) -> usize {
        self.ctx.len as usize
    }

    /// Returns the bytes for this security context.
    #[inline]
    pub fn as_bytes(&self) -> &[u8] {
        let ptr = self.ctx.context;
        if ptr.is_null() {
            debug_assert_eq!(self.len(), 0);
            // We can't pass a null pointer to `slice::from_raw_parts` even if the length is zero.
            return &[];
        }

        // SAFETY: The call to `security_secid_to_secctx` guarantees that the pointer is valid for
        // `self.len()` bytes. Furthermore, if the length is zero, then we have ensured that the
        // pointer is not null.
        unsafe { core::slice::from_raw_parts(ptr.cast(), self.len()) }
    }
}

impl Drop for SecurityCtx {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: By the invariant of `Self`, this releases an lsm context that came from a
        // successful call to `security_secid_to_secctx` and has not yet been released.
        unsafe { bindings::security_release_secctx(&mut self.ctx) };
    }
}
