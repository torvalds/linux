// SPDX-License-Identifier: GPL-2.0

//! Safety related APIs.

/// Checks that a precondition of an unsafe function is followed.
///
/// The check is enabled at runtime if debug assertions (`CONFIG_RUST_DEBUG_ASSERTIONS`)
/// are enabled. Otherwise, this macro is a no-op.
///
/// # Examples
///
/// ```no_run
/// use kernel::unsafe_precondition_assert;
///
/// struct RawBuffer<T: Copy, const N: usize> {
///     data: [T; N],
/// }
///
/// impl<T: Copy, const N: usize> RawBuffer<T, N> {
///     /// # Safety
///     ///
///     /// The caller must ensure that `index` is less than `N`.
///     unsafe fn set_unchecked(&mut self, index: usize, value: T) {
///         unsafe_precondition_assert!(
///             index < N,
///             "RawBuffer::set_unchecked() requires index ({index}) < N ({N})"
///         );
///
///         // SAFETY: By the safety requirements of this function, `index` is valid.
///         unsafe {
///             *self.data.get_unchecked_mut(index) = value;
///         }
///     }
/// }
/// ```
///
/// # Panics
///
/// Panics if the expression is evaluated to [`false`] at runtime.
#[macro_export]
macro_rules! unsafe_precondition_assert {
    ($cond:expr $(,)?) => {
        $crate::unsafe_precondition_assert!(@inner $cond, ::core::stringify!($cond))
    };

    ($cond:expr, $($arg:tt)+) => {
        $crate::unsafe_precondition_assert!(@inner $cond, $crate::prelude::fmt!($($arg)+))
    };

    (@inner $cond:expr, $msg:expr) => {
        ::core::debug_assert!($cond, "unsafe precondition violated: {}", $msg)
    };
}
