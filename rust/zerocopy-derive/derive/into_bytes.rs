// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT
//
use proc_macro2::{Span, TokenStream};
use quote::quote;
use syn::{parse_quote, Data, DataEnum, DataStruct, DataUnion, Error, Ident, Type, WherePredicate};

use crate::{
    repr::{EnumRepr, StructUnionRepr},
    util::{
        generate_tag_enum, Ctx, DataExt, FieldBounds, ImplBlockBuilder, PaddingCheck, Trait,
        TraitBound,
    },
};
pub(crate) fn derive_into_bytes(ctx: &Ctx, _top_level: Trait) -> Result<TokenStream, Error> {
    match &ctx.ast.data {
        Data::Struct(strct) => derive_into_bytes_struct(ctx, strct),
        Data::Enum(enm) => derive_into_bytes_enum(ctx, enm),
        Data::Union(unn) => derive_into_bytes_union(ctx, unn),
    }
}

/// If every field is exactly `T`, `[T; _]`, or a final `[T]` for the same type
/// parameter `T`, returns the bounds required to prove this to rustc.
fn homogeneous_field_bounds(ctx: &Ctx, strct: &DataStruct) -> Option<Vec<WherePredicate>> {
    fn strip_parens_and_groups(mut ty: &Type) -> &Type {
        loop {
            ty = match ty {
                Type::Group(group) => &group.elem,
                Type::Paren(paren) => &paren.elem,
                ty => return ty,
            };
        }
    }

    fn type_is_parameter(ty: &Type, parameter: &Ident) -> bool {
        match strip_parens_and_groups(ty) {
            Type::Path(path) => path.qself.is_none() && path.path.is_ident(parameter),
            _ => false,
        }
    }

    let fields = strct.fields();
    ctx.ast.generics.type_params().find_map(|parameter| {
        let parameter = &parameter.ident;
        let elements = fields
            .iter()
            .enumerate()
            .map(|(index, (_, _, ty))| match strip_parens_and_groups(ty) {
                Type::Array(array) => Some(&*array.elem),
                Type::Slice(slice) if index + 1 == fields.len() => Some(&*slice.elem),
                Type::Slice(_) => None,
                ty => Some(ty),
            })
            .collect::<Option<Vec<_>>>()?;

        if !elements.iter().all(|element| type_is_parameter(element, parameter)) {
            return None;
        }

        let zerocopy_crate = &ctx.zerocopy_crate;
        let mut bounds = fields
            .iter()
            .map(|(_, _, ty)| parse_quote!(#ty: #zerocopy_crate::IntoBytes))
            .collect::<Vec<WherePredicate>>();

        // `Ident` equality is only a syntactic prefilter above. In
        // macro-generated input, two identifiers can both print as `T` and
        // compare equal here while having different hygiene contexts. For
        // example, a call-site `T` could resolve to a `u8` type alias while a
        // def-site `T` resolves to this struct's type parameter. Instantiating
        // that parameter with `u16` would make the fields actually be `u8` and
        // `u16`; treating them as homogeneous could overlook padding and cause
        // us to emit an unsound `IntoBytes` impl.
        //
        // Quote every element type from its original field AST so that its
        // hygiene context is retained, then use `Identity` to have rustc verify
        // that it resolves to `parameter`. Do not deduplicate these predicates:
        // identically-printed types may have different hygiene contexts and
        // must each be checked by rustc.
        bounds.extend(elements.into_iter().map(|element| {
            parse_quote! {
                #element: #zerocopy_crate::util::macro_util::Identity<Type = #parameter>
            }
        }));
        Some(bounds)
    })
}

fn derive_into_bytes_struct(ctx: &Ctx, strct: &DataStruct) -> Result<TokenStream, Error> {
    let repr = StructUnionRepr::from_attrs(&ctx.ast.attrs)?;

    let is_transparent = repr.is_transparent();
    let is_c = repr.is_c();
    let is_packed_1 = repr.is_packed_1();
    let num_fields = strct.fields().len();
    let mut homogeneous_bounds =
        if is_c && !repr.is_align_gt_1() { homogeneous_field_bounds(ctx, strct) } else { None };

    let (padding_check, require_unaligned_fields, explicit_field_bounds) = if is_transparent
        || is_packed_1
    {
        // No padding check needed.
        // - repr(transparent): The layout and ABI of the whole struct is the
        //   same as its only non-ZST field (meaning there's no padding outside
        //   of that field) and we require that field to be `IntoBytes` (meaning
        //   there's no padding in that field).
        // - repr(packed): Any inter-field padding bytes are removed, meaning
        //   that any padding bytes would need to come from the fields, all of
        //   which we require to be `IntoBytes` (meaning they don't have any
        //   padding). Note that this holds regardless of other `repr`
        //   attributes, including `repr(Rust)`. [1]
        //
        // [1] Per https://doc.rust-lang.org/1.81.0/reference/type-layout.html#the-alignment-modifiers:
        //
        //   An important consequence of these rules is that a type with
        //   `#[repr(packed(1))]`` (or `#[repr(packed)]``) will have no
        //   inter-field padding.
        (None, false, None)
    } else if is_c && !repr.is_align_gt_1() && num_fields <= 1 {
        // No padding check needed. A repr(C) struct with zero or one field has
        // no padding unless #[repr(align)] explicitly adds padding, which we
        // check for in this branch's condition.
        (None, false, None)
    } else if ctx.ast.generics.params.is_empty() {
        // Is the last field a syntactic slice, i.e., `[SomeType]`.
        let is_syntactic_dst =
            strct.fields().last().map(|(_, _, ty)| matches!(ty, Type::Slice(_))).unwrap_or(false);
        // Since there are no generics, we can emit a padding check. All reprs
        // guarantee that fields won't overlap [1], so the padding check is
        // sound. This is more permissive than the next case, which requires
        // that all field types implement `Unaligned`.
        //
        // [1] Per https://doc.rust-lang.org/1.81.0/reference/type-layout.html#the-rust-representation:
        //
        //   The only data layout guarantees made by [`repr(Rust)`] are those
        //   required for soundness. They are:
        //   ...
        //   2. The fields do not overlap.
        //   ...
        if is_c && is_syntactic_dst {
            (Some(PaddingCheck::ReprCStruct), false, None)
        } else {
            (Some(PaddingCheck::Struct), false, None)
        }
    } else if let Some(bounds) = homogeneous_bounds.take() {
        // Let `a` be the alignment of `T` and `s` be its size. Rust guarantees
        // that `s` is a multiple of `a`. `T`, `[T; N]`, and `[T]` all have
        // alignment `a`, and their sizes are `s`, `N * s`, and `len * s`,
        // respectively.
        //
        // Without a `packed` modifier, each field and the struct have
        // alignment `a`. `align(1)` does not change that because every Rust
        // type already has alignment at least 1. With `packed(P)`, each field
        // and the struct instead have alignment `b = min(a, P)`. Rust
        // alignments and valid values of `P` are powers of two, so `b` divides
        // `a`; every field size is therefore also a multiple of `b`.
        //
        // Thus, after placing any field, the running offset is already aligned
        // for the next field, and the final offset is already aligned for the
        // struct. A repr(C) without an `align` modifier greater than 1
        // therefore introduces neither inter-field nor trailing padding,
        // including for a slice DST. The bounds returned by
        // `homogeneous_field_bounds` require rustc to verify that the element
        // type of every field is the same type `T`.
        (None, false, Some(bounds))
    } else if is_c && !repr.is_align_gt_1() {
        // We can't use a padding check since there are generic type arguments.
        // Instead, we require all field types to implement `Unaligned`. This
        // ensures that the `repr(C)` layout algorithm will not insert any
        // padding unless #[repr(align)] explicitly adds padding, which we check
        // for in this branch's condition.
        //
        // FIXME(#10): Support type parameters for non-transparent, non-packed
        // structs without requiring `Unaligned`.
        (None, true, None)
    } else {
        return ctx.error_or_skip(Error::new(
            Span::call_site(),
            "must have a non-align #[repr(...)] attribute in order to guarantee this type's memory layout",
        ));
    };

    let field_bounds = if let Some(bounds) = explicit_field_bounds {
        FieldBounds::Explicit(bounds)
    } else if require_unaligned_fields {
        FieldBounds::All(&[TraitBound::Slf, TraitBound::Other(Trait::Unaligned)])
    } else {
        FieldBounds::ALL_SELF
    };

    Ok(ImplBlockBuilder::new(ctx, strct, Trait::IntoBytes, field_bounds)
        .padding_check(padding_check)
        .build())
}

fn derive_into_bytes_enum(ctx: &Ctx, enm: &DataEnum) -> Result<TokenStream, Error> {
    let repr = EnumRepr::from_attrs(&ctx.ast.attrs)?;
    if !repr.is_c() && !repr.is_primitive() {
        return ctx.error_or_skip(Error::new(
            Span::call_site(),
            "must have #[repr(C)] or #[repr(Int)] attribute in order to guarantee this type's memory layout",
        ));
    }

    let tag_type_definition = generate_tag_enum(ctx, &repr, enm);
    Ok(ImplBlockBuilder::new(ctx, enm, Trait::IntoBytes, FieldBounds::ALL_SELF)
        .padding_check(PaddingCheck::Enum { tag_type_definition })
        .build())
}

fn derive_into_bytes_union(ctx: &Ctx, unn: &DataUnion) -> Result<TokenStream, Error> {
    // See #1792 for more context.
    //
    // By checking for `zerocopy_derive_union_into_bytes` both here and in the
    // generated code, we ensure that `--cfg zerocopy_derive_union_into_bytes`
    // need only be passed *either* when compiling this crate *or* when
    // compiling the user's crate. The former is preferable, but in some
    // situations (such as when cross-compiling using `cargo build --target`),
    // it doesn't get propagated to this crate's build by default.
    let cfg_compile_error = if cfg!(zerocopy_derive_union_into_bytes) {
        quote!()
    } else {
        let core = ctx.core_path();
        let error_message = "requires --cfg zerocopy_derive_union_into_bytes;
please let us know you use this feature: https://github.com/google/zerocopy/discussions/1802";
        quote!(
            #[allow(unused_attributes, unexpected_cfgs)]
            const _: () = {
                #[cfg(not(zerocopy_derive_union_into_bytes))]
                #core::compile_error!(#error_message);
            };
        )
    };

    // FIXME(#10): Support type parameters.
    if !ctx.ast.generics.params.is_empty() {
        return ctx.error_or_skip(Error::new(
            Span::call_site(),
            "unsupported on types with type parameters",
        ));
    }

    // Because we don't support generics, we don't need to worry about
    // special-casing different reprs. So long as there is *some* repr which
    // guarantees the layout, our `PaddingCheck::Union` guarantees that there is
    // no padding.
    let repr = StructUnionRepr::from_attrs(&ctx.ast.attrs)?;
    if !repr.is_c() && !repr.is_transparent() && !repr.is_packed_1() {
        return ctx.error_or_skip(Error::new(
            Span::call_site(),
            "must be #[repr(C)], #[repr(packed)], or #[repr(transparent)]",
        ));
    }

    let impl_block = ImplBlockBuilder::new(ctx, unn, Trait::IntoBytes, FieldBounds::ALL_SELF)
        .padding_check(PaddingCheck::Union)
        .build();
    Ok(quote!(#cfg_compile_error #impl_block))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn derive(input: syn::DeriveInput) -> TokenStream {
        let ctx = Ctx::try_from_derive_input(input).unwrap();
        derive_into_bytes(&ctx, Trait::IntoBytes).unwrap()
    }

    #[test]
    fn raw_repr_matches_ordinary_repr() {
        let ordinary = derive(parse_quote! {
            #[repr(C)]
            #[repr(align(8))]
            struct Response {
                status: u8,
            }
        });
        let raw = derive(parse_quote! {
            #[repr(C)]
            #[r#repr(r#align(8))]
            struct Response {
                status: u8,
            }
        });

        let ordinary = ordinary.to_string();
        let raw = raw.to_string();

        // In particular, both expansions contain the padding check which
        // prevents the resulting type from implementing `IntoBytes`.
        assert!(ordinary.contains("PaddingFree"));
        assert_eq!(ordinary, raw);
    }
}
