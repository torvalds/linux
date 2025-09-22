//===- GlobalTypeTableBuilder.h ----------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_GLOBALTYPETABLEBUILDER_H
#define LLVM_DEBUGINFO_CODEVIEW_GLOBALTYPETABLEBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/SimpleTypeSerializer.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeHashing.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/Allocator.h"
#include <cassert>
#include <cstdint>

namespace llvm {
namespace codeview {

class ContinuationRecordBuilder;

class GlobalTypeTableBuilder : public TypeCollection {
  /// Storage for records.  These need to outlive the TypeTableBuilder.
  BumpPtrAllocator &RecordStorage;

  /// A serializer that can write non-continuation leaf types.  Only used as
  /// a convenience function so that we can provide an interface method to
  /// write an unserialized record.
  SimpleTypeSerializer SimpleSerializer;

  /// Hash table.
  DenseMap<GloballyHashedType, TypeIndex> HashedRecords;

  /// Contains a list of all records indexed by TypeIndex.toArrayIndex().
  SmallVector<ArrayRef<uint8_t>, 2> SeenRecords;

  /// Contains a list of all hash values indexed by TypeIndex.toArrayIndex().
  SmallVector<GloballyHashedType, 2> SeenHashes;

public:
  explicit GlobalTypeTableBuilder(BumpPtrAllocator &Storage);
  ~GlobalTypeTableBuilder();

  // TypeCollection overrides
  std::optional<TypeIndex> getFirst() override;
  std::optional<TypeIndex> getNext(TypeIndex Prev) override;
  CVType getType(TypeIndex Index) override;
  StringRef getTypeName(TypeIndex Index) override;
  bool contains(TypeIndex Index) override;
  uint32_t size() override;
  uint32_t capacity() override;
  bool replaceType(TypeIndex &Index, CVType Data, bool Stabilize) override;

  // public interface
  void reset();
  TypeIndex nextTypeIndex() const;

  BumpPtrAllocator &getAllocator() { return RecordStorage; }

  ArrayRef<ArrayRef<uint8_t>> records() const;
  ArrayRef<GloballyHashedType> hashes() const;

  template <typename CreateFunc>
  TypeIndex insertRecordAs(GloballyHashedType Hash, size_t RecordSize,
                           CreateFunc Create) {
    assert(RecordSize < UINT32_MAX && "Record too big");
    assert(RecordSize % 4 == 0 &&
           "RecordSize is not a multiple of 4 bytes which will cause "
           "misalignment in the output TPI stream!");

    auto Result = HashedRecords.try_emplace(Hash, nextTypeIndex());

    if (LLVM_UNLIKELY(Result.second /*inserted*/ ||
                      Result.first->second.isSimple())) {
      uint8_t *Stable = RecordStorage.Allocate<uint8_t>(RecordSize);
      MutableArrayRef<uint8_t> Data(Stable, RecordSize);
      ArrayRef<uint8_t> StableRecord = Create(Data);
      if (StableRecord.empty()) {
        // Records with forward references into the Type stream will be deferred
        // for insertion at a later time, on the second pass.
        Result.first->getSecond() = TypeIndex(SimpleTypeKind::NotTranslated);
        return TypeIndex(SimpleTypeKind::NotTranslated);
      }
      if (Result.first->second.isSimple()) {
        assert(Result.first->second.getIndex() ==
               (uint32_t)SimpleTypeKind::NotTranslated);
        // On the second pass, update with index to remapped record. The
        // (initially misbehaved) record will now come *after* other records
        // resolved in the first pass, with proper *back* references in the
        // stream.
        Result.first->second = nextTypeIndex();
      }
      SeenRecords.push_back(StableRecord);
      SeenHashes.push_back(Hash);
    }

    return Result.first->second;
  }

  TypeIndex insertRecordBytes(ArrayRef<uint8_t> Data);
  TypeIndex insertRecord(ContinuationRecordBuilder &Builder);

  template <typename T> TypeIndex writeLeafType(T &Record) {
    ArrayRef<uint8_t> Data = SimpleSerializer.serialize(Record);
    return insertRecordBytes(Data);
  }
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_GLOBALTYPETABLEBUILDER_H
