//==- NativeEnumInjectedSources.cpp - Native Injected Source Enumerator --*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMINJECTEDSOURCES_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMINJECTEDSOURCES_H

#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBInjectedSource.h"
#include "llvm/DebugInfo/PDB/Native/InjectedSourceStream.h"

namespace llvm {
namespace pdb {

class InjectedSourceStream;
class PDBFile;
class PDBStringTable;

class NativeEnumInjectedSources : public IPDBEnumChildren<IPDBInjectedSource> {
public:
  NativeEnumInjectedSources(PDBFile &File, const InjectedSourceStream &IJS,
                            const PDBStringTable &Strings);

  uint32_t getChildCount() const override;
  std::unique_ptr<IPDBInjectedSource>
  getChildAtIndex(uint32_t Index) const override;
  std::unique_ptr<IPDBInjectedSource> getNext() override;
  void reset() override;

private:
  PDBFile &File;
  const InjectedSourceStream &Stream;
  const PDBStringTable &Strings;
  InjectedSourceStream::const_iterator Cur;
};

} // namespace pdb
} // namespace llvm

#endif
