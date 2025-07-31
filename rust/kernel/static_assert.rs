// SPDX-License-Identifier: GPL-2.0

//! Static assert.

/// Static assert (i.e. compile-time assert).
///
/// Similar to C11 [`_Static_assert`] and C++11 [`static_assert`].
///
/// An optional panic message can be supplied after the expression.
/// Currently only a string literal without formatting is supported
/// due to constness limitations of the [`assert!`] macro.
///
/// The feature may be added to Rust in the future: see [RFC 2790].
///
/// [`_Static_assert`]: https://en.cppreference.com/w/c/language/_Static_assert
/// [`static_assert`]: https://en.cppreference.com/w/cpp/language/static_assert
/// [RFC 2790]: https://github.com/rust-lang/rfcs/issues/2790
///
/// # Examples
///
/// ```
/// static_assert!(42 > 24);
/// static_assert!(core::mem::size_of::<u8>() == 1);
///
/// const X: &[u8] = b"bar";
/// static_assert!(X[1] == b'a');
///
/// const fn f(x: i32) -> i32 {
///     x + 2
/// }
/// static_assert!(f(40) == 42);
/// static_assert!(f(40) == 42, "f(x) must add 2 to the given input.");
/// ```
#[macro_export]
macro_rules! static_assert {
    ($condition:expr $(,$arg:literal)?) => {
        const _: () = ::core::assert!($condition $(,$arg)?);
    };
}
