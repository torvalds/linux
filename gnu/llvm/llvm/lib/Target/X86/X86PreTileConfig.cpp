//===-- X86PreTileConfig.cpp - Tile Register Pre-configure-----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Pass to pre-config the shapes of AMX registers
/// AMX register needs to be configured before use. The shapes of AMX register
/// are encoded in the 1st and 2nd machine operand of AMX pseudo instructions.
///
/// The instruction ldtilecfg is used to config the shapes. It must be reachable
/// for all variable shapes. ldtilecfg will be inserted more than once if we
/// cannot find a dominating point for all AMX instructions.
///
/// The configure register is caller saved according to ABI. We need to insert
/// ldtilecfg again after the call instruction if callee clobbers any AMX
/// registers.
///
/// This pass calculates all points that ldtilecfg need to be inserted to and
/// insert them. It reports error if the reachability conditions aren't met.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86MachineFunctionInfo.h"
#include "X86RegisterInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "tile-pre-config"

static void emitErrorMsg(MachineFunction &MF) {
  LLVMContext &Context = MF.getFunction().getContext();
  Context.emitError(
      MF.getName() +
      ": Failed to config tile register, please define the shape earlier");
}

namespace {

struct MIRef {
  MachineInstr *MI = nullptr;
  MachineBasicBlock *MBB = nullptr;
  // A virtual position for instruction that will be inserted after MI.
  size_t Pos = 0;
  MIRef() = default;
  MIRef(MachineBasicBlock *MBB) : MBB(MBB) {
    for (auto I = MBB->begin(), E = MBB->end(); I != E && I->isPHI();
         ++I, ++Pos)
      MI = &*I;
  }
  MIRef(MachineInstr *MI)
      : MI(MI), MBB(MI->getParent()),
        Pos(std::distance(MBB->instr_begin(), ++MI->getIterator())) {}
  MIRef(MachineInstr *MI, MachineBasicBlock *MBB)
      : MI(MI), MBB(MBB),
        Pos(std::distance(MBB->instr_begin(), ++MI->getIterator())) {}
  MIRef(MachineInstr *MI, MachineBasicBlock *MBB, size_t Pos)
      : MI(MI), MBB(MBB), Pos(Pos) {}
  operator bool() const { return MBB != nullptr; }
  bool operator==(const MIRef &RHS) const {
    return MI == RHS.MI && MBB == RHS.MBB;
  }
  bool operator!=(const MIRef &RHS) const { return !(*this == RHS); }
  bool operator<(const MIRef &RHS) const {
    // Comparison between different BBs happens when inserting a MIRef into set.
    // So we compare MBB first to make the insertion happy.
    return MBB < RHS.MBB || (MBB == RHS.MBB && Pos < RHS.Pos);
  }
  bool operator>(const MIRef &RHS) const {
    // Comparison between different BBs happens when inserting a MIRef into set.
    // So we compare MBB first to make the insertion happy.
    return MBB > RHS.MBB || (MBB == RHS.MBB && Pos > RHS.Pos);
  }
};

struct BBInfo {
  MIRef FirstAMX;
  MIRef LastCall;
  bool HasAMXRegLiveIn = false;
  bool TileCfgForbidden = false;
  bool NeedTileCfgLiveIn = false;
};

class X86PreTileConfig : public MachineFunctionPass {
  MachineRegisterInfo *MRI = nullptr;
  const MachineLoopInfo *MLI = nullptr;
  SmallSet<MachineInstr *, 8> DefVisited;
  DenseMap<MachineBasicBlock *, BBInfo> BBVisitedInfo;
  DenseMap<MachineBasicBlock *, SmallVector<MIRef, 8>> ShapeBBs;

  /// Check if the callee will clobber AMX registers.
  bool isDestructiveCall(MachineInstr &MI, BitVector UsableRegs) {
    auto Iter = llvm::find_if(
        MI.operands(), [](MachineOperand &MO) { return MO.isRegMask(); });
    if (Iter == MI.operands_end())
      return false;
    UsableRegs.clearBitsInMask(Iter->getRegMask());
    return !UsableRegs.none();
  }

  /// Check if MI is AMX pseudo instruction.
  bool isAMXInstruction(MachineInstr &MI) {
    if (MI.isPHI() || MI.isDebugInstr() || MI.getNumOperands() < 3)
      return false;
    MachineOperand &MO = MI.getOperand(0);
    // We can simply check if it is AMX instruction by its def.
    // But we should exclude old API which uses physical registers.
    if (MO.isReg() && MO.getReg().isVirtual() &&
        MRI->getRegClass(MO.getReg())->getID() == X86::TILERegClassID) {
      collectShapeInfo(MI);
      return true;
    }
    // PTILESTOREDV is the only exception that doesn't def a AMX register.
    return MI.getOpcode() == X86::PTILESTOREDV;
  }

  /// Check if it is an edge from loop bottom to loop head.
  bool isLoopBackEdge(MachineBasicBlock *Header, MachineBasicBlock *Bottom) {
    if (!MLI->isLoopHeader(Header))
      return false;
    auto *ML = MLI->getLoopFor(Header);
    if (ML->contains(Bottom) && ML->isLoopLatch(Bottom))
      return true;

    return false;
  }

  /// Collect the shape def information for later use.
  void collectShapeInfo(MachineInstr &MI);

  /// Try to hoist shapes definded below AMX instructions.
  bool hoistShapesInBB(MachineBasicBlock *MBB, SmallVectorImpl<MIRef> &Shapes) {
    MIRef &FirstAMX = BBVisitedInfo[MBB].FirstAMX;
    auto FirstShapeBelowAMX = llvm::lower_bound(Shapes, FirstAMX);
    auto InsertPoint = FirstAMX.MI->getIterator();
    for (auto I = FirstShapeBelowAMX, E = Shapes.end(); I != E; ++I) {
      // Do not hoist instructions that access memory.
      if (I->MI->mayLoadOrStore())
        return false;
      for (auto &MO : I->MI->operands()) {
        if (MO.isDef())
          continue;
        // Do not hoist instructions if the sources' def under AMX instruction.
        // TODO: We can handle isMoveImmediate MI here.
        if (MO.isReg() && MIRef(MRI->getVRegDef(MO.getReg())) > FirstAMX)
          return false;
        // TODO: Maybe need more checks here.
      }
      MBB->insert(InsertPoint, I->MI->removeFromParent());
    }
    // We only need to mark the last shape in the BB now.
    Shapes.clear();
    Shapes.push_back(MIRef(&*--InsertPoint, MBB));
    return true;
  }

public:
  X86PreTileConfig() : MachineFunctionPass(ID) {}

  /// Return the pass name.
  StringRef getPassName() const override {
    return "Tile Register Pre-configure";
  }

  /// X86PreTileConfig analysis usage.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  /// Clear MF related structures.
  void releaseMemory() override {
    ShapeBBs.clear();
    DefVisited.clear();
    BBVisitedInfo.clear();
  }

  /// Perform ldtilecfg instructions inserting.
  bool runOnMachineFunction(MachineFunction &MF) override;

  static char ID;
};

} // end anonymous namespace

char X86PreTileConfig::ID = 0;

INITIALIZE_PASS_BEGIN(X86PreTileConfig, "tilepreconfig",
                      "Tile Register Pre-configure", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(X86PreTileConfig, "tilepreconfig",
                    "Tile Register Pre-configure", false, false)

void X86PreTileConfig::collectShapeInfo(MachineInstr &MI) {
  auto RecordShape = [&](MachineInstr *MI, MachineBasicBlock *MBB) {
    MIRef MIR(MI, MBB);
    auto I = llvm::lower_bound(ShapeBBs[MBB], MIR);
    if (I == ShapeBBs[MBB].end() || *I != MIR)
      ShapeBBs[MBB].insert(I, MIR);
  };

  SmallVector<Register, 8> WorkList(
      {MI.getOperand(1).getReg(), MI.getOperand(2).getReg()});
  while (!WorkList.empty()) {
    Register R = WorkList.pop_back_val();
    MachineInstr *DefMI = MRI->getVRegDef(R);
    assert(DefMI && "R must has one define instruction");
    MachineBasicBlock *DefMBB = DefMI->getParent();
    if (DefMI->isMoveImmediate() || !DefVisited.insert(DefMI).second)
      continue;
    if (DefMI->isPHI()) {
      for (unsigned I = 1; I < DefMI->getNumOperands(); I += 2)
        if (isLoopBackEdge(DefMBB, DefMI->getOperand(I + 1).getMBB()))
          RecordShape(DefMI, DefMBB); // In this case, PHI is also a shape def.
        else
          WorkList.push_back(DefMI->getOperand(I).getReg());
    } else {
      RecordShape(DefMI, DefMBB);
    }
  }
}

bool X86PreTileConfig::runOnMachineFunction(MachineFunction &MF) {
  X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
  // Early exit in the common case of non-AMX code.
  if (X86FI->getAMXProgModel() != AMXProgModelEnum::ManagedRA)
    return false;

  const X86Subtarget &ST = MF.getSubtarget<X86Subtarget>();
  const TargetInstrInfo *TII = ST.getInstrInfo();
  const TargetRegisterInfo *TRI = ST.getRegisterInfo();
  const TargetRegisterClass *RC = TRI->getRegClass(X86::TILERegClassID);

  BitVector AMXRegs(TRI->getNumRegs());
  for (unsigned I = 0; I < RC->getNumRegs(); I++)
    AMXRegs.set(X86::TMM0 + I);

  // Iterate MF to collect information.
  MRI = &MF.getRegInfo();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  SmallSet<MIRef, 8> CfgNeedInsert;
  SmallVector<MachineBasicBlock *, 8> CfgLiveInBBs;
  for (auto &MBB : MF) {
    size_t Pos = 0;
    for (auto &MI : MBB) {
      ++Pos;
      if (isAMXInstruction(MI)) {
        // If there's call before the AMX, we need to reload tile config.
        if (BBVisitedInfo[&MBB].LastCall)
          CfgNeedInsert.insert(BBVisitedInfo[&MBB].LastCall);
        else // Otherwise, we need tile config to live in this BB.
          BBVisitedInfo[&MBB].NeedTileCfgLiveIn = true;
        // Always record the first AMX in case there's shape def after it.
        if (!BBVisitedInfo[&MBB].FirstAMX)
          BBVisitedInfo[&MBB].FirstAMX = MIRef(&MI, &MBB, Pos);
      } else if (MI.isCall() && isDestructiveCall(MI, AMXRegs)) {
        // Record the call only if the callee clobbers all AMX registers.
        BBVisitedInfo[&MBB].LastCall = MIRef(&MI, &MBB, Pos);
      }
    }
    if (BBVisitedInfo[&MBB].NeedTileCfgLiveIn) {
      if (&MBB == &MF.front())
        CfgNeedInsert.insert(MIRef(&MBB));
      else
        CfgLiveInBBs.push_back(&MBB);
    }
    if (BBVisitedInfo[&MBB].FirstAMX || BBVisitedInfo[&MBB].HasAMXRegLiveIn)
      for (auto *Succ : MBB.successors())
        if (!isLoopBackEdge(Succ, &MBB))
          BBVisitedInfo[Succ].HasAMXRegLiveIn = true;
  }

  // Update NeedTileCfgLiveIn for predecessors.
  while (!CfgLiveInBBs.empty()) {
    MachineBasicBlock *MBB = CfgLiveInBBs.pop_back_val();
    for (auto *Pred : MBB->predecessors()) {
      if (BBVisitedInfo[Pred].LastCall) {
        CfgNeedInsert.insert(BBVisitedInfo[Pred].LastCall);
      } else if (!BBVisitedInfo[Pred].NeedTileCfgLiveIn) {
        BBVisitedInfo[Pred].NeedTileCfgLiveIn = true;
        if (Pred == &MF.front())
          CfgNeedInsert.insert(MIRef(Pred));
        else
          CfgLiveInBBs.push_back(Pred);
      }
    }
  }

  // There's no AMX instruction if we didn't find a tile config live in point.
  if (CfgNeedInsert.empty())
    return false;

  // Avoid to insert ldtilecfg before any shape defs.
  SmallVector<MachineBasicBlock *, 8> WorkList;
  for (auto &I : ShapeBBs) {
    // TODO: We can hoist shapes across BBs here.
    if (BBVisitedInfo[I.first].HasAMXRegLiveIn) {
      // We are not able to config tile registers since the shape to config
      // is not defined yet. Emit error message and continue. The function
      // would not config tile registers.
      emitErrorMsg(MF);
      return false;
    }
    if (BBVisitedInfo[I.first].FirstAMX &&
        BBVisitedInfo[I.first].FirstAMX < I.second.back() &&
        !hoistShapesInBB(I.first, I.second)) {
      emitErrorMsg(MF);
      return false;
    }
    WorkList.push_back(I.first);
  }
  while (!WorkList.empty()) {
    MachineBasicBlock *MBB = WorkList.pop_back_val();
    for (auto *Pred : MBB->predecessors()) {
      if (!BBVisitedInfo[Pred].TileCfgForbidden && !isLoopBackEdge(MBB, Pred)) {
        BBVisitedInfo[Pred].TileCfgForbidden = true;
        WorkList.push_back(Pred);
      }
    }
  }

  DebugLoc DL;
  SmallSet<MIRef, 8> VisitedOrInserted;
  int SS = MF.getFrameInfo().CreateStackObject(
      ST.getTileConfigSize(), ST.getTileConfigAlignment(), false);

  // Try to insert for the tile config live in points.
  for (const auto &I : CfgNeedInsert) {
    SmallSet<MIRef, 8> InsertPoints;
    SmallVector<MIRef, 8> WorkList({I});
    while (!WorkList.empty()) {
      MIRef I = WorkList.pop_back_val();
      if (!VisitedOrInserted.count(I)) {
        if (!BBVisitedInfo[I.MBB].TileCfgForbidden) {
          // If the BB is all shapes reachable, stop sink and try to insert.
          InsertPoints.insert(I);
        } else {
          // Avoid the BB to be multi visited.
          VisitedOrInserted.insert(I);
          // Sink the inserting point along the chain with NeedTileCfgLiveIn =
          // true when MBB isn't all shapes reachable.
          for (auto *Succ : I.MBB->successors())
            if (BBVisitedInfo[Succ].NeedTileCfgLiveIn)
              WorkList.push_back(MIRef(Succ));
        }
      }
    }

    // A given point might be forked due to shape conditions are not met.
    for (MIRef I : InsertPoints) {
      // Make sure we insert ldtilecfg after the last shape def in MBB.
      if (ShapeBBs.count(I.MBB) && I < ShapeBBs[I.MBB].back())
        I = ShapeBBs[I.MBB].back();
      // There're chances the MBB is sunk more than once. Record it to avoid
      // multi insert.
      if (VisitedOrInserted.insert(I).second) {
        auto II = I.MI ? I.MI->getIterator() : I.MBB->instr_begin();
        addFrameReference(BuildMI(*I.MBB, ++II, DL, TII->get(X86::PLDTILECFGV)),
                          SS);
      }
    }
  }

  // Zero stack slot.
  MachineBasicBlock &MBB = MF.front();
  MachineInstr *MI = &*MBB.begin();
  if (ST.hasAVX512()) {
    Register Zmm = MRI->createVirtualRegister(&X86::VR512RegClass);
    BuildMI(MBB, MI, DL, TII->get(X86::AVX512_512_SET0), Zmm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(X86::VMOVUPSZmr)), SS)
        .addReg(Zmm);
  } else if (ST.hasAVX2()) {
    Register Ymm = MRI->createVirtualRegister(&X86::VR256RegClass);
    BuildMI(MBB, MI, DL, TII->get(X86::AVX_SET0), Ymm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(X86::VMOVUPSYmr)), SS)
        .addReg(Ymm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(X86::VMOVUPSYmr)), SS, 32)
        .addReg(Ymm);
  } else {
    assert(ST.hasSSE2() && "AMX should assume SSE2 enabled");
    unsigned StoreOpc = ST.hasAVX() ? X86::VMOVUPSmr : X86::MOVUPSmr;
    Register Xmm = MRI->createVirtualRegister(&X86::VR128RegClass);
    BuildMI(MBB, MI, DL, TII->get(X86::V_SET0), Xmm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(StoreOpc)), SS).addReg(Xmm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(StoreOpc)), SS, 16)
        .addReg(Xmm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(StoreOpc)), SS, 32)
        .addReg(Xmm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(StoreOpc)), SS, 48)
        .addReg(Xmm);
  }
  // Fill in the palette first.
  addFrameReference(BuildMI(MBB, MI, DL, TII->get(X86::MOV8mi)), SS).addImm(1);

  return true;
}

FunctionPass *llvm::createX86PreTileConfigPass() {
  return new X86PreTileConfig();
}
