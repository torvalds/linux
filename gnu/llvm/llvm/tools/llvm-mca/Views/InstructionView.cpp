//===----------------------- InstructionView.cpp ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the member functions of the class InstructionView.
///
//===----------------------------------------------------------------------===//

#include "Views/InstructionView.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCSubtargetInfo.h"

namespace llvm {
namespace mca {

InstructionView::~InstructionView() = default;

StringRef
InstructionView::printInstructionString(const llvm::MCInst &MCI) const {
  InstructionString = "";
  MCIP.printInst(&MCI, 0, "", STI, InstrStream);
  InstrStream.flush();
  // Remove any tabs or spaces at the beginning of the instruction.
  return StringRef(InstructionString).ltrim();
}

json::Value InstructionView::toJSON() const {
  json::Array SourceInfo;
  for (const auto &MCI : getSource()) {
    StringRef Instruction = printInstructionString(MCI);
    SourceInfo.push_back(Instruction.str());
  }
  return SourceInfo;
}

} // namespace mca
} // namespace llvm
