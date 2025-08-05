// SPDX-License-Identifier: GPL-2.0

//! Bit manipulation macros.
//!
//! C header: [`include/linux/bits.h`](srctree/include/linux/bits.h)

use crate::prelude::*;
use core::ops::RangeInclusive;
use macros::paste;

macro_rules! impl_bit_fn {
    (
        $ty:ty
    ) => {
        paste! {
            /// Computes `1 << n` if `n` is in bounds, i.e.: if `n` is smaller than
            /// the maximum number of bits supported by the type.
            ///
            /// Returns [`None`] otherwise.
            #[inline]
            pub fn [<checked_bit_ $ty>](n: u32) -> Option<$ty> {
                (1 as $ty).checked_shl(n)
            }

            /// Computes `1 << n` by performing a compile-time assertion that `n` is
            /// in bounds.
            ///
            /// This version is the default and should be used if `n` is known at
            /// compile time.
            #[inline]
            pub const fn [<bit_ $ty>](n: u32) -> $ty {
                build_assert!(n < <$ty>::BITS);
                (1 as $ty) << n
            }
        }
    };
}

impl_bit_fn!(u64);
impl_bit_fn!(u32);
impl_bit_fn!(u16);
impl_bit_fn!(u8);

macro_rules! impl_genmask_fn {
    (
        $ty:ty,
        $(#[$genmask_checked_ex:meta])*,
        $(#[$genmask_ex:meta])*
    ) => {
        paste! {
            /// Creates a contiguous bitmask for the given range by validating
            /// the range at runtime.
            ///
            /// Returns [`None`] if the range is invalid, i.e.: if the start is
            /// greater than the end or if the range is outside of the
            /// representable range for the type.
            $(#[$genmask_checked_ex])*
            #[inline]
            pub fn [<genmask_checked_ $ty>](range: RangeInclusive<u32>) -> Option<$ty> {
                let start = *range.start();
                let end = *range.end();

                if start > end {
                    return None;
                }

                let high = [<checked_bit_ $ty>](end)?;
                let low = [<checked_bit_ $ty>](start)?;
                Some((high | (high - 1)) & !(low - 1))
            }

            /// Creates a compile-time contiguous bitmask for the given range by
            /// performing a compile-time assertion that the range is valid.
            ///
            /// This version is the default and should be used if the range is known
            /// at compile time.
            $(#[$genmask_ex])*
            #[inline]
            pub const fn [<genmask_ $ty>](range: RangeInclusive<u32>) -> $ty {
                let start = *range.start();
                let end = *range.end();

                build_assert!(start <= end);

                let high = [<bit_ $ty>](end);
                let low = [<bit_ $ty>](start);
                (high | (high - 1)) & !(low - 1)
            }
        }
    };
}

impl_genmask_fn!(
    u64,
    /// # Examples
    ///
    /// ```
    /// # #![expect(clippy::reversed_empty_ranges)]
    /// # use kernel::bits::genmask_checked_u64;
    /// assert_eq!(genmask_checked_u64(0..=0), Some(0b1));
    /// assert_eq!(genmask_checked_u64(0..=63), Some(u64::MAX));
    /// assert_eq!(genmask_checked_u64(21..=39), Some(0x0000_00ff_ffe0_0000));
    ///
    /// // `80` is out of the supported bit range.
    /// assert_eq!(genmask_checked_u64(21..=80), None);
    ///
    /// // Invalid range where the start is bigger than the end.
    /// assert_eq!(genmask_checked_u64(15..=8), None);
    /// ```
    ,
    /// # Examples
    ///
    /// ```
    /// # use kernel::bits::genmask_u64;
    /// assert_eq!(genmask_u64(21..=39), 0x0000_00ff_ffe0_0000);
    /// assert_eq!(genmask_u64(0..=0), 0b1);
    /// assert_eq!(genmask_u64(0..=63), u64::MAX);
    /// ```
);

impl_genmask_fn!(
    u32,
    /// # Examples
    ///
    /// ```
    /// # #![expect(clippy::reversed_empty_ranges)]
    /// # use kernel::bits::genmask_checked_u32;
    /// assert_eq!(genmask_checked_u32(0..=0), Some(0b1));
    /// assert_eq!(genmask_checked_u32(0..=31), Some(u32::MAX));
    /// assert_eq!(genmask_checked_u32(21..=31), Some(0xffe0_0000));
    ///
    /// // `40` is out of the supported bit range.
    /// assert_eq!(genmask_checked_u32(21..=40), None);
    ///
    /// // Invalid range where the start is bigger than the end.
    /// assert_eq!(genmask_checked_u32(15..=8), None);
    /// ```
    ,
    /// # Examples
    ///
    /// ```
    /// # use kernel::bits::genmask_u32;
    /// assert_eq!(genmask_u32(21..=31), 0xffe0_0000);
    /// assert_eq!(genmask_u32(0..=0), 0b1);
    /// assert_eq!(genmask_u32(0..=31), u32::MAX);
    /// ```
);

impl_genmask_fn!(
    u16,
    /// # Examples
    ///
    /// ```
    /// # #![expect(clippy::reversed_empty_ranges)]
    /// # use kernel::bits::genmask_checked_u16;
    /// assert_eq!(genmask_checked_u16(0..=0), Some(0b1));
    /// assert_eq!(genmask_checked_u16(0..=15), Some(u16::MAX));
    /// assert_eq!(genmask_checked_u16(6..=15), Some(0xffc0));
    ///
    /// // `20` is out of the supported bit range.
    /// assert_eq!(genmask_checked_u16(6..=20), None);
    ///
    /// // Invalid range where the start is bigger than the end.
    /// assert_eq!(genmask_checked_u16(10..=5), None);
    /// ```
    ,
    /// # Examples
    ///
    /// ```
    /// # use kernel::bits::genmask_u16;
    /// assert_eq!(genmask_u16(6..=15), 0xffc0);
    /// assert_eq!(genmask_u16(0..=0), 0b1);
    /// assert_eq!(genmask_u16(0..=15), u16::MAX);
    /// ```
);

impl_genmask_fn!(
    u8,
    /// # Examples
    ///
    /// ```
    /// # #![expect(clippy::reversed_empty_ranges)]
    /// # use kernel::bits::genmask_checked_u8;
    /// assert_eq!(genmask_checked_u8(0..=0), Some(0b1));
    /// assert_eq!(genmask_checked_u8(0..=7), Some(u8::MAX));
    /// assert_eq!(genmask_checked_u8(6..=7), Some(0xc0));
    ///
    /// // `10` is out of the supported bit range.
    /// assert_eq!(genmask_checked_u8(6..=10), None);
    ///
    /// // Invalid range where the start is bigger than the end.
    /// assert_eq!(genmask_checked_u8(5..=2), None);
    /// ```
    ,
    /// # Examples
    ///
    /// ```
    /// # use kernel::bits::genmask_u8;
    /// assert_eq!(genmask_u8(6..=7), 0xc0);
    /// assert_eq!(genmask_u8(0..=0), 0b1);
    /// assert_eq!(genmask_u8(0..=7), u8::MAX);
    /// ```
);
