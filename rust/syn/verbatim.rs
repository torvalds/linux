use crate::parse::ParseStream;
use proc_macro2::{Delimiter, TokenStream};
use std::cmp::Ordering;
use std::iter;

pub(crate) fn between<'a>(begin: ParseStream<'a>, end: ParseStream<'a>) -> TokenStream {
    let end = end.cursor();
    let mut cursor = begin.cursor();
    assert!(crate::buffer::same_buffer(end, cursor));

    let mut tokens = TokenStream::new();
    while cursor != end {
        let (tt, next) = cursor.token_tree().unwrap();

        if crate::buffer::cmp_assuming_same_buffer(end, next) == Ordering::Less {
            // A syntax node can cross the boundary of a None-delimited group
            // due to such groups being transparent to the parser in most cases.
            // Any time this occurs the group is known to be semantically
            // irrelevant. https://github.com/dtolnay/syn/issues/1235
            if let Some((inside, _span, after)) = cursor.group(Delimiter::None) {
                assert!(next == after);
                cursor = inside;
                continue;
            } else {
                panic!("verbatim end must not be inside a delimited group");
            }
        }

        tokens.extend(iter::once(tt));
        cursor = next;
    }
    tokens
}
