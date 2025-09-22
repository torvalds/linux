//===-- CSKYISelLowering.cpp - CSKY DAG Lowering Implementation  ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that CSKY uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_CSKYISELLOWERING_H
#define LLVM_LIB_TARGET_CSKY_CSKYISELLOWERING_H

#include "MCTargetDesc/CSKYBaseInfo.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
class CSKYSubtarget;

namespace CSKYISD {
enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  NIE,
  NIR,
  RET,
  CALL,
  CALLReg,
  TAIL,
  TAILReg,
  LOAD_ADDR,
  // i32, i32 <-- f64
  BITCAST_TO_LOHI,
  // f64 < -- i32, i32
  BITCAST_FROM_LOHI,
};
}

class CSKYTargetLowering : public TargetLowering {
  const CSKYSubtarget &Subtarget;

public:
  explicit CSKYTargetLowering(const TargetMachine &TM,
                              const CSKYSubtarget &STI);

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

private:
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  const char *getTargetNodeName(unsigned Opcode) const override;

  /// If a physical register, this returns the register that receives the
  /// exception address on entry to an EH pad.
  Register
  getExceptionPointerRegister(const Constant *PersonalityFn) const override;

  /// If a physical register, this returns the register that receives the
  /// exception typeid on entry to a landing pad.
  Register
  getExceptionSelectorRegister(const Constant *PersonalityFn) const override;

  bool isSelectSupported(SelectSupportKind Kind) const override {
    // CSKY does not support scalar condition selects on vectors.
    return (Kind != ScalarCondVectorVal);
  }

  ConstraintType getConstraintType(StringRef Constraint) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;

  SDValue getTargetNode(GlobalAddressSDNode *N, SDLoc DL, EVT Ty,
                        SelectionDAG &DAG, unsigned Flags) const;

  SDValue getTargetNode(ExternalSymbolSDNode *N, SDLoc DL, EVT Ty,
                        SelectionDAG &DAG, unsigned Flags) const;

  SDValue getTargetNode(JumpTableSDNode *N, SDLoc DL, EVT Ty, SelectionDAG &DAG,
                        unsigned Flags) const;

  SDValue getTargetNode(BlockAddressSDNode *N, SDLoc DL, EVT Ty,
                        SelectionDAG &DAG, unsigned Flags) const;

  SDValue getTargetNode(ConstantPoolSDNode *N, SDLoc DL, EVT Ty,
                        SelectionDAG &DAG, unsigned Flags) const;

  SDValue getTargetConstantPoolValue(GlobalAddressSDNode *N, EVT Ty,
                                     SelectionDAG &DAG, unsigned Flags) const;

  SDValue getTargetConstantPoolValue(ExternalSymbolSDNode *N, EVT Ty,
                                     SelectionDAG &DAG, unsigned Flags) const;

  SDValue getTargetConstantPoolValue(JumpTableSDNode *N, EVT Ty,
                                     SelectionDAG &DAG, unsigned Flags) const;

  SDValue getTargetConstantPoolValue(BlockAddressSDNode *N, EVT Ty,
                                     SelectionDAG &DAG, unsigned Flags) const;

  SDValue getTargetConstantPoolValue(ConstantPoolSDNode *N, EVT Ty,
                                     SelectionDAG &DAG, unsigned Flags) const;

  template <class NodeTy, bool IsCall = false>
  SDValue getAddr(NodeTy *N, SelectionDAG &DAG, bool IsLocal = true) const {
    SDLoc DL(N);
    EVT Ty = getPointerTy(DAG.getDataLayout());

    unsigned Flag = CSKYII::MO_None;
    bool IsPIC = isPositionIndependent();

    if (IsPIC)
      Flag = IsLocal  ? CSKYII::MO_GOTOFF
             : IsCall ? CSKYII::MO_PLT32
                      : CSKYII::MO_GOT32;

    SDValue TCPV = getTargetConstantPoolValue(N, Ty, DAG, Flag);
    SDValue TV = getTargetNode(N, DL, Ty, DAG, Flag);
    SDValue Addr = DAG.getNode(CSKYISD::LOAD_ADDR, DL, Ty, {TV, TCPV});

    if (!IsPIC)
      return Addr;

    SDValue Result =
        DAG.getNode(ISD::ADD, DL, Ty, {DAG.getGLOBAL_OFFSET_TABLE(Ty), Addr});
    if (IsLocal)
      return Result;

    return DAG.getLoad(Ty, DL, DAG.getEntryNode(), Result,
                       MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  }

  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;

  SDValue getStaticTLSAddr(GlobalAddressSDNode *N, SelectionDAG &DAG,
                           bool UseGOT) const;
  SDValue getDynamicTLSAddr(GlobalAddressSDNode *N, SelectionDAG &DAG) const;

  CCAssignFn *CCAssignFnForCall(CallingConv::ID CC, bool IsVarArg) const;
  CCAssignFn *CCAssignFnForReturn(CallingConv::ID CC, bool IsVarArg) const;

  bool decomposeMulByConstant(LLVMContext &Context, EVT VT,
                              SDValue C) const override;
  bool isCheapToSpeculateCttz(Type *Ty) const override;
  bool isCheapToSpeculateCtlz(Type *Ty) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_CSKY_CSKYISELLOWERING_H
