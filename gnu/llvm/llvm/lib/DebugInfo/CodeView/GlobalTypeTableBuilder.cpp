//===- GlobalTypeTableBuilder.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/GlobalTypeTableBuilder.h"
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

TypeIndex GlobalTypeTableBuilder::nextTypeIndex() const {
  return TypeIndex::fromArrayIndex(SeenRecords.size());
}

GlobalTypeTableBuilder::GlobalTypeTableBuilder(BumpPtrAllocator &Storage)
    : RecordStorage(Storage) {
  SeenRecords.reserve(4096);
}

GlobalTypeTableBuilder::~GlobalTypeTableBuilder() = default;

std::optional<TypeIndex> GlobalTypeTableBuilder::getFirst() {
  if (empty())
    return std::nullopt;

  return TypeIndex(TypeIndex::FirstNonSimpleIndex);
}

std::optional<TypeIndex> GlobalTypeTableBuilder::getNext(TypeIndex Prev) {
  if (++Prev == nextTypeIndex())
    return std::nullopt;
  return Prev;
}

CVType GlobalTypeTableBuilder::getType(TypeIndex Index) {
  CVType Type(SeenRecords[Index.toArrayIndex()]);
  return Type;
}

StringRef GlobalTypeTableBuilder::getTypeName(TypeIndex Index) {
  llvm_unreachable("Method not implemented");
}

bool GlobalTypeTableBuilder::contains(TypeIndex Index) {
  if (Index.isSimple() || Index.isNoneType())
    return false;

  return Index.toArrayIndex() < SeenRecords.size();
}

uint32_t GlobalTypeTableBuilder::size() { return SeenRecords.size(); }

uint32_t GlobalTypeTableBuilder::capacity() { return SeenRecords.size(); }

ArrayRef<ArrayRef<uint8_t>> GlobalTypeTableBuilder::records() const {
  return SeenRecords;
}

ArrayRef<GloballyHashedType> GlobalTypeTableBuilder::hashes() const {
  return SeenHashes;
}

void GlobalTypeTableBuilder::reset() {
  HashedRecords.clear();
  SeenRecords.clear();
}

static inline ArrayRef<uint8_t> stabilize(BumpPtrAllocator &Alloc,
                                          ArrayRef<uint8_t> Data) {
  uint8_t *Stable = Alloc.Allocate<uint8_t>(Data.size());
  memcpy(Stable, Data.data(), Data.size());
  return ArrayRef(Stable, Data.size());
}

TypeIndex GlobalTypeTableBuilder::insertRecordBytes(ArrayRef<uint8_t> Record) {
  GloballyHashedType GHT =
      GloballyHashedType::hashType(Record, SeenHashes, SeenHashes);
  return insertRecordAs(GHT, Record.size(),
                        [Record](MutableArrayRef<uint8_t> Data) {
                          assert(Data.size() == Record.size());
                          ::memcpy(Data.data(), Record.data(), Record.size());
                          return Data;
                        });
}

TypeIndex
GlobalTypeTableBuilder::insertRecord(ContinuationRecordBuilder &Builder) {
  TypeIndex TI;
  auto Fragments = Builder.end(nextTypeIndex());
  assert(!Fragments.empty());
  for (auto C : Fragments)
    TI = insertRecordBytes(C.RecordData);
  return TI;
}

bool GlobalTypeTableBuilder::replaceType(TypeIndex &Index, CVType Data,
                                         bool Stabilize) {
  assert(Index.toArrayIndex() < SeenRecords.size() &&
         "This function cannot be used to insert records!");

  ArrayRef<uint8_t> Record = Data.data();
  assert(Record.size() < UINT32_MAX && "Record too big");
  assert(Record.size() % 4 == 0 &&
         "The type record size is not a multiple of 4 bytes which will cause "
         "misalignment in the output TPI stream!");

  GloballyHashedType Hash =
      GloballyHashedType::hashType(Record, SeenHashes, SeenHashes);
  auto Result = HashedRecords.try_emplace(Hash, Index.toArrayIndex());
  if (!Result.second) {
    Index = Result.first->second;
    return false; // The record is already there, at a different location
  }

  if (Stabilize)
    Record = stabilize(RecordStorage, Record);

  SeenRecords[Index.toArrayIndex()] = Record;
  SeenHashes[Index.toArrayIndex()] = Hash;
  return true;
}
