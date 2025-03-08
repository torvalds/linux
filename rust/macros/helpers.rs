// SPDX-License-Identifier: GPL-2.0

use proc_macro::{token_stream, Group, Ident, TokenStream, TokenTree};

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

/// Given a function declaration, finds the name of the function.
pub(crate) fn function_name(input: TokenStream) -> Option<Ident> {
    let mut input = input.into_iter();
    while let Some(token) = input.next() {
        match token {
            TokenTree::Ident(i) if i.to_string() == "fn" => {
                if let Some(TokenTree::Ident(i)) = input.next() {
                    return Some(i);
                }
                return None;
            }
            _ => continue,
        }
    }
    None
}
