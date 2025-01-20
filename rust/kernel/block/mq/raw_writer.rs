// SPDX-License-Identifier: GPL-2.0

use core::fmt::{self, Write};

use crate::error::Result;
use crate::prelude::EINVAL;

/// A mutable reference to a byte buffer where a string can be written into.
///
/// # Invariants
///
/// `buffer` is always null terminated.
pub(crate) struct RawWriter<'a> {
    buffer: &'a mut [u8],
    pos: usize,
}

impl<'a> RawWriter<'a> {
    /// Create a new `RawWriter` instance.
    fn new(buffer: &'a mut [u8]) -> Result<RawWriter<'a>> {
        *(buffer.last_mut().ok_or(EINVAL)?) = 0;

        // INVARIANT: We null terminated the buffer above.
        Ok(Self { buffer, pos: 0 })
    }

    pub(crate) fn from_array<const N: usize>(
        a: &'a mut [crate::ffi::c_char; N],
    ) -> Result<RawWriter<'a>> {
        Self::new(
            // SAFETY: the buffer of `a` is valid for read and write as `u8` for
            // at least `N` bytes.
            unsafe { core::slice::from_raw_parts_mut(a.as_mut_ptr().cast::<u8>(), N) },
        )
    }
}

impl Write for RawWriter<'_> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        let bytes = s.as_bytes();
        let len = bytes.len();

        // We do not want to overwrite our null terminator
        if self.pos + len > self.buffer.len() - 1 {
            return Err(fmt::Error);
        }

        // INVARIANT: We are not overwriting the last byte
        self.buffer[self.pos..self.pos + len].copy_from_slice(bytes);

        self.pos += len;

        Ok(())
    }
}
