use crate::buffer::Cursor;
use crate::error::{self, Error};
use crate::sealed::lookahead::Sealed;
use crate::span::IntoSpans;
use crate::token::{CustomToken, Token};
use proc_macro2::{Delimiter, Span};
use std::cell::RefCell;

/// Support for checking the next token in a stream to decide how to parse.
///
/// An important advantage over [`ParseStream::peek`] is that here we
/// automatically construct an appropriate error message based on the token
/// alternatives that get peeked. If you are producing your own error message,
/// go ahead and use `ParseStream::peek` instead.
///
/// Use [`ParseStream::lookahead1`] to construct this object.
///
/// [`ParseStream::peek`]: crate::parse::ParseBuffer::peek
/// [`ParseStream::lookahead1`]: crate::parse::ParseBuffer::lookahead1
///
/// Consuming tokens from the source stream after constructing a lookahead
/// object does not also advance the lookahead object.
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
pub struct Lookahead1<'a> {
    scope: Span,
    cursor: Cursor<'a>,
    comparisons: RefCell<Vec<&'static str>>,
}

pub(crate) fn new(scope: Span, cursor: Cursor) -> Lookahead1 {
    Lookahead1 {
        scope,
        cursor,
        comparisons: RefCell::new(Vec::new()),
    }
}

fn peek_impl(
    lookahead: &Lookahead1,
    peek: fn(Cursor) -> bool,
    display: fn() -> &'static str,
) -> bool {
    if peek(lookahead.cursor) {
        return true;
    }
    lookahead.comparisons.borrow_mut().push(display());
    false
}

impl<'a> Lookahead1<'a> {
    /// Looks at the next token in the parse stream to determine whether it
    /// matches the requested type of token.
    ///
    /// # Syntax
    ///
    /// Note that this method does not use turbofish syntax. Pass the peek type
    /// inside of parentheses.
    ///
    /// - `input.peek(Token![struct])`
    /// - `input.peek(Token![==])`
    /// - `input.peek(Ident)`&emsp;*(does not accept keywords)*
    /// - `input.peek(Ident::peek_any)`
    /// - `input.peek(Lifetime)`
    /// - `input.peek(token::Brace)`
    pub fn peek<T: Peek>(&self, token: T) -> bool {
        let _ = token;
        peek_impl(self, T::Token::peek, T::Token::display)
    }

    /// Triggers an error at the current position of the parse stream.
    ///
    /// The error message will identify all of the expected token types that
    /// have been peeked against this lookahead instance.
    pub fn error(self) -> Error {
        let mut comparisons = self.comparisons.into_inner();
        comparisons.retain_mut(|display| {
            if *display == "`)`" {
                *display = match self.cursor.scope_delimiter() {
                    Delimiter::Parenthesis => "`)`",
                    Delimiter::Brace => "`}`",
                    Delimiter::Bracket => "`]`",
                    Delimiter::None => return false,
                }
            }
            true
        });
        match comparisons.len() {
            0 => {
                if self.cursor.eof() {
                    Error::new(self.scope, "unexpected end of input")
                } else {
                    Error::new(self.cursor.span(), "unexpected token")
                }
            }
            1 => {
                let message = format!("expected {}", comparisons[0]);
                error::new_at(self.scope, self.cursor, message)
            }
            2 => {
                let message = format!("expected {} or {}", comparisons[0], comparisons[1]);
                error::new_at(self.scope, self.cursor, message)
            }
            _ => {
                let join = comparisons.join(", ");
                let message = format!("expected one of: {}", join);
                error::new_at(self.scope, self.cursor, message)
            }
        }
    }
}

/// Types that can be parsed by looking at just one token.
///
/// Use [`ParseStream::peek`] to peek one of these types in a parse stream
/// without consuming it from the stream.
///
/// This trait is sealed and cannot be implemented for types outside of Syn.
///
/// [`ParseStream::peek`]: crate::parse::ParseBuffer::peek
pub trait Peek: Sealed {
    // Not public API.
    #[doc(hidden)]
    type Token: Token;
}

/// Pseudo-token used for peeking the end of a parse stream.
///
/// This type is only useful as an argument to one of the following functions:
///
/// - [`ParseStream::peek`][crate::parse::ParseBuffer::peek]
/// - [`ParseStream::peek2`][crate::parse::ParseBuffer::peek2]
/// - [`ParseStream::peek3`][crate::parse::ParseBuffer::peek3]
/// - [`Lookahead1::peek`]
///
/// The peek will return `true` if there are no remaining tokens after that
/// point in the parse stream.
///
/// # Example
///
/// Suppose we are parsing attributes containing core::fmt inspired formatting
/// arguments:
///
/// - `#[fmt("simple example")]`
/// - `#[fmt("interpolation e{}ample", self.x)]`
/// - `#[fmt("interpolation e{x}ample")]`
///
/// and we want to recognize the cases where no interpolation occurs so that
/// more efficient code can be generated.
///
/// The following implementation uses `input.peek(Token![,]) &&
/// input.peek2(End)` to recognize the case of a trailing comma without
/// consuming the comma from the parse stream, because if it isn't a trailing
/// comma, that same comma needs to be parsed as part of `args`.
///
/// ```
/// use proc_macro2::TokenStream;
/// use quote::quote;
/// use syn::parse::{End, Parse, ParseStream, Result};
/// use syn::{parse_quote, Attribute, LitStr, Token};
///
/// struct FormatArgs {
///     template: LitStr,  // "...{}..."
///     args: TokenStream, // , self.x
/// }
///
/// impl Parse for FormatArgs {
///     fn parse(input: ParseStream) -> Result<Self> {
///         let template: LitStr = input.parse()?;
///
///         let args = if input.is_empty()
///             || input.peek(Token![,]) && input.peek2(End)
///         {
///             input.parse::<Option<Token![,]>>()?;
///             TokenStream::new()
///         } else {
///             input.parse()?
///         };
///
///         Ok(FormatArgs {
///             template,
///             args,
///         })
///     }
/// }
///
/// fn main() -> Result<()> {
///     let attrs: Vec<Attribute> = parse_quote! {
///         #[fmt("simple example")]
///         #[fmt("interpolation e{}ample", self.x)]
///         #[fmt("interpolation e{x}ample")]
///     };
///
///     for attr in &attrs {
///         let FormatArgs { template, args } = attr.parse_args()?;
///         let requires_fmt_machinery =
///             !args.is_empty() || template.value().contains(['{', '}']);
///         let out = if requires_fmt_machinery {
///             quote! {
///                 ::core::write!(__formatter, #template #args)
///             }
///         } else {
///             quote! {
///                 __formatter.write_str(#template)
///             }
///         };
///         println!("{}", out);
///     }
///     Ok(())
/// }
/// ```
///
/// Implementing this parsing logic without `peek2(End)` is more clumsy because
/// we'd need a parse stream actually advanced past the comma before being able
/// to find out whether there is anything after it. It would look something
/// like:
///
/// ```
/// # use proc_macro2::TokenStream;
/// # use syn::parse::{ParseStream, Result};
/// # use syn::Token;
/// #
/// # fn parse(input: ParseStream) -> Result<()> {
/// use syn::parse::discouraged::Speculative as _;
///
/// let ahead = input.fork();
/// ahead.parse::<Option<Token![,]>>()?;
/// let args = if ahead.is_empty() {
///     input.advance_to(&ahead);
///     TokenStream::new()
/// } else {
///     input.parse()?
/// };
/// # Ok(())
/// # }
/// ```
///
/// or:
///
/// ```
/// # use proc_macro2::TokenStream;
/// # use syn::parse::{ParseStream, Result};
/// # use syn::Token;
/// #
/// # fn parse(input: ParseStream) -> Result<()> {
/// use quote::ToTokens as _;
///
/// let comma: Option<Token![,]> = input.parse()?;
/// let mut args = TokenStream::new();
/// if !input.is_empty() {
///     comma.to_tokens(&mut args);
///     input.parse::<TokenStream>()?.to_tokens(&mut args);
/// }
/// # Ok(())
/// # }
/// ```
pub struct End;

impl Copy for End {}

impl Clone for End {
    fn clone(&self) -> Self {
        *self
    }
}

impl Peek for End {
    type Token = Self;
}

impl CustomToken for End {
    fn peek(cursor: Cursor) -> bool {
        cursor.eof()
    }

    fn display() -> &'static str {
        "`)`" // Lookahead1 error message will fill in the expected close delimiter
    }
}

impl<F: Copy + FnOnce(TokenMarker) -> T, T: Token> Peek for F {
    type Token = T;
}

pub enum TokenMarker {}

impl<S> IntoSpans<S> for TokenMarker {
    fn into_spans(self) -> S {
        match self {}
    }
}

impl<F: Copy + FnOnce(TokenMarker) -> T, T: Token> Sealed for F {}

impl Sealed for End {}
