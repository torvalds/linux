// SPDX-License-Identifier: GPL-2.0

//! Rust single host program sample.

mod a;
mod b;

fn main() {
    println!("Hello world!");

    a::f(b::CONSTANT);
}
