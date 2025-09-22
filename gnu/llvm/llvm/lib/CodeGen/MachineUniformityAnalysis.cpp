//===- MachineUniformityAnalysis.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineUniformityAnalysis.h"
#include "llvm/ADT/GenericUniformityImpl.h"
#include "llvm/CodeGen/MachineCycleAnalysis.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineSSAContext.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

template <>
bool llvm::GenericUniformityAnalysisImpl<MachineSSAContext>::hasDivergentDefs(
    const MachineInstr &I) const {
  for (auto &op : I.all_defs()) {
    if (isDivergent(op.getReg()))
      return true;
  }
  return false;
}

template <>
bool llvm::GenericUniformityAnalysisImpl<MachineSSAContext>::markDefsDivergent(
    const MachineInstr &Instr) {
  bool insertedDivergent = false;
  const auto &MRI = F.getRegInfo();
  const auto &RBI = *F.getSubtarget().getRegBankInfo();
  const auto &TRI = *MRI.getTargetRegisterInfo();
  for (auto &op : Instr.all_defs()) {
    if (!op.getReg().isVirtual())
      continue;
    assert(!op.getSubReg());
    if (TRI.isUniformReg(MRI, RBI, op.getReg()))
      continue;
    insertedDivergent |= markDivergent(op.getReg());
  }
  return insertedDivergent;
}

template <>
void llvm::GenericUniformityAnalysisImpl<MachineSSAContext>::initialize() {
  const auto &InstrInfo = *F.getSubtarget().getInstrInfo();

  for (const MachineBasicBlock &block : F) {
    for (const MachineInstr &instr : block) {
      auto uniformity = InstrInfo.getInstructionUniformity(instr);
      if (uniformity == InstructionUniformity::AlwaysUniform) {
        addUniformOverride(instr);
        continue;
      }

      if (uniformity == InstructionUniformity::NeverUniform) {
        markDivergent(instr);
      }
    }
  }
}

template <>
void llvm::GenericUniformityAnalysisImpl<MachineSSAContext>::pushUsers(
    Register Reg) {
  assert(isDivergent(Reg));
  const auto &RegInfo = F.getRegInfo();
  for (MachineInstr &UserInstr : RegInfo.use_instructions(Reg)) {
    markDivergent(UserInstr);
  }
}

template <>
void llvm::GenericUniformityAnalysisImpl<MachineSSAContext>::pushUsers(
    const MachineInstr &Instr) {
  assert(!isAlwaysUniform(Instr));
  if (Instr.isTerminator())
    return;
  for (const MachineOperand &op : Instr.all_defs()) {
    auto Reg = op.getReg();
    if (isDivergent(Reg))
      pushUsers(Reg);
  }
}

template <>
bool llvm::GenericUniformityAnalysisImpl<MachineSSAContext>::usesValueFromCycle(
    const MachineInstr &I, const MachineCycle &DefCycle) const {
  assert(!isAlwaysUniform(I));
  for (auto &Op : I.operands()) {
    if (!Op.isReg() || !Op.readsReg())
      continue;
    auto Reg = Op.getReg();

    // FIXME: Physical registers need to be properly checked instead of always
    // returning true
    if (Reg.isPhysical())
      return true;

    auto *Def = F.getRegInfo().getVRegDef(Reg);
    if (DefCycle.contains(Def->getParent()))
      return true;
  }
  return false;
}

template <>
void llvm::GenericUniformityAnalysisImpl<MachineSSAContext>::
    propagateTemporalDivergence(const MachineInstr &I,
                                const MachineCycle &DefCycle) {
  const auto &RegInfo = F.getRegInfo();
  for (auto &Op : I.all_defs()) {
    if (!Op.getReg().isVirtual())
      continue;
    auto Reg = Op.getReg();
    if (isDivergent(Reg))
      continue;
    for (MachineInstr &UserInstr : RegInfo.use_instructions(Reg)) {
      if (DefCycle.contains(UserInstr.getParent()))
        continue;
      markDivergent(UserInstr);
    }
  }
}

template <>
bool llvm::GenericUniformityAnalysisImpl<MachineSSAContext>::isDivergentUse(
    const MachineOperand &U) const {
  if (!U.isReg())
    return false;

  auto Reg = U.getReg();
  if (isDivergent(Reg))
    return true;

  const auto &RegInfo = F.getRegInfo();
  auto *Def = RegInfo.getOneDef(Reg);
  if (!Def)
    return true;

  auto *DefInstr = Def->getParent();
  auto *UseInstr = U.getParent();
  return isTemporalDivergent(*UseInstr->getParent(), *DefInstr);
}

// This ensures explicit instantiation of
// GenericUniformityAnalysisImpl::ImplDeleter::operator()
template class llvm::GenericUniformityInfo<MachineSSAContext>;
template struct llvm::GenericUniformityAnalysisImplDeleter<
    llvm::GenericUniformityAnalysisImpl<MachineSSAContext>>;

MachineUniformityInfo llvm::computeMachineUniformityInfo(
    MachineFunction &F, const MachineCycleInfo &cycleInfo,
    const MachineDominatorTree &domTree, bool HasBranchDivergence) {
  assert(F.getRegInfo().isSSA() && "Expected to be run on SSA form!");
  MachineUniformityInfo UI(domTree, cycleInfo);
  if (HasBranchDivergence)
    UI.compute();
  return UI;
}

namespace {

class MachineUniformityInfoPrinterPass : public MachineFunctionPass {
public:
  static char ID;

  MachineUniformityInfoPrinterPass();

  bool runOnMachineFunction(MachineFunction &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

} // namespace

char MachineUniformityAnalysisPass::ID = 0;

MachineUniformityAnalysisPass::MachineUniformityAnalysisPass()
    : MachineFunctionPass(ID) {
  initializeMachineUniformityAnalysisPassPass(*PassRegistry::getPassRegistry());
}

INITIALIZE_PASS_BEGIN(MachineUniformityAnalysisPass, "machine-uniformity",
                      "Machine Uniformity Info Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(MachineCycleInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(MachineUniformityAnalysisPass, "machine-uniformity",
                    "Machine Uniformity Info Analysis", true, true)

void MachineUniformityAnalysisPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MachineCycleInfoWrapperPass>();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool MachineUniformityAnalysisPass::runOnMachineFunction(MachineFunction &MF) {
  auto &DomTree =
      getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree().getBase();
  auto &CI = getAnalysis<MachineCycleInfoWrapperPass>().getCycleInfo();
  // FIXME: Query TTI::hasBranchDivergence. -run-pass seems to end up with a
  // default NoTTI
  UI = computeMachineUniformityInfo(MF, CI, DomTree, true);
  return false;
}

void MachineUniformityAnalysisPass::print(raw_ostream &OS,
                                          const Module *) const {
  OS << "MachineUniformityInfo for function: " << UI.getFunction().getName()
     << "\n";
  UI.print(OS);
}

char MachineUniformityInfoPrinterPass::ID = 0;

MachineUniformityInfoPrinterPass::MachineUniformityInfoPrinterPass()
    : MachineFunctionPass(ID) {
  initializeMachineUniformityInfoPrinterPassPass(
      *PassRegistry::getPassRegistry());
}

INITIALIZE_PASS_BEGIN(MachineUniformityInfoPrinterPass,
                      "print-machine-uniformity",
                      "Print Machine Uniformity Info Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(MachineUniformityAnalysisPass)
INITIALIZE_PASS_END(MachineUniformityInfoPrinterPass,
                    "print-machine-uniformity",
                    "Print Machine Uniformity Info Analysis", true, true)

void MachineUniformityInfoPrinterPass::getAnalysisUsage(
    AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MachineUniformityAnalysisPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool MachineUniformityInfoPrinterPass::runOnMachineFunction(
    MachineFunction &F) {
  auto &UI = getAnalysis<MachineUniformityAnalysisPass>();
  UI.print(errs());
  return false;
}
