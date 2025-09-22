//===-- AVRISelLowering.h - AVR DAG Lowering Interface ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that AVR uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_AVR_ISEL_LOWERING_H
#define LLVM_AVR_ISEL_LOWERING_H

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

namespace AVRISD {

/// AVR Specific DAG Nodes
enum NodeType {
  /// Start the numbering where the builtin ops leave off.
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  /// Return from subroutine.
  RET_GLUE,
  /// Return from ISR.
  RETI_GLUE,
  /// Represents an abstract call instruction,
  /// which includes a bunch of information.
  CALL,
  /// A wrapper node for TargetConstantPool,
  /// TargetExternalSymbol, and TargetGlobalAddress.
  WRAPPER,
  LSL,     ///< Logical shift left.
  LSLBN,   ///< Byte logical shift left N bits.
  LSLWN,   ///< Word logical shift left N bits.
  LSLHI,   ///< Higher 8-bit of word logical shift left.
  LSLW,    ///< Wide logical shift left.
  LSR,     ///< Logical shift right.
  LSRBN,   ///< Byte logical shift right N bits.
  LSRWN,   ///< Word logical shift right N bits.
  LSRLO,   ///< Lower 8-bit of word logical shift right.
  LSRW,    ///< Wide logical shift right.
  ASR,     ///< Arithmetic shift right.
  ASRBN,   ///< Byte arithmetic shift right N bits.
  ASRWN,   ///< Word arithmetic shift right N bits.
  ASRLO,   ///< Lower 8-bit of word arithmetic shift right.
  ASRW,    ///< Wide arithmetic shift right.
  ROR,     ///< Bit rotate right.
  ROL,     ///< Bit rotate left.
  LSLLOOP, ///< A loop of single logical shift left instructions.
  LSRLOOP, ///< A loop of single logical shift right instructions.
  ROLLOOP, ///< A loop of single left bit rotate instructions.
  RORLOOP, ///< A loop of single right bit rotate instructions.
  ASRLOOP, ///< A loop of single arithmetic shift right instructions.
  /// AVR conditional branches. Operand 0 is the chain operand, operand 1
  /// is the block to branch if condition is true, operand 2 is the
  /// condition code, and operand 3 is the flag operand produced by a CMP
  /// or TEST instruction.
  BRCOND,
  /// Compare instruction.
  CMP,
  /// Compare with carry instruction.
  CMPC,
  /// Test for zero or minus instruction.
  TST,
  /// Swap Rd[7:4] <-> Rd[3:0].
  SWAP,
  /// Operand 0 and operand 1 are selection variable, operand 2
  /// is condition code and operand 3 is flag operand.
  SELECT_CC
};

} // end of namespace AVRISD

class AVRSubtarget;
class AVRTargetMachine;

/// Performs target lowering for the AVR.
class AVRTargetLowering : public TargetLowering {
public:
  explicit AVRTargetLowering(const AVRTargetMachine &TM,
                             const AVRSubtarget &STI);

public:
  MVT getScalarShiftAmountTy(const DataLayout &, EVT LHSTy) const override {
    return MVT::i8;
  }

  MVT::SimpleValueType getCmpLibcallReturnType() const override {
    return MVT::i8;
  }

  const char *getTargetNodeName(unsigned Opcode) const override;

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AS,
                             Instruction *I = nullptr) const override;

  bool getPreIndexedAddressParts(SDNode *N, SDValue &Base, SDValue &Offset,
                                 ISD::MemIndexedMode &AM,
                                 SelectionDAG &DAG) const override;

  bool getPostIndexedAddressParts(SDNode *N, SDNode *Op, SDValue &Base,
                                  SDValue &Offset, ISD::MemIndexedMode &AM,
                                  SelectionDAG &DAG) const override;

  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;

  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *MBB) const override;

  ConstraintType getConstraintType(StringRef Constraint) const override;

  ConstraintWeight
  getSingleConstraintMatchWeight(AsmOperandInfo &info,
                                 const char *constraint) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

  InlineAsm::ConstraintCode
  getInlineAsmMemConstraint(StringRef ConstraintCode) const override;

  void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                    std::vector<SDValue> &Ops,
                                    SelectionDAG &DAG) const override;

  Register getRegisterByName(const char *RegName, LLT VT,
                             const MachineFunction &MF) const override;

  bool shouldSplitFunctionArgumentsAsLittleEndian(
      const DataLayout &DL) const override {
    return false;
  }

  ShiftLegalizationStrategy
  preferredShiftLegalizationStrategy(SelectionDAG &DAG, SDNode *N,
                                     unsigned ExpansionFactor) const override {
    return ShiftLegalizationStrategy::LowerToLibcall;
  }

private:
  SDValue getAVRCmp(SDValue LHS, SDValue RHS, ISD::CondCode CC, SDValue &AVRcc,
                    SelectionDAG &DAG, SDLoc dl) const;
  SDValue getAVRCmp(SDValue LHS, SDValue RHS, SelectionDAG &DAG,
                    SDLoc dl) const;
  SDValue LowerShifts(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDivRem(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINLINEASM(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
                      SelectionDAG &DAG) const override;
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &dl, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;
  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;
  SDValue LowerCallResult(SDValue Chain, SDValue InGlue,
                          CallingConv::ID CallConv, bool isVarArg,
                          const SmallVectorImpl<ISD::InputArg> &Ins,
                          const SDLoc &dl, SelectionDAG &DAG,
                          SmallVectorImpl<SDValue> &InVals) const;

protected:
  const AVRSubtarget &Subtarget;

private:
  MachineBasicBlock *insertShift(MachineInstr &MI, MachineBasicBlock *BB,
                                 bool Tiny) const;
  MachineBasicBlock *insertWideShift(MachineInstr &MI,
                                     MachineBasicBlock *BB) const;
  MachineBasicBlock *insertMul(MachineInstr &MI, MachineBasicBlock *BB) const;
  MachineBasicBlock *insertCopyZero(MachineInstr &MI,
                                    MachineBasicBlock *BB) const;
  MachineBasicBlock *insertAtomicArithmeticOp(MachineInstr &MI,
                                              MachineBasicBlock *BB,
                                              unsigned Opcode, int Width) const;
};

} // end namespace llvm

#endif // LLVM_AVR_ISEL_LOWERING_H
