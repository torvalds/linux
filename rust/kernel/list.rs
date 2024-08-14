// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! A linked list implementation.

use crate::init::PinInit;
use crate::types::Opaque;
use core::ptr;

mod arc;
pub use self::arc::{impl_list_arc_safe, AtomicTracker, ListArc, ListArcSafe, TryNewListArc};

/// Implemented by types where a [`ListArc<Self>`] can be inserted into a `List`.
///
/// # Safety
///
/// Implementers must ensure that they provide the guarantees documented on methods provided by
/// this trait.
///
/// [`ListArc<Self>`]: ListArc
pub unsafe trait ListItem<const ID: u64 = 0>: ListArcSafe<ID> {
    /// Views the [`ListLinks`] for this value.
    ///
    /// # Guarantees
    ///
    /// If there is a previous call to `prepare_to_insert` and there is no call to `post_remove`
    /// since the most recent such call, then this returns the same pointer as the one returned by
    /// the most recent call to `prepare_to_insert`.
    ///
    /// Otherwise, the returned pointer points at a read-only [`ListLinks`] with two null pointers.
    ///
    /// # Safety
    ///
    /// The provided pointer must point at a valid value. (It need not be in an `Arc`.)
    unsafe fn view_links(me: *const Self) -> *mut ListLinks<ID>;

    /// View the full value given its [`ListLinks`] field.
    ///
    /// Can only be used when the value is in a list.
    ///
    /// # Guarantees
    ///
    /// * Returns the same pointer as the one passed to the most recent call to `prepare_to_insert`.
    /// * The returned pointer is valid until the next call to `post_remove`.
    ///
    /// # Safety
    ///
    /// * The provided pointer must originate from the most recent call to `prepare_to_insert`, or
    ///   from a call to `view_links` that happened after the most recent call to
    ///   `prepare_to_insert`.
    /// * Since the most recent call to `prepare_to_insert`, the `post_remove` method must not have
    ///   been called.
    unsafe fn view_value(me: *mut ListLinks<ID>) -> *const Self;

    /// This is called when an item is inserted into a `List`.
    ///
    /// # Guarantees
    ///
    /// The caller is granted exclusive access to the returned [`ListLinks`] until `post_remove` is
    /// called.
    ///
    /// # Safety
    ///
    /// * The provided pointer must point at a valid value in an [`Arc`].
    /// * Calls to `prepare_to_insert` and `post_remove` on the same value must alternate.
    /// * The caller must own the [`ListArc`] for this value.
    /// * The caller must not give up ownership of the [`ListArc`] unless `post_remove` has been
    ///   called after this call to `prepare_to_insert`.
    ///
    /// [`Arc`]: crate::sync::Arc
    unsafe fn prepare_to_insert(me: *const Self) -> *mut ListLinks<ID>;

    /// This undoes a previous call to `prepare_to_insert`.
    ///
    /// # Guarantees
    ///
    /// The returned pointer is the pointer that was originally passed to `prepare_to_insert`.
    ///
    /// # Safety
    ///
    /// The provided pointer must be the pointer returned by the most recent call to
    /// `prepare_to_insert`.
    unsafe fn post_remove(me: *mut ListLinks<ID>) -> *const Self;
}

#[repr(C)]
#[derive(Copy, Clone)]
struct ListLinksFields {
    next: *mut ListLinksFields,
    prev: *mut ListLinksFields,
}

/// The prev/next pointers for an item in a linked list.
///
/// # Invariants
///
/// The fields are null if and only if this item is not in a list.
#[repr(transparent)]
pub struct ListLinks<const ID: u64 = 0> {
    // This type is `!Unpin` for aliasing reasons as the pointers are part of an intrusive linked
    // list.
    #[allow(dead_code)]
    inner: Opaque<ListLinksFields>,
}

// SAFETY: The only way to access/modify the pointers inside of `ListLinks<ID>` is via holding the
// associated `ListArc<T, ID>`. Since that type correctly implements `Send`, it is impossible to
// move this an instance of this type to a different thread if the pointees are `!Send`.
unsafe impl<const ID: u64> Send for ListLinks<ID> {}
// SAFETY: The type is opaque so immutable references to a ListLinks are useless. Therefore, it's
// okay to have immutable access to a ListLinks from several threads at once.
unsafe impl<const ID: u64> Sync for ListLinks<ID> {}

impl<const ID: u64> ListLinks<ID> {
    /// Creates a new initializer for this type.
    pub fn new() -> impl PinInit<Self> {
        // INVARIANT: Pin-init initializers can't be used on an existing `Arc`, so this value will
        // not be constructed in an `Arc` that already has a `ListArc`.
        ListLinks {
            inner: Opaque::new(ListLinksFields {
                prev: ptr::null_mut(),
                next: ptr::null_mut(),
            }),
        }
    }
}
