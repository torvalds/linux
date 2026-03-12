// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Google LLC.

//! Traits for rendering or updating values exported to DebugFS.

use crate::{
    alloc::Allocator,
    fmt,
    fs::file,
    prelude::*,
    sync::{
        atomic::{
            Atomic,
            AtomicBasicOps,
            AtomicType,
            Relaxed, //
        },
        Arc,
        Mutex, //
    },
    transmute::{
        AsBytes,
        FromBytes, //
    },
    uaccess::{
        UserSliceReader,
        UserSliceWriter, //
    },
};

use core::{
    ops::{
        Deref,
        DerefMut, //
    },
    str::FromStr,
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
    fn write(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result;
}

impl<T: Writer> Writer for Mutex<T> {
    fn write(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.lock().write(f)
    }
}

impl<T: fmt::Debug> Writer for T {
    fn write(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "{self:?}")
    }
}

/// Trait for types that can be written out as binary.
pub trait BinaryWriter {
    /// Writes the binary form of `self` into `writer`.
    ///
    /// `offset` is the requested offset into the binary representation of `self`.
    ///
    /// On success, returns the number of bytes written in to `writer`.
    fn write_to_slice(
        &self,
        writer: &mut UserSliceWriter,
        offset: &mut file::Offset,
    ) -> Result<usize>;
}

// Base implementation for any `T: AsBytes`.
impl<T: AsBytes> BinaryWriter for T {
    fn write_to_slice(
        &self,
        writer: &mut UserSliceWriter,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        writer.write_slice_file(self.as_bytes(), offset)
    }
}

// Delegate for `Mutex<T>`: Support a `T` with an outer mutex.
impl<T: BinaryWriter> BinaryWriter for Mutex<T> {
    fn write_to_slice(
        &self,
        writer: &mut UserSliceWriter,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        let guard = self.lock();

        guard.write_to_slice(writer, offset)
    }
}

// Delegate for `Box<T, A>`: Support a `Box<T, A>` with no lock or an inner lock.
impl<T, A> BinaryWriter for Box<T, A>
where
    T: BinaryWriter,
    A: Allocator,
{
    fn write_to_slice(
        &self,
        writer: &mut UserSliceWriter,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        self.deref().write_to_slice(writer, offset)
    }
}

// Delegate for `Pin<Box<T, A>>`: Support a `Pin<Box<T, A>>` with no lock or an inner lock.
impl<T, A> BinaryWriter for Pin<Box<T, A>>
where
    T: BinaryWriter,
    A: Allocator,
{
    fn write_to_slice(
        &self,
        writer: &mut UserSliceWriter,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        self.deref().write_to_slice(writer, offset)
    }
}

// Delegate for `Arc<T>`: Support a `Arc<T>` with no lock or an inner lock.
impl<T> BinaryWriter for Arc<T>
where
    T: BinaryWriter,
{
    fn write_to_slice(
        &self,
        writer: &mut UserSliceWriter,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        self.deref().write_to_slice(writer, offset)
    }
}

// Delegate for `Vec<T, A>`.
impl<T, A> BinaryWriter for Vec<T, A>
where
    T: AsBytes,
    A: Allocator,
{
    fn write_to_slice(
        &self,
        writer: &mut UserSliceWriter,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        let slice = self.as_slice();

        // SAFETY: `T: AsBytes` allows us to treat `&[T]` as `&[u8]`.
        let buffer = unsafe {
            core::slice::from_raw_parts(slice.as_ptr().cast(), core::mem::size_of_val(slice))
        };

        writer.write_slice_file(buffer, offset)
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

impl<T: FromStr + Unpin> Reader for Mutex<T> {
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

impl<T: AtomicType + FromStr> Reader for Atomic<T>
where
    T::Repr: AtomicBasicOps,
{
    fn read_from_slice(&self, reader: &mut UserSliceReader) -> Result {
        let mut buf = [0u8; 21]; // Enough for a 64-bit number.
        if reader.len() > buf.len() {
            return Err(EINVAL);
        }
        let n = reader.len();
        reader.read_slice(&mut buf[..n])?;

        let s = core::str::from_utf8(&buf[..n]).map_err(|_| EINVAL)?;
        let val = s.trim().parse::<T>().map_err(|_| EINVAL)?;
        self.store(val, Relaxed);
        Ok(())
    }
}

/// Trait for types that can be constructed from a binary representation.
///
/// See also [`BinaryReader`] for interior mutability.
pub trait BinaryReaderMut {
    /// Reads the binary form of `self` from `reader`.
    ///
    /// Same as [`BinaryReader::read_from_slice`], but takes a mutable reference.
    ///
    /// `offset` is the requested offset into the binary representation of `self`.
    ///
    /// On success, returns the number of bytes read from `reader`.
    fn read_from_slice_mut(
        &mut self,
        reader: &mut UserSliceReader,
        offset: &mut file::Offset,
    ) -> Result<usize>;
}

// Base implementation for any `T: AsBytes + FromBytes`.
impl<T: AsBytes + FromBytes> BinaryReaderMut for T {
    fn read_from_slice_mut(
        &mut self,
        reader: &mut UserSliceReader,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        reader.read_slice_file(self.as_bytes_mut(), offset)
    }
}

// Delegate for `Box<T, A>`: Support a `Box<T, A>` with an outer lock.
impl<T: ?Sized + BinaryReaderMut, A: Allocator> BinaryReaderMut for Box<T, A> {
    fn read_from_slice_mut(
        &mut self,
        reader: &mut UserSliceReader,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        self.deref_mut().read_from_slice_mut(reader, offset)
    }
}

// Delegate for `Vec<T, A>`: Support a `Vec<T, A>` with an outer lock.
impl<T, A> BinaryReaderMut for Vec<T, A>
where
    T: AsBytes + FromBytes,
    A: Allocator,
{
    fn read_from_slice_mut(
        &mut self,
        reader: &mut UserSliceReader,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        let slice = self.as_mut_slice();

        // SAFETY: `T: AsBytes + FromBytes` allows us to treat `&mut [T]` as `&mut [u8]`.
        let buffer = unsafe {
            core::slice::from_raw_parts_mut(
                slice.as_mut_ptr().cast(),
                core::mem::size_of_val(slice),
            )
        };

        reader.read_slice_file(buffer, offset)
    }
}

/// Trait for types that can be constructed from a binary representation.
///
/// See also [`BinaryReaderMut`] for the mutable version.
pub trait BinaryReader {
    /// Reads the binary form of `self` from `reader`.
    ///
    /// `offset` is the requested offset into the binary representation of `self`.
    ///
    /// On success, returns the number of bytes read from `reader`.
    fn read_from_slice(
        &self,
        reader: &mut UserSliceReader,
        offset: &mut file::Offset,
    ) -> Result<usize>;
}

// Delegate for `Mutex<T>`: Support a `T` with an outer `Mutex`.
impl<T: BinaryReaderMut + Unpin> BinaryReader for Mutex<T> {
    fn read_from_slice(
        &self,
        reader: &mut UserSliceReader,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        let mut this = self.lock();

        this.read_from_slice_mut(reader, offset)
    }
}

// Delegate for `Box<T, A>`: Support a `Box<T, A>` with an inner lock.
impl<T: ?Sized + BinaryReader, A: Allocator> BinaryReader for Box<T, A> {
    fn read_from_slice(
        &self,
        reader: &mut UserSliceReader,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        self.deref().read_from_slice(reader, offset)
    }
}

// Delegate for `Pin<Box<T, A>>`: Support a `Pin<Box<T, A>>` with an inner lock.
impl<T: ?Sized + BinaryReader, A: Allocator> BinaryReader for Pin<Box<T, A>> {
    fn read_from_slice(
        &self,
        reader: &mut UserSliceReader,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        self.deref().read_from_slice(reader, offset)
    }
}

// Delegate for `Arc<T>`: Support an `Arc<T>` with an inner lock.
impl<T: ?Sized + BinaryReader> BinaryReader for Arc<T> {
    fn read_from_slice(
        &self,
        reader: &mut UserSliceReader,
        offset: &mut file::Offset,
    ) -> Result<usize> {
        self.deref().read_from_slice(reader, offset)
    }
}
