//===--- AArch64StorePairSuppress.cpp --- Suppress store pair formation ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass identifies floating point stores that should not be combined into
// store pairs. Later we may do the same for floating point loads.
// ===---------------------------------------------------------------------===//

#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineTraceMetrics.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-stp-suppress"

#define STPSUPPRESS_PASS_NAME "AArch64 Store Pair Suppression"

namespace {
class AArch64StorePairSuppress : public MachineFunctionPass {
  const AArch64InstrInfo *TII;
  const TargetRegisterInfo *TRI;
  const MachineRegisterInfo *MRI;
  TargetSchedModel SchedModel;
  MachineTraceMetrics *Traces;
  MachineTraceMetrics::Ensemble *MinInstr;

public:
  static char ID;
  AArch64StorePairSuppress() : MachineFunctionPass(ID) {
    initializeAArch64StorePairSuppressPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return STPSUPPRESS_PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &F) override;

private:
  bool shouldAddSTPToBlock(const MachineBasicBlock *BB);

  bool isNarrowFPStore(const MachineInstr &MI);

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineTraceMetrics>();
    AU.addPreserved<MachineTraceMetrics>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
char AArch64StorePairSuppress::ID = 0;
} // anonymous

INITIALIZE_PASS(AArch64StorePairSuppress, "aarch64-stp-suppress",
                STPSUPPRESS_PASS_NAME, false, false)

FunctionPass *llvm::createAArch64StorePairSuppressPass() {
  return new AArch64StorePairSuppress();
}

/// Return true if an STP can be added to this block without increasing the
/// critical resource height. STP is good to form in Ld/St limited blocks and
/// bad to form in float-point limited blocks. This is true independent of the
/// critical path. If the critical path is longer than the resource height, the
/// extra vector ops can limit physreg renaming. Otherwise, it could simply
/// oversaturate the vector units.
bool AArch64StorePairSuppress::shouldAddSTPToBlock(const MachineBasicBlock *BB) {
  if (!MinInstr)
    MinInstr = Traces->getEnsemble(MachineTraceStrategy::TS_MinInstrCount);

  MachineTraceMetrics::Trace BBTrace = MinInstr->getTrace(BB);
  unsigned ResLength = BBTrace.getResourceLength();

  // Get the machine model's scheduling class for STPDi and STRDui.
  // Bypass TargetSchedule's SchedClass resolution since we only have an opcode.
  unsigned SCIdx = TII->get(AArch64::STPDi).getSchedClass();
  const MCSchedClassDesc *PairSCDesc =
      SchedModel.getMCSchedModel()->getSchedClassDesc(SCIdx);

  unsigned SCIdx2 = TII->get(AArch64::STRDui).getSchedClass();
  const MCSchedClassDesc *SingleSCDesc =
      SchedModel.getMCSchedModel()->getSchedClassDesc(SCIdx2);

  // If a subtarget does not define resources for STPDi, bail here.
  if (PairSCDesc->isValid() && !PairSCDesc->isVariant() &&
      SingleSCDesc->isValid() && !SingleSCDesc->isVariant()) {
    // Compute the new critical resource length after replacing 2 separate
    // STRDui with one STPDi.
    unsigned ResLenWithSTP = BBTrace.getResourceLength(
        std::nullopt, PairSCDesc, {SingleSCDesc, SingleSCDesc});
    if (ResLenWithSTP > ResLength) {
      LLVM_DEBUG(dbgs() << "  Suppress STP in BB: " << BB->getNumber()
                        << " resources " << ResLength << " -> " << ResLenWithSTP
                        << "\n");
      return false;
    }
  }
  return true;
}

/// Return true if this is a floating-point store smaller than the V reg. On
/// cyclone, these require a vector shuffle before storing a pair.
/// Ideally we would call getMatchingPairOpcode() and have the machine model
/// tell us if it's profitable with no cpu knowledge here.
///
/// FIXME: We plan to develop a decent Target abstraction for simple loads and
/// stores. Until then use a nasty switch similar to AArch64LoadStoreOptimizer.
bool AArch64StorePairSuppress::isNarrowFPStore(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case AArch64::STRSui:
  case AArch64::STRDui:
  case AArch64::STURSi:
  case AArch64::STURDi:
    return true;
  }
}

bool AArch64StorePairSuppress::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()) || MF.getFunction().hasOptSize())
    return false;

  const AArch64Subtarget &ST = MF.getSubtarget<AArch64Subtarget>();
  if (!ST.enableStorePairSuppress())
    return false;

  TII = static_cast<const AArch64InstrInfo *>(ST.getInstrInfo());
  TRI = ST.getRegisterInfo();
  MRI = &MF.getRegInfo();
  SchedModel.init(&ST);
  Traces = &getAnalysis<MachineTraceMetrics>();
  MinInstr = nullptr;

  LLVM_DEBUG(dbgs() << "*** " << getPassName() << ": " << MF.getName() << '\n');

  if (!SchedModel.hasInstrSchedModel()) {
    LLVM_DEBUG(dbgs() << "  Skipping pass: no machine model present.\n");
    return false;
  }

  // Check for a sequence of stores to the same base address. We don't need to
  // precisely determine whether a store pair can be formed. But we do want to
  // filter out most situations where we can't form store pairs to avoid
  // computing trace metrics in those cases.
  for (auto &MBB : MF) {
    bool SuppressSTP = false;
    unsigned PrevBaseReg = 0;
    for (auto &MI : MBB) {
      if (!isNarrowFPStore(MI))
        continue;
      const MachineOperand *BaseOp;
      int64_t Offset;
      bool OffsetIsScalable;
      if (TII->getMemOperandWithOffset(MI, BaseOp, Offset, OffsetIsScalable,
                                       TRI) &&
          BaseOp->isReg()) {
        Register BaseReg = BaseOp->getReg();
        if (PrevBaseReg == BaseReg) {
          // If this block can take STPs, skip ahead to the next block.
          if (!SuppressSTP && shouldAddSTPToBlock(MI.getParent()))
            break;
          // Otherwise, continue unpairing the stores in this block.
          LLVM_DEBUG(dbgs() << "Unpairing store " << MI << "\n");
          SuppressSTP = true;
          TII->suppressLdStPair(MI);
        }
        PrevBaseReg = BaseReg;
      } else
        PrevBaseReg = 0;
    }
  }
  // This pass just sets some internal MachineMemOperand flags. It can't really
  // invalidate anything.
  return false;
}
