//===- NativePublicSymbol.cpp - info about public symbols -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativePublicSymbol.h"

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativePublicSymbol::NativePublicSymbol(NativeSession &Session, SymIndexId Id,
                                       const codeview::PublicSym32 &Sym)
    : NativeRawSymbol(Session, PDB_SymType::PublicSymbol, Id), Sym(Sym) {}

NativePublicSymbol::~NativePublicSymbol() = default;

void NativePublicSymbol::dump(raw_ostream &OS, int Indent,
                              PdbSymbolIdField ShowIdFields,
                              PdbSymbolIdField RecurseIdFields) const {
  NativeRawSymbol::dump(OS, Indent, ShowIdFields, RecurseIdFields);
  dumpSymbolField(OS, "name", getName(), Indent);
  dumpSymbolField(OS, "offset", getAddressOffset(), Indent);
  dumpSymbolField(OS, "section", getAddressSection(), Indent);
}

uint32_t NativePublicSymbol::getAddressOffset() const { return Sym.Offset; }

uint32_t NativePublicSymbol::getAddressSection() const { return Sym.Segment; }

std::string NativePublicSymbol::getName() const {
  return std::string(Sym.Name);
}

uint32_t NativePublicSymbol::getRelativeVirtualAddress() const {
  return Session.getRVAFromSectOffset(Sym.Segment, Sym.Offset);
}

uint64_t NativePublicSymbol::getVirtualAddress() const {
  return Session.getVAFromSectOffset(Sym.Segment, Sym.Offset);
}
