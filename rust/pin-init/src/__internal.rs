// SPDX-License-Identifier: Apache-2.0 OR MIT

//! This module contains library internal items.
//!
//! These items must not be used outside of this crate and the pin-init-internal crate located at
//! `../internal`.

use core::marker::PhantomPinned;
use core::ops::Deref;

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
/// `pin-init` relies on the correctness of the helper functions defined on `PinData`.
/// Thus, only the `#[pin_data]` can implement this trait.
#[diagnostic::on_unimplemented(
    message = "`{Self}` cannot be used with `pin_init!` macro",
    note = "did you forget to add `#[pin_data]` attribute to the struct?"
)]
pub unsafe trait HasPinData {
    type PinData;

    fn __pin_data(_: InitData<Self>) -> Self::PinData;
}

/// This trait is automatically implemented for every type.
///
/// It aims to provide type inference help; `PATH::__init_data()` would be able to retrieve an
/// instance of `InitData<PATH<Generics>>` without having to mention the generics explicitly.
pub trait HasInitData {
    #[inline]
    fn __init_data() -> InitData<Self> {
        InitData(PhantomInvariant::new())
    }
}

impl<T: ?Sized> HasInitData for T {}

pub struct InitData<T: ?Sized>(PhantomInvariant<T>);

impl<T: ?Sized> Clone for InitData<T> {
    #[inline]
    fn clone(&self) -> Self {
        *self
    }
}

impl<T: ?Sized> Copy for InitData<T> {}

impl<T: ?Sized> InitData<T> {
    /// Type inference helper function.
    #[inline(always)]
    pub fn __make_closure<F, E>(self, f: F) -> F
    where
        F: FnOnce(*mut T) -> Result<InitOk, E>,
    {
        f
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
        unsafe { init.__init(this.value.as_mut_ptr())? };
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
        unsafe { init.__init(self.ptr)? };

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
    #[inline]
    pub fn new() -> Self {
        Self { _t: PhantomData }
    }
}

impl<T: ?Sized> Default for AlwaysFail<T> {
    #[inline]
    fn default() -> Self {
        Self::new()
    }
}

// SAFETY: `__init` always fails, which is always okay.
unsafe impl<T: ?Sized> PinInit<T, ()> for AlwaysFail<T> {
    #[inline]
    unsafe fn __init(self, _slot: *mut T) -> Result<(), ()> {
        Err(())
    }
}
/// Polyfill of `FnOnce` trait to be able to reference output via associated type.
pub trait FnOutput<Args> {
    type Output;
}

macro_rules! impl_fn_output {
    () => {};
    ($ret:ident, $($arg:ident,)*) => {
        impl<This, $ret, $($arg,)*> FnOutput<($($arg,)*)> for This
        where
            This: FnOnce($($arg,)*) -> $ret,
        {
            type Output = $ret;
        }
        impl_fn_output!($($arg,)*);
    };
}

impl_fn_output!(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U,);

/// Lifetime erasure facility.
///
/// Say we have `exists<'a, 'b> Foo<'a, 'b>` and we want to store it. There's no concrete
/// lifetimes we can use, so we want to erase the lifetime.
///
/// Such erasure can be encoded as
/// `Erased<for<'a> fn(&'a ()) -> for<'b> fn(&'b()) -> (Foo<'a, 'b>,)`.
///
/// This can be considered the stable version of Rust's `unsafe_binder` feature, without the
/// no-drop-glue requirement.
#[repr(transparent)]
#[allow(private_bounds)]
pub struct Erase<F: EraseLt>(F::Erased);

/// Helper trait to resolve the erased lifetime.
trait EraseLt {
    type Erased;
}

impl<T> EraseLt for (T,) {
    type Erased = T;
}

impl<T> EraseLt for T
where
    T: for<'a> FnOutput<(&'a (),), Output: EraseLt>,
{
    type Erased = <<T as FnOutput<(&'static (),)>>::Output as EraseLt>::Erased;
}

// The default `Send` and `Sync` are not sufficient, because one can use lifetime specialization to
// implement `Send` or `Sync` for a concrete instance of lifetime. Use HRTB to ensure that the type
// will only implement `Send` or `Sync` if it's implemented for *all* erased lifetimes.

// SAFETY: Trivial, no lifetime to erase.
unsafe impl<T: Send> Send for Erase<(T,)> {}

// SAFETY: If we erased a lifetime, then the type needs to be `Send` for across *all* that
// lifetimes.
unsafe impl<F: EraseLt> Send for Erase<F>
where
    F: for<'a> FnOutput<(&'a (),), Output: EraseLt>,
    for<'a> Erase<<F as FnOutput<(&'a (),)>>::Output>: Send,
{
}

// SAFETY: Trivial, no lifetime to erase.
unsafe impl<T: Sync> Sync for Erase<(T,)> {}

// SAFETY: If we erased a lifetime, then the type needs to be `Sync` for across *all* that
// lifetimes.
unsafe impl<F: EraseLt> Sync for Erase<F>
where
    F: for<'a> FnOutput<(&'a (),), Output: EraseLt>,
    for<'a> Erase<<F as FnOutput<(&'a (),)>>::Output>: Sync,
{
}

/// Wrapper for borrowed fields.
///
/// This should be switched to `UnsafePinned` when it is stable.
/// NOTE: This type needs to be covariant; Rust's 1.89+'s `UnsafePinned` is invariant.
#[repr(transparent)]
pub struct Borrowed<T: ?Sized>(PhantomPinned, T);

// Lifetimes not needed by drop glue are considered by Rust's drop check to be considered
// `#[may_dangle]`. In case for a self-referential struct, we may have fields which need lifetime of
// borrowed fields in their drop glue, so compiler's automatic check is insufficient.
//
// Code like this:
// ```
// #[pin_data]
// struct SelfRef<'a> {
//     borrow: PrintOnDrop<&'owner str>,
//     owner: &'a str,
// }
// ```
// may access `owner` during the drop, however Rust will determine that since `'a` only is used in
// `owner`, the `'a` lifetime may dangle during drop.
//
// This is undesirable for pin-init self references, because `&'a str` could be coerecd to
// `&'owner str` and this could further coerce if there're implied outlives, e.g.
// `&'earlier_field &'owner ()` would allow `&'owner str` to further coerce to `&'earlier_field`.
//
// Thus, if any self-referential field require field lifetime access in `Drop` impl, we would need
// to ensure that the all generic parameters visible by self-referential fields would strictly
// outlive the struct. And this can be done by a simple `Drop` impl that does nothing. Without a
// dropck eye patch, presence of `Drop` impl, albeit empty, tells the drop check that the strict
// outlive relation is needed.
impl<T: ?Sized> Drop for Borrowed<T> {
    #[inline(always)]
    fn drop(&mut self) {}
}

impl<T: ?Sized> Deref for Borrowed<T> {
    type Target = T;

    #[inline(always)]
    fn deref(&self) -> &T {
        &self.1
    }
}
