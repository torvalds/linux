// SPDX-License-Identifier: GPL-2.0

//! Types and functions to work with pointers and addresses.

use core::fmt::Debug;
use core::mem::align_of;
use core::num::NonZero;

use crate::build_assert;

/// Type representing an alignment, which is always a power of two.
///
/// It is used to validate that a given value is a valid alignment, and to perform masking and
/// alignment operations.
///
/// This is a temporary substitute for the [`Alignment`] nightly type from the standard library,
/// and to be eventually replaced by it.
///
/// [`Alignment`]: https://github.com/rust-lang/rust/issues/102070
///
/// # Invariants
///
/// An alignment is always a power of two.
#[repr(transparent)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Alignment(NonZero<usize>);

impl Alignment {
    /// Validates that `ALIGN` is a power of two at build-time, and returns an [`Alignment`] of the
    /// same value.
    ///
    /// A build error is triggered if `ALIGN` is not a power of two.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::ptr::Alignment;
    ///
    /// let v = Alignment::new::<16>();
    /// assert_eq!(v.as_usize(), 16);
    /// ```
    #[inline(always)]
    pub const fn new<const ALIGN: usize>() -> Self {
        build_assert!(
            ALIGN.is_power_of_two(),
            "Provided alignment is not a power of two."
        );

        // INVARIANT: `align` is a power of two.
        // SAFETY: `align` is a power of two, and thus non-zero.
        Self(unsafe { NonZero::new_unchecked(ALIGN) })
    }

    /// Validates that `align` is a power of two at runtime, and returns an
    /// [`Alignment`] of the same value.
    ///
    /// Returns [`None`] if `align` is not a power of two.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::ptr::Alignment;
    ///
    /// assert_eq!(Alignment::new_checked(16), Some(Alignment::new::<16>()));
    /// assert_eq!(Alignment::new_checked(15), None);
    /// assert_eq!(Alignment::new_checked(1), Some(Alignment::new::<1>()));
    /// assert_eq!(Alignment::new_checked(0), None);
    /// ```
    #[inline(always)]
    pub const fn new_checked(align: usize) -> Option<Self> {
        if align.is_power_of_two() {
            // INVARIANT: `align` is a power of two.
            // SAFETY: `align` is a power of two, and thus non-zero.
            Some(Self(unsafe { NonZero::new_unchecked(align) }))
        } else {
            None
        }
    }

    /// Returns the alignment of `T`.
    ///
    /// This is equivalent to [`align_of`], but with the return value provided as an [`Alignment`].
    #[inline(always)]
    pub const fn of<T>() -> Self {
        #![allow(clippy::incompatible_msrv)]
        // This cannot panic since alignments are always powers of two.
        //
        // We unfortunately cannot use `new` as it would require the `generic_const_exprs` feature.
        const { Alignment::new_checked(align_of::<T>()).unwrap() }
    }

    /// Returns this alignment as a [`usize`].
    ///
    /// It is guaranteed to be a power of two.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::ptr::Alignment;
    ///
    /// assert_eq!(Alignment::new::<16>().as_usize(), 16);
    /// ```
    #[inline(always)]
    pub const fn as_usize(self) -> usize {
        self.as_nonzero().get()
    }

    /// Returns this alignment as a [`NonZero`].
    ///
    /// It is guaranteed to be a power of two.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::ptr::Alignment;
    ///
    /// assert_eq!(Alignment::new::<16>().as_nonzero().get(), 16);
    /// ```
    #[inline(always)]
    pub const fn as_nonzero(self) -> NonZero<usize> {
        // Allow the compiler to know that the value is indeed a power of two. This can help
        // optimize some operations down the line, like e.g. replacing divisions by bit shifts.
        if !self.0.is_power_of_two() {
            // SAFETY: Per the invariants, `self.0` is always a power of two so this block will
            // never be reached.
            unsafe { core::hint::unreachable_unchecked() }
        }
        self.0
    }

    /// Returns the base-2 logarithm of the alignment.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::ptr::Alignment;
    ///
    /// assert_eq!(Alignment::of::<u8>().log2(), 0);
    /// assert_eq!(Alignment::new::<16>().log2(), 4);
    /// ```
    #[inline(always)]
    pub const fn log2(self) -> u32 {
        self.0.ilog2()
    }

    /// Returns the mask for this alignment.
    ///
    /// This is equivalent to `!(self.as_usize() - 1)`.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::ptr::Alignment;
    ///
    /// assert_eq!(Alignment::new::<0x10>().mask(), !0xf);
    /// ```
    #[inline(always)]
    pub const fn mask(self) -> usize {
        // No underflow can occur as the alignment is guaranteed to be a power of two, and thus is
        // non-zero.
        !(self.as_usize() - 1)
    }
}

/// Trait for items that can be aligned against an [`Alignment`].
pub trait Alignable: Sized {
    /// Aligns `self` down to `alignment`.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::ptr::{Alignable, Alignment};
    ///
    /// assert_eq!(0x2f_usize.align_down(Alignment::new::<0x10>()), 0x20);
    /// assert_eq!(0x30usize.align_down(Alignment::new::<0x10>()), 0x30);
    /// assert_eq!(0xf0u8.align_down(Alignment::new::<0x1000>()), 0x0);
    /// ```
    fn align_down(self, alignment: Alignment) -> Self;

    /// Aligns `self` up to `alignment`, returning `None` if aligning would result in an overflow.
    ///
    /// # Examples
    ///
    /// ```
    /// use kernel::ptr::{Alignable, Alignment};
    ///
    /// assert_eq!(0x4fusize.align_up(Alignment::new::<0x10>()), Some(0x50));
    /// assert_eq!(0x40usize.align_up(Alignment::new::<0x10>()), Some(0x40));
    /// assert_eq!(0x0usize.align_up(Alignment::new::<0x10>()), Some(0x0));
    /// assert_eq!(u8::MAX.align_up(Alignment::new::<0x10>()), None);
    /// assert_eq!(0x10u8.align_up(Alignment::new::<0x100>()), None);
    /// assert_eq!(0x0u8.align_up(Alignment::new::<0x100>()), Some(0x0));
    /// ```
    fn align_up(self, alignment: Alignment) -> Option<Self>;
}

/// Implement [`Alignable`] for unsigned integer types.
macro_rules! impl_alignable_uint {
    ($($t:ty),*) => {
        $(
        impl Alignable for $t {
            #[inline(always)]
            fn align_down(self, alignment: Alignment) -> Self {
                // The operands of `&` need to be of the same type so convert the alignment to
                // `Self`. This means we need to compute the mask ourselves.
                ::core::num::NonZero::<Self>::try_from(alignment.as_nonzero())
                    .map(|align| self & !(align.get() - 1))
                    // An alignment larger than `Self` always aligns down to `0`.
                    .unwrap_or(0)
            }

            #[inline(always)]
            fn align_up(self, alignment: Alignment) -> Option<Self> {
                let aligned_down = self.align_down(alignment);
                if self == aligned_down {
                    Some(aligned_down)
                } else {
                    Self::try_from(alignment.as_usize())
                        .ok()
                        .and_then(|align| aligned_down.checked_add(align))
                }
            }
        }
        )*
    };
}

impl_alignable_uint!(u8, u16, u32, u64, usize);
