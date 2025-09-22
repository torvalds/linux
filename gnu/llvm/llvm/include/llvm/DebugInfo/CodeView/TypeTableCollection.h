//===- TypeTableCollection.h ---------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPETABLECOLLECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPETABLECOLLECTION_H

#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/Support/StringSaver.h"

#include <vector>

namespace llvm {
namespace codeview {

class TypeTableCollection : public TypeCollection {
public:
  explicit TypeTableCollection(ArrayRef<ArrayRef<uint8_t>> Records);

  std::optional<TypeIndex> getFirst() override;
  std::optional<TypeIndex> getNext(TypeIndex Prev) override;

  CVType getType(TypeIndex Index) override;
  StringRef getTypeName(TypeIndex Index) override;
  bool contains(TypeIndex Index) override;
  uint32_t size() override;
  uint32_t capacity() override;
  bool replaceType(TypeIndex &Index, CVType Data, bool Stabilize) override;

private:
  BumpPtrAllocator Allocator;
  StringSaver NameStorage;
  std::vector<StringRef> Names;
  ArrayRef<ArrayRef<uint8_t>> Records;
};
}
}

#endif
