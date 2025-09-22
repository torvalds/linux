//==- DIAEnumLineNumbers.cpp - DIA Line Number Enumerator impl ---*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIAEnumLineNumbers.h"
#include "llvm/DebugInfo/PDB/DIA/DIALineNumber.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"

using namespace llvm;
using namespace llvm::pdb;

DIAEnumLineNumbers::DIAEnumLineNumbers(
    CComPtr<IDiaEnumLineNumbers> DiaEnumerator)
    : Enumerator(DiaEnumerator) {}

uint32_t DIAEnumLineNumbers::getChildCount() const {
  LONG Count = 0;
  return (S_OK == Enumerator->get_Count(&Count)) ? Count : 0;
}

std::unique_ptr<IPDBLineNumber>
DIAEnumLineNumbers::getChildAtIndex(uint32_t Index) const {
  CComPtr<IDiaLineNumber> Item;
  if (S_OK != Enumerator->Item(Index, &Item))
    return nullptr;

  return std::unique_ptr<IPDBLineNumber>(new DIALineNumber(Item));
}

std::unique_ptr<IPDBLineNumber> DIAEnumLineNumbers::getNext() {
  CComPtr<IDiaLineNumber> Item;
  ULONG NumFetched = 0;
  if (S_OK != Enumerator->Next(1, &Item, &NumFetched))
    return nullptr;

  return std::unique_ptr<IPDBLineNumber>(new DIALineNumber(Item));
}

void DIAEnumLineNumbers::reset() { Enumerator->Reset(); }
