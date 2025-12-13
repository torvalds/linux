// SPDX-License-Identifier: Apache-2.0 OR MIT

use proc_macro::{TokenStream, TokenTree};

pub(crate) trait ToTokens {
    fn to_tokens(&self, tokens: &mut TokenStream);
}

impl<T: ToTokens> ToTokens for Option<T> {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        if let Some(v) = self {
            v.to_tokens(tokens);
        }
    }
}

impl ToTokens for proc_macro::Group {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.extend([TokenTree::from(self.clone())]);
    }
}

impl ToTokens for proc_macro::Ident {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.extend([TokenTree::from(self.clone())]);
    }
}

impl ToTokens for TokenTree {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.extend([self.clone()]);
    }
}

impl ToTokens for TokenStream {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.extend(self.clone());
    }
}

/// Converts tokens into [`proc_macro::TokenStream`] and performs variable interpolations with
/// the given span.
///
/// This is a similar to the
/// [`quote_spanned!`](https://docs.rs/quote/latest/quote/macro.quote_spanned.html) macro from the
/// `quote` crate but provides only just enough functionality needed by the current `macros` crate.
macro_rules! quote_spanned {
    ($span:expr => $($tt:tt)*) => {{
        let mut tokens = ::proc_macro::TokenStream::new();
        {
            let span = $span;
            quote_spanned!(@proc tokens span $($tt)*);
        }
        tokens
    }};
    (@proc $v:ident $span:ident) => {};
    (@proc $v:ident $span:ident #$id:ident $($tt:tt)*) => {
        $crate::quote::ToTokens::to_tokens(&$id, &mut $v);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident #(#$id:ident)* $($tt:tt)*) => {
        for token in $id {
            $crate::quote::ToTokens::to_tokens(&token, &mut $v);
        }
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident ( $($inner:tt)* ) $($tt:tt)*) => {
        #[allow(unused_mut)]
        let mut tokens = ::proc_macro::TokenStream::new();
        quote_spanned!(@proc tokens $span $($inner)*);
        $v.extend([::proc_macro::TokenTree::Group(::proc_macro::Group::new(
            ::proc_macro::Delimiter::Parenthesis,
            tokens,
        ))]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident [ $($inner:tt)* ] $($tt:tt)*) => {
        let mut tokens = ::proc_macro::TokenStream::new();
        quote_spanned!(@proc tokens $span $($inner)*);
        $v.extend([::proc_macro::TokenTree::Group(::proc_macro::Group::new(
            ::proc_macro::Delimiter::Bracket,
            tokens,
        ))]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident { $($inner:tt)* } $($tt:tt)*) => {
        let mut tokens = ::proc_macro::TokenStream::new();
        quote_spanned!(@proc tokens $span $($inner)*);
        $v.extend([::proc_macro::TokenTree::Group(::proc_macro::Group::new(
            ::proc_macro::Delimiter::Brace,
            tokens,
        ))]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident :: $($tt:tt)*) => {
        $v.extend([::proc_macro::Spacing::Joint, ::proc_macro::Spacing::Alone].map(|spacing| {
            ::proc_macro::TokenTree::Punct(::proc_macro::Punct::new(':', spacing))
        }));
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident : $($tt:tt)*) => {
        $v.extend([::proc_macro::TokenTree::Punct(
            ::proc_macro::Punct::new(':', ::proc_macro::Spacing::Alone),
        )]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident , $($tt:tt)*) => {
        $v.extend([::proc_macro::TokenTree::Punct(
            ::proc_macro::Punct::new(',', ::proc_macro::Spacing::Alone),
        )]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident @ $($tt:tt)*) => {
        $v.extend([::proc_macro::TokenTree::Punct(
            ::proc_macro::Punct::new('@', ::proc_macro::Spacing::Alone),
        )]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident ! $($tt:tt)*) => {
        $v.extend([::proc_macro::TokenTree::Punct(
            ::proc_macro::Punct::new('!', ::proc_macro::Spacing::Alone),
        )]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident ; $($tt:tt)*) => {
        $v.extend([::proc_macro::TokenTree::Punct(
            ::proc_macro::Punct::new(';', ::proc_macro::Spacing::Alone),
        )]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident + $($tt:tt)*) => {
        $v.extend([::proc_macro::TokenTree::Punct(
            ::proc_macro::Punct::new('+', ::proc_macro::Spacing::Alone),
        )]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident = $($tt:tt)*) => {
        $v.extend([::proc_macro::TokenTree::Punct(
            ::proc_macro::Punct::new('=', ::proc_macro::Spacing::Alone),
        )]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident # $($tt:tt)*) => {
        $v.extend([::proc_macro::TokenTree::Punct(
            ::proc_macro::Punct::new('#', ::proc_macro::Spacing::Alone),
        )]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident _ $($tt:tt)*) => {
        $v.extend([::proc_macro::TokenTree::Ident(
            ::proc_macro::Ident::new("_", $span),
        )]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
    (@proc $v:ident $span:ident $id:ident $($tt:tt)*) => {
        $v.extend([::proc_macro::TokenTree::Ident(
            ::proc_macro::Ident::new(stringify!($id), $span),
        )]);
        quote_spanned!(@proc $v $span $($tt)*);
    };
}

/// Converts tokens into [`proc_macro::TokenStream`] and performs variable interpolations with
/// mixed site span ([`Span::mixed_site()`]).
///
/// This is a similar to the [`quote!`](https://docs.rs/quote/latest/quote/macro.quote.html) macro
/// from the `quote` crate but provides only just enough functionality needed by the current
/// `macros` crate.
///
/// [`Span::mixed_site()`]: https://doc.rust-lang.org/proc_macro/struct.Span.html#method.mixed_site
macro_rules! quote {
    ($($tt:tt)*) => {
        quote_spanned!(::proc_macro::Span::mixed_site() => $($tt)*)
    }
}
