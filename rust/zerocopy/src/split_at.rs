// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT
//
// Copyright 2025 The Fuchsia Authors
//
// Licensed under the 2-Clause BSD License <LICENSE-BSD or
// https://opensource.org/license/bsd-2-clause>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

use super::*;
use crate::pointer::invariant::{Aligned, Exclusive, Invariants, Safe, Shared};

/// Types that can be split in two.
///
/// This trait generalizes Rust's existing support for splitting slices to
/// support slices and slice-based dynamically-sized types ("slice DSTs").
///
/// For a `repr(C, packed)` or `repr(C, packed(N))` struct, the derive wraps
/// the trailing field's [`Elem`](Self::Elem) in [`Unalign`]. The returned
/// slice can therefore be accessed even when packing misaligns the original
/// elements. Each packed struct in the trailing-field chain adds one wrapper;
/// unpacked structs and [`ManuallyDrop`] forward their trailing type's `Elem`.
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
/// A packed struct exposes its trailing elements through `Unalign`:
///
/// ```
/// use zerocopy::{FromBytes, SplitAt, Unalign};
/// # use zerocopy_derive::*;
///
/// #[derive(FromBytes, IntoBytes, KnownLayout, SplitAt)]
/// #[repr(C, packed)]
/// struct Packet {
///     tag: u8,
///     words: [u16],
/// }
///
/// let mut bytes = [0u8; 5];
/// let packet = Packet::mut_from_bytes(&mut bytes[..]).unwrap();
/// let (left, right): (&mut Packet, &mut [Unalign<u16>]) =
///     packet.split_at_mut(1).unwrap().via_into_bytes();
/// left.tag = 1;
/// right[0] = Unalign::new(0x0202);
/// assert_eq!(bytes, [1, 0, 0, 2, 2]);
/// ```
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
// `Self` is a slice, a `repr(C)` (possibly packed) or `repr(transparent)` slice
// DST, or `ManuallyDrop<T>` for some `T: SplitAt`.
//
// `Self::Elem` has the same size, bit validity, and `UnsafeCell` coverage as
// the actual trailing element type. Access through either representation
// preserves the other's validity, including writes through mutable references
// and interior mutation through shared references. For every aligned `Self`,
// its trailing slice's address is aligned for `Self::Elem`.
pub unsafe trait SplitAt: KnownLayout<PointerMetadata = usize> {
    /// The element type exposed by the split's returned slice.
    ///
    /// For `[T]`, this is `T`. For a struct, this is the trailing field's `Elem`,
    /// wrapped in [`Unalign`] if the struct is packed. Nested packed structs
    /// produce nested `Unalign` wrappers.
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
    /// trailing slice.
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

// SAFETY: `[T]` exposes its own element type, preserving size, validity, and
// `UnsafeCell` coverage. An aligned slice is aligned for its elements.
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

// SAFETY: `ManuallyDrop<T>` has the same layout and bit validity as `T` [1].
// Its `KnownLayout` implementation preserves `T`'s metadata and trailing-slice
// layout. Forwarding `T::Elem` therefore preserves its size, validity, and
// alignment guarantees. Its `UnsafeCell` coverage also matches `T`'s, as
// established for `ManuallyDrop` in `impls.rs`; accesses through the forwarded
// element representation preserve validity in both directions.
//
// [1] Per https://doc.rust-lang.org/1.93.0/std/mem/struct.ManuallyDrop.html:
//
//   `ManuallyDrop<T>` is guaranteed to have the same layout and bit validity as
//   `T`
unsafe impl<T: ?Sized + SplitAt> SplitAt for ManuallyDrop<T> {
    type Elem = T::Elem;

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
/// not overlap. Use [`Self::via_into_bytes`] or
/// [`Self::via_no_dynamic_padding`] to establish this without a runtime check,
/// or [`Self::via_runtime_check`] to check a particular split. Shared references
/// to an [`Immutable`] type may overlap; use [`Self::via_immutable`] in that
/// case.
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
    fn into_ptr(self) -> Split<Ptr<'a, T, (Shared, Aligned, Safe)>> {
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

    /// Produces the split parts of `self` using [`Self::via_no_dynamic_padding`].
    #[deprecated(note = "use `Split::via_no_dynamic_padding` instead")]
    #[doc(hidden)]
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn via_unaligned(self) -> (&'a T, &'a [T::Elem])
    where
        T: Unaligned,
    {
        self.via_no_dynamic_padding()
    }

    /// Produces the split parts of `self` using a compile-time assertion to
    /// ensure that it is unconditionally sound to have concurrent references to
    /// both parts.
    ///
    /// When possible, you should prefer using [`Self::via_into_bytes`], which
    /// ensures this safety property through regular trait bounds.
    ///
    /// # Examples
    ///
    /// In the below example, a single byte of padding exists (on most
    /// platforms) between `Packet<[u16]>`'s `length` and `body` fields,
    /// precluding the use of [`Self::via_into_bytes`].
    ///
    /// ```
    /// use zerocopy::SplitAt;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, KnownLayout)]
    /// #[repr(C)]
    /// struct Packet<B: ?Sized> {
    ///     length: u8,
    ///     body: B,
    /// }
    ///
    /// let packet: &Packet<[u16]> = &Packet { length: 4, body: [1u16, 2, 3, 4, 5, 6, 7, 8, 9] };
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    ///
    /// if let Some(split) = packet.split_at(packet.length as usize) {
    ///     // Every body length requires no trailing padding.
    ///     let (packet, rest) = split.via_no_dynamic_padding();
    ///     assert_eq!(packet.length, 4);
    ///     assert_eq!(packet.body, [1, 2, 3, 4]);
    ///     assert_eq!(rest, [5, 6, 7, 8, 9]);
    /// } else {
    ///     unreachable!("The packet's length field is within its body");
    /// }
    /// ```
    ///
    /// # Compile-Time Assertions
    ///
    /// This method rejects at compile-time any type that admits dynamic
    /// trailing padding. In the below example, some `split_at` indices would
    /// produce left parts whose trailing padding overlaps with the right part:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::SplitAt;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, KnownLayout)]
    /// #[repr(C, align(2))]
    /// struct Packet<B: ?Sized> {
    ///     length: u16,
    ///     body: B,
    /// }
    ///
    /// let packet: &Packet<[u8]> = &Packet { length: 4, body: [1u8, 2, 3, 4, 5, 6, 7, 8, 9] };
    ///
    /// if let Some(split) = packet.split_at(packet.length as usize) {
    ///     let _ = split.via_no_dynamic_padding(); // ⚠ Compile Error!
    /// } else {
    ///     unreachable!("The packet's length field is within its body");
    /// }
    /// ```
    ///
    /// If you need to split such types, use [`Self::via_runtime_check`].
    ///
    #[doc = codegen_header!("h5", "split_via_no_dynamic_padding")]
    ///
    /// See [`Split::via_immutable`](#method.split_via_immutable.codegen).
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn via_no_dynamic_padding(self) -> (&'a T, &'a [T::Elem]) {
        let (l, r) = self.into_ptr().via_no_dynamic_padding();
        (l.as_ref(), r.as_ref())
    }

    /// Produces the split parts of `self`, using a dynamic check to ensure that
    /// it is sound to have concurrent references to both parts. You should
    /// prefer using [`Self::via_immutable`], [`Self::via_into_bytes`], or
    /// [`Self::via_no_dynamic_padding`], which perform no runtime check.
    ///
    /// Note that this check is overly conservative if `T` is [`Immutable`]; for
    /// some types, this check will reject some splits which
    /// [`Self::via_immutable`] will accept.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::SplitAt;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, KnownLayout)]
    /// #[repr(C, align(2))]
    /// struct Packet<B: ?Sized> {
    ///     length: u16,
    ///     body: B,
    /// }
    ///
    /// let packet: &Packet<[u8]> = &Packet { length: 4, body: [1u8, 2, 3, 4, 5, 6, 7, 8, 9] };
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    ///
    /// // The two-byte prefix and four-byte body need no trailing padding.
    /// if let Some(split) = packet.split_at(packet.length as usize) {
    ///     if let Ok((packet, rest)) = split.via_runtime_check() {
    ///         assert_eq!(packet.length, 4);
    ///         assert_eq!(packet.body, [1, 2, 3, 4]);
    ///         assert_eq!(rest, [5, 6, 7, 8, 9]);
    ///     } else {
    ///         unreachable!("A four-byte body requires no trailing padding");
    ///     }
    /// } else {
    ///     unreachable!("The packet's length field is within its body");
    /// }
    ///
    /// // A three-byte body needs a padding byte that would overlap `rest`.
    /// if let Some(split) = packet.split_at((packet.length - 1) as usize) {
    ///     assert!(split.via_runtime_check().is_err());
    /// } else {
    ///     unreachable!("One less than the packet's length is within its body");
    /// }
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
    fn into_ptr(self) -> Split<Ptr<'a, T, (Exclusive, Aligned, Safe)>> {
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

    /// Produces the split parts of `self` using [`Self::via_no_dynamic_padding`].
    #[deprecated(note = "use `Split::via_no_dynamic_padding` instead")]
    #[doc(hidden)]
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn via_unaligned(self) -> (&'a mut T, &'a mut [T::Elem])
    where
        T: Unaligned,
    {
        self.via_no_dynamic_padding()
    }

    /// Produces the split parts of `self` using a compile-time assertion to
    /// ensure that it is unconditionally sound to have concurrent references to
    /// both parts.
    ///
    /// When possible, you should prefer using [`Self::via_into_bytes`], which
    /// ensures this safety property through regular trait bounds.
    ///
    /// # Examples
    ///
    /// In the below example, a single byte of padding exists (on most
    /// platforms) between `Packet<[u16]>`'s `length` and `body` fields,
    /// precluding the use of [`Self::via_into_bytes`].
    ///
    /// ```
    /// use zerocopy::SplitAt;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, KnownLayout)]
    /// #[repr(C)]
    /// struct Packet<B: ?Sized> {
    ///     length: u8,
    ///     body: B,
    /// }
    ///
    /// let packet: &mut Packet<[u16]> = &mut Packet { length: 4, body: [1u16, 2, 3, 4, 5, 6, 7, 8, 9] };
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    ///
    /// // Every body length requires no trailing padding.
    /// if let Some(split) = packet.split_at_mut(packet.length as usize) {
    ///     let (packet, rest) = split.via_no_dynamic_padding();
    ///     assert_eq!(packet.length, 4);
    ///     assert_eq!(packet.body, [1, 2, 3, 4]);
    ///     assert_eq!(rest, [5, 6, 7, 8, 9]);
    ///     rest.fill(0);
    /// } else {
    ///     unreachable!("The packet's length field is within its body");
    /// }
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 0, 0, 0, 0, 0]);
    /// ```
    ///
    /// # Compile-Time Assertions
    ///
    /// This method rejects at compile-time any type that admits dynamic
    /// trailing padding. In the below example, some `split_at` indices would
    /// produce left parts whose trailing padding overlaps with the right part:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::SplitAt;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, KnownLayout)]
    /// #[repr(C, align(2))]
    /// struct Packet<B: ?Sized> {
    ///     length: u16,
    ///     body: B,
    /// }
    ///
    /// let packet: &mut Packet<[u8]> = &mut Packet { length: 4, body: [1u8, 2, 3, 4, 5, 6, 7, 8, 9] };
    ///
    /// if let Some(split) = packet.split_at_mut(packet.length as usize) {
    ///     let _ = split.via_no_dynamic_padding(); // ⚠ Compile Error!
    /// } else {
    ///     unreachable!("The packet's length field is within its body");
    /// }
    /// ```
    ///
    /// If you need to split such types, use [`Self::via_runtime_check`].
    ///
    /// # Code Generation
    ///
    /// See [`Split::via_immutable`](#method.split_via_immutable.codegen).
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn via_no_dynamic_padding(self) -> (&'a mut T, &'a mut [T::Elem]) {
        let (l, r) = self.into_ptr().via_no_dynamic_padding();
        (l.as_mut(), r.as_mut())
    }

    /// Produces the split parts of `self`, using a dynamic check to ensure that
    /// it is sound to have concurrent references to both parts. You should
    /// prefer using [`Self::via_into_bytes`] or
    /// [`Self::via_no_dynamic_padding`], which perform no runtime check.
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::SplitAt;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(SplitAt, KnownLayout)]
    /// #[repr(C, align(2))]
    /// struct Packet<B: ?Sized> {
    ///     length: u16,
    ///     body: B,
    /// }
    ///
    /// let packet: &mut Packet<[u8]> = &mut Packet { length: 4, body: [1u8, 2, 3, 4, 5, 6, 7, 8, 9] };
    ///
    /// assert_eq!(packet.length, 4);
    /// assert_eq!(packet.body, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    ///
    /// // The two-byte prefix and four-byte body need no trailing padding.
    /// if let Some(split) = packet.split_at_mut(packet.length as usize) {
    ///     if let Ok((packet, rest)) = split.via_runtime_check() {
    ///         assert_eq!(packet.length, 4);
    ///         assert_eq!(packet.body, [1, 2, 3, 4]);
    ///         assert_eq!(rest, [5, 6, 7, 8, 9]);
    ///         rest.fill(0);
    ///     } else {
    ///         unreachable!("A four-byte body requires no trailing padding");
    ///     }
    /// } else {
    ///     unreachable!("The packet's length field is within its body");
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
    I: Invariants<Alignment = Aligned, Validity = Safe>,
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

    /// Produces the split parts of `self` after the conservative layout
    /// predicate establishes that `T` never requires dynamic trailing padding.
    #[inline(always)]
    fn via_no_dynamic_padding(self) -> (Ptr<'a, T, I>, Ptr<'a, [T::Elem], I>) {
        static_assert!(
            T: ?Sized + KnownLayout => !T::LAYOUT.requires_dynamic_padding(),
            "`Split::via_no_dynamic_padding` cannot be used with a type whose layout may require dynamic trailing padding; use `Split::via_runtime_check` instead"
        );

        // SAFETY: The assertion establishes that `requires_dynamic_padding()`
        // is false. By that method's guarantee, every valid metadata—including
        // `self.l_len()`, which is valid by `Split`'s invariant—describes an
        // object with no trailing padding. By the postcondition of
        // `PtrInner::split_at_unchecked`, the two returned referents are then
        // contiguous and non-overlapping. Since the source already conforms to
        // `I::Aliasing`, splitting it into disjoint byte ranges preserves that
        // invariant: under `Exclusive`, neither result aliases or accesses the
        // other's bytes; under `Shared`, interior mutation through one result
        // cannot mutate bytes pointed to by the other. This satisfies Rust's
        // reference aliasing rules [1].
        //
        // [1] Per https://doc.rust-lang.org/1.93.1/std/ptr/index.html#pointer-to-reference-conversion:
        //
        //   When creating a mutable reference, then while this reference
        //   exists, the memory it points to must not get accessed (read or
        //   written) through any other pointer or reference not derived from
        //   this reference.
        //
        //   When creating a shared reference, then while this reference
        //   exists, the memory it points to must not get mutated (except inside
        //   `UnsafeCell`).
        unsafe { self.via_unchecked() }
    }

    /// Produces the split parts of `self`, using a dynamic check to ensure that
    /// it is sound to have concurrent references to both parts. You should
    /// prefer using [`Self::via_immutable`], [`Self::via_into_bytes`], or
    /// [`Self::via_no_dynamic_padding`], which perform no runtime check.
    #[inline(always)]
    fn via_runtime_check(self) -> Result<(Ptr<'a, T, I>, Ptr<'a, [T::Elem], I>), Self> {
        let l_len = self.l_len();
        // `l_len` witnesses an object size at most `isize::MAX`, and
        // `KnownLayout::LAYOUT` guarantees that it contains the trailing
        // slice. Thus `padding_for_elems` returns the exact trailing padding.
        let trailing_padding = crate::trailing_slice_layout::<T>().padding_for_elems(l_len.get());
        // FIXME(#1290): Once we require `KnownLayout` on all fields, add an
        // `IS_IMMUTABLE` associated const, and add `T::IS_IMMUTABLE ||` to the
        // below check.
        if trailing_padding == 0 {
            // SAFETY: As established above, `trailing_padding` is the exact
            // padding after the left part's trailing slice. If it is zero,
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
    /// permits interior mutation, then the left part has no trailing padding.
    #[inline(always)]
    unsafe fn via_unchecked(self) -> (Ptr<'a, T, I>, Ptr<'a, [T::Elem], I>) {
        let l_len = self.l_len();
        let inner = self.source.as_inner();

        // SAFETY: By invariant on `Self::l_len`, `l_len` is not greater than
        // the length of `inner`'s trailing slice.
        let (left, right) = unsafe { inner.split_at_unchecked(l_len) };

        // Lemma 0: `left` and `right` conform to the aliasing invariant
        // `I::Aliasing`. Proof: If `I::Aliasing` is `Exclusive` or `T` permits
        // interior mutation, the caller promises that the left part has no
        // trailing padding. Consequently, by the postcondition of
        // `PtrInner::split_at_unchecked`, `left` and `right` do not overlap.
        // If `I::Aliasing` is shared and `T` forbids interior mutation, then
        // overlap between their referents is permissible.

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
        // 1. `right` conforms to the alignment invariant of `I::Alignment`:
        //    if the source is aligned, `T: SplitAt` guarantees that its
        //    trailing slice's address is aligned for `T::Elem`. Advancing by
        //    whole elements preserves that alignment.
        // 2. `right` conforms to the validity invariant of `I::Validity`,
        //    because `T: SplitAt` guarantees that `T::Elem` has the size and
        //    bit validity of the original trailing element. It also guarantees
        //    compatible shared and mutable access, so writes through `right`
        //    preserve the original elements' validity and interior mutation
        //    respects the original `UnsafeCell` coverage. The left part
        //    cannot invalidate `right`: when the references are exclusive or
        //    permit interior mutation, the caller guarantees no trailing
        //    padding and hence no overlap.
        let right = unsafe { Ptr::from_inner(right) };

        (left, right)
    }
}

#[cfg(test)]
mod tests {
    use core::{cell::Cell, mem::ManuallyDrop};

    use crate::{Immutable, KnownLayout, SplitAt, Unalign, Unaligned};

    #[derive(KnownLayout, SplitAt, Immutable, Unaligned)]
    #[repr(C, packed)]
    struct Packed<T: ?Sized> {
        prefix: u8,
        tail: ManuallyDrop<T>,
    }

    #[derive(KnownLayout, SplitAt, Immutable)]
    #[repr(C, packed(2))]
    struct Packed2<T: ?Sized> {
        prefix: u8,
        tail: ManuallyDrop<T>,
    }

    #[derive(KnownLayout, SplitAt, Immutable)]
    #[repr(C)]
    struct Inner<T: ?Sized> {
        prefix: u32,
        tail: T,
    }

    #[test]
    fn test_split_at_packed() {
        use crate::{FromBytes, IntoBytes};

        #[derive(FromBytes, IntoBytes, KnownLayout, SplitAt, Immutable, Unaligned)]
        #[repr(C, packed)]
        struct Packet {
            prefix: u8,
            tail: [u16],
        }

        // The tail starts at an odd address, exercising unaligned reads and
        // writes on targets where `u16` has alignment 2.
        #[repr(align(8))]
        struct Bytes([u8; 9]);

        for i in 0..=4 {
            let mut bytes = Bytes([1; 9]);
            let packet = Packet::ref_from_bytes(&bytes.0).unwrap();
            let (left, right): (&Packet, &[Unalign<u16>]) =
                packet.split_at(i).unwrap().via_immutable();
            assert_eq!(core::mem::size_of_val(left), 1 + 2 * i);
            assert_eq!(right.len(), 4 - i);
            assert!(right.iter().all(|elem| elem.get() == 0x0101));
            assert!(left.split_at(i).is_some());
            assert!(left.split_at(i + 1).is_none());
            assert!(packet.split_at(5).is_none());

            let (_, right) = packet.split_at(i).unwrap().via_into_bytes();
            assert_eq!(right.len(), 4 - i);
            let (_, right) = packet.split_at(i).unwrap().via_no_dynamic_padding();
            assert_eq!(right.len(), 4 - i);

            let packet = Packet::mut_from_bytes(&mut bytes.0).unwrap();
            let (left, right): (&mut Packet, &mut [Unalign<u16>]) =
                packet.split_at_mut(i).unwrap().via_runtime_check().ok().unwrap();
            left.prefix = 2;
            for elem in right {
                *elem = Unalign::new(0x0202);
            }
            assert_eq!(bytes.0[0], 2);
            assert!(bytes.0[1..1 + 2 * i].iter().all(|&byte| byte == 1));
            assert!(bytes.0[1 + 2 * i..].iter().all(|&byte| byte == 2));
        }
    }

    #[test]
    fn test_split_at_nested_packed() {
        // A packing factor greater than 1 still exposes `Unalign` elements,
        // including when the factor is below the original element alignment.
        let mut words = Packed2 { prefix: 0, tail: ManuallyDrop::new([1u32, 2, 3]) };
        let dst: &mut Packed2<[u32]> = &mut words;
        let (left, right): (&mut _, &mut [Unalign<u32>]) =
            dst.split_at_mut(1).unwrap().via_no_dynamic_padding();
        left.prefix = 4;
        right[0] = Unalign::new(5);
        let dst: &Packed2<[u32]> = &words;
        let (left, right) = dst.split_at(0).unwrap().via_immutable();
        assert_eq!(left.prefix, 4);
        assert_eq!(right[0].get(), 1);
        assert_eq!(right[1].get(), 5);
        assert_eq!(right[2].get(), 3);

        let mut packet = Inner {
            prefix: 0,
            tail: Packed2 {
                prefix: 1,
                tail: ManuallyDrop::new(Packed {
                    prefix: 2,
                    tail: ManuallyDrop::new([3u16, 4, 5]),
                }),
            },
        };
        let dst: &Inner<Packed2<Packed<[u16]>>> = &packet;
        let (left, right): (&_, &[Unalign<Unalign<u16>>]) =
            dst.split_at(1).unwrap().via_immutable();
        assert_eq!(left.prefix, 0);
        assert_eq!(right.len(), 2);
        assert_eq!(right[0].get().get(), 4);
        assert_eq!(right[1].get().get(), 5);

        // Split the packed field separately: the unpacked outer struct's
        // padding does not constrain this split.
        let dst: &mut Packed2<Packed<[u16]>> = &mut packet.tail;
        let (left, right): (&mut _, &mut [Unalign<Unalign<u16>>]) =
            dst.split_at_mut(1).unwrap().via_no_dynamic_padding();
        left.prefix = 6;
        right[0] = Unalign::new(Unalign::new(7));
        let dst: &Inner<Packed2<Packed<[u16]>>> = &packet;
        let (_, right) = dst.split_at(0).unwrap().via_immutable();
        assert_eq!(right[0].get().get(), 3);
        assert_eq!(right[1].get().get(), 7);
        assert_eq!(right[2].get().get(), 5);
    }

    #[test]
    fn test_split_at_packed_preserves_inner_padding() {
        for i in 0..=4 {
            let mut packet = Packed {
                prefix: 1,
                tail: ManuallyDrop::new(Inner { prefix: 0, tail: [2u8, 3, 4, 5] }),
            };
            let dst: &Packed<Inner<[u8]>> = &packet;
            let (left, right): (&_, &[Unalign<u8>]) = dst.split_at(i).unwrap().via_immutable();
            assert_eq!(right.len(), 4 - i);
            assert_eq!(right.first().map(Unalign::get), [2, 3, 4, 5].get(i).copied());

            // The inner `u32` prefix makes the tail start at byte 5 of the
            // packed wrapper. Its size may still include inner padding.
            let has_padding = core::mem::size_of_val(left) != 5 + i;
            assert_eq!(dst.split_at(i).unwrap().via_runtime_check().is_err(), has_padding);

            let dst: &mut Packed<Inner<[u8]>> = &mut packet;
            let split = dst.split_at_mut(i).unwrap().via_runtime_check();
            assert_eq!(split.is_err(), has_padding);
            if let Ok((left, right)) = split {
                left.prefix = 6;
                for elem in right {
                    *elem = Unalign::new(7);
                }
                let dst: &Packed<Inner<[u8]>> = &packet;
                let (left, right) = dst.split_at(i).unwrap().via_immutable();
                assert_eq!(left.prefix, 6);
                assert!(right.iter().all(|elem| elem.get() == 7));
            }
        }
    }

    #[test]
    fn test_split_at_packed_interior_mutation_and_zsts() {
        let packet = Packed {
            prefix: 0,
            tail: ManuallyDrop::new([Cell::new(1u8), Cell::new(2), Cell::new(3)]),
        };
        let dst: &Packed<[Cell<u8>]> = &packet;
        let (left, right): (&_, &[Unalign<Cell<u8>>]) =
            dst.split_at(1).unwrap().via_no_dynamic_padding();
        left.tail[0].set(4);
        right[0].try_deref().unwrap().set(5);
        assert_eq!(dst.tail[0].get(), 4);
        assert_eq!(dst.tail[1].get(), 5);
        assert_eq!(dst.tail[2].get(), 3);

        let mut packet = Packed { prefix: 0, tail: ManuallyDrop::new([(); 3]) };
        for i in 0..=3 {
            let dst: &mut Packed<[()]> = &mut packet;
            let (left, right): (&mut _, &mut [Unalign<()>]) =
                dst.split_at_mut(i).unwrap().via_no_dynamic_padding();
            assert_eq!(left.tail.len(), i);
            assert_eq!(right.len(), 3 - i);
            assert_eq!(core::mem::size_of_val(left), 1);
        }
    }

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

        assert!(SliceDst::LAYOUT.requires_dynamic_padding());

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
    #[allow(deprecated)]
    fn test_split_at_via_unaligned() {
        use crate::{Immutable, KnownLayout, Split, SplitAt, Unaligned};

        fn via_unaligned<'a, T>(split: Split<&'a T>) -> (&'a T, &'a [T::Elem])
        where
            T: ?Sized + SplitAt + Unaligned,
        {
            split.via_unaligned()
        }

        fn via_unaligned_mut<'a, T>(split: Split<&'a mut T>) -> (&'a mut T, &'a mut [T::Elem])
        where
            T: ?Sized + SplitAt + Unaligned,
        {
            split.via_unaligned()
        }

        #[derive(KnownLayout, SplitAt, Immutable, Unaligned)]
        #[repr(C)]
        struct Packet<B: ?Sized> {
            prefix: [u8; 2],
            body: B,
        }

        // Exercise generic callers using the original `T: Unaligned` bound.
        assert!(!Packet::<[[u8; 2]]>::LAYOUT.requires_dynamic_padding());
        let packet = Packet { prefix: [0, 1], body: [[2, 3], [4, 5], [6, 7]] };
        let packet: &Packet<[[u8; 2]]> = &packet;

        let split = packet.split_at(2).unwrap();
        let (l, r) = via_unaligned(split);
        assert_eq!(l.body, [[2, 3], [4, 5]]);
        assert_eq!(r, &[[6, 7]]);

        let mut packet = Packet { prefix: [0, 1], body: [[2, 3], [4, 5], [6, 7]] };
        {
            let packet: &mut Packet<[[u8; 2]]> = &mut packet;
            let split = packet.split_at_mut(2).unwrap();
            let (l, r) = via_unaligned_mut(split);
            l.body[0] = [8, 9];
            r[0] = [10, 11];
        }
        assert_eq!(packet.body, [[8, 9], [4, 5], [10, 11]]);
    }

    #[test]
    fn test_split_at_via_no_dynamic_padding() {
        use core::cell::Cell;

        use crate::SplitAt;

        // `u16` does not implement `Unaligned`, but `[u16]` never requires
        // dynamic trailing padding.
        let words = [1u16, 2, 3, 4];
        let split = SplitAt::split_at(&words[..], 2).unwrap();
        let (left, right) = split.via_no_dynamic_padding();
        assert_eq!(left, [1, 2]);
        assert_eq!(right, [3, 4]);

        let mut words = [1u16, 2, 3, 4];
        let split = SplitAt::split_at_mut(&mut words[..], 2).unwrap();
        let (left, right) = split.via_no_dynamic_padding();
        left[0] = 5;
        right[0] = 6;
        assert_eq!(words, [5, 2, 6, 4]);

        // Exercise the safety-sensitive shared route with interior mutation.
        let cells = [Cell::new(1u16), Cell::new(2), Cell::new(3)];
        let split = SplitAt::split_at(&cells[..], 2).unwrap();
        let (left, right) = split.via_no_dynamic_padding();
        left[0].set(4);
        right[0].set(5);
        assert_eq!([cells[0].get(), cells[1].get(), cells[2].get()], [4, 2, 5]);
    }
}
