//===-- OptimizedStructLayout.h - Struct layout algorithm ---------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides an interface for laying out a sequence of fields
/// as a struct in a way that attempts to minimizes the total space
/// requirements of the struct while still satisfying the layout
/// requirements of the individual fields.  The resulting layout may be
/// substantially more compact than simply laying out the fields in their
/// original order.
///
/// Fields may be pre-assigned fixed offsets.  They may also be given sizes
/// that are not multiples of their alignments.  There is no currently no
/// way to describe that a field has interior padding that other fields may
/// be allocated into.
///
/// This algorithm does not claim to be "optimal" for several reasons:
///
/// - First, it does not guarantee that the result is minimal in size.
///   There is no known efficient algoorithm to achieve minimality for
///   unrestricted inputs.  Nonetheless, this algorithm
///
/// - Second, there are other ways that a struct layout could be optimized
///   besides space usage, such as locality.  This layout may have a mixed
///   impact on locality: less overall memory may be used, but adjacent
///   fields in the original array may be moved further from one another.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_OPTIMIZEDSTRUCTLAYOUT_H
#define LLVM_SUPPORT_OPTIMIZEDSTRUCTLAYOUT_H

#include "llvm/Support/Alignment.h"
#include "llvm/ADT/ArrayRef.h"
#include <utility>

namespace llvm {

/// A field in a structure.
struct OptimizedStructLayoutField {
  /// A special value for Offset indicating that the field can be moved
  /// anywhere.
  static constexpr uint64_t FlexibleOffset = ~(uint64_t)0;

  OptimizedStructLayoutField(const void *Id, uint64_t Size, Align Alignment,
                             uint64_t FixedOffset = FlexibleOffset)
      : Offset(FixedOffset), Size(Size), Id(Id), Alignment(Alignment) {
    assert(Size > 0 && "adding an empty field to the layout");
  }

  /// The offset of this field in the final layout.  If this is
  /// initialized to FlexibleOffset, layout will overwrite it with
  /// the assigned offset of the field.
  uint64_t Offset;

  /// The required size of this field in bytes.  Does not have to be
  /// a multiple of Alignment.  Must be non-zero.
  uint64_t Size;

  /// A opaque value which uniquely identifies this field.
  const void *Id;

  /// Private scratch space for the algorithm.  The implementation
  /// must treat this as uninitialized memory on entry.
  void *Scratch;

  /// The required alignment of this field.
  Align Alignment;

  /// Return true if this field has been assigned a fixed offset.
  /// After layout, this will be true of all the fields.
  bool hasFixedOffset() const {
    return (Offset != FlexibleOffset);
  }

  /// Given that this field has a fixed offset, return the offset
  /// of the first byte following it.
  uint64_t getEndOffset() const {
    assert(hasFixedOffset());
    return Offset + Size;
  }
};

/// Compute a layout for a struct containing the given fields, making a
/// best-effort attempt to minimize the amount of space required.
///
/// Two features are supported which require a more careful solution
/// than the well-known "sort by decreasing alignment" solution:
///
/// - Fields may be assigned a fixed offset in the layout.  If there are
///   gaps among the fixed-offset fields, the algorithm may attempt
///   to allocate flexible-offset fields into those gaps.  If that's
///   undesirable, the caller should "block out" those gaps by e.g.
///   just creating a single fixed-offset field that represents the
///   entire "header".
///
/// - The size of a field is not required to be a multiple of, or even
///   greater than, the field's required alignment.  The only constraint
///   on fields is that they must not be zero-sized.
///
/// To simplify the implementation, any fixed-offset fields in the
/// layout must appear at the start of the field array, and they must
/// be ordered by increasing offset.
///
/// The algorithm will produce a guaranteed-minimal layout with no
/// interior padding in the following "C-style" case:
///
/// - every field's size is a multiple of its required alignment and
/// - either no fields have initially fixed offsets, or the fixed-offset
///   fields have no interior padding and end at an offset that is at
///   least as aligned as all the flexible-offset fields.
///
/// Otherwise, while the algorithm will make a best-effort attempt to
/// avoid padding, it cannot guarantee a minimal layout, as there is
/// no known efficient algorithm for doing so.
///
/// The layout produced by this algorithm may not be stable across LLVM
/// releases.  Do not use this anywhere where ABI stability is required.
///
/// Flexible-offset fields with the same size and alignment will be ordered
/// the same way they were in the initial array.  Otherwise the current
/// algorithm makes no effort to preserve the initial order of
/// flexible-offset fields.
///
/// On return, all fields will have been assigned a fixed offset, and the
/// array will be sorted in order of ascending offsets.  Note that this
/// means that the fixed-offset fields may no longer form a strict prefix
/// if there's any padding before they end.
///
/// The return value is the total size of the struct and its required
/// alignment.  Note that the total size is not rounded up to a multiple
/// of the required alignment; clients which require this can do so easily.
std::pair<uint64_t, Align> performOptimizedStructLayout(
                        MutableArrayRef<OptimizedStructLayoutField> Fields);

} // namespace llvm

#endif
