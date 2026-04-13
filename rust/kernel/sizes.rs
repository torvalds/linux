// SPDX-License-Identifier: GPL-2.0

//! Commonly used sizes.
//!
//! C headers: [`include/linux/sizes.h`](srctree/include/linux/sizes.h).
//!
//! The top-level `SZ_*` constants are [`usize`]-typed, for use in kernel page
//! arithmetic and similar CPU-side work.
//!
//! The [`SizeConstants`] trait provides the same constants as associated constants
//! on [`u32`], [`u64`], and [`usize`], for use in device address spaces where
//! the address width depends on the hardware. Device drivers frequently need
//! these constants as [`u64`] (or [`u32`]) rather than [`usize`], because
//! device address spaces are sized independently of the CPU pointer width.
//!
//! # Examples
//!
//! ```
//! use kernel::{
//!     page::PAGE_SIZE,
//!     sizes::{
//!         SizeConstants,
//!         SZ_1M, //
//!     }, //
//! };
//!
//! // Module-level constants continue to work without a type qualifier.
//! let num_pages_in_1m = SZ_1M / PAGE_SIZE;
//!
//! // Trait associated constants require a type qualifier.
//! let heap_size = 14 * u64::SZ_1M;
//! let small = u32::SZ_4K;
//! ```

macro_rules! define_sizes {
    ($($type:ty),* $(,)?) => {
        define_sizes!(@internal [$($type),*]
            /// `0x0000_0400`.
            SZ_1K,
            /// `0x0000_0800`.
            SZ_2K,
            /// `0x0000_1000`.
            SZ_4K,
            /// `0x0000_2000`.
            SZ_8K,
            /// `0x0000_4000`.
            SZ_16K,
            /// `0x0000_8000`.
            SZ_32K,
            /// `0x0001_0000`.
            SZ_64K,
            /// `0x0002_0000`.
            SZ_128K,
            /// `0x0004_0000`.
            SZ_256K,
            /// `0x0008_0000`.
            SZ_512K,
            /// `0x0010_0000`.
            SZ_1M,
            /// `0x0020_0000`.
            SZ_2M,
            /// `0x0040_0000`.
            SZ_4M,
            /// `0x0080_0000`.
            SZ_8M,
            /// `0x0100_0000`.
            SZ_16M,
            /// `0x0200_0000`.
            SZ_32M,
            /// `0x0400_0000`.
            SZ_64M,
            /// `0x0800_0000`.
            SZ_128M,
            /// `0x1000_0000`.
            SZ_256M,
            /// `0x2000_0000`.
            SZ_512M,
            /// `0x4000_0000`.
            SZ_1G,
            /// `0x8000_0000`.
            SZ_2G,
        );
    };

    (@internal [$($type:ty),*] $($names_and_metas:tt)*) => {
        define_sizes!(@consts_and_trait $($names_and_metas)*);
        define_sizes!(@impls [$($type),*] $($names_and_metas)*);
    };

    (@consts_and_trait $($(#[$meta:meta])* $name:ident,)*) => {
        $(
            $(#[$meta])*
            pub const $name: usize = bindings::$name as usize;
        )*

        /// Size constants for device address spaces.
        ///
        /// Implemented for [`u32`], [`u64`], and [`usize`] so drivers can
        /// choose the width that matches their hardware. All `SZ_*` values fit
        /// in a [`u32`], so all implementations are lossless.
        ///
        /// # Examples
        ///
        /// ```
        /// use kernel::sizes::SizeConstants;
        ///
        /// let gpu_heap = 14 * u64::SZ_1M;
        /// let mmio_window = u32::SZ_16M;
        /// ```
        pub trait SizeConstants {
            $(
                $(#[$meta])*
                const $name: Self;
            )*
        }
    };

    (@impls [] $($(#[$meta:meta])* $name:ident,)*) => {};

    (@impls [$first:ty $(, $rest:ty)*] $($(#[$meta:meta])* $name:ident,)*) => {
        impl SizeConstants for $first {
            $(
                const $name: Self = {
                    assert!((self::$name as u128) <= (<$first>::MAX as u128));
                    self::$name as $first
                };
            )*
        }

        define_sizes!(@impls [$($rest),*] $($(#[$meta])* $name,)*);
    };
}

define_sizes!(u32, u64, usize);
