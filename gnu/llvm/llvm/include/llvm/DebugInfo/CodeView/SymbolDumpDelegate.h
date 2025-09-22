//===-- SymbolDumpDelegate.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLDUMPDELEGATE_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLDUMPDELEGATE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorDelegate.h"
#include <cstdint>

namespace llvm {
namespace codeview {

class SymbolDumpDelegate : public SymbolVisitorDelegate {
public:
  ~SymbolDumpDelegate() override = default;

  virtual void printRelocatedField(StringRef Label, uint32_t RelocOffset,
                                   uint32_t Offset,
                                   StringRef *RelocSym = nullptr) = 0;
  virtual void printBinaryBlockWithRelocs(StringRef Label,
                                          ArrayRef<uint8_t> Block) = 0;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SYMBOLDUMPDELEGATE_H
