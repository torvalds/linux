//===- TypeCollection.h - A collection of CodeView type records -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPECOLLECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPECOLLECTION_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"

namespace llvm {
namespace codeview {
class TypeCollection {
public:
  virtual ~TypeCollection() = default;

  bool empty() { return size() == 0; }

  virtual std::optional<TypeIndex> getFirst() = 0;
  virtual std::optional<TypeIndex> getNext(TypeIndex Prev) = 0;

  virtual CVType getType(TypeIndex Index) = 0;
  virtual StringRef getTypeName(TypeIndex Index) = 0;
  virtual bool contains(TypeIndex Index) = 0;
  virtual uint32_t size() = 0;
  virtual uint32_t capacity() = 0;
  virtual bool replaceType(TypeIndex &Index, CVType Data, bool Stabilize) = 0;

  template <typename TFunc> void ForEachRecord(TFunc Func) {
    std::optional<TypeIndex> Next = getFirst();

    while (Next) {
      TypeIndex N = *Next;
      Func(N, getType(N));
      Next = getNext(N);
    }
  }
};
}
}

#endif
