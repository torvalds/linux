// SPDX-License-Identifier: GPL-2.0

//! Integer parsing functions.
//!
//! Integer parsing functions for parsing signed and unsigned integers
//! potentially prefixed with `0x`, `0o`, or `0b`.

use crate::prelude::*;
use crate::str::BStr;
use core::ops::Deref;

// Make `FromStrRadix` a public type with a private name. This seals
// `ParseInt`, that is, prevents downstream users from implementing the
// trait.
mod private {
    use crate::prelude::*;
    use crate::str::BStr;

    /// Trait that allows parsing a [`&BStr`] to an integer with a radix.
    pub trait FromStrRadix: Sized {
        /// Parse `src` to [`Self`] using radix `radix`.
        fn from_str_radix(src: &BStr, radix: u32) -> Result<Self>;

        /// Tries to convert `value` into [`Self`] and negates the resulting value.
        fn from_u64_negated(value: u64) -> Result<Self>;
    }
}

/// Extract the radix from an integer literal optionally prefixed with
/// one of `0x`, `0X`, `0o`, `0O`, `0b`, `0B`, `0`.
fn strip_radix(src: &BStr) -> (u32, &BStr) {
    match src.deref() {
        [b'0', b'x' | b'X', rest @ ..] => (16, rest.as_ref()),
        [b'0', b'o' | b'O', rest @ ..] => (8, rest.as_ref()),
        [b'0', b'b' | b'B', rest @ ..] => (2, rest.as_ref()),
        // NOTE: We are including the leading zero to be able to parse
        // literal `0` here. If we removed it as a radix prefix, we would
        // not be able to parse `0`.
        [b'0', ..] => (8, src),
        _ => (10, src),
    }
}

/// Trait for parsing string representations of integers.
///
/// Strings beginning with `0x`, `0o`, or `0b` are parsed as hex, octal, or
/// binary respectively. Strings beginning with `0` otherwise are parsed as
/// octal. Anything else is parsed as decimal. A leading `+` or `-` is also
/// permitted. Any string parsed by [`kstrtol()`] or [`kstrtoul()`] will be
/// successfully parsed.
///
/// [`kstrtol()`]: https://docs.kernel.org/core-api/kernel-api.html#c.kstrtol
/// [`kstrtoul()`]: https://docs.kernel.org/core-api/kernel-api.html#c.kstrtoul
///
/// # Examples
///
/// ```
/// # use kernel::str::parse_int::ParseInt;
/// # use kernel::b_str;
///
/// assert_eq!(Ok(0u8), u8::from_str(b_str!("0")));
///
/// assert_eq!(Ok(0xa2u8), u8::from_str(b_str!("0xa2")));
/// assert_eq!(Ok(-0xa2i32), i32::from_str(b_str!("-0xa2")));
///
/// assert_eq!(Ok(-0o57i8), i8::from_str(b_str!("-0o57")));
/// assert_eq!(Ok(0o57i8), i8::from_str(b_str!("057")));
///
/// assert_eq!(Ok(0b1001i16), i16::from_str(b_str!("0b1001")));
/// assert_eq!(Ok(-0b1001i16), i16::from_str(b_str!("-0b1001")));
///
/// assert_eq!(Ok(127i8), i8::from_str(b_str!("127")));
/// assert!(i8::from_str(b_str!("128")).is_err());
/// assert_eq!(Ok(-128i8), i8::from_str(b_str!("-128")));
/// assert!(i8::from_str(b_str!("-129")).is_err());
/// assert_eq!(Ok(255u8), u8::from_str(b_str!("255")));
/// assert!(u8::from_str(b_str!("256")).is_err());
/// ```
pub trait ParseInt: private::FromStrRadix + TryFrom<u64> {
    /// Parse a string according to the description in [`Self`].
    fn from_str(src: &BStr) -> Result<Self> {
        match src.deref() {
            [b'-', rest @ ..] => {
                let (radix, digits) = strip_radix(rest.as_ref());
                // 2's complement values range from -2^(b-1) to 2^(b-1)-1.
                // So if we want to parse negative numbers as positive and
                // later multiply by -1, we have to parse into a larger
                // integer. We choose `u64` as sufficiently large.
                //
                // NOTE: 128 bit integers are not available on all
                // platforms, hence the choice of 64 bits.
                let val =
                    u64::from_str_radix(core::str::from_utf8(digits).map_err(|_| EINVAL)?, radix)
                        .map_err(|_| EINVAL)?;
                Self::from_u64_negated(val)
            }
            _ => {
                let (radix, digits) = strip_radix(src);
                Self::from_str_radix(digits, radix).map_err(|_| EINVAL)
            }
        }
    }
}

macro_rules! impl_parse_int {
    ($($ty:ty),*) => {
        $(
            impl private::FromStrRadix for $ty {
                fn from_str_radix(src: &BStr, radix: u32) -> Result<Self> {
                    <$ty>::from_str_radix(core::str::from_utf8(src).map_err(|_| EINVAL)?, radix)
                        .map_err(|_| EINVAL)
                }

                fn from_u64_negated(value: u64) -> Result<Self> {
                    const ABS_MIN: u64 = {
                        #[allow(unused_comparisons)]
                        if <$ty>::MIN < 0 {
                            1u64 << (<$ty>::BITS - 1)
                        } else {
                            0
                        }
                    };

                    if value > ABS_MIN {
                        return Err(EINVAL);
                    }

                    if value == ABS_MIN {
                        return Ok(<$ty>::MIN);
                    }

                    // SAFETY: The above checks guarantee that `value` fits into `Self`:
                    // - if `Self` is unsigned, then `ABS_MIN == 0` and thus we have returned above
                    //   (either `EINVAL` or `MIN`).
                    // - if `Self` is signed, then we have that `0 <= value < ABS_MIN`. And since
                    //   `ABS_MIN - 1` fits into `Self` by construction, `value` also does.
                    let value: Self = unsafe { value.try_into().unwrap_unchecked() };

                    Ok((!value).wrapping_add(1))
                }
            }

            impl ParseInt for $ty {}
        )*
    };
}

impl_parse_int![i8, u8, i16, u16, i32, u32, i64, u64, isize, usize];
