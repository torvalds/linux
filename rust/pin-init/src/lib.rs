// SPDX-License-Identifier: Apache-2.0 OR MIT

//! Library to safely and fallibly initialize pinned `struct`s using in-place constructors.
//!
//! [Pinning][pinning] is Rust's way of ensuring data does not move.
//!
//! It also allows in-place initialization of big `struct`s that would otherwise produce a stack
//! overflow.
//!
//! This library's main use-case is in [Rust-for-Linux]. Although this version can be used
//! standalone.
//!
//! There are cases when you want to in-place initialize a struct. For example when it is very big
//! and moving it from the stack is not an option, because it is bigger than the stack itself.
//! Another reason would be that you need the address of the object to initialize it. This stands
//! in direct conflict with Rust's normal process of first initializing an object and then moving
//! it into it's final memory location. For more information, see
//! <https://rust-for-linux.com/the-safe-pinned-initialization-problem>.
//!
//! This library allows you to do in-place initialization safely.
//!
//! ## Nightly Needed for `alloc` feature
//!
//! This library requires the [`allocator_api` unstable feature] when the `alloc` feature is
//! enabled and thus this feature can only be used with a nightly compiler. When enabling the
//! `alloc` feature, the user will be required to activate `allocator_api` as well.
//!
//! [`allocator_api` unstable feature]: https://doc.rust-lang.org/nightly/unstable-book/library-features/allocator-api.html
//!
//! The feature is enabled by default, thus by default `pin-init` will require a nightly compiler.
//! However, using the crate on stable compilers is possible by disabling `alloc`. In practice this
//! will require the `std` feature, because stable compilers have neither `Box` nor `Arc` in no-std
//! mode.
//!
//! ## Nightly needed for `unsafe-pinned` feature
//!
//! This feature enables the `Wrapper` implementation on the unstable `core::pin::UnsafePinned` type.
//! This requires the [`unsafe_pinned` unstable feature](https://github.com/rust-lang/rust/issues/125735)
//! and therefore a nightly compiler. Note that this feature is not enabled by default.
//!
//! # Overview
//!
//! To initialize a `struct` with an in-place constructor you will need two things:
//! - an in-place constructor,
//! - a memory location that can hold your `struct` (this can be the [stack], an [`Arc<T>`],
//!   [`Box<T>`] or any other smart pointer that supports this library).
//!
//! To get an in-place constructor there are generally three options:
//! - directly creating an in-place constructor using the [`pin_init!`] macro,
//! - a custom function/macro returning an in-place constructor provided by someone else,
//! - using the unsafe function [`pin_init_from_closure()`] to manually create an initializer.
//!
//! Aside from pinned initialization, this library also supports in-place construction without
//! pinning, the macros/types/functions are generally named like the pinned variants without the
//! `pin_` prefix.
//!
//! # Examples
//!
//! Throughout the examples we will often make use of the `CMutex` type which can be found in
//! `../examples/mutex.rs`. It is essentially a userland rebuild of the `struct mutex` type from
//! the Linux kernel. It also uses a wait list and a basic spinlock. Importantly the wait list
//! requires it to be pinned to be locked and thus is a prime candidate for using this library.
//!
//! ## Using the [`pin_init!`] macro
//!
//! If you want to use [`PinInit`], then you will have to annotate your `struct` with
//! `#[`[`pin_data`]`]`. It is a macro that uses `#[pin]` as a marker for
//! [structurally pinned fields]. After doing this, you can then create an in-place constructor via
//! [`pin_init!`]. The syntax is almost the same as normal `struct` initializers. The difference is
//! that you need to write `<-` instead of `:` for fields that you want to initialize in-place.
//!
//! ```rust
//! # #![expect(clippy::disallowed_names)]
//! # #![feature(allocator_api)]
//! # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
//! # use core::pin::Pin;
//! use pin_init::{pin_data, pin_init, InPlaceInit};
//!
//! #[pin_data]
//! struct Foo {
//!     #[pin]
//!     a: CMutex<usize>,
//!     b: u32,
//! }
//!
//! let foo = pin_init!(Foo {
//!     a <- CMutex::new(42),
//!     b: 24,
//! });
//! # let _ = Box::pin_init(foo);
//! ```
//!
//! `foo` now is of the type [`impl PinInit<Foo>`]. We can now use any smart pointer that we like
//! (or just the stack) to actually initialize a `Foo`:
//!
//! ```rust
//! # #![expect(clippy::disallowed_names)]
//! # #![feature(allocator_api)]
//! # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
//! # use core::{alloc::AllocError, pin::Pin};
//! # use pin_init::*;
//! #
//! # #[pin_data]
//! # struct Foo {
//! #     #[pin]
//! #     a: CMutex<usize>,
//! #     b: u32,
//! # }
//! #
//! # let foo = pin_init!(Foo {
//! #     a <- CMutex::new(42),
//! #     b: 24,
//! # });
//! let foo: Result<Pin<Box<Foo>>, AllocError> = Box::pin_init(foo);
//! ```
//!
//! For more information see the [`pin_init!`] macro.
//!
//! ## Using a custom function/macro that returns an initializer
//!
//! Many types that use this library supply a function/macro that returns an initializer, because
//! the above method only works for types where you can access the fields.
//!
//! ```rust
//! # #![feature(allocator_api)]
//! # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
//! # use pin_init::*;
//! # use std::sync::Arc;
//! # use core::pin::Pin;
//! let mtx: Result<Pin<Arc<CMutex<usize>>>, _> = Arc::pin_init(CMutex::new(42));
//! ```
//!
//! To declare an init macro/function you just return an [`impl PinInit<T, E>`]:
//!
//! ```rust
//! # #![feature(allocator_api)]
//! # use pin_init::*;
//! # #[path = "../examples/error.rs"] mod error; use error::Error;
//! # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
//! #[pin_data]
//! struct DriverData {
//!     #[pin]
//!     status: CMutex<i32>,
//!     buffer: Box<[u8; 1_000_000]>,
//! }
//!
//! impl DriverData {
//!     fn new() -> impl PinInit<Self, Error> {
//!         try_pin_init!(Self {
//!             status <- CMutex::new(0),
//!             buffer: Box::init(pin_init::init_zeroed())?,
//!         }? Error)
//!     }
//! }
//! ```
//!
//! ## Manual creation of an initializer
//!
//! Often when working with primitives the previous approaches are not sufficient. That is where
//! [`pin_init_from_closure()`] comes in. This `unsafe` function allows you to create a
//! [`impl PinInit<T, E>`] directly from a closure. Of course you have to ensure that the closure
//! actually does the initialization in the correct way. Here are the things to look out for
//! (we are calling the parameter to the closure `slot`):
//! - when the closure returns `Ok(())`, then it has completed the initialization successfully, so
//!   `slot` now contains a valid bit pattern for the type `T`,
//! - when the closure returns `Err(e)`, then the caller may deallocate the memory at `slot`, so
//!   you need to take care to clean up anything if your initialization fails mid-way,
//! - you may assume that `slot` will stay pinned even after the closure returns until `drop` of
//!   `slot` gets called.
//!
//! ```rust
//! # #![feature(extern_types)]
//! use pin_init::{pin_data, pinned_drop, PinInit, PinnedDrop, pin_init_from_closure};
//! use core::{
//!     ptr::addr_of_mut,
//!     marker::PhantomPinned,
//!     cell::UnsafeCell,
//!     pin::Pin,
//!     mem::MaybeUninit,
//! };
//! mod bindings {
//!     #[repr(C)]
//!     pub struct foo {
//!         /* fields from C ... */
//!     }
//!     extern "C" {
//!         pub fn init_foo(ptr: *mut foo);
//!         pub fn destroy_foo(ptr: *mut foo);
//!         #[must_use = "you must check the error return code"]
//!         pub fn enable_foo(ptr: *mut foo, flags: u32) -> i32;
//!     }
//! }
//!
//! /// # Invariants
//! ///
//! /// `foo` is always initialized
//! #[pin_data(PinnedDrop)]
//! pub struct RawFoo {
//!     #[pin]
//!     _p: PhantomPinned,
//!     #[pin]
//!     foo: UnsafeCell<MaybeUninit<bindings::foo>>,
//! }
//!
//! impl RawFoo {
//!     pub fn new(flags: u32) -> impl PinInit<Self, i32> {
//!         // SAFETY:
//!         // - when the closure returns `Ok(())`, then it has successfully initialized and
//!         //   enabled `foo`,
//!         // - when it returns `Err(e)`, then it has cleaned up before
//!         unsafe {
//!             pin_init_from_closure(move |slot: *mut Self| {
//!                 // `slot` contains uninit memory, avoid creating a reference.
//!                 let foo = addr_of_mut!((*slot).foo);
//!                 let foo = UnsafeCell::raw_get(foo).cast::<bindings::foo>();
//!
//!                 // Initialize the `foo`
//!                 bindings::init_foo(foo);
//!
//!                 // Try to enable it.
//!                 let err = bindings::enable_foo(foo, flags);
//!                 if err != 0 {
//!                     // Enabling has failed, first clean up the foo and then return the error.
//!                     bindings::destroy_foo(foo);
//!                     Err(err)
//!                 } else {
//!                     // All fields of `RawFoo` have been initialized, since `_p` is a ZST.
//!                     Ok(())
//!                 }
//!             })
//!         }
//!     }
//! }
//!
//! #[pinned_drop]
//! impl PinnedDrop for RawFoo {
//!     fn drop(self: Pin<&mut Self>) {
//!         // SAFETY: Since `foo` is initialized, destroying is safe.
//!         unsafe { bindings::destroy_foo(self.foo.get().cast::<bindings::foo>()) };
//!     }
//! }
//! ```
//!
//! For more information on how to use [`pin_init_from_closure()`], take a look at the uses inside
//! the `kernel` crate. The [`sync`] module is a good starting point.
//!
//! [`sync`]: https://rust.docs.kernel.org/kernel/sync/index.html
//! [pinning]: https://doc.rust-lang.org/std/pin/index.html
//! [structurally pinned fields]:
//!     https://doc.rust-lang.org/std/pin/index.html#projections-and-structural-pinning
//! [stack]: crate::stack_pin_init
#![cfg_attr(
    kernel,
    doc = "[`Arc<T>`]: https://rust.docs.kernel.org/kernel/sync/struct.Arc.html"
)]
#![cfg_attr(
    kernel,
    doc = "[`Box<T>`]: https://rust.docs.kernel.org/kernel/alloc/kbox/struct.Box.html"
)]
#![cfg_attr(not(kernel), doc = "[`Arc<T>`]: alloc::alloc::sync::Arc")]
#![cfg_attr(not(kernel), doc = "[`Box<T>`]: alloc::alloc::boxed::Box")]
//! [`impl PinInit<Foo>`]: crate::PinInit
//! [`impl PinInit<T, E>`]: crate::PinInit
//! [`impl Init<T, E>`]: crate::Init
//! [Rust-for-Linux]: https://rust-for-linux.com/

#![cfg_attr(not(RUSTC_LINT_REASONS_IS_STABLE), feature(lint_reasons))]
#![cfg_attr(
    all(
        any(feature = "alloc", feature = "std"),
        not(RUSTC_NEW_UNINIT_IS_STABLE)
    ),
    feature(new_uninit)
)]
#![forbid(missing_docs, unsafe_op_in_unsafe_fn)]
#![cfg_attr(not(feature = "std"), no_std)]
#![cfg_attr(feature = "alloc", feature(allocator_api))]
#![cfg_attr(
    all(feature = "unsafe-pinned", CONFIG_RUSTC_HAS_UNSAFE_PINNED),
    feature(unsafe_pinned)
)]

use core::{
    cell::UnsafeCell,
    convert::Infallible,
    marker::PhantomData,
    mem::MaybeUninit,
    num::*,
    pin::Pin,
    ptr::{self, NonNull},
};

#[doc(hidden)]
pub mod __internal;
#[doc(hidden)]
pub mod macros;

#[cfg(any(feature = "std", feature = "alloc"))]
mod alloc;
#[cfg(any(feature = "std", feature = "alloc"))]
pub use alloc::InPlaceInit;

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
/// ```
/// # #![feature(allocator_api)]
/// # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
/// use pin_init::pin_data;
///
/// enum Command {
///     /* ... */
/// }
///
/// #[pin_data]
/// struct DriverData {
///     #[pin]
///     queue: CMutex<Vec<Command>>,
///     buf: Box<[u8; 1024 * 1024]>,
/// }
/// ```
///
/// ```
/// # #![feature(allocator_api)]
/// # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
/// # mod bindings { pub struct info; pub unsafe fn destroy_info(_: *mut info) {} }
/// use core::pin::Pin;
/// use pin_init::{pin_data, pinned_drop, PinnedDrop};
///
/// enum Command {
///     /* ... */
/// }
///
/// #[pin_data(PinnedDrop)]
/// struct DriverData {
///     #[pin]
///     queue: CMutex<Vec<Command>>,
///     buf: Box<[u8; 1024 * 1024]>,
///     raw_info: *mut bindings::info,
/// }
///
/// #[pinned_drop]
/// impl PinnedDrop for DriverData {
///     fn drop(self: Pin<&mut Self>) {
///         unsafe { bindings::destroy_info(self.raw_info) };
///     }
/// }
/// ```
pub use ::pin_init_internal::pin_data;

/// Used to implement `PinnedDrop` safely.
///
/// Only works on structs that are annotated via `#[`[`macro@pin_data`]`]`.
///
/// # Examples
///
/// ```
/// # #![feature(allocator_api)]
/// # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
/// # mod bindings { pub struct info; pub unsafe fn destroy_info(_: *mut info) {} }
/// use core::pin::Pin;
/// use pin_init::{pin_data, pinned_drop, PinnedDrop};
///
/// enum Command {
///     /* ... */
/// }
///
/// #[pin_data(PinnedDrop)]
/// struct DriverData {
///     #[pin]
///     queue: CMutex<Vec<Command>>,
///     buf: Box<[u8; 1024 * 1024]>,
///     raw_info: *mut bindings::info,
/// }
///
/// #[pinned_drop]
/// impl PinnedDrop for DriverData {
///     fn drop(self: Pin<&mut Self>) {
///         unsafe { bindings::destroy_info(self.raw_info) };
///     }
/// }
/// ```
pub use ::pin_init_internal::pinned_drop;

/// Derives the [`Zeroable`] trait for the given `struct` or `union`.
///
/// This can only be used for `struct`s/`union`s where every field implements the [`Zeroable`]
/// trait.
///
/// # Examples
///
/// ```
/// use pin_init::Zeroable;
///
/// #[derive(Zeroable)]
/// pub struct DriverData {
///     pub(crate) id: i64,
///     buf_ptr: *mut u8,
///     len: usize,
/// }
/// ```
///
/// ```
/// use pin_init::Zeroable;
///
/// #[derive(Zeroable)]
/// pub union SignCast {
///     signed: i64,
///     unsigned: u64,
/// }
/// ```
pub use ::pin_init_internal::Zeroable;

/// Derives the [`Zeroable`] trait for the given `struct` or `union` if all fields implement
/// [`Zeroable`].
///
/// Contrary to the derive macro named [`macro@Zeroable`], this one silently fails when a field
/// doesn't implement [`Zeroable`].
///
/// # Examples
///
/// ```
/// use pin_init::MaybeZeroable;
///
/// // implmements `Zeroable`
/// #[derive(MaybeZeroable)]
/// pub struct DriverData {
///     pub(crate) id: i64,
///     buf_ptr: *mut u8,
///     len: usize,
/// }
///
/// // does not implmement `Zeroable`
/// #[derive(MaybeZeroable)]
/// pub struct DriverData2 {
///     pub(crate) id: i64,
///     buf_ptr: *mut u8,
///     len: usize,
///     // this field doesn't implement `Zeroable`
///     other_data: &'static i32,
/// }
/// ```
pub use ::pin_init_internal::MaybeZeroable;

/// Initialize and pin a type directly on the stack.
///
/// # Examples
///
/// ```rust
/// # #![expect(clippy::disallowed_names)]
/// # #![feature(allocator_api)]
/// # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
/// # use pin_init::*;
/// # use core::pin::Pin;
/// #[pin_data]
/// struct Foo {
///     #[pin]
///     a: CMutex<usize>,
///     b: Bar,
/// }
///
/// #[pin_data]
/// struct Bar {
///     x: u32,
/// }
///
/// stack_pin_init!(let foo = pin_init!(Foo {
///     a <- CMutex::new(42),
///     b: Bar {
///         x: 64,
///     },
/// }));
/// let foo: Pin<&mut Foo> = foo;
/// println!("a: {}", &*foo.a.lock());
/// ```
///
/// # Syntax
///
/// A normal `let` binding with optional type annotation. The expression is expected to implement
/// [`PinInit`]/[`Init`] with the error type [`Infallible`]. If you want to use a different error
/// type, then use [`stack_try_pin_init!`].
#[macro_export]
macro_rules! stack_pin_init {
    (let $var:ident $(: $t:ty)? = $val:expr) => {
        let val = $val;
        let mut $var = ::core::pin::pin!($crate::__internal::StackInit$(::<$t>)?::uninit());
        let mut $var = match $crate::__internal::StackInit::init($var, val) {
            Ok(res) => res,
            Err(x) => {
                let x: ::core::convert::Infallible = x;
                match x {}
            }
        };
    };
}

/// Initialize and pin a type directly on the stack.
///
/// # Examples
///
/// ```rust
/// # #![expect(clippy::disallowed_names)]
/// # #![feature(allocator_api)]
/// # #[path = "../examples/error.rs"] mod error; use error::Error;
/// # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
/// # use pin_init::*;
/// #[pin_data]
/// struct Foo {
///     #[pin]
///     a: CMutex<usize>,
///     b: Box<Bar>,
/// }
///
/// struct Bar {
///     x: u32,
/// }
///
/// stack_try_pin_init!(let foo: Foo = try_pin_init!(Foo {
///     a <- CMutex::new(42),
///     b: Box::try_new(Bar {
///         x: 64,
///     })?,
/// }? Error));
/// let foo = foo.unwrap();
/// println!("a: {}", &*foo.a.lock());
/// ```
///
/// ```rust
/// # #![expect(clippy::disallowed_names)]
/// # #![feature(allocator_api)]
/// # #[path = "../examples/error.rs"] mod error; use error::Error;
/// # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
/// # use pin_init::*;
/// #[pin_data]
/// struct Foo {
///     #[pin]
///     a: CMutex<usize>,
///     b: Box<Bar>,
/// }
///
/// struct Bar {
///     x: u32,
/// }
///
/// stack_try_pin_init!(let foo: Foo =? try_pin_init!(Foo {
///     a <- CMutex::new(42),
///     b: Box::try_new(Bar {
///         x: 64,
///     })?,
/// }? Error));
/// println!("a: {}", &*foo.a.lock());
/// # Ok::<_, Error>(())
/// ```
///
/// # Syntax
///
/// A normal `let` binding with optional type annotation. The expression is expected to implement
/// [`PinInit`]/[`Init`]. This macro assigns a result to the given variable, adding a `?` after the
/// `=` will propagate this error.
#[macro_export]
macro_rules! stack_try_pin_init {
    (let $var:ident $(: $t:ty)? = $val:expr) => {
        let val = $val;
        let mut $var = ::core::pin::pin!($crate::__internal::StackInit$(::<$t>)?::uninit());
        let mut $var = $crate::__internal::StackInit::init($var, val);
    };
    (let $var:ident $(: $t:ty)? =? $val:expr) => {
        let val = $val;
        let mut $var = ::core::pin::pin!($crate::__internal::StackInit$(::<$t>)?::uninit());
        let mut $var = $crate::__internal::StackInit::init($var, val)?;
    };
}

/// Construct an in-place, pinned initializer for `struct`s.
///
/// This macro defaults the error to [`Infallible`]. If you need a different error, then use
/// [`try_pin_init!`].
///
/// The syntax is almost identical to that of a normal `struct` initializer:
///
/// ```rust
/// # use pin_init::*;
/// # use core::pin::Pin;
/// #[pin_data]
/// struct Foo {
///     a: usize,
///     b: Bar,
/// }
///
/// #[pin_data]
/// struct Bar {
///     x: u32,
/// }
///
/// # fn demo() -> impl PinInit<Foo> {
/// let a = 42;
///
/// let initializer = pin_init!(Foo {
///     a,
///     b: Bar {
///         x: 64,
///     },
/// });
/// # initializer }
/// # Box::pin_init(demo()).unwrap();
/// ```
///
/// Arbitrary Rust expressions can be used to set the value of a variable.
///
/// The fields are initialized in the order that they appear in the initializer. So it is possible
/// to read already initialized fields using raw pointers.
///
/// IMPORTANT: You are not allowed to create references to fields of the struct inside of the
/// initializer.
///
/// # Init-functions
///
/// When working with this library it is often desired to let others construct your types without
/// giving access to all fields. This is where you would normally write a plain function `new` that
/// would return a new instance of your type. With this library that is also possible. However,
/// there are a few extra things to keep in mind.
///
/// To create an initializer function, simply declare it like this:
///
/// ```rust
/// # use pin_init::*;
/// # use core::pin::Pin;
/// # #[pin_data]
/// # struct Foo {
/// #     a: usize,
/// #     b: Bar,
/// # }
/// # #[pin_data]
/// # struct Bar {
/// #     x: u32,
/// # }
/// impl Foo {
///     fn new() -> impl PinInit<Self> {
///         pin_init!(Self {
///             a: 42,
///             b: Bar {
///                 x: 64,
///             },
///         })
///     }
/// }
/// ```
///
/// Users of `Foo` can now create it like this:
///
/// ```rust
/// # #![expect(clippy::disallowed_names)]
/// # use pin_init::*;
/// # use core::pin::Pin;
/// # #[pin_data]
/// # struct Foo {
/// #     a: usize,
/// #     b: Bar,
/// # }
/// # #[pin_data]
/// # struct Bar {
/// #     x: u32,
/// # }
/// # impl Foo {
/// #     fn new() -> impl PinInit<Self> {
/// #         pin_init!(Self {
/// #             a: 42,
/// #             b: Bar {
/// #                 x: 64,
/// #             },
/// #         })
/// #     }
/// # }
/// let foo = Box::pin_init(Foo::new());
/// ```
///
/// They can also easily embed it into their own `struct`s:
///
/// ```rust
/// # use pin_init::*;
/// # use core::pin::Pin;
/// # #[pin_data]
/// # struct Foo {
/// #     a: usize,
/// #     b: Bar,
/// # }
/// # #[pin_data]
/// # struct Bar {
/// #     x: u32,
/// # }
/// # impl Foo {
/// #     fn new() -> impl PinInit<Self> {
/// #         pin_init!(Self {
/// #             a: 42,
/// #             b: Bar {
/// #                 x: 64,
/// #             },
/// #         })
/// #     }
/// # }
/// #[pin_data]
/// struct FooContainer {
///     #[pin]
///     foo1: Foo,
///     #[pin]
///     foo2: Foo,
///     other: u32,
/// }
///
/// impl FooContainer {
///     fn new(other: u32) -> impl PinInit<Self> {
///         pin_init!(Self {
///             foo1 <- Foo::new(),
///             foo2 <- Foo::new(),
///             other,
///         })
///     }
/// }
/// ```
///
/// Here we see that when using `pin_init!` with `PinInit`, one needs to write `<-` instead of `:`.
/// This signifies that the given field is initialized in-place. As with `struct` initializers, just
/// writing the field (in this case `other`) without `:` or `<-` means `other: other,`.
///
/// # Syntax
///
/// As already mentioned in the examples above, inside of `pin_init!` a `struct` initializer with
/// the following modifications is expected:
/// - Fields that you want to initialize in-place have to use `<-` instead of `:`.
/// - You can use `_: { /* run any user-code here */ },` anywhere where you can place fields in
///   order to run arbitrary code.
/// - In front of the initializer you can write `&this in` to have access to a [`NonNull<Self>`]
///   pointer named `this` inside of the initializer.
/// - Using struct update syntax one can place `..Zeroable::init_zeroed()` at the very end of the
///   struct, this initializes every field with 0 and then runs all initializers specified in the
///   body. This can only be done if [`Zeroable`] is implemented for the struct.
///
/// For instance:
///
/// ```rust
/// # use pin_init::*;
/// # use core::{ptr::addr_of_mut, marker::PhantomPinned};
/// #[pin_data]
/// #[derive(Zeroable)]
/// struct Buf {
///     // `ptr` points into `buf`.
///     ptr: *mut u8,
///     buf: [u8; 64],
///     #[pin]
///     pin: PhantomPinned,
/// }
///
/// let init = pin_init!(&this in Buf {
///     buf: [0; 64],
///     // SAFETY: TODO.
///     ptr: unsafe { addr_of_mut!((*this.as_ptr()).buf).cast() },
///     pin: PhantomPinned,
/// });
/// let init = pin_init!(Buf {
///     buf: [1; 64],
///     ..Zeroable::init_zeroed()
/// });
/// ```
///
/// [`NonNull<Self>`]: core::ptr::NonNull
// For a detailed example of how this macro works, see the module documentation of the hidden
// module `macros` inside of `macros.rs`.
#[macro_export]
macro_rules! pin_init {
    ($(&$this:ident in)? $t:ident $(::<$($generics:ty),* $(,)?>)? {
        $($fields:tt)*
    }) => {
        $crate::try_pin_init!($(&$this in)? $t $(::<$($generics),*>)? {
            $($fields)*
        }? ::core::convert::Infallible)
    };
}

/// Construct an in-place, fallible pinned initializer for `struct`s.
///
/// If the initialization can complete without error (or [`Infallible`]), then use [`pin_init!`].
///
/// You can use the `?` operator or use `return Err(err)` inside the initializer to stop
/// initialization and return the error.
///
/// IMPORTANT: if you have `unsafe` code inside of the initializer you have to ensure that when
/// initialization fails, the memory can be safely deallocated without any further modifications.
///
/// The syntax is identical to [`pin_init!`] with the following exception: you must append `? $type`
/// after the `struct` initializer to specify the error type you want to use.
///
/// # Examples
///
/// ```rust
/// # #![feature(allocator_api)]
/// # #[path = "../examples/error.rs"] mod error; use error::Error;
/// use pin_init::{pin_data, try_pin_init, PinInit, InPlaceInit, init_zeroed};
///
/// #[pin_data]
/// struct BigBuf {
///     big: Box<[u8; 1024 * 1024 * 1024]>,
///     small: [u8; 1024 * 1024],
///     ptr: *mut u8,
/// }
///
/// impl BigBuf {
///     fn new() -> impl PinInit<Self, Error> {
///         try_pin_init!(Self {
///             big: Box::init(init_zeroed())?,
///             small: [0; 1024 * 1024],
///             ptr: core::ptr::null_mut(),
///         }? Error)
///     }
/// }
/// # let _ = Box::pin_init(BigBuf::new());
/// ```
// For a detailed example of how this macro works, see the module documentation of the hidden
// module `macros` inside of `macros.rs`.
#[macro_export]
macro_rules! try_pin_init {
    ($(&$this:ident in)? $t:ident $(::<$($generics:ty),* $(,)?>)? {
        $($fields:tt)*
    }? $err:ty) => {
        $crate::__init_internal!(
            @this($($this)?),
            @typ($t $(::<$($generics),*>)? ),
            @fields($($fields)*),
            @error($err),
            @data(PinData, use_data),
            @has_data(HasPinData, __pin_data),
            @construct_closure(pin_init_from_closure),
            @munch_fields($($fields)*),
        )
    }
}

/// Construct an in-place initializer for `struct`s.
///
/// This macro defaults the error to [`Infallible`]. If you need a different error, then use
/// [`try_init!`].
///
/// The syntax is identical to [`pin_init!`] and its safety caveats also apply:
/// - `unsafe` code must guarantee either full initialization or return an error and allow
///   deallocation of the memory.
/// - the fields are initialized in the order given in the initializer.
/// - no references to fields are allowed to be created inside of the initializer.
///
/// This initializer is for initializing data in-place that might later be moved. If you want to
/// pin-initialize, use [`pin_init!`].
///
/// # Examples
///
/// ```rust
/// # #![feature(allocator_api)]
/// # #[path = "../examples/error.rs"] mod error; use error::Error;
/// # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
/// # use pin_init::InPlaceInit;
/// use pin_init::{init, Init, init_zeroed};
///
/// struct BigBuf {
///     small: [u8; 1024 * 1024],
/// }
///
/// impl BigBuf {
///     fn new() -> impl Init<Self> {
///         init!(Self {
///             small <- init_zeroed(),
///         })
///     }
/// }
/// # let _ = Box::init(BigBuf::new());
/// ```
// For a detailed example of how this macro works, see the module documentation of the hidden
// module `macros` inside of `macros.rs`.
#[macro_export]
macro_rules! init {
    ($(&$this:ident in)? $t:ident $(::<$($generics:ty),* $(,)?>)? {
        $($fields:tt)*
    }) => {
        $crate::try_init!($(&$this in)? $t $(::<$($generics),*>)? {
            $($fields)*
        }? ::core::convert::Infallible)
    }
}

/// Construct an in-place fallible initializer for `struct`s.
///
/// If the initialization can complete without error (or [`Infallible`]), then use
/// [`init!`].
///
/// The syntax is identical to [`try_pin_init!`]. You need to specify a custom error
/// via `? $type` after the `struct` initializer.
/// The safety caveats from [`try_pin_init!`] also apply:
/// - `unsafe` code must guarantee either full initialization or return an error and allow
///   deallocation of the memory.
/// - the fields are initialized in the order given in the initializer.
/// - no references to fields are allowed to be created inside of the initializer.
///
/// # Examples
///
/// ```rust
/// # #![feature(allocator_api)]
/// # use core::alloc::AllocError;
/// # use pin_init::InPlaceInit;
/// use pin_init::{try_init, Init, init_zeroed};
///
/// struct BigBuf {
///     big: Box<[u8; 1024 * 1024 * 1024]>,
///     small: [u8; 1024 * 1024],
/// }
///
/// impl BigBuf {
///     fn new() -> impl Init<Self, AllocError> {
///         try_init!(Self {
///             big: Box::init(init_zeroed())?,
///             small: [0; 1024 * 1024],
///         }? AllocError)
///     }
/// }
/// # let _ = Box::init(BigBuf::new());
/// ```
// For a detailed example of how this macro works, see the module documentation of the hidden
// module `macros` inside of `macros.rs`.
#[macro_export]
macro_rules! try_init {
    ($(&$this:ident in)? $t:ident $(::<$($generics:ty),* $(,)?>)? {
        $($fields:tt)*
    }? $err:ty) => {
        $crate::__init_internal!(
            @this($($this)?),
            @typ($t $(::<$($generics),*>)?),
            @fields($($fields)*),
            @error($err),
            @data(InitData, /*no use_data*/),
            @has_data(HasInitData, __init_data),
            @construct_closure(init_from_closure),
            @munch_fields($($fields)*),
        )
    }
}

/// Asserts that a field on a struct using `#[pin_data]` is marked with `#[pin]` ie. that it is
/// structurally pinned.
///
/// # Examples
///
/// This will succeed:
/// ```
/// use pin_init::{pin_data, assert_pinned};
///
/// #[pin_data]
/// struct MyStruct {
///     #[pin]
///     some_field: u64,
/// }
///
/// assert_pinned!(MyStruct, some_field, u64);
/// ```
///
/// This will fail:
/// ```compile_fail
/// use pin_init::{pin_data, assert_pinned};
///
/// #[pin_data]
/// struct MyStruct {
///     some_field: u64,
/// }
///
/// assert_pinned!(MyStruct, some_field, u64);
/// ```
///
/// Some uses of the macro may trigger the `can't use generic parameters from outer item` error. To
/// work around this, you may pass the `inline` parameter to the macro. The `inline` parameter can
/// only be used when the macro is invoked from a function body.
/// ```
/// # use core::pin::Pin;
/// use pin_init::{pin_data, assert_pinned};
///
/// #[pin_data]
/// struct Foo<T> {
///     #[pin]
///     elem: T,
/// }
///
/// impl<T> Foo<T> {
///     fn project_this(self: Pin<&mut Self>) -> Pin<&mut T> {
///         assert_pinned!(Foo<T>, elem, T, inline);
///
///         // SAFETY: The field is structurally pinned.
///         unsafe { self.map_unchecked_mut(|me| &mut me.elem) }
///     }
/// }
/// ```
#[macro_export]
macro_rules! assert_pinned {
    ($ty:ty, $field:ident, $field_ty:ty, inline) => {
        let _ = move |ptr: *mut $field_ty| {
            // SAFETY: This code is unreachable.
            let data = unsafe { <$ty as $crate::__internal::HasPinData>::__pin_data() };
            let init = $crate::__internal::AlwaysFail::<$field_ty>::new();
            // SAFETY: This code is unreachable.
            unsafe { data.$field(ptr, init) }.ok();
        };
    };

    ($ty:ty, $field:ident, $field_ty:ty) => {
        const _: () = {
            $crate::assert_pinned!($ty, $field, $field_ty, inline);
        };
    };
}

/// A pin-initializer for the type `T`.
///
/// To use this initializer, you will need a suitable memory location that can hold a `T`. This can
/// be [`Box<T>`], [`Arc<T>`] or even the stack (see [`stack_pin_init!`]).
///
/// Also see the [module description](self).
///
/// # Safety
///
/// When implementing this trait you will need to take great care. Also there are probably very few
/// cases where a manual implementation is necessary. Use [`pin_init_from_closure`] where possible.
///
/// The [`PinInit::__pinned_init`] function:
/// - returns `Ok(())` if it initialized every field of `slot`,
/// - returns `Err(err)` if it encountered an error and then cleaned `slot`, this means:
///     - `slot` can be deallocated without UB occurring,
///     - `slot` does not need to be dropped,
///     - `slot` is not partially initialized.
/// - while constructing the `T` at `slot` it upholds the pinning invariants of `T`.
///
#[cfg_attr(
    kernel,
    doc = "[`Arc<T>`]: https://rust.docs.kernel.org/kernel/sync/struct.Arc.html"
)]
#[cfg_attr(
    kernel,
    doc = "[`Box<T>`]: https://rust.docs.kernel.org/kernel/alloc/kbox/struct.Box.html"
)]
#[cfg_attr(not(kernel), doc = "[`Arc<T>`]: alloc::alloc::sync::Arc")]
#[cfg_attr(not(kernel), doc = "[`Box<T>`]: alloc::alloc::boxed::Box")]
#[must_use = "An initializer must be used in order to create its value."]
pub unsafe trait PinInit<T: ?Sized, E = Infallible>: Sized {
    /// Initializes `slot`.
    ///
    /// # Safety
    ///
    /// - `slot` is a valid pointer to uninitialized memory.
    /// - the caller does not touch `slot` when `Err` is returned, they are only permitted to
    ///   deallocate.
    /// - `slot` will not move until it is dropped, i.e. it will be pinned.
    unsafe fn __pinned_init(self, slot: *mut T) -> Result<(), E>;

    /// First initializes the value using `self` then calls the function `f` with the initialized
    /// value.
    ///
    /// If `f` returns an error the value is dropped and the initializer will forward the error.
    ///
    /// # Examples
    ///
    /// ```rust
    /// # #![feature(allocator_api)]
    /// # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
    /// # use pin_init::*;
    /// let mtx_init = CMutex::new(42);
    /// // Make the initializer print the value.
    /// let mtx_init = mtx_init.pin_chain(|mtx| {
    ///     println!("{:?}", mtx.get_data_mut());
    ///     Ok(())
    /// });
    /// ```
    fn pin_chain<F>(self, f: F) -> ChainPinInit<Self, F, T, E>
    where
        F: FnOnce(Pin<&mut T>) -> Result<(), E>,
    {
        ChainPinInit(self, f, PhantomData)
    }
}

/// An initializer returned by [`PinInit::pin_chain`].
pub struct ChainPinInit<I, F, T: ?Sized, E>(I, F, __internal::Invariant<(E, T)>);

// SAFETY: The `__pinned_init` function is implemented such that it
// - returns `Ok(())` on successful initialization,
// - returns `Err(err)` on error and in this case `slot` will be dropped.
// - considers `slot` pinned.
unsafe impl<T: ?Sized, E, I, F> PinInit<T, E> for ChainPinInit<I, F, T, E>
where
    I: PinInit<T, E>,
    F: FnOnce(Pin<&mut T>) -> Result<(), E>,
{
    unsafe fn __pinned_init(self, slot: *mut T) -> Result<(), E> {
        // SAFETY: All requirements fulfilled since this function is `__pinned_init`.
        unsafe { self.0.__pinned_init(slot)? };
        // SAFETY: The above call initialized `slot` and we still have unique access.
        let val = unsafe { &mut *slot };
        // SAFETY: `slot` is considered pinned.
        let val = unsafe { Pin::new_unchecked(val) };
        // SAFETY: `slot` was initialized above.
        (self.1)(val).inspect_err(|_| unsafe { core::ptr::drop_in_place(slot) })
    }
}

/// An initializer for `T`.
///
/// To use this initializer, you will need a suitable memory location that can hold a `T`. This can
/// be [`Box<T>`], [`Arc<T>`] or even the stack (see [`stack_pin_init!`]). Because
/// [`PinInit<T, E>`] is a super trait, you can use every function that takes it as well.
///
/// Also see the [module description](self).
///
/// # Safety
///
/// When implementing this trait you will need to take great care. Also there are probably very few
/// cases where a manual implementation is necessary. Use [`init_from_closure`] where possible.
///
/// The [`Init::__init`] function:
/// - returns `Ok(())` if it initialized every field of `slot`,
/// - returns `Err(err)` if it encountered an error and then cleaned `slot`, this means:
///     - `slot` can be deallocated without UB occurring,
///     - `slot` does not need to be dropped,
///     - `slot` is not partially initialized.
/// - while constructing the `T` at `slot` it upholds the pinning invariants of `T`.
///
/// The `__pinned_init` function from the supertrait [`PinInit`] needs to execute the exact same
/// code as `__init`.
///
/// Contrary to its supertype [`PinInit<T, E>`] the caller is allowed to
/// move the pointee after initialization.
///
#[cfg_attr(
    kernel,
    doc = "[`Arc<T>`]: https://rust.docs.kernel.org/kernel/sync/struct.Arc.html"
)]
#[cfg_attr(
    kernel,
    doc = "[`Box<T>`]: https://rust.docs.kernel.org/kernel/alloc/kbox/struct.Box.html"
)]
#[cfg_attr(not(kernel), doc = "[`Arc<T>`]: alloc::alloc::sync::Arc")]
#[cfg_attr(not(kernel), doc = "[`Box<T>`]: alloc::alloc::boxed::Box")]
#[must_use = "An initializer must be used in order to create its value."]
pub unsafe trait Init<T: ?Sized, E = Infallible>: PinInit<T, E> {
    /// Initializes `slot`.
    ///
    /// # Safety
    ///
    /// - `slot` is a valid pointer to uninitialized memory.
    /// - the caller does not touch `slot` when `Err` is returned, they are only permitted to
    ///   deallocate.
    unsafe fn __init(self, slot: *mut T) -> Result<(), E>;

    /// First initializes the value using `self` then calls the function `f` with the initialized
    /// value.
    ///
    /// If `f` returns an error the value is dropped and the initializer will forward the error.
    ///
    /// # Examples
    ///
    /// ```rust
    /// # #![expect(clippy::disallowed_names)]
    /// use pin_init::{init, init_zeroed, Init};
    ///
    /// struct Foo {
    ///     buf: [u8; 1_000_000],
    /// }
    ///
    /// impl Foo {
    ///     fn setup(&mut self) {
    ///         println!("Setting up foo");
    ///     }
    /// }
    ///
    /// let foo = init!(Foo {
    ///     buf <- init_zeroed()
    /// }).chain(|foo| {
    ///     foo.setup();
    ///     Ok(())
    /// });
    /// ```
    fn chain<F>(self, f: F) -> ChainInit<Self, F, T, E>
    where
        F: FnOnce(&mut T) -> Result<(), E>,
    {
        ChainInit(self, f, PhantomData)
    }
}

/// An initializer returned by [`Init::chain`].
pub struct ChainInit<I, F, T: ?Sized, E>(I, F, __internal::Invariant<(E, T)>);

// SAFETY: The `__init` function is implemented such that it
// - returns `Ok(())` on successful initialization,
// - returns `Err(err)` on error and in this case `slot` will be dropped.
unsafe impl<T: ?Sized, E, I, F> Init<T, E> for ChainInit<I, F, T, E>
where
    I: Init<T, E>,
    F: FnOnce(&mut T) -> Result<(), E>,
{
    unsafe fn __init(self, slot: *mut T) -> Result<(), E> {
        // SAFETY: All requirements fulfilled since this function is `__init`.
        unsafe { self.0.__pinned_init(slot)? };
        // SAFETY: The above call initialized `slot` and we still have unique access.
        (self.1)(unsafe { &mut *slot }).inspect_err(|_|
            // SAFETY: `slot` was initialized above.
            unsafe { core::ptr::drop_in_place(slot) })
    }
}

// SAFETY: `__pinned_init` behaves exactly the same as `__init`.
unsafe impl<T: ?Sized, E, I, F> PinInit<T, E> for ChainInit<I, F, T, E>
where
    I: Init<T, E>,
    F: FnOnce(&mut T) -> Result<(), E>,
{
    unsafe fn __pinned_init(self, slot: *mut T) -> Result<(), E> {
        // SAFETY: `__init` has less strict requirements compared to `__pinned_init`.
        unsafe { self.__init(slot) }
    }
}

/// Creates a new [`PinInit<T, E>`] from the given closure.
///
/// # Safety
///
/// The closure:
/// - returns `Ok(())` if it initialized every field of `slot`,
/// - returns `Err(err)` if it encountered an error and then cleaned `slot`, this means:
///     - `slot` can be deallocated without UB occurring,
///     - `slot` does not need to be dropped,
///     - `slot` is not partially initialized.
/// - may assume that the `slot` does not move if `T: !Unpin`,
/// - while constructing the `T` at `slot` it upholds the pinning invariants of `T`.
#[inline]
pub const unsafe fn pin_init_from_closure<T: ?Sized, E>(
    f: impl FnOnce(*mut T) -> Result<(), E>,
) -> impl PinInit<T, E> {
    __internal::InitClosure(f, PhantomData)
}

/// Creates a new [`Init<T, E>`] from the given closure.
///
/// # Safety
///
/// The closure:
/// - returns `Ok(())` if it initialized every field of `slot`,
/// - returns `Err(err)` if it encountered an error and then cleaned `slot`, this means:
///     - `slot` can be deallocated without UB occurring,
///     - `slot` does not need to be dropped,
///     - `slot` is not partially initialized.
/// - the `slot` may move after initialization.
/// - while constructing the `T` at `slot` it upholds the pinning invariants of `T`.
#[inline]
pub const unsafe fn init_from_closure<T: ?Sized, E>(
    f: impl FnOnce(*mut T) -> Result<(), E>,
) -> impl Init<T, E> {
    __internal::InitClosure(f, PhantomData)
}

/// Changes the to be initialized type.
///
/// # Safety
///
/// - `*mut U` must be castable to `*mut T` and any value of type `T` written through such a
///   pointer must result in a valid `U`.
#[expect(clippy::let_and_return)]
pub const unsafe fn cast_pin_init<T, U, E>(init: impl PinInit<T, E>) -> impl PinInit<U, E> {
    // SAFETY: initialization delegated to a valid initializer. Cast is valid by function safety
    // requirements.
    let res = unsafe { pin_init_from_closure(|ptr: *mut U| init.__pinned_init(ptr.cast::<T>())) };
    // FIXME: remove the let statement once the nightly-MSRV allows it (1.78 otherwise encounters a
    // cycle when computing the type returned by this function)
    res
}

/// Changes the to be initialized type.
///
/// # Safety
///
/// - `*mut U` must be castable to `*mut T` and any value of type `T` written through such a
///   pointer must result in a valid `U`.
#[expect(clippy::let_and_return)]
pub const unsafe fn cast_init<T, U, E>(init: impl Init<T, E>) -> impl Init<U, E> {
    // SAFETY: initialization delegated to a valid initializer. Cast is valid by function safety
    // requirements.
    let res = unsafe { init_from_closure(|ptr: *mut U| init.__init(ptr.cast::<T>())) };
    // FIXME: remove the let statement once the nightly-MSRV allows it (1.78 otherwise encounters a
    // cycle when computing the type returned by this function)
    res
}

/// An initializer that leaves the memory uninitialized.
///
/// The initializer is a no-op. The `slot` memory is not changed.
#[inline]
pub fn uninit<T, E>() -> impl Init<MaybeUninit<T>, E> {
    // SAFETY: The memory is allowed to be uninitialized.
    unsafe { init_from_closure(|_| Ok(())) }
}

/// Initializes an array by initializing each element via the provided initializer.
///
/// # Examples
///
/// ```rust
/// # use pin_init::*;
/// use pin_init::init_array_from_fn;
/// let array: Box<[usize; 1_000]> = Box::init(init_array_from_fn(|i| i)).unwrap();
/// assert_eq!(array.len(), 1_000);
/// ```
pub fn init_array_from_fn<I, const N: usize, T, E>(
    mut make_init: impl FnMut(usize) -> I,
) -> impl Init<[T; N], E>
where
    I: Init<T, E>,
{
    let init = move |slot: *mut [T; N]| {
        let slot = slot.cast::<T>();
        for i in 0..N {
            let init = make_init(i);
            // SAFETY: Since 0 <= `i` < N, it is still in bounds of `[T; N]`.
            let ptr = unsafe { slot.add(i) };
            // SAFETY: The pointer is derived from `slot` and thus satisfies the `__init`
            // requirements.
            if let Err(e) = unsafe { init.__init(ptr) } {
                // SAFETY: The loop has initialized the elements `slot[0..i]` and since we return
                // `Err` below, `slot` will be considered uninitialized memory.
                unsafe { ptr::drop_in_place(ptr::slice_from_raw_parts_mut(slot, i)) };
                return Err(e);
            }
        }
        Ok(())
    };
    // SAFETY: The initializer above initializes every element of the array. On failure it drops
    // any initialized elements and returns `Err`.
    unsafe { init_from_closure(init) }
}

/// Initializes an array by initializing each element via the provided initializer.
///
/// # Examples
///
/// ```rust
/// # #![feature(allocator_api)]
/// # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
/// # use pin_init::*;
/// # use core::pin::Pin;
/// use pin_init::pin_init_array_from_fn;
/// use std::sync::Arc;
/// let array: Pin<Arc<[CMutex<usize>; 1_000]>> =
///     Arc::pin_init(pin_init_array_from_fn(|i| CMutex::new(i))).unwrap();
/// assert_eq!(array.len(), 1_000);
/// ```
pub fn pin_init_array_from_fn<I, const N: usize, T, E>(
    mut make_init: impl FnMut(usize) -> I,
) -> impl PinInit<[T; N], E>
where
    I: PinInit<T, E>,
{
    let init = move |slot: *mut [T; N]| {
        let slot = slot.cast::<T>();
        for i in 0..N {
            let init = make_init(i);
            // SAFETY: Since 0 <= `i` < N, it is still in bounds of `[T; N]`.
            let ptr = unsafe { slot.add(i) };
            // SAFETY: The pointer is derived from `slot` and thus satisfies the `__init`
            // requirements.
            if let Err(e) = unsafe { init.__pinned_init(ptr) } {
                // SAFETY: The loop has initialized the elements `slot[0..i]` and since we return
                // `Err` below, `slot` will be considered uninitialized memory.
                unsafe { ptr::drop_in_place(ptr::slice_from_raw_parts_mut(slot, i)) };
                return Err(e);
            }
        }
        Ok(())
    };
    // SAFETY: The initializer above initializes every element of the array. On failure it drops
    // any initialized elements and returns `Err`.
    unsafe { pin_init_from_closure(init) }
}

// SAFETY: the `__init` function always returns `Ok(())` and initializes every field of `slot`.
unsafe impl<T> Init<T> for T {
    unsafe fn __init(self, slot: *mut T) -> Result<(), Infallible> {
        // SAFETY: `slot` is valid for writes by the safety requirements of this function.
        unsafe { slot.write(self) };
        Ok(())
    }
}

// SAFETY: the `__pinned_init` function always returns `Ok(())` and initializes every field of
// `slot`. Additionally, all pinning invariants of `T` are upheld.
unsafe impl<T> PinInit<T> for T {
    unsafe fn __pinned_init(self, slot: *mut T) -> Result<(), Infallible> {
        // SAFETY: `slot` is valid for writes by the safety requirements of this function.
        unsafe { slot.write(self) };
        Ok(())
    }
}

// SAFETY: when the `__init` function returns with
// - `Ok(())`, `slot` was initialized and all pinned invariants of `T` are upheld.
// - `Err(err)`, slot was not written to.
unsafe impl<T, E> Init<T, E> for Result<T, E> {
    unsafe fn __init(self, slot: *mut T) -> Result<(), E> {
        // SAFETY: `slot` is valid for writes by the safety requirements of this function.
        unsafe { slot.write(self?) };
        Ok(())
    }
}

// SAFETY: when the `__pinned_init` function returns with
// - `Ok(())`, `slot` was initialized and all pinned invariants of `T` are upheld.
// - `Err(err)`, slot was not written to.
unsafe impl<T, E> PinInit<T, E> for Result<T, E> {
    unsafe fn __pinned_init(self, slot: *mut T) -> Result<(), E> {
        // SAFETY: `slot` is valid for writes by the safety requirements of this function.
        unsafe { slot.write(self?) };
        Ok(())
    }
}

/// Smart pointer containing uninitialized memory and that can write a value.
pub trait InPlaceWrite<T> {
    /// The type `Self` turns into when the contents are initialized.
    type Initialized;

    /// Use the given initializer to write a value into `self`.
    ///
    /// Does not drop the current value and considers it as uninitialized memory.
    fn write_init<E>(self, init: impl Init<T, E>) -> Result<Self::Initialized, E>;

    /// Use the given pin-initializer to write a value into `self`.
    ///
    /// Does not drop the current value and considers it as uninitialized memory.
    fn write_pin_init<E>(self, init: impl PinInit<T, E>) -> Result<Pin<Self::Initialized>, E>;
}

/// Trait facilitating pinned destruction.
///
/// Use [`pinned_drop`] to implement this trait safely:
///
/// ```rust
/// # #![feature(allocator_api)]
/// # #[path = "../examples/mutex.rs"] mod mutex; use mutex::*;
/// # use pin_init::*;
/// use core::pin::Pin;
/// #[pin_data(PinnedDrop)]
/// struct Foo {
///     #[pin]
///     mtx: CMutex<usize>,
/// }
///
/// #[pinned_drop]
/// impl PinnedDrop for Foo {
///     fn drop(self: Pin<&mut Self>) {
///         println!("Foo is being dropped!");
///     }
/// }
/// ```
///
/// # Safety
///
/// This trait must be implemented via the [`pinned_drop`] proc-macro attribute on the impl.
pub unsafe trait PinnedDrop: __internal::HasPinData {
    /// Executes the pinned destructor of this type.
    ///
    /// While this function is marked safe, it is actually unsafe to call it manually. For this
    /// reason it takes an additional parameter. This type can only be constructed by `unsafe` code
    /// and thus prevents this function from being called where it should not.
    ///
    /// This extra parameter will be generated by the `#[pinned_drop]` proc-macro attribute
    /// automatically.
    fn drop(self: Pin<&mut Self>, only_call_from_drop: __internal::OnlyCallFromDrop);
}

/// Marker trait for types that can be initialized by writing just zeroes.
///
/// # Safety
///
/// The bit pattern consisting of only zeroes is a valid bit pattern for this type. In other words,
/// this is not UB:
///
/// ```rust,ignore
/// let val: Self = unsafe { core::mem::zeroed() };
/// ```
pub unsafe trait Zeroable {
    /// Create a new zeroed `Self`.
    ///
    /// The returned initializer will write `0x00` to every byte of the given `slot`.
    #[inline]
    fn init_zeroed() -> impl Init<Self>
    where
        Self: Sized,
    {
        init_zeroed()
    }

    /// Create a `Self` consisting of all zeroes.
    ///
    /// Whenever a type implements [`Zeroable`], this function should be preferred over
    /// [`core::mem::zeroed()`] or using `MaybeUninit<T>::zeroed().assume_init()`.
    ///
    /// # Examples
    ///
    /// ```
    /// use pin_init::{Zeroable, zeroed};
    ///
    /// #[derive(Zeroable)]
    /// struct Point {
    ///     x: u32,
    ///     y: u32,
    /// }
    ///
    /// let point: Point = zeroed();
    /// assert_eq!(point.x, 0);
    /// assert_eq!(point.y, 0);
    /// ```
    fn zeroed() -> Self
    where
        Self: Sized,
    {
        zeroed()
    }
}

/// Marker trait for types that allow `Option<Self>` to be set to all zeroes in order to write
/// `None` to that location.
///
/// # Safety
///
/// The implementer needs to ensure that `unsafe impl Zeroable for Option<Self> {}` is sound.
pub unsafe trait ZeroableOption {}

// SAFETY: by the safety requirement of `ZeroableOption`, this is valid.
unsafe impl<T: ZeroableOption> Zeroable for Option<T> {}

// SAFETY: `Option<&T>` is part of the option layout optimization guarantee:
// <https://doc.rust-lang.org/stable/std/option/index.html#representation>.
unsafe impl<T> ZeroableOption for &T {}
// SAFETY: `Option<&mut T>` is part of the option layout optimization guarantee:
// <https://doc.rust-lang.org/stable/std/option/index.html#representation>.
unsafe impl<T> ZeroableOption for &mut T {}
// SAFETY: `Option<NonNull<T>>` is part of the option layout optimization guarantee:
// <https://doc.rust-lang.org/stable/std/option/index.html#representation>.
unsafe impl<T> ZeroableOption for NonNull<T> {}

/// Create an initializer for a zeroed `T`.
///
/// The returned initializer will write `0x00` to every byte of the given `slot`.
#[inline]
pub fn init_zeroed<T: Zeroable>() -> impl Init<T> {
    // SAFETY: Because `T: Zeroable`, all bytes zero is a valid bit pattern for `T`
    // and because we write all zeroes, the memory is initialized.
    unsafe {
        init_from_closure(|slot: *mut T| {
            slot.write_bytes(0, 1);
            Ok(())
        })
    }
}

/// Create a `T` consisting of all zeroes.
///
/// Whenever a type implements [`Zeroable`], this function should be preferred over
/// [`core::mem::zeroed()`] or using `MaybeUninit<T>::zeroed().assume_init()`.
///
/// # Examples
///
/// ```
/// use pin_init::{Zeroable, zeroed};
///
/// #[derive(Zeroable)]
/// struct Point {
///     x: u32,
///     y: u32,
/// }
///
/// let point: Point = zeroed();
/// assert_eq!(point.x, 0);
/// assert_eq!(point.y, 0);
/// ```
pub const fn zeroed<T: Zeroable>() -> T {
    // SAFETY:By the type invariants of `Zeroable`, all zeroes is a valid bit pattern for `T`.
    unsafe { core::mem::zeroed() }
}

macro_rules! impl_zeroable {
    ($($({$($generics:tt)*})? $t:ty, )*) => {
        // SAFETY: Safety comments written in the macro invocation.
        $(unsafe impl$($($generics)*)? Zeroable for $t {})*
    };
}

impl_zeroable! {
    // SAFETY: All primitives that are allowed to be zero.
    bool,
    char,
    u8, u16, u32, u64, u128, usize,
    i8, i16, i32, i64, i128, isize,
    f32, f64,

    // Note: do not add uninhabited types (such as `!` or `core::convert::Infallible`) to this list;
    // creating an instance of an uninhabited type is immediate undefined behavior. For more on
    // uninhabited/empty types, consult The Rustonomicon:
    // <https://doc.rust-lang.org/stable/nomicon/exotic-sizes.html#empty-types>. The Rust Reference
    // also has information on undefined behavior:
    // <https://doc.rust-lang.org/stable/reference/behavior-considered-undefined.html>.
    //
    // SAFETY: These are inhabited ZSTs; there is nothing to zero and a valid value exists.
    {<T: ?Sized>} PhantomData<T>, core::marker::PhantomPinned, (),

    // SAFETY: Type is allowed to take any value, including all zeros.
    {<T>} MaybeUninit<T>,

    // SAFETY: `T: Zeroable` and `UnsafeCell` is `repr(transparent)`.
    {<T: ?Sized + Zeroable>} UnsafeCell<T>,

    // SAFETY: All zeros is equivalent to `None` (option layout optimization guarantee:
    // <https://doc.rust-lang.org/stable/std/option/index.html#representation>).
    Option<NonZeroU8>, Option<NonZeroU16>, Option<NonZeroU32>, Option<NonZeroU64>,
    Option<NonZeroU128>, Option<NonZeroUsize>,
    Option<NonZeroI8>, Option<NonZeroI16>, Option<NonZeroI32>, Option<NonZeroI64>,
    Option<NonZeroI128>, Option<NonZeroIsize>,

    // SAFETY: `null` pointer is valid.
    //
    // We cannot use `T: ?Sized`, since the VTABLE pointer part of fat pointers is not allowed to be
    // null.
    //
    // When `Pointee` gets stabilized, we could use
    // `T: ?Sized where <T as Pointee>::Metadata: Zeroable`
    {<T>} *mut T, {<T>} *const T,

    // SAFETY: `null` pointer is valid and the metadata part of these fat pointers is allowed to be
    // zero.
    {<T>} *mut [T], {<T>} *const [T], *mut str, *const str,

    // SAFETY: `T` is `Zeroable`.
    {<const N: usize, T: Zeroable>} [T; N], {<T: Zeroable>} Wrapping<T>,
}

macro_rules! impl_tuple_zeroable {
    ($(,)?) => {};
    ($first:ident, $($t:ident),* $(,)?) => {
        // SAFETY: All elements are zeroable and padding can be zero.
        unsafe impl<$first: Zeroable, $($t: Zeroable),*> Zeroable for ($first, $($t),*) {}
        impl_tuple_zeroable!($($t),* ,);
    }
}

impl_tuple_zeroable!(A, B, C, D, E, F, G, H, I, J);

macro_rules! impl_fn_zeroable_option {
    ([$($abi:literal),* $(,)?] $args:tt) => {
        $(impl_fn_zeroable_option!({extern $abi} $args);)*
        $(impl_fn_zeroable_option!({unsafe extern $abi} $args);)*
    };
    ({$($prefix:tt)*} {$(,)?}) => {};
    ({$($prefix:tt)*} {$ret:ident, $($rest:ident),* $(,)?}) => {
        // SAFETY: function pointers are part of the option layout optimization:
        // <https://doc.rust-lang.org/stable/std/option/index.html#representation>.
        unsafe impl<$ret, $($rest),*> ZeroableOption for $($prefix)* fn($($rest),*) -> $ret {}
        impl_fn_zeroable_option!({$($prefix)*} {$($rest),*,});
    };
}

impl_fn_zeroable_option!(["Rust", "C"] { A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U });

/// This trait allows creating an instance of `Self` which contains exactly one
/// [structurally pinned value](https://doc.rust-lang.org/std/pin/index.html#projections-and-structural-pinning).
///
/// This is useful when using wrapper `struct`s like [`UnsafeCell`] or with new-type `struct`s.
///
/// # Examples
///
/// ```
/// # use core::cell::UnsafeCell;
/// # use pin_init::{pin_data, pin_init, Wrapper};
///
/// #[pin_data]
/// struct Foo {}
///
/// #[pin_data]
/// struct Bar {
///     #[pin]
///     content: UnsafeCell<Foo>
/// };
///
/// let foo_initializer = pin_init!(Foo{});
/// let initializer = pin_init!(Bar {
///     content <- UnsafeCell::pin_init(foo_initializer)
/// });
/// ```
pub trait Wrapper<T> {
    /// Creates an pin-initializer for a [`Self`] containing `T` from the `value_init` initializer.
    fn pin_init<E>(value_init: impl PinInit<T, E>) -> impl PinInit<Self, E>;
}

impl<T> Wrapper<T> for UnsafeCell<T> {
    fn pin_init<E>(value_init: impl PinInit<T, E>) -> impl PinInit<Self, E> {
        // SAFETY: `UnsafeCell<T>` has a compatible layout to `T`.
        unsafe { cast_pin_init(value_init) }
    }
}

impl<T> Wrapper<T> for MaybeUninit<T> {
    fn pin_init<E>(value_init: impl PinInit<T, E>) -> impl PinInit<Self, E> {
        // SAFETY: `MaybeUninit<T>` has a compatible layout to `T`.
        unsafe { cast_pin_init(value_init) }
    }
}

#[cfg(all(feature = "unsafe-pinned", CONFIG_RUSTC_HAS_UNSAFE_PINNED))]
impl<T> Wrapper<T> for core::pin::UnsafePinned<T> {
    fn pin_init<E>(init: impl PinInit<T, E>) -> impl PinInit<Self, E> {
        // SAFETY: `UnsafePinned<T>` has a compatible layout to `T`.
        unsafe { cast_pin_init(init) }
    }
}
