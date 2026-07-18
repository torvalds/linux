// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

use proc_macro2::TokenStream;
use quote::quote;
use syn::{
    parse_quote, spanned::Spanned as _, Data, DataEnum, DataStruct, DataUnion, DeriveInput, Error,
    Expr, Fields, Ident, Index, Type,
};

use crate::{
    repr::{EnumRepr, StructUnionRepr},
    util::{
        const_block, enum_size_from_repr, generate_tag_enum, Ctx, DataExt, FieldBounds,
        ImplBlockBuilder, Trait, TraitBound,
    },
};
fn tag_ident(variant_ident: &Ident) -> Ident {
    ident!(("___ZEROCOPY_TAG_{}", variant_ident), variant_ident.span())
}

/// Generates a constant for the tag associated with each variant of the enum.
/// When we match on the enum's tag, each arm matches one of these constants. We
/// have to use constants here because:
///
/// - The type that we're matching on is not the type of the tag, it's an
///   integer of the same size as the tag type and with the same bit patterns.
/// - We can't read the enum tag as an enum because the bytes may not represent
///   a valid variant.
/// - Patterns do not currently support const expressions, so we have to assign
///   these constants to names rather than use them inline in the `match`
///   statement.
fn generate_tag_consts(data: &DataEnum) -> TokenStream {
    let tags = data.variants.iter().map(|v| {
        let variant_ident = &v.ident;
        let tag_ident = tag_ident(variant_ident);

        quote! {
            // This casts the enum variant to its discriminant, and then
            // converts the discriminant to the target integral type via a
            // numeric cast [1].
            //
            // Because these are the same size, this is defined to be a no-op
            // and therefore is a lossless conversion [2].
            //
            // [1] Per https://doc.rust-lang.org/1.81.0/reference/expressions/operator-expr.html#enum-cast:
            //
            //   Casts an enum to its discriminant.
            //
            // [2] Per https://doc.rust-lang.org/1.81.0/reference/expressions/operator-expr.html#numeric-cast:
            //
            //   Casting between two integers of the same size (e.g. i32 -> u32)
            //   is a no-op.
            const #tag_ident: ___ZerocopyTagPrimitive =
                ___ZerocopyTag::#variant_ident as ___ZerocopyTagPrimitive;
        }
    });

    quote! {
        #(#tags)*
    }
}

fn variant_struct_ident(variant_ident: &Ident) -> Ident {
    ident!(("___ZerocopyVariantStruct_{}", variant_ident), variant_ident.span())
}

/// Generates variant structs for the given enum variant.
///
/// These are structs associated with each variant of an enum. They are
/// `repr(C)` tuple structs with the same fields as the variant after a
/// `MaybeUninit<___ZerocopyInnerTag>`.
///
/// In order to unify the generated types for `repr(C)` and `repr(int)` enums,
/// we use a "fused" representation with fields for both an inner tag and an
/// outer tag. Depending on the repr, we will set one of these tags to the tag
/// type and the other to `()`. This lets us generate the same code but put the
/// tags in different locations.
fn generate_variant_structs(ctx: &Ctx, data: &DataEnum) -> TokenStream {
    let (impl_generics, ty_generics, where_clause) = ctx.ast.generics.split_for_impl();

    let enum_name = &ctx.ast.ident;

    // All variant structs have a `PhantomData<MyEnum<...>>` field because we
    // don't know which generic parameters each variant will use, and unused
    // generic parameters are a compile error.
    let core = ctx.core_path();
    let phantom_ty = quote! {
        #core::marker::PhantomData<#enum_name #ty_generics>
    };

    let variant_structs = data.variants.iter().filter_map(|variant| {
        // We don't generate variant structs for unit variants because we only
        // need to check the tag. This helps cut down our generated code a bit.
        if matches!(variant.fields, Fields::Unit) {
            return None;
        }

        let variant_struct_ident = variant_struct_ident(&variant.ident);
        let field_types = variant.fields.iter().map(|f| &f.ty);

        let variant_struct = parse_quote! {
            #[repr(C)]
            struct #variant_struct_ident #impl_generics (
                #core::mem::MaybeUninit<___ZerocopyInnerTag>,
                #(#field_types,)*
                #phantom_ty,
            ) #where_clause;
        };

        // We do this rather than emitting `#[derive(::zerocopy::TryFromBytes)]`
        // because that is not hygienic, and this is also more performant.
        let try_from_bytes_impl =
            derive_try_from_bytes(&ctx.with_input(&variant_struct), Trait::TryFromBytes)
                .expect("derive_try_from_bytes should not fail on synthesized type");

        Some(quote! {
            #variant_struct
            #try_from_bytes_impl
        })
    });

    quote! {
        #(#variant_structs)*
    }
}

fn variants_union_field_ident(ident: &Ident) -> Ident {
    // Field names are prefixed with `__field_` to prevent name collision
    // with the `__nonempty` field.
    ident!(("__field_{}", ident), ident.span())
}

fn generate_variants_union(ctx: &Ctx, data: &DataEnum) -> TokenStream {
    let generics = &ctx.ast.generics;
    let (_, ty_generics, _) = generics.split_for_impl();

    let fields = data.variants.iter().filter_map(|variant| {
        // We don't generate variant structs for unit variants because we only
        // need to check the tag. This helps cut down our generated code a bit.
        if matches!(variant.fields, Fields::Unit) {
            return None;
        }

        let field_name = variants_union_field_ident(&variant.ident);
        let variant_struct_ident = variant_struct_ident(&variant.ident);

        let core = ctx.core_path();
        Some(quote! {
            #field_name: #core::mem::ManuallyDrop<#variant_struct_ident #ty_generics>,
        })
    });

    let variants_union = parse_quote! {
        #[repr(C)]
        union ___ZerocopyVariants #generics {
            #(#fields)*
            // Enums can have variants with no fields, but unions must
            // have at least one field. So we just add a trailing unit
            // to ensure that this union always has at least one field.
            // Because this union is `repr(C)`, this unit type does not
            // affect the layout.
            __nonempty: (),
        }
    };

    let has_field =
        derive_has_field_struct_union(&ctx.with_input(&variants_union), &variants_union.data);

    quote! {
        #variants_union
        #has_field
    }
}

/// Generates an implementation of `is_bit_valid` for an arbitrary enum.
///
/// The general process is:
///
/// 1. Generate a tag enum. This is an enum with the same repr, variants, and
///    corresponding discriminants as the original enum, but without any fields
///    on the variants. This gives us access to an enum where the variants have
///    the same discriminants as the one we're writing `is_bit_valid` for.
/// 2. Make constants from the variants of the tag enum. We need these because
///    we can't put const exprs in match arms.
/// 3. Generate variant structs. These are structs which have the same fields as
///    each variant of the enum, and are `#[repr(C)]` with an optional "inner
///    tag".
/// 4. Generate a variants union, with one field for each variant struct type.
/// 5. And finally, our raw enum is a `#[repr(C)]` struct of an "outer tag" and
///    the variants union.
///
/// See these reference links for fully-worked example decompositions.
///
/// - `repr(C)`: <https://doc.rust-lang.org/reference/type-layout.html#reprc-enums-with-fields>
/// - `repr(int)`: <https://doc.rust-lang.org/reference/type-layout.html#primitive-representation-of-enums-with-fields>
/// - `repr(C, int)`: <https://doc.rust-lang.org/reference/type-layout.html#combining-primitive-representations-of-enums-with-fields-and-reprc>
pub(crate) fn derive_is_bit_valid(
    ctx: &Ctx,
    data: &DataEnum,
    repr: &EnumRepr,
) -> Result<TokenStream, Error> {
    let trait_path = Trait::TryFromBytes.crate_path(ctx);
    let tag_enum = generate_tag_enum(ctx, repr, data);
    let tag_consts = generate_tag_consts(data);

    let (outer_tag_type, inner_tag_type) = if repr.is_c() {
        (quote! { ___ZerocopyTag }, quote! { () })
    } else if repr.is_primitive() {
        (quote! { () }, quote! { ___ZerocopyTag })
    } else {
        return Err(Error::new(
            ctx.ast.span(),
            "must have #[repr(C)] or #[repr(Int)] attribute in order to guarantee this type's memory layout",
        ));
    };

    let variant_structs = generate_variant_structs(ctx, data);
    let variants_union = generate_variants_union(ctx, data);

    let (impl_generics, ty_generics, where_clause) = ctx.ast.generics.split_for_impl();

    let zerocopy_crate = &ctx.zerocopy_crate;
    let has_tag = ImplBlockBuilder::new(ctx, data, Trait::HasTag, FieldBounds::None)
        .inner_extras(quote! {
            type Tag = ___ZerocopyTag;
            type ProjectToTag = #zerocopy_crate::pointer::cast::CastSized;
        })
        .build();
    let has_fields = data.variants().into_iter().flat_map(|(variant, fields)| {
        let variant_ident = &variant.unwrap().ident;
        let variants_union_field_ident = variants_union_field_ident(variant_ident);
        let field: Box<syn::Type> = parse_quote!(());
        fields.into_iter().enumerate().map(move |(idx, (vis, ident, ty))| {
            // Rust does not presently support explicit visibility modifiers on
            // enum fields, but we guard against the possibility to ensure this
            // derive remains sound.
            assert!(matches!(vis, syn::Visibility::Inherited));
            let variant_struct_field_index = Index::from(idx + 1);
            let (_, ty_generics, _) = ctx.ast.generics.split_for_impl();
            let has_field_trait = Trait::HasField {
                variant_id: parse_quote!({ #zerocopy_crate::ident_id!(#variant_ident) }),
                // Since Rust does not presently support explicit visibility
                // modifiers on enum fields, any public type is suitable here;
                // we use `()`.
                field: field.clone(),
                field_id: parse_quote!({ #zerocopy_crate::ident_id!(#ident) }),
            };
            let has_field_path = has_field_trait.crate_path(ctx);
            let has_field = ImplBlockBuilder::new(
                ctx,
                data,
                has_field_trait,
                FieldBounds::None,
            )
            .inner_extras(quote! {
                type Type = #ty;

                #[inline(always)]
                fn project(slf: #zerocopy_crate::pointer::PtrInner<'_, Self>) -> *mut <Self as #has_field_path>::Type {
                    use #zerocopy_crate::pointer::cast::{CastSized, Projection};

                    slf.project::<___ZerocopyRawEnum #ty_generics, CastSized>()
                        .project::<_, Projection<_, { #zerocopy_crate::STRUCT_VARIANT_ID }, { #zerocopy_crate::ident_id!(variants) }>>()
                        .project::<_, Projection<_, { #zerocopy_crate::REPR_C_UNION_VARIANT_ID }, { #zerocopy_crate::ident_id!(#variants_union_field_ident) }>>()
                        .project::<_, Projection<_, { #zerocopy_crate::STRUCT_VARIANT_ID }, { #zerocopy_crate::ident_id!(value) }>>()
                        .project::<_, Projection<_, { #zerocopy_crate::STRUCT_VARIANT_ID }, { #zerocopy_crate::ident_id!(#variant_struct_field_index) }>>()
                        .as_ptr()
                }
            })
            .build();

            let project = ImplBlockBuilder::new(
                ctx,
                data,
                Trait::ProjectField {
                    variant_id: parse_quote!({ #zerocopy_crate::ident_id!(#variant_ident) }),
                    // Since Rust does not presently support explicit visibility
                    // modifiers on enum fields, any public type is suitable
                    // here; we use `()`.
                    field: field.clone(),
                    field_id: parse_quote!({ #zerocopy_crate::ident_id!(#ident) }),
                    invariants: parse_quote!((Aliasing, Alignment, #zerocopy_crate::invariant::Initialized)),
                },
                FieldBounds::None,
            )
            .param_extras(vec![
                parse_quote!(Aliasing: #zerocopy_crate::invariant::Aliasing),
                parse_quote!(Alignment: #zerocopy_crate::invariant::Alignment),
            ])
            .inner_extras(quote! {
                type Error = #zerocopy_crate::util::macro_util::core_reexport::convert::Infallible;
                type Invariants = (Aliasing, Alignment, #zerocopy_crate::invariant::Initialized);
            })
            .build();

            quote! {
                #has_field
                #project
            }
        })
    });

    let core = ctx.core_path();
    let match_arms = data.variants.iter().map(|variant| {
        let tag_ident = tag_ident(&variant.ident);
        let variant_struct_ident = variant_struct_ident(&variant.ident);
        let variants_union_field_ident = variants_union_field_ident(&variant.ident);

        if matches!(variant.fields, Fields::Unit) {
            // Unit variants don't need any further validation beyond checking
            // the tag.
            quote! {
                #tag_ident => true
            }
        } else {
            quote! {
                #tag_ident => {
                    // SAFETY: Since we know that the tag is `#tag_ident`, we
                    // know that no other `&`s exist which refer to this enum
                    // as any other variant.
                    let variant_md = variants.cast::<
                        _,
                        #zerocopy_crate::pointer::cast::Projection<
                            // #zerocopy_crate::ReadOnly<_>,
                            _,
                            { #zerocopy_crate::REPR_C_UNION_VARIANT_ID },
                            { #zerocopy_crate::ident_id!(#variants_union_field_ident) }
                        >,
                        _
                    >();
                    let variant = variant_md.cast::<
                        #zerocopy_crate::ReadOnly<#variant_struct_ident #ty_generics>,
                        #zerocopy_crate::pointer::cast::CastSized,
                        (#zerocopy_crate::pointer::BecauseRead, _)
                    >();
                    <
                        #variant_struct_ident #ty_generics as #trait_path
                    >::is_bit_valid(variant)
                }
            }
        }
    });

    let generics = &ctx.ast.generics;
    let raw_enum: DeriveInput = parse_quote! {
        #[repr(C)]
        struct ___ZerocopyRawEnum #generics {
            tag: ___ZerocopyOuterTag,
            variants: ___ZerocopyVariants #ty_generics,
        }
    };

    let self_ident = &ctx.ast.ident;
    let invariants_eq_impl = quote! {
        // SAFETY: `___ZerocopyRawEnum` is designed to have the same layout,
        // validity, and invariants as `Self`.
        unsafe impl #impl_generics #zerocopy_crate::pointer::InvariantsEq<___ZerocopyRawEnum #ty_generics> for #self_ident #ty_generics #where_clause {}
    };

    let raw_enum_projections =
        derive_has_field_struct_union(&ctx.with_input(&raw_enum), &raw_enum.data);

    let raw_enum = quote! {
        #raw_enum
        #invariants_eq_impl
        #raw_enum_projections
    };

    Ok(quote! {
        // SAFETY: We use `is_bit_valid` to validate that the bit pattern of the
        // enum's tag corresponds to one of the enum's discriminants. Then, we
        // check the bit validity of each field of the corresponding variant.
        // Thus, this is a sound implementation of `is_bit_valid`.
        #[inline]
        fn is_bit_valid<___ZcAlignment>(
            mut candidate: #zerocopy_crate::Maybe<'_, Self, ___ZcAlignment>,
        ) -> #core::primitive::bool
        where
            ___ZcAlignment: #zerocopy_crate::invariant::Alignment,
        {
            #tag_enum

            type ___ZerocopyTagPrimitive = #zerocopy_crate::util::macro_util::SizeToTag<
                { #core::mem::size_of::<___ZerocopyTag>() },
            >;

            #tag_consts

            type ___ZerocopyOuterTag = #outer_tag_type;
            type ___ZerocopyInnerTag = #inner_tag_type;

            #variant_structs

            #variants_union

            #raw_enum

            #has_tag

            #(#has_fields)*

            let tag = {
                // SAFETY:
                // - The provided cast addresses a subset of the bytes addressed
                //   by `candidate` because it addresses the starting tag of the
                //   enum.
                // - Because the pointer is cast from `candidate`, it has the
                //   same provenance as it.
                // - There are no `UnsafeCell`s in the tag because it is a
                //   primitive integer.
                // - `tag_ptr` is casted from `candidate`, whose referent is
                //   `Initialized`. Since we have not written uninitialized
                //   bytes into the referent, `tag_ptr` is also `Initialized`.
                //
                // FIXME(#2874): Revise this to a `cast` once `candidate`
                // references a `ReadOnly<Self>`.
                let tag_ptr = unsafe {
                    candidate.reborrow().project_transmute_unchecked::<
                        _,
                        #zerocopy_crate::invariant::Initialized,
                        #zerocopy_crate::pointer::cast::CastSized
                    >()
                };
                tag_ptr.recall_validity::<_, (_, (_, _))>().read::<#zerocopy_crate::BecauseImmutable>()
            };

            let mut raw_enum = candidate.cast::<
                #zerocopy_crate::ReadOnly<___ZerocopyRawEnum #ty_generics>,
                #zerocopy_crate::pointer::cast::CastSized,
                (#zerocopy_crate::pointer::BecauseRead, _)
            >();

            let variants = #zerocopy_crate::into_inner!(raw_enum.project::<
                _,
                { #zerocopy_crate::STRUCT_VARIANT_ID },
                { #zerocopy_crate::ident_id!(variants) }
            >());

            match tag {
                #(#match_arms,)*
                _ => false,
            }
        }
    })
}
pub(crate) fn derive_try_from_bytes(ctx: &Ctx, top_level: Trait) -> Result<TokenStream, Error> {
    match &ctx.ast.data {
        Data::Struct(strct) => derive_try_from_bytes_struct(ctx, strct, top_level),
        Data::Enum(enm) => derive_try_from_bytes_enum(ctx, enm, top_level),
        Data::Union(unn) => Ok(derive_try_from_bytes_union(ctx, unn, top_level)),
    }
}
fn derive_has_field_struct_union(ctx: &Ctx, data: &dyn DataExt) -> TokenStream {
    let fields = ctx.ast.data.fields();
    if fields.is_empty() {
        return quote! {};
    }

    let field_tokens = fields.iter().map(|(vis, ident, _)| {
        let ident = ident!(("__z{}", ident), ident.span());
        quote!(
            #vis enum #ident {}
        )
    });

    let zerocopy_crate = &ctx.zerocopy_crate;
    let variant_id: Box<Expr> = match &ctx.ast.data {
        Data::Struct(_) => parse_quote!({ #zerocopy_crate::STRUCT_VARIANT_ID }),
        Data::Union(_) => {
            let is_repr_c = StructUnionRepr::from_attrs(&ctx.ast.attrs)
                .map(|repr| repr.is_c())
                .unwrap_or(false);
            if is_repr_c {
                parse_quote!({ #zerocopy_crate::REPR_C_UNION_VARIANT_ID })
            } else {
                parse_quote!({ #zerocopy_crate::UNION_VARIANT_ID })
            }
        }
        _ => unreachable!(),
    };

    let core = ctx.core_path();
    let has_tag = ImplBlockBuilder::new(ctx, data, Trait::HasTag, FieldBounds::None)
        .inner_extras(quote! {
            type Tag = ();
            type ProjectToTag = #zerocopy_crate::pointer::cast::CastToUnit;
        })
        .build();
    let has_fields = fields.iter().map(move |(_, ident, ty)| {
        let field_token = ident!(("__z{}", ident), ident.span());
        let field: Box<Type> = parse_quote!(#field_token);
        let field_id: Box<Expr> = parse_quote!({ #zerocopy_crate::ident_id!(#ident) });
        let has_field_trait = Trait::HasField {
                variant_id: variant_id.clone(),
                field: field.clone(),
                field_id: field_id.clone(),
            };
            let has_field_path = has_field_trait.crate_path(ctx);
            ImplBlockBuilder::new(
                ctx,
                data,
                has_field_trait,
                FieldBounds::None,
            )
            .inner_extras(quote! {
                type Type = #ty;

                #[inline(always)]
                fn project(slf: #zerocopy_crate::pointer::PtrInner<'_, Self>) -> *mut <Self as #has_field_path>::Type {
                    let slf = slf.as_ptr();
                    // SAFETY: By invariant on `PtrInner`, `slf` is a non-null
                    // pointer whose referent is zero-sized or lives in a valid
                    // allocation. Since `#ident` is a struct or union field of
                    // `Self`, this projection preserves or shrinks the referent
                    // size, and so the resulting referent also fits in the same
                    // allocation.
                    unsafe { #core::ptr::addr_of_mut!((*slf).#ident) }
                }
            }).outer_extras(if matches!(&ctx.ast.data, Data::Struct(..)) {
            let fields_preserve_alignment = StructUnionRepr::from_attrs(&ctx.ast.attrs)
                .map(|repr| repr.get_packed().is_none())
                .unwrap();
            let alignment = if fields_preserve_alignment {
                quote! { Alignment }
            } else {
                quote! { #zerocopy_crate::invariant::Unaligned }
            };
            // SAFETY: See comments on items.
            ImplBlockBuilder::new(
                ctx,
                data,
                Trait::ProjectField {
                    variant_id: variant_id.clone(),
                    field: field.clone(),
                    field_id: field_id.clone(),
                    invariants: parse_quote!((Aliasing, Alignment, #zerocopy_crate::invariant::Initialized)),
                },
                FieldBounds::None,
            )
            .param_extras(vec![
                parse_quote!(Aliasing: #zerocopy_crate::invariant::Aliasing),
                parse_quote!(Alignment: #zerocopy_crate::invariant::Alignment),
            ])
            .inner_extras(quote! {
                // SAFETY: Projection into structs is always infallible.
                type Error = #zerocopy_crate::util::macro_util::core_reexport::convert::Infallible;
                // SAFETY: The alignment of the projected `Ptr` is `Unaligned`
                // if the structure is packed; otherwise inherited from the
                // outer `Ptr`. If the validity of the outer pointer is
                // `Initialized`, so too is the validity of its fields.
                type Invariants = (Aliasing, #alignment, #zerocopy_crate::invariant::Initialized);
            })
            .build()
        } else {
            quote! {}
        })
        .build()
    });

    const_block(field_tokens.into_iter().chain(Some(has_tag)).chain(has_fields).map(Some))
}
fn derive_try_from_bytes_struct(
    ctx: &Ctx,
    strct: &DataStruct,
    top_level: Trait,
) -> Result<TokenStream, Error> {
    let extras = try_gen_trivial_is_bit_valid(ctx, top_level).unwrap_or_else(|| {
        let zerocopy_crate = &ctx.zerocopy_crate;
        let fields = strct.fields();
        let field_names = fields.iter().map(|(_vis, name, _ty)| name);
        let field_tys = fields.iter().map(|(_vis, _name, ty)| ty);
        let core = ctx.core_path();
        quote!(
            // SAFETY: We use `is_bit_valid` to validate that each field is
            // bit-valid, and only return `true` if all of them are. The bit
            // validity of a struct is just the composition of the bit
            // validities of its fields, so this is a sound implementation
            // of `is_bit_valid`.
            #[inline]
            fn is_bit_valid<___ZcAlignment>(
                mut candidate: #zerocopy_crate::Maybe<'_, Self, ___ZcAlignment>,
            ) -> #core::primitive::bool
            where
                ___ZcAlignment: #zerocopy_crate::invariant::Alignment,
            {
                true #(&& {
                    let field_candidate =   #zerocopy_crate::into_inner!(candidate.reborrow().project::<
                        _,
                        { #zerocopy_crate::STRUCT_VARIANT_ID },
                        { #zerocopy_crate::ident_id!(#field_names) }
                    >());
                    <#field_tys as #zerocopy_crate::TryFromBytes>::is_bit_valid(field_candidate)
                })*
            }
        )
    });
    Ok(ImplBlockBuilder::new(ctx, strct, Trait::TryFromBytes, FieldBounds::ALL_SELF)
        .inner_extras(extras)
        .outer_extras(derive_has_field_struct_union(ctx, strct))
        .build())
}
fn derive_try_from_bytes_union(ctx: &Ctx, unn: &DataUnion, top_level: Trait) -> TokenStream {
    let field_type_trait_bounds = FieldBounds::All(&[TraitBound::Slf]);

    let zerocopy_crate = &ctx.zerocopy_crate;
    let variant_id: Box<Expr> = {
        let is_repr_c =
            StructUnionRepr::from_attrs(&ctx.ast.attrs).map(|repr| repr.is_c()).unwrap_or(false);
        if is_repr_c {
            parse_quote!({ #zerocopy_crate::REPR_C_UNION_VARIANT_ID })
        } else {
            parse_quote!({ #zerocopy_crate::UNION_VARIANT_ID })
        }
    };

    let extras = try_gen_trivial_is_bit_valid(ctx, top_level).unwrap_or_else(|| {
        let fields = unn.fields();
        let field_names = fields.iter().map(|(_vis, name, _ty)| name);
        let field_tys = fields.iter().map(|(_vis, _name, ty)| ty);
        let core = ctx.core_path();
        quote!(
            // SAFETY: We use `is_bit_valid` to validate that any field is
            // bit-valid; we only return `true` if at least one of them is.
            // The bit validity of a union is not yet well defined in Rust,
            // but it is guaranteed to be no more strict than this
            // definition. See #696 for a more in-depth discussion.
            #[inline]
            fn is_bit_valid<___ZcAlignment>(
                mut candidate: #zerocopy_crate::Maybe<'_, Self, ___ZcAlignment>,
            ) -> #core::primitive::bool
            where
                ___ZcAlignment: #zerocopy_crate::invariant::Alignment,
            {
                false #(|| {
                    // SAFETY:
                    // - Since `ReadOnly<Self>: Immutable` unconditionally,
                    //   neither `*slf` nor the returned pointer's referent
                    //   permit interior mutation.
                    // - Both source and destination validity are
                    //   `Initialized`, which is always a sound
                    //   transmutation.
                    let field_candidate = unsafe {
                        candidate.reborrow().project_transmute_unchecked::<
                            _,
                            _,
                            #zerocopy_crate::pointer::cast::Projection<
                                _,
                                #variant_id,
                                { #zerocopy_crate::ident_id!(#field_names) }
                            >
                        >()
                    };

                    <#field_tys as #zerocopy_crate::TryFromBytes>::is_bit_valid(field_candidate)
                })*
            }
        )
    });
    ImplBlockBuilder::new(ctx, unn, Trait::TryFromBytes, field_type_trait_bounds)
        .inner_extras(extras)
        .outer_extras(derive_has_field_struct_union(ctx, unn))
        .build()
}
fn derive_try_from_bytes_enum(
    ctx: &Ctx,
    enm: &DataEnum,
    top_level: Trait,
) -> Result<TokenStream, Error> {
    let repr = EnumRepr::from_attrs(&ctx.ast.attrs)?;

    // If an enum has no fields, it has a well-defined integer representation,
    // and every possible bit pattern corresponds to a valid discriminant tag,
    // then it *could* be `FromBytes` (even if the user hasn't derived
    // `FromBytes`). This holds if, for `repr(uN)` or `repr(iN)`, there are 2^N
    // variants.
    let could_be_from_bytes = enum_size_from_repr(&repr)
        .map(|size| enm.fields().is_empty() && enm.variants.len() == 1usize << size)
        .unwrap_or(false);

    let trivial_is_bit_valid = try_gen_trivial_is_bit_valid(ctx, top_level);
    let extra = match (trivial_is_bit_valid, could_be_from_bytes) {
        (Some(is_bit_valid), _) => is_bit_valid,
        // SAFETY: It would be sound for the enum to implement `FromBytes`, as
        // required by `gen_trivial_is_bit_valid_unchecked`.
        (None, true) => unsafe { gen_trivial_is_bit_valid_unchecked(ctx) },
        (None, false) => match derive_is_bit_valid(ctx, enm, &repr) {
            Ok(extra) => extra,
            Err(_) if ctx.skip_on_error => return Ok(TokenStream::new()),
            Err(e) => return Err(e),
        },
    };

    Ok(ImplBlockBuilder::new(ctx, enm, Trait::TryFromBytes, FieldBounds::ALL_SELF)
        .inner_extras(extra)
        .build())
}
fn try_gen_trivial_is_bit_valid(ctx: &Ctx, top_level: Trait) -> Option<proc_macro2::TokenStream> {
    // If the top-level trait is `FromBytes` and `Self` has no type parameters,
    // then the `FromBytes` derive will fail compilation if `Self` is not
    // actually soundly `FromBytes`, and so we can rely on that for our
    // `is_bit_valid` impl. It's plausible that we could make changes - or Rust
    // could make changes (such as the "trivial bounds" language feature) - that
    // make this no longer true. To hedge against these, we include an explicit
    // `Self: FromBytes` check in the generated `is_bit_valid`, which is
    // bulletproof.
    //
    // If `ctx.skip_on_error` is true, we can't rely on the `FromBytes` derive
    // to fail compilation if `Self` is not actually soundly `FromBytes`.
    if matches!(top_level, Trait::FromBytes)
        && ctx.ast.generics.params.is_empty()
        && !ctx.skip_on_error
    {
        let zerocopy_crate = &ctx.zerocopy_crate;
        let core = ctx.core_path();
        Some(quote!(
            // SAFETY: See inline.
            #[inline(always)]
            fn is_bit_valid<___ZcAlignment>(
                _candidate: #zerocopy_crate::Maybe<'_, Self, ___ZcAlignment>,
            ) -> #core::primitive::bool
            where
                ___ZcAlignment: #zerocopy_crate::invariant::Alignment,
            {
                if false {
                    fn assert_is_from_bytes<T>()
                    where
                        T: #zerocopy_crate::FromBytes,
                        T: ?#core::marker::Sized,
                    {
                    }

                    assert_is_from_bytes::<Self>();
                }

                // SAFETY: The preceding code only compiles if `Self:
                // FromBytes`. Thus, this code only compiles if all initialized
                // byte sequences represent valid instances of `Self`.
                true
            }
        ))
    } else {
        None
    }
}

/// # Safety
///
/// All initialized bit patterns must be valid for `Self`.
unsafe fn gen_trivial_is_bit_valid_unchecked(ctx: &Ctx) -> proc_macro2::TokenStream {
    let zerocopy_crate = &ctx.zerocopy_crate;
    let core = ctx.core_path();
    quote!(
        // SAFETY: The caller of `gen_trivial_is_bit_valid_unchecked` has
        // promised that all initialized bit patterns are valid for `Self`.
        #[inline(always)]
        fn is_bit_valid<___ZcAlignment>(
            _candidate: #zerocopy_crate::Maybe<'_, Self, ___ZcAlignment>,
        ) -> #core::primitive::bool
        where
            ___ZcAlignment: #zerocopy_crate::invariant::Alignment,
        {
            true
        }
    )
}
