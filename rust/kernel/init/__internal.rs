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
pub(super) type Invariant<T> = PhantomData<fn(*mut T) -> *mut T>;

/// Module-internal type implementing `PinInit` and `Init`.
///
/// It is unsafe to create this type, since the closure needs to fulfill the same safety
/// requirement as the `__pinned_init`/`__init` functions.
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

// SAFETY: While constructing the `InitClosure`, the user promised that it upholds the
// `__pinned_init` invariants.
unsafe impl<T: ?Sized, F, E> PinInit<T, E> for InitClosure<F, T, E>
where
    F: FnOnce(*mut T) -> Result<(), E>,
{
    #[inline]
    unsafe fn __pinned_init(self, slot: *mut T) -> Result<(), E> {
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

    #[expect(clippy::missing_safety_doc)]
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

    #[expect(clippy::missing_safety_doc)]
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

// SAFETY: TODO.
unsafe impl<T: ?Sized> InitData for AllData<T> {
    type Datee = T;
}

// SAFETY: TODO.
unsafe impl<T: ?Sized> HasInitData for T {
    type InitData = AllData<T>;

    unsafe fn __init_data() -> Self::InitData {
        AllData(PhantomData)
    }
}

/// Stack initializer helper type. Use [`stack_pin_init`] instead of this primitive.
///
/// # Invariants
///
/// If `self.is_init` is true, then `self.value` is initialized.
///
/// [`stack_pin_init`]: kernel::stack_pin_init
pub struct StackInit<T> {
    value: MaybeUninit<T>,
    is_init: bool,
}

impl<T> Drop for StackInit<T> {
    #[inline]
    fn drop(&mut self) {
        if self.is_init {
            // SAFETY: As we are being dropped, we only call this once. And since `self.is_init` is
            // true, `self.value` is initialized.
            unsafe { self.value.assume_init_drop() };
        }
    }
}

impl<T> StackInit<T> {
    /// Creates a new [`StackInit<T>`] that is uninitialized. Use [`stack_pin_init`] instead of this
    /// primitive.
    ///
    /// [`stack_pin_init`]: kernel::stack_pin_init
    #[inline]
    pub fn uninit() -> Self {
        Self {
            value: MaybeUninit::uninit(),
            is_init: false,
        }
    }

    /// Initializes the contents and returns the result.
    #[inline]
    pub fn init<E>(self: Pin<&mut Self>, init: impl PinInit<T, E>) -> Result<Pin<&mut T>, E> {
        // SAFETY: We never move out of `this`.
        let this = unsafe { Pin::into_inner_unchecked(self) };
        // The value is currently initialized, so it needs to be dropped before we can reuse
        // the memory (this is a safety guarantee of `Pin`).
        if this.is_init {
            this.is_init = false;
            // SAFETY: `this.is_init` was true and therefore `this.value` is initialized.
            unsafe { this.value.assume_init_drop() };
        }
        // SAFETY: The memory slot is valid and this type ensures that it will stay pinned.
        unsafe { init.__pinned_init(this.value.as_mut_ptr())? };
        // INVARIANT: `this.value` is initialized above.
        this.is_init = true;
        // SAFETY: The slot is now pinned, since we will never give access to `&mut T`.
        Ok(unsafe { Pin::new_unchecked(this.value.assume_init_mut()) })
    }
}

/// When a value of this type is dropped, it drops a `T`.
///
/// Can be forgotten to prevent the drop.
pub struct DropGuard<T: ?Sized> {
    ptr: *mut T,
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
        Self { ptr }
    }
}

impl<T: ?Sized> Drop for DropGuard<T> {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: A `DropGuard` can only be constructed using the unsafe `new` function
        // ensuring that this operation is safe.
        unsafe { ptr::drop_in_place(self.ptr) }
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

/// Initializer that always fails.
///
/// Used by [`assert_pinned!`].
///
/// [`assert_pinned!`]: crate::assert_pinned
pub struct AlwaysFail<T: ?Sized> {
    _t: PhantomData<T>,
}

impl<T: ?Sized> AlwaysFail<T> {
    /// Creates a new initializer that always fails.
    pub fn new() -> Self {
        Self { _t: PhantomData }
    }
}

impl<T: ?Sized> Default for AlwaysFail<T> {
    fn default() -> Self {
        Self::new()
    }
}

// SAFETY: `__pinned_init` always fails, which is always okay.
unsafe impl<T: ?Sized> PinInit<T, ()> for AlwaysFail<T> {
    unsafe fn __pinned_init(self, _slot: *mut T) -> Result<(), ()> {
        Err(())
    }
}
