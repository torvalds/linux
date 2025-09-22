//===- DebugSymbolsSubsection.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGSYMBOLSSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGSYMBOLSSUBSECTION_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
class DebugSymbolsSubsectionRef final : public DebugSubsectionRef {
public:
  DebugSymbolsSubsectionRef()
      : DebugSubsectionRef(DebugSubsectionKind::Symbols) {}

  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::Symbols;
  }

  Error initialize(BinaryStreamReader Reader);

  CVSymbolArray::Iterator begin() const { return Records.begin(); }
  CVSymbolArray::Iterator end() const { return Records.end(); }

private:
  CVSymbolArray Records;
};

class DebugSymbolsSubsection final : public DebugSubsection {
public:
  DebugSymbolsSubsection() : DebugSubsection(DebugSubsectionKind::Symbols) {}
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::Symbols;
  }

  uint32_t calculateSerializedSize() const override;
  Error commit(BinaryStreamWriter &Writer) const override;

  void addSymbol(CVSymbol Symbol);

private:
  uint32_t Length = 0;
  std::vector<CVSymbol> Records;
};
}
}

#endif
