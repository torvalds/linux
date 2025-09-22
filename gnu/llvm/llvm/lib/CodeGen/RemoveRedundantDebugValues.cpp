//===- RemoveRedundantDebugValues.cpp - Remove Redundant Debug Value MIs --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"

/// \file RemoveRedundantDebugValues.cpp
///
/// The RemoveRedundantDebugValues pass removes redundant DBG_VALUEs that
/// appear in MIR after the register allocator.

#define DEBUG_TYPE "removeredundantdebugvalues"

using namespace llvm;

STATISTIC(NumRemovedBackward, "Number of DBG_VALUEs removed (backward scan)");
STATISTIC(NumRemovedForward, "Number of DBG_VALUEs removed (forward scan)");

namespace {

class RemoveRedundantDebugValues : public MachineFunctionPass {
public:
  static char ID;

  RemoveRedundantDebugValues();

  bool reduceDbgValues(MachineFunction &MF);

  /// Remove redundant debug value MIs for the given machine function.
  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

//===----------------------------------------------------------------------===//
//            Implementation
//===----------------------------------------------------------------------===//

char RemoveRedundantDebugValues::ID = 0;

char &llvm::RemoveRedundantDebugValuesID = RemoveRedundantDebugValues::ID;

INITIALIZE_PASS(RemoveRedundantDebugValues, DEBUG_TYPE,
                "Remove Redundant DEBUG_VALUE analysis", false, false)

/// Default construct and initialize the pass.
RemoveRedundantDebugValues::RemoveRedundantDebugValues()
    : MachineFunctionPass(ID) {
  initializeRemoveRedundantDebugValuesPass(*PassRegistry::getPassRegistry());
}

// This analysis aims to remove redundant DBG_VALUEs by going forward
// in the basic block by considering the first DBG_VALUE as a valid
// until its first (location) operand is not clobbered/modified.
// For example:
//   (1) DBG_VALUE $edi, !"var1", ...
//   (2) <block of code that does affect $edi>
//   (3) DBG_VALUE $edi, !"var1", ...
//   ...
// in this case, we can remove (3).
// TODO: Support DBG_VALUE_LIST and other debug instructions.
static bool reduceDbgValsForwardScan(MachineBasicBlock &MBB) {
  LLVM_DEBUG(dbgs() << "\n == Forward Scan == \n");

  SmallVector<MachineInstr *, 8> DbgValsToBeRemoved;
  DenseMap<DebugVariable, std::pair<MachineOperand *, const DIExpression *>>
      VariableMap;
  const auto *TRI = MBB.getParent()->getSubtarget().getRegisterInfo();

  for (auto &MI : MBB) {
    if (MI.isDebugValue()) {
      DebugVariable Var(MI.getDebugVariable(), std::nullopt,
                        MI.getDebugLoc()->getInlinedAt());
      auto VMI = VariableMap.find(Var);
      // Just stop tracking this variable, until we cover DBG_VALUE_LIST.
      // 1  DBG_VALUE $rax, "x", DIExpression()
      // ...
      // 2  DBG_VALUE_LIST "x", DIExpression(...), $rax, $rbx
      // ...
      // 3  DBG_VALUE $rax, "x", DIExpression()
      if (MI.isDebugValueList() && VMI != VariableMap.end()) {
        VariableMap.erase(VMI);
        continue;
      }

      MachineOperand &Loc = MI.getDebugOperand(0);
      if (!Loc.isReg()) {
        // If it's not a register, just stop tracking such variable.
        if (VMI != VariableMap.end())
          VariableMap.erase(VMI);
        continue;
      }

      // We have found a new value for a variable.
      if (VMI == VariableMap.end() ||
          VMI->second.first->getReg() != Loc.getReg() ||
          VMI->second.second != MI.getDebugExpression()) {
        VariableMap[Var] = {&Loc, MI.getDebugExpression()};
        continue;
      }

      // Found an identical DBG_VALUE, so it can be considered
      // for later removal.
      DbgValsToBeRemoved.push_back(&MI);
    }

    if (MI.isMetaInstruction())
      continue;

    // Stop tracking any location that is clobbered by this instruction.
    for (auto &Var : VariableMap) {
      auto &LocOp = Var.second.first;
      if (MI.modifiesRegister(LocOp->getReg(), TRI))
        VariableMap.erase(Var.first);
    }
  }

  for (auto &Instr : DbgValsToBeRemoved) {
    LLVM_DEBUG(dbgs() << "removing "; Instr->dump());
    Instr->eraseFromParent();
    ++NumRemovedForward;
  }

  return !DbgValsToBeRemoved.empty();
}

// This analysis aims to remove redundant DBG_VALUEs by going backward
// in the basic block and removing all but the last DBG_VALUE for any
// given variable in a set of consecutive DBG_VALUE instructions.
// For example:
//   (1) DBG_VALUE $edi, !"var1", ...
//   (2) DBG_VALUE $esi, !"var2", ...
//   (3) DBG_VALUE $edi, !"var1", ...
//   ...
// in this case, we can remove (1).
static bool reduceDbgValsBackwardScan(MachineBasicBlock &MBB) {
  LLVM_DEBUG(dbgs() << "\n == Backward Scan == \n");
  SmallVector<MachineInstr *, 8> DbgValsToBeRemoved;
  SmallDenseSet<DebugVariable> VariableSet;

  for (MachineInstr &MI : llvm::reverse(MBB)) {
    if (MI.isDebugValue()) {
      DebugVariable Var(MI.getDebugVariable(), MI.getDebugExpression(),
                        MI.getDebugLoc()->getInlinedAt());
      auto R = VariableSet.insert(Var);
      // If it is a DBG_VALUE describing a constant as:
      //   DBG_VALUE 0, ...
      // we just don't consider such instructions as candidates
      // for redundant removal.
      if (MI.isNonListDebugValue()) {
        MachineOperand &Loc = MI.getDebugOperand(0);
        if (!Loc.isReg()) {
          // If we have already encountered this variable, just stop
          // tracking it.
          if (!R.second)
            VariableSet.erase(Var);
          continue;
        }
      }

      // We have already encountered the value for this variable,
      // so this one can be deleted.
      if (!R.second)
        DbgValsToBeRemoved.push_back(&MI);
      continue;
    }

    // If we encountered a non-DBG_VALUE, try to find the next
    // sequence with consecutive DBG_VALUE instructions.
    VariableSet.clear();
  }

  for (auto &Instr : DbgValsToBeRemoved) {
    LLVM_DEBUG(dbgs() << "removing "; Instr->dump());
    Instr->eraseFromParent();
    ++NumRemovedBackward;
  }

  return !DbgValsToBeRemoved.empty();
}

bool RemoveRedundantDebugValues::reduceDbgValues(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "\nDebug Value Reduction\n");

  bool Changed = false;

  for (auto &MBB : MF) {
    Changed |= reduceDbgValsBackwardScan(MBB);
    Changed |= reduceDbgValsForwardScan(MBB);
  }

  return Changed;
}

bool RemoveRedundantDebugValues::runOnMachineFunction(MachineFunction &MF) {
  // Skip functions without debugging information.
  if (!MF.getFunction().getSubprogram())
    return false;

  // Skip functions from NoDebug compilation units.
  if (MF.getFunction().getSubprogram()->getUnit()->getEmissionKind() ==
      DICompileUnit::NoDebug)
    return false;

  bool Changed = reduceDbgValues(MF);
  return Changed;
}
