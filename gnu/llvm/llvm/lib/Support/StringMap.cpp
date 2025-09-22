//===--- StringMap.cpp - String Hash table map implementation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the StringMap class.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ReverseIteration.h"
#include "llvm/Support/xxhash.h"

using namespace llvm;

/// Returns the number of buckets to allocate to ensure that the DenseMap can
/// accommodate \p NumEntries without need to grow().
static inline unsigned getMinBucketToReserveForEntries(unsigned NumEntries) {
  // Ensure that "NumEntries * 4 < NumBuckets * 3"
  if (NumEntries == 0)
    return 0;
  // +1 is required because of the strict equality.
  // For example if NumEntries is 48, we need to return 401.
  return NextPowerOf2(NumEntries * 4 / 3 + 1);
}

static inline StringMapEntryBase **createTable(unsigned NewNumBuckets) {
  auto **Table = static_cast<StringMapEntryBase **>(safe_calloc(
      NewNumBuckets + 1, sizeof(StringMapEntryBase **) + sizeof(unsigned)));

  // Allocate one extra bucket, set it to look filled so the iterators stop at
  // end.
  Table[NewNumBuckets] = (StringMapEntryBase *)2;
  return Table;
}

static inline unsigned *getHashTable(StringMapEntryBase **TheTable,
                                     unsigned NumBuckets) {
  return reinterpret_cast<unsigned *>(TheTable + NumBuckets + 1);
}

uint32_t StringMapImpl::hash(StringRef Key) { return xxh3_64bits(Key); }

StringMapImpl::StringMapImpl(unsigned InitSize, unsigned itemSize) {
  ItemSize = itemSize;

  // If a size is specified, initialize the table with that many buckets.
  if (InitSize) {
    // The table will grow when the number of entries reach 3/4 of the number of
    // buckets. To guarantee that "InitSize" number of entries can be inserted
    // in the table without growing, we allocate just what is needed here.
    init(getMinBucketToReserveForEntries(InitSize));
    return;
  }

  // Otherwise, initialize it with zero buckets to avoid the allocation.
  TheTable = nullptr;
  NumBuckets = 0;
  NumItems = 0;
  NumTombstones = 0;
}

void StringMapImpl::init(unsigned InitSize) {
  assert((InitSize & (InitSize - 1)) == 0 &&
         "Init Size must be a power of 2 or zero!");

  unsigned NewNumBuckets = InitSize ? InitSize : 16;
  NumItems = 0;
  NumTombstones = 0;

  TheTable = createTable(NewNumBuckets);

  // Set the member only if TheTable was successfully allocated
  NumBuckets = NewNumBuckets;
}

/// LookupBucketFor - Look up the bucket that the specified string should end
/// up in.  If it already exists as a key in the map, the Item pointer for the
/// specified bucket will be non-null.  Otherwise, it will be null.  In either
/// case, the FullHashValue field of the bucket will be set to the hash value
/// of the string.
unsigned StringMapImpl::LookupBucketFor(StringRef Name,
                                        uint32_t FullHashValue) {
#ifdef EXPENSIVE_CHECKS
  assert(FullHashValue == hash(Name));
#endif
  // Hash table unallocated so far?
  if (NumBuckets == 0)
    init(16);
  if (shouldReverseIterate())
    FullHashValue = ~FullHashValue;
  unsigned BucketNo = FullHashValue & (NumBuckets - 1);
  unsigned *HashTable = getHashTable(TheTable, NumBuckets);

  unsigned ProbeAmt = 1;
  int FirstTombstone = -1;
  while (true) {
    StringMapEntryBase *BucketItem = TheTable[BucketNo];
    // If we found an empty bucket, this key isn't in the table yet, return it.
    if (LLVM_LIKELY(!BucketItem)) {
      // If we found a tombstone, we want to reuse the tombstone instead of an
      // empty bucket.  This reduces probing.
      if (FirstTombstone != -1) {
        HashTable[FirstTombstone] = FullHashValue;
        return FirstTombstone;
      }

      HashTable[BucketNo] = FullHashValue;
      return BucketNo;
    }

    if (BucketItem == getTombstoneVal()) {
      // Skip over tombstones.  However, remember the first one we see.
      if (FirstTombstone == -1)
        FirstTombstone = BucketNo;
    } else if (LLVM_LIKELY(HashTable[BucketNo] == FullHashValue)) {
      // If the full hash value matches, check deeply for a match.  The common
      // case here is that we are only looking at the buckets (for item info
      // being non-null and for the full hash value) not at the items.  This
      // is important for cache locality.

      // Do the comparison like this because Name isn't necessarily
      // null-terminated!
      char *ItemStr = (char *)BucketItem + ItemSize;
      if (Name == StringRef(ItemStr, BucketItem->getKeyLength())) {
        // We found a match!
        return BucketNo;
      }
    }

    // Okay, we didn't find the item.  Probe to the next bucket.
    BucketNo = (BucketNo + ProbeAmt) & (NumBuckets - 1);

    // Use quadratic probing, it has fewer clumping artifacts than linear
    // probing and has good cache behavior in the common case.
    ++ProbeAmt;
  }
}

/// FindKey - Look up the bucket that contains the specified key. If it exists
/// in the map, return the bucket number of the key.  Otherwise return -1.
/// This does not modify the map.
int StringMapImpl::FindKey(StringRef Key, uint32_t FullHashValue) const {
  if (NumBuckets == 0)
    return -1; // Really empty table?
#ifdef EXPENSIVE_CHECKS
  assert(FullHashValue == hash(Key));
#endif
  if (shouldReverseIterate())
    FullHashValue = ~FullHashValue;
  unsigned BucketNo = FullHashValue & (NumBuckets - 1);
  unsigned *HashTable = getHashTable(TheTable, NumBuckets);

  unsigned ProbeAmt = 1;
  while (true) {
    StringMapEntryBase *BucketItem = TheTable[BucketNo];
    // If we found an empty bucket, this key isn't in the table yet, return.
    if (LLVM_LIKELY(!BucketItem))
      return -1;

    if (BucketItem == getTombstoneVal()) {
      // Ignore tombstones.
    } else if (LLVM_LIKELY(HashTable[BucketNo] == FullHashValue)) {
      // If the full hash value matches, check deeply for a match.  The common
      // case here is that we are only looking at the buckets (for item info
      // being non-null and for the full hash value) not at the items.  This
      // is important for cache locality.

      // Do the comparison like this because NameStart isn't necessarily
      // null-terminated!
      char *ItemStr = (char *)BucketItem + ItemSize;
      if (Key == StringRef(ItemStr, BucketItem->getKeyLength())) {
        // We found a match!
        return BucketNo;
      }
    }

    // Okay, we didn't find the item.  Probe to the next bucket.
    BucketNo = (BucketNo + ProbeAmt) & (NumBuckets - 1);

    // Use quadratic probing, it has fewer clumping artifacts than linear
    // probing and has good cache behavior in the common case.
    ++ProbeAmt;
  }
}

/// RemoveKey - Remove the specified StringMapEntry from the table, but do not
/// delete it.  This aborts if the value isn't in the table.
void StringMapImpl::RemoveKey(StringMapEntryBase *V) {
  const char *VStr = (char *)V + ItemSize;
  StringMapEntryBase *V2 = RemoveKey(StringRef(VStr, V->getKeyLength()));
  (void)V2;
  assert(V == V2 && "Didn't find key?");
}

/// RemoveKey - Remove the StringMapEntry for the specified key from the
/// table, returning it.  If the key is not in the table, this returns null.
StringMapEntryBase *StringMapImpl::RemoveKey(StringRef Key) {
  int Bucket = FindKey(Key);
  if (Bucket == -1)
    return nullptr;

  StringMapEntryBase *Result = TheTable[Bucket];
  TheTable[Bucket] = getTombstoneVal();
  --NumItems;
  ++NumTombstones;
  assert(NumItems + NumTombstones <= NumBuckets);

  return Result;
}

/// RehashTable - Grow the table, redistributing values into the buckets with
/// the appropriate mod-of-hashtable-size.
unsigned StringMapImpl::RehashTable(unsigned BucketNo) {
  unsigned NewSize;
  // If the hash table is now more than 3/4 full, or if fewer than 1/8 of
  // the buckets are empty (meaning that many are filled with tombstones),
  // grow/rehash the table.
  if (LLVM_UNLIKELY(NumItems * 4 > NumBuckets * 3)) {
    NewSize = NumBuckets * 2;
  } else if (LLVM_UNLIKELY(NumBuckets - (NumItems + NumTombstones) <=
                           NumBuckets / 8)) {
    NewSize = NumBuckets;
  } else {
    return BucketNo;
  }

  unsigned NewBucketNo = BucketNo;
  auto **NewTableArray = createTable(NewSize);
  unsigned *NewHashArray = getHashTable(NewTableArray, NewSize);
  unsigned *HashTable = getHashTable(TheTable, NumBuckets);

  // Rehash all the items into their new buckets.  Luckily :) we already have
  // the hash values available, so we don't have to rehash any strings.
  for (unsigned I = 0, E = NumBuckets; I != E; ++I) {
    StringMapEntryBase *Bucket = TheTable[I];
    if (Bucket && Bucket != getTombstoneVal()) {
      // If the bucket is not available, probe for a spot.
      unsigned FullHash = HashTable[I];
      unsigned NewBucket = FullHash & (NewSize - 1);
      if (NewTableArray[NewBucket]) {
        unsigned ProbeSize = 1;
        do {
          NewBucket = (NewBucket + ProbeSize++) & (NewSize - 1);
        } while (NewTableArray[NewBucket]);
      }

      // Finally found a slot.  Fill it in.
      NewTableArray[NewBucket] = Bucket;
      NewHashArray[NewBucket] = FullHash;
      if (I == BucketNo)
        NewBucketNo = NewBucket;
    }
  }

  free(TheTable);

  TheTable = NewTableArray;
  NumBuckets = NewSize;
  NumTombstones = 0;
  return NewBucketNo;
}
