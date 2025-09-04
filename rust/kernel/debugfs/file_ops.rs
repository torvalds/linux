// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Google LLC.

use super::Writer;
use crate::prelude::*;
use crate::seq_file::SeqFile;
use crate::seq_print;
use core::fmt::{Display, Formatter, Result};
use core::marker::PhantomData;

#[cfg(CONFIG_DEBUG_FS)]
use core::ops::Deref;

/// # Invariant
///
/// `FileOps<T>` will always contain an `operations` which is safe to use for a file backed
/// off an inode which has a pointer to a `T` in its private data that is safe to convert
/// into a reference.
pub(super) struct FileOps<T> {
    #[cfg(CONFIG_DEBUG_FS)]
    operations: bindings::file_operations,
    #[cfg(CONFIG_DEBUG_FS)]
    mode: u16,
    _phantom: PhantomData<T>,
}

impl<T> FileOps<T> {
    /// # Safety
    ///
    /// The caller asserts that the provided `operations` is safe to use for a file whose
    /// inode has a pointer to `T` in its private data that is safe to convert into a reference.
    const unsafe fn new(operations: bindings::file_operations, mode: u16) -> Self {
        Self {
            #[cfg(CONFIG_DEBUG_FS)]
            operations,
            #[cfg(CONFIG_DEBUG_FS)]
            mode,
            _phantom: PhantomData,
        }
    }

    #[cfg(CONFIG_DEBUG_FS)]
    pub(crate) const fn mode(&self) -> u16 {
        self.mode
    }
}

#[cfg(CONFIG_DEBUG_FS)]
impl<T> Deref for FileOps<T> {
    type Target = bindings::file_operations;

    fn deref(&self) -> &Self::Target {
        &self.operations
    }
}

struct WriterAdapter<T>(T);

impl<'a, T: Writer> Display for WriterAdapter<&'a T> {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        self.0.write(f)
    }
}

/// Implements `open` for `file_operations` via `single_open` to fill out a `seq_file`.
///
/// # Safety
///
/// * `inode`'s private pointer must point to a value of type `T` which will outlive the `inode`
///   and will not have any unique references alias it during the call.
/// * `file` must point to a live, not-yet-initialized file object.
unsafe extern "C" fn writer_open<T: Writer + Sync>(
    inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> c_int {
    // SAFETY: The caller ensures that `inode` is a valid pointer.
    let data = unsafe { (*inode).i_private };
    // SAFETY:
    // * `file` is acceptable by caller precondition.
    // * `print_act` will be called on a `seq_file` with private data set to the third argument,
    //   so we meet its safety requirements.
    // * The `data` pointer passed in the third argument is a valid `T` pointer that outlives
    //   this call by caller preconditions.
    unsafe { bindings::single_open(file, Some(writer_act::<T>), data) }
}

/// Prints private data stashed in a seq_file to that seq file.
///
/// # Safety
///
/// `seq` must point to a live `seq_file` whose private data is a valid pointer to a `T` which may
/// not have any unique references alias it during the call.
unsafe extern "C" fn writer_act<T: Writer + Sync>(
    seq: *mut bindings::seq_file,
    _: *mut c_void,
) -> c_int {
    // SAFETY: By caller precondition, this pointer is valid pointer to a `T`, and
    // there are not and will not be any unique references until we are done.
    let data = unsafe { &*((*seq).private.cast::<T>()) };
    // SAFETY: By caller precondition, `seq_file` points to a live `seq_file`, so we can lift
    // it.
    let seq_file = unsafe { SeqFile::from_raw(seq) };
    seq_print!(seq_file, "{}", WriterAdapter(data));
    0
}

// Work around lack of generic const items.
pub(crate) trait ReadFile<T> {
    const FILE_OPS: FileOps<T>;
}

impl<T: Writer + Sync> ReadFile<T> for T {
    const FILE_OPS: FileOps<T> = {
        let operations = bindings::file_operations {
            read: Some(bindings::seq_read),
            llseek: Some(bindings::seq_lseek),
            release: Some(bindings::single_release),
            open: Some(writer_open::<Self>),
            // SAFETY: `file_operations` supports zeroes in all fields.
            ..unsafe { core::mem::zeroed() }
        };
        // SAFETY: `operations` is all stock `seq_file` implementations except for `writer_open`.
        // `open`'s only requirement beyond what is provided to all open functions is that the
        // inode's data pointer must point to a `T` that will outlive it, which matches the
        // `FileOps` requirements.
        unsafe { FileOps::new(operations, 0o400) }
    };
}
