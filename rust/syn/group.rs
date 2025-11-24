use crate::error::Result;
use crate::parse::ParseBuffer;
use crate::token;
use proc_macro2::extra::DelimSpan;
use proc_macro2::Delimiter;

// Not public API.
#[doc(hidden)]
pub struct Parens<'a> {
    #[doc(hidden)]
    pub token: token::Paren,
    #[doc(hidden)]
    pub content: ParseBuffer<'a>,
}

// Not public API.
#[doc(hidden)]
pub struct Braces<'a> {
    #[doc(hidden)]
    pub token: token::Brace,
    #[doc(hidden)]
    pub content: ParseBuffer<'a>,
}

// Not public API.
#[doc(hidden)]
pub struct Brackets<'a> {
    #[doc(hidden)]
    pub token: token::Bracket,
    #[doc(hidden)]
    pub content: ParseBuffer<'a>,
}

// Not public API.
#[cfg(any(feature = "full", feature = "derive"))]
#[doc(hidden)]
pub struct Group<'a> {
    #[doc(hidden)]
    pub token: token::Group,
    #[doc(hidden)]
    pub content: ParseBuffer<'a>,
}

// Not public API.
#[doc(hidden)]
pub fn parse_parens<'a>(input: &ParseBuffer<'a>) -> Result<Parens<'a>> {
    parse_delimited(input, Delimiter::Parenthesis).map(|(span, content)| Parens {
        token: token::Paren(span),
        content,
    })
}

// Not public API.
#[doc(hidden)]
pub fn parse_braces<'a>(input: &ParseBuffer<'a>) -> Result<Braces<'a>> {
    parse_delimited(input, Delimiter::Brace).map(|(span, content)| Braces {
        token: token::Brace(span),
        content,
    })
}

// Not public API.
#[doc(hidden)]
pub fn parse_brackets<'a>(input: &ParseBuffer<'a>) -> Result<Brackets<'a>> {
    parse_delimited(input, Delimiter::Bracket).map(|(span, content)| Brackets {
        token: token::Bracket(span),
        content,
    })
}

#[cfg(any(feature = "full", feature = "derive"))]
pub(crate) fn parse_group<'a>(input: &ParseBuffer<'a>) -> Result<Group<'a>> {
    parse_delimited(input, Delimiter::None).map(|(span, content)| Group {
        token: token::Group(span.join()),
        content,
    })
}

fn parse_delimited<'a>(
    input: &ParseBuffer<'a>,
    delimiter: Delimiter,
) -> Result<(DelimSpan, ParseBuffer<'a>)> {
    input.step(|cursor| {
        if let Some((content, span, rest)) = cursor.group(delimiter) {
            let scope = span.close();
            let nested = crate::parse::advance_step_cursor(cursor, content);
            let unexpected = crate::parse::get_unexpected(input);
            let content = crate::parse::new_parse_buffer(scope, nested, unexpected);
            Ok(((span, content), rest))
        } else {
            let message = match delimiter {
                Delimiter::Parenthesis => "expected parentheses",
                Delimiter::Brace => "expected curly braces",
                Delimiter::Bracket => "expected square brackets",
                Delimiter::None => "expected invisible group",
            };
            Err(cursor.error(message))
        }
    })
}

/// Parse a set of parentheses and expose their content to subsequent parsers.
///
/// # Example
///
/// ```
/// # use quote::quote;
/// #
/// use syn::{parenthesized, token, Ident, Result, Token, Type};
/// use syn::parse::{Parse, ParseStream};
/// use syn::punctuated::Punctuated;
///
/// // Parse a simplified tuple struct syntax like:
/// //
/// //     struct S(A, B);
/// struct TupleStruct {
///     struct_token: Token![struct],
///     ident: Ident,
///     paren_token: token::Paren,
///     fields: Punctuated<Type, Token![,]>,
///     semi_token: Token![;],
/// }
///
/// impl Parse for TupleStruct {
///     fn parse(input: ParseStream) -> Result<Self> {
///         let content;
///         Ok(TupleStruct {
///             struct_token: input.parse()?,
///             ident: input.parse()?,
///             paren_token: parenthesized!(content in input),
///             fields: content.parse_terminated(Type::parse, Token![,])?,
///             semi_token: input.parse()?,
///         })
///     }
/// }
/// #
/// # fn main() {
/// #     let input = quote! {
/// #         struct S(A, B);
/// #     };
/// #     syn::parse2::<TupleStruct>(input).unwrap();
/// # }
/// ```
#[macro_export]
#[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
macro_rules! parenthesized {
    ($content:ident in $cursor:expr) => {
        match $crate::__private::parse_parens(&$cursor) {
            $crate::__private::Ok(parens) => {
                $content = parens.content;
                parens.token
            }
            $crate::__private::Err(error) => {
                return $crate::__private::Err(error);
            }
        }
    };
}

/// Parse a set of curly braces and expose their content to subsequent parsers.
///
/// # Example
///
/// ```
/// # use quote::quote;
/// #
/// use syn::{braced, token, Ident, Result, Token, Type};
/// use syn::parse::{Parse, ParseStream};
/// use syn::punctuated::Punctuated;
///
/// // Parse a simplified struct syntax like:
/// //
/// //     struct S {
/// //         a: A,
/// //         b: B,
/// //     }
/// struct Struct {
///     struct_token: Token![struct],
///     ident: Ident,
///     brace_token: token::Brace,
///     fields: Punctuated<Field, Token![,]>,
/// }
///
/// struct Field {
///     name: Ident,
///     colon_token: Token![:],
///     ty: Type,
/// }
///
/// impl Parse for Struct {
///     fn parse(input: ParseStream) -> Result<Self> {
///         let content;
///         Ok(Struct {
///             struct_token: input.parse()?,
///             ident: input.parse()?,
///             brace_token: braced!(content in input),
///             fields: content.parse_terminated(Field::parse, Token![,])?,
///         })
///     }
/// }
///
/// impl Parse for Field {
///     fn parse(input: ParseStream) -> Result<Self> {
///         Ok(Field {
///             name: input.parse()?,
///             colon_token: input.parse()?,
///             ty: input.parse()?,
///         })
///     }
/// }
/// #
/// # fn main() {
/// #     let input = quote! {
/// #         struct S {
/// #             a: A,
/// #             b: B,
/// #         }
/// #     };
/// #     syn::parse2::<Struct>(input).unwrap();
/// # }
/// ```
#[macro_export]
#[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
macro_rules! braced {
    ($content:ident in $cursor:expr) => {
        match $crate::__private::parse_braces(&$cursor) {
            $crate::__private::Ok(braces) => {
                $content = braces.content;
                braces.token
            }
            $crate::__private::Err(error) => {
                return $crate::__private::Err(error);
            }
        }
    };
}

/// Parse a set of square brackets and expose their content to subsequent
/// parsers.
///
/// # Example
///
/// ```
/// # use quote::quote;
/// #
/// use proc_macro2::TokenStream;
/// use syn::{bracketed, token, Result, Token};
/// use syn::parse::{Parse, ParseStream};
///
/// // Parse an outer attribute like:
/// //
/// //     #[repr(C, packed)]
/// struct OuterAttribute {
///     pound_token: Token![#],
///     bracket_token: token::Bracket,
///     content: TokenStream,
/// }
///
/// impl Parse for OuterAttribute {
///     fn parse(input: ParseStream) -> Result<Self> {
///         let content;
///         Ok(OuterAttribute {
///             pound_token: input.parse()?,
///             bracket_token: bracketed!(content in input),
///             content: content.parse()?,
///         })
///     }
/// }
/// #
/// # fn main() {
/// #     let input = quote! {
/// #         #[repr(C, packed)]
/// #     };
/// #     syn::parse2::<OuterAttribute>(input).unwrap();
/// # }
/// ```
#[macro_export]
#[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
macro_rules! bracketed {
    ($content:ident in $cursor:expr) => {
        match $crate::__private::parse_brackets(&$cursor) {
            $crate::__private::Ok(brackets) => {
                $content = brackets.content;
                brackets.token
            }
            $crate::__private::Err(error) => {
                return $crate::__private::Err(error);
            }
        }
    };
}
