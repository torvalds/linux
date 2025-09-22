//===- PrettyTypeDumper.cpp - PDBSymDumper type dumper *------------ C++ *-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PrettyTypeDumper.h"

#include "PrettyBuiltinDumper.h"
#include "PrettyClassDefinitionDumper.h"
#include "PrettyEnumDumper.h"
#include "PrettyFunctionDumper.h"
#include "PrettyTypedefDumper.h"
#include "llvm-pdbutil.h"

#include "llvm/DebugInfo/PDB/ConcreteSymbolEnumerator.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/PDBSymbolExe.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeArray.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypePointer.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeTypedef.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeVTableShape.h"
#include "llvm/DebugInfo/PDB/UDTLayout.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::pdb;

using LayoutPtr = std::unique_ptr<ClassLayout>;

typedef bool (*CompareFunc)(const LayoutPtr &S1, const LayoutPtr &S2);

static bool CompareNames(const LayoutPtr &S1, const LayoutPtr &S2) {
  return S1->getName() < S2->getName();
}

static bool CompareSizes(const LayoutPtr &S1, const LayoutPtr &S2) {
  return S1->getSize() < S2->getSize();
}

static bool ComparePadding(const LayoutPtr &S1, const LayoutPtr &S2) {
  return S1->deepPaddingSize() < S2->deepPaddingSize();
}

static bool ComparePaddingPct(const LayoutPtr &S1, const LayoutPtr &S2) {
  double Pct1 = (double)S1->deepPaddingSize() / (double)S1->getSize();
  double Pct2 = (double)S2->deepPaddingSize() / (double)S2->getSize();
  return Pct1 < Pct2;
}

static bool ComparePaddingImmediate(const LayoutPtr &S1, const LayoutPtr &S2) {
  return S1->immediatePadding() < S2->immediatePadding();
}

static bool ComparePaddingPctImmediate(const LayoutPtr &S1,
                                       const LayoutPtr &S2) {
  double Pct1 = (double)S1->immediatePadding() / (double)S1->getSize();
  double Pct2 = (double)S2->immediatePadding() / (double)S2->getSize();
  return Pct1 < Pct2;
}

static CompareFunc getComparisonFunc(opts::pretty::ClassSortMode Mode) {
  switch (Mode) {
  case opts::pretty::ClassSortMode::Name:
    return CompareNames;
  case opts::pretty::ClassSortMode::Size:
    return CompareSizes;
  case opts::pretty::ClassSortMode::Padding:
    return ComparePadding;
  case opts::pretty::ClassSortMode::PaddingPct:
    return ComparePaddingPct;
  case opts::pretty::ClassSortMode::PaddingImmediate:
    return ComparePaddingImmediate;
  case opts::pretty::ClassSortMode::PaddingPctImmediate:
    return ComparePaddingPctImmediate;
  default:
    return nullptr;
  }
}

template <typename Enumerator>
static std::vector<std::unique_ptr<ClassLayout>>
filterAndSortClassDefs(LinePrinter &Printer, Enumerator &E,
                       uint32_t UnfilteredCount) {
  std::vector<std::unique_ptr<ClassLayout>> Filtered;

  Filtered.reserve(UnfilteredCount);
  CompareFunc Comp = getComparisonFunc(opts::pretty::ClassOrder);

  if (UnfilteredCount > 10000) {
    errs() << formatv("Filtering and sorting {0} types", UnfilteredCount);
    errs().flush();
  }
  uint32_t Examined = 0;
  uint32_t Discarded = 0;
  while (auto Class = E.getNext()) {
    ++Examined;
    if (Examined % 10000 == 0) {
      errs() << formatv("Examined {0}/{1} items.  {2} items discarded\n",
                        Examined, UnfilteredCount, Discarded);
      errs().flush();
    }

    if (Class->getUnmodifiedTypeId() != 0) {
      ++Discarded;
      continue;
    }

    if (Printer.IsTypeExcluded(Class->getName(), Class->getLength())) {
      ++Discarded;
      continue;
    }

    auto Layout = std::make_unique<ClassLayout>(std::move(Class));
    if (Layout->deepPaddingSize() < opts::pretty::PaddingThreshold) {
      ++Discarded;
      continue;
    }
    if (Layout->immediatePadding() < opts::pretty::ImmediatePaddingThreshold) {
      ++Discarded;
      continue;
    }

    Filtered.push_back(std::move(Layout));
  }

  if (Comp)
    llvm::sort(Filtered, Comp);
  return Filtered;
}

TypeDumper::TypeDumper(LinePrinter &P) : PDBSymDumper(true), Printer(P) {}

template <typename T>
static bool isTypeExcluded(LinePrinter &Printer, const T &Symbol) {
  return false;
}

static bool isTypeExcluded(LinePrinter &Printer,
                           const PDBSymbolTypeEnum &Enum) {
  if (Printer.IsTypeExcluded(Enum.getName(), Enum.getLength()))
    return true;
  // Dump member enums when dumping their class definition.
  if (nullptr != Enum.getClassParent())
    return true;
  return false;
}

static bool isTypeExcluded(LinePrinter &Printer,
                           const PDBSymbolTypeTypedef &Typedef) {
  return Printer.IsTypeExcluded(Typedef.getName(), Typedef.getLength());
}

template <typename SymbolT>
static void dumpSymbolCategory(LinePrinter &Printer, const PDBSymbolExe &Exe,
                               TypeDumper &TD, StringRef Label) {
  if (auto Children = Exe.findAllChildren<SymbolT>()) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::Identifier).get() << Label;
    Printer << ": (" << Children->getChildCount() << " items)";
    Printer.Indent();
    while (auto Child = Children->getNext()) {
      if (isTypeExcluded(Printer, *Child))
        continue;

      Printer.NewLine();
      Child->dump(TD);
    }
    Printer.Unindent();
  }
}

static void printClassDecl(LinePrinter &Printer,
                           const PDBSymbolTypeUDT &Class) {
  if (Class.getUnmodifiedTypeId() != 0) {
    if (Class.isConstType())
      WithColor(Printer, PDB_ColorItem::Keyword).get() << "const ";
    if (Class.isVolatileType())
      WithColor(Printer, PDB_ColorItem::Keyword).get() << "volatile ";
    if (Class.isUnalignedType())
      WithColor(Printer, PDB_ColorItem::Keyword).get() << "unaligned ";
  }
  WithColor(Printer, PDB_ColorItem::Keyword).get() << Class.getUdtKind() << " ";
  WithColor(Printer, PDB_ColorItem::Type).get() << Class.getName();
}

void TypeDumper::start(const PDBSymbolExe &Exe) {
  if (opts::pretty::Enums)
    dumpSymbolCategory<PDBSymbolTypeEnum>(Printer, Exe, *this, "Enums");

  if (opts::pretty::Funcsigs)
    dumpSymbolCategory<PDBSymbolTypeFunctionSig>(Printer, Exe, *this,
                                                 "Function Signatures");

  if (opts::pretty::Typedefs)
    dumpSymbolCategory<PDBSymbolTypeTypedef>(Printer, Exe, *this, "Typedefs");

  if (opts::pretty::Arrays)
    dumpSymbolCategory<PDBSymbolTypeArray>(Printer, Exe, *this, "Arrays");

  if (opts::pretty::Pointers)
    dumpSymbolCategory<PDBSymbolTypePointer>(Printer, Exe, *this, "Pointers");

  if (opts::pretty::VTShapes)
    dumpSymbolCategory<PDBSymbolTypeVTableShape>(Printer, Exe, *this,
                                                 "VFTable Shapes");

  if (opts::pretty::Classes) {
    if (auto Classes = Exe.findAllChildren<PDBSymbolTypeUDT>()) {
      uint32_t All = Classes->getChildCount();

      Printer.NewLine();
      WithColor(Printer, PDB_ColorItem::Identifier).get() << "Classes";

      bool Precompute = false;
      Precompute =
          (opts::pretty::ClassOrder != opts::pretty::ClassSortMode::None);

      // If we're using no sort mode, then we can start getting immediate output
      // from the tool by just filtering as we go, rather than processing
      // everything up front so that we can sort it.  This makes the tool more
      // responsive.  So only precompute the filtered/sorted set of classes if
      // necessary due to the specified options.
      std::vector<LayoutPtr> Filtered;
      uint32_t Shown = All;
      if (Precompute) {
        Filtered = filterAndSortClassDefs(Printer, *Classes, All);

        Shown = Filtered.size();
      }

      Printer << ": (Showing " << Shown << " items";
      if (Shown < All)
        Printer << ", " << (All - Shown) << " filtered";
      Printer << ")";
      Printer.Indent();

      // If we pre-computed, iterate the filtered/sorted list, otherwise iterate
      // the DIA enumerator and filter on the fly.
      if (Precompute) {
        for (auto &Class : Filtered)
          dumpClassLayout(*Class);
      } else {
        while (auto Class = Classes->getNext()) {
          if (Printer.IsTypeExcluded(Class->getName(), Class->getLength()))
            continue;

          // No point duplicating a full class layout.  Just print the modified
          // declaration and continue.
          if (Class->getUnmodifiedTypeId() != 0) {
            Printer.NewLine();
            printClassDecl(Printer, *Class);
            continue;
          }

          auto Layout = std::make_unique<ClassLayout>(std::move(Class));
          if (Layout->deepPaddingSize() < opts::pretty::PaddingThreshold)
            continue;

          dumpClassLayout(*Layout);
        }
      }

      Printer.Unindent();
    }
  }
}

void TypeDumper::dump(const PDBSymbolTypeEnum &Symbol) {
  assert(opts::pretty::Enums);

  EnumDumper Dumper(Printer);
  Dumper.start(Symbol);
}

void TypeDumper::dump(const PDBSymbolTypeBuiltin &Symbol) {
  BuiltinDumper BD(Printer);
  BD.start(Symbol);
}

void TypeDumper::dump(const PDBSymbolTypeUDT &Symbol) {
  printClassDecl(Printer, Symbol);
}

void TypeDumper::dump(const PDBSymbolTypeTypedef &Symbol) {
  assert(opts::pretty::Typedefs);

  TypedefDumper Dumper(Printer);
  Dumper.start(Symbol);
}

void TypeDumper::dump(const PDBSymbolTypeArray &Symbol) {
  auto ElementType = Symbol.getElementType();

  ElementType->dump(*this);
  Printer << "[";
  WithColor(Printer, PDB_ColorItem::LiteralValue).get() << Symbol.getCount();
  Printer << "]";
}

void TypeDumper::dump(const PDBSymbolTypeFunctionSig &Symbol) {
  FunctionDumper Dumper(Printer);
  Dumper.start(Symbol, nullptr, FunctionDumper::PointerType::None);
}

void TypeDumper::dump(const PDBSymbolTypePointer &Symbol) {
  std::unique_ptr<PDBSymbol> P = Symbol.getPointeeType();

  if (auto *FS = dyn_cast<PDBSymbolTypeFunctionSig>(P.get())) {
    FunctionDumper Dumper(Printer);
    FunctionDumper::PointerType PT =
        Symbol.isReference() ? FunctionDumper::PointerType::Reference
                             : FunctionDumper::PointerType::Pointer;
    Dumper.start(*FS, nullptr, PT);
    return;
  }

  if (auto *UDT = dyn_cast<PDBSymbolTypeUDT>(P.get())) {
    printClassDecl(Printer, *UDT);
  } else if (P) {
    P->dump(*this);
  }

  if (auto Parent = Symbol.getClassParent()) {
    auto UDT = llvm::unique_dyn_cast<PDBSymbolTypeUDT>(std::move(Parent));
    if (UDT)
      Printer << " " << UDT->getName() << "::";
  }

  if (Symbol.isReference())
    Printer << "&";
  else if (Symbol.isRValueReference())
    Printer << "&&";
  else
    Printer << "*";
}

void TypeDumper::dump(const PDBSymbolTypeVTableShape &Symbol) {
  Printer.format("<vtshape ({0} methods)>", Symbol.getCount());
}

void TypeDumper::dumpClassLayout(const ClassLayout &Class) {
  assert(opts::pretty::Classes);

  if (opts::pretty::ClassFormat == opts::pretty::ClassDefinitionFormat::None) {
    WithColor(Printer, PDB_ColorItem::Keyword).get()
        << Class.getClass().getUdtKind() << " ";
    WithColor(Printer, PDB_ColorItem::Type).get() << Class.getName();
  } else {
    ClassDefinitionDumper Dumper(Printer);
    Dumper.start(Class);
  }
}
