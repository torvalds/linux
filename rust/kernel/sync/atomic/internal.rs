// SPDX-License-Identifier: GPL-2.0

//! Atomic internal implementations.
//!
//! Provides 1:1 mapping to the C atomic operations.

use crate::bindings;
use crate::macros::paste;
use core::cell::UnsafeCell;

mod private {
    /// Sealed trait marker to disable customized impls on atomic implementation traits.
    pub trait Sealed {}
}

// The C side supports atomic primitives only for `i32` and `i64` (`atomic_t` and `atomic64_t`),
// while the Rust side also layers provides atomic support for `i8` and `i16`
// on top of lower-level C primitives.
impl private::Sealed for i8 {}
impl private::Sealed for i16 {}
impl private::Sealed for i32 {}
impl private::Sealed for i64 {}

/// A marker trait for types that implement atomic operations with C side primitives.
///
/// This trait is sealed, and only types that map directly to the C side atomics
/// or can be implemented with lower-level C primitives are allowed to implement this:
///
/// - `i8` and `i16` are implemented with lower-level C primitives.
/// - `i32` map to `atomic_t`
/// - `i64` map to `atomic64_t`
pub trait AtomicImpl: Sized + Send + Copy + private::Sealed {
    /// The type of the delta in arithmetic or logical operations.
    ///
    /// For example, in `atomic_add(ptr, v)`, it's the type of `v`. Usually it's the same type of
    /// [`Self`], but it may be different for the atomic pointer type.
    type Delta;
}

// The current helpers of load/store uses `{WRITE,READ}_ONCE()` hence the atomicity is only
// guaranteed against read-modify-write operations if the architecture supports native atomic RmW.
#[cfg(CONFIG_ARCH_SUPPORTS_ATOMIC_RMW)]
impl AtomicImpl for i8 {
    type Delta = Self;
}

// The current helpers of load/store uses `{WRITE,READ}_ONCE()` hence the atomicity is only
// guaranteed against read-modify-write operations if the architecture supports native atomic RmW.
#[cfg(CONFIG_ARCH_SUPPORTS_ATOMIC_RMW)]
impl AtomicImpl for i16 {
    type Delta = Self;
}

// `atomic_t` implements atomic operations on `i32`.
impl AtomicImpl for i32 {
    type Delta = Self;
}

// `atomic64_t` implements atomic operations on `i64`.
impl AtomicImpl for i64 {
    type Delta = Self;
}

/// Atomic representation.
#[repr(transparent)]
pub struct AtomicRepr<T: AtomicImpl>(UnsafeCell<T>);

impl<T: AtomicImpl> AtomicRepr<T> {
    /// Creates a new atomic representation `T`.
    pub const fn new(v: T) -> Self {
        Self(UnsafeCell::new(v))
    }

    /// Returns a pointer to the underlying `T`.
    ///
    /// # Guarantees
    ///
    /// The returned pointer is valid and properly aligned (i.e. aligned to [`align_of::<T>()`]).
    pub const fn as_ptr(&self) -> *mut T {
        // GUARANTEE: `self.0` is an `UnsafeCell<T>`, therefore the pointer returned by `.get()`
        // must be valid and properly aligned.
        self.0.get()
    }
}

// This macro generates the function signature with given argument list and return type.
macro_rules! declare_atomic_method {
    (
        $(#[doc=$doc:expr])*
        $func:ident($($arg:ident : $arg_type:ty),*) $(-> $ret:ty)?
    ) => {
        paste!(
            $(#[doc = $doc])*
            fn [< atomic_ $func >]($($arg: $arg_type,)*) $(-> $ret)?;
        );
    };
    (
        $(#[doc=$doc:expr])*
        $func:ident [$variant:ident $($rest:ident)*]($($arg_sig:tt)*) $(-> $ret:ty)?
    ) => {
        paste!(
            declare_atomic_method!(
                $(#[doc = $doc])*
                [< $func _ $variant >]($($arg_sig)*) $(-> $ret)?
            );
        );

        declare_atomic_method!(
            $(#[doc = $doc])*
            $func [$($rest)*]($($arg_sig)*) $(-> $ret)?
        );
    };
    (
        $(#[doc=$doc:expr])*
        $func:ident []($($arg_sig:tt)*) $(-> $ret:ty)?
    ) => {
        declare_atomic_method!(
            $(#[doc = $doc])*
            $func($($arg_sig)*) $(-> $ret)?
        );
    }
}

// This macro generates the function implementation with given argument list and return type, and it
// will replace "call(...)" expression with "$ctype _ $func" to call the real C function.
macro_rules! impl_atomic_method {
    (
        ($ctype:ident) $func:ident($($arg:ident: $arg_type:ty),*) $(-> $ret:ty)? {
            $unsafe:tt { call($($c_arg:expr),*) }
        }
    ) => {
        paste!(
            #[inline(always)]
            fn [< atomic_ $func >]($($arg: $arg_type,)*) $(-> $ret)? {
                // TODO: Ideally we want to use the SAFETY comments written at the macro invocation
                // (e.g. in `declare_and_impl_atomic_methods!()`, however, since SAFETY comments
                // are just comments, and they are not passed to macros as tokens, therefore we
                // cannot use them here. One potential improvement is that if we support using
                // attributes as an alternative for SAFETY comments, then we can use that for macro
                // generating code.
                //
                // SAFETY: specified on macro invocation.
                $unsafe { bindings::[< $ctype _ $func >]($($c_arg,)*) }
            }
        );
    };
    (
        ($ctype:ident) $func:ident[$variant:ident $($rest:ident)*]($($arg_sig:tt)*) $(-> $ret:ty)? {
            $unsafe:tt { call($($arg:tt)*) }
        }
    ) => {
        paste!(
            impl_atomic_method!(
                ($ctype) [< $func _ $variant >]($($arg_sig)*) $( -> $ret)? {
                    $unsafe { call($($arg)*) }
            }
            );
        );
        impl_atomic_method!(
            ($ctype) $func [$($rest)*]($($arg_sig)*) $( -> $ret)? {
                $unsafe { call($($arg)*) }
            }
        );
    };
    (
        ($ctype:ident) $func:ident[]($($arg_sig:tt)*) $( -> $ret:ty)? {
            $unsafe:tt { call($($arg:tt)*) }
        }
    ) => {
        impl_atomic_method!(
            ($ctype) $func($($arg_sig)*) $(-> $ret)? {
                $unsafe { call($($arg)*) }
            }
        );
    }
}

macro_rules! declare_atomic_ops_trait {
    (
        $(#[$attr:meta])* $pub:vis trait $ops:ident {
            $(
                $(#[doc=$doc:expr])*
                fn $func:ident [$($variant:ident),*]($($arg_sig:tt)*) $( -> $ret:ty)? {
                    $unsafe:tt { bindings::#call($($arg:tt)*) }
                }
            )*
        }
    ) => {
        $(#[$attr])*
        $pub trait $ops: AtomicImpl {
            $(
                declare_atomic_method!(
                    $(#[doc=$doc])*
                    $func[$($variant)*]($($arg_sig)*) $(-> $ret)?
                );
            )*
        }
    }
}

macro_rules! impl_atomic_ops_for_one {
    (
        $ty:ty => $ctype:ident,
        $(#[$attr:meta])* $pub:vis trait $ops:ident {
            $(
                $(#[doc=$doc:expr])*
                fn $func:ident [$($variant:ident),*]($($arg_sig:tt)*) $( -> $ret:ty)? {
                    $unsafe:tt { bindings::#call($($arg:tt)*) }
                }
            )*
        }
    ) => {
        impl $ops for $ty {
            $(
                impl_atomic_method!(
                    ($ctype) $func[$($variant)*]($($arg_sig)*) $(-> $ret)? {
                        $unsafe { call($($arg)*) }
                    }
                );
            )*
        }
    }
}

// Declares $ops trait with methods and implements the trait.
macro_rules! declare_and_impl_atomic_methods {
    (
        [ $($map:tt)* ]
        $(#[$attr:meta])* $pub:vis trait $ops:ident { $($body:tt)* }
    ) => {
        declare_and_impl_atomic_methods!(
            @with_ops_def
            [ $($map)* ]
            ( $(#[$attr])* $pub trait $ops { $($body)* } )
        );
    };

    (@with_ops_def [ $($map:tt)* ] ( $($ops_def:tt)* )) => {
        declare_atomic_ops_trait!( $($ops_def)* );

        declare_and_impl_atomic_methods!(
            @munch
            [ $($map)* ]
            ( $($ops_def)* )
        );
    };

    (@munch [] ( $($ops_def:tt)* )) => {};

    (@munch [ $ty:ty => $ctype:ident $(, $($rest:tt)*)? ] ( $($ops_def:tt)* )) => {
        impl_atomic_ops_for_one!(
            $ty => $ctype,
            $($ops_def)*
        );

        declare_and_impl_atomic_methods!(
            @munch
            [ $($($rest)*)? ]
            ( $($ops_def)* )
        );
    };
}

declare_and_impl_atomic_methods!(
    [ i8 => atomic_i8, i16 => atomic_i16, i32 => atomic, i64 => atomic64 ]
    /// Basic atomic operations
    pub trait AtomicBasicOps {
        /// Atomic read (load).
        fn read[acquire](a: &AtomicRepr<Self>) -> Self {
            // SAFETY: `a.as_ptr()` is valid and properly aligned.
            unsafe { bindings::#call(a.as_ptr().cast()) }
        }

        /// Atomic set (store).
        fn set[release](a: &AtomicRepr<Self>, v: Self) {
            // SAFETY: `a.as_ptr()` is valid and properly aligned.
            unsafe { bindings::#call(a.as_ptr().cast(), v) }
        }
    }
);

declare_and_impl_atomic_methods!(
    [ i8 => atomic_i8, i16 => atomic_i16, i32 => atomic, i64 => atomic64 ]
    /// Exchange and compare-and-exchange atomic operations
    pub trait AtomicExchangeOps {
        /// Atomic exchange.
        ///
        /// Atomically updates `*a` to `v` and returns the old value.
        fn xchg[acquire, release, relaxed](a: &AtomicRepr<Self>, v: Self) -> Self {
            // SAFETY: `a.as_ptr()` is valid and properly aligned.
            unsafe { bindings::#call(a.as_ptr().cast(), v) }
        }

        /// Atomic compare and exchange.
        ///
        /// If `*a` == `*old`, atomically updates `*a` to `new`. Otherwise, `*a` is not
        /// modified, `*old` is updated to the current value of `*a`.
        ///
        /// Return `true` if the update of `*a` occurred, `false` otherwise.
        fn try_cmpxchg[acquire, release, relaxed](
            a: &AtomicRepr<Self>, old: &mut Self, new: Self
        ) -> bool {
            // SAFETY: `a.as_ptr()` is valid and properly aligned. `core::ptr::from_mut(old)`
            // is valid and properly aligned.
            unsafe { bindings::#call(a.as_ptr().cast(), core::ptr::from_mut(old), new) }
        }
    }
);

declare_and_impl_atomic_methods!(
    [ i32 => atomic, i64 => atomic64 ]
    /// Atomic arithmetic operations
    pub trait AtomicArithmeticOps {
        /// Atomic add (wrapping).
        ///
        /// Atomically updates `*a` to `(*a).wrapping_add(v)`.
        fn add[](a: &AtomicRepr<Self>, v: Self::Delta) {
            // SAFETY: `a.as_ptr()` is valid and properly aligned.
            unsafe { bindings::#call(v, a.as_ptr().cast()) }
        }

        /// Atomic fetch and add (wrapping).
        ///
        /// Atomically updates `*a` to `(*a).wrapping_add(v)`, and returns the value of `*a`
        /// before the update.
        fn fetch_add[acquire, release, relaxed](a: &AtomicRepr<Self>, v: Self::Delta) -> Self {
            // SAFETY: `a.as_ptr()` is valid and properly aligned.
            unsafe { bindings::#call(v, a.as_ptr().cast()) }
        }
    }
);
