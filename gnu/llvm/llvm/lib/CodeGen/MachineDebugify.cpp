//===- MachineDebugify.cpp - Attach synthetic debug info to everything ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This pass attaches synthetic debug info to everything. It can be used
/// to create targeted tests for debug info preservation, or test for CodeGen
/// differences with vs. without debug info.
///
/// This isn't intended to have feature parity with Debugify.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/InitializePasses.h"
#include "llvm/Transforms/Utils/Debugify.h"

#define DEBUG_TYPE "mir-debugify"

using namespace llvm;

namespace {
bool applyDebugifyMetadataToMachineFunction(MachineModuleInfo &MMI,
                                            DIBuilder &DIB, Function &F) {
  MachineFunction *MaybeMF = MMI.getMachineFunction(F);
  if (!MaybeMF)
    return false;
  MachineFunction &MF = *MaybeMF;
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

  DISubprogram *SP = F.getSubprogram();
  assert(SP && "IR Debugify just created it?");

  Module &M = *F.getParent();
  LLVMContext &Ctx = M.getContext();

  unsigned NextLine = SP->getLine();
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      // This will likely emit line numbers beyond the end of the imagined
      // source function and into subsequent ones. We don't do anything about
      // that as it doesn't really matter to the compiler where the line is in
      // the imaginary source code.
      MI.setDebugLoc(DILocation::get(Ctx, NextLine++, 1, SP));
    }
  }

  // Find local variables defined by debugify. No attempt is made to match up
  // MIR-level regs to the 'correct' IR-level variables: there isn't a simple
  // way to do that, and it isn't necessary to find interesting CodeGen bugs.
  // Instead, simply keep track of one variable per line. Later, we can insert
  // DBG_VALUE insts that point to these local variables. Emitting DBG_VALUEs
  // which cover a wide range of lines can help stress the debug info passes:
  // if we can't do that, fall back to using the local variable which precedes
  // all the others.
  Function *DbgValF = M.getFunction("llvm.dbg.value");
  DbgValueInst *EarliestDVI = nullptr;
  DbgVariableRecord *EarliestDVR = nullptr;
  DenseMap<unsigned, DILocalVariable *> Line2Var;
  DIExpression *Expr = nullptr;
  if (DbgValF) {
    for (const Use &U : DbgValF->uses()) {
      auto *DVI = dyn_cast<DbgValueInst>(U.getUser());
      if (!DVI || DVI->getFunction() != &F)
        continue;
      unsigned Line = DVI->getDebugLoc().getLine();
      assert(Line != 0 && "debugify should not insert line 0 locations");
      Line2Var[Line] = DVI->getVariable();
      if (!EarliestDVI || Line < EarliestDVI->getDebugLoc().getLine())
        EarliestDVI = DVI;
      Expr = DVI->getExpression();
    }
  }
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange())) {
        if (!DVR.isDbgValue())
          continue;
        unsigned Line = DVR.getDebugLoc().getLine();
        assert(Line != 0 && "debugify should not insert line 0 locations");
        Line2Var[Line] = DVR.getVariable();
        if (!EarliestDVR || Line < EarliestDVR->getDebugLoc().getLine())
          EarliestDVR = &DVR;
        Expr = DVR.getExpression();
      }
    }
  }
  if (Line2Var.empty())
    return true;

  // Now, try to insert a DBG_VALUE instruction after each real instruction.
  // Do this by introducing debug uses of each register definition. If that is
  // not possible (e.g. we have a phi or a meta instruction), emit a constant.
  uint64_t NextImm = 0;
  SmallSet<DILocalVariable *, 16> VarSet;
  const MCInstrDesc &DbgValDesc = TII.get(TargetOpcode::DBG_VALUE);
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::iterator FirstNonPHIIt = MBB.getFirstNonPHI();
    for (auto I = MBB.begin(), E = MBB.end(); I != E;) {
      MachineInstr &MI = *I;
      ++I;

      // `I` may point to a DBG_VALUE created in the previous loop iteration.
      if (MI.isDebugInstr())
        continue;

      // It's not allowed to insert DBG_VALUEs after a terminator.
      if (MI.isTerminator())
        continue;

      // Find a suitable insertion point for the DBG_VALUE.
      auto InsertBeforeIt = MI.isPHI() ? FirstNonPHIIt : I;

      // Find a suitable local variable for the DBG_VALUE.
      unsigned Line = MI.getDebugLoc().getLine();
      if (!Line2Var.count(Line))
        Line = EarliestDVI ? EarliestDVI->getDebugLoc().getLine()
                           : EarliestDVR->getDebugLoc().getLine();
      DILocalVariable *LocalVar = Line2Var[Line];
      assert(LocalVar && "No variable for current line?");
      VarSet.insert(LocalVar);

      // Emit DBG_VALUEs for register definitions.
      SmallVector<MachineOperand *, 4> RegDefs;
      for (MachineOperand &MO : MI.all_defs())
        if (MO.getReg())
          RegDefs.push_back(&MO);
      for (MachineOperand *MO : RegDefs)
        BuildMI(MBB, InsertBeforeIt, MI.getDebugLoc(), DbgValDesc,
                /*IsIndirect=*/false, *MO, LocalVar, Expr);

      // OK, failing that, emit a constant DBG_VALUE.
      if (RegDefs.empty()) {
        auto ImmOp = MachineOperand::CreateImm(NextImm++);
        BuildMI(MBB, InsertBeforeIt, MI.getDebugLoc(), DbgValDesc,
                /*IsIndirect=*/false, ImmOp, LocalVar, Expr);
      }
    }
  }

  // Here we save the number of lines and variables into "llvm.mir.debugify".
  // It is useful for mir-check-debugify.
  NamedMDNode *NMD = M.getNamedMetadata("llvm.mir.debugify");
  IntegerType *Int32Ty = Type::getInt32Ty(Ctx);
  if (!NMD) {
    NMD = M.getOrInsertNamedMetadata("llvm.mir.debugify");
    auto addDebugifyOperand = [&](unsigned N) {
      NMD->addOperand(MDNode::get(
          Ctx, ValueAsMetadata::getConstant(ConstantInt::get(Int32Ty, N))));
    };
    // Add number of lines.
    addDebugifyOperand(NextLine - 1);
    // Add number of variables.
    addDebugifyOperand(VarSet.size());
  } else {
    assert(NMD->getNumOperands() == 2 &&
           "llvm.mir.debugify should have exactly 2 operands!");
    auto setDebugifyOperand = [&](unsigned Idx, unsigned N) {
      NMD->setOperand(Idx, MDNode::get(Ctx, ValueAsMetadata::getConstant(
                                                ConstantInt::get(Int32Ty, N))));
    };
    auto getDebugifyOperand = [&](unsigned Idx) {
      return mdconst::extract<ConstantInt>(NMD->getOperand(Idx)->getOperand(0))
          ->getZExtValue();
    };
    // Set number of lines.
    setDebugifyOperand(0, NextLine - 1);
    // Set number of variables.
    auto OldNumVars = getDebugifyOperand(1);
    setDebugifyOperand(1, OldNumVars + VarSet.size());
  }

  return true;
}

/// ModulePass for attaching synthetic debug info to everything, used with the
/// legacy module pass manager.
struct DebugifyMachineModule : public ModulePass {
  bool runOnModule(Module &M) override {
    // We will insert new debugify metadata, so erasing the old one.
    assert(!M.getNamedMetadata("llvm.mir.debugify") &&
           "llvm.mir.debugify metadata already exists! Strip it first");
    MachineModuleInfo &MMI =
        getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
    return applyDebugifyMetadata(
        M, M.functions(),
        "ModuleDebugify: ", [&](DIBuilder &DIB, Function &F) -> bool {
          return applyDebugifyMetadataToMachineFunction(MMI, DIB, F);
        });
  }

  DebugifyMachineModule() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    AU.addPreserved<MachineModuleInfoWrapperPass>();
    AU.setPreservesCFG();
  }

  static char ID; // Pass identification.
};
char DebugifyMachineModule::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(DebugifyMachineModule, DEBUG_TYPE,
                      "Machine Debugify Module", false, false)
INITIALIZE_PASS_END(DebugifyMachineModule, DEBUG_TYPE,
                    "Machine Debugify Module", false, false)

ModulePass *llvm::createDebugifyMachineModulePass() {
  return new DebugifyMachineModule();
}
