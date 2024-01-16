// SPDX-License-Identifier: GPL-2.0

//! Crate for all kernel procedural macros.

mod helpers;
mod module;

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
///     name: b"my_kernel_module",
///     author: b"Rust for Linux Contributors",
///     description: b"My very own kernel module!",
///     license: b"GPL",
///     params: {
///        my_i32: i32 {
///            default: 42,
///            permissions: 0o000,
///            description: b"Example of i32",
///        },
///        writeable_i32: i32 {
///            default: 42,
///            permissions: 0o644,
///            description: b"Example of i32",
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
