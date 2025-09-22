//===- TypeTableCollection.cpp -------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/TypeTableCollection.h"

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/RecordName.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;
using namespace llvm::codeview;

TypeTableCollection::TypeTableCollection(ArrayRef<ArrayRef<uint8_t>> Records)
    : NameStorage(Allocator), Records(Records) {
  Names.resize(Records.size());
}

std::optional<TypeIndex> TypeTableCollection::getFirst() {
  if (empty())
    return std::nullopt;
  return TypeIndex::fromArrayIndex(0);
}

std::optional<TypeIndex> TypeTableCollection::getNext(TypeIndex Prev) {
  assert(contains(Prev));
  ++Prev;
  if (Prev.toArrayIndex() == size())
    return std::nullopt;
  return Prev;
}

CVType TypeTableCollection::getType(TypeIndex Index) {
  assert(Index.toArrayIndex() < Records.size());
  return CVType(Records[Index.toArrayIndex()]);
}

StringRef TypeTableCollection::getTypeName(TypeIndex Index) {
  if (Index.isNoneType() || Index.isSimple())
    return TypeIndex::simpleTypeName(Index);

  uint32_t I = Index.toArrayIndex();
  if (Names[I].data() == nullptr) {
    StringRef Result = NameStorage.save(computeTypeName(*this, Index));
    Names[I] = Result;
  }
  return Names[I];
}

bool TypeTableCollection::contains(TypeIndex Index) {
  return Index.toArrayIndex() <= size();
}

uint32_t TypeTableCollection::size() { return Records.size(); }

uint32_t TypeTableCollection::capacity() { return Records.size(); }

bool TypeTableCollection::replaceType(TypeIndex &Index, CVType Data,
                                      bool Stabilize) {
  llvm_unreachable("Method cannot be called");
}
