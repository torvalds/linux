//===- BlockPrinter.cpp - FDR Block Pretty Printer Implementation --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/BlockPrinter.h"

namespace llvm {
namespace xray {

Error BlockPrinter::visit(BufferExtents &R) {
  OS << "\n[New Block]\n";
  CurrentState = State::Preamble;
  return RP.visit(R);
}

// Preamble printing.
Error BlockPrinter::visit(NewBufferRecord &R) {
  if (CurrentState == State::Start)
    OS << "\n[New Block]\n";

  OS << "Preamble: \n";
  CurrentState = State::Preamble;
  return RP.visit(R);
}

Error BlockPrinter::visit(WallclockRecord &R) {
  CurrentState = State::Preamble;
  return RP.visit(R);
}

Error BlockPrinter::visit(PIDRecord &R) {
  CurrentState = State::Preamble;
  return RP.visit(R);
}

// Metadata printing.
Error BlockPrinter::visit(NewCPUIDRecord &R) {
  if (CurrentState == State::Preamble)
    OS << "\nBody:\n";
  if (CurrentState == State::Function)
    OS << "\nMetadata: ";
  CurrentState = State::Metadata;
  OS << " ";
  auto E = RP.visit(R);
  return E;
}

Error BlockPrinter::visit(TSCWrapRecord &R) {
  if (CurrentState == State::Function)
    OS << "\nMetadata:";
  CurrentState = State::Metadata;
  OS << " ";
  auto E = RP.visit(R);
  return E;
}

// Custom events will be rendered like "function" events.
Error BlockPrinter::visit(CustomEventRecord &R) {
  if (CurrentState == State::Metadata)
    OS << "\n";
  CurrentState = State::CustomEvent;
  OS << "*  ";
  auto E = RP.visit(R);
  return E;
}

Error BlockPrinter::visit(CustomEventRecordV5 &R) {
  if (CurrentState == State::Metadata)
    OS << "\n";
  CurrentState = State::CustomEvent;
  OS << "*  ";
  auto E = RP.visit(R);
  return E;
}

Error BlockPrinter::visit(TypedEventRecord &R) {
  if (CurrentState == State::Metadata)
    OS << "\n";
  CurrentState = State::CustomEvent;
  OS << "*  ";
  auto E = RP.visit(R);
  return E;
}

// Function call printing.
Error BlockPrinter::visit(FunctionRecord &R) {
  if (CurrentState == State::Metadata)
    OS << "\n";
  CurrentState = State::Function;
  OS << "-  ";
  auto E = RP.visit(R);
  return E;
}

Error BlockPrinter::visit(CallArgRecord &R) {
  CurrentState = State::Arg;
  OS << " : ";
  auto E = RP.visit(R);
  return E;
}

Error BlockPrinter::visit(EndBufferRecord &R) {
    CurrentState = State::End;
    OS << " *** ";
    auto E = RP.visit(R);
    return E;
}

} // namespace xray
} // namespace llvm
