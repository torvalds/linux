//===- PrettyFunctionDumper.h --------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PRETTYFUNCTIONDUMPER_H
#define LLVM_TOOLS_LLVMPDBDUMP_PRETTYFUNCTIONDUMPER_H

#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

namespace llvm {
namespace pdb {
class LinePrinter;

class FunctionDumper : public PDBSymDumper {
public:
  FunctionDumper(LinePrinter &P);

  enum class PointerType { None, Pointer, Reference };

  void start(const PDBSymbolTypeFunctionSig &Symbol, const char *Name,
             PointerType Pointer);
  void start(const PDBSymbolFunc &Symbol, PointerType Pointer);

  void dump(const PDBSymbolTypeArray &Symbol) override;
  void dump(const PDBSymbolTypeBuiltin &Symbol) override;
  void dump(const PDBSymbolTypeEnum &Symbol) override;
  void dump(const PDBSymbolTypeFunctionArg &Symbol) override;
  void dump(const PDBSymbolTypePointer &Symbol) override;
  void dump(const PDBSymbolTypeTypedef &Symbol) override;
  void dump(const PDBSymbolTypeUDT &Symbol) override;

private:
  LinePrinter &Printer;
};
}
}

#endif
