//==--- InstrEmitter.cpp - Emit MachineInstrs for the SelectionDAG class ---==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the Emit routines for the SelectionDAG class, which creates
// MachineInstrs based on the decisions of the SelectionDAG instruction
// selection.
//
//===----------------------------------------------------------------------===//

#include "InstrEmitter.h"
#include "SDNodeDbgValue.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/PseudoProbe.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "instr-emitter"

/// MinRCSize - Smallest register class we allow when constraining virtual
/// registers.  If satisfying all register class constraints would require
/// using a smaller register class, emit a COPY to a new virtual register
/// instead.
const unsigned MinRCSize = 4;

/// CountResults - The results of target nodes have register or immediate
/// operands first, then an optional chain, and optional glue operands (which do
/// not go into the resulting MachineInstr).
unsigned InstrEmitter::CountResults(SDNode *Node) {
  unsigned N = Node->getNumValues();
  while (N && Node->getValueType(N - 1) == MVT::Glue)
    --N;
  if (N && Node->getValueType(N - 1) == MVT::Other)
    --N;    // Skip over chain result.
  return N;
}

/// countOperands - The inputs to target nodes have any actual inputs first,
/// followed by an optional chain operand, then an optional glue operand.
/// Compute the number of actual operands that will go into the resulting
/// MachineInstr.
///
/// Also count physreg RegisterSDNode and RegisterMaskSDNode operands preceding
/// the chain and glue. These operands may be implicit on the machine instr.
static unsigned countOperands(SDNode *Node, unsigned NumExpUses,
                              unsigned &NumImpUses) {
  unsigned N = Node->getNumOperands();
  while (N && Node->getOperand(N - 1).getValueType() == MVT::Glue)
    --N;
  if (N && Node->getOperand(N - 1).getValueType() == MVT::Other)
    --N; // Ignore chain if it exists.

  // Count RegisterSDNode and RegisterMaskSDNode operands for NumImpUses.
  NumImpUses = N - NumExpUses;
  for (unsigned I = N; I > NumExpUses; --I) {
    if (isa<RegisterMaskSDNode>(Node->getOperand(I - 1)))
      continue;
    if (RegisterSDNode *RN = dyn_cast<RegisterSDNode>(Node->getOperand(I - 1)))
      if (RN->getReg().isPhysical())
        continue;
    NumImpUses = N - I;
    break;
  }

  return N;
}

/// EmitCopyFromReg - Generate machine code for an CopyFromReg node or an
/// implicit physical register output.
void InstrEmitter::EmitCopyFromReg(SDNode *Node, unsigned ResNo, bool IsClone,
                                   Register SrcReg,
                                   DenseMap<SDValue, Register> &VRBaseMap) {
  Register VRBase;
  if (SrcReg.isVirtual()) {
    // Just use the input register directly!
    SDValue Op(Node, ResNo);
    if (IsClone)
      VRBaseMap.erase(Op);
    bool isNew = VRBaseMap.insert(std::make_pair(Op, SrcReg)).second;
    (void)isNew; // Silence compiler warning.
    assert(isNew && "Node emitted out of order - early");
    return;
  }

  // If the node is only used by a CopyToReg and the dest reg is a vreg, use
  // the CopyToReg'd destination register instead of creating a new vreg.
  bool MatchReg = true;
  const TargetRegisterClass *UseRC = nullptr;
  MVT VT = Node->getSimpleValueType(ResNo);

  // Stick to the preferred register classes for legal types.
  if (TLI->isTypeLegal(VT))
    UseRC = TLI->getRegClassFor(VT, Node->isDivergent());

  for (SDNode *User : Node->uses()) {
    bool Match = true;
    if (User->getOpcode() == ISD::CopyToReg &&
        User->getOperand(2).getNode() == Node &&
        User->getOperand(2).getResNo() == ResNo) {
      Register DestReg = cast<RegisterSDNode>(User->getOperand(1))->getReg();
      if (DestReg.isVirtual()) {
        VRBase = DestReg;
        Match = false;
      } else if (DestReg != SrcReg)
        Match = false;
    } else {
      for (unsigned i = 0, e = User->getNumOperands(); i != e; ++i) {
        SDValue Op = User->getOperand(i);
        if (Op.getNode() != Node || Op.getResNo() != ResNo)
          continue;
        MVT VT = Node->getSimpleValueType(Op.getResNo());
        if (VT == MVT::Other || VT == MVT::Glue)
          continue;
        Match = false;
        if (User->isMachineOpcode()) {
          const MCInstrDesc &II = TII->get(User->getMachineOpcode());
          const TargetRegisterClass *RC = nullptr;
          if (i + II.getNumDefs() < II.getNumOperands()) {
            RC = TRI->getAllocatableClass(
                TII->getRegClass(II, i + II.getNumDefs(), TRI, *MF));
          }
          if (!UseRC)
            UseRC = RC;
          else if (RC) {
            const TargetRegisterClass *ComRC =
                TRI->getCommonSubClass(UseRC, RC);
            // If multiple uses expect disjoint register classes, we emit
            // copies in AddRegisterOperand.
            if (ComRC)
              UseRC = ComRC;
          }
        }
      }
    }
    MatchReg &= Match;
    if (VRBase)
      break;
  }

  const TargetRegisterClass *SrcRC = nullptr, *DstRC = nullptr;
  SrcRC = TRI->getMinimalPhysRegClass(SrcReg, VT);

  // Figure out the register class to create for the destreg.
  if (VRBase) {
    DstRC = MRI->getRegClass(VRBase);
  } else if (UseRC) {
    assert(TRI->isTypeLegalForClass(*UseRC, VT) &&
           "Incompatible phys register def and uses!");
    DstRC = UseRC;
  } else
    DstRC = SrcRC;

  // If all uses are reading from the src physical register and copying the
  // register is either impossible or very expensive, then don't create a copy.
  if (MatchReg && SrcRC->getCopyCost() < 0) {
    VRBase = SrcReg;
  } else {
    // Create the reg, emit the copy.
    VRBase = MRI->createVirtualRegister(DstRC);
    BuildMI(*MBB, InsertPos, Node->getDebugLoc(), TII->get(TargetOpcode::COPY),
            VRBase).addReg(SrcReg);
  }

  SDValue Op(Node, ResNo);
  if (IsClone)
    VRBaseMap.erase(Op);
  bool isNew = VRBaseMap.insert(std::make_pair(Op, VRBase)).second;
  (void)isNew; // Silence compiler warning.
  assert(isNew && "Node emitted out of order - early");
}

void InstrEmitter::CreateVirtualRegisters(SDNode *Node,
                                       MachineInstrBuilder &MIB,
                                       const MCInstrDesc &II,
                                       bool IsClone, bool IsCloned,
                                       DenseMap<SDValue, Register> &VRBaseMap) {
  assert(Node->getMachineOpcode() != TargetOpcode::IMPLICIT_DEF &&
         "IMPLICIT_DEF should have been handled as a special case elsewhere!");

  unsigned NumResults = CountResults(Node);
  bool HasVRegVariadicDefs = !MF->getTarget().usesPhysRegsForValues() &&
                             II.isVariadic() && II.variadicOpsAreDefs();
  unsigned NumVRegs = HasVRegVariadicDefs ? NumResults : II.getNumDefs();
  if (Node->getMachineOpcode() == TargetOpcode::STATEPOINT)
    NumVRegs = NumResults;
  for (unsigned i = 0; i < NumVRegs; ++i) {
    // If the specific node value is only used by a CopyToReg and the dest reg
    // is a vreg in the same register class, use the CopyToReg'd destination
    // register instead of creating a new vreg.
    Register VRBase;
    const TargetRegisterClass *RC =
      TRI->getAllocatableClass(TII->getRegClass(II, i, TRI, *MF));
    // Always let the value type influence the used register class. The
    // constraints on the instruction may be too lax to represent the value
    // type correctly. For example, a 64-bit float (X86::FR64) can't live in
    // the 32-bit float super-class (X86::FR32).
    if (i < NumResults && TLI->isTypeLegal(Node->getSimpleValueType(i))) {
      const TargetRegisterClass *VTRC = TLI->getRegClassFor(
          Node->getSimpleValueType(i),
          (Node->isDivergent() || (RC && TRI->isDivergentRegClass(RC))));
      if (RC)
        VTRC = TRI->getCommonSubClass(RC, VTRC);
      if (VTRC)
        RC = VTRC;
    }

    if (!II.operands().empty() && II.operands()[i].isOptionalDef()) {
      // Optional def must be a physical register.
      VRBase = cast<RegisterSDNode>(Node->getOperand(i-NumResults))->getReg();
      assert(VRBase.isPhysical());
      MIB.addReg(VRBase, RegState::Define);
    }

    if (!VRBase && !IsClone && !IsCloned)
      for (SDNode *User : Node->uses()) {
        if (User->getOpcode() == ISD::CopyToReg &&
            User->getOperand(2).getNode() == Node &&
            User->getOperand(2).getResNo() == i) {
          Register Reg = cast<RegisterSDNode>(User->getOperand(1))->getReg();
          if (Reg.isVirtual()) {
            const TargetRegisterClass *RegRC = MRI->getRegClass(Reg);
            if (RegRC == RC) {
              VRBase = Reg;
              MIB.addReg(VRBase, RegState::Define);
              break;
            }
          }
        }
      }

    // Create the result registers for this node and add the result regs to
    // the machine instruction.
    if (VRBase == 0) {
      assert(RC && "Isn't a register operand!");
      VRBase = MRI->createVirtualRegister(RC);
      MIB.addReg(VRBase, RegState::Define);
    }

    // If this def corresponds to a result of the SDNode insert the VRBase into
    // the lookup map.
    if (i < NumResults) {
      SDValue Op(Node, i);
      if (IsClone)
        VRBaseMap.erase(Op);
      bool isNew = VRBaseMap.insert(std::make_pair(Op, VRBase)).second;
      (void)isNew; // Silence compiler warning.
      assert(isNew && "Node emitted out of order - early");
    }
  }
}

/// getVR - Return the virtual register corresponding to the specified result
/// of the specified node.
Register InstrEmitter::getVR(SDValue Op,
                             DenseMap<SDValue, Register> &VRBaseMap) {
  if (Op.isMachineOpcode() &&
      Op.getMachineOpcode() == TargetOpcode::IMPLICIT_DEF) {
    // Add an IMPLICIT_DEF instruction before every use.
    // IMPLICIT_DEF can produce any type of result so its MCInstrDesc
    // does not include operand register class info.
    const TargetRegisterClass *RC = TLI->getRegClassFor(
        Op.getSimpleValueType(), Op.getNode()->isDivergent());
    Register VReg = MRI->createVirtualRegister(RC);
    BuildMI(*MBB, InsertPos, Op.getDebugLoc(),
            TII->get(TargetOpcode::IMPLICIT_DEF), VReg);
    return VReg;
  }

  DenseMap<SDValue, Register>::iterator I = VRBaseMap.find(Op);
  assert(I != VRBaseMap.end() && "Node emitted out of order - late");
  return I->second;
}

static bool isConvergenceCtrlMachineOp(SDValue Op) {
  if (Op->isMachineOpcode()) {
    switch (Op->getMachineOpcode()) {
    case TargetOpcode::CONVERGENCECTRL_ANCHOR:
    case TargetOpcode::CONVERGENCECTRL_ENTRY:
    case TargetOpcode::CONVERGENCECTRL_LOOP:
    case TargetOpcode::CONVERGENCECTRL_GLUE:
      return true;
    }
    return false;
  }

  // We can reach here when CopyFromReg is encountered. But rather than making a
  // special case for that, we just make sure we don't reach here in some
  // surprising way.
  switch (Op->getOpcode()) {
  case ISD::CONVERGENCECTRL_ANCHOR:
  case ISD::CONVERGENCECTRL_ENTRY:
  case ISD::CONVERGENCECTRL_LOOP:
  case ISD::CONVERGENCECTRL_GLUE:
    llvm_unreachable("Convergence control should have been selected by now.");
  }
  return false;
}

/// AddRegisterOperand - Add the specified register as an operand to the
/// specified machine instr. Insert register copies if the register is
/// not in the required register class.
void
InstrEmitter::AddRegisterOperand(MachineInstrBuilder &MIB,
                                 SDValue Op,
                                 unsigned IIOpNum,
                                 const MCInstrDesc *II,
                                 DenseMap<SDValue, Register> &VRBaseMap,
                                 bool IsDebug, bool IsClone, bool IsCloned) {
  assert(Op.getValueType() != MVT::Other &&
         Op.getValueType() != MVT::Glue &&
         "Chain and glue operands should occur at end of operand list!");
  // Get/emit the operand.
  Register VReg = getVR(Op, VRBaseMap);

  const MCInstrDesc &MCID = MIB->getDesc();
  bool isOptDef = IIOpNum < MCID.getNumOperands() &&
                  MCID.operands()[IIOpNum].isOptionalDef();

  // If the instruction requires a register in a different class, create
  // a new virtual register and copy the value into it, but first attempt to
  // shrink VReg's register class within reason.  For example, if VReg == GR32
  // and II requires a GR32_NOSP, just constrain VReg to GR32_NOSP.
  if (II) {
    const TargetRegisterClass *OpRC = nullptr;
    if (IIOpNum < II->getNumOperands())
      OpRC = TII->getRegClass(*II, IIOpNum, TRI, *MF);

    if (OpRC) {
      unsigned MinNumRegs = MinRCSize;
      // Don't apply any RC size limit for IMPLICIT_DEF. Each use has a unique
      // virtual register.
      if (Op.isMachineOpcode() &&
          Op.getMachineOpcode() == TargetOpcode::IMPLICIT_DEF)
        MinNumRegs = 0;

      const TargetRegisterClass *ConstrainedRC
        = MRI->constrainRegClass(VReg, OpRC, MinNumRegs);
      if (!ConstrainedRC) {
        OpRC = TRI->getAllocatableClass(OpRC);
        assert(OpRC && "Constraints cannot be fulfilled for allocation");
        Register NewVReg = MRI->createVirtualRegister(OpRC);
        BuildMI(*MBB, InsertPos, Op.getNode()->getDebugLoc(),
                TII->get(TargetOpcode::COPY), NewVReg).addReg(VReg);
        VReg = NewVReg;
      } else {
        assert(ConstrainedRC->isAllocatable() &&
           "Constraining an allocatable VReg produced an unallocatable class?");
      }
    }
  }

  // If this value has only one use, that use is a kill. This is a
  // conservative approximation. InstrEmitter does trivial coalescing
  // with CopyFromReg nodes, so don't emit kill flags for them.
  // Avoid kill flags on Schedule cloned nodes, since there will be
  // multiple uses.
  // Tied operands are never killed, so we need to check that. And that
  // means we need to determine the index of the operand.
  // Don't kill convergence control tokens. Initially they are only used in glue
  // nodes, and the InstrEmitter later adds implicit uses on the users of the
  // glue node. This can sometimes make it seem like there is only one use,
  // which is the glue node itself.
  bool isKill = Op.hasOneUse() && !isConvergenceCtrlMachineOp(Op) &&
                Op.getNode()->getOpcode() != ISD::CopyFromReg && !IsDebug &&
                !(IsClone || IsCloned);
  if (isKill) {
    unsigned Idx = MIB->getNumOperands();
    while (Idx > 0 &&
           MIB->getOperand(Idx-1).isReg() &&
           MIB->getOperand(Idx-1).isImplicit())
      --Idx;
    bool isTied = MCID.getOperandConstraint(Idx, MCOI::TIED_TO) != -1;
    if (isTied)
      isKill = false;
  }

  MIB.addReg(VReg, getDefRegState(isOptDef) | getKillRegState(isKill) |
             getDebugRegState(IsDebug));
}

/// AddOperand - Add the specified operand to the specified machine instr.  II
/// specifies the instruction information for the node, and IIOpNum is the
/// operand number (in the II) that we are adding.
void InstrEmitter::AddOperand(MachineInstrBuilder &MIB,
                              SDValue Op,
                              unsigned IIOpNum,
                              const MCInstrDesc *II,
                              DenseMap<SDValue, Register> &VRBaseMap,
                              bool IsDebug, bool IsClone, bool IsCloned) {
  if (Op.isMachineOpcode()) {
    AddRegisterOperand(MIB, Op, IIOpNum, II, VRBaseMap,
                       IsDebug, IsClone, IsCloned);
  } else if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op)) {
    MIB.addImm(C->getSExtValue());
  } else if (ConstantFPSDNode *F = dyn_cast<ConstantFPSDNode>(Op)) {
    MIB.addFPImm(F->getConstantFPValue());
  } else if (RegisterSDNode *R = dyn_cast<RegisterSDNode>(Op)) {
    Register VReg = R->getReg();
    MVT OpVT = Op.getSimpleValueType();
    const TargetRegisterClass *IIRC =
        II ? TRI->getAllocatableClass(TII->getRegClass(*II, IIOpNum, TRI, *MF))
           : nullptr;
    const TargetRegisterClass *OpRC =
        TLI->isTypeLegal(OpVT)
            ? TLI->getRegClassFor(OpVT,
                                  Op.getNode()->isDivergent() ||
                                      (IIRC && TRI->isDivergentRegClass(IIRC)))
            : nullptr;

    if (OpRC && IIRC && OpRC != IIRC && VReg.isVirtual()) {
      Register NewVReg = MRI->createVirtualRegister(IIRC);
      BuildMI(*MBB, InsertPos, Op.getNode()->getDebugLoc(),
               TII->get(TargetOpcode::COPY), NewVReg).addReg(VReg);
      VReg = NewVReg;
    }
    // Turn additional physreg operands into implicit uses on non-variadic
    // instructions. This is used by call and return instructions passing
    // arguments in registers.
    bool Imp = II && (IIOpNum >= II->getNumOperands() && !II->isVariadic());
    MIB.addReg(VReg, getImplRegState(Imp));
  } else if (RegisterMaskSDNode *RM = dyn_cast<RegisterMaskSDNode>(Op)) {
    MIB.addRegMask(RM->getRegMask());
  } else if (GlobalAddressSDNode *TGA = dyn_cast<GlobalAddressSDNode>(Op)) {
    MIB.addGlobalAddress(TGA->getGlobal(), TGA->getOffset(),
                         TGA->getTargetFlags());
  } else if (BasicBlockSDNode *BBNode = dyn_cast<BasicBlockSDNode>(Op)) {
    MIB.addMBB(BBNode->getBasicBlock());
  } else if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(Op)) {
    MIB.addFrameIndex(FI->getIndex());
  } else if (JumpTableSDNode *JT = dyn_cast<JumpTableSDNode>(Op)) {
    MIB.addJumpTableIndex(JT->getIndex(), JT->getTargetFlags());
  } else if (ConstantPoolSDNode *CP = dyn_cast<ConstantPoolSDNode>(Op)) {
    int Offset = CP->getOffset();
    Align Alignment = CP->getAlign();

    unsigned Idx;
    MachineConstantPool *MCP = MF->getConstantPool();
    if (CP->isMachineConstantPoolEntry())
      Idx = MCP->getConstantPoolIndex(CP->getMachineCPVal(), Alignment);
    else
      Idx = MCP->getConstantPoolIndex(CP->getConstVal(), Alignment);
    MIB.addConstantPoolIndex(Idx, Offset, CP->getTargetFlags());
  } else if (ExternalSymbolSDNode *ES = dyn_cast<ExternalSymbolSDNode>(Op)) {
    MIB.addExternalSymbol(ES->getSymbol(), ES->getTargetFlags());
  } else if (auto *SymNode = dyn_cast<MCSymbolSDNode>(Op)) {
    MIB.addSym(SymNode->getMCSymbol());
  } else if (BlockAddressSDNode *BA = dyn_cast<BlockAddressSDNode>(Op)) {
    MIB.addBlockAddress(BA->getBlockAddress(),
                        BA->getOffset(),
                        BA->getTargetFlags());
  } else if (TargetIndexSDNode *TI = dyn_cast<TargetIndexSDNode>(Op)) {
    MIB.addTargetIndex(TI->getIndex(), TI->getOffset(), TI->getTargetFlags());
  } else {
    assert(Op.getValueType() != MVT::Other &&
           Op.getValueType() != MVT::Glue &&
           "Chain and glue operands should occur at end of operand list!");
    AddRegisterOperand(MIB, Op, IIOpNum, II, VRBaseMap,
                       IsDebug, IsClone, IsCloned);
  }
}

Register InstrEmitter::ConstrainForSubReg(Register VReg, unsigned SubIdx,
                                          MVT VT, bool isDivergent, const DebugLoc &DL) {
  const TargetRegisterClass *VRC = MRI->getRegClass(VReg);
  const TargetRegisterClass *RC = TRI->getSubClassWithSubReg(VRC, SubIdx);

  // RC is a sub-class of VRC that supports SubIdx.  Try to constrain VReg
  // within reason.
  if (RC && RC != VRC)
    RC = MRI->constrainRegClass(VReg, RC, MinRCSize);

  // VReg has been adjusted.  It can be used with SubIdx operands now.
  if (RC)
    return VReg;

  // VReg couldn't be reasonably constrained.  Emit a COPY to a new virtual
  // register instead.
  RC = TRI->getSubClassWithSubReg(TLI->getRegClassFor(VT, isDivergent), SubIdx);
  assert(RC && "No legal register class for VT supports that SubIdx");
  Register NewReg = MRI->createVirtualRegister(RC);
  BuildMI(*MBB, InsertPos, DL, TII->get(TargetOpcode::COPY), NewReg)
    .addReg(VReg);
  return NewReg;
}

/// EmitSubregNode - Generate machine code for subreg nodes.
///
void InstrEmitter::EmitSubregNode(SDNode *Node,
                                  DenseMap<SDValue, Register> &VRBaseMap,
                                  bool IsClone, bool IsCloned) {
  Register VRBase;
  unsigned Opc = Node->getMachineOpcode();

  // If the node is only used by a CopyToReg and the dest reg is a vreg, use
  // the CopyToReg'd destination register instead of creating a new vreg.
  for (SDNode *User : Node->uses()) {
    if (User->getOpcode() == ISD::CopyToReg &&
        User->getOperand(2).getNode() == Node) {
      Register DestReg = cast<RegisterSDNode>(User->getOperand(1))->getReg();
      if (DestReg.isVirtual()) {
        VRBase = DestReg;
        break;
      }
    }
  }

  if (Opc == TargetOpcode::EXTRACT_SUBREG) {
    // EXTRACT_SUBREG is lowered as %dst = COPY %src:sub.  There are no
    // constraints on the %dst register, COPY can target all legal register
    // classes.
    unsigned SubIdx = Node->getConstantOperandVal(1);
    const TargetRegisterClass *TRC =
      TLI->getRegClassFor(Node->getSimpleValueType(0), Node->isDivergent());

    Register Reg;
    MachineInstr *DefMI;
    RegisterSDNode *R = dyn_cast<RegisterSDNode>(Node->getOperand(0));
    if (R && R->getReg().isPhysical()) {
      Reg = R->getReg();
      DefMI = nullptr;
    } else {
      Reg = R ? R->getReg() : getVR(Node->getOperand(0), VRBaseMap);
      DefMI = MRI->getVRegDef(Reg);
    }

    Register SrcReg, DstReg;
    unsigned DefSubIdx;
    if (DefMI &&
        TII->isCoalescableExtInstr(*DefMI, SrcReg, DstReg, DefSubIdx) &&
        SubIdx == DefSubIdx &&
        TRC == MRI->getRegClass(SrcReg)) {
      // Optimize these:
      // r1025 = s/zext r1024, 4
      // r1026 = extract_subreg r1025, 4
      // to a copy
      // r1026 = copy r1024
      VRBase = MRI->createVirtualRegister(TRC);
      BuildMI(*MBB, InsertPos, Node->getDebugLoc(),
              TII->get(TargetOpcode::COPY), VRBase).addReg(SrcReg);
      MRI->clearKillFlags(SrcReg);
    } else {
      // Reg may not support a SubIdx sub-register, and we may need to
      // constrain its register class or issue a COPY to a compatible register
      // class.
      if (Reg.isVirtual())
        Reg = ConstrainForSubReg(Reg, SubIdx,
                                 Node->getOperand(0).getSimpleValueType(),
                                 Node->isDivergent(), Node->getDebugLoc());
      // Create the destreg if it is missing.
      if (!VRBase)
        VRBase = MRI->createVirtualRegister(TRC);

      // Create the extract_subreg machine instruction.
      MachineInstrBuilder CopyMI =
          BuildMI(*MBB, InsertPos, Node->getDebugLoc(),
                  TII->get(TargetOpcode::COPY), VRBase);
      if (Reg.isVirtual())
        CopyMI.addReg(Reg, 0, SubIdx);
      else
        CopyMI.addReg(TRI->getSubReg(Reg, SubIdx));
    }
  } else if (Opc == TargetOpcode::INSERT_SUBREG ||
             Opc == TargetOpcode::SUBREG_TO_REG) {
    SDValue N0 = Node->getOperand(0);
    SDValue N1 = Node->getOperand(1);
    SDValue N2 = Node->getOperand(2);
    unsigned SubIdx = N2->getAsZExtVal();

    // Figure out the register class to create for the destreg.  It should be
    // the largest legal register class supporting SubIdx sub-registers.
    // RegisterCoalescer will constrain it further if it decides to eliminate
    // the INSERT_SUBREG instruction.
    //
    //   %dst = INSERT_SUBREG %src, %sub, SubIdx
    //
    // is lowered by TwoAddressInstructionPass to:
    //
    //   %dst = COPY %src
    //   %dst:SubIdx = COPY %sub
    //
    // There is no constraint on the %src register class.
    //
    const TargetRegisterClass *SRC =
        TLI->getRegClassFor(Node->getSimpleValueType(0), Node->isDivergent());
    SRC = TRI->getSubClassWithSubReg(SRC, SubIdx);
    assert(SRC && "No register class supports VT and SubIdx for INSERT_SUBREG");

    if (VRBase == 0 || !SRC->hasSubClassEq(MRI->getRegClass(VRBase)))
      VRBase = MRI->createVirtualRegister(SRC);

    // Create the insert_subreg or subreg_to_reg machine instruction.
    MachineInstrBuilder MIB =
      BuildMI(*MF, Node->getDebugLoc(), TII->get(Opc), VRBase);

    // If creating a subreg_to_reg, then the first input operand
    // is an implicit value immediate, otherwise it's a register
    if (Opc == TargetOpcode::SUBREG_TO_REG) {
      const ConstantSDNode *SD = cast<ConstantSDNode>(N0);
      MIB.addImm(SD->getZExtValue());
    } else
      AddOperand(MIB, N0, 0, nullptr, VRBaseMap, /*IsDebug=*/false,
                 IsClone, IsCloned);
    // Add the subregister being inserted
    AddOperand(MIB, N1, 0, nullptr, VRBaseMap, /*IsDebug=*/false,
               IsClone, IsCloned);
    MIB.addImm(SubIdx);
    MBB->insert(InsertPos, MIB);
  } else
    llvm_unreachable("Node is not insert_subreg, extract_subreg, or subreg_to_reg");

  SDValue Op(Node, 0);
  bool isNew = VRBaseMap.insert(std::make_pair(Op, VRBase)).second;
  (void)isNew; // Silence compiler warning.
  assert(isNew && "Node emitted out of order - early");
}

/// EmitCopyToRegClassNode - Generate machine code for COPY_TO_REGCLASS nodes.
/// COPY_TO_REGCLASS is just a normal copy, except that the destination
/// register is constrained to be in a particular register class.
///
void
InstrEmitter::EmitCopyToRegClassNode(SDNode *Node,
                                     DenseMap<SDValue, Register> &VRBaseMap) {
  unsigned VReg = getVR(Node->getOperand(0), VRBaseMap);

  // Create the new VReg in the destination class and emit a copy.
  unsigned DstRCIdx = Node->getConstantOperandVal(1);
  const TargetRegisterClass *DstRC =
    TRI->getAllocatableClass(TRI->getRegClass(DstRCIdx));
  Register NewVReg = MRI->createVirtualRegister(DstRC);
  BuildMI(*MBB, InsertPos, Node->getDebugLoc(), TII->get(TargetOpcode::COPY),
    NewVReg).addReg(VReg);

  SDValue Op(Node, 0);
  bool isNew = VRBaseMap.insert(std::make_pair(Op, NewVReg)).second;
  (void)isNew; // Silence compiler warning.
  assert(isNew && "Node emitted out of order - early");
}

/// EmitRegSequence - Generate machine code for REG_SEQUENCE nodes.
///
void InstrEmitter::EmitRegSequence(SDNode *Node,
                                  DenseMap<SDValue, Register> &VRBaseMap,
                                  bool IsClone, bool IsCloned) {
  unsigned DstRCIdx = Node->getConstantOperandVal(0);
  const TargetRegisterClass *RC = TRI->getRegClass(DstRCIdx);
  Register NewVReg = MRI->createVirtualRegister(TRI->getAllocatableClass(RC));
  const MCInstrDesc &II = TII->get(TargetOpcode::REG_SEQUENCE);
  MachineInstrBuilder MIB = BuildMI(*MF, Node->getDebugLoc(), II, NewVReg);
  unsigned NumOps = Node->getNumOperands();
  // If the input pattern has a chain, then the root of the corresponding
  // output pattern will get a chain as well. This can happen to be a
  // REG_SEQUENCE (which is not "guarded" by countOperands/CountResults).
  if (NumOps && Node->getOperand(NumOps-1).getValueType() == MVT::Other)
    --NumOps; // Ignore chain if it exists.

  assert((NumOps & 1) == 1 &&
         "REG_SEQUENCE must have an odd number of operands!");
  for (unsigned i = 1; i != NumOps; ++i) {
    SDValue Op = Node->getOperand(i);
    if ((i & 1) == 0) {
      RegisterSDNode *R = dyn_cast<RegisterSDNode>(Node->getOperand(i-1));
      // Skip physical registers as they don't have a vreg to get and we'll
      // insert copies for them in TwoAddressInstructionPass anyway.
      if (!R || !R->getReg().isPhysical()) {
        unsigned SubIdx = Op->getAsZExtVal();
        unsigned SubReg = getVR(Node->getOperand(i-1), VRBaseMap);
        const TargetRegisterClass *TRC = MRI->getRegClass(SubReg);
        const TargetRegisterClass *SRC =
        TRI->getMatchingSuperRegClass(RC, TRC, SubIdx);
        if (SRC && SRC != RC) {
          MRI->setRegClass(NewVReg, SRC);
          RC = SRC;
        }
      }
    }
    AddOperand(MIB, Op, i+1, &II, VRBaseMap, /*IsDebug=*/false,
               IsClone, IsCloned);
  }

  MBB->insert(InsertPos, MIB);
  SDValue Op(Node, 0);
  bool isNew = VRBaseMap.insert(std::make_pair(Op, NewVReg)).second;
  (void)isNew; // Silence compiler warning.
  assert(isNew && "Node emitted out of order - early");
}

/// EmitDbgValue - Generate machine instruction for a dbg_value node.
///
MachineInstr *
InstrEmitter::EmitDbgValue(SDDbgValue *SD,
                           DenseMap<SDValue, Register> &VRBaseMap) {
  DebugLoc DL = SD->getDebugLoc();
  assert(cast<DILocalVariable>(SD->getVariable())
             ->isValidLocationForIntrinsic(DL) &&
         "Expected inlined-at fields to agree");

  SD->setIsEmitted();

  assert(!SD->getLocationOps().empty() &&
         "dbg_value with no location operands?");

  if (SD->isInvalidated())
    return EmitDbgNoLocation(SD);

  // Attempt to produce a DBG_INSTR_REF if we've been asked to.
  if (EmitDebugInstrRefs)
    if (auto *InstrRef = EmitDbgInstrRef(SD, VRBaseMap))
      return InstrRef;

  // Emit variadic dbg_value nodes as DBG_VALUE_LIST if they have not been
  // emitted as instruction references.
  if (SD->isVariadic())
    return EmitDbgValueList(SD, VRBaseMap);

  // Emit single-location dbg_value nodes as DBG_VALUE if they have not been
  // emitted as instruction references.
  return EmitDbgValueFromSingleOp(SD, VRBaseMap);
}

MachineOperand GetMOForConstDbgOp(const SDDbgOperand &Op) {
  const Value *V = Op.getConst();
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    if (CI->getBitWidth() > 64)
      return MachineOperand::CreateCImm(CI);
    return MachineOperand::CreateImm(CI->getSExtValue());
  }
  if (const ConstantFP *CF = dyn_cast<ConstantFP>(V))
    return MachineOperand::CreateFPImm(CF);
  // Note: This assumes that all nullptr constants are zero-valued.
  if (isa<ConstantPointerNull>(V))
    return MachineOperand::CreateImm(0);
  // Undef or unhandled value type, so return an undef operand.
  return MachineOperand::CreateReg(
      /* Reg */ 0U, /* isDef */ false, /* isImp */ false,
      /* isKill */ false, /* isDead */ false,
      /* isUndef */ false, /* isEarlyClobber */ false,
      /* SubReg */ 0, /* isDebug */ true);
}

void InstrEmitter::AddDbgValueLocationOps(
    MachineInstrBuilder &MIB, const MCInstrDesc &DbgValDesc,
    ArrayRef<SDDbgOperand> LocationOps,
    DenseMap<SDValue, Register> &VRBaseMap) {
  for (const SDDbgOperand &Op : LocationOps) {
    switch (Op.getKind()) {
    case SDDbgOperand::FRAMEIX:
      MIB.addFrameIndex(Op.getFrameIx());
      break;
    case SDDbgOperand::VREG:
      MIB.addReg(Op.getVReg());
      break;
    case SDDbgOperand::SDNODE: {
      SDValue V = SDValue(Op.getSDNode(), Op.getResNo());
      // It's possible we replaced this SDNode with other(s) and therefore
      // didn't generate code for it. It's better to catch these cases where
      // they happen and transfer the debug info, but trying to guarantee that
      // in all cases would be very fragile; this is a safeguard for any
      // that were missed.
      if (VRBaseMap.count(V) == 0)
        MIB.addReg(0U); // undef
      else
        AddOperand(MIB, V, (*MIB).getNumOperands(), &DbgValDesc, VRBaseMap,
                   /*IsDebug=*/true, /*IsClone=*/false, /*IsCloned=*/false);
    } break;
    case SDDbgOperand::CONST:
      MIB.add(GetMOForConstDbgOp(Op));
      break;
    }
  }
}

MachineInstr *
InstrEmitter::EmitDbgInstrRef(SDDbgValue *SD,
                              DenseMap<SDValue, Register> &VRBaseMap) {
  MDNode *Var = SD->getVariable();
  const DIExpression *Expr = (DIExpression *)SD->getExpression();
  DebugLoc DL = SD->getDebugLoc();
  const MCInstrDesc &RefII = TII->get(TargetOpcode::DBG_INSTR_REF);

  // Returns true if the given operand is not a legal debug operand for a
  // DBG_INSTR_REF.
  auto IsInvalidOp = [](SDDbgOperand DbgOp) {
    return DbgOp.getKind() == SDDbgOperand::FRAMEIX;
  };
  // Returns true if the given operand is not itself an instruction reference
  // but is a legal debug operand for a DBG_INSTR_REF.
  auto IsNonInstrRefOp = [](SDDbgOperand DbgOp) {
    return DbgOp.getKind() == SDDbgOperand::CONST;
  };

  // If this variable location does not depend on any instructions or contains
  // any stack locations, produce it as a standard debug value instead.
  if (any_of(SD->getLocationOps(), IsInvalidOp) ||
      all_of(SD->getLocationOps(), IsNonInstrRefOp)) {
    if (SD->isVariadic())
      return EmitDbgValueList(SD, VRBaseMap);
    return EmitDbgValueFromSingleOp(SD, VRBaseMap);
  }

  // Immediately fold any indirectness from the LLVM-IR intrinsic into the
  // expression:
  if (SD->isIndirect())
    Expr = DIExpression::append(Expr, dwarf::DW_OP_deref);
  // If this is not already a variadic expression, it must be modified to become
  // one.
  if (!SD->isVariadic())
    Expr = DIExpression::convertToVariadicExpression(Expr);

  SmallVector<MachineOperand> MOs;

  // It may not be immediately possible to identify the MachineInstr that
  // defines a VReg, it can depend for example on the order blocks are
  // emitted in. When this happens, or when further analysis is needed later,
  // produce an instruction like this:
  //
  //    DBG_INSTR_REF !123, !456, %0:gr64
  //
  // i.e., point the instruction at the vreg, and patch it up later in
  // MachineFunction::finalizeDebugInstrRefs.
  auto AddVRegOp = [&](unsigned VReg) {
    MOs.push_back(MachineOperand::CreateReg(
        /* Reg */ VReg, /* isDef */ false, /* isImp */ false,
        /* isKill */ false, /* isDead */ false,
        /* isUndef */ false, /* isEarlyClobber */ false,
        /* SubReg */ 0, /* isDebug */ true));
  };
  unsigned OpCount = SD->getLocationOps().size();
  for (unsigned OpIdx = 0; OpIdx < OpCount; ++OpIdx) {
    SDDbgOperand DbgOperand = SD->getLocationOps()[OpIdx];

    // Try to find both the defined register and the instruction defining it.
    MachineInstr *DefMI = nullptr;
    unsigned VReg;

    if (DbgOperand.getKind() == SDDbgOperand::VREG) {
      VReg = DbgOperand.getVReg();

      // No definition means that block hasn't been emitted yet. Leave a vreg
      // reference to be fixed later.
      if (!MRI->hasOneDef(VReg)) {
        AddVRegOp(VReg);
        continue;
      }

      DefMI = &*MRI->def_instr_begin(VReg);
    } else if (DbgOperand.getKind() == SDDbgOperand::SDNODE) {
      // Look up the corresponding VReg for the given SDNode, if any.
      SDNode *Node = DbgOperand.getSDNode();
      SDValue Op = SDValue(Node, DbgOperand.getResNo());
      DenseMap<SDValue, Register>::iterator I = VRBaseMap.find(Op);
      // No VReg -> produce a DBG_VALUE $noreg instead.
      if (I == VRBaseMap.end())
        break;

      // Try to pick out a defining instruction at this point.
      VReg = getVR(Op, VRBaseMap);

      // Again, if there's no instruction defining the VReg right now, fix it up
      // later.
      if (!MRI->hasOneDef(VReg)) {
        AddVRegOp(VReg);
        continue;
      }

      DefMI = &*MRI->def_instr_begin(VReg);
    } else {
      assert(DbgOperand.getKind() == SDDbgOperand::CONST);
      MOs.push_back(GetMOForConstDbgOp(DbgOperand));
      continue;
    }

    // Avoid copy like instructions: they don't define values, only move them.
    // Leave a virtual-register reference until it can be fixed up later, to
    // find the underlying value definition.
    if (DefMI->isCopyLike() || TII->isCopyInstr(*DefMI)) {
      AddVRegOp(VReg);
      continue;
    }

    // Find the operand number which defines the specified VReg.
    unsigned OperandIdx = 0;
    for (const auto &MO : DefMI->operands()) {
      if (MO.isReg() && MO.isDef() && MO.getReg() == VReg)
        break;
      ++OperandIdx;
    }
    assert(OperandIdx < DefMI->getNumOperands());

    // Make the DBG_INSTR_REF refer to that instruction, and that operand.
    unsigned InstrNum = DefMI->getDebugInstrNum();
    MOs.push_back(MachineOperand::CreateDbgInstrRef(InstrNum, OperandIdx));
  }

  // If we haven't created a valid MachineOperand for every DbgOp, abort and
  // produce an undef DBG_VALUE.
  if (MOs.size() != OpCount)
    return EmitDbgNoLocation(SD);

  return BuildMI(*MF, DL, RefII, false, MOs, Var, Expr);
}

MachineInstr *InstrEmitter::EmitDbgNoLocation(SDDbgValue *SD) {
  // An invalidated SDNode must generate an undef DBG_VALUE: although the
  // original value is no longer computed, earlier DBG_VALUEs live ranges
  // must not leak into later code.
  DIVariable *Var = SD->getVariable();
  const DIExpression *Expr =
      DIExpression::convertToUndefExpression(SD->getExpression());
  DebugLoc DL = SD->getDebugLoc();
  const MCInstrDesc &Desc = TII->get(TargetOpcode::DBG_VALUE);
  return BuildMI(*MF, DL, Desc, false, 0U, Var, Expr);
}

MachineInstr *
InstrEmitter::EmitDbgValueList(SDDbgValue *SD,
                               DenseMap<SDValue, Register> &VRBaseMap) {
  MDNode *Var = SD->getVariable();
  DIExpression *Expr = SD->getExpression();
  DebugLoc DL = SD->getDebugLoc();
  // DBG_VALUE_LIST := "DBG_VALUE_LIST" var, expression, loc (, loc)*
  const MCInstrDesc &DbgValDesc = TII->get(TargetOpcode::DBG_VALUE_LIST);
  // Build the DBG_VALUE_LIST instruction base.
  auto MIB = BuildMI(*MF, DL, DbgValDesc);
  MIB.addMetadata(Var);
  MIB.addMetadata(Expr);
  AddDbgValueLocationOps(MIB, DbgValDesc, SD->getLocationOps(), VRBaseMap);
  return &*MIB;
}

MachineInstr *
InstrEmitter::EmitDbgValueFromSingleOp(SDDbgValue *SD,
                                       DenseMap<SDValue, Register> &VRBaseMap) {
  MDNode *Var = SD->getVariable();
  DIExpression *Expr = SD->getExpression();
  DebugLoc DL = SD->getDebugLoc();
  const MCInstrDesc &II = TII->get(TargetOpcode::DBG_VALUE);

  assert(SD->getLocationOps().size() == 1 &&
         "Non variadic dbg_value should have only one location op");

  // See about constant-folding the expression.
  // Copy the location operand in case we replace it.
  SmallVector<SDDbgOperand, 1> LocationOps(1, SD->getLocationOps()[0]);
  if (Expr && LocationOps[0].getKind() == SDDbgOperand::CONST) {
    const Value *V = LocationOps[0].getConst();
    if (auto *C = dyn_cast<ConstantInt>(V)) {
      std::tie(Expr, C) = Expr->constantFold(C);
      LocationOps[0] = SDDbgOperand::fromConst(C);
    }
  }

  // Emit non-variadic dbg_value nodes as DBG_VALUE.
  // DBG_VALUE := "DBG_VALUE" loc, isIndirect, var, expr
  auto MIB = BuildMI(*MF, DL, II);
  AddDbgValueLocationOps(MIB, II, LocationOps, VRBaseMap);

  if (SD->isIndirect())
    MIB.addImm(0U);
  else
    MIB.addReg(0U);

  return MIB.addMetadata(Var).addMetadata(Expr);
}

MachineInstr *
InstrEmitter::EmitDbgLabel(SDDbgLabel *SD) {
  MDNode *Label = SD->getLabel();
  DebugLoc DL = SD->getDebugLoc();
  assert(cast<DILabel>(Label)->isValidLocationForIntrinsic(DL) &&
         "Expected inlined-at fields to agree");

  const MCInstrDesc &II = TII->get(TargetOpcode::DBG_LABEL);
  MachineInstrBuilder MIB = BuildMI(*MF, DL, II);
  MIB.addMetadata(Label);

  return &*MIB;
}

/// EmitMachineNode - Generate machine code for a target-specific node and
/// needed dependencies.
///
void InstrEmitter::
EmitMachineNode(SDNode *Node, bool IsClone, bool IsCloned,
                DenseMap<SDValue, Register> &VRBaseMap) {
  unsigned Opc = Node->getMachineOpcode();

  // Handle subreg insert/extract specially
  if (Opc == TargetOpcode::EXTRACT_SUBREG ||
      Opc == TargetOpcode::INSERT_SUBREG ||
      Opc == TargetOpcode::SUBREG_TO_REG) {
    EmitSubregNode(Node, VRBaseMap, IsClone, IsCloned);
    return;
  }

  // Handle COPY_TO_REGCLASS specially.
  if (Opc == TargetOpcode::COPY_TO_REGCLASS) {
    EmitCopyToRegClassNode(Node, VRBaseMap);
    return;
  }

  // Handle REG_SEQUENCE specially.
  if (Opc == TargetOpcode::REG_SEQUENCE) {
    EmitRegSequence(Node, VRBaseMap, IsClone, IsCloned);
    return;
  }

  if (Opc == TargetOpcode::IMPLICIT_DEF)
    // We want a unique VR for each IMPLICIT_DEF use.
    return;

  const MCInstrDesc &II = TII->get(Opc);
  unsigned NumResults = CountResults(Node);
  unsigned NumDefs = II.getNumDefs();
  const MCPhysReg *ScratchRegs = nullptr;

  // Handle STACKMAP and PATCHPOINT specially and then use the generic code.
  if (Opc == TargetOpcode::STACKMAP || Opc == TargetOpcode::PATCHPOINT) {
    // Stackmaps do not have arguments and do not preserve their calling
    // convention. However, to simplify runtime support, they clobber the same
    // scratch registers as AnyRegCC.
    unsigned CC = CallingConv::AnyReg;
    if (Opc == TargetOpcode::PATCHPOINT) {
      CC = Node->getConstantOperandVal(PatchPointOpers::CCPos);
      NumDefs = NumResults;
    }
    ScratchRegs = TLI->getScratchRegisters((CallingConv::ID) CC);
  } else if (Opc == TargetOpcode::STATEPOINT) {
    NumDefs = NumResults;
  }

  unsigned NumImpUses = 0;
  unsigned NodeOperands =
    countOperands(Node, II.getNumOperands() - NumDefs, NumImpUses);
  bool HasVRegVariadicDefs = !MF->getTarget().usesPhysRegsForValues() &&
                             II.isVariadic() && II.variadicOpsAreDefs();
  bool HasPhysRegOuts = NumResults > NumDefs && !II.implicit_defs().empty() &&
                        !HasVRegVariadicDefs;
#ifndef NDEBUG
  unsigned NumMIOperands = NodeOperands + NumResults;
  if (II.isVariadic())
    assert(NumMIOperands >= II.getNumOperands() &&
           "Too few operands for a variadic node!");
  else
    assert(NumMIOperands >= II.getNumOperands() &&
           NumMIOperands <=
               II.getNumOperands() + II.implicit_defs().size() + NumImpUses &&
           "#operands for dag node doesn't match .td file!");
#endif

  // Create the new machine instruction.
  MachineInstrBuilder MIB = BuildMI(*MF, Node->getDebugLoc(), II);

  // Add result register values for things that are defined by this
  // instruction.
  if (NumResults) {
    CreateVirtualRegisters(Node, MIB, II, IsClone, IsCloned, VRBaseMap);

    // Transfer any IR flags from the SDNode to the MachineInstr
    MachineInstr *MI = MIB.getInstr();
    const SDNodeFlags Flags = Node->getFlags();
    if (Flags.hasNoSignedZeros())
      MI->setFlag(MachineInstr::MIFlag::FmNsz);

    if (Flags.hasAllowReciprocal())
      MI->setFlag(MachineInstr::MIFlag::FmArcp);

    if (Flags.hasNoNaNs())
      MI->setFlag(MachineInstr::MIFlag::FmNoNans);

    if (Flags.hasNoInfs())
      MI->setFlag(MachineInstr::MIFlag::FmNoInfs);

    if (Flags.hasAllowContract())
      MI->setFlag(MachineInstr::MIFlag::FmContract);

    if (Flags.hasApproximateFuncs())
      MI->setFlag(MachineInstr::MIFlag::FmAfn);

    if (Flags.hasAllowReassociation())
      MI->setFlag(MachineInstr::MIFlag::FmReassoc);

    if (Flags.hasNoUnsignedWrap())
      MI->setFlag(MachineInstr::MIFlag::NoUWrap);

    if (Flags.hasNoSignedWrap())
      MI->setFlag(MachineInstr::MIFlag::NoSWrap);

    if (Flags.hasExact())
      MI->setFlag(MachineInstr::MIFlag::IsExact);

    if (Flags.hasNoFPExcept())
      MI->setFlag(MachineInstr::MIFlag::NoFPExcept);

    if (Flags.hasUnpredictable())
      MI->setFlag(MachineInstr::MIFlag::Unpredictable);
  }

  // Emit all of the actual operands of this instruction, adding them to the
  // instruction as appropriate.
  bool HasOptPRefs = NumDefs > NumResults;
  assert((!HasOptPRefs || !HasPhysRegOuts) &&
         "Unable to cope with optional defs and phys regs defs!");
  unsigned NumSkip = HasOptPRefs ? NumDefs - NumResults : 0;
  for (unsigned i = NumSkip; i != NodeOperands; ++i)
    AddOperand(MIB, Node->getOperand(i), i-NumSkip+NumDefs, &II,
               VRBaseMap, /*IsDebug=*/false, IsClone, IsCloned);

  // Add scratch registers as implicit def and early clobber
  if (ScratchRegs)
    for (unsigned i = 0; ScratchRegs[i]; ++i)
      MIB.addReg(ScratchRegs[i], RegState::ImplicitDefine |
                                 RegState::EarlyClobber);

  // Set the memory reference descriptions of this instruction now that it is
  // part of the function.
  MIB.setMemRefs(cast<MachineSDNode>(Node)->memoperands());

  // Set the CFI type.
  MIB->setCFIType(*MF, Node->getCFIType());

  // Insert the instruction into position in the block. This needs to
  // happen before any custom inserter hook is called so that the
  // hook knows where in the block to insert the replacement code.
  MBB->insert(InsertPos, MIB);

  // The MachineInstr may also define physregs instead of virtregs.  These
  // physreg values can reach other instructions in different ways:
  //
  // 1. When there is a use of a Node value beyond the explicitly defined
  //    virtual registers, we emit a CopyFromReg for one of the implicitly
  //    defined physregs.  This only happens when HasPhysRegOuts is true.
  //
  // 2. A CopyFromReg reading a physreg may be glued to this instruction.
  //
  // 3. A glued instruction may implicitly use a physreg.
  //
  // 4. A glued instruction may use a RegisterSDNode operand.
  //
  // Collect all the used physreg defs, and make sure that any unused physreg
  // defs are marked as dead.
  SmallVector<Register, 8> UsedRegs;

  // Additional results must be physical register defs.
  if (HasPhysRegOuts) {
    for (unsigned i = NumDefs; i < NumResults; ++i) {
      Register Reg = II.implicit_defs()[i - NumDefs];
      if (!Node->hasAnyUseOfValue(i))
        continue;
      // This implicitly defined physreg has a use.
      UsedRegs.push_back(Reg);
      EmitCopyFromReg(Node, i, IsClone, Reg, VRBaseMap);
    }
  }

  // Scan the glue chain for any used physregs.
  if (Node->getValueType(Node->getNumValues()-1) == MVT::Glue) {
    for (SDNode *F = Node->getGluedUser(); F; F = F->getGluedUser()) {
      if (F->getOpcode() == ISD::CopyFromReg) {
        UsedRegs.push_back(cast<RegisterSDNode>(F->getOperand(1))->getReg());
        continue;
      } else if (F->getOpcode() == ISD::CopyToReg) {
        // Skip CopyToReg nodes that are internal to the glue chain.
        continue;
      }
      // Collect declared implicit uses.
      const MCInstrDesc &MCID = TII->get(F->getMachineOpcode());
      append_range(UsedRegs, MCID.implicit_uses());
      // In addition to declared implicit uses, we must also check for
      // direct RegisterSDNode operands.
      for (const SDValue &Op : F->op_values())
        if (RegisterSDNode *R = dyn_cast<RegisterSDNode>(Op)) {
          Register Reg = R->getReg();
          if (Reg.isPhysical())
            UsedRegs.push_back(Reg);
        }
    }
  }

  // Add rounding control registers as implicit def for function call.
  if (II.isCall() && MF->getFunction().hasFnAttribute(Attribute::StrictFP)) {
    ArrayRef<MCPhysReg> RCRegs = TLI->getRoundingControlRegisters();
    for (MCPhysReg Reg : RCRegs)
      UsedRegs.push_back(Reg);
  }

  // Finally mark unused registers as dead.
  if (!UsedRegs.empty() || !II.implicit_defs().empty() || II.hasOptionalDef())
    MIB->setPhysRegsDeadExcept(UsedRegs, *TRI);

  // STATEPOINT is too 'dynamic' to have meaningful machine description.
  // We have to manually tie operands.
  if (Opc == TargetOpcode::STATEPOINT && NumDefs > 0) {
    assert(!HasPhysRegOuts && "STATEPOINT mishandled");
    MachineInstr *MI = MIB;
    unsigned Def = 0;
    int First = StatepointOpers(MI).getFirstGCPtrIdx();
    assert(First > 0 && "Statepoint has Defs but no GC ptr list");
    unsigned Use = (unsigned)First;
    while (Def < NumDefs) {
      if (MI->getOperand(Use).isReg())
        MI->tieOperands(Def++, Use);
      Use = StackMaps::getNextMetaArgIdx(MI, Use);
    }
  }

  if (SDNode *GluedNode = Node->getGluedNode()) {
    // FIXME: Possibly iterate over multiple glue nodes?
    if (GluedNode->getOpcode() ==
        ~(unsigned)TargetOpcode::CONVERGENCECTRL_GLUE) {
      Register VReg = getVR(GluedNode->getOperand(0), VRBaseMap);
      MachineOperand MO = MachineOperand::CreateReg(VReg, /*isDef=*/false,
                                                    /*isImp=*/true);
      MIB->addOperand(MO);
    }
  }

  // Run post-isel target hook to adjust this instruction if needed.
  if (II.hasPostISelHook())
    TLI->AdjustInstrPostInstrSelection(*MIB, Node);
}

/// EmitSpecialNode - Generate machine code for a target-independent node and
/// needed dependencies.
void InstrEmitter::
EmitSpecialNode(SDNode *Node, bool IsClone, bool IsCloned,
                DenseMap<SDValue, Register> &VRBaseMap) {
  switch (Node->getOpcode()) {
  default:
#ifndef NDEBUG
    Node->dump();
#endif
    llvm_unreachable("This target-independent node should have been selected!");
  case ISD::EntryToken:
  case ISD::MERGE_VALUES:
  case ISD::TokenFactor: // fall thru
    break;
  case ISD::CopyToReg: {
    Register DestReg = cast<RegisterSDNode>(Node->getOperand(1))->getReg();
    SDValue SrcVal = Node->getOperand(2);
    if (DestReg.isVirtual() && SrcVal.isMachineOpcode() &&
        SrcVal.getMachineOpcode() == TargetOpcode::IMPLICIT_DEF) {
      // Instead building a COPY to that vreg destination, build an
      // IMPLICIT_DEF instruction instead.
      BuildMI(*MBB, InsertPos, Node->getDebugLoc(),
              TII->get(TargetOpcode::IMPLICIT_DEF), DestReg);
      break;
    }
    Register SrcReg;
    if (RegisterSDNode *R = dyn_cast<RegisterSDNode>(SrcVal))
      SrcReg = R->getReg();
    else
      SrcReg = getVR(SrcVal, VRBaseMap);

    if (SrcReg == DestReg) // Coalesced away the copy? Ignore.
      break;

    BuildMI(*MBB, InsertPos, Node->getDebugLoc(), TII->get(TargetOpcode::COPY),
            DestReg).addReg(SrcReg);
    break;
  }
  case ISD::CopyFromReg: {
    unsigned SrcReg = cast<RegisterSDNode>(Node->getOperand(1))->getReg();
    EmitCopyFromReg(Node, 0, IsClone, SrcReg, VRBaseMap);
    break;
  }
  case ISD::EH_LABEL:
  case ISD::ANNOTATION_LABEL: {
    unsigned Opc = (Node->getOpcode() == ISD::EH_LABEL)
                       ? TargetOpcode::EH_LABEL
                       : TargetOpcode::ANNOTATION_LABEL;
    MCSymbol *S = cast<LabelSDNode>(Node)->getLabel();
    BuildMI(*MBB, InsertPos, Node->getDebugLoc(),
            TII->get(Opc)).addSym(S);
    break;
  }

  case ISD::LIFETIME_START:
  case ISD::LIFETIME_END: {
    unsigned TarOp = (Node->getOpcode() == ISD::LIFETIME_START)
                         ? TargetOpcode::LIFETIME_START
                         : TargetOpcode::LIFETIME_END;
    auto *FI = cast<FrameIndexSDNode>(Node->getOperand(1));
    BuildMI(*MBB, InsertPos, Node->getDebugLoc(), TII->get(TarOp))
    .addFrameIndex(FI->getIndex());
    break;
  }

  case ISD::PSEUDO_PROBE: {
    unsigned TarOp = TargetOpcode::PSEUDO_PROBE;
    auto Guid = cast<PseudoProbeSDNode>(Node)->getGuid();
    auto Index = cast<PseudoProbeSDNode>(Node)->getIndex();
    auto Attr = cast<PseudoProbeSDNode>(Node)->getAttributes();

    BuildMI(*MBB, InsertPos, Node->getDebugLoc(), TII->get(TarOp))
        .addImm(Guid)
        .addImm(Index)
        .addImm((uint8_t)PseudoProbeType::Block)
        .addImm(Attr);
    break;
  }

  case ISD::INLINEASM:
  case ISD::INLINEASM_BR: {
    unsigned NumOps = Node->getNumOperands();
    if (Node->getOperand(NumOps-1).getValueType() == MVT::Glue)
      --NumOps;  // Ignore the glue operand.

    // Create the inline asm machine instruction.
    unsigned TgtOpc = Node->getOpcode() == ISD::INLINEASM_BR
                          ? TargetOpcode::INLINEASM_BR
                          : TargetOpcode::INLINEASM;
    MachineInstrBuilder MIB =
        BuildMI(*MF, Node->getDebugLoc(), TII->get(TgtOpc));

    // Add the asm string as an external symbol operand.
    SDValue AsmStrV = Node->getOperand(InlineAsm::Op_AsmString);
    const char *AsmStr = cast<ExternalSymbolSDNode>(AsmStrV)->getSymbol();
    MIB.addExternalSymbol(AsmStr);

    // Add the HasSideEffect, isAlignStack, AsmDialect, MayLoad and MayStore
    // bits.
    int64_t ExtraInfo =
      cast<ConstantSDNode>(Node->getOperand(InlineAsm::Op_ExtraInfo))->
                          getZExtValue();
    MIB.addImm(ExtraInfo);

    // Remember to operand index of the group flags.
    SmallVector<unsigned, 8> GroupIdx;

    // Remember registers that are part of early-clobber defs.
    SmallVector<unsigned, 8> ECRegs;

    // Add all of the operand registers to the instruction.
    for (unsigned i = InlineAsm::Op_FirstOperand; i != NumOps;) {
      unsigned Flags = Node->getConstantOperandVal(i);
      const InlineAsm::Flag F(Flags);
      const unsigned NumVals = F.getNumOperandRegisters();

      GroupIdx.push_back(MIB->getNumOperands());
      MIB.addImm(Flags);
      ++i;  // Skip the ID value.

      switch (F.getKind()) {
      case InlineAsm::Kind::RegDef:
        for (unsigned j = 0; j != NumVals; ++j, ++i) {
          Register Reg = cast<RegisterSDNode>(Node->getOperand(i))->getReg();
          // FIXME: Add dead flags for physical and virtual registers defined.
          // For now, mark physical register defs as implicit to help fast
          // regalloc. This makes inline asm look a lot like calls.
          MIB.addReg(Reg, RegState::Define | getImplRegState(Reg.isPhysical()));
        }
        break;
      case InlineAsm::Kind::RegDefEarlyClobber:
      case InlineAsm::Kind::Clobber:
        for (unsigned j = 0; j != NumVals; ++j, ++i) {
          Register Reg = cast<RegisterSDNode>(Node->getOperand(i))->getReg();
          MIB.addReg(Reg, RegState::Define | RegState::EarlyClobber |
                              getImplRegState(Reg.isPhysical()));
          ECRegs.push_back(Reg);
        }
        break;
      case InlineAsm::Kind::RegUse: // Use of register.
      case InlineAsm::Kind::Imm:    // Immediate.
      case InlineAsm::Kind::Mem:    // Non-function addressing mode.
        // The addressing mode has been selected, just add all of the
        // operands to the machine instruction.
        for (unsigned j = 0; j != NumVals; ++j, ++i)
          AddOperand(MIB, Node->getOperand(i), 0, nullptr, VRBaseMap,
                     /*IsDebug=*/false, IsClone, IsCloned);

        // Manually set isTied bits.
        if (F.isRegUseKind()) {
          unsigned DefGroup;
          if (F.isUseOperandTiedToDef(DefGroup)) {
            unsigned DefIdx = GroupIdx[DefGroup] + 1;
            unsigned UseIdx = GroupIdx.back() + 1;
            for (unsigned j = 0; j != NumVals; ++j)
              MIB->tieOperands(DefIdx + j, UseIdx + j);
          }
        }
        break;
      case InlineAsm::Kind::Func: // Function addressing mode.
        for (unsigned j = 0; j != NumVals; ++j, ++i) {
          SDValue Op = Node->getOperand(i);
          AddOperand(MIB, Op, 0, nullptr, VRBaseMap,
                     /*IsDebug=*/false, IsClone, IsCloned);

          // Adjust Target Flags for function reference.
          if (auto *TGA = dyn_cast<GlobalAddressSDNode>(Op)) {
            unsigned NewFlags =
                MF->getSubtarget().classifyGlobalFunctionReference(
                    TGA->getGlobal());
            unsigned LastIdx = MIB.getInstr()->getNumOperands() - 1;
            MIB.getInstr()->getOperand(LastIdx).setTargetFlags(NewFlags);
          }
        }
      }
    }

    // Add rounding control registers as implicit def for inline asm.
    if (MF->getFunction().hasFnAttribute(Attribute::StrictFP)) {
      ArrayRef<MCPhysReg> RCRegs = TLI->getRoundingControlRegisters();
      for (MCPhysReg Reg : RCRegs)
        MIB.addReg(Reg, RegState::ImplicitDefine);
    }

    // GCC inline assembly allows input operands to also be early-clobber
    // output operands (so long as the operand is written only after it's
    // used), but this does not match the semantics of our early-clobber flag.
    // If an early-clobber operand register is also an input operand register,
    // then remove the early-clobber flag.
    for (unsigned Reg : ECRegs) {
      if (MIB->readsRegister(Reg, TRI)) {
        MachineOperand *MO =
            MIB->findRegisterDefOperand(Reg, TRI, false, false);
        assert(MO && "No def operand for clobbered register?");
        MO->setIsEarlyClobber(false);
      }
    }

    // Get the mdnode from the asm if it exists and add it to the instruction.
    SDValue MDV = Node->getOperand(InlineAsm::Op_MDNode);
    const MDNode *MD = cast<MDNodeSDNode>(MDV)->getMD();
    if (MD)
      MIB.addMetadata(MD);

    MBB->insert(InsertPos, MIB);
    break;
  }
  }
}

/// InstrEmitter - Construct an InstrEmitter and set it to start inserting
/// at the given position in the given block.
InstrEmitter::InstrEmitter(const TargetMachine &TM, MachineBasicBlock *mbb,
                           MachineBasicBlock::iterator insertpos)
    : MF(mbb->getParent()), MRI(&MF->getRegInfo()),
      TII(MF->getSubtarget().getInstrInfo()),
      TRI(MF->getSubtarget().getRegisterInfo()),
      TLI(MF->getSubtarget().getTargetLowering()), MBB(mbb),
      InsertPos(insertpos) {
  EmitDebugInstrRefs = mbb->getParent()->useDebugInstrRef();
}
