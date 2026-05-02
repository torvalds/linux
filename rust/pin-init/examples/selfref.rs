// SPDX-License-Identifier: Apache-2.0 OR MIT

#![allow(clippy::disallowed_names)]

use pin_init::*;

#[pin_data]
struct SelfRef {
    #[uses('_: invariant)]
    not_cov: Box<dyn Fn(&'str str) -> bool + 'str>,

    part: &'str str,
    str: String,

    mut_part: &'mut_str mut str,
    #[borrowed(mut)]
    mut_str: String,
}

fn use_self_ref() {
    stack_pin_init!(let foo = pin_init!(SelfRef {
        str: "hello world".to_owned(),
        part: &str[..5],
        mut_str: "hello world".to_owned(),
        mut_part: &mut mut_str[..5],
        not_cov: Box::new(move |s| s == str),
    }));

    // Access via projection.
    println!("{}", foo.as_mut().project().part);

    // Access via accessor.
    println!("{}", foo.part());

    // Access via `with_project`, gives mutable reference.
    foo.as_mut().with_project(|proj| {
        *proj.part = &proj.str[5..];
    });

    println!("{}", foo.part());

    // Access fields that mutable borrow others are similar to those of shared borrow.
    println!("{}", foo.as_mut().project().mut_part);
    println!("{}", foo.mut_part());
    foo.as_mut().with_project(|proj| {
        proj.mut_part.make_ascii_uppercase();
    });

    // Access non-covariant type using `with_` accessor.
    foo.with_not_cov(|not_cov| {
        not_cov("");
    });

    // Access non-covariant type using `with_project`.
    foo.as_mut().with_project(|proj| (proj.not_cov)(proj.str));
}

fn main() {
    use_self_ref();
}
