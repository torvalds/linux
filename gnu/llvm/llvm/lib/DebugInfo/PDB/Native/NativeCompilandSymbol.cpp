//===- NativeCompilandSymbol.cpp - Native impl for compilands ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeCompilandSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"

namespace llvm {
namespace pdb {

NativeCompilandSymbol::NativeCompilandSymbol(NativeSession &Session,
                                             SymIndexId SymbolId,
                                             DbiModuleDescriptor MI)
    : NativeRawSymbol(Session, PDB_SymType::Compiland, SymbolId), Module(MI) {}

PDB_SymType NativeCompilandSymbol::getSymTag() const {
  return PDB_SymType::Compiland;
}

void NativeCompilandSymbol::dump(raw_ostream &OS, int Indent,
                                 PdbSymbolIdField ShowIdFields,
                                 PdbSymbolIdField RecurseIdFields) const {
  NativeRawSymbol::dump(OS, Indent, ShowIdFields, RecurseIdFields);

  dumpSymbolIdField(OS, "lexicalParentId", 0, Indent, Session,
                    PdbSymbolIdField::LexicalParent, ShowIdFields,
                    RecurseIdFields);
  dumpSymbolField(OS, "libraryName", getLibraryName(), Indent);
  dumpSymbolField(OS, "name", getName(), Indent);
  dumpSymbolField(OS, "editAndContinueEnabled", isEditAndContinueEnabled(),
                  Indent);
}

bool NativeCompilandSymbol::isEditAndContinueEnabled() const {
  return Module.hasECInfo();
}

SymIndexId NativeCompilandSymbol::getLexicalParentId() const { return 0; }

// The usage of getObjFileName for getLibraryName and getModuleName for getName
// may seem backwards, but it is consistent with DIA, which is what this API
// was modeled after.  We may rename these methods later to try to eliminate
// this potential confusion.

std::string NativeCompilandSymbol::getLibraryName() const {
  return std::string(Module.getObjFileName());
}

std::string NativeCompilandSymbol::getName() const {
  return std::string(Module.getModuleName());
}

} // namespace pdb
} // namespace llvm
