// SPDX-License-Identifier: GPL-2.0

//! Extensions to the [`pin-init`] crate.
//!
//! Most `struct`s from the [`sync`] module need to be pinned, because they contain self-referential
//! `struct`s from C. [Pinning][pinning] is Rust's way of ensuring data does not move.
//!
//! The [`pin-init`] crate is the way such structs are initialized on the Rust side. Please refer
//! to its documentation to better understand how to use it. Additionally, there are many examples
//! throughout the kernel, such as the types from the [`sync`] module. And the ones presented
//! below.
//!
//! [`sync`]: crate::sync
//! [pinning]: https://doc.rust-lang.org/std/pin/index.html
//! [`pin-init`]: https://rust.docs.kernel.org/pin_init/
//!
//! # [`Opaque<T>`]
//!
//! For the special case where initializing a field is a single FFI-function call that cannot fail,
//! there exist the helper function [`Opaque::ffi_init`]. This function initialize a single
//! [`Opaque<T>`] field by just delegating to the supplied closure. You can use these in
//! combination with [`pin_init!`].
//!
//! [`Opaque<T>`]: crate::types::Opaque
//! [`Opaque::ffi_init`]: crate::types::Opaque::ffi_init
//! [`pin_init!`]: crate::pin_init
//!
//! # Examples
//!
//! ## General Examples
//!
//! ```rust,ignore
//! # #![allow(clippy::disallowed_names)]
//! use kernel::types::Opaque;
//! use pin_init::pin_init_from_closure;
//!
//! // assume we have some `raw_foo` type in C:
//! #[repr(C)]
//! struct RawFoo([u8; 16]);
//! extern {
//!     fn init_foo(_: *mut RawFoo);
//! }
//!
//! #[pin_data]
//! struct Foo {
//!     #[pin]
//!     raw: Opaque<RawFoo>,
//! }
//!
//! impl Foo {
//!     fn setup(self: Pin<&mut Self>) {
//!         pr_info!("Setting up foo");
//!     }
//! }
//!
//! let foo = pin_init!(Foo {
//!     raw <- unsafe {
//!         Opaque::ffi_init(|s| {
//!             // note that this cannot fail.
//!             init_foo(s);
//!         })
//!     },
//! }).pin_chain(|foo| {
//!     foo.setup();
//!     Ok(())
//! });
//! ```
//!
//! ```rust,ignore
//! # #![allow(unreachable_pub, clippy::disallowed_names)]
//! use kernel::{prelude::*, types::Opaque};
//! use core::{ptr::addr_of_mut, marker::PhantomPinned, pin::Pin};
//! # mod bindings {
//! #     #![allow(non_camel_case_types)]
//! #     pub struct foo;
//! #     pub unsafe fn init_foo(_ptr: *mut foo) {}
//! #     pub unsafe fn destroy_foo(_ptr: *mut foo) {}
//! #     pub unsafe fn enable_foo(_ptr: *mut foo, _flags: u32) -> i32 { 0 }
//! # }
//! # // `Error::from_errno` is `pub(crate)` in the `kernel` crate, thus provide a workaround.
//! # trait FromErrno {
//! #     fn from_errno(errno: core::ffi::c_int) -> Error {
//! #         // Dummy error that can be constructed outside the `kernel` crate.
//! #         Error::from(core::fmt::Error)
//! #     }
//! # }
//! # impl FromErrno for Error {}
//! /// # Invariants
//! ///
//! /// `foo` is always initialized
//! #[pin_data(PinnedDrop)]
//! pub struct RawFoo {
//!     #[pin]
//!     foo: Opaque<bindings::foo>,
//!     #[pin]
//!     _p: PhantomPinned,
//! }
//!
//! impl RawFoo {
//!     pub fn new(flags: u32) -> impl PinInit<Self, Error> {
//!         // SAFETY:
//!         // - when the closure returns `Ok(())`, then it has successfully initialized and
//!         //   enabled `foo`,
//!         // - when it returns `Err(e)`, then it has cleaned up before
//!         unsafe {
//!             pin_init::pin_init_from_closure(move |slot: *mut Self| {
//!                 // `slot` contains uninit memory, avoid creating a reference.
//!                 let foo = addr_of_mut!((*slot).foo);
//!
//!                 // Initialize the `foo`
//!                 bindings::init_foo(Opaque::raw_get(foo));
//!
//!                 // Try to enable it.
//!                 let err = bindings::enable_foo(Opaque::raw_get(foo), flags);
//!                 if err != 0 {
//!                     // Enabling has failed, first clean up the foo and then return the error.
//!                     bindings::destroy_foo(Opaque::raw_get(foo));
//!                     return Err(Error::from_errno(err));
//!                 }
//!
//!                 // All fields of `RawFoo` have been initialized, since `_p` is a ZST.
//!                 Ok(())
//!             })
//!         }
//!     }
//! }
//!
//! #[pinned_drop]
//! impl PinnedDrop for RawFoo {
//!     fn drop(self: Pin<&mut Self>) {
//!         // SAFETY: Since `foo` is initialized, destroying is safe.
//!         unsafe { bindings::destroy_foo(self.foo.get()) };
//!     }
//! }
//! ```
