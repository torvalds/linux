// SPDX-License-Identifier: GPL-2.0

//! Extensions to [`Vec`] for fallible allocations.

use super::{AllocError, Flags};
use alloc::vec::Vec;
use core::ptr;

/// Extensions to [`Vec`].
pub trait VecExt<T>: Sized {
    /// Creates a new [`Vec`] instance with at least the given capacity.
    ///
    /// # Examples
    ///
    /// ```
    /// let v = Vec::<u32>::with_capacity(20, GFP_KERNEL)?;
    ///
    /// assert!(v.capacity() >= 20);
    /// # Ok::<(), Error>(())
    /// ```
    fn with_capacity(capacity: usize, flags: Flags) -> Result<Self, AllocError>;

    /// Appends an element to the back of the [`Vec`] instance.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = Vec::new();
    /// v.push(1, GFP_KERNEL)?;
    /// assert_eq!(&v, &[1]);
    ///
    /// v.push(2, GFP_KERNEL)?;
    /// assert_eq!(&v, &[1, 2]);
    /// # Ok::<(), Error>(())
    /// ```
    fn push(&mut self, v: T, flags: Flags) -> Result<(), AllocError>;

    /// Pushes clones of the elements of slice into the [`Vec`] instance.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = Vec::new();
    /// v.push(1, GFP_KERNEL)?;
    ///
    /// v.extend_from_slice(&[20, 30, 40], GFP_KERNEL)?;
    /// assert_eq!(&v, &[1, 20, 30, 40]);
    ///
    /// v.extend_from_slice(&[50, 60], GFP_KERNEL)?;
    /// assert_eq!(&v, &[1, 20, 30, 40, 50, 60]);
    /// # Ok::<(), Error>(())
    /// ```
    fn extend_from_slice(&mut self, other: &[T], flags: Flags) -> Result<(), AllocError>
    where
        T: Clone;

    /// Ensures that the capacity exceeds the length by at least `additional` elements.
    ///
    /// # Examples
    ///
    /// ```
    /// let mut v = Vec::new();
    /// v.push(1, GFP_KERNEL)?;
    ///
    /// v.reserve(10, GFP_KERNEL)?;
    /// let cap = v.capacity();
    /// assert!(cap >= 10);
    ///
    /// v.reserve(10, GFP_KERNEL)?;
    /// let new_cap = v.capacity();
    /// assert_eq!(new_cap, cap);
    ///
    /// # Ok::<(), Error>(())
    /// ```
    fn reserve(&mut self, additional: usize, flags: Flags) -> Result<(), AllocError>;
}

impl<T> VecExt<T> for Vec<T> {
    fn with_capacity(capacity: usize, flags: Flags) -> Result<Self, AllocError> {
        let mut v = Vec::new();
        <Self as VecExt<_>>::reserve(&mut v, capacity, flags)?;
        Ok(v)
    }

    fn push(&mut self, v: T, flags: Flags) -> Result<(), AllocError> {
        <Self as VecExt<_>>::reserve(self, 1, flags)?;
        let s = self.spare_capacity_mut();
        s[0].write(v);

        // SAFETY: We just initialised the first spare entry, so it is safe to increase the length
        // by 1. We also know that the new length is <= capacity because of the previous call to
        // `reserve` above.
        unsafe { self.set_len(self.len() + 1) };
        Ok(())
    }

    fn extend_from_slice(&mut self, other: &[T], flags: Flags) -> Result<(), AllocError>
    where
        T: Clone,
    {
        <Self as VecExt<_>>::reserve(self, other.len(), flags)?;
        for (slot, item) in core::iter::zip(self.spare_capacity_mut(), other) {
            slot.write(item.clone());
        }

        // SAFETY: We just initialised the `other.len()` spare entries, so it is safe to increase
        // the length by the same amount. We also know that the new length is <= capacity because
        // of the previous call to `reserve` above.
        unsafe { self.set_len(self.len() + other.len()) };
        Ok(())
    }

    #[cfg(any(test, testlib))]
    fn reserve(&mut self, additional: usize, _flags: Flags) -> Result<(), AllocError> {
        Vec::reserve(self, additional);
        Ok(())
    }

    #[cfg(not(any(test, testlib)))]
    fn reserve(&mut self, additional: usize, flags: Flags) -> Result<(), AllocError> {
        let len = self.len();
        let cap = self.capacity();

        if cap - len >= additional {
            return Ok(());
        }

        if core::mem::size_of::<T>() == 0 {
            // The capacity is already `usize::MAX` for SZTs, we can't go higher.
            return Err(AllocError);
        }

        // We know cap is <= `isize::MAX` because `Layout::array` fails if the resulting byte size
        // is greater than `isize::MAX`. So the multiplication by two won't overflow.
        let new_cap = core::cmp::max(cap * 2, len.checked_add(additional).ok_or(AllocError)?);
        let layout = core::alloc::Layout::array::<T>(new_cap).map_err(|_| AllocError)?;

        let (old_ptr, len, cap) = destructure(self);

        // We need to make sure that `ptr` is either NULL or comes from a previous call to
        // `krealloc_aligned`. A `Vec<T>`'s `ptr` value is not guaranteed to be NULL and might be
        // dangling after being created with `Vec::new`. Instead, we can rely on `Vec<T>`'s capacity
        // to be zero if no memory has been allocated yet.
        let ptr = if cap == 0 { ptr::null_mut() } else { old_ptr };

        // SAFETY: `ptr` is valid because it's either NULL or comes from a previous call to
        // `krealloc_aligned`. We also verified that the type is not a ZST.
        let new_ptr = unsafe { super::allocator::krealloc_aligned(ptr.cast(), layout, flags) };
        if new_ptr.is_null() {
            // SAFETY: We are just rebuilding the existing `Vec` with no changes.
            unsafe { rebuild(self, old_ptr, len, cap) };
            Err(AllocError)
        } else {
            // SAFETY: `ptr` has been reallocated with the layout for `new_cap` elements. New cap
            // is greater than `cap`, so it continues to be >= `len`.
            unsafe { rebuild(self, new_ptr.cast::<T>(), len, new_cap) };
            Ok(())
        }
    }
}

#[cfg(not(any(test, testlib)))]
fn destructure<T>(v: &mut Vec<T>) -> (*mut T, usize, usize) {
    let mut tmp = Vec::new();
    core::mem::swap(&mut tmp, v);
    let mut tmp = core::mem::ManuallyDrop::new(tmp);
    let len = tmp.len();
    let cap = tmp.capacity();
    (tmp.as_mut_ptr(), len, cap)
}

/// Rebuilds a `Vec` from a pointer, length, and capacity.
///
/// # Safety
///
/// The same as [`Vec::from_raw_parts`].
#[cfg(not(any(test, testlib)))]
unsafe fn rebuild<T>(v: &mut Vec<T>, ptr: *mut T, len: usize, cap: usize) {
    // SAFETY: The safety requirements from this function satisfy those of `from_raw_parts`.
    let mut tmp = unsafe { Vec::from_raw_parts(ptr, len, cap) };
    core::mem::swap(&mut tmp, v);
}
