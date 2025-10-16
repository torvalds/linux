// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Google LLC.

//! Adapters which allow the user to supply a write or read implementation as a value rather
//! than a trait implementation. If provided, it will override the trait implementation.

use super::{Reader, Writer};
use crate::prelude::*;
use crate::uaccess::UserSliceReader;
use core::fmt;
use core::fmt::Formatter;
use core::marker::PhantomData;
use core::ops::Deref;

/// # Safety
///
/// To implement this trait, it must be safe to cast a `&Self` to a `&Inner`.
/// It is intended for use in unstacking adapters out of `FileOps` backings.
pub(crate) unsafe trait Adapter {
    type Inner;
}

/// Adapter to implement `Reader` via a callback with the same representation as `T`.
///
/// * Layer it on top of `WriterAdapter` if you want to add a custom callback for `write`.
/// * Layer it on top of `NoWriter` to pass through any support present on the underlying type.
///
/// # Invariants
///
/// If an instance for `WritableAdapter<_, W>` is constructed, `W` is inhabited.
#[repr(transparent)]
pub(crate) struct WritableAdapter<D, W> {
    inner: D,
    _writer: PhantomData<W>,
}

// SAFETY: Stripping off the adapter only removes constraints
unsafe impl<D, W> Adapter for WritableAdapter<D, W> {
    type Inner = D;
}

impl<D: Writer, W> Writer for WritableAdapter<D, W> {
    fn write(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.inner.write(fmt)
    }
}

impl<D: Deref, W> Reader for WritableAdapter<D, W>
where
    W: Fn(&D::Target, &mut UserSliceReader) -> Result + Send + Sync + 'static,
{
    fn read_from_slice(&self, reader: &mut UserSliceReader) -> Result {
        // SAFETY: WritableAdapter<_, W> can only be constructed if W is inhabited
        let w: &W = unsafe { materialize_zst() };
        w(self.inner.deref(), reader)
    }
}

/// Adapter to implement `Writer` via a callback with the same representation as `T`.
///
/// # Invariants
///
/// If an instance for `FormatAdapter<_, F>` is constructed, `F` is inhabited.
#[repr(transparent)]
pub(crate) struct FormatAdapter<D, F> {
    inner: D,
    _formatter: PhantomData<F>,
}

impl<D, F> Deref for FormatAdapter<D, F> {
    type Target = D;
    fn deref(&self) -> &D {
        &self.inner
    }
}

impl<D, F> Writer for FormatAdapter<D, F>
where
    F: Fn(&D, &mut Formatter<'_>) -> fmt::Result + 'static,
{
    fn write(&self, fmt: &mut Formatter<'_>) -> fmt::Result {
        // SAFETY: FormatAdapter<_, F> can only be constructed if F is inhabited
        let f: &F = unsafe { materialize_zst() };
        f(&self.inner, fmt)
    }
}

// SAFETY: Stripping off the adapter only removes constraints
unsafe impl<D, F> Adapter for FormatAdapter<D, F> {
    type Inner = D;
}

#[repr(transparent)]
pub(crate) struct NoWriter<D> {
    inner: D,
}

// SAFETY: Stripping off the adapter only removes constraints
unsafe impl<D> Adapter for NoWriter<D> {
    type Inner = D;
}

impl<D> Deref for NoWriter<D> {
    type Target = D;
    fn deref(&self) -> &D {
        &self.inner
    }
}

/// For types with a unique value, produce a static reference to it.
///
/// # Safety
///
/// The caller asserts that F is inhabited
unsafe fn materialize_zst<F>() -> &'static F {
    const { assert!(core::mem::size_of::<F>() == 0) };
    let zst_dangle: core::ptr::NonNull<F> = core::ptr::NonNull::dangling();
    // SAFETY: While the pointer is dangling, it is a dangling pointer to a ZST, based on the
    // assertion above. The type is also inhabited, by the caller's assertion. This means
    // we can materialize it.
    unsafe { zst_dangle.as_ref() }
}
