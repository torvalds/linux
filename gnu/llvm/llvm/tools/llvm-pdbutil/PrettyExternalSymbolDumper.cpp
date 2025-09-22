//===- PrettyExternalSymbolDumper.cpp -------------------------- *- C++ *-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PrettyExternalSymbolDumper.h"

#include "llvm/DebugInfo/PDB/ConcreteSymbolEnumerator.h"
#include "llvm/DebugInfo/PDB/Native/LinePrinter.h"
#include "llvm/DebugInfo/PDB/PDBSymbolExe.h"
#include "llvm/DebugInfo/PDB/PDBSymbolPublicSymbol.h"
#include "llvm/Support/Format.h"

using namespace llvm;
using namespace llvm::pdb;

ExternalSymbolDumper::ExternalSymbolDumper(LinePrinter &P)
    : PDBSymDumper(true), Printer(P) {}

void ExternalSymbolDumper::start(const PDBSymbolExe &Symbol) {
  if (auto Vars = Symbol.findAllChildren<PDBSymbolPublicSymbol>()) {
    while (auto Var = Vars->getNext())
      Var->dump(*this);
  }
}

void ExternalSymbolDumper::dump(const PDBSymbolPublicSymbol &Symbol) {
  std::string LinkageName = Symbol.getName();
  if (Printer.IsSymbolExcluded(LinkageName))
    return;

  Printer.NewLine();
  uint64_t Addr = Symbol.getVirtualAddress();

  Printer << "public [";
  WithColor(Printer, PDB_ColorItem::Address).get() << format_hex(Addr, 10);
  Printer << "] ";
  WithColor(Printer, PDB_ColorItem::Identifier).get() << LinkageName;
}
