/// Define a type that supports parsing and printing a given identifier as if it
/// were a keyword.
///
/// # Usage
///
/// As a convention, it is recommended that this macro be invoked within a
/// module called `kw` or `keyword` and that the resulting parser be invoked
/// with a `kw::` or `keyword::` prefix.
///
/// ```
/// mod kw {
///     syn::custom_keyword!(whatever);
/// }
/// ```
///
/// The generated syntax tree node supports the following operations just like
/// any built-in keyword token.
///
/// - [Peeking] — `input.peek(kw::whatever)`
///
/// - [Parsing] — `input.parse::<kw::whatever>()?`
///
/// - [Printing] — `quote!( ... #whatever_token ... )`
///
/// - Construction from a [`Span`] — `let whatever_token = kw::whatever(sp)`
///
/// - Field access to its span — `let sp = whatever_token.span`
///
/// [Peeking]: crate::parse::ParseBuffer::peek
/// [Parsing]: crate::parse::ParseBuffer::parse
/// [Printing]: quote::ToTokens
/// [`Span`]: proc_macro2::Span
///
/// # Example
///
/// This example parses input that looks like `bool = true` or `str = "value"`.
/// The key must be either the identifier `bool` or the identifier `str`. If
/// `bool`, the value may be either `true` or `false`. If `str`, the value may
/// be any string literal.
///
/// The symbols `bool` and `str` are not reserved keywords in Rust so these are
/// not considered keywords in the `syn::token` module. Like any other
/// identifier that is not a keyword, these can be declared as custom keywords
/// by crates that need to use them as such.
///
/// ```
/// use syn::{LitBool, LitStr, Result, Token};
/// use syn::parse::{Parse, ParseStream};
///
/// mod kw {
///     syn::custom_keyword!(bool);
///     syn::custom_keyword!(str);
/// }
///
/// enum Argument {
///     Bool {
///         bool_token: kw::bool,
///         eq_token: Token![=],
///         value: LitBool,
///     },
///     Str {
///         str_token: kw::str,
///         eq_token: Token![=],
///         value: LitStr,
///     },
/// }
///
/// impl Parse for Argument {
///     fn parse(input: ParseStream) -> Result<Self> {
///         let lookahead = input.lookahead1();
///         if lookahead.peek(kw::bool) {
///             Ok(Argument::Bool {
///                 bool_token: input.parse::<kw::bool>()?,
///                 eq_token: input.parse()?,
///                 value: input.parse()?,
///             })
///         } else if lookahead.peek(kw::str) {
///             Ok(Argument::Str {
///                 str_token: input.parse::<kw::str>()?,
///                 eq_token: input.parse()?,
///                 value: input.parse()?,
///             })
///         } else {
///             Err(lookahead.error())
///         }
///     }
/// }
/// ```
#[macro_export]
macro_rules! custom_keyword {
    ($ident:ident) => {
        #[allow(non_camel_case_types)]
        pub struct $ident {
            #[allow(dead_code)]
            pub span: $crate::__private::Span,
        }

        #[doc(hidden)]
        #[allow(dead_code, non_snake_case)]
        pub fn $ident<__S: $crate::__private::IntoSpans<$crate::__private::Span>>(
            span: __S,
        ) -> $ident {
            $ident {
                span: $crate::__private::IntoSpans::into_spans(span),
            }
        }

        const _: () = {
            impl $crate::__private::Default for $ident {
                fn default() -> Self {
                    $ident {
                        span: $crate::__private::Span::call_site(),
                    }
                }
            }

            $crate::impl_parse_for_custom_keyword!($ident);
            $crate::impl_to_tokens_for_custom_keyword!($ident);
            $crate::impl_clone_for_custom_keyword!($ident);
            $crate::impl_extra_traits_for_custom_keyword!($ident);
        };
    };
}

// Not public API.
#[cfg(feature = "parsing")]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_parse_for_custom_keyword {
    ($ident:ident) => {
        // For peek.
        impl $crate::__private::CustomToken for $ident {
            fn peek(cursor: $crate::buffer::Cursor) -> $crate::__private::bool {
                if let $crate::__private::Some((ident, _rest)) = cursor.ident() {
                    ident == $crate::__private::stringify!($ident)
                } else {
                    false
                }
            }

            fn display() -> &'static $crate::__private::str {
                $crate::__private::concat!("`", $crate::__private::stringify!($ident), "`")
            }
        }

        impl $crate::parse::Parse for $ident {
            fn parse(input: $crate::parse::ParseStream) -> $crate::parse::Result<$ident> {
                input.step(|cursor| {
                    if let $crate::__private::Some((ident, rest)) = cursor.ident() {
                        if ident == $crate::__private::stringify!($ident) {
                            return $crate::__private::Ok(($ident { span: ident.span() }, rest));
                        }
                    }
                    $crate::__private::Err(cursor.error($crate::__private::concat!(
                        "expected `",
                        $crate::__private::stringify!($ident),
                        "`",
                    )))
                })
            }
        }
    };
}

// Not public API.
#[cfg(not(feature = "parsing"))]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_parse_for_custom_keyword {
    ($ident:ident) => {};
}

// Not public API.
#[cfg(feature = "printing")]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_to_tokens_for_custom_keyword {
    ($ident:ident) => {
        impl $crate::__private::ToTokens for $ident {
            fn to_tokens(&self, tokens: &mut $crate::__private::TokenStream2) {
                let ident = $crate::Ident::new($crate::__private::stringify!($ident), self.span);
                $crate::__private::TokenStreamExt::append(tokens, ident);
            }
        }
    };
}

// Not public API.
#[cfg(not(feature = "printing"))]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_to_tokens_for_custom_keyword {
    ($ident:ident) => {};
}

// Not public API.
#[cfg(feature = "clone-impls")]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_clone_for_custom_keyword {
    ($ident:ident) => {
        impl $crate::__private::Copy for $ident {}

        #[allow(clippy::expl_impl_clone_on_copy)]
        impl $crate::__private::Clone for $ident {
            fn clone(&self) -> Self {
                *self
            }
        }
    };
}

// Not public API.
#[cfg(not(feature = "clone-impls"))]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_clone_for_custom_keyword {
    ($ident:ident) => {};
}

// Not public API.
#[cfg(feature = "extra-traits")]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_extra_traits_for_custom_keyword {
    ($ident:ident) => {
        impl $crate::__private::Debug for $ident {
            fn fmt(&self, f: &mut $crate::__private::Formatter) -> $crate::__private::FmtResult {
                $crate::__private::Formatter::write_str(
                    f,
                    $crate::__private::concat!(
                        "Keyword [",
                        $crate::__private::stringify!($ident),
                        "]",
                    ),
                )
            }
        }

        impl $crate::__private::Eq for $ident {}

        impl $crate::__private::PartialEq for $ident {
            fn eq(&self, _other: &Self) -> $crate::__private::bool {
                true
            }
        }

        impl $crate::__private::Hash for $ident {
            fn hash<__H: $crate::__private::Hasher>(&self, _state: &mut __H) {}
        }
    };
}

// Not public API.
#[cfg(not(feature = "extra-traits"))]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_extra_traits_for_custom_keyword {
    ($ident:ident) => {};
}
