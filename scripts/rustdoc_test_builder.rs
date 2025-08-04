// SPDX-License-Identifier: GPL-2.0

//! Test builder for `rustdoc`-generated tests.
//!
//! This script is a hack to extract the test from `rustdoc`'s output. Ideally, `rustdoc` would
//! have an option to generate this information instead, e.g. as JSON output.
//!
//! The `rustdoc`-generated test names look like `{file}_{line}_{number}`, e.g.
//! `...path_rust_kernel_sync_arc_rs_42_0`. `number` is the "test number", needed in cases like
//! a macro that expands into items with doctests is invoked several times within the same line.
//!
//! However, since these names are used for bisection in CI, the line number makes it not stable
//! at all. In the future, we would like `rustdoc` to give us the Rust item path associated with
//! the test, plus a "test number" (for cases with several examples per item) and generate a name
//! from that. For the moment, we generate ourselves a new name, `{file}_{number}` instead, in
//! the `gen` script (done there since we need to be aware of all the tests in a given file).

use std::io::Read;

fn main() {
    let mut stdin = std::io::stdin().lock();
    let mut body = String::new();
    stdin.read_to_string(&mut body).unwrap();

    // Find the generated function name looking for the inner function inside `main()`.
    //
    // The line we are looking for looks like one of the following:
    //
    // ```
    // fn main() { #[allow(non_snake_case)] fn _doctest_main_rust_kernel_file_rs_28_0() {
    // fn main() { #[allow(non_snake_case)] fn _doctest_main_rust_kernel_file_rs_37_0() -> Result<(), impl ::core::fmt::Debug> {
    // ```
    //
    // It should be unlikely that doctest code matches such lines (when code is formatted properly).
    let rustdoc_function_name = body
        .lines()
        .find_map(|line| {
            Some(
                line.split_once("fn main() {")?
                    .1
                    .split_once("fn ")?
                    .1
                    .split_once("()")?
                    .0,
            )
            .filter(|x| x.chars().all(|c| c.is_alphanumeric() || c == '_'))
        })
        .expect("No test function found in `rustdoc`'s output.");

    // Qualify `Result` to avoid the collision with our own `Result` coming from the prelude.
    let body = body.replace(
        &format!("{rustdoc_function_name}() -> Result<(), impl ::core::fmt::Debug> {{"),
        &format!(
            "{rustdoc_function_name}() -> ::core::result::Result<(), impl ::core::fmt::Debug> {{"
        ),
    );

    // For tests that get generated with `Result`, like above, `rustdoc` generates an `unwrap()` on
    // the return value to check there were no returned errors. Instead, we use our assert macro
    // since we want to just fail the test, not panic the kernel.
    //
    // We save the result in a variable so that the failed assertion message looks nicer.
    let body = body.replace(
        &format!("}} {rustdoc_function_name}().unwrap() }}"),
        &format!("}} let test_return_value = {rustdoc_function_name}(); assert!(test_return_value.is_ok()); }}"),
    );

    // Figure out a smaller test name based on the generated function name.
    let name = rustdoc_function_name.split_once("_rust_kernel_").unwrap().1;

    let path = format!("rust/test/doctests/kernel/{name}");

    std::fs::write(path, body.as_bytes()).unwrap();
}
