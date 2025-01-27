// SPDX-License-Identifier: GPL-2.0

//! Memory layout.
//!
//! Custom layout types extending or improving [`Layout`].

use core::{alloc::Layout, marker::PhantomData};

/// Error when constructing an [`ArrayLayout`].
pub struct LayoutError;

/// A layout for an array `[T; n]`.
///
/// # Invariants
///
/// - `len * size_of::<T>() <= isize::MAX`.
pub struct ArrayLayout<T> {
    len: usize,
    _phantom: PhantomData<fn() -> T>,
}

impl<T> Clone for ArrayLayout<T> {
    fn clone(&self) -> Self {
        *self
    }
}
impl<T> Copy for ArrayLayout<T> {}

const ISIZE_MAX: usize = isize::MAX as usize;

impl<T> ArrayLayout<T> {
    /// Creates a new layout for `[T; 0]`.
    pub const fn empty() -> Self {
        // INVARIANT: `0 * size_of::<T>() <= isize::MAX`.
        Self {
            len: 0,
            _phantom: PhantomData,
        }
    }

    /// Creates a new layout for `[T; len]`.
    ///
    /// # Errors
    ///
    /// When `len * size_of::<T>()` overflows or when `len * size_of::<T>() > isize::MAX`.
    pub const fn new(len: usize) -> Result<Self, LayoutError> {
        match len.checked_mul(core::mem::size_of::<T>()) {
            Some(size) if size <= ISIZE_MAX => {
                // INVARIANT: We checked above that `len * size_of::<T>() <= isize::MAX`.
                Ok(Self {
                    len,
                    _phantom: PhantomData,
                })
            }
            _ => Err(LayoutError),
        }
    }

    /// Creates a new layout for `[T; len]`.
    ///
    /// # Safety
    ///
    /// `len` must be a value, for which `len * size_of::<T>() <= isize::MAX` is true.
    pub unsafe fn new_unchecked(len: usize) -> Self {
        // INVARIANT: By the safety requirements of this function
        // `len * size_of::<T>() <= isize::MAX`.
        Self {
            len,
            _phantom: PhantomData,
        }
    }

    /// Returns the number of array elements represented by this layout.
    pub const fn len(&self) -> usize {
        self.len
    }

    /// Returns `true` when no array elements are represented by this layout.
    pub const fn is_empty(&self) -> bool {
        self.len == 0
    }
}

impl<T> From<ArrayLayout<T>> for Layout {
    fn from(value: ArrayLayout<T>) -> Self {
        let res = Layout::array::<T>(value.len);
        // SAFETY: By the type invariant of `ArrayLayout` we have
        // `len * size_of::<T>() <= isize::MAX` and thus the result must be `Ok`.
        unsafe { res.unwrap_unchecked() }
    }
}
