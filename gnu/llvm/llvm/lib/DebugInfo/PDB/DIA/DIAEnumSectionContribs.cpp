//==- DIAEnumSectionContribs.cpp ---------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIAEnumSectionContribs.h"
#include "llvm/DebugInfo/PDB/DIA/DIASectionContrib.h"
#include "llvm/DebugInfo/PDB/DIA/DIASession.h"

using namespace llvm;
using namespace llvm::pdb;

DIAEnumSectionContribs::DIAEnumSectionContribs(
    const DIASession &PDBSession,
    CComPtr<IDiaEnumSectionContribs> DiaEnumerator)
    : Session(PDBSession), Enumerator(DiaEnumerator) {}

uint32_t DIAEnumSectionContribs::getChildCount() const {
  LONG Count = 0;
  return (S_OK == Enumerator->get_Count(&Count)) ? Count : 0;
}

std::unique_ptr<IPDBSectionContrib>
DIAEnumSectionContribs::getChildAtIndex(uint32_t Index) const {
  CComPtr<IDiaSectionContrib> Item;
  if (S_OK != Enumerator->Item(Index, &Item))
    return nullptr;

  return std::unique_ptr<IPDBSectionContrib>(
      new DIASectionContrib(Session, Item));
}

std::unique_ptr<IPDBSectionContrib> DIAEnumSectionContribs::getNext() {
  CComPtr<IDiaSectionContrib> Item;
  ULONG NumFetched = 0;
  if (S_OK != Enumerator->Next(1, &Item, &NumFetched))
    return nullptr;

  return std::unique_ptr<IPDBSectionContrib>(
      new DIASectionContrib(Session, Item));
}

void DIAEnumSectionContribs::reset() { Enumerator->Reset(); }
