//===-- llvm/ADT/edit_distance.h - Array edit distance function --- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines a Levenshtein distance function that works for any two
/// sequences, with each element of each sequence being analogous to a character
/// in a string.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_EDIT_DISTANCE_H
#define LLVM_ADT_EDIT_DISTANCE_H

#include "llvm/ADT/ArrayRef.h"
#include <algorithm>

namespace llvm {

/// Determine the edit distance between two sequences.
///
/// \param FromArray the first sequence to compare.
///
/// \param ToArray the second sequence to compare.
///
/// \param Map A Functor to apply to each item of the sequences before
/// comparison.
///
/// \param AllowReplacements whether to allow element replacements (change one
/// element into another) as a single operation, rather than as two operations
/// (an insertion and a removal).
///
/// \param MaxEditDistance If non-zero, the maximum edit distance that this
/// routine is allowed to compute. If the edit distance will exceed that
/// maximum, returns \c MaxEditDistance+1.
///
/// \returns the minimum number of element insertions, removals, or (if
/// \p AllowReplacements is \c true) replacements needed to transform one of
/// the given sequences into the other. If zero, the sequences are identical.
template <typename T, typename Functor>
unsigned ComputeMappedEditDistance(ArrayRef<T> FromArray, ArrayRef<T> ToArray,
                                   Functor Map, bool AllowReplacements = true,
                                   unsigned MaxEditDistance = 0) {
  // The algorithm implemented below is the "classic"
  // dynamic-programming algorithm for computing the Levenshtein
  // distance, which is described here:
  //
  //   http://en.wikipedia.org/wiki/Levenshtein_distance
  //
  // Although the algorithm is typically described using an m x n
  // array, only one row plus one element are used at a time, so this
  // implementation just keeps one vector for the row.  To update one entry,
  // only the entries to the left, top, and top-left are needed.  The left
  // entry is in Row[x-1], the top entry is what's in Row[x] from the last
  // iteration, and the top-left entry is stored in Previous.
  typename ArrayRef<T>::size_type m = FromArray.size();
  typename ArrayRef<T>::size_type n = ToArray.size();

  if (MaxEditDistance) {
    // If the difference in size between the 2 arrays is larger than the max
    // distance allowed, we can bail out as we will always need at least
    // MaxEditDistance insertions or removals.
    typename ArrayRef<T>::size_type AbsDiff = m > n ? m - n : n - m;
    if (AbsDiff > MaxEditDistance)
      return MaxEditDistance + 1;
  }

  SmallVector<unsigned, 64> Row(n + 1);
  for (unsigned i = 1; i < Row.size(); ++i)
    Row[i] = i;

  for (typename ArrayRef<T>::size_type y = 1; y <= m; ++y) {
    Row[0] = y;
    unsigned BestThisRow = Row[0];

    unsigned Previous = y - 1;
    const auto &CurItem = Map(FromArray[y - 1]);
    for (typename ArrayRef<T>::size_type x = 1; x <= n; ++x) {
      int OldRow = Row[x];
      if (AllowReplacements) {
        Row[x] = std::min(Previous + (CurItem == Map(ToArray[x - 1]) ? 0u : 1u),
                          std::min(Row[x - 1], Row[x]) + 1);
      }
      else {
        if (CurItem == Map(ToArray[x - 1]))
          Row[x] = Previous;
        else Row[x] = std::min(Row[x-1], Row[x]) + 1;
      }
      Previous = OldRow;
      BestThisRow = std::min(BestThisRow, Row[x]);
    }

    if (MaxEditDistance && BestThisRow > MaxEditDistance)
      return MaxEditDistance + 1;
  }

  unsigned Result = Row[n];
  return Result;
}

template <typename T>
unsigned ComputeEditDistance(ArrayRef<T> FromArray, ArrayRef<T> ToArray,
                             bool AllowReplacements = true,
                             unsigned MaxEditDistance = 0) {
  return ComputeMappedEditDistance(
      FromArray, ToArray, [](const T &X) -> const T & { return X; },
      AllowReplacements, MaxEditDistance);
}

} // End llvm namespace

#endif
