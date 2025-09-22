//===-- Mips16ISelDAGToDAG.cpp - A Dag to Dag Inst Selector for Mips16 ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Subclass of MipsDAGToDAGISel specialized for mips16.
//
//===----------------------------------------------------------------------===//

#include "Mips16ISelDAGToDAG.h"
#include "MCTargetDesc/MipsBaseInfo.h"
#include "Mips.h"
#include "MipsMachineFunction.h"
#include "MipsRegisterInfo.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "mips-isel"

bool Mips16DAGToDAGISel::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<MipsSubtarget>();
  if (!Subtarget->inMips16Mode())
    return false;
  return MipsDAGToDAGISel::runOnMachineFunction(MF);
}
/// Select multiply instructions.
std::pair<SDNode *, SDNode *>
Mips16DAGToDAGISel::selectMULT(SDNode *N, unsigned Opc, const SDLoc &DL, EVT Ty,
                               bool HasLo, bool HasHi) {
  SDNode *Lo = nullptr, *Hi = nullptr;
  SDNode *Mul = CurDAG->getMachineNode(Opc, DL, MVT::Glue, N->getOperand(0),
                                       N->getOperand(1));
  SDValue InGlue = SDValue(Mul, 0);

  if (HasLo) {
    unsigned Opcode = Mips::Mflo16;
    Lo = CurDAG->getMachineNode(Opcode, DL, Ty, MVT::Glue, InGlue);
    InGlue = SDValue(Lo, 1);
  }
  if (HasHi) {
    unsigned Opcode = Mips::Mfhi16;
    Hi = CurDAG->getMachineNode(Opcode, DL, Ty, InGlue);
  }
  return std::make_pair(Lo, Hi);
}

void Mips16DAGToDAGISel::initGlobalBaseReg(MachineFunction &MF) {
  MipsFunctionInfo *MipsFI = MF.getInfo<MipsFunctionInfo>();

  if (!MipsFI->globalBaseRegSet())
    return;

  MachineBasicBlock &MBB = MF.front();
  MachineBasicBlock::iterator I = MBB.begin();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  const TargetInstrInfo &TII = *Subtarget->getInstrInfo();
  DebugLoc DL;
  Register V0, V1, V2, GlobalBaseReg = MipsFI->getGlobalBaseReg(MF);
  const TargetRegisterClass *RC = &Mips::CPU16RegsRegClass;

  V0 = RegInfo.createVirtualRegister(RC);
  V1 = RegInfo.createVirtualRegister(RC);
  V2 = RegInfo.createVirtualRegister(RC);


  BuildMI(MBB, I, DL, TII.get(Mips::LiRxImmX16), V0)
      .addExternalSymbol("_gp_disp", MipsII::MO_ABS_HI);
  BuildMI(MBB, I, DL, TII.get(Mips::AddiuRxPcImmX16), V1)
      .addExternalSymbol("_gp_disp", MipsII::MO_ABS_LO);

  BuildMI(MBB, I, DL, TII.get(Mips::SllX16), V2).addReg(V0).addImm(16);
  BuildMI(MBB, I, DL, TII.get(Mips::AdduRxRyRz16), GlobalBaseReg)
      .addReg(V1)
      .addReg(V2);
}

void Mips16DAGToDAGISel::processFunctionAfterISel(MachineFunction &MF) {
  initGlobalBaseReg(MF);
}

bool Mips16DAGToDAGISel::selectAddr(bool SPAllowed, SDValue Addr, SDValue &Base,
                                    SDValue &Offset) {
  SDLoc DL(Addr);
  EVT ValTy = Addr.getValueType();

  // if Address is FI, get the TargetFrameIndex.
  if (SPAllowed) {
    if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
      Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), ValTy);
      Offset = CurDAG->getTargetConstant(0, DL, ValTy);
      return true;
    }
  }
  // on PIC code Load GA
  if (Addr.getOpcode() == MipsISD::Wrapper) {
    Base = Addr.getOperand(0);
    Offset = Addr.getOperand(1);
    return true;
  }
  if (!TM.isPositionIndependent()) {
    if ((Addr.getOpcode() == ISD::TargetExternalSymbol ||
         Addr.getOpcode() == ISD::TargetGlobalAddress))
      return false;
  }
  // Addresses of the form FI+const or FI|const
  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    auto *CN = cast<ConstantSDNode>(Addr.getOperand(1));
    if (isInt<16>(CN->getSExtValue())) {
      // If the first operand is a FI, get the TargetFI Node
      if (SPAllowed) {
        if (FrameIndexSDNode *FIN =
                dyn_cast<FrameIndexSDNode>(Addr.getOperand(0))) {
          Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), ValTy);
          Offset = CurDAG->getTargetConstant(CN->getZExtValue(), DL, ValTy);
          return true;
        }
      }

      Base = Addr.getOperand(0);
      Offset = CurDAG->getTargetConstant(CN->getZExtValue(), DL, ValTy);
      return true;
    }
  }
  // Operand is a result from an ADD.
  if (Addr.getOpcode() == ISD::ADD) {
    // When loading from constant pools, load the lower address part in
    // the instruction itself. Example, instead of:
    //  lui $2, %hi($CPI1_0)
    //  addiu $2, $2, %lo($CPI1_0)
    //  lwc1 $f0, 0($2)
    // Generate:
    //  lui $2, %hi($CPI1_0)
    //  lwc1 $f0, %lo($CPI1_0)($2)
    if (Addr.getOperand(1).getOpcode() == MipsISD::Lo ||
        Addr.getOperand(1).getOpcode() == MipsISD::GPRel) {
      SDValue Opnd0 = Addr.getOperand(1).getOperand(0);
      if (isa<ConstantPoolSDNode>(Opnd0) || isa<GlobalAddressSDNode>(Opnd0) ||
          isa<JumpTableSDNode>(Opnd0)) {
        Base = Addr.getOperand(0);
        Offset = Opnd0;
        return true;
      }
    }
  }
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, DL, ValTy);
  return true;
}

bool Mips16DAGToDAGISel::selectAddr16(SDValue Addr, SDValue &Base,
                                      SDValue &Offset) {
  return selectAddr(false, Addr, Base, Offset);
}

bool Mips16DAGToDAGISel::selectAddr16SP(SDValue Addr, SDValue &Base,
                                        SDValue &Offset) {
  return selectAddr(true, Addr, Base, Offset);
}

/// Select instructions not customized! Used for
/// expanded, promoted and normal instructions
bool Mips16DAGToDAGISel::trySelect(SDNode *Node) {
  unsigned Opcode = Node->getOpcode();
  SDLoc DL(Node);

  ///
  // Instruction Selection not handled by the auto-generated
  // tablegen selection should be handled here.
  ///
  EVT NodeTy = Node->getValueType(0);
  unsigned MultOpc;

  switch (Opcode) {
  default:
    break;

  /// Mul with two results
  case ISD::SMUL_LOHI:
  case ISD::UMUL_LOHI: {
    MultOpc = (Opcode == ISD::UMUL_LOHI ? Mips::MultuRxRy16 : Mips::MultRxRy16);
    std::pair<SDNode *, SDNode *> LoHi =
        selectMULT(Node, MultOpc, DL, NodeTy, true, true);
    if (!SDValue(Node, 0).use_empty())
      ReplaceUses(SDValue(Node, 0), SDValue(LoHi.first, 0));

    if (!SDValue(Node, 1).use_empty())
      ReplaceUses(SDValue(Node, 1), SDValue(LoHi.second, 0));

    CurDAG->RemoveDeadNode(Node);
    return true;
  }

  case ISD::MULHS:
  case ISD::MULHU: {
    MultOpc = (Opcode == ISD::MULHU ? Mips::MultuRxRy16 : Mips::MultRxRy16);
    auto LoHi = selectMULT(Node, MultOpc, DL, NodeTy, false, true);
    ReplaceNode(Node, LoHi.second);
    return true;
  }
  }

  return false;
}

Mips16DAGToDAGISelLegacy::Mips16DAGToDAGISelLegacy(MipsTargetMachine &TM,
                                                   CodeGenOptLevel OL)
    : MipsDAGToDAGISelLegacy(std::make_unique<Mips16DAGToDAGISel>(TM, OL)) {}

FunctionPass *llvm::createMips16ISelDag(MipsTargetMachine &TM,
                                        CodeGenOptLevel OptLevel) {
  return new Mips16DAGToDAGISelLegacy(TM, OptLevel);
}
