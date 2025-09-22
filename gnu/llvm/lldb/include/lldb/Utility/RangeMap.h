//===-- RangeMap.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_RANGEMAP_H
#define LLDB_UTILITY_RANGEMAP_H

#include <algorithm>
#include <vector>

#include "llvm/ADT/SmallVector.h"

#include "lldb/lldb-private.h"

// Uncomment to make sure all Range objects are sorted when needed
//#define ASSERT_RANGEMAP_ARE_SORTED

namespace lldb_private {

// Templatized classes for dealing with generic ranges and also collections of
// ranges, or collections of ranges that have associated data.

// A simple range class where you get to define the type of the range
// base "B", and the type used for the range byte size "S".
template <typename B, typename S> struct Range {
  typedef B BaseType;
  typedef S SizeType;

  BaseType base;
  SizeType size;

  Range() : base(0), size(0) {}

  Range(BaseType b, SizeType s) : base(b), size(s) {}

  void Clear(BaseType b = 0) {
    base = b;
    size = 0;
  }

  BaseType GetRangeBase() const { return base; }

  /// Set the start value for the range, and keep the same size
  void SetRangeBase(BaseType b) { base = b; }

  void Slide(BaseType slide) { base += slide; }

  void ShrinkFront(S s) {
    base += s;
    size -= std::min(s, size);
  }

  bool Union(const Range &rhs) {
    if (DoesAdjoinOrIntersect(rhs)) {
      auto new_end = std::max<BaseType>(GetRangeEnd(), rhs.GetRangeEnd());
      base = std::min<BaseType>(base, rhs.base);
      size = new_end - base;
      return true;
    }
    return false;
  }

  Range Intersect(const Range &rhs) const {
    const BaseType lhs_base = this->GetRangeBase();
    const BaseType rhs_base = rhs.GetRangeBase();
    const BaseType lhs_end = this->GetRangeEnd();
    const BaseType rhs_end = rhs.GetRangeEnd();
    Range range;
    range.SetRangeBase(std::max(lhs_base, rhs_base));
    range.SetRangeEnd(std::min(lhs_end, rhs_end));
    return range;
  }

  BaseType GetRangeEnd() const { return base + size; }

  void SetRangeEnd(BaseType end) {
    if (end > base)
      size = end - base;
    else
      size = 0;
  }

  SizeType GetByteSize() const { return size; }

  void SetByteSize(SizeType s) { size = s; }

  bool IsValid() const { return size > 0; }

  bool Contains(BaseType r) const {
    return (GetRangeBase() <= r) && (r < GetRangeEnd());
  }

  bool ContainsEndInclusive(BaseType r) const {
    return (GetRangeBase() <= r) && (r <= GetRangeEnd());
  }

  bool Contains(const Range &range) const {
    return Contains(range.GetRangeBase()) &&
           ContainsEndInclusive(range.GetRangeEnd());
  }

  // Returns true if the two ranges adjoing or intersect
  bool DoesAdjoinOrIntersect(const Range &rhs) const {
    const BaseType lhs_base = this->GetRangeBase();
    const BaseType rhs_base = rhs.GetRangeBase();
    const BaseType lhs_end = this->GetRangeEnd();
    const BaseType rhs_end = rhs.GetRangeEnd();
    bool result = (lhs_base <= rhs_end) && (lhs_end >= rhs_base);
    return result;
  }

  // Returns true if the two ranges intersect
  bool DoesIntersect(const Range &rhs) const {
    return Intersect(rhs).IsValid();
  }

  bool operator<(const Range &rhs) const {
    if (base == rhs.base)
      return size < rhs.size;
    return base < rhs.base;
  }

  bool operator==(const Range &rhs) const {
    return base == rhs.base && size == rhs.size;
  }

  bool operator!=(const Range &rhs) const {
    return base != rhs.base || size != rhs.size;
  }
};

template <typename B, typename S, unsigned N = 0> class RangeVector {
public:
  typedef B BaseType;
  typedef S SizeType;
  typedef Range<B, S> Entry;
  typedef llvm::SmallVector<Entry, N> Collection;

  RangeVector() = default;

  ~RangeVector() = default;

  static RangeVector GetOverlaps(const RangeVector &vec1,
                                 const RangeVector &vec2) {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(vec1.IsSorted() && vec2.IsSorted());
#endif
    RangeVector result;
    auto pos1 = vec1.begin();
    auto end1 = vec1.end();
    auto pos2 = vec2.begin();
    auto end2 = vec2.end();
    while (pos1 != end1 && pos2 != end2) {
      Entry entry = pos1->Intersect(*pos2);
      if (entry.IsValid())
        result.Append(entry);
      if (pos1->GetRangeEnd() < pos2->GetRangeEnd())
        ++pos1;
      else
        ++pos2;
    }
    return result;
  }

  bool operator==(const RangeVector &rhs) const {
    if (GetSize() != rhs.GetSize())
      return false;
    for (size_t i = 0; i < GetSize(); ++i) {
      if (GetEntryRef(i) != rhs.GetEntryRef(i))
        return false;
    }
    return true;
  }

  void Append(const Entry &entry) { m_entries.push_back(entry); }

  void Append(B base, S size) { m_entries.emplace_back(base, size); }

  // Insert an item into a sorted list and optionally combine it with any
  // adjacent blocks if requested.
  void Insert(const Entry &entry, bool combine) {
    if (m_entries.empty()) {
      m_entries.push_back(entry);
      return;
    }
    auto begin = m_entries.begin();
    auto end = m_entries.end();
    auto pos = std::lower_bound(begin, end, entry);
    if (combine) {
      if (pos != end && pos->Union(entry)) {
        CombinePrevAndNext(pos);
        return;
      }
      if (pos != begin) {
        auto prev = pos - 1;
        if (prev->Union(entry)) {
          CombinePrevAndNext(prev);
          return;
        }
      }
    }
    m_entries.insert(pos, entry);
  }

  bool RemoveEntryAtIndex(uint32_t idx) {
    if (idx < m_entries.size()) {
      m_entries.erase(m_entries.begin() + idx);
      return true;
    }
    return false;
  }

  void Sort() {
    if (m_entries.size() > 1)
      std::stable_sort(m_entries.begin(), m_entries.end());
  }

#ifdef ASSERT_RANGEMAP_ARE_SORTED
  bool IsSorted() const {
    typename Collection::const_iterator pos, end, prev;
    // First we determine if we can combine any of the Entry objects so we
    // don't end up allocating and making a new collection for no reason
    for (pos = m_entries.begin(), end = m_entries.end(), prev = end; pos != end;
         prev = pos++) {
      if (prev != end && *pos < *prev)
        return false;
    }
    return true;
  }
#endif

  void CombineConsecutiveRanges() {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    auto first_intersect = std::adjacent_find(
        m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) {
          return a.DoesAdjoinOrIntersect(b);
        });
    if (first_intersect == m_entries.end())
      return;

    // We can combine at least one entry, then we make a new collection and
    // populate it accordingly, and then swap it into place.
    auto pos = std::next(first_intersect);
    Collection minimal_ranges(m_entries.begin(), pos);
    for (; pos != m_entries.end(); ++pos) {
      Entry &back = minimal_ranges.back();
      if (back.DoesAdjoinOrIntersect(*pos))
        back.SetRangeEnd(std::max(back.GetRangeEnd(), pos->GetRangeEnd()));
      else
        minimal_ranges.push_back(*pos);
    }
    m_entries.swap(minimal_ranges);
  }

  BaseType GetMinRangeBase(BaseType fail_value) const {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    if (m_entries.empty())
      return fail_value;
    // m_entries must be sorted, so if we aren't empty, we grab the first
    // range's base
    return m_entries.front().GetRangeBase();
  }

  BaseType GetMaxRangeEnd(BaseType fail_value) const {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    if (m_entries.empty())
      return fail_value;
    // m_entries must be sorted, so if we aren't empty, we grab the last
    // range's end
    return m_entries.back().GetRangeEnd();
  }

  void Slide(BaseType slide) {
    typename Collection::iterator pos, end;
    for (pos = m_entries.begin(), end = m_entries.end(); pos != end; ++pos)
      pos->Slide(slide);
  }

  void Clear() { m_entries.clear(); }

  void Reserve(typename Collection::size_type size) { m_entries.reserve(size); }

  bool IsEmpty() const { return m_entries.empty(); }

  size_t GetSize() const { return m_entries.size(); }

  const Entry *GetEntryAtIndex(size_t i) const {
    return ((i < m_entries.size()) ? &m_entries[i] : nullptr);
  }

  // Clients must ensure that "i" is a valid index prior to calling this
  // function
  Entry &GetEntryRef(size_t i) { return m_entries[i]; }
  const Entry &GetEntryRef(size_t i) const { return m_entries[i]; }

  Entry *Back() { return (m_entries.empty() ? nullptr : &m_entries.back()); }

  const Entry *Back() const {
    return (m_entries.empty() ? nullptr : &m_entries.back());
  }

  static bool BaseLessThan(const Entry &lhs, const Entry &rhs) {
    return lhs.GetRangeBase() < rhs.GetRangeBase();
  }

  uint32_t FindEntryIndexThatContains(B addr) const {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    if (!m_entries.empty()) {
      Entry entry(addr, 1);
      typename Collection::const_iterator begin = m_entries.begin();
      typename Collection::const_iterator end = m_entries.end();
      typename Collection::const_iterator pos =
          std::lower_bound(begin, end, entry, BaseLessThan);

      if (pos != end && pos->Contains(addr)) {
        return std::distance(begin, pos);
      } else if (pos != begin) {
        --pos;
        if (pos->Contains(addr))
          return std::distance(begin, pos);
      }
    }
    return UINT32_MAX;
  }

  const Entry *FindEntryThatContains(B addr) const {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    if (!m_entries.empty()) {
      Entry entry(addr, 1);
      typename Collection::const_iterator begin = m_entries.begin();
      typename Collection::const_iterator end = m_entries.end();
      typename Collection::const_iterator pos =
          std::lower_bound(begin, end, entry, BaseLessThan);

      if (pos != end && pos->Contains(addr)) {
        return &(*pos);
      } else if (pos != begin) {
        --pos;
        if (pos->Contains(addr)) {
          return &(*pos);
        }
      }
    }
    return nullptr;
  }

  const Entry *FindEntryThatContains(const Entry &range) const {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    if (!m_entries.empty()) {
      typename Collection::const_iterator begin = m_entries.begin();
      typename Collection::const_iterator end = m_entries.end();
      typename Collection::const_iterator pos =
          std::lower_bound(begin, end, range, BaseLessThan);

      if (pos != end && pos->Contains(range)) {
        return &(*pos);
      } else if (pos != begin) {
        --pos;
        if (pos->Contains(range)) {
          return &(*pos);
        }
      }
    }
    return nullptr;
  }

  using const_iterator = typename Collection::const_iterator;
  const_iterator begin() const { return m_entries.begin(); }
  const_iterator end() const { return m_entries.end(); }

protected:
  void CombinePrevAndNext(typename Collection::iterator pos) {
    // Check if the prev or next entries in case they need to be unioned with
    // the entry pointed to by "pos".
    if (pos != m_entries.begin()) {
      auto prev = pos - 1;
      if (prev->Union(*pos))
        m_entries.erase(pos);
      pos = prev;
    }

    auto end = m_entries.end();
    if (pos != end) {
      auto next = pos + 1;
      if (next != end) {
        if (pos->Union(*next))
          m_entries.erase(next);
      }
    }
  }

  Collection m_entries;
};

// A simple range  with data class where you get to define the type of
// the range base "B", the type used for the range byte size "S", and the type
// for the associated data "T".
template <typename B, typename S, typename T>
struct RangeData : public Range<B, S> {
  typedef T DataType;

  DataType data;

  RangeData() : Range<B, S>(), data() {}

  RangeData(B base, S size) : Range<B, S>(base, size), data() {}

  RangeData(B base, S size, DataType d) : Range<B, S>(base, size), data(d) {}
};

// We can treat the vector as a flattened Binary Search Tree, augmenting it
// with upper bounds (max of range endpoints) for every index allows us to
// query for range containment quicker.
template <typename B, typename S, typename T>
struct AugmentedRangeData : public RangeData<B, S, T> {
  B upper_bound;

  AugmentedRangeData(const RangeData<B, S, T> &rd)
      : RangeData<B, S, T>(rd), upper_bound() {}
};

template <typename B, typename S, typename T, unsigned N = 0,
          class Compare = std::less<T>>
class RangeDataVector {
public:
  typedef lldb_private::Range<B, S> Range;
  typedef RangeData<B, S, T> Entry;
  typedef AugmentedRangeData<B, S, T> AugmentedEntry;
  typedef llvm::SmallVector<AugmentedEntry, N> Collection;

  RangeDataVector(Compare compare = Compare()) : m_compare(compare) {}

  ~RangeDataVector() = default;

  void Append(const Entry &entry) { m_entries.emplace_back(entry); }

  bool Erase(uint32_t start, uint32_t end) {
    if (start >= end || end > m_entries.size())
      return false;
    m_entries.erase(begin() + start, begin() + end);
    return true;
  }

  void Sort() {
    if (m_entries.size() > 1)
      std::stable_sort(m_entries.begin(), m_entries.end(),
                       [&compare = m_compare](const Entry &a, const Entry &b) {
                         if (a.base != b.base)
                           return a.base < b.base;
                         if (a.size != b.size)
                           return a.size < b.size;
                         return compare(a.data, b.data);
                       });
    if (!m_entries.empty())
      ComputeUpperBounds(0, m_entries.size());
  }

#ifdef ASSERT_RANGEMAP_ARE_SORTED
  bool IsSorted() const {
    typename Collection::const_iterator pos, end, prev;
    for (pos = m_entries.begin(), end = m_entries.end(), prev = end; pos != end;
         prev = pos++) {
      if (prev != end && *pos < *prev)
        return false;
    }
    return true;
  }
#endif

  void CombineConsecutiveEntriesWithEqualData() {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    typename Collection::iterator pos;
    typename Collection::iterator end;
    typename Collection::iterator prev;
    bool can_combine = false;
    // First we determine if we can combine any of the Entry objects so we
    // don't end up allocating and making a new collection for no reason
    for (pos = m_entries.begin(), end = m_entries.end(), prev = end; pos != end;
         prev = pos++) {
      if (prev != end && prev->data == pos->data) {
        can_combine = true;
        break;
      }
    }

    // We can combine at least one entry, then we make a new collection and
    // populate it accordingly, and then swap it into place.
    if (can_combine) {
      Collection minimal_ranges;
      for (pos = m_entries.begin(), end = m_entries.end(), prev = end;
           pos != end; prev = pos++) {
        if (prev != end && prev->data == pos->data)
          minimal_ranges.back().SetRangeEnd(pos->GetRangeEnd());
        else
          minimal_ranges.push_back(*pos);
      }
      // Use the swap technique in case our new vector is much smaller. We must
      // swap when using the STL because std::vector objects never release or
      // reduce the memory once it has been allocated/reserved.
      m_entries.swap(minimal_ranges);
    }
  }

  void Clear() { m_entries.clear(); }

  bool IsEmpty() const { return m_entries.empty(); }

  size_t GetSize() const { return m_entries.size(); }

  const Entry *GetEntryAtIndex(size_t i) const {
    return ((i < m_entries.size()) ? &m_entries[i] : nullptr);
  }

  Entry *GetMutableEntryAtIndex(size_t i) {
    return ((i < m_entries.size()) ? &m_entries[i] : nullptr);
  }

  // Clients must ensure that "i" is a valid index prior to calling this
  // function
  Entry &GetEntryRef(size_t i) { return m_entries[i]; }
  const Entry &GetEntryRef(size_t i) const { return m_entries[i]; }

  static bool BaseLessThan(const Entry &lhs, const Entry &rhs) {
    return lhs.GetRangeBase() < rhs.GetRangeBase();
  }

  uint32_t FindEntryIndexThatContains(B addr) const {
    const AugmentedEntry *entry =
        static_cast<const AugmentedEntry *>(FindEntryThatContains(addr));
    if (entry)
      return std::distance(m_entries.begin(), entry);
    return UINT32_MAX;
  }

  uint32_t FindEntryIndexesThatContain(B addr, std::vector<uint32_t> &indexes) {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    if (!m_entries.empty())
      FindEntryIndexesThatContain(addr, 0, m_entries.size(), indexes);

    return indexes.size();
  }

  Entry *FindEntryThatContains(B addr) {
    return const_cast<Entry *>(
        static_cast<const RangeDataVector *>(this)->FindEntryThatContains(
            addr));
  }

  const Entry *FindEntryThatContains(B addr) const {
    return FindEntryThatContains(Entry(addr, 1));
  }

  const Entry *FindEntryThatContains(const Entry &range) const {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    if (!m_entries.empty()) {
      typename Collection::const_iterator begin = m_entries.begin();
      typename Collection::const_iterator end = m_entries.end();
      typename Collection::const_iterator pos =
          std::lower_bound(begin, end, range, BaseLessThan);

      while (pos != begin && pos[-1].Contains(range))
        --pos;

      if (pos != end && pos->Contains(range))
        return &(*pos);
    }
    return nullptr;
  }

  const Entry *FindEntryStartsAt(B addr) const {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    if (!m_entries.empty()) {
      auto begin = m_entries.begin(), end = m_entries.end();
      auto pos = std::lower_bound(begin, end, Entry(addr, 1), BaseLessThan);
      if (pos != end && pos->base == addr)
        return &(*pos);
    }
    return nullptr;
  }

  // This method will return the entry that contains the given address, or the
  // entry following that address.  If you give it an address of 0 and the
  // first entry starts at address 0x100, you will get the entry at 0x100.
  //
  // For most uses, FindEntryThatContains is the correct one to use, this is a
  // less commonly needed behavior.  It was added for core file memory regions,
  // where we want to present a gap in the memory regions as a distinct region,
  // so we need to know the start address of the next memory section that
  // exists.
  const Entry *FindEntryThatContainsOrFollows(B addr) const {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    if (!m_entries.empty()) {
      typename Collection::const_iterator begin = m_entries.begin();
      typename Collection::const_iterator end = m_entries.end();
      typename Collection::const_iterator pos = llvm::lower_bound(
          m_entries, addr, [](const Entry &lhs, B rhs_base) -> bool {
            return lhs.GetRangeEnd() <= rhs_base;
          });

      while (pos != begin && pos[-1].Contains(addr))
        --pos;

      if (pos != end)
        return &(*pos);
    }
    return nullptr;
  }

  uint32_t FindEntryIndexThatContainsOrFollows(B addr) const {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    const AugmentedEntry *entry = static_cast<const AugmentedEntry *>(
        FindEntryThatContainsOrFollows(addr));
    if (entry)
      return std::distance(m_entries.begin(), entry);
    return UINT32_MAX;
  }

  Entry *Back() { return (m_entries.empty() ? nullptr : &m_entries.back()); }

  const Entry *Back() const {
    return (m_entries.empty() ? nullptr : &m_entries.back());
  }

  using const_iterator = typename Collection::const_iterator;
  const_iterator begin() const { return m_entries.begin(); }
  const_iterator end() const { return m_entries.end(); }

protected:
  Collection m_entries;
  Compare m_compare;

private:
  // Compute extra information needed for search
  B ComputeUpperBounds(size_t lo, size_t hi) {
    size_t mid = (lo + hi) / 2;
    AugmentedEntry &entry = m_entries[mid];

    entry.upper_bound = entry.base + entry.size;

    if (lo < mid)
      entry.upper_bound =
          std::max(entry.upper_bound, ComputeUpperBounds(lo, mid));

    if (mid + 1 < hi)
      entry.upper_bound =
          std::max(entry.upper_bound, ComputeUpperBounds(mid + 1, hi));

    return entry.upper_bound;
  }

  // This is based on the augmented tree implementation found at
  // https://en.wikipedia.org/wiki/Interval_tree#Augmented_tree
  void FindEntryIndexesThatContain(B addr, size_t lo, size_t hi,
                                   std::vector<uint32_t> &indexes) {
    size_t mid = (lo + hi) / 2;
    const AugmentedEntry &entry = m_entries[mid];

    // addr is greater than the rightmost point of any interval below mid
    // so there are cannot be any matches.
    if (addr > entry.upper_bound)
      return;

    // Recursively search left subtree
    if (lo < mid)
      FindEntryIndexesThatContain(addr, lo, mid, indexes);

    // If addr is smaller than the start of the current interval it
    // cannot contain it nor can any of its right subtree.
    if (addr < entry.base)
      return;

    if (entry.Contains(addr))
      indexes.push_back(entry.data);

    // Recursively search right subtree
    if (mid + 1 < hi)
      FindEntryIndexesThatContain(addr, mid + 1, hi, indexes);
  }
};

// A simple range  with data class where you get to define the type of
// the range base "B", the type used for the range byte size "S", and the type
// for the associated data "T".
template <typename B, typename T> struct AddressData {
  typedef B BaseType;
  typedef T DataType;

  BaseType addr;
  DataType data;

  AddressData() : addr(), data() {}

  AddressData(B a, DataType d) : addr(a), data(d) {}

  bool operator<(const AddressData &rhs) const {
    if (this->addr == rhs.addr)
      return this->data < rhs.data;
    return this->addr < rhs.addr;
  }

  bool operator==(const AddressData &rhs) const {
    return this->addr == rhs.addr && this->data == rhs.data;
  }

  bool operator!=(const AddressData &rhs) const {
    return this->addr != rhs.addr || this->data == rhs.data;
  }
};

template <typename B, typename T, unsigned N> class AddressDataArray {
public:
  typedef AddressData<B, T> Entry;
  typedef llvm::SmallVector<Entry, N> Collection;

  AddressDataArray() = default;

  ~AddressDataArray() = default;

  void Append(const Entry &entry) { m_entries.push_back(entry); }

  void Sort() {
    if (m_entries.size() > 1)
      std::stable_sort(m_entries.begin(), m_entries.end());
  }

#ifdef ASSERT_RANGEMAP_ARE_SORTED
  bool IsSorted() const {
    typename Collection::const_iterator pos, end, prev;
    // First we determine if we can combine any of the Entry objects so we
    // don't end up allocating and making a new collection for no reason
    for (pos = m_entries.begin(), end = m_entries.end(), prev = end; pos != end;
         prev = pos++) {
      if (prev != end && *pos < *prev)
        return false;
    }
    return true;
  }
#endif

  void Clear() { m_entries.clear(); }

  bool IsEmpty() const { return m_entries.empty(); }

  size_t GetSize() const { return m_entries.size(); }

  const Entry *GetEntryAtIndex(size_t i) const {
    return ((i < m_entries.size()) ? &m_entries[i] : nullptr);
  }

  // Clients must ensure that "i" is a valid index prior to calling this
  // function
  const Entry &GetEntryRef(size_t i) const { return m_entries[i]; }

  static bool BaseLessThan(const Entry &lhs, const Entry &rhs) {
    return lhs.addr < rhs.addr;
  }

  Entry *FindEntry(B addr, bool exact_match_only) {
#ifdef ASSERT_RANGEMAP_ARE_SORTED
    assert(IsSorted());
#endif
    if (!m_entries.empty()) {
      Entry entry;
      entry.addr = addr;
      typename Collection::iterator begin = m_entries.begin();
      typename Collection::iterator end = m_entries.end();
      typename Collection::iterator pos =
          llvm::lower_bound(m_entries, entry, BaseLessThan);

      while (pos != begin && pos[-1].addr == addr)
        --pos;

      if (pos != end) {
        if (pos->addr == addr || !exact_match_only)
          return &(*pos);
      }
    }
    return nullptr;
  }

  const Entry *FindNextEntry(const Entry *entry) {
    if (entry >= &*m_entries.begin() && entry + 1 < &*m_entries.end())
      return entry + 1;
    return nullptr;
  }

  Entry *Back() { return (m_entries.empty() ? nullptr : &m_entries.back()); }

  const Entry *Back() const {
    return (m_entries.empty() ? nullptr : &m_entries.back());
  }

protected:
  Collection m_entries;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_RANGEMAP_H
