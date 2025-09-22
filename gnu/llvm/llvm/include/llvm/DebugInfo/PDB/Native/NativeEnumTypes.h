//==- NativeEnumTypes.h - Native Type Enumerator impl ------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMTYPES_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMTYPES_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"

#include <vector>

namespace llvm {
namespace codeview {
class LazyRandomTypeCollection;
}
namespace pdb {

class NativeSession;

class NativeEnumTypes : public IPDBEnumChildren<PDBSymbol> {
public:
  NativeEnumTypes(NativeSession &Session,
                  codeview::LazyRandomTypeCollection &TypeCollection,
                  std::vector<codeview::TypeLeafKind> Kinds);

  NativeEnumTypes(NativeSession &Session,
                  std::vector<codeview::TypeIndex> Indices);

  uint32_t getChildCount() const override;
  std::unique_ptr<PDBSymbol> getChildAtIndex(uint32_t Index) const override;
  std::unique_ptr<PDBSymbol> getNext() override;
  void reset() override;

private:
  std::vector<codeview::TypeIndex> Matches;
  uint32_t Index;
  NativeSession &Session;
};

} // namespace pdb
} // namespace llvm

#endif
