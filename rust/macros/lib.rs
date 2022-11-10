// SPDX-License-Identifier: GPL-2.0

//! Crate for all kernel procedural macros.

mod concat_idents;
mod helpers;
mod module;
mod vtable;

use proc_macro::TokenStream;

/// Declares a kernel module.
///
/// The `type` argument should be a type which implements the [`Module`]
/// trait. Also accepts various forms of kernel metadata.
///
/// C header: [`include/linux/moduleparam.h`](../../../include/linux/moduleparam.h)
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
///     params: {
///        my_i32: i32 {
///            default: 42,
///            permissions: 0o000,
///            description: "Example of i32",
///        },
///        writeable_i32: i32 {
///            default: 42,
///            permissions: 0o644,
///            description: "Example of i32",
///        },
///    },
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
/// # Supported argument types
///   - `type`: type which implements the [`Module`] trait (required).
///   - `name`: byte array of the name of the kernel module (required).
///   - `author`: byte array of the author of the kernel module.
///   - `description`: byte array of the description of the kernel module.
///   - `license`: byte array of the license of the kernel module (required).
///   - `alias`: byte array of alias name of the kernel module.
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
/// This attribute is intended to close the gap. Traits can be declared and
/// implemented with the `#[vtable]` attribute, and a `HAS_*` associated constant
/// will be generated for each method in the trait, indicating if the implementor
/// has overridden a method.
///
/// This attribute is not needed if all methods are required.
///
/// # Examples
///
/// ```ignore
/// use kernel::prelude::*;
///
/// // Declares a `#[vtable]` trait
/// #[vtable]
/// pub trait Operations: Send + Sync + Sized {
///     fn foo(&self) -> Result<()> {
///         Err(EINVAL)
///     }
///
///     fn bar(&self) -> Result<()> {
///         Err(EINVAL)
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
