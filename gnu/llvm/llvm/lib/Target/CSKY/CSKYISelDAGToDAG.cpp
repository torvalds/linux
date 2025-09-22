//===-- CSKYISelDAGToDAG.cpp - A dag to dag inst selector for CSKY---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the CSKY target.
//
//===----------------------------------------------------------------------===//

#include "CSKY.h"
#include "CSKYSubtarget.h"
#include "CSKYTargetMachine.h"
#include "MCTargetDesc/CSKYMCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"

using namespace llvm;

#define DEBUG_TYPE "csky-isel"
#define PASS_NAME "CSKY DAG->DAG Pattern Instruction Selection"

namespace {
class CSKYDAGToDAGISel : public SelectionDAGISel {
  const CSKYSubtarget *Subtarget;

public:
  explicit CSKYDAGToDAGISel(CSKYTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    // Reset the subtarget each time through.
    Subtarget = &MF.getSubtarget<CSKYSubtarget>();
    SelectionDAGISel::runOnMachineFunction(MF);
    return true;
  }

  void Select(SDNode *N) override;
  bool selectAddCarry(SDNode *N);
  bool selectSubCarry(SDNode *N);
  bool selectBITCAST_TO_LOHI(SDNode *N);
  bool selectInlineAsm(SDNode *N);

  SDNode *createGPRPairNode(EVT VT, SDValue V0, SDValue V1);

  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintID,
                                    std::vector<SDValue> &OutOps) override;

#include "CSKYGenDAGISel.inc"
};

class CSKYDAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  explicit CSKYDAGToDAGISelLegacy(CSKYTargetMachine &TM,
                                  CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<CSKYDAGToDAGISel>(TM, OptLevel)) {}
};
} // namespace

char CSKYDAGToDAGISelLegacy::ID = 0;

INITIALIZE_PASS(CSKYDAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

void CSKYDAGToDAGISel::Select(SDNode *N) {
  // If we have a custom node, we have already selected
  if (N->isMachineOpcode()) {
    LLVM_DEBUG(dbgs() << "== "; N->dump(CurDAG); dbgs() << "\n");
    N->setNodeId(-1);
    return;
  }

  SDLoc Dl(N);
  unsigned Opcode = N->getOpcode();
  bool IsSelected = false;

  switch (Opcode) {
  default:
    break;
  case ISD::UADDO_CARRY:
    IsSelected = selectAddCarry(N);
    break;
  case ISD::USUBO_CARRY:
    IsSelected = selectSubCarry(N);
    break;
  case ISD::GLOBAL_OFFSET_TABLE: {
    Register GP = Subtarget->getInstrInfo()->getGlobalBaseReg(*MF);
    ReplaceNode(N, CurDAG->getRegister(GP, N->getValueType(0)).getNode());

    IsSelected = true;
    break;
  }
  case ISD::FrameIndex: {
    SDValue Imm = CurDAG->getTargetConstant(0, Dl, MVT::i32);
    int FI = cast<FrameIndexSDNode>(N)->getIndex();
    SDValue TFI = CurDAG->getTargetFrameIndex(FI, MVT::i32);
    ReplaceNode(N, CurDAG->getMachineNode(Subtarget->hasE2() ? CSKY::ADDI32
                                                             : CSKY::ADDI16XZ,
                                          Dl, MVT::i32, TFI, Imm));

    IsSelected = true;
    break;
  }
  case CSKYISD::BITCAST_TO_LOHI:
    IsSelected = selectBITCAST_TO_LOHI(N);
    break;
  case ISD::INLINEASM:
  case ISD::INLINEASM_BR:
    IsSelected = selectInlineAsm(N);
    break;
  }

  if (IsSelected)
    return;

  // Select the default instruction.
  SelectCode(N);
}

bool CSKYDAGToDAGISel::selectInlineAsm(SDNode *N) {
  std::vector<SDValue> AsmNodeOperands;
  InlineAsm::Flag Flag;
  bool Changed = false;
  unsigned NumOps = N->getNumOperands();

  // Normally, i64 data is bounded to two arbitrary GRPs for "%r" constraint.
  // However, some instructions (e.g. mula.s32) require GPR pair.
  // Since there is no constraint to explicitly specify a
  // reg pair, we use GPRPair reg class for "%r" for 64-bit data.

  SDLoc dl(N);
  SDValue Glue =
      N->getGluedNode() ? N->getOperand(NumOps - 1) : SDValue(nullptr, 0);

  SmallVector<bool, 8> OpChanged;
  // Glue node will be appended late.
  for (unsigned i = 0, e = N->getGluedNode() ? NumOps - 1 : NumOps; i < e;
       ++i) {
    SDValue op = N->getOperand(i);
    AsmNodeOperands.push_back(op);

    if (i < InlineAsm::Op_FirstOperand)
      continue;

    if (const auto *C = dyn_cast<ConstantSDNode>(N->getOperand(i)))
      Flag = InlineAsm::Flag(C->getZExtValue());
    else
      continue;

    // Immediate operands to inline asm in the SelectionDAG are modeled with
    // two operands. The first is a constant of value InlineAsm::Kind::Imm, and
    // the second is a constant with the value of the immediate. If we get here
    // and we have a Kind::Imm, skip the next operand, and continue.
    if (Flag.isImmKind()) {
      SDValue op = N->getOperand(++i);
      AsmNodeOperands.push_back(op);
      continue;
    }

    const unsigned NumRegs = Flag.getNumOperandRegisters();
    if (NumRegs)
      OpChanged.push_back(false);

    unsigned DefIdx = 0;
    bool IsTiedToChangedOp = false;
    // If it's a use that is tied with a previous def, it has no
    // reg class constraint.
    if (Changed && Flag.isUseOperandTiedToDef(DefIdx))
      IsTiedToChangedOp = OpChanged[DefIdx];

    // Memory operands to inline asm in the SelectionDAG are modeled with two
    // operands: a constant of value InlineAsm::Kind::Mem followed by the input
    // operand. If we get here and we have a Kind::Mem, skip the next operand
    // (so it doesn't get misinterpreted), and continue. We do this here because
    // it's important to update the OpChanged array correctly before moving on.
    if (Flag.isMemKind()) {
      SDValue op = N->getOperand(++i);
      AsmNodeOperands.push_back(op);
      continue;
    }

    if (!Flag.isRegUseKind() && !Flag.isRegDefKind() &&
        !Flag.isRegDefEarlyClobberKind())
      continue;

    unsigned RC;
    const bool HasRC = Flag.hasRegClassConstraint(RC);
    if ((!IsTiedToChangedOp && (!HasRC || RC != CSKY::GPRRegClassID)) ||
        NumRegs != 2)
      continue;

    assert((i + 2 < NumOps) && "Invalid number of operands in inline asm");
    SDValue V0 = N->getOperand(i + 1);
    SDValue V1 = N->getOperand(i + 2);
    unsigned Reg0 = cast<RegisterSDNode>(V0)->getReg();
    unsigned Reg1 = cast<RegisterSDNode>(V1)->getReg();
    SDValue PairedReg;
    MachineRegisterInfo &MRI = MF->getRegInfo();

    if (Flag.isRegDefKind() || Flag.isRegDefEarlyClobberKind()) {
      // Replace the two GPRs with 1 GPRPair and copy values from GPRPair to
      // the original GPRs.

      Register GPVR = MRI.createVirtualRegister(&CSKY::GPRPairRegClass);
      PairedReg = CurDAG->getRegister(GPVR, MVT::i64);
      SDValue Chain = SDValue(N, 0);

      SDNode *GU = N->getGluedUser();
      SDValue RegCopy =
          CurDAG->getCopyFromReg(Chain, dl, GPVR, MVT::i64, Chain.getValue(1));

      // Extract values from a GPRPair reg and copy to the original GPR reg.
      SDValue Sub0 =
          CurDAG->getTargetExtractSubreg(CSKY::sub32_0, dl, MVT::i32, RegCopy);
      SDValue Sub1 =
          CurDAG->getTargetExtractSubreg(CSKY::sub32_32, dl, MVT::i32, RegCopy);
      SDValue T0 =
          CurDAG->getCopyToReg(Sub0, dl, Reg0, Sub0, RegCopy.getValue(1));
      SDValue T1 = CurDAG->getCopyToReg(Sub1, dl, Reg1, Sub1, T0.getValue(1));

      // Update the original glue user.
      std::vector<SDValue> Ops(GU->op_begin(), GU->op_end() - 1);
      Ops.push_back(T1.getValue(1));
      CurDAG->UpdateNodeOperands(GU, Ops);
    } else {
      // For Kind  == InlineAsm::Kind::RegUse, we first copy two GPRs into a
      // GPRPair and then pass the GPRPair to the inline asm.
      SDValue Chain = AsmNodeOperands[InlineAsm::Op_InputChain];

      // As REG_SEQ doesn't take RegisterSDNode, we copy them first.
      SDValue T0 =
          CurDAG->getCopyFromReg(Chain, dl, Reg0, MVT::i32, Chain.getValue(1));
      SDValue T1 =
          CurDAG->getCopyFromReg(Chain, dl, Reg1, MVT::i32, T0.getValue(1));
      SDValue Pair = SDValue(createGPRPairNode(MVT::i64, T0, T1), 0);

      // Copy REG_SEQ into a GPRPair-typed VR and replace the original two
      // i32 VRs of inline asm with it.
      Register GPVR = MRI.createVirtualRegister(&CSKY::GPRPairRegClass);
      PairedReg = CurDAG->getRegister(GPVR, MVT::i64);
      Chain = CurDAG->getCopyToReg(T1, dl, GPVR, Pair, T1.getValue(1));

      AsmNodeOperands[InlineAsm::Op_InputChain] = Chain;
      Glue = Chain.getValue(1);
    }

    Changed = true;

    if (PairedReg.getNode()) {
      OpChanged[OpChanged.size() - 1] = true;
      // TODO: maybe a setter for getNumOperandRegisters?
      Flag = InlineAsm::Flag(Flag.getKind(), 1 /* RegNum*/);
      if (IsTiedToChangedOp)
        Flag.setMatchingOp(DefIdx);
      else
        Flag.setRegClass(CSKY::GPRPairRegClassID);
      // Replace the current flag.
      AsmNodeOperands[AsmNodeOperands.size() - 1] =
          CurDAG->getTargetConstant(Flag, dl, MVT::i32);
      // Add the new register node and skip the original two GPRs.
      AsmNodeOperands.push_back(PairedReg);
      // Skip the next two GPRs.
      i += 2;
    }
  }

  if (Glue.getNode())
    AsmNodeOperands.push_back(Glue);
  if (!Changed)
    return false;

  SDValue New = CurDAG->getNode(N->getOpcode(), SDLoc(N),
                                CurDAG->getVTList(MVT::Other, MVT::Glue),
                                AsmNodeOperands);
  New->setNodeId(-1);
  ReplaceNode(N, New.getNode());
  return true;
}

bool CSKYDAGToDAGISel::selectBITCAST_TO_LOHI(SDNode *N) {
  SDLoc Dl(N);
  auto VT = N->getValueType(0);
  auto V = N->getOperand(0);

  if (!Subtarget->hasFPUv2DoubleFloat())
    return false;

  SDValue V1 = SDValue(CurDAG->getMachineNode(CSKY::FMFVRL_D, Dl, VT, V), 0);
  SDValue V2 = SDValue(CurDAG->getMachineNode(CSKY::FMFVRH_D, Dl, VT, V), 0);

  ReplaceUses(SDValue(N, 0), V1);
  ReplaceUses(SDValue(N, 1), V2);
  CurDAG->RemoveDeadNode(N);

  return true;
}

bool CSKYDAGToDAGISel::selectAddCarry(SDNode *N) {
  MachineSDNode *NewNode = nullptr;
  auto Type0 = N->getValueType(0);
  auto Type1 = N->getValueType(1);
  auto Op0 = N->getOperand(0);
  auto Op1 = N->getOperand(1);
  auto Op2 = N->getOperand(2);

  SDLoc Dl(N);

  if (isNullConstant(Op2)) {
    auto *CA = CurDAG->getMachineNode(
        Subtarget->has2E3() ? CSKY::CLRC32 : CSKY::CLRC16, Dl, Type1);
    NewNode = CurDAG->getMachineNode(
        Subtarget->has2E3() ? CSKY::ADDC32 : CSKY::ADDC16, Dl, {Type0, Type1},
        {Op0, Op1, SDValue(CA, 0)});
  } else if (isOneConstant(Op2)) {
    auto *CA = CurDAG->getMachineNode(
        Subtarget->has2E3() ? CSKY::SETC32 : CSKY::SETC16, Dl, Type1);
    NewNode = CurDAG->getMachineNode(
        Subtarget->has2E3() ? CSKY::ADDC32 : CSKY::ADDC16, Dl, {Type0, Type1},
        {Op0, Op1, SDValue(CA, 0)});
  } else {
    NewNode = CurDAG->getMachineNode(Subtarget->has2E3() ? CSKY::ADDC32
                                                         : CSKY::ADDC16,
                                     Dl, {Type0, Type1}, {Op0, Op1, Op2});
  }
  ReplaceNode(N, NewNode);
  return true;
}

static SDValue InvertCarryFlag(const CSKYSubtarget *Subtarget,
                               SelectionDAG *DAG, SDLoc Dl, SDValue OldCarry) {
  auto NewCarryReg =
      DAG->getMachineNode(Subtarget->has2E3() ? CSKY::MVCV32 : CSKY::MVCV16, Dl,
                          MVT::i32, OldCarry);
  auto NewCarry =
      DAG->getMachineNode(Subtarget->hasE2() ? CSKY::BTSTI32 : CSKY::BTSTI16,
                          Dl, OldCarry.getValueType(), SDValue(NewCarryReg, 0),
                          DAG->getTargetConstant(0, Dl, MVT::i32));
  return SDValue(NewCarry, 0);
}

bool CSKYDAGToDAGISel::selectSubCarry(SDNode *N) {
  MachineSDNode *NewNode = nullptr;
  auto Type0 = N->getValueType(0);
  auto Type1 = N->getValueType(1);
  auto Op0 = N->getOperand(0);
  auto Op1 = N->getOperand(1);
  auto Op2 = N->getOperand(2);

  SDLoc Dl(N);

  if (isNullConstant(Op2)) {
    auto *CA = CurDAG->getMachineNode(
        Subtarget->has2E3() ? CSKY::SETC32 : CSKY::SETC16, Dl, Type1);
    NewNode = CurDAG->getMachineNode(
        Subtarget->has2E3() ? CSKY::SUBC32 : CSKY::SUBC16, Dl, {Type0, Type1},
        {Op0, Op1, SDValue(CA, 0)});
  } else if (isOneConstant(Op2)) {
    auto *CA = CurDAG->getMachineNode(
        Subtarget->has2E3() ? CSKY::CLRC32 : CSKY::CLRC16, Dl, Type1);
    NewNode = CurDAG->getMachineNode(
        Subtarget->has2E3() ? CSKY::SUBC32 : CSKY::SUBC16, Dl, {Type0, Type1},
        {Op0, Op1, SDValue(CA, 0)});
  } else {
    auto CarryIn = InvertCarryFlag(Subtarget, CurDAG, Dl, Op2);
    NewNode = CurDAG->getMachineNode(Subtarget->has2E3() ? CSKY::SUBC32
                                                         : CSKY::SUBC16,
                                     Dl, {Type0, Type1}, {Op0, Op1, CarryIn});
  }
  auto CarryOut = InvertCarryFlag(Subtarget, CurDAG, Dl, SDValue(NewNode, 1));

  ReplaceUses(SDValue(N, 0), SDValue(NewNode, 0));
  ReplaceUses(SDValue(N, 1), CarryOut);
  CurDAG->RemoveDeadNode(N);

  return true;
}

SDNode *CSKYDAGToDAGISel::createGPRPairNode(EVT VT, SDValue V0, SDValue V1) {
  SDLoc dl(V0.getNode());
  SDValue RegClass =
      CurDAG->getTargetConstant(CSKY::GPRPairRegClassID, dl, MVT::i32);
  SDValue SubReg0 = CurDAG->getTargetConstant(CSKY::sub32_0, dl, MVT::i32);
  SDValue SubReg1 = CurDAG->getTargetConstant(CSKY::sub32_32, dl, MVT::i32);
  const SDValue Ops[] = {RegClass, V0, SubReg0, V1, SubReg1};
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl, VT, Ops);
}

bool CSKYDAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, const InlineAsm::ConstraintCode ConstraintID,
    std::vector<SDValue> &OutOps) {
  switch (ConstraintID) {
  case InlineAsm::ConstraintCode::m:
    // We just support simple memory operands that have a single address
    // operand and need no special handling.
    OutOps.push_back(Op);
    return false;
  default:
    break;
  }

  return true;
}

FunctionPass *llvm::createCSKYISelDag(CSKYTargetMachine &TM,
                                      CodeGenOptLevel OptLevel) {
  return new CSKYDAGToDAGISelLegacy(TM, OptLevel);
}
