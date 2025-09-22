//===- llvm/TableGen/TableGenBackend.h - Backend utilities ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Useful utilities for TableGen backends.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_TABLEGENBACKEND_H
#define LLVM_TABLEGEN_TABLEGENBACKEND_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/TableGen/Record.h"

namespace llvm {

class RecordKeeper;
class raw_ostream;

namespace TableGen::Emitter {
using FnT = void (*)(RecordKeeper &Records, raw_ostream &OS);

struct OptCreatorT {
  static void *call();
};

extern ManagedStatic<cl::opt<FnT>, OptCreatorT> Action;

struct Opt {
  Opt(StringRef Name, FnT CB, StringRef Desc, bool ByDefault = false) {
    if (ByDefault)
      Action->setInitialValue(CB);
    Action->getParser().addLiteralOption(Name, CB, Desc);
  }
};

template <class EmitterC> class OptClass : Opt {
  static void run(RecordKeeper &RK, raw_ostream &OS) { EmitterC(RK).run(OS); }

public:
  OptClass(StringRef Name, StringRef Desc) : Opt(Name, run, Desc) {}
};

} // namespace TableGen::Emitter

/// emitSourceFileHeader - Output an LLVM style file header to the specified
/// raw_ostream.
void emitSourceFileHeader(StringRef Desc, raw_ostream &OS,
                          const RecordKeeper &Record = RecordKeeper());

} // End llvm namespace

#endif
