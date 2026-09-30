// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

//! Rust API for bitmap.
//!
//! C headers: [`include/linux/bitmap.h`](srctree/include/linux/bitmap.h).

use crate::alloc::{AllocError, Flags};
use crate::bindings;
#[cfg(not(CONFIG_RUST_BITMAP_HARDENED))]
use crate::pr_err;
use crate::ptr::Alignment;
use core::{
    num::NonZero,
    ptr::NonNull, //
};

/// Represents a C bitmap. Wraps underlying C bitmap API.
///
/// # Invariants
///
/// Must reference a `[c_ulong]` long enough to fit `data.len()` bits.
/// Must not be longer than [`i32::MAX`] bits, so offsets and lengths used with
/// [`Bitmap`] functions fit in the `int` and `unsigned int` arguments of the C bitmap API.
/// This also matches [`BitmapVec::MAX_LEN`].
#[cfg_attr(CONFIG_64BIT, repr(align(8)))]
#[cfg_attr(not(CONFIG_64BIT), repr(align(4)))]
pub struct Bitmap {
    data: [()],
}

macro_rules! bitmap_assert {
    ($cond:expr, $($arg:tt)+) => {
        #[cfg(CONFIG_RUST_BITMAP_HARDENED)]
        assert!($cond, $($arg)*);
    }
}

macro_rules! bitmap_assert_return {
    ($cond:expr, $($arg:tt)+) => {
        #[cfg(CONFIG_RUST_BITMAP_HARDENED)]
        assert!($cond, $($arg)*);

        #[cfg(not(CONFIG_RUST_BITMAP_HARDENED))]
        if !($cond) {
            pr_err!($($arg)*);
            return
        }
    }
}

impl Bitmap {
    /// Borrows a C bitmap.
    ///
    /// # Panics
    ///
    /// Panics if CONFIG_RUST_BITMAP_HARDENED is enabled and `nbits` exceeds [`i32::MAX`].
    ///
    /// # Safety
    ///
    /// * `ptr` holds a non-null address of an initialized array of `unsigned long`
    ///   that is large enough to hold `nbits` bits.
    /// * `nbits` must not exceed [`i32::MAX`].
    /// * the array must not be freed for the lifetime of this [`Bitmap`]
    /// * concurrent access only happens through atomic operations
    pub unsafe fn from_raw<'a>(ptr: *const usize, nbits: usize) -> &'a Bitmap {
        bitmap_assert!(
            nbits <= i32::MAX as usize,
            "`nbits` must be <= {}, was {}",
            i32::MAX,
            nbits
        );
        let data: *const [()] = core::ptr::slice_from_raw_parts(ptr.cast(), nbits);
        // INVARIANT: `data` references an initialized array that can hold `nbits` bits.
        // INVARIANT: The caller guarantees that `nbits` does not exceed `i32::MAX`.
        // SAFETY:
        // The caller guarantees that `data` (derived from `ptr` and `nbits`)
        // points to a valid, initialized, and appropriately sized memory region
        // that will not be freed for the lifetime 'a.
        // We are casting `*const [()]` to `*const Bitmap`. The `Bitmap`
        // struct is a ZST with a `data: [()]` field. This means its layout
        // is compatible with a slice of `()`, and effectively it's a "thin pointer"
        // (its size is 0 and alignment is 1). The `slice_from_raw_parts`
        // function correctly encodes the length (number of bits, not elements)
        // into the metadata of the fat pointer. Therefore, dereferencing this
        // pointer as `&Bitmap` is safe given the caller's guarantees.
        unsafe { &*(data as *const Bitmap) }
    }

    /// Borrows a C bitmap exclusively.
    ///
    /// # Panics
    ///
    /// Panics if CONFIG_RUST_BITMAP_HARDENED is enabled and `nbits` exceeds [`i32::MAX`].
    ///
    /// # Safety
    ///
    /// * `ptr` holds a non-null address of an initialized array of `unsigned long`
    ///   that is large enough to hold `nbits` bits.
    /// * `nbits` must not exceed [`i32::MAX`].
    /// * the array must not be freed for the lifetime of this [`Bitmap`]
    /// * no concurrent access may happen.
    pub unsafe fn from_raw_mut<'a>(ptr: *mut usize, nbits: usize) -> &'a mut Bitmap {
        bitmap_assert!(
            nbits <= i32::MAX as usize,
            "`nbits` must be <= {}, was {}",
            i32::MAX,
            nbits
        );
        let data: *mut [()] = core::ptr::slice_from_raw_parts_mut(ptr.cast(), nbits);
        // INVARIANT: `data` references an initialized array that can hold `nbits` bits.
        // INVARIANT: The caller guarantees that `nbits` does not exceed `i32::MAX`.
        // SAFETY:
        // The caller guarantees that `data` (derived from `ptr` and `nbits`)
        // points to a valid, initialized, and appropriately sized memory region
        // that will not be freed for the lifetime 'a.
        // Furthermore, the caller guarantees no concurrent access will happen,
        // which upholds the exclusivity requirement for a mutable reference.
        // Similar to `from_raw`, casting `*mut [()]` to `*mut Bitmap` is
        // safe because `Bitmap` is a ZST with a `data: [()]` field,
        // making its layout compatible with a slice of `()`.
        unsafe { &mut *(data as *mut Bitmap) }
    }

    /// Returns a raw pointer to the backing [`Bitmap`].
    pub fn as_ptr(&self) -> *const usize {
        core::ptr::from_ref::<Bitmap>(self).cast::<usize>()
    }

    /// Returns a mutable raw pointer to the backing [`Bitmap`].
    pub fn as_mut_ptr(&mut self) -> *mut usize {
        core::ptr::from_mut::<Bitmap>(self).cast::<usize>()
    }

    /// Returns length of this [`Bitmap`].
    #[expect(clippy::len_without_is_empty)]
    pub fn len(&self) -> usize {
        self.data.len()
    }
}

/// Holds either a pointer to array of `unsigned long` or a small bitmap.
#[repr(C)]
union BitmapRepr {
    bitmap: usize,
    ptr: NonNull<usize>,
}

/// Represents an owned bitmap.
///
/// Wraps underlying C bitmap API. See [`Bitmap`] for available
/// methods.
///
/// # Examples
///
/// Basic usage
///
/// ```
/// use kernel::alloc::flags::GFP_KERNEL;
/// use kernel::bitmap::BitmapVec;
///
/// let mut b = BitmapVec::new(16, GFP_KERNEL)?;
///
/// assert_eq!(16, b.len());
/// for i in 0..16 {
///     if i % 4 == 0 {
///       b.set_bit(i);
///     }
/// }
/// assert_eq!(Some(0), b.next_bit(0));
/// assert_eq!(Some(1), b.next_zero_bit(0));
/// assert_eq!(Some(4), b.next_bit(1));
/// assert_eq!(Some(5), b.next_zero_bit(4));
/// assert_eq!(Some(12), b.last_bit());
/// # Ok::<(), Error>(())
/// ```
///
/// # Invariants
///
/// * `nbits` is `<= MAX_LEN`.
/// * if `nbits <= MAX_INLINE_LEN`, then `repr` is a `usize`.
/// * otherwise, `repr` holds a non-null pointer to an initialized
///   array of `unsigned long` that is large enough to hold `nbits` bits.
pub struct BitmapVec {
    /// Representation of bitmap.
    repr: BitmapRepr,
    /// Length of this bitmap. Must be `<= MAX_LEN`.
    nbits: usize,
}

impl core::ops::Deref for BitmapVec {
    type Target = Bitmap;

    fn deref(&self) -> &Bitmap {
        let ptr = if self.nbits <= BitmapVec::MAX_INLINE_LEN {
            // SAFETY: Bitmap is represented inline.
            #[allow(unused_unsafe, reason = "Safe since Rust 1.92.0")]
            unsafe {
                core::ptr::addr_of!(self.repr.bitmap)
            }
        } else {
            // SAFETY: Bitmap is represented as array of `unsigned long`.
            unsafe { self.repr.ptr.as_ptr() }
        };

        // SAFETY: We got the right pointer and invariants of [`Bitmap`] hold.
        // An inline bitmap is treated like an array with single element.
        unsafe { Bitmap::from_raw(ptr, self.nbits) }
    }
}

impl core::ops::DerefMut for BitmapVec {
    fn deref_mut(&mut self) -> &mut Bitmap {
        let ptr = if self.nbits <= BitmapVec::MAX_INLINE_LEN {
            // SAFETY: Bitmap is represented inline.
            #[allow(unused_unsafe, reason = "Safe since Rust 1.92.0")]
            unsafe {
                core::ptr::addr_of_mut!(self.repr.bitmap)
            }
        } else {
            // SAFETY: Bitmap is represented as array of `unsigned long`.
            unsafe { self.repr.ptr.as_ptr() }
        };

        // SAFETY: We got the right pointer and invariants of [`BitmapVec`] hold.
        // An inline bitmap is treated like an array with single element.
        unsafe { Bitmap::from_raw_mut(ptr, self.nbits) }
    }
}

/// Enable ownership transfer to other threads.
///
/// SAFETY: We own the underlying bitmap representation.
unsafe impl Send for BitmapVec {}

/// Enable unsynchronized concurrent access to [`BitmapVec`] through shared references.
///
/// SAFETY: `deref()` will return a reference to a [`Bitmap`]. Its methods
/// take immutable references are either atomic or read-only.
unsafe impl Sync for BitmapVec {}

impl Drop for BitmapVec {
    fn drop(&mut self) {
        if self.nbits <= BitmapVec::MAX_INLINE_LEN {
            return;
        }
        // SAFETY: `self.ptr` was returned by the C `bitmap_zalloc`.
        //
        // INVARIANT: there is no other use of the `self.ptr` after this
        // call and the value is being dropped so the broken invariant is
        // not observable on function exit.
        unsafe { bindings::bitmap_free(self.repr.ptr.as_ptr()) };
    }
}

impl BitmapVec {
    /// The maximum possible length of a `BitmapVec`.
    pub const MAX_LEN: usize = i32::MAX as usize;

    /// The maximum length that uses the inline representation.
    pub const MAX_INLINE_LEN: usize = usize::BITS as usize;

    /// Construct a longest possible inline [`BitmapVec`].
    #[inline]
    pub fn new_inline() -> Self {
        // INVARIANT: `nbits <= MAX_INLINE_LEN`, so an inline bitmap is the right repr.
        BitmapVec {
            repr: BitmapRepr { bitmap: 0 },
            nbits: BitmapVec::MAX_INLINE_LEN,
        }
    }

    /// Constructs a new [`BitmapVec`].
    ///
    /// Fails with [`AllocError`] when the [`BitmapVec`] could not be allocated. This
    /// includes the case when `nbits` is greater than `MAX_LEN`.
    #[inline]
    pub fn new(nbits: usize, flags: Flags) -> Result<Self, AllocError> {
        if nbits <= BitmapVec::MAX_INLINE_LEN {
            return Ok(BitmapVec {
                repr: BitmapRepr { bitmap: 0 },
                nbits,
            });
        }
        if nbits > Self::MAX_LEN {
            return Err(AllocError);
        }
        let nbits_u32 = u32::try_from(nbits).unwrap();
        // SAFETY: `MAX_INLINE_LEN < nbits` and `nbits <= MAX_LEN`.
        let ptr = unsafe { bindings::bitmap_zalloc(nbits_u32, flags.as_raw()) };
        let ptr = NonNull::new(ptr).ok_or(AllocError)?;
        // INVARIANT: `ptr` returned by C `bitmap_zalloc` and `nbits` checked.
        Ok(BitmapVec {
            repr: BitmapRepr { ptr },
            nbits,
        })
    }

    /// Returns length of this [`Bitmap`].
    #[allow(clippy::len_without_is_empty)]
    #[inline]
    pub fn len(&self) -> usize {
        self.nbits
    }

    /// Fills this `Bitmap` with random bits.
    #[cfg(CONFIG_FIND_BIT_BENCHMARK_RUST)]
    pub fn fill_random(&mut self) {
        // SAFETY: `self.as_mut_ptr` points to either an array of the
        // appropriate length or one usize.
        unsafe {
            bindings::get_random_bytes(
                self.as_mut_ptr().cast::<ffi::c_void>(),
                usize::div_ceil(self.nbits, bindings::BITS_PER_LONG as usize)
                    * bindings::BITS_PER_LONG as usize
                    / 8,
            );
        }
    }
}

impl Bitmap {
    /// Set bit with index `index`.
    ///
    /// ATTENTION: `set_bit` is non-atomic, which differs from the naming
    /// convention in C code. The corresponding C function is `__set_bit`.
    ///
    /// If CONFIG_RUST_BITMAP_HARDENED is not enabled and `index` is greater than
    /// or equal to `self.nbits`, does nothing.
    ///
    /// # Panics
    ///
    /// Panics if CONFIG_RUST_BITMAP_HARDENED is enabled and `index` is greater than
    /// or equal to `self.nbits`.
    #[inline]
    pub fn set_bit(&mut self, index: usize) {
        bitmap_assert_return!(
            index < self.len(),
            "Bit `index` must be < {}, was {}",
            self.len(),
            index
        );
        // SAFETY: Bit `index` is within bounds.
        unsafe { bindings::__set_bit(index, self.as_mut_ptr()) };
    }

    /// Set bit with index `index`, atomically.
    ///
    /// This is a relaxed atomic operation (no implied memory barriers).
    ///
    /// ATTENTION: The naming convention differs from C, where the corresponding
    /// function is called `set_bit`.
    ///
    /// If CONFIG_RUST_BITMAP_HARDENED is not enabled and `index` is greater than
    /// or equal to `self.len()`, does nothing.
    ///
    /// # Panics
    ///
    /// Panics if CONFIG_RUST_BITMAP_HARDENED is enabled and `index` is greater than
    /// or equal to `self.len()`.
    #[inline]
    pub fn set_bit_atomic(&self, index: usize) {
        bitmap_assert_return!(
            index < self.len(),
            "Bit `index` must be < {}, was {}",
            self.len(),
            index
        );
        // SAFETY: `index` is within bounds and the caller has ensured that
        // there is no mix of non-atomic and atomic operations.
        unsafe { bindings::set_bit(index, self.as_ptr().cast_mut()) };
    }

    /// Clear `index` bit.
    ///
    /// ATTENTION: `clear_bit` is non-atomic, which differs from the naming
    /// convention in C code. The corresponding C function is `__clear_bit`.
    ///
    /// If CONFIG_RUST_BITMAP_HARDENED is not enabled and `index` is greater than
    /// or equal to `self.len()`, does nothing.
    ///
    /// # Panics
    ///
    /// Panics if CONFIG_RUST_BITMAP_HARDENED is enabled and `index` is greater than
    /// or equal to `self.len()`.
    #[inline]
    pub fn clear_bit(&mut self, index: usize) {
        bitmap_assert_return!(
            index < self.len(),
            "Bit `index` must be < {}, was {}",
            self.len(),
            index
        );
        // SAFETY: `index` is within bounds.
        unsafe { bindings::__clear_bit(index, self.as_mut_ptr()) };
    }

    /// Clear `index` bit, atomically.
    ///
    /// This is a relaxed atomic operation (no implied memory barriers).
    ///
    /// ATTENTION: The naming convention differs from C, where the corresponding
    /// function is called `clear_bit`.
    ///
    /// If CONFIG_RUST_BITMAP_HARDENED is not enabled and `index` is greater than
    /// or equal to `self.len()`, does nothing.
    ///
    /// # Panics
    ///
    /// Panics if CONFIG_RUST_BITMAP_HARDENED is enabled and `index` is greater than
    /// or equal to `self.len()`.
    #[inline]
    pub fn clear_bit_atomic(&self, index: usize) {
        bitmap_assert_return!(
            index < self.len(),
            "Bit `index` must be < {}, was {}",
            self.len(),
            index
        );
        // SAFETY: `index` is within bounds and the caller has ensured that
        // there is no mix of non-atomic and atomic operations.
        unsafe { bindings::clear_bit(index, self.as_ptr().cast_mut()) };
    }

    /// Copy `src` into this [`Bitmap`] and set any remaining bits to zero.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::alloc::{AllocError, flags::GFP_KERNEL};
    /// use kernel::bitmap::BitmapVec;
    ///
    /// let mut long_bitmap = BitmapVec::new(256, GFP_KERNEL)?;
    ///
    /// assert_eq!(None, long_bitmap.last_bit());
    ///
    /// let mut short_bitmap = BitmapVec::new(16, GFP_KERNEL)?;
    ///
    /// short_bitmap.set_bit(7);
    /// long_bitmap.copy_and_extend(&short_bitmap);
    /// assert_eq!(Some(7), long_bitmap.last_bit());
    ///
    /// # Ok::<(), AllocError>(())
    /// ```
    #[inline]
    pub fn copy_and_extend(&mut self, src: &Bitmap) {
        let len = core::cmp::min(src.len(), self.len());
        // SAFETY: Access to `self` and `src` is within bounds. Both lengths fit in `u32`
        // because a `Bitmap` is at most `i32::MAX` bits, so the casts are lossless.
        unsafe {
            bindings::bitmap_copy_and_extend(
                self.as_mut_ptr(),
                src.as_ptr(),
                len as u32,
                self.len() as u32,
            )
        };
    }

    /// Finds last set bit.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::alloc::{AllocError, flags::GFP_KERNEL};
    /// use kernel::bitmap::BitmapVec;
    ///
    /// let bitmap = BitmapVec::new(64, GFP_KERNEL)?;
    ///
    /// match bitmap.last_bit() {
    ///     Some(idx) => {
    ///         pr_info!("The last bit has index {idx}.\n");
    ///     }
    ///     None => {
    ///         pr_info!("All bits in this bitmap are 0.\n");
    ///     }
    /// }
    /// # Ok::<(), AllocError>(())
    /// ```
    #[inline]
    pub fn last_bit(&self) -> Option<usize> {
        // SAFETY: `_find_next_bit` access is within bounds due to invariant.
        let index = unsafe { bindings::_find_last_bit(self.as_ptr(), self.len()) };
        if index >= self.len() {
            None
        } else {
            Some(index)
        }
    }

    /// Finds next set bit, starting from `start`.
    ///
    /// Returns `None` if `start` is greater or equal to `self.nbits`.
    #[inline]
    pub fn next_bit(&self, start: usize) -> Option<usize> {
        bitmap_assert!(
            start < self.len(),
            "`start` must be < {} was {}",
            self.len(),
            start
        );
        // SAFETY: `_find_next_bit` tolerates out-of-bounds arguments and returns a
        // value larger than or equal to `self.len()` in that case.
        let index = unsafe { bindings::_find_next_bit(self.as_ptr(), self.len(), start) };
        if index >= self.len() {
            None
        } else {
            Some(index)
        }
    }

    /// Finds next zero bit, starting from `start`.
    /// Returns `None` if `start` is greater than or equal to `self.len()`.
    #[inline]
    pub fn next_zero_bit(&self, start: usize) -> Option<usize> {
        bitmap_assert!(
            start < self.len(),
            "`start` must be < {} was {}",
            self.len(),
            start
        );
        // SAFETY: `_find_next_zero_bit` tolerates out-of-bounds arguments and returns a
        // value larger than or equal to `self.len()` in that case.
        let index = unsafe { bindings::_find_next_zero_bit(self.as_ptr(), self.len(), start) };
        if index >= self.len() {
            None
        } else {
            Some(index)
        }
    }

    /// Finds a contiguous area of `nbits` zero bits at or after `start`, where the area plus
    /// `align_offset` is aligned to `align`.
    ///
    /// Returns the bit index of the start of the area, or [`None`] if no such area fitting in
    /// the bitmap exists.
    ///
    /// The returned index plus `align_offset` is a multiple of `align`.
    ///
    /// # Panics
    ///
    /// Panics if CONFIG_RUST_BITMAP_HARDENED is enabled and `start` is out of bounds.
    #[inline]
    pub fn next_zero_area_off(
        &self,
        start: usize,
        nbits: NonZero<usize>,
        align: Alignment,
        align_offset: usize,
    ) -> Option<usize> {
        bitmap_assert!(
            start < self.len(),
            "`start` must be < {}, was {}",
            self.len(),
            start
        );

        let nr = u32::try_from(nbits.get()).ok()?;
        let align_mask = align.as_usize() - 1;

        // The C alignment and end arithmetic must not overflow, or it can read out of bounds.
        // Overflow is only possible on 32-bit.
        #[cfg(not(CONFIG_64BIT))]
        align_mask
            .checked_add(self.len())?
            .checked_add(nbits.get())?;

        // SAFETY: `bitmap_find_next_zero_area_off` is safe to use with an out of bounds `start`
        // value and, given the overflow check above, never reads beyond `self.len()` bits.
        let index = unsafe {
            bindings::bitmap_find_next_zero_area_off(
                self.as_ptr().cast_mut(),
                self.len(),
                start,
                nr,
                align_mask,
                align_offset,
            )
        };

        (index < self.len()).then_some(index)
    }

    /// Finds a contiguous area of `nbits` zero bits at or after `start`, aligned to `align`.
    ///
    /// Returns the bit index of the start of the area, or [`None`] if no such area fitting in
    /// the bitmap exists.
    ///
    /// The returned index is a multiple of `align`.
    ///
    /// # Panics
    ///
    /// Panics if CONFIG_RUST_BITMAP_HARDENED is enabled and `start` is out of bounds.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::{
    ///     alloc::{AllocError, flags::GFP_KERNEL},
    ///     bitmap::BitmapVec,
    ///     ptr::Alignment,
    ///     sizes::SizeConstants, //
    /// };
    ///
    /// let mut b = BitmapVec::new(64, GFP_KERNEL)?;
    ///
    /// assert_eq!(Some(0), b.next_zero_area(0, cv!(8), Alignment::SZ_1));
    /// b.set(0, cv!(5));
    /// assert_eq!(Some(5), b.next_zero_area(0, cv!(8), Alignment::SZ_1));
    /// assert_eq!(Some(8), b.next_zero_area(0, cv!(8), Alignment::SZ_8));
    /// assert_eq!(None, b.next_zero_area(0, cv!(65), Alignment::SZ_1));
    /// # Ok::<(), AllocError>(())
    /// ```
    #[inline]
    pub fn next_zero_area(
        &self,
        start: usize,
        nbits: NonZero<usize>,
        align: Alignment,
    ) -> Option<usize> {
        self.next_zero_area_off(start, nbits, align, 0)
    }

    /// Sets a contiguous area of `nbits` bits starting at `start`.
    ///
    /// If CONFIG_RUST_BITMAP_HARDENED is not enabled and the area `start..start + nbits` is out of
    /// bounds, does nothing.
    ///
    /// # Panics
    ///
    /// Panics if CONFIG_RUST_BITMAP_HARDENED is enabled and the area `start..start + nbits` is out
    /// of bounds.
    #[inline]
    pub fn set(&mut self, start: usize, nbits: NonZero<usize>) {
        bitmap_assert_return!(
            start
                .checked_add(nbits.get())
                .is_some_and(|end| end <= self.len()),
            "Area `start..start + nbits` ({}..{}) must be within bounds {}",
            start,
            start.saturating_add(nbits.get()),
            self.len()
        );
        // SAFETY: The area `start..start + nbits` is within bounds and a `Bitmap` is at most
        // `i32::MAX` bits, so the casts are lossless.
        unsafe { bindings::__bitmap_set(self.as_mut_ptr(), start as u32, nbits.get() as i32) };
    }

    /// Clears a contiguous area of `nbits` bits starting at `start`.
    ///
    /// If CONFIG_RUST_BITMAP_HARDENED is not enabled and the area `start..start + nbits` is out of
    /// bounds, does nothing.
    ///
    /// # Panics
    ///
    /// Panics if CONFIG_RUST_BITMAP_HARDENED is enabled and the area `start..start + nbits` is out
    /// of bounds.
    #[inline]
    pub fn clear(&mut self, start: usize, nbits: NonZero<usize>) {
        bitmap_assert_return!(
            start
                .checked_add(nbits.get())
                .is_some_and(|end| end <= self.len()),
            "Area `start..start + nbits` ({}..{}) must be within bounds {}",
            start,
            start.saturating_add(nbits.get()),
            self.len()
        );
        // SAFETY: The area `start..start + nbits` is within bounds and a `Bitmap` is at most
        // `i32::MAX` bits, so the casts are lossless.
        unsafe { bindings::__bitmap_clear(self.as_mut_ptr(), start as u32, nbits.get() as i32) };
    }
}

#[cfg(CONFIG_RUST_BITMAP_KUNIT_TEST)]
#[macros::kunit_tests(rust_kernel_bitmap)]
mod tests {
    use super::*;
    use kernel::{
        alloc::flags::GFP_KERNEL,
        num::cv,
        sizes::SizeConstants, //
    };

    #[test]
    fn bitmap_borrow() {
        let fake_bitmap: [usize; 2] = [0, 0];
        let fake_bitmap_len = 2 * usize::BITS as usize;
        // SAFETY: `fake_c_bitmap` is an array of expected length.
        let b = unsafe { Bitmap::from_raw(fake_bitmap.as_ptr(), fake_bitmap_len) };
        assert_eq!(fake_bitmap_len, b.len());
        assert_eq!(None, b.next_bit(0));
    }

    #[test]
    fn bitmap_copy() {
        let fake_bitmap: usize = 0xFF;
        // SAFETY: `fake_c_bitmap` can be used as one-element array of expected length.
        let b = unsafe { Bitmap::from_raw(core::ptr::addr_of!(fake_bitmap), 8) };
        assert_eq!(8, b.len());
        assert_eq!(None, b.next_zero_bit(0));
    }

    #[test]
    fn bitmap_vec_new() -> Result<(), AllocError> {
        let b = BitmapVec::new(0, GFP_KERNEL)?;
        assert_eq!(0, b.len());

        let b = BitmapVec::new(3, GFP_KERNEL)?;
        assert_eq!(3, b.len());

        let b = BitmapVec::new(1024, GFP_KERNEL)?;
        assert_eq!(1024, b.len());

        // Requesting too large values results in [`AllocError`].
        let res = BitmapVec::new(1 << 31, GFP_KERNEL);
        assert!(res.is_err());
        Ok(())
    }

    #[test]
    fn bitmap_set_clear_find() -> Result<(), AllocError> {
        let mut b = BitmapVec::new(128, GFP_KERNEL)?;

        // Zero-initialized
        assert_eq!(None, b.next_bit(0));
        assert_eq!(Some(0), b.next_zero_bit(0));
        assert_eq!(None, b.last_bit());

        b.set_bit(17);

        assert_eq!(Some(17), b.next_bit(0));
        assert_eq!(Some(17), b.next_bit(17));
        assert_eq!(None, b.next_bit(18));
        assert_eq!(Some(17), b.last_bit());

        b.set_bit(107);

        assert_eq!(Some(17), b.next_bit(0));
        assert_eq!(Some(17), b.next_bit(17));
        assert_eq!(Some(107), b.next_bit(18));
        assert_eq!(Some(107), b.last_bit());

        b.clear_bit(17);

        assert_eq!(Some(107), b.next_bit(0));
        assert_eq!(Some(107), b.last_bit());
        Ok(())
    }

    #[test]
    #[cfg(not(CONFIG_RUST_BITMAP_HARDENED))]
    fn owned_bitmap_out_of_bounds() -> Result<(), AllocError> {
        let mut b = BitmapVec::new(128, GFP_KERNEL)?;

        b.set_bit(2048);
        b.set_bit_atomic(2048);
        b.clear_bit(2048);
        b.clear_bit_atomic(2048);
        assert_eq!(None, b.next_bit(2048));
        assert_eq!(None, b.next_zero_bit(2048));
        assert_eq!(None, b.last_bit());
        Ok(())
    }

    // TODO: uncomment once kunit supports `#[should_panic]`.
    // #[cfg(CONFIG_RUST_BITMAP_HARDENED)]
    // #[test]
    // #[should_panic]
    // fn owned_bitmap_out_of_bounds() -> Result<(), AllocError> {
    //     let mut b = BitmapVec::new(128, GFP_KERNEL)?;
    //
    //     b.set_bit(2048);
    // }

    #[test]
    fn bitmap_copy_and_extend() -> Result<(), AllocError> {
        let mut long_bitmap = BitmapVec::new(256, GFP_KERNEL)?;

        long_bitmap.set_bit(3);
        long_bitmap.set_bit(200);

        let mut short_bitmap = BitmapVec::new(32, GFP_KERNEL)?;

        short_bitmap.set_bit(17);

        long_bitmap.copy_and_extend(&short_bitmap);

        // Previous bits have been cleared.
        assert_eq!(Some(17), long_bitmap.next_bit(0));
        assert_eq!(Some(17), long_bitmap.last_bit());
        Ok(())
    }

    #[test]
    fn bitmap_area_set_clear_find() -> Result<(), AllocError> {
        let mut b = BitmapVec::new(128, GFP_KERNEL)?;

        assert_eq!(Some(0), b.next_zero_area(0, cv!(5), Alignment::SZ_1));
        b.set(0, cv!(5)); // Now contains {[0, 5)}.

        assert_eq!(Some(0), b.next_bit(0));
        assert_eq!(Some(4), b.next_bit(4));
        assert_eq!(Some(5), b.next_zero_bit(0));
        assert_eq!(Some(5), b.next_zero_area(0, cv!(5), Alignment::SZ_1));
        assert_eq!(Some(8), b.next_zero_area(0, cv!(5), Alignment::SZ_8));

        b.set(8, cv!(8)); // Now contains {[0, 5), [8, 16)}.
        assert_eq!(Some(16), b.next_zero_area(0, cv!(4), Alignment::SZ_16));
        assert_eq!(Some(16), b.next_zero_area(0, cv!(4), Alignment::SZ_1));

        b.clear(0, cv!(5)); // Now contains {[8, 16)}.
        assert_eq!(Some(0), b.next_zero_area(0, cv!(5), Alignment::SZ_1));
        assert_eq!(Some(8), b.next_bit(0));
        assert_eq!(Some(15), b.last_bit());

        b.set(60, cv!(10)); // Now contains {[8, 16), [60, 70)}.
        assert_eq!(Some(60), b.next_bit(16));
        assert_eq!(Some(69), b.last_bit());
        assert_eq!(Some(16), b.next_zero_area(9, cv!(40), Alignment::SZ_1));
        assert_eq!(Some(70), b.next_zero_area(0, cv!(45), Alignment::SZ_1));

        b.clear(62, cv!(6)); // Now contains {[8, 16), [60, 62), [68, 70)}.
        assert_eq!(Some(62), b.next_zero_area(60, cv!(6), Alignment::SZ_1));
        assert_eq!(Some(61), b.next_bit(61));
        assert_eq!(Some(69), b.last_bit());
        Ok(())
    }

    #[test]
    fn bitmap_area_exhaustion() -> Result<(), AllocError> {
        let mut b = BitmapVec::new(64, GFP_KERNEL)?;

        assert_eq!(None, b.next_zero_area(0, cv!(65), Alignment::SZ_1));
        assert_eq!(None, b.next_zero_area(0, cv!(usize::MAX), Alignment::SZ_1));
        assert_eq!(None, b.next_zero_area(1, cv!(usize::MAX), Alignment::SZ_1));

        b.set_bit(0); // Now contains {[0, 1)}.
        assert_eq!(None, b.next_zero_area(0, cv!(usize::MAX), Alignment::SZ_1));

        b.set(0, cv!(61)); // Now contains {[0, 61)}.
        assert_eq!(None, b.next_zero_area(0, cv!(4), Alignment::SZ_1));
        assert_eq!(Some(61), b.next_zero_area(0, cv!(3), Alignment::SZ_1));
        assert_eq!(None, b.next_zero_area(0, cv!(1), Alignment::SZ_64));
        Ok(())
    }

    #[test]
    fn bitmap_area_off() -> Result<(), AllocError> {
        let mut b = BitmapVec::new(64, GFP_KERNEL)?;

        b.set(0, cv!(5)); // Now contains {[0, 5)}.

        // The area plus align_offset starts at a multiple of the alignment.
        assert_eq!(Some(7), b.next_zero_area_off(0, cv!(8), Alignment::SZ_8, 1));
        assert_eq!(Some(5), b.next_zero_area_off(0, cv!(8), Alignment::SZ_8, 3));

        // A zero offset behaves like next_zero_area().
        assert_eq!(
            b.next_zero_area(0, cv!(8), Alignment::SZ_8),
            b.next_zero_area_off(0, cv!(8), Alignment::SZ_8, 0)
        );
        Ok(())
    }

    #[test]
    #[cfg(not(CONFIG_RUST_BITMAP_HARDENED))]
    fn owned_bitmap_area_out_of_bounds() -> Result<(), AllocError> {
        let mut b = BitmapVec::new(64, GFP_KERNEL)?;

        // Should be ignored since out of bounds.
        b.set(64, cv!(4));
        b.set(62, cv!(8));
        b.set(usize::MAX, cv!(1));
        b.clear(usize::MAX, cv!(1));
        b.clear(2048, cv!(8));
        assert_eq!(None, b.next_bit(0));
        assert_eq!(None, b.next_zero_area(64, cv!(1), Alignment::SZ_1));
        Ok(())
    }
}
