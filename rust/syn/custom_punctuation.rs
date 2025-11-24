/// Define a type that supports parsing and printing a multi-character symbol
/// as if it were a punctuation token.
///
/// # Usage
///
/// ```
/// syn::custom_punctuation!(LeftRightArrow, <=>);
/// ```
///
/// The generated syntax tree node supports the following operations just like
/// any built-in punctuation token.
///
/// - [Peeking] — `input.peek(LeftRightArrow)`
///
/// - [Parsing] — `input.parse::<LeftRightArrow>()?`
///
/// - [Printing] — `quote!( ... #lrarrow ... )`
///
/// - Construction from a [`Span`] — `let lrarrow = LeftRightArrow(sp)`
///
/// - Construction from multiple [`Span`] — `let lrarrow = LeftRightArrow([sp, sp, sp])`
///
/// - Field access to its spans — `let spans = lrarrow.spans`
///
/// [Peeking]: crate::parse::ParseBuffer::peek
/// [Parsing]: crate::parse::ParseBuffer::parse
/// [Printing]: quote::ToTokens
/// [`Span`]: proc_macro2::Span
///
/// # Example
///
/// ```
/// use proc_macro2::{TokenStream, TokenTree};
/// use syn::parse::{Parse, ParseStream, Peek, Result};
/// use syn::punctuated::Punctuated;
/// use syn::Expr;
///
/// syn::custom_punctuation!(PathSeparator, </>);
///
/// // expr </> expr </> expr ...
/// struct PathSegments {
///     segments: Punctuated<Expr, PathSeparator>,
/// }
///
/// impl Parse for PathSegments {
///     fn parse(input: ParseStream) -> Result<Self> {
///         let mut segments = Punctuated::new();
///
///         let first = parse_until(input, PathSeparator)?;
///         segments.push_value(syn::parse2(first)?);
///
///         while input.peek(PathSeparator) {
///             segments.push_punct(input.parse()?);
///
///             let next = parse_until(input, PathSeparator)?;
///             segments.push_value(syn::parse2(next)?);
///         }
///
///         Ok(PathSegments { segments })
///     }
/// }
///
/// fn parse_until<E: Peek>(input: ParseStream, end: E) -> Result<TokenStream> {
///     let mut tokens = TokenStream::new();
///     while !input.is_empty() && !input.peek(end) {
///         let next: TokenTree = input.parse()?;
///         tokens.extend(Some(next));
///     }
///     Ok(tokens)
/// }
///
/// fn main() {
///     let input = r#" a::b </> c::d::e "#;
///     let _: PathSegments = syn::parse_str(input).unwrap();
/// }
/// ```
#[macro_export]
macro_rules! custom_punctuation {
    ($ident:ident, $($tt:tt)+) => {
        pub struct $ident {
            #[allow(dead_code)]
            pub spans: $crate::custom_punctuation_repr!($($tt)+),
        }

        #[doc(hidden)]
        #[allow(dead_code, non_snake_case)]
        pub fn $ident<__S: $crate::__private::IntoSpans<$crate::custom_punctuation_repr!($($tt)+)>>(
            spans: __S,
        ) -> $ident {
            let _validate_len = 0 $(+ $crate::custom_punctuation_len!(strict, $tt))*;
            $ident {
                spans: $crate::__private::IntoSpans::into_spans(spans)
            }
        }

        const _: () = {
            impl $crate::__private::Default for $ident {
                fn default() -> Self {
                    $ident($crate::__private::Span::call_site())
                }
            }

            $crate::impl_parse_for_custom_punctuation!($ident, $($tt)+);
            $crate::impl_to_tokens_for_custom_punctuation!($ident, $($tt)+);
            $crate::impl_clone_for_custom_punctuation!($ident, $($tt)+);
            $crate::impl_extra_traits_for_custom_punctuation!($ident, $($tt)+);
        };
    };
}

// Not public API.
#[cfg(feature = "parsing")]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_parse_for_custom_punctuation {
    ($ident:ident, $($tt:tt)+) => {
        impl $crate::__private::CustomToken for $ident {
            fn peek(cursor: $crate::buffer::Cursor) -> $crate::__private::bool {
                $crate::__private::peek_punct(cursor, $crate::stringify_punct!($($tt)+))
            }

            fn display() -> &'static $crate::__private::str {
                $crate::__private::concat!("`", $crate::stringify_punct!($($tt)+), "`")
            }
        }

        impl $crate::parse::Parse for $ident {
            fn parse(input: $crate::parse::ParseStream) -> $crate::parse::Result<$ident> {
                let spans: $crate::custom_punctuation_repr!($($tt)+) =
                    $crate::__private::parse_punct(input, $crate::stringify_punct!($($tt)+))?;
                Ok($ident(spans))
            }
        }
    };
}

// Not public API.
#[cfg(not(feature = "parsing"))]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_parse_for_custom_punctuation {
    ($ident:ident, $($tt:tt)+) => {};
}

// Not public API.
#[cfg(feature = "printing")]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_to_tokens_for_custom_punctuation {
    ($ident:ident, $($tt:tt)+) => {
        impl $crate::__private::ToTokens for $ident {
            fn to_tokens(&self, tokens: &mut $crate::__private::TokenStream2) {
                $crate::__private::print_punct($crate::stringify_punct!($($tt)+), &self.spans, tokens)
            }
        }
    };
}

// Not public API.
#[cfg(not(feature = "printing"))]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_to_tokens_for_custom_punctuation {
    ($ident:ident, $($tt:tt)+) => {};
}

// Not public API.
#[cfg(feature = "clone-impls")]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_clone_for_custom_punctuation {
    ($ident:ident, $($tt:tt)+) => {
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
macro_rules! impl_clone_for_custom_punctuation {
    ($ident:ident, $($tt:tt)+) => {};
}

// Not public API.
#[cfg(feature = "extra-traits")]
#[doc(hidden)]
#[macro_export]
macro_rules! impl_extra_traits_for_custom_punctuation {
    ($ident:ident, $($tt:tt)+) => {
        impl $crate::__private::Debug for $ident {
            fn fmt(&self, f: &mut $crate::__private::Formatter) -> $crate::__private::FmtResult {
                $crate::__private::Formatter::write_str(f, $crate::__private::stringify!($ident))
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
macro_rules! impl_extra_traits_for_custom_punctuation {
    ($ident:ident, $($tt:tt)+) => {};
}

// Not public API.
#[doc(hidden)]
#[macro_export]
macro_rules! custom_punctuation_repr {
    ($($tt:tt)+) => {
        [$crate::__private::Span; 0 $(+ $crate::custom_punctuation_len!(lenient, $tt))+]
    };
}

// Not public API.
#[doc(hidden)]
#[macro_export]
#[rustfmt::skip]
macro_rules! custom_punctuation_len {
    ($mode:ident, &)     => { 1 };
    ($mode:ident, &&)    => { 2 };
    ($mode:ident, &=)    => { 2 };
    ($mode:ident, @)     => { 1 };
    ($mode:ident, ^)     => { 1 };
    ($mode:ident, ^=)    => { 2 };
    ($mode:ident, :)     => { 1 };
    ($mode:ident, ,)     => { 1 };
    ($mode:ident, $)     => { 1 };
    ($mode:ident, .)     => { 1 };
    ($mode:ident, ..)    => { 2 };
    ($mode:ident, ...)   => { 3 };
    ($mode:ident, ..=)   => { 3 };
    ($mode:ident, =)     => { 1 };
    ($mode:ident, ==)    => { 2 };
    ($mode:ident, =>)    => { 2 };
    ($mode:ident, >=)    => { 2 };
    ($mode:ident, >)     => { 1 };
    ($mode:ident, <-)    => { 2 };
    ($mode:ident, <=)    => { 2 };
    ($mode:ident, <)     => { 1 };
    ($mode:ident, -)     => { 1 };
    ($mode:ident, -=)    => { 2 };
    ($mode:ident, !=)    => { 2 };
    ($mode:ident, !)     => { 1 };
    ($mode:ident, |)     => { 1 };
    ($mode:ident, |=)    => { 2 };
    ($mode:ident, ||)    => { 2 };
    ($mode:ident, ::)    => { 2 };
    ($mode:ident, %)     => { 1 };
    ($mode:ident, %=)    => { 2 };
    ($mode:ident, +)     => { 1 };
    ($mode:ident, +=)    => { 2 };
    ($mode:ident, #)     => { 1 };
    ($mode:ident, ?)     => { 1 };
    ($mode:ident, ->)    => { 2 };
    ($mode:ident, ;)     => { 1 };
    ($mode:ident, <<)    => { 2 };
    ($mode:ident, <<=)   => { 3 };
    ($mode:ident, >>)    => { 2 };
    ($mode:ident, >>=)   => { 3 };
    ($mode:ident, /)     => { 1 };
    ($mode:ident, /=)    => { 2 };
    ($mode:ident, *)     => { 1 };
    ($mode:ident, *=)    => { 2 };
    ($mode:ident, ~)     => { 1 };
    (lenient, $tt:tt)    => { 0 };
    (strict, $tt:tt)     => {{ $crate::custom_punctuation_unexpected!($tt); 0 }};
}

// Not public API.
#[doc(hidden)]
#[macro_export]
macro_rules! custom_punctuation_unexpected {
    () => {};
}

// Not public API.
#[doc(hidden)]
#[macro_export]
macro_rules! stringify_punct {
    ($($tt:tt)+) => {
        $crate::__private::concat!($($crate::__private::stringify!($tt)),+)
    };
}
