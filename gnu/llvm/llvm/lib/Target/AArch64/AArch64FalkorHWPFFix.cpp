//===- AArch64FalkorHWPFFix.cpp - Avoid HW prefetcher pitfalls on Falkor --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file For Falkor, we want to avoid HW prefetcher instruction tag collisions
/// that may inhibit the HW prefetching.  This is done in two steps.  Before
/// ISel, we mark strided loads (i.e. those that will likely benefit from
/// prefetching) with metadata.  Then, after opcodes have been finalized, we
/// insert MOVs and re-write loads to prevent unintentional tag collisions.
// ===---------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "AArch64TargetMachine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "aarch64-falkor-hwpf-fix"

STATISTIC(NumStridedLoadsMarked, "Number of strided loads marked");
STATISTIC(NumCollisionsAvoided,
          "Number of HW prefetch tag collisions avoided");
STATISTIC(NumCollisionsNotAvoided,
          "Number of HW prefetch tag collisions not avoided due to lack of registers");
DEBUG_COUNTER(FixCounter, "falkor-hwpf",
              "Controls which tag collisions are avoided");

namespace {

class FalkorMarkStridedAccesses {
public:
  FalkorMarkStridedAccesses(LoopInfo &LI, ScalarEvolution &SE)
      : LI(LI), SE(SE) {}

  bool run();

private:
  bool runOnLoop(Loop &L);

  LoopInfo &LI;
  ScalarEvolution &SE;
};

class FalkorMarkStridedAccessesLegacy : public FunctionPass {
public:
  static char ID; // Pass ID, replacement for typeid

  FalkorMarkStridedAccessesLegacy() : FunctionPass(ID) {
    initializeFalkorMarkStridedAccessesLegacyPass(
        *PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetPassConfig>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addPreserved<ScalarEvolutionWrapperPass>();
  }

  bool runOnFunction(Function &F) override;
};

} // end anonymous namespace

char FalkorMarkStridedAccessesLegacy::ID = 0;

INITIALIZE_PASS_BEGIN(FalkorMarkStridedAccessesLegacy, DEBUG_TYPE,
                      "Falkor HW Prefetch Fix", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_END(FalkorMarkStridedAccessesLegacy, DEBUG_TYPE,
                    "Falkor HW Prefetch Fix", false, false)

FunctionPass *llvm::createFalkorMarkStridedAccessesPass() {
  return new FalkorMarkStridedAccessesLegacy();
}

bool FalkorMarkStridedAccessesLegacy::runOnFunction(Function &F) {
  TargetPassConfig &TPC = getAnalysis<TargetPassConfig>();
  const AArch64Subtarget *ST =
      TPC.getTM<AArch64TargetMachine>().getSubtargetImpl(F);
  if (ST->getProcFamily() != AArch64Subtarget::Falkor)
    return false;

  if (skipFunction(F))
    return false;

  LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();

  FalkorMarkStridedAccesses LDP(LI, SE);
  return LDP.run();
}

bool FalkorMarkStridedAccesses::run() {
  bool MadeChange = false;

  for (Loop *L : LI)
    for (Loop *LIt : depth_first(L))
      MadeChange |= runOnLoop(*LIt);

  return MadeChange;
}

bool FalkorMarkStridedAccesses::runOnLoop(Loop &L) {
  // Only mark strided loads in the inner-most loop
  if (!L.isInnermost())
    return false;

  bool MadeChange = false;

  for (BasicBlock *BB : L.blocks()) {
    for (Instruction &I : *BB) {
      LoadInst *LoadI = dyn_cast<LoadInst>(&I);
      if (!LoadI)
        continue;

      Value *PtrValue = LoadI->getPointerOperand();
      if (L.isLoopInvariant(PtrValue))
        continue;

      const SCEV *LSCEV = SE.getSCEV(PtrValue);
      const SCEVAddRecExpr *LSCEVAddRec = dyn_cast<SCEVAddRecExpr>(LSCEV);
      if (!LSCEVAddRec || !LSCEVAddRec->isAffine())
        continue;

      LoadI->setMetadata(FALKOR_STRIDED_ACCESS_MD,
                         MDNode::get(LoadI->getContext(), {}));
      ++NumStridedLoadsMarked;
      LLVM_DEBUG(dbgs() << "Load: " << I << " marked as strided\n");
      MadeChange = true;
    }
  }

  return MadeChange;
}

namespace {

class FalkorHWPFFix : public MachineFunctionPass {
public:
  static char ID;

  FalkorHWPFFix() : MachineFunctionPass(ID) {
    initializeFalkorHWPFFixPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &Fn) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  void runOnLoop(MachineLoop &L, MachineFunction &Fn);

  const AArch64InstrInfo *TII;
  const TargetRegisterInfo *TRI;
  DenseMap<unsigned, SmallVector<MachineInstr *, 4>> TagMap;
  bool Modified;
};

/// Bits from load opcodes used to compute HW prefetcher instruction tags.
struct LoadInfo {
  LoadInfo() = default;

  Register DestReg;
  Register BaseReg;
  int BaseRegIdx = -1;
  const MachineOperand *OffsetOpnd = nullptr;
  bool IsPrePost = false;
};

} // end anonymous namespace

char FalkorHWPFFix::ID = 0;

INITIALIZE_PASS_BEGIN(FalkorHWPFFix, "aarch64-falkor-hwpf-fix-late",
                      "Falkor HW Prefetch Fix Late Phase", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(FalkorHWPFFix, "aarch64-falkor-hwpf-fix-late",
                    "Falkor HW Prefetch Fix Late Phase", false, false)

static unsigned makeTag(unsigned Dest, unsigned Base, unsigned Offset) {
  return (Dest & 0xf) | ((Base & 0xf) << 4) | ((Offset & 0x3f) << 8);
}

static std::optional<LoadInfo> getLoadInfo(const MachineInstr &MI) {
  int DestRegIdx;
  int BaseRegIdx;
  int OffsetIdx;
  bool IsPrePost;

  switch (MI.getOpcode()) {
  default:
    return std::nullopt;

  case AArch64::LD1i64:
  case AArch64::LD2i64:
    DestRegIdx = 0;
    BaseRegIdx = 3;
    OffsetIdx = -1;
    IsPrePost = false;
    break;

  case AArch64::LD1i8:
  case AArch64::LD1i16:
  case AArch64::LD1i32:
  case AArch64::LD2i8:
  case AArch64::LD2i16:
  case AArch64::LD2i32:
  case AArch64::LD3i8:
  case AArch64::LD3i16:
  case AArch64::LD3i32:
  case AArch64::LD3i64:
  case AArch64::LD4i8:
  case AArch64::LD4i16:
  case AArch64::LD4i32:
  case AArch64::LD4i64:
    DestRegIdx = -1;
    BaseRegIdx = 3;
    OffsetIdx = -1;
    IsPrePost = false;
    break;

  case AArch64::LD1Onev1d:
  case AArch64::LD1Onev2s:
  case AArch64::LD1Onev4h:
  case AArch64::LD1Onev8b:
  case AArch64::LD1Onev2d:
  case AArch64::LD1Onev4s:
  case AArch64::LD1Onev8h:
  case AArch64::LD1Onev16b:
  case AArch64::LD1Rv1d:
  case AArch64::LD1Rv2s:
  case AArch64::LD1Rv4h:
  case AArch64::LD1Rv8b:
  case AArch64::LD1Rv2d:
  case AArch64::LD1Rv4s:
  case AArch64::LD1Rv8h:
  case AArch64::LD1Rv16b:
    DestRegIdx = 0;
    BaseRegIdx = 1;
    OffsetIdx = -1;
    IsPrePost = false;
    break;

  case AArch64::LD1Twov1d:
  case AArch64::LD1Twov2s:
  case AArch64::LD1Twov4h:
  case AArch64::LD1Twov8b:
  case AArch64::LD1Twov2d:
  case AArch64::LD1Twov4s:
  case AArch64::LD1Twov8h:
  case AArch64::LD1Twov16b:
  case AArch64::LD1Threev1d:
  case AArch64::LD1Threev2s:
  case AArch64::LD1Threev4h:
  case AArch64::LD1Threev8b:
  case AArch64::LD1Threev2d:
  case AArch64::LD1Threev4s:
  case AArch64::LD1Threev8h:
  case AArch64::LD1Threev16b:
  case AArch64::LD1Fourv1d:
  case AArch64::LD1Fourv2s:
  case AArch64::LD1Fourv4h:
  case AArch64::LD1Fourv8b:
  case AArch64::LD1Fourv2d:
  case AArch64::LD1Fourv4s:
  case AArch64::LD1Fourv8h:
  case AArch64::LD1Fourv16b:
  case AArch64::LD2Twov2s:
  case AArch64::LD2Twov4s:
  case AArch64::LD2Twov8b:
  case AArch64::LD2Twov2d:
  case AArch64::LD2Twov4h:
  case AArch64::LD2Twov8h:
  case AArch64::LD2Twov16b:
  case AArch64::LD2Rv1d:
  case AArch64::LD2Rv2s:
  case AArch64::LD2Rv4s:
  case AArch64::LD2Rv8b:
  case AArch64::LD2Rv2d:
  case AArch64::LD2Rv4h:
  case AArch64::LD2Rv8h:
  case AArch64::LD2Rv16b:
  case AArch64::LD3Threev2s:
  case AArch64::LD3Threev4h:
  case AArch64::LD3Threev8b:
  case AArch64::LD3Threev2d:
  case AArch64::LD3Threev4s:
  case AArch64::LD3Threev8h:
  case AArch64::LD3Threev16b:
  case AArch64::LD3Rv1d:
  case AArch64::LD3Rv2s:
  case AArch64::LD3Rv4h:
  case AArch64::LD3Rv8b:
  case AArch64::LD3Rv2d:
  case AArch64::LD3Rv4s:
  case AArch64::LD3Rv8h:
  case AArch64::LD3Rv16b:
  case AArch64::LD4Fourv2s:
  case AArch64::LD4Fourv4h:
  case AArch64::LD4Fourv8b:
  case AArch64::LD4Fourv2d:
  case AArch64::LD4Fourv4s:
  case AArch64::LD4Fourv8h:
  case AArch64::LD4Fourv16b:
  case AArch64::LD4Rv1d:
  case AArch64::LD4Rv2s:
  case AArch64::LD4Rv4h:
  case AArch64::LD4Rv8b:
  case AArch64::LD4Rv2d:
  case AArch64::LD4Rv4s:
  case AArch64::LD4Rv8h:
  case AArch64::LD4Rv16b:
    DestRegIdx = -1;
    BaseRegIdx = 1;
    OffsetIdx = -1;
    IsPrePost = false;
    break;

  case AArch64::LD1i64_POST:
  case AArch64::LD2i64_POST:
    DestRegIdx = 1;
    BaseRegIdx = 4;
    OffsetIdx = 5;
    IsPrePost = true;
    break;

  case AArch64::LD1i8_POST:
  case AArch64::LD1i16_POST:
  case AArch64::LD1i32_POST:
  case AArch64::LD2i8_POST:
  case AArch64::LD2i16_POST:
  case AArch64::LD2i32_POST:
  case AArch64::LD3i8_POST:
  case AArch64::LD3i16_POST:
  case AArch64::LD3i32_POST:
  case AArch64::LD3i64_POST:
  case AArch64::LD4i8_POST:
  case AArch64::LD4i16_POST:
  case AArch64::LD4i32_POST:
  case AArch64::LD4i64_POST:
    DestRegIdx = -1;
    BaseRegIdx = 4;
    OffsetIdx = 5;
    IsPrePost = true;
    break;

  case AArch64::LD1Onev1d_POST:
  case AArch64::LD1Onev2s_POST:
  case AArch64::LD1Onev4h_POST:
  case AArch64::LD1Onev8b_POST:
  case AArch64::LD1Onev2d_POST:
  case AArch64::LD1Onev4s_POST:
  case AArch64::LD1Onev8h_POST:
  case AArch64::LD1Onev16b_POST:
  case AArch64::LD1Rv1d_POST:
  case AArch64::LD1Rv2s_POST:
  case AArch64::LD1Rv4h_POST:
  case AArch64::LD1Rv8b_POST:
  case AArch64::LD1Rv2d_POST:
  case AArch64::LD1Rv4s_POST:
  case AArch64::LD1Rv8h_POST:
  case AArch64::LD1Rv16b_POST:
    DestRegIdx = 1;
    BaseRegIdx = 2;
    OffsetIdx = 3;
    IsPrePost = true;
    break;

  case AArch64::LD1Twov1d_POST:
  case AArch64::LD1Twov2s_POST:
  case AArch64::LD1Twov4h_POST:
  case AArch64::LD1Twov8b_POST:
  case AArch64::LD1Twov2d_POST:
  case AArch64::LD1Twov4s_POST:
  case AArch64::LD1Twov8h_POST:
  case AArch64::LD1Twov16b_POST:
  case AArch64::LD1Threev1d_POST:
  case AArch64::LD1Threev2s_POST:
  case AArch64::LD1Threev4h_POST:
  case AArch64::LD1Threev8b_POST:
  case AArch64::LD1Threev2d_POST:
  case AArch64::LD1Threev4s_POST:
  case AArch64::LD1Threev8h_POST:
  case AArch64::LD1Threev16b_POST:
  case AArch64::LD1Fourv1d_POST:
  case AArch64::LD1Fourv2s_POST:
  case AArch64::LD1Fourv4h_POST:
  case AArch64::LD1Fourv8b_POST:
  case AArch64::LD1Fourv2d_POST:
  case AArch64::LD1Fourv4s_POST:
  case AArch64::LD1Fourv8h_POST:
  case AArch64::LD1Fourv16b_POST:
  case AArch64::LD2Twov2s_POST:
  case AArch64::LD2Twov4s_POST:
  case AArch64::LD2Twov8b_POST:
  case AArch64::LD2Twov2d_POST:
  case AArch64::LD2Twov4h_POST:
  case AArch64::LD2Twov8h_POST:
  case AArch64::LD2Twov16b_POST:
  case AArch64::LD2Rv1d_POST:
  case AArch64::LD2Rv2s_POST:
  case AArch64::LD2Rv4s_POST:
  case AArch64::LD2Rv8b_POST:
  case AArch64::LD2Rv2d_POST:
  case AArch64::LD2Rv4h_POST:
  case AArch64::LD2Rv8h_POST:
  case AArch64::LD2Rv16b_POST:
  case AArch64::LD3Threev2s_POST:
  case AArch64::LD3Threev4h_POST:
  case AArch64::LD3Threev8b_POST:
  case AArch64::LD3Threev2d_POST:
  case AArch64::LD3Threev4s_POST:
  case AArch64::LD3Threev8h_POST:
  case AArch64::LD3Threev16b_POST:
  case AArch64::LD3Rv1d_POST:
  case AArch64::LD3Rv2s_POST:
  case AArch64::LD3Rv4h_POST:
  case AArch64::LD3Rv8b_POST:
  case AArch64::LD3Rv2d_POST:
  case AArch64::LD3Rv4s_POST:
  case AArch64::LD3Rv8h_POST:
  case AArch64::LD3Rv16b_POST:
  case AArch64::LD4Fourv2s_POST:
  case AArch64::LD4Fourv4h_POST:
  case AArch64::LD4Fourv8b_POST:
  case AArch64::LD4Fourv2d_POST:
  case AArch64::LD4Fourv4s_POST:
  case AArch64::LD4Fourv8h_POST:
  case AArch64::LD4Fourv16b_POST:
  case AArch64::LD4Rv1d_POST:
  case AArch64::LD4Rv2s_POST:
  case AArch64::LD4Rv4h_POST:
  case AArch64::LD4Rv8b_POST:
  case AArch64::LD4Rv2d_POST:
  case AArch64::LD4Rv4s_POST:
  case AArch64::LD4Rv8h_POST:
  case AArch64::LD4Rv16b_POST:
    DestRegIdx = -1;
    BaseRegIdx = 2;
    OffsetIdx = 3;
    IsPrePost = true;
    break;

  case AArch64::LDRBBroW:
  case AArch64::LDRBBroX:
  case AArch64::LDRBBui:
  case AArch64::LDRBroW:
  case AArch64::LDRBroX:
  case AArch64::LDRBui:
  case AArch64::LDRDl:
  case AArch64::LDRDroW:
  case AArch64::LDRDroX:
  case AArch64::LDRDui:
  case AArch64::LDRHHroW:
  case AArch64::LDRHHroX:
  case AArch64::LDRHHui:
  case AArch64::LDRHroW:
  case AArch64::LDRHroX:
  case AArch64::LDRHui:
  case AArch64::LDRQl:
  case AArch64::LDRQroW:
  case AArch64::LDRQroX:
  case AArch64::LDRQui:
  case AArch64::LDRSBWroW:
  case AArch64::LDRSBWroX:
  case AArch64::LDRSBWui:
  case AArch64::LDRSBXroW:
  case AArch64::LDRSBXroX:
  case AArch64::LDRSBXui:
  case AArch64::LDRSHWroW:
  case AArch64::LDRSHWroX:
  case AArch64::LDRSHWui:
  case AArch64::LDRSHXroW:
  case AArch64::LDRSHXroX:
  case AArch64::LDRSHXui:
  case AArch64::LDRSWl:
  case AArch64::LDRSWroW:
  case AArch64::LDRSWroX:
  case AArch64::LDRSWui:
  case AArch64::LDRSl:
  case AArch64::LDRSroW:
  case AArch64::LDRSroX:
  case AArch64::LDRSui:
  case AArch64::LDRWl:
  case AArch64::LDRWroW:
  case AArch64::LDRWroX:
  case AArch64::LDRWui:
  case AArch64::LDRXl:
  case AArch64::LDRXroW:
  case AArch64::LDRXroX:
  case AArch64::LDRXui:
  case AArch64::LDURBBi:
  case AArch64::LDURBi:
  case AArch64::LDURDi:
  case AArch64::LDURHHi:
  case AArch64::LDURHi:
  case AArch64::LDURQi:
  case AArch64::LDURSBWi:
  case AArch64::LDURSBXi:
  case AArch64::LDURSHWi:
  case AArch64::LDURSHXi:
  case AArch64::LDURSWi:
  case AArch64::LDURSi:
  case AArch64::LDURWi:
  case AArch64::LDURXi:
    DestRegIdx = 0;
    BaseRegIdx = 1;
    OffsetIdx = 2;
    IsPrePost = false;
    break;

  case AArch64::LDRBBpost:
  case AArch64::LDRBBpre:
  case AArch64::LDRBpost:
  case AArch64::LDRBpre:
  case AArch64::LDRDpost:
  case AArch64::LDRDpre:
  case AArch64::LDRHHpost:
  case AArch64::LDRHHpre:
  case AArch64::LDRHpost:
  case AArch64::LDRHpre:
  case AArch64::LDRQpost:
  case AArch64::LDRQpre:
  case AArch64::LDRSBWpost:
  case AArch64::LDRSBWpre:
  case AArch64::LDRSBXpost:
  case AArch64::LDRSBXpre:
  case AArch64::LDRSHWpost:
  case AArch64::LDRSHWpre:
  case AArch64::LDRSHXpost:
  case AArch64::LDRSHXpre:
  case AArch64::LDRSWpost:
  case AArch64::LDRSWpre:
  case AArch64::LDRSpost:
  case AArch64::LDRSpre:
  case AArch64::LDRWpost:
  case AArch64::LDRWpre:
  case AArch64::LDRXpost:
  case AArch64::LDRXpre:
    DestRegIdx = 1;
    BaseRegIdx = 2;
    OffsetIdx = 3;
    IsPrePost = true;
    break;

  case AArch64::LDNPDi:
  case AArch64::LDNPQi:
  case AArch64::LDNPSi:
  case AArch64::LDPQi:
  case AArch64::LDPDi:
  case AArch64::LDPSi:
    DestRegIdx = -1;
    BaseRegIdx = 2;
    OffsetIdx = 3;
    IsPrePost = false;
    break;

  case AArch64::LDPSWi:
  case AArch64::LDPWi:
  case AArch64::LDPXi:
    DestRegIdx = 0;
    BaseRegIdx = 2;
    OffsetIdx = 3;
    IsPrePost = false;
    break;

  case AArch64::LDPQpost:
  case AArch64::LDPQpre:
  case AArch64::LDPDpost:
  case AArch64::LDPDpre:
  case AArch64::LDPSpost:
  case AArch64::LDPSpre:
    DestRegIdx = -1;
    BaseRegIdx = 3;
    OffsetIdx = 4;
    IsPrePost = true;
    break;

  case AArch64::LDPSWpost:
  case AArch64::LDPSWpre:
  case AArch64::LDPWpost:
  case AArch64::LDPWpre:
  case AArch64::LDPXpost:
  case AArch64::LDPXpre:
    DestRegIdx = 1;
    BaseRegIdx = 3;
    OffsetIdx = 4;
    IsPrePost = true;
    break;
  }

  // Loads from the stack pointer don't get prefetched.
  Register BaseReg = MI.getOperand(BaseRegIdx).getReg();
  if (BaseReg == AArch64::SP || BaseReg == AArch64::WSP)
    return std::nullopt;

  LoadInfo LI;
  LI.DestReg = DestRegIdx == -1 ? Register() : MI.getOperand(DestRegIdx).getReg();
  LI.BaseReg = BaseReg;
  LI.BaseRegIdx = BaseRegIdx;
  LI.OffsetOpnd = OffsetIdx == -1 ? nullptr : &MI.getOperand(OffsetIdx);
  LI.IsPrePost = IsPrePost;
  return LI;
}

static std::optional<unsigned> getTag(const TargetRegisterInfo *TRI,
                                      const MachineInstr &MI,
                                      const LoadInfo &LI) {
  unsigned Dest = LI.DestReg ? TRI->getEncodingValue(LI.DestReg) : 0;
  unsigned Base = TRI->getEncodingValue(LI.BaseReg);
  unsigned Off;
  if (LI.OffsetOpnd == nullptr)
    Off = 0;
  else if (LI.OffsetOpnd->isGlobal() || LI.OffsetOpnd->isSymbol() ||
           LI.OffsetOpnd->isCPI())
    return std::nullopt;
  else if (LI.OffsetOpnd->isReg())
    Off = (1 << 5) | TRI->getEncodingValue(LI.OffsetOpnd->getReg());
  else
    Off = LI.OffsetOpnd->getImm() >> 2;

  return makeTag(Dest, Base, Off);
}

void FalkorHWPFFix::runOnLoop(MachineLoop &L, MachineFunction &Fn) {
  // Build the initial tag map for the whole loop.
  TagMap.clear();
  for (MachineBasicBlock *MBB : L.getBlocks())
    for (MachineInstr &MI : *MBB) {
      std::optional<LoadInfo> LInfo = getLoadInfo(MI);
      if (!LInfo)
        continue;
      std::optional<unsigned> Tag = getTag(TRI, MI, *LInfo);
      if (!Tag)
        continue;
      TagMap[*Tag].push_back(&MI);
    }

  bool AnyCollisions = false;
  for (auto &P : TagMap) {
    auto Size = P.second.size();
    if (Size > 1) {
      for (auto *MI : P.second) {
        if (TII->isStridedAccess(*MI)) {
          AnyCollisions = true;
          break;
        }
      }
    }
    if (AnyCollisions)
      break;
  }
  // Nothing to fix.
  if (!AnyCollisions)
    return;

  MachineRegisterInfo &MRI = Fn.getRegInfo();

  // Go through all the basic blocks in the current loop and fix any streaming
  // loads to avoid collisions with any other loads.
  LiveRegUnits LR(*TRI);
  for (MachineBasicBlock *MBB : L.getBlocks()) {
    LR.clear();
    LR.addLiveOuts(*MBB);
    for (auto I = MBB->rbegin(); I != MBB->rend(); LR.stepBackward(*I), ++I) {
      MachineInstr &MI = *I;
      if (!TII->isStridedAccess(MI))
        continue;

      std::optional<LoadInfo> OptLdI = getLoadInfo(MI);
      if (!OptLdI)
        continue;
      LoadInfo LdI = *OptLdI;
      std::optional<unsigned> OptOldTag = getTag(TRI, MI, LdI);
      if (!OptOldTag)
        continue;
      auto &OldCollisions = TagMap[*OptOldTag];
      if (OldCollisions.size() <= 1)
        continue;

      bool Fixed = false;
      LLVM_DEBUG(dbgs() << "Attempting to fix tag collision: " << MI);

      if (!DebugCounter::shouldExecute(FixCounter)) {
        LLVM_DEBUG(dbgs() << "Skipping fix due to debug counter:\n  " << MI);
        continue;
      }

      // Add the non-base registers of MI as live so we don't use them as
      // scratch registers.
      for (unsigned OpI = 0, OpE = MI.getNumOperands(); OpI < OpE; ++OpI) {
        if (OpI == static_cast<unsigned>(LdI.BaseRegIdx))
          continue;
        MachineOperand &MO = MI.getOperand(OpI);
        if (MO.isReg() && MO.readsReg())
          LR.addReg(MO.getReg());
      }

      for (unsigned ScratchReg : AArch64::GPR64RegClass) {
        if (!LR.available(ScratchReg) || MRI.isReserved(ScratchReg))
          continue;

        LoadInfo NewLdI(LdI);
        NewLdI.BaseReg = ScratchReg;
        unsigned NewTag = *getTag(TRI, MI, NewLdI);
        // Scratch reg tag would collide too, so don't use it.
        if (TagMap.count(NewTag))
          continue;

        LLVM_DEBUG(dbgs() << "Changing base reg to: "
                          << printReg(ScratchReg, TRI) << '\n');

        // Rewrite:
        //   Xd = LOAD Xb, off
        // to:
        //   Xc = MOV Xb
        //   Xd = LOAD Xc, off
        DebugLoc DL = MI.getDebugLoc();
        BuildMI(*MBB, &MI, DL, TII->get(AArch64::ORRXrs), ScratchReg)
            .addReg(AArch64::XZR)
            .addReg(LdI.BaseReg)
            .addImm(0);
        MachineOperand &BaseOpnd = MI.getOperand(LdI.BaseRegIdx);
        BaseOpnd.setReg(ScratchReg);

        // If the load does a pre/post increment, then insert a MOV after as
        // well to update the real base register.
        if (LdI.IsPrePost) {
          LLVM_DEBUG(dbgs() << "Doing post MOV of incremented reg: "
                            << printReg(ScratchReg, TRI) << '\n');
          MI.getOperand(0).setReg(
              ScratchReg); // Change tied operand pre/post update dest.
          BuildMI(*MBB, std::next(MachineBasicBlock::iterator(MI)), DL,
                  TII->get(AArch64::ORRXrs), LdI.BaseReg)
              .addReg(AArch64::XZR)
              .addReg(ScratchReg)
              .addImm(0);
        }

        for (int I = 0, E = OldCollisions.size(); I != E; ++I)
          if (OldCollisions[I] == &MI) {
            std::swap(OldCollisions[I], OldCollisions[E - 1]);
            OldCollisions.pop_back();
            break;
          }

        // Update TagMap to reflect instruction changes to reduce the number
        // of later MOVs to be inserted.  This needs to be done after
        // OldCollisions is updated since it may be relocated by this
        // insertion.
        TagMap[NewTag].push_back(&MI);
        ++NumCollisionsAvoided;
        Fixed = true;
        Modified = true;
        break;
      }
      if (!Fixed)
        ++NumCollisionsNotAvoided;
    }
  }
}

bool FalkorHWPFFix::runOnMachineFunction(MachineFunction &Fn) {
  auto &ST = Fn.getSubtarget<AArch64Subtarget>();
  if (ST.getProcFamily() != AArch64Subtarget::Falkor)
    return false;

  if (skipFunction(Fn.getFunction()))
    return false;

  TII = static_cast<const AArch64InstrInfo *>(ST.getInstrInfo());
  TRI = ST.getRegisterInfo();

  MachineLoopInfo &LI = getAnalysis<MachineLoopInfoWrapperPass>().getLI();

  Modified = false;

  for (MachineLoop *I : LI)
    for (MachineLoop *L : depth_first(I))
      // Only process inner-loops
      if (L->isInnermost())
        runOnLoop(*L, Fn);

  return Modified;
}

FunctionPass *llvm::createFalkorHWPFFixPass() { return new FalkorHWPFFix(); }
