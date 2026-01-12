// SPDX-License-Identifier: GPL-2.0

use proc_macro2::{
    Ident,
    TokenStream,
    TokenTree, //
};
use syn::{
    parse::{
        Parse,
        ParseStream, //
    },
    Result,
    Token, //
};

pub(crate) struct Input {
    a: Ident,
    _comma: Token![,],
    b: Ident,
}

impl Parse for Input {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        Ok(Self {
            a: input.parse()?,
            _comma: input.parse()?,
            b: input.parse()?,
        })
    }
}

pub(crate) fn concat_idents(Input { a, b, .. }: Input) -> TokenStream {
    let res = Ident::new(&format!("{a}{b}"), b.span());
    TokenStream::from_iter([TokenTree::Ident(res)])
}
