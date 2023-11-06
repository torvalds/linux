// SPDX-License-Identifier: Apache-2.0 OR MIT

use core::num::{Saturating, Wrapping};

use crate::boxed::Box;

#[rustc_specialization_trait]
pub(super) unsafe trait IsZero {
    /// Whether this value's representation is all zeros,
    /// or can be represented with all zeroes.
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

impl_is_zero!(i8, |x| x == 0); // It is needed to impl for arrays and tuples of i8.
impl_is_zero!(i16, |x| x == 0);
impl_is_zero!(i32, |x| x == 0);
impl_is_zero!(i64, |x| x == 0);
impl_is_zero!(i128, |x| x == 0);
impl_is_zero!(isize, |x| x == 0);

impl_is_zero!(u8, |x| x == 0); // It is needed to impl for arrays and tuples of u8.
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
        // it's worth doing if the array is really long. The threshold here
        // is largely arbitrary, but was picked because as of 2022-07-01 LLVM
        // fails to const-fold the check in `vec![[1; 32]; n]`
        // See https://github.com/rust-lang/rust/pull/97581#issuecomment-1166628022
        // Feel free to tweak if you have better evidence.

        N <= 16 && self.iter().all(IsZero::is_zero)
    }
}

// This is recursive macro.
macro_rules! impl_for_tuples {
    // Stopper
    () => {
        // No use for implementing for empty tuple because it is ZST.
    };
    ($first_arg:ident $(,$rest:ident)*) => {
        unsafe impl <$first_arg: IsZero, $($rest: IsZero,)*> IsZero for ($first_arg, $($rest,)*){
            #[inline]
            fn is_zero(&self) -> bool{
                // Destructure tuple to N references
                // Rust allows to hide generic params by local variable names.
                #[allow(non_snake_case)]
                let ($first_arg, $($rest,)*) = self;

                $first_arg.is_zero()
                    $( && $rest.is_zero() )*
            }
        }

        impl_for_tuples!($($rest),*);
    }
}

impl_for_tuples!(A, B, C, D, E, F, G, H);

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

macro_rules! impl_is_zero_option_of_num {
    ($($t:ty,)+) => {$(
        unsafe impl IsZero for Option<$t> {
            #[inline]
            fn is_zero(&self) -> bool {
                const {
                    let none: Self = unsafe { core::mem::MaybeUninit::zeroed().assume_init() };
                    assert!(none.is_none());
                }
                self.is_none()
            }
        }
    )+};
}

impl_is_zero_option_of_num!(u8, u16, u32, u64, u128, i8, i16, i32, i64, i128, usize, isize,);

unsafe impl<T: IsZero> IsZero for Wrapping<T> {
    #[inline]
    fn is_zero(&self) -> bool {
        self.0.is_zero()
    }
}

unsafe impl<T: IsZero> IsZero for Saturating<T> {
    #[inline]
    fn is_zero(&self) -> bool {
        self.0.is_zero()
    }
}

macro_rules! impl_for_optional_bool {
    ($($t:ty,)+) => {$(
        unsafe impl IsZero for $t {
            #[inline]
            fn is_zero(&self) -> bool {
                // SAFETY: This is *not* a stable layout guarantee, but
                // inside `core` we're allowed to rely on the current rustc
                // behaviour that options of bools will be one byte with
                // no padding, so long as they're nested less than 254 deep.
                let raw: u8 = unsafe { core::mem::transmute(*self) };
                raw == 0
            }
        }
    )+};
}
impl_for_optional_bool! {
    Option<bool>,
    Option<Option<bool>>,
    Option<Option<Option<bool>>>,
    // Could go further, but not worth the metadata overhead
}
