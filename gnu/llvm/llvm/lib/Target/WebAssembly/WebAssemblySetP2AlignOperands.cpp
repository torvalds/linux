//=- WebAssemblySetP2AlignOperands.cpp - Set alignments on loads and stores -=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file sets the p2align operands on load and store instructions.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyInstrInfo.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-set-p2align-operands"

namespace {
class WebAssemblySetP2AlignOperands final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblySetP2AlignOperands() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "WebAssembly Set p2align Operands";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addPreserved<MachineBlockFrequencyInfoWrapperPass>();
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char WebAssemblySetP2AlignOperands::ID = 0;
INITIALIZE_PASS(WebAssemblySetP2AlignOperands, DEBUG_TYPE,
                "Set the p2align operands for WebAssembly loads and stores",
                false, false)

FunctionPass *llvm::createWebAssemblySetP2AlignOperands() {
  return new WebAssemblySetP2AlignOperands();
}

static void rewriteP2Align(MachineInstr &MI, unsigned OperandNo) {
  assert(MI.getOperand(OperandNo).getImm() == 0 &&
         "ISel should set p2align operands to 0");
  assert(MI.hasOneMemOperand() &&
         "Load and store instructions have exactly one mem operand");
  assert((*MI.memoperands_begin())->getSize() ==
             (UINT64_C(1) << WebAssembly::GetDefaultP2Align(MI.getOpcode())) &&
         "Default p2align value should be natural");
  assert(MI.getDesc().operands()[OperandNo].OperandType ==
             WebAssembly::OPERAND_P2ALIGN &&
         "Load and store instructions should have a p2align operand");
  uint64_t P2Align = Log2((*MI.memoperands_begin())->getAlign());

  // WebAssembly does not currently support supernatural alignment.
  P2Align = std::min(P2Align,
                     uint64_t(WebAssembly::GetDefaultP2Align(MI.getOpcode())));

  MI.getOperand(OperandNo).setImm(P2Align);
}

bool WebAssemblySetP2AlignOperands::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Set p2align Operands **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  bool Changed = false;

  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      int16_t P2AlignOpNum = WebAssembly::getNamedOperandIdx(
          MI.getOpcode(), WebAssembly::OpName::p2align);
      if (P2AlignOpNum != -1) {
        rewriteP2Align(MI, P2AlignOpNum);
        Changed = true;
      }
    }
  }

  return Changed;
}
