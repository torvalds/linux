// SPDX-License-Identifier: GPL-2.0

//! Slices to user space memory regions.
//!
//! C header: [`include/linux/uaccess.h`](srctree/include/linux/uaccess.h)

use crate::{
    alloc::{Allocator, Flags},
    bindings,
    error::Result,
    ffi::c_void,
    prelude::*,
    transmute::{AsBytes, FromBytes},
};
use core::mem::{size_of, MaybeUninit};

/// The type used for userspace addresses.
pub type UserPtr = usize;

/// A pointer to an area in userspace memory, which can be either read-only or read-write.
///
/// All methods on this struct are safe: attempting to read or write on bad addresses (either out of
/// the bound of the slice or unmapped addresses) will return [`EFAULT`]. Concurrent access,
/// *including data races to/from userspace memory*, is permitted, because fundamentally another
/// userspace thread/process could always be modifying memory at the same time (in the same way that
/// userspace Rust's [`std::io`] permits data races with the contents of files on disk). In the
/// presence of a race, the exact byte values read/written are unspecified but the operation is
/// well-defined. Kernelspace code should validate its copy of data after completing a read, and not
/// expect that multiple reads of the same address will return the same value.
///
/// These APIs are designed to make it difficult to accidentally write TOCTOU (time-of-check to
/// time-of-use) bugs. Every time a memory location is read, the reader's position is advanced by
/// the read length and the next read will start from there. This helps prevent accidentally reading
/// the same location twice and causing a TOCTOU bug.
///
/// Creating a [`UserSliceReader`] and/or [`UserSliceWriter`] consumes the `UserSlice`, helping
/// ensure that there aren't multiple readers or writers to the same location.
///
/// If double-fetching a memory location is necessary for some reason, then that is done by creating
/// multiple readers to the same memory location, e.g. using [`clone_reader`].
///
/// # Examples
///
/// Takes a region of userspace memory from the current process, and modify it by adding one to
/// every byte in the region.
///
/// ```no_run
/// use kernel::ffi::c_void;
/// use kernel::error::Result;
/// use kernel::uaccess::{UserPtr, UserSlice};
///
/// fn bytes_add_one(uptr: UserPtr, len: usize) -> Result<()> {
///     let (read, mut write) = UserSlice::new(uptr, len).reader_writer();
///
///     let mut buf = KVec::new();
///     read.read_all(&mut buf, GFP_KERNEL)?;
///
///     for b in &mut buf {
///         *b = b.wrapping_add(1);
///     }
///
///     write.write_slice(&buf)?;
///     Ok(())
/// }
/// ```
///
/// Example illustrating a TOCTOU (time-of-check to time-of-use) bug.
///
/// ```no_run
/// use kernel::ffi::c_void;
/// use kernel::error::{code::EINVAL, Result};
/// use kernel::uaccess::{UserPtr, UserSlice};
///
/// /// Returns whether the data in this region is valid.
/// fn is_valid(uptr: UserPtr, len: usize) -> Result<bool> {
///     let read = UserSlice::new(uptr, len).reader();
///
///     let mut buf = KVec::new();
///     read.read_all(&mut buf, GFP_KERNEL)?;
///
///     todo!()
/// }
///
/// /// Returns the bytes behind this user pointer if they are valid.
/// fn get_bytes_if_valid(uptr: UserPtr, len: usize) -> Result<KVec<u8>> {
///     if !is_valid(uptr, len)? {
///         return Err(EINVAL);
///     }
///
///     let read = UserSlice::new(uptr, len).reader();
///
///     let mut buf = KVec::new();
///     read.read_all(&mut buf, GFP_KERNEL)?;
///
///     // THIS IS A BUG! The bytes could have changed since we checked them.
///     //
///     // To avoid this kind of bug, don't call `UserSlice::new` multiple
///     // times with the same address.
///     Ok(buf)
/// }
/// ```
///
/// [`std::io`]: https://doc.rust-lang.org/std/io/index.html
/// [`clone_reader`]: UserSliceReader::clone_reader
pub struct UserSlice {
    ptr: UserPtr,
    length: usize,
}

impl UserSlice {
    /// Constructs a user slice from a raw pointer and a length in bytes.
    ///
    /// Constructing a [`UserSlice`] performs no checks on the provided address and length, it can
    /// safely be constructed inside a kernel thread with no current userspace process. Reads and
    /// writes wrap the kernel APIs `copy_from_user` and `copy_to_user`, which check the memory map
    /// of the current process and enforce that the address range is within the user range (no
    /// additional calls to `access_ok` are needed). Validity of the pointer is checked when you
    /// attempt to read or write, not in the call to `UserSlice::new`.
    ///
    /// Callers must be careful to avoid time-of-check-time-of-use (TOCTOU) issues. The simplest way
    /// is to create a single instance of [`UserSlice`] per user memory block as it reads each byte
    /// at most once.
    pub fn new(ptr: UserPtr, length: usize) -> Self {
        UserSlice { ptr, length }
    }

    /// Reads the entirety of the user slice, appending it to the end of the provided buffer.
    ///
    /// Fails with [`EFAULT`] if the read happens on a bad address.
    pub fn read_all<A: Allocator>(self, buf: &mut Vec<u8, A>, flags: Flags) -> Result {
        self.reader().read_all(buf, flags)
    }

    /// Constructs a [`UserSliceReader`].
    pub fn reader(self) -> UserSliceReader {
        UserSliceReader {
            ptr: self.ptr,
            length: self.length,
        }
    }

    /// Constructs a [`UserSliceWriter`].
    pub fn writer(self) -> UserSliceWriter {
        UserSliceWriter {
            ptr: self.ptr,
            length: self.length,
        }
    }

    /// Constructs both a [`UserSliceReader`] and a [`UserSliceWriter`].
    ///
    /// Usually when this is used, you will first read the data, and then overwrite it afterwards.
    pub fn reader_writer(self) -> (UserSliceReader, UserSliceWriter) {
        (
            UserSliceReader {
                ptr: self.ptr,
                length: self.length,
            },
            UserSliceWriter {
                ptr: self.ptr,
                length: self.length,
            },
        )
    }
}

/// A reader for [`UserSlice`].
///
/// Used to incrementally read from the user slice.
pub struct UserSliceReader {
    ptr: UserPtr,
    length: usize,
}

impl UserSliceReader {
    /// Skip the provided number of bytes.
    ///
    /// Returns an error if skipping more than the length of the buffer.
    pub fn skip(&mut self, num_skip: usize) -> Result {
        // Update `self.length` first since that's the fallible part of this operation.
        self.length = self.length.checked_sub(num_skip).ok_or(EFAULT)?;
        self.ptr = self.ptr.wrapping_add(num_skip);
        Ok(())
    }

    /// Create a reader that can access the same range of data.
    ///
    /// Reading from the clone does not advance the current reader.
    ///
    /// The caller should take care to not introduce TOCTOU issues, as described in the
    /// documentation for [`UserSlice`].
    pub fn clone_reader(&self) -> UserSliceReader {
        UserSliceReader {
            ptr: self.ptr,
            length: self.length,
        }
    }

    /// Returns the number of bytes left to be read from this reader.
    ///
    /// Note that even reading less than this number of bytes may fail.
    pub fn len(&self) -> usize {
        self.length
    }

    /// Returns `true` if no data is available in the io buffer.
    pub fn is_empty(&self) -> bool {
        self.length == 0
    }

    /// Reads raw data from the user slice into a kernel buffer.
    ///
    /// For a version that uses `&mut [u8]`, please see [`UserSliceReader::read_slice`].
    ///
    /// Fails with [`EFAULT`] if the read happens on a bad address, or if the read goes out of
    /// bounds of this [`UserSliceReader`]. This call may modify `out` even if it returns an error.
    ///
    /// # Guarantees
    ///
    /// After a successful call to this method, all bytes in `out` are initialized.
    pub fn read_raw(&mut self, out: &mut [MaybeUninit<u8>]) -> Result {
        let len = out.len();
        let out_ptr = out.as_mut_ptr().cast::<c_void>();
        if len > self.length {
            return Err(EFAULT);
        }
        // SAFETY: `out_ptr` points into a mutable slice of length `len`, so we may write
        // that many bytes to it.
        let res = unsafe { bindings::copy_from_user(out_ptr, self.ptr as *const c_void, len) };
        if res != 0 {
            return Err(EFAULT);
        }
        self.ptr = self.ptr.wrapping_add(len);
        self.length -= len;
        Ok(())
    }

    /// Reads raw data from the user slice into a kernel buffer.
    ///
    /// Fails with [`EFAULT`] if the read happens on a bad address, or if the read goes out of
    /// bounds of this [`UserSliceReader`]. This call may modify `out` even if it returns an error.
    pub fn read_slice(&mut self, out: &mut [u8]) -> Result {
        // SAFETY: The types are compatible and `read_raw` doesn't write uninitialized bytes to
        // `out`.
        let out = unsafe { &mut *(out as *mut [u8] as *mut [MaybeUninit<u8>]) };
        self.read_raw(out)
    }

    /// Reads a value of the specified type.
    ///
    /// Fails with [`EFAULT`] if the read happens on a bad address, or if the read goes out of
    /// bounds of this [`UserSliceReader`].
    pub fn read<T: FromBytes>(&mut self) -> Result<T> {
        let len = size_of::<T>();
        if len > self.length {
            return Err(EFAULT);
        }
        let mut out: MaybeUninit<T> = MaybeUninit::uninit();
        // SAFETY: The local variable `out` is valid for writing `size_of::<T>()` bytes.
        //
        // By using the _copy_from_user variant, we skip the check_object_size check that verifies
        // the kernel pointer. This mirrors the logic on the C side that skips the check when the
        // length is a compile-time constant.
        let res = unsafe {
            bindings::_copy_from_user(
                out.as_mut_ptr().cast::<c_void>(),
                self.ptr as *const c_void,
                len,
            )
        };
        if res != 0 {
            return Err(EFAULT);
        }
        self.ptr = self.ptr.wrapping_add(len);
        self.length -= len;
        // SAFETY: The read above has initialized all bytes in `out`, and since `T` implements
        // `FromBytes`, any bit-pattern is a valid value for this type.
        Ok(unsafe { out.assume_init() })
    }

    /// Reads the entirety of the user slice, appending it to the end of the provided buffer.
    ///
    /// Fails with [`EFAULT`] if the read happens on a bad address.
    pub fn read_all<A: Allocator>(mut self, buf: &mut Vec<u8, A>, flags: Flags) -> Result {
        let len = self.length;
        buf.reserve(len, flags)?;

        // The call to `try_reserve` was successful, so the spare capacity is at least `len` bytes
        // long.
        self.read_raw(&mut buf.spare_capacity_mut()[..len])?;

        // SAFETY: Since the call to `read_raw` was successful, so the next `len` bytes of the
        // vector have been initialized.
        unsafe { buf.set_len(buf.len() + len) };
        Ok(())
    }
}

/// A writer for [`UserSlice`].
///
/// Used to incrementally write into the user slice.
pub struct UserSliceWriter {
    ptr: UserPtr,
    length: usize,
}

impl UserSliceWriter {
    /// Returns the amount of space remaining in this buffer.
    ///
    /// Note that even writing less than this number of bytes may fail.
    pub fn len(&self) -> usize {
        self.length
    }

    /// Returns `true` if no more data can be written to this buffer.
    pub fn is_empty(&self) -> bool {
        self.length == 0
    }

    /// Writes raw data to this user pointer from a kernel buffer.
    ///
    /// Fails with [`EFAULT`] if the write happens on a bad address, or if the write goes out of
    /// bounds of this [`UserSliceWriter`]. This call may modify the associated userspace slice even
    /// if it returns an error.
    pub fn write_slice(&mut self, data: &[u8]) -> Result {
        let len = data.len();
        let data_ptr = data.as_ptr().cast::<c_void>();
        if len > self.length {
            return Err(EFAULT);
        }
        // SAFETY: `data_ptr` points into an immutable slice of length `len`, so we may read
        // that many bytes from it.
        let res = unsafe { bindings::copy_to_user(self.ptr as *mut c_void, data_ptr, len) };
        if res != 0 {
            return Err(EFAULT);
        }
        self.ptr = self.ptr.wrapping_add(len);
        self.length -= len;
        Ok(())
    }

    /// Writes the provided Rust value to this userspace pointer.
    ///
    /// Fails with [`EFAULT`] if the write happens on a bad address, or if the write goes out of
    /// bounds of this [`UserSliceWriter`]. This call may modify the associated userspace slice even
    /// if it returns an error.
    pub fn write<T: AsBytes>(&mut self, value: &T) -> Result {
        let len = size_of::<T>();
        if len > self.length {
            return Err(EFAULT);
        }
        // SAFETY: The reference points to a value of type `T`, so it is valid for reading
        // `size_of::<T>()` bytes.
        //
        // By using the _copy_to_user variant, we skip the check_object_size check that verifies the
        // kernel pointer. This mirrors the logic on the C side that skips the check when the length
        // is a compile-time constant.
        let res = unsafe {
            bindings::_copy_to_user(
                self.ptr as *mut c_void,
                (value as *const T).cast::<c_void>(),
                len,
            )
        };
        if res != 0 {
            return Err(EFAULT);
        }
        self.ptr = self.ptr.wrapping_add(len);
        self.length -= len;
        Ok(())
    }
}
