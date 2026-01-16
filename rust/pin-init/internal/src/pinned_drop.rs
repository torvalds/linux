// SPDX-License-Identifier: Apache-2.0 OR MIT

use proc_macro2::TokenStream;
use quote::quote;
use syn::{parse::Nothing, parse_quote, spanned::Spanned, ImplItem, ItemImpl, Token};

use crate::diagnostics::{DiagCtxt, ErrorGuaranteed};

pub(crate) fn pinned_drop(
    _args: Nothing,
    mut input: ItemImpl,
    dcx: &mut DiagCtxt,
) -> Result<TokenStream, ErrorGuaranteed> {
    if let Some(unsafety) = input.unsafety {
        dcx.error(unsafety, "implementing `PinnedDrop` is safe");
    }
    input.unsafety = Some(Token![unsafe](input.impl_token.span));
    match &mut input.trait_ {
        Some((not, path, _for)) => {
            if let Some(not) = not {
                dcx.error(not, "cannot implement `!PinnedDrop`");
            }
            for (seg, expected) in path
                .segments
                .iter()
                .rev()
                .zip(["PinnedDrop", "pin_init", ""])
            {
                if expected.is_empty() || seg.ident != expected {
                    dcx.error(seg, "bad import path for `PinnedDrop`");
                }
                if !seg.arguments.is_none() {
                    dcx.error(&seg.arguments, "unexpected arguments for `PinnedDrop` path");
                }
            }
            *path = parse_quote!(::pin_init::PinnedDrop);
        }
        None => {
            let span = input
                .impl_token
                .span
                .join(input.self_ty.span())
                .unwrap_or(input.impl_token.span);
            dcx.error(
                span,
                "expected `impl ... PinnedDrop for ...`, got inherent impl",
            );
        }
    }
    for item in &mut input.items {
        if let ImplItem::Fn(fn_item) = item {
            if fn_item.sig.ident == "drop" {
                fn_item
                    .sig
                    .inputs
                    .push(parse_quote!(_: ::pin_init::__internal::OnlyCallFromDrop));
            }
        }
    }
    Ok(quote!(#input))
}
