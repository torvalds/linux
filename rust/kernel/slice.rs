// SPDX-License-Identifier: GPL-2.0

//! Additional (and temporary) slice helpers.

/// Extension trait providing a portable version of [`as_flattened`] and
/// [`as_flattened_mut`].
///
/// In Rust 1.80, the previously unstable `slice::flatten` family of methods
/// have been stabilized and renamed from `flatten` to `as_flattened`.
///
/// This creates an issue for as long as the MSRV is < 1.80, as the same functionality is provided
/// by different methods depending on the compiler version.
///
/// This extension trait solves this by abstracting `as_flatten` and calling the correct method
/// depending on the Rust version.
///
/// This trait can be removed once the MSRV passes 1.80.
///
/// [`as_flattened`]: https://doc.rust-lang.org/std/primitive.slice.html#method.as_flattened
/// [`as_flattened_mut`]: https://doc.rust-lang.org/std/primitive.slice.html#method.as_flattened_mut
#[cfg(not(CONFIG_RUSTC_HAS_SLICE_AS_FLATTENED))]
pub trait AsFlattened<T> {
    /// Takes a `&[[T; N]]` and flattens it to a `&[T]`.
    ///
    /// This is an portable layer on top of [`as_flattened`]; see its documentation for details.
    ///
    /// [`as_flattened`]: https://doc.rust-lang.org/std/primitive.slice.html#method.as_flattened
    fn as_flattened(&self) -> &[T];

    /// Takes a `&mut [[T; N]]` and flattens it to a `&mut [T]`.
    ///
    /// This is an portable layer on top of [`as_flattened_mut`]; see its documentation for details.
    ///
    /// [`as_flattened_mut`]: https://doc.rust-lang.org/std/primitive.slice.html#method.as_flattened_mut
    fn as_flattened_mut(&mut self) -> &mut [T];
}

#[cfg(not(CONFIG_RUSTC_HAS_SLICE_AS_FLATTENED))]
impl<T, const N: usize> AsFlattened<T> for [[T; N]] {
    #[allow(clippy::incompatible_msrv)]
    fn as_flattened(&self) -> &[T] {
        self.flatten()
    }

    #[allow(clippy::incompatible_msrv)]
    fn as_flattened_mut(&mut self) -> &mut [T] {
        self.flatten_mut()
    }
}
