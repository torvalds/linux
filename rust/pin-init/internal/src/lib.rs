// SPDX-License-Identifier: Apache-2.0 OR MIT

/// Used to specify the pinning information of the fields of a struct.
///
/// This is somewhat similar in purpose as
/// [pin-project-lite](https://crates.io/crates/pin-project-lite).
/// Place this macro on a struct definition and then `#[pin]` in front of the attributes of each
/// field you want to structurally pin.
///
/// This macro enables the use of the [`pin_init!`] macro. When pin-initializing a `struct`,
/// then `#[pin]` directs the type of initializer that is required.
///
/// If your `struct` implements `Drop`, then you need to add `PinnedDrop` as arguments to this
/// macro, and change your `Drop` implementation to `PinnedDrop` annotated with
/// `#[`[`macro@pinned_drop`]`]`, since dropping pinned values requires extra care.
///
/// # Examples
///
/// ```ignore
/// # #![feature(lint_reasons)]
/// # use kernel::prelude::*;
/// # use std::{sync::Mutex, process::Command};
/// # use kernel::macros::pin_data;
/// #[pin_data]
/// struct DriverData {
///     #[pin]
///     queue: Mutex<KVec<Command>>,
///     buf: KBox<[u8; 1024 * 1024]>,
/// }
/// ```
///
/// ```ignore
/// # #![feature(lint_reasons)]
/// # use kernel::prelude::*;
/// # use std::{sync::Mutex, process::Command};
/// # use core::pin::Pin;
/// # pub struct Info;
/// # mod bindings {
/// #     pub unsafe fn destroy_info(_ptr: *mut super::Info) {}
/// # }
/// use kernel::macros::{pin_data, pinned_drop};
///
/// #[pin_data(PinnedDrop)]
/// struct DriverData {
///     #[pin]
///     queue: Mutex<KVec<Command>>,
///     buf: KBox<[u8; 1024 * 1024]>,
///     raw_info: *mut Info,
/// }
///
/// #[pinned_drop]
/// impl PinnedDrop for DriverData {
///     fn drop(self: Pin<&mut Self>) {
///         unsafe { bindings::destroy_info(self.raw_info) };
///     }
/// }
/// # fn main() {}
/// ```
///
/// [`pin_init!`]: ../kernel/macro.pin_init.html
//  ^ cannot use direct link, since `kernel` is not a dependency of `macros`.
#[proc_macro_attribute]
pub fn pin_data(inner: TokenStream, item: TokenStream) -> TokenStream {
    pin_data::pin_data(inner, item)
}

/// Used to implement `PinnedDrop` safely.
///
/// Only works on structs that are annotated via `#[`[`macro@pin_data`]`]`.
///
/// # Examples
///
/// ```ignore
/// # #![feature(lint_reasons)]
/// # use kernel::prelude::*;
/// # use macros::{pin_data, pinned_drop};
/// # use std::{sync::Mutex, process::Command};
/// # use core::pin::Pin;
/// # mod bindings {
/// #     pub struct Info;
/// #     pub unsafe fn destroy_info(_ptr: *mut Info) {}
/// # }
/// #[pin_data(PinnedDrop)]
/// struct DriverData {
///     #[pin]
///     queue: Mutex<KVec<Command>>,
///     buf: KBox<[u8; 1024 * 1024]>,
///     raw_info: *mut bindings::Info,
/// }
///
/// #[pinned_drop]
/// impl PinnedDrop for DriverData {
///     fn drop(self: Pin<&mut Self>) {
///         unsafe { bindings::destroy_info(self.raw_info) };
///     }
/// }
/// ```
#[proc_macro_attribute]
pub fn pinned_drop(args: TokenStream, input: TokenStream) -> TokenStream {
    pinned_drop::pinned_drop(args, input)
}

/// Derives the [`Zeroable`] trait for the given struct.
///
/// This can only be used for structs where every field implements the [`Zeroable`] trait.
///
/// # Examples
///
/// ```ignore
/// use kernel::macros::Zeroable;
///
/// #[derive(Zeroable)]
/// pub struct DriverData {
///     id: i64,
///     buf_ptr: *mut u8,
///     len: usize,
/// }
/// ```
#[proc_macro_derive(Zeroable)]
pub fn derive_zeroable(input: TokenStream) -> TokenStream {
    zeroable::derive(input)
}
