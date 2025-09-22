//===-- M68kISelLowering.h - M68k DAG Lowering Interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the interfaces that M68k uses to lower LLVM code into a
/// selection DAG.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M68K_M68KISELLOWERING_H
#define LLVM_LIB_TARGET_M68K_M68KISELLOWERING_H

#include "M68k.h"

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/Function.h"

#include <deque>

namespace llvm {
namespace M68kISD {

/// M68k Specific DAG nodes
enum NodeType {
  /// Start the numbering from where ISD NodeType finishes.
  FIRST_NUMBER = ISD::BUILTIN_OP_END,

  CALL,
  RET,
  TAIL_CALL,
  TC_RETURN,

  /// M68k compare and logical compare instructions. Subtracts the source
  /// operand from the destination data register and sets the condition
  /// codes according to the result. Immediate always goes first.
  CMP,

  /// M68k bit-test instructions.
  BTST,

  /// M68k Select
  SELECT,

  /// M68k SetCC. Operand 0 is condition code, and operand 1 is the CCR
  /// operand, usually produced by a CMP instruction.
  SETCC,

  // Same as SETCC except it's materialized with a subx and the value is all
  // one's or all zero's.
  SETCC_CARRY, // R = carry_bit ? ~0 : 0

  /// M68k conditional moves. Operand 0 and operand 1 are the two values
  /// to select from. Operand 2 is the condition code, and operand 3 is the
  /// flag operand produced by a CMP or TEST instruction. It also writes a
  /// flag result.
  CMOV,

  /// M68k conditional branches. Operand 0 is the chain operand, operand 1
  /// is the block to branch if condition is true, operand 2 is the
  /// condition code, and operand 3 is the flag operand produced by a CMP
  /// or TEST instruction.
  BRCOND,

  // Arithmetic operations with CCR results.
  ADD,
  SUB,
  ADDX,
  SUBX,
  SMUL,
  UMUL,
  OR,
  XOR,
  AND,

  // GlobalBaseReg,
  GLOBAL_BASE_REG,

  /// A wrapper node for TargetConstantPool,
  /// TargetExternalSymbol, and TargetGlobalAddress.
  Wrapper,

  /// Special wrapper used under M68k PIC mode for PC
  /// relative displacements.
  WrapperPC,

  // For allocating variable amounts of stack space when using
  // segmented stacks. Check if the current stacklet has enough space, and
  // falls back to heap allocation if not.
  SEG_ALLOCA,
};
} // namespace M68kISD

/// Define some predicates that are used for node matching.
namespace M68k {

/// Determines whether the callee is required to pop its
/// own arguments. Callee pop is necessary to support tail calls.
bool isCalleePop(CallingConv::ID CallingConv, bool IsVarArg, bool GuaranteeTCO);

} // end namespace M68k

//===--------------------------------------------------------------------===//
// TargetLowering Implementation
//===--------------------------------------------------------------------===//

class M68kMachineFunctionInfo;
class M68kSubtarget;

class M68kTargetLowering : public TargetLowering {
  const M68kSubtarget &Subtarget;
  const M68kTargetMachine &TM;

public:
  explicit M68kTargetLowering(const M68kTargetMachine &TM,
                              const M68kSubtarget &STI);

  static const M68kTargetLowering *create(const M68kTargetMachine &TM,
                                          const M68kSubtarget &STI);

  const char *getTargetNodeName(unsigned Opcode) const override;

  /// Return the value type to use for ISD::SETCC.
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

  /// EVT is not used in-tree, but is used by out-of-tree target.
  virtual MVT getScalarShiftAmountTy(const DataLayout &, EVT) const override;

  /// Provide custom lowering hooks for some operations.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  /// Return the entry encoding for a jump table in the current function.
  /// The returned value is a member of the  MachineJumpTableInfo::JTEntryKind
  /// enum.
  unsigned getJumpTableEncoding() const override;

  const MCExpr *LowerCustomJumpTableEntry(const MachineJumpTableInfo *MJTI,
                                          const MachineBasicBlock *MBB,
                                          unsigned uid,
                                          MCContext &Ctx) const override;

  /// Returns relocation base for the given PIC jumptable.
  SDValue getPICJumpTableRelocBase(SDValue Table,
                                   SelectionDAG &DAG) const override;

  /// This returns the relocation base for the given PIC jumptable,
  /// the same as getPICJumpTableRelocBase, but as an MCExpr.
  const MCExpr *getPICJumpTableRelocBaseExpr(const MachineFunction *MF,
                                             unsigned JTI,
                                             MCContext &Ctx) const override;

  ConstraintType getConstraintType(StringRef ConstraintStr) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

  // Lower operand with C_Immediate and C_Other constraint type
  void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                    std::vector<SDValue> &Ops,
                                    SelectionDAG &DAG) const override;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *MBB) const override;

  CCAssignFn *getCCAssignFn(CallingConv::ID CC, bool Return,
                            bool IsVarArg) const;

  AtomicExpansionKind
  shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const override;

  /// If a physical register, this returns the register that receives the
  /// exception address on entry to an EH pad.
  Register
  getExceptionPointerRegister(const Constant *PersonalityFn) const override;

  /// If a physical register, this returns the register that receives the
  /// exception typeid on entry to a landing pad.
  Register
  getExceptionSelectorRegister(const Constant *PersonalityFn) const override;

  InlineAsm::ConstraintCode
  getInlineAsmMemConstraint(StringRef ConstraintCode) const override;

private:
  unsigned GetAlignedArgumentStackSize(unsigned StackSize,
                                       SelectionDAG &DAG) const;

  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override {
    // In many cases, `GA` doesn't give the correct offset to fold. It's
    // hard to know if the real offset actually fits into the displacement
    // of the perspective addressing mode.
    // Thus, we disable offset folding altogether and leave that to ISel
    // patterns.
    return false;
  }

  SDValue getReturnAddressFrameIndex(SelectionDAG &DAG) const;

  /// Emit a load of return address if tail call
  /// optimization is performed and it is required.
  SDValue EmitTailCallLoadRetAddr(SelectionDAG &DAG, SDValue &OutRetAddr,
                                  SDValue Chain, bool IsTailCall, int FPDiff,
                                  const SDLoc &DL) const;

  /// Emit a store of the return address if tail call
  /// optimization is performed and it is required (FPDiff!=0).
  SDValue EmitTailCallStoreRetAddr(SelectionDAG &DAG, MachineFunction &MF,
                                   SDValue Chain, SDValue RetAddrFrIdx,
                                   EVT PtrVT, unsigned SlotSize, int FPDiff,
                                   const SDLoc &DL) const;

  SDValue LowerMemArgument(SDValue Chain, CallingConv::ID CallConv,
                           const SmallVectorImpl<ISD::InputArg> &ArgInfo,
                           const SDLoc &DL, SelectionDAG &DAG,
                           const CCValAssign &VA, MachineFrameInfo &MFI,
                           unsigned ArgIdx) const;

  SDValue LowerMemOpCallTo(SDValue Chain, SDValue StackPtr, SDValue Arg,
                           const SDLoc &DL, SelectionDAG &DAG,
                           const CCValAssign &VA, ISD::ArgFlagsTy Flags) const;

  SDValue LowerXALUO(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerToBTST(SDValue And, ISD::CondCode CC, const SDLoc &DL,
                      SelectionDAG &DAG) const;
  SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSETCCCARRY(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerADDC_ADDE_SUBC_SUBE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalAddress(const GlobalValue *GV, const SDLoc &DL,
                             int64_t Offset, SelectionDAG &DAG) const;
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerShiftLeftParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerShiftRightParts(SDValue Op, SelectionDAG &DAG, bool IsSRA) const;

  SDValue LowerATOMICFENCE(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerCallResult(SDValue Chain, SDValue InGlue,
                          CallingConv::ID CallConv, bool IsVarArg,
                          const SmallVectorImpl<ISD::InputArg> &Ins,
                          const SDLoc &DL, SelectionDAG &DAG,
                          SmallVectorImpl<SDValue> &InVals) const;
  SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;

  /// LowerFormalArguments - transform physical registers into virtual
  /// registers and generate load operations for arguments places on the stack.
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CCID,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerCall(CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context) const override;

  /// Lower the result values of a call into the
  /// appropriate copies out of appropriate physical registers.
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CCID, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

  SDValue LowerExternalSymbolCall(SelectionDAG &DAG, SDLoc loc,
                                  llvm::StringRef SymbolName,
                                  ArgListTy &&ArgList) const;
  SDValue getTLSGetAddr(GlobalAddressSDNode *GA, SelectionDAG &DAG,
                        unsigned TargetFlags) const;
  SDValue getM68kReadTp(SDLoc Loc, SelectionDAG &DAG) const;

  SDValue LowerTLSGeneralDynamic(GlobalAddressSDNode *GA,
                                 SelectionDAG &DAG) const;
  SDValue LowerTLSLocalDynamic(GlobalAddressSDNode *GA,
                               SelectionDAG &DAG) const;
  SDValue LowerTLSInitialExec(GlobalAddressSDNode *GA, SelectionDAG &DAG) const;
  SDValue LowerTLSLocalExec(GlobalAddressSDNode *GA, SelectionDAG &DAG) const;

  bool decomposeMulByConstant(LLVMContext &Context, EVT VT,
                              SDValue C) const override;

  MachineBasicBlock *EmitLoweredSelect(MachineInstr &I,
                                       MachineBasicBlock *MBB) const;
  MachineBasicBlock *EmitLoweredSegAlloca(MachineInstr &MI,
                                          MachineBasicBlock *BB) const;

  /// Emit nodes that will be selected as "test Op0,Op0", or something
  /// equivalent, for use with the given M68k condition code.
  SDValue EmitTest(SDValue Op0, unsigned M68kCC, const SDLoc &dl,
                   SelectionDAG &DAG) const;

  /// Emit nodes that will be selected as "cmp Op0,Op1", or something
  /// equivalent, for use with the given M68k condition code.
  SDValue EmitCmp(SDValue Op0, SDValue Op1, unsigned M68kCC, const SDLoc &dl,
                  SelectionDAG &DAG) const;

  /// Check whether the call is eligible for tail call optimization. Targets
  /// that want to do tail call optimization should implement this function.
  bool IsEligibleForTailCallOptimization(
      SDValue Callee, CallingConv::ID CalleeCC, bool IsVarArg,
      bool IsCalleeStructRet, bool IsCallerStructRet, Type *RetTy,
      const SmallVectorImpl<ISD::OutputArg> &Outs,
      const SmallVectorImpl<SDValue> &OutVals,
      const SmallVectorImpl<ISD::InputArg> &Ins, SelectionDAG &DAG) const;

  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;
};
} // namespace llvm

#endif // LLVM_LIB_TARGET_M68K_M68KISELLOWERING_H
