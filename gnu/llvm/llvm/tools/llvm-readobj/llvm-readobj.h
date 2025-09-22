//===-- llvm-readobj.h ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_READOBJ_LLVM_READOBJ_H
#define LLVM_TOOLS_LLVM_READOBJ_LLVM_READOBJ_H

#include "ObjDumper.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"

namespace llvm {
  namespace object {
    class RelocationRef;
  }

  // Various helper functions.
  [[noreturn]] void reportError(Error Err, StringRef Input);
  void reportWarning(Error Err, StringRef Input);

  template <class T> T unwrapOrError(StringRef Input, Expected<T> EO) {
    if (EO)
      return *EO;
    reportError(EO.takeError(), Input);
  }
} // namespace llvm

namespace opts {
extern bool SectionRelocations;
extern bool SectionSymbols;
extern bool SectionData;
extern bool ExpandRelocs;
extern bool CodeViewSubsectionBytes;
extern bool Demangle;
enum OutputStyleTy { LLVM, GNU, JSON, UNKNOWN };
extern OutputStyleTy Output;
} // namespace opts

#define LLVM_READOBJ_ENUM_ENT(ns, enum) \
  { #enum, ns::enum }

#define LLVM_READOBJ_ENUM_CLASS_ENT(enum_class, enum) \
  { #enum, std::underlying_type_t<enum_class>(enum_class::enum) }

#endif
