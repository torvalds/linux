// SPDX-License-Identifier: GPL-2.0

use proc_macro2::{
    token_stream,
    Ident,
    TokenStream,
    TokenTree, //
};
use quote::ToTokens;
use syn::{
    parse::{
        Parse,
        ParseStream, //
    },
    Error,
    LitStr,
    Result, //
};

pub(crate) fn expect_punct(it: &mut token_stream::IntoIter) -> char {
    if let TokenTree::Punct(punct) = it.next().expect("Reached end of token stream for Punct") {
        punct.as_char()
    } else {
        panic!("Expected Punct");
    }
}

/// A string literal that is required to have ASCII value only.
pub(crate) struct AsciiLitStr(LitStr);

impl Parse for AsciiLitStr {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        let s: LitStr = input.parse()?;
        if !s.value().is_ascii() {
            return Err(Error::new_spanned(s, "expected ASCII-only string literal"));
        }
        Ok(Self(s))
    }
}

impl ToTokens for AsciiLitStr {
    fn to_tokens(&self, ts: &mut TokenStream) {
        self.0.to_tokens(ts);
    }
}

impl AsciiLitStr {
    pub(crate) fn value(&self) -> String {
        self.0.value()
    }
}

/// Given a function declaration, finds the name of the function.
pub(crate) fn function_name(input: TokenStream) -> Option<Ident> {
    let mut input = input.into_iter();
    while let Some(token) = input.next() {
        match token {
            TokenTree::Ident(i) if i == "fn" => {
                if let Some(TokenTree::Ident(i)) = input.next() {
                    return Some(i);
                }
                return None;
            }
            _ => continue,
        }
    }
    None
}

pub(crate) fn file() -> String {
    #[cfg(not(CONFIG_RUSTC_HAS_SPAN_FILE))]
    {
        proc_macro::Span::call_site()
            .source_file()
            .path()
            .to_string_lossy()
            .into_owned()
    }

    #[cfg(CONFIG_RUSTC_HAS_SPAN_FILE)]
    #[allow(clippy::incompatible_msrv)]
    {
        proc_macro::Span::call_site().file()
    }
}
