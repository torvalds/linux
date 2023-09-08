// SPDX-License-Identifier: Apache-2.0 OR MIT

//! This module provides the macros that actually implement the proc-macros `pin_data` and
//! `pinned_drop`.
//!
//! These macros should never be called directly, since they expect their input to be
//! in a certain format which is internal. Use the proc-macros instead.
//!
//! This architecture has been chosen because the kernel does not yet have access to `syn` which
//! would make matters a lot easier for implementing these as proc-macros.
//!
//! # Macro expansion example
//!
//! This section is intended for readers trying to understand the macros in this module and the
//! `pin_init!` macros from `init.rs`.
//!
//! We will look at the following example:
//!
//! ```rust
//! # use kernel::init::*;
//! #[pin_data]
//! #[repr(C)]
//! struct Bar<T> {
//!     #[pin]
//!     t: T,
//!     pub x: usize,
//! }
//!
//! impl<T> Bar<T> {
//!     fn new(t: T) -> impl PinInit<Self> {
//!         pin_init!(Self { t, x: 0 })
//!     }
//! }
//!
//! #[pin_data(PinnedDrop)]
//! struct Foo {
//!     a: usize,
//!     #[pin]
//!     b: Bar<u32>,
//! }
//!
//! #[pinned_drop]
//! impl PinnedDrop for Foo {
//!     fn drop(self: Pin<&mut Self>) {
//!         println!("{self:p} is getting dropped.");
//!     }
//! }
//!
//! let a = 42;
//! let initializer = pin_init!(Foo {
//!     a,
//!     b <- Bar::new(36),
//! });
//! ```
//!
//! This example includes the most common and important features of the pin-init API.
//!
//! Below you can find individual section about the different macro invocations. Here are some
//! general things we need to take into account when designing macros:
//! - use global paths, similarly to file paths, these start with the separator: `::core::panic!()`
//!   this ensures that the correct item is used, since users could define their own `mod core {}`
//!   and then their own `panic!` inside to execute arbitrary code inside of our macro.
//! - macro `unsafe` hygiene: we need to ensure that we do not expand arbitrary, user-supplied
//!   expressions inside of an `unsafe` block in the macro, because this would allow users to do
//!   `unsafe` operations without an associated `unsafe` block.
//!
//! ## `#[pin_data]` on `Bar`
//!
//! This macro is used to specify which fields are structurally pinned and which fields are not. It
//! is placed on the struct definition and allows `#[pin]` to be placed on the fields.
//!
//! Here is the definition of `Bar` from our example:
//!
//! ```rust
//! # use kernel::init::*;
//! #[pin_data]
//! #[repr(C)]
//! struct Bar<T> {
//!     t: T,
//!     pub x: usize,
//! }
//! ```
//!
//! This expands to the following code:
//!
//! ```rust
//! // Firstly the normal definition of the struct, attributes are preserved:
//! #[repr(C)]
//! struct Bar<T> {
//!     t: T,
//!     pub x: usize,
//! }
//! // Then an anonymous constant is defined, this is because we do not want any code to access the
//! // types that we define inside:
//! const _: () = {
//!     // We define the pin-data carrying struct, it is a ZST and needs to have the same generics,
//!     // since we need to implement access functions for each field and thus need to know its
//!     // type.
//!     struct __ThePinData<T> {
//!         __phantom: ::core::marker::PhantomData<fn(Bar<T>) -> Bar<T>>,
//!     }
//!     // We implement `Copy` for the pin-data struct, since all functions it defines will take
//!     // `self` by value.
//!     impl<T> ::core::clone::Clone for __ThePinData<T> {
//!         fn clone(&self) -> Self {
//!             *self
//!         }
//!     }
//!     impl<T> ::core::marker::Copy for __ThePinData<T> {}
//!     // For every field of `Bar`, the pin-data struct will define a function with the same name
//!     // and accessor (`pub` or `pub(crate)` etc.). This function will take a pointer to the
//!     // field (`slot`) and a `PinInit` or `Init` depending on the projection kind of the field
//!     // (if pinning is structural for the field, then `PinInit` otherwise `Init`).
//!     #[allow(dead_code)]
//!     impl<T> __ThePinData<T> {
//!         unsafe fn t<E>(
//!             self,
//!             slot: *mut T,
//!             init: impl ::kernel::init::Init<T, E>,
//!         ) -> ::core::result::Result<(), E> {
//!             unsafe { ::kernel::init::Init::__init(init, slot) }
//!         }
//!         pub unsafe fn x<E>(
//!             self,
//!             slot: *mut usize,
//!             init: impl ::kernel::init::Init<usize, E>,
//!         ) -> ::core::result::Result<(), E> {
//!             unsafe { ::kernel::init::Init::__init(init, slot) }
//!         }
//!     }
//!     // Implement the internal `HasPinData` trait that associates `Bar` with the pin-data struct
//!     // that we constructed beforehand.
//!     unsafe impl<T> ::kernel::init::__internal::HasPinData for Bar<T> {
//!         type PinData = __ThePinData<T>;
//!         unsafe fn __pin_data() -> Self::PinData {
//!             __ThePinData {
//!                 __phantom: ::core::marker::PhantomData,
//!             }
//!         }
//!     }
//!     // Implement the internal `PinData` trait that marks the pin-data struct as a pin-data
//!     // struct. This is important to ensure that no user can implement a rouge `__pin_data`
//!     // function without using `unsafe`.
//!     unsafe impl<T> ::kernel::init::__internal::PinData for __ThePinData<T> {
//!         type Datee = Bar<T>;
//!     }
//!     // Now we only want to implement `Unpin` for `Bar` when every structurally pinned field is
//!     // `Unpin`. In other words, whether `Bar` is `Unpin` only depends on structurally pinned
//!     // fields (those marked with `#[pin]`). These fields will be listed in this struct, in our
//!     // case no such fields exist, hence this is almost empty. The two phantomdata fields exist
//!     // for two reasons:
//!     // - `__phantom`: every generic must be used, since we cannot really know which generics
//!     //   are used, we declere all and then use everything here once.
//!     // - `__phantom_pin`: uses the `'__pin` lifetime and ensures that this struct is invariant
//!     //   over it. The lifetime is needed to work around the limitation that trait bounds must
//!     //   not be trivial, e.g. the user has a `#[pin] PhantomPinned` field -- this is
//!     //   unconditionally `!Unpin` and results in an error. The lifetime tricks the compiler
//!     //   into accepting these bounds regardless.
//!     #[allow(dead_code)]
//!     struct __Unpin<'__pin, T> {
//!         __phantom_pin: ::core::marker::PhantomData<fn(&'__pin ()) -> &'__pin ()>,
//!         __phantom: ::core::marker::PhantomData<fn(Bar<T>) -> Bar<T>>,
//!     }
//!     #[doc(hidden)]
//!     impl<'__pin, T>
//!         ::core::marker::Unpin for Bar<T> where __Unpin<'__pin, T>: ::core::marker::Unpin {}
//!     // Now we need to ensure that `Bar` does not implement `Drop`, since that would give users
//!     // access to `&mut self` inside of `drop` even if the struct was pinned. This could lead to
//!     // UB with only safe code, so we disallow this by giving a trait implementation error using
//!     // a direct impl and a blanket implementation.
//!     trait MustNotImplDrop {}
//!     // Normally `Drop` bounds do not have the correct semantics, but for this purpose they do
//!     // (normally people want to know if a type has any kind of drop glue at all, here we want
//!     // to know if it has any kind of custom drop glue, which is exactly what this bound does).
//!     #[allow(drop_bounds)]
//!     impl<T: ::core::ops::Drop> MustNotImplDrop for T {}
//!     impl<T> MustNotImplDrop for Bar<T> {}
//!     // Here comes a convenience check, if one implemented `PinnedDrop`, but forgot to add it to
//!     // `#[pin_data]`, then this will error with the same mechanic as above, this is not needed
//!     // for safety, but a good sanity check, since no normal code calls `PinnedDrop::drop`.
//!     #[allow(non_camel_case_types)]
//!     trait UselessPinnedDropImpl_you_need_to_specify_PinnedDrop {}
//!     impl<T: ::kernel::init::PinnedDrop>
//!         UselessPinnedDropImpl_you_need_to_specify_PinnedDrop for T {}
//!     impl<T> UselessPinnedDropImpl_you_need_to_specify_PinnedDrop for Bar<T> {}
//! };
//! ```
//!
//! ## `pin_init!` in `impl Bar`
//!
//! This macro creates an pin-initializer for the given struct. It requires that the struct is
//! annotated by `#[pin_data]`.
//!
//! Here is the impl on `Bar` defining the new function:
//!
//! ```rust
//! impl<T> Bar<T> {
//!     fn new(t: T) -> impl PinInit<Self> {
//!         pin_init!(Self { t, x: 0 })
//!     }
//! }
//! ```
//!
//! This expands to the following code:
//!
//! ```rust
//! impl<T> Bar<T> {
//!     fn new(t: T) -> impl PinInit<Self> {
//!         {
//!             // We do not want to allow arbitrary returns, so we declare this type as the `Ok`
//!             // return type and shadow it later when we insert the arbitrary user code. That way
//!             // there will be no possibility of returning without `unsafe`.
//!             struct __InitOk;
//!             // Get the pin-data type from the initialized type.
//!             // - the function is unsafe, hence the unsafe block
//!             // - we `use` the `HasPinData` trait in the block, it is only available in that
//!             //   scope.
//!             let data = unsafe {
//!                 use ::kernel::init::__internal::HasPinData;
//!                 Self::__pin_data()
//!             };
//!             // Use `data` to help with type inference, the closure supplied will have the type
//!             // `FnOnce(*mut Self) -> Result<__InitOk, Infallible>`.
//!             let init = ::kernel::init::__internal::PinData::make_closure::<
//!                 _,
//!                 __InitOk,
//!                 ::core::convert::Infallible,
//!             >(data, move |slot| {
//!                 {
//!                     // Shadow the structure so it cannot be used to return early. If a user
//!                     // tries to write `return Ok(__InitOk)`, then they get a type error, since
//!                     // that will refer to this struct instead of the one defined above.
//!                     struct __InitOk;
//!                     // This is the expansion of `t,`, which is syntactic sugar for `t: t,`.
//!                     unsafe { ::core::ptr::write(&raw mut (*slot).t, t) };
//!                     // Since initialization could fail later (not in this case, since the error
//!                     // type is `Infallible`) we will need to drop this field if it fails. This
//!                     // `DropGuard` will drop the field when it gets dropped and has not yet
//!                     // been forgotten. We make a reference to it, so users cannot `mem::forget`
//!                     // it from the initializer, since the name is the same as the field.
//!                     let t = &unsafe {
//!                         ::kernel::init::__internal::DropGuard::new(&raw mut (*slot).t)
//!                     };
//!                     // Expansion of `x: 0,`:
//!                     // Since this can be an arbitrary expression we cannot place it inside of
//!                     // the `unsafe` block, so we bind it here.
//!                     let x = 0;
//!                     unsafe { ::core::ptr::write(&raw mut (*slot).x, x) };
//!                     let x = &unsafe {
//!                         ::kernel::init::__internal::DropGuard::new(&raw mut (*slot).x)
//!                     };
//!
//!                     // Here we use the type checker to ensuer that every field has been
//!                     // initialized exactly once, since this is `if false` it will never get
//!                     // executed, but still type-checked.
//!                     // Additionally we abuse `slot` to automatically infer the correct type for
//!                     // the struct. This is also another check that every field is accessible
//!                     // from this scope.
//!                     #[allow(unreachable_code, clippy::diverging_sub_expression)]
//!                     if false {
//!                         unsafe {
//!                             ::core::ptr::write(
//!                                 slot,
//!                                 Self {
//!                                     // We only care about typecheck finding every field here,
//!                                     // the expression does not matter, just conjure one using
//!                                     // `panic!()`:
//!                                     t: ::core::panic!(),
//!                                     x: ::core::panic!(),
//!                                 },
//!                             );
//!                         };
//!                     }
//!                     // Since initialization has successfully completed, we can now forget the
//!                     // guards.
//!                     unsafe { ::kernel::init::__internal::DropGuard::forget(t) };
//!                     unsafe { ::kernel::init::__internal::DropGuard::forget(x) };
//!                 }
//!                 // We leave the scope above and gain access to the previously shadowed
//!                 // `__InitOk` that we need to return.
//!                 Ok(__InitOk)
//!             });
//!             // Change the return type of the closure.
//!             let init = move |slot| -> ::core::result::Result<(), ::core::convert::Infallible> {
//!                 init(slot).map(|__InitOk| ())
//!             };
//!             // Construct the initializer.
//!             let init = unsafe {
//!                 ::kernel::init::pin_init_from_closure::<_, ::core::convert::Infallible>(init)
//!             };
//!             init
//!         }
//!     }
//! }
//! ```
//!
//! ## `#[pin_data]` on `Foo`
//!
//! Since we already took a look at `#[pin_data]` on `Bar`, this section will only explain the
//! differences/new things in the expansion of the `Foo` definition:
//!
//! ```rust
//! #[pin_data(PinnedDrop)]
//! struct Foo {
//!     a: usize,
//!     #[pin]
//!     b: Bar<u32>,
//! }
//! ```
//!
//! This expands to the following code:
//!
//! ```rust
//! struct Foo {
//!     a: usize,
//!     b: Bar<u32>,
//! }
//! const _: () = {
//!     struct __ThePinData {
//!         __phantom: ::core::marker::PhantomData<fn(Foo) -> Foo>,
//!     }
//!     impl ::core::clone::Clone for __ThePinData {
//!         fn clone(&self) -> Self {
//!             *self
//!         }
//!     }
//!     impl ::core::marker::Copy for __ThePinData {}
//!     #[allow(dead_code)]
//!     impl __ThePinData {
//!         unsafe fn b<E>(
//!             self,
//!             slot: *mut Bar<u32>,
//!             // Note that this is `PinInit` instead of `Init`, this is because `b` is
//!             // structurally pinned, as marked by the `#[pin]` attribute.
//!             init: impl ::kernel::init::PinInit<Bar<u32>, E>,
//!         ) -> ::core::result::Result<(), E> {
//!             unsafe { ::kernel::init::PinInit::__pinned_init(init, slot) }
//!         }
//!         unsafe fn a<E>(
//!             self,
//!             slot: *mut usize,
//!             init: impl ::kernel::init::Init<usize, E>,
//!         ) -> ::core::result::Result<(), E> {
//!             unsafe { ::kernel::init::Init::__init(init, slot) }
//!         }
//!     }
//!     unsafe impl ::kernel::init::__internal::HasPinData for Foo {
//!         type PinData = __ThePinData;
//!         unsafe fn __pin_data() -> Self::PinData {
//!             __ThePinData {
//!                 __phantom: ::core::marker::PhantomData,
//!             }
//!         }
//!     }
//!     unsafe impl ::kernel::init::__internal::PinData for __ThePinData {
//!         type Datee = Foo;
//!     }
//!     #[allow(dead_code)]
//!     struct __Unpin<'__pin> {
//!         __phantom_pin: ::core::marker::PhantomData<fn(&'__pin ()) -> &'__pin ()>,
//!         __phantom: ::core::marker::PhantomData<fn(Foo) -> Foo>,
//!         // Since this field is `#[pin]`, it is listed here.
//!         b: Bar<u32>,
//!     }
//!     #[doc(hidden)]
//!     impl<'__pin> ::core::marker::Unpin for Foo where __Unpin<'__pin>: ::core::marker::Unpin {}
//!     // Since we specified `PinnedDrop` as the argument to `#[pin_data]`, we expect `Foo` to
//!     // implement `PinnedDrop`. Thus we do not need to prevent `Drop` implementations like
//!     // before, instead we implement it here and delegate to `PinnedDrop`.
//!     impl ::core::ops::Drop for Foo {
//!         fn drop(&mut self) {
//!             // Since we are getting dropped, no one else has a reference to `self` and thus we
//!             // can assume that we never move.
//!             let pinned = unsafe { ::core::pin::Pin::new_unchecked(self) };
//!             // Create the unsafe token that proves that we are inside of a destructor, this
//!             // type is only allowed to be created in a destructor.
//!             let token = unsafe { ::kernel::init::__internal::OnlyCallFromDrop::new() };
//!             ::kernel::init::PinnedDrop::drop(pinned, token);
//!         }
//!     }
//! };
//! ```
//!
//! ## `#[pinned_drop]` on `impl PinnedDrop for Foo`
//!
//! This macro is used to implement the `PinnedDrop` trait, since that trait is `unsafe` and has an
//! extra parameter that should not be used at all. The macro hides that parameter.
//!
//! Here is the `PinnedDrop` impl for `Foo`:
//!
//! ```rust
//! #[pinned_drop]
//! impl PinnedDrop for Foo {
//!     fn drop(self: Pin<&mut Self>) {
//!         println!("{self:p} is getting dropped.");
//!     }
//! }
//! ```
//!
//! This expands to the following code:
//!
//! ```rust
//! // `unsafe`, full path and the token parameter are added, everything else stays the same.
//! unsafe impl ::kernel::init::PinnedDrop for Foo {
//!     fn drop(self: Pin<&mut Self>, _: ::kernel::init::__internal::OnlyCallFromDrop) {
//!         println!("{self:p} is getting dropped.");
//!     }
//! }
//! ```
//!
//! ## `pin_init!` on `Foo`
//!
//! Since we already took a look at `pin_init!` on `Bar`, this section will only explain the
//! differences/new things in the expansion of `pin_init!` on `Foo`:
//!
//! ```rust
//! let a = 42;
//! let initializer = pin_init!(Foo {
//!     a,
//!     b <- Bar::new(36),
//! });
//! ```
//!
//! This expands to the following code:
//!
//! ```rust
//! let a = 42;
//! let initializer = {
//!     struct __InitOk;
//!     let data = unsafe {
//!         use ::kernel::init::__internal::HasPinData;
//!         Foo::__pin_data()
//!     };
//!     let init = ::kernel::init::__internal::PinData::make_closure::<
//!         _,
//!         __InitOk,
//!         ::core::convert::Infallible,
//!     >(data, move |slot| {
//!         {
//!             struct __InitOk;
//!             unsafe { ::core::ptr::write(&raw mut (*slot).a, a) };
//!             let a = &unsafe { ::kernel::init::__internal::DropGuard::new(&raw mut (*slot).a) };
//!             let b = Bar::new(36);
//!             // Here we use `data` to access the correct field and require that `b` is of type
//!             // `PinInit<Bar<u32>, Infallible>`.
//!             unsafe { data.b(&raw mut (*slot).b, b)? };
//!             let b = &unsafe { ::kernel::init::__internal::DropGuard::new(&raw mut (*slot).b) };
//!
//!             #[allow(unreachable_code, clippy::diverging_sub_expression)]
//!             if false {
//!                 unsafe {
//!                     ::core::ptr::write(
//!                         slot,
//!                         Foo {
//!                             a: ::core::panic!(),
//!                             b: ::core::panic!(),
//!                         },
//!                     );
//!                 };
//!             }
//!             unsafe { ::kernel::init::__internal::DropGuard::forget(a) };
//!             unsafe { ::kernel::init::__internal::DropGuard::forget(b) };
//!         }
//!         Ok(__InitOk)
//!     });
//!     let init = move |slot| -> ::core::result::Result<(), ::core::convert::Infallible> {
//!         init(slot).map(|__InitOk| ())
//!     };
//!     let init = unsafe {
//!         ::kernel::init::pin_init_from_closure::<_, ::core::convert::Infallible>(init)
//!     };
//!     init
//! };
//! ```

/// Creates a `unsafe impl<...> PinnedDrop for $type` block.
///
/// See [`PinnedDrop`] for more information.
#[doc(hidden)]
#[macro_export]
macro_rules! __pinned_drop {
    (
        @impl_sig($($impl_sig:tt)*),
        @impl_body(
            $(#[$($attr:tt)*])*
            fn drop($($sig:tt)*) {
                $($inner:tt)*
            }
        ),
    ) => {
        unsafe $($impl_sig)* {
            // Inherit all attributes and the type/ident tokens for the signature.
            $(#[$($attr)*])*
            fn drop($($sig)*, _: $crate::init::__internal::OnlyCallFromDrop) {
                $($inner)*
            }
        }
    }
}

/// This macro first parses the struct definition such that it separates pinned and not pinned
/// fields. Afterwards it declares the struct and implement the `PinData` trait safely.
#[doc(hidden)]
#[macro_export]
macro_rules! __pin_data {
    // Proc-macro entry point, this is supplied by the proc-macro pre-parsing.
    (parse_input:
        @args($($pinned_drop:ident)?),
        @sig(
            $(#[$($struct_attr:tt)*])*
            $vis:vis struct $name:ident
            $(where $($whr:tt)*)?
        ),
        @impl_generics($($impl_generics:tt)*),
        @ty_generics($($ty_generics:tt)*),
        @body({ $($fields:tt)* }),
    ) => {
        // We now use token munching to iterate through all of the fields. While doing this we
        // identify fields marked with `#[pin]`, these fields are the 'pinned fields'. The user
        // wants these to be structurally pinned. The rest of the fields are the
        // 'not pinned fields'. Additionally we collect all fields, since we need them in the right
        // order to declare the struct.
        //
        // In this call we also put some explaining comments for the parameters.
        $crate::__pin_data!(find_pinned_fields:
            // Attributes on the struct itself, these will just be propagated to be put onto the
            // struct definition.
            @struct_attrs($(#[$($struct_attr)*])*),
            // The visibility of the struct.
            @vis($vis),
            // The name of the struct.
            @name($name),
            // The 'impl generics', the generics that will need to be specified on the struct inside
            // of an `impl<$ty_generics>` block.
            @impl_generics($($impl_generics)*),
            // The 'ty generics', the generics that will need to be specified on the impl blocks.
            @ty_generics($($ty_generics)*),
            // The where clause of any impl block and the declaration.
            @where($($($whr)*)?),
            // The remaining fields tokens that need to be processed.
            // We add a `,` at the end to ensure correct parsing.
            @fields_munch($($fields)* ,),
            // The pinned fields.
            @pinned(),
            // The not pinned fields.
            @not_pinned(),
            // All fields.
            @fields(),
            // The accumulator containing all attributes already parsed.
            @accum(),
            // Contains `yes` or `` to indicate if `#[pin]` was found on the current field.
            @is_pinned(),
            // The proc-macro argument, this should be `PinnedDrop` or ``.
            @pinned_drop($($pinned_drop)?),
        );
    };
    (find_pinned_fields:
        @struct_attrs($($struct_attrs:tt)*),
        @vis($vis:vis),
        @name($name:ident),
        @impl_generics($($impl_generics:tt)*),
        @ty_generics($($ty_generics:tt)*),
        @where($($whr:tt)*),
        // We found a PhantomPinned field, this should generally be pinned!
        @fields_munch($field:ident : $($($(::)?core::)?marker::)?PhantomPinned, $($rest:tt)*),
        @pinned($($pinned:tt)*),
        @not_pinned($($not_pinned:tt)*),
        @fields($($fields:tt)*),
        @accum($($accum:tt)*),
        // This field is not pinned.
        @is_pinned(),
        @pinned_drop($($pinned_drop:ident)?),
    ) => {
        ::core::compile_error!(concat!(
            "The field `",
            stringify!($field),
            "` of type `PhantomPinned` only has an effect, if it has the `#[pin]` attribute.",
        ));
        $crate::__pin_data!(find_pinned_fields:
            @struct_attrs($($struct_attrs)*),
            @vis($vis),
            @name($name),
            @impl_generics($($impl_generics)*),
            @ty_generics($($ty_generics)*),
            @where($($whr)*),
            @fields_munch($($rest)*),
            @pinned($($pinned)* $($accum)* $field: ::core::marker::PhantomPinned,),
            @not_pinned($($not_pinned)*),
            @fields($($fields)* $($accum)* $field: ::core::marker::PhantomPinned,),
            @accum(),
            @is_pinned(),
            @pinned_drop($($pinned_drop)?),
        );
    };
    (find_pinned_fields:
        @struct_attrs($($struct_attrs:tt)*),
        @vis($vis:vis),
        @name($name:ident),
        @impl_generics($($impl_generics:tt)*),
        @ty_generics($($ty_generics:tt)*),
        @where($($whr:tt)*),
        // We reached the field declaration.
        @fields_munch($field:ident : $type:ty, $($rest:tt)*),
        @pinned($($pinned:tt)*),
        @not_pinned($($not_pinned:tt)*),
        @fields($($fields:tt)*),
        @accum($($accum:tt)*),
        // This field is pinned.
        @is_pinned(yes),
        @pinned_drop($($pinned_drop:ident)?),
    ) => {
        $crate::__pin_data!(find_pinned_fields:
            @struct_attrs($($struct_attrs)*),
            @vis($vis),
            @name($name),
            @impl_generics($($impl_generics)*),
            @ty_generics($($ty_generics)*),
            @where($($whr)*),
            @fields_munch($($rest)*),
            @pinned($($pinned)* $($accum)* $field: $type,),
            @not_pinned($($not_pinned)*),
            @fields($($fields)* $($accum)* $field: $type,),
            @accum(),
            @is_pinned(),
            @pinned_drop($($pinned_drop)?),
        );
    };
    (find_pinned_fields:
        @struct_attrs($($struct_attrs:tt)*),
        @vis($vis:vis),
        @name($name:ident),
        @impl_generics($($impl_generics:tt)*),
        @ty_generics($($ty_generics:tt)*),
        @where($($whr:tt)*),
        // We reached the field declaration.
        @fields_munch($field:ident : $type:ty, $($rest:tt)*),
        @pinned($($pinned:tt)*),
        @not_pinned($($not_pinned:tt)*),
        @fields($($fields:tt)*),
        @accum($($accum:tt)*),
        // This field is not pinned.
        @is_pinned(),
        @pinned_drop($($pinned_drop:ident)?),
    ) => {
        $crate::__pin_data!(find_pinned_fields:
            @struct_attrs($($struct_attrs)*),
            @vis($vis),
            @name($name),
            @impl_generics($($impl_generics)*),
            @ty_generics($($ty_generics)*),
            @where($($whr)*),
            @fields_munch($($rest)*),
            @pinned($($pinned)*),
            @not_pinned($($not_pinned)* $($accum)* $field: $type,),
            @fields($($fields)* $($accum)* $field: $type,),
            @accum(),
            @is_pinned(),
            @pinned_drop($($pinned_drop)?),
        );
    };
    (find_pinned_fields:
        @struct_attrs($($struct_attrs:tt)*),
        @vis($vis:vis),
        @name($name:ident),
        @impl_generics($($impl_generics:tt)*),
        @ty_generics($($ty_generics:tt)*),
        @where($($whr:tt)*),
        // We found the `#[pin]` attr.
        @fields_munch(#[pin] $($rest:tt)*),
        @pinned($($pinned:tt)*),
        @not_pinned($($not_pinned:tt)*),
        @fields($($fields:tt)*),
        @accum($($accum:tt)*),
        @is_pinned($($is_pinned:ident)?),
        @pinned_drop($($pinned_drop:ident)?),
    ) => {
        $crate::__pin_data!(find_pinned_fields:
            @struct_attrs($($struct_attrs)*),
            @vis($vis),
            @name($name),
            @impl_generics($($impl_generics)*),
            @ty_generics($($ty_generics)*),
            @where($($whr)*),
            @fields_munch($($rest)*),
            // We do not include `#[pin]` in the list of attributes, since it is not actually an
            // attribute that is defined somewhere.
            @pinned($($pinned)*),
            @not_pinned($($not_pinned)*),
            @fields($($fields)*),
            @accum($($accum)*),
            // Set this to `yes`.
            @is_pinned(yes),
            @pinned_drop($($pinned_drop)?),
        );
    };
    (find_pinned_fields:
        @struct_attrs($($struct_attrs:tt)*),
        @vis($vis:vis),
        @name($name:ident),
        @impl_generics($($impl_generics:tt)*),
        @ty_generics($($ty_generics:tt)*),
        @where($($whr:tt)*),
        // We reached the field declaration with visibility, for simplicity we only munch the
        // visibility and put it into `$accum`.
        @fields_munch($fvis:vis $field:ident $($rest:tt)*),
        @pinned($($pinned:tt)*),
        @not_pinned($($not_pinned:tt)*),
        @fields($($fields:tt)*),
        @accum($($accum:tt)*),
        @is_pinned($($is_pinned:ident)?),
        @pinned_drop($($pinned_drop:ident)?),
    ) => {
        $crate::__pin_data!(find_pinned_fields:
            @struct_attrs($($struct_attrs)*),
            @vis($vis),
            @name($name),
            @impl_generics($($impl_generics)*),
            @ty_generics($($ty_generics)*),
            @where($($whr)*),
            @fields_munch($field $($rest)*),
            @pinned($($pinned)*),
            @not_pinned($($not_pinned)*),
            @fields($($fields)*),
            @accum($($accum)* $fvis),
            @is_pinned($($is_pinned)?),
            @pinned_drop($($pinned_drop)?),
        );
    };
    (find_pinned_fields:
        @struct_attrs($($struct_attrs:tt)*),
        @vis($vis:vis),
        @name($name:ident),
        @impl_generics($($impl_generics:tt)*),
        @ty_generics($($ty_generics:tt)*),
        @where($($whr:tt)*),
        // Some other attribute, just put it into `$accum`.
        @fields_munch(#[$($attr:tt)*] $($rest:tt)*),
        @pinned($($pinned:tt)*),
        @not_pinned($($not_pinned:tt)*),
        @fields($($fields:tt)*),
        @accum($($accum:tt)*),
        @is_pinned($($is_pinned:ident)?),
        @pinned_drop($($pinned_drop:ident)?),
    ) => {
        $crate::__pin_data!(find_pinned_fields:
            @struct_attrs($($struct_attrs)*),
            @vis($vis),
            @name($name),
            @impl_generics($($impl_generics)*),
            @ty_generics($($ty_generics)*),
            @where($($whr)*),
            @fields_munch($($rest)*),
            @pinned($($pinned)*),
            @not_pinned($($not_pinned)*),
            @fields($($fields)*),
            @accum($($accum)* #[$($attr)*]),
            @is_pinned($($is_pinned)?),
            @pinned_drop($($pinned_drop)?),
        );
    };
    (find_pinned_fields:
        @struct_attrs($($struct_attrs:tt)*),
        @vis($vis:vis),
        @name($name:ident),
        @impl_generics($($impl_generics:tt)*),
        @ty_generics($($ty_generics:tt)*),
        @where($($whr:tt)*),
        // We reached the end of the fields, plus an optional additional comma, since we added one
        // before and the user is also allowed to put a trailing comma.
        @fields_munch($(,)?),
        @pinned($($pinned:tt)*),
        @not_pinned($($not_pinned:tt)*),
        @fields($($fields:tt)*),
        @accum(),
        @is_pinned(),
        @pinned_drop($($pinned_drop:ident)?),
    ) => {
        // Declare the struct with all fields in the correct order.
        $($struct_attrs)*
        $vis struct $name <$($impl_generics)*>
        where $($whr)*
        {
            $($fields)*
        }

        // We put the rest into this const item, because it then will not be accessible to anything
        // outside.
        const _: () = {
            // We declare this struct which will host all of the projection function for our type.
            // it will be invariant over all generic parameters which are inherited from the
            // struct.
            $vis struct __ThePinData<$($impl_generics)*>
            where $($whr)*
            {
                __phantom: ::core::marker::PhantomData<
                    fn($name<$($ty_generics)*>) -> $name<$($ty_generics)*>
                >,
            }

            impl<$($impl_generics)*> ::core::clone::Clone for __ThePinData<$($ty_generics)*>
            where $($whr)*
            {
                fn clone(&self) -> Self { *self }
            }

            impl<$($impl_generics)*> ::core::marker::Copy for __ThePinData<$($ty_generics)*>
            where $($whr)*
            {}

            // Make all projection functions.
            $crate::__pin_data!(make_pin_data:
                @pin_data(__ThePinData),
                @impl_generics($($impl_generics)*),
                @ty_generics($($ty_generics)*),
                @where($($whr)*),
                @pinned($($pinned)*),
                @not_pinned($($not_pinned)*),
            );

            // SAFETY: We have added the correct projection functions above to `__ThePinData` and
            // we also use the least restrictive generics possible.
            unsafe impl<$($impl_generics)*>
                $crate::init::__internal::HasPinData for $name<$($ty_generics)*>
            where $($whr)*
            {
                type PinData = __ThePinData<$($ty_generics)*>;

                unsafe fn __pin_data() -> Self::PinData {
                    __ThePinData { __phantom: ::core::marker::PhantomData }
                }
            }

            unsafe impl<$($impl_generics)*>
                $crate::init::__internal::PinData for __ThePinData<$($ty_generics)*>
            where $($whr)*
            {
                type Datee = $name<$($ty_generics)*>;
            }

            // This struct will be used for the unpin analysis. Since only structurally pinned
            // fields are relevant whether the struct should implement `Unpin`.
            #[allow(dead_code)]
            struct __Unpin <'__pin, $($impl_generics)*>
            where $($whr)*
            {
                __phantom_pin: ::core::marker::PhantomData<fn(&'__pin ()) -> &'__pin ()>,
                __phantom: ::core::marker::PhantomData<
                    fn($name<$($ty_generics)*>) -> $name<$($ty_generics)*>
                >,
                // Only the pinned fields.
                $($pinned)*
            }

            #[doc(hidden)]
            impl<'__pin, $($impl_generics)*> ::core::marker::Unpin for $name<$($ty_generics)*>
            where
                __Unpin<'__pin, $($ty_generics)*>: ::core::marker::Unpin,
                $($whr)*
            {}

            // We need to disallow normal `Drop` implementation, the exact behavior depends on
            // whether `PinnedDrop` was specified as the parameter.
            $crate::__pin_data!(drop_prevention:
                @name($name),
                @impl_generics($($impl_generics)*),
                @ty_generics($($ty_generics)*),
                @where($($whr)*),
                @pinned_drop($($pinned_drop)?),
            );
        };
    };
    // When no `PinnedDrop` was specified, then we have to prevent implementing drop.
    (drop_prevention:
        @name($name:ident),
        @impl_generics($($impl_generics:tt)*),
        @ty_generics($($ty_generics:tt)*),
        @where($($whr:tt)*),
        @pinned_drop(),
    ) => {
        // We prevent this by creating a trait that will be implemented for all types implementing
        // `Drop`. Additionally we will implement this trait for the struct leading to a conflict,
        // if it also implements `Drop`
        trait MustNotImplDrop {}
        #[allow(drop_bounds)]
        impl<T: ::core::ops::Drop> MustNotImplDrop for T {}
        impl<$($impl_generics)*> MustNotImplDrop for $name<$($ty_generics)*>
        where $($whr)* {}
        // We also take care to prevent users from writing a useless `PinnedDrop` implementation.
        // They might implement `PinnedDrop` correctly for the struct, but forget to give
        // `PinnedDrop` as the parameter to `#[pin_data]`.
        #[allow(non_camel_case_types)]
        trait UselessPinnedDropImpl_you_need_to_specify_PinnedDrop {}
        impl<T: $crate::init::PinnedDrop>
            UselessPinnedDropImpl_you_need_to_specify_PinnedDrop for T {}
        impl<$($impl_generics)*>
            UselessPinnedDropImpl_you_need_to_specify_PinnedDrop for $name<$($ty_generics)*>
        where $($whr)* {}
    };
    // When `PinnedDrop` was specified we just implement `Drop` and delegate.
    (drop_prevention:
        @name($name:ident),
        @impl_generics($($impl_generics:tt)*),
        @ty_generics($($ty_generics:tt)*),
        @where($($whr:tt)*),
        @pinned_drop(PinnedDrop),
    ) => {
        impl<$($impl_generics)*> ::core::ops::Drop for $name<$($ty_generics)*>
        where $($whr)*
        {
            fn drop(&mut self) {
                // SAFETY: Since this is a destructor, `self` will not move after this function
                // terminates, since it is inaccessible.
                let pinned = unsafe { ::core::pin::Pin::new_unchecked(self) };
                // SAFETY: Since this is a drop function, we can create this token to call the
                // pinned destructor of this type.
                let token = unsafe { $crate::init::__internal::OnlyCallFromDrop::new() };
                $crate::init::PinnedDrop::drop(pinned, token);
            }
        }
    };
    // If some other parameter was specified, we emit a readable error.
    (drop_prevention:
        @name($name:ident),
        @impl_generics($($impl_generics:tt)*),
        @ty_generics($($ty_generics:tt)*),
        @where($($whr:tt)*),
        @pinned_drop($($rest:tt)*),
    ) => {
        compile_error!(
            "Wrong parameters to `#[pin_data]`, expected nothing or `PinnedDrop`, got '{}'.",
            stringify!($($rest)*),
        );
    };
    (make_pin_data:
        @pin_data($pin_data:ident),
        @impl_generics($($impl_generics:tt)*),
        @ty_generics($($ty_generics:tt)*),
        @where($($whr:tt)*),
        @pinned($($(#[$($p_attr:tt)*])* $pvis:vis $p_field:ident : $p_type:ty),* $(,)?),
        @not_pinned($($(#[$($attr:tt)*])* $fvis:vis $field:ident : $type:ty),* $(,)?),
    ) => {
        // For every field, we create a projection function according to its projection type. If a
        // field is structurally pinned, then it must be initialized via `PinInit`, if it is not
        // structurally pinned, then it can be initialized via `Init`.
        //
        // The functions are `unsafe` to prevent accidentally calling them.
        #[allow(dead_code)]
        impl<$($impl_generics)*> $pin_data<$($ty_generics)*>
        where $($whr)*
        {
            $(
                $pvis unsafe fn $p_field<E>(
                    self,
                    slot: *mut $p_type,
                    init: impl $crate::init::PinInit<$p_type, E>,
                ) -> ::core::result::Result<(), E> {
                    unsafe { $crate::init::PinInit::__pinned_init(init, slot) }
                }
            )*
            $(
                $fvis unsafe fn $field<E>(
                    self,
                    slot: *mut $type,
                    init: impl $crate::init::Init<$type, E>,
                ) -> ::core::result::Result<(), E> {
                    unsafe { $crate::init::Init::__init(init, slot) }
                }
            )*
        }
    };
}
