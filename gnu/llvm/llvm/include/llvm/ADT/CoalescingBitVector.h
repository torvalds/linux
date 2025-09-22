//===- llvm/ADT/CoalescingBitVector.h - A coalescing bitvector --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// A bitvector that uses an IntervalMap to coalesce adjacent elements
/// into intervals.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_COALESCINGBITVECTOR_H
#define LLVM_ADT_COALESCINGBITVECTOR_H

#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <initializer_list>

namespace llvm {

/// A bitvector that, under the hood, relies on an IntervalMap to coalesce
/// elements into intervals. Good for representing sets which predominantly
/// contain contiguous ranges. Bad for representing sets with lots of gaps
/// between elements.
///
/// Compared to SparseBitVector, CoalescingBitVector offers more predictable
/// performance for non-sequential find() operations.
///
/// \tparam IndexT - The type of the index into the bitvector.
template <typename IndexT> class CoalescingBitVector {
  static_assert(std::is_unsigned<IndexT>::value,
                "Index must be an unsigned integer.");

  using ThisT = CoalescingBitVector<IndexT>;

  /// An interval map for closed integer ranges. The mapped values are unused.
  using MapT = IntervalMap<IndexT, char>;

  using UnderlyingIterator = typename MapT::const_iterator;

  using IntervalT = std::pair<IndexT, IndexT>;

public:
  using Allocator = typename MapT::Allocator;

  /// Construct by passing in a CoalescingBitVector<IndexT>::Allocator
  /// reference.
  CoalescingBitVector(Allocator &Alloc)
      : Alloc(&Alloc), Intervals(Alloc) {}

  /// \name Copy/move constructors and assignment operators.
  /// @{

  CoalescingBitVector(const ThisT &Other)
      : Alloc(Other.Alloc), Intervals(*Other.Alloc) {
    set(Other);
  }

  ThisT &operator=(const ThisT &Other) {
    clear();
    set(Other);
    return *this;
  }

  CoalescingBitVector(ThisT &&Other) = delete;
  ThisT &operator=(ThisT &&Other) = delete;

  /// @}

  /// Clear all the bits.
  void clear() { Intervals.clear(); }

  /// Check whether no bits are set.
  bool empty() const { return Intervals.empty(); }

  /// Count the number of set bits.
  unsigned count() const {
    unsigned Bits = 0;
    for (auto It = Intervals.begin(), End = Intervals.end(); It != End; ++It)
      Bits += 1 + It.stop() - It.start();
    return Bits;
  }

  /// Set the bit at \p Index.
  ///
  /// This method does /not/ support setting a bit that has already been set,
  /// for efficiency reasons. If possible, restructure your code to not set the
  /// same bit multiple times, or use \ref test_and_set.
  void set(IndexT Index) {
    assert(!test(Index) && "Setting already-set bits not supported/efficient, "
                           "IntervalMap will assert");
    insert(Index, Index);
  }

  /// Set the bits set in \p Other.
  ///
  /// This method does /not/ support setting already-set bits, see \ref set
  /// for the rationale. For a safe set union operation, use \ref operator|=.
  void set(const ThisT &Other) {
    for (auto It = Other.Intervals.begin(), End = Other.Intervals.end();
         It != End; ++It)
      insert(It.start(), It.stop());
  }

  /// Set the bits at \p Indices. Used for testing, primarily.
  void set(std::initializer_list<IndexT> Indices) {
    for (IndexT Index : Indices)
      set(Index);
  }

  /// Check whether the bit at \p Index is set.
  bool test(IndexT Index) const {
    const auto It = Intervals.find(Index);
    if (It == Intervals.end())
      return false;
    assert(It.stop() >= Index && "Interval must end after Index");
    return It.start() <= Index;
  }

  /// Set the bit at \p Index. Supports setting an already-set bit.
  void test_and_set(IndexT Index) {
    if (!test(Index))
      set(Index);
  }

  /// Reset the bit at \p Index. Supports resetting an already-unset bit.
  void reset(IndexT Index) {
    auto It = Intervals.find(Index);
    if (It == Intervals.end())
      return;

    // Split the interval containing Index into up to two parts: one from
    // [Start, Index-1] and another from [Index+1, Stop]. If Index is equal to
    // either Start or Stop, we create one new interval. If Index is equal to
    // both Start and Stop, we simply erase the existing interval.
    IndexT Start = It.start();
    if (Index < Start)
      // The index was not set.
      return;
    IndexT Stop = It.stop();
    assert(Index <= Stop && "Wrong interval for index");
    It.erase();
    if (Start < Index)
      insert(Start, Index - 1);
    if (Index < Stop)
      insert(Index + 1, Stop);
  }

  /// Set union. If \p RHS is guaranteed to not overlap with this, \ref set may
  /// be a faster alternative.
  void operator|=(const ThisT &RHS) {
    // Get the overlaps between the two interval maps.
    SmallVector<IntervalT, 8> Overlaps;
    getOverlaps(RHS, Overlaps);

    // Insert the non-overlapping parts of all the intervals from RHS.
    for (auto It = RHS.Intervals.begin(), End = RHS.Intervals.end();
         It != End; ++It) {
      IndexT Start = It.start();
      IndexT Stop = It.stop();
      SmallVector<IntervalT, 8> NonOverlappingParts;
      getNonOverlappingParts(Start, Stop, Overlaps, NonOverlappingParts);
      for (IntervalT AdditivePortion : NonOverlappingParts)
        insert(AdditivePortion.first, AdditivePortion.second);
    }
  }

  /// Set intersection.
  void operator&=(const ThisT &RHS) {
    // Get the overlaps between the two interval maps (i.e. the intersection).
    SmallVector<IntervalT, 8> Overlaps;
    getOverlaps(RHS, Overlaps);
    // Rebuild the interval map, including only the overlaps.
    clear();
    for (IntervalT Overlap : Overlaps)
      insert(Overlap.first, Overlap.second);
  }

  /// Reset all bits present in \p Other.
  void intersectWithComplement(const ThisT &Other) {
    SmallVector<IntervalT, 8> Overlaps;
    if (!getOverlaps(Other, Overlaps)) {
      // If there is no overlap with Other, the intersection is empty.
      return;
    }

    // Delete the overlapping intervals. Split up intervals that only partially
    // intersect an overlap.
    for (IntervalT Overlap : Overlaps) {
      IndexT OlapStart, OlapStop;
      std::tie(OlapStart, OlapStop) = Overlap;

      auto It = Intervals.find(OlapStart);
      IndexT CurrStart = It.start();
      IndexT CurrStop = It.stop();
      assert(CurrStart <= OlapStart && OlapStop <= CurrStop &&
             "Expected some intersection!");

      // Split the overlap interval into up to two parts: one from [CurrStart,
      // OlapStart-1] and another from [OlapStop+1, CurrStop]. If OlapStart is
      // equal to CurrStart, the first split interval is unnecessary. Ditto for
      // when OlapStop is equal to CurrStop, we omit the second split interval.
      It.erase();
      if (CurrStart < OlapStart)
        insert(CurrStart, OlapStart - 1);
      if (OlapStop < CurrStop)
        insert(OlapStop + 1, CurrStop);
    }
  }

  bool operator==(const ThisT &RHS) const {
    // We cannot just use std::equal because it checks the dereferenced values
    // of an iterator pair for equality, not the iterators themselves. In our
    // case that results in comparison of the (unused) IntervalMap values.
    auto ItL = Intervals.begin();
    auto ItR = RHS.Intervals.begin();
    while (ItL != Intervals.end() && ItR != RHS.Intervals.end() &&
           ItL.start() == ItR.start() && ItL.stop() == ItR.stop()) {
      ++ItL;
      ++ItR;
    }
    return ItL == Intervals.end() && ItR == RHS.Intervals.end();
  }

  bool operator!=(const ThisT &RHS) const { return !operator==(RHS); }

  class const_iterator {
    friend class CoalescingBitVector;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = IndexT;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

  private:
    // For performance reasons, make the offset at the end different than the
    // one used in \ref begin, to optimize the common `It == end()` pattern.
    static constexpr unsigned kIteratorAtTheEndOffset = ~0u;

    UnderlyingIterator MapIterator;
    unsigned OffsetIntoMapIterator = 0;

    // Querying the start/stop of an IntervalMap iterator can be very expensive.
    // Cache these values for performance reasons.
    IndexT CachedStart = IndexT();
    IndexT CachedStop = IndexT();

    void setToEnd() {
      OffsetIntoMapIterator = kIteratorAtTheEndOffset;
      CachedStart = IndexT();
      CachedStop = IndexT();
    }

    /// MapIterator has just changed, reset the cached state to point to the
    /// start of the new underlying iterator.
    void resetCache() {
      if (MapIterator.valid()) {
        OffsetIntoMapIterator = 0;
        CachedStart = MapIterator.start();
        CachedStop = MapIterator.stop();
      } else {
        setToEnd();
      }
    }

    /// Advance the iterator to \p Index, if it is contained within the current
    /// interval. The public-facing method which supports advancing past the
    /// current interval is \ref advanceToLowerBound.
    void advanceTo(IndexT Index) {
      assert(Index <= CachedStop && "Cannot advance to OOB index");
      if (Index < CachedStart)
        // We're already past this index.
        return;
      OffsetIntoMapIterator = Index - CachedStart;
    }

    const_iterator(UnderlyingIterator MapIt) : MapIterator(MapIt) {
      resetCache();
    }

  public:
    const_iterator() { setToEnd(); }

    bool operator==(const const_iterator &RHS) const {
      // Do /not/ compare MapIterator for equality, as this is very expensive.
      // The cached start/stop values make that check unnecessary.
      return std::tie(OffsetIntoMapIterator, CachedStart, CachedStop) ==
             std::tie(RHS.OffsetIntoMapIterator, RHS.CachedStart,
                      RHS.CachedStop);
    }

    bool operator!=(const const_iterator &RHS) const {
      return !operator==(RHS);
    }

    IndexT operator*() const { return CachedStart + OffsetIntoMapIterator; }

    const_iterator &operator++() { // Pre-increment (++It).
      if (CachedStart + OffsetIntoMapIterator < CachedStop) {
        // Keep going within the current interval.
        ++OffsetIntoMapIterator;
      } else {
        // We reached the end of the current interval: advance.
        ++MapIterator;
        resetCache();
      }
      return *this;
    }

    const_iterator operator++(int) { // Post-increment (It++).
      const_iterator tmp = *this;
      operator++();
      return tmp;
    }

    /// Advance the iterator to the first set bit AT, OR AFTER, \p Index. If
    /// no such set bit exists, advance to end(). This is like std::lower_bound.
    /// This is useful if \p Index is close to the current iterator position.
    /// However, unlike \ref find(), this has worst-case O(n) performance.
    void advanceToLowerBound(IndexT Index) {
      if (OffsetIntoMapIterator == kIteratorAtTheEndOffset)
        return;

      // Advance to the first interval containing (or past) Index, or to end().
      while (Index > CachedStop) {
        ++MapIterator;
        resetCache();
        if (OffsetIntoMapIterator == kIteratorAtTheEndOffset)
          return;
      }

      advanceTo(Index);
    }
  };

  const_iterator begin() const { return const_iterator(Intervals.begin()); }

  const_iterator end() const { return const_iterator(); }

  /// Return an iterator pointing to the first set bit AT, OR AFTER, \p Index.
  /// If no such set bit exists, return end(). This is like std::lower_bound.
  /// This has worst-case logarithmic performance (roughly O(log(gaps between
  /// contiguous ranges))).
  const_iterator find(IndexT Index) const {
    auto UnderlyingIt = Intervals.find(Index);
    if (UnderlyingIt == Intervals.end())
      return end();
    auto It = const_iterator(UnderlyingIt);
    It.advanceTo(Index);
    return It;
  }

  /// Return a range iterator which iterates over all of the set bits in the
  /// half-open range [Start, End).
  iterator_range<const_iterator> half_open_range(IndexT Start,
                                                 IndexT End) const {
    assert(Start < End && "Not a valid range");
    auto StartIt = find(Start);
    if (StartIt == end() || *StartIt >= End)
      return {end(), end()};
    auto EndIt = StartIt;
    EndIt.advanceToLowerBound(End);
    return {StartIt, EndIt};
  }

  void print(raw_ostream &OS) const {
    OS << "{";
    for (auto It = Intervals.begin(), End = Intervals.end(); It != End;
         ++It) {
      OS << "[" << It.start();
      if (It.start() != It.stop())
        OS << ", " << It.stop();
      OS << "]";
    }
    OS << "}";
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() const {
    // LLDB swallows the first line of output after callling dump(). Add
    // newlines before/after the braces to work around this.
    dbgs() << "\n";
    print(dbgs());
    dbgs() << "\n";
  }
#endif

private:
  void insert(IndexT Start, IndexT End) { Intervals.insert(Start, End, 0); }

  /// Record the overlaps between \p this and \p Other in \p Overlaps. Return
  /// true if there is any overlap.
  bool getOverlaps(const ThisT &Other,
                   SmallVectorImpl<IntervalT> &Overlaps) const {
    for (IntervalMapOverlaps<MapT, MapT> I(Intervals, Other.Intervals);
         I.valid(); ++I)
      Overlaps.emplace_back(I.start(), I.stop());
    assert(llvm::is_sorted(Overlaps,
                           [](IntervalT LHS, IntervalT RHS) {
                             return LHS.second < RHS.first;
                           }) &&
           "Overlaps must be sorted");
    return !Overlaps.empty();
  }

  /// Given the set of overlaps between this and some other bitvector, and an
  /// interval [Start, Stop] from that bitvector, determine the portions of the
  /// interval which do not overlap with this.
  void getNonOverlappingParts(IndexT Start, IndexT Stop,
                              const SmallVectorImpl<IntervalT> &Overlaps,
                              SmallVectorImpl<IntervalT> &NonOverlappingParts) {
    IndexT NextUncoveredBit = Start;
    for (IntervalT Overlap : Overlaps) {
      IndexT OlapStart, OlapStop;
      std::tie(OlapStart, OlapStop) = Overlap;

      // [Start;Stop] and [OlapStart;OlapStop] overlap iff OlapStart <= Stop
      // and Start <= OlapStop.
      bool DoesOverlap = OlapStart <= Stop && Start <= OlapStop;
      if (!DoesOverlap)
        continue;

      // Cover the range [NextUncoveredBit, OlapStart). This puts the start of
      // the next uncovered range at OlapStop+1.
      if (NextUncoveredBit < OlapStart)
        NonOverlappingParts.emplace_back(NextUncoveredBit, OlapStart - 1);
      NextUncoveredBit = OlapStop + 1;
      if (NextUncoveredBit > Stop)
        break;
    }
    if (NextUncoveredBit <= Stop)
      NonOverlappingParts.emplace_back(NextUncoveredBit, Stop);
  }

  Allocator *Alloc;
  MapT Intervals;
};

} // namespace llvm

#endif // LLVM_ADT_COALESCINGBITVECTOR_H
