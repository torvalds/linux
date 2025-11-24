#[cfg(feature = "parsing")]
use crate::buffer::Cursor;
use crate::thread::ThreadBound;
use proc_macro2::{
    Delimiter, Group, Ident, LexError, Literal, Punct, Spacing, Span, TokenStream, TokenTree,
};
#[cfg(feature = "printing")]
use quote::ToTokens;
use std::fmt::{self, Debug, Display};
use std::slice;
use std::vec;

/// The result of a Syn parser.
pub type Result<T> = std::result::Result<T, Error>;

/// Error returned when a Syn parser cannot parse the input tokens.
///
/// # Error reporting in proc macros
///
/// The correct way to report errors back to the compiler from a procedural
/// macro is by emitting an appropriately spanned invocation of
/// [`compile_error!`] in the generated code. This produces a better diagnostic
/// message than simply panicking the macro.
///
/// [`compile_error!`]: std::compile_error!
///
/// When parsing macro input, the [`parse_macro_input!`] macro handles the
/// conversion to `compile_error!` automatically.
///
/// [`parse_macro_input!`]: crate::parse_macro_input!
///
/// ```
/// # extern crate proc_macro;
/// #
/// use proc_macro::TokenStream;
/// use syn::parse::{Parse, ParseStream, Result};
/// use syn::{parse_macro_input, ItemFn};
///
/// # const IGNORE: &str = stringify! {
/// #[proc_macro_attribute]
/// # };
/// pub fn my_attr(args: TokenStream, input: TokenStream) -> TokenStream {
///     let args = parse_macro_input!(args as MyAttrArgs);
///     let input = parse_macro_input!(input as ItemFn);
///
///     /* ... */
///     # TokenStream::new()
/// }
///
/// struct MyAttrArgs {
///     # _k: [(); { stringify! {
///     ...
///     # }; 0 }]
/// }
///
/// impl Parse for MyAttrArgs {
///     fn parse(input: ParseStream) -> Result<Self> {
///         # stringify! {
///         ...
///         # };
///         # unimplemented!()
///     }
/// }
/// ```
///
/// For errors that arise later than the initial parsing stage, the
/// [`.to_compile_error()`] or [`.into_compile_error()`] methods can be used to
/// perform an explicit conversion to `compile_error!`.
///
/// [`.to_compile_error()`]: Error::to_compile_error
/// [`.into_compile_error()`]: Error::into_compile_error
///
/// ```
/// # extern crate proc_macro;
/// #
/// # use proc_macro::TokenStream;
/// # use syn::{parse_macro_input, DeriveInput};
/// #
/// # const IGNORE: &str = stringify! {
/// #[proc_macro_derive(MyDerive)]
/// # };
/// pub fn my_derive(input: TokenStream) -> TokenStream {
///     let input = parse_macro_input!(input as DeriveInput);
///
///     // fn(DeriveInput) -> syn::Result<proc_macro2::TokenStream>
///     expand::my_derive(input)
///         .unwrap_or_else(syn::Error::into_compile_error)
///         .into()
/// }
/// #
/// # mod expand {
/// #     use proc_macro2::TokenStream;
/// #     use syn::{DeriveInput, Result};
/// #
/// #     pub fn my_derive(input: DeriveInput) -> Result<TokenStream> {
/// #         unimplemented!()
/// #     }
/// # }
/// ```
pub struct Error {
    messages: Vec<ErrorMessage>,
}

struct ErrorMessage {
    // Span is implemented as an index into a thread-local interner to keep the
    // size small. It is not safe to access from a different thread. We want
    // errors to be Send and Sync to play nicely with ecosystem crates for error
    // handling, so pin the span we're given to its original thread and assume
    // it is Span::call_site if accessed from any other thread.
    span: ThreadBound<SpanRange>,
    message: String,
}

// Cannot use std::ops::Range<Span> because that does not implement Copy,
// whereas ThreadBound<T> requires a Copy impl as a way to ensure no Drop impls
// are involved.
struct SpanRange {
    start: Span,
    end: Span,
}

#[cfg(test)]
struct _Test
where
    Error: Send + Sync;

impl Error {
    /// Usually the [`ParseStream::error`] method will be used instead, which
    /// automatically uses the correct span from the current position of the
    /// parse stream.
    ///
    /// Use `Error::new` when the error needs to be triggered on some span other
    /// than where the parse stream is currently positioned.
    ///
    /// [`ParseStream::error`]: crate::parse::ParseBuffer::error
    ///
    /// # Example
    ///
    /// ```
    /// use syn::{Error, Ident, LitStr, Result, Token};
    /// use syn::parse::ParseStream;
    ///
    /// // Parses input that looks like `name = "string"` where the key must be
    /// // the identifier `name` and the value may be any string literal.
    /// // Returns the string literal.
    /// fn parse_name(input: ParseStream) -> Result<LitStr> {
    ///     let name_token: Ident = input.parse()?;
    ///     if name_token != "name" {
    ///         // Trigger an error not on the current position of the stream,
    ///         // but on the position of the unexpected identifier.
    ///         return Err(Error::new(name_token.span(), "expected `name`"));
    ///     }
    ///     input.parse::<Token![=]>()?;
    ///     let s: LitStr = input.parse()?;
    ///     Ok(s)
    /// }
    /// ```
    pub fn new<T: Display>(span: Span, message: T) -> Self {
        return new(span, message.to_string());

        fn new(span: Span, message: String) -> Error {
            Error {
                messages: vec![ErrorMessage {
                    span: ThreadBound::new(SpanRange {
                        start: span,
                        end: span,
                    }),
                    message,
                }],
            }
        }
    }

    /// Creates an error with the specified message spanning the given syntax
    /// tree node.
    ///
    /// Unlike the `Error::new` constructor, this constructor takes an argument
    /// `tokens` which is a syntax tree node. This allows the resulting `Error`
    /// to attempt to span all tokens inside of `tokens`. While you would
    /// typically be able to use the `Spanned` trait with the above `Error::new`
    /// constructor, implementation limitations today mean that
    /// `Error::new_spanned` may provide a higher-quality error message on
    /// stable Rust.
    ///
    /// When in doubt it's recommended to stick to `Error::new` (or
    /// `ParseStream::error`)!
    #[cfg(feature = "printing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    pub fn new_spanned<T: ToTokens, U: Display>(tokens: T, message: U) -> Self {
        return new_spanned(tokens.into_token_stream(), message.to_string());

        fn new_spanned(tokens: TokenStream, message: String) -> Error {
            let mut iter = tokens.into_iter();
            let start = iter.next().map_or_else(Span::call_site, |t| t.span());
            let end = iter.last().map_or(start, |t| t.span());
            Error {
                messages: vec![ErrorMessage {
                    span: ThreadBound::new(SpanRange { start, end }),
                    message,
                }],
            }
        }
    }

    /// The source location of the error.
    ///
    /// Spans are not thread-safe so this function returns `Span::call_site()`
    /// if called from a different thread than the one on which the `Error` was
    /// originally created.
    pub fn span(&self) -> Span {
        let SpanRange { start, end } = match self.messages[0].span.get() {
            Some(span) => *span,
            None => return Span::call_site(),
        };
        start.join(end).unwrap_or(start)
    }

    /// Render the error as an invocation of [`compile_error!`].
    ///
    /// The [`parse_macro_input!`] macro provides a convenient way to invoke
    /// this method correctly in a procedural macro.
    ///
    /// [`compile_error!`]: std::compile_error!
    /// [`parse_macro_input!`]: crate::parse_macro_input!
    pub fn to_compile_error(&self) -> TokenStream {
        self.messages
            .iter()
            .map(ErrorMessage::to_compile_error)
            .collect()
    }

    /// Render the error as an invocation of [`compile_error!`].
    ///
    /// [`compile_error!`]: std::compile_error!
    ///
    /// # Example
    ///
    /// ```
    /// # extern crate proc_macro;
    /// #
    /// use proc_macro::TokenStream;
    /// use syn::{parse_macro_input, DeriveInput, Error};
    ///
    /// # const _: &str = stringify! {
    /// #[proc_macro_derive(MyTrait)]
    /// # };
    /// pub fn derive_my_trait(input: TokenStream) -> TokenStream {
    ///     let input = parse_macro_input!(input as DeriveInput);
    ///     my_trait::expand(input)
    ///         .unwrap_or_else(Error::into_compile_error)
    ///         .into()
    /// }
    ///
    /// mod my_trait {
    ///     use proc_macro2::TokenStream;
    ///     use syn::{DeriveInput, Result};
    ///
    ///     pub(crate) fn expand(input: DeriveInput) -> Result<TokenStream> {
    ///         /* ... */
    ///         # unimplemented!()
    ///     }
    /// }
    /// ```
    pub fn into_compile_error(self) -> TokenStream {
        self.to_compile_error()
    }

    /// Add another error message to self such that when `to_compile_error()` is
    /// called, both errors will be emitted together.
    pub fn combine(&mut self, another: Error) {
        self.messages.extend(another.messages);
    }
}

impl ErrorMessage {
    fn to_compile_error(&self) -> TokenStream {
        let (start, end) = match self.span.get() {
            Some(range) => (range.start, range.end),
            None => (Span::call_site(), Span::call_site()),
        };

        // ::core::compile_error!($message)
        TokenStream::from_iter([
            TokenTree::Punct({
                let mut punct = Punct::new(':', Spacing::Joint);
                punct.set_span(start);
                punct
            }),
            TokenTree::Punct({
                let mut punct = Punct::new(':', Spacing::Alone);
                punct.set_span(start);
                punct
            }),
            TokenTree::Ident(Ident::new("core", start)),
            TokenTree::Punct({
                let mut punct = Punct::new(':', Spacing::Joint);
                punct.set_span(start);
                punct
            }),
            TokenTree::Punct({
                let mut punct = Punct::new(':', Spacing::Alone);
                punct.set_span(start);
                punct
            }),
            TokenTree::Ident(Ident::new("compile_error", start)),
            TokenTree::Punct({
                let mut punct = Punct::new('!', Spacing::Alone);
                punct.set_span(start);
                punct
            }),
            TokenTree::Group({
                let mut group = Group::new(Delimiter::Brace, {
                    TokenStream::from_iter([TokenTree::Literal({
                        let mut string = Literal::string(&self.message);
                        string.set_span(end);
                        string
                    })])
                });
                group.set_span(end);
                group
            }),
        ])
    }
}

#[cfg(feature = "parsing")]
pub(crate) fn new_at<T: Display>(scope: Span, cursor: Cursor, message: T) -> Error {
    if cursor.eof() {
        Error::new(scope, format!("unexpected end of input, {}", message))
    } else {
        let span = crate::buffer::open_span_of_group(cursor);
        Error::new(span, message)
    }
}

#[cfg(all(feature = "parsing", any(feature = "full", feature = "derive")))]
pub(crate) fn new2<T: Display>(start: Span, end: Span, message: T) -> Error {
    return new2(start, end, message.to_string());

    fn new2(start: Span, end: Span, message: String) -> Error {
        Error {
            messages: vec![ErrorMessage {
                span: ThreadBound::new(SpanRange { start, end }),
                message,
            }],
        }
    }
}

impl Debug for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        if self.messages.len() == 1 {
            formatter
                .debug_tuple("Error")
                .field(&self.messages[0])
                .finish()
        } else {
            formatter
                .debug_tuple("Error")
                .field(&self.messages)
                .finish()
        }
    }
}

impl Debug for ErrorMessage {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        Debug::fmt(&self.message, formatter)
    }
}

impl Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        formatter.write_str(&self.messages[0].message)
    }
}

impl Clone for Error {
    fn clone(&self) -> Self {
        Error {
            messages: self.messages.clone(),
        }
    }
}

impl Clone for ErrorMessage {
    fn clone(&self) -> Self {
        ErrorMessage {
            span: self.span,
            message: self.message.clone(),
        }
    }
}

impl Clone for SpanRange {
    fn clone(&self) -> Self {
        *self
    }
}

impl Copy for SpanRange {}

impl std::error::Error for Error {}

impl From<LexError> for Error {
    fn from(err: LexError) -> Self {
        Error::new(err.span(), err)
    }
}

impl IntoIterator for Error {
    type Item = Error;
    type IntoIter = IntoIter;

    fn into_iter(self) -> Self::IntoIter {
        IntoIter {
            messages: self.messages.into_iter(),
        }
    }
}

pub struct IntoIter {
    messages: vec::IntoIter<ErrorMessage>,
}

impl Iterator for IntoIter {
    type Item = Error;

    fn next(&mut self) -> Option<Self::Item> {
        Some(Error {
            messages: vec![self.messages.next()?],
        })
    }
}

impl<'a> IntoIterator for &'a Error {
    type Item = Error;
    type IntoIter = Iter<'a>;

    fn into_iter(self) -> Self::IntoIter {
        Iter {
            messages: self.messages.iter(),
        }
    }
}

pub struct Iter<'a> {
    messages: slice::Iter<'a, ErrorMessage>,
}

impl<'a> Iterator for Iter<'a> {
    type Item = Error;

    fn next(&mut self) -> Option<Self::Item> {
        Some(Error {
            messages: vec![self.messages.next()?.clone()],
        })
    }
}

impl Extend<Error> for Error {
    fn extend<T: IntoIterator<Item = Error>>(&mut self, iter: T) {
        for err in iter {
            self.combine(err);
        }
    }
}
