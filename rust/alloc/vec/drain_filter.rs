// SPDX-License-Identifier: Apache-2.0 OR MIT

use crate::alloc::{Allocator, Global};
use core::mem::{self, ManuallyDrop};
use core::ptr;
use core::slice;

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

    /// Keep unyielded elements in the source `Vec`.
    ///
    /// # Examples
    ///
    /// ```
    /// #![feature(drain_filter)]
    /// #![feature(drain_keep_rest)]
    ///
    /// let mut vec = vec!['a', 'b', 'c'];
    /// let mut drain = vec.drain_filter(|_| true);
    ///
    /// assert_eq!(drain.next().unwrap(), 'a');
    ///
    /// // This call keeps 'b' and 'c' in the vec.
    /// drain.keep_rest();
    ///
    /// // If we wouldn't call `keep_rest()`,
    /// // `vec` would be empty.
    /// assert_eq!(vec, ['b', 'c']);
    /// ```
    #[unstable(feature = "drain_keep_rest", issue = "101122")]
    pub fn keep_rest(self) {
        // At this moment layout looks like this:
        //
        //  _____________________/-- old_len
        // /                     \
        // [kept] [yielded] [tail]
        //        \_______/ ^-- idx
        //                \-- del
        //
        // Normally `Drop` impl would drop [tail] (via .for_each(drop), ie still calling `pred`)
        //
        // 1. Move [tail] after [kept]
        // 2. Update length of the original vec to `old_len - del`
        //    a. In case of ZST, this is the only thing we want to do
        // 3. Do *not* drop self, as everything is put in a consistent state already, there is nothing to do
        let mut this = ManuallyDrop::new(self);

        unsafe {
            // ZSTs have no identity, so we don't need to move them around.
            let needs_move = mem::size_of::<T>() != 0;

            if needs_move && this.idx < this.old_len && this.del > 0 {
                let ptr = this.vec.as_mut_ptr();
                let src = ptr.add(this.idx);
                let dst = src.sub(this.del);
                let tail_len = this.old_len - this.idx;
                src.copy_to(dst, tail_len);
            }

            let new_len = this.old_len - this.del;
            this.vec.set_len(new_len);
        }
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
