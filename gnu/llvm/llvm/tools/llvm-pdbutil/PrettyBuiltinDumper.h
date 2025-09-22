//===- PrettyBuiltinDumper.h ---------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PRETTYBUILTINDUMPER_H
#define LLVM_TOOLS_LLVMPDBDUMP_PRETTYBUILTINDUMPER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

namespace llvm {
namespace pdb {

class LinePrinter;

class BuiltinDumper : public PDBSymDumper {
public:
  BuiltinDumper(LinePrinter &P);

  void start(const PDBSymbolTypeBuiltin &Symbol);

private:
  StringRef getTypeName(const PDBSymbolTypeBuiltin &Symbol);

  LinePrinter &Printer;
};
}
}

#endif
