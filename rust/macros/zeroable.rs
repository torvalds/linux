// SPDX-License-Identifier: GPL-2.0

use crate::helpers::{parse_generics, Generics};
use proc_macro::{TokenStream, TokenTree};

pub(crate) fn derive(input: TokenStream) -> TokenStream {
    let (
        Generics {
            impl_generics,
            decl_generics: _,
            ty_generics,
        },
        mut rest,
    ) = parse_generics(input);
    // This should be the body of the struct `{...}`.
    let last = rest.pop();
    // Now we insert `Zeroable` as a bound for every generic parameter in `impl_generics`.
    let mut new_impl_generics = Vec::with_capacity(impl_generics.len());
    // Are we inside of a generic where we want to add `Zeroable`?
    let mut in_generic = !impl_generics.is_empty();
    // Have we already inserted `Zeroable`?
    let mut inserted = false;
    // Level of `<>` nestings.
    let mut nested = 0;
    for tt in impl_generics {
        match &tt {
            // If we find a `,`, then we have finished a generic/constant/lifetime parameter.
            TokenTree::Punct(p) if nested == 0 && p.as_char() == ',' => {
                if in_generic && !inserted {
                    new_impl_generics.extend(quote! { : ::kernel::init::Zeroable });
                }
                in_generic = true;
                inserted = false;
                new_impl_generics.push(tt);
            }
            // If we find `'`, then we are entering a lifetime.
            TokenTree::Punct(p) if nested == 0 && p.as_char() == '\'' => {
                in_generic = false;
                new_impl_generics.push(tt);
            }
            TokenTree::Punct(p) if nested == 0 && p.as_char() == ':' => {
                new_impl_generics.push(tt);
                if in_generic {
                    new_impl_generics.extend(quote! { ::kernel::init::Zeroable + });
                    inserted = true;
                }
            }
            TokenTree::Punct(p) if p.as_char() == '<' => {
                nested += 1;
                new_impl_generics.push(tt);
            }
            TokenTree::Punct(p) if p.as_char() == '>' => {
                assert!(nested > 0);
                nested -= 1;
                new_impl_generics.push(tt);
            }
            _ => new_impl_generics.push(tt),
        }
    }
    assert_eq!(nested, 0);
    if in_generic && !inserted {
        new_impl_generics.extend(quote! { : ::kernel::init::Zeroable });
    }
    quote! {
        ::kernel::__derive_zeroable!(
            parse_input:
                @sig(#(#rest)*),
                @impl_generics(#(#new_impl_generics)*),
                @ty_generics(#(#ty_generics)*),
                @body(#last),
        );
    }
}
