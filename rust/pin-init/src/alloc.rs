// SPDX-License-Identifier: Apache-2.0 OR MIT

#[cfg(all(feature = "alloc", not(feature = "std")))]
use alloc::{boxed::Box, sync::Arc};
#[cfg(feature = "alloc")]
use core::alloc::AllocError;
use core::{mem::MaybeUninit, pin::Pin};
#[cfg(feature = "std")]
use std::sync::Arc;

#[cfg(not(feature = "alloc"))]
type AllocError = core::convert::Infallible;

use crate::{
    init_from_closure, pin_init_from_closure, InPlaceWrite, Init, PinInit, ZeroableOption,
};

pub extern crate alloc;

// SAFETY: All zeros is equivalent to `None` (option layout optimization guarantee:
// <https://doc.rust-lang.org/stable/std/option/index.html#representation>).
unsafe impl<T> ZeroableOption for Box<T> {}

/// Smart pointer that can initialize memory in-place.
pub trait InPlaceInit<T>: Sized {
    /// Use the given pin-initializer to pin-initialize a `T` inside of a new smart pointer of this
    /// type.
    ///
    /// If `T: !Unpin` it will not be able to move afterwards.
    fn try_pin_init<E>(init: impl PinInit<T, E>) -> Result<Pin<Self>, E>
    where
        E: From<AllocError>;

    /// Use the given pin-initializer to pin-initialize a `T` inside of a new smart pointer of this
    /// type.
    ///
    /// If `T: !Unpin` it will not be able to move afterwards.
    fn pin_init(init: impl PinInit<T>) -> Result<Pin<Self>, AllocError> {
        // SAFETY: We delegate to `init` and only change the error type.
        let init = unsafe {
            pin_init_from_closure(|slot| match init.__pinned_init(slot) {
                Ok(()) => Ok(()),
                Err(i) => match i {},
            })
        };
        Self::try_pin_init(init)
    }

    /// Use the given initializer to in-place initialize a `T`.
    fn try_init<E>(init: impl Init<T, E>) -> Result<Self, E>
    where
        E: From<AllocError>;

    /// Use the given initializer to in-place initialize a `T`.
    fn init(init: impl Init<T>) -> Result<Self, AllocError> {
        // SAFETY: We delegate to `init` and only change the error type.
        let init = unsafe {
            init_from_closure(|slot| match init.__init(slot) {
                Ok(()) => Ok(()),
                Err(i) => match i {},
            })
        };
        Self::try_init(init)
    }
}

#[cfg(feature = "alloc")]
macro_rules! try_new_uninit {
    ($type:ident) => {
        $type::try_new_uninit()?
    };
}
#[cfg(all(feature = "std", not(feature = "alloc")))]
macro_rules! try_new_uninit {
    ($type:ident) => {
        $type::new_uninit()
    };
}

impl<T> InPlaceInit<T> for Box<T> {
    #[inline]
    fn try_pin_init<E>(init: impl PinInit<T, E>) -> Result<Pin<Self>, E>
    where
        E: From<AllocError>,
    {
        try_new_uninit!(Box).write_pin_init(init)
    }

    #[inline]
    fn try_init<E>(init: impl Init<T, E>) -> Result<Self, E>
    where
        E: From<AllocError>,
    {
        try_new_uninit!(Box).write_init(init)
    }
}

impl<T> InPlaceInit<T> for Arc<T> {
    #[inline]
    fn try_pin_init<E>(init: impl PinInit<T, E>) -> Result<Pin<Self>, E>
    where
        E: From<AllocError>,
    {
        let mut this = try_new_uninit!(Arc);
        let Some(slot) = Arc::get_mut(&mut this) else {
            // SAFETY: the Arc has just been created and has no external references
            unsafe { core::hint::unreachable_unchecked() }
        };
        let slot = slot.as_mut_ptr();
        // SAFETY: When init errors/panics, slot will get deallocated but not dropped,
        // slot is valid and will not be moved, because we pin it later.
        unsafe { init.__pinned_init(slot)? };
        // SAFETY: All fields have been initialized and this is the only `Arc` to that data.
        Ok(unsafe { Pin::new_unchecked(this.assume_init()) })
    }

    #[inline]
    fn try_init<E>(init: impl Init<T, E>) -> Result<Self, E>
    where
        E: From<AllocError>,
    {
        let mut this = try_new_uninit!(Arc);
        let Some(slot) = Arc::get_mut(&mut this) else {
            // SAFETY: the Arc has just been created and has no external references
            unsafe { core::hint::unreachable_unchecked() }
        };
        let slot = slot.as_mut_ptr();
        // SAFETY: When init errors/panics, slot will get deallocated but not dropped,
        // slot is valid.
        unsafe { init.__init(slot)? };
        // SAFETY: All fields have been initialized.
        Ok(unsafe { this.assume_init() })
    }
}

impl<T> InPlaceWrite<T> for Box<MaybeUninit<T>> {
    type Initialized = Box<T>;

    fn write_init<E>(mut self, init: impl Init<T, E>) -> Result<Self::Initialized, E> {
        let slot = self.as_mut_ptr();
        // SAFETY: When init errors/panics, slot will get deallocated but not dropped,
        // slot is valid.
        unsafe { init.__init(slot)? };
        // SAFETY: All fields have been initialized.
        Ok(unsafe { self.assume_init() })
    }

    fn write_pin_init<E>(mut self, init: impl PinInit<T, E>) -> Result<Pin<Self::Initialized>, E> {
        let slot = self.as_mut_ptr();
        // SAFETY: When init errors/panics, slot will get deallocated but not dropped,
        // slot is valid and will not be moved, because we pin it later.
        unsafe { init.__pinned_init(slot)? };
        // SAFETY: All fields have been initialized.
        Ok(unsafe { self.assume_init() }.into())
    }
}
