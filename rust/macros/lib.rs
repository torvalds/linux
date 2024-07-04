// SPDX-License-Identifier: GPL-2.0

//! Crate for all kernel procedural macros.

// When fixdep scans this, it will find this string `CONFIG_RUSTC_VERSION_TEXT`
// and thus add a dependency on `include/config/RUSTC_VERSION_TEXT`, which is
// touched by Kconfig when the version string from the compiler changes.

#[macro_use]
mod quote;
mod concat_idents;
mod helpers;
mod module;
mod paste;
mod pin_data;
mod pinned_drop;
mod vtable;
mod zeroable;

use proc_macro::TokenStream;

/// Declares a kernel module.
///
/// The `type` argument should be a type which implements the [`Module`]
/// trait. Also accepts various forms of kernel metadata.
///
/// C header: [`include/linux/moduleparam.h`](srctree/include/linux/moduleparam.h)
///
/// [`Module`]: ../kernel/trait.Module.html
///
/// # Examples
///
/// ```ignore
/// use kernel::prelude::*;
///
/// module!{
///     type: MyModule,
///     name: "my_kernel_module",
///     author: "Rust for Linux Contributors",
///     description: "My very own kernel module!",
///     license: "GPL",
///     alias: ["alternate_module_name"],
/// }
///
/// struct MyModule;
///
/// impl kernel::Module for MyModule {
///     fn init() -> Result<Self> {
///         // If the parameter is writeable, then the kparam lock must be
///         // taken to read the parameter:
///         {
///             let lock = THIS_MODULE.kernel_param_lock();
///             pr_info!("i32 param is:  {}\n", writeable_i32.read(&lock));
///         }
///         // If the parameter is read only, it can be read without locking
///         // the kernel parameters:
///         pr_info!("i32 param is:  {}\n", my_i32.read());
///         Ok(Self)
///     }
/// }
/// ```
///
/// ## Firmware
///
/// The following example shows how to declare a kernel module that needs
/// to load binary firmware files. You need to specify the file names of
/// the firmware in the `firmware` field. The information is embedded
/// in the `modinfo` section of the kernel module. For example, a tool to
/// build an initramfs uses this information to put the firmware files into
/// the initramfs image.
///
/// ```ignore
/// use kernel::prelude::*;
///
/// module!{
///     type: MyDeviceDriverModule,
///     name: "my_device_driver_module",
///     author: "Rust for Linux Contributors",
///     description: "My device driver requires firmware",
///     license: "GPL",
///     firmware: ["my_device_firmware1.bin", "my_device_firmware2.bin"],
/// }
///
/// struct MyDeviceDriverModule;
///
/// impl kernel::Module for MyDeviceDriverModule {
///     fn init() -> Result<Self> {
///         Ok(Self)
///     }
/// }
/// ```
///
/// # Supported argument types
///   - `type`: type which implements the [`Module`] trait (required).
///   - `name`: ASCII string literal of the name of the kernel module (required).
///   - `author`: string literal of the author of the kernel module.
///   - `description`: string literal of the description of the kernel module.
///   - `license`: ASCII string literal of the license of the kernel module (required).
///   - `alias`: array of ASCII string literals of the alias names of the kernel module.
///   - `firmware`: array of ASCII string literals of the firmware files of
///     the kernel module.
#[proc_macro]
pub fn module(ts: TokenStream) -> TokenStream {
    module::module(ts)
}

/// Declares or implements a vtable trait.
///
/// Linux's use of pure vtables is very close to Rust traits, but they differ
/// in how unimplemented functions are represented. In Rust, traits can provide
/// default implementation for all non-required methods (and the default
/// implementation could just return `Error::EINVAL`); Linux typically use C
/// `NULL` pointers to represent these functions.
///
/// This attribute closes that gap. A trait can be annotated with the
/// `#[vtable]` attribute. Implementers of the trait will then also have to
/// annotate the trait with `#[vtable]`. This attribute generates a `HAS_*`
/// associated constant bool for each method in the trait that is set to true if
/// the implementer has overridden the associated method.
///
/// For a trait method to be optional, it must have a default implementation.
/// This is also the case for traits annotated with `#[vtable]`, but in this
/// case the default implementation will never be executed. The reason for this
/// is that the functions will be called through function pointers installed in
/// C side vtables. When an optional method is not implemented on a `#[vtable]`
/// trait, a NULL entry is installed in the vtable. Thus the default
/// implementation is never called. Since these traits are not designed to be
/// used on the Rust side, it should not be possible to call the default
/// implementation. This is done to ensure that we call the vtable methods
/// through the C vtable, and not through the Rust vtable. Therefore, the
/// default implementation should call `kernel::build_error`, which prevents
/// calls to this function at compile time:
///
/// ```compile_fail
/// # // Intentionally missing `use`s to simplify `rusttest`.
/// kernel::build_error(VTABLE_DEFAULT_ERROR)
/// ```
///
/// Note that you might need to import [`kernel::error::VTABLE_DEFAULT_ERROR`].
///
/// This macro should not be used when all functions are required.
///
/// # Examples
///
/// ```ignore
/// use kernel::error::VTABLE_DEFAULT_ERROR;
/// use kernel::prelude::*;
///
/// // Declares a `#[vtable]` trait
/// #[vtable]
/// pub trait Operations: Send + Sync + Sized {
///     fn foo(&self) -> Result<()> {
///         kernel::build_error(VTABLE_DEFAULT_ERROR)
///     }
///
///     fn bar(&self) -> Result<()> {
///         kernel::build_error(VTABLE_DEFAULT_ERROR)
///     }
/// }
///
/// struct Foo;
///
/// // Implements the `#[vtable]` trait
/// #[vtable]
/// impl Operations for Foo {
///     fn foo(&self) -> Result<()> {
/// #        Err(EINVAL)
///         // ...
///     }
/// }
///
/// assert_eq!(<Foo as Operations>::HAS_FOO, true);
/// assert_eq!(<Foo as Operations>::HAS_BAR, false);
/// ```
///
/// [`kernel::error::VTABLE_DEFAULT_ERROR`]: ../kernel/error/constant.VTABLE_DEFAULT_ERROR.html
#[proc_macro_attribute]
pub fn vtable(attr: TokenStream, ts: TokenStream) -> TokenStream {
    vtable::vtable(attr, ts)
}

/// Concatenate two identifiers.
///
/// This is useful in macros that need to declare or reference items with names
/// starting with a fixed prefix and ending in a user specified name. The resulting
/// identifier has the span of the second argument.
///
/// # Examples
///
/// ```ignore
/// use kernel::macro::concat_idents;
///
/// macro_rules! pub_no_prefix {
///     ($prefix:ident, $($newname:ident),+) => {
///         $(pub(crate) const $newname: u32 = kernel::macros::concat_idents!($prefix, $newname);)+
///     };
/// }
///
/// pub_no_prefix!(
///     binder_driver_return_protocol_,
///     BR_OK,
///     BR_ERROR,
///     BR_TRANSACTION,
///     BR_REPLY,
///     BR_DEAD_REPLY,
///     BR_TRANSACTION_COMPLETE,
///     BR_INCREFS,
///     BR_ACQUIRE,
///     BR_RELEASE,
///     BR_DECREFS,
///     BR_NOOP,
///     BR_SPAWN_LOOPER,
///     BR_DEAD_BINDER,
///     BR_CLEAR_DEATH_NOTIFICATION_DONE,
///     BR_FAILED_REPLY
/// );
///
/// assert_eq!(BR_OK, binder_driver_return_protocol_BR_OK);
/// ```
#[proc_macro]
pub fn concat_idents(ts: TokenStream) -> TokenStream {
    concat_idents::concat_idents(ts)
}

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
/// ```rust,ignore
/// #[pin_data]
/// struct DriverData {
///     #[pin]
///     queue: Mutex<KVec<Command>>,
///     buf: KBox<[u8; 1024 * 1024]>,
/// }
/// ```
///
/// ```rust,ignore
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
/// ```rust,ignore
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
/// ```
#[proc_macro_attribute]
pub fn pinned_drop(args: TokenStream, input: TokenStream) -> TokenStream {
    pinned_drop::pinned_drop(args, input)
}

/// Paste identifiers together.
///
/// Within the `paste!` macro, identifiers inside `[<` and `>]` are concatenated together to form a
/// single identifier.
///
/// This is similar to the [`paste`] crate, but with pasting feature limited to identifiers and
/// literals (lifetimes and documentation strings are not supported). There is a difference in
/// supported modifiers as well.
///
/// # Example
///
/// ```ignore
/// use kernel::macro::paste;
///
/// macro_rules! pub_no_prefix {
///     ($prefix:ident, $($newname:ident),+) => {
///         paste! {
///             $(pub(crate) const $newname: u32 = [<$prefix $newname>];)+
///         }
///     };
/// }
///
/// pub_no_prefix!(
///     binder_driver_return_protocol_,
///     BR_OK,
///     BR_ERROR,
///     BR_TRANSACTION,
///     BR_REPLY,
///     BR_DEAD_REPLY,
///     BR_TRANSACTION_COMPLETE,
///     BR_INCREFS,
///     BR_ACQUIRE,
///     BR_RELEASE,
///     BR_DECREFS,
///     BR_NOOP,
///     BR_SPAWN_LOOPER,
///     BR_DEAD_BINDER,
///     BR_CLEAR_DEATH_NOTIFICATION_DONE,
///     BR_FAILED_REPLY
/// );
///
/// assert_eq!(BR_OK, binder_driver_return_protocol_BR_OK);
/// ```
///
/// # Modifiers
///
/// For each identifier, it is possible to attach one or multiple modifiers to
/// it.
///
/// Currently supported modifiers are:
/// * `span`: change the span of concatenated identifier to the span of the specified token. By
///   default the span of the `[< >]` group is used.
/// * `lower`: change the identifier to lower case.
/// * `upper`: change the identifier to upper case.
///
/// ```ignore
/// use kernel::macro::paste;
///
/// macro_rules! pub_no_prefix {
///     ($prefix:ident, $($newname:ident),+) => {
///         kernel::macros::paste! {
///             $(pub(crate) const fn [<$newname:lower:span>]() -> u32 { [<$prefix $newname:span>] })+
///         }
///     };
/// }
///
/// pub_no_prefix!(
///     binder_driver_return_protocol_,
///     BR_OK,
///     BR_ERROR,
///     BR_TRANSACTION,
///     BR_REPLY,
///     BR_DEAD_REPLY,
///     BR_TRANSACTION_COMPLETE,
///     BR_INCREFS,
///     BR_ACQUIRE,
///     BR_RELEASE,
///     BR_DECREFS,
///     BR_NOOP,
///     BR_SPAWN_LOOPER,
///     BR_DEAD_BINDER,
///     BR_CLEAR_DEATH_NOTIFICATION_DONE,
///     BR_FAILED_REPLY
/// );
///
/// assert_eq!(br_ok(), binder_driver_return_protocol_BR_OK);
/// ```
///
/// # Literals
///
/// Literals can also be concatenated with other identifiers:
///
/// ```ignore
/// macro_rules! create_numbered_fn {
///     ($name:literal, $val:literal) => {
///         kernel::macros::paste! {
///             fn [<some_ $name _fn $val>]() -> u32 { $val }
///         }
///     };
/// }
///
/// create_numbered_fn!("foo", 100);
///
/// assert_eq!(some_foo_fn100(), 100)
/// ```
///
/// [`paste`]: https://docs.rs/paste/
#[proc_macro]
pub fn paste(input: TokenStream) -> TokenStream {
    let mut tokens = input.into_iter().collect();
    paste::expand(&mut tokens);
    tokens.into_iter().collect()
}

/// Derives the [`Zeroable`] trait for the given struct.
///
/// This can only be used for structs where every field implements the [`Zeroable`] trait.
///
/// # Examples
///
/// ```rust,ignore
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
