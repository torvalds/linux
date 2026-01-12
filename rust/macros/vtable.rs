// SPDX-License-Identifier: GPL-2.0

use std::{
    collections::HashSet,
    iter::Extend, //
};

use proc_macro2::{
    Ident,
    TokenStream, //
};
use quote::ToTokens;
use syn::{
    parse_quote,
    Error,
    ImplItem,
    Item,
    ItemImpl,
    ItemTrait,
    Result,
    TraitItem, //
};

fn handle_trait(mut item: ItemTrait) -> Result<ItemTrait> {
    let mut gen_items = Vec::new();
    let mut gen_consts = HashSet::new();

    gen_items.push(parse_quote! {
         /// A marker to prevent implementors from forgetting to use [`#[vtable]`](vtable)
         /// attribute when implementing this trait.
         const USE_VTABLE_ATTR: ();
    });

    for item in &item.items {
        if let TraitItem::Fn(fn_item) = item {
            let name = &fn_item.sig.ident;
            let gen_const_name = Ident::new(
                &format!("HAS_{}", name.to_string().to_uppercase()),
                name.span(),
            );
            // Skip if it's declared already -- this can happen if `#[cfg]` is used to selectively
            // define functions.
            // FIXME: `#[cfg]` should be copied and propagated to the generated consts.
            if gen_consts.contains(&gen_const_name) {
                continue;
            }

            // We don't know on the implementation-site whether a method is required or provided
            // so we have to generate a const for all methods.
            let comment =
                format!("Indicates if the `{name}` method is overridden by the implementor.");
            gen_items.push(parse_quote! {
                #[doc = #comment]
                const #gen_const_name: bool = false;
            });
            gen_consts.insert(gen_const_name);
        }
    }

    item.items.extend(gen_items);
    Ok(item)
}

fn handle_impl(mut item: ItemImpl) -> Result<ItemImpl> {
    let mut gen_items = Vec::new();
    let mut defined_consts = HashSet::new();

    // Iterate over all user-defined constants to gather any possible explicit overrides.
    for item in &item.items {
        if let ImplItem::Const(const_item) = item {
            defined_consts.insert(const_item.ident.clone());
        }
    }

    gen_items.push(parse_quote! {
        const USE_VTABLE_ATTR: () = ();
    });

    for item in &item.items {
        if let ImplItem::Fn(fn_item) = item {
            let name = &fn_item.sig.ident;
            let gen_const_name = Ident::new(
                &format!("HAS_{}", name.to_string().to_uppercase()),
                name.span(),
            );
            // Skip if it's declared already -- this allows user override.
            if defined_consts.contains(&gen_const_name) {
                continue;
            }
            gen_items.push(parse_quote! {
                const #gen_const_name: bool = true;
            });
            defined_consts.insert(gen_const_name);
        }
    }

    item.items.extend(gen_items);
    Ok(item)
}

pub(crate) fn vtable(input: Item) -> Result<TokenStream> {
    match input {
        Item::Trait(item) => Ok(handle_trait(item)?.into_token_stream()),
        Item::Impl(item) => Ok(handle_impl(item)?.into_token_stream()),
        _ => Err(Error::new_spanned(
            input,
            "`#[vtable]` attribute should only be applied to trait or impl block",
        ))?,
    }
}
