// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT
//
use proc_macro2::{Ident, Span, TokenStream};
use quote::{quote, ToTokens as _};
use syn::{spanned::Spanned as _, Data, DataEnum, DataStruct, DataUnion, Error, Field};

use crate::{
    derive::project::{
        derive_enum, derive_projection_struct_union, generate_tag_consts, struct_union_variant_id,
        tag_ident,
    },
    repr::EnumRepr,
    util::{
        enum_size_from_repr, generate_tag_enum, Client, Ctx, DataExt, FieldBounds,
        ImplBlockBuilder, Trait, TraitBound,
    },
};

fn candidate_ident(ctx: &Ctx) -> Ident {
    // Choose a source-pointer name distinct from the field names. Mixed-site
    // hygiene keeps it out of the user's invariant expressions.
    let fields = ctx.ast.data.fields();
    let mut name = "candidate".to_owned();
    while fields.iter().any(|(_, field, _)| field.to_string().trim_start_matches("r#") == name) {
        name.push('_');
    }
    Ident::new(&name, Span::mixed_site())
}

/// Generates validation of every field of a struct or enum variant, leaving
/// the validated pointers in scope for subsequent field invariants.
/// A union is checked by invoking this once per field.
fn derive_variant_is_safe<'a>(
    ctx: &Ctx,
    variant_id: &TokenStream,
    fields: impl IntoIterator<Item = &'a Field>,
) -> Result<TokenStream, Error> {
    let fields = fields.into_iter().collect::<Vec<_>>();
    if fields.is_empty() {
        return Ok(quote!(true));
    }

    let zerocopy_crate = &ctx.zerocopy_crate;
    let core = ctx.core_path();
    let candidate = candidate_ident(ctx);
    let field_names = fields.iter().enumerate().map(|(idx, field)| {
        field
            .ident
            .as_ref()
            .map(|name| name.to_token_stream())
            .unwrap_or_else(|| syn::Index::from(idx).to_token_stream())
    });
    let field_checks = field_names
        .map(|name| {
            quote! {
                #zerocopy_crate::into_inner!(
                    #candidate.project::<
                        #zerocopy_crate::project_clients::TryFromBytesDerive,
                        _,
                        { #variant_id },
                        { #zerocopy_crate::ident_id!(#name) },
                    >()
                ).try_into_safe::<_, #zerocopy_crate::BecauseImmutable>()
            }
        })
        .collect::<Vec<_>>();
    // Without invariants, no expression needs the validated pointers. Avoid
    // introducing bindings that could collide with caller-defined items.
    if ctx.invariant_span.is_none() {
        return Ok(quote! { true #(&& #field_checks.is_ok())* });
    }

    let field_bindings = fields
        .iter()
        .enumerate()
        .map(|(idx, _)| ctx.fresh_ident(&format!("field_{}", idx)))
        .collect::<Vec<_>>();
    let bound_fields =
        fields.iter().copied().zip(field_bindings.iter().cloned()).collect::<Vec<_>>();
    let field_validations = fields
        .iter()
        .enumerate()
        .map(|(idx, field)| {
            let invariants = crate::invariant::parse(&field.attrs)?;
            let invariants = invariants
                .iter()
                .map(|expression| crate::invariant::bind_fields(&bound_fields[..=idx], expression));
            Ok(quote! {
                #(
                    // Keep `return` in an invariant from bypassing later fields.
                    if !{ #[inline(always)] || -> #core::primitive::bool { #invariants } }() {
                        return false;
                    }
                )*
            })
        })
        .collect::<Result<Vec<_>, Error>>()?;
    Ok(quote! {
        {
            #(
                // Retain each shared pointer for subsequent field invariants.
                #[allow(unused_variables)]
                let #field_bindings = match #field_checks {
                    #core::result::Result::Ok(#field_bindings) => #field_bindings,
                    #core::result::Result::Err(_) => return false,
                };
                #field_validations
            )*
            true
        }
    })
}

/// Generates an implementation of `is_safe` for an arbitrary enum.
///
/// For an enum with fields, [`derive_enum`] generates the representation model
/// and projection impls. This function reads the tag, matches it against the
/// enum's discriminants, and validates each field of the selected variant
/// through those projections. A fieldless enum needs only the generated tag
/// enum and discriminant constants.
pub(crate) fn derive_is_safe(
    ctx: &Ctx,
    data: &DataEnum,
    repr: &EnumRepr,
) -> Result<TokenStream, Error> {
    if !(repr.is_c() || repr.is_primitive()) {
        return Err(Error::new(
            ctx.ast.span(),
            "must have #[repr(C)] or #[repr(Int)] attribute in order to guarantee this type's memory layout",
        ));
    }

    let zerocopy_crate = &ctx.zerocopy_crate;
    let core = ctx.core_path();
    let candidate = candidate_ident(ctx);
    let alignment = ctx.fresh_ident("___ZcAlignment");
    let tag = Ident::new("tag", Span::mixed_site());
    let projections = if data.fields().is_empty() {
        let tag_enum = generate_tag_enum(ctx, repr, data);
        let tag_consts = generate_tag_consts(data);
        quote! {
            #tag_enum

            type ___ZerocopyTagPrimitive = #zerocopy_crate::util::macro_util::SizeToTag<
                { #core::mem::size_of::<___ZerocopyTag>() },
            >;

            #tag_consts
        }
    } else {
        derive_enum(ctx, data, Client::TryFromBytesDerive)?
    };

    let match_arms = data
        .variants
        .iter()
        .enumerate()
        .map(|(idx, variant)| {
            let name = &variant.ident;
            let variant_id = quote! { #zerocopy_crate::ident_id!(#name) };
            let fields_is_safe = derive_variant_is_safe(ctx, &variant_id, &variant.fields)?;
            let pattern = if ctx.invariant_span.is_some() {
                quote! { #core::option::Option::Some(#idx) }
            } else {
                tag_ident(name).to_token_stream()
            };
            Ok(quote! {
                #pattern => { #fields_is_safe }
            })
        })
        .collect::<Result<Vec<_>, Error>>()?;

    let read_tag = quote! {
        let #tag = #candidate
            .reborrow()
            .cast::<
                ___ZerocopyTagPrimitive,
                #zerocopy_crate::pointer::cast::CastSized,
                (#zerocopy_crate::pointer::BecauseRead, _),
            >()
            .recall_validity::<_, (_, (_, _))>()
            .read::<#zerocopy_crate::BecauseImmutable>();
    };
    let tag_init = if ctx.invariant_span.is_some() {
        let allow = crate::util::allow_generated_code();
        let tag_arms = data.variants.iter().enumerate().map(|(idx, variant)| {
            let tag = tag_ident(&variant.ident);
            quote! { #tag => #core::option::Option::Some(#idx) }
        });
        quote! {
            // Keep implementation-only items and their lint allowances out
            // of the scopes containing caller-authored invariant expressions.
            #allow
            let #tag = {
                #projections
                #read_tag
                match #tag {
                    #(#tag_arms,)*
                    _ => #core::option::Option::None,
                }
            };
        }
    } else {
        quote! { #projections #read_tag }
    };

    Ok(quote! {
        // SAFETY: We use `is_safe` to validate that the bit pattern of the
        // enum's tag corresponds to one of the enum's discriminants. Then, we
        // check the bit validity of each field of the corresponding variant.
        // Thus, this is a sound implementation of `is_safe`.
        #[inline]
        fn is_safe<#alignment>(
            mut #candidate: #zerocopy_crate::Maybe<'_, Self, #alignment>,
        ) -> #core::primitive::bool
        where
            #alignment: #zerocopy_crate::invariant::Alignment,
        {
            #tag_init

            match #tag {
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
        Data::Union(unn) => derive_try_from_bytes_union(ctx, unn, top_level),
    }
}
fn derive_try_from_bytes_struct(
    ctx: &Ctx,
    strct: &DataStruct,
    top_level: Trait,
) -> Result<TokenStream, Error> {
    let extras = if let Some(extras) = try_gen_trivial_is_safe(ctx, top_level) {
        extras
    } else {
        let zerocopy_crate = &ctx.zerocopy_crate;
        let variant_id = quote! { #zerocopy_crate::STRUCT_VARIANT_ID };
        let fields_is_safe = derive_variant_is_safe(ctx, &variant_id, &strct.fields)?;
        let core = ctx.core_path();
        let candidate = candidate_ident(ctx);
        let alignment = ctx.fresh_ident("___ZcAlignment");
        quote!(
            // SAFETY: We use `is_safe` to validate that each field is bit-valid,
            // and only return `true` if all of them are. The bit validity of a
            // struct is just the composition of the bit validities of its
            // fields, so this is a sound implementation of `is_safe`.
            #[inline]
            fn is_safe<#alignment>(
                mut #candidate: #zerocopy_crate::Maybe<'_, Self, #alignment>,
            ) -> #core::primitive::bool
            where
                #alignment: #zerocopy_crate::invariant::Alignment,
            {
                #fields_is_safe
            }
        )
    };
    Ok(ImplBlockBuilder::new(ctx, strct, Trait::TryFromBytes, FieldBounds::ALL_SELF)
        .inner_extras(extras)
        .outer_extras(derive_projection_struct_union(ctx, strct, Client::TryFromBytesDerive))
        .build())
}
fn derive_try_from_bytes_union(
    ctx: &Ctx,
    unn: &DataUnion,
    top_level: Trait,
) -> Result<TokenStream, Error> {
    let field_type_trait_bounds = FieldBounds::All(&[TraitBound::Slf]);

    let zerocopy_crate = &ctx.zerocopy_crate;
    let union_variant_id = struct_union_variant_id(ctx).to_token_stream();
    let extras = if let Some(extras) = try_gen_trivial_is_safe(ctx, top_level) {
        extras
    } else {
        let fields_is_safe = unn
            .fields
            .named
            .iter()
            .map(|field| derive_variant_is_safe(ctx, &union_variant_id, [field]))
            .collect::<Result<Vec<_>, _>>()?;
        let core = ctx.core_path();
        let candidate = candidate_ident(ctx);
        let alignment = ctx.fresh_ident("___ZcAlignment");
        quote!(
            // SAFETY: We use `is_safe` to validate that any field is bit-valid;
            // we only return `true` if at least one of them is and its
            // invariants also hold. The bit validity of a union is not yet
            // well defined in Rust, but it is guaranteed to be no more strict
            // than this definition. See #696 for a more in-depth discussion.
            #[inline]
            fn is_safe<#alignment>(
                mut #candidate: #zerocopy_crate::Maybe<'_, Self, #alignment>,
            ) -> #core::primitive::bool
            where
                #alignment: #zerocopy_crate::invariant::Alignment,
            {
                // Keep each field's bindings and early returns local to that
                // field, so a failure lets validation try the next field.
                false #(|| { #[inline(always)] || { #fields_is_safe } }())*
            }
        )
    };
    Ok(ImplBlockBuilder::new(ctx, unn, Trait::TryFromBytes, field_type_trait_bounds)
        .inner_extras(extras)
        .outer_extras(derive_projection_struct_union(ctx, unn, Client::TryFromBytesDerive))
        .build())
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

    let trivial_is_safe = try_gen_trivial_is_safe(ctx, top_level);
    let extra = match (trivial_is_safe, could_be_from_bytes) {
        (Some(is_safe), _) => is_safe,
        // SAFETY: It would be sound for the enum to implement `FromBytes`, as
        // required by `gen_trivial_is_safe_unchecked`.
        (None, true) => unsafe { gen_trivial_is_safe_unchecked(ctx) },
        (None, false) => match derive_is_safe(ctx, enm, &repr) {
            Ok(extra) => extra,
            Err(e) => return ctx.error_or_skip(e),
        },
    };

    Ok(ImplBlockBuilder::new(ctx, enm, Trait::TryFromBytes, FieldBounds::ALL_SELF)
        .inner_extras(extra)
        .build())
}
fn try_gen_trivial_is_safe(ctx: &Ctx, top_level: Trait) -> Option<proc_macro2::TokenStream> {
    // If the top-level trait is `FromBytes` and `Self` has no type parameters,
    // then the `FromBytes` derive will fail compilation if `Self` is not
    // actually soundly `FromBytes`, and so we can rely on that for our `is_safe`
    // impl. It's plausible that we could make changes - or Rust could make
    // changes (such as the "trivial bounds" language feature) - that make this
    // no longer true. To hedge against these, we include an explicit `Self:
    // FromBytes` check in the generated `is_safe`, which is bulletproof.
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
            fn is_safe<___ZcAlignment>(
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
unsafe fn gen_trivial_is_safe_unchecked(ctx: &Ctx) -> proc_macro2::TokenStream {
    let zerocopy_crate = &ctx.zerocopy_crate;
    let core = ctx.core_path();
    quote!(
        // SAFETY: The caller of `gen_trivial_is_safe_unchecked` has promised
        // that all initialized bit patterns are valid for `Self`.
        #[inline(always)]
        fn is_safe<___ZcAlignment>(
            _candidate: #zerocopy_crate::Maybe<'_, Self, ___ZcAlignment>,
        ) -> #core::primitive::bool
        where
            ___ZcAlignment: #zerocopy_crate::invariant::Alignment,
        {
            true
        }
    )
}

#[cfg(test)]
mod tests {
    use syn::{parse_quote, DeriveInput};

    use super::*;

    fn derive_error(input: DeriveInput) -> String {
        let ctx = Ctx::try_from_derive_input(input).unwrap();
        match derive_try_from_bytes(&ctx, Trait::TryFromBytes) {
            Ok(_) => panic!("expected derive to reject conflicting representations"),
            Err(error) => error.to_string(),
        }
    }

    #[test]
    fn raw_repr_conflict_matches_ordinary_repr() {
        let ordinary = derive_error(parse_quote! {
            #[repr(C, align(8))]
            #[repr(u8)]
            enum Packet {
                Value(core::num::NonZeroU8),
                End,
            }
        });
        let raw = derive_error(parse_quote! {
            #[repr(C, align(8))]
            #[r#repr(r#u8)]
            enum Packet {
                Value(core::num::NonZeroU8),
                End,
            }
        });

        // Rejecting both spellings prevents generating a validator whose
        // synthetic enum layout disagrees with the compiler's actual layout.
        assert_eq!(ordinary, "this conflicts with another representation hint");
        assert_eq!(ordinary, raw);
    }
}
