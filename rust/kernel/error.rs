// SPDX-License-Identifier: GPL-2.0

//! Kernel errors.
//!
//! C header: [`include/uapi/asm-generic/errno-base.h`](../../../include/uapi/asm-generic/errno-base.h)

use alloc::{
    alloc::{AllocError, LayoutError},
    collections::TryReserveError,
};

use core::convert::From;
use core::num::TryFromIntError;
use core::str::Utf8Error;

/// Contains the C-compatible error codes.
pub mod code {
    macro_rules! declare_err {
        ($err:tt $(,)? $($doc:expr),+) => {
            $(
            #[doc = $doc]
            )*
            pub const $err: super::Error = super::Error(-(crate::bindings::$err as i32));
        };
    }

    declare_err!(EPERM, "Operation not permitted.");
    declare_err!(ENOENT, "No such file or directory.");
    declare_err!(ESRCH, "No such process.");
    declare_err!(EINTR, "Interrupted system call.");
    declare_err!(EIO, "I/O error.");
    declare_err!(ENXIO, "No such device or address.");
    declare_err!(E2BIG, "Argument list too long.");
    declare_err!(ENOEXEC, "Exec format error.");
    declare_err!(EBADF, "Bad file number.");
    declare_err!(ECHILD, "Exec format error.");
    declare_err!(EAGAIN, "Try again.");
    declare_err!(ENOMEM, "Out of memory.");
    declare_err!(EACCES, "Permission denied.");
    declare_err!(EFAULT, "Bad address.");
    declare_err!(ENOTBLK, "Block device required.");
    declare_err!(EBUSY, "Device or resource busy.");
    declare_err!(EEXIST, "File exists.");
    declare_err!(EXDEV, "Cross-device link.");
    declare_err!(ENODEV, "No such device.");
    declare_err!(ENOTDIR, "Not a directory.");
    declare_err!(EISDIR, "Is a directory.");
    declare_err!(EINVAL, "Invalid argument.");
    declare_err!(ENFILE, "File table overflow.");
    declare_err!(EMFILE, "Too many open files.");
    declare_err!(ENOTTY, "Not a typewriter.");
    declare_err!(ETXTBSY, "Text file busy.");
    declare_err!(EFBIG, "File too large.");
    declare_err!(ENOSPC, "No space left on device.");
    declare_err!(ESPIPE, "Illegal seek.");
    declare_err!(EROFS, "Read-only file system.");
    declare_err!(EMLINK, "Too many links.");
    declare_err!(EPIPE, "Broken pipe.");
    declare_err!(EDOM, "Math argument out of domain of func.");
    declare_err!(ERANGE, "Math result not representable.");
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

impl From<AllocError> for Error {
    fn from(_: AllocError) -> Error {
        code::ENOMEM
    }
}

impl From<TryFromIntError> for Error {
    fn from(_: TryFromIntError) -> Error {
        code::EINVAL
    }
}

impl From<Utf8Error> for Error {
    fn from(_: Utf8Error) -> Error {
        code::EINVAL
    }
}

impl From<TryReserveError> for Error {
    fn from(_: TryReserveError) -> Error {
        code::ENOMEM
    }
}

impl From<LayoutError> for Error {
    fn from(_: LayoutError) -> Error {
        code::ENOMEM
    }
}

impl From<core::fmt::Error> for Error {
    fn from(_: core::fmt::Error) -> Error {
        code::EINVAL
    }
}

impl From<core::convert::Infallible> for Error {
    fn from(e: core::convert::Infallible) -> Error {
        match e {}
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
