// SPDX-License-Identifier: Apache-2.0 OR MIT

#![allow(clippy::disallowed_names)]

use pin_init::*;

#[pin_data]
struct SelfRef {
    part: &'str str,
    str: String,
}

fn use_self_ref() {
    stack_pin_init!(let foo = pin_init!(SelfRef {
        str: "hello world".to_owned(),
        part: &str[..5],
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
}

fn main() {
    use_self_ref();
}
