//===- MergingTypeTableBuilder.h ---------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_MERGINGTYPETABLEBUILDER_H
#define LLVM_DEBUGINFO_CODEVIEW_MERGINGTYPETABLEBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/SimpleTypeSerializer.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/Allocator.h"
#include <cstdint>

namespace llvm {
namespace codeview {
struct LocallyHashedType;

class ContinuationRecordBuilder;

class MergingTypeTableBuilder : public TypeCollection {
  /// Storage for records.  These need to outlive the TypeTableBuilder.
  BumpPtrAllocator &RecordStorage;

  /// A serializer that can write non-continuation leaf types.  Only used as
  /// a convenience function so that we can provide an interface method to
  /// write an unserialized record.
  SimpleTypeSerializer SimpleSerializer;

  /// Hash table.
  DenseMap<LocallyHashedType, TypeIndex> HashedRecords;

  /// Contains a list of all records indexed by TypeIndex.toArrayIndex().
  SmallVector<ArrayRef<uint8_t>, 2> SeenRecords;

public:
  explicit MergingTypeTableBuilder(BumpPtrAllocator &Storage);
  ~MergingTypeTableBuilder();

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

  TypeIndex insertRecordAs(hash_code Hash, ArrayRef<uint8_t> &Record);
  TypeIndex insertRecordBytes(ArrayRef<uint8_t> &Record);
  TypeIndex insertRecord(ContinuationRecordBuilder &Builder);

  template <typename T> TypeIndex writeLeafType(T &Record) {
    ArrayRef<uint8_t> Data = SimpleSerializer.serialize(Record);
    return insertRecordBytes(Data);
  }
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_MERGINGTYPETABLEBUILDER_H
