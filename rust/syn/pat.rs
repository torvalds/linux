use crate::attr::Attribute;
use crate::expr::Member;
use crate::ident::Ident;
use crate::path::{Path, QSelf};
use crate::punctuated::Punctuated;
use crate::token;
use crate::ty::Type;
use proc_macro2::TokenStream;

pub use crate::expr::{
    ExprConst as PatConst, ExprLit as PatLit, ExprMacro as PatMacro, ExprPath as PatPath,
    ExprRange as PatRange,
};

ast_enum_of_structs! {
    /// A pattern in a local binding, function signature, match expression, or
    /// various other places.
    ///
    /// # Syntax tree enum
    ///
    /// This type is a [syntax tree enum].
    ///
    /// [syntax tree enum]: crate::expr::Expr#syntax-tree-enums
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    #[non_exhaustive]
    pub enum Pat {
        /// A const block: `const { ... }`.
        Const(PatConst),

        /// A pattern that binds a new variable: `ref mut binding @ SUBPATTERN`.
        Ident(PatIdent),

        /// A literal pattern: `0`.
        Lit(PatLit),

        /// A macro in pattern position.
        Macro(PatMacro),

        /// A pattern that matches any one of a set of cases.
        Or(PatOr),

        /// A parenthesized pattern: `(A | B)`.
        Paren(PatParen),

        /// A path pattern like `Color::Red`, optionally qualified with a
        /// self-type.
        ///
        /// Unqualified path patterns can legally refer to variants, structs,
        /// constants or associated constants. Qualified path patterns like
        /// `<A>::B::C` and `<A as Trait>::B::C` can only legally refer to
        /// associated constants.
        Path(PatPath),

        /// A range pattern: `1..=2`.
        Range(PatRange),

        /// A reference pattern: `&mut var`.
        Reference(PatReference),

        /// The dots in a tuple or slice pattern: `[0, 1, ..]`.
        Rest(PatRest),

        /// A dynamically sized slice pattern: `[a, b, ref i @ .., y, z]`.
        Slice(PatSlice),

        /// A struct or struct variant pattern: `Variant { x, y, .. }`.
        Struct(PatStruct),

        /// A tuple pattern: `(a, b)`.
        Tuple(PatTuple),

        /// A tuple struct or tuple variant pattern: `Variant(x, y, .., z)`.
        TupleStruct(PatTupleStruct),

        /// A type ascription pattern: `foo: f64`.
        Type(PatType),

        /// Tokens in pattern position not interpreted by Syn.
        Verbatim(TokenStream),

        /// A pattern that matches any value: `_`.
        Wild(PatWild),

        // For testing exhaustiveness in downstream code, use the following idiom:
        //
        //     match pat {
        //         #![cfg_attr(test, deny(non_exhaustive_omitted_patterns))]
        //
        //         Pat::Box(pat) => {...}
        //         Pat::Ident(pat) => {...}
        //         ...
        //         Pat::Wild(pat) => {...}
        //
        //         _ => { /* some sane fallback */ }
        //     }
        //
        // This way we fail your tests but don't break your library when adding
        // a variant. You will be notified by a test failure when a variant is
        // added, so that you can add code to handle it, but your library will
        // continue to compile and work for downstream users in the interim.
    }
}

ast_struct! {
    /// A pattern that binds a new variable: `ref mut binding @ SUBPATTERN`.
    ///
    /// It may also be a unit struct or struct variant (e.g. `None`), or a
    /// constant; these cannot be distinguished syntactically.
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    pub struct PatIdent {
        pub attrs: Vec<Attribute>,
        pub by_ref: Option<Token![ref]>,
        pub mutability: Option<Token![mut]>,
        pub ident: Ident,
        pub subpat: Option<(Token![@], Box<Pat>)>,
    }
}

ast_struct! {
    /// A pattern that matches any one of a set of cases.
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    pub struct PatOr {
        pub attrs: Vec<Attribute>,
        pub leading_vert: Option<Token![|]>,
        pub cases: Punctuated<Pat, Token![|]>,
    }
}

ast_struct! {
    /// A parenthesized pattern: `(A | B)`.
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    pub struct PatParen {
        pub attrs: Vec<Attribute>,
        pub paren_token: token::Paren,
        pub pat: Box<Pat>,
    }
}

ast_struct! {
    /// A reference pattern: `&mut var`.
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    pub struct PatReference {
        pub attrs: Vec<Attribute>,
        pub and_token: Token![&],
        pub mutability: Option<Token![mut]>,
        pub pat: Box<Pat>,
    }
}

ast_struct! {
    /// The dots in a tuple or slice pattern: `[0, 1, ..]`.
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    pub struct PatRest {
        pub attrs: Vec<Attribute>,
        pub dot2_token: Token![..],
    }
}

ast_struct! {
    /// A dynamically sized slice pattern: `[a, b, ref i @ .., y, z]`.
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    pub struct PatSlice {
        pub attrs: Vec<Attribute>,
        pub bracket_token: token::Bracket,
        pub elems: Punctuated<Pat, Token![,]>,
    }
}

ast_struct! {
    /// A struct or struct variant pattern: `Variant { x, y, .. }`.
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    pub struct PatStruct {
        pub attrs: Vec<Attribute>,
        pub qself: Option<QSelf>,
        pub path: Path,
        pub brace_token: token::Brace,
        pub fields: Punctuated<FieldPat, Token![,]>,
        pub rest: Option<PatRest>,
    }
}

ast_struct! {
    /// A tuple pattern: `(a, b)`.
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    pub struct PatTuple {
        pub attrs: Vec<Attribute>,
        pub paren_token: token::Paren,
        pub elems: Punctuated<Pat, Token![,]>,
    }
}

ast_struct! {
    /// A tuple struct or tuple variant pattern: `Variant(x, y, .., z)`.
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    pub struct PatTupleStruct {
        pub attrs: Vec<Attribute>,
        pub qself: Option<QSelf>,
        pub path: Path,
        pub paren_token: token::Paren,
        pub elems: Punctuated<Pat, Token![,]>,
    }
}

ast_struct! {
    /// A type ascription pattern: `foo: f64`.
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    pub struct PatType {
        pub attrs: Vec<Attribute>,
        pub pat: Box<Pat>,
        pub colon_token: Token![:],
        pub ty: Box<Type>,
    }
}

ast_struct! {
    /// A pattern that matches any value: `_`.
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    pub struct PatWild {
        pub attrs: Vec<Attribute>,
        pub underscore_token: Token![_],
    }
}

ast_struct! {
    /// A single field in a struct pattern.
    ///
    /// Patterns like the fields of Foo `{ x, ref y, ref mut z }` are treated
    /// the same as `x: x, y: ref y, z: ref mut z` but there is no colon token.
    #[cfg_attr(docsrs, doc(cfg(feature = "full")))]
    pub struct FieldPat {
        pub attrs: Vec<Attribute>,
        pub member: Member,
        pub colon_token: Option<Token![:]>,
        pub pat: Box<Pat>,
    }
}

#[cfg(feature = "parsing")]
pub(crate) mod parsing {
    use crate::attr::Attribute;
    use crate::error::{self, Result};
    use crate::expr::{
        Expr, ExprConst, ExprLit, ExprMacro, ExprPath, ExprRange, Member, RangeLimits,
    };
    use crate::ext::IdentExt as _;
    use crate::ident::Ident;
    use crate::lit::Lit;
    use crate::mac::{self, Macro};
    use crate::parse::{Parse, ParseBuffer, ParseStream};
    use crate::pat::{
        FieldPat, Pat, PatIdent, PatOr, PatParen, PatReference, PatRest, PatSlice, PatStruct,
        PatTuple, PatTupleStruct, PatType, PatWild,
    };
    use crate::path::{self, Path, QSelf};
    use crate::punctuated::Punctuated;
    use crate::stmt::Block;
    use crate::token;
    use crate::verbatim;
    use proc_macro2::TokenStream;

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Pat {
        /// Parse a pattern that does _not_ involve `|` at the top level.
        ///
        /// This parser matches the behavior of the `$:pat_param` macro_rules
        /// matcher, and on editions prior to Rust 2021, the behavior of
        /// `$:pat`.
        ///
        /// In Rust syntax, some examples of where this syntax would occur are
        /// in the argument pattern of functions and closures. Patterns using
        /// `|` are not allowed to occur in these positions.
        ///
        /// ```compile_fail
        /// fn f(Some(_) | None: Option<T>) {
        ///     let _ = |Some(_) | None: Option<T>| {};
        ///     //       ^^^^^^^^^^^^^^^^^^^^^^^^^??? :(
        /// }
        /// ```
        ///
        /// ```console
        /// error: top-level or-patterns are not allowed in function parameters
        ///  --> src/main.rs:1:6
        ///   |
        /// 1 | fn f(Some(_) | None: Option<T>) {
        ///   |      ^^^^^^^^^^^^^^ help: wrap the pattern in parentheses: `(Some(_) | None)`
        /// ```
        pub fn parse_single(input: ParseStream) -> Result<Self> {
            let begin = input.fork();
            let lookahead = input.lookahead1();
            if lookahead.peek(Ident)
                && (input.peek2(Token![::])
                    || input.peek2(Token![!])
                    || input.peek2(token::Brace)
                    || input.peek2(token::Paren)
                    || input.peek2(Token![..]))
                || input.peek(Token![self]) && input.peek2(Token![::])
                || lookahead.peek(Token![::])
                || lookahead.peek(Token![<])
                || input.peek(Token![Self])
                || input.peek(Token![super])
                || input.peek(Token![crate])
            {
                pat_path_or_macro_or_struct_or_range(input)
            } else if lookahead.peek(Token![_]) {
                input.call(pat_wild).map(Pat::Wild)
            } else if input.peek(Token![box]) {
                pat_box(begin, input)
            } else if input.peek(Token![-]) || lookahead.peek(Lit) || lookahead.peek(Token![const])
            {
                pat_lit_or_range(input)
            } else if lookahead.peek(Token![ref])
                || lookahead.peek(Token![mut])
                || input.peek(Token![self])
                || input.peek(Ident)
            {
                input.call(pat_ident).map(Pat::Ident)
            } else if lookahead.peek(Token![&]) {
                input.call(pat_reference).map(Pat::Reference)
            } else if lookahead.peek(token::Paren) {
                input.call(pat_paren_or_tuple)
            } else if lookahead.peek(token::Bracket) {
                input.call(pat_slice).map(Pat::Slice)
            } else if lookahead.peek(Token![..]) && !input.peek(Token![...]) {
                pat_range_half_open(input)
            } else if lookahead.peek(Token![const]) {
                input.call(pat_const).map(Pat::Verbatim)
            } else {
                Err(lookahead.error())
            }
        }

        /// Parse a pattern, possibly involving `|`, but not a leading `|`.
        pub fn parse_multi(input: ParseStream) -> Result<Self> {
            multi_pat_impl(input, None)
        }

        /// Parse a pattern, possibly involving `|`, possibly including a
        /// leading `|`.
        ///
        /// This parser matches the behavior of the Rust 2021 edition's `$:pat`
        /// macro_rules matcher.
        ///
        /// In Rust syntax, an example of where this syntax would occur is in
        /// the pattern of a `match` arm, where the language permits an optional
        /// leading `|`, although it is not idiomatic to write one there in
        /// handwritten code.
        ///
        /// ```
        /// # let wat = None;
        /// match wat {
        ///     | None | Some(false) => {}
        ///     | Some(true) => {}
        /// }
        /// ```
        ///
        /// The compiler accepts it only to facilitate some situations in
        /// macro-generated code where a macro author might need to write:
        ///
        /// ```
        /// # macro_rules! doc {
        /// #     ($value:expr, ($($conditions1:pat),*), ($($conditions2:pat),*), $then:expr) => {
        /// match $value {
        ///     $(| $conditions1)* $(| $conditions2)* => $then
        /// }
        /// #     };
        /// # }
        /// #
        /// # doc!(true, (true), (false), {});
        /// # doc!(true, (), (true, false), {});
        /// # doc!(true, (true, false), (), {});
        /// ```
        ///
        /// Expressing the same thing correctly in the case that either one (but
        /// not both) of `$conditions1` and `$conditions2` might be empty,
        /// without leading `|`, is complex.
        ///
        /// Use [`Pat::parse_multi`] instead if you are not intending to support
        /// macro-generated macro input.
        pub fn parse_multi_with_leading_vert(input: ParseStream) -> Result<Self> {
            let leading_vert: Option<Token![|]> = input.parse()?;
            multi_pat_impl(input, leading_vert)
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for PatType {
        fn parse(input: ParseStream) -> Result<Self> {
            Ok(PatType {
                attrs: Vec::new(),
                pat: Box::new(Pat::parse_single(input)?),
                colon_token: input.parse()?,
                ty: input.parse()?,
            })
        }
    }

    fn multi_pat_impl(input: ParseStream, leading_vert: Option<Token![|]>) -> Result<Pat> {
        let mut pat = Pat::parse_single(input)?;
        if leading_vert.is_some()
            || input.peek(Token![|]) && !input.peek(Token![||]) && !input.peek(Token![|=])
        {
            let mut cases = Punctuated::new();
            cases.push_value(pat);
            while input.peek(Token![|]) && !input.peek(Token![||]) && !input.peek(Token![|=]) {
                let punct = input.parse()?;
                cases.push_punct(punct);
                let pat = Pat::parse_single(input)?;
                cases.push_value(pat);
            }
            pat = Pat::Or(PatOr {
                attrs: Vec::new(),
                leading_vert,
                cases,
            });
        }
        Ok(pat)
    }

    fn pat_path_or_macro_or_struct_or_range(input: ParseStream) -> Result<Pat> {
        let expr_style = true;
        let (qself, path) = path::parsing::qpath(input, expr_style)?;

        if qself.is_none()
            && input.peek(Token![!])
            && !input.peek(Token![!=])
            && path.is_mod_style()
        {
            let bang_token: Token![!] = input.parse()?;
            let (delimiter, tokens) = mac::parse_delimiter(input)?;
            return Ok(Pat::Macro(ExprMacro {
                attrs: Vec::new(),
                mac: Macro {
                    path,
                    bang_token,
                    delimiter,
                    tokens,
                },
            }));
        }

        if input.peek(token::Brace) {
            pat_struct(input, qself, path).map(Pat::Struct)
        } else if input.peek(token::Paren) {
            pat_tuple_struct(input, qself, path).map(Pat::TupleStruct)
        } else if input.peek(Token![..]) {
            pat_range(input, qself, path)
        } else {
            Ok(Pat::Path(ExprPath {
                attrs: Vec::new(),
                qself,
                path,
            }))
        }
    }

    fn pat_wild(input: ParseStream) -> Result<PatWild> {
        Ok(PatWild {
            attrs: Vec::new(),
            underscore_token: input.parse()?,
        })
    }

    fn pat_box(begin: ParseBuffer, input: ParseStream) -> Result<Pat> {
        input.parse::<Token![box]>()?;
        Pat::parse_single(input)?;
        Ok(Pat::Verbatim(verbatim::between(&begin, input)))
    }

    fn pat_ident(input: ParseStream) -> Result<PatIdent> {
        Ok(PatIdent {
            attrs: Vec::new(),
            by_ref: input.parse()?,
            mutability: input.parse()?,
            ident: {
                if input.peek(Token![self]) {
                    input.call(Ident::parse_any)?
                } else {
                    input.parse()?
                }
            },
            subpat: {
                if input.peek(Token![@]) {
                    let at_token: Token![@] = input.parse()?;
                    let subpat = Pat::parse_single(input)?;
                    Some((at_token, Box::new(subpat)))
                } else {
                    None
                }
            },
        })
    }

    fn pat_tuple_struct(
        input: ParseStream,
        qself: Option<QSelf>,
        path: Path,
    ) -> Result<PatTupleStruct> {
        let content;
        let paren_token = parenthesized!(content in input);

        let mut elems = Punctuated::new();
        while !content.is_empty() {
            let value = Pat::parse_multi_with_leading_vert(&content)?;
            elems.push_value(value);
            if content.is_empty() {
                break;
            }
            let punct = content.parse()?;
            elems.push_punct(punct);
        }

        Ok(PatTupleStruct {
            attrs: Vec::new(),
            qself,
            path,
            paren_token,
            elems,
        })
    }

    fn pat_struct(input: ParseStream, qself: Option<QSelf>, path: Path) -> Result<PatStruct> {
        let content;
        let brace_token = braced!(content in input);

        let mut fields = Punctuated::new();
        let mut rest = None;
        while !content.is_empty() {
            let attrs = content.call(Attribute::parse_outer)?;
            if content.peek(Token![..]) {
                rest = Some(PatRest {
                    attrs,
                    dot2_token: content.parse()?,
                });
                break;
            }
            let mut value = content.call(field_pat)?;
            value.attrs = attrs;
            fields.push_value(value);
            if content.is_empty() {
                break;
            }
            let punct: Token![,] = content.parse()?;
            fields.push_punct(punct);
        }

        Ok(PatStruct {
            attrs: Vec::new(),
            qself,
            path,
            brace_token,
            fields,
            rest,
        })
    }

    fn field_pat(input: ParseStream) -> Result<FieldPat> {
        let begin = input.fork();
        let boxed: Option<Token![box]> = input.parse()?;
        let by_ref: Option<Token![ref]> = input.parse()?;
        let mutability: Option<Token![mut]> = input.parse()?;

        let member = if boxed.is_some() || by_ref.is_some() || mutability.is_some() {
            input.parse().map(Member::Named)
        } else {
            input.parse()
        }?;

        if boxed.is_none() && by_ref.is_none() && mutability.is_none() && input.peek(Token![:])
            || !member.is_named()
        {
            return Ok(FieldPat {
                attrs: Vec::new(),
                member,
                colon_token: Some(input.parse()?),
                pat: Box::new(Pat::parse_multi_with_leading_vert(input)?),
            });
        }

        let ident = match member {
            Member::Named(ident) => ident,
            Member::Unnamed(_) => unreachable!(),
        };

        let pat = if boxed.is_some() {
            Pat::Verbatim(verbatim::between(&begin, input))
        } else {
            Pat::Ident(PatIdent {
                attrs: Vec::new(),
                by_ref,
                mutability,
                ident: ident.clone(),
                subpat: None,
            })
        };

        Ok(FieldPat {
            attrs: Vec::new(),
            member: Member::Named(ident),
            colon_token: None,
            pat: Box::new(pat),
        })
    }

    fn pat_range(input: ParseStream, qself: Option<QSelf>, path: Path) -> Result<Pat> {
        let limits = RangeLimits::parse_obsolete(input)?;
        let end = input.call(pat_range_bound)?;
        if let (RangeLimits::Closed(_), None) = (&limits, &end) {
            return Err(input.error("expected range upper bound"));
        }
        Ok(Pat::Range(ExprRange {
            attrs: Vec::new(),
            start: Some(Box::new(Expr::Path(ExprPath {
                attrs: Vec::new(),
                qself,
                path,
            }))),
            limits,
            end: end.map(PatRangeBound::into_expr),
        }))
    }

    fn pat_range_half_open(input: ParseStream) -> Result<Pat> {
        let limits: RangeLimits = input.parse()?;
        let end = input.call(pat_range_bound)?;
        if end.is_some() {
            Ok(Pat::Range(ExprRange {
                attrs: Vec::new(),
                start: None,
                limits,
                end: end.map(PatRangeBound::into_expr),
            }))
        } else {
            match limits {
                RangeLimits::HalfOpen(dot2_token) => Ok(Pat::Rest(PatRest {
                    attrs: Vec::new(),
                    dot2_token,
                })),
                RangeLimits::Closed(_) => Err(input.error("expected range upper bound")),
            }
        }
    }

    fn pat_paren_or_tuple(input: ParseStream) -> Result<Pat> {
        let content;
        let paren_token = parenthesized!(content in input);

        let mut elems = Punctuated::new();
        while !content.is_empty() {
            let value = Pat::parse_multi_with_leading_vert(&content)?;
            if content.is_empty() {
                if elems.is_empty() && !matches!(value, Pat::Rest(_)) {
                    return Ok(Pat::Paren(PatParen {
                        attrs: Vec::new(),
                        paren_token,
                        pat: Box::new(value),
                    }));
                }
                elems.push_value(value);
                break;
            }
            elems.push_value(value);
            let punct = content.parse()?;
            elems.push_punct(punct);
        }

        Ok(Pat::Tuple(PatTuple {
            attrs: Vec::new(),
            paren_token,
            elems,
        }))
    }

    fn pat_reference(input: ParseStream) -> Result<PatReference> {
        Ok(PatReference {
            attrs: Vec::new(),
            and_token: input.parse()?,
            mutability: input.parse()?,
            pat: Box::new(Pat::parse_single(input)?),
        })
    }

    fn pat_lit_or_range(input: ParseStream) -> Result<Pat> {
        let start = input.call(pat_range_bound)?.unwrap();
        if input.peek(Token![..]) {
            let limits = RangeLimits::parse_obsolete(input)?;
            let end = input.call(pat_range_bound)?;
            if let (RangeLimits::Closed(_), None) = (&limits, &end) {
                return Err(input.error("expected range upper bound"));
            }
            Ok(Pat::Range(ExprRange {
                attrs: Vec::new(),
                start: Some(start.into_expr()),
                limits,
                end: end.map(PatRangeBound::into_expr),
            }))
        } else {
            Ok(start.into_pat())
        }
    }

    // Patterns that can appear on either side of a range pattern.
    enum PatRangeBound {
        Const(ExprConst),
        Lit(ExprLit),
        Path(ExprPath),
    }

    impl PatRangeBound {
        fn into_expr(self) -> Box<Expr> {
            Box::new(match self {
                PatRangeBound::Const(pat) => Expr::Const(pat),
                PatRangeBound::Lit(pat) => Expr::Lit(pat),
                PatRangeBound::Path(pat) => Expr::Path(pat),
            })
        }

        fn into_pat(self) -> Pat {
            match self {
                PatRangeBound::Const(pat) => Pat::Const(pat),
                PatRangeBound::Lit(pat) => Pat::Lit(pat),
                PatRangeBound::Path(pat) => Pat::Path(pat),
            }
        }
    }

    fn pat_range_bound(input: ParseStream) -> Result<Option<PatRangeBound>> {
        if input.is_empty()
            || input.peek(Token![|])
            || input.peek(Token![=])
            || input.peek(Token![:]) && !input.peek(Token![::])
            || input.peek(Token![,])
            || input.peek(Token![;])
            || input.peek(Token![if])
        {
            return Ok(None);
        }

        let lookahead = input.lookahead1();
        let expr = if lookahead.peek(Lit) {
            PatRangeBound::Lit(input.parse()?)
        } else if lookahead.peek(Ident)
            || lookahead.peek(Token![::])
            || lookahead.peek(Token![<])
            || lookahead.peek(Token![self])
            || lookahead.peek(Token![Self])
            || lookahead.peek(Token![super])
            || lookahead.peek(Token![crate])
        {
            PatRangeBound::Path(input.parse()?)
        } else if lookahead.peek(Token![const]) {
            PatRangeBound::Const(input.parse()?)
        } else {
            return Err(lookahead.error());
        };

        Ok(Some(expr))
    }

    fn pat_slice(input: ParseStream) -> Result<PatSlice> {
        let content;
        let bracket_token = bracketed!(content in input);

        let mut elems = Punctuated::new();
        while !content.is_empty() {
            let value = Pat::parse_multi_with_leading_vert(&content)?;
            match value {
                Pat::Range(pat) if pat.start.is_none() || pat.end.is_none() => {
                    let (start, end) = match pat.limits {
                        RangeLimits::HalfOpen(dot_dot) => (dot_dot.spans[0], dot_dot.spans[1]),
                        RangeLimits::Closed(dot_dot_eq) => {
                            (dot_dot_eq.spans[0], dot_dot_eq.spans[2])
                        }
                    };
                    let msg = "range pattern is not allowed unparenthesized inside slice pattern";
                    return Err(error::new2(start, end, msg));
                }
                _ => {}
            }
            elems.push_value(value);
            if content.is_empty() {
                break;
            }
            let punct = content.parse()?;
            elems.push_punct(punct);
        }

        Ok(PatSlice {
            attrs: Vec::new(),
            bracket_token,
            elems,
        })
    }

    fn pat_const(input: ParseStream) -> Result<TokenStream> {
        let begin = input.fork();
        input.parse::<Token![const]>()?;

        let content;
        braced!(content in input);
        content.call(Attribute::parse_inner)?;
        content.call(Block::parse_within)?;

        Ok(verbatim::between(&begin, input))
    }
}

#[cfg(feature = "printing")]
mod printing {
    use crate::attr::FilterAttrs;
    use crate::pat::{
        FieldPat, Pat, PatIdent, PatOr, PatParen, PatReference, PatRest, PatSlice, PatStruct,
        PatTuple, PatTupleStruct, PatType, PatWild,
    };
    use crate::path;
    use crate::path::printing::PathStyle;
    use proc_macro2::TokenStream;
    use quote::{ToTokens, TokenStreamExt};

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PatIdent {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.attrs.outer());
            self.by_ref.to_tokens(tokens);
            self.mutability.to_tokens(tokens);
            self.ident.to_tokens(tokens);
            if let Some((at_token, subpat)) = &self.subpat {
                at_token.to_tokens(tokens);
                subpat.to_tokens(tokens);
            }
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PatOr {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.attrs.outer());
            self.leading_vert.to_tokens(tokens);
            self.cases.to_tokens(tokens);
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PatParen {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.attrs.outer());
            self.paren_token.surround(tokens, |tokens| {
                self.pat.to_tokens(tokens);
            });
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PatReference {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.attrs.outer());
            self.and_token.to_tokens(tokens);
            self.mutability.to_tokens(tokens);
            self.pat.to_tokens(tokens);
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PatRest {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.attrs.outer());
            self.dot2_token.to_tokens(tokens);
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PatSlice {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.attrs.outer());
            self.bracket_token.surround(tokens, |tokens| {
                self.elems.to_tokens(tokens);
            });
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PatStruct {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.attrs.outer());
            path::printing::print_qpath(tokens, &self.qself, &self.path, PathStyle::Expr);
            self.brace_token.surround(tokens, |tokens| {
                self.fields.to_tokens(tokens);
                // NOTE: We need a comma before the dot2 token if it is present.
                if !self.fields.empty_or_trailing() && self.rest.is_some() {
                    <Token![,]>::default().to_tokens(tokens);
                }
                self.rest.to_tokens(tokens);
            });
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PatTuple {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.attrs.outer());
            self.paren_token.surround(tokens, |tokens| {
                self.elems.to_tokens(tokens);
                // If there is only one element, a trailing comma is needed to
                // distinguish PatTuple from PatParen, unless this is `(..)`
                // which is a tuple pattern even without comma.
                if self.elems.len() == 1
                    && !self.elems.trailing_punct()
                    && !matches!(self.elems[0], Pat::Rest { .. })
                {
                    <Token![,]>::default().to_tokens(tokens);
                }
            });
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PatTupleStruct {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.attrs.outer());
            path::printing::print_qpath(tokens, &self.qself, &self.path, PathStyle::Expr);
            self.paren_token.surround(tokens, |tokens| {
                self.elems.to_tokens(tokens);
            });
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PatType {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.attrs.outer());
            self.pat.to_tokens(tokens);
            self.colon_token.to_tokens(tokens);
            self.ty.to_tokens(tokens);
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for PatWild {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.attrs.outer());
            self.underscore_token.to_tokens(tokens);
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for FieldPat {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.attrs.outer());
            if let Some(colon_token) = &self.colon_token {
                self.member.to_tokens(tokens);
                colon_token.to_tokens(tokens);
            }
            self.pat.to_tokens(tokens);
        }
    }
}
