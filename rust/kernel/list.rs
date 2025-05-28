// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! A linked list implementation.

use crate::sync::ArcBorrow;
use crate::types::Opaque;
use core::iter::{DoubleEndedIterator, FusedIterator};
use core::marker::PhantomData;
use core::ptr;
use pin_init::PinInit;

mod impl_list_item_mod;
pub use self::impl_list_item_mod::{
    impl_has_list_links, impl_has_list_links_self_ptr, impl_list_item, HasListLinks, HasSelfPtr,
};

mod arc;
pub use self::arc::{impl_list_arc_safe, AtomicTracker, ListArc, ListArcSafe, TryNewListArc};

mod arc_field;
pub use self::arc_field::{define_list_arc_field_getter, ListArcField};

/// A linked list.
///
/// All elements in this linked list will be [`ListArc`] references to the value. Since a value can
/// only have one `ListArc` (for each pair of prev/next pointers), this ensures that the same
/// prev/next pointers are not used for several linked lists.
///
/// # Invariants
///
/// * If the list is empty, then `first` is null. Otherwise, `first` points at the `ListLinks`
///   field of the first element in the list.
/// * All prev/next pointers in `ListLinks` fields of items in the list are valid and form a cycle.
/// * For every item in the list, the list owns the associated [`ListArc`] reference and has
///   exclusive access to the `ListLinks` field.
pub struct List<T: ?Sized + ListItem<ID>, const ID: u64 = 0> {
    first: *mut ListLinksFields,
    _ty: PhantomData<ListArc<T, ID>>,
}

// SAFETY: This is a container of `ListArc<T, ID>`, and access to the container allows the same
// type of access to the `ListArc<T, ID>` elements.
unsafe impl<T, const ID: u64> Send for List<T, ID>
where
    ListArc<T, ID>: Send,
    T: ?Sized + ListItem<ID>,
{
}
// SAFETY: This is a container of `ListArc<T, ID>`, and access to the container allows the same
// type of access to the `ListArc<T, ID>` elements.
unsafe impl<T, const ID: u64> Sync for List<T, ID>
where
    ListArc<T, ID>: Sync,
    T: ?Sized + ListItem<ID>,
{
}

/// Implemented by types where a [`ListArc<Self>`] can be inserted into a [`List`].
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

    /// This is called when an item is inserted into a [`List`].
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

    /// # Safety
    ///
    /// `me` must be dereferenceable.
    #[inline]
    unsafe fn fields(me: *mut Self) -> *mut ListLinksFields {
        // SAFETY: The caller promises that the pointer is valid.
        unsafe { Opaque::raw_get(ptr::addr_of!((*me).inner)) }
    }

    /// # Safety
    ///
    /// `me` must be dereferenceable.
    #[inline]
    unsafe fn from_fields(me: *mut ListLinksFields) -> *mut Self {
        me.cast()
    }
}

/// Similar to [`ListLinks`], but also contains a pointer to the full value.
///
/// This type can be used instead of [`ListLinks`] to support lists with trait objects.
#[repr(C)]
pub struct ListLinksSelfPtr<T: ?Sized, const ID: u64 = 0> {
    /// The `ListLinks` field inside this value.
    ///
    /// This is public so that it can be used with `impl_has_list_links!`.
    pub inner: ListLinks<ID>,
    // UnsafeCell is not enough here because we use `Opaque::uninit` as a dummy value, and
    // `ptr::null()` doesn't work for `T: ?Sized`.
    self_ptr: Opaque<*const T>,
}

// SAFETY: The fields of a ListLinksSelfPtr can be moved across thread boundaries.
unsafe impl<T: ?Sized + Send, const ID: u64> Send for ListLinksSelfPtr<T, ID> {}
// SAFETY: The type is opaque so immutable references to a ListLinksSelfPtr are useless. Therefore,
// it's okay to have immutable access to a ListLinks from several threads at once.
//
// Note that `inner` being a public field does not prevent this type from being opaque, since
// `inner` is a opaque type.
unsafe impl<T: ?Sized + Sync, const ID: u64> Sync for ListLinksSelfPtr<T, ID> {}

impl<T: ?Sized, const ID: u64> ListLinksSelfPtr<T, ID> {
    /// The offset from the [`ListLinks`] to the self pointer field.
    pub const LIST_LINKS_SELF_PTR_OFFSET: usize = core::mem::offset_of!(Self, self_ptr);

    /// Creates a new initializer for this type.
    pub fn new() -> impl PinInit<Self> {
        // INVARIANT: Pin-init initializers can't be used on an existing `Arc`, so this value will
        // not be constructed in an `Arc` that already has a `ListArc`.
        Self {
            inner: ListLinks {
                inner: Opaque::new(ListLinksFields {
                    prev: ptr::null_mut(),
                    next: ptr::null_mut(),
                }),
            },
            self_ptr: Opaque::uninit(),
        }
    }
}

impl<T: ?Sized + ListItem<ID>, const ID: u64> List<T, ID> {
    /// Creates a new empty list.
    pub const fn new() -> Self {
        Self {
            first: ptr::null_mut(),
            _ty: PhantomData,
        }
    }

    /// Returns whether this list is empty.
    pub fn is_empty(&self) -> bool {
        self.first.is_null()
    }

    /// Inserts `item` before `next` in the cycle.
    ///
    /// Returns a pointer to the newly inserted element. Never changes `self.first` unless the list
    /// is empty.
    ///
    /// # Safety
    ///
    /// * `next` must be an element in this list or null.
    /// * if `next` is null, then the list must be empty.
    unsafe fn insert_inner(
        &mut self,
        item: ListArc<T, ID>,
        next: *mut ListLinksFields,
    ) -> *mut ListLinksFields {
        let raw_item = ListArc::into_raw(item);
        // SAFETY:
        // * We just got `raw_item` from a `ListArc`, so it's in an `Arc`.
        // * Since we have ownership of the `ListArc`, `post_remove` must have been called after
        //   the most recent call to `prepare_to_insert`, if any.
        // * We own the `ListArc`.
        // * Removing items from this list is always done using `remove_internal_inner`, which
        //   calls `post_remove` before giving up ownership.
        let list_links = unsafe { T::prepare_to_insert(raw_item) };
        // SAFETY: We have not yet called `post_remove`, so `list_links` is still valid.
        let item = unsafe { ListLinks::fields(list_links) };

        // Check if the list is empty.
        if next.is_null() {
            // SAFETY: The caller just gave us ownership of these fields.
            // INVARIANT: A linked list with one item should be cyclic.
            unsafe {
                (*item).next = item;
                (*item).prev = item;
            }
            self.first = item;
        } else {
            // SAFETY: By the type invariant, this pointer is valid or null. We just checked that
            // it's not null, so it must be valid.
            let prev = unsafe { (*next).prev };
            // SAFETY: Pointers in a linked list are never dangling, and the caller just gave us
            // ownership of the fields on `item`.
            // INVARIANT: This correctly inserts `item` between `prev` and `next`.
            unsafe {
                (*item).next = next;
                (*item).prev = prev;
                (*prev).next = item;
                (*next).prev = item;
            }
        }

        item
    }

    /// Add the provided item to the back of the list.
    pub fn push_back(&mut self, item: ListArc<T, ID>) {
        // SAFETY:
        // * `self.first` is null or in the list.
        // * `self.first` is only null if the list is empty.
        unsafe { self.insert_inner(item, self.first) };
    }

    /// Add the provided item to the front of the list.
    pub fn push_front(&mut self, item: ListArc<T, ID>) {
        // SAFETY:
        // * `self.first` is null or in the list.
        // * `self.first` is only null if the list is empty.
        let new_elem = unsafe { self.insert_inner(item, self.first) };

        // INVARIANT: `new_elem` is in the list because we just inserted it.
        self.first = new_elem;
    }

    /// Removes the last item from this list.
    pub fn pop_back(&mut self) -> Option<ListArc<T, ID>> {
        if self.first.is_null() {
            return None;
        }

        // SAFETY: We just checked that the list is not empty.
        let last = unsafe { (*self.first).prev };
        // SAFETY: The last item of this list is in this list.
        Some(unsafe { self.remove_internal(last) })
    }

    /// Removes the first item from this list.
    pub fn pop_front(&mut self) -> Option<ListArc<T, ID>> {
        if self.first.is_null() {
            return None;
        }

        // SAFETY: The first item of this list is in this list.
        Some(unsafe { self.remove_internal(self.first) })
    }

    /// Removes the provided item from this list and returns it.
    ///
    /// This returns `None` if the item is not in the list. (Note that by the safety requirements,
    /// this means that the item is not in any list.)
    ///
    /// # Safety
    ///
    /// `item` must not be in a different linked list (with the same id).
    pub unsafe fn remove(&mut self, item: &T) -> Option<ListArc<T, ID>> {
        // SAFETY: TODO.
        let mut item = unsafe { ListLinks::fields(T::view_links(item)) };
        // SAFETY: The user provided a reference, and reference are never dangling.
        //
        // As for why this is not a data race, there are two cases:
        //
        //  * If `item` is not in any list, then these fields are read-only and null.
        //  * If `item` is in this list, then we have exclusive access to these fields since we
        //    have a mutable reference to the list.
        //
        // In either case, there's no race.
        let ListLinksFields { next, prev } = unsafe { *item };

        debug_assert_eq!(next.is_null(), prev.is_null());
        if !next.is_null() {
            // This is really a no-op, but this ensures that `item` is a raw pointer that was
            // obtained without going through a pointer->reference->pointer conversion roundtrip.
            // This ensures that the list is valid under the more restrictive strict provenance
            // ruleset.
            //
            // SAFETY: We just checked that `next` is not null, and it's not dangling by the
            // list invariants.
            unsafe {
                debug_assert_eq!(item, (*next).prev);
                item = (*next).prev;
            }

            // SAFETY: We just checked that `item` is in a list, so the caller guarantees that it
            // is in this list. The pointers are in the right order.
            Some(unsafe { self.remove_internal_inner(item, next, prev) })
        } else {
            None
        }
    }

    /// Removes the provided item from the list.
    ///
    /// # Safety
    ///
    /// `item` must point at an item in this list.
    unsafe fn remove_internal(&mut self, item: *mut ListLinksFields) -> ListArc<T, ID> {
        // SAFETY: The caller promises that this pointer is not dangling, and there's no data race
        // since we have a mutable reference to the list containing `item`.
        let ListLinksFields { next, prev } = unsafe { *item };
        // SAFETY: The pointers are ok and in the right order.
        unsafe { self.remove_internal_inner(item, next, prev) }
    }

    /// Removes the provided item from the list.
    ///
    /// # Safety
    ///
    /// The `item` pointer must point at an item in this list, and we must have `(*item).next ==
    /// next` and `(*item).prev == prev`.
    unsafe fn remove_internal_inner(
        &mut self,
        item: *mut ListLinksFields,
        next: *mut ListLinksFields,
        prev: *mut ListLinksFields,
    ) -> ListArc<T, ID> {
        // SAFETY: We have exclusive access to the pointers of items in the list, and the prev/next
        // pointers are always valid for items in a list.
        //
        // INVARIANT: There are three cases:
        //  * If the list has at least three items, then after removing the item, `prev` and `next`
        //    will be next to each other.
        //  * If the list has two items, then the remaining item will point at itself.
        //  * If the list has one item, then `next == prev == item`, so these writes have no
        //    effect. The list remains unchanged and `item` is still in the list for now.
        unsafe {
            (*next).prev = prev;
            (*prev).next = next;
        }
        // SAFETY: We have exclusive access to items in the list.
        // INVARIANT: `item` is being removed, so the pointers should be null.
        unsafe {
            (*item).prev = ptr::null_mut();
            (*item).next = ptr::null_mut();
        }
        // INVARIANT: There are three cases:
        //  * If `item` was not the first item, then `self.first` should remain unchanged.
        //  * If `item` was the first item and there is another item, then we just updated
        //    `prev->next` to `next`, which is the new first item, and setting `item->next` to null
        //    did not modify `prev->next`.
        //  * If `item` was the only item in the list, then `prev == item`, and we just set
        //    `item->next` to null, so this correctly sets `first` to null now that the list is
        //    empty.
        if self.first == item {
            // SAFETY: The `prev` pointer is the value that `item->prev` had when it was in this
            // list, so it must be valid. There is no race since `prev` is still in the list and we
            // still have exclusive access to the list.
            self.first = unsafe { (*prev).next };
        }

        // SAFETY: `item` used to be in the list, so it is dereferenceable by the type invariants
        // of `List`.
        let list_links = unsafe { ListLinks::from_fields(item) };
        // SAFETY: Any pointer in the list originates from a `prepare_to_insert` call.
        let raw_item = unsafe { T::post_remove(list_links) };
        // SAFETY: The above call to `post_remove` guarantees that we can recreate the `ListArc`.
        unsafe { ListArc::from_raw(raw_item) }
    }

    /// Moves all items from `other` into `self`.
    ///
    /// The items of `other` are added to the back of `self`, so the last item of `other` becomes
    /// the last item of `self`.
    pub fn push_all_back(&mut self, other: &mut List<T, ID>) {
        // First, we insert the elements into `self`. At the end, we make `other` empty.
        if self.is_empty() {
            // INVARIANT: All of the elements in `other` become elements of `self`.
            self.first = other.first;
        } else if !other.is_empty() {
            let other_first = other.first;
            // SAFETY: The other list is not empty, so this pointer is valid.
            let other_last = unsafe { (*other_first).prev };
            let self_first = self.first;
            // SAFETY: The self list is not empty, so this pointer is valid.
            let self_last = unsafe { (*self_first).prev };

            // SAFETY: We have exclusive access to both lists, so we can update the pointers.
            // INVARIANT: This correctly sets the pointers to merge both lists. We do not need to
            // update `self.first` because the first element of `self` does not change.
            unsafe {
                (*self_first).prev = other_last;
                (*other_last).next = self_first;
                (*self_last).next = other_first;
                (*other_first).prev = self_last;
            }
        }

        // INVARIANT: The other list is now empty, so update its pointer.
        other.first = ptr::null_mut();
    }

    /// Returns a cursor that points before the first element of the list.
    pub fn cursor_front(&mut self) -> Cursor<'_, T, ID> {
        // INVARIANT: `self.first` is in this list.
        Cursor {
            next: self.first,
            list: self,
        }
    }

    /// Returns a cursor that points after the last element in the list.
    pub fn cursor_back(&mut self) -> Cursor<'_, T, ID> {
        // INVARIANT: `next` is allowed to be null.
        Cursor {
            next: core::ptr::null_mut(),
            list: self,
        }
    }

    /// Creates an iterator over the list.
    pub fn iter(&self) -> Iter<'_, T, ID> {
        // INVARIANT: If the list is empty, both pointers are null. Otherwise, both pointers point
        // at the first element of the same list.
        Iter {
            current: self.first,
            stop: self.first,
            _ty: PhantomData,
        }
    }
}

impl<T: ?Sized + ListItem<ID>, const ID: u64> Default for List<T, ID> {
    fn default() -> Self {
        List::new()
    }
}

impl<T: ?Sized + ListItem<ID>, const ID: u64> Drop for List<T, ID> {
    fn drop(&mut self) {
        while let Some(item) = self.pop_front() {
            drop(item);
        }
    }
}

/// An iterator over a [`List`].
///
/// # Invariants
///
/// * There must be a [`List`] that is immutably borrowed for the duration of `'a`.
/// * The `current` pointer is null or points at a value in that [`List`].
/// * The `stop` pointer is equal to the `first` field of that [`List`].
#[derive(Clone)]
pub struct Iter<'a, T: ?Sized + ListItem<ID>, const ID: u64 = 0> {
    current: *mut ListLinksFields,
    stop: *mut ListLinksFields,
    _ty: PhantomData<&'a ListArc<T, ID>>,
}

impl<'a, T: ?Sized + ListItem<ID>, const ID: u64> Iterator for Iter<'a, T, ID> {
    type Item = ArcBorrow<'a, T>;

    fn next(&mut self) -> Option<ArcBorrow<'a, T>> {
        if self.current.is_null() {
            return None;
        }

        let current = self.current;

        // SAFETY: We just checked that `current` is not null, so it is in a list, and hence not
        // dangling. There's no race because the iterator holds an immutable borrow to the list.
        let next = unsafe { (*current).next };
        // INVARIANT: If `current` was the last element of the list, then this updates it to null.
        // Otherwise, we update it to the next element.
        self.current = if next != self.stop {
            next
        } else {
            ptr::null_mut()
        };

        // SAFETY: The `current` pointer points at a value in the list.
        let item = unsafe { T::view_value(ListLinks::from_fields(current)) };
        // SAFETY:
        // * All values in a list are stored in an `Arc`.
        // * The value cannot be removed from the list for the duration of the lifetime annotated
        //   on the returned `ArcBorrow`, because removing it from the list would require mutable
        //   access to the list. However, the `ArcBorrow` is annotated with the iterator's
        //   lifetime, and the list is immutably borrowed for that lifetime.
        // * Values in a list never have a `UniqueArc` reference.
        Some(unsafe { ArcBorrow::from_raw(item) })
    }
}

/// A cursor into a [`List`].
///
/// A cursor always rests between two elements in the list. This means that a cursor has a previous
/// and next element, but no current element. It also means that it's possible to have a cursor
/// into an empty list.
///
/// # Examples
///
/// ```
/// use kernel::prelude::*;
/// use kernel::list::{List, ListArc, ListLinks};
///
/// #[pin_data]
/// struct ListItem {
///     value: u32,
///     #[pin]
///     links: ListLinks,
/// }
///
/// impl ListItem {
///     fn new(value: u32) -> Result<ListArc<Self>> {
///         ListArc::pin_init(try_pin_init!(Self {
///             value,
///             links <- ListLinks::new(),
///         }), GFP_KERNEL)
///     }
/// }
///
/// kernel::list::impl_has_list_links! {
///     impl HasListLinks<0> for ListItem { self.links }
/// }
/// kernel::list::impl_list_arc_safe! {
///     impl ListArcSafe<0> for ListItem { untracked; }
/// }
/// kernel::list::impl_list_item! {
///     impl ListItem<0> for ListItem { using ListLinks; }
/// }
///
/// // Use a cursor to remove the first element with the given value.
/// fn remove_first(list: &mut List<ListItem>, value: u32) -> Option<ListArc<ListItem>> {
///     let mut cursor = list.cursor_front();
///     while let Some(next) = cursor.peek_next() {
///         if next.value == value {
///             return Some(next.remove());
///         }
///         cursor.move_next();
///     }
///     None
/// }
///
/// // Use a cursor to remove the last element with the given value.
/// fn remove_last(list: &mut List<ListItem>, value: u32) -> Option<ListArc<ListItem>> {
///     let mut cursor = list.cursor_back();
///     while let Some(prev) = cursor.peek_prev() {
///         if prev.value == value {
///             return Some(prev.remove());
///         }
///         cursor.move_prev();
///     }
///     None
/// }
///
/// // Use a cursor to remove all elements with the given value. The removed elements are moved to
/// // a new list.
/// fn remove_all(list: &mut List<ListItem>, value: u32) -> List<ListItem> {
///     let mut out = List::new();
///     let mut cursor = list.cursor_front();
///     while let Some(next) = cursor.peek_next() {
///         if next.value == value {
///             out.push_back(next.remove());
///         } else {
///             cursor.move_next();
///         }
///     }
///     out
/// }
///
/// // Use a cursor to insert a value at a specific index. Returns an error if the index is out of
/// // bounds.
/// fn insert_at(list: &mut List<ListItem>, new: ListArc<ListItem>, idx: usize) -> Result {
///     let mut cursor = list.cursor_front();
///     for _ in 0..idx {
///         if !cursor.move_next() {
///             return Err(EINVAL);
///         }
///     }
///     cursor.insert_next(new);
///     Ok(())
/// }
///
/// // Merge two sorted lists into a single sorted list.
/// fn merge_sorted(list: &mut List<ListItem>, merge: List<ListItem>) {
///     let mut cursor = list.cursor_front();
///     for to_insert in merge {
///         while let Some(next) = cursor.peek_next() {
///             if to_insert.value < next.value {
///                 break;
///             }
///             cursor.move_next();
///         }
///         cursor.insert_prev(to_insert);
///     }
/// }
///
/// let mut list = List::new();
/// list.push_back(ListItem::new(14)?);
/// list.push_back(ListItem::new(12)?);
/// list.push_back(ListItem::new(10)?);
/// list.push_back(ListItem::new(12)?);
/// list.push_back(ListItem::new(15)?);
/// list.push_back(ListItem::new(14)?);
/// assert_eq!(remove_all(&mut list, 12).iter().count(), 2);
/// // [14, 10, 15, 14]
/// assert!(remove_first(&mut list, 14).is_some());
/// // [10, 15, 14]
/// insert_at(&mut list, ListItem::new(12)?, 2)?;
/// // [10, 15, 12, 14]
/// assert!(remove_last(&mut list, 15).is_some());
/// // [10, 12, 14]
///
/// let mut list2 = List::new();
/// list2.push_back(ListItem::new(11)?);
/// list2.push_back(ListItem::new(13)?);
/// merge_sorted(&mut list, list2);
///
/// let mut items = list.into_iter();
/// assert_eq!(items.next().unwrap().value, 10);
/// assert_eq!(items.next().unwrap().value, 11);
/// assert_eq!(items.next().unwrap().value, 12);
/// assert_eq!(items.next().unwrap().value, 13);
/// assert_eq!(items.next().unwrap().value, 14);
/// assert!(items.next().is_none());
/// # Result::<(), Error>::Ok(())
/// ```
///
/// # Invariants
///
/// The `next` pointer is null or points a value in `list`.
pub struct Cursor<'a, T: ?Sized + ListItem<ID>, const ID: u64 = 0> {
    list: &'a mut List<T, ID>,
    /// Points at the element after this cursor, or null if the cursor is after the last element.
    next: *mut ListLinksFields,
}

impl<'a, T: ?Sized + ListItem<ID>, const ID: u64> Cursor<'a, T, ID> {
    /// Returns a pointer to the element before the cursor.
    ///
    /// Returns null if there is no element before the cursor.
    fn prev_ptr(&self) -> *mut ListLinksFields {
        let mut next = self.next;
        let first = self.list.first;
        if next == first {
            // We are before the first element.
            return core::ptr::null_mut();
        }

        if next.is_null() {
            // We are after the last element, so we need a pointer to the last element, which is
            // the same as `(*first).prev`.
            next = first;
        }

        // SAFETY: `next` can't be null, because then `first` must also be null, but in that case
        // we would have exited at the `next == first` check. Thus, `next` is an element in the
        // list, so we can access its `prev` pointer.
        unsafe { (*next).prev }
    }

    /// Access the element after this cursor.
    pub fn peek_next(&mut self) -> Option<CursorPeek<'_, 'a, T, true, ID>> {
        if self.next.is_null() {
            return None;
        }

        // INVARIANT:
        // * We just checked that `self.next` is non-null, so it must be in `self.list`.
        // * `ptr` is equal to `self.next`.
        Some(CursorPeek {
            ptr: self.next,
            cursor: self,
        })
    }

    /// Access the element before this cursor.
    pub fn peek_prev(&mut self) -> Option<CursorPeek<'_, 'a, T, false, ID>> {
        let prev = self.prev_ptr();

        if prev.is_null() {
            return None;
        }

        // INVARIANT:
        // * We just checked that `prev` is non-null, so it must be in `self.list`.
        // * `self.prev_ptr()` never returns `self.next`.
        Some(CursorPeek {
            ptr: prev,
            cursor: self,
        })
    }

    /// Move the cursor one element forward.
    ///
    /// If the cursor is after the last element, then this call does nothing. This call returns
    /// `true` if the cursor's position was changed.
    pub fn move_next(&mut self) -> bool {
        if self.next.is_null() {
            return false;
        }

        // SAFETY: `self.next` is an element in the list and we borrow the list mutably, so we can
        // access the `next` field.
        let mut next = unsafe { (*self.next).next };

        if next == self.list.first {
            next = core::ptr::null_mut();
        }

        // INVARIANT: `next` is either null or the next element after an element in the list.
        self.next = next;
        true
    }

    /// Move the cursor one element backwards.
    ///
    /// If the cursor is before the first element, then this call does nothing. This call returns
    /// `true` if the cursor's position was changed.
    pub fn move_prev(&mut self) -> bool {
        if self.next == self.list.first {
            return false;
        }

        // INVARIANT: `prev_ptr()` always returns a pointer that is null or in the list.
        self.next = self.prev_ptr();
        true
    }

    /// Inserts an element where the cursor is pointing and get a pointer to the new element.
    fn insert_inner(&mut self, item: ListArc<T, ID>) -> *mut ListLinksFields {
        let ptr = if self.next.is_null() {
            self.list.first
        } else {
            self.next
        };
        // SAFETY:
        // * `ptr` is an element in the list or null.
        // * if `ptr` is null, then `self.list.first` is null so the list is empty.
        let item = unsafe { self.list.insert_inner(item, ptr) };
        if self.next == self.list.first {
            // INVARIANT: We just inserted `item`, so it's a member of list.
            self.list.first = item;
        }
        item
    }

    /// Insert an element at this cursor's location.
    pub fn insert(mut self, item: ListArc<T, ID>) {
        // This is identical to `insert_prev`, but consumes the cursor. This is helpful because it
        // reduces confusion when the last operation on the cursor is an insertion; in that case,
        // you just want to insert the element at the cursor, and it is confusing that the call
        // involves the word prev or next.
        self.insert_inner(item);
    }

    /// Inserts an element after this cursor.
    ///
    /// After insertion, the new element will be after the cursor.
    pub fn insert_next(&mut self, item: ListArc<T, ID>) {
        self.next = self.insert_inner(item);
    }

    /// Inserts an element before this cursor.
    ///
    /// After insertion, the new element will be before the cursor.
    pub fn insert_prev(&mut self, item: ListArc<T, ID>) {
        self.insert_inner(item);
    }

    /// Remove the next element from the list.
    pub fn remove_next(&mut self) -> Option<ListArc<T, ID>> {
        self.peek_next().map(|v| v.remove())
    }

    /// Remove the previous element from the list.
    pub fn remove_prev(&mut self) -> Option<ListArc<T, ID>> {
        self.peek_prev().map(|v| v.remove())
    }
}

/// References the element in the list next to the cursor.
///
/// # Invariants
///
/// * `ptr` is an element in `self.cursor.list`.
/// * `ISNEXT == (self.ptr == self.cursor.next)`.
pub struct CursorPeek<'a, 'b, T: ?Sized + ListItem<ID>, const ISNEXT: bool, const ID: u64> {
    cursor: &'a mut Cursor<'b, T, ID>,
    ptr: *mut ListLinksFields,
}

impl<'a, 'b, T: ?Sized + ListItem<ID>, const ISNEXT: bool, const ID: u64>
    CursorPeek<'a, 'b, T, ISNEXT, ID>
{
    /// Remove the element from the list.
    pub fn remove(self) -> ListArc<T, ID> {
        if ISNEXT {
            self.cursor.move_next();
        }

        // INVARIANT: `self.ptr` is not equal to `self.cursor.next` due to the above `move_next`
        // call.
        // SAFETY: By the type invariants of `Self`, `next` is not null, so `next` is an element of
        // `self.cursor.list` by the type invariants of `Cursor`.
        unsafe { self.cursor.list.remove_internal(self.ptr) }
    }

    /// Access this value as an [`ArcBorrow`].
    pub fn arc(&self) -> ArcBorrow<'_, T> {
        // SAFETY: `self.ptr` points at an element in `self.cursor.list`.
        let me = unsafe { T::view_value(ListLinks::from_fields(self.ptr)) };
        // SAFETY:
        // * All values in a list are stored in an `Arc`.
        // * The value cannot be removed from the list for the duration of the lifetime annotated
        //   on the returned `ArcBorrow`, because removing it from the list would require mutable
        //   access to the `CursorPeek`, the `Cursor` or the `List`. However, the `ArcBorrow` holds
        //   an immutable borrow on the `CursorPeek`, which in turn holds a mutable borrow on the
        //   `Cursor`, which in turn holds a mutable borrow on the `List`, so any such mutable
        //   access requires first releasing the immutable borrow on the `CursorPeek`.
        // * Values in a list never have a `UniqueArc` reference, because the list has a `ListArc`
        //   reference, and `UniqueArc` references must be unique.
        unsafe { ArcBorrow::from_raw(me) }
    }
}

impl<'a, 'b, T: ?Sized + ListItem<ID>, const ISNEXT: bool, const ID: u64> core::ops::Deref
    for CursorPeek<'a, 'b, T, ISNEXT, ID>
{
    // If you change the `ptr` field to have type `ArcBorrow<'a, T>`, it might seem like you could
    // get rid of the `CursorPeek::arc` method and change the deref target to `ArcBorrow<'a, T>`.
    // However, that doesn't work because 'a is too long. You could obtain an `ArcBorrow<'a, T>`
    // and then call `CursorPeek::remove` without giving up the `ArcBorrow<'a, T>`, which would be
    // unsound.
    type Target = T;

    fn deref(&self) -> &T {
        // SAFETY: `self.ptr` points at an element in `self.cursor.list`.
        let me = unsafe { T::view_value(ListLinks::from_fields(self.ptr)) };

        // SAFETY: The value cannot be removed from the list for the duration of the lifetime
        // annotated on the returned `&T`, because removing it from the list would require mutable
        // access to the `CursorPeek`, the `Cursor` or the `List`. However, the `&T` holds an
        // immutable borrow on the `CursorPeek`, which in turn holds a mutable borrow on the
        // `Cursor`, which in turn holds a mutable borrow on the `List`, so any such mutable access
        // requires first releasing the immutable borrow on the `CursorPeek`.
        unsafe { &*me }
    }
}

impl<'a, T: ?Sized + ListItem<ID>, const ID: u64> FusedIterator for Iter<'a, T, ID> {}

impl<'a, T: ?Sized + ListItem<ID>, const ID: u64> IntoIterator for &'a List<T, ID> {
    type IntoIter = Iter<'a, T, ID>;
    type Item = ArcBorrow<'a, T>;

    fn into_iter(self) -> Iter<'a, T, ID> {
        self.iter()
    }
}

/// An owning iterator into a [`List`].
pub struct IntoIter<T: ?Sized + ListItem<ID>, const ID: u64 = 0> {
    list: List<T, ID>,
}

impl<T: ?Sized + ListItem<ID>, const ID: u64> Iterator for IntoIter<T, ID> {
    type Item = ListArc<T, ID>;

    fn next(&mut self) -> Option<ListArc<T, ID>> {
        self.list.pop_front()
    }
}

impl<T: ?Sized + ListItem<ID>, const ID: u64> FusedIterator for IntoIter<T, ID> {}

impl<T: ?Sized + ListItem<ID>, const ID: u64> DoubleEndedIterator for IntoIter<T, ID> {
    fn next_back(&mut self) -> Option<ListArc<T, ID>> {
        self.list.pop_back()
    }
}

impl<T: ?Sized + ListItem<ID>, const ID: u64> IntoIterator for List<T, ID> {
    type IntoIter = IntoIter<T, ID>;
    type Item = ListArc<T, ID>;

    fn into_iter(self) -> IntoIter<T, ID> {
        IntoIter { list: self }
    }
}
