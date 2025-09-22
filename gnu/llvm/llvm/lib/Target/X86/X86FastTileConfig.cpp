//===-- X86FastTileConfig.cpp - Fast Tile Register Configure---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Pass to config the shape of AMX physical registers
/// AMX register need to be configured before use. Before FastRegAllocation pass
/// the ldtilecfg instruction is inserted, however at that time we don't
/// know the shape of each physical tile registers, because the register
/// allocation is not done yet. This pass runs after register allocation
/// pass. It collects the shape information of each physical tile register
/// and store the shape in the stack slot that is allocated for load config
/// to tile config register.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86MachineFunctionInfo.h"
#include "X86RegisterInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "fasttileconfig"

namespace {

class X86FastTileConfig : public MachineFunctionPass {
  // context
  MachineFunction *MF = nullptr;
  const TargetInstrInfo *TII = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  X86MachineFunctionInfo *X86FI = nullptr;

  bool configBasicBlock(MachineBasicBlock &MBB);

public:
  X86FastTileConfig() : MachineFunctionPass(ID) {}

  /// Return the pass name.
  StringRef getPassName() const override {
    return "Fast Tile Register Configure";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  /// Perform register allocation.
  bool runOnMachineFunction(MachineFunction &MFunc) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoPHIs);
  }

  static char ID;
};

} // end anonymous namespace

char X86FastTileConfig::ID = 0;

INITIALIZE_PASS_BEGIN(X86FastTileConfig, DEBUG_TYPE,
                      "Fast Tile Register Configure", false, false)
INITIALIZE_PASS_END(X86FastTileConfig, DEBUG_TYPE,
                    "Fast Tile Register Configure", false, false)

static bool isTileDef(MachineRegisterInfo *MRI, MachineInstr &MI) {
  // There is no phi instruction after register allocation.
  assert(MI.isPHI() == false);
  // The instruction must have 3 operands: tile def, row, col.
  // It should be AMX pseudo instruction that have shape operand.
  if (MI.isDebugInstr() || MI.isCopy() || MI.getNumOperands() < 3 ||
      !MI.isPseudo())
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

// PreTileConfig should configure the tile registers based on basic
// block.
bool X86FastTileConfig::configBasicBlock(MachineBasicBlock &MBB) {
  bool Change = false;
  SmallVector<std::pair<unsigned, ShapeT>, 6> ShapeInfos;
  for (MachineInstr &MI : reverse(MBB)) {
    if (!isTileDef(MRI, MI) && MI.getOpcode() != X86::PLDTILECFGV)
      continue;
    // AMX instructions that define tile register.
    if (MI.getOpcode() != X86::PLDTILECFGV) {
      MachineOperand &Row = MI.getOperand(1);
      MachineOperand &Col = MI.getOperand(2);
      unsigned TMMIdx = MI.getOperand(0).getReg() - X86::TMM0;
      ShapeInfos.push_back({TMMIdx, ShapeT(&Row, &Col)});
    } else { // PLDTILECFGV
      // Rewrite the shape information to memory. Stack slot should have
      // been initialized to zero in pre config.
      int SS = MI.getOperand(0).getIndex(); // tile config stack slot.
      for (auto &ShapeInfo : ShapeInfos) {
        DebugLoc DL;
        unsigned TMMIdx = ShapeInfo.first;
        Register RowReg = ShapeInfo.second.getRow()->getReg();
        Register ColReg = ShapeInfo.second.getCol()->getReg();
        // Here is the data format for the tile config.
        // 0      palette
        // 1      start_row
        // 2-15   reserved, must be zero
        // 16-17  tile0.colsb Tile 0 bytes per row.
        // 18-19  tile1.colsb Tile 1 bytes per row.
        // 20-21  tile2.colsb Tile 2 bytes per row.
        // ... (sequence continues)
        // 30-31  tile7.colsb Tile 7 bytes per row.
        // 32-47  reserved, must be zero
        // 48     tile0.rows Tile 0 rows.
        // 49     tile1.rows Tile 1 rows.
        // 50     tile2.rows Tile 2 rows.
        // ... (sequence continues)
        // 55     tile7.rows Tile 7 rows.
        // 56-63  reserved, must be zero
        int RowOffset = 48 + TMMIdx;
        int ColOffset = 16 + TMMIdx * 2;

        Register SubRowReg = TRI->getSubReg(RowReg, X86::sub_8bit);
        BuildMI(MBB, MI, DL, TII->get(X86::IMPLICIT_DEF), SubRowReg);
        MachineInstrBuilder StoreRow =
            BuildMI(MBB, MI, DL, TII->get(X86::MOV8mr));
        addFrameReference(StoreRow, SS, RowOffset).addReg(SubRowReg);

        MachineInstrBuilder StoreCol =
            BuildMI(MBB, MI, DL, TII->get(X86::MOV16mr));
        addFrameReference(StoreCol, SS, ColOffset).addReg(ColReg);
      }
      ShapeInfos.clear();
      Change = true;
    }
  }

  return Change;
}

bool X86FastTileConfig::runOnMachineFunction(MachineFunction &MFunc) {
  X86FI = MFunc.getInfo<X86MachineFunctionInfo>();
  // Early exit in the common case of non-AMX code.
  if (X86FI->getAMXProgModel() != AMXProgModelEnum::ManagedRA)
    return false;

  MF = &MFunc;
  MRI = &MFunc.getRegInfo();
  const TargetSubtargetInfo *ST = &MFunc.getSubtarget<X86Subtarget>();
  TRI = ST->getRegisterInfo();
  TII = MFunc.getSubtarget().getInstrInfo();
  bool Change = false;

  // Loop over all of the basic blocks, eliminating virtual register references
  for (MachineBasicBlock &MBB : MFunc)
    Change |= configBasicBlock(MBB);

  return Change;
}

FunctionPass *llvm::createX86FastTileConfigPass() {
  return new X86FastTileConfig();
}
