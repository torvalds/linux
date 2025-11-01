// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Google LLC.

//! Traits for rendering or updating values exported to DebugFS.

use crate::prelude::*;
use crate::sync::Mutex;
use crate::uaccess::UserSliceReader;
use core::fmt::{self, Debug, Formatter};
use core::str::FromStr;
use core::sync::atomic::{
    AtomicI16, AtomicI32, AtomicI64, AtomicI8, AtomicIsize, AtomicU16, AtomicU32, AtomicU64,
    AtomicU8, AtomicUsize, Ordering,
};

/// A trait for types that can be written into a string.
///
/// This works very similarly to `Debug`, and is automatically implemented if `Debug` is
/// implemented for a type. It is also implemented for any writable type inside a `Mutex`.
///
/// The derived implementation of `Debug` [may
/// change](https://doc.rust-lang.org/std/fmt/trait.Debug.html#stability)
/// between Rust versions, so if stability is key for your use case, please implement `Writer`
/// explicitly instead.
pub trait Writer {
    /// Formats the value using the given formatter.
    fn write(&self, f: &mut Formatter<'_>) -> fmt::Result;
}

impl<T: Writer> Writer for Mutex<T> {
    fn write(&self, f: &mut Formatter<'_>) -> fmt::Result {
        self.lock().write(f)
    }
}

impl<T: Debug> Writer for T {
    fn write(&self, f: &mut Formatter<'_>) -> fmt::Result {
        writeln!(f, "{self:?}")
    }
}

/// A trait for types that can be updated from a user slice.
///
/// This works similarly to `FromStr`, but operates on a `UserSliceReader` rather than a &str.
///
/// It is automatically implemented for all atomic integers, or any type that implements `FromStr`
/// wrapped in a `Mutex`.
pub trait Reader {
    /// Updates the value from the given user slice.
    fn read_from_slice(&self, reader: &mut UserSliceReader) -> Result;
}

impl<T: FromStr> Reader for Mutex<T> {
    fn read_from_slice(&self, reader: &mut UserSliceReader) -> Result {
        let mut buf = [0u8; 128];
        if reader.len() > buf.len() {
            return Err(EINVAL);
        }
        let n = reader.len();
        reader.read_slice(&mut buf[..n])?;

        let s = core::str::from_utf8(&buf[..n]).map_err(|_| EINVAL)?;
        let val = s.trim().parse::<T>().map_err(|_| EINVAL)?;
        *self.lock() = val;
        Ok(())
    }
}

macro_rules! impl_reader_for_atomic {
    ($(($atomic_type:ty, $int_type:ty)),*) => {
        $(
            impl Reader for $atomic_type {
                fn read_from_slice(&self, reader: &mut UserSliceReader) -> Result {
                    let mut buf = [0u8; 21]; // Enough for a 64-bit number.
                    if reader.len() > buf.len() {
                        return Err(EINVAL);
                    }
                    let n = reader.len();
                    reader.read_slice(&mut buf[..n])?;

                    let s = core::str::from_utf8(&buf[..n]).map_err(|_| EINVAL)?;
                    let val = s.trim().parse::<$int_type>().map_err(|_| EINVAL)?;
                    self.store(val, Ordering::Relaxed);
                    Ok(())
                }
            }
        )*
    };
}

impl_reader_for_atomic!(
    (AtomicI16, i16),
    (AtomicI32, i32),
    (AtomicI64, i64),
    (AtomicI8, i8),
    (AtomicIsize, isize),
    (AtomicU16, u16),
    (AtomicU32, u32),
    (AtomicU64, u64),
    (AtomicU8, u8),
    (AtomicUsize, usize)
);
