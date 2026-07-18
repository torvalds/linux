// SPDX-License-Identifier: Apache-2.0 OR MIT

use proc_macro2::{
    Span,
    TokenStream, //
};
use quote::{
    format_ident,
    quote, //
};
use syn::{
    parse::{
        Parse,
        ParseStream, //
    },
    visit::Visit,
    visit_mut::VisitMut,
    Lifetime,
    Result,
    Token,
    Type, //
};

pub(crate) enum HigherRankedType {
    Explicit {
        _for_token: Token![for],
        _lt_token: Token![<],
        lifetime: Lifetime,
        _gt_token: Token![>],
        ty: Type,
    },
    Implicit {
        ty: Type,
    },
}

impl Parse for HigherRankedType {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        if input.peek(Token![for]) {
            Ok(Self::Explicit {
                _for_token: input.parse()?,
                _lt_token: input.parse()?,
                lifetime: input.parse()?,
                _gt_token: input.parse()?,
                ty: input.parse()?,
            })
        } else {
            Ok(Self::Implicit { ty: input.parse()? })
        }
    }
}

trait TypeExt {
    fn expand_elided_lifetime(&self, explicit_lt: &Lifetime) -> Type;
    fn replace_lifetime(&self, src: &Lifetime, dst: &Lifetime) -> Type;
    fn has_lifetime(&self, lt: &Lifetime) -> bool;
}

impl TypeExt for Type {
    fn expand_elided_lifetime(&self, explicit_lt: &Lifetime) -> Type {
        struct ElidedLifetimeExpander<'a>(&'a Lifetime);

        impl VisitMut for ElidedLifetimeExpander<'_> {
            fn visit_lifetime_mut(&mut self, lifetime: &mut Lifetime) {
                // Expand explicit `'_`
                if lifetime.ident == "_" {
                    *lifetime = self.0.clone();
                }
            }

            fn visit_type_reference_mut(&mut self, reference: &mut syn::TypeReference) {
                syn::visit_mut::visit_type_reference_mut(self, reference);

                if reference.lifetime.is_none() {
                    reference.lifetime = Some(self.0.clone());
                }
            }
        }

        let mut ret = self.clone();
        ElidedLifetimeExpander(explicit_lt).visit_type_mut(&mut ret);
        ret
    }

    fn replace_lifetime(&self, src: &Lifetime, dst: &Lifetime) -> Type {
        struct LifetimeReplacer<'a>(&'a Lifetime, &'a Lifetime);

        impl VisitMut for LifetimeReplacer<'_> {
            fn visit_lifetime_mut(&mut self, lifetime: &mut Lifetime) {
                if lifetime.ident == self.0.ident {
                    *lifetime = self.1.clone();
                }
            }
        }

        let mut ret = self.clone();
        LifetimeReplacer(src, dst).visit_type_mut(&mut ret);
        ret
    }

    fn has_lifetime(&self, lt: &Lifetime) -> bool {
        struct HasLifetime<'a>(&'a Lifetime, bool);

        impl Visit<'_> for HasLifetime<'_> {
            fn visit_lifetime(&mut self, lifetime: &Lifetime) {
                if lifetime.ident == self.0.ident {
                    self.1 = true;
                }
            }

            // Macro invocations are opaque; conservatively assume they may
            // reference the lifetime.
            fn visit_macro(&mut self, _: &syn::Macro) {
                self.1 = true;
            }
        }

        let mut visitor = HasLifetime(lt, false);
        visitor.visit_type(self);
        visitor.1
    }
}

struct Prover<'a>(&'a Lifetime, Vec<&'a Type>);

impl<'a> Prover<'a> {
    /// Prove that `ty` is covariant over `'lt`.
    ///
    /// This also needs to prove that it'll be wellformed for any instance of `'lt`.
    /// It can be assumed that `ty` will be wellformed if `'lt` is substituted to `'static`.
    fn prove(&mut self, ty: &'a Type) {
        match ty {
            Type::Paren(ty) => self.prove(&ty.elem),
            Type::Group(ty) => self.prove(&ty.elem),

            // No lifetime involved
            Type::Never(_) => {}

            // `[T; N]` and `[T]` is covariant over `T`.
            Type::Array(ty) => self.prove(&ty.elem),
            Type::Slice(ty) => self.prove(&ty.elem),

            Type::Tuple(ty) => {
                for elem in &ty.elems {
                    self.prove(elem);
                }
            }

            // `*const T` is covariant over `T`
            Type::Ptr(ty) if ty.const_token.is_some() => self.prove(&ty.elem),

            // `&T` is covariant over `T` and lifetime.
            //
            // Note that if we encounter `&'other_lt T`, then we still need to make sure the type
            // is wellformed if `T` involves `&'lt`, so we defer to the compiler.
            //
            // This is to block cases like `ForLt!(for<'a> &'static &'a u32)`, as the presence of
            // the type implies `'a: 'static` but this is unsound.
            Type::Reference(ty)
                if ty.mutability.is_none() && ty.lifetime.as_ref() == Some(self.0) =>
            {
                self.prove(&ty.elem)
            }

            // `&[mut] T` is covariant over lifetime.
            // In case we have `&[mut] NoLifetime`, we don't need to do additional checks.
            Type::Reference(ty) if !ty.elem.has_lifetime(self.0) => (),

            // No mention of lifetime at all, no need to perform compiler check.
            ty if !ty.has_lifetime(self.0) => (),

            // Otherwise, we need to emit checks so that compiler can determine if the types are
            // actually covariant.
            ty => self.1.push(ty),
        }
    }
}

pub(crate) fn for_lt(input: HigherRankedType) -> TokenStream {
    let (ty, lifetime) = match input {
        HigherRankedType::Explicit { lifetime, ty, .. } => (ty, lifetime),
        HigherRankedType::Implicit { ty } => {
            // If there's no explicit `for<'a>` binder, inject a synthetic `'__elided` lifetime
            // and expand elided sites.
            let lifetime = Lifetime {
                apostrophe: Span::mixed_site(),
                ident: format_ident!("__elided", span = Span::mixed_site()),
            };
            (ty.expand_elided_lifetime(&lifetime), lifetime)
        }
    };

    let mut prover = Prover(&lifetime, Vec::new());
    prover.prove(&ty);

    let mut proof = Vec::new();

    // Emit proofs for every type that requires additional compiler help in proving covariance.
    for (idx, required_proof) in prover.1.into_iter().enumerate() {
        // Insert a proof that the type is well-formed.
        //
        // This is intended to workaround a Rust compiler soundness bug related to HRTB.
        // https://github.com/rust-lang/rust/issues/152489
        //
        // This needs to be a struct instead of fn to avoid the implied WF bounds.
        let wf_proof_name = format_ident!("ProveWf{idx}");
        proof.push(quote!(
            struct #wf_proof_name<#lifetime>(
                ::core::marker::PhantomData<&#lifetime ()>, #required_proof
            );
        ));

        // Insert a proof that the type is covariant.
        let cov_proof_name = format_ident!("prove_covariant_{idx}");
        proof.push(quote!(
            fn #cov_proof_name<'__short, '__long: '__short>(
                long: #wf_proof_name<'__long>
            ) -> #wf_proof_name<'__short> {
                long
            }
        ));
    }

    // Make sure that the type is wellformed when substituting lifetime with `'static`.
    //
    // Currently the Rust compiler doesn't check this, see the above `ProveWf` documentation.
    //
    // We prefer to use this way of proving WF-ness as it can work when generics are involved.
    let ty_static = ty.replace_lifetime(
        &lifetime,
        &Lifetime {
            apostrophe: Span::mixed_site(),
            ident: format_ident!("static"),
        },
    );

    quote!(
        ::kernel::types::for_lt::UnsafeForLtImpl::<
            dyn for<#lifetime> ::kernel::types::for_lt::WithLt<#lifetime, Of = #ty>,
            #ty_static,
            {
                #(#proof)*

                0
            }
        >
    )
}
