// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2024 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

#![allow(missing_copy_implementations, missing_debug_implementations, missing_docs)]

//! The parameterized invariants of a [`Ptr`][super::Ptr].
//!
//! Invariants are encoded as ([`Aliasing`], [`Alignment`], [`Validity`])
//! triples implementing the [`Invariants`] trait.

/// The invariants of a [`Ptr`][super::Ptr].
pub trait Invariants: Sealed {
    type Aliasing: Aliasing;
    type Alignment: Alignment;
    type Validity: Validity;
}

impl<A: Aliasing, AA: Alignment, V: Validity> Invariants for (A, AA, V) {
    type Aliasing = A;
    type Alignment = AA;
    type Validity = V;
}

/// The aliasing invariant of a [`Ptr`][super::Ptr].
///
/// All aliasing invariants must permit reading from the bytes of a pointer's
/// referent which are not covered by [`UnsafeCell`]s.
///
/// [`UnsafeCell`]: core::cell::UnsafeCell
pub trait Aliasing: Sealed {
    /// Is `Self` [`Exclusive`]?
    #[doc(hidden)]
    const IS_EXCLUSIVE: bool;
}

/// The alignment invariant of a [`Ptr`][super::Ptr].
pub trait Alignment: Sealed {
    #[doc(hidden)]
    #[must_use]
    fn read<T, I, R>(ptr: crate::Ptr<'_, T, I>) -> T
    where
        T: Copy + Read<I::Aliasing, R>,
        I: Invariants<Alignment = Self, Validity = Valid>,
        I::Aliasing: Reference;
}

/// The validity invariant of a [`Ptr`][super::Ptr].
///
/// # Safety
///
/// In this section, we will use `Ptr<T, V>` as a shorthand for `Ptr<T, I:
/// Invariants<Validity = V>>` for brevity.
///
/// Each `V: Validity` defines a set of bit values which may appear in the
/// referent of a `Ptr<T, V>`, denoted `S(T, V)`. Each `V: Validity`, in its
/// documentation, provides a definition of `S(T, V)` which must be valid for
/// all `T: ?Sized`. Any `V: Validity` must guarantee that this set is only a
/// function of the *bit validity* of the referent type, `T`, and not of any
/// other property of `T`. As a consequence, given `V: Validity`, `T`, and `U`
/// where `T` and `U` have the same bit validity, `S(V, T) = S(V, U)`.
///
/// It is guaranteed that the referent of any `ptr: Ptr<T, V>` is a member of
/// `S(T, V)`. Unsafe code must ensure that this guarantee will be upheld for
/// any existing `Ptr`s or any `Ptr`s that that code creates.
///
/// An important implication of this guarantee is that it restricts what
/// transmutes are sound, where "transmute" is used in this context to refer to
/// changing the referent type or validity invariant of a `Ptr`, as either
/// change may change the set of bit values permitted to appear in the referent.
/// In particular, the following are necessary (but not sufficient) conditions
/// in order for a transmute from `src: Ptr<T, V>` to `dst: Ptr<U, W>` to be
/// sound:
/// - If `S(T, V) = S(U, W)`, then no restrictions apply; otherwise,
/// - If `dst` permits mutation of its referent (e.g. via `Exclusive` aliasing
///   or interior mutation under `Shared` aliasing), then it must hold that
///   `S(T, V) ⊇ S(U, W)` - in other words, the transmute must not expand the
///   set of allowed referent bit patterns. A violation of this requirement
///   would permit using `dst` to write `x` where `x ∈ S(U, W)` but `x ∉ S(T,
///   V)`, which would violate the guarantee that `src`'s referent may only
///   contain values in `S(T, V)`.
/// - If the referent may be mutated without going through `dst` while `dst` is
///   live (e.g. via interior mutation on a `Shared`-aliased `Ptr` or `&`
///   reference), then it must hold that `S(T, V) ⊆ S(U, W)` - in other words,
///   the transmute must not shrink the set of allowed referent bit patterns. A
///   violation of this requirement would permit using `src` or another
///   mechanism (e.g. a `&` reference used to derive `src`) to write `x` where
///   `x ∈ S(T, V)` but `x ∉ S(U, W)`, which would violate the guarantee that
///   `dst`'s referent may only contain values in `S(U, W)`.
pub unsafe trait Validity: Sealed {
    const KIND: ValidityKind;
}

pub enum ValidityKind {
    Uninit,
    AsInitialized,
    Initialized,
    Valid,
}

/// An [`Aliasing`] invariant which is either [`Shared`] or [`Exclusive`].
///
/// # Safety
///
/// Given `A: Reference`, callers may assume that either `A = Shared` or `A =
/// Exclusive`.
pub trait Reference: Aliasing + Sealed {}

/// The `Ptr<'a, T>` adheres to the aliasing rules of a `&'a T`.
///
/// The referent of a shared-aliased `Ptr` may be concurrently referenced by any
/// number of shared-aliased `Ptr` or `&T` references, or by any number of
/// `Ptr<U>` or `&U` references as permitted by `T`'s library safety invariants,
/// and may not be concurrently referenced by any exclusively-aliased `Ptr`s or
/// `&mut` references. The referent must not be mutated, except via
/// [`UnsafeCell`]s, and only when permitted by `T`'s library safety invariants.
///
/// [`UnsafeCell`]: core::cell::UnsafeCell
pub enum Shared {}
impl Aliasing for Shared {
    const IS_EXCLUSIVE: bool = false;
}
impl Reference for Shared {}

/// The `Ptr<'a, T>` adheres to the aliasing rules of a `&'a mut T`.
///
/// The referent of an exclusively-aliased `Ptr` may not be concurrently
/// referenced by any other `Ptr`s or references, and may not be accessed (read
/// or written) other than via this `Ptr`.
pub enum Exclusive {}
impl Aliasing for Exclusive {
    const IS_EXCLUSIVE: bool = true;
}
impl Reference for Exclusive {}

/// It is unknown whether the pointer is aligned.
pub enum Unaligned {}

impl Alignment for Unaligned {
    #[inline(always)]
    fn read<T, I, R>(ptr: crate::Ptr<'_, T, I>) -> T
    where
        T: Copy + Read<I::Aliasing, R>,
        I: Invariants<Alignment = Self, Validity = Valid>,
        I::Aliasing: Reference,
    {
        (*ptr.into_unalign().as_ref()).into_inner()
    }
}

/// The referent is aligned: for `Ptr<T>`, the referent's address is a multiple
/// of the `T`'s alignment.
pub enum Aligned {}
impl Alignment for Aligned {
    #[inline(always)]
    fn read<T, I, R>(ptr: crate::Ptr<'_, T, I>) -> T
    where
        T: Copy + Read<I::Aliasing, R>,
        I: Invariants<Alignment = Self, Validity = Valid>,
        I::Aliasing: Reference,
    {
        *ptr.as_ref()
    }
}

/// Any bit pattern is allowed in the `Ptr`'s referent, including uninitialized
/// bytes.
pub enum Uninit {}
// SAFETY: `Uninit`'s validity is well-defined for all `T: ?Sized`, and is not a
// function of any property of `T` other than its bit validity (in fact, it's
// not even a property of `T`'s bit validity, but this is more than we are
// required to uphold).
unsafe impl Validity for Uninit {
    const KIND: ValidityKind = ValidityKind::Uninit;
}

/// The byte ranges initialized in `T` are also initialized in the referent of a
/// `Ptr<T>`.
///
/// Formally: uninitialized bytes may only be present in `Ptr<T>`'s referent
/// where they are guaranteed to be present in `T`. This is a dynamic property:
/// if, at a particular byte offset, a valid enum discriminant is set, the
/// subsequent bytes may only have uninitialized bytes as specified by the
/// corresponding enum.
///
/// Formally, given `len = size_of_val_raw(ptr)`, at every byte offset, `b`, in
/// the range `[0, len)`:
/// - If, in any instance `t: T` of length `len`, the byte at offset `b` in `t`
///   is initialized, then the byte at offset `b` within `*ptr` must be
///   initialized.
/// - Let `c` be the contents of the byte range `[0, b)` in `*ptr`. Let `S` be
///   the subset of valid instances of `T` of length `len` which contain `c` in
///   the offset range `[0, b)`. If, in any instance of `t: T` in `S`, the byte
///   at offset `b` in `t` is initialized, then the byte at offset `b` in `*ptr`
///   must be initialized.
///
///   Pragmatically, this means that if `*ptr` is guaranteed to contain an enum
///   type at a particular offset, and the enum discriminant stored in `*ptr`
///   corresponds to a valid variant of that enum type, then it is guaranteed
///   that the appropriate bytes of `*ptr` are initialized as defined by that
///   variant's bit validity (although note that the variant may contain another
///   enum type, in which case the same rules apply depending on the state of
///   its discriminant, and so on recursively).
pub enum AsInitialized {}
// SAFETY: `AsInitialized`'s validity is well-defined for all `T: ?Sized`, and
// is not a function of any property of `T` other than its bit validity.
unsafe impl Validity for AsInitialized {
    const KIND: ValidityKind = ValidityKind::AsInitialized;
}

/// The byte ranges in the referent are fully initialized. In other words, if
/// the referent is `N` bytes long, then it contains a bit-valid `[u8; N]`.
pub enum Initialized {}
// SAFETY: `Initialized`'s validity is well-defined for all `T: ?Sized`, and is
// not a function of any property of `T` other than its bit validity (in fact,
// it's not even a property of `T`'s bit validity, but this is more than we are
// required to uphold).
unsafe impl Validity for Initialized {
    const KIND: ValidityKind = ValidityKind::Initialized;
}

/// The referent of a `Ptr<T>` is valid for `T`, upholding bit validity and any
/// library safety invariants.
pub enum Valid {}
// SAFETY: `Valid`'s validity is well-defined for all `T: ?Sized`, and is not a
// function of any property of `T` other than its bit validity.
unsafe impl Validity for Valid {
    const KIND: ValidityKind = ValidityKind::Valid;
}

/// # Safety
///
/// `DT: CastableFrom<ST, SV, DV>` is sound if `SV = DV = Uninit` or `SV = DV =
/// Initialized`.
pub unsafe trait CastableFrom<ST: ?Sized, SV, DV> {}

// SAFETY: `SV = DV = Uninit`.
unsafe impl<ST: ?Sized, DT: ?Sized> CastableFrom<ST, Uninit, Uninit> for DT {}
// SAFETY: `SV = DV = Initialized`.
unsafe impl<ST: ?Sized, DT: ?Sized> CastableFrom<ST, Initialized, Initialized> for DT {}

/// [`Ptr`](crate::Ptr) referents that permit unsynchronized read operations.
///
/// `T: Read<A, R>` implies that a pointer to `T` with aliasing `A` permits
/// unsynchronized read operations. This can be because `A` is [`Exclusive`] or
/// because `T` does not permit interior mutation.
///
/// # Safety
///
/// `T: Read<A, R>` if either of the following conditions holds:
/// - `A` is [`Exclusive`]
/// - `T` implements [`Immutable`](crate::Immutable)
///
/// As a consequence, if `T: Read<A, R>`, then any `Ptr<T, (A, ...)>` is
/// permitted to perform unsynchronized reads from its referent.
pub trait Read<A: Aliasing, R> {}

impl<A: Aliasing, T: ?Sized + crate::Immutable> Read<A, BecauseImmutable> for T {}
impl<T: ?Sized> Read<Exclusive, BecauseExclusive> for T {}

/// Unsynchronized reads are permitted because only one live [`Ptr`](crate::Ptr)
/// or reference may exist to the referent bytes at a time.
#[derive(Copy, Clone, Debug)]
pub enum BecauseExclusive {}

/// Unsynchronized reads are permitted because no live [`Ptr`](crate::Ptr)s or
/// references permit interior mutation.
#[derive(Copy, Clone, Debug)]
pub enum BecauseImmutable {}

use sealed::Sealed;
mod sealed {
    use super::*;

    pub trait Sealed {}

    impl Sealed for Shared {}
    impl Sealed for Exclusive {}

    impl Sealed for Unaligned {}
    impl Sealed for Aligned {}

    impl Sealed for Uninit {}
    impl Sealed for AsInitialized {}
    impl Sealed for Initialized {}
    impl Sealed for Valid {}

    impl<A: Sealed, AA: Sealed, V: Sealed> Sealed for (A, AA, V) {}

    impl Sealed for BecauseImmutable {}
    impl Sealed for BecauseExclusive {}
}
