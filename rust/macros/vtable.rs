// SPDX-License-Identifier: GPL-2.0

use std::{
    collections::HashSet,
    iter::Extend, //
};

use proc_macro2::{
    Ident,
    TokenStream, //
};
use quote::{
    quote,
    ToTokens, //
};
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

    gen_items.push(parse_quote! {
         /// A marker to prevent implementors from forgetting to use [`#[vtable]`](vtable)
         /// attribute when implementing this trait.
         const USE_VTABLE_ATTR: ();
    });

    // Add `type OwnerModule: ModuleMetadata` as a required associated type if
    // the trait does not already define it.
    if !item
        .items
        .iter()
        .any(|i| matches!(i, TraitItem::Type(t) if t.ident == "OwnerModule"))
    {
        gen_items.push(parse_quote! {
            /// The module implementing this vtable trait.
            ///
            /// Automatically set to `crate::LocalModule` by the `#[vtable]`
            /// impl macro.
            type OwnerModule: ::kernel::ModuleMetadata;
        });
    }

    for item in &mut item.items {
        if let TraitItem::Fn(fn_item) = item {
            let name = &fn_item.sig.ident;
            let gen_const_name = Ident::new(
                &format!("HAS_{}", name.to_string().to_uppercase()),
                name.span(),
            );

            if fn_item
                .attrs
                .extract_if(.., |attr| attr.path().is_ident("optional"))
                .count()
                != 0
            {
                if let Some(default) = &fn_item.default {
                    Err(Error::new_spanned(
                        default,
                        "`#[optional]` methods must not have a default implementation",
                    ))?;
                }

                // Optional methods in Rust need a default implementation. Inject one that fails
                // the build at compile time if not overridden.
                fn_item.default = Some(parse_quote!({
                    ::kernel::build_assert::build_error!(
                        "This function must not be called, see the `#[vtable]` documentation."
                    );
                }));
                // Ensure that the function is never code generated unless used.
                fn_item.attrs.push(parse_quote!(#[inline]));
            }

            // We don't know on the implementation-site whether a method is required or provided
            // so we have to generate a const for all methods.
            // However, hide it for required methods as it will always be true.
            let doc = if fn_item.default.is_some() {
                let comment =
                    format!("Indicates if the `{name}` method is overridden by the implementor.");
                quote!(#[doc = #comment])
            } else {
                quote!(#[doc(hidden)])
            };
            let cfg_attrs = crate::helpers::gather_cfg_attrs(&fn_item.attrs);
            gen_items.push(parse_quote! {
                #(#cfg_attrs)*
                #doc
                const #gen_const_name: bool = false;
            });
        }
    }

    item.items.extend(gen_items);
    Ok(item)
}

fn handle_impl(mut item: ItemImpl) -> Result<ItemImpl> {
    let mut gen_items = Vec::new();
    let mut defined_items = HashSet::new();

    // Iterate over all user-defined items to gather any possible explicit overrides.
    for item in &item.items {
        match item {
            ImplItem::Const(const_item) => {
                defined_items.insert(const_item.ident.clone());
            }
            ImplItem::Type(type_item) => {
                defined_items.insert(type_item.ident.clone());
            }
            _ => {}
        }
    }

    gen_items.push(parse_quote! {
        const USE_VTABLE_ATTR: () = ();
    });

    // Auto-insert `type OwnerModule = crate::LocalModule` if not explicitly defined.
    // `crate::LocalModule` resolves to the real module type (via `module!`) or a
    // dummy fallback in non-module contexts (e.g., doctests).
    if !defined_items.contains(&parse_quote!(OwnerModule)) {
        gen_items.push(parse_quote! {
            type OwnerModule = crate::LocalModule;
        });
    }

    for item in &item.items {
        if let ImplItem::Fn(fn_item) = item {
            let name = &fn_item.sig.ident;
            let gen_const_name = Ident::new(
                &format!("HAS_{}", name.to_string().to_uppercase()),
                name.span(),
            );
            // Skip if it's declared already -- this allows user override.
            if defined_items.contains(&gen_const_name) {
                continue;
            }
            let cfg_attrs = crate::helpers::gather_cfg_attrs(&fn_item.attrs);
            gen_items.push(parse_quote! {
                #(#cfg_attrs)*
                const #gen_const_name: bool = true;
            });
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
