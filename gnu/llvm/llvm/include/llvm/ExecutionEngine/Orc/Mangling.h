//===------ Mangling.h -- Name Mangling Utilities for ORC -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Name mangling utilities for ORC.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MANGLING_H
#define LLVM_EXECUTIONENGINE_ORC_MANGLING_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {
namespace orc {

/// Mangles symbol names then uniques them in the context of an
/// ExecutionSession.
class MangleAndInterner {
public:
  MangleAndInterner(ExecutionSession &ES, const DataLayout &DL);
  SymbolStringPtr operator()(StringRef Name);

private:
  ExecutionSession &ES;
  const DataLayout &DL;
};

/// Maps IR global values to their linker symbol names / flags.
///
/// This utility can be used when adding new IR globals in the JIT.
class IRSymbolMapper {
public:
  struct ManglingOptions {
    bool EmulatedTLS = false;
  };

  using SymbolNameToDefinitionMap = std::map<SymbolStringPtr, GlobalValue *>;

  /// Add mangled symbols for the given GlobalValues to SymbolFlags.
  /// If a SymbolToDefinitionMap pointer is supplied then it will be populated
  /// with Name-to-GlobalValue* mappings. Note that this mapping is not
  /// necessarily one-to-one: thread-local GlobalValues, for example, may
  /// produce more than one symbol, in which case the map will contain duplicate
  /// values.
  static void add(ExecutionSession &ES, const ManglingOptions &MO,
                  ArrayRef<GlobalValue *> GVs, SymbolFlagsMap &SymbolFlags,
                  SymbolNameToDefinitionMap *SymbolToDefinition = nullptr);
};

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_MANGLING_H
