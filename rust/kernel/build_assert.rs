// SPDX-License-Identifier: GPL-2.0

//! Build-time assert.

#[doc(hidden)]
pub use build_error::build_error;

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
///
/// #[inline]
/// fn bar(n: usize) {
///     // `static_assert!(n > 1);` is not allowed
///     build_assert!(n > 1); // Build-time check
///     assert!(n > 1); // Run-time check
/// }
/// ```
///
/// [`static_assert!`]: crate::static_assert!
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
