//===- SymbolizableModule.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SymbolizableModule interface.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZABLEMODULE_H
#define LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZABLEMODULE_H

#include "llvm/DebugInfo/DIContext.h"
#include <cstdint>

namespace llvm {
namespace symbolize {

using FunctionNameKind = DILineInfoSpecifier::FunctionNameKind;

class SymbolizableModule {
public:
  virtual ~SymbolizableModule() = default;

  virtual DILineInfo symbolizeCode(object::SectionedAddress ModuleOffset,
                                   DILineInfoSpecifier LineInfoSpecifier,
                                   bool UseSymbolTable) const = 0;
  virtual DIInliningInfo
  symbolizeInlinedCode(object::SectionedAddress ModuleOffset,
                       DILineInfoSpecifier LineInfoSpecifier,
                       bool UseSymbolTable) const = 0;
  virtual DIGlobal
  symbolizeData(object::SectionedAddress ModuleOffset) const = 0;
  virtual std::vector<DILocal>
  symbolizeFrame(object::SectionedAddress ModuleOffset) const = 0;

  virtual std::vector<object::SectionedAddress>
  findSymbol(StringRef Symbol, uint64_t Offset) const = 0;

  // Return true if this is a 32-bit x86 PE COFF module.
  virtual bool isWin32Module() const = 0;

  // Returns the preferred base of the module, i.e. where the loader would place
  // it in memory assuming there were no conflicts.
  virtual uint64_t getModulePreferredBase() const = 0;
};

} // end namespace symbolize
} // end namespace llvm

#endif  // LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZABLEMODULE_H
