// SPDX-License-Identifier: Apache-2.0 OR MIT

//! This module contains library internal items.
//!
//! These items must not be used outside of this crate and the pin-init-internal crate located at
//! `../internal`.

use super::*;

/// Zero-sized type used to mark a type as invariant.
///
/// This is a polyfill for the [unstable type] in the standard library of the same name.
///
/// See the [nomicon] for what subtyping is. See also [this table].
///
/// [unstable type]: https://doc.rust-lang.org/nightly/std/marker/struct.PhantomInvariant.html
/// [nomicon]: https://doc.rust-lang.org/nomicon/subtyping.html
/// [this table]: https://doc.rust-lang.org/nomicon/phantom-data.html#table-of-phantomdata-patterns
#[repr(transparent)]
pub struct PhantomInvariant<T: ?Sized>(PhantomData<fn(T) -> T>);

impl<T: ?Sized> Clone for PhantomInvariant<T> {
    #[inline(always)]
    fn clone(&self) -> Self {
        *self
    }
}

impl<T: ?Sized> Copy for PhantomInvariant<T> {}

impl<T: ?Sized> Default for PhantomInvariant<T> {
    #[inline(always)]
    fn default() -> Self {
        Self::new()
    }
}

impl<T: ?Sized> PhantomInvariant<T> {
    #[inline(always)]
    pub const fn new() -> Self {
        Self(PhantomData)
    }
}

/// Zero-sized type used to mark a lifetime as invariant.
///
/// This is a polyfill for the [unstable type] in the standard library of the same name.
///
/// [unstable type]: https://doc.rust-lang.org/nightly/std/marker/struct.PhantomInvariantLifetime.html
#[repr(transparent)]
#[derive(Clone, Copy, Default)]
pub struct PhantomInvariantLifetime<'a>(PhantomInvariant<&'a ()>);

impl PhantomInvariantLifetime<'_> {
    #[inline(always)]
    pub const fn new() -> Self {
        Self(PhantomInvariant::new())
    }
}

/// Token type to signify successful initialization.
///
/// Can only be constructed via the unsafe [`Self::new`] function. The initializer macros use this
/// token type to prevent returning `Ok` from an initializer without initializing all fields.
pub struct InitOk(());

impl InitOk {
    /// Creates a new token.
    ///
    /// # Safety
    ///
    /// This function may only be called from the `init!` macro in `../internal/src/init.rs`.
    #[inline(always)]
    pub unsafe fn new() -> Self {
        Self(())
    }
}

/// This trait is only implemented via the `#[pin_data]` proc-macro. It is used to facilitate
/// the pin projections within the initializers.
///
/// # Safety
///
/// Only the `init` module is allowed to use this trait.
pub unsafe trait HasPinData {
    type PinData;

    #[expect(clippy::missing_safety_doc)]
    unsafe fn __pin_data() -> Self::PinData;
}

/// This trait is automatically implemented for every type. It aims to provide the same type
/// inference help as `HasPinData`.
///
/// # Safety
///
/// Only the `init` module is allowed to use this trait.
pub unsafe trait HasInitData {
    type InitData;

    #[expect(clippy::missing_safety_doc)]
    unsafe fn __init_data() -> Self::InitData;
}

pub struct AllData<T: ?Sized>(PhantomInvariant<T>);

impl<T: ?Sized> Clone for AllData<T> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<T: ?Sized> Copy for AllData<T> {}

impl<T: ?Sized> AllData<T> {
    /// Type inference helper function.
    #[inline(always)]
    pub fn __make_closure<F, E>(self, f: F) -> F
    where
        F: FnOnce(*mut T) -> Result<InitOk, E>,
    {
        f
    }
}

// SAFETY: TODO.
unsafe impl<T: ?Sized> HasInitData for T {
    type InitData = AllData<T>;

    unsafe fn __init_data() -> Self::InitData {
        AllData(PhantomInvariant::new())
    }
}

/// Stack initializer helper type. Use [`stack_pin_init`] instead of this primitive.
///
/// # Invariants
///
/// If `self.is_init` is true, then `self.value` is initialized.
///
/// [`stack_pin_init`]: crate::stack_pin_init
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
    /// [`stack_pin_init`]: crate::stack_pin_init
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

#[test]
#[cfg(feature = "std")]
fn stack_init_reuse() {
    use ::std::{borrow::ToOwned, println, string::String};
    use core::pin::pin;

    #[derive(Debug)]
    struct Foo {
        a: usize,
        b: String,
    }
    let mut slot: Pin<&mut StackInit<Foo>> = pin!(StackInit::uninit());
    let value: Result<Pin<&mut Foo>, core::convert::Infallible> =
        slot.as_mut().init(crate::init!(Foo {
            a: 42,
            b: "Hello".to_owned(),
        }));
    let value = value.unwrap();
    println!("{value:?}");
    let value: Result<Pin<&mut Foo>, core::convert::Infallible> =
        slot.as_mut().init(crate::init!(Foo {
            a: 24,
            b: "world!".to_owned(),
        }));
    let value = value.unwrap();
    println!("{value:?}");
}

// Marker types that determines type of `DropGuard`'s let bindings.
pub struct Pinned;
pub struct Unpinned;

/// Represent an uninitialized field.
///
/// # Invariants
///
/// - `ptr` is valid, properly aligned and points to uninitialized and exclusively accessed memory.
/// - If `P` is `Pinned`, then `ptr` is structurally pinned.
pub struct Slot<P, T: ?Sized> {
    ptr: *mut T,
    _phantom: PhantomData<P>,
}

impl<P, T: ?Sized> Slot<P, T> {
    /// # Safety
    ///
    /// - `ptr` is valid, properly aligned and points to uninitialized and exclusively accessed
    ///   memory.
    /// - If `P` is `Pinned`, then `ptr` is structurally pinned.
    #[inline(always)]
    pub unsafe fn new(ptr: *mut T) -> Self {
        // INVARIANT: Per safety requirement.
        Self {
            ptr,
            _phantom: PhantomData,
        }
    }

    /// Initialize the field by value.
    #[inline(always)]
    pub fn write(self, value: T) -> DropGuard<P, T>
    where
        T: Sized,
    {
        // SAFETY: `self.ptr` is a valid and aligned pointer for write.
        unsafe { self.ptr.write(value) }
        // SAFETY:
        // - `self.ptr` is valid and properly aligned per type invariant.
        // - `*self.ptr` is initialized above and the ownership is transferred to the guard.
        // - If `P` is `Pinned`, `self.ptr` is pinned.
        unsafe { DropGuard::new(self.ptr) }
    }
}

impl<T: ?Sized> Slot<Unpinned, T> {
    /// Initialize the field.
    #[inline(always)]
    pub fn init<E>(self, init: impl Init<T, E>) -> Result<DropGuard<Unpinned, T>, E> {
        // SAFETY:
        // - `self.ptr` is valid and properly aligned.
        // - when `Err` is returned, we also propagate the error without touching `slot`;
        //   also `self` is consumed so it cannot be touched further.
        unsafe { init.__init(self.ptr)? };

        // SAFETY:
        // - `self.ptr` is valid and properly aligned per type invariant.
        // - `*self.ptr` is initialized above and the ownership is transferred to the guard.
        Ok(unsafe { DropGuard::new(self.ptr) })
    }
}

impl<T: ?Sized> Slot<Pinned, T> {
    /// Initialize the field.
    #[inline(always)]
    pub fn init<E>(self, init: impl PinInit<T, E>) -> Result<DropGuard<Pinned, T>, E> {
        // SAFETY:
        // - `self.ptr` is valid and properly aligned.
        // - when `Err` is returned, we also propagate the error without touching `ptr`;
        //   also `self` is consumed so it cannot be touched further.
        // - the drop guard will not hand out `&mut` (only `Pin<&mut T>`).
        unsafe { init.__pinned_init(self.ptr)? };

        // SAFETY:
        // - `self.ptr` is valid, properly aligned and pinned per type invariant.
        // - `*self.ptr` is initialized above and the ownership is transferred to the guard.
        Ok(unsafe { DropGuard::new(self.ptr) })
    }
}

/// When a value of this type is dropped, it drops a `T`.
///
/// Can be forgotten to prevent the drop.
///
/// # Invariants
///
/// - `ptr` is valid and properly aligned.
/// - `*ptr` is initialized and owned by this guard.
/// - if `P` is `Pinned`, `ptr` is pinned.
pub struct DropGuard<P, T: ?Sized> {
    ptr: *mut T,
    phantom: PhantomData<P>,
}

impl<P, T: ?Sized> DropGuard<P, T> {
    /// Creates a drop guard and transfer the ownership of the pointer content.
    ///
    /// The ownership is only relinguished if the guard is forgotten via [`core::mem::forget`].
    ///
    /// # Safety
    ///
    /// - `ptr` is valid and properly aligned.
    /// - `*ptr` is initialized, and the ownership is transferred to this guard.
    /// - if `P` is `Pinned`, `ptr` is pinned.
    #[inline]
    pub unsafe fn new(ptr: *mut T) -> Self {
        // INVARIANT: By safety requirement.
        Self {
            ptr,
            phantom: PhantomData,
        }
    }
}

impl<T: ?Sized> DropGuard<Unpinned, T> {
    /// Create a let binding for accessor use.
    #[inline]
    pub fn let_binding(&mut self) -> &mut T {
        // SAFETY: Per type invariant.
        unsafe { &mut *self.ptr }
    }
}

impl<T: ?Sized> DropGuard<Pinned, T> {
    /// Create a let binding for accessor use.
    #[inline]
    pub fn let_binding(&mut self) -> Pin<&mut T> {
        // SAFETY: `self.ptr` is valid, properly aligned, initialized, exclusively accessible and
        // pinned per type invariant.
        unsafe { Pin::new_unchecked(&mut *self.ptr) }
    }
}

impl<P, T: ?Sized> Drop for DropGuard<P, T> {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: `self.ptr` is valid, properly aligned and `*self.ptr` is owned by this guard.
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
