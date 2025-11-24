#[cfg(feature = "parsing")]
use crate::lookahead;
use proc_macro2::{Ident, Span};
use std::cmp::Ordering;
use std::fmt::{self, Display};
use std::hash::{Hash, Hasher};

/// A Rust lifetime: `'a`.
///
/// Lifetime names must conform to the following rules:
///
/// - Must start with an apostrophe.
/// - Must not consist of just an apostrophe: `'`.
/// - Character after the apostrophe must be `_` or a Unicode code point with
///   the XID_Start property.
/// - All following characters must be Unicode code points with the XID_Continue
///   property.
pub struct Lifetime {
    pub apostrophe: Span,
    pub ident: Ident,
}

impl Lifetime {
    /// # Panics
    ///
    /// Panics if the lifetime does not conform to the bulleted rules above.
    ///
    /// # Invocation
    ///
    /// ```
    /// # use proc_macro2::Span;
    /// # use syn::Lifetime;
    /// #
    /// # fn f() -> Lifetime {
    /// Lifetime::new("'a", Span::call_site())
    /// # }
    /// ```
    pub fn new(symbol: &str, span: Span) -> Self {
        if !symbol.starts_with('\'') {
            panic!(
                "lifetime name must start with apostrophe as in \"'a\", got {:?}",
                symbol
            );
        }

        if symbol == "'" {
            panic!("lifetime name must not be empty");
        }

        if !crate::ident::xid_ok(&symbol[1..]) {
            panic!("{:?} is not a valid lifetime name", symbol);
        }

        Lifetime {
            apostrophe: span,
            ident: Ident::new(&symbol[1..], span),
        }
    }

    pub fn span(&self) -> Span {
        self.apostrophe
            .join(self.ident.span())
            .unwrap_or(self.apostrophe)
    }

    pub fn set_span(&mut self, span: Span) {
        self.apostrophe = span;
        self.ident.set_span(span);
    }
}

impl Display for Lifetime {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        "'".fmt(formatter)?;
        self.ident.fmt(formatter)
    }
}

impl Clone for Lifetime {
    fn clone(&self) -> Self {
        Lifetime {
            apostrophe: self.apostrophe,
            ident: self.ident.clone(),
        }
    }
}

impl PartialEq for Lifetime {
    fn eq(&self, other: &Lifetime) -> bool {
        self.ident.eq(&other.ident)
    }
}

impl Eq for Lifetime {}

impl PartialOrd for Lifetime {
    fn partial_cmp(&self, other: &Lifetime) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for Lifetime {
    fn cmp(&self, other: &Lifetime) -> Ordering {
        self.ident.cmp(&other.ident)
    }
}

impl Hash for Lifetime {
    fn hash<H: Hasher>(&self, h: &mut H) {
        self.ident.hash(h);
    }
}

#[cfg(feature = "parsing")]
pub_if_not_doc! {
    #[doc(hidden)]
    #[allow(non_snake_case)]
    pub fn Lifetime(marker: lookahead::TokenMarker) -> Lifetime {
        match marker {}
    }
}

#[cfg(feature = "parsing")]
pub(crate) mod parsing {
    use crate::error::Result;
    use crate::lifetime::Lifetime;
    use crate::parse::{Parse, ParseStream};

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for Lifetime {
        fn parse(input: ParseStream) -> Result<Self> {
            input.step(|cursor| {
                cursor
                    .lifetime()
                    .ok_or_else(|| cursor.error("expected lifetime"))
            })
        }
    }
}

#[cfg(feature = "printing")]
mod printing {
    use crate::lifetime::Lifetime;
    use proc_macro2::{Punct, Spacing, TokenStream};
    use quote::{ToTokens, TokenStreamExt};

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl ToTokens for Lifetime {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            let mut apostrophe = Punct::new('\'', Spacing::Joint);
            apostrophe.set_span(self.apostrophe);
            tokens.append(apostrophe);
            self.ident.to_tokens(tokens);
        }
    }
}
