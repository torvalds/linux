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

use core::{mem, num::NonZeroUsize};

use crate::util;

/// The target pointer width, counted in bits.
const POINTER_WIDTH_BITS: usize = mem::size_of::<usize>() * 8;

/// The layout of a type which might be dynamically-sized.
///
/// `DstLayout` describes the layout of sized types, slice types, and "slice
/// DSTs" - ie, those that are known by the type system to have a trailing slice
/// (as distinguished from `dyn Trait` types - such types *might* have a
/// trailing slice type, but the type system isn't aware of it).
///
/// Note that `DstLayout` does not have any internal invariants, so no guarantee
/// is made that a `DstLayout` conforms to any of Rust's requirements regarding
/// the layout of real Rust types or instances of types.
#[doc(hidden)]
#[allow(missing_debug_implementations, missing_copy_implementations)]
#[cfg_attr(any(kani, test), derive(Debug, PartialEq, Eq))]
#[derive(Copy, Clone)]
#[cfg_attr(kani, derive(kani::Arbitrary))]
pub struct DstLayout {
    pub(crate) align: NonZeroUsize,
    pub(crate) size_info: SizeInfo,
    // Is it guaranteed statically (without knowing a value's runtime metadata)
    // that the top-level type contains no padding? This does *not* apply
    // recursively - for example, `[(u8, u16)]` has `statically_shallow_unpadded
    // = true` even though this type likely has padding inside each `(u8, u16)`.
    pub(crate) statically_shallow_unpadded: bool,
}

#[cfg_attr(any(kani, test), derive(Debug, PartialEq, Eq))]
#[derive(Copy, Clone)]
#[cfg_attr(kani, derive(kani::Arbitrary))]
pub(crate) enum SizeInfo<E = usize> {
    Sized { size: usize },
    SliceDst(TrailingSliceLayout<E>),
}

/// The alignment and phase of a rounded size calculation.
///
/// If `encoded` is the stored non-zero integer, then its highest set bit is a
/// power-of-two alignment `align`, and its remaining lower bits are a `phase`
/// in `0..align`. Thus every non-zero bit pattern is a valid encoding, and:
///
/// ```text
/// encoded = align + phase
/// ```
#[cfg_attr(any(kani, test), derive(Debug, PartialEq, Eq))]
#[repr(transparent)]
#[derive(Copy, Clone)]
#[cfg_attr(kani, derive(kani::Arbitrary))]
pub(crate) struct RoundingAlignAndPhase(NonZeroUsize);

impl RoundingAlignAndPhase {
    /// Encodes `align` and `phase`.
    ///
    /// # Panics
    ///
    /// Panics if `align` is not a power of two and `phase` is not less than
    /// `align`.
    #[inline(always)]
    #[cfg_attr(kani, kani::requires(align.is_power_of_two() && phase < align.get()))]
    #[cfg_attr(kani, kani::ensures(|result| result.0.get() == align.get() + phase))]
    pub(crate) const fn new(align: NonZeroUsize, phase: usize) -> Self {
        #[cfg(kani)]
        #[kani::proof_for_contract(RoundingAlignAndPhase::new)]
        #[kani::solver(kissat)]
        fn proof() {
            RoundingAlignAndPhase::new(kani::any(), kani::any());
        }

        const_assert!(align.get().is_power_of_two());
        const_assert!(phase < align.get());

        // Since `align` is a power of two and `phase < align`, their set bits
        // are disjoint. The result is therefore non-zero and losslessly stores
        // both components.
        let encoded = align.get() | phase;
        match NonZeroUsize::new(encoded) {
            Some(encoded) => Self(encoded),
            None => const_unreachable!(),
        }
    }

    /// Decodes the alignment and phase.
    #[inline(always)]
    #[cfg_attr(kani, kani::ensures(|&(align, phase)| {
        align.is_power_of_two() && phase < align.get()
            && align.get().checked_add(phase) == Some(self.0.get())
    }))]
    pub(crate) const fn components(self) -> (NonZeroUsize, usize) {
        #[cfg(kani)]
        #[kani::proof_for_contract(RoundingAlignAndPhase::components)]
        #[kani::solver(kissat)]
        fn proof() {
            kani::any::<RoundingAlignAndPhase>().components();
        }
        // `leading_zeros <= POINTER_WIDTH_BITS - 1` because the encoded value
        // is non-zero. Converting `leading_zeros` to `usize` cannot truncate
        // because it is at most the number of bits in a `usize`.
        #[allow(
            clippy::arithmetic_side_effects,
            clippy::as_conversions,
            clippy::needless_nonzero_get
        )]
        let shift = (POINTER_WIDTH_BITS - 1) - (self.0.get().leading_zeros() as usize);
        #[allow(clippy::arithmetic_side_effects)]
        let align = 1usize << shift;
        let align = match NonZeroUsize::new(align) {
            Some(align) => align,
            None => const_unreachable!(),
        };
        // `align` is the highest set bit, so XOR removes exactly that bit and
        // leaves a value in `0..align`.
        let phase = self.0.get() ^ align.get();
        (align, phase)
    }

    /// Decodes the alignment, which is the highest set bit of `self`.
    #[inline(always)]
    #[cfg_attr(kani, kani::ensures(|align| {
        align.is_power_of_two() && align.get() <= self.0.get()
            && self.0.get() - align.get() < align.get()
    }))]
    pub(crate) const fn align(self) -> NonZeroUsize {
        #[cfg(kani)]
        #[kani::proof_for_contract(RoundingAlignAndPhase::align)]
        #[kani::solver(kissat)]
        fn proof() {
            kani::any::<RoundingAlignAndPhase>().align();
        }

        self.components().0
    }
}

#[cfg_attr(any(kani, test), derive(Debug, PartialEq, Eq))]
#[derive(Copy, Clone)]
#[cfg_attr(kani, derive(kani::Arbitrary))]
pub(crate) struct TrailingSliceLayout<E = usize> {
    /// The offset of the first byte of the trailing slice field. Note that this
    /// is NOT the same as the minimum size of the type. For example, consider
    /// the following type:
    ///
    /// ```ignore
    /// #[repr(C, align(2))]
    /// struct Foo {
    ///     header: u16,
    ///     tag: u8,
    ///     tail: [u8],
    /// }
    /// ```
    ///
    /// In `Foo`, `tail` is at byte offset 3. When `tail.len() == 0`, `tail` is
    /// followed by a padding byte.
    pub(crate) offset: usize,
    /// The size of the element type of the trailing slice field.
    pub(crate) elem_size: E,
    /// The trailing slice's byte offset plus fixed trailing padding, minus the
    /// rounding phase.
    ///
    /// Fixed trailing padding excludes padding added by the final alignment
    /// rounding. The rounding alignment and phase are stored in
    /// `size_rounding_align_and_phase`. This value is independent of the slice
    /// length and can be smaller than, equal to, or larger than `offset`.
    ///
    /// For a completed layout, this value is a multiple of `DstLayout::align`.
    /// The minimum object size equals this value when the phase is zero;
    /// otherwise, it exceeds this value by one rounding-alignment unit.
    pub(crate) size_base: usize,
    /// The alignment and byte phase of the rounding applied to the trailing
    /// slice's contribution to object size.
    ///
    /// The alignment `size_align` is a nonzero power of two. The phase
    /// `size_phase` is the fixed byte contribution inside that rounding, in
    /// `0..size_align`. Both are independent of the trailing slice's length.
    /// For `elems` trailing elements, the enclosing object's size is:
    ///
    /// ```text
    /// size_base + round_up(size_phase + elems * elem_size, size_align)
    /// ```
    ///
    /// For a completed layout, `size_align` is at least `DstLayout::align`.
    /// Packing can reduce the enclosing type's alignment and the alignment
    /// used to place its trailing field without removing padding required by
    /// a nested DST's own layout. For example, a `repr(packed(2))` wrapper
    /// around a DST with alignment 4 can have an object alignment of 2 and a
    /// rounding alignment of 4.
    ///
    /// The rounding parameters account for padding preserved within nested
    /// fields. Their rounding origin can differ from the physical start of
    /// the trailing slice, so the phase need not equal `offset % size_align`.
    pub(crate) size_rounding_align_and_phase: RoundingAlignAndPhase,
}

impl<E> TrailingSliceLayout<E> {
    /// Produces `self.size_base` rounded down to a multiple of `size_align`,
    /// plus `size_phase`, where both components come from
    /// `self.size_rounding_align_and_phase`.
    ///
    /// This value need not equal `self.offset`, the trailing slice's byte
    /// offset.
    #[inline(always)]
    #[cfg_attr(kani, kani::ensures(|&offset| {
        let (align, phase) = self.size_rounding_align_and_phase.components();
        offset == util::round_down_to_next_multiple_of_alignment(self.size_base, align) + phase
    }))]
    const fn size_offset(&self) -> usize {
        #[cfg(all(kani, kani_slow))]
        #[kani::proof_for_contract(TrailingSliceLayout::<usize>::size_offset)]
        #[kani::solver(kissat)]
        fn proof() {
            kani::any::<TrailingSliceLayout>().size_offset();
        }

        let (size_align, size_phase) = self.size_rounding_align_and_phase.components();
        let aligned_base =
            util::round_down_to_next_multiple_of_alignment(self.size_base, size_align);
        // The aligned base and phase have disjoint set bits.
        aligned_base | size_phase
    }

    /// Produces the largest trailing-slice byte capacity whose complete, padded
    /// object fits in `available_bytes`, or `None` if the object does not fit
    /// even with an empty trailing slice.
    ///
    /// This capacity is independent of `elem_size`: it need not represent a
    /// whole number of elements, even for a valid Rust layout. For example, a
    /// slice of `[u8; 3]` elements has three-byte elements and alignment 1.
    /// With `available_bytes = 4`, this method produces `Some(4)`, although
    /// only one whole element, occupying three bytes, fits.
    #[inline(always)]
    #[cfg_attr(kani, kani::ensures(|&result| {
        let (align, phase) = self.size_rounding_align_and_phase.components();
        result == available_bytes.checked_sub(self.size_base)
            .map(|bytes| util::round_down_to_next_multiple_of_alignment(bytes, align))
            .and_then(|rounded| rounded.checked_sub(phase))
    }))]
    const fn max_trailing_bytes(&self, available_bytes: usize) -> Option<usize> {
        #[cfg(all(kani, kani_slow))]
        #[kani::proof_for_contract(TrailingSliceLayout::<usize>::max_trailing_bytes)]
        #[kani::solver(kissat)]
        fn proof() {
            kani::any::<TrailingSliceLayout>().max_trailing_bytes(kani::any());
        }

        #[cfg(all(kani, kani_slow))]
        #[kani::proof_for_contract(TrailingSliceLayout::<NonZeroUsize>::max_trailing_bytes)]
        #[kani::solver(kissat)]
        fn proof_nonzero() {
            kani::any::<TrailingSliceLayout<NonZeroUsize>>().max_trailing_bytes(kani::any());
        }

        let (size_align, size_phase) = self.size_rounding_align_and_phase.components();

        // Interpret the normalized size formula over the nonnegative integers:
        //
        //   object_size(trailing_bytes)
        //       = self.size_base
        //           + round_up(size_phase + trailing_bytes, size_align).
        //
        // We seek the largest byte count with
        // `object_size(trailing_bytes) <= available_bytes`.
        //
        // At zero trailing bytes, the input to rounding is just `size_phase`.
        // Since `0 <= size_phase < size_align`, the least alignment multiple at
        // or above it is zero for a zero phase, and `size_align` otherwise.
        // Thus `rounded_phase = round_up(size_phase, size_align)` is exactly
        // the rounded contribution of an empty tail.
        let rounded_phase = if size_phase == 0 { 0 } else { size_align.get() };

        // Adding the fixed base gives `minimum_size = object_size(0)`. The size
        // formula is nondecreasing in the trailing byte count, so this is the
        // minimum object size. If this sum exceeds `usize::MAX`, it also
        // exceeds `available_bytes`, and no tail can fit.
        let minimum_size = match self.size_base.checked_add(rounded_phase) {
            Some(size) => size,
            None => return None,
        };

        // Subtracting the minimum size gives the budget for increasing the
        // rounded contribution. Failure means even the minimum size exceeds the
        // available space. Success establishes `minimum_size + extra_bytes =
        // available_bytes`.
        let extra_bytes = match available_bytes.checked_sub(minimum_size) {
            Some(bytes) => bytes,
            None => return None,
        };

        // The rounded contribution can increase only by multiples of
        // `size_align`. Its largest permitted increase is therefore
        // `aligned_bytes = round_down(extra_bytes, size_align)`, with:
        //
        //   aligned_bytes <= extra_bytes < aligned_bytes + size_align.
        //
        // Hence `rounded_phase + aligned_bytes` is the largest alignment
        // multiple that fits after the base: adding the base gives at most
        // `minimum_size + extra_bytes = available_bytes`, while the next
        // alignment multiple would exceed that budget.
        let aligned_bytes = util::round_down_to_next_multiple_of_alignment(extra_bytes, size_align);

        // Rounding an empty tail adds `rounded_phase - size_phase` bytes. This
        // is `initial_padding`: the tail can use these bytes without increasing
        // the rounded contribution. The two cases for `rounded_phase` give `0
        // <= initial_padding < size_align`, so the subtraction is
        // representable.
        #[allow(clippy::arithmetic_side_effects)]
        let initial_padding = rounded_phase - size_phase;

        // For any nonnegative candidate byte count, the aligned limit above
        // gives the following equivalences:
        //
        //   object_size(candidate_bytes) <= available_bytes
        //       iff round_up(size_phase + candidate_bytes, size_align)
        //           <= rounded_phase + aligned_bytes
        //       iff size_phase + candidate_bytes
        //           <= rounded_phase + aligned_bytes
        //       iff candidate_bytes <= aligned_bytes + initial_padding.
        //
        // Removing `round_up` is valid because its upper bound is an alignment
        // multiple. Thus `aligned_bytes + initial_padding` is exactly the
        // largest tail that fits. Since `aligned_bytes` is a multiple of the
        // power-of-two `size_align` and `initial_padding < size_align`, their
        // set bits are disjoint. Bitwise OR therefore equals their sum, with no
        // carry or overflow.
        let trailing_bytes = aligned_bytes | initial_padding;
        Some(trailing_bytes)
    }
}

impl TrailingSliceLayout {
    /// Returns the difference between the complete object size and the
    /// trailing-slice end for `elems` elements, modulo `usize::MAX + 1`.
    ///
    /// If the object size fits in `usize` and contains the trailing slice,
    /// this is the exact trailing padding. Other inputs need not describe a
    /// Rust type; their result is still the modular difference.
    #[inline(always)]
    #[cfg_attr(kani, kani::ensures(|&padding| {
        let (align, phase) = self.size_rounding_align_and_phase.components();
        let trailing_bytes = elems.wrapping_mul(self.elem_size);
        let mask = align.get() - 1;
        let rounded = phase.wrapping_add(trailing_bytes).wrapping_add(mask) & !mask;
        let object_size = self.size_base.wrapping_add(rounded);
        padding == object_size.wrapping_sub(self.offset.wrapping_add(trailing_bytes))
    }))]
    pub(crate) const fn padding_for_elems(self, elems: usize) -> usize {
        #[cfg(all(kani, kani_slow))]
        #[kani::proof_for_contract(TrailingSliceLayout::padding_for_elems)]
        #[kani::solver(kissat)]
        fn proof() {
            kani::any::<TrailingSliceLayout>().padding_for_elems(kani::any());
        }

        let (size_align, size_phase) = self.size_rounding_align_and_phase.components();

        // Since `size_align` is a nonzero power of two, subtracting one gives
        // a mask selecting precisely the remainder modulo `size_align`.
        #[allow(clippy::arithmetic_side_effects)]
        let size_mask = size_align.get() - 1;

        // Removing whole alignment multiples from each element's size
        // preserves the trailing-byte count modulo `size_align`. If elements
        // occupy whole alignment units, this is zero, making the padding's
        // independence from the element count explicit to the optimizer.
        let elem_remainder = self.elem_size & size_mask;

        // The product is congruent to `elems * self.elem_size` modulo
        // `size_align`. Wrapping preserves that congruence because every
        // power-of-two `size_align` divides `usize::MAX + 1`. Masking then
        // gives the exact trailing-byte remainder in `0..size_align`.
        let trailing_remainder = elems.wrapping_mul(elem_remainder) & size_mask;

        // Adding `size_phase` gives the same remainder as the mathematical
        // `size_phase + elems * self.elem_size`. Reducing before adding the
        // phase also exposes this periodicity to the optimizer.
        let rounding_input = size_phase.wrapping_add(trailing_remainder);

        // `rounding_padding` is the unique value in `0..size_align` that
        // rounds `rounding_input` up to an alignment multiple. Since only
        // the remainder determines this value, it is also the padding added
        // by `round_up(size_phase + elems * self.elem_size, size_align)`.
        let rounding_padding = util::padding_needed_for(rounding_input, size_align);

        // Write `trailing_bytes = elems * self.elem_size` over mathematical
        // integers. Subtracting the trailing-slice end from the complete
        // object size cancels `trailing_bytes`:
        //
        //   size_base + round_up(size_phase + trailing_bytes, size_align)
        //       - (offset + trailing_bytes)
        //       = size_base + size_phase - offset + rounding_padding.
        //
        // Wrapping computes this difference modulo `usize::MAX + 1`, even
        // if an intermediate value or the difference is unrepresentable.
        // If the object size fits in `usize` and contains the trailing slice,
        // the difference is in `0..=usize::MAX`; its residue is then the
        // exact number of padding bytes after the trailing slice.
        self.size_base
            .wrapping_add(size_phase)
            .wrapping_sub(self.offset)
            .wrapping_add(rounding_padding)
    }

    /// Computes the size described by this layout for `elems` trailing slice
    /// elements.
    ///
    /// Returns `None` if the element-byte count or complete rounded size
    /// cannot be represented as a `usize`.
    #[inline(always)]
    #[cfg_attr(kani, kani::ensures(|&size| {
        size == elems.checked_mul(self.elem_size)
            .and_then(|bytes| proofs::slice_dst_size_for_trailing_bytes(self, bytes))
    }))]
    pub(crate) const fn size_for_elems(self, elems: usize) -> Option<usize> {
        #[cfg(all(kani, kani_slow))]
        #[kani::proof_for_contract(TrailingSliceLayout::size_for_elems)]
        #[kani::solver(kissat)]
        fn proof() {
            kani::any::<TrailingSliceLayout>().size_for_elems(kani::any());
        }

        // Let `size_align` and `size_phase` denote the components stored in
        // `size_rounding_align_and_phase`. The normalized layout formula,
        // evaluated over the nonnegative integers, is:
        //
        //   object_size(trailing_bytes)
        //       = self.size_base
        //           + round_up(size_phase + trailing_bytes, size_align)
        //
        // Here, `round_up` gives the least multiple of `size_align` greater
        // than or equal to its input. Thus `object_size` is nondecreasing.
        // By the capacity helper's contract, `max_trailing_bytes` is the
        // largest byte count for which `object_size` fits in a `usize`.
        // Monotonicity then gives the equivalence:
        //
        //   object_size(trailing_bytes) <= usize::MAX
        //       iff trailing_bytes <= max_trailing_bytes.
        //
        // If the helper returns `None`, even `object_size(0)` exceeds the
        // limit, so no element count can produce a representable size.
        let max_trailing_bytes = match self.max_trailing_bytes(usize::MAX) {
            Some(bytes) => bytes,
            None => return None,
        };

        // Each of the `elems` elements contributes `self.elem_size` bytes,
        // so substitution into the formula requires their product. Checked
        // multiplication yields that exact product, including zero for
        // zero-sized elements. If it overflows, the complete size cannot fit
        // either: `object_size(trailing_bytes) >= trailing_bytes` because
        // the base, phase, and rounding increment are all nonnegative.
        let trailing_size = match self.elem_size.checked_mul(elems) {
            Some(bytes) => bytes,
            None => return None,
        };

        // The equivalence above rejects exactly the remaining cases whose
        // complete size exceeds `usize::MAX`. After this check we have
        // `object_size(trailing_size) <= usize::MAX`.
        if trailing_size > max_trailing_bytes {
            return None;
        }

        // The trailing slice starts at `self.offset` and occupies
        // `trailing_size` bytes. Thus `trailing_end` is its end offset modulo
        // `usize::MAX + 1`. This sum need not fit: a raw layout need not
        // describe a Rust type, so its trailing slice need not lie within
        // the complete object size bounded above.
        let trailing_end = self.offset.wrapping_add(trailing_size);

        // `padding_for_elems` returns `object_size(trailing_size)` minus the
        // trailing-slice end, modulo `usize::MAX + 1`. Adding the end back
        // therefore gives `object_size(trailing_size)` modulo the same
        // modulus. The bound above establishes that this size fits in
        // `usize`, so its residue is the exact size specified by the layout.
        let size = trailing_end.wrapping_add(self.padding_for_elems(elems));
        Some(size)
    }

    /// Returns `true` only when `self` and `other` describe the same size for
    /// every trailing slice length.
    ///
    /// This recognizes sufficient conditions for equality. A `false` result
    /// also includes equivalent size sequences that this test cannot recognize.
    #[inline(always)]
    #[cfg_attr(kani, kani::ensures(|&same| {
        let (self_align, self_phase) = self.size_rounding_align_and_phase.components();
        let (other_align, other_phase) = other.size_rounding_align_and_phase.components();
        let initial_size = proofs::slice_dst_size_for_trailing_bytes(self, 0);
        same == (self.elem_size == other.elem_size
            && initial_size.is_some()
            && initial_size == proofs::slice_dst_size_for_trailing_bytes(other, 0)
            && ((self.elem_size % self_align.get() == 0
                && self.elem_size % other_align.get() == 0)
                || (self_align == other_align && self_phase == other_phase)))
    }))]
    pub(crate) const fn has_same_size_sequence(self, other: Self) -> bool {
        #[cfg(all(kani, kani_slow))]
        #[kani::proof_for_contract(TrailingSliceLayout::has_same_size_sequence)]
        #[kani::solver(kissat)]
        fn proof() {
            kani::any::<TrailingSliceLayout>().has_same_size_sequence(kani::any());
        }

        // For either layout, interpret its normalized size formula over the
        // nonnegative integers:
        //
        //   object_size(elems)
        //       = size_base
        //           + round_up(size_phase + elems * elem_size, size_align).
        //
        // In two cases below, we establish equality of these mathematical sizes
        // for every `elems`. Equal sizes also have the same `usize`
        // representability bound.
        //
        // Both cases require a common byte contribution at each element count.
        // Equal element sizes give that shared term, `elems * elem_size`.
        if self.elem_size != other.elem_size {
            return false;
        }

        // At `elems == 0`, the element contribution vanishes. Require equal,
        // representable initial sizes; each case below then shows that
        // increasing `elems` adds the same amount to both initial sizes.
        match (self.size_for_elems(0), other.size_for_elems(0)) {
            (Some(self_size), Some(other_size)) if self_size == other_size => {}
            _ => return false,
        }

        let (self_align, self_phase) = self.size_rounding_align_and_phase.components();
        let (other_align, other_phase) = other.size_rounding_align_and_phase.components();
        let self_align = self_align.get();
        let other_align = other_align.get();

        // Case 1: The element size is a multiple of both alignments.
        //
        // For two power-of-two alignments, the larger is divisible by the
        // smaller. Thus `max_align` is their least common multiple, and a byte
        // count is divisible by both alignments exactly when it is divisible by
        // `max_align`.
        let max_align = if self_align > other_align { self_align } else { other_align };

        // If the element size is divisible by both alignments, so is `elems *
        // elem_size`. Adding an alignment multiple commutes with rounding,
        // giving the following identity for each layout:
        //
        //   round_up(size_phase + elems * elem_size, size_align)
        //       = round_up(size_phase, size_align) + elems * elem_size.
        //
        // Consequently, `object_size(elems) = object_size(0) + elems *
        // elem_size`. The initial sizes and element sizes already agree, so the
        // complete size sequences agree. This also covers zero-sized elements,
        // whose contribution is zero for every element count.
        #[allow(clippy::arithmetic_side_effects)]
        if self.elem_size % max_align == 0 {
            return true;
        }

        // Case 2: The rounding alignments and phases match.
        //
        // Require a common rounding alignment before comparing phases. If the
        // alignments differ, neither case establishes equality.
        if self_align != other_align {
            return false;
        }

        // If the phases also agree, both `round_up` operations have the same
        // input and alignment at every element count. In particular, their
        // rounded terms agree at zero. Subtracting that common term from the
        // equal initial sizes proves `self.size_base == other.size_base`. Both
        // the fixed and rounded contributions therefore agree for every
        // `elems`, which establishes equality of the complete size formulas.
        self_phase == other_phase
    }

    /// Advances the size formula by `bytes` while replacing its element size.
    ///
    /// Let `(size_align, size_phase)` be decoded from
    /// `self.size_rounding_align_and_phase`. For a trailing slice length
    /// `elems`, the returned layout describes:
    ///
    /// ```text
    /// self.size_base
    ///     + round_up(size_phase + bytes + elems * elem_size, size_align)
    /// ```
    ///
    /// Here, `elem_size` is the replacement element size passed to this
    /// method. The returned layout preserves the physical slice offset
    /// `self.offset` and the rounding alignment; advancing changes the size
    /// calculation.
    ///
    /// Returns `None` if advancing cannot produce representable normalized
    /// components. Even when those components fit, evaluating the rounded
    /// object size may overflow for some or all trailing slice lengths.
    #[inline(always)]
    #[cfg_attr(kani, kani::ensures(|result| {
        let expected_offset = self.size_offset().checked_add(bytes);
        match result {
            Some(advanced) => {
                let align = self.size_rounding_align_and_phase.align();
                expected_offset == Some(advanced.size_offset())
                    && advanced.offset == self.offset
                    && advanced.elem_size == elem_size
                    && advanced.size_rounding_align_and_phase.align() == align
                    && (advanced.size_base & (align.get() - 1)) == (self.size_base & (align.get() - 1))
            }
            None => expected_offset.is_none(),
        }
    }))]
    const fn advance(self, bytes: usize, elem_size: usize) -> Option<Self> {
        #[cfg(all(kani, kani_slow))]
        #[kani::proof_for_contract(TrailingSliceLayout::advance)]
        #[kani::solver(kissat)]
        fn proof() {
            kani::any::<TrailingSliceLayout>().advance(kani::any(), kani::any());
        }

        let (size_align, size_phase) = self.size_rounding_align_and_phase.components();

        // Advancing replaces the phase inside `round_up` with `size_phase +
        // bytes`. To normalize this sum, separate it into whole alignment units
        // and a remainder. The whole units can move outside `round_up` into the
        // base; the remainder becomes the new phase.
        //
        // Since `size_align` is a nonzero power of two, `align_mask` selects
        // exactly the bits representing a remainder modulo `size_align`.
        // Nonzero `size_align` also makes this subtraction representable.
        #[allow(clippy::arithmetic_side_effects)]
        let align_mask = size_align.get() - 1;

        // The base can increase by at most `usize::MAX - self.size_base`. Since
        // only whole alignment units move into the base, the largest
        // transferable contribution is:
        //
        //   whole_capacity
        //       = round_down(usize::MAX - self.size_base, size_align).
        //
        // A remainder up to `align_mask` stays in the phase and consumes no
        // base capacity. Thus the largest advanced phase that can be split into
        // representable normalized components is `phase_capacity =
        // whole_capacity + align_mask`. Setting the low bits of the remaining
        // base capacity computes exactly that sum. Its two terms have disjoint
        // set bits, so it cannot overflow. Any larger sum would transfer more
        // whole units than the base can hold.
        #[allow(clippy::arithmetic_side_effects)]
        let phase_capacity = (usize::MAX - self.size_base) | align_mask;

        // The existing phase already occupies `size_phase` bytes of this
        // capacity. Subtracting it gives the greatest permitted increment:
        //
        //   bytes <= max_advance
        //       iff size_phase + bytes <= phase_capacity.
        //
        // Since `phase_capacity >= align_mask >= size_phase`, the subtraction
        // cannot underflow.
        #[allow(clippy::arithmetic_side_effects)]
        let max_advance = phase_capacity - size_phase;
        if bytes > max_advance {
            return None;
        }

        // This is the fixed term inside rounding after the requested
        // advancement, before normalization. The check above proves `size_phase
        // + bytes <= phase_capacity <= usize::MAX`, so the addition is
        // representable.
        #[allow(clippy::arithmetic_side_effects)]
        let advanced_phase = size_phase + bytes;

        // Taking the remainder retains precisely the part that cannot move
        // outside rounding as whole alignment units. In particular, `0 <=
        // normalized_phase < size_align`, as required for the new rounding
        // parameters.
        let normalized_phase = advanced_phase & align_mask;

        // Rounding down removes precisely the remainder retained in the
        // phase, so `whole_bytes = advanced_phase - normalized_phase`.
        let whole_bytes =
            util::round_down_to_next_multiple_of_alignment(advanced_phase, size_align);

        // Because `advanced_phase <= phase_capacity`, we have
        // `whole_bytes <= whole_capacity <= usize::MAX - self.size_base`.
        // Adding this transferred contribution to the base therefore fits.
        #[allow(clippy::arithmetic_side_effects)]
        let size_base = self.size_base + whole_bytes;

        // To verify the returned formula, let `trailing_size = elems *
        // elem_size` and evaluate the following over the nonnegative integers.
        // Since `whole_bytes` is a multiple of `size_align`, it can move back
        // inside `round_up` without changing the result:
        //
        //   size_base + round_up(normalized_phase + trailing_size, size_align)
        //       = self.size_base
        //           + round_up(whole_bytes + normalized_phase + trailing_size, size_align)
        //       = self.size_base
        //           + round_up(size_phase + bytes + trailing_size, size_align).
        //
        // This is the requested advanced formula. The replacement element size
        // supplies its new coefficient of `elems`; `offset` retains the
        // caller's field-placement description.
        Some(Self {
            offset: self.offset,
            elem_size,
            size_base,
            size_rounding_align_and_phase: RoundingAlignAndPhase::new(size_align, normalized_phase),
        })
    }
}

impl SizeInfo {
    /// Attempts to create a `SizeInfo` from `Self` in which `elem_size` is a
    /// `NonZeroUsize`. If `elem_size` is 0, returns `None`.
    #[allow(unused)]
    #[cfg_attr(not(zerocopy_inline_always), inline)]
    #[cfg_attr(zerocopy_inline_always, inline(always))]
    #[cfg_attr(kani, kani::ensures(|result| match (*self, *result) {
        (SizeInfo::Sized { size }, Some(SizeInfo::Sized { size: converted })) => {
            size == converted
        }
        (SizeInfo::SliceDst(original), Some(SizeInfo::SliceDst(converted))) => {
            converted.elem_size.get() == original.elem_size
                && converted.offset == original.offset
                && converted.size_base == original.size_base
                && converted.size_rounding_align_and_phase == original.size_rounding_align_and_phase
        }
        (SizeInfo::SliceDst(original), None) => original.elem_size == 0,
        _ => false,
    }))]
    const fn try_to_nonzero_elem_size(&self) -> Option<SizeInfo<NonZeroUsize>> {
        #[cfg(kani)]
        #[kani::proof_for_contract(SizeInfo::try_to_nonzero_elem_size)]
        #[kani::solver(kissat)]
        fn proof() {
            kani::any::<SizeInfo>().try_to_nonzero_elem_size();
        }

        Some(match *self {
            SizeInfo::Sized { size } => SizeInfo::Sized { size },
            SizeInfo::SliceDst(TrailingSliceLayout {
                offset,
                elem_size,
                size_base,
                size_rounding_align_and_phase,
            }) => {
                if let Some(elem_size) = NonZeroUsize::new(elem_size) {
                    SizeInfo::SliceDst(TrailingSliceLayout {
                        offset,
                        elem_size,
                        size_base,
                        size_rounding_align_and_phase,
                    })
                } else {
                    return None;
                }
            }
        })
    }
}

/// Returns the largest number of `elem_size`-byte elements which fit in
/// `bytes`, along with the number of bytes those elements occupy.
#[cfg_attr(
    kani,
    kani::ensures(|&(elems, used)| {
        (match elems.checked_mul(elem_size.get()) {
            Some(product) => {
                product == used && used <= bytes && bytes - used < elem_size.get()
            }
            None => false,
        }) && used
            .checked_add(elem_size.get())
            .map(|next_used| next_used > bytes)
            .unwrap_or(true)
    })
)]
#[inline(always)]
const fn max_elems_for_bytes(bytes: usize, elem_size: NonZeroUsize) -> (usize, usize) {
    #[cfg(kani)]
    #[kani::proof_for_contract(max_elems_for_bytes)]
    #[kani::solver(kissat)]
    fn proof() {
        max_elems_for_bytes(kani::any(), kani::any());
    }

    #[allow(clippy::arithmetic_side_effects)]
    let elems = bytes / elem_size.get();
    let used = match elems.checked_mul(elem_size.get()) {
        Some(used) => used,
        None => const_unreachable!(),
    };
    (elems, used)
}

#[doc(hidden)]
#[derive(Copy, Clone)]
#[cfg_attr(test, derive(Debug))]
#[allow(missing_debug_implementations)]
pub enum CastType {
    Prefix,
    Suffix,
}

#[cfg_attr(test, derive(Debug))]
#[cfg_attr(kani, derive(kani::Arbitrary))]
pub(crate) enum MetadataCastError {
    Alignment,
    Size,
}

impl DstLayout {
    /// The minimum possible alignment of a type.
    const MIN_ALIGN: NonZeroUsize = match NonZeroUsize::new(1) {
        Some(min_align) => min_align,
        None => const_unreachable!(),
    };

    /// The maximum theoretic possible alignment of a type.
    ///
    /// For compatibility with future Rust versions, this is defined as the
    /// maximum power-of-two that fits into a `usize`. See also
    /// [`DstLayout::CURRENT_MAX_ALIGN`].
    pub(crate) const THEORETICAL_MAX_ALIGN: NonZeroUsize =
        match NonZeroUsize::new(1 << (POINTER_WIDTH_BITS - 1)) {
            Some(max_align) => max_align,
            None => const_unreachable!(),
        };

    /// The current, documented max alignment of a type \[1\].
    ///
    /// \[1\] Per <https://doc.rust-lang.org/reference/type-layout.html#the-alignment-modifiers>:
    ///
    ///   The alignment value must be a power of two from 1 up to
    ///   2<sup>29</sup>.
    #[cfg(not(kani))]
    #[cfg(not(target_pointer_width = "16"))]
    pub(crate) const CURRENT_MAX_ALIGN: NonZeroUsize = match NonZeroUsize::new(1 << 29) {
        Some(max_align) => max_align,
        None => const_unreachable!(),
    };

    #[cfg(not(kani))]
    #[cfg(target_pointer_width = "16")]
    pub(crate) const CURRENT_MAX_ALIGN: NonZeroUsize = match NonZeroUsize::new(1 << 15) {
        Some(max_align) => max_align,
        None => const_unreachable!(),
    };

    /// The maximum size of an allocation \[1\].
    ///
    /// \[1\] Per <https://doc.rust-lang.org/1.91.1/std/ptr/index.html#allocation>:
    ///
    ///   For any allocation with base `address`, `size`, and a set of `addresses`,
    ///   the following are guaranteed: [..]
    ///
    ///   - `size <= isize::MAX`
    ///
    #[allow(clippy::as_conversions)]
    pub(crate) const MAX_SIZE: usize = isize::MAX as usize;

    /// Assumes that this layout lacks static shallow padding.
    ///
    /// # Panics
    ///
    /// This method does not panic.
    ///
    /// # Safety
    ///
    /// If `self` describes the size and alignment of type that lacks static
    /// shallow padding, unsafe code may assume that the result of this method
    /// accurately reflects the size, alignment, and lack of static shallow
    /// padding of that type.
    #[cfg_attr(kani, kani::ensures(|result| {
        result.align == self.align && result.size_info == self.size_info
            && result.statically_shallow_unpadded
    }))]
    const fn assume_shallow_unpadded(self) -> Self {
        #[cfg(kani)]
        #[kani::proof_for_contract(DstLayout::assume_shallow_unpadded)]
        #[kani::solver(kissat)]
        fn proof() {
            kani::any::<DstLayout>().assume_shallow_unpadded();
        }

        Self { statically_shallow_unpadded: true, ..self }
    }

    /// Constructs a `DstLayout` for a zero-sized type with `repr_align`
    /// alignment (or 1). If `repr_align` is provided, then it must be a power
    /// of two.
    ///
    /// # Panics
    ///
    /// This function panics if the supplied `repr_align` is not a power of two.
    ///
    /// # Safety
    ///
    /// Unsafe code may assume that the contract of this function is satisfied.
    #[doc(hidden)]
    #[must_use]
    #[inline]
    #[cfg_attr(kani, kani::requires(repr_align.map_or(true, |align| align.is_power_of_two())))]
    #[cfg_attr(kani, kani::ensures(|result| {
        result.align == repr_align.unwrap_or(Self::MIN_ALIGN)
            && result.size_info == SizeInfo::Sized { size: 0 }
            && result.statically_shallow_unpadded
    }))]
    pub const fn new_zst(repr_align: Option<NonZeroUsize>) -> DstLayout {
        #[cfg(kani)]
        #[kani::proof_for_contract(DstLayout::new_zst)]
        #[kani::solver(kissat)]
        fn proof() {
            let _ = DstLayout::new_zst(kani::any());
        }

        let align = match repr_align {
            Some(align) => align,
            None => Self::MIN_ALIGN,
        };

        const_assert!(align.get().is_power_of_two());

        DstLayout {
            align,
            size_info: SizeInfo::Sized { size: 0 },
            statically_shallow_unpadded: true,
        }
    }

    /// Constructs a `DstLayout` which describes `T` and assumes `T` may contain
    /// padding.
    ///
    /// # Safety
    ///
    /// Unsafe code may assume that `DstLayout` is the correct layout for `T`.
    #[doc(hidden)]
    #[must_use]
    #[inline]
    #[cfg_attr(kani, kani::ensures(|result| {
        result.align.get() == mem::align_of::<T>()
            && result.size_info == SizeInfo::Sized { size: mem::size_of::<T>() }
            && result.statically_shallow_unpadded == false
    }))]
    pub const fn for_type<T>() -> DstLayout {
        #[cfg(kani)]
        #[kani::proof_for_contract(DstLayout::for_type::<()>)]
        fn proof_zst() {
            let _ = DstLayout::for_type::<()>();
        }

        #[cfg(kani)]
        #[kani::proof_for_contract(DstLayout::for_type::<(u8, u64)>)]
        fn proof_padded() {
            let _ = DstLayout::for_type::<(u8, u64)>();
        }

        // SAFETY: `align` is correct by construction. `T: Sized`, and so it is
        // sound to initialize `size_info` to `SizeInfo::Sized { size }`; the
        // `size` field is also correct by construction. `unpadded` can safely
        // default to `false`.
        DstLayout {
            align: match NonZeroUsize::new(mem::align_of::<T>()) {
                Some(align) => align,
                None => const_unreachable!(),
            },
            size_info: SizeInfo::Sized { size: mem::size_of::<T>() },
            statically_shallow_unpadded: false,
        }
    }

    /// Produces `T`'s size and alignment with static shallow padding assumed
    /// absent.
    ///
    /// This method records the assumption without checking `T` for padding.
    /// It can be used to ignore padding within a field when computing padding
    /// introduced by its enclosing struct.
    ///
    /// # Safety
    ///
    /// Unsafe code may rely on the result's size and alignment being correct
    /// for `T`. Relying on its recorded absence of static shallow padding
    /// requires an independent justification that `T` lacks such padding.
    #[doc(hidden)]
    #[must_use]
    #[inline]
    #[cfg_attr(kani, kani::ensures(|result| {
        result.align.get() == mem::align_of::<T>()
            && result.size_info == SizeInfo::Sized { size: mem::size_of::<T>() }
            && result.statically_shallow_unpadded == true
    }))]
    pub const fn for_unpadded_type<T>() -> DstLayout {
        #[cfg(kani)]
        #[kani::proof_for_contract(DstLayout::for_unpadded_type::<()>)]
        fn proof_zst() {
            let _ = DstLayout::for_unpadded_type::<()>();
        }

        #[cfg(kani)]
        #[kani::proof_for_contract(DstLayout::for_unpadded_type::<(u8, u64)>)]
        fn proof_padded() {
            let _ = DstLayout::for_unpadded_type::<(u8, u64)>();
        }

        Self::for_type::<T>().assume_shallow_unpadded()
    }

    /// Constructs a `DstLayout` which describes `[T]`.
    ///
    /// # Safety
    ///
    /// Unsafe code may assume that `DstLayout` is the correct layout for `[T]`.
    #[cfg_attr(kani, kani::ensures(|result| {
        result.align.get() == mem::align_of::<T>() && result.statically_shallow_unpadded
            && matches!(result.size_info, SizeInfo::SliceDst(trailing)
                if trailing.offset == 0 && trailing.size_base == 0
                    && trailing.elem_size == mem::size_of::<T>()
                    && trailing.size_rounding_align_and_phase.components() == (result.align, 0))
    }))]
    pub(crate) const fn for_slice<T>() -> DstLayout {
        #[cfg(kani)]
        #[kani::proof_for_contract(DstLayout::for_slice::<()>)]
        fn proof_zst() {
            let _ = DstLayout::for_slice::<()>();
        }

        #[cfg(kani)]
        #[kani::proof_for_contract(DstLayout::for_slice::<(u8, u64)>)]
        fn proof_padded() {
            let _ = DstLayout::for_slice::<(u8, u64)>();
        }

        let align = match NonZeroUsize::new(mem::align_of::<T>()) {
            Some(align) => align,
            None => const_unreachable!(),
        };

        // SAFETY: The alignment of a slice is equal to the alignment of its
        // element type, and so `align` is initialized correctly.
        //
        // Since this is just a slice type, there is no offset between the
        // beginning of the type and the beginning of the slice, so it is
        // correct to set `offset: 0`. The `elem_size` is correct by
        // construction. Since `[T]` is a (degenerate case of a) slice DST, it
        // is correct to initialize `size_info` to `SizeInfo::SliceDst`.
        DstLayout {
            align,
            size_info: SizeInfo::SliceDst(TrailingSliceLayout {
                offset: 0,
                elem_size: mem::size_of::<T>(),
                size_base: 0,
                size_rounding_align_and_phase: RoundingAlignAndPhase::new(align, 0),
            }),
            statically_shallow_unpadded: true,
        }
    }

    /// Constructs a complete `DstLayout` reflecting a `repr(C)` struct with the
    /// given alignment modifiers and fields.
    ///
    /// This method cannot be used to match the layout of a record with the
    /// default representation, as that representation is mostly unspecified.
    ///
    /// # Safety
    ///
    /// For any definition of a `repr(C)` struct, if this method is invoked with
    /// alignment modifiers and fields corresponding to that definition, the
    /// resulting `DstLayout` will correctly encode the layout of that struct.
    ///
    /// This guarantee uses one explicit compatibility premise for generic
    /// indirection: while Rust accepts an instantiation which transitively
    /// places a `repr(align)` type inside a `repr(packed)` type, that
    /// instantiation follows the otherwise-applicable `repr(C)`,
    /// `repr(packed)`, and inner-type layout rules. The Reference prohibits the
    /// direct form, and Rust's continued acceptance and layout behavior are
    /// outside the algebra proved by this implementation. Re-audit if the
    /// compiler rejects such an instantiation or assigns it a different
    /// layout.
    ///
    /// We make no guarantees to the behavior of this method when it is invoked
    /// with arguments that cannot correspond to a valid `repr(C)` struct.
    #[must_use]
    #[inline]
    #[cfg_attr(kani, kani::requires(proofs::repr_c_layout(repr_align, repr_packed, fields).is_some()))]
    #[cfg_attr(kani, kani::ensures(|&result| {
        Some(result) == proofs::repr_c_layout(repr_align, repr_packed, fields)
    }))]
    pub const fn for_repr_c_struct(
        repr_align: Option<NonZeroUsize>,
        repr_packed: Option<NonZeroUsize>,
        fields: &[DstLayout],
    ) -> DstLayout {
        // This harness covers up to three fields, including an optional final
        // DST. Arbitrary-length composition is not established by this bound.
        #[cfg(all(kani, kani_slow))]
        #[kani::proof_for_contract(DstLayout::for_repr_c_struct)]
        #[kani::solver(kissat)]
        #[kani::unwind(4)]
        fn proof() {
            let fields: [DstLayout; 3] = kani::any();
            let len: usize = kani::any();
            kani::assume(len <= fields.len());
            let _ = DstLayout::for_repr_c_struct(kani::any(), kani::any(), &fields[..len]);
        }

        let mut layout = DstLayout::new_zst(repr_align);

        let mut i = 0;
        #[allow(clippy::arithmetic_side_effects)]
        while i < fields.len() {
            #[allow(clippy::indexing_slicing)]
            let field = fields[i];
            layout = layout.extend(field, repr_packed);
            i += 1;
        }

        layout = layout.pad_to_align();

        // SAFETY: `layout` accurately describes the layout of a `repr(C)`
        // struct with `repr_align` or `repr_packed` alignment modifications and
        // the given `fields`. The `layout` is constructed using a sequence of
        // invocations of `DstLayout::{new_zst,extend,pad_to_align}`. The
        // documentation of these items vows that invocations in this manner
        // will accurately describe a type, so long as:
        //
        //  - that type is `repr(C)`,
        //  - its fields are enumerated in the order they appear,
        //  - the presence of `repr_align` and `repr_packed` are correctly accounted for.
        //
        // We respect all three of these preconditions above.
        layout
    }

    /// Like `Layout::extend`, this creates a layout that describes a record
    /// whose layout consists of `self` followed by `next` that includes the
    /// necessary inter-field padding, but not any trailing padding.
    ///
    /// In order to match the layout of a `#[repr(C)]` struct, this method
    /// should be invoked for each field in declaration order. To add trailing
    /// padding, call `DstLayout::pad_to_align` after extending the layout for
    /// all fields. If `self` corresponds to a type marked with
    /// `repr(packed(N))`, then `repr_packed` should be set to `Some(N)`,
    /// otherwise `None`.
    ///
    /// This method cannot be used to match the layout of a record with the
    /// default representation, as that representation is mostly unspecified.
    ///
    /// # Safety
    ///
    /// If a (potentially hypothetical) valid `repr(C)` Rust type begins with
    /// fields whose layout are `self`, and those fields are immediately
    /// followed by a field whose layout is `field`, then unsafe code may rely
    /// on `self.extend(field, repr_packed)` producing a layout that correctly
    /// encompasses those two components.
    ///
    /// We make no guarantees to the behavior of this method if these fragments
    /// cannot appear in a valid Rust type (e.g., the concatenation of the
    /// layouts would lead to a size larger than `isize::MAX`).
    #[doc(hidden)]
    #[must_use]
    #[inline]
    #[cfg_attr(kani, kani::requires(proofs::extended_layout(self, field, repr_packed).is_some()))]
    #[cfg_attr(kani, kani::ensures(|&result| {
        Some(result) == proofs::extended_layout(self, field, repr_packed)
    }))]
    pub const fn extend(self, field: DstLayout, repr_packed: Option<NonZeroUsize>) -> Self {
        #[cfg(kani)]
        #[kani::proof_for_contract(DstLayout::extend)]
        #[kani::solver(kissat)]
        fn proof() {
            let _ = kani::any::<DstLayout>().extend(kani::any(), kani::any());
        }

        use util::{max, min, padding_needed_for};

        // If `repr_packed` is `None`, there are no alignment constraints, and
        // the value can be defaulted to `THEORETICAL_MAX_ALIGN`.
        let max_align = match repr_packed {
            Some(max_align) => max_align,
            None => Self::THEORETICAL_MAX_ALIGN,
        };

        const_assert!(max_align.get().is_power_of_two());

        // We use Kani to prove that this method is robust to future increases
        // in Rust's maximum allowed alignment. However, if such a change ever
        // actually occurs, we'd like to be notified via assertion failures.
        #[cfg(not(kani))]
        {
            const_debug_assert!(self.align.get() <= DstLayout::CURRENT_MAX_ALIGN.get());
            const_debug_assert!(field.align.get() <= DstLayout::CURRENT_MAX_ALIGN.get());
            if let Some(repr_packed) = repr_packed {
                const_debug_assert!(repr_packed.get() <= DstLayout::CURRENT_MAX_ALIGN.get());
            }
        }

        // The field's alignment is clamped by `repr_packed` (i.e., the
        // `repr(packed(N))` attribute, if any) [1].
        //
        // [1] Per https://doc.rust-lang.org/reference/type-layout.html#the-alignment-modifiers:
        //
        //   The alignments of each field, for the purpose of positioning
        //   fields, is the smaller of the specified alignment and the alignment
        //   of the field's type.
        let field_align = min(field.align, max_align);

        // The struct's alignment is the maximum of its previous alignment and
        // `field_align`.
        let align = max(self.align, field_align);

        let (interfield_padding, size_info) = match self.size_info {
            // If the layout is already a DST, we panic; DSTs cannot be extended
            // with additional fields.
            SizeInfo::SliceDst(..) => const_panic!("Cannot extend a DST with additional fields."),

            SizeInfo::Sized { size: preceding_size } => {
                // Compute the minimum amount of inter-field padding needed to
                // satisfy the field's alignment, and offset of the trailing
                // field. [1]
                //
                // [1] Per https://doc.rust-lang.org/reference/type-layout.html#the-alignment-modifiers:
                //
                //   Inter-field padding is guaranteed to be the minimum
                //   required in order to satisfy each field's (possibly
                //   altered) alignment.
                let padding = padding_needed_for(preceding_size, field_align);

                // This will not panic (and is proven to not panic, with Kani)
                // if the layout components can correspond to a leading layout
                // fragment of a valid Rust type, but may panic otherwise (e.g.,
                // combining or aligning the components would create a size
                // exceeding `isize::MAX`).
                let offset = match preceding_size.checked_add(padding) {
                    Some(offset) => offset,
                    None => const_panic!("Adding padding to `self`'s size overflows `usize`."),
                };

                (
                    padding,
                    match field.size_info {
                        SizeInfo::Sized { size: field_size } => {
                            // If the trailing field is sized, the resulting layout
                            // will be sized. Its size will be the sum of the
                            // preceding layout, the size of the new field, and the
                            // size of inter-field padding between the two.
                            //
                            // This will not panic (and is proven with Kani to not
                            // panic) if the layout components can correspond to a
                            // leading layout fragment of a valid Rust type, but may
                            // panic otherwise (e.g., combining or aligning the
                            // components would create a size exceeding
                            // `usize::MAX`).
                            let size = match offset.checked_add(field_size) {
                                Some(size) => size,
                                None => const_panic!("`field` cannot be appended without the total size overflowing `usize`"),
                            };
                            SizeInfo::Sized { size }
                        }
                        SizeInfo::SliceDst(TrailingSliceLayout {
                            offset: trailing_offset,
                            elem_size,
                            size_base,
                            size_rounding_align_and_phase,
                        }) => {
                            // If the trailing field is dynamically sized, so too
                            // will the resulting layout. The offset of the trailing
                            // slice component is the sum of the offset of the
                            // trailing field and the trailing slice offset within
                            // that field.
                            //
                            // This will not panic (and is proven with Kani to not
                            // panic) if the layout components can correspond to a
                            // leading layout fragment of a valid Rust type, but may
                            // panic otherwise (e.g., combining or aligning the
                            // components would create a size exceeding
                            // `usize::MAX`).
                            let trailing_offset = match offset.checked_add(trailing_offset) {
                                Some(offset) => offset,
                                None => const_panic!("`field` cannot be appended without the total size overflowing `usize`"),
                            };

                            // Before normalization, the composite's size is:
                            //
                            //   offset + size_base
                            //       + round_up(size_phase + elems * elem_size, size_align)
                            //
                            // `offset` does not participate in the rounding,
                            // so it can be added directly to the normalized
                            // base.
                            let size_base = match offset.checked_add(size_base) {
                                Some(base) => base,
                                None => const_panic!("`field` cannot be appended without the total size overflowing `usize`"),
                            };

                            SizeInfo::SliceDst(TrailingSliceLayout {
                                offset: trailing_offset,
                                elem_size,
                                size_base,
                                size_rounding_align_and_phase,
                            })
                        }
                    },
                )
            }
        };

        let statically_shallow_unpadded = self.statically_shallow_unpadded
            && field.statically_shallow_unpadded
            && interfield_padding == 0;

        DstLayout { align, size_info, statically_shallow_unpadded }
    }

    /// Like `Layout::pad_to_align`, this routine rounds the size of this layout
    /// up to the nearest multiple of this type's alignment. For DST layouts,
    /// this updates the runtime size formula to account for trailing padding.
    ///
    /// In order to match the layout of a `#[repr(C)]` struct, this method
    /// should be invoked after the invocations of [`DstLayout::extend`].
    ///
    /// This method cannot be used to match the layout of a record with the
    /// default representation, as that representation is mostly unspecified.
    ///
    /// # Safety
    ///
    /// If a (potentially hypothetical) valid `repr(C)` type begins with fields
    /// whose layout are `self` followed only by zero or more bytes of trailing
    /// padding (not included in `self`), then unsafe code may rely on
    /// `self.pad_to_align()` producing a layout that correctly
    /// encapsulates the layout of that type.
    ///
    /// We make no guarantees to the behavior of this method if `self` cannot
    /// appear in a valid Rust type (e.g., because the addition of trailing
    /// padding would lead to a size larger than `isize::MAX`).
    #[doc(hidden)]
    #[must_use]
    #[inline]
    #[cfg_attr(kani, kani::requires(proofs::padded_layout(self).is_some()))]
    #[cfg_attr(kani, kani::ensures(|&result| Some(result) == proofs::padded_layout(self)))]
    pub const fn pad_to_align(self) -> Self {
        #[cfg(all(kani, kani_slow))]
        #[kani::proof_for_contract(DstLayout::pad_to_align)]
        #[kani::solver(kissat)]
        fn proof() {
            let _ = kani::any::<DstLayout>().pad_to_align();
        }

        use util::padding_needed_for;

        let (static_padding, size_info) = match self.size_info {
            // For sized layouts, we add the minimum amount of trailing padding
            // needed to satisfy alignment.
            SizeInfo::Sized { size: unpadded_size } => {
                let padding = padding_needed_for(unpadded_size, self.align);
                let size = match unpadded_size.checked_add(padding) {
                    Some(size) => size,
                    None => const_panic!("Adding padding caused size to overflow `usize`."),
                };
                (padding, SizeInfo::Sized { size })
            }
            // For DST layouts, trailing padding depends on the length of the
            // trailing DST and is computed at runtime. Normalize the composed
            // rounding operation back into the representation documented on
            // `TrailingSliceLayout`.
            SizeInfo::SliceDst(mut trailing) => {
                let (size_align, size_phase) = trailing.size_rounding_align_and_phase.components();
                if size_align.get() < self.align.get() {
                    // For a trailing-slice byte count `trailing_size`, the
                    // input layout describes:
                    //
                    //   trailing.size_base
                    //       + round_up(size_phase + trailing_size, size_align)
                    //
                    // Here, `size_align < self.align`. Both alignments are
                    // powers of two, so rounding the complete input size to
                    // `self.align` is equivalent to:
                    //
                    //   rounded_base = round_up(trailing.size_base, size_align)
                    //   fixed_contribution = rounded_base + size_phase
                    //   new_phase = fixed_contribution % self.align
                    //   new_base = fixed_contribution - new_phase
                    //   new_base + round_up(new_phase + trailing_size, self.align)
                    //
                    // `prove_dst_layout_pad_to_align_dst_outer_alignment_transition`
                    // checks the exact field transition and preserved
                    // invariants. Its `_formula` companion proves this
                    // identity for every representable trailing byte count.
                    let base_padding = padding_needed_for(trailing.size_base, size_align);
                    let rounded_base = match trailing.size_base.checked_add(base_padding) {
                        Some(base) => base,
                        None => const_panic!("Adding padding caused size to overflow `usize`."),
                    };
                    let fixed_contribution = match rounded_base.checked_add(size_phase) {
                        Some(bytes) => bytes,
                        None => const_panic!("Adding padding caused size to overflow `usize`."),
                    };
                    #[allow(clippy::arithmetic_side_effects)]
                    let phase = fixed_contribution & (self.align.get() - 1);
                    // Rounding down removes `phase`, the fixed contribution
                    // modulo `self.align`, leaving the new base.
                    let size_base = util::round_down_to_next_multiple_of_alignment(
                        fixed_contribution,
                        self.align,
                    );
                    trailing.size_base = size_base;
                    trailing.size_rounding_align_and_phase =
                        RoundingAlignAndPhase::new(self.align, phase);
                } else {
                    // `size_align` is a multiple of `self.align`, so
                    // the inner rounded size is already aligned to
                    // `self.align`. Only `size_base` needs to be rounded.
                    // `prove_dst_layout_pad_to_align_dst_inner_rounding_transition`
                    // checks the exact field transition and preserved
                    // invariants. Its `_formula` companion proves size
                    // equivalence for every representable trailing byte count.
                    let padding = padding_needed_for(trailing.size_base, self.align);
                    trailing.size_base = match trailing.size_base.checked_add(padding) {
                        Some(base) => base,
                        None => const_panic!("Adding padding caused size to overflow `usize`."),
                    };
                }

                (0, SizeInfo::SliceDst(trailing))
            }
        };

        let statically_shallow_unpadded = self.statically_shallow_unpadded && static_padding == 0;

        DstLayout { align: self.align, size_info, statically_shallow_unpadded }
    }

    /// Produces `false` when this layout is marked as having no static shallow
    /// padding, and `true` otherwise.
    ///
    /// A `true` result is conservative: it does not establish that the type
    /// contains padding. For example, this method produces `true` for
    /// `DstLayout::for_type::<u8>()`, since `for_type` does not record whether
    /// static shallow padding is absent.
    #[must_use]
    #[inline(always)]
    #[cfg_attr(kani, kani::ensures(|&padding| padding == !self.statically_shallow_unpadded))]
    pub const fn requires_static_padding(self) -> bool {
        #[cfg(kani)]
        #[kani::proof_for_contract(DstLayout::requires_static_padding)]
        #[kani::solver(kissat)]
        fn proof() {
            let _ = kani::any::<DstLayout>().requires_static_padding();
        }

        !self.statically_shallow_unpadded
    }

    /// Produces `false` only if every valid metadata describes an instance
    /// which requires no dynamic trailing padding.
    ///
    /// A `true` result conservatively selects the dynamic-padding path; it does
    /// not guarantee that any valid metadata actually requires padding.
    #[must_use]
    #[inline(always)]
    #[cfg_attr(kani, kani::ensures(|&padding| match self.size_info {
        SizeInfo::Sized { .. } => !padding,
        SizeInfo::SliceDst(trailing) => padding == (
            proofs::slice_dst_size_for_trailing_bytes(trailing, 0) != Some(trailing.offset)
                || trailing.elem_size % trailing.size_rounding_align_and_phase.align().get() != 0
        ),
    }))]
    pub const fn requires_dynamic_padding(self) -> bool {
        #[cfg(all(kani, kani_slow))]
        #[kani::proof_for_contract(DstLayout::requires_dynamic_padding)]
        #[kani::solver(kissat)]
        fn proof() {
            let _ = kani::any::<DstLayout>().requires_dynamic_padding();
        }

        match self.size_info {
            SizeInfo::Sized { .. } => false,
            SizeInfo::SliceDst(trailing_slice_layout) => {
                // SAFETY: `proofs::prove_requires_dynamic_padding` formally
                // proves the safety-critical direction: when this predicate
                // returns false, no valid metadata requires dynamic padding.
                match trailing_slice_layout.size_for_elems(0) {
                    Some(initial_size) => {
                        #[allow(clippy::arithmetic_side_effects)]
                        let has_static_stride = trailing_slice_layout.elem_size
                            % trailing_slice_layout.size_rounding_align_and_phase.align().get()
                            == 0;
                        initial_size != trailing_slice_layout.offset || !has_static_stride
                    }
                    None => true,
                }
            }
        }
    }

    /// Returns the largest trailing slice length whose object size is exactly
    /// `size`.
    ///
    /// Returns `None` if there is no such length, if this describes a sized
    /// type, or if the trailing slice element is zero-sized.
    #[inline(always)]
    #[cfg_attr(kani, kani::ensures(|&result| {
        result == match self.size_info {
            SizeInfo::SliceDst(trailing) if trailing.elem_size != 0 => {
                proofs::max_trailing_bytes(trailing, size)
                    .map(|bytes| bytes / trailing.elem_size)
                    .filter(|&elems| trailing.size_for_elems(elems) == Some(size))
            }
            _ => None,
        }
    }))]
    const fn metadata_for_exact_size(&self, size: usize) -> Option<usize> {
        #[cfg(all(kani, kani_slow))]
        #[kani::proof_for_contract(DstLayout::metadata_for_exact_size)]
        #[kani::solver(kissat)]
        fn proof() {
            kani::any::<DstLayout>().metadata_for_exact_size(kani::any());
        }

        match self.size_info {
            SizeInfo::Sized { .. }
            | SizeInfo::SliceDst(TrailingSliceLayout { elem_size: 0, .. }) => None,
            SizeInfo::SliceDst(_) => {
                // Address zero is aligned to every supported alignment, and
                // `0 + size` cannot overflow. The validator returns the
                // largest metadata whose object fits in `size`; accepting it
                // only when the resulting object consumes all `size` bytes
                // turns that bounded query into an exact-size query.
                match self.validate_cast_and_convert_metadata(0, size, CastType::Prefix) {
                    Ok((elems, object_size)) if object_size == size => Some(elems),
                    _ => None,
                }
            }
        }
    }

    /// Given a completed type layout, produces metadata and a split index for
    /// a prefix or suffix cast that satisfies the type's size and alignment
    /// requirements.
    ///
    /// `addr` and `bytes_len` describe the address and size of the source
    /// memory region, and `cast_type` selects its prefix or suffix.
    ///
    /// If the cast is valid, `validate_cast_and_convert_metadata` returns
    /// `Ok((elems, split_at))`. For a dynamically-sized type, `elems` is the
    /// maximum number of trailing slice elements for which the cast is valid.
    /// For sized types, `elems` is meaningless and should be ignored.
    /// `split_at` is the index at which to split the memory region so that the
    /// prefix (suffix) contains the result of the cast and the remaining
    /// suffix (prefix) contains the leftover bytes.
    ///
    /// There are three conditions under which a cast can fail:
    ///
    /// - The smallest possible value for the type is larger than the provided
    ///   memory region
    /// - A prefix cast is requested, and `addr` does not satisfy `self`'s
    ///   alignment requirement
    /// - A suffix cast is requested, and `addr + bytes_len` does not satisfy
    ///   `self`'s alignment requirement (as a consequence, since all instances
    ///   of the type are a multiple of its alignment, no size for the type will
    ///   result in a starting address which is properly aligned)
    ///
    /// # Safety
    ///
    /// When `self` accurately describes a completed type layout, including
    /// trailing padding, and `addr + bytes_len` fits in `usize`, callers may
    /// rely on the following guarantees for `Ok((elems, split_at))`:
    ///
    /// - A pointer to the type (for dynamically sized types, this includes
    ///   `elems` as its pointer metadata) describes an object of size `size <=
    ///   bytes_len`
    /// - If this is a prefix cast:
    ///   - `addr` satisfies `self`'s alignment
    ///   - `size == split_at`
    /// - If this is a suffix cast:
    ///   - `split_at == bytes_len - size`
    ///   - `addr + split_at` satisfies `self`'s alignment
    ///
    /// These guarantees require the completed-layout premise; `DstLayout`
    /// also permits unfinished layout fragments and arbitrary field values.
    /// In particular, suffix alignment relies on every object size being a
    /// multiple of the type's alignment.
    ///
    /// Note that this method does *not* ensure that a pointer constructed from
    /// its return values will be a valid pointer. In particular, this method
    /// does not reason about `isize` overflow, which is a requirement of many
    /// Rust pointer APIs, and may at some point be determined to be a validity
    /// invariant of pointer types themselves. This should never be a problem so
    /// long as the arguments to this method are derived from a known-valid
    /// pointer (e.g., one derived from a safe Rust reference), but it is
    /// nonetheless the caller's responsibility to justify that pointer
    /// arithmetic will not overflow based on a safety argument *other than* the
    /// mere fact that this method returned successfully.
    ///
    /// # Panics
    ///
    /// `validate_cast_and_convert_metadata` will panic if `self` describes a
    /// DST whose trailing slice element is zero-sized.
    ///
    /// If `addr + bytes_len` overflows `usize`,
    /// `validate_cast_and_convert_metadata` may panic, or it may return
    /// incorrect results. No guarantees are made about when
    /// `validate_cast_and_convert_metadata` will panic. The caller should not
    /// rely on `validate_cast_and_convert_metadata` panicking in any particular
    /// condition, even if `debug_assertions` are enabled.
    #[allow(unused)]
    #[inline(always)]
    #[cfg_attr(kani, kani::requires(addr.checked_add(bytes_len).is_some()))]
    #[cfg_attr(kani, kani::requires(!matches!(self.size_info,
        SizeInfo::SliceDst(TrailingSliceLayout { elem_size: 0, .. }))))]
    #[cfg_attr(kani, kani::ensures(|result| {
        let endpoint = match cast_type {
            CastType::Prefix => addr,
            CastType::Suffix => addr + bytes_len,
        };
        if endpoint % self.align.get() != 0 {
            return matches!(result, Err(MetadataCastError::Alignment));
        }
        let candidate = match self.size_info {
            SizeInfo::Sized { size } => {
                if size <= bytes_len { Some((0, size)) } else { None }
            }
            SizeInfo::SliceDst(trailing) => {
                proofs::max_trailing_bytes(trailing, bytes_len).and_then(|bytes| {
                    let elems = bytes / trailing.elem_size;
                    trailing.size_for_elems(elems).map(|size| (elems, size))
                })
            }
        };
        match (result, candidate) {
            (Ok((elems, split_at)), Some((expected_elems, size))) => {
                *elems == expected_elems && *split_at == match cast_type {
                    CastType::Prefix => size,
                    CastType::Suffix => bytes_len - size,
                }
            }
            (Err(MetadataCastError::Size), None) => true,
            _ => false,
        }
    }))]
    pub(crate) const fn validate_cast_and_convert_metadata(
        &self,
        addr: usize,
        bytes_len: usize,
        cast_type: CastType,
    ) -> Result<(usize, usize), MetadataCastError> {
        #[cfg(all(kani, kani_slow))]
        #[kani::proof_for_contract(DstLayout::validate_cast_and_convert_metadata)]
        #[kani::solver(kissat)]
        fn proof() {
            let cast_type = if kani::any() { CastType::Prefix } else { CastType::Suffix };
            let _ = kani::any::<DstLayout>().validate_cast_and_convert_metadata(
                kani::any(),
                kani::any(),
                cast_type,
            );
        }

        // `debug_assert!`, but with `#[allow(clippy::arithmetic_side_effects)]`.
        macro_rules! __const_debug_assert {
            ($e:expr $(, $msg:expr)?) => {
                const_debug_assert!({
                    #[allow(clippy::arithmetic_side_effects)]
                    let e = $e;
                    e
                } $(, $msg)?);
            };
        }

        // Note that, in practice, `self` is always a compile-time constant. We
        // do this check earlier than needed to ensure that we always panic as a
        // result of bugs in the program (such as calling this function on an
        // invalid type) instead of allowing this panic to be hidden if the cast
        // would have failed anyway for runtime reasons (such as a too-small
        // memory region).
        //
        // FIXME(#67): Once our MSRV is 1.65, use let-else:
        // https://blog.rust-lang.org/2022/11/03/Rust-1.65.0.html#let-else-statements
        let size_info = match self.size_info.try_to_nonzero_elem_size() {
            Some(size_info) => size_info,
            None => const_panic!("attempted to cast to slice type with zero-sized element"),
        };

        // Precondition
        __const_debug_assert!(
            addr.checked_add(bytes_len).is_some(),
            "`addr` + `bytes_len` > usize::MAX"
        );

        // Alignment checks go in their own block to avoid introducing variables
        // into the top-level scope.
        {
            // We check alignment for `addr` (for prefix casts) or `addr +
            // bytes_len` (for suffix casts). For a prefix cast, the correctness
            // of this check is trivial - `addr` is the address the object will
            // live at.
            //
            // Under the documented completed-layout premise, all valid sizes
            // for the type are multiples of its alignment. Thus, a
            // validly-sized instance which lives at a validly-aligned address
            // must also end at a validly-aligned address. Thus, if the end
            // address for a suffix cast (`addr + bytes_len`) is not aligned,
            // then no valid start address will be aligned either.
            let offset = match cast_type {
                CastType::Prefix => 0,
                CastType::Suffix => bytes_len,
            };

            // Addition is guaranteed not to overflow because `offset <=
            // bytes_len`, and `addr + bytes_len <= usize::MAX` is a
            // precondition of this method. Modulus is guaranteed not to divide
            // by zero because `self.align` is non-zero.
            #[allow(clippy::arithmetic_side_effects)]
            if (addr + offset) % self.align.get() != 0 {
                return Err(MetadataCastError::Alignment);
            }
        }

        let (elems, self_bytes) = match size_info {
            SizeInfo::Sized { size } => {
                if size > bytes_len {
                    return Err(MetadataCastError::Size);
                }
                (0, size)
            }
            SizeInfo::SliceDst(trailing) => {
                let max_slice_bytes = match trailing.max_trailing_bytes(bytes_len) {
                    Some(bytes) => bytes,
                    None => return Err(MetadataCastError::Size),
                };
                // Calculate the number of elements that fit in
                // `max_slice_bytes`. Any remaining input bytes stay outside
                // the selected object unless the object's own rounding
                // operation consumes them as padding.
                //
                // Guaranteed not to divide by zero: `elem_size` is non-zero.
                let (elems, _) = max_elems_for_bytes(max_slice_bytes, trailing.elem_size);
                // The remainder is `max_slice_bytes - elems * elem_size`.
                // Writing it as a remainder exposes its bound to the
                // optimizer: when `elem_size <= size_align`, rounding the
                // remainder down to `size_align` always gives zero.
                let size_align = trailing.size_rounding_align_and_phase.align();
                // `size_phase + max_slice_bytes == max_rounded_bytes`, an
                // alignment multiple. The selected tail removes `unused_bytes`
                // from this boundary; rounding back up removes only whole
                // alignment units. Thus its normalized rounded size is:
                //
                //   round_up(size_phase + elems * elem_size, size_align)
                //       = max_rounded_bytes - round_down(unused_bytes, size_align)
                //
                // `prove_slice_dst_validator_selected_size_fits` checks this
                // identity and its intermediate bounds, using the relations
                // in `prove_max_trailing_bytes_components`. The capacity is
                // exact by `prove_max_trailing_bytes`; the element helper's
                // contract establishes the largest whole-element count.
                #[allow(clippy::arithmetic_side_effects)]
                let unused_bytes = max_slice_bytes % trailing.elem_size.get();
                let unused_aligned_bytes =
                    util::round_down_to_next_multiple_of_alignment(unused_bytes, size_align);
                #[allow(clippy::arithmetic_side_effects)]
                let max_rounded_bytes = util::round_down_to_next_multiple_of_alignment(
                    bytes_len - trailing.size_base,
                    size_align,
                );
                // `unused_aligned_bytes <= unused_bytes <= max_slice_bytes
                // <= max_rounded_bytes`, and `size_base + max_rounded_bytes
                // <= bytes_len`, so neither operation can overflow.
                #[allow(clippy::arithmetic_side_effects)]
                let self_bytes = trailing.size_base + (max_rounded_bytes - unused_aligned_bytes);
                (elems, self_bytes)
            }
        };

        __const_debug_assert!(self_bytes <= bytes_len);

        let split_at = match cast_type {
            CastType::Prefix => self_bytes,
            // Guaranteed not to underflow:
            // - In the `Sized` branch, only returns `size` if `size <=
            //   bytes_len`.
            // - In the `SliceDst` branch, calculates `self_bytes <=
            //   bytes_len`.
            #[allow(clippy::arithmetic_side_effects)]
            CastType::Suffix => bytes_len - self_bytes,
        };

        Ok((elems, split_at))
    }
}

/// Computes the affine destination-metadata map.
///
/// # Safety
///
/// `base + metadata * multiple`, evaluated over the mathematical nonnegative
/// integers, must not exceed `usize::MAX`.
#[inline(always)]
#[allow(unstable_name_collisions)]
#[cfg_attr(kani, kani::requires(metadata.checked_mul(multiple)
    .and_then(|scaled| base.checked_add(scaled)).is_some()))]
#[cfg_attr(kani, kani::ensures(|&result| {
    metadata.checked_mul(multiple).and_then(|scaled| base.checked_add(scaled)) == Some(result)
}))]
unsafe fn add_scaled_metadata(base: usize, metadata: usize, multiple: usize) -> usize {
    // The call-site safety argument establishes that both operations are
    // representable for valid source metadata. This harness verifies the
    // production implementation selected by Kani's modern rustc against
    // checked arithmetic under precisely that precondition. The distributive
    // natural-number argument which establishes the precondition is kept
    // beside the unsafe pointer projection where it can be reviewed with the
    // layout premises that it consumes.
    #[cfg(kani)]
    #[kani::proof_for_contract(add_scaled_metadata)]
    #[kani::solver(kissat)]
    fn proof() {
        let base: usize = kani::any();
        let metadata: usize = kani::any();
        let multiple: usize = kani::any();
        let Some(scaled) = metadata.checked_mul(multiple) else {
            kani::assume(false);
            loop {}
        };
        let Some(expected) = base.checked_add(scaled) else {
            kani::assume(false);
            loop {}
        };

        // SAFETY: The checked operations above returned `Some`, so the
        // operation is representable in `usize`. The standard-library
        // contracts say that this rules out undefined behavior for both
        // unchecked operations [1][2].
        //
        // [1] Per https://doc.rust-lang.org/1.93.1/std/primitive.usize.html#method.unchecked_mul:
        //
        //   This results in undefined behavior [..] when `checked_mul` would
        //   return `None`.
        //
        // [2] Per https://doc.rust-lang.org/1.93.1/std/primitive.usize.html#method.unchecked_add:
        //
        //   This results in undefined behavior [..] when `checked_add` would
        //   return `None`.
        assert_eq!(unsafe { add_scaled_metadata(base, metadata, multiple) }, expected);
    }

    #[allow(unused_imports)]
    use crate::util::polyfills::*;

    // SAFETY: The caller promises that `base + metadata * multiple` is
    // representable over the nonnegative integers. Thus `metadata * multiple`
    // is representable too. On toolchains with inherent unchecked arithmetic,
    // this is exactly the condition under which `usize::unchecked_mul` does
    // not have undefined behavior [1]. On older supported toolchains, method
    // resolution selects `polyfills::NumExt::unchecked_mul`, whose safety
    // contract requires the same no-overflow condition. Kani checks the two
    // implementations in separate harnesses.
    //
    // [1] Per https://doc.rust-lang.org/1.93.1/std/primitive.usize.html#method.unchecked_mul:
    //
    //   This results in undefined behavior when `self * rhs > usize::MAX` or
    //   `self * rhs < usize::MIN`, i.e. when `checked_mul` would return `None`.
    //
    let scaled = unsafe { metadata.unchecked_mul(multiple) };

    // SAFETY: The caller promises that `base + metadata * multiple` is
    // representable in `usize`, and `scaled == metadata * multiple`. On
    // toolchains with inherent unchecked arithmetic, that is exactly the
    // condition under which `usize::unchecked_add` does not have undefined
    // behavior [1]. On older supported toolchains, method resolution selects
    // `polyfills::NumExt::unchecked_add`, whose safety contract requires the
    // same no-overflow condition. Kani checks the two implementations in
    // separate harnesses.
    //
    // [1] Per https://doc.rust-lang.org/1.93.1/std/primitive.usize.html#method.unchecked_add:
    //
    //   This results in undefined behavior when `self + rhs > usize::MAX` or
    //   `self + rhs < usize::MIN`, i.e. when `checked_add` would return `None`.
    unsafe { base.unchecked_add(scaled) }
}

pub(crate) use cast_from::CastFrom;
mod cast_from {
    use crate::*;

    pub(crate) struct CastFrom<Dst: ?Sized> {
        _never: core::convert::Infallible,
        _marker: PhantomData<Dst>,
    }

    // SAFETY: The implementation of `Project::project` preserves the address
    // of the referent – it only modifies pointer metadata.
    unsafe impl<Src, Dst> crate::pointer::cast::Cast<Src, Dst> for CastFrom<Dst>
    where
        Src: KnownLayout + ?Sized,
        Dst: KnownLayout + ?Sized,
    {
    }

    // SAFETY: The implementation of `Project::project` preserves the size of
    // the referent (see inline comments for a more detailed proof of this).
    unsafe impl<Src, Dst> crate::pointer::cast::CastExact<Src, Dst> for CastFrom<Dst>
    where
        Src: KnownLayout + ?Sized,
        Dst: KnownLayout + ?Sized,
    {
    }

    // SAFETY: `project` produces a pointer which refers to the same referent
    // bytes as its input, or to a subset of them (see inline comments for a
    // more detailed proof of this). It does this using provenance-preserving
    // operations.
    unsafe impl<Src, Dst> crate::pointer::cast::Project<Src, Dst> for CastFrom<Dst>
    where
        Src: KnownLayout + ?Sized,
        Dst: KnownLayout + ?Sized,
    {
        /// # PME
        ///
        /// Generates a post-monomorphization error if it is not possible to
        /// implement soundly.
        //
        // FIXME(#1817): Support Sized->Unsized and Unsized->Sized casts
        fn project(src: PtrInner<'_, Src>) -> *mut Dst {
            /// The parameters required in order to perform a pointer cast from
            /// `Src` to `Dst`.
            ///
            /// These are a compile-time function of the layouts of `Src`
            /// and `Dst`.
            ///
            /// # Safety
            ///
            /// `Src`'s alignment must not be smaller than `Dst`'s alignment.
            struct CastParams<Src: ?Sized, Dst: ?Sized> {
                inner: CastParamsInner,
                _src: PhantomData<Src>,
                _dst: PhantomData<Dst>,
            }

            #[derive(Copy, Clone)]
            enum CastParamsInner {
                // At compile time (specifically, post-monomorphization time),
                // we need to compute two things:
                // - Whether, given *any* `*Src`, it is possible to construct a
                //   `*Dst` which addresses the same number of bytes (ie,
                //   whether, for any `Src` pointer metadata, there exists `Dst`
                //   pointer metadata that addresses the same number of bytes)
                // - If this is possible, any information necessary to perform
                //   the `Src`->`Dst` metadata conversion at runtime.
                //
                // For slice DSTs, destination metadata is an affine function
                // of source metadata:
                //
                //   dst_meta = offset_delta_elems + src_meta * elem_multiple
                //
                // `elem_multiple` scales the destination's trailing element
                // size to the source's. `offset_delta_elems` selects a
                // destination size equal to the source's zero-element size.
                // We then prove that the two complete rounded size formulas
                // produce the same sequence for all metadata. This is
                // necessary because a packed outer DST can retain a nested
                // field's rounding operation; comparing only physical
                // trailing-slice offsets is not sufficient.
                /// The parameters required in order to perform an
                /// unsized-to-unsized pointer cast from `Src` to `Dst` as
                /// described above.
                ///
                /// # Safety
                ///
                /// `Src` and `Dst` must both be slice DSTs.
                ///
                /// `offset_delta_elems` and `elem_multiple` must be valid as
                /// described above.
                UnsizedToUnsized { offset_delta_elems: usize, elem_multiple: usize },

                /// The metadata of a `Dst` which has the same size as `Src:
                /// Sized`.
                ///
                /// # Safety
                ///
                /// `Src: Sized` and `Dst` must be a slice DST.
                ///
                /// A raw `Dst` pointer with metadata `dst_meta` must address
                /// `size_of::<Src>()` bytes.
                SizedToUnsized { dst_meta: usize },

                /// The metadata of a `Dst` which has the same size as `Src:
                /// Sized`.
                ///
                /// # Safety
                ///
                /// `Src` and `Dst` must both be `Sized` and `size_of::<Src>()
                /// == size_of::<Dst>()`.
                SizedToSized,
            }

            impl<Src: ?Sized, Dst: ?Sized> Copy for CastParams<Src, Dst> {}
            impl<Src: ?Sized, Dst: ?Sized> Clone for CastParams<Src, Dst> {
                fn clone(&self) -> Self {
                    *self
                }
            }

            impl<Src: ?Sized, Dst: ?Sized> CastParams<Src, Dst> {
                /// Given a nonzero `dst.elem_size` that exactly divides
                /// `src.elem_size`, produces `true` only when the following
                /// metadata map preserves the complete object size for every
                /// source element count `src_meta`:
                ///
                /// ```text
                /// dst_meta = dst_base + src_meta * (src.elem_size / dst.elem_size)
                /// ```
                ///
                /// This recognizes sufficient conditions for equality. A
                /// `false` result also includes equivalent size sequences
                /// that this test cannot recognize, or an adjustment whose
                /// normalized components cannot be represented in `usize`.
                const fn size_sequences_match(
                    src: TrailingSliceLayout,
                    dst: TrailingSliceLayout,
                    dst_base: usize,
                ) -> bool {
                    let base_bytes = match dst_base.checked_mul(dst.elem_size) {
                        Some(bytes) => bytes,
                        None => return false,
                    };
                    let shifted_dst = match dst.advance(base_bytes, src.elem_size) {
                        Some(layout) => layout,
                        None => return false,
                    };
                    src.has_same_size_sequence(shifted_dst)
                }

                /// Given the complete layouts of `Src` and `Dst`, produces
                /// parameters for a cast that preserves object size.
                ///
                /// A `Some` result establishes that `Src`'s alignment is at
                /// least `Dst`'s and that the metadata map preserves size for
                /// every valid source metadata value.
                ///
                /// Supports casts between sized types of equal size, from a
                /// sized type to a slice DST, and between slice DSTs. A slice
                /// DST destination must have nonzero-sized elements; between
                /// slice DSTs, its element size must exactly divide the
                /// source's nonzero element size.
                ///
                /// Produces `None` for unsupported casts or when this method
                /// cannot establish a size-preserving metadata map. Rejection
                /// does not establish that no such map exists.
                const fn try_compute(
                    src_layout: &DstLayout,
                    dst_layout: &DstLayout,
                ) -> Option<CastParams<Src, Dst>> {
                    if src_layout.align.get() < dst_layout.align.get() {
                        return None;
                    }

                    let inner = match (src_layout.size_info, dst_layout.size_info) {
                        (
                            SizeInfo::Sized { size: src_size },
                            SizeInfo::Sized { size: dst_size },
                        ) => {
                            if src_size != dst_size {
                                return None;
                            }

                            // SAFETY: We checked above that `src_size ==
                            // dst_size`.
                            CastParamsInner::SizedToSized
                        }
                        (SizeInfo::Sized { size: src_size }, SizeInfo::SliceDst(_)) => {
                            let dst_meta = match dst_layout.metadata_for_exact_size(src_size) {
                                Some(meta) => meta,
                                None => return None,
                            };

                            // SAFETY: The preceding math ensures that a `Dst`
                            // with `dst_meta` addresses `src_size` bytes.
                            CastParamsInner::SizedToUnsized { dst_meta }
                        }
                        (SizeInfo::SliceDst(src), SizeInfo::SliceDst(dst)) => {
                            let dst_elem_size = if let Some(e) = NonZeroUsize::new(dst.elem_size) {
                                e
                            } else {
                                return None;
                            };

                            if src.elem_size < dst.elem_size {
                                return None;
                            }

                            let (elem_multiple, described_src_elem_size) =
                                super::max_elems_for_bytes(src.elem_size, dst_elem_size);
                            if described_src_elem_size != src.elem_size {
                                return None;
                            }

                            // Prefer an exact destination-element adjustment
                            // between `src.size_offset()` and
                            // `dst.size_offset()`. This preserves offset-based
                            // cast compatibility and metadata selection when
                            // several lengths have the same rounded size. It
                            // also avoids choosing a length whose subsequent
                            // padding sequence differs from the source's.
                            let exact_offset_delta_elems =
                                match src.size_offset().checked_sub(dst.size_offset()) {
                                    Some(delta) => {
                                        let (elems, described_delta) =
                                            super::max_elems_for_bytes(delta, dst_elem_size);
                                        if described_delta == delta {
                                            Some(elems)
                                        } else {
                                            None
                                        }
                                    }
                                    None => None,
                                };

                            let offset_delta_elems = match exact_offset_delta_elems {
                                Some(elems) if Self::size_sequences_match(src, dst, elems) => elems,
                                _ => {
                                    let src_zero_size = match src.size_for_elems(0) {
                                        Some(size) => size,
                                        None => return None,
                                    };
                                    let elems =
                                        match dst_layout.metadata_for_exact_size(src_zero_size) {
                                            Some(elems) => elems,
                                            None => return None,
                                        };
                                    if !Self::size_sequences_match(src, dst, elems) {
                                        return None;
                                    }
                                    elems
                                }
                            };

                            CastParamsInner::UnsizedToUnsized {
                                // SAFETY: `size_sequences_match` proves that
                                // this affine metadata map preserves size.
                                offset_delta_elems,
                                // SAFETY: We checked above that this is an exact
                                // ratio of source to destination element size.
                                elem_multiple,
                            }
                        }
                        _ => return None,
                    };

                    // SAFETY: We checked above that `src.align >= dst.align`.
                    Some(CastParams { inner, _src: PhantomData, _dst: PhantomData })
                }
            }

            impl<Src: KnownLayout + ?Sized, Dst: KnownLayout + ?Sized> CastParams<Src, Dst> {
                /// Produces `Dst` metadata describing an object of the same
                /// size as the `Src` described by `src_meta`.
                ///
                /// # Safety
                ///
                /// `src_meta` describes a `Src` whose size is no larger than
                /// `isize::MAX`.
                #[inline(always)]
                unsafe fn cast_metadata(
                    self,
                    src_meta: Src::PointerMetadata,
                ) -> Dst::PointerMetadata {
                    let dst_meta = match self.inner {
                        CastParamsInner::UnsizedToUnsized { offset_delta_elems, elem_multiple } => {
                            let src_meta = src_meta.to_elem_count();
                            // SAFETY: `self` witnesses that this affine map
                            // makes `Src` and `Dst`'s complete rounded size
                            // formulas equal for every source metadata value.
                            // Interpret the arithmetic in this proof over the
                            // mathematical nonnegative integers. Since the
                            // caller promises that `src_meta` is valid `Src`
                            // metadata, the source object size `src_size` it
                            // describes is at most `isize::MAX`. Let
                            // `src_elem_size` and `dst_elem_size` be the
                            // respective trailing-slice element sizes. Then:
                            //
                            //   src_meta * src_elem_size
                            //       <= src_size <= isize::MAX.
                            //
                            // Since `elem_multiple` is the exact ratio
                            // `src_elem_size / dst_elem_size` and
                            // `dst_elem_size >= 1`,
                            //
                            //   src_meta * elem_multiple
                            //       <= src_meta * src_elem_size
                            //       <= isize::MAX <= usize::MAX.
                            //
                            // Thus `scaled_elems = src_meta * elem_multiple`
                            // is representable, and its byte contribution is
                            // `scaled_elems * dst_elem_size = src_meta *
                            // src_elem_size`. Let `base_bytes =
                            // offset_delta_elems * dst_elem_size`.
                            // `size_sequences_match` compares the source
                            // formula with the destination formula advanced by
                            // `base_bytes`. The latter's unrounded term
                            // contains `base_bytes + src_meta * src_elem_size`,
                            // and its complete size is at least that large
                            // (formally checked by Kani in
                            // `prove_size_formula_bounds_size_offset`).
                            // Sequence equality therefore gives, over the
                            // mathematical nonnegative integers,
                            //
                            //   base_bytes + src_meta * src_elem_size
                            //       = (offset_delta_elems + scaled_elems)
                            //           * dst_elem_size
                            //       <= src_size.
                            //
                            // Since `dst_elem_size >= 1`, the metadata sum
                            // `offset_delta_elems + scaled_elems` is also at
                            // most `src_size`, and thus at most `isize::MAX <=
                            // usize::MAX`. The sum is therefore representable,
                            // and the returned metadata describes a `Dst` of
                            // the same size.
                            // SAFETY: The bounds above establish the helper's
                            // complete no-overflow precondition.
                            unsafe {
                                super::add_scaled_metadata(
                                    offset_delta_elems,
                                    src_meta,
                                    elem_multiple,
                                )
                            }
                        }
                        CastParamsInner::SizedToUnsized { dst_meta } => dst_meta,
                        CastParamsInner::SizedToSized => 0,
                    };
                    Dst::PointerMetadata::from_elem_count(dst_meta)
                }
            }

            trait Params<Src: ?Sized> {
                const CAST_PARAMS: CastParams<Src, Self>;
            }

            impl<Src, Dst> Params<Src> for Dst
            where
                Src: KnownLayout + ?Sized,
                Dst: KnownLayout + ?Sized,
            {
                const CAST_PARAMS: CastParams<Src, Dst> =
                    match CastParams::try_compute(&Src::LAYOUT, &Dst::LAYOUT) {
                        Some(params) => params,
                        None => const_panic!(
                            "cannot `transmute_ref!` or `transmute_mut!` between incompatible types"
                        ),
                    };
            }

            let src_meta = <Src as KnownLayout>::pointer_to_metadata(src.as_ptr());
            let params = <Dst as Params<Src>>::CAST_PARAMS;

            // SAFETY: `src: PtrInner` guarantees that `src`'s referent is zero
            // bytes or lives in a single allocation, which means that it is no
            // larger than `isize::MAX` bytes [1].
            //
            // [1] https://doc.rust-lang.org/1.92.0/std/ptr/index.html#allocation
            let dst_meta = unsafe { params.cast_metadata(src_meta) };

            <Dst as KnownLayout>::raw_from_ptr_len(src.as_non_null().cast(), dst_meta).as_ptr()
        }
    }
}

#[cfg(any(test, all(kani, feature = "derive")))]
mod padding_testutil {
    use crate::KnownLayout;

    #[derive(KnownLayout)]
    #[repr(C, align(4))]
    pub(super) struct Aligned<Prefix, Tail: ?Sized> {
        pub(super) prefix: Prefix,
        pub(super) tail: Tail,
    }

    /// Compare direct padding with the complete size minus the slice end.
    /// Counts whose complete object size exceeds `usize::MAX` are omitted.
    pub(super) fn check_padding_for_elems<Target>(elems: usize) -> Option<usize>
    where
        Target: ?Sized + KnownLayout<PointerMetadata = usize>,
    {
        let layout = crate::trailing_slice_layout::<Target>();
        let trailing_size = elems.checked_mul(layout.elem_size)?;

        // Evaluate the complete rounded size independently of both helpers:
        // `size_for_elems` delegates to the padding method being tested.
        let (alignment, phase) = layout.size_rounding_align_and_phase.components();
        #[allow(clippy::arithmetic_side_effects)]
        let mask = alignment.get() - 1;
        let unrounded = phase.checked_add(trailing_size)?;
        let rounded = unrounded.checked_add(mask)? & !mask;
        let object_size = layout.size_base.checked_add(rounded)?;
        let trailing_end = layout.offset.checked_add(trailing_size).unwrap();
        let expected = object_size.checked_sub(trailing_end).unwrap();

        let padding = layout.padding_for_elems(elems);
        assert_eq!(padding, expected);
        Some(padding)
    }

    pub(super) fn check_layouts(elems: usize) {
        // Slices have no trailing padding. The aligned fixtures cover
        // zero and nonzero phases, a three-byte element whose padding
        // cycles with the element count, and zero-sized elements.
        let _ = check_padding_for_elems::<[u8]>(elems);
        let _ = check_padding_for_elems::<[()]>(elems);
        let _ = check_padding_for_elems::<Aligned<[u8; 8], [u8]>>(elems);
        let _ = check_padding_for_elems::<Aligned<[u8; 9], [u8]>>(elems);
        let _ = check_padding_for_elems::<Aligned<[u8; 9], [[u8; 3]]>>(elems);
        let _ = check_padding_for_elems::<Aligned<[u8; 9], [()]>>(elems);
    }
}

// FIXME(#67): For some reason, on our MSRV toolchain, this `allow` isn't
// enforced despite having `#![allow(unknown_lints)]` at the crate root, but
// putting it here works. Once our MSRV is high enough that this bug has been
// fixed, remove this `allow`.
#[allow(unknown_lints)]
#[cfg(test)]
mod tests {
    use super::*;
    use crate::PointerMetadata as _;

    /// Models the size of nested `repr(C)` structs ending in a slice.
    ///
    /// `leading` lists structs from outermost to innermost. Each tuple gives
    /// the packing factor, minimum alignment, and prefix size before padding
    /// for the final field. `trailing_elem_size` and `trailing_alignment` give
    /// the slice element size and alignment. An empty `leading` describes the
    /// slice itself.
    ///
    /// The returned closure maps a slice length to the complete size, including
    /// padding at every nesting level. It returns `None` only on `usize`
    /// overflow; sizes above `isize::MAX` are permitted by this arithmetic model.
    /// Packing affects field placement, preserving each field's internal
    /// padding, even for descriptors that Rust would reject as types.
    ///
    /// # Panics
    ///
    /// Panics if any alignment or packing factor is not a power of two, a
    /// minimum alignment exceeds its packing factor, or the element size is
    /// not a multiple of the slice alignment.
    fn size_for_metadata_model(
        leading: &[(NonZeroUsize, NonZeroUsize, usize)],
        trailing_elem_size: usize,
        trailing_alignment: NonZeroUsize,
    ) -> impl Fn(usize) -> Option<usize> + '_ {
        // Rust requires power-of-two alignments and sizes divisible by their
        // alignment [1]; alignment modifiers also take powers of two [2].
        //
        // [1] https://doc.rust-lang.org/1.93.0/reference/type-layout.html#size-and-alignment
        // [2] https://doc.rust-lang.org/1.93.0/reference/type-layout.html#the-alignment-modifiers
        assert!(trailing_alignment.get().is_power_of_two());
        assert_eq!(trailing_elem_size % trailing_alignment, 0);
        for &(packed, align, _) in leading {
            assert!(packed.get().is_power_of_two());
            assert!(align.get().is_power_of_two());
            // The descriptor requires its minimum to fit under the packing
            // cap. It also models combinations that Rust forbids, including
            // simultaneous `align` and `packed` modifiers [2].
            assert!(align <= packed);
        }

        move |elems| {
            // A slice shares its array section's layout [3]. Array size is
            // element size times length, with the element's alignment [4].
            //
            // [3] https://doc.rust-lang.org/1.93.0/reference/type-layout.html#slice-layout
            // [4] https://doc.rust-lang.org/1.93.0/reference/type-layout.html#array-layout
            let mut size = trailing_elem_size.checked_mul(elems)?;
            let mut alignment = trailing_alignment;

            // Work outward with each field's complete size, including its
            // trailing padding: an enclosing representation "does not change
            // the layout of the fields themselves" [5]. Thus packing a nested
            // field preserves the padding already included in `size`.
            //
            // [5] https://doc.rust-lang.org/1.93.0/reference/type-layout.html#representations
            for &(packed, align, prefix) in leading.iter().rev() {
                // Packing caps the field's alignment for placement [2].
                let field_align = alignment.min(packed);

                // `repr(C)` places the next field at an aligned offset [6];
                // packing requires the least sufficient inter-field padding
                // [2]. Here `prefix` is the end of the preceding fields.
                //
                // [6] https://doc.rust-lang.org/1.93.0/reference/type-layout.html#reprc-structs
                let offset = prefix.checked_add(util::padding_needed_for(prefix, field_align))?;

                // `repr(C)` uses the greatest field alignment [6], and an
                // explicit alignment can only raise it [2]. Here `align`
                // summarizes the minimum from the prefix and any explicit
                // alignment. Both operands are <= `packed`, so their maximum
                // also respects the packing cap.
                alignment = align.max(field_align);

                // Advance past the complete field, then round to the struct's
                // alignment, following `repr(C)`'s final sizing steps [6].
                size = offset.checked_add(size)?;
                size = size.checked_add(util::padding_needed_for(size, alignment))?;
            }
            // Each enclosing size is at least its field's size. Thus any
            // checked multiplication or addition that overflowed above would
            // also make the final size unrepresentable in `usize`.
            Some(size)
        }
    }

    /// Computes [`size_for_metadata_model`]'s size function using `DstLayout`.
    ///
    /// Layout construction uses [`DstLayout::for_repr_c_struct`], and the
    /// returned closure uses [`crate::PointerMetadata::size_for_metadata`].
    ///
    /// # Panics
    ///
    /// Panics for invalid descriptors as in [`size_for_metadata_model`], or if
    /// `DstLayout` cannot construct the layout. This includes static size
    /// overflow and, in debug builds, alignments or packing factors above
    /// [`DstLayout::CURRENT_MAX_ALIGN`].
    fn size_for_metadata_via_dst_layout(
        leading: &[(NonZeroUsize, NonZeroUsize, usize)],
        trailing_elem_size: usize,
        trailing_alignment: NonZeroUsize,
    ) -> impl Fn(usize) -> Option<usize> + '_ {
        assert_eq!(trailing_elem_size % trailing_alignment, 0);
        let mut layout = DstLayout {
            align: trailing_alignment,
            size_info: SizeInfo::SliceDst(TrailingSliceLayout {
                offset: 0,
                elem_size: trailing_elem_size,
                size_base: 0,
                size_rounding_align_and_phase: RoundingAlignAndPhase::new(trailing_alignment, 0),
            }),
            statically_shallow_unpadded: true,
        };
        for &(packed, align, prefix) in leading.iter().rev() {
            assert!(align <= packed);
            let prefix = DstLayout {
                align: DstLayout::MIN_ALIGN,
                size_info: SizeInfo::Sized { size: prefix },
                statically_shallow_unpadded: true,
            };
            layout = DstLayout::for_repr_c_struct(Some(align), Some(packed), &[prefix, layout]);
        }
        move |elems: usize| elems.size_for_metadata(layout)
    }

    #[test]
    fn test_model_via_dst_layout() {
        let nz = |n| NonZeroUsize::new(n).unwrap();
        let cases: &[&[_]] = &[
            &[],
            &[(nz(8), nz(1), 3)],
            &[(nz(8), nz(8), 3)],
            &[(nz(2), nz(1), 1), (nz(8), nz(8), 3)],
            &[(nz(8), nz(8), 1), (nz(2), nz(1), 3), (nz(8), nz(8), 5)],
            &[(nz(1), nz(1), 1), (nz(2), nz(2), 3), (nz(8), nz(8), 5)],
        ];
        for &leading in cases {
            for &(elem_size, alignment) in &[(0, 1), (0, 8), (1, 1), (2, 2), (3, 1), (8, 8)] {
                let alignment = nz(alignment);
                let expected = size_for_metadata_model(leading, elem_size, alignment);
                let actual = size_for_metadata_via_dst_layout(leading, elem_size, alignment);
                let max_elems = usize::MAX / elem_size.max(1);
                for elems in (0..32).chain([
                    max_elems - 1,
                    max_elems,
                    max_elems.saturating_add(1),
                    usize::MAX,
                ]) {
                    assert_eq!(
                        actual(elems),
                        expected(elems),
                        "leading: {:?}, trailing: {:?}, elems: {}",
                        leading,
                        (elem_size, alignment),
                        elems,
                    );
                }
            }
        }

        for prefix in [usize::MAX - 1, usize::MAX] {
            let leading = [(nz(1), nz(1), prefix)];
            let expected = size_for_metadata_model(&leading, 1, nz(1));
            let actual = size_for_metadata_via_dst_layout(&leading, 1, nz(1));
            for elems in [0, 1, 2] {
                assert_eq!(actual(elems), expected(elems));
            }
        }
    }

    // This exercises arithmetic rather than unsafe operations; keep the
    // smaller comparison test enabled under Miri instead of this large search.
    #[test]
    #[cfg_attr(miri, ignore)]
    fn test_model_via_dst_layout_combinations() {
        use rand::{rngs::SmallRng, Rng as _, SeedableRng as _};

        fn check(
            leading: &[(NonZeroUsize, NonZeroUsize, usize)],
            trailing_elem_size: usize,
            trailing_alignment: NonZeroUsize,
        ) {
            let expected = size_for_metadata_model(leading, trailing_elem_size, trailing_alignment);
            assert!(
                expected(0).is_some(),
                "unrepresentable test layout: {:?}, {:?}",
                leading,
                (trailing_elem_size, trailing_alignment)
            );
            let actual = std::panic::catch_unwind(|| {
                size_for_metadata_via_dst_layout(leading, trailing_elem_size, trailing_alignment)
            })
            .unwrap_or_else(|_| {
                panic!(
                    "DstLayout rejected leading: {:?}, trailing: {:?}",
                    leading,
                    (trailing_elem_size, trailing_alignment)
                )
            });
            let compare = |elems| {
                let expected = expected(elems);
                assert_eq!(
                    actual(elems),
                    expected,
                    "leading: {:?}, trailing: {:?}, elems: {}",
                    leading,
                    (trailing_elem_size, trailing_alignment),
                    elems,
                );
                expected
            };

            let first_sixteen = 0..=16;
            let pointer_width = (0..POINTER_WIDTH_BITS).step_by(4).flat_map(|shift| {
                let elems = 1usize << shift;
                [elems - 1, elems, elems + 1]
            });
            let last_two = (usize::MAX - 1)..=usize::MAX;
            let elemses = first_sixteen.chain(pointer_width).chain(last_two);
            for elems in elemses {
                let _ = compare(elems);
            }

            // The modeled size is nondecreasing. Find the last fitting length,
            // comparing every probe, then check both sides of that boundary.
            // Zero-sized elements can remain representable at usize::MAX.
            let (mut low, mut high) = (0, usize::MAX);
            while low < high {
                let distance = high - low;
                let mid = low + distance / 2 + distance % 2;
                if compare(mid).is_some() {
                    low = mid;
                } else {
                    high = mid - 1;
                }
            }
            assert!(compare(low).is_some());
            if let Some(previous) = low.checked_sub(1) {
                assert!(compare(previous).is_some());
            }
            if let Some(next) = low.checked_add(1) {
                assert_eq!(compare(next), None);
            }
        }

        let nz = |n| NonZeroUsize::new(n).unwrap();
        let check_small_tails = |leading: &[_]| {
            for alignment in [1, 2, 4, 8] {
                for multiple in [0, 1, 2, 3, 7] {
                    check(leading, multiple * alignment, nz(alignment));
                }
            }
        };
        let mut fragments = Vec::new();
        for packed in [1, 2, 4] {
            for align in [1, 2, 4].iter().copied().filter(|&align| align <= packed) {
                for prefix in 0..=4 {
                    fragments.push((nz(packed), nz(align), prefix));
                }
            }
        }

        // Exhaust every combination in this small domain at depths 0, 1, 2.
        check_small_tails(&[]);
        for &outer in &fragments {
            check_small_tails(&[outer]);
            for &inner in &fragments {
                check_small_tails(&[outer, inner]);
            }
        }

        let mut rng = SmallRng::seed_from_u64(0);
        for depth in (0usize..=128).chain([256, 512, 1024]) {
            // A small prefix adds at most 4 * max_align bytes per layer.
            // Reserve half of usize::MAX for a large outermost prefix so
            // construction remains representable even at the largest depth.
            let align_limit =
                DstLayout::CURRENT_MAX_ALIGN.get().min(usize::MAX / (8 * depth.max(1)));
            let alignments: Vec<_> = (0..POINTER_WIDTH_BITS)
                .map(|shift| 1usize << shift)
                .take_while(|&align| align <= align_limit)
                .collect();
            let last = alignments.len() - 1;
            let max_align = alignments[last];
            for case in 0..40 {
                let mut leading = Vec::with_capacity(depth);
                for level in 0..depth {
                    // Mix arbitrary modifiers, increasing and decreasing
                    // alignments, alternating packing, and fully packed trees.
                    let (packed, align) = match case / 8 {
                        0 => {
                            let packed = rng.gen_range(0..=last);
                            (packed, rng.gen_range(0..=packed))
                        }
                        1 => (last, level % alignments.len()),
                        2 => (last, last - level % alignments.len()),
                        3 => {
                            let index = if level % 2 == 0 { 0 } else { last };
                            (index, index)
                        }
                        _ => (0, 0),
                    };
                    let (packed, align) = (alignments[packed], alignments[align]);
                    let prefix = if level == 0 && case % 4 == 3 {
                        usize::MAX / 2 + rng.gen_range(0..=2) - 1
                    } else {
                        match rng.gen_range(0..8) {
                            0 => 0,
                            1 => 1,
                            2 => align - 1,
                            3 => align,
                            4 => align + 1,
                            5 => packed - 1,
                            6 => packed + 1,
                            _ => rng.gen_range(0..=2 * max_align),
                        }
                    };
                    leading.push((nz(packed), nz(align), prefix));
                }
                let alignment = alignments[rng.gen_range(0..=last)];
                let elem_size = match case % 8 {
                    0 => 0,
                    1 => alignment,
                    2 => 2 * alignment,
                    3 => 3 * alignment,
                    4 => 7 * alignment,
                    5 => 15 * alignment,
                    6 => (usize::MAX / 2 / alignment) * alignment,
                    _ => (usize::MAX / alignment) * alignment,
                };
                check(&leading, elem_size, nz(alignment));
            }
        }
    }

    const TEST_SIZE_ROUNDING_ALIGN_AND_PHASE_ALIGN: NonZeroUsize = match NonZeroUsize::new(8) {
        Some(align) => align,
        None => const_unreachable!(),
    };
    const TEST_SIZE_ROUNDING_ALIGN_AND_PHASE: RoundingAlignAndPhase =
        RoundingAlignAndPhase::new(TEST_SIZE_ROUNDING_ALIGN_AND_PHASE_ALIGN, 3);
    const TEST_SIZE_ROUNDING_ALIGN_AND_PHASE_DECODED_ALIGN: usize =
        TEST_SIZE_ROUNDING_ALIGN_AND_PHASE.align().get();
    const TEST_SIZE_ROUNDING_ALIGN_AND_PHASE_DECODED_PHASE: usize =
        TEST_SIZE_ROUNDING_ALIGN_AND_PHASE.components().1;

    #[test]
    #[allow(clippy::arithmetic_side_effects)]
    fn test_size_rounding_align_and_phase_encoding() {
        // These constants ensure that encoding and decoding remain evaluable
        // on every supported compiler, including the MSRV.
        assert_eq!(TEST_SIZE_ROUNDING_ALIGN_AND_PHASE_DECODED_ALIGN, 8);
        assert_eq!(TEST_SIZE_ROUNDING_ALIGN_AND_PHASE_DECODED_PHASE, 3);

        for shift in 0..POINTER_WIDTH_BITS {
            #[allow(clippy::arithmetic_side_effects)]
            let align = NonZeroUsize::new(1usize << shift).unwrap();
            for phase in [0, align.get() / 2, align.get() - 1] {
                let rounding = RoundingAlignAndPhase::new(align, phase);
                assert_eq!(rounding.align(), align);
                assert_eq!(rounding.components().1, phase);
                assert_eq!(rounding.0.get(), align.get() | phase);
            }
        }

        let max_align = DstLayout::THEORETICAL_MAX_ALIGN;
        let max_phase = max_align.get() - 1;
        let max = RoundingAlignAndPhase::new(max_align, max_phase);
        assert_eq!(max.0.get(), usize::MAX);
        assert_eq!(max.align(), max_align);
        assert_eq!(max.components().1, max_phase);

        assert_eq!(mem::size_of::<TrailingSliceLayout>(), 4 * mem::size_of::<usize>());
    }

    #[test]
    #[allow(clippy::arithmetic_side_effects)]
    fn test_trailing_slice_layout_normal_form() {
        // Compare the stored base-and-phase formula with
        // `prefix + round_up(offset + elems * elem_size, align)`, where
        // `prefix < align`. Move whole alignment-sized chunks of `offset`
        // into `size_base` and retain the remainder as the phase. Also verify
        // that `size_offset()` recovers the exact `offset` used by casts to
        // choose between metadata values with the same rounded object size.
        for align in [1, 2, 4, 8, 16] {
            let align = NonZeroUsize::new(align).unwrap();
            for prefix in 0..align.get() {
                for offset in 0..32 {
                    let phase = offset % align;
                    let size_base = (offset - phase) | prefix;
                    let layout = TrailingSliceLayout {
                        offset: 0,
                        elem_size: 3,
                        size_base,
                        size_rounding_align_and_phase: RoundingAlignAndPhase::new(align, phase),
                    };
                    assert_eq!(layout.size_offset(), offset);

                    for elems in 0..32 {
                        let unrounded = offset + elems * layout.elem_size;
                        let old_size =
                            prefix + unrounded + util::padding_needed_for(unrounded, align);
                        assert_eq!(layout.size_for_elems(elems), Some(old_size));
                    }
                }
            }
        }
    }

    #[test]
    #[allow(clippy::arithmetic_side_effects)]
    fn test_normalized_size_bounds() {
        // Use checked evaluation as an independent oracle, including layouts
        // whose empty object is already too large to represent.
        let checked_size = |layout: TrailingSliceLayout, trailing_bytes: usize| {
            let (alignment, phase) = layout.size_rounding_align_and_phase.components();
            let unrounded = phase.checked_add(trailing_bytes)?;
            let rounded = unrounded.checked_add(util::padding_needed_for(unrounded, alignment))?;
            layout.size_base.checked_add(rounded)
        };
        for exponent in 0..POINTER_WIDTH_BITS {
            let alignment = NonZeroUsize::new(1usize << exponent).unwrap();
            for phase in [0, alignment.get() / 2, alignment.get() - 1] {
                for base in [0, 1, alignment.get() - 1, alignment.get(), usize::MAX - 1, usize::MAX]
                {
                    let mut layout = TrailingSliceLayout {
                        offset: 0,
                        elem_size: 1,
                        size_base: base,
                        size_rounding_align_and_phase: RoundingAlignAndPhase::new(alignment, phase),
                    };
                    for available in [0, 1, alignment.get() - 1, alignment.get(), usize::MAX] {
                        match layout.max_trailing_bytes(available) {
                            Some(max_bytes) => {
                                assert!(checked_size(layout, max_bytes).unwrap() <= available);
                                if let Some(next_bytes) = max_bytes.checked_add(1) {
                                    assert!(checked_size(layout, next_bytes)
                                        .map(|size| size > available)
                                        .unwrap_or(true));
                                }
                            }
                            None => {
                                assert!(checked_size(layout, 0)
                                    .map(|size| size > available)
                                    .unwrap_or(true));
                            }
                        }
                    }

                    for elem_size in [0, 1, 3, alignment.get(), usize::MAX] {
                        layout.elem_size = elem_size;
                        let max_elems = layout
                            .max_trailing_bytes(usize::MAX)
                            .and_then(|bytes| bytes.checked_div(elem_size))
                            .unwrap_or(usize::MAX);
                        for elems in [
                            0,
                            1,
                            max_elems.saturating_sub(1),
                            max_elems,
                            max_elems.saturating_add(1),
                            usize::MAX,
                        ] {
                            let expected = elems
                                .checked_mul(elem_size)
                                .and_then(|bytes| checked_size(layout, bytes));
                            // The physical slice offset does not affect
                            // the size formula. Raw layouts may place the
                            // slice beyond the object, and adding its byte
                            // count to the offset may overflow.
                            for offset in [0, usize::MAX] {
                                layout.offset = offset;
                                assert_eq!(layout.size_for_elems(elems), expected);
                            }
                        }
                    }
                }
            }
        }
    }

    #[test]
    fn test_padding_for_elems_for_rust_layouts() {
        use super::padding_testutil::{check_layouts, check_padding_for_elems, Aligned};

        for elems in 0..256 {
            check_layouts(elems);
        }
        let size_limit = usize::MAX;
        for remaining in 0..16 {
            check_layouts(size_limit - remaining);
            check_layouts(size_limit / 3 - remaining);
        }
        check_layouts(usize::MAX);

        for (elems, expected) in [(0, 3), (1, 0), (2, 1), (3, 2)] {
            assert_eq!(
                check_padding_for_elems::<Aligned<[u8; 9], [[u8; 3]]>>(elems),
                Some(expected)
            );
        }
        // Every count is valid for zero-sized trailing elements.
        assert_eq!(check_padding_for_elems::<Aligned<[u8; 9], [()]>>(usize::MAX), Some(3));
    }

    #[test]
    fn test_padding_for_elems() {
        for exponent in 0..POINTER_WIDTH_BITS {
            let alignment = NonZeroUsize::new(1usize << exponent).unwrap();
            let mask = alignment.get() - 1;
            for phase in [0, alignment.get() / 2, mask] {
                // Include zero-sized elements, a negative fixed term, fixed
                // padding larger than the alignment, and overflowing sums
                // and products. Raw layouts need not describe Rust types.
                for (offset, elem_size, size_base) in [
                    (0, 0, 0),
                    (0, 1, 1),
                    (9, 3, 8),
                    (3, 3, 0),
                    (9, 3, 12),
                    (usize::MAX, usize::MAX, usize::MAX),
                    (usize::MAX, 0, usize::MAX),
                ] {
                    let layout = TrailingSliceLayout {
                        offset,
                        elem_size,
                        size_base,
                        size_rounding_align_and_phase: RoundingAlignAndPhase::new(alignment, phase),
                    };
                    for elems in [0, 1, 2, 3, alignment.get(), usize::MAX] {
                        // Independently reconstruct the complete rounded
                        // size and slice end, then subtract. Round up by
                        // adding the mask and clearing the low bits.
                        let trailing_bytes = elems.wrapping_mul(elem_size);
                        let unrounded = phase.wrapping_add(trailing_bytes);
                        let rounded = unrounded.wrapping_add(mask) & !mask;
                        let object_size = size_base.wrapping_add(rounded);
                        let trailing_end = offset.wrapping_add(trailing_bytes);
                        assert_eq!(
                            layout.padding_for_elems(elems),
                            object_size.wrapping_sub(trailing_end),
                        );
                    }
                }
            }
        }
    }

    #[test]
    fn test_dst_layout_for_slice() {
        let layout = DstLayout::for_slice::<u32>();
        match layout.size_info {
            SizeInfo::SliceDst(TrailingSliceLayout { offset, elem_size, .. }) => {
                assert_eq!(offset, 0);
                assert_eq!(elem_size, 4);
            }
            _ => panic!("Expected SliceDst"),
        }
        assert_eq!(layout.align.get(), 4);
    }

    /// Tests of when a sized `DstLayout` is extended with a sized field.
    #[allow(clippy::decimal_literal_representation)]
    #[test]
    fn test_dst_layout_extend_sized_with_sized() {
        // This macro constructs a layout corresponding to a `u8` and extends
        // it with a zero-sized trailing field of the specified alignment. The
        // resulting size and alignment must both be the field's alignment
        // capped by the packing limit. Exercise every supported packing limit
        // as well as the absence of a packing limit.
        macro_rules! test_align_is_size {
            ($n:expr) => {
                let base = DstLayout::for_type::<u8>();
                let trailing_field = DstLayout::for_type::<elain::Align<$n>>();

                let packs = core::iter::once(None)
                    .chain((0..=29).map(|p| NonZeroUsize::new(2usize.pow(p))));

                for pack in packs {
                    let composite = base.extend(trailing_field, pack);
                    let max_align = pack.unwrap_or(DstLayout::CURRENT_MAX_ALIGN);
                    let align = $n.min(max_align.get());
                    assert_eq!(
                        composite,
                        DstLayout {
                            align: NonZeroUsize::new(align).unwrap(),
                            size_info: SizeInfo::Sized { size: align },
                            statically_shallow_unpadded: false,
                        }
                    )
                }
            };
        }

        test_align_is_size!(1);
        test_align_is_size!(2);
        test_align_is_size!(4);
        test_align_is_size!(8);
        test_align_is_size!(16);
        test_align_is_size!(32);
        test_align_is_size!(64);
        test_align_is_size!(128);
        test_align_is_size!(256);
        test_align_is_size!(512);
        test_align_is_size!(1024);
        test_align_is_size!(2048);
        test_align_is_size!(4096);
        test_align_is_size!(8192);
        test_align_is_size!(16384);
        test_align_is_size!(32768);
        test_align_is_size!(65536);
        test_align_is_size!(131072);
        test_align_is_size!(262144);
        test_align_is_size!(524288);
        test_align_is_size!(1048576);
        test_align_is_size!(2097152);
        test_align_is_size!(4194304);
        test_align_is_size!(8388608);
        test_align_is_size!(16777216);
        test_align_is_size!(33554432);
        test_align_is_size!(67108864);
        test_align_is_size!(33554432);
        test_align_is_size!(134217728);
        test_align_is_size!(268435456);
        test_align_is_size!(536870912);
    }

    /// Tests of when a sized `DstLayout` is extended with a DST field.
    #[test]
    fn test_dst_layout_extend_sized_with_dst() {
        // Test that for all combinations of real-world alignments and
        // `repr_packed` values, that the extension of a sized `DstLayout`` with
        // a DST field correctly computes the trailing offset in the composite
        // layout.

        let aligns = (0..29).map(|p| NonZeroUsize::new(2usize.pow(p)).unwrap());
        let packs = core::iter::once(None).chain(aligns.clone().map(Some));

        for field_type_align in aligns {
            for pack in packs.clone() {
                let base = DstLayout::for_type::<u8>();
                let elem_size = 42;
                let trailing_field_offset = 11;
                #[allow(clippy::arithmetic_side_effects)]
                let trailing_phase = trailing_field_offset % field_type_align;
                let trailing_base = util::round_down_to_next_multiple_of_alignment(
                    trailing_field_offset,
                    field_type_align,
                );

                let trailing_field = DstLayout {
                    align: field_type_align,
                    size_info: SizeInfo::SliceDst(TrailingSliceLayout {
                        elem_size,
                        offset: trailing_field_offset,
                        size_base: trailing_base,
                        size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                            field_type_align,
                            trailing_phase,
                        ),
                    }),
                    statically_shallow_unpadded: false,
                };

                let composite = base.extend(trailing_field, pack);

                let max_align = pack.unwrap_or(DstLayout::CURRENT_MAX_ALIGN).get();

                let align = field_type_align.get().min(max_align);
                let field_offset = align;
                let size_base = field_offset + trailing_base;

                assert_eq!(
                    composite,
                    DstLayout {
                        align: NonZeroUsize::new(align).unwrap(),
                        size_info: SizeInfo::SliceDst(TrailingSliceLayout {
                            elem_size,
                            offset: field_offset + trailing_field_offset,
                            size_base,
                            size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                                field_type_align,
                                trailing_phase,
                            ),
                        }),
                        statically_shallow_unpadded: false,
                    }
                )
            }
        }
    }

    /// Tests that calling `pad_to_align` on a sized `DstLayout` adds the
    /// expected amount of trailing padding.
    #[test]
    fn test_dst_layout_pad_to_align_with_sized() {
        // For all valid alignments `align`, construct a one-byte layout aligned
        // to `align`, call `pad_to_align`, and assert that the size of the
        // resulting layout is equal to `align`.
        for align in (0..29).map(|p| NonZeroUsize::new(2usize.pow(p)).unwrap()) {
            let layout = DstLayout {
                align,
                size_info: SizeInfo::Sized { size: 1 },
                statically_shallow_unpadded: true,
            };

            assert_eq!(
                layout.pad_to_align(),
                DstLayout {
                    align,
                    size_info: SizeInfo::Sized { size: align.get() },
                    statically_shallow_unpadded: align.get() == 1
                }
            );
        }

        // Test explicitly-provided combinations of unpadded and padded
        // counterparts.

        macro_rules! test {
            (unpadded { size: $unpadded_size:expr, align: $unpadded_align:expr }
                    => padded { size: $padded_size:expr, align: $padded_align:expr }) => {
                let unpadded = DstLayout {
                    align: NonZeroUsize::new($unpadded_align).unwrap(),
                    size_info: SizeInfo::Sized { size: $unpadded_size },
                    statically_shallow_unpadded: false,
                };
                let padded = unpadded.pad_to_align();

                assert_eq!(
                    padded,
                    DstLayout {
                        align: NonZeroUsize::new($padded_align).unwrap(),
                        size_info: SizeInfo::Sized { size: $padded_size },
                        statically_shallow_unpadded: false,
                    }
                );
            };
        }

        test!(unpadded { size: 0, align: 4 } => padded { size: 0, align: 4 });
        test!(unpadded { size: 1, align: 4 } => padded { size: 4, align: 4 });
        test!(unpadded { size: 2, align: 4 } => padded { size: 4, align: 4 });
        test!(unpadded { size: 3, align: 4 } => padded { size: 4, align: 4 });
        test!(unpadded { size: 4, align: 4 } => padded { size: 4, align: 4 });
        test!(unpadded { size: 5, align: 4 } => padded { size: 8, align: 4 });
        test!(unpadded { size: 6, align: 4 } => padded { size: 8, align: 4 });
        test!(unpadded { size: 7, align: 4 } => padded { size: 8, align: 4 });
        test!(unpadded { size: 8, align: 4 } => padded { size: 8, align: 4 });

        let current_max_align = DstLayout::CURRENT_MAX_ALIGN.get();

        test!(unpadded { size: 1, align: current_max_align }
                => padded { size: current_max_align, align: current_max_align });

        test!(unpadded { size: current_max_align + 1, align: current_max_align }
                => padded { size: current_max_align * 2, align: current_max_align });
    }

    /// Tests that calling `pad_to_align` on a DST `DstLayout` incorporates the
    /// type's trailing-padding operation into its runtime size formula.
    #[test]
    fn test_dst_layout_pad_to_align_with_dst() {
        for align in (0..29).map(|p| NonZeroUsize::new(2usize.pow(p)).unwrap()) {
            for offset in 0..10 {
                for elem_size in 0..10 {
                    let layout = DstLayout {
                        align,
                        size_info: SizeInfo::SliceDst(TrailingSliceLayout {
                            offset,
                            elem_size,
                            size_base: offset,
                            size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                                DstLayout::MIN_ALIGN,
                                0,
                            ),
                        }),
                        statically_shallow_unpadded: false,
                    };
                    assert_eq!(
                        layout.pad_to_align(),
                        DstLayout {
                            align,
                            size_info: SizeInfo::SliceDst(TrailingSliceLayout {
                                offset,
                                elem_size,
                                size_base: offset - offset % align,
                                size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                                    align,
                                    offset % align
                                ),
                            }),
                            statically_shallow_unpadded: false,
                        }
                    );
                }
            }
        }

        // Exercise the branch where the existing inner rounding alignment is
        // at least the enclosing alignment. The inner rounded term is already
        // aligned to two, so padding only advances `size_base` from three to
        // four.
        let inner_rounding_dominates = DstLayout {
            align: NonZeroUsize::new(2).unwrap(),
            size_info: SizeInfo::SliceDst(TrailingSliceLayout {
                offset: 3,
                elem_size: 1,
                size_base: 3,
                size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                    NonZeroUsize::new(4).unwrap(),
                    1,
                ),
            }),
            statically_shallow_unpadded: false,
        };
        assert_eq!(
            inner_rounding_dominates.pad_to_align(),
            DstLayout {
                align: NonZeroUsize::new(2).unwrap(),
                size_info: SizeInfo::SliceDst(TrailingSliceLayout {
                    offset: 3,
                    elem_size: 1,
                    size_base: 4,
                    size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                        NonZeroUsize::new(4).unwrap(),
                        1
                    ),
                }),
                statically_shallow_unpadded: false,
            }
        );

        // Exercise the branch where the enclosing alignment is larger. For
        // a trailing-slice byte count `trailing_size`, rounding
        // `6 + round_up(1 + trailing_size, 4)` to eight gives the equivalent
        // formula `8 + round_up(1 + trailing_size, 8)`.
        let outer_rounding_dominates = DstLayout {
            align: NonZeroUsize::new(8).unwrap(),
            size_info: SizeInfo::SliceDst(TrailingSliceLayout {
                offset: 7,
                elem_size: 1,
                size_base: 6,
                size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                    NonZeroUsize::new(4).unwrap(),
                    1,
                ),
            }),
            statically_shallow_unpadded: false,
        };
        assert_eq!(
            outer_rounding_dominates.pad_to_align(),
            DstLayout {
                align: NonZeroUsize::new(8).unwrap(),
                size_info: SizeInfo::SliceDst(TrailingSliceLayout {
                    offset: 7,
                    elem_size: 1,
                    size_base: 8,
                    size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                        NonZeroUsize::new(8).unwrap(),
                        1
                    ),
                }),
                statically_shallow_unpadded: false,
            }
        );
    }

    #[test]
    fn test_trailing_slice_layout_size_sequence_equivalence() {
        // These are the two normalized descriptions produced by a
        // `repr(C, packed(2))` wrapper around slice DSTs whose element types
        // both have size four but alignments four and two respectively. Their
        // formulas differ structurally, but both simplify to `2 + 4 * elems`.
        let align4 = TrailingSliceLayout {
            offset: 2,
            elem_size: 4,
            size_base: 2,
            size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                NonZeroUsize::new(4).unwrap(),
                0,
            ),
        };
        let align2 = TrailingSliceLayout {
            offset: 2,
            elem_size: 4,
            size_base: 2,
            size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                NonZeroUsize::new(2).unwrap(),
                0,
            ),
        };
        assert!(align4.has_same_size_sequence(align2));
        for elems in 0..8 {
            assert_eq!(align4.size_for_elems(elems), Some(2 + 4 * elems));
            assert_eq!(align4.size_for_elems(elems), align2.size_for_elems(elems));
        }

        let dynamically_padded = TrailingSliceLayout { elem_size: 1, ..align4 };
        assert!(!dynamically_padded.has_same_size_sequence(align2));
    }

    // This test takes a long time when running under Miri, so we skip it in
    // that case. This is acceptable because this is a logic test that doesn't
    // attempt to expose UB.
    #[test]
    #[cfg_attr(miri, ignore)]
    fn test_validate_cast_and_convert_metadata() {
        #[allow(non_local_definitions)]
        impl From<usize> for SizeInfo {
            fn from(size: usize) -> SizeInfo {
                SizeInfo::Sized { size }
            }
        }

        #[allow(non_local_definitions)]
        impl From<(usize, usize)> for SizeInfo {
            fn from((offset, elem_size): (usize, usize)) -> SizeInfo {
                SizeInfo::SliceDst(TrailingSliceLayout {
                    offset,
                    elem_size,
                    size_base: offset,
                    size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                        DstLayout::MIN_ALIGN,
                        0,
                    ),
                })
            }
        }

        fn layout<S: Into<SizeInfo>>(s: S, align: usize) -> DstLayout {
            let align = NonZeroUsize::new(align).unwrap();
            let size_info = match s.into() {
                SizeInfo::SliceDst(mut trailing) => {
                    let phase = trailing.size_base % align;
                    trailing.size_base =
                        util::round_down_to_next_multiple_of_alignment(trailing.size_base, align);
                    trailing.size_rounding_align_and_phase =
                        RoundingAlignAndPhase::new(align, phase);
                    SizeInfo::SliceDst(trailing)
                }
                size_info => size_info,
            };
            DstLayout { size_info, align, statically_shallow_unpadded: false }
        }

        /// This macro accepts arguments in the form of:
        ///
        ///           layout(_, _).validate(_, _, _), Ok(Some((_, _)))
        ///                  |  |           |  |  |            |  |
        ///    size ---------+  |           |  |  |            |  |
        ///    align -----------+           |  |  |            |  |
        ///    addr ------------------------+  |  |            |  |
        ///    bytes_len ----------------------+  |            |  |
        ///    cast_type -------------------------+            |  |
        ///    elems ------------------------------------------+  |
        ///    split_at ------------------------------------------+
        ///
        /// `.validate` is shorthand for `.validate_cast_and_convert_metadata`
        /// for brevity.
        ///
        /// Each argument can either be an iterator or a wildcard. Each
        /// wildcarded variable is implicitly replaced by an iterator over a
        /// representative sample of values for that variable. Each `test!`
        /// invocation iterates over every combination of values provided by
        /// each variable's iterator (ie, the cartesian product) and validates
        /// that the results are expected.
        ///
        /// The final argument uses the same syntax, but it has a different
        /// meaning:
        /// - If it is `Ok(pat)`, then the pattern `pat` is supplied to
        ///   a matching assert to validate the computed result for each
        ///   combination of input values.
        /// - If it is `Err(Some(msg) | None)`, then `test!` validates that the
        ///   call to `validate_cast_and_convert_metadata` panics with the given
        ///   panic message or, if the current Rust toolchain version is too
        ///   early to support panicking in `const fn`s, panics with *some*
        ///   message. In the latter case, the `const_panic!` macro is used,
        ///   which emits code which causes a non-panicking error at const eval
        ///   time, but which does panic when invoked at runtime. Thus, it is
        ///   merely difficult to predict the *value* of this panic. We deem
        ///   that testing against the real panic strings on stable and nightly
        ///   toolchains is enough to ensure correctness.
        ///
        /// Note that the meta-variables that match these variables have the
        /// `tt` type, and some valid expressions are not valid `tt`s (such as
        /// `a..b`). In this case, wrap the expression in parentheses, and it
        /// will become valid `tt`.
        macro_rules! test {
                (
                    layout($size:tt, $align:tt)
                    .validate($addr:tt, $bytes_len:tt, $cast_type:tt), $expect:pat $(,)?
                ) => {
                    itertools::iproduct!(
                        test!(@generate_size $size),
                        test!(@generate_align $align),
                        test!(@generate_usize $addr),
                        test!(@generate_usize $bytes_len),
                        test!(@generate_cast_type $cast_type)
                    ).for_each(|(size_info, align, addr, bytes_len, cast_type)| {
                        // Temporarily disable the panic hook installed by the test
                        // harness. If we don't do this, all panic messages will be
                        // kept in an internal log. On its own, this isn't a
                        // problem, but if a non-caught panic ever happens (ie, in
                        // code later in this test not in this macro), all of the
                        // previously-buffered messages will be dumped, hiding the
                        // real culprit.
                        let previous_hook = std::panic::take_hook();
                        // I don't understand why, but this seems to be required in
                        // addition to the previous line.
                        std::panic::set_hook(Box::new(|_| {}));
                        let actual = std::panic::catch_unwind(|| {
                            layout(size_info, align).validate_cast_and_convert_metadata(addr, bytes_len, cast_type)
                        }).map_err(|d| {
                            let msg = d.downcast::<&'static str>().ok().map(|s| *s.as_ref());
                            assert!(msg.is_some() || cfg!(no_zerocopy_panic_in_const_and_vec_try_reserve_1_57_0), "non-string panic messages are not permitted when usage of panic in const fn is enabled");
                            msg
                        });
                        std::panic::set_hook(previous_hook);

                        assert!(
                            matches!(actual, $expect),
                            "layout({:?}, {}).validate_cast_and_convert_metadata({}, {}, {:?})" ,size_info, align, addr, bytes_len, cast_type
                        );
                    });
                };
                (@generate_usize _) => { 0..8 };
                // Generate sizes for both Sized and !Sized types.
                (@generate_size _) => {
                    test!(@generate_size (_)).chain(test!(@generate_size (_, _)))
                };
                // Generate sizes for both Sized and !Sized types by chaining
                // specified iterators for each.
                (@generate_size ($sized_sizes:tt | $unsized_sizes:tt)) => {
                    test!(@generate_size ($sized_sizes)).chain(test!(@generate_size $unsized_sizes))
                };
                // Generate sizes for Sized types.
                (@generate_size (_)) => { test!(@generate_size (0..8)) };
                (@generate_size ($sizes:expr)) => { $sizes.into_iter().map(Into::<SizeInfo>::into) };
                // Generate sizes for !Sized types.
                (@generate_size ($min_sizes:tt, $elem_sizes:tt)) => {
                    itertools::iproduct!(
                        test!(@generate_min_size $min_sizes),
                        test!(@generate_elem_size $elem_sizes)
                    ).map(Into::<SizeInfo>::into)
                };
                (@generate_fixed_size _) => { (0..8).into_iter().map(Into::<SizeInfo>::into) };
                (@generate_min_size _) => { 0..8 };
                (@generate_elem_size _) => { 1..8 };
                (@generate_align _) => { [1, 2, 4, 8, 16] };
                (@generate_opt_usize _) => { [None].into_iter().chain((0..8).map(Some).into_iter()) };
                (@generate_cast_type _) => { [CastType::Prefix, CastType::Suffix] };
                (@generate_cast_type $variant:ident) => { [CastType::$variant] };
                // Some expressions need to be wrapped in parentheses in order to be
                // valid `tt`s (required by the top match pattern). See the comment
                // below for more details. This arm removes these parentheses to
                // avoid generating an `unused_parens` warning.
                (@$_:ident ($vals:expr)) => { $vals };
                (@$_:ident $vals:expr) => { $vals };
            }

        const EVENS: [usize; 8] = [0, 2, 4, 6, 8, 10, 12, 14];
        const ODDS: [usize; 8] = [1, 3, 5, 7, 9, 11, 13, 15];

        // base_size is too big for the memory region.
        test!(
            layout(((1..8) | ((1..8), (1..8))), _).validate([0], [0], _),
            Ok(Err(MetadataCastError::Size))
        );
        test!(
            layout(((2..8) | ((2..8), (2..8))), _).validate([0], [1], Prefix),
            Ok(Err(MetadataCastError::Size))
        );
        test!(
            layout(((2..8) | ((2..8), (2..8))), _).validate([0x1000_0000 - 1], [1], Suffix),
            Ok(Err(MetadataCastError::Size))
        );

        // addr is unaligned for prefix cast
        test!(layout(_, [2]).validate(ODDS, _, Prefix), Ok(Err(MetadataCastError::Alignment)));
        test!(layout(_, [2]).validate(ODDS, _, Prefix), Ok(Err(MetadataCastError::Alignment)));

        // addr is aligned, but end of buffer is unaligned for suffix cast
        test!(layout(_, [2]).validate(EVENS, ODDS, Suffix), Ok(Err(MetadataCastError::Alignment)));
        test!(layout(_, [2]).validate(EVENS, ODDS, Suffix), Ok(Err(MetadataCastError::Alignment)));

        // Unfortunately, these constants cannot easily be used in the
        // implementation of `validate_cast_and_convert_metadata`, since
        // `panic!` consumes a string literal, not an expression.
        //
        // It's important that these messages be in a separate module. If they
        // were at the function's top level, we'd pass them to `test!` as, e.g.,
        // `Err(TRAILING)`, which would run into a subtle Rust footgun - the
        // `TRAILING` identifier would be treated as a pattern to match rather
        // than a value to check for equality.
        mod msgs {
            pub(super) const TRAILING: &str =
                "attempted to cast to slice type with zero-sized element";
            pub(super) const OVERFLOW: &str = "`addr` + `bytes_len` > usize::MAX";
        }

        // casts with ZST trailing element types are unsupported
        test!(layout((_, [0]), _).validate(_, _, _), Err(Some(msgs::TRAILING) | None),);

        // addr + bytes_len must not overflow usize
        test!(layout(_, _).validate([usize::MAX], (1..100), _), Err(Some(msgs::OVERFLOW) | None));
        test!(layout(_, _).validate((1..100), [usize::MAX], _), Err(Some(msgs::OVERFLOW) | None));
        test!(
            layout(_, _).validate(
                [usize::MAX / 2 + 1, usize::MAX],
                [usize::MAX / 2 + 1, usize::MAX],
                _
            ),
            Err(Some(msgs::OVERFLOW) | None)
        );

        // Validates that `validate_cast_and_convert_metadata` satisfies its own
        // documented safety postconditions, and also a few other properties
        // that aren't documented but we want to guarantee anyway.
        fn validate_behavior(
            (layout, addr, bytes_len, cast_type): (DstLayout, usize, usize, CastType),
        ) {
            if let Ok((elems, split_at)) =
                layout.validate_cast_and_convert_metadata(addr, bytes_len, cast_type)
            {
                let (size_info, align) = (layout.size_info, layout.align);
                let debug_str = format!(
                    "layout({:?}, {}).validate_cast_and_convert_metadata({}, {}, {:?}) => ({}, {})",
                    size_info, align, addr, bytes_len, cast_type, elems, split_at
                );

                // If this is a sized type (no trailing slice), then `elems` is
                // meaningless, but in practice we set it to 0. Callers are not
                // allowed to rely on this, but a lot of math is nicer if
                // they're able to, and some callers might accidentally do that.
                let sized = matches!(layout.size_info, SizeInfo::Sized { .. });
                assert!(!(sized && elems != 0), "{}", debug_str);

                let resulting_size = match layout.size_info {
                    SizeInfo::Sized { size } => size,
                    SizeInfo::SliceDst(TrailingSliceLayout {
                        elem_size,
                        size_base,
                        size_rounding_align_and_phase,
                        ..
                    }) => {
                        let (size_align, size_phase) = size_rounding_align_and_phase.components();
                        let padded_size = |elems| {
                            let without_padding = size_phase + elems * elem_size;
                            size_base
                                + without_padding
                                + util::padding_needed_for(without_padding, size_align)
                        };

                        let resulting_size = padded_size(elems);
                        // Test that `validate_cast_and_convert_metadata`
                        // computed the largest possible value that fits in the
                        // given range.
                        assert!(padded_size(elems + 1) > bytes_len, "{}", debug_str);
                        resulting_size
                    }
                };

                // Test safety postconditions guaranteed by
                // `validate_cast_and_convert_metadata`.
                assert!(resulting_size <= bytes_len, "{}", debug_str);
                match cast_type {
                    CastType::Prefix => {
                        assert_eq!(addr % align, 0, "{}", debug_str);
                        assert_eq!(resulting_size, split_at, "{}", debug_str);
                    }
                    CastType::Suffix => {
                        assert_eq!(split_at, bytes_len - resulting_size, "{}", debug_str);
                        assert_eq!((addr + split_at) % align, 0, "{}", debug_str);
                    }
                }
            } else {
                let min_size = match layout.size_info {
                    SizeInfo::Sized { size } => size,
                    SizeInfo::SliceDst(TrailingSliceLayout {
                        size_base,
                        size_rounding_align_and_phase,
                        ..
                    }) => {
                        let (size_align, size_phase) = size_rounding_align_and_phase.components();
                        size_base + size_phase + util::padding_needed_for(size_phase, size_align)
                    }
                };

                // If a cast is invalid, it is either because...
                // 1. there are insufficient bytes at the given region for type:
                let insufficient_bytes = bytes_len < min_size;
                // 2. performing the cast would misalign type:
                let base = match cast_type {
                    CastType::Prefix => 0,
                    CastType::Suffix => bytes_len,
                };
                let misaligned = (base + addr) % layout.align != 0;

                assert!(insufficient_bytes || misaligned);
            }
        }

        let sizes = 0..8;
        let elem_sizes = 1..8;
        let size_infos = sizes
            .clone()
            .map(Into::<SizeInfo>::into)
            .chain(itertools::iproduct!(sizes, elem_sizes).map(Into::<SizeInfo>::into));
        let layouts = itertools::iproduct!(size_infos, [1, 2, 4, 8, 16, 32])
                .filter(|(size_info, align)| !matches!(size_info, SizeInfo::Sized { size } if size % align != 0))
                .map(|(size_info, align)| layout(size_info, align));
        itertools::iproduct!(layouts, 0..8, 0..8, [CastType::Prefix, CastType::Suffix])
            .for_each(validate_behavior);

        // Exercise a normalized formula which cannot be represented as
        // `round_up(offset + elems * elem_size, align)`: the physical trailing
        // slice begins at offset 7, while an inner aligned DST contributes the
        // size formula `2 + round_up(5 + elems, 4)` to a packed outer type.
        let nested_packed = DstLayout {
            align: NonZeroUsize::new(2).unwrap(),
            size_info: SizeInfo::SliceDst(TrailingSliceLayout {
                offset: 7,
                elem_size: 1,
                size_base: 6,
                size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                    NonZeroUsize::new(4).unwrap(),
                    1,
                ),
            }),
            statically_shallow_unpadded: false,
        };
        itertools::iproduct!([nested_packed], 0..8, 0..24, [CastType::Prefix, CastType::Suffix])
            .for_each(validate_behavior);

        assert!(matches!(
            nested_packed.validate_cast_and_convert_metadata(0, 8, CastType::Prefix),
            Err(MetadataCastError::Size)
        ));
        assert!(matches!(
            nested_packed.validate_cast_and_convert_metadata(0, 10, CastType::Prefix),
            Ok((3, 10))
        ));
    }

    #[test]
    #[cfg(__ZEROCOPY_INTERNAL_USE_ONLY_NIGHTLY_FEATURES_IN_TESTS)]
    fn test_validate_rust_layout() {
        use core::{
            convert::TryInto as _,
            ptr::{self, NonNull},
        };

        use crate::util::testutil::*;

        // This test synthesizes pointers with various metadata and uses Rust's
        // built-in APIs to confirm that Rust makes decisions about type layout
        // which are consistent with what we believe is guaranteed by the
        // language. If this test fails, it doesn't just mean our code is wrong
        // - it means we're misunderstanding the language's guarantees.

        #[derive(Debug)]
        struct MacroArgs {
            offset: usize,
            align: NonZeroUsize,
            elem_size: Option<usize>,
        }

        /// # Safety
        ///
        /// `test` promises to only call `addr_of_slice_field` on a `NonNull<T>`
        /// which points to a valid `T`.
        ///
        /// `with_elems` must produce a pointer which points to a valid `T`.
        fn test<T: ?Sized, W: Fn(usize) -> NonNull<T>>(
            args: MacroArgs,
            with_elems: W,
            addr_of_slice_field: Option<fn(NonNull<T>) -> NonNull<u8>>,
        ) {
            let dst = args.elem_size.is_some();
            let layout = {
                let size_info = match args.elem_size {
                    Some(elem_size) => SizeInfo::SliceDst(TrailingSliceLayout {
                        offset: args.offset,
                        elem_size,
                        size_base: util::round_down_to_next_multiple_of_alignment(
                            args.offset,
                            args.align,
                        ),
                        size_rounding_align_and_phase: RoundingAlignAndPhase::new(
                            args.align,
                            args.offset % args.align,
                        ),
                    }),
                    None => SizeInfo::Sized {
                        // Rust only supports types whose sizes are a multiple
                        // of their alignment. If the macro created a type like
                        // this:
                        //
                        //   #[repr(C, align(2))]
                        //   struct Foo([u8; 1]);
                        //
                        // ...then Rust will automatically round the type's size
                        // up to 2.
                        size: args.offset + util::padding_needed_for(args.offset, args.align),
                    },
                };
                DstLayout { size_info, align: args.align, statically_shallow_unpadded: false }
            };

            for elems in 0..128 {
                let ptr = with_elems(elems);

                if let Some(addr_of_slice_field) = addr_of_slice_field {
                    let slc_field_ptr = addr_of_slice_field(ptr).as_ptr();
                    // SAFETY: Both `slc_field_ptr` and `ptr` are pointers to
                    // the same valid Rust object.
                    // Work around https://github.com/rust-lang/rust-clippy/issues/12280
                    let offset: usize =
                        unsafe { slc_field_ptr.byte_offset_from(ptr.as_ptr()).try_into().unwrap() };
                    assert_eq!(offset, args.offset);
                }

                // SAFETY: `ptr` points to a valid `T`.
                #[allow(clippy::multiple_unsafe_ops_per_block)]
                let (size, align) = unsafe {
                    (mem::size_of_val_raw(ptr.as_ptr()), mem::align_of_val_raw(ptr.as_ptr()))
                };

                // Avoid expensive allocation when running under Miri.
                let assert_msg = if !cfg!(miri) {
                    format!("\n{:?}\nsize:{}, align:{}", args, size, align)
                } else {
                    String::new()
                };

                let without_padding =
                    args.offset + args.elem_size.map(|elem_size| elems * elem_size).unwrap_or(0);
                assert!(size >= without_padding, "{}", assert_msg);
                assert_eq!(align, args.align.get(), "{}", assert_msg);

                // This encodes the most important part of the test: our
                // understanding of how Rust determines the layout of repr(C)
                // types. Sized repr(C) types are trivial, but DST types have
                // some subtlety. Note that:
                // - For sized types, `without_padding` is just the size of the
                //   type that we constructed for `Foo`. Since we may have
                //   requested a larger alignment, `Foo` may actually be larger
                //   than this, hence `padding_needed_for`.
                // - For unsized types, `without_padding` is dynamically
                //   computed from the offset, the element size, and element
                //   count. We expect that the size of the object should be
                //   `offset + elem_size * elems` rounded up to the next
                //   alignment.
                let expected_size =
                    without_padding + util::padding_needed_for(without_padding, args.align);
                assert_eq!(expected_size, size, "{}", assert_msg);

                // For zero-sized element types,
                // `validate_cast_and_convert_metadata` just panics, so we skip
                // testing those types.
                if args.elem_size.map(|elem_size| elem_size > 0).unwrap_or(true) {
                    let addr = ptr.addr().get();
                    let (got_elems, got_split_at) = layout
                        .validate_cast_and_convert_metadata(addr, size, CastType::Prefix)
                        .unwrap();
                    // Avoid expensive allocation when running under Miri.
                    let assert_msg = if !cfg!(miri) {
                        format!(
                            "{}\nvalidate_cast_and_convert_metadata({}, {})",
                            assert_msg, addr, size,
                        )
                    } else {
                        String::new()
                    };
                    assert_eq!(got_split_at, size, "{}", assert_msg);
                    if dst {
                        assert!(got_elems >= elems, "{}", assert_msg);
                        if got_elems != elems {
                            // If `validate_cast_and_convert_metadata`
                            // returned more elements than `elems`, that
                            // means that `elems` is not the maximum number
                            // of elements that can fit in `size` - in other
                            // words, there is enough padding at the end of
                            // the value to fit at least one more element.
                            // If we use this metadata to synthesize a
                            // pointer, despite having a different element
                            // count, we still expect it to have the same
                            // size.
                            let got_ptr = with_elems(got_elems);
                            // SAFETY: `got_ptr` is a pointer to a valid `T`.
                            let size_of_got_ptr = unsafe { mem::size_of_val_raw(got_ptr.as_ptr()) };
                            assert_eq!(size_of_got_ptr, size, "{}", assert_msg);
                        }
                    } else {
                        // For sized casts, the returned element value is
                        // technically meaningless, and we don't guarantee any
                        // particular value. In practice, it's always zero.
                        assert_eq!(got_elems, 0, "{}", assert_msg)
                    }
                }
            }
        }

        macro_rules! validate_against_rust {
                ($offset:literal, $align:literal $(, $elem_size:literal)?) => {{
                    #[repr(C, align($align))]
                    struct Foo([u8; $offset]$(, [[u8; $elem_size]])?);

                    let args = MacroArgs {
                        offset: $offset,
                        align: $align.try_into().unwrap(),
                        elem_size: {
                            #[allow(unused)]
                            let ret = None::<usize>;
                            $(let ret = Some($elem_size);)?
                            ret
                        }
                    };

                    #[repr(C, align($align))]
                    struct FooAlign;
                    // Create an aligned buffer to use in order to synthesize
                    // pointers to `Foo`. We don't ever load values from these
                    // pointers - we just do arithmetic on them - so having a "real"
                    // block of memory as opposed to a validly-aligned-but-dangling
                    // pointer is only necessary to make Miri happy since we run it
                    // with "strict provenance" checking enabled.
                    let aligned_buf = Align::<_, FooAlign>::new([0u8; 1024]);
                    let with_elems = |elems| {
                        let slc = NonNull::slice_from_raw_parts(NonNull::from(&aligned_buf.t), elems);
                        #[allow(clippy::as_conversions)]
                        NonNull::new(slc.as_ptr() as *mut Foo).unwrap()
                    };
                    let addr_of_slice_field = {
                        #[allow(unused)]
                        let f = None::<fn(NonNull<Foo>) -> NonNull<u8>>;
                        $(
                            // SAFETY: `test` promises to only call `f` with a `ptr`
                            // to a valid `Foo`.
                            let f: Option<fn(NonNull<Foo>) -> NonNull<u8>> = Some(|ptr: NonNull<Foo>| unsafe {
                                NonNull::new(ptr::addr_of_mut!((*ptr.as_ptr()).1)).unwrap().cast::<u8>()
                            });
                            let _ = $elem_size;
                        )?
                        f
                    };

                    test::<Foo, _>(args, with_elems, addr_of_slice_field);
                }};
            }

        // Every permutation of:
        // - offset in [0, 4]
        // - align in [1, 16]
        // - elem_size in [0, 4] (plus no elem_size)
        validate_against_rust!(0, 1);
        validate_against_rust!(0, 1, 0);
        validate_against_rust!(0, 1, 1);
        validate_against_rust!(0, 1, 2);
        validate_against_rust!(0, 1, 3);
        validate_against_rust!(0, 1, 4);
        validate_against_rust!(0, 2);
        validate_against_rust!(0, 2, 0);
        validate_against_rust!(0, 2, 1);
        validate_against_rust!(0, 2, 2);
        validate_against_rust!(0, 2, 3);
        validate_against_rust!(0, 2, 4);
        validate_against_rust!(0, 4);
        validate_against_rust!(0, 4, 0);
        validate_against_rust!(0, 4, 1);
        validate_against_rust!(0, 4, 2);
        validate_against_rust!(0, 4, 3);
        validate_against_rust!(0, 4, 4);
        validate_against_rust!(0, 8);
        validate_against_rust!(0, 8, 0);
        validate_against_rust!(0, 8, 1);
        validate_against_rust!(0, 8, 2);
        validate_against_rust!(0, 8, 3);
        validate_against_rust!(0, 8, 4);
        validate_against_rust!(0, 16);
        validate_against_rust!(0, 16, 0);
        validate_against_rust!(0, 16, 1);
        validate_against_rust!(0, 16, 2);
        validate_against_rust!(0, 16, 3);
        validate_against_rust!(0, 16, 4);
        validate_against_rust!(1, 1);
        validate_against_rust!(1, 1, 0);
        validate_against_rust!(1, 1, 1);
        validate_against_rust!(1, 1, 2);
        validate_against_rust!(1, 1, 3);
        validate_against_rust!(1, 1, 4);
        validate_against_rust!(1, 2);
        validate_against_rust!(1, 2, 0);
        validate_against_rust!(1, 2, 1);
        validate_against_rust!(1, 2, 2);
        validate_against_rust!(1, 2, 3);
        validate_against_rust!(1, 2, 4);
        validate_against_rust!(1, 4);
        validate_against_rust!(1, 4, 0);
        validate_against_rust!(1, 4, 1);
        validate_against_rust!(1, 4, 2);
        validate_against_rust!(1, 4, 3);
        validate_against_rust!(1, 4, 4);
        validate_against_rust!(1, 8);
        validate_against_rust!(1, 8, 0);
        validate_against_rust!(1, 8, 1);
        validate_against_rust!(1, 8, 2);
        validate_against_rust!(1, 8, 3);
        validate_against_rust!(1, 8, 4);
        validate_against_rust!(1, 16);
        validate_against_rust!(1, 16, 0);
        validate_against_rust!(1, 16, 1);
        validate_against_rust!(1, 16, 2);
        validate_against_rust!(1, 16, 3);
        validate_against_rust!(1, 16, 4);
        validate_against_rust!(2, 1);
        validate_against_rust!(2, 1, 0);
        validate_against_rust!(2, 1, 1);
        validate_against_rust!(2, 1, 2);
        validate_against_rust!(2, 1, 3);
        validate_against_rust!(2, 1, 4);
        validate_against_rust!(2, 2);
        validate_against_rust!(2, 2, 0);
        validate_against_rust!(2, 2, 1);
        validate_against_rust!(2, 2, 2);
        validate_against_rust!(2, 2, 3);
        validate_against_rust!(2, 2, 4);
        validate_against_rust!(2, 4);
        validate_against_rust!(2, 4, 0);
        validate_against_rust!(2, 4, 1);
        validate_against_rust!(2, 4, 2);
        validate_against_rust!(2, 4, 3);
        validate_against_rust!(2, 4, 4);
        validate_against_rust!(2, 8);
        validate_against_rust!(2, 8, 0);
        validate_against_rust!(2, 8, 1);
        validate_against_rust!(2, 8, 2);
        validate_against_rust!(2, 8, 3);
        validate_against_rust!(2, 8, 4);
        validate_against_rust!(2, 16);
        validate_against_rust!(2, 16, 0);
        validate_against_rust!(2, 16, 1);
        validate_against_rust!(2, 16, 2);
        validate_against_rust!(2, 16, 3);
        validate_against_rust!(2, 16, 4);
        validate_against_rust!(3, 1);
        validate_against_rust!(3, 1, 0);
        validate_against_rust!(3, 1, 1);
        validate_against_rust!(3, 1, 2);
        validate_against_rust!(3, 1, 3);
        validate_against_rust!(3, 1, 4);
        validate_against_rust!(3, 2);
        validate_against_rust!(3, 2, 0);
        validate_against_rust!(3, 2, 1);
        validate_against_rust!(3, 2, 2);
        validate_against_rust!(3, 2, 3);
        validate_against_rust!(3, 2, 4);
        validate_against_rust!(3, 4);
        validate_against_rust!(3, 4, 0);
        validate_against_rust!(3, 4, 1);
        validate_against_rust!(3, 4, 2);
        validate_against_rust!(3, 4, 3);
        validate_against_rust!(3, 4, 4);
        validate_against_rust!(3, 8);
        validate_against_rust!(3, 8, 0);
        validate_against_rust!(3, 8, 1);
        validate_against_rust!(3, 8, 2);
        validate_against_rust!(3, 8, 3);
        validate_against_rust!(3, 8, 4);
        validate_against_rust!(3, 16);
        validate_against_rust!(3, 16, 0);
        validate_against_rust!(3, 16, 1);
        validate_against_rust!(3, 16, 2);
        validate_against_rust!(3, 16, 3);
        validate_against_rust!(3, 16, 4);
        validate_against_rust!(4, 1);
        validate_against_rust!(4, 1, 0);
        validate_against_rust!(4, 1, 1);
        validate_against_rust!(4, 1, 2);
        validate_against_rust!(4, 1, 3);
        validate_against_rust!(4, 1, 4);
        validate_against_rust!(4, 2);
        validate_against_rust!(4, 2, 0);
        validate_against_rust!(4, 2, 1);
        validate_against_rust!(4, 2, 2);
        validate_against_rust!(4, 2, 3);
        validate_against_rust!(4, 2, 4);
        validate_against_rust!(4, 4);
        validate_against_rust!(4, 4, 0);
        validate_against_rust!(4, 4, 1);
        validate_against_rust!(4, 4, 2);
        validate_against_rust!(4, 4, 3);
        validate_against_rust!(4, 4, 4);
        validate_against_rust!(4, 8);
        validate_against_rust!(4, 8, 0);
        validate_against_rust!(4, 8, 1);
        validate_against_rust!(4, 8, 2);
        validate_against_rust!(4, 8, 3);
        validate_against_rust!(4, 8, 4);
        validate_against_rust!(4, 16);
        validate_against_rust!(4, 16, 0);
        validate_against_rust!(4, 16, 1);
        validate_against_rust!(4, 16, 2);
        validate_against_rust!(4, 16, 3);
        validate_against_rust!(4, 16, 4);
    }
}

#[cfg(kani)]
mod proofs {
    //! Arithmetic models and input generators for the layout proofs.
    //!
    //! The contract models specify expected results without calling the
    //! operation being checked. The composition models (`extended_layout`,
    //! `padded_layout`, and `repr_c_layout`) also define the contracts' input
    //! domains: `Some` supplies an expected layout, while `None` excludes an
    //! input from the corresponding non-panicking contract. These models check
    //! representability in `usize`, without imposing Rust's `isize::MAX` bound.

    use core::alloc::Layout;

    use super::*;

    /// Evaluates the normalized object size for a trailing slice of
    /// `trailing_size` bytes:
    ///
    /// ```text
    /// size_base + round_up(size_phase + trailing_size, size_align)
    /// ```
    ///
    /// Here, `size_align` and `size_phase` come from `layout.size_rounding_align_and_phase`.
    /// `round_up` rounds its input up to a multiple of its alignment. Returns
    /// `None` if the size is not representable in `usize`.
    ///
    /// Using byte counts separates rounding from element-count multiplication
    /// and permits proofs over byte counts that are not whole elements.
    pub(super) fn slice_dst_size_for_trailing_bytes<ElementSize>(
        layout: TrailingSliceLayout<ElementSize>,
        trailing_size: usize,
    ) -> Option<usize> {
        let without_padding =
            layout.size_rounding_align_and_phase.components().1.checked_add(trailing_size)?;
        let padding =
            util::padding_needed_for(without_padding, layout.size_rounding_align_and_phase.align());
        let rounded = without_padding.checked_add(padding)?;
        layout.size_base.checked_add(rounded)
    }

    /// Returns the least multiple of `align` greater than or equal to `bytes`,
    /// or `None` if that multiple exceeds `usize::MAX`.
    ///
    /// Requires a power-of-two `align`. Composition models use this checked
    /// operation to identify inputs for which adding padding would overflow.
    fn round_up(bytes: usize, align: NonZeroUsize) -> Option<usize> {
        bytes.checked_add(util::padding_needed_for(bytes, align))
    }

    /// Models the largest trailing-slice byte count whose object size fits in
    /// `available` bytes. Returns `None` if even an empty trailing slice cannot
    /// fit. The result need not be a multiple of the element size.
    ///
    /// Subtracting `size_base` leaves the budget for the rounded contribution.
    /// Rounding that budget down to `size_align` and subtracting `size_phase`
    /// gives the largest byte count whose `slice_dst_size_for_trailing_bytes`
    /// result is at most `available`. Metadata and casting contracts use
    /// this bound without calling the production capacity calculation.
    pub(super) fn max_trailing_bytes(
        trailing: TrailingSliceLayout,
        available: usize,
    ) -> Option<usize> {
        let (align, phase) = trailing.size_rounding_align_and_phase.components();
        let capacity = available.checked_sub(trailing.size_base)?;
        util::round_down_to_next_multiple_of_alignment(capacity, align).checked_sub(phase)
    }

    /// Models appending `field` to a sized `prefix` for `DstLayout::extend`.
    ///
    /// Places the field at the first offset after the prefix that satisfies the
    /// field's alignment, capped by `packed` when present. The resulting
    /// alignment is the maximum of the prefix's alignment and this effective
    /// field alignment. A DST field retains its inner rounding operation, with
    /// its slice offset and size base shifted by the field's placement.
    /// Trailing padding for the enclosing layout is left to `padded_layout`.
    ///
    /// Returns `None` for a non-power-of-two alignment, an unsized prefix, or
    /// arithmetic overflow. The `statically_shallow_unpadded` flag is true only
    /// if both inputs have it set and field placement introduces no gap.
    pub(super) fn extended_layout(
        prefix: DstLayout,
        field: DstLayout,
        packed: Option<NonZeroUsize>,
    ) -> Option<DstLayout> {
        if !prefix.align.is_power_of_two()
            || !field.align.is_power_of_two()
            || packed.map_or(false, |align| !align.is_power_of_two())
        {
            return None;
        }
        let SizeInfo::Sized { size: prefix_size } = prefix.size_info else { return None };
        let field_align = field.align.min(packed.unwrap_or(DstLayout::THEORETICAL_MAX_ALIGN));
        let field_offset = round_up(prefix_size, field_align)?;
        let size_info = match field.size_info {
            SizeInfo::Sized { size } => SizeInfo::Sized { size: field_offset.checked_add(size)? },
            SizeInfo::SliceDst(trailing) => SizeInfo::SliceDst(TrailingSliceLayout {
                offset: field_offset.checked_add(trailing.offset)?,
                size_base: field_offset.checked_add(trailing.size_base)?,
                ..trailing
            }),
        };
        Some(DstLayout {
            align: prefix.align.max(field_align),
            size_info,
            statically_shallow_unpadded: prefix.statically_shallow_unpadded
                && field.statically_shallow_unpadded
                && field_offset == prefix_size,
        })
    }

    /// Models adding trailing padding for `DstLayout::pad_to_align`.
    ///
    /// A sized layout rounds its size up to a multiple of `layout.align`. A DST
    /// instead composes that rounding with its existing size expression:
    ///
    /// ```text
    /// round_up(size_base + round_up(size_phase + trailing_size, size_align),
    ///          layout.align)
    /// ```
    ///
    /// Here, `size_align` and `size_phase` come from the DST's
    /// `size_rounding_align_and_phase`. The result encodes this expression with
    /// one base, phase, and rounding alignment, preserving the physical slice
    /// offset and element size. The `statically_shallow_unpadded` flag is
    /// cleared only when a sized layout gains padding; DST padding is accounted
    /// for by the size expression.
    ///
    /// Returns `None` if `layout.align` is not a power of two or a checked
    /// intermediate overflows. The contract compares this expected encoding
    /// with the production result; the separate padding formula proofs check
    /// that the encoding preserves the rounded size.
    pub(super) fn padded_layout(layout: DstLayout) -> Option<DstLayout> {
        if !layout.align.is_power_of_two() {
            return None;
        }
        let (size_info, no_static_padding) = match layout.size_info {
            SizeInfo::Sized { size } => {
                let rounded = round_up(size, layout.align)?;
                (SizeInfo::Sized { size: rounded }, rounded == size)
            }
            SizeInfo::SliceDst(trailing) => {
                let (inner_align, phase) = trailing.size_rounding_align_and_phase.components();
                let (size_base, size_rounding_align_and_phase) = if inner_align >= layout.align {
                    (
                        round_up(trailing.size_base, layout.align)?,
                        trailing.size_rounding_align_and_phase,
                    )
                } else {
                    let fixed = round_up(trailing.size_base, inner_align)?.checked_add(phase)?;
                    let new_phase = fixed & (layout.align.get() - 1);
                    (fixed - new_phase, RoundingAlignAndPhase::new(layout.align, new_phase))
                };
                (
                    SizeInfo::SliceDst(TrailingSliceLayout {
                        size_base,
                        size_rounding_align_and_phase,
                        ..trailing
                    }),
                    true,
                )
            }
        };
        Some(DstLayout {
            size_info,
            statically_shallow_unpadded: layout.statically_shallow_unpadded && no_static_padding,
            ..layout
        })
    }

    /// Models a complete layout for `DstLayout::for_repr_c_struct` by composing
    /// the field-placement and trailing-padding specifications.
    ///
    /// Starts with an empty prefix of alignment `align`, or one if absent.
    /// Appends fields in order, with `packed` as an optional cap on field
    /// alignment; the final layout is padded to its resulting alignment. Only
    /// the last field may be unsized, since field extension requires a sized
    /// prefix.
    ///
    /// Returns `None` if the initial alignment is not a power of two or any
    /// field extension or final padding step returns `None`.
    pub(super) fn repr_c_layout(
        align: Option<NonZeroUsize>,
        packed: Option<NonZeroUsize>,
        fields: &[DstLayout],
    ) -> Option<DstLayout> {
        let align = align.unwrap_or(DstLayout::MIN_ALIGN);
        if !align.is_power_of_two() {
            return None;
        }
        let mut layout = DstLayout {
            align,
            size_info: SizeInfo::Sized { size: 0 },
            statically_shallow_unpadded: true,
        };
        for &field in fields {
            layout = extended_layout(layout, field, packed)?;
        }
        padded_layout(layout)
    }

    // The inline contract harnesses use unrestricted `Arbitrary` values.
    // These generators retain the narrower domains of the Rust-layout proofs.

    /// Generates any power-of-two alignment representable in `usize`, including
    /// `DstLayout::THEORETICAL_MAX_ALIGN`. This restricts the alignment alone;
    /// callers must impose any constraints relating it to a layout's size.
    fn any_layout_align() -> NonZeroUsize {
        let exponent: u8 = kani::any();
        kani::assume(usize::from(exponent) < POINTER_WIDTH_BITS);

        // `exponent < POINTER_WIDTH_BITS`, so the shift is in range and its
        // result is non-zero. This construction is surjective over every
        // power-of-two alignment up to and including
        // `THEORETICAL_MAX_ALIGN`. `Layout` admits that maximum alignment for
        // zero-sized layouts, so the validity assumptions below—not this
        // generator—filter combinations that cannot describe Rust types.
        NonZeroUsize::new(1usize << exponent).unwrap()
    }

    /// Generates layouts satisfying the arithmetic restrictions used by the
    /// standalone Rust-layout proofs.
    ///
    /// The fixed size, or the zero-element DST size, must be accepted by
    /// `Layout::from_size_align`. For a DST, `size_base + size_phase` must also
    /// be representable and at least the physical slice offset. Component
    /// bounds come from `any_bounded_size_info`; `statically_shallow_unpadded`
    /// remains arbitrary. These conditions do not establish that a Rust type
    /// with this layout exists.
    fn any_valid_dst_layout() -> DstLayout {
        let align = any_layout_align();
        let size_info = any_bounded_size_info();

        kani::assume(
            match size_info {
                SizeInfo::Sized { size } => Layout::from_size_align(size, align.get()),
                SizeInfo::SliceDst(trailing) => {
                    // `SliceDst` cannot encode one exact size. Validate its
                    // minimum size and the invariants required of the size
                    // formula for layouts of real Rust types.
                    let min_size = trailing.size_for_elems(0);
                    let unrounded_min_size = trailing
                        .size_base
                        .checked_add(trailing.size_rounding_align_and_phase.components().1);

                    kani::assume(matches!(
                        unrounded_min_size,
                        Some(size) if size >= trailing.offset
                    ));

                    match min_size {
                        Some(min_size) => Layout::from_size_align(min_size, align.get()),
                        None => Layout::from_size_align(usize::MAX, align.get()),
                    }
                }
            }
            .is_ok(),
        );

        DstLayout { align: align, size_info: size_info, statically_shallow_unpadded: kani::any() }
    }

    /// Generates either a fixed size at most `DstLayout::MAX_SIZE`, or a DST
    /// description from `any_bounded_trailing_layout`.
    ///
    /// Supplies bounded size components to `any_valid_dst_layout`, which adds
    /// constraints involving the enclosing layout's alignment.
    fn any_bounded_size_info() -> SizeInfo {
        let is_sized: bool = kani::any();

        match is_sized {
            true => {
                let size: usize = kani::any();

                kani::assume(size <= DstLayout::MAX_SIZE);

                SizeInfo::Sized { size }
            }
            false => SizeInfo::SliceDst(any_bounded_trailing_layout()),
        }
    }

    /// Generates a trailing-slice description whose element size, slice offset,
    /// and size base are each at most `DstLayout::MAX_SIZE` (`isize::MAX`).
    ///
    /// The rounding alignment spans all representable powers of two, and the
    /// phase spans all values below that alignment. No further relationships
    /// between these components are imposed: their combined size may overflow
    /// or fail to contain the slice. Standalone proofs add the relationships
    /// they require; contract harnesses use unrestricted `Arbitrary` values
    /// instead.
    fn any_bounded_trailing_layout() -> TrailingSliceLayout {
        let elem_size: usize = kani::any();
        let offset: usize = kani::any();
        let size_base: usize = kani::any();
        let size_align = any_layout_align();
        let raw_size_phase: usize = kani::any();
        // Since `size_align` is a power of two, masking is surjective over
        // precisely the values in `0..size_align`.
        #[allow(clippy::arithmetic_side_effects)]
        let size_phase = raw_size_phase & (size_align.get() - 1);

        kani::assume(elem_size <= DstLayout::MAX_SIZE);
        kani::assume(offset <= DstLayout::MAX_SIZE);
        kani::assume(size_base <= DstLayout::MAX_SIZE);

        TrailingSliceLayout {
            elem_size,
            offset,
            size_base,
            size_rounding_align_and_phase: RoundingAlignAndPhase::new(size_align, size_phase),
        }
    }

    #[cfg(feature = "derive")]
    #[kani::proof]
    fn prove_padding_for_elems_for_rust_layouts() {
        // For each fixture, check every element count whose complete size
        // fits in `usize`, including all counts for zero-sized elements.
        padding_testutil::check_layouts(kani::any());
    }

    #[cfg(kani_slow)]
    #[kani::proof]
    #[kani::solver(kissat)]
    fn prove_padding_for_elems() {
        // Check the modular result for every field value and element count,
        // including layouts that cannot describe a Rust type.
        let layout = TrailingSliceLayout {
            offset: kani::any(),
            elem_size: kani::any(),
            size_base: kani::any(),
            size_rounding_align_and_phase: RoundingAlignAndPhase(kani::any()),
        };
        let elems: usize = kani::any();
        let (size_align, size_phase) = layout.size_rounding_align_and_phase.components();
        let mask = size_align.get() - 1;

        // Evaluate the complete size and trailing-slice end separately in
        // modular arithmetic, without reducing the trailing bytes first or
        // canceling them from the difference.
        let trailing_bytes = elems.wrapping_mul(layout.elem_size);
        let unrounded = size_phase.wrapping_add(trailing_bytes);
        let rounded = unrounded.wrapping_add(mask) & !mask;
        let object_size = layout.size_base.wrapping_add(rounded);
        let trailing_end = layout.offset.wrapping_add(trailing_bytes);
        assert_eq!(layout.padding_for_elems(elems), object_size.wrapping_sub(trailing_end));
    }

    #[kani::proof]
    fn prove_size_rounding_align_and_phase_encoding() {
        let encoded: usize = kani::any();
        kani::assume(encoded != 0);
        let encoded = NonZeroUsize::new(encoded).unwrap();
        let rounding = RoundingAlignAndPhase(encoded);

        let align = rounding.align();
        let phase = rounding.components().1;
        assert!(align.is_power_of_two());
        assert!(phase < align.get());
        assert_eq!(RoundingAlignAndPhase::new(align, phase), rounding);
    }

    #[kani::proof]
    fn prove_size_rounding_align_and_phase_decoding() {
        let align: NonZeroUsize = kani::any();
        let phase: usize = kani::any();
        kani::assume(align.is_power_of_two());
        kani::assume(phase < align.get());

        let rounding = RoundingAlignAndPhase::new(align, phase);
        assert_eq!(rounding.components(), (align, phase));
    }

    #[cfg(kani_slow)]
    #[kani::proof]
    fn prove_trailing_slice_layout_normal_form() {
        let rounding_word: usize = kani::any();
        kani::assume(rounding_word != 0);
        let align = RoundingAlignAndPhase(NonZeroUsize::new(rounding_word).unwrap()).align();

        let raw_prefix: usize = kani::any();
        let offset: usize = kani::any();
        let trailing_size: usize = kani::any();
        #[allow(clippy::arithmetic_side_effects)]
        let prefix = raw_prefix & (align.get() - 1);
        #[allow(clippy::arithmetic_side_effects)]
        let phase = offset & (align.get() - 1);
        #[allow(clippy::arithmetic_side_effects)]
        let aligned_offset = offset - phase;
        let layout = TrailingSliceLayout {
            offset: 0,
            elem_size: 1,
            size_base: aligned_offset | prefix,
            size_rounding_align_and_phase: RoundingAlignAndPhase::new(align, phase),
        };

        assert_eq!(layout.size_offset(), offset);

        let old_size = offset
            .checked_add(trailing_size)
            .and_then(|without_padding| {
                without_padding.checked_add(util::padding_needed_for(without_padding, align))
            })
            .and_then(|rounded| prefix.checked_add(rounded));
        assert_eq!(layout.size_for_elems(trailing_size), old_size);
        assert_eq!(slice_dst_size_for_trailing_bytes(layout, trailing_size), old_size);
    }

    #[cfg(kani_slow)]
    #[kani::proof]
    #[kani::solver(kissat)]
    fn prove_element_product_preserves_alignment() {
        let elem_size: usize = kani::any();
        let elems: usize = kani::any();
        let align = any_layout_align();
        #[allow(clippy::arithmetic_side_effects)]
        let align_mask = align.get() - 1;

        kani::assume(elem_size & align_mask == 0);
        let Some(bytes) = elem_size.checked_mul(elems) else {
            kani::assume(false);
            loop {}
        };
        assert_eq!(bytes & align_mask, 0);
    }

    #[kani::proof]
    fn prove_trailing_slice_layout_advance() {
        let layout: TrailingSliceLayout = any_bounded_trailing_layout();
        let bytes: usize = kani::any();
        let elem_size: usize = kani::any();
        let trailing_size: usize = kani::any();

        let size_offset = layout.size_offset();
        let advanced_offset = size_offset.checked_add(bytes);
        let advanced = layout.advance(bytes, elem_size);
        assert_eq!(advanced.is_some(), advanced_offset.is_some());
        let Some(advanced_offset) = advanced_offset else {
            kani::assume(false);
            loop {}
        };
        let Some(advanced) = advanced else { unreachable!() };

        assert_eq!(advanced.offset, layout.offset);
        assert_eq!(advanced.size_offset(), advanced_offset);
        assert_eq!(advanced.elem_size, elem_size);
        assert_eq!(
            advanced.size_rounding_align_and_phase.align(),
            layout.size_rounding_align_and_phase.align()
        );
        #[allow(clippy::arithmetic_side_effects)]
        let size_align_mask = advanced.size_rounding_align_and_phase.align().get() - 1;
        assert_eq!(advanced.size_base & size_align_mask, layout.size_base & size_align_mask);

        let size_align = layout.size_rounding_align_and_phase.align();
        #[allow(clippy::arithmetic_side_effects)]
        let size_prefix = layout.size_base & (size_align.get() - 1);
        let expected_size = advanced_offset
            .checked_add(trailing_size)
            .and_then(|without_padding| {
                without_padding.checked_add(util::padding_needed_for(without_padding, size_align))
            })
            .and_then(|rounded| size_prefix.checked_add(rounded));
        assert_eq!(slice_dst_size_for_trailing_bytes(advanced, trailing_size), expected_size);
    }

    #[cfg(kani_slow)]
    #[kani::proof]
    fn prove_has_same_size_sequence() {
        let left: TrailingSliceLayout = any_bounded_trailing_layout();
        let right: TrailingSliceLayout = any_bounded_trailing_layout();
        let trailing_size: usize = kani::any();

        kani::assume(left.has_same_size_sequence(right));
        let left_align = left.size_rounding_align_and_phase.align().get();
        let right_align = right.size_rounding_align_and_phase.align().get();
        let max_align = if left_align > right_align { left_align } else { right_align };
        #[allow(clippy::arithmetic_side_effects)]
        let max_align_mask = max_align - 1;
        if left.elem_size & max_align_mask == 0 {
            // In this branch `has_same_size_sequence` relies on every actual
            // trailing byte count being a multiple of both alignments. Prove
            // the formula for that entire byte class, which includes every
            // representable `elems * elem_size`, without introducing a
            // nonlinear symbolic multiplication into the harness.
            kani::assume(trailing_size & max_align_mask == 0);
        }
        assert_eq!(
            slice_dst_size_for_trailing_bytes(left, trailing_size),
            slice_dst_size_for_trailing_bytes(right, trailing_size)
        );
    }

    #[kani::proof]
    #[kani::solver(kissat)]
    fn prove_max_trailing_bytes() {
        let trailing: TrailingSliceLayout = any_bounded_trailing_layout();
        let available_bytes: usize = kani::any();
        let trailing_bytes: usize = kani::any();
        let fits = slice_dst_size_for_trailing_bytes(trailing, trailing_bytes)
            .map(|size| size <= available_bytes)
            .unwrap_or(false);
        match trailing.max_trailing_bytes(available_bytes) {
            Some(max_bytes) => assert_eq!(trailing_bytes <= max_bytes, fits),
            None => assert!(!fits),
        }
    }

    #[kani::proof]
    #[kani::solver(kissat)]
    fn prove_max_trailing_bytes_components() {
        let trailing: TrailingSliceLayout = any_bounded_trailing_layout();
        let bytes_len: usize = kani::any();
        let Some(max_slice_bytes) = trailing.max_trailing_bytes(bytes_len) else {
            kani::assume(false);
            loop {}
        };
        let (size_align, size_phase) = trailing.size_rounding_align_and_phase.components();
        let Some(bytes_after_base) = bytes_len.checked_sub(trailing.size_base) else {
            unreachable!()
        };
        let max_rounded_bytes =
            util::round_down_to_next_multiple_of_alignment(bytes_after_base, size_align);
        assert_eq!(size_phase.checked_add(max_slice_bytes), Some(max_rounded_bytes));
        assert!(trailing.size_base.checked_add(max_rounded_bytes).unwrap() <= bytes_len);
    }

    #[kani::proof]
    #[kani::solver(kissat)]
    fn prove_slice_dst_validator_selected_size_fits() {
        let size_align = any_layout_align();
        #[allow(clippy::arithmetic_side_effects)]
        let align_mask = size_align.get() - 1;
        let raw_phase: usize = kani::any();
        let size_phase = raw_phase & align_mask;
        let raw_capacity: usize = kani::any();
        let max_rounded_bytes = raw_capacity & !align_mask;
        let size_base: usize = kani::any();
        let bytes_len: usize = kani::any();
        let trailing_size: usize = kani::any();

        // These are exactly the relations established for every successful
        // capacity query by `prove_max_trailing_bytes_components`. Isolate
        // them from the helper's implementation to keep this arithmetic
        // identity independent of how that helper computes its bound.
        let Some(max_slice_bytes) = max_rounded_bytes.checked_sub(size_phase) else {
            kani::assume(false);
            loop {}
        };
        let Some(max_object_size) = size_base.checked_add(max_rounded_bytes) else {
            kani::assume(false);
            loop {}
        };
        kani::assume(max_object_size <= bytes_len);
        kani::assume(trailing_size <= max_slice_bytes);

        // For a whole-element count selected by the validator, this unused
        // byte count is also `max_slice_bytes % elem_size`. The identity here
        // holds for every byte count satisfying the capacity bound.
        #[allow(clippy::arithmetic_side_effects)]
        let unused_bytes = max_slice_bytes - trailing_size;
        let unused_aligned_bytes =
            util::round_down_to_next_multiple_of_alignment(unused_bytes, size_align);
        #[allow(clippy::arithmetic_side_effects)]
        let object_size = size_base + (max_rounded_bytes - unused_aligned_bytes);

        let unrounded = size_phase.checked_add(trailing_size).unwrap();
        let rounded =
            unrounded.checked_add(util::padding_needed_for(unrounded, size_align)).unwrap();
        assert_eq!(Some(object_size), size_base.checked_add(rounded));
        assert!(object_size <= bytes_len);
    }

    #[kani::proof]
    #[kani::solver(kissat)]
    fn prove_slice_dst_validator_rejects_larger_trailing_size() {
        let trailing: TrailingSliceLayout = any_bounded_trailing_layout();
        let bytes_len: usize = kani::any();
        let larger_trailing_size: usize = kani::any();
        let Some(max_slice_bytes) = trailing.max_trailing_bytes(bytes_len) else {
            kani::assume(false);
            loop {}
        };
        kani::assume(larger_trailing_size > max_slice_bytes);
        if let Some(object_size) = slice_dst_size_for_trailing_bytes(trailing, larger_trailing_size)
        {
            assert!(object_size > bytes_len);
        }
    }

    #[kani::proof]
    fn prove_slice_formula_contains_trailing_slice() {
        let trailing: TrailingSliceLayout = any_bounded_trailing_layout();
        let trailing_size: usize = kani::any();
        let (_, size_phase) = trailing.size_rounding_align_and_phase.components();

        let Some(unrounded_min) = trailing.size_base.checked_add(size_phase) else {
            kani::assume(false);
            loop {}
        };
        kani::assume(unrounded_min >= trailing.offset);
        let Some(object_size) = slice_dst_size_for_trailing_bytes(trailing, trailing_size) else {
            kani::assume(false);
            loop {}
        };

        let Some(trailing_end) = trailing.offset.checked_add(trailing_size) else { unreachable!() };
        assert!(trailing_end <= object_size);
    }

    #[kani::proof]
    fn prove_finalized_slice_formula_is_aligned() {
        let trailing: TrailingSliceLayout = any_bounded_trailing_layout();
        let outer_align = any_layout_align();
        let trailing_size: usize = kani::any();
        kani::assume(trailing.size_rounding_align_and_phase.align() >= outer_align);
        #[allow(clippy::arithmetic_side_effects)]
        let outer_align_mask = outer_align.get() - 1;
        kani::assume(trailing.size_base & outer_align_mask == 0);
        let Some(object_size) = slice_dst_size_for_trailing_bytes(trailing, trailing_size) else {
            kani::assume(false);
            loop {}
        };

        assert_eq!(object_size & outer_align_mask, 0);
    }

    #[kani::proof]
    fn prove_validator_checks_selected_endpoint_alignment() {
        let align = any_layout_align();
        let addr: usize = kani::any();
        let bytes_len: usize = kani::any();
        let Some(end) = addr.checked_add(bytes_len) else {
            kani::assume(false);
            loop {}
        };
        let suffix: bool = kani::any();
        let cast_type = if suffix { CastType::Suffix } else { CastType::Prefix };
        let selected_endpoint = if suffix { end } else { addr };
        #[allow(clippy::arithmetic_side_effects)]
        let align_mask = align.get() - 1;

        // The endpoint check precedes and is independent of the size-info
        // branch. A zero-sized layout makes every aligned case succeed and
        // isolates that control flow, including prefix/suffix split selection.
        let layout = DstLayout {
            align,
            size_info: SizeInfo::Sized { size: 0 },
            statically_shallow_unpadded: false,
        };
        match layout.validate_cast_and_convert_metadata(addr, bytes_len, cast_type) {
            Ok((elems, split_at)) => {
                assert_eq!(selected_endpoint & align_mask, 0);
                assert_eq!(elems, 0);
                if suffix {
                    assert_eq!(split_at, bytes_len);
                } else {
                    assert_eq!(split_at, 0);
                }
            }
            Err(MetadataCastError::Alignment) => {
                assert_ne!(selected_endpoint & align_mask, 0);
            }
            Err(MetadataCastError::Size) => unreachable!(),
        }
    }

    #[kani::proof]
    fn prove_aligned_suffix_end_and_size_imply_aligned_start() {
        let align = any_layout_align();
        #[allow(clippy::arithmetic_side_effects)]
        let align_mask = align.get() - 1;
        let addr: usize = kani::any();
        let bytes_len: usize = kani::any();
        let object_size: usize = kani::any();
        let Some(end) = addr.checked_add(bytes_len) else {
            kani::assume(false);
            loop {}
        };
        kani::assume(end & align_mask == 0);
        kani::assume(object_size & align_mask == 0);
        kani::assume(object_size <= bytes_len);

        #[allow(clippy::arithmetic_side_effects)]
        let split_at = bytes_len - object_size;
        let Some(object_start) = addr.checked_add(split_at) else { unreachable!() };
        assert_eq!(object_start & align_mask, 0);
    }

    #[cfg(kani_slow)]
    #[kani::proof]
    #[kani::solver(kissat)]
    fn prove_size_formula_bounds_size_offset() {
        // Prove the bound for arbitrary byte counts, including counts that
        // are not whole elements. The `size_for_elems` contract relates this
        // formula to checked element products. This harness checks the
        // formula's bound independently of that implementation.
        let trailing: TrailingSliceLayout = kani::any();
        let trailing_size: usize = kani::any();
        let Some(object_size) = slice_dst_size_for_trailing_bytes(trailing, trailing_size) else {
            kani::assume(false);
            loop {}
        };
        let size_offset = trailing.size_offset();
        let Some(without_padding) = size_offset.checked_add(trailing_size) else { unreachable!() };

        assert!(without_padding <= object_size);
    }

    // Older supported compilers resolve the unchecked operation syntax in
    // `add_scaled_metadata` to `polyfills::NumExt`; newer compilers prefer the
    // inherent methods. Use UFCS to verify the fallback implementation which
    // Kani's modern rustc cannot select in the production helper.
    #[kani::proof]
    #[kani::solver(kissat)]
    fn prove_num_ext_affine_fallback_matches_checked_arithmetic() {
        let base: usize = kani::any();
        let metadata: usize = kani::any();
        let multiple: usize = kani::any();
        let Some(scaled) = metadata.checked_mul(multiple) else {
            kani::assume(false);
            loop {}
        };
        let Some(expected) = base.checked_add(scaled) else {
            kani::assume(false);
            loop {}
        };

        // SAFETY: The checked multiplication returned `Some`, satisfying
        // `NumExt::unchecked_mul`'s no-overflow contract.
        let scaled =
            unsafe { <usize as crate::util::polyfills::NumExt>::unchecked_mul(metadata, multiple) };
        // SAFETY: The checked addition returned `Some`, and `scaled` is the
        // checked product, satisfying `NumExt::unchecked_add`'s no-overflow
        // contract.
        let actual =
            unsafe { <usize as crate::util::polyfills::NumExt>::unchecked_add(base, scaled) };
        assert_eq!(actual, expected);
    }

    #[cfg(kani_slow)]
    #[kani::proof]
    fn prove_requires_dynamic_padding() {
        let layout: DstLayout = any_valid_dst_layout();

        let SizeInfo::SliceDst(size_info) = layout.size_info else {
            kani::assume(false);
            loop {}
        };

        let meta: usize = kani::any();

        let Some(trailing_slice_size) = size_info.elem_size.checked_mul(meta) else {
            // The `trailing_slice_size` exceeds `usize::MAX`; `meta` is invalid.
            kani::assume(false);
            loop {}
        };

        let Some(trailing_slice_end) = size_info.offset.checked_add(trailing_slice_size) else {
            // The trailing slice end exceeds `usize::MAX`; `meta` is invalid.
            kani::assume(false);
            loop {}
        };

        let Some(size) = slice_dst_size_for_trailing_bytes(size_info, trailing_slice_size) else {
            kani::assume(false);
            loop {}
        };

        if size > DstLayout::MAX_SIZE || trailing_slice_end > size {
            // This metadata cannot describe a valid instance.
            kani::assume(false);
            loop {}
        }

        if !layout.requires_dynamic_padding() {
            assert_eq!(trailing_slice_end, size);
        }
    }

    #[cfg(kani_slow)]
    #[kani::proof]
    fn prove_dst_layout_extend() {
        use crate::util::{max, min, padding_needed_for};

        let base: DstLayout = any_valid_dst_layout();
        let field: DstLayout = any_valid_dst_layout();
        let packed: Option<NonZeroUsize> = kani::any();

        if let Some(max_align) = packed {
            kani::assume(max_align.is_power_of_two());
            kani::assume(base.align <= max_align);
        }

        // The base can only be extended if it's sized.
        kani::assume(matches!(base.size_info, SizeInfo::Sized { .. }));
        let base_size = if let SizeInfo::Sized { size } = base.size_info {
            size
        } else {
            unreachable!();
        };

        // The field's alignment is clamped by `max_align` (i.e., the
        // `packed` attribute, if any) [1].
        //
        // [1] Per https://doc.rust-lang.org/reference/type-layout.html#the-alignment-modifiers:
        //
        //   The alignments of each field, for the purpose of positioning
        //   fields, is the smaller of the specified alignment and the
        //   alignment of the field's type.
        let field_align = min(field.align, packed.unwrap_or(DstLayout::THEORETICAL_MAX_ALIGN));

        // Compute the minimum amount of inter-field padding needed to
        // satisfy the field's alignment, and offset of the trailing field.
        // [1]
        //
        // [1] Per https://doc.rust-lang.org/reference/type-layout.html#the-alignment-modifiers:
        //
        //   Inter-field padding is guaranteed to be the minimum required in
        //   order to satisfy each field's (possibly altered) alignment.
        let padding = padding_needed_for(base_size, field_align);
        let offset = match base_size.checked_add(padding) {
            Some(offset) => offset,
            None => {
                kani::assume(false);
                loop {}
            }
        };

        // Restrict the proof to extensions whose zero-metadata composite size
        // is representable. This semantic condition implies the
        // representability of each implementation intermediate below; prove
        // those implications rather than assuming the intermediates.
        match field.size_info {
            SizeInfo::Sized { size } => kani::assume(offset.checked_add(size).is_some()),
            SizeInfo::SliceDst(trailing) => {
                let Some(field_min) = trailing.size_for_elems(0) else {
                    kani::assume(false);
                    loop {}
                };
                kani::assume(offset.checked_add(field_min).is_some());

                assert!(offset.checked_add(trailing.offset).is_some());
                assert!(offset.checked_add(trailing.size_base).is_some());
            }
        }

        // Under the above conditions, `DstLayout::extend` will not panic.
        let composite = base.extend(field, packed);

        // The struct's alignment is the maximum of its previous alignment and
        // `field_align`.
        assert_eq!(composite.align, max(base.align, field_align));

        // For testing purposes, we'll also construct `alloc::Layout`
        // stand-ins for `DstLayout`, and show that `extend` behaves
        // comparably on both types.
        let base_analog = Layout::from_size_align(base_size, base.align.get()).unwrap();

        match field.size_info {
            SizeInfo::Sized { size: field_size } => {
                if let SizeInfo::Sized { size: composite_size } = composite.size_info {
                    // If the trailing field is sized, the resulting layout will
                    // be sized. Its size will be the sum of the preceding
                    // layout, the size of the new field, and the size of
                    // inter-field padding between the two.
                    assert_eq!(composite_size, offset + field_size);

                    let field_analog =
                        Layout::from_size_align(field_size, field_align.get()).unwrap();

                    if let Ok((actual_composite, actual_offset)) = base_analog.extend(field_analog)
                    {
                        assert_eq!(actual_offset, offset);
                        assert_eq!(actual_composite.size(), composite_size);
                        assert_eq!(actual_composite.align(), composite.align.get());
                    } else {
                        // An error here reflects that composite of `base`
                        // and `field` cannot correspond to a real Rust type
                        // fragment, because such a fragment would violate
                        // the basic invariants of a valid Rust layout. At
                        // the time of writing, `DstLayout` is a little more
                        // permissive than `Layout`, so we don't assert
                        // anything in this branch (e.g., unreachability).
                    }
                } else {
                    panic!("The composite of two sized layouts must be sized.")
                }
            }
            SizeInfo::SliceDst(field_trailing) => {
                if let SizeInfo::SliceDst(composite_trailing) = composite.size_info {
                    // The offset of the trailing slice component is the sum
                    // of the offset of the trailing field and the trailing
                    // slice offset within that field.
                    assert_eq!(composite_trailing.offset, offset + field_trailing.offset);
                    // The elem size is unchanged.
                    assert_eq!(composite_trailing.elem_size, field_trailing.elem_size);
                    // Extension preserves the invariant that the physical
                    // trailing-slice field fits within the unrounded minimum
                    // size. This is one of the premises consumed by the
                    // validator soundness proof.
                    let composite_phase =
                        composite_trailing.size_rounding_align_and_phase.components().1;
                    let Some(composite_unrounded_min) =
                        composite_trailing.size_base.checked_add(composite_phase)
                    else {
                        unreachable!()
                    };
                    assert!(composite_unrounded_min >= composite_trailing.offset);

                    // The normalized formula must continue to describe the
                    // size of the field at every trailing byte contribution
                    // for which the checked size computation succeeds. This
                    // is stronger than considering only products of a slice
                    // length and `elem_size`, and avoids a nonlinear
                    // multiplication in the verifier.
                    let trailing_size: usize = kani::any();
                    let field_size =
                        match slice_dst_size_for_trailing_bytes(field_trailing, trailing_size) {
                            Some(size) => size,
                            None => {
                                kani::assume(false);
                                loop {}
                            }
                        };
                    let expected_size = match offset.checked_add(field_size) {
                        Some(size) => size,
                        None => {
                            kani::assume(false);
                            loop {}
                        }
                    };
                    assert_eq!(
                        slice_dst_size_for_trailing_bytes(composite_trailing, trailing_size),
                        Some(expected_size)
                    );

                    // `alloc::Layout` permits sizes which are not a multiple
                    // of alignment, so this represents the unpadded prefix of
                    // the DST through the start of its trailing slice.
                    let field_analog =
                        Layout::from_size_align(field_trailing.offset, field_align.get()).unwrap();

                    if let Ok((actual_composite, actual_offset)) = base_analog.extend(field_analog)
                    {
                        assert_eq!(actual_offset, offset);
                        assert_eq!(actual_composite.size(), composite_trailing.offset);
                        assert_eq!(actual_composite.align(), composite.align.get());
                    } else {
                        // An error here reflects that composite of `base`
                        // and `field` cannot correspond to a real Rust type
                        // fragment, because such a fragment would violate
                        // the basic invariants of a valid Rust layout. At
                        // the time of writing, `DstLayout` is a little more
                        // permissive than `Layout`, so we don't assert
                        // anything in this branch (e.g., unreachability).
                    }
                } else {
                    panic!("The extension of a layout with a DST must result in a DST.")
                }
            }
        }
    }

    #[kani::proof]
    #[kani::should_panic]
    fn prove_dst_layout_extend_dst_panics() {
        let base: DstLayout = any_valid_dst_layout();
        let field: DstLayout = any_valid_dst_layout();
        let packed: Option<NonZeroUsize> = kani::any();

        if let Some(max_align) = packed {
            kani::assume(max_align.is_power_of_two());
            kani::assume(base.align <= max_align);
        }

        kani::assume(matches!(base.size_info, SizeInfo::SliceDst(..)));

        let _ = base.extend(field, packed);
    }

    #[cfg(kani_slow)]
    fn prove_dst_layout_pad_to_align_dst_invariants(
        layout: DstLayout,
        unpadded_trailing: TrailingSliceLayout,
        padded: DstLayout,
    ) {
        // Calling `pad_to_align` does not alter the `DstLayout`'s alignment.
        assert_eq!(padded.align, layout.align);
        assert_eq!(padded.statically_shallow_unpadded, layout.statically_shallow_unpadded);

        let SizeInfo::SliceDst(padded_trailing) = padded.size_info else {
            panic!("The padding of a DST layout must result in a DST layout.")
        };
        assert_eq!(padded_trailing.offset, unpadded_trailing.offset);
        assert_eq!(padded_trailing.elem_size, unpadded_trailing.elem_size);

        // A finalized DST formula is visibly aligned: the rounding alignment
        // is at least the outer alignment, and the fixed base is a multiple of
        // the outer alignment. This is the invariant which lets suffix
        // validation infer alignment of the object's start from alignment of
        // its end.
        assert!(padded_trailing.size_rounding_align_and_phase.align() >= padded.align);
        #[allow(clippy::arithmetic_side_effects)]
        let padded_align_mask = padded.align.get() - 1;
        assert_eq!(padded_trailing.size_base & padded_align_mask, 0);
        assert_eq!(padded.pad_to_align(), padded);
        let padded_phase = padded_trailing.size_rounding_align_and_phase.components().1;
        let Some(padded_unrounded_min) = padded_trailing.size_base.checked_add(padded_phase) else {
            unreachable!()
        };
        assert!(padded_unrounded_min >= padded_trailing.offset);
    }

    #[kani::proof]
    fn prove_dst_layout_pad_to_align_sized() {
        use crate::util::padding_needed_for;

        let layout: DstLayout = any_valid_dst_layout();
        let SizeInfo::Sized { size: unpadded_size } = layout.size_info else {
            kani::assume(false);
            loop {}
        };

        let padded = layout.pad_to_align();
        assert_eq!(padded.align, layout.align);
        let SizeInfo::Sized { size: padded_size } = padded.size_info else {
            panic!("The padding of a sized layout must result in a sized layout.")
        };

        // If the layout is sized, it will remain sized after padding is added.
        // Its sum will be its unpadded size and the size of the trailing
        // padding needed to satisfy its alignment requirements.
        let padding = padding_needed_for(unpadded_size, layout.align);
        assert_eq!(padded_size, unpadded_size + padding);
        assert_eq!(
            padded.statically_shallow_unpadded,
            layout.statically_shallow_unpadded && padding == 0
        );

        // Prove that calling `DstLayout::pad_to_align` behaves identically to
        // `Layout::pad_to_align`.
        let layout_analog = Layout::from_size_align(unpadded_size, layout.align.get()).unwrap();
        let padded_analog = layout_analog.pad_to_align();
        assert_eq!(padded_analog.align(), layout.align.get());
        assert_eq!(padded_analog.size(), padded_size);
    }

    #[cfg(kani_slow)]
    #[kani::proof]
    fn prove_dst_layout_pad_to_align_dst_inner_rounding_transition() {
        use crate::util::padding_needed_for;

        let layout: DstLayout = any_valid_dst_layout();
        let SizeInfo::SliceDst(trailing) = layout.size_info else {
            kani::assume(false);
            loop {}
        };
        kani::assume(trailing.size_rounding_align_and_phase.align() >= layout.align);

        // `any_valid_dst_layout` requires the zero-metadata layout to be valid.
        // Prove that this semantic condition bounds both the padded minimum
        // and the implementation intermediate; do not assume the intermediate
        // merely because the implementation uses it.
        let Some(min_size) = trailing.size_for_elems(0) else { unreachable!() };
        let min_padding = padding_needed_for(min_size, layout.align);
        let Some(padded_min_size) = min_size.checked_add(min_padding) else { unreachable!() };
        assert!(padded_min_size <= DstLayout::MAX_SIZE);

        let padding = padding_needed_for(trailing.size_base, layout.align);
        let Some(expected_base) = trailing.size_base.checked_add(padding) else { unreachable!() };

        let padded = layout.pad_to_align();
        let SizeInfo::SliceDst(padded_trailing) = padded.size_info else { unreachable!() };
        assert_eq!(padded_trailing.size_base, expected_base);
        assert_eq!(
            padded_trailing.size_rounding_align_and_phase,
            trailing.size_rounding_align_and_phase
        );
        prove_dst_layout_pad_to_align_dst_invariants(layout, trailing, padded);
    }

    #[kani::proof]
    #[kani::solver(kissat)]
    fn prove_dst_layout_pad_to_align_dst_inner_rounding_formula() {
        use crate::util::padding_needed_for;

        let size_align = any_layout_align();
        let outer_align = any_layout_align();
        kani::assume(size_align >= outer_align);
        let size_base: usize = kani::any();
        let raw_size_phase: usize = kani::any();
        #[allow(clippy::arithmetic_side_effects)]
        let size_phase = raw_size_phase & (size_align.get() - 1);
        let trailing_size: usize = kani::any();

        // Evaluate `size_base + round_up(size_phase + trailing_size,
        // size_align)`, then round that complete size to `outer_align`.
        // Compare with the equivalent expression obtained by rounding only
        // `size_base` to `outer_align`, since `size_align` is a multiple of
        // `outer_align`. Valid Rust objects have representable sizes; discard
        // only trailing byte counts whose input or padded object size is not
        // representable.
        let Some(unrounded_inner) = size_phase.checked_add(trailing_size) else {
            kani::assume(false);
            loop {}
        };
        let Some(rounded_inner) =
            unrounded_inner.checked_add(padding_needed_for(unrounded_inner, size_align))
        else {
            kani::assume(false);
            loop {}
        };
        let Some(unpadded_size) = size_base.checked_add(rounded_inner) else {
            kani::assume(false);
            loop {}
        };
        let Some(expected_size) =
            unpadded_size.checked_add(padding_needed_for(unpadded_size, outer_align))
        else {
            kani::assume(false);
            loop {}
        };

        let Some(padded_base) = size_base.checked_add(padding_needed_for(size_base, outer_align))
        else {
            unreachable!()
        };
        let Some(actual_size) = padded_base.checked_add(rounded_inner) else { unreachable!() };
        assert_eq!(actual_size, expected_size);
    }

    #[cfg(kani_slow)]
    #[kani::proof]
    fn prove_dst_layout_pad_to_align_dst_outer_alignment_transition() {
        use crate::util::padding_needed_for;

        let layout: DstLayout = any_valid_dst_layout();
        let SizeInfo::SliceDst(trailing) = layout.size_info else {
            kani::assume(false);
            loop {}
        };
        let size_align = trailing.size_rounding_align_and_phase.align();
        kani::assume(size_align < layout.align);

        // `any_valid_dst_layout` requires the zero-metadata layout to be valid.
        // Prove that this semantic condition bounds both the padded minimum
        // and every implementation intermediate; do not assume the
        // intermediates merely because the implementation uses them.
        let Some(min_size) = trailing.size_for_elems(0) else { unreachable!() };
        let min_padding = padding_needed_for(min_size, layout.align);
        let Some(padded_min_size) = min_size.checked_add(min_padding) else { unreachable!() };
        assert!(padded_min_size <= DstLayout::MAX_SIZE);

        let base_padding = padding_needed_for(trailing.size_base, size_align);
        let Some(rounded_base) = trailing.size_base.checked_add(base_padding) else {
            unreachable!()
        };
        let Some(t) =
            rounded_base.checked_add(trailing.size_rounding_align_and_phase.components().1)
        else {
            unreachable!()
        };
        #[allow(clippy::arithmetic_side_effects)]
        let expected_phase = t & (layout.align.get() - 1);
        #[allow(clippy::arithmetic_side_effects)]
        let expected_base = t - expected_phase;

        let padded = layout.pad_to_align();
        let SizeInfo::SliceDst(padded_trailing) = padded.size_info else { unreachable!() };
        assert_eq!(padded_trailing.size_base, expected_base);
        assert_eq!(
            padded_trailing.size_rounding_align_and_phase,
            RoundingAlignAndPhase::new(layout.align, expected_phase)
        );
        prove_dst_layout_pad_to_align_dst_invariants(layout, trailing, padded);
    }

    #[kani::proof]
    #[kani::solver(kissat)]
    fn prove_dst_layout_pad_to_align_dst_outer_alignment_formula() {
        use crate::util::padding_needed_for;

        let size_align = any_layout_align();
        let outer_align = any_layout_align();
        kani::assume(size_align < outer_align);
        let size_base: usize = kani::any();
        let raw_size_phase: usize = kani::any();
        #[allow(clippy::arithmetic_side_effects)]
        let size_phase = raw_size_phase & (size_align.get() - 1);
        let trailing_size: usize = kani::any();

        // Evaluate `size_base + round_up(size_phase + trailing_size,
        // size_align)`, then round that complete size to `outer_align`.
        let Some(unrounded_inner) = size_phase.checked_add(trailing_size) else {
            kani::assume(false);
            loop {}
        };
        let Some(rounded_inner) =
            unrounded_inner.checked_add(padding_needed_for(unrounded_inner, size_align))
        else {
            kani::assume(false);
            loop {}
        };
        let Some(unpadded_size) = size_base.checked_add(rounded_inner) else {
            kani::assume(false);
            loop {}
        };
        let Some(expected_size) =
            unpadded_size.checked_add(padding_needed_for(unpadded_size, outer_align))
        else {
            kani::assume(false);
            loop {}
        };

        // Since `outer_align` is a larger multiple of `size_align`, absorb
        // the inner rounding into the outer rounding and split the fixed
        // contribution into a base and phase:
        //
        //   fixed_contribution = round_up(size_base, size_align) + size_phase
        //   new_phase = fixed_contribution % outer_align
        //   new_base = fixed_contribution - new_phase
        //   new_base + round_up(new_phase + trailing_size, outer_align)
        let Some(rounded_base) = size_base.checked_add(padding_needed_for(size_base, size_align))
        else {
            unreachable!()
        };
        let Some(t) = rounded_base.checked_add(size_phase) else { unreachable!() };
        #[allow(clippy::arithmetic_side_effects)]
        let new_phase = t & (outer_align.get() - 1);
        #[allow(clippy::arithmetic_side_effects)]
        let new_base = t - new_phase;
        let Some(new_unrounded_inner) = new_phase.checked_add(trailing_size) else {
            unreachable!()
        };
        let Some(new_rounded_inner) =
            new_unrounded_inner.checked_add(padding_needed_for(new_unrounded_inner, outer_align))
        else {
            unreachable!()
        };
        let Some(actual_size) = new_base.checked_add(new_rounded_inner) else { unreachable!() };
        assert_eq!(actual_size, expected_size);
    }
}
