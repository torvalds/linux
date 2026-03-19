// SPDX-License-Identifier: GPL-2.0

//! Various assertions that happen during build-time.

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
/// [`static_assert!`] should be preferred to `build_assert!` whenever possible.
///
/// # Examples
///
/// These examples show that different types of [`assert!`] will trigger errors
/// at different stage of compilation. It is preferred to err as early as
/// possible, so [`static_assert!`] should be used whenever possible.
/// ```ignore
/// fn foo() {
///     static_assert!(1 > 1); // Compile-time error
///     build_assert!(1 > 1); // Build-time error
///     assert!(1 > 1); // Run-time error
/// }
/// ```
///
/// When the condition refers to generic parameters or parameters of an inline function,
/// [`static_assert!`] cannot be used. Use `build_assert!` in this scenario.
/// ```
/// fn foo<const N: usize>() {
///     // `static_assert!(N > 1);` is not allowed
///     build_assert!(N > 1); // Build-time check
///     assert!(N > 1); // Run-time check
/// }
/// ```
///
/// When a condition depends on a function argument, the function must be annotated with
/// `#[inline(always)]`. Without this attribute, the compiler may choose to not inline the
/// function, preventing it from optimizing out the error path.
/// ```
/// #[inline(always)]
/// fn bar(n: usize) {
///     // `static_assert!(n > 1);` is not allowed
///     build_assert!(n > 1); // Build-time check
///     assert!(n > 1); // Run-time check
/// }
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
