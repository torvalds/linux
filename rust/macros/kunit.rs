// SPDX-License-Identifier: GPL-2.0

//! Procedural macro to run KUnit tests using a user-space like syntax.
//!
//! Copyright (c) 2023 José Expósito <jose.exposito89@gmail.com>

use proc_macro::{Delimiter, Group, TokenStream, TokenTree};
use std::fmt::Write;

pub(crate) fn kunit_tests(attr: TokenStream, ts: TokenStream) -> TokenStream {
    let attr = attr.to_string();

    if attr.is_empty() {
        panic!("Missing test name in `#[kunit_tests(test_name)]` macro")
    }

    if attr.len() > 255 {
        panic!("The test suite name `{attr}` exceeds the maximum length of 255 bytes")
    }

    let mut tokens: Vec<_> = ts.into_iter().collect();

    // Scan for the `mod` keyword.
    tokens
        .iter()
        .find_map(|token| match token {
            TokenTree::Ident(ident) => match ident.to_string().as_str() {
                "mod" => Some(true),
                _ => None,
            },
            _ => None,
        })
        .expect("`#[kunit_tests(test_name)]` attribute should only be applied to modules");

    // Retrieve the main body. The main body should be the last token tree.
    let body = match tokens.pop() {
        Some(TokenTree::Group(group)) if group.delimiter() == Delimiter::Brace => group,
        _ => panic!("Cannot locate main body of module"),
    };

    // Get the functions set as tests. Search for `[test]` -> `fn`.
    let mut body_it = body.stream().into_iter();
    let mut tests = Vec::new();
    while let Some(token) = body_it.next() {
        match token {
            TokenTree::Group(ident) if ident.to_string() == "[test]" => match body_it.next() {
                Some(TokenTree::Ident(ident)) if ident.to_string() == "fn" => {
                    let test_name = match body_it.next() {
                        Some(TokenTree::Ident(ident)) => ident.to_string(),
                        _ => continue,
                    };
                    tests.push(test_name);
                }
                _ => continue,
            },
            _ => (),
        }
    }

    // Add `#[cfg(CONFIG_KUNIT)]` before the module declaration.
    let config_kunit = "#[cfg(CONFIG_KUNIT)]".to_owned().parse().unwrap();
    tokens.insert(
        0,
        TokenTree::Group(Group::new(Delimiter::None, config_kunit)),
    );

    // Generate the test KUnit test suite and a test case for each `#[test]`.
    // The code generated for the following test module:
    //
    // ```
    // #[kunit_tests(kunit_test_suit_name)]
    // mod tests {
    //     #[test]
    //     fn foo() {
    //         assert_eq!(1, 1);
    //     }
    //
    //     #[test]
    //     fn bar() {
    //         assert_eq!(2, 2);
    //     }
    // }
    // ```
    //
    // Looks like:
    //
    // ```
    // unsafe extern "C" fn kunit_rust_wrapper_foo(_test: *mut kernel::bindings::kunit) { foo(); }
    // unsafe extern "C" fn kunit_rust_wrapper_bar(_test: *mut kernel::bindings::kunit) { bar(); }
    //
    // static mut TEST_CASES: [kernel::bindings::kunit_case; 3] = [
    //     kernel::kunit::kunit_case(kernel::c_str!("foo"), kunit_rust_wrapper_foo),
    //     kernel::kunit::kunit_case(kernel::c_str!("bar"), kunit_rust_wrapper_bar),
    //     kernel::kunit::kunit_case_null(),
    // ];
    //
    // kernel::kunit_unsafe_test_suite!(kunit_test_suit_name, TEST_CASES);
    // ```
    let mut kunit_macros = "".to_owned();
    let mut test_cases = "".to_owned();
    for test in &tests {
        let kunit_wrapper_fn_name = format!("kunit_rust_wrapper_{test}");
        let kunit_wrapper = format!(
            "unsafe extern \"C\" fn {kunit_wrapper_fn_name}(_test: *mut kernel::bindings::kunit) {{ {test}(); }}"
        );
        writeln!(kunit_macros, "{kunit_wrapper}").unwrap();
        writeln!(
            test_cases,
            "    kernel::kunit::kunit_case(kernel::c_str!(\"{test}\"), {kunit_wrapper_fn_name}),"
        )
        .unwrap();
    }

    writeln!(kunit_macros).unwrap();
    writeln!(
        kunit_macros,
        "static mut TEST_CASES: [kernel::bindings::kunit_case; {}] = [\n{test_cases}    kernel::kunit::kunit_case_null(),\n];",
        tests.len() + 1
    )
    .unwrap();

    writeln!(
        kunit_macros,
        "kernel::kunit_unsafe_test_suite!({attr}, TEST_CASES);"
    )
    .unwrap();

    // Remove the `#[test]` macros.
    // We do this at a token level, in order to preserve span information.
    let mut new_body = vec![];
    let mut body_it = body.stream().into_iter();

    while let Some(token) = body_it.next() {
        match token {
            TokenTree::Punct(ref c) if c.as_char() == '#' => match body_it.next() {
                Some(TokenTree::Group(group)) if group.to_string() == "[test]" => (),
                Some(next) => {
                    new_body.extend([token, next]);
                }
                _ => {
                    new_body.push(token);
                }
            },
            _ => {
                new_body.push(token);
            }
        }
    }

    let mut new_body = TokenStream::from_iter(new_body);
    new_body.extend::<TokenStream>(kunit_macros.parse().unwrap());

    tokens.push(TokenTree::Group(Group::new(Delimiter::Brace, new_body)));

    tokens.into_iter().collect()
}
