//===- PrettyEnumDumper.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PrettyEnumDumper.h"

#include "PrettyBuiltinDumper.h"
#include "llvm-pdbutil.h"

#include "llvm/DebugInfo/PDB/ConcreteSymbolEnumerator.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"

using namespace llvm;
using namespace llvm::pdb;

EnumDumper::EnumDumper(LinePrinter &P) : PDBSymDumper(true), Printer(P) {}

void EnumDumper::start(const PDBSymbolTypeEnum &Symbol) {
  if (Symbol.getUnmodifiedTypeId() != 0) {
    if (Symbol.isConstType())
      WithColor(Printer, PDB_ColorItem::Keyword).get() << "const ";
    if (Symbol.isVolatileType())
      WithColor(Printer, PDB_ColorItem::Keyword).get() << "volatile ";
    if (Symbol.isUnalignedType())
      WithColor(Printer, PDB_ColorItem::Keyword).get() << "unaligned ";
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "enum ";
    WithColor(Printer, PDB_ColorItem::Type).get() << Symbol.getName();
    return;
  }

  WithColor(Printer, PDB_ColorItem::Keyword).get() << "enum ";
  WithColor(Printer, PDB_ColorItem::Type).get() << Symbol.getName();
  if (!opts::pretty::NoEnumDefs) {
    auto UnderlyingType = Symbol.getUnderlyingType();
    if (!UnderlyingType)
      return;
    if (UnderlyingType->getBuiltinType() != PDB_BuiltinType::Int ||
        UnderlyingType->getLength() != 4) {
      Printer << " : ";
      BuiltinDumper Dumper(Printer);
      Dumper.start(*UnderlyingType);
    }
    auto EnumValues = Symbol.findAllChildren<PDBSymbolData>();
    Printer << " {";
    Printer.Indent();
    if (EnumValues && EnumValues->getChildCount() > 0) {
      while (auto EnumValue = EnumValues->getNext()) {
        if (EnumValue->getDataKind() != PDB_DataKind::Constant)
          continue;
        Printer.NewLine();
        WithColor(Printer, PDB_ColorItem::Identifier).get()
            << EnumValue->getName();
        Printer << " = ";
        WithColor(Printer, PDB_ColorItem::LiteralValue).get()
            << EnumValue->getValue();
      }
    }
    Printer.Unindent();
    Printer.NewLine();
    Printer << "}";
  }
}
