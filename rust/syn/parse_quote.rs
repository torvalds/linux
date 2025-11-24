/// Quasi-quotation macro that accepts input like the [`quote!`] macro but uses
/// type inference to figure out a return type for those tokens.
///
/// [`quote!`]: https://docs.rs/quote/1.0/quote/index.html
///
/// The return type can be any syntax tree node that implements the [`Parse`]
/// trait.
///
/// [`Parse`]: crate::parse::Parse
///
/// ```
/// use quote::quote;
/// use syn::{parse_quote, Stmt};
///
/// fn main() {
///     let name = quote!(v);
///     let ty = quote!(u8);
///
///     let stmt: Stmt = parse_quote! {
///         let #name: #ty = Default::default();
///     };
///
///     println!("{:#?}", stmt);
/// }
/// ```
///
/// *This macro is available only if Syn is built with both the `"parsing"` and
/// `"printing"` features.*
///
/// # Example
///
/// The following helper function adds a bound `T: HeapSize` to every type
/// parameter `T` in the input generics.
///
/// ```
/// use syn::{parse_quote, Generics, GenericParam};
///
/// // Add a bound `T: HeapSize` to every type parameter T.
/// fn add_trait_bounds(mut generics: Generics) -> Generics {
///     for param in &mut generics.params {
///         if let GenericParam::Type(type_param) = param {
///             type_param.bounds.push(parse_quote!(HeapSize));
///         }
///     }
///     generics
/// }
/// ```
///
/// # Special cases
///
/// This macro can parse the following additional types as a special case even
/// though they do not implement the `Parse` trait.
///
/// - [`Attribute`] — parses one attribute, allowing either outer like `#[...]`
///   or inner like `#![...]`
/// - [`Vec<Attribute>`] — parses multiple attributes, including mixed kinds in
///   any order
/// - [`Punctuated<T, P>`] — parses zero or more `T` separated by punctuation
///   `P` with optional trailing punctuation
/// - [`Vec<Arm>`] — parses arms separated by optional commas according to the
///   same grammar as the inside of a `match` expression
/// - [`Vec<Stmt>`] — parses the same as `Block::parse_within`
/// - [`Pat`], [`Box<Pat>`] — parses the same as
///   `Pat::parse_multi_with_leading_vert`
/// - [`Field`] — parses a named or unnamed struct field
///
/// [`Vec<Attribute>`]: Attribute
/// [`Vec<Arm>`]: Arm
/// [`Vec<Stmt>`]: Block::parse_within
/// [`Pat`]: Pat::parse_multi_with_leading_vert
/// [`Box<Pat>`]: Pat::parse_multi_with_leading_vert
///
/// # Panics
///
/// Panics if the tokens fail to parse as the expected syntax tree type. The
/// caller is responsible for ensuring that the input tokens are syntactically
/// valid.
#[cfg_attr(docsrs, doc(cfg(all(feature = "parsing", feature = "printing"))))]
#[macro_export]
macro_rules! parse_quote {
    ($($tt:tt)*) => {
        $crate::__private::parse_quote($crate::__private::quote::quote!($($tt)*))
    };
}

/// This macro is [`parse_quote!`] + [`quote_spanned!`][quote::quote_spanned].
///
/// Please refer to each of their documentation.
///
/// # Example
///
/// ```
/// use quote::{quote, quote_spanned};
/// use syn::spanned::Spanned;
/// use syn::{parse_quote_spanned, ReturnType, Signature};
///
/// // Changes `fn()` to `fn() -> Pin<Box<dyn Future<Output = ()>>>`,
/// // and `fn() -> T` to `fn() -> Pin<Box<dyn Future<Output = T>>>`,
/// // without introducing any call_site() spans.
/// fn make_ret_pinned_future(sig: &mut Signature) {
///     let ret = match &sig.output {
///         ReturnType::Default => quote_spanned!(sig.paren_token.span=> ()),
///         ReturnType::Type(_, ret) => quote!(#ret),
///     };
///     sig.output = parse_quote_spanned! {ret.span()=>
///         -> ::std::pin::Pin<::std::boxed::Box<dyn ::std::future::Future<Output = #ret>>>
///     };
/// }
/// ```
#[cfg_attr(docsrs, doc(cfg(all(feature = "parsing", feature = "printing"))))]
#[macro_export]
macro_rules! parse_quote_spanned {
    ($span:expr=> $($tt:tt)*) => {
        $crate::__private::parse_quote($crate::__private::quote::quote_spanned!($span=> $($tt)*))
    };
}

////////////////////////////////////////////////////////////////////////////////
// Can parse any type that implements Parse.

use crate::error::Result;
use crate::parse::{Parse, ParseStream, Parser};
use proc_macro2::TokenStream;

// Not public API.
#[doc(hidden)]
#[track_caller]
pub fn parse<T: ParseQuote>(token_stream: TokenStream) -> T {
    let parser = T::parse;
    match parser.parse2(token_stream) {
        Ok(t) => t,
        Err(err) => panic!("{}", err),
    }
}

#[doc(hidden)]
pub trait ParseQuote: Sized {
    fn parse(input: ParseStream) -> Result<Self>;
}

impl<T: Parse> ParseQuote for T {
    fn parse(input: ParseStream) -> Result<Self> {
        <T as Parse>::parse(input)
    }
}

////////////////////////////////////////////////////////////////////////////////
// Any other types that we want `parse_quote!` to be able to parse.

use crate::punctuated::Punctuated;
#[cfg(any(feature = "full", feature = "derive"))]
use crate::{attr, Attribute, Field, FieldMutability, Ident, Type, Visibility};
#[cfg(feature = "full")]
use crate::{Arm, Block, Pat, Stmt};

#[cfg(any(feature = "full", feature = "derive"))]
impl ParseQuote for Attribute {
    fn parse(input: ParseStream) -> Result<Self> {
        if input.peek(Token![#]) && input.peek2(Token![!]) {
            attr::parsing::single_parse_inner(input)
        } else {
            attr::parsing::single_parse_outer(input)
        }
    }
}

#[cfg(any(feature = "full", feature = "derive"))]
impl ParseQuote for Vec<Attribute> {
    fn parse(input: ParseStream) -> Result<Self> {
        let mut attrs = Vec::new();
        while !input.is_empty() {
            attrs.push(ParseQuote::parse(input)?);
        }
        Ok(attrs)
    }
}

#[cfg(any(feature = "full", feature = "derive"))]
impl ParseQuote for Field {
    fn parse(input: ParseStream) -> Result<Self> {
        let attrs = input.call(Attribute::parse_outer)?;
        let vis: Visibility = input.parse()?;

        let ident: Option<Ident>;
        let colon_token: Option<Token![:]>;
        let is_named = input.peek(Ident) && input.peek2(Token![:]) && !input.peek2(Token![::]);
        if is_named {
            ident = Some(input.parse()?);
            colon_token = Some(input.parse()?);
        } else {
            ident = None;
            colon_token = None;
        }

        let ty: Type = input.parse()?;

        Ok(Field {
            attrs,
            vis,
            mutability: FieldMutability::None,
            ident,
            colon_token,
            ty,
        })
    }
}

#[cfg(feature = "full")]
impl ParseQuote for Pat {
    fn parse(input: ParseStream) -> Result<Self> {
        Pat::parse_multi_with_leading_vert(input)
    }
}

#[cfg(feature = "full")]
impl ParseQuote for Box<Pat> {
    fn parse(input: ParseStream) -> Result<Self> {
        <Pat as ParseQuote>::parse(input).map(Box::new)
    }
}

impl<T: Parse, P: Parse> ParseQuote for Punctuated<T, P> {
    fn parse(input: ParseStream) -> Result<Self> {
        Self::parse_terminated(input)
    }
}

#[cfg(feature = "full")]
impl ParseQuote for Vec<Stmt> {
    fn parse(input: ParseStream) -> Result<Self> {
        Block::parse_within(input)
    }
}

#[cfg(feature = "full")]
impl ParseQuote for Vec<Arm> {
    fn parse(input: ParseStream) -> Result<Self> {
        Arm::parse_multiple(input)
    }
}
