// SPDX-License-Identifier: Apache-2.0 OR MIT

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
