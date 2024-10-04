// SPDX-License-Identifier: Apache-2.0 OR MIT

//! Rust standard library vendored code.
//!
//! The contents of this file come from the Rust standard library, hosted in
//! the <https://github.com/rust-lang/rust> repository, licensed under
//! "Apache-2.0 OR MIT" and adapted for kernel use. For copyright details,
//! see <https://github.com/rust-lang/rust/blob/master/COPYRIGHT>.

/// [`std::dbg`], but using [`pr_info`] instead of [`eprintln`].
///
/// Prints and returns the value of a given expression for quick and dirty
/// debugging.
///
/// An example:
///
/// ```rust
/// let a = 2;
/// # #[expect(clippy::disallowed_macros)]
/// let b = dbg!(a * 2) + 1;
/// //      ^-- prints: [src/main.rs:3:9] a * 2 = 4
/// assert_eq!(b, 5);
/// ```
///
/// The macro works by using the `Debug` implementation of the type of
/// the given expression to print the value with [`printk`] along with the
/// source location of the macro invocation as well as the source code
/// of the expression.
///
/// Invoking the macro on an expression moves and takes ownership of it
/// before returning the evaluated expression unchanged. If the type
/// of the expression does not implement `Copy` and you don't want
/// to give up ownership, you can instead borrow with `dbg!(&expr)`
/// for some expression `expr`.
///
/// The `dbg!` macro works exactly the same in release builds.
/// This is useful when debugging issues that only occur in release
/// builds or when debugging in release mode is significantly faster.
///
/// Note that the macro is intended as a temporary debugging tool to be
/// used during development. Therefore, avoid committing `dbg!` macro
/// invocations into the kernel tree.
///
/// For debug output that is intended to be kept in the kernel tree,
/// use [`pr_debug`] and similar facilities instead.
///
/// # Stability
///
/// The exact output printed by this macro should not be relied upon
/// and is subject to future changes.
///
/// # Further examples
///
/// With a method call:
///
/// ```rust
/// # #[expect(clippy::disallowed_macros)]
/// fn foo(n: usize) {
///     if dbg!(n.checked_sub(4)).is_some() {
///         // ...
///     }
/// }
///
/// foo(3)
/// ```
///
/// This prints to the kernel log:
///
/// ```text,ignore
/// [src/main.rs:3:8] n.checked_sub(4) = None
/// ```
///
/// Naive factorial implementation:
///
/// ```rust
/// # #![expect(clippy::disallowed_macros)]
/// fn factorial(n: u32) -> u32 {
///     if dbg!(n <= 1) {
///         dbg!(1)
///     } else {
///         dbg!(n * factorial(n - 1))
///     }
/// }
///
/// dbg!(factorial(4));
/// ```
///
/// This prints to the kernel log:
///
/// ```text,ignore
/// [src/main.rs:3:8] n <= 1 = false
/// [src/main.rs:3:8] n <= 1 = false
/// [src/main.rs:3:8] n <= 1 = false
/// [src/main.rs:3:8] n <= 1 = true
/// [src/main.rs:4:9] 1 = 1
/// [src/main.rs:5:9] n * factorial(n - 1) = 2
/// [src/main.rs:5:9] n * factorial(n - 1) = 6
/// [src/main.rs:5:9] n * factorial(n - 1) = 24
/// [src/main.rs:11:1] factorial(4) = 24
/// ```
///
/// The `dbg!(..)` macro moves the input:
///
/// ```ignore
/// /// A wrapper around `usize` which importantly is not Copyable.
/// #[derive(Debug)]
/// struct NoCopy(usize);
///
/// let a = NoCopy(42);
/// let _ = dbg!(a); // <-- `a` is moved here.
/// let _ = dbg!(a); // <-- `a` is moved again; error!
/// ```
///
/// You can also use `dbg!()` without a value to just print the
/// file and line whenever it's reached.
///
/// Finally, if you want to `dbg!(..)` multiple values, it will treat them as
/// a tuple (and return it, too):
///
/// ```
/// # #![expect(clippy::disallowed_macros)]
/// assert_eq!(dbg!(1usize, 2u32), (1, 2));
/// ```
///
/// However, a single argument with a trailing comma will still not be treated
/// as a tuple, following the convention of ignoring trailing commas in macro
/// invocations. You can use a 1-tuple directly if you need one:
///
/// ```
/// # #![expect(clippy::disallowed_macros)]
/// assert_eq!(1, dbg!(1u32,)); // trailing comma ignored
/// assert_eq!((1,), dbg!((1u32,))); // 1-tuple
/// ```
///
/// [`std::dbg`]: https://doc.rust-lang.org/std/macro.dbg.html
/// [`eprintln`]: https://doc.rust-lang.org/std/macro.eprintln.html
/// [`printk`]: https://docs.kernel.org/core-api/printk-basics.html
/// [`pr_info`]: crate::pr_info!
/// [`pr_debug`]: crate::pr_debug!
#[macro_export]
macro_rules! dbg {
    // NOTE: We cannot use `concat!` to make a static string as a format argument
    // of `pr_info!` because `file!` could contain a `{` or
    // `$val` expression could be a block (`{ .. }`), in which case the `pr_info!`
    // will be malformed.
    () => {
        $crate::pr_info!("[{}:{}:{}]\n", ::core::file!(), ::core::line!(), ::core::column!())
    };
    ($val:expr $(,)?) => {
        // Use of `match` here is intentional because it affects the lifetimes
        // of temporaries - https://stackoverflow.com/a/48732525/1063961
        match $val {
            tmp => {
                $crate::pr_info!("[{}:{}:{}] {} = {:#?}\n",
                    ::core::file!(), ::core::line!(), ::core::column!(),
                    ::core::stringify!($val), &tmp);
                tmp
            }
        }
    };
    ($($val:expr),+ $(,)?) => {
        ($($crate::dbg!($val)),+,)
    };
}
