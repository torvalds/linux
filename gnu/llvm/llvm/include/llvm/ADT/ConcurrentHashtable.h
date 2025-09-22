//===- ConcurrentHashtable.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_CONCURRENTHASHTABLE_H
#define LLVM_ADT_CONCURRENTHASHTABLE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/xxhash.h"
#include <atomic>
#include <cstddef>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <type_traits>

namespace llvm {

/// ConcurrentHashTable - is a resizeable concurrent hashtable.
/// The number of resizings limited up to x2^31. This hashtable is
/// useful to have efficient access to aggregate data(like strings,
/// type descriptors...) and to keep only single copy of such
/// an aggregate. The hashtable allows only concurrent insertions:
///
/// KeyDataTy* = insert ( const KeyTy& );
///
/// Data structure:
///
/// Inserted value KeyTy is mapped to 64-bit hash value ->
///
///          [------- 64-bit Hash value --------]
///          [  StartEntryIndex ][ Bucket Index ]
///                    |                |
///              points to the     points to
///              first probe       the bucket.
///              position inside
///              bucket entries
///
/// After initialization, all buckets have an initial size. During insertions,
/// buckets might be extended to contain more entries. Each bucket can be
/// independently resized and rehashed(no need to lock the whole table).
/// Different buckets may have different sizes. If the single bucket is full
/// then the bucket is resized.
///
/// BucketsArray keeps all buckets. Each bucket keeps an array of Entries
/// (pointers to KeyDataTy) and another array of entries hashes:
///
/// BucketsArray[BucketIdx].Hashes[EntryIdx]:
/// BucketsArray[BucketIdx].Entries[EntryIdx]:
///
/// [Bucket 0].Hashes -> [uint32_t][uint32_t]
/// [Bucket 0].Entries -> [KeyDataTy*][KeyDataTy*]
///
/// [Bucket 1].Hashes -> [uint32_t][uint32_t][uint32_t][uint32_t]
/// [Bucket 1].Entries -> [KeyDataTy*][KeyDataTy*][KeyDataTy*][KeyDataTy*]
///                      .........................
/// [Bucket N].Hashes -> [uint32_t][uint32_t][uint32_t]
/// [Bucket N].Entries -> [KeyDataTy*][KeyDataTy*][KeyDataTy*]
///
/// ConcurrentHashTableByPtr uses an external thread-safe allocator to allocate
/// KeyDataTy items.

template <typename KeyTy, typename KeyDataTy, typename AllocatorTy>
class ConcurrentHashTableInfoByPtr {
public:
  /// \returns Hash value for the specified \p Key.
  static inline uint64_t getHashValue(const KeyTy &Key) {
    return xxh3_64bits(Key);
  }

  /// \returns true if both \p LHS and \p RHS are equal.
  static inline bool isEqual(const KeyTy &LHS, const KeyTy &RHS) {
    return LHS == RHS;
  }

  /// \returns key for the specified \p KeyData.
  static inline const KeyTy &getKey(const KeyDataTy &KeyData) {
    return KeyData.getKey();
  }

  /// \returns newly created object of KeyDataTy type.
  static inline KeyDataTy *create(const KeyTy &Key, AllocatorTy &Allocator) {
    return KeyDataTy::create(Key, Allocator);
  }
};

template <typename KeyTy, typename KeyDataTy, typename AllocatorTy,
          typename Info =
              ConcurrentHashTableInfoByPtr<KeyTy, KeyDataTy, AllocatorTy>>
class ConcurrentHashTableByPtr {
public:
  ConcurrentHashTableByPtr(
      AllocatorTy &Allocator, uint64_t EstimatedSize = 100000,
      size_t ThreadsNum = parallel::strategy.compute_thread_count(),
      size_t InitialNumberOfBuckets = 128)
      : MultiThreadAllocator(Allocator) {
    assert((ThreadsNum > 0) && "ThreadsNum must be greater than 0");
    assert((InitialNumberOfBuckets > 0) &&
           "InitialNumberOfBuckets must be greater than 0");

    // Calculate number of buckets.
    uint64_t EstimatedNumberOfBuckets = ThreadsNum;
    if (ThreadsNum > 1) {
      EstimatedNumberOfBuckets *= InitialNumberOfBuckets;
      EstimatedNumberOfBuckets *= std::max(
          1,
          countr_zero(PowerOf2Ceil(EstimatedSize / InitialNumberOfBuckets)) >>
              2);
    }
    EstimatedNumberOfBuckets = PowerOf2Ceil(EstimatedNumberOfBuckets);
    NumberOfBuckets =
        std::min(EstimatedNumberOfBuckets, (uint64_t)(1Ull << 31));

    // Allocate buckets.
    BucketsArray = std::make_unique<Bucket[]>(NumberOfBuckets);

    InitialBucketSize = EstimatedSize / NumberOfBuckets;
    InitialBucketSize = std::max((uint32_t)1, InitialBucketSize);
    InitialBucketSize = PowerOf2Ceil(InitialBucketSize);

    // Initialize each bucket.
    for (uint32_t Idx = 0; Idx < NumberOfBuckets; Idx++) {
      HashesPtr Hashes = new ExtHashBitsTy[InitialBucketSize];
      memset(Hashes, 0, sizeof(ExtHashBitsTy) * InitialBucketSize);

      DataPtr Entries = new EntryDataTy[InitialBucketSize];
      memset(Entries, 0, sizeof(EntryDataTy) * InitialBucketSize);

      BucketsArray[Idx].Size = InitialBucketSize;
      BucketsArray[Idx].Hashes = Hashes;
      BucketsArray[Idx].Entries = Entries;
    }

    // Calculate masks.
    HashMask = NumberOfBuckets - 1;

    size_t LeadingZerosNumber = countl_zero(HashMask);
    HashBitsNum = 64 - LeadingZerosNumber;

    // We keep only high 32-bits of hash value. So bucket size cannot
    // exceed 2^31. Bucket size is always power of two.
    MaxBucketSize = 1Ull << (std::min((size_t)31, LeadingZerosNumber));

    // Calculate mask for extended hash bits.
    ExtHashMask = (uint64_t)NumberOfBuckets * MaxBucketSize - 1;
  }

  virtual ~ConcurrentHashTableByPtr() {
    // Deallocate buckets.
    for (uint32_t Idx = 0; Idx < NumberOfBuckets; Idx++) {
      delete[] BucketsArray[Idx].Hashes;
      delete[] BucketsArray[Idx].Entries;
    }
  }

  /// Insert new value \p NewValue or return already existing entry.
  ///
  /// \returns entry and "true" if an entry is just inserted or
  /// "false" if an entry already exists.
  std::pair<KeyDataTy *, bool> insert(const KeyTy &NewValue) {
    // Calculate bucket index.
    uint64_t Hash = Info::getHashValue(NewValue);
    Bucket &CurBucket = BucketsArray[getBucketIdx(Hash)];
    uint32_t ExtHashBits = getExtHashBits(Hash);

#if LLVM_ENABLE_THREADS
    // Lock bucket.
    CurBucket.Guard.lock();
#endif

    HashesPtr BucketHashes = CurBucket.Hashes;
    DataPtr BucketEntries = CurBucket.Entries;
    uint32_t CurEntryIdx = getStartIdx(ExtHashBits, CurBucket.Size);

    while (true) {
      uint32_t CurEntryHashBits = BucketHashes[CurEntryIdx];

      if (CurEntryHashBits == 0 && BucketEntries[CurEntryIdx] == nullptr) {
        // Found empty slot. Insert data.
        KeyDataTy *NewData = Info::create(NewValue, MultiThreadAllocator);
        BucketEntries[CurEntryIdx] = NewData;
        BucketHashes[CurEntryIdx] = ExtHashBits;

        CurBucket.NumberOfEntries++;
        RehashBucket(CurBucket);

#if LLVM_ENABLE_THREADS
        CurBucket.Guard.unlock();
#endif

        return {NewData, true};
      }

      if (CurEntryHashBits == ExtHashBits) {
        // Hash matched. Check value for equality.
        KeyDataTy *EntryData = BucketEntries[CurEntryIdx];
        if (Info::isEqual(Info::getKey(*EntryData), NewValue)) {
          // Already existed entry matched with inserted data is found.
#if LLVM_ENABLE_THREADS
          CurBucket.Guard.unlock();
#endif

          return {EntryData, false};
        }
      }

      CurEntryIdx++;
      CurEntryIdx &= (CurBucket.Size - 1);
    }

    llvm_unreachable("Insertion error.");
    return {};
  }

  /// Print information about current state of hash table structures.
  void printStatistic(raw_ostream &OS) {
    OS << "\n--- HashTable statistic:\n";
    OS << "\nNumber of buckets = " << NumberOfBuckets;
    OS << "\nInitial bucket size = " << InitialBucketSize;

    uint64_t NumberOfNonEmptyBuckets = 0;
    uint64_t NumberOfEntriesPlusEmpty = 0;
    uint64_t OverallNumberOfEntries = 0;
    uint64_t OverallSize = sizeof(*this) + NumberOfBuckets * sizeof(Bucket);

    DenseMap<uint32_t, uint32_t> BucketSizesMap;

    // For each bucket...
    for (uint32_t Idx = 0; Idx < NumberOfBuckets; Idx++) {
      Bucket &CurBucket = BucketsArray[Idx];

      BucketSizesMap[CurBucket.Size]++;

      if (CurBucket.NumberOfEntries != 0)
        NumberOfNonEmptyBuckets++;
      NumberOfEntriesPlusEmpty += CurBucket.Size;
      OverallNumberOfEntries += CurBucket.NumberOfEntries;
      OverallSize +=
          (sizeof(ExtHashBitsTy) + sizeof(EntryDataTy)) * CurBucket.Size;
    }

    OS << "\nOverall number of entries = " << OverallNumberOfEntries;
    OS << "\nOverall number of non empty buckets = " << NumberOfNonEmptyBuckets;
    for (auto &BucketSize : BucketSizesMap)
      OS << "\n Number of buckets with size " << BucketSize.first << ": "
         << BucketSize.second;

    std::stringstream stream;
    stream << std::fixed << std::setprecision(2)
           << ((float)OverallNumberOfEntries / (float)NumberOfEntriesPlusEmpty);
    std::string str = stream.str();

    OS << "\nLoad factor = " << str;
    OS << "\nOverall allocated size = " << OverallSize;
  }

protected:
  using ExtHashBitsTy = uint32_t;
  using EntryDataTy = KeyDataTy *;

  using HashesPtr = ExtHashBitsTy *;
  using DataPtr = EntryDataTy *;

  // Bucket structure. Keeps bucket data.
  struct Bucket {
    Bucket() = default;

    // Size of bucket.
    uint32_t Size = 0;

    // Number of non-null entries.
    uint32_t NumberOfEntries = 0;

    // Hashes for [Size] entries.
    HashesPtr Hashes = nullptr;

    // [Size] entries.
    DataPtr Entries = nullptr;

#if LLVM_ENABLE_THREADS
    // Mutex for this bucket.
    std::mutex Guard;
#endif
  };

  // Reallocate and rehash bucket if this is full enough.
  void RehashBucket(Bucket &CurBucket) {
    assert((CurBucket.Size > 0) && "Uninitialised bucket");
    if (CurBucket.NumberOfEntries < CurBucket.Size * 0.9)
      return;

    if (CurBucket.Size >= MaxBucketSize)
      report_fatal_error("ConcurrentHashTable is full");

    uint32_t NewBucketSize = CurBucket.Size << 1;
    assert((NewBucketSize <= MaxBucketSize) && "New bucket size is too big");
    assert((CurBucket.Size < NewBucketSize) &&
           "New bucket size less than size of current bucket");

    // Store old entries & hashes arrays.
    HashesPtr SrcHashes = CurBucket.Hashes;
    DataPtr SrcEntries = CurBucket.Entries;

    // Allocate new entries&hashes arrays.
    HashesPtr DestHashes = new ExtHashBitsTy[NewBucketSize];
    memset(DestHashes, 0, sizeof(ExtHashBitsTy) * NewBucketSize);

    DataPtr DestEntries = new EntryDataTy[NewBucketSize];
    memset(DestEntries, 0, sizeof(EntryDataTy) * NewBucketSize);

    // For each entry in source arrays...
    for (uint32_t CurSrcEntryIdx = 0; CurSrcEntryIdx < CurBucket.Size;
         CurSrcEntryIdx++) {
      uint32_t CurSrcEntryHashBits = SrcHashes[CurSrcEntryIdx];

      // Check for null entry.
      if (CurSrcEntryHashBits == 0 && SrcEntries[CurSrcEntryIdx] == nullptr)
        continue;

      uint32_t StartDestIdx = getStartIdx(CurSrcEntryHashBits, NewBucketSize);

      // Insert non-null entry into the new arrays.
      while (true) {
        uint32_t CurDestEntryHashBits = DestHashes[StartDestIdx];

        if (CurDestEntryHashBits == 0 && DestEntries[StartDestIdx] == nullptr) {
          // Found empty slot. Insert data.
          DestHashes[StartDestIdx] = CurSrcEntryHashBits;
          DestEntries[StartDestIdx] = SrcEntries[CurSrcEntryIdx];
          break;
        }

        StartDestIdx++;
        StartDestIdx = StartDestIdx & (NewBucketSize - 1);
      }
    }

    // Update bucket fields.
    CurBucket.Hashes = DestHashes;
    CurBucket.Entries = DestEntries;
    CurBucket.Size = NewBucketSize;

    // Delete old bucket entries.
    if (SrcHashes != nullptr)
      delete[] SrcHashes;
    if (SrcEntries != nullptr)
      delete[] SrcEntries;
  }

  uint32_t getBucketIdx(hash_code Hash) { return Hash & HashMask; }

  uint32_t getExtHashBits(uint64_t Hash) {
    return (Hash & ExtHashMask) >> HashBitsNum;
  }

  uint32_t getStartIdx(uint32_t ExtHashBits, uint32_t BucketSize) {
    assert((BucketSize > 0) && "Empty bucket");

    return ExtHashBits & (BucketSize - 1);
  }

  // Number of bits in hash mask.
  uint64_t HashBitsNum = 0;

  // Hash mask.
  uint64_t HashMask = 0;

  // Hash mask for the extended hash bits.
  uint64_t ExtHashMask = 0;

  // The maximal bucket size.
  uint32_t MaxBucketSize = 0;

  // Initial size of bucket.
  uint32_t InitialBucketSize = 0;

  // The number of buckets.
  uint32_t NumberOfBuckets = 0;

  // Array of buckets.
  std::unique_ptr<Bucket[]> BucketsArray;

  // Used for allocating KeyDataTy values.
  AllocatorTy &MultiThreadAllocator;
};

} // end namespace llvm

#endif // LLVM_ADT_CONCURRENTHASHTABLE_H
