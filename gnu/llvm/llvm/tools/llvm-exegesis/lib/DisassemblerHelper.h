//===-- DisassemblerHelper.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Helper class for decoding machine instructions and printing them in an
/// assembler form.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_DISASSEMBLER_HELPER_H
#define LLVM_TOOLS_LLVM_EXEGESIS_DISASSEMBLER_HELPER_H

#include "LlvmState.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstPrinter.h"

#include <memory>

namespace llvm {
namespace exegesis {

// A helper class for decoding and printing machine instructions.
class DisassemblerHelper {
public:
  DisassemblerHelper(const LLVMState &State);

  void printInst(const MCInst *MI, raw_ostream &OS) const {
    const auto &STI = State_.getSubtargetInfo();
    InstPrinter_->printInst(MI, 0, "", STI, OS);
  }

  bool decodeInst(MCInst &MI, uint64_t &MISize, ArrayRef<uint8_t> Bytes) const {
    return Disasm_->getInstruction(MI, MISize, Bytes, 0, nulls());
  }

private:
  const LLVMState &State_;
  std::unique_ptr<MCContext> Context_;
  std::unique_ptr<MCAsmInfo> AsmInfo_;
  std::unique_ptr<MCInstPrinter> InstPrinter_;
  std::unique_ptr<MCDisassembler> Disasm_;
};

} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_DISASSEMBLER_HELPER_H
