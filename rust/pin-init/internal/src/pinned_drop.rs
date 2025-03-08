// SPDX-License-Identifier: Apache-2.0 OR MIT

use proc_macro::{TokenStream, TokenTree};

pub(crate) fn pinned_drop(_args: TokenStream, input: TokenStream) -> TokenStream {
    let mut toks = input.into_iter().collect::<Vec<_>>();
    assert!(!toks.is_empty());
    // Ensure that we have an `impl` item.
    assert!(matches!(&toks[0], TokenTree::Ident(i) if i.to_string() == "impl"));
    // Ensure that we are implementing `PinnedDrop`.
    let mut nesting: usize = 0;
    let mut pinned_drop_idx = None;
    for (i, tt) in toks.iter().enumerate() {
        match tt {
            TokenTree::Punct(p) if p.as_char() == '<' => {
                nesting += 1;
            }
            TokenTree::Punct(p) if p.as_char() == '>' => {
                nesting = nesting.checked_sub(1).unwrap();
                continue;
            }
            _ => {}
        }
        if i >= 1 && nesting == 0 {
            // Found the end of the generics, this should be `PinnedDrop`.
            assert!(
                matches!(tt, TokenTree::Ident(i) if i.to_string() == "PinnedDrop"),
                "expected 'PinnedDrop', found: '{:?}'",
                tt
            );
            pinned_drop_idx = Some(i);
            break;
        }
    }
    let idx = pinned_drop_idx
        .unwrap_or_else(|| panic!("Expected an `impl` block implementing `PinnedDrop`."));
    // Fully qualify the `PinnedDrop`, as to avoid any tampering.
    toks.splice(idx..idx, quote!(::pin_init::));
    // Take the `{}` body and call the declarative macro.
    if let Some(TokenTree::Group(last)) = toks.pop() {
        let last = last.stream();
        quote!(::pin_init::__pinned_drop! {
            @impl_sig(#(#toks)*),
            @impl_body(#last),
        })
    } else {
        TokenStream::from_iter(toks)
    }
}
