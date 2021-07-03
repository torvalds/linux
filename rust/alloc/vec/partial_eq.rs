// SPDX-License-Identifier: Apache-2.0 OR MIT

use crate::alloc::Allocator;
#[cfg(not(no_global_oom_handling))]
use crate::borrow::Cow;

use super::Vec;

macro_rules! __impl_slice_eq1 {
    ([$($vars:tt)*] $lhs:ty, $rhs:ty $(where $ty:ty: $bound:ident)?, #[$stability:meta]) => {
        #[$stability]
        impl<T, U, $($vars)*> PartialEq<$rhs> for $lhs
        where
            T: PartialEq<U>,
            $($ty: $bound)?
        {
            #[inline]
            fn eq(&self, other: &$rhs) -> bool { self[..] == other[..] }
            #[inline]
            fn ne(&self, other: &$rhs) -> bool { self[..] != other[..] }
        }
    }
}

__impl_slice_eq1! { [A1: Allocator, A2: Allocator] Vec<T, A1>, Vec<U, A2>, #[stable(feature = "rust1", since = "1.0.0")] }
__impl_slice_eq1! { [A: Allocator] Vec<T, A>, &[U], #[stable(feature = "rust1", since = "1.0.0")] }
__impl_slice_eq1! { [A: Allocator] Vec<T, A>, &mut [U], #[stable(feature = "rust1", since = "1.0.0")] }
__impl_slice_eq1! { [A: Allocator] &[T], Vec<U, A>, #[stable(feature = "partialeq_vec_for_ref_slice", since = "1.46.0")] }
__impl_slice_eq1! { [A: Allocator] &mut [T], Vec<U, A>, #[stable(feature = "partialeq_vec_for_ref_slice", since = "1.46.0")] }
__impl_slice_eq1! { [A: Allocator] Vec<T, A>, [U], #[stable(feature = "partialeq_vec_for_slice", since = "1.48.0")]  }
__impl_slice_eq1! { [A: Allocator] [T], Vec<U, A>, #[stable(feature = "partialeq_vec_for_slice", since = "1.48.0")]  }
#[cfg(not(no_global_oom_handling))]
__impl_slice_eq1! { [A: Allocator] Cow<'_, [T]>, Vec<U, A> where T: Clone, #[stable(feature = "rust1", since = "1.0.0")] }
#[cfg(not(no_global_oom_handling))]
__impl_slice_eq1! { [] Cow<'_, [T]>, &[U] where T: Clone, #[stable(feature = "rust1", since = "1.0.0")] }
#[cfg(not(no_global_oom_handling))]
__impl_slice_eq1! { [] Cow<'_, [T]>, &mut [U] where T: Clone, #[stable(feature = "rust1", since = "1.0.0")] }
__impl_slice_eq1! { [A: Allocator, const N: usize] Vec<T, A>, [U; N], #[stable(feature = "rust1", since = "1.0.0")] }
__impl_slice_eq1! { [A: Allocator, const N: usize] Vec<T, A>, &[U; N], #[stable(feature = "rust1", since = "1.0.0")] }

// NOTE: some less important impls are omitted to reduce code bloat
// FIXME(Centril): Reconsider this?
//__impl_slice_eq1! { [const N: usize] Vec<A>, &mut [B; N], }
//__impl_slice_eq1! { [const N: usize] [A; N], Vec<B>, }
//__impl_slice_eq1! { [const N: usize] &[A; N], Vec<B>, }
//__impl_slice_eq1! { [const N: usize] &mut [A; N], Vec<B>, }
//__impl_slice_eq1! { [const N: usize] Cow<'a, [A]>, [B; N], }
//__impl_slice_eq1! { [const N: usize] Cow<'a, [A]>, &[B; N], }
//__impl_slice_eq1! { [const N: usize] Cow<'a, [A]>, &mut [B; N], }
