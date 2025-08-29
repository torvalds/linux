// SPDX-License-Identifier: GPL-2.0

//! Kernel errors.
//!
//! C header: [`include/uapi/asm-generic/errno-base.h`](srctree/include/uapi/asm-generic/errno-base.h)\
//! C header: [`include/uapi/asm-generic/errno.h`](srctree/include/uapi/asm-generic/errno.h)\
//! C header: [`include/linux/errno.h`](srctree/include/linux/errno.h)

use crate::{
    alloc::{layout::LayoutError, AllocError},
    fmt,
    str::CStr,
};

use core::num::NonZeroI32;
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
            pub const $err: super::Error =
                match super::Error::try_from_errno(-(crate::bindings::$err as i32)) {
                    Some(err) => err,
                    None => panic!("Invalid errno in `declare_err!`"),
                };
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
    declare_err!(ECHILD, "No child processes.");
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
    declare_err!(EOVERFLOW, "Value too large for defined data type.");
    declare_err!(ETIMEDOUT, "Connection timed out.");
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
pub struct Error(NonZeroI32);

impl Error {
    /// Creates an [`Error`] from a kernel error code.
    ///
    /// `errno` must be within error code range (i.e. `>= -MAX_ERRNO && < 0`).
    ///
    /// It is a bug to pass an out-of-range `errno`. [`code::EINVAL`] is returned in such a case.
    ///
    /// # Examples
    ///
    /// ```
    /// assert_eq!(Error::from_errno(-1), EPERM);
    /// assert_eq!(Error::from_errno(-2), ENOENT);
    /// ```
    ///
    /// The following calls are considered a bug:
    ///
    /// ```
    /// assert_eq!(Error::from_errno(0), EINVAL);
    /// assert_eq!(Error::from_errno(-1000000), EINVAL);
    /// ```
    pub fn from_errno(errno: crate::ffi::c_int) -> Error {
        if let Some(error) = Self::try_from_errno(errno) {
            error
        } else {
            // TODO: Make it a `WARN_ONCE` once available.
            crate::pr_warn!(
                "attempted to create `Error` with out of range `errno`: {}\n",
                errno
            );
            code::EINVAL
        }
    }

    /// Creates an [`Error`] from a kernel error code.
    ///
    /// Returns [`None`] if `errno` is out-of-range.
    const fn try_from_errno(errno: crate::ffi::c_int) -> Option<Error> {
        if errno < -(bindings::MAX_ERRNO as i32) || errno >= 0 {
            return None;
        }

        // SAFETY: `errno` is checked above to be in a valid range.
        Some(unsafe { Error::from_errno_unchecked(errno) })
    }

    /// Creates an [`Error`] from a kernel error code.
    ///
    /// # Safety
    ///
    /// `errno` must be within error code range (i.e. `>= -MAX_ERRNO && < 0`).
    const unsafe fn from_errno_unchecked(errno: crate::ffi::c_int) -> Error {
        // INVARIANT: The contract ensures the type invariant
        // will hold.
        // SAFETY: The caller guarantees `errno` is non-zero.
        Error(unsafe { NonZeroI32::new_unchecked(errno) })
    }

    /// Returns the kernel error code.
    pub fn to_errno(self) -> crate::ffi::c_int {
        self.0.get()
    }

    #[cfg(CONFIG_BLOCK)]
    pub(crate) fn to_blk_status(self) -> bindings::blk_status_t {
        // SAFETY: `self.0` is a valid error due to its invariant.
        unsafe { bindings::errno_to_blk_status(self.0.get()) }
    }

    /// Returns the error encoded as a pointer.
    pub fn to_ptr<T>(self) -> *mut T {
        // SAFETY: `self.0` is a valid error due to its invariant.
        unsafe { bindings::ERR_PTR(self.0.get() as crate::ffi::c_long).cast() }
    }

    /// Returns a string representing the error, if one exists.
    #[cfg(not(testlib))]
    pub fn name(&self) -> Option<&'static CStr> {
        // SAFETY: Just an FFI call, there are no extra safety requirements.
        let ptr = unsafe { bindings::errname(-self.0.get()) };
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
            Some(name) => f
                .debug_tuple(
                    // SAFETY: These strings are ASCII-only.
                    unsafe { core::str::from_utf8_unchecked(name.to_bytes()) },
                )
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

impl From<LayoutError> for Error {
    fn from(_: LayoutError) -> Error {
        code::ENOMEM
    }
}

impl From<fmt::Error> for Error {
    fn from(_: fmt::Error) -> Error {
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
/// it should still be modeled as returning a [`Result`] rather than
/// just an [`Error`].
///
/// Calling a function that returns [`Result`] forces the caller to handle
/// the returned [`Result`].
///
/// This can be done "manually" by using [`match`]. Using [`match`] to decode
/// the [`Result`] is similar to C where all the return value decoding and the
/// error handling is done explicitly by writing handling code for each
/// error to cover. Using [`match`] the error and success handling can be
/// implemented in all detail as required. For example (inspired by
/// [`samples/rust/rust_minimal.rs`]):
///
/// ```
/// # #[allow(clippy::single_match)]
/// fn example() -> Result {
///     let mut numbers = KVec::new();
///
///     match numbers.push(72, GFP_KERNEL) {
///         Err(e) => {
///             pr_err!("Error pushing 72: {e:?}");
///             return Err(e.into());
///         }
///         // Do nothing, continue.
///         Ok(()) => (),
///     }
///
///     match numbers.push(108, GFP_KERNEL) {
///         Err(e) => {
///             pr_err!("Error pushing 108: {e:?}");
///             return Err(e.into());
///         }
///         // Do nothing, continue.
///         Ok(()) => (),
///     }
///
///     match numbers.push(200, GFP_KERNEL) {
///         Err(e) => {
///             pr_err!("Error pushing 200: {e:?}");
///             return Err(e.into());
///         }
///         // Do nothing, continue.
///         Ok(()) => (),
///     }
///
///     Ok(())
/// }
/// # example()?;
/// # Ok::<(), Error>(())
/// ```
///
/// An alternative to be more concise is the [`if let`] syntax:
///
/// ```
/// fn example() -> Result {
///     let mut numbers = KVec::new();
///
///     if let Err(e) = numbers.push(72, GFP_KERNEL) {
///         pr_err!("Error pushing 72: {e:?}");
///         return Err(e.into());
///     }
///
///     if let Err(e) = numbers.push(108, GFP_KERNEL) {
///         pr_err!("Error pushing 108: {e:?}");
///         return Err(e.into());
///     }
///
///     if let Err(e) = numbers.push(200, GFP_KERNEL) {
///         pr_err!("Error pushing 200: {e:?}");
///         return Err(e.into());
///     }
///
///     Ok(())
/// }
/// # example()?;
/// # Ok::<(), Error>(())
/// ```
///
/// Instead of these verbose [`match`]/[`if let`], the [`?`] operator can
/// be used to handle the [`Result`]. Using the [`?`] operator is often
/// the best choice to handle [`Result`] in a non-verbose way as done in
/// [`samples/rust/rust_minimal.rs`]:
///
/// ```
/// fn example() -> Result {
///     let mut numbers = KVec::new();
///
///     numbers.push(72, GFP_KERNEL)?;
///     numbers.push(108, GFP_KERNEL)?;
///     numbers.push(200, GFP_KERNEL)?;
///
///     Ok(())
/// }
/// # example()?;
/// # Ok::<(), Error>(())
/// ```
///
/// Another possibility is to call [`unwrap()`](Result::unwrap) or
/// [`expect()`](Result::expect). However, use of these functions is
/// *heavily discouraged* in the kernel because they trigger a Rust
/// [`panic!`] if an error happens, which may destabilize the system or
/// entirely break it as a result -- just like the C [`BUG()`] macro.
/// Please see the documentation for the C macro [`BUG()`] for guidance
/// on when to use these functions.
///
/// Alternatively, depending on the use case, using [`unwrap_or()`],
/// [`unwrap_or_else()`], [`unwrap_or_default()`] or [`unwrap_unchecked()`]
/// might be an option, as well.
///
/// For even more details, please see the [Rust documentation].
///
/// [`match`]: https://doc.rust-lang.org/reference/expressions/match-expr.html
/// [`samples/rust/rust_minimal.rs`]: srctree/samples/rust/rust_minimal.rs
/// [`if let`]: https://doc.rust-lang.org/reference/expressions/if-expr.html#if-let-expressions
/// [`?`]: https://doc.rust-lang.org/reference/expressions/operator-expr.html#the-question-mark-operator
/// [`unwrap()`]: Result::unwrap
/// [`expect()`]: Result::expect
/// [`BUG()`]: https://docs.kernel.org/process/deprecated.html#bug-and-bug-on
/// [`unwrap_or()`]: Result::unwrap_or
/// [`unwrap_or_else()`]: Result::unwrap_or_else
/// [`unwrap_or_default()`]: Result::unwrap_or_default
/// [`unwrap_unchecked()`]: Result::unwrap_unchecked
/// [Rust documentation]: https://doc.rust-lang.org/book/ch09-02-recoverable-errors-with-result.html
pub type Result<T = (), E = Error> = core::result::Result<T, E>;

/// Converts an integer as returned by a C kernel function to a [`Result`].
///
/// If the integer is negative, an [`Err`] with an [`Error`] as given by [`Error::from_errno`] is
/// returned. This means the integer must be `>= -MAX_ERRNO`.
///
/// Otherwise, it returns [`Ok`].
///
/// It is a bug to pass an out-of-range negative integer. `Err(EINVAL)` is returned in such a case.
///
/// # Examples
///
/// This function may be used to easily perform early returns with the [`?`] operator when working
/// with C APIs within Rust abstractions:
///
/// ```
/// # use kernel::error::to_result;
/// # mod bindings {
/// #     #![expect(clippy::missing_safety_doc)]
/// #     use kernel::prelude::*;
/// #     pub(super) unsafe fn f1() -> c_int { 0 }
/// #     pub(super) unsafe fn f2() -> c_int { EINVAL.to_errno() }
/// # }
/// fn f() -> Result {
///     // SAFETY: ...
///     to_result(unsafe { bindings::f1() })?;
///
///     // SAFETY: ...
///     to_result(unsafe { bindings::f2() })?;
///
///     // ...
///
///     Ok(())
/// }
/// # assert_eq!(f(), Err(EINVAL));
/// ```
///
/// [`?`]: https://doc.rust-lang.org/reference/expressions/operator-expr.html#the-question-mark-operator
pub fn to_result(err: crate::ffi::c_int) -> Result {
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
/// ) -> Result<*mut kernel::ffi::c_void> {
///     // SAFETY: `pdev` points to a valid platform device. There are no safety requirements
///     // on `index`.
///     from_err_ptr(unsafe { bindings::devm_platform_ioremap_resource(pdev.to_ptr(), index) })
/// }
/// ```
pub fn from_err_ptr<T>(ptr: *mut T) -> Result<*mut T> {
    // CAST: Casting a pointer to `*const crate::ffi::c_void` is always valid.
    let const_ptr: *const crate::ffi::c_void = ptr.cast();
    // SAFETY: The FFI function does not deref the pointer.
    if unsafe { bindings::IS_ERR(const_ptr) } {
        // SAFETY: The FFI function does not deref the pointer.
        let err = unsafe { bindings::PTR_ERR(const_ptr) };

        #[allow(clippy::unnecessary_cast)]
        // CAST: If `IS_ERR()` returns `true`,
        // then `PTR_ERR()` is guaranteed to return a
        // negative value greater-or-equal to `-bindings::MAX_ERRNO`,
        // which always fits in an `i16`, as per the invariant above.
        // And an `i16` always fits in an `i32`. So casting `err` to
        // an `i32` can never overflow, and is always valid.
        //
        // SAFETY: `IS_ERR()` ensures `err` is a
        // negative value greater-or-equal to `-bindings::MAX_ERRNO`.
        return Err(unsafe { Error::from_errno_unchecked(err as crate::ffi::c_int) });
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
/// ) -> kernel::ffi::c_int {
///     from_result(|| {
///         let ptr = devm_alloc(pdev)?;
///         bindings::platform_set_drvdata(pdev, ptr);
///         Ok(0)
///     })
/// }
/// ```
pub fn from_result<T, F>(f: F) -> T
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

/// Error message for calling a default function of a [`#[vtable]`](macros::vtable) trait.
pub const VTABLE_DEFAULT_ERROR: &str =
    "This function must not be called, see the #[vtable] documentation.";
