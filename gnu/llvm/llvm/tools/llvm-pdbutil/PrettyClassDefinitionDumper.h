//===- PrettyClassDefinitionDumper.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PRETTYCLASSDEFINITIONDUMPER_H
#define LLVM_TOOLS_LLVMPDBDUMP_PRETTYCLASSDEFINITIONDUMPER_H

#include "llvm/ADT/BitVector.h"

#include "llvm/DebugInfo/PDB/PDBSymDumper.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"

#include <memory>

namespace llvm {
class BitVector;

namespace pdb {

class ClassLayout;
class LinePrinter;

class ClassDefinitionDumper : public PDBSymDumper {
public:
  ClassDefinitionDumper(LinePrinter &P);

  void start(const PDBSymbolTypeUDT &Class);
  void start(const ClassLayout &Class);

private:
  void prettyPrintClassIntro(const ClassLayout &Class);
  void prettyPrintClassOutro(const ClassLayout &Class);

  LinePrinter &Printer;
  bool DumpedAnything = false;
};
}
}
#endif
