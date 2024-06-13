// SPDX-License-Identifier: Apache-2.0 OR MIT

use crate::alloc::{Allocator, Global};
use core::ptr::{self};
use core::slice::{self};

use super::Vec;

/// An iterator which uses a closure to determine if an element should be removed.
///
/// This struct is created by [`Vec::drain_filter`].
/// See its documentation for more.
///
/// # Example
///
/// ```
/// #![feature(drain_filter)]
///
/// let mut v = vec![0, 1, 2];
/// let iter: std::vec::DrainFilter<_, _> = v.drain_filter(|x| *x % 2 == 0);
/// ```
#[unstable(feature = "drain_filter", reason = "recently added", issue = "43244")]
#[derive(Debug)]
pub struct DrainFilter<
    'a,
    T,
    F,
    #[unstable(feature = "allocator_api", issue = "32838")] A: Allocator = Global,
> where
    F: FnMut(&mut T) -> bool,
{
    pub(super) vec: &'a mut Vec<T, A>,
    /// The index of the item that will be inspected by the next call to `next`.
    pub(super) idx: usize,
    /// The number of items that have been drained (removed) thus far.
    pub(super) del: usize,
    /// The original length of `vec` prior to draining.
    pub(super) old_len: usize,
    /// The filter test predicate.
    pub(super) pred: F,
    /// A flag that indicates a panic has occurred in the filter test predicate.
    /// This is used as a hint in the drop implementation to prevent consumption
    /// of the remainder of the `DrainFilter`. Any unprocessed items will be
    /// backshifted in the `vec`, but no further items will be dropped or
    /// tested by the filter predicate.
    pub(super) panic_flag: bool,
}

impl<T, F, A: Allocator> DrainFilter<'_, T, F, A>
where
    F: FnMut(&mut T) -> bool,
{
    /// Returns a reference to the underlying allocator.
    #[unstable(feature = "allocator_api", issue = "32838")]
    #[inline]
    pub fn allocator(&self) -> &A {
        self.vec.allocator()
    }
}

#[unstable(feature = "drain_filter", reason = "recently added", issue = "43244")]
impl<T, F, A: Allocator> Iterator for DrainFilter<'_, T, F, A>
where
    F: FnMut(&mut T) -> bool,
{
    type Item = T;

    fn next(&mut self) -> Option<T> {
        unsafe {
            while self.idx < self.old_len {
                let i = self.idx;
                let v = slice::from_raw_parts_mut(self.vec.as_mut_ptr(), self.old_len);
                self.panic_flag = true;
                let drained = (self.pred)(&mut v[i]);
                self.panic_flag = false;
                // Update the index *after* the predicate is called. If the index
                // is updated prior and the predicate panics, the element at this
                // index would be leaked.
                self.idx += 1;
                if drained {
                    self.del += 1;
                    return Some(ptr::read(&v[i]));
                } else if self.del > 0 {
                    let del = self.del;
                    let src: *const T = &v[i];
                    let dst: *mut T = &mut v[i - del];
                    ptr::copy_nonoverlapping(src, dst, 1);
                }
            }
            None
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (0, Some(self.old_len - self.idx))
    }
}

#[unstable(feature = "drain_filter", reason = "recently added", issue = "43244")]
impl<T, F, A: Allocator> Drop for DrainFilter<'_, T, F, A>
where
    F: FnMut(&mut T) -> bool,
{
    fn drop(&mut self) {
        struct BackshiftOnDrop<'a, 'b, T, F, A: Allocator>
        where
            F: FnMut(&mut T) -> bool,
        {
            drain: &'b mut DrainFilter<'a, T, F, A>,
        }

        impl<'a, 'b, T, F, A: Allocator> Drop for BackshiftOnDrop<'a, 'b, T, F, A>
        where
            F: FnMut(&mut T) -> bool,
        {
            fn drop(&mut self) {
                unsafe {
                    if self.drain.idx < self.drain.old_len && self.drain.del > 0 {
                        // This is a pretty messed up state, and there isn't really an
                        // obviously right thing to do. We don't want to keep trying
                        // to execute `pred`, so we just backshift all the unprocessed
                        // elements and tell the vec that they still exist. The backshift
                        // is required to prevent a double-drop of the last successfully
                        // drained item prior to a panic in the predicate.
                        let ptr = self.drain.vec.as_mut_ptr();
                        let src = ptr.add(self.drain.idx);
                        let dst = src.sub(self.drain.del);
                        let tail_len = self.drain.old_len - self.drain.idx;
                        src.copy_to(dst, tail_len);
                    }
                    self.drain.vec.set_len(self.drain.old_len - self.drain.del);
                }
            }
        }

        let backshift = BackshiftOnDrop { drain: self };

        // Attempt to consume any remaining elements if the filter predicate
        // has not yet panicked. We'll backshift any remaining elements
        // whether we've already panicked or if the consumption here panics.
        if !backshift.drain.panic_flag {
            backshift.drain.for_each(drop);
        }
    }
}
