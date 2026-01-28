// SPDX-License-Identifier: GPL-2.0

use proc_macro2::TokenStream;
use quote::ToTokens;
use syn::{
    parse::{
        Parse,
        ParseStream, //
    },
    Attribute,
    Error,
    LitStr,
    Result, //
};

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

/// Obtain all `#[cfg]` attributes.
pub(crate) fn gather_cfg_attrs(attr: &[Attribute]) -> impl Iterator<Item = &Attribute> + '_ {
    attr.iter().filter(|a| a.path().is_ident("cfg"))
}
