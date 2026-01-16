// SPDX-License-Identifier: Apache-2.0 OR MIT

// When fixdep scans this, it will find this string `CONFIG_RUSTC_VERSION_TEXT`
// and thus add a dependency on `include/config/RUSTC_VERSION_TEXT`, which is
// touched by Kconfig when the version string from the compiler changes.

//! `pin-init` proc macros.

#![cfg_attr(not(RUSTC_LINT_REASONS_IS_STABLE), feature(lint_reasons))]
// Documentation is done in the pin-init crate instead.
#![allow(missing_docs)]

use proc_macro::TokenStream;
use syn::parse_macro_input;

use crate::diagnostics::DiagCtxt;

mod diagnostics;
mod init;
mod pin_data;
mod pinned_drop;
mod zeroable;

#[proc_macro_attribute]
pub fn pin_data(args: TokenStream, input: TokenStream) -> TokenStream {
    let args = parse_macro_input!(args);
    let input = parse_macro_input!(input);
    DiagCtxt::with(|dcx| pin_data::pin_data(args, input, dcx)).into()
}

#[proc_macro_attribute]
pub fn pinned_drop(args: TokenStream, input: TokenStream) -> TokenStream {
    let args = parse_macro_input!(args);
    let input = parse_macro_input!(input);
    DiagCtxt::with(|dcx| pinned_drop::pinned_drop(args, input, dcx)).into()
}

#[proc_macro_derive(Zeroable)]
pub fn derive_zeroable(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input);
    DiagCtxt::with(|dcx| zeroable::derive(input, dcx)).into()
}

#[proc_macro_derive(MaybeZeroable)]
pub fn maybe_derive_zeroable(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input);
    DiagCtxt::with(|dcx| zeroable::maybe_derive(input, dcx)).into()
}
#[proc_macro]
pub fn init(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input);
    DiagCtxt::with(|dcx| init::expand(input, Some("::core::convert::Infallible"), false, dcx))
        .into()
}

#[proc_macro]
pub fn pin_init(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input);
    DiagCtxt::with(|dcx| init::expand(input, Some("::core::convert::Infallible"), true, dcx)).into()
}
