//! Parsing interface for parsing a token stream into a syntax tree node.
//!
//! Parsing in Syn is built on parser functions that take in a [`ParseStream`]
//! and produce a [`Result<T>`] where `T` is some syntax tree node. Underlying
//! these parser functions is a lower level mechanism built around the
//! [`Cursor`] type. `Cursor` is a cheaply copyable cursor over a range of
//! tokens in a token stream.
//!
//! [`Result<T>`]: Result
//! [`Cursor`]: crate::buffer::Cursor
//!
//! # Example
//!
//! Here is a snippet of parsing code to get a feel for the style of the
//! library. We define data structures for a subset of Rust syntax including
//! enums (not shown) and structs, then provide implementations of the [`Parse`]
//! trait to parse these syntax tree data structures from a token stream.
//!
//! Once `Parse` impls have been defined, they can be called conveniently from a
//! procedural macro through [`parse_macro_input!`] as shown at the bottom of
//! the snippet. If the caller provides syntactically invalid input to the
//! procedural macro, they will receive a helpful compiler error message
//! pointing out the exact token that triggered the failure to parse.
//!
//! [`parse_macro_input!`]: crate::parse_macro_input!
//!
//! ```
//! # extern crate proc_macro;
//! #
//! use proc_macro::TokenStream;
//! use syn::{braced, parse_macro_input, token, Field, Ident, Result, Token};
//! use syn::parse::{Parse, ParseStream};
//! use syn::punctuated::Punctuated;
//!
//! enum Item {
//!     Struct(ItemStruct),
//!     Enum(ItemEnum),
//! }
//!
//! struct ItemStruct {
//!     struct_token: Token![struct],
//!     ident: Ident,
//!     brace_token: token::Brace,
//!     fields: Punctuated<Field, Token![,]>,
//! }
//! #
//! # enum ItemEnum {}
//!
//! impl Parse for Item {
//!     fn parse(input: ParseStream) -> Result<Self> {
//!         let lookahead = input.lookahead1();
//!         if lookahead.peek(Token![struct]) {
//!             input.parse().map(Item::Struct)
//!         } else if lookahead.peek(Token![enum]) {
//!             input.parse().map(Item::Enum)
//!         } else {
//!             Err(lookahead.error())
//!         }
//!     }
//! }
//!
//! impl Parse for ItemStruct {
//!     fn parse(input: ParseStream) -> Result<Self> {
//!         let content;
//!         Ok(ItemStruct {
//!             struct_token: input.parse()?,
//!             ident: input.parse()?,
//!             brace_token: braced!(content in input),
//!             fields: content.parse_terminated(Field::parse_named, Token![,])?,
//!         })
//!     }
//! }
//! #
//! # impl Parse for ItemEnum {
//! #     fn parse(input: ParseStream) -> Result<Self> {
//! #         unimplemented!()
//! #     }
//! # }
//!
//! # const IGNORE: &str = stringify! {
//! #[proc_macro]
//! # };
//! pub fn my_macro(tokens: TokenStream) -> TokenStream {
//!     let input = parse_macro_input!(tokens as Item);
//!
//!     /* ... */
//! #   TokenStream::new()
//! }
//! ```
//!
//! # The `syn::parse*` functions
//!
//! The [`syn::parse`], [`syn::parse2`], and [`syn::parse_str`] functions serve
//! as an entry point for parsing syntax tree nodes that can be parsed in an
//! obvious default way. These functions can return any syntax tree node that
//! implements the [`Parse`] trait, which includes most types in Syn.
//!
//! [`syn::parse`]: crate::parse()
//! [`syn::parse2`]: crate::parse2()
//! [`syn::parse_str`]: crate::parse_str()
//!
//! ```
//! use syn::Type;
//!
//! # fn run_parser() -> syn::Result<()> {
//! let t: Type = syn::parse_str("std::collections::HashMap<String, Value>")?;
//! #     Ok(())
//! # }
//! #
//! # run_parser().unwrap();
//! ```
//!
//! The [`parse_quote!`] macro also uses this approach.
//!
//! [`parse_quote!`]: crate::parse_quote!
//!
//! # The `Parser` trait
//!
//! Some types can be parsed in several ways depending on context. For example
//! an [`Attribute`] can be either "outer" like `#[...]` or "inner" like
//! `#![...]` and parsing the wrong one would be a bug. Similarly [`Punctuated`]
//! may or may not allow trailing punctuation, and parsing it the wrong way
//! would either reject valid input or accept invalid input.
//!
//! [`Attribute`]: crate::Attribute
//! [`Punctuated`]: crate::punctuated
//!
//! The `Parse` trait is not implemented in these cases because there is no good
//! behavior to consider the default.
//!
//! ```compile_fail
//! # extern crate proc_macro;
//! #
//! # use syn::punctuated::Punctuated;
//! # use syn::{PathSegment, Result, Token};
//! #
//! # fn f(tokens: proc_macro::TokenStream) -> Result<()> {
//! #
//! // Can't parse `Punctuated` without knowing whether trailing punctuation
//! // should be allowed in this context.
//! let path: Punctuated<PathSegment, Token![::]> = syn::parse(tokens)?;
//! #
//! #     Ok(())
//! # }
//! ```
//!
//! In these cases the types provide a choice of parser functions rather than a
//! single `Parse` implementation, and those parser functions can be invoked
//! through the [`Parser`] trait.
//!
//!
//! ```
//! # extern crate proc_macro;
//! #
//! use proc_macro::TokenStream;
//! use syn::parse::Parser;
//! use syn::punctuated::Punctuated;
//! use syn::{Attribute, Expr, PathSegment, Result, Token};
//!
//! fn call_some_parser_methods(input: TokenStream) -> Result<()> {
//!     // Parse a nonempty sequence of path segments separated by `::` punctuation
//!     // with no trailing punctuation.
//!     let tokens = input.clone();
//!     let parser = Punctuated::<PathSegment, Token![::]>::parse_separated_nonempty;
//!     let _path = parser.parse(tokens)?;
//!
//!     // Parse a possibly empty sequence of expressions terminated by commas with
//!     // an optional trailing punctuation.
//!     let tokens = input.clone();
//!     let parser = Punctuated::<Expr, Token![,]>::parse_terminated;
//!     let _args = parser.parse(tokens)?;
//!
//!     // Parse zero or more outer attributes but not inner attributes.
//!     let tokens = input.clone();
//!     let parser = Attribute::parse_outer;
//!     let _attrs = parser.parse(tokens)?;
//!
//!     Ok(())
//! }
//! ```

#[path = "discouraged.rs"]
pub mod discouraged;

use crate::buffer::{Cursor, TokenBuffer};
use crate::error;
use crate::lookahead;
use crate::punctuated::Punctuated;
use crate::token::Token;
use proc_macro2::{Delimiter, Group, Literal, Punct, Span, TokenStream, TokenTree};
#[cfg(feature = "printing")]
use quote::ToTokens;
use std::cell::Cell;
use std::fmt::{self, Debug, Display};
#[cfg(feature = "extra-traits")]
use std::hash::{Hash, Hasher};
use std::marker::PhantomData;
use std::mem;
use std::ops::Deref;
use std::panic::{RefUnwindSafe, UnwindSafe};
use std::rc::Rc;
use std::str::FromStr;

pub use crate::error::{Error, Result};
pub use crate::lookahead::{End, Lookahead1, Peek};

/// Parsing interface implemented by all types that can be parsed in a default
/// way from a token stream.
///
/// Refer to the [module documentation] for details about implementing and using
/// the `Parse` trait.
///
/// [module documentation]: self
pub trait Parse: Sized {
    fn parse(input: ParseStream) -> Result<Self>;
}

/// Input to a Syn parser function.
///
/// See the methods of this type under the documentation of [`ParseBuffer`]. For
/// an overview of parsing in Syn, refer to the [module documentation].
///
/// [module documentation]: self
pub type ParseStream<'a> = &'a ParseBuffer<'a>;

/// Cursor position within a buffered token stream.
///
/// This type is more commonly used through the type alias [`ParseStream`] which
/// is an alias for `&ParseBuffer`.
///
/// `ParseStream` is the input type for all parser functions in Syn. They have
/// the signature `fn(ParseStream) -> Result<T>`.
///
/// ## Calling a parser function
///
/// There is no public way to construct a `ParseBuffer`. Instead, if you are
/// looking to invoke a parser function that requires `ParseStream` as input,
/// you will need to go through one of the public parsing entry points.
///
/// - The [`parse_macro_input!`] macro if parsing input of a procedural macro;
/// - One of [the `syn::parse*` functions][syn-parse]; or
/// - A method of the [`Parser`] trait.
///
/// [`parse_macro_input!`]: crate::parse_macro_input!
/// [syn-parse]: self#the-synparse-functions
pub struct ParseBuffer<'a> {
    scope: Span,
    // Instead of Cell<Cursor<'a>> so that ParseBuffer<'a> is covariant in 'a.
    // The rest of the code in this module needs to be careful that only a
    // cursor derived from this `cell` is ever assigned to this `cell`.
    //
    // Cell<Cursor<'a>> cannot be covariant in 'a because then we could take a
    // ParseBuffer<'a>, upcast to ParseBuffer<'short> for some lifetime shorter
    // than 'a, and then assign a Cursor<'short> into the Cell.
    //
    // By extension, it would not be safe to expose an API that accepts a
    // Cursor<'a> and trusts that it lives as long as the cursor currently in
    // the cell.
    cell: Cell<Cursor<'static>>,
    marker: PhantomData<Cursor<'a>>,
    unexpected: Cell<Option<Rc<Cell<Unexpected>>>>,
}

impl<'a> Drop for ParseBuffer<'a> {
    fn drop(&mut self) {
        if let Some((unexpected_span, delimiter)) = span_of_unexpected_ignoring_nones(self.cursor())
        {
            let (inner, old_span) = inner_unexpected(self);
            if old_span.is_none() {
                inner.set(Unexpected::Some(unexpected_span, delimiter));
            }
        }
    }
}

impl<'a> Display for ParseBuffer<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Display::fmt(&self.cursor().token_stream(), f)
    }
}

impl<'a> Debug for ParseBuffer<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Debug::fmt(&self.cursor().token_stream(), f)
    }
}

impl<'a> UnwindSafe for ParseBuffer<'a> {}
impl<'a> RefUnwindSafe for ParseBuffer<'a> {}

/// Cursor state associated with speculative parsing.
///
/// This type is the input of the closure provided to [`ParseStream::step`].
///
/// [`ParseStream::step`]: ParseBuffer::step
///
/// # Example
///
/// ```
/// use proc_macro2::TokenTree;
/// use syn::Result;
/// use syn::parse::ParseStream;
///
/// // This function advances the stream past the next occurrence of `@`. If
/// // no `@` is present in the stream, the stream position is unchanged and
/// // an error is returned.
/// fn skip_past_next_at(input: ParseStream) -> Result<()> {
///     input.step(|cursor| {
///         let mut rest = *cursor;
///         while let Some((tt, next)) = rest.token_tree() {
///             match &tt {
///                 TokenTree::Punct(punct) if punct.as_char() == '@' => {
///                     return Ok(((), next));
///                 }
///                 _ => rest = next,
///             }
///         }
///         Err(cursor.error("no `@` was found after this point"))
///     })
/// }
/// #
/// # fn remainder_after_skipping_past_next_at(
/// #     input: ParseStream,
/// # ) -> Result<proc_macro2::TokenStream> {
/// #     skip_past_next_at(input)?;
/// #     input.parse()
/// # }
/// #
/// # use syn::parse::Parser;
/// # let remainder = remainder_after_skipping_past_next_at
/// #     .parse_str("a @ b c")
/// #     .unwrap();
/// # assert_eq!(remainder.to_string(), "b c");
/// ```
pub struct StepCursor<'c, 'a> {
    scope: Span,
    // This field is covariant in 'c.
    cursor: Cursor<'c>,
    // This field is contravariant in 'c. Together these make StepCursor
    // invariant in 'c. Also covariant in 'a. The user cannot cast 'c to a
    // different lifetime but can upcast into a StepCursor with a shorter
    // lifetime 'a.
    //
    // As long as we only ever construct a StepCursor for which 'c outlives 'a,
    // this means if ever a StepCursor<'c, 'a> exists we are guaranteed that 'c
    // outlives 'a.
    marker: PhantomData<fn(Cursor<'c>) -> Cursor<'a>>,
}

impl<'c, 'a> Deref for StepCursor<'c, 'a> {
    type Target = Cursor<'c>;

    fn deref(&self) -> &Self::Target {
        &self.cursor
    }
}

impl<'c, 'a> Copy for StepCursor<'c, 'a> {}

impl<'c, 'a> Clone for StepCursor<'c, 'a> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<'c, 'a> StepCursor<'c, 'a> {
    /// Triggers an error at the current position of the parse stream.
    ///
    /// The `ParseStream::step` invocation will return this same error without
    /// advancing the stream state.
    pub fn error<T: Display>(self, message: T) -> Error {
        error::new_at(self.scope, self.cursor, message)
    }
}

pub(crate) fn advance_step_cursor<'c, 'a>(proof: StepCursor<'c, 'a>, to: Cursor<'c>) -> Cursor<'a> {
    // Refer to the comments within the StepCursor definition. We use the
    // fact that a StepCursor<'c, 'a> exists as proof that 'c outlives 'a.
    // Cursor is covariant in its lifetime parameter so we can cast a
    // Cursor<'c> to one with the shorter lifetime Cursor<'a>.
    let _ = proof;
    unsafe { mem::transmute::<Cursor<'c>, Cursor<'a>>(to) }
}

pub(crate) fn new_parse_buffer(
    scope: Span,
    cursor: Cursor,
    unexpected: Rc<Cell<Unexpected>>,
) -> ParseBuffer {
    ParseBuffer {
        scope,
        // See comment on `cell` in the struct definition.
        cell: Cell::new(unsafe { mem::transmute::<Cursor, Cursor<'static>>(cursor) }),
        marker: PhantomData,
        unexpected: Cell::new(Some(unexpected)),
    }
}

pub(crate) enum Unexpected {
    None,
    Some(Span, Delimiter),
    Chain(Rc<Cell<Unexpected>>),
}

impl Default for Unexpected {
    fn default() -> Self {
        Unexpected::None
    }
}

impl Clone for Unexpected {
    fn clone(&self) -> Self {
        match self {
            Unexpected::None => Unexpected::None,
            Unexpected::Some(span, delimiter) => Unexpected::Some(*span, *delimiter),
            Unexpected::Chain(next) => Unexpected::Chain(next.clone()),
        }
    }
}

// We call this on Cell<Unexpected> and Cell<Option<T>> where temporarily
// swapping in a None is cheap.
fn cell_clone<T: Default + Clone>(cell: &Cell<T>) -> T {
    let prev = cell.take();
    let ret = prev.clone();
    cell.set(prev);
    ret
}

fn inner_unexpected(buffer: &ParseBuffer) -> (Rc<Cell<Unexpected>>, Option<(Span, Delimiter)>) {
    let mut unexpected = get_unexpected(buffer);
    loop {
        match cell_clone(&unexpected) {
            Unexpected::None => return (unexpected, None),
            Unexpected::Some(span, delimiter) => return (unexpected, Some((span, delimiter))),
            Unexpected::Chain(next) => unexpected = next,
        }
    }
}

pub(crate) fn get_unexpected(buffer: &ParseBuffer) -> Rc<Cell<Unexpected>> {
    cell_clone(&buffer.unexpected).unwrap()
}

fn span_of_unexpected_ignoring_nones(mut cursor: Cursor) -> Option<(Span, Delimiter)> {
    if cursor.eof() {
        return None;
    }
    while let Some((inner, _span, rest)) = cursor.group(Delimiter::None) {
        if let Some(unexpected) = span_of_unexpected_ignoring_nones(inner) {
            return Some(unexpected);
        }
        cursor = rest;
    }
    if cursor.eof() {
        None
    } else {
        Some((cursor.span(), cursor.scope_delimiter()))
    }
}

impl<'a> ParseBuffer<'a> {
    /// Parses a syntax tree node of type `T`, advancing the position of our
    /// parse stream past it.
    pub fn parse<T: Parse>(&self) -> Result<T> {
        T::parse(self)
    }

    /// Calls the given parser function to parse a syntax tree node of type `T`
    /// from this stream.
    ///
    /// # Example
    ///
    /// The parser below invokes [`Attribute::parse_outer`] to parse a vector of
    /// zero or more outer attributes.
    ///
    /// [`Attribute::parse_outer`]: crate::Attribute::parse_outer
    ///
    /// ```
    /// use syn::{Attribute, Ident, Result, Token};
    /// use syn::parse::{Parse, ParseStream};
    ///
    /// // Parses a unit struct with attributes.
    /// //
    /// //     #[path = "s.tmpl"]
    /// //     struct S;
    /// struct UnitStruct {
    ///     attrs: Vec<Attribute>,
    ///     struct_token: Token![struct],
    ///     name: Ident,
    ///     semi_token: Token![;],
    /// }
    ///
    /// impl Parse for UnitStruct {
    ///     fn parse(input: ParseStream) -> Result<Self> {
    ///         Ok(UnitStruct {
    ///             attrs: input.call(Attribute::parse_outer)?,
    ///             struct_token: input.parse()?,
    ///             name: input.parse()?,
    ///             semi_token: input.parse()?,
    ///         })
    ///     }
    /// }
    /// ```
    pub fn call<T>(&'a self, function: fn(ParseStream<'a>) -> Result<T>) -> Result<T> {
        function(self)
    }

    /// Looks at the next token in the parse stream to determine whether it
    /// matches the requested type of token.
    ///
    /// Does not advance the position of the parse stream.
    ///
    /// # Syntax
    ///
    /// Note that this method does not use turbofish syntax. Pass the peek type
    /// inside of parentheses.
    ///
    /// - `input.peek(Token![struct])`
    /// - `input.peek(Token![==])`
    /// - `input.peek(syn::Ident)`&emsp;*(does not accept keywords)*
    /// - `input.peek(syn::Ident::peek_any)`
    /// - `input.peek(Lifetime)`
    /// - `input.peek(token::Brace)`
    ///
    /// # Example
    ///
    /// In this example we finish parsing the list of supertraits when the next
    /// token in the input is either `where` or an opening curly brace.
    ///
    /// ```
    /// use syn::{braced, token, Generics, Ident, Result, Token, TypeParamBound};
    /// use syn::parse::{Parse, ParseStream};
    /// use syn::punctuated::Punctuated;
    ///
    /// // Parses a trait definition containing no associated items.
    /// //
    /// //     trait Marker<'de, T>: A + B<'de> where Box<T>: Clone {}
    /// struct MarkerTrait {
    ///     trait_token: Token![trait],
    ///     ident: Ident,
    ///     generics: Generics,
    ///     colon_token: Option<Token![:]>,
    ///     supertraits: Punctuated<TypeParamBound, Token![+]>,
    ///     brace_token: token::Brace,
    /// }
    ///
    /// impl Parse for MarkerTrait {
    ///     fn parse(input: ParseStream) -> Result<Self> {
    ///         let trait_token: Token![trait] = input.parse()?;
    ///         let ident: Ident = input.parse()?;
    ///         let mut generics: Generics = input.parse()?;
    ///         let colon_token: Option<Token![:]> = input.parse()?;
    ///
    ///         let mut supertraits = Punctuated::new();
    ///         if colon_token.is_some() {
    ///             loop {
    ///                 supertraits.push_value(input.parse()?);
    ///                 if input.peek(Token![where]) || input.peek(token::Brace) {
    ///                     break;
    ///                 }
    ///                 supertraits.push_punct(input.parse()?);
    ///             }
    ///         }
    ///
    ///         generics.where_clause = input.parse()?;
    ///         let content;
    ///         let empty_brace_token = braced!(content in input);
    ///
    ///         Ok(MarkerTrait {
    ///             trait_token,
    ///             ident,
    ///             generics,
    ///             colon_token,
    ///             supertraits,
    ///             brace_token: empty_brace_token,
    ///         })
    ///     }
    /// }
    /// ```
    pub fn peek<T: Peek>(&self, token: T) -> bool {
        let _ = token;
        T::Token::peek(self.cursor())
    }

    /// Looks at the second-next token in the parse stream.
    ///
    /// This is commonly useful as a way to implement contextual keywords.
    ///
    /// # Example
    ///
    /// This example needs to use `peek2` because the symbol `union` is not a
    /// keyword in Rust. We can't use just `peek` and decide to parse a union if
    /// the very next token is `union`, because someone is free to write a `mod
    /// union` and a macro invocation that looks like `union::some_macro! { ...
    /// }`. In other words `union` is a contextual keyword.
    ///
    /// ```
    /// use syn::{Ident, ItemUnion, Macro, Result, Token};
    /// use syn::parse::{Parse, ParseStream};
    ///
    /// // Parses either a union or a macro invocation.
    /// enum UnionOrMacro {
    ///     // union MaybeUninit<T> { uninit: (), value: T }
    ///     Union(ItemUnion),
    ///     // lazy_static! { ... }
    ///     Macro(Macro),
    /// }
    ///
    /// impl Parse for UnionOrMacro {
    ///     fn parse(input: ParseStream) -> Result<Self> {
    ///         if input.peek(Token![union]) && input.peek2(Ident) {
    ///             input.parse().map(UnionOrMacro::Union)
    ///         } else {
    ///             input.parse().map(UnionOrMacro::Macro)
    ///         }
    ///     }
    /// }
    /// ```
    pub fn peek2<T: Peek>(&self, token: T) -> bool {
        fn peek2(buffer: &ParseBuffer, peek: fn(Cursor) -> bool) -> bool {
            buffer.cursor().skip().map_or(false, peek)
        }

        let _ = token;
        peek2(self, T::Token::peek)
    }

    /// Looks at the third-next token in the parse stream.
    pub fn peek3<T: Peek>(&self, token: T) -> bool {
        fn peek3(buffer: &ParseBuffer, peek: fn(Cursor) -> bool) -> bool {
            buffer
                .cursor()
                .skip()
                .and_then(Cursor::skip)
                .map_or(false, peek)
        }

        let _ = token;
        peek3(self, T::Token::peek)
    }

    /// Parses zero or more occurrences of `T` separated by punctuation of type
    /// `P`, with optional trailing punctuation.
    ///
    /// Parsing continues until the end of this parse stream. The entire content
    /// of this parse stream must consist of `T` and `P`.
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
    /// # let input = quote! {
    /// #     struct S(A, B);
    /// # };
    /// # syn::parse2::<TupleStruct>(input).unwrap();
    /// ```
    ///
    /// # See also
    ///
    /// If your separator is anything more complicated than an invocation of the
    /// `Token!` macro, this method won't be applicable and you can instead
    /// directly use `Punctuated`'s parser functions: [`parse_terminated`],
    /// [`parse_separated_nonempty`] etc.
    ///
    /// [`parse_terminated`]: Punctuated::parse_terminated
    /// [`parse_separated_nonempty`]: Punctuated::parse_separated_nonempty
    ///
    /// ```
    /// use syn::{custom_keyword, Expr, Result, Token};
    /// use syn::parse::{Parse, ParseStream};
    /// use syn::punctuated::Punctuated;
    ///
    /// mod kw {
    ///     syn::custom_keyword!(fin);
    /// }
    ///
    /// struct Fin(kw::fin, Token![;]);
    ///
    /// impl Parse for Fin {
    ///     fn parse(input: ParseStream) -> Result<Self> {
    ///         Ok(Self(input.parse()?, input.parse()?))
    ///     }
    /// }
    ///
    /// struct Thing {
    ///     steps: Punctuated<Expr, Fin>,
    /// }
    ///
    /// impl Parse for Thing {
    ///     fn parse(input: ParseStream) -> Result<Self> {
    /// # if true {
    ///         Ok(Thing {
    ///             steps: Punctuated::parse_terminated(input)?,
    ///         })
    /// # } else {
    ///         // or equivalently, this means the same thing:
    /// #       Ok(Thing {
    ///             steps: input.call(Punctuated::parse_terminated)?,
    /// #       })
    /// # }
    ///     }
    /// }
    /// ```
    pub fn parse_terminated<T, P>(
        &'a self,
        parser: fn(ParseStream<'a>) -> Result<T>,
        separator: P,
    ) -> Result<Punctuated<T, P::Token>>
    where
        P: Peek,
        P::Token: Parse,
    {
        let _ = separator;
        Punctuated::parse_terminated_with(self, parser)
    }

    /// Returns whether there are no more tokens remaining to be parsed from
    /// this stream.
    ///
    /// This method returns true upon reaching the end of the content within a
    /// set of delimiters, as well as at the end of the tokens provided to the
    /// outermost parsing entry point.
    ///
    /// This is equivalent to
    /// <code>.<a href="#method.peek">peek</a>(<a href="struct.End.html">syn::parse::End</a>)</code>.
    /// Use `.peek2(End)` or `.peek3(End)` to look for the end of a parse stream
    /// further ahead than the current position.
    ///
    /// # Example
    ///
    /// ```
    /// use syn::{braced, token, Ident, Item, Result, Token};
    /// use syn::parse::{Parse, ParseStream};
    ///
    /// // Parses a Rust `mod m { ... }` containing zero or more items.
    /// struct Mod {
    ///     mod_token: Token![mod],
    ///     name: Ident,
    ///     brace_token: token::Brace,
    ///     items: Vec<Item>,
    /// }
    ///
    /// impl Parse for Mod {
    ///     fn parse(input: ParseStream) -> Result<Self> {
    ///         let content;
    ///         Ok(Mod {
    ///             mod_token: input.parse()?,
    ///             name: input.parse()?,
    ///             brace_token: braced!(content in input),
    ///             items: {
    ///                 let mut items = Vec::new();
    ///                 while !content.is_empty() {
    ///                     items.push(content.parse()?);
    ///                 }
    ///                 items
    ///             },
    ///         })
    ///     }
    /// }
    /// ```
    pub fn is_empty(&self) -> bool {
        self.cursor().eof()
    }

    /// Constructs a helper for peeking at the next token in this stream and
    /// building an error message if it is not one of a set of expected tokens.
    ///
    /// # Example
    ///
    /// ```
    /// use syn::{ConstParam, Ident, Lifetime, LifetimeParam, Result, Token, TypeParam};
    /// use syn::parse::{Parse, ParseStream};
    ///
    /// // A generic parameter, a single one of the comma-separated elements inside
    /// // angle brackets in:
    /// //
    /// //     fn f<T: Clone, 'a, 'b: 'a, const N: usize>() { ... }
    /// //
    /// // On invalid input, lookahead gives us a reasonable error message.
    /// //
    /// //     error: expected one of: identifier, lifetime, `const`
    /// //       |
    /// //     5 |     fn f<!Sized>() {}
    /// //       |          ^
    /// enum GenericParam {
    ///     Type(TypeParam),
    ///     Lifetime(LifetimeParam),
    ///     Const(ConstParam),
    /// }
    ///
    /// impl Parse for GenericParam {
    ///     fn parse(input: ParseStream) -> Result<Self> {
    ///         let lookahead = input.lookahead1();
    ///         if lookahead.peek(Ident) {
    ///             input.parse().map(GenericParam::Type)
    ///         } else if lookahead.peek(Lifetime) {
    ///             input.parse().map(GenericParam::Lifetime)
    ///         } else if lookahead.peek(Token![const]) {
    ///             input.parse().map(GenericParam::Const)
    ///         } else {
    ///             Err(lookahead.error())
    ///         }
    ///     }
    /// }
    /// ```
    pub fn lookahead1(&self) -> Lookahead1<'a> {
        lookahead::new(self.scope, self.cursor())
    }

    /// Forks a parse stream so that parsing tokens out of either the original
    /// or the fork does not advance the position of the other.
    ///
    /// # Performance
    ///
    /// Forking a parse stream is a cheap fixed amount of work and does not
    /// involve copying token buffers. Where you might hit performance problems
    /// is if your macro ends up parsing a large amount of content more than
    /// once.
    ///
    /// ```
    /// # use syn::{Expr, Result};
    /// # use syn::parse::ParseStream;
    /// #
    /// # fn bad(input: ParseStream) -> Result<Expr> {
    /// // Do not do this.
    /// if input.fork().parse::<Expr>().is_ok() {
    ///     return input.parse::<Expr>();
    /// }
    /// # unimplemented!()
    /// # }
    /// ```
    ///
    /// As a rule, avoid parsing an unbounded amount of tokens out of a forked
    /// parse stream. Only use a fork when the amount of work performed against
    /// the fork is small and bounded.
    ///
    /// When complex speculative parsing against the forked stream is
    /// unavoidable, use [`parse::discouraged::Speculative`] to advance the
    /// original stream once the fork's parse is determined to have been
    /// successful.
    ///
    /// For a lower level way to perform speculative parsing at the token level,
    /// consider using [`ParseStream::step`] instead.
    ///
    /// [`parse::discouraged::Speculative`]: discouraged::Speculative
    /// [`ParseStream::step`]: ParseBuffer::step
    ///
    /// # Example
    ///
    /// The parse implementation shown here parses possibly restricted `pub`
    /// visibilities.
    ///
    /// - `pub`
    /// - `pub(crate)`
    /// - `pub(self)`
    /// - `pub(super)`
    /// - `pub(in some::path)`
    ///
    /// To handle the case of visibilities inside of tuple structs, the parser
    /// needs to distinguish parentheses that specify visibility restrictions
    /// from parentheses that form part of a tuple type.
    ///
    /// ```
    /// # struct A;
    /// # struct B;
    /// # struct C;
    /// #
    /// struct S(pub(crate) A, pub (B, C));
    /// ```
    ///
    /// In this example input the first tuple struct element of `S` has
    /// `pub(crate)` visibility while the second tuple struct element has `pub`
    /// visibility; the parentheses around `(B, C)` are part of the type rather
    /// than part of a visibility restriction.
    ///
    /// The parser uses a forked parse stream to check the first token inside of
    /// parentheses after the `pub` keyword. This is a small bounded amount of
    /// work performed against the forked parse stream.
    ///
    /// ```
    /// use syn::{parenthesized, token, Ident, Path, Result, Token};
    /// use syn::ext::IdentExt;
    /// use syn::parse::{Parse, ParseStream};
    ///
    /// struct PubVisibility {
    ///     pub_token: Token![pub],
    ///     restricted: Option<Restricted>,
    /// }
    ///
    /// struct Restricted {
    ///     paren_token: token::Paren,
    ///     in_token: Option<Token![in]>,
    ///     path: Path,
    /// }
    ///
    /// impl Parse for PubVisibility {
    ///     fn parse(input: ParseStream) -> Result<Self> {
    ///         let pub_token: Token![pub] = input.parse()?;
    ///
    ///         if input.peek(token::Paren) {
    ///             let ahead = input.fork();
    ///             let mut content;
    ///             parenthesized!(content in ahead);
    ///
    ///             if content.peek(Token![crate])
    ///                 || content.peek(Token![self])
    ///                 || content.peek(Token![super])
    ///             {
    ///                 return Ok(PubVisibility {
    ///                     pub_token,
    ///                     restricted: Some(Restricted {
    ///                         paren_token: parenthesized!(content in input),
    ///                         in_token: None,
    ///                         path: Path::from(content.call(Ident::parse_any)?),
    ///                     }),
    ///                 });
    ///             } else if content.peek(Token![in]) {
    ///                 return Ok(PubVisibility {
    ///                     pub_token,
    ///                     restricted: Some(Restricted {
    ///                         paren_token: parenthesized!(content in input),
    ///                         in_token: Some(content.parse()?),
    ///                         path: content.call(Path::parse_mod_style)?,
    ///                     }),
    ///                 });
    ///             }
    ///         }
    ///
    ///         Ok(PubVisibility {
    ///             pub_token,
    ///             restricted: None,
    ///         })
    ///     }
    /// }
    /// ```
    pub fn fork(&self) -> Self {
        ParseBuffer {
            scope: self.scope,
            cell: self.cell.clone(),
            marker: PhantomData,
            // Not the parent's unexpected. Nothing cares whether the clone
            // parses all the way unless we `advance_to`.
            unexpected: Cell::new(Some(Rc::new(Cell::new(Unexpected::None)))),
        }
    }

    /// Triggers an error at the current position of the parse stream.
    ///
    /// # Example
    ///
    /// ```
    /// use syn::{Expr, Result, Token};
    /// use syn::parse::{Parse, ParseStream};
    ///
    /// // Some kind of loop: `while` or `for` or `loop`.
    /// struct Loop {
    ///     expr: Expr,
    /// }
    ///
    /// impl Parse for Loop {
    ///     fn parse(input: ParseStream) -> Result<Self> {
    ///         if input.peek(Token![while])
    ///             || input.peek(Token![for])
    ///             || input.peek(Token![loop])
    ///         {
    ///             Ok(Loop {
    ///                 expr: input.parse()?,
    ///             })
    ///         } else {
    ///             Err(input.error("expected some kind of loop"))
    ///         }
    ///     }
    /// }
    /// ```
    pub fn error<T: Display>(&self, message: T) -> Error {
        error::new_at(self.scope, self.cursor(), message)
    }

    /// Speculatively parses tokens from this parse stream, advancing the
    /// position of this stream only if parsing succeeds.
    ///
    /// This is a powerful low-level API used for defining the `Parse` impls of
    /// the basic built-in token types. It is not something that will be used
    /// widely outside of the Syn codebase.
    ///
    /// # Example
    ///
    /// ```
    /// use proc_macro2::TokenTree;
    /// use syn::Result;
    /// use syn::parse::ParseStream;
    ///
    /// // This function advances the stream past the next occurrence of `@`. If
    /// // no `@` is present in the stream, the stream position is unchanged and
    /// // an error is returned.
    /// fn skip_past_next_at(input: ParseStream) -> Result<()> {
    ///     input.step(|cursor| {
    ///         let mut rest = *cursor;
    ///         while let Some((tt, next)) = rest.token_tree() {
    ///             match &tt {
    ///                 TokenTree::Punct(punct) if punct.as_char() == '@' => {
    ///                     return Ok(((), next));
    ///                 }
    ///                 _ => rest = next,
    ///             }
    ///         }
    ///         Err(cursor.error("no `@` was found after this point"))
    ///     })
    /// }
    /// #
    /// # fn remainder_after_skipping_past_next_at(
    /// #     input: ParseStream,
    /// # ) -> Result<proc_macro2::TokenStream> {
    /// #     skip_past_next_at(input)?;
    /// #     input.parse()
    /// # }
    /// #
    /// # use syn::parse::Parser;
    /// # let remainder = remainder_after_skipping_past_next_at
    /// #     .parse_str("a @ b c")
    /// #     .unwrap();
    /// # assert_eq!(remainder.to_string(), "b c");
    /// ```
    pub fn step<F, R>(&self, function: F) -> Result<R>
    where
        F: for<'c> FnOnce(StepCursor<'c, 'a>) -> Result<(R, Cursor<'c>)>,
    {
        // Since the user's function is required to work for any 'c, we know
        // that the Cursor<'c> they return is either derived from the input
        // StepCursor<'c, 'a> or from a Cursor<'static>.
        //
        // It would not be legal to write this function without the invariant
        // lifetime 'c in StepCursor<'c, 'a>. If this function were written only
        // in terms of 'a, the user could take our ParseBuffer<'a>, upcast it to
        // a ParseBuffer<'short> which some shorter lifetime than 'a, invoke
        // `step` on their ParseBuffer<'short> with a closure that returns
        // Cursor<'short>, and we would wrongly write that Cursor<'short> into
        // the Cell intended to hold Cursor<'a>.
        //
        // In some cases it may be necessary for R to contain a Cursor<'a>.
        // Within Syn we solve this using `advance_step_cursor` which uses the
        // existence of a StepCursor<'c, 'a> as proof that it is safe to cast
        // from Cursor<'c> to Cursor<'a>. If needed outside of Syn, it would be
        // safe to expose that API as a method on StepCursor.
        let (node, rest) = function(StepCursor {
            scope: self.scope,
            cursor: self.cell.get(),
            marker: PhantomData,
        })?;
        self.cell.set(rest);
        Ok(node)
    }

    /// Returns the `Span` of the next token in the parse stream, or
    /// `Span::call_site()` if this parse stream has completely exhausted its
    /// input `TokenStream`.
    pub fn span(&self) -> Span {
        let cursor = self.cursor();
        if cursor.eof() {
            self.scope
        } else {
            crate::buffer::open_span_of_group(cursor)
        }
    }

    /// Provides low-level access to the token representation underlying this
    /// parse stream.
    ///
    /// Cursors are immutable so no operations you perform against the cursor
    /// will affect the state of this parse stream.
    ///
    /// # Example
    ///
    /// ```
    /// use proc_macro2::TokenStream;
    /// use syn::buffer::Cursor;
    /// use syn::parse::{ParseStream, Result};
    ///
    /// // Run a parser that returns T, but get its output as TokenStream instead of T.
    /// // This works without T needing to implement ToTokens.
    /// fn recognize_token_stream<T>(
    ///     recognizer: fn(ParseStream) -> Result<T>,
    /// ) -> impl Fn(ParseStream) -> Result<TokenStream> {
    ///     move |input| {
    ///         let begin = input.cursor();
    ///         recognizer(input)?;
    ///         let end = input.cursor();
    ///         Ok(tokens_between(begin, end))
    ///     }
    /// }
    ///
    /// // Collect tokens between two cursors as a TokenStream.
    /// fn tokens_between(begin: Cursor, end: Cursor) -> TokenStream {
    ///     assert!(begin <= end);
    ///
    ///     let mut cursor = begin;
    ///     let mut tokens = TokenStream::new();
    ///     while cursor < end {
    ///         let (token, next) = cursor.token_tree().unwrap();
    ///         tokens.extend(std::iter::once(token));
    ///         cursor = next;
    ///     }
    ///     tokens
    /// }
    ///
    /// fn main() {
    ///     use quote::quote;
    ///     use syn::parse::{Parse, Parser};
    ///     use syn::Token;
    ///
    ///     // Parse syn::Type as a TokenStream, surrounded by angle brackets.
    ///     fn example(input: ParseStream) -> Result<TokenStream> {
    ///         let _langle: Token![<] = input.parse()?;
    ///         let ty = recognize_token_stream(syn::Type::parse)(input)?;
    ///         let _rangle: Token![>] = input.parse()?;
    ///         Ok(ty)
    ///     }
    ///
    ///     let tokens = quote! { <fn() -> u8> };
    ///     println!("{}", example.parse2(tokens).unwrap());
    /// }
    /// ```
    pub fn cursor(&self) -> Cursor<'a> {
        self.cell.get()
    }

    fn check_unexpected(&self) -> Result<()> {
        match inner_unexpected(self).1 {
            Some((span, delimiter)) => Err(err_unexpected_token(span, delimiter)),
            None => Ok(()),
        }
    }
}

#[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
impl<T: Parse> Parse for Box<T> {
    fn parse(input: ParseStream) -> Result<Self> {
        input.parse().map(Box::new)
    }
}

#[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
impl<T: Parse + Token> Parse for Option<T> {
    fn parse(input: ParseStream) -> Result<Self> {
        if T::peek(input.cursor()) {
            Ok(Some(input.parse()?))
        } else {
            Ok(None)
        }
    }
}

#[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
impl Parse for TokenStream {
    fn parse(input: ParseStream) -> Result<Self> {
        input.step(|cursor| Ok((cursor.token_stream(), Cursor::empty())))
    }
}

#[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
impl Parse for TokenTree {
    fn parse(input: ParseStream) -> Result<Self> {
        input.step(|cursor| match cursor.token_tree() {
            Some((tt, rest)) => Ok((tt, rest)),
            None => Err(cursor.error("expected token tree")),
        })
    }
}

#[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
impl Parse for Group {
    fn parse(input: ParseStream) -> Result<Self> {
        input.step(|cursor| {
            if let Some((group, rest)) = cursor.any_group_token() {
                if group.delimiter() != Delimiter::None {
                    return Ok((group, rest));
                }
            }
            Err(cursor.error("expected group token"))
        })
    }
}

#[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
impl Parse for Punct {
    fn parse(input: ParseStream) -> Result<Self> {
        input.step(|cursor| match cursor.punct() {
            Some((punct, rest)) => Ok((punct, rest)),
            None => Err(cursor.error("expected punctuation token")),
        })
    }
}

#[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
impl Parse for Literal {
    fn parse(input: ParseStream) -> Result<Self> {
        input.step(|cursor| match cursor.literal() {
            Some((literal, rest)) => Ok((literal, rest)),
            None => Err(cursor.error("expected literal token")),
        })
    }
}

/// Parser that can parse Rust tokens into a particular syntax tree node.
///
/// Refer to the [module documentation] for details about parsing in Syn.
///
/// [module documentation]: self
pub trait Parser: Sized {
    type Output;

    /// Parse a proc-macro2 token stream into the chosen syntax tree node.
    ///
    /// This function enforces that the input is fully parsed. If there are any
    /// unparsed tokens at the end of the stream, an error is returned.
    fn parse2(self, tokens: TokenStream) -> Result<Self::Output>;

    /// Parse tokens of source code into the chosen syntax tree node.
    ///
    /// This function enforces that the input is fully parsed. If there are any
    /// unparsed tokens at the end of the stream, an error is returned.
    #[cfg(feature = "proc-macro")]
    #[cfg_attr(docsrs, doc(cfg(feature = "proc-macro")))]
    fn parse(self, tokens: proc_macro::TokenStream) -> Result<Self::Output> {
        self.parse2(proc_macro2::TokenStream::from(tokens))
    }

    /// Parse a string of Rust code into the chosen syntax tree node.
    ///
    /// This function enforces that the input is fully parsed. If there are any
    /// unparsed tokens at the end of the string, an error is returned.
    ///
    /// # Hygiene
    ///
    /// Every span in the resulting syntax tree will be set to resolve at the
    /// macro call site.
    fn parse_str(self, s: &str) -> Result<Self::Output> {
        self.parse2(proc_macro2::TokenStream::from_str(s)?)
    }

    // Not public API.
    #[doc(hidden)]
    fn __parse_scoped(self, scope: Span, tokens: TokenStream) -> Result<Self::Output> {
        let _ = scope;
        self.parse2(tokens)
    }
}

fn tokens_to_parse_buffer(tokens: &TokenBuffer) -> ParseBuffer {
    let scope = Span::call_site();
    let cursor = tokens.begin();
    let unexpected = Rc::new(Cell::new(Unexpected::None));
    new_parse_buffer(scope, cursor, unexpected)
}

impl<F, T> Parser for F
where
    F: FnOnce(ParseStream) -> Result<T>,
{
    type Output = T;

    fn parse2(self, tokens: TokenStream) -> Result<T> {
        let buf = TokenBuffer::new2(tokens);
        let state = tokens_to_parse_buffer(&buf);
        let node = self(&state)?;
        state.check_unexpected()?;
        if let Some((unexpected_span, delimiter)) =
            span_of_unexpected_ignoring_nones(state.cursor())
        {
            Err(err_unexpected_token(unexpected_span, delimiter))
        } else {
            Ok(node)
        }
    }

    fn __parse_scoped(self, scope: Span, tokens: TokenStream) -> Result<Self::Output> {
        let buf = TokenBuffer::new2(tokens);
        let cursor = buf.begin();
        let unexpected = Rc::new(Cell::new(Unexpected::None));
        let state = new_parse_buffer(scope, cursor, unexpected);
        let node = self(&state)?;
        state.check_unexpected()?;
        if let Some((unexpected_span, delimiter)) =
            span_of_unexpected_ignoring_nones(state.cursor())
        {
            Err(err_unexpected_token(unexpected_span, delimiter))
        } else {
            Ok(node)
        }
    }
}

pub(crate) fn parse_scoped<F: Parser>(f: F, scope: Span, tokens: TokenStream) -> Result<F::Output> {
    f.__parse_scoped(scope, tokens)
}

fn err_unexpected_token(span: Span, delimiter: Delimiter) -> Error {
    let msg = match delimiter {
        Delimiter::Parenthesis => "unexpected token, expected `)`",
        Delimiter::Brace => "unexpected token, expected `}`",
        Delimiter::Bracket => "unexpected token, expected `]`",
        Delimiter::None => "unexpected token",
    };
    Error::new(span, msg)
}

/// An empty syntax tree node that consumes no tokens when parsed.
///
/// This is useful for attribute macros that want to ensure they are not
/// provided any attribute args.
///
/// ```
/// # extern crate proc_macro;
/// #
/// use proc_macro::TokenStream;
/// use syn::parse_macro_input;
/// use syn::parse::Nothing;
///
/// # const IGNORE: &str = stringify! {
/// #[proc_macro_attribute]
/// # };
/// pub fn my_attr(args: TokenStream, input: TokenStream) -> TokenStream {
///     parse_macro_input!(args as Nothing);
///
///     /* ... */
/// #   TokenStream::new()
/// }
/// ```
///
/// ```text
/// error: unexpected token
///  --> src/main.rs:3:19
///   |
/// 3 | #[my_attr(asdf)]
///   |           ^^^^
/// ```
pub struct Nothing;

impl Parse for Nothing {
    fn parse(_input: ParseStream) -> Result<Self> {
        Ok(Nothing)
    }
}

#[cfg(feature = "printing")]
#[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
impl ToTokens for Nothing {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let _ = tokens;
    }
}

#[cfg(feature = "clone-impls")]
#[cfg_attr(docsrs, doc(cfg(feature = "clone-impls")))]
impl Clone for Nothing {
    fn clone(&self) -> Self {
        *self
    }
}

#[cfg(feature = "clone-impls")]
#[cfg_attr(docsrs, doc(cfg(feature = "clone-impls")))]
impl Copy for Nothing {}

#[cfg(feature = "extra-traits")]
#[cfg_attr(docsrs, doc(cfg(feature = "extra-traits")))]
impl Debug for Nothing {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str("Nothing")
    }
}

#[cfg(feature = "extra-traits")]
#[cfg_attr(docsrs, doc(cfg(feature = "extra-traits")))]
impl Eq for Nothing {}

#[cfg(feature = "extra-traits")]
#[cfg_attr(docsrs, doc(cfg(feature = "extra-traits")))]
impl PartialEq for Nothing {
    fn eq(&self, _other: &Self) -> bool {
        true
    }
}

#[cfg(feature = "extra-traits")]
#[cfg_attr(docsrs, doc(cfg(feature = "extra-traits")))]
impl Hash for Nothing {
    fn hash<H: Hasher>(&self, _state: &mut H) {}
}
