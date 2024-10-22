// SPDX-License-Identifier: GPL-2.0

//! Extensions to [`Box`] for fallible allocations.

use super::{AllocError, Flags};
use alloc::boxed::Box;
use core::mem::MaybeUninit;

/// Extensions to [`Box`].
pub trait BoxExt<T>: Sized {
    /// Allocates a new box.
    ///
    /// The allocation may fail, in which case an error is returned.
    fn new(x: T, flags: Flags) -> Result<Self, AllocError>;

    /// Allocates a new uninitialised box.
    ///
    /// The allocation may fail, in which case an error is returned.
    fn new_uninit(flags: Flags) -> Result<Box<MaybeUninit<T>>, AllocError>;
}

impl<T> BoxExt<T> for Box<T> {
    fn new(x: T, flags: Flags) -> Result<Self, AllocError> {
        let mut b = <Self as BoxExt<_>>::new_uninit(flags)?;
        b.write(x);
        // SAFETY: We just wrote to it.
        Ok(unsafe { b.assume_init() })
    }

    #[cfg(any(test, testlib))]
    fn new_uninit(_flags: Flags) -> Result<Box<MaybeUninit<T>>, AllocError> {
        Ok(Box::new_uninit())
    }

    #[cfg(not(any(test, testlib)))]
    fn new_uninit(flags: Flags) -> Result<Box<MaybeUninit<T>>, AllocError> {
        let ptr = if core::mem::size_of::<MaybeUninit<T>>() == 0 {
            core::ptr::NonNull::<_>::dangling().as_ptr()
        } else {
            let layout = core::alloc::Layout::new::<MaybeUninit<T>>();

            // SAFETY: Memory is being allocated (first arg is null). The only other source of
            // safety issues is sleeping on atomic context, which is addressed by klint. Lastly,
            // the type is not a SZT (checked above).
            let ptr =
                unsafe { super::allocator::krealloc_aligned(core::ptr::null_mut(), layout, flags) };
            if ptr.is_null() {
                return Err(AllocError);
            }

            ptr.cast::<MaybeUninit<T>>()
        };

        // SAFETY: For non-zero-sized types, we allocate above using the global allocator. For
        // zero-sized types, we use `NonNull::dangling`.
        Ok(unsafe { Box::from_raw(ptr) })
    }
}
