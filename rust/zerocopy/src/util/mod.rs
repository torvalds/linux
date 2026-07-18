// SPDX-License-Identifier: (BSD-2-Clause OR Apache-2.0) OR MIT

// Copyright 2023 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

#[macro_use]
pub(crate) mod macros;

#[doc(hidden)]
pub mod macro_util;

use core::{
    marker::PhantomData,
    mem::{self, ManuallyDrop},
    num::NonZeroUsize,
    ptr::NonNull,
};

use super::*;
use crate::pointer::{
    invariant::{Exclusive, Shared, Valid},
    SizeEq, TransmuteFromPtr,
};

/// Like [`PhantomData`], but [`Send`] and [`Sync`] regardless of whether the
/// wrapped `T` is.
pub(crate) struct SendSyncPhantomData<T: ?Sized>(PhantomData<T>);

// SAFETY: `SendSyncPhantomData` does not enable any behavior which isn't sound
// to be called from multiple threads.
unsafe impl<T: ?Sized> Send for SendSyncPhantomData<T> {}
// SAFETY: `SendSyncPhantomData` does not enable any behavior which isn't sound
// to be called from multiple threads.
unsafe impl<T: ?Sized> Sync for SendSyncPhantomData<T> {}

impl<T: ?Sized> Default for SendSyncPhantomData<T> {
    fn default() -> SendSyncPhantomData<T> {
        SendSyncPhantomData(PhantomData)
    }
}

impl<T: ?Sized> PartialEq for SendSyncPhantomData<T> {
    fn eq(&self, _other: &Self) -> bool {
        true
    }
}

impl<T: ?Sized> Eq for SendSyncPhantomData<T> {}

impl<T: ?Sized> Clone for SendSyncPhantomData<T> {
    fn clone(&self) -> Self {
        SendSyncPhantomData(PhantomData)
    }
}

#[cfg(miri)]
extern "Rust" {
    /// Miri-provided intrinsic that marks the pointer `ptr` as aligned to
    /// `align`.
    ///
    /// This intrinsic is used to inform Miri's symbolic alignment checker that
    /// a pointer is aligned, even if Miri cannot statically deduce that fact.
    /// This is often required when performing raw pointer arithmetic or casts
    /// where the alignment is guaranteed by runtime checks or invariants that
    /// Miri is not aware of.
    pub(crate) fn miri_promise_symbolic_alignment(ptr: *const (), align: usize);
}

pub(crate) trait AsAddress {
    fn addr(self) -> usize;
}

impl<T: ?Sized> AsAddress for &T {
    #[inline(always)]
    fn addr(self) -> usize {
        let ptr: *const T = self;
        AsAddress::addr(ptr)
    }
}

impl<T: ?Sized> AsAddress for &mut T {
    #[inline(always)]
    fn addr(self) -> usize {
        let ptr: *const T = self;
        AsAddress::addr(ptr)
    }
}

impl<T: ?Sized> AsAddress for NonNull<T> {
    #[inline(always)]
    fn addr(self) -> usize {
        AsAddress::addr(self.as_ptr())
    }
}

impl<T: ?Sized> AsAddress for *const T {
    #[inline(always)]
    fn addr(self) -> usize {
        // FIXME(#181), FIXME(https://github.com/rust-lang/rust/issues/95228):
        // Use `.addr()` instead of `as usize` once it's stable, and get rid of
        // this `allow`. Currently, `as usize` is the only way to accomplish
        // this.
        #[allow(clippy::as_conversions)]
        #[cfg_attr(
            __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS,
            allow(lossy_provenance_casts)
        )]
        return self.cast::<()>() as usize;
    }
}

impl<T: ?Sized> AsAddress for *mut T {
    #[inline(always)]
    fn addr(self) -> usize {
        let ptr: *const T = self;
        AsAddress::addr(ptr)
    }
}

/// Validates that `t` is aligned to `align_of::<U>()`.
#[inline(always)]
pub(crate) fn validate_aligned_to<T: AsAddress, U>(t: T) -> Result<(), AlignmentError<(), U>> {
    // `mem::align_of::<U>()` is guaranteed to return a non-zero value, which in
    // turn guarantees that this mod operation will not panic.
    #[allow(clippy::arithmetic_side_effects)]
    let remainder = t.addr() % mem::align_of::<U>();
    if remainder == 0 {
        Ok(())
    } else {
        // SAFETY: We just confirmed that `t.addr() % align_of::<U>() != 0`.
        // That's only possible if `align_of::<U>() > 1`.
        Err(unsafe { AlignmentError::new_unchecked(()) })
    }
}

/// Returns the bytes needed to pad `len` to the next multiple of `align`.
///
/// This function assumes that align is a power of two; there are no guarantees
/// on the answer it gives if this is not the case.
#[cfg_attr(
    kani,
    kani::requires(len <= DstLayout::MAX_SIZE),
    kani::requires(align.is_power_of_two()),
    kani::ensures(|&p| (len + p) % align.get() == 0),
    // Ensures that we add the minimum required padding.
    kani::ensures(|&p| p < align.get()),
)]
pub(crate) const fn padding_needed_for(len: usize, align: NonZeroUsize) -> usize {
    #[cfg(kani)]
    #[kani::proof_for_contract(padding_needed_for)]
    fn proof() {
        padding_needed_for(kani::any(), kani::any());
    }

    // Abstractly, we want to compute:
    //   align - (len % align).
    // Handling the case where len%align is 0.
    // Because align is a power of two, len % align = len & (align-1).
    // Guaranteed not to underflow as align is nonzero.
    #[allow(clippy::arithmetic_side_effects)]
    let mask = align.get() - 1;

    // To efficiently subtract this value from align, we can use the bitwise
    // complement.
    // Note that ((!len) & (align-1)) gives us a number that with (len &
    // (align-1)) sums to align-1. So subtracting 1 from x before taking the
    // complement subtracts `len` from `align`. Some quick inspection of
    // cases shows that this also handles the case where `len % align = 0`
    // correctly too: len-1 % align then equals align-1, so the complement mod
    // align will be 0, as desired.
    //
    // The following reasoning can be verified quickly by an SMT solver
    // supporting the theory of bitvectors:
    // ```smtlib
    // ; Naive implementation of padding
    // (define-fun padding1 (
    //     (len (_ BitVec 32))
    //     (align (_ BitVec 32))) (_ BitVec 32)
    //    (ite
    //      (= (_ bv0 32) (bvand len (bvsub align (_ bv1 32))))
    //      (_ bv0 32)
    //      (bvsub align (bvand len (bvsub align (_ bv1 32))))))
    //
    // ; The implementation below
    // (define-fun padding2 (
    //     (len (_ BitVec 32))
    //     (align (_ BitVec 32))) (_ BitVec 32)
    // (bvand (bvnot (bvsub len (_ bv1 32))) (bvsub align (_ bv1 32))))
    //
    // (define-fun is-power-of-two ((x (_ BitVec 32))) Bool
    //   (= (_ bv0 32) (bvand x (bvsub x (_ bv1 32)))))
    //
    // (declare-const len (_ BitVec 32))
    // (declare-const align (_ BitVec 32))
    // ; Search for a case where align is a power of two and padding2 disagrees
    // ; with padding1
    // (assert (and (is-power-of-two align)
    //              (not (= (padding1 len align) (padding2 len align)))))
    // (simplify (padding1 (_ bv300 32) (_ bv32 32))) ; 20
    // (simplify (padding2 (_ bv300 32) (_ bv32 32))) ; 20
    // (simplify (padding1 (_ bv322 32) (_ bv32 32))) ; 30
    // (simplify (padding2 (_ bv322 32) (_ bv32 32))) ; 30
    // (simplify (padding1 (_ bv8 32) (_ bv8 32)))    ; 0
    // (simplify (padding2 (_ bv8 32) (_ bv8 32)))    ; 0
    // (check-sat) ; unsat, also works for 64-bit bitvectors
    // ```
    !(len.wrapping_sub(1)) & mask
}

/// Rounds `n` down to the largest value `m` such that `m <= n` and `m % align
/// == 0`.
///
/// # Panics
///
/// May panic if `align` is not a power of two. Even if it doesn't panic in this
/// case, it will produce nonsense results.
#[inline(always)]
#[cfg_attr(
    kani,
    kani::requires(align.is_power_of_two()),
    kani::ensures(|&m| m <= n && m % align.get() == 0),
    // Guarantees that `m` is the *largest* value such that `m % align == 0`.
    kani::ensures(|&m| {
        // If this `checked_add` fails, then the next multiple would wrap
        // around, which trivially satisfies the "largest value" requirement.
        m.checked_add(align.get()).map(|next_mul| next_mul > n).unwrap_or(true)
    })
)]
pub(crate) const fn round_down_to_next_multiple_of_alignment(
    n: usize,
    align: NonZeroUsize,
) -> usize {
    #[cfg(kani)]
    #[kani::proof_for_contract(round_down_to_next_multiple_of_alignment)]
    fn proof() {
        round_down_to_next_multiple_of_alignment(kani::any(), kani::any());
    }

    let align = align.get();
    #[cfg(not(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0))]
    debug_assert!(align.is_power_of_two());

    // Subtraction can't underflow because `align.get() >= 1`.
    #[allow(clippy::arithmetic_side_effects)]
    let mask = !(align - 1);
    n & mask
}

pub(crate) const fn max(a: NonZeroUsize, b: NonZeroUsize) -> NonZeroUsize {
    if a.get() < b.get() {
        b
    } else {
        a
    }
}

pub(crate) const fn min(a: NonZeroUsize, b: NonZeroUsize) -> NonZeroUsize {
    if a.get() > b.get() {
        b
    } else {
        a
    }
}

/// Copies `src` into the prefix of `dst`.
///
/// # Safety
///
/// The caller guarantees that `src.len() <= dst.len()`.
#[inline(always)]
pub(crate) unsafe fn copy_unchecked(src: &[u8], dst: &mut [u8]) {
    debug_assert!(src.len() <= dst.len());
    // SAFETY: This invocation satisfies the safety contract of
    // copy_nonoverlapping [1]:
    // - `src.as_ptr()` is trivially valid for reads of `src.len()` bytes
    // - `dst.as_ptr()` is valid for writes of `src.len()` bytes, because the
    //   caller has promised that `src.len() <= dst.len()`
    // - `src` and `dst` are, trivially, properly aligned
    // - the region of memory beginning at `src` with a size of `src.len()`
    //   bytes does not overlap with the region of memory beginning at `dst`
    //   with the same size, because `dst` is derived from an exclusive
    //   reference.
    unsafe {
        core::ptr::copy_nonoverlapping(src.as_ptr(), dst.as_mut_ptr(), src.len());
    };
}

/// Unsafely transmutes the given `src` into a type `Dst`.
///
/// # Safety
///
/// The value `src` must be a valid instance of `Dst`.
#[inline(always)]
pub(crate) const unsafe fn transmute_unchecked<Src, Dst>(src: Src) -> Dst {
    static_assert!(Src, Dst => core::mem::size_of::<Src>() == core::mem::size_of::<Dst>());

    #[repr(C)]
    union Transmute<Src, Dst> {
        src: ManuallyDrop<Src>,
        dst: ManuallyDrop<Dst>,
    }

    // SAFETY: Since `Transmute<Src, Dst>` is `#[repr(C)]`, its `src` and `dst`
    // fields both start at the same offset and the types of those fields are
    // transparent wrappers around `Src` and `Dst` [1]. Consequently,
    // initializing `Transmute` with with `src` and then reading out `dst` is
    // equivalent to transmuting from `Src` to `Dst` [2]. Transmuting from `src`
    // to `Dst` is valid because — by contract on the caller — `src` is a valid
    // instance of `Dst`.
    //
    // [1] Per https://doc.rust-lang.org/1.82.0/std/mem/struct.ManuallyDrop.html:
    //
    //     `ManuallyDrop<T>` is guaranteed to have the same layout and bit
    //     validity as `T`, and is subject to the same layout optimizations as
    //     `T`.
    //
    // [2] Per https://doc.rust-lang.org/1.82.0/reference/items/unions.html#reading-and-writing-union-fields:
    //
    //     Effectively, writing to and then reading from a union with the C
    //     representation is analogous to a transmute from the type used for
    //     writing to the type used for reading.
    unsafe { ManuallyDrop::into_inner(Transmute { src: ManuallyDrop::new(src) }.dst) }
}

/// # Safety
///
/// `Src` must have a greater or equal alignment to `Dst`.
pub(crate) unsafe fn transmute_ref<Src, Dst, R>(src: &Src) -> &Dst
where
    Src: ?Sized,
    Dst: SizeEq<Src>
        + TransmuteFromPtr<Src, Shared, Valid, Valid, <Dst as SizeEq<Src>>::CastFrom, R>
        + ?Sized,
{
    let dst = Ptr::from_ref(src).transmute();
    // SAFETY: The caller promises that `Src`'s alignment is at least as large
    // as `Dst`'s alignment.
    let dst = unsafe { dst.assume_alignment() };
    dst.as_ref()
}

/// # Safety
///
/// `Src` must have a greater or equal alignment to `Dst`.
pub(crate) unsafe fn transmute_mut<Src, Dst, R>(src: &mut Src) -> &mut Dst
where
    Src: ?Sized,
    Dst: SizeEq<Src>
        + TransmuteFromPtr<Src, Exclusive, Valid, Valid, <Dst as SizeEq<Src>>::CastFrom, R>
        + ?Sized,
{
    let dst = Ptr::from_mut(src).transmute();
    // SAFETY: The caller promises that `Src`'s alignment is at least as large
    // as `Dst`'s alignment.
    let dst = unsafe { dst.assume_alignment() };
    dst.as_mut()
}

/// Uses `allocate` to create a `Box<T>`.
///
/// # Errors
///
/// Returns an error on allocation failure. Allocation failure is guaranteed
/// never to cause a panic or an abort.
///
/// # Safety
///
/// `allocate` must be either `alloc::alloc::alloc` or
/// `alloc::alloc::alloc_zeroed`. The referent of the box returned by `new_box`
/// has the same bit-validity as the referent of the pointer returned by the
/// given `allocate` and sufficient size to store `T` with `meta`.
#[must_use = "has no side effects (other than allocation)"]
#[cfg(feature = "alloc")]
#[inline]
pub(crate) unsafe fn new_box<T>(
    meta: T::PointerMetadata,
    allocate: unsafe fn(core::alloc::Layout) -> *mut u8,
) -> Result<alloc::boxed::Box<T>, AllocError>
where
    T: ?Sized + crate::KnownLayout,
{
    let align = T::LAYOUT.align.get();
    if !T::is_valid_metadata(meta) {
        return Err(AllocError);
    }
    let size = match T::size_for_metadata(meta) {
        Some(size) => size,
        // Thanks to the `!T::is_valid_metadata(meta)` check
        // above, this branch is unreachable. Fortunately, the
        // optimizer recognizes this, so replacing this branch
        // with `unreachable_unchecked` produces no codegen
        // improvements.
        None => return Err(AllocError),
    };
    let ptr = if size != 0 {
        // SAFETY:
        // - `align` is derived from a `NonZeroUsize` and is thus non-zero.
        // - `align` is a power of two because, by invariant on
        //   `KnownLayout::LAYOUT` `<T as KnownLayout>::LAYOUT` accurately
        //   reflects the layout of `T`.
        // - `size`, by invariant on `size_for_metadata` is well-aligned for
        //   `align` and, by the check on `T::is_valid_metadata(meta)`, is less
        //   than `isize::MAX`.
        let layout: Layout = unsafe { Layout::from_size_align_unchecked(size, align) };
        // SAFETY: By contract on the caller, `allocate` is either
        // `alloc::alloc::alloc` or `alloc::alloc::alloc_zeroed`. The above
        // check ensures their shared safety precondition: that the supplied
        // layout is not zero-sized type [1].
        //
        // [1] Per https://doc.rust-lang.org/1.81.0/std/alloc/trait.GlobalAlloc.html#tymethod.alloc:
        //
        //     This function is unsafe because undefined behavior can result if
        //     the caller does not ensure that layout has non-zero size.
        let ptr = unsafe { allocate(layout) };
        match NonNull::new(ptr) {
            Some(ptr) => ptr,
            None => return Err(AllocError),
        }
    } else {
        // We use `transmute` instead of an `as` cast since Miri (with strict
        // provenance enabled) notices and complains that an `as` cast creates a
        // pointer with no provenance. Miri isn't smart enough to realize that
        // we're only executing this branch when we're constructing a zero-sized
        // `Box`, which doesn't require provenance.
        //
        // SAFETY: any initialized bit sequence is a bit-valid `*mut u8`. All
        // bits of a `usize` are initialized.
        //
        // `#[allow(unknown_lints)]` is for `integer_to_ptr_transmutes`
        #[allow(unknown_lints)]
        #[allow(clippy::useless_transmute, integer_to_ptr_transmutes)]
        let dangling = unsafe { mem::transmute::<usize, *mut u8>(align) };
        // SAFETY: `dangling` is constructed from `align`, which is derived from
        // a `NonZeroUsize`, which is guaranteed to be non-zero.
        //
        // `Box<[T]>` does not allocate when `T` is zero-sized or when `len` is
        // zero, but it does require a non-null dangling pointer for its
        // allocation.
        //
        // FIXME(https://github.com/rust-lang/rust/issues/95228): Use
        // `std::ptr::without_provenance` once it's stable. That may optimize
        // better. As written, Rust may assume that this consumes "exposed"
        // provenance, and thus Rust may have to assume that this may consume
        // provenance from any pointer whose provenance has been exposed.
        unsafe { NonNull::new_unchecked(dangling) }
    };

    let ptr = T::raw_from_ptr_len(ptr, meta);

    // FIXME(#429): Add a "SAFETY" comment and remove this `allow`. Make sure to
    // include a justification that `ptr.as_ptr()` is validly-aligned in the ZST
    // case (in which we manually construct a dangling pointer) and to justify
    // why `Box` is safe to drop (it's because `allocate` uses the system
    // allocator).
    #[allow(clippy::undocumented_unsafe_blocks)]
    Ok(unsafe { alloc::boxed::Box::from_raw(ptr.as_ptr()) })
}

mod len_of {
    use super::*;

    /// A witness type for metadata of a valid instance of `&T`.
    pub struct MetadataOf<T: ?Sized + KnownLayout> {
        /// # Safety
        ///
        /// The size of an instance of `&T` with the given metadata is not
        /// larger than `isize::MAX`.
        meta: T::PointerMetadata,
        _p: PhantomData<T>,
    }

    impl<T: ?Sized + KnownLayout> Copy for MetadataOf<T> {}
    impl<T: ?Sized + KnownLayout> Clone for MetadataOf<T> {
        #[inline]
        fn clone(&self) -> Self {
            *self
        }
    }

    impl<T: ?Sized + KnownLayout> core::fmt::Debug for MetadataOf<T>
    where
        T::PointerMetadata: core::fmt::Debug,
    {
        #[inline]
        fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
            f.debug_struct("MetadataOf").field("meta", &self.meta).finish()
        }
    }

    impl<T: ?Sized> MetadataOf<T>
    where
        T: KnownLayout,
    {
        /// Returns `None` if `meta` is greater than `t`'s metadata.
        #[inline(always)]
        pub(crate) fn new_in_bounds(t: &T, meta: usize) -> Option<Self>
        where
            T: KnownLayout<PointerMetadata = usize>,
        {
            if meta <= Ptr::from_ref(t).len() {
                // SAFETY: We have checked that `meta` is not greater than `t`'s
                // metadata, which, by invariant on `&T`, addresses no more than
                // `isize::MAX` bytes [1][2].
                //
                // [1] Per https://doc.rust-lang.org/1.85.0/std/primitive.reference.html#safety:
                //
                //    For all types, `T: ?Sized`, and for all `t: &T` or `t:
                //    &mut T`, when such values cross an API boundary, the
                //    following invariants must generally be upheld:
                //
                //    * `t` is non-null
                //    * `t` is aligned to `align_of_val(t)`
                //    * if `size_of_val(t) > 0`, then `t` is dereferenceable for
                //      `size_of_val(t)` many bytes
                //
                //    If `t` points at address `a`, being "dereferenceable" for
                //    N bytes means that the memory range `[a, a + N)` is all
                //    contained within a single allocated object.
                //
                // [2] Per https://doc.rust-lang.org/1.85.0/std/ptr/index.html#allocated-object:
                //
                //    For any allocated object with `base` address, `size`, and
                //    a set of `addresses`, the following are guaranteed:
                //    - For all addresses `a` in `addresses`, `a` is in the
                //      range `base .. (base + size)` (note that this requires
                //      `a < base + size`, not `a <= base + size`)
                //    - `base` is not equal to [`null()`] (i.e., the address
                //      with the numerical value 0)
                //    - `base + size <= usize::MAX`
                //    - `size <= isize::MAX`
                Some(unsafe { Self::new_unchecked(meta) })
            } else {
                None
            }
        }

        /// # Safety
        ///
        /// The size of an instance of `&T` with the given metadata is not
        /// larger than `isize::MAX`.
        pub(crate) unsafe fn new_unchecked(meta: T::PointerMetadata) -> Self {
            // SAFETY: The caller has promised that the size of an instance of
            // `&T` with the given metadata is not larger than `isize::MAX`.
            Self { meta, _p: PhantomData }
        }

        pub(crate) fn get(&self) -> T::PointerMetadata
        where
            T::PointerMetadata: Copy,
        {
            self.meta
        }

        #[inline]
        pub(crate) fn padding_needed_for(&self) -> usize
        where
            T: KnownLayout<PointerMetadata = usize>,
        {
            let trailing_slice_layout = crate::trailing_slice_layout::<T>();

            // FIXME(#67): Remove this allow. See NumExt for more details.
            #[allow(
                unstable_name_collisions,
                clippy::incompatible_msrv,
                clippy::multiple_unsafe_ops_per_block
            )]
            // SAFETY: By invariant on `self`, a `&T` with metadata `self.meta`
            // describes an object of size `<= isize::MAX`. This computes the
            // size of such a `&T` without any trailing padding, and so neither
            // the multiplication nor the addition will overflow.
            let unpadded_size = unsafe {
                let trailing_size = self.meta.unchecked_mul(trailing_slice_layout.elem_size);
                trailing_size.unchecked_add(trailing_slice_layout.offset)
            };

            util::padding_needed_for(unpadded_size, T::LAYOUT.align)
        }

        #[inline(always)]
        pub(crate) fn validate_cast_and_convert_metadata(
            addr: usize,
            bytes_len: MetadataOf<[u8]>,
            cast_type: CastType,
            meta: Option<T::PointerMetadata>,
        ) -> Result<(MetadataOf<T>, MetadataOf<[u8]>), MetadataCastError> {
            let layout = match meta {
                None => T::LAYOUT,
                // This can return `Err(MetadataCastError::Size)` if the
                // metadata describes an object which can't fit in an `isize`.
                Some(meta) => {
                    if !T::is_valid_metadata(meta) {
                        return Err(MetadataCastError::Size);
                    }
                    let size = match T::size_for_metadata(meta) {
                        Some(size) => size,
                        // Thanks to the `!T::is_valid_metadata(meta)` check
                        // above, this branch is unreachable. Fortunately, the
                        // optimizer recognizes this, so replacing this branch
                        // with `unreachable_unchecked` produces no codegen
                        // improvements.
                        None => return Err(MetadataCastError::Size),
                    };
                    DstLayout {
                        align: T::LAYOUT.align,
                        size_info: crate::SizeInfo::Sized { size },
                        statically_shallow_unpadded: false,
                    }
                }
            };
            // Lemma 0: By contract on `validate_cast_and_convert_metadata`, if
            // the result is `Ok(..)`, then a `&T` with `elems` trailing slice
            // elements is no larger in size than `bytes_len.get()`.
            let (elems, split_at) =
                layout.validate_cast_and_convert_metadata(addr, bytes_len.get(), cast_type)?;
            let elems = T::PointerMetadata::from_elem_count(elems);

            // For a slice DST type, if `meta` is `Some(elems)`, then we
            // synthesize `layout` to describe a sized type whose size is equal
            // to the size of the instance that we are asked to cast. For sized
            // types, `validate_cast_and_convert_metadata` returns `elems == 0`.
            // Thus, in this case, we need to use the `elems` passed by the
            // caller, not the one returned by
            // `validate_cast_and_convert_metadata`.
            //
            // Lemma 1: A `&T` with `elems` trailing slice elements is no larger
            // in size than `bytes_len.get()`. Proof:
            // - If `meta` is `None`, then `elems` satisfies this condition by
            //   Lemma 0.
            // - If `meta` is `Some(meta)`, then `layout` describes an object
            //   whose size is equal to the size of an `&T` with `meta`
            //   metadata. By Lemma 0, that size is not larger than
            //   `bytes_len.get()`.
            //
            // Lemma 2: A `&T` with `elems` trailing slice elements is no larger
            // than `isize::MAX` bytes. Proof: By Lemma 1, a `&T` with metadata
            // `elems` is not larger in size than `bytes_len.get()`. By
            // invariant on `MetadataOf<[u8]>`, a `&[u8]` with metadata
            // `bytes_len` is not larger than `isize::MAX`. Because
            // `size_of::<u8>()` is `1`, a `&[u8]` with metadata `bytes_len` has
            // size `bytes_len.get()` bytes. Therefore, a `&T` with metadata
            // `elems` has size not larger than `isize::MAX`.
            let elems = meta.unwrap_or(elems);

            // SAFETY: See Lemma 2.
            let elems = unsafe { MetadataOf::new_unchecked(elems) };

            // SAFETY: Let `size` be the size of a `&T` with metadata `elems`.
            // By post-condition on `validate_cast_and_convert_metadata`, one of
            // the following conditions holds:
            // - `split_at == size`, in which case, by Lemma 2, `split_at <=
            //   isize::MAX`. Since `size_of::<u8>() == 1`, a `[u8]` with
            //   `split_at` elems has size not larger than `isize::MAX`.
            // - `split_at == bytes_len - size`. Since `bytes_len:
            //   MetadataOf<u8>`, and since `size` is non-negative, `split_at`
            //   addresses no more bytes than `bytes_len` does. Since
            //   `bytes_len: MetadataOf<u8>`, `bytes_len` describes a `[u8]`
            //   which has no more than `isize::MAX` bytes, and thus so does
            //   `split_at`.
            let split_at = unsafe { MetadataOf::<[u8]>::new_unchecked(split_at) };
            Ok((elems, split_at))
        }
    }
}

pub use len_of::MetadataOf;

/// Since we support multiple versions of Rust, there are often features which
/// have been stabilized in the most recent stable release which do not yet
/// exist (stably) on our MSRV. This module provides polyfills for those
/// features so that we can write more "modern" code, and just remove the
/// polyfill once our MSRV supports the corresponding feature. Without this,
/// we'd have to write worse/more verbose code and leave FIXME comments
/// sprinkled throughout the codebase to update to the new pattern once it's
/// stabilized.
///
/// Each trait is imported as `_` at the crate root; each polyfill should "just
/// work" at usage sites.
pub(crate) mod polyfills {
    use core::ptr::{self, NonNull};

    // A polyfill for `NonNull::slice_from_raw_parts` that we can use before our
    // MSRV is 1.70, when that function was stabilized.
    //
    // The `#[allow(unused)]` is necessary because, on sufficiently recent
    // toolchain versions, `ptr.slice_from_raw_parts()` resolves to the inherent
    // method rather than to this trait, and so this trait is considered unused.
    //
    // FIXME(#67): Once our MSRV is 1.70, remove this.
    #[allow(unused)]
    pub(crate) trait NonNullExt<T> {
        fn slice_from_raw_parts(data: Self, len: usize) -> NonNull<[T]>;
    }

    impl<T> NonNullExt<T> for NonNull<T> {
        // NOTE on coverage: this will never be tested in nightly since it's a
        // polyfill for a feature which has been stabilized on our nightly
        // toolchain.
        #[cfg_attr(
            all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS),
            coverage(off)
        )]
        #[inline(always)]
        fn slice_from_raw_parts(data: Self, len: usize) -> NonNull<[T]> {
            let ptr = ptr::slice_from_raw_parts_mut(data.as_ptr(), len);
            // SAFETY: `ptr` is converted from `data`, which is non-null.
            unsafe { NonNull::new_unchecked(ptr) }
        }
    }

    // A polyfill for `Self::unchecked_sub` that we can use until methods like
    // `usize::unchecked_sub` is stabilized.
    //
    // The `#[allow(unused)]` is necessary because, on sufficiently recent
    // toolchain versions, `ptr.slice_from_raw_parts()` resolves to the inherent
    // method rather than to this trait, and so this trait is considered unused.
    //
    // FIXME(#67): Once our MSRV is high enough, remove this.
    #[allow(unused)]
    pub(crate) trait NumExt {
        /// Add without checking for overflow.
        ///
        /// # Safety
        ///
        /// The caller promises that the addition will not overflow.
        unsafe fn unchecked_add(self, rhs: Self) -> Self;

        /// Subtract without checking for underflow.
        ///
        /// # Safety
        ///
        /// The caller promises that the subtraction will not underflow.
        unsafe fn unchecked_sub(self, rhs: Self) -> Self;

        /// Multiply without checking for overflow.
        ///
        /// # Safety
        ///
        /// The caller promises that the multiplication will not overflow.
        unsafe fn unchecked_mul(self, rhs: Self) -> Self;
    }

    // NOTE on coverage: these will never be tested in nightly since they're
    // polyfills for a feature which has been stabilized on our nightly
    // toolchain.
    impl NumExt for usize {
        #[cfg_attr(
            all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS),
            coverage(off)
        )]
        #[inline(always)]
        unsafe fn unchecked_add(self, rhs: usize) -> usize {
            match self.checked_add(rhs) {
                Some(x) => x,
                None => {
                    // SAFETY: The caller promises that the addition will not
                    // underflow.
                    unsafe { core::hint::unreachable_unchecked() }
                }
            }
        }

        #[cfg_attr(
            all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS),
            coverage(off)
        )]
        #[inline(always)]
        unsafe fn unchecked_sub(self, rhs: usize) -> usize {
            match self.checked_sub(rhs) {
                Some(x) => x,
                None => {
                    // SAFETY: The caller promises that the subtraction will not
                    // underflow.
                    unsafe { core::hint::unreachable_unchecked() }
                }
            }
        }

        #[cfg_attr(
            all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS),
            coverage(off)
        )]
        #[inline(always)]
        unsafe fn unchecked_mul(self, rhs: usize) -> usize {
            match self.checked_mul(rhs) {
                Some(x) => x,
                None => {
                    // SAFETY: The caller promises that the multiplication will
                    // not overflow.
                    unsafe { core::hint::unreachable_unchecked() }
                }
            }
        }
    }
}

#[cfg(test)]
pub(crate) mod testutil {
    use crate::*;

    /// A `T` which is aligned to at least `align_of::<A>()`.
    #[derive(Default)]
    pub(crate) struct Align<T, A> {
        pub(crate) t: T,
        _a: [A; 0],
    }

    impl<T: Default, A> Align<T, A> {
        pub(crate) fn set_default(&mut self) {
            self.t = T::default();
        }
    }

    impl<T, A> Align<T, A> {
        pub(crate) const fn new(t: T) -> Align<T, A> {
            Align { t, _a: [] }
        }
    }

    /// A `T` which is guaranteed not to satisfy `align_of::<A>()`.
    ///
    /// It must be the case that `align_of::<T>() < align_of::<A>()` in order
    /// for this type to work properly.
    #[repr(C)]
    pub(crate) struct ForceUnalign<T: Unaligned, A> {
        // The outer struct is aligned to `A`, and, thanks to `repr(C)`, `t` is
        // placed at the minimum offset that guarantees its alignment. If
        // `align_of::<T>() < align_of::<A>()`, then that offset will be
        // guaranteed *not* to satisfy `align_of::<A>()`.
        //
        // Note that we need `T: Unaligned` in order to guarantee that there is
        // no padding between `_u` and `t`.
        _u: u8,
        pub(crate) t: T,
        _a: [A; 0],
    }

    impl<T: Unaligned, A> ForceUnalign<T, A> {
        pub(crate) fn new(t: T) -> ForceUnalign<T, A> {
            ForceUnalign { _u: 0, t, _a: [] }
        }
    }
    // A `u64` with alignment 8.
    //
    // Though `u64` has alignment 8 on some platforms, it's not guaranteed. By
    // contrast, `AU64` is guaranteed to have alignment 8 on all platforms.
    #[derive(
        KnownLayout,
        Immutable,
        FromBytes,
        IntoBytes,
        Eq,
        PartialEq,
        Ord,
        PartialOrd,
        Default,
        Debug,
        Copy,
        Clone,
    )]
    #[repr(C, align(8))]
    pub(crate) struct AU64(pub(crate) u64);

    impl AU64 {
        // Converts this `AU64` to bytes using this platform's endianness.
        pub(crate) fn to_bytes(self) -> [u8; 8] {
            crate::transmute!(self)
        }
    }

    impl Display for AU64 {
        #[cfg_attr(
            all(coverage_nightly, __ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS),
            coverage(off)
        )]
        fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
            Display::fmt(&self.0, f)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_round_down_to_next_multiple_of_alignment() {
        fn alt_impl(n: usize, align: NonZeroUsize) -> usize {
            let mul = n / align.get();
            mul * align.get()
        }

        for align in [1, 2, 4, 8, 16] {
            for n in 0..256 {
                let align = NonZeroUsize::new(align).unwrap();
                let want = alt_impl(n, align);
                let got = round_down_to_next_multiple_of_alignment(n, align);
                assert_eq!(got, want, "round_down_to_next_multiple_of_alignment({}, {})", n, align);
            }
        }
    }

    #[rustversion::since(1.57.0)]
    #[test]
    #[should_panic]
    fn test_round_down_to_next_multiple_of_alignment_zerocopy_panic_in_const_and_vec_try_reserve() {
        round_down_to_next_multiple_of_alignment(0, NonZeroUsize::new(3).unwrap());
    }
    #[test]
    fn test_send_sync_phantom_data() {
        let x = SendSyncPhantomData::<u8>::default();
        let y = x.clone();
        assert!(x == y);
        assert!(x == SendSyncPhantomData::<u8>::default());
    }

    #[test]
    #[allow(clippy::as_conversions)]
    fn test_as_address() {
        let x = 0u8;
        let r = &x;
        let mut x_mut = 0u8;
        let rm = &mut x_mut;
        let p = r as *const u8;
        let pm = rm as *mut u8;
        let nn = NonNull::new(p as *mut u8).unwrap();

        assert_eq!(AsAddress::addr(r), p as usize);
        assert_eq!(AsAddress::addr(rm), pm as usize);
        assert_eq!(AsAddress::addr(p), p as usize);
        assert_eq!(AsAddress::addr(pm), pm as usize);
        assert_eq!(AsAddress::addr(nn), p as usize);
    }
}
