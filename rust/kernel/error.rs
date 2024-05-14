// SPDX-License-Identifier: GPL-2.0

//! Kernel errors.
//!
//! C header: [`include/uapi/asm-generic/errno-base.h`](../../../include/uapi/asm-generic/errno-base.h)

use alloc::collections::TryReserveError;

/// Contains the C-compatible error codes.
pub mod code {
    /// Out of memory.
    pub const ENOMEM: super::Error = super::Error(-(crate::bindings::ENOMEM as i32));
}

/// Generic integer kernel error.
///
/// The kernel defines a set of integer generic error codes based on C and
/// POSIX ones. These codes may have a more specific meaning in some contexts.
///
/// # Invariants
///
/// The value is a valid `errno` (i.e. `>= -MAX_ERRNO && < 0`).
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct Error(core::ffi::c_int);

impl Error {
    /// Returns the kernel error code.
    pub fn to_kernel_errno(self) -> core::ffi::c_int {
        self.0
    }
}

impl From<TryReserveError> for Error {
    fn from(_: TryReserveError) -> Error {
        code::ENOMEM
    }
}

/// A [`Result`] with an [`Error`] error type.
///
/// To be used as the return type for functions that may fail.
///
/// # Error codes in C and Rust
///
/// In C, it is common that functions indicate success or failure through
/// their return value; modifying or returning extra data through non-`const`
/// pointer parameters. In particular, in the kernel, functions that may fail
/// typically return an `int` that represents a generic error code. We model
/// those as [`Error`].
///
/// In Rust, it is idiomatic to model functions that may fail as returning
/// a [`Result`]. Since in the kernel many functions return an error code,
/// [`Result`] is a type alias for a [`core::result::Result`] that uses
/// [`Error`] as its error type.
///
/// Note that even if a function does not return anything when it succeeds,
/// it should still be modeled as returning a `Result` rather than
/// just an [`Error`].
pub type Result<T = ()> = core::result::Result<T, Error>;
