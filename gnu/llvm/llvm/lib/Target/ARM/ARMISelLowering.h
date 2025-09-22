//===- ARMISelLowering.h - ARM DAG Lowering Interface -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that ARM uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMISELLOWERING_H
#define LLVM_LIB_TARGET_ARM_ARMISELLOWERING_H

#include "MCTargetDesc/ARMBaseInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Support/CodeGen.h"
#include <optional>
#include <utility>

namespace llvm {

class ARMSubtarget;
class DataLayout;
class FastISel;
class FunctionLoweringInfo;
class GlobalValue;
class InstrItineraryData;
class Instruction;
class IRBuilderBase;
class MachineBasicBlock;
class MachineInstr;
class SelectionDAG;
class TargetLibraryInfo;
class TargetMachine;
class TargetRegisterInfo;
class VectorType;

  namespace ARMISD {

  // ARM Specific DAG Nodes
  enum NodeType : unsigned {
    // Start the numbering where the builtin ops and target ops leave off.
    FIRST_NUMBER = ISD::BUILTIN_OP_END,

    Wrapper,    // Wrapper - A wrapper node for TargetConstantPool,
                // TargetExternalSymbol, and TargetGlobalAddress.
    WrapperPIC, // WrapperPIC - A wrapper node for TargetGlobalAddress in
                // PIC mode.
    WrapperJT,  // WrapperJT - A wrapper node for TargetJumpTable

    // Add pseudo op to model memcpy for struct byval.
    COPY_STRUCT_BYVAL,

    CALL,        // Function call.
    CALL_PRED,   // Function call that's predicable.
    CALL_NOLINK, // Function call with branch not branch-and-link.
    tSECALL,     // CMSE non-secure function call.
    t2CALL_BTI,  // Thumb function call followed by BTI instruction.
    BRCOND,      // Conditional branch.
    BR_JT,       // Jumptable branch.
    BR2_JT,      // Jumptable branch (2 level - jumptable entry is a jump).
    RET_GLUE,    // Return with a flag operand.
    SERET_GLUE,  // CMSE Entry function return with a flag operand.
    INTRET_GLUE, // Interrupt return with an LR-offset and a flag operand.

    PIC_ADD, // Add with a PC operand and a PIC label.

    ASRL, // MVE long arithmetic shift right.
    LSRL, // MVE long shift right.
    LSLL, // MVE long shift left.

    CMP,      // ARM compare instructions.
    CMN,      // ARM CMN instructions.
    CMPZ,     // ARM compare that sets only Z flag.
    CMPFP,    // ARM VFP compare instruction, sets FPSCR.
    CMPFPE,   // ARM VFP signalling compare instruction, sets FPSCR.
    CMPFPw0,  // ARM VFP compare against zero instruction, sets FPSCR.
    CMPFPEw0, // ARM VFP signalling compare against zero instruction, sets
              // FPSCR.
    FMSTAT,   // ARM fmstat instruction.

    CMOV, // ARM conditional move instructions.

    SSAT, // Signed saturation
    USAT, // Unsigned saturation

    BCC_i64,

    SRL_GLUE, // V,Flag = srl_flag X -> srl X, 1 + save carry out.
    SRA_GLUE, // V,Flag = sra_flag X -> sra X, 1 + save carry out.
    RRX,      // V = RRX X, Flag     -> srl X, 1 + shift in carry flag.

    ADDC, // Add with carry
    ADDE, // Add using carry
    SUBC, // Sub with carry
    SUBE, // Sub using carry
    LSLS, // Shift left producing carry

    VMOVRRD, // double to two gprs.
    VMOVDRR, // Two gprs to double.
    VMOVSR,  // move gpr to single, used for f32 literal constructed in a gpr

    EH_SJLJ_SETJMP,         // SjLj exception handling setjmp.
    EH_SJLJ_LONGJMP,        // SjLj exception handling longjmp.
    EH_SJLJ_SETUP_DISPATCH, // SjLj exception handling setup_dispatch.

    TC_RETURN, // Tail call return pseudo.

    THREAD_POINTER,

    DYN_ALLOC, // Dynamic allocation on the stack.

    MEMBARRIER_MCR, // Memory barrier (MCR)

    PRELOAD, // Preload

    WIN__CHKSTK, // Windows' __chkstk call to do stack probing.
    WIN__DBZCHK, // Windows' divide by zero check

    WLS, // Low-overhead loops, While Loop Start branch. See t2WhileLoopStart
    WLSSETUP, // Setup for the iteration count of a WLS. See t2WhileLoopSetup.
    LOOP_DEC, // Really a part of LE, performs the sub
    LE,       // Low-overhead loops, Loop End

    PREDICATE_CAST,  // Predicate cast for MVE i1 types
    VECTOR_REG_CAST, // Reinterpret the current contents of a vector register

    MVESEXT,  // Legalization aids for extending a vector into two/four vectors.
    MVEZEXT,  //  or truncating two/four vectors into one. Eventually becomes
    MVETRUNC, //  stack store/load sequence, if not optimized to anything else.

    VCMP,  // Vector compare.
    VCMPZ, // Vector compare to zero.
    VTST,  // Vector test bits.

    // Vector shift by vector
    VSHLs, // ...left/right by signed
    VSHLu, // ...left/right by unsigned

    // Vector shift by immediate:
    VSHLIMM,  // ...left
    VSHRsIMM, // ...right (signed)
    VSHRuIMM, // ...right (unsigned)

    // Vector rounding shift by immediate:
    VRSHRsIMM, // ...right (signed)
    VRSHRuIMM, // ...right (unsigned)
    VRSHRNIMM, // ...right narrow

    // Vector saturating shift by immediate:
    VQSHLsIMM,   // ...left (signed)
    VQSHLuIMM,   // ...left (unsigned)
    VQSHLsuIMM,  // ...left (signed to unsigned)
    VQSHRNsIMM,  // ...right narrow (signed)
    VQSHRNuIMM,  // ...right narrow (unsigned)
    VQSHRNsuIMM, // ...right narrow (signed to unsigned)

    // Vector saturating rounding shift by immediate:
    VQRSHRNsIMM,  // ...right narrow (signed)
    VQRSHRNuIMM,  // ...right narrow (unsigned)
    VQRSHRNsuIMM, // ...right narrow (signed to unsigned)

    // Vector shift and insert:
    VSLIIMM, // ...left
    VSRIIMM, // ...right

    // Vector get lane (VMOV scalar to ARM core register)
    // (These are used for 8- and 16-bit element types only.)
    VGETLANEu, // zero-extend vector extract element
    VGETLANEs, // sign-extend vector extract element

    // Vector move immediate and move negated immediate:
    VMOVIMM,
    VMVNIMM,

    // Vector move f32 immediate:
    VMOVFPIMM,

    // Move H <-> R, clearing top 16 bits
    VMOVrh,
    VMOVhr,

    // Vector duplicate:
    VDUP,
    VDUPLANE,

    // Vector shuffles:
    VEXT,   // extract
    VREV64, // reverse elements within 64-bit doublewords
    VREV32, // reverse elements within 32-bit words
    VREV16, // reverse elements within 16-bit halfwords
    VZIP,   // zip (interleave)
    VUZP,   // unzip (deinterleave)
    VTRN,   // transpose
    VTBL1,  // 1-register shuffle with mask
    VTBL2,  // 2-register shuffle with mask
    VMOVN,  // MVE vmovn

    // MVE Saturating truncates
    VQMOVNs, // Vector (V) Saturating (Q) Move and Narrow (N), signed (s)
    VQMOVNu, // Vector (V) Saturating (Q) Move and Narrow (N), unsigned (u)

    // MVE float <> half converts
    VCVTN, // MVE vcvt f32 -> f16, truncating into either the bottom or top
           // lanes
    VCVTL, // MVE vcvt f16 -> f32, extending from either the bottom or top lanes

    // MVE VIDUP instruction, taking a start value and increment.
    VIDUP,

    // Vector multiply long:
    VMULLs, // ...signed
    VMULLu, // ...unsigned

    VQDMULH, // MVE vqdmulh instruction

    // MVE reductions
    VADDVs,  // sign- or zero-extend the elements of a vector to i32,
    VADDVu,  //   add them all together, and return an i32 of their sum
    VADDVps, // Same as VADDV[su] but with a v4i1 predicate mask
    VADDVpu,
    VADDLVs,  // sign- or zero-extend elements to i64 and sum, returning
    VADDLVu,  //   the low and high 32-bit halves of the sum
    VADDLVAs, // Same as VADDLV[su] but also add an input accumulator
    VADDLVAu, //   provided as low and high halves
    VADDLVps, // Same as VADDLV[su] but with a v4i1 predicate mask
    VADDLVpu,
    VADDLVAps, // Same as VADDLVp[su] but with a v4i1 predicate mask
    VADDLVApu,
    VMLAVs, // sign- or zero-extend the elements of two vectors to i32, multiply
    VMLAVu, //   them and add the results together, returning an i32 of the sum
    VMLAVps, // Same as VMLAV[su] with a v4i1 predicate mask
    VMLAVpu,
    VMLALVs,  // Same as VMLAV but with i64, returning the low and
    VMLALVu,  //   high 32-bit halves of the sum
    VMLALVps, // Same as VMLALV[su] with a v4i1 predicate mask
    VMLALVpu,
    VMLALVAs,  // Same as VMLALV but also add an input accumulator
    VMLALVAu,  //   provided as low and high halves
    VMLALVAps, // Same as VMLALVA[su] with a v4i1 predicate mask
    VMLALVApu,
    VMINVu, // Find minimum unsigned value of a vector and register
    VMINVs, // Find minimum signed value of a vector and register
    VMAXVu, // Find maximum unsigned value of a vector and register
    VMAXVs, // Find maximum signed value of a vector and register

    SMULWB,  // Signed multiply word by half word, bottom
    SMULWT,  // Signed multiply word by half word, top
    UMLAL,   // 64bit Unsigned Accumulate Multiply
    SMLAL,   // 64bit Signed Accumulate Multiply
    UMAAL,   // 64-bit Unsigned Accumulate Accumulate Multiply
    SMLALBB, // 64-bit signed accumulate multiply bottom, bottom 16
    SMLALBT, // 64-bit signed accumulate multiply bottom, top 16
    SMLALTB, // 64-bit signed accumulate multiply top, bottom 16
    SMLALTT, // 64-bit signed accumulate multiply top, top 16
    SMLALD,  // Signed multiply accumulate long dual
    SMLALDX, // Signed multiply accumulate long dual exchange
    SMLSLD,  // Signed multiply subtract long dual
    SMLSLDX, // Signed multiply subtract long dual exchange
    SMMLAR,  // Signed multiply long, round and add
    SMMLSR,  // Signed multiply long, subtract and round

    // Single Lane QADD8 and QADD16. Only the bottom lane. That's what the b
    // stands for.
    QADD8b,
    QSUB8b,
    QADD16b,
    QSUB16b,
    UQADD8b,
    UQSUB8b,
    UQADD16b,
    UQSUB16b,

    // Operands of the standard BUILD_VECTOR node are not legalized, which
    // is fine if BUILD_VECTORs are always lowered to shuffles or other
    // operations, but for ARM some BUILD_VECTORs are legal as-is and their
    // operands need to be legalized.  Define an ARM-specific version of
    // BUILD_VECTOR for this purpose.
    BUILD_VECTOR,

    // Bit-field insert
    BFI,

    // Vector OR with immediate
    VORRIMM,
    // Vector AND with NOT of immediate
    VBICIMM,

    // Pseudo vector bitwise select
    VBSP,

    // Pseudo-instruction representing a memory copy using ldm/stm
    // instructions.
    MEMCPY,

    // Pseudo-instruction representing a memory copy using a tail predicated
    // loop
    MEMCPYLOOP,
    // Pseudo-instruction representing a memset using a tail predicated
    // loop
    MEMSETLOOP,

    // V8.1MMainline condition select
    CSINV, // Conditional select invert.
    CSNEG, // Conditional select negate.
    CSINC, // Conditional select increment.

    // Vector load N-element structure to all lanes:
    VLD1DUP = ISD::FIRST_TARGET_MEMORY_OPCODE,
    VLD2DUP,
    VLD3DUP,
    VLD4DUP,

    // NEON loads with post-increment base updates:
    VLD1_UPD,
    VLD2_UPD,
    VLD3_UPD,
    VLD4_UPD,
    VLD2LN_UPD,
    VLD3LN_UPD,
    VLD4LN_UPD,
    VLD1DUP_UPD,
    VLD2DUP_UPD,
    VLD3DUP_UPD,
    VLD4DUP_UPD,
    VLD1x2_UPD,
    VLD1x3_UPD,
    VLD1x4_UPD,

    // NEON stores with post-increment base updates:
    VST1_UPD,
    VST2_UPD,
    VST3_UPD,
    VST4_UPD,
    VST2LN_UPD,
    VST3LN_UPD,
    VST4LN_UPD,
    VST1x2_UPD,
    VST1x3_UPD,
    VST1x4_UPD,

    // Load/Store of dual registers
    LDRD,
    STRD
  };

  } // end namespace ARMISD

  namespace ARM {
  /// Possible values of current rounding mode, which is specified in bits
  /// 23:22 of FPSCR.
  enum Rounding {
    RN = 0,    // Round to Nearest
    RP = 1,    // Round towards Plus infinity
    RM = 2,    // Round towards Minus infinity
    RZ = 3,    // Round towards Zero
    rmMask = 3 // Bit mask selecting rounding mode
  };

  // Bit position of rounding mode bits in FPSCR.
  const unsigned RoundingBitsPos = 22;

  // Bits of floating-point status. These are NZCV flags, QC bit and cumulative
  // FP exception bits.
  const unsigned FPStatusBits = 0xf800009f;

  // Some bits in the FPSCR are not yet defined.  They must be preserved when
  // modifying the contents.
  const unsigned FPReservedBits = 0x00006060;
  } // namespace ARM

  /// Define some predicates that are used for node matching.
  namespace ARM {

    bool isBitFieldInvertedMask(unsigned v);

  } // end namespace ARM

  //===--------------------------------------------------------------------===//
  //  ARMTargetLowering - ARM Implementation of the TargetLowering interface

  class ARMTargetLowering : public TargetLowering {
  public:
    explicit ARMTargetLowering(const TargetMachine &TM,
                               const ARMSubtarget &STI);

    unsigned getJumpTableEncoding() const override;
    bool useSoftFloat() const override;

    SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

    /// ReplaceNodeResults - Replace the results of node with an illegal result
    /// type with new values built out of custom code.
    void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue>&Results,
                            SelectionDAG &DAG) const override;

    const char *getTargetNodeName(unsigned Opcode) const override;

    bool isSelectSupported(SelectSupportKind Kind) const override {
      // ARM does not support scalar condition selects on vectors.
      return (Kind != ScalarCondVectorVal);
    }

    bool isReadOnly(const GlobalValue *GV) const;

    /// getSetCCResultType - Return the value type to use for ISD::SETCC.
    EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                           EVT VT) const override;

    MachineBasicBlock *
    EmitInstrWithCustomInserter(MachineInstr &MI,
                                MachineBasicBlock *MBB) const override;

    void AdjustInstrPostInstrSelection(MachineInstr &MI,
                                       SDNode *Node) const override;

    SDValue PerformCMOVCombine(SDNode *N, SelectionDAG &DAG) const;
    SDValue PerformBRCONDCombine(SDNode *N, SelectionDAG &DAG) const;
    SDValue PerformCMOVToBFICombine(SDNode *N, SelectionDAG &DAG) const;
    SDValue PerformIntrinsicCombine(SDNode *N, DAGCombinerInfo &DCI) const;
    SDValue PerformMVEExtCombine(SDNode *N, DAGCombinerInfo &DCI) const;
    SDValue PerformMVETruncCombine(SDNode *N, DAGCombinerInfo &DCI) const;
    SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

    bool SimplifyDemandedBitsForTargetNode(SDValue Op,
                                           const APInt &OriginalDemandedBits,
                                           const APInt &OriginalDemandedElts,
                                           KnownBits &Known,
                                           TargetLoweringOpt &TLO,
                                           unsigned Depth) const override;

    bool isDesirableToTransformToIntegerOp(unsigned Opc, EVT VT) const override;

    /// allowsMisalignedMemoryAccesses - Returns true if the target allows
    /// unaligned memory accesses of the specified type. Returns whether it
    /// is "fast" by reference in the second argument.
    bool allowsMisalignedMemoryAccesses(EVT VT, unsigned AddrSpace,
                                        Align Alignment,
                                        MachineMemOperand::Flags Flags,
                                        unsigned *Fast) const override;

    EVT getOptimalMemOpType(const MemOp &Op,
                            const AttributeList &FuncAttributes) const override;

    bool isTruncateFree(Type *SrcTy, Type *DstTy) const override;
    bool isTruncateFree(EVT SrcVT, EVT DstVT) const override;
    bool isZExtFree(SDValue Val, EVT VT2) const override;
    bool shouldSinkOperands(Instruction *I,
                            SmallVectorImpl<Use *> &Ops) const override;
    Type* shouldConvertSplatType(ShuffleVectorInst* SVI) const override;

    bool isFNegFree(EVT VT) const override;

    bool isVectorLoadExtDesirable(SDValue ExtVal) const override;

    bool allowTruncateForTailCall(Type *Ty1, Type *Ty2) const override;


    /// isLegalAddressingMode - Return true if the addressing mode represented
    /// by AM is legal for this target, for a load/store of the specified type.
    bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM,
                               Type *Ty, unsigned AS,
                               Instruction *I = nullptr) const override;

    bool isLegalT2ScaledAddressingMode(const AddrMode &AM, EVT VT) const;

    /// Returns true if the addressing mode representing by AM is legal
    /// for the Thumb1 target, for a load/store of the specified type.
    bool isLegalT1ScaledAddressingMode(const AddrMode &AM, EVT VT) const;

    /// isLegalICmpImmediate - Return true if the specified immediate is legal
    /// icmp immediate, that is the target has icmp instructions which can
    /// compare a register against the immediate without having to materialize
    /// the immediate into a register.
    bool isLegalICmpImmediate(int64_t Imm) const override;

    /// isLegalAddImmediate - Return true if the specified immediate is legal
    /// add immediate, that is the target has add instructions which can
    /// add a register and the immediate without having to materialize
    /// the immediate into a register.
    bool isLegalAddImmediate(int64_t Imm) const override;

    /// getPreIndexedAddressParts - returns true by value, base pointer and
    /// offset pointer and addressing mode by reference if the node's address
    /// can be legally represented as pre-indexed load / store address.
    bool getPreIndexedAddressParts(SDNode *N, SDValue &Base, SDValue &Offset,
                                   ISD::MemIndexedMode &AM,
                                   SelectionDAG &DAG) const override;

    /// getPostIndexedAddressParts - returns true by value, base pointer and
    /// offset pointer and addressing mode by reference if this node can be
    /// combined with a load / store to form a post-indexed load / store.
    bool getPostIndexedAddressParts(SDNode *N, SDNode *Op, SDValue &Base,
                                    SDValue &Offset, ISD::MemIndexedMode &AM,
                                    SelectionDAG &DAG) const override;

    void computeKnownBitsForTargetNode(const SDValue Op, KnownBits &Known,
                                       const APInt &DemandedElts,
                                       const SelectionDAG &DAG,
                                       unsigned Depth) const override;

    bool targetShrinkDemandedConstant(SDValue Op, const APInt &DemandedBits,
                                      const APInt &DemandedElts,
                                      TargetLoweringOpt &TLO) const override;

    bool ExpandInlineAsm(CallInst *CI) const override;

    ConstraintType getConstraintType(StringRef Constraint) const override;

    /// Examine constraint string and operand type and determine a weight value.
    /// The operand object must already have been set up with the operand type.
    ConstraintWeight getSingleConstraintMatchWeight(
      AsmOperandInfo &info, const char *constraint) const override;

    std::pair<unsigned, const TargetRegisterClass *>
    getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                 StringRef Constraint, MVT VT) const override;

    const char *LowerXConstraint(EVT ConstraintVT) const override;

    /// LowerAsmOperandForConstraint - Lower the specified operand into the Ops
    /// vector.  If it is invalid, don't add anything to Ops. If hasMemory is
    /// true it means one of the asm constraint of the inline asm instruction
    /// being processed is 'm'.
    void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                      std::vector<SDValue> &Ops,
                                      SelectionDAG &DAG) const override;

    InlineAsm::ConstraintCode
    getInlineAsmMemConstraint(StringRef ConstraintCode) const override {
      if (ConstraintCode == "Q")
        return InlineAsm::ConstraintCode::Q;
      if (ConstraintCode.size() == 2) {
        if (ConstraintCode[0] == 'U') {
          switch(ConstraintCode[1]) {
          default:
            break;
          case 'm':
            return InlineAsm::ConstraintCode::Um;
          case 'n':
            return InlineAsm::ConstraintCode::Un;
          case 'q':
            return InlineAsm::ConstraintCode::Uq;
          case 's':
            return InlineAsm::ConstraintCode::Us;
          case 't':
            return InlineAsm::ConstraintCode::Ut;
          case 'v':
            return InlineAsm::ConstraintCode::Uv;
          case 'y':
            return InlineAsm::ConstraintCode::Uy;
          }
        }
      }
      return TargetLowering::getInlineAsmMemConstraint(ConstraintCode);
    }

    const ARMSubtarget* getSubtarget() const {
      return Subtarget;
    }

    /// getRegClassFor - Return the register class that should be used for the
    /// specified value type.
    const TargetRegisterClass *
    getRegClassFor(MVT VT, bool isDivergent = false) const override;

    bool shouldAlignPointerArgs(CallInst *CI, unsigned &MinSize,
                                Align &PrefAlign) const override;

    /// createFastISel - This method returns a target specific FastISel object,
    /// or null if the target does not support "fast" ISel.
    FastISel *createFastISel(FunctionLoweringInfo &funcInfo,
                             const TargetLibraryInfo *libInfo) const override;

    Sched::Preference getSchedulingPreference(SDNode *N) const override;

    bool preferZeroCompareBranch() const override { return true; }

    bool isMaskAndCmp0FoldingBeneficial(const Instruction &AndI) const override;

    bool
    isShuffleMaskLegal(ArrayRef<int> M, EVT VT) const override;
    bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;

    /// isFPImmLegal - Returns true if the target can instruction select the
    /// specified FP immediate natively. If false, the legalizer will
    /// materialize the FP immediate as a load from a constant pool.
    bool isFPImmLegal(const APFloat &Imm, EVT VT,
                      bool ForCodeSize = false) const override;

    bool getTgtMemIntrinsic(IntrinsicInfo &Info,
                            const CallInst &I,
                            MachineFunction &MF,
                            unsigned Intrinsic) const override;

    /// Returns true if it is beneficial to convert a load of a constant
    /// to just the constant itself.
    bool shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                           Type *Ty) const override;

    /// Return true if EXTRACT_SUBVECTOR is cheap for this result type
    /// with this index.
    bool isExtractSubvectorCheap(EVT ResVT, EVT SrcVT,
                                 unsigned Index) const override;

    bool shouldFormOverflowOp(unsigned Opcode, EVT VT,
                              bool MathUsed) const override {
      // Using overflow ops for overflow checks only should beneficial on ARM.
      return TargetLowering::shouldFormOverflowOp(Opcode, VT, true);
    }

    bool shouldReassociateReduction(unsigned Opc, EVT VT) const override {
      return Opc != ISD::VECREDUCE_ADD;
    }

    /// Returns true if an argument of type Ty needs to be passed in a
    /// contiguous block of registers in calling convention CallConv.
    bool functionArgumentNeedsConsecutiveRegisters(
        Type *Ty, CallingConv::ID CallConv, bool isVarArg,
        const DataLayout &DL) const override;

    /// If a physical register, this returns the register that receives the
    /// exception address on entry to an EH pad.
    Register
    getExceptionPointerRegister(const Constant *PersonalityFn) const override;

    /// If a physical register, this returns the register that receives the
    /// exception typeid on entry to a landing pad.
    Register
    getExceptionSelectorRegister(const Constant *PersonalityFn) const override;

    Instruction *makeDMB(IRBuilderBase &Builder, ARM_MB::MemBOpt Domain) const;
    Value *emitLoadLinked(IRBuilderBase &Builder, Type *ValueTy, Value *Addr,
                          AtomicOrdering Ord) const override;
    Value *emitStoreConditional(IRBuilderBase &Builder, Value *Val, Value *Addr,
                                AtomicOrdering Ord) const override;

    void
    emitAtomicCmpXchgNoStoreLLBalance(IRBuilderBase &Builder) const override;

    Instruction *emitLeadingFence(IRBuilderBase &Builder, Instruction *Inst,
                                  AtomicOrdering Ord) const override;
    Instruction *emitTrailingFence(IRBuilderBase &Builder, Instruction *Inst,
                                   AtomicOrdering Ord) const override;

    unsigned getMaxSupportedInterleaveFactor() const override;

    bool lowerInterleavedLoad(LoadInst *LI,
                              ArrayRef<ShuffleVectorInst *> Shuffles,
                              ArrayRef<unsigned> Indices,
                              unsigned Factor) const override;
    bool lowerInterleavedStore(StoreInst *SI, ShuffleVectorInst *SVI,
                               unsigned Factor) const override;

    bool shouldInsertFencesForAtomic(const Instruction *I) const override;
    TargetLoweringBase::AtomicExpansionKind
    shouldExpandAtomicLoadInIR(LoadInst *LI) const override;
    TargetLoweringBase::AtomicExpansionKind
    shouldExpandAtomicStoreInIR(StoreInst *SI) const override;
    TargetLoweringBase::AtomicExpansionKind
    shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const override;
    TargetLoweringBase::AtomicExpansionKind
    shouldExpandAtomicCmpXchgInIR(AtomicCmpXchgInst *AI) const override;

    bool useLoadStackGuardNode() const override;

    void insertSSPDeclarations(Module &M) const override;
    Value *getSDagStackGuard(const Module &M) const override;
    Function *getSSPStackGuardCheck(const Module &M) const override;

    bool canCombineStoreAndExtract(Type *VectorTy, Value *Idx,
                                   unsigned &Cost) const override;

    bool canMergeStoresTo(unsigned AddressSpace, EVT MemVT,
                          const MachineFunction &MF) const override {
      // Do not merge to larger than i32.
      return (MemVT.getSizeInBits() <= 32);
    }

    bool isCheapToSpeculateCttz(Type *Ty) const override;
    bool isCheapToSpeculateCtlz(Type *Ty) const override;

    bool convertSetCCLogicToBitwiseLogic(EVT VT) const override {
      return VT.isScalarInteger();
    }

    bool supportSwiftError() const override {
      return true;
    }

    bool hasStandaloneRem(EVT VT) const override {
      return HasStandaloneRem;
    }

    ShiftLegalizationStrategy
    preferredShiftLegalizationStrategy(SelectionDAG &DAG, SDNode *N,
                                       unsigned ExpansionFactor) const override;

    CCAssignFn *CCAssignFnForCall(CallingConv::ID CC, bool isVarArg) const;
    CCAssignFn *CCAssignFnForReturn(CallingConv::ID CC, bool isVarArg) const;

    /// Returns true if \p VecTy is a legal interleaved access type. This
    /// function checks the vector element type and the overall width of the
    /// vector.
    bool isLegalInterleavedAccessType(unsigned Factor, FixedVectorType *VecTy,
                                      Align Alignment,
                                      const DataLayout &DL) const;

    bool isMulAddWithConstProfitable(SDValue AddNode,
                                     SDValue ConstNode) const override;

    bool alignLoopsWithOptSize() const override;

    /// Returns the number of interleaved accesses that will be generated when
    /// lowering accesses of the given type.
    unsigned getNumInterleavedAccesses(VectorType *VecTy,
                                       const DataLayout &DL) const;

    void finalizeLowering(MachineFunction &MF) const override;

    /// Return the correct alignment for the current calling convention.
    Align getABIAlignmentForCallingConv(Type *ArgTy,
                                        const DataLayout &DL) const override;

    bool isDesirableToCommuteWithShift(const SDNode *N,
                                       CombineLevel Level) const override;

    bool isDesirableToCommuteXorWithShift(const SDNode *N) const override;

    bool shouldFoldConstantShiftPairToMask(const SDNode *N,
                                           CombineLevel Level) const override;

    bool shouldFoldSelectWithIdentityConstant(unsigned BinOpcode,
                                              EVT VT) const override;

    bool preferIncOfAddToSubOfNot(EVT VT) const override;

    bool shouldConvertFpToSat(unsigned Op, EVT FPVT, EVT VT) const override;

    bool isComplexDeinterleavingSupported() const override;
    bool isComplexDeinterleavingOperationSupported(
        ComplexDeinterleavingOperation Operation, Type *Ty) const override;

    Value *createComplexDeinterleavingIR(
        IRBuilderBase &B, ComplexDeinterleavingOperation OperationType,
        ComplexDeinterleavingRotation Rotation, Value *InputA, Value *InputB,
        Value *Accumulator = nullptr) const override;

    bool softPromoteHalfType() const override { return true; }

    bool useFPRegsForHalfType() const override { return true; }

  protected:
    std::pair<const TargetRegisterClass *, uint8_t>
    findRepresentativeClass(const TargetRegisterInfo *TRI,
                            MVT VT) const override;

  private:
    /// Subtarget - Keep a pointer to the ARMSubtarget around so that we can
    /// make the right decision when generating code for different targets.
    const ARMSubtarget *Subtarget;

    const TargetRegisterInfo *RegInfo;

    const InstrItineraryData *Itins;

    // TODO: remove this, and have shouldInsertFencesForAtomic do the proper
    // check.
    bool InsertFencesForAtomic;

    bool HasStandaloneRem = true;

    void addTypeForNEON(MVT VT, MVT PromotedLdStVT);
    void addDRTypeForNEON(MVT VT);
    void addQRTypeForNEON(MVT VT);
    std::pair<SDValue, SDValue> getARMXALUOOp(SDValue Op, SelectionDAG &DAG, SDValue &ARMcc) const;

    using RegsToPassVector = SmallVector<std::pair<unsigned, SDValue>, 8>;

    void PassF64ArgInRegs(const SDLoc &dl, SelectionDAG &DAG, SDValue Chain,
                          SDValue &Arg, RegsToPassVector &RegsToPass,
                          CCValAssign &VA, CCValAssign &NextVA,
                          SDValue &StackPtr,
                          SmallVectorImpl<SDValue> &MemOpChains,
                          bool IsTailCall,
                          int SPDiff) const;
    SDValue GetF64FormalArgument(CCValAssign &VA, CCValAssign &NextVA,
                                 SDValue &Root, SelectionDAG &DAG,
                                 const SDLoc &dl) const;

    CallingConv::ID getEffectiveCallingConv(CallingConv::ID CC,
                                            bool isVarArg) const;
    CCAssignFn *CCAssignFnForNode(CallingConv::ID CC, bool Return,
                                  bool isVarArg) const;
    std::pair<SDValue, MachinePointerInfo>
    computeAddrForCallArg(const SDLoc &dl, SelectionDAG &DAG,
                          const CCValAssign &VA, SDValue StackPtr,
                          bool IsTailCall, int SPDiff) const;
    SDValue LowerEH_SJLJ_SETJMP(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerEH_SJLJ_LONGJMP(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerEH_SJLJ_SETUP_DISPATCH(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerINTRINSIC_VOID(SDValue Op, SelectionDAG &DAG,
                                    const ARMSubtarget *Subtarget) const;
    SDValue LowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG,
                                    const ARMSubtarget *Subtarget) const;
    SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGlobalAddressDarwin(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGlobalAddressELF(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGlobalAddressWindows(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerToTLSGeneralDynamicModel(GlobalAddressSDNode *GA,
                                            SelectionDAG &DAG) const;
    SDValue LowerToTLSExecModels(GlobalAddressSDNode *GA,
                                 SelectionDAG &DAG,
                                 TLSModel::Model model) const;
    SDValue LowerGlobalTLSAddressDarwin(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGlobalTLSAddressWindows(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerBR_JT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSignedALUO(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerUnsignedALUO(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSELECT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFCOPYSIGN(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerShiftRightParts(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerShiftLeftParts(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGET_ROUNDING(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSET_ROUNDING(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSET_FPMODE(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerRESET_FPMODE(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerConstantFP(SDValue Op, SelectionDAG &DAG,
                            const ARMSubtarget *ST) const;
    SDValue LowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG,
                              const ARMSubtarget *ST) const;
    SDValue LowerINSERT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFSINCOS(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerDivRem(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerDIV_Windows(SDValue Op, SelectionDAG &DAG, bool Signed) const;
    void ExpandDIV_Windows(SDValue Op, SelectionDAG &DAG, bool Signed,
                           SmallVectorImpl<SDValue> &Results) const;
    SDValue ExpandBITCAST(SDNode *N, SelectionDAG &DAG,
                          const ARMSubtarget *Subtarget) const;
    SDValue LowerWindowsDIVLibCall(SDValue Op, SelectionDAG &DAG, bool Signed,
                                   SDValue &Chain) const;
    SDValue LowerREM(SDNode *N, SelectionDAG &DAG) const;
    SDValue LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFP_ROUND(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFP_EXTEND(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFP_TO_INT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFSETCC(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSPONENTRY(SDValue Op, SelectionDAG &DAG) const;
    void LowerLOAD(SDNode *N, SmallVectorImpl<SDValue> &Results,
                   SelectionDAG &DAG) const;

    Register getRegisterByName(const char* RegName, LLT VT,
                               const MachineFunction &MF) const override;

    SDValue BuildSDIVPow2(SDNode *N, const APInt &Divisor, SelectionDAG &DAG,
                          SmallVectorImpl<SDNode *> &Created) const override;

    bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                    EVT VT) const override;

    SDValue MoveToHPR(const SDLoc &dl, SelectionDAG &DAG, MVT LocVT, MVT ValVT,
                      SDValue Val) const;
    SDValue MoveFromHPR(const SDLoc &dl, SelectionDAG &DAG, MVT LocVT,
                        MVT ValVT, SDValue Val) const;

    SDValue ReconstructShuffle(SDValue Op, SelectionDAG &DAG) const;

    SDValue LowerCallResult(SDValue Chain, SDValue InGlue,
                            CallingConv::ID CallConv, bool isVarArg,
                            const SmallVectorImpl<ISD::InputArg> &Ins,
                            const SDLoc &dl, SelectionDAG &DAG,
                            SmallVectorImpl<SDValue> &InVals, bool isThisReturn,
                            SDValue ThisVal, bool isCmseNSCall) const;

    bool supportSplitCSR(MachineFunction *MF) const override {
      return MF->getFunction().getCallingConv() == CallingConv::CXX_FAST_TLS &&
          MF->getFunction().hasFnAttribute(Attribute::NoUnwind);
    }

    void initializeSplitCSR(MachineBasicBlock *Entry) const override;
    void insertCopiesSplitCSR(
      MachineBasicBlock *Entry,
      const SmallVectorImpl<MachineBasicBlock *> &Exits) const override;

    bool splitValueIntoRegisterParts(
        SelectionDAG & DAG, const SDLoc &DL, SDValue Val, SDValue *Parts,
        unsigned NumParts, MVT PartVT, std::optional<CallingConv::ID> CC)
        const override;

    SDValue joinRegisterPartsIntoValue(
        SelectionDAG & DAG, const SDLoc &DL, const SDValue *Parts,
        unsigned NumParts, MVT PartVT, EVT ValueVT,
        std::optional<CallingConv::ID> CC) const override;

    SDValue
    LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                         const SmallVectorImpl<ISD::InputArg> &Ins,
                         const SDLoc &dl, SelectionDAG &DAG,
                         SmallVectorImpl<SDValue> &InVals) const override;

    int StoreByValRegs(CCState &CCInfo, SelectionDAG &DAG, const SDLoc &dl,
                       SDValue &Chain, const Value *OrigArg,
                       unsigned InRegsParamRecordIdx, int ArgOffset,
                       unsigned ArgSize) const;

    void VarArgStyleRegisters(CCState &CCInfo, SelectionDAG &DAG,
                              const SDLoc &dl, SDValue &Chain,
                              unsigned ArgOffset, unsigned TotalArgRegsSaveSize,
                              bool ForceMutable = false) const;

    SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                      SmallVectorImpl<SDValue> &InVals) const override;

    /// HandleByVal - Target-specific cleanup for ByVal support.
    void HandleByVal(CCState *, unsigned &, Align) const override;

    /// IsEligibleForTailCallOptimization - Check whether the call is eligible
    /// for tail call optimization. Targets which want to do tail call
    /// optimization should implement this function.
    bool IsEligibleForTailCallOptimization(
        TargetLowering::CallLoweringInfo &CLI, CCState &CCInfo,
        SmallVectorImpl<CCValAssign> &ArgLocs, const bool isIndirect) const;

    bool CanLowerReturn(CallingConv::ID CallConv,
                        MachineFunction &MF, bool isVarArg,
                        const SmallVectorImpl<ISD::OutputArg> &Outs,
                        LLVMContext &Context) const override;

    SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                        const SmallVectorImpl<ISD::OutputArg> &Outs,
                        const SmallVectorImpl<SDValue> &OutVals,
                        const SDLoc &dl, SelectionDAG &DAG) const override;

    bool isUsedByReturnOnly(SDNode *N, SDValue &Chain) const override;

    bool mayBeEmittedAsTailCall(const CallInst *CI) const override;

    bool shouldConsiderGEPOffsetSplit() const override { return true; }

    bool isUnsupportedFloatingType(EVT VT) const;

    SDValue getCMOV(const SDLoc &dl, EVT VT, SDValue FalseVal, SDValue TrueVal,
                    SDValue ARMcc, SDValue CCR, SDValue Cmp,
                    SelectionDAG &DAG) const;
    SDValue getARMCmp(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                      SDValue &ARMcc, SelectionDAG &DAG, const SDLoc &dl) const;
    SDValue getVFPCmp(SDValue LHS, SDValue RHS, SelectionDAG &DAG,
                      const SDLoc &dl, bool Signaling = false) const;
    SDValue duplicateCmp(SDValue Cmp, SelectionDAG &DAG) const;

    SDValue OptimizeVFPBrcond(SDValue Op, SelectionDAG &DAG) const;

    void SetupEntryBlockForSjLj(MachineInstr &MI, MachineBasicBlock *MBB,
                                MachineBasicBlock *DispatchBB, int FI) const;

    void EmitSjLjDispatchBlock(MachineInstr &MI, MachineBasicBlock *MBB) const;

    MachineBasicBlock *EmitStructByval(MachineInstr &MI,
                                       MachineBasicBlock *MBB) const;

    MachineBasicBlock *EmitLowered__chkstk(MachineInstr &MI,
                                           MachineBasicBlock *MBB) const;
    MachineBasicBlock *EmitLowered__dbzchk(MachineInstr &MI,
                                           MachineBasicBlock *MBB) const;
    void addMVEVectorTypes(bool HasMVEFP);
    void addAllExtLoads(const MVT From, const MVT To, LegalizeAction Action);
    void setAllExpand(MVT VT);
  };

  enum VMOVModImmType {
    VMOVModImm,
    VMVNModImm,
    MVEVMVNModImm,
    OtherModImm
  };

  namespace ARM {

    FastISel *createFastISel(FunctionLoweringInfo &funcInfo,
                             const TargetLibraryInfo *libInfo);

  } // end namespace ARM

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARMISELLOWERING_H
