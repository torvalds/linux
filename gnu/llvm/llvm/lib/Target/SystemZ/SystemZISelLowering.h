//===-- SystemZISelLowering.h - SystemZ DAG lowering interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that SystemZ uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZISELLOWERING_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZISELLOWERING_H

#include "SystemZ.h"
#include "SystemZInstrInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"
#include <optional>

namespace llvm {
namespace SystemZISD {
enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,

  // Return with a glue operand.  Operand 0 is the chain operand.
  RET_GLUE,

  // Calls a function.  Operand 0 is the chain operand and operand 1
  // is the target address.  The arguments start at operand 2.
  // There is an optional glue operand at the end.
  CALL,
  SIBCALL,

  // TLS calls.  Like regular calls, except operand 1 is the TLS symbol.
  // (The call target is implicitly __tls_get_offset.)
  TLS_GDCALL,
  TLS_LDCALL,

  // Wraps a TargetGlobalAddress that should be loaded using PC-relative
  // accesses (LARL).  Operand 0 is the address.
  PCREL_WRAPPER,

  // Used in cases where an offset is applied to a TargetGlobalAddress.
  // Operand 0 is the full TargetGlobalAddress and operand 1 is a
  // PCREL_WRAPPER for an anchor point.  This is used so that we can
  // cheaply refer to either the full address or the anchor point
  // as a register base.
  PCREL_OFFSET,

  // Integer comparisons.  There are three operands: the two values
  // to compare, and an integer of type SystemZICMP.
  ICMP,

  // Floating-point comparisons.  The two operands are the values to compare.
  FCMP,

  // Test under mask.  The first operand is ANDed with the second operand
  // and the condition codes are set on the result.  The third operand is
  // a boolean that is true if the condition codes need to distinguish
  // between CCMASK_TM_MIXED_MSB_0 and CCMASK_TM_MIXED_MSB_1 (which the
  // register forms do but the memory forms don't).
  TM,

  // Branches if a condition is true.  Operand 0 is the chain operand;
  // operand 1 is the 4-bit condition-code mask, with bit N in
  // big-endian order meaning "branch if CC=N"; operand 2 is the
  // target block and operand 3 is the flag operand.
  BR_CCMASK,

  // Selects between operand 0 and operand 1.  Operand 2 is the
  // mask of condition-code values for which operand 0 should be
  // chosen over operand 1; it has the same form as BR_CCMASK.
  // Operand 3 is the flag operand.
  SELECT_CCMASK,

  // Evaluates to the gap between the stack pointer and the
  // base of the dynamically-allocatable area.
  ADJDYNALLOC,

  // For allocating stack space when using stack clash protector.
  // Allocation is performed by block, and each block is probed.
  PROBED_ALLOCA,

  // Count number of bits set in operand 0 per byte.
  POPCNT,

  // Wrappers around the ISD opcodes of the same name.  The output is GR128.
  // Input operands may be GR64 or GR32, depending on the instruction.
  SMUL_LOHI,
  UMUL_LOHI,
  SDIVREM,
  UDIVREM,

  // Add/subtract with overflow/carry.  These have the same operands as
  // the corresponding standard operations, except with the carry flag
  // replaced by a condition code value.
  SADDO, SSUBO, UADDO, USUBO, ADDCARRY, SUBCARRY,

  // Set the condition code from a boolean value in operand 0.
  // Operand 1 is a mask of all condition-code values that may result of this
  // operation, operand 2 is a mask of condition-code values that may result
  // if the boolean is true.
  // Note that this operation is always optimized away, we will never
  // generate any code for it.
  GET_CCMASK,

  // Use a series of MVCs to copy bytes from one memory location to another.
  // The operands are:
  // - the target address
  // - the source address
  // - the constant length
  //
  // This isn't a memory opcode because we'd need to attach two
  // MachineMemOperands rather than one.
  MVC,

  // Similar to MVC, but for logic operations (AND, OR, XOR).
  NC,
  OC,
  XC,

  // Use CLC to compare two blocks of memory, with the same comments
  // as for MVC.
  CLC,

  // Use MVC to set a block of memory after storing the first byte.
  MEMSET_MVC,

  // Use an MVST-based sequence to implement stpcpy().
  STPCPY,

  // Use a CLST-based sequence to implement strcmp().  The two input operands
  // are the addresses of the strings to compare.
  STRCMP,

  // Use an SRST-based sequence to search a block of memory.  The first
  // operand is the end address, the second is the start, and the third
  // is the character to search for.  CC is set to 1 on success and 2
  // on failure.
  SEARCH_STRING,

  // Store the CC value in bits 29 and 28 of an integer.
  IPM,

  // Transaction begin.  The first operand is the chain, the second
  // the TDB pointer, and the third the immediate control field.
  // Returns CC value and chain.
  TBEGIN,
  TBEGIN_NOFLOAT,

  // Transaction end.  Just the chain operand.  Returns CC value and chain.
  TEND,

  // Create a vector constant by filling byte N of the result with bit
  // 15-N of the single operand.
  BYTE_MASK,

  // Create a vector constant by replicating an element-sized RISBG-style mask.
  // The first operand specifies the starting set bit and the second operand
  // specifies the ending set bit.  Both operands count from the MSB of the
  // element.
  ROTATE_MASK,

  // Replicate a GPR scalar value into all elements of a vector.
  REPLICATE,

  // Create a vector from two i64 GPRs.
  JOIN_DWORDS,

  // Replicate one element of a vector into all elements.  The first operand
  // is the vector and the second is the index of the element to replicate.
  SPLAT,

  // Interleave elements from the high half of operand 0 and the high half
  // of operand 1.
  MERGE_HIGH,

  // Likewise for the low halves.
  MERGE_LOW,

  // Concatenate the vectors in the first two operands, shift them left
  // by the third operand, and take the first half of the result.
  SHL_DOUBLE,

  // Take one element of the first v2i64 operand and the one element of
  // the second v2i64 operand and concatenate them to form a v2i64 result.
  // The third operand is a 4-bit value of the form 0A0B, where A and B
  // are the element selectors for the first operand and second operands
  // respectively.
  PERMUTE_DWORDS,

  // Perform a general vector permute on vector operands 0 and 1.
  // Each byte of operand 2 controls the corresponding byte of the result,
  // in the same way as a byte-level VECTOR_SHUFFLE mask.
  PERMUTE,

  // Pack vector operands 0 and 1 into a single vector with half-sized elements.
  PACK,

  // Likewise, but saturate the result and set CC.  PACKS_CC does signed
  // saturation and PACKLS_CC does unsigned saturation.
  PACKS_CC,
  PACKLS_CC,

  // Unpack the first half of vector operand 0 into double-sized elements.
  // UNPACK_HIGH sign-extends and UNPACKL_HIGH zero-extends.
  UNPACK_HIGH,
  UNPACKL_HIGH,

  // Likewise for the second half.
  UNPACK_LOW,
  UNPACKL_LOW,

  // Shift/rotate each element of vector operand 0 by the number of bits
  // specified by scalar operand 1.
  VSHL_BY_SCALAR,
  VSRL_BY_SCALAR,
  VSRA_BY_SCALAR,
  VROTL_BY_SCALAR,

  // For each element of the output type, sum across all sub-elements of
  // operand 0 belonging to the corresponding element, and add in the
  // rightmost sub-element of the corresponding element of operand 1.
  VSUM,

  // Compute carry/borrow indication for add/subtract.
  VACC, VSCBI,
  // Add/subtract with carry/borrow.
  VAC, VSBI,
  // Compute carry/borrow indication for add/subtract with carry/borrow.
  VACCC, VSBCBI,

  // Compare integer vector operands 0 and 1 to produce the usual 0/-1
  // vector result.  VICMPE is for equality, VICMPH for "signed greater than"
  // and VICMPHL for "unsigned greater than".
  VICMPE,
  VICMPH,
  VICMPHL,

  // Likewise, but also set the condition codes on the result.
  VICMPES,
  VICMPHS,
  VICMPHLS,

  // Compare floating-point vector operands 0 and 1 to produce the usual 0/-1
  // vector result.  VFCMPE is for "ordered and equal", VFCMPH for "ordered and
  // greater than" and VFCMPHE for "ordered and greater than or equal to".
  VFCMPE,
  VFCMPH,
  VFCMPHE,

  // Likewise, but also set the condition codes on the result.
  VFCMPES,
  VFCMPHS,
  VFCMPHES,

  // Test floating-point data class for vectors.
  VFTCI,

  // Extend the even f32 elements of vector operand 0 to produce a vector
  // of f64 elements.
  VEXTEND,

  // Round the f64 elements of vector operand 0 to f32s and store them in the
  // even elements of the result.
  VROUND,

  // AND the two vector operands together and set CC based on the result.
  VTM,

  // i128 high integer comparisons.
  SCMP128HI,
  UCMP128HI,

  // String operations that set CC as a side-effect.
  VFAE_CC,
  VFAEZ_CC,
  VFEE_CC,
  VFEEZ_CC,
  VFENE_CC,
  VFENEZ_CC,
  VISTR_CC,
  VSTRC_CC,
  VSTRCZ_CC,
  VSTRS_CC,
  VSTRSZ_CC,

  // Test Data Class.
  //
  // Operand 0: the value to test
  // Operand 1: the bit mask
  TDC,

  // z/OS XPLINK ADA Entry
  // Wraps a TargetGlobalAddress that should be loaded from a function's
  // AssociatedData Area (ADA). Tha ADA is passed to the function by the
  // caller in the XPLink ABI defined register R5.
  // Operand 0: the GlobalValue/External Symbol
  // Operand 1: the ADA register
  // Operand 2: the offset (0 for the first and 8 for the second element in the
  // function descriptor)
  ADA_ENTRY,

  // Strict variants of scalar floating-point comparisons.
  // Quiet and signaling versions.
  STRICT_FCMP = ISD::FIRST_TARGET_STRICTFP_OPCODE,
  STRICT_FCMPS,

  // Strict variants of vector floating-point comparisons.
  // Quiet and signaling versions.
  STRICT_VFCMPE,
  STRICT_VFCMPH,
  STRICT_VFCMPHE,
  STRICT_VFCMPES,
  STRICT_VFCMPHS,
  STRICT_VFCMPHES,

  // Strict variants of VEXTEND and VROUND.
  STRICT_VEXTEND,
  STRICT_VROUND,

  // Wrappers around the inner loop of an 8- or 16-bit ATOMIC_SWAP or
  // ATOMIC_LOAD_<op>.
  //
  // Operand 0: the address of the containing 32-bit-aligned field
  // Operand 1: the second operand of <op>, in the high bits of an i32
  //            for everything except ATOMIC_SWAPW
  // Operand 2: how many bits to rotate the i32 left to bring the first
  //            operand into the high bits
  // Operand 3: the negative of operand 2, for rotating the other way
  // Operand 4: the width of the field in bits (8 or 16)
  ATOMIC_SWAPW = ISD::FIRST_TARGET_MEMORY_OPCODE,
  ATOMIC_LOADW_ADD,
  ATOMIC_LOADW_SUB,
  ATOMIC_LOADW_AND,
  ATOMIC_LOADW_OR,
  ATOMIC_LOADW_XOR,
  ATOMIC_LOADW_NAND,
  ATOMIC_LOADW_MIN,
  ATOMIC_LOADW_MAX,
  ATOMIC_LOADW_UMIN,
  ATOMIC_LOADW_UMAX,

  // A wrapper around the inner loop of an ATOMIC_CMP_SWAP.
  //
  // Operand 0: the address of the containing 32-bit-aligned field
  // Operand 1: the compare value, in the low bits of an i32
  // Operand 2: the swap value, in the low bits of an i32
  // Operand 3: how many bits to rotate the i32 left to bring the first
  //            operand into the high bits
  // Operand 4: the negative of operand 2, for rotating the other way
  // Operand 5: the width of the field in bits (8 or 16)
  ATOMIC_CMP_SWAPW,

  // Atomic compare-and-swap returning CC value.
  // Val, CC, OUTCHAIN = ATOMIC_CMP_SWAP(INCHAIN, ptr, cmp, swap)
  ATOMIC_CMP_SWAP,

  // 128-bit atomic load.
  // Val, OUTCHAIN = ATOMIC_LOAD_128(INCHAIN, ptr)
  ATOMIC_LOAD_128,

  // 128-bit atomic store.
  // OUTCHAIN = ATOMIC_STORE_128(INCHAIN, val, ptr)
  ATOMIC_STORE_128,

  // 128-bit atomic compare-and-swap.
  // Val, CC, OUTCHAIN = ATOMIC_CMP_SWAP(INCHAIN, ptr, cmp, swap)
  ATOMIC_CMP_SWAP_128,

  // Byte swapping load/store.  Same operands as regular load/store.
  LRV, STRV,

  // Element swapping load/store.  Same operands as regular load/store.
  VLER, VSTER,

  // Use STORE CLOCK FAST to store current TOD clock value.
  STCKF,

  // Prefetch from the second operand using the 4-bit control code in
  // the first operand.  The code is 1 for a load prefetch and 2 for
  // a store prefetch.
  PREFETCH
};

// Return true if OPCODE is some kind of PC-relative address.
inline bool isPCREL(unsigned Opcode) {
  return Opcode == PCREL_WRAPPER || Opcode == PCREL_OFFSET;
}
} // end namespace SystemZISD

namespace SystemZICMP {
// Describes whether an integer comparison needs to be signed or unsigned,
// or whether either type is OK.
enum {
  Any,
  UnsignedOnly,
  SignedOnly
};
} // end namespace SystemZICMP

class SystemZSubtarget;

class SystemZTargetLowering : public TargetLowering {
public:
  explicit SystemZTargetLowering(const TargetMachine &TM,
                                 const SystemZSubtarget &STI);

  bool useSoftFloat() const override;

  // Override TargetLowering.
  MVT getScalarShiftAmountTy(const DataLayout &, EVT) const override {
    return MVT::i32;
  }
  MVT getVectorIdxTy(const DataLayout &DL) const override {
    // Only the lower 12 bits of an element index are used, so we don't
    // want to clobber the upper 32 bits of a GPR unnecessarily.
    return MVT::i32;
  }
  TargetLoweringBase::LegalizeTypeAction getPreferredVectorAction(MVT VT)
    const override {
    // Widen subvectors to the full width rather than promoting integer
    // elements.  This is better because:
    //
    // (a) it means that we can handle the ABI for passing and returning
    //     sub-128 vectors without having to handle them as legal types.
    //
    // (b) we don't have instructions to extend on load and truncate on store,
    //     so promoting the integers is less efficient.
    //
    // (c) there are no multiplication instructions for the widest integer
    //     type (v2i64).
    if (VT.getScalarSizeInBits() % 8 == 0)
      return TypeWidenVector;
    return TargetLoweringBase::getPreferredVectorAction(VT);
  }
  unsigned
  getNumRegisters(LLVMContext &Context, EVT VT,
                  std::optional<MVT> RegisterVT) const override {
    // i128 inline assembly operand.
    if (VT == MVT::i128 && RegisterVT && *RegisterVT == MVT::Untyped)
      return 1;
    return TargetLowering::getNumRegisters(Context, VT);
  }
  MVT getRegisterTypeForCallingConv(LLVMContext &Context, CallingConv::ID CC,
                                    EVT VT) const override {
    // 128-bit single-element vector types are passed like other vectors,
    // not like their element type.
    if (VT.isVector() && VT.getSizeInBits() == 128 &&
        VT.getVectorNumElements() == 1)
      return MVT::v16i8;
    return TargetLowering::getRegisterTypeForCallingConv(Context, CC, VT);
  }
  bool isCheapToSpeculateCtlz(Type *) const override { return true; }
  bool isCheapToSpeculateCttz(Type *) const override { return true; }
  bool preferZeroCompareBranch() const override { return true; }
  bool isMaskAndCmp0FoldingBeneficial(const Instruction &AndI) const override {
    ConstantInt* Mask = dyn_cast<ConstantInt>(AndI.getOperand(1));
    return Mask && Mask->getValue().isIntN(16);
  }
  bool convertSetCCLogicToBitwiseLogic(EVT VT) const override {
    return VT.isScalarInteger();
  }
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &,
                         EVT) const override;
  bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                  EVT VT) const override;
  bool isFPImmLegal(const APFloat &Imm, EVT VT,
                    bool ForCodeSize) const override;
  bool ShouldShrinkFPConstant(EVT VT) const override {
    // Do not shrink 64-bit FP constpool entries since LDEB is slower than
    // LD, and having the full constant in memory enables reg/mem opcodes.
    return VT != MVT::f64;
  }
  bool hasInlineStackProbe(const MachineFunction &MF) const override;
  AtomicExpansionKind shouldCastAtomicLoadInIR(LoadInst *LI) const override;
  AtomicExpansionKind shouldCastAtomicStoreInIR(StoreInst *SI) const override;
  AtomicExpansionKind
  shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const override;
  bool isLegalICmpImmediate(int64_t Imm) const override;
  bool isLegalAddImmediate(int64_t Imm) const override;
  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AS,
                             Instruction *I = nullptr) const override;
  bool allowsMisalignedMemoryAccesses(EVT VT, unsigned AS, Align Alignment,
                                      MachineMemOperand::Flags Flags,
                                      unsigned *Fast) const override;
  bool
  findOptimalMemOpLowering(std::vector<EVT> &MemOps, unsigned Limit,
                           const MemOp &Op, unsigned DstAS, unsigned SrcAS,
                           const AttributeList &FuncAttributes) const override;
  EVT getOptimalMemOpType(const MemOp &Op,
                          const AttributeList &FuncAttributes) const override;
  bool isTruncateFree(Type *, Type *) const override;
  bool isTruncateFree(EVT, EVT) const override;

  bool shouldFormOverflowOp(unsigned Opcode, EVT VT,
                            bool MathUsed) const override {
    // Form add and sub with overflow intrinsics regardless of any extra
    // users of the math result.
    return VT == MVT::i32 || VT == MVT::i64;
  }

  bool shouldConsiderGEPOffsetSplit() const override { return true; }

  bool shouldExpandCmpUsingSelects() const override { return true; }

  const char *getTargetNodeName(unsigned Opcode) const override;
  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;
  TargetLowering::ConstraintType
  getConstraintType(StringRef Constraint) const override;
  TargetLowering::ConstraintWeight
    getSingleConstraintMatchWeight(AsmOperandInfo &info,
                                   const char *constraint) const override;
  void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                    std::vector<SDValue> &Ops,
                                    SelectionDAG &DAG) const override;

  InlineAsm::ConstraintCode
  getInlineAsmMemConstraint(StringRef ConstraintCode) const override {
    if (ConstraintCode.size() == 1) {
      switch(ConstraintCode[0]) {
      default:
        break;
      case 'o':
        return InlineAsm::ConstraintCode::o;
      case 'Q':
        return InlineAsm::ConstraintCode::Q;
      case 'R':
        return InlineAsm::ConstraintCode::R;
      case 'S':
        return InlineAsm::ConstraintCode::S;
      case 'T':
        return InlineAsm::ConstraintCode::T;
      }
    } else if (ConstraintCode.size() == 2 && ConstraintCode[0] == 'Z') {
      switch (ConstraintCode[1]) {
      default:
        break;
      case 'Q':
        return InlineAsm::ConstraintCode::ZQ;
      case 'R':
        return InlineAsm::ConstraintCode::ZR;
      case 'S':
        return InlineAsm::ConstraintCode::ZS;
      case 'T':
        return InlineAsm::ConstraintCode::ZT;
      }
    }
    return TargetLowering::getInlineAsmMemConstraint(ConstraintCode);
  }

  Register getRegisterByName(const char *RegName, LLT VT,
                             const MachineFunction &MF) const override;

  /// If a physical register, this returns the register that receives the
  /// exception address on entry to an EH pad.
  Register
  getExceptionPointerRegister(const Constant *PersonalityFn) const override;

  /// If a physical register, this returns the register that receives the
  /// exception typeid on entry to a landing pad.
  Register
  getExceptionSelectorRegister(const Constant *PersonalityFn) const override;

  /// Override to support customized stack guard loading.
  bool useLoadStackGuardNode() const override {
    return true;
  }
  void insertSSPDeclarations(Module &M) const override {
  }

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  void LowerOperationWrapper(SDNode *N, SmallVectorImpl<SDValue> &Results,
                             SelectionDAG &DAG) const override;
  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue>&Results,
                          SelectionDAG &DAG) const override;
  const MCPhysReg *getScratchRegisters(CallingConv::ID CC) const override;
  bool allowTruncateForTailCall(Type *, Type *) const override;
  bool mayBeEmittedAsTailCall(const CallInst *CI) const override;
  bool splitValueIntoRegisterParts(
      SelectionDAG & DAG, const SDLoc &DL, SDValue Val, SDValue *Parts,
      unsigned NumParts, MVT PartVT, std::optional<CallingConv::ID> CC)
      const override;
  SDValue joinRegisterPartsIntoValue(
      SelectionDAG & DAG, const SDLoc &DL, const SDValue *Parts,
      unsigned NumParts, MVT PartVT, EVT ValueVT,
      std::optional<CallingConv::ID> CC) const override;
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;
  SDValue LowerCall(CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  std::pair<SDValue, SDValue>
  makeExternalCall(SDValue Chain, SelectionDAG &DAG, const char *CalleeName,
                   EVT RetVT, ArrayRef<SDValue> Ops, CallingConv::ID CallConv,
                   bool IsSigned, SDLoc DL, bool DoesNotReturn,
                   bool IsReturnValueUsed) const;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context) const override;
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;
  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

  /// Determine which of the bits specified in Mask are known to be either
  /// zero or one and return them in the KnownZero/KnownOne bitsets.
  void computeKnownBitsForTargetNode(const SDValue Op,
                                     KnownBits &Known,
                                     const APInt &DemandedElts,
                                     const SelectionDAG &DAG,
                                     unsigned Depth = 0) const override;

  /// Determine the number of bits in the operation that are sign bits.
  unsigned ComputeNumSignBitsForTargetNode(SDValue Op,
                                           const APInt &DemandedElts,
                                           const SelectionDAG &DAG,
                                           unsigned Depth) const override;

  bool isGuaranteedNotToBeUndefOrPoisonForTargetNode(
      SDValue Op, const APInt &DemandedElts, const SelectionDAG &DAG,
      bool PoisonOnly, unsigned Depth) const override;

  ISD::NodeType getExtendForAtomicOps() const override {
    return ISD::ANY_EXTEND;
  }
  ISD::NodeType getExtendForAtomicCmpSwapArg() const override {
    return ISD::ZERO_EXTEND;
  }

  bool supportSwiftError() const override {
    return true;
  }

  unsigned getStackProbeSize(const MachineFunction &MF) const;

private:
  const SystemZSubtarget &Subtarget;

  // Implement LowerOperation for individual opcodes.
  SDValue getVectorCmp(SelectionDAG &DAG, unsigned Opcode,
                       const SDLoc &DL, EVT VT,
                       SDValue CmpOp0, SDValue CmpOp1, SDValue Chain) const;
  SDValue lowerVectorSETCC(SelectionDAG &DAG, const SDLoc &DL,
                           EVT VT, ISD::CondCode CC,
                           SDValue CmpOp0, SDValue CmpOp1,
                           SDValue Chain = SDValue(),
                           bool IsSignaling = false) const;
  SDValue lowerSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSTRICT_FSETCC(SDValue Op, SelectionDAG &DAG,
                             bool IsSignaling) const;
  SDValue lowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerGlobalAddress(GlobalAddressSDNode *Node,
                             SelectionDAG &DAG) const;
  SDValue lowerTLSGetOffset(GlobalAddressSDNode *Node,
                            SelectionDAG &DAG, unsigned Opcode,
                            SDValue GOTOffset) const;
  SDValue lowerThreadPointer(const SDLoc &DL, SelectionDAG &DAG) const;
  SDValue lowerGlobalTLSAddress(GlobalAddressSDNode *Node,
                                SelectionDAG &DAG) const;
  SDValue lowerBlockAddress(BlockAddressSDNode *Node,
                            SelectionDAG &DAG) const;
  SDValue lowerJumpTable(JumpTableSDNode *JT, SelectionDAG &DAG) const;
  SDValue lowerConstantPool(ConstantPoolSDNode *CP, SelectionDAG &DAG) const;
  SDValue lowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVASTART_ELF(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVASTART_XPLINK(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVACOPY(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerDYNAMIC_STACKALLOC_ELF(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerDYNAMIC_STACKALLOC_XPLINK(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerGET_DYNAMIC_AREA_OFFSET(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSMUL_LOHI(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerUMUL_LOHI(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSDIVREM(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerUDIVREM(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerXALUO(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerUADDSUBO_CARRY(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerBITCAST(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerCTPOP(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVECREDUCE_ADD(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerATOMIC_FENCE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerATOMIC_LDST_I128(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerATOMIC_LOAD_OP(SDValue Op, SelectionDAG &DAG,
                              unsigned Opcode) const;
  SDValue lowerATOMIC_LOAD_SUB(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerATOMIC_CMP_SWAP(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSTACKSAVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSTACKRESTORE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerPREFETCH(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerINTRINSIC_W_CHAIN(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG) const;
  bool isVectorElementLoad(SDValue Op) const;
  SDValue buildVector(SelectionDAG &DAG, const SDLoc &DL, EVT VT,
                      SmallVectorImpl<SDValue> &Elems) const;
  SDValue lowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVECTOR_SHUFFLE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSCALAR_TO_VECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerINSERT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerEXTRACT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSIGN_EXTEND_VECTOR_INREG(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerZERO_EXTEND_VECTOR_INREG(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerShift(SDValue Op, SelectionDAG &DAG, unsigned ByScalar) const;
  SDValue lowerIS_FPCLASS(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerGET_ROUNDING(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerREADCYCLECOUNTER(SDValue Op, SelectionDAG &DAG) const;

  bool canTreatAsByteVector(EVT VT) const;
  SDValue combineExtract(const SDLoc &DL, EVT ElemVT, EVT VecVT, SDValue OrigOp,
                         unsigned Index, DAGCombinerInfo &DCI,
                         bool Force) const;
  SDValue combineTruncateExtract(const SDLoc &DL, EVT TruncVT, SDValue Op,
                                 DAGCombinerInfo &DCI) const;
  SDValue combineZERO_EXTEND(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineSIGN_EXTEND(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineSIGN_EXTEND_INREG(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineMERGE(SDNode *N, DAGCombinerInfo &DCI) const;
  bool canLoadStoreByteSwapped(EVT VT) const;
  SDValue combineLOAD(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineSTORE(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineVECTOR_SHUFFLE(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineEXTRACT_VECTOR_ELT(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineJOIN_DWORDS(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineFP_ROUND(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineFP_EXTEND(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineINT_TO_FP(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineBSWAP(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineBR_CCMASK(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineSELECT_CCMASK(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineGET_CCMASK(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineIntDIVREM(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue combineINTRINSIC(SDNode *N, DAGCombinerInfo &DCI) const;

  SDValue unwrapAddress(SDValue N) const override;

  // If the last instruction before MBBI in MBB was some form of COMPARE,
  // try to replace it with a COMPARE AND BRANCH just before MBBI.
  // CCMask and Target are the BRC-like operands for the branch.
  // Return true if the change was made.
  bool convertPrevCompareToBranch(MachineBasicBlock *MBB,
                                  MachineBasicBlock::iterator MBBI,
                                  unsigned CCMask,
                                  MachineBasicBlock *Target) const;

  // Implement EmitInstrWithCustomInserter for individual operation types.
  MachineBasicBlock *emitAdjCallStack(MachineInstr &MI,
                                      MachineBasicBlock *BB) const;
  MachineBasicBlock *emitSelect(MachineInstr &MI, MachineBasicBlock *BB) const;
  MachineBasicBlock *emitCondStore(MachineInstr &MI, MachineBasicBlock *BB,
                                   unsigned StoreOpcode, unsigned STOCOpcode,
                                   bool Invert) const;
  MachineBasicBlock *emitICmp128Hi(MachineInstr &MI, MachineBasicBlock *BB,
                                   bool Unsigned) const;
  MachineBasicBlock *emitPair128(MachineInstr &MI,
                                 MachineBasicBlock *MBB) const;
  MachineBasicBlock *emitExt128(MachineInstr &MI, MachineBasicBlock *MBB,
                                bool ClearEven) const;
  MachineBasicBlock *emitAtomicLoadBinary(MachineInstr &MI,
                                          MachineBasicBlock *BB,
                                          unsigned BinOpcode,
                                          bool Invert = false) const;
  MachineBasicBlock *emitAtomicLoadMinMax(MachineInstr &MI,
                                          MachineBasicBlock *MBB,
                                          unsigned CompareOpcode,
                                          unsigned KeepOldMask) const;
  MachineBasicBlock *emitAtomicCmpSwapW(MachineInstr &MI,
                                        MachineBasicBlock *BB) const;
  MachineBasicBlock *emitMemMemWrapper(MachineInstr &MI, MachineBasicBlock *BB,
                                       unsigned Opcode,
                                       bool IsMemset = false) const;
  MachineBasicBlock *emitStringWrapper(MachineInstr &MI, MachineBasicBlock *BB,
                                       unsigned Opcode) const;
  MachineBasicBlock *emitTransactionBegin(MachineInstr &MI,
                                          MachineBasicBlock *MBB,
                                          unsigned Opcode, bool NoFloat) const;
  MachineBasicBlock *emitLoadAndTestCmp0(MachineInstr &MI,
                                         MachineBasicBlock *MBB,
                                         unsigned Opcode) const;
  MachineBasicBlock *emitProbedAlloca(MachineInstr &MI,
                                      MachineBasicBlock *MBB) const;

  SDValue getBackchainAddress(SDValue SP, SelectionDAG &DAG) const;

  MachineMemOperand::Flags
  getTargetMMOFlags(const Instruction &I) const override;
  const TargetRegisterClass *getRepRegClassFor(MVT VT) const override;
};

struct SystemZVectorConstantInfo {
private:
  APInt IntBits;             // The 128 bits as an integer.
  APInt SplatBits;           // Smallest splat value.
  APInt SplatUndef;          // Bits correspoding to undef operands of the BVN.
  unsigned SplatBitSize = 0;
  bool isFP128 = false;
public:
  unsigned Opcode = 0;
  SmallVector<unsigned, 2> OpVals;
  MVT VecVT;
  SystemZVectorConstantInfo(APInt IntImm);
  SystemZVectorConstantInfo(APFloat FPImm)
      : SystemZVectorConstantInfo(FPImm.bitcastToAPInt()) {
    isFP128 = (&FPImm.getSemantics() == &APFloat::IEEEquad());
  }
  SystemZVectorConstantInfo(BuildVectorSDNode *BVN);
  bool isVectorConstantLegal(const SystemZSubtarget &Subtarget);
};

} // end namespace llvm

#endif
