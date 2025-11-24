#[cfg(feature = "parsing")]
use crate::error::Result;
use crate::expr::Expr;
use crate::generics::TypeParamBound;
use crate::ident::Ident;
use crate::lifetime::Lifetime;
use crate::punctuated::Punctuated;
use crate::token;
use crate::ty::{ReturnType, Type};

ast_struct! {
    /// A path at which a named item is exported (e.g. `std::collections::HashMap`).
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub struct Path {
        pub leading_colon: Option<Token![::]>,
        pub segments: Punctuated<PathSegment, Token![::]>,
    }
}

impl<T> From<T> for Path
where
    T: Into<PathSegment>,
{
    fn from(segment: T) -> Self {
        let mut path = Path {
            leading_colon: None,
            segments: Punctuated::new(),
        };
        path.segments.push_value(segment.into());
        path
    }
}

impl Path {
    /// Determines whether this is a path of length 1 equal to the given
    /// ident.
    ///
    /// For them to compare equal, it must be the case that:
    ///
    /// - the path has no leading colon,
    /// - the number of path segments is 1,
    /// - the first path segment has no angle bracketed or parenthesized
    ///   path arguments, and
    /// - the ident of the first path segment is equal to the given one.
    ///
    /// # Example
    ///
    /// ```
    /// use proc_macro2::TokenStream;
    /// use syn::{Attribute, Error, Meta, Result};
    ///
    /// fn get_serde_meta_item(attr: &Attribute) -> Result<Option<&TokenStream>> {
    ///     if attr.path().is_ident("serde") {
    ///         match &attr.meta {
    ///             Meta::List(meta) => Ok(Some(&meta.tokens)),
    ///             bad => Err(Error::new_spanned(bad, "unrecognized attribute")),
    ///         }
    ///     } else {
    ///         Ok(None)
    ///     }
    /// }
    /// ```
    pub fn is_ident<I>(&self, ident: &I) -> bool
    where
        I: ?Sized,
        Ident: PartialEq<I>,
    {
        match self.get_ident() {
            Some(id) => id == ident,
            None => false,
        }
    }

    /// If this path consists of a single ident, returns the ident.
    ///
    /// A path is considered an ident if:
    ///
    /// - the path has no leading colon,
    /// - the number of path segments is 1, and
    /// - the first path segment has no angle bracketed or parenthesized
    ///   path arguments.
    pub fn get_ident(&self) -> Option<&Ident> {
        if self.leading_colon.is_none()
            && self.segments.len() == 1
            && self.segments[0].arguments.is_none()
        {
            Some(&self.segments[0].ident)
        } else {
            None
        }
    }

    /// An error if this path is not a single ident, as defined in `get_ident`.
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn require_ident(&self) -> Result<&Ident> {
        self.get_ident().ok_or_else(|| {
            crate::error::new2(
                self.segments.first().unwrap().ident.span(),
                self.segments.last().unwrap().ident.span(),
                "expected this path to be an identifier",
            )
        })
    }
}

ast_struct! {
    /// A segment of a path together with any path arguments on that segment.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub struct PathSegment {
        pub ident: Ident,
        pub arguments: PathArguments,
    }
}

impl<T> From<T> for PathSegment
where
    T: Into<Ident>,
{
    fn from(ident: T) -> Self {
        PathSegment {
            ident: ident.into(),
            arguments: PathArguments::None,
        }
    }
}

ast_enum! {
    /// Angle bracketed or parenthesized arguments of a path segment.
    ///
    /// ## Angle bracketed
    ///
    /// The `<'a, T>` in `std::slice::iter<'a, T>`.
    ///
    /// ## Parenthesized
    ///
    /// The `(A, B) -> C` in `Fn(A, B) -> C`.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub enum PathArguments {
        None,
        /// The `<'a, T>` in `std::slice::iter<'a, T>`.
        AngleBracketed(AngleBracketedGenericArguments),
        /// The `(A, B) -> C` in `Fn(A, B) -> C`.
        Parenthesized(ParenthesizedGenericArguments),
    }
}

impl Default for PathArguments {
    fn default() -> Self {
        PathArguments::None
    }
}

impl PathArguments {
    pub fn is_empty(&self) -> bool {
        match self {
            PathArguments::None => true,
            PathArguments::AngleBracketed(bracketed) => bracketed.args.is_empty(),
            PathArguments::Parenthesized(_) => false,
        }
    }

    pub fn is_none(&self) -> bool {
        match self {
            PathArguments::None => true,
            PathArguments::AngleBracketed(_) | PathArguments::Parenthesized(_) => false,
        }
    }
}

ast_enum! {
    /// An individual generic argument, like `'a`, `T`, or `Item = T`.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    #[non_exhaustive]
    pub enum GenericArgument {
        /// A lifetime argument.
        Lifetime(Lifetime),
        /// A type argument.
        Type(Type),
        /// A const expression. Must be inside of a block.
        ///
        /// NOTE: Identity expressions are represented as Type arguments, as
        /// they are indistinguishable syntactically.
        Const(Expr),
        /// A binding (equality constraint) on an associated type: the `Item =
        /// u8` in `Iterator<Item = u8>`.
        AssocType(AssocType),
        /// An equality constraint on an associated constant: the `PANIC =
        /// false` in `Trait<PANIC = false>`.
        AssocConst(AssocConst),
        /// An associated type bound: `Iterator<Item: Display>`.
        Constraint(Constraint),
    }
}

ast_struct! {
    /// Angle bracketed arguments of a path segment: the `<K, V>` in `HashMap<K,
    /// V>`.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub struct AngleBracketedGenericArguments {
        pub colon2_token: Option<Token![::]>,
        pub lt_token: Token![<],
        pub args: Punctuated<GenericArgument, Token![,]>,
        pub gt_token: Token![>],
    }
}

ast_struct! {
    /// A binding (equality constraint) on an associated type: the `Item = u8`
    /// in `Iterator<Item = u8>`.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub struct AssocType {
        pub ident: Ident,
        pub generics: Option<AngleBracketedGenericArguments>,
        pub eq_token: Token![=],
        pub ty: Type,
    }
}

ast_struct! {
    /// An equality constraint on an associated constant: the `PANIC = false` in
    /// `Trait<PANIC = false>`.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub struct AssocConst {
        pub ident: Ident,
        pub generics: Option<AngleBracketedGenericArguments>,
        pub eq_token: Token![=],
        pub value: Expr,
    }
}

ast_struct! {
    /// An associated type bound: `Iterator<Item: Display>`.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub struct Constraint {
        pub ident: Ident,
        pub generics: Option<AngleBracketedGenericArguments>,
        pub colon_token: Token![:],
        pub bounds: Punctuated<TypeParamBound, Token![+]>,
    }
}

ast_struct! {
    /// Arguments of a function path segment: the `(A, B) -> C` in `Fn(A,B) ->
    /// C`.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub struct ParenthesizedGenericArguments {
        pub paren_token: token::Paren,
        /// `(A, B)`
        pub inputs: Punctuated<Type, Token![,]>,
        /// `C`
        pub output: ReturnType,
    }
}

ast_struct! {
    /// The explicit Self type in a qualified path: the `T` in `<T as
    /// Display>::fmt`.
    ///
    /// The actual path, including the trait and the associated item, is stored
    /// separately. The `position` field represents the index of the associated
    /// item qualified with this Self type.
    ///
    /// ```text
    /// <Vec<T> as a::b::Trait>::AssociatedItem
    ///  ^~~~~~    ~~~~~~~~~~~~~~^
    ///  ty        position = 3
    ///
    /// <Vec<T>>::AssociatedItem
    ///  ^~~~~~   ^
    ///  ty       position = 0
    /// ```
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub struct QSelf {
        pub lt_token: Token![<],
        pub ty: Box<Type>,
        pub position: usize,
        pub as_token: Option<Token![as]>,
        pub gt_token: Token![>],
    }
}

#[cfg(feature = "parsing")]
pub(crate) mod parsing {
    use crate::error::Result;
    #[cfg(feature = "full")]
    use crate::expr::ExprBlock;
    use crate::expr::{Expr, ExprPath};
    use crate::ext::IdentExt as _;
    #[cfg(feature = "full")]
    use crate::generics::TypeParamBound;
    use crate::ident::Ident;
    use crate::lifetime::Lifetime;
    use crate::lit::Lit;
    use crate::parse::{Parse, ParseStream};
    #[cfg(feature = "full")]
    use crate::path::Constraint;
    use crate::path::{
        AngleBracketedGenericArguments, AssocConst, AssocType, GenericArgument,
        ParenthesizedGenericArguments, Path, PathArguments, PathSegment, QSelf,
    };
    use crate::punctuated::Punctuated;
    use crate::token;
    use crate::ty::{ReturnType, Type};
    #[cfg(not(feature = "full"))]
    use crate::verbatim;

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for Path {
        fn parse(input: ParseStream) -> Result<Self> {
            Self::parse_helper(input, false)
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for GenericArgument {
        fn parse(input: ParseStream) -> Result<Self> {
            if input.peek(Lifetime) && !input.peek2(Token![+]) {
                return Ok(GenericArgument::Lifetime(input.parse()?));
            }

            if input.peek(Lit) || input.peek(token::Brace) {
                return const_argument(input).map(GenericArgument::Const);
            }

            let mut argument: Type = input.parse()?;

            match argument {
                Type::Path(mut ty)
                    if ty.qself.is_none()
                        && ty.path.leading_colon.is_none()
                        && ty.path.segments.len() == 1
                        && match &ty.path.segments[0].arguments {
                            PathArguments::None | PathArguments::AngleBracketed(_) => true,
                            PathArguments::Parenthesized(_) => false,
                        } =>
                {
                    if let Some(eq_token) = input.parse::<Option<Token![=]>>()? {
                        let segment = ty.path.segments.pop().unwrap().into_value();
                        let ident = segment.ident;
                        let generics = match segment.arguments {
                            PathArguments::None => None,
                            PathArguments::AngleBracketed(arguments) => Some(arguments),
                            PathArguments::Parenthesized(_) => unreachable!(),
                        };
                        return if input.peek(Lit) || input.peek(token::Brace) {
                            Ok(GenericArgument::AssocConst(AssocConst {
                                ident,
                                generics,
                                eq_token,
                                value: const_argument(input)?,
                            }))
                        } else {
                            Ok(GenericArgument::AssocType(AssocType {
                                ident,
                                generics,
                                eq_token,
                                ty: input.parse()?,
                            }))
                        };
                    }

                    #[cfg(feature = "full")]
                    if let Some(colon_token) = input.parse::<Option<Token![:]>>()? {
                        let segment = ty.path.segments.pop().unwrap().into_value();
                        return Ok(GenericArgument::Constraint(Constraint {
                            ident: segment.ident,
                            generics: match segment.arguments {
                                PathArguments::None => None,
                                PathArguments::AngleBracketed(arguments) => Some(arguments),
                                PathArguments::Parenthesized(_) => unreachable!(),
                            },
                            colon_token,
                            bounds: {
                                let mut bounds = Punctuated::new();
                                loop {
                                    if input.peek(Token![,]) || input.peek(Token![>]) {
                                        break;
                                    }
                                    bounds.push_value({
                                        let allow_precise_capture = false;
                                        let allow_const = true;
                                        TypeParamBound::parse_single(
                                            input,
                                            allow_precise_capture,
                                            allow_const,
                                        )?
                                    });
                                    if !input.peek(Token![+]) {
                                        break;
                                    }
                                    let punct: Token![+] = input.parse()?;
                                    bounds.push_punct(punct);
                                }
                                bounds
                            },
                        }));
                    }

                    argument = Type::Path(ty);
                }
                _ => {}
            }

            Ok(GenericArgument::Type(argument))
        }
    }

    pub(crate) fn const_argument(input: ParseStream) -> Result<Expr> {
        let lookahead = input.lookahead1();

        if input.peek(Lit) {
            let lit = input.parse()?;
            return Ok(Expr::Lit(lit));
        }

        if input.peek(Ident) {
            let ident: Ident = input.parse()?;
            return Ok(Expr::Path(ExprPath {
                attrs: Vec::new(),
                qself: None,
                path: Path::from(ident),
            }));
        }

        if input.peek(token::Brace) {
            #[cfg(feature = "full")]
            {
                let block: ExprBlock = input.parse()?;
                return Ok(Expr::Block(block));
            }

            #[cfg(not(feature = "full"))]
            {
                let begin = input.fork();
                let content;
                braced!(content in input);
                content.parse::<Expr>()?;
                let verbatim = verbatim::between(&begin, input);
                return Ok(Expr::Verbatim(verbatim));
            }
        }

        Err(lookahead.error())
    }

    impl AngleBracketedGenericArguments {
        /// Parse `::<…>` with mandatory leading `::`.
        ///
        /// The ordinary [`Parse`] impl for `AngleBracketedGenericArguments`
        /// parses optional leading `::`.
        #[cfg(feature = "full")]
        #[cfg_attr(docsrs, doc(cfg(all(feature = "parsing", feature = "full"))))]
        pub fn parse_turbofish(input: ParseStream) -> Result<Self> {
            let colon2_token: Token![::] = input.parse()?;
            Self::do_parse(Some(colon2_token), input)
        }

        pub(crate) fn do_parse(
            colon2_token: Option<Token![::]>,
            input: ParseStream,
        ) -> Result<Self> {
            Ok(AngleBracketedGenericArguments {
                colon2_token,
                lt_token: input.parse()?,
                args: {
                    let mut args = Punctuated::new();
                    loop {
                        if input.peek(Token![>]) {
                            break;
                        }
                        let value: GenericArgument = input.parse()?;
                        args.push_value(value);
                        if input.peek(Token![>]) {
                            break;
                        }
                        let punct: Token![,] = input.parse()?;
                        args.push_punct(punct);
                    }
                    args
                },
                gt_token: input.parse()?,
            })
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for AngleBracketedGenericArguments {
        fn parse(input: ParseStream) -> Result<Self> {
            let colon2_token: Option<Token![::]> = input.parse()?;
            Self::do_parse(colon2_token, input)
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for ParenthesizedGenericArguments {
        fn parse(input: ParseStream) -> Result<Self> {
            let content;
            Ok(ParenthesizedGenericArguments {
                paren_token: parenthesized!(content in input),
                inputs: content.parse_terminated(Type::parse, Token![,])?,
                output: input.call(ReturnType::without_plus)?,
            })
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for PathSegment {
        fn parse(input: ParseStream) -> Result<Self> {
            Self::parse_helper(input, false)
        }
    }

    impl PathSegment {
        fn parse_helper(input: ParseStream, expr_style: bool) -> Result<Self> {
            if input.peek(Token![super])
                || input.peek(Token![self])
                || input.peek(Token![crate])
                || cfg!(feature = "full") && input.peek(Token![try])
            {
                let ident = input.call(Ident::parse_any)?;
                return Ok(PathSegment::from(ident));
            }

            let ident = if input.peek(Token![Self]) {
                input.call(Ident::parse_any)?
            } else {
                input.parse()?
            };

            if !expr_style
                && input.peek(Token![<])
                && !input.peek(Token![<=])
                && !input.peek(Token![<<=])
                || input.peek(Token![::]) && input.peek3(Token![<])
            {
                Ok(PathSegment {
                    ident,
                    arguments: PathArguments::AngleBracketed(input.parse()?),
                })
            } else {
                Ok(PathSegment::from(ident))
            }
        }
    }

    impl Path {
        /// Parse a `Path` containing no path arguments on any of its segments.
        ///
        /// # Example
        ///
        /// ```
        /// use syn::{Path, Result, Token};
        /// use syn::parse::{Parse, ParseStream};
        ///
        /// // A simplified single `use` statement like:
        /// //
        /// //     use std::collections::HashMap;
        /// //
        /// // Note that generic parameters are not allowed in a `use` statement
        /// // so the following must not be accepted.
        /// //
        /// //     use a::<b>::c;
        /// struct SingleUse {
        ///     use_token: Token![use],
        ///     path: Path,
        /// }
        ///
        /// impl Parse for SingleUse {
        ///     fn parse(input: ParseStream) -> Result<Self> {
        ///         Ok(SingleUse {
        ///             use_token: input.parse()?,
        ///             path: input.call(Path::parse_mod_style)?,
        ///         })
        ///     }
        /// }
        /// ```
        #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
        pub fn parse_mod_style(input: ParseStream) -> Result<Self> {
            Ok(Path {
                leading_colon: input.parse()?,
                segments: {
                    let mut segments = Punctuated::new();
                    loop {
                        if !input.peek(Ident)
                            && !input.peek(Token![super])
                            && !input.peek(Token![self])
                            && !input.peek(Token![Self])
                            && !input.peek(Token![crate])
                        {
                            break;
                        }
                        let ident = Ident::parse_any(input)?;
                        segments.push_value(PathSegment::from(ident));
                        if !input.peek(Token![::]) {
                            break;
                        }
                        let punct = input.parse()?;
                        segments.push_punct(punct);
                    }
                    if segments.is_empty() {
                        return Err(input.parse::<Ident>().unwrap_err());
                    } else if segments.trailing_punct() {
                        return Err(input.error("expected path segment after `::`"));
                    }
                    segments
                },
            })
        }

        pub(crate) fn parse_helper(input: ParseStream, expr_style: bool) -> Result<Self> {
            let mut path = Path {
                leading_colon: input.parse()?,
                segments: {
                    let mut segments = Punctuated::new();
                    let value = PathSegment::parse_helper(input, expr_style)?;
                    segments.push_value(value);
                    segments
                },
            };
            Path::parse_rest(input, &mut path, expr_style)?;
            Ok(path)
        }

        pub(crate) fn parse_rest(
            input: ParseStream,
            path: &mut Self,
            expr_style: bool,
        ) -> Result<()> {
            while input.peek(Token![::]) && !input.peek3(token::Paren) {
                let punct: Token![::] = input.parse()?;
                path.segments.push_punct(punct);
                let value = PathSegment::parse_helper(input, expr_style)?;
                path.segments.push_value(value);
            }
            Ok(())
        }

        pub(crate) fn is_mod_style(&self) -> bool {
            self.segments
                .iter()
                .all(|segment| segment.arguments.is_none())
        }
    }

    pub(crate) fn qpath(input: ParseStream, expr_style: bool) -> Result<(Option<QSelf>, Path)> {
        if input.peek(Token![<]) {
            let lt_token: Token![<] = input.parse()?;
            let this: Type = input.parse()?;
            let path = if input.peek(Token![as]) {
                let as_token: Token![as] = input.parse()?;
                let path: Path = input.parse()?;
                Some((as_token, path))
            } else {
                None
            };
            let gt_token: Token![>] = input.parse()?;
            let colon2_token: Token![::] = input.parse()?;
            let mut rest = Punctuated::new();
            loop {
                let path = PathSegment::parse_helper(input, expr_style)?;
                rest.push_value(path);
                if !input.peek(Token![::]) {
                    break;
                }
                let punct: Token![::] = input.parse()?;
                rest.push_punct(punct);
            }
            let (position, as_token, path) = match path {
                Some((as_token, mut path)) => {
                    let pos = path.segments.len();
                    path.segments.push_punct(colon2_token);
                    path.segments.extend(rest.into_pairs());
                    (pos, Some(as_token), path)
                }
                None => {
                    let path = Path {
                        leading_colon: Some(colon2_token),
                        segments: rest,
                    };
                    (0, None, path)
                }
            };
            let qself = QSelf {
                lt_token,
                ty: Box::new(this),
                position,
                as_token,
                gt_token,
            };
            Ok((Some(qself), path))
        } else {
            let path = Path::parse_helper(input, expr_style)?;
            Ok((None, path))
        }
    }
}

#[cfg(feature = "printing")]
pub(crate) mod printing {
    use crate::generics;
    use crate::path::{
        AngleBracketedGenericArguments, AssocConst, AssocType, Constraint, GenericArgument,
        ParenthesizedGenericArguments, Path, PathArguments, PathSegment, QSelf,
    };
    use crate::print::TokensOrDefault;
    #[cfg(feature = "parsing")]
    use crate::spanned::Spanned;
    #[cfg(feature = "parsing")]
    use proc_macro2::Span;
    use proc_macro2::TokenStream;
    use quote::ToTokens;
    use std::cmp;

    pub(crate) enum PathStyle {
        Expr,
        Mod,
        AsWritten,
    }

    impl Copy for PathStyle {}

    impl Clone for PathStyle {
        fn clone(&self) -> Self {
            *self
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for Path {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            print_path(tokens, self, PathStyle::AsWritten);
        }
    }

    pub(crate) fn print_path(tokens: &mut TokenStream, path: &Path, style: PathStyle) {
        path.leading_colon.to_tokens(tokens);
        for segment in path.segments.pairs() {
            print_path_segment(tokens, segment.value(), style);
            segment.punct().to_tokens(tokens);
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PathSegment {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            print_path_segment(tokens, self, PathStyle::AsWritten);
        }
    }

    fn print_path_segment(tokens: &mut TokenStream, segment: &PathSegment, style: PathStyle) {
        segment.ident.to_tokens(tokens);
        print_path_arguments(tokens, &segment.arguments, style);
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PathArguments {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            print_path_arguments(tokens, self, PathStyle::AsWritten);
        }
    }

    fn print_path_arguments(tokens: &mut TokenStream, arguments: &PathArguments, style: PathStyle) {
        match arguments {
            PathArguments::None => {}
            PathArguments::AngleBracketed(arguments) => {
                print_angle_bracketed_generic_arguments(tokens, arguments, style);
            }
            PathArguments::Parenthesized(arguments) => {
                print_parenthesized_generic_arguments(tokens, arguments, style);
            }
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for GenericArgument {
        #[allow(clippy::match_same_arms)]
        fn to_tokens(&self, tokens: &mut TokenStream) {
            match self {
                GenericArgument::Lifetime(lt) => lt.to_tokens(tokens),
                GenericArgument::Type(ty) => ty.to_tokens(tokens),
                GenericArgument::Const(expr) => {
                    generics::printing::print_const_argument(expr, tokens);
                }
                GenericArgument::AssocType(assoc) => assoc.to_tokens(tokens),
                GenericArgument::AssocConst(assoc) => assoc.to_tokens(tokens),
                GenericArgument::Constraint(constraint) => constraint.to_tokens(tokens),
            }
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for AngleBracketedGenericArguments {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            print_angle_bracketed_generic_arguments(tokens, self, PathStyle::AsWritten);
        }
    }

    pub(crate) fn print_angle_bracketed_generic_arguments(
        tokens: &mut TokenStream,
        arguments: &AngleBracketedGenericArguments,
        style: PathStyle,
    ) {
        if let PathStyle::Mod = style {
            return;
        }

        conditionally_print_turbofish(tokens, &arguments.colon2_token, style);
        arguments.lt_token.to_tokens(tokens);

        // Print lifetimes before types/consts/bindings, regardless of their
        // order in args.
        let mut trailing_or_empty = true;
        for param in arguments.args.pairs() {
            match param.value() {
                GenericArgument::Lifetime(_) => {
                    param.to_tokens(tokens);
                    trailing_or_empty = param.punct().is_some();
                }
                GenericArgument::Type(_)
                | GenericArgument::Const(_)
                | GenericArgument::AssocType(_)
                | GenericArgument::AssocConst(_)
                | GenericArgument::Constraint(_) => {}
            }
        }
        for param in arguments.args.pairs() {
            match param.value() {
                GenericArgument::Type(_)
                | GenericArgument::Const(_)
                | GenericArgument::AssocType(_)
                | GenericArgument::AssocConst(_)
                | GenericArgument::Constraint(_) => {
                    if !trailing_or_empty {
                        <Token![,]>::default().to_tokens(tokens);
                    }
                    param.to_tokens(tokens);
                    trailing_or_empty = param.punct().is_some();
                }
                GenericArgument::Lifetime(_) => {}
            }
        }

        arguments.gt_token.to_tokens(tokens);
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for AssocType {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            self.ident.to_tokens(tokens);
            self.generics.to_tokens(tokens);
            self.eq_token.to_tokens(tokens);
            self.ty.to_tokens(tokens);
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for AssocConst {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            self.ident.to_tokens(tokens);
            self.generics.to_tokens(tokens);
            self.eq_token.to_tokens(tokens);
            generics::printing::print_const_argument(&self.value, tokens);
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for Constraint {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            self.ident.to_tokens(tokens);
            self.generics.to_tokens(tokens);
            self.colon_token.to_tokens(tokens);
            self.bounds.to_tokens(tokens);
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for ParenthesizedGenericArguments {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            print_parenthesized_generic_arguments(tokens, self, PathStyle::AsWritten);
        }
    }

    fn print_parenthesized_generic_arguments(
        tokens: &mut TokenStream,
        arguments: &ParenthesizedGenericArguments,
        style: PathStyle,
    ) {
        if let PathStyle::Mod = style {
            return;
        }

        conditionally_print_turbofish(tokens, &None, style);
        arguments.paren_token.surround(tokens, |tokens| {
            arguments.inputs.to_tokens(tokens);
        });
        arguments.output.to_tokens(tokens);
    }

    pub(crate) fn print_qpath(
        tokens: &mut TokenStream,
        qself: &Option<QSelf>,
        path: &Path,
        style: PathStyle,
    ) {
        let qself = match qself {
            Some(qself) => qself,
            None => {
                print_path(tokens, path, style);
                return;
            }
        };
        qself.lt_token.to_tokens(tokens);
        qself.ty.to_tokens(tokens);

        let pos = cmp::min(qself.position, path.segments.len());
        let mut segments = path.segments.pairs();
        if pos > 0 {
            TokensOrDefault(&qself.as_token).to_tokens(tokens);
            path.leading_colon.to_tokens(tokens);
            for (i, segment) in segments.by_ref().take(pos).enumerate() {
                print_path_segment(tokens, segment.value(), PathStyle::AsWritten);
                if i + 1 == pos {
                    qself.gt_token.to_tokens(tokens);
                }
                segment.punct().to_tokens(tokens);
            }
        } else {
            qself.gt_token.to_tokens(tokens);
            path.leading_colon.to_tokens(tokens);
        }
        for segment in segments {
            print_path_segment(tokens, segment.value(), style);
            segment.punct().to_tokens(tokens);
        }
    }

    fn conditionally_print_turbofish(
        tokens: &mut TokenStream,
        colon2_token: &Option<Token![::]>,
        style: PathStyle,
    ) {
        match style {
            PathStyle::Expr => TokensOrDefault(colon2_token).to_tokens(tokens),
            PathStyle::Mod => unreachable!(),
            PathStyle::AsWritten => colon2_token.to_tokens(tokens),
        }
    }

    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(all(feature = "parsing", feature = "printing"))))]
    impl Spanned for QSelf {
        fn span(&self) -> Span {
            struct QSelfDelimiters<'a>(&'a QSelf);

            impl<'a> ToTokens for QSelfDelimiters<'a> {
                fn to_tokens(&self, tokens: &mut TokenStream) {
                    self.0.lt_token.to_tokens(tokens);
                    self.0.gt_token.to_tokens(tokens);
                }
            }

            QSelfDelimiters(self).span()
        }
    }
}
