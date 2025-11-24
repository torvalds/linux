#[cfg(feature = "parsing")]
use crate::lookahead;

pub use proc_macro2::Ident;

#[cfg(feature = "parsing")]
pub_if_not_doc! {
    #[doc(hidden)]
    #[allow(non_snake_case)]
    pub fn Ident(marker: lookahead::TokenMarker) -> Ident {
        match marker {}
    }
}

macro_rules! ident_from_token {
    ($token:ident) => {
        impl From<Token![$token]> for Ident {
            fn from(token: Token![$token]) -> Ident {
                Ident::new(stringify!($token), token.span)
            }
        }
    };
}

ident_from_token!(self);
ident_from_token!(Self);
ident_from_token!(super);
ident_from_token!(crate);
ident_from_token!(extern);

impl From<Token![_]> for Ident {
    fn from(token: Token![_]) -> Ident {
        Ident::new("_", token.span)
    }
}

pub(crate) fn xid_ok(symbol: &str) -> bool {
    let mut chars = symbol.chars();
    let first = chars.next().unwrap();
    if !(first == '_' || unicode_ident::is_xid_start(first)) {
        return false;
    }
    for ch in chars {
        if !unicode_ident::is_xid_continue(ch) {
            return false;
        }
    }
    true
}

#[cfg(feature = "parsing")]
mod parsing {
    use crate::buffer::Cursor;
    use crate::error::Result;
    use crate::parse::{Parse, ParseStream};
    use crate::token::Token;
    use proc_macro2::Ident;

    fn accept_as_ident(ident: &Ident) -> bool {
        match ident.to_string().as_str() {
            "_" |
            // Based on https://doc.rust-lang.org/1.65.0/reference/keywords.html
            "abstract" | "as" | "async" | "await" | "become" | "box" | "break" |
            "const" | "continue" | "crate" | "do" | "dyn" | "else" | "enum" |
            "extern" | "false" | "final" | "fn" | "for" | "if" | "impl" | "in" |
            "let" | "loop" | "macro" | "match" | "mod" | "move" | "mut" |
            "override" | "priv" | "pub" | "ref" | "return" | "Self" | "self" |
            "static" | "struct" | "super" | "trait" | "true" | "try" | "type" |
            "typeof" | "unsafe" | "unsized" | "use" | "virtual" | "where" |
            "while" | "yield" => false,
            _ => true,
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    impl Parse for Ident {
        fn parse(input: ParseStream) -> Result<Self> {
            input.step(|cursor| {
                if let Some((ident, rest)) = cursor.ident() {
                    if accept_as_ident(&ident) {
                        Ok((ident, rest))
                    } else {
                        Err(cursor.error(format_args!(
                            "expected identifier, found keyword `{}`",
                            ident,
                        )))
                    }
                } else {
                    Err(cursor.error("expected identifier"))
                }
            })
        }
    }

    impl Token for Ident {
        fn peek(cursor: Cursor) -> bool {
            if let Some((ident, _rest)) = cursor.ident() {
                accept_as_ident(&ident)
            } else {
                false
            }
        }

        fn display() -> &'static str {
            "identifier"
        }
    }
}
