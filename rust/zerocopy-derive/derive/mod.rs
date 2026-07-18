// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

pub mod from_bytes;
pub mod into_bytes;
pub mod known_layout;
pub mod try_from_bytes;
pub mod unaligned;

use proc_macro2::{Span, TokenStream};
use quote::quote;
use syn::{Data, Error};

use crate::{
    repr::StructUnionRepr,
    util::{Ctx, DataExt, FieldBounds, ImplBlockBuilder, Trait},
};

pub(crate) fn derive_immutable(ctx: &Ctx, _top_level: Trait) -> TokenStream {
    match &ctx.ast.data {
        Data::Struct(strct) => {
            ImplBlockBuilder::new(ctx, strct, Trait::Immutable, FieldBounds::ALL_SELF).build()
        }
        Data::Enum(enm) => {
            ImplBlockBuilder::new(ctx, enm, Trait::Immutable, FieldBounds::ALL_SELF).build()
        }
        Data::Union(unn) => {
            ImplBlockBuilder::new(ctx, unn, Trait::Immutable, FieldBounds::ALL_SELF).build()
        }
    }
}

pub(crate) fn derive_hash(ctx: &Ctx, _top_level: Trait) -> Result<TokenStream, Error> {
    // This doesn't delegate to `impl_block` because `impl_block` assumes it is
    // deriving a `zerocopy`-defined trait, and these trait impls share a common
    // shape that `Hash` does not. In particular, `zerocopy` traits contain a
    // method that only `zerocopy_derive` macros are supposed to implement, and
    // `impl_block` generating this trait method is incompatible with `Hash`.
    let type_ident = &ctx.ast.ident;
    let (impl_generics, ty_generics, where_clause) = ctx.ast.generics.split_for_impl();
    let where_predicates = where_clause.map(|clause| &clause.predicates);
    let zerocopy_crate = &ctx.zerocopy_crate;
    let core = ctx.core_path();
    Ok(quote! {
        impl #impl_generics #core::hash::Hash for #type_ident #ty_generics
        where
            Self: #zerocopy_crate::IntoBytes + #zerocopy_crate::Immutable,
            #where_predicates
        {
            fn hash<H: #core::hash::Hasher>(&self, state: &mut H) {
                #core::hash::Hasher::write(state, #zerocopy_crate::IntoBytes::as_bytes(self))
            }

            fn hash_slice<H: #core::hash::Hasher>(data: &[Self], state: &mut H) {
                #core::hash::Hasher::write(state, #zerocopy_crate::IntoBytes::as_bytes(data))
            }
        }
    })
}

pub(crate) fn derive_eq(ctx: &Ctx, _top_level: Trait) -> Result<TokenStream, Error> {
    // This doesn't delegate to `impl_block` because `impl_block` assumes it is
    // deriving a `zerocopy`-defined trait, and these trait impls share a common
    // shape that `Eq` does not. In particular, `zerocopy` traits contain a
    // method that only `zerocopy_derive` macros are supposed to implement, and
    // `impl_block` generating this trait method is incompatible with `Eq`.
    let type_ident = &ctx.ast.ident;
    let (impl_generics, ty_generics, where_clause) = ctx.ast.generics.split_for_impl();
    let where_predicates = where_clause.map(|clause| &clause.predicates);
    let zerocopy_crate = &ctx.zerocopy_crate;
    let core = ctx.core_path();
    Ok(quote! {
        impl #impl_generics #core::cmp::PartialEq for #type_ident #ty_generics
        where
            Self: #zerocopy_crate::IntoBytes + #zerocopy_crate::Immutable,
            #where_predicates
        {
            fn eq(&self, other: &Self) -> bool {
                #core::cmp::PartialEq::eq(
                    #zerocopy_crate::IntoBytes::as_bytes(self),
                    #zerocopy_crate::IntoBytes::as_bytes(other),
                )
            }
        }

        impl #impl_generics #core::cmp::Eq for #type_ident #ty_generics
        where
            Self: #zerocopy_crate::IntoBytes + #zerocopy_crate::Immutable,
            #where_predicates
        {
        }
    })
}

pub(crate) fn derive_split_at(ctx: &Ctx, _top_level: Trait) -> Result<TokenStream, Error> {
    let repr = StructUnionRepr::from_attrs(&ctx.ast.attrs)?;

    match &ctx.ast.data {
        Data::Struct(_) => {}
        Data::Enum(_) | Data::Union(_) => {
            return Err(Error::new(Span::call_site(), "can only be applied to structs"));
        }
    };

    if repr.get_packed().is_some() {
        return Err(Error::new(Span::call_site(), "must not have #[repr(packed)] attribute"));
    }

    if !(repr.is_c() || repr.is_transparent()) {
        return Err(Error::new(
            Span::call_site(),
            "must have #[repr(C)] or #[repr(transparent)] in order to guarantee this type's layout is splitable",
        ));
    }

    let fields = ctx.ast.data.fields();
    let trailing_field = if let Some(((_, _, trailing_field), _)) = fields.split_last() {
        trailing_field
    } else {
        return Err(Error::new(Span::call_site(), "must at least one field"));
    };

    let zerocopy_crate = &ctx.zerocopy_crate;
    // SAFETY: `#ty`, per the above checks, is `repr(C)` or `repr(transparent)`
    // and is not packed; its trailing field is guaranteed to be well-aligned
    // for its type. By invariant on `FieldBounds::TRAILING_SELF`, the trailing
    // slice of the trailing field is also well-aligned for its type.
    Ok(ImplBlockBuilder::new(ctx, &ctx.ast.data, Trait::SplitAt, FieldBounds::TRAILING_SELF)
        .inner_extras(quote! {
            type Elem = <#trailing_field as #zerocopy_crate::SplitAt>::Elem;
        })
        .build())
}
