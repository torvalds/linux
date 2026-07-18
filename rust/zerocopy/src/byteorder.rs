// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2019 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

//! Byte order-aware numeric primitives.
//!
//! This module contains equivalents of the native multi-byte integer types with
//! no alignment requirement and supporting byte order conversions.
//!
//! For each native multi-byte integer type - `u16`, `i16`, `u32`, etc - and
//! floating point type - `f32` and `f64` - an equivalent type is defined by
//! this module - [`U16`], [`I16`], [`U32`], [`F32`], [`F64`], etc. Unlike their
//! native counterparts, these types have alignment 1, and take a type parameter
//! specifying the byte order in which the bytes are stored in memory. Each type
//! implements this crate's relevant conversion and marker traits.
//!
//! These two properties, taken together, make these types useful for defining
//! data structures whose memory layout matches a wire format such as that of a
//! network protocol or a file format. Such formats often have multi-byte values
//! at offsets that do not respect the alignment requirements of the equivalent
//! native types, and stored in a byte order not necessarily the same as that of
//! the target platform.
//!
//! Type aliases are provided for common byte orders in the [`big_endian`],
//! [`little_endian`], [`network_endian`], and [`native_endian`] submodules.
//! Note that network-endian is a synonym for big-endian.
//!
//! # Example
//!
//! One use of these types is for representing network packet formats, such as
//! UDP:
//!
//! ```rust
//! use zerocopy::{*, byteorder::network_endian::U16};
//! # use zerocopy_derive::*;
//!
//! #[derive(FromBytes, IntoBytes, KnownLayout, Immutable, Unaligned)]
//! #[repr(C)]
//! struct UdpHeader {
//!     src_port: U16,
//!     dst_port: U16,
//!     length: U16,
//!     checksum: U16,
//! }
//!
//! #[derive(FromBytes, IntoBytes, KnownLayout, Immutable, Unaligned)]
//! #[repr(C, packed)]
//! struct UdpPacket {
//!     header: UdpHeader,
//!     body: [u8],
//! }
//!
//! impl UdpPacket {
//!     fn parse(bytes: &[u8]) -> Option<&UdpPacket> {
//!         UdpPacket::ref_from_bytes(bytes).ok()
//!     }
//! }
//! ```

use core::{
    convert::{TryFrom, TryInto},
    fmt::{Binary, Debug, LowerHex, Octal, UpperHex},
    hash::Hash,
    num::TryFromIntError,
};

use super::*;

/// A type-level representation of byte order.
///
/// This type is implemented by [`BigEndian`] and [`LittleEndian`], which
/// represent big-endian and little-endian byte order respectively. This module
/// also provides a number of useful aliases for those types: [`NativeEndian`],
/// [`NetworkEndian`], [`BE`], and [`LE`].
///
/// `ByteOrder` types can be used to specify the byte order of the types in this
/// module - for example, [`U32<BigEndian>`] is a 32-bit integer stored in
/// big-endian byte order.
///
/// [`U32<BigEndian>`]: U32
pub trait ByteOrder:
    Copy + Clone + Debug + Display + Eq + PartialEq + Ord + PartialOrd + Hash + private::Sealed
{
    #[doc(hidden)]
    const ORDER: Order;
}

mod private {
    pub trait Sealed {}

    impl Sealed for super::BigEndian {}
    impl Sealed for super::LittleEndian {}
}

#[allow(missing_copy_implementations, missing_debug_implementations)]
#[doc(hidden)]
pub enum Order {
    BigEndian,
    LittleEndian,
}

/// Big-endian byte order.
///
/// See [`ByteOrder`] for more details.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum BigEndian {}

impl ByteOrder for BigEndian {
    const ORDER: Order = Order::BigEndian;
}

impl Display for BigEndian {
    #[inline]
    fn fmt(&self, _: &mut Formatter<'_>) -> fmt::Result {
        match *self {}
    }
}

/// Little-endian byte order.
///
/// See [`ByteOrder`] for more details.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum LittleEndian {}

impl ByteOrder for LittleEndian {
    const ORDER: Order = Order::LittleEndian;
}

impl Display for LittleEndian {
    #[inline]
    fn fmt(&self, _: &mut Formatter<'_>) -> fmt::Result {
        match *self {}
    }
}

/// The endianness used by this platform.
///
/// This is a type alias for [`BigEndian`] or [`LittleEndian`] depending on the
/// endianness of the target platform.
#[cfg(target_endian = "big")]
pub type NativeEndian = BigEndian;

/// The endianness used by this platform.
///
/// This is a type alias for [`BigEndian`] or [`LittleEndian`] depending on the
/// endianness of the target platform.
#[cfg(target_endian = "little")]
pub type NativeEndian = LittleEndian;

/// The endianness used in many network protocols.
///
/// This is a type alias for [`BigEndian`].
pub type NetworkEndian = BigEndian;

/// A type alias for [`BigEndian`].
pub type BE = BigEndian;

/// A type alias for [`LittleEndian`].
pub type LE = LittleEndian;

macro_rules! impl_fmt_trait {
    ($name:ident, $native:ident, $trait:ident) => {
        impl<O: ByteOrder> $trait for $name<O> {
            #[inline(always)]
            fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
                $trait::fmt(&self.get(), f)
            }
        }
    };
}

macro_rules! impl_fmt_traits {
    ($name:ident, $native:ident, "floating point number") => {
    };
    ($name:ident, $native:ident, "unsigned integer") => {
        impl_fmt_traits!($name, $native, @all_types);
    };
    ($name:ident, $native:ident, "signed integer") => {
        impl_fmt_traits!($name, $native, @all_types);
    };
    ($name:ident, $native:ident, @all_types) => {
        impl_fmt_trait!($name, $native, Display);
        impl_fmt_trait!($name, $native, Octal);
        impl_fmt_trait!($name, $native, LowerHex);
        impl_fmt_trait!($name, $native, UpperHex);
        impl_fmt_trait!($name, $native, Binary);
    };
}

macro_rules! impl_ops_traits {
    ($name:ident, $native:ident, "floating point number") => {
        impl_ops_traits!($name, $native, @all_types);
        impl_ops_traits!($name, $native, @signed_integer_floating_point);

        impl<O: ByteOrder> PartialOrd for $name<O> {
            #[inline(always)]
            fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
                self.get().partial_cmp(&other.get())
            }
        }
    };
    ($name:ident, $native:ident, "unsigned integer") => {
        impl_ops_traits!($name, $native, @signed_unsigned_integer);
        impl_ops_traits!($name, $native, @all_types);
    };
    ($name:ident, $native:ident, "signed integer") => {
        impl_ops_traits!($name, $native, @signed_unsigned_integer);
        impl_ops_traits!($name, $native, @signed_integer_floating_point);
        impl_ops_traits!($name, $native, @all_types);
    };
    ($name:ident, $native:ident, @signed_unsigned_integer) => {
        impl_ops_traits!(@without_byteorder_swap $name, $native, BitAnd, bitand, BitAndAssign, bitand_assign);
        impl_ops_traits!(@without_byteorder_swap $name, $native, BitOr, bitor, BitOrAssign, bitor_assign);
        impl_ops_traits!(@without_byteorder_swap $name, $native, BitXor, bitxor, BitXorAssign, bitxor_assign);
        impl_ops_traits!(@with_byteorder_swap $name, $native, Shl, shl, ShlAssign, shl_assign);
        impl_ops_traits!(@with_byteorder_swap $name, $native, Shr, shr, ShrAssign, shr_assign);

        impl<O> core::ops::Not for $name<O> {
            type Output = $name<O>;

            #[inline(always)]
            fn not(self) -> $name<O> {
                 let self_native = $native::from_ne_bytes(self.0);
                 $name((!self_native).to_ne_bytes(), PhantomData)
            }
        }

        impl<O: ByteOrder> PartialOrd for $name<O> {
            #[inline(always)]
            fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
                Some(self.cmp(other))
            }
        }

        impl<O: ByteOrder> Ord for $name<O> {
            #[inline(always)]
            fn cmp(&self, other: &Self) -> Ordering {
                self.get().cmp(&other.get())
            }
        }

        impl<O: ByteOrder> PartialOrd<$native> for $name<O> {
            #[inline(always)]
            fn partial_cmp(&self, other: &$native) -> Option<Ordering> {
                self.get().partial_cmp(other)
            }
        }
    };
    ($name:ident, $native:ident, @signed_integer_floating_point) => {
        impl<O: ByteOrder> core::ops::Neg for $name<O> {
            type Output = $name<O>;

            #[inline(always)]
            fn neg(self) -> $name<O> {
                let self_native: $native = self.get();
                #[allow(clippy::arithmetic_side_effects)]
                $name::<O>::new(-self_native)
            }
        }
    };
    ($name:ident, $native:ident, @all_types) => {
        impl_ops_traits!(@with_byteorder_swap $name, $native, Add, add, AddAssign, add_assign);
        impl_ops_traits!(@with_byteorder_swap $name, $native, Div, div, DivAssign, div_assign);
        impl_ops_traits!(@with_byteorder_swap $name, $native, Mul, mul, MulAssign, mul_assign);
        impl_ops_traits!(@with_byteorder_swap $name, $native, Rem, rem, RemAssign, rem_assign);
        impl_ops_traits!(@with_byteorder_swap $name, $native, Sub, sub, SubAssign, sub_assign);
    };
    (@with_byteorder_swap $name:ident, $native:ident, $trait:ident, $method:ident, $trait_assign:ident, $method_assign:ident) => {
        impl<O: ByteOrder> core::ops::$trait<$name<O>> for $name<O> {
            type Output = $name<O>;

            #[inline(always)]
            fn $method(self, rhs: $name<O>) -> $name<O> {
                let self_native: $native = self.get();
                let rhs_native: $native = rhs.get();
                let result_native = core::ops::$trait::$method(self_native, rhs_native);
                $name::<O>::new(result_native)
            }
        }

        impl<O: ByteOrder> core::ops::$trait<$name<O>> for $native {
            type Output = $name<O>;

            #[inline(always)]
            fn $method(self, rhs: $name<O>) -> $name<O> {
                let rhs_native: $native = rhs.get();
                let result_native = core::ops::$trait::$method(self, rhs_native);
                $name::<O>::new(result_native)
            }
        }

        impl<O: ByteOrder> core::ops::$trait<$native> for $name<O> {
            type Output = $name<O>;

            #[inline(always)]
            fn $method(self, rhs: $native) -> $name<O> {
                let self_native: $native = self.get();
                let result_native = core::ops::$trait::$method(self_native, rhs);
                $name::<O>::new(result_native)
            }
        }

        impl<O: ByteOrder> core::ops::$trait_assign<$name<O>> for $name<O> {
            #[inline(always)]
            fn $method_assign(&mut self, rhs: $name<O>) {
                *self = core::ops::$trait::$method(*self, rhs);
            }
        }

        impl<O: ByteOrder> core::ops::$trait_assign<$name<O>> for $native {
            #[inline(always)]
            fn $method_assign(&mut self, rhs: $name<O>) {
                let rhs_native: $native = rhs.get();
                *self = core::ops::$trait::$method(*self, rhs_native);
            }
        }

        impl<O: ByteOrder> core::ops::$trait_assign<$native> for $name<O> {
            #[inline(always)]
            fn $method_assign(&mut self, rhs: $native) {
                *self = core::ops::$trait::$method(*self, rhs);
            }
        }
    };
    // Implement traits in terms of the same trait on the native type, but
    // without performing a byte order swap when both operands are byteorder
    // types. This only works for bitwise operations like `&`, `|`, etc.
    //
    // When only one operand is a byteorder type, we still need to perform a
    // byteorder swap.
    (@without_byteorder_swap $name:ident, $native:ident, $trait:ident, $method:ident, $trait_assign:ident, $method_assign:ident) => {
        impl<O: ByteOrder> core::ops::$trait<$name<O>> for $name<O> {
            type Output = $name<O>;

            #[inline(always)]
            fn $method(self, rhs: $name<O>) -> $name<O> {
                let self_native = $native::from_ne_bytes(self.0);
                let rhs_native = $native::from_ne_bytes(rhs.0);
                let result_native = core::ops::$trait::$method(self_native, rhs_native);
                $name(result_native.to_ne_bytes(), PhantomData)
            }
        }

        impl<O: ByteOrder> core::ops::$trait<$name<O>> for $native {
            type Output = $name<O>;

            #[inline(always)]
            fn $method(self, rhs: $name<O>) -> $name<O> {
                // No runtime cost - just byte packing
                let rhs_native = $native::from_ne_bytes(rhs.0);
                // (Maybe) runtime cost - byte order swap
                let slf_byteorder = $name::<O>::new(self);
                // No runtime cost - just byte packing
                let slf_native = $native::from_ne_bytes(slf_byteorder.0);
                // Runtime cost - perform the operation
                let result_native = core::ops::$trait::$method(slf_native, rhs_native);
                // No runtime cost - just byte unpacking
                $name(result_native.to_ne_bytes(), PhantomData)
            }
        }

        impl<O: ByteOrder> core::ops::$trait<$native> for $name<O> {
            type Output = $name<O>;

            #[inline(always)]
            fn $method(self, rhs: $native) -> $name<O> {
                // (Maybe) runtime cost - byte order swap
                let rhs_byteorder = $name::<O>::new(rhs);
                // No runtime cost - just byte packing
                let rhs_native = $native::from_ne_bytes(rhs_byteorder.0);
                // No runtime cost - just byte packing
                let slf_native = $native::from_ne_bytes(self.0);
                // Runtime cost - perform the operation
                let result_native = core::ops::$trait::$method(slf_native, rhs_native);
                // No runtime cost - just byte unpacking
                $name(result_native.to_ne_bytes(), PhantomData)
            }
        }

        impl<O: ByteOrder> core::ops::$trait_assign<$name<O>> for $name<O> {
            #[inline(always)]
            fn $method_assign(&mut self, rhs: $name<O>) {
                *self = core::ops::$trait::$method(*self, rhs);
            }
        }

        impl<O: ByteOrder> core::ops::$trait_assign<$name<O>> for $native {
            #[inline(always)]
            fn $method_assign(&mut self, rhs: $name<O>) {
                // (Maybe) runtime cost - byte order swap
                let rhs_native = rhs.get();
                // Runtime cost - perform the operation
                *self = core::ops::$trait::$method(*self, rhs_native);
            }
        }

        impl<O: ByteOrder> core::ops::$trait_assign<$native> for $name<O> {
            #[inline(always)]
            fn $method_assign(&mut self, rhs: $native) {
                *self = core::ops::$trait::$method(*self, rhs);
            }
        }
    };
}

macro_rules! doc_comment {
    ($x:expr, $($tt:tt)*) => {
        #[doc = $x]
        $($tt)*
    };
}

macro_rules! define_max_value_constant {
    ($name:ident, $bytes:expr, "unsigned integer") => {
        /// The maximum value.
        ///
        /// This constant should be preferred to constructing a new value using
        /// `new`, as `new` may perform an endianness swap depending on the
        /// endianness `O` and the endianness of the platform.
        pub const MAX_VALUE: $name<O> = $name([0xFFu8; $bytes], PhantomData);
    };
    // We don't provide maximum and minimum value constants for signed values
    // and floats because there's no way to do it generically - it would require
    // a different value depending on the value of the `ByteOrder` type
    // parameter. Currently, one workaround would be to provide implementations
    // for concrete implementations of that trait. In the long term, if we are
    // ever able to make the `new` constructor a const fn, we could use that
    // instead.
    ($name:ident, $bytes:expr, "signed integer") => {};
    ($name:ident, $bytes:expr, "floating point number") => {};
}

macro_rules! define_type {
    (
        $article:ident,
        $description:expr,
        $name:ident,
        $native:ident,
        $bits:expr,
        $bytes:expr,
        $from_be_fn:path,
        $to_be_fn:path,
        $from_le_fn:path,
        $to_le_fn:path,
        $number_kind:tt,
        [$($larger_native:ty),*],
        [$($larger_native_try:ty),*],
        [$($larger_byteorder:ident),*],
        [$($larger_byteorder_try:ident),*]
    ) => {
        doc_comment! {
            concat!($description, " stored in a given byte order.

`", stringify!($name), "` is like the native `", stringify!($native), "` type with
two major differences: First, it has no alignment requirement (its alignment is 1).
Second, the endianness of its memory layout is given by the type parameter `O`,
which can be any type which implements [`ByteOrder`]. In particular, this refers
to [`BigEndian`], [`LittleEndian`], [`NativeEndian`], and [`NetworkEndian`].

", stringify!($article), " `", stringify!($name), "` can be constructed using
the [`new`] method, and its contained value can be obtained as a native
`",stringify!($native), "` using the [`get`] method, or updated in place with
the [`set`] method. In all cases, if the endianness `O` is not the same as the
endianness of the current platform, an endianness swap will be performed in
order to uphold the invariants that a) the layout of `", stringify!($name), "`
has endianness `O` and that, b) the layout of `", stringify!($native), "` has
the platform's native endianness.

`", stringify!($name), "` implements [`FromBytes`], [`IntoBytes`], and [`Unaligned`],
making it useful for parsing and serialization. See the module documentation for an
example of how it can be used for parsing UDP packets.

[`new`]: crate::byteorder::", stringify!($name), "::new
[`get`]: crate::byteorder::", stringify!($name), "::get
[`set`]: crate::byteorder::", stringify!($name), "::set
[`FromBytes`]: crate::FromBytes
[`IntoBytes`]: crate::IntoBytes
[`Unaligned`]: crate::Unaligned"),
            #[derive(Copy, Clone, Eq, PartialEq, Hash)]
            #[cfg_attr(any(feature = "derive", test), derive(KnownLayout, Immutable, FromBytes, IntoBytes, Unaligned))]
            #[repr(transparent)]
            pub struct $name<O>([u8; $bytes], PhantomData<O>);
        }

        #[cfg(not(any(feature = "derive", test)))]
        impl_known_layout!(O => $name<O>);

        #[allow(unused_unsafe)] // Unused when `feature = "derive"`.
        // SAFETY: `$name<O>` is `repr(transparent)`, and so it has the same
        // layout as its only non-zero field, which is a `u8` array. `u8` arrays
        // are `Immutable`, `TryFromBytes`, `FromZeros`, `FromBytes`,
        // `IntoBytes`, and `Unaligned`.
        #[allow(clippy::multiple_unsafe_ops_per_block)]
        const _: () = unsafe {
            impl_or_verify!(O => Immutable for $name<O>);
            impl_or_verify!(O => TryFromBytes for $name<O>);
            impl_or_verify!(O => FromZeros for $name<O>);
            impl_or_verify!(O => FromBytes for $name<O>);
            impl_or_verify!(O => IntoBytes for $name<O>);
            impl_or_verify!(O => Unaligned for $name<O>);
        };

        impl<O> Default for $name<O> {
            #[inline(always)]
            fn default() -> $name<O> {
                $name::ZERO
            }
        }

        impl<O> $name<O> {
            /// The value zero.
            ///
            /// This constant should be preferred to constructing a new value
            /// using `new`, as `new` may perform an endianness swap depending
            /// on the endianness and platform.
            pub const ZERO: $name<O> = $name([0u8; $bytes], PhantomData);

            define_max_value_constant!($name, $bytes, $number_kind);

            /// Constructs a new value from bytes which are already in `O` byte
            /// order.
            #[must_use = "has no side effects"]
            #[inline(always)]
            pub const fn from_bytes(bytes: [u8; $bytes]) -> $name<O> {
                $name(bytes, PhantomData)
            }

            /// Extracts the bytes of `self` without swapping the byte order.
            ///
            /// The returned bytes will be in `O` byte order.
            #[must_use = "has no side effects"]
            #[inline(always)]
            pub const fn to_bytes(self) -> [u8; $bytes] {
                self.0
            }
        }

        impl<O: ByteOrder> $name<O> {
            maybe_const_trait_bounded_fn! {
                /// Constructs a new value, possibly performing an endianness
                /// swap to guarantee that the returned value has endianness
                /// `O`.
                #[must_use = "has no side effects"]
                #[inline(always)]
                pub const fn new(n: $native) -> $name<O> {
                    let bytes = match O::ORDER {
                        Order::BigEndian => $to_be_fn(n),
                        Order::LittleEndian => $to_le_fn(n),
                    };

                    $name(bytes, PhantomData)
                }
            }

            maybe_const_trait_bounded_fn! {
                /// Returns the value as a primitive type, possibly performing
                /// an endianness swap to guarantee that the return value has
                /// the endianness of the native platform.
                #[must_use = "has no side effects"]
                #[inline(always)]
                pub const fn get(self) -> $native {
                    match O::ORDER {
                        Order::BigEndian => $from_be_fn(self.0),
                        Order::LittleEndian => $from_le_fn(self.0),
                    }
                }
            }

            /// Updates the value in place as a primitive type, possibly
            /// performing an endianness swap to guarantee that the stored value
            /// has the endianness `O`.
            #[inline(always)]
            pub fn set(&mut self, n: $native) {
                *self = Self::new(n);
            }
        }

        // The reasoning behind which traits to implement here is to only
        // implement traits which won't cause inference issues. Notably,
        // comparison traits like PartialEq and PartialOrd tend to cause
        // inference issues.

        impl<O: ByteOrder> From<$name<O>> for [u8; $bytes] {
            #[inline(always)]
            fn from(x: $name<O>) -> [u8; $bytes] {
                x.0
            }
        }

        impl<O: ByteOrder> From<[u8; $bytes]> for $name<O> {
            #[inline(always)]
            fn from(bytes: [u8; $bytes]) -> $name<O> {
                $name(bytes, PhantomData)
            }
        }

        impl<O: ByteOrder> From<$name<O>> for $native {
            #[inline(always)]
            fn from(x: $name<O>) -> $native {
                x.get()
            }
        }

        impl<O: ByteOrder> From<$native> for $name<O> {
            #[inline(always)]
            fn from(x: $native) -> $name<O> {
                $name::new(x)
            }
        }

        $(
            impl<O: ByteOrder> From<$name<O>> for $larger_native {
                #[inline(always)]
                fn from(x: $name<O>) -> $larger_native {
                    x.get().into()
                }
            }
        )*

        $(
            impl<O: ByteOrder> TryFrom<$larger_native_try> for $name<O> {
                type Error = TryFromIntError;
                #[inline(always)]
                fn try_from(x: $larger_native_try) -> Result<$name<O>, TryFromIntError> {
                    $native::try_from(x).map($name::new)
                }
            }
        )*

        $(
            impl<O: ByteOrder, P: ByteOrder> From<$name<O>> for $larger_byteorder<P> {
                #[inline(always)]
                fn from(x: $name<O>) -> $larger_byteorder<P> {
                    $larger_byteorder::new(x.get().into())
                }
            }
        )*

        $(
            impl<O: ByteOrder, P: ByteOrder> TryFrom<$larger_byteorder_try<P>> for $name<O> {
                type Error = TryFromIntError;
                #[inline(always)]
                fn try_from(x: $larger_byteorder_try<P>) -> Result<$name<O>, TryFromIntError> {
                    x.get().try_into().map($name::new)
                }
            }
        )*

        impl<O> AsRef<[u8; $bytes]> for $name<O> {
            #[inline(always)]
            fn as_ref(&self) -> &[u8; $bytes] {
                &self.0
            }
        }

        impl<O> AsMut<[u8; $bytes]> for $name<O> {
            #[inline(always)]
            fn as_mut(&mut self) -> &mut [u8; $bytes] {
                &mut self.0
            }
        }

        impl<O> PartialEq<$name<O>> for [u8; $bytes] {
            #[inline(always)]
            fn eq(&self, other: &$name<O>) -> bool {
                self.eq(&other.0)
            }
        }

        impl<O> PartialEq<[u8; $bytes]> for $name<O> {
            #[inline(always)]
            fn eq(&self, other: &[u8; $bytes]) -> bool {
                self.0.eq(other)
            }
        }

        impl<O: ByteOrder> PartialEq<$native> for $name<O> {
            #[inline(always)]
            fn eq(&self, other: &$native) -> bool {
                self.get().eq(other)
            }
        }

        impl_fmt_traits!($name, $native, $number_kind);
        impl_ops_traits!($name, $native, $number_kind);

        impl<O: ByteOrder> Debug for $name<O> {
            #[inline]
            fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
                // This results in a format like "U16(42)".
                f.debug_tuple(stringify!($name)).field(&self.get()).finish()
            }
        }
    };
}

define_type!(
    A,
    "A 16-bit unsigned integer",
    U16,
    u16,
    16,
    2,
    u16::from_be_bytes,
    u16::to_be_bytes,
    u16::from_le_bytes,
    u16::to_le_bytes,
    "unsigned integer",
    [u32, u64, u128, usize],
    [u32, u64, u128, usize],
    [U32, U64, U128, Usize],
    [U32, U64, U128, Usize]
);
define_type!(
    A,
    "A 32-bit unsigned integer",
    U32,
    u32,
    32,
    4,
    u32::from_be_bytes,
    u32::to_be_bytes,
    u32::from_le_bytes,
    u32::to_le_bytes,
    "unsigned integer",
    [u64, u128],
    [u64, u128],
    [U64, U128],
    [U64, U128]
);
define_type!(
    A,
    "A 64-bit unsigned integer",
    U64,
    u64,
    64,
    8,
    u64::from_be_bytes,
    u64::to_be_bytes,
    u64::from_le_bytes,
    u64::to_le_bytes,
    "unsigned integer",
    [u128],
    [u128],
    [U128],
    [U128]
);
define_type!(
    A,
    "A 128-bit unsigned integer",
    U128,
    u128,
    128,
    16,
    u128::from_be_bytes,
    u128::to_be_bytes,
    u128::from_le_bytes,
    u128::to_le_bytes,
    "unsigned integer",
    [],
    [],
    [],
    []
);
define_type!(
    A,
    "A word-sized unsigned integer",
    Usize,
    usize,
    mem::size_of::<usize>() * 8,
    mem::size_of::<usize>(),
    usize::from_be_bytes,
    usize::to_be_bytes,
    usize::from_le_bytes,
    usize::to_le_bytes,
    "unsigned integer",
    [],
    [],
    [],
    []
);
define_type!(
    An,
    "A 16-bit signed integer",
    I16,
    i16,
    16,
    2,
    i16::from_be_bytes,
    i16::to_be_bytes,
    i16::from_le_bytes,
    i16::to_le_bytes,
    "signed integer",
    [i32, i64, i128, isize],
    [i32, i64, i128, isize],
    [I32, I64, I128, Isize],
    [I32, I64, I128, Isize]
);
define_type!(
    An,
    "A 32-bit signed integer",
    I32,
    i32,
    32,
    4,
    i32::from_be_bytes,
    i32::to_be_bytes,
    i32::from_le_bytes,
    i32::to_le_bytes,
    "signed integer",
    [i64, i128],
    [i64, i128],
    [I64, I128],
    [I64, I128]
);
define_type!(
    An,
    "A 64-bit signed integer",
    I64,
    i64,
    64,
    8,
    i64::from_be_bytes,
    i64::to_be_bytes,
    i64::from_le_bytes,
    i64::to_le_bytes,
    "signed integer",
    [i128],
    [i128],
    [I128],
    [I128]
);
define_type!(
    An,
    "A 128-bit signed integer",
    I128,
    i128,
    128,
    16,
    i128::from_be_bytes,
    i128::to_be_bytes,
    i128::from_le_bytes,
    i128::to_le_bytes,
    "signed integer",
    [],
    [],
    [],
    []
);
define_type!(
    An,
    "A word-sized signed integer",
    Isize,
    isize,
    mem::size_of::<isize>() * 8,
    mem::size_of::<isize>(),
    isize::from_be_bytes,
    isize::to_be_bytes,
    isize::from_le_bytes,
    isize::to_le_bytes,
    "signed integer",
    [],
    [],
    [],
    []
);

// FIXME(https://github.com/rust-lang/rust/issues/72447): Use the endianness
// conversion methods directly once those are const-stable.
macro_rules! define_float_conversion {
    ($ty:ty, $bits:ident, $bytes:expr, $mod:ident) => {
        mod $mod {
            use super::*;

            define_float_conversion!($ty, $bits, $bytes, from_be_bytes, to_be_bytes);
            define_float_conversion!($ty, $bits, $bytes, from_le_bytes, to_le_bytes);
        }
    };
    ($ty:ty, $bits:ident, $bytes:expr, $from:ident, $to:ident) => {
        // Clippy: The suggestion of using `from_bits()` instead doesn't work
        // because `from_bits` is not const-stable on our MSRV.
        #[allow(clippy::unnecessary_transmutes)]
        pub(crate) const fn $from(bytes: [u8; $bytes]) -> $ty {
            transmute!($bits::$from(bytes))
        }

        pub(crate) const fn $to(f: $ty) -> [u8; $bytes] {
            // Clippy: The suggestion of using `f.to_bits()` instead doesn't
            // work because `to_bits` is not const-stable on our MSRV.
            #[allow(clippy::unnecessary_transmutes)]
            let bits: $bits = transmute!(f);
            bits.$to()
        }
    };
}

define_float_conversion!(f32, u32, 4, f32_ext);
define_float_conversion!(f64, u64, 8, f64_ext);

define_type!(
    An,
    "A 32-bit floating point number",
    F32,
    f32,
    32,
    4,
    f32_ext::from_be_bytes,
    f32_ext::to_be_bytes,
    f32_ext::from_le_bytes,
    f32_ext::to_le_bytes,
    "floating point number",
    [f64],
    [],
    [F64],
    []
);
define_type!(
    An,
    "A 64-bit floating point number",
    F64,
    f64,
    64,
    8,
    f64_ext::from_be_bytes,
    f64_ext::to_be_bytes,
    f64_ext::from_le_bytes,
    f64_ext::to_le_bytes,
    "floating point number",
    [],
    [],
    [],
    []
);

macro_rules! module {
    ($name:ident, $trait:ident, $endianness_str:expr) => {
        /// Numeric primitives stored in
        #[doc = $endianness_str]
        /// byte order.
        pub mod $name {
            use super::$trait;

            module!(@ty U16,  $trait, "16-bit unsigned integer", $endianness_str);
            module!(@ty U32,  $trait, "32-bit unsigned integer", $endianness_str);
            module!(@ty U64,  $trait, "64-bit unsigned integer", $endianness_str);
            module!(@ty U128, $trait, "128-bit unsigned integer", $endianness_str);
            module!(@ty I16,  $trait, "16-bit signed integer", $endianness_str);
            module!(@ty I32,  $trait, "32-bit signed integer", $endianness_str);
            module!(@ty I64,  $trait, "64-bit signed integer", $endianness_str);
            module!(@ty I128, $trait, "128-bit signed integer", $endianness_str);
            module!(@ty F32,  $trait, "32-bit floating point number", $endianness_str);
            module!(@ty F64,  $trait, "64-bit floating point number", $endianness_str);
        }
    };
    (@ty $ty:ident, $trait:ident, $desc_str:expr, $endianness_str:expr) => {
        /// A
        #[doc = $desc_str]
        /// stored in
        #[doc = $endianness_str]
        /// byte order.
        pub type $ty = crate::byteorder::$ty<$trait>;
    };
}

module!(big_endian, BigEndian, "big-endian");
module!(little_endian, LittleEndian, "little-endian");
module!(network_endian, NetworkEndian, "network-endian");
module!(native_endian, NativeEndian, "native-endian");

#[cfg(any(test, kani))]
mod tests {
    use super::*;

    #[cfg(not(kani))]
    mod compatibility {
        pub(super) use rand::{
            distributions::{Distribution, Standard},
            rngs::SmallRng,
            Rng, SeedableRng,
        };

        pub(crate) trait Arbitrary {}

        impl<T> Arbitrary for T {}
    }

    #[cfg(kani)]
    mod compatibility {
        pub(crate) use kani::Arbitrary;

        pub(crate) struct SmallRng;

        impl SmallRng {
            pub(crate) fn seed_from_u64(_state: u64) -> Self {
                Self
            }
        }

        pub(crate) trait Rng {
            fn sample<T, D: Distribution<T>>(&mut self, _distr: D) -> T
            where
                T: Arbitrary,
            {
                kani::any()
            }
        }

        impl Rng for SmallRng {}

        pub(crate) trait Distribution<T> {}
        impl<T, U> Distribution<T> for U {}

        pub(crate) struct Standard;
    }

    use compatibility::*;

    // A native integer type (u16, i32, etc).
    trait Native: Arbitrary + FromBytes + IntoBytes + Immutable + Copy + PartialEq + Debug {
        const ZERO: Self;
        const MAX_VALUE: Self;

        type Distribution: Distribution<Self>;
        const DIST: Self::Distribution;

        fn rand<R: Rng>(rng: &mut R) -> Self {
            rng.sample(Self::DIST)
        }

        #[cfg_attr(kani, allow(unused))]
        fn checked_add(self, rhs: Self) -> Option<Self>;

        #[cfg_attr(kani, allow(unused))]
        fn checked_div(self, rhs: Self) -> Option<Self>;

        #[cfg_attr(kani, allow(unused))]
        fn checked_mul(self, rhs: Self) -> Option<Self>;

        #[cfg_attr(kani, allow(unused))]
        fn checked_rem(self, rhs: Self) -> Option<Self>;

        #[cfg_attr(kani, allow(unused))]
        fn checked_sub(self, rhs: Self) -> Option<Self>;

        #[cfg_attr(kani, allow(unused))]
        fn checked_shl(self, rhs: Self) -> Option<Self>;

        #[cfg_attr(kani, allow(unused))]
        fn checked_shr(self, rhs: Self) -> Option<Self>;

        fn is_nan(self) -> bool;

        /// For `f32` and `f64`, NaN values are not considered equal to
        /// themselves. This method is like `assert_eq!`, but it treats NaN
        /// values as equal.
        fn assert_eq_or_nan(self, other: Self) {
            let slf = (!self.is_nan()).then(|| self);
            let other = (!other.is_nan()).then(|| other);
            assert_eq!(slf, other);
        }
    }

    trait ByteArray:
        FromBytes + IntoBytes + Immutable + Copy + AsRef<[u8]> + AsMut<[u8]> + Debug + Default + Eq
    {
        /// Invert the order of the bytes in the array.
        fn invert(self) -> Self;
    }

    trait ByteOrderType:
        FromBytes + IntoBytes + Unaligned + Copy + Eq + Debug + Hash + From<Self::Native>
    {
        type Native: Native;
        type ByteArray: ByteArray;

        const ZERO: Self;

        fn new(native: Self::Native) -> Self;
        fn get(self) -> Self::Native;
        fn set(&mut self, native: Self::Native);
        fn from_bytes(bytes: Self::ByteArray) -> Self;
        fn into_bytes(self) -> Self::ByteArray;

        /// For `f32` and `f64`, NaN values are not considered equal to
        /// themselves. This method is like `assert_eq!`, but it treats NaN
        /// values as equal.
        fn assert_eq_or_nan(self, other: Self) {
            let slf = (!self.get().is_nan()).then(|| self);
            let other = (!other.get().is_nan()).then(|| other);
            assert_eq!(slf, other);
        }
    }

    trait ByteOrderTypeUnsigned: ByteOrderType {
        const MAX_VALUE: Self;
    }

    macro_rules! impl_byte_array {
        ($bytes:expr) => {
            impl ByteArray for [u8; $bytes] {
                fn invert(mut self) -> [u8; $bytes] {
                    self.reverse();
                    self
                }
            }
        };
    }

    impl_byte_array!(2);
    impl_byte_array!(4);
    impl_byte_array!(8);
    impl_byte_array!(16);

    macro_rules! impl_byte_order_type_unsigned {
        ($name:ident, unsigned) => {
            impl<O: ByteOrder> ByteOrderTypeUnsigned for $name<O> {
                const MAX_VALUE: $name<O> = $name::MAX_VALUE;
            }
        };
        ($name:ident, signed) => {};
    }

    macro_rules! impl_traits {
        ($name:ident, $native:ident, $sign:ident $(, @$float:ident)?) => {
            impl Native for $native {
                // For some types, `0 as $native` is required (for example, when
                // `$native` is a floating-point type; `0` is an integer), but
                // for other types, it's a trivial cast. In all cases, Clippy
                // thinks it's dangerous.
                #[allow(trivial_numeric_casts, clippy::as_conversions)]
                const ZERO: $native = 0 as $native;
                const MAX_VALUE: $native = $native::MAX;

                type Distribution = Standard;
                const DIST: Standard = Standard;

                impl_traits!(@float_dependent_methods $(@$float)?);
            }

            impl<O: ByteOrder> ByteOrderType for $name<O> {
                type Native = $native;
                type ByteArray = [u8; mem::size_of::<$native>()];

                const ZERO: $name<O> = $name::ZERO;

                fn new(native: $native) -> $name<O> {
                    $name::new(native)
                }

                fn get(self) -> $native {
                    $name::get(self)
                }

                fn set(&mut self, native: $native) {
                    $name::set(self, native)
                }

                fn from_bytes(bytes: [u8; mem::size_of::<$native>()]) -> $name<O> {
                    $name::from(bytes)
                }

                fn into_bytes(self) -> [u8; mem::size_of::<$native>()] {
                    <[u8; mem::size_of::<$native>()]>::from(self)
                }
            }

            impl_byte_order_type_unsigned!($name, $sign);
        };
        (@float_dependent_methods) => {
            fn checked_add(self, rhs: Self) -> Option<Self> { self.checked_add(rhs) }
            fn checked_div(self, rhs: Self) -> Option<Self> { self.checked_div(rhs) }
            fn checked_mul(self, rhs: Self) -> Option<Self> { self.checked_mul(rhs) }
            fn checked_rem(self, rhs: Self) -> Option<Self> { self.checked_rem(rhs) }
            fn checked_sub(self, rhs: Self) -> Option<Self> { self.checked_sub(rhs) }
            fn checked_shl(self, rhs: Self) -> Option<Self> { self.checked_shl(rhs.try_into().unwrap_or(u32::MAX)) }
            fn checked_shr(self, rhs: Self) -> Option<Self> { self.checked_shr(rhs.try_into().unwrap_or(u32::MAX)) }
            fn is_nan(self) -> bool { false }
        };
        (@float_dependent_methods @float) => {
            fn checked_add(self, rhs: Self) -> Option<Self> { Some(self + rhs) }
            fn checked_div(self, rhs: Self) -> Option<Self> { Some(self / rhs) }
            fn checked_mul(self, rhs: Self) -> Option<Self> { Some(self * rhs) }
            fn checked_rem(self, rhs: Self) -> Option<Self> { Some(self % rhs) }
            fn checked_sub(self, rhs: Self) -> Option<Self> { Some(self - rhs) }
            fn checked_shl(self, _rhs: Self) -> Option<Self> { unimplemented!() }
            fn checked_shr(self, _rhs: Self) -> Option<Self> { unimplemented!() }
            fn is_nan(self) -> bool { self.is_nan() }
        };
    }

    impl_traits!(U16, u16, unsigned);
    impl_traits!(U32, u32, unsigned);
    impl_traits!(U64, u64, unsigned);
    impl_traits!(U128, u128, unsigned);
    impl_traits!(Usize, usize, unsigned);
    impl_traits!(I16, i16, signed);
    impl_traits!(I32, i32, signed);
    impl_traits!(I64, i64, signed);
    impl_traits!(I128, i128, signed);
    impl_traits!(Isize, isize, unsigned);
    impl_traits!(F32, f32, signed, @float);
    impl_traits!(F64, f64, signed, @float);

    macro_rules! call_for_unsigned_types {
        ($fn:ident, $byteorder:ident) => {
            $fn::<U16<$byteorder>>();
            $fn::<U32<$byteorder>>();
            $fn::<U64<$byteorder>>();
            $fn::<U128<$byteorder>>();
            $fn::<Usize<$byteorder>>();
        };
    }

    macro_rules! call_for_signed_types {
        ($fn:ident, $byteorder:ident) => {
            $fn::<I16<$byteorder>>();
            $fn::<I32<$byteorder>>();
            $fn::<I64<$byteorder>>();
            $fn::<I128<$byteorder>>();
            $fn::<Isize<$byteorder>>();
        };
    }

    macro_rules! call_for_float_types {
        ($fn:ident, $byteorder:ident) => {
            $fn::<F32<$byteorder>>();
            $fn::<F64<$byteorder>>();
        };
    }

    macro_rules! call_for_all_types {
        ($fn:ident, $byteorder:ident) => {
            call_for_unsigned_types!($fn, $byteorder);
            call_for_signed_types!($fn, $byteorder);
            call_for_float_types!($fn, $byteorder);
        };
    }

    #[cfg(target_endian = "big")]
    type NonNativeEndian = LittleEndian;
    #[cfg(target_endian = "little")]
    type NonNativeEndian = BigEndian;

    // We use a `u64` seed so that we can use `SeedableRng::seed_from_u64`.
    // `SmallRng`'s `SeedableRng::Seed` differs by platform, so if we wanted to
    // call `SeedableRng::from_seed`, which takes a `Seed`, we would need
    // conditional compilation by `target_pointer_width`.
    const RNG_SEED: u64 = 0x7A03CAE2F32B5B8F;

    const RAND_ITERS: usize = if cfg!(any(miri, kani)) {
        // The tests below which use this constant used to take a very long time
        // on Miri, which slows down local development and CI jobs. We're not
        // using Miri to check for the correctness of our code, but rather its
        // soundness, and at least in the context of these particular tests, a
        // single loop iteration is just as good for surfacing UB as multiple
        // iterations are.
        //
        // As of the writing of this comment, here's one set of measurements:
        //
        //   $ # RAND_ITERS == 1
        //   $ cargo miri test -- -Z unstable-options --report-time endian
        //   test byteorder::tests::test_native_endian ... ok <0.049s>
        //   test byteorder::tests::test_non_native_endian ... ok <0.061s>
        //
        //   $ # RAND_ITERS == 1024
        //   $ cargo miri test -- -Z unstable-options --report-time endian
        //   test byteorder::tests::test_native_endian ... ok <25.716s>
        //   test byteorder::tests::test_non_native_endian ... ok <38.127s>
        1
    } else {
        1024
    };

    #[test]
    fn test_const_methods() {
        use big_endian::*;

        #[rustversion::since(1.61.0)]
        const _U: U16 = U16::new(0);
        #[rustversion::since(1.61.0)]
        const _NATIVE: u16 = _U.get();
        const _FROM_BYTES: U16 = U16::from_bytes([0, 1]);
        const _BYTES: [u8; 2] = _FROM_BYTES.to_bytes();
    }

    #[cfg_attr(test, test)]
    #[cfg_attr(kani, kani::proof)]
    fn test_zero() {
        fn test_zero<T: ByteOrderType>() {
            assert_eq!(T::ZERO.get(), T::Native::ZERO);
        }

        call_for_all_types!(test_zero, NativeEndian);
        call_for_all_types!(test_zero, NonNativeEndian);
    }

    #[cfg_attr(test, test)]
    #[cfg_attr(kani, kani::proof)]
    fn test_max_value() {
        fn test_max_value<T: ByteOrderTypeUnsigned>() {
            assert_eq!(T::MAX_VALUE.get(), T::Native::MAX_VALUE);
        }

        call_for_unsigned_types!(test_max_value, NativeEndian);
        call_for_unsigned_types!(test_max_value, NonNativeEndian);
    }

    #[cfg_attr(test, test)]
    #[cfg_attr(kani, kani::proof)]
    fn test_endian() {
        fn test<T: ByteOrderType>(invert: bool) {
            let mut r = SmallRng::seed_from_u64(RNG_SEED);
            for _ in 0..RAND_ITERS {
                let native = T::Native::rand(&mut r);
                let mut bytes = T::ByteArray::default();
                bytes.as_mut_bytes().copy_from_slice(native.as_bytes());
                if invert {
                    bytes = bytes.invert();
                }
                let mut from_native = T::new(native);
                let from_bytes = T::from_bytes(bytes);

                from_native.assert_eq_or_nan(from_bytes);
                from_native.get().assert_eq_or_nan(native);
                from_bytes.get().assert_eq_or_nan(native);

                assert_eq!(from_native.into_bytes(), bytes);
                assert_eq!(from_bytes.into_bytes(), bytes);

                let updated = T::Native::rand(&mut r);
                from_native.set(updated);
                from_native.get().assert_eq_or_nan(updated);
            }
        }

        fn test_native<T: ByteOrderType>() {
            test::<T>(false);
        }

        fn test_non_native<T: ByteOrderType>() {
            test::<T>(true);
        }

        call_for_all_types!(test_native, NativeEndian);
        call_for_all_types!(test_non_native, NonNativeEndian);
    }

    #[test]
    fn test_ops_impls() {
        // Test implementations of traits in `core::ops`. Some of these are
        // fairly banal, but some are optimized to perform the operation without
        // swapping byte order (namely, bit-wise operations which are identical
        // regardless of byte order). These are important to test, and while
        // we're testing those anyway, it's trivial to test all of the impls.

        fn test<T, FTT, FTN, FNT, FNN, FNNChecked, FATT, FATN, FANT>(
            op_t_t: FTT,
            op_t_n: FTN,
            op_n_t: FNT,
            op_n_n: FNN,
            op_n_n_checked: Option<FNNChecked>,
            op_assign: Option<(FATT, FATN, FANT)>,
        ) where
            T: ByteOrderType,
            FTT: Fn(T, T) -> T,
            FTN: Fn(T, T::Native) -> T,
            FNT: Fn(T::Native, T) -> T,
            FNN: Fn(T::Native, T::Native) -> T::Native,
            FNNChecked: Fn(T::Native, T::Native) -> Option<T::Native>,
            FATT: Fn(&mut T, T),
            FATN: Fn(&mut T, T::Native),
            FANT: Fn(&mut T::Native, T),
        {
            let mut r = SmallRng::seed_from_u64(RNG_SEED);
            for _ in 0..RAND_ITERS {
                let n0 = T::Native::rand(&mut r);
                let n1 = T::Native::rand(&mut r);
                let t0 = T::new(n0);
                let t1 = T::new(n1);

                // If this operation would overflow/underflow, skip it rather
                // than attempt to catch and recover from panics.
                if matches!(&op_n_n_checked, Some(checked) if checked(n0, n1).is_none()) {
                    continue;
                }

                let t_t_res = op_t_t(t0, t1);
                let t_n_res = op_t_n(t0, n1);
                let n_t_res = op_n_t(n0, t1);
                let n_n_res = op_n_n(n0, n1);

                // For `f32` and `f64`, NaN values are not considered equal to
                // themselves. We store `Option<f32>`/`Option<f64>` and store
                // NaN as `None` so they can still be compared.
                let val_or_none = |t: T| (!T::Native::is_nan(t.get())).then(|| t.get());
                let t_t_res = val_or_none(t_t_res);
                let t_n_res = val_or_none(t_n_res);
                let n_t_res = val_or_none(n_t_res);
                let n_n_res = (!T::Native::is_nan(n_n_res)).then(|| n_n_res);
                assert_eq!(t_t_res, n_n_res);
                assert_eq!(t_n_res, n_n_res);
                assert_eq!(n_t_res, n_n_res);

                if let Some((op_assign_t_t, op_assign_t_n, op_assign_n_t)) = &op_assign {
                    let mut t_t_res = t0;
                    op_assign_t_t(&mut t_t_res, t1);
                    let mut t_n_res = t0;
                    op_assign_t_n(&mut t_n_res, n1);
                    let mut n_t_res = n0;
                    op_assign_n_t(&mut n_t_res, t1);

                    // For `f32` and `f64`, NaN values are not considered equal to
                    // themselves. We store `Option<f32>`/`Option<f64>` and store
                    // NaN as `None` so they can still be compared.
                    let t_t_res = val_or_none(t_t_res);
                    let t_n_res = val_or_none(t_n_res);
                    let n_t_res = (!T::Native::is_nan(n_t_res)).then(|| n_t_res);
                    assert_eq!(t_t_res, n_n_res);
                    assert_eq!(t_n_res, n_n_res);
                    assert_eq!(n_t_res, n_n_res);
                }
            }
        }

        macro_rules! test {
            (
                @binary
                $trait:ident,
                $method:ident $([$checked_method:ident])?,
                $trait_assign:ident,
                $method_assign:ident,
                $($call_for_macros:ident),*
            ) => {{
                fn t<T>()
                where
                    T: ByteOrderType,
                    T: core::ops::$trait<T, Output = T>,
                    T: core::ops::$trait<T::Native, Output = T>,
                    T::Native: core::ops::$trait<T, Output = T>,
                    T::Native: core::ops::$trait<T::Native, Output = T::Native>,

                    T: core::ops::$trait_assign<T>,
                    T: core::ops::$trait_assign<T::Native>,
                    T::Native: core::ops::$trait_assign<T>,
                    T::Native: core::ops::$trait_assign<T::Native>,
                {
                    test::<T, _, _, _, _, _, _, _, _>(
                        core::ops::$trait::$method,
                        core::ops::$trait::$method,
                        core::ops::$trait::$method,
                        core::ops::$trait::$method,
                        {
                            #[allow(unused_mut, unused_assignments)]
                            let mut op_native_checked = None::<fn(T::Native, T::Native) -> Option<T::Native>>;
                            $(
                                op_native_checked = Some(T::Native::$checked_method);
                            )?
                            op_native_checked
                        },
                        Some((
                            <T as core::ops::$trait_assign<T>>::$method_assign,
                            <T as core::ops::$trait_assign::<T::Native>>::$method_assign,
                            <T::Native as core::ops::$trait_assign::<T>>::$method_assign
                        )),
                    );
                }

                $(
                    $call_for_macros!(t, NativeEndian);
                    $call_for_macros!(t, NonNativeEndian);
                )*
            }};
            (
                @unary
                $trait:ident,
                $method:ident,
                $($call_for_macros:ident),*
            ) => {{
                fn t<T>()
                where
                    T: ByteOrderType,
                    T: core::ops::$trait<Output = T>,
                    T::Native: core::ops::$trait<Output = T::Native>,
                {
                    test::<T, _, _, _, _, _, _, _, _>(
                        |slf, _rhs| core::ops::$trait::$method(slf),
                        |slf, _rhs| core::ops::$trait::$method(slf),
                        |slf, _rhs| core::ops::$trait::$method(slf).into(),
                        |slf, _rhs| core::ops::$trait::$method(slf),
                        None::<fn(T::Native, T::Native) -> Option<T::Native>>,
                        None::<(fn(&mut T, T), fn(&mut T, T::Native), fn(&mut T::Native, T))>,
                    );
                }

                $(
                    $call_for_macros!(t, NativeEndian);
                    $call_for_macros!(t, NonNativeEndian);
                )*
            }};
        }

        test!(@binary Add, add[checked_add], AddAssign, add_assign, call_for_all_types);
        test!(@binary Div, div[checked_div], DivAssign, div_assign, call_for_all_types);
        test!(@binary Mul, mul[checked_mul], MulAssign, mul_assign, call_for_all_types);
        test!(@binary Rem, rem[checked_rem], RemAssign, rem_assign, call_for_all_types);
        test!(@binary Sub, sub[checked_sub], SubAssign, sub_assign, call_for_all_types);

        test!(@binary BitAnd, bitand, BitAndAssign, bitand_assign, call_for_unsigned_types, call_for_signed_types);
        test!(@binary BitOr, bitor, BitOrAssign, bitor_assign, call_for_unsigned_types, call_for_signed_types);
        test!(@binary BitXor, bitxor, BitXorAssign, bitxor_assign, call_for_unsigned_types, call_for_signed_types);
        test!(@binary Shl, shl[checked_shl], ShlAssign, shl_assign, call_for_unsigned_types, call_for_signed_types);
        test!(@binary Shr, shr[checked_shr], ShrAssign, shr_assign, call_for_unsigned_types, call_for_signed_types);

        test!(@unary Not, not, call_for_signed_types, call_for_unsigned_types);
        test!(@unary Neg, neg, call_for_signed_types, call_for_float_types);
    }

    #[test]
    fn test_debug_impl() {
        // Ensure that Debug applies format options to the inner value.
        let val = U16::<LE>::new(10);
        assert_eq!(format!("{:?}", val), "U16(10)");
        assert_eq!(format!("{:03?}", val), "U16(010)");
        assert_eq!(format!("{:x?}", val), "U16(a)");
    }

    #[test]
    fn test_byteorder_traits_coverage() {
        let val_be = U16::<BigEndian>::from_bytes([0, 1]);
        let val_le = U16::<LittleEndian>::from_bytes([1, 0]);

        assert_eq!(val_be.get(), 1);
        assert_eq!(val_le.get(), 1);

        // Debug
        assert_eq!(format!("{:?}", val_be), "U16(1)");
        assert_eq!(format!("{:?}", val_le), "U16(1)");

        // PartialOrd, Ord with same type
        assert!(val_be >= val_be);
        assert!(val_be <= val_be);
        assert_eq!(val_be.cmp(&val_be), core::cmp::Ordering::Equal);

        // PartialOrd with native
        assert!(val_be == 1u16);
        assert!(val_be >= 1u16);

        // Default
        let default_be: U16<BigEndian> = Default::default();
        assert_eq!(default_be.get(), 0);

        // I16
        let val_be_i16 = I16::<BigEndian>::from_bytes([0, 1]);
        assert_eq!(val_be_i16.get(), 1);
        assert_eq!(format!("{:?}", val_be_i16), "I16(1)");
        assert_eq!(val_be_i16.cmp(&val_be_i16), core::cmp::Ordering::Equal);
    }
}
