//- WebAssemblyISelLowering.h - WebAssembly DAG Lowering Interface -*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the interfaces that WebAssembly uses to lower LLVM
/// code into a selection DAG.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYISELLOWERING_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

namespace WebAssemblyISD {

enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
#define HANDLE_NODETYPE(NODE) NODE,
#define HANDLE_MEM_NODETYPE(NODE)
#include "WebAssemblyISD.def"
  FIRST_MEM_OPCODE = ISD::FIRST_TARGET_MEMORY_OPCODE,
#undef HANDLE_NODETYPE
#undef HANDLE_MEM_NODETYPE
#define HANDLE_NODETYPE(NODE)
#define HANDLE_MEM_NODETYPE(NODE) NODE,
#include "WebAssemblyISD.def"
#undef HANDLE_NODETYPE
#undef HANDLE_MEM_NODETYPE
};

} // end namespace WebAssemblyISD

class WebAssemblySubtarget;

class WebAssemblyTargetLowering final : public TargetLowering {
public:
  WebAssemblyTargetLowering(const TargetMachine &TM,
                            const WebAssemblySubtarget &STI);

  MVT getPointerTy(const DataLayout &DL, uint32_t AS = 0) const override;
  MVT getPointerMemTy(const DataLayout &DL, uint32_t AS = 0) const override;

private:
  /// Keep a pointer to the WebAssemblySubtarget around so that we can make the
  /// right decision when generating code for different targets.
  const WebAssemblySubtarget *Subtarget;

  AtomicExpansionKind shouldExpandAtomicRMWInIR(AtomicRMWInst *) const override;
  bool shouldScalarizeBinop(SDValue VecOp) const override;
  FastISel *createFastISel(FunctionLoweringInfo &FuncInfo,
                           const TargetLibraryInfo *LibInfo) const override;
  MVT getScalarShiftAmountTy(const DataLayout &DL, EVT) const override;
  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *MBB) const override;
  const char *getTargetNodeName(unsigned Opcode) const override;
  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;
  bool isCheapToSpeculateCttz(Type *Ty) const override;
  bool isCheapToSpeculateCtlz(Type *Ty) const override;
  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AS,
                             Instruction *I = nullptr) const override;
  bool allowsMisalignedMemoryAccesses(EVT, unsigned AddrSpace, Align Alignment,
                                      MachineMemOperand::Flags Flags,
                                      unsigned *Fast) const override;
  bool isIntDivCheap(EVT VT, AttributeList Attr) const override;
  bool isVectorLoadExtDesirable(SDValue ExtVal) const override;
  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;
  bool shouldSinkOperands(Instruction *I,
                          SmallVectorImpl<Use *> &Ops) const override;
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;
  bool getTgtMemIntrinsic(IntrinsicInfo &Info, const CallInst &I,
                          MachineFunction &MF,
                          unsigned Intrinsic) const override;

  void computeKnownBitsForTargetNode(const SDValue Op, KnownBits &Known,
                                     const APInt &DemandedElts,
                                     const SelectionDAG &DAG,
                                     unsigned Depth) const override;

  TargetLoweringBase::LegalizeTypeAction
  getPreferredVectorAction(MVT VT) const override;

  SDValue LowerCall(CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;
  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context) const override;
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
                      SelectionDAG &DAG) const override;
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  bool
  shouldSimplifyDemandedVectorElts(SDValue Op,
                                   const TargetLoweringOpt &TLO) const override;

  // Custom lowering hooks.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  SDValue LowerFrameIndex(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_JT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerCopyToReg(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerIntrinsic(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSIGN_EXTEND_INREG(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerEXTEND_VECTOR_INREG(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVECTOR_SHUFFLE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerAccessVectorElement(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerShift(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFP_TO_INT_SAT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerLoad(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerStore(SDValue Op, SelectionDAG &DAG) const;

  // Custom DAG combine hooks
  SDValue
  PerformDAGCombine(SDNode *N,
                    TargetLowering::DAGCombinerInfo &DCI) const override;
};

namespace WebAssembly {
FastISel *createFastISel(FunctionLoweringInfo &funcInfo,
                         const TargetLibraryInfo *libInfo);
} // end namespace WebAssembly

} // end namespace llvm

#endif
