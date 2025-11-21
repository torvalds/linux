[![Crates.io](https://img.shields.io/crates/v/pin-init.svg)](https://crates.io/crates/pin-init)
[![Documentation](https://docs.rs/pin-init/badge.svg)](https://docs.rs/pin-init/)
[![Dependency status](https://deps.rs/repo/github/Rust-for-Linux/pin-init/status.svg)](https://deps.rs/repo/github/Rust-for-Linux/pin-init)
![License](https://img.shields.io/crates/l/pin-init)
[![Toolchain](https://img.shields.io/badge/toolchain-nightly-red)](#nightly-only)
![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/Rust-for-Linux/pin-init/test.yml)
# `pin-init`

> [!NOTE]
> 
> This crate was originally named [`pinned-init`], but the migration to
> `pin-init` is not yet complete. The `legcay` branch contains the current
> version of the `pinned-init` crate & the `main` branch already incorporates
> the rename to `pin-init`.
>
> There are still some changes needed on the kernel side before the migration
> can be completed.

[`pinned-init`]: https://crates.io/crates/pinned-init

<!-- cargo-rdme start -->

Library to safely and fallibly initialize pinned `struct`s using in-place constructors.

[Pinning][pinning] is Rust's way of ensuring data does not move.

It also allows in-place initialization of big `struct`s that would otherwise produce a stack
overflow.

This library's main use-case is in [Rust-for-Linux]. Although this version can be used
standalone.

There are cases when you want to in-place initialize a struct. For example when it is very big
and moving it from the stack is not an option, because it is bigger than the stack itself.
Another reason would be that you need the address of the object to initialize it. This stands
in direct conflict with Rust's normal process of first initializing an object and then moving
it into it's final memory location. For more information, see
<https://rust-for-linux.com/the-safe-pinned-initialization-problem>.

This library allows you to do in-place initialization safely.

### Nightly Needed for `alloc` feature

This library requires the [`allocator_api` unstable feature] when the `alloc` feature is
enabled and thus this feature can only be used with a nightly compiler. When enabling the
`alloc` feature, the user will be required to activate `allocator_api` as well.

[`allocator_api` unstable feature]: https://doc.rust-lang.org/nightly/unstable-book/library-features/allocator-api.html

The feature is enabled by default, thus by default `pin-init` will require a nightly compiler.
However, using the crate on stable compilers is possible by disabling `alloc`. In practice this
will require the `std` feature, because stable compilers have neither `Box` nor `Arc` in no-std
mode.

### Nightly needed for `unsafe-pinned` feature

This feature enables the `Wrapper` implementation on the unstable `core::pin::UnsafePinned` type.
This requires the [`unsafe_pinned` unstable feature](https://github.com/rust-lang/rust/issues/125735)
and therefore a nightly compiler. Note that this feature is not enabled by default.

## Overview

To initialize a `struct` with an in-place constructor you will need two things:
- an in-place constructor,
- a memory location that can hold your `struct` (this can be the [stack], an [`Arc<T>`],
  [`Box<T>`] or any other smart pointer that supports this library).

To get an in-place constructor there are generally three options:
- directly creating an in-place constructor using the [`pin_init!`] macro,
- a custom function/macro returning an in-place constructor provided by someone else,
- using the unsafe function [`pin_init_from_closure()`] to manually create an initializer.

Aside from pinned initialization, this library also supports in-place construction without
pinning, the macros/types/functions are generally named like the pinned variants without the
`pin_` prefix.

## Examples

Throughout the examples we will often make use of the `CMutex` type which can be found in
`../examples/mutex.rs`. It is essentially a userland rebuild of the `struct mutex` type from
the Linux kernel. It also uses a wait list and a basic spinlock. Importantly the wait list
requires it to be pinned to be locked and thus is a prime candidate for using this library.

### Using the [`pin_init!`] macro

If you want to use [`PinInit`], then you will have to annotate your `struct` with
`#[`[`pin_data`]`]`. It is a macro that uses `#[pin]` as a marker for
[structurally pinned fields]. After doing this, you can then create an in-place constructor via
[`pin_init!`]. The syntax is almost the same as normal `struct` initializers. The difference is
that you need to write `<-` instead of `:` for fields that you want to initialize in-place.

```rust
use pin_init::{pin_data, pin_init, InPlaceInit};

#[pin_data]
struct Foo {
    #[pin]
    a: CMutex<usize>,
    b: u32,
}

let foo = pin_init!(Foo {
    a <- CMutex::new(42),
    b: 24,
});
```

`foo` now is of the type [`impl PinInit<Foo>`]. We can now use any smart pointer that we like
(or just the stack) to actually initialize a `Foo`:

```rust
let foo: Result<Pin<Box<Foo>>, AllocError> = Box::pin_init(foo);
```

For more information see the [`pin_init!`] macro.

### Using a custom function/macro that returns an initializer

Many types that use this library supply a function/macro that returns an initializer, because
the above method only works for types where you can access the fields.

```rust
let mtx: Result<Pin<Arc<CMutex<usize>>>, _> = Arc::pin_init(CMutex::new(42));
```

To declare an init macro/function you just return an [`impl PinInit<T, E>`]:

```rust
#[pin_data]
struct DriverData {
    #[pin]
    status: CMutex<i32>,
    buffer: Box<[u8; 1_000_000]>,
}

impl DriverData {
    fn new() -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            status <- CMutex::new(0),
            buffer: Box::init(pin_init::init_zeroed())?,
        }? Error)
    }
}
```

### Manual creation of an initializer

Often when working with primitives the previous approaches are not sufficient. That is where
[`pin_init_from_closure()`] comes in. This `unsafe` function allows you to create a
[`impl PinInit<T, E>`] directly from a closure. Of course you have to ensure that the closure
actually does the initialization in the correct way. Here are the things to look out for
(we are calling the parameter to the closure `slot`):
- when the closure returns `Ok(())`, then it has completed the initialization successfully, so
  `slot` now contains a valid bit pattern for the type `T`,
- when the closure returns `Err(e)`, then the caller may deallocate the memory at `slot`, so
  you need to take care to clean up anything if your initialization fails mid-way,
- you may assume that `slot` will stay pinned even after the closure returns until `drop` of
  `slot` gets called.

```rust
use pin_init::{pin_data, pinned_drop, PinInit, PinnedDrop, pin_init_from_closure};
use core::{
    ptr::addr_of_mut,
    marker::PhantomPinned,
    cell::UnsafeCell,
    pin::Pin,
    mem::MaybeUninit,
};
mod bindings {
    #[repr(C)]
    pub struct foo {
        /* fields from C ... */
    }
    extern "C" {
        pub fn init_foo(ptr: *mut foo);
        pub fn destroy_foo(ptr: *mut foo);
        #[must_use = "you must check the error return code"]
        pub fn enable_foo(ptr: *mut foo, flags: u32) -> i32;
    }
}

/// # Invariants
///
/// `foo` is always initialized
#[pin_data(PinnedDrop)]
pub struct RawFoo {
    #[pin]
    _p: PhantomPinned,
    #[pin]
    foo: UnsafeCell<MaybeUninit<bindings::foo>>,
}

impl RawFoo {
    pub fn new(flags: u32) -> impl PinInit<Self, i32> {
        // SAFETY:
        // - when the closure returns `Ok(())`, then it has successfully initialized and
        //   enabled `foo`,
        // - when it returns `Err(e)`, then it has cleaned up before
        unsafe {
            pin_init_from_closure(move |slot: *mut Self| {
                // `slot` contains uninit memory, avoid creating a reference.
                let foo = addr_of_mut!((*slot).foo);
                let foo = UnsafeCell::raw_get(foo).cast::<bindings::foo>();

                // Initialize the `foo`
                bindings::init_foo(foo);

                // Try to enable it.
                let err = bindings::enable_foo(foo, flags);
                if err != 0 {
                    // Enabling has failed, first clean up the foo and then return the error.
                    bindings::destroy_foo(foo);
                    Err(err)
                } else {
                    // All fields of `RawFoo` have been initialized, since `_p` is a ZST.
                    Ok(())
                }
            })
        }
    }
}

#[pinned_drop]
impl PinnedDrop for RawFoo {
    fn drop(self: Pin<&mut Self>) {
        // SAFETY: Since `foo` is initialized, destroying is safe.
        unsafe { bindings::destroy_foo(self.foo.get().cast::<bindings::foo>()) };
    }
}
```

For more information on how to use [`pin_init_from_closure()`], take a look at the uses inside
the `kernel` crate. The [`sync`] module is a good starting point.

[`sync`]: https://rust.docs.kernel.org/kernel/sync/index.html
[pinning]: https://doc.rust-lang.org/std/pin/index.html
[structurally pinned fields]: https://doc.rust-lang.org/std/pin/index.html#projections-and-structural-pinning
[stack]: https://docs.rs/pin-init/latest/pin_init/macro.stack_pin_init.html
[`impl PinInit<Foo>`]: https://docs.rs/pin-init/latest/pin_init/trait.PinInit.html
[`impl PinInit<T, E>`]: https://docs.rs/pin-init/latest/pin_init/trait.PinInit.html
[`impl Init<T, E>`]: https://docs.rs/pin-init/latest/pin_init/trait.Init.html
[Rust-for-Linux]: https://rust-for-linux.com/

<!-- cargo-rdme end -->

<!-- These links are not picked up by cargo-rdme, since they are behind cfgs... -->
[`Arc<T>`]: https://doc.rust-lang.org/stable/alloc/sync/struct.Arc.html
[`Box<T>`]: https://doc.rust-lang.org/stable/alloc/boxed/struct.Box.html
