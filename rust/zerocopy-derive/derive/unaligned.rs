// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

use proc_macro2::{Span, TokenStream};
use syn::{Data, DataEnum, DataStruct, DataUnion, Error};

use crate::{
    repr::{EnumRepr, StructUnionRepr},
    util::{Ctx, FieldBounds, ImplBlockBuilder, Trait},
};

pub(crate) fn derive_unaligned(ctx: &Ctx, _top_level: Trait) -> Result<TokenStream, Error> {
    match &ctx.ast.data {
        Data::Struct(strct) => derive_unaligned_struct(ctx, strct),
        Data::Enum(enm) => derive_unaligned_enum(ctx, enm),
        Data::Union(unn) => derive_unaligned_union(ctx, unn),
    }
}

/// A struct is `Unaligned` if:
/// - `repr(align)` is no more than 1 and either
///   - `repr(C)` or `repr(transparent)` and
///     - all fields `Unaligned`
///   - `repr(packed)`
fn derive_unaligned_struct(ctx: &Ctx, strct: &DataStruct) -> Result<TokenStream, Error> {
    let repr = StructUnionRepr::from_attrs(&ctx.ast.attrs)?;
    repr.unaligned_validate_no_align_gt_1()?;

    let field_bounds = if repr.is_packed_1() {
        FieldBounds::None
    } else if repr.is_c() || repr.is_transparent() {
        FieldBounds::ALL_SELF
    } else {
        return ctx.error_or_skip(Error::new(
            Span::call_site(),
            "must have #[repr(C)], #[repr(transparent)], or #[repr(packed)] attribute in order to guarantee this type's alignment",
        ));
    };

    Ok(ImplBlockBuilder::new(ctx, strct, Trait::Unaligned, field_bounds).build())
}

/// An enum is `Unaligned` if:
/// - No `repr(align(N > 1))`
/// - `repr(u8)` or `repr(i8)`
fn derive_unaligned_enum(ctx: &Ctx, enm: &DataEnum) -> Result<TokenStream, Error> {
    let repr = EnumRepr::from_attrs(&ctx.ast.attrs)?;
    repr.unaligned_validate_no_align_gt_1()?;

    if !repr.is_u8() && !repr.is_i8() {
        return ctx.error_or_skip(Error::new(
            Span::call_site(),
            "must have #[repr(u8)] or #[repr(i8)] attribute in order to guarantee this type's alignment",
        ));
    }

    Ok(ImplBlockBuilder::new(ctx, enm, Trait::Unaligned, FieldBounds::ALL_SELF).build())
}

/// Like structs, a union is `Unaligned` if:
/// - `repr(align)` is no more than 1 and either
///   - `repr(C)` or `repr(transparent)` and
///     - all fields `Unaligned`
///   - `repr(packed)`
fn derive_unaligned_union(ctx: &Ctx, unn: &DataUnion) -> Result<TokenStream, Error> {
    let repr = StructUnionRepr::from_attrs(&ctx.ast.attrs)?;
    repr.unaligned_validate_no_align_gt_1()?;

    let field_type_trait_bounds = if repr.is_packed_1() {
        FieldBounds::None
    } else if repr.is_c() || repr.is_transparent() {
        FieldBounds::ALL_SELF
    } else {
        return ctx.error_or_skip(Error::new(
            Span::call_site(),
            "must have #[repr(C)], #[repr(transparent)], or #[repr(packed)] attribute in order to guarantee this type's alignment",
        ));
    };

    Ok(ImplBlockBuilder::new(ctx, unn, Trait::Unaligned, field_type_trait_bounds).build())
}
