//===--- OnDiskHashTable.h - On-Disk Hash Table Implementation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines facilities for reading and writing on-disk hash tables.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_SUPPORT_ONDISKHASHTABLE_H
#define LLVM_SUPPORT_ONDISKHASHTABLE_H

#include "llvm/Support/Alignment.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdlib>

namespace llvm {

/// Generates an on disk hash table.
///
/// This needs an \c Info that handles storing values into the hash table's
/// payload and computes the hash for a given key. This should provide the
/// following interface:
///
/// \code
/// class ExampleInfo {
/// public:
///   typedef ExampleKey key_type;   // Must be copy constructible
///   typedef ExampleKey &key_type_ref;
///   typedef ExampleData data_type; // Must be copy constructible
///   typedef ExampleData &data_type_ref;
///   typedef uint32_t hash_value_type; // The type the hash function returns.
///   typedef uint32_t offset_type; // The type for offsets into the table.
///
///   /// Calculate the hash for Key
///   static hash_value_type ComputeHash(key_type_ref Key);
///   /// Return the lengths, in bytes, of the given Key/Data pair.
///   static std::pair<offset_type, offset_type>
///   EmitKeyDataLength(raw_ostream &Out, key_type_ref Key, data_type_ref Data);
///   /// Write Key to Out.  KeyLen is the length from EmitKeyDataLength.
///   static void EmitKey(raw_ostream &Out, key_type_ref Key,
///                       offset_type KeyLen);
///   /// Write Data to Out.  DataLen is the length from EmitKeyDataLength.
///   static void EmitData(raw_ostream &Out, key_type_ref Key,
///                        data_type_ref Data, offset_type DataLen);
///   /// Determine if two keys are equal. Optional, only needed by contains.
///   static bool EqualKey(key_type_ref Key1, key_type_ref Key2);
/// };
/// \endcode
template <typename Info> class OnDiskChainedHashTableGenerator {
  /// A single item in the hash table.
  class Item {
  public:
    typename Info::key_type Key;
    typename Info::data_type Data;
    Item *Next;
    const typename Info::hash_value_type Hash;

    Item(typename Info::key_type_ref Key, typename Info::data_type_ref Data,
         Info &InfoObj)
        : Key(Key), Data(Data), Next(nullptr), Hash(InfoObj.ComputeHash(Key)) {}
  };

  typedef typename Info::offset_type offset_type;
  offset_type NumBuckets;
  offset_type NumEntries;
  llvm::SpecificBumpPtrAllocator<Item> BA;

  /// A linked list of values in a particular hash bucket.
  struct Bucket {
    offset_type Off;
    unsigned Length;
    Item *Head;
  };

  Bucket *Buckets;

private:
  /// Insert an item into the appropriate hash bucket.
  void insert(Bucket *Buckets, size_t Size, Item *E) {
    Bucket &B = Buckets[E->Hash & (Size - 1)];
    E->Next = B.Head;
    ++B.Length;
    B.Head = E;
  }

  /// Resize the hash table, moving the old entries into the new buckets.
  void resize(size_t NewSize) {
    Bucket *NewBuckets = static_cast<Bucket *>(
        safe_calloc(NewSize, sizeof(Bucket)));
    // Populate NewBuckets with the old entries.
    for (size_t I = 0; I < NumBuckets; ++I)
      for (Item *E = Buckets[I].Head; E;) {
        Item *N = E->Next;
        E->Next = nullptr;
        insert(NewBuckets, NewSize, E);
        E = N;
      }

    free(Buckets);
    NumBuckets = NewSize;
    Buckets = NewBuckets;
  }

public:
  /// Insert an entry into the table.
  void insert(typename Info::key_type_ref Key,
              typename Info::data_type_ref Data) {
    Info InfoObj;
    insert(Key, Data, InfoObj);
  }

  /// Insert an entry into the table.
  ///
  /// Uses the provided Info instead of a stack allocated one.
  void insert(typename Info::key_type_ref Key,
              typename Info::data_type_ref Data, Info &InfoObj) {
    ++NumEntries;
    if (4 * NumEntries >= 3 * NumBuckets)
      resize(NumBuckets * 2);
    insert(Buckets, NumBuckets, new (BA.Allocate()) Item(Key, Data, InfoObj));
  }

  /// Determine whether an entry has been inserted.
  bool contains(typename Info::key_type_ref Key, Info &InfoObj) {
    unsigned Hash = InfoObj.ComputeHash(Key);
    for (Item *I = Buckets[Hash & (NumBuckets - 1)].Head; I; I = I->Next)
      if (I->Hash == Hash && InfoObj.EqualKey(I->Key, Key))
        return true;
    return false;
  }

  /// Emit the table to Out, which must not be at offset 0.
  offset_type Emit(raw_ostream &Out) {
    Info InfoObj;
    return Emit(Out, InfoObj);
  }

  /// Emit the table to Out, which must not be at offset 0.
  ///
  /// Uses the provided Info instead of a stack allocated one.
  offset_type Emit(raw_ostream &Out, Info &InfoObj) {
    using namespace llvm::support;
    endian::Writer LE(Out, llvm::endianness::little);

    // Now we're done adding entries, resize the bucket list if it's
    // significantly too large. (This only happens if the number of
    // entries is small and we're within our initial allocation of
    // 64 buckets.) We aim for an occupancy ratio in [3/8, 3/4).
    //
    // As a special case, if there are two or fewer entries, just
    // form a single bucket. A linear scan is fine in that case, and
    // this is very common in C++ class lookup tables. This also
    // guarantees we produce at least one bucket for an empty table.
    //
    // FIXME: Try computing a perfect hash function at this point.
    unsigned TargetNumBuckets =
        NumEntries <= 2 ? 1 : llvm::bit_ceil(NumEntries * 4 / 3 + 1);
    if (TargetNumBuckets != NumBuckets)
      resize(TargetNumBuckets);

    // Emit the payload of the table.
    for (offset_type I = 0; I < NumBuckets; ++I) {
      Bucket &B = Buckets[I];
      if (!B.Head)
        continue;

      // Store the offset for the data of this bucket.
      B.Off = Out.tell();
      assert(B.Off && "Cannot write a bucket at offset 0. Please add padding.");

      // Write out the number of items in the bucket.
      LE.write<uint16_t>(B.Length);
      assert(B.Length != 0 && "Bucket has a head but zero length?");

      // Write out the entries in the bucket.
      for (Item *I = B.Head; I; I = I->Next) {
        LE.write<typename Info::hash_value_type>(I->Hash);
        const std::pair<offset_type, offset_type> &Len =
            InfoObj.EmitKeyDataLength(Out, I->Key, I->Data);
#ifdef NDEBUG
        InfoObj.EmitKey(Out, I->Key, Len.first);
        InfoObj.EmitData(Out, I->Key, I->Data, Len.second);
#else
        // In asserts mode, check that the users length matches the data they
        // wrote.
        uint64_t KeyStart = Out.tell();
        InfoObj.EmitKey(Out, I->Key, Len.first);
        uint64_t DataStart = Out.tell();
        InfoObj.EmitData(Out, I->Key, I->Data, Len.second);
        uint64_t End = Out.tell();
        assert(offset_type(DataStart - KeyStart) == Len.first &&
               "key length does not match bytes written");
        assert(offset_type(End - DataStart) == Len.second &&
               "data length does not match bytes written");
#endif
      }
    }

    // Pad with zeros so that we can start the hashtable at an aligned address.
    offset_type TableOff = Out.tell();
    uint64_t N = offsetToAlignment(TableOff, Align(alignof(offset_type)));
    TableOff += N;
    while (N--)
      LE.write<uint8_t>(0);

    // Emit the hashtable itself.
    LE.write<offset_type>(NumBuckets);
    LE.write<offset_type>(NumEntries);
    for (offset_type I = 0; I < NumBuckets; ++I)
      LE.write<offset_type>(Buckets[I].Off);

    return TableOff;
  }

  OnDiskChainedHashTableGenerator() {
    NumEntries = 0;
    NumBuckets = 64;
    // Note that we do not need to run the constructors of the individual
    // Bucket objects since 'calloc' returns bytes that are all 0.
    Buckets = static_cast<Bucket *>(safe_calloc(NumBuckets, sizeof(Bucket)));
  }

  ~OnDiskChainedHashTableGenerator() { std::free(Buckets); }
};

/// Provides lookup on an on disk hash table.
///
/// This needs an \c Info that handles reading values from the hash table's
/// payload and computes the hash for a given key. This should provide the
/// following interface:
///
/// \code
/// class ExampleLookupInfo {
/// public:
///   typedef ExampleData data_type;
///   typedef ExampleInternalKey internal_key_type; // The stored key type.
///   typedef ExampleKey external_key_type; // The type to pass to find().
///   typedef uint32_t hash_value_type; // The type the hash function returns.
///   typedef uint32_t offset_type; // The type for offsets into the table.
///
///   /// Compare two keys for equality.
///   static bool EqualKey(internal_key_type &Key1, internal_key_type &Key2);
///   /// Calculate the hash for the given key.
///   static hash_value_type ComputeHash(internal_key_type &IKey);
///   /// Translate from the semantic type of a key in the hash table to the
///   /// type that is actually stored and used for hashing and comparisons.
///   /// The internal and external types are often the same, in which case this
///   /// can simply return the passed in value.
///   static const internal_key_type &GetInternalKey(external_key_type &EKey);
///   /// Read the key and data length from Buffer, leaving it pointing at the
///   /// following byte.
///   static std::pair<offset_type, offset_type>
///   ReadKeyDataLength(const unsigned char *&Buffer);
///   /// Read the key from Buffer, given the KeyLen as reported from
///   /// ReadKeyDataLength.
///   const internal_key_type &ReadKey(const unsigned char *Buffer,
///                                    offset_type KeyLen);
///   /// Read the data for Key from Buffer, given the DataLen as reported from
///   /// ReadKeyDataLength.
///   data_type ReadData(StringRef Key, const unsigned char *Buffer,
///                      offset_type DataLen);
/// };
/// \endcode
template <typename Info> class OnDiskChainedHashTable {
  const typename Info::offset_type NumBuckets;
  const typename Info::offset_type NumEntries;
  const unsigned char *const Buckets;
  const unsigned char *const Base;
  Info InfoObj;

public:
  typedef Info InfoType;
  typedef typename Info::internal_key_type internal_key_type;
  typedef typename Info::external_key_type external_key_type;
  typedef typename Info::data_type data_type;
  typedef typename Info::hash_value_type hash_value_type;
  typedef typename Info::offset_type offset_type;

  OnDiskChainedHashTable(offset_type NumBuckets, offset_type NumEntries,
                         const unsigned char *Buckets,
                         const unsigned char *Base,
                         const Info &InfoObj = Info())
      : NumBuckets(NumBuckets), NumEntries(NumEntries), Buckets(Buckets),
        Base(Base), InfoObj(InfoObj) {
    assert((reinterpret_cast<uintptr_t>(Buckets) & 0x3) == 0 &&
           "'buckets' must have a 4-byte alignment");
  }

  /// Read the number of buckets and the number of entries from a hash table
  /// produced by OnDiskHashTableGenerator::Emit, and advance the Buckets
  /// pointer past them.
  static std::pair<offset_type, offset_type>
  readNumBucketsAndEntries(const unsigned char *&Buckets) {
    assert((reinterpret_cast<uintptr_t>(Buckets) & 0x3) == 0 &&
           "buckets should be 4-byte aligned.");
    using namespace llvm::support;
    offset_type NumBuckets =
        endian::readNext<offset_type, llvm::endianness::little, aligned>(
            Buckets);
    offset_type NumEntries =
        endian::readNext<offset_type, llvm::endianness::little, aligned>(
            Buckets);
    return std::make_pair(NumBuckets, NumEntries);
  }

  offset_type getNumBuckets() const { return NumBuckets; }
  offset_type getNumEntries() const { return NumEntries; }
  const unsigned char *getBase() const { return Base; }
  const unsigned char *getBuckets() const { return Buckets; }

  bool isEmpty() const { return NumEntries == 0; }

  class iterator {
    internal_key_type Key;
    const unsigned char *const Data;
    const offset_type Len;
    Info *InfoObj;

  public:
    iterator() : Key(), Data(nullptr), Len(0), InfoObj(nullptr) {}
    iterator(const internal_key_type K, const unsigned char *D, offset_type L,
             Info *InfoObj)
        : Key(K), Data(D), Len(L), InfoObj(InfoObj) {}

    data_type operator*() const { return InfoObj->ReadData(Key, Data, Len); }

    const unsigned char *getDataPtr() const { return Data; }
    offset_type getDataLen() const { return Len; }

    bool operator==(const iterator &X) const { return X.Data == Data; }
    bool operator!=(const iterator &X) const { return X.Data != Data; }
  };

  /// Look up the stored data for a particular key.
  iterator find(const external_key_type &EKey, Info *InfoPtr = nullptr) {
    const internal_key_type &IKey = InfoObj.GetInternalKey(EKey);
    hash_value_type KeyHash = InfoObj.ComputeHash(IKey);
    return find_hashed(IKey, KeyHash, InfoPtr);
  }

  /// Look up the stored data for a particular key with a known hash.
  iterator find_hashed(const internal_key_type &IKey, hash_value_type KeyHash,
                       Info *InfoPtr = nullptr) {
    using namespace llvm::support;

    if (!InfoPtr)
      InfoPtr = &InfoObj;

    // Each bucket is just an offset into the hash table file.
    offset_type Idx = KeyHash & (NumBuckets - 1);
    const unsigned char *Bucket = Buckets + sizeof(offset_type) * Idx;

    offset_type Offset =
        endian::readNext<offset_type, llvm::endianness::little, aligned>(
            Bucket);
    if (Offset == 0)
      return iterator(); // Empty bucket.
    const unsigned char *Items = Base + Offset;

    // 'Items' starts with a 16-bit unsigned integer representing the
    // number of items in this bucket.
    unsigned Len = endian::readNext<uint16_t, llvm::endianness::little>(Items);

    for (unsigned i = 0; i < Len; ++i) {
      // Read the hash.
      hash_value_type ItemHash =
          endian::readNext<hash_value_type, llvm::endianness::little>(Items);

      // Determine the length of the key and the data.
      const std::pair<offset_type, offset_type> &L =
          Info::ReadKeyDataLength(Items);
      offset_type ItemLen = L.first + L.second;

      // Compare the hashes.  If they are not the same, skip the entry entirely.
      if (ItemHash != KeyHash) {
        Items += ItemLen;
        continue;
      }

      // Read the key.
      const internal_key_type &X =
          InfoPtr->ReadKey((const unsigned char *const)Items, L.first);

      // If the key doesn't match just skip reading the value.
      if (!InfoPtr->EqualKey(X, IKey)) {
        Items += ItemLen;
        continue;
      }

      // The key matches!
      return iterator(X, Items + L.first, L.second, InfoPtr);
    }

    return iterator();
  }

  iterator end() const { return iterator(); }

  Info &getInfoObj() { return InfoObj; }

  /// Create the hash table.
  ///
  /// \param Buckets is the beginning of the hash table itself, which follows
  /// the payload of entire structure. This is the value returned by
  /// OnDiskHashTableGenerator::Emit.
  ///
  /// \param Base is the point from which all offsets into the structure are
  /// based. This is offset 0 in the stream that was used when Emitting the
  /// table.
  static OnDiskChainedHashTable *Create(const unsigned char *Buckets,
                                        const unsigned char *const Base,
                                        const Info &InfoObj = Info()) {
    assert(Buckets > Base);
    auto NumBucketsAndEntries = readNumBucketsAndEntries(Buckets);
    return new OnDiskChainedHashTable<Info>(NumBucketsAndEntries.first,
                                            NumBucketsAndEntries.second,
                                            Buckets, Base, InfoObj);
  }
};

/// Provides lookup and iteration over an on disk hash table.
///
/// \copydetails llvm::OnDiskChainedHashTable
template <typename Info>
class OnDiskIterableChainedHashTable : public OnDiskChainedHashTable<Info> {
  const unsigned char *Payload;

public:
  typedef OnDiskChainedHashTable<Info>          base_type;
  typedef typename base_type::internal_key_type internal_key_type;
  typedef typename base_type::external_key_type external_key_type;
  typedef typename base_type::data_type         data_type;
  typedef typename base_type::hash_value_type   hash_value_type;
  typedef typename base_type::offset_type       offset_type;

private:
  /// Iterates over all of the keys in the table.
  class iterator_base {
    const unsigned char *Ptr;
    offset_type NumItemsInBucketLeft;
    offset_type NumEntriesLeft;

  public:
    typedef external_key_type value_type;

    iterator_base(const unsigned char *const Ptr, offset_type NumEntries)
        : Ptr(Ptr), NumItemsInBucketLeft(0), NumEntriesLeft(NumEntries) {}
    iterator_base()
        : Ptr(nullptr), NumItemsInBucketLeft(0), NumEntriesLeft(0) {}

    friend bool operator==(const iterator_base &X, const iterator_base &Y) {
      return X.NumEntriesLeft == Y.NumEntriesLeft;
    }
    friend bool operator!=(const iterator_base &X, const iterator_base &Y) {
      return X.NumEntriesLeft != Y.NumEntriesLeft;
    }

    /// Move to the next item.
    void advance() {
      using namespace llvm::support;
      if (!NumItemsInBucketLeft) {
        // 'Items' starts with a 16-bit unsigned integer representing the
        // number of items in this bucket.
        NumItemsInBucketLeft =
            endian::readNext<uint16_t, llvm::endianness::little>(Ptr);
      }
      Ptr += sizeof(hash_value_type); // Skip the hash.
      // Determine the length of the key and the data.
      const std::pair<offset_type, offset_type> &L =
          Info::ReadKeyDataLength(Ptr);
      Ptr += L.first + L.second;
      assert(NumItemsInBucketLeft);
      --NumItemsInBucketLeft;
      assert(NumEntriesLeft);
      --NumEntriesLeft;
    }

    /// Get the start of the item as written by the trait (after the hash and
    /// immediately before the key and value length).
    const unsigned char *getItem() const {
      return Ptr + (NumItemsInBucketLeft ? 0 : 2) + sizeof(hash_value_type);
    }
  };

public:
  OnDiskIterableChainedHashTable(offset_type NumBuckets, offset_type NumEntries,
                                 const unsigned char *Buckets,
                                 const unsigned char *Payload,
                                 const unsigned char *Base,
                                 const Info &InfoObj = Info())
      : base_type(NumBuckets, NumEntries, Buckets, Base, InfoObj),
        Payload(Payload) {}

  /// Iterates over all of the keys in the table.
  class key_iterator : public iterator_base {
    Info *InfoObj;

  public:
    typedef external_key_type value_type;

    key_iterator(const unsigned char *const Ptr, offset_type NumEntries,
                 Info *InfoObj)
        : iterator_base(Ptr, NumEntries), InfoObj(InfoObj) {}
    key_iterator() : iterator_base(), InfoObj() {}

    key_iterator &operator++() {
      this->advance();
      return *this;
    }
    key_iterator operator++(int) { // Postincrement
      key_iterator tmp = *this;
      ++*this;
      return tmp;
    }

    internal_key_type getInternalKey() const {
      auto *LocalPtr = this->getItem();

      // Determine the length of the key and the data.
      auto L = Info::ReadKeyDataLength(LocalPtr);

      // Read the key.
      return InfoObj->ReadKey(LocalPtr, L.first);
    }

    value_type operator*() const {
      return InfoObj->GetExternalKey(getInternalKey());
    }
  };

  key_iterator key_begin() {
    return key_iterator(Payload, this->getNumEntries(), &this->getInfoObj());
  }
  key_iterator key_end() { return key_iterator(); }

  iterator_range<key_iterator> keys() {
    return make_range(key_begin(), key_end());
  }

  /// Iterates over all the entries in the table, returning the data.
  class data_iterator : public iterator_base {
    Info *InfoObj;

  public:
    typedef data_type value_type;

    data_iterator(const unsigned char *const Ptr, offset_type NumEntries,
                  Info *InfoObj)
        : iterator_base(Ptr, NumEntries), InfoObj(InfoObj) {}
    data_iterator() : iterator_base(), InfoObj() {}

    data_iterator &operator++() { // Preincrement
      this->advance();
      return *this;
    }
    data_iterator operator++(int) { // Postincrement
      data_iterator tmp = *this;
      ++*this;
      return tmp;
    }

    value_type operator*() const {
      auto *LocalPtr = this->getItem();

      // Determine the length of the key and the data.
      auto L = Info::ReadKeyDataLength(LocalPtr);

      // Read the key.
      const internal_key_type &Key = InfoObj->ReadKey(LocalPtr, L.first);
      return InfoObj->ReadData(Key, LocalPtr + L.first, L.second);
    }
  };

  data_iterator data_begin() {
    return data_iterator(Payload, this->getNumEntries(), &this->getInfoObj());
  }
  data_iterator data_end() { return data_iterator(); }

  iterator_range<data_iterator> data() {
    return make_range(data_begin(), data_end());
  }

  /// Create the hash table.
  ///
  /// \param Buckets is the beginning of the hash table itself, which follows
  /// the payload of entire structure. This is the value returned by
  /// OnDiskHashTableGenerator::Emit.
  ///
  /// \param Payload is the beginning of the data contained in the table.  This
  /// is Base plus any padding or header data that was stored, ie, the offset
  /// that the stream was at when calling Emit.
  ///
  /// \param Base is the point from which all offsets into the structure are
  /// based. This is offset 0 in the stream that was used when Emitting the
  /// table.
  static OnDiskIterableChainedHashTable *
  Create(const unsigned char *Buckets, const unsigned char *const Payload,
         const unsigned char *const Base, const Info &InfoObj = Info()) {
    assert(Buckets > Base);
    auto NumBucketsAndEntries =
        OnDiskIterableChainedHashTable<Info>::readNumBucketsAndEntries(Buckets);
    return new OnDiskIterableChainedHashTable<Info>(
        NumBucketsAndEntries.first, NumBucketsAndEntries.second,
        Buckets, Payload, Base, InfoObj);
  }
};

} // end namespace llvm

#endif
