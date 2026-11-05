// SPDX-License-Identifier: GPL-2.0

//! Additional numerical features for the kernel.

use core::ops;

pub mod bounded;
pub mod casts;

pub use bounded::*;

/// Designates unsigned primitive types.
pub enum Unsigned {}

/// Designates signed primitive types.
pub enum Signed {}

mod private {
    pub trait Sealed {}
}

/// Describes core properties of integer types.
pub trait Integer:
    private::Sealed
    + Sized
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
        impl private::Sealed for $type {}

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

/// Creates a constant value ("cv") from an integer constant expression, with validity checked at
/// compile time.
///
/// This works for any type that implements [`FromConst`], with the target type inferred from
/// the context, or named explicitly with `cv!(value => Type)`. It can also be used in const
/// contexts.
///
/// # Pitfalls
///
/// Only the target type is inferred from the context. The expression passed to [`cv!`] is typed by
/// itself. So, unsuffixed integer literal expressions default to [`i32`]. For example
/// `cv!(2 << 31)` does not evaluate to the same value as `cv!(2i64 << 31)`, since `2 << 31` is
/// treated as an [`i32`] before being widened to [`i128`] internally. Literals and arithmetic
/// expressions that overflow [`i32`] are a compile error, it's just left shifts that require care.
///
/// # Examples
///
/// ```
/// use core::num::NonZero;
///
/// use kernel::{
///     num::Bounded,
///     ptr::Alignment, //
/// };
///
/// let v: NonZero<usize> = cv!(8);
/// assert_eq!(v.get(), 8);
///
/// // Any integer constant expression works, not only literals.
/// let m: NonZero<usize> = cv!(usize::MAX);
/// assert_eq!(m.get(), usize::MAX);
///
/// let b: Bounded<u32, 4> = cv!(15);
/// assert_eq!(b.get(), 15);
///
/// let a: Alignment = cv!(4096);
/// assert_eq!(a.as_usize(), 4096);
///
/// // Checked narrowing of integer constants, including in `const` items.
/// const SMALL: u8 = cv!(200u32);
/// assert_eq!(SMALL, 200);
///
/// const N: NonZero<u8> = cv!(5);
/// assert_eq!(N.get(), 5);
///
/// // The target type can be given explicitly.
/// let e = cv!(200u32 => u8);
/// assert_eq!(e, 200);
///
/// // With an explicit primitive target, the expression can use generic parameters.
/// const fn as_u64<const KEY: u16>() -> u64 {
///     cv!(KEY => u64)
/// }
/// assert_eq!(as_u64::<0x40>(), 0x40);
/// ```
#[macro_export]
#[doc(hidden)]
macro_rules! cv {
    (@cast $v:expr => $t:ty) => {
        const {
            #[allow(unused_comparisons, unused_assignments, clippy::as_underscore)]
            {
                let v = $v;
                let r = v as $t;
                // Pin `back` to `v`'s type so `as _` casts back to the source type.
                let mut back = v;
                back = r as _;

                ::core::assert!(
                    back == v && (v < 0) == (r < 0),
                    "value does not fit into the target type"
                );

                r
            }
        }
    };

    // Using `FromConst<V>::VALUE` means const generic expressions can't be used, since it requires
    // the `generic_const_exprs` feature. Provide a special path for each primitive type that allows
    // const generic expressions.
    ($v:expr => u8) => { $crate::cv!(@cast $v => u8) };
    ($v:expr => u16) => { $crate::cv!(@cast $v => u16) };
    ($v:expr => u32) => { $crate::cv!(@cast $v => u32) };
    ($v:expr => u64) => { $crate::cv!(@cast $v => u64) };
    ($v:expr => u128) => { $crate::cv!(@cast $v => u128) };
    ($v:expr => usize) => { $crate::cv!(@cast $v => usize) };
    ($v:expr => i8) => { $crate::cv!(@cast $v => i8) };
    ($v:expr => i16) => { $crate::cv!(@cast $v => i16) };
    ($v:expr => i32) => { $crate::cv!(@cast $v => i32) };
    ($v:expr => i64) => { $crate::cv!(@cast $v => i64) };
    ($v:expr => i128) => { $crate::cv!(@cast $v => i128) };
    ($v:expr => isize) => { $crate::cv!(@cast $v => isize) };
    ($v:expr => $t:ty) => {
        <$t as $crate::num::FromConst<{ $crate::cv!(@cast $v => i128) }>>::VALUE
    };
    ($v:expr) => {
        <_ as $crate::num::FromConst<{ $crate::cv!(@cast $v => i128) }>>::VALUE
    };
}
#[doc(inline)]
pub use cv;

/// Types that can be created from an integer constant expression validated at compile time.
///
/// Implement this trait to make a type usable with [`cv!`]. Use the [`cv!`] macro, not this trait
/// directly, for creating values.
#[diagnostic::on_unimplemented(message = "`{Self}` cannot be converted from a constant")]
pub trait FromConst<const V: i128>: Sized {
    /// The value that corresponds to the constant `V`.
    ///
    /// Fails the build if `V` is not a valid value for [`Self`].
    const VALUE: Self;
}

/// Implements [`FromConst`] for primitive integer types and their [`NonZero`](core::num::NonZero)
/// versions.
macro_rules! impl_from_const {
    ($($type:ty)*) => {
        $(
        impl<const V: i128> FromConst<V> for $type {
            const VALUE: Self = {
                // CAST: The macro is only used for types up to 64 bits wide, so `MIN` and `MAX`
                // widen to `i128` losslessly.
                assert!(
                    V >= <$type>::MIN as i128 && V <= <$type>::MAX as i128,
                    "constant cannot be represented by the target type"
                );

                // CAST: The assert above confirmed that `V` fits in `$type`.
                V as $type
            };
        }

        impl<const V: i128> FromConst<V> for core::num::NonZero<$type> {
            const VALUE: Self = match core::num::NonZero::new(<$type as FromConst<V>>::VALUE) {
                Some(value) => value,
                None => panic!("constant cannot be zero"),
            };
        }
        )*
    };
}

impl_from_const!(
    u8 u16 u32 u64 usize
    i8 i16 i32 i64 isize
);
