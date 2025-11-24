//! Facility for interpreting structured content inside of an `Attribute`.

use crate::error::{Error, Result};
use crate::ext::IdentExt as _;
use crate::lit::Lit;
use crate::parse::{ParseStream, Parser};
use crate::path::{Path, PathSegment};
use crate::punctuated::Punctuated;
use proc_macro2::Ident;
use std::fmt::Display;

/// Make a parser that is usable with `parse_macro_input!` in a
/// `#[proc_macro_attribute]` macro.
///
/// *Warning:* When parsing attribute args **other than** the
/// `proc_macro::TokenStream` input of a `proc_macro_attribute`, you do **not**
/// need this function. In several cases your callers will get worse error
/// messages if you use this function, because the surrounding delimiter's span
/// is concealed from attribute macros by rustc. Use
/// [`Attribute::parse_nested_meta`] instead.
///
/// [`Attribute::parse_nested_meta`]: crate::Attribute::parse_nested_meta
///
/// # Example
///
/// This example implements an attribute macro whose invocations look like this:
///
/// ```
/// # const IGNORE: &str = stringify! {
/// #[tea(kind = "EarlGrey", hot)]
/// struct Picard {...}
/// # };
/// ```
///
/// The "parameters" supported by the attribute are:
///
/// - `kind = "..."`
/// - `hot`
/// - `with(sugar, milk, ...)`, a comma-separated list of ingredients
///
/// ```
/// # extern crate proc_macro;
/// #
/// use proc_macro::TokenStream;
/// use syn::{parse_macro_input, LitStr, Path};
///
/// # const IGNORE: &str = stringify! {
/// #[proc_macro_attribute]
/// # };
/// pub fn tea(args: TokenStream, input: TokenStream) -> TokenStream {
///     let mut kind: Option<LitStr> = None;
///     let mut hot: bool = false;
///     let mut with: Vec<Path> = Vec::new();
///     let tea_parser = syn::meta::parser(|meta| {
///         if meta.path.is_ident("kind") {
///             kind = Some(meta.value()?.parse()?);
///             Ok(())
///         } else if meta.path.is_ident("hot") {
///             hot = true;
///             Ok(())
///         } else if meta.path.is_ident("with") {
///             meta.parse_nested_meta(|meta| {
///                 with.push(meta.path);
///                 Ok(())
///             })
///         } else {
///             Err(meta.error("unsupported tea property"))
///         }
///     });
///
///     parse_macro_input!(args with tea_parser);
///     eprintln!("kind={kind:?} hot={hot} with={with:?}");
///
///     /* ... */
/// #   TokenStream::new()
/// }
/// ```
///
/// The `syn::meta` library will take care of dealing with the commas including
/// trailing commas, and producing sensible error messages on unexpected input.
///
/// ```console
/// error: expected `,`
///  --> src/main.rs:3:37
///   |
/// 3 | #[tea(kind = "EarlGrey", with(sugar = "lol", milk))]
///   |                                     ^
/// ```
///
/// # Example
///
/// Same as above but we factor out most of the logic into a separate function.
///
/// ```
/// # extern crate proc_macro;
/// #
/// use proc_macro::TokenStream;
/// use syn::meta::ParseNestedMeta;
/// use syn::parse::{Parser, Result};
/// use syn::{parse_macro_input, LitStr, Path};
///
/// # const IGNORE: &str = stringify! {
/// #[proc_macro_attribute]
/// # };
/// pub fn tea(args: TokenStream, input: TokenStream) -> TokenStream {
///     let mut attrs = TeaAttributes::default();
///     let tea_parser = syn::meta::parser(|meta| attrs.parse(meta));
///     parse_macro_input!(args with tea_parser);
///
///     /* ... */
/// #   TokenStream::new()
/// }
///
/// #[derive(Default)]
/// struct TeaAttributes {
///     kind: Option<LitStr>,
///     hot: bool,
///     with: Vec<Path>,
/// }
///
/// impl TeaAttributes {
///     fn parse(&mut self, meta: ParseNestedMeta) -> Result<()> {
///         if meta.path.is_ident("kind") {
///             self.kind = Some(meta.value()?.parse()?);
///             Ok(())
///         } else /* just like in last example */
/// #           { unimplemented!() }
///
///     }
/// }
/// ```
pub fn parser(logic: impl FnMut(ParseNestedMeta) -> Result<()>) -> impl Parser<Output = ()> {
    |input: ParseStream| {
        if input.is_empty() {
            Ok(())
        } else {
            parse_nested_meta(input, logic)
        }
    }
}

/// Context for parsing a single property in the conventional syntax for
/// structured attributes.
///
/// # Examples
///
/// Refer to usage examples on the following two entry-points:
///
/// - [`Attribute::parse_nested_meta`] if you have an entire `Attribute` to
///   parse. Always use this if possible. Generally this is able to produce
///   better error messages because `Attribute` holds span information for all
///   of the delimiters therein.
///
/// - [`syn::meta::parser`] if you are implementing a `proc_macro_attribute`
///   macro and parsing the arguments to the attribute macro, i.e. the ones
///   written in the same attribute that dispatched the macro invocation. Rustc
///   does not pass span information for the surrounding delimiters into the
///   attribute macro invocation in this situation, so error messages might be
///   less precise.
///
/// [`Attribute::parse_nested_meta`]: crate::Attribute::parse_nested_meta
/// [`syn::meta::parser`]: crate::meta::parser
#[non_exhaustive]
pub struct ParseNestedMeta<'a> {
    pub path: Path,
    pub input: ParseStream<'a>,
}

impl<'a> ParseNestedMeta<'a> {
    /// Used when parsing `key = "value"` syntax.
    ///
    /// All it does is advance `meta.input` past the `=` sign in the input. You
    /// could accomplish the same effect by writing
    /// `meta.parse::<Token![=]>()?`, so at most it is a minor convenience to
    /// use `meta.value()?`.
    ///
    /// # Example
    ///
    /// ```
    /// use syn::{parse_quote, Attribute, LitStr};
    ///
    /// let attr: Attribute = parse_quote! {
    ///     #[tea(kind = "EarlGrey")]
    /// };
    ///                                          // conceptually:
    /// if attr.path().is_ident("tea") {         // this parses the `tea`
    ///     attr.parse_nested_meta(|meta| {      // this parses the `(`
    ///         if meta.path.is_ident("kind") {  // this parses the `kind`
    ///             let value = meta.value()?;   // this parses the `=`
    ///             let s: LitStr = value.parse()?;  // this parses `"EarlGrey"`
    ///             if s.value() == "EarlGrey" {
    ///                 // ...
    ///             }
    ///             Ok(())
    ///         } else {
    ///             Err(meta.error("unsupported attribute"))
    ///         }
    ///     })?;
    /// }
    /// # anyhow::Ok(())
    /// ```
    pub fn value(&self) -> Result<ParseStream<'a>> {
        self.input.parse::<Token![=]>()?;
        Ok(self.input)
    }

    /// Used when parsing `list(...)` syntax **if** the content inside the
    /// nested parentheses is also expected to conform to Rust's structured
    /// attribute convention.
    ///
    /// # Example
    ///
    /// ```
    /// use syn::{parse_quote, Attribute};
    ///
    /// let attr: Attribute = parse_quote! {
    ///     #[tea(with(sugar, milk))]
    /// };
    ///
    /// if attr.path().is_ident("tea") {
    ///     attr.parse_nested_meta(|meta| {
    ///         if meta.path.is_ident("with") {
    ///             meta.parse_nested_meta(|meta| {  // <---
    ///                 if meta.path.is_ident("sugar") {
    ///                     // Here we can go even deeper if needed.
    ///                     Ok(())
    ///                 } else if meta.path.is_ident("milk") {
    ///                     Ok(())
    ///                 } else {
    ///                     Err(meta.error("unsupported ingredient"))
    ///                 }
    ///             })
    ///         } else {
    ///             Err(meta.error("unsupported tea property"))
    ///         }
    ///     })?;
    /// }
    /// # anyhow::Ok(())
    /// ```
    ///
    /// # Counterexample
    ///
    /// If you don't need `parse_nested_meta`'s help in parsing the content
    /// written within the nested parentheses, keep in mind that you can always
    /// just parse it yourself from the exposed ParseStream. Rust syntax permits
    /// arbitrary tokens within those parentheses so for the crazier stuff,
    /// `parse_nested_meta` is not what you want.
    ///
    /// ```
    /// use syn::{parenthesized, parse_quote, Attribute, LitInt};
    ///
    /// let attr: Attribute = parse_quote! {
    ///     #[repr(align(32))]
    /// };
    ///
    /// let mut align: Option<LitInt> = None;
    /// if attr.path().is_ident("repr") {
    ///     attr.parse_nested_meta(|meta| {
    ///         if meta.path.is_ident("align") {
    ///             let content;
    ///             parenthesized!(content in meta.input);
    ///             align = Some(content.parse()?);
    ///             Ok(())
    ///         } else {
    ///             Err(meta.error("unsupported repr"))
    ///         }
    ///     })?;
    /// }
    /// # anyhow::Ok(())
    /// ```
    pub fn parse_nested_meta(
        &self,
        logic: impl FnMut(ParseNestedMeta) -> Result<()>,
    ) -> Result<()> {
        let content;
        parenthesized!(content in self.input);
        parse_nested_meta(&content, logic)
    }

    /// Report that the attribute's content did not conform to expectations.
    ///
    /// The span of the resulting error will cover `meta.path` *and* everything
    /// that has been parsed so far since it.
    ///
    /// There are 2 ways you might call this. First, if `meta.path` is not
    /// something you recognize:
    ///
    /// ```
    /// # use syn::Attribute;
    /// #
    /// # fn example(attr: &Attribute) -> syn::Result<()> {
    /// attr.parse_nested_meta(|meta| {
    ///     if meta.path.is_ident("kind") {
    ///         // ...
    ///         Ok(())
    ///     } else {
    ///         Err(meta.error("unsupported tea property"))
    ///     }
    /// })?;
    /// # Ok(())
    /// # }
    /// ```
    ///
    /// In this case, it behaves exactly like
    /// `syn::Error::new_spanned(&meta.path, "message...")`.
    ///
    /// ```console
    /// error: unsupported tea property
    ///  --> src/main.rs:3:26
    ///   |
    /// 3 | #[tea(kind = "EarlGrey", wat = "foo")]
    ///   |                          ^^^
    /// ```
    ///
    /// More usefully, the second place is if you've already parsed a value but
    /// have decided not to accept the value:
    ///
    /// ```
    /// # use syn::Attribute;
    /// #
    /// # fn example(attr: &Attribute) -> syn::Result<()> {
    /// use syn::Expr;
    ///
    /// attr.parse_nested_meta(|meta| {
    ///     if meta.path.is_ident("kind") {
    ///         let expr: Expr = meta.value()?.parse()?;
    ///         match expr {
    ///             Expr::Lit(expr) => /* ... */
    /// #               unimplemented!(),
    ///             Expr::Path(expr) => /* ... */
    /// #               unimplemented!(),
    ///             Expr::Macro(expr) => /* ... */
    /// #               unimplemented!(),
    ///             _ => Err(meta.error("tea kind must be a string literal, path, or macro")),
    ///         }
    ///     } else /* as above */
    /// #       { unimplemented!() }
    ///
    /// })?;
    /// # Ok(())
    /// # }
    /// ```
    ///
    /// ```console
    /// error: tea kind must be a string literal, path, or macro
    ///  --> src/main.rs:3:7
    ///   |
    /// 3 | #[tea(kind = async { replicator.await })]
    ///   |       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    /// ```
    ///
    /// Often you may want to use `syn::Error::new_spanned` even in this
    /// situation. In the above code, that would be:
    ///
    /// ```
    /// # use syn::{Error, Expr};
    /// #
    /// # fn example(expr: Expr) -> syn::Result<()> {
    ///     match expr {
    ///         Expr::Lit(expr) => /* ... */
    /// #           unimplemented!(),
    ///         Expr::Path(expr) => /* ... */
    /// #           unimplemented!(),
    ///         Expr::Macro(expr) => /* ... */
    /// #           unimplemented!(),
    ///         _ => Err(Error::new_spanned(expr, "unsupported expression type for `kind`")),
    ///     }
    /// # }
    /// ```
    ///
    /// ```console
    /// error: unsupported expression type for `kind`
    ///  --> src/main.rs:3:14
    ///   |
    /// 3 | #[tea(kind = async { replicator.await })]
    ///   |              ^^^^^^^^^^^^^^^^^^^^^^^^^^
    /// ```
    pub fn error(&self, msg: impl Display) -> Error {
        let start_span = self.path.segments[0].ident.span();
        let end_span = self.input.cursor().prev_span();
        crate::error::new2(start_span, end_span, msg)
    }
}

pub(crate) fn parse_nested_meta(
    input: ParseStream,
    mut logic: impl FnMut(ParseNestedMeta) -> Result<()>,
) -> Result<()> {
    loop {
        let path = input.call(parse_meta_path)?;
        logic(ParseNestedMeta { path, input })?;
        if input.is_empty() {
            return Ok(());
        }
        input.parse::<Token![,]>()?;
        if input.is_empty() {
            return Ok(());
        }
    }
}

// Like Path::parse_mod_style, but accepts keywords in the path.
fn parse_meta_path(input: ParseStream) -> Result<Path> {
    Ok(Path {
        leading_colon: input.parse()?,
        segments: {
            let mut segments = Punctuated::new();
            if input.peek(Ident::peek_any) {
                let ident = Ident::parse_any(input)?;
                segments.push_value(PathSegment::from(ident));
            } else if input.is_empty() {
                return Err(input.error("expected nested attribute"));
            } else if input.peek(Lit) {
                return Err(input.error("unexpected literal in nested attribute, expected ident"));
            } else {
                return Err(input.error("unexpected token in nested attribute, expected ident"));
            }
            while input.peek(Token![::]) {
                let punct = input.parse()?;
                segments.push_punct(punct);
                let ident = Ident::parse_any(input)?;
                segments.push_value(PathSegment::from(ident));
            }
            segments
        },
    })
}
