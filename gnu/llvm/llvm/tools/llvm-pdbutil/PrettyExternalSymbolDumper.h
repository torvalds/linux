//===- PrettyExternalSymbolDumper.h --------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PRETTYEXTERNALSYMBOLDUMPER_H
#define LLVM_TOOLS_LLVMPDBDUMP_PRETTYEXTERNALSYMBOLDUMPER_H

#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

namespace llvm {
namespace pdb {

class LinePrinter;

class ExternalSymbolDumper : public PDBSymDumper {
public:
  ExternalSymbolDumper(LinePrinter &P);

  void start(const PDBSymbolExe &Symbol);

  void dump(const PDBSymbolPublicSymbol &Symbol) override;

private:
  LinePrinter &Printer;
};
}
}

#endif
