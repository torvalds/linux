//===- PrettyClassDefinitionDumper.cpp --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PrettyClassDefinitionDumper.h"

#include "PrettyClassLayoutGraphicalDumper.h"
#include "llvm-pdbutil.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBaseClass.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"
#include "llvm/DebugInfo/PDB/UDTLayout.h"

#include "llvm/Support/Format.h"

using namespace llvm;
using namespace llvm::pdb;

ClassDefinitionDumper::ClassDefinitionDumper(LinePrinter &P)
    : PDBSymDumper(true), Printer(P) {}

void ClassDefinitionDumper::start(const PDBSymbolTypeUDT &Class) {
  assert(opts::pretty::ClassFormat !=
         opts::pretty::ClassDefinitionFormat::None);

  ClassLayout Layout(Class);
  start(Layout);
}

void ClassDefinitionDumper::start(const ClassLayout &Layout) {
  prettyPrintClassIntro(Layout);

  PrettyClassLayoutGraphicalDumper Dumper(Printer, 1, 0);
  DumpedAnything |= Dumper.start(Layout);

  prettyPrintClassOutro(Layout);
}

void ClassDefinitionDumper::prettyPrintClassIntro(const ClassLayout &Layout) {
  DumpedAnything = false;
  Printer.NewLine();

  uint32_t Size = Layout.getSize();
  const PDBSymbolTypeUDT &Class = Layout.getClass();

  if (Layout.getClass().isConstType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "const ";
  if (Layout.getClass().isVolatileType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "volatile ";
  if (Layout.getClass().isUnalignedType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "unaligned ";

  WithColor(Printer, PDB_ColorItem::Keyword).get() << Class.getUdtKind() << " ";
  WithColor(Printer, PDB_ColorItem::Type).get() << Class.getName();
  WithColor(Printer, PDB_ColorItem::Comment).get() << " [sizeof = " << Size
                                                   << "]";
  uint32_t BaseCount = Layout.bases().size();
  if (BaseCount > 0) {
    Printer.Indent();
    char NextSeparator = ':';
    for (auto *BC : Layout.bases()) {
      const auto &Base = BC->getBase();
      if (Base.isIndirectVirtualBaseClass())
        continue;

      Printer.NewLine();
      Printer << NextSeparator << " ";
      WithColor(Printer, PDB_ColorItem::Keyword).get() << Base.getAccess();
      if (BC->isVirtualBase())
        WithColor(Printer, PDB_ColorItem::Keyword).get() << " virtual";

      WithColor(Printer, PDB_ColorItem::Type).get() << " " << Base.getName();
      NextSeparator = ',';
    }

    Printer.Unindent();
  }

  Printer << " {";
  Printer.Indent();
}

void ClassDefinitionDumper::prettyPrintClassOutro(const ClassLayout &Layout) {
  Printer.Unindent();
  if (DumpedAnything)
    Printer.NewLine();
  Printer << "}";
  Printer.NewLine();
  if (Layout.deepPaddingSize() > 0) {
    APFloat Pct(100.0 * (double)Layout.deepPaddingSize() /
                (double)Layout.getSize());
    SmallString<8> PctStr;
    Pct.toString(PctStr, 4);
    WithColor(Printer, PDB_ColorItem::Padding).get()
        << "Total padding " << Layout.deepPaddingSize() << " bytes (" << PctStr
        << "% of class size)";
    Printer.NewLine();
    APFloat Pct2(100.0 * (double)Layout.immediatePadding() /
                 (double)Layout.getSize());
    PctStr.clear();
    Pct2.toString(PctStr, 4);
    WithColor(Printer, PDB_ColorItem::Padding).get()
        << "Immediate padding " << Layout.immediatePadding() << " bytes ("
        << PctStr << "% of class size)";
    Printer.NewLine();
  }
}
