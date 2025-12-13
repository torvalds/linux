// SPDX-License-Identifier: GPL-2.0

//! Procedural macro to run KUnit tests using a user-space like syntax.
//!
//! Copyright (c) 2023 José Expósito <jose.exposito89@gmail.com>

use proc_macro::{Delimiter, Group, TokenStream, TokenTree};
use std::collections::HashMap;
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
    let mut attributes: HashMap<String, TokenStream> = HashMap::new();
    while let Some(token) = body_it.next() {
        match token {
            TokenTree::Punct(ref p) if p.as_char() == '#' => match body_it.next() {
                Some(TokenTree::Group(g)) if g.delimiter() == Delimiter::Bracket => {
                    if let Some(TokenTree::Ident(name)) = g.stream().into_iter().next() {
                        // Collect attributes because we need to find which are tests. We also
                        // need to copy `cfg` attributes so tests can be conditionally enabled.
                        attributes
                            .entry(name.to_string())
                            .or_default()
                            .extend([token, TokenTree::Group(g)]);
                    }
                    continue;
                }
                _ => (),
            },
            TokenTree::Ident(i) if i.to_string() == "fn" && attributes.contains_key("test") => {
                if let Some(TokenTree::Ident(test_name)) = body_it.next() {
                    tests.push((test_name, attributes.remove("cfg").unwrap_or_default()))
                }
            }

            _ => (),
        }
        attributes.clear();
    }

    // Add `#[cfg(CONFIG_KUNIT="y")]` before the module declaration.
    let config_kunit = "#[cfg(CONFIG_KUNIT=\"y\")]".to_owned().parse().unwrap();
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
    // unsafe extern "C" fn kunit_rust_wrapper_foo(_test: *mut ::kernel::bindings::kunit) { foo(); }
    // unsafe extern "C" fn kunit_rust_wrapper_bar(_test: *mut ::kernel::bindings::kunit) { bar(); }
    //
    // static mut TEST_CASES: [::kernel::bindings::kunit_case; 3] = [
    //     ::kernel::kunit::kunit_case(::kernel::c_str!("foo"), kunit_rust_wrapper_foo),
    //     ::kernel::kunit::kunit_case(::kernel::c_str!("bar"), kunit_rust_wrapper_bar),
    //     ::kernel::kunit::kunit_case_null(),
    // ];
    //
    // ::kernel::kunit_unsafe_test_suite!(kunit_test_suit_name, TEST_CASES);
    // ```
    let mut kunit_macros = "".to_owned();
    let mut test_cases = "".to_owned();
    let mut assert_macros = "".to_owned();
    let path = crate::helpers::file();
    let num_tests = tests.len();
    for (test, cfg_attr) in tests {
        let kunit_wrapper_fn_name = format!("kunit_rust_wrapper_{test}");
        // Append any `cfg` attributes the user might have written on their tests so we don't
        // attempt to call them when they are `cfg`'d out. An extra `use` is used here to reduce
        // the length of the assert message.
        let kunit_wrapper = format!(
            r#"unsafe extern "C" fn {kunit_wrapper_fn_name}(_test: *mut ::kernel::bindings::kunit)
            {{
                (*_test).status = ::kernel::bindings::kunit_status_KUNIT_SKIPPED;
                {cfg_attr} {{
                    (*_test).status = ::kernel::bindings::kunit_status_KUNIT_SUCCESS;
                    use ::kernel::kunit::is_test_result_ok;
                    assert!(is_test_result_ok({test}()));
                }}
            }}"#,
        );
        writeln!(kunit_macros, "{kunit_wrapper}").unwrap();
        writeln!(
            test_cases,
            "    ::kernel::kunit::kunit_case(::kernel::c_str!(\"{test}\"), {kunit_wrapper_fn_name}),"
        )
        .unwrap();
        writeln!(
            assert_macros,
            r#"
/// Overrides the usual [`assert!`] macro with one that calls KUnit instead.
#[allow(unused)]
macro_rules! assert {{
    ($cond:expr $(,)?) => {{{{
        kernel::kunit_assert!("{test}", "{path}", 0, $cond);
    }}}}
}}

/// Overrides the usual [`assert_eq!`] macro with one that calls KUnit instead.
#[allow(unused)]
macro_rules! assert_eq {{
    ($left:expr, $right:expr $(,)?) => {{{{
        kernel::kunit_assert_eq!("{test}", "{path}", 0, $left, $right);
    }}}}
}}
        "#
        )
        .unwrap();
    }

    writeln!(kunit_macros).unwrap();
    writeln!(
        kunit_macros,
        "static mut TEST_CASES: [::kernel::bindings::kunit_case; {}] = [\n{test_cases}    ::kernel::kunit::kunit_case_null(),\n];",
        num_tests + 1
    )
    .unwrap();

    writeln!(
        kunit_macros,
        "::kernel::kunit_unsafe_test_suite!({attr}, TEST_CASES);"
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

    let mut final_body = TokenStream::new();
    final_body.extend::<TokenStream>(assert_macros.parse().unwrap());
    final_body.extend(new_body);
    final_body.extend::<TokenStream>(kunit_macros.parse().unwrap());

    tokens.push(TokenTree::Group(Group::new(Delimiter::Brace, final_body)));

    tokens.into_iter().collect()
}
