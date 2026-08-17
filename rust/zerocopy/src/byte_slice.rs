// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2024 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

//! Traits for types that encapsulate a `[u8]`.
//!
//! These traits are used to bound the `B` parameter of [`Ref`].

use core::{
    cell,
    ops::{Deref, DerefMut},
};

// For each trait polyfill, as soon as the corresponding feature is stable, the
// polyfill import will be unused because method/function resolution will prefer
// the inherent method/function over a trait method/function. Thus, we suppress
// the `unused_imports` warning.
//
// See the documentation on `util::polyfills` for more information.
#[allow(unused_imports)]
use crate::util::polyfills::{self, NonNullExt as _, NumExt as _};
#[cfg(doc)]
use crate::Ref;

/// A mutable or immutable reference to a byte slice.
///
/// `ByteSlice` abstracts over the mutability of a byte slice reference, and is
/// implemented for various special reference types such as
/// [`Ref<[u8]>`](core::cell::Ref) and [`RefMut<[u8]>`](core::cell::RefMut).
///
/// # Safety
///
/// Implementations of `ByteSlice` must promise that their implementations of
/// [`Deref`] and [`DerefMut`] are "stable". In particular, given `B: ByteSlice`
/// and `b: B`, two calls, each to either `b.deref()` or `b.deref_mut()`, must
/// return a byte slice with the same address and length. This must hold even if
/// the two calls are separated by an arbitrary sequence of calls to methods on
/// `ByteSlice`, [`ByteSliceMut`], [`IntoByteSlice`], or [`IntoByteSliceMut`],
/// or on their super-traits. This does *not* need to hold if the two calls are
/// separated by any method calls, field accesses, or field modifications *other
/// than* those from these traits.
///
/// Note that this also implies that, given `b: B`, the address and length
/// cannot be modified via objects other than `b`, either on the same thread or
/// on another thread.
pub unsafe trait ByteSlice: Deref<Target = [u8]> + Sized {}

/// A mutable reference to a byte slice.
///
/// `ByteSliceMut` abstracts over various ways of storing a mutable reference to
/// a byte slice, and is implemented for various special reference types such as
/// `RefMut<[u8]>`.
///
/// `ByteSliceMut` is a shorthand for [`ByteSlice`] and [`DerefMut`].
pub trait ByteSliceMut: ByteSlice + DerefMut {}
impl<B: ByteSlice + DerefMut> ByteSliceMut for B {}

/// A [`ByteSlice`] which can be copied without violating dereference stability.
///
/// # Safety
///
/// If `B: CopyableByteSlice`, then the dereference stability properties
/// required by [`ByteSlice`] (see that trait's safety documentation) do not
/// only hold regarding two calls to `b.deref()` or `b.deref_mut()`, but also
/// hold regarding `c.deref()` or `c.deref_mut()`, where `c` is produced by
/// copying `b`.
pub unsafe trait CopyableByteSlice: ByteSlice + Copy + CloneableByteSlice {}

/// A [`ByteSlice`] which can be cloned without violating dereference stability.
///
/// # Safety
///
/// If `B: CloneableByteSlice`, then the dereference stability properties
/// required by [`ByteSlice`] (see that trait's safety documentation) do not
/// only hold regarding two calls to `b.deref()` or `b.deref_mut()`, but also
/// hold regarding `c.deref()` or `c.deref_mut()`, where `c` is produced by
/// `b.clone()`, `b.clone().clone()`, etc.
pub unsafe trait CloneableByteSlice: ByteSlice + Clone {}

/// A [`ByteSlice`] that can be split in two.
///
/// # Safety
///
/// Unsafe code may depend for its soundness on the assumption that `split_at`
/// and `split_at_unchecked` are implemented correctly. In particular, given `B:
/// SplitByteSlice` and `b: B`, if `b.deref()` returns a byte slice with address
/// `addr` and length `len`, then if `split <= len`, both of these
/// invocations:
/// - `b.split_at(split)`
/// - `b.split_at_unchecked(split)`
///
/// ...will return `(first, second)` such that:
/// - `first`'s address is `addr` and its length is `split`
/// - `second`'s address is `addr + split` and its length is `len - split`
pub unsafe trait SplitByteSlice: ByteSlice {
    /// Attempts to split `self` at the midpoint.
    ///
    /// `s.split_at(mid)` returns `Ok((s[..mid], s[mid..]))` if `mid <=
    /// s.deref().len()` and otherwise returns `Err(s)`.
    ///
    /// # Safety
    ///
    /// Unsafe code may rely on this function correctly implementing the above
    /// functionality.
    #[inline]
    fn split_at(self, mid: usize) -> Result<(Self, Self), Self> {
        if mid <= self.deref().len() {
            // SAFETY: Above, we ensure that `mid <= self.deref().len()`. By
            // invariant on `ByteSlice`, a supertrait of `SplitByteSlice`,
            // `.deref()` is guaranteed to be "stable"; i.e., it will always
            // dereference to a byte slice of the same address and length. Thus,
            // we can be sure that the above precondition remains satisfied
            // through the call to `split_at_unchecked`.
            unsafe { Ok(self.split_at_unchecked(mid)) }
        } else {
            Err(self)
        }
    }

    /// Splits the slice at the midpoint, possibly omitting bounds checks.
    ///
    /// `s.split_at_unchecked(mid)` returns `s[..mid]` and `s[mid..]`.
    ///
    /// # Safety
    ///
    /// `mid` must not be greater than `self.deref().len()`.
    ///
    /// # Panics
    ///
    /// Implementations of this method may choose to perform a bounds check and
    /// panic if `mid > self.deref().len()`. They may also panic for any other
    /// reason. Since it is optional, callers must not rely on this behavior for
    /// soundness.
    #[must_use]
    unsafe fn split_at_unchecked(self, mid: usize) -> (Self, Self);
}

/// A shorthand for [`SplitByteSlice`] and [`ByteSliceMut`].
pub trait SplitByteSliceMut: SplitByteSlice + ByteSliceMut {}
impl<B: SplitByteSlice + ByteSliceMut> SplitByteSliceMut for B {}

#[allow(clippy::missing_safety_doc)] // There's a `Safety` section on `into_byte_slice`.
/// A [`ByteSlice`] that conveys no ownership, and so can be converted into a
/// byte slice.
///
/// Some `ByteSlice` types (notably, the standard library's [`Ref`] type) convey
/// ownership, and so they cannot soundly be moved by-value into a byte slice
/// type (`&[u8]`). Some methods in this crate's API (such as [`Ref::into_ref`])
/// are only compatible with `ByteSlice` types without these ownership
/// semantics.
///
/// [`Ref`]: core::cell::Ref
pub unsafe trait IntoByteSlice<'a>: ByteSlice {
    /// Coverts `self` into a `&[u8]`.
    ///
    /// # Safety
    ///
    /// The returned reference has the same address and length as `self.deref()`
    /// and `self.deref_mut()`.
    ///
    /// Note that, combined with the safety invariant on [`ByteSlice`], this
    /// safety invariant implies that the returned reference is "stable" in the
    /// sense described in the `ByteSlice` docs.
    fn into_byte_slice(self) -> &'a [u8];
}

#[allow(clippy::missing_safety_doc)] // There's a `Safety` section on `into_byte_slice_mut`.
/// A [`ByteSliceMut`] that conveys no ownership, and so can be converted into a
/// mutable byte slice.
///
/// Some `ByteSliceMut` types (notably, the standard library's [`RefMut`] type)
/// convey ownership, and so they cannot soundly be moved by-value into a byte
/// slice type (`&mut [u8]`). Some methods in this crate's API (such as
/// [`Ref::into_mut`]) are only compatible with `ByteSliceMut` types without
/// these ownership semantics.
///
/// [`RefMut`]: core::cell::RefMut
pub unsafe trait IntoByteSliceMut<'a>: IntoByteSlice<'a> + ByteSliceMut {
    /// Coverts `self` into a `&mut [u8]`.
    ///
    /// # Safety
    ///
    /// The returned reference has the same address and length as `self.deref()`
    /// and `self.deref_mut()`.
    ///
    /// Note that, combined with the safety invariant on [`ByteSlice`], this
    /// safety invariant implies that the returned reference is "stable" in the
    /// sense described in the `ByteSlice` docs.
    fn into_byte_slice_mut(self) -> &'a mut [u8];
}

// FIXME(#429): Add a "SAFETY" comment and remove this `allow`.
#[allow(clippy::undocumented_unsafe_blocks)]
unsafe impl ByteSlice for &[u8] {}

// FIXME(#429): Add a "SAFETY" comment and remove this `allow`.
#[allow(clippy::undocumented_unsafe_blocks)]
unsafe impl CopyableByteSlice for &[u8] {}

// FIXME(#429): Add a "SAFETY" comment and remove this `allow`.
#[allow(clippy::undocumented_unsafe_blocks)]
unsafe impl CloneableByteSlice for &[u8] {}

// SAFETY: This delegates to `polyfills:split_at_unchecked`, which is documented
// to correctly split `self` into two slices at the given `mid` point.
unsafe impl SplitByteSlice for &[u8] {
    #[inline]
    unsafe fn split_at_unchecked(self, mid: usize) -> (Self, Self) {
        // SAFETY: By contract on caller, `mid` is not greater than
        // `self.len()`.
        #[allow(clippy::multiple_unsafe_ops_per_block)]
        unsafe {
            (<[u8]>::get_unchecked(self, ..mid), <[u8]>::get_unchecked(self, mid..))
        }
    }
}

// SAFETY: See inline.
unsafe impl<'a> IntoByteSlice<'a> for &'a [u8] {
    #[inline(always)]
    fn into_byte_slice(self) -> &'a [u8] {
        // SAFETY: It would be patently insane to implement `<Deref for
        // &[u8]>::deref` as anything other than `fn deref(&self) -> &[u8] {
        // *self }`. Assuming this holds, then `self` is stable as required by
        // `into_byte_slice`.
        self
    }
}

// FIXME(#429): Add a "SAFETY" comment and remove this `allow`.
#[allow(clippy::undocumented_unsafe_blocks)]
unsafe impl ByteSlice for &mut [u8] {}

// SAFETY: This delegates to `polyfills:split_at_mut_unchecked`, which is
// documented to correctly split `self` into two slices at the given `mid`
// point.
unsafe impl SplitByteSlice for &mut [u8] {
    #[inline]
    unsafe fn split_at_unchecked(self, mid: usize) -> (Self, Self) {
        use core::slice::from_raw_parts_mut;

        // `l_ptr` is non-null, because `self` is non-null, by invariant on
        // `&mut [u8]`.
        let l_ptr = self.as_mut_ptr();

        // SAFETY: By contract on caller, `mid` is not greater than
        // `self.len()`.
        let r_ptr = unsafe { l_ptr.add(mid) };

        let l_len = mid;

        // SAFETY: By contract on caller, `mid` is not greater than
        // `self.len()`.
        //
        // FIXME(#67): Remove this allow. See NumExt for more details.
        #[allow(unstable_name_collisions)]
        let r_len = unsafe { self.len().unchecked_sub(mid) };

        // SAFETY: These invocations of `from_raw_parts_mut` satisfy its
        // documented safety preconditions [1]:
        // - The data `l_ptr` and `r_ptr` are valid for both reads and writes of
        //   `l_len` and `r_len` bytes, respectively, and they are trivially
        //   aligned. In particular:
        //   - The entire memory range of each slice is contained within a
        //     single allocated object, since `l_ptr` and `r_ptr` are both
        //     derived from within the address range of `self`.
        //   - Both `l_ptr` and `r_ptr` are non-null and trivially aligned.
        //     `self` is non-null by invariant on `&mut [u8]`, and the
        //     operations that derive `l_ptr` and `r_ptr` from `self` do not
        //     nullify either pointer.
        // - The data `l_ptr` and `r_ptr` point to `l_len` and `r_len`,
        //   respectively, consecutive properly initialized values of type `u8`.
        //   This is true for `self` by invariant on `&mut [u8]`, and remains
        //   true for these two sub-slices of `self`.
        // - The memory referenced by the returned slice cannot be accessed
        //   through any other pointer (not derived from the return value) for
        //   the duration of lifetime `'a``, because:
        //   - `split_at_unchecked` consumes `self` (which is not `Copy`),
        //   - `split_at_unchecked` does not exfiltrate any references to this
        //     memory, besides those references returned below,
        //   - the returned slices are non-overlapping.
        // - The individual sizes of the sub-slices of `self` are no larger than
        //   `isize::MAX`, because their combined sizes are no larger than
        //   `isize::MAX`, by invariant on `self`.
        //
        // [1] https://doc.rust-lang.org/std/slice/fn.from_raw_parts_mut.html#safety
        #[allow(clippy::multiple_unsafe_ops_per_block)]
        unsafe {
            (from_raw_parts_mut(l_ptr, l_len), from_raw_parts_mut(r_ptr, r_len))
        }
    }
}

// SAFETY: See inline.
unsafe impl<'a> IntoByteSlice<'a> for &'a mut [u8] {
    #[inline(always)]
    fn into_byte_slice(self) -> &'a [u8] {
        // SAFETY: It would be patently insane to implement `<Deref for &mut
        // [u8]>::deref` as anything other than `fn deref(&self) -> &[u8] {
        // *self }`. Assuming this holds, then `self` is stable as required by
        // `into_byte_slice`.
        self
    }
}

// SAFETY: See inline.
unsafe impl<'a> IntoByteSliceMut<'a> for &'a mut [u8] {
    #[inline(always)]
    fn into_byte_slice_mut(self) -> &'a mut [u8] {
        // SAFETY: It would be patently insane to implement `<DerefMut for &mut
        // [u8]>::deref` as anything other than `fn deref_mut(&mut self) -> &mut
        // [u8] { *self }`. Assuming this holds, then `self` is stable as
        // required by `into_byte_slice_mut`.
        self
    }
}

// FIXME(#429): Add a "SAFETY" comment and remove this `allow`.
#[allow(clippy::undocumented_unsafe_blocks)]
unsafe impl ByteSlice for cell::Ref<'_, [u8]> {}

// SAFETY: This delegates to stdlib implementation of `Ref::map_split`, which is
// assumed to be correct, and `SplitByteSlice::split_at_unchecked`, which is
// documented to correctly split `self` into two slices at the given `mid`
// point.
unsafe impl SplitByteSlice for cell::Ref<'_, [u8]> {
    #[inline]
    unsafe fn split_at_unchecked(self, mid: usize) -> (Self, Self) {
        cell::Ref::map_split(self, |slice|
            // SAFETY: By precondition on caller, `mid` is not greater than
            // `slice.len()`.
            unsafe {
                SplitByteSlice::split_at_unchecked(slice, mid)
            })
    }
}

// FIXME(#429): Add a "SAFETY" comment and remove this `allow`.
#[allow(clippy::undocumented_unsafe_blocks)]
unsafe impl ByteSlice for cell::RefMut<'_, [u8]> {}

// SAFETY: This delegates to stdlib implementation of `RefMut::map_split`, which
// is assumed to be correct, and `SplitByteSlice::split_at_unchecked`, which is
// documented to correctly split `self` into two slices at the given `mid`
// point.
unsafe impl SplitByteSlice for cell::RefMut<'_, [u8]> {
    #[inline]
    unsafe fn split_at_unchecked(self, mid: usize) -> (Self, Self) {
        cell::RefMut::map_split(self, |slice|
            // SAFETY: By precondition on caller, `mid` is not greater than
            // `slice.len()`
            unsafe {
                SplitByteSlice::split_at_unchecked(slice, mid)
            })
    }
}

#[cfg(kani)]
mod proofs {
    use super::*;

    fn any_vec() -> Vec<u8> {
        let len = kani::any();
        kani::assume(len <= crate::DstLayout::MAX_SIZE);
        vec![0u8; len]
    }

    #[kani::proof]
    fn prove_split_at_unchecked() {
        let v = any_vec();
        let slc = v.as_slice();
        let mid = kani::any();
        kani::assume(mid <= slc.len());
        let (l, r) = unsafe { slc.split_at_unchecked(mid) };
        assert_eq!(l.len() + r.len(), slc.len());

        let slc: *const _ = slc;
        let l: *const _ = l;
        let r: *const _ = r;

        assert_eq!(slc.cast::<u8>(), l.cast::<u8>());
        assert_eq!(unsafe { slc.cast::<u8>().add(mid) }, r.cast::<u8>());

        let mut v = any_vec();
        let slc = v.as_mut_slice();
        let len = slc.len();
        let mid = kani::any();
        kani::assume(mid <= slc.len());
        let (l, r) = unsafe { slc.split_at_unchecked(mid) };
        assert_eq!(l.len() + r.len(), len);

        let l: *mut _ = l;
        let r: *mut _ = r;
        let slc: *mut _ = slc;

        assert_eq!(slc.cast::<u8>(), l.cast::<u8>());
        assert_eq!(unsafe { slc.cast::<u8>().add(mid) }, r.cast::<u8>());
    }
}

#[cfg(test)]
mod tests {
    use core::cell::RefCell;

    use super::*;

    #[test]
    fn test_ref_split_at_unchecked() {
        let cell = RefCell::new([1, 2, 3, 4]);
        let borrow = cell.borrow();
        let slice_ref: cell::Ref<'_, [u8]> = cell::Ref::map(borrow, |a| &a[..]);
        // SAFETY: 2 is within bounds of [1, 2, 3, 4]
        let (l, r) = unsafe { slice_ref.split_at_unchecked(2) };
        assert_eq!(*l, [1, 2]);
        assert_eq!(*r, [3, 4]);
    }

    #[test]
    fn test_ref_mut_split_at_unchecked() {
        let cell = RefCell::new([1, 2, 3, 4]);
        let borrow_mut = cell.borrow_mut();
        let slice_ref_mut: cell::RefMut<'_, [u8]> = cell::RefMut::map(borrow_mut, |a| &mut a[..]);
        // SAFETY: 2 is within bounds of [1, 2, 3, 4]
        let (l, r) = unsafe { slice_ref_mut.split_at_unchecked(2) };
        assert_eq!(*l, [1, 2]);
        assert_eq!(*r, [3, 4]);
    }
}
