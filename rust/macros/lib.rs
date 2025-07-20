// SPDX-License-Identifier: GPL-2.0

//! Crate for all kernel procedural macros.

// When fixdep scans this, it will find this string `CONFIG_RUSTC_VERSION_TEXT`
// and thus add a dependency on `include/config/RUSTC_VERSION_TEXT`, which is
// touched by Kconfig when the version string from the compiler changes.

// Stable since Rust 1.88.0 under a different name, `proc_macro_span_file`,
// which was added in Rust 1.88.0. This is why `cfg_attr` is used here, i.e.
// to avoid depending on the full `proc_macro_span` on Rust >= 1.88.0.
#![cfg_attr(not(CONFIG_RUSTC_HAS_SPAN_FILE), feature(proc_macro_span))]

#[macro_use]
mod quote;
mod concat_idents;
mod export;
mod helpers;
mod kunit;
mod module;
mod paste;
mod vtable;

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
/// ```
/// use kernel::prelude::*;
///
/// module!{
///     type: MyModule,
///     name: "my_kernel_module",
///     authors: ["Rust for Linux Contributors"],
///     description: "My very own kernel module!",
///     license: "GPL",
///     alias: ["alternate_module_name"],
/// }
///
/// struct MyModule(i32);
///
/// impl kernel::Module for MyModule {
///     fn init(_module: &'static ThisModule) -> Result<Self> {
///         let foo: i32 = 42;
///         pr_info!("I contain:  {}\n", foo);
///         Ok(Self(foo))
///     }
/// }
/// # fn main() {}
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
/// ```
/// use kernel::prelude::*;
///
/// module!{
///     type: MyDeviceDriverModule,
///     name: "my_device_driver_module",
///     authors: ["Rust for Linux Contributors"],
///     description: "My device driver requires firmware",
///     license: "GPL",
///     firmware: ["my_device_firmware1.bin", "my_device_firmware2.bin"],
/// }
///
/// struct MyDeviceDriverModule;
///
/// impl kernel::Module for MyDeviceDriverModule {
///     fn init(_module: &'static ThisModule) -> Result<Self> {
///         Ok(Self)
///     }
/// }
/// # fn main() {}
/// ```
///
/// # Supported argument types
///   - `type`: type which implements the [`Module`] trait (required).
///   - `name`: ASCII string literal of the name of the kernel module (required).
///   - `authors`: array of ASCII string literals of the authors of the kernel module.
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
/// default implementation should call `build_error!`, which prevents
/// calls to this function at compile time:
///
/// ```compile_fail
/// # // Intentionally missing `use`s to simplify `rusttest`.
/// build_error!(VTABLE_DEFAULT_ERROR)
/// ```
///
/// Note that you might need to import [`kernel::error::VTABLE_DEFAULT_ERROR`].
///
/// This macro should not be used when all functions are required.
///
/// # Examples
///
/// ```
/// use kernel::error::VTABLE_DEFAULT_ERROR;
/// use kernel::prelude::*;
///
/// // Declares a `#[vtable]` trait
/// #[vtable]
/// pub trait Operations: Send + Sync + Sized {
///     fn foo(&self) -> Result<()> {
///         build_error!(VTABLE_DEFAULT_ERROR)
///     }
///
///     fn bar(&self) -> Result<()> {
///         build_error!(VTABLE_DEFAULT_ERROR)
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

/// Export a function so that C code can call it via a header file.
///
/// Functions exported using this macro can be called from C code using the declaration in the
/// appropriate header file. It should only be used in cases where C calls the function through a
/// header file; cases where C calls into Rust via a function pointer in a vtable (such as
/// `file_operations`) should not use this macro.
///
/// This macro has the following effect:
///
/// * Disables name mangling for this function.
/// * Verifies at compile-time that the function signature matches the declaration in the header
///   file.
///
/// You must declare the signature of the Rust function in a header file that is included by
/// `rust/bindings/bindings_helper.h`.
///
/// This macro is *not* the same as the C macros `EXPORT_SYMBOL_*`. All Rust symbols are currently
/// automatically exported with `EXPORT_SYMBOL_GPL`.
#[proc_macro_attribute]
pub fn export(attr: TokenStream, ts: TokenStream) -> TokenStream {
    export::export(attr, ts)
}

/// Concatenate two identifiers.
///
/// This is useful in macros that need to declare or reference items with names
/// starting with a fixed prefix and ending in a user specified name. The resulting
/// identifier has the span of the second argument.
///
/// # Examples
///
/// ```
/// # const binder_driver_return_protocol_BR_OK: u32 = 0;
/// # const binder_driver_return_protocol_BR_ERROR: u32 = 1;
/// # const binder_driver_return_protocol_BR_TRANSACTION: u32 = 2;
/// # const binder_driver_return_protocol_BR_REPLY: u32 = 3;
/// # const binder_driver_return_protocol_BR_DEAD_REPLY: u32 = 4;
/// # const binder_driver_return_protocol_BR_TRANSACTION_COMPLETE: u32 = 5;
/// # const binder_driver_return_protocol_BR_INCREFS: u32 = 6;
/// # const binder_driver_return_protocol_BR_ACQUIRE: u32 = 7;
/// # const binder_driver_return_protocol_BR_RELEASE: u32 = 8;
/// # const binder_driver_return_protocol_BR_DECREFS: u32 = 9;
/// # const binder_driver_return_protocol_BR_NOOP: u32 = 10;
/// # const binder_driver_return_protocol_BR_SPAWN_LOOPER: u32 = 11;
/// # const binder_driver_return_protocol_BR_DEAD_BINDER: u32 = 12;
/// # const binder_driver_return_protocol_BR_CLEAR_DEATH_NOTIFICATION_DONE: u32 = 13;
/// # const binder_driver_return_protocol_BR_FAILED_REPLY: u32 = 14;
/// use kernel::macros::concat_idents;
///
/// macro_rules! pub_no_prefix {
///     ($prefix:ident, $($newname:ident),+) => {
///         $(pub(crate) const $newname: u32 = concat_idents!($prefix, $newname);)+
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

/// Paste identifiers together.
///
/// Within the `paste!` macro, identifiers inside `[<` and `>]` are concatenated together to form a
/// single identifier.
///
/// This is similar to the [`paste`] crate, but with pasting feature limited to identifiers and
/// literals (lifetimes and documentation strings are not supported). There is a difference in
/// supported modifiers as well.
///
/// # Examples
///
/// ```
/// # const binder_driver_return_protocol_BR_OK: u32 = 0;
/// # const binder_driver_return_protocol_BR_ERROR: u32 = 1;
/// # const binder_driver_return_protocol_BR_TRANSACTION: u32 = 2;
/// # const binder_driver_return_protocol_BR_REPLY: u32 = 3;
/// # const binder_driver_return_protocol_BR_DEAD_REPLY: u32 = 4;
/// # const binder_driver_return_protocol_BR_TRANSACTION_COMPLETE: u32 = 5;
/// # const binder_driver_return_protocol_BR_INCREFS: u32 = 6;
/// # const binder_driver_return_protocol_BR_ACQUIRE: u32 = 7;
/// # const binder_driver_return_protocol_BR_RELEASE: u32 = 8;
/// # const binder_driver_return_protocol_BR_DECREFS: u32 = 9;
/// # const binder_driver_return_protocol_BR_NOOP: u32 = 10;
/// # const binder_driver_return_protocol_BR_SPAWN_LOOPER: u32 = 11;
/// # const binder_driver_return_protocol_BR_DEAD_BINDER: u32 = 12;
/// # const binder_driver_return_protocol_BR_CLEAR_DEATH_NOTIFICATION_DONE: u32 = 13;
/// # const binder_driver_return_protocol_BR_FAILED_REPLY: u32 = 14;
/// macro_rules! pub_no_prefix {
///     ($prefix:ident, $($newname:ident),+) => {
///         ::kernel::macros::paste! {
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
/// ```
/// # const binder_driver_return_protocol_BR_OK: u32 = 0;
/// # const binder_driver_return_protocol_BR_ERROR: u32 = 1;
/// # const binder_driver_return_protocol_BR_TRANSACTION: u32 = 2;
/// # const binder_driver_return_protocol_BR_REPLY: u32 = 3;
/// # const binder_driver_return_protocol_BR_DEAD_REPLY: u32 = 4;
/// # const binder_driver_return_protocol_BR_TRANSACTION_COMPLETE: u32 = 5;
/// # const binder_driver_return_protocol_BR_INCREFS: u32 = 6;
/// # const binder_driver_return_protocol_BR_ACQUIRE: u32 = 7;
/// # const binder_driver_return_protocol_BR_RELEASE: u32 = 8;
/// # const binder_driver_return_protocol_BR_DECREFS: u32 = 9;
/// # const binder_driver_return_protocol_BR_NOOP: u32 = 10;
/// # const binder_driver_return_protocol_BR_SPAWN_LOOPER: u32 = 11;
/// # const binder_driver_return_protocol_BR_DEAD_BINDER: u32 = 12;
/// # const binder_driver_return_protocol_BR_CLEAR_DEATH_NOTIFICATION_DONE: u32 = 13;
/// # const binder_driver_return_protocol_BR_FAILED_REPLY: u32 = 14;
/// macro_rules! pub_no_prefix {
///     ($prefix:ident, $($newname:ident),+) => {
///         ::kernel::macros::paste! {
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
/// ```
/// macro_rules! create_numbered_fn {
///     ($name:literal, $val:literal) => {
///         ::kernel::macros::paste! {
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

/// Registers a KUnit test suite and its test cases using a user-space like syntax.
///
/// This macro should be used on modules. If `CONFIG_KUNIT` (in `.config`) is `n`, the target module
/// is ignored.
///
/// # Examples
///
/// ```ignore
/// # use kernel::prelude::*;
/// #[kunit_tests(kunit_test_suit_name)]
/// mod tests {
///     #[test]
///     fn foo() {
///         assert_eq!(1, 1);
///     }
///
///     #[test]
///     fn bar() {
///         assert_eq!(2, 2);
///     }
/// }
/// ```
#[proc_macro_attribute]
pub fn kunit_tests(attr: TokenStream, ts: TokenStream) -> TokenStream {
    kunit::kunit_tests(attr, ts)
}
