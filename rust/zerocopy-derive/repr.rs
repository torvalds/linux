// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2019 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

use core::{
    convert::{Infallible, TryFrom},
    num::NonZeroU32,
};

use proc_macro2::{Span, TokenStream};
use quote::{quote_spanned, ToTokens, TokenStreamExt as _};
use syn::{
    punctuated::Punctuated, spanned::Spanned as _, token::Comma, Attribute, Error, LitInt, Meta,
    MetaList,
};

/// The computed representation of a type.
///
/// This is the result of processing all `#[repr(...)]` attributes on a type, if
/// any. A `Repr` is only capable of representing legal combinations of
/// `#[repr(...)]` attributes.
#[cfg_attr(test, derive(Copy, Clone, Debug))]
pub(crate) enum Repr<Prim, Packed> {
    /// `#[repr(transparent)]`
    Transparent(Span),
    /// A compound representation: `repr(C)`, `repr(Rust)`, or `repr(Int)`
    /// optionally combined with `repr(packed(...))` or `repr(align(...))`
    Compound(Spanned<CompoundRepr<Prim>>, Option<Spanned<AlignRepr<Packed>>>),
}

/// A compound representation: `repr(C)`, `repr(Rust)`, or `repr(Int)`.
#[cfg_attr(test, derive(Copy, Clone, Debug, Eq, PartialEq))]
pub(crate) enum CompoundRepr<Prim> {
    C,
    Rust,
    Primitive(Prim),
}

/// `repr(Int)`
#[derive(Copy, Clone)]
#[cfg_attr(test, derive(Debug, Eq, PartialEq))]
pub(crate) enum PrimitiveRepr {
    U8,
    U16,
    U32,
    U64,
    U128,
    Usize,
    I8,
    I16,
    I32,
    I64,
    I128,
    Isize,
}

/// `repr(packed(...))` or `repr(align(...))`
#[cfg_attr(test, derive(Copy, Clone, Debug, Eq, PartialEq))]
pub(crate) enum AlignRepr<Packed> {
    Packed(Packed),
    Align(NonZeroU32),
}

/// The representations which can legally appear on a struct or union type.
pub(crate) type StructUnionRepr = Repr<Infallible, NonZeroU32>;

/// The representations which can legally appear on an enum type.
pub(crate) type EnumRepr = Repr<PrimitiveRepr, Infallible>;

impl<Prim, Packed> Repr<Prim, Packed> {
    /// Gets the name of this "repr type" - the non-align `repr(X)` that is used
    /// in prose to refer to this type.
    ///
    /// For example, we would refer to `#[repr(C, align(4))] struct Foo { ... }`
    /// as a "`repr(C)` struct".
    pub(crate) fn repr_type_name(&self) -> &str
    where
        Prim: Copy + With<PrimitiveRepr>,
    {
        use CompoundRepr::*;
        use PrimitiveRepr::*;
        use Repr::*;
        match self {
            Transparent(_span) => "repr(transparent)",
            Compound(Spanned { t: repr, span: _ }, _align) => match repr {
                C => "repr(C)",
                Rust => "repr(Rust)",
                Primitive(prim) => prim.with(|prim| match prim {
                    U8 => "repr(u8)",
                    U16 => "repr(u16)",
                    U32 => "repr(u32)",
                    U64 => "repr(u64)",
                    U128 => "repr(u128)",
                    Usize => "repr(usize)",
                    I8 => "repr(i8)",
                    I16 => "repr(i16)",
                    I32 => "repr(i32)",
                    I64 => "repr(i64)",
                    I128 => "repr(i128)",
                    Isize => "repr(isize)",
                }),
            },
        }
    }

    pub(crate) fn is_transparent(&self) -> bool {
        matches!(self, Repr::Transparent(_))
    }

    pub(crate) fn is_c(&self) -> bool {
        use CompoundRepr::*;
        matches!(self, Repr::Compound(Spanned { t: C, span: _ }, _align))
    }

    pub(crate) fn is_primitive(&self) -> bool {
        use CompoundRepr::*;
        matches!(self, Repr::Compound(Spanned { t: Primitive(_), span: _ }, _align))
    }

    pub(crate) fn get_packed(&self) -> Option<&Packed> {
        use AlignRepr::*;
        use Repr::*;
        if let Compound(_, Some(Spanned { t: Packed(p), span: _ })) = self {
            Some(p)
        } else {
            None
        }
    }

    pub(crate) fn get_align(&self) -> Option<Spanned<NonZeroU32>> {
        use AlignRepr::*;
        use Repr::*;
        if let Compound(_, Some(Spanned { t: Align(n), span })) = self {
            Some(Spanned::new(*n, *span))
        } else {
            None
        }
    }

    pub(crate) fn is_align_gt_1(&self) -> bool {
        self.get_align().map(|n| n.t.get() > 1).unwrap_or(false)
    }

    /// When deriving `Unaligned`, validate that the decorated type has no
    /// `#[repr(align(N))]` attribute where `N > 1`. If no such attribute exists
    /// (including if `N == 1`), this returns `Ok(())`, and otherwise it returns
    /// a descriptive error.
    pub(crate) fn unaligned_validate_no_align_gt_1(&self) -> Result<(), Error> {
        if let Some(n) = self.get_align().filter(|n| n.t.get() > 1) {
            Err(Error::new(
                n.span,
                "cannot derive `Unaligned` on type with alignment greater than 1",
            ))
        } else {
            Ok(())
        }
    }
}

impl<Prim> Repr<Prim, NonZeroU32> {
    /// Does `self` describe a `#[repr(packed)]` or `#[repr(packed(1))]` type?
    pub(crate) fn is_packed_1(&self) -> bool {
        self.get_packed().map(|n| n.get() == 1).unwrap_or(false)
    }
}

impl<Packed> Repr<PrimitiveRepr, Packed> {
    fn get_primitive(&self) -> Option<&PrimitiveRepr> {
        use CompoundRepr::*;
        use Repr::*;
        if let Compound(Spanned { t: Primitive(p), span: _ }, _align) = self {
            Some(p)
        } else {
            None
        }
    }

    /// Does `self` describe a `#[repr(u8)]` type?
    pub(crate) fn is_u8(&self) -> bool {
        matches!(self.get_primitive(), Some(PrimitiveRepr::U8))
    }

    /// Does `self` describe a `#[repr(i8)]` type?
    pub(crate) fn is_i8(&self) -> bool {
        matches!(self.get_primitive(), Some(PrimitiveRepr::I8))
    }
}

impl<Prim, Packed> ToTokens for Repr<Prim, Packed>
where
    Prim: With<PrimitiveRepr> + Copy,
    Packed: With<NonZeroU32> + Copy,
{
    fn to_tokens(&self, ts: &mut TokenStream) {
        use Repr::*;
        match self {
            Transparent(span) => ts.append_all(quote_spanned! { *span=> #[repr(transparent)] }),
            Compound(repr, align) => {
                repr.to_tokens(ts);
                if let Some(align) = align {
                    align.to_tokens(ts);
                }
            }
        }
    }
}

impl<Prim: With<PrimitiveRepr> + Copy> ToTokens for Spanned<CompoundRepr<Prim>> {
    fn to_tokens(&self, ts: &mut TokenStream) {
        use CompoundRepr::*;
        match &self.t {
            C => ts.append_all(quote_spanned! { self.span=> #[repr(C)] }),
            Rust => ts.append_all(quote_spanned! { self.span=> #[repr(Rust)] }),
            Primitive(prim) => prim.with(|prim| Spanned::new(prim, self.span).to_tokens(ts)),
        }
    }
}

impl ToTokens for Spanned<PrimitiveRepr> {
    fn to_tokens(&self, ts: &mut TokenStream) {
        use PrimitiveRepr::*;
        match self.t {
            U8 => ts.append_all(quote_spanned! { self.span => #[repr(u8)] }),
            U16 => ts.append_all(quote_spanned! { self.span => #[repr(u16)] }),
            U32 => ts.append_all(quote_spanned! { self.span => #[repr(u32)] }),
            U64 => ts.append_all(quote_spanned! { self.span => #[repr(u64)] }),
            U128 => ts.append_all(quote_spanned! { self.span => #[repr(u128)] }),
            Usize => ts.append_all(quote_spanned! { self.span => #[repr(usize)] }),
            I8 => ts.append_all(quote_spanned! { self.span => #[repr(i8)] }),
            I16 => ts.append_all(quote_spanned! { self.span => #[repr(i16)] }),
            I32 => ts.append_all(quote_spanned! { self.span => #[repr(i32)] }),
            I64 => ts.append_all(quote_spanned! { self.span => #[repr(i64)] }),
            I128 => ts.append_all(quote_spanned! { self.span => #[repr(i128)] }),
            Isize => ts.append_all(quote_spanned! { self.span => #[repr(isize)] }),
        }
    }
}

impl<Packed: With<NonZeroU32> + Copy> ToTokens for Spanned<AlignRepr<Packed>> {
    fn to_tokens(&self, ts: &mut TokenStream) {
        use AlignRepr::*;
        // We use `syn::Index` instead of `u32` because `quote_spanned!`
        // serializes `u32` literals as `123u32`, not just `123`. Rust doesn't
        // recognize that as a valid argument to `#[repr(align(...))]` or
        // `#[repr(packed(...))]`.
        let to_index = |n: NonZeroU32| syn::Index { index: n.get(), span: self.span };
        match self.t {
            Packed(n) => n.with(|n| {
                let n = to_index(n);
                ts.append_all(quote_spanned! { self.span => #[repr(packed(#n))] })
            }),
            Align(n) => {
                let n = to_index(n);
                ts.append_all(quote_spanned! { self.span => #[repr(align(#n))] })
            }
        }
    }
}

/// The result of parsing a single `#[repr(...)]` attribute or a single
/// directive inside a compound `#[repr(..., ...)]` attribute.
#[derive(Copy, Clone, PartialEq, Eq)]
#[cfg_attr(test, derive(Debug))]
pub(crate) enum RawRepr {
    Transparent,
    C,
    Rust,
    U8,
    U16,
    U32,
    U64,
    U128,
    Usize,
    I8,
    I16,
    I32,
    I64,
    I128,
    Isize,
    Align(NonZeroU32),
    PackedN(NonZeroU32),
    Packed,
}

/// The error from converting from a `RawRepr`.
#[cfg_attr(test, derive(Debug, Eq, PartialEq))]
pub(crate) enum FromRawReprError<E> {
    /// The `RawRepr` doesn't affect the high-level repr we're parsing (e.g.
    /// it's `align(...)` and we're parsing a `CompoundRepr`).
    None,
    /// The `RawRepr` is invalid for the high-level repr we're parsing (e.g.
    /// it's `packed` repr and we're parsing an `AlignRepr` for an enum type).
    Err(E),
}

/// The representation hint is not supported for the decorated type.
#[cfg_attr(test, derive(Copy, Clone, Debug, Eq, PartialEq))]
pub(crate) struct UnsupportedReprError;

impl<Prim: With<PrimitiveRepr>> TryFrom<RawRepr> for CompoundRepr<Prim> {
    type Error = FromRawReprError<UnsupportedReprError>;
    fn try_from(
        raw: RawRepr,
    ) -> Result<CompoundRepr<Prim>, FromRawReprError<UnsupportedReprError>> {
        use RawRepr::*;
        match raw {
            C => Ok(CompoundRepr::C),
            Rust => Ok(CompoundRepr::Rust),
            raw @ (U8 | U16 | U32 | U64 | U128 | Usize | I8 | I16 | I32 | I64 | I128 | Isize) => {
                Prim::try_with_or(
                    || match raw {
                        U8 => Ok(PrimitiveRepr::U8),
                        U16 => Ok(PrimitiveRepr::U16),
                        U32 => Ok(PrimitiveRepr::U32),
                        U64 => Ok(PrimitiveRepr::U64),
                        U128 => Ok(PrimitiveRepr::U128),
                        Usize => Ok(PrimitiveRepr::Usize),
                        I8 => Ok(PrimitiveRepr::I8),
                        I16 => Ok(PrimitiveRepr::I16),
                        I32 => Ok(PrimitiveRepr::I32),
                        I64 => Ok(PrimitiveRepr::I64),
                        I128 => Ok(PrimitiveRepr::I128),
                        Isize => Ok(PrimitiveRepr::Isize),
                        Transparent | C | Rust | Align(_) | PackedN(_) | Packed => {
                            Err(UnsupportedReprError)
                        }
                    },
                    UnsupportedReprError,
                )
                .map(CompoundRepr::Primitive)
                .map_err(FromRawReprError::Err)
            }
            Transparent | Align(_) | PackedN(_) | Packed => Err(FromRawReprError::None),
        }
    }
}

impl<Pcked: With<NonZeroU32>> TryFrom<RawRepr> for AlignRepr<Pcked> {
    type Error = FromRawReprError<UnsupportedReprError>;
    fn try_from(raw: RawRepr) -> Result<AlignRepr<Pcked>, FromRawReprError<UnsupportedReprError>> {
        use RawRepr::*;
        match raw {
            Packed | PackedN(_) => Pcked::try_with_or(
                || match raw {
                    Packed => Ok(NonZeroU32::new(1).unwrap()),
                    PackedN(n) => Ok(n),
                    U8 | U16 | U32 | U64 | U128 | Usize | I8 | I16 | I32 | I64 | I128 | Isize
                    | Transparent | C | Rust | Align(_) => Err(UnsupportedReprError),
                },
                UnsupportedReprError,
            )
            .map(AlignRepr::Packed)
            .map_err(FromRawReprError::Err),
            Align(n) => Ok(AlignRepr::Align(n)),
            U8 | U16 | U32 | U64 | U128 | Usize | I8 | I16 | I32 | I64 | I128 | Isize
            | Transparent | C | Rust => Err(FromRawReprError::None),
        }
    }
}

/// The error from extracting a high-level repr type from a list of `RawRepr`s.
#[cfg_attr(test, derive(Copy, Clone, Debug, Eq, PartialEq))]
enum FromRawReprsError<E> {
    /// One of the `RawRepr`s is invalid for the high-level repr we're parsing
    /// (e.g. there's a `packed` repr and we're parsing an `AlignRepr` for an
    /// enum type).
    Single(E),
    /// Two `RawRepr`s appear which both affect the high-level repr we're
    /// parsing (e.g., the list is `#[repr(align(2), packed)]`). Note that we
    /// conservatively treat redundant reprs as conflicting (e.g.
    /// `#[repr(packed, packed)]`).
    Conflict,
}

/// Tries to extract a high-level repr from a list of `RawRepr`s.
fn try_from_raw_reprs<'a, E, R: TryFrom<RawRepr, Error = FromRawReprError<E>>>(
    r: impl IntoIterator<Item = &'a Spanned<RawRepr>>,
) -> Result<Option<Spanned<R>>, Spanned<FromRawReprsError<E>>> {
    // Walk the list of `RawRepr`s and attempt to convert each to an `R`. Bail
    // if we find any errors. If we find more than one which converts to an `R`,
    // bail with a `Conflict` error.
    r.into_iter().try_fold(None, |found: Option<Spanned<R>>, raw| {
        let new = match Spanned::<R>::try_from(*raw) {
            Ok(r) => r,
            // This `RawRepr` doesn't convert to an `R`, so keep the current
            // found `R`, if any.
            Err(FromRawReprError::None) => return Ok(found),
            // This repr is unsupported for the decorated type (e.g.
            // `repr(packed)` on an enum).
            Err(FromRawReprError::Err(Spanned { t: err, span })) => {
                return Err(Spanned::new(FromRawReprsError::Single(err), span))
            }
        };

        if let Some(found) = found {
            // We already found an `R`, but this `RawRepr` also converts to an
            // `R`, so that's a conflict.
            //
            // `Span::join` returns `None` if the two spans are from different
            // files or if we're not on the nightly compiler. In that case, just
            // use `new`'s span.
            let span = found.span.join(new.span).unwrap_or(new.span);
            Err(Spanned::new(FromRawReprsError::Conflict, span))
        } else {
            Ok(Some(new))
        }
    })
}

/// The error returned from [`Repr::from_attrs`].
#[cfg_attr(test, derive(Copy, Clone, Debug, Eq, PartialEq))]
enum FromAttrsError {
    FromRawReprs(FromRawReprsError<UnsupportedReprError>),
    Unrecognized,
}

impl From<FromRawReprsError<UnsupportedReprError>> for FromAttrsError {
    fn from(err: FromRawReprsError<UnsupportedReprError>) -> FromAttrsError {
        FromAttrsError::FromRawReprs(err)
    }
}

impl From<UnrecognizedReprError> for FromAttrsError {
    fn from(_err: UnrecognizedReprError) -> FromAttrsError {
        FromAttrsError::Unrecognized
    }
}

impl From<Spanned<FromAttrsError>> for Error {
    fn from(err: Spanned<FromAttrsError>) -> Error {
        let Spanned { t: err, span } = err;
        match err {
            FromAttrsError::FromRawReprs(FromRawReprsError::Single(
                _err @ UnsupportedReprError,
            )) => Error::new(span, "unsupported representation hint for the decorated type"),
            FromAttrsError::FromRawReprs(FromRawReprsError::Conflict) => {
                // NOTE: This says "another" rather than "a preceding" because
                // when one of the reprs involved is `transparent`, we detect
                // that condition in `Repr::from_attrs`, and at that point we
                // can't tell which repr came first, so we might report this on
                // the first involved repr rather than the second, third, etc.
                Error::new(span, "this conflicts with another representation hint")
            }
            FromAttrsError::Unrecognized => Error::new(span, "unrecognized representation hint"),
        }
    }
}

impl<Prim, Packed> Repr<Prim, Packed> {
    fn from_attrs_inner(attrs: &[Attribute]) -> Result<Repr<Prim, Packed>, Spanned<FromAttrsError>>
    where
        Prim: With<PrimitiveRepr>,
        Packed: With<NonZeroU32>,
    {
        let raw_reprs = RawRepr::from_attrs(attrs).map_err(Spanned::from)?;

        let transparent = {
            let mut transparents = raw_reprs.iter().filter_map(|Spanned { t, span }| match t {
                RawRepr::Transparent => Some(span),
                _ => None,
            });
            let first = transparents.next();
            let second = transparents.next();
            match (first, second) {
                (None, None) => None,
                (Some(span), None) => Some(*span),
                (Some(_), Some(second)) => {
                    return Err(Spanned::new(
                        FromAttrsError::FromRawReprs(FromRawReprsError::Conflict),
                        *second,
                    ))
                }
                // An iterator can't produce a value only on the second call to
                // `.next()`.
                (None, Some(_)) => unreachable!(),
            }
        };

        let compound: Option<Spanned<CompoundRepr<Prim>>> =
            try_from_raw_reprs(raw_reprs.iter()).map_err(Spanned::from)?;
        let align: Option<Spanned<AlignRepr<Packed>>> =
            try_from_raw_reprs(raw_reprs.iter()).map_err(Spanned::from)?;

        if let Some(span) = transparent {
            if compound.is_some() || align.is_some() {
                // Arbitrarily report the problem on the `transparent` span. Any
                // span will do.
                return Err(Spanned::new(FromRawReprsError::Conflict.into(), span));
            }

            Ok(Repr::Transparent(span))
        } else {
            Ok(Repr::Compound(
                compound.unwrap_or(Spanned::new(CompoundRepr::Rust, Span::call_site())),
                align,
            ))
        }
    }
}

impl<Prim, Packed> Repr<Prim, Packed> {
    pub(crate) fn from_attrs(attrs: &[Attribute]) -> Result<Repr<Prim, Packed>, Error>
    where
        Prim: With<PrimitiveRepr>,
        Packed: With<NonZeroU32>,
    {
        Repr::from_attrs_inner(attrs).map_err(Into::into)
    }
}

/// The representation hint could not be parsed or was unrecognized.
struct UnrecognizedReprError;

impl RawRepr {
    fn from_attrs(
        attrs: &[Attribute],
    ) -> Result<Vec<Spanned<RawRepr>>, Spanned<UnrecognizedReprError>> {
        let mut reprs = Vec::new();
        for attr in attrs {
            // Ignore documentation attributes.
            if attr.path().is_ident("doc") {
                continue;
            }
            if let Meta::List(ref meta_list) = attr.meta {
                if meta_list.path.is_ident("repr") {
                    let parsed: Punctuated<Meta, Comma> =
                        match meta_list.parse_args_with(Punctuated::parse_terminated) {
                            Ok(parsed) => parsed,
                            Err(_) => {
                                return Err(Spanned::new(
                                    UnrecognizedReprError,
                                    meta_list.tokens.span(),
                                ))
                            }
                        };
                    for meta in parsed {
                        let s = meta.span();
                        reprs.push(
                            RawRepr::from_meta(&meta)
                                .map(|r| Spanned::new(r, s))
                                .map_err(|e| Spanned::new(e, s))?,
                        );
                    }
                }
            }
        }

        Ok(reprs)
    }

    fn from_meta(meta: &Meta) -> Result<RawRepr, UnrecognizedReprError> {
        let (path, list) = match meta {
            Meta::Path(path) => (path, None),
            Meta::List(list) => (&list.path, Some(list)),
            _ => return Err(UnrecognizedReprError),
        };

        let ident = path.get_ident().ok_or(UnrecognizedReprError)?;

        // Only returns `Ok` for non-zero power-of-two values.
        let parse_nzu64 = |list: &MetaList| {
            list.parse_args::<LitInt>()
                .and_then(|int| int.base10_parse::<NonZeroU32>())
                .map_err(|_| UnrecognizedReprError)
                .and_then(|nz| {
                    if nz.get().is_power_of_two() {
                        Ok(nz)
                    } else {
                        Err(UnrecognizedReprError)
                    }
                })
        };

        use RawRepr::*;
        Ok(match (ident.to_string().as_str(), list) {
            ("u8", None) => U8,
            ("u16", None) => U16,
            ("u32", None) => U32,
            ("u64", None) => U64,
            ("u128", None) => U128,
            ("usize", None) => Usize,
            ("i8", None) => I8,
            ("i16", None) => I16,
            ("i32", None) => I32,
            ("i64", None) => I64,
            ("i128", None) => I128,
            ("isize", None) => Isize,
            ("C", None) => C,
            ("transparent", None) => Transparent,
            ("Rust", None) => Rust,
            ("packed", None) => Packed,
            ("packed", Some(list)) => PackedN(parse_nzu64(list)?),
            ("align", Some(list)) => Align(parse_nzu64(list)?),
            _ => return Err(UnrecognizedReprError),
        })
    }
}

pub(crate) use util::*;
mod util {
    use super::*;
    /// A value with an associated span.
    #[derive(Copy, Clone)]
    #[cfg_attr(test, derive(Debug))]
    pub(crate) struct Spanned<T> {
        pub(crate) t: T,
        pub(crate) span: Span,
    }

    impl<T> Spanned<T> {
        pub(super) fn new(t: T, span: Span) -> Spanned<T> {
            Spanned { t, span }
        }

        pub(super) fn from<U>(s: Spanned<U>) -> Spanned<T>
        where
            T: From<U>,
        {
            let Spanned { t: u, span } = s;
            Spanned::new(u.into(), span)
        }

        /// Delegates to `T: TryFrom`, preserving span information in both the
        /// success and error cases.
        pub(super) fn try_from<E, U>(
            u: Spanned<U>,
        ) -> Result<Spanned<T>, FromRawReprError<Spanned<E>>>
        where
            T: TryFrom<U, Error = FromRawReprError<E>>,
        {
            let Spanned { t: u, span } = u;
            T::try_from(u).map(|t| Spanned { t, span }).map_err(|err| match err {
                FromRawReprError::None => FromRawReprError::None,
                FromRawReprError::Err(e) => FromRawReprError::Err(Spanned::new(e, span)),
            })
        }
    }

    // Used to permit implementing `With<T> for T: Inhabited` and for
    // `Infallible` without a blanket impl conflict.
    pub(crate) trait Inhabited {}
    impl Inhabited for PrimitiveRepr {}
    impl Inhabited for NonZeroU32 {}

    pub(crate) trait With<T> {
        fn with<O, F: FnOnce(T) -> O>(self, f: F) -> O;
        fn try_with_or<E, F: FnOnce() -> Result<T, E>>(f: F, err: E) -> Result<Self, E>
        where
            Self: Sized;
    }

    impl<T: Inhabited> With<T> for T {
        fn with<O, F: FnOnce(T) -> O>(self, f: F) -> O {
            f(self)
        }

        fn try_with_or<E, F: FnOnce() -> Result<T, E>>(f: F, _err: E) -> Result<Self, E> {
            f()
        }
    }

    impl<T> With<T> for Infallible {
        fn with<O, F: FnOnce(T) -> O>(self, _f: F) -> O {
            match self {}
        }

        fn try_with_or<E, F: FnOnce() -> Result<T, E>>(_f: F, err: E) -> Result<Self, E> {
            Err(err)
        }
    }
}

#[cfg(test)]
mod tests {
    use syn::parse_quote;

    use super::*;

    impl<T> From<T> for Spanned<T> {
        fn from(t: T) -> Spanned<T> {
            Spanned::new(t, Span::call_site())
        }
    }

    // We ignore spans for equality in testing since real spans are hard to
    // synthesize and don't implement `PartialEq`.
    impl<T: PartialEq> PartialEq for Spanned<T> {
        fn eq(&self, other: &Spanned<T>) -> bool {
            self.t.eq(&other.t)
        }
    }

    impl<T: Eq> Eq for Spanned<T> {}

    impl<Prim: PartialEq, Packed: PartialEq> PartialEq for Repr<Prim, Packed> {
        fn eq(&self, other: &Repr<Prim, Packed>) -> bool {
            match (self, other) {
                (Repr::Transparent(_), Repr::Transparent(_)) => true,
                (Repr::Compound(sc, sa), Repr::Compound(oc, oa)) => (sc, sa) == (oc, oa),
                _ => false,
            }
        }
    }

    fn s() -> Span {
        Span::call_site()
    }

    #[test]
    fn test() {
        // Test that a given `#[repr(...)]` attribute parses and returns the
        // given `Repr` or error.
        macro_rules! test {
            ($(#[$attr:meta])* => $repr:expr) => {
                test!(@inner $(#[$attr])* => Repr => Ok($repr));
            };
            // In the error case, the caller must explicitly provide the name of
            // the `Repr` type to assist in type inference.
            (@error $(#[$attr:meta])* => $typ:ident => $repr:expr) => {
                test!(@inner $(#[$attr])* => $typ => Err($repr));
            };
            (@inner $(#[$attr:meta])* => $typ:ident => $repr:expr) => {
                let attr: Attribute = parse_quote!($(#[$attr])*);
                let mut got = $typ::from_attrs_inner(&[attr]);
                let expect: Result<Repr<_, _>, _> = $repr;
                if false {
                    // Force Rust to infer `got` as having the same type as
                    // `expect`.
                    got = expect;
                }
                assert_eq!(got, expect, stringify!($(#[$attr])*));
            };
        }

        use AlignRepr::*;
        use CompoundRepr::*;
        use PrimitiveRepr::*;
        let nz = |n: u32| NonZeroU32::new(n).unwrap();

        test!(#[repr(transparent)] => StructUnionRepr::Transparent(s()));
        test!(#[repr()] => StructUnionRepr::Compound(Rust.into(), None));
        test!(#[repr(packed)] => StructUnionRepr::Compound(Rust.into(), Some(Packed(nz(1)).into())));
        test!(#[repr(packed(2))] => StructUnionRepr::Compound(Rust.into(), Some(Packed(nz(2)).into())));
        test!(#[repr(align(1))] => StructUnionRepr::Compound(Rust.into(), Some(Align(nz(1)).into())));
        test!(#[repr(align(2))] => StructUnionRepr::Compound(Rust.into(), Some(Align(nz(2)).into())));
        test!(#[repr(C)] => StructUnionRepr::Compound(C.into(), None));
        test!(#[repr(C, packed)] => StructUnionRepr::Compound(C.into(), Some(Packed(nz(1)).into())));
        test!(#[repr(C, packed(2))] => StructUnionRepr::Compound(C.into(), Some(Packed(nz(2)).into())));
        test!(#[repr(C, align(1))] => StructUnionRepr::Compound(C.into(), Some(Align(nz(1)).into())));
        test!(#[repr(C, align(2))] => StructUnionRepr::Compound(C.into(), Some(Align(nz(2)).into())));

        test!(#[repr(transparent)] => EnumRepr::Transparent(s()));
        test!(#[repr()] => EnumRepr::Compound(Rust.into(), None));
        test!(#[repr(align(1))] => EnumRepr::Compound(Rust.into(), Some(Align(nz(1)).into())));
        test!(#[repr(align(2))] => EnumRepr::Compound(Rust.into(), Some(Align(nz(2)).into())));

        macro_rules! for_each_compound_repr {
            ($($r:tt => $var:expr),*) => {
                $(
                    test!(#[repr($r)] => EnumRepr::Compound($var.into(), None));
                    test!(#[repr($r, align(1))] => EnumRepr::Compound($var.into(), Some(Align(nz(1)).into())));
                    test!(#[repr($r, align(2))] => EnumRepr::Compound($var.into(), Some(Align(nz(2)).into())));
                )*
            }
        }

        for_each_compound_repr!(
            C => C,
            u8 => Primitive(U8),
            u16 => Primitive(U16),
            u32 => Primitive(U32),
            u64 => Primitive(U64),
            usize => Primitive(Usize),
            i8 => Primitive(I8),
            i16 => Primitive(I16),
            i32 => Primitive(I32),
            i64 => Primitive(I64),
            isize => Primitive(Isize)
        );

        use FromAttrsError::*;
        use FromRawReprsError::*;

        // Run failure tests which are valid for both `StructUnionRepr` and
        // `EnumRepr`.
        macro_rules! for_each_repr_type {
            ($($repr:ident),*) => {
                $(
                    // Invalid packed or align attributes
                    test!(@error #[repr(packed(0))] => $repr => Unrecognized.into());
                    test!(@error #[repr(packed(3))] => $repr => Unrecognized.into());
                    test!(@error #[repr(align(0))] => $repr => Unrecognized.into());
                    test!(@error #[repr(align(3))] => $repr => Unrecognized.into());

                    // Conflicts
                    test!(@error #[repr(transparent, transparent)] => $repr => FromRawReprs(Conflict).into());
                    test!(@error #[repr(transparent, C)] => $repr => FromRawReprs(Conflict).into());
                    test!(@error #[repr(transparent, Rust)] => $repr => FromRawReprs(Conflict).into());

                    test!(@error #[repr(C, transparent)] => $repr => FromRawReprs(Conflict).into());
                    test!(@error #[repr(C, C)] => $repr => FromRawReprs(Conflict).into());
                    test!(@error #[repr(C, Rust)] => $repr => FromRawReprs(Conflict).into());

                    test!(@error #[repr(Rust, transparent)] => $repr => FromRawReprs(Conflict).into());
                    test!(@error #[repr(Rust, C)] => $repr => FromRawReprs(Conflict).into());
                    test!(@error #[repr(Rust, Rust)] => $repr => FromRawReprs(Conflict).into());
                )*
            }
        }

        for_each_repr_type!(StructUnionRepr, EnumRepr);

        // Enum-specific conflicts.
        //
        // We don't bother to test every combination since that would be a huge
        // number (enums can have primitive reprs u8, u16, u32, u64, usize, i8,
        // i16, i32, i64, and isize). Instead, since the conflict logic doesn't
        // care what specific value of `PrimitiveRepr` is present, we assume
        // that testing against u8 alone is fine.
        test!(@error #[repr(transparent, u8)] => EnumRepr => FromRawReprs(Conflict).into());
        test!(@error #[repr(u8, transparent)] => EnumRepr => FromRawReprs(Conflict).into());
        test!(@error #[repr(C, u8)] => EnumRepr => FromRawReprs(Conflict).into());
        test!(@error #[repr(u8, C)] => EnumRepr => FromRawReprs(Conflict).into());
        test!(@error #[repr(Rust, u8)] => EnumRepr => FromRawReprs(Conflict).into());
        test!(@error #[repr(u8, Rust)] => EnumRepr => FromRawReprs(Conflict).into());
        test!(@error #[repr(u8, u8)] => EnumRepr => FromRawReprs(Conflict).into());

        // Illegal struct/union reprs
        test!(@error #[repr(u8)] => StructUnionRepr => FromRawReprs(Single(UnsupportedReprError)).into());
        test!(@error #[repr(u16)] => StructUnionRepr => FromRawReprs(Single(UnsupportedReprError)).into());
        test!(@error #[repr(u32)] => StructUnionRepr => FromRawReprs(Single(UnsupportedReprError)).into());
        test!(@error #[repr(u64)] => StructUnionRepr => FromRawReprs(Single(UnsupportedReprError)).into());
        test!(@error #[repr(usize)] => StructUnionRepr => FromRawReprs(Single(UnsupportedReprError)).into());
        test!(@error #[repr(i8)] => StructUnionRepr => FromRawReprs(Single(UnsupportedReprError)).into());
        test!(@error #[repr(i16)] => StructUnionRepr => FromRawReprs(Single(UnsupportedReprError)).into());
        test!(@error #[repr(i32)] => StructUnionRepr => FromRawReprs(Single(UnsupportedReprError)).into());
        test!(@error #[repr(i64)] => StructUnionRepr => FromRawReprs(Single(UnsupportedReprError)).into());
        test!(@error #[repr(isize)] => StructUnionRepr => FromRawReprs(Single(UnsupportedReprError)).into());

        // Illegal enum reprs
        test!(@error #[repr(packed)] => EnumRepr => FromRawReprs(Single(UnsupportedReprError)).into());
        test!(@error #[repr(packed(1))] => EnumRepr => FromRawReprs(Single(UnsupportedReprError)).into());
        test!(@error #[repr(packed(2))] => EnumRepr => FromRawReprs(Single(UnsupportedReprError)).into());
    }
}
