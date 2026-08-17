// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2023 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

/// Unsafely implements trait(s) for a type.
///
/// # Safety
///
/// The trait impl must be sound.
///
/// When implementing `TryFromBytes`:
/// - If no `is_bit_valid` impl is provided, then it must be valid for
///   `is_bit_valid` to unconditionally return `true`. In other words, it must
///   be the case that any initialized sequence of bytes constitutes a valid
///   instance of `$ty`.
/// - If an `is_bit_valid` impl is provided, then the impl of `is_bit_valid`
///   must only return `true` if its argument refers to a valid `$ty`.
macro_rules! unsafe_impl {
    // Implement `$trait` for `$ty` with no bounds.
    ($(#[$attr:meta])* $ty:ty: $trait:ident $(; |$candidate:ident| $is_bit_valid:expr)?) => {{
        crate::util::macros::__unsafe();

        $(#[$attr])*
        // SAFETY: The caller promises that this is sound.
        unsafe impl $trait for $ty {
            unsafe_impl!(@method $trait $(; |$candidate| $is_bit_valid)?);
        }
    }};

    // Implement all `$traits` for `$ty` with no bounds.
    //
    // The 2 arms under this one are there so we can apply
    // N attributes for each one of M trait implementations.
    // The simple solution of:
    //
    // ($(#[$attrs:meta])* $ty:ty: $($traits:ident),*) => {
    //     $( unsafe_impl!( $(#[$attrs])* $ty: $traits ) );*
    // }
    //
    // Won't work. The macro processor sees that the outer repetition
    // contains both $attrs and $traits and expects them to match the same
    // amount of fragments.
    //
    // To solve this we must:
    // 1. Pack the attributes into a single token tree fragment we can match over.
    // 2. Expand the traits.
    // 3. Unpack and expand the attributes.
    ($(#[$attrs:meta])* $ty:ty: $($traits:ident),*) => {
        unsafe_impl!(@impl_traits_with_packed_attrs { $(#[$attrs])* } $ty: $($traits),*)
    };

    (@impl_traits_with_packed_attrs $attrs:tt $ty:ty: $($traits:ident),*) => {{
        $( unsafe_impl!(@unpack_attrs $attrs $ty: $traits); )*
    }};

    (@unpack_attrs { $(#[$attrs:meta])* } $ty:ty: $traits:ident) => {
        unsafe_impl!($(#[$attrs])* $ty: $traits);
    };

    // This arm is identical to the following one, except it contains a
    // preceding `const`. If we attempt to handle these with a single arm, there
    // is an inherent ambiguity between `const` (the keyword) and `const` (the
    // ident match for `$tyvar:ident`).
    //
    // To explain how this works, consider the following invocation:
    //
    //   unsafe_impl!(const N: usize, T: ?Sized + Copy => Clone for Foo<T>);
    //
    // In this invocation, here are the assignments to meta-variables:
    //
    //   |---------------|------------|
    //   | Meta-variable | Assignment |
    //   |---------------|------------|
    //   | $constname    |  N         |
    //   | $constty      |  usize     |
    //   | $tyvar        |  T         |
    //   | $optbound     |  Sized     |
    //   | $bound        |  Copy      |
    //   | $trait        |  Clone     |
    //   | $ty           |  Foo<T>    |
    //   |---------------|------------|
    //
    // The following arm has the same behavior with the exception of the lack of
    // support for a leading `const` parameter.
    (
        $(#[$attr:meta])*
        const $constname:ident : $constty:ident $(,)?
        $($tyvar:ident $(: $(? $optbound:ident $(+)?)* $($bound:ident $(+)?)* )?),*
        => $trait:ident for $ty:ty $(; |$candidate:ident| $is_bit_valid:expr)?
    ) => {
        unsafe_impl!(
            @inner
            $(#[$attr])*
            @const $constname: $constty,
            $($tyvar $(: $(? $optbound +)* + $($bound +)*)?,)*
            => $trait for $ty $(; |$candidate| $is_bit_valid)?
        );
    };
    (
        $(#[$attr:meta])*
        $($tyvar:ident $(: $(? $optbound:ident $(+)?)* $($bound:ident $(+)?)* )?),*
        => $trait:ident for $ty:ty $(; |$candidate:ident| $is_bit_valid:expr)?
    ) => {{
        unsafe_impl!(
            @inner
            $(#[$attr])*
            $($tyvar $(: $(? $optbound +)* + $($bound +)*)?,)*
            => $trait for $ty $(; |$candidate| $is_bit_valid)?
        );
    }};
    (
        @inner
        $(#[$attr:meta])*
        $(@const $constname:ident : $constty:ident,)*
        $($tyvar:ident $(: $(? $optbound:ident +)* + $($bound:ident +)* )?,)*
        => $trait:ident for $ty:ty $(; |$candidate:ident| $is_bit_valid:expr)?
    ) => {{
        crate::util::macros::__unsafe();

        $(#[$attr])*
        #[allow(non_local_definitions)]
        // SAFETY: The caller promises that this is sound.
        unsafe impl<$($tyvar $(: $(? $optbound +)* $($bound +)*)?),* $(, const $constname: $constty,)*> $trait for $ty {
            unsafe_impl!(@method $trait $(; |$candidate| $is_bit_valid)?);
        }
    }};

    (@method TryFromBytes ; |$candidate:ident| $is_bit_valid:expr) => {
        #[allow(clippy::missing_inline_in_public_items, dead_code)]
        #[cfg_attr(all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS), coverage(off))]
        fn only_derive_is_allowed_to_implement_this_trait() {}

        #[inline]
        fn is_bit_valid<Alignment>($candidate: Maybe<'_, Self, Alignment>) -> bool
        where
            Alignment: crate::invariant::Alignment,
        {
            $is_bit_valid
        }
    };
    (@method TryFromBytes) => {
        #[allow(clippy::missing_inline_in_public_items)]
        #[cfg_attr(all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS), coverage(off))]
        fn only_derive_is_allowed_to_implement_this_trait() {}
        #[inline(always)]
        fn is_bit_valid<Alignment>(_candidate: Maybe<'_, Self, Alignment>) -> bool
        where
            Alignment: crate::invariant::Alignment,
        {
            true
        }
    };
    (@method $trait:ident) => {
        #[allow(clippy::missing_inline_in_public_items, dead_code)]
        #[cfg_attr(all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS), coverage(off))]
        fn only_derive_is_allowed_to_implement_this_trait() {}
    };
    (@method $trait:ident; |$_candidate:ident| $_is_bit_valid:expr) => {
        compile_error!("Can't provide `is_bit_valid` impl for trait other than `TryFromBytes`");
    };
}

/// Implements `$trait` for `$ty` where `$ty: TransmuteFrom<$repr>` (and
/// vice-versa).
///
/// Calling this macro is safe; the internals of the macro emit appropriate
/// trait bounds which ensure that the given impl is sound.
macro_rules! impl_for_transmute_from {
    (
        $(#[$attr:meta])*
        $($tyvar:ident $(: $(? $optbound:ident $(+)?)* $($bound:ident $(+)?)* )?)?
        => $trait:ident for $ty:ty [$repr:ty]
    ) => {
        const _: () = {
            $(#[$attr])*
            #[allow(non_local_definitions)]

            // SAFETY: `is_trait<T, R>` (defined and used below) requires `T:
            // TransmuteFrom<R>`, `R: TransmuteFrom<T>`, and `R: $trait`. It is
            // called using `$ty` and `$repr`, ensuring that `$ty` and `$repr`
            // have equivalent bit validity, and ensuring that `$repr: $trait`.
            // The supported traits - `TryFromBytes`, `FromZeros`, `FromBytes`,
            // and `IntoBytes` - are defined only in terms of the bit validity
            // of a type. Therefore, `$repr: $trait` ensures that `$ty: $trait`
            // is sound.
            unsafe impl<$($tyvar $(: $(? $optbound +)* $($bound +)*)?)?> $trait for $ty {
                #[allow(dead_code, clippy::missing_inline_in_public_items)]
                #[cfg_attr(all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS), coverage(off))]
                fn only_derive_is_allowed_to_implement_this_trait() {
                    use crate::pointer::{*, invariant::Valid};

                    impl_for_transmute_from!(@assert_is_supported_trait $trait);

                    fn is_trait<T, R>()
                    where
                        T: TransmuteFrom<R, Valid, Valid> + ?Sized,
                        R: TransmuteFrom<T, Valid, Valid> + ?Sized,
                        R: $trait,
                    {
                    }

                    #[cfg_attr(all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS), coverage(off))]
                    fn f<$($tyvar $(: $(? $optbound +)* $($bound +)*)?)?>() {
                        is_trait::<$ty, $repr>();
                    }
                }

                impl_for_transmute_from!(
                    @is_bit_valid
                    $(<$tyvar $(: $(? $optbound +)* $($bound +)*)?>)?
                    $trait for $ty [$repr]
                );
            }
        };
    };
    (@assert_is_supported_trait TryFromBytes) => {};
    (@assert_is_supported_trait FromZeros) => {};
    (@assert_is_supported_trait FromBytes) => {};
    (@assert_is_supported_trait IntoBytes) => {};
    (
        @is_bit_valid
        $(<$tyvar:ident $(: $(? $optbound:ident $(+)?)* $($bound:ident $(+)?)* )?>)?
        TryFromBytes for $ty:ty [$repr:ty]
    ) => {
        #[inline(always)]
        fn is_bit_valid<Alignment>(candidate: $crate::Maybe<'_, Self, Alignment>) -> bool
        where
            Alignment: $crate::invariant::Alignment,
        {
            // SAFETY: This macro ensures that `$repr` and `Self` have the same
            // size and bit validity. Thus, a bit-valid instance of `$repr` is
            // also a bit-valid instance of `Self`.
            <$repr as TryFromBytes>::is_bit_valid(candidate.transmute::<_, _, BecauseImmutable>())
        }
    };
    (
        @is_bit_valid
        $(<$tyvar:ident $(: $(? $optbound:ident $(+)?)* $($bound:ident $(+)?)* )?>)?
        $trait:ident for $ty:ty [$repr:ty]
    ) => {
        // Trait other than `TryFromBytes`; no `is_bit_valid` impl.
    };
}

/// Implements a trait for a type, bounding on each member of the power set of
/// a set of type variables. This is useful for implementing traits for tuples
/// or `fn` types.
///
/// The last argument is the name of a macro which will be called in every
/// `impl` block, and is expected to expand to the name of the type for which to
/// implement the trait.
///
/// For example, the invocation:
/// ```ignore
/// unsafe_impl_for_power_set!(A, B => Foo for type!(...))
/// ```
/// ...expands to:
/// ```ignore
/// unsafe impl       Foo for type!()     { ... }
/// unsafe impl<B>    Foo for type!(B)    { ... }
/// unsafe impl<A, B> Foo for type!(A, B) { ... }
/// ```
macro_rules! unsafe_impl_for_power_set {
    (
        $first:ident $(, $rest:ident)* $(-> $ret:ident)? => $trait:ident for $macro:ident!(...)
        $(; |$candidate:ident| $is_bit_valid:expr)?
    ) => {
        unsafe_impl_for_power_set!(
            $($rest),* $(-> $ret)? => $trait for $macro!(...)
            $(; |$candidate| $is_bit_valid)?
        );
        unsafe_impl_for_power_set!(
            @impl $first $(, $rest)* $(-> $ret)? => $trait for $macro!(...)
            $(; |$candidate| $is_bit_valid)?
        );
    };
    (
        $(-> $ret:ident)? => $trait:ident for $macro:ident!(...)
        $(; |$candidate:ident| $is_bit_valid:expr)?
    ) => {
        unsafe_impl_for_power_set!(
            @impl $(-> $ret)? => $trait for $macro!(...)
            $(; |$candidate| $is_bit_valid)?
        );
    };
    (
        @impl $($vars:ident),* $(-> $ret:ident)? => $trait:ident for $macro:ident!(...)
        $(; |$candidate:ident| $is_bit_valid:expr)?
    ) => {
        unsafe_impl!(
            $($vars,)* $($ret)? => $trait for $macro!($($vars),* $(-> $ret)?)
            $(; |$candidate| $is_bit_valid)?
        );
    };
}

/// Expands to an `Option<extern "C" fn>` type with the given argument types and
/// return type. Designed for use with `unsafe_impl_for_power_set`.
macro_rules! opt_extern_c_fn {
    ($($args:ident),* -> $ret:ident) => { Option<extern "C" fn($($args),*) -> $ret> };
}

/// Expands to an `Option<unsafe extern "C" fn>` type with the given argument
/// types and return type. Designed for use with `unsafe_impl_for_power_set`.
macro_rules! opt_unsafe_extern_c_fn {
    ($($args:ident),* -> $ret:ident) => { Option<unsafe extern "C" fn($($args),*) -> $ret> };
}

/// Expands to an `Option<fn>` type with the given argument types and return
/// type. Designed for use with `unsafe_impl_for_power_set`.
macro_rules! opt_fn {
    ($($args:ident),* -> $ret:ident) => { Option<fn($($args),*) -> $ret> };
}

/// Expands to an `Option<unsafe fn>` type with the given argument types and
/// return type. Designed for use with `unsafe_impl_for_power_set`.
macro_rules! opt_unsafe_fn {
    ($($args:ident),* -> $ret:ident) => { Option<unsafe fn($($args),*) -> $ret> };
}

// This `allow` is needed because, when testing, we export this macro so it can
// be used in `doctests`.
#[allow(rustdoc::private_intra_doc_links)]
/// Implements trait(s) for a type or verifies the given implementation by
/// referencing an existing (derived) implementation.
///
/// This macro exists so that we can provide zerocopy-derive as an optional
/// dependency and still get the benefit of using its derives to validate that
/// our trait impls are sound.
///
/// When compiling without `--cfg 'feature = "derive"` and without `--cfg test`,
/// `impl_or_verify!` emits the provided trait impl. When compiling with either
/// of those cfgs, it is expected that the type in question is deriving the
/// traits instead. In this case, `impl_or_verify!` emits code which validates
/// that the given trait impl is at least as restrictive as the the impl emitted
/// by the custom derive. This has the effect of confirming that the impl which
/// is emitted when the `derive` feature is disabled is actually sound (on the
/// assumption that the impl emitted by the custom derive is sound).
///
/// The caller is still required to provide a safety comment (e.g. using the
/// `const _: () = unsafe` macro). The reason for this restriction is that,
/// while `impl_or_verify!` can guarantee that the provided impl is sound when
/// it is compiled with the appropriate cfgs, there is no way to guarantee that
/// it is ever compiled with those cfgs. In particular, it would be possible to
/// accidentally place an `impl_or_verify!` call in a context that is only ever
/// compiled when the `derive` feature is disabled. If that were to happen,
/// there would be nothing to prevent an unsound trait impl from being emitted.
/// Requiring a safety comment reduces the likelihood of emitting an unsound
/// impl in this case, and also provides useful documentation for readers of the
/// code.
///
/// Finally, if a `TryFromBytes::is_bit_valid` impl is provided, it must adhere
/// to the safety preconditions of [`unsafe_impl!`].
///
/// ## Example
///
/// ```rust,ignore
/// // Note that these derives are gated by `feature = "derive"`
/// #[cfg_attr(any(feature = "derive", test), derive(FromZeros, FromBytes, IntoBytes, Unaligned))]
/// #[repr(transparent)]
/// struct Wrapper<T>(T);
///
/// const _: () = unsafe {
///     /// SAFETY:
///     /// `Wrapper<T>` is `repr(transparent)`, so it is sound to implement any
///     /// zerocopy trait if `T` implements that trait.
///     impl_or_verify!(T: FromZeros => FromZeros for Wrapper<T>);
///     impl_or_verify!(T: FromBytes => FromBytes for Wrapper<T>);
///     impl_or_verify!(T: IntoBytes => IntoBytes for Wrapper<T>);
///     impl_or_verify!(T: Unaligned => Unaligned for Wrapper<T>);
/// }
/// ```
#[cfg_attr(__ZEROCOPY_INTERNAL_USE_ONLY_DEV_MODE, macro_export)] // Used in `doctests.rs`
#[doc(hidden)]
macro_rules! impl_or_verify {
    // The following two match arms follow the same pattern as their
    // counterparts in `unsafe_impl!`; see the documentation on those arms for
    // more details.
    (
        const $constname:ident : $constty:ident $(,)?
        $($tyvar:ident $(: $(? $optbound:ident $(+)?)* $($bound:ident $(+)?)* )?),*
        => $trait:ident for $ty:ty
    ) => {
        impl_or_verify!(@impl { unsafe_impl!(
            const $constname: $constty, $($tyvar $(: $(? $optbound +)* $($bound +)*)?),* => $trait for $ty
        ); });
        impl_or_verify!(@verify $trait, {
            impl<const $constname: $constty, $($tyvar $(: $(? $optbound +)* $($bound +)*)?),*> Subtrait for $ty {}
        });
    };
    (
        $($tyvar:ident $(: $(? $optbound:ident $(+)?)* $($bound:ident $(+)?)* )?),*
        => $trait:ident for $ty:ty $(; |$candidate:ident| $is_bit_valid:expr)?
    ) => {
        impl_or_verify!(@impl { unsafe_impl!(
            $($tyvar $(: $(? $optbound +)* $($bound +)*)?),* => $trait for $ty
            $(; |$candidate| $is_bit_valid)?
        ); });
        impl_or_verify!(@verify $trait, {
            impl<$($tyvar $(: $(? $optbound +)* $($bound +)*)?),*> Subtrait for $ty {}
        });
    };
    (@impl $impl_block:tt) => {
        #[cfg(not(any(feature = "derive", test)))]
        { $impl_block };
    };
    (@verify $trait:ident, $impl_block:tt) => {
        #[cfg(any(feature = "derive", test))]
        {
            // On some toolchains, `Subtrait` triggers the `dead_code` lint
            // because it is implemented but never used.
            #[allow(dead_code)]
            trait Subtrait: $trait {}
            $impl_block
        };
    };
}

/// Implements `KnownLayout` for a sized type.
macro_rules! impl_known_layout {
    ($(const $constvar:ident : $constty:ty, $tyvar:ident $(: ?$optbound:ident)? => $ty:ty),* $(,)?) => {
        $(impl_known_layout!(@inner const $constvar: $constty, $tyvar $(: ?$optbound)? => $ty);)*
    };
    ($($tyvar:ident $(: ?$optbound:ident)? => $ty:ty),* $(,)?) => {
        $(impl_known_layout!(@inner , $tyvar $(: ?$optbound)? => $ty);)*
    };
    ($($(#[$attrs:meta])* $ty:ty),*) => { $(impl_known_layout!(@inner , => $(#[$attrs])* $ty);)* };
    (@inner $(const $constvar:ident : $constty:ty)? , $($tyvar:ident $(: ?$optbound:ident)?)? => $(#[$attrs:meta])* $ty:ty) => {
        const _: () = {
            use core::ptr::NonNull;

            #[allow(non_local_definitions)]
            $(#[$attrs])*
            // SAFETY: Delegates safety to `DstLayout::for_type`.
            unsafe impl<$($tyvar $(: ?$optbound)?)? $(, const $constvar : $constty)?> KnownLayout for $ty {
                #[allow(clippy::missing_inline_in_public_items)]
                #[cfg_attr(all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS), coverage(off))]
                fn only_derive_is_allowed_to_implement_this_trait() where Self: Sized {}

                type PointerMetadata = ();

                // SAFETY: `CoreMaybeUninit<T>::LAYOUT` and `T::LAYOUT` are
                // identical because `CoreMaybeUninit<T>` has the same size and
                // alignment as `T` [1], and `CoreMaybeUninit` admits
                // uninitialized bytes in all positions.
                //
                // [1] Per https://doc.rust-lang.org/1.81.0/std/mem/union.MaybeUninit.html#layout-1:
                //
                //   `MaybeUninit<T>` is guaranteed to have the same size,
                //   alignment, and ABI as `T`
                type MaybeUninit = core::mem::MaybeUninit<Self>;

                const LAYOUT: crate::DstLayout = crate::DstLayout::for_type::<$ty>();

                // SAFETY: `.cast` preserves address and provenance.
                //
                // FIXME(#429): Add documentation to `.cast` that promises that
                // it preserves provenance.
                #[inline(always)]
                fn raw_from_ptr_len(bytes: NonNull<u8>, _meta: ()) -> NonNull<Self> {
                    bytes.cast::<Self>()
                }

                #[inline(always)]
                fn pointer_to_metadata(_ptr: *mut Self) -> () {
                }
            }
        };
    };
}

/// Implements `KnownLayout` for a type in terms of the implementation of
/// another type with the same representation.
///
/// # Safety
///
/// - `$ty` and `$repr` must have the same:
///   - Fixed prefix size
///   - Alignment
///   - (For DSTs) trailing slice element size
/// - It must be valid to perform an `as` cast from `*mut $repr` to `*mut $ty`,
///   and this operation must preserve referent size (ie, `size_of_val_raw`).
macro_rules! unsafe_impl_known_layout {
    ($($tyvar:ident: ?Sized + KnownLayout =>)? #[repr($repr:ty)] $ty:ty) => {{
        use core::ptr::NonNull;

        crate::util::macros::__unsafe();

        #[allow(non_local_definitions)]
        // SAFETY: The caller promises that this is sound.
        unsafe impl<$($tyvar: ?Sized + KnownLayout)?> KnownLayout for $ty {
            #[allow(clippy::missing_inline_in_public_items, dead_code)]
            #[cfg_attr(all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS), coverage(off))]
            fn only_derive_is_allowed_to_implement_this_trait() {}

            type PointerMetadata = <$repr as KnownLayout>::PointerMetadata;
            type MaybeUninit = <$repr as KnownLayout>::MaybeUninit;

            const LAYOUT: DstLayout = <$repr as KnownLayout>::LAYOUT;

            // SAFETY: All operations preserve address and provenance. Caller
            // has promised that the `as` cast preserves size.
            //
            // FIXME(#429): Add documentation to `NonNull::new_unchecked` that
            // it preserves provenance.
            #[inline(always)]
            fn raw_from_ptr_len(bytes: NonNull<u8>, meta: <$repr as KnownLayout>::PointerMetadata) -> NonNull<Self> {
                #[allow(clippy::as_conversions)]
                let ptr = <$repr>::raw_from_ptr_len(bytes, meta).as_ptr() as *mut Self;
                // SAFETY: `ptr` was converted from `bytes`, which is non-null.
                unsafe { NonNull::new_unchecked(ptr) }
            }

            #[inline(always)]
            fn pointer_to_metadata(ptr: *mut Self) -> Self::PointerMetadata {
                #[allow(clippy::as_conversions)]
                let ptr = ptr as *mut $repr;
                <$repr>::pointer_to_metadata(ptr)
            }
        }
    }};
}

/// Uses `align_of` to confirm that a type or set of types have alignment 1.
///
/// Note that `align_of<T>` requires `T: Sized`, so this macro doesn't work for
/// unsized types.
macro_rules! assert_unaligned {
    ($($tys:ty),*) => {
        $(
            // We only compile this assertion under `cfg(test)` to avoid taking
            // an extra non-dev dependency (and making this crate more expensive
            // to compile for our dependents).
            #[cfg(test)]
            static_assertions::const_assert_eq!(core::mem::align_of::<$tys>(), 1);
        )*
    };
}

/// Emits a function definition as either `const fn` or `fn` depending on
/// whether the current toolchain version supports `const fn` with generic trait
/// bounds.
macro_rules! maybe_const_trait_bounded_fn {
    // This case handles both `self` methods (where `self` is by value) and
    // non-method functions. Each `$args` may optionally be followed by `:
    // $arg_tys:ty`, which can be omitted for `self`.
    ($(#[$attr:meta])* $vis:vis const fn $name:ident($($args:ident $(: $arg_tys:ty)?),* $(,)?) $(-> $ret_ty:ty)? $body:block) => {
        #[cfg(not(no_zerocopy_generic_bounds_in_const_fn_1_61_0))]
        $(#[$attr])* $vis const fn $name($($args $(: $arg_tys)?),*) $(-> $ret_ty)? $body

        #[cfg(no_zerocopy_generic_bounds_in_const_fn_1_61_0)]
        $(#[$attr])* $vis fn $name($($args $(: $arg_tys)?),*) $(-> $ret_ty)? $body
    };
}

/// Either panic (if the current Rust toolchain supports panicking in `const
/// fn`) or evaluate a constant that will cause an array indexing error whose
/// error message will include the format string.
///
/// The type that this expression evaluates to must be `Copy`, or else the
/// non-panicking desugaring will fail to compile.
macro_rules! const_panic {
    (@non_panic $($_arg:tt)+) => {{
        // This will type check to whatever type is expected based on the call
        // site.
        let panic: [_; 0] = [];
        // This will always fail (since we're indexing into an array of size 0.
        #[allow(unconditional_panic)]
        panic[0]
    }};
    ($($arg:tt)+) => {{
        #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
        panic!($($arg)+);
        #[cfg(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0)]
        const_panic!(@non_panic $($arg)+)
    }};
}

/// Either assert (if the current Rust toolchain supports panicking in `const
/// fn`) or evaluate the expression and, if it evaluates to `false`, call
/// `const_panic!`. This is used in place of `assert!` in const contexts to
/// accommodate old toolchains.
macro_rules! const_assert {
    ($e:expr) => {{
        #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
        assert!($e);
        #[cfg(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0)]
        {
            let e = $e;
            if !e {
                let _: () = const_panic!(@non_panic concat!("assertion failed: ", stringify!($e)));
            }
        }
    }};
    ($e:expr, $($args:tt)+) => {{
        #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
        assert!($e, $($args)+);
        #[cfg(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0)]
        {
            let e = $e;
            if !e {
                let _: () = const_panic!(@non_panic concat!("assertion failed: ", stringify!($e), ": ", stringify!($arg)), $($args)*);
            }
        }
    }};
}

/// Like `const_assert!`, but relative to `debug_assert!`.
macro_rules! const_debug_assert {
    ($e:expr $(, $msg:expr)?) => {{
        #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
        debug_assert!($e $(, $msg)?);
        #[cfg(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0)]
        {
            // Use this (rather than `#[cfg(debug_assertions)]`) to ensure that
            // `$e` is always compiled even if it will never be evaluated at
            // runtime.
            if cfg!(debug_assertions) {
                let e = $e;
                if !e {
                    let _: () = const_panic!(@non_panic concat!("assertion failed: ", stringify!($e) $(, ": ", $msg)?));
                }
            }
        }
    }}
}

/// Either invoke `unreachable!()` or `loop {}` depending on whether the Rust
/// toolchain supports panicking in `const fn`.
macro_rules! const_unreachable {
    () => {{
        #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
        unreachable!();

        #[cfg(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0)]
        loop {}
    }};
}

/// Asserts at compile time that `$condition` is true for `Self` or the given
/// `$tyvar`s. Unlike `const_assert`, this is *strictly* a compile-time check;
/// it cannot be evaluated in a runtime context. The condition is checked after
/// monomorphization and, upon failure, emits a compile error.
macro_rules! static_assert {
    (Self $(: $(? $optbound:ident $(+)?)* $($bound:ident $(+)?)* )? => $condition:expr $(, $args:tt)*) => {{
        trait StaticAssert {
            const ASSERT: bool;
        }

        impl<T $(: $(? $optbound +)* $($bound +)*)?> StaticAssert for T {
            const ASSERT: bool = {
                const_assert!($condition $(, $args)*);
                $condition
            };
        }

        const_assert!(<Self as StaticAssert>::ASSERT);
    }};
    ($($tyvar:ident $(: $(? $optbound:ident $(+)?)* $($bound:ident $(+)?)* )?),* => $condition:expr $(, $args:tt)*) => {{
        trait StaticAssert {
            const ASSERT: bool;
        }

        // NOTE: We use `PhantomData` so we can support unsized types.
        impl<$($tyvar $(: $(? $optbound +)* $($bound +)*)?,)*> StaticAssert for ($(core::marker::PhantomData<$tyvar>,)*) {
            const ASSERT: bool = {
                const_assert!($condition $(, $args)*);
                $condition
            };
        }

        const_assert!(<($(core::marker::PhantomData<$tyvar>,)*) as StaticAssert>::ASSERT);
    }};
}

/// Assert at compile time that `tyvar` does not have a zero-sized DST
/// component.
macro_rules! static_assert_dst_is_not_zst {
    ($tyvar:ident) => {{
        use crate::KnownLayout;
        static_assert!($tyvar: ?Sized + KnownLayout => {
            let dst_is_zst = match $tyvar::LAYOUT.size_info {
                crate::SizeInfo::Sized { .. } => false,
                crate::SizeInfo::SliceDst(TrailingSliceLayout { elem_size, .. }) => {
                    elem_size == 0
                }
            };
            !dst_is_zst
        }, "cannot call this method on a dynamically-sized type whose trailing slice element is zero-sized");
    }}
}

/// Defines a named [`Cast`] implementation.
///
/// # Safety
///
/// The caller must ensure that, given `src: *mut $src`, `src as *mut $dst` is a
/// size-preserving or size-shrinking cast.
///
/// [`Cast`]: crate::pointer::cast::Cast
#[macro_export]
#[doc(hidden)]
macro_rules! define_cast {
    // We require the caller to provide an `unsafe` block as part of the input
    // syntax since a call to `define_cast!` is useless inside of an `unsafe`
    // block (since it would introduce a type which can't be named outside of
    // the context of that block).
    (unsafe { $vis:vis $name:ident $(<$tyvar:ident $(: ?$optbound:ident)?>)? = $src:ty => $dst:ty }) => {
        #[allow(missing_debug_implementations, missing_copy_implementations, unreachable_pub)]
        $vis enum $name {}

        // SAFETY: The caller promises that `src as *mut $src` is a size-
        // preserving or size-shrinking cast. All operations preserve
        // provenance.
        unsafe impl $(<$tyvar $(: ?$optbound)?>)? $crate::pointer::cast::Project<$src, $dst> for $name {
            fn project(src: $crate::pointer::PtrInner<'_, $src>) -> *mut $dst {
                #[allow(clippy::as_conversions)]
                return src.as_ptr() as *mut $dst;
            }
        }

        // SAFETY: The impl of `Project::project` preserves referent address.
        unsafe impl $(<$tyvar $(: ?$optbound)?>)? $crate::pointer::cast::Cast<$src, $dst> for $name {}
    };
}

/// Implements `TransmuteFrom` and `SizeEq` for `T` and `$wrapper<T>`.
///
/// # Safety
///
/// `T` and `$wrapper<T>` must have the same bit validity, and must have the
/// same size in the sense of `CastExact` (specifically, both a
/// `T`-to-`$wrapper<T>` cast and a `$wrapper<T>`-to-`T` cast must be
/// size-preserving).
macro_rules! unsafe_impl_for_transparent_wrapper {
    ($vis:vis T $(: ?$optbound:ident)? => $wrapper:ident<T>) => {{
        crate::util::macros::__unsafe();

        use crate::pointer::{TransmuteFrom, cast::{CastExact, TransitiveProject}, SizeEq, invariant::Valid};
        use crate::wrappers::ReadOnly;

        // SAFETY: The caller promises that `T` and `$wrapper<T>` have the same
        // bit validity.
        unsafe impl<T $(: ?$optbound)?> TransmuteFrom<T, Valid, Valid> for $wrapper<T> {}
        // SAFETY: See previous safety comment.
        unsafe impl<T $(: ?$optbound)?> TransmuteFrom<$wrapper<T>, Valid, Valid> for T {}
        // SAFETY: The caller promises that a `T` to `$wrapper<T>` cast is
        // size-preserving.
        define_cast!(unsafe { $vis CastToWrapper<T $(: ?$optbound)? > = T => $wrapper<T> });
        // SAFETY: The caller promises that a `T` to `$wrapper<T>` cast is
        // size-preserving.
        unsafe impl<T $(: ?$optbound)?> CastExact<T, $wrapper<T>> for CastToWrapper {}
        // SAFETY: The caller promises that a `$wrapper<T>` to `T` cast is
        // size-preserving.
        define_cast!(unsafe { $vis CastFromWrapper<T $(: ?$optbound)? > = $wrapper<T> => T });
        // SAFETY: The caller promises that a `$wrapper<T>` to `T` cast is
        // size-preserving.
        unsafe impl<T $(: ?$optbound)?> CastExact<$wrapper<T>, T> for CastFromWrapper {}

        impl<T $(: ?$optbound)?> SizeEq<T> for $wrapper<T> {
            type CastFrom = CastToWrapper;
        }
        impl<T $(: ?$optbound)?> SizeEq<$wrapper<T>> for T {
            type CastFrom = CastFromWrapper;
        }

        impl<T $(: ?$optbound)?> SizeEq<ReadOnly<T>> for $wrapper<T> {
            type CastFrom = TransitiveProject<
                T,
                <T as SizeEq<ReadOnly<T>>>::CastFrom,
                CastToWrapper,
            >;
        }
        impl<T $(: ?$optbound)?> SizeEq<$wrapper<T>> for ReadOnly<T> {
            type CastFrom = TransitiveProject<
                T,
                CastFromWrapper,
                <ReadOnly<T> as SizeEq<T>>::CastFrom,
            >;
        }

        impl<T $(: ?$optbound)?> SizeEq<ReadOnly<T>> for ReadOnly<$wrapper<T>> {
            type CastFrom = TransitiveProject<
                $wrapper<T>,
                <$wrapper<T> as SizeEq<ReadOnly<T>>>::CastFrom,
                <ReadOnly<$wrapper<T>> as SizeEq<$wrapper<T>>>::CastFrom,
            >;
        }
        impl<T $(: ?$optbound)?> SizeEq<ReadOnly<$wrapper<T>>> for ReadOnly<T> {
            type CastFrom = TransitiveProject<
                $wrapper<T>,
                <$wrapper<T> as SizeEq<ReadOnly<$wrapper<T>>>>::CastFrom,
                <ReadOnly<T> as SizeEq<$wrapper<T>>>::CastFrom,
            >;
        }
    }};
}

macro_rules! impl_transitive_transmute_from {
    ($($tyvar:ident $(: ?$optbound:ident)?)? => $t:ty => $u:ty => $v:ty) => {
        const _: () = {
            use crate::pointer::{TransmuteFrom, SizeEq, invariant::Valid};

            impl<$($tyvar $(: ?$optbound)?)?> SizeEq<$t> for $v
            where
                $u: SizeEq<$t>,
                $v: SizeEq<$u>,
            {
                type CastFrom = cast::TransitiveProject<
                    $u,
                    <$u as SizeEq<$t>>::CastFrom,
                    <$v as SizeEq<$u>>::CastFrom
                >;
            }

            // SAFETY: Since `$u: TransmuteFrom<$t, Valid, Valid>`, it is sound
            // to transmute a bit-valid `$t` to a bit-valid `$u`. Since `$v:
            // TransmuteFrom<$u, Valid, Valid>`, it is sound to transmute that
            // bit-valid `$u` to a bit-valid `$v`.
            unsafe impl<$($tyvar $(: ?$optbound)?)?> TransmuteFrom<$t, Valid, Valid> for $v
            where
                $u: TransmuteFrom<$t, Valid, Valid>,
                $v: TransmuteFrom<$u, Valid, Valid>,
            {}
        };
    };
}

/// A no-op `unsafe fn` for use in macro expansions.
///
/// Calling this function in a macro expansion ensures that the macro's caller
/// must wrap the call in `unsafe { ... }`.
#[inline(always)]
pub(crate) const unsafe fn __unsafe() {}

/// Extracts the contents of doc comments.
#[allow(unused)]
macro_rules! docstring {
    ($(#[doc = $content:expr])*) => {
        concat!($($content, "\n",)*)
    }
}

/// Generate a rustdoc-style header with `$name` as the HTML ID for the 'Code
/// Generation' section of documentation.
#[allow(unused)]
macro_rules! codegen_header {
    ($level:expr, $name:expr) => {
        concat!(
            "
<",
            $level,
            " id='method.",
            $name,
            ".codegen'>
    <a class='doc-anchor' href='#method.",
            $name,
            ".codegen'>§</a>
    Code Generation
</",
            $level,
            ">
"
        )
    };
}

/// Generates HTML tabs.
#[rustfmt::skip]
#[allow(unused)]
macro_rules! tabs {
    (
        name = $name:expr,
        arity = $arity:literal,
        $([
            $($open:ident)?
            @index $n:literal
            @title $title:literal
            $(#[doc = $content:expr])*
        ]),*
    ) => {
        concat!("
<div class='codegen-tabs' style='--arity: ", $arity ,"'>", $(concat!("
    <details name='tab-", $name,"' style='--n: ", $n ,"'", $(stringify!($open),)*">
        <summary><h6>", $title, "</h6></summary>
        <div>

", $($content, "\n",)* "
\
        </div>
    </details>"),)*
"</div>")
    }
}

/// Generates the HTML for a single benchmark example.
#[allow(unused)]
macro_rules! codegen_example {
    (format = $format:expr, bench = $bench:expr) => {
        tabs!(
            name = $bench,
            arity = 4,
            [
                @index 1
                @title "Format"
                /// ```ignore
                #[doc = include_str!(concat!("../benches/formats/", $format, ".rs"))]
                /// ```
            ],
            [
                @index 2
                @title "Benchmark"
                /// ```ignore
                #[doc = include_str!(concat!("../benches/", $bench, ".rs"))]
                /// ```
            ],
            [
                open
                @index 3
                @title "Assembly"
                /// ```plain
                #[doc = include_str!(concat!("../benches/", $bench, ".x86-64"))]
                /// ```
            ],
            [
                @index 4
                @title "Machine Code Analysis"
                /// ```plain
                #[doc = include_str!(concat!("../benches/", $bench, ".x86-64.mca"))]
                /// ```
            ]
        )
    }
}

/// Generate the HTML for a suite of benchmark examples.
#[allow(unused)]
macro_rules! codegen_example_suite {
    (
        bench = $bench:expr,
        format = $format:expr,
        arity = $arity:literal,
        $([
            $($open:ident)?
            @index $index:literal
            @title $title:literal
            @variant $variant:literal
        ]),*
    ) => {
        tabs!(
            name = $bench,
            arity = $arity,
            $([
                $($open)*
                @index $index
                @title $title
                #[doc = codegen_example!(
                    format = concat!($format, "_", $variant),
                    bench = concat!($bench, "_", $variant)
                )]
            ]),*
        )
    }
}

/// Generates the string for code generation preamble.
#[allow(unused)]
macro_rules! codegen_preamble {
    () => {
        docstring!(
            ///
            /// This abstraction is safe and cheap, but does not necessarily
            /// have zero runtime cost. The codegen you experience in practice
            /// will depend on optimization level, the layout of the destination
            /// type, and what the compiler can prove about the source.
            ///
        )
    }
}

/// Stub for rendering codegen documentation; used to break build dependency
/// between benches and zerocopy when re-blessing codegen tests.
#[allow(unused)]
#[cfg(not(doc))]
macro_rules! codegen_section {
    (
        header = $level:expr,
        bench = $bench:expr,
        format = $format:expr,
        arity = $arity:literal,
        $([
            $($open:ident)?
            @index $index:literal
            @title $title:literal
            @variant $variant:literal
        ]),*
    ) => {
        ""
    };
    (
        header = $level:expr,
        bench = $bench:expr,
        format = $format:expr,
    ) => {
        ""
    };
}

/// Generates the HTML for code generation documentation.
#[allow(unused)]
#[cfg(doc)]
macro_rules! codegen_section {
    (
        header = $level:expr,
        bench = $bench:expr,
        format = $format:expr,
        arity = $arity:literal,
        $([
            $($open:ident)?
            @index $index:literal
            @title $title:literal
            @variant $variant:literal
        ]),*
    ) => {
        concat!(
            codegen_header!($level, $bench),
            codegen_preamble!(),
            docstring!(
                ///
                /// The below examples illustrate typical codegen for
                /// increasingly complex types:
                ///
            ),
            codegen_example_suite!(
                bench = $bench,
                format = $format,
                arity = $arity,
                $([
                    $($open)*
                    @index $index
                    @title $title
                    @variant $variant
                ]),*
            )
        )
    };
    (
        header = $level:expr,
        bench = $bench:expr,
        format = $format:expr,
    ) => {
        concat!(
            codegen_header!($level, $bench),
            codegen_preamble!(),
            codegen_example!(
                format = $format,
                bench = $bench
            )
        )
    }
}
