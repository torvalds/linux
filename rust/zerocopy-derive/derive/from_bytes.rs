// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

use proc_macro2::{Span, TokenStream};
use syn::{
    parse_quote, Data, DataEnum, DataStruct, DataUnion, Error, Expr, ExprLit, ExprUnary, Lit, UnOp,
    WherePredicate,
};

use crate::{
    derive::try_from_bytes::derive_try_from_bytes,
    repr::{CompoundRepr, EnumRepr, Repr, Spanned},
    util::{enum_size_from_repr, Ctx, FieldBounds, ImplBlockBuilder, Trait, TraitBound},
};
/// Returns `Ok(index)` if variant `index` of the enum has a discriminant of
/// zero. If `Err(bool)` is returned, the boolean is true if the enum has
/// unknown discriminants (e.g. discriminants set to const expressions which we
/// can't evaluate in a proc macro). If the enum has unknown discriminants, then
/// it might have a zero variant that we just can't detect.
pub(crate) fn find_zero_variant(enm: &DataEnum) -> Result<usize, bool> {
    // Discriminants can be anywhere in the range [i128::MIN, u128::MAX] because
    // the discriminant type may be signed or unsigned. Since we only care about
    // tracking the discriminant when it's less than or equal to zero, we can
    // avoid u128 -> i128 conversions and bounds checking by making the "next
    // discriminant" value implicitly negative.
    // Technically 64 bits is enough, but 128 is better for future compatibility
    // with https://github.com/rust-lang/rust/issues/56071
    let mut next_negative_discriminant = Some(0);

    // Sometimes we encounter explicit discriminants that we can't know the
    // value of (e.g. a constant expression that requires evaluation). These
    // could evaluate to zero or a negative number, but we can't assume that
    // they do (no false positives allowed!). So we treat them like strictly-
    // positive values that can't result in any zero variants, and track whether
    // we've encountered any unknown discriminants.
    let mut has_unknown_discriminants = false;

    for (i, v) in enm.variants.iter().enumerate() {
        match v.discriminant.as_ref() {
            // Implicit discriminant
            None => {
                match next_negative_discriminant.as_mut() {
                    Some(0) => return Ok(i),
                    // n is nonzero so subtraction is always safe
                    Some(n) => *n -= 1,
                    None => (),
                }
            }
            // Explicit positive discriminant
            Some((_, Expr::Lit(ExprLit { lit: Lit::Int(int), .. }))) => {
                match int.base10_parse::<u128>().ok() {
                    Some(0) => return Ok(i),
                    Some(_) => next_negative_discriminant = None,
                    None => {
                        // Numbers should never fail to parse, but just in case:
                        has_unknown_discriminants = true;
                        next_negative_discriminant = None;
                    }
                }
            }
            // Explicit negative discriminant
            Some((_, Expr::Unary(ExprUnary { op: UnOp::Neg(_), expr, .. }))) => match &**expr {
                Expr::Lit(ExprLit { lit: Lit::Int(int), .. }) => {
                    match int.base10_parse::<u128>().ok() {
                        Some(0) => return Ok(i),
                        // x is nonzero so subtraction is always safe
                        Some(x) => next_negative_discriminant = Some(x - 1),
                        None => {
                            // Numbers should never fail to parse, but just in
                            // case:
                            has_unknown_discriminants = true;
                            next_negative_discriminant = None;
                        }
                    }
                }
                // Unknown negative discriminant (e.g. const repr)
                _ => {
                    has_unknown_discriminants = true;
                    next_negative_discriminant = None;
                }
            },
            // Unknown discriminant (e.g. const expr)
            _ => {
                has_unknown_discriminants = true;
                next_negative_discriminant = None;
            }
        }
    }

    Err(has_unknown_discriminants)
}
pub(crate) fn derive_from_zeros(ctx: &Ctx, top_level: Trait) -> Result<TokenStream, Error> {
    let try_from_bytes = derive_try_from_bytes(ctx, top_level)?;
    let from_zeros = match &ctx.ast.data {
        Data::Struct(strct) => derive_from_zeros_struct(ctx, strct),
        Data::Enum(enm) => derive_from_zeros_enum(ctx, enm)?,
        Data::Union(unn) => derive_from_zeros_union(ctx, unn),
    };
    Ok(IntoIterator::into_iter([try_from_bytes, from_zeros]).collect())
}
pub(crate) fn derive_from_bytes(ctx: &Ctx, top_level: Trait) -> Result<TokenStream, Error> {
    let from_zeros = derive_from_zeros(ctx, top_level)?;
    let from_bytes = match &ctx.ast.data {
        Data::Struct(strct) => derive_from_bytes_struct(ctx, strct),
        Data::Enum(enm) => derive_from_bytes_enum(ctx, enm)?,
        Data::Union(unn) => derive_from_bytes_union(ctx, unn),
    };

    Ok(IntoIterator::into_iter([from_zeros, from_bytes]).collect())
}
fn derive_from_zeros_struct(ctx: &Ctx, strct: &DataStruct) -> TokenStream {
    ImplBlockBuilder::new(ctx, strct, Trait::FromZeros, FieldBounds::ALL_SELF).build()
}
fn derive_from_zeros_enum(ctx: &Ctx, enm: &DataEnum) -> Result<TokenStream, Error> {
    let repr = EnumRepr::from_attrs(&ctx.ast.attrs)?;

    // We don't actually care what the repr is; we just care that it's one of
    // the allowed ones.
    match repr {
        Repr::Compound(Spanned { t: CompoundRepr::C | CompoundRepr::Primitive(_), span: _ }, _) => {
        }
        Repr::Transparent(_) | Repr::Compound(Spanned { t: CompoundRepr::Rust, span: _ }, _) => {
            return ctx.error_or_skip(
                Error::new(
                    Span::call_site(),
                    "must have #[repr(C)] or #[repr(Int)] attribute in order to guarantee this type's memory layout",
                ),
            );
        }
    }

    let zero_variant = match find_zero_variant(enm) {
        Ok(index) => enm.variants.iter().nth(index).unwrap(),
        // Has unknown variants
        Err(true) => {
            return ctx.error_or_skip(Error::new_spanned(
                &ctx.ast,
                "FromZeros only supported on enums with a variant that has a discriminant of `0`\n\
                help: This enum has discriminants which are not literal integers. One of those may \
                define or imply which variant has a discriminant of zero. Use a literal integer to \
                define or imply the variant with a discriminant of zero.",
            ));
        }
        // Does not have unknown variants
        Err(false) => {
            return ctx.error_or_skip(Error::new_spanned(
                &ctx.ast,
                "FromZeros only supported on enums with a variant that has a discriminant of `0`",
            ));
        }
    };

    let zerocopy_crate = &ctx.zerocopy_crate;
    let explicit_bounds = zero_variant
        .fields
        .iter()
        .map(|field| {
            let ty = &field.ty;
            parse_quote! { #ty: #zerocopy_crate::FromZeros }
        })
        .collect::<Vec<WherePredicate>>();

    Ok(ImplBlockBuilder::new(ctx, enm, Trait::FromZeros, FieldBounds::Explicit(explicit_bounds))
        .build())
}
fn derive_from_zeros_union(ctx: &Ctx, unn: &DataUnion) -> TokenStream {
    let field_type_trait_bounds = FieldBounds::All(&[TraitBound::Slf]);
    ImplBlockBuilder::new(ctx, unn, Trait::FromZeros, field_type_trait_bounds).build()
}
fn derive_from_bytes_struct(ctx: &Ctx, strct: &DataStruct) -> TokenStream {
    ImplBlockBuilder::new(ctx, strct, Trait::FromBytes, FieldBounds::ALL_SELF).build()
}
fn derive_from_bytes_enum(ctx: &Ctx, enm: &DataEnum) -> Result<TokenStream, Error> {
    let repr = EnumRepr::from_attrs(&ctx.ast.attrs)?;

    let variants_required = 1usize << enum_size_from_repr(&repr)?;
    if enm.variants.len() != variants_required {
        return ctx.error_or_skip(Error::new_spanned(
            &ctx.ast,
            format!(
                "FromBytes only supported on {} enum with {} variants",
                repr.repr_type_name(),
                variants_required
            ),
        ));
    }

    Ok(ImplBlockBuilder::new(ctx, enm, Trait::FromBytes, FieldBounds::ALL_SELF).build())
}
fn derive_from_bytes_union(ctx: &Ctx, unn: &DataUnion) -> TokenStream {
    let field_type_trait_bounds = FieldBounds::All(&[TraitBound::Slf]);
    ImplBlockBuilder::new(ctx, unn, Trait::FromBytes, field_type_trait_bounds).build()
}
