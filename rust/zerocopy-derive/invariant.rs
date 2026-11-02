// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT
//
// Copyright 2026 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

use proc_macro2::{Ident, Span, TokenStream, TokenTree};
use quote::{quote, ToTokens as _};
use syn::{
    punctuated::Punctuated, spanned::Spanned as _, Attribute, Data, DeriveInput, Error, Expr,
    Field, MacroDelimiter, Meta, Token,
};

use crate::util::path_is_ident;

/// Parses field invariants in attribute order.
pub(crate) fn parse(attrs: &[Attribute]) -> Result<Vec<Expr>, Error> {
    let mut invariants = Vec::new();
    for attr in attrs.iter().filter(|attr| path_is_ident(attr.path(), "zerocopy")) {
        let options = attr.parse_args_with(Punctuated::<Meta, Token![,]>::parse_terminated)?;
        for option in options {
            if !path_is_ident(option.path(), "invariant") {
                return Err(Error::new_spanned(option, "expected `invariant(...)`"));
            }
            match option {
                Meta::List(list) if matches!(list.delimiter, MacroDelimiter::Paren(_)) => {
                    invariants.push(list.parse_args::<Expr>()?)
                }
                other => return Err(Error::new_spanned(other, "expected `invariant(...)`")),
            }
        }
    }
    Ok(invariants)
}

/// Validates invariant syntax and placement, returning the first invariant's
/// span so derives which cannot honor invariants can reject them.
pub(crate) fn validate(ast: &DeriveInput) -> Result<Option<Span>, Error> {
    let fields: Vec<&Field> = match &ast.data {
        Data::Struct(data) => data.fields.iter().collect(),
        Data::Enum(data) => {
            for variant in &data.variants {
                if let Some(invariant) = parse(&variant.attrs)?.first() {
                    return Err(Error::new(
                        invariant.span(),
                        "invariants are only supported on named fields",
                    ));
                }
            }
            data.variants.iter().flat_map(|variant| &variant.fields).collect()
        }
        Data::Union(data) => data.fields.named.iter().collect(),
    };
    let mut first = None;
    for field in fields {
        if let Some(invariant) = parse(&field.attrs)?.first() {
            if field.ident.is_none() {
                return Err(Error::new(
                    invariant.span(),
                    "invariants are only supported on named fields",
                ));
            }
            first.get_or_insert(invariant.span());
        }
    }
    Ok(first)
}

/// Visits identifiers, including those inside macro arguments.
pub(crate) fn idents(tokens: TokenStream) -> Vec<Ident> {
    tokens
        .into_iter()
        .flat_map(|token| match token {
            TokenTree::Ident(ident) => vec![ident],
            TokenTree::Group(group) => idents(group.stream()),
            _ => Vec::new(),
        })
        .collect()
}

/// Binds semantic field names in every syntax context used by the expression.
/// Preserve the expression's tokens so its other names retain their hygiene.
pub(crate) fn bind_fields(fields: &[(&Field, Ident)], expression: &Expr) -> TokenStream {
    let mut body = expression.to_token_stream();
    let references = idents(body.clone());
    for (field, binding) in fields {
        let name = match &field.ident {
            Some(name) => name,
            None => continue,
        };
        let spelling = name.to_string();
        let spelling = spelling.trim_start_matches("r#");
        // Also support references produced by a macro invoked by the expression.
        let mut call_site = name.clone();
        call_site.set_span(Span::call_site());
        for alias in [name.clone(), call_site].into_iter().chain(
            references
                .iter()
                .filter(|ident| ident.to_string().trim_start_matches("r#") == spelling)
                .cloned(),
        ) {
            body = quote! {{
                // Require a binding; reject names Rust would resolve as constant
                // patterns. Item aliases could capture helper calls in macros.
                #[allow(unused_variables, non_snake_case, clippy::redundant_pattern)]
                let #alias @ _ = #binding;
                #body
            }};
        }
    }
    body
}
