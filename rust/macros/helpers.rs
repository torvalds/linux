// SPDX-License-Identifier: GPL-2.0

use proc_macro::{token_stream, Group, TokenStream, TokenTree};

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

/// Parsed generics.
///
/// See the field documentation for an explanation what each of the fields represents.
///
/// # Examples
///
/// ```rust,ignore
/// # let input = todo!();
/// let (Generics { decl_generics, impl_generics, ty_generics }, rest) = parse_generics(input);
/// quote! {
///     struct Foo<$($decl_generics)*> {
///         // ...
///     }
///
///     impl<$impl_generics> Foo<$ty_generics> {
///         fn foo() {
///             // ...
///         }
///     }
/// }
/// ```
pub(crate) struct Generics {
    /// The generics with bounds and default values (e.g. `T: Clone, const N: usize = 0`).
    ///
    /// Use this on type definitions e.g. `struct Foo<$decl_generics> ...` (or `union`/`enum`).
    pub(crate) decl_generics: Vec<TokenTree>,
    /// The generics with bounds (e.g. `T: Clone, const N: usize`).
    ///
    /// Use this on `impl` blocks e.g. `impl<$impl_generics> Trait for ...`.
    pub(crate) impl_generics: Vec<TokenTree>,
    /// The generics without bounds and without default values (e.g. `T, N`).
    ///
    /// Use this when you use the type that is declared with these generics e.g.
    /// `Foo<$ty_generics>`.
    pub(crate) ty_generics: Vec<TokenTree>,
}

/// Parses the given `TokenStream` into `Generics` and the rest.
///
/// The generics are not present in the rest, but a where clause might remain.
pub(crate) fn parse_generics(input: TokenStream) -> (Generics, Vec<TokenTree>) {
    // The generics with bounds and default values.
    let mut decl_generics = vec![];
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
    let mut skip_until_comma = false;
    while let Some(tt) = toks.next() {
        if nesting == 1 && matches!(&tt, TokenTree::Punct(p) if p.as_char() == '>') {
            // Found the end of the generics.
            break;
        } else if nesting >= 1 {
            decl_generics.push(tt.clone());
        }
        match tt.clone() {
            TokenTree::Punct(p) if p.as_char() == '<' => {
                if nesting >= 1 && !skip_until_comma {
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
                    if nesting >= 1 && !skip_until_comma {
                        // We are still inside of the generics and part of some bound.
                        impl_generics.push(tt);
                    }
                }
            }
            TokenTree::Punct(p) if skip_until_comma && p.as_char() == ',' => {
                if nesting == 1 {
                    impl_generics.push(tt.clone());
                    impl_generics.push(tt);
                    skip_until_comma = false;
                }
            }
            _ if !skip_until_comma => {
                match nesting {
                    // If we haven't entered the generics yet, we still want to keep these tokens.
                    0 => rest.push(tt),
                    1 => {
                        // Here depending on the token, it might be a generic variable name.
                        match tt.clone() {
                            TokenTree::Ident(i) if at_start && i.to_string() == "const" => {
                                let Some(name) = toks.next() else {
                                    // Parsing error.
                                    break;
                                };
                                impl_generics.push(tt);
                                impl_generics.push(name.clone());
                                ty_generics.push(name.clone());
                                decl_generics.push(name);
                                at_start = false;
                            }
                            TokenTree::Ident(_) if at_start => {
                                impl_generics.push(tt.clone());
                                ty_generics.push(tt);
                                at_start = false;
                            }
                            TokenTree::Punct(p) if p.as_char() == ',' => {
                                impl_generics.push(tt.clone());
                                ty_generics.push(tt);
                                at_start = true;
                            }
                            // Lifetimes begin with `'`.
                            TokenTree::Punct(p) if p.as_char() == '\'' && at_start => {
                                impl_generics.push(tt.clone());
                                ty_generics.push(tt);
                            }
                            // Generics can have default values, we skip these.
                            TokenTree::Punct(p) if p.as_char() == '=' => {
                                skip_until_comma = true;
                            }
                            _ => impl_generics.push(tt),
                        }
                    }
                    _ => impl_generics.push(tt),
                }
            }
            _ => {}
        }
    }
    rest.extend(toks);
    (
        Generics {
            impl_generics,
            decl_generics,
            ty_generics,
        },
        rest,
    )
}
