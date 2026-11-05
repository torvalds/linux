// SPDX-License-Identifier: Apache-2.0 OR MIT

// When fixdep scans this, it will find this string `CONFIG_RUSTC_VERSION_TEXT`
// and thus add a dependency on `include/config/RUSTC_VERSION_TEXT`, which is
// touched by Kconfig when the version string from the compiler changes.

//! `pin-init` proc macros.

// Documentation is done in the pin-init crate instead.
#![allow(missing_docs)]

use proc_macro::TokenStream;

use crate::diagnostics::DiagCtxt;

mod diagnostics;
mod init;
mod pin_data;
mod pinned_drop;
mod util;
mod zeroable;

#[proc_macro_attribute]
pub fn pin_data(args: TokenStream, input: TokenStream) -> TokenStream {
    DiagCtxt::for_item(|dcx| pin_data::expand_with_cfg(syn::parse(args)?, syn::parse(input)?, dcx))
        .into()
}

#[proc_macro_attribute]
pub fn pinned_drop(args: TokenStream, input: TokenStream) -> TokenStream {
    DiagCtxt::for_item(|dcx| pinned_drop::pinned_drop(syn::parse(args)?, syn::parse(input)?, dcx))
        .into()
}

#[proc_macro_derive(Zeroable)]
pub fn derive_zeroable(input: TokenStream) -> TokenStream {
    DiagCtxt::for_item(|dcx| zeroable::derive(syn::parse(input)?, dcx)).into()
}

#[proc_macro_derive(MaybeZeroable)]
pub fn maybe_derive_zeroable(input: TokenStream) -> TokenStream {
    DiagCtxt::for_item(|dcx| zeroable::maybe_derive(syn::parse(input)?, dcx)).into()
}
#[proc_macro]
pub fn init(input: TokenStream) -> TokenStream {
    DiagCtxt::for_expr(|dcx| {
        init::expand_with_cfg(
            syn::parse(input)?,
            Some("::core::convert::Infallible"),
            false,
            dcx,
        )
    })
    .into()
}

#[proc_macro]
pub fn pin_init(input: TokenStream) -> TokenStream {
    DiagCtxt::for_expr(|dcx| {
        init::expand_with_cfg(
            syn::parse(input)?,
            Some("::core::convert::Infallible"),
            true,
            dcx,
        )
    })
    .into()
}
