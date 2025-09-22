//===----- DebugUtils.h - Utilities for debugging ORC JITs ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for debugging ORC-based JITs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_DEBUGUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_DEBUGUTILS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/SymbolStringPool.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <string>

namespace llvm {

class MemoryBuffer;

namespace orc {

// --raw_ostream operators for ORC types--

/// Render a SymbolStringPtr.
raw_ostream &operator<<(raw_ostream &OS, const SymbolStringPtr &Sym);

/// Render a NonOwningSymbolStringPtr.
raw_ostream &operator<<(raw_ostream &OS, NonOwningSymbolStringPtr Sym);

/// Render a SymbolNameSet.
raw_ostream &operator<<(raw_ostream &OS, const SymbolNameSet &Symbols);

/// Render a SymbolNameVector.
raw_ostream &operator<<(raw_ostream &OS, const SymbolNameVector &Symbols);

/// Render an array of SymbolStringPtrs.
raw_ostream &operator<<(raw_ostream &OS, ArrayRef<SymbolStringPtr> Symbols);

/// Render JITSymbolFlags.
raw_ostream &operator<<(raw_ostream &OS, const JITSymbolFlags &Flags);

/// Render a SymbolFlagsMap entry.
raw_ostream &operator<<(raw_ostream &OS, const SymbolFlagsMap::value_type &KV);

/// Render a SymbolMap entry.
raw_ostream &operator<<(raw_ostream &OS, const SymbolMap::value_type &KV);

/// Render a SymbolFlagsMap.
raw_ostream &operator<<(raw_ostream &OS, const SymbolFlagsMap &SymbolFlags);

/// Render a SymbolMap.
raw_ostream &operator<<(raw_ostream &OS, const SymbolMap &Symbols);

/// Render a SymbolDependenceMap entry.
raw_ostream &operator<<(raw_ostream &OS,
                        const SymbolDependenceMap::value_type &KV);

/// Render a SymbolDependendeMap.
raw_ostream &operator<<(raw_ostream &OS, const SymbolDependenceMap &Deps);

/// Render a MaterializationUnit.
raw_ostream &operator<<(raw_ostream &OS, const MaterializationUnit &MU);

//// Render a JITDylibLookupFlags instance.
raw_ostream &operator<<(raw_ostream &OS,
                        const JITDylibLookupFlags &JDLookupFlags);

/// Render a SymbolLookupFlags instance.
raw_ostream &operator<<(raw_ostream &OS, const SymbolLookupFlags &LookupFlags);

/// Render a SymbolLookupSet entry.
raw_ostream &operator<<(raw_ostream &OS, const SymbolLookupSet::value_type &KV);

/// Render a SymbolLookupSet.
raw_ostream &operator<<(raw_ostream &OS, const SymbolLookupSet &LookupSet);

/// Render a JITDylibSearchOrder.
raw_ostream &operator<<(raw_ostream &OS,
                        const JITDylibSearchOrder &SearchOrder);

/// Render a SymbolAliasMap.
raw_ostream &operator<<(raw_ostream &OS, const SymbolAliasMap &Aliases);

/// Render a SymbolState.
raw_ostream &operator<<(raw_ostream &OS, const SymbolState &S);

/// Render a LookupKind.
raw_ostream &operator<<(raw_ostream &OS, const LookupKind &K);

/// Dump a SymbolStringPool. Useful for debugging dangling-pointer crashes.
raw_ostream &operator<<(raw_ostream &OS, const SymbolStringPool &SSP);

/// A function object that can be used as an ObjectTransformLayer transform
/// to dump object files to disk at a specified path.
class DumpObjects {
public:
  /// Construct a DumpObjects transform that will dump objects to disk.
  ///
  /// @param DumpDir specifies the path to write dumped objects to. DumpDir may
  /// be empty, in which case files will be dumped to the working directory. If
  /// DumpDir is non-empty then any trailing separators will be discarded.
  ///
  /// @param IdentifierOverride specifies a file name stem to use when dumping
  /// objects. If empty, each MemoryBuffer's identifier will be used (with a .o
  /// suffix added if not already present). If an identifier override is
  /// supplied it will be used instead (since all buffers will use the same
  /// identifier, the resulting files will be named <ident>.o, <ident>.2.o,
  /// <ident>.3.o, and so on). IdentifierOverride should not contain an
  /// extension, as a .o suffix will be added by DumpObjects.
  DumpObjects(std::string DumpDir = "", std::string IdentifierOverride = "");

  /// Dumps the given buffer to disk.
  Expected<std::unique_ptr<MemoryBuffer>>
  operator()(std::unique_ptr<MemoryBuffer> Obj);

private:
  StringRef getBufferIdentifier(MemoryBuffer &B);
  std::string DumpDir;
  std::string IdentifierOverride;
};

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_DEBUGUTILS_H
