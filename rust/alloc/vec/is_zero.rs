// SPDX-License-Identifier: Apache-2.0 OR MIT

use crate::boxed::Box;

#[rustc_specialization_trait]
pub(super) unsafe trait IsZero {
    /// Whether this value's representation is all zeros
    fn is_zero(&self) -> bool;
}

macro_rules! impl_is_zero {
    ($t:ty, $is_zero:expr) => {
        unsafe impl IsZero for $t {
            #[inline]
            fn is_zero(&self) -> bool {
                $is_zero(*self)
            }
        }
    };
}

impl_is_zero!(i16, |x| x == 0);
impl_is_zero!(i32, |x| x == 0);
impl_is_zero!(i64, |x| x == 0);
impl_is_zero!(i128, |x| x == 0);
impl_is_zero!(isize, |x| x == 0);

impl_is_zero!(u16, |x| x == 0);
impl_is_zero!(u32, |x| x == 0);
impl_is_zero!(u64, |x| x == 0);
impl_is_zero!(u128, |x| x == 0);
impl_is_zero!(usize, |x| x == 0);

impl_is_zero!(bool, |x| x == false);
impl_is_zero!(char, |x| x == '\0');

impl_is_zero!(f32, |x: f32| x.to_bits() == 0);
impl_is_zero!(f64, |x: f64| x.to_bits() == 0);

unsafe impl<T> IsZero for *const T {
    #[inline]
    fn is_zero(&self) -> bool {
        (*self).is_null()
    }
}

unsafe impl<T> IsZero for *mut T {
    #[inline]
    fn is_zero(&self) -> bool {
        (*self).is_null()
    }
}

unsafe impl<T: IsZero, const N: usize> IsZero for [T; N] {
    #[inline]
    fn is_zero(&self) -> bool {
        // Because this is generated as a runtime check, it's not obvious that
        // it's worth doing if the array is really long.  The threshold here
        // is largely arbitrary, but was picked because as of 2022-05-01 LLVM
        // can const-fold the check in `vec![[0; 32]; n]` but not in
        // `vec![[0; 64]; n]`: https://godbolt.org/z/WTzjzfs5b
        // Feel free to tweak if you have better evidence.

        N <= 32 && self.iter().all(IsZero::is_zero)
    }
}

// `Option<&T>` and `Option<Box<T>>` are guaranteed to represent `None` as null.
// For fat pointers, the bytes that would be the pointer metadata in the `Some`
// variant are padding in the `None` variant, so ignoring them and
// zero-initializing instead is ok.
// `Option<&mut T>` never implements `Clone`, so there's no need for an impl of
// `SpecFromElem`.

unsafe impl<T: ?Sized> IsZero for Option<&T> {
    #[inline]
    fn is_zero(&self) -> bool {
        self.is_none()
    }
}

unsafe impl<T: ?Sized> IsZero for Option<Box<T>> {
    #[inline]
    fn is_zero(&self) -> bool {
        self.is_none()
    }
}

// `Option<num::NonZeroU32>` and similar have a representation guarantee that
// they're the same size as the corresponding `u32` type, as well as a guarantee
// that transmuting between `NonZeroU32` and `Option<num::NonZeroU32>` works.
// While the documentation officially makes it UB to transmute from `None`,
// we're the standard library so we can make extra inferences, and we know that
// the only niche available to represent `None` is the one that's all zeros.

macro_rules! impl_is_zero_option_of_nonzero {
    ($($t:ident,)+) => {$(
        unsafe impl IsZero for Option<core::num::$t> {
            #[inline]
            fn is_zero(&self) -> bool {
                self.is_none()
            }
        }
    )+};
}

impl_is_zero_option_of_nonzero!(
    NonZeroU8,
    NonZeroU16,
    NonZeroU32,
    NonZeroU64,
    NonZeroU128,
    NonZeroI8,
    NonZeroI16,
    NonZeroI32,
    NonZeroI64,
    NonZeroI128,
    NonZeroUsize,
    NonZeroIsize,
);
