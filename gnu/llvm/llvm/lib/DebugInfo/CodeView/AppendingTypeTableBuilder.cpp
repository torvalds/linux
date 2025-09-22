//===- AppendingTypeTableBuilder.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/AppendingTypeTableBuilder.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/ContinuationRecordBuilder.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <cstring>

using namespace llvm;
using namespace llvm::codeview;

TypeIndex AppendingTypeTableBuilder::nextTypeIndex() const {
  return TypeIndex::fromArrayIndex(SeenRecords.size());
}

AppendingTypeTableBuilder::AppendingTypeTableBuilder(BumpPtrAllocator &Storage)
    : RecordStorage(Storage) {}

AppendingTypeTableBuilder::~AppendingTypeTableBuilder() = default;

std::optional<TypeIndex> AppendingTypeTableBuilder::getFirst() {
  if (empty())
    return std::nullopt;

  return TypeIndex(TypeIndex::FirstNonSimpleIndex);
}

std::optional<TypeIndex> AppendingTypeTableBuilder::getNext(TypeIndex Prev) {
  if (++Prev == nextTypeIndex())
    return std::nullopt;
  return Prev;
}

CVType AppendingTypeTableBuilder::getType(TypeIndex Index){
  return CVType(SeenRecords[Index.toArrayIndex()]);
}

StringRef AppendingTypeTableBuilder::getTypeName(TypeIndex Index) {
  llvm_unreachable("Method not implemented");
}

bool AppendingTypeTableBuilder::contains(TypeIndex Index) {
  if (Index.isSimple() || Index.isNoneType())
    return false;

  return Index.toArrayIndex() < SeenRecords.size();
}

uint32_t AppendingTypeTableBuilder::size() { return SeenRecords.size(); }

uint32_t AppendingTypeTableBuilder::capacity() { return SeenRecords.size(); }

ArrayRef<ArrayRef<uint8_t>> AppendingTypeTableBuilder::records() const {
  return SeenRecords;
}

void AppendingTypeTableBuilder::reset() { SeenRecords.clear(); }

static ArrayRef<uint8_t> stabilize(BumpPtrAllocator &RecordStorage,
                                   ArrayRef<uint8_t> Record) {
  uint8_t *Stable = RecordStorage.Allocate<uint8_t>(Record.size());
  memcpy(Stable, Record.data(), Record.size());
  return ArrayRef<uint8_t>(Stable, Record.size());
}

TypeIndex
AppendingTypeTableBuilder::insertRecordBytes(ArrayRef<uint8_t> &Record) {
  TypeIndex NewTI = nextTypeIndex();
  Record = stabilize(RecordStorage, Record);
  SeenRecords.push_back(Record);
  return NewTI;
}

TypeIndex
AppendingTypeTableBuilder::insertRecord(ContinuationRecordBuilder &Builder) {
  TypeIndex TI;
  auto Fragments = Builder.end(nextTypeIndex());
  assert(!Fragments.empty());
  for (auto C : Fragments)
    TI = insertRecordBytes(C.RecordData);
  return TI;
}

bool AppendingTypeTableBuilder::replaceType(TypeIndex &Index, CVType Data,
                                            bool Stabilize) {
  assert(Index.toArrayIndex() < SeenRecords.size() &&
         "This function cannot be used to insert records!");

  ArrayRef<uint8_t> Record = Data.data();
  if (Stabilize)
    Record = stabilize(RecordStorage, Record);
  SeenRecords[Index.toArrayIndex()] = Record;
  return true;
}
