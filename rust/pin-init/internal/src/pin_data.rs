// SPDX-License-Identifier: Apache-2.0 OR MIT

use proc_macro2::TokenStream;
use quote::{format_ident, quote};
use syn::{
    parse::{End, Nothing, Parse},
    parse_quote, parse_quote_spanned,
    spanned::Spanned,
    visit_mut::VisitMut,
    Field, Generics, Ident, Item, PathSegment, Type, TypePath, Visibility, WhereClause,
};

use crate::diagnostics::{DiagCtxt, ErrorGuaranteed};

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

pub(crate) fn pin_data(
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

    let fields: Vec<(bool, &Field)> = struct_
        .fields
        .iter_mut()
        .map(|field| {
            let len = field.attrs.len();
            field.attrs.retain(|a| !a.path().is_ident("pin"));
            (len != field.attrs.len(), &*field)
        })
        .collect();

    for (pinned, field) in &fields {
        if !pinned && is_phantom_pinned(&field.ty) {
            dcx.error(
                field,
                format!(
                    "The field `{}` of type `PhantomPinned` only has an effect \
                    if it has the `#[pin]` attribute",
                    field.ident.as_ref().unwrap(),
                ),
            );
        }
    }

    let unpin_impl = generate_unpin_impl(&struct_.ident, &struct_.generics, &fields);
    let drop_impl = generate_drop_impl(&struct_.ident, &struct_.generics, args);
    let projections =
        generate_projections(&struct_.vis, &struct_.ident, &struct_.generics, &fields);
    let the_pin_data =
        generate_the_pin_data(&struct_.vis, &struct_.ident, &struct_.generics, &fields);

    Ok(quote! {
        #struct_
        #projections
        // We put the rest into this const item, because it then will not be accessible to anything
        // outside.
        const _: () = {
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

fn generate_unpin_impl(
    ident: &Ident,
    generics: &Generics,
    fields: &[(bool, &Field)],
) -> TokenStream {
    let (_, ty_generics, _) = generics.split_for_impl();
    let mut generics_with_pin_lt = generics.clone();
    generics_with_pin_lt.params.insert(0, parse_quote!('__pin));
    generics_with_pin_lt.make_where_clause();
    let (
        impl_generics_with_pin_lt,
        ty_generics_with_pin_lt,
        Some(WhereClause {
            where_token,
            predicates,
        }),
    ) = generics_with_pin_lt.split_for_impl()
    else {
        unreachable!()
    };
    let pinned_fields = fields.iter().filter_map(|(b, f)| b.then_some(f));
    quote! {
        // This struct will be used for the unpin analysis. It is needed, because only structurally
        // pinned fields are relevant whether the struct should implement `Unpin`.
        #[allow(dead_code)] // The fields below are never used.
        struct __Unpin #generics_with_pin_lt
        #where_token
            #predicates
        {
            __phantom_pin: ::core::marker::PhantomData<fn(&'__pin ()) -> &'__pin ()>,
            __phantom: ::core::marker::PhantomData<
                fn(#ident #ty_generics) -> #ident #ty_generics
            >,
            #(#pinned_fields),*
        }

        #[doc(hidden)]
        impl #impl_generics_with_pin_lt ::core::marker::Unpin for #ident #ty_generics
        #where_token
            __Unpin #ty_generics_with_pin_lt: ::core::marker::Unpin,
            #predicates
        {}
    }
}

fn generate_drop_impl(ident: &Ident, generics: &Generics, args: Args) -> TokenStream {
    let (impl_generics, ty_generics, whr) = generics.split_for_impl();
    let has_pinned_drop = matches!(args, Args::PinnedDrop(_));
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
            #[expect(drop_bounds)]
            impl<T: ::core::ops::Drop + ?::core::marker::Sized> MustNotImplDrop for T {}
            impl #impl_generics MustNotImplDrop for #ident #ty_generics
                #whr
            {}
            // We also take care to prevent users from writing a useless `PinnedDrop` implementation.
            // They might implement `PinnedDrop` correctly for the struct, but forget to give
            // `PinnedDrop` as the parameter to `#[pin_data]`.
            #[expect(non_camel_case_types)]
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

fn generate_projections(
    vis: &Visibility,
    ident: &Ident,
    generics: &Generics,
    fields: &[(bool, &Field)],
) -> TokenStream {
    let (impl_generics, ty_generics, _) = generics.split_for_impl();
    let mut generics_with_pin_lt = generics.clone();
    generics_with_pin_lt.params.insert(0, parse_quote!('__pin));
    let (_, ty_generics_with_pin_lt, whr) = generics_with_pin_lt.split_for_impl();
    let projection = format_ident!("{ident}Projection");
    let this = format_ident!("this");

    let (fields_decl, fields_proj) = collect_tuple(fields.iter().map(
        |(
            pinned,
            Field {
                vis,
                ident,
                ty,
                attrs,
                ..
            },
        )| {
            let mut attrs = attrs.clone();
            attrs.retain(|a| !a.path().is_ident("pin"));
            let mut no_doc_attrs = attrs.clone();
            no_doc_attrs.retain(|a| !a.path().is_ident("doc"));
            let ident = ident
                .as_ref()
                .expect("only structs with named fields are supported");
            if *pinned {
                (
                    quote!(
                        #(#attrs)*
                        #vis #ident: ::core::pin::Pin<&'__pin mut #ty>,
                    ),
                    quote!(
                        #(#no_doc_attrs)*
                        // SAFETY: this field is structurally pinned.
                        #ident: unsafe { ::core::pin::Pin::new_unchecked(&mut #this.#ident) },
                    ),
                )
            } else {
                (
                    quote!(
                        #(#attrs)*
                        #vis #ident: &'__pin mut #ty,
                    ),
                    quote!(
                        #(#no_doc_attrs)*
                        #ident: &mut #this.#ident,
                    ),
                )
            }
        },
    ));
    let structurally_pinned_fields_docs = fields
        .iter()
        .filter_map(|(pinned, field)| pinned.then_some(field))
        .map(|Field { ident, .. }| format!(" - `{}`", ident.as_ref().unwrap()));
    let not_structurally_pinned_fields_docs = fields
        .iter()
        .filter_map(|(pinned, field)| (!pinned).then_some(field))
        .map(|Field { ident, .. }| format!(" - `{}`", ident.as_ref().unwrap()));
    let docs = format!(" Pin-projections of [`{ident}`]");
    quote! {
        #[doc = #docs]
        #[allow(dead_code)]
        #[doc(hidden)]
        #vis struct #projection #generics_with_pin_lt {
            #(#fields_decl)*
            ___pin_phantom_data: ::core::marker::PhantomData<&'__pin mut ()>,
        }

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
            #vis fn project<'__pin>(
                self: ::core::pin::Pin<&'__pin mut Self>,
            ) -> #projection #ty_generics_with_pin_lt {
                // SAFETY: we only give access to `&mut` for fields not structurally pinned.
                let #this = unsafe { ::core::pin::Pin::get_unchecked_mut(self) };
                #projection {
                    #(#fields_proj)*
                    ___pin_phantom_data: ::core::marker::PhantomData,
                }
            }
        }
    }
}

fn generate_the_pin_data(
    vis: &Visibility,
    ident: &Ident,
    generics: &Generics,
    fields: &[(bool, &Field)],
) -> TokenStream {
    let (impl_generics, ty_generics, whr) = generics.split_for_impl();

    // For every field, we create an initializing projection function according to its projection
    // type. If a field is structurally pinned, then it must be initialized via `PinInit`, if it is
    // not structurally pinned, then it can be initialized via `Init`.
    //
    // The functions are `unsafe` to prevent accidentally calling them.
    fn handle_field(
        Field {
            vis,
            ident,
            ty,
            attrs,
            ..
        }: &Field,
        struct_ident: &Ident,
        pinned: bool,
    ) -> TokenStream {
        let mut attrs = attrs.clone();
        attrs.retain(|a| !a.path().is_ident("pin"));
        let ident = ident
            .as_ref()
            .expect("only structs with named fields are supported");
        let project_ident = format_ident!("__project_{ident}");
        let (init_ty, init_fn, project_ty, project_body, pin_safety) = if pinned {
            (
                quote!(PinInit),
                quote!(__pinned_init),
                quote!(::core::pin::Pin<&'__slot mut #ty>),
                // SAFETY: this field is structurally pinned.
                quote!(unsafe { ::core::pin::Pin::new_unchecked(slot) }),
                quote!(
                    /// - `slot` will not move until it is dropped, i.e. it will be pinned.
                ),
            )
        } else {
            (
                quote!(Init),
                quote!(__init),
                quote!(&'__slot mut #ty),
                quote!(slot),
                quote!(),
            )
        };
        let slot_safety = format!(
            " `slot` points at the field `{ident}` inside of `{struct_ident}`, which is pinned.",
        );
        quote! {
            /// # Safety
            ///
            /// - `slot` is a valid pointer to uninitialized memory.
            /// - the caller does not touch `slot` when `Err` is returned, they are only permitted
            ///   to deallocate.
            #pin_safety
            #(#attrs)*
            #vis unsafe fn #ident<E>(
                self,
                slot: *mut #ty,
                init: impl ::pin_init::#init_ty<#ty, E>,
            ) -> ::core::result::Result<(), E> {
                // SAFETY: this function has the same safety requirements as the __init function
                // called below.
                unsafe { ::pin_init::#init_ty::#init_fn(init, slot) }
            }

            /// # Safety
            ///
            #[doc = #slot_safety]
            #(#attrs)*
            #vis unsafe fn #project_ident<'__slot>(
                self,
                slot: &'__slot mut #ty,
            ) -> #project_ty {
                #project_body
            }
        }
    }

    let field_accessors = fields
        .iter()
        .map(|(pinned, field)| handle_field(field, ident, *pinned))
        .collect::<TokenStream>();
    quote! {
        // We declare this struct which will host all of the projection function for our type. It
        // will be invariant over all generic parameters which are inherited from the struct.
        #[doc(hidden)]
        #vis struct __ThePinData #generics
            #whr
        {
            __phantom: ::core::marker::PhantomData<
                fn(#ident #ty_generics) -> #ident #ty_generics
            >,
        }

        impl #impl_generics ::core::clone::Clone for __ThePinData #ty_generics
            #whr
        {
            fn clone(&self) -> Self { *self }
        }

        impl #impl_generics ::core::marker::Copy for __ThePinData #ty_generics
            #whr
        {}

        #[allow(dead_code)] // Some functions might never be used and private.
        #[expect(clippy::missing_safety_doc)]
        impl #impl_generics __ThePinData #ty_generics
            #whr
        {
            #field_accessors
        }

        // SAFETY: We have added the correct projection functions above to `__ThePinData` and
        // we also use the least restrictive generics possible.
        unsafe impl #impl_generics ::pin_init::__internal::HasPinData for #ident #ty_generics
            #whr
        {
            type PinData = __ThePinData #ty_generics;

            unsafe fn __pin_data() -> Self::PinData {
                __ThePinData { __phantom: ::core::marker::PhantomData }
            }
        }

        // SAFETY: TODO
        unsafe impl #impl_generics ::pin_init::__internal::PinData for __ThePinData #ty_generics
            #whr
        {
            type Datee = #ident #ty_generics;
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

// replace with `.collect()` once MSRV is above 1.79
fn collect_tuple<A, B>(iter: impl Iterator<Item = (A, B)>) -> (Vec<A>, Vec<B>) {
    let mut res_a = vec![];
    let mut res_b = vec![];
    for (a, b) in iter {
        res_a.push(a);
        res_b.push(b);
    }
    (res_a, res_b)
}
