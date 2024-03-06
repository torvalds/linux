// SPDX-License-Identifier: GPL-2.0

use proc_macro::{Delimiter, Group, Ident, Spacing, Span, TokenTree};

fn concat(tokens: &[TokenTree], group_span: Span) -> TokenTree {
    let mut tokens = tokens.iter();
    let mut segments = Vec::new();
    let mut span = None;
    loop {
        match tokens.next() {
            None => break,
            Some(TokenTree::Literal(lit)) => {
                // Allow us to concat string literals by stripping quotes
                let mut value = lit.to_string();
                if value.starts_with('"') && value.ends_with('"') {
                    value.remove(0);
                    value.pop();
                }
                segments.push((value, lit.span()));
            }
            Some(TokenTree::Ident(ident)) => {
                let mut value = ident.to_string();
                if value.starts_with("r#") {
                    value.replace_range(0..2, "");
                }
                segments.push((value, ident.span()));
            }
            Some(TokenTree::Punct(p)) if p.as_char() == ':' => {
                let Some(TokenTree::Ident(ident)) = tokens.next() else {
                    panic!("expected identifier as modifier");
                };

                let (mut value, sp) = segments.pop().expect("expected identifier before modifier");
                match ident.to_string().as_str() {
                    // Set the overall span of concatenated token as current span
                    "span" => {
                        assert!(
                            span.is_none(),
                            "span modifier should only appear at most once"
                        );
                        span = Some(sp);
                    }
                    "lower" => value = value.to_lowercase(),
                    "upper" => value = value.to_uppercase(),
                    v => panic!("unknown modifier `{v}`"),
                };
                segments.push((value, sp));
            }
            _ => panic!("unexpected token in paste segments"),
        };
    }

    let pasted: String = segments.into_iter().map(|x| x.0).collect();
    TokenTree::Ident(Ident::new(&pasted, span.unwrap_or(group_span)))
}

pub(crate) fn expand(tokens: &mut Vec<TokenTree>) {
    for token in tokens.iter_mut() {
        if let TokenTree::Group(group) = token {
            let delimiter = group.delimiter();
            let span = group.span();
            let mut stream: Vec<_> = group.stream().into_iter().collect();
            // Find groups that looks like `[< A B C D >]`
            if delimiter == Delimiter::Bracket
                && stream.len() >= 3
                && matches!(&stream[0], TokenTree::Punct(p) if p.as_char() == '<')
                && matches!(&stream[stream.len() - 1], TokenTree::Punct(p) if p.as_char() == '>')
            {
                // Replace the group with concatenated token
                *token = concat(&stream[1..stream.len() - 1], span);
            } else {
                // Recursively expand tokens inside the group
                expand(&mut stream);
                let mut group = Group::new(delimiter, stream.into_iter().collect());
                group.set_span(span);
                *token = TokenTree::Group(group);
            }
        }
    }

    // Path segments cannot contain invisible delimiter group, so remove them if any.
    for i in (0..tokens.len().saturating_sub(3)).rev() {
        // Looking for a double colon
        if matches!(
            (&tokens[i + 1], &tokens[i + 2]),
            (TokenTree::Punct(a), TokenTree::Punct(b))
                if a.as_char() == ':' && a.spacing() == Spacing::Joint && b.as_char() == ':'
        ) {
            match &tokens[i + 3] {
                TokenTree::Group(group) if group.delimiter() == Delimiter::None => {
                    tokens.splice(i + 3..i + 4, group.stream());
                }
                _ => (),
            }

            match &tokens[i] {
                TokenTree::Group(group) if group.delimiter() == Delimiter::None => {
                    tokens.splice(i..i + 1, group.stream());
                }
                _ => (),
            }
        }
    }
}
