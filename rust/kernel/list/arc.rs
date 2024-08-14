// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! A wrapper around `Arc` for linked lists.

use crate::alloc::{AllocError, Flags};
use crate::prelude::*;
use crate::sync::{Arc, ArcBorrow, UniqueArc};
use core::marker::Unsize;
use core::ops::Deref;
use core::pin::Pin;

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

/// Declares that this type supports [`ListArc`].
///
/// When using this macro, it will only be possible to create a [`ListArc`] from a [`UniqueArc`].
#[macro_export]
macro_rules! impl_list_arc_safe {
    (impl$({$($generics:tt)*})? ListArcSafe<$num:tt> for $t:ty { untracked; } $($rest:tt)*) => {
        impl$(<$($generics)*>)? $crate::list::ListArcSafe<$num> for $t {
            unsafe fn on_create_list_arc_from_unique(self: ::core::pin::Pin<&mut Self>) {}
            unsafe fn on_drop_list_arc(&self) {}
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
/// exclusive access to the `next`/`prev` pointers. When a `ListArc` is inserted into a `List`, the
/// `List` takes ownership of the `ListArc` reference.
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
#[repr(transparent)]
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

// This is to allow [`ListArc`] (and variants) to be used as the type of `self`.
impl<T, const ID: u64> core::ops::Receiver for ListArc<T, ID> where T: ListArcSafe<ID> + ?Sized {}

// This is to allow coercion from `ListArc<T>` to `ListArc<U>` if `T` can be converted to the
// dynamically-sized type (DST) `U`.
impl<T, U, const ID: u64> core::ops::CoerceUnsized<ListArc<U, ID>> for ListArc<T, ID>
where
    T: ListArcSafe<ID> + Unsize<U> + ?Sized,
    U: ListArcSafe<ID> + ?Sized,
{
}

// This is to allow `ListArc<U>` to be dispatched on when `ListArc<T>` can be coerced into
// `ListArc<U>`.
impl<T, U, const ID: u64> core::ops::DispatchFromDyn<ListArc<U, ID>> for ListArc<T, ID>
where
    T: ListArcSafe<ID> + Unsize<U> + ?Sized,
    U: ListArcSafe<ID> + ?Sized,
{
}
