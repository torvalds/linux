// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! A wrapper around `Arc` for linked lists.

use crate::alloc::{AllocError, Flags};
use crate::prelude::*;
use crate::sync::{Arc, ArcBorrow, UniqueArc};
use core::marker::PhantomPinned;
use core::ops::Deref;
use core::pin::Pin;
use core::sync::atomic::{AtomicBool, Ordering};

/// Declares that this type has some way to ensure that there is exactly one `ListArc` instance for
/// this id.
///
/// Types that implement this trait should include some kind of logic for keeping track of whether
/// a [`ListArc`] exists or not. We refer to this logic as "the tracking inside `T`".
///
/// We allow the case where the tracking inside `T` thinks that a [`ListArc`] exists, but actually,
/// there isn't a [`ListArc`]. However, we do not allow the opposite situation where a [`ListArc`]
/// exists, but the tracking thinks it doesn't. This is because the former can at most result in us
/// failing to create a [`ListArc`] when the operation could succeed, whereas the latter can result
/// in the creation of two [`ListArc`] references. Only the latter situation can lead to memory
/// safety issues.
///
/// A consequence of the above is that you may implement the tracking inside `T` by not actually
/// keeping track of anything. To do this, you always claim that a [`ListArc`] exists, even if
/// there isn't one. This implementation is allowed by the above rule, but it means that
/// [`ListArc`] references can only be created if you have ownership of *all* references to the
/// refcounted object, as you otherwise have no way of knowing whether a [`ListArc`] exists.
pub trait ListArcSafe<const ID: u64 = 0> {
    /// Informs the tracking inside this type that it now has a [`ListArc`] reference.
    ///
    /// This method may be called even if the tracking inside this type thinks that a `ListArc`
    /// reference exists. (But only if that's not actually the case.)
    ///
    /// # Safety
    ///
    /// Must not be called if a [`ListArc`] already exist for this value.
    unsafe fn on_create_list_arc_from_unique(self: Pin<&mut Self>);

    /// Informs the tracking inside this type that there is no [`ListArc`] reference anymore.
    ///
    /// # Safety
    ///
    /// Must only be called if there is no [`ListArc`] reference, but the tracking thinks there is.
    unsafe fn on_drop_list_arc(&self);
}

/// Declares that this type is able to safely attempt to create `ListArc`s at any time.
///
/// # Safety
///
/// The guarantees of `try_new_list_arc` must be upheld.
pub unsafe trait TryNewListArc<const ID: u64 = 0>: ListArcSafe<ID> {
    /// Attempts to convert an `Arc<Self>` into an `ListArc<Self>`. Returns `true` if the
    /// conversion was successful.
    ///
    /// This method should not be called directly. Use [`ListArc::try_from_arc`] instead.
    ///
    /// # Guarantees
    ///
    /// If this call returns `true`, then there is no [`ListArc`] pointing to this value.
    /// Additionally, this call will have transitioned the tracking inside `Self` from not thinking
    /// that a [`ListArc`] exists, to thinking that a [`ListArc`] exists.
    fn try_new_list_arc(&self) -> bool;
}

/// Declares that this type supports [`ListArc`].
///
/// This macro supports a few different strategies for implementing the tracking inside the type:
///
/// * The `untracked` strategy does not actually keep track of whether a [`ListArc`] exists. When
///   using this strategy, the only way to create a [`ListArc`] is using a [`UniqueArc`].
/// * The `tracked_by` strategy defers the tracking to a field of the struct. The user must specify
///   which field to defer the tracking to. The field must implement [`ListArcSafe`]. If the field
///   implements [`TryNewListArc`], then the type will also implement [`TryNewListArc`].
///
/// The `tracked_by` strategy is usually used by deferring to a field of type
/// [`AtomicTracker`]. However, it is also possible to defer the tracking to another struct
/// using also using this macro.
#[macro_export]
macro_rules! impl_list_arc_safe {
    (impl$({$($generics:tt)*})? ListArcSafe<$num:tt> for $t:ty { untracked; } $($rest:tt)*) => {
        impl$(<$($generics)*>)? $crate::list::ListArcSafe<$num> for $t {
            unsafe fn on_create_list_arc_from_unique(self: ::core::pin::Pin<&mut Self>) {}
            unsafe fn on_drop_list_arc(&self) {}
        }
        $crate::list::impl_list_arc_safe! { $($rest)* }
    };

    (impl$({$($generics:tt)*})? ListArcSafe<$num:tt> for $t:ty {
        tracked_by $field:ident : $fty:ty;
    } $($rest:tt)*) => {
        impl$(<$($generics)*>)? $crate::list::ListArcSafe<$num> for $t {
            unsafe fn on_create_list_arc_from_unique(self: ::core::pin::Pin<&mut Self>) {
                ::pin_init::assert_pinned!($t, $field, $fty, inline);

                // SAFETY: This field is structurally pinned as per the above assertion.
                let field = unsafe {
                    ::core::pin::Pin::map_unchecked_mut(self, |me| &mut me.$field)
                };
                // SAFETY: The caller promises that there is no `ListArc`.
                unsafe {
                    <$fty as $crate::list::ListArcSafe<$num>>::on_create_list_arc_from_unique(field)
                };
            }
            unsafe fn on_drop_list_arc(&self) {
                // SAFETY: The caller promises that there is no `ListArc` reference, and also
                // promises that the tracking thinks there is a `ListArc` reference.
                unsafe { <$fty as $crate::list::ListArcSafe<$num>>::on_drop_list_arc(&self.$field) };
            }
        }
        unsafe impl$(<$($generics)*>)? $crate::list::TryNewListArc<$num> for $t
        where
            $fty: TryNewListArc<$num>,
        {
            fn try_new_list_arc(&self) -> bool {
                <$fty as $crate::list::TryNewListArc<$num>>::try_new_list_arc(&self.$field)
            }
        }
        $crate::list::impl_list_arc_safe! { $($rest)* }
    };

    () => {};
}
pub use impl_list_arc_safe;

/// A wrapper around [`Arc`] that's guaranteed unique for the given id.
///
/// The `ListArc` type can be thought of as a special reference to a refcounted object that owns the
/// permission to manipulate the `next`/`prev` pointers stored in the refcounted object. By ensuring
/// that each object has only one `ListArc` reference, the owner of that reference is assured
/// exclusive access to the `next`/`prev` pointers. When a `ListArc` is inserted into a [`List`],
/// the [`List`] takes ownership of the `ListArc` reference.
///
/// There are various strategies to ensuring that a value has only one `ListArc` reference. The
/// simplest is to convert a [`UniqueArc`] into a `ListArc`. However, the refcounted object could
/// also keep track of whether a `ListArc` exists using a boolean, which could allow for the
/// creation of new `ListArc` references from an [`Arc`] reference. Whatever strategy is used, the
/// relevant tracking is referred to as "the tracking inside `T`", and the [`ListArcSafe`] trait
/// (and its subtraits) are used to update the tracking when a `ListArc` is created or destroyed.
///
/// Note that we allow the case where the tracking inside `T` thinks that a `ListArc` exists, but
/// actually, there isn't a `ListArc`. However, we do not allow the opposite situation where a
/// `ListArc` exists, but the tracking thinks it doesn't. This is because the former can at most
/// result in us failing to create a `ListArc` when the operation could succeed, whereas the latter
/// can result in the creation of two `ListArc` references.
///
/// While this `ListArc` is unique for the given id, there still might exist normal `Arc`
/// references to the object.
///
/// # Invariants
///
/// * Each reference counted object has at most one `ListArc` for each value of `ID`.
/// * The tracking inside `T` is aware that a `ListArc` reference exists.
///
/// [`List`]: crate::list::List
#[repr(transparent)]
#[cfg_attr(CONFIG_RUSTC_HAS_COERCE_POINTEE, derive(core::marker::CoercePointee))]
pub struct ListArc<T, const ID: u64 = 0>
where
    T: ListArcSafe<ID> + ?Sized,
{
    arc: Arc<T>,
}

impl<T: ListArcSafe<ID>, const ID: u64> ListArc<T, ID> {
    /// Constructs a new reference counted instance of `T`.
    #[inline]
    pub fn new(contents: T, flags: Flags) -> Result<Self, AllocError> {
        Ok(Self::from(UniqueArc::new(contents, flags)?))
    }

    /// Use the given initializer to in-place initialize a `T`.
    ///
    /// If `T: !Unpin` it will not be able to move afterwards.
    // We don't implement `InPlaceInit` because `ListArc` is implicitly pinned. This is similar to
    // what we do for `Arc`.
    #[inline]
    pub fn pin_init<E>(init: impl PinInit<T, E>, flags: Flags) -> Result<Self, E>
    where
        E: From<AllocError>,
    {
        Ok(Self::from(UniqueArc::try_pin_init(init, flags)?))
    }

    /// Use the given initializer to in-place initialize a `T`.
    ///
    /// This is equivalent to [`ListArc<T>::pin_init`], since a [`ListArc`] is always pinned.
    #[inline]
    pub fn init<E>(init: impl Init<T, E>, flags: Flags) -> Result<Self, E>
    where
        E: From<AllocError>,
    {
        Ok(Self::from(UniqueArc::try_init(init, flags)?))
    }
}

impl<T, const ID: u64> From<UniqueArc<T>> for ListArc<T, ID>
where
    T: ListArcSafe<ID> + ?Sized,
{
    /// Convert a [`UniqueArc`] into a [`ListArc`].
    #[inline]
    fn from(unique: UniqueArc<T>) -> Self {
        Self::from(Pin::from(unique))
    }
}

impl<T, const ID: u64> From<Pin<UniqueArc<T>>> for ListArc<T, ID>
where
    T: ListArcSafe<ID> + ?Sized,
{
    /// Convert a pinned [`UniqueArc`] into a [`ListArc`].
    #[inline]
    fn from(mut unique: Pin<UniqueArc<T>>) -> Self {
        // SAFETY: We have a `UniqueArc`, so there is no `ListArc`.
        unsafe { T::on_create_list_arc_from_unique(unique.as_mut()) };
        let arc = Arc::from(unique);
        // SAFETY: We just called `on_create_list_arc_from_unique` on an arc without a `ListArc`,
        // so we can create a `ListArc`.
        unsafe { Self::transmute_from_arc(arc) }
    }
}

impl<T, const ID: u64> ListArc<T, ID>
where
    T: ListArcSafe<ID> + ?Sized,
{
    /// Creates two `ListArc`s from a [`UniqueArc`].
    ///
    /// The two ids must be different.
    #[inline]
    pub fn pair_from_unique<const ID2: u64>(unique: UniqueArc<T>) -> (Self, ListArc<T, ID2>)
    where
        T: ListArcSafe<ID2>,
    {
        Self::pair_from_pin_unique(Pin::from(unique))
    }

    /// Creates two `ListArc`s from a pinned [`UniqueArc`].
    ///
    /// The two ids must be different.
    #[inline]
    pub fn pair_from_pin_unique<const ID2: u64>(
        mut unique: Pin<UniqueArc<T>>,
    ) -> (Self, ListArc<T, ID2>)
    where
        T: ListArcSafe<ID2>,
    {
        build_assert!(ID != ID2);

        // SAFETY: We have a `UniqueArc`, so there is no `ListArc`.
        unsafe { <T as ListArcSafe<ID>>::on_create_list_arc_from_unique(unique.as_mut()) };
        // SAFETY: We have a `UniqueArc`, so there is no `ListArc`.
        unsafe { <T as ListArcSafe<ID2>>::on_create_list_arc_from_unique(unique.as_mut()) };

        let arc1 = Arc::from(unique);
        let arc2 = Arc::clone(&arc1);

        // SAFETY: We just called `on_create_list_arc_from_unique` on an arc without a `ListArc`
        // for both IDs (which are different), so we can create two `ListArc`s.
        unsafe {
            (
                Self::transmute_from_arc(arc1),
                ListArc::transmute_from_arc(arc2),
            )
        }
    }

    /// Try to create a new `ListArc`.
    ///
    /// This fails if this value already has a `ListArc`.
    pub fn try_from_arc(arc: Arc<T>) -> Result<Self, Arc<T>>
    where
        T: TryNewListArc<ID>,
    {
        if arc.try_new_list_arc() {
            // SAFETY: The `try_new_list_arc` method returned true, so we made the tracking think
            // that a `ListArc` exists. This lets us create a `ListArc`.
            Ok(unsafe { Self::transmute_from_arc(arc) })
        } else {
            Err(arc)
        }
    }

    /// Try to create a new `ListArc`.
    ///
    /// This fails if this value already has a `ListArc`.
    pub fn try_from_arc_borrow(arc: ArcBorrow<'_, T>) -> Option<Self>
    where
        T: TryNewListArc<ID>,
    {
        if arc.try_new_list_arc() {
            // SAFETY: The `try_new_list_arc` method returned true, so we made the tracking think
            // that a `ListArc` exists. This lets us create a `ListArc`.
            Some(unsafe { Self::transmute_from_arc(Arc::from(arc)) })
        } else {
            None
        }
    }

    /// Try to create a new `ListArc`.
    ///
    /// If it's not possible to create a new `ListArc`, then the `Arc` is dropped. This will never
    /// run the destructor of the value.
    pub fn try_from_arc_or_drop(arc: Arc<T>) -> Option<Self>
    where
        T: TryNewListArc<ID>,
    {
        match Self::try_from_arc(arc) {
            Ok(list_arc) => Some(list_arc),
            Err(arc) => Arc::into_unique_or_drop(arc).map(Self::from),
        }
    }

    /// Transmutes an [`Arc`] into a `ListArc` without updating the tracking inside `T`.
    ///
    /// # Safety
    ///
    /// * The value must not already have a `ListArc` reference.
    /// * The tracking inside `T` must think that there is a `ListArc` reference.
    #[inline]
    unsafe fn transmute_from_arc(arc: Arc<T>) -> Self {
        // INVARIANT: By the safety requirements, the invariants on `ListArc` are satisfied.
        Self { arc }
    }

    /// Transmutes a `ListArc` into an [`Arc`] without updating the tracking inside `T`.
    ///
    /// After this call, the tracking inside `T` will still think that there is a `ListArc`
    /// reference.
    #[inline]
    fn transmute_to_arc(self) -> Arc<T> {
        // Use a transmute to skip destructor.
        //
        // SAFETY: ListArc is repr(transparent).
        unsafe { core::mem::transmute(self) }
    }

    /// Convert ownership of this `ListArc` into a raw pointer.
    ///
    /// The returned pointer is indistinguishable from pointers returned by [`Arc::into_raw`]. The
    /// tracking inside `T` will still think that a `ListArc` exists after this call.
    #[inline]
    pub fn into_raw(self) -> *const T {
        Arc::into_raw(Self::transmute_to_arc(self))
    }

    /// Take ownership of the `ListArc` from a raw pointer.
    ///
    /// # Safety
    ///
    /// * `ptr` must satisfy the safety requirements of [`Arc::from_raw`].
    /// * The value must not already have a `ListArc` reference.
    /// * The tracking inside `T` must think that there is a `ListArc` reference.
    #[inline]
    pub unsafe fn from_raw(ptr: *const T) -> Self {
        // SAFETY: The pointer satisfies the safety requirements for `Arc::from_raw`.
        let arc = unsafe { Arc::from_raw(ptr) };
        // SAFETY: The value doesn't already have a `ListArc` reference, but the tracking thinks it
        // does.
        unsafe { Self::transmute_from_arc(arc) }
    }

    /// Converts the `ListArc` into an [`Arc`].
    #[inline]
    pub fn into_arc(self) -> Arc<T> {
        let arc = Self::transmute_to_arc(self);
        // SAFETY: There is no longer a `ListArc`, but the tracking thinks there is.
        unsafe { T::on_drop_list_arc(&arc) };
        arc
    }

    /// Clone a `ListArc` into an [`Arc`].
    #[inline]
    pub fn clone_arc(&self) -> Arc<T> {
        self.arc.clone()
    }

    /// Returns a reference to an [`Arc`] from the given [`ListArc`].
    ///
    /// This is useful when the argument of a function call is an [`&Arc`] (e.g., in a method
    /// receiver), but we have a [`ListArc`] instead.
    ///
    /// [`&Arc`]: Arc
    #[inline]
    pub fn as_arc(&self) -> &Arc<T> {
        &self.arc
    }

    /// Returns an [`ArcBorrow`] from the given [`ListArc`].
    ///
    /// This is useful when the argument of a function call is an [`ArcBorrow`] (e.g., in a method
    /// receiver), but we have an [`Arc`] instead. Getting an [`ArcBorrow`] is free when optimised.
    #[inline]
    pub fn as_arc_borrow(&self) -> ArcBorrow<'_, T> {
        self.arc.as_arc_borrow()
    }

    /// Compare whether two [`ListArc`] pointers reference the same underlying object.
    #[inline]
    pub fn ptr_eq(this: &Self, other: &Self) -> bool {
        Arc::ptr_eq(&this.arc, &other.arc)
    }
}

impl<T, const ID: u64> Deref for ListArc<T, ID>
where
    T: ListArcSafe<ID> + ?Sized,
{
    type Target = T;

    #[inline]
    fn deref(&self) -> &Self::Target {
        self.arc.deref()
    }
}

impl<T, const ID: u64> Drop for ListArc<T, ID>
where
    T: ListArcSafe<ID> + ?Sized,
{
    #[inline]
    fn drop(&mut self) {
        // SAFETY: There is no longer a `ListArc`, but the tracking thinks there is by the type
        // invariants on `Self`.
        unsafe { T::on_drop_list_arc(&self.arc) };
    }
}

impl<T, const ID: u64> AsRef<Arc<T>> for ListArc<T, ID>
where
    T: ListArcSafe<ID> + ?Sized,
{
    #[inline]
    fn as_ref(&self) -> &Arc<T> {
        self.as_arc()
    }
}

// This is to allow coercion from `ListArc<T>` to `ListArc<U>` if `T` can be converted to the
// dynamically-sized type (DST) `U`.
#[cfg(not(CONFIG_RUSTC_HAS_COERCE_POINTEE))]
impl<T, U, const ID: u64> core::ops::CoerceUnsized<ListArc<U, ID>> for ListArc<T, ID>
where
    T: ListArcSafe<ID> + core::marker::Unsize<U> + ?Sized,
    U: ListArcSafe<ID> + ?Sized,
{
}

// This is to allow `ListArc<U>` to be dispatched on when `ListArc<T>` can be coerced into
// `ListArc<U>`.
#[cfg(not(CONFIG_RUSTC_HAS_COERCE_POINTEE))]
impl<T, U, const ID: u64> core::ops::DispatchFromDyn<ListArc<U, ID>> for ListArc<T, ID>
where
    T: ListArcSafe<ID> + core::marker::Unsize<U> + ?Sized,
    U: ListArcSafe<ID> + ?Sized,
{
}

/// A utility for tracking whether a [`ListArc`] exists using an atomic.
///
/// # Invariants
///
/// If the boolean is `false`, then there is no [`ListArc`] for this value.
#[repr(transparent)]
pub struct AtomicTracker<const ID: u64 = 0> {
    inner: AtomicBool,
    // This value needs to be pinned to justify the INVARIANT: comment in `AtomicTracker::new`.
    _pin: PhantomPinned,
}

impl<const ID: u64> AtomicTracker<ID> {
    /// Creates a new initializer for this type.
    pub fn new() -> impl PinInit<Self> {
        // INVARIANT: Pin-init initializers can't be used on an existing `Arc`, so this value will
        // not be constructed in an `Arc` that already has a `ListArc`.
        Self {
            inner: AtomicBool::new(false),
            _pin: PhantomPinned,
        }
    }

    fn project_inner(self: Pin<&mut Self>) -> &mut AtomicBool {
        // SAFETY: The `inner` field is not structurally pinned, so we may obtain a mutable
        // reference to it even if we only have a pinned reference to `self`.
        unsafe { &mut Pin::into_inner_unchecked(self).inner }
    }
}

impl<const ID: u64> ListArcSafe<ID> for AtomicTracker<ID> {
    unsafe fn on_create_list_arc_from_unique(self: Pin<&mut Self>) {
        // INVARIANT: We just created a ListArc, so the boolean should be true.
        *self.project_inner().get_mut() = true;
    }

    unsafe fn on_drop_list_arc(&self) {
        // INVARIANT: We just dropped a ListArc, so the boolean should be false.
        self.inner.store(false, Ordering::Release);
    }
}

// SAFETY: If this method returns `true`, then by the type invariant there is no `ListArc` before
// this call, so it is okay to create a new `ListArc`.
//
// The acquire ordering will synchronize with the release store from the destruction of any
// previous `ListArc`, so if there was a previous `ListArc`, then the destruction of the previous
// `ListArc` happens-before the creation of the new `ListArc`.
unsafe impl<const ID: u64> TryNewListArc<ID> for AtomicTracker<ID> {
    fn try_new_list_arc(&self) -> bool {
        // INVARIANT: If this method returns true, then the boolean used to be false, and is no
        // longer false, so it is okay for the caller to create a new [`ListArc`].
        self.inner
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_ok()
    }
}
