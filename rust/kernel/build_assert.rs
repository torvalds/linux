// SPDX-License-Identifier: GPL-2.0

//! Various assertions that happen during build-time.
//!
//! There are three types of build-time assertions that you can use:
//! - [`static_assert!`]
//! - [`const_assert!`]
//! - [`build_assert!`]
//!
//! The ones towards the bottom of the list are more expressive, while the ones towards the top of
//! the list are more robust and trigger earlier in the compilation pipeline. Therefore, you should
//! prefer the ones towards the top of the list wherever possible.
//!
//! # Choosing the correct assertion
//!
//! If you're asserting outside any bodies (e.g. initializers or function bodies), you should use
//! [`static_assert!`] as it is the only assertion that can be used in that context.
//!
//! Inside bodies, if your assertion condition does not depend on any variable or generics, you
//! should use [`static_assert!`]. If the condition depends on generics, but not variables
//! (including function arguments), you should use [`const_assert!`]. Otherwise, use
//! [`build_assert!`]. The same is true regardless if the function is `const fn`.
//!
//! ```
//! // Outside any bodies.
//! static_assert!(core::mem::size_of::<u8>() == 1);
//! // `const_assert!` and `build_assert!` cannot be used here, they will fail to compile.
//!
//! #[inline(always)]
//! fn foo<const N: usize>(v: usize) {
//!     static_assert!(core::mem::size_of::<u8>() == 1); // Preferred.
//!     const_assert!(core::mem::size_of::<u8>() == 1); // Discouraged.
//!     build_assert!(core::mem::size_of::<u8>() == 1); // Discouraged.
//!
//!     // `static_assert!(N > 1);` is not allowed.
//!     const_assert!(N > 1); // Preferred.
//!     build_assert!(N > 1); // Discouraged.
//!
//!     // `static_assert!(v > 1);` is not allowed.
//!     // `const_assert!(v > 1);` is not allowed.
//!     build_assert!(v > 1); // Works.
//! }
//! ```
//!
//! # Detailed behavior
//!
//! `static_assert!()` is equivalent to `static_assert` in C. It requires `expr` to be a constant
//! expression. This expression cannot refer to any generics. A `static_assert!(expr)` in a program
//! is always evaluated, regardless if the function it appears in is used or not. This is also the
//! only usable assertion outside a body.
//!
//! `const_assert!()` has no direct C equivalence. It is a more powerful version of
//! `static_assert!()`, where it may refer to generics in a function. Note that due to the ability
//! to refer to generics, the assertion is tied to a specific instance of a function. So if it is
//! used in a generic function that is not instantiated, the assertion will not be checked. For this
//! reason, `static_assert!()` is preferred wherever possible.
//!
//! `build_assert!()` is equivalent to `BUILD_BUG_ON`. It is even more powerful than
//! `const_assert!()` because it can be used to check tautologies that depend on runtime value (this
//! is the same as `BUILD_BUG_ON`). However, the assertion failure mechanism can possibly be
//! undefined symbols and linker errors, it is not developer friendly to debug, so it is recommended
//! to avoid it and prefer other two assertions where possible.

pub use crate::{
    build_assert,
    build_error,
    const_assert,
    static_assert, //
};

#[doc(hidden)]
pub use build_error::build_error;

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
/// You cannot refer to generics or variables with [`static_assert!`]. If you need to refer to
/// generics, use [`const_assert!`]; if you need to refer to variables, use [`build_assert!`]. See
/// the [module documentation](self).
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

/// Assertion during constant evaluation.
///
/// This is a more powerful version of [`static_assert!`] that can refer to generics inside
/// functions or implementation blocks. However, it also has a limitation where it can only appear
/// in places where statements can appear; for example, you cannot use it as an item in the module.
///
/// [`static_assert!`] should be preferred if no generics are referred to in the condition. You
/// cannot refer to variables with [`const_assert!`] (even inside `const fn`); if you need the
/// capability, use [`build_assert!`]. See the [module documentation](self).
///
/// # Examples
///
/// ```
/// fn foo<const N: usize>() {
///     const_assert!(N > 1);
/// }
///
/// fn bar<T>() {
///     const_assert!(size_of::<T>() > 0, "T cannot be ZST");
/// }
/// ```
#[macro_export]
macro_rules! const_assert {
    ($condition:expr $(,$arg:literal)?) => {
        const { ::core::assert!($condition $(,$arg)?) };
    };
}

/// Fails the build if the code path calling `build_error!` can possibly be executed.
///
/// If the macro is executed in const context, `build_error!` will panic.
/// If the compiler or optimizer cannot guarantee that `build_error!` can never
/// be called, a build error will be triggered.
///
/// # Examples
///
/// ```
/// #[inline]
/// fn foo(a: usize) -> usize {
///     a.checked_add(1).unwrap_or_else(|| build_error!("overflow"))
/// }
///
/// assert_eq!(foo(usize::MAX - 1), usize::MAX); // OK.
/// // foo(usize::MAX); // Fails to compile.
/// ```
#[macro_export]
macro_rules! build_error {
    () => {{
        $crate::build_assert::build_error("")
    }};
    ($msg:expr) => {{
        $crate::build_assert::build_error($msg)
    }};
}

/// Asserts that a boolean expression is `true` at compile time.
///
/// If the condition is evaluated to `false` in const context, `build_assert!`
/// will panic. If the compiler or optimizer cannot guarantee the condition will
/// be evaluated to `true`, a build error will be triggered.
///
/// When a condition depends on a function argument, the function must be annotated with
/// `#[inline(always)]`. Without this attribute, the compiler may choose to not inline the
/// function, preventing it from optimizing out the error path.
///
/// If the assertion condition does not depend on any variables or generics, you should use
/// [`static_assert!`]. If the assertion condition does not depend on variables, but does depend on
/// generics, you should use [`const_assert!`]. See the [module documentation](self).
///
/// # Examples
///
/// ```
/// #[inline(always)] // Important.
/// fn bar(n: usize) {
///     build_assert!(n > 1);
/// }
///
/// fn foo() {
///     bar(2);
/// }
///
/// #[inline(always)] // Important.
/// const fn const_bar(n: usize) {
///     build_assert!(n > 1);
/// }
///
/// const _: () = const_bar(2);
/// ```
#[macro_export]
macro_rules! build_assert {
    ($cond:expr $(,)?) => {{
        if !$cond {
            $crate::build_assert::build_error(concat!("assertion failed: ", stringify!($cond)));
        }
    }};
    ($cond:expr, $msg:expr) => {{
        if !$cond {
            $crate::build_assert::build_error($msg);
        }
    }};
}
