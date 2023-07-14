// SPDX-License-Identifier: GPL-2.0

use proc_macro::{token_stream, Group, Punct, Spacing, TokenStream, TokenTree};

pub(crate) fn try_ident(it: &mut token_stream::IntoIter) -> Option<String> {
    if let Some(TokenTree::Ident(ident)) = it.next() {
        Some(ident.to_string())
    } else {
        None
    }
}

pub(crate) fn try_literal(it: &mut token_stream::IntoIter) -> Option<String> {
    if let Some(TokenTree::Literal(literal)) = it.next() {
        Some(literal.to_string())
    } else {
        None
    }
}

pub(crate) fn try_string(it: &mut token_stream::IntoIter) -> Option<String> {
    try_literal(it).and_then(|string| {
        if string.starts_with('\"') && string.ends_with('\"') {
            let content = &string[1..string.len() - 1];
            if content.contains('\\') {
                panic!("Escape sequences in string literals not yet handled");
            }
            Some(content.to_string())
        } else if string.starts_with("r\"") {
            panic!("Raw string literals are not yet handled");
        } else {
            None
        }
    })
}

pub(crate) fn expect_ident(it: &mut token_stream::IntoIter) -> String {
    try_ident(it).expect("Expected Ident")
}

pub(crate) fn expect_punct(it: &mut token_stream::IntoIter) -> char {
    if let TokenTree::Punct(punct) = it.next().expect("Reached end of token stream for Punct") {
        punct.as_char()
    } else {
        panic!("Expected Punct");
    }
}

pub(crate) fn expect_string(it: &mut token_stream::IntoIter) -> String {
    try_string(it).expect("Expected string")
}

pub(crate) fn expect_string_ascii(it: &mut token_stream::IntoIter) -> String {
    let string = try_string(it).expect("Expected string");
    assert!(string.is_ascii(), "Expected ASCII string");
    string
}

pub(crate) fn expect_group(it: &mut token_stream::IntoIter) -> Group {
    if let TokenTree::Group(group) = it.next().expect("Reached end of token stream for Group") {
        group
    } else {
        panic!("Expected Group");
    }
}

pub(crate) fn expect_end(it: &mut token_stream::IntoIter) {
    if it.next().is_some() {
        panic!("Expected end");
    }
}

pub(crate) struct Generics {
    pub(crate) impl_generics: Vec<TokenTree>,
    pub(crate) ty_generics: Vec<TokenTree>,
}

/// Parses the given `TokenStream` into `Generics` and the rest.
///
/// The generics are not present in the rest, but a where clause might remain.
pub(crate) fn parse_generics(input: TokenStream) -> (Generics, Vec<TokenTree>) {
    // `impl_generics`, the declared generics with their bounds.
    let mut impl_generics = vec![];
    // Only the names of the generics, without any bounds.
    let mut ty_generics = vec![];
    // Tokens not related to the generics e.g. the `where` token and definition.
    let mut rest = vec![];
    // The current level of `<`.
    let mut nesting = 0;
    let mut toks = input.into_iter();
    // If we are at the beginning of a generic parameter.
    let mut at_start = true;
    for tt in &mut toks {
        match tt.clone() {
            TokenTree::Punct(p) if p.as_char() == '<' => {
                if nesting >= 1 {
                    // This is inside of the generics and part of some bound.
                    impl_generics.push(tt);
                }
                nesting += 1;
            }
            TokenTree::Punct(p) if p.as_char() == '>' => {
                // This is a parsing error, so we just end it here.
                if nesting == 0 {
                    break;
                } else {
                    nesting -= 1;
                    if nesting >= 1 {
                        // We are still inside of the generics and part of some bound.
                        impl_generics.push(tt);
                    }
                    if nesting == 0 {
                        break;
                    }
                }
            }
            tt => {
                if nesting == 1 {
                    // Here depending on the token, it might be a generic variable name.
                    match &tt {
                        // Ignore const.
                        TokenTree::Ident(i) if i.to_string() == "const" => {}
                        TokenTree::Ident(_) if at_start => {
                            ty_generics.push(tt.clone());
                            // We also already push the `,` token, this makes it easier to append
                            // generics.
                            ty_generics.push(TokenTree::Punct(Punct::new(',', Spacing::Alone)));
                            at_start = false;
                        }
                        TokenTree::Punct(p) if p.as_char() == ',' => at_start = true,
                        // Lifetimes begin with `'`.
                        TokenTree::Punct(p) if p.as_char() == '\'' && at_start => {
                            ty_generics.push(tt.clone());
                        }
                        _ => {}
                    }
                }
                if nesting >= 1 {
                    impl_generics.push(tt);
                } else if nesting == 0 {
                    // If we haven't entered the generics yet, we still want to keep these tokens.
                    rest.push(tt);
                }
            }
        }
    }
    rest.extend(toks);
    (
        Generics {
            impl_generics,
            ty_generics,
        },
        rest,
    )
}
