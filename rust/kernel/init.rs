// SPDX-License-Identifier: Apache-2.0 OR MIT

//! API to safely and fallibly initialize pinned `struct`s using in-place constructors.
//!
//! It also allows in-place initialization of big `struct`s that would otherwise produce a stack
//! overflow.
//!
//! Most `struct`s from the [`sync`] module need to be pinned, because they contain self-referential
//! `struct`s from C. [Pinning][pinning] is Rust's way of ensuring data does not move.
//!
//! # Overview
//!
//! To initialize a `struct` with an in-place constructor you will need two things:
//! - an in-place constructor,
//! - a memory location that can hold your `struct` (this can be the [stack], an [`Arc<T>`],
//!   [`UniqueArc<T>`], [`Box<T>`] or any other smart pointer that implements [`InPlaceInit`]).
//!
//! To get an in-place constructor there are generally three options:
//! - directly creating an in-place constructor using the [`pin_init!`] macro,
//! - a custom function/macro returning an in-place constructor provided by someone else,
//! - using the unsafe function [`pin_init_from_closure()`] to manually create an initializer.
//!
//! Aside from pinned initialization, this API also supports in-place construction without pinning,
//! the macros/types/functions are generally named like the pinned variants without the `pin`
//! prefix.
//!
//! # Examples
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
//! # #![allow(clippy::disallowed_names)]
//! use kernel::sync::{new_mutex, Mutex};
//! # use core::pin::Pin;
//! #[pin_data]
//! struct Foo {
//!     #[pin]
//!     a: Mutex<usize>,
//!     b: u32,
//! }
//!
//! let foo = pin_init!(Foo {
//!     a <- new_mutex!(42, "Foo::a"),
//!     b: 24,
//! });
//! ```
//!
//! `foo` now is of the type [`impl PinInit<Foo>`]. We can now use any smart pointer that we like
//! (or just the stack) to actually initialize a `Foo`:
//!
//! ```rust
//! # #![allow(clippy::disallowed_names)]
//! # use kernel::sync::{new_mutex, Mutex};
//! # use core::pin::Pin;
//! # #[pin_data]
//! # struct Foo {
//! #     #[pin]
//! #     a: Mutex<usize>,
//! #     b: u32,
//! # }
//! # let foo = pin_init!(Foo {
//! #     a <- new_mutex!(42, "Foo::a"),
//! #     b: 24,
//! # });
//! let foo: Result<Pin<Box<Foo>>> = Box::pin_init(foo, GFP_KERNEL);
//! ```
//!
//! For more information see the [`pin_init!`] macro.
//!
//! ## Using a custom function/macro that returns an initializer
//!
//! Many types from the kernel supply a function/macro that returns an initializer, because the
//! above method only works for types where you can access the fields.
//!
//! ```rust
//! # use kernel::sync::{new_mutex, Arc, Mutex};
//! let mtx: Result<Arc<Mutex<usize>>> =
//!     Arc::pin_init(new_mutex!(42, "example::mtx"), GFP_KERNEL);
//! ```
//!
//! To declare an init macro/function you just return an [`impl PinInit<T, E>`]:
//!
//! ```rust
//! # use kernel::{sync::Mutex, new_mutex, init::PinInit, try_pin_init};
//! #[pin_data]
//! struct DriverData {
//!     #[pin]
//!     status: Mutex<i32>,
//!     buffer: Box<[u8; 1_000_000]>,
//! }
//!
//! impl DriverData {
//!     fn new() -> impl PinInit<Self, Error> {
//!         try_pin_init!(Self {
//!             status <- new_mutex!(0, "DriverData::status"),
//!             buffer: Box::init(kernel::init::zeroed(), GFP_KERNEL)?,
//!         })
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
//! # #![allow(unreachable_pub, clippy::disallowed_names)]
//! use kernel::{init, types::Opaque};
//! use core::{ptr::addr_of_mut, marker::PhantomPinned, pin::Pin};
//! # mod bindings {
//! #     #![allow(non_camel_case_types)]
//! #     #![allow(clippy::missing_safety_doc)]
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
//!             init::pin_init_from_closure(move |slot: *mut Self| {
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
//!
//! For the special case where initializing a field is a single FFI-function call that cannot fail,
//! there exist the helper function [`Opaque::ffi_init`]. This function initialize a single
//! [`Opaque`] field by just delegating to the supplied closure. You can use these in combination
//! with [`pin_init!`].
//!
//! For more information on how to use [`pin_init_from_closure()`], take a look at the uses inside
//! the `kernel` crate. The [`sync`] module is a good starting point.
//!
//! [`sync`]: kernel::sync
//! [pinning]: https://doc.rust-lang.org/std/pin/index.html
//! [structurally pinned fields]:
//!     https://doc.rust-lang.org/std/pin/index.html#pinning-is-structural-for-field
//! [stack]: crate::stack_pin_init
//! [`Arc<T>`]: crate::sync::Arc
//! [`impl PinInit<Foo>`]: PinInit
//! [`impl PinInit<T, E>`]: PinInit
//! [`impl Init<T, E>`]: Init
//! [`Opaque`]: kernel::types::Opaque
//! [`Opaque::ffi_init`]: kernel::types::Opaque::ffi_init
//! [`pin_data`]: ::macros::pin_data
//! [`pin_init!`]: crate::pin_init!

use crate::{
    alloc::{box_ext::BoxExt, AllocError, Flags},
    error::{self, Error},
    sync::Arc,
    sync::UniqueArc,
    types::{Opaque, ScopeGuard},
};
use alloc::boxed::Box;
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

/// Initialize and pin a type directly on the stack.
///
/// # Examples
///
/// ```rust
/// # #![allow(clippy::disallowed_names)]
/// # use kernel::{init, macros::pin_data, pin_init, stack_pin_init, init::*, sync::Mutex, new_mutex};
/// # use core::pin::Pin;
/// #[pin_data]
/// struct Foo {
///     #[pin]
///     a: Mutex<usize>,
///     b: Bar,
/// }
///
/// #[pin_data]
/// struct Bar {
///     x: u32,
/// }
///
/// stack_pin_init!(let foo = pin_init!(Foo {
///     a <- new_mutex!(42),
///     b: Bar {
///         x: 64,
///     },
/// }));
/// let foo: Pin<&mut Foo> = foo;
/// pr_info!("a: {}", &*foo.a.lock());
/// ```
///
/// # Syntax
///
/// A normal `let` binding with optional type annotation. The expression is expected to implement
/// [`PinInit`]/[`Init`] with the error type [`Infallible`]. If you want to use a different error
/// type, then use [`stack_try_pin_init!`].
///
/// [`stack_try_pin_init!`]: crate::stack_try_pin_init!
#[macro_export]
macro_rules! stack_pin_init {
    (let $var:ident $(: $t:ty)? = $val:expr) => {
        let val = $val;
        let mut $var = ::core::pin::pin!($crate::init::__internal::StackInit$(::<$t>)?::uninit());
        let mut $var = match $crate::init::__internal::StackInit::init($var, val) {
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
/// ```rust,ignore
/// # #![allow(clippy::disallowed_names)]
/// # use kernel::{init, pin_init, stack_try_pin_init, init::*, sync::Mutex, new_mutex};
/// # use macros::pin_data;
/// # use core::{alloc::AllocError, pin::Pin};
/// #[pin_data]
/// struct Foo {
///     #[pin]
///     a: Mutex<usize>,
///     b: Box<Bar>,
/// }
///
/// struct Bar {
///     x: u32,
/// }
///
/// stack_try_pin_init!(let foo: Result<Pin<&mut Foo>, AllocError> = pin_init!(Foo {
///     a <- new_mutex!(42),
///     b: Box::new(Bar {
///         x: 64,
///     }, GFP_KERNEL)?,
/// }));
/// let foo = foo.unwrap();
/// pr_info!("a: {}", &*foo.a.lock());
/// ```
///
/// ```rust,ignore
/// # #![allow(clippy::disallowed_names)]
/// # use kernel::{init, pin_init, stack_try_pin_init, init::*, sync::Mutex, new_mutex};
/// # use macros::pin_data;
/// # use core::{alloc::AllocError, pin::Pin};
/// #[pin_data]
/// struct Foo {
///     #[pin]
///     a: Mutex<usize>,
///     b: Box<Bar>,
/// }
///
/// struct Bar {
///     x: u32,
/// }
///
/// stack_try_pin_init!(let foo: Pin<&mut Foo> =? pin_init!(Foo {
///     a <- new_mutex!(42),
///     b: Box::new(Bar {
///         x: 64,
///     }, GFP_KERNEL)?,
/// }));
/// pr_info!("a: {}", &*foo.a.lock());
/// # Ok::<_, AllocError>(())
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
        let mut $var = ::core::pin::pin!($crate::init::__internal::StackInit$(::<$t>)?::uninit());
        let mut $var = $crate::init::__internal::StackInit::init($var, val);
    };
    (let $var:ident $(: $t:ty)? =? $val:expr) => {
        let val = $val;
        let mut $var = ::core::pin::pin!($crate::init::__internal::StackInit$(::<$t>)?::uninit());
        let mut $var = $crate::init::__internal::StackInit::init($var, val)?;
    };
}

/// Construct an in-place, pinned initializer for `struct`s.
///
/// This macro defaults the error to [`Infallible`]. If you need [`Error`], then use
/// [`try_pin_init!`].
///
/// The syntax is almost identical to that of a normal `struct` initializer:
///
/// ```rust
/// # use kernel::{init, pin_init, macros::pin_data, init::*};
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
/// # Box::pin_init(demo(), GFP_KERNEL).unwrap();
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
/// When working with this API it is often desired to let others construct your types without
/// giving access to all fields. This is where you would normally write a plain function `new`
/// that would return a new instance of your type. With this API that is also possible.
/// However, there are a few extra things to keep in mind.
///
/// To create an initializer function, simply declare it like this:
///
/// ```rust
/// # use kernel::{init, pin_init, init::*};
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
/// # #![allow(clippy::disallowed_names)]
/// # use kernel::{init, pin_init, macros::pin_data, init::*};
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
/// let foo = Box::pin_init(Foo::new(), GFP_KERNEL);
/// ```
///
/// They can also easily embed it into their own `struct`s:
///
/// ```rust
/// # use kernel::{init, pin_init, macros::pin_data, init::*};
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
/// - In front of the initializer you can write `&this in` to have access to a [`NonNull<Self>`]
///   pointer named `this` inside of the initializer.
/// - Using struct update syntax one can place `..Zeroable::zeroed()` at the very end of the
///   struct, this initializes every field with 0 and then runs all initializers specified in the
///   body. This can only be done if [`Zeroable`] is implemented for the struct.
///
/// For instance:
///
/// ```rust
/// # use kernel::{macros::{Zeroable, pin_data}, pin_init};
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
/// pin_init!(&this in Buf {
///     buf: [0; 64],
///     // SAFETY: TODO.
///     ptr: unsafe { addr_of_mut!((*this.as_ptr()).buf).cast() },
///     pin: PhantomPinned,
/// });
/// pin_init!(Buf {
///     buf: [1; 64],
///     ..Zeroable::zeroed()
/// });
/// ```
///
/// [`try_pin_init!`]: kernel::try_pin_init
/// [`NonNull<Self>`]: core::ptr::NonNull
// For a detailed example of how this macro works, see the module documentation of the hidden
// module `__internal` inside of `init/__internal.rs`.
#[macro_export]
macro_rules! pin_init {
    ($(&$this:ident in)? $t:ident $(::<$($generics:ty),* $(,)?>)? {
        $($fields:tt)*
    }) => {
        $crate::__init_internal!(
            @this($($this)?),
            @typ($t $(::<$($generics),*>)?),
            @fields($($fields)*),
            @error(::core::convert::Infallible),
            @data(PinData, use_data),
            @has_data(HasPinData, __pin_data),
            @construct_closure(pin_init_from_closure),
            @munch_fields($($fields)*),
        )
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
/// This macro defaults the error to [`Error`].
///
/// The syntax is identical to [`pin_init!`] with the following exception: you can append `? $type`
/// after the `struct` initializer to specify the error type you want to use.
///
/// # Examples
///
/// ```rust
/// # #![feature(new_uninit)]
/// use kernel::{init::{self, PinInit}, error::Error};
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
///             big: Box::init(init::zeroed(), GFP_KERNEL)?,
///             small: [0; 1024 * 1024],
///             ptr: core::ptr::null_mut(),
///         }? Error)
///     }
/// }
/// ```
// For a detailed example of how this macro works, see the module documentation of the hidden
// module `__internal` inside of `init/__internal.rs`.
#[macro_export]
macro_rules! try_pin_init {
    ($(&$this:ident in)? $t:ident $(::<$($generics:ty),* $(,)?>)? {
        $($fields:tt)*
    }) => {
        $crate::__init_internal!(
            @this($($this)?),
            @typ($t $(::<$($generics),*>)? ),
            @fields($($fields)*),
            @error($crate::error::Error),
            @data(PinData, use_data),
            @has_data(HasPinData, __pin_data),
            @construct_closure(pin_init_from_closure),
            @munch_fields($($fields)*),
        )
    };
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
    };
}

/// Construct an in-place initializer for `struct`s.
///
/// This macro defaults the error to [`Infallible`]. If you need [`Error`], then use
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
/// [`try_init!`]: crate::try_init!
// For a detailed example of how this macro works, see the module documentation of the hidden
// module `__internal` inside of `init/__internal.rs`.
#[macro_export]
macro_rules! init {
    ($(&$this:ident in)? $t:ident $(::<$($generics:ty),* $(,)?>)? {
        $($fields:tt)*
    }) => {
        $crate::__init_internal!(
            @this($($this)?),
            @typ($t $(::<$($generics),*>)?),
            @fields($($fields)*),
            @error(::core::convert::Infallible),
            @data(InitData, /*no use_data*/),
            @has_data(HasInitData, __init_data),
            @construct_closure(init_from_closure),
            @munch_fields($($fields)*),
        )
    }
}

/// Construct an in-place fallible initializer for `struct`s.
///
/// This macro defaults the error to [`Error`]. If you need [`Infallible`], then use
/// [`init!`].
///
/// The syntax is identical to [`try_pin_init!`]. If you want to specify a custom error,
/// append `? $type` after the `struct` initializer.
/// The safety caveats from [`try_pin_init!`] also apply:
/// - `unsafe` code must guarantee either full initialization or return an error and allow
///   deallocation of the memory.
/// - the fields are initialized in the order given in the initializer.
/// - no references to fields are allowed to be created inside of the initializer.
///
/// # Examples
///
/// ```rust
/// use kernel::{init::{PinInit, zeroed}, error::Error};
/// struct BigBuf {
///     big: Box<[u8; 1024 * 1024 * 1024]>,
///     small: [u8; 1024 * 1024],
/// }
///
/// impl BigBuf {
///     fn new() -> impl Init<Self, Error> {
///         try_init!(Self {
///             big: Box::init(zeroed(), GFP_KERNEL)?,
///             small: [0; 1024 * 1024],
///         }? Error)
///     }
/// }
/// ```
// For a detailed example of how this macro works, see the module documentation of the hidden
// module `__internal` inside of `init/__internal.rs`.
#[macro_export]
macro_rules! try_init {
    ($(&$this:ident in)? $t:ident $(::<$($generics:ty),* $(,)?>)? {
        $($fields:tt)*
    }) => {
        $crate::__init_internal!(
            @this($($this)?),
            @typ($t $(::<$($generics),*>)?),
            @fields($($fields)*),
            @error($crate::error::Error),
            @data(InitData, /*no use_data*/),
            @has_data(HasInitData, __init_data),
            @construct_closure(init_from_closure),
            @munch_fields($($fields)*),
        )
    };
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
    };
}

/// Asserts that a field on a struct using `#[pin_data]` is marked with `#[pin]` ie. that it is
/// structurally pinned.
///
/// # Example
///
/// This will succeed:
/// ```
/// use kernel::assert_pinned;
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
// TODO: replace with `compile_fail` when supported.
/// ```ignore
/// use kernel::assert_pinned;
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
/// use kernel::assert_pinned;
/// #[pin_data]
/// struct Foo<T> {
///     #[pin]
///     elem: T,
/// }
///
/// impl<T> Foo<T> {
///     fn project(self: Pin<&mut Self>) -> Pin<&mut T> {
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
            let data = unsafe { <$ty as $crate::init::__internal::HasPinData>::__pin_data() };
            let init = $crate::init::__internal::AlwaysFail::<$field_ty>::new();
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
/// be [`Box<T>`], [`Arc<T>`], [`UniqueArc<T>`] or even the stack (see [`stack_pin_init!`]). Use the
/// [`InPlaceInit::pin_init`] function of a smart pointer like [`Arc<T>`] on this.
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
/// [`Arc<T>`]: crate::sync::Arc
/// [`Arc::pin_init`]: crate::sync::Arc::pin_init
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
    /// # #![allow(clippy::disallowed_names)]
    /// use kernel::{types::Opaque, init::pin_init_from_closure};
    /// #[repr(C)]
    /// struct RawFoo([u8; 16]);
    /// extern {
    ///     fn init_foo(_: *mut RawFoo);
    /// }
    ///
    /// #[pin_data]
    /// struct Foo {
    ///     #[pin]
    ///     raw: Opaque<RawFoo>,
    /// }
    ///
    /// impl Foo {
    ///     fn setup(self: Pin<&mut Self>) {
    ///         pr_info!("Setting up foo");
    ///     }
    /// }
    ///
    /// let foo = pin_init!(Foo {
    ///     // SAFETY: TODO.
    ///     raw <- unsafe {
    ///         Opaque::ffi_init(|s| {
    ///             init_foo(s);
    ///         })
    ///     },
    /// }).pin_chain(|foo| {
    ///     foo.setup();
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
pub struct ChainPinInit<I, F, T: ?Sized, E>(I, F, __internal::Invariant<(E, Box<T>)>);

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
/// be [`Box<T>`], [`Arc<T>`], [`UniqueArc<T>`] or even the stack (see [`stack_pin_init!`]). Use the
/// [`InPlaceInit::init`] function of a smart pointer like [`Arc<T>`] on this. Because
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
/// [`Arc<T>`]: crate::sync::Arc
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
    /// # #![allow(clippy::disallowed_names)]
    /// use kernel::{types::Opaque, init::{self, init_from_closure}};
    /// struct Foo {
    ///     buf: [u8; 1_000_000],
    /// }
    ///
    /// impl Foo {
    ///     fn setup(&mut self) {
    ///         pr_info!("Setting up foo");
    ///     }
    /// }
    ///
    /// let foo = init!(Foo {
    ///     buf <- init::zeroed()
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
pub struct ChainInit<I, F, T: ?Sized, E>(I, F, __internal::Invariant<(E, Box<T>)>);

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
/// use kernel::{error::Error, init::init_array_from_fn};
/// let array: Box<[usize; 1_000]> = Box::init::<Error>(init_array_from_fn(|i| i), GFP_KERNEL).unwrap();
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
        // Counts the number of initialized elements and when dropped drops that many elements from
        // `slot`.
        let mut init_count = ScopeGuard::new_with_data(0, |i| {
            // We now free every element that has been initialized before.
            // SAFETY: The loop initialized exactly the values from 0..i and since we
            // return `Err` below, the caller will consider the memory at `slot` as
            // uninitialized.
            unsafe { ptr::drop_in_place(ptr::slice_from_raw_parts_mut(slot, i)) };
        });
        for i in 0..N {
            let init = make_init(i);
            // SAFETY: Since 0 <= `i` < N, it is still in bounds of `[T; N]`.
            let ptr = unsafe { slot.add(i) };
            // SAFETY: The pointer is derived from `slot` and thus satisfies the `__init`
            // requirements.
            unsafe { init.__init(ptr) }?;
            *init_count += 1;
        }
        init_count.dismiss();
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
/// use kernel::{sync::{Arc, Mutex}, init::pin_init_array_from_fn, new_mutex};
/// let array: Arc<[Mutex<usize>; 1_000]> =
///     Arc::pin_init(pin_init_array_from_fn(|i| new_mutex!(i)), GFP_KERNEL).unwrap();
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
        // Counts the number of initialized elements and when dropped drops that many elements from
        // `slot`.
        let mut init_count = ScopeGuard::new_with_data(0, |i| {
            // We now free every element that has been initialized before.
            // SAFETY: The loop initialized exactly the values from 0..i and since we
            // return `Err` below, the caller will consider the memory at `slot` as
            // uninitialized.
            unsafe { ptr::drop_in_place(ptr::slice_from_raw_parts_mut(slot, i)) };
        });
        for i in 0..N {
            let init = make_init(i);
            // SAFETY: Since 0 <= `i` < N, it is still in bounds of `[T; N]`.
            let ptr = unsafe { slot.add(i) };
            // SAFETY: The pointer is derived from `slot` and thus satisfies the `__init`
            // requirements.
            unsafe { init.__pinned_init(ptr) }?;
            *init_count += 1;
        }
        init_count.dismiss();
        Ok(())
    };
    // SAFETY: The initializer above initializes every element of the array. On failure it drops
    // any initialized elements and returns `Err`.
    unsafe { pin_init_from_closure(init) }
}

// SAFETY: Every type can be initialized by-value.
unsafe impl<T, E> Init<T, E> for T {
    unsafe fn __init(self, slot: *mut T) -> Result<(), E> {
        // SAFETY: TODO.
        unsafe { slot.write(self) };
        Ok(())
    }
}

// SAFETY: Every type can be initialized by-value. `__pinned_init` calls `__init`.
unsafe impl<T, E> PinInit<T, E> for T {
    unsafe fn __pinned_init(self, slot: *mut T) -> Result<(), E> {
        // SAFETY: TODO.
        unsafe { self.__init(slot) }
    }
}

/// Smart pointer that can initialize memory in-place.
pub trait InPlaceInit<T>: Sized {
    /// Pinned version of `Self`.
    ///
    /// If a type already implicitly pins its pointee, `Pin<Self>` is unnecessary. In this case use
    /// `Self`, otherwise just use `Pin<Self>`.
    type PinnedSelf;

    /// Use the given pin-initializer to pin-initialize a `T` inside of a new smart pointer of this
    /// type.
    ///
    /// If `T: !Unpin` it will not be able to move afterwards.
    fn try_pin_init<E>(init: impl PinInit<T, E>, flags: Flags) -> Result<Self::PinnedSelf, E>
    where
        E: From<AllocError>;

    /// Use the given pin-initializer to pin-initialize a `T` inside of a new smart pointer of this
    /// type.
    ///
    /// If `T: !Unpin` it will not be able to move afterwards.
    fn pin_init<E>(init: impl PinInit<T, E>, flags: Flags) -> error::Result<Self::PinnedSelf>
    where
        Error: From<E>,
    {
        // SAFETY: We delegate to `init` and only change the error type.
        let init = unsafe {
            pin_init_from_closure(|slot| init.__pinned_init(slot).map_err(|e| Error::from(e)))
        };
        Self::try_pin_init(init, flags)
    }

    /// Use the given initializer to in-place initialize a `T`.
    fn try_init<E>(init: impl Init<T, E>, flags: Flags) -> Result<Self, E>
    where
        E: From<AllocError>;

    /// Use the given initializer to in-place initialize a `T`.
    fn init<E>(init: impl Init<T, E>, flags: Flags) -> error::Result<Self>
    where
        Error: From<E>,
    {
        // SAFETY: We delegate to `init` and only change the error type.
        let init = unsafe {
            init_from_closure(|slot| init.__pinned_init(slot).map_err(|e| Error::from(e)))
        };
        Self::try_init(init, flags)
    }
}

impl<T> InPlaceInit<T> for Arc<T> {
    type PinnedSelf = Self;

    #[inline]
    fn try_pin_init<E>(init: impl PinInit<T, E>, flags: Flags) -> Result<Self::PinnedSelf, E>
    where
        E: From<AllocError>,
    {
        UniqueArc::try_pin_init(init, flags).map(|u| u.into())
    }

    #[inline]
    fn try_init<E>(init: impl Init<T, E>, flags: Flags) -> Result<Self, E>
    where
        E: From<AllocError>,
    {
        UniqueArc::try_init(init, flags).map(|u| u.into())
    }
}

impl<T> InPlaceInit<T> for Box<T> {
    type PinnedSelf = Pin<Self>;

    #[inline]
    fn try_pin_init<E>(init: impl PinInit<T, E>, flags: Flags) -> Result<Self::PinnedSelf, E>
    where
        E: From<AllocError>,
    {
        <Box<_> as BoxExt<_>>::new_uninit(flags)?.write_pin_init(init)
    }

    #[inline]
    fn try_init<E>(init: impl Init<T, E>, flags: Flags) -> Result<Self, E>
    where
        E: From<AllocError>,
    {
        <Box<_> as BoxExt<_>>::new_uninit(flags)?.write_init(init)
    }
}

impl<T> InPlaceInit<T> for UniqueArc<T> {
    type PinnedSelf = Pin<Self>;

    #[inline]
    fn try_pin_init<E>(init: impl PinInit<T, E>, flags: Flags) -> Result<Self::PinnedSelf, E>
    where
        E: From<AllocError>,
    {
        UniqueArc::new_uninit(flags)?.write_pin_init(init)
    }

    #[inline]
    fn try_init<E>(init: impl Init<T, E>, flags: Flags) -> Result<Self, E>
    where
        E: From<AllocError>,
    {
        UniqueArc::new_uninit(flags)?.write_init(init)
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

impl<T> InPlaceWrite<T> for Box<MaybeUninit<T>> {
    type Initialized = Box<T>;

    fn write_init<E>(mut self, init: impl Init<T, E>) -> Result<Self::Initialized, E> {
        let slot = self.as_mut_ptr();
        // SAFETY: When init errors/panics, slot will get deallocated but not dropped,
        // slot is valid.
        unsafe { init.__init(slot)? };
        // SAFETY: All fields have been initialized.
        Ok(unsafe { self.assume_init() })
    }

    fn write_pin_init<E>(mut self, init: impl PinInit<T, E>) -> Result<Pin<Self::Initialized>, E> {
        let slot = self.as_mut_ptr();
        // SAFETY: When init errors/panics, slot will get deallocated but not dropped,
        // slot is valid and will not be moved, because we pin it later.
        unsafe { init.__pinned_init(slot)? };
        // SAFETY: All fields have been initialized.
        Ok(unsafe { self.assume_init() }.into())
    }
}

impl<T> InPlaceWrite<T> for UniqueArc<MaybeUninit<T>> {
    type Initialized = UniqueArc<T>;

    fn write_init<E>(mut self, init: impl Init<T, E>) -> Result<Self::Initialized, E> {
        let slot = self.as_mut_ptr();
        // SAFETY: When init errors/panics, slot will get deallocated but not dropped,
        // slot is valid.
        unsafe { init.__init(slot)? };
        // SAFETY: All fields have been initialized.
        Ok(unsafe { self.assume_init() })
    }

    fn write_pin_init<E>(mut self, init: impl PinInit<T, E>) -> Result<Pin<Self::Initialized>, E> {
        let slot = self.as_mut_ptr();
        // SAFETY: When init errors/panics, slot will get deallocated but not dropped,
        // slot is valid and will not be moved, because we pin it later.
        unsafe { init.__pinned_init(slot)? };
        // SAFETY: All fields have been initialized.
        Ok(unsafe { self.assume_init() }.into())
    }
}

/// Trait facilitating pinned destruction.
///
/// Use [`pinned_drop`] to implement this trait safely:
///
/// ```rust
/// # use kernel::sync::Mutex;
/// use kernel::macros::pinned_drop;
/// use core::pin::Pin;
/// #[pin_data(PinnedDrop)]
/// struct Foo {
///     #[pin]
///     mtx: Mutex<usize>,
/// }
///
/// #[pinned_drop]
/// impl PinnedDrop for Foo {
///     fn drop(self: Pin<&mut Self>) {
///         pr_info!("Foo is being dropped!");
///     }
/// }
/// ```
///
/// # Safety
///
/// This trait must be implemented via the [`pinned_drop`] proc-macro attribute on the impl.
///
/// [`pinned_drop`]: kernel::macros::pinned_drop
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
pub unsafe trait Zeroable {}

/// Create a new zeroed T.
///
/// The returned initializer will write `0x00` to every byte of the given `slot`.
#[inline]
pub fn zeroed<T: Zeroable>() -> impl Init<T> {
    // SAFETY: Because `T: Zeroable`, all bytes zero is a valid bit pattern for `T`
    // and because we write all zeroes, the memory is initialized.
    unsafe {
        init_from_closure(|slot: *mut T| {
            slot.write_bytes(0, 1);
            Ok(())
        })
    }
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
    // SAFETY: Type is allowed to take any value, including all zeros.
    {<T>} Opaque<T>,

    // SAFETY: `T: Zeroable` and `UnsafeCell` is `repr(transparent)`.
    {<T: ?Sized + Zeroable>} UnsafeCell<T>,

    // SAFETY: All zeros is equivalent to `None` (option layout optimization guarantee).
    Option<NonZeroU8>, Option<NonZeroU16>, Option<NonZeroU32>, Option<NonZeroU64>,
    Option<NonZeroU128>, Option<NonZeroUsize>,
    Option<NonZeroI8>, Option<NonZeroI16>, Option<NonZeroI32>, Option<NonZeroI64>,
    Option<NonZeroI128>, Option<NonZeroIsize>,

    // SAFETY: All zeros is equivalent to `None` (option layout optimization guarantee).
    //
    // In this case we are allowed to use `T: ?Sized`, since all zeros is the `None` variant.
    {<T: ?Sized>} Option<NonNull<T>>,
    {<T: ?Sized>} Option<Box<T>>,

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
