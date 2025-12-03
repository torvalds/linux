// SPDX-License-Identifier: GPL-2.0

//! Additional numerical features for the kernel.

use core::ops;

pub mod bounded;
pub use bounded::*;

/// Designates unsigned primitive types.
pub enum Unsigned {}

/// Designates signed primitive types.
pub enum Signed {}

/// Describes core properties of integer types.
pub trait Integer:
    Sized
    + Copy
    + Clone
    + PartialEq
    + Eq
    + PartialOrd
    + Ord
    + ops::Add<Output = Self>
    + ops::AddAssign
    + ops::Sub<Output = Self>
    + ops::SubAssign
    + ops::Mul<Output = Self>
    + ops::MulAssign
    + ops::Div<Output = Self>
    + ops::DivAssign
    + ops::Rem<Output = Self>
    + ops::RemAssign
    + ops::BitAnd<Output = Self>
    + ops::BitAndAssign
    + ops::BitOr<Output = Self>
    + ops::BitOrAssign
    + ops::BitXor<Output = Self>
    + ops::BitXorAssign
    + ops::Shl<u32, Output = Self>
    + ops::ShlAssign<u32>
    + ops::Shr<u32, Output = Self>
    + ops::ShrAssign<u32>
    + ops::Not
{
    /// Whether this type is [`Signed`] or [`Unsigned`].
    type Signedness;

    /// Number of bits used for value representation.
    const BITS: u32;
}

macro_rules! impl_integer {
    ($($type:ty: $signedness:ty), *) => {
        $(
        impl Integer for $type {
            type Signedness = $signedness;

            const BITS: u32 = <$type>::BITS;
        }
        )*
    };
}

impl_integer!(
    u8: Unsigned,
    u16: Unsigned,
    u32: Unsigned,
    u64: Unsigned,
    u128: Unsigned,
    usize: Unsigned,
    i8: Signed,
    i16: Signed,
    i32: Signed,
    i64: Signed,
    i128: Signed,
    isize: Signed
);
