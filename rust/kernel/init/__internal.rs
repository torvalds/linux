// SPDX-License-Identifier: Apache-2.0 OR MIT

//! This module contains API-internal items for pin-init.
//!
//! These items must not be used outside of
//! - `kernel/init.rs`
//! - `macros/pin_data.rs`
//! - `macros/pinned_drop.rs`

use super::*;

/// See the [nomicon] for what subtyping is. See also [this table].
///
/// [nomicon]: https://doc.rust-lang.org/nomicon/subtyping.html
/// [this table]: https://doc.rust-lang.org/nomicon/phantom-data.html#table-of-phantomdata-patterns
type Invariant<T> = PhantomData<fn(*mut T) -> *mut T>;

/// This is the module-internal type implementing `PinInit` and `Init`. It is unsafe to create this
/// type, since the closure needs to fulfill the same safety requirement as the
/// `__pinned_init`/`__init` functions.
pub(crate) struct InitClosure<F, T: ?Sized, E>(pub(crate) F, pub(crate) Invariant<(E, T)>);

// SAFETY: While constructing the `InitClosure`, the user promised that it upholds the
// `__init` invariants.
unsafe impl<T: ?Sized, F, E> Init<T, E> for InitClosure<F, T, E>
where
    F: FnOnce(*mut T) -> Result<(), E>,
{
    #[inline]
    unsafe fn __init(self, slot: *mut T) -> Result<(), E> {
        (self.0)(slot)
    }
}

/// This trait is only implemented via the `#[pin_data]` proc-macro. It is used to facilitate
/// the pin projections within the initializers.
///
/// # Safety
///
/// Only the `init` module is allowed to use this trait.
pub unsafe trait HasPinData {
    type PinData: PinData;

    unsafe fn __pin_data() -> Self::PinData;
}

/// Marker trait for pinning data of structs.
///
/// # Safety
///
/// Only the `init` module is allowed to use this trait.
pub unsafe trait PinData: Copy {
    type Datee: ?Sized + HasPinData;

    /// Type inference helper function.
    fn make_closure<F, O, E>(self, f: F) -> F
    where
        F: FnOnce(*mut Self::Datee) -> Result<O, E>,
    {
        f
    }
}

/// This trait is automatically implemented for every type. It aims to provide the same type
/// inference help as `HasPinData`.
///
/// # Safety
///
/// Only the `init` module is allowed to use this trait.
pub unsafe trait HasInitData {
    type InitData: InitData;

    unsafe fn __init_data() -> Self::InitData;
}

/// Same function as `PinData`, but for arbitrary data.
///
/// # Safety
///
/// Only the `init` module is allowed to use this trait.
pub unsafe trait InitData: Copy {
    type Datee: ?Sized + HasInitData;

    /// Type inference helper function.
    fn make_closure<F, O, E>(self, f: F) -> F
    where
        F: FnOnce(*mut Self::Datee) -> Result<O, E>,
    {
        f
    }
}

pub struct AllData<T: ?Sized>(PhantomData<fn(Box<T>) -> Box<T>>);

impl<T: ?Sized> Clone for AllData<T> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<T: ?Sized> Copy for AllData<T> {}

unsafe impl<T: ?Sized> InitData for AllData<T> {
    type Datee = T;
}

unsafe impl<T: ?Sized> HasInitData for T {
    type InitData = AllData<T>;

    unsafe fn __init_data() -> Self::InitData {
        AllData(PhantomData)
    }
}

/// When a value of this type is dropped, it drops a `T`.
///
/// Can be forgotten to prevent the drop.
pub struct DropGuard<T: ?Sized> {
    ptr: *mut T,
    do_drop: Cell<bool>,
}

impl<T: ?Sized> DropGuard<T> {
    /// Creates a new [`DropGuard<T>`]. It will [`ptr::drop_in_place`] `ptr` when it gets dropped.
    ///
    /// # Safety
    ///
    /// `ptr` must be a valid pointer.
    ///
    /// It is the callers responsibility that `self` will only get dropped if the pointee of `ptr`:
    /// - has not been dropped,
    /// - is not accessible by any other means,
    /// - will not be dropped by any other means.
    #[inline]
    pub unsafe fn new(ptr: *mut T) -> Self {
        Self {
            ptr,
            do_drop: Cell::new(true),
        }
    }

    /// Prevents this guard from dropping the supplied pointer.
    ///
    /// # Safety
    ///
    /// This function is unsafe in order to prevent safe code from forgetting this guard. It should
    /// only be called by the macros in this module.
    #[inline]
    pub unsafe fn forget(&self) {
        self.do_drop.set(false);
    }
}

impl<T: ?Sized> Drop for DropGuard<T> {
    #[inline]
    fn drop(&mut self) {
        if self.do_drop.get() {
            // SAFETY: A `DropGuard` can only be constructed using the unsafe `new` function
            // ensuring that this operation is safe.
            unsafe { ptr::drop_in_place(self.ptr) }
        }
    }
}

/// Token used by `PinnedDrop` to prevent calling the function without creating this unsafely
/// created struct. This is needed, because the `drop` function is safe, but should not be called
/// manually.
pub struct OnlyCallFromDrop(());

impl OnlyCallFromDrop {
    /// # Safety
    ///
    /// This function should only be called from the [`Drop::drop`] function and only be used to
    /// delegate the destruction to the pinned destructor [`PinnedDrop::drop`] of the same type.
    pub unsafe fn new() -> Self {
        Self(())
    }
}
