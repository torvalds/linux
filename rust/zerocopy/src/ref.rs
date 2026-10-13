// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT
//
// Copyright 2024 The Fuchsia Authors
//
// Licensed under the 2-Clause BSD License <LICENSE-BSD or
// https://opensource.org/license/bsd-2-clause>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.
use super::*;
use crate::pointer::{
    BecauseMutationCompatible, BecauseSharedCompatible, MutationCompatible, TransmuteFromPtr,
};

mod def {
    use core::marker::PhantomData;

    use crate::{
        ByteSlice, ByteSliceMut, CloneableByteSlice, CopyableByteSlice, IntoByteSlice,
        IntoByteSliceMut, KnownLayout, PointerMetadata,
    };

    /// A typed reference derived from a byte slice.
    ///
    /// A `Ref<B, T>` is a reference to a `T` which is stored in a byte slice, `B`.
    /// Unlike a native reference (`&T` or `&mut T`), `Ref<B, T>` has the same
    /// mutability as the byte slice it was constructed from (`B`).
    ///
    /// # Examples
    ///
    /// `Ref` can be used to treat a sequence of bytes as a structured type, and
    /// to read and write the fields of that type as if the byte slice reference
    /// were simply a reference to that type.
    ///
    /// ```rust
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(FromBytes, IntoBytes, KnownLayout, Immutable, Unaligned)]
    /// #[repr(C)]
    /// struct UdpHeader {
    ///     src_port: [u8; 2],
    ///     dst_port: [u8; 2],
    ///     length: [u8; 2],
    ///     checksum: [u8; 2],
    /// }
    ///
    /// #[derive(FromBytes, IntoBytes, KnownLayout, Immutable, Unaligned)]
    /// #[repr(C, packed)]
    /// struct UdpPacket {
    ///     header: UdpHeader,
    ///     body: [u8],
    /// }
    ///
    /// impl UdpPacket {
    ///     pub fn parse<B: ByteSlice>(bytes: B) -> Option<Ref<B, UdpPacket>> {
    ///         Ref::from_bytes(bytes).ok()
    ///     }
    /// }
    /// ```
    pub struct Ref<B, T: ?Sized> {
        // INVARIANTS:
        // - The referent (via `.deref`, `.deref_mut`, `.into`) byte slice is
        //   aligned to `T`'s alignment, and its size is valid for `T`.
        // - If `T: KnownLayout`, let `metadata` be
        //   `T::PointerMetadata::from_elem_count` applied to `elem_count`.
        //   Then `T::size_for_metadata(metadata)` is `Some(referent.len())`.
        bytes: B,
        // Ignored when `T` does not implement `KnownLayout`.
        elem_count: usize,
        _marker: PhantomData<T>,
    }

    impl<B, T> Ref<B, T> {
        /// Constructs a new `Ref` to a sized `T`.
        ///
        /// # Safety
        ///
        /// `bytes` dereferences (via [`deref`], [`deref_mut`], and [`into`]) to
        /// a byte slice which is aligned to `T`'s alignment and whose size is
        /// `size_of::<T>()`.
        ///
        /// [`deref`]: core::ops::Deref::deref
        /// [`deref_mut`]: core::ops::DerefMut::deref_mut
        /// [`into`]: core::convert::Into::into
        pub(super) unsafe fn new_sized_unchecked(bytes: B) -> Ref<B, T> {
            // INVARIANTS: The caller has promised that `bytes`'s referent is
            // validly aligned and has size `size_of::<T>()`. If `T:
            // KnownLayout`, then, since `T` is sized, `T::PointerMetadata` is
            // `()`. `from_elem_count` returns `()` for any element count, and
            // `T::size_for_metadata(()) == Some(size_of::<T>())`. `size_of`
            // reports this size in bytes [1].
            //
            // [1] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.size_of.html:
            //
            //     Returns the size of a type in bytes.
            Ref { bytes, elem_count: 0, _marker: PhantomData }
        }
    }

    impl<B, T: KnownLayout + ?Sized> Ref<B, T> {
        /// Constructs a new `Ref`.
        ///
        /// # Safety
        ///
        /// `bytes` dereferences (via [`deref`], [`deref_mut`], and [`into`]) to
        /// a byte slice which is aligned to `T`'s alignment and satisfies
        /// `T::size_for_metadata(metadata) == Some(referent.len())`.
        ///
        /// [`deref`]: core::ops::Deref::deref
        /// [`deref_mut`]: core::ops::DerefMut::deref_mut
        /// [`into`]: core::convert::Into::into
        pub(super) unsafe fn new_unchecked(bytes: B, metadata: T::PointerMetadata) -> Ref<B, T> {
            // INVARIANTS: The caller has promised that `bytes`'s referent is
            // validly aligned and has exactly the size described by `metadata`.
            // `KnownLayout::PointerMetadata` is `()` for sized types and
            // `usize` for slice DSTs. For both permitted metadata types,
            // `from_elem_count(metadata.to_elem_count()) == metadata`: `()`
            // round-trips through zero, while `usize` round-trips through
            // itself. Thus, storing the element count preserves the metadata
            // whose size the caller validated. This agrees with Rust's
            // language-level definition of slice pointer metadata [1].
            //
            // [1] Per https://doc.rust-lang.org/1.56.0/reference/dynamically-sized-types.html:
            //
            //     Pointers to slices also store the number of elements of the
            //     slice.
            Ref { bytes, elem_count: metadata.to_elem_count(), _marker: PhantomData }
        }

        /// Gets the pointer metadata represented by this `Ref`.
        #[inline(always)]
        pub(super) fn pointer_metadata(&self) -> T::PointerMetadata {
            // INVARIANTS: `KnownLayout::PointerMetadata` is either `()` or
            // `usize`. For `()`, `from_elem_count` ignores the element count;
            // for `usize`, it returns the element count unchanged.
            T::PointerMetadata::from_elem_count(self.elem_count)
        }
    }

    impl<B: ByteSlice, T: ?Sized> Ref<B, T> {
        /// Access the byte slice as a [`ByteSlice`].
        ///
        /// # Safety
        ///
        /// The caller promises not to call methods on the returned
        /// [`ByteSlice`] other than `ByteSlice` methods (for example, via
        /// `Any::downcast_ref`).
        ///
        /// `as_byte_slice` promises to return a `ByteSlice` whose referent is
        /// validly aligned for `T` and has a valid size for `T`. If `T:
        /// KnownLayout`, then `T::size_for_metadata(self.pointer_metadata())`
        /// returns `Some(referent.len())`.
        pub(crate) unsafe fn as_byte_slice(&self) -> &impl ByteSlice {
            // INVARIANTS: The caller promises not to call methods other than
            // those on `ByteSlice`. Since `B: ByteSlice`, dereference stability
            // guarantees that calling `ByteSlice` methods will not change the
            // address or length of `self.bytes`'s referent.
            //
            // SAFETY: By invariant on `self`, the alignment, size, and metadata
            // postconditions are upheld.
            &self.bytes
        }
    }

    impl<B: ByteSliceMut, T: ?Sized> Ref<B, T> {
        /// Access the byte slice as a [`ByteSliceMut`].
        ///
        /// # Safety
        ///
        /// The caller promises not to call methods on the returned
        /// [`ByteSliceMut`] other than `ByteSliceMut` methods (for example, via
        /// `Any::downcast_mut`).
        ///
        /// `as_byte_slice_mut` promises to return a `ByteSliceMut` whose
        /// referent is validly aligned for `T` and has a valid size for `T`. If
        /// `T: KnownLayout`, then
        /// `T::size_for_metadata(self.pointer_metadata())` returns
        /// `Some(referent.len())`.
        pub(crate) unsafe fn as_byte_slice_mut(&mut self) -> &mut impl ByteSliceMut {
            // INVARIANTS: The caller promises not to call methods other than
            // those on `ByteSliceMut`. Since `B: ByteSlice`, dereference
            // stability guarantees that calling `ByteSlice` methods will not
            // change the address or length of `self.bytes`'s referent.
            //
            // SAFETY: By invariant on `self`, the alignment, size, and metadata
            // postconditions are upheld.
            &mut self.bytes
        }
    }

    impl<'a, B: IntoByteSlice<'a>, T: ?Sized> Ref<B, T> {
        /// Access the byte slice as an [`IntoByteSlice`].
        ///
        /// # Safety
        ///
        /// The caller promises not to call methods on the returned
        /// [`IntoByteSlice`] other than `IntoByteSlice` methods (for example,
        /// via `Any::downcast_ref`).
        ///
        /// `into_byte_slice` promises to return an `IntoByteSlice` whose
        /// referent is validly aligned for `T` and has a valid size for `T`. If
        /// `T: KnownLayout`, then
        /// `T::size_for_metadata(self.pointer_metadata())` returns
        /// `Some(referent.len())`.
        pub(crate) unsafe fn into_byte_slice(self) -> impl IntoByteSlice<'a> {
            // INVARIANTS: The caller promises not to call methods other than
            // those on `IntoByteSlice`. Since `B: ByteSlice`, dereference
            // stability guarantees that calling `ByteSlice` methods will not
            // change the address or length of `self.bytes`'s referent.
            //
            // SAFETY: By invariant on `self`, the alignment, size, and metadata
            // postconditions are upheld.
            self.bytes
        }
    }

    impl<'a, B: IntoByteSliceMut<'a>, T: ?Sized> Ref<B, T> {
        /// Access the byte slice as an [`IntoByteSliceMut`].
        ///
        /// # Safety
        ///
        /// The caller promises not to call methods on the returned
        /// [`IntoByteSliceMut`] other than `IntoByteSliceMut` methods (for
        /// example, via `Any::downcast_mut`).
        ///
        /// `into_byte_slice_mut` promises to return an `IntoByteSliceMut` whose
        /// referent is validly aligned for `T` and has a valid size for `T`. If
        /// `T: KnownLayout`, then
        /// `T::size_for_metadata(self.pointer_metadata())` returns
        /// `Some(referent.len())`.
        pub(crate) unsafe fn into_byte_slice_mut(self) -> impl IntoByteSliceMut<'a> {
            // INVARIANTS: The caller promises not to call methods other than
            // those on `IntoByteSliceMut`. Since `B: ByteSlice`, dereference
            // stability guarantees that calling `ByteSlice` methods will not
            // change the address or length of `self.bytes`'s referent.
            //
            // SAFETY: By invariant on `self`, the alignment, size, and metadata
            // postconditions are upheld.
            self.bytes
        }
    }

    impl<B: CloneableByteSlice + Clone, T: ?Sized> Clone for Ref<B, T> {
        #[inline]
        fn clone(&self) -> Ref<B, T> {
            // INVARIANTS: Since `B: CloneableByteSlice`, `self.bytes.clone()` has
            // the same address and length as `self.bytes`. We copy
            // `self.elem_count`, so the clone also has the same metadata. Since
            // `self` upholds the field invariants, so does the clone.
            Ref { bytes: self.bytes.clone(), elem_count: self.elem_count, _marker: PhantomData }
        }
    }

    // INVARIANTS: Since `B: CopyableByteSlice`, the copied `Ref`'s `bytes` has the
    // same address and length as the original `Ref`'s `bytes`. The copied
    // `elem_count` has the same metadata. Since the original upholds the field
    // invariants, so does the copy.
    impl<B: CopyableByteSlice + Copy, T: ?Sized> Copy for Ref<B, T> {}
}

#[allow(unreachable_pub)] // This is a false positive on our MSRV toolchain.
pub use def::Ref;

use crate::pointer::{
    invariant::{Aligned, BecauseExclusive, Initialized, Safe, Unaligned},
    BecauseRead, PtrInner,
};

impl<B, T> Ref<B, T>
where
    B: ByteSlice,
{
    #[must_use = "has no side effects"]
    pub(crate) fn sized_from(bytes: B) -> Result<Ref<B, T>, CastError<B, T>> {
        if bytes.len() != mem::size_of::<T>() {
            return Err(SizeError::new(bytes).into());
        }
        if let Err(err) = util::validate_aligned_to::<_, T>(bytes.deref()) {
            return Err(err.with_src(bytes).into());
        }

        // SAFETY: `size_of` returns `T`'s size in bytes [1]. The equality check
        // above therefore establishes that `bytes` has exactly that byte
        // length, and `validate_aligned_to` establishes alignment.
        //
        // [1] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.size_of.html:
        //
        //     Returns the size of a type in bytes.
        Ok(unsafe { Ref::new_sized_unchecked(bytes) })
    }
}

impl<B, T> Ref<B, T>
where
    B: SplitByteSlice,
{
    #[must_use = "has no side effects"]
    pub(crate) fn sized_from_prefix(bytes: B) -> Result<(Ref<B, T>, B), CastError<B, T>> {
        if bytes.len() < mem::size_of::<T>() {
            return Err(SizeError::new(bytes).into());
        }
        if let Err(err) = util::validate_aligned_to::<_, T>(bytes.deref()) {
            return Err(err.with_src(bytes).into());
        }
        let (bytes, suffix) = bytes.split_at(mem::size_of::<T>()).map_err(
            #[inline(always)]
            |b| SizeError::new(b).into(),
        )?;
        // SAFETY: We just validated alignment and that `bytes` is at least as
        // large as `T`. `bytes.split_at(mem::size_of::<T>())?` ensures that the
        // new `bytes` is exactly the size of `T`, measured in bytes as specified
        // for `size_of` [1]. By safety postcondition on
        // `SplitByteSlice::split_at` we can rely on `split_at` to produce the
        // correct `bytes` and `suffix`.
        //
        // [1] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.size_of.html:
        //
        //     Returns the size of a type in bytes.
        let r = unsafe { Ref::new_sized_unchecked(bytes) };
        Ok((r, suffix))
    }

    #[must_use = "has no side effects"]
    pub(crate) fn sized_from_suffix(bytes: B) -> Result<(B, Ref<B, T>), CastError<B, T>> {
        let bytes_len = bytes.len();
        let split_at = if let Some(split_at) = bytes_len.checked_sub(mem::size_of::<T>()) {
            split_at
        } else {
            return Err(SizeError::new(bytes).into());
        };
        let (prefix, bytes) = bytes.split_at(split_at).map_err(|b| SizeError::new(b).into())?;
        if let Err(err) = util::validate_aligned_to::<_, T>(bytes.deref()) {
            return Err(err.with_src(bytes).into());
        }
        // SAFETY: Since `split_at` is defined as `bytes_len - size_of::<T>()`,
        // the `bytes` which results from `let (prefix, bytes) =
        // bytes.split_at(split_at)?` has length `size_of::<T>()`, measured in
        // bytes as specified for `size_of` [1]. After constructing `bytes`, we
        // validate that it has the proper alignment. By safety postcondition on
        // `SplitByteSlice::split_at` we can rely on `split_at` to produce the
        // correct `prefix` and `bytes`.
        //
        // [1] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.size_of.html:
        //
        //     Returns the size of a type in bytes.
        let r = unsafe { Ref::new_sized_unchecked(bytes) };
        Ok((prefix, r))
    }
}

impl<B, T> Ref<B, T>
where
    B: ByteSlice,
    T: KnownLayout + Immutable + ?Sized,
{
    /// Constructs a `Ref` from a byte slice.
    ///
    /// If the length of `source` is not a [valid size of `T`][valid-size], or
    /// if `source` is not appropriately aligned for `T`, this returns `Err`. If
    /// [`T: Unaligned`][t-unaligned], you can [infallibly discard the alignment
    /// error][size-error-from].
    ///
    /// `T` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [t-unaligned]: crate::Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let _ = Ref::<_, ZSTy>::from_bytes(&b"UU"[..]); // ⚠ Compile Error!
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_from_bytes",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[inline]
    pub fn from_bytes(source: B) -> Result<Ref<B, T>, CastError<B, T>> {
        static_assert_dst_is_not_zst!(T);
        let metadata = match Ptr::from_ref(source.deref())
            .try_cast_into_no_leftover::<T, BecauseImmutable>(None)
        {
            Ok(ptr) => T::pointer_to_metadata(ptr.as_inner().as_ptr()),
            Err(e) => return Err(e.with_src(()).with_src(source)),
        };
        // SAFETY: By its safety postcondition, `try_cast_into_no_leftover`
        // returned a `Ptr<T>` aligned as required for references [2], over the
        // same byte range as `source.deref()`. `pointer_to_metadata` returns
        // the metadata stored in that pointer, and `size_for_metadata` computes
        // the corresponding dynamically-known byte size [1]. Thus,
        // `T::size_for_metadata(metadata)` is `Some(source.len())`.
        // `ByteSlice` and its applicable subtraits guarantee that the stored
        // `source` continues to dereference or convert to that address and
        // length. These facts establish both preconditions of `new_unchecked`.
        //
        // [1] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.size_of_val.html:
        //
        //     Returns the size of the pointed-to value in bytes.
        //
        //     ... when `T` has no statically-known size ... `size_of_val` can
        //     be used to get the dynamically-known size.
        //
        // [2] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.align_of_val.html:
        //
        //     Returns the ABI-required minimum alignment of the type of the
        //     value that `val` points to.
        //
        //     Every reference to a value of the type `T` must be a multiple of
        //     this number.
        Ok(unsafe { Ref::new_unchecked(source, metadata) })
    }
}

impl<B, T> Ref<B, T>
where
    B: SplitByteSlice,
    T: KnownLayout + Immutable + ?Sized,
{
    /// Constructs a `Ref` from the prefix of a byte slice.
    ///
    /// This method computes the [largest possible size of `T`][valid-size] that
    /// can fit in the leading bytes of `source`, then attempts to return both a
    /// `Ref` to those bytes, and a reference to the remaining bytes. If there
    /// are insufficient bytes, or if `source` is not appropriately aligned,
    /// this returns `Err`. If [`T: Unaligned`][t-unaligned], you can
    /// [infallibly discard the alignment error][size-error-from].
    ///
    /// `T` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [t-unaligned]: crate::Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let _ = Ref::<_, ZSTy>::from_prefix(&b"UU"[..]); // ⚠ Compile Error!
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_from_prefix",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[inline]
    pub fn from_prefix(source: B) -> Result<(Ref<B, T>, B), CastError<B, T>> {
        static_assert_dst_is_not_zst!(T);
        let (metadata, remainder) = match Ptr::from_ref(source.deref())
            .try_cast_into::<T, BecauseImmutable>(CastType::Prefix, None)
        {
            Ok((ptr, remainder)) => (T::pointer_to_metadata(ptr.as_inner().as_ptr()), remainder),
            Err(e) => {
                return Err(e.with_src(()).with_src(source));
            }
        };

        // SAFETY: `remainder` is constructed as a subset of `source`, and so it
        // cannot have a larger size than `source`. Both of their `len` methods
        // measure bytes (`source` deref's to `[u8]`, and `remainder` is a
        // `Ptr<[u8]>`), so `source.len() >= remainder.len()`. Thus, this cannot
        // underflow.
        #[allow(unstable_name_collisions)]
        let split_at = unsafe { source.len().unchecked_sub(remainder.len()) };
        let (bytes, suffix) = source.split_at(split_at).map_err(|b| SizeError::new(b).into())?;
        // SAFETY: By its safety postcondition, `try_cast_into` returned an
        // `Ptr<T>` aligned as required for references [2], and a remainder
        // which are non-overlapping, cover `source.deref()`, and place the
        // `Ptr<T>` at `source.deref()`'s address.
        // Therefore, `split_at` is the typed pointer's byte length. By the
        // safety postcondition of `SplitByteSlice::split_at`, `bytes` has that
        // length and address. `pointer_to_metadata` returns the metadata stored
        // in the typed pointer, and `size_for_metadata` computes the
        // corresponding dynamically-known byte size [1]. Thus,
        // `T::size_for_metadata(metadata)` is `Some(bytes.len())`. The
        // `SplitByteSlice` and `ByteSlice` contracts preserve the established
        // address and length, satisfying both preconditions of
        // `new_unchecked`.
        //
        // [1] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.size_of_val.html:
        //
        //     Returns the size of the pointed-to value in bytes.
        //
        //     ... when `T` has no statically-known size ... `size_of_val` can
        //     be used to get the dynamically-known size.
        //
        // [2] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.align_of_val.html:
        //
        //     Returns the ABI-required minimum alignment of the type of the
        //     value that `val` points to.
        //
        //     Every reference to a value of the type `T` must be a multiple of
        //     this number.
        let r = unsafe { Ref::new_unchecked(bytes, metadata) };
        Ok((r, suffix))
    }

    /// Constructs a `Ref` from the suffix of a byte slice.
    ///
    /// This method computes the [largest possible size of `T`][valid-size] that
    /// can fit in the trailing bytes of `source`, then attempts to return both
    /// a `Ref` to those bytes, and a reference to the preceding bytes. If there
    /// are insufficient bytes, or if that suffix of `source` is not
    /// appropriately aligned, this returns `Err`. If [`T:
    /// Unaligned`][t-unaligned], you can [infallibly discard the alignment
    /// error][size-error-from].
    ///
    /// `T` may be a sized type, a slice, or a [slice DST][slice-dst].
    ///
    /// [valid-size]: crate::KnownLayout#what-is-a-valid-size
    /// [t-unaligned]: crate::Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    /// [slice-dst]: KnownLayout#dynamically-sized-types
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let _ = Ref::<_, ZSTy>::from_suffix(&b"UU"[..]); // ⚠ Compile Error!
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_from_suffix",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[inline]
    pub fn from_suffix(source: B) -> Result<(B, Ref<B, T>), CastError<B, T>> {
        static_assert_dst_is_not_zst!(T);
        let (metadata, remainder) = match Ptr::from_ref(source.deref())
            .try_cast_into::<T, BecauseImmutable>(CastType::Suffix, None)
        {
            Ok((ptr, remainder)) => (T::pointer_to_metadata(ptr.as_inner().as_ptr()), remainder),
            Err(e) => {
                let e = e.with_src(());
                return Err(e.with_src(source));
            }
        };

        let split_at = remainder.len();
        let (prefix, bytes) = source.split_at(split_at).map_err(|b| SizeError::new(b).into())?;
        // SAFETY: By its safety postcondition, `try_cast_into` returned an
        // `Ptr<T>` aligned as required for references [2], and a remainder
        // which are non-overlapping, cover `source.deref()`, and place the
        // remainder at `source.deref()`'s address. More precisely,
        // `PtrInner::try_cast_into` constructs the typed suffix from the
        // `r_slice` produced by `split_at_unchecked(split_at)` and returns its
        // `l_slice` as `remainder`. Thus, even when `T` is zero-sized, the typed
        // pointer and the `bytes` produced by splitting `source` at
        // `remainder.len()` have the same address and length. By the safety
        // postcondition of `SplitByteSlice::split_at`, `bytes` retains that
        // address and length. `pointer_to_metadata` returns the metadata stored
        // in the typed pointer, and `size_for_metadata` computes the
        // corresponding dynamically-known byte size [1]. Thus,
        // `T::size_for_metadata(metadata)` is `Some(bytes.len())`. The
        // `SplitByteSlice` and `ByteSlice` contracts preserve the established
        // address and length, satisfying both preconditions of
        // `new_unchecked`.
        //
        // [1] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.size_of_val.html:
        //
        //     Returns the size of the pointed-to value in bytes.
        //
        //     ... when `T` has no statically-known size ... `size_of_val` can
        //     be used to get the dynamically-known size.
        //
        // [2] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.align_of_val.html:
        //
        //     Returns the ABI-required minimum alignment of the type of the
        //     value that `val` points to.
        //
        //     Every reference to a value of the type `T` must be a multiple of
        //     this number.
        let r = unsafe { Ref::new_unchecked(bytes, metadata) };
        Ok((prefix, r))
    }
}

impl<B, T> Ref<B, T>
where
    B: ByteSlice,
    T: KnownLayout<PointerMetadata = usize> + Immutable + ?Sized,
{
    /// Constructs a `Ref` from the given bytes with DST length equal to `count`
    /// without copying.
    ///
    /// This method attempts to return a `Ref` to the prefix of `source`
    /// interpreted as a `T` with `count` trailing elements, and a reference to
    /// the remaining bytes. If the length of `source` is not equal to the size
    /// of `Self` with `count` elements, or if `source` is not appropriately
    /// aligned, this returns `Err`. If [`T: Unaligned`][t-unaligned], you can
    /// [infallibly discard the alignment error][size-error-from].
    ///
    /// [t-unaligned]: crate::Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let _ = Ref::<_, ZSTy>::from_bytes_with_elems(&b"UU"[..], 42); // ⚠ Compile Error!
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_from_bytes_with_elems",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[inline]
    pub fn from_bytes_with_elems(source: B, count: usize) -> Result<Ref<B, T>, CastError<B, T>> {
        static_assert_dst_is_not_zst!(T);
        let expected_len = match T::size_for_metadata(count) {
            Some(len) => len,
            None => return Err(SizeError::new(source).into()),
        };
        if source.len() != expected_len {
            return Err(SizeError::new(source).into());
        }
        if let Err(e) = Ptr::from_ref(source.deref())
            .try_cast_into_no_leftover::<T, BecauseImmutable>(Some(count))
        {
            return Err(e.with_src(()).with_src(source));
        }
        // SAFETY: `size_for_metadata(count)` returned `expected_len`, and the
        // equality check established that this is `source.len()`. This is the
        // dynamically-known byte size described by `size_of_val` [1]. By its
        // safety postcondition, `try_cast_into_no_leftover(Some(count))` only
        // succeeded after producing a `Ptr<T>` aligned as required for
        // references [2], with that metadata over the same byte range as
        // `source.deref()`. `ByteSlice` and its applicable subtraits guarantee
        // that the stored `source` continues to dereference or convert to that
        // address and length. These facts establish both preconditions of
        // `new_unchecked`.
        //
        // [1] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.size_of_val.html:
        //
        //     Returns the size of the pointed-to value in bytes.
        //
        //     ... when `T` has no statically-known size ... `size_of_val` can
        //     be used to get the dynamically-known size.
        //
        // [2] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.align_of_val.html:
        //
        //     Returns the ABI-required minimum alignment of the type of the
        //     value that `val` points to.
        //
        //     Every reference to a value of the type `T` must be a multiple of
        //     this number.
        Ok(unsafe { Ref::new_unchecked(source, count) })
    }
}

impl<B, T> Ref<B, T>
where
    B: SplitByteSlice,
    T: KnownLayout<PointerMetadata = usize> + Immutable + ?Sized,
{
    /// Constructs a `Ref` from the prefix of the given bytes with DST
    /// length equal to `count` without copying.
    ///
    /// This method attempts to return a `Ref` to the prefix of `source`
    /// interpreted as a `T` with `count` trailing elements, and a reference to
    /// the remaining bytes. If there are insufficient bytes, or if `source` is
    /// not appropriately aligned, this returns `Err`. If [`T:
    /// Unaligned`][t-unaligned], you can [infallibly discard the alignment
    /// error][size-error-from].
    ///
    /// [t-unaligned]: crate::Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let _ = Ref::<_, ZSTy>::from_prefix_with_elems(&b"UU"[..], 42); // ⚠ Compile Error!
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_from_prefix_with_elems",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[inline]
    pub fn from_prefix_with_elems(
        source: B,
        count: usize,
    ) -> Result<(Ref<B, T>, B), CastError<B, T>> {
        static_assert_dst_is_not_zst!(T);
        let expected_len = match T::size_for_metadata(count) {
            Some(len) => len,
            None => return Err(SizeError::new(source).into()),
        };
        let (prefix, bytes) = source.split_at(expected_len).map_err(SizeError::new)?;
        Self::from_bytes_with_elems(prefix, count).map(move |l| (l, bytes))
    }

    /// Constructs a `Ref` from the suffix of the given bytes with DST length
    /// equal to `count` without copying.
    ///
    /// This method attempts to return a `Ref` to the suffix of `source`
    /// interpreted as a `T` with `count` trailing elements, and a reference to
    /// the preceding bytes. If there are insufficient bytes, or if that suffix
    /// of `source` is not appropriately aligned, this returns `Err`. If [`T:
    /// Unaligned`][t-unaligned], you can [infallibly discard the alignment
    /// error][size-error-from].
    ///
    /// [t-unaligned]: crate::Unaligned
    /// [size-error-from]: error/struct.SizeError.html#method.from-1
    ///
    /// # Compile-Time Assertions
    ///
    /// This method cannot yet be used on unsized types whose dynamically-sized
    /// component is zero-sized. Attempting to use this method on such types
    /// results in a compile-time assertion error; e.g.:
    ///
    /// ```compile_fail,E0080
    /// use zerocopy::*;
    /// # use zerocopy_derive::*;
    ///
    /// #[derive(Immutable, KnownLayout)]
    /// #[repr(C)]
    /// struct ZSTy {
    ///     leading_sized: u16,
    ///     trailing_dst: [()],
    /// }
    ///
    /// let _ = Ref::<_, ZSTy>::from_suffix_with_elems(&b"UU"[..], 42); // ⚠ Compile Error!
    /// ```
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_from_suffix_with_elems",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 2
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[inline]
    pub fn from_suffix_with_elems(
        source: B,
        count: usize,
    ) -> Result<(B, Ref<B, T>), CastError<B, T>> {
        static_assert_dst_is_not_zst!(T);
        let expected_len = match T::size_for_metadata(count) {
            Some(len) => len,
            None => return Err(SizeError::new(source).into()),
        };
        let split_at = if let Some(split_at) = source.len().checked_sub(expected_len) {
            split_at
        } else {
            return Err(SizeError::new(source).into());
        };
        // SAFETY: The preceding `source.len().checked_sub(expected_len)`
        // guarantees that `split_at` is in-bounds.
        let (bytes, suffix) = unsafe { source.split_at_unchecked(split_at) };
        Self::from_bytes_with_elems(suffix, count).map(move |l| (bytes, l))
    }
}

impl<'a, B, T> Ref<B, T>
where
    B: 'a + IntoByteSlice<'a>,
    T: FromBytes + KnownLayout + Immutable + ?Sized,
{
    /// Converts this `Ref` into a reference.
    ///
    /// `into_ref` consumes the `Ref`, and returns a reference to `T`.
    ///
    /// Note: this is an associated function, which means that you have to call
    /// it as `Ref::into_ref(r)` instead of `r.into_ref()`. This is so that
    /// there is no conflict with a method on the inner type.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_into_ref",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn into_ref(r: Self) -> &'a T {
        // Presumably unreachable, since we've guarded each constructor of `Ref`.
        static_assert_dst_is_not_zst!(T);

        let metadata = r.pointer_metadata();

        // SAFETY: We don't call any methods on `b` other than those provided by
        // `IntoByteSlice`.
        let b = unsafe { r.into_byte_slice() };
        let b = b.into_byte_slice();

        if let crate::layout::SizeInfo::Sized { .. } = T::LAYOUT.size_info {
            let ptr = Ptr::from_ref(b);
            // SAFETY: We just checked that `T: Sized`. By invariant on `r`,
            // `b`'s size is equal to `size_of::<T>()`.
            let ptr = unsafe { cast_for_sized::<T, _, _, _>(ptr) };

            // SAFETY: None of the preceding transformations modifies the
            // address of the pointer, and by invariant on `r`, we know that it
            // is validly-aligned.
            let ptr = unsafe { ptr.assume_alignment::<Aligned>() };
            return ptr.as_ref();
        }

        // PANICS: By post-condition on `into_byte_slice`, `b` is aligned for
        // `T`, and its size is exactly the size encoded by `metadata`. By
        // post-condition, `b.into_byte_slice()` produces a byte slice with
        // identical address and length to that produced by `b.deref()`.
        let ptr = Ptr::from_ref(b.into_byte_slice())
            .try_cast_into_no_leftover::<T, BecauseImmutable>(Some(metadata))
            .expect("zerocopy internal error: into_ref should be infallible");
        let ptr = ptr.recall_validity();
        ptr.as_ref()
    }
}

impl<'a, B, T> Ref<B, T>
where
    B: 'a + IntoByteSliceMut<'a>,
    T: FromBytes + IntoBytes + KnownLayout + ?Sized,
{
    /// Converts this `Ref` into a mutable reference.
    ///
    /// `into_mut` consumes the `Ref`, and returns a mutable reference to `T`.
    ///
    /// Note: this is an associated function, which means that you have to call
    /// it as `Ref::into_mut(r)` instead of `r.into_mut()`. This is so that
    /// there is no conflict with a method on the inner type.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_into_mut",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ]
    )]
    #[must_use = "has no side effects"]
    #[inline(always)]
    pub fn into_mut(r: Self) -> &'a mut T {
        // Presumably unreachable, since we've guarded each constructor of `Ref`.
        static_assert_dst_is_not_zst!(T);

        let metadata = r.pointer_metadata();

        // SAFETY: We don't call any methods on `b` other than those provided by
        // `IntoByteSliceMut`.
        let b = unsafe { r.into_byte_slice_mut() };
        let b = b.into_byte_slice_mut();

        if let crate::layout::SizeInfo::Sized { .. } = T::LAYOUT.size_info {
            let ptr = Ptr::from_mut(b);
            // SAFETY: We just checked that `T: Sized`. By invariant on `r`,
            // `b`'s size is equal to `size_of::<T>()`.
            let ptr = unsafe {
                cast_for_sized::<
                    T,
                    _,
                    (BecauseRead, BecauseExclusive),
                    (BecauseMutationCompatible, BecauseSharedCompatible),
                >(ptr)
            };

            // SAFETY: None of the preceding transformations modifies the
            // address of the pointer, and by invariant on `r`, we know that it
            // is validly-aligned.
            let ptr = unsafe { ptr.assume_alignment::<Aligned>() };
            return ptr.as_mut();
        }

        // PANICS: By post-condition on `into_byte_slice_mut`, `b` is aligned
        // for `T`, and its size is exactly the size encoded by `metadata`. By
        // post-condition, `b.into_byte_slice_mut()` produces a byte slice with
        // identical address and length to that produced by `b.deref_mut()`.
        let ptr = Ptr::from_mut(b.into_byte_slice_mut())
            .try_cast_into_no_leftover::<T, BecauseExclusive>(Some(metadata))
            .expect("zerocopy internal error: into_mut should be infallible");
        let ptr = ptr.recall_validity::<_, (_, (_, _))>();
        ptr.as_mut()
    }
}

impl<B, T> Ref<B, T>
where
    B: ByteSlice,
    T: ?Sized,
{
    /// Gets the underlying bytes.
    ///
    /// Note: this is an associated function, which means that you have to call
    /// it as `Ref::bytes(r)` instead of `r.bytes()`. This is so that there is
    /// no conflict with a method on the inner type.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_bytes",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[inline]
    pub fn bytes(r: &Self) -> &[u8] {
        // SAFETY: We don't call any methods on `b` other than those provided by
        // `ByteSlice`.
        unsafe { r.as_byte_slice().deref() }
    }
}

impl<B, T> Ref<B, T>
where
    B: ByteSliceMut,
    T: ?Sized,
{
    /// Gets the underlying bytes mutably.
    ///
    /// Note: this is an associated function, which means that you have to call
    /// it as `Ref::bytes_mut(r)` instead of `r.bytes_mut()`. This is so that
    /// there is no conflict with a method on the inner type.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_bytes_mut",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[inline]
    pub fn bytes_mut(r: &mut Self) -> &mut [u8] {
        // SAFETY: We don't call any methods on `b` other than those provided by
        // `ByteSliceMut`.
        unsafe { r.as_byte_slice_mut().deref_mut() }
    }
}

impl<B, T> Ref<B, T>
where
    B: ByteSlice,
    T: FromBytes,
{
    /// Reads a copy of `T`.
    ///
    /// Note: this is an associated function, which means that you have to call
    /// it as `Ref::read(r)` instead of `r.read()`. This is so that there is no
    /// conflict with a method on the inner type.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_read_static_size",
        format = "coco_static_size",
    )]
    #[must_use = "has no side effects"]
    #[inline]
    pub fn read(r: &Self) -> T {
        // SAFETY: We don't call any methods on `b` other than those provided by
        // `ByteSlice`.
        let b = unsafe { r.as_byte_slice() };

        // SAFETY: By postcondition on `as_byte_slice`, we know that `b` is a
        // valid size and alignment for `T`. By safety invariant on `ByteSlice`,
        // we know that this is preserved via `.deref()`. Because `T:
        // FromBytes`, it is sound to interpret these bytes as a `T`.
        unsafe { ptr::read(b.deref().as_ptr().cast::<T>()) }
    }
}

impl<B, T> Ref<B, T>
where
    B: ByteSliceMut,
    T: IntoBytes,
{
    /// Writes the bytes of `t` and then forgets `t`.
    ///
    /// Note: this is an associated function, which means that you have to call
    /// it as `Ref::write(r, t)` instead of `r.write(t)`. This is so that there
    /// is no conflict with a method on the inner type.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_write_static_size",
        format = "coco_static_size",
    )]
    #[inline]
    pub fn write(r: &mut Self, t: T) {
        // SAFETY: We don't call any methods on `b` other than those provided by
        // `ByteSliceMut`.
        let b = unsafe { r.as_byte_slice_mut() };

        // SAFETY: By postcondition on `as_byte_slice_mut`, we know that `b` is a
        // valid size and alignment for `T`. By safety invariant on
        // `ByteSlice`, we know that this is preserved via `.deref()`. Writing
        // `t` to the buffer will allow all of the bytes of `t` to be accessed
        // as a `[u8]`, but because `T: IntoBytes`, we know that this is sound.
        unsafe { ptr::write(b.deref_mut().as_mut_ptr().cast::<T>(), t) }
    }
}

impl<B, T> Deref for Ref<B, T>
where
    B: ByteSlice,
    T: FromBytes + KnownLayout + Immutable + ?Sized,
{
    type Target = T;

    /// Dereferences the value.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_deref",
        format = "coco",
        arity = 3,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ],
        [
            @index 3
            @title "Dynamically Padded"
            @variant "dynamic_padding"
        ]
    )]
    #[inline]
    fn deref(&self) -> &T {
        // Presumably unreachable, since we've guarded each constructor of `Ref`.
        static_assert_dst_is_not_zst!(T);

        let metadata = self.pointer_metadata();

        // SAFETY: We don't call any methods on `b` other than those provided by
        // `ByteSlice`.
        let b = unsafe { self.as_byte_slice() };
        let b = b.deref();

        if let crate::layout::SizeInfo::Sized { .. } = T::LAYOUT.size_info {
            let ptr = Ptr::from_ref(b);
            // SAFETY: We just checked that `T: Sized`. By invariant on `r`,
            // `b`'s size is equal to `size_of::<T>()`.
            let ptr = unsafe { cast_for_sized::<T, _, _, _>(ptr) };

            // SAFETY: None of the preceding transformations modifies the
            // address of the pointer, and by invariant on `r`, we know that it
            // is validly-aligned.
            let ptr = unsafe { ptr.assume_alignment::<Aligned>() };
            return ptr.as_ref();
        }

        // PANICS: By postcondition on `as_byte_slice`, `b` is aligned for `T`,
        // and its size is exactly the size encoded by `metadata`. By invariant
        // on `ByteSlice`, these are preserved through `.deref()`, so this
        // `unwrap` will not panic.
        let ptr = Ptr::from_ref(b)
            .try_cast_into_no_leftover::<T, BecauseImmutable>(Some(metadata))
            .expect("zerocopy internal error: Deref::deref should be infallible");
        let ptr = ptr.recall_validity();
        ptr.as_ref()
    }
}

impl<B, T> DerefMut for Ref<B, T>
where
    B: ByteSliceMut,
    // FIXME(#251): We can't remove `Immutable` here because it's required by
    // the impl of `Deref`, which is a super-trait of `DerefMut`. Maybe we can
    // add a separate inherent method for this?
    T: FromBytes + IntoBytes + KnownLayout + Immutable + ?Sized,
{
    /// Mutably dereferences the value.
    ///
    #[doc = codegen_section!(
        header = "h5",
        bench = "ref_type_deref_mut",
        format = "coco",
        arity = 2,
        [
            open
            @index 1
            @title "Sized"
            @variant "static_size"
        ],
        [
            @index 2
            @title "Unsized"
            @variant "dynamic_size"
        ]
    )]
    #[inline]
    fn deref_mut(&mut self) -> &mut T {
        // Presumably unreachable, since we've guarded each constructor of `Ref`.
        static_assert_dst_is_not_zst!(T);

        let metadata = self.pointer_metadata();

        // SAFETY: We don't call any methods on `b` other than those provided by
        // `ByteSliceMut`.
        let b = unsafe { self.as_byte_slice_mut() };
        let b = b.deref_mut();

        if let crate::layout::SizeInfo::Sized { .. } = T::LAYOUT.size_info {
            let ptr = Ptr::from_mut(b);
            // SAFETY: We just checked that `T: Sized`. By invariant on `r`,
            // `b`'s size is equal to `size_of::<T>()`.
            let ptr = unsafe {
                cast_for_sized::<
                    T,
                    _,
                    (BecauseRead, BecauseExclusive),
                    (BecauseMutationCompatible, BecauseSharedCompatible),
                >(ptr)
            };

            // SAFETY: None of the preceding transformations modifies the
            // address of the pointer, and by invariant on `r`, we know that it
            // is validly-aligned.
            let ptr = unsafe { ptr.assume_alignment::<Aligned>() };
            return ptr.as_mut();
        }

        // PANICS: By postcondition on `as_byte_slice_mut`, `b` is aligned for
        // `T`, and its size is exactly the size encoded by `metadata`. By
        // invariant on `ByteSlice`, these are preserved through `.deref_mut()`,
        // so this `unwrap` will not panic.
        let ptr = Ptr::from_mut(b)
            .try_cast_into_no_leftover::<T, BecauseExclusive>(Some(metadata))
            .expect("zerocopy internal error: DerefMut::deref_mut should be infallible");
        let ptr = ptr.recall_validity::<_, (_, (_, BecauseExclusive))>();
        ptr.as_mut()
    }
}

impl<T, B> Display for Ref<B, T>
where
    B: ByteSlice,
    T: FromBytes + Display + KnownLayout + Immutable + ?Sized,
{
    #[inline]
    fn fmt(&self, fmt: &mut Formatter<'_>) -> fmt::Result {
        let inner: &T = self;
        inner.fmt(fmt)
    }
}

impl<T, B> Debug for Ref<B, T>
where
    B: ByteSlice,
    T: FromBytes + Debug + KnownLayout + Immutable + ?Sized,
{
    #[inline]
    fn fmt(&self, fmt: &mut Formatter<'_>) -> fmt::Result {
        let inner: &T = self;
        fmt.debug_tuple("Ref").field(&inner).finish()
    }
}

impl<T, B> Eq for Ref<B, T>
where
    B: ByteSlice,
    T: FromBytes + Eq + KnownLayout + Immutable + ?Sized,
{
}

impl<T, B> PartialEq for Ref<B, T>
where
    B: ByteSlice,
    T: FromBytes + PartialEq + KnownLayout + Immutable + ?Sized,
{
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        self.deref().eq(other.deref())
    }
}

impl<T, B> Ord for Ref<B, T>
where
    B: ByteSlice,
    T: FromBytes + Ord + KnownLayout + Immutable + ?Sized,
{
    #[inline]
    fn cmp(&self, other: &Self) -> Ordering {
        let inner: &T = self;
        let other_inner: &T = other;
        inner.cmp(other_inner)
    }
}

impl<T, B> PartialOrd for Ref<B, T>
where
    B: ByteSlice,
    T: FromBytes + PartialOrd + KnownLayout + Immutable + ?Sized,
{
    #[inline]
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        let inner: &T = self;
        let other_inner: &T = other;
        inner.partial_cmp(other_inner)
    }
}

/// # Safety
///
/// `T: Sized` and `ptr`'s referent must have size `size_of::<T>()`.
#[inline(always)]
unsafe fn cast_for_sized<'a, T, A, R, S>(
    ptr: Ptr<'a, [u8], (A, Aligned, Safe)>,
) -> Ptr<'a, T, (A, Unaligned, Safe)>
where
    T: FromBytes + KnownLayout + ?Sized,
    A: crate::invariant::Aliasing,
    [u8]: MutationCompatible<T, A, Initialized, Initialized, R>,
    T: TransmuteFromPtr<T, A, Initialized, Safe, crate::pointer::cast::IdCast, S>,
{
    use crate::pointer::cast::{Cast, Project};

    enum CastForSized {}

    // SAFETY: `CastForSized` is only used below with the input `ptr`, which the
    // caller promises has size `size_of::<T>()`. Thus, the referent produced in
    // this cast has the same size as `ptr`'s referent. All operations preserve
    // provenance.
    unsafe impl<T: ?Sized + KnownLayout> Project<[u8], T> for CastForSized {
        #[inline(always)]
        fn project(src: PtrInner<'_, [u8]>) -> *mut T {
            T::raw_from_ptr_len(
                src.as_non_null().cast(),
                <T::PointerMetadata as crate::PointerMetadata>::from_elem_count(0),
            )
            .as_ptr()
        }
    }

    // SAFETY: The `Project::project` impl preserves referent address.
    unsafe impl<T: ?Sized + KnownLayout> Cast<[u8], T> for CastForSized {}

    ptr.recall_validity::<Initialized, (_, (_, _))>()
        .cast::<_, CastForSized, _>()
        .recall_validity::<Safe, _>()
}

#[cfg(test)]
#[allow(clippy::assertions_on_result_states)]
mod tests {
    use core::convert::TryInto as _;

    use super::*;
    use crate::util::testutil::*;

    #[test]
    fn test_mut_slice_into_ref() {
        // Prior to #1260/#1299, calling `into_ref` on a `&mut [u8]`-backed
        // `Ref` was not supported.
        let mut buf = [0u8];
        let r = Ref::<&mut [u8], u8>::from_bytes(&mut buf).unwrap();
        assert_eq!(Ref::into_ref(r), &0);
    }

    #[test]
    fn test_address() {
        // Test that the `Deref` and `DerefMut` implementations return a
        // reference which points to the right region of memory.

        let buf = [0];
        let r = Ref::<_, u8>::from_bytes(&buf[..]).unwrap();
        let buf_ptr = buf.as_ptr();
        let deref_ptr: *const u8 = r.deref();
        assert_eq!(buf_ptr, deref_ptr);

        let buf = [0];
        let r = Ref::<_, [u8]>::from_bytes(&buf[..]).unwrap();
        let buf_ptr = buf.as_ptr();
        let deref_ptr = r.deref().as_ptr();
        assert_eq!(buf_ptr, deref_ptr);
    }

    #[test]
    #[allow(clippy::clone_on_copy)] // Exercise `Ref`'s manual `Clone` impl.
    fn test_with_elems_preserves_ambiguous_metadata() {
        #[derive(FromBytes, Immutable, KnownLayout)]
        #[repr(C, align(8))]
        struct Frame {
            header: [u8; 8],
            body: [u8],
        }

        #[derive(FromBytes, Immutable, KnownLayout)]
        #[repr(C, align(8))]
        struct OddHeader {
            header: [u8; 7],
            body: [u8],
        }

        // Dynamic trailing padding makes these pairs of distinct element
        // counts produce the same total byte size. Thus, the byte length alone
        // cannot recover the count supplied to a `*_with_elems` constructor.
        assert_eq!(Frame::size_for_metadata(1), Some(16));
        assert_eq!(Frame::size_for_metadata(8), Some(16));
        assert_eq!(OddHeader::size_for_metadata(0), Some(8));
        assert_eq!(OddHeader::size_for_metadata(1), Some(8));

        let bytes = Align::<[u8; 32], AU64>::default();

        let whole = Ref::<_, Frame>::from_bytes_with_elems(&bytes.t[..16], 1).unwrap();
        assert_eq!(whole.body.len(), 1);
        assert_eq!(Ref::bytes(&whole).len(), 16);
        assert_eq!(whole.clone().body.len(), 1);
        let copied = whole;
        assert_eq!(copied.body.len(), 1);
        assert_eq!(Ref::into_ref(whole).body.len(), 1);

        let (prefix, rest) = Ref::<_, Frame>::from_prefix_with_elems(&bytes.t[..], 1).unwrap();
        assert_eq!(prefix.body.len(), 1);
        assert_eq!(rest.len(), 16);

        let (before, suffix) = Ref::<_, Frame>::from_suffix_with_elems(&bytes.t[..24], 1).unwrap();
        assert_eq!(before.len(), 8);
        assert_eq!(suffix.body.len(), 1);

        // Zero is real metadata, not a sentinel for "infer from the bytes".
        let empty = Ref::<_, OddHeader>::from_bytes_with_elems(&bytes.t[..8], 0).unwrap();
        assert!(empty.body.is_empty());
        assert!(Ref::into_ref(empty).body.is_empty());

        // The implicit constructors still choose the largest fitting count,
        // and retain the metadata selected during their validating cast.
        let inferred = Ref::<_, Frame>::from_bytes(&bytes.t[..16]).unwrap();
        assert_eq!(inferred.body.len(), 8);
        let (inferred, rest) = Ref::<_, Frame>::from_prefix(&bytes.t[..16]).unwrap();
        assert_eq!(inferred.body.len(), 8);
        assert!(rest.is_empty());
        let (before, inferred) = Ref::<_, Frame>::from_suffix(&bytes.t[..16]).unwrap();
        assert!(before.is_empty());
        assert_eq!(inferred.body.len(), 8);
    }

    // Verify that values written to a `Ref` are properly shared between the
    // typed and untyped representations, that reads via `deref` and `read`
    // behave the same, and that writes via `deref_mut` and `write` behave the
    // same.
    fn test_new_helper(mut r: Ref<&mut [u8], AU64>) {
        // assert that the value starts at 0
        assert_eq!(*r, AU64(0));
        assert_eq!(Ref::read(&r), AU64(0));

        // Assert that values written to the typed value are reflected in the
        // byte slice.
        const VAL1: AU64 = AU64(0xFF00FF00FF00FF00);
        *r = VAL1;
        assert_eq!(Ref::bytes(&r), &VAL1.to_bytes());
        *r = AU64(0);
        Ref::write(&mut r, VAL1);
        assert_eq!(Ref::bytes(&r), &VAL1.to_bytes());

        // Assert that values written to the byte slice are reflected in the
        // typed value.
        const VAL2: AU64 = AU64(!VAL1.0); // different from `VAL1`
        Ref::bytes_mut(&mut r).copy_from_slice(&VAL2.to_bytes()[..]);
        assert_eq!(*r, VAL2);
        assert_eq!(Ref::read(&r), VAL2);
    }

    // Verify that values written to a `Ref` are properly shared between the
    // typed and untyped representations; pass a value with `typed_len` `AU64`s
    // backed by an array of `typed_len * 8` bytes.
    fn test_new_helper_slice(mut r: Ref<&mut [u8], [AU64]>, typed_len: usize) {
        // Assert that the value starts out zeroed.
        assert_eq!(&*r, vec![AU64(0); typed_len].as_slice());

        // Check the backing storage is the exact same slice.
        let untyped_len = typed_len * 8;
        assert_eq!(Ref::bytes(&r).len(), untyped_len);
        assert_eq!(Ref::bytes(&r).as_ptr(), r.as_ptr().cast::<u8>());

        // Assert that values written to the typed value are reflected in the
        // byte slice.
        const VAL1: AU64 = AU64(0xFF00FF00FF00FF00);
        for typed in &mut *r {
            *typed = VAL1;
        }
        assert_eq!(Ref::bytes(&r), VAL1.0.to_ne_bytes().repeat(typed_len).as_slice());

        // Assert that values written to the byte slice are reflected in the
        // typed value.
        const VAL2: AU64 = AU64(!VAL1.0); // different from VAL1
        Ref::bytes_mut(&mut r).copy_from_slice(&VAL2.0.to_ne_bytes().repeat(typed_len));
        assert!(r.iter().copied().all(|x| x == VAL2));
    }

    #[test]
    fn test_new_aligned_sized() {
        // Test that a properly-aligned, properly-sized buffer works for new,
        // new_from_prefix, and new_from_suffix, and that new_from_prefix and
        // new_from_suffix return empty slices. Test that a properly-aligned
        // buffer whose length is a multiple of the element size works for
        // new_slice.

        // A buffer with an alignment of 8.
        let mut buf = Align::<[u8; 8], AU64>::default();
        // `buf.t` should be aligned to 8, so this should always succeed.
        test_new_helper(Ref::<_, AU64>::from_bytes(&mut buf.t[..]).unwrap());
        {
            // In a block so that `r` and `suffix` don't live too long.
            buf.set_default();
            let (r, suffix) = Ref::<_, AU64>::from_prefix(&mut buf.t[..]).unwrap();
            assert!(suffix.is_empty());
            test_new_helper(r);
        }
        {
            buf.set_default();
            let (prefix, r) = Ref::<_, AU64>::from_suffix(&mut buf.t[..]).unwrap();
            assert!(prefix.is_empty());
            test_new_helper(r);
        }

        // A buffer with alignment 8 and length 24. We choose this length very
        // intentionally: if we instead used length 16, then the prefix and
        // suffix lengths would be identical. In the past, we used length 16,
        // which resulted in this test failing to discover the bug uncovered in
        // #506.
        let mut buf = Align::<[u8; 24], AU64>::default();
        // `buf.t` should be aligned to 8 and have a length which is a multiple
        // of `size_of::<AU64>()`, so this should always succeed.
        test_new_helper_slice(Ref::<_, [AU64]>::from_bytes(&mut buf.t[..]).unwrap(), 3);
        buf.set_default();
        let r = Ref::<_, [AU64]>::from_bytes_with_elems(&mut buf.t[..], 3).unwrap();
        test_new_helper_slice(r, 3);

        let ascending: [u8; 24] = (0..24).collect::<Vec<_>>().try_into().unwrap();
        // 16 ascending bytes followed by 8 zeros.
        let mut ascending_prefix = ascending;
        ascending_prefix[16..].copy_from_slice(&[0, 0, 0, 0, 0, 0, 0, 0]);
        // 8 zeros followed by 16 ascending bytes.
        let mut ascending_suffix = ascending;
        ascending_suffix[..8].copy_from_slice(&[0, 0, 0, 0, 0, 0, 0, 0]);
        {
            buf.t = ascending_suffix;
            let (r, suffix) = Ref::<_, [AU64]>::from_prefix_with_elems(&mut buf.t[..], 1).unwrap();
            assert_eq!(suffix, &ascending[8..]);
            test_new_helper_slice(r, 1);
        }
        {
            buf.t = ascending_prefix;
            let (prefix, r) = Ref::<_, [AU64]>::from_suffix_with_elems(&mut buf.t[..], 1).unwrap();
            assert_eq!(prefix, &ascending[..16]);
            test_new_helper_slice(r, 1);
        }
    }

    #[test]
    fn test_new_oversized() {
        // Test that a properly-aligned, overly-sized buffer works for
        // `new_from_prefix` and `new_from_suffix`, and that they return the
        // remainder and prefix of the slice respectively.

        let mut buf = Align::<[u8; 16], AU64>::default();
        {
            // In a block so that `r` and `suffix` don't live too long. `buf.t`
            // should be aligned to 8, so this should always succeed.
            let (r, suffix) = Ref::<_, AU64>::from_prefix(&mut buf.t[..]).unwrap();
            assert_eq!(suffix.len(), 8);
            test_new_helper(r);
        }
        {
            buf.set_default();
            // `buf.t` should be aligned to 8, so this should always succeed.
            let (prefix, r) = Ref::<_, AU64>::from_suffix(&mut buf.t[..]).unwrap();
            assert_eq!(prefix.len(), 8);
            test_new_helper(r);
        }
    }

    #[test]
    #[allow(clippy::cognitive_complexity)]
    fn test_new_error() {
        // Fail because the buffer is too large.

        // A buffer with an alignment of 8.
        let buf = Align::<[u8; 16], AU64>::default();
        // `buf.t` should be aligned to 8, so only the length check should fail.
        assert!(Ref::<_, AU64>::from_bytes(&buf.t[..]).is_err());

        // Fail because the buffer is too small.

        // A buffer with an alignment of 8.
        let buf = Align::<[u8; 4], AU64>::default();
        // `buf.t` should be aligned to 8, so only the length check should fail.
        assert!(Ref::<_, AU64>::from_bytes(&buf.t[..]).is_err());
        assert!(Ref::<_, AU64>::from_prefix(&buf.t[..]).is_err());
        assert!(Ref::<_, AU64>::from_suffix(&buf.t[..]).is_err());

        // Fail because the length is not a multiple of the element size.

        let buf = Align::<[u8; 12], AU64>::default();
        // `buf.t` has length 12, but element size is 8.
        assert!(Ref::<_, [AU64]>::from_bytes(&buf.t[..]).is_err());

        // Fail because the buffer is too short.
        let buf = Align::<[u8; 12], AU64>::default();
        // `buf.t` has length 12, but the element size is 8 (and we're expecting
        // two of them). For each function, we test with a length that would
        // cause the size to overflow `usize`, and with a normal length that
        // will fail thanks to the buffer being too short; these are different
        // error paths, and while the error types are the same, the distinction
        // shows up in code coverage metrics.
        let n = (usize::MAX / mem::size_of::<AU64>()) + 1;
        assert!(Ref::<_, [AU64]>::from_bytes_with_elems(&buf.t[..], n).is_err());
        assert!(Ref::<_, [AU64]>::from_bytes_with_elems(&buf.t[..], 2).is_err());
        assert!(Ref::<_, [AU64]>::from_prefix_with_elems(&buf.t[..], n).is_err());
        assert!(Ref::<_, [AU64]>::from_prefix_with_elems(&buf.t[..], 2).is_err());
        assert!(Ref::<_, [AU64]>::from_suffix_with_elems(&buf.t[..], n).is_err());
        assert!(Ref::<_, [AU64]>::from_suffix_with_elems(&buf.t[..], 2).is_err());

        // Fail because the alignment is insufficient.

        // A buffer with an alignment of 8. An odd buffer size is chosen so that
        // the last byte of the buffer has odd alignment.
        let buf = Align::<[u8; 13], AU64>::default();
        // Slicing from 1, we get a buffer with size 12 (so the length check
        // should succeed) but an alignment of only 1, which is insufficient.
        assert!(Ref::<_, AU64>::from_bytes(&buf.t[1..]).is_err());
        assert!(Ref::<_, AU64>::from_prefix(&buf.t[1..]).is_err());
        assert!(Ref::<_, [AU64]>::from_bytes(&buf.t[1..]).is_err());
        assert!(Ref::<_, [AU64]>::from_bytes_with_elems(&buf.t[1..], 1).is_err());
        assert!(Ref::<_, [AU64]>::from_prefix_with_elems(&buf.t[1..], 1).is_err());
        assert!(Ref::<_, [AU64]>::from_suffix_with_elems(&buf.t[1..], 1).is_err());
        // Slicing is unnecessary here because `new_from_suffix` uses the suffix
        // of the slice, which has odd alignment.
        assert!(Ref::<_, AU64>::from_suffix(&buf.t[..]).is_err());

        // Fail due to arithmetic overflow.

        let buf = Align::<[u8; 16], AU64>::default();
        let unreasonable_len = usize::MAX / mem::size_of::<AU64>() + 1;
        assert!(Ref::<_, [AU64]>::from_prefix_with_elems(&buf.t[..], unreasonable_len).is_err());
        assert!(Ref::<_, [AU64]>::from_suffix_with_elems(&buf.t[..], unreasonable_len).is_err());
    }

    #[test]
    #[allow(unstable_name_collisions)]
    #[allow(clippy::as_conversions)]
    fn test_into_ref_mut() {
        #[allow(unused)]
        use crate::util::AsAddress as _;

        let mut buf = Align::<[u8; 8], u64>::default();
        let r = Ref::<_, u64>::from_bytes(&buf.t[..]).unwrap();
        let rf = Ref::into_ref(r);
        assert_eq!(rf, &0u64);
        let buf_addr = (&buf.t as *const [u8; 8]).addr();
        assert_eq!((rf as *const u64).addr(), buf_addr);

        let r = Ref::<_, u64>::from_bytes(&mut buf.t[..]).unwrap();
        let rf = Ref::into_mut(r);
        assert_eq!(rf, &mut 0u64);
        assert_eq!((rf as *mut u64).addr(), buf_addr);

        *rf = u64::MAX;
        assert_eq!(buf.t, [0xFF; 8]);

        // Exercise the unsized `into_mut` route, which must use the metadata
        // retained by the constructor rather than infer fresh metadata.
        let mut buf = Align::<[u8; 8], u16>::default();
        let r = Ref::<_, [u16]>::from_bytes_with_elems(&mut buf.t[..], 4).unwrap();
        let rf = Ref::into_mut(r);
        assert_eq!(rf.len(), 4);
    }

    #[test]
    fn test_display_debug() {
        let buf = Align::<[u8; 8], u64>::default();
        let r = Ref::<_, u64>::from_bytes(&buf.t[..]).unwrap();
        assert_eq!(format!("{}", r), "0");
        assert_eq!(format!("{:?}", r), "Ref(0)");

        let buf = Align::<[u8; 8], u64>::default();
        let r = Ref::<_, [u64]>::from_bytes(&buf.t[..]).unwrap();
        assert_eq!(format!("{:?}", r), "Ref([0])");
    }

    #[test]
    fn test_eq() {
        let buf1 = 0_u64;
        let r1 = Ref::<_, u64>::from_bytes(buf1.as_bytes()).unwrap();
        let buf2 = 0_u64;
        let r2 = Ref::<_, u64>::from_bytes(buf2.as_bytes()).unwrap();
        assert_eq!(r1, r2);
    }

    #[test]
    fn test_ne() {
        let buf1 = 0_u64;
        let r1 = Ref::<_, u64>::from_bytes(buf1.as_bytes()).unwrap();
        let buf2 = 1_u64;
        let r2 = Ref::<_, u64>::from_bytes(buf2.as_bytes()).unwrap();
        assert_ne!(r1, r2);
    }

    #[test]
    fn test_ord() {
        let buf1 = 0_u64;
        let r1 = Ref::<_, u64>::from_bytes(buf1.as_bytes()).unwrap();
        let buf2 = 1_u64;
        let r2 = Ref::<_, u64>::from_bytes(buf2.as_bytes()).unwrap();
        assert!(r1 < r2);
        assert_eq!(PartialOrd::partial_cmp(&r1, &r2), Some(Ordering::Less));
        assert_eq!(Ord::cmp(&r1, &r2), Ordering::Less);
    }
}

#[cfg(all(test, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS))]
mod benches {
    use test::{self, Bencher};

    use super::*;
    use crate::util::testutil::*;

    #[bench]
    fn bench_from_bytes_sized(b: &mut Bencher) {
        let buf = Align::<[u8; 8], AU64>::default();
        // `buf.t` should be aligned to 8, so this should always succeed.
        let bytes = &buf.t[..];
        b.iter(|| test::black_box(Ref::<_, AU64>::from_bytes(test::black_box(bytes)).unwrap()));
    }

    #[bench]
    fn bench_into_ref_sized(b: &mut Bencher) {
        let buf = Align::<[u8; 8], AU64>::default();
        let bytes = &buf.t[..];
        let r = Ref::<_, AU64>::from_bytes(bytes).unwrap();
        b.iter(|| test::black_box(Ref::into_ref(test::black_box(r))));
    }

    #[bench]
    fn bench_into_mut_sized(b: &mut Bencher) {
        let mut buf = Align::<[u8; 8], AU64>::default();
        let buf = &mut buf.t[..];
        let _ = Ref::<_, AU64>::from_bytes(&mut *buf).unwrap();
        b.iter(move || {
            // SAFETY: The preceding `from_bytes` succeeded, and so we know that
            // `buf` is validly aligned and has the `size_of::<AU64>()` byte
            // length required by `new_sized_unchecked` [1].
            //
            // [1] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.size_of.html:
            //
            //     Returns the size of a type in bytes.
            let r = unsafe { Ref::<&mut [u8], AU64>::new_sized_unchecked(&mut *buf) };
            test::black_box(Ref::into_mut(test::black_box(r)));
        });
    }

    #[bench]
    fn bench_deref_sized(b: &mut Bencher) {
        let buf = Align::<[u8; 8], AU64>::default();
        let bytes = &buf.t[..];
        let r = Ref::<_, AU64>::from_bytes(bytes).unwrap();
        b.iter(|| {
            let temp = test::black_box(r);
            test::black_box(temp.deref());
        });
    }

    #[bench]
    fn bench_deref_mut_sized(b: &mut Bencher) {
        let mut buf = Align::<[u8; 8], AU64>::default();
        let buf = &mut buf.t[..];
        let _ = Ref::<_, AU64>::from_bytes(&mut *buf).unwrap();
        b.iter(|| {
            // SAFETY: The preceding `from_bytes` succeeded, and so we know that
            // `buf` is validly aligned and has the `size_of::<AU64>()` byte
            // length required by `new_sized_unchecked` [1].
            //
            // [1] Per https://doc.rust-lang.org/1.56.0/core/mem/fn.size_of.html:
            //
            //     Returns the size of a type in bytes.
            let r = unsafe { Ref::<&mut [u8], AU64>::new_sized_unchecked(&mut *buf) };
            let mut temp = test::black_box(r);
            test::black_box(temp.deref_mut());
        });
    }
}
