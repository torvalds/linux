//==- DIAEnumSectionContribs.h --------------------------------- -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIAENUMSECTIONCONTRIBS_H
#define LLVM_DEBUGINFO_PDB_DIA_DIAENUMSECTIONCONTRIBS_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBSectionContrib.h"

namespace llvm {
namespace pdb {
class DIASession;

class DIAEnumSectionContribs : public IPDBEnumChildren<IPDBSectionContrib> {
public:
  explicit DIAEnumSectionContribs(
      const DIASession &PDBSession,
      CComPtr<IDiaEnumSectionContribs> DiaEnumerator);

  uint32_t getChildCount() const override;
  ChildTypePtr getChildAtIndex(uint32_t Index) const override;
  ChildTypePtr getNext() override;
  void reset() override;

private:
  const DIASession &Session;
  CComPtr<IDiaEnumSectionContribs> Enumerator;
};
} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_DIA_DIAENUMSECTIONCONTRIBS_H
