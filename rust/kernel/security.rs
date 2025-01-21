// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Linux Security Modules (LSM).
//!
//! C header: [`include/linux/security.h`](srctree/include/linux/security.h).

use crate::{
    bindings,
    error::{to_result, Result},
};

/// A security context string.
///
/// # Invariants
///
/// The `secdata` and `seclen` fields correspond to a valid security context as returned by a
/// successful call to `security_secid_to_secctx`, that has not yet been destroyed by calling
/// `security_release_secctx`.
pub struct SecurityCtx {
    secdata: *mut core::ffi::c_char,
    seclen: usize,
}

impl SecurityCtx {
    /// Get the security context given its id.
    pub fn from_secid(secid: u32) -> Result<Self> {
        let mut secdata = core::ptr::null_mut();
        let mut seclen = 0u32;
        // SAFETY: Just a C FFI call. The pointers are valid for writes.
        to_result(unsafe { bindings::security_secid_to_secctx(secid, &mut secdata, &mut seclen) })?;

        // INVARIANT: If the above call did not fail, then we have a valid security context.
        Ok(Self {
            secdata,
            seclen: seclen as usize,
        })
    }

    /// Returns whether the security context is empty.
    pub fn is_empty(&self) -> bool {
        self.seclen == 0
    }

    /// Returns the length of this security context.
    pub fn len(&self) -> usize {
        self.seclen
    }

    /// Returns the bytes for this security context.
    pub fn as_bytes(&self) -> &[u8] {
        let ptr = self.secdata;
        if ptr.is_null() {
            debug_assert_eq!(self.seclen, 0);
            // We can't pass a null pointer to `slice::from_raw_parts` even if the length is zero.
            return &[];
        }

        // SAFETY: The call to `security_secid_to_secctx` guarantees that the pointer is valid for
        // `seclen` bytes. Furthermore, if the length is zero, then we have ensured that the
        // pointer is not null.
        unsafe { core::slice::from_raw_parts(ptr.cast(), self.seclen) }
    }
}

impl Drop for SecurityCtx {
    fn drop(&mut self) {
        // SAFETY: By the invariant of `Self`, this frees a pointer that came from a successful
        // call to `security_secid_to_secctx` and has not yet been destroyed by
        // `security_release_secctx`.
        unsafe { bindings::security_release_secctx(self.secdata, self.seclen as u32) };
    }
}
