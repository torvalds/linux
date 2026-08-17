// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2023 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

use core::{fmt, hash::Hash};

use super::*;
use crate::pointer::{invariant::Valid, SizeEq, TransmuteFrom};

/// A type with no alignment requirement.
///
/// An `Unalign` wraps a `T`, removing any alignment requirement. `Unalign<T>`
/// has the same size and bit validity as `T`, but not necessarily the same
/// alignment [or ABI]. This is useful if a type with an alignment requirement
/// needs to be read from a chunk of memory which provides no alignment
/// guarantees.
///
/// Since `Unalign` has no alignment requirement, the inner `T` may not be
/// properly aligned in memory. There are five ways to access the inner `T`:
/// - by value, using [`get`] or [`into_inner`]
/// - by reference inside of a callback, using [`update`]
/// - fallibly by reference, using [`try_deref`] or [`try_deref_mut`]; these can
///   fail if the `Unalign` does not satisfy `T`'s alignment requirement at
///   runtime
/// - unsafely by reference, using [`deref_unchecked`] or
///   [`deref_mut_unchecked`]; it is the caller's responsibility to ensure that
///   the `Unalign` satisfies `T`'s alignment requirement
/// - (where `T: Unaligned`) infallibly by reference, using [`Deref::deref`] or
///   [`DerefMut::deref_mut`]
///
/// [or ABI]: https://github.com/google/zerocopy/issues/164
/// [`get`]: Unalign::get
/// [`into_inner`]: Unalign::into_inner
/// [`update`]: Unalign::update
/// [`try_deref`]: Unalign::try_deref
/// [`try_deref_mut`]: Unalign::try_deref_mut
/// [`deref_unchecked`]: Unalign::deref_unchecked
/// [`deref_mut_unchecked`]: Unalign::deref_mut_unchecked
///
/// # Example
///
/// In this example, we need `EthernetFrame` to have no alignment requirement -
/// and thus implement [`Unaligned`]. `EtherType` is `#[repr(u16)]` and so
/// cannot implement `Unaligned`. We use `Unalign` to relax `EtherType`'s
/// alignment requirement so that `EthernetFrame` has no alignment requirement
/// and can implement `Unaligned`.
///
/// ```rust
/// use zerocopy::*;
/// # use zerocopy_derive::*;
/// # #[derive(FromBytes, KnownLayout, Immutable, Unaligned)] #[repr(C)] struct Mac([u8; 6]);
///
/// # #[derive(PartialEq, Copy, Clone, Debug)]
/// #[derive(TryFromBytes, KnownLayout, Immutable)]
/// #[repr(u16)]
/// enum EtherType {
///     Ipv4 = 0x0800u16.to_be(),
///     Arp = 0x0806u16.to_be(),
///     Ipv6 = 0x86DDu16.to_be(),
///     # /*
///     ...
///     # */
/// }
///
/// #[derive(TryFromBytes, KnownLayout, Immutable, Unaligned)]
/// #[repr(C)]
/// struct EthernetFrame {
///     src: Mac,
///     dst: Mac,
///     ethertype: Unalign<EtherType>,
///     payload: [u8],
/// }
///
/// let bytes = &[
///     # 0, 1, 2, 3, 4, 5,
///     # 6, 7, 8, 9, 10, 11,
///     # /*
///     ...
///     # */
///     0x86, 0xDD,            // EtherType
///     0xDE, 0xAD, 0xBE, 0xEF // Payload
/// ][..];
///
/// // PANICS: Guaranteed not to panic because `bytes` is of the right
/// // length, has the right contents, and `EthernetFrame` has no
/// // alignment requirement.
/// let packet = EthernetFrame::try_ref_from_bytes(&bytes).unwrap();
///
/// assert_eq!(packet.ethertype.get(), EtherType::Ipv6);
/// assert_eq!(packet.payload, [0xDE, 0xAD, 0xBE, 0xEF]);
/// ```
///
/// # Safety
///
/// `Unalign<T>` is guaranteed to have the same size and bit validity as `T`,
/// and to have [`UnsafeCell`]s covering the same byte ranges as `T`.
/// `Unalign<T>` is guaranteed to have alignment 1.
// NOTE: This type is sound to use with types that need to be dropped. The
// reason is that the compiler-generated drop code automatically moves all
// values to aligned memory slots before dropping them in-place. This is not
// well-documented, but it's hinted at in places like [1] and [2]. However, this
// also means that `T` must be `Sized`; unless something changes, we can never
// support unsized `T`. [3]
//
// [1] https://github.com/rust-lang/rust/issues/54148#issuecomment-420529646
// [2] https://github.com/google/zerocopy/pull/126#discussion_r1018512323
// [3] https://github.com/google/zerocopy/issues/209
#[allow(missing_debug_implementations)]
#[derive(Default, Copy)]
#[cfg_attr(any(feature = "derive", test), derive(Immutable, FromBytes, IntoBytes, Unaligned))]
#[repr(C, packed)]
pub struct Unalign<T>(T);

// We do not use `derive(KnownLayout)` on `Unalign`, because the derive is not
// smart enough to realize that `Unalign<T>` is always sized and thus emits a
// `KnownLayout` impl bounded on `T: KnownLayout.` This is overly restrictive.
impl_known_layout!(T => Unalign<T>);

// FIXME(https://github.com/rust-lang/rust-clippy/issues/16087): Move these
// attributes below the comment once this Clippy bug is fixed.
#[cfg_attr(
    all(__ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS, any(feature = "derive", test)),
    expect(unused_unsafe)
)]
#[cfg_attr(
    all(
        not(__ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS),
        any(feature = "derive", test)
    ),
    allow(unused_unsafe)
)]
// SAFETY:
// - `Unalign<T>` promises to have alignment 1, and so we don't require that `T:
//   Unaligned`.
// - `Unalign<T>` has the same bit validity as `T`, and so it is `FromZeros`,
//   `FromBytes`, or `IntoBytes` exactly when `T` is as well.
// - `Immutable`: `Unalign<T>` has the same fields as `T`, so it permits
//   interior mutation exactly when `T` does.
// - `TryFromBytes`: `Unalign<T>` has the same the same bit validity as `T`, so
//   `T::is_bit_valid` is a sound implementation of `is_bit_valid`.
//
#[allow(clippy::multiple_unsafe_ops_per_block)]
const _: () = unsafe {
    impl_or_verify!(T => Unaligned for Unalign<T>);
    impl_or_verify!(T: Immutable => Immutable for Unalign<T>);
    impl_or_verify!(
        T: TryFromBytes => TryFromBytes for Unalign<T>;
        |c| T::is_bit_valid(c.transmute::<_, _, BecauseImmutable>())
    );
    impl_or_verify!(T: FromZeros => FromZeros for Unalign<T>);
    impl_or_verify!(T: FromBytes => FromBytes for Unalign<T>);
    impl_or_verify!(T: IntoBytes => IntoBytes for Unalign<T>);
};

// Note that `Unalign: Clone` only if `T: Copy`. Since the inner `T` may not be
// aligned, there's no way to safely call `T::clone`, and so a `T: Clone` bound
// is not sufficient to implement `Clone` for `Unalign`.
impl<T: Copy> Clone for Unalign<T> {
    #[inline(always)]
    fn clone(&self) -> Unalign<T> {
        *self
    }
}

impl<T> Unalign<T> {
    /// Constructs a new `Unalign`.
    #[inline(always)]
    pub const fn new(val: T) -> Unalign<T> {
        Unalign(val)
    }

    /// Consumes `self`, returning the inner `T`.
    #[inline(always)]
    pub const fn into_inner(self) -> T {
        // SAFETY: Since `Unalign` is `#[repr(C, packed)]`, it has the same size
        // and bit validity as `T`.
        //
        // We do this instead of just destructuring in order to prevent
        // `Unalign`'s `Drop::drop` from being run, since dropping is not
        // supported in `const fn`s.
        //
        // FIXME(https://github.com/rust-lang/rust/issues/73255): Destructure
        // instead of using unsafe.
        unsafe { crate::util::transmute_unchecked(self) }
    }

    /// Attempts to return a reference to the wrapped `T`, failing if `self` is
    /// not properly aligned.
    ///
    /// If `self` does not satisfy `align_of::<T>()`, then `try_deref` returns
    /// `Err`.
    ///
    /// If `T: Unaligned`, then `Unalign<T>` implements [`Deref`], and callers
    /// may prefer [`Deref::deref`], which is infallible.
    #[inline(always)]
    pub fn try_deref(&self) -> Result<&T, AlignmentError<&Self, T>> {
        let inner = Ptr::from_ref(self).transmute();
        match inner.try_into_aligned() {
            Ok(aligned) => Ok(aligned.as_ref()),
            Err(err) => Err(err.map_src(
                #[inline(always)]
                |src| src.into_unalign().as_ref(),
            )),
        }
    }

    /// Attempts to return a mutable reference to the wrapped `T`, failing if
    /// `self` is not properly aligned.
    ///
    /// If `self` does not satisfy `align_of::<T>()`, then `try_deref` returns
    /// `Err`.
    ///
    /// If `T: Unaligned`, then `Unalign<T>` implements [`DerefMut`], and
    /// callers may prefer [`DerefMut::deref_mut`], which is infallible.
    #[inline(always)]
    pub fn try_deref_mut(&mut self) -> Result<&mut T, AlignmentError<&mut Self, T>> {
        let inner = Ptr::from_mut(self).transmute::<_, _, (_, (_, _))>();
        match inner.try_into_aligned() {
            Ok(aligned) => Ok(aligned.as_mut()),
            Err(err) => Err(err.map_src(|src| src.into_unalign().as_mut())),
        }
    }

    /// Returns a reference to the wrapped `T` without checking alignment.
    ///
    /// If `T: Unaligned`, then `Unalign<T>` implements[ `Deref`], and callers
    /// may prefer [`Deref::deref`], which is safe.
    ///
    /// # Safety
    ///
    /// The caller must guarantee that `self` satisfies `align_of::<T>()`.
    #[inline(always)]
    pub const unsafe fn deref_unchecked(&self) -> &T {
        // SAFETY: `Unalign<T>` is `repr(transparent)`, so there is a valid `T`
        // at the same memory location as `self`. It has no alignment guarantee,
        // but the caller has promised that `self` is properly aligned, so we
        // know that it is sound to create a reference to `T` at this memory
        // location.
        //
        // We use `mem::transmute` instead of `&*self.get_ptr()` because
        // dereferencing pointers is not stable in `const` on our current MSRV
        // (1.56 as of this writing).
        unsafe { mem::transmute(self) }
    }

    /// Returns a mutable reference to the wrapped `T` without checking
    /// alignment.
    ///
    /// If `T: Unaligned`, then `Unalign<T>` implements[ `DerefMut`], and
    /// callers may prefer [`DerefMut::deref_mut`], which is safe.
    ///
    /// # Safety
    ///
    /// The caller must guarantee that `self` satisfies `align_of::<T>()`.
    #[inline(always)]
    pub unsafe fn deref_mut_unchecked(&mut self) -> &mut T {
        // SAFETY: `self.get_mut_ptr()` returns a raw pointer to a valid `T` at
        // the same memory location as `self`. It has no alignment guarantee,
        // but the caller has promised that `self` is properly aligned, so we
        // know that the pointer itself is aligned, and thus that it is sound to
        // create a reference to a `T` at this memory location.
        unsafe { &mut *self.get_mut_ptr() }
    }

    /// Gets an unaligned raw pointer to the inner `T`.
    ///
    /// # Safety
    ///
    /// The returned raw pointer is not necessarily aligned to
    /// `align_of::<T>()`. Most functions which operate on raw pointers require
    /// those pointers to be aligned, so calling those functions with the result
    /// of `get_ptr` will result in undefined behavior if alignment is not
    /// guaranteed using some out-of-band mechanism. In general, the only
    /// functions which are safe to call with this pointer are those which are
    /// explicitly documented as being sound to use with an unaligned pointer,
    /// such as [`read_unaligned`].
    ///
    /// Even if the caller is permitted to mutate `self` (e.g. they have
    /// ownership or a mutable borrow), it is not guaranteed to be sound to
    /// write through the returned pointer. If writing is required, prefer
    /// [`get_mut_ptr`] instead.
    ///
    /// [`read_unaligned`]: core::ptr::read_unaligned
    /// [`get_mut_ptr`]: Unalign::get_mut_ptr
    #[inline(always)]
    pub const fn get_ptr(&self) -> *const T {
        ptr::addr_of!(self.0)
    }

    /// Gets an unaligned mutable raw pointer to the inner `T`.
    ///
    /// # Safety
    ///
    /// The returned raw pointer is not necessarily aligned to
    /// `align_of::<T>()`. Most functions which operate on raw pointers require
    /// those pointers to be aligned, so calling those functions with the result
    /// of `get_ptr` will result in undefined behavior if alignment is not
    /// guaranteed using some out-of-band mechanism. In general, the only
    /// functions which are safe to call with this pointer are those which are
    /// explicitly documented as being sound to use with an unaligned pointer,
    /// such as [`read_unaligned`].
    ///
    /// [`read_unaligned`]: core::ptr::read_unaligned
    // FIXME(https://github.com/rust-lang/rust/issues/57349): Make this `const`.
    #[inline(always)]
    pub fn get_mut_ptr(&mut self) -> *mut T {
        ptr::addr_of_mut!(self.0)
    }

    /// Sets the inner `T`, dropping the previous value.
    // FIXME(https://github.com/rust-lang/rust/issues/57349): Make this `const`.
    #[inline(always)]
    pub fn set(&mut self, t: T) {
        *self = Unalign::new(t);
    }

    /// Updates the inner `T` by calling a function on it.
    ///
    /// If [`T: Unaligned`], then `Unalign<T>` implements [`DerefMut`], and that
    /// impl should be preferred over this method when performing updates, as it
    /// will usually be faster and more ergonomic.
    ///
    /// For large types, this method may be expensive, as it requires copying
    /// `2 * size_of::<T>()` bytes. \[1\]
    ///
    /// \[1\] Since the inner `T` may not be aligned, it would not be sound to
    /// invoke `f` on it directly. Instead, `update` moves it into a
    /// properly-aligned location in the local stack frame, calls `f` on it, and
    /// then moves it back to its original location in `self`.
    ///
    /// [`T: Unaligned`]: Unaligned
    #[inline]
    pub fn update<O, F: FnOnce(&mut T) -> O>(&mut self, f: F) -> O {
        if mem::align_of::<T>() == 1 {
            // While we advise callers to use `DerefMut` when `T: Unaligned`,
            // not all callers will be able to guarantee `T: Unaligned` in all
            // cases. In particular, callers who are themselves providing an API
            // which is generic over `T` may sometimes be called by *their*
            // callers with `T` such that `align_of::<T>() == 1`, but cannot
            // guarantee this in the general case. Thus, this optimization may
            // sometimes be helpful.

            // SAFETY: Since `T`'s alignment is 1, `self` satisfies its
            // alignment by definition.
            let t = unsafe { self.deref_mut_unchecked() };
            return f(t);
        }

        // On drop, this moves `copy` out of itself and uses `ptr::write` to
        // overwrite `slf`.
        struct WriteBackOnDrop<T> {
            copy: ManuallyDrop<T>,
            slf: *mut Unalign<T>,
        }

        impl<T> Drop for WriteBackOnDrop<T> {
            fn drop(&mut self) {
                // SAFETY: We never use `copy` again as required by
                // `ManuallyDrop::take`.
                let copy = unsafe { ManuallyDrop::take(&mut self.copy) };
                // SAFETY: `slf` is the raw pointer value of `self`. We know it
                // is valid for writes and properly aligned because `self` is a
                // mutable reference, which guarantees both of these properties.
                unsafe { ptr::write(self.slf, Unalign::new(copy)) };
            }
        }

        // SAFETY: We know that `self` is valid for reads, properly aligned, and
        // points to an initialized `Unalign<T>` because it is a mutable
        // reference, which guarantees all of these properties.
        //
        // Since `T: !Copy`, it would be unsound in the general case to allow
        // both the original `Unalign<T>` and the copy to be used by safe code.
        // We guarantee that the copy is used to overwrite the original in the
        // `Drop::drop` impl of `WriteBackOnDrop`. So long as this `drop` is
        // called before any other safe code executes, soundness is upheld.
        // While this method can terminate in two ways (by returning normally or
        // by unwinding due to a panic in `f`), in both cases, `write_back` is
        // dropped - and its `drop` called - before any other safe code can
        // execute.
        let copy = unsafe { ptr::read(self) }.into_inner();
        let mut write_back = WriteBackOnDrop { copy: ManuallyDrop::new(copy), slf: self };

        let ret = f(&mut write_back.copy);

        drop(write_back);
        ret
    }
}

impl<T: Copy> Unalign<T> {
    /// Gets a copy of the inner `T`.
    // FIXME(https://github.com/rust-lang/rust/issues/57349): Make this `const`.
    #[inline(always)]
    pub fn get(&self) -> T {
        let Unalign(val) = *self;
        val
    }
}

impl<T: Unaligned> Deref for Unalign<T> {
    type Target = T;

    #[inline(always)]
    fn deref(&self) -> &T {
        Ptr::from_ref(self).transmute().bikeshed_recall_aligned().as_ref()
    }
}

impl<T: Unaligned> DerefMut for Unalign<T> {
    #[inline(always)]
    fn deref_mut(&mut self) -> &mut T {
        Ptr::from_mut(self).transmute::<_, _, (_, (_, _))>().bikeshed_recall_aligned().as_mut()
    }
}

impl<T: Unaligned + PartialOrd> PartialOrd<Unalign<T>> for Unalign<T> {
    #[inline(always)]
    fn partial_cmp(&self, other: &Unalign<T>) -> Option<Ordering> {
        PartialOrd::partial_cmp(self.deref(), other.deref())
    }
}

impl<T: Unaligned + Ord> Ord for Unalign<T> {
    #[inline(always)]
    fn cmp(&self, other: &Unalign<T>) -> Ordering {
        Ord::cmp(self.deref(), other.deref())
    }
}

impl<T: Unaligned + PartialEq> PartialEq<Unalign<T>> for Unalign<T> {
    #[inline(always)]
    fn eq(&self, other: &Unalign<T>) -> bool {
        PartialEq::eq(self.deref(), other.deref())
    }
}

impl<T: Unaligned + Eq> Eq for Unalign<T> {}

impl<T: Unaligned + Hash> Hash for Unalign<T> {
    #[inline(always)]
    fn hash<H>(&self, state: &mut H)
    where
        H: Hasher,
    {
        self.deref().hash(state);
    }
}

impl<T: Unaligned + Debug> Debug for Unalign<T> {
    #[inline(always)]
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        Debug::fmt(self.deref(), f)
    }
}

impl<T: Unaligned + Display> Display for Unalign<T> {
    #[inline(always)]
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        Display::fmt(self.deref(), f)
    }
}

/// A wrapper type to construct uninitialized instances of `T`.
///
/// `MaybeUninit` is identical to the [standard library
/// `MaybeUninit`][core-maybe-uninit] type except that it supports unsized
/// types.
///
/// # Layout
///
/// The same layout guarantees and caveats apply to `MaybeUninit<T>` as apply to
/// the [standard library `MaybeUninit`][core-maybe-uninit] with one exception:
/// for `T: !Sized`, there is no single value for `T`'s size. Instead, for such
/// types, the following are guaranteed:
/// - Every [valid size][valid-size] for `T` is a valid size for
///   `MaybeUninit<T>` and vice versa
/// - Given `t: *const T` and `m: *const MaybeUninit<T>` with identical fat
///   pointer metadata, `t` and `m` address the same number of bytes (and
///   likewise for `*mut`)
///
/// [core-maybe-uninit]: core::mem::MaybeUninit
/// [valid-size]: crate::KnownLayout#what-is-a-valid-size
#[repr(transparent)]
#[doc(hidden)]
pub struct MaybeUninit<T: ?Sized + KnownLayout>(
    // SAFETY: `MaybeUninit<T>` has the same size as `T`, because (by invariant
    // on `T::MaybeUninit`) `T::MaybeUninit` has `T::LAYOUT` identical to `T`,
    // and because (invariant on `T::LAYOUT`) we can trust that `LAYOUT`
    // accurately reflects the layout of `T`. By invariant on `T::MaybeUninit`,
    // it admits uninitialized bytes in all positions. Because `MaybeUninit` is
    // marked `repr(transparent)`, these properties additionally hold true for
    // `Self`.
    T::MaybeUninit,
);

#[doc(hidden)]
impl<T: ?Sized + KnownLayout> MaybeUninit<T> {
    /// Constructs a `MaybeUninit<T>` initialized with the given value.
    #[inline(always)]
    pub fn new(val: T) -> Self
    where
        T: Sized,
        Self: Sized,
    {
        // SAFETY: It is valid to transmute `val` to `MaybeUninit<T>` because it
        // is both valid to transmute `val` to `T::MaybeUninit`, and it is valid
        // to transmute from `T::MaybeUninit` to `MaybeUninit<T>`.
        //
        // First, it is valid to transmute `val` to `T::MaybeUninit` because, by
        // invariant on `T::MaybeUninit`:
        // - For `T: Sized`, `T` and `T::MaybeUninit` have the same size.
        // - All byte sequences of the correct size are valid values of
        //   `T::MaybeUninit`.
        //
        // Second, it is additionally valid to transmute from `T::MaybeUninit`
        // to `MaybeUninit<T>`, because `MaybeUninit<T>` is a
        // `repr(transparent)` wrapper around `T::MaybeUninit`.
        //
        // These two transmutes are collapsed into one so we don't need to add a
        // `T::MaybeUninit: Sized` bound to this function's `where` clause.
        unsafe { crate::util::transmute_unchecked(val) }
    }

    /// Constructs an uninitialized `MaybeUninit<T>`.
    #[must_use]
    #[inline(always)]
    pub fn uninit() -> Self
    where
        T: Sized,
        Self: Sized,
    {
        let uninit = CoreMaybeUninit::<T>::uninit();
        // SAFETY: It is valid to transmute from `CoreMaybeUninit<T>` to
        // `MaybeUninit<T>` since they both admit uninitialized bytes in all
        // positions, and they have the same size (i.e., that of `T`).
        //
        // `MaybeUninit<T>` has the same size as `T`, because (by invariant on
        // `T::MaybeUninit`) `T::MaybeUninit` has `T::LAYOUT` identical to `T`,
        // and because (invariant on `T::LAYOUT`) we can trust that `LAYOUT`
        // accurately reflects the layout of `T`.
        //
        // `CoreMaybeUninit<T>` has the same size as `T` [1] and admits
        // uninitialized bytes in all positions.
        //
        // [1] Per https://doc.rust-lang.org/1.81.0/std/mem/union.MaybeUninit.html#layout-1:
        //
        //   `MaybeUninit<T>` is guaranteed to have the same size, alignment,
        //   and ABI as `T`
        unsafe { crate::util::transmute_unchecked(uninit) }
    }

    /// Creates a `Box<MaybeUninit<T>>`.
    ///
    /// This function is useful for allocating large, uninit values on the heap
    /// without ever creating a temporary instance of `Self` on the stack.
    ///
    /// # Errors
    ///
    /// Returns an error on allocation failure. Allocation failure is guaranteed
    /// never to cause a panic or an abort.
    #[cfg(feature = "alloc")]
    #[inline]
    pub fn new_boxed_uninit(meta: T::PointerMetadata) -> Result<Box<Self>, AllocError> {
        // SAFETY: `alloc::alloc::alloc_zeroed` is a valid argument of
        // `new_box`. The referent of the pointer returned by `alloc` (and,
        // consequently, the `Box` derived from it) is a valid instance of
        // `Self`, because `Self` is `MaybeUninit` and thus admits arbitrary
        // (un)initialized bytes.
        unsafe { crate::util::new_box(meta, alloc::alloc::alloc) }
    }

    /// Extracts the value from the `MaybeUninit<T>` container.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `self` is in an bit-valid state. Depending
    /// on subsequent use, it may also need to be in a library-valid state.
    #[inline(always)]
    pub unsafe fn assume_init(self) -> T
    where
        T: Sized,
        Self: Sized,
    {
        // SAFETY: The caller guarantees that `self` is in an bit-valid state.
        unsafe { crate::util::transmute_unchecked(self) }
    }
}

impl<T: ?Sized + KnownLayout> fmt::Debug for MaybeUninit<T> {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.pad(core::any::type_name::<Self>())
    }
}

#[allow(unreachable_pub)] // False positive on MSRV
#[doc(hidden)]
pub use read_only_def::*;
mod read_only_def {
    /// A read-only wrapper.
    ///
    /// A `ReadOnly<T>` disables any interior mutability in `T`, ensuring that
    /// a `&ReadOnly<T>` is genuinely read-only. Thus, `ReadOnly<T>` is
    /// [`Immutable`] regardless of whether `T` is.
    ///
    /// Note that `&mut ReadOnly<T>` still permits mutation – the read-only
    /// property only applies to shared references.
    ///
    /// [`Immutable`]: crate::Immutable
    #[repr(transparent)]
    pub struct ReadOnly<T: ?Sized> {
        // INVARIANT: `inner` is never mutated through a `&ReadOnly<T>`
        // reference.
        inner: T,
    }

    impl<T> ReadOnly<T> {
        /// Creates a new `ReadOnly`.
        #[must_use]
        #[inline(always)]
        pub const fn new(t: T) -> ReadOnly<T> {
            ReadOnly { inner: t }
        }

        /// Returns the inner value.
        #[must_use]
        #[inline(always)]
        pub fn into_inner(r: ReadOnly<T>) -> T {
            r.inner
        }
    }

    impl<T: ?Sized> ReadOnly<T> {
        #[inline(always)]
        pub(crate) fn as_mut(r: &mut ReadOnly<T>) -> &mut T {
            // SAFETY: `r: &mut ReadOnly`, so this doesn't violate the invariant
            // that `inner` is never mutated through a `&ReadOnly<T>` reference.
            &mut r.inner
        }

        /// # Safety
        ///
        /// The caller promises not to mutate the referent (i.e., via interior
        /// mutation).
        pub(crate) const unsafe fn as_ref_unchecked(r: &ReadOnly<T>) -> &T {
            // SAFETY: The caller promises not to mutate the referent.
            &r.inner
        }
    }
}

// SAFETY: `ReadOnly<T>` is a `#[repr(transparent)` wrapper around `T`.
const _: () = unsafe {
    unsafe_impl_known_layout!(T: ?Sized + KnownLayout => #[repr(T)] ReadOnly<T>);
};

#[allow(clippy::multiple_unsafe_ops_per_block)]
// SAFETY:
// - `ReadOnly<T>` has the same alignment as `T`, and so it is `Unaligned`
//   exactly when `T` is as well.
// - `ReadOnly<T>` has the same bit validity as `T`, and so this `is_bit_valid`
//   implementation is correct, and thus the `TryFromBytes` impl is sound.
// - `ReadOnly<T>` has the same bit validity as `T`, and so it is `FromZeros`,
//   `FromBytes`, and `IntoBytes` exactly when `T` is as well.
const _: () = unsafe {
    unsafe_impl!(T: ?Sized + Unaligned => Unaligned for ReadOnly<T>);
    unsafe_impl!(
        T: ?Sized + TryFromBytes => TryFromBytes for ReadOnly<T>;
        |c| T::is_bit_valid(c.cast::<_, <ReadOnly<T> as SizeEq<ReadOnly<ReadOnly<T>>>>::CastFrom, _>())
    );
    unsafe_impl!(T: ?Sized + FromZeros => FromZeros for ReadOnly<T>);
    unsafe_impl!(T: ?Sized + FromBytes => FromBytes for ReadOnly<T>);
    unsafe_impl!(T: ?Sized + IntoBytes => IntoBytes for ReadOnly<T>);
};

// SAFETY: By invariant, `inner` is never mutated through a `&ReadOnly<T>`
// reference.
const _: () = unsafe {
    unsafe_impl!(T: ?Sized => Immutable for ReadOnly<T>);
};

const _: () = {
    use crate::pointer::cast::CastExact;

    // SAFETY: `ReadOnly<T>` has the same layout as `T`.
    define_cast!(unsafe { pub CastFromReadOnly<T: ?Sized> = ReadOnly<T> => T});
    // SAFETY: `ReadOnly<T>` has the same layout as `T`.
    unsafe impl<T: ?Sized> CastExact<ReadOnly<T>, T> for CastFromReadOnly {}
    // SAFETY: `ReadOnly<T>` has the same layout as `T`.
    define_cast!(unsafe { pub CastToReadOnly<T: ?Sized> = T => ReadOnly<T>});
    // SAFETY: `ReadOnly<T>` has the same layout as `T`.
    unsafe impl<T: ?Sized> CastExact<T, ReadOnly<T>> for CastToReadOnly {}

    impl<T: ?Sized> SizeEq<ReadOnly<T>> for T {
        type CastFrom = CastFromReadOnly;
    }

    impl<T: ?Sized> SizeEq<T> for ReadOnly<T> {
        type CastFrom = CastToReadOnly;
    }
};

// SAFETY: `ReadOnly<T>` is a `#[repr(transparent)]` wrapper around `T`, and so
// it has the same bit validity as `T`.
unsafe impl<T: ?Sized> TransmuteFrom<T, Valid, Valid> for ReadOnly<T> {}

// SAFETY: `ReadOnly<T>` is a `#[repr(transparent)]` wrapper around `T`, and so
// it has the same bit validity as `T`.
unsafe impl<T: ?Sized> TransmuteFrom<ReadOnly<T>, Valid, Valid> for T {}

impl<'a, T: ?Sized + Immutable> From<&'a T> for &'a ReadOnly<T> {
    #[inline(always)]
    fn from(t: &'a T) -> &'a ReadOnly<T> {
        let ro = Ptr::from_ref(t).transmute::<_, _, (_, _)>();
        // SAFETY: `ReadOnly<T>` has the same alignment as `T`, and
        // `Ptr::from_ref` produces an aligned `Ptr`.
        let ro = unsafe { ro.assume_alignment() };
        ro.as_ref()
    }
}

impl<T: ?Sized + Immutable> Deref for ReadOnly<T> {
    type Target = T;

    #[inline(always)]
    fn deref(&self) -> &Self::Target {
        // SAFETY: By `T: Immutable`, `&T` doesn't permit interior mutation.
        unsafe { ReadOnly::as_ref_unchecked(self) }
    }
}

impl<T: ?Sized + Immutable> DerefMut for ReadOnly<T> {
    #[inline(always)]
    fn deref_mut(&mut self) -> &mut Self::Target {
        ReadOnly::as_mut(self)
    }
}

impl<T: ?Sized + Immutable + Debug> Debug for ReadOnly<T> {
    #[inline(always)]
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        self.deref().fmt(f)
    }
}

// SAFETY: See safety comment on `ProjectToTag`.
unsafe impl<T: HasTag + ?Sized> HasTag for ReadOnly<T> {
    #[allow(clippy::missing_inline_in_public_items)]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized,
    {
    }

    type Tag = T::Tag;

    // SAFETY: `<T as SizeEq<ReadOnly<T>>>::CastFrom` is a no-op projection that
    // produces a pointer with the same referent. By invariant, for any `Ptr<'_,
    // T, I>` it is sound to use `T::ProjectToTag` to project to a `Ptr<'_,
    // T::Tag, I>`. Since `ReadOnly<T>` has the same layout and validity as `T`,
    // the same is true of projecting from a `Ptr<'_, ReadOnly<T>, I>`.
    type ProjectToTag = crate::pointer::cast::TransitiveProject<
        T,
        <T as SizeEq<ReadOnly<T>>>::CastFrom,
        T::ProjectToTag,
    >;
}

// SAFETY: `ReadOnly<T>` is a `#[repr(transparent)]` wrapper around `T`, and so
// has the same fields at the same offsets. Thus, it satisfies the safety
// invariants of `HasField<Field, VARIANT_ID, FIELD_ID>` for field `f` exactly
// when `T` does, as guaranteed by the `T: HasField` bound:
// - If `VARIANT_ID` is `STRUCT_VARIANT_ID` or `UNION_VARIANT_ID`, then `T` has
//   the layout of a struct or union type. Since `ReadOnly<T>` is a transparent
//   wrapper around `T`, it does too. Otherwise, if `VARIANT_ID` is an enum
//   variant index, then `T` has the layout of an enum type, and `ReadOnly<T>`
//   does too.
// - By `T: HasField<_, _, FIELD_ID>`:
//   - `T` has a field `f` with name `n` such that
//     `FIELD_ID = zerocopy::ident_id!(n)` or at index `i` such that
//     `FIELD_ID = zerocopy::ident_id!(i)`.
//   - `Field` has the same visibility as `f`.
//   - `T::Type` has the same type as `f`. Thus, `ReadOnly<T::Type>` has the
//     same type as `f`, wrapped in `ReadOnly`.
//
// `project` satisfies its post-condition – namely, that the returned pointer
// refers to a non-strict subset of the bytes of `slf`'s referent, and has the
// same provenance as `slf` – because all intermediate operations satisfy those
// same conditions.
unsafe impl<T, Field, const VARIANT_ID: i128, const FIELD_ID: i128>
    HasField<Field, VARIANT_ID, FIELD_ID> for ReadOnly<T>
where
    T: HasField<Field, VARIANT_ID, FIELD_ID> + ?Sized,
{
    #[allow(clippy::missing_inline_in_public_items)]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized,
    {
    }

    type Type = ReadOnly<T::Type>;

    #[inline(always)]
    fn project(slf: PtrInner<'_, Self>) -> *mut ReadOnly<T::Type> {
        slf.project::<_, <T as SizeEq<ReadOnly<T>>>::CastFrom>()
            .project::<_, crate::pointer::cast::Projection<Field, VARIANT_ID, FIELD_ID>>()
            .project::<_, <ReadOnly<T::Type> as SizeEq<T::Type>>::CastFrom>()
            .as_non_null()
            .as_ptr()
    }
}

// SAFETY: `ReadOnly<T>` is a `#[repr(transparent)]` wrapper around `T`, and so
// has the same fields at the same offsets. `is_projectable` simply delegates to
// `T::is_projectable`, which is sound because a `Ptr<'_, ReadOnly<T>, I>` will
// be projectable exactly when a `Ptr<'_, T, I>` referent is.
unsafe impl<T, Field, I, const VARIANT_ID: i128, const FIELD_ID: i128>
    ProjectField<Field, I, VARIANT_ID, FIELD_ID> for ReadOnly<T>
where
    T: ProjectField<Field, I, VARIANT_ID, FIELD_ID> + ?Sized,
    I: invariant::Invariants,
{
    #[allow(clippy::missing_inline_in_public_items)]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized,
    {
    }

    type Invariants = T::Invariants;

    type Error = T::Error;

    #[inline(always)]
    fn is_projectable<'a>(ptr: Ptr<'a, Self::Tag, I>) -> Result<(), Self::Error> {
        T::is_projectable(ptr)
    }
}

#[cfg(test)]
mod tests {
    use core::panic::AssertUnwindSafe;

    use super::*;
    use crate::util::testutil::*;

    #[test]
    fn test_unalign() {
        // Test methods that don't depend on alignment.
        let mut u = Unalign::new(AU64(123));
        assert_eq!(u.get(), AU64(123));
        assert_eq!(u.into_inner(), AU64(123));
        assert_eq!(u.get_ptr(), <*const _>::cast::<AU64>(&u));
        assert_eq!(u.get_mut_ptr(), <*mut _>::cast::<AU64>(&mut u));
        u.set(AU64(321));
        assert_eq!(u.get(), AU64(321));

        // Test methods that depend on alignment (when alignment is satisfied).
        let mut u: Align<_, AU64> = Align::new(Unalign::new(AU64(123)));
        assert_eq!(u.t.try_deref().unwrap(), &AU64(123));
        assert_eq!(u.t.try_deref_mut().unwrap(), &mut AU64(123));
        // SAFETY: The `Align<_, AU64>` guarantees proper alignment.
        assert_eq!(unsafe { u.t.deref_unchecked() }, &AU64(123));
        // SAFETY: The `Align<_, AU64>` guarantees proper alignment.
        assert_eq!(unsafe { u.t.deref_mut_unchecked() }, &mut AU64(123));
        *u.t.try_deref_mut().unwrap() = AU64(321);
        assert_eq!(u.t.get(), AU64(321));

        // Test methods that depend on alignment (when alignment is not
        // satisfied).
        let mut u: ForceUnalign<_, AU64> = ForceUnalign::new(Unalign::new(AU64(123)));
        assert!(matches!(u.t.try_deref(), Err(AlignmentError { .. })));
        assert!(matches!(u.t.try_deref_mut(), Err(AlignmentError { .. })));

        // Test methods that depend on `T: Unaligned`.
        let mut u = Unalign::new(123u8);
        assert_eq!(u.try_deref(), Ok(&123));
        assert_eq!(u.try_deref_mut(), Ok(&mut 123));
        assert_eq!(u.deref(), &123);
        assert_eq!(u.deref_mut(), &mut 123);
        *u = 21;
        assert_eq!(u.get(), 21);

        // Test that some `Unalign` functions and methods are `const`.
        const _UNALIGN: Unalign<u64> = Unalign::new(0);
        const _UNALIGN_PTR: *const u64 = _UNALIGN.get_ptr();
        const _U64: u64 = _UNALIGN.into_inner();
        // Make sure all code is considered "used".
        //
        // FIXME(https://github.com/rust-lang/rust/issues/104084): Remove this
        // attribute.
        #[allow(dead_code)]
        const _: () = {
            let x: Align<_, AU64> = Align::new(Unalign::new(AU64(123)));
            // Make sure that `deref_unchecked` is `const`.
            //
            // SAFETY: The `Align<_, AU64>` guarantees proper alignment.
            let au64 = unsafe { x.t.deref_unchecked() };
            match au64 {
                AU64(123) => {}
                _ => const_unreachable!(),
            }
        };
    }

    #[test]
    fn test_unalign_update() {
        let mut u = Unalign::new(AU64(123));
        u.update(|a| a.0 += 1);
        assert_eq!(u.get(), AU64(124));

        // Test that, even if the callback panics, the original is still
        // correctly overwritten. Use a `Box` so that Miri is more likely to
        // catch any unsoundness (which would likely result in two `Box`es for
        // the same heap object, which is the sort of thing that Miri would
        // probably catch).
        let mut u = Unalign::new(Box::new(AU64(123)));
        let res = std::panic::catch_unwind(AssertUnwindSafe(|| {
            u.update(|a| {
                a.0 += 1;
                panic!();
            })
        }));
        assert!(res.is_err());
        assert_eq!(u.into_inner(), Box::new(AU64(124)));

        // Test the align_of::<T>() == 1 optimization.
        let mut u = Unalign::new([0u8, 1]);
        u.update(|a| a[0] += 1);
        assert_eq!(u.get(), [1u8, 1]);
    }

    #[test]
    fn test_unalign_copy_clone() {
        // Test that `Copy` and `Clone` do not cause soundness issues. This test
        // is mainly meant to exercise UB that would be caught by Miri.

        // `u.t` is definitely not validly-aligned for `AU64`'s alignment of 8.
        let u = ForceUnalign::<_, AU64>::new(Unalign::new(AU64(123)));
        #[allow(clippy::clone_on_copy)]
        let v = u.t.clone();
        let w = u.t;
        assert_eq!(u.t.get(), v.get());
        assert_eq!(u.t.get(), w.get());
        assert_eq!(v.get(), w.get());
    }

    #[test]
    fn test_unalign_trait_impls() {
        let zero = Unalign::new(0u8);
        let one = Unalign::new(1u8);

        assert!(zero < one);
        assert_eq!(PartialOrd::partial_cmp(&zero, &one), Some(Ordering::Less));
        assert_eq!(Ord::cmp(&zero, &one), Ordering::Less);

        assert_ne!(zero, one);
        assert_eq!(zero, zero);
        assert!(!PartialEq::eq(&zero, &one));
        assert!(PartialEq::eq(&zero, &zero));

        fn hash<T: Hash>(t: &T) -> u64 {
            let mut h = std::collections::hash_map::DefaultHasher::new();
            t.hash(&mut h);
            h.finish()
        }

        assert_eq!(hash(&zero), hash(&0u8));
        assert_eq!(hash(&one), hash(&1u8));

        assert_eq!(format!("{:?}", zero), format!("{:?}", 0u8));
        assert_eq!(format!("{:?}", one), format!("{:?}", 1u8));
        assert_eq!(format!("{}", zero), format!("{}", 0u8));
        assert_eq!(format!("{}", one), format!("{}", 1u8));
    }

    #[test]
    #[allow(clippy::as_conversions)]
    fn test_maybe_uninit() {
        // int
        {
            let input = 42;
            let uninit = MaybeUninit::new(input);
            // SAFETY: `uninit` is in an initialized state
            let output = unsafe { uninit.assume_init() };
            assert_eq!(input, output);
        }

        // thin ref
        {
            let input = 42;
            let uninit = MaybeUninit::new(&input);
            // SAFETY: `uninit` is in an initialized state
            let output = unsafe { uninit.assume_init() };
            assert_eq!(&input as *const _, output as *const _);
            assert_eq!(input, *output);
        }

        // wide ref
        {
            let input = [1, 2, 3, 4];
            let uninit = MaybeUninit::new(&input[..]);
            // SAFETY: `uninit` is in an initialized state
            let output = unsafe { uninit.assume_init() };
            assert_eq!(&input[..] as *const _, output as *const _);
            assert_eq!(input, *output);
        }
    }
    #[test]
    fn test_maybe_uninit_uninit() {
        let _uninit = MaybeUninit::<u8>::uninit();
        // Cannot check value, but can check it compiles and runs
    }

    #[test]
    #[cfg(feature = "alloc")]
    fn test_maybe_uninit_new_boxed_uninit() {
        let _boxed = MaybeUninit::<u8>::new_boxed_uninit(()).unwrap();
    }

    #[test]
    fn test_maybe_uninit_debug() {
        let uninit = MaybeUninit::<u8>::uninit();
        assert!(format!("{:?}", uninit).contains("MaybeUninit"));
    }
}
