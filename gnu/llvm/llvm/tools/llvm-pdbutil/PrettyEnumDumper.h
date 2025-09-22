//===- PrettyEnumDumper.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PRETTYENUMDUMPER_H
#define LLVM_TOOLS_LLVMPDBDUMP_PRETTYENUMDUMPER_H

#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

namespace llvm {
namespace pdb {

class LinePrinter;

class EnumDumper : public PDBSymDumper {
public:
  EnumDumper(LinePrinter &P);

  void start(const PDBSymbolTypeEnum &Symbol);

private:
  LinePrinter &Printer;
};
}
}
#endif
