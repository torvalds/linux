//===-- WebAssemblyDebugFixup.cpp - Debug Fixup ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Several prior passes may "stackify" registers, here we ensure any references
/// in such registers in debug_value instructions become stack relative also.
/// This is done in a separate pass such that not all previous passes need to
/// track stack depth when values get stackified.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-debug-fixup"

namespace {
class WebAssemblyDebugFixup final : public MachineFunctionPass {
  StringRef getPassName() const override { return "WebAssembly Debug Fixup"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyDebugFixup() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char WebAssemblyDebugFixup::ID = 0;
INITIALIZE_PASS(
    WebAssemblyDebugFixup, DEBUG_TYPE,
    "Ensures debug_value's that have been stackified become stack relative",
    false, false)

FunctionPass *llvm::createWebAssemblyDebugFixup() {
  return new WebAssemblyDebugFixup();
}

// At this very end of the compilation pipeline, if any DBG_VALUEs with
// registers remain, it means they are dangling info which we failed to update
// when their corresponding def instruction was transformed/moved/splitted etc.
// Because Wasm cannot access values in LLVM virtual registers in the debugger,
// these dangling DBG_VALUEs in effect kill the effect of any previous DBG_VALUE
// associated with the variable, which will appear as "optimized out".
static void setDanglingDebugValuesUndef(MachineBasicBlock &MBB,
                                        const TargetInstrInfo *TII) {
  for (auto &MI : llvm::make_early_inc_range(MBB)) {
    if (MI.isDebugValue() && MI.getDebugOperand(0).isReg() &&
        !MI.isUndefDebugValue()) {
      LLVM_DEBUG(dbgs() << "Warning: dangling DBG_VALUE set to undef: " << MI
                        << "\n");
      MI.setDebugValueUndef();
    }
  }
}

bool WebAssemblyDebugFixup::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Debug Fixup **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  WebAssemblyFunctionInfo &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();
  const auto *TII = MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();

  struct StackElem {
    unsigned Reg;
    MachineInstr *DebugValue;
  };
  std::vector<StackElem> Stack;
  for (MachineBasicBlock &MBB : MF) {
    // We may insert into this list.
    for (auto MII = MBB.begin(); MII != MBB.end(); ++MII) {
      MachineInstr &MI = *MII;
      if (MI.isDebugValue()) {
        auto &MO = MI.getOperand(0);
        // Also check if not a $noreg: likely a DBG_VALUE we just inserted.
        if (MO.isReg() && MO.getReg().isValid() &&
            MFI.isVRegStackified(MO.getReg())) {
          // Found a DBG_VALUE with a stackified register we will
          // change into a stack operand.
          // Search for register rather than assume it is on top (which it
          // typically is if it appears right after the def), since
          // DBG_VALUE's may shift under some circumstances.
          for (auto &Elem : reverse(Stack)) {
            if (MO.getReg() == Elem.Reg) {
              auto Depth = static_cast<unsigned>(&Elem - &Stack[0]);
              LLVM_DEBUG(dbgs() << "Debug Value VReg " << MO.getReg()
                                << " -> Stack Relative " << Depth << "\n");
              MO.ChangeToTargetIndex(WebAssembly::TI_OPERAND_STACK, Depth);
              // Save the DBG_VALUE instruction that defined this stackified
              // variable since later we need it to construct another one on
              // pop.
              Elem.DebugValue = &MI;
              break;
            }
          }
          // If the Reg was not found, we have a DBG_VALUE outside of its
          // def-use range, and we leave it unmodified as reg, which means
          // it will be culled later.
        }
      } else {
        // Track stack depth.
        for (MachineOperand &MO : reverse(MI.explicit_uses())) {
          if (MO.isReg() && MFI.isVRegStackified(MO.getReg())) {
            auto Prev = Stack.back();
            Stack.pop_back();
            assert(Prev.Reg == MO.getReg() &&
                   "WebAssemblyDebugFixup: Pop: Register not matched!");
            // We should not put a DBG_VALUE after a terminator; debug ranges
            // are terminated at the end of a BB anyway.
            if (Prev.DebugValue && !MI.isTerminator()) {
              // This stackified reg is a variable that started life at
              // Prev.DebugValue, so now that we're popping it we must insert
              // a $noreg DBG_VALUE for the variable to end it, right after
              // the current instruction.
              BuildMI(*Prev.DebugValue->getParent(), std::next(MII),
                      Prev.DebugValue->getDebugLoc(),
                      TII->get(WebAssembly::DBG_VALUE), false, Register(),
                      Prev.DebugValue->getOperand(2).getMetadata(),
                      Prev.DebugValue->getOperand(3).getMetadata());
            }
          }
        }
        for (MachineOperand &MO : MI.defs()) {
          if (MO.isReg() && MFI.isVRegStackified(MO.getReg())) {
            Stack.push_back({MO.getReg(), nullptr});
          }
        }
      }
    }
    assert(Stack.empty() &&
           "WebAssemblyDebugFixup: Stack not empty at end of basic block!");

    setDanglingDebugValuesUndef(MBB, TII);
  }

  return true;
}
