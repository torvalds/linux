// SPDX-License-Identifier: Apache-2.0 OR MIT

// When fixdep scans this, it will find this string `CONFIG_RUSTC_VERSION_TEXT`
// and thus add a dependency on `include/config/RUSTC_VERSION_TEXT`, which is
// touched by Kconfig when the version string from the compiler changes.

//! `pin-init` proc macros.

#![cfg_attr(not(RUSTC_LINT_REASONS_IS_STABLE), feature(lint_reasons))]

use proc_macro::TokenStream;

#[cfg(kernel)]
#[path = "../../../macros/quote.rs"]
#[macro_use]
mod quote;

mod helpers;
mod pin_data;
mod pinned_drop;
mod zeroable;

#[allow(missing_docs)]
#[proc_macro_attribute]
pub fn pin_data(inner: TokenStream, item: TokenStream) -> TokenStream {
    pin_data::pin_data(inner, item)
}

#[allow(missing_docs)]
#[proc_macro_attribute]
pub fn pinned_drop(args: TokenStream, input: TokenStream) -> TokenStream {
    pinned_drop::pinned_drop(args, input)
}

#[allow(missing_docs)]
#[proc_macro_derive(Zeroable)]
pub fn derive_zeroable(input: TokenStream) -> TokenStream {
    zeroable::derive(input)
}
