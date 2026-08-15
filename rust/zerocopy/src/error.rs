// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2024 The Fuchsia Authors
//
// Licensed under the 2-Clause BSD License <LICENSE-BSD or
// https://opensource.org/license/bsd-2-clause>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

//! Types related to error reporting.
//!
//! ## Single failure mode errors
//!
//! Generally speaking, zerocopy's conversions may fail for one of up to three
//! reasons:
//! - [`AlignmentError`]: the conversion source was improperly aligned
//! - [`SizeError`]: the conversion source was of incorrect size
//! - [`ValidityError`]: the conversion source contained invalid data
//!
//! Methods that only have one failure mode, like
//! [`FromBytes::read_from_bytes`], return that mode's corresponding error type
//! directly.
//!
//! ## Compound errors
//!
//! Conversion methods that have either two or three possible failure modes
//! return one of these error types:
//! - [`CastError`]: the error type of reference conversions
//! - [`TryCastError`]: the error type of fallible reference conversions
//! - [`TryReadError`]: the error type of fallible read conversions
//!
//! ## [`Unaligned`] destination types
//!
//! For [`Unaligned`] destination types, alignment errors are impossible. All
//! compound error types support infallibly discarding the alignment error via
//! [`From`] so long as `Dst: Unaligned`. For example, see [`<SizeError as
//! From<ConvertError>>::from`][size-error-from].
//!
//! [size-error-from]: struct.SizeError.html#method.from-1
//!
//! ## Accessing the conversion source
//!
//! All error types provide an `into_src` method that converts the error into
//! the source value underlying the failed conversion.
//!
//! ## Display formatting
//!
//! All error types provide a `Display` implementation that produces a
//! human-readable error message. When `debug_assertions` are enabled, these
//! error messages are verbose and may include potentially sensitive
//! information, including:
//!
//! - the names of the involved types
//! - the sizes of the involved types
//! - the addresses of the involved types
//! - the contents of the involved types
//!
//! When `debug_assertions` are disabled (as is default for `release` builds),
//! such potentially sensitive information is excluded.
//!
//! In the future, we may support manually configuring this behavior. If you are
//! interested in this feature, [let us know on GitHub][issue-1457] so we know
//! to prioritize it.
//!
//! [issue-1457]: https://github.com/google/zerocopy/issues/1457
//!
//! ## Validation order
//!
//! Our conversion methods typically check alignment, then size, then bit
//! validity. However, we do not guarantee that this is always the case, and
//! this behavior may change between releases.
//!
//! ## `Send`, `Sync`, and `'static`
//!
//! Our error types are `Send`, `Sync`, and `'static` when their `Src` parameter
//! is `Send`, `Sync`, or `'static`, respectively. This can cause issues when an
//! error is sent or synchronized across threads; e.g.:
//!
//! ```compile_fail,E0515
//! use zerocopy::*;
//!
//! let result: SizeError<&[u8], u32> = std::thread::spawn(|| {
//!     let source = &mut [0u8, 1, 2][..];
//!     // Try (and fail) to read a `u32` from `source`.
//!     u32::read_from_bytes(source).unwrap_err()
//! }).join().unwrap();
//! ```
//!
//! To work around this, use [`map_src`][CastError::map_src] to convert the
//! source parameter to an unproblematic type; e.g.:
//!
//! ```
//! use zerocopy::*;
//!
//! let result: SizeError<(), u32> = std::thread::spawn(|| {
//!     let source = &mut [0u8, 1, 2][..];
//!     // Try (and fail) to read a `u32` from `source`.
//!     u32::read_from_bytes(source).unwrap_err()
//!         // Erase the error source.
//!         .map_src(drop)
//! }).join().unwrap();
//! ```
//!
//! Alternatively, use `.to_string()` to eagerly convert the error into a
//! human-readable message; e.g.:
//!
//! ```
//! use zerocopy::*;
//!
//! let result: Result<u32, String> = std::thread::spawn(|| {
//!     let source = &mut [0u8, 1, 2][..];
//!     // Try (and fail) to read a `u32` from `source`.
//!     u32::read_from_bytes(source)
//!         // Eagerly render the error message.
//!         .map_err(|err| err.to_string())
//! }).join().unwrap();
//! ```
#[cfg(not(no_zerocopy_core_error_1_81_0))]
use core::error::Error;
use core::{
    convert::Infallible,
    fmt::{self, Debug, Write},
    ops::Deref,
};
#[cfg(all(no_zerocopy_core_error_1_81_0, any(feature = "std", test)))]
use std::error::Error;

use crate::{util::SendSyncPhantomData, KnownLayout, TryFromBytes, Unaligned};
#[cfg(doc)]
use crate::{FromBytes, Ref};

/// Zerocopy's generic error type.
///
/// Generally speaking, zerocopy's conversions may fail for one of up to three
/// reasons:
/// - [`AlignmentError`]: the conversion source was improperly aligned
/// - [`SizeError`]: the conversion source was of incorrect size
/// - [`ValidityError`]: the conversion source contained invalid data
///
/// However, not all conversions produce all errors. For instance,
/// [`FromBytes::ref_from_bytes`] may fail due to alignment or size issues, but
/// not validity issues. This generic error type captures these
/// (im)possibilities via parameterization: `A` is parameterized with
/// [`AlignmentError`], `S` is parameterized with [`SizeError`], and `V` is
/// parameterized with [`Infallible`].
///
/// Zerocopy never uses this type directly in its API. Rather, we provide three
/// pre-parameterized aliases:
/// - [`CastError`]: the error type of reference conversions
/// - [`TryCastError`]: the error type of fallible reference conversions
/// - [`TryReadError`]: the error type of fallible read conversions
#[derive(PartialEq, Eq, Clone)]
pub enum ConvertError<A, S, V> {
    /// The conversion source was improperly aligned.
    Alignment(A),
    /// The conversion source was of incorrect size.
    Size(S),
    /// The conversion source contained invalid data.
    Validity(V),
}

impl<Src, Dst: ?Sized + Unaligned, S, V> From<ConvertError<AlignmentError<Src, Dst>, S, V>>
    for ConvertError<Infallible, S, V>
{
    /// Infallibly discards the alignment error from this `ConvertError` since
    /// `Dst` is unaligned.
    ///
    /// Since [`Dst: Unaligned`], it is impossible to encounter an alignment
    /// error. This method permits discarding that alignment error infallibly
    /// and replacing it with [`Infallible`].
    ///
    /// [`Dst: Unaligned`]: crate::Unaligned
    ///
    /// # Examples
    ///
    /// ```
    /// use core::convert::Infallible;
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(TryFromBytes, KnownLayout, Unaligned, Immutable)]
    /// #[repr(C, packed)]
    /// struct Bools {
    ///     one: bool,
    ///     two: bool,
    ///     many: [bool],
    /// }
    ///
    /// impl Bools {
    ///     fn parse(bytes: &[u8]) -> Result<&Bools, AlignedTryCastError<&[u8], Bools>> {
    ///         // Since `Bools: Unaligned`, we can infallibly discard
    ///         // the alignment error.
    ///         Bools::try_ref_from_bytes(bytes).map_err(Into::into)
    ///     }
    /// }
    /// ```
    #[inline]
    fn from(err: ConvertError<AlignmentError<Src, Dst>, S, V>) -> ConvertError<Infallible, S, V> {
        match err {
            ConvertError::Alignment(e) => {
                #[allow(unreachable_code)]
                return ConvertError::Alignment(Infallible::from(e));
            }
            ConvertError::Size(e) => ConvertError::Size(e),
            ConvertError::Validity(e) => ConvertError::Validity(e),
        }
    }
}

impl<A: fmt::Debug, S: fmt::Debug, V: fmt::Debug> fmt::Debug for ConvertError<A, S, V> {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Alignment(e) => f.debug_tuple("Alignment").field(e).finish(),
            Self::Size(e) => f.debug_tuple("Size").field(e).finish(),
            Self::Validity(e) => f.debug_tuple("Validity").field(e).finish(),
        }
    }
}

/// Produces a human-readable error message.
///
/// The message differs between debug and release builds. When
/// `debug_assertions` are enabled, this message is verbose and includes
/// potentially sensitive information.
impl<A: fmt::Display, S: fmt::Display, V: fmt::Display> fmt::Display for ConvertError<A, S, V> {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Alignment(e) => e.fmt(f),
            Self::Size(e) => e.fmt(f),
            Self::Validity(e) => e.fmt(f),
        }
    }
}

#[cfg(any(not(no_zerocopy_core_error_1_81_0), feature = "std", test))]
#[cfg_attr(doc_cfg, doc(cfg(all(rust = "1.81.0", feature = "std"))))]
impl<A, S, V> Error for ConvertError<A, S, V>
where
    A: fmt::Display + fmt::Debug,
    S: fmt::Display + fmt::Debug,
    V: fmt::Display + fmt::Debug,
{
}

/// The error emitted if the conversion source is improperly aligned.
pub struct AlignmentError<Src, Dst: ?Sized> {
    /// The source value involved in the conversion.
    src: Src,
    /// The inner destination type involved in the conversion.
    ///
    /// INVARIANT: An `AlignmentError` may only be constructed if `Dst`'s
    /// alignment requirement is greater than one.
    _dst: SendSyncPhantomData<Dst>,
}

impl<Src, Dst: ?Sized> AlignmentError<Src, Dst> {
    /// # Safety
    ///
    /// The caller must ensure that `Dst`'s alignment requirement is greater
    /// than one.
    pub(crate) unsafe fn new_unchecked(src: Src) -> Self {
        // INVARIANT: The caller guarantees that `Dst`'s alignment requirement
        // is greater than one.
        Self { src, _dst: SendSyncPhantomData::default() }
    }

    /// Produces the source underlying the failed conversion.
    #[inline]
    pub fn into_src(self) -> Src {
        self.src
    }

    pub(crate) fn with_src<NewSrc>(self, new_src: NewSrc) -> AlignmentError<NewSrc, Dst> {
        // INVARIANT: `with_src` doesn't change the type of `Dst`, so the
        // invariant that `Dst`'s alignment requirement is greater than one is
        // preserved.
        AlignmentError { src: new_src, _dst: SendSyncPhantomData::default() }
    }

    /// Maps the source value associated with the conversion error.
    ///
    /// This can help mitigate [issues with `Send`, `Sync` and `'static`
    /// bounds][self#send-sync-and-static].
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::*;
    ///
    /// let unaligned = Unalign::new(0u16);
    ///
    /// // Attempt to deref `unaligned`. This might fail with an alignment error.
    /// let maybe_n: Result<&u16, AlignmentError<&Unalign<u16>, u16>> = unaligned.try_deref();
    ///
    /// // Map the error's source to its address as a usize.
    /// let maybe_n: Result<&u16, AlignmentError<usize, u16>> = maybe_n.map_err(|err| {
    ///     err.map_src(|src| src as *const _ as usize)
    /// });
    /// ```
    #[inline]
    pub fn map_src<NewSrc>(self, f: impl FnOnce(Src) -> NewSrc) -> AlignmentError<NewSrc, Dst> {
        AlignmentError { src: f(self.src), _dst: SendSyncPhantomData::default() }
    }

    pub(crate) fn into<S, V>(self) -> ConvertError<Self, S, V> {
        ConvertError::Alignment(self)
    }

    /// Format extra details for a verbose, human-readable error message.
    ///
    /// This formatting may include potentially sensitive information.
    fn display_verbose_extras(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
    where
        Src: Deref,
        Dst: KnownLayout,
    {
        #[allow(clippy::as_conversions)]
        let addr = self.src.deref() as *const _ as *const ();
        let addr_align = 2usize.pow((crate::util::AsAddress::addr(addr)).trailing_zeros());

        f.write_str("\n\nSource type: ")?;
        f.write_str(core::any::type_name::<Src>())?;

        f.write_str("\nSource address: ")?;
        addr.fmt(f)?;
        f.write_str(" (a multiple of ")?;
        addr_align.fmt(f)?;
        f.write_str(")")?;

        f.write_str("\nDestination type: ")?;
        f.write_str(core::any::type_name::<Dst>())?;

        f.write_str("\nDestination alignment: ")?;
        <Dst as KnownLayout>::LAYOUT.align.get().fmt(f)?;

        Ok(())
    }
}

impl<Src: Clone, Dst: ?Sized> Clone for AlignmentError<Src, Dst> {
    #[inline]
    fn clone(&self) -> Self {
        Self { src: self.src.clone(), _dst: SendSyncPhantomData::default() }
    }
}

impl<Src: PartialEq, Dst: ?Sized> PartialEq for AlignmentError<Src, Dst> {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        self.src == other.src
    }
}

impl<Src: Eq, Dst: ?Sized> Eq for AlignmentError<Src, Dst> {}

impl<Src, Dst: ?Sized + Unaligned> From<AlignmentError<Src, Dst>> for Infallible {
    #[inline(always)]
    fn from(_: AlignmentError<Src, Dst>) -> Infallible {
        // SAFETY: `AlignmentError`s can only be constructed when `Dst`'s
        // alignment requirement is greater than one. In this block, `Dst:
        // Unaligned`, which means that its alignment requirement is equal to
        // one. Thus, it's not possible to reach here at runtime.
        unsafe { core::hint::unreachable_unchecked() }
    }
}

#[cfg(test)]
impl<Src, Dst> AlignmentError<Src, Dst> {
    // A convenience constructor so that test code doesn't need to write
    // `unsafe`.
    fn new_checked(src: Src) -> AlignmentError<Src, Dst> {
        assert_ne!(core::mem::align_of::<Dst>(), 1);
        // SAFETY: The preceding assertion guarantees that `Dst`'s alignment
        // requirement is greater than one.
        unsafe { AlignmentError::new_unchecked(src) }
    }
}

impl<Src, Dst: ?Sized> fmt::Debug for AlignmentError<Src, Dst> {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("AlignmentError").finish()
    }
}

/// Produces a human-readable error message.
///
/// The message differs between debug and release builds. When
/// `debug_assertions` are enabled, this message is verbose and includes
/// potentially sensitive information.
impl<Src, Dst: ?Sized> fmt::Display for AlignmentError<Src, Dst>
where
    Src: Deref,
    Dst: KnownLayout,
{
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("The conversion failed because the address of the source is not a multiple of the alignment of the destination type.")?;

        if cfg!(debug_assertions) {
            self.display_verbose_extras(f)
        } else {
            Ok(())
        }
    }
}

#[cfg(any(not(no_zerocopy_core_error_1_81_0), feature = "std", test))]
#[cfg_attr(doc_cfg, doc(cfg(all(rust = "1.81.0", feature = "std"))))]
impl<Src, Dst: ?Sized> Error for AlignmentError<Src, Dst>
where
    Src: Deref,
    Dst: KnownLayout,
{
}

impl<Src, Dst: ?Sized, S, V> From<AlignmentError<Src, Dst>>
    for ConvertError<AlignmentError<Src, Dst>, S, V>
{
    #[inline(always)]
    fn from(err: AlignmentError<Src, Dst>) -> Self {
        Self::Alignment(err)
    }
}

/// The error emitted if the conversion source is of incorrect size.
pub struct SizeError<Src, Dst: ?Sized> {
    /// The source value involved in the conversion.
    src: Src,
    /// The inner destination type involved in the conversion.
    _dst: SendSyncPhantomData<Dst>,
}

impl<Src, Dst: ?Sized> SizeError<Src, Dst> {
    pub(crate) fn new(src: Src) -> Self {
        Self { src, _dst: SendSyncPhantomData::default() }
    }

    /// Produces the source underlying the failed conversion.
    #[inline]
    pub fn into_src(self) -> Src {
        self.src
    }

    /// Sets the source value associated with the conversion error.
    pub(crate) fn with_src<NewSrc>(self, new_src: NewSrc) -> SizeError<NewSrc, Dst> {
        SizeError { src: new_src, _dst: SendSyncPhantomData::default() }
    }

    /// Maps the source value associated with the conversion error.
    ///
    /// This can help mitigate [issues with `Send`, `Sync` and `'static`
    /// bounds][self#send-sync-and-static].
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::*;
    ///
    /// let source: [u8; 3] = [0, 1, 2];
    ///
    /// // Try to read a `u32` from `source`. This will fail because there are insufficient
    /// // bytes in `source`.
    /// let maybe_u32: Result<u32, SizeError<&[u8], u32>> = u32::read_from_bytes(&source[..]);
    ///
    /// // Map the error's source to its size.
    /// let maybe_u32: Result<u32, SizeError<usize, u32>> = maybe_u32.map_err(|err| {
    ///     err.map_src(|src| src.len())
    /// });
    /// ```
    #[inline]
    pub fn map_src<NewSrc>(self, f: impl FnOnce(Src) -> NewSrc) -> SizeError<NewSrc, Dst> {
        SizeError { src: f(self.src), _dst: SendSyncPhantomData::default() }
    }

    /// Sets the destination type associated with the conversion error.
    pub(crate) fn with_dst<NewDst: ?Sized>(self) -> SizeError<Src, NewDst> {
        SizeError { src: self.src, _dst: SendSyncPhantomData::default() }
    }

    /// Converts the error into a general [`ConvertError`].
    pub(crate) fn into<A, V>(self) -> ConvertError<A, Self, V> {
        ConvertError::Size(self)
    }

    /// Format extra details for a verbose, human-readable error message.
    ///
    /// This formatting may include potentially sensitive information.
    fn display_verbose_extras(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
    where
        Src: Deref,
        Dst: KnownLayout,
    {
        // include the source type
        f.write_str("\nSource type: ")?;
        f.write_str(core::any::type_name::<Src>())?;

        // include the source.deref() size
        let src_size = core::mem::size_of_val(&*self.src);
        f.write_str("\nSource size: ")?;
        src_size.fmt(f)?;
        f.write_str(" byte")?;
        if src_size != 1 {
            f.write_char('s')?;
        }

        // if `Dst` is `Sized`, include the `Dst` size
        if let crate::SizeInfo::Sized { size } = Dst::LAYOUT.size_info {
            f.write_str("\nDestination size: ")?;
            size.fmt(f)?;
            f.write_str(" byte")?;
            if size != 1 {
                f.write_char('s')?;
            }
        }

        // include the destination type
        f.write_str("\nDestination type: ")?;
        f.write_str(core::any::type_name::<Dst>())?;

        Ok(())
    }
}

impl<Src: Clone, Dst: ?Sized> Clone for SizeError<Src, Dst> {
    #[inline]
    fn clone(&self) -> Self {
        Self { src: self.src.clone(), _dst: SendSyncPhantomData::default() }
    }
}

impl<Src: PartialEq, Dst: ?Sized> PartialEq for SizeError<Src, Dst> {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        self.src == other.src
    }
}

impl<Src: Eq, Dst: ?Sized> Eq for SizeError<Src, Dst> {}

impl<Src, Dst: ?Sized> fmt::Debug for SizeError<Src, Dst> {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("SizeError").finish()
    }
}

/// Produces a human-readable error message.
///
/// The message differs between debug and release builds. When
/// `debug_assertions` are enabled, this message is verbose and includes
/// potentially sensitive information.
impl<Src, Dst: ?Sized> fmt::Display for SizeError<Src, Dst>
where
    Src: Deref,
    Dst: KnownLayout,
{
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("The conversion failed because the source was incorrectly sized to complete the conversion into the destination type.")?;
        if cfg!(debug_assertions) {
            f.write_str("\n")?;
            self.display_verbose_extras(f)?;
        }
        Ok(())
    }
}

#[cfg(any(not(no_zerocopy_core_error_1_81_0), feature = "std", test))]
#[cfg_attr(doc_cfg, doc(cfg(all(rust = "1.81.0", feature = "std"))))]
impl<Src, Dst: ?Sized> Error for SizeError<Src, Dst>
where
    Src: Deref,
    Dst: KnownLayout,
{
}

impl<Src, Dst: ?Sized, A, V> From<SizeError<Src, Dst>> for ConvertError<A, SizeError<Src, Dst>, V> {
    #[inline(always)]
    fn from(err: SizeError<Src, Dst>) -> Self {
        Self::Size(err)
    }
}

/// The error emitted if the conversion source contains invalid data.
pub struct ValidityError<Src, Dst: ?Sized + TryFromBytes> {
    /// The source value involved in the conversion.
    pub(crate) src: Src,
    /// The inner destination type involved in the conversion.
    _dst: SendSyncPhantomData<Dst>,
}

impl<Src, Dst: ?Sized + TryFromBytes> ValidityError<Src, Dst> {
    pub(crate) fn new(src: Src) -> Self {
        Self { src, _dst: SendSyncPhantomData::default() }
    }

    /// Produces the source underlying the failed conversion.
    #[inline]
    pub fn into_src(self) -> Src {
        self.src
    }

    /// Maps the source value associated with the conversion error.
    ///
    /// This can help mitigate [issues with `Send`, `Sync` and `'static`
    /// bounds][self#send-sync-and-static].
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::*;
    ///
    /// let source: u8 = 42;
    ///
    /// // Try to transmute the `source` to a `bool`. This will fail.
    /// let maybe_bool: Result<bool, ValidityError<u8, bool>> = try_transmute!(source);
    ///
    /// // Drop the error's source.
    /// let maybe_bool: Result<bool, ValidityError<(), bool>> = maybe_bool.map_err(|err| {
    ///     err.map_src(drop)
    /// });
    /// ```
    #[inline]
    pub fn map_src<NewSrc>(self, f: impl FnOnce(Src) -> NewSrc) -> ValidityError<NewSrc, Dst> {
        ValidityError { src: f(self.src), _dst: SendSyncPhantomData::default() }
    }

    /// Converts the error into a general [`ConvertError`].
    pub(crate) fn into<A, S>(self) -> ConvertError<A, S, Self> {
        ConvertError::Validity(self)
    }

    /// Format extra details for a verbose, human-readable error message.
    ///
    /// This formatting may include potentially sensitive information.
    fn display_verbose_extras(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result
    where
        Dst: KnownLayout,
    {
        f.write_str("Destination type: ")?;
        f.write_str(core::any::type_name::<Dst>())?;
        Ok(())
    }
}

impl<Src: Clone, Dst: ?Sized + TryFromBytes> Clone for ValidityError<Src, Dst> {
    #[inline]
    fn clone(&self) -> Self {
        Self { src: self.src.clone(), _dst: SendSyncPhantomData::default() }
    }
}

// SAFETY: `ValidityError` contains a single `Self::Inner = Src`, and no other
// non-ZST fields. `map` passes ownership of `self`'s sole `Self::Inner` to `f`.
unsafe impl<Src, NewSrc, Dst> crate::pointer::TryWithError<NewSrc>
    for crate::ValidityError<Src, Dst>
where
    Dst: TryFromBytes + ?Sized,
{
    type Inner = Src;
    type Mapped = crate::ValidityError<NewSrc, Dst>;
    #[inline]
    fn map<F: FnOnce(Src) -> NewSrc>(self, f: F) -> Self::Mapped {
        self.map_src(f)
    }
}

impl<Src: PartialEq, Dst: ?Sized + TryFromBytes> PartialEq for ValidityError<Src, Dst> {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        self.src == other.src
    }
}

impl<Src: Eq, Dst: ?Sized + TryFromBytes> Eq for ValidityError<Src, Dst> {}

impl<Src, Dst: ?Sized + TryFromBytes> fmt::Debug for ValidityError<Src, Dst> {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ValidityError").finish()
    }
}

/// Produces a human-readable error message.
///
/// The message differs between debug and release builds. When
/// `debug_assertions` are enabled, this message is verbose and includes
/// potentially sensitive information.
impl<Src, Dst: ?Sized> fmt::Display for ValidityError<Src, Dst>
where
    Dst: KnownLayout + TryFromBytes,
{
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("The conversion failed because the source bytes are not a valid value of the destination type.")?;
        if cfg!(debug_assertions) {
            f.write_str("\n\n")?;
            self.display_verbose_extras(f)?;
        }
        Ok(())
    }
}

#[cfg(any(not(no_zerocopy_core_error_1_81_0), feature = "std", test))]
#[cfg_attr(doc_cfg, doc(cfg(all(rust = "1.81.0", feature = "std"))))]
impl<Src, Dst: ?Sized> Error for ValidityError<Src, Dst> where Dst: KnownLayout + TryFromBytes {}

impl<Src, Dst: ?Sized + TryFromBytes, A, S> From<ValidityError<Src, Dst>>
    for ConvertError<A, S, ValidityError<Src, Dst>>
{
    #[inline(always)]
    fn from(err: ValidityError<Src, Dst>) -> Self {
        Self::Validity(err)
    }
}

/// The error type of reference conversions.
///
/// Reference conversions, like [`FromBytes::ref_from_bytes`] may emit
/// [alignment](AlignmentError) and [size](SizeError) errors.
// Bounds on generic parameters are not enforced in type aliases, but they do
// appear in rustdoc.
#[allow(type_alias_bounds)]
pub type CastError<Src, Dst: ?Sized> =
    ConvertError<AlignmentError<Src, Dst>, SizeError<Src, Dst>, Infallible>;

impl<Src, Dst: ?Sized> CastError<Src, Dst> {
    /// Produces the source underlying the failed conversion.
    #[inline]
    pub fn into_src(self) -> Src {
        match self {
            Self::Alignment(e) => e.src,
            Self::Size(e) => e.src,
            Self::Validity(i) => match i {},
        }
    }

    /// Sets the source value associated with the conversion error.
    pub(crate) fn with_src<NewSrc>(self, new_src: NewSrc) -> CastError<NewSrc, Dst> {
        match self {
            Self::Alignment(e) => CastError::Alignment(e.with_src(new_src)),
            Self::Size(e) => CastError::Size(e.with_src(new_src)),
            Self::Validity(i) => match i {},
        }
    }

    /// Maps the source value associated with the conversion error.
    ///
    /// This can help mitigate [issues with `Send`, `Sync` and `'static`
    /// bounds][self#send-sync-and-static].
    ///
    /// # Examples
    ///
    /// ```
    /// use zerocopy::*;
    ///
    /// let source: [u8; 3] = [0, 1, 2];
    ///
    /// // Try to read a `u32` from `source`. This will fail because there are insufficient
    /// // bytes in `source`.
    /// let maybe_u32: Result<&u32, CastError<&[u8], u32>> = u32::ref_from_bytes(&source[..]);
    ///
    /// // Map the error's source to its size and address.
    /// let maybe_u32: Result<&u32, CastError<(usize, usize), u32>> = maybe_u32.map_err(|err| {
    ///     err.map_src(|src| (src.len(), src.as_ptr() as usize))
    /// });
    /// ```
    #[inline]
    pub fn map_src<NewSrc>(self, f: impl FnOnce(Src) -> NewSrc) -> CastError<NewSrc, Dst> {
        match self {
            Self::Alignment(e) => CastError::Alignment(e.map_src(f)),
            Self::Size(e) => CastError::Size(e.map_src(f)),
            Self::Validity(i) => match i {},
        }
    }

    /// Converts the error into a general [`ConvertError`].
    pub(crate) fn into(self) -> TryCastError<Src, Dst>
    where
        Dst: TryFromBytes,
    {
        match self {
            Self::Alignment(e) => TryCastError::Alignment(e),
            Self::Size(e) => TryCastError::Size(e),
            Self::Validity(i) => match i {},
        }
    }
}

// SAFETY: `CastError` is either a single `AlignmentError` or a single
// `SizeError`. In either case, it contains a single `Self::Inner = Src`, and no
// other non-ZST fields. `map` passes ownership of `self`'s sole `Self::Inner`
// to `f`.
unsafe impl<Src, NewSrc, Dst> crate::pointer::TryWithError<NewSrc> for crate::CastError<Src, Dst>
where
    Dst: ?Sized,
{
    type Inner = Src;
    type Mapped = crate::CastError<NewSrc, Dst>;

    #[inline]
    fn map<F: FnOnce(Src) -> NewSrc>(self, f: F) -> Self::Mapped {
        self.map_src(f)
    }
}

impl<Src, Dst: ?Sized + Unaligned> From<CastError<Src, Dst>> for SizeError<Src, Dst> {
    /// Infallibly extracts the [`SizeError`] from this `CastError` since `Dst`
    /// is unaligned.
    ///
    /// Since [`Dst: Unaligned`], it is impossible to encounter an alignment
    /// error, and so the only error that can be encountered at runtime is a
    /// [`SizeError`]. This method permits extracting that `SizeError`
    /// infallibly.
    ///
    /// [`Dst: Unaligned`]: crate::Unaligned
    ///
    /// # Examples
    ///
    /// ```rust
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, IntoBytes, KnownLayout, Immutable, Unaligned)]
    /// #[repr(C)]
    /// struct UdpHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// #[derive(FromBytes, IntoBytes, KnownLayout, Immutable, Unaligned)]
    /// #[repr(C, packed)]
    /// struct UdpPacket {
    ///     header: UdpHeader,
    ///     body: [u8],
    /// }
    ///
    /// impl UdpPacket {
    ///     pub fn parse(bytes: &[u8]) -> Result<&UdpPacket, SizeError<&[u8], UdpPacket>> {
    ///         // Since `UdpPacket: Unaligned`, we can map the `CastError` to a `SizeError`.
    ///         UdpPacket::ref_from_bytes(bytes).map_err(Into::into)
    ///     }
    /// }
    /// ```
    #[inline(always)]
    fn from(err: CastError<Src, Dst>) -> SizeError<Src, Dst> {
        match err {
            #[allow(unreachable_code)]
            CastError::Alignment(e) => match Infallible::from(e) {},
            CastError::Size(e) => e,
            CastError::Validity(i) => match i {},
        }
    }
}

/// The error type of fallible reference conversions.
///
/// Fallible reference conversions, like [`TryFromBytes::try_ref_from_bytes`]
/// may emit [alignment](AlignmentError), [size](SizeError), and
/// [validity](ValidityError) errors.
// Bounds on generic parameters are not enforced in type aliases, but they do
// appear in rustdoc.
#[allow(type_alias_bounds)]
pub type TryCastError<Src, Dst: ?Sized + TryFromBytes> =
    ConvertError<AlignmentError<Src, Dst>, SizeError<Src, Dst>, ValidityError<Src, Dst>>;

// FIXME(#1139): Remove the `TryFromBytes` here and in other downstream
// locations (all the way to `ValidityError`) if we determine it's not necessary
// for rich validity errors.
impl<Src, Dst: ?Sized + TryFromBytes> TryCastError<Src, Dst> {
    /// Produces the source underlying the failed conversion.
    #[inline]
    pub fn into_src(self) -> Src {
        match self {
            Self::Alignment(e) => e.src,
            Self::Size(e) => e.src,
            Self::Validity(e) => e.src,
        }
    }

    /// Maps the source value associated with the conversion error.
    ///
    /// This can help mitigate [issues with `Send`, `Sync` and `'static`
    /// bounds][self#send-sync-and-static].
    ///
    /// # Examples
    ///
    /// ```
    /// use core::num::NonZeroU32;
    /// use zerocopy::*;
    ///
    /// let source: [u8; 3] = [0, 0, 0];
    ///
    /// // Try to read a `NonZeroU32` from `source`.
    /// let maybe_u32: Result<&NonZeroU32, TryCastError<&[u8], NonZeroU32>>
    ///     = NonZeroU32::try_ref_from_bytes(&source[..]);
    ///
    /// // Map the error's source to its size and address.
    /// let maybe_u32: Result<&NonZeroU32, TryCastError<(usize, usize), NonZeroU32>> =
    ///     maybe_u32.map_err(|err| {
    ///         err.map_src(|src| (src.len(), src.as_ptr() as usize))
    ///     });
    /// ```
    #[inline]
    pub fn map_src<NewSrc>(self, f: impl FnOnce(Src) -> NewSrc) -> TryCastError<NewSrc, Dst> {
        match self {
            Self::Alignment(e) => TryCastError::Alignment(e.map_src(f)),
            Self::Size(e) => TryCastError::Size(e.map_src(f)),
            Self::Validity(e) => TryCastError::Validity(e.map_src(f)),
        }
    }
}

impl<Src, Dst: ?Sized + TryFromBytes> From<CastError<Src, Dst>> for TryCastError<Src, Dst> {
    #[inline]
    fn from(value: CastError<Src, Dst>) -> Self {
        match value {
            CastError::Alignment(e) => Self::Alignment(e),
            CastError::Size(e) => Self::Size(e),
            CastError::Validity(i) => match i {},
        }
    }
}

/// The error type of fallible read-conversions.
///
/// Fallible read-conversions, like [`TryFromBytes::try_read_from_bytes`] may
/// emit [size](SizeError) and [validity](ValidityError) errors, but not
/// alignment errors.
// Bounds on generic parameters are not enforced in type aliases, but they do
// appear in rustdoc.
#[allow(type_alias_bounds)]
pub type TryReadError<Src, Dst: ?Sized + TryFromBytes> =
    ConvertError<Infallible, SizeError<Src, Dst>, ValidityError<Src, Dst>>;

impl<Src, Dst: ?Sized + TryFromBytes> TryReadError<Src, Dst> {
    /// Produces the source underlying the failed conversion.
    #[inline]
    pub fn into_src(self) -> Src {
        match self {
            Self::Alignment(i) => match i {},
            Self::Size(e) => e.src,
            Self::Validity(e) => e.src,
        }
    }

    /// Maps the source value associated with the conversion error.
    ///
    /// This can help mitigate [issues with `Send`, `Sync` and `'static`
    /// bounds][self#send-sync-and-static].
    ///
    /// # Examples
    ///
    /// ```
    /// use core::num::NonZeroU32;
    /// use zerocopy::*;
    ///
    /// let source: [u8; 3] = [0, 0, 0];
    ///
    /// // Try to read a `NonZeroU32` from `source`.
    /// let maybe_u32: Result<NonZeroU32, TryReadError<&[u8], NonZeroU32>>
    ///     = NonZeroU32::try_read_from_bytes(&source[..]);
    ///
    /// // Map the error's source to its size.
    /// let maybe_u32: Result<NonZeroU32, TryReadError<usize, NonZeroU32>> =
    ///     maybe_u32.map_err(|err| {
    ///         err.map_src(|src| src.len())
    ///     });
    /// ```
    #[inline]
    pub fn map_src<NewSrc>(self, f: impl FnOnce(Src) -> NewSrc) -> TryReadError<NewSrc, Dst> {
        match self {
            Self::Alignment(i) => match i {},
            Self::Size(e) => TryReadError::Size(e.map_src(f)),
            Self::Validity(e) => TryReadError::Validity(e.map_src(f)),
        }
    }
}

/// The error type of well-aligned, fallible casts.
///
/// This is like [`TryCastError`], but for casts that are always well-aligned.
/// It is identical to `TryCastError`, except that its alignment error is
/// [`Infallible`].
///
/// As of this writing, none of zerocopy's API produces this error directly.
/// However, it is useful since it permits users to infallibly discard alignment
/// errors when they can prove statically that alignment errors are impossible.
///
/// # Examples
///
/// ```
/// use core::convert::Infallible;
/// use zerocopy::*;
/// # use zerocopy_derive::*;
///
/// #[derive(TryFromBytes, KnownLayout, Unaligned, Immutable)]
/// #[repr(C, packed)]
/// struct Bools {
///     one: bool,
///     two: bool,
///     many: [bool],
/// }
///
/// impl Bools {
///     fn parse(bytes: &[u8]) -> Result<&Bools, AlignedTryCastError<&[u8], Bools>> {
///         // Since `Bools: Unaligned`, we can infallibly discard
///         // the alignment error.
///         Bools::try_ref_from_bytes(bytes).map_err(Into::into)
///     }
/// }
/// ```
#[allow(type_alias_bounds)]
pub type AlignedTryCastError<Src, Dst: ?Sized + TryFromBytes> =
    ConvertError<Infallible, SizeError<Src, Dst>, ValidityError<Src, Dst>>;

/// The error type of a failed allocation.
///
/// This type is intended to be deprecated in favor of the standard library's
/// [`AllocError`] type once it is stabilized. When that happens, this type will
/// be replaced by a type alias to the standard library type. We do not intend
/// to treat this as a breaking change; users who wish to avoid breakage should
/// avoid writing code which assumes that this is *not* such an alias. For
/// example, implementing the same trait for both types will result in an impl
/// conflict once this type is an alias.
///
/// [`AllocError`]: https://doc.rust-lang.org/alloc/alloc/struct.AllocError.html
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct AllocError;

#[cfg(test)]
mod tests {
    use core::convert::Infallible;

    use super::*;

    #[test]
    fn test_send_sync() {
        // Test that all error types are `Send + Sync` even if `Dst: !Send +
        // !Sync`.

        #[allow(dead_code)]
        fn is_send_sync<T: Send + Sync>(_t: T) {}

        #[allow(dead_code)]
        fn alignment_err_is_send_sync<Src: Send + Sync, Dst>(err: AlignmentError<Src, Dst>) {
            is_send_sync(err)
        }

        #[allow(dead_code)]
        fn size_err_is_send_sync<Src: Send + Sync, Dst>(err: SizeError<Src, Dst>) {
            is_send_sync(err)
        }

        #[allow(dead_code)]
        fn validity_err_is_send_sync<Src: Send + Sync, Dst: TryFromBytes>(
            err: ValidityError<Src, Dst>,
        ) {
            is_send_sync(err)
        }

        #[allow(dead_code)]
        fn convert_error_is_send_sync<Src: Send + Sync, Dst: TryFromBytes>(
            err: ConvertError<
                AlignmentError<Src, Dst>,
                SizeError<Src, Dst>,
                ValidityError<Src, Dst>,
            >,
        ) {
            is_send_sync(err)
        }
    }

    #[test]
    fn test_eq_partial_eq_clone() {
        // Test that all error types implement `Eq`, `PartialEq`
        // and `Clone` if src does
        // even if `Dst: !Eq`, `!PartialEq`, `!Clone`.

        #[allow(dead_code)]
        fn is_eq_partial_eq_clone<T: Eq + PartialEq + Clone>(_t: T) {}

        #[allow(dead_code)]
        fn alignment_err_is_eq_partial_eq_clone<Src: Eq + PartialEq + Clone, Dst>(
            err: AlignmentError<Src, Dst>,
        ) {
            is_eq_partial_eq_clone(err)
        }

        #[allow(dead_code)]
        fn size_err_is_eq_partial_eq_clone<Src: Eq + PartialEq + Clone, Dst>(
            err: SizeError<Src, Dst>,
        ) {
            is_eq_partial_eq_clone(err)
        }

        #[allow(dead_code)]
        fn validity_err_is_eq_partial_eq_clone<Src: Eq + PartialEq + Clone, Dst: TryFromBytes>(
            err: ValidityError<Src, Dst>,
        ) {
            is_eq_partial_eq_clone(err)
        }

        #[allow(dead_code)]
        fn convert_error_is_eq_partial_eq_clone<Src: Eq + PartialEq + Clone, Dst: TryFromBytes>(
            err: ConvertError<
                AlignmentError<Src, Dst>,
                SizeError<Src, Dst>,
                ValidityError<Src, Dst>,
            >,
        ) {
            is_eq_partial_eq_clone(err)
        }
    }

    #[test]
    fn alignment_display() {
        #[repr(C, align(128))]
        struct Aligned {
            bytes: [u8; 128],
        }

        impl_known_layout!(elain::Align::<8>);

        let aligned = Aligned { bytes: [0; 128] };

        let bytes = &aligned.bytes[1..];
        let addr = crate::util::AsAddress::addr(bytes);
        assert_eq!(
            AlignmentError::<_, elain::Align::<8>>::new_checked(bytes).to_string(),
            format!("The conversion failed because the address of the source is not a multiple of the alignment of the destination type.\n\
            \nSource type: &[u8]\
            \nSource address: 0x{:x} (a multiple of 1)\
            \nDestination type: elain::Align<8>\
            \nDestination alignment: 8", addr)
        );

        let bytes = &aligned.bytes[2..];
        let addr = crate::util::AsAddress::addr(bytes);
        assert_eq!(
            AlignmentError::<_, elain::Align::<8>>::new_checked(bytes).to_string(),
            format!("The conversion failed because the address of the source is not a multiple of the alignment of the destination type.\n\
            \nSource type: &[u8]\
            \nSource address: 0x{:x} (a multiple of 2)\
            \nDestination type: elain::Align<8>\
            \nDestination alignment: 8", addr)
        );

        let bytes = &aligned.bytes[3..];
        let addr = crate::util::AsAddress::addr(bytes);
        assert_eq!(
            AlignmentError::<_, elain::Align::<8>>::new_checked(bytes).to_string(),
            format!("The conversion failed because the address of the source is not a multiple of the alignment of the destination type.\n\
            \nSource type: &[u8]\
            \nSource address: 0x{:x} (a multiple of 1)\
            \nDestination type: elain::Align<8>\
            \nDestination alignment: 8", addr)
        );

        let bytes = &aligned.bytes[4..];
        let addr = crate::util::AsAddress::addr(bytes);
        assert_eq!(
            AlignmentError::<_, elain::Align::<8>>::new_checked(bytes).to_string(),
            format!("The conversion failed because the address of the source is not a multiple of the alignment of the destination type.\n\
            \nSource type: &[u8]\
            \nSource address: 0x{:x} (a multiple of 4)\
            \nDestination type: elain::Align<8>\
            \nDestination alignment: 8", addr)
        );
    }

    #[test]
    fn size_display() {
        assert_eq!(
            SizeError::<_, [u8]>::new(&[0u8; 2][..]).to_string(),
            "The conversion failed because the source was incorrectly sized to complete the conversion into the destination type.\n\
            \nSource type: &[u8]\
            \nSource size: 2 bytes\
            \nDestination type: [u8]"
        );

        assert_eq!(
            SizeError::<_, [u8; 2]>::new(&[0u8; 1][..]).to_string(),
            "The conversion failed because the source was incorrectly sized to complete the conversion into the destination type.\n\
            \nSource type: &[u8]\
            \nSource size: 1 byte\
            \nDestination size: 2 bytes\
            \nDestination type: [u8; 2]"
        );
    }

    #[test]
    fn validity_display() {
        assert_eq!(
            ValidityError::<_, bool>::new(&[2u8; 1][..]).to_string(),
            "The conversion failed because the source bytes are not a valid value of the destination type.\n\
            \n\
            Destination type: bool"
        );
    }

    #[test]
    fn test_convert_error_debug() {
        let err: ConvertError<
            AlignmentError<&[u8], u16>,
            SizeError<&[u8], u16>,
            ValidityError<&[u8], bool>,
        > = ConvertError::Alignment(AlignmentError::new_checked(&[0u8]));
        assert_eq!(format!("{:?}", err), "Alignment(AlignmentError)");

        let err: ConvertError<
            AlignmentError<&[u8], u16>,
            SizeError<&[u8], u16>,
            ValidityError<&[u8], bool>,
        > = ConvertError::Size(SizeError::new(&[0u8]));
        assert_eq!(format!("{:?}", err), "Size(SizeError)");

        let err: ConvertError<
            AlignmentError<&[u8], u16>,
            SizeError<&[u8], u16>,
            ValidityError<&[u8], bool>,
        > = ConvertError::Validity(ValidityError::new(&[0u8]));
        assert_eq!(format!("{:?}", err), "Validity(ValidityError)");
    }

    #[test]
    fn test_convert_error_from_unaligned() {
        // u8 is Unaligned
        let err: ConvertError<
            AlignmentError<&[u8], u8>,
            SizeError<&[u8], u8>,
            ValidityError<&[u8], bool>,
        > = ConvertError::Size(SizeError::new(&[0u8]));
        let converted: ConvertError<Infallible, SizeError<&[u8], u8>, ValidityError<&[u8], bool>> =
            ConvertError::from(err);
        match converted {
            ConvertError::Size(_) => {}
            _ => panic!("Expected Size error"),
        }
    }

    #[test]
    fn test_alignment_error_display_debug() {
        let err: AlignmentError<&[u8], u16> = AlignmentError::new_checked(&[0u8]);
        assert!(format!("{:?}", err).contains("AlignmentError"));
        assert!(format!("{}", err).contains("address of the source is not a multiple"));
    }

    #[test]
    fn test_size_error_display_debug() {
        let err: SizeError<&[u8], u16> = SizeError::new(&[0u8]);
        assert!(format!("{:?}", err).contains("SizeError"));
        assert!(format!("{}", err).contains("source was incorrectly sized"));
    }

    #[test]
    fn test_validity_error_display_debug() {
        let err: ValidityError<&[u8], bool> = ValidityError::new(&[0u8]);
        assert!(format!("{:?}", err).contains("ValidityError"));
        assert!(format!("{}", err).contains("source bytes are not a valid value"));
    }

    #[test]
    fn test_convert_error_display_debug_more() {
        let err: ConvertError<
            AlignmentError<&[u8], u16>,
            SizeError<&[u8], u16>,
            ValidityError<&[u8], bool>,
        > = ConvertError::Alignment(AlignmentError::new_checked(&[0u8]));
        assert!(format!("{}", err).contains("address of the source is not a multiple"));

        let err: ConvertError<
            AlignmentError<&[u8], u16>,
            SizeError<&[u8], u16>,
            ValidityError<&[u8], bool>,
        > = ConvertError::Size(SizeError::new(&[0u8]));
        assert!(format!("{}", err).contains("source was incorrectly sized"));

        let err: ConvertError<
            AlignmentError<&[u8], u16>,
            SizeError<&[u8], u16>,
            ValidityError<&[u8], bool>,
        > = ConvertError::Validity(ValidityError::new(&[0u8]));
        assert!(format!("{}", err).contains("source bytes are not a valid value"));
    }

    #[test]
    fn test_alignment_error_methods() {
        let err: AlignmentError<&[u8], u16> = AlignmentError::new_checked(&[0u8]);

        // into_src
        let src = err.clone().into_src();
        assert_eq!(src, &[0u8]);

        // into
        let converted: ConvertError<
            AlignmentError<&[u8], u16>,
            SizeError<&[u8], u16>,
            ValidityError<&[u8], bool>,
        > = err.clone().into();
        match converted {
            ConvertError::Alignment(_) => {}
            _ => panic!("Expected Alignment error"),
        }

        // clone
        let cloned = err.clone();
        assert_eq!(err, cloned);

        // eq
        assert_eq!(err, cloned);
        let err2: AlignmentError<&[u8], u16> = AlignmentError::new_checked(&[1u8]);
        assert_ne!(err, err2);
    }

    #[test]
    fn test_convert_error_from_unaligned_variants() {
        // u8 is Unaligned
        let err: ConvertError<
            AlignmentError<&[u8], u8>,
            SizeError<&[u8], u8>,
            ValidityError<&[u8], bool>,
        > = ConvertError::Validity(ValidityError::new(&[0u8]));
        let converted: ConvertError<Infallible, SizeError<&[u8], u8>, ValidityError<&[u8], bool>> =
            ConvertError::from(err);
        match converted {
            ConvertError::Validity(_) => {}
            _ => panic!("Expected Validity error"),
        }

        let err: ConvertError<
            AlignmentError<&[u8], u8>,
            SizeError<&[u8], u8>,
            ValidityError<&[u8], bool>,
        > = ConvertError::Size(SizeError::new(&[0u8]));
        let converted: ConvertError<Infallible, SizeError<&[u8], u8>, ValidityError<&[u8], bool>> =
            ConvertError::from(err);
        match converted {
            ConvertError::Size(_) => {}
            _ => panic!("Expected Size error"),
        }
    }
}
