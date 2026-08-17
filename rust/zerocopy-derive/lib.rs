// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2019 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

//! Derive macros for [zerocopy]'s traits.
//!
//! [zerocopy]: https://docs.rs/zerocopy

// Sometimes we want to use lints which were added after our MSRV.
// `unknown_lints` is `warn` by default and we deny warnings in CI, so without
// this attribute, any unknown lint would cause a CI failure when testing with
// our MSRV.
#![allow(unknown_lints)]
#![deny(renamed_and_removed_lints)]
#![deny(
    clippy::all,
    clippy::missing_safety_doc,
    clippy::multiple_unsafe_ops_per_block,
    clippy::undocumented_unsafe_blocks
)]
// We defer to own discretion on type complexity.
#![allow(clippy::type_complexity)]
// Inlining format args isn't supported on our MSRV.
#![allow(clippy::uninlined_format_args)]
#![deny(
    rustdoc::bare_urls,
    rustdoc::broken_intra_doc_links,
    rustdoc::invalid_codeblock_attributes,
    rustdoc::invalid_html_tags,
    rustdoc::invalid_rust_codeblocks,
    rustdoc::missing_crate_level_docs,
    rustdoc::private_intra_doc_links
)]
#![recursion_limit = "128"]

macro_rules! ident {
    (($fmt:literal $(, $arg:expr)*), $span:expr) => {
        syn::Ident::new(&format!($fmt $(, crate::util::to_ident_str($arg))*), $span)
    };
}

mod derive;
#[cfg(test)]
mod output_tests;
mod repr;
mod util;

use syn::{DeriveInput, Error};

use crate::util::*;

// FIXME(https://github.com/rust-lang/rust/issues/54140): Some errors could be
// made better if we could add multiple lines of error output like this:
//
// error: unsupported representation
//   --> enum.rs:28:8
//    |
// 28 | #[repr(transparent)]
//    |
// help: required by the derive of FromBytes
//
// Instead, we have more verbose error messages like "unsupported representation
// for deriving FromZeros, FromBytes, IntoBytes, or Unaligned on an enum"
//
// This will probably require Span::error
// (https://doc.rust-lang.org/nightly/proc_macro/struct.Span.html#method.error),
// which is currently unstable. Revisit this once it's stable.

/// Defines a derive function named `$outer` which parses its input
/// `TokenStream` as a `DeriveInput` and then invokes the `$inner` function.
///
/// Note that the separate `$outer` parameter is required - proc macro functions
/// are currently required to live at the crate root, and so the caller must
/// specify the name in order to avoid name collisions.
macro_rules! derive {
    ($trait:ident => $outer:ident => $inner:path) => {
        #[proc_macro_derive($trait, attributes(zerocopy))]
        pub fn $outer(ts: proc_macro::TokenStream) -> proc_macro::TokenStream {
            let ast = syn::parse_macro_input!(ts as DeriveInput);
            let ctx = match Ctx::try_from_derive_input(ast) {
                Ok(ctx) => ctx,
                Err(e) => return e.into_compile_error().into(),
            };
            let ts = $inner(&ctx, Trait::$trait).into_ts();
            // We wrap in `const_block` as a backstop in case any derive fails
            // to wrap its output in `const_block` (and thus fails to annotate)
            // with the full set of `#[allow(...)]` attributes).
            let ts = const_block([Some(ts)]);
            #[cfg(test)]
            crate::util::testutil::check_hygiene(ts.clone());
            ts.into()
        }
    };
}

trait IntoTokenStream {
    fn into_ts(self) -> proc_macro2::TokenStream;
}

impl IntoTokenStream for proc_macro2::TokenStream {
    fn into_ts(self) -> proc_macro2::TokenStream {
        self
    }
}

impl IntoTokenStream for Result<proc_macro2::TokenStream, Error> {
    fn into_ts(self) -> proc_macro2::TokenStream {
        match self {
            Ok(ts) => ts,
            Err(err) => err.to_compile_error(),
        }
    }
}

derive!(KnownLayout => derive_known_layout => crate::derive::known_layout::derive);
derive!(Immutable => derive_immutable => crate::derive::derive_immutable);
derive!(TryFromBytes => derive_try_from_bytes => crate::derive::try_from_bytes::derive_try_from_bytes);
derive!(FromZeros => derive_from_zeros => crate::derive::from_bytes::derive_from_zeros);
derive!(FromBytes => derive_from_bytes => crate::derive::from_bytes::derive_from_bytes);
derive!(IntoBytes => derive_into_bytes => crate::derive::into_bytes::derive_into_bytes);
derive!(Unaligned => derive_unaligned => crate::derive::unaligned::derive_unaligned);
derive!(ByteHash => derive_hash => crate::derive::derive_hash);
derive!(ByteEq => derive_eq => crate::derive::derive_eq);
derive!(SplitAt => derive_split_at => crate::derive::derive_split_at);

/// Deprecated: prefer [`FromZeros`] instead.
#[deprecated(since = "0.8.0", note = "`FromZeroes` was renamed to `FromZeros`")]
#[doc(hidden)]
#[proc_macro_derive(FromZeroes)]
pub fn derive_from_zeroes(ts: proc_macro::TokenStream) -> proc_macro::TokenStream {
    derive_from_zeros(ts)
}

/// Deprecated: prefer [`IntoBytes`] instead.
#[deprecated(since = "0.8.0", note = "`AsBytes` was renamed to `IntoBytes`")]
#[doc(hidden)]
#[proc_macro_derive(AsBytes)]
pub fn derive_as_bytes(ts: proc_macro::TokenStream) -> proc_macro::TokenStream {
    derive_into_bytes(ts)
}
