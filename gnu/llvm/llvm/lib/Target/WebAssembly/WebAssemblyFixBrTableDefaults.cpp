//=- WebAssemblyFixBrTableDefaults.cpp - Fix br_table default branch targets -//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file implements a pass that eliminates redundant range checks
/// guarding br_table instructions. Since jump tables on most targets cannot
/// handle out of range indices, LLVM emits these checks before most jump
/// tables. But br_table takes a default branch target as an argument, so it
/// does not need the range checks.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblySubtarget.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "wasm-fix-br-table-defaults"

namespace {

class WebAssemblyFixBrTableDefaults final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly Fix br_table Defaults";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyFixBrTableDefaults() : MachineFunctionPass(ID) {}
};

char WebAssemblyFixBrTableDefaults::ID = 0;

// Target indepedent selection dag assumes that it is ok to use PointerTy
// as the index for a "switch", whereas Wasm so far only has a 32-bit br_table.
// See e.g. SelectionDAGBuilder::visitJumpTableHeader
// We have a 64-bit br_table in the tablegen defs as a result, which does get
// selected, and thus we get incorrect truncates/extensions happening on
// wasm64. Here we fix that.
void fixBrTableIndex(MachineInstr &MI, MachineBasicBlock *MBB,
                     MachineFunction &MF) {
  // Only happens on wasm64.
  auto &WST = MF.getSubtarget<WebAssemblySubtarget>();
  if (!WST.hasAddr64())
    return;

  assert(MI.getDesc().getOpcode() == WebAssembly::BR_TABLE_I64 &&
         "64-bit br_table pseudo instruction expected");

  // Find extension op, if any. It sits in the previous BB before the branch.
  auto ExtMI = MF.getRegInfo().getVRegDef(MI.getOperand(0).getReg());
  if (ExtMI->getOpcode() == WebAssembly::I64_EXTEND_U_I32) {
    // Unnecessarily extending a 32-bit value to 64, remove it.
    auto ExtDefReg = ExtMI->getOperand(0).getReg();
    assert(MI.getOperand(0).getReg() == ExtDefReg);
    MI.getOperand(0).setReg(ExtMI->getOperand(1).getReg());
    if (MF.getRegInfo().use_nodbg_empty(ExtDefReg)) {
      // No more users of extend, delete it.
      ExtMI->eraseFromParent();
    }
  } else {
    // Incoming 64-bit value that needs to be truncated.
    Register Reg32 =
        MF.getRegInfo().createVirtualRegister(&WebAssembly::I32RegClass);
    BuildMI(*MBB, MI.getIterator(), MI.getDebugLoc(),
            WST.getInstrInfo()->get(WebAssembly::I32_WRAP_I64), Reg32)
        .addReg(MI.getOperand(0).getReg());
    MI.getOperand(0).setReg(Reg32);
  }

  // We now have a 32-bit operand in all cases, so change the instruction
  // accordingly.
  MI.setDesc(WST.getInstrInfo()->get(WebAssembly::BR_TABLE_I32));
}

// `MI` is a br_table instruction with a dummy default target argument. This
// function finds and adds the default target argument and removes any redundant
// range check preceding the br_table. Returns the MBB that the br_table is
// moved into so it can be removed from further consideration, or nullptr if the
// br_table cannot be optimized.
MachineBasicBlock *fixBrTableDefault(MachineInstr &MI, MachineBasicBlock *MBB,
                                     MachineFunction &MF) {
  // Get the header block, which contains the redundant range check.
  assert(MBB->pred_size() == 1 && "Expected a single guard predecessor");
  auto *HeaderMBB = *MBB->pred_begin();

  // Find the conditional jump to the default target. If it doesn't exist, the
  // default target is unreachable anyway, so we can keep the existing dummy
  // target.
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 2> Cond;
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
  bool Analyzed = !TII.analyzeBranch(*HeaderMBB, TBB, FBB, Cond);
  assert(Analyzed && "Could not analyze jump header branches");
  (void)Analyzed;

  // Here are the possible outcomes. '_' is nullptr, `J` is the jump table block
  // aka MBB, 'D' is the default block.
  //
  // TBB | FBB | Meaning
  //  _  |  _  | No default block, header falls through to jump table
  //  J  |  _  | No default block, header jumps to the jump table
  //  D  |  _  | Header jumps to the default and falls through to the jump table
  //  D  |  J  | Header jumps to the default and also to the jump table
  if (TBB && TBB != MBB) {
    assert((FBB == nullptr || FBB == MBB) &&
           "Expected jump or fallthrough to br_table block");
    assert(Cond.size() == 2 && Cond[1].isReg() && "Unexpected condition info");

    // If the range check checks an i64 value, we cannot optimize it out because
    // the i64 index is truncated to an i32, making values over 2^32
    // indistinguishable from small numbers. There are also other strange edge
    // cases that can arise in practice that we don't want to reason about, so
    // conservatively only perform the optimization if the range check is the
    // normal case of an i32.gt_u.
    MachineRegisterInfo &MRI = MF.getRegInfo();
    auto *RangeCheck = MRI.getVRegDef(Cond[1].getReg());
    assert(RangeCheck != nullptr);
    if (RangeCheck->getOpcode() != WebAssembly::GT_U_I32)
      return nullptr;

    // Remove the dummy default target and install the real one.
    MI.removeOperand(MI.getNumExplicitOperands() - 1);
    MI.addOperand(MF, MachineOperand::CreateMBB(TBB));
  }

  // Remove any branches from the header and splice in the jump table instead
  TII.removeBranch(*HeaderMBB, nullptr);
  HeaderMBB->splice(HeaderMBB->end(), MBB, MBB->begin(), MBB->end());

  // Update CFG to skip the old jump table block. Remove shared successors
  // before transferring to avoid duplicated successors.
  HeaderMBB->removeSuccessor(MBB);
  for (auto &Succ : MBB->successors())
    if (HeaderMBB->isSuccessor(Succ))
      HeaderMBB->removeSuccessor(Succ);
  HeaderMBB->transferSuccessorsAndUpdatePHIs(MBB);

  // Remove the old jump table block from the function
  MF.erase(MBB);

  return HeaderMBB;
}

bool WebAssemblyFixBrTableDefaults::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Fixing br_table Default Targets **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  bool Changed = false;
  SetVector<MachineBasicBlock *, SmallVector<MachineBasicBlock *, 16>,
            DenseSet<MachineBasicBlock *>, 16>
      MBBSet;
  for (auto &MBB : MF)
    MBBSet.insert(&MBB);

  while (!MBBSet.empty()) {
    MachineBasicBlock *MBB = *MBBSet.begin();
    MBBSet.remove(MBB);
    for (auto &MI : *MBB) {
      if (WebAssembly::isBrTable(MI.getOpcode())) {
        fixBrTableIndex(MI, MBB, MF);
        auto *Fixed = fixBrTableDefault(MI, MBB, MF);
        if (Fixed != nullptr) {
          MBBSet.remove(Fixed);
          Changed = true;
        }
        break;
      }
    }
  }

  if (Changed) {
    // We rewrote part of the function; recompute relevant things.
    MF.RenumberBlocks();
    return true;
  }

  return false;
}

} // end anonymous namespace

INITIALIZE_PASS(WebAssemblyFixBrTableDefaults, DEBUG_TYPE,
                "Removes range checks and sets br_table default targets", false,
                false)

FunctionPass *llvm::createWebAssemblyFixBrTableDefaults() {
  return new WebAssemblyFixBrTableDefaults();
}
