// SPDX-License-Identifier: Apache-2.0 OR MIT

// When fixdep scans this, it will find this string `CONFIG_RUSTC_VERSION_TEXT`
// and thus add a dependency on `include/config/RUSTC_VERSION_TEXT`, which is
// touched by Kconfig when the version string from the compiler changes.

//! `pin-init` proc macros.

#![cfg_attr(not(RUSTC_LINT_REASONS_IS_STABLE), feature(lint_reasons))]
// Allow `.into()` to convert
// - `proc_macro2::TokenStream` into `proc_macro::TokenStream` in the user-space version.
// - `proc_macro::TokenStream` into `proc_macro::TokenStream` in the kernel version.
//   Clippy warns on this conversion, but it's required by the user-space version.
//
// Remove once we have `proc_macro2` in the kernel.
#![allow(clippy::useless_conversion)]
// Documentation is done in the pin-init crate instead.
#![allow(missing_docs)]

use proc_macro::TokenStream;

#[cfg(kernel)]
#[path = "../../../macros/quote.rs"]
#[macro_use]
#[cfg_attr(not(kernel), rustfmt::skip)]
mod quote;
#[cfg(not(kernel))]
#[macro_use]
extern crate quote;

mod helpers;
mod pin_data;
mod pinned_drop;
mod zeroable;

#[proc_macro_attribute]
pub fn pin_data(inner: TokenStream, item: TokenStream) -> TokenStream {
    pin_data::pin_data(inner.into(), item.into()).into()
}

#[proc_macro_attribute]
pub fn pinned_drop(args: TokenStream, input: TokenStream) -> TokenStream {
    pinned_drop::pinned_drop(args.into(), input.into()).into()
}

#[proc_macro_derive(Zeroable)]
pub fn derive_zeroable(input: TokenStream) -> TokenStream {
    zeroable::derive(input.into()).into()
}

#[proc_macro_derive(MaybeZeroable)]
pub fn maybe_derive_zeroable(input: TokenStream) -> TokenStream {
    zeroable::maybe_derive(input.into()).into()
}
