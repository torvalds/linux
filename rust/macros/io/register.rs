// SPDX-License-Identifier: GPL-2.0

//! Documentation and usage example of the macro can be found at `rust/kernel/io/register.rs`.

use proc_macro2::{
    Group,
    Literal,
    Span,
    TokenStream, //
};
use quote::{
    quote,
    quote_spanned, //
};
use syn::{
    bracketed,
    parenthesized,
    parse::Parse,
    parse_quote,
    spanned::Spanned,
    token,
    Attribute,
    Error,
    Expr,
    Ident,
    Path,
    Result,
    Token,
    Type,
    Visibility, //
};

mod kw {
    syn::custom_keyword!(base);
    syn::custom_keyword!(stride);
}

/// Definition of a register array.
///
/// Specify a size, and optionally a stride. Syntax is of form `[EXPR $(, stride = EXPR)?]`.
struct RegArrayDef {
    size: Expr,
    stride: Option<Expr>,
}

/// Offset of a register.
///
/// Can be either of form
/// * `@ offset` for fixed offset
/// * `=> alias` for alias of register `alias`.
/// * `=> alias[idx]` for alias of register array `alias`'s `idx`-th element.
enum RegOffset {
    /// Register is located at fixed address.
    Fixed { offset: Literal },
    /// Register is an alias of a fixed register.
    Alias { alias: Path },
    /// Register is an alias of an element of a register array.
    ElementAlias { alias: Path, idx: Expr },
}

/// Definition of a single register.
struct Reg {
    attrs: Vec<Attribute>,
    vis: Visibility,
    name: Ident,
    unique: bool,
    ty: Type,
    array: Option<RegArrayDef>,
    offset: RegOffset,
    bitfield: Option<(Type, Group)>,
}

impl Parse for Reg {
    fn parse(input: syn::parse::ParseStream<'_>) -> Result<Self> {
        let attrs = input.call(Attribute::parse_outer)?;
        let vis = input.parse()?;
        let name = input.parse()?;

        let lh = input.lookahead1();
        let (unique, ty, bitfield_storage) = if lh.peek(Token![:]) {
            let _: Token![:] = input.parse()?;

            let mut attrs = input.call(Attribute::parse_outer)?;
            let unique = attrs
                .extract_if(.., |attr| attr.path().is_ident("unique"))
                .count()
                != 0;
            if !attrs.is_empty() {
                Err(Error::new_spanned(&attrs[0], "unexpected attributes"))?
            }

            (unique, input.parse()?, None)
        } else if lh.peek(token::Paren) {
            let content;
            parenthesized!(content in input);
            let bitfield_storage = Some(content.parse()?);

            // For bitfields, bitfield macro will generate a type with the same name as `name`.
            (true, parse_quote!(#name), bitfield_storage)
        } else {
            Err(lh.error())?
        };

        let array = if input.peek(token::Bracket) {
            let content;
            bracketed!(content in input);
            let size = content.parse()?;
            let stride = if content.peek(Token![,]) {
                let _: Token![,] = content.parse()?;
                let _: kw::stride = content.parse()?;
                let _: Token![=] = content.parse()?;
                Some(content.parse()?)
            } else {
                None
            };
            Some(RegArrayDef { size, stride })
        } else {
            None
        };

        // Parse offset and the base it's relative to.
        let lh = input.lookahead1();
        let offset = if lh.peek(Token![@]) {
            let _: Token![@] = input.parse()?;

            RegOffset::Fixed {
                offset: input.parse()?,
            }
        } else if lh.peek(Token![=>]) {
            let _: Token![=>] = input.parse()?;
            let alias: Path = input.parse()?;

            if input.peek(token::Bracket) {
                let content;
                bracketed!(content in input);
                RegOffset::ElementAlias {
                    alias,
                    idx: content.parse()?,
                }
            } else {
                RegOffset::Alias { alias }
            }
        } else {
            Err(lh.error())?
        };

        let bitfield = if let Some(storage) = bitfield_storage {
            let lh = input.lookahead1();
            let args = if lh.peek(token::Brace) {
                input.parse()?
            } else {
                Err(lh.error())?
            };
            Some((storage, args))
        } else {
            let _: Token![;] = input.parse()?;
            None
        };

        Ok(Self {
            attrs,
            vis,
            name,
            unique,
            ty,
            array,
            offset,
            bitfield,
        })
    }
}

pub(crate) struct RegDef {
    base: Type,
    regs: Vec<Reg>,
}

impl Parse for RegDef {
    fn parse(input: syn::parse::ParseStream<'_>) -> Result<Self> {
        let _: kw::base = input.parse().map_err(|e| {
            Error::new(
                e.span(),
                "a base type needs to be specified for `register!` invocation with `base: ty;`",
            )
        })?;

        let _: Token![:] = input.parse()?;
        let base = input.parse()?;
        let _: Token![;] = input.parse()?;

        let mut regs = Vec::new();
        while !input.is_empty() {
            regs.push(input.parse()?);
        }
        Ok(RegDef { base, regs })
    }
}

pub(crate) fn register(def: RegDef) -> Result<TokenStream> {
    let mut outputs = TokenStream::new();

    let base = &def.base;
    for reg in def.regs {
        let Reg {
            attrs,
            vis,
            name,
            unique,
            ty,
            array,
            offset,
            bitfield,
        } = reg;

        // Use register name's span for generated code, so error messages (if any) can point to it
        // instead of the entire register allocation.
        let span = name.span().resolved_at(Span::mixed_site());

        let offset = match offset {
            RegOffset::Fixed { offset } => quote!(#offset),
            RegOffset::Alias { alias } => {
                quote_spanned!(alias.span().resolved_at(span) =>
                    ::kernel::io::register::OffsetLoc::<#base, _>::const_offset(#alias)
                )
            }
            RegOffset::ElementAlias { alias, idx } => {
                quote_spanned!(alias.span().resolved_at(span) =>
                    ::kernel::io::register::element_alias_offset::<#base, #alias>(#idx)
                )
            }
        };

        if let Some((storage, args)) = &bitfield {
            outputs.extend(quote_spanned!(span =>
                ::kernel::bitfield!(
                    // `#[allow(non_camel_case_types)]` is added since register names typically use
                    // `SCREAMING_CASE`.
                    #[allow(non_camel_case_types)]
                    #(#attrs)* #vis struct #name(#storage) #args
                );
            ));
        }

        match array {
            None => {
                if unique {
                    outputs.extend(quote!(
                        impl ::kernel::io::register::FixedIoLoc<#base> for #ty {
                            type Location = ::kernel::io::register::OffsetLoc<#base, #ty>;
                            const LOCATION: Self::Location = #name;
                        }
                    ))
                }

                outputs.extend(quote_spanned!(span =>
                    #(#attrs)* #vis const #name: ::kernel::io::register::OffsetLoc<#base, #ty> =
                        ::kernel::io::register::OffsetLoc::new(#offset);
                ));
            }

            Some(def) => {
                if bitfield.is_none() {
                    Err(Error::new_spanned(
                        &ty,
                        "defining without bitfield is not yet supported for this type of register",
                    ))?
                }

                let size = &def.size;
                let stride = if let Some(stride) = &def.stride {
                    outputs.extend(quote_spanned!(stride.span().resolved_at(span) =>
                        ::kernel::build_assert::static_assert!(
                            ::core::mem::size_of::<#ty>() <= #stride
                        );
                    ));
                    quote!(#stride)
                } else {
                    quote_spanned!(span => ::core::mem::size_of::<#ty>())
                };

                outputs.extend(quote_spanned!(span =>
                    impl ::kernel::io::register::Array for #name {}

                    impl ::kernel::io::register::RegisterArray for #name {
                        type Base = #base;
                        const OFFSET: usize = #offset;
                        const SIZE: usize = #size;
                        const STRIDE: usize = #stride;
                    }
                ));
            }
        };
    }

    Ok(outputs)
}
