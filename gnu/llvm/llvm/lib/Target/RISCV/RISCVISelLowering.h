//===-- RISCVISelLowering.h - RISC-V DAG Lowering Interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that RISC-V uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_RISCVISELLOWERING_H
#define LLVM_LIB_TARGET_RISCV_RISCVISELLOWERING_H

#include "RISCV.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"
#include <optional>

namespace llvm {
class InstructionCost;
class RISCVSubtarget;
struct RISCVRegisterInfo;
class RVVArgDispatcher;

namespace RISCVISD {
// clang-format off
enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  RET_GLUE,
  SRET_GLUE,
  MRET_GLUE,
  CALL,
  /// Select with condition operator - This selects between a true value and
  /// a false value (ops #3 and #4) based on the boolean result of comparing
  /// the lhs and rhs (ops #0 and #1) of a conditional expression with the
  /// condition code in op #2, a XLenVT constant from the ISD::CondCode enum.
  /// The lhs and rhs are XLenVT integers. The true and false values can be
  /// integer or floating point.
  SELECT_CC,
  BR_CC,
  BuildPairF64,
  SplitF64,
  TAIL,

  // Add the Lo 12 bits from an address. Selected to ADDI.
  ADD_LO,
  // Get the Hi 20 bits from an address. Selected to LUI.
  HI,

  // Represents an AUIPC+ADDI pair. Selected to PseudoLLA.
  LLA,

  // Selected as PseudoAddTPRel. Used to emit a TP-relative relocation.
  ADD_TPREL,

  // Multiply high for signedxunsigned.
  MULHSU,

  // Represents (ADD (SHL a, b), c) with the arguments appearing in the order
  // a, b, c.  'b' must be a constant.  Maps to sh1add/sh2add/sh3add with zba
  // or addsl with XTheadBa.
  SHL_ADD,

  // RV64I shifts, directly matching the semantics of the named RISC-V
  // instructions.
  SLLW,
  SRAW,
  SRLW,
  // 32-bit operations from RV64M that can't be simply matched with a pattern
  // at instruction selection time. These have undefined behavior for division
  // by 0 or overflow (divw) like their target independent counterparts.
  DIVW,
  DIVUW,
  REMUW,
  // RV64IB rotates, directly matching the semantics of the named RISC-V
  // instructions.
  ROLW,
  RORW,
  // RV64IZbb bit counting instructions directly matching the semantics of the
  // named RISC-V instructions.
  CLZW,
  CTZW,

  // RV64IZbb absolute value for i32. Expanded to (max (negw X), X) during isel.
  ABSW,

  // FPR<->GPR transfer operations when the FPR is smaller than XLEN, needed as
  // XLEN is the only legal integer width.
  //
  // FMV_H_X matches the semantics of the FMV.H.X.
  // FMV_X_ANYEXTH is similar to FMV.X.H but has an any-extended result.
  // FMV_X_SIGNEXTH is similar to FMV.X.H and has a sign-extended result.
  // FMV_W_X_RV64 matches the semantics of the FMV.W.X.
  // FMV_X_ANYEXTW_RV64 is similar to FMV.X.W but has an any-extended result.
  //
  // This is a more convenient semantic for producing dagcombines that remove
  // unnecessary GPR->FPR->GPR moves.
  FMV_H_X,
  FMV_X_ANYEXTH,
  FMV_X_SIGNEXTH,
  FMV_W_X_RV64,
  FMV_X_ANYEXTW_RV64,
  // FP to XLen int conversions. Corresponds to fcvt.l(u).s/d/h on RV64 and
  // fcvt.w(u).s/d/h on RV32. Unlike FP_TO_S/UINT these saturate out of
  // range inputs. These are used for FP_TO_S/UINT_SAT lowering. Rounding mode
  // is passed as a TargetConstant operand using the RISCVFPRndMode enum.
  FCVT_X,
  FCVT_XU,
  // FP to 32 bit int conversions for RV64. These are used to keep track of the
  // result being sign extended to 64 bit. These saturate out of range inputs.
  // Used for FP_TO_S/UINT and FP_TO_S/UINT_SAT lowering. Rounding mode
  // is passed as a TargetConstant operand using the RISCVFPRndMode enum.
  FCVT_W_RV64,
  FCVT_WU_RV64,

  FP_ROUND_BF16,
  FP_EXTEND_BF16,

  // Rounds an FP value to its corresponding integer in the same FP format.
  // First operand is the value to round, the second operand is the largest
  // integer that can be represented exactly in the FP format. This will be
  // expanded into multiple instructions and basic blocks with a custom
  // inserter.
  FROUND,

  FCLASS,

  // Floating point fmax and fmin matching the RISC-V instruction semantics.
  FMAX, FMIN,

  // A read of the 64-bit counter CSR on a 32-bit target (returns (Lo, Hi)).
  // It takes a chain operand and another two target constant operands (the
  // CSR numbers of the low and high parts of the counter).
  READ_COUNTER_WIDE,

  // brev8, orc.b, zip, and unzip from Zbb and Zbkb. All operands are i32 or
  // XLenVT.
  BREV8,
  ORC_B,
  ZIP,
  UNZIP,

  // Scalar cryptography
  CLMUL, CLMULH, CLMULR,
  SHA256SIG0, SHA256SIG1, SHA256SUM0, SHA256SUM1,
  SM4KS, SM4ED,
  SM3P0, SM3P1,

  // May-Be-Operations
  MOPR, MOPRR,

  // Vector Extension
  FIRST_VL_VECTOR_OP,
  // VMV_V_V_VL matches the semantics of vmv.v.v but includes an extra operand
  // for the VL value to be used for the operation. The first operand is
  // passthru operand.
  VMV_V_V_VL = FIRST_VL_VECTOR_OP,
  // VMV_V_X_VL matches the semantics of vmv.v.x but includes an extra operand
  // for the VL value to be used for the operation. The first operand is
  // passthru operand.
  VMV_V_X_VL,
  // VFMV_V_F_VL matches the semantics of vfmv.v.f but includes an extra operand
  // for the VL value to be used for the operation. The first operand is
  // passthru operand.
  VFMV_V_F_VL,
  // VMV_X_S matches the semantics of vmv.x.s. The result is always XLenVT sign
  // extended from the vector element size.
  VMV_X_S,
  // VMV_S_X_VL matches the semantics of vmv.s.x. It carries a VL operand.
  VMV_S_X_VL,
  // VFMV_S_F_VL matches the semantics of vfmv.s.f. It carries a VL operand.
  VFMV_S_F_VL,
  // Splats an 64-bit value that has been split into two i32 parts. This is
  // expanded late to two scalar stores and a stride 0 vector load.
  // The first operand is passthru operand.
  SPLAT_VECTOR_SPLIT_I64_VL,
  // Truncates a RVV integer vector by one power-of-two. Carries both an extra
  // mask and VL operand.
  TRUNCATE_VECTOR_VL,
  // Matches the semantics of vslideup/vslidedown. The first operand is the
  // pass-thru operand, the second is the source vector, the third is the XLenVT
  // index (either constant or non-constant), the fourth is the mask, the fifth
  // is the VL and the sixth is the policy.
  VSLIDEUP_VL,
  VSLIDEDOWN_VL,
  // Matches the semantics of vslide1up/slide1down. The first operand is
  // passthru operand, the second is source vector, third is the XLenVT scalar
  // value. The fourth and fifth operands are the mask and VL operands.
  VSLIDE1UP_VL,
  VSLIDE1DOWN_VL,
  // Matches the semantics of vfslide1up/vfslide1down. The first operand is
  // passthru operand, the second is source vector, third is a scalar value
  // whose type matches the element type of the vectors.  The fourth and fifth
  // operands are the mask and VL operands.
  VFSLIDE1UP_VL,
  VFSLIDE1DOWN_VL,
  // Matches the semantics of the vid.v instruction, with a mask and VL
  // operand.
  VID_VL,
  // Matches the semantics of the vfcnvt.rod function (Convert double-width
  // float to single-width float, rounding towards odd). Takes a double-width
  // float vector and produces a single-width float vector. Also has a mask and
  // VL operand.
  VFNCVT_ROD_VL,
  // These nodes match the semantics of the corresponding RVV vector reduction
  // instructions. They produce a vector result which is the reduction
  // performed over the second vector operand plus the first element of the
  // third vector operand. The first operand is the pass-thru operand. The
  // second operand is an unconstrained vector type, and the result, first, and
  // third operand's types are expected to be the corresponding full-width
  // LMUL=1 type for the second operand:
  //   nxv8i8 = vecreduce_add nxv8i8, nxv32i8, nxv8i8
  //   nxv2i32 = vecreduce_add nxv2i32, nxv8i32, nxv2i32
  // The different in types does introduce extra vsetvli instructions but
  // similarly it reduces the number of registers consumed per reduction.
  // Also has a mask and VL operand.
  VECREDUCE_ADD_VL,
  VECREDUCE_UMAX_VL,
  VECREDUCE_SMAX_VL,
  VECREDUCE_UMIN_VL,
  VECREDUCE_SMIN_VL,
  VECREDUCE_AND_VL,
  VECREDUCE_OR_VL,
  VECREDUCE_XOR_VL,
  VECREDUCE_FADD_VL,
  VECREDUCE_SEQ_FADD_VL,
  VECREDUCE_FMIN_VL,
  VECREDUCE_FMAX_VL,

  // Vector binary ops with a merge as a third operand, a mask as a fourth
  // operand, and VL as a fifth operand.
  ADD_VL,
  AND_VL,
  MUL_VL,
  OR_VL,
  SDIV_VL,
  SHL_VL,
  SREM_VL,
  SRA_VL,
  SRL_VL,
  ROTL_VL,
  ROTR_VL,
  SUB_VL,
  UDIV_VL,
  UREM_VL,
  XOR_VL,
  SMIN_VL,
  SMAX_VL,
  UMIN_VL,
  UMAX_VL,

  BITREVERSE_VL,
  BSWAP_VL,
  CTLZ_VL,
  CTTZ_VL,
  CTPOP_VL,

  SADDSAT_VL,
  UADDSAT_VL,
  SSUBSAT_VL,
  USUBSAT_VL,

  // Averaging adds of signed integers.
  AVGFLOORS_VL,
  // Averaging adds of unsigned integers.
  AVGFLOORU_VL,
  // Rounding averaging adds of signed integers.
  AVGCEILS_VL,
  // Rounding averaging adds of unsigned integers.
  AVGCEILU_VL,

  // Operands are (source, shift, merge, mask, roundmode, vl)
  VNCLIPU_VL,
  VNCLIP_VL,

  MULHS_VL,
  MULHU_VL,
  FADD_VL,
  FSUB_VL,
  FMUL_VL,
  FDIV_VL,
  VFMIN_VL,
  VFMAX_VL,

  // Vector unary ops with a mask as a second operand and VL as a third operand.
  FNEG_VL,
  FABS_VL,
  FSQRT_VL,
  FCLASS_VL,
  FCOPYSIGN_VL, // Has a merge operand
  VFCVT_RTZ_X_F_VL,
  VFCVT_RTZ_XU_F_VL,
  VFCVT_X_F_VL,
  VFCVT_XU_F_VL,
  VFROUND_NOEXCEPT_VL,
  VFCVT_RM_X_F_VL,  // Has a rounding mode operand.
  VFCVT_RM_XU_F_VL, // Has a rounding mode operand.
  SINT_TO_FP_VL,
  UINT_TO_FP_VL,
  VFCVT_RM_F_X_VL,  // Has a rounding mode operand.
  VFCVT_RM_F_XU_VL, // Has a rounding mode operand.
  FP_ROUND_VL,
  FP_EXTEND_VL,

  // Vector FMA ops with a mask as a fourth operand and VL as a fifth operand.
  VFMADD_VL,
  VFNMADD_VL,
  VFMSUB_VL,
  VFNMSUB_VL,

  // Vector widening FMA ops with a mask as a fourth operand and VL as a fifth
  // operand.
  VFWMADD_VL,
  VFWNMADD_VL,
  VFWMSUB_VL,
  VFWNMSUB_VL,

  // Widening instructions with a merge value a third operand, a mask as a
  // fourth operand, and VL as a fifth operand.
  VWMUL_VL,
  VWMULU_VL,
  VWMULSU_VL,
  VWADD_VL,
  VWADDU_VL,
  VWSUB_VL,
  VWSUBU_VL,
  VWADD_W_VL,
  VWADDU_W_VL,
  VWSUB_W_VL,
  VWSUBU_W_VL,
  VWSLL_VL,

  VFWMUL_VL,
  VFWADD_VL,
  VFWSUB_VL,
  VFWADD_W_VL,
  VFWSUB_W_VL,

  // Widening ternary operations with a mask as the fourth operand and VL as the
  // fifth operand.
  VWMACC_VL,
  VWMACCU_VL,
  VWMACCSU_VL,

  // Narrowing logical shift right.
  // Operands are (source, shift, passthru, mask, vl)
  VNSRL_VL,

  // Vector compare producing a mask. Fourth operand is input mask. Fifth
  // operand is VL.
  SETCC_VL,

  // General vmerge node with mask, true, false, passthru, and vl operands.
  // Tail agnostic vselect can be implemented by setting passthru to undef.
  VMERGE_VL,

  // Mask binary operators.
  VMAND_VL,
  VMOR_VL,
  VMXOR_VL,

  // Set mask vector to all zeros or ones.
  VMCLR_VL,
  VMSET_VL,

  // Matches the semantics of vrgather.vx and vrgather.vv with extra operands
  // for passthru and VL. Operands are (src, index, mask, passthru, vl).
  VRGATHER_VX_VL,
  VRGATHER_VV_VL,
  VRGATHEREI16_VV_VL,

  // Vector sign/zero extend with additional mask & VL operands.
  VSEXT_VL,
  VZEXT_VL,

  //  vcpop.m with additional mask and VL operands.
  VCPOP_VL,

  //  vfirst.m with additional mask and VL operands.
  VFIRST_VL,

  LAST_VL_VECTOR_OP = VFIRST_VL,

  // Read VLENB CSR
  READ_VLENB,
  // Reads value of CSR.
  // The first operand is a chain pointer. The second specifies address of the
  // required CSR. Two results are produced, the read value and the new chain
  // pointer.
  READ_CSR,
  // Write value to CSR.
  // The first operand is a chain pointer, the second specifies address of the
  // required CSR and the third is the value to write. The result is the new
  // chain pointer.
  WRITE_CSR,
  // Read and write value of CSR.
  // The first operand is a chain pointer, the second specifies address of the
  // required CSR and the third is the value to write. Two results are produced,
  // the value read before the modification and the new chain pointer.
  SWAP_CSR,

  // Branchless select operations, matching the semantics of the instructions
  // defined in Zicond or XVentanaCondOps.
  CZERO_EQZ, // vt.maskc for XVentanaCondOps.
  CZERO_NEZ, // vt.maskcn for XVentanaCondOps.

  /// Software guarded BRIND node. Operand 0 is the chain operand and
  /// operand 1 is the target address.
  SW_GUARDED_BRIND,

  // FP to 32 bit int conversions for RV64. These are used to keep track of the
  // result being sign extended to 64 bit. These saturate out of range inputs.
  STRICT_FCVT_W_RV64 = ISD::FIRST_TARGET_STRICTFP_OPCODE,
  STRICT_FCVT_WU_RV64,
  STRICT_FADD_VL,
  STRICT_FSUB_VL,
  STRICT_FMUL_VL,
  STRICT_FDIV_VL,
  STRICT_FSQRT_VL,
  STRICT_VFMADD_VL,
  STRICT_VFNMADD_VL,
  STRICT_VFMSUB_VL,
  STRICT_VFNMSUB_VL,
  STRICT_FP_ROUND_VL,
  STRICT_FP_EXTEND_VL,
  STRICT_VFNCVT_ROD_VL,
  STRICT_SINT_TO_FP_VL,
  STRICT_UINT_TO_FP_VL,
  STRICT_VFCVT_RM_X_F_VL,
  STRICT_VFCVT_RTZ_X_F_VL,
  STRICT_VFCVT_RTZ_XU_F_VL,
  STRICT_FSETCC_VL,
  STRICT_FSETCCS_VL,
  STRICT_VFROUND_NOEXCEPT_VL,
  LAST_RISCV_STRICTFP_OPCODE = STRICT_VFROUND_NOEXCEPT_VL,

  SF_VC_XV_SE,
  SF_VC_IV_SE,
  SF_VC_VV_SE,
  SF_VC_FV_SE,
  SF_VC_XVV_SE,
  SF_VC_IVV_SE,
  SF_VC_VVV_SE,
  SF_VC_FVV_SE,
  SF_VC_XVW_SE,
  SF_VC_IVW_SE,
  SF_VC_VVW_SE,
  SF_VC_FVW_SE,
  SF_VC_V_X_SE,
  SF_VC_V_I_SE,
  SF_VC_V_XV_SE,
  SF_VC_V_IV_SE,
  SF_VC_V_VV_SE,
  SF_VC_V_FV_SE,
  SF_VC_V_XVV_SE,
  SF_VC_V_IVV_SE,
  SF_VC_V_VVV_SE,
  SF_VC_V_FVV_SE,
  SF_VC_V_XVW_SE,
  SF_VC_V_IVW_SE,
  SF_VC_V_VVW_SE,
  SF_VC_V_FVW_SE,

  // WARNING: Do not add anything in the end unless you want the node to
  // have memop! In fact, starting from FIRST_TARGET_MEMORY_OPCODE all
  // opcodes will be thought as target memory ops!

  TH_LWD = ISD::FIRST_TARGET_MEMORY_OPCODE,
  TH_LWUD,
  TH_LDD,
  TH_SWD,
  TH_SDD,
};
// clang-format on
} // namespace RISCVISD

class RISCVTargetLowering : public TargetLowering {
  const RISCVSubtarget &Subtarget;

public:
  explicit RISCVTargetLowering(const TargetMachine &TM,
                               const RISCVSubtarget &STI);

  const RISCVSubtarget &getSubtarget() const { return Subtarget; }

  bool getTgtMemIntrinsic(IntrinsicInfo &Info, const CallInst &I,
                          MachineFunction &MF,
                          unsigned Intrinsic) const override;
  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AS,
                             Instruction *I = nullptr) const override;
  bool isLegalICmpImmediate(int64_t Imm) const override;
  bool isLegalAddImmediate(int64_t Imm) const override;
  bool isTruncateFree(Type *SrcTy, Type *DstTy) const override;
  bool isTruncateFree(EVT SrcVT, EVT DstVT) const override;
  bool isTruncateFree(SDValue Val, EVT VT2) const override;
  bool isZExtFree(SDValue Val, EVT VT2) const override;
  bool isSExtCheaperThanZExt(EVT SrcVT, EVT DstVT) const override;
  bool signExtendConstant(const ConstantInt *CI) const override;
  bool isCheapToSpeculateCttz(Type *Ty) const override;
  bool isCheapToSpeculateCtlz(Type *Ty) const override;
  bool isMaskAndCmp0FoldingBeneficial(const Instruction &AndI) const override;
  bool hasAndNotCompare(SDValue Y) const override;
  bool hasBitTest(SDValue X, SDValue Y) const override;
  bool shouldProduceAndByConstByHoistingConstFromShiftsLHSOfAnd(
      SDValue X, ConstantSDNode *XC, ConstantSDNode *CC, SDValue Y,
      unsigned OldShiftOpcode, unsigned NewShiftOpcode,
      SelectionDAG &DAG) const override;
  /// Return true if the (vector) instruction I will be lowered to an instruction
  /// with a scalar splat operand for the given Operand number.
  bool canSplatOperand(Instruction *I, int Operand) const;
  /// Return true if a vector instruction will lower to a target instruction
  /// able to splat the given operand.
  bool canSplatOperand(unsigned Opcode, int Operand) const;
  bool shouldSinkOperands(Instruction *I,
                          SmallVectorImpl<Use *> &Ops) const override;
  bool shouldScalarizeBinop(SDValue VecOp) const override;
  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;
  std::pair<int, bool> getLegalZfaFPImm(const APFloat &Imm, EVT VT) const;
  bool isFPImmLegal(const APFloat &Imm, EVT VT,
                    bool ForCodeSize) const override;
  bool isExtractSubvectorCheap(EVT ResVT, EVT SrcVT,
                               unsigned Index) const override;

  bool isIntDivCheap(EVT VT, AttributeList Attr) const override;

  bool preferScalarizeSplat(SDNode *N) const override;

  bool softPromoteHalfType() const override { return true; }

  /// Return the register type for a given MVT, ensuring vectors are treated
  /// as a series of gpr sized integers.
  MVT getRegisterTypeForCallingConv(LLVMContext &Context, CallingConv::ID CC,
                                    EVT VT) const override;

  /// Return the number of registers for a given MVT, ensuring vectors are
  /// treated as a series of gpr sized integers.
  unsigned getNumRegistersForCallingConv(LLVMContext &Context,
                                         CallingConv::ID CC,
                                         EVT VT) const override;

  unsigned getVectorTypeBreakdownForCallingConv(LLVMContext &Context,
                                                CallingConv::ID CC, EVT VT,
                                                EVT &IntermediateVT,
                                                unsigned &NumIntermediates,
                                                MVT &RegisterVT) const override;

  bool shouldFoldSelectWithIdentityConstant(unsigned BinOpcode,
                                            EVT VT) const override;

  /// Return true if the given shuffle mask can be codegen'd directly, or if it
  /// should be stack expanded.
  bool isShuffleMaskLegal(ArrayRef<int> M, EVT VT) const override;

  bool isMultiStoresCheaperThanBitsMerge(EVT LTy, EVT HTy) const override {
    // If the pair to store is a mixture of float and int values, we will
    // save two bitwise instructions and one float-to-int instruction and
    // increase one store instruction. There is potentially a more
    // significant benefit because it avoids the float->int domain switch
    // for input value. So It is more likely a win.
    if ((LTy.isFloatingPoint() && HTy.isInteger()) ||
        (LTy.isInteger() && HTy.isFloatingPoint()))
      return true;
    // If the pair only contains int values, we will save two bitwise
    // instructions and increase one store instruction (costing one more
    // store buffer). Since the benefit is more blurred we leave such a pair
    // out until we get testcase to prove it is a win.
    return false;
  }

  bool
  shouldExpandBuildVectorWithShuffles(EVT VT,
                                      unsigned DefinedValues) const override;

  bool shouldExpandCttzElements(EVT VT) const override;

  /// Return the cost of LMUL for linear operations.
  InstructionCost getLMULCost(MVT VT) const;

  InstructionCost getVRGatherVVCost(MVT VT) const;
  InstructionCost getVRGatherVICost(MVT VT) const;
  InstructionCost getVSlideVXCost(MVT VT) const;
  InstructionCost getVSlideVICost(MVT VT) const;

  // Provide custom lowering hooks for some operations.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

  bool targetShrinkDemandedConstant(SDValue Op, const APInt &DemandedBits,
                                    const APInt &DemandedElts,
                                    TargetLoweringOpt &TLO) const override;

  void computeKnownBitsForTargetNode(const SDValue Op,
                                     KnownBits &Known,
                                     const APInt &DemandedElts,
                                     const SelectionDAG &DAG,
                                     unsigned Depth) const override;
  unsigned ComputeNumSignBitsForTargetNode(SDValue Op,
                                           const APInt &DemandedElts,
                                           const SelectionDAG &DAG,
                                           unsigned Depth) const override;

  bool canCreateUndefOrPoisonForTargetNode(SDValue Op,
                                           const APInt &DemandedElts,
                                           const SelectionDAG &DAG,
                                           bool PoisonOnly, bool ConsiderFlags,
                                           unsigned Depth) const override;

  const Constant *getTargetConstantFromLoad(LoadSDNode *LD) const override;

  // This method returns the name of a target specific DAG node.
  const char *getTargetNodeName(unsigned Opcode) const override;

  MachineMemOperand::Flags
  getTargetMMOFlags(const Instruction &I) const override;

  MachineMemOperand::Flags
  getTargetMMOFlags(const MemSDNode &Node) const override;

  bool
  areTwoSDNodeTargetMMOFlagsMergeable(const MemSDNode &NodeX,
                                      const MemSDNode &NodeY) const override;

  ConstraintType getConstraintType(StringRef Constraint) const override;

  InlineAsm::ConstraintCode
  getInlineAsmMemConstraint(StringRef ConstraintCode) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

  void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                    std::vector<SDValue> &Ops,
                                    SelectionDAG &DAG) const override;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;

  void AdjustInstrPostInstrSelection(MachineInstr &MI,
                                     SDNode *Node) const override;

  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

  bool shouldFormOverflowOp(unsigned Opcode, EVT VT,
                            bool MathUsed) const override {
    if (VT == MVT::i8 || VT == MVT::i16)
      return false;

    return TargetLowering::shouldFormOverflowOp(Opcode, VT, MathUsed);
  }

  bool storeOfVectorConstantIsCheap(bool IsZero, EVT MemVT, unsigned NumElem,
                                    unsigned AddrSpace) const override {
    // If we can replace 4 or more scalar stores, there will be a reduction
    // in instructions even after we add a vector constant load.
    return NumElem >= 4;
  }

  bool convertSetCCLogicToBitwiseLogic(EVT VT) const override {
    return VT.isScalarInteger();
  }
  bool convertSelectOfConstantsToMath(EVT VT) const override { return true; }

  bool isCtpopFast(EVT VT) const override;

  unsigned getCustomCtpopCost(EVT VT, ISD::CondCode Cond) const override;

  bool preferZeroCompareBranch() const override { return true; }

  bool shouldInsertFencesForAtomic(const Instruction *I) const override {
    return isa<LoadInst>(I) || isa<StoreInst>(I);
  }
  Instruction *emitLeadingFence(IRBuilderBase &Builder, Instruction *Inst,
                                AtomicOrdering Ord) const override;
  Instruction *emitTrailingFence(IRBuilderBase &Builder, Instruction *Inst,
                                 AtomicOrdering Ord) const override;

  bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                  EVT VT) const override;

  ISD::NodeType getExtendForAtomicOps() const override {
    return ISD::SIGN_EXTEND;
  }

  ISD::NodeType getExtendForAtomicCmpSwapArg() const override;

  bool shouldTransformSignedTruncationCheck(EVT XVT,
                                            unsigned KeptBits) const override;

  TargetLowering::ShiftLegalizationStrategy
  preferredShiftLegalizationStrategy(SelectionDAG &DAG, SDNode *N,
                                     unsigned ExpansionFactor) const override {
    if (DAG.getMachineFunction().getFunction().hasMinSize())
      return ShiftLegalizationStrategy::LowerToLibcall;
    return TargetLowering::preferredShiftLegalizationStrategy(DAG, N,
                                                              ExpansionFactor);
  }

  bool isDesirableToCommuteWithShift(const SDNode *N,
                                     CombineLevel Level) const override;

  /// If a physical register, this returns the register that receives the
  /// exception address on entry to an EH pad.
  Register
  getExceptionPointerRegister(const Constant *PersonalityFn) const override;

  /// If a physical register, this returns the register that receives the
  /// exception typeid on entry to a landing pad.
  Register
  getExceptionSelectorRegister(const Constant *PersonalityFn) const override;

  bool shouldExtendTypeInLibCall(EVT Type) const override;
  bool shouldSignExtendTypeInLibCall(EVT Type, bool IsSigned) const override;

  /// Returns the register with the specified architectural or ABI name. This
  /// method is necessary to lower the llvm.read_register.* and
  /// llvm.write_register.* intrinsics. Allocatable registers must be reserved
  /// with the clang -ffixed-xX flag for access to be allowed.
  Register getRegisterByName(const char *RegName, LLT VT,
                             const MachineFunction &MF) const override;

  // Lower incoming arguments, copy physregs into vregs
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

  bool shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                         Type *Ty) const override;
  bool isUsedByReturnOnly(SDNode *N, SDValue &Chain) const override;
  bool mayBeEmittedAsTailCall(const CallInst *CI) const override;
  bool shouldConsiderGEPOffsetSplit() const override { return true; }

  bool decomposeMulByConstant(LLVMContext &Context, EVT VT,
                              SDValue C) const override;

  bool isMulAddWithConstProfitable(SDValue AddNode,
                                   SDValue ConstNode) const override;

  TargetLowering::AtomicExpansionKind
  shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const override;
  Value *emitMaskedAtomicRMWIntrinsic(IRBuilderBase &Builder, AtomicRMWInst *AI,
                                      Value *AlignedAddr, Value *Incr,
                                      Value *Mask, Value *ShiftAmt,
                                      AtomicOrdering Ord) const override;
  TargetLowering::AtomicExpansionKind
  shouldExpandAtomicCmpXchgInIR(AtomicCmpXchgInst *CI) const override;
  Value *emitMaskedAtomicCmpXchgIntrinsic(IRBuilderBase &Builder,
                                          AtomicCmpXchgInst *CI,
                                          Value *AlignedAddr, Value *CmpVal,
                                          Value *NewVal, Value *Mask,
                                          AtomicOrdering Ord) const override;

  /// Returns true if the target allows unaligned memory accesses of the
  /// specified type.
  bool allowsMisalignedMemoryAccesses(
      EVT VT, unsigned AddrSpace = 0, Align Alignment = Align(1),
      MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
      unsigned *Fast = nullptr) const override;

  EVT getOptimalMemOpType(const MemOp &Op,
                          const AttributeList &FuncAttributes) const override;

  bool splitValueIntoRegisterParts(
      SelectionDAG & DAG, const SDLoc &DL, SDValue Val, SDValue *Parts,
      unsigned NumParts, MVT PartVT, std::optional<CallingConv::ID> CC)
      const override;

  SDValue joinRegisterPartsIntoValue(
      SelectionDAG & DAG, const SDLoc &DL, const SDValue *Parts,
      unsigned NumParts, MVT PartVT, EVT ValueVT,
      std::optional<CallingConv::ID> CC) const override;

  // Return the value of VLMax for the given vector type (i.e. SEW and LMUL)
  SDValue computeVLMax(MVT VecVT, const SDLoc &DL, SelectionDAG &DAG) const;

  static RISCVII::VLMUL getLMUL(MVT VT);
  inline static unsigned computeVLMAX(unsigned VectorBits, unsigned EltSize,
                                      unsigned MinSize) {
    // Original equation:
    //   VLMAX = (VectorBits / EltSize) * LMUL
    //   where LMUL = MinSize / RISCV::RVVBitsPerBlock
    // The following equations have been reordered to prevent loss of precision
    // when calculating fractional LMUL.
    return ((VectorBits / EltSize) * MinSize) / RISCV::RVVBitsPerBlock;
  }

  // Return inclusive (low, high) bounds on the value of VLMAX for the
  // given scalable container type given known bounds on VLEN.
  static std::pair<unsigned, unsigned>
  computeVLMAXBounds(MVT ContainerVT, const RISCVSubtarget &Subtarget);

  static unsigned getRegClassIDForLMUL(RISCVII::VLMUL LMul);
  static unsigned getSubregIndexByMVT(MVT VT, unsigned Index);
  static unsigned getRegClassIDForVecVT(MVT VT);
  static std::pair<unsigned, unsigned>
  decomposeSubvectorInsertExtractToSubRegs(MVT VecVT, MVT SubVecVT,
                                           unsigned InsertExtractIdx,
                                           const RISCVRegisterInfo *TRI);
  MVT getContainerForFixedLengthVector(MVT VT) const;

  bool shouldRemoveExtendFromGSIndex(SDValue Extend, EVT DataVT) const override;

  bool isLegalElementTypeForRVV(EVT ScalarTy) const;

  bool shouldConvertFpToSat(unsigned Op, EVT FPVT, EVT VT) const override;

  unsigned getJumpTableEncoding() const override;

  const MCExpr *LowerCustomJumpTableEntry(const MachineJumpTableInfo *MJTI,
                                          const MachineBasicBlock *MBB,
                                          unsigned uid,
                                          MCContext &Ctx) const override;

  bool isVScaleKnownToBeAPowerOfTwo() const override;

  bool getIndexedAddressParts(SDNode *Op, SDValue &Base, SDValue &Offset,
                              ISD::MemIndexedMode &AM, SelectionDAG &DAG) const;
  bool getPreIndexedAddressParts(SDNode *N, SDValue &Base, SDValue &Offset,
                                 ISD::MemIndexedMode &AM,
                                 SelectionDAG &DAG) const override;
  bool getPostIndexedAddressParts(SDNode *N, SDNode *Op, SDValue &Base,
                                  SDValue &Offset, ISD::MemIndexedMode &AM,
                                  SelectionDAG &DAG) const override;

  bool isLegalScaleForGatherScatter(uint64_t Scale,
                                    uint64_t ElemSize) const override {
    // Scaled addressing not supported on indexed load/stores
    return Scale == 1;
  }

  /// If the target has a standard location for the stack protector cookie,
  /// returns the address of that location. Otherwise, returns nullptr.
  Value *getIRStackGuard(IRBuilderBase &IRB) const override;

  /// Returns whether or not generating a interleaved load/store intrinsic for
  /// this type will be legal.
  bool isLegalInterleavedAccessType(VectorType *VTy, unsigned Factor,
                                    Align Alignment, unsigned AddrSpace,
                                    const DataLayout &) const;

  /// Return true if a stride load store of the given result type and
  /// alignment is legal.
  bool isLegalStridedLoadStore(EVT DataType, Align Alignment) const;

  unsigned getMaxSupportedInterleaveFactor() const override { return 8; }

  bool fallBackToDAGISel(const Instruction &Inst) const override;

  bool lowerInterleavedLoad(LoadInst *LI,
                            ArrayRef<ShuffleVectorInst *> Shuffles,
                            ArrayRef<unsigned> Indices,
                            unsigned Factor) const override;

  bool lowerInterleavedStore(StoreInst *SI, ShuffleVectorInst *SVI,
                             unsigned Factor) const override;

  bool lowerDeinterleaveIntrinsicToLoad(IntrinsicInst *II,
                                        LoadInst *LI) const override;

  bool lowerInterleaveIntrinsicToStore(IntrinsicInst *II,
                                       StoreInst *SI) const override;

  bool supportKCFIBundles() const override { return true; }

  SDValue expandIndirectJTBranch(const SDLoc &dl, SDValue Value, SDValue Addr,
                                 int JTI, SelectionDAG &DAG) const override;

  MachineInstr *EmitKCFICheck(MachineBasicBlock &MBB,
                              MachineBasicBlock::instr_iterator &MBBI,
                              const TargetInstrInfo *TII) const override;

  /// RISCVCCAssignFn - This target-specific function extends the default
  /// CCValAssign with additional information used to lower RISC-V calling
  /// conventions.
  typedef bool RISCVCCAssignFn(const DataLayout &DL, RISCVABI::ABI,
                               unsigned ValNo, MVT ValVT, MVT LocVT,
                               CCValAssign::LocInfo LocInfo,
                               ISD::ArgFlagsTy ArgFlags, CCState &State,
                               bool IsFixed, bool IsRet, Type *OrigTy,
                               const RISCVTargetLowering &TLI,
                               RVVArgDispatcher &RVVDispatcher);

private:
  void analyzeInputArgs(MachineFunction &MF, CCState &CCInfo,
                        const SmallVectorImpl<ISD::InputArg> &Ins, bool IsRet,
                        RISCVCCAssignFn Fn) const;
  void analyzeOutputArgs(MachineFunction &MF, CCState &CCInfo,
                         const SmallVectorImpl<ISD::OutputArg> &Outs,
                         bool IsRet, CallLoweringInfo *CLI,
                         RISCVCCAssignFn Fn) const;

  template <class NodeTy>
  SDValue getAddr(NodeTy *N, SelectionDAG &DAG, bool IsLocal = true,
                  bool IsExternWeak = false) const;
  SDValue getStaticTLSAddr(GlobalAddressSDNode *N, SelectionDAG &DAG,
                           bool UseGOT) const;
  SDValue getDynamicTLSAddr(GlobalAddressSDNode *N, SelectionDAG &DAG) const;
  SDValue getTLSDescAddr(GlobalAddressSDNode *N, SelectionDAG &DAG) const;

  SDValue lowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSELECT(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerShiftLeftParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerShiftRightParts(SDValue Op, SelectionDAG &DAG, bool IsSRA) const;
  SDValue lowerSPLAT_VECTOR_PARTS(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVectorMaskSplat(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVectorMaskExt(SDValue Op, SelectionDAG &DAG,
                             int64_t ExtTrueVal) const;
  SDValue lowerVectorMaskTruncLike(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVectorTruncLike(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVectorFPExtendOrRoundLike(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerINSERT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerEXTRACT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINTRINSIC_W_CHAIN(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINTRINSIC_VOID(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVPREDUCE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVECREDUCE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVectorMaskVecReduction(SDValue Op, SelectionDAG &DAG,
                                      bool IsVP) const;
  SDValue lowerFPVECREDUCE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerINSERT_SUBVECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerEXTRACT_SUBVECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVECTOR_DEINTERLEAVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVECTOR_INTERLEAVE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSTEP_VECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVECTOR_REVERSE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVECTOR_SPLICE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerABS(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerMaskedLoad(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerMaskedStore(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFixedLengthVectorFCOPYSIGNToRVV(SDValue Op,
                                               SelectionDAG &DAG) const;
  SDValue lowerMaskedGather(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerMaskedScatter(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFixedLengthVectorLoadToRVV(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFixedLengthVectorStoreToRVV(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFixedLengthVectorSetccToRVV(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFixedLengthVectorSelectToRVV(SDValue Op,
                                            SelectionDAG &DAG) const;
  SDValue lowerToScalableOp(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerIS_FPCLASS(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVPOp(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerLogicVPOp(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVPExtMaskOp(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVPSetCCMaskOp(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVPSplatExperimental(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVPSpliceExperimental(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVPReverseExperimental(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVPFPIntConvOp(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVPStridedLoad(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVPStridedStore(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVPCttzElements(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFixedLengthVectorExtendToRVV(SDValue Op, SelectionDAG &DAG,
                                            unsigned ExtendOpc) const;
  SDValue lowerGET_ROUNDING(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSET_ROUNDING(SDValue Op, SelectionDAG &DAG) const;

  SDValue lowerEH_DWARF_CFA(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerCTLZ_CTTZ_ZERO_UNDEF(SDValue Op, SelectionDAG &DAG) const;

  SDValue lowerStrictFPExtendOrRoundLike(SDValue Op, SelectionDAG &DAG) const;

  SDValue lowerVectorStrictFSetcc(SDValue Op, SelectionDAG &DAG) const;

  SDValue expandUnalignedRVVLoad(SDValue Op, SelectionDAG &DAG) const;
  SDValue expandUnalignedRVVStore(SDValue Op, SelectionDAG &DAG) const;

  bool isEligibleForTailCallOptimization(
      CCState &CCInfo, CallLoweringInfo &CLI, MachineFunction &MF,
      const SmallVector<CCValAssign, 16> &ArgLocs) const;

  /// Generate error diagnostics if any register used by CC has been marked
  /// reserved.
  void validateCCReservedRegs(
      const SmallVectorImpl<std::pair<llvm::Register, llvm::SDValue>> &Regs,
      MachineFunction &MF) const;

  bool useRVVForFixedLengthVectorVT(MVT VT) const;

  MVT getVPExplicitVectorLengthTy() const override;

  bool shouldExpandGetVectorLength(EVT TripCountVT, unsigned VF,
                                   bool IsScalable) const override;

  /// RVV code generation for fixed length vectors does not lower all
  /// BUILD_VECTORs. This makes BUILD_VECTOR legalisation a source of stores to
  /// merge. However, merging them creates a BUILD_VECTOR that is just as
  /// illegal as the original, thus leading to an infinite legalisation loop.
  /// NOTE: Once BUILD_VECTOR can be custom lowered for all legal vector types,
  /// this override can be removed.
  bool mergeStoresAfterLegalization(EVT VT) const override;

  /// Disable normalizing
  /// select(N0&N1, X, Y) => select(N0, select(N1, X, Y), Y) and
  /// select(N0|N1, X, Y) => select(N0, select(N1, X, Y, Y))
  /// RISC-V doesn't have flags so it's better to perform the and/or in a GPR.
  bool shouldNormalizeToSelectSequence(LLVMContext &, EVT) const override {
    return false;
  }

  /// For available scheduling models FDIV + two independent FMULs are much
  /// faster than two FDIVs.
  unsigned combineRepeatedFPDivisors() const override;

  SDValue BuildSDIVPow2(SDNode *N, const APInt &Divisor, SelectionDAG &DAG,
                        SmallVectorImpl<SDNode *> &Created) const override;

  bool shouldFoldSelectWithSingleBitTest(EVT VT,
                                         const APInt &AndMask) const override;

  unsigned getMinimumJumpTableEntries() const override;

  SDValue emitFlushICache(SelectionDAG &DAG, SDValue InChain, SDValue Start,
                          SDValue End, SDValue Flags, SDLoc DL) const;
};

/// As per the spec, the rules for passing vector arguments are as follows:
///
/// 1. For the first vector mask argument, use v0 to pass it.
/// 2. For vector data arguments or rest vector mask arguments, starting from
/// the v8 register, if a vector register group between v8-v23 that has not been
/// allocated can be found and the first register number is a multiple of LMUL,
/// then allocate this vector register group to the argument and mark these
/// registers as allocated. Otherwise, pass it by reference and are replaced in
/// the argument list with the address.
/// 3. For tuple vector data arguments, starting from the v8 register, if
/// NFIELDS consecutive vector register groups between v8-v23 that have not been
/// allocated can be found and the first register number is a multiple of LMUL,
/// then allocate these vector register groups to the argument and mark these
/// registers as allocated. Otherwise, pass it by reference and are replaced in
/// the argument list with the address.
class RVVArgDispatcher {
public:
  static constexpr unsigned NumArgVRs = 16;

  struct RVVArgInfo {
    unsigned NF;
    MVT VT;
    bool FirstVMask = false;
  };

  template <typename Arg>
  RVVArgDispatcher(const MachineFunction *MF, const RISCVTargetLowering *TLI,
                   ArrayRef<Arg> ArgList)
      : MF(MF), TLI(TLI) {
    constructArgInfos(ArgList);
    compute();
  }

  RVVArgDispatcher() = default;

  MCPhysReg getNextPhysReg();

private:
  SmallVector<RVVArgInfo, 4> RVVArgInfos;
  SmallVector<MCPhysReg, 4> AllocatedPhysRegs;

  const MachineFunction *MF = nullptr;
  const RISCVTargetLowering *TLI = nullptr;

  unsigned CurIdx = 0;

  template <typename Arg> void constructArgInfos(ArrayRef<Arg> Ret);
  void compute();
  void allocatePhysReg(unsigned NF = 1, unsigned LMul = 1,
                       unsigned StartReg = 0);
};

namespace RISCV {

bool CC_RISCV(const DataLayout &DL, RISCVABI::ABI ABI, unsigned ValNo,
              MVT ValVT, MVT LocVT, CCValAssign::LocInfo LocInfo,
              ISD::ArgFlagsTy ArgFlags, CCState &State, bool IsFixed,
              bool IsRet, Type *OrigTy, const RISCVTargetLowering &TLI,
              RVVArgDispatcher &RVVDispatcher);

bool CC_RISCV_FastCC(const DataLayout &DL, RISCVABI::ABI ABI, unsigned ValNo,
                     MVT ValVT, MVT LocVT, CCValAssign::LocInfo LocInfo,
                     ISD::ArgFlagsTy ArgFlags, CCState &State, bool IsFixed,
                     bool IsRet, Type *OrigTy, const RISCVTargetLowering &TLI,
                     RVVArgDispatcher &RVVDispatcher);

bool CC_RISCV_GHC(unsigned ValNo, MVT ValVT, MVT LocVT,
                  CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                  CCState &State);

ArrayRef<MCPhysReg> getArgGPRs(const RISCVABI::ABI ABI);

} // end namespace RISCV

namespace RISCVVIntrinsicsTable {

struct RISCVVIntrinsicInfo {
  unsigned IntrinsicID;
  uint8_t ScalarOperand;
  uint8_t VLOperand;
  bool hasScalarOperand() const {
    // 0xF is not valid. See NoScalarOperand in IntrinsicsRISCV.td.
    return ScalarOperand != 0xF;
  }
  bool hasVLOperand() const {
    // 0x1F is not valid. See NoVLOperand in IntrinsicsRISCV.td.
    return VLOperand != 0x1F;
  }
};

using namespace RISCV;

#define GET_RISCVVIntrinsicsTable_DECL
#include "RISCVGenSearchableTables.inc"
#undef GET_RISCVVIntrinsicsTable_DECL

} // end namespace RISCVVIntrinsicsTable

} // end namespace llvm

#endif
