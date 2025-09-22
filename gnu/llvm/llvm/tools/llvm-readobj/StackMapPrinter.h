//===-------- StackMapPrinter.h - Pretty-print stackmaps --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_READOBJ_STACKMAPPRINTER_H
#define LLVM_TOOLS_LLVM_READOBJ_STACKMAPPRINTER_H

#include "llvm/Object/StackMapParser.h"
#include "llvm/Support/ScopedPrinter.h"

namespace llvm {

// Pretty print a stackmap to the given ostream.
template <typename StackMapParserT>
void prettyPrintStackMap(ScopedPrinter &W, const StackMapParserT &SMP) {

  W.printNumber("LLVM StackMap Version",  SMP.getVersion());
  W.printNumber("Num Functions", SMP.getNumFunctions());

  // Functions:
  for (const auto &F : SMP.functions())
    W.startLine() << "  Function address: " << F.getFunctionAddress()
       << ", stack size: " << F.getStackSize()
       << ", callsite record count: " << F.getRecordCount() << "\n";

  // Constants:
  W.printNumber("Num Constants", SMP.getNumConstants());
  unsigned ConstantIndex = 0;
  for (const auto &C : SMP.constants())
    W.startLine() << "  #" << ++ConstantIndex << ": " << C.getValue() << "\n";

  // Records:
  W.printNumber("Num Records", SMP.getNumRecords());
  for (const auto &R : SMP.records()) {
    W.startLine() << "  Record ID: " << R.getID()
                  << ", instruction offset: " << R.getInstructionOffset()
                  << "\n";
    W.startLine() << "    " << R.getNumLocations() << " locations:\n";

    unsigned LocationIndex = 0;
    for (const auto &Loc : R.locations()) {
      raw_ostream &OS = W.startLine();
      OS << "      #" << ++LocationIndex << ": ";
      switch (Loc.getKind()) {
      case StackMapParserT::LocationKind::Register:
        OS << "Register R#" << Loc.getDwarfRegNum();
        break;
      case StackMapParserT::LocationKind::Direct:
        OS << "Direct R#" << Loc.getDwarfRegNum() << " + " << Loc.getOffset();
        break;
      case StackMapParserT::LocationKind::Indirect:
        OS << "Indirect [R#" << Loc.getDwarfRegNum() << " + " << Loc.getOffset()
           << "]";
        break;
      case StackMapParserT::LocationKind::Constant:
        OS << "Constant " << Loc.getSmallConstant();
        break;
      case StackMapParserT::LocationKind::ConstantIndex:
        OS << "ConstantIndex #" << Loc.getConstantIndex() << " ("
           << SMP.getConstant(Loc.getConstantIndex()).getValue() << ")";
        break;
      }
      OS << ", size: " << Loc.getSizeInBytes() << "\n";
    }

    raw_ostream &OS = W.startLine();
    OS << "    " << R.getNumLiveOuts() << " live-outs: [ ";
    for (const auto &LO : R.liveouts())
      OS << "R#" << LO.getDwarfRegNum() << " ("
         << LO.getSizeInBytes() << "-bytes) ";
    OS << "]\n";
  }
}

}

#endif
