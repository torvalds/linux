// SPDX-License-Identifier: GPL-2.0

//! Kernel types.

use core::{cell::UnsafeCell, mem::MaybeUninit};

/// Stores an opaque value.
///
/// This is meant to be used with FFI objects that are never interpreted by Rust code.
#[repr(transparent)]
pub struct Opaque<T>(MaybeUninit<UnsafeCell<T>>);

impl<T> Opaque<T> {
    /// Creates a new opaque value.
    pub const fn new(value: T) -> Self {
        Self(MaybeUninit::new(UnsafeCell::new(value)))
    }

    /// Creates an uninitialised value.
    pub const fn uninit() -> Self {
        Self(MaybeUninit::uninit())
    }

    /// Returns a raw pointer to the opaque data.
    pub fn get(&self) -> *mut T {
        UnsafeCell::raw_get(self.0.as_ptr())
    }
}

/// A sum type that always holds either a value of type `L` or `R`.
pub enum Either<L, R> {
    /// Constructs an instance of [`Either`] containing a value of type `L`.
    Left(L),

    /// Constructs an instance of [`Either`] containing a value of type `R`.
    Right(R),
}
