// SPDX-License-Identifier: GPL-2.0

//! Procedural macro to run KUnit tests using a user-space like syntax.
//!
//! Copyright (c) 2023 José Expósito <jose.exposito89@gmail.com>

use std::ffi::CString;

use proc_macro2::TokenStream;
use quote::{
    format_ident,
    quote,
    ToTokens, //
};
use syn::{
    parse_quote,
    Error,
    Ident,
    Item,
    ItemMod,
    LitCStr,
    Result, //
};

pub(crate) fn kunit_tests(test_suite: Ident, mut module: ItemMod) -> Result<TokenStream> {
    if test_suite.to_string().len() > 255 {
        return Err(Error::new_spanned(
            test_suite,
            "test suite names cannot exceed the maximum length of 255 bytes",
        ));
    }

    // We cannot handle modules that defer to another file (e.g. `mod foo;`).
    let Some((module_brace, module_items)) = module.content.take() else {
        Err(Error::new_spanned(
            module,
            "`#[kunit_tests(test_name)]` attribute should only be applied to inline modules",
        ))?
    };

    // Make the entire module gated behind `CONFIG_KUNIT`.
    module
        .attrs
        .insert(0, parse_quote!(#[cfg(CONFIG_KUNIT="y")]));

    let mut processed_items = Vec::new();
    let mut test_cases = Vec::new();

    // Generate the test KUnit test suite and a test case for each `#[test]`.
    //
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
    //     ::kernel::kunit::kunit_case(c"foo", kunit_rust_wrapper_foo),
    //     ::kernel::kunit::kunit_case(c"bar", kunit_rust_wrapper_bar),
    //     ::pin_init::zeroed(),
    // ];
    //
    // ::kernel::kunit_unsafe_test_suite!(kunit_test_suit_name, TEST_CASES);
    // ```
    //
    // Non-function items (e.g. imports) are preserved.
    for item in module_items {
        let Item::Fn(mut f) = item else {
            processed_items.push(item);
            continue;
        };

        // TODO: Replace below with `extract_if` when MSRV is bumped above 1.85.
        let before_len = f.attrs.len();
        f.attrs.retain(|attr| !attr.path().is_ident("test"));
        if f.attrs.len() == before_len {
            processed_items.push(Item::Fn(f));
            continue;
        }

        let test = f.sig.ident.clone();

        // Retrieve `#[cfg]` applied on the function which needs to be present on derived items too.
        let cfg_attrs: Vec<_> = f
            .attrs
            .iter()
            .filter(|attr| attr.path().is_ident("cfg"))
            .cloned()
            .collect();

        // Before the test, override usual `assert!` and `assert_eq!` macros with ones that call
        // KUnit instead.
        let test_str = test.to_string();
        let path = CString::new(crate::helpers::file()).expect("file path cannot contain NUL");
        processed_items.push(parse_quote! {
            #[allow(unused)]
            macro_rules! assert {
                ($cond:expr $(,)?) => {{
                    kernel::kunit_assert!(#test_str, #path, 0, $cond);
                }}
            }
        });
        processed_items.push(parse_quote! {
            #[allow(unused)]
            macro_rules! assert_eq {
                ($left:expr, $right:expr $(,)?) => {{
                    kernel::kunit_assert_eq!(#test_str, #path, 0, $left, $right);
                }}
            }
        });

        // Add back the test item.
        processed_items.push(Item::Fn(f));

        let kunit_wrapper_fn_name = format_ident!("kunit_rust_wrapper_{test}");
        let test_cstr = LitCStr::new(
            &CString::new(test_str.as_str()).expect("identifier cannot contain NUL"),
            test.span(),
        );
        processed_items.push(parse_quote! {
            unsafe extern "C" fn #kunit_wrapper_fn_name(_test: *mut ::kernel::bindings::kunit) {
                (*_test).status = ::kernel::bindings::kunit_status_KUNIT_SKIPPED;

                // Append any `cfg` attributes the user might have written on their tests so we
                // don't attempt to call them when they are `cfg`'d out. An extra `use` is used
                // here to reduce the length of the assert message.
                #(#cfg_attrs)*
                {
                    (*_test).status = ::kernel::bindings::kunit_status_KUNIT_SUCCESS;
                    use ::kernel::kunit::is_test_result_ok;
                    assert!(is_test_result_ok(#test()));
                }
            }
        });

        test_cases.push(quote!(
            ::kernel::kunit::kunit_case(#test_cstr, #kunit_wrapper_fn_name)
        ));
    }

    let num_tests_plus_1 = test_cases.len() + 1;
    processed_items.push(parse_quote! {
        static mut TEST_CASES: [::kernel::bindings::kunit_case; #num_tests_plus_1] = [
            #(#test_cases,)*
            ::pin_init::zeroed(),
        ];
    });
    processed_items.push(parse_quote! {
        ::kernel::kunit_unsafe_test_suite!(#test_suite, TEST_CASES);
    });

    module.content = Some((module_brace, processed_items));
    Ok(module.to_token_stream())
}
