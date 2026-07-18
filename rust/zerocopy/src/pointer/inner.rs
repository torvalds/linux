// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2024 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

use core::{marker::PhantomData, ops::Range, ptr::NonNull};

pub use _def::PtrInner;

#[allow(unused_imports)]
use crate::util::polyfills::NumExt as _;
use crate::{
    layout::{CastType, MetadataCastError},
    pointer::cast,
    util::AsAddress,
    AlignmentError, CastError, KnownLayout, MetadataOf, SizeError, SplitAt,
};

mod _def {
    use super::*;
    /// The inner pointer stored inside a [`Ptr`][crate::Ptr].
    ///
    /// `PtrInner<'a, T>` is [covariant] in `'a` and invariant in `T`.
    ///
    /// [covariant]: https://doc.rust-lang.org/reference/subtyping.html
    #[allow(missing_debug_implementations)]
    pub struct PtrInner<'a, T>
    where
        T: ?Sized,
    {
        /// # Invariants
        ///
        /// 0. If `ptr`'s referent is not zero sized, then `ptr` has valid
        ///    provenance for its referent, which is entirely contained in some
        ///    Rust allocation, `A`.
        /// 1. If `ptr`'s referent is not zero sized, `A` is guaranteed to live
        ///    for at least `'a`.
        ///
        /// # Postconditions
        ///
        /// By virtue of these invariants, code may assume the following, which
        /// are logical implications of the invariants:
        /// - `ptr`'s referent is not larger than `isize::MAX` bytes \[1\]
        /// - `ptr`'s referent does not wrap around the address space \[1\]
        ///
        /// \[1\] Per <https://doc.rust-lang.org/1.85.0/std/ptr/index.html#allocated-object>:
        ///
        ///   For any allocated object with `base` address, `size`, and a set of
        ///   `addresses`, the following are guaranteed:
        ///   ...
        ///   - `size <= isize::MAX`
        ///
        ///   As a consequence of these guarantees, given any address `a` within
        ///   the set of addresses of an allocated object:
        ///   ...
        ///   - It is guaranteed that, given `o = a - base` (i.e., the offset of
        ///     `a` within the allocated object), `base + o` will not wrap
        ///     around the address space (in other words, will not overflow
        ///     `usize`)
        ptr: NonNull<T>,
        // SAFETY: `&'a UnsafeCell<T>` is covariant in `'a` and invariant in `T`
        // [1]. We use this construction rather than the equivalent `&mut T`,
        // because our MSRV of 1.65 prohibits `&mut` types in const contexts.
        //
        // [1] https://doc.rust-lang.org/1.81.0/reference/subtyping.html#variance
        _marker: PhantomData<&'a core::cell::UnsafeCell<T>>,
    }

    impl<'a, T: 'a + ?Sized> Copy for PtrInner<'a, T> {}
    impl<'a, T: 'a + ?Sized> Clone for PtrInner<'a, T> {
        #[inline(always)]
        fn clone(&self) -> PtrInner<'a, T> {
            // SAFETY: None of the invariants on `ptr` are affected by having
            // multiple copies of a `PtrInner`.
            *self
        }
    }

    impl<'a, T: 'a + ?Sized> PtrInner<'a, T> {
        /// Constructs a `Ptr` from a [`NonNull`].
        ///
        /// # Safety
        ///
        /// The caller promises that:
        ///
        /// 0. If `ptr`'s referent is not zero sized, then `ptr` has valid
        ///    provenance for its referent, which is entirely contained in some
        ///    Rust allocation, `A`.
        /// 1. If `ptr`'s referent is not zero sized, `A` is guaranteed to live
        ///    for at least `'a`.
        #[inline(always)]
        #[must_use]
        pub const unsafe fn new(ptr: NonNull<T>) -> PtrInner<'a, T> {
            // SAFETY: The caller has promised to satisfy all safety invariants
            // of `PtrInner`.
            Self { ptr, _marker: PhantomData }
        }

        /// Converts this `PtrInner<T>` to a [`NonNull<T>`].
        ///
        /// Note that this method does not consume `self`. The caller should
        /// watch out for `unsafe` code which uses the returned `NonNull` in a
        /// way that violates the safety invariants of `self`.
        #[inline(always)]
        #[must_use]
        pub const fn as_non_null(&self) -> NonNull<T> {
            self.ptr
        }

        /// Converts this `PtrInner<T>` to a [`*mut T`].
        ///
        /// Note that this method does not consume `self`. The caller should
        /// watch out for `unsafe` code which uses the returned `*mut T` in a
        /// way that violates the safety invariants of `self`.
        #[inline(always)]
        #[must_use]
        pub const fn as_ptr(&self) -> *mut T {
            self.ptr.as_ptr()
        }
    }
}

impl<'a, T: ?Sized> PtrInner<'a, T> {
    /// Constructs a `PtrInner` from a reference.
    #[inline]
    pub fn from_ref(ptr: &'a T) -> Self {
        let ptr = NonNull::from(ptr);
        // SAFETY:
        // 0. If `ptr`'s referent is not zero sized, then `ptr`, by invariant on
        //    `&'a T` [1], has valid provenance for its referent, which is
        //    entirely contained in some Rust allocation, `A`.
        // 1. If `ptr`'s referent is not zero sized, then `A`, by invariant on
        //    `&'a T`, is guaranteed to live for at least `'a`.
        //
        // [1] Per https://doc.rust-lang.org/1.85.0/std/primitive.reference.html#safety:
        //
        //   For all types, `T: ?Sized`, and for all `t: &T` or `t: &mut T`,
        //   when such values cross an API boundary, the following invariants
        //   must generally be upheld:
        //   ...
        //   - if `size_of_val(t) > 0`, then `t` is dereferenceable for
        //     `size_of_val(t)` many bytes
        //
        //   If `t` points at address `a`, being “dereferenceable” for N bytes
        //   means that the memory range `[a, a + N)` is all contained within a
        //   single allocated object.
        unsafe { Self::new(ptr) }
    }

    /// Constructs a `PtrInner` from a mutable reference.
    #[inline]
    pub fn from_mut(ptr: &'a mut T) -> Self {
        let ptr = NonNull::from(ptr);
        // SAFETY:
        // 0. If `ptr`'s referent is not zero sized, then `ptr`, by invariant on
        //    `&'a mut T` [1], has valid provenance for its referent, which is
        //    entirely contained in some Rust allocation, `A`.
        // 1. If `ptr`'s referent is not zero sized, then `A`, by invariant on
        //    `&'a mut T`, is guaranteed to live for at least `'a`.
        //
        // [1] Per https://doc.rust-lang.org/1.85.0/std/primitive.reference.html#safety:
        //
        //   For all types, `T: ?Sized`, and for all `t: &T` or `t: &mut T`,
        //   when such values cross an API boundary, the following invariants
        //   must generally be upheld:
        //   ...
        //   - if `size_of_val(t) > 0`, then `t` is dereferenceable for
        //     `size_of_val(t)` many bytes
        //
        //   If `t` points at address `a`, being “dereferenceable” for N bytes
        //   means that the memory range `[a, a + N)` is all contained within a
        //   single allocated object.
        unsafe { Self::new(ptr) }
    }

    /// # Safety
    ///
    /// The caller may assume that the resulting `PtrInner` addresses the subset
    /// of the bytes of `self`'s referent addressed by `C::project(self)`.
    #[must_use]
    #[inline(always)]
    pub fn project<U: ?Sized, C: cast::Project<T, U>>(self) -> PtrInner<'a, U> {
        let projected_raw = C::project(self);

        // SAFETY: `self`'s referent lives at a `NonNull` address, and is either
        // zero-sized or lives in an allocation. In either case, it does not
        // wrap around the address space [1], and so none of the addresses
        // contained in it or one-past-the-end of it are null.
        //
        // By invariant on `C: Project`, `C::project` is a provenance-preserving
        // projection which preserves or shrinks the set of referent bytes, so
        // `projected_raw` references a subset of `self`'s referent, and so it
        // cannot be null.
        //
        // [1] https://doc.rust-lang.org/1.92.0/std/ptr/index.html#allocation
        let projected_non_null = unsafe { NonNull::new_unchecked(projected_raw) };

        // SAFETY: As described in the preceding safety comment, `projected_raw`,
        // and thus `projected_non_null`, addresses a subset of `self`'s
        // referent. Thus, `projected_non_null` either:
        // - Addresses zero bytes or,
        // - Addresses a subset of the referent of `self`. In this case, `self`
        //   has provenance for its referent, which lives in an allocation.
        //   Since `projected_non_null` was constructed using a sequence of
        //   provenance-preserving operations, it also has provenance for its
        //   referent and that referent lives in an allocation. By invariant on
        //   `self`, that allocation lives for `'a`.
        unsafe { PtrInner::new(projected_non_null) }
    }
}

#[allow(clippy::needless_lifetimes)]
impl<'a, T> PtrInner<'a, T>
where
    T: ?Sized + KnownLayout,
{
    /// Extracts the metadata of this `ptr`.
    #[inline]
    #[must_use]
    pub fn meta(self) -> MetadataOf<T> {
        let meta = T::pointer_to_metadata(self.as_ptr());
        // SAFETY: By invariant on `PtrInner`, `self.as_non_null()` addresses no
        // more than `isize::MAX` bytes.
        unsafe { MetadataOf::new_unchecked(meta) }
    }

    /// Produces a `PtrInner` with the same address and provenance as `self` but
    /// the given `meta`.
    ///
    /// # Safety
    ///
    /// The caller promises that if `self`'s referent is not zero sized, then
    /// a pointer constructed from its address with the given `meta` metadata
    /// will address a subset of the allocation pointed to by `self`.
    #[inline]
    #[must_use]
    pub unsafe fn with_meta(self, meta: T::PointerMetadata) -> Self
    where
        T: KnownLayout,
    {
        let raw = T::raw_from_ptr_len(self.as_non_null().cast(), meta);

        // SAFETY:
        //
        // Lemma 0: `raw` either addresses zero bytes, or addresses a subset of
        //          the allocation pointed to by `self` and has the same
        //          provenance as `self`. Proof: `raw` is constructed using
        //          provenance-preserving operations, and the caller has
        //          promised that, if `self`'s referent is not zero-sized, the
        //          resulting pointer addresses a subset of the allocation
        //          pointed to by `self`.
        //
        // 0. Per Lemma 0 and by invariant on `self`, if `ptr`'s referent is not
        //    zero sized, then `ptr` is derived from some valid Rust allocation,
        //    `A`.
        // 1. Per Lemma 0 and by invariant on `self`, if `ptr`'s referent is not
        //    zero sized, then `ptr` has valid provenance for `A`.
        // 2. Per Lemma 0 and by invariant on `self`, if `ptr`'s referent is not
        //    zero sized, then `ptr` addresses a byte range which is entirely
        //    contained in `A`.
        // 3. Per Lemma 0 and by invariant on `self`, `ptr` addresses a byte
        //    range whose length fits in an `isize`.
        // 4. Per Lemma 0 and by invariant on `self`, `ptr` addresses a byte
        //    range which does not wrap around the address space.
        // 5. Per Lemma 0 and by invariant on `self`, if `ptr`'s referent is not
        //    zero sized, then `A` is guaranteed to live for at least `'a`.
        unsafe { PtrInner::new(raw) }
    }
}

#[allow(clippy::needless_lifetimes)]
impl<'a, T> PtrInner<'a, T>
where
    T: ?Sized + KnownLayout<PointerMetadata = usize>,
{
    /// Splits `T` in two.
    ///
    /// # Safety
    ///
    /// The caller promises that:
    ///  - `l_len.get() <= self.meta()`.
    ///
    /// ## (Non-)Overlap
    ///
    /// Given `let (left, right) = ptr.split_at(l_len)`, it is guaranteed that
    /// `left` and `right` are contiguous and non-overlapping if
    /// `l_len.padding_needed_for() == 0`. This is true for all `[T]`.
    ///
    /// If `l_len.padding_needed_for() != 0`, then the left pointer will overlap
    /// the right pointer to satisfy `T`'s padding requirements.
    #[inline]
    #[must_use]
    pub unsafe fn split_at_unchecked(
        self,
        l_len: crate::util::MetadataOf<T>,
    ) -> (Self, PtrInner<'a, [T::Elem]>)
    where
        T: SplitAt,
    {
        let l_len = l_len.get();

        // SAFETY: The caller promises that `l_len.get() <= self.meta()`.
        // Trivially, `0 <= l_len`.
        let left = unsafe { self.with_meta(l_len) };

        let right = self.trailing_slice();
        // SAFETY: The caller promises that `l_len <= self.meta() = slf.meta()`.
        // Trivially, `slf.meta() <= slf.meta()`.
        let right = unsafe { right.slice_unchecked(l_len..self.meta().get()) };

        // SAFETY: If `l_len.padding_needed_for() == 0`, then `left` and `right`
        // are non-overlapping. Proof: `left` is constructed `slf` with `l_len`
        // as its (exclusive) upper bound. If `l_len.padding_needed_for() == 0`,
        // then `left` requires no trailing padding following its final element.
        // Since `right` is constructed from `slf`'s trailing slice with `l_len`
        // as its (inclusive) lower bound, no byte is referred to by both
        // pointers.
        //
        // Conversely, `l_len.padding_needed_for() == N`, where `N
        // > 0`, `left` requires `N` bytes of trailing padding following its
        // final element. Since `right` is constructed from the trailing slice
        // of `slf` with `l_len` as its (inclusive) lower bound, the first `N`
        // bytes of `right` are aliased by `left`.
        (left, right)
    }

    /// Produces the trailing slice of `self`.
    #[inline]
    #[must_use]
    pub fn trailing_slice(self) -> PtrInner<'a, [T::Elem]>
    where
        T: SplitAt,
    {
        let offset = crate::trailing_slice_layout::<T>().offset;

        let bytes = self.as_non_null().cast::<u8>().as_ptr();

        // SAFETY:
        // - By invariant on `T: KnownLayout`, `T::LAYOUT` describes `T`'s
        //   layout. `offset` is the offset of the trailing slice within `T`,
        //   which is by definition in-bounds or one byte past the end of any
        //   `T`, regardless of metadata. By invariant on `PtrInner`, `self`
        //   (and thus `bytes`) points to a byte range of size `<= isize::MAX`,
        //   and so `offset <= isize::MAX`. Since `size_of::<u8>() == 1`,
        //   `offset * size_of::<u8>() <= isize::MAX`.
        // - If `offset > 0`, then by invariant on `PtrInner`, `self` (and thus
        //   `bytes`) points to a byte range entirely contained within the same
        //   allocated object as `self`. As explained above, this offset results
        //   in a pointer to or one byte past the end of this allocated object.
        let bytes = unsafe { bytes.add(offset) };

        // SAFETY: By the preceding safety argument, `bytes` is within or one
        // byte past the end of the same allocated object as `self`, which
        // ensures that it is non-null.
        let bytes = unsafe { NonNull::new_unchecked(bytes) };

        let ptr = KnownLayout::raw_from_ptr_len(bytes, self.meta().get());

        // SAFETY:
        // 0. If `ptr`'s referent is not zero sized, then `ptr` is derived from
        //    some valid Rust allocation, `A`, because `ptr` is derived from
        //    the same allocated object as `self`.
        // 1. If `ptr`'s referent is not zero sized, then `ptr` has valid
        //    provenance for `A` because `raw` is derived from the same
        //    allocated object as `self` via provenance-preserving operations.
        // 2. If `ptr`'s referent is not zero sized, then `ptr` addresses a byte
        //    range which is entirely contained in `A`, by previous safety proof
        //    on `bytes`.
        // 3. `ptr` addresses a byte range whose length fits in an `isize`, by
        //    consequence of #2.
        // 4. `ptr` addresses a byte range which does not wrap around the
        //    address space, by consequence of #2.
        // 5. If `ptr`'s referent is not zero sized, then `A` is guaranteed to
        //    live for at least `'a`, because `ptr` is derived from `self`.
        unsafe { PtrInner::new(ptr) }
    }
}

#[allow(clippy::needless_lifetimes)]
impl<'a, T> PtrInner<'a, [T]> {
    /// Creates a pointer which addresses the given `range` of self.
    ///
    /// # Safety
    ///
    /// `range` is a valid range (`start <= end`) and `end <= self.meta()`.
    #[inline]
    #[must_use]
    pub unsafe fn slice_unchecked(self, range: Range<usize>) -> Self {
        let base = self.as_non_null().cast::<T>().as_ptr();

        // SAFETY: The caller promises that `start <= end <= self.meta()`. By
        // invariant, if `self`'s referent is not zero-sized, then `self` refers
        // to a byte range which is contained within a single allocation, which
        // is no more than `isize::MAX` bytes long, and which does not wrap
        // around the address space. Thus, this pointer arithmetic remains
        // in-bounds of the same allocation, and does not wrap around the
        // address space. The offset (in bytes) does not overflow `isize`.
        //
        // If `self`'s referent is zero-sized, then these conditions are
        // trivially satisfied.
        let base = unsafe { base.add(range.start) };

        // SAFETY: The caller promises that `start <= end`, and so this will not
        // underflow.
        #[allow(unstable_name_collisions)]
        let len = unsafe { range.end.unchecked_sub(range.start) };

        let ptr = core::ptr::slice_from_raw_parts_mut(base, len);

        // SAFETY: By invariant, `self`'s referent is either a ZST or lives
        // entirely in an allocation. `ptr` points inside of or one byte past
        // the end of that referent. Thus, in either case, `ptr` is non-null.
        let ptr = unsafe { NonNull::new_unchecked(ptr) };

        // SAFETY:
        //
        // Lemma 0: `ptr` addresses a subset of the bytes addressed by `self`,
        //          and has the same provenance. Proof: The caller guarantees
        //          that `start <= end <= self.meta()`. Thus, `base` is
        //          in-bounds of `self`, and `base + (end - start)` is also
        //          in-bounds of self. Finally, `ptr` is constructed using
        //          provenance-preserving operations.
        //
        // 0. Per Lemma 0 and by invariant on `self`, if `ptr`'s referent is not
        //    zero sized, then `ptr` has valid provenance for its referent,
        //    which is entirely contained in some Rust allocation, `A`.
        // 1. Per Lemma 0 and by invariant on `self`, if `ptr`'s referent is not
        //    zero sized, then `A` is guaranteed to live for at least `'a`.
        unsafe { PtrInner::new(ptr) }
    }

    /// Iteratively projects the elements `PtrInner<T>` from `PtrInner<[T]>`.
    #[inline]
    pub fn iter(&self) -> impl Iterator<Item = PtrInner<'a, T>> {
        // FIXME(#429): Once `NonNull::cast` documents that it preserves
        // provenance, cite those docs.
        let base = self.as_non_null().cast::<T>().as_ptr();
        (0..self.meta().get()).map(move |i| {
            // FIXME(https://github.com/rust-lang/rust/issues/74265): Use
            // `NonNull::get_unchecked_mut`.

            // SAFETY: If the following conditions are not satisfied
            // `pointer::cast` may induce Undefined Behavior [1]:
            //
            // > - The computed offset, `count * size_of::<T>()` bytes, must not
            // >   overflow `isize``.
            // > - If the computed offset is non-zero, then `self` must be
            // >   derived from a pointer to some allocated object, and the
            // >   entire memory range between `self` and the result must be in
            // >   bounds of that allocated object. In particular, this range
            // >   must not “wrap around” the edge of the address space.
            //
            // [1] https://doc.rust-lang.org/std/primitive.pointer.html#method.add
            //
            // We satisfy both of these conditions here:
            // - By invariant on `Ptr`, `self` addresses a byte range whose
            //   length fits in an `isize`. Since `elem` is contained in `self`,
            //   the computed offset of `elem` must fit within `isize.`
            // - If the computed offset is non-zero, then this means that the
            //   referent is not zero-sized. In this case, `base` points to an
            //   allocated object (by invariant on `self`). Thus:
            //   - By contract, `self.meta()` accurately reflects the number of
            //     elements in the slice. `i` is in bounds of `c.meta()` by
            //     construction, and so the result of this addition cannot
            //     overflow past the end of the allocation referred to by `c`.
            //   - By invariant on `Ptr`, `self` addresses a byte range which
            //     does not wrap around the address space. Since `elem` is
            //     contained in `self`, the computed offset of `elem` must wrap
            //     around the address space.
            //
            // FIXME(#429): Once `pointer::add` documents that it preserves
            // provenance, cite those docs.
            let elem = unsafe { base.add(i) };

            // SAFETY: `elem` must not be null. `base` is constructed from a
            // `NonNull` pointer, and the addition that produces `elem` must not
            // overflow or wrap around, so `elem >= base > 0`.
            //
            // FIXME(#429): Once `NonNull::new_unchecked` documents that it
            // preserves provenance, cite those docs.
            let elem = unsafe { NonNull::new_unchecked(elem) };

            // SAFETY: The safety invariants of `Ptr::new` (see definition) are
            // satisfied:
            // 0. If `elem`'s referent is not zero sized, then `elem` has valid
            //    provenance for its referent, because it derived from `self`
            //    using a series of provenance-preserving operations, and
            //    because `self` has valid provenance for its referent. By the
            //    same argument, `elem`'s referent is entirely contained within
            //    the same allocated object as `self`'s referent.
            // 1. If `elem`'s referent is not zero sized, then the allocation of
            //    `elem` is guaranteed to live for at least `'a`, because `elem`
            //    is entirely contained in `self`, which lives for at least `'a`
            //    by invariant on `Ptr`.
            unsafe { PtrInner::new(elem) }
        })
    }
}

impl<'a, T, const N: usize> PtrInner<'a, [T; N]> {
    /// Casts this pointer-to-array into a slice.
    ///
    /// # Safety
    ///
    /// Callers may assume that the returned `PtrInner` references the same
    /// address and length as `self`.
    #[allow(clippy::wrong_self_convention)]
    #[inline]
    #[must_use]
    pub fn as_slice(self) -> PtrInner<'a, [T]> {
        let start = self.as_non_null().cast::<T>().as_ptr();
        let slice = core::ptr::slice_from_raw_parts_mut(start, N);
        // SAFETY: `slice` is not null, because it is derived from `start`
        // which is non-null.
        let slice = unsafe { NonNull::new_unchecked(slice) };
        // SAFETY: Lemma: In the following safety arguments, note that `slice`
        // is derived from `self` in two steps: first, by casting `self: [T; N]`
        // to `start: T`, then by constructing a pointer to a slice starting at
        // `start` of length `N`. As a result, `slice` references exactly the
        // same allocation as `self`, if any.
        //
        // 0. By the above lemma, if `slice`'s referent is not zero sized, then
        //    `slice` has the same referent as `self`. By invariant on `self`,
        //    this referent is entirely contained within some allocation, `A`.
        //    Because `slice` was constructed using provenance-preserving
        //    operations, it has provenance for its entire referent.
        // 1. By the above lemma, if `slice`'s referent is not zero sized, then
        //    `A` is guaranteed to live for at least `'a`, because it is derived
        //    from the same allocation as `self`, which, by invariant on
        //    `PtrInner`, lives for at least `'a`.
        unsafe { PtrInner::new(slice) }
    }
}

impl<'a> PtrInner<'a, [u8]> {
    /// Attempts to cast `self` to a `U` using the given cast type.
    ///
    /// If `U` is a slice DST and pointer metadata (`meta`) is provided, then
    /// the cast will only succeed if it would produce an object with the given
    /// metadata.
    ///
    /// Returns `None` if the resulting `U` would be invalidly-aligned, if no
    /// `U` can fit in `self`, or if the provided pointer metadata describes an
    /// invalid instance of `U`. On success, returns a pointer to the
    /// largest-possible `U` which fits in `self`.
    ///
    /// # Safety
    ///
    /// The caller may assume that this implementation is correct, and may rely
    /// on that assumption for the soundness of their code. In particular, the
    /// caller may assume that, if `try_cast_into` returns `Some((ptr,
    /// remainder))`, then `ptr` and `remainder` refer to non-overlapping byte
    /// ranges within `self`, and that `ptr` and `remainder` entirely cover
    /// `self`. Finally:
    /// - If this is a prefix cast, `ptr` has the same address as `self`.
    /// - If this is a suffix cast, `remainder` has the same address as `self`.
    #[inline]
    pub fn try_cast_into<U>(
        self,
        cast_type: CastType,
        meta: Option<U::PointerMetadata>,
    ) -> Result<(PtrInner<'a, U>, PtrInner<'a, [u8]>), CastError<Self, U>>
    where
        U: 'a + ?Sized + KnownLayout,
    {
        // PANICS: By invariant, the byte range addressed by
        // `self.as_non_null()` does not wrap around the address space. This
        // implies that the sum of the address (represented as a `usize`) and
        // length do not overflow `usize`, as required by
        // `validate_cast_and_convert_metadata`. Thus, this call to
        // `validate_cast_and_convert_metadata` will only panic if `U` is a DST
        // whose trailing slice element is zero-sized.
        let maybe_metadata = MetadataOf::<U>::validate_cast_and_convert_metadata(
            AsAddress::addr(self.as_ptr()),
            self.meta(),
            cast_type,
            meta,
        );

        let (elems, split_at) = match maybe_metadata {
            Ok((elems, split_at)) => (elems, split_at),
            Err(MetadataCastError::Alignment) => {
                // SAFETY: Since `validate_cast_and_convert_metadata` returned
                // an alignment error, `U` must have an alignment requirement
                // greater than one.
                let err = unsafe { AlignmentError::<_, U>::new_unchecked(self) };
                return Err(CastError::Alignment(err));
            }
            Err(MetadataCastError::Size) => return Err(CastError::Size(SizeError::new(self))),
        };

        // SAFETY: `validate_cast_and_convert_metadata` promises to return
        // `split_at <= self.meta()`.
        //
        // Lemma 0: `l_slice` and `r_slice` are non-overlapping. Proof: By
        // contract on `PtrInner::split_at_unchecked`, the produced `PtrInner`s
        // are always non-overlapping if `self` is a `[T]`; here it is a `[u8]`.
        let (l_slice, r_slice) = unsafe { self.split_at_unchecked(split_at) };

        let (target, remainder) = match cast_type {
            CastType::Prefix => (l_slice, r_slice),
            CastType::Suffix => (r_slice, l_slice),
        };

        let base = target.as_non_null().cast::<u8>();

        let ptr = U::raw_from_ptr_len(base, elems.get());

        // SAFETY:
        // 0. By invariant, if `target`'s referent is not zero sized, then
        //    `target` has provenance valid for some Rust allocation, `A`.
        //    Because `ptr` is derived from `target` via provenance-preserving
        //    operations, `ptr` will also have provenance valid for its entire
        //    referent.
        // 1. `validate_cast_and_convert_metadata` promises that the object
        //    described by `elems` and `split_at` lives at a byte range which is
        //    a subset of the input byte range. Thus, by invariant, if
        //    `target`'s referent is not zero sized, then `target` refers to an
        //    allocation which is guaranteed to live for at least `'a`, and thus
        //    so does `ptr`.
        Ok((unsafe { PtrInner::new(ptr) }, remainder))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::*;

    #[test]
    fn test_meta() {
        let arr = [1; 16];
        let dst = <[u8]>::ref_from_bytes(&arr[..]).unwrap();
        let ptr = PtrInner::from_ref(dst);
        assert_eq!(ptr.meta().get(), 16);

        // SAFETY: 8 is less than 16
        let ptr = unsafe { ptr.with_meta(8) };

        assert_eq!(ptr.meta().get(), 8);
    }

    #[test]
    fn test_split_at() {
        fn test_split_at<const OFFSET: usize, const BUFFER_SIZE: usize>() {
            #[derive(FromBytes, KnownLayout, SplitAt, Immutable)]
            #[repr(C)]
            struct SliceDst<const OFFSET: usize> {
                prefix: [u8; OFFSET],
                trailing: [u8],
            }

            let n: usize = BUFFER_SIZE - OFFSET;
            let arr = [1; BUFFER_SIZE];
            let dst = SliceDst::<OFFSET>::ref_from_bytes(&arr[..]).unwrap();
            let ptr = PtrInner::from_ref(dst);
            for i in 0..=n {
                assert_eq!(ptr.meta().get(), n);
                // SAFETY: `i` is in bounds by construction.
                let i = unsafe { MetadataOf::new_unchecked(i) };
                // SAFETY: `i` is in bounds by construction.
                let (l, r) = unsafe { ptr.split_at_unchecked(i) };
                // SAFETY: Points to a valid value by construction.
                #[allow(clippy::undocumented_unsafe_blocks, clippy::as_conversions)]
                // Clippy false positive
                let l_sum: usize = l
                    .trailing_slice()
                    .iter()
                    .map(
                        #[inline(always)]
                        |ptr| unsafe { core::ptr::read_unaligned(ptr.as_ptr()) } as usize,
                    )
                    .sum();
                // SAFETY: Points to a valid value by construction.
                #[allow(clippy::undocumented_unsafe_blocks, clippy::as_conversions)]
                // Clippy false positive
                let r_sum: usize = r
                    .iter()
                    .map(
                        #[inline(always)]
                        |ptr| unsafe { core::ptr::read_unaligned(ptr.as_ptr()) } as usize,
                    )
                    .sum();
                assert_eq!(l_sum, i.get());
                assert_eq!(r_sum, n - i.get());
                assert_eq!(l_sum + r_sum, n);
            }
        }

        test_split_at::<0, 16>();
        test_split_at::<1, 17>();
        test_split_at::<2, 18>();
    }

    #[test]
    fn test_trailing_slice() {
        fn test_trailing_slice<const OFFSET: usize, const BUFFER_SIZE: usize>() {
            #[derive(FromBytes, KnownLayout, SplitAt, Immutable)]
            #[repr(C)]
            struct SliceDst<const OFFSET: usize> {
                prefix: [u8; OFFSET],
                trailing: [u8],
            }

            let n: usize = BUFFER_SIZE - OFFSET;
            let arr = [1; BUFFER_SIZE];
            let dst = SliceDst::<OFFSET>::ref_from_bytes(&arr[..]).unwrap();
            let ptr = PtrInner::from_ref(dst);

            assert_eq!(ptr.meta().get(), n);
            let trailing = ptr.trailing_slice();
            assert_eq!(trailing.meta().get(), n);

            assert_eq!(
                // SAFETY: We assume this to be sound for the sake of this test,
                // which will fail, here, in miri, if the safety precondition of
                // `offset_of` is not satisfied.
                unsafe {
                    #[allow(clippy::as_conversions)]
                    let offset = (trailing.as_ptr() as *mut u8).offset_from(ptr.as_ptr() as *mut _);
                    offset
                },
                isize::try_from(OFFSET).unwrap(),
            );

            // SAFETY: Points to a valid value by construction.
            #[allow(clippy::undocumented_unsafe_blocks, clippy::as_conversions)]
            // Clippy false positive
            let trailing: usize = trailing
                .iter()
                .map(|ptr| unsafe { core::ptr::read_unaligned(ptr.as_ptr()) } as usize)
                .sum();

            assert_eq!(trailing, n);
        }

        test_trailing_slice::<0, 16>();
        test_trailing_slice::<1, 17>();
        test_trailing_slice::<2, 18>();
    }
    #[test]
    fn test_ptr_inner_clone() {
        let mut x = 0u8;
        let p = PtrInner::from_mut(&mut x);
        #[allow(clippy::clone_on_copy)]
        let p2 = p.clone();
        assert_eq!(p.as_non_null(), p2.as_non_null());
    }
}
