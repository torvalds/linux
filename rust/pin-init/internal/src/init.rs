// SPDX-License-Identifier: Apache-2.0 OR MIT

use proc_macro2::{Span, TokenStream};
use quote::{format_ident, quote, quote_spanned, ToTokens, TokenStreamExt};
use syn::{
    braced, parenthesized,
    parse::{End, Parse},
    parse_quote,
    punctuated::{Pair, Punctuated},
    spanned::Spanned,
    token, Attribute, Block, Expr, ExprCall, ExprPath, Ident, Index, LitInt, Member, Path, Token,
    Type,
};

use crate::{
    diagnostics::{DiagCtxt, ErrorGuaranteed},
    util::*,
};

pub(crate) struct Initializer<Kind = InitExprKind> {
    attrs: Vec<InitializerAttribute>,
    this: Option<This>,
    kind: Kind,
    error: Option<(Token![?], Type)>,
}

pub(crate) struct InitExprStruct {
    path: Path,
    brace_token: token::Brace,
    fields: Punctuated<InitializerField, Token![,]>,
    rest: Option<(Token![..], Expr)>,
}

pub(crate) struct InitExprTuple {
    path: Path,
    paren_token: token::Paren,
    fields: Punctuated<InitTupleField, Token![,]>,
}

pub(crate) enum InitExprKind {
    Struct(InitExprStruct),
    Tuple(InitExprTuple),
}

struct InitTupleField {
    attrs: Vec<Attribute>,
    value: Expr,
}

impl InitExprTuple {
    fn normalize(self) -> InitExprStruct {
        let InitExprTuple {
            path,
            paren_token,
            fields,
        } = self;
        InitExprStruct {
            path,
            brace_token: token::Brace {
                span: paren_token.span,
            },
            fields: fields
                .into_pairs()
                .enumerate()
                .map(|(index, pair)| {
                    let (field, comma) = pair.into_tuple();
                    let span = field.value.span();
                    let field = InitializerField {
                        attrs: field.attrs,
                        kind: InitializerKind::Value {
                            member: Member::Unnamed(Index {
                                index: index.try_into().unwrap(),
                                span,
                            }),
                            value: Some((Token![:](span), field.value)),
                        },
                    };
                    Pair::new(field, comma)
                })
                .collect(),
            rest: None,
        }
    }
}

struct This {
    _and_token: Token![&],
    ident: Ident,
    _in_token: Token![in],
}

struct InitializerField {
    attrs: Vec<Attribute>,
    kind: InitializerKind,
}

enum InitializerKind {
    Value {
        member: Member,
        value: Option<(Token![:], Expr)>,
    },
    Init {
        member: Member,
        left_arrow_token: Token![<-],
        value: Expr,
    },
    Code {
        _underscore_token: Token![_],
        _colon_token: Token![:],
        block: Block,
    },
}

impl InitializerKind {
    fn member(&self) -> Option<&Member> {
        match self {
            Self::Value { member, .. } | Self::Init { member, .. } => Some(member),
            Self::Code { .. } => None,
        }
    }
}

enum InitializerAttribute {
    DefaultError(DefaultErrorAttribute),
}

struct DefaultErrorAttribute {
    ty: Box<Type>,
}

pub(crate) fn expand_with_cfg(
    initializer: Initializer,
    default_error: Option<&'static str>,
    pinned: bool,
    dcx: &mut DiagCtxt,
) -> Result<TokenStream, ErrorGuaranteed> {
    let initializer = match initializer.kind {
        InitExprKind::Tuple(expr) => {
            let mut initializer = Initializer {
                attrs: initializer.attrs,
                this: initializer.this,
                kind: expr,
                error: initializer.error,
            };

            // Removing a tuple field renumbers every field after it, which cannot be expressed with
            // a `cfg` attribute on the initializer of a single field. Therefore, resolve tuple
            // field cfgs before continuing. Struct expression syntax uses explicit numbers, so
            // there is no need to pre-expand them and we only need to emit their cfgs on generated
            // code.
            for (field_idx, field) in initializer.kind.fields.iter_mut().enumerate() {
                let cfg = field.attrs.extract_cfg_attrs();

                if cfg.is_empty() {
                    continue;
                }

                let true_initializer = initializer.to_token_stream();
                initializer.kind.fields = initializer
                    .kind
                    .fields
                    .into_pairs()
                    .enumerate()
                    .filter(|&(index, _)| index != field_idx)
                    .map(|(_, pair)| pair)
                    .collect();

                let false_initializer = &initializer;

                let macro_name = if pinned {
                    quote!(::pin_init::pin_init)
                } else {
                    quote!(::pin_init::init)
                };

                // Resolve one field at a time until we've got no more tuple field cfgs.
                //
                // This is linear time because macro invocations with false cfg will not be
                // expanded.
                return Ok(quote! {
                    {
                        // Use `{}` delimiter here so semicolon is not required, otherwise the
                        // expression becomes unit type.
                        #[cfg(all(#(#cfg,)*))]
                        #macro_name! { #true_initializer }

                        #[cfg(not(all(#(#cfg,)*)))]
                        #macro_name! { #false_initializer }
                    }
                });
            }

            // No cfgs left, we can normalize the initializer to the struct kind.
            Initializer {
                attrs: initializer.attrs,
                this: initializer.this,
                kind: initializer.kind.normalize(),
                error: initializer.error,
            }
        }

        InitExprKind::Struct(expr) => Initializer {
            attrs: initializer.attrs,
            this: initializer.this,
            kind: expr,
            error: initializer.error,
        },
    };

    expand(initializer, default_error, pinned, dcx)
}

fn expand(
    Initializer {
        attrs,
        this,
        kind:
            InitExprStruct {
                path,
                brace_token,
                fields,
                rest,
            },
        error,
    }: Initializer<InitExprStruct>,
    default_error: Option<&'static str>,
    pinned: bool,
    dcx: &mut DiagCtxt,
) -> Result<TokenStream, ErrorGuaranteed> {
    let error = error.map_or_else(
        || {
            if let Some(default_error) = attrs.iter().fold(None, |acc, attr| {
                #[expect(irrefutable_let_patterns)]
                if let InitializerAttribute::DefaultError(DefaultErrorAttribute { ty }) = attr {
                    Some(ty.clone())
                } else {
                    acc
                }
            }) {
                default_error
            } else if let Some(default_error) = default_error {
                syn::parse_str(default_error).unwrap()
            } else {
                dcx.error(
                    brace_token.span.close(),
                    "expected `? <type>` after initializer",
                );
                parse_quote!(::core::convert::Infallible)
            }
        },
        |(_, err)| Box::new(err),
    );
    let (get_pin_data, init_from_closure) = if pinned {
        (
            Some(
                quote_spanned! { path.span().resolved_at(Span::mixed_site()) =>
                    let data = ::pin_init::__internal::HasPinData::__pin_data(data);
                },
            ),
            format_ident!("pin_init_from_closure"),
        )
    } else {
        (None, format_ident!("init_from_closure"))
    };
    let init_kind = get_init_kind(rest, dcx);
    let zeroable_check = match init_kind {
        InitKind::Normal => quote!(),
        InitKind::Zeroing => quote_spanned! { Span::mixed_site() =>
            // The user specified `..Zeroable::zeroed()` at the end of the list of fields.
            // Therefore we check if the struct implements `Zeroable` and then zero the memory.
            // This allows us to also remove the check that all fields are present (since we
            // already set the memory to zero and that is a valid bit pattern).
            fn assert_zeroable<T: ?::core::marker::Sized>(_: *mut T)
            where T: ::pin_init::Zeroable
            {}
            // Ensure that the struct is indeed `Zeroable`.
            assert_zeroable(slot);
            // SAFETY: The type implements `Zeroable` by the check above.
            unsafe { ::core::ptr::write_bytes(slot, 0, 1) };
        },
    };
    let this = match this {
        None => quote!(),
        Some(This { ident, .. }) => quote_spanned! { Span::mixed_site() =>
            // Create the `this` so it can be referenced by the user inside of the
            // expressions creating the individual fields.
            let #ident = unsafe { ::core::ptr::NonNull::new_unchecked(slot) };
        },
    };
    // `mixed_site` ensures that the data is not accessible to the user-controlled code.
    let init_fields = make_field_init(&fields, pinned, false);
    let drop_check = make_field_init(&fields, pinned, true);
    let field_check = make_field_check(&fields, init_kind, &path);

    Ok(quote_spanned! { Span::mixed_site() => {
        // Get the data about fields from the supplied type.
        let data = {
            use ::pin_init::__internal::HasInitData;
            // Can't use `<#path as #has_data_trait>::#get_data`, since the user is able to omit
            // generics (which need to be present with that syntax).
            #path::__init_data()
        };

        #get_pin_data

        // Ensure that `data` really is of type `data` and help with type inference:
        let init = data.__make_closure::<_, #error>(
            move |slot, data_lt| {
                #zeroable_check
                #this
                // Generate init twice, which is mostly identical except for lifetimes.
                if true {
                    // In this path, we use the field lifetime from HRTB to prevent environment
                    // lifetime from entering the fields, and to ensure that the dependency of
                    // fields is consistent with the field drop of the struct.
                    #init_fields
                } else {
                    // In this path, we use local lifetime, to make sure that if initialization
                    // fails, the destructor execution will not cause lifetime issues. This is
                    // separate as implied bounds between field lifetimes can be inconsistent with
                    // that of the drop.
                    #drop_check
                }
                #field_check
                // SAFETY: we are the `init!` macro that is allowed to call this.
                Ok(unsafe { ::pin_init::__internal::InitOk::new() })
            }
        );
        let init = move |slot| -> ::core::result::Result<(), #error> {
            init(slot, data.__with_lt()).map(|__InitOk| ())
        };
        // SAFETY: TODO
        unsafe { ::pin_init::#init_from_closure::<_, #error>(init) }
    }})
}

enum InitKind {
    Normal,
    Zeroing,
}

fn get_init_kind(rest: Option<(Token![..], Expr)>, dcx: &mut DiagCtxt) -> InitKind {
    let Some((dotdot, expr)) = rest else {
        return InitKind::Normal;
    };
    match &expr {
        Expr::Call(ExprCall { func, args, .. }) if args.is_empty() => match &**func {
            Expr::Path(ExprPath {
                attrs,
                qself: None,
                path:
                    Path {
                        leading_colon: None,
                        segments,
                    },
            }) if attrs.is_empty()
                && segments.len() == 2
                && segments[0].ident == "Zeroable"
                && segments[0].arguments.is_none()
                && segments[1].ident == "init_zeroed"
                && segments[1].arguments.is_none() =>
            {
                return InitKind::Zeroing;
            }
            _ => {}
        },
        _ => {}
    }
    dcx.error(
        dotdot.span().join(expr.span()).unwrap_or(expr.span()),
        "expected nothing or `..Zeroable::init_zeroed()`.",
    );
    InitKind::Normal
}

/// Generate the code that initializes the fields of the struct using the initializers in `field`.
fn make_field_init(
    fields: &Punctuated<InitializerField, Token![,]>,
    pinned: bool,
    dropck: bool,
) -> TokenStream {
    let mut forget_guards = vec![];
    let mut res = TokenStream::new();
    for InitializerField { attrs, kind } in fields {
        let cfgs = {
            let mut cfgs = attrs.clone();
            cfgs.retain(|attr| attr.path().is_ident("cfg"));
            cfgs
        };

        let member = match kind {
            InitializerKind::Value { member, .. } => member,
            InitializerKind::Init { member, .. } => member,
            InitializerKind::Code { block, .. } => {
                let stmt = &block.stmts;
                res.extend(quote! {
                    #(#attrs)*
                    {
                        #(#stmt)*
                    }
                });
                continue;
            }
        };
        let ident = member.as_ident();
        let span = Span::mixed_site().located_at(ident.span());

        let slot = if pinned {
            let data = if !dropck {
                quote_spanned!(span => data_lt)
            } else {
                quote_spanned!(span => data.__with_lt())
            };
            quote_spanned! { span =>
                // SAFETY:
                // - `slot` is valid and properly aligned.
                // - `make_field_check` checks that `&raw mut (*slot).#member` is properly aligned.
                // - `make_field_check` prevents `#member` from being used twice, therefore
                //   `(*slot).#member` is exclusively accessed and has not been initialized.
                (unsafe { #data.#ident(slot) })
            }
        } else {
            quote_spanned! { span =>
                // For `init!()` macro, everything is unpinned.
                // SAFETY:
                // - `&raw mut (*slot).#member` is valid.
                // - `make_field_check` checks that `&raw mut (*slot).#member` is properly aligned.
                // - `make_field_check` prevents `#member` from being used twice, therefore
                //   `(*slot).#member` is exclusively accessed and has not been initialized.
                (unsafe {
                    ::pin_init::__internal::Slot::<::pin_init::__internal::Unpinned, _>::new(
                        &raw mut (*slot).#member
                    )
                })
            }
        };

        // `mixed_site` ensures that the guard is not accessible to the user-controlled code.
        let guard = format_ident!("__{ident}_guard", span = span);
        let full_span = kind.span().resolved_at(Span::mixed_site());

        let init = match kind {
            InitializerKind::Value { value, .. } => {
                let (colon_span, value) = value
                    .as_ref()
                    .map(|(colon_token, value)| (colon_token.span(), quote!(#value)))
                    .unwrap_or_else(|| (member.span(), quote!(#member)));

                // Use `:` as the span of the method name, so the type requirement appears to come
                // from `:`.
                let write =
                    format_ident!("write", span = colon_span.resolved_at(Span::mixed_site()));

                quote_spanned! { full_span =>
                    #(#attrs)*
                    let mut #guard = #slot.#write(#value);
                }
            }
            InitializerKind::Init {
                value,
                left_arrow_token,
                ..
            } => {
                // Use `<-` as the span of the method name, so the trait bound appears to come from
                // `<-`.
                let init = format_ident!(
                    "init",
                    span = left_arrow_token.span().resolved_at(Span::mixed_site())
                );

                quote_spanned! { full_span =>
                    #(#attrs)*
                    let mut #guard = #slot.#init(#value)?;
                }
            }
            InitializerKind::Code { .. } => unreachable!(),
        };

        // A tuple field has no name that could be bound here (the `_0` identifiers are considered
        // implementation detail and not user-facing).
        let let_binding_method = if !dropck {
            format_ident!("let_binding", span = span)
        } else {
            format_ident!("let_binding_in_dropck", span = span)
        };
        let binding = match member {
            Member::Named(ident) => quote_spanned! { span =>
                #(#cfgs)*
                // Allow `non_snake_case` since the same warning is going to be reported for the
                // struct field.
                #[allow(unused_variables, non_snake_case)]
                // Include `mut` so that `Pin<&mut T>` bindings can be reborrowed via `.as_mut()`.
                let mut #ident = #guard.#let_binding_method();
            },
            Member::Unnamed(_) => quote!(),
        };

        res.extend(quote! {
            #init

            #binding
        });

        forget_guards.push(quote_spanned! { span =>
            #(#cfgs)*
            ::core::mem::forget(#guard);
        });
    }

    forget_guards.reverse();

    quote! {
        #res
        // If execution reaches this point, all fields have been initialized. Therefore we can now
        // dismiss the guards by forgetting them.
        #(#forget_guards)*
    }
}

/// Generate the check for ensuring that every field has been initialized and aligned.
fn make_field_check(
    fields: &Punctuated<InitializerField, Token![,]>,
    init_kind: InitKind,
    path: &Path,
) -> TokenStream {
    let align_checks: TokenStream = fields
        .iter()
        .filter_map(|f| {
            let member = f.kind.member()?;
            let span = member.span().resolved_at(Span::mixed_site());
            let attrs = &f.attrs;

            Some(quote_spanned! {span =>
                // Create references to ensure that the initialized field is properly aligned.
                // Unaligned fields will cause the compiler to emit E0793. We do not support
                // unaligned fields since `Init::__init` requires an aligned pointer; the call to
                // `ptr::write` for value-initialization case has the same requirement.
                #(#attrs)*
                let _ = &(*slot).#member;
            })
        })
        .collect();

    let fake_field_init: TokenStream = fields
        .iter()
        .filter_map(|f| {
            let member = f.kind.member()?;
            let span = member.span().resolved_at(Span::mixed_site());
            let attrs = &f.attrs;

            Some(quote_spanned! {span =>
                #(#attrs)*
                #member: loop {},
            })
        })
        .collect();
    let zeroing_trailer = match init_kind {
        InitKind::Normal => None,
        InitKind::Zeroing => Some(quote! {
            ..::core::mem::zeroed()
        }),
    };
    let field_dup_checks = quote_spanned! { Span::mixed_site() =>
        // If the zeroing trailer is not present, this checks that all fields have been
        // mentioned exactly once. If the zeroing trailer is present, all missing fields will be
        // zeroed, so this checks that all fields have been mentioned at most once. The use of
        // struct initializer will still generate very natural error messages for any misuse.
        ::core::ptr::write(slot, #path {
            #fake_field_init
            #zeroing_trailer
        })
    };

    quote_spanned! { Span::mixed_site() =>
        #[allow(unreachable_code)]
        // We use unreachable code to perform field checks. They're still checked by the compiler.
        // SAFETY: this code is never executed.
        let _ = || unsafe {
            #align_checks
            #field_dup_checks
        };
    }
}

impl InitExprStruct {
    fn parse_with_path(path: Path, input: syn::parse::ParseStream<'_>) -> syn::Result<Self> {
        let content;
        let brace_token = braced!(content in input);
        let mut fields = Punctuated::new();
        loop {
            let lh = content.lookahead1();
            if lh.peek(End) || lh.peek(Token![..]) {
                break;
            } else if lh.peek(Ident) || lh.peek(LitInt) || lh.peek(Token![_]) || lh.peek(Token![#])
            {
                fields.push_value(content.parse()?);
                let lh = content.lookahead1();
                if lh.peek(End) {
                    break;
                } else if lh.peek(Token![,]) {
                    fields.push_punct(content.parse()?);
                } else {
                    return Err(lh.error());
                }
            } else {
                return Err(lh.error());
            }
        }
        let rest = content
            .peek(Token![..])
            .then(|| Ok::<_, syn::Error>((content.parse()?, content.parse()?)))
            .transpose()?;
        Ok(Self {
            path,
            brace_token,
            fields,
            rest,
        })
    }
}

impl InitExprTuple {
    fn parse_with_path(path: Path, input: syn::parse::ParseStream<'_>) -> syn::Result<Self> {
        let content;
        let paren_token = parenthesized!(content in input);
        let mut fields = Punctuated::new();
        while !content.is_empty() {
            let attrs = content.call(Attribute::parse_outer)?;

            if let Some(left_arrow_token) = content.parse::<Option<Token![<-]>>()? {
                DiagCtxt::current(|dcx| {
                    dcx.error(
                        left_arrow_token,
                        "`<-` is not supported in tuple constructor syntax; name the fields by \
                        index instead, e.g. `Type { 0 <- initializer, 1: value }`",
                    )
                });
            }

            fields.push_value(InitTupleField {
                attrs,
                value: content.parse()?,
            });
            if content.is_empty() {
                break;
            }
            fields.push_punct(content.parse()?);
        }
        Ok(InitExprTuple {
            path,
            paren_token,
            fields,
        })
    }
}

impl Parse for Initializer {
    fn parse(input: syn::parse::ParseStream<'_>) -> syn::Result<Self> {
        let attrs = input.call(Attribute::parse_outer)?;
        let this = input.peek(Token![&]).then(|| input.parse()).transpose()?;
        let path = input.parse()?;
        let kind = if input.peek(token::Brace) {
            InitExprKind::Struct(InitExprStruct::parse_with_path(path, input)?)
        } else if input.peek(token::Paren) {
            InitExprKind::Tuple(InitExprTuple::parse_with_path(path, input)?)
        } else {
            return Err(input.error("expected curly braces or parentheses"));
        };
        let error = input
            .peek(Token![?])
            .then(|| Ok::<_, syn::Error>((input.parse()?, input.parse()?)))
            .transpose()?;
        let attrs = attrs
            .into_iter()
            .map(|a| {
                if a.path().is_ident("default_error") {
                    a.parse_args::<DefaultErrorAttribute>()
                        .map(InitializerAttribute::DefaultError)
                } else {
                    Err(syn::Error::new_spanned(a, "unknown initializer attribute"))
                }
            })
            .collect::<Result<Vec<_>, _>>()?;
        Ok(Self {
            attrs,
            this,
            kind,
            error,
        })
    }
}

impl Parse for DefaultErrorAttribute {
    fn parse(input: syn::parse::ParseStream<'_>) -> syn::Result<Self> {
        Ok(Self { ty: input.parse()? })
    }
}

impl Parse for This {
    fn parse(input: syn::parse::ParseStream<'_>) -> syn::Result<Self> {
        Ok(Self {
            _and_token: input.parse()?,
            ident: input.parse()?,
            _in_token: input.parse()?,
        })
    }
}

impl Parse for InitializerField {
    fn parse(input: syn::parse::ParseStream<'_>) -> syn::Result<Self> {
        let attrs = input.call(Attribute::parse_outer)?;
        Ok(Self {
            attrs,
            kind: input.parse()?,
        })
    }
}

impl Parse for InitializerKind {
    fn parse(input: syn::parse::ParseStream<'_>) -> syn::Result<Self> {
        let lh = input.lookahead1();
        let member = if lh.peek(Token![_]) {
            return Ok(Self::Code {
                _underscore_token: input.parse()?,
                _colon_token: input.parse()?,
                block: input.parse()?,
            });
        } else if lh.peek(Ident) || lh.peek(LitInt) {
            input.parse::<Member>()?
        } else {
            return Err(lh.error());
        };

        let lh = input.lookahead1();
        if lh.peek(Token![<-]) {
            Ok(Self::Init {
                member,
                left_arrow_token: input.parse()?,
                value: input.parse()?,
            })
        } else if lh.peek(Token![:]) {
            Ok(Self::Value {
                member,
                value: Some((input.parse()?, input.parse()?)),
            })
        } else if matches!(member, Member::Named(_)) && (lh.peek(Token![,]) || lh.peek(End)) {
            // Short-hand syntax, available for named fields only.
            Ok(Self::Value {
                member,
                value: None,
            })
        } else {
            Err(lh.error())
        }
    }
}

impl<Kind: ToTokens> ToTokens for Initializer<Kind> {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let Self {
            attrs,
            this,
            kind,
            error,
        } = self;
        tokens.append_all(attrs);
        this.to_tokens(tokens);
        kind.to_tokens(tokens);
        if let Some((question, ty)) = error {
            question.to_tokens(tokens);
            ty.to_tokens(tokens);
        }
    }
}

impl ToTokens for InitExprKind {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        match self {
            Self::Struct(init) => init.to_tokens(tokens),
            Self::Tuple(init) => init.to_tokens(tokens),
        }
    }
}

impl ToTokens for InitExprStruct {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let Self {
            path,
            brace_token,
            fields,
            rest,
        } = self;
        path.to_tokens(tokens);
        brace_token.surround(tokens, |tokens| {
            fields.to_tokens(tokens);
            if let Some((dotdot, expr)) = rest {
                dotdot.to_tokens(tokens);
                expr.to_tokens(tokens);
            }
        });
    }
}

impl ToTokens for InitExprTuple {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let Self {
            path,
            paren_token,
            fields,
        } = self;
        path.to_tokens(tokens);
        paren_token.surround(tokens, |tokens| fields.to_tokens(tokens));
    }
}

impl ToTokens for InitTupleField {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let Self { attrs, value } = self;
        tokens.append_all(attrs);
        value.to_tokens(tokens);
    }
}

impl ToTokens for InitializerAttribute {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        match self {
            Self::DefaultError(DefaultErrorAttribute { ty }) => {
                quote!(#[default_error(#ty)]).to_tokens(tokens);
            }
        }
    }
}

impl ToTokens for This {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let Self {
            _and_token,
            ident,
            _in_token,
        } = self;
        _and_token.to_tokens(tokens);
        ident.to_tokens(tokens);
        _in_token.to_tokens(tokens);
    }
}

impl ToTokens for InitializerField {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let Self { attrs, kind } = self;
        tokens.append_all(attrs);
        kind.to_tokens(tokens);
    }
}

impl ToTokens for InitializerKind {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        match self {
            Self::Value { member, value } => {
                member.to_tokens(tokens);
                if let Some((colon, expr)) = value {
                    colon.to_tokens(tokens);
                    expr.to_tokens(tokens);
                }
            }
            Self::Init {
                member,
                left_arrow_token,
                value,
            } => {
                member.to_tokens(tokens);
                left_arrow_token.to_tokens(tokens);
                value.to_tokens(tokens);
            }
            Self::Code {
                _underscore_token,
                _colon_token,
                block,
            } => {
                _underscore_token.to_tokens(tokens);
                _colon_token.to_tokens(tokens);
                block.to_tokens(tokens);
            }
        }
    }
}
