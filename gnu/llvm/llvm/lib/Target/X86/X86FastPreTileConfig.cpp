//===-- X86FastPreTileConfig.cpp - Fast Tile Register Configure------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Pass to preconfig the shape of physical tile registers
/// It inserts ldtilecfg ahead of each group of tile registers. The algorithm
/// walk each instruction of basic block in reverse order. All the tile
/// registers that live out the basic block would be spilled and reloaded
/// before its user. It also check the depenedency of the shape to ensure
/// the shape is defined before ldtilecfg.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86MachineFunctionInfo.h"
#include "X86RegisterInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "fastpretileconfig"

STATISTIC(NumStores, "Number of stores added");
STATISTIC(NumLoads, "Number of loads added");

namespace {

class X86FastPreTileConfig : public MachineFunctionPass {
  MachineFunction *MF = nullptr;
  const X86Subtarget *ST = nullptr;
  const TargetInstrInfo *TII = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  X86MachineFunctionInfo *X86FI = nullptr;
  MachineFrameInfo *MFI = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  MachineBasicBlock *MBB = nullptr;
  int CfgSS = -1;
  struct PHIInfo {
    Register Row;
    Register Col;
    Register StackAddr;
  };
  DenseMap<MachineInstr *, struct PHIInfo> VisitedPHIs;

  /// Maps virtual regs to the frame index where these values are spilled.
  IndexedMap<int, VirtReg2IndexFunctor> StackSlotForVirtReg;

  /// Has a bit set for tile virtual register for which it was determined
  /// that it is alive across blocks.
  BitVector MayLiveAcrossBlocks;

  int getStackSpaceFor(Register VirtReg);
  void InitializeTileConfigStackSpace();
  bool mayLiveOut(Register VirtReg, MachineInstr *CfgMI);
  void spill(MachineBasicBlock::iterator Before, Register VirtReg, bool Kill);
  void reload(MachineBasicBlock::iterator UseMI, Register VirtReg,
              MachineOperand *RowMO, MachineOperand *ColMO);
  void canonicalizePHIs(MachineBasicBlock &MBB);
  void convertPHI(MachineBasicBlock *MBB, MachineInstr &PHI);
  void convertPHIs(MachineBasicBlock &MBB);
  bool configBasicBlock(MachineBasicBlock &MBB);

public:
  X86FastPreTileConfig() : MachineFunctionPass(ID), StackSlotForVirtReg(-1) {}

  /// Return the pass name.
  StringRef getPassName() const override {
    return "Fast Tile Register Preconfigure";
  }

  /// Perform tile register configure.
  bool runOnMachineFunction(MachineFunction &MFunc) override;

  static char ID;
};

} // end anonymous namespace

char X86FastPreTileConfig::ID = 0;

INITIALIZE_PASS_BEGIN(X86FastPreTileConfig, DEBUG_TYPE,
                      "Fast Tile Register Preconfigure", false, false)
INITIALIZE_PASS_END(X86FastPreTileConfig, DEBUG_TYPE,
                    "Fast Tile Register Preconfigure", false, false)

static bool dominates(MachineBasicBlock &MBB,
                      MachineBasicBlock::const_iterator A,
                      MachineBasicBlock::const_iterator B) {
  auto MBBEnd = MBB.end();
  if (B == MBBEnd)
    return true;

  MachineBasicBlock::const_iterator I = MBB.begin();
  for (; &*I != A && &*I != B; ++I)
    ;

  return &*I == A;
}

/// This allocates space for the specified virtual register to be held on the
/// stack.
int X86FastPreTileConfig::getStackSpaceFor(Register VirtReg) {
  // Find the location Reg would belong...
  int SS = StackSlotForVirtReg[VirtReg];
  // Already has space allocated?
  if (SS != -1)
    return SS;

  // Allocate a new stack object for this spill location...
  const TargetRegisterClass &RC = *MRI->getRegClass(VirtReg);
  unsigned Size = TRI->getSpillSize(RC);
  Align Alignment = TRI->getSpillAlign(RC);
  int FrameIdx = MFI->CreateSpillStackObject(Size, Alignment);

  // Assign the slot.
  StackSlotForVirtReg[VirtReg] = FrameIdx;
  return FrameIdx;
}

/// Returns false if \p VirtReg is known to not live out of the current config.
/// If \p VirtReg live out of the current MBB, it must live out of the current
/// config
bool X86FastPreTileConfig::mayLiveOut(Register VirtReg, MachineInstr *CfgMI) {
  if (MayLiveAcrossBlocks.test(Register::virtReg2Index(VirtReg)))
    return true;

  for (const MachineInstr &UseInst : MRI->use_nodbg_instructions(VirtReg)) {
    if (UseInst.getParent() != MBB) {
      MayLiveAcrossBlocks.set(Register::virtReg2Index(VirtReg));
      return true;
    }

    // The use and def are in the same MBB. If the tile register is
    // reconfigured, it is crobbered and we need to spill and reload
    // tile register.
    if (CfgMI) {
      if (dominates(*MBB, *CfgMI, UseInst)) {
        MayLiveAcrossBlocks.set(Register::virtReg2Index(VirtReg));
        return true;
      }
    }
  }

  return false;
}

void X86FastPreTileConfig::InitializeTileConfigStackSpace() {
  MachineBasicBlock &MBB = MF->front();
  MachineInstr *MI = &*MBB.getFirstNonPHI();
  DebugLoc DL;
  if (ST->hasAVX512()) {
    Register Zmm = MRI->createVirtualRegister(&X86::VR512RegClass);
    BuildMI(MBB, MI, DL, TII->get(X86::AVX512_512_SET0), Zmm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(X86::VMOVUPSZmr)), CfgSS)
        .addReg(Zmm);
  } else if (ST->hasAVX2()) {
    Register Ymm = MRI->createVirtualRegister(&X86::VR256RegClass);
    BuildMI(MBB, MI, DL, TII->get(X86::AVX_SET0), Ymm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(X86::VMOVUPSYmr)), CfgSS)
        .addReg(Ymm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(X86::VMOVUPSYmr)), CfgSS,
                      32)
        .addReg(Ymm);
  } else {
    assert(ST->hasSSE2() && "AMX should assume SSE2 enabled");
    unsigned StoreOpc = ST->hasAVX() ? X86::VMOVUPSmr : X86::MOVUPSmr;
    Register Xmm = MRI->createVirtualRegister(&X86::VR128RegClass);
    BuildMI(MBB, MI, DL, TII->get(X86::V_SET0), Xmm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(StoreOpc)), CfgSS)
        .addReg(Xmm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(StoreOpc)), CfgSS, 16)
        .addReg(Xmm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(StoreOpc)), CfgSS, 32)
        .addReg(Xmm);
    addFrameReference(BuildMI(MBB, MI, DL, TII->get(StoreOpc)), CfgSS, 48)
        .addReg(Xmm);
  }
  // Fill in the palette first.
  addFrameReference(BuildMI(MBB, MI, DL, TII->get(X86::MOV8mi)), CfgSS)
      .addImm(1);
}

/// Insert spill instruction for \p AssignedReg before \p Before.
/// TODO: Update DBG_VALUEs with \p VirtReg operands with the stack slot.
void X86FastPreTileConfig::spill(MachineBasicBlock::iterator Before,
                                 Register VirtReg, bool Kill) {
  LLVM_DEBUG(dbgs() << "Spilling " << printReg(VirtReg, TRI) << " \n");
  int FI = getStackSpaceFor(VirtReg);
  LLVM_DEBUG(dbgs() << " to stack slot #" << FI << '\n');

  const TargetRegisterClass &RC = *MRI->getRegClass(VirtReg);
  // Don't need shape information for tile store, becasue it is adjacent to
  // the tile def instruction.
  TII->storeRegToStackSlot(*MBB, Before, VirtReg, Kill, FI, &RC, TRI,
                           Register());
  ++NumStores;

  // TODO: update DBG_VALUEs
}

/// Insert reload instruction for \p PhysReg before \p Before.
void X86FastPreTileConfig::reload(MachineBasicBlock::iterator UseMI,
                                  Register OrigReg, MachineOperand *RowMO,
                                  MachineOperand *ColMO) {
  int FI = getStackSpaceFor(OrigReg);
  const TargetRegisterClass &RC = *MRI->getRegClass(OrigReg);
  Register TileReg;
  // Fold copy to tileload
  // BB1:
  // spill src to s
  //
  // BB2:
  // t = copy src
  // -->
  // t = tileload (s)
  if (UseMI->isCopy())
    TileReg = UseMI->getOperand(0).getReg();
  else
    TileReg = MRI->createVirtualRegister(&RC);
  // Can't use TII->loadRegFromStackSlot(), because we need the shape
  // information for reload.
  // tileloadd (%sp, %idx), %tmm
  unsigned Opc = X86::PTILELOADDV;
  Register StrideReg = MRI->createVirtualRegister(&X86::GR64_NOSPRegClass);
  // FIXME: MBB is not the parent of UseMI.
  MachineInstr *NewMI = BuildMI(*UseMI->getParent(), UseMI, DebugLoc(),
                                TII->get(X86::MOV64ri), StrideReg)
                            .addImm(64);
  NewMI = addFrameReference(
      BuildMI(*UseMI->getParent(), UseMI, DebugLoc(), TII->get(Opc), TileReg)
          .addReg(RowMO->getReg())
          .addReg(ColMO->getReg()),
      FI);
  MachineOperand &MO = NewMI->getOperand(5);
  MO.setReg(StrideReg);
  MO.setIsKill(true);
  RowMO->setIsKill(false);
  ColMO->setIsKill(false);
  // Erase copy instruction after it is folded.
  if (UseMI->isCopy()) {
    UseMI->eraseFromParent();
  } else {
    // Replace the register in the user MI.
    for (auto &MO : UseMI->operands()) {
      if (MO.isReg() && MO.getReg() == OrigReg)
        MO.setReg(TileReg);
    }
  }

  ++NumLoads;
  LLVM_DEBUG(dbgs() << "Reloading " << printReg(OrigReg, TRI) << " into "
                    << printReg(TileReg, TRI) << '\n');
}

static bool isTileDef(MachineRegisterInfo *MRI, MachineInstr &MI) {
  // The instruction must have 3 operands: tile def, row, col.
  if (MI.isDebugInstr() || MI.getNumOperands() < 3 || !MI.isPseudo())
    return false;
  MachineOperand &MO = MI.getOperand(0);

  if (MO.isReg()) {
    Register Reg = MO.getReg();
    // FIXME it may be used after Greedy RA and the physical
    // register is not rewritten yet.
    if (Reg.isVirtual() &&
        MRI->getRegClass(Reg)->getID() == X86::TILERegClassID)
      return true;
    if (Reg >= X86::TMM0 && Reg <= X86::TMM7)
      return true;
  }

  return false;
}

static ShapeT getShape(MachineRegisterInfo *MRI, Register TileReg) {
  MachineInstr *MI = MRI->getVRegDef(TileReg);
  if (isTileDef(MRI, *MI)) {
    MachineOperand *RowMO = &MI->getOperand(1);
    MachineOperand *ColMO = &MI->getOperand(2);
    return ShapeT(RowMO, ColMO, MRI);
  } else if (MI->isCopy()) {
    TileReg = MI->getOperand(1).getReg();
    return getShape(MRI, TileReg);
  }

  // The def should not be PHI node, because we walk the MBB in reverse post
  // order.
  assert(MI->isPHI() && "Unexpected PHI when get shape.");
  llvm_unreachable("Unexpected MI when get shape.");
}

// BB0:
// spill t0 to s0
// BB1:
// spill t1 to s1
//
// BB2:
// t = phi [t0, bb0] [t1, bb1]
// -->
// row = phi [r0, bb0] [r1, bb1]
// col = phi [c0, bb0] [c1, bb1]
//   s = phi [s0, bb0] [s1, bb1]
//   t = tileload row, col, s
// The new instruction is inserted at the end of the phi node. The order
// of the original phi node is not ensured.
void X86FastPreTileConfig::convertPHI(MachineBasicBlock *MBB,
                                      MachineInstr &PHI) {
  // 1. Create instruction to get stack slot address of each incoming block.
  // 2. Create PHI node for the stack address.
  // 3. Create PHI node for shape. If one of the incoming shape is immediate
  //    use the immediate and delete the PHI node.
  // 4. Create tileload instruction from the stack address.
  Register StackAddrReg = MRI->createVirtualRegister(&X86::GR64_NOSPRegClass);
  MachineInstrBuilder AddrPHI = BuildMI(*MBB, ++PHI.getIterator(), DebugLoc(),
                                        TII->get(X86::PHI), StackAddrReg);
  Register RowReg = MRI->createVirtualRegister(&X86::GR16RegClass);
  MachineInstrBuilder RowPHI = BuildMI(*MBB, ++PHI.getIterator(), DebugLoc(),
                                       TII->get(X86::PHI), RowReg);
  Register ColReg = MRI->createVirtualRegister(&X86::GR16RegClass);
  MachineInstrBuilder ColPHI = BuildMI(*MBB, ++PHI.getIterator(), DebugLoc(),
                                       TII->get(X86::PHI), ColReg);
  // Record the mapping of phi node and its row/column information.
  VisitedPHIs[&PHI] = {RowReg, ColReg, StackAddrReg};

  for (unsigned I = 1, E = PHI.getNumOperands(); I != E; I += 2) {
    // Get the 2 incoming value of tile register and MBB.
    Register InTileReg = PHI.getOperand(I).getReg();
    // Mark it as liveout, so that it will be spilled when visit
    // the incoming MBB. Otherwise since phi will be deleted, it
    // would miss spill when visit incoming MBB.
    MayLiveAcrossBlocks.set(Register::virtReg2Index(InTileReg));
    MachineBasicBlock *InMBB = PHI.getOperand(I + 1).getMBB();

    MachineInstr *TileDefMI = MRI->getVRegDef(InTileReg);
    MachineBasicBlock::iterator InsertPos;
    if (TileDefMI->isPHI()) {
      InsertPos = TileDefMI->getParent()->getFirstNonPHI();
      if (VisitedPHIs.count(TileDefMI)) { // circular phi reference
        //        def t1
        //       /       \
        //  def t2       t3 = phi(t1, t4) <--
        //       \       /                  |
        //      t4 = phi(t2, t3)-------------
        //
        // For each (row, column and stack address) append phi incoming value.
        // Create r3 = phi(r1, r4)
        // Create r4 = phi(r2, r3)
        Register InRowReg = VisitedPHIs[TileDefMI].Row;
        Register InColReg = VisitedPHIs[TileDefMI].Col;
        Register InStackAddrReg = VisitedPHIs[TileDefMI].StackAddr;
        RowPHI.addReg(InRowReg).addMBB(InMBB);
        ColPHI.addReg(InColReg).addMBB(InMBB);
        AddrPHI.addReg(InStackAddrReg).addMBB(InMBB);
        continue;
      } else {
        // Recursively convert PHI to tileload
        convertPHI(TileDefMI->getParent(), *TileDefMI);
        // The PHI node is coverted to tileload instruction. Get the stack
        // address from tileload operands.
        MachineInstr *TileLoad = MRI->getVRegDef(InTileReg);
        assert(TileLoad && TileLoad->getOpcode() == X86::PTILELOADDV);
        Register InRowReg = TileLoad->getOperand(1).getReg();
        Register InColReg = TileLoad->getOperand(2).getReg();
        Register InStackAddrReg = TileLoad->getOperand(3).getReg();
        RowPHI.addReg(InRowReg).addMBB(InMBB);
        ColPHI.addReg(InColReg).addMBB(InMBB);
        AddrPHI.addReg(InStackAddrReg).addMBB(InMBB);
      }
    } else {
      InsertPos = TileDefMI->getIterator();

      // Fill the incoming operand of row/column phi instruction.
      ShapeT Shape = getShape(MRI, InTileReg);
      Shape.getRow()->setIsKill(false);
      Shape.getCol()->setIsKill(false);
      RowPHI.addReg(Shape.getRow()->getReg()).addMBB(InMBB);
      ColPHI.addReg(Shape.getCol()->getReg()).addMBB(InMBB);

      // The incoming tile register live out of its def BB, it would be spilled.
      // Create MI to get the spill stack slot address for the tile register
      int FI = getStackSpaceFor(InTileReg);
      Register InStackAddrReg =
          MRI->createVirtualRegister(&X86::GR64_NOSPRegClass);
      addOffset(BuildMI(*TileDefMI->getParent(), InsertPos, DebugLoc(),
                        TII->get(X86::LEA64r), InStackAddrReg)
                    .addFrameIndex(FI),
                0);
      AddrPHI.addReg(InStackAddrReg).addMBB(InMBB);
    }
  }

  MachineBasicBlock::iterator InsertPos = MBB->getFirstNonPHI();
  Register StrideReg = MRI->createVirtualRegister(&X86::GR64_NOSPRegClass);
  BuildMI(*MBB, InsertPos, DebugLoc(), TII->get(X86::MOV64ri), StrideReg)
      .addImm(64);
  Register TileReg = PHI.getOperand(0).getReg();
  MachineInstr *NewMI = addDirectMem(
      BuildMI(*MBB, InsertPos, DebugLoc(), TII->get(X86::PTILELOADDV), TileReg)
          .addReg(RowReg)
          .addReg(ColReg),
      StackAddrReg);
  MachineOperand &MO = NewMI->getOperand(5);
  MO.setReg(StrideReg);
  MO.setIsKill(true);
  PHI.eraseFromParent();
  VisitedPHIs.erase(&PHI);
}

static bool isTileRegDef(MachineRegisterInfo *MRI, MachineInstr &MI) {
  MachineOperand &MO = MI.getOperand(0);
  if (MO.isReg() && MO.getReg().isVirtual() &&
      MRI->getRegClass(MO.getReg())->getID() == X86::TILERegClassID)
    return true;
  return false;
}

void X86FastPreTileConfig::canonicalizePHIs(MachineBasicBlock &MBB) {
  SmallVector<MachineInstr *, 8> PHIs;

  for (MachineInstr &MI : MBB) {
    if (!MI.isPHI())
      break;
    if (!isTileRegDef(MRI, MI))
      continue;
    PHIs.push_back(&MI);
  }
  // Canonicalize the phi node first. One tile phi may depeneds previous
  // phi node. For below case, we need convert %t4.
  //
  // BB0:
  // %t3 = phi (t1 BB1, t2 BB0)
  // %t4 = phi (t5 BB1, t3 BB0)
  // -->
  // %t3 = phi (t1 BB1, t2 BB0)
  // %t4 = phi (t5 BB1, t2 BB0)
  //
  while (!PHIs.empty()) {
    MachineInstr *PHI = PHIs.pop_back_val();

    // Find the operand that is incoming from the same MBB and the def
    // is also phi node.
    MachineOperand *InMO = nullptr;
    MachineInstr *DefMI = nullptr;
    for (unsigned I = 1, E = PHI->getNumOperands(); I != E; I += 2) {
      Register InTileReg = PHI->getOperand(I).getReg();
      MachineBasicBlock *InMBB = PHI->getOperand(I + 1).getMBB();
      DefMI = MRI->getVRegDef(InTileReg);
      if (InMBB != &MBB || !DefMI->isPHI())
        continue;

      InMO = &PHI->getOperand(I);
      break;
    }
    // If can't find such operand, do nothing.
    if (!InMO)
      continue;

    // Current phi node depends on previous phi node. Break the
    // dependency.
    Register DefTileReg;
    for (unsigned I = 1, E = DefMI->getNumOperands(); I != E; I += 2) {
      MachineBasicBlock *InMBB = PHI->getOperand(I + 1).getMBB();
      if (InMBB != &MBB)
        continue;
      DefTileReg = DefMI->getOperand(I).getReg();
      InMO->setReg(DefTileReg);
      break;
    }
  }
}

void X86FastPreTileConfig::convertPHIs(MachineBasicBlock &MBB) {
  SmallVector<MachineInstr *, 8> PHIs;
  for (MachineInstr &MI : MBB) {
    if (!MI.isPHI())
      break;
    if (!isTileRegDef(MRI, MI))
      continue;
    PHIs.push_back(&MI);
  }
  while (!PHIs.empty()) {
    MachineInstr *MI = PHIs.pop_back_val();
    VisitedPHIs.clear();
    convertPHI(&MBB, *MI);
  }
}

// PreTileConfig should configure the tile registers based on basic
// block.
bool X86FastPreTileConfig::configBasicBlock(MachineBasicBlock &MBB) {
  this->MBB = &MBB;
  bool Change = false;
  MachineInstr *LastShapeMI = nullptr;
  MachineInstr *LastTileCfg = nullptr;
  bool HasUnconfigTile = false;

  auto Config = [&](MachineInstr &Before) {
    if (CfgSS == -1)
      CfgSS = MFI->CreateStackObject(ST->getTileConfigSize(),
                                     ST->getTileConfigAlignment(), false);
    LastTileCfg = addFrameReference(
        BuildMI(MBB, Before, DebugLoc(), TII->get(X86::PLDTILECFGV)), CfgSS);
    LastShapeMI = nullptr;
    Change = true;
  };
  auto HasTileOperand = [](MachineRegisterInfo *MRI, MachineInstr &MI) {
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg())
        continue;
      Register Reg = MO.getReg();
      if (Reg.isVirtual() &&
          MRI->getRegClass(Reg)->getID() == X86::TILERegClassID)
        return true;
    }
    return false;
  };
  for (MachineInstr &MI : reverse(MBB)) {
    // We have transformed phi node before configuring BB.
    if (MI.isPHI())
      break;
    // Don't collect the shape of used tile, the tile should be defined
    // before the tile use. Spill and reload would happen if there is only
    // tile use after ldtilecfg, so the shape can be collected from reload.
    // Take below code for example. %t would be reloaded before tilestore
    // call
    // ....
    // tilestore %r, %c, %t
    // -->
    // call
    // ldtilecfg
    // %t = tileload %r, %c
    // tilestore %r, %c, %t
    if (HasTileOperand(MRI, MI))
      HasUnconfigTile = true;
    // According to AMX ABI, all the tile registers including config register
    // are volatile. Caller need to save/restore config register.
    if (MI.isCall() && HasUnconfigTile) {
      MachineBasicBlock::iterator I;
      if (LastShapeMI && dominates(MBB, MI, LastShapeMI))
        I = ++LastShapeMI->getIterator();
      else
        I = ++MI.getIterator();
      Config(*I);
      HasUnconfigTile = false;
      continue;
    }
    if (!isTileDef(MRI, MI))
      continue;
    //
    //---------------------------------------------------------------------
    // Don't handle COPY instruction. If the src and dst of the COPY can be
    // in the same config in below case, we just check the shape of t0.
    // def row0
    // def col0
    // ldtilecfg
    // t0 = tielzero(row0, col0)
    // t1 = copy t0
    // ...
    // If the src and dst of the COPY can NOT be in the same config in below
    // case. Reload would be generated befor the copy instruction.
    // def row0
    // def col0
    // t0 = tielzero(row0, col0)
    // spill t0
    // ...
    // def row1
    // def col1
    // ldtilecfg
    // t1 = tilezero(row1, col1)
    // reload t0
    // t1 = copy t0
    //---------------------------------------------------------------------
    //
    // If MI dominate the last shape def instruction, we need insert
    // ldtilecfg after LastShapeMI now. The config doesn't include
    // current MI.
    //   def row0
    //   def col0
    //   tilezero(row0, col0)  <- MI
    //   def row1
    //   def col1
    //   ldtilecfg             <- insert
    //   tilezero(row1, col1)
    if (LastShapeMI && dominates(MBB, MI, LastShapeMI))
      Config(*(++LastShapeMI->getIterator()));
    MachineOperand *RowMO = &MI.getOperand(1);
    MachineOperand *ColMO = &MI.getOperand(2);
    MachineInstr *RowMI = MRI->getVRegDef(RowMO->getReg());
    MachineInstr *ColMI = MRI->getVRegDef(ColMO->getReg());
    // If the shape is defined in current MBB, check the domination.
    // FIXME how about loop?
    if (RowMI->getParent() == &MBB) {
      if (!LastShapeMI)
        LastShapeMI = RowMI;
      else if (dominates(MBB, LastShapeMI, RowMI))
        LastShapeMI = RowMI;
    }
    if (ColMI->getParent() == &MBB) {
      if (!LastShapeMI)
        LastShapeMI = ColMI;
      else if (dominates(MBB, LastShapeMI, ColMI))
        LastShapeMI = ColMI;
    }
    // If there is user live out of the tilecfg, spill it and reload in
    // before the user.
    Register TileReg = MI.getOperand(0).getReg();
    if (mayLiveOut(TileReg, LastTileCfg))
      spill(++MI.getIterator(), TileReg, false);
    for (MachineInstr &UseMI : MRI->use_instructions(TileReg)) {
      if (UseMI.getParent() == &MBB) {
        // check user should not across ldtilecfg
        if (!LastTileCfg || !dominates(MBB, LastTileCfg, UseMI))
          continue;
        // reload befor UseMI
        reload(UseMI.getIterator(), TileReg, RowMO, ColMO);
      } else {
        // Don't reload for phi instruction, we handle phi reload separately.
        // TODO: merge the reload for the same user MBB.
        if (!UseMI.isPHI())
          reload(UseMI.getIterator(), TileReg, RowMO, ColMO);
      }
    }
  }

  // Configure tile registers at the head of the MBB
  if (HasUnconfigTile) {
    MachineInstr *Before;
    if (LastShapeMI == nullptr || LastShapeMI->isPHI())
      Before = &*MBB.getFirstNonPHI();
    else
      Before = &*(++LastShapeMI->getIterator());

    Config(*Before);
  }

  return Change;
}

bool X86FastPreTileConfig::runOnMachineFunction(MachineFunction &MFunc) {
  X86FI = MFunc.getInfo<X86MachineFunctionInfo>();
  // Early exit in the common case of non-AMX code.
  if (X86FI->getAMXProgModel() != AMXProgModelEnum::ManagedRA)
    return false;

  MF = &MFunc;
  MRI = &MFunc.getRegInfo();
  ST = &MFunc.getSubtarget<X86Subtarget>();
  TII = ST->getInstrInfo();
  MFI = &MFunc.getFrameInfo();
  TRI = ST->getRegisterInfo();
  CfgSS = -1;

  unsigned NumVirtRegs = MRI->getNumVirtRegs();

  StackSlotForVirtReg.resize(NumVirtRegs);
  MayLiveAcrossBlocks.clear();
  // We will create register during config. *3 is to make sure
  // the virtual register number doesn't exceed the size of
  // the bit vector.
  MayLiveAcrossBlocks.resize(NumVirtRegs * 3);
  bool Change = false;
  assert(MRI->isSSA());

  // Canonicalize the phi node first.
  for (MachineBasicBlock &MBB : MFunc)
    canonicalizePHIs(MBB);

  // Loop over all of the basic blocks in reverse post order and insert
  // ldtilecfg for tile registers. The reserse post order is to facilitate
  // PHI node convert.
  ReversePostOrderTraversal<MachineFunction *> RPOT(MF);
  for (MachineBasicBlock *MBB : RPOT) {
    convertPHIs(*MBB);
    Change |= configBasicBlock(*MBB);
  }

  if (Change)
    InitializeTileConfigStackSpace();

  StackSlotForVirtReg.clear();
  return Change;
}

FunctionPass *llvm::createX86FastPreTileConfigPass() {
  return new X86FastPreTileConfig();
}
