// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2023 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

//! Abstractions over raw pointers.

#![allow(missing_docs)]

mod inner;
pub mod invariant;
mod ptr;
pub mod transmute;

pub use inner::PtrInner;
pub use invariant::{BecauseExclusive, BecauseImmutable, Read};
pub use ptr::{Ptr, TryWithError};
pub use transmute::*;

use crate::wrappers::ReadOnly;

/// A shorthand for a maybe-valid, maybe-aligned reference. Used as the argument
/// to [`TryFromBytes::is_bit_valid`].
///
/// [`TryFromBytes::is_bit_valid`]: crate::TryFromBytes::is_bit_valid
pub type Maybe<'a, T, Alignment = invariant::Unaligned> =
    Ptr<'a, ReadOnly<T>, (invariant::Shared, Alignment, invariant::Initialized)>;

/// Checks if the referent is zeroed.
pub(crate) fn is_zeroed<T, I>(ptr: Ptr<'_, T, I>) -> bool
where
    T: crate::Immutable + crate::KnownLayout,
    I: invariant::Invariants<Validity = invariant::Initialized>,
    I::Aliasing: invariant::Reference,
{
    ptr.as_bytes().as_ref().iter().all(
        #[inline(always)]
        |&byte| byte == 0,
    )
}

pub mod cast {
    use core::{marker::PhantomData, mem};

    use crate::{
        layout::{SizeInfo, TrailingSliceLayout},
        HasField, KnownLayout, PtrInner,
    };

    /// A pointer cast or projection.
    ///
    /// # Safety
    ///
    /// The implementation of `project` must satisfy its safety post-condition.
    pub unsafe trait Project<Src: ?Sized, Dst: ?Sized> {
        /// Projects a pointer from `Src` to `Dst`.
        ///
        /// Users should generally not call `project` directly, and instead
        /// should use high-level APIs like [`PtrInner::project`] or
        /// [`Ptr::project`].
        ///
        /// [`Ptr::project`]: crate::pointer::Ptr::project
        ///
        /// # Safety
        ///
        /// The returned pointer refers to a non-strict subset of the bytes of
        /// `src`'s referent, and has the same provenance as `src`.
        fn project(src: PtrInner<'_, Src>) -> *mut Dst;
    }

    /// A [`Project`] which preserves the address of the referent – a pointer
    /// cast.
    ///
    /// # Safety
    ///
    /// A `Cast` projection must preserve the address of the referent. It may
    /// shrink the set of referent bytes, and it may change the referent's type.
    pub unsafe trait Cast<Src: ?Sized, Dst: ?Sized>: Project<Src, Dst> {}

    /// A [`Cast`] which does not shrink the set of referent bytes.
    ///
    /// # Safety
    ///
    /// A `CastExact` projection must preserve the set of referent bytes.
    pub unsafe trait CastExact<Src: ?Sized, Dst: ?Sized>: Cast<Src, Dst> {}

    /// A no-op pointer cast.
    #[derive(Default, Copy, Clone)]
    #[allow(missing_debug_implementations)]
    pub struct IdCast;

    // SAFETY: `project` returns its argument unchanged, and so it is a
    // provenance-preserving projection which preserves the set of referent
    // bytes.
    unsafe impl<T: ?Sized> Project<T, T> for IdCast {
        #[inline(always)]
        fn project(src: PtrInner<'_, T>) -> *mut T {
            src.as_ptr()
        }
    }

    // SAFETY: The `Project::project` impl preserves referent address.
    unsafe impl<T: ?Sized> Cast<T, T> for IdCast {}

    // SAFETY: The `Project::project` impl preserves referent size.
    unsafe impl<T: ?Sized> CastExact<T, T> for IdCast {}

    /// A pointer cast which preserves or shrinks the set of referent bytes of
    /// a statically-sized referent.
    ///
    /// # Safety
    ///
    /// The implementation of [`Project`] uses a compile-time assertion to
    /// guarantee that `Dst` is no larger than `Src`. Thus, `CastSized` has a
    /// sound implementation of [`Project`] for all `Src` and `Dst` – the caller
    /// may pass any `Src` and `Dst` without being responsible for soundness.
    #[allow(missing_debug_implementations, missing_copy_implementations)]
    pub enum CastSized {}

    // SAFETY: By the `static_assert!`, `Dst` is no larger than `Src`,
    // and so all casts preserve or shrink the set of referent bytes. All
    // operations preserve provenance.
    unsafe impl<Src, Dst> Project<Src, Dst> for CastSized {
        #[inline(always)]
        fn project(src: PtrInner<'_, Src>) -> *mut Dst {
            static_assert!(Src, Dst => mem::size_of::<Src>() >= mem::size_of::<Dst>());
            src.as_ptr().cast::<Dst>()
        }
    }

    // SAFETY: The `Project::project` impl preserves referent address.
    unsafe impl<Src, Dst> Cast<Src, Dst> for CastSized {}

    /// A pointer cast which preserves the set of referent bytes of a
    /// statically-sized referent.
    ///
    /// # Safety
    ///
    /// The implementation of [`Project`] uses a compile-time assertion to
    /// guarantee that `Dst` has the same size as `Src`. Thus, `CastSizedExact`
    /// has a sound implementation of [`Project`] for all `Src` and `Dst` – the
    /// caller may pass any `Src` and `Dst` without being responsible for
    /// soundness.
    #[allow(missing_debug_implementations, missing_copy_implementations)]
    pub enum CastSizedExact {}

    // SAFETY: By the `static_assert!`, `Dst` has the same size as `Src`,
    // and so all casts preserve the set of referent bytes. All operations
    // preserve provenance.
    unsafe impl<Src, Dst> Project<Src, Dst> for CastSizedExact {
        #[inline(always)]
        fn project(src: PtrInner<'_, Src>) -> *mut Dst {
            static_assert!(Src, Dst => mem::size_of::<Src>() == mem::size_of::<Dst>());
            src.as_ptr().cast::<Dst>()
        }
    }

    // SAFETY: The `Project::project_raw` impl preserves referent address.
    unsafe impl<Src, Dst> Cast<Src, Dst> for CastSizedExact {}

    // SAFETY: By the `static_assert!`, `Project::project_raw` impl preserves
    // referent size.
    unsafe impl<Src, Dst> CastExact<Src, Dst> for CastSizedExact {}

    /// A pointer cast which preserves or shrinks the set of referent bytes of
    /// a dynamically-sized referent.
    ///
    /// # Safety
    ///
    /// The implementation of [`Project`] uses a compile-time assertion to
    /// guarantee that the cast preserves the set of referent bytes. Thus,
    /// `CastUnsized` has a sound implementation of [`Project`] for all `Src`
    /// and `Dst` – the caller may pass any `Src` and `Dst` without being
    /// responsible for soundness.
    #[allow(missing_debug_implementations, missing_copy_implementations)]
    pub enum CastUnsized {}

    // SAFETY: By the `static_assert!`, `Src` and `Dst` are either:
    // - Both sized and equal in size
    // - Both slice DSTs with the same trailing slice offset and element size
    //   and with align_of::<Src>() == align_of::<Dst>(). These ensure that any
    //   given pointer metadata encodes the same size for both `Src` and `Dst`
    //   (note that the alignment is required as it affects the amount of
    //   trailing padding). Thus, `project` preserves the set of referent bytes.
    unsafe impl<Src, Dst> Project<Src, Dst> for CastUnsized
    where
        Src: ?Sized + KnownLayout,
        Dst: ?Sized + KnownLayout<PointerMetadata = Src::PointerMetadata>,
    {
        #[inline(always)]
        fn project(src: PtrInner<'_, Src>) -> *mut Dst {
            // FIXME: Do we want this to support shrinking casts as well? If so,
            // we'll need to remove the `CastExact` impl.
            static_assert!(Src: ?Sized + KnownLayout, Dst: ?Sized + KnownLayout => {
                let src = <Src as KnownLayout>::LAYOUT;
                let dst = <Dst as KnownLayout>::LAYOUT;
                match (src.size_info, dst.size_info) {
                    (SizeInfo::Sized { size: src_size }, SizeInfo::Sized { size: dst_size }) => src_size == dst_size,
                    (
                        SizeInfo::SliceDst(TrailingSliceLayout { offset: src_offset, elem_size: src_elem_size }),
                        SizeInfo::SliceDst(TrailingSliceLayout { offset: dst_offset, elem_size: dst_elem_size })
                    ) => src.align.get() == dst.align.get() && src_offset == dst_offset && src_elem_size == dst_elem_size,
                    _ => false,
                }
            });

            let metadata = Src::pointer_to_metadata(src.as_ptr());
            Dst::raw_from_ptr_len(src.as_non_null().cast::<u8>(), metadata).as_ptr()
        }
    }

    // SAFETY: The `Project::project` impl preserves referent address.
    unsafe impl<Src, Dst> Cast<Src, Dst> for CastUnsized
    where
        Src: ?Sized + KnownLayout,
        Dst: ?Sized + KnownLayout<PointerMetadata = Src::PointerMetadata>,
    {
    }

    // SAFETY: By the `static_assert!` in `Project::project`, `Src` and `Dst`
    // are either:
    // - Both sized and equal in size
    // - Both slice DSTs with the same alignment, trailing slice offset, and
    //   element size. These ensure that any given pointer metadata encodes the
    //   same size for both `Src` and `Dst` (note that the alignment is required
    //   as it affects the amount of trailing padding).
    unsafe impl<Src, Dst> CastExact<Src, Dst> for CastUnsized
    where
        Src: ?Sized + KnownLayout,
        Dst: ?Sized + KnownLayout<PointerMetadata = Src::PointerMetadata>,
    {
    }

    /// A field projection
    ///
    /// A `Projection` is a [`Project`] which implements projection by
    /// delegating to an implementation of [`HasField::project`].
    #[allow(missing_debug_implementations, missing_copy_implementations)]
    pub struct Projection<F: ?Sized, const VARIANT_ID: i128, const FIELD_ID: i128> {
        _never: core::convert::Infallible,
        _phantom: PhantomData<F>,
    }

    // SAFETY: `HasField::project` has the same safety post-conditions as
    // `Project::project`.
    unsafe impl<T: ?Sized, F, const VARIANT_ID: i128, const FIELD_ID: i128> Project<T, T::Type>
        for Projection<F, VARIANT_ID, FIELD_ID>
    where
        T: HasField<F, VARIANT_ID, FIELD_ID>,
    {
        #[inline(always)]
        fn project(src: PtrInner<'_, T>) -> *mut T::Type {
            T::project(src)
        }
    }

    // SAFETY: All `repr(C)` union fields exist at offset 0 within the union [1],
    // and so any union projection is actually a cast (ie, preserves address).
    //
    // [1] Per
    //     https://doc.rust-lang.org/1.92.0/reference/type-layout.html#reprc-unions,
    //     it's not *technically* guaranteed that non-maximally-sized fields
    //     are at offset 0, but it's clear that this is the intention of `repr(C)`
    //     unions. It says:
    //
    //     > A union declared with `#[repr(C)]` will have the same size and
    //     > alignment as an equivalent C union declaration in the C language for
    //     > the target platform.
    //
    //     Note that this only mentions size and alignment, not layout. However,
    //     C unions *do* guarantee that all fields start at offset 0. [2]
    //
    //     This is also reinforced by
    //     https://doc.rust-lang.org/1.92.0/reference/items/unions.html#r-items.union.fields.offset:
    //
    //     > Fields might have a non-zero offset (except when the C
    //     > representation is used); in that case the bits starting at the
    //     > offset of the fields are read
    //
    // [2] Per https://port70.net/~nsz/c/c11/n1570.html#6.7.2.1p16:
    //
    //     > The size of a union is sufficient to contain the largest of its
    //     > members. The value of at most one of the members can be stored in a
    //     > union object at any time. A pointer to a union object, suitably
    //     > converted, points to each of its members (or if a member is a
    //     > bit-field, then to the unit in which it resides), and vice versa.
    //
    // FIXME(https://github.com/rust-lang/unsafe-code-guidelines/issues/595):
    // Cite the documentation once it's updated.
    unsafe impl<T: ?Sized, F, const FIELD_ID: i128> Cast<T, T::Type>
        for Projection<F, { crate::REPR_C_UNION_VARIANT_ID }, FIELD_ID>
    where
        T: HasField<F, { crate::REPR_C_UNION_VARIANT_ID }, FIELD_ID>,
    {
    }

    /// A transitive sequence of projections.
    ///
    /// Given `TU: Project` and `UV: Project`, `TransitiveProject<_, TU, UV>` is
    /// a [`Project`] which projects by applying `TU` followed by `UV`.
    ///
    /// If `TU: Cast` and `UV: Cast`, then `TransitiveProject<_, TU, UV>: Cast`.
    #[allow(missing_debug_implementations)]
    pub struct TransitiveProject<U: ?Sized, TU, UV> {
        _never: core::convert::Infallible,
        _projections: PhantomData<(TU, UV)>,
        // On our MSRV (1.56), the debuginfo for a tuple containing both an
        // uninhabited type and a DST causes an ICE. We split `U` from `TU` and
        // `UV` to avoid this situation.
        _u: PhantomData<U>,
    }

    // SAFETY: Since `TU::project` and `UV::project` are each
    // provenance-preserving operations which preserve or shrink the set of
    // referent bytes, so is their composition.
    unsafe impl<T, U, V, TU, UV> Project<T, V> for TransitiveProject<U, TU, UV>
    where
        T: ?Sized,
        U: ?Sized,
        V: ?Sized,
        TU: Project<T, U>,
        UV: Project<U, V>,
    {
        #[inline(always)]
        fn project(t: PtrInner<'_, T>) -> *mut V {
            t.project::<_, TU>().project::<_, UV>().as_ptr()
        }
    }

    // SAFETY: Since the `Project::project` impl delegates to `TU::project` and
    // `UV::project`, and since `TU` and `UV` are `Cast`, the `Project::project`
    // impl preserves the address of the referent.
    unsafe impl<T, U, V, TU, UV> Cast<T, V> for TransitiveProject<U, TU, UV>
    where
        T: ?Sized,
        U: ?Sized,
        V: ?Sized,
        TU: Cast<T, U>,
        UV: Cast<U, V>,
    {
    }

    // SAFETY: Since the `Project::project` impl delegates to `TU::project` and
    // `UV::project`, and since `TU` and `UV` are `CastExact`, the `Project::project`
    // impl preserves the set of referent bytes.
    unsafe impl<T, U, V, TU, UV> CastExact<T, V> for TransitiveProject<U, TU, UV>
    where
        T: ?Sized,
        U: ?Sized,
        V: ?Sized,
        TU: CastExact<T, U>,
        UV: CastExact<U, V>,
    {
    }

    /// A cast from `T` to `[u8]`.
    #[allow(missing_copy_implementations, missing_debug_implementations)]
    pub struct AsBytesCast;

    // SAFETY: `project` constructs a pointer with the same address as `src`
    // and with a referent of the same size as `*src`. It does this using
    // provenance-preserving operations.
    //
    // FIXME(https://github.com/rust-lang/unsafe-code-guidelines/issues/594):
    // Technically, this proof assumes that `*src` is contiguous (the same is
    // true of other proofs in this codebase). Is this guaranteed anywhere?
    unsafe impl<T: ?Sized + KnownLayout> Project<T, [u8]> for AsBytesCast {
        #[inline(always)]
        fn project(src: PtrInner<'_, T>) -> *mut [u8] {
            let bytes = match T::size_of_val_raw(src.as_non_null()) {
                Some(bytes) => bytes,
                // SAFETY: `KnownLayout::size_of_val_raw` promises to always
                // return `Some` so long as the resulting size fits in a
                // `usize`. By invariant on `PtrInner`, `src` refers to a range
                // of bytes whose size fits in an `isize`, which implies that it
                // also fits in a `usize`.
                None => unsafe { core::hint::unreachable_unchecked() },
            };

            core::ptr::slice_from_raw_parts_mut(src.as_ptr().cast::<u8>(), bytes)
        }
    }

    // SAFETY: The `Project::project` impl preserves referent address.
    unsafe impl<T: ?Sized + KnownLayout> Cast<T, [u8]> for AsBytesCast {}

    // SAFETY: The `Project::project` impl preserves the set of referent bytes.
    unsafe impl<T: ?Sized + KnownLayout> CastExact<T, [u8]> for AsBytesCast {}

    /// A cast from any type to `()`.
    #[allow(missing_copy_implementations, missing_debug_implementations)]
    pub struct CastToUnit;

    // SAFETY: The `project` implementation projects to a subset of its
    // argument's referent using provenance-preserving operations.
    unsafe impl<T: ?Sized> Project<T, ()> for CastToUnit {
        #[inline(always)]
        fn project(src: PtrInner<'_, T>) -> *mut () {
            src.as_ptr().cast::<()>()
        }
    }

    // SAFETY: The `project` implementation preserves referent address.
    unsafe impl<T: ?Sized> Cast<T, ()> for CastToUnit {}
}
