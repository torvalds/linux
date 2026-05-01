// SPDX-License-Identifier: Apache-2.0 OR MIT

use std::collections::{BTreeMap, BTreeSet};

use proc_macro2::TokenStream;
use quote::{format_ident, quote, ToTokens};
use syn::{
    parse::{End, Nothing, Parse},
    parse_quote, parse_quote_spanned,
    punctuated::Punctuated,
    spanned::Spanned,
    visit::Visit,
    visit_mut::VisitMut,
    Field, Fields, Generics, Ident, Index, Item, ItemStruct, Lifetime, Member, PathSegment, Type,
    TypePath,
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
#[expect(unused)]
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
#[expect(unused)]
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
}

struct StructInfo {
    args: Args,
    struct_: ItemStruct,
    fields: Vec<FieldInfo>,
    is_tuple_struct: bool,
    self_referential: bool,
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
                if bound_lifetimes.contains(lt) || captures.contains(lt) {
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

            FieldInfo {
                field,
                member,
                pinned,
                borrowed: None,
                captures,
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

    struct_.fields = Fields::Unit;
    let info = StructInfo {
        self_referential: fields
            .iter()
            .any(|f| !f.captures.is_empty() || f.borrowed.is_some()),
        args,
        struct_,
        fields,
        is_tuple_struct,
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
    let projections = generate_projections(&info);
    let the_pin_data = generate_the_pin_data(&info);

    Ok(quote! {
        #struct_def
        // We put the rest into this const item, because it then will not be accessible to anything
        // outside.
        const _: () = {
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
    let (impl_generics, ty_generics, whr) = generics.split_for_impl();

    // For every field, we create an initializing projection function according to its projection
    // type. If a field is structurally pinned, we create a `Slot` with `Pinned` which must be
    // initialized via `PinInit`; if it is not structurally pinned, then we create a `Slot` with
    // `Unpinned` which allows initialization via `Init`.
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
                ) -> ::pin_init::__internal::Slot<::pin_init::__internal::#pin_marker, #ty> {
                    // SAFETY:
                    // - If `#pin_marker` is `Pinned`, the corresponding field is structurally
                    //   pinned.
                    // - Other safety requirements follows the safety requirement.
                    unsafe { ::pin_init::__internal::Slot::new(&raw mut (*slot).#member) }
                }
            }
        })
        .collect::<TokenStream>();
    quote! {
        // We declare this struct which will host all of the projection function for our type. It
        // will be invariant over all generic parameters which are inherited from the struct.
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

        #[allow(dead_code)] // Some functions might never be used and private.
        impl #impl_generics __ThePinData #ty_generics
            #whr
        {
            /// Type inference helper function.
            #[inline(always)]
            #vis fn __make_closure<__F, __E>(self, f: __F) -> __F
            where
                __F: FnOnce(*mut #struct_name #ty_generics) ->
                    ::core::result::Result<::pin_init::__internal::InitOk, __E>,
            {
                f
            }

            #field_accessors
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
