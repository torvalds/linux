//===- sanitizer_dense_map.h - Dense probed hash table ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is fork of llvm/ADT/DenseMap.h class with the following changes:
//  * Use mmap to allocate.
//  * No iterators.
//  * Does not shrink.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_DENSE_MAP_H
#define SANITIZER_DENSE_MAP_H

#include "sanitizer_common.h"
#include "sanitizer_dense_map_info.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_type_traits.h"

namespace __sanitizer {

template <typename DerivedT, typename KeyT, typename ValueT, typename KeyInfoT,
          typename BucketT>
class DenseMapBase {
 public:
  using size_type = unsigned;
  using key_type = KeyT;
  using mapped_type = ValueT;
  using value_type = BucketT;

  WARN_UNUSED_RESULT bool empty() const { return getNumEntries() == 0; }
  unsigned size() const { return getNumEntries(); }

  /// Grow the densemap so that it can contain at least \p NumEntries items
  /// before resizing again.
  void reserve(size_type NumEntries) {
    auto NumBuckets = getMinBucketToReserveForEntries(NumEntries);
    if (NumBuckets > getNumBuckets())
      grow(NumBuckets);
  }

  void clear() {
    if (getNumEntries() == 0 && getNumTombstones() == 0)
      return;

    const KeyT EmptyKey = getEmptyKey(), TombstoneKey = getTombstoneKey();
    if (__sanitizer::is_trivially_destructible<ValueT>::value) {
      // Use a simpler loop when values don't need destruction.
      for (BucketT *P = getBuckets(), *E = getBucketsEnd(); P != E; ++P)
        P->getFirst() = EmptyKey;
    } else {
      unsigned NumEntries = getNumEntries();
      for (BucketT *P = getBuckets(), *E = getBucketsEnd(); P != E; ++P) {
        if (!KeyInfoT::isEqual(P->getFirst(), EmptyKey)) {
          if (!KeyInfoT::isEqual(P->getFirst(), TombstoneKey)) {
            P->getSecond().~ValueT();
            --NumEntries;
          }
          P->getFirst() = EmptyKey;
        }
      }
      CHECK_EQ(NumEntries, 0);
    }
    setNumEntries(0);
    setNumTombstones(0);
  }

  /// Return 1 if the specified key is in the map, 0 otherwise.
  size_type count(const KeyT &Key) const {
    const BucketT *TheBucket;
    return LookupBucketFor(Key, TheBucket) ? 1 : 0;
  }

  value_type *find(const KeyT &Key) {
    BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return TheBucket;
    return nullptr;
  }
  const value_type *find(const KeyT &Key) const {
    const BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return TheBucket;
    return nullptr;
  }

  /// Alternate version of find() which allows a different, and possibly
  /// less expensive, key type.
  /// The DenseMapInfo is responsible for supplying methods
  /// getHashValue(LookupKeyT) and isEqual(LookupKeyT, KeyT) for each key
  /// type used.
  template <class LookupKeyT>
  value_type *find_as(const LookupKeyT &Key) {
    BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return TheBucket;
    return nullptr;
  }
  template <class LookupKeyT>
  const value_type *find_as(const LookupKeyT &Key) const {
    const BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return TheBucket;
    return nullptr;
  }

  /// lookup - Return the entry for the specified key, or a default
  /// constructed value if no such entry exists.
  ValueT lookup(const KeyT &Key) const {
    const BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return TheBucket->getSecond();
    return ValueT();
  }

  // Inserts key,value pair into the map if the key isn't already in the map.
  // If the key is already in the map, it returns false and doesn't update the
  // value.
  detail::DenseMapPair<value_type *, bool> insert(const value_type &KV) {
    return try_emplace(KV.first, KV.second);
  }

  // Inserts key,value pair into the map if the key isn't already in the map.
  // If the key is already in the map, it returns false and doesn't update the
  // value.
  detail::DenseMapPair<value_type *, bool> insert(value_type &&KV) {
    return try_emplace(__sanitizer::move(KV.first),
                       __sanitizer::move(KV.second));
  }

  // Inserts key,value pair into the map if the key isn't already in the map.
  // The value is constructed in-place if the key is not in the map, otherwise
  // it is not moved.
  template <typename... Ts>
  detail::DenseMapPair<value_type *, bool> try_emplace(KeyT &&Key,
                                                       Ts &&...Args) {
    BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return {TheBucket, false};  // Already in map.

    // Otherwise, insert the new element.
    TheBucket = InsertIntoBucket(TheBucket, __sanitizer::move(Key),
                                 __sanitizer::forward<Ts>(Args)...);
    return {TheBucket, true};
  }

  // Inserts key,value pair into the map if the key isn't already in the map.
  // The value is constructed in-place if the key is not in the map, otherwise
  // it is not moved.
  template <typename... Ts>
  detail::DenseMapPair<value_type *, bool> try_emplace(const KeyT &Key,
                                                       Ts &&...Args) {
    BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return {TheBucket, false};  // Already in map.

    // Otherwise, insert the new element.
    TheBucket =
        InsertIntoBucket(TheBucket, Key, __sanitizer::forward<Ts>(Args)...);
    return {TheBucket, true};
  }

  /// Alternate version of insert() which allows a different, and possibly
  /// less expensive, key type.
  /// The DenseMapInfo is responsible for supplying methods
  /// getHashValue(LookupKeyT) and isEqual(LookupKeyT, KeyT) for each key
  /// type used.
  template <typename LookupKeyT>
  detail::DenseMapPair<value_type *, bool> insert_as(value_type &&KV,
                                                     const LookupKeyT &Val) {
    BucketT *TheBucket;
    if (LookupBucketFor(Val, TheBucket))
      return {TheBucket, false};  // Already in map.

    // Otherwise, insert the new element.
    TheBucket =
        InsertIntoBucketWithLookup(TheBucket, __sanitizer::move(KV.first),
                                   __sanitizer::move(KV.second), Val);
    return {TheBucket, true};
  }

  bool erase(const KeyT &Val) {
    BucketT *TheBucket;
    if (!LookupBucketFor(Val, TheBucket))
      return false;  // not in map.

    TheBucket->getSecond().~ValueT();
    TheBucket->getFirst() = getTombstoneKey();
    decrementNumEntries();
    incrementNumTombstones();
    return true;
  }

  void erase(value_type *I) {
    CHECK_NE(I, nullptr);
    BucketT *TheBucket = &*I;
    TheBucket->getSecond().~ValueT();
    TheBucket->getFirst() = getTombstoneKey();
    decrementNumEntries();
    incrementNumTombstones();
  }

  value_type &FindAndConstruct(const KeyT &Key) {
    BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return *TheBucket;

    return *InsertIntoBucket(TheBucket, Key);
  }

  ValueT &operator[](const KeyT &Key) { return FindAndConstruct(Key).second; }

  value_type &FindAndConstruct(KeyT &&Key) {
    BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return *TheBucket;

    return *InsertIntoBucket(TheBucket, __sanitizer::move(Key));
  }

  ValueT &operator[](KeyT &&Key) {
    return FindAndConstruct(__sanitizer::move(Key)).second;
  }

  /// Iterate over active entries of the container.
  ///
  /// Function can return fast to stop the process.
  template <class Fn>
  void forEach(Fn fn) {
    const KeyT EmptyKey = getEmptyKey(), TombstoneKey = getTombstoneKey();
    for (auto *P = getBuckets(), *E = getBucketsEnd(); P != E; ++P) {
      const KeyT K = P->getFirst();
      if (!KeyInfoT::isEqual(K, EmptyKey) &&
          !KeyInfoT::isEqual(K, TombstoneKey)) {
        if (!fn(*P))
          return;
      }
    }
  }

  template <class Fn>
  void forEach(Fn fn) const {
    const_cast<DenseMapBase *>(this)->forEach(
        [&](const value_type &KV) { return fn(KV); });
  }

 protected:
  DenseMapBase() = default;

  void destroyAll() {
    if (getNumBuckets() == 0)  // Nothing to do.
      return;

    const KeyT EmptyKey = getEmptyKey(), TombstoneKey = getTombstoneKey();
    for (BucketT *P = getBuckets(), *E = getBucketsEnd(); P != E; ++P) {
      if (!KeyInfoT::isEqual(P->getFirst(), EmptyKey) &&
          !KeyInfoT::isEqual(P->getFirst(), TombstoneKey))
        P->getSecond().~ValueT();
      P->getFirst().~KeyT();
    }
  }

  void initEmpty() {
    setNumEntries(0);
    setNumTombstones(0);

    CHECK_EQ((getNumBuckets() & (getNumBuckets() - 1)), 0);
    const KeyT EmptyKey = getEmptyKey();
    for (BucketT *B = getBuckets(), *E = getBucketsEnd(); B != E; ++B)
      ::new (&B->getFirst()) KeyT(EmptyKey);
  }

  /// Returns the number of buckets to allocate to ensure that the DenseMap can
  /// accommodate \p NumEntries without need to grow().
  unsigned getMinBucketToReserveForEntries(unsigned NumEntries) {
    // Ensure that "NumEntries * 4 < NumBuckets * 3"
    if (NumEntries == 0)
      return 0;
    // +1 is required because of the strict equality.
    // For example if NumEntries is 48, we need to return 401.
    return RoundUpToPowerOfTwo((NumEntries * 4 / 3 + 1) + /* NextPowerOf2 */ 1);
  }

  void moveFromOldBuckets(BucketT *OldBucketsBegin, BucketT *OldBucketsEnd) {
    initEmpty();

    // Insert all the old elements.
    const KeyT EmptyKey = getEmptyKey();
    const KeyT TombstoneKey = getTombstoneKey();
    for (BucketT *B = OldBucketsBegin, *E = OldBucketsEnd; B != E; ++B) {
      if (!KeyInfoT::isEqual(B->getFirst(), EmptyKey) &&
          !KeyInfoT::isEqual(B->getFirst(), TombstoneKey)) {
        // Insert the key/value into the new table.
        BucketT *DestBucket;
        bool FoundVal = LookupBucketFor(B->getFirst(), DestBucket);
        (void)FoundVal;  // silence warning.
        CHECK(!FoundVal);
        DestBucket->getFirst() = __sanitizer::move(B->getFirst());
        ::new (&DestBucket->getSecond())
            ValueT(__sanitizer::move(B->getSecond()));
        incrementNumEntries();

        // Free the value.
        B->getSecond().~ValueT();
      }
      B->getFirst().~KeyT();
    }
  }

  template <typename OtherBaseT>
  void copyFrom(
      const DenseMapBase<OtherBaseT, KeyT, ValueT, KeyInfoT, BucketT> &other) {
    CHECK_NE(&other, this);
    CHECK_EQ(getNumBuckets(), other.getNumBuckets());

    setNumEntries(other.getNumEntries());
    setNumTombstones(other.getNumTombstones());

    if (__sanitizer::is_trivially_copyable<KeyT>::value &&
        __sanitizer::is_trivially_copyable<ValueT>::value)
      internal_memcpy(reinterpret_cast<void *>(getBuckets()),
                      other.getBuckets(), getNumBuckets() * sizeof(BucketT));
    else
      for (uptr i = 0; i < getNumBuckets(); ++i) {
        ::new (&getBuckets()[i].getFirst())
            KeyT(other.getBuckets()[i].getFirst());
        if (!KeyInfoT::isEqual(getBuckets()[i].getFirst(), getEmptyKey()) &&
            !KeyInfoT::isEqual(getBuckets()[i].getFirst(), getTombstoneKey()))
          ::new (&getBuckets()[i].getSecond())
              ValueT(other.getBuckets()[i].getSecond());
      }
  }

  static unsigned getHashValue(const KeyT &Val) {
    return KeyInfoT::getHashValue(Val);
  }

  template <typename LookupKeyT>
  static unsigned getHashValue(const LookupKeyT &Val) {
    return KeyInfoT::getHashValue(Val);
  }

  static const KeyT getEmptyKey() { return KeyInfoT::getEmptyKey(); }

  static const KeyT getTombstoneKey() { return KeyInfoT::getTombstoneKey(); }

 private:
  unsigned getNumEntries() const {
    return static_cast<const DerivedT *>(this)->getNumEntries();
  }

  void setNumEntries(unsigned Num) {
    static_cast<DerivedT *>(this)->setNumEntries(Num);
  }

  void incrementNumEntries() { setNumEntries(getNumEntries() + 1); }

  void decrementNumEntries() { setNumEntries(getNumEntries() - 1); }

  unsigned getNumTombstones() const {
    return static_cast<const DerivedT *>(this)->getNumTombstones();
  }

  void setNumTombstones(unsigned Num) {
    static_cast<DerivedT *>(this)->setNumTombstones(Num);
  }

  void incrementNumTombstones() { setNumTombstones(getNumTombstones() + 1); }

  void decrementNumTombstones() { setNumTombstones(getNumTombstones() - 1); }

  const BucketT *getBuckets() const {
    return static_cast<const DerivedT *>(this)->getBuckets();
  }

  BucketT *getBuckets() { return static_cast<DerivedT *>(this)->getBuckets(); }

  unsigned getNumBuckets() const {
    return static_cast<const DerivedT *>(this)->getNumBuckets();
  }

  BucketT *getBucketsEnd() { return getBuckets() + getNumBuckets(); }

  const BucketT *getBucketsEnd() const {
    return getBuckets() + getNumBuckets();
  }

  void grow(unsigned AtLeast) { static_cast<DerivedT *>(this)->grow(AtLeast); }

  template <typename KeyArg, typename... ValueArgs>
  BucketT *InsertIntoBucket(BucketT *TheBucket, KeyArg &&Key,
                            ValueArgs &&...Values) {
    TheBucket = InsertIntoBucketImpl(Key, Key, TheBucket);

    TheBucket->getFirst() = __sanitizer::forward<KeyArg>(Key);
    ::new (&TheBucket->getSecond())
        ValueT(__sanitizer::forward<ValueArgs>(Values)...);
    return TheBucket;
  }

  template <typename LookupKeyT>
  BucketT *InsertIntoBucketWithLookup(BucketT *TheBucket, KeyT &&Key,
                                      ValueT &&Value, LookupKeyT &Lookup) {
    TheBucket = InsertIntoBucketImpl(Key, Lookup, TheBucket);

    TheBucket->getFirst() = __sanitizer::move(Key);
    ::new (&TheBucket->getSecond()) ValueT(__sanitizer::move(Value));
    return TheBucket;
  }

  template <typename LookupKeyT>
  BucketT *InsertIntoBucketImpl(const KeyT &Key, const LookupKeyT &Lookup,
                                BucketT *TheBucket) {
    // If the load of the hash table is more than 3/4, or if fewer than 1/8 of
    // the buckets are empty (meaning that many are filled with tombstones),
    // grow the table.
    //
    // The later case is tricky.  For example, if we had one empty bucket with
    // tons of tombstones, failing lookups (e.g. for insertion) would have to
    // probe almost the entire table until it found the empty bucket.  If the
    // table completely filled with tombstones, no lookup would ever succeed,
    // causing infinite loops in lookup.
    unsigned NewNumEntries = getNumEntries() + 1;
    unsigned NumBuckets = getNumBuckets();
    if (UNLIKELY(NewNumEntries * 4 >= NumBuckets * 3)) {
      this->grow(NumBuckets * 2);
      LookupBucketFor(Lookup, TheBucket);
      NumBuckets = getNumBuckets();
    } else if (UNLIKELY(NumBuckets - (NewNumEntries + getNumTombstones()) <=
                        NumBuckets / 8)) {
      this->grow(NumBuckets);
      LookupBucketFor(Lookup, TheBucket);
    }
    CHECK(TheBucket);

    // Only update the state after we've grown our bucket space appropriately
    // so that when growing buckets we have self-consistent entry count.
    incrementNumEntries();

    // If we are writing over a tombstone, remember this.
    const KeyT EmptyKey = getEmptyKey();
    if (!KeyInfoT::isEqual(TheBucket->getFirst(), EmptyKey))
      decrementNumTombstones();

    return TheBucket;
  }

  /// LookupBucketFor - Lookup the appropriate bucket for Val, returning it in
  /// FoundBucket.  If the bucket contains the key and a value, this returns
  /// true, otherwise it returns a bucket with an empty marker or tombstone and
  /// returns false.
  template <typename LookupKeyT>
  bool LookupBucketFor(const LookupKeyT &Val,
                       const BucketT *&FoundBucket) const {
    const BucketT *BucketsPtr = getBuckets();
    const unsigned NumBuckets = getNumBuckets();

    if (NumBuckets == 0) {
      FoundBucket = nullptr;
      return false;
    }

    // FoundTombstone - Keep track of whether we find a tombstone while probing.
    const BucketT *FoundTombstone = nullptr;
    const KeyT EmptyKey = getEmptyKey();
    const KeyT TombstoneKey = getTombstoneKey();
    CHECK(!KeyInfoT::isEqual(Val, EmptyKey));
    CHECK(!KeyInfoT::isEqual(Val, TombstoneKey));

    unsigned BucketNo = getHashValue(Val) & (NumBuckets - 1);
    unsigned ProbeAmt = 1;
    while (true) {
      const BucketT *ThisBucket = BucketsPtr + BucketNo;
      // Found Val's bucket?  If so, return it.
      if (LIKELY(KeyInfoT::isEqual(Val, ThisBucket->getFirst()))) {
        FoundBucket = ThisBucket;
        return true;
      }

      // If we found an empty bucket, the key doesn't exist in the set.
      // Insert it and return the default value.
      if (LIKELY(KeyInfoT::isEqual(ThisBucket->getFirst(), EmptyKey))) {
        // If we've already seen a tombstone while probing, fill it in instead
        // of the empty bucket we eventually probed to.
        FoundBucket = FoundTombstone ? FoundTombstone : ThisBucket;
        return false;
      }

      // If this is a tombstone, remember it.  If Val ends up not in the map, we
      // prefer to return it than something that would require more probing.
      if (KeyInfoT::isEqual(ThisBucket->getFirst(), TombstoneKey) &&
          !FoundTombstone)
        FoundTombstone = ThisBucket;  // Remember the first tombstone found.

      // Otherwise, it's a hash collision or a tombstone, continue quadratic
      // probing.
      BucketNo += ProbeAmt++;
      BucketNo &= (NumBuckets - 1);
    }
  }

  template <typename LookupKeyT>
  bool LookupBucketFor(const LookupKeyT &Val, BucketT *&FoundBucket) {
    const BucketT *ConstFoundBucket;
    bool Result = const_cast<const DenseMapBase *>(this)->LookupBucketFor(
        Val, ConstFoundBucket);
    FoundBucket = const_cast<BucketT *>(ConstFoundBucket);
    return Result;
  }

 public:
  /// Return the approximate size (in bytes) of the actual map.
  /// This is just the raw memory used by DenseMap.
  /// If entries are pointers to objects, the size of the referenced objects
  /// are not included.
  uptr getMemorySize() const {
    return RoundUpTo(getNumBuckets() * sizeof(BucketT), GetPageSizeCached());
  }
};

/// Equality comparison for DenseMap.
///
/// Iterates over elements of LHS confirming that each (key, value) pair in LHS
/// is also in RHS, and that no additional pairs are in RHS.
/// Equivalent to N calls to RHS.find and N value comparisons. Amortized
/// complexity is linear, worst case is O(N^2) (if every hash collides).
template <typename DerivedT, typename KeyT, typename ValueT, typename KeyInfoT,
          typename BucketT>
bool operator==(
    const DenseMapBase<DerivedT, KeyT, ValueT, KeyInfoT, BucketT> &LHS,
    const DenseMapBase<DerivedT, KeyT, ValueT, KeyInfoT, BucketT> &RHS) {
  if (LHS.size() != RHS.size())
    return false;

  bool R = true;
  LHS.forEach(
      [&](const typename DenseMapBase<DerivedT, KeyT, ValueT, KeyInfoT,
                                      BucketT>::value_type &KV) -> bool {
        const auto *I = RHS.find(KV.first);
        if (!I || I->second != KV.second) {
          R = false;
          return false;
        }
        return true;
      });

  return R;
}

/// Inequality comparison for DenseMap.
///
/// Equivalent to !(LHS == RHS). See operator== for performance notes.
template <typename DerivedT, typename KeyT, typename ValueT, typename KeyInfoT,
          typename BucketT>
bool operator!=(
    const DenseMapBase<DerivedT, KeyT, ValueT, KeyInfoT, BucketT> &LHS,
    const DenseMapBase<DerivedT, KeyT, ValueT, KeyInfoT, BucketT> &RHS) {
  return !(LHS == RHS);
}

template <typename KeyT, typename ValueT,
          typename KeyInfoT = DenseMapInfo<KeyT>,
          typename BucketT = detail::DenseMapPair<KeyT, ValueT>>
class DenseMap : public DenseMapBase<DenseMap<KeyT, ValueT, KeyInfoT, BucketT>,
                                     KeyT, ValueT, KeyInfoT, BucketT> {
  friend class DenseMapBase<DenseMap, KeyT, ValueT, KeyInfoT, BucketT>;

  // Lift some types from the dependent base class into this class for
  // simplicity of referring to them.
  using BaseT = DenseMapBase<DenseMap, KeyT, ValueT, KeyInfoT, BucketT>;

  BucketT *Buckets = nullptr;
  unsigned NumEntries = 0;
  unsigned NumTombstones = 0;
  unsigned NumBuckets = 0;

 public:
  /// Create a DenseMap with an optional \p InitialReserve that guarantee that
  /// this number of elements can be inserted in the map without grow()
  explicit DenseMap(unsigned InitialReserve) { init(InitialReserve); }
  constexpr DenseMap() = default;

  DenseMap(const DenseMap &other) : BaseT() {
    init(0);
    copyFrom(other);
  }

  DenseMap(DenseMap &&other) : BaseT() {
    init(0);
    swap(other);
  }

  ~DenseMap() {
    this->destroyAll();
    deallocate_buffer(Buckets, sizeof(BucketT) * NumBuckets);
  }

  void swap(DenseMap &RHS) {
    Swap(Buckets, RHS.Buckets);
    Swap(NumEntries, RHS.NumEntries);
    Swap(NumTombstones, RHS.NumTombstones);
    Swap(NumBuckets, RHS.NumBuckets);
  }

  DenseMap &operator=(const DenseMap &other) {
    if (&other != this)
      copyFrom(other);
    return *this;
  }

  DenseMap &operator=(DenseMap &&other) {
    this->destroyAll();
    deallocate_buffer(Buckets, sizeof(BucketT) * NumBuckets, alignof(BucketT));
    init(0);
    swap(other);
    return *this;
  }

  void copyFrom(const DenseMap &other) {
    this->destroyAll();
    deallocate_buffer(Buckets, sizeof(BucketT) * NumBuckets);
    if (allocateBuckets(other.NumBuckets)) {
      this->BaseT::copyFrom(other);
    } else {
      NumEntries = 0;
      NumTombstones = 0;
    }
  }

  void init(unsigned InitNumEntries) {
    auto InitBuckets = BaseT::getMinBucketToReserveForEntries(InitNumEntries);
    if (allocateBuckets(InitBuckets)) {
      this->BaseT::initEmpty();
    } else {
      NumEntries = 0;
      NumTombstones = 0;
    }
  }

  void grow(unsigned AtLeast) {
    unsigned OldNumBuckets = NumBuckets;
    BucketT *OldBuckets = Buckets;

    allocateBuckets(RoundUpToPowerOfTwo(Max<unsigned>(64, AtLeast)));
    CHECK(Buckets);
    if (!OldBuckets) {
      this->BaseT::initEmpty();
      return;
    }

    this->moveFromOldBuckets(OldBuckets, OldBuckets + OldNumBuckets);

    // Free the old table.
    deallocate_buffer(OldBuckets, sizeof(BucketT) * OldNumBuckets);
  }

 private:
  unsigned getNumEntries() const { return NumEntries; }

  void setNumEntries(unsigned Num) { NumEntries = Num; }

  unsigned getNumTombstones() const { return NumTombstones; }

  void setNumTombstones(unsigned Num) { NumTombstones = Num; }

  BucketT *getBuckets() const { return Buckets; }

  unsigned getNumBuckets() const { return NumBuckets; }

  bool allocateBuckets(unsigned Num) {
    NumBuckets = Num;
    if (NumBuckets == 0) {
      Buckets = nullptr;
      return false;
    }

    uptr Size = sizeof(BucketT) * NumBuckets;
    if (Size * 2 <= GetPageSizeCached()) {
      // We always allocate at least a page, so use entire space.
      unsigned Log2 = MostSignificantSetBitIndex(GetPageSizeCached() / Size);
      Size <<= Log2;
      NumBuckets <<= Log2;
      CHECK_EQ(Size, sizeof(BucketT) * NumBuckets);
      CHECK_GT(Size * 2, GetPageSizeCached());
    }
    Buckets = static_cast<BucketT *>(allocate_buffer(Size));
    return true;
  }

  static void *allocate_buffer(uptr Size) {
    return MmapOrDie(RoundUpTo(Size, GetPageSizeCached()), "DenseMap");
  }

  static void deallocate_buffer(void *Ptr, uptr Size) {
    UnmapOrDie(Ptr, RoundUpTo(Size, GetPageSizeCached()));
  }
};

}  // namespace __sanitizer

#endif  // SANITIZER_DENSE_MAP_H
