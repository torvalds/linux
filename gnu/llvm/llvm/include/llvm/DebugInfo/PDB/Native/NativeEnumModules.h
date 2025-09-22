//==- NativeEnumModules.h - Native Module Enumerator impl --------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMMODULES_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMMODULES_H

#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
namespace llvm {
namespace pdb {

class NativeSession;

class NativeEnumModules : public IPDBEnumChildren<PDBSymbol> {
public:
  NativeEnumModules(NativeSession &Session, uint32_t Index = 0);

  uint32_t getChildCount() const override;
  std::unique_ptr<PDBSymbol> getChildAtIndex(uint32_t Index) const override;
  std::unique_ptr<PDBSymbol> getNext() override;
  void reset() override;

private:
  NativeSession &Session;
  uint32_t Index;
};
}
}

#endif
