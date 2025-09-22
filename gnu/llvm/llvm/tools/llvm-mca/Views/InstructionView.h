//===----------------------- InstructionView.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the main interface for Views that examine and reference
/// a sequence of machine instructions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCA_INSTRUCTIONVIEW_H
#define LLVM_TOOLS_LLVM_MCA_INSTRUCTIONVIEW_H

#include "llvm/MCA/View.h"
#include "llvm/Support/JSON.h"

namespace llvm {
class MCInstPrinter;

namespace mca {

// The base class for views that deal with individual machine instructions.
class InstructionView : public View {
  const llvm::MCSubtargetInfo &STI;
  llvm::MCInstPrinter &MCIP;
  llvm::ArrayRef<llvm::MCInst> Source;

  mutable std::string InstructionString;
  mutable raw_string_ostream InstrStream;

public:
  void printView(llvm::raw_ostream &) const override {}
  InstructionView(const llvm::MCSubtargetInfo &STI,
                  llvm::MCInstPrinter &Printer, llvm::ArrayRef<llvm::MCInst> S)
      : STI(STI), MCIP(Printer), Source(S), InstrStream(InstructionString) {}

  virtual ~InstructionView();

  StringRef getNameAsString() const override { return "Instructions"; }

  // Return a reference to a string representing a given machine instruction.
  // The result should be used or copied before the next call to
  // printInstructionString() as it will overwrite the previous result.
  StringRef printInstructionString(const llvm::MCInst &MCI) const;
  const llvm::MCSubtargetInfo &getSubTargetInfo() const { return STI; }

  llvm::MCInstPrinter &getInstPrinter() const { return MCIP; }
  llvm::ArrayRef<llvm::MCInst> getSource() const { return Source; }

  json::Value toJSON() const override;
};

} // namespace mca
} // namespace llvm

#endif
