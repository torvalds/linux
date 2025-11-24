#[cfg(feature = "parsing")]
use crate::error::Error;
#[cfg(feature = "parsing")]
use crate::error::Result;
use crate::expr::Expr;
use crate::mac::MacroDelimiter;
#[cfg(feature = "parsing")]
use crate::meta::{self, ParseNestedMeta};
#[cfg(feature = "parsing")]
use crate::parse::{Parse, ParseStream, Parser};
use crate::path::Path;
use crate::token;
use proc_macro2::TokenStream;
#[cfg(feature = "printing")]
use std::iter;
#[cfg(feature = "printing")]
use std::slice;

ast_struct! {
    /// An attribute, like `#[repr(transparent)]`.
    ///
    /// <br>
    ///
    /// # Syntax
    ///
    /// Rust has six types of attributes.
    ///
    /// - Outer attributes like `#[repr(transparent)]`. These appear outside or
    ///   in front of the item they describe.
    ///
    /// - Inner attributes like `#![feature(proc_macro)]`. These appear inside
    ///   of the item they describe, usually a module.
    ///
    /// - Outer one-line doc comments like `/// Example`.
    ///
    /// - Inner one-line doc comments like `//! Please file an issue`.
    ///
    /// - Outer documentation blocks `/** Example */`.
    ///
    /// - Inner documentation blocks `/*! Please file an issue */`.
    ///
    /// The `style` field of type `AttrStyle` distinguishes whether an attribute
    /// is outer or inner.
    ///
    /// Every attribute has a `path` that indicates the intended interpretation
    /// of the rest of the attribute's contents. The path and the optional
    /// additional contents are represented together in the `meta` field of the
    /// attribute in three possible varieties:
    ///
    /// - Meta::Path &mdash; attributes whose information content conveys just a
    ///   path, for example the `#[test]` attribute.
    ///
    /// - Meta::List &mdash; attributes that carry arbitrary tokens after the
    ///   path, surrounded by a delimiter (parenthesis, bracket, or brace). For
    ///   example `#[derive(Copy)]` or `#[precondition(x < 5)]`.
    ///
    /// - Meta::NameValue &mdash; attributes with an `=` sign after the path,
    ///   followed by a Rust expression. For example `#[path =
    ///   "sys/windows.rs"]`.
    ///
    /// All doc comments are represented in the NameValue style with a path of
    /// "doc", as this is how they are processed by the compiler and by
    /// `macro_rules!` macros.
    ///
    /// ```text
    /// #[derive(Copy, Clone)]
    ///   ~~~~~~Path
    ///   ^^^^^^^^^^^^^^^^^^^Meta::List
    ///
    /// #[path = "sys/windows.rs"]
    ///   ~~~~Path
    ///   ^^^^^^^^^^^^^^^^^^^^^^^Meta::NameValue
    ///
    /// #[test]
    ///   ^^^^Meta::Path
    /// ```
    ///
    /// <br>
    ///
    /// # Parsing from tokens to Attribute
    ///
    /// This type does not implement the [`Parse`] trait and thus cannot be
    /// parsed directly by [`ParseStream::parse`]. Instead use
    /// [`ParseStream::call`] with one of the two parser functions
    /// [`Attribute::parse_outer`] or [`Attribute::parse_inner`] depending on
    /// which you intend to parse.
    ///
    /// [`Parse`]: crate::parse::Parse
    /// [`ParseStream::parse`]: crate::parse::ParseBuffer::parse
    /// [`ParseStream::call`]: crate::parse::ParseBuffer::call
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
    ///
    /// <p><br></p>
    ///
    /// # Parsing from Attribute to structured arguments
    ///
    /// The grammar of attributes in Rust is very flexible, which makes the
    /// syntax tree not that useful on its own. In particular, arguments of the
    /// `Meta::List` variety of attribute are held in an arbitrary `tokens:
    /// TokenStream`. Macros are expected to check the `path` of the attribute,
    /// decide whether they recognize it, and then parse the remaining tokens
    /// according to whatever grammar they wish to require for that kind of
    /// attribute. Use [`parse_args()`] to parse those tokens into the expected
    /// data structure.
    ///
    /// [`parse_args()`]: Attribute::parse_args
    ///
    /// <p><br></p>
    ///
    /// # Doc comments
    ///
    /// The compiler transforms doc comments, such as `/// comment` and `/*!
    /// comment */`, into attributes before macros are expanded. Each comment is
    /// expanded into an attribute of the form `#[doc = r"comment"]`.
    ///
    /// As an example, the following `mod` items are expanded identically:
    ///
    /// ```
    /// # use syn::{ItemMod, parse_quote};
    /// let doc: ItemMod = parse_quote! {
    ///     /// Single line doc comments
    ///     /// We write so many!
    ///     /**
    ///      * Multi-line comments...
    ///      * May span many lines
    ///      */
    ///     mod example {
    ///         //! Of course, they can be inner too
    ///         /*! And fit in a single line */
    ///     }
    /// };
    /// let attr: ItemMod = parse_quote! {
    ///     #[doc = r" Single line doc comments"]
    ///     #[doc = r" We write so many!"]
    ///     #[doc = r"
    ///      * Multi-line comments...
    ///      * May span many lines
    ///      "]
    ///     mod example {
    ///         #![doc = r" Of course, they can be inner too"]
    ///         #![doc = r" And fit in a single line "]
    ///     }
    /// };
    /// assert_eq!(doc, attr);
    /// ```
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub struct Attribute {
        pub pound_token: Token![#],
        pub style: AttrStyle,
        pub bracket_token: token::Bracket,
        pub meta: Meta,
    }
}

impl Attribute {
    /// Returns the path that identifies the interpretation of this attribute.
    ///
    /// For example this would return the `test` in `#[test]`, the `derive` in
    /// `#[derive(Copy)]`, and the `path` in `#[path = "sys/windows.rs"]`.
    pub fn path(&self) -> &Path {
        self.meta.path()
    }

    /// Parse the arguments to the attribute as a syntax tree.
    ///
    /// This is similar to pulling out the `TokenStream` from `Meta::List` and
    /// doing `syn::parse2::<T>(meta_list.tokens)`, except that using
    /// `parse_args` the error message has a more useful span when `tokens` is
    /// empty.
    ///
    /// The surrounding delimiters are *not* included in the input to the
    /// parser.
    ///
    /// ```text
    /// #[my_attr(value < 5)]
    ///           ^^^^^^^^^ what gets parsed
    /// ```
    ///
    /// # Example
    ///
    /// ```
    /// use syn::{parse_quote, Attribute, Expr};
    ///
    /// let attr: Attribute = parse_quote! {
    ///     #[precondition(value < 5)]
    /// };
    ///
    /// if attr.path().is_ident("precondition") {
    ///     let precondition: Expr = attr.parse_args()?;
    ///     // ...
    /// }
    /// # anyhow::Ok(())
    /// ```
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn parse_args<T: Parse>(&self) -> Result<T> {
        self.parse_args_with(T::parse)
    }

    /// Parse the arguments to the attribute using the given parser.
    ///
    /// # Example
    ///
    /// ```
    /// use syn::{parse_quote, Attribute};
    ///
    /// let attr: Attribute = parse_quote! {
    ///     #[inception { #[brrrrrrraaaaawwwwrwrrrmrmrmmrmrmmmmm] }]
    /// };
    ///
    /// let bwom = attr.parse_args_with(Attribute::parse_outer)?;
    ///
    /// // Attribute does not have a Parse impl, so we couldn't directly do:
    /// // let bwom: Attribute = attr.parse_args()?;
    /// # anyhow::Ok(())
    /// ```
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn parse_args_with<F: Parser>(&self, parser: F) -> Result<F::Output> {
        match &self.meta {
            Meta::Path(path) => Err(crate::error::new2(
                path.segments.first().unwrap().ident.span(),
                path.segments.last().unwrap().ident.span(),
                format!(
                    "expected attribute arguments in parentheses: {}[{}(...)]",
                    parsing::DisplayAttrStyle(&self.style),
                    parsing::DisplayPath(path),
                ),
            )),
            Meta::NameValue(meta) => Err(Error::new(
                meta.eq_token.span,
                format_args!(
                    "expected parentheses: {}[{}(...)]",
                    parsing::DisplayAttrStyle(&self.style),
                    parsing::DisplayPath(&meta.path),
                ),
            )),
            Meta::List(meta) => meta.parse_args_with(parser),
        }
    }

    /// Parse the arguments to the attribute, expecting it to follow the
    /// conventional structure used by most of Rust's built-in attributes.
    ///
    /// The [*Meta Item Attribute Syntax*][syntax] section in the Rust reference
    /// explains the convention in more detail. Not all attributes follow this
    /// convention, so [`parse_args()`][Self::parse_args] is available if you
    /// need to parse arbitrarily goofy attribute syntax.
    ///
    /// [syntax]: https://doc.rust-lang.org/reference/attributes.html#meta-item-attribute-syntax
    ///
    /// # Example
    ///
    /// We'll parse a struct, and then parse some of Rust's `#[repr]` attribute
    /// syntax.
    ///
    /// ```
    /// use syn::{parenthesized, parse_quote, token, ItemStruct, LitInt};
    ///
    /// let input: ItemStruct = parse_quote! {
    ///     #[repr(C, align(4))]
    ///     pub struct MyStruct(u16, u32);
    /// };
    ///
    /// let mut repr_c = false;
    /// let mut repr_transparent = false;
    /// let mut repr_align = None::<usize>;
    /// let mut repr_packed = None::<usize>;
    /// for attr in &input.attrs {
    ///     if attr.path().is_ident("repr") {
    ///         attr.parse_nested_meta(|meta| {
    ///             // #[repr(C)]
    ///             if meta.path.is_ident("C") {
    ///                 repr_c = true;
    ///                 return Ok(());
    ///             }
    ///
    ///             // #[repr(transparent)]
    ///             if meta.path.is_ident("transparent") {
    ///                 repr_transparent = true;
    ///                 return Ok(());
    ///             }
    ///
    ///             // #[repr(align(N))]
    ///             if meta.path.is_ident("align") {
    ///                 let content;
    ///                 parenthesized!(content in meta.input);
    ///                 let lit: LitInt = content.parse()?;
    ///                 let n: usize = lit.base10_parse()?;
    ///                 repr_align = Some(n);
    ///                 return Ok(());
    ///             }
    ///
    ///             // #[repr(packed)] or #[repr(packed(N))], omitted N means 1
    ///             if meta.path.is_ident("packed") {
    ///                 if meta.input.peek(token::Paren) {
    ///                     let content;
    ///                     parenthesized!(content in meta.input);
    ///                     let lit: LitInt = content.parse()?;
    ///                     let n: usize = lit.base10_parse()?;
    ///                     repr_packed = Some(n);
    ///                 } else {
    ///                     repr_packed = Some(1);
    ///                 }
    ///                 return Ok(());
    ///             }
    ///
    ///             Err(meta.error("unrecognized repr"))
    ///         })?;
    ///     }
    /// }
    /// # anyhow::Ok(())
    /// ```
    ///
    /// # Alternatives
    ///
    /// In some cases, for attributes which have nested layers of structured
    /// content, the following less flexible approach might be more convenient:
    ///
    /// ```
    /// # use syn::{parse_quote, ItemStruct};
    /// #
    /// # let input: ItemStruct = parse_quote! {
    /// #     #[repr(C, align(4))]
    /// #     pub struct MyStruct(u16, u32);
    /// # };
    /// #
    /// use syn::punctuated::Punctuated;
    /// use syn::{parenthesized, token, Error, LitInt, Meta, Token};
    ///
    /// let mut repr_c = false;
    /// let mut repr_transparent = false;
    /// let mut repr_align = None::<usize>;
    /// let mut repr_packed = None::<usize>;
    /// for attr in &input.attrs {
    ///     if attr.path().is_ident("repr") {
    ///         let nested = attr.parse_args_with(Punctuated::<Meta, Token![,]>::parse_terminated)?;
    ///         for meta in nested {
    ///             match meta {
    ///                 // #[repr(C)]
    ///                 Meta::Path(path) if path.is_ident("C") => {
    ///                     repr_c = true;
    ///                 }
    ///
    ///                 // #[repr(align(N))]
    ///                 Meta::List(meta) if meta.path.is_ident("align") => {
    ///                     let lit: LitInt = meta.parse_args()?;
    ///                     let n: usize = lit.base10_parse()?;
    ///                     repr_align = Some(n);
    ///                 }
    ///
    ///                 /* ... */
    ///
    ///                 _ => {
    ///                     return Err(Error::new_spanned(meta, "unrecognized repr"));
    ///                 }
    ///             }
    ///         }
    ///     }
    /// }
    /// # Ok(())
    /// ```
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn parse_nested_meta(
        &self,
        logic: impl FnMut(ParseNestedMeta) -> Result<()>,
    ) -> Result<()> {
        self.parse_args_with(meta::parser(logic))
    }

    /// Parses zero or more outer attributes from the stream.
    ///
    /// # Example
    ///
    /// See
    /// [*Parsing from tokens to Attribute*](#parsing-from-tokens-to-attribute).
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn parse_outer(input: ParseStream) -> Result<Vec<Self>> {
        let mut attrs = Vec::new();
        while input.peek(Token![#]) {
            attrs.push(input.call(parsing::single_parse_outer)?);
        }
        Ok(attrs)
    }

    /// Parses zero or more inner attributes from the stream.
    ///
    /// # Example
    ///
    /// See
    /// [*Parsing from tokens to Attribute*](#parsing-from-tokens-to-attribute).
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn parse_inner(input: ParseStream) -> Result<Vec<Self>> {
        let mut attrs = Vec::new();
        parsing::parse_inner(input, &mut attrs)?;
        Ok(attrs)
    }
}

ast_enum! {
    /// Distinguishes between attributes that decorate an item and attributes
    /// that are contained within an item.
    ///
    /// # Outer attributes
    ///
    /// - `#[repr(transparent)]`
    /// - `/// # Example`
    /// - `/** Please file an issue */`
    ///
    /// # Inner attributes
    ///
    /// - `#![feature(proc_macro)]`
    /// - `//! # Example`
    /// - `/*! Please file an issue */`
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub enum AttrStyle {
        Outer,
        Inner(Token![!]),
    }
}

ast_enum! {
    /// Content of a compile-time structured attribute.
    ///
    /// ## Path
    ///
    /// A meta path is like the `test` in `#[test]`.
    ///
    /// ## List
    ///
    /// A meta list is like the `derive(Copy)` in `#[derive(Copy)]`.
    ///
    /// ## NameValue
    ///
    /// A name-value meta is like the `path = "..."` in `#[path =
    /// "sys/windows.rs"]`.
    ///
    /// # Syntax tree enum
    ///
    /// This type is a [syntax tree enum].
    ///
    /// [syntax tree enum]: crate::expr::Expr#syntax-tree-enums
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub enum Meta {
        Path(Path),

        /// A structured list within an attribute, like `derive(Copy, Clone)`.
        List(MetaList),

        /// A name-value pair within an attribute, like `feature = "nightly"`.
        NameValue(MetaNameValue),
    }
}

ast_struct! {
    /// A structured list within an attribute, like `derive(Copy, Clone)`.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub struct MetaList {
        pub path: Path,
        pub delimiter: MacroDelimiter,
        pub tokens: TokenStream,
    }
}

ast_struct! {
    /// A name-value pair within an attribute, like `feature = "nightly"`.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub struct MetaNameValue {
        pub path: Path,
        pub eq_token: Token![=],
        pub value: Expr,
    }
}

impl Meta {
    /// Returns the path that begins this structured meta item.
    ///
    /// For example this would return the `test` in `#[test]`, the `derive` in
    /// `#[derive(Copy)]`, and the `path` in `#[path = "sys/windows.rs"]`.
    pub fn path(&self) -> &Path {
        match self {
            Meta::Path(path) => path,
            Meta::List(meta) => &meta.path,
            Meta::NameValue(meta) => &meta.path,
        }
    }

    /// Error if this is a `Meta::List` or `Meta::NameValue`.
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn require_path_only(&self) -> Result<&Path> {
        let error_span = match self {
            Meta::Path(path) => return Ok(path),
            Meta::List(meta) => meta.delimiter.span().open(),
            Meta::NameValue(meta) => meta.eq_token.span,
        };
        Err(Error::new(error_span, "unexpected token in attribute"))
    }

    /// Error if this is a `Meta::Path` or `Meta::NameValue`.
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn require_list(&self) -> Result<&MetaList> {
        match self {
            Meta::List(meta) => Ok(meta),
            Meta::Path(path) => Err(crate::error::new2(
                path.segments.first().unwrap().ident.span(),
                path.segments.last().unwrap().ident.span(),
                format!(
                    "expected attribute arguments in parentheses: `{}(...)`",
                    parsing::DisplayPath(path),
                ),
            )),
            Meta::NameValue(meta) => Err(Error::new(meta.eq_token.span, "expected `(`")),
        }
    }

    /// Error if this is a `Meta::Path` or `Meta::List`.
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn require_name_value(&self) -> Result<&MetaNameValue> {
        match self {
            Meta::NameValue(meta) => Ok(meta),
            Meta::Path(path) => Err(crate::error::new2(
                path.segments.first().unwrap().ident.span(),
                path.segments.last().unwrap().ident.span(),
                format!(
                    "expected a value for this attribute: `{} = ...`",
                    parsing::DisplayPath(path),
                ),
            )),
            Meta::List(meta) => Err(Error::new(meta.delimiter.span().open(), "expected `=`")),
        }
    }
}

impl MetaList {
    /// See [`Attribute::parse_args`].
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn parse_args<T: Parse>(&self) -> Result<T> {
        self.parse_args_with(T::parse)
    }

    /// See [`Attribute::parse_args_with`].
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn parse_args_with<F: Parser>(&self, parser: F) -> Result<F::Output> {
        let scope = self.delimiter.span().close();
        crate::parse::parse_scoped(parser, scope, self.tokens.clone())
    }

    /// See [`Attribute::parse_nested_meta`].
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn parse_nested_meta(
        &self,
        logic: impl FnMut(ParseNestedMeta) -> Result<()>,
    ) -> Result<()> {
        self.parse_args_with(meta::parser(logic))
    }
}

#[cfg(feature = "printing")]
pub(crate) trait FilterAttrs<'a> {
    type Ret: Iterator<Item = &'a Attribute>;

    fn outer(self) -> Self::Ret;
    #[cfg(feature = "full")]
    fn inner(self) -> Self::Ret;
}

#[cfg(feature = "printing")]
impl<'a> FilterAttrs<'a> for &'a [Attribute] {
    type Ret = iter::Filter<slice::Iter<'a, Attribute>, fn(&&Attribute) -> bool>;

    fn outer(self) -> Self::Ret {
        fn is_outer(attr: &&Attribute) -> bool {
            match attr.style {
                AttrStyle::Outer => true,
                AttrStyle::Inner(_) => false,
            }
        }
        self.iter().filter(is_outer)
    }

    #[cfg(feature = "full")]
    fn inner(self) -> Self::Ret {
        fn is_inner(attr: &&Attribute) -> bool {
            match attr.style {
                AttrStyle::Inner(_) => true,
                AttrStyle::Outer => false,
            }
        }
        self.iter().filter(is_inner)
    }
}

impl From<Path> for Meta {
    fn from(meta: Path) -> Meta {
        Meta::Path(meta)
    }
}

impl From<MetaList> for Meta {
    fn from(meta: MetaList) -> Meta {
        Meta::List(meta)
    }
}

impl From<MetaNameValue> for Meta {
    fn from(meta: MetaNameValue) -> Meta {
        Meta::NameValue(meta)
    }
}

#[cfg(feature = "parsing")]
pub(crate) mod parsing {
    use crate::attr::{AttrStyle, Attribute, Meta, MetaList, MetaNameValue};
    use crate::error::Result;
    use crate::expr::{Expr, ExprLit};
    use crate::lit::Lit;
    use crate::parse::discouraged::Speculative as _;
    use crate::parse::{Parse, ParseStream};
    use crate::path::Path;
    use crate::{mac, token};
    use proc_macro2::Ident;
    use std::fmt::{self, Display};

    pub(crate) fn parse_inner(input: ParseStream, attrs: &mut Vec<Attribute>) -> Result<()> {
        while input.peek(Token![#]) && input.peek2(Token![!]) {
            attrs.push(input.call(single_parse_inner)?);
        }
        Ok(())
    }

    pub(crate) fn single_parse_inner(input: ParseStream) -> Result<Attribute> {
        let content;
        Ok(Attribute {
            pound_token: input.parse()?,
            style: AttrStyle::Inner(input.parse()?),
            bracket_token: bracketed!(content in input),
            meta: content.parse()?,
        })
    }

    pub(crate) fn single_parse_outer(input: ParseStream) -> Result<Attribute> {
        let content;
        Ok(Attribute {
            pound_token: input.parse()?,
            style: AttrStyle::Outer,
            bracket_token: bracketed!(content in input),
            meta: content.parse()?,
        })
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for Meta {
        fn parse(input: ParseStream) -> Result<Self> {
            let path = parse_outermost_meta_path(input)?;
            parse_meta_after_path(path, input)
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for MetaList {
        fn parse(input: ParseStream) -> Result<Self> {
            let path = parse_outermost_meta_path(input)?;
            parse_meta_list_after_path(path, input)
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for MetaNameValue {
        fn parse(input: ParseStream) -> Result<Self> {
            let path = parse_outermost_meta_path(input)?;
            parse_meta_name_value_after_path(path, input)
        }
    }

    // Unlike meta::parse_meta_path which accepts arbitrary keywords in the path,
    // only the `unsafe` keyword is accepted as an attribute's outermost path.
    fn parse_outermost_meta_path(input: ParseStream) -> Result<Path> {
        if input.peek(Token![unsafe]) {
            let unsafe_token: Token![unsafe] = input.parse()?;
            Ok(Path::from(Ident::new("unsafe", unsafe_token.span)))
        } else {
            Path::parse_mod_style(input)
        }
    }

    pub(crate) fn parse_meta_after_path(path: Path, input: ParseStream) -> Result<Meta> {
        if input.peek(token::Paren) || input.peek(token::Bracket) || input.peek(token::Brace) {
            parse_meta_list_after_path(path, input).map(Meta::List)
        } else if input.peek(Token![=]) {
            parse_meta_name_value_after_path(path, input).map(Meta::NameValue)
        } else {
            Ok(Meta::Path(path))
        }
    }

    fn parse_meta_list_after_path(path: Path, input: ParseStream) -> Result<MetaList> {
        let (delimiter, tokens) = mac::parse_delimiter(input)?;
        Ok(MetaList {
            path,
            delimiter,
            tokens,
        })
    }

    fn parse_meta_name_value_after_path(path: Path, input: ParseStream) -> Result<MetaNameValue> {
        let eq_token: Token![=] = input.parse()?;
        let ahead = input.fork();
        let lit: Option<Lit> = ahead.parse()?;
        let value = if let (Some(lit), true) = (lit, ahead.is_empty()) {
            input.advance_to(&ahead);
            Expr::Lit(ExprLit {
                attrs: Vec::new(),
                lit,
            })
        } else if input.peek(Token![#]) && input.peek2(token::Bracket) {
            return Err(input.error("unexpected attribute inside of attribute"));
        } else {
            input.parse()?
        };
        Ok(MetaNameValue {
            path,
            eq_token,
            value,
        })
    }

    pub(super) struct DisplayAttrStyle<'a>(pub &'a AttrStyle);

    impl<'a> Display for DisplayAttrStyle<'a> {
        fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
            formatter.write_str(match self.0 {
                AttrStyle::Outer => "#",
                AttrStyle::Inner(_) => "#!",
            })
        }
    }

    pub(super) struct DisplayPath<'a>(pub &'a Path);

    impl<'a> Display for DisplayPath<'a> {
        fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
            for (i, segment) in self.0.segments.iter().enumerate() {
                if i > 0 || self.0.leading_colon.is_some() {
                    formatter.write_str("::")?;
                }
                write!(formatter, "{}", segment.ident)?;
            }
            Ok(())
        }
    }
}

#[cfg(feature = "printing")]
mod printing {
    use crate::attr::{AttrStyle, Attribute, Meta, MetaList, MetaNameValue};
    use crate::path;
    use crate::path::printing::PathStyle;
    use proc_macro2::TokenStream;
    use quote::ToTokens;

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for Attribute {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            self.pound_token.to_tokens(tokens);
            if let AttrStyle::Inner(b) = &self.style {
                b.to_tokens(tokens);
            }
            self.bracket_token.surround(tokens, |tokens| {
                self.meta.to_tokens(tokens);
            });
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for Meta {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            match self {
                Meta::Path(path) => path::printing::print_path(tokens, path, PathStyle::Mod),
                Meta::List(meta_list) => meta_list.to_tokens(tokens),
                Meta::NameValue(meta_name_value) => meta_name_value.to_tokens(tokens),
            }
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for MetaList {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            path::printing::print_path(tokens, &self.path, PathStyle::Mod);
            self.delimiter.surround(tokens, self.tokens.clone());
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for MetaNameValue {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            path::printing::print_path(tokens, &self.path, PathStyle::Mod);
            self.eq_token.to_tokens(tokens);
            self.value.to_tokens(tokens);
        }
    }
}
