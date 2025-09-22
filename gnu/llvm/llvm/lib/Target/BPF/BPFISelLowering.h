//===-- BPFISelLowering.h - BPF DAG Lowering Interface ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that BPF uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_BPFISELLOWERING_H
#define LLVM_LIB_TARGET_BPF_BPFISELLOWERING_H

#include "BPF.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
class BPFSubtarget;
namespace BPFISD {
enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  RET_GLUE,
  CALL,
  SELECT_CC,
  BR_CC,
  Wrapper,
  MEMCPY
};
}

class BPFTargetLowering : public TargetLowering {
public:
  explicit BPFTargetLowering(const TargetMachine &TM, const BPFSubtarget &STI);

  // Provide custom lowering hooks for some operations.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  // This method returns the name of a target specific DAG node.
  const char *getTargetNodeName(unsigned Opcode) const override;

  // This method decides whether folding a constant offset
  // with the given GlobalAddress is legal.
  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;

  BPFTargetLowering::ConstraintType
  getConstraintType(StringRef Constraint) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;

  bool getHasAlu32() const { return HasAlu32; }
  bool getHasJmp32() const { return HasJmp32; }
  bool getHasJmpExt() const { return HasJmpExt; }

  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

  MVT getScalarShiftAmountTy(const DataLayout &, EVT) const override;

private:
  // Control Instruction Selection Features
  bool HasAlu32;
  bool HasJmp32;
  bool HasJmpExt;
  bool HasMovsx;

  SDValue LowerSDIVSREM(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;

  template <class NodeTy>
  SDValue getAddr(NodeTy *N, SelectionDAG &DAG, unsigned Flags = 0) const;

  // Lower the result values of a call, copying them out of physregs into vregs
  SDValue LowerCallResult(SDValue Chain, SDValue InGlue,
                          CallingConv::ID CallConv, bool IsVarArg,
                          const SmallVectorImpl<ISD::InputArg> &Ins,
                          const SDLoc &DL, SelectionDAG &DAG,
                          SmallVectorImpl<SDValue> &InVals) const;

  // Maximum number of arguments to a call
  static const size_t MaxArgs;

  // Lower a call into CALLSEQ_START - BPFISD:CALL - CALLSEQ_END chain
  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  // Lower incoming arguments, copy physregs into vregs
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  EVT getOptimalMemOpType(const MemOp &Op,
                          const AttributeList &FuncAttributes) const override {
    return Op.size() >= 8 ? MVT::i64 : MVT::i32;
  }

  bool isIntDivCheap(EVT VT, AttributeList Attr) const override { return true; }

  bool shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                         Type *Ty) const override {
    return true;
  }

  // Prevent reducing load width during SelectionDag phase.
  // Otherwise, we may transform the following
  //   ctx = ctx + reloc_offset
  //   ... (*(u32 *)ctx) & 0x8000...
  // to
  //   ctx = ctx + reloc_offset
  //   ... (*(u8 *)(ctx + 1)) & 0x80 ...
  // which will be rejected by the verifier.
  bool shouldReduceLoadWidth(SDNode *Load, ISD::LoadExtType ExtTy,
                             EVT NewVT) const override {
    return false;
  }

  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM,
                             Type *Ty, unsigned AS,
                             Instruction *I = nullptr) const override;

  // isTruncateFree - Return true if it's free to truncate a value of
  // type Ty1 to type Ty2. e.g. On BPF at alu32 mode, it's free to truncate
  // a i64 value in register R1 to i32 by referencing its sub-register W1.
  bool isTruncateFree(Type *Ty1, Type *Ty2) const override;
  bool isTruncateFree(EVT VT1, EVT VT2) const override;

  // For 32bit ALU result zext to 64bit is free.
  bool isZExtFree(Type *Ty1, Type *Ty2) const override;
  bool isZExtFree(EVT VT1, EVT VT2) const override;
  bool isZExtFree(SDValue Val, EVT VT2) const override;

  unsigned EmitSubregExt(MachineInstr &MI, MachineBasicBlock *BB, unsigned Reg,
                         bool isSigned) const;

  MachineBasicBlock * EmitInstrWithCustomInserterMemcpy(MachineInstr &MI,
                                                        MachineBasicBlock *BB)
                                                        const;

};
}

#endif
