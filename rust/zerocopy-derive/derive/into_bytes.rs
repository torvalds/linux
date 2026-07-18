// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

use proc_macro2::{Span, TokenStream};
use quote::quote;
use syn::{Data, DataEnum, DataStruct, DataUnion, Error, Type};

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
fn derive_into_bytes_struct(ctx: &Ctx, strct: &DataStruct) -> Result<TokenStream, Error> {
    let repr = StructUnionRepr::from_attrs(&ctx.ast.attrs)?;

    let is_transparent = repr.is_transparent();
    let is_c = repr.is_c();
    let is_packed_1 = repr.is_packed_1();
    let num_fields = strct.fields().len();

    let (padding_check, require_unaligned_fields) = if is_transparent || is_packed_1 {
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
        (None, false)
    } else if is_c && !repr.is_align_gt_1() && num_fields <= 1 {
        // No padding check needed. A repr(C) struct with zero or one field has
        // no padding unless #[repr(align)] explicitly adds padding, which we
        // check for in this branch's condition.
        (None, false)
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
            (Some(PaddingCheck::ReprCStruct), false)
        } else {
            (Some(PaddingCheck::Struct), false)
        }
    } else if is_c && !repr.is_align_gt_1() {
        // We can't use a padding check since there are generic type arguments.
        // Instead, we require all field types to implement `Unaligned`. This
        // ensures that the `repr(C)` layout algorithm will not insert any
        // padding unless #[repr(align)] explicitly adds padding, which we check
        // for in this branch's condition.
        //
        // FIXME(#10): Support type parameters for non-transparent, non-packed
        // structs without requiring `Unaligned`.
        (None, true)
    } else {
        return ctx.error_or_skip(Error::new(
            Span::call_site(),
            "must have a non-align #[repr(...)] attribute in order to guarantee this type's memory layout",
        ));
    };

    let field_bounds = if require_unaligned_fields {
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
