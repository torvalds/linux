//===- PrettyCompilandDumper.h - llvm-pdbutil compiland dumper -*- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PRETTYCOMPILANDDUMPER_H
#define LLVM_TOOLS_LLVMPDBDUMP_PRETTYCOMPILANDDUMPER_H

#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

namespace llvm {
namespace pdb {

class LinePrinter;

typedef int CompilandDumpFlags;
class CompilandDumper : public PDBSymDumper {
public:
  enum Flags { None = 0x0, Children = 0x1, Symbols = 0x2, Lines = 0x4 };

  CompilandDumper(LinePrinter &P);

  void start(const PDBSymbolCompiland &Symbol, CompilandDumpFlags flags);

  void dump(const PDBSymbolCompilandDetails &Symbol) override;
  void dump(const PDBSymbolCompilandEnv &Symbol) override;
  void dump(const PDBSymbolData &Symbol) override;
  void dump(const PDBSymbolFunc &Symbol) override;
  void dump(const PDBSymbolLabel &Symbol) override;
  void dump(const PDBSymbolThunk &Symbol) override;
  void dump(const PDBSymbolTypeTypedef &Symbol) override;
  void dump(const PDBSymbolUnknown &Symbol) override;
  void dump(const PDBSymbolUsingNamespace &Symbol) override;

private:
  LinePrinter &Printer;
};
}
}

#endif
