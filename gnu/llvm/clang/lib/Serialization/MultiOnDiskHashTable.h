//===- MultiOnDiskHashTable.h - Merged set of hash tables -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file provides a hash table data structure suitable for incremental and
//  distributed storage across a set of files.
//
//  Multiple hash tables from different files are implicitly merged to improve
//  performance, and on reload the merged table will override those from other
//  files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_SERIALIZATION_MULTIONDISKHASHTABLE_H
#define LLVM_CLANG_LIB_SERIALIZATION_MULTIONDISKHASHTABLE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/OnDiskHashTable.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace clang {
namespace serialization {

/// A collection of on-disk hash tables, merged when relevant for performance.
template<typename Info> class MultiOnDiskHashTable {
public:
  /// A handle to a file, used when overriding tables.
  using file_type = typename Info::file_type;

  /// A pointer to an on-disk representation of the hash table.
  using storage_type = const unsigned char *;

  using external_key_type = typename Info::external_key_type;
  using internal_key_type = typename Info::internal_key_type;
  using data_type = typename Info::data_type;
  using data_type_builder = typename Info::data_type_builder;
  using hash_value_type = unsigned;

private:
  /// The generator is permitted to read our merged table.
  template<typename ReaderInfo, typename WriterInfo>
  friend class MultiOnDiskHashTableGenerator;

  /// A hash table stored on disk.
  struct OnDiskTable {
    using HashTable = llvm::OnDiskIterableChainedHashTable<Info>;

    file_type File;
    HashTable Table;

    OnDiskTable(file_type File, unsigned NumBuckets, unsigned NumEntries,
                storage_type Buckets, storage_type Payload, storage_type Base,
                const Info &InfoObj)
        : File(File),
          Table(NumBuckets, NumEntries, Buckets, Payload, Base, InfoObj) {}
  };

  struct MergedTable {
    std::vector<file_type> Files;
    llvm::DenseMap<internal_key_type, data_type> Data;
  };

  using Table = llvm::PointerUnion<OnDiskTable *, MergedTable *>;
  using TableVector = llvm::TinyPtrVector<void *>;

  /// The current set of on-disk and merged tables.
  /// We manually store the opaque value of the Table because TinyPtrVector
  /// can't cope with holding a PointerUnion directly.
  /// There can be at most one MergedTable in this vector, and if present,
  /// it is the first table.
  TableVector Tables;

  /// Files corresponding to overridden tables that we've not yet
  /// discarded.
  llvm::TinyPtrVector<file_type> PendingOverrides;

  struct AsOnDiskTable {
    using result_type = OnDiskTable *;

    result_type operator()(void *P) const {
      return Table::getFromOpaqueValue(P).template get<OnDiskTable *>();
    }
  };

  using table_iterator =
      llvm::mapped_iterator<TableVector::iterator, AsOnDiskTable>;
  using table_range = llvm::iterator_range<table_iterator>;

  /// The current set of on-disk tables.
  table_range tables() {
    auto Begin = Tables.begin(), End = Tables.end();
    if (getMergedTable())
      ++Begin;
    return llvm::make_range(llvm::map_iterator(Begin, AsOnDiskTable()),
                            llvm::map_iterator(End, AsOnDiskTable()));
  }

  MergedTable *getMergedTable() const {
    // If we already have a merged table, it's the first one.
    return Tables.empty() ? nullptr : Table::getFromOpaqueValue(*Tables.begin())
                                          .template dyn_cast<MergedTable*>();
  }

  /// Delete all our current on-disk tables.
  void clear() {
    for (auto *T : tables())
      delete T;
    if (auto *M = getMergedTable())
      delete M;
    Tables.clear();
  }

  void removeOverriddenTables() {
    llvm::DenseSet<file_type> Files;
    Files.insert(PendingOverrides.begin(), PendingOverrides.end());
    // Explicitly capture Files to work around an MSVC 2015 rejects-valid bug.
    auto ShouldRemove = [&Files](void *T) -> bool {
      auto *ODT = Table::getFromOpaqueValue(T).template get<OnDiskTable *>();
      bool Remove = Files.count(ODT->File);
      if (Remove)
        delete ODT;
      return Remove;
    };
    Tables.erase(std::remove_if(tables().begin().getCurrent(), Tables.end(),
                                ShouldRemove),
                 Tables.end());
    PendingOverrides.clear();
  }

  void condense() {
    MergedTable *Merged = getMergedTable();
    if (!Merged)
      Merged = new MergedTable;

    // Read in all the tables and merge them together.
    // FIXME: Be smarter about which tables we merge.
    for (auto *ODT : tables()) {
      auto &HT = ODT->Table;
      Info &InfoObj = HT.getInfoObj();

      for (auto I = HT.data_begin(), E = HT.data_end(); I != E; ++I) {
        auto *LocalPtr = I.getItem();

        // FIXME: Don't rely on the OnDiskHashTable format here.
        auto L = InfoObj.ReadKeyDataLength(LocalPtr);
        const internal_key_type &Key = InfoObj.ReadKey(LocalPtr, L.first);
        data_type_builder ValueBuilder(Merged->Data[Key]);
        InfoObj.ReadDataInto(Key, LocalPtr + L.first, L.second,
                             ValueBuilder);
      }

      Merged->Files.push_back(ODT->File);
      delete ODT;
    }

    Tables.clear();
    Tables.push_back(Table(Merged).getOpaqueValue());
  }

public:
  MultiOnDiskHashTable() = default;

  MultiOnDiskHashTable(MultiOnDiskHashTable &&O)
      : Tables(std::move(O.Tables)),
        PendingOverrides(std::move(O.PendingOverrides)) {
    O.Tables.clear();
  }

  MultiOnDiskHashTable &operator=(MultiOnDiskHashTable &&O) {
    if (&O == this)
      return *this;
    clear();
    Tables = std::move(O.Tables);
    O.Tables.clear();
    PendingOverrides = std::move(O.PendingOverrides);
    return *this;
  }

  ~MultiOnDiskHashTable() { clear(); }

  /// Add the table \p Data loaded from file \p File.
  void add(file_type File, storage_type Data, Info InfoObj = Info()) {
    using namespace llvm::support;

    storage_type Ptr = Data;

    uint32_t BucketOffset =
        endian::readNext<uint32_t, llvm::endianness::little>(Ptr);

    // Read the list of overridden files.
    uint32_t NumFiles =
        endian::readNext<uint32_t, llvm::endianness::little>(Ptr);
    // FIXME: Add a reserve() to TinyPtrVector so that we don't need to make
    // an additional copy.
    llvm::SmallVector<file_type, 16> OverriddenFiles;
    OverriddenFiles.reserve(NumFiles);
    for (/**/; NumFiles != 0; --NumFiles)
      OverriddenFiles.push_back(InfoObj.ReadFileRef(Ptr));
    PendingOverrides.insert(PendingOverrides.end(), OverriddenFiles.begin(),
                            OverriddenFiles.end());

    // Read the OnDiskChainedHashTable header.
    storage_type Buckets = Data + BucketOffset;
    auto NumBucketsAndEntries =
        OnDiskTable::HashTable::readNumBucketsAndEntries(Buckets);

    // Register the table.
    Table NewTable = new OnDiskTable(File, NumBucketsAndEntries.first,
                                     NumBucketsAndEntries.second,
                                     Buckets, Ptr, Data, std::move(InfoObj));
    Tables.push_back(NewTable.getOpaqueValue());
  }

  /// Find and read the lookup results for \p EKey.
  data_type find(const external_key_type &EKey) {
    data_type Result;

    if (!PendingOverrides.empty())
      removeOverriddenTables();

    if (Tables.size() > static_cast<unsigned>(Info::MaxTables))
      condense();

    internal_key_type Key = Info::GetInternalKey(EKey);
    auto KeyHash = Info::ComputeHash(Key);

    if (MergedTable *M = getMergedTable()) {
      auto It = M->Data.find(Key);
      if (It != M->Data.end())
        Result = It->second;
    }

    data_type_builder ResultBuilder(Result);

    for (auto *ODT : tables()) {
      auto &HT = ODT->Table;
      auto It = HT.find_hashed(Key, KeyHash);
      if (It != HT.end())
        HT.getInfoObj().ReadDataInto(Key, It.getDataPtr(), It.getDataLen(),
                                     ResultBuilder);
    }

    return Result;
  }

  /// Read all the lookup results into a single value. This only makes
  /// sense if merging values across keys is meaningful.
  data_type findAll() {
    data_type Result;
    data_type_builder ResultBuilder(Result);

    if (!PendingOverrides.empty())
      removeOverriddenTables();

    if (MergedTable *M = getMergedTable()) {
      for (auto &KV : M->Data)
        Info::MergeDataInto(KV.second, ResultBuilder);
    }

    for (auto *ODT : tables()) {
      auto &HT = ODT->Table;
      Info &InfoObj = HT.getInfoObj();
      for (auto I = HT.data_begin(), E = HT.data_end(); I != E; ++I) {
        auto *LocalPtr = I.getItem();

        // FIXME: Don't rely on the OnDiskHashTable format here.
        auto L = InfoObj.ReadKeyDataLength(LocalPtr);
        const internal_key_type &Key = InfoObj.ReadKey(LocalPtr, L.first);
        InfoObj.ReadDataInto(Key, LocalPtr + L.first, L.second, ResultBuilder);
      }
    }

    return Result;
  }
};

/// Writer for the on-disk hash table.
template<typename ReaderInfo, typename WriterInfo>
class MultiOnDiskHashTableGenerator {
  using BaseTable = MultiOnDiskHashTable<ReaderInfo>;
  using Generator = llvm::OnDiskChainedHashTableGenerator<WriterInfo>;

  Generator Gen;

public:
  MultiOnDiskHashTableGenerator() : Gen() {}

  void insert(typename WriterInfo::key_type_ref Key,
              typename WriterInfo::data_type_ref Data, WriterInfo &Info) {
    Gen.insert(Key, Data, Info);
  }

  void emit(llvm::SmallVectorImpl<char> &Out, WriterInfo &Info,
            const BaseTable *Base) {
    using namespace llvm::support;

    llvm::raw_svector_ostream OutStream(Out);

    // Write our header information.
    {
      endian::Writer Writer(OutStream, llvm::endianness::little);

      // Reserve four bytes for the bucket offset.
      Writer.write<uint32_t>(0);

      if (auto *Merged = Base ? Base->getMergedTable() : nullptr) {
        // Write list of overridden files.
        Writer.write<uint32_t>(Merged->Files.size());
        for (const auto &F : Merged->Files)
          Info.EmitFileRef(OutStream, F);

        // Add all merged entries from Base to the generator.
        for (auto &KV : Merged->Data) {
          if (!Gen.contains(KV.first, Info))
            Gen.insert(KV.first, Info.ImportData(KV.second), Info);
        }
      } else {
        Writer.write<uint32_t>(0);
      }
    }

    // Write the table itself.
    uint32_t BucketOffset = Gen.Emit(OutStream, Info);

    // Replace the first four bytes with the bucket offset.
    endian::write32le(Out.data(), BucketOffset);
  }
};

} // namespace serialization
} // namespace clang

#endif // LLVM_CLANG_LIB_SERIALIZATION_MULTIONDISKHASHTABLE_H
