// SPDX-License-Identifier: GPL-2.0

//! Build-time error.
//!
//! This crate provides a [const function][const-functions] `build_error`, which will panic in
//! compile-time if executed in [const context][const-context], and will cause a build error
//! if not executed at compile time and the optimizer does not optimise away the call.
//!
//! It is used by `build_assert!` in the kernel crate, allowing checking of
//! conditions that could be checked statically, but could not be enforced in
//! Rust yet (e.g. perform some checks in [const functions][const-functions], but those
//! functions could still be called in the runtime).
//!
//! For details on constant evaluation in Rust, please see the [Reference][const-eval].
//!
//! [const-eval]: https://doc.rust-lang.org/reference/const_eval.html
//! [const-functions]: https://doc.rust-lang.org/reference/const_eval.html#const-functions
//! [const-context]: https://doc.rust-lang.org/reference/const_eval.html#const-context

#![no_std]

/// Panics if executed in [const context][const-context], or triggers a build error if not.
///
/// [const-context]: https://doc.rust-lang.org/reference/const_eval.html#const-context
#[inline(never)]
#[cold]
#[export_name = "rust_build_error"]
#[track_caller]
pub const fn build_error(msg: &'static str) -> ! {
    panic!("{}", msg);
}
