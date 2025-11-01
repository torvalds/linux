// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Helpers for implementing list traits safely.

/// Declares that this type has a [`ListLinks<ID>`] field.
///
/// This trait is only used to help implement [`ListItem`] safely. If [`ListItem`] is implemented
/// manually, then this trait is not needed. Use the [`impl_has_list_links!`] macro to implement
/// this trait.
///
/// # Safety
///
/// The methods on this trait must have exactly the behavior that the definitions given below have.
///
/// [`ListLinks<ID>`]: crate::list::ListLinks
/// [`ListItem`]: crate::list::ListItem
pub unsafe trait HasListLinks<const ID: u64 = 0> {
    /// Returns a pointer to the [`ListLinks<ID>`] field.
    ///
    /// # Safety
    ///
    /// The provided pointer must point at a valid struct of type `Self`.
    ///
    /// [`ListLinks<ID>`]: crate::list::ListLinks
    unsafe fn raw_get_list_links(ptr: *mut Self) -> *mut crate::list::ListLinks<ID>;
}

/// Implements the [`HasListLinks`] trait for the given type.
#[macro_export]
macro_rules! impl_has_list_links {
    ($(impl$({$($generics:tt)*})?
       HasListLinks$(<$id:tt>)?
       for $self:ty
       { self$(.$field:ident)* }
    )*) => {$(
        // SAFETY: The implementation of `raw_get_list_links` only compiles if the field has the
        // right type.
        unsafe impl$(<$($generics)*>)? $crate::list::HasListLinks$(<$id>)? for $self {
            #[inline]
            unsafe fn raw_get_list_links(ptr: *mut Self) -> *mut $crate::list::ListLinks$(<$id>)? {
                // Statically ensure that `$(.field)*` doesn't follow any pointers.
                //
                // Cannot be `const` because `$self` may contain generics and E0401 says constants
                // "can't use {`Self`,generic parameters} from outer item".
                if false { let _: usize = ::core::mem::offset_of!(Self, $($field).*); }

                // SAFETY: The caller promises that the pointer is not dangling. We know that this
                // expression doesn't follow any pointers, as the `offset_of!` invocation above
                // would otherwise not compile.
                unsafe { ::core::ptr::addr_of_mut!((*ptr)$(.$field)*) }
            }
        }
    )*};
}
pub use impl_has_list_links;

/// Declares that the [`ListLinks<ID>`] field in this struct is inside a
/// [`ListLinksSelfPtr<T, ID>`].
///
/// # Safety
///
/// The [`ListLinks<ID>`] field of this struct at [`HasListLinks<ID>::raw_get_list_links`] must be
/// inside a [`ListLinksSelfPtr<T, ID>`].
///
/// [`ListLinks<ID>`]: crate::list::ListLinks
/// [`ListLinksSelfPtr<T, ID>`]: crate::list::ListLinksSelfPtr
pub unsafe trait HasSelfPtr<T: ?Sized, const ID: u64 = 0>
where
    Self: HasListLinks<ID>,
{
}

/// Implements the [`HasListLinks`] and [`HasSelfPtr`] traits for the given type.
#[macro_export]
macro_rules! impl_has_list_links_self_ptr {
    ($(impl$({$($generics:tt)*})?
       HasSelfPtr<$item_type:ty $(, $id:tt)?>
       for $self:ty
       { self$(.$field:ident)* }
    )*) => {$(
        // SAFETY: The implementation of `raw_get_list_links` only compiles if the field has the
        // right type.
        unsafe impl$(<$($generics)*>)? $crate::list::HasSelfPtr<$item_type $(, $id)?> for $self {}

        unsafe impl$(<$($generics)*>)? $crate::list::HasListLinks$(<$id>)? for $self {
            #[inline]
            unsafe fn raw_get_list_links(ptr: *mut Self) -> *mut $crate::list::ListLinks$(<$id>)? {
                // SAFETY: The caller promises that the pointer is not dangling.
                let ptr: *mut $crate::list::ListLinksSelfPtr<$item_type $(, $id)?> =
                    unsafe { ::core::ptr::addr_of_mut!((*ptr)$(.$field)*) };
                ptr.cast()
            }
        }
    )*};
}
pub use impl_has_list_links_self_ptr;

/// Implements the [`ListItem`] trait for the given type.
///
/// Requires that the type implements [`HasListLinks`]. Use the [`impl_has_list_links!`] macro to
/// implement that trait.
///
/// [`ListItem`]: crate::list::ListItem
///
/// # Examples
///
/// ```
/// #[pin_data]
/// struct SimpleListItem {
///     value: u32,
///     #[pin]
///     links: kernel::list::ListLinks,
/// }
///
/// kernel::list::impl_list_arc_safe! {
///     impl ListArcSafe<0> for SimpleListItem { untracked; }
/// }
///
/// kernel::list::impl_list_item! {
///     impl ListItem<0> for SimpleListItem { using ListLinks { self.links }; }
/// }
///
/// struct ListLinksHolder {
///     inner: kernel::list::ListLinks,
/// }
///
/// #[pin_data]
/// struct ComplexListItem<T, U> {
///     value: Result<T, U>,
///     #[pin]
///     links: ListLinksHolder,
/// }
///
/// kernel::list::impl_list_arc_safe! {
///     impl{T, U} ListArcSafe<0> for ComplexListItem<T, U> { untracked; }
/// }
///
/// kernel::list::impl_list_item! {
///     impl{T, U} ListItem<0> for ComplexListItem<T, U> { using ListLinks { self.links.inner }; }
/// }
/// ```
///
/// ```
/// #[pin_data]
/// struct SimpleListItem {
///     value: u32,
///     #[pin]
///     links: kernel::list::ListLinksSelfPtr<SimpleListItem>,
/// }
///
/// kernel::list::impl_list_arc_safe! {
///     impl ListArcSafe<0> for SimpleListItem { untracked; }
/// }
///
/// kernel::list::impl_list_item! {
///     impl ListItem<0> for SimpleListItem { using ListLinksSelfPtr { self.links }; }
/// }
///
/// struct ListLinksSelfPtrHolder<T, U> {
///     inner: kernel::list::ListLinksSelfPtr<ComplexListItem<T, U>>,
/// }
///
/// #[pin_data]
/// struct ComplexListItem<T, U> {
///     value: Result<T, U>,
///     #[pin]
///     links: ListLinksSelfPtrHolder<T, U>,
/// }
///
/// kernel::list::impl_list_arc_safe! {
///     impl{T, U} ListArcSafe<0> for ComplexListItem<T, U> { untracked; }
/// }
///
/// kernel::list::impl_list_item! {
///     impl{T, U} ListItem<0> for ComplexListItem<T, U> {
///         using ListLinksSelfPtr { self.links.inner };
///     }
/// }
/// ```
#[macro_export]
macro_rules! impl_list_item {
    (
        $(impl$({$($generics:tt)*})? ListItem<$num:tt> for $self:ty {
            using ListLinks { self$(.$field:ident)* };
        })*
    ) => {$(
        $crate::list::impl_has_list_links! {
            impl$({$($generics)*})? HasListLinks<$num> for $self { self$(.$field)* }
        }

        // SAFETY: See GUARANTEES comment on each method.
        unsafe impl$(<$($generics)*>)? $crate::list::ListItem<$num> for $self {
            // GUARANTEES:
            // * This returns the same pointer as `prepare_to_insert` because `prepare_to_insert`
            //   is implemented in terms of `view_links`.
            // * By the type invariants of `ListLinks`, the `ListLinks` has two null pointers when
            //   this value is not in a list.
            unsafe fn view_links(me: *const Self) -> *mut $crate::list::ListLinks<$num> {
                // SAFETY: The caller guarantees that `me` points at a valid value of type `Self`.
                unsafe {
                    <Self as $crate::list::HasListLinks<$num>>::raw_get_list_links(me.cast_mut())
                }
            }

            // GUARANTEES:
            // * `me` originates from the most recent call to `prepare_to_insert`, which calls
            //   `raw_get_list_link`, which is implemented using `addr_of_mut!((*self)$(.$field)*)`.
            //   This method uses `container_of` to perform the inverse operation, so it returns the
            //   pointer originally passed to `prepare_to_insert`.
            // * The pointer remains valid until the next call to `post_remove` because the caller
            //   of the most recent call to `prepare_to_insert` promised to retain ownership of the
            //   `ListArc` containing `Self` until the next call to `post_remove`. The value cannot
            //   be destroyed while a `ListArc` reference exists.
            unsafe fn view_value(me: *mut $crate::list::ListLinks<$num>) -> *const Self {
                // SAFETY: `me` originates from the most recent call to `prepare_to_insert`, so it
                // points at the field `$field` in a value of type `Self`. Thus, reversing that
                // operation is still in-bounds of the allocation.
                $crate::container_of!(me, Self, $($field).*)
            }

            // GUARANTEES:
            // This implementation of `ListItem` will not give out exclusive access to the same
            // `ListLinks` several times because calls to `prepare_to_insert` and `post_remove`
            // must alternate and exclusive access is given up when `post_remove` is called.
            //
            // Other invocations of `impl_list_item!` also cannot give out exclusive access to the
            // same `ListLinks` because you can only implement `ListItem` once for each value of
            // `ID`, and the `ListLinks` fields only work with the specified `ID`.
            unsafe fn prepare_to_insert(me: *const Self) -> *mut $crate::list::ListLinks<$num> {
                // SAFETY: The caller promises that `me` points at a valid value.
                unsafe { <Self as $crate::list::ListItem<$num>>::view_links(me) }
            }

            // GUARANTEES:
            // * `me` originates from the most recent call to `prepare_to_insert`, which calls
            //   `raw_get_list_link`, which is implemented using `addr_of_mut!((*self)$(.$field)*)`.
            //   This method uses `container_of` to perform the inverse operation, so it returns the
            //   pointer originally passed to `prepare_to_insert`.
            unsafe fn post_remove(me: *mut $crate::list::ListLinks<$num>) -> *const Self {
                // SAFETY: `me` originates from the most recent call to `prepare_to_insert`, so it
                // points at the field `$field` in a value of type `Self`. Thus, reversing that
                // operation is still in-bounds of the allocation.
                $crate::container_of!(me, Self, $($field).*)
            }
        }
    )*};

    (
        $(impl$({$($generics:tt)*})? ListItem<$num:tt> for $self:ty {
            using ListLinksSelfPtr { self$(.$field:ident)* };
        })*
    ) => {$(
        $crate::list::impl_has_list_links_self_ptr! {
            impl$({$($generics)*})? HasSelfPtr<$self> for $self { self$(.$field)* }
        }

        // SAFETY: See GUARANTEES comment on each method.
        unsafe impl$(<$($generics)*>)? $crate::list::ListItem<$num> for $self {
            // GUARANTEES:
            // This implementation of `ListItem` will not give out exclusive access to the same
            // `ListLinks` several times because calls to `prepare_to_insert` and `post_remove`
            // must alternate and exclusive access is given up when `post_remove` is called.
            //
            // Other invocations of `impl_list_item!` also cannot give out exclusive access to the
            // same `ListLinks` because you can only implement `ListItem` once for each value of
            // `ID`, and the `ListLinks` fields only work with the specified `ID`.
            unsafe fn prepare_to_insert(me: *const Self) -> *mut $crate::list::ListLinks<$num> {
                // SAFETY: The caller promises that `me` points at a valid value of type `Self`.
                let links_field = unsafe { <Self as $crate::list::ListItem<$num>>::view_links(me) };

                let container = $crate::container_of!(
                    links_field, $crate::list::ListLinksSelfPtr<Self, $num>, inner
                );

                // SAFETY: By the same reasoning above, `links_field` is a valid pointer.
                let self_ptr = unsafe {
                    $crate::list::ListLinksSelfPtr::raw_get_self_ptr(container)
                };

                let cell_inner = $crate::types::Opaque::cast_into(self_ptr);

                // SAFETY: This value is not accessed in any other places than `prepare_to_insert`,
                // `post_remove`, or `view_value`. By the safety requirements of those methods,
                // none of these three methods may be called in parallel with this call to
                // `prepare_to_insert`, so this write will not race with any other access to the
                // value.
                unsafe { ::core::ptr::write(cell_inner, me) };

                links_field
            }

            // GUARANTEES:
            // * This returns the same pointer as `prepare_to_insert` because `prepare_to_insert`
            //   returns the return value of `view_links`.
            // * By the type invariants of `ListLinks`, the `ListLinks` has two null pointers when
            //   this value is not in a list.
            unsafe fn view_links(me: *const Self) -> *mut $crate::list::ListLinks<$num> {
                // SAFETY: The caller promises that `me` points at a valid value of type `Self`.
                unsafe {
                    <Self as $crate::list::HasListLinks<$num>>::raw_get_list_links(me.cast_mut())
                }
            }

            // This function is also used as the implementation of `post_remove`, so the caller
            // may choose to satisfy the safety requirements of `post_remove` instead of the safety
            // requirements for `view_value`.
            //
            // GUARANTEES: (always)
            // * This returns the same pointer as the one passed to the most recent call to
            //   `prepare_to_insert` since that call wrote that pointer to this location. The value
            //   is only modified in `prepare_to_insert`, so it has not been modified since the
            //   most recent call.
            //
            // GUARANTEES: (only when using the `view_value` safety requirements)
            // * The pointer remains valid until the next call to `post_remove` because the caller
            //   of the most recent call to `prepare_to_insert` promised to retain ownership of the
            //   `ListArc` containing `Self` until the next call to `post_remove`. The value cannot
            //   be destroyed while a `ListArc` reference exists.
            unsafe fn view_value(links_field: *mut $crate::list::ListLinks<$num>) -> *const Self {
                let container = $crate::container_of!(
                    links_field, $crate::list::ListLinksSelfPtr<Self, $num>, inner
                );

                // SAFETY: By the same reasoning above, `links_field` is a valid pointer.
                let self_ptr = unsafe {
                    $crate::list::ListLinksSelfPtr::raw_get_self_ptr(container)
                };

                let cell_inner = $crate::types::Opaque::cast_into(self_ptr);

                // SAFETY: This is not a data race, because the only function that writes to this
                // value is `prepare_to_insert`, but by the safety requirements the
                // `prepare_to_insert` method may not be called in parallel with `view_value` or
                // `post_remove`.
                unsafe { ::core::ptr::read(cell_inner) }
            }

            // GUARANTEES:
            // The first guarantee of `view_value` is exactly what `post_remove` guarantees.
            unsafe fn post_remove(me: *mut $crate::list::ListLinks<$num>) -> *const Self {
                // SAFETY: This specific implementation of `view_value` allows the caller to
                // promise the safety requirements of `post_remove` instead of the safety
                // requirements for `view_value`.
                unsafe { <Self as $crate::list::ListItem<$num>>::view_value(me) }
            }
        }
    )*};
}
pub use impl_list_item;
