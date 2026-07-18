// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

use proc_macro2::TokenStream;
use quote::quote;
use syn::{parse_quote, Data, Error, Type};

use crate::{
    repr::StructUnionRepr,
    util::{Ctx, DataExt, FieldBounds, ImplBlockBuilder, SelfBounds, Trait},
};

fn derive_known_layout_for_repr_c_struct<'a>(
    ctx: &'a Ctx,
    repr: &StructUnionRepr,
    fields: &[(&'a syn::Visibility, TokenStream, &'a Type)],
) -> Option<(SelfBounds<'a>, TokenStream, Option<TokenStream>)> {
    let (trailing_field, leading_fields) = fields.split_last()?;

    let (_vis, trailing_field_name, trailing_field_ty) = trailing_field;
    let leading_fields_tys = leading_fields.iter().map(|(_vis, _name, ty)| ty);

    let core = ctx.core_path();
    let repr_align = repr
        .get_align()
        .map(|align| {
            let align = align.t.get();
            quote!(#core::num::NonZeroUsize::new(#align as usize))
        })
        .unwrap_or_else(|| quote!(#core::option::Option::None));
    let repr_packed = repr
        .get_packed()
        .map(|packed| {
            let packed = packed.get();
            quote!(#core::num::NonZeroUsize::new(#packed as usize))
        })
        .unwrap_or_else(|| quote!(#core::option::Option::None));

    let zerocopy_crate = &ctx.zerocopy_crate;
    let make_methods = |trailing_field_ty| {
        quote! {
            // SAFETY:
            // - The returned pointer has the same address and provenance as
            //   `bytes`:
            //   - The recursive call to `raw_from_ptr_len` preserves both
            //     address and provenance.
            //   - The `as` cast preserves both address and provenance.
            //   - `NonNull::new_unchecked` preserves both address and
            //     provenance.
            // - If `Self` is a slice DST, the returned pointer encodes
            //   `elems` elements in the trailing slice:
            //   - This is true of the recursive call to `raw_from_ptr_len`.
            //   - `trailing.as_ptr() as *mut Self` preserves trailing slice
            //     element count [1].
            //   - `NonNull::new_unchecked` preserves trailing slice element
            //     count.
            //
            // [1] Per https://doc.rust-lang.org/reference/expressions/operator-expr.html#pointer-to-pointer-cast:
            //
            //   `*const T`` / `*mut T` can be cast to `*const U` / `*mut U`
            //   with the following behavior:
            //     ...
            //     - If `T` and `U` are both unsized, the pointer is also
            //       returned unchanged. In particular, the metadata is
            //       preserved exactly.
            //
            //       For instance, a cast from `*const [T]` to `*const [U]`
            //       preserves the number of elements. ... The same holds
            //       for str and any compound type whose unsized tail is a
            //       slice type, such as struct `Foo(i32, [u8])` or
            //       `(u64, Foo)`.
            #[inline(always)]
            fn raw_from_ptr_len(
                bytes: #core::ptr::NonNull<u8>,
                meta: <Self as #zerocopy_crate::KnownLayout>::PointerMetadata,
            ) -> #core::ptr::NonNull<Self> {
                let trailing = <#trailing_field_ty as #zerocopy_crate::KnownLayout>::raw_from_ptr_len(bytes, meta);
                let slf = trailing.as_ptr() as *mut Self;
                // SAFETY: Constructed from `trailing`, which is non-null.
                unsafe { #core::ptr::NonNull::new_unchecked(slf) }
            }

            #[inline(always)]
            fn pointer_to_metadata(ptr: *mut Self) -> <Self as #zerocopy_crate::KnownLayout>::PointerMetadata {
                <#trailing_field_ty>::pointer_to_metadata(ptr as *mut _)
            }
        }
    };

    let inner_extras = {
        let leading_fields_tys = leading_fields_tys.clone();
        let methods = make_methods(*trailing_field_ty);
        let (_, ty_generics, _) = ctx.ast.generics.split_for_impl();

        quote!(
            type PointerMetadata = <#trailing_field_ty as #zerocopy_crate::KnownLayout>::PointerMetadata;

            type MaybeUninit = __ZerocopyKnownLayoutMaybeUninit #ty_generics;

            // SAFETY: `LAYOUT` accurately describes the layout of `Self`.
            // The documentation of `DstLayout::for_repr_c_struct` vows that
            // invocations in this manner will accurately describe a type,
            // so long as:
            //
            //  - that type is `repr(C)`,
            //  - its fields are enumerated in the order they appear,
            //  - the presence of `repr_align` and `repr_packed` are
            //    correctly accounted for.
            //
            // We respect all three of these preconditions here. This
            // expansion is only used if `is_repr_c_struct`, we enumerate
            // the fields in order, and we extract the values of `align(N)`
            // and `packed(N)`.
            const LAYOUT: #zerocopy_crate::DstLayout = #zerocopy_crate::DstLayout::for_repr_c_struct(
                #repr_align,
                #repr_packed,
                &[
                    #(#zerocopy_crate::DstLayout::for_type::<#leading_fields_tys>(),)*
                    <#trailing_field_ty as #zerocopy_crate::KnownLayout>::LAYOUT
                ],
            );

            #methods
        )
    };

    let outer_extras = {
        let ident = &ctx.ast.ident;
        let vis = &ctx.ast.vis;
        let params = &ctx.ast.generics.params;
        let (impl_generics, ty_generics, where_clause) = ctx.ast.generics.split_for_impl();

        let predicates = if let Some(where_clause) = where_clause {
            where_clause.predicates.clone()
        } else {
            Default::default()
        };

        // Generate a valid ident for a type-level handle to a field of a
        // given `name`.
        let field_index = |name: &TokenStream| ident!(("__Zerocopy_Field_{}", name), ident.span());

        let field_indices: Vec<_> =
            fields.iter().map(|(_vis, name, _ty)| field_index(name)).collect();

        // Define the collection of type-level field handles.
        let field_defs = field_indices.iter().zip(fields).map(|(idx, (vis, _, _))| {
            quote! {
                #vis struct #idx;
            }
        });

        let field_impls = field_indices.iter().zip(fields).map(|(idx, (_, _, ty))| quote! {
            // SAFETY: `#ty` is the type of `#ident`'s field at `#idx`.
            //
            // We implement `Field` for each field of the struct to create a
            // projection from the field index to its type. This allows us
            // to refer to the field's type in a way that respects `Self`
            // hygiene. If we just copy-pasted the tokens of `#ty`, we
            // would not respect `Self` hygiene, as `Self` would refer to
            // the helper struct we are generating, not the derive target
            // type.
            unsafe impl #impl_generics #zerocopy_crate::util::macro_util::Field<#idx> for #ident #ty_generics
            where
                #predicates
            {
                type Type = #ty;
            }
        });

        let trailing_field_index = field_index(trailing_field_name);
        let leading_field_indices =
            leading_fields.iter().map(|(_vis, name, _ty)| field_index(name));

        // We use `Field` to project the type of the trailing field. This is
        // required to ensure that if the field type uses `Self`, it
        // resolves to the derive target type, not the helper struct we are
        // generating.
        let trailing_field_ty = quote! {
            <#ident #ty_generics as
                #zerocopy_crate::util::macro_util::Field<#trailing_field_index>
            >::Type
        };

        let methods = make_methods(&parse_quote! {
            <#trailing_field_ty as #zerocopy_crate::KnownLayout>::MaybeUninit
        });

        let core = ctx.core_path();

        quote! {
            #(#field_defs)*

            #(#field_impls)*

            // SAFETY: This has the same layout as the derive target type,
            // except that it admits uninit bytes. This is ensured by using
            // the same repr as the target type, and by using field types
            // which have the same layout as the target type's fields,
            // except that they admit uninit bytes. We indirect through
            // `Field` to ensure that occurrences of `Self` resolve to
            // `#ty`, not `__ZerocopyKnownLayoutMaybeUninit` (see #2116).
            #repr
            #[doc(hidden)]
            #vis struct __ZerocopyKnownLayoutMaybeUninit<#params> (
                #(#core::mem::MaybeUninit<
                    <#ident #ty_generics as
                        #zerocopy_crate::util::macro_util::Field<#leading_field_indices>
                    >::Type
                >,)*
                // NOTE(#2302): We wrap in `ManuallyDrop` here in case the
                // type we're operating on is both generic and
                // `repr(packed)`. In that case, Rust needs to know that the
                // type is *either* `Sized` or has a trivial `Drop`.
                // `ManuallyDrop` has a trivial `Drop`, and so satisfies
                // this requirement.
                #core::mem::ManuallyDrop<
                    <#trailing_field_ty as #zerocopy_crate::KnownLayout>::MaybeUninit
                >
            )
            where
                #trailing_field_ty: #zerocopy_crate::KnownLayout,
                #predicates;

            // SAFETY: We largely defer to the `KnownLayout` implementation
            // on the derive target type (both by using the same tokens, and
            // by deferring to impl via type-level indirection). This is
            // sound, since `__ZerocopyKnownLayoutMaybeUninit` is guaranteed
            // to have the same layout as the derive target type, except
            // that `__ZerocopyKnownLayoutMaybeUninit` admits uninit bytes.
            unsafe impl #impl_generics #zerocopy_crate::KnownLayout for __ZerocopyKnownLayoutMaybeUninit #ty_generics
            where
                #trailing_field_ty: #zerocopy_crate::KnownLayout,
                #predicates
            {
                fn only_derive_is_allowed_to_implement_this_trait() {}

                type PointerMetadata = <#ident #ty_generics as #zerocopy_crate::KnownLayout>::PointerMetadata;

                type MaybeUninit = Self;

                const LAYOUT: #zerocopy_crate::DstLayout = <#ident #ty_generics as #zerocopy_crate::KnownLayout>::LAYOUT;

                #methods
            }
        }
    };

    Some((SelfBounds::None, inner_extras, Some(outer_extras)))
}

pub(crate) fn derive(ctx: &Ctx, _top_level: Trait) -> Result<TokenStream, Error> {
    // If this is a `repr(C)` struct, then `c_struct_repr` contains the entire
    // `repr` attribute.
    let c_struct_repr = match &ctx.ast.data {
        Data::Struct(..) => {
            let repr = StructUnionRepr::from_attrs(&ctx.ast.attrs)?;
            if repr.is_c() {
                Some(repr)
            } else {
                None
            }
        }
        Data::Enum(..) | Data::Union(..) => None,
    };

    let fields = ctx.ast.data.fields();

    let (self_bounds, inner_extras, outer_extras) = c_struct_repr
        .as_ref()
        .and_then(|repr| {
            derive_known_layout_for_repr_c_struct(ctx, repr, &fields)
        })
        .unwrap_or_else(|| {
            let zerocopy_crate = &ctx.zerocopy_crate;
            let core = ctx.core_path();

            // For enums, unions, and non-`repr(C)` structs, we require that
            // `Self` is sized, and as a result don't need to reason about the
            // internals of the type.
            (
                SelfBounds::SIZED,
                quote!(
                    type PointerMetadata = ();
                    type MaybeUninit =
                        #core::mem::MaybeUninit<Self>;

                    // SAFETY: `LAYOUT` is guaranteed to accurately describe the
                    // layout of `Self`, because that is the documented safety
                    // contract of `DstLayout::for_type`.
                    const LAYOUT: #zerocopy_crate::DstLayout = #zerocopy_crate::DstLayout::for_type::<Self>();

                    // SAFETY: `.cast` preserves address and provenance.
                    //
                    // FIXME(#429): Add documentation to `.cast` that promises that
                    // it preserves provenance.
                    #[inline(always)]
                    fn raw_from_ptr_len(bytes: #core::ptr::NonNull<u8>, _meta: ()) -> #core::ptr::NonNull<Self> {
                        bytes.cast::<Self>()
                    }

                    #[inline(always)]
                    fn pointer_to_metadata(_ptr: *mut Self) -> () {}
                ),
                None,
            )
        });
    Ok(match &ctx.ast.data {
        Data::Struct(strct) => {
            let require_trait_bound_on_field_types =
                if matches!(self_bounds, SelfBounds::All(&[Trait::Sized])) {
                    FieldBounds::None
                } else {
                    FieldBounds::TRAILING_SELF
                };

            // A bound on the trailing field is required, since structs are
            // unsized if their trailing field is unsized. Reflecting the layout
            // of an usized trailing field requires that the field is
            // `KnownLayout`.
            ImplBlockBuilder::new(
                ctx,
                strct,
                Trait::KnownLayout,
                require_trait_bound_on_field_types,
            )
            .self_type_trait_bounds(self_bounds)
            .inner_extras(inner_extras)
            .outer_extras(outer_extras)
            .build()
        }
        Data::Enum(enm) => {
            // A bound on the trailing field is not required, since enums cannot
            // currently be unsized.
            ImplBlockBuilder::new(ctx, enm, Trait::KnownLayout, FieldBounds::None)
                .self_type_trait_bounds(SelfBounds::SIZED)
                .inner_extras(inner_extras)
                .outer_extras(outer_extras)
                .build()
        }
        Data::Union(unn) => {
            // A bound on the trailing field is not required, since unions
            // cannot currently be unsized.
            ImplBlockBuilder::new(ctx, unn, Trait::KnownLayout, FieldBounds::None)
                .self_type_trait_bounds(SelfBounds::SIZED)
                .inner_extras(inner_extras)
                .outer_extras(outer_extras)
                .build()
        }
    })
}
