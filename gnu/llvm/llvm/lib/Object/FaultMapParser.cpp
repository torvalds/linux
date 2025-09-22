//===----------------------- FaultMapParser.cpp ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/FaultMapParser.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

void printFaultType(FaultMapParser::FaultKind FT, raw_ostream &OS) {
  switch (FT) {
  default:
    llvm_unreachable("unhandled fault type!");
  case FaultMapParser::FaultingLoad:
    OS << "FaultingLoad";
    break;
  case FaultMapParser::FaultingLoadStore:
    OS << "FaultingLoadStore";
    break;
  case FaultMapParser::FaultingStore:
    OS << "FaultingStore";
    break;
  }
}

raw_ostream &
llvm::operator<<(raw_ostream &OS,
                 const FaultMapParser::FunctionFaultInfoAccessor &FFI) {
  OS << "Fault kind: ";
  printFaultType((FaultMapParser::FaultKind)FFI.getFaultKind(), OS);
  OS << ", faulting PC offset: " << FFI.getFaultingPCOffset()
     << ", handling PC offset: " << FFI.getHandlerPCOffset();
  return OS;
}

raw_ostream &llvm::operator<<(raw_ostream &OS,
                              const FaultMapParser::FunctionInfoAccessor &FI) {
  OS << "FunctionAddress: " << format_hex(FI.getFunctionAddr(), 8)
     << ", NumFaultingPCs: " << FI.getNumFaultingPCs() << "\n";
  for (unsigned I = 0, E = FI.getNumFaultingPCs(); I != E; ++I)
    OS << FI.getFunctionFaultInfoAt(I) << "\n";
  return OS;
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const FaultMapParser &FMP) {
  OS << "Version: " << format_hex(FMP.getFaultMapVersion(), 2) << "\n";
  OS << "NumFunctions: " << FMP.getNumFunctions() << "\n";

  if (FMP.getNumFunctions() == 0)
    return OS;

  FaultMapParser::FunctionInfoAccessor FI;

  for (unsigned I = 0, E = FMP.getNumFunctions(); I != E; ++I) {
    FI = (I == 0) ? FMP.getFirstFunctionInfo() : FI.getNextFunctionInfo();
    OS << FI;
  }

  return OS;
}
