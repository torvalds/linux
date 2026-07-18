// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2025 The Fuchsia Authors
//
// Licensed under the 2-Clause BSD License <LICENSE-BSD or
// https://opensource.org/license/bsd-2-clause>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

use super::*;
use crate::pointer::invariant::{Aligned, Exclusive, Invariants, Shared, Valid};

/// Types that can be split in two.
///
/// This trait generalizes Rust's existing support for splitting slices to
/// support slices and slice-based dynamically-sized types ("slice DSTs").
///
/// # Implementation
///
/// **Do not implement this trait yourself!** Instead, use
/// [`#[derive(SplitAt)]`][derive]; e.g.:
///
/// ```
/// # use zerocopy_derive::{SplitAt, KnownLayout};
/// #[derive(SplitAt, KnownLayout)]
/// #[repr(C)]
/// struct MyStruct<T: ?Sized> {
/// # /*
///     ...,
/// # */
///     // `SplitAt` types must have at least one field.
///     field: T,
/// }
/// ```
///
/// This derive performs a sophisticated, compile-time safety analysis to
/// determine whether a type is `SplitAt`.
///
/// # Safety
///
/// This trait does not convey any safety guarantees to code outside this crate.
///
/// You must not rely on the `#[doc(hidden)]` internals of `SplitAt`. Future
/// releases of zerocopy may make backwards-breaking changes to these items,
/// including changes that only affect soundness, which may cause code which
/// uses those items to silently become unsound.
///
#[cfg_attr(feature = "derive", doc = "[derive]: zerocopy_derive::SplitAt")]
#[cfg_attr(
    not(feature = "derive"),
    doc = concat!("[derive]: https://docs.rs/zerocopy/", env!("CARGO_PKG_VERSION"), "/zerocopy/derive.SplitAt.html"),
)]
#[cfg_attr(
    not(no_zerocopy_diagnostic_on_unimplemented_1_78_0),
    diagnostic::on_unimplemented(note = "Consider adding `#[derive(SplitAt)]` to `{Self}`")
)]
// # Safety
//
// The trailing slice is well-aligned for its element type. `Self` is `[T]`, or
// a `repr(C)` or `repr(transparent)` slice DST.
pub unsafe trait SplitAt: KnownLayout<PointerMetadata = usize> {
    /// The element type of the trailing slice.
    type Elem;

    #[doc(hidden)]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized;

    /// Unsafely splits `self` in two.
    ///
    /// # Safety
    ///
    /// The caller promises that `l_len` is not greater than the length of
    /// `self`'s trailing slice.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "split_at_unchecked",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[inline]
    #[must_use]
    unsafe fn split_at_unchecked(&self, l_len: usize) -> Split<&Self> {
        // SAFETY: By precondition on the caller, `l_len <= self.len()`.
        unsafe { Split::<&Self>::new(self, l_len) }
    }

    /// Attempts to split `self` in two.
    ///
    /// Returns `None` if `l_len` is greater than the length of `self`'s
    /// trailing slice.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::{SplitAt, FromBytes};
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, FromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct Packet {
    ///     length: u8,
    ///     body: [u8],
    /// }
    ///
    /// // These bytes encode a `Packet`.
    /// let bytes = &[4, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let packet = Packet::ref_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    ///
    /// // Attempt to split `packet` at `length`.
    /// let split = packet.split_at(packet.length as usize).unwrap();
    ///
    /// // Use the `Immutable` bound on `Packet` to prove that it's okay to
    /// // return concurrent references to `packet` and `rest`.
    /// let (packet, rest) = split.via_immutable();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4]);
    /// assert_eq!(rest, [5, 6, 7, 8, 9]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "split_at",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[inline]
    #[must_use = "has no side effects"]
    fn split_at(&self, l_len: usize) -> Option<Split<&Self>> {
        MetadataOf::new_in_bounds(self, l_len).map(
            #[inline(always)]
            |l_len| {
                // SAFETY: We have ensured that `l_len <= self.len()` (by
                // post-condition on `MetadataOf::new_in_bounds`)
                unsafe { Split::new(self, l_len.get()) }
            },
        )
    }

    /// Unsafely splits `self` in two.
    ///
    /// # Safety
    ///
    /// The caller promises that `l_len` is not greater than the length of
    /// `self`'s trailing slice.
    ///
    #[doc = codegen_header!("h5", "split_at_mut_unchecked")]
    ///
    /// See [`SplitAt::split_at_unchecked`](#method.split_at_unchecked.codegen).
    #[inline]
    #[must_use]
    unsafe fn split_at_mut_unchecked(&mut self, l_len: usize) -> Split<&mut Self> {
        // SAFETY: By precondition on the caller, `l_len <= self.len()`.
        unsafe { Split::<&mut Self>::new(self, l_len) }
    }

    /// Attempts to split `self` in two.
    ///
    /// Returns `None` if `l_len` is greater than the length of `self`'s
    /// trailing slice, or if the given `l_len` would result in [the trailing
    /// padding](KnownLayout#slice-dst-layout) of the left portion overlapping
    /// the right portion.
    ///
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::{SplitAt, FromBytes};
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, FromBytes, KnownLayout, IntoBytes)]
    /// #[repr(C)]
    /// struct Packet<B: ?Sized> {
    ///     length: u8,
    ///     body: B,
    /// }
    ///
    /// // These bytes encode a `Packet`.
    /// let mut bytes = &mut [4, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let packet = Packet::<[u8]>::mut_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    ///
    /// {
    ///     // Attempt to split `packet` at `length`.
    ///     let split = packet.split_at_mut(packet.length as usize).unwrap();
    ///
    ///     // Use the `IntoBytes` bound on `Packet` to prove that it's okay to
    ///     // return concurrent references to `packet` and `rest`.
    ///     let (packet, rest) = split.via_into_bytes();
    ///
    ///     assert_eq!(packet.length, 4);
    ///     assert_eq!(packet.body, [1, 2, 3, 4]);
    ///     assert_eq!(rest, [5, 6, 7, 8, 9]);
    ///
    ///     rest.fill(0);
    /// }
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 0, 0, 0, 0, 0]);
    /// ```
    ///
    #[doc = codegen_header!("h5", "split_at_mut")]
    ///
    /// See [`SplitAt::split_at`](#method.split_at.codegen).
    #[inline]
    fn split_at_mut(&mut self, l_len: usize) -> Option<Split<&mut Self>> {
        MetadataOf::new_in_bounds(self, l_len).map(
            #[inline(always)]
            |l_len| {
                // SAFETY: We have ensured that `l_len <= self.len()` (by
                // post-condition on `MetadataOf::new_in_bounds`)
                unsafe { Split::new(self, l_len.get()) }
            },
        )
    }
}

// SAFETY: `[T]`'s trailing slice is `[T]`, which is trivially aligned.
unsafe impl<T> SplitAt for [T] {
    type Elem = T;

    #[inline]
    #[allow(dead_code)]
    fn only_derive_is_allowed_to_implement_this_trait()
    where
        Self: Sized,
    {
    }
}

/// A `T` that has been split into two possibly-overlapping parts.
///
/// For some dynamically sized types, the padding that appears after the
/// trailing slice field [is a dynamic function of the trailing slice
/// length](KnownLayout#slice-dst-layout). If `T` is split at a length that
/// requires trailing padding, the trailing padding of the left part of the
/// split `T` will overlap the right part. If `T` is a mutable reference or
/// permits interior mutation, you must ensure that the left and right parts do
/// not overlap. You can do this at zero-cost using using
/// [`Self::via_immutable`], [`Self::via_into_bytes`], or
/// [`Self::via_unaligned`], or with a dynamic check by using
/// [`Self::via_runtime_check`].
#[derive(Debug)]
pub struct Split<T> {
    /// A pointer to the source slice DST.
    source: T,
    /// The length of the future left half of `source`.
    ///
    /// # Safety
    ///
    /// If `source` is a pointer to a slice DST, `l_len` is no greater than
    /// `source`'s length.
    l_len: usize,
}

impl<T> Split<T> {
    /// Produces a `Split` of `source` with `l_len`.
    ///
    /// # Safety
    ///
    /// `l_len` is no greater than `source`'s length.
    #[inline(always)]
    unsafe fn new(source: T, l_len: usize) -> Self {
        Self { source, l_len }
    }
}

impl<'a, T> Split<&'a T>
where
    T: ?Sized + SplitAt,
{
    #[inline(always)]
    fn into_ptr(self) -> Split<Ptr<'a, T, (Shared, Aligned, Valid)>> {
        let source = Ptr::from_ref(self.source);
        // SAFETY: `Ptr::from_ref(self.source)` points to exactly `self.source`
        // and thus maintains the invariants of `self` with respect to `l_len`.
        unsafe { Split::new(source, self.l_len) }
    }

    /// Produces the split parts of `self`, using [`Immutable`] to ensure that
    /// it is sound to have concurrent references to both parts.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::{SplitAt, FromBytes};
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, FromBytes, KnownLayout, Immutable)]
    /// #[repr(C)]
    /// struct Packet {
    ///     length: u8,
    ///     body: [u8],
    /// }
    ///
    /// // These bytes encode a `Packet`.
    /// let bytes = &[4, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let packet = Packet::ref_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    ///
    /// // Attempt to split `packet` at `length`.
    /// let split = packet.split_at(packet.length as usize).unwrap();
    ///
    /// // Use the `Immutable` bound on `Packet` to prove that it's okay to
    /// // return concurrent references to `packet` and `rest`.
    /// let (packet, rest) = split.via_immutable();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4]);
    /// assert_eq!(rest, [5, 6, 7, 8, 9]);
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "split_via_immutable",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn via_immutable(self) -> (&'a T, &'a [T::Elem])
    where
        T: Immutable,
    {
        let (l, r) = self.into_ptr().via_immutable();
        (l.as_ref(), r.as_ref())
    }

    /// Produces the split parts of `self`, using [`IntoBytes`] to ensure that
    /// it is sound to have concurrent references to both parts.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::{SplitAt, FromBytes};
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, FromBytes, KnownLayout, Immutable, IntoBytes)]
    /// #[repr(C)]
    /// struct Packet<B: ?Sized> {
    ///     length: u8,
    ///     body: B,
    /// }
    ///
    /// // These bytes encode a `Packet`.
    /// let bytes = &[4, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let packet = Packet::<[u8]>::ref_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    ///
    /// // Attempt to split `packet` at `length`.
    /// let split = packet.split_at(packet.length as usize).unwrap();
    ///
    /// // Use the `IntoBytes` bound on `Packet` to prove that it's okay to
    /// // return concurrent references to `packet` and `rest`.
    /// let (packet, rest) = split.via_into_bytes();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4]);
    /// assert_eq!(rest, [5, 6, 7, 8, 9]);
    /// ```
    ///
    #[doc = codegen_header!("h5", "split_via_into_bytes")]
    ///
    /// See [`Split::via_immutable`](#method.split_via_immutable.codegen).
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn via_into_bytes(self) -> (&'a T, &'a [T::Elem])
    where
        T: IntoBytes,
    {
        let (l, r) = self.into_ptr().via_into_bytes();
        (l.as_ref(), r.as_ref())
    }

    /// Produces the split parts of `self`, using [`Unaligned`] to ensure that
    /// it is sound to have concurrent references to both parts.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::{SplitAt, FromBytes};
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, FromBytes, KnownLayout, Immutable, Unaligned)]
    /// #[repr(C)]
    /// struct Packet {
    ///     length: u8,
    ///     body: [u8],
    /// }
    ///
    /// // These bytes encode a `Packet`.
    /// let bytes = &[4, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let packet = Packet::ref_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    ///
    /// // Attempt to split `packet` at `length`.
    /// let split = packet.split_at(packet.length as usize).unwrap();
    ///
    /// // Use the `Unaligned` bound on `Packet` to prove that it's okay to
    /// // return concurrent references to `packet` and `rest`.
    /// let (packet, rest) = split.via_unaligned();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4]);
    /// assert_eq!(rest, [5, 6, 7, 8, 9]);
    /// ```
    ///
    #[doc = codegen_header!("h5", "split_via_unaligned")]
    ///
    /// See [`Split::via_immutable`](#method.split_via_immutable.codegen).
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn via_unaligned(self) -> (&'a T, &'a [T::Elem])
    where
        T: Unaligned,
    {
        let (l, r) = self.into_ptr().via_unaligned();
        (l.as_ref(), r.as_ref())
    }

    /// Produces the split parts of `self`, using a dynamic check to ensure that
    /// it is sound to have concurrent references to both parts. You should
    /// prefer using [`Self::via_immutable`], [`Self::via_into_bytes`], or
    /// [`Self::via_unaligned`], which have no runtime cost.
    ///
    /// Note that this check is overly conservative if `T` is [`Immutable`]; for
    /// some types, this check will reject some splits which
    /// [`Self::via_immutable`] will accept.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::{SplitAt, FromBytes, IntoBytes, network_endian::U16};
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, FromBytes, KnownLayout, Immutable, Debug)]
    /// #[repr(C, align(2))]
    /// struct Packet {
    ///     length: U16,
    ///     body: [u8],
    /// }
    ///
    /// // These bytes encode a `Packet`.
    /// let bytes = [
    ///     4u16.to_be(),
    ///     1u16.to_be(),
    ///     2u16.to_be(),
    ///     3u16.to_be(),
    ///     4u16.to_be()
    /// ];
    ///
    /// let packet = Packet::ref_from_bytes(bytes.as_bytes()).unwrap();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [0, 1, 0, 2, 0, 3, 0, 4]);
    ///
    /// // Attempt to split `packet` at `length`.
    /// let split = packet.split_at(packet.length.into()).unwrap();
    ///
    /// // Use a dynamic check to prove that it's okay to return concurrent
    /// // references to `packet` and `rest`.
    /// let (packet, rest) = split.via_runtime_check().unwrap();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [0, 1, 0, 2]);
    /// assert_eq!(rest, [0, 3, 0, 4]);
    ///
    /// // Attempt to split `packet` at `length - 1`.
    /// let idx = packet.length.get() - 1;
    /// let split = packet.split_at(idx as usize).unwrap();
    ///
    /// // Attempt (and fail) to use a dynamic check to prove that it's okay
    /// // to return concurrent references to `packet` and `rest`. Note that
    /// // this is a case of `via_runtime_check` being overly conservative.
    /// // Although the left and right parts indeed overlap, the `Immutable`
    /// // bound ensures that concurrently referencing these overlapping
    /// // parts is sound.
    /// assert!(split.via_runtime_check().is_err());
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "split_via_runtime_check",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn via_runtime_check(self) -> Result<(&'a T, &'a [T::Elem]), Self> {
        match self.into_ptr().via_runtime_check() {
            Ok((l, r)) => Ok((l.as_ref(), r.as_ref())),
            Err(s) => Err(s.into_ref()),
        }
    }

    /// Unsafely produces the split parts of `self`.
    ///
    /// # Safety
    ///
    /// If `T` permits interior mutation, the trailing padding bytes of the left
    /// portion must not overlap the right portion. For some dynamically sized
    /// types, the padding that appears after the trailing slice field [is a
    /// dynamic function of the trailing slice
    /// length](KnownLayout#slice-dst-layout). Thus, for some types, this
    /// condition is dependent on the length of the left portion.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "split_via_unchecked",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub unsafe fn via_unchecked(self) -> (&'a T, &'a [T::Elem]) {
        // SAFETY: The aliasing of `self.into_ptr()` is not `Exclusive`, but the
        // caller has promised that if `T` permits interior mutation then the
        // left and right portions of `self` split at `l_len` do not overlap.
        let (l, r) = unsafe { self.into_ptr().via_unchecked() };
        (l.as_ref(), r.as_ref())
    }
}

impl<'a, T> Split<&'a mut T>
where
    T: ?Sized + SplitAt,
{
    #[inline(always)]
    fn into_ptr(self) -> Split<Ptr<'a, T, (Exclusive, Aligned, Valid)>> {
        let source = Ptr::from_mut(self.source);
        // SAFETY: `Ptr::from_mut(self.source)` points to exactly `self.source`,
        // and thus maintains the invariants of `self` with respect to `l_len`.
        unsafe { Split::new(source, self.l_len) }
    }

    /// Produces the split parts of `self`, using [`IntoBytes`] to ensure that
    /// it is sound to have concurrent references to both parts.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::{SplitAt, FromBytes};
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, FromBytes, KnownLayout, IntoBytes)]
    /// #[repr(C)]
    /// struct Packet<B: ?Sized> {
    ///     length: u8,
    ///     body: B,
    /// }
    ///
    /// // These bytes encode a `Packet`.
    /// let mut bytes = &mut [4, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let packet = Packet::<[u8]>::mut_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    ///
    /// {
    ///     // Attempt to split `packet` at `length`.
    ///     let split = packet.split_at_mut(packet.length as usize).unwrap();
    ///
    ///     // Use the `IntoBytes` bound on `Packet` to prove that it's okay to
    ///     // return concurrent references to `packet` and `rest`.
    ///     let (packet, rest) = split.via_into_bytes();
    ///
    ///     assert_eq!(packet.length, 4);
    ///     assert_eq!(packet.body, [1, 2, 3, 4]);
    ///     assert_eq!(rest, [5, 6, 7, 8, 9]);
    ///
    ///     rest.fill(0);
    /// }
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 0, 0, 0, 0, 0]);
    /// ```
    ///
    /// # Code Generation
    ///
    /// See [`Split::via_immutable`](#method.split_via_immutable.codegen).
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn via_into_bytes(self) -> (&'a mut T, &'a mut [T::Elem])
    where
        T: IntoBytes,
    {
        let (l, r) = self.into_ptr().via_into_bytes();
        (l.as_mut(), r.as_mut())
    }

    /// Produces the split parts of `self`, using [`Unaligned`] to ensure that
    /// it is sound to have concurrent references to both parts.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::{SplitAt, FromBytes};
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, FromBytes, KnownLayout, IntoBytes, Unaligned)]
    /// #[repr(C)]
    /// struct Packet<B: ?Sized> {
    ///     length: u8,
    ///     body: B,
    /// }
    ///
    /// // These bytes encode a `Packet`.
    /// let mut bytes = &mut [4, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let packet = Packet::<[u8]>::mut_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    ///
    /// {
    ///     // Attempt to split `packet` at `length`.
    ///     let split = packet.split_at_mut(packet.length as usize).unwrap();
    ///
    ///     // Use the `Unaligned` bound on `Packet` to prove that it's okay to
    ///     // return concurrent references to `packet` and `rest`.
    ///     let (packet, rest) = split.via_unaligned();
    ///
    ///     assert_eq!(packet.length, 4);
    ///     assert_eq!(packet.body, [1, 2, 3, 4]);
    ///     assert_eq!(rest, [5, 6, 7, 8, 9]);
    ///
    ///     rest.fill(0);
    /// }
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 0, 0, 0, 0, 0]);
    /// ```
    ///
    /// # Code Generation
    ///
    /// See [`Split::via_immutable`](#method.split_via_immutable.codegen).
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn via_unaligned(self) -> (&'a mut T, &'a mut [T::Elem])
    where
        T: Unaligned,
    {
        let (l, r) = self.into_ptr().via_unaligned();
        (l.as_mut(), r.as_mut())
    }

    /// Produces the split parts of `self`, using a dynamic check to ensure that
    /// it is sound to have concurrent references to both parts. You should
    /// prefer using [`Self::via_into_bytes`] or [`Self::via_unaligned`], which
    /// have no runtime cost.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::{SplitAt, FromBytes};
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, FromBytes, KnownLayout, IntoBytes, Debug)]
    /// #[repr(C)]
    /// struct Packet<B: ?Sized> {
    ///     length: u8,
    ///     body: B,
    /// }
    ///
    /// // These bytes encode a `Packet`.
    /// let mut bytes = &mut [4, 1, 2, 3, 4, 5, 6, 7, 8, 9][..];
    ///
    /// let packet = Packet::<[u8]>::mut_from_bytes(bytes).unwrap();
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    ///
    /// {
    ///     // Attempt to split `packet` at `length`.
    ///     let split = packet.split_at_mut(packet.length as usize).unwrap();
    ///
    ///     // Use a dynamic check to prove that it's okay to return concurrent
    ///     // references to `packet` and `rest`.
    ///     let (packet, rest) = split.via_runtime_check().unwrap();
    ///
    ///     assert_eq!(packet.length, 4);
    ///     assert_eq!(packet.body, [1, 2, 3, 4]);
    ///     assert_eq!(rest, [5, 6, 7, 8, 9]);
    ///
    ///     rest.fill(0);
    /// }
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 0, 0, 0, 0, 0]);
    /// ```
    ///
    /// # Code Generation
    ///
    /// See [`Split::via_runtime_check`](#method.split_via_runtime_check.codegen).
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn via_runtime_check(self) -> Result<(&'a mut T, &'a mut [T::Elem]), Self> {
        match self.into_ptr().via_runtime_check() {
            Ok((l, r)) => Ok((l.as_mut(), r.as_mut())),
            Err(s) => Err(s.into_mut()),
        }
    }

    /// Unsafely produces the split parts of `self`.
    ///
    /// # Safety
    ///
    /// The trailing padding bytes of the left portion must not overlap the
    /// right portion. For some dynamically sized types, the padding that
    /// appears after the trailing slice field [is a dynamic function of the
    /// trailing slice length](KnownLayout#slice-dst-layout). Thus, for some
    /// types, this condition is dependent on the length of the left portion.
    ///
    /// # Code Generation
    ///
    /// See [`Split::via_unchecked`](#method.split_via_unchecked.codegen).
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub unsafe fn via_unchecked(self) -> (&'a mut T, &'a mut [T::Elem]) {
        // SAFETY: The aliasing of `self.into_ptr()` is `Exclusive`, and the
        // caller has promised that the left and right portions of `self` split
        // at `l_len` do not overlap.
        let (l, r) = unsafe { self.into_ptr().via_unchecked() };
        (l.as_mut(), r.as_mut())
    }
}

impl<'a, T, I> Split<Ptr<'a, T, I>>
where
    T: ?Sized + SplitAt,
    I: Invariants<Alignment = Aligned, Validity = Valid>,
{
    fn into_ref(self) -> Split<&'a T>
    where
        I: Invariants<Aliasing = Shared>,
    {
        // SAFETY: `self.source.as_ref()` points to exactly the same referent as
        // `self.source` and thus maintains the invariants of `self` with
        // respect to `l_len`.
        unsafe { Split::new(self.source.as_ref(), self.l_len) }
    }

    fn into_mut(self) -> Split<&'a mut T>
    where
        I: Invariants<Aliasing = Exclusive>,
    {
        // SAFETY: `self.source.as_mut()` points to exactly the same referent as
        // `self.source` and thus maintains the invariants of `self` with
        // respect to `l_len`.
        unsafe { Split::new(self.source.unify_invariants().as_mut(), self.l_len) }
    }

    /// Produces the length of `self`'s left part.
    #[inline(always)]
    fn l_len(&self) -> MetadataOf<T> {
        // SAFETY: By invariant on `Split`, `self.l_len` is not greater than the
        // length of `self.source`.
        unsafe { MetadataOf::<T>::new_unchecked(self.l_len) }
    }

    /// Produces the split parts of `self`, using [`Immutable`] to ensure that
    /// it is sound to have concurrent references to both parts.
    #[inline(always)]
    fn via_immutable(self) -> (Ptr<'a, T, I>, Ptr<'a, [T::Elem], I>)
    where
        T: Immutable,
        I: Invariants<Aliasing = Shared>,
    {
        // SAFETY: `Aliasing = Shared` and `T: Immutable`.
        unsafe { self.via_unchecked() }
    }

    /// Produces the split parts of `self`, using [`IntoBytes`] to ensure that
    /// it is sound to have concurrent references to both parts.
    #[inline(always)]
    fn via_into_bytes(self) -> (Ptr<'a, T, I>, Ptr<'a, [T::Elem], I>)
    where
        T: IntoBytes,
    {
        // SAFETY: By `T: IntoBytes`, `T` has no padding for any length.
        // Consequently, `T` can be split into non-overlapping parts at any
        // index.
        unsafe { self.via_unchecked() }
    }

    /// Produces the split parts of `self`, using [`Unaligned`] to ensure that
    /// it is sound to have concurrent references to both parts.
    #[inline(always)]
    fn via_unaligned(self) -> (Ptr<'a, T, I>, Ptr<'a, [T::Elem], I>)
    where
        T: Unaligned,
    {
        // SAFETY: By `T: SplitAt + Unaligned`, `T` is either a slice or a
        // `repr(C)` or `repr(transparent)` slice DST that is well-aligned at
        // any address and length. If `T` is a slice DST with alignment 1,
        // `repr(C)` or `repr(transparent)` ensures that no padding is placed
        // after the final element of the trailing slice. Consequently, `T` can
        // be split into strictly non-overlapping parts any any index.
        unsafe { self.via_unchecked() }
    }

    /// Produces the split parts of `self`, using a dynamic check to ensure that
    /// it is sound to have concurrent references to both parts. You should
    /// prefer using [`Self::via_immutable`], [`Self::via_into_bytes`], or
    /// [`Self::via_unaligned`], which have no runtime cost.
    #[inline(always)]
    fn via_runtime_check(self) -> Result<(Ptr<'a, T, I>, Ptr<'a, [T::Elem], I>), Self> {
        let l_len = self.l_len();
        // FIXME(#1290): Once we require `KnownLayout` on all fields, add an
        // `IS_IMMUTABLE` associated const, and add `T::IS_IMMUTABLE ||` to the
        // below check.
        if l_len.padding_needed_for() == 0 {
            // SAFETY: By `T: SplitAt`, `T` is either `[T]`, or a `repr(C)` or
            // `repr(transparent)` slice DST, for which the trailing padding
            // needed to accommodate `l_len` trailing elements is
            // `l_len.padding_needed_for()`. If no trailing padding is required,
            // the left and right parts are strictly non-overlapping.
            Ok(unsafe { self.via_unchecked() })
        } else {
            Err(self)
        }
    }

    /// Unsafely produces the split parts of `self`.
    ///
    /// # Safety
    ///
    /// The caller promises that if `I::Aliasing` is [`Exclusive`] or `T`
    /// permits interior mutation, then `l_len.padding_needed_for() == 0`.
    #[inline(always)]
    unsafe fn via_unchecked(self) -> (Ptr<'a, T, I>, Ptr<'a, [T::Elem], I>) {
        let l_len = self.l_len();
        let inner = self.source.as_inner();

        // SAFETY: By invariant on `Self::l_len`, `l_len` is not greater than
        // the length of `inner`'s trailing slice.
        let (left, right) = unsafe { inner.split_at_unchecked(l_len) };

        // Lemma 0: `left` and `right` conform to the aliasing invariant
        // `I::Aliasing`. Proof: If `I::Aliasing` is `Exclusive` or `T` permits
        // interior mutation, the caller promises that `l_len.padding_needed_for()
        // == 0`. Consequently, by post-condition on `PtrInner::split_at_unchecked`,
        // there is no trailing padding after `left`'s final element that would
        // overlap into `right`. If `I::Aliasing` is shared and `T` forbids interior
        // mutation, then overlap between their referents is permissible.

        // SAFETY:
        // 0. `left` conforms to the aliasing invariant of `I::Aliasing`, by Lemma 0.
        // 1. `left` conforms to the alignment invariant of `I::Alignment, because
        //    the referents of `left` and `Self` have the same address and type
        //    (and, thus, alignment requirement).
        // 2. `left` conforms to the validity invariant of `I::Validity`, neither
        //    the type nor bytes of `left`'s referent have been changed.
        let left = unsafe { Ptr::from_inner(left) };

        // SAFETY:
        // 0. `right` conforms to the aliasing invariant of `I::Aliasing`, by Lemma
        //    0.
        // 1. `right` conforms to the alignment invariant of `I::Alignment, because
        //    if `ptr` with `I::Alignment = Aligned`, then by invariant on `T:
        //    SplitAt`, the trailing slice of `ptr` (from which `right` is derived)
        //    will also be well-aligned.
        // 2. `right` conforms to the validity invariant of `I::Validity`,
        //    because `right: [T::Elem]` is derived from the trailing slice of
        //    `ptr`, which, by contract on `T: SplitAt::Elem`, has type
        //    `[T::Elem]`. The `left` part cannot be used to invalidate `right`,
        //    because the caller promises that if `I::Aliasing` is `Exclusive`
        //    or `T` permits interior mutation, then `l_len.padding_needed_for()
        //    == 0` and thus the parts will be non-overlapping.
        let right = unsafe { Ptr::from_inner(right) };

        (left, right)
    }
}

#[cfg(test)]
mod tests {
    #[cfg(feature = "derive")]
    #[test]
    fn test_split_at() {
        use crate::{FromBytes, Immutable, IntoBytes, KnownLayout, SplitAt};

        #[derive(FromBytes, KnownLayout, SplitAt, IntoBytes, Immutable, Debug)]
        #[repr(C)]
        struct SliceDst<const OFFSET: usize> {
            prefix: [u8; OFFSET],
            trailing: [u8],
        }

        #[allow(clippy::as_conversions)]
        fn test_split_at<const OFFSET: usize, const BUFFER_SIZE: usize>() {
            // Test `split_at`
            let n: usize = BUFFER_SIZE - OFFSET;
            let arr = [1; BUFFER_SIZE];
            let dst = SliceDst::<OFFSET>::ref_from_bytes(&arr[..]).unwrap();
            for i in 0..=n {
                let (l, r) = dst.split_at(i).unwrap().via_runtime_check().unwrap();
                let l_sum: u8 = l.trailing.iter().sum();
                let r_sum: u8 = r.iter().sum();
                assert_eq!(l_sum, i as u8);
                assert_eq!(r_sum, (n - i) as u8);
                assert_eq!(l_sum + r_sum, n as u8);
            }

            // Test `split_at_mut`
            let n: usize = BUFFER_SIZE - OFFSET;
            let mut arr = [1; BUFFER_SIZE];
            let dst = SliceDst::<OFFSET>::mut_from_bytes(&mut arr[..]).unwrap();
            for i in 0..=n {
                let (l, r) = dst.split_at_mut(i).unwrap().via_runtime_check().unwrap();
                let l_sum: u8 = l.trailing.iter().sum();
                let r_sum: u8 = r.iter().sum();
                assert_eq!(l_sum, i as u8);
                assert_eq!(r_sum, (n - i) as u8);
                assert_eq!(l_sum + r_sum, n as u8);
            }
        }

        test_split_at::<0, 16>();
        test_split_at::<1, 17>();
        test_split_at::<2, 18>();
    }

    #[cfg(feature = "derive")]
    #[test]
    #[allow(clippy::as_conversions)]
    fn test_split_at_overlapping() {
        use crate::{FromBytes, Immutable, IntoBytes, KnownLayout, SplitAt};

        #[derive(FromBytes, KnownLayout, SplitAt, Immutable)]
        #[repr(C, align(2))]
        struct SliceDst {
            prefix: u8,
            trailing: [u8],
        }

        const N: usize = 16;

        let arr = [1u16; N];
        let dst = SliceDst::ref_from_bytes(arr.as_bytes()).unwrap();

        for i in 0..N {
            let split = dst.split_at(i).unwrap().via_runtime_check();
            if i % 2 == 1 {
                assert!(split.is_ok());
            } else {
                assert!(split.is_err());
            }
        }
    }
    #[test]
    fn test_split_at_unchecked() {
        use crate::SplitAt;
        let mut arr = [1, 2, 3, 4];
        let slice = &arr[..];
        // SAFETY: 2 <= arr.len() (4)
        let split = unsafe { SplitAt::split_at_unchecked(slice, 2) };
        // SAFETY: SplitAt::split_at_unchecked guarantees that the split is valid.
        let (l, r) = unsafe { split.via_unchecked() };
        assert_eq!(l, &[1, 2]);
        assert_eq!(r, &[3, 4]);

        let slice_mut = &mut arr[..];
        // SAFETY: 2 <= arr.len() (4)
        let split = unsafe { SplitAt::split_at_mut_unchecked(slice_mut, 2) };
        // SAFETY: SplitAt::split_at_mut_unchecked guarantees that the split is valid.
        let (l, r) = unsafe { split.via_unchecked() };
        assert_eq!(l, &mut [1, 2]);
        assert_eq!(r, &mut [3, 4]);
    }

    #[test]
    fn test_split_at_via_methods() {
        use crate::{FromBytes, Immutable, IntoBytes, KnownLayout, SplitAt};
        #[derive(FromBytes, KnownLayout, SplitAt, IntoBytes, Immutable, Debug)]
        #[repr(C)]
        struct Packet {
            length: u8,
            body: [u8],
        }

        let arr = [1, 2, 3, 4];
        let packet = Packet::ref_from_bytes(&arr[..]).unwrap();

        let split1 = packet.split_at(2).unwrap();
        let (l, r) = split1.via_immutable();
        assert_eq!(l.length, 1);
        assert_eq!(r, &[4]);

        let split2 = packet.split_at(2).unwrap();
        let (l, r) = split2.via_into_bytes();
        assert_eq!(l.length, 1);
        assert_eq!(r, &[4]);
    }
    #[test]
    fn test_split_at_via_unaligned() {
        use crate::{FromBytes, Immutable, IntoBytes, KnownLayout, SplitAt, Unaligned};
        #[derive(FromBytes, KnownLayout, SplitAt, IntoBytes, Immutable, Unaligned)]
        #[repr(C)]
        struct Packet {
            length: u8,
            body: [u8],
        }

        let arr = [1, 2, 3, 4];
        let packet = Packet::ref_from_bytes(&arr[..]).unwrap();

        let split = packet.split_at(2).unwrap();
        let (l, r) = split.via_unaligned();
        assert_eq!(l.length, 1);
        assert_eq!(r, &[4]);
    }
}
