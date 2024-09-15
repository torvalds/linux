// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Files and file descriptors.
//!
//! C headers: [`include/linux/fs.h`](srctree/include/linux/fs.h) and
//! [`include/linux/file.h`](srctree/include/linux/file.h)

use crate::{
    bindings,
    cred::Credential,
    error::{code::*, Error, Result},
    types::{ARef, AlwaysRefCounted, Opaque},
};
use core::ptr;

/// Flags associated with a [`File`].
pub mod flags {
    /// File is opened in append mode.
    pub const O_APPEND: u32 = bindings::O_APPEND;

    /// Signal-driven I/O is enabled.
    pub const O_ASYNC: u32 = bindings::FASYNC;

    /// Close-on-exec flag is set.
    pub const O_CLOEXEC: u32 = bindings::O_CLOEXEC;

    /// File was created if it didn't already exist.
    pub const O_CREAT: u32 = bindings::O_CREAT;

    /// Direct I/O is enabled for this file.
    pub const O_DIRECT: u32 = bindings::O_DIRECT;

    /// File must be a directory.
    pub const O_DIRECTORY: u32 = bindings::O_DIRECTORY;

    /// Like [`O_SYNC`] except metadata is not synced.
    pub const O_DSYNC: u32 = bindings::O_DSYNC;

    /// Ensure that this file is created with the `open(2)` call.
    pub const O_EXCL: u32 = bindings::O_EXCL;

    /// Large file size enabled (`off64_t` over `off_t`).
    pub const O_LARGEFILE: u32 = bindings::O_LARGEFILE;

    /// Do not update the file last access time.
    pub const O_NOATIME: u32 = bindings::O_NOATIME;

    /// File should not be used as process's controlling terminal.
    pub const O_NOCTTY: u32 = bindings::O_NOCTTY;

    /// If basename of path is a symbolic link, fail open.
    pub const O_NOFOLLOW: u32 = bindings::O_NOFOLLOW;

    /// File is using nonblocking I/O.
    pub const O_NONBLOCK: u32 = bindings::O_NONBLOCK;

    /// File is using nonblocking I/O.
    ///
    /// This is effectively the same flag as [`O_NONBLOCK`] on all architectures
    /// except SPARC64.
    pub const O_NDELAY: u32 = bindings::O_NDELAY;

    /// Used to obtain a path file descriptor.
    pub const O_PATH: u32 = bindings::O_PATH;

    /// Write operations on this file will flush data and metadata.
    pub const O_SYNC: u32 = bindings::O_SYNC;

    /// This file is an unnamed temporary regular file.
    pub const O_TMPFILE: u32 = bindings::O_TMPFILE;

    /// File should be truncated to length 0.
    pub const O_TRUNC: u32 = bindings::O_TRUNC;

    /// Bitmask for access mode flags.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::fs::file;
    /// # fn do_something() {}
    /// # let flags = 0;
    /// if (flags & file::flags::O_ACCMODE) == file::flags::O_RDONLY {
    ///     do_something();
    /// }
    /// ```
    pub const O_ACCMODE: u32 = bindings::O_ACCMODE;

    /// File is read only.
    pub const O_RDONLY: u32 = bindings::O_RDONLY;

    /// File is write only.
    pub const O_WRONLY: u32 = bindings::O_WRONLY;

    /// File can be both read and written.
    pub const O_RDWR: u32 = bindings::O_RDWR;
}

/// Wraps the kernel's `struct file`. Thread safe.
///
/// This represents an open file rather than a file on a filesystem. Processes generally reference
/// open files using file descriptors. However, file descriptors are not the same as files. A file
/// descriptor is just an integer that corresponds to a file, and a single file may be referenced
/// by multiple file descriptors.
///
/// # Refcounting
///
/// Instances of this type are reference-counted. The reference count is incremented by the
/// `fget`/`get_file` functions and decremented by `fput`. The Rust type `ARef<File>` represents a
/// pointer that owns a reference count on the file.
///
/// Whenever a process opens a file descriptor (fd), it stores a pointer to the file in its fd
/// table (`struct files_struct`). This pointer owns a reference count to the file, ensuring the
/// file isn't prematurely deleted while the file descriptor is open. In Rust terminology, the
/// pointers in `struct files_struct` are `ARef<File>` pointers.
///
/// ## Light refcounts
///
/// Whenever a process has an fd to a file, it may use something called a "light refcount" as a
/// performance optimization. Light refcounts are acquired by calling `fdget` and released with
/// `fdput`. The idea behind light refcounts is that if the fd is not closed between the calls to
/// `fdget` and `fdput`, then the refcount cannot hit zero during that time, as the `struct
/// files_struct` holds a reference until the fd is closed. This means that it's safe to access the
/// file even if `fdget` does not increment the refcount.
///
/// The requirement that the fd is not closed during a light refcount applies globally across all
/// threads - not just on the thread using the light refcount. For this reason, light refcounts are
/// only used when the `struct files_struct` is not shared with other threads, since this ensures
/// that other unrelated threads cannot suddenly start using the fd and close it. Therefore,
/// calling `fdget` on a shared `struct files_struct` creates a normal refcount instead of a light
/// refcount.
///
/// Light reference counts must be released with `fdput` before the system call returns to
/// userspace. This means that if you wait until the current system call returns to userspace, then
/// all light refcounts that existed at the time have gone away.
///
/// ### The file position
///
/// Each `struct file` has a position integer, which is protected by the `f_pos_lock` mutex.
/// However, if the `struct file` is not shared, then the kernel may avoid taking the lock as a
/// performance optimization.
///
/// The condition for avoiding the `f_pos_lock` mutex is different from the condition for using
/// `fdget`. With `fdget`, you may avoid incrementing the refcount as long as the current fd table
/// is not shared; it is okay if there are other fd tables that also reference the same `struct
/// file`. However, `fdget_pos` can only avoid taking the `f_pos_lock` if the entire `struct file`
/// is not shared, as different processes with an fd to the same `struct file` share the same
/// position.
///
/// To represent files that are not thread safe due to this optimization, the [`LocalFile`] type is
/// used.
///
/// ## Rust references
///
/// The reference type `&File` is similar to light refcounts:
///
/// * `&File` references don't own a reference count. They can only exist as long as the reference
///   count stays positive, and can only be created when there is some mechanism in place to ensure
///   this.
///
/// * The Rust borrow-checker normally ensures this by enforcing that the `ARef<File>` from which
///   a `&File` is created outlives the `&File`.
///
/// * Using the unsafe [`File::from_raw_file`] means that it is up to the caller to ensure that the
///   `&File` only exists while the reference count is positive.
///
/// * You can think of `fdget` as using an fd to look up an `ARef<File>` in the `struct
///   files_struct` and create an `&File` from it. The "fd cannot be closed" rule is like the Rust
///   rule "the `ARef<File>` must outlive the `&File`".
///
/// # Invariants
///
/// * All instances of this type are refcounted using the `f_count` field.
/// * There must not be any active calls to `fdget_pos` on this file that did not take the
///   `f_pos_lock` mutex.
#[repr(transparent)]
pub struct File {
    inner: Opaque<bindings::file>,
}

// SAFETY: This file is known to not have any active `fdget_pos` calls that did not take the
// `f_pos_lock` mutex, so it is safe to transfer it between threads.
unsafe impl Send for File {}

// SAFETY: This file is known to not have any active `fdget_pos` calls that did not take the
// `f_pos_lock` mutex, so it is safe to access its methods from several threads in parallel.
unsafe impl Sync for File {}

// SAFETY: The type invariants guarantee that `File` is always ref-counted. This implementation
// makes `ARef<File>` own a normal refcount.
unsafe impl AlwaysRefCounted for File {
    #[inline]
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference means that the refcount is nonzero.
        unsafe { bindings::get_file(self.as_ptr()) };
    }

    #[inline]
    unsafe fn dec_ref(obj: ptr::NonNull<File>) {
        // SAFETY: To call this method, the caller passes us ownership of a normal refcount, so we
        // may drop it. The cast is okay since `File` has the same representation as `struct file`.
        unsafe { bindings::fput(obj.cast().as_ptr()) }
    }
}

/// Wraps the kernel's `struct file`. Not thread safe.
///
/// This type represents a file that is not known to be safe to transfer across thread boundaries.
/// To obtain a thread-safe [`File`], use the [`assume_no_fdget_pos`] conversion.
///
/// See the documentation for [`File`] for more information.
///
/// # Invariants
///
/// * All instances of this type are refcounted using the `f_count` field.
/// * If there is an active call to `fdget_pos` that did not take the `f_pos_lock` mutex, then it
///   must be on the same thread as this file.
///
/// [`assume_no_fdget_pos`]: LocalFile::assume_no_fdget_pos
pub struct LocalFile {
    inner: Opaque<bindings::file>,
}

// SAFETY: The type invariants guarantee that `LocalFile` is always ref-counted. This implementation
// makes `ARef<File>` own a normal refcount.
unsafe impl AlwaysRefCounted for LocalFile {
    #[inline]
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference means that the refcount is nonzero.
        unsafe { bindings::get_file(self.as_ptr()) };
    }

    #[inline]
    unsafe fn dec_ref(obj: ptr::NonNull<LocalFile>) {
        // SAFETY: To call this method, the caller passes us ownership of a normal refcount, so we
        // may drop it. The cast is okay since `File` has the same representation as `struct file`.
        unsafe { bindings::fput(obj.cast().as_ptr()) }
    }
}

impl LocalFile {
    /// Constructs a new `struct file` wrapper from a file descriptor.
    ///
    /// The file descriptor belongs to the current process, and there might be active local calls
    /// to `fdget_pos` on the same file.
    ///
    /// To obtain an `ARef<File>`, use the [`assume_no_fdget_pos`] function to convert.
    ///
    /// [`assume_no_fdget_pos`]: LocalFile::assume_no_fdget_pos
    #[inline]
    pub fn fget(fd: u32) -> Result<ARef<LocalFile>, BadFdError> {
        // SAFETY: FFI call, there are no requirements on `fd`.
        let ptr = ptr::NonNull::new(unsafe { bindings::fget(fd) }).ok_or(BadFdError)?;

        // SAFETY: `bindings::fget` created a refcount, and we pass ownership of it to the `ARef`.
        //
        // INVARIANT: This file is in the fd table on this thread, so either all `fdget_pos` calls
        // are on this thread, or the file is shared, in which case `fdget_pos` calls took the
        // `f_pos_lock` mutex.
        Ok(unsafe { ARef::from_raw(ptr.cast()) })
    }

    /// Creates a reference to a [`LocalFile`] from a valid pointer.
    ///
    /// # Safety
    ///
    /// * The caller must ensure that `ptr` points at a valid file and that the file's refcount is
    ///   positive for the duration of 'a.
    /// * The caller must ensure that if there is an active call to `fdget_pos` that did not take
    ///   the `f_pos_lock` mutex, then that call is on the current thread.
    #[inline]
    pub unsafe fn from_raw_file<'a>(ptr: *const bindings::file) -> &'a LocalFile {
        // SAFETY: The caller guarantees that the pointer is not dangling and stays valid for the
        // duration of 'a. The cast is okay because `File` is `repr(transparent)`.
        //
        // INVARIANT: The caller guarantees that there are no problematic `fdget_pos` calls.
        unsafe { &*ptr.cast() }
    }

    /// Assume that there are no active `fdget_pos` calls that prevent us from sharing this file.
    ///
    /// This makes it safe to transfer this file to other threads. No checks are performed, and
    /// using it incorrectly may lead to a data race on the file position if the file is shared
    /// with another thread.
    ///
    /// This method is intended to be used together with [`LocalFile::fget`] when the caller knows
    /// statically that there are no `fdget_pos` calls on the current thread. For example, you
    /// might use it when calling `fget` from an ioctl, since ioctls usually do not touch the file
    /// position.
    ///
    /// # Safety
    ///
    /// There must not be any active `fdget_pos` calls on the current thread.
    #[inline]
    pub unsafe fn assume_no_fdget_pos(me: ARef<LocalFile>) -> ARef<File> {
        // INVARIANT: There are no `fdget_pos` calls on the current thread, and by the type
        // invariants, if there is a `fdget_pos` call on another thread, then it took the
        // `f_pos_lock` mutex.
        //
        // SAFETY: `LocalFile` and `File` have the same layout.
        unsafe { ARef::from_raw(ARef::into_raw(me).cast()) }
    }

    /// Returns a raw pointer to the inner C struct.
    #[inline]
    pub fn as_ptr(&self) -> *mut bindings::file {
        self.inner.get()
    }

    /// Returns the credentials of the task that originally opened the file.
    pub fn cred(&self) -> &Credential {
        // SAFETY: It's okay to read the `f_cred` field without synchronization because `f_cred` is
        // never changed after initialization of the file.
        let ptr = unsafe { (*self.as_ptr()).f_cred };

        // SAFETY: The signature of this function ensures that the caller will only access the
        // returned credential while the file is still valid, and the C side ensures that the
        // credential stays valid at least as long as the file.
        unsafe { Credential::from_ptr(ptr) }
    }

    /// Returns the flags associated with the file.
    ///
    /// The flags are a combination of the constants in [`flags`].
    #[inline]
    pub fn flags(&self) -> u32 {
        // This `read_volatile` is intended to correspond to a READ_ONCE call.
        //
        // SAFETY: The file is valid because the shared reference guarantees a nonzero refcount.
        //
        // FIXME(read_once): Replace with `read_once` when available on the Rust side.
        unsafe { core::ptr::addr_of!((*self.as_ptr()).f_flags).read_volatile() }
    }
}

impl File {
    /// Creates a reference to a [`File`] from a valid pointer.
    ///
    /// # Safety
    ///
    /// * The caller must ensure that `ptr` points at a valid file and that the file's refcount is
    ///   positive for the duration of 'a.
    /// * The caller must ensure that if there are active `fdget_pos` calls on this file, then they
    ///   took the `f_pos_lock` mutex.
    #[inline]
    pub unsafe fn from_raw_file<'a>(ptr: *const bindings::file) -> &'a File {
        // SAFETY: The caller guarantees that the pointer is not dangling and stays valid for the
        // duration of 'a. The cast is okay because `File` is `repr(transparent)`.
        //
        // INVARIANT: The caller guarantees that there are no problematic `fdget_pos` calls.
        unsafe { &*ptr.cast() }
    }
}

// Make LocalFile methods available on File.
impl core::ops::Deref for File {
    type Target = LocalFile;
    #[inline]
    fn deref(&self) -> &LocalFile {
        // SAFETY: The caller provides a `&File`, and since it is a reference, it must point at a
        // valid file for the desired duration.
        //
        // By the type invariants, there are no `fdget_pos` calls that did not take the
        // `f_pos_lock` mutex.
        unsafe { LocalFile::from_raw_file(self as *const File as *const bindings::file) }
    }
}

/// Represents the `EBADF` error code.
///
/// Used for methods that can only fail with `EBADF`.
#[derive(Copy, Clone, Eq, PartialEq)]
pub struct BadFdError;

impl From<BadFdError> for Error {
    #[inline]
    fn from(_: BadFdError) -> Error {
        EBADF
    }
}

impl core::fmt::Debug for BadFdError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.pad("EBADF")
    }
}
