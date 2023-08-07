// SPDX-License-Identifier: GPL-2.0

//! Kernel errors.
//!
//! C header: [`include/uapi/asm-generic/errno-base.h`](../../../include/uapi/asm-generic/errno-base.h)

use crate::str::CStr;

use alloc::{
    alloc::{AllocError, LayoutError},
    collections::TryReserveError,
};

use core::convert::From;
use core::fmt;
use core::num::TryFromIntError;
use core::str::Utf8Error;

/// Contains the C-compatible error codes.
#[rustfmt::skip]
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
    declare_err!(ERESTARTSYS, "Restart the system call.");
    declare_err!(ERESTARTNOINTR, "System call was interrupted by a signal and will be restarted.");
    declare_err!(ERESTARTNOHAND, "Restart if no handler.");
    declare_err!(ENOIOCTLCMD, "No ioctl command.");
    declare_err!(ERESTART_RESTARTBLOCK, "Restart by calling sys_restart_syscall.");
    declare_err!(EPROBE_DEFER, "Driver requests probe retry.");
    declare_err!(EOPENSTALE, "Open found a stale dentry.");
    declare_err!(ENOPARAM, "Parameter not supported.");
    declare_err!(EBADHANDLE, "Illegal NFS file handle.");
    declare_err!(ENOTSYNC, "Update synchronization mismatch.");
    declare_err!(EBADCOOKIE, "Cookie is stale.");
    declare_err!(ENOTSUPP, "Operation is not supported.");
    declare_err!(ETOOSMALL, "Buffer or request is too small.");
    declare_err!(ESERVERFAULT, "An untranslatable error occurred.");
    declare_err!(EBADTYPE, "Type not supported by server.");
    declare_err!(EJUKEBOX, "Request initiated, but will not complete before timeout.");
    declare_err!(EIOCBQUEUED, "iocb queued, will get completion event.");
    declare_err!(ERECALLCONFLICT, "Conflict with recalled state.");
    declare_err!(ENOGRACE, "NFS file lock reclaim refused.");
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
    /// Creates an [`Error`] from a kernel error code.
    ///
    /// It is a bug to pass an out-of-range `errno`. `EINVAL` would
    /// be returned in such a case.
    pub(crate) fn from_errno(errno: core::ffi::c_int) -> Error {
        if errno < -(bindings::MAX_ERRNO as i32) || errno >= 0 {
            // TODO: Make it a `WARN_ONCE` once available.
            crate::pr_warn!(
                "attempted to create `Error` with out of range `errno`: {}",
                errno
            );
            return code::EINVAL;
        }

        // INVARIANT: The check above ensures the type invariant
        // will hold.
        Error(errno)
    }

    /// Creates an [`Error`] from a kernel error code.
    ///
    /// # Safety
    ///
    /// `errno` must be within error code range (i.e. `>= -MAX_ERRNO && < 0`).
    unsafe fn from_errno_unchecked(errno: core::ffi::c_int) -> Error {
        // INVARIANT: The contract ensures the type invariant
        // will hold.
        Error(errno)
    }

    /// Returns the kernel error code.
    pub fn to_errno(self) -> core::ffi::c_int {
        self.0
    }

    /// Returns the error encoded as a pointer.
    #[allow(dead_code)]
    pub(crate) fn to_ptr<T>(self) -> *mut T {
        // SAFETY: self.0 is a valid error due to its invariant.
        unsafe { bindings::ERR_PTR(self.0.into()) as *mut _ }
    }

    /// Returns a string representing the error, if one exists.
    #[cfg(not(testlib))]
    pub fn name(&self) -> Option<&'static CStr> {
        // SAFETY: Just an FFI call, there are no extra safety requirements.
        let ptr = unsafe { bindings::errname(-self.0) };
        if ptr.is_null() {
            None
        } else {
            // SAFETY: The string returned by `errname` is static and `NUL`-terminated.
            Some(unsafe { CStr::from_char_ptr(ptr) })
        }
    }

    /// Returns a string representing the error, if one exists.
    ///
    /// When `testlib` is configured, this always returns `None` to avoid the dependency on a
    /// kernel function so that tests that use this (e.g., by calling [`Result::unwrap`]) can still
    /// run in userspace.
    #[cfg(testlib)]
    pub fn name(&self) -> Option<&'static CStr> {
        None
    }
}

impl fmt::Debug for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.name() {
            // Print out number if no name can be found.
            None => f.debug_tuple("Error").field(&-self.0).finish(),
            // SAFETY: These strings are ASCII-only.
            Some(name) => f
                .debug_tuple(unsafe { core::str::from_utf8_unchecked(name) })
                .finish(),
        }
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
pub type Result<T = (), E = Error> = core::result::Result<T, E>;

/// Converts an integer as returned by a C kernel function to an error if it's negative, and
/// `Ok(())` otherwise.
pub fn to_result(err: core::ffi::c_int) -> Result {
    if err < 0 {
        Err(Error::from_errno(err))
    } else {
        Ok(())
    }
}

/// Transform a kernel "error pointer" to a normal pointer.
///
/// Some kernel C API functions return an "error pointer" which optionally
/// embeds an `errno`. Callers are supposed to check the returned pointer
/// for errors. This function performs the check and converts the "error pointer"
/// to a normal pointer in an idiomatic fashion.
///
/// # Examples
///
/// ```ignore
/// # use kernel::from_err_ptr;
/// # use kernel::bindings;
/// fn devm_platform_ioremap_resource(
///     pdev: &mut PlatformDevice,
///     index: u32,
/// ) -> Result<*mut core::ffi::c_void> {
///     // SAFETY: FFI call.
///     unsafe {
///         from_err_ptr(bindings::devm_platform_ioremap_resource(
///             pdev.to_ptr(),
///             index,
///         ))
///     }
/// }
/// ```
// TODO: Remove `dead_code` marker once an in-kernel client is available.
#[allow(dead_code)]
pub(crate) fn from_err_ptr<T>(ptr: *mut T) -> Result<*mut T> {
    // CAST: Casting a pointer to `*const core::ffi::c_void` is always valid.
    let const_ptr: *const core::ffi::c_void = ptr.cast();
    // SAFETY: The FFI function does not deref the pointer.
    if unsafe { bindings::IS_ERR(const_ptr) } {
        // SAFETY: The FFI function does not deref the pointer.
        let err = unsafe { bindings::PTR_ERR(const_ptr) };
        // CAST: If `IS_ERR()` returns `true`,
        // then `PTR_ERR()` is guaranteed to return a
        // negative value greater-or-equal to `-bindings::MAX_ERRNO`,
        // which always fits in an `i16`, as per the invariant above.
        // And an `i16` always fits in an `i32`. So casting `err` to
        // an `i32` can never overflow, and is always valid.
        //
        // SAFETY: `IS_ERR()` ensures `err` is a
        // negative value greater-or-equal to `-bindings::MAX_ERRNO`.
        #[allow(clippy::unnecessary_cast)]
        return Err(unsafe { Error::from_errno_unchecked(err as core::ffi::c_int) });
    }
    Ok(ptr)
}

/// Calls a closure returning a [`crate::error::Result<T>`] and converts the result to
/// a C integer result.
///
/// This is useful when calling Rust functions that return [`crate::error::Result<T>`]
/// from inside `extern "C"` functions that need to return an integer error result.
///
/// `T` should be convertible from an `i16` via `From<i16>`.
///
/// # Examples
///
/// ```ignore
/// # use kernel::from_result;
/// # use kernel::bindings;
/// unsafe extern "C" fn probe_callback(
///     pdev: *mut bindings::platform_device,
/// ) -> core::ffi::c_int {
///     from_result(|| {
///         let ptr = devm_alloc(pdev)?;
///         bindings::platform_set_drvdata(pdev, ptr);
///         Ok(0)
///     })
/// }
/// ```
// TODO: Remove `dead_code` marker once an in-kernel client is available.
#[allow(dead_code)]
pub(crate) fn from_result<T, F>(f: F) -> T
where
    T: From<i16>,
    F: FnOnce() -> Result<T>,
{
    match f() {
        Ok(v) => v,
        // NO-OVERFLOW: negative `errno`s are no smaller than `-bindings::MAX_ERRNO`,
        // `-bindings::MAX_ERRNO` fits in an `i16` as per invariant above,
        // therefore a negative `errno` always fits in an `i16` and will not overflow.
        Err(e) => T::from(e.to_errno() as i16),
    }
}
