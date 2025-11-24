use crate::path::Path;
use crate::token;

ast_enum! {
    /// The visibility level of an item: inherited or `pub` or
    /// `pub(restricted)`.
    ///
    /// # Syntax tree enum
    ///
    /// This type is a [syntax tree enum].
    ///
    /// [syntax tree enum]: crate::expr::Expr#syntax-tree-enums
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub enum Visibility {
        /// A public visibility level: `pub`.
        Public(Token![pub]),

        /// A visibility level restricted to some path: `pub(self)` or
        /// `pub(super)` or `pub(crate)` or `pub(in some::module)`.
        Restricted(VisRestricted),

        /// An inherited visibility, which usually means private.
        Inherited,
    }
}

ast_struct! {
    /// A visibility level restricted to some path: `pub(self)` or
    /// `pub(super)` or `pub(crate)` or `pub(in some::module)`.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    pub struct VisRestricted {
        pub pub_token: Token![pub],
        pub paren_token: token::Paren,
        pub in_token: Option<Token![in]>,
        pub path: Box<Path>,
    }
}

ast_enum! {
    /// Unused, but reserved for RFC 3323 restrictions.
    #[cfg_attr(docsrs, doc(cfg(any(feature = "full", feature = "derive"))))]
    #[non_exhaustive]
    pub enum FieldMutability {
        None,

        // TODO: https://rust-lang.github.io/rfcs/3323-restrictions.html
        //
        // FieldMutability::Restricted(MutRestricted)
        //
        // pub struct MutRestricted {
        //     pub mut_token: Token![mut],
        //     pub paren_token: token::Paren,
        //     pub in_token: Option<Token![in]>,
        //     pub path: Box<Path>,
        // }
    }
}

#[cfg(feature = "parsing")]
pub(crate) mod parsing {
    use crate::error::Result;
    use crate::ext::IdentExt as _;
    use crate::ident::Ident;
    use crate::parse::discouraged::Speculative as _;
    use crate::parse::{Parse, ParseStream};
    use crate::path::Path;
    use crate::restriction::{VisRestricted, Visibility};
    use crate::token;

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for Visibility {
        fn parse(input: ParseStream) -> Result<Self> {
            // Recognize an empty None-delimited group, as produced by a $:vis
            // matcher that matched no tokens.
            if input.peek(token::Group) {
                let ahead = input.fork();
                let group = crate::group::parse_group(&ahead)?;
                if group.content.is_empty() {
                    input.advance_to(&ahead);
                    return Ok(Visibility::Inherited);
                }
            }

            if input.peek(Token![pub]) {
                Self::parse_pub(input)
            } else {
                Ok(Visibility::Inherited)
            }
        }
    }

    impl Visibility {
        fn parse_pub(input: ParseStream) -> Result<Self> {
            let pub_token = input.parse::<Token![pub]>()?;

            if input.peek(token::Paren) {
                let ahead = input.fork();

                let content;
                let paren_token = parenthesized!(content in ahead);
                if content.peek(Token![crate])
                    || content.peek(Token![self])
                    || content.peek(Token![super])
                {
                    let path = content.call(Ident::parse_any)?;

                    // Ensure there are no additional tokens within `content`.
                    // Without explicitly checking, we may misinterpret a tuple
                    // field as a restricted visibility, causing a parse error.
                    // e.g. `pub (crate::A, crate::B)` (Issue #720).
                    if content.is_empty() {
                        input.advance_to(&ahead);
                        return Ok(Visibility::Restricted(VisRestricted {
                            pub_token,
                            paren_token,
                            in_token: None,
                            path: Box::new(Path::from(path)),
                        }));
                    }
                } else if content.peek(Token![in]) {
                    let in_token: Token![in] = content.parse()?;
                    let path = content.call(Path::parse_mod_style)?;

                    input.advance_to(&ahead);
                    return Ok(Visibility::Restricted(VisRestricted {
                        pub_token,
                        paren_token,
                        in_token: Some(in_token),
                        path: Box::new(path),
                    }));
                }
            }

            Ok(Visibility::Public(pub_token))
        }

        #[cfg(feature = "full")]
        pub(crate) fn is_some(&self) -> bool {
            match self {
                Visibility::Inherited => false,
                _ => true,
            }
        }
    }
}

#[cfg(feature = "printing")]
mod printing {
    use crate::path;
    use crate::path::printing::PathStyle;
    use crate::restriction::{VisRestricted, Visibility};
    use proc_macro2::TokenStream;
    use quote::ToTokens;

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for Visibility {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            match self {
                Visibility::Public(pub_token) => pub_token.to_tokens(tokens),
                Visibility::Restricted(vis_restricted) => vis_restricted.to_tokens(tokens),
                Visibility::Inherited => {}
            }
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for VisRestricted {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            self.pub_token.to_tokens(tokens);
            self.paren_token.surround(tokens, |tokens| {
                // TODO: If we have a path which is not "self" or "super" or
                // "crate", automatically add the "in" token.
                self.in_token.to_tokens(tokens);
                path::printing::print_path(tokens, &self.path, PathStyle::Mod);
            });
        }
    }
}
