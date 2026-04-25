// SPDX-License-Identifier: Apache-2.0 OR MIT

use std::collections::{BTreeMap, BTreeSet};

use proc_macro2::{Span, TokenStream};
use quote::{format_ident, quote, quote_spanned, ToTokens};
use syn::{
    parse::{End, Nothing, Parse},
    parse_quote, parse_quote_spanned,
    punctuated::Punctuated,
    spanned::Spanned,
    visit::Visit,
    visit_mut::VisitMut,
    Field, Fields, GenericParam, Generics, Ident, Index, Item, ItemStruct, Lifetime, LifetimeParam,
    Member, PathSegment, Type, TypePath,
};

use crate::{
    diagnostics::{DiagCtxt, ErrorGuaranteed},
    util::*,
};

pub(crate) mod kw {
    syn::custom_keyword!(PinnedDrop);
}

pub(crate) enum Args {
    Nothing(Nothing),
    #[allow(dead_code)]
    PinnedDrop(kw::PinnedDrop),
}

impl Parse for Args {
    fn parse(input: syn::parse::ParseStream<'_>) -> syn::Result<Self> {
        let lh = input.lookahead1();
        if lh.peek(End) {
            input.parse().map(Self::Nothing)
        } else if lh.peek(kw::PinnedDrop) {
            input.parse().map(Self::PinnedDrop)
        } else {
            Err(lh.error())
        }
    }
}

impl ToTokens for Args {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        match self {
            Self::Nothing(_) => (),
            Self::PinnedDrop(kw) => kw.to_tokens(tokens),
        }
    }
}

/// Description of how a field is borrowed.
#[derive(Clone, Copy, Default, PartialEq, Eq)]
enum BorrowedKind {
    /// Implicitly inferreed.
    #[default]
    Shared,
}

/// Information about a borrowed field.
struct BorrowedInfo {
    kind: BorrowedKind,
    /// Field lifetime for this field.
    lifetime: Lifetime,
}

#[derive(Clone, Copy, Default, PartialEq, Eq)]
enum Variance {
    /// Implicitly inferred variance.
    #[default]
    Covariant,
}

/// Information about field lifetimes captured in a type.
struct Capture {
    variance: Variance,
    /// Lifetime to be captured.
    lifetime: Lifetime,
}

impl std::borrow::Borrow<Lifetime> for Capture {
    fn borrow(&self) -> &Lifetime {
        &self.lifetime
    }
}

impl PartialEq for Capture {
    fn eq(&self, other: &Self) -> bool {
        self.lifetime == other.lifetime
    }
}

impl Eq for Capture {}

impl PartialOrd for Capture {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for Capture {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.lifetime.cmp(&other.lifetime)
    }
}

struct FieldInfo {
    field: Field,
    member: Member,
    pinned: bool,
    borrowed: Option<BorrowedInfo>,
    captures: BTreeSet<Capture>,
    generic_lt_captures: BTreeSet<Lifetime>,
    generic_ty_captures: BTreeSet<Ident>,
}

struct StructInfo {
    args: Args,
    struct_: ItemStruct,
    fields: Vec<FieldInfo>,
    field_idx_map: BTreeMap<Ident, usize>,
    is_tuple_struct: bool,
    self_referential: bool,
    /// Field lifetime generics.
    field_lts: Generics,
}

pub(crate) fn expand_with_cfg(
    args: Args,
    input: Item,
    dcx: &mut DiagCtxt,
) -> Result<TokenStream, ErrorGuaranteed> {
    let mut struct_ = match input {
        Item::Struct(struct_) => struct_,
        Item::Enum(enum_) => {
            return Err(dcx.error(
                enum_.enum_token,
                "`#[pin_data]` only supports structs for now",
            ));
        }
        Item::Union(union) => {
            return Err(dcx.error(
                union.union_token,
                "`#[pin_data]` only supports structs for now",
            ));
        }
        rest => {
            return Err(dcx.error(
                rest,
                "`#[pin_data]` can only be applied to struct, enum and union definitions",
            ));
        }
    };

    // Handling cfg can gets very complicated, especially for tuple structs. Therefore, resolve all
    // field cfgs first before continuing.
    //
    // We need to perform this after parsing so we can reliably detect field cfgs.
    for (field_idx, field) in struct_.fields.iter_mut().enumerate() {
        let cfg = field.attrs.extract_cfg_attrs();
        if cfg.is_empty() {
            continue;
        }

        let cfg_true_struct = quote!(#struct_);

        let punctuated = match &mut struct_.fields {
            Fields::Named(fields) => &mut fields.named,
            Fields::Unnamed(fields) => &mut fields.unnamed,
            Fields::Unit => unreachable!(),
        };
        *punctuated = std::mem::take(punctuated)
            .into_pairs()
            .enumerate()
            .filter(|&(i, _)| i != field_idx)
            .map(|(_, p)| p)
            .collect();
        let cfg_false_struct = quote!(#struct_);

        // Resolve one field at a time until we've got no more field cfgs.
        //
        // This is linear time because macro invocations with false cfg will not be expanded.
        return Ok(quote!(
            #[cfg(all(#(#cfg,)*))]
            #[::pin_init::pin_data(#args)]
            #cfg_true_struct

            #[cfg(not(all(#(#cfg,)*)))]
            #[::pin_init::pin_data(#args)]
            #cfg_false_struct
        ));
    }

    expand(args, struct_, dcx)
}

fn expand(
    args: Args,
    mut struct_: ItemStruct,
    dcx: &mut DiagCtxt,
) -> Result<TokenStream, ErrorGuaranteed> {
    // The generics might contain the `Self` type. Since this macro will define a new type with the
    // same generics and bounds, this poses a problem: `Self` will refer to the new type as opposed
    // to this struct definition. Therefore we have to replace `Self` with the concrete name.
    let mut replacer = {
        let name = &struct_.ident;
        let (_, ty_generics, _) = struct_.generics.split_for_impl();
        SelfReplacer(parse_quote!(#name #ty_generics))
    };
    replacer.visit_generics_mut(&mut struct_.generics);
    replacer.visit_fields_mut(&mut struct_.fields);

    let is_tuple_struct = matches!(struct_.fields, Fields::Unnamed(_));

    // Collect all bound lifetimes from generics.
    let bound_lifetimes: BTreeSet<&Lifetime> =
        struct_.generics.lifetimes().map(|x| &x.lifetime).collect();
    // Collect all type parameters from generics.
    let type_params: BTreeSet<&Ident> = struct_.generics.type_params().map(|x| &x.ident).collect();
    // Collect all fields.
    let field_idx_map: BTreeMap<Ident, usize> = struct_
        .fields
        .iter()
        .enumerate()
        .filter_map(|(index, field)| Some((field.ident.clone()?, index)))
        .collect();

    // Keep track on fields being implicitly borrowed by being mentioned.
    let mut implicitly_borrowed = BTreeSet::new();

    let mut fields: Vec<FieldInfo> = struct_
        .fields
        .into_iter()
        .enumerate()
        .map(|(index, mut field)| {
            let pinned = field.attrs.extract_single_attr(dcx, "pin").is_some();

            assert!(
                !field.attrs.iter().any(|a| a.path().is_ident("cfg")),
                "cfgs should be all resolved at this point"
            );
            let member = match &field.ident {
                Some(ident) => Member::Named(ident.clone()),
                None => Member::Unnamed(Index {
                    index: index as u32,
                    span: field.span(),
                }),
            };

            let mut captures = BTreeSet::new();
            let wildcard_variance = Variance::default();

            let mut generic_lt_captures = BTreeSet::new();
            let mut generic_ty_captures = BTreeSet::new();

            // Infer lifetime based on the field referenced.
            // Bound lifetimes from struct generics take priority.
            //
            // For example,
            // ```
            // struct Foo<'a> {
            //     bar: &'a (),
            //     a: u32,
            // }
            // ```
            // would not be inferred as self-referential because `'a` is already bound by the
            // struct generics.
            Lifetime::visitor(|lt| {
                if bound_lifetimes.contains(lt) {
                    generic_lt_captures.insert(lt.clone());
                    return;
                }

                if captures.contains(lt) {
                    return;
                }

                if !field_idx_map.contains_key(&lt.ident) {
                    dcx.error(
                        lt,
                        format!("`{lt}` is neither a lifetime in generics nor a field name"),
                    );
                    return;
                }

                captures.insert(Capture {
                    variance: wildcard_variance,
                    lifetime: lt.clone(),
                });
            })
            .visit_type(&field.ty);

            for capture in captures.iter() {
                implicitly_borrowed.insert(capture.lifetime.ident.clone());
            }

            GenericParam::maybe_type_params_visitor(|ident| {
                if type_params.contains(ident) {
                    generic_ty_captures.insert(ident.clone());
                }
            })
            .visit_type(&field.ty);

            FieldInfo {
                field,
                member,
                pinned,
                borrowed: None,
                captures,
                generic_lt_captures,
                generic_ty_captures,
            }
        })
        .collect();

    for field_name in implicitly_borrowed.into_iter() {
        let field = &mut fields[field_idx_map[&field_name]];

        // If field is not explicit marked as borrowed, infer a shared borrow.
        if field.borrowed.is_none() {
            field.borrowed = Some(BorrowedInfo {
                kind: BorrowedKind::Shared,
                // Obtaining from `field` instead of `field_name` for the correct span.
                lifetime: Lifetime::from_ident(&field.member.as_ident()),
            });
        }
    }

    // Check that field lifetimes do not appear in the bounds.
    Lifetime::visitor(|lt| {
        if bound_lifetimes.contains(&lt) {
            return;
        }

        if field_idx_map.contains_key(&lt.ident) {
            // Forbid the use of field lifetimes within bounds.
            dcx.error(lt, "field lifetimes cannot be used in bounds");
        }

        // Otherwise this is completely unbound. Let Rust compiler produce that error instead.
    })
    .visit_generics(&struct_.generics);

    // Create a lifetime parameter for each field.
    let borrowed_fields: Vec<_> = fields.iter().filter_map(|f| f.borrowed.as_ref()).collect();
    let mut field_lts = Generics {
        lt_token: None,
        params: borrowed_fields
            .iter()
            .map(|borrowed| {
                GenericParam::Lifetime(LifetimeParam {
                    attrs: Vec::new(),
                    lifetime: borrowed.lifetime.clone(),
                    colon_token: None,
                    bounds: Default::default(),
                })
            })
            .collect(),
        gt_token: None,
        where_clause: None,
    };

    // Insert necessary bounds to make types well-formed.
    for field in fields.iter() {
        let Some(borrowed) = &field.borrowed else {
            continue;
        };
        let field_lt = &borrowed.lifetime;

        // For each borrowed field that borrows from other fields, we need to insert outlive bounds.
        for capture in &field.captures {
            let lt = &capture.lifetime;
            field_lts
                .make_where_clause()
                .predicates
                .push(parse_quote!(#lt: #field_lt));
        }

        // For each borrowed field that references a generic, we also need to insert their outlive
        // bounds so they can refer to generics.
        for lt in field.generic_lt_captures.iter() {
            field_lts
                .make_where_clause()
                .predicates
                .push(parse_quote!(#lt: #field_lt));
        }

        for ty in field.generic_ty_captures.iter() {
            field_lts
                .make_where_clause()
                .predicates
                .push(parse_quote!(#ty: #field_lt));
        }
    }

    struct_.fields = Fields::Unit;
    let info = StructInfo {
        self_referential: fields
            .iter()
            .any(|f| !f.captures.is_empty() || f.borrowed.is_some()),
        args,
        struct_,
        fields,
        field_idx_map,
        is_tuple_struct,
        field_lts,
    };

    for field in &info.fields {
        if !field.pinned && is_phantom_pinned(&field.field.ty) {
            dcx.warn(
                &field.field,
                format!(
                    "The field {} of type `PhantomPinned` only has an effect \
                    if it has the `#[pin]` attribute",
                    field.member.display_name(),
                ),
            );
        }
    }

    if info.self_referential {
        dcx.error(
            &info.struct_.ident,
            "self-referential support is not fully implemented",
        );
    }

    let struct_def = generate_struct_def(&info);
    let unpin_impl = generate_unpin_impl(&info);
    let drop_impl = generate_drop_impl(&info);
    let drop_order_check = generate_drop_order_check(dcx, &info);
    let variance_check = generate_variance_check(&info);
    let projections = generate_projections(&info);
    let the_pin_data = generate_the_pin_data(&info);

    Ok(quote! {
        #struct_def
        // We put the rest into this const item, because it then will not be accessible to anything
        // outside.
        const _: () = {
            #drop_order_check
            #variance_check
            #projections
            #the_pin_data
            #unpin_impl
            #drop_impl
        };
    })
}

fn is_phantom_pinned(ty: &Type) -> bool {
    match ty {
        Type::Path(TypePath { qself: None, path }) => {
            // Cannot possibly refer to `PhantomPinned` (except alias, but that's on the user).
            if path.segments.len() > 3 {
                return false;
            }
            // If there is a `::`, then the path needs to be `::core::marker::PhantomPinned` or
            // `::std::marker::PhantomPinned`.
            if path.leading_colon.is_some() && path.segments.len() != 3 {
                return false;
            }
            let expected: Vec<&[&str]> = vec![&["PhantomPinned"], &["marker"], &["core", "std"]];
            for (actual, expected) in path.segments.iter().rev().zip(expected) {
                if !actual.arguments.is_empty() || expected.iter().all(|e| actual.ident != e) {
                    return false;
                }
            }
            true
        }
        _ => false,
    }
}

fn generate_struct_def(info: &StructInfo) -> TokenStream {
    let ItemStruct {
        attrs,
        vis,
        struct_token,
        ident,
        generics,
        fields: _,
        semi_token,
    } = &info.struct_;

    let generated_fields = info.fields.iter().map(|field| {
        let Field {
            attrs,
            vis,
            mutability: _,
            ident,
            colon_token,
            ty,
        } = &field.field;

        let mut ty = ty.to_token_stream();

        // Replace lifetime for self-referential fields.
        if !field.captures.is_empty() {
            // Build a chain `for<'a> fn(&'a ()) -> ... -> (Ty,)`. Such type will have a `EraseLt`
            // implementation and thus may be used inside `Erase`.
            ty = quote!((#ty,));

            for borrow in field.captures.iter().rev() {
                let lt = &borrow.lifetime;
                ty = quote!(for<#lt> fn(&#lt()) -> #ty);
            }

            ty = quote!(::pin_init::__internal::Erase<#ty>);
        };

        if field.borrowed.is_some() {
            ty = quote!(::pin_init::__internal::Borrowed<#ty>);
        }

        quote! {
           #(#attrs)* #vis #ident #colon_token #ty
        }
    });

    let whr = &generics.where_clause;

    if info.is_tuple_struct {
        quote!(
            #(#attrs)*
            #vis
            #struct_token #ident #generics (#(#generated_fields,)*) #whr
            #semi_token
        )
    } else {
        quote!(
            #(#attrs)*
            #vis
            #struct_token #ident #generics #whr {
                #(#generated_fields,)*
            }
            #semi_token
        )
    }
}

fn generate_unpin_impl(info: &StructInfo) -> TokenStream {
    let ItemStruct {
        generics, ident, ..
    } = &info.struct_;
    let (impl_generics, ty_generics, whr) = generics.split_for_impl();
    let predicates = whr
        .map(|x| &x.predicates)
        .unwrap_or(const { &Punctuated::new() });

    if info.self_referential {
        // Self-referential structs must always be pinned.
        return quote! {
            #[doc(hidden)]
            impl #impl_generics ::core::marker::Unpin for #ident #ty_generics
            where
                // the `for<'__dummy>` HRTB makes this not error without the `trivial_bounds`
                // feature <https://github.com/rust-lang/rust/issues/48214#issuecomment-2557829956>.
                for<'__dummy> ::core::marker::PhantomPinned: ::core::marker::Unpin,
                #predicates
            {}
        };
    }

    let pinned_fields = info.fields.iter().filter(|f| f.pinned).map(|f| {
        let ident = f.member.as_ident();
        let ty = &f.field.ty;
        quote!(
            #ident: #ty
        )
    });

    quote! {
        // This struct will be used for the unpin analysis. It is needed, because only structurally
        // pinned fields are relevant whether the struct should implement `Unpin`.
        #[allow(
            dead_code, // The fields below are never used.
            non_snake_case // The warning will be emitted on the struct definition.
        )]
        struct __Unpin #generics #whr
        {
            __phantom: ::pin_init::__internal::PhantomInvariant<#ident #ty_generics>,
            #(#pinned_fields),*
        }

        #[doc(hidden)]
        impl #impl_generics ::core::marker::Unpin for #ident #ty_generics
        where
            // the `for<'__dummy>` HRTB makes this not error without the `trivial_bounds`
            // feature <https://github.com/rust-lang/rust/issues/48214#issuecomment-2557829956>.
            for<'__dummy> __Unpin #ty_generics: ::core::marker::Unpin,
            #predicates
        {}
    }
}

fn generate_drop_impl(info: &StructInfo) -> TokenStream {
    let ItemStruct {
        generics, ident, ..
    } = &info.struct_;
    let (impl_generics, ty_generics, whr) = generics.split_for_impl();
    let has_pinned_drop = matches!(info.args, Args::PinnedDrop(_));
    // We need to disallow normal `Drop` implementation, the exact behavior depends on whether
    // `PinnedDrop` was specified in `args`.
    if has_pinned_drop {
        // When `PinnedDrop` was specified we just implement `Drop` and delegate.
        quote! {
            impl #impl_generics ::core::ops::Drop for #ident #ty_generics
                #whr
            {
                fn drop(&mut self) {
                    // SAFETY: Since this is a destructor, `self` will not move after this function
                    // terminates, since it is inaccessible.
                    let pinned = unsafe { ::core::pin::Pin::new_unchecked(self) };
                    // SAFETY: Since this is a drop function, we can create this token to call the
                    // pinned destructor of this type.
                    let token = unsafe { ::pin_init::__internal::OnlyCallFromDrop::new() };
                    ::pin_init::PinnedDrop::drop(pinned, token);
                }
            }
        }
    } else {
        // When no `PinnedDrop` was specified, then we have to prevent implementing drop.
        quote! {
            // We prevent this by creating a trait that will be implemented for all types implementing
            // `Drop`. Additionally we will implement this trait for the struct leading to a conflict,
            // if it also implements `Drop`
            trait MustNotImplDrop {}
            impl<T: ::core::ops::Drop + ?::core::marker::Sized> MustNotImplDrop for T {}
            impl #impl_generics MustNotImplDrop for #ident #ty_generics
                #whr
            {}
            // We also take care to prevent users from writing a useless `PinnedDrop` implementation.
            // They might implement `PinnedDrop` correctly for the struct, but forget to give
            // `PinnedDrop` as the parameter to `#[pin_data]`.
            trait UselessPinnedDropImpl_you_need_to_specify_PinnedDrop {}
            impl<T: ::pin_init::PinnedDrop + ?::core::marker::Sized>
                UselessPinnedDropImpl_you_need_to_specify_PinnedDrop for T {}
            impl #impl_generics
                UselessPinnedDropImpl_you_need_to_specify_PinnedDrop for #ident #ty_generics
                #whr
            {}
        }
    }
}

fn generate_drop_order_check(dcx: &mut DiagCtxt, info: &StructInfo) -> TokenStream {
    let ItemStruct {
        ident: struct_name,
        generics,
        ..
    } = &info.struct_;

    // If the struct is not self-referential then we can just skip.
    if !info.self_referential {
        return quote!();
    }

    // Make sure fields are dropped earlier than the fields that they borrow.
    for (i, field) in info.fields.iter().enumerate() {
        let ident = field.member.as_ident();
        for capture in &field.captures {
            let borrowed_field = &capture.lifetime.ident;

            if let Some(&borrowed_idx) = info.field_idx_map.get(borrowed_field) {
                if i == borrowed_idx {
                    // We need a strict outlive relationship, in case the lifetime is needed by the
                    // field's drop glue.
                    dcx.error(
                        borrowed_field,
                        format!("field `{ident}` cannot borrow from itself"),
                    );
                } else if i > borrowed_idx {
                    dcx.error(
                        borrowed_field,
                        format!("field `{ident}` borrows `{borrowed_field}`, but drops later"),
                    );
                }
            }
        }
    }

    // The check above is necessary, but not sufficient.
    //
    // Consider this case:
    // ```
    // struct Foo {
    //     x: &'b &'a (),
    //     a: String,
    //     y: PrintOnDrop<&'b str>,
    //     b: String,
    // }
    // ```
    // we need to ensure that `b` will strictly outlive `a`.
    //
    // Rust needs to ensure that types are well-formed; in the above example, `&'b &'a ()` is
    // well-formed only if `a` outlive `b`. To avoid requiring everyone from having to express this
    // bound explicitly when declaring a struct, the `'b: 'a` bound is inferred by the Rust
    // compiler. However this causes an issue, where now `&'a str` can be coerced to `&'b str`
    // because compiler thinks that it shorten the lifetime. We'll be able to put a reference to `a`
    // into `y`; but `a` drops first, so when `y` drops, it accesses `a` and causes a
    // use-after-free!
    //
    // Therefore, we must ensure the types contained within the struct has their implied bound being
    // consistent with the actual lifetime relationship. We create a `__drop_order_check` function,
    // with known lifetime bounds as bounds on the function, and asks Rust to *prove* that the types
    // are wellformed, given the bounds that we understand.

    let generics_with_field_lt = CombinedGenerics(vec![&info.field_lts, generics]);

    let (_, ty_generics, _) = generics.split_for_impl();
    let (impl_generics_with_field_lt, _, whr_with_field_lt) =
        generics_with_field_lt.split_for_impl();

    // Prove the wellformedness of struct fields with regarding to the bounds of
    // `__drop_order_check`.
    //
    // Consider this case:
    // ```
    // struct Foo {
    //     x: &'b &'a (),
    //     a: String,
    //     y: PrintOnDrop<&'b str>,
    //     b: String,
    // }
    // ```
    // we need to ensure that `b` will strictly outlive `a`.
    //
    // Rust needs to ensure that types are well-formed; in the above example, `&'b &'a ()` is
    // well-formed only if `a` outlive `b`. To avoid requiring everyone from having to express this
    // bound explicitly when declaring a struct, the `'b: 'a` bound is inferred by the Rust
    // compiler. However this causes an issue, where now `&'a str` can be coerced to `&'b str`
    // because compiler thinks that it shorten the lifetime. We'll be able to put a reference to `a`
    // into `y`; but `a` drops first, so when `y` drops, it accesses `a` and causes a
    // use-after-free!
    //
    // Rust needs to *prove* the wellformedness of the type below, taking into account only the
    // explicitly defined bounds plus the bounds implied by the lifetime-erased struct (but not
    // the full implied bound between the field lifetimes).
    let wf_proofs = info.fields.iter().rev().map(|f| {
        let ty = &f.field.ty;
        let span = ty.span().resolved_at(Span::mixed_site());
        let ident = f.member.as_ident();
        let lt = f.borrowed.as_ref().map(|b| &b.lifetime);
        quote_spanned!(span =>
            let #ident: &#lt mut #ty = loop {};
        )
    });

    let struct_span = struct_name.span().resolved_at(Span::mixed_site());
    quote_spanned! {struct_span =>
        #[allow(non_snake_case, unused)]
        fn __drop_order_check #impl_generics_with_field_lt (
            // This must be present so the function can *assume* the implied bounds on the erased
            // struct. For example, if the struct has `&'a T`, Rust will infer `T: 'a`; we still
            // want to assume these bounds as they are not relevant to the field lifetimes.
            _: &#struct_name #ty_generics,
        ) #whr_with_field_lt {
            #(#wf_proofs)*
        }
    }
}

/// Produce variance checks, so we can ensure that the variance of lifetimes captured by field types
/// actually match our expectation.
fn generate_variance_check(info: &StructInfo) -> TokenStream {
    if !info.self_referential {
        return quote!();
    }

    let mut checks = Vec::new();

    for f in info.fields.iter() {
        let covariant_captures: Vec<_> = f
            .captures
            .iter()
            .filter(|b| b.variance == Variance::Covariant)
            .map(|b| &b.lifetime)
            .collect();
        if covariant_captures.is_empty() {
            continue;
        }

        let ident = f.member.as_ident();
        // Use the span of type for better error message.
        let span = f.field.ty.span().resolved_at(Span::mixed_site());

        let other_field_lifetimes = Generics {
            lt_token: None,
            params: f
                .captures
                .iter()
                .filter(|b| b.variance != Variance::Covariant)
                .map(|b| GenericParam::Lifetime(LifetimeParam::new(b.lifetime.clone())))
                .collect(),
            gt_token: None,
            where_clause: None,
        };

        let long = Lifetime::new("'__long", span);
        let long_ty = f
            .field
            .ty
            .replace_lifetimes(&covariant_captures, &vec![&long; covariant_captures.len()]);

        let short = Lifetime::new("'__short", span);
        let short_ty = f
            .field
            .ty
            .replace_lifetimes(&covariant_captures, &vec![&short; covariant_captures.len()]);

        let check_name = format_ident!("__{ident}_covariance", span = span);

        // Add `<'__long: '__short, 'short>` as additional generics.
        let covariance_check_generics = parse_quote!(<#long: #short, #short>);
        let combined_generics = CombinedGenerics(vec![
            &covariance_check_generics,
            &other_field_lifetimes,
            &info.struct_.generics,
        ]);
        let (combined_impl_generics, _, whr) = combined_generics.split_for_impl();

        checks.push(quote_spanned!(span =>
            // Emit a check to ensure the type is *really* covariant for soundness.
            fn #check_name #combined_impl_generics (long: #long_ty) -> #short_ty #whr {
                long
            }
        ));
    }

    quote!(
        #(#checks)*
    )
}

fn generate_projections(info: &StructInfo) -> TokenStream {
    let ItemStruct {
        vis,
        ident,
        generics,
        ..
    } = &info.struct_;
    let this_lt_generics: Generics = parse_quote!(<'__this>);
    let generics_with_this_lt = CombinedGenerics(vec![&this_lt_generics, generics]);

    let (impl_generics, ty_generics, whr) = generics.split_for_impl();
    let (_, ty_generics_with_this_lt, _) = generics_with_this_lt.split_for_impl();

    let this = format_ident!("this");

    let (fields_decl, fields_proj): (Vec<_>, Vec<_>) = info
        .fields
        .iter()
        .map(|field| {
            let Field { vis, ty, .. } = &field.field;
            let member = &field.member;
            // The projection of a tuple struct is a tuple struct itself, so its fields are
            // positional and must not be named.
            let name = (!info.is_tuple_struct).then(|| {
                let ident = field.member.as_ident();
                quote!(#ident:)
            });

            if field.pinned {
                (
                    quote!(
                        #vis #name ::core::pin::Pin<&'__this mut #ty>,
                    ),
                    quote!(
                        // SAFETY: this field is structurally pinned.
                        #name unsafe { ::core::pin::Pin::new_unchecked(&mut #this.#member) },
                    ),
                )
            } else {
                (
                    quote!(
                        #vis #name &'__this mut #ty,
                    ),
                    quote!(
                        #name &mut #this.#member,
                    ),
                )
            }
        })
        .collect();
    let structurally_pinned_fields_docs = info
        .fields
        .iter()
        .filter(|f| f.pinned)
        .map(|f| format!(" - {}", f.member.display_name()));
    let not_structurally_pinned_fields_docs = info
        .fields
        .iter()
        .filter(|f| !f.pinned)
        .map(|f| format!(" - {}", f.member.display_name()));
    let docs = format!(" Pin-projections of [`{ident}`]");
    let (projection_def, projection_init) = if info.is_tuple_struct {
        (
            quote! {
                #vis struct __Projection #generics_with_this_lt (
                    #(#fields_decl)*
                    ::core::marker::PhantomData<&'__this mut #ident #ty_generics>,
                ) #whr;
            },
            quote! {
                __Projection(
                    #(#fields_proj)*
                    ::core::marker::PhantomData,
                )
            },
        )
    } else {
        (
            quote! {
                #vis struct __Projection #generics_with_this_lt
                    #whr
                {
                    #(#fields_decl)*
                    __this: ::core::marker::PhantomData<&'__this mut #ident #ty_generics>,
                }
            },
            quote! {
                __Projection {
                    #(#fields_proj)*
                    __this: ::core::marker::PhantomData,
                }
            },
        )
    };
    quote! {
        #[doc = #docs]
        // Allow `non_snake_case` since the same warning will be emitted on
        // the struct definition.
        #[allow(dead_code, non_snake_case)]
        #[doc(hidden)]
        #projection_def

        impl #impl_generics #ident #ty_generics
            #whr
        {
            /// Pin-projects all fields of `Self`.
            ///
            /// These fields are structurally pinned:
            #(#[doc = #structurally_pinned_fields_docs])*
            ///
            /// These fields are **not** structurally pinned:
            #(#[doc = #not_structurally_pinned_fields_docs])*
            #[inline]
            #vis fn project<'__this>(
                self: ::core::pin::Pin<&'__this mut Self>,
            ) -> __Projection #ty_generics_with_this_lt {
                // SAFETY: we only give access to `&mut` for fields not structurally pinned.
                let #this = unsafe { ::core::pin::Pin::get_unchecked_mut(self) };
                #projection_init
            }
        }
    }
}

fn generate_the_pin_data(info: &StructInfo) -> TokenStream {
    let ItemStruct {
        vis,
        ident: struct_name,
        generics,
        ..
    } = &info.struct_;

    // Wrap in `CombinedGenerics` because it's ty generics will always output `<>`, so it can be
    // used with `for`.
    let field_lts = CombinedGenerics(vec![&info.field_lts]);
    let generics_with_field_lt = CombinedGenerics(vec![&info.field_lts, generics]);

    let (impl_generics, ty_generics, whr) = generics.split_for_impl();
    let (_, field_lt_ty_generics, _) = field_lts.split_for_impl();
    let (impl_generics_with_lt, ty_generics_with_field_lt, whr_with_field_lt) =
        generics_with_field_lt.split_for_impl();

    // Wrap each field in a `PhantomInvariant`. For borrowed fields, additionally
    // use `&#lt mut #ty` so the `lt` becomes associated with `#ty` which deduces
    // implied bounds.
    let phantom_fields = info.fields.iter().map(|f| {
        let ty = &f.field.ty;
        let ident = f.member.as_ident();

        if let Some(borrowed) = &f.borrowed {
            let lt = &borrowed.lifetime;
            quote!(
                #ident: ::pin_init::__internal::PhantomInvariant<&#lt mut #ty>,
            )
        } else {
            quote!(
                #ident: ::pin_init::__internal::PhantomInvariant<#ty>,
            )
        }
    });

    let field_accessors = info
        .fields
        .iter()
        .map(|f| {
            let Field { vis, ty, .. } = &f.field;
            let field_name = f.member.as_ident();
            let member = &f.member;
            let pin_marker = if f.pinned {
                quote!(Pinned)
            } else {
                quote!(Unpinned)
            };

            let (slot_ty, slot_arg) = match &f.borrowed {
                None => (quote!(Slot), quote!()),
                Some(BorrowedInfo {
                    kind: BorrowedKind::Shared,
                    lifetime,
                }) => (
                    // For borrowed fields, create a `SelfRefSlot`, which after initialization
                    // turns into a `SelfRefDropGuard` instead of `DropGuard`.
                    //
                    // They're mostly the same, except that `SelfRefDropGuard` returns `&'field T`
                    // instead of `&'guard T` for let bindings; this allows it to be used to be
                    // used to initialize other fields.
                    //
                    // The soundness of doing so relies on fact that `__make_init` requires a
                    // higher-ranked trait bound on the closure. Within the closure (which is the
                    // caller of the generated slot projection functions here), it can make no
                    // assumptions on the lifetime except for those implied by the struct's bounds,
                    // and we have validated them in `generate_drop_check`.
                    quote!(SelfRefSlot),
                    quote!(#lifetime,),
                ),
            };

            quote! {
                /// # Safety
                ///
                /// - `slot` is valid and properly aligned.
                /// - `(*slot).#field_name` is properly aligned.
                /// - `(*slot).#field_name` points to uninitialized and exclusively accessed
                ///   memory.
                // Allow `non_snake_case` since the same warning will be emitted on
                // the struct definition.
                #[allow(non_snake_case)]
                #[inline(always)]
                #vis unsafe fn #field_name(
                    self,
                    slot: *mut #struct_name #ty_generics,
                ) -> ::pin_init::__internal::#slot_ty<
                    #slot_arg ::pin_init::__internal::#pin_marker, #ty
                > {
                    // CAST: `as _` is needed to convert types wrapped inside `SelfRef`.
                    // SAFETY:
                    // - If `#pin_marker` is `Pinned`, the corresponding field is structurally
                    //   pinned.
                    // - Other safety requirements follows the safety requirement.
                    // - If `#slot_ty` is `SelfRefSlot`, the lifetime `#lt` represents that of the
                    //   field.
                    unsafe { ::pin_init::__internal::#slot_ty::new(&raw mut (*slot).#member as _) }
                }
            }
        })
        .collect::<TokenStream>();

    quote! {
        // We declare this struct which will host all of the projection function for our type.
        #[doc(hidden)]
        #[allow(non_snake_case)]
        #vis struct __PinDataLt #generics_with_field_lt
            #whr_with_field_lt
        {
            #(#phantom_fields)*
            __pin_phantom: ::core::marker::PhantomData<#struct_name #ty_generics>,
        }

        impl #impl_generics_with_lt ::core::clone::Clone for __PinDataLt #ty_generics_with_field_lt
            #whr_with_field_lt
        {
            fn clone(&self) -> Self { *self }
        }

        impl #impl_generics_with_lt ::core::marker::Copy for __PinDataLt #ty_generics_with_field_lt
            #whr_with_field_lt
        {}

        #[allow(dead_code)] // Some functions might never be used and private.
        #[expect(clippy::missing_safety_doc)]
        impl #impl_generics_with_lt __PinDataLt #ty_generics_with_field_lt
            #whr_with_field_lt
        {
            #field_accessors
        }

        // Declare a type that serves as the entry point of interaction with the `pin_init!` macro.
        // We use this type instead of defining methods directly on user's type to avoid possibility
        // of name conflicts.
        #[doc(hidden)]
        #vis struct __ThePinData #generics
            #whr
        {
            __phantom: ::pin_init::__internal::PhantomInvariant<#struct_name #ty_generics>,
        }

        impl #impl_generics ::core::clone::Clone for __ThePinData #ty_generics
            #whr
        {
            #[inline]
            fn clone(&self) -> Self { *self }
        }

        impl #impl_generics ::core::marker::Copy for __ThePinData #ty_generics
            #whr
        {}

        impl #impl_generics __ThePinData #ty_generics
            #whr
        {
            /// Type inference helper function.
            #[inline(always)]
            #vis fn __make_closure<__F, __E>(self, f: __F) -> __F
            where
                __F: for #field_lt_ty_generics ::core::ops::FnOnce(
                    *mut #struct_name #ty_generics,
                    __PinDataLt #ty_generics_with_field_lt
                ) -> ::core::result::Result<::pin_init::__internal::InitOk, __E>,
            {
                f
            }

            #[inline(always)]
            #vis fn __with_lt #field_lts(self) -> __PinDataLt #ty_generics_with_field_lt {
                // Generate a zeroed to avoid naming all fields.
                // SAFETY: `__PinDataLt` only contains phantom fields.
                unsafe { ::core::mem::zeroed() }
            }
        }

        // SAFETY: We have added the correct projection functions above to `__ThePinData` and
        // we also use the least restrictive generics possible.
        unsafe impl #impl_generics ::pin_init::__internal::HasPinData for #struct_name #ty_generics
            #whr
        {
            type PinData = __ThePinData #ty_generics;

            #[inline]
            fn __pin_data(_: ::pin_init::__internal::InitData<Self>) -> Self::PinData {
                __ThePinData { __phantom: ::pin_init::__internal::PhantomInvariant::new() }
            }
        }
    }
}

struct SelfReplacer(PathSegment);

impl VisitMut for SelfReplacer {
    fn visit_path_mut(&mut self, i: &mut syn::Path) {
        if i.is_ident("Self") {
            let span = i.span();
            let seg = &self.0;
            *i = parse_quote_spanned!(span=> #seg);
        } else {
            syn::visit_mut::visit_path_mut(self, i);
        }
    }

    fn visit_path_segment_mut(&mut self, seg: &mut PathSegment) {
        if seg.ident == "Self" {
            let span = seg.span();
            let this = &self.0;
            *seg = parse_quote_spanned!(span=> #this);
        } else {
            syn::visit_mut::visit_path_segment_mut(self, seg);
        }
    }

    fn visit_item_mut(&mut self, _: &mut Item) {
        // Do not descend into items, since items reset/change what `Self` refers to.
    }
}
