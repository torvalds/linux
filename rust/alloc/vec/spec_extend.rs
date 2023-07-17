// SPDX-License-Identifier: Apache-2.0 OR MIT

use crate::alloc::Allocator;
use crate::collections::TryReserveError;
use core::iter::TrustedLen;
use core::slice::{self};

use super::{IntoIter, Vec};

// Specialization trait used for Vec::extend
#[cfg(not(no_global_oom_handling))]
pub(super) trait SpecExtend<T, I> {
    fn spec_extend(&mut self, iter: I);
}

// Specialization trait used for Vec::try_extend
pub(super) trait TrySpecExtend<T, I> {
    fn try_spec_extend(&mut self, iter: I) -> Result<(), TryReserveError>;
}

#[cfg(not(no_global_oom_handling))]
impl<T, I, A: Allocator> SpecExtend<T, I> for Vec<T, A>
where
    I: Iterator<Item = T>,
{
    default fn spec_extend(&mut self, iter: I) {
        self.extend_desugared(iter)
    }
}

impl<T, I, A: Allocator> TrySpecExtend<T, I> for Vec<T, A>
where
    I: Iterator<Item = T>,
{
    default fn try_spec_extend(&mut self, iter: I) -> Result<(), TryReserveError> {
        self.try_extend_desugared(iter)
    }
}

#[cfg(not(no_global_oom_handling))]
impl<T, I, A: Allocator> SpecExtend<T, I> for Vec<T, A>
where
    I: TrustedLen<Item = T>,
{
    default fn spec_extend(&mut self, iterator: I) {
        self.extend_trusted(iterator)
    }
}

impl<T, I, A: Allocator> TrySpecExtend<T, I> for Vec<T, A>
where
    I: TrustedLen<Item = T>,
{
    default fn try_spec_extend(&mut self, iterator: I) -> Result<(), TryReserveError> {
        self.try_extend_trusted(iterator)
    }
}

#[cfg(not(no_global_oom_handling))]
impl<T, A: Allocator> SpecExtend<T, IntoIter<T>> for Vec<T, A> {
    fn spec_extend(&mut self, mut iterator: IntoIter<T>) {
        unsafe {
            self.append_elements(iterator.as_slice() as _);
        }
        iterator.forget_remaining_elements();
    }
}

impl<T, A: Allocator> TrySpecExtend<T, IntoIter<T>> for Vec<T, A> {
    fn try_spec_extend(&mut self, mut iterator: IntoIter<T>) -> Result<(), TryReserveError> {
        unsafe {
            self.try_append_elements(iterator.as_slice() as _)?;
        }
        iterator.forget_remaining_elements();
        Ok(())
    }
}

#[cfg(not(no_global_oom_handling))]
impl<'a, T: 'a, I, A: Allocator + 'a> SpecExtend<&'a T, I> for Vec<T, A>
where
    I: Iterator<Item = &'a T>,
    T: Clone,
{
    default fn spec_extend(&mut self, iterator: I) {
        self.spec_extend(iterator.cloned())
    }
}

impl<'a, T: 'a, I, A: Allocator + 'a> TrySpecExtend<&'a T, I> for Vec<T, A>
where
    I: Iterator<Item = &'a T>,
    T: Clone,
{
    default fn try_spec_extend(&mut self, iterator: I) -> Result<(), TryReserveError> {
        self.try_spec_extend(iterator.cloned())
    }
}

#[cfg(not(no_global_oom_handling))]
impl<'a, T: 'a, A: Allocator + 'a> SpecExtend<&'a T, slice::Iter<'a, T>> for Vec<T, A>
where
    T: Copy,
{
    fn spec_extend(&mut self, iterator: slice::Iter<'a, T>) {
        let slice = iterator.as_slice();
        unsafe { self.append_elements(slice) };
    }
}

impl<'a, T: 'a, A: Allocator + 'a> TrySpecExtend<&'a T, slice::Iter<'a, T>> for Vec<T, A>
where
    T: Copy,
{
    fn try_spec_extend(&mut self, iterator: slice::Iter<'a, T>) -> Result<(), TryReserveError> {
        let slice = iterator.as_slice();
        unsafe { self.try_append_elements(slice) }
    }
}
