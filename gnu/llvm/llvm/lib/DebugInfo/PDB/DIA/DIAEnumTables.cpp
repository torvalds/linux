//===- DIAEnumTables.cpp - DIA Table Enumerator Impl ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIAEnumTables.h"
#include "llvm/DebugInfo/PDB/DIA/DIATable.h"

using namespace llvm;
using namespace llvm::pdb;

DIAEnumTables::DIAEnumTables(CComPtr<IDiaEnumTables> DiaEnumerator)
    : Enumerator(DiaEnumerator) {}

uint32_t DIAEnumTables::getChildCount() const {
  LONG Count = 0;
  return (S_OK == Enumerator->get_Count(&Count)) ? Count : 0;
}

std::unique_ptr<IPDBTable>
DIAEnumTables::getChildAtIndex(uint32_t Index) const {
  CComPtr<IDiaTable> Item;
  VARIANT Var;
  Var.vt = VT_UINT;
  Var.uintVal = Index;
  if (S_OK != Enumerator->Item(Var, &Item))
    return nullptr;

  return std::unique_ptr<IPDBTable>(new DIATable(Item));
}

std::unique_ptr<IPDBTable> DIAEnumTables::getNext() {
  CComPtr<IDiaTable> Item;
  ULONG CeltFetched = 0;
  if (S_OK != Enumerator->Next(1, &Item, &CeltFetched))
    return nullptr;

  return std::unique_ptr<IPDBTable>(new DIATable(Item));
}

void DIAEnumTables::reset() { Enumerator->Reset(); }
