// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

//! IO vectors.
//!
//! C headers: [`include/linux/iov_iter.h`](srctree/include/linux/iov_iter.h),
//! [`include/linux/uio.h`](srctree/include/linux/uio.h)

use crate::{
    alloc::{Allocator, Flags},
    bindings,
    prelude::*,
    types::Opaque,
};
use core::{marker::PhantomData, mem::MaybeUninit, ptr, slice};

const ITER_SOURCE: bool = bindings::ITER_SOURCE != 0;
const ITER_DEST: bool = bindings::ITER_DEST != 0;

// Compile-time assertion for the above constants.
const _: () = {
    build_assert!(
        ITER_SOURCE != ITER_DEST,
        "ITER_DEST and ITER_SOURCE should be different."
    );
};

/// An IO vector that acts as a source of data.
///
/// The data may come from many different sources. This includes both things in kernel-space and
/// reading from userspace. It's not necessarily the case that the data source is immutable, so
/// rewinding the IO vector to read the same data twice is not guaranteed to result in the same
/// bytes. It's also possible that the data source is mapped in a thread-local manner using e.g.
/// `kmap_local_page()`, so this type is not `Send` to ensure that the mapping is read from the
/// right context in that scenario.
///
/// # Invariants
///
/// Must hold a valid `struct iov_iter` with `data_source` set to `ITER_SOURCE`. For the duration
/// of `'data`, it must be safe to read from this IO vector using the standard C methods for this
/// purpose.
#[repr(transparent)]
pub struct IovIterSource<'data> {
    iov: Opaque<bindings::iov_iter>,
    /// Represent to the type system that this value contains a pointer to readable data it does
    /// not own.
    _source: PhantomData<&'data [u8]>,
}

impl<'data> IovIterSource<'data> {
    /// Obtain an `IovIterSource` from a raw pointer.
    ///
    /// # Safety
    ///
    /// * The referenced `struct iov_iter` must be valid and must only be accessed through the
    ///   returned reference for the duration of `'iov`.
    /// * The referenced `struct iov_iter` must have `data_source` set to `ITER_SOURCE`.
    /// * For the duration of `'data`, it must be safe to read from this IO vector using the
    ///   standard C methods for this purpose.
    #[track_caller]
    #[inline]
    pub unsafe fn from_raw<'iov>(ptr: *mut bindings::iov_iter) -> &'iov mut IovIterSource<'data> {
        // SAFETY: The caller ensures that `ptr` is valid.
        let data_source = unsafe { (*ptr).data_source };
        assert_eq!(data_source, ITER_SOURCE);

        // SAFETY: The caller ensures the type invariants for the right durations, and
        // `IovIterSource` is layout compatible with `struct iov_iter`.
        unsafe { &mut *ptr.cast::<IovIterSource<'data>>() }
    }

    /// Access this as a raw `struct iov_iter`.
    #[inline]
    pub fn as_raw(&mut self) -> *mut bindings::iov_iter {
        self.iov.get()
    }

    /// Returns the number of bytes available in this IO vector.
    ///
    /// Note that this may overestimate the number of bytes. For example, reading from userspace
    /// memory could fail with `EFAULT`, which will be treated as the end of the IO vector.
    #[inline]
    pub fn len(&self) -> usize {
        // SAFETY: We have shared access to this IO vector, so we can read its `count` field.
        unsafe {
            (*self.iov.get())
                .__bindgen_anon_1
                .__bindgen_anon_1
                .as_ref()
                .count
        }
    }

    /// Returns whether there are any bytes left in this IO vector.
    ///
    /// This may return `true` even if there are no more bytes available. For example, reading from
    /// userspace memory could fail with `EFAULT`, which will be treated as the end of the IO vector.
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Advance this IO vector by `bytes` bytes.
    ///
    /// If `bytes` is larger than the size of this IO vector, it is advanced to the end.
    #[inline]
    pub fn advance(&mut self, bytes: usize) {
        // SAFETY: By the type invariants, `self.iov` is a valid IO vector.
        unsafe { bindings::iov_iter_advance(self.as_raw(), bytes) };
    }

    /// Advance this IO vector backwards by `bytes` bytes.
    ///
    /// # Safety
    ///
    /// The IO vector must not be reverted to before its beginning.
    #[inline]
    pub unsafe fn revert(&mut self, bytes: usize) {
        // SAFETY: By the type invariants, `self.iov` is a valid IO vector, and the caller
        // ensures that `bytes` is in bounds.
        unsafe { bindings::iov_iter_revert(self.as_raw(), bytes) };
    }

    /// Read data from this IO vector.
    ///
    /// Returns the number of bytes that have been copied.
    #[inline]
    pub fn copy_from_iter(&mut self, out: &mut [u8]) -> usize {
        // SAFETY: `Self::copy_from_iter_raw` guarantees that it will not write any uninitialized
        // bytes in the provided buffer, so `out` is still a valid `u8` slice after this call.
        let out = unsafe { &mut *(ptr::from_mut(out) as *mut [MaybeUninit<u8>]) };

        self.copy_from_iter_raw(out).len()
    }

    /// Read data from this IO vector and append it to a vector.
    ///
    /// Returns the number of bytes that have been copied.
    #[inline]
    pub fn copy_from_iter_vec<A: Allocator>(
        &mut self,
        out: &mut Vec<u8, A>,
        flags: Flags,
    ) -> Result<usize> {
        out.reserve(self.len(), flags)?;
        let len = self.copy_from_iter_raw(out.spare_capacity_mut()).len();
        // SAFETY:
        // - `len` is the length of a subslice of the spare capacity, so `len` is at most the
        //   length of the spare capacity.
        // - `Self::copy_from_iter_raw` guarantees that the first `len` bytes of the spare capacity
        //   have been initialized.
        unsafe { out.inc_len(len) };
        Ok(len)
    }

    /// Read data from this IO vector into potentially uninitialized memory.
    ///
    /// Returns the sub-slice of the output that has been initialized. If the returned slice is
    /// shorter than the input buffer, then the entire IO vector has been read.
    ///
    /// This will never write uninitialized bytes to the provided buffer.
    #[inline]
    pub fn copy_from_iter_raw(&mut self, out: &mut [MaybeUninit<u8>]) -> &mut [u8] {
        let capacity = out.len();
        let out = out.as_mut_ptr().cast::<u8>();

        // GUARANTEES: The C API guarantees that it does not write uninitialized bytes to the
        // provided buffer.
        // SAFETY:
        // * By the type invariants, it is still valid to read from this IO vector.
        // * `out` is valid for writing for `capacity` bytes because it comes from a slice of
        //   that length.
        let len = unsafe { bindings::_copy_from_iter(out.cast(), capacity, self.as_raw()) };

        // SAFETY: The underlying C api guarantees that initialized bytes have been written to the
        // first `len` bytes of the spare capacity.
        unsafe { slice::from_raw_parts_mut(out, len) }
    }
}

/// An IO vector that acts as a destination for data.
///
/// IO vectors support many different types of destinations. This includes both buffers in
/// kernel-space and writing to userspace. It's possible that the destination buffer is mapped in a
/// thread-local manner using e.g. `kmap_local_page()`, so this type is not `Send` to ensure that
/// the mapping is written to the right context in that scenario.
///
/// # Invariants
///
/// Must hold a valid `struct iov_iter` with `data_source` set to `ITER_DEST`. For the duration of
/// `'data`, it must be safe to write to this IO vector using the standard C methods for this
/// purpose.
#[repr(transparent)]
pub struct IovIterDest<'data> {
    iov: Opaque<bindings::iov_iter>,
    /// Represent to the type system that this value contains a pointer to writable data it does
    /// not own.
    _source: PhantomData<&'data mut [u8]>,
}

impl<'data> IovIterDest<'data> {
    /// Obtain an `IovIterDest` from a raw pointer.
    ///
    /// # Safety
    ///
    /// * The referenced `struct iov_iter` must be valid and must only be accessed through the
    ///   returned reference for the duration of `'iov`.
    /// * The referenced `struct iov_iter` must have `data_source` set to `ITER_DEST`.
    /// * For the duration of `'data`, it must be safe to write to this IO vector using the
    ///   standard C methods for this purpose.
    #[track_caller]
    #[inline]
    pub unsafe fn from_raw<'iov>(ptr: *mut bindings::iov_iter) -> &'iov mut IovIterDest<'data> {
        // SAFETY: The caller ensures that `ptr` is valid.
        let data_source = unsafe { (*ptr).data_source };
        assert_eq!(data_source, ITER_DEST);

        // SAFETY: The caller ensures the type invariants for the right durations, and
        // `IovIterSource` is layout compatible with `struct iov_iter`.
        unsafe { &mut *ptr.cast::<IovIterDest<'data>>() }
    }

    /// Access this as a raw `struct iov_iter`.
    #[inline]
    pub fn as_raw(&mut self) -> *mut bindings::iov_iter {
        self.iov.get()
    }

    /// Returns the number of bytes available in this IO vector.
    ///
    /// Note that this may overestimate the number of bytes. For example, reading from userspace
    /// memory could fail with EFAULT, which will be treated as the end of the IO vector.
    #[inline]
    pub fn len(&self) -> usize {
        // SAFETY: We have shared access to this IO vector, so we can read its `count` field.
        unsafe {
            (*self.iov.get())
                .__bindgen_anon_1
                .__bindgen_anon_1
                .as_ref()
                .count
        }
    }

    /// Returns whether there are any bytes left in this IO vector.
    ///
    /// This may return `true` even if there are no more bytes available. For example, reading from
    /// userspace memory could fail with EFAULT, which will be treated as the end of the IO vector.
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Advance this IO vector by `bytes` bytes.
    ///
    /// If `bytes` is larger than the size of this IO vector, it is advanced to the end.
    #[inline]
    pub fn advance(&mut self, bytes: usize) {
        // SAFETY: By the type invariants, `self.iov` is a valid IO vector.
        unsafe { bindings::iov_iter_advance(self.as_raw(), bytes) };
    }

    /// Advance this IO vector backwards by `bytes` bytes.
    ///
    /// # Safety
    ///
    /// The IO vector must not be reverted to before its beginning.
    #[inline]
    pub unsafe fn revert(&mut self, bytes: usize) {
        // SAFETY: By the type invariants, `self.iov` is a valid IO vector, and the caller
        // ensures that `bytes` is in bounds.
        unsafe { bindings::iov_iter_revert(self.as_raw(), bytes) };
    }

    /// Write data to this IO vector.
    ///
    /// Returns the number of bytes that were written. If this is shorter than the provided slice,
    /// then no more bytes can be written.
    #[inline]
    pub fn copy_to_iter(&mut self, input: &[u8]) -> usize {
        // SAFETY:
        // * By the type invariants, it is still valid to write to this IO vector.
        // * `input` is valid for `input.len()` bytes.
        unsafe { bindings::_copy_to_iter(input.as_ptr().cast(), input.len(), self.as_raw()) }
    }

    /// Utility for implementing `read_iter` given the full contents of the file.
    ///
    /// The full contents of the file being read from is represented by `contents`. This call will
    /// write the appropriate sub-slice of `contents` and update the file position in `ppos` so
    /// that the file will appear to contain `contents` even if takes multiple reads to read the
    /// entire file.
    #[inline]
    pub fn simple_read_from_buffer(&mut self, ppos: &mut i64, contents: &[u8]) -> Result<usize> {
        if *ppos < 0 {
            return Err(EINVAL);
        }
        let Ok(pos) = usize::try_from(*ppos) else {
            return Ok(0);
        };
        if pos >= contents.len() {
            return Ok(0);
        }

        // BOUNDS: We just checked that `pos < contents.len()` above.
        let num_written = self.copy_to_iter(&contents[pos..]);

        // OVERFLOW: `pos+num_written <= contents.len() <= isize::MAX <= i64::MAX`.
        *ppos = (pos + num_written) as i64;

        Ok(num_written)
    }
}
