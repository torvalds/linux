//===- PrettyTypeDumper.h - PDBSymDumper implementation for types *- C++ *-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PRETTYTYPEDUMPER_H
#define LLVM_TOOLS_LLVMPDBDUMP_PRETTYTYPEDUMPER_H

#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

namespace llvm {
namespace pdb {
class LinePrinter;
class ClassLayout;

class TypeDumper : public PDBSymDumper {
public:
  TypeDumper(LinePrinter &P);

  void start(const PDBSymbolExe &Exe);

  void dump(const PDBSymbolTypeEnum &Symbol) override;
  void dump(const PDBSymbolTypeTypedef &Symbol) override;
  void dump(const PDBSymbolTypeFunctionSig &Symbol) override;
  void dump(const PDBSymbolTypeArray &Symbol) override;
  void dump(const PDBSymbolTypeBuiltin &Symbol) override;
  void dump(const PDBSymbolTypePointer &Symbol) override;
  void dump(const PDBSymbolTypeVTableShape &Symbol) override;
  void dump(const PDBSymbolTypeUDT &Symbol) override;

  void dumpClassLayout(const ClassLayout &Class);

private:
  LinePrinter &Printer;
};
}
}
#endif
