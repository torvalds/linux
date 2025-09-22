//===- Win64EHDumper.h - Win64 EH Printing ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_READOBJ_WIN64EHDUMPER_H
#define LLVM_TOOLS_LLVM_READOBJ_WIN64EHDUMPER_H

#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/Win64EH.h"

namespace llvm {
namespace object {
class COFFObjectFile;
class SymbolRef;
struct coff_section;
}

namespace Win64EH {
class Dumper {
  ScopedPrinter &SW;
  raw_ostream &OS;

public:
  typedef std::error_code (*SymbolResolver)(const object::coff_section *,
                                            uint64_t, object::SymbolRef &,
                                            void *);

  struct Context {
    const object::COFFObjectFile &COFF;
    SymbolResolver ResolveSymbol;
    void *UserData;

    Context(const object::COFFObjectFile &COFF, SymbolResolver Resolver,
            void *UserData)
      : COFF(COFF), ResolveSymbol(Resolver), UserData(UserData) {}
  };

private:
  void printRuntimeFunctionEntry(const Context &Ctx,
                                 const object::coff_section *Section,
                                 uint64_t SectionOffset,
                                 const RuntimeFunction &RF);
  void printUnwindCode(const UnwindInfo& UI, ArrayRef<UnwindCode> UC);
  void printUnwindInfo(const Context &Ctx, const object::coff_section *Section,
                       off_t Offset, const UnwindInfo &UI);
  void printRuntimeFunction(const Context &Ctx,
                            const object::coff_section *Section,
                            uint64_t SectionOffset, const RuntimeFunction &RF);

public:
  Dumper(ScopedPrinter &SW) : SW(SW), OS(SW.getOStream()) {}

  void printData(const Context &Ctx);
};
}
}

#endif
