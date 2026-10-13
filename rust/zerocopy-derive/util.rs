// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT
//
// Copyright 2019 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

use std::num::NonZeroU32;

use proc_macro2::{Span, TokenStream};
use quote::{quote, quote_spanned, ToTokens};
use syn::{
    parse::ParseBuffer, parse_quote, spanned::Spanned as _, token::PathSep, Data, DataEnum,
    DataStruct, DataUnion, DeriveInput, Error, Expr, ExprLit, Field, GenericParam, Ident, Index,
    Lit, LitStr, Meta, Path, Type, Variant, Visibility, WherePredicate,
};

use crate::repr::{CompoundRepr, EnumRepr, PrimitiveRepr, Repr, Spanned};

pub(crate) struct Ctx {
    pub(crate) ast: DeriveInput,
    pub(crate) zerocopy_crate: Path,

    // The value of the last `#[zerocopy(on_error = ...)]` attribute, or `false`
    // if none is provided.
    pub(crate) skip_on_error: bool,

    // The span of the last `#[zerocopy(on_error = ...)]` attribute, if any.
    pub(crate) on_error_span: Option<proc_macro2::Span>,

    /// The first field invariant on the source type, if any.
    pub(crate) invariant_span: Option<Span>,
}

#[derive(Eq, PartialEq)]
enum CratePath {
    External,
    CrateRelative,
    ModuleRelative,
}

fn validate_crate_path(path: &Path) -> Result<CratePath, ()> {
    if path.segments.is_empty() {
        return Err(());
    }

    enum ModuleRelative {
        Yes,
        No,
    }
    let first = path.segments[0].ident.to_string();
    let (mut prev_segment, path_type) = match first.as_str() {
        "Self" => return Err(()),
        "crate" => {
            if path.leading_colon.is_some() {
                return Err(());
            }
            (ModuleRelative::No, CratePath::CrateRelative)
        }
        "self" | "super" => {
            if path.leading_colon.is_some() {
                return Err(());
            }
            (ModuleRelative::Yes, CratePath::ModuleRelative)
        }
        _ => (ModuleRelative::No, CratePath::External),
    };

    for seg in path.segments.iter().skip(1) {
        let ident = seg.ident.to_string();
        match ident.as_str() {
            "Self" | "crate" | "self" => return Err(()),
            "super" => match prev_segment {
                ModuleRelative::Yes => {}
                ModuleRelative::No => return Err(()),
            },
            _ => {
                prev_segment = ModuleRelative::No;
            }
        }
    }
    Ok(path_type)
}

impl Ctx {
    /// Attempt to extract a crate path from the provided attributes. Defaults to
    /// `::zerocopy` if not found.
    pub(crate) fn try_from_derive_input(ast: DeriveInput) -> Result<Self, Error> {
        let mut path = parse_quote!(::zerocopy);
        let mut skip_on_error = false;
        let mut on_error_span = None;

        for attr in &ast.attrs {
            if let Meta::List(ref meta_list) = attr.meta {
                if path_is_ident(&meta_list.path, "zerocopy") {
                    attr.parse_nested_meta(|meta| {
                        if path_is_ident(&meta.path, "crate") {
                            let expr = meta.value().and_then(ParseBuffer::parse);
                            if let Ok(Expr::Lit(ExprLit { lit: Lit::Str(lit), .. })) = expr {
                                if let Ok(mut path_lit) = lit.parse_with(Path::parse_mod_style) {
                                    if let Ok(crate_path) = validate_crate_path(&path_lit) {
                                        // If not expressly relative, absolutize.
                                        if path_lit.leading_colon.is_none() && crate_path == CratePath::External {
                                            path_lit.leading_colon = Some(PathSep::default());
                                        }
                                        path = path_lit;
                                        return Ok(());
                                    }

                                    return Err(Error::new(
                                        lit.span(),
                                        "`crate` attribute requires a valid module path",
                                    ));
                                }

                                return Err(Error::new(
                                    lit.span(),
                                    "`crate` attribute requires a path as the value",
                                ));
                            }

                            return Err(Error::new(
                                meta.path.span(),
                                "`crate` attribute requires a path as the value",
                            ));
                        }

                        if path_is_ident(&meta.path, "on_error") {
                            on_error_span = Some(meta.path.span());
                            let value = meta.value()?;
                            let s: LitStr = value.parse()?;
                            match s.value().as_str() {
                                "skip" => skip_on_error = true,
                                "fail" => skip_on_error = false,
                                _ => return Err(Error::new(
                                    s.span(),
                                    "unrecognized value for `on_error` attribute from `zerocopy`; expected `skip` or `fail`",
                                )),
                            }
                            return Ok(());
                        }

                        Err(Error::new(
                            Span::call_site(),
                            format!(
                                "unknown attribute encountered: {}",
                                meta.path.into_token_stream()
                            ),
                        ))
                    })?;
                }
            }
        }

        let invariant_span = crate::invariant::validate(&ast)?;
        Ok(Self { ast, zerocopy_crate: path, skip_on_error, on_error_span, invariant_span })
    }

    pub(crate) fn with_input(&self, input: &DeriveInput) -> Self {
        Self {
            ast: input.clone(),
            zerocopy_crate: self.zerocopy_crate.clone(),
            skip_on_error: self.skip_on_error,
            on_error_span: self.on_error_span,
            invariant_span: self.invariant_span,
        }
    }

    pub(crate) fn skip_on_error(mut self) -> Self {
        self.skip_on_error = true;
        self
    }

    pub(crate) fn core_path(&self) -> TokenStream {
        let zerocopy_crate = &self.zerocopy_crate;
        quote!(#zerocopy_crate::util::macro_util::core_reexport)
    }

    /// Choose an implementation name absent from the caller's tokens. This is
    /// best-effort; names introduced by nested macros are not considered.
    /// Unlike local variables, type parameters are not hidden by mixed-site
    /// hygiene.
    pub(crate) fn fresh_ident(&self, name: &str) -> Ident {
        let idents = crate::invariant::idents(self.ast.to_token_stream());
        let mut name = name.to_owned();
        while idents.iter().any(|ident| ident.to_string().trim_start_matches("r#") == name) {
            name.push('_');
        }
        Ident::new(&name, Span::mixed_site())
    }

    /// Caller-authored invariant expressions must inherit the caller's lint
    /// policy, rather than the blanket allowances for generated glue.
    pub(crate) fn const_block(
        &self,
        items: impl IntoIterator<Item = Option<TokenStream>>,
    ) -> TokenStream {
        if self.invariant_span.is_none() {
            const_block(items)
        } else {
            let items = items.into_iter().flatten();
            quote! { const _: () = { #(#items)* }; }
        }
    }

    pub(crate) fn cfg_compile_error(&self) -> TokenStream {
        // By checking both during the compilation of the proc macro *and* in
        // the generated code, we ensure that each cfg need only be passed when
        // compiling this crate *or* when compiling the user's crate. The former
        // is preferable, but in some situations (such as when cross-compiling
        // using `cargo build --target`), it doesn't get propagated to this
        // crate's build by default.
        let on_error = if cfg!(zerocopy_unstable_linux) {
            quote!()
        } else if let Some(span) = self.on_error_span {
            let core = self.core_path();
            let error_message =
                "`on_error` is experimental; pass '--cfg zerocopy_unstable_linux' to enable";
            quote::quote_spanned! {span=>
                #[allow(unused_attributes, unexpected_cfgs)]
                const _: () = {
                    #[cfg(not(zerocopy_unstable_linux))]
                    #core::compile_error!(#error_message);
                };
            }
        } else {
            quote!()
        };
        let invariant = if cfg!(zerocopy_unstable_ptr) {
            quote!()
        } else if let Some(span) = self.invariant_span {
            let core = self.core_path();
            let error_message =
                "`invariant` is experimental; pass '--cfg zerocopy_unstable_ptr' to enable";
            quote::quote_spanned! {span=>
                #[allow(unused_attributes, unexpected_cfgs)]
                const _: () = {
                    #[cfg(not(zerocopy_unstable_ptr))]
                    #core::compile_error!(#error_message);
                };
            }
        } else {
            quote!()
        };
        quote!(#on_error #invariant)
    }

    pub(crate) fn error_or_skip<E>(&self, error: E) -> Result<TokenStream, E> {
        if self.skip_on_error {
            Ok(self.cfg_compile_error())
        } else {
            Err(error)
        }
    }
}

pub(crate) trait DataExt {
    /// Extracts the names and types of all fields. For enums, extracts the
    /// names and types of fields from each variant. For tuple structs, the
    /// names are the indices used to index into the struct (ie, `0`, `1`, etc).
    ///
    /// FIXME: Extracting field names for enums doesn't really make sense. Types
    /// makes sense because we don't care about where they live - we just care
    /// about transitive ownership. But for field names, we'd only use them when
    /// generating is_safe, which cares about where they live.
    fn fields(&self) -> Vec<(&Visibility, TokenStream, &Type)>;

    fn variants(&self) -> Vec<(Option<&Variant>, Vec<(&Visibility, TokenStream, &Type)>)>;

    fn tag(&self) -> Option<Ident>;
}

impl DataExt for Data {
    fn fields(&self) -> Vec<(&Visibility, TokenStream, &Type)> {
        match self {
            Data::Struct(strc) => strc.fields(),
            Data::Enum(enm) => enm.fields(),
            Data::Union(un) => un.fields(),
        }
    }

    fn variants(&self) -> Vec<(Option<&Variant>, Vec<(&Visibility, TokenStream, &Type)>)> {
        match self {
            Data::Struct(strc) => strc.variants(),
            Data::Enum(enm) => enm.variants(),
            Data::Union(un) => un.variants(),
        }
    }

    fn tag(&self) -> Option<Ident> {
        match self {
            Data::Struct(strc) => strc.tag(),
            Data::Enum(enm) => enm.tag(),
            Data::Union(un) => un.tag(),
        }
    }
}

impl DataExt for DataStruct {
    fn fields(&self) -> Vec<(&Visibility, TokenStream, &Type)> {
        map_fields(&self.fields)
    }

    fn variants(&self) -> Vec<(Option<&Variant>, Vec<(&Visibility, TokenStream, &Type)>)> {
        vec![(None, self.fields())]
    }

    fn tag(&self) -> Option<Ident> {
        None
    }
}

impl DataExt for DataEnum {
    fn fields(&self) -> Vec<(&Visibility, TokenStream, &Type)> {
        map_fields(self.variants.iter().flat_map(|var| &var.fields))
    }

    fn variants(&self) -> Vec<(Option<&Variant>, Vec<(&Visibility, TokenStream, &Type)>)> {
        self.variants.iter().map(|var| (Some(var), map_fields(&var.fields))).collect()
    }

    fn tag(&self) -> Option<Ident> {
        Some(Ident::new("___ZerocopyTag", Span::call_site()))
    }
}

impl DataExt for DataUnion {
    fn fields(&self) -> Vec<(&Visibility, TokenStream, &Type)> {
        map_fields(&self.fields.named)
    }

    fn variants(&self) -> Vec<(Option<&Variant>, Vec<(&Visibility, TokenStream, &Type)>)> {
        vec![(None, self.fields())]
    }

    fn tag(&self) -> Option<Ident> {
        None
    }
}

fn map_fields<'a>(
    fields: impl 'a + IntoIterator<Item = &'a Field>,
) -> Vec<(&'a Visibility, TokenStream, &'a Type)> {
    fields
        .into_iter()
        .enumerate()
        .map(|(idx, f)| {
            (
                &f.vis,
                f.ident
                    .as_ref()
                    .map(ToTokens::to_token_stream)
                    .unwrap_or_else(|| Index::from(idx).to_token_stream()),
                &f.ty,
            )
        })
        .collect()
}

pub(crate) fn to_ident_str(t: &impl ToString) -> String {
    let s = t.to_string();
    if let Some(stripped) = s.strip_prefix("r#") {
        stripped.to_string()
    } else {
        s
    }
}

/// Does `path` consist solely of the identifier `expected`?
///
/// Unlike [`Path::is_ident`], this treats a raw identifier and its ordinary
/// spelling as the same identifier. Rust applies that same normalization when
/// interpreting attribute names and arguments.
pub(crate) fn path_is_ident(path: &Path, expected: &str) -> bool {
    match path.get_ident() {
        Some(ident) => to_ident_str(ident) == expected,
        None => false,
    }
}

/// This enum describes what kind of padding check needs to be generated for the
/// associated impl.
pub(crate) enum PaddingCheck {
    /// Check that the sum of the fields' sizes exactly equals the struct's
    /// size.
    Struct,
    /// Check that a `repr(C)` struct has no padding.
    ReprCStruct,
    /// Check that the size of each field exactly equals the union's size.
    Union,
    /// Check that every variant of the enum contains no padding.
    ///
    /// Because doing so requires a tag enum, this padding check requires an
    /// additional `TokenStream` which defines the tag enum as `___ZerocopyTag`.
    Enum { tag_type_definition: TokenStream },
}

impl PaddingCheck {
    /// Returns the idents of the trait to use and the macro to call in order to
    /// validate that a type passes the relevant padding check.
    pub(crate) fn validator_trait_and_macro_idents(&self) -> (Ident, Ident) {
        let (trt, mcro) = match self {
            PaddingCheck::Struct => ("PaddingFree", "struct_padding"),
            PaddingCheck::ReprCStruct => ("DynamicPaddingFree", "repr_c_struct_has_padding"),
            PaddingCheck::Union => ("PaddingFree", "union_padding"),
            PaddingCheck::Enum { .. } => ("PaddingFree", "enum_padding"),
        };

        let trt = Ident::new(trt, Span::call_site());
        let mcro = Ident::new(mcro, Span::call_site());
        (trt, mcro)
    }

    /// Sometimes performing the padding check requires some additional
    /// "context" code. For enums, this is the definition of the tag enum.
    pub(crate) fn validator_macro_context(&self) -> Option<&TokenStream> {
        match self {
            PaddingCheck::Struct | PaddingCheck::ReprCStruct | PaddingCheck::Union => None,
            PaddingCheck::Enum { tag_type_definition } => Some(tag_type_definition),
        }
    }
}

#[derive(Copy, Clone)]
pub(crate) enum Client {
    ProjectDerive,
    TryFromBytesDerive,
}

impl ToTokens for Client {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        let s = match self {
            Client::ProjectDerive => "ProjectDerive",
            Client::TryFromBytesDerive => "TryFromBytesDerive",
        };
        let ident = Ident::new(s, Span::call_site());
        tokens.extend(quote!(project_clients::#ident));
    }
}

impl Client {
    pub(crate) fn crate_path(&self, ctx: &Ctx) -> Path {
        let zerocopy_crate = &ctx.zerocopy_crate;
        parse_quote!(#zerocopy_crate::#self)
    }
}

/// Bundles the trait parameters for `HasField` and `ProjectField`.
#[derive(Clone)]
pub(crate) struct FieldProjection {
    pub(crate) variant_id: Box<Expr>,
    pub(crate) field: Box<Type>,
    pub(crate) field_id: Box<Expr>,
}

#[derive(Clone)]
pub(crate) enum Trait {
    KnownLayout,
    HasTag { client: Client },
    HasField { client: Client, projection: FieldProjection },
    ProjectField { client: Client, projection: FieldProjection, invariants: Box<Type> },
    Immutable,
    Project,
    TryFromBytes,
    FromZeros,
    FromBytes,
    IntoBytes,
    Unaligned,
    Sized,
    ByteHash,
    ByteEq,
    SplitAt,
}

impl ToTokens for Trait {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        // According to [1], the format of the derived `Debug`` output is not
        // stable and therefore not guaranteed to represent the variant names.
        // Indeed with the (unstable) `fmt-debug` compiler flag [2], it can
        // return only a minimalized output or empty string. To make sure this
        // code will work in the future and independent of the compiler flag, we
        // translate the variants to their names manually here.
        //
        // [1] https://doc.rust-lang.org/1.81.0/std/fmt/trait.Debug.html#stability
        // [2] https://doc.rust-lang.org/beta/unstable-book/compiler-flags/fmt-debug.html
        let s = match self {
            Trait::HasField { .. } => "HasField",
            Trait::ProjectField { .. } => "ProjectField",
            Trait::KnownLayout => "KnownLayout",
            Trait::HasTag { .. } => "HasTag",
            Trait::Immutable => "Immutable",
            Trait::Project => "Project",
            Trait::TryFromBytes => "TryFromBytes",
            Trait::FromZeros => "FromZeros",
            Trait::FromBytes => "FromBytes",
            Trait::IntoBytes => "IntoBytes",
            Trait::Unaligned => "Unaligned",
            Trait::Sized => "Sized",
            Trait::ByteHash => "ByteHash",
            Trait::ByteEq => "ByteEq",
            Trait::SplitAt => "SplitAt",
        };
        let ident = Ident::new(s, Span::call_site());
        let arguments: Option<syn::AngleBracketedGenericArguments> = match self {
            Trait::HasTag { client } => Some(parse_quote!(<#client>)),
            Trait::HasField { client, projection } => {
                let FieldProjection { variant_id, field, field_id } = projection;
                Some(parse_quote!(<#client, #field, #variant_id, #field_id>))
            }
            Trait::ProjectField { client, projection, invariants } => {
                let FieldProjection { variant_id, field, field_id } = projection;
                Some(parse_quote!(<#client, #field, #invariants, #variant_id, #field_id>))
            }
            Trait::KnownLayout
            | Trait::Immutable
            | Trait::Project
            | Trait::TryFromBytes
            | Trait::FromZeros
            | Trait::FromBytes
            | Trait::IntoBytes
            | Trait::Unaligned
            | Trait::Sized
            | Trait::ByteHash
            | Trait::ByteEq
            | Trait::SplitAt => None,
        };
        tokens.extend(quote!(#ident #arguments));
    }
}

impl Trait {
    pub(crate) fn crate_path(&self, ctx: &Ctx) -> Path {
        let zerocopy_crate = &ctx.zerocopy_crate;
        let core = ctx.core_path();
        match self {
            Self::Sized => parse_quote!(#core::marker::#self),
            Self::HasTag { client } => {
                let client = client.crate_path(ctx);
                parse_quote!(#zerocopy_crate::HasTag<#client>)
            }
            Self::HasField { client, projection } => {
                let FieldProjection { variant_id, field, field_id } = projection;
                let client = client.crate_path(ctx);
                parse_quote!(#zerocopy_crate::HasField<#client, #field, #variant_id, #field_id>)
            }
            Self::ProjectField { client, projection, invariants } => {
                let FieldProjection { variant_id, field, field_id } = projection;
                let client = client.crate_path(ctx);
                parse_quote!(
                    #zerocopy_crate::ProjectField<
                        #client,
                        #field,
                        #invariants,
                        #variant_id,
                        #field_id
                    >
                )
            }
            _ => parse_quote!(#zerocopy_crate::#self),
        }
    }
}

pub(crate) enum TraitBound {
    Slf,
    Other(Trait),
}

pub(crate) enum FieldBounds<'a> {
    None,
    All(&'a [TraitBound]),
    Trailing(&'a [TraitBound]),
    Explicit(Vec<WherePredicate>),
}

impl<'a> FieldBounds<'a> {
    pub(crate) const ALL_SELF: FieldBounds<'a> = FieldBounds::All(&[TraitBound::Slf]);
    pub(crate) const TRAILING_SELF: FieldBounds<'a> = FieldBounds::Trailing(&[TraitBound::Slf]);
}

pub(crate) enum SelfBounds<'a> {
    None,
    All(&'a [Trait]),
}

// FIXME(https://github.com/rust-lang/rust-clippy/issues/12908): This is a false
// positive. Explicit lifetimes are actually necessary here.
#[allow(clippy::needless_lifetimes)]
impl<'a> SelfBounds<'a> {
    pub(crate) const SIZED: Self = Self::All(&[Trait::Sized]);
}

/// Normalizes a slice of bounds by replacing [`TraitBound::Slf`] with `slf`.
pub(crate) fn normalize_bounds<'a>(
    slf: &'a Trait,
    bounds: &'a [TraitBound],
) -> impl 'a + Iterator<Item = Trait> {
    bounds.iter().map(move |bound| match bound {
        TraitBound::Slf => slf.clone(),
        TraitBound::Other(trt) => trt.clone(),
    })
}

pub(crate) struct ImplBlockBuilder<'a> {
    ctx: &'a Ctx,
    data: &'a dyn DataExt,
    trt: Trait,
    field_type_trait_bounds: FieldBounds<'a>,
    self_type_trait_bounds: SelfBounds<'a>,
    padding_check: Option<PaddingCheck>,
    param_extras: Vec<GenericParam>,
    inner_extras: Option<TokenStream>,
    outer_extras: Option<TokenStream>,
}

impl<'a> ImplBlockBuilder<'a> {
    pub(crate) fn new(
        ctx: &'a Ctx,
        data: &'a dyn DataExt,
        trt: Trait,
        field_type_trait_bounds: FieldBounds<'a>,
    ) -> Self {
        Self {
            ctx,
            data,
            trt,
            field_type_trait_bounds,
            self_type_trait_bounds: SelfBounds::None,
            padding_check: None,
            param_extras: Vec::new(),
            inner_extras: None,
            outer_extras: None,
        }
    }

    pub(crate) fn self_type_trait_bounds(mut self, self_type_trait_bounds: SelfBounds<'a>) -> Self {
        self.self_type_trait_bounds = self_type_trait_bounds;
        self
    }

    pub(crate) fn padding_check<P: Into<Option<PaddingCheck>>>(mut self, padding_check: P) -> Self {
        self.padding_check = padding_check.into();
        self
    }

    pub(crate) fn param_extras(mut self, param_extras: Vec<GenericParam>) -> Self {
        self.param_extras.extend(param_extras);
        self
    }

    pub(crate) fn inner_extras(mut self, inner_extras: TokenStream) -> Self {
        self.inner_extras = Some(inner_extras);
        self
    }

    pub(crate) fn outer_extras<T: Into<Option<TokenStream>>>(mut self, outer_extras: T) -> Self {
        self.outer_extras = outer_extras.into();
        self
    }

    pub(crate) fn build(self) -> TokenStream {
        // In this documentation, we will refer to this hypothetical struct:
        //
        //   #[derive(FromBytes)]
        //   struct Foo<T, I: Iterator>
        //   where
        //       T: Copy,
        //       I: Clone,
        //       I::Item: Clone,
        //   {
        //       a: u8,
        //       b: T,
        //       c: I::Item,
        //   }
        //
        // We extract the field types, which in this case are `u8`, `T`, and
        // `I::Item`. We re-use the existing parameters and where clauses. If
        // `require_trait_bound == true` (as it is for `FromBytes), we add where
        // bounds for each field's type:
        //
        //   impl<T, I: Iterator> FromBytes for Foo<T, I>
        //   where
        //       T: Copy,
        //       I: Clone,
        //       I::Item: Clone,
        //       T: FromBytes,
        //       I::Item: FromBytes,
        //   {
        //   }
        //
        // NOTE: It is standard practice to only emit bounds for the type
        // parameters themselves, not for field types based on those parameters
        // (e.g., `T` vs `T::Foo`). For a discussion of why this is standard
        // practice, see https://github.com/rust-lang/rust/issues/26925.
        //
        // The reason we diverge from this standard is that doing it that way
        // for us would be unsound. E.g., consider a type, `T` where `T:
        // FromBytes` but `T::Foo: !FromBytes`. It would not be sound for us to
        // accept a type with a `T::Foo` field as `FromBytes` simply because `T:
        // FromBytes`.
        //
        // While there's no getting around this requirement for us, it does have
        // the pretty serious downside that, when lifetimes are involved, the
        // trait solver ties itself in knots:
        //
        //     #[derive(Unaligned)]
        //     #[repr(C)]
        //     struct Dup<'a, 'b> {
        //         a: PhantomData<&'a u8>,
        //         b: PhantomData<&'b u8>,
        //     }
        //
        //     error[E0283]: type annotations required: cannot resolve `core::marker::PhantomData<&'a u8>: zerocopy::Unaligned`
        //      --> src/main.rs:6:10
        //       |
        //     6 | #[derive(Unaligned)]
        //       |          ^^^^^^^^^
        //       |
        //       = note: required by `zerocopy::Unaligned`

        let type_ident = &self.ctx.ast.ident;
        let trait_path = self.trt.crate_path(self.ctx);
        let fields = self.data.fields();
        let variants = self.data.variants();
        let tag = self.data.tag();
        let zerocopy_crate = &self.ctx.zerocopy_crate;

        fn bound_tt(ty: &Type, traits: impl Iterator<Item = Trait>, ctx: &Ctx) -> WherePredicate {
            let traits = traits.map(|t| t.crate_path(ctx));
            parse_quote!(#ty: #(#traits)+*)
        }
        let field_type_bounds: Vec<_> = match (self.field_type_trait_bounds, &fields[..]) {
            (FieldBounds::All(traits), _) => fields
                .iter()
                .map(|(_vis, _name, ty)| {
                    bound_tt(ty, normalize_bounds(&self.trt, traits), self.ctx)
                })
                .collect(),
            (FieldBounds::None, _) | (FieldBounds::Trailing(..), []) => vec![],
            (FieldBounds::Trailing(traits), [.., last]) => {
                vec![bound_tt(last.2, normalize_bounds(&self.trt, traits), self.ctx)]
            }
            (FieldBounds::Explicit(bounds), _) => bounds,
        };

        let padding_check_bound = self
            .padding_check
            .map(|check| {
                // Parse the repr for `align` and `packed` modifiers. Note that
                // `Repr::<PrimitiveRepr, NonZeroU32>` is more permissive than
                // what Rust supports for structs, enums, or unions, and thus
                // reliably extracts these modifiers for any kind of type.
                let repr =
                    Repr::<PrimitiveRepr, NonZeroU32>::from_attrs(&self.ctx.ast.attrs).unwrap();
                let core = self.ctx.core_path();
                let option = quote! { #core::option::Option };
                let nonzero = quote! { #core::num::NonZeroUsize };
                let none = quote! { #option::None::<#nonzero> };
                let repr_align =
                    repr.get_align().map(|spanned| {
                        let n = spanned.t.get();
                        quote_spanned! { spanned.span => (#nonzero::new(#n as usize)) }
                    }).unwrap_or(quote! { (#none) });
                let repr_packed =
                    repr.get_packed().map(|packed| {
                        let n = packed.get();
                        quote! { (#nonzero::new(#n as usize)) }
                    }).unwrap_or(quote! { (#none) });
                let variant_types = variants.iter().map(|(_, fields)| {
                    let types = fields.iter().map(|(_vis, _name, ty)| ty);
                    quote!([#((#types)),*])
                });
                let validator_context = check.validator_macro_context();
                let (trt, validator_macro) = check.validator_trait_and_macro_idents();
                let t = tag.iter();
                parse_quote! {
                    (): #zerocopy_crate::util::macro_util::#trt<
                        Self,
                        {
                            #validator_context
                            #zerocopy_crate::#validator_macro!(Self, #repr_align, #repr_packed, #(#t,)* #(#variant_types),*)
                        }
                    >
                }
            });

        let self_bounds: Option<WherePredicate> = match self.self_type_trait_bounds {
            SelfBounds::None => None,
            SelfBounds::All(traits) => {
                Some(bound_tt(&parse_quote!(Self), traits.iter().cloned(), self.ctx))
            }
        };

        let zerocopy_bounds =
            field_type_bounds
                .into_iter()
                .chain(padding_check_bound)
                .chain(self_bounds)
                .map(|bound| {
                    if self.ctx.skip_on_error {
                        parse_quote!(for<'zc> #bound)
                    } else {
                        bound.clone()
                    }
                })
                .collect::<Vec<_>>();

        let bounds = self
            .ctx
            .ast
            .generics
            .where_clause
            .as_ref()
            .map(|where_clause| where_clause.predicates.iter())
            .into_iter()
            .flatten()
            .chain(zerocopy_bounds.iter());

        // The parameters with trait bounds, but without type defaults.
        let mut params: Vec<_> = self
            .ctx
            .ast
            .generics
            .params
            .clone()
            .into_iter()
            .map(|mut param| {
                match &mut param {
                    GenericParam::Type(ty) => ty.default = None,
                    GenericParam::Const(cnst) => cnst.default = None,
                    GenericParam::Lifetime(_) => {}
                }
                parse_quote!(#param)
            })
            .chain(self.param_extras)
            .collect();

        // For MSRV purposes, ensure that lifetimes precede types precede const
        // generics.
        params.sort_by_cached_key(|param| match param {
            GenericParam::Lifetime(_) => 0,
            GenericParam::Type(_) => 1,
            GenericParam::Const(_) => 2,
        });

        // The identifiers of the parameters without trait bounds or type
        // defaults.
        let param_idents = self.ctx.ast.generics.params.iter().map(|param| match param {
            GenericParam::Type(ty) => {
                let ident = &ty.ident;
                quote!(#ident)
            }
            GenericParam::Lifetime(l) => {
                let ident = &l.lifetime;
                quote!(#ident)
            }
            GenericParam::Const(cnst) => {
                let ident = &cnst.ident;
                quote!({#ident})
            }
        });

        let inner_extras = self.inner_extras;
        let allow_trivial_bounds =
            if self.ctx.skip_on_error { quote!(#[allow(trivial_bounds)]) } else { quote!() };
        let impl_tokens = quote! {
            #allow_trivial_bounds
            unsafe impl < #(#params),* > #trait_path for #type_ident < #(#param_idents),* >
            where
                #(#bounds,)*
            {
                fn only_derive_is_allowed_to_implement_this_trait() {}

                #inner_extras
            }
        };

        let outer_extras = self.outer_extras.filter(|e| !e.is_empty());
        let cfg_compile_error = self.ctx.cfg_compile_error();
        let items = [Some(cfg_compile_error), Some(impl_tokens), outer_extras];
        if matches!(self.trt, Trait::TryFromBytes) {
            self.ctx.const_block(items)
        } else {
            const_block(items)
        }
    }
}

// A polyfill for `Option::then_some`, which was added after our MSRV.
//
// The `#[allow(unused)]` is necessary because, on sufficiently recent toolchain
// versions, `b.then_some(...)` resolves to the inherent method rather than to
// this trait, and so this trait is considered unused.
//
// FIXME(#67): Remove this once our MSRV is >= 1.62.
#[allow(unused)]
trait BoolExt {
    fn then_some<T>(self, t: T) -> Option<T>;
}

impl BoolExt for bool {
    fn then_some<T>(self, t: T) -> Option<T> {
        if self {
            Some(t)
        } else {
            None
        }
    }
}

pub(crate) fn allow_generated_code() -> TokenStream {
    quote! {
        #[allow(
            // FIXME(#553): Add a test that generates a warning when
            // `#[allow(deprecated)]` isn't present.
            deprecated,
            // Required on some rustc versions due to a lint that is only
            // triggered when `derive(KnownLayout)` is applied to `repr(C)`
            // structs that are generated by macros. See #2177 for details.
            private_bounds,
            non_local_definitions,
            non_camel_case_types,
            non_upper_case_globals,
            non_snake_case,
            non_ascii_idents,
            clippy::missing_inline_in_public_items,
        )]
    }
}

pub(crate) fn const_block(items: impl IntoIterator<Item = Option<TokenStream>>) -> TokenStream {
    let items = items.into_iter().flatten();
    let allow = allow_generated_code();
    quote! {
        #allow
        #[deny(ambiguous_associated_items)]
        // While there are not currently any warnings that this suppresses
        // (that we're aware of), it's good future-proofing hygiene.
        #[automatically_derived]
        const _: () = {
            #(#items)*
        };
    }
}
pub(crate) fn generate_tag_enum(ctx: &Ctx, repr: &EnumRepr, data: &DataEnum) -> TokenStream {
    let zerocopy_crate = &ctx.zerocopy_crate;
    let variants = data.variants.iter().map(|v| {
        let ident = &v.ident;
        if let Some((eq, discriminant)) = &v.discriminant {
            quote! { #ident #eq #discriminant }
        } else {
            quote! { #ident }
        }
    });

    // Don't include any `repr(align)` when generating the tag enum, as that
    // could add padding after the tag but before any variants, which is not the
    // correct behavior.
    let repr = match repr {
        EnumRepr::Transparent(span) => quote::quote_spanned! { *span => #[repr(transparent)] },
        EnumRepr::Compound(c, _) => quote! { #c },
    };

    quote! {
        #repr
        #[allow(dead_code, clippy::derive_partial_eq_without_eq)]
        #[derive(Copy, Clone, PartialEq)]
        pub enum ___ZerocopyTag {
            #(#variants,)*
        }

        // SAFETY: `___ZerocopyTag` has no fields, and so it does not permit
        // interior mutation.
        unsafe impl #zerocopy_crate::Immutable for ___ZerocopyTag {
            fn only_derive_is_allowed_to_implement_this_trait() {}
        }
    }
}
pub(crate) fn enum_size_from_repr(repr: &EnumRepr) -> Result<usize, Error> {
    use CompoundRepr::*;
    use PrimitiveRepr::*;
    use Repr::*;
    match repr {
        Transparent(span)
        | Compound(
            Spanned {
                t: C | Rust | Primitive(U32 | I32 | U64 | I64 | U128 | I128 | Usize | Isize),
                span,
            },
            _,
        ) => Err(Error::new(
            *span,
            "`FromBytes` only supported on enums with `#[repr(...)]` attributes `u8`, `i8`, `u16`, or `i16`",
        )),
        Compound(Spanned { t: Primitive(U8 | I8), span: _ }, _align) => Ok(8),
        Compound(Spanned { t: Primitive(U16 | I16), span: _ }, _align) => Ok(16),
    }
}

#[cfg(test)]
pub(crate) mod testutil {
    use proc_macro2::TokenStream;
    use syn::visit::{self, Visit};

    /// Checks for hygiene violations in the generated code.
    ///
    /// # Panics
    ///
    /// Panics if a hygiene violation is found.
    pub(crate) fn check_hygiene(ts: TokenStream) {
        struct AmbiguousItemVisitor;

        fn panic_ambiguous_path(path: &impl quote::ToTokens) -> ! {
            panic!(
                "Found ambiguous path `{}` in generated output. \
                 All associated item access must be fully qualified (e.g., `<Self as Trait>::Item`) \
                 to prevent hygiene issues.",
                quote::quote!(#path)
            );
        }

        impl<'ast> Visit<'ast> for AmbiguousItemVisitor {
            fn visit_expr_path(&mut self, i: &'ast syn::ExprPath) {
                if let Some(qself) = &i.qself {
                    if qself.position == 0 {
                        panic_ambiguous_path(i);
                    }
                }
                visit::visit_expr_path(self, i);
            }

            fn visit_path(&mut self, i: &'ast syn::Path) {
                if i.segments.len() > 1 && i.segments.first().unwrap().ident == "Self" {
                    panic_ambiguous_path(i);
                }
                visit::visit_path(self, i);
            }

            fn visit_type_path(&mut self, i: &'ast syn::TypePath) {
                if let Some(qself) = &i.qself {
                    if qself.position == 0 {
                        panic_ambiguous_path(i);
                    }
                }
                visit::visit_type_path(self, i);
            }
        }

        let file = syn::parse2::<syn::File>(ts).expect("failed to parse generated output as File");
        AmbiguousItemVisitor.visit_file(&file);
    }

    #[test]
    fn test_check_hygiene_success() {
        check_hygiene(quote::quote! {
            fn foo() {
                let _ = <Self as Trait>::Item;
            }
        });
    }

    #[test]
    #[should_panic(expected = "Found ambiguous path `Self :: Ambiguous`")]
    fn test_check_hygiene_failure() {
        check_hygiene(quote::quote! {
            fn foo() {
                let _ = Self::Ambiguous;
            }
        });
    }

    #[test]
    #[should_panic(expected = "Found ambiguous path `< T > :: Ambiguous`")]
    fn test_check_hygiene_type_relative_expr_failure() {
        check_hygiene(quote::quote! {
            fn foo<T>() {
                let _ = <T>::Ambiguous;
            }
        });
    }

    #[test]
    #[should_panic(expected = "Found ambiguous path `< T > :: Ambiguous`")]
    fn test_check_hygiene_type_relative_type_failure() {
        check_hygiene(quote::quote! {
            fn foo<T>() {
                let _: <T>::Ambiguous;
            }
        });
    }

    #[test]
    fn test_validate_crate_path() {
        use syn::parse_str;

        let valid = [
            "zerocopy",
            "crate",
            "crate::foo::bar",
            "self",
            "self::foo",
            "self::super::foo",
            "super",
            "super::foo",
            "super::super::foo",
            "super::super::super",
            "foo::bar::baz",
            "::foo::bar",
        ];

        for path_str in valid {
            let path = parse_str::<syn::Path>(path_str).unwrap();
            assert!(
                super::validate_crate_path(&path).is_ok(),
                "expected valid path for `{}`",
                path_str
            );
        }

        let invalid = [
            "::crate::foo",
            "::self::foo",
            "::super::foo",
            "Self",
            "Self::foo",
            "foo::Self::bar",
            "foo::crate::bar",
            "foo::super::bar",
            "foo::self",
            "super::foo::super",
            "super::crate::foo",
            "crate::super::foo",
            "self::self::foo",
        ];

        for path_str in invalid {
            let path = parse_str::<syn::Path>(path_str).unwrap();
            assert!(
                super::validate_crate_path(&path).is_err(),
                "expected invalid path for `{}`",
                path_str
            );
        }
    }

    #[test]
    fn test_path_is_ident() {
        use syn::parse_str;

        for (expected, ordinary, raw) in [
            ("doc", "doc", "r#doc"),
            ("repr", "repr", "r#repr"),
            ("zerocopy", "zerocopy", "r#zerocopy"),
            ("on_error", "on_error", "r#on_error"),
        ] {
            let ordinary = parse_str::<syn::Path>(ordinary).unwrap();
            let raw = parse_str::<syn::Path>(raw).unwrap();
            assert!(super::path_is_ident(&ordinary, expected));
            assert!(super::path_is_ident(&raw, expected));
        }

        // `r#crate` is forbidden by Rust's raw identifier grammar.
        let crate_path = parse_str::<syn::Path>("crate").unwrap();
        assert!(super::path_is_ident(&crate_path, "crate"));

        for path in ["::zerocopy", "module::zerocopy"] {
            let path = parse_str::<syn::Path>(path).unwrap();
            assert!(!super::path_is_ident(&path, "zerocopy"));
        }
    }

    #[test]
    fn test_raw_zerocopy_attributes() {
        let ast: syn::DeriveInput = syn::parse_quote! {
            #[r#zerocopy(crate = "renamed", r#on_error = "skip")]
            struct Foo;
        };

        let ctx = super::Ctx::try_from_derive_input(ast).unwrap();
        let path = &ctx.zerocopy_crate;
        assert_eq!(quote::quote!(#path).to_string(), ":: renamed");
        assert!(ctx.skip_on_error);
    }
}
