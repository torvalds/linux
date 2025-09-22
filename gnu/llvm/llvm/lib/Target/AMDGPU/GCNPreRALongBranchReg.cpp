//===-- GCNPreRALongBranchReg.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// \brief Pass to estimate pre RA branch size and reserve a pair of SGPRs if
// there is a long branch. Branch size at this point is difficult to track since
// we have no idea what spills will be inserted later on. We just assume 8 bytes
// per instruction to compute approximations without computing the actual
// instruction size to see if we're in the neighborhood of the maximum branch
// distrance threshold tuning of what is considered "long" is handled through
// amdgpu-long-branch-factor cl argument which sets LongBranchFactor.
//===----------------------------------------------------------------------===//
#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "amdgpu-pre-ra-long-branch-reg"

namespace {

static cl::opt<double> LongBranchFactor(
    "amdgpu-long-branch-factor", cl::init(1.0), cl::Hidden,
    cl::desc("Factor to apply to what qualifies as a long branch "
             "to reserve a pair of scalar registers. If this value "
             "is 0 the long branch registers are never reserved. As this "
             "value grows the greater chance the branch distance will fall "
             "within the threshold and the registers will be marked to be "
             "reserved. We lean towards always reserving a register for  "
             "long jumps"));

class GCNPreRALongBranchReg : public MachineFunctionPass {

  struct BasicBlockInfo {
    // Offset - Distance from the beginning of the function to the beginning
    // of this basic block.
    uint64_t Offset = 0;
    // Size - Size of the basic block in bytes
    uint64_t Size = 0;
  };
  void generateBlockInfo(MachineFunction &MF,
                         SmallVectorImpl<BasicBlockInfo> &BlockInfo);

public:
  static char ID;
  GCNPreRALongBranchReg() : MachineFunctionPass(ID) {
    initializeGCNPreRALongBranchRegPass(*PassRegistry::getPassRegistry());
  }
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "AMDGPU Pre-RA Long Branch Reg";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
} // End anonymous namespace.
char GCNPreRALongBranchReg::ID = 0;

INITIALIZE_PASS(GCNPreRALongBranchReg, DEBUG_TYPE,
                "AMDGPU Pre-RA Long Branch Reg", false, false)

char &llvm::GCNPreRALongBranchRegID = GCNPreRALongBranchReg::ID;
void GCNPreRALongBranchReg::generateBlockInfo(
    MachineFunction &MF, SmallVectorImpl<BasicBlockInfo> &BlockInfo) {

  BlockInfo.resize(MF.getNumBlockIDs());

  // Approximate the size of all basic blocks by just
  // assuming 8 bytes per instruction
  for (const MachineBasicBlock &MBB : MF) {
    uint64_t NumInstr = 0;
    // Loop through the basic block and add up all non-debug
    // non-meta instructions
    for (const MachineInstr &MI : MBB) {
      // isMetaInstruction is a superset of isDebugIstr
      if (MI.isMetaInstruction())
        continue;
      NumInstr += 1;
    }
    // Approximate size as just 8 bytes per instruction
    BlockInfo[MBB.getNumber()].Size = 8 * NumInstr;
  }
  uint64_t PrevNum = (&MF)->begin()->getNumber();
  for (auto &MBB :
       make_range(std::next(MachineFunction::iterator((&MF)->begin())),
                  (&MF)->end())) {
    uint64_t Num = MBB.getNumber();
    // Compute the offset immediately following this block.
    BlockInfo[Num].Offset = BlockInfo[PrevNum].Offset + BlockInfo[PrevNum].Size;
    PrevNum = Num;
  }
}
bool GCNPreRALongBranchReg::runOnMachineFunction(MachineFunction &MF) {
  const GCNSubtarget &STM = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = STM.getInstrInfo();
  const SIRegisterInfo *TRI = STM.getRegisterInfo();
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // For now, reserve highest available SGPR pair. After RA,
  // shift down to a lower unused pair of SGPRs
  // If all registers are used, then findUnusedRegister will return
  // AMDGPU::NoRegister.
  constexpr bool ReserveHighestRegister = true;
  Register LongBranchReservedReg = TRI->findUnusedRegister(
      MRI, &AMDGPU::SGPR_64RegClass, MF, ReserveHighestRegister);
  if (!LongBranchReservedReg)
    return false;

  // Approximate code size and offsets of each basic block
  SmallVector<BasicBlockInfo, 16> BlockInfo;
  generateBlockInfo(MF, BlockInfo);

  for (const MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::const_iterator Last = MBB.getLastNonDebugInstr();
    if (Last == MBB.end() || !Last->isUnconditionalBranch())
      continue;
    MachineBasicBlock *DestBB = TII->getBranchDestBlock(*Last);
    uint64_t BlockDistance = static_cast<uint64_t>(
        LongBranchFactor * BlockInfo[DestBB->getNumber()].Offset);
    // If the distance falls outside the threshold assume it is a long branch
    // and we need to reserve the registers
    if (!TII->isBranchOffsetInRange(Last->getOpcode(), BlockDistance)) {
      MFI->setLongBranchReservedReg(LongBranchReservedReg);
      return true;
    }
  }
  return false;
}
