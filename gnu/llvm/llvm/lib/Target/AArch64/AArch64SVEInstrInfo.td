//=- AArch64SVEInstrInfo.td -  AArch64 SVE Instructions -*- tablegen -*-----=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AArch64 Scalable Vector Extension (SVE) Instruction definitions.
//
//===----------------------------------------------------------------------===//

// For predicated nodes where the entire operation is controlled by a governing
// predicate, please stick to a similar naming convention as used for the
// ISD nodes:
//
//    SDNode      <=>     AArch64ISD
//    -------------------------------
//    _m<n>       <=>     _MERGE_OP<n>
//    _mt         <=>     _MERGE_PASSTHRU
//    _z          <=>     _MERGE_ZERO
//    _p          <=>     _PRED
//
//  Given the context of this file, it is not strictly necessary to use _p to
//  distinguish predicated from unpredicated nodes given that most SVE
//  instructions are predicated.

// Contiguous loads - node definitions
//
def SDT_AArch64_LD1 : SDTypeProfile<1, 3, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisPtrTy<2>,
  SDTCVecEltisVT<1,i1>, SDTCisSameNumEltsAs<0,1>
]>;

def AArch64ld1_z  : SDNode<"AArch64ISD::LD1_MERGE_ZERO",    SDT_AArch64_LD1, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue]>;
def AArch64ld1s_z : SDNode<"AArch64ISD::LD1S_MERGE_ZERO",   SDT_AArch64_LD1, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue]>;

// Non-faulting & first-faulting loads - node definitions
//
def AArch64ldnf1_z : SDNode<"AArch64ISD::LDNF1_MERGE_ZERO", SDT_AArch64_LD1, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1_z : SDNode<"AArch64ISD::LDFF1_MERGE_ZERO", SDT_AArch64_LD1, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;

def AArch64ldnf1s_z : SDNode<"AArch64ISD::LDNF1S_MERGE_ZERO", SDT_AArch64_LD1, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1s_z : SDNode<"AArch64ISD::LDFF1S_MERGE_ZERO", SDT_AArch64_LD1, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;

// Contiguous load and replicate - node definitions
//

def SDT_AArch64_LD1Replicate : SDTypeProfile<1, 2, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisPtrTy<2>,
  SDTCVecEltisVT<1,i1>, SDTCisSameNumEltsAs<0,1>
]>;

def AArch64ld1rq_z : SDNode<"AArch64ISD::LD1RQ_MERGE_ZERO",  SDT_AArch64_LD1Replicate, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1ro_z : SDNode<"AArch64ISD::LD1RO_MERGE_ZERO",  SDT_AArch64_LD1Replicate, [SDNPHasChain, SDNPMayLoad]>;

// Gather loads - node definitions
//
def SDT_AArch64_GATHER_SV : SDTypeProfile<1, 4, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisPtrTy<2>, SDTCisVec<3>, SDTCisVT<4, OtherVT>,
  SDTCVecEltisVT<1,i1>, SDTCisSameNumEltsAs<0,1>
]>;

def SDT_AArch64_GATHER_VS : SDTypeProfile<1, 4, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisVec<2>, SDTCisInt<3>, SDTCisVT<4, OtherVT>,
  SDTCVecEltisVT<1,i1>, SDTCisSameNumEltsAs<0,1>
]>;

def AArch64ld1_gather_z             : SDNode<"AArch64ISD::GLD1_MERGE_ZERO",             SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1_gather_scaled_z      : SDNode<"AArch64ISD::GLD1_SCALED_MERGE_ZERO",      SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1_gather_uxtw_z        : SDNode<"AArch64ISD::GLD1_UXTW_MERGE_ZERO",        SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1_gather_sxtw_z        : SDNode<"AArch64ISD::GLD1_SXTW_MERGE_ZERO",        SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1_gather_uxtw_scaled_z : SDNode<"AArch64ISD::GLD1_UXTW_SCALED_MERGE_ZERO", SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1_gather_sxtw_scaled_z : SDNode<"AArch64ISD::GLD1_SXTW_SCALED_MERGE_ZERO", SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1_gather_imm_z         : SDNode<"AArch64ISD::GLD1_IMM_MERGE_ZERO",         SDT_AArch64_GATHER_VS, [SDNPHasChain, SDNPMayLoad]>;

def AArch64ld1s_gather_z             : SDNode<"AArch64ISD::GLD1S_MERGE_ZERO",             SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1s_gather_scaled_z      : SDNode<"AArch64ISD::GLD1S_SCALED_MERGE_ZERO",      SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1s_gather_uxtw_z        : SDNode<"AArch64ISD::GLD1S_UXTW_MERGE_ZERO",        SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1s_gather_sxtw_z        : SDNode<"AArch64ISD::GLD1S_SXTW_MERGE_ZERO",        SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1s_gather_uxtw_scaled_z : SDNode<"AArch64ISD::GLD1S_UXTW_SCALED_MERGE_ZERO", SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1s_gather_sxtw_scaled_z : SDNode<"AArch64ISD::GLD1S_SXTW_SCALED_MERGE_ZERO", SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ld1s_gather_imm_z         : SDNode<"AArch64ISD::GLD1S_IMM_MERGE_ZERO",         SDT_AArch64_GATHER_VS, [SDNPHasChain, SDNPMayLoad]>;

def AArch64ldff1_gather_z             : SDNode<"AArch64ISD::GLDFF1_MERGE_ZERO",             SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1_gather_scaled_z      : SDNode<"AArch64ISD::GLDFF1_SCALED_MERGE_ZERO",      SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1_gather_uxtw_z        : SDNode<"AArch64ISD::GLDFF1_UXTW_MERGE_ZERO",        SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1_gather_sxtw_z        : SDNode<"AArch64ISD::GLDFF1_SXTW_MERGE_ZERO",        SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1_gather_uxtw_scaled_z : SDNode<"AArch64ISD::GLDFF1_UXTW_SCALED_MERGE_ZERO", SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1_gather_sxtw_scaled_z : SDNode<"AArch64ISD::GLDFF1_SXTW_SCALED_MERGE_ZERO", SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1_gather_imm_z         : SDNode<"AArch64ISD::GLDFF1_IMM_MERGE_ZERO",         SDT_AArch64_GATHER_VS, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;

def AArch64ldff1s_gather_z             : SDNode<"AArch64ISD::GLDFF1S_MERGE_ZERO",             SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1s_gather_scaled_z      : SDNode<"AArch64ISD::GLDFF1S_SCALED_MERGE_ZERO",      SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1s_gather_uxtw_z        : SDNode<"AArch64ISD::GLDFF1S_UXTW_MERGE_ZERO",        SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1s_gather_sxtw_z        : SDNode<"AArch64ISD::GLDFF1S_SXTW_MERGE_ZERO",        SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1s_gather_uxtw_scaled_z : SDNode<"AArch64ISD::GLDFF1S_UXTW_SCALED_MERGE_ZERO", SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1s_gather_sxtw_scaled_z : SDNode<"AArch64ISD::GLDFF1S_SXTW_SCALED_MERGE_ZERO", SDT_AArch64_GATHER_SV, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64ldff1s_gather_imm_z         : SDNode<"AArch64ISD::GLDFF1S_IMM_MERGE_ZERO",         SDT_AArch64_GATHER_VS, [SDNPHasChain, SDNPMayLoad, SDNPOptInGlue, SDNPOutGlue]>;

def AArch64ldnt1_gather_z  : SDNode<"AArch64ISD::GLDNT1_MERGE_ZERO",  SDT_AArch64_GATHER_VS, [SDNPHasChain, SDNPMayLoad]>;
def AArch64ldnt1s_gather_z : SDNode<"AArch64ISD::GLDNT1S_MERGE_ZERO", SDT_AArch64_GATHER_VS, [SDNPHasChain, SDNPMayLoad]>;
// Gather vector base +  scalar offset
def AArch64ld1q_gather_z: SDNode<"AArch64ISD::GLD1Q_MERGE_ZERO", SDT_AArch64_GATHER_VS, [SDNPHasChain, SDNPMayLoad]>;

// Contiguous stores - node definitions
//
def SDT_AArch64_ST1 : SDTypeProfile<0, 4, [
  SDTCisVec<0>, SDTCisPtrTy<1>, SDTCisVec<2>,
  SDTCVecEltisVT<2,i1>, SDTCisSameNumEltsAs<0,2>
]>;

def AArch64st1 : SDNode<"AArch64ISD::ST1_PRED", SDT_AArch64_ST1, [SDNPHasChain, SDNPMayStore]>;

// Scatter stores - node definitions
//
def SDT_AArch64_SCATTER_SV : SDTypeProfile<0, 5, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisPtrTy<2>, SDTCisVec<3>, SDTCisVT<4, OtherVT>,
  SDTCVecEltisVT<1,i1>, SDTCisSameNumEltsAs<0,1>
]>;

def SDT_AArch64_SCATTER_VS : SDTypeProfile<0, 5, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisVec<2>, SDTCisInt<3>, SDTCisVT<4, OtherVT>,
  SDTCVecEltisVT<1,i1>, SDTCisSameNumEltsAs<0,1>
]>;

def AArch64st1_scatter             : SDNode<"AArch64ISD::SST1_PRED",             SDT_AArch64_SCATTER_SV, [SDNPHasChain, SDNPMayStore]>;
def AArch64st1_scatter_scaled      : SDNode<"AArch64ISD::SST1_SCALED_PRED",      SDT_AArch64_SCATTER_SV, [SDNPHasChain, SDNPMayStore]>;
def AArch64st1_scatter_uxtw        : SDNode<"AArch64ISD::SST1_UXTW_PRED",        SDT_AArch64_SCATTER_SV, [SDNPHasChain, SDNPMayStore]>;
def AArch64st1_scatter_sxtw        : SDNode<"AArch64ISD::SST1_SXTW_PRED",        SDT_AArch64_SCATTER_SV, [SDNPHasChain, SDNPMayStore]>;
def AArch64st1_scatter_uxtw_scaled : SDNode<"AArch64ISD::SST1_UXTW_SCALED_PRED", SDT_AArch64_SCATTER_SV, [SDNPHasChain, SDNPMayStore]>;
def AArch64st1_scatter_sxtw_scaled : SDNode<"AArch64ISD::SST1_SXTW_SCALED_PRED", SDT_AArch64_SCATTER_SV, [SDNPHasChain, SDNPMayStore]>;
def AArch64st1_scatter_imm         : SDNode<"AArch64ISD::SST1_IMM_PRED",         SDT_AArch64_SCATTER_VS, [SDNPHasChain, SDNPMayStore]>;

def AArch64stnt1_scatter : SDNode<"AArch64ISD::SSTNT1_PRED", SDT_AArch64_SCATTER_VS, [SDNPHasChain, SDNPMayStore]>;

// Scatter vector base + scalar offset
def AArch64st1q_scatter : SDNode<"AArch64ISD::SST1Q_PRED", SDT_AArch64_SCATTER_VS, [SDNPHasChain, SDNPMayStore]>;

// AArch64 SVE/SVE2 - the remaining node definitions
//

// SVE CNT/INC/RDVL
def sve_rdvl_imm : ComplexPattern<i64, 1, "SelectRDVLImm<-32, 31, 16>">;
def sve_cnth_imm : ComplexPattern<i64, 1, "SelectRDVLImm<1, 16, 8>">;
def sve_cntw_imm : ComplexPattern<i64, 1, "SelectRDVLImm<1, 16, 4>">;
def sve_cntd_imm : ComplexPattern<i64, 1, "SelectRDVLImm<1, 16, 2>">;

// SVE DEC
def sve_cnth_imm_neg : ComplexPattern<i64, 1, "SelectRDVLImm<1, 16, -8>">;
def sve_cntw_imm_neg : ComplexPattern<i64, 1, "SelectRDVLImm<1, 16, -4>">;
def sve_cntd_imm_neg : ComplexPattern<i64, 1, "SelectRDVLImm<1, 16, -2>">;

def SDT_AArch64Reduce : SDTypeProfile<1, 2, [SDTCisVec<1>, SDTCisVec<2>]>;
def AArch64faddv_p   : SDNode<"AArch64ISD::FADDV_PRED",   SDT_AArch64Reduce>;
def AArch64fmaxv_p   : SDNode<"AArch64ISD::FMAXV_PRED",   SDT_AArch64Reduce>;
def AArch64fmaxnmv_p : SDNode<"AArch64ISD::FMAXNMV_PRED", SDT_AArch64Reduce>;
def AArch64fminv_p   : SDNode<"AArch64ISD::FMINV_PRED",   SDT_AArch64Reduce>;
def AArch64fminnmv_p : SDNode<"AArch64ISD::FMINNMV_PRED", SDT_AArch64Reduce>;
def AArch64saddv_p   : SDNode<"AArch64ISD::SADDV_PRED",   SDT_AArch64Reduce>;
def AArch64uaddv_p   : SDNode<"AArch64ISD::UADDV_PRED",   SDT_AArch64Reduce>;
def AArch64smaxv_p   : SDNode<"AArch64ISD::SMAXV_PRED",   SDT_AArch64Reduce>;
def AArch64umaxv_p   : SDNode<"AArch64ISD::UMAXV_PRED",   SDT_AArch64Reduce>;
def AArch64sminv_p   : SDNode<"AArch64ISD::SMINV_PRED",   SDT_AArch64Reduce>;
def AArch64uminv_p   : SDNode<"AArch64ISD::UMINV_PRED",   SDT_AArch64Reduce>;
def AArch64orv_p     : SDNode<"AArch64ISD::ORV_PRED",     SDT_AArch64Reduce>;
def AArch64eorv_p    : SDNode<"AArch64ISD::EORV_PRED",    SDT_AArch64Reduce>;
def AArch64andv_p    : SDNode<"AArch64ISD::ANDV_PRED",    SDT_AArch64Reduce>;
def AArch64lasta     : SDNode<"AArch64ISD::LASTA",        SDT_AArch64Reduce>;
def AArch64lastb     : SDNode<"AArch64ISD::LASTB",        SDT_AArch64Reduce>;

def SDT_AArch64Arith : SDTypeProfile<1, 3, [
  SDTCisVec<0>, SDTCVecEltisVT<1,i1>, SDTCisSameAs<0,2>,
  SDTCisSameAs<2,3>, SDTCisSameNumEltsAs<0,1>
]>;

def SDT_AArch64FMA : SDTypeProfile<1, 4, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisVec<2>, SDTCisVec<3>, SDTCisVec<4>,
  SDTCVecEltisVT<1,i1>, SDTCisSameNumEltsAs<0,1>,
  SDTCisSameAs<0,2>, SDTCisSameAs<0,3>, SDTCisSameAs<0,4>
]>;

// Predicated operations with the result of inactive lanes being unspecified.
def AArch64asr_p  : SDNode<"AArch64ISD::SRA_PRED",  SDT_AArch64Arith>;
def AArch64fadd_p : SDNode<"AArch64ISD::FADD_PRED", SDT_AArch64Arith>;
def AArch64fdiv_p : SDNode<"AArch64ISD::FDIV_PRED", SDT_AArch64Arith>;
def AArch64fma_p  : SDNode<"AArch64ISD::FMA_PRED",  SDT_AArch64FMA>;
def AArch64fmax_p : SDNode<"AArch64ISD::FMAX_PRED", SDT_AArch64Arith>;
def AArch64fmaxnm_p : SDNode<"AArch64ISD::FMAXNM_PRED", SDT_AArch64Arith>;
def AArch64fmin_p : SDNode<"AArch64ISD::FMIN_PRED", SDT_AArch64Arith>;
def AArch64fminnm_p : SDNode<"AArch64ISD::FMINNM_PRED", SDT_AArch64Arith>;
def AArch64fmul_p : SDNode<"AArch64ISD::FMUL_PRED", SDT_AArch64Arith>;
def AArch64fsub_p : SDNode<"AArch64ISD::FSUB_PRED", SDT_AArch64Arith>;
def AArch64lsl_p  : SDNode<"AArch64ISD::SHL_PRED",  SDT_AArch64Arith>;
def AArch64lsr_p  : SDNode<"AArch64ISD::SRL_PRED",  SDT_AArch64Arith>;
def AArch64mul_p  : SDNode<"AArch64ISD::MUL_PRED",  SDT_AArch64Arith>;
def AArch64sabd_p : SDNode<"AArch64ISD::ABDS_PRED", SDT_AArch64Arith>;
def AArch64shadd_p : SDNode<"AArch64ISD::HADDS_PRED", SDT_AArch64Arith>;
def AArch64srhadd_p : SDNode<"AArch64ISD::RHADDS_PRED", SDT_AArch64Arith>;
def AArch64sdiv_p : SDNode<"AArch64ISD::SDIV_PRED", SDT_AArch64Arith>;
def AArch64smax_p : SDNode<"AArch64ISD::SMAX_PRED", SDT_AArch64Arith>;
def AArch64smin_p : SDNode<"AArch64ISD::SMIN_PRED", SDT_AArch64Arith>;
def AArch64smulh_p : SDNode<"AArch64ISD::MULHS_PRED", SDT_AArch64Arith>;
def AArch64uabd_p : SDNode<"AArch64ISD::ABDU_PRED", SDT_AArch64Arith>;
def AArch64uhadd_p : SDNode<"AArch64ISD::HADDU_PRED", SDT_AArch64Arith>;
def AArch64urhadd_p : SDNode<"AArch64ISD::RHADDU_PRED", SDT_AArch64Arith>;
def AArch64udiv_p : SDNode<"AArch64ISD::UDIV_PRED", SDT_AArch64Arith>;
def AArch64umax_p : SDNode<"AArch64ISD::UMAX_PRED", SDT_AArch64Arith>;
def AArch64umin_p : SDNode<"AArch64ISD::UMIN_PRED", SDT_AArch64Arith>;
def AArch64umulh_p : SDNode<"AArch64ISD::MULHU_PRED", SDT_AArch64Arith>;

def AArch64fadd_p_contract : PatFrag<(ops node:$op1, node:$op2, node:$op3),
                                     (AArch64fadd_p node:$op1, node:$op2, node:$op3), [{
  return N->getFlags().hasAllowContract();
}]>;
def AArch64fadd_p_nsz : PatFrag<(ops node:$op1, node:$op2, node:$op3),
                                (AArch64fadd_p node:$op1, node:$op2, node:$op3), [{
  return N->getFlags().hasNoSignedZeros();
}]>;
def AArch64fsub_p_contract : PatFrag<(ops node:$op1, node:$op2, node:$op3),
                                     (AArch64fsub_p node:$op1, node:$op2, node:$op3), [{
  return N->getFlags().hasAllowContract();
}]>;
def AArch64fsub_p_nsz : PatFrag<(ops node:$op1, node:$op2, node:$op3),
                                (AArch64fsub_p node:$op1, node:$op2, node:$op3), [{
  return N->getFlags().hasNoSignedZeros();
}]>;

def SDT_AArch64Arith_Imm : SDTypeProfile<1, 3, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisVec<2>, SDTCisVT<3,i32>,
  SDTCVecEltisVT<1,i1>, SDTCisSameAs<0,2>
]>;

def AArch64asrd_m1 : SDNode<"AArch64ISD::SRAD_MERGE_OP1", SDT_AArch64Arith_Imm>;
def AArch64urshri_p_node : SDNode<"AArch64ISD::URSHR_I_PRED", SDT_AArch64Arith_Imm>;

def AArch64urshri_p : PatFrags<(ops node:$op1, node:$op2, node:$op3),
                           [(int_aarch64_sve_urshr node:$op1, node:$op2, node:$op3),
                            (AArch64urshri_p_node node:$op1, node:$op2, node:$op3)]>;

def SDT_AArch64IntExtend : SDTypeProfile<1, 4, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisVec<2>, SDTCisVT<3, OtherVT>, SDTCisVec<4>,
  SDTCVecEltisVT<1,i1>, SDTCisSameAs<0,2>, SDTCisVTSmallerThanOp<3, 2>, SDTCisSameAs<0,4>
]>;

// Predicated operations with the result of inactive lanes provided by the last operand.
def AArch64clz_mt    : SDNode<"AArch64ISD::CTLZ_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64cnt_mt    : SDNode<"AArch64ISD::CTPOP_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64fneg_mt   : SDNode<"AArch64ISD::FNEG_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64fabs_mt   : SDNode<"AArch64ISD::FABS_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64abs_mt    : SDNode<"AArch64ISD::ABS_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64neg_mt    : SDNode<"AArch64ISD::NEG_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64sxt_mt    : SDNode<"AArch64ISD::SIGN_EXTEND_INREG_MERGE_PASSTHRU", SDT_AArch64IntExtend>;
def AArch64uxt_mt    : SDNode<"AArch64ISD::ZERO_EXTEND_INREG_MERGE_PASSTHRU", SDT_AArch64IntExtend>;
def AArch64frintp_mt : SDNode<"AArch64ISD::FCEIL_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64frintm_mt : SDNode<"AArch64ISD::FFLOOR_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64frinti_mt : SDNode<"AArch64ISD::FNEARBYINT_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64frintx_mt : SDNode<"AArch64ISD::FRINT_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64frinta_mt : SDNode<"AArch64ISD::FROUND_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64frintn_mt : SDNode<"AArch64ISD::FROUNDEVEN_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64frintz_mt : SDNode<"AArch64ISD::FTRUNC_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64fsqrt_mt  : SDNode<"AArch64ISD::FSQRT_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64frecpx_mt : SDNode<"AArch64ISD::FRECPX_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64rbit_mt   : SDNode<"AArch64ISD::BITREVERSE_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64revb_mt   : SDNode<"AArch64ISD::BSWAP_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64revh_mt   : SDNode<"AArch64ISD::REVH_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64revw_mt   : SDNode<"AArch64ISD::REVW_MERGE_PASSTHRU", SDT_AArch64Arith>;
def AArch64revd_mt   : SDNode<"AArch64ISD::REVD_MERGE_PASSTHRU", SDT_AArch64Arith>;

def AArch64fneg_mt_nsz : PatFrag<(ops node:$pred, node:$op, node:$pt),
                                 (AArch64fneg_mt node:$pred, node:$op, node:$pt), [{
  return N->getFlags().hasNoSignedZeros();
}]>;

// These are like the above but we don't yet have need for ISD nodes. They allow
// a single pattern to match intrinsic and ISD operand layouts.
def AArch64cls_mt  : PatFrags<(ops node:$pg, node:$op, node:$pt), [(int_aarch64_sve_cls  node:$pt, node:$pg, node:$op)]>;
def AArch64cnot_mt : PatFrags<(ops node:$pg, node:$op, node:$pt), [(int_aarch64_sve_cnot node:$pt, node:$pg, node:$op)]>;
def AArch64not_mt  : PatFrags<(ops node:$pg, node:$op, node:$pt), [(int_aarch64_sve_not  node:$pt, node:$pg, node:$op)]>;

def AArch64fmul_m1 : VSelectPredOrPassthruPatFrags<int_aarch64_sve_fmul, AArch64fmul_p>;
def AArch64fadd_m1 : PatFrags<(ops node:$pg, node:$op1, node:$op2), [
    (int_aarch64_sve_fadd node:$pg, node:$op1, node:$op2),
    (vselect node:$pg, (AArch64fadd_p (SVEAllActive), node:$op1, node:$op2), node:$op1),
    (AArch64fadd_p_nsz (SVEAllActive), node:$op1, (vselect node:$pg, node:$op2, (SVEDup0))),
    (AArch64fadd_p (SVEAllActive), node:$op1, (vselect node:$pg, node:$op2, (SVEDupNeg0)))
]>;
def AArch64fsub_m1 : PatFrags<(ops node:$pg, node:$op1, node:$op2), [
    (int_aarch64_sve_fsub node:$pg, node:$op1, node:$op2),
    (vselect node:$pg, (AArch64fsub_p (SVEAllActive), node:$op1, node:$op2), node:$op1),
    (AArch64fsub_p (SVEAllActive), node:$op1, (vselect node:$pg, node:$op2, (SVEDup0))),
    (AArch64fsub_p_nsz (SVEAllActive), node:$op1, (vselect node:$pg, node:$op2, (SVEDupNeg0)))
]>;

def AArch64shadd : PatFrags<(ops node:$pg, node:$op1, node:$op2),
                            [(int_aarch64_sve_shadd node:$pg, node:$op1, node:$op2),
                             (AArch64shadd_p node:$pg, node:$op1, node:$op2)]>;
def AArch64uhadd : PatFrags<(ops node:$pg, node:$op1, node:$op2),
                            [(int_aarch64_sve_uhadd node:$pg, node:$op1, node:$op2),
                             (AArch64uhadd_p node:$pg, node:$op1, node:$op2)]>;
def AArch64srhadd : PatFrags<(ops node:$pg, node:$op1, node:$op2),
                            [(int_aarch64_sve_srhadd node:$pg, node:$op1, node:$op2),
                             (AArch64srhadd_p node:$pg, node:$op1, node:$op2)]>;
def AArch64urhadd : PatFrags<(ops node:$pg, node:$op1, node:$op2),
                            [(int_aarch64_sve_urhadd node:$pg, node:$op1, node:$op2),
                             (AArch64urhadd_p node:$pg, node:$op1, node:$op2)]>;

def AArch64saba : PatFrags<(ops node:$op1, node:$op2, node:$op3),
                           [(int_aarch64_sve_saba node:$op1, node:$op2, node:$op3),
                            (add node:$op1, (AArch64sabd_p (SVEAllActive), node:$op2, node:$op3))]>;

def AArch64uaba : PatFrags<(ops node:$op1, node:$op2, node:$op3),
                           [(int_aarch64_sve_uaba node:$op1, node:$op2, node:$op3),
                            (add node:$op1, (AArch64uabd_p (SVEAllActive), node:$op2, node:$op3))]>;

def AArch64usra : PatFrags<(ops node:$op1, node:$op2, node:$op3),
                           [(int_aarch64_sve_usra node:$op1, node:$op2, node:$op3),
                            (add node:$op1, (AArch64lsr_p (SVEAnyPredicate), node:$op2, (SVEShiftSplatImmR (i32 node:$op3))))]>;

def AArch64ssra : PatFrags<(ops node:$op1, node:$op2, node:$op3),
                           [(int_aarch64_sve_ssra node:$op1, node:$op2, node:$op3),
                            (add node:$op1, (AArch64asr_p (SVEAnyPredicate), node:$op2, (SVEShiftSplatImmR (i32 node:$op3))))]>;

// Replace pattern min(max(v1,v2),v3) by clamp
def AArch64sclamp : PatFrags<(ops node:$Zd, node:$Zn, node:$Zm),
                              [(int_aarch64_sve_sclamp node:$Zd, node:$Zn, node:$Zm),
                              (AArch64smin_p (SVEAllActive),
                                  (AArch64smax_p (SVEAllActive), node:$Zd, node:$Zn),
                                  node:$Zm)
                               ]>;
def AArch64uclamp : PatFrags<(ops node:$Zd, node:$Zn, node:$Zm),
                              [(int_aarch64_sve_uclamp node:$Zd, node:$Zn, node:$Zm),
                               (AArch64umin_p (SVEAllActive),
                                  (AArch64umax_p (SVEAllActive), node:$Zd, node:$Zn),
                                  node:$Zm)
                              ]>;
def AArch64fclamp : PatFrags<(ops node:$Zd, node:$Zn, node:$Zm),
                              [(int_aarch64_sve_fclamp node:$Zd, node:$Zn, node:$Zm),
                              (AArch64fminnm_p (SVEAllActive),
                                  (AArch64fmaxnm_p (SVEAllActive), node:$Zd, node:$Zn),
                               node:$Zm)
                               ]>;

def SDT_AArch64FCVT : SDTypeProfile<1, 3, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisVec<2>, SDTCisVec<3>,
  SDTCVecEltisVT<1,i1>
]>;

def SDT_AArch64FCVTR : SDTypeProfile<1, 4, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisVec<2>, SDTCisInt<3>, SDTCisVec<4>,
  SDTCVecEltisVT<1,i1>
]>;

def AArch64fcvtr_mt  : SDNode<"AArch64ISD::FP_ROUND_MERGE_PASSTHRU", SDT_AArch64FCVTR>;
def AArch64fcvte_mt  : SDNode<"AArch64ISD::FP_EXTEND_MERGE_PASSTHRU", SDT_AArch64FCVT>;
def AArch64ucvtf_mt  : SDNode<"AArch64ISD::UINT_TO_FP_MERGE_PASSTHRU", SDT_AArch64FCVT>;
def AArch64scvtf_mt  : SDNode<"AArch64ISD::SINT_TO_FP_MERGE_PASSTHRU", SDT_AArch64FCVT>;
def AArch64fcvtzu_mt : SDNode<"AArch64ISD::FCVTZU_MERGE_PASSTHRU", SDT_AArch64FCVT>;
def AArch64fcvtzs_mt : SDNode<"AArch64ISD::FCVTZS_MERGE_PASSTHRU", SDT_AArch64FCVT>;

def SDT_AArch64ReduceWithInit : SDTypeProfile<1, 3,
   [SDTCisVec<1>, SDTCVecEltisVT<1,i1>, SDTCisVec<3>, SDTCisSameNumEltsAs<1,3>]>;
def AArch64clasta_n     : SDNode<"AArch64ISD::CLASTA_N",   SDT_AArch64ReduceWithInit>;
def AArch64clastb_n     : SDNode<"AArch64ISD::CLASTB_N",   SDT_AArch64ReduceWithInit>;
def AArch64fadda_p_node : SDNode<"AArch64ISD::FADDA_PRED", SDT_AArch64ReduceWithInit>;

def AArch64fadda_p : PatFrags<(ops node:$op1, node:$op2, node:$op3),
    [(AArch64fadda_p_node node:$op1, node:$op2, node:$op3),
     (AArch64fadda_p_node (SVEAllActive), node:$op2,
             (vselect node:$op1, node:$op3, (splat_vector (f16 fpimm_minus0)))),
     (AArch64fadda_p_node (SVEAllActive), node:$op2,
             (vselect node:$op1, node:$op3, (splat_vector (f32 fpimm_minus0)))),
     (AArch64fadda_p_node (SVEAllActive), node:$op2,
             (vselect node:$op1, node:$op3, (splat_vector (f64 fpimm_minus0))))]>;

def SDT_AArch64PTest : SDTypeProfile<0, 2, [SDTCisVec<0>, SDTCisSameAs<0,1>]>;
def AArch64ptest     : SDNode<"AArch64ISD::PTEST", SDT_AArch64PTest>;
def AArch64ptest_any : SDNode<"AArch64ISD::PTEST_ANY", SDT_AArch64PTest>;

def SDT_AArch64DUP_PRED  : SDTypeProfile<1, 3,
  [SDTCisVec<0>, SDTCisSameAs<0, 3>, SDTCisVec<1>, SDTCVecEltisVT<1,i1>, SDTCisSameNumEltsAs<0, 1>]>;
def AArch64dup_mt : SDNode<"AArch64ISD::DUP_MERGE_PASSTHRU", SDT_AArch64DUP_PRED>;

def AArch64splice : SDNode<"AArch64ISD::SPLICE", SDT_AArch64Arith>;

def reinterpret_cast : SDNode<"AArch64ISD::REINTERPRET_CAST", SDTUnaryOp>;

def AArch64mul_p_oneuse : PatFrag<(ops node:$pred, node:$src1, node:$src2),
                                  (AArch64mul_p node:$pred, node:$src1, node:$src2), [{
  return N->hasOneUse();
}]>;

def AArch64fmul_p_oneuse : PatFrag<(ops node:$pred, node:$src1, node:$src2),
                                   (AArch64fmul_p node:$pred, node:$src1, node:$src2), [{
  return N->hasOneUse();
}]>;


def AArch64fabd_p : PatFrags<(ops node:$pg, node:$op1, node:$op2),
                             [(int_aarch64_sve_fabd_u node:$pg, node:$op1, node:$op2),
                              (AArch64fabs_mt node:$pg, (AArch64fsub_p node:$pg, node:$op1, node:$op2), undef)]>;

def AArch64fmla_p : PatFrags<(ops node:$pg, node:$za, node:$zn, node:$zm),
                             [(AArch64fma_p node:$pg, node:$zn, node:$zm, node:$za)]>;

def AArch64fmls_p : PatFrags<(ops node:$pg, node:$za, node:$zn, node:$zm),
                             [(int_aarch64_sve_fmls_u node:$pg, node:$za, node:$zn, node:$zm),
                              (AArch64fma_p node:$pg, (AArch64fneg_mt node:$pg, node:$zn, (undef)), node:$zm, node:$za),
                              (AArch64fma_p node:$pg, node:$zm, (AArch64fneg_mt node:$pg, node:$zn, (undef)), node:$za)]>;

def AArch64fnmla_p : PatFrags<(ops node:$pg, node:$za, node:$zn, node:$zm),
                              [(int_aarch64_sve_fnmla_u node:$pg, node:$za, node:$zn, node:$zm),
                               (AArch64fma_p node:$pg, (AArch64fneg_mt node:$pg, node:$zn, (undef)), node:$zm, (AArch64fneg_mt node:$pg, node:$za, (undef))),
                               (AArch64fneg_mt_nsz node:$pg, (AArch64fma_p node:$pg, node:$zn, node:$zm, node:$za), (undef))]>;

def AArch64fnmls_p : PatFrags<(ops node:$pg, node:$za, node:$zn, node:$zm),
                              [(int_aarch64_sve_fnmls_u node:$pg, node:$za, node:$zn, node:$zm),
                               (AArch64fma_p node:$pg, node:$zn, node:$zm, (AArch64fneg_mt node:$pg, node:$za, (undef)))]>;

def AArch64fsubr_p : PatFrag<(ops node:$pg, node:$op1, node:$op2),
                             (AArch64fsub_p node:$pg, node:$op2, node:$op1)>;

def SDT_AArch64Arith_Unpred : SDTypeProfile<1, 2, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisVec<2>,
  SDTCisSameAs<0,1>, SDTCisSameAs<1,2>
]>;

def AArch64bic_node : SDNode<"AArch64ISD::BIC",  SDT_AArch64Arith_Unpred>;

def AArch64bic : PatFrags<(ops node:$op1, node:$op2),
                          [(and node:$op1, (xor node:$op2, (splat_vector (i32 -1)))),
                           (and node:$op1, (xor node:$op2, (splat_vector (i64 -1)))),
                           (and node:$op1, (xor node:$op2, (SVEAllActive))),
                           (AArch64bic_node node:$op1, node:$op2)]>;

def AArch64subr : PatFrag<(ops node:$op1, node:$op2),
                          (sub node:$op2, node:$op1)>;
def AArch64mla_m1 : PatFrags<(ops node:$pred, node:$op1, node:$op2, node:$op3),
                             [(int_aarch64_sve_mla node:$pred, node:$op1, node:$op2, node:$op3),
                              (vselect node:$pred, (add node:$op1, (AArch64mul_p_oneuse (SVEAllActive), node:$op2, node:$op3)), node:$op1)]>;
def AArch64mla_p : PatFrags<(ops node:$pred, node:$op1, node:$op2, node:$op3),
                            [(int_aarch64_sve_mla_u node:$pred, node:$op1, node:$op2, node:$op3),
                             (add node:$op1, (AArch64mul_p_oneuse node:$pred, node:$op2, node:$op3))]>;
def AArch64mad_m1 : PatFrags<(ops node:$pred, node:$op1, node:$op2, node:$op3),
                             [(int_aarch64_sve_mad node:$pred, node:$op1, node:$op2, node:$op3),
                              (vselect node:$pred, (add node:$op3, (AArch64mul_p_oneuse (SVEAllActive), node:$op1, node:$op2)), node:$op1),
                              (vselect node:$pred, (add node:$op3, (AArch64mul_p_oneuse (SVEAllActive), node:$op2, node:$op1)), node:$op1)]>;
def AArch64mls_m1 : PatFrags<(ops node:$pred, node:$op1, node:$op2, node:$op3),
                             [(int_aarch64_sve_mls node:$pred, node:$op1, node:$op2, node:$op3),
                              (vselect node:$pred, (sub node:$op1, (AArch64mul_p_oneuse (SVEAllActive), node:$op2, node:$op3)), node:$op1)]>;
def AArch64mls_p : PatFrags<(ops node:$pred, node:$op1, node:$op2, node:$op3),
                             [(int_aarch64_sve_mls_u node:$pred, node:$op1, node:$op2, node:$op3),
                              (sub node:$op1, (AArch64mul_p_oneuse node:$pred, node:$op2, node:$op3))]>;
def AArch64msb_m1 : PatFrags<(ops node:$pred, node:$op1, node:$op2, node:$op3),
                             [(int_aarch64_sve_msb node:$pred, node:$op1, node:$op2, node:$op3),
                              (vselect node:$pred, (sub node:$op3, (AArch64mul_p_oneuse (SVEAllActive), node:$op1, node:$op2)), node:$op1),
                              (vselect node:$pred, (sub node:$op3, (AArch64mul_p_oneuse (SVEAllActive), node:$op2, node:$op1)), node:$op1)]>;
def AArch64eor3 : PatFrags<(ops node:$op1, node:$op2, node:$op3),
                           [(int_aarch64_sve_eor3 node:$op1, node:$op2, node:$op3),
                            (xor node:$op1, (xor node:$op2, node:$op3))]>;
def AArch64bcax : PatFrags<(ops node:$op1, node:$op2, node:$op3),
                           [(int_aarch64_sve_bcax node:$op1, node:$op2, node:$op3),
                            (xor node:$op1, (and node:$op2, (vnot node:$op3)))]>;

def AArch64fmla_m1 : PatFrags<(ops node:$pg, node:$za, node:$zn, node:$zm),
                              [(int_aarch64_sve_fmla node:$pg, node:$za, node:$zn, node:$zm),
                               (vselect node:$pg, (AArch64fadd_p_contract (SVEAllActive), node:$za, (AArch64fmul_p_oneuse (SVEAllActive), node:$zn, node:$zm)), node:$za),
                               (vselect node:$pg, (AArch64fma_p (SVEAllActive), node:$zn, node:$zm, node:$za), node:$za)]>;

def AArch64fmls_m1 : PatFrags<(ops node:$pg, node:$za, node:$zn, node:$zm),
                              [(int_aarch64_sve_fmls node:$pg, node:$za, node:$zn, node:$zm),
                               (vselect node:$pg, (AArch64fsub_p_contract (SVEAllActive), node:$za, (AArch64fmul_p_oneuse (SVEAllActive), node:$zn, node:$zm)), node:$za),
                               (vselect node:$pg, (AArch64fma_p (SVEAllActive), (AArch64fneg_mt (SVEAllActive), node:$zn, (undef)), node:$zm, node:$za), node:$za)]>;

def AArch64add_m1 : VSelectUnpredOrPassthruPatFrags<int_aarch64_sve_add, add>;
def AArch64sub_m1 : VSelectUnpredOrPassthruPatFrags<int_aarch64_sve_sub, sub>;
def AArch64mul_m1 : VSelectCommPredOrPassthruPatFrags<int_aarch64_sve_mul, AArch64mul_p>;
def AArch64and_m1 : VSelectUnpredOrPassthruPatFrags<int_aarch64_sve_and, and>;
def AArch64orr_m1 : VSelectUnpredOrPassthruPatFrags<int_aarch64_sve_orr, or>;
def AArch64eor_m1 : VSelectUnpredOrPassthruPatFrags<int_aarch64_sve_eor, xor>;
def AArch64smax_m1 : VSelectCommPredOrPassthruPatFrags<int_aarch64_sve_smax, AArch64smax_p>;
def AArch64umax_m1 : VSelectCommPredOrPassthruPatFrags<int_aarch64_sve_umax, AArch64umax_p>;
def AArch64smin_m1 : VSelectCommPredOrPassthruPatFrags<int_aarch64_sve_smin, AArch64smin_p>;
def AArch64umin_m1 : VSelectCommPredOrPassthruPatFrags<int_aarch64_sve_umin, AArch64umin_p>;
def AArch64fminnm_m1 : VSelectCommPredOrPassthruPatFrags<int_aarch64_sve_fminnm, AArch64fminnm_p>;
def AArch64fmaxnm_m1 : VSelectCommPredOrPassthruPatFrags<int_aarch64_sve_fmaxnm, AArch64fmaxnm_p>;
def AArch64fmin_m1 : VSelectCommPredOrPassthruPatFrags<int_aarch64_sve_fmin, AArch64fmin_p>;
def AArch64fmax_m1 : VSelectCommPredOrPassthruPatFrags<int_aarch64_sve_fmax, AArch64fmax_p>;

def AArch64fadd : PatFrags<(ops node:$op1, node:$op2),
                            [(fadd node:$op1, node:$op2),
                             (AArch64fadd_p (SVEAllActive), node:$op1, node:$op2)]>;

def AArch64fmul : PatFrags<(ops node:$op1, node:$op2),
                            [(fmul node:$op1, node:$op2),
                             (AArch64fmul_p (SVEAllActive), node:$op1, node:$op2)]>;

def AArch64fsub : PatFrags<(ops node:$op1, node:$op2),
                            [(fsub node:$op1, node:$op2),
                             (AArch64fsub_p (SVEAllActive), node:$op1, node:$op2)]>;

def AArch64mul : PatFrag<(ops node:$op1, node:$op2),
                         (AArch64mul_p (SVEAnyPredicate), node:$op1, node:$op2)>;

def AArch64smulh : PatFrag<(ops node:$op1, node:$op2),
                           (AArch64smulh_p (SVEAnyPredicate), node:$op1, node:$op2)>;

def AArch64umulh : PatFrag<(ops node:$op1, node:$op2),
                           (AArch64umulh_p (SVEAnyPredicate), node:$op1, node:$op2)>;


def AArch64bsl  : PatFrags<(ops node:$Op1, node:$Op2, node:$Op3),
                           [(int_aarch64_sve_bsl node:$Op1, node:$Op2, node:$Op3),
                            (AArch64bsp node:$Op3, node:$Op1, node:$Op2)]>;

def AArch64nbsl : PatFrags<(ops node:$Op1, node:$Op2, node:$Op3),
                           [(int_aarch64_sve_nbsl node:$Op1, node:$Op2, node:$Op3),
                            (vnot (AArch64bsp node:$Op3, node:$Op1, node:$Op2))]>;


let Predicates = [HasSVE] in {
  def RDFFR_PPz  : sve_int_rdffr_pred<0b0, "rdffr", int_aarch64_sve_rdffr_z>;
  def RDFFRS_PPz : sve_int_rdffr_pred<0b1, "rdffrs">;
  def RDFFR_P    : sve_int_rdffr_unpred<"rdffr", int_aarch64_sve_rdffr>;
  def SETFFR     : sve_int_setffr<"setffr", int_aarch64_sve_setffr>;
  def WRFFR      : sve_int_wrffr<"wrffr", int_aarch64_sve_wrffr>;
} // End HasSVE

let Predicates = [HasSVEorSME] in {
  defm ADD_ZZZ   : sve_int_bin_cons_arit_0<0b000, "add", add>;
  defm SUB_ZZZ   : sve_int_bin_cons_arit_0<0b001, "sub", sub>;
  defm SQADD_ZZZ : sve_int_bin_cons_arit_0<0b100, "sqadd", saddsat>;
  defm UQADD_ZZZ : sve_int_bin_cons_arit_0<0b101, "uqadd", uaddsat>;
  defm SQSUB_ZZZ : sve_int_bin_cons_arit_0<0b110, "sqsub", ssubsat>;
  defm UQSUB_ZZZ : sve_int_bin_cons_arit_0<0b111, "uqsub", usubsat>;

  defm AND_ZZZ : sve_int_bin_cons_log<0b00, "and", and>;
  defm ORR_ZZZ : sve_int_bin_cons_log<0b01, "orr", or>;
  defm EOR_ZZZ : sve_int_bin_cons_log<0b10, "eor", xor>;
  defm BIC_ZZZ : sve_int_bin_cons_log<0b11, "bic", AArch64bic>;

  defm ADD_ZPmZ  : sve_int_bin_pred_arit_0<0b000, "add",  "ADD_ZPZZ", AArch64add_m1, DestructiveBinaryComm>;
  defm SUB_ZPmZ  : sve_int_bin_pred_arit_0<0b001, "sub",  "SUB_ZPZZ", AArch64sub_m1, DestructiveBinaryCommWithRev, "SUBR_ZPmZ">;
  defm SUBR_ZPmZ : sve_int_bin_pred_arit_0<0b011, "subr", "SUBR_ZPZZ", int_aarch64_sve_subr, DestructiveBinaryCommWithRev, "SUB_ZPmZ", /*isReverseInstr*/ 1>;

  defm ORR_ZPmZ : sve_int_bin_pred_log<0b000, "orr", "ORR_ZPZZ", AArch64orr_m1, DestructiveBinaryComm>;
  defm EOR_ZPmZ : sve_int_bin_pred_log<0b001, "eor", "EOR_ZPZZ", AArch64eor_m1, DestructiveBinaryComm>;
  defm AND_ZPmZ : sve_int_bin_pred_log<0b010, "and", "AND_ZPZZ", AArch64and_m1, DestructiveBinaryComm>;
  defm BIC_ZPmZ : sve_int_bin_pred_log<0b011, "bic", "BIC_ZPZZ", int_aarch64_sve_bic, DestructiveBinary>;
} // End HasSVEorSME

let Predicates = [HasSVEorSME, UseExperimentalZeroingPseudos] in {
  defm ADD_ZPZZ  : sve_int_bin_pred_zeroing_bhsd<int_aarch64_sve_add>;
  defm SUB_ZPZZ  : sve_int_bin_pred_zeroing_bhsd<int_aarch64_sve_sub>;
  defm SUBR_ZPZZ : sve_int_bin_pred_zeroing_bhsd<int_aarch64_sve_subr>;

  defm ORR_ZPZZ  : sve_int_bin_pred_zeroing_bhsd<int_aarch64_sve_orr>;
  defm EOR_ZPZZ  : sve_int_bin_pred_zeroing_bhsd<int_aarch64_sve_eor>;
  defm AND_ZPZZ  : sve_int_bin_pred_zeroing_bhsd<int_aarch64_sve_and>;
  defm BIC_ZPZZ  : sve_int_bin_pred_zeroing_bhsd<int_aarch64_sve_bic>;
} // End HasSVEorSME, UseExperimentalZeroingPseudos

let Predicates = [HasSVEorSME] in {
  defm ADD_ZI   : sve_int_arith_imm0<0b000, "add", add>;
  defm SUB_ZI   : sve_int_arith_imm0<0b001, "sub", sub>;
  defm SUBR_ZI  : sve_int_arith_imm0<0b011, "subr", AArch64subr>;
  defm SQADD_ZI : sve_int_arith_imm0_ssat<0b100, "sqadd", saddsat, ssubsat>;
  defm UQADD_ZI : sve_int_arith_imm0<0b101, "uqadd", uaddsat>;
  defm SQSUB_ZI : sve_int_arith_imm0_ssat<0b110, "sqsub", ssubsat, saddsat>;
  defm UQSUB_ZI : sve_int_arith_imm0<0b111, "uqsub", usubsat>;

  defm MAD_ZPmZZ : sve_int_mladdsub_vvv_pred<0b0, "mad", AArch64mad_m1, "MLA_ZPmZZ", /*isReverseInstr*/ 1>;
  defm MSB_ZPmZZ : sve_int_mladdsub_vvv_pred<0b1, "msb", AArch64msb_m1, "MLS_ZPmZZ", /*isReverseInstr*/ 1>;
  defm MLA_ZPmZZ : sve_int_mlas_vvv_pred<0b0, "mla", AArch64mla_m1, "MLA_ZPZZZ", "MAD_ZPmZZ">;
  defm MLS_ZPmZZ : sve_int_mlas_vvv_pred<0b1, "mls", AArch64mls_m1, "MLS_ZPZZZ", "MSB_ZPmZZ">;

  defm MLA_ZPZZZ : sve_int_3op_p_mladdsub<AArch64mla_p>;
  defm MLS_ZPZZZ : sve_int_3op_p_mladdsub<AArch64mls_p>;

  // SVE predicated integer reductions.
  defm SADDV_VPZ : sve_int_reduce_0_saddv<0b000, "saddv", AArch64saddv_p>;
  defm UADDV_VPZ : sve_int_reduce_0_uaddv<0b001, "uaddv", AArch64uaddv_p>;
  defm SMAXV_VPZ : sve_int_reduce_1<0b000, "smaxv", AArch64smaxv_p>;
  defm UMAXV_VPZ : sve_int_reduce_1<0b001, "umaxv", AArch64umaxv_p>;
  defm SMINV_VPZ : sve_int_reduce_1<0b010, "sminv", AArch64sminv_p>;
  defm UMINV_VPZ : sve_int_reduce_1<0b011, "uminv", AArch64uminv_p>;
  defm ORV_VPZ   : sve_int_reduce_2<0b000, "orv",   AArch64orv_p>;
  defm EORV_VPZ  : sve_int_reduce_2<0b001, "eorv",  AArch64eorv_p>;
  defm ANDV_VPZ  : sve_int_reduce_2<0b010, "andv",  AArch64andv_p>;

  defm ORR_ZI : sve_int_log_imm<0b00, "orr", "orn", or>;
  defm EOR_ZI : sve_int_log_imm<0b01, "eor", "eon", xor>;
  defm AND_ZI : sve_int_log_imm<0b10, "and", "bic", and>;
  defm BIC_ZI : sve_int_log_imm_bic<AArch64bic>;

  defm SMAX_ZI   : sve_int_arith_imm1<0b00, "smax", AArch64smax_p>;
  defm SMIN_ZI   : sve_int_arith_imm1<0b10, "smin", AArch64smin_p>;
  defm UMAX_ZI   : sve_int_arith_imm1_unsigned<0b01, "umax", AArch64umax_p>;
  defm UMIN_ZI   : sve_int_arith_imm1_unsigned<0b11, "umin", AArch64umin_p>;

  defm MUL_ZI     : sve_int_arith_imm2<"mul", AArch64mul_p>;
  defm MUL_ZPmZ   : sve_int_bin_pred_arit_2<0b000, "mul",   "MUL_ZPZZ",   AArch64mul_m1,         DestructiveBinaryComm>;
  defm SMULH_ZPmZ : sve_int_bin_pred_arit_2<0b010, "smulh", "SMULH_ZPZZ", int_aarch64_sve_smulh, DestructiveBinaryComm>;
  defm UMULH_ZPmZ : sve_int_bin_pred_arit_2<0b011, "umulh", "UMULH_ZPZZ", int_aarch64_sve_umulh, DestructiveBinaryComm>;

  defm MUL_ZPZZ   : sve_int_bin_pred_bhsd<AArch64mul_p>;
  defm SMULH_ZPZZ : sve_int_bin_pred_bhsd<AArch64smulh_p>;
  defm UMULH_ZPZZ : sve_int_bin_pred_bhsd<AArch64umulh_p>;

  defm SDIV_ZPmZ  : sve_int_bin_pred_arit_2_div<0b100, "sdiv",  "SDIV_ZPZZ", int_aarch64_sve_sdiv, DestructiveBinaryCommWithRev, "SDIVR_ZPmZ">;
  defm UDIV_ZPmZ  : sve_int_bin_pred_arit_2_div<0b101, "udiv",  "UDIV_ZPZZ", int_aarch64_sve_udiv, DestructiveBinaryCommWithRev, "UDIVR_ZPmZ">;
  defm SDIVR_ZPmZ : sve_int_bin_pred_arit_2_div<0b110, "sdivr", "SDIVR_ZPZZ", int_aarch64_sve_sdivr, DestructiveBinaryCommWithRev, "SDIV_ZPmZ", /*isReverseInstr*/ 1>;
  defm UDIVR_ZPmZ : sve_int_bin_pred_arit_2_div<0b111, "udivr", "UDIVR_ZPZZ", int_aarch64_sve_udivr, DestructiveBinaryCommWithRev, "UDIV_ZPmZ", /*isReverseInstr*/ 1>;

  defm SDIV_ZPZZ  : sve_int_bin_pred_sd<AArch64sdiv_p>;
  defm UDIV_ZPZZ  : sve_int_bin_pred_sd<AArch64udiv_p>;

  defm SDOT_ZZZ : sve_intx_dot<0b0, "sdot", AArch64sdot>;
  defm UDOT_ZZZ : sve_intx_dot<0b1, "udot", AArch64udot>;

  defm SDOT_ZZZI : sve_intx_dot_by_indexed_elem<0b0, "sdot", int_aarch64_sve_sdot_lane>;
  defm UDOT_ZZZI : sve_intx_dot_by_indexed_elem<0b1, "udot", int_aarch64_sve_udot_lane>;

  defm SXTB_ZPmZ : sve_int_un_pred_arit_0_h<0b000, "sxtb", AArch64sxt_mt>;
  defm UXTB_ZPmZ : sve_int_un_pred_arit_0_h<0b001, "uxtb", AArch64uxt_mt>;
  defm SXTH_ZPmZ : sve_int_un_pred_arit_0_w<0b010, "sxth", AArch64sxt_mt>;
  defm UXTH_ZPmZ : sve_int_un_pred_arit_0_w<0b011, "uxth", AArch64uxt_mt>;
  defm SXTW_ZPmZ : sve_int_un_pred_arit_0_d<0b100, "sxtw", AArch64sxt_mt>;
  defm UXTW_ZPmZ : sve_int_un_pred_arit_0_d<0b101, "uxtw", AArch64uxt_mt>;
  defm ABS_ZPmZ  : sve_int_un_pred_arit_0<  0b110, "abs",  AArch64abs_mt>;
  defm NEG_ZPmZ  : sve_int_un_pred_arit_0<  0b111, "neg",  AArch64neg_mt>;

  defm CLS_ZPmZ  : sve_int_un_pred_arit_1<   0b000, "cls",  AArch64cls_mt>;
  defm CLZ_ZPmZ  : sve_int_un_pred_arit_1<   0b001, "clz",  AArch64clz_mt>;
  defm CNT_ZPmZ  : sve_int_un_pred_arit_1<   0b010, "cnt",  AArch64cnt_mt>;
  defm CNOT_ZPmZ : sve_int_un_pred_arit_1<   0b011, "cnot", AArch64cnot_mt>;
  defm NOT_ZPmZ  : sve_int_un_pred_arit_1<   0b110, "not",  AArch64not_mt>;
  defm FABS_ZPmZ : sve_int_un_pred_arit_1_fp<0b100, "fabs", AArch64fabs_mt>;
  defm FNEG_ZPmZ : sve_int_un_pred_arit_1_fp<0b101, "fneg", AArch64fneg_mt>;

  // zext(cmpeq(x, splat(0))) -> cnot(x)
  def : Pat<(nxv16i8 (zext (nxv16i1 (AArch64setcc_z (nxv16i1 (SVEAllActive):$Pg), nxv16i8:$Op2, (SVEDup0), SETEQ)))),
            (CNOT_ZPmZ_B $Op2, $Pg, $Op2)>;
  def : Pat<(nxv8i16 (zext (nxv8i1 (AArch64setcc_z (nxv8i1 (SVEAllActive):$Pg), nxv8i16:$Op2, (SVEDup0), SETEQ)))),
            (CNOT_ZPmZ_H $Op2, $Pg, $Op2)>;
  def : Pat<(nxv4i32 (zext (nxv4i1 (AArch64setcc_z (nxv4i1 (SVEAllActive):$Pg), nxv4i32:$Op2, (SVEDup0), SETEQ)))),
            (CNOT_ZPmZ_S $Op2, $Pg, $Op2)>;
  def : Pat<(nxv2i64 (zext (nxv2i1 (AArch64setcc_z (nxv2i1 (SVEAllActive):$Pg), nxv2i64:$Op2, (SVEDup0), SETEQ)))),
            (CNOT_ZPmZ_D $Op2, $Pg, $Op2)>;

  defm SMAX_ZPmZ : sve_int_bin_pred_arit_1<0b000, "smax", "SMAX_ZPZZ", AArch64smax_m1, DestructiveBinaryComm>;
  defm UMAX_ZPmZ : sve_int_bin_pred_arit_1<0b001, "umax", "UMAX_ZPZZ", AArch64umax_m1, DestructiveBinaryComm>;
  defm SMIN_ZPmZ : sve_int_bin_pred_arit_1<0b010, "smin", "SMIN_ZPZZ", AArch64smin_m1, DestructiveBinaryComm>;
  defm UMIN_ZPmZ : sve_int_bin_pred_arit_1<0b011, "umin", "UMIN_ZPZZ", AArch64umin_m1, DestructiveBinaryComm>;
  defm SABD_ZPmZ : sve_int_bin_pred_arit_1<0b100, "sabd", "SABD_ZPZZ", int_aarch64_sve_sabd, DestructiveBinaryComm>;
  defm UABD_ZPmZ : sve_int_bin_pred_arit_1<0b101, "uabd", "UABD_ZPZZ", int_aarch64_sve_uabd, DestructiveBinaryComm>;

  defm SMAX_ZPZZ : sve_int_bin_pred_bhsd<AArch64smax_p>;
  defm UMAX_ZPZZ : sve_int_bin_pred_bhsd<AArch64umax_p>;
  defm SMIN_ZPZZ : sve_int_bin_pred_bhsd<AArch64smin_p>;
  defm UMIN_ZPZZ : sve_int_bin_pred_bhsd<AArch64umin_p>;
  defm SABD_ZPZZ : sve_int_bin_pred_bhsd<AArch64sabd_p>;
  defm UABD_ZPZZ : sve_int_bin_pred_bhsd<AArch64uabd_p>;

  defm FRECPE_ZZ  : sve_fp_2op_u_zd<0b110, "frecpe",  AArch64frecpe>;
  defm FRSQRTE_ZZ : sve_fp_2op_u_zd<0b111, "frsqrte", AArch64frsqrte>;

  defm FADD_ZPmI    : sve_fp_2op_i_p_zds<0b000, "fadd", "FADD_ZPZI", sve_fpimm_half_one, fpimm_half, fpimm_one, int_aarch64_sve_fadd>;
  defm FSUB_ZPmI    : sve_fp_2op_i_p_zds<0b001, "fsub", "FSUB_ZPZI", sve_fpimm_half_one, fpimm_half, fpimm_one, int_aarch64_sve_fsub>;
  defm FMUL_ZPmI    : sve_fp_2op_i_p_zds<0b010, "fmul", "FMUL_ZPZI", sve_fpimm_half_two, fpimm_half, fpimm_two, int_aarch64_sve_fmul>;
  defm FSUBR_ZPmI   : sve_fp_2op_i_p_zds<0b011, "fsubr", "FSUBR_ZPZI", sve_fpimm_half_one, fpimm_half, fpimm_one, int_aarch64_sve_fsubr>;
  defm FMAXNM_ZPmI  : sve_fp_2op_i_p_zds<0b100, "fmaxnm", "FMAXNM_ZPZI", sve_fpimm_zero_one, fpimm0, fpimm_one, int_aarch64_sve_fmaxnm>;
  defm FMINNM_ZPmI  : sve_fp_2op_i_p_zds<0b101, "fminnm", "FMINNM_ZPZI", sve_fpimm_zero_one, fpimm0, fpimm_one, int_aarch64_sve_fminnm>;
  defm FMAX_ZPmI    : sve_fp_2op_i_p_zds<0b110, "fmax", "FMAX_ZPZI", sve_fpimm_zero_one, fpimm0, fpimm_one, int_aarch64_sve_fmax>;
  defm FMIN_ZPmI    : sve_fp_2op_i_p_zds<0b111, "fmin", "FMIN_ZPZI", sve_fpimm_zero_one, fpimm0, fpimm_one, int_aarch64_sve_fmin>;

  defm FADD_ZPZI    : sve_fp_2op_i_p_zds_hfd<sve_fpimm_half_one, fpimm_half, fpimm_one, AArch64fadd_p>;
  defm FSUB_ZPZI    : sve_fp_2op_i_p_zds_hfd<sve_fpimm_half_one, fpimm_half, fpimm_one, AArch64fsub_p>;
  defm FMUL_ZPZI    : sve_fp_2op_i_p_zds_hfd<sve_fpimm_half_two, fpimm_half, fpimm_two, AArch64fmul_p>;
  defm FSUBR_ZPZI   : sve_fp_2op_i_p_zds_hfd<sve_fpimm_half_one, fpimm_half, fpimm_one, AArch64fsubr_p>;
  defm FMAXNM_ZPZI  : sve_fp_2op_i_p_zds_hfd<sve_fpimm_zero_one, fpimm0, fpimm_one, AArch64fmaxnm_p>;
  defm FMINNM_ZPZI  : sve_fp_2op_i_p_zds_hfd<sve_fpimm_zero_one, fpimm0, fpimm_one, AArch64fminnm_p>;
  defm FMAX_ZPZI    : sve_fp_2op_i_p_zds_hfd<sve_fpimm_zero_one, fpimm0, fpimm_one, AArch64fmax_p>;
  defm FMIN_ZPZI    : sve_fp_2op_i_p_zds_hfd<sve_fpimm_zero_one, fpimm0, fpimm_one, AArch64fmin_p>;

  let Predicates = [HasSVE, UseExperimentalZeroingPseudos] in {
    defm FADD_ZPZI    : sve_fp_2op_i_p_zds_zeroing_hfd<sve_fpimm_half_one, fpimm_half, fpimm_one, int_aarch64_sve_fadd>;
    defm FSUB_ZPZI    : sve_fp_2op_i_p_zds_zeroing_hfd<sve_fpimm_half_one, fpimm_half, fpimm_one, int_aarch64_sve_fsub>;
    defm FMUL_ZPZI    : sve_fp_2op_i_p_zds_zeroing_hfd<sve_fpimm_half_two, fpimm_half, fpimm_two, int_aarch64_sve_fmul>;
    defm FSUBR_ZPZI   : sve_fp_2op_i_p_zds_zeroing_hfd<sve_fpimm_half_one, fpimm_half, fpimm_one, int_aarch64_sve_fsubr>;
    defm FMAXNM_ZPZI  : sve_fp_2op_i_p_zds_zeroing_hfd<sve_fpimm_zero_one, fpimm0, fpimm_one, int_aarch64_sve_fmaxnm>;
    defm FMINNM_ZPZI  : sve_fp_2op_i_p_zds_zeroing_hfd<sve_fpimm_zero_one, fpimm0, fpimm_one, int_aarch64_sve_fminnm>;
    defm FMAX_ZPZI    : sve_fp_2op_i_p_zds_zeroing_hfd<sve_fpimm_zero_one, fpimm0, fpimm_one, int_aarch64_sve_fmax>;
    defm FMIN_ZPZI    : sve_fp_2op_i_p_zds_zeroing_hfd<sve_fpimm_zero_one, fpimm0, fpimm_one, int_aarch64_sve_fmin>;
  }

  defm FADD_ZPmZ   : sve_fp_2op_p_zds<0b0000, "fadd", "FADD_ZPZZ", AArch64fadd_m1, DestructiveBinaryComm>;
  defm FSUB_ZPmZ   : sve_fp_2op_p_zds<0b0001, "fsub", "FSUB_ZPZZ", AArch64fsub_m1, DestructiveBinaryCommWithRev, "FSUBR_ZPmZ">;
  defm FMUL_ZPmZ   : sve_fp_2op_p_zds<0b0010, "fmul", "FMUL_ZPZZ", AArch64fmul_m1, DestructiveBinaryComm>;
  defm FSUBR_ZPmZ  : sve_fp_2op_p_zds<0b0011, "fsubr", "FSUBR_ZPZZ", int_aarch64_sve_fsubr, DestructiveBinaryCommWithRev, "FSUB_ZPmZ", /*isReverseInstr*/ 1>;
  defm FMAXNM_ZPmZ : sve_fp_2op_p_zds<0b0100, "fmaxnm", "FMAXNM_ZPZZ", AArch64fmaxnm_m1, DestructiveBinaryComm>;
  defm FMINNM_ZPmZ : sve_fp_2op_p_zds<0b0101, "fminnm", "FMINNM_ZPZZ", AArch64fminnm_m1, DestructiveBinaryComm>;
  defm FMAX_ZPmZ   : sve_fp_2op_p_zds<0b0110, "fmax", "FMAX_ZPZZ", AArch64fmax_m1, DestructiveBinaryComm>;
  defm FMIN_ZPmZ   : sve_fp_2op_p_zds<0b0111, "fmin", "FMIN_ZPZZ", AArch64fmin_m1, DestructiveBinaryComm>;
  defm FABD_ZPmZ   : sve_fp_2op_p_zds<0b1000, "fabd", "FABD_ZPZZ", int_aarch64_sve_fabd, DestructiveBinaryComm>;
  defm FSCALE_ZPmZ : sve_fp_2op_p_zds_fscale<0b1001, "fscale", int_aarch64_sve_fscale>;
  defm FMULX_ZPmZ  : sve_fp_2op_p_zds<0b1010, "fmulx", "FMULX_ZPZZ", int_aarch64_sve_fmulx, DestructiveBinaryComm>;
  defm FDIVR_ZPmZ  : sve_fp_2op_p_zds<0b1100, "fdivr", "FDIVR_ZPZZ", int_aarch64_sve_fdivr, DestructiveBinaryCommWithRev, "FDIV_ZPmZ", /*isReverseInstr*/ 1>;
  defm FDIV_ZPmZ   : sve_fp_2op_p_zds<0b1101, "fdiv", "FDIV_ZPZZ", int_aarch64_sve_fdiv, DestructiveBinaryCommWithRev, "FDIVR_ZPmZ">;

  defm FADD_ZPZZ   : sve_fp_bin_pred_hfd<AArch64fadd_p>;
  defm FSUB_ZPZZ   : sve_fp_bin_pred_hfd<AArch64fsub_p>;
  defm FMUL_ZPZZ   : sve_fp_bin_pred_hfd<AArch64fmul_p>;
  defm FMAXNM_ZPZZ : sve_fp_bin_pred_hfd<AArch64fmaxnm_p>;
  defm FMINNM_ZPZZ : sve_fp_bin_pred_hfd<AArch64fminnm_p>;
  defm FMAX_ZPZZ   : sve_fp_bin_pred_hfd<AArch64fmax_p>;
  defm FMIN_ZPZZ   : sve_fp_bin_pred_hfd<AArch64fmin_p>;
  defm FABD_ZPZZ   : sve_fp_bin_pred_hfd<AArch64fabd_p>;
  defm FMULX_ZPZZ  : sve_fp_bin_pred_hfd<int_aarch64_sve_fmulx_u>;
  defm FDIV_ZPZZ   : sve_fp_bin_pred_hfd<AArch64fdiv_p>;
} // End HasSVEorSME

let Predicates = [HasSVEorSME, UseExperimentalZeroingPseudos] in {
  defm FADD_ZPZZ   : sve_fp_2op_p_zds_zeroing_hsd<int_aarch64_sve_fadd>;
  defm FSUB_ZPZZ   : sve_fp_2op_p_zds_zeroing_hsd<int_aarch64_sve_fsub>;
  defm FMUL_ZPZZ   : sve_fp_2op_p_zds_zeroing_hsd<int_aarch64_sve_fmul>;
  defm FSUBR_ZPZZ  : sve_fp_2op_p_zds_zeroing_hsd<int_aarch64_sve_fsubr>;
  defm FMAXNM_ZPZZ : sve_fp_2op_p_zds_zeroing_hsd<int_aarch64_sve_fmaxnm>;
  defm FMINNM_ZPZZ : sve_fp_2op_p_zds_zeroing_hsd<int_aarch64_sve_fminnm>;
  defm FMAX_ZPZZ   : sve_fp_2op_p_zds_zeroing_hsd<int_aarch64_sve_fmax>;
  defm FMIN_ZPZZ   : sve_fp_2op_p_zds_zeroing_hsd<int_aarch64_sve_fmin>;
  defm FABD_ZPZZ   : sve_fp_2op_p_zds_zeroing_hsd<int_aarch64_sve_fabd>;
  defm FMULX_ZPZZ  : sve_fp_2op_p_zds_zeroing_hsd<int_aarch64_sve_fmulx>;
  defm FDIVR_ZPZZ  : sve_fp_2op_p_zds_zeroing_hsd<int_aarch64_sve_fdivr>;
  defm FDIV_ZPZZ   : sve_fp_2op_p_zds_zeroing_hsd<int_aarch64_sve_fdiv>;
} // End HasSVEorSME, UseExperimentalZeroingPseudos

let Predicates = [HasSVEorSME] in {
  defm FADD_ZZZ    : sve_fp_3op_u_zd<0b000, "fadd", AArch64fadd>;
  defm FSUB_ZZZ    : sve_fp_3op_u_zd<0b001, "fsub", AArch64fsub>;
  defm FMUL_ZZZ    : sve_fp_3op_u_zd<0b010, "fmul", AArch64fmul>;
} // End HasSVEorSME

let Predicates = [HasSVE] in {
  defm FTSMUL_ZZZ  : sve_fp_3op_u_zd_ftsmul<0b011, "ftsmul", int_aarch64_sve_ftsmul_x>;
} // End HasSVE

let Predicates = [HasSVEorSME] in {
  defm FRECPS_ZZZ  : sve_fp_3op_u_zd<0b110, "frecps",  AArch64frecps>;
  defm FRSQRTS_ZZZ : sve_fp_3op_u_zd<0b111, "frsqrts", AArch64frsqrts>;
} // End HasSVEorSME

let Predicates = [HasSVE] in {
  defm FTSSEL_ZZZ : sve_int_bin_cons_misc_0_b<"ftssel", int_aarch64_sve_ftssel_x>;
} // End HasSVE

let Predicates = [HasSVEorSME] in {
  defm FCADD_ZPmZ  : sve_fp_fcadd<"fcadd", int_aarch64_sve_fcadd>;
  defm FCMLA_ZPmZZ : sve_fp_fcmla<"fcmla", int_aarch64_sve_fcmla>;

  defm FMLA_ZPmZZ  : sve_fp_3op_p_zds_a<0b00, "fmla",  "FMLA_ZPZZZ", AArch64fmla_m1, "FMAD_ZPmZZ">;
  defm FMLS_ZPmZZ  : sve_fp_3op_p_zds_a<0b01, "fmls",  "FMLS_ZPZZZ", AArch64fmls_m1, "FMSB_ZPmZZ">;
  defm FNMLA_ZPmZZ : sve_fp_3op_p_zds_a<0b10, "fnmla", "FNMLA_ZPZZZ", int_aarch64_sve_fnmla, "FNMAD_ZPmZZ">;
  defm FNMLS_ZPmZZ : sve_fp_3op_p_zds_a<0b11, "fnmls", "FNMLS_ZPZZZ", int_aarch64_sve_fnmls, "FNMSB_ZPmZZ">;

  defm FMAD_ZPmZZ  : sve_fp_3op_p_zds_b<0b00, "fmad",  int_aarch64_sve_fmad, "FMLA_ZPmZZ", /*isReverseInstr*/ 1>;
  defm FMSB_ZPmZZ  : sve_fp_3op_p_zds_b<0b01, "fmsb",  int_aarch64_sve_fmsb, "FMLS_ZPmZZ", /*isReverseInstr*/ 1>;
  defm FNMAD_ZPmZZ : sve_fp_3op_p_zds_b<0b10, "fnmad", int_aarch64_sve_fnmad, "FNMLA_ZPmZZ", /*isReverseInstr*/ 1>;
  defm FNMSB_ZPmZZ : sve_fp_3op_p_zds_b<0b11, "fnmsb", int_aarch64_sve_fnmsb, "FNMLS_ZPmZZ", /*isReverseInstr*/ 1>;

  defm FMLA_ZPZZZ  : sve_fp_3op_pred_hfd<AArch64fmla_p>;
  defm FMLS_ZPZZZ  : sve_fp_3op_pred_hfd<AArch64fmls_p>;
  defm FNMLA_ZPZZZ : sve_fp_3op_pred_hfd<AArch64fnmla_p>;
  defm FNMLS_ZPZZZ : sve_fp_3op_pred_hfd<AArch64fnmls_p>;
} // End HasSVEorSME

let Predicates = [HasSVE] in {
  defm FTMAD_ZZI : sve_fp_ftmad<"ftmad", int_aarch64_sve_ftmad_x>;
} // End HasSVE

let Predicates = [HasSVEorSME] in {
  defm FMLA_ZZZI : sve_fp_fma_by_indexed_elem<0b00, "fmla", int_aarch64_sve_fmla_lane>;
  defm FMLS_ZZZI : sve_fp_fma_by_indexed_elem<0b01, "fmls", int_aarch64_sve_fmls_lane>;

  defm FCMLA_ZZZI : sve_fp_fcmla_by_indexed_elem<"fcmla", int_aarch64_sve_fcmla_lane>;
  defm FMUL_ZZZI   : sve_fp_fmul_by_indexed_elem<"fmul", int_aarch64_sve_fmul_lane>;
} // End HasSVEorSME

let Predicates = [HasSVE] in {
  // SVE floating point reductions.
  defm FADDA_VPZ   : sve_fp_2op_p_vd<0b000, "fadda",   AArch64fadda_p>;
} // End HasSVE

let Predicates = [HasSVEorSME] in {
  defm FADDV_VPZ   : sve_fp_fast_red<0b000, "faddv",   AArch64faddv_p>;
  defm FMAXNMV_VPZ : sve_fp_fast_red<0b100, "fmaxnmv", AArch64fmaxnmv_p>;
  defm FMINNMV_VPZ : sve_fp_fast_red<0b101, "fminnmv", AArch64fminnmv_p>;
  defm FMAXV_VPZ   : sve_fp_fast_red<0b110, "fmaxv",   AArch64fmaxv_p>;
  defm FMINV_VPZ   : sve_fp_fast_red<0b111, "fminv",   AArch64fminv_p>;

  // Splat immediate (unpredicated)
  defm DUP_ZI  : sve_int_dup_imm<"dup">;
  defm FDUP_ZI : sve_int_dup_fpimm<"fdup">;
  defm DUPM_ZI : sve_int_dup_mask_imm<"dupm">;

  // Splat immediate (predicated)
  defm CPY_ZPmI  : sve_int_dup_imm_pred_merge<"cpy">;
  defm CPY_ZPzI  : sve_int_dup_imm_pred_zero<"cpy">;
  defm FCPY_ZPmI : sve_int_dup_fpimm_pred<"fcpy">;

  // Splat scalar register (unpredicated, GPR or vector + element index)
  defm DUP_ZR  : sve_int_perm_dup_r<"dup", splat_vector>;
  defm DUP_ZZI : sve_int_perm_dup_i<"dup">;

  // Splat scalar register (predicated)
  defm CPY_ZPmR : sve_int_perm_cpy_r<"cpy", AArch64dup_mt>;
  defm CPY_ZPmV : sve_int_perm_cpy_v<"cpy", AArch64dup_mt>;

  // Duplicate FP scalar into all vector elements
  def : Pat<(nxv8f16 (splat_vector (f16 FPR16:$src))),
            (DUP_ZZI_H (INSERT_SUBREG (IMPLICIT_DEF), FPR16:$src, hsub), 0)>;
  def : Pat<(nxv4f16 (splat_vector (f16 FPR16:$src))),
            (DUP_ZZI_H (INSERT_SUBREG (IMPLICIT_DEF), FPR16:$src, hsub), 0)>;
  def : Pat<(nxv2f16 (splat_vector (f16 FPR16:$src))),
            (DUP_ZZI_H (INSERT_SUBREG (IMPLICIT_DEF), FPR16:$src, hsub), 0)>;
  def : Pat<(nxv4f32 (splat_vector (f32 FPR32:$src))),
            (DUP_ZZI_S (INSERT_SUBREG (IMPLICIT_DEF), FPR32:$src, ssub), 0)>;
  def : Pat<(nxv2f32 (splat_vector (f32 FPR32:$src))),
            (DUP_ZZI_S (INSERT_SUBREG (IMPLICIT_DEF), FPR32:$src, ssub), 0)>;
  def : Pat<(nxv2f64 (splat_vector (f64 FPR64:$src))),
            (DUP_ZZI_D (INSERT_SUBREG (IMPLICIT_DEF), FPR64:$src, dsub), 0)>;
  def : Pat<(nxv8bf16 (splat_vector (bf16 FPR16:$src))),
            (DUP_ZZI_H (INSERT_SUBREG (IMPLICIT_DEF), FPR16:$src, hsub), 0)>;
  def : Pat<(nxv4bf16 (splat_vector (bf16 FPR16:$src))),
            (DUP_ZZI_H (INSERT_SUBREG (IMPLICIT_DEF), FPR16:$src, hsub), 0)>;
  def : Pat<(nxv2bf16 (splat_vector (bf16 FPR16:$src))),
            (DUP_ZZI_H (INSERT_SUBREG (IMPLICIT_DEF), FPR16:$src, hsub), 0)>;

  // Duplicate +0.0 into all vector elements
  def : Pat<(nxv8f16 (splat_vector (f16 fpimm0))), (DUP_ZI_H 0, 0)>;
  def : Pat<(nxv4f16 (splat_vector (f16 fpimm0))), (DUP_ZI_H 0, 0)>;
  def : Pat<(nxv2f16 (splat_vector (f16 fpimm0))), (DUP_ZI_H 0, 0)>;
  def : Pat<(nxv4f32 (splat_vector (f32 fpimm0))), (DUP_ZI_S 0, 0)>;
  def : Pat<(nxv2f32 (splat_vector (f32 fpimm0))), (DUP_ZI_S 0, 0)>;
  def : Pat<(nxv2f64 (splat_vector (f64 fpimm0))), (DUP_ZI_D 0, 0)>;
  def : Pat<(nxv8bf16 (splat_vector (bf16 fpimm0))), (DUP_ZI_H 0, 0)>;
  def : Pat<(nxv4bf16 (splat_vector (bf16 fpimm0))), (DUP_ZI_H 0, 0)>;
  def : Pat<(nxv2bf16 (splat_vector (bf16 fpimm0))), (DUP_ZI_H 0, 0)>;

  // Duplicate Int immediate into all vector elements
  def : Pat<(nxv16i8 (splat_vector (i32 (SVECpyDupImm8Pat i32:$a, i32:$b)))),
            (DUP_ZI_B $a, $b)>;
  def : Pat<(nxv8i16 (splat_vector (i32 (SVECpyDupImm16Pat i32:$a, i32:$b)))),
            (DUP_ZI_H $a, $b)>;
  def : Pat<(nxv4i32 (splat_vector (i32 (SVECpyDupImm32Pat i32:$a, i32:$b)))),
            (DUP_ZI_S $a, $b)>;
  def : Pat<(nxv2i64 (splat_vector (i64 (SVECpyDupImm64Pat i32:$a, i32:$b)))),
            (DUP_ZI_D $a, $b)>;

  // Duplicate immediate FP into all vector elements.
  def : Pat<(nxv2f16 (splat_vector (f16 fpimm:$val))),
            (DUP_ZR_H (MOVi32imm (bitcast_fpimm_to_i32 f16:$val)))>;
  def : Pat<(nxv4f16 (splat_vector (f16 fpimm:$val))),
            (DUP_ZR_H (MOVi32imm (bitcast_fpimm_to_i32 f16:$val)))>;
  def : Pat<(nxv8f16 (splat_vector (f16 fpimm:$val))),
            (DUP_ZR_H (MOVi32imm (bitcast_fpimm_to_i32 f16:$val)))>;
  def : Pat<(nxv2f32 (splat_vector (f32 fpimm:$val))),
            (DUP_ZR_S (MOVi32imm (bitcast_fpimm_to_i32 f32:$val)))>;
  def : Pat<(nxv4f32 (splat_vector (f32 fpimm:$val))),
            (DUP_ZR_S (MOVi32imm (bitcast_fpimm_to_i32 f32:$val)))>;
  def : Pat<(nxv2f64 (splat_vector (f64 fpimm:$val))),
            (DUP_ZR_D (MOVi64imm (bitcast_fpimm_to_i64 f64:$val)))>;

  // Duplicate FP immediate into all vector elements
  let AddedComplexity = 2 in {
    def : Pat<(nxv8f16 (splat_vector fpimm16:$imm8)),
              (FDUP_ZI_H fpimm16:$imm8)>;
    def : Pat<(nxv4f16 (splat_vector fpimm16:$imm8)),
              (FDUP_ZI_H fpimm16:$imm8)>;
    def : Pat<(nxv2f16 (splat_vector fpimm16:$imm8)),
              (FDUP_ZI_H fpimm16:$imm8)>;
    def : Pat<(nxv4f32 (splat_vector fpimm32:$imm8)),
              (FDUP_ZI_S fpimm32:$imm8)>;
    def : Pat<(nxv2f32 (splat_vector fpimm32:$imm8)),
              (FDUP_ZI_S fpimm32:$imm8)>;
    def : Pat<(nxv2f64 (splat_vector fpimm64:$imm8)),
              (FDUP_ZI_D fpimm64:$imm8)>;
  }

  // Select elements from either vector (predicated)
  defm SEL_ZPZZ   : sve_int_sel_vvv<"sel", vselect>;

  defm SPLICE_ZPZ : sve_int_perm_splice<"splice", AArch64splice>;
} // End HasSVEorSME

let Predicates = [HasSVE] in {
  defm COMPACT_ZPZ : sve_int_perm_compact<"compact", int_aarch64_sve_compact>;
} // End HasSVE

let Predicates = [HasSVEorSME] in {
  defm INSR_ZR : sve_int_perm_insrs<"insr", AArch64insr>;
  defm INSR_ZV : sve_int_perm_insrv<"insr", AArch64insr>;
  defm EXT_ZZI : sve_int_perm_extract_i<"ext", AArch64ext>;

  defm RBIT_ZPmZ : sve_int_perm_rev_rbit<"rbit", AArch64rbit_mt>;
  defm REVB_ZPmZ : sve_int_perm_rev_revb<"revb", AArch64revb_mt>;
  defm REVH_ZPmZ : sve_int_perm_rev_revh<"revh", AArch64revh_mt>;
  defm REVW_ZPmZ : sve_int_perm_rev_revw<"revw", AArch64revw_mt>;

  defm REV_PP : sve_int_perm_reverse_p<"rev", vector_reverse, int_aarch64_sve_rev_b16, int_aarch64_sve_rev_b32, int_aarch64_sve_rev_b64>;
  defm REV_ZZ : sve_int_perm_reverse_z<"rev", vector_reverse>;

  defm SUNPKLO_ZZ : sve_int_perm_unpk<0b00, "sunpklo", AArch64sunpklo>;
  defm SUNPKHI_ZZ : sve_int_perm_unpk<0b01, "sunpkhi", AArch64sunpkhi>;
  defm UUNPKLO_ZZ : sve_int_perm_unpk<0b10, "uunpklo", AArch64uunpklo>;
  defm UUNPKHI_ZZ : sve_int_perm_unpk<0b11, "uunpkhi", AArch64uunpkhi>;

  defm PUNPKLO_PP : sve_int_perm_punpk<0b0, "punpklo", int_aarch64_sve_punpklo>;
  defm PUNPKHI_PP : sve_int_perm_punpk<0b1, "punpkhi", int_aarch64_sve_punpkhi>;

  // Define pattern for `nxv1i1 splat_vector(1)`.
  // We do this here instead of in ISelLowering such that PatFrag's can still
  // recognize a splat.
  def : Pat<(nxv1i1 immAllOnesV), (PUNPKLO_PP (PTRUE_D 31))>;

  defm MOVPRFX_ZPzZ : sve_int_movprfx_pred_zero<0b000, "movprfx">;
  defm MOVPRFX_ZPmZ : sve_int_movprfx_pred_merge<0b001, "movprfx">;
  def MOVPRFX_ZZ : sve_int_bin_cons_misc_0_c<0b00000001, "movprfx", ZPRAny>;
} // End HasSVEorSME

let Predicates = [HasSVE] in {
  defm FEXPA_ZZ : sve_int_bin_cons_misc_0_c_fexpa<"fexpa", int_aarch64_sve_fexpa_x>;
} // End HasSVE

let Predicates = [HasSVEorSME] in {
  defm BRKPA_PPzPP  : sve_int_brkp<0b00, "brkpa",  int_aarch64_sve_brkpa_z>;
  defm BRKPAS_PPzPP : sve_int_brkp<0b10, "brkpas", null_frag>;
  defm BRKPB_PPzPP  : sve_int_brkp<0b01, "brkpb",  int_aarch64_sve_brkpb_z>;
  defm BRKPBS_PPzPP : sve_int_brkp<0b11, "brkpbs", null_frag>;

  defm BRKN_PPzP  : sve_int_brkn<0b0, "brkn",  int_aarch64_sve_brkn_z>;
  defm BRKNS_PPzP : sve_int_brkn<0b1, "brkns", null_frag>;

  defm BRKA_PPzP  : sve_int_break_z<0b000, "brka",  int_aarch64_sve_brka_z>;
  defm BRKA_PPmP  : sve_int_break_m<0b001, "brka",  int_aarch64_sve_brka>;
  defm BRKAS_PPzP : sve_int_break_z<0b010, "brkas", null_frag>;
  defm BRKB_PPzP  : sve_int_break_z<0b100, "brkb",  int_aarch64_sve_brkb_z>;
  defm BRKB_PPmP  : sve_int_break_m<0b101, "brkb",  int_aarch64_sve_brkb>;
  defm BRKBS_PPzP : sve_int_break_z<0b110, "brkbs", null_frag>;

  defm PTEST_PP : sve_int_ptest<0b010000, "ptest", AArch64ptest, AArch64ptest_any>;
  defm PFALSE   : sve_int_pfalse<0b000000, "pfalse">;
  defm PFIRST   : sve_int_pfirst<0b00000, "pfirst", int_aarch64_sve_pfirst>;
  defm PNEXT    : sve_int_pnext<0b00110, "pnext", int_aarch64_sve_pnext>;

  defm AND_PPzPP   : sve_int_pred_log_v2<0b0000, "and", int_aarch64_sve_and_z, and>;
  defm BIC_PPzPP   : sve_int_pred_log_v2<0b0001, "bic", int_aarch64_sve_bic_z, AArch64bic>;
  defm EOR_PPzPP   : sve_int_pred_log<0b0010, "eor", int_aarch64_sve_eor_z, xor>;
  defm SEL_PPPP    : sve_int_pred_log_v2<0b0011, "sel", vselect, or>;
  defm ANDS_PPzPP  : sve_int_pred_log<0b0100, "ands", null_frag>;
  defm BICS_PPzPP  : sve_int_pred_log<0b0101, "bics", null_frag>;
  defm EORS_PPzPP  : sve_int_pred_log<0b0110, "eors", null_frag>;
  defm ORR_PPzPP   : sve_int_pred_log<0b1000, "orr", int_aarch64_sve_orr_z>;
  defm ORN_PPzPP   : sve_int_pred_log<0b1001, "orn", int_aarch64_sve_orn_z>;
  defm NOR_PPzPP   : sve_int_pred_log<0b1010, "nor", int_aarch64_sve_nor_z>;
  defm NAND_PPzPP  : sve_int_pred_log<0b1011, "nand", int_aarch64_sve_nand_z>;
  defm ORRS_PPzPP  : sve_int_pred_log<0b1100, "orrs", null_frag>;
  defm ORNS_PPzPP  : sve_int_pred_log<0b1101, "orns", null_frag>;
  defm NORS_PPzPP  : sve_int_pred_log<0b1110, "nors", null_frag>;
  defm NANDS_PPzPP : sve_int_pred_log<0b1111, "nands", null_frag>;

  defm CLASTA_RPZ : sve_int_perm_clast_rz<0, "clasta", AArch64clasta_n>;
  defm CLASTB_RPZ : sve_int_perm_clast_rz<1, "clastb", AArch64clastb_n>;
  defm CLASTA_VPZ : sve_int_perm_clast_vz<0, "clasta", AArch64clasta_n>;
  defm CLASTB_VPZ : sve_int_perm_clast_vz<1, "clastb", AArch64clastb_n>;
  defm CLASTA_ZPZ : sve_int_perm_clast_zz<0, "clasta", int_aarch64_sve_clasta>;
  defm CLASTB_ZPZ : sve_int_perm_clast_zz<1, "clastb", int_aarch64_sve_clastb>;

  defm LASTA_RPZ : sve_int_perm_last_r<0, "lasta", AArch64lasta>;
  defm LASTB_RPZ : sve_int_perm_last_r<1, "lastb", AArch64lastb>;
  defm LASTA_VPZ : sve_int_perm_last_v<0, "lasta", AArch64lasta>;
  defm LASTB_VPZ : sve_int_perm_last_v<1, "lastb", AArch64lastb>;

  // continuous load with reg+immediate
  defm LD1B_IMM    : sve_mem_cld_si<0b0000, "ld1b",  Z_b, ZPR8>;
  defm LD1B_H_IMM  : sve_mem_cld_si<0b0001, "ld1b",  Z_h, ZPR16>;
  defm LD1B_S_IMM  : sve_mem_cld_si<0b0010, "ld1b",  Z_s, ZPR32>;
  defm LD1B_D_IMM  : sve_mem_cld_si<0b0011, "ld1b",  Z_d, ZPR64>;
  defm LD1SW_D_IMM : sve_mem_cld_si<0b0100, "ld1sw", Z_d, ZPR64>;
  defm LD1H_IMM    : sve_mem_cld_si<0b0101, "ld1h",  Z_h, ZPR16>;
  defm LD1H_S_IMM  : sve_mem_cld_si<0b0110, "ld1h",  Z_s, ZPR32>;
  defm LD1H_D_IMM  : sve_mem_cld_si<0b0111, "ld1h",  Z_d, ZPR64>;
  defm LD1SH_D_IMM : sve_mem_cld_si<0b1000, "ld1sh", Z_d, ZPR64>;
  defm LD1SH_S_IMM : sve_mem_cld_si<0b1001, "ld1sh", Z_s, ZPR32>;
  defm LD1W_IMM    : sve_mem_cld_si<0b1010, "ld1w",  Z_s, ZPR32>;
  defm LD1W_D_IMM  : sve_mem_cld_si<0b1011, "ld1w",  Z_d, ZPR64>;
  let Predicates = [HasSVE2p1] in {
  defm LD1W_Q_IMM  : sve_mem_128b_cld_si<0b10, "ld1w">;
  }
  defm LD1SB_D_IMM : sve_mem_cld_si<0b1100, "ld1sb", Z_d, ZPR64>;
  defm LD1SB_S_IMM : sve_mem_cld_si<0b1101, "ld1sb", Z_s, ZPR32>;
  defm LD1SB_H_IMM : sve_mem_cld_si<0b1110, "ld1sb", Z_h, ZPR16>;
  defm LD1D_IMM    : sve_mem_cld_si<0b1111, "ld1d",  Z_d, ZPR64>;
  let Predicates = [HasSVE2p1] in {
  defm LD1D_Q_IMM  : sve_mem_128b_cld_si<0b11, "ld1d">;
  }

  // LD1R loads (splat scalar to vector)
  defm LD1RB_IMM    : sve_mem_ld_dup<0b00, 0b00, "ld1rb",  Z_b, ZPR8,  uimm6s1>;
  defm LD1RB_H_IMM  : sve_mem_ld_dup<0b00, 0b01, "ld1rb",  Z_h, ZPR16, uimm6s1>;
  defm LD1RB_S_IMM  : sve_mem_ld_dup<0b00, 0b10, "ld1rb",  Z_s, ZPR32, uimm6s1>;
  defm LD1RB_D_IMM  : sve_mem_ld_dup<0b00, 0b11, "ld1rb",  Z_d, ZPR64, uimm6s1>;
  defm LD1RSW_IMM   : sve_mem_ld_dup<0b01, 0b00, "ld1rsw", Z_d, ZPR64, uimm6s4>;
  defm LD1RH_IMM    : sve_mem_ld_dup<0b01, 0b01, "ld1rh",  Z_h, ZPR16, uimm6s2>;
  defm LD1RH_S_IMM  : sve_mem_ld_dup<0b01, 0b10, "ld1rh",  Z_s, ZPR32, uimm6s2>;
  defm LD1RH_D_IMM  : sve_mem_ld_dup<0b01, 0b11, "ld1rh",  Z_d, ZPR64, uimm6s2>;
  defm LD1RSH_D_IMM : sve_mem_ld_dup<0b10, 0b00, "ld1rsh", Z_d, ZPR64, uimm6s2>;
  defm LD1RSH_S_IMM : sve_mem_ld_dup<0b10, 0b01, "ld1rsh", Z_s, ZPR32, uimm6s2>;
  defm LD1RW_IMM    : sve_mem_ld_dup<0b10, 0b10, "ld1rw",  Z_s, ZPR32, uimm6s4>;
  defm LD1RW_D_IMM  : sve_mem_ld_dup<0b10, 0b11, "ld1rw",  Z_d, ZPR64, uimm6s4>;
  defm LD1RSB_D_IMM : sve_mem_ld_dup<0b11, 0b00, "ld1rsb", Z_d, ZPR64, uimm6s1>;
  defm LD1RSB_S_IMM : sve_mem_ld_dup<0b11, 0b01, "ld1rsb", Z_s, ZPR32, uimm6s1>;
  defm LD1RSB_H_IMM : sve_mem_ld_dup<0b11, 0b10, "ld1rsb", Z_h, ZPR16, uimm6s1>;
  defm LD1RD_IMM    : sve_mem_ld_dup<0b11, 0b11, "ld1rd",  Z_d, ZPR64, uimm6s8>;

  // LD1RQ loads (load quadword-vector and splat to scalable vector)
  defm LD1RQ_B_IMM  : sve_mem_ldqr_si<0b00, "ld1rqb", Z_b, ZPR8>;
  defm LD1RQ_H_IMM  : sve_mem_ldqr_si<0b01, "ld1rqh", Z_h, ZPR16>;
  defm LD1RQ_W_IMM  : sve_mem_ldqr_si<0b10, "ld1rqw", Z_s, ZPR32>;
  defm LD1RQ_D_IMM  : sve_mem_ldqr_si<0b11, "ld1rqd", Z_d, ZPR64>;
  defm LD1RQ_B      : sve_mem_ldqr_ss<0b00, "ld1rqb", Z_b, ZPR8,  GPR64NoXZRshifted8>;
  defm LD1RQ_H      : sve_mem_ldqr_ss<0b01, "ld1rqh", Z_h, ZPR16, GPR64NoXZRshifted16>;
  defm LD1RQ_W      : sve_mem_ldqr_ss<0b10, "ld1rqw", Z_s, ZPR32, GPR64NoXZRshifted32>;
  defm LD1RQ_D      : sve_mem_ldqr_ss<0b11, "ld1rqd", Z_d, ZPR64, GPR64NoXZRshifted64>;

  multiclass sve_ld1rq_duplane_pat<ValueType vt1, ValueType vt2, SDPatternOperator op, Instruction load_instr_imm, Instruction ptrue, Instruction load_instr_scalar, ComplexPattern AddrCP> {
    def : Pat<(vt1 (op (vt1 (vector_insert_subvec (vt1 undef), (vt2 (load GPR64sp:$Xn)), (i64 0))), (i64 0))),
          (load_instr_imm (ptrue 31), GPR64sp:$Xn, 0)>;
    let AddedComplexity = 2 in {
      def : Pat<(vt1 (op (vt1 (vector_insert_subvec (vt1 undef), (vt2 (load (add GPR64sp:$Xn, simm4s16:$imm))), (i64 0))), (i64 0))),
            (load_instr_imm (ptrue 31), GPR64sp:$Xn, simm4s16:$imm)>;
    }
    def : Pat<(vt1 (op (vt1 (vector_insert_subvec (vt1 undef), (vt2 (load (AddrCP GPR64sp:$Xn, GPR64sp:$idx))), (i64 0))), (i64 0))),
          (load_instr_scalar (ptrue 31), GPR64sp:$Xn, $idx)>;
  }
  defm : sve_ld1rq_duplane_pat<nxv16i8, v16i8, AArch64duplane128, LD1RQ_B_IMM, PTRUE_B, LD1RQ_B, am_sve_regreg_lsl0>;
  defm : sve_ld1rq_duplane_pat<nxv8i16, v8i16, AArch64duplane128, LD1RQ_H_IMM, PTRUE_H, LD1RQ_H, am_sve_regreg_lsl1>;
  defm : sve_ld1rq_duplane_pat<nxv4i32, v4i32, AArch64duplane128, LD1RQ_W_IMM, PTRUE_S, LD1RQ_W, am_sve_regreg_lsl2>;
  defm : sve_ld1rq_duplane_pat<nxv2i64, v2i64, AArch64duplane128, LD1RQ_D_IMM, PTRUE_D, LD1RQ_D, am_sve_regreg_lsl3>;

  // continuous load with reg+reg addressing.
  defm LD1B    : sve_mem_cld_ss<0b0000, "ld1b",  Z_b, ZPR8,  GPR64NoXZRshifted8>;
  defm LD1B_H  : sve_mem_cld_ss<0b0001, "ld1b",  Z_h, ZPR16, GPR64NoXZRshifted8>;
  defm LD1B_S  : sve_mem_cld_ss<0b0010, "ld1b",  Z_s, ZPR32, GPR64NoXZRshifted8>;
  defm LD1B_D  : sve_mem_cld_ss<0b0011, "ld1b",  Z_d, ZPR64, GPR64NoXZRshifted8>;
  defm LD1SW_D : sve_mem_cld_ss<0b0100, "ld1sw", Z_d, ZPR64, GPR64NoXZRshifted32>;
  defm LD1H    : sve_mem_cld_ss<0b0101, "ld1h",  Z_h, ZPR16, GPR64NoXZRshifted16>;
  defm LD1H_S  : sve_mem_cld_ss<0b0110, "ld1h",  Z_s, ZPR32, GPR64NoXZRshifted16>;
  defm LD1H_D  : sve_mem_cld_ss<0b0111, "ld1h",  Z_d, ZPR64, GPR64NoXZRshifted16>;
  defm LD1SH_D : sve_mem_cld_ss<0b1000, "ld1sh", Z_d, ZPR64, GPR64NoXZRshifted16>;
  defm LD1SH_S : sve_mem_cld_ss<0b1001, "ld1sh", Z_s, ZPR32, GPR64NoXZRshifted16>;
  defm LD1W    : sve_mem_cld_ss<0b1010, "ld1w",  Z_s, ZPR32, GPR64NoXZRshifted32>;
  defm LD1W_D  : sve_mem_cld_ss<0b1011, "ld1w",  Z_d, ZPR64, GPR64NoXZRshifted32>;
  let Predicates = [HasSVE2p1] in {
  defm LD1W_Q  : sve_mem_128b_cld_ss<0b10, "ld1w", GPR64NoXZRshifted32>;
  }
  defm LD1SB_D : sve_mem_cld_ss<0b1100, "ld1sb", Z_d, ZPR64, GPR64NoXZRshifted8>;
  defm LD1SB_S : sve_mem_cld_ss<0b1101, "ld1sb", Z_s, ZPR32, GPR64NoXZRshifted8>;
  defm LD1SB_H : sve_mem_cld_ss<0b1110, "ld1sb", Z_h, ZPR16, GPR64NoXZRshifted8>;
  defm LD1D    : sve_mem_cld_ss<0b1111, "ld1d",  Z_d, ZPR64, GPR64NoXZRshifted64>;
  let Predicates = [HasSVE2p1] in {
  defm LD1D_Q  : sve_mem_128b_cld_ss<0b11, "ld1d", GPR64NoXZRshifted64>;
  }
} // End HasSVEorSME

let Predicates = [HasSVE] in {
  // non-faulting continuous load with reg+immediate
  defm LDNF1B_IMM    : sve_mem_cldnf_si<0b0000, "ldnf1b",  Z_b, ZPR8>;
  defm LDNF1B_H_IMM  : sve_mem_cldnf_si<0b0001, "ldnf1b",  Z_h, ZPR16>;
  defm LDNF1B_S_IMM  : sve_mem_cldnf_si<0b0010, "ldnf1b",  Z_s, ZPR32>;
  defm LDNF1B_D_IMM  : sve_mem_cldnf_si<0b0011, "ldnf1b",  Z_d, ZPR64>;
  defm LDNF1SW_D_IMM : sve_mem_cldnf_si<0b0100, "ldnf1sw", Z_d, ZPR64>;
  defm LDNF1H_IMM    : sve_mem_cldnf_si<0b0101, "ldnf1h",  Z_h, ZPR16>;
  defm LDNF1H_S_IMM  : sve_mem_cldnf_si<0b0110, "ldnf1h",  Z_s, ZPR32>;
  defm LDNF1H_D_IMM  : sve_mem_cldnf_si<0b0111, "ldnf1h",  Z_d, ZPR64>;
  defm LDNF1SH_D_IMM : sve_mem_cldnf_si<0b1000, "ldnf1sh", Z_d, ZPR64>;
  defm LDNF1SH_S_IMM : sve_mem_cldnf_si<0b1001, "ldnf1sh", Z_s, ZPR32>;
  defm LDNF1W_IMM    : sve_mem_cldnf_si<0b1010, "ldnf1w",  Z_s, ZPR32>;
  defm LDNF1W_D_IMM  : sve_mem_cldnf_si<0b1011, "ldnf1w",  Z_d, ZPR64>;
  defm LDNF1SB_D_IMM : sve_mem_cldnf_si<0b1100, "ldnf1sb", Z_d, ZPR64>;
  defm LDNF1SB_S_IMM : sve_mem_cldnf_si<0b1101, "ldnf1sb", Z_s, ZPR32>;
  defm LDNF1SB_H_IMM : sve_mem_cldnf_si<0b1110, "ldnf1sb", Z_h, ZPR16>;
  defm LDNF1D_IMM    : sve_mem_cldnf_si<0b1111, "ldnf1d",  Z_d, ZPR64>;

  // First-faulting loads with reg+reg addressing.
  defm LDFF1B    : sve_mem_cldff_ss<0b0000, "ldff1b",  Z_b, ZPR8,  GPR64shifted8>;
  defm LDFF1B_H  : sve_mem_cldff_ss<0b0001, "ldff1b",  Z_h, ZPR16, GPR64shifted8>;
  defm LDFF1B_S  : sve_mem_cldff_ss<0b0010, "ldff1b",  Z_s, ZPR32, GPR64shifted8>;
  defm LDFF1B_D  : sve_mem_cldff_ss<0b0011, "ldff1b",  Z_d, ZPR64, GPR64shifted8>;
  defm LDFF1SW_D : sve_mem_cldff_ss<0b0100, "ldff1sw", Z_d, ZPR64, GPR64shifted32>;
  defm LDFF1H    : sve_mem_cldff_ss<0b0101, "ldff1h",  Z_h, ZPR16, GPR64shifted16>;
  defm LDFF1H_S  : sve_mem_cldff_ss<0b0110, "ldff1h",  Z_s, ZPR32, GPR64shifted16>;
  defm LDFF1H_D  : sve_mem_cldff_ss<0b0111, "ldff1h",  Z_d, ZPR64, GPR64shifted16>;
  defm LDFF1SH_D : sve_mem_cldff_ss<0b1000, "ldff1sh", Z_d, ZPR64, GPR64shifted16>;
  defm LDFF1SH_S : sve_mem_cldff_ss<0b1001, "ldff1sh", Z_s, ZPR32, GPR64shifted16>;
  defm LDFF1W    : sve_mem_cldff_ss<0b1010, "ldff1w",  Z_s, ZPR32, GPR64shifted32>;
  defm LDFF1W_D  : sve_mem_cldff_ss<0b1011, "ldff1w",  Z_d, ZPR64, GPR64shifted32>;
  defm LDFF1SB_D : sve_mem_cldff_ss<0b1100, "ldff1sb", Z_d, ZPR64, GPR64shifted8>;
  defm LDFF1SB_S : sve_mem_cldff_ss<0b1101, "ldff1sb", Z_s, ZPR32, GPR64shifted8>;
  defm LDFF1SB_H : sve_mem_cldff_ss<0b1110, "ldff1sb", Z_h, ZPR16, GPR64shifted8>;
  defm LDFF1D    : sve_mem_cldff_ss<0b1111, "ldff1d",  Z_d, ZPR64, GPR64shifted64>;
} // End HasSVE

let Predicates = [HasSVEorSME] in {
  // LD(2|3|4) structured loads with reg+immediate
  defm LD2B_IMM : sve_mem_eld_si<0b00, 0b001, ZZ_b,   "ld2b", simm4s2>;
  defm LD3B_IMM : sve_mem_eld_si<0b00, 0b010, ZZZ_b,  "ld3b", simm4s3>;
  defm LD4B_IMM : sve_mem_eld_si<0b00, 0b011, ZZZZ_b, "ld4b", simm4s4>;
  defm LD2H_IMM : sve_mem_eld_si<0b01, 0b001, ZZ_h,   "ld2h", simm4s2>;
  defm LD3H_IMM : sve_mem_eld_si<0b01, 0b010, ZZZ_h,  "ld3h", simm4s3>;
  defm LD4H_IMM : sve_mem_eld_si<0b01, 0b011, ZZZZ_h, "ld4h", simm4s4>;
  defm LD2W_IMM : sve_mem_eld_si<0b10, 0b001, ZZ_s,   "ld2w", simm4s2>;
  defm LD3W_IMM : sve_mem_eld_si<0b10, 0b010, ZZZ_s,  "ld3w", simm4s3>;
  defm LD4W_IMM : sve_mem_eld_si<0b10, 0b011, ZZZZ_s, "ld4w", simm4s4>;
  defm LD2D_IMM : sve_mem_eld_si<0b11, 0b001, ZZ_d,   "ld2d", simm4s2>;
  defm LD3D_IMM : sve_mem_eld_si<0b11, 0b010, ZZZ_d,  "ld3d", simm4s3>;
  defm LD4D_IMM : sve_mem_eld_si<0b11, 0b011, ZZZZ_d, "ld4d", simm4s4>;
  let Predicates = [HasSVE2p1_or_HasSME2p1] in {
  defm LD2Q_IMM : sve_mem_eld_si<0b01, 0b100, ZZ_q,   "ld2q", simm4s2>;
  defm LD3Q_IMM : sve_mem_eld_si<0b10, 0b100, ZZZ_q,  "ld3q", simm4s3>;
  defm LD4Q_IMM : sve_mem_eld_si<0b11, 0b100, ZZZZ_q, "ld4q", simm4s4>;
  }

  // LD(2|3|4) structured loads (register + register)
  def LD2B : sve_mem_eld_ss<0b00, 0b101, ZZ_b,   "ld2b", GPR64NoXZRshifted8>;
  def LD3B : sve_mem_eld_ss<0b00, 0b110, ZZZ_b,  "ld3b", GPR64NoXZRshifted8>;
  def LD4B : sve_mem_eld_ss<0b00, 0b111, ZZZZ_b, "ld4b", GPR64NoXZRshifted8>;
  def LD2H : sve_mem_eld_ss<0b01, 0b101, ZZ_h,   "ld2h", GPR64NoXZRshifted16>;
  def LD3H : sve_mem_eld_ss<0b01, 0b110, ZZZ_h,  "ld3h", GPR64NoXZRshifted16>;
  def LD4H : sve_mem_eld_ss<0b01, 0b111, ZZZZ_h, "ld4h", GPR64NoXZRshifted16>;
  def LD2W : sve_mem_eld_ss<0b10, 0b101, ZZ_s,   "ld2w", GPR64NoXZRshifted32>;
  def LD3W : sve_mem_eld_ss<0b10, 0b110, ZZZ_s,  "ld3w", GPR64NoXZRshifted32>;
  def LD4W : sve_mem_eld_ss<0b10, 0b111, ZZZZ_s, "ld4w", GPR64NoXZRshifted32>;
  def LD2D : sve_mem_eld_ss<0b11, 0b101, ZZ_d,   "ld2d", GPR64NoXZRshifted64>;
  def LD3D : sve_mem_eld_ss<0b11, 0b110, ZZZ_d,  "ld3d", GPR64NoXZRshifted64>;
  def LD4D : sve_mem_eld_ss<0b11, 0b111, ZZZZ_d, "ld4d", GPR64NoXZRshifted64>;
  let Predicates = [HasSVE2p1_or_HasSME2p1] in {
  def LD2Q : sve_mem_eld_ss<0b01, 0b001, ZZ_q,   "ld2q", GPR64NoXZRshifted128>;
  def LD3Q : sve_mem_eld_ss<0b10, 0b001, ZZZ_q,  "ld3q", GPR64NoXZRshifted128>;
  def LD4Q : sve_mem_eld_ss<0b11, 0b001, ZZZZ_q, "ld4q", GPR64NoXZRshifted128>;
  }
} // End HasSVEorSME

let Predicates = [HasSVE] in {
  // Gathers using unscaled 32-bit offsets, e.g.
  //    ld1h z0.s, p0/z, [x0, z0.s, uxtw]
  defm GLD1SB_S   : sve_mem_32b_gld_vs_32_unscaled<0b0000, "ld1sb",   AArch64ld1s_gather_sxtw_z,   AArch64ld1s_gather_uxtw_z,   ZPR32ExtSXTW8Only, ZPR32ExtUXTW8Only, nxv4i8>;
  defm GLDFF1SB_S : sve_mem_32b_gld_vs_32_unscaled<0b0001, "ldff1sb", AArch64ldff1s_gather_sxtw_z, AArch64ldff1s_gather_uxtw_z, ZPR32ExtSXTW8Only, ZPR32ExtUXTW8Only, nxv4i8>;
  defm GLD1B_S    : sve_mem_32b_gld_vs_32_unscaled<0b0010, "ld1b",    AArch64ld1_gather_sxtw_z,    AArch64ld1_gather_uxtw_z,    ZPR32ExtSXTW8Only, ZPR32ExtUXTW8Only, nxv4i8>;
  defm GLDFF1B_S  : sve_mem_32b_gld_vs_32_unscaled<0b0011, "ldff1b",  AArch64ldff1_gather_sxtw_z,  AArch64ldff1_gather_uxtw_z,  ZPR32ExtSXTW8Only, ZPR32ExtUXTW8Only, nxv4i8>;
  defm GLD1SH_S   : sve_mem_32b_gld_vs_32_unscaled<0b0100, "ld1sh",   AArch64ld1s_gather_sxtw_z,   AArch64ld1s_gather_uxtw_z,   ZPR32ExtSXTW8,     ZPR32ExtUXTW8,     nxv4i16>;
  defm GLDFF1SH_S : sve_mem_32b_gld_vs_32_unscaled<0b0101, "ldff1sh", AArch64ldff1s_gather_sxtw_z, AArch64ldff1s_gather_uxtw_z, ZPR32ExtSXTW8,     ZPR32ExtUXTW8,     nxv4i16>;
  defm GLD1H_S    : sve_mem_32b_gld_vs_32_unscaled<0b0110, "ld1h",    AArch64ld1_gather_sxtw_z,    AArch64ld1_gather_uxtw_z,    ZPR32ExtSXTW8,     ZPR32ExtUXTW8,     nxv4i16>;
  defm GLDFF1H_S  : sve_mem_32b_gld_vs_32_unscaled<0b0111, "ldff1h",  AArch64ldff1_gather_sxtw_z,  AArch64ldff1_gather_uxtw_z,  ZPR32ExtSXTW8,     ZPR32ExtUXTW8,     nxv4i16>;
  defm GLD1W      : sve_mem_32b_gld_vs_32_unscaled<0b1010, "ld1w",    AArch64ld1_gather_sxtw_z,    AArch64ld1_gather_uxtw_z,    ZPR32ExtSXTW8,     ZPR32ExtUXTW8,     nxv4i32>;
  defm GLDFF1W    : sve_mem_32b_gld_vs_32_unscaled<0b1011, "ldff1w",  AArch64ldff1_gather_sxtw_z,  AArch64ldff1_gather_uxtw_z,  ZPR32ExtSXTW8,     ZPR32ExtUXTW8,     nxv4i32>;

  // Gathers using scaled 32-bit offsets, e.g.
  //    ld1h z0.s, p0/z, [x0, z0.s, uxtw #1]
  defm GLD1SH_S   : sve_mem_32b_gld_sv_32_scaled<0b0100, "ld1sh",   AArch64ld1s_gather_sxtw_scaled_z,   AArch64ld1s_gather_uxtw_scaled_z,   ZPR32ExtSXTW16, ZPR32ExtUXTW16, nxv4i16>;
  defm GLDFF1SH_S : sve_mem_32b_gld_sv_32_scaled<0b0101, "ldff1sh", AArch64ldff1s_gather_sxtw_scaled_z, AArch64ldff1s_gather_uxtw_scaled_z, ZPR32ExtSXTW16, ZPR32ExtUXTW16, nxv4i16>;
  defm GLD1H_S    : sve_mem_32b_gld_sv_32_scaled<0b0110, "ld1h",    AArch64ld1_gather_sxtw_scaled_z,    AArch64ld1_gather_uxtw_scaled_z,    ZPR32ExtSXTW16, ZPR32ExtUXTW16, nxv4i16>;
  defm GLDFF1H_S  : sve_mem_32b_gld_sv_32_scaled<0b0111, "ldff1h",  AArch64ldff1_gather_sxtw_scaled_z,  AArch64ldff1_gather_uxtw_scaled_z,  ZPR32ExtSXTW16, ZPR32ExtUXTW16, nxv4i16>;
  defm GLD1W      : sve_mem_32b_gld_sv_32_scaled<0b1010, "ld1w",    AArch64ld1_gather_sxtw_scaled_z,    AArch64ld1_gather_uxtw_scaled_z,    ZPR32ExtSXTW32, ZPR32ExtUXTW32, nxv4i32>;
  defm GLDFF1W    : sve_mem_32b_gld_sv_32_scaled<0b1011, "ldff1w",  AArch64ldff1_gather_sxtw_scaled_z,  AArch64ldff1_gather_uxtw_scaled_z,  ZPR32ExtSXTW32, ZPR32ExtUXTW32, nxv4i32>;

  // Gathers using 32-bit pointers with scaled offset, e.g.
  //    ld1h z0.s, p0/z, [z0.s, #16]
  defm GLD1SB_S   : sve_mem_32b_gld_vi_32_ptrs<0b0000, "ld1sb",   imm0_31, AArch64ld1s_gather_imm_z,   nxv4i8>;
  defm GLDFF1SB_S : sve_mem_32b_gld_vi_32_ptrs<0b0001, "ldff1sb", imm0_31, AArch64ldff1s_gather_imm_z, nxv4i8>;
  defm GLD1B_S    : sve_mem_32b_gld_vi_32_ptrs<0b0010, "ld1b",    imm0_31, AArch64ld1_gather_imm_z,    nxv4i8>;
  defm GLDFF1B_S  : sve_mem_32b_gld_vi_32_ptrs<0b0011, "ldff1b",  imm0_31, AArch64ldff1_gather_imm_z,  nxv4i8>;
  defm GLD1SH_S   : sve_mem_32b_gld_vi_32_ptrs<0b0100, "ld1sh",   uimm5s2, AArch64ld1s_gather_imm_z,   nxv4i16>;
  defm GLDFF1SH_S : sve_mem_32b_gld_vi_32_ptrs<0b0101, "ldff1sh", uimm5s2, AArch64ldff1s_gather_imm_z, nxv4i16>;
  defm GLD1H_S    : sve_mem_32b_gld_vi_32_ptrs<0b0110, "ld1h",    uimm5s2, AArch64ld1_gather_imm_z,    nxv4i16>;
  defm GLDFF1H_S  : sve_mem_32b_gld_vi_32_ptrs<0b0111, "ldff1h",  uimm5s2, AArch64ldff1_gather_imm_z,  nxv4i16>;
  defm GLD1W      : sve_mem_32b_gld_vi_32_ptrs<0b1010, "ld1w",    uimm5s4, AArch64ld1_gather_imm_z,    nxv4i32>;
  defm GLDFF1W    : sve_mem_32b_gld_vi_32_ptrs<0b1011, "ldff1w",  uimm5s4, AArch64ldff1_gather_imm_z,  nxv4i32>;

  // Gathers using 64-bit pointers with scaled offset, e.g.
  //    ld1h z0.d, p0/z, [z0.d, #16]
  defm GLD1SB_D   : sve_mem_64b_gld_vi_64_ptrs<0b0000, "ld1sb",   imm0_31, AArch64ld1s_gather_imm_z,   nxv2i8>;
  defm GLDFF1SB_D : sve_mem_64b_gld_vi_64_ptrs<0b0001, "ldff1sb", imm0_31, AArch64ldff1s_gather_imm_z, nxv2i8>;
  defm GLD1B_D    : sve_mem_64b_gld_vi_64_ptrs<0b0010, "ld1b",    imm0_31, AArch64ld1_gather_imm_z,    nxv2i8>;
  defm GLDFF1B_D  : sve_mem_64b_gld_vi_64_ptrs<0b0011, "ldff1b",  imm0_31, AArch64ldff1_gather_imm_z,  nxv2i8>;
  defm GLD1SH_D   : sve_mem_64b_gld_vi_64_ptrs<0b0100, "ld1sh",   uimm5s2, AArch64ld1s_gather_imm_z,   nxv2i16>;
  defm GLDFF1SH_D : sve_mem_64b_gld_vi_64_ptrs<0b0101, "ldff1sh", uimm5s2, AArch64ldff1s_gather_imm_z, nxv2i16>;
  defm GLD1H_D    : sve_mem_64b_gld_vi_64_ptrs<0b0110, "ld1h",    uimm5s2, AArch64ld1_gather_imm_z,    nxv2i16>;
  defm GLDFF1H_D  : sve_mem_64b_gld_vi_64_ptrs<0b0111, "ldff1h",  uimm5s2, AArch64ldff1_gather_imm_z,  nxv2i16>;
  defm GLD1SW_D   : sve_mem_64b_gld_vi_64_ptrs<0b1000, "ld1sw",   uimm5s4, AArch64ld1s_gather_imm_z,   nxv2i32>;
  defm GLDFF1SW_D : sve_mem_64b_gld_vi_64_ptrs<0b1001, "ldff1sw", uimm5s4, AArch64ldff1s_gather_imm_z, nxv2i32>;
  defm GLD1W_D    : sve_mem_64b_gld_vi_64_ptrs<0b1010, "ld1w",    uimm5s4, AArch64ld1_gather_imm_z,    nxv2i32>;
  defm GLDFF1W_D  : sve_mem_64b_gld_vi_64_ptrs<0b1011, "ldff1w",  uimm5s4, AArch64ldff1_gather_imm_z,  nxv2i32>;
  defm GLD1D      : sve_mem_64b_gld_vi_64_ptrs<0b1110, "ld1d",    uimm5s8, AArch64ld1_gather_imm_z,    nxv2i64>;
  defm GLDFF1D    : sve_mem_64b_gld_vi_64_ptrs<0b1111, "ldff1d",  uimm5s8, AArch64ldff1_gather_imm_z,  nxv2i64>;

  // Gathers using unscaled 64-bit offsets, e.g.
  //    ld1h z0.d, p0/z, [x0, z0.d]
  defm GLD1SB_D   : sve_mem_64b_gld_vs2_64_unscaled<0b0000, "ld1sb",   AArch64ld1s_gather_z,   nxv2i8>;
  defm GLDFF1SB_D : sve_mem_64b_gld_vs2_64_unscaled<0b0001, "ldff1sb", AArch64ldff1s_gather_z, nxv2i8>;
  defm GLD1B_D    : sve_mem_64b_gld_vs2_64_unscaled<0b0010, "ld1b",    AArch64ld1_gather_z,    nxv2i8>;
  defm GLDFF1B_D  : sve_mem_64b_gld_vs2_64_unscaled<0b0011, "ldff1b",  AArch64ldff1_gather_z,  nxv2i8>;
  defm GLD1SH_D   : sve_mem_64b_gld_vs2_64_unscaled<0b0100, "ld1sh",   AArch64ld1s_gather_z,   nxv2i16>;
  defm GLDFF1SH_D : sve_mem_64b_gld_vs2_64_unscaled<0b0101, "ldff1sh", AArch64ldff1s_gather_z, nxv2i16>;
  defm GLD1H_D    : sve_mem_64b_gld_vs2_64_unscaled<0b0110, "ld1h",    AArch64ld1_gather_z,    nxv2i16>;
  defm GLDFF1H_D  : sve_mem_64b_gld_vs2_64_unscaled<0b0111, "ldff1h",  AArch64ldff1_gather_z,  nxv2i16>;
  defm GLD1SW_D   : sve_mem_64b_gld_vs2_64_unscaled<0b1000, "ld1sw",   AArch64ld1s_gather_z,   nxv2i32>;
  defm GLDFF1SW_D : sve_mem_64b_gld_vs2_64_unscaled<0b1001, "ldff1sw", AArch64ldff1s_gather_z, nxv2i32>;
  defm GLD1W_D    : sve_mem_64b_gld_vs2_64_unscaled<0b1010, "ld1w",    AArch64ld1_gather_z,    nxv2i32>;
  defm GLDFF1W_D  : sve_mem_64b_gld_vs2_64_unscaled<0b1011, "ldff1w",  AArch64ldff1_gather_z,  nxv2i32>;
  defm GLD1D      : sve_mem_64b_gld_vs2_64_unscaled<0b1110, "ld1d",    AArch64ld1_gather_z,    nxv2i64>;
  defm GLDFF1D    : sve_mem_64b_gld_vs2_64_unscaled<0b1111, "ldff1d",  AArch64ldff1_gather_z,  nxv2i64>;
  let Predicates = [HasSVE2p1] in {
  defm GLD1Q      : sve_mem_128b_gld_64_unscaled<"ld1q", AArch64ld1q_gather_z>;
  }

  // Gathers using scaled 64-bit offsets, e.g.
  //    ld1h z0.d, p0/z, [x0, z0.d, lsl #1]
  defm GLD1SH_D   : sve_mem_64b_gld_sv2_64_scaled<0b0100, "ld1sh",    AArch64ld1s_gather_scaled_z,   ZPR64ExtLSL16, nxv2i16>;
  defm GLDFF1SH_D : sve_mem_64b_gld_sv2_64_scaled<0b0101, "ldff1sh",  AArch64ldff1s_gather_scaled_z, ZPR64ExtLSL16, nxv2i16>;
  defm GLD1H_D    : sve_mem_64b_gld_sv2_64_scaled<0b0110, "ld1h",     AArch64ld1_gather_scaled_z,    ZPR64ExtLSL16, nxv2i16>;
  defm GLDFF1H_D  : sve_mem_64b_gld_sv2_64_scaled<0b0111, "ldff1h",   AArch64ldff1_gather_scaled_z,  ZPR64ExtLSL16, nxv2i16>;
  defm GLD1SW_D   : sve_mem_64b_gld_sv2_64_scaled<0b1000, "ld1sw",    AArch64ld1s_gather_scaled_z,   ZPR64ExtLSL32, nxv2i32>;
  defm GLDFF1SW_D : sve_mem_64b_gld_sv2_64_scaled<0b1001, "ldff1sw",  AArch64ldff1s_gather_scaled_z, ZPR64ExtLSL32, nxv2i32>;
  defm GLD1W_D    : sve_mem_64b_gld_sv2_64_scaled<0b1010, "ld1w",     AArch64ld1_gather_scaled_z,    ZPR64ExtLSL32, nxv2i32>;
  defm GLDFF1W_D  : sve_mem_64b_gld_sv2_64_scaled<0b1011, "ldff1w",   AArch64ldff1_gather_scaled_z,  ZPR64ExtLSL32, nxv2i32>;
  defm GLD1D      : sve_mem_64b_gld_sv2_64_scaled<0b1110, "ld1d",     AArch64ld1_gather_scaled_z,    ZPR64ExtLSL64, nxv2i64>;
  defm GLDFF1D    : sve_mem_64b_gld_sv2_64_scaled<0b1111, "ldff1d",   AArch64ldff1_gather_scaled_z,  ZPR64ExtLSL64, nxv2i64>;

  // Gathers using unscaled 32-bit offsets unpacked in 64-bits elements, e.g.
  //    ld1h z0.d, p0/z, [x0, z0.d, uxtw]
  defm GLD1SB_D   : sve_mem_64b_gld_vs_32_unscaled<0b0000, "ld1sb",   AArch64ld1s_gather_sxtw_z,   AArch64ld1s_gather_uxtw_z,   ZPR64ExtSXTW8Only, ZPR64ExtUXTW8Only, nxv2i8>;
  defm GLDFF1SB_D : sve_mem_64b_gld_vs_32_unscaled<0b0001, "ldff1sb", AArch64ldff1s_gather_sxtw_z, AArch64ldff1s_gather_uxtw_z, ZPR64ExtSXTW8Only, ZPR64ExtUXTW8Only, nxv2i8>;
  defm GLD1B_D    : sve_mem_64b_gld_vs_32_unscaled<0b0010, "ld1b",    AArch64ld1_gather_sxtw_z,    AArch64ld1_gather_uxtw_z,    ZPR64ExtSXTW8Only, ZPR64ExtUXTW8Only, nxv2i8>;
  defm GLDFF1B_D  : sve_mem_64b_gld_vs_32_unscaled<0b0011, "ldff1b",  AArch64ldff1_gather_sxtw_z,  AArch64ldff1_gather_uxtw_z,  ZPR64ExtSXTW8Only, ZPR64ExtUXTW8Only, nxv2i8>;
  defm GLD1SH_D   : sve_mem_64b_gld_vs_32_unscaled<0b0100, "ld1sh",   AArch64ld1s_gather_sxtw_z,   AArch64ld1s_gather_uxtw_z,   ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i16>;
  defm GLDFF1SH_D : sve_mem_64b_gld_vs_32_unscaled<0b0101, "ldff1sh", AArch64ldff1s_gather_sxtw_z, AArch64ldff1s_gather_uxtw_z, ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i16>;
  defm GLD1H_D    : sve_mem_64b_gld_vs_32_unscaled<0b0110, "ld1h",    AArch64ld1_gather_sxtw_z,    AArch64ld1_gather_uxtw_z,    ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i16>;
  defm GLDFF1H_D  : sve_mem_64b_gld_vs_32_unscaled<0b0111, "ldff1h",  AArch64ldff1_gather_sxtw_z,  AArch64ldff1_gather_uxtw_z,  ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i16>;
  defm GLD1SW_D   : sve_mem_64b_gld_vs_32_unscaled<0b1000, "ld1sw",   AArch64ld1s_gather_sxtw_z,   AArch64ld1s_gather_uxtw_z,   ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i32>;
  defm GLDFF1SW_D : sve_mem_64b_gld_vs_32_unscaled<0b1001, "ldff1sw", AArch64ldff1s_gather_sxtw_z, AArch64ldff1s_gather_uxtw_z, ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i32>;
  defm GLD1W_D    : sve_mem_64b_gld_vs_32_unscaled<0b1010, "ld1w",    AArch64ld1_gather_sxtw_z,    AArch64ld1_gather_uxtw_z,    ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i32>;
  defm GLDFF1W_D  : sve_mem_64b_gld_vs_32_unscaled<0b1011, "ldff1w",  AArch64ldff1_gather_sxtw_z,  AArch64ldff1_gather_uxtw_z,  ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i32>;
  defm GLD1D      : sve_mem_64b_gld_vs_32_unscaled<0b1110, "ld1d",    AArch64ld1_gather_sxtw_z,    AArch64ld1_gather_uxtw_z,    ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i64>;
  defm GLDFF1D    : sve_mem_64b_gld_vs_32_unscaled<0b1111, "ldff1d",  AArch64ldff1_gather_sxtw_z,  AArch64ldff1_gather_uxtw_z,  ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i64>;

  // Gathers using scaled 32-bit offsets unpacked in 64-bits elements, e.g.
  //    ld1h z0.d, p0/z, [x0, z0.d, uxtw #1]
  defm GLD1SH_D   : sve_mem_64b_gld_sv_32_scaled<0b0100, "ld1sh",   AArch64ld1s_gather_sxtw_scaled_z,   AArch64ld1s_gather_uxtw_scaled_z,   ZPR64ExtSXTW16, ZPR64ExtUXTW16, nxv2i16>;
  defm GLDFF1SH_D : sve_mem_64b_gld_sv_32_scaled<0b0101, "ldff1sh", AArch64ldff1s_gather_sxtw_scaled_z, AArch64ldff1s_gather_uxtw_scaled_z, ZPR64ExtSXTW16, ZPR64ExtUXTW16, nxv2i16>;
  defm GLD1H_D    : sve_mem_64b_gld_sv_32_scaled<0b0110, "ld1h",    AArch64ld1_gather_sxtw_scaled_z,    AArch64ld1_gather_uxtw_scaled_z,    ZPR64ExtSXTW16, ZPR64ExtUXTW16, nxv2i16>;
  defm GLDFF1H_D  : sve_mem_64b_gld_sv_32_scaled<0b0111, "ldff1h",  AArch64ldff1_gather_sxtw_scaled_z,  AArch64ldff1_gather_uxtw_scaled_z,  ZPR64ExtSXTW16, ZPR64ExtUXTW16, nxv2i16>;
  defm GLD1SW_D   : sve_mem_64b_gld_sv_32_scaled<0b1000, "ld1sw",   AArch64ld1s_gather_sxtw_scaled_z,   AArch64ld1s_gather_uxtw_scaled_z,   ZPR64ExtSXTW32, ZPR64ExtUXTW32, nxv2i32>;
  defm GLDFF1SW_D : sve_mem_64b_gld_sv_32_scaled<0b1001, "ldff1sw", AArch64ldff1s_gather_sxtw_scaled_z, AArch64ldff1s_gather_uxtw_scaled_z, ZPR64ExtSXTW32, ZPR64ExtUXTW32, nxv2i32>;
  defm GLD1W_D    : sve_mem_64b_gld_sv_32_scaled<0b1010, "ld1w",    AArch64ld1_gather_sxtw_scaled_z,    AArch64ld1_gather_uxtw_scaled_z,    ZPR64ExtSXTW32, ZPR64ExtUXTW32, nxv2i32>;
  defm GLDFF1W_D  : sve_mem_64b_gld_sv_32_scaled<0b1011, "ldff1w",  AArch64ldff1_gather_sxtw_scaled_z,  AArch64ldff1_gather_uxtw_scaled_z,  ZPR64ExtSXTW32, ZPR64ExtUXTW32, nxv2i32>;
  defm GLD1D      : sve_mem_64b_gld_sv_32_scaled<0b1110, "ld1d",    AArch64ld1_gather_sxtw_scaled_z,    AArch64ld1_gather_uxtw_scaled_z,    ZPR64ExtSXTW64, ZPR64ExtUXTW64, nxv2i64>;
  defm GLDFF1D    : sve_mem_64b_gld_sv_32_scaled<0b1111, "ldff1d",  AArch64ldff1_gather_sxtw_scaled_z,  AArch64ldff1_gather_uxtw_scaled_z,  ZPR64ExtSXTW64, ZPR64ExtUXTW64, nxv2i64>;

  multiclass sve_masked_gather_x2_scaled<ValueType Ty, SDPatternOperator Load, string Inst> {
    // base + vector of scaled offsets
    def : Pat<(Ty (Load (SVEDup0Undef), nxv2i1:$gp, GPR64:$base, nxv2i64:$offs)),
              (!cast<Instruction>(Inst # _SCALED) PPR:$gp, GPR64:$base, ZPR:$offs)>;
    // base + vector of signed 32bit scaled offsets
    def : Pat<(Ty (Load (SVEDup0Undef), nxv2i1:$gp, GPR64:$base, (sext_inreg nxv2i64:$offs, nxv2i32))),
              (!cast<Instruction>(Inst # _SXTW_SCALED) PPR:$gp, GPR64:$base, ZPR:$offs)>;
    // base + vector of unsigned 32bit scaled offsets
    def : Pat<(Ty (Load (SVEDup0Undef), nxv2i1:$gp, GPR64:$base, (and nxv2i64:$offs, (nxv2i64 (splat_vector (i64 0xFFFFFFFF)))))),
              (!cast<Instruction>(Inst # _UXTW_SCALED) PPR:$gp, GPR64:$base, ZPR:$offs)>;
  }

  multiclass sve_masked_gather_x2_unscaled<ValueType Ty, SDPatternOperator Load, string Inst, Operand ImmTy> {
    // vector of pointers + immediate offset (includes zero)
    def : Pat<(Ty (Load (SVEDup0Undef), nxv2i1:$gp, (i64 ImmTy:$imm), nxv2i64:$ptrs)),
              (!cast<Instruction>(Inst # _IMM) PPR:$gp, ZPR:$ptrs, ImmTy:$imm)>;
    // base + vector of offsets
    def : Pat<(Ty (Load (SVEDup0Undef), nxv2i1:$gp, GPR64:$base, nxv2i64:$offs)),
              (!cast<Instruction>(Inst) PPR:$gp, GPR64:$base, ZPR:$offs)>;
    // base + vector of signed 32bit offsets
    def : Pat<(Ty (Load (SVEDup0Undef), nxv2i1:$gp, GPR64:$base, (sext_inreg nxv2i64:$offs, nxv2i32))),
              (!cast<Instruction>(Inst # _SXTW) PPR:$gp, GPR64:$base, ZPR:$offs)>;
    // base + vector of unsigned 32bit offsets
    def : Pat<(Ty (Load (SVEDup0Undef), nxv2i1:$gp, GPR64:$base, (and nxv2i64:$offs, (nxv2i64 (splat_vector (i64 0xFFFFFFFF)))))),
              (!cast<Instruction>(Inst # _UXTW) PPR:$gp, GPR64:$base, ZPR:$offs)>;
  }

  multiclass sve_masked_gather_x4<ValueType Ty, SDPatternOperator Load, Instruction Inst> {
    def : Pat<(Ty (Load (SVEDup0Undef), nxv4i1:$gp, GPR64:$base, nxv4i32:$offs)),
              (Inst PPR:$gp, GPR64:$base, ZPR:$offs)>;
  }

  defm : sve_masked_gather_x2_scaled<nxv2i64,  azext_masked_gather_i16_signed_scaled, "GLD1H_D">;
  defm : sve_masked_gather_x2_scaled<nxv2i64,  sext_masked_gather_i16_signed_scaled,  "GLD1SH_D">;
  defm : sve_masked_gather_x2_scaled<nxv2i64,  azext_masked_gather_i32_signed_scaled, "GLD1W_D">;
  defm : sve_masked_gather_x2_scaled<nxv2i64,  sext_masked_gather_i32_signed_scaled,  "GLD1SW_D">;
  defm : sve_masked_gather_x2_scaled<nxv2i64,  nonext_masked_gather_signed_scaled,    "GLD1D">;
  defm : sve_masked_gather_x2_scaled<nxv2f16,  nonext_masked_gather_signed_scaled,    "GLD1H_D">;
  defm : sve_masked_gather_x2_scaled<nxv2f32,  nonext_masked_gather_signed_scaled,    "GLD1W_D">;
  defm : sve_masked_gather_x2_scaled<nxv2f64,  nonext_masked_gather_signed_scaled,    "GLD1D">;
  defm : sve_masked_gather_x2_scaled<nxv2bf16, nonext_masked_gather_signed_scaled,    "GLD1H_D">;

  defm : sve_masked_gather_x2_unscaled<nxv2i64,  azext_masked_gather_i8_signed_unscaled,  "GLD1B_D" , imm0_31>;
  defm : sve_masked_gather_x2_unscaled<nxv2i64,  sext_masked_gather_i8_signed_unscaled,   "GLD1SB_D", imm0_31>;
  defm : sve_masked_gather_x2_unscaled<nxv2i64,  azext_masked_gather_i16_signed_unscaled, "GLD1H_D",  uimm5s2>;
  defm : sve_masked_gather_x2_unscaled<nxv2i64,  sext_masked_gather_i16_signed_unscaled,  "GLD1SH_D", uimm5s2>;
  defm : sve_masked_gather_x2_unscaled<nxv2i64,  azext_masked_gather_i32_signed_unscaled, "GLD1W_D",  uimm5s4>;
  defm : sve_masked_gather_x2_unscaled<nxv2i64,  sext_masked_gather_i32_signed_unscaled,  "GLD1SW_D", uimm5s4>;
  defm : sve_masked_gather_x2_unscaled<nxv2i64,  nonext_masked_gather_signed_unscaled,    "GLD1D",    uimm5s8>;
  defm : sve_masked_gather_x2_unscaled<nxv2f16,  nonext_masked_gather_signed_unscaled,    "GLD1H_D",  uimm5s2>;
  defm : sve_masked_gather_x2_unscaled<nxv2f32,  nonext_masked_gather_signed_unscaled,    "GLD1W_D",  uimm5s4>;
  defm : sve_masked_gather_x2_unscaled<nxv2f64,  nonext_masked_gather_signed_unscaled,    "GLD1D",    uimm5s8>;
  defm : sve_masked_gather_x2_unscaled<nxv2bf16, nonext_masked_gather_signed_unscaled,    "GLD1H_D",  uimm5s2>;

  defm : sve_masked_gather_x4<nxv4i32,  azext_masked_gather_i16_signed_scaled, GLD1H_S_SXTW_SCALED>;
  defm : sve_masked_gather_x4<nxv4i32,  sext_masked_gather_i16_signed_scaled,  GLD1SH_S_SXTW_SCALED>;
  defm : sve_masked_gather_x4<nxv4i32,  nonext_masked_gather_signed_scaled,    GLD1W_SXTW_SCALED>;
  defm : sve_masked_gather_x4<nxv4f16,  nonext_masked_gather_signed_scaled,    GLD1H_S_SXTW_SCALED>;
  defm : sve_masked_gather_x4<nxv4f32,  nonext_masked_gather_signed_scaled,    GLD1W_SXTW_SCALED>;
  defm : sve_masked_gather_x4<nxv4bf16, nonext_masked_gather_signed_scaled,    GLD1H_S_SXTW_SCALED>;

  defm : sve_masked_gather_x4<nxv4i32,  azext_masked_gather_i8_signed_unscaled,  GLD1B_S_SXTW>;
  defm : sve_masked_gather_x4<nxv4i32,  sext_masked_gather_i8_signed_unscaled,   GLD1SB_S_SXTW>;
  defm : sve_masked_gather_x4<nxv4i32,  azext_masked_gather_i16_signed_unscaled, GLD1H_S_SXTW>;
  defm : sve_masked_gather_x4<nxv4i32,  sext_masked_gather_i16_signed_unscaled,  GLD1SH_S_SXTW>;
  defm : sve_masked_gather_x4<nxv4i32,  nonext_masked_gather_signed_unscaled,    GLD1W_SXTW>;
  defm : sve_masked_gather_x4<nxv4f16,  nonext_masked_gather_signed_unscaled,    GLD1H_S_SXTW>;
  defm : sve_masked_gather_x4<nxv4f32,  nonext_masked_gather_signed_unscaled,    GLD1W_SXTW>;
  defm : sve_masked_gather_x4<nxv4bf16, nonext_masked_gather_signed_unscaled,    GLD1H_S_SXTW>;

  defm : sve_masked_gather_x4<nxv4i32,  azext_masked_gather_i16_unsigned_scaled, GLD1H_S_UXTW_SCALED>;
  defm : sve_masked_gather_x4<nxv4i32,  sext_masked_gather_i16_unsigned_scaled,  GLD1SH_S_UXTW_SCALED>;
  defm : sve_masked_gather_x4<nxv4i32,  nonext_masked_gather_unsigned_scaled,    GLD1W_UXTW_SCALED>;
  defm : sve_masked_gather_x4<nxv4f16,  nonext_masked_gather_unsigned_scaled,    GLD1H_S_UXTW_SCALED>;
  defm : sve_masked_gather_x4<nxv4f32,  nonext_masked_gather_unsigned_scaled,    GLD1W_UXTW_SCALED>;
  defm : sve_masked_gather_x4<nxv4bf16, nonext_masked_gather_unsigned_scaled,    GLD1H_S_UXTW_SCALED>;

  defm : sve_masked_gather_x4<nxv4i32,  azext_masked_gather_i8_unsigned_unscaled,  GLD1B_S_UXTW>;
  defm : sve_masked_gather_x4<nxv4i32,  sext_masked_gather_i8_unsigned_unscaled,   GLD1SB_S_UXTW>;
  defm : sve_masked_gather_x4<nxv4i32,  azext_masked_gather_i16_unsigned_unscaled, GLD1H_S_UXTW>;
  defm : sve_masked_gather_x4<nxv4i32,  sext_masked_gather_i16_unsigned_unscaled,  GLD1SH_S_UXTW>;
  defm : sve_masked_gather_x4<nxv4i32,  nonext_masked_gather_unsigned_unscaled,    GLD1W_UXTW>;
  defm : sve_masked_gather_x4<nxv4f16,  nonext_masked_gather_unsigned_unscaled,    GLD1H_S_UXTW>;
  defm : sve_masked_gather_x4<nxv4f32,  nonext_masked_gather_unsigned_unscaled,    GLD1W_UXTW>;
  defm : sve_masked_gather_x4<nxv4bf16, nonext_masked_gather_unsigned_unscaled,    GLD1H_S_UXTW>;
} // End HasSVE

let Predicates = [HasSVEorSME] in {
  // Non-temporal contiguous loads (register + immediate)
  defm LDNT1B_ZRI : sve_mem_cldnt_si<0b00, "ldnt1b", Z_b, ZPR8>;
  defm LDNT1H_ZRI : sve_mem_cldnt_si<0b01, "ldnt1h", Z_h, ZPR16>;
  defm LDNT1W_ZRI : sve_mem_cldnt_si<0b10, "ldnt1w", Z_s, ZPR32>;
  defm LDNT1D_ZRI : sve_mem_cldnt_si<0b11, "ldnt1d", Z_d, ZPR64>;

  // Non-temporal contiguous loads (register + register)
  defm LDNT1B_ZRR : sve_mem_cldnt_ss<0b00, "ldnt1b", Z_b, ZPR8,  GPR64NoXZRshifted8>;
  defm LDNT1H_ZRR : sve_mem_cldnt_ss<0b01, "ldnt1h", Z_h, ZPR16, GPR64NoXZRshifted16>;
  defm LDNT1W_ZRR : sve_mem_cldnt_ss<0b10, "ldnt1w", Z_s, ZPR32, GPR64NoXZRshifted32>;
  defm LDNT1D_ZRR : sve_mem_cldnt_ss<0b11, "ldnt1d", Z_d, ZPR64, GPR64NoXZRshifted64>;

  // contiguous store with immediates
  defm ST1B_IMM   : sve_mem_cst_si<0b00, 0b00, "st1b", Z_b, ZPR8>;
  defm ST1B_H_IMM : sve_mem_cst_si<0b00, 0b01, "st1b", Z_h, ZPR16>;
  defm ST1B_S_IMM : sve_mem_cst_si<0b00, 0b10, "st1b", Z_s, ZPR32>;
  defm ST1B_D_IMM : sve_mem_cst_si<0b00, 0b11, "st1b", Z_d, ZPR64>;
  defm ST1H_IMM   : sve_mem_cst_si<0b01, 0b01, "st1h", Z_h, ZPR16>;
  defm ST1H_S_IMM : sve_mem_cst_si<0b01, 0b10, "st1h", Z_s, ZPR32>;
  defm ST1H_D_IMM : sve_mem_cst_si<0b01, 0b11, "st1h", Z_d, ZPR64>;
  defm ST1W_IMM   : sve_mem_cst_si<0b10, 0b10, "st1w", Z_s, ZPR32>;
  defm ST1W_D_IMM : sve_mem_cst_si<0b10, 0b11, "st1w", Z_d, ZPR64>;
  let Predicates = [HasSVE2p1] in {
  defm ST1W_Q_IMM : sve_mem_cst_si<0b10, 0b00, "st1w", Z_q, ZPR128>;
  }
  defm ST1D_IMM   : sve_mem_cst_si<0b11, 0b11, "st1d", Z_d, ZPR64>;
  let Predicates = [HasSVE2p1] in {
  defm ST1D_Q_IMM : sve_mem_cst_si<0b11, 0b10, "st1d", Z_q, ZPR128>;
  }

  // contiguous store with reg+reg addressing.
  defm ST1B   : sve_mem_cst_ss<0b0000, "st1b", Z_b, ZPR8,  GPR64NoXZRshifted8>;
  defm ST1B_H : sve_mem_cst_ss<0b0001, "st1b", Z_h, ZPR16, GPR64NoXZRshifted8>;
  defm ST1B_S : sve_mem_cst_ss<0b0010, "st1b", Z_s, ZPR32, GPR64NoXZRshifted8>;
  defm ST1B_D : sve_mem_cst_ss<0b0011, "st1b", Z_d, ZPR64, GPR64NoXZRshifted8>;
  defm ST1H   : sve_mem_cst_ss<0b0101, "st1h", Z_h, ZPR16, GPR64NoXZRshifted16>;
  defm ST1H_S : sve_mem_cst_ss<0b0110, "st1h", Z_s, ZPR32, GPR64NoXZRshifted16>;
  defm ST1H_D : sve_mem_cst_ss<0b0111, "st1h", Z_d, ZPR64, GPR64NoXZRshifted16>;
  defm ST1W   : sve_mem_cst_ss<0b1010, "st1w", Z_s, ZPR32, GPR64NoXZRshifted32>;
  defm ST1W_D : sve_mem_cst_ss<0b1011, "st1w", Z_d, ZPR64, GPR64NoXZRshifted32>;
  let Predicates = [HasSVE2p1] in {
  defm ST1W_Q : sve_mem_cst_ss<0b1000, "st1w", Z_q, ZPR128, GPR64NoXZRshifted32>;
  }
  defm ST1D   : sve_mem_cst_ss<0b1111, "st1d", Z_d, ZPR64, GPR64NoXZRshifted64>;
  let Predicates = [HasSVE2p1] in {
  defm ST1D_Q : sve_mem_cst_ss<0b1110, "st1d", Z_q, ZPR128, GPR64NoXZRshifted64>;
  }

  multiclass sve_ld1q_pat<ValueType Ty, ValueType PredTy, SDPatternOperator Load1qOp, Instruction RegRegInst, Instruction RegImmInst, ComplexPattern AddrCP> {
    let AddedComplexity = 2 in {
      def _reg_imm : Pat<(Ty (Load1qOp (PredTy PPR3bAny:$Pg), (am_sve_indexed_s4 GPR64sp:$base, simm4s1:$imm))),
                        (RegImmInst PPR3bAny:$Pg, GPR64sp:$base, simm4s1:$imm)>;
    }

    let AddedComplexity = 1 in {
      def _reg_reg : Pat<(Ty (Load1qOp (PredTy PPR3bAny:$Pg), (AddrCP GPR64sp:$base, GPR64:$offset))),
                        (RegRegInst PPR3bAny:$Pg, GPR64sp:$base, GPR64:$offset)>;
    }

    def _default : Pat<(Ty (Load1qOp (PredTy PPR3bAny:$Pg), (i64 GPR64sp:$base))),
                     (RegImmInst PPR3bAny:$Pg, GPR64sp:$base, (i64 0))>;
  }

  multiclass sve_st1q_pat<ValueType DataType, ValueType PredTy, SDPatternOperator Store1qOp, Instruction RegRegInst, Instruction RegImmInst, ComplexPattern AddrCP> {
    let AddedComplexity = 2 in {
      def _reg_imm : Pat<(Store1qOp (DataType ZPR128:$Zt), (PredTy PPR3bAny:$Pg), (am_sve_indexed_s4 GPR64sp:$base, simm4s1:$imm)),
                        (RegImmInst Z_q:$Zt, PPR3bAny:$Pg, GPR64sp:$base, simm4s1:$imm)>;
    }

    let AddedComplexity = 1 in {
      def _reg_reg : Pat<(Store1qOp (DataType ZPR128:$Zt), (PredTy PPR3bAny:$Pg), (AddrCP GPR64sp:$base, GPR64:$offset)),
                        (RegRegInst Z_q:$Zt, PPR3bAny:$Pg, GPR64sp:$base, GPR64:$offset)>;
    }

    def _default : Pat<(Store1qOp (DataType ZPR128:$Zt), (PredTy PPR3bAny:$Pg), (i64 GPR64sp:$base)),
                        (RegImmInst Z_q:$Zt, PPR3bAny:$Pg, GPR64sp:$base, (i64 0))>;
  }

  // ld1quw/st1qw
  defm : sve_ld1q_pat<nxv4i32, nxv1i1, int_aarch64_sve_ld1uwq, LD1W_Q, LD1W_Q_IMM, am_sve_regreg_lsl2>;
  defm : sve_ld1q_pat<nxv4f32, nxv1i1, int_aarch64_sve_ld1uwq, LD1W_Q, LD1W_Q_IMM, am_sve_regreg_lsl2>;
  defm : sve_st1q_pat<nxv4i32, nxv1i1, int_aarch64_sve_st1wq,  ST1W_Q, ST1W_Q_IMM, am_sve_regreg_lsl2>;
  defm : sve_st1q_pat<nxv4f32, nxv1i1, int_aarch64_sve_st1wq,  ST1W_Q, ST1W_Q_IMM, am_sve_regreg_lsl2>;

  // ld1qud/st1qd
  defm : sve_ld1q_pat<nxv2i64, nxv1i1, int_aarch64_sve_ld1udq, LD1D_Q, LD1D_Q_IMM, am_sve_regreg_lsl3>;
  defm : sve_ld1q_pat<nxv2f64, nxv1i1, int_aarch64_sve_ld1udq, LD1D_Q, LD1D_Q_IMM, am_sve_regreg_lsl3>;
  defm : sve_st1q_pat<nxv2i64, nxv1i1, int_aarch64_sve_st1dq,  ST1D_Q, ST1D_Q_IMM, am_sve_regreg_lsl3>;
  defm : sve_st1q_pat<nxv2f64, nxv1i1, int_aarch64_sve_st1dq,  ST1D_Q, ST1D_Q_IMM, am_sve_regreg_lsl3>;

} // End HasSVEorSME

let Predicates = [HasSVE] in {
  // Scatters using unpacked, unscaled 32-bit offsets, e.g.
  //    st1h z0.d, p0, [x0, z0.d, uxtw]
  defm SST1B_D : sve_mem_64b_sst_sv_32_unscaled<0b000, "st1b", AArch64st1_scatter_sxtw, AArch64st1_scatter_uxtw, ZPR64ExtSXTW8Only, ZPR64ExtUXTW8Only, nxv2i8>;
  defm SST1H_D : sve_mem_64b_sst_sv_32_unscaled<0b010, "st1h", AArch64st1_scatter_sxtw, AArch64st1_scatter_uxtw, ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i16>;
  defm SST1W_D : sve_mem_64b_sst_sv_32_unscaled<0b100, "st1w", AArch64st1_scatter_sxtw, AArch64st1_scatter_uxtw, ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i32>;
  defm SST1D   : sve_mem_64b_sst_sv_32_unscaled<0b110, "st1d", AArch64st1_scatter_sxtw, AArch64st1_scatter_uxtw, ZPR64ExtSXTW8, ZPR64ExtUXTW8, nxv2i64>;

  // Scatters using packed, unscaled 32-bit offsets, e.g.
  //    st1h z0.s, p0, [x0, z0.s, uxtw]
  defm SST1B_S : sve_mem_32b_sst_sv_32_unscaled<0b001, "st1b", AArch64st1_scatter_sxtw, AArch64st1_scatter_uxtw, ZPR32ExtSXTW8Only, ZPR32ExtUXTW8Only, nxv4i8>;
  defm SST1H_S : sve_mem_32b_sst_sv_32_unscaled<0b011, "st1h", AArch64st1_scatter_sxtw, AArch64st1_scatter_uxtw, ZPR32ExtSXTW8, ZPR32ExtUXTW8, nxv4i16>;
  defm SST1W   : sve_mem_32b_sst_sv_32_unscaled<0b101, "st1w", AArch64st1_scatter_sxtw, AArch64st1_scatter_uxtw, ZPR32ExtSXTW8, ZPR32ExtUXTW8, nxv4i32>;

  // Scatters using packed, scaled 32-bit offsets, e.g.
  //    st1h z0.s, p0, [x0, z0.s, uxtw #1]
  defm SST1H_S : sve_mem_32b_sst_sv_32_scaled<0b011, "st1h", AArch64st1_scatter_sxtw_scaled, AArch64st1_scatter_uxtw_scaled, ZPR32ExtSXTW16, ZPR32ExtUXTW16, nxv4i16>;
  defm SST1W   : sve_mem_32b_sst_sv_32_scaled<0b101, "st1w", AArch64st1_scatter_sxtw_scaled, AArch64st1_scatter_uxtw_scaled, ZPR32ExtSXTW32, ZPR32ExtUXTW32, nxv4i32>;

  // Scatters using unpacked, scaled 32-bit offsets, e.g.
  //    st1h z0.d, p0, [x0, z0.d, uxtw #1]
  defm SST1H_D : sve_mem_64b_sst_sv_32_scaled<0b010, "st1h", AArch64st1_scatter_sxtw_scaled, AArch64st1_scatter_uxtw_scaled, ZPR64ExtSXTW16, ZPR64ExtUXTW16, nxv2i16>;
  defm SST1W_D : sve_mem_64b_sst_sv_32_scaled<0b100, "st1w", AArch64st1_scatter_sxtw_scaled, AArch64st1_scatter_uxtw_scaled, ZPR64ExtSXTW32, ZPR64ExtUXTW32, nxv2i32>;
  defm SST1D   : sve_mem_64b_sst_sv_32_scaled<0b110, "st1d", AArch64st1_scatter_sxtw_scaled, AArch64st1_scatter_uxtw_scaled, ZPR64ExtSXTW64, ZPR64ExtUXTW64, nxv2i64>;

  // Scatters using 32/64-bit pointers with offset, e.g.
  //    st1h z0.s, p0, [z0.s, #16]
  defm SST1B_S : sve_mem_32b_sst_vi_ptrs<0b001, "st1b", imm0_31, AArch64st1_scatter_imm, nxv4i8>;
  defm SST1H_S : sve_mem_32b_sst_vi_ptrs<0b011, "st1h", uimm5s2, AArch64st1_scatter_imm, nxv4i16>;
  defm SST1W   : sve_mem_32b_sst_vi_ptrs<0b101, "st1w", uimm5s4, AArch64st1_scatter_imm, nxv4i32>;

  // Scatters using 32/64-bit pointers with offset, e.g.
  //    st1h z0.d, p0, [z0.d, #16]
  defm SST1B_D : sve_mem_64b_sst_vi_ptrs<0b000, "st1b", imm0_31, AArch64st1_scatter_imm, nxv2i8>;
  defm SST1H_D : sve_mem_64b_sst_vi_ptrs<0b010, "st1h", uimm5s2, AArch64st1_scatter_imm, nxv2i16>;
  defm SST1W_D : sve_mem_64b_sst_vi_ptrs<0b100, "st1w", uimm5s4, AArch64st1_scatter_imm, nxv2i32>;
  defm SST1D   : sve_mem_64b_sst_vi_ptrs<0b110, "st1d", uimm5s8, AArch64st1_scatter_imm, nxv2i64>;

  // Scatters using unscaled 64-bit offsets, e.g.
  //    st1h z0.d, p0, [x0, z0.d]
  defm SST1B_D : sve_mem_sst_sv_64_unscaled<0b00, "st1b", AArch64st1_scatter, nxv2i8>;
  defm SST1H_D : sve_mem_sst_sv_64_unscaled<0b01, "st1h", AArch64st1_scatter, nxv2i16>;
  defm SST1W_D : sve_mem_sst_sv_64_unscaled<0b10, "st1w", AArch64st1_scatter, nxv2i32>;
  defm SST1D   : sve_mem_sst_sv_64_unscaled<0b11, "st1d", AArch64st1_scatter, nxv2i64>;
  let Predicates = [HasSVE2p1] in {
  defm SST1Q   : sve_mem_sst_128b_64_unscaled<"st1q", AArch64st1q_scatter>;
  }

  // Scatters using scaled 64-bit offsets, e.g.
  //    st1h z0.d, p0, [x0, z0.d, lsl #1]
  defm SST1H_D : sve_mem_sst_sv_64_scaled<0b01, "st1h", AArch64st1_scatter_scaled, ZPR64ExtLSL16, nxv2i16>;
  defm SST1W_D : sve_mem_sst_sv_64_scaled<0b10, "st1w", AArch64st1_scatter_scaled, ZPR64ExtLSL32, nxv2i32>;
  defm SST1D   : sve_mem_sst_sv_64_scaled<0b11, "st1d", AArch64st1_scatter_scaled, ZPR64ExtLSL64, nxv2i64>;

  multiclass sve_masked_scatter_x2_scaled<ValueType Ty, SDPatternOperator Store, string Inst> {
    // base + vector of scaled offsets
    def : Pat<(Store Ty:$data, nxv2i1:$gp, GPR64:$base, nxv2i64:$offs),
              (!cast<Instruction>(Inst # _SCALED) ZPR:$data, PPR:$gp, GPR64:$base, ZPR:$offs)>;
    // base + vector of signed 32bit scaled offsets
    def : Pat<(Store Ty:$data, nxv2i1:$gp, GPR64:$base, (sext_inreg nxv2i64:$offs, nxv2i32)),
              (!cast<Instruction>(Inst # _SXTW_SCALED) ZPR:$data, PPR:$gp, GPR64:$base, ZPR:$offs)>;
    // base + vector of unsigned 32bit scaled offsets
    def : Pat<(Store Ty:$data, nxv2i1:$gp, GPR64:$base, (and nxv2i64:$offs, (nxv2i64 (splat_vector (i64 0xFFFFFFFF))))),
              (!cast<Instruction>(Inst # _UXTW_SCALED) ZPR:$data, PPR:$gp, GPR64:$base, ZPR:$offs)>;
  }

  multiclass sve_masked_scatter_x2_unscaled<ValueType Ty, SDPatternOperator Store, string Inst, Operand ImmTy> {
    // vector of pointers + immediate offset (includes zero)
    def : Pat<(Store Ty:$data, nxv2i1:$gp, (i64 ImmTy:$imm), nxv2i64:$ptrs),
              (!cast<Instruction>(Inst # _IMM) ZPR:$data, PPR:$gp, ZPR:$ptrs, ImmTy:$imm)>;
    // base + vector of offsets
    def : Pat<(Store Ty:$data, nxv2i1:$gp, GPR64:$base, nxv2i64:$offs),
              (!cast<Instruction>(Inst) ZPR:$data, PPR:$gp, GPR64:$base, ZPR:$offs)>;
    // base + vector of signed 32bit offsets
    def : Pat<(Store Ty:$data, nxv2i1:$gp, GPR64:$base, (sext_inreg nxv2i64:$offs, nxv2i32)),
              (!cast<Instruction>(Inst # _SXTW) ZPR:$data, PPR:$gp, GPR64:$base, ZPR:$offs)>;
    // base + vector of unsigned 32bit offsets
    def : Pat<(Store Ty:$data, nxv2i1:$gp, GPR64:$base, (and nxv2i64:$offs, (nxv2i64 (splat_vector (i64 0xFFFFFFFF))))),
              (!cast<Instruction>(Inst # _UXTW) ZPR:$data, PPR:$gp, GPR64:$base, ZPR:$offs)>;
  }

  multiclass sve_masked_scatter_x4<ValueType Ty, SDPatternOperator Store, Instruction Inst> {
    def : Pat<(Store Ty:$data, nxv4i1:$gp, GPR64:$base, nxv4i32:$offs),
              (Inst ZPR:$data, PPR:$gp, GPR64:$base, ZPR:$offs)>;
  }

  defm : sve_masked_scatter_x2_scaled<nxv2i64,  trunc_masked_scatter_i16_signed_scaled, "SST1H_D">;
  defm : sve_masked_scatter_x2_scaled<nxv2i64,  trunc_masked_scatter_i32_signed_scaled, "SST1W_D">;
  defm : sve_masked_scatter_x2_scaled<nxv2i64,  nontrunc_masked_scatter_signed_scaled,  "SST1D">;
  defm : sve_masked_scatter_x2_scaled<nxv2f16,  nontrunc_masked_scatter_signed_scaled,  "SST1H_D">;
  defm : sve_masked_scatter_x2_scaled<nxv2f32,  nontrunc_masked_scatter_signed_scaled,  "SST1W_D">;
  defm : sve_masked_scatter_x2_scaled<nxv2f64,  nontrunc_masked_scatter_signed_scaled,  "SST1D">;
  defm : sve_masked_scatter_x2_scaled<nxv2bf16, nontrunc_masked_scatter_signed_scaled,  "SST1H_D">;

  defm : sve_masked_scatter_x2_unscaled<nxv2i64,  trunc_masked_scatter_i8_signed_unscaled,  "SST1B_D" , imm0_31>;
  defm : sve_masked_scatter_x2_unscaled<nxv2i64,  trunc_masked_scatter_i16_signed_unscaled, "SST1H_D",  uimm5s2>;
  defm : sve_masked_scatter_x2_unscaled<nxv2i64,  trunc_masked_scatter_i32_signed_unscaled, "SST1W_D",  uimm5s4>;
  defm : sve_masked_scatter_x2_unscaled<nxv2i64,  nontrunc_masked_scatter_signed_unscaled,  "SST1D",    uimm5s8>;
  defm : sve_masked_scatter_x2_unscaled<nxv2f16,  nontrunc_masked_scatter_signed_unscaled,  "SST1H_D",  uimm5s2>;
  defm : sve_masked_scatter_x2_unscaled<nxv2f32,  nontrunc_masked_scatter_signed_unscaled,  "SST1W_D",  uimm5s4>;
  defm : sve_masked_scatter_x2_unscaled<nxv2f64,  nontrunc_masked_scatter_signed_unscaled,  "SST1D",    uimm5s8>;
  defm : sve_masked_scatter_x2_unscaled<nxv2bf16, nontrunc_masked_scatter_signed_unscaled,  "SST1H_D",  uimm5s2>;

  defm : sve_masked_scatter_x4<nxv4i32,  trunc_masked_scatter_i16_signed_scaled, SST1H_S_SXTW_SCALED>;
  defm : sve_masked_scatter_x4<nxv4i32,  nontrunc_masked_scatter_signed_scaled,  SST1W_SXTW_SCALED>;
  defm : sve_masked_scatter_x4<nxv4f16,  nontrunc_masked_scatter_signed_scaled,  SST1H_S_SXTW_SCALED>;
  defm : sve_masked_scatter_x4<nxv4f32,  nontrunc_masked_scatter_signed_scaled,  SST1W_SXTW_SCALED>;
  defm : sve_masked_scatter_x4<nxv4bf16, nontrunc_masked_scatter_signed_scaled,  SST1H_S_SXTW_SCALED>;

  defm : sve_masked_scatter_x4<nxv4i32,  trunc_masked_scatter_i8_signed_unscaled,  SST1B_S_SXTW>;
  defm : sve_masked_scatter_x4<nxv4i32,  trunc_masked_scatter_i16_signed_unscaled, SST1H_S_SXTW>;
  defm : sve_masked_scatter_x4<nxv4i32,  nontrunc_masked_scatter_signed_unscaled,  SST1W_SXTW>;
  defm : sve_masked_scatter_x4<nxv4f16,  nontrunc_masked_scatter_signed_unscaled,  SST1H_S_SXTW>;
  defm : sve_masked_scatter_x4<nxv4f32,  nontrunc_masked_scatter_signed_unscaled,  SST1W_SXTW>;
  defm : sve_masked_scatter_x4<nxv4bf16, nontrunc_masked_scatter_signed_unscaled,  SST1H_S_SXTW>;

  defm : sve_masked_scatter_x4<nxv4i32,  trunc_masked_scatter_i16_unsigned_scaled, SST1H_S_UXTW_SCALED>;
  defm : sve_masked_scatter_x4<nxv4i32,  nontrunc_masked_scatter_unsigned_scaled,  SST1W_UXTW_SCALED>;
  defm : sve_masked_scatter_x4<nxv4f16,  nontrunc_masked_scatter_unsigned_scaled,  SST1H_S_UXTW_SCALED>;
  defm : sve_masked_scatter_x4<nxv4f32,  nontrunc_masked_scatter_unsigned_scaled,  SST1W_UXTW_SCALED>;
  defm : sve_masked_scatter_x4<nxv4bf16, nontrunc_masked_scatter_unsigned_scaled,  SST1H_S_UXTW_SCALED>;

  defm : sve_masked_scatter_x4<nxv4i32,  trunc_masked_scatter_i8_unsigned_unscaled,  SST1B_S_UXTW>;
  defm : sve_masked_scatter_x4<nxv4i32,  trunc_masked_scatter_i16_unsigned_unscaled, SST1H_S_UXTW>;
  defm : sve_masked_scatter_x4<nxv4i32,  nontrunc_masked_scatter_unsigned_unscaled,  SST1W_UXTW>;
  defm : sve_masked_scatter_x4<nxv4f16,  nontrunc_masked_scatter_unsigned_unscaled,  SST1H_S_UXTW>;
  defm : sve_masked_scatter_x4<nxv4f32,  nontrunc_masked_scatter_unsigned_unscaled,  SST1W_UXTW>;
  defm : sve_masked_scatter_x4<nxv4bf16, nontrunc_masked_scatter_unsigned_unscaled,  SST1H_S_UXTW>;
} // End HasSVE

let Predicates = [HasSVEorSME] in {
  // ST(2|3|4) structured stores (register + immediate)
  defm ST2B_IMM : sve_mem_est_si<0b00, 0b01, ZZ_b,   "st2b", simm4s2>;
  defm ST3B_IMM : sve_mem_est_si<0b00, 0b10, ZZZ_b,  "st3b", simm4s3>;
  defm ST4B_IMM : sve_mem_est_si<0b00, 0b11, ZZZZ_b, "st4b", simm4s4>;
  defm ST2H_IMM : sve_mem_est_si<0b01, 0b01, ZZ_h,   "st2h", simm4s2>;
  defm ST3H_IMM : sve_mem_est_si<0b01, 0b10, ZZZ_h,  "st3h", simm4s3>;
  defm ST4H_IMM : sve_mem_est_si<0b01, 0b11, ZZZZ_h, "st4h", simm4s4>;
  defm ST2W_IMM : sve_mem_est_si<0b10, 0b01, ZZ_s,   "st2w", simm4s2>;
  defm ST3W_IMM : sve_mem_est_si<0b10, 0b10, ZZZ_s,  "st3w", simm4s3>;
  defm ST4W_IMM : sve_mem_est_si<0b10, 0b11, ZZZZ_s, "st4w", simm4s4>;
  defm ST2D_IMM : sve_mem_est_si<0b11, 0b01, ZZ_d,   "st2d", simm4s2>;
  defm ST3D_IMM : sve_mem_est_si<0b11, 0b10, ZZZ_d,  "st3d", simm4s3>;
  defm ST4D_IMM : sve_mem_est_si<0b11, 0b11, ZZZZ_d, "st4d", simm4s4>;
  let Predicates = [HasSVE2p1_or_HasSME2p1] in {
  defm ST2Q_IMM : sve_mem_128b_est_si<0b01, ZZ_q,    "st2q", simm4s2>;
  defm ST3Q_IMM : sve_mem_128b_est_si<0b10, ZZZ_q,   "st3q", simm4s3>;
  defm ST4Q_IMM : sve_mem_128b_est_si<0b11, ZZZZ_q,  "st4q", simm4s4>;
  }

  // ST(2|3|4) structured stores (register + register)
  def ST2B : sve_mem_est_ss<0b00, 0b01, ZZ_b,   "st2b", GPR64NoXZRshifted8>;
  def ST3B : sve_mem_est_ss<0b00, 0b10, ZZZ_b,  "st3b", GPR64NoXZRshifted8>;
  def ST4B : sve_mem_est_ss<0b00, 0b11, ZZZZ_b, "st4b", GPR64NoXZRshifted8>;
  def ST2H : sve_mem_est_ss<0b01, 0b01, ZZ_h,   "st2h", GPR64NoXZRshifted16>;
  def ST3H : sve_mem_est_ss<0b01, 0b10, ZZZ_h,  "st3h", GPR64NoXZRshifted16>;
  def ST4H : sve_mem_est_ss<0b01, 0b11, ZZZZ_h, "st4h", GPR64NoXZRshifted16>;
  def ST2W : sve_mem_est_ss<0b10, 0b01, ZZ_s,   "st2w", GPR64NoXZRshifted32>;
  def ST3W : sve_mem_est_ss<0b10, 0b10, ZZZ_s,  "st3w", GPR64NoXZRshifted32>;
  def ST4W : sve_mem_est_ss<0b10, 0b11, ZZZZ_s, "st4w", GPR64NoXZRshifted32>;
  def ST2D : sve_mem_est_ss<0b11, 0b01, ZZ_d,   "st2d", GPR64NoXZRshifted64>;
  def ST3D : sve_mem_est_ss<0b11, 0b10, ZZZ_d,  "st3d", GPR64NoXZRshifted64>;
  def ST4D : sve_mem_est_ss<0b11, 0b11, ZZZZ_d, "st4d", GPR64NoXZRshifted64>;
  let Predicates = [HasSVE2p1_or_HasSME2p1] in {
  def ST2Q : sve_mem_128b_est_ss<0b01, ZZ_q,    "st2q", GPR64NoXZRshifted128>;
  def ST3Q : sve_mem_128b_est_ss<0b10, ZZZ_q,   "st3q", GPR64NoXZRshifted128>;
  def ST4Q : sve_mem_128b_est_ss<0b11, ZZZZ_q,  "st4q", GPR64NoXZRshifted128>;
  }
  // Non-temporal contiguous stores (register + immediate)
  defm STNT1B_ZRI : sve_mem_cstnt_si<0b00, "stnt1b", Z_b, ZPR8>;
  defm STNT1H_ZRI : sve_mem_cstnt_si<0b01, "stnt1h", Z_h, ZPR16>;
  defm STNT1W_ZRI : sve_mem_cstnt_si<0b10, "stnt1w", Z_s, ZPR32>;
  defm STNT1D_ZRI : sve_mem_cstnt_si<0b11, "stnt1d", Z_d, ZPR64>;

  // Non-temporal contiguous stores (register + register)
  defm STNT1B_ZRR : sve_mem_cstnt_ss<0b00, "stnt1b", Z_b, ZPR8, GPR64NoXZRshifted8>;
  defm STNT1H_ZRR : sve_mem_cstnt_ss<0b01, "stnt1h", Z_h, ZPR16, GPR64NoXZRshifted16>;
  defm STNT1W_ZRR : sve_mem_cstnt_ss<0b10, "stnt1w", Z_s, ZPR32, GPR64NoXZRshifted32>;
  defm STNT1D_ZRR : sve_mem_cstnt_ss<0b11, "stnt1d", Z_d, ZPR64, GPR64NoXZRshifted64>;

  // Fill/Spill
  defm LDR_ZXI : sve_mem_z_fill<"ldr">;
  defm LDR_PXI : sve_mem_p_fill<"ldr">;
  defm STR_ZXI : sve_mem_z_spill<"str">;
  defm STR_PXI : sve_mem_p_spill<"str">;

  // Contiguous prefetch (register + immediate)
  defm PRFB_PRI : sve_mem_prfm_si<0b00, "prfb">;
  defm PRFH_PRI : sve_mem_prfm_si<0b01, "prfh">;
  defm PRFW_PRI : sve_mem_prfm_si<0b10, "prfw">;
  defm PRFD_PRI : sve_mem_prfm_si<0b11, "prfd">;

  // Contiguous prefetch (register + register)
  def PRFB_PRR : sve_mem_prfm_ss<0b001, "prfb", GPR64NoXZRshifted8>;
  def PRFH_PRR : sve_mem_prfm_ss<0b011, "prfh", GPR64NoXZRshifted16>;
  def PRFW_PRR : sve_mem_prfm_ss<0b101, "prfw", GPR64NoXZRshifted32>;
  def PRFD_PRR : sve_mem_prfm_ss<0b111, "prfd", GPR64NoXZRshifted64>;

  multiclass sve_prefetch<SDPatternOperator prefetch, ValueType PredTy, Instruction RegImmInst, Instruction RegRegInst, ComplexPattern AddrCP> {
    // reg + imm
    let AddedComplexity = 2 in {
      def _reg_imm : Pat<(prefetch (PredTy PPR_3b:$gp), (am_sve_indexed_s6 GPR64sp:$base, simm6s1:$offset), (i32 sve_prfop:$prfop)),
                         (RegImmInst sve_prfop:$prfop, PPR_3b:$gp, GPR64:$base, simm6s1:$offset)>;
    }

    // reg + reg
    let AddedComplexity = 1 in {
      def _reg_reg : Pat<(prefetch (PredTy PPR_3b:$gp), (AddrCP GPR64sp:$base, GPR64:$index), (i32 sve_prfop:$prfop)),
                         (RegRegInst sve_prfop:$prfop, PPR_3b:$gp, GPR64:$base, GPR64:$index)>;
    }

    // default fallback
    def _default : Pat<(prefetch  (PredTy PPR_3b:$gp), GPR64:$base, (i32 sve_prfop:$prfop)),
                       (RegImmInst sve_prfop:$prfop, PPR_3b:$gp, GPR64:$base, (i64 0))>;
  }

  defm : sve_prefetch<int_aarch64_sve_prf, nxv16i1, PRFB_PRI, PRFB_PRR, am_sve_regreg_lsl0>;
  defm : sve_prefetch<int_aarch64_sve_prf, nxv8i1,  PRFH_PRI, PRFH_PRR, am_sve_regreg_lsl1>;
  defm : sve_prefetch<int_aarch64_sve_prf, nxv4i1,  PRFW_PRI, PRFW_PRR, am_sve_regreg_lsl2>;
  defm : sve_prefetch<int_aarch64_sve_prf, nxv2i1,  PRFD_PRI, PRFD_PRR, am_sve_regreg_lsl3>;
} // End HasSVEorSME

let Predicates = [HasSVE] in {
  // Gather prefetch using scaled 32-bit offsets, e.g.
  //    prfh pldl1keep, p0, [x0, z0.s, uxtw #1]
  defm PRFB_S : sve_mem_32b_prfm_sv_scaled<0b00, "prfb", ZPR32ExtSXTW8Only,  ZPR32ExtUXTW8Only, int_aarch64_sve_prfb_gather_sxtw_index, int_aarch64_sve_prfb_gather_uxtw_index>;
  defm PRFH_S : sve_mem_32b_prfm_sv_scaled<0b01, "prfh", ZPR32ExtSXTW16,     ZPR32ExtUXTW16,    int_aarch64_sve_prfh_gather_sxtw_index, int_aarch64_sve_prfh_gather_uxtw_index>;
  defm PRFW_S : sve_mem_32b_prfm_sv_scaled<0b10, "prfw", ZPR32ExtSXTW32,     ZPR32ExtUXTW32,    int_aarch64_sve_prfw_gather_sxtw_index, int_aarch64_sve_prfw_gather_uxtw_index>;
  defm PRFD_S : sve_mem_32b_prfm_sv_scaled<0b11, "prfd", ZPR32ExtSXTW64,     ZPR32ExtUXTW64,    int_aarch64_sve_prfd_gather_sxtw_index, int_aarch64_sve_prfd_gather_uxtw_index>;

  // Gather prefetch using unpacked, scaled 32-bit offsets, e.g.
  //    prfh pldl1keep, p0, [x0, z0.d, uxtw #1]
  defm PRFB_D : sve_mem_64b_prfm_sv_ext_scaled<0b00, "prfb", ZPR64ExtSXTW8Only, ZPR64ExtUXTW8Only, int_aarch64_sve_prfb_gather_sxtw_index, int_aarch64_sve_prfb_gather_uxtw_index>;
  defm PRFH_D : sve_mem_64b_prfm_sv_ext_scaled<0b01, "prfh", ZPR64ExtSXTW16,    ZPR64ExtUXTW16,    int_aarch64_sve_prfh_gather_sxtw_index, int_aarch64_sve_prfh_gather_uxtw_index>;
  defm PRFW_D : sve_mem_64b_prfm_sv_ext_scaled<0b10, "prfw", ZPR64ExtSXTW32,    ZPR64ExtUXTW32,    int_aarch64_sve_prfw_gather_sxtw_index, int_aarch64_sve_prfw_gather_uxtw_index>;
  defm PRFD_D : sve_mem_64b_prfm_sv_ext_scaled<0b11, "prfd", ZPR64ExtSXTW64,    ZPR64ExtUXTW64,    int_aarch64_sve_prfd_gather_sxtw_index, int_aarch64_sve_prfd_gather_uxtw_index>;

  // Gather prefetch using scaled 64-bit offsets, e.g.
  //    prfh pldl1keep, p0, [x0, z0.d, lsl #1]
  defm PRFB_D_SCALED : sve_mem_64b_prfm_sv_lsl_scaled<0b00, "prfb", ZPR64ExtLSL8,  int_aarch64_sve_prfb_gather_index>;
  defm PRFH_D_SCALED : sve_mem_64b_prfm_sv_lsl_scaled<0b01, "prfh", ZPR64ExtLSL16, int_aarch64_sve_prfh_gather_index>;
  defm PRFW_D_SCALED : sve_mem_64b_prfm_sv_lsl_scaled<0b10, "prfw", ZPR64ExtLSL32, int_aarch64_sve_prfw_gather_index>;
  defm PRFD_D_SCALED : sve_mem_64b_prfm_sv_lsl_scaled<0b11, "prfd", ZPR64ExtLSL64, int_aarch64_sve_prfd_gather_index>;

  // Gather prefetch using 32/64-bit pointers with offset, e.g.
  //    prfh pldl1keep, p0, [z0.s, #16]
  //    prfh pldl1keep, p0, [z0.d, #16]
  defm PRFB_S_PZI : sve_mem_32b_prfm_vi<0b00, "prfb", imm0_31, int_aarch64_sve_prfb_gather_scalar_offset>;
  defm PRFH_S_PZI : sve_mem_32b_prfm_vi<0b01, "prfh", uimm5s2, int_aarch64_sve_prfh_gather_scalar_offset>;
  defm PRFW_S_PZI : sve_mem_32b_prfm_vi<0b10, "prfw", uimm5s4, int_aarch64_sve_prfw_gather_scalar_offset>;
  defm PRFD_S_PZI : sve_mem_32b_prfm_vi<0b11, "prfd", uimm5s8, int_aarch64_sve_prfd_gather_scalar_offset>;

  defm PRFB_D_PZI : sve_mem_64b_prfm_vi<0b00, "prfb", imm0_31, int_aarch64_sve_prfb_gather_scalar_offset>;
  defm PRFH_D_PZI : sve_mem_64b_prfm_vi<0b01, "prfh", uimm5s2, int_aarch64_sve_prfh_gather_scalar_offset>;
  defm PRFW_D_PZI : sve_mem_64b_prfm_vi<0b10, "prfw", uimm5s4, int_aarch64_sve_prfw_gather_scalar_offset>;
  defm PRFD_D_PZI : sve_mem_64b_prfm_vi<0b11, "prfd", uimm5s8, int_aarch64_sve_prfd_gather_scalar_offset>;

  defm ADR_SXTW_ZZZ_D : sve_int_bin_cons_misc_0_a_sxtw<0b00, "adr">;
  defm ADR_UXTW_ZZZ_D : sve_int_bin_cons_misc_0_a_uxtw<0b01, "adr">;
  defm ADR_LSL_ZZZ_S  : sve_int_bin_cons_misc_0_a_32_lsl<0b10, "adr">;
  defm ADR_LSL_ZZZ_D  : sve_int_bin_cons_misc_0_a_64_lsl<0b11, "adr">;

  def : Pat<(nxv4i32 (int_aarch64_sve_adrb nxv4i32:$Op1, nxv4i32:$Op2)),
            (ADR_LSL_ZZZ_S_0 $Op1, $Op2)>;
  def : Pat<(nxv4i32 (int_aarch64_sve_adrh nxv4i32:$Op1, nxv4i32:$Op2)),
            (ADR_LSL_ZZZ_S_1 $Op1, $Op2)>;
  def : Pat<(nxv4i32 (int_aarch64_sve_adrw nxv4i32:$Op1, nxv4i32:$Op2)),
            (ADR_LSL_ZZZ_S_2 $Op1, $Op2)>;
  def : Pat<(nxv4i32 (int_aarch64_sve_adrd nxv4i32:$Op1, nxv4i32:$Op2)),
            (ADR_LSL_ZZZ_S_3 $Op1, $Op2)>;

  def : Pat<(nxv2i64 (int_aarch64_sve_adrb nxv2i64:$Op1, nxv2i64:$Op2)),
            (ADR_LSL_ZZZ_D_0 $Op1, $Op2)>;
  def : Pat<(nxv2i64 (int_aarch64_sve_adrh nxv2i64:$Op1, nxv2i64:$Op2)),
            (ADR_LSL_ZZZ_D_1 $Op1, $Op2)>;
  def : Pat<(nxv2i64 (int_aarch64_sve_adrw nxv2i64:$Op1, nxv2i64:$Op2)),
            (ADR_LSL_ZZZ_D_2 $Op1, $Op2)>;
  def : Pat<(nxv2i64 (int_aarch64_sve_adrd nxv2i64:$Op1, nxv2i64:$Op2)),
            (ADR_LSL_ZZZ_D_3 $Op1, $Op2)>;

  // Patterns to generate adr instruction.
  // adr z0.d, [z0.d, z0.d, uxtw]
  def : Pat<(add nxv2i64:$Op1,
                (nxv2i64 (and nxv2i64:$Op2, (nxv2i64 (splat_vector (i64 0xFFFFFFFF)))))),
            (ADR_UXTW_ZZZ_D_0 $Op1, $Op2)>;
  // adr z0.d, [z0.d, z0.d, sxtw]
  def : Pat<(add nxv2i64:$Op1,
                (nxv2i64 (sext_inreg nxv2i64:$Op2, nxv2i32))),
            (ADR_SXTW_ZZZ_D_0 $Op1, $Op2)>;

  // adr z0.s, [z0.s, z0.s, lsl #<shift>]
  // adr z0.d, [z0.d, z0.d, lsl #<shift>]
  multiclass adrShiftPat<ValueType Ty, ValueType PredTy, ValueType ShiftTy, Instruction DestAdrIns, int ShiftAmt> {
    def : Pat<(add Ty:$Op1,
                  (Ty (AArch64lsl_p (PredTy (SVEAllActive)),
                                    Ty:$Op2,
                                    (Ty (splat_vector (ShiftTy ShiftAmt)))))),
              (DestAdrIns $Op1, $Op2)>;
  }
  defm : adrShiftPat<nxv2i64, nxv2i1, i64, ADR_LSL_ZZZ_D_1, 1>;
  defm : adrShiftPat<nxv2i64, nxv2i1, i64, ADR_LSL_ZZZ_D_2, 2>;
  defm : adrShiftPat<nxv2i64, nxv2i1, i64, ADR_LSL_ZZZ_D_3, 3>;
  defm : adrShiftPat<nxv4i32, nxv4i1, i32, ADR_LSL_ZZZ_S_1, 1>;
  defm : adrShiftPat<nxv4i32, nxv4i1, i32, ADR_LSL_ZZZ_S_2, 2>;
  defm : adrShiftPat<nxv4i32, nxv4i1, i32, ADR_LSL_ZZZ_S_3, 3>;

  // adr z0.d, [z0.d, z0.d, uxtw #<shift>]
  // adr z0.d, [z0.d, z0.d, sxtw #<shift>]
  multiclass adrXtwShiftPat<ValueType Ty, ValueType PredTy, int ShiftAmt> {
    def : Pat<(add Ty:$Op1,
                  (Ty (AArch64lsl_p (PredTy (SVEAllActive)),
                                    (Ty (and Ty:$Op2, (Ty (splat_vector (i64 0xFFFFFFFF))))),
                                    (Ty (splat_vector (i64 ShiftAmt)))))),
              (!cast<Instruction>("ADR_UXTW_ZZZ_D_"#ShiftAmt) $Op1, $Op2)>;

    def : Pat<(add Ty:$Op1,
                  (Ty (AArch64lsl_p (PredTy (SVEAllActive)),
                                    (Ty (sext_inreg Ty:$Op2, nxv2i32)),
                                    (Ty (splat_vector (i64 ShiftAmt)))))),
              (!cast<Instruction>("ADR_SXTW_ZZZ_D_"#ShiftAmt) $Op1, $Op2)>;
  }
  defm : adrXtwShiftPat<nxv2i64, nxv2i1, 1>;
  defm : adrXtwShiftPat<nxv2i64, nxv2i1, 2>;
  defm : adrXtwShiftPat<nxv2i64, nxv2i1, 3>;
} // End HasSVE

let Predicates = [HasSVEorSME] in {
  defm TBL_ZZZ  : sve_int_perm_tbl<"tbl", AArch64tbl>;

  defm ZIP1_ZZZ : sve_int_perm_bin_perm_zz<0b000, "zip1", AArch64zip1>;
  defm ZIP2_ZZZ : sve_int_perm_bin_perm_zz<0b001, "zip2", AArch64zip2>;
  defm UZP1_ZZZ : sve_int_perm_bin_perm_zz<0b010, "uzp1", AArch64uzp1>;
  defm UZP2_ZZZ : sve_int_perm_bin_perm_zz<0b011, "uzp2", AArch64uzp2>;
  defm TRN1_ZZZ : sve_int_perm_bin_perm_zz<0b100, "trn1", AArch64trn1>;
  defm TRN2_ZZZ : sve_int_perm_bin_perm_zz<0b101, "trn2", AArch64trn2>;

  defm ZIP1_PPP : sve_int_perm_bin_perm_pp<0b000, "zip1", AArch64zip1, int_aarch64_sve_zip1_b16, int_aarch64_sve_zip1_b32, int_aarch64_sve_zip1_b64>;
  defm ZIP2_PPP : sve_int_perm_bin_perm_pp<0b001, "zip2", AArch64zip2, int_aarch64_sve_zip2_b16, int_aarch64_sve_zip2_b32, int_aarch64_sve_zip2_b64>;
  defm UZP1_PPP : sve_int_perm_bin_perm_pp<0b010, "uzp1", AArch64uzp1, int_aarch64_sve_uzp1_b16, int_aarch64_sve_uzp1_b32, int_aarch64_sve_uzp1_b64>;
  defm UZP2_PPP : sve_int_perm_bin_perm_pp<0b011, "uzp2", AArch64uzp2, int_aarch64_sve_uzp2_b16, int_aarch64_sve_uzp2_b32, int_aarch64_sve_uzp2_b64>;
  defm TRN1_PPP : sve_int_perm_bin_perm_pp<0b100, "trn1", AArch64trn1, int_aarch64_sve_trn1_b16, int_aarch64_sve_trn1_b32, int_aarch64_sve_trn1_b64>;
  defm TRN2_PPP : sve_int_perm_bin_perm_pp<0b101, "trn2", AArch64trn2, int_aarch64_sve_trn2_b16, int_aarch64_sve_trn2_b32, int_aarch64_sve_trn2_b64>;

  // Extract lo/hi halves of legal predicate types.
  def : Pat<(nxv1i1 (extract_subvector nxv2i1:$Ps, (i64 0))),
            (PUNPKLO_PP PPR:$Ps)>;
  def : Pat<(nxv1i1 (extract_subvector nxv2i1:$Ps, (i64 1))),
            (PUNPKHI_PP PPR:$Ps)>;
  def : Pat<(nxv2i1 (extract_subvector nxv4i1:$Ps, (i64 0))),
            (PUNPKLO_PP PPR:$Ps)>;
  def : Pat<(nxv2i1 (extract_subvector nxv4i1:$Ps, (i64 2))),
            (PUNPKHI_PP PPR:$Ps)>;
  def : Pat<(nxv4i1 (extract_subvector nxv8i1:$Ps, (i64 0))),
            (PUNPKLO_PP PPR:$Ps)>;
  def : Pat<(nxv4i1 (extract_subvector nxv8i1:$Ps, (i64 4))),
            (PUNPKHI_PP PPR:$Ps)>;
  def : Pat<(nxv8i1 (extract_subvector nxv16i1:$Ps, (i64 0))),
            (PUNPKLO_PP PPR:$Ps)>;
  def : Pat<(nxv8i1 (extract_subvector nxv16i1:$Ps, (i64 8))),
            (PUNPKHI_PP PPR:$Ps)>;

  def : Pat<(nxv1i1 (extract_subvector nxv4i1:$Ps, (i64 0))),
            (PUNPKLO_PP (PUNPKLO_PP PPR:$Ps))>;
  def : Pat<(nxv1i1 (extract_subvector nxv4i1:$Ps, (i64 1))),
            (PUNPKHI_PP (PUNPKLO_PP PPR:$Ps))>;
  def : Pat<(nxv1i1 (extract_subvector nxv4i1:$Ps, (i64 2))),
            (PUNPKLO_PP (PUNPKHI_PP PPR:$Ps))>;
  def : Pat<(nxv1i1 (extract_subvector nxv4i1:$Ps, (i64 3))),
            (PUNPKHI_PP (PUNPKHI_PP PPR:$Ps))>;
  def : Pat<(nxv2i1 (extract_subvector nxv8i1:$Ps, (i64 0))),
            (PUNPKLO_PP (PUNPKLO_PP PPR:$Ps))>;
  def : Pat<(nxv2i1 (extract_subvector nxv8i1:$Ps, (i64 2))),
            (PUNPKHI_PP (PUNPKLO_PP PPR:$Ps))>;
  def : Pat<(nxv2i1 (extract_subvector nxv8i1:$Ps, (i64 4))),
            (PUNPKLO_PP (PUNPKHI_PP PPR:$Ps))>;
  def : Pat<(nxv2i1 (extract_subvector nxv8i1:$Ps, (i64 6))),
            (PUNPKHI_PP (PUNPKHI_PP PPR:$Ps))>;
  def : Pat<(nxv4i1 (extract_subvector nxv16i1:$Ps, (i64 0))),
            (PUNPKLO_PP (PUNPKLO_PP PPR:$Ps))>;
  def : Pat<(nxv4i1 (extract_subvector nxv16i1:$Ps, (i64 4))),
            (PUNPKHI_PP (PUNPKLO_PP PPR:$Ps))>;
  def : Pat<(nxv4i1 (extract_subvector nxv16i1:$Ps, (i64 8))),
            (PUNPKLO_PP (PUNPKHI_PP PPR:$Ps))>;
  def : Pat<(nxv4i1 (extract_subvector nxv16i1:$Ps, (i64 12))),
            (PUNPKHI_PP (PUNPKHI_PP PPR:$Ps))>;


  def : Pat<(nxv1i1 (extract_subvector nxv8i1:$Ps, (i64 0))),
            (PUNPKLO_PP (PUNPKLO_PP (PUNPKLO_PP PPR:$Ps)))>;
  def : Pat<(nxv1i1 (extract_subvector nxv8i1:$Ps, (i64 1))),
            (PUNPKHI_PP (PUNPKLO_PP (PUNPKLO_PP PPR:$Ps)))>;
  def : Pat<(nxv1i1 (extract_subvector nxv8i1:$Ps, (i64 2))),
            (PUNPKLO_PP (PUNPKHI_PP (PUNPKLO_PP PPR:$Ps)))>;
  def : Pat<(nxv1i1 (extract_subvector nxv8i1:$Ps, (i64 3))),
            (PUNPKHI_PP (PUNPKHI_PP (PUNPKLO_PP PPR:$Ps)))>;
  def : Pat<(nxv1i1 (extract_subvector nxv8i1:$Ps, (i64 4))),
            (PUNPKLO_PP (PUNPKLO_PP (PUNPKHI_PP PPR:$Ps)))>;
  def : Pat<(nxv1i1 (extract_subvector nxv8i1:$Ps, (i64 5))),
            (PUNPKHI_PP (PUNPKLO_PP (PUNPKHI_PP PPR:$Ps)))>;
  def : Pat<(nxv1i1 (extract_subvector nxv8i1:$Ps, (i64 6))),
            (PUNPKLO_PP (PUNPKHI_PP (PUNPKHI_PP PPR:$Ps)))>;
  def : Pat<(nxv1i1 (extract_subvector nxv8i1:$Ps, (i64 7))),
            (PUNPKHI_PP (PUNPKHI_PP (PUNPKHI_PP PPR:$Ps)))>;
  def : Pat<(nxv2i1 (extract_subvector nxv16i1:$Ps, (i64 0))),
            (PUNPKLO_PP (PUNPKLO_PP (PUNPKLO_PP PPR:$Ps)))>;
  def : Pat<(nxv2i1 (extract_subvector nxv16i1:$Ps, (i64 2))),
            (PUNPKHI_PP (PUNPKLO_PP (PUNPKLO_PP PPR:$Ps)))>;
  def : Pat<(nxv2i1 (extract_subvector nxv16i1:$Ps, (i64 4))),
            (PUNPKLO_PP (PUNPKHI_PP (PUNPKLO_PP PPR:$Ps)))>;
  def : Pat<(nxv2i1 (extract_subvector nxv16i1:$Ps, (i64 6))),
            (PUNPKHI_PP (PUNPKHI_PP (PUNPKLO_PP PPR:$Ps)))>;
  def : Pat<(nxv2i1 (extract_subvector nxv16i1:$Ps, (i64 8))),
            (PUNPKLO_PP (PUNPKLO_PP (PUNPKHI_PP PPR:$Ps)))>;
  def : Pat<(nxv2i1 (extract_subvector nxv16i1:$Ps, (i64 10))),
            (PUNPKHI_PP (PUNPKLO_PP (PUNPKHI_PP PPR:$Ps)))>;
  def : Pat<(nxv2i1 (extract_subvector nxv16i1:$Ps, (i64 12))),
            (PUNPKLO_PP (PUNPKHI_PP (PUNPKHI_PP PPR:$Ps)))>;
  def : Pat<(nxv2i1 (extract_subvector nxv16i1:$Ps, (i64 14))),
            (PUNPKHI_PP (PUNPKHI_PP (PUNPKHI_PP PPR:$Ps)))>;

  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 0))),
            (PUNPKLO_PP (PUNPKLO_PP (PUNPKLO_PP  (PUNPKLO_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 1))),
            (PUNPKHI_PP (PUNPKLO_PP (PUNPKLO_PP  (PUNPKLO_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 2))),
            (PUNPKLO_PP (PUNPKHI_PP (PUNPKLO_PP  (PUNPKLO_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 3))),
            (PUNPKHI_PP (PUNPKHI_PP (PUNPKLO_PP  (PUNPKLO_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 4))),
            (PUNPKLO_PP (PUNPKLO_PP (PUNPKHI_PP  (PUNPKLO_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 5))),
            (PUNPKHI_PP (PUNPKLO_PP (PUNPKHI_PP  (PUNPKLO_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 6))),
            (PUNPKLO_PP (PUNPKHI_PP (PUNPKHI_PP  (PUNPKLO_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 7))),
            (PUNPKHI_PP (PUNPKHI_PP (PUNPKHI_PP  (PUNPKLO_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 8))),
            (PUNPKLO_PP (PUNPKLO_PP (PUNPKLO_PP  (PUNPKHI_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 9))),
            (PUNPKHI_PP (PUNPKLO_PP (PUNPKLO_PP  (PUNPKHI_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 10))),
            (PUNPKLO_PP (PUNPKHI_PP (PUNPKLO_PP  (PUNPKHI_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 11))),
            (PUNPKHI_PP (PUNPKHI_PP (PUNPKLO_PP  (PUNPKHI_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 12))),
            (PUNPKLO_PP (PUNPKLO_PP (PUNPKHI_PP  (PUNPKHI_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 13))),
            (PUNPKHI_PP (PUNPKLO_PP (PUNPKHI_PP  (PUNPKHI_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 14))),
            (PUNPKLO_PP (PUNPKHI_PP (PUNPKHI_PP  (PUNPKHI_PP PPR:$Ps))))>;
  def : Pat<(nxv1i1 (extract_subvector nxv16i1:$Ps, (i64 15))),
            (PUNPKHI_PP (PUNPKHI_PP (PUNPKHI_PP  (PUNPKHI_PP PPR:$Ps))))>;

  // Extract subvectors from FP SVE vectors
  def : Pat<(nxv2f16 (extract_subvector nxv4f16:$Zs, (i64 0))),
            (UUNPKLO_ZZ_D ZPR:$Zs)>;
  def : Pat<(nxv2f16 (extract_subvector nxv4f16:$Zs, (i64 2))),
            (UUNPKHI_ZZ_D ZPR:$Zs)>;
  def : Pat<(nxv4f16 (extract_subvector nxv8f16:$Zs, (i64 0))),
            (UUNPKLO_ZZ_S ZPR:$Zs)>;
  def : Pat<(nxv4f16 (extract_subvector nxv8f16:$Zs, (i64 4))),
            (UUNPKHI_ZZ_S ZPR:$Zs)>;
  def : Pat<(nxv2f32 (extract_subvector nxv4f32:$Zs, (i64 0))),
            (UUNPKLO_ZZ_D ZPR:$Zs)>;
  def : Pat<(nxv2f32 (extract_subvector nxv4f32:$Zs, (i64 2))),
            (UUNPKHI_ZZ_D ZPR:$Zs)>;

  def : Pat<(nxv2bf16 (extract_subvector nxv4bf16:$Zs, (i64 0))),
            (UUNPKLO_ZZ_D ZPR:$Zs)>;
  def : Pat<(nxv2bf16 (extract_subvector nxv4bf16:$Zs, (i64 2))),
            (UUNPKHI_ZZ_D ZPR:$Zs)>;
  def : Pat<(nxv4bf16 (extract_subvector nxv8bf16:$Zs, (i64 0))),
            (UUNPKLO_ZZ_S ZPR:$Zs)>;
  def : Pat<(nxv4bf16 (extract_subvector nxv8bf16:$Zs, (i64 4))),
            (UUNPKHI_ZZ_S ZPR:$Zs)>;

  def : Pat<(nxv2f16 (extract_subvector nxv8f16:$Zs, (i64 0))),
            (UUNPKLO_ZZ_D (UUNPKLO_ZZ_S ZPR:$Zs))>;
  def : Pat<(nxv2f16 (extract_subvector nxv8f16:$Zs, (i64 2))),
            (UUNPKHI_ZZ_D (UUNPKLO_ZZ_S ZPR:$Zs))>;
  def : Pat<(nxv2f16 (extract_subvector nxv8f16:$Zs, (i64 4))),
            (UUNPKLO_ZZ_D (UUNPKHI_ZZ_S ZPR:$Zs))>;
  def : Pat<(nxv2f16 (extract_subvector nxv8f16:$Zs, (i64 6))),
            (UUNPKHI_ZZ_D (UUNPKHI_ZZ_S ZPR:$Zs))>;

  def : Pat<(nxv2bf16 (extract_subvector nxv8bf16:$Zs, (i64 0))),
            (UUNPKLO_ZZ_D (UUNPKLO_ZZ_S ZPR:$Zs))>;
  def : Pat<(nxv2bf16 (extract_subvector nxv8bf16:$Zs, (i64 2))),
            (UUNPKHI_ZZ_D (UUNPKLO_ZZ_S ZPR:$Zs))>;
  def : Pat<(nxv2bf16 (extract_subvector nxv8bf16:$Zs, (i64 4))),
            (UUNPKLO_ZZ_D (UUNPKHI_ZZ_S ZPR:$Zs))>;
  def : Pat<(nxv2bf16 (extract_subvector nxv8bf16:$Zs, (i64 6))),
            (UUNPKHI_ZZ_D (UUNPKHI_ZZ_S ZPR:$Zs))>;

  // extract/insert 64-bit fixed length vector from/into a scalable vector
  foreach VT = [v8i8, v4i16, v2i32, v1i64, v4f16, v2f32, v1f64, v4bf16] in {
    def : Pat<(VT (vector_extract_subvec SVEContainerVT<VT>.Value:$Zs, (i64 0))),
              (EXTRACT_SUBREG ZPR:$Zs, dsub)>;
    def : Pat<(SVEContainerVT<VT>.Value (vector_insert_subvec undef, (VT V64:$src), (i64 0))),
              (INSERT_SUBREG (IMPLICIT_DEF), $src, dsub)>;
  }

  // extract/insert 128-bit fixed length vector from/into a scalable vector
  foreach VT = [v16i8, v8i16, v4i32, v2i64, v8f16, v4f32, v2f64, v8bf16] in {
    def : Pat<(VT (vector_extract_subvec SVEContainerVT<VT>.Value:$Zs, (i64 0))),
              (EXTRACT_SUBREG ZPR:$Zs, zsub)>;
    def : Pat<(SVEContainerVT<VT>.Value (vector_insert_subvec undef, (VT V128:$src), (i64 0))),
              (INSERT_SUBREG (IMPLICIT_DEF), $src, zsub)>;
  }

  // Concatenate two predicates.
  def : Pat<(nxv2i1 (concat_vectors nxv1i1:$p1, nxv1i1:$p2)),
            (UZP1_PPP_D $p1, $p2)>;
  def : Pat<(nxv4i1 (concat_vectors nxv2i1:$p1, nxv2i1:$p2)),
            (UZP1_PPP_S $p1, $p2)>;
  def : Pat<(nxv8i1 (concat_vectors nxv4i1:$p1, nxv4i1:$p2)),
            (UZP1_PPP_H $p1, $p2)>;
  def : Pat<(nxv16i1 (concat_vectors nxv8i1:$p1, nxv8i1:$p2)),
            (UZP1_PPP_B $p1, $p2)>;

  // Concatenate two floating point vectors.
  def : Pat<(nxv4f16 (concat_vectors nxv2f16:$v1, nxv2f16:$v2)),
            (UZP1_ZZZ_S $v1, $v2)>;
  def : Pat<(nxv8f16 (concat_vectors nxv4f16:$v1, nxv4f16:$v2)),
            (UZP1_ZZZ_H $v1, $v2)>;
  def : Pat<(nxv4f32 (concat_vectors nxv2f32:$v1, nxv2f32:$v2)),
            (UZP1_ZZZ_S $v1, $v2)>;
  def : Pat<(nxv4bf16 (concat_vectors nxv2bf16:$v1, nxv2bf16:$v2)),
            (UZP1_ZZZ_S $v1, $v2)>;
  def : Pat<(nxv8bf16 (concat_vectors nxv4bf16:$v1, nxv4bf16:$v2)),
            (UZP1_ZZZ_H $v1, $v2)>;

  // Splice with lane equal to -1
  def : Pat<(nxv16i8 (vector_splice nxv16i8:$Z1, nxv16i8:$Z2, (i64 -1))),
            (INSR_ZV_B ZPR:$Z2, (INSERT_SUBREG (IMPLICIT_DEF),
            (LASTB_VPZ_B (PTRUE_B 31), ZPR:$Z1), bsub))>;
  def : Pat<(nxv8i16 (vector_splice nxv8i16:$Z1, nxv8i16:$Z2, (i64 -1))),
            (INSR_ZV_H ZPR:$Z2, (INSERT_SUBREG (IMPLICIT_DEF),
            (LASTB_VPZ_H (PTRUE_H 31), ZPR:$Z1), hsub))>;
  def : Pat<(nxv4i32 (vector_splice nxv4i32:$Z1, nxv4i32:$Z2, (i64 -1))),
            (INSR_ZV_S ZPR:$Z2, (INSERT_SUBREG (IMPLICIT_DEF),
            (LASTB_VPZ_S (PTRUE_S 31), ZPR:$Z1), ssub))>;
  def : Pat<(nxv2i64 (vector_splice nxv2i64:$Z1, nxv2i64:$Z2, (i64 -1))),
            (INSR_ZV_D ZPR:$Z2, (INSERT_SUBREG (IMPLICIT_DEF),
            (LASTB_VPZ_D (PTRUE_D 31), ZPR:$Z1), dsub))>;

  // Splice with lane bigger or equal to 0
  foreach VT = [nxv16i8] in
    def : Pat<(VT (vector_splice VT:$Z1, VT:$Z2, (i64 (sve_ext_imm_0_255 i32:$index)))),
              (EXT_ZZI  ZPR:$Z1, ZPR:$Z2, imm0_255:$index)>;

  foreach VT = [nxv8i16, nxv8f16, nxv8bf16] in
    def : Pat<(VT (vector_splice VT:$Z1, VT:$Z2, (i64 (sve_ext_imm_0_127 i32:$index)))),
              (EXT_ZZI  ZPR:$Z1, ZPR:$Z2, imm0_255:$index)>;

  foreach VT = [nxv4i32, nxv4f16, nxv4f32, nxv4bf16] in
    def : Pat<(VT (vector_splice VT:$Z1, VT:$Z2, (i64 (sve_ext_imm_0_63 i32:$index)))),
              (EXT_ZZI  ZPR:$Z1, ZPR:$Z2, imm0_255:$index)>;

  foreach VT = [nxv2i64, nxv2f16, nxv2f32, nxv2f64, nxv2bf16] in
    def : Pat<(VT (vector_splice VT:$Z1, VT:$Z2, (i64 (sve_ext_imm_0_31 i32:$index)))),
              (EXT_ZZI  ZPR:$Z1, ZPR:$Z2, imm0_255:$index)>;

  defm CMPHS_PPzZZ : sve_int_cmp_0<0b000, "cmphs", SETUGE, SETULE>;
  defm CMPHI_PPzZZ : sve_int_cmp_0<0b001, "cmphi", SETUGT, SETULT>;
  defm CMPGE_PPzZZ : sve_int_cmp_0<0b100, "cmpge", SETGE, SETLE>;
  defm CMPGT_PPzZZ : sve_int_cmp_0<0b101, "cmpgt", SETGT, SETLT>;
  defm CMPEQ_PPzZZ : sve_int_cmp_0<0b110, "cmpeq", SETEQ, SETEQ>;
  defm CMPNE_PPzZZ : sve_int_cmp_0<0b111, "cmpne", SETNE, SETNE>;

  defm CMPEQ_WIDE_PPzZZ : sve_int_cmp_0_wide<0b010, "cmpeq", int_aarch64_sve_cmpeq_wide>;
  defm CMPNE_WIDE_PPzZZ : sve_int_cmp_0_wide<0b011, "cmpne", int_aarch64_sve_cmpne_wide>;
  defm CMPGE_WIDE_PPzZZ : sve_int_cmp_1_wide<0b000, "cmpge", int_aarch64_sve_cmpge_wide>;
  defm CMPGT_WIDE_PPzZZ : sve_int_cmp_1_wide<0b001, "cmpgt", int_aarch64_sve_cmpgt_wide>;
  defm CMPLT_WIDE_PPzZZ : sve_int_cmp_1_wide<0b010, "cmplt", int_aarch64_sve_cmplt_wide>;
  defm CMPLE_WIDE_PPzZZ : sve_int_cmp_1_wide<0b011, "cmple", int_aarch64_sve_cmple_wide>;
  defm CMPHS_WIDE_PPzZZ : sve_int_cmp_1_wide<0b100, "cmphs", int_aarch64_sve_cmphs_wide>;
  defm CMPHI_WIDE_PPzZZ : sve_int_cmp_1_wide<0b101, "cmphi", int_aarch64_sve_cmphi_wide>;
  defm CMPLO_WIDE_PPzZZ : sve_int_cmp_1_wide<0b110, "cmplo", int_aarch64_sve_cmplo_wide>;
  defm CMPLS_WIDE_PPzZZ : sve_int_cmp_1_wide<0b111, "cmpls", int_aarch64_sve_cmpls_wide>;

  defm CMPGE_PPzZI : sve_int_scmp_vi<0b000, "cmpge", SETGE, SETLE>;
  defm CMPGT_PPzZI : sve_int_scmp_vi<0b001, "cmpgt", SETGT, SETLT>;
  defm CMPLT_PPzZI : sve_int_scmp_vi<0b010, "cmplt", SETLT, SETGT>;
  defm CMPLE_PPzZI : sve_int_scmp_vi<0b011, "cmple", SETLE, SETGE>;
  defm CMPEQ_PPzZI : sve_int_scmp_vi<0b100, "cmpeq", SETEQ, SETEQ>;
  defm CMPNE_PPzZI : sve_int_scmp_vi<0b101, "cmpne", SETNE, SETEQ>;
  defm CMPHS_PPzZI : sve_int_ucmp_vi<0b00, "cmphs", SETUGE, SETULE>;
  defm CMPHI_PPzZI : sve_int_ucmp_vi<0b01, "cmphi", SETUGT, SETULT>;
  defm CMPLO_PPzZI : sve_int_ucmp_vi<0b10, "cmplo", SETULT, SETUGT>;
  defm CMPLS_PPzZI : sve_int_ucmp_vi<0b11, "cmpls", SETULE, SETUGE>;

  defm FCMGE_PPzZZ : sve_fp_3op_p_pd_cc<0b000, "fcmge", SETOGE, SETGE, SETOLE, SETLE>;
  defm FCMGT_PPzZZ : sve_fp_3op_p_pd_cc<0b001, "fcmgt", SETOGT, SETGT, SETOLT, SETLT>;
  defm FCMEQ_PPzZZ : sve_fp_3op_p_pd_cc<0b010, "fcmeq", SETOEQ, SETEQ, SETOEQ, SETEQ>;
  defm FCMNE_PPzZZ : sve_fp_3op_p_pd_cc<0b011, "fcmne", SETUNE, SETNE, SETUNE, SETNE>;
  defm FCMUO_PPzZZ : sve_fp_3op_p_pd_cc<0b100, "fcmuo", SETUO, SETUO, SETUO, SETUO>;
  defm FACGE_PPzZZ : sve_fp_3op_p_pd<0b101, "facge", int_aarch64_sve_facge>;
  defm FACGT_PPzZZ : sve_fp_3op_p_pd<0b111, "facgt", int_aarch64_sve_facgt>;

  defm FCMGE_PPzZ0 : sve_fp_2op_p_pd<0b000, "fcmge", SETOGE, SETGE, SETOLE, SETLE>;
  defm FCMGT_PPzZ0 : sve_fp_2op_p_pd<0b001, "fcmgt", SETOGT, SETGT, SETOLT, SETLT>;
  defm FCMLT_PPzZ0 : sve_fp_2op_p_pd<0b010, "fcmlt", SETOLT, SETLT, SETOGT, SETGT>;
  defm FCMLE_PPzZ0 : sve_fp_2op_p_pd<0b011, "fcmle", SETOLE, SETLE, SETOGE, SETGE>;
  defm FCMEQ_PPzZ0 : sve_fp_2op_p_pd<0b100, "fcmeq", SETOEQ, SETEQ, SETOEQ, SETEQ>;
  defm FCMNE_PPzZ0 : sve_fp_2op_p_pd<0b110, "fcmne", SETUNE, SETNE, SETUNE, SETNE>;

  defm WHILELT_PWW : sve_int_while4_rr<0b010, "whilelt", int_aarch64_sve_whilelt, int_aarch64_sve_whilegt>;
  defm WHILELE_PWW : sve_int_while4_rr<0b011, "whilele", int_aarch64_sve_whilele, null_frag>;
  defm WHILELO_PWW : sve_int_while4_rr<0b110, "whilelo", int_aarch64_sve_whilelo, int_aarch64_sve_whilehi>;
  defm WHILELS_PWW : sve_int_while4_rr<0b111, "whilels", int_aarch64_sve_whilels, null_frag>;

  defm WHILELT_PXX : sve_int_while8_rr<0b010, "whilelt", int_aarch64_sve_whilelt, int_aarch64_sve_whilegt>;
  defm WHILELE_PXX : sve_int_while8_rr<0b011, "whilele", int_aarch64_sve_whilele, null_frag>;
  defm WHILELO_PXX : sve_int_while8_rr<0b110, "whilelo", int_aarch64_sve_whilelo, int_aarch64_sve_whilehi>;
  defm WHILELS_PXX : sve_int_while8_rr<0b111, "whilels", int_aarch64_sve_whilels, null_frag>;

  def CTERMEQ_WW : sve_int_cterm<0b0, 0b0, "ctermeq", GPR32>;
  def CTERMNE_WW : sve_int_cterm<0b0, 0b1, "ctermne", GPR32>;
  def CTERMEQ_XX : sve_int_cterm<0b1, 0b0, "ctermeq", GPR64>;
  def CTERMNE_XX : sve_int_cterm<0b1, 0b1, "ctermne", GPR64>;

  def RDVLI_XI  : sve_int_read_vl_a<0b0, 0b11111, "rdvl">;
  def ADDVL_XXI : sve_int_arith_vl<0b0, "addvl">;
  def ADDPL_XXI : sve_int_arith_vl<0b1, "addpl">;

  defm CNTB_XPiI : sve_int_count<0b000, "cntb", int_aarch64_sve_cntb>;
  defm CNTH_XPiI : sve_int_count<0b010, "cnth", int_aarch64_sve_cnth>;
  defm CNTW_XPiI : sve_int_count<0b100, "cntw", int_aarch64_sve_cntw>;
  defm CNTD_XPiI : sve_int_count<0b110, "cntd", int_aarch64_sve_cntd>;
  defm CNTP_XPP : sve_int_pcount_pred<0b0000, "cntp", int_aarch64_sve_cntp>;

  def : Pat<(i64 (AArch64CttzElts nxv16i1:$Op1)),
            (CNTP_XPP_B (BRKB_PPzP (PTRUE_B 31), PPR:$Op1),
                        (BRKB_PPzP (PTRUE_B 31), PPR:$Op1))>;

  def : Pat<(i64 (AArch64CttzElts nxv8i1:$Op1)),
            (CNTP_XPP_H (BRKB_PPzP (PTRUE_H 31), PPR:$Op1),
                        (BRKB_PPzP (PTRUE_H 31), PPR:$Op1))>;

  def : Pat<(i64 (AArch64CttzElts nxv4i1:$Op1)),
            (CNTP_XPP_S (BRKB_PPzP (PTRUE_S 31), PPR:$Op1),
                        (BRKB_PPzP (PTRUE_S 31), PPR:$Op1))>;

  def : Pat<(i64 (AArch64CttzElts nxv2i1:$Op1)),
            (CNTP_XPP_D (BRKB_PPzP (PTRUE_D 31), PPR:$Op1),
                        (BRKB_PPzP (PTRUE_D 31), PPR:$Op1))>;
}

  defm INCB_XPiI : sve_int_pred_pattern_a<0b000, "incb", add, int_aarch64_sve_cntb>;
  defm DECB_XPiI : sve_int_pred_pattern_a<0b001, "decb", sub, int_aarch64_sve_cntb>;
  defm INCH_XPiI : sve_int_pred_pattern_a<0b010, "inch", add, int_aarch64_sve_cnth>;
  defm DECH_XPiI : sve_int_pred_pattern_a<0b011, "dech", sub, int_aarch64_sve_cnth>;
  defm INCW_XPiI : sve_int_pred_pattern_a<0b100, "incw", add, int_aarch64_sve_cntw>;
  defm DECW_XPiI : sve_int_pred_pattern_a<0b101, "decw", sub, int_aarch64_sve_cntw>;
  defm INCD_XPiI : sve_int_pred_pattern_a<0b110, "incd", add, int_aarch64_sve_cntd>;
  defm DECD_XPiI : sve_int_pred_pattern_a<0b111, "decd", sub, int_aarch64_sve_cntd>;

let Predicates = [HasSVEorSME] in {
  defm SQINCB_XPiWdI : sve_int_pred_pattern_b_s32<0b00000, "sqincb", int_aarch64_sve_sqincb_n32>;
  defm UQINCB_WPiI   : sve_int_pred_pattern_b_u32<0b00001, "uqincb", int_aarch64_sve_uqincb_n32>;
  defm SQDECB_XPiWdI : sve_int_pred_pattern_b_s32<0b00010, "sqdecb", int_aarch64_sve_sqdecb_n32>;
  defm UQDECB_WPiI   : sve_int_pred_pattern_b_u32<0b00011, "uqdecb", int_aarch64_sve_uqdecb_n32>;
  defm SQINCB_XPiI   : sve_int_pred_pattern_b_x64<0b00100, "sqincb", int_aarch64_sve_sqincb_n64>;
  defm UQINCB_XPiI   : sve_int_pred_pattern_b_x64<0b00101, "uqincb", int_aarch64_sve_uqincb_n64>;
  defm SQDECB_XPiI   : sve_int_pred_pattern_b_x64<0b00110, "sqdecb", int_aarch64_sve_sqdecb_n64>;
  defm UQDECB_XPiI   : sve_int_pred_pattern_b_x64<0b00111, "uqdecb", int_aarch64_sve_uqdecb_n64>;

  defm SQINCH_XPiWdI : sve_int_pred_pattern_b_s32<0b01000, "sqinch", int_aarch64_sve_sqinch_n32>;
  defm UQINCH_WPiI   : sve_int_pred_pattern_b_u32<0b01001, "uqinch", int_aarch64_sve_uqinch_n32>;
  defm SQDECH_XPiWdI : sve_int_pred_pattern_b_s32<0b01010, "sqdech", int_aarch64_sve_sqdech_n32>;
  defm UQDECH_WPiI   : sve_int_pred_pattern_b_u32<0b01011, "uqdech", int_aarch64_sve_uqdech_n32>;
  defm SQINCH_XPiI   : sve_int_pred_pattern_b_x64<0b01100, "sqinch", int_aarch64_sve_sqinch_n64>;
  defm UQINCH_XPiI   : sve_int_pred_pattern_b_x64<0b01101, "uqinch", int_aarch64_sve_uqinch_n64>;
  defm SQDECH_XPiI   : sve_int_pred_pattern_b_x64<0b01110, "sqdech", int_aarch64_sve_sqdech_n64>;
  defm UQDECH_XPiI   : sve_int_pred_pattern_b_x64<0b01111, "uqdech", int_aarch64_sve_uqdech_n64>;

  defm SQINCW_XPiWdI : sve_int_pred_pattern_b_s32<0b10000, "sqincw", int_aarch64_sve_sqincw_n32>;
  defm UQINCW_WPiI   : sve_int_pred_pattern_b_u32<0b10001, "uqincw", int_aarch64_sve_uqincw_n32>;
  defm SQDECW_XPiWdI : sve_int_pred_pattern_b_s32<0b10010, "sqdecw", int_aarch64_sve_sqdecw_n32>;
  defm UQDECW_WPiI   : sve_int_pred_pattern_b_u32<0b10011, "uqdecw", int_aarch64_sve_uqdecw_n32>;
  defm SQINCW_XPiI   : sve_int_pred_pattern_b_x64<0b10100, "sqincw", int_aarch64_sve_sqincw_n64>;
  defm UQINCW_XPiI   : sve_int_pred_pattern_b_x64<0b10101, "uqincw", int_aarch64_sve_uqincw_n64>;
  defm SQDECW_XPiI   : sve_int_pred_pattern_b_x64<0b10110, "sqdecw", int_aarch64_sve_sqdecw_n64>;
  defm UQDECW_XPiI   : sve_int_pred_pattern_b_x64<0b10111, "uqdecw", int_aarch64_sve_uqdecw_n64>;

  defm SQINCD_XPiWdI : sve_int_pred_pattern_b_s32<0b11000, "sqincd", int_aarch64_sve_sqincd_n32>;
  defm UQINCD_WPiI   : sve_int_pred_pattern_b_u32<0b11001, "uqincd", int_aarch64_sve_uqincd_n32>;
  defm SQDECD_XPiWdI : sve_int_pred_pattern_b_s32<0b11010, "sqdecd", int_aarch64_sve_sqdecd_n32>;
  defm UQDECD_WPiI   : sve_int_pred_pattern_b_u32<0b11011, "uqdecd", int_aarch64_sve_uqdecd_n32>;
  defm SQINCD_XPiI   : sve_int_pred_pattern_b_x64<0b11100, "sqincd", int_aarch64_sve_sqincd_n64>;
  defm UQINCD_XPiI   : sve_int_pred_pattern_b_x64<0b11101, "uqincd", int_aarch64_sve_uqincd_n64>;
  defm SQDECD_XPiI   : sve_int_pred_pattern_b_x64<0b11110, "sqdecd", int_aarch64_sve_sqdecd_n64>;
  defm UQDECD_XPiI   : sve_int_pred_pattern_b_x64<0b11111, "uqdecd", int_aarch64_sve_uqdecd_n64>;

  defm SQINCH_ZPiI : sve_int_countvlv<0b01000, "sqinch", ZPR16, int_aarch64_sve_sqinch, nxv8i16>;
  defm UQINCH_ZPiI : sve_int_countvlv<0b01001, "uqinch", ZPR16, int_aarch64_sve_uqinch, nxv8i16>;
  defm SQDECH_ZPiI : sve_int_countvlv<0b01010, "sqdech", ZPR16, int_aarch64_sve_sqdech, nxv8i16>;
  defm UQDECH_ZPiI : sve_int_countvlv<0b01011, "uqdech", ZPR16, int_aarch64_sve_uqdech, nxv8i16>;
  defm INCH_ZPiI   : sve_int_countvlv<0b01100, "inch",   ZPR16>;
  defm DECH_ZPiI   : sve_int_countvlv<0b01101, "dech",   ZPR16>;
  defm SQINCW_ZPiI : sve_int_countvlv<0b10000, "sqincw", ZPR32, int_aarch64_sve_sqincw, nxv4i32>;
  defm UQINCW_ZPiI : sve_int_countvlv<0b10001, "uqincw", ZPR32, int_aarch64_sve_uqincw, nxv4i32>;
  defm SQDECW_ZPiI : sve_int_countvlv<0b10010, "sqdecw", ZPR32, int_aarch64_sve_sqdecw, nxv4i32>;
  defm UQDECW_ZPiI : sve_int_countvlv<0b10011, "uqdecw", ZPR32, int_aarch64_sve_uqdecw, nxv4i32>;
  defm INCW_ZPiI   : sve_int_countvlv<0b10100, "incw",   ZPR32>;
  defm DECW_ZPiI   : sve_int_countvlv<0b10101, "decw",   ZPR32>;
  defm SQINCD_ZPiI : sve_int_countvlv<0b11000, "sqincd", ZPR64, int_aarch64_sve_sqincd, nxv2i64>;
  defm UQINCD_ZPiI : sve_int_countvlv<0b11001, "uqincd", ZPR64, int_aarch64_sve_uqincd, nxv2i64>;
  defm SQDECD_ZPiI : sve_int_countvlv<0b11010, "sqdecd", ZPR64, int_aarch64_sve_sqdecd, nxv2i64>;
  defm UQDECD_ZPiI : sve_int_countvlv<0b11011, "uqdecd", ZPR64, int_aarch64_sve_uqdecd, nxv2i64>;
  defm INCD_ZPiI   : sve_int_countvlv<0b11100, "incd",   ZPR64>;
  defm DECD_ZPiI   : sve_int_countvlv<0b11101, "decd",   ZPR64>;

  defm SQINCP_XPWd : sve_int_count_r_s32<0b00000, "sqincp", int_aarch64_sve_sqincp_n32>;
  defm SQINCP_XP   : sve_int_count_r_x64<0b00010, "sqincp", int_aarch64_sve_sqincp_n64>;
  defm UQINCP_WP   : sve_int_count_r_u32<0b00100, "uqincp", int_aarch64_sve_uqincp_n32>;
  defm UQINCP_XP   : sve_int_count_r_x64<0b00110, "uqincp", int_aarch64_sve_uqincp_n64>;
  defm SQDECP_XPWd : sve_int_count_r_s32<0b01000, "sqdecp", int_aarch64_sve_sqdecp_n32>;
  defm SQDECP_XP   : sve_int_count_r_x64<0b01010, "sqdecp", int_aarch64_sve_sqdecp_n64>;
  defm UQDECP_WP   : sve_int_count_r_u32<0b01100, "uqdecp", int_aarch64_sve_uqdecp_n32>;
  defm UQDECP_XP   : sve_int_count_r_x64<0b01110, "uqdecp", int_aarch64_sve_uqdecp_n64>;
  defm INCP_XP     : sve_int_count_r_x64<0b10000, "incp", null_frag, add>;
  defm DECP_XP     : sve_int_count_r_x64<0b10100, "decp", null_frag, sub>;

  defm SQINCP_ZP   : sve_int_count_v<0b00000, "sqincp", int_aarch64_sve_sqincp>;
  defm UQINCP_ZP   : sve_int_count_v<0b00100, "uqincp", int_aarch64_sve_uqincp>;
  defm SQDECP_ZP   : sve_int_count_v<0b01000, "sqdecp", int_aarch64_sve_sqdecp>;
  defm UQDECP_ZP   : sve_int_count_v<0b01100, "uqdecp", int_aarch64_sve_uqdecp>;
  defm INCP_ZP     : sve_int_count_v<0b10000, "incp">;
  defm DECP_ZP     : sve_int_count_v<0b10100, "decp">;

  def : Pat<(i64 (add GPR64:$Op1, (i64 (AArch64CttzElts nxv16i1:$Op2)))),
            (INCP_XP_B (BRKB_PPzP (PTRUE_B 31), PPR:$Op2), GPR64:$Op1)>;

  def : Pat<(i32 (add GPR32:$Op1, (trunc (i64 (AArch64CttzElts nxv16i1:$Op2))))),
            (EXTRACT_SUBREG (INCP_XP_B (BRKB_PPzP (PTRUE_B 31), PPR:$Op2),
                                       (INSERT_SUBREG (IMPLICIT_DEF), GPR32:$Op1, sub_32)),
                            sub_32)>;

  def : Pat<(i64 (add GPR64:$Op1, (i64 (AArch64CttzElts nxv8i1:$Op2)))),
            (INCP_XP_H (BRKB_PPzP (PTRUE_H 31), PPR:$Op2), GPR64:$Op1)>;

  def : Pat<(i32 (add GPR32:$Op1, (trunc (i64 (AArch64CttzElts nxv8i1:$Op2))))),
            (EXTRACT_SUBREG (INCP_XP_H (BRKB_PPzP (PTRUE_H 31), PPR:$Op2),
                                       (INSERT_SUBREG (IMPLICIT_DEF), GPR32:$Op1, sub_32)),
                            sub_32)>;

  def : Pat<(i64 (add GPR64:$Op1, (i64 (AArch64CttzElts nxv4i1:$Op2)))),
            (INCP_XP_S (BRKB_PPzP (PTRUE_S 31), PPR:$Op2), GPR64:$Op1)>;

  def : Pat<(i32 (add GPR32:$Op1, (trunc (i64 (AArch64CttzElts nxv4i1:$Op2))))),
            (EXTRACT_SUBREG (INCP_XP_S (BRKB_PPzP (PTRUE_S 31), PPR:$Op2),
                                       (INSERT_SUBREG (IMPLICIT_DEF), GPR32:$Op1, sub_32)),
                            sub_32)>;

  def : Pat<(i64 (add GPR64:$Op1, (i64 (AArch64CttzElts nxv2i1:$Op2)))),
            (INCP_XP_D (BRKB_PPzP (PTRUE_D 31), PPR:$Op2), GPR64:$Op1)>;

  def : Pat<(i32 (add GPR32:$Op1, (trunc (i64 (AArch64CttzElts nxv2i1:$Op2))))),
            (EXTRACT_SUBREG (INCP_XP_D (BRKB_PPzP (PTRUE_D 31), PPR:$Op2),
                                       (INSERT_SUBREG (IMPLICIT_DEF), GPR32:$Op1, sub_32)),
                            sub_32)>;

  defm INDEX_RR : sve_int_index_rr<"index", AArch64mul_p_oneuse>;
  defm INDEX_IR : sve_int_index_ir<"index", AArch64mul_p, AArch64mul_p_oneuse>;
  defm INDEX_RI : sve_int_index_ri<"index">;
  defm INDEX_II : sve_int_index_ii<"index">;

  // Unpredicated shifts
  defm ASR_ZZI : sve_int_bin_cons_shift_imm_right<0b00, "asr", AArch64asr_p>;
  defm LSR_ZZI : sve_int_bin_cons_shift_imm_right<0b01, "lsr", AArch64lsr_p>;
  defm LSL_ZZI : sve_int_bin_cons_shift_imm_left< 0b11, "lsl", AArch64lsl_p>;

  defm ASR_WIDE_ZZZ : sve_int_bin_cons_shift_wide<0b00, "asr", int_aarch64_sve_asr_wide>;
  defm LSR_WIDE_ZZZ : sve_int_bin_cons_shift_wide<0b01, "lsr", int_aarch64_sve_lsr_wide>;
  defm LSL_WIDE_ZZZ : sve_int_bin_cons_shift_wide<0b11, "lsl", int_aarch64_sve_lsl_wide>;

  // Predicated shifts
  defm ASR_ZPmI  : sve_int_bin_pred_shift_imm_right_dup<0b0000, "asr",  "ASR_ZPZI",  int_aarch64_sve_asr>;
  defm LSR_ZPmI  : sve_int_bin_pred_shift_imm_right_dup<0b0001, "lsr",  "LSR_ZPZI",  int_aarch64_sve_lsr>;
  defm LSL_ZPmI  : sve_int_bin_pred_shift_imm_left_dup< 0b0011, "lsl",  "LSL_ZPZI",  int_aarch64_sve_lsl>;
  defm ASRD_ZPmI : sve_int_bin_pred_shift_imm_right<    0b0100, "asrd", "ASRD_ZPZI", AArch64asrd_m1>;

  defm ASR_ZPZI : sve_int_shift_pred_bhsd<AArch64asr_p, SVEShiftImmR8, SVEShiftImmR16, SVEShiftImmR32, SVEShiftImmR64>;
  defm LSR_ZPZI : sve_int_shift_pred_bhsd<AArch64lsr_p, SVEShiftImmR8, SVEShiftImmR16, SVEShiftImmR32, SVEShiftImmR64>;
  defm LSL_ZPZI : sve_int_shift_pred_bhsd<AArch64lsl_p, SVEShiftImmL8, SVEShiftImmL16, SVEShiftImmL32, SVEShiftImmL64>;
} // End HasSVEorSME

let Predicates = [HasSVEorSME, UseExperimentalZeroingPseudos] in {
  defm ASR_ZPZZ  : sve_int_bin_pred_zeroing_bhsd<int_aarch64_sve_asr>;
  defm LSR_ZPZZ  : sve_int_bin_pred_zeroing_bhsd<int_aarch64_sve_lsr>;
  defm LSL_ZPZZ  : sve_int_bin_pred_zeroing_bhsd<int_aarch64_sve_lsl>;
  defm ASRD_ZPZI : sve_int_bin_pred_shift_imm_right_zeroing_bhsd<AArch64asrd_m1>;

  defm ASR_ZPZI  : sve_int_bin_pred_imm_zeroing_bhsd<int_aarch64_sve_asr, SVEShiftImmR8, SVEShiftImmR16, SVEShiftImmR32, SVEShiftImmR64>;
  defm LSR_ZPZI  : sve_int_bin_pred_imm_zeroing_bhsd<int_aarch64_sve_lsr, SVEShiftImmR8, SVEShiftImmR16, SVEShiftImmR32, SVEShiftImmR64>;
  defm LSL_ZPZI  : sve_int_bin_pred_imm_zeroing_bhsd<int_aarch64_sve_lsl, SVEShiftImmL8, SVEShiftImmL16, SVEShiftImmL32, SVEShiftImmL64>;
} // End HasSVEorSME, UseExperimentalZeroingPseudos

let Predicates = [HasSVEorSME] in {
  defm ASR_ZPmZ  : sve_int_bin_pred_shift<0b000, "asr", "ASR_ZPZZ", int_aarch64_sve_asr, "ASRR_ZPmZ">;
  defm LSR_ZPmZ  : sve_int_bin_pred_shift<0b001, "lsr", "LSR_ZPZZ", int_aarch64_sve_lsr, "LSRR_ZPmZ">;
  defm LSL_ZPmZ  : sve_int_bin_pred_shift<0b011, "lsl", "LSL_ZPZZ", int_aarch64_sve_lsl, "LSLR_ZPmZ">;
  defm ASRR_ZPmZ : sve_int_bin_pred_shift<0b100, "asrr", "ASRR_ZPZZ", null_frag, "ASR_ZPmZ", /*isReverseInstr*/ 1>;
  defm LSRR_ZPmZ : sve_int_bin_pred_shift<0b101, "lsrr", "LSRR_ZPZZ", null_frag, "LSR_ZPmZ", /*isReverseInstr*/ 1>;
  defm LSLR_ZPmZ : sve_int_bin_pred_shift<0b111, "lslr", "LSLR_ZPZZ", null_frag, "LSL_ZPmZ", /*isReverseInstr*/ 1>;

  defm ASR_ZPZZ : sve_int_bin_pred_bhsd<AArch64asr_p>;
  defm LSR_ZPZZ : sve_int_bin_pred_bhsd<AArch64lsr_p>;
  defm LSL_ZPZZ : sve_int_bin_pred_bhsd<AArch64lsl_p>;

  defm ASR_WIDE_ZPmZ : sve_int_bin_pred_shift_wide<0b000, "asr", int_aarch64_sve_asr_wide>;
  defm LSR_WIDE_ZPmZ : sve_int_bin_pred_shift_wide<0b001, "lsr", int_aarch64_sve_lsr_wide>;
  defm LSL_WIDE_ZPmZ : sve_int_bin_pred_shift_wide<0b011, "lsl", int_aarch64_sve_lsl_wide>;

  defm FCVT_ZPmZ_StoH   : sve_fp_2op_p_zdr<0b1001000, "fcvt",   ZPR32, ZPR16, int_aarch64_sve_fcvt_f16f32,   AArch64fcvtr_mt,  nxv4f16, nxv4i1, nxv4f32, ElementSizeS>;
  defm FCVT_ZPmZ_HtoS   : sve_fp_2op_p_zd< 0b1001001, "fcvt",   ZPR16, ZPR32, int_aarch64_sve_fcvt_f32f16,   AArch64fcvte_mt,  nxv4f32, nxv4i1, nxv4f16, ElementSizeS>;
  defm SCVTF_ZPmZ_HtoH  : sve_fp_2op_p_zd< 0b0110010, "scvtf",  ZPR16, ZPR16, null_frag,                     AArch64scvtf_mt,  nxv8f16, nxv8i1, nxv8i16, ElementSizeH>;
  defm SCVTF_ZPmZ_StoS  : sve_fp_2op_p_zd< 0b1010100, "scvtf",  ZPR32, ZPR32, null_frag,                     AArch64scvtf_mt,  nxv4f32, nxv4i1, nxv4i32, ElementSizeS>;
  defm UCVTF_ZPmZ_StoS  : sve_fp_2op_p_zd< 0b1010101, "ucvtf",  ZPR32, ZPR32, null_frag,                     AArch64ucvtf_mt,  nxv4f32, nxv4i1, nxv4i32, ElementSizeS>;
  defm UCVTF_ZPmZ_HtoH  : sve_fp_2op_p_zd< 0b0110011, "ucvtf",  ZPR16, ZPR16, null_frag,                     AArch64ucvtf_mt,  nxv8f16, nxv8i1, nxv8i16, ElementSizeH>;
  defm FCVTZS_ZPmZ_HtoH : sve_fp_2op_p_zd< 0b0111010, "fcvtzs", ZPR16, ZPR16, null_frag,                     AArch64fcvtzs_mt, nxv8i16, nxv8i1, nxv8f16, ElementSizeH>;
  defm FCVTZS_ZPmZ_StoS : sve_fp_2op_p_zd< 0b1011100, "fcvtzs", ZPR32, ZPR32, null_frag,                     AArch64fcvtzs_mt, nxv4i32, nxv4i1, nxv4f32, ElementSizeS>;
  defm FCVTZU_ZPmZ_HtoH : sve_fp_2op_p_zd< 0b0111011, "fcvtzu", ZPR16, ZPR16, null_frag,                     AArch64fcvtzu_mt, nxv8i16, nxv8i1, nxv8f16, ElementSizeH>;
  defm FCVTZU_ZPmZ_StoS : sve_fp_2op_p_zd< 0b1011101, "fcvtzu", ZPR32, ZPR32, null_frag,                     AArch64fcvtzu_mt, nxv4i32, nxv4i1, nxv4f32, ElementSizeS>;
  defm FCVT_ZPmZ_DtoH   : sve_fp_2op_p_zdr<0b1101000, "fcvt",   ZPR64, ZPR16, int_aarch64_sve_fcvt_f16f64,   AArch64fcvtr_mt,  nxv2f16, nxv2i1, nxv2f64, ElementSizeD>;
  defm FCVT_ZPmZ_HtoD   : sve_fp_2op_p_zd< 0b1101001, "fcvt",   ZPR16, ZPR64, int_aarch64_sve_fcvt_f64f16,   AArch64fcvte_mt,  nxv2f64, nxv2i1, nxv2f16, ElementSizeD>;
  defm FCVT_ZPmZ_DtoS   : sve_fp_2op_p_zdr<0b1101010, "fcvt",   ZPR64, ZPR32, int_aarch64_sve_fcvt_f32f64,   AArch64fcvtr_mt,  nxv2f32, nxv2i1, nxv2f64, ElementSizeD>;
  defm FCVT_ZPmZ_StoD   : sve_fp_2op_p_zd< 0b1101011, "fcvt",   ZPR32, ZPR64, int_aarch64_sve_fcvt_f64f32,   AArch64fcvte_mt,  nxv2f64, nxv2i1, nxv2f32, ElementSizeD>;
  defm SCVTF_ZPmZ_StoD  : sve_fp_2op_p_zd< 0b1110000, "scvtf",  ZPR32, ZPR64, int_aarch64_sve_scvtf_f64i32,  AArch64scvtf_mt,  nxv2f64, nxv2i1, nxv4i32, ElementSizeD>;
  defm UCVTF_ZPmZ_StoD  : sve_fp_2op_p_zd< 0b1110001, "ucvtf",  ZPR32, ZPR64, int_aarch64_sve_ucvtf_f64i32,  AArch64ucvtf_mt,  nxv2f64, nxv2i1, nxv4i32, ElementSizeD>;
  defm UCVTF_ZPmZ_StoH  : sve_fp_2op_p_zd< 0b0110101, "ucvtf",  ZPR32, ZPR16, int_aarch64_sve_ucvtf_f16i32,  AArch64ucvtf_mt,  nxv4f16, nxv4i1, nxv4i32, ElementSizeS>;
  defm SCVTF_ZPmZ_DtoS  : sve_fp_2op_p_zd< 0b1110100, "scvtf",  ZPR64, ZPR32, int_aarch64_sve_scvtf_f32i64,  AArch64scvtf_mt,  nxv2f32, nxv2i1, nxv2i64, ElementSizeD>;
  defm SCVTF_ZPmZ_StoH  : sve_fp_2op_p_zd< 0b0110100, "scvtf",  ZPR32, ZPR16, int_aarch64_sve_scvtf_f16i32,  AArch64scvtf_mt,  nxv4f16, nxv4i1, nxv4i32, ElementSizeS>;
  defm SCVTF_ZPmZ_DtoH  : sve_fp_2op_p_zd< 0b0110110, "scvtf",  ZPR64, ZPR16, int_aarch64_sve_scvtf_f16i64,  AArch64scvtf_mt,  nxv2f16, nxv2i1, nxv2i64, ElementSizeD>;
  defm UCVTF_ZPmZ_DtoS  : sve_fp_2op_p_zd< 0b1110101, "ucvtf",  ZPR64, ZPR32, int_aarch64_sve_ucvtf_f32i64,  AArch64ucvtf_mt,  nxv2f32, nxv2i1, nxv2i64, ElementSizeD>;
  defm UCVTF_ZPmZ_DtoH  : sve_fp_2op_p_zd< 0b0110111, "ucvtf",  ZPR64, ZPR16, int_aarch64_sve_ucvtf_f16i64,  AArch64ucvtf_mt,  nxv2f16, nxv2i1, nxv2i64, ElementSizeD>;
  defm SCVTF_ZPmZ_DtoD  : sve_fp_2op_p_zd< 0b1110110, "scvtf",  ZPR64, ZPR64, null_frag,                     AArch64scvtf_mt,  nxv2f64, nxv2i1, nxv2i64, ElementSizeD>;
  defm UCVTF_ZPmZ_DtoD  : sve_fp_2op_p_zd< 0b1110111, "ucvtf",  ZPR64, ZPR64, null_frag,                     AArch64ucvtf_mt,  nxv2f64, nxv2i1, nxv2i64, ElementSizeD>;
  defm FCVTZS_ZPmZ_DtoS : sve_fp_2op_p_zd< 0b1111000, "fcvtzs", ZPR64, ZPR32, int_aarch64_sve_fcvtzs_i32f64, null_frag,        nxv4i32, nxv2i1, nxv2f64, ElementSizeD>;
  defm FCVTZU_ZPmZ_DtoS : sve_fp_2op_p_zd< 0b1111001, "fcvtzu", ZPR64, ZPR32, int_aarch64_sve_fcvtzu_i32f64, null_frag,        nxv4i32, nxv2i1, nxv2f64, ElementSizeD>;
  defm FCVTZS_ZPmZ_StoD : sve_fp_2op_p_zd< 0b1111100, "fcvtzs", ZPR32, ZPR64, int_aarch64_sve_fcvtzs_i64f32, AArch64fcvtzs_mt, nxv2i64, nxv2i1, nxv2f32, ElementSizeD>;
  defm FCVTZS_ZPmZ_HtoS : sve_fp_2op_p_zd< 0b0111100, "fcvtzs", ZPR16, ZPR32, int_aarch64_sve_fcvtzs_i32f16, AArch64fcvtzs_mt, nxv4i32, nxv4i1, nxv4f16, ElementSizeS>;
  defm FCVTZS_ZPmZ_HtoD : sve_fp_2op_p_zd< 0b0111110, "fcvtzs", ZPR16, ZPR64, int_aarch64_sve_fcvtzs_i64f16, AArch64fcvtzs_mt, nxv2i64, nxv2i1, nxv2f16, ElementSizeD>;
  defm FCVTZU_ZPmZ_HtoS : sve_fp_2op_p_zd< 0b0111101, "fcvtzu", ZPR16, ZPR32, int_aarch64_sve_fcvtzu_i32f16, AArch64fcvtzu_mt, nxv4i32, nxv4i1, nxv4f16, ElementSizeS>;
  defm FCVTZU_ZPmZ_HtoD : sve_fp_2op_p_zd< 0b0111111, "fcvtzu", ZPR16, ZPR64, int_aarch64_sve_fcvtzu_i64f16, AArch64fcvtzu_mt, nxv2i64, nxv2i1, nxv2f16, ElementSizeD>;
  defm FCVTZU_ZPmZ_StoD : sve_fp_2op_p_zd< 0b1111101, "fcvtzu", ZPR32, ZPR64, int_aarch64_sve_fcvtzu_i64f32, AArch64fcvtzu_mt, nxv2i64, nxv2i1, nxv2f32, ElementSizeD>;
  defm FCVTZS_ZPmZ_DtoD : sve_fp_2op_p_zd< 0b1111110, "fcvtzs", ZPR64, ZPR64, null_frag,                     AArch64fcvtzs_mt, nxv2i64, nxv2i1, nxv2f64, ElementSizeD>;
  defm FCVTZU_ZPmZ_DtoD : sve_fp_2op_p_zd< 0b1111111, "fcvtzu", ZPR64, ZPR64, null_frag,                     AArch64fcvtzu_mt, nxv2i64, nxv2i1, nxv2f64, ElementSizeD>;

  //These patterns exist to improve the code quality of conversions on unpacked types.
  def : Pat<(nxv2f32 (AArch64fcvte_mt (nxv2i1 (SVEAllActive:$Pg)), nxv2f16:$Zs, nxv2f32:$Zd)),
            (FCVT_ZPmZ_HtoS_UNDEF ZPR:$Zd, PPR:$Pg, ZPR:$Zs)>;

  // FP_ROUND has an additional 'precise' flag which indicates the type of rounding.
  // This is ignored by the pattern below where it is matched by (i64 timm0_1)
  def : Pat<(nxv2f16 (AArch64fcvtr_mt (nxv2i1 (SVEAllActive:$Pg)), nxv2f32:$Zs, (i64 timm0_1), nxv2f16:$Zd)),
            (FCVT_ZPmZ_StoH_UNDEF ZPR:$Zd, PPR:$Pg, ZPR:$Zs)>;

  // Signed integer -> Floating-point 
  def : Pat<(nxv2f16 (AArch64scvtf_mt (nxv2i1 (SVEAllActive):$Pg),
                      (sext_inreg nxv2i64:$Zs, nxv2i16), nxv2f16:$Zd)),
            (SCVTF_ZPmZ_HtoH_UNDEF ZPR:$Zd, PPR:$Pg, ZPR:$Zs)>;

  def : Pat<(nxv4f16 (AArch64scvtf_mt (nxv4i1 (SVEAllActive):$Pg),
                      (sext_inreg nxv4i32:$Zs, nxv4i16), nxv4f16:$Zd)),
            (SCVTF_ZPmZ_HtoH_UNDEF ZPR:$Zd, PPR:$Pg, ZPR:$Zs)>;

  def : Pat<(nxv2f16 (AArch64scvtf_mt (nxv2i1 (SVEAllActive):$Pg),
                      (sext_inreg nxv2i64:$Zs, nxv2i32), nxv2f16:$Zd)),
            (SCVTF_ZPmZ_StoH_UNDEF ZPR:$Zd, PPR:$Pg, ZPR:$Zs)>;

  def : Pat<(nxv2f32 (AArch64scvtf_mt (nxv2i1 (SVEAllActive):$Pg),
                      (sext_inreg nxv2i64:$Zs, nxv2i32), nxv2f32:$Zd)),
            (SCVTF_ZPmZ_StoS_UNDEF ZPR:$Zd, PPR:$Pg, ZPR:$Zs)>;

  def : Pat<(nxv2f64 (AArch64scvtf_mt (nxv2i1 (SVEAllActive):$Pg),
                      (sext_inreg nxv2i64:$Zs, nxv2i32), nxv2f64:$Zd)),
            (SCVTF_ZPmZ_StoD_UNDEF ZPR:$Zd, PPR:$Pg, ZPR:$Zs)>;

  // Unsigned integer -> Floating-point
  def : Pat<(nxv2f16 (AArch64ucvtf_mt (nxv2i1 (SVEAllActive:$Pg)),
                      (and nxv2i64:$Zs,
                       (nxv2i64 (splat_vector (i64 0xFFFF)))), nxv2f16:$Zd)),
            (UCVTF_ZPmZ_HtoH_UNDEF ZPR:$Zd, PPR:$Pg, ZPR:$Zs)>;

  def : Pat<(nxv2f16 (AArch64ucvtf_mt (nxv2i1 (SVEAllActive:$Pg)),
                      (and nxv2i64:$Zs,
                       (nxv2i64 (splat_vector (i64 0xFFFFFFFF)))), nxv2f16:$Zd)),
            (UCVTF_ZPmZ_StoH_UNDEF ZPR:$Zd, PPR:$Pg, ZPR:$Zs)>;

  def : Pat<(nxv4f16 (AArch64ucvtf_mt (nxv4i1 (SVEAllActive:$Pg)),
                      (and nxv4i32:$Zs,
                       (nxv4i32 (splat_vector (i32 0xFFFF)))), nxv4f16:$Zd)),
            (UCVTF_ZPmZ_HtoH_UNDEF ZPR:$Zd, PPR:$Pg, ZPR:$Zs)>;

  def : Pat<(nxv2f32 (AArch64ucvtf_mt (nxv2i1 (SVEAllActive:$Pg)),
                      (and nxv2i64:$Zs,
                       (nxv2i64 (splat_vector (i64 0xFFFFFFFF)))), nxv2f32:$Zd)),
            (UCVTF_ZPmZ_StoS_UNDEF ZPR:$Zd, PPR:$Pg, ZPR:$Zs)>;

  def : Pat<(nxv2f64 (AArch64ucvtf_mt (nxv2i1 (SVEAllActive:$Pg)),
                      (and nxv2i64:$Zs,
                       (nxv2i64 (splat_vector (i64 0xFFFFFFFF)))), nxv2f64:$Zd)),
            (UCVTF_ZPmZ_StoD_UNDEF ZPR:$Zd, PPR:$Pg, ZPR:$Zs)>;

  defm FRINTN_ZPmZ : sve_fp_2op_p_zd_HSD<0b00000, "frintn", AArch64frintn_mt>;
  defm FRINTP_ZPmZ : sve_fp_2op_p_zd_HSD<0b00001, "frintp", AArch64frintp_mt>;
  defm FRINTM_ZPmZ : sve_fp_2op_p_zd_HSD<0b00010, "frintm", AArch64frintm_mt>;
  defm FRINTZ_ZPmZ : sve_fp_2op_p_zd_HSD<0b00011, "frintz", AArch64frintz_mt>;
  defm FRINTA_ZPmZ : sve_fp_2op_p_zd_HSD<0b00100, "frinta", AArch64frinta_mt>;
  defm FRINTX_ZPmZ : sve_fp_2op_p_zd_HSD<0b00110, "frintx", AArch64frintx_mt>;
  defm FRINTI_ZPmZ : sve_fp_2op_p_zd_HSD<0b00111, "frinti", AArch64frinti_mt>;
  defm FRECPX_ZPmZ : sve_fp_2op_p_zd_HSD<0b01100, "frecpx", AArch64frecpx_mt>;
  defm FSQRT_ZPmZ  : sve_fp_2op_p_zd_HSD<0b01101, "fsqrt",  AArch64fsqrt_mt>;
} // End HasSVEorSME

let Predicates = [HasBF16, HasSVEorSME] in {
  defm BFDOT_ZZZ    : sve_float_dot<0b1, 0b0, ZPR32, ZPR16, "bfdot", nxv8bf16, int_aarch64_sve_bfdot>;
  defm BFDOT_ZZI    : sve_float_dot_indexed<0b1, 0b00, ZPR16, ZPR3b16, "bfdot", nxv8bf16, int_aarch64_sve_bfdot_lane_v2>;
} // End HasBF16, HasSVEorSME

let Predicates = [HasBF16, HasSVE] in {
  defm BFMMLA_ZZZ   : sve_bfloat_matmul<"bfmmla", int_aarch64_sve_bfmmla>;
} // End HasBF16, HasSVE

let Predicates = [HasBF16, HasSVEorSME] in {
  defm BFMLALB_ZZZ : sve2_fp_mla_long<0b100, "bfmlalb", nxv4f32, nxv8bf16, int_aarch64_sve_bfmlalb>;
  defm BFMLALT_ZZZ : sve2_fp_mla_long<0b101, "bfmlalt", nxv4f32, nxv8bf16, int_aarch64_sve_bfmlalt>;
  defm BFMLALB_ZZZI : sve2_fp_mla_long_by_indexed_elem<0b100, "bfmlalb", nxv4f32, nxv8bf16, int_aarch64_sve_bfmlalb_lane_v2>;
  defm BFMLALT_ZZZI : sve2_fp_mla_long_by_indexed_elem<0b101, "bfmlalt", nxv4f32, nxv8bf16, int_aarch64_sve_bfmlalt_lane_v2>;
  defm BFCVT_ZPmZ   : sve_bfloat_convert<0b1, "bfcvt",   int_aarch64_sve_fcvt_bf16f32>;
  defm BFCVTNT_ZPmZ : sve_bfloat_convert<0b0, "bfcvtnt", int_aarch64_sve_fcvtnt_bf16f32>;
} // End HasBF16, HasSVEorSME

let Predicates = [HasSVEorSME] in {
  // InstAliases
  def : InstAlias<"mov $Zd, $Zn",
                  (ORR_ZZZ ZPR64:$Zd, ZPR64:$Zn, ZPR64:$Zn), 1>;
  def : InstAlias<"mov $Pd, $Pg/m, $Pn",
                  (SEL_PPPP PPR8:$Pd, PPRAny:$Pg, PPR8:$Pn, PPR8:$Pd), 1>;
  def : InstAlias<"mov $Pd, $Pn",
                  (ORR_PPzPP PPR8:$Pd, PPR8:$Pn, PPR8:$Pn, PPR8:$Pn), 1>;
  def : InstAlias<"mov $Pd, $Pg/z, $Pn",
                  (AND_PPzPP PPR8:$Pd, PPRAny:$Pg, PPR8:$Pn, PPR8:$Pn), 1>;

  def : InstAlias<"movs $Pd, $Pn",
                  (ORRS_PPzPP PPR8:$Pd, PPR8:$Pn, PPR8:$Pn, PPR8:$Pn), 1>;
  def : InstAlias<"movs $Pd, $Pg/z, $Pn",
                  (ANDS_PPzPP PPR8:$Pd, PPRAny:$Pg, PPR8:$Pn, PPR8:$Pn), 1>;

  def : InstAlias<"not $Pd, $Pg/z, $Pn",
                  (EOR_PPzPP PPR8:$Pd, PPRAny:$Pg, PPR8:$Pn, PPRAny:$Pg), 1>;

  def : InstAlias<"nots $Pd, $Pg/z, $Pn",
                  (EORS_PPzPP PPR8:$Pd, PPRAny:$Pg, PPR8:$Pn, PPRAny:$Pg), 1>;

  def : InstAlias<"cmple $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGE_PPzZZ_B PPR8:$Zd, PPR3bAny:$Pg, ZPR8:$Zn, ZPR8:$Zm), 0>;
  def : InstAlias<"cmple $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGE_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"cmple $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGE_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"cmple $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGE_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"cmplo $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHI_PPzZZ_B PPR8:$Zd, PPR3bAny:$Pg, ZPR8:$Zn, ZPR8:$Zm), 0>;
  def : InstAlias<"cmplo $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHI_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"cmplo $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHI_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"cmplo $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHI_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"cmpls $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHS_PPzZZ_B PPR8:$Zd, PPR3bAny:$Pg, ZPR8:$Zn, ZPR8:$Zm), 0>;
  def : InstAlias<"cmpls $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHS_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"cmpls $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHS_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"cmpls $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHS_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"cmplt $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGT_PPzZZ_B PPR8:$Zd, PPR3bAny:$Pg, ZPR8:$Zn, ZPR8:$Zm), 0>;
  def : InstAlias<"cmplt $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGT_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"cmplt $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGT_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"cmplt $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGT_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"facle $Zd, $Pg/z, $Zm, $Zn",
                  (FACGE_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"facle $Zd, $Pg/z, $Zm, $Zn",
                  (FACGE_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"facle $Zd, $Pg/z, $Zm, $Zn",
                  (FACGE_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"faclt $Zd, $Pg/z, $Zm, $Zn",
                  (FACGT_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"faclt $Zd, $Pg/z, $Zm, $Zn",
                  (FACGT_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"faclt $Zd, $Pg/z, $Zm, $Zn",
                  (FACGT_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"fcmle $Zd, $Pg/z, $Zm, $Zn",
                  (FCMGE_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"fcmle $Zd, $Pg/z, $Zm, $Zn",
                  (FCMGE_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"fcmle $Zd, $Pg/z, $Zm, $Zn",
                  (FCMGE_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"fcmlt $Zd, $Pg/z, $Zm, $Zn",
                  (FCMGT_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"fcmlt $Zd, $Pg/z, $Zm, $Zn",
                  (FCMGT_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"fcmlt $Zd, $Pg/z, $Zm, $Zn",
                  (FCMGT_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  // Pseudo instructions representing unpredicated LDR and STR for ZPR2,3,4.
  // These get expanded to individual LDR_ZXI/STR_ZXI instructions in
  // AArch64ExpandPseudoInsts.
  let mayLoad = 1, hasSideEffects = 0 in {
    def LDR_ZZXI   : Pseudo<(outs   ZZ_b_strided_and_contiguous:$Zd), (ins GPR64sp:$sp, simm4s1:$offset),[]>, Sched<[]>;
    def LDR_ZZZXI  : Pseudo<(outs  ZZZ_b:$Zd), (ins GPR64sp:$sp, simm4s1:$offset),[]>, Sched<[]>;
    def LDR_ZZZZXI : Pseudo<(outs ZZZZ_b_strided_and_contiguous:$Zd), (ins GPR64sp:$sp, simm4s1:$offset),[]>, Sched<[]>;
    def LDR_PPXI   : Pseudo<(outs PPR2:$pp), (ins GPR64sp:$sp, simm4s1:$offset),[]>, Sched<[]>;
  }
  let mayStore = 1, hasSideEffects = 0 in {
    def STR_ZZXI   : Pseudo<(outs), (ins   ZZ_b_strided_and_contiguous:$Zs, GPR64sp:$sp, simm4s1:$offset),[]>, Sched<[]>;
    def STR_ZZZXI  : Pseudo<(outs), (ins  ZZZ_b:$Zs, GPR64sp:$sp, simm4s1:$offset),[]>, Sched<[]>;
    def STR_ZZZZXI : Pseudo<(outs), (ins ZZZZ_b_strided_and_contiguous:$Zs, GPR64sp:$sp, simm4s1:$offset),[]>, Sched<[]>;
    def STR_PPXI   : Pseudo<(outs), (ins PPR2:$pp, GPR64sp:$sp, simm4s1:$offset),[]>, Sched<[]>;
  }

  let AddedComplexity = 1 in {
    multiclass LD1RPat<ValueType vt, SDPatternOperator operator,
                  Instruction load, Instruction ptrue, ValueType index_vt, ComplexPattern CP, Operand immtype> {
      def : Pat<(vt (splat_vector (index_vt (operator (CP GPR64:$base, immtype:$offset))))),
                (load (ptrue 31), GPR64:$base, $offset)>;
      def : Pat<(vt (AArch64dup_mt PPR:$pg, (index_vt (operator (CP GPR64:$base, immtype:$offset))), (SVEDup0Undef))),
                (load $pg, GPR64:$base, $offset)>;
    }
  }

  // LDR1 of 8-bit data
  defm : LD1RPat<nxv16i8, extloadi8,  LD1RB_IMM,    PTRUE_B, i32, am_indexed8_6b, uimm6s1>;
  defm : LD1RPat<nxv8i16, zextloadi8, LD1RB_H_IMM,  PTRUE_H, i32, am_indexed8_6b, uimm6s1>;
  defm : LD1RPat<nxv4i32, zextloadi8, LD1RB_S_IMM,  PTRUE_S, i32, am_indexed8_6b, uimm6s1>;
  defm : LD1RPat<nxv2i64, zextloadi8, LD1RB_D_IMM,  PTRUE_D, i64, am_indexed8_6b, uimm6s1>;
  defm : LD1RPat<nxv8i16, sextloadi8, LD1RSB_H_IMM, PTRUE_H, i32, am_indexed8_6b, uimm6s1>;
  defm : LD1RPat<nxv4i32, sextloadi8, LD1RSB_S_IMM, PTRUE_S, i32, am_indexed8_6b, uimm6s1>;
  defm : LD1RPat<nxv2i64, sextloadi8, LD1RSB_D_IMM, PTRUE_D, i64, am_indexed8_6b, uimm6s1>;

  // LDR1 of 16-bit data
  defm : LD1RPat<nxv8i16, extloadi16,  LD1RH_IMM,    PTRUE_H, i32, am_indexed16_6b, uimm6s2>;
  defm : LD1RPat<nxv4i32, zextloadi16, LD1RH_S_IMM,  PTRUE_S, i32, am_indexed16_6b, uimm6s2>;
  defm : LD1RPat<nxv2i64, zextloadi16, LD1RH_D_IMM,  PTRUE_D, i64, am_indexed16_6b, uimm6s2>;
  defm : LD1RPat<nxv4i32, sextloadi16, LD1RSH_S_IMM, PTRUE_S, i32, am_indexed16_6b, uimm6s2>;
  defm : LD1RPat<nxv2i64, sextloadi16, LD1RSH_D_IMM, PTRUE_D, i64, am_indexed16_6b, uimm6s2>;

  // LDR1 of 32-bit data
  defm : LD1RPat<nxv4i32, load,        LD1RW_IMM,   PTRUE_S, i32, am_indexed32_6b, uimm6s4>;
  defm : LD1RPat<nxv2i64, zextloadi32, LD1RW_D_IMM, PTRUE_D, i64, am_indexed32_6b, uimm6s4>;
  defm : LD1RPat<nxv2i64, sextloadi32, LD1RSW_IMM,  PTRUE_D, i64, am_indexed32_6b, uimm6s4>;

  // LDR1 of 64-bit data
  defm : LD1RPat<nxv2i64, load, LD1RD_IMM, PTRUE_D, i64, am_indexed64_6b, uimm6s8>;

  let Predicates = [HasSVEorSME, UseSVEFPLD1R] in {
    // LD1R of FP data
    defm : LD1RPat<nxv8f16, load, LD1RH_IMM,   PTRUE_H, f16, am_indexed16_6b, uimm6s2>;
    defm : LD1RPat<nxv4f16, load, LD1RH_S_IMM, PTRUE_S, f16, am_indexed16_6b, uimm6s2>;
    defm : LD1RPat<nxv2f16, load, LD1RH_D_IMM, PTRUE_D, f16, am_indexed16_6b, uimm6s2>;
    defm : LD1RPat<nxv4f32, load, LD1RW_IMM,   PTRUE_S, f32, am_indexed32_6b, uimm6s4>;
    defm : LD1RPat<nxv2f32, load, LD1RW_D_IMM, PTRUE_D, f32, am_indexed32_6b, uimm6s4>;
    defm : LD1RPat<nxv2f64, load, LD1RD_IMM,   PTRUE_D, f64, am_indexed64_6b, uimm6s8>;
  }

// LD1R of 128-bit masked data
  multiclass ld1rq_pat<ValueType vt1, SDPatternOperator op, Instruction load_instr, ComplexPattern AddrCP>{
    def : Pat<(vt1 (AArch64ld1rq_z PPR:$gp, GPR64:$base)),
          (!cast<Instruction>(load_instr # _IMM) $gp, $base, (i64 0))>;
    let AddedComplexity = 2 in {
      def : Pat<(vt1 (op PPR:$gp, (add GPR64:$base, (i64 simm4s16:$imm)))),
            (!cast<Instruction>(load_instr # _IMM) $gp, $base, simm4s16:$imm)>;
    }
    def : Pat<(vt1 (op PPR:$gp, (AddrCP GPR64:$base, GPR64:$idx))),
          (load_instr $gp, $base, $idx)>;
  }

  defm : ld1rq_pat<nxv16i8, AArch64ld1rq_z, LD1RQ_B, am_sve_regreg_lsl0>;
  defm : ld1rq_pat<nxv8i16, AArch64ld1rq_z, LD1RQ_H, am_sve_regreg_lsl1>;
  defm : ld1rq_pat<nxv4i32, AArch64ld1rq_z, LD1RQ_W, am_sve_regreg_lsl2>;
  defm : ld1rq_pat<nxv2i64, AArch64ld1rq_z, LD1RQ_D, am_sve_regreg_lsl3>;

  def : Pat<(sext_inreg nxv2i64:$Zs, nxv2i32), (SXTW_ZPmZ_D_UNDEF (IMPLICIT_DEF), (PTRUE_D 31), ZPR:$Zs)>;
  def : Pat<(sext_inreg nxv2i64:$Zs, nxv2i16), (SXTH_ZPmZ_D_UNDEF (IMPLICIT_DEF), (PTRUE_D 31), ZPR:$Zs)>;
  def : Pat<(sext_inreg nxv2i64:$Zs, nxv2i8),  (SXTB_ZPmZ_D_UNDEF (IMPLICIT_DEF), (PTRUE_D 31), ZPR:$Zs)>;
  def : Pat<(sext_inreg nxv4i32:$Zs, nxv4i16), (SXTH_ZPmZ_S_UNDEF (IMPLICIT_DEF), (PTRUE_S 31), ZPR:$Zs)>;
  def : Pat<(sext_inreg nxv4i32:$Zs, nxv4i8),  (SXTB_ZPmZ_S_UNDEF (IMPLICIT_DEF), (PTRUE_S 31), ZPR:$Zs)>;
  def : Pat<(sext_inreg nxv8i16:$Zs, nxv8i8),  (SXTB_ZPmZ_H_UNDEF (IMPLICIT_DEF), (PTRUE_H 31), ZPR:$Zs)>;

  // General case that we ideally never want to match.
  def : Pat<(vscale GPR64:$scale), (MADDXrrr (UBFMXri (RDVLI_XI 1), 4, 63), $scale, XZR)>;

  let AddedComplexity = 5 in {
    def : Pat<(vscale (i64 1)), (UBFMXri (RDVLI_XI 1), 4, 63)>;
    def : Pat<(vscale (i64 -1)), (SBFMXri (RDVLI_XI -1), 4, 63)>;

    def : Pat<(vscale (sve_rdvl_imm i32:$imm)), (RDVLI_XI $imm)>;
    def : Pat<(vscale (sve_cnth_imm i32:$imm)), (CNTH_XPiI 31, $imm)>;
    def : Pat<(vscale (sve_cntw_imm i32:$imm)), (CNTW_XPiI 31, $imm)>;
    def : Pat<(vscale (sve_cntd_imm i32:$imm)), (CNTD_XPiI 31, $imm)>;

    def : Pat<(vscale (sve_cnth_imm_neg i32:$imm)), (SUBXrs XZR, (CNTH_XPiI 31, $imm), 0)>;
    def : Pat<(vscale (sve_cntw_imm_neg i32:$imm)), (SUBXrs XZR, (CNTW_XPiI 31, $imm), 0)>;
    def : Pat<(vscale (sve_cntd_imm_neg i32:$imm)), (SUBXrs XZR, (CNTD_XPiI 31, $imm), 0)>;
  }

  // Add NoUseScalarIncVL to avoid affecting for patterns with UseScalarIncVL
  let Predicates = [HasSVEorSME, NoUseScalarIncVL] in {
    def : Pat<(add GPR64:$op, (vscale (sve_cnth_imm_neg i32:$imm))),
              (SUBXrs GPR64:$op, (CNTH_XPiI 31, $imm), 0)>;
    def : Pat<(add GPR64:$op, (vscale (sve_cntw_imm_neg i32:$imm))),
              (SUBXrs GPR64:$op, (CNTW_XPiI 31, $imm), 0)>;
    def : Pat<(add GPR64:$op, (vscale (sve_cntd_imm_neg i32:$imm))),
              (SUBXrs GPR64:$op, (CNTD_XPiI 31, $imm), 0)>;

    def : Pat<(add GPR32:$op, (i32 (trunc (vscale (sve_cnth_imm_neg i32:$imm))))),
              (SUBSWrr GPR32:$op, (EXTRACT_SUBREG (CNTH_XPiI 31, $imm), sub_32))>;
    def : Pat<(add GPR32:$op, (i32 (trunc (vscale (sve_cntw_imm_neg i32:$imm))))),
              (SUBSWrr GPR32:$op, (EXTRACT_SUBREG (CNTW_XPiI 31, $imm), sub_32))>;
    def : Pat<(add GPR32:$op, (i32 (trunc (vscale (sve_cntd_imm_neg i32:$imm))))),
              (SUBSWrr GPR32:$op, (EXTRACT_SUBREG (CNTD_XPiI 31, $imm), sub_32))>;
  }

  let AddedComplexity = 5 in {
    def : Pat<(nxv8i16 (add ZPR:$op, (nxv8i16 (splat_vector (i32 (trunc (vscale (sve_cnth_imm i32:$imm)))))))),
              (INCH_ZPiI ZPR:$op, 31, $imm)>;
    def : Pat<(nxv4i32 (add ZPR:$op, (nxv4i32 (splat_vector (i32 (trunc (vscale (sve_cntw_imm i32:$imm)))))))),
              (INCW_ZPiI ZPR:$op, 31, $imm)>;
    def : Pat<(nxv2i64 (add ZPR:$op, (nxv2i64 (splat_vector (i64 (vscale (sve_cntd_imm i32:$imm))))))),
              (INCD_ZPiI ZPR:$op, 31, $imm)>;

    def : Pat<(nxv8i16 (sub ZPR:$op, (nxv8i16 (splat_vector (i32 (trunc (vscale (sve_cnth_imm i32:$imm)))))))),
              (DECH_ZPiI ZPR:$op, 31, $imm)>;
    def : Pat<(nxv4i32 (sub ZPR:$op, (nxv4i32 (splat_vector (i32 (trunc (vscale (sve_cntw_imm i32:$imm)))))))),
              (DECW_ZPiI ZPR:$op, 31, $imm)>;
    def : Pat<(nxv2i64 (sub ZPR:$op, (nxv2i64 (splat_vector (i64 (vscale (sve_cntd_imm i32:$imm))))))),
              (DECD_ZPiI ZPR:$op, 31, $imm)>;
  }

  let Predicates = [HasSVEorSME, UseScalarIncVL], AddedComplexity = 5 in {
    def : Pat<(add GPR64:$op, (vscale (sve_rdvl_imm i32:$imm))),
              (ADDVL_XXI GPR64:$op, $imm)>;

    def : Pat<(add GPR32:$op, (i32 (trunc (vscale (sve_rdvl_imm i32:$imm))))),
              (EXTRACT_SUBREG (ADDVL_XXI (INSERT_SUBREG (IMPLICIT_DEF),
                                        GPR32:$op, sub_32), $imm),
                              sub_32)>;

    def : Pat<(add GPR64:$op, (vscale (sve_cnth_imm i32:$imm))),
              (INCH_XPiI GPR64:$op, 31, $imm)>;
    def : Pat<(add GPR64:$op, (vscale (sve_cntw_imm i32:$imm))),
              (INCW_XPiI GPR64:$op, 31, $imm)>;
    def : Pat<(add GPR64:$op, (vscale (sve_cntd_imm i32:$imm))),
              (INCD_XPiI GPR64:$op, 31, $imm)>;

    def : Pat<(add GPR64:$op, (vscale (sve_cnth_imm_neg i32:$imm))),
              (DECH_XPiI GPR64:$op, 31, $imm)>;
    def : Pat<(add GPR64:$op, (vscale (sve_cntw_imm_neg i32:$imm))),
              (DECW_XPiI GPR64:$op, 31, $imm)>;
    def : Pat<(add GPR64:$op, (vscale (sve_cntd_imm_neg i32:$imm))),
              (DECD_XPiI GPR64:$op, 31, $imm)>;

    def : Pat<(add GPR32:$op, (i32 (trunc (vscale (sve_cnth_imm i32:$imm))))),
              (EXTRACT_SUBREG (INCH_XPiI (INSERT_SUBREG (IMPLICIT_DEF),
                                          GPR32:$op, sub_32), 31, $imm),
                               sub_32)>;
    def : Pat<(add GPR32:$op, (i32 (trunc (vscale (sve_cntw_imm i32:$imm))))),
              (EXTRACT_SUBREG (INCW_XPiI (INSERT_SUBREG (IMPLICIT_DEF),
                                          GPR32:$op, sub_32), 31, $imm),
                               sub_32)>;
    def : Pat<(add GPR32:$op, (i32 (trunc (vscale (sve_cntd_imm i32:$imm))))),
              (EXTRACT_SUBREG (INCD_XPiI (INSERT_SUBREG (IMPLICIT_DEF),
                                          GPR32:$op, sub_32), 31, $imm),
                               sub_32)>;

    def : Pat<(add GPR32:$op, (i32 (trunc (vscale (sve_cnth_imm_neg i32:$imm))))),
              (EXTRACT_SUBREG (DECH_XPiI (INSERT_SUBREG (IMPLICIT_DEF),
                                          GPR32:$op, sub_32), 31, $imm),
                               sub_32)>;
    def : Pat<(add GPR32:$op, (i32 (trunc (vscale (sve_cntw_imm_neg i32:$imm))))),
              (EXTRACT_SUBREG (DECW_XPiI (INSERT_SUBREG (IMPLICIT_DEF),
                                          GPR32:$op, sub_32), 31, $imm),
                               sub_32)>;
    def : Pat<(add GPR32:$op, (i32 (trunc (vscale (sve_cntd_imm_neg i32:$imm))))),
              (EXTRACT_SUBREG (DECD_XPiI (INSERT_SUBREG (IMPLICIT_DEF),
                                          GPR32:$op, sub_32), 31, $imm),
                               sub_32)>;
  }

  // FIXME: BigEndian requires an additional REV instruction to satisfy the
  // constraint that none of the bits change when stored to memory as one
  // type, and reloaded as another type.
  let Predicates = [IsLE] in {
    def : Pat<(nxv16i8 (bitconvert nxv8i16:$src)), (nxv16i8 ZPR:$src)>;
    def : Pat<(nxv16i8 (bitconvert nxv4i32:$src)), (nxv16i8 ZPR:$src)>;
    def : Pat<(nxv16i8 (bitconvert nxv2i64:$src)), (nxv16i8 ZPR:$src)>;
    def : Pat<(nxv16i8 (bitconvert nxv8f16:$src)), (nxv16i8 ZPR:$src)>;
    def : Pat<(nxv16i8 (bitconvert nxv4f32:$src)), (nxv16i8 ZPR:$src)>;
    def : Pat<(nxv16i8 (bitconvert nxv2f64:$src)), (nxv16i8 ZPR:$src)>;

    def : Pat<(nxv8i16 (bitconvert nxv16i8:$src)), (nxv8i16 ZPR:$src)>;
    def : Pat<(nxv8i16 (bitconvert nxv4i32:$src)), (nxv8i16 ZPR:$src)>;
    def : Pat<(nxv8i16 (bitconvert nxv2i64:$src)), (nxv8i16 ZPR:$src)>;
    def : Pat<(nxv8i16 (bitconvert nxv8f16:$src)), (nxv8i16 ZPR:$src)>;
    def : Pat<(nxv8i16 (bitconvert nxv4f32:$src)), (nxv8i16 ZPR:$src)>;
    def : Pat<(nxv8i16 (bitconvert nxv2f64:$src)), (nxv8i16 ZPR:$src)>;

    def : Pat<(nxv4i32 (bitconvert nxv16i8:$src)), (nxv4i32 ZPR:$src)>;
    def : Pat<(nxv4i32 (bitconvert nxv8i16:$src)), (nxv4i32 ZPR:$src)>;
    def : Pat<(nxv4i32 (bitconvert nxv2i64:$src)), (nxv4i32 ZPR:$src)>;
    def : Pat<(nxv4i32 (bitconvert nxv8f16:$src)), (nxv4i32 ZPR:$src)>;
    def : Pat<(nxv4i32 (bitconvert nxv4f32:$src)), (nxv4i32 ZPR:$src)>;
    def : Pat<(nxv4i32 (bitconvert nxv2f64:$src)), (nxv4i32 ZPR:$src)>;

    def : Pat<(nxv2i64 (bitconvert nxv16i8:$src)), (nxv2i64 ZPR:$src)>;
    def : Pat<(nxv2i64 (bitconvert nxv8i16:$src)), (nxv2i64 ZPR:$src)>;
    def : Pat<(nxv2i64 (bitconvert nxv4i32:$src)), (nxv2i64 ZPR:$src)>;
    def : Pat<(nxv2i64 (bitconvert nxv8f16:$src)), (nxv2i64 ZPR:$src)>;
    def : Pat<(nxv2i64 (bitconvert nxv4f32:$src)), (nxv2i64 ZPR:$src)>;
    def : Pat<(nxv2i64 (bitconvert nxv2f64:$src)), (nxv2i64 ZPR:$src)>;

    def : Pat<(nxv8f16 (bitconvert nxv16i8:$src)), (nxv8f16 ZPR:$src)>;
    def : Pat<(nxv8f16 (bitconvert nxv8i16:$src)), (nxv8f16 ZPR:$src)>;
    def : Pat<(nxv8f16 (bitconvert nxv4i32:$src)), (nxv8f16 ZPR:$src)>;
    def : Pat<(nxv8f16 (bitconvert nxv2i64:$src)), (nxv8f16 ZPR:$src)>;
    def : Pat<(nxv8f16 (bitconvert nxv4f32:$src)), (nxv8f16 ZPR:$src)>;
    def : Pat<(nxv8f16 (bitconvert nxv2f64:$src)), (nxv8f16 ZPR:$src)>;

    def : Pat<(nxv4f32 (bitconvert nxv16i8:$src)), (nxv4f32 ZPR:$src)>;
    def : Pat<(nxv4f32 (bitconvert nxv8i16:$src)), (nxv4f32 ZPR:$src)>;
    def : Pat<(nxv4f32 (bitconvert nxv4i32:$src)), (nxv4f32 ZPR:$src)>;
    def : Pat<(nxv4f32 (bitconvert nxv2i64:$src)), (nxv4f32 ZPR:$src)>;
    def : Pat<(nxv4f32 (bitconvert nxv8f16:$src)), (nxv4f32 ZPR:$src)>;
    def : Pat<(nxv4f32 (bitconvert nxv2f64:$src)), (nxv4f32 ZPR:$src)>;

    def : Pat<(nxv2f64 (bitconvert nxv16i8:$src)), (nxv2f64 ZPR:$src)>;
    def : Pat<(nxv2f64 (bitconvert nxv8i16:$src)), (nxv2f64 ZPR:$src)>;
    def : Pat<(nxv2f64 (bitconvert nxv4i32:$src)), (nxv2f64 ZPR:$src)>;
    def : Pat<(nxv2f64 (bitconvert nxv2i64:$src)), (nxv2f64 ZPR:$src)>;
    def : Pat<(nxv2f64 (bitconvert nxv8f16:$src)), (nxv2f64 ZPR:$src)>;
    def : Pat<(nxv2f64 (bitconvert nxv4f32:$src)), (nxv2f64 ZPR:$src)>;

    def : Pat<(nxv8bf16 (bitconvert nxv16i8:$src)), (nxv8bf16 ZPR:$src)>;
    def : Pat<(nxv8bf16 (bitconvert nxv8i16:$src)), (nxv8bf16 ZPR:$src)>;
    def : Pat<(nxv8bf16 (bitconvert nxv4i32:$src)), (nxv8bf16 ZPR:$src)>;
    def : Pat<(nxv8bf16 (bitconvert nxv2i64:$src)), (nxv8bf16 ZPR:$src)>;
    def : Pat<(nxv8bf16 (bitconvert nxv8f16:$src)), (nxv8bf16 ZPR:$src)>;
    def : Pat<(nxv8bf16 (bitconvert nxv4f32:$src)), (nxv8bf16 ZPR:$src)>;
    def : Pat<(nxv8bf16 (bitconvert nxv2f64:$src)), (nxv8bf16 ZPR:$src)>;

    def : Pat<(nxv16i8 (bitconvert nxv8bf16:$src)), (nxv16i8 ZPR:$src)>;
    def : Pat<(nxv8i16 (bitconvert nxv8bf16:$src)), (nxv8i16 ZPR:$src)>;
    def : Pat<(nxv4i32 (bitconvert nxv8bf16:$src)), (nxv4i32 ZPR:$src)>;
    def : Pat<(nxv2i64 (bitconvert nxv8bf16:$src)), (nxv2i64 ZPR:$src)>;
    def : Pat<(nxv8f16 (bitconvert nxv8bf16:$src)), (nxv8f16 ZPR:$src)>;
    def : Pat<(nxv4f32 (bitconvert nxv8bf16:$src)), (nxv4f32 ZPR:$src)>;
    def : Pat<(nxv2f64 (bitconvert nxv8bf16:$src)), (nxv2f64 ZPR:$src)>;

    def : Pat<(nxv16i1 (bitconvert aarch64svcount:$src)), (nxv16i1 PPR:$src)>;
    def : Pat<(aarch64svcount (bitconvert nxv16i1:$src)), (aarch64svcount PNR:$src)>;
  }

  // These allow casting from/to unpacked predicate types.
  def : Pat<(nxv16i1 (reinterpret_cast nxv16i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv16i1 (reinterpret_cast nxv8i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv16i1 (reinterpret_cast nxv4i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv16i1 (reinterpret_cast nxv2i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv16i1 (reinterpret_cast nxv1i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv8i1 (reinterpret_cast nxv16i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv8i1 (reinterpret_cast  nxv4i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv8i1 (reinterpret_cast  nxv2i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv8i1 (reinterpret_cast  nxv1i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv4i1 (reinterpret_cast nxv16i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv4i1 (reinterpret_cast  nxv8i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv4i1 (reinterpret_cast  nxv2i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv4i1 (reinterpret_cast  nxv1i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv2i1 (reinterpret_cast nxv16i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv2i1 (reinterpret_cast  nxv8i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv2i1 (reinterpret_cast  nxv4i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv2i1 (reinterpret_cast  nxv1i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv1i1 (reinterpret_cast nxv16i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv1i1 (reinterpret_cast  nxv8i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv1i1 (reinterpret_cast  nxv4i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;
  def : Pat<(nxv1i1 (reinterpret_cast  nxv2i1:$src)), (COPY_TO_REGCLASS PPR:$src, PPR)>;

  // These allow casting from/to unpacked floating-point types.
  def : Pat<(nxv2f16 (reinterpret_cast nxv8f16:$src)), (COPY_TO_REGCLASS ZPR:$src, ZPR)>;
  def : Pat<(nxv8f16 (reinterpret_cast nxv2f16:$src)), (COPY_TO_REGCLASS ZPR:$src, ZPR)>;
  def : Pat<(nxv4f16 (reinterpret_cast nxv8f16:$src)), (COPY_TO_REGCLASS ZPR:$src, ZPR)>;
  def : Pat<(nxv8f16 (reinterpret_cast nxv4f16:$src)), (COPY_TO_REGCLASS ZPR:$src, ZPR)>;
  def : Pat<(nxv2f32 (reinterpret_cast nxv4f32:$src)), (COPY_TO_REGCLASS ZPR:$src, ZPR)>;
  def : Pat<(nxv4f32 (reinterpret_cast nxv2f32:$src)), (COPY_TO_REGCLASS ZPR:$src, ZPR)>;
  def : Pat<(nxv2bf16 (reinterpret_cast nxv8bf16:$src)), (COPY_TO_REGCLASS ZPR:$src, ZPR)>;
  def : Pat<(nxv8bf16 (reinterpret_cast nxv2bf16:$src)), (COPY_TO_REGCLASS ZPR:$src, ZPR)>;
  def : Pat<(nxv4bf16 (reinterpret_cast nxv8bf16:$src)), (COPY_TO_REGCLASS ZPR:$src, ZPR)>;
  def : Pat<(nxv8bf16 (reinterpret_cast nxv4bf16:$src)), (COPY_TO_REGCLASS ZPR:$src, ZPR)>;

  def : Pat<(nxv16i1 (and PPR:$Ps1, PPR:$Ps2)),
            (AND_PPzPP (PTRUE_B 31), PPR:$Ps1, PPR:$Ps2)>;
  def : Pat<(nxv8i1 (and PPR:$Ps1, PPR:$Ps2)),
            (AND_PPzPP (PTRUE_H 31), PPR:$Ps1, PPR:$Ps2)>;
  def : Pat<(nxv4i1 (and PPR:$Ps1, PPR:$Ps2)),
            (AND_PPzPP (PTRUE_S 31), PPR:$Ps1, PPR:$Ps2)>;
  def : Pat<(nxv2i1 (and PPR:$Ps1, PPR:$Ps2)),
            (AND_PPzPP (PTRUE_D 31), PPR:$Ps1, PPR:$Ps2)>;
  // Emulate .Q operation using a PTRUE_D when the other lanes don't matter.
  def : Pat<(nxv1i1 (and PPR:$Ps1, PPR:$Ps2)),
            (AND_PPzPP (PTRUE_D 31), PPR:$Ps1, PPR:$Ps2)>;

  // Add more complex addressing modes here as required
  multiclass pred_load<ValueType Ty, ValueType PredTy, SDPatternOperator Load,
                       Instruction RegRegInst, Instruction RegImmInst, ComplexPattern AddrCP> {
    let AddedComplexity = 1 in {
      def _reg_reg_z : Pat<(Ty (Load (AddrCP GPR64:$base, GPR64:$offset), (PredTy PPR:$gp), (SVEDup0Undef))),
                           (RegRegInst PPR:$gp, GPR64:$base, GPR64:$offset)>;
    }
    let AddedComplexity = 2 in {
      def _reg_imm_z : Pat<(Ty (Load (am_sve_indexed_s4 GPR64sp:$base, simm4s1:$offset), (PredTy PPR:$gp), (SVEDup0Undef))),
                           (RegImmInst PPR:$gp, GPR64:$base, simm4s1:$offset)>;
    }
    def _default_z : Pat<(Ty (Load  GPR64:$base, (PredTy PPR:$gp), (SVEDup0Undef))),
                         (RegImmInst PPR:$gp, GPR64:$base, (i64 0))>;
  }

  // 2-element contiguous loads
  defm : pred_load<nxv2i64,  nxv2i1, azext_masked_load_i8,  LD1B_D,  LD1B_D_IMM,  am_sve_regreg_lsl0>;
  defm : pred_load<nxv2i64,  nxv2i1, sext_masked_load_i8,   LD1SB_D, LD1SB_D_IMM, am_sve_regreg_lsl0>;
  defm : pred_load<nxv2i64,  nxv2i1, azext_masked_load_i16, LD1H_D,  LD1H_D_IMM,  am_sve_regreg_lsl1>;
  defm : pred_load<nxv2i64,  nxv2i1, sext_masked_load_i16,  LD1SH_D, LD1SH_D_IMM, am_sve_regreg_lsl1>;
  defm : pred_load<nxv2i64,  nxv2i1, azext_masked_load_i32, LD1W_D,  LD1W_D_IMM,  am_sve_regreg_lsl2>;
  defm : pred_load<nxv2i64,  nxv2i1, sext_masked_load_i32,  LD1SW_D, LD1SW_D_IMM, am_sve_regreg_lsl2>;
  defm : pred_load<nxv2i64,  nxv2i1, nonext_masked_load,    LD1D,    LD1D_IMM,    am_sve_regreg_lsl3>;
  defm : pred_load<nxv2f16,  nxv2i1, nonext_masked_load,    LD1H_D,  LD1H_D_IMM,  am_sve_regreg_lsl1>;
  defm : pred_load<nxv2bf16, nxv2i1, nonext_masked_load,    LD1H_D,  LD1H_D_IMM,  am_sve_regreg_lsl1>;
  defm : pred_load<nxv2f32,  nxv2i1, nonext_masked_load,    LD1W_D,  LD1W_D_IMM,  am_sve_regreg_lsl2>;
  defm : pred_load<nxv2f64,  nxv2i1, nonext_masked_load,    LD1D,    LD1D_IMM,    am_sve_regreg_lsl3>;

  // 4-element contiguous loads
  defm : pred_load<nxv4i32,  nxv4i1, azext_masked_load_i8,  LD1B_S,  LD1B_S_IMM,  am_sve_regreg_lsl0>;
  defm : pred_load<nxv4i32,  nxv4i1, sext_masked_load_i8,   LD1SB_S, LD1SB_S_IMM, am_sve_regreg_lsl0>;
  defm : pred_load<nxv4i32,  nxv4i1, azext_masked_load_i16, LD1H_S,  LD1H_S_IMM,  am_sve_regreg_lsl1>;
  defm : pred_load<nxv4i32,  nxv4i1, sext_masked_load_i16,  LD1SH_S, LD1SH_S_IMM, am_sve_regreg_lsl1>;
  defm : pred_load<nxv4i32,  nxv4i1, nonext_masked_load,    LD1W,    LD1W_IMM,    am_sve_regreg_lsl2>;
  defm : pred_load<nxv4f16,  nxv4i1, nonext_masked_load,    LD1H_S,  LD1H_S_IMM,  am_sve_regreg_lsl1>;
  defm : pred_load<nxv4bf16, nxv4i1, nonext_masked_load,    LD1H_S,  LD1H_S_IMM,  am_sve_regreg_lsl1>;
  defm : pred_load<nxv4f32,  nxv4i1, nonext_masked_load,    LD1W,    LD1W_IMM,    am_sve_regreg_lsl2>;

  // 8-element contiguous loads
  defm : pred_load<nxv8i16,  nxv8i1, azext_masked_load_i8, LD1B_H,  LD1B_H_IMM,  am_sve_regreg_lsl0>;
  defm : pred_load<nxv8i16,  nxv8i1, sext_masked_load_i8,  LD1SB_H, LD1SB_H_IMM, am_sve_regreg_lsl0>;
  defm : pred_load<nxv8i16,  nxv8i1, nonext_masked_load,   LD1H,    LD1H_IMM,    am_sve_regreg_lsl1>;
  defm : pred_load<nxv8f16,  nxv8i1, nonext_masked_load,   LD1H,    LD1H_IMM,    am_sve_regreg_lsl1>;
  defm : pred_load<nxv8bf16, nxv8i1, nonext_masked_load,   LD1H,    LD1H_IMM,    am_sve_regreg_lsl1>;

  // 16-element contiguous loads
  defm : pred_load<nxv16i8, nxv16i1, nonext_masked_load, LD1B, LD1B_IMM, am_sve_regreg_lsl0>;

  multiclass pred_store<ValueType Ty, ValueType PredTy, SDPatternOperator Store,
                        Instruction RegRegInst, Instruction RegImmInst, ComplexPattern AddrCP> {
    let AddedComplexity = 1 in {
      def _reg_reg : Pat<(Store Ty:$vec, (AddrCP GPR64:$base, GPR64:$offset), PredTy:$gp),
                         (RegRegInst ZPR:$vec, PPR:$gp, GPR64:$base, GPR64:$offset)>;
    }
    let AddedComplexity = 2 in {
      def _reg_imm : Pat<(Store Ty:$vec, (am_sve_indexed_s4 GPR64sp:$base, simm4s1:$offset), PredTy:$gp),
                         (RegImmInst ZPR:$vec, PPR:$gp, GPR64:$base, simm4s1:$offset)>;
    }
    def _default : Pat<(Store Ty:$vec, GPR64:$base, PredTy:$gp),
                       (RegImmInst ZPR:$vec, PPR:$gp, GPR64:$base, (i64 0))>;
  }

  // 2-element contiguous stores
  defm : pred_store<nxv2i64,  nxv2i1, trunc_masked_store_i8,  ST1B_D, ST1B_D_IMM, am_sve_regreg_lsl0>;
  defm : pred_store<nxv2i64,  nxv2i1, trunc_masked_store_i16, ST1H_D, ST1H_D_IMM, am_sve_regreg_lsl1>;
  defm : pred_store<nxv2i64,  nxv2i1, trunc_masked_store_i32, ST1W_D, ST1W_D_IMM, am_sve_regreg_lsl2>;
  defm : pred_store<nxv2i64,  nxv2i1, nontrunc_masked_store,  ST1D,   ST1D_IMM,   am_sve_regreg_lsl3>;
  defm : pred_store<nxv2f16,  nxv2i1, nontrunc_masked_store,  ST1H_D, ST1H_D_IMM, am_sve_regreg_lsl1>;
  defm : pred_store<nxv2bf16, nxv2i1, nontrunc_masked_store,  ST1H_D, ST1H_D_IMM, am_sve_regreg_lsl1>;
  defm : pred_store<nxv2f32,  nxv2i1, nontrunc_masked_store,  ST1W_D, ST1W_D_IMM, am_sve_regreg_lsl2>;
  defm : pred_store<nxv2f64,  nxv2i1, nontrunc_masked_store,  ST1D,   ST1D_IMM,   am_sve_regreg_lsl3>;

  // 4-element contiguous stores
  defm : pred_store<nxv4i32,  nxv4i1, trunc_masked_store_i8,  ST1B_S, ST1B_S_IMM, am_sve_regreg_lsl0>;
  defm : pred_store<nxv4i32,  nxv4i1, trunc_masked_store_i16, ST1H_S, ST1H_S_IMM, am_sve_regreg_lsl1>;
  defm : pred_store<nxv4i32,  nxv4i1, nontrunc_masked_store,  ST1W,   ST1W_IMM,   am_sve_regreg_lsl2>;
  defm : pred_store<nxv4f16,  nxv4i1, nontrunc_masked_store,  ST1H_S, ST1H_S_IMM, am_sve_regreg_lsl1>;
  defm : pred_store<nxv4bf16, nxv4i1, nontrunc_masked_store,  ST1H_S, ST1H_S_IMM, am_sve_regreg_lsl1>;
  defm : pred_store<nxv4f32,  nxv4i1, nontrunc_masked_store,  ST1W,   ST1W_IMM,   am_sve_regreg_lsl2>;

  // 8-element contiguous stores
  defm : pred_store<nxv8i16,  nxv8i1, trunc_masked_store_i8, ST1B_H, ST1B_H_IMM, am_sve_regreg_lsl0>;
  defm : pred_store<nxv8i16,  nxv8i1, nontrunc_masked_store, ST1H,   ST1H_IMM,   am_sve_regreg_lsl1>;
  defm : pred_store<nxv8f16,  nxv8i1, nontrunc_masked_store, ST1H,   ST1H_IMM,   am_sve_regreg_lsl1>;
  defm : pred_store<nxv8bf16, nxv8i1, nontrunc_masked_store, ST1H,   ST1H_IMM,   am_sve_regreg_lsl1>;

  // 16-element contiguous stores
  defm : pred_store<nxv16i8, nxv16i1, nontrunc_masked_store, ST1B, ST1B_IMM, am_sve_regreg_lsl0>;

  defm : pred_load<nxv16i8, nxv16i1, non_temporal_load, LDNT1B_ZRR, LDNT1B_ZRI, am_sve_regreg_lsl0>;
  defm : pred_load<nxv8i16, nxv8i1,  non_temporal_load, LDNT1H_ZRR, LDNT1H_ZRI, am_sve_regreg_lsl1>;
  defm : pred_load<nxv4i32, nxv4i1,  non_temporal_load, LDNT1W_ZRR, LDNT1W_ZRI, am_sve_regreg_lsl2>;
  defm : pred_load<nxv2i64, nxv2i1,  non_temporal_load, LDNT1D_ZRR, LDNT1D_ZRI, am_sve_regreg_lsl3>;

  defm : pred_store<nxv16i8, nxv16i1, non_temporal_store, STNT1B_ZRR, STNT1B_ZRI, am_sve_regreg_lsl0>;
  defm : pred_store<nxv8i16, nxv8i1,  non_temporal_store, STNT1H_ZRR, STNT1H_ZRI, am_sve_regreg_lsl1>;
  defm : pred_store<nxv4i32, nxv4i1,  non_temporal_store, STNT1W_ZRR, STNT1W_ZRI, am_sve_regreg_lsl2>;
  defm : pred_store<nxv2i64, nxv2i1,  non_temporal_store, STNT1D_ZRR, STNT1D_ZRI, am_sve_regreg_lsl3>;

  multiclass unpred_store<PatFrag Store, ValueType Ty, Instruction RegRegInst,
                          Instruction RegImmInst, Instruction PTrue,
                          ComplexPattern AddrCP> {
    let AddedComplexity = 1 in {
      def _reg : Pat<(Store Ty:$val, (AddrCP GPR64sp:$base, GPR64:$offset)),
                     (RegRegInst ZPR:$val, (PTrue 31), GPR64sp:$base, GPR64:$offset)>;
    }
    let AddedComplexity = 2 in {
      def _imm : Pat<(Store Ty:$val, (am_sve_indexed_s4 GPR64sp:$base, simm4s1:$offset)),
                     (RegImmInst ZPR:$val, (PTrue 31), GPR64sp:$base, simm4s1:$offset)>;
    }

    def : Pat<(Store Ty:$val, GPR64:$base),
              (RegImmInst ZPR:$val, (PTrue 31), GPR64:$base, (i64 0))>;
  }

  defm : unpred_store<         store, nxv16i8,    ST1B,   ST1B_IMM, PTRUE_B, am_sve_regreg_lsl0>;
  defm : unpred_store< truncstorevi8, nxv8i16,  ST1B_H, ST1B_H_IMM, PTRUE_H, am_sve_regreg_lsl0>;
  defm : unpred_store< truncstorevi8, nxv4i32,  ST1B_S, ST1B_S_IMM, PTRUE_S, am_sve_regreg_lsl0>;
  defm : unpred_store< truncstorevi8, nxv2i64,  ST1B_D, ST1B_D_IMM, PTRUE_D, am_sve_regreg_lsl0>;
  defm : unpred_store<         store, nxv8i16,    ST1H,   ST1H_IMM, PTRUE_H, am_sve_regreg_lsl1>;
  defm : unpred_store<truncstorevi16, nxv4i32,  ST1H_S, ST1H_S_IMM, PTRUE_S, am_sve_regreg_lsl1>;
  defm : unpred_store<truncstorevi16, nxv2i64,  ST1H_D, ST1H_D_IMM, PTRUE_D, am_sve_regreg_lsl1>;
  defm : unpred_store<         store, nxv4i32,    ST1W,   ST1W_IMM, PTRUE_S, am_sve_regreg_lsl2>;
  defm : unpred_store<truncstorevi32, nxv2i64,  ST1W_D, ST1W_D_IMM, PTRUE_D, am_sve_regreg_lsl2>;
  defm : unpred_store<         store, nxv2i64,    ST1D,   ST1D_IMM, PTRUE_D, am_sve_regreg_lsl3>;
  defm : unpred_store<         store, nxv8f16,    ST1H,   ST1H_IMM, PTRUE_H, am_sve_regreg_lsl1>;
  defm : unpred_store<         store, nxv8bf16,   ST1H,   ST1H_IMM, PTRUE_H, am_sve_regreg_lsl1>;
  defm : unpred_store<         store, nxv4f16,  ST1H_S, ST1H_S_IMM, PTRUE_S, am_sve_regreg_lsl1>;
  defm : unpred_store<         store, nxv4bf16, ST1H_S, ST1H_S_IMM, PTRUE_S, am_sve_regreg_lsl1>;
  defm : unpred_store<         store, nxv2f16,  ST1H_D, ST1H_D_IMM, PTRUE_D, am_sve_regreg_lsl1>;
  defm : unpred_store<         store, nxv2bf16, ST1H_D, ST1H_D_IMM, PTRUE_D, am_sve_regreg_lsl1>;
  defm : unpred_store<         store, nxv4f32,    ST1W,   ST1W_IMM, PTRUE_S, am_sve_regreg_lsl2>;
  defm : unpred_store<         store, nxv2f32,  ST1W_D, ST1W_D_IMM, PTRUE_D, am_sve_regreg_lsl2>;
  defm : unpred_store<         store, nxv2f64,    ST1D,   ST1D_IMM, PTRUE_D, am_sve_regreg_lsl3>;

  multiclass unpred_load<PatFrag Load, ValueType Ty, Instruction RegRegInst,
                         Instruction RegImmInst, Instruction PTrue,
                         ComplexPattern AddrCP> {
    let AddedComplexity = 1 in {
      def _reg: Pat<(Ty (Load  (AddrCP GPR64sp:$base, GPR64:$offset))),
                    (RegRegInst (PTrue 31), GPR64sp:$base, GPR64:$offset)>;
    }
    let AddedComplexity = 2 in {
      def _imm: Pat<(Ty (Load  (am_sve_indexed_s4 GPR64sp:$base, simm4s1:$offset))),
                    (RegImmInst (PTrue 31), GPR64sp:$base, simm4s1:$offset)>;
    }

    def : Pat<(Ty (Load GPR64:$base)),
              (RegImmInst (PTrue 31), GPR64:$base, (i64 0))>;
  }

  defm : unpred_load<        load, nxv16i8,    LD1B,    LD1B_IMM, PTRUE_B, am_sve_regreg_lsl0>;
  defm : unpred_load< zextloadvi8, nxv8i16,  LD1B_H,  LD1B_H_IMM, PTRUE_H, am_sve_regreg_lsl0>;
  defm : unpred_load< zextloadvi8, nxv4i32,  LD1B_S,  LD1B_S_IMM, PTRUE_S, am_sve_regreg_lsl0>;
  defm : unpred_load< zextloadvi8, nxv2i64,  LD1B_D,  LD1B_D_IMM, PTRUE_D, am_sve_regreg_lsl0>;
  defm : unpred_load<  extloadvi8, nxv8i16,  LD1B_H,  LD1B_H_IMM, PTRUE_H, am_sve_regreg_lsl0>;
  defm : unpred_load<  extloadvi8, nxv4i32,  LD1B_S,  LD1B_S_IMM, PTRUE_S, am_sve_regreg_lsl0>;
  defm : unpred_load<  extloadvi8, nxv2i64,  LD1B_D,  LD1B_D_IMM, PTRUE_D, am_sve_regreg_lsl0>;
  defm : unpred_load< sextloadvi8, nxv8i16, LD1SB_H, LD1SB_H_IMM, PTRUE_H, am_sve_regreg_lsl0>;
  defm : unpred_load< sextloadvi8, nxv4i32, LD1SB_S, LD1SB_S_IMM, PTRUE_S, am_sve_regreg_lsl0>;
  defm : unpred_load< sextloadvi8, nxv2i64, LD1SB_D, LD1SB_D_IMM, PTRUE_D, am_sve_regreg_lsl0>;
  defm : unpred_load<        load, nxv8i16,    LD1H,    LD1H_IMM, PTRUE_H, am_sve_regreg_lsl1>;
  defm : unpred_load<zextloadvi16, nxv4i32,  LD1H_S,  LD1H_S_IMM, PTRUE_S, am_sve_regreg_lsl1>;
  defm : unpred_load<zextloadvi16, nxv2i64,  LD1H_D,  LD1H_D_IMM, PTRUE_D, am_sve_regreg_lsl1>;
  defm : unpred_load< extloadvi16, nxv4i32,  LD1H_S,  LD1H_S_IMM, PTRUE_S, am_sve_regreg_lsl1>;
  defm : unpred_load< extloadvi16, nxv2i64,  LD1H_D,  LD1H_D_IMM, PTRUE_D, am_sve_regreg_lsl1>;
  defm : unpred_load<sextloadvi16, nxv4i32, LD1SH_S, LD1SH_S_IMM, PTRUE_S, am_sve_regreg_lsl1>;
  defm : unpred_load<sextloadvi16, nxv2i64, LD1SH_D, LD1SH_D_IMM, PTRUE_D, am_sve_regreg_lsl1>;
  defm : unpred_load<        load, nxv4i32,    LD1W,    LD1W_IMM, PTRUE_S, am_sve_regreg_lsl2>;
  defm : unpred_load<zextloadvi32, nxv2i64,  LD1W_D,  LD1W_D_IMM, PTRUE_D, am_sve_regreg_lsl2>;
  defm : unpred_load< extloadvi32, nxv2i64,  LD1W_D,  LD1W_D_IMM, PTRUE_D, am_sve_regreg_lsl2>;
  defm : unpred_load<sextloadvi32, nxv2i64, LD1SW_D, LD1SW_D_IMM, PTRUE_D, am_sve_regreg_lsl2>;
  defm : unpred_load<        load, nxv2i64,    LD1D,    LD1D_IMM, PTRUE_D, am_sve_regreg_lsl3>;
  defm : unpred_load<        load, nxv8f16,    LD1H,    LD1H_IMM, PTRUE_H, am_sve_regreg_lsl1>;
  defm : unpred_load<        load, nxv8bf16,   LD1H,    LD1H_IMM, PTRUE_H, am_sve_regreg_lsl1>;
  defm : unpred_load<        load, nxv4f16,  LD1H_S,  LD1H_S_IMM, PTRUE_S, am_sve_regreg_lsl1>;
  defm : unpred_load<        load, nxv4bf16, LD1H_S,  LD1H_S_IMM, PTRUE_S, am_sve_regreg_lsl1>;
  defm : unpred_load<        load, nxv2f16,  LD1H_D,  LD1H_D_IMM, PTRUE_D, am_sve_regreg_lsl1>;
  defm : unpred_load<        load, nxv2bf16, LD1H_D,  LD1H_D_IMM, PTRUE_D, am_sve_regreg_lsl1>;
  defm : unpred_load<        load, nxv4f32,    LD1W,    LD1W_IMM, PTRUE_S, am_sve_regreg_lsl2>;
  defm : unpred_load<        load, nxv2f32,  LD1W_D,  LD1W_D_IMM, PTRUE_D, am_sve_regreg_lsl2>;
  defm : unpred_load<        load, nxv2f64,    LD1D,    LD1D_IMM, PTRUE_D, am_sve_regreg_lsl3>;

  // Allow using the reg+reg form of ld1b/st1b for memory accesses with the
  // same width as nxv16i8.  This saves an add in cases where we would
  // otherwise compute the address separately.
  multiclass unpred_loadstore_bitcast<ValueType Ty> {
    let Predicates = [IsLE] in {
      def : Pat<(Ty (load (am_sve_regreg_lsl0 GPR64sp:$base, GPR64:$offset))),
                (LD1B (PTRUE_B 31), GPR64sp:$base, GPR64:$offset)>;
      def : Pat<(store Ty:$val, (am_sve_regreg_lsl0 GPR64sp:$base, GPR64:$offset)),
                (ST1B ZPR:$val, (PTRUE_B 31), GPR64sp:$base, GPR64:$offset)>;
    }
  }
  defm : unpred_loadstore_bitcast<nxv8i16>;
  defm : unpred_loadstore_bitcast<nxv8f16>;
  defm : unpred_loadstore_bitcast<nxv8bf16>;
  defm : unpred_loadstore_bitcast<nxv4f32>;
  defm : unpred_loadstore_bitcast<nxv4i32>;
  defm : unpred_loadstore_bitcast<nxv2i64>;
  defm : unpred_loadstore_bitcast<nxv2f64>;

  multiclass unpred_store_predicate<ValueType Ty, Instruction Store> {
    def _fi : Pat<(store (Ty PPR:$val), (am_sve_fi GPR64sp:$base, simm9:$offset)),
                  (Store PPR:$val, GPR64sp:$base, simm9:$offset)>;

    def _default : Pat<(store (Ty PPR:$Val), GPR64:$base),
                  (Store PPR:$Val, GPR64:$base, (i64 0))>;
  }

  defm Pat_Store_P16 : unpred_store_predicate<nxv16i1, STR_PXI>;

  multiclass unpred_load_predicate<ValueType Ty, Instruction Load> {
    def _fi : Pat<(Ty (load (am_sve_fi GPR64sp:$base, simm9:$offset))),
                  (Load GPR64sp:$base, simm9:$offset)>;

    def _default : Pat<(Ty (load GPR64:$base)),
                  (Load GPR64:$base, (i64 0))>;
  }

  defm Pat_Load_P16 : unpred_load_predicate<nxv16i1, LDR_PXI>;

  multiclass ld1<Instruction RegRegInst, Instruction RegImmInst, ValueType Ty,
                 SDPatternOperator Load, ValueType PredTy, ValueType MemVT, ComplexPattern AddrCP> {
    // reg + reg
    let AddedComplexity = 1 in {
      def : Pat<(Ty (Load  (PredTy PPR:$gp), (AddrCP GPR64:$base, GPR64:$offset), MemVT)),
                (RegRegInst PPR:$gp, GPR64sp:$base, GPR64:$offset)>;
    }

    // scalar + immediate (mul vl)
    let AddedComplexity = 2 in {
      def : Pat<(Ty (Load  (PredTy PPR:$gp), (am_sve_indexed_s4 GPR64sp:$base, simm4s1:$offset), MemVT)),
                (RegImmInst PPR:$gp, GPR64sp:$base, simm4s1:$offset)>;
    }

    // base
    def : Pat<(Ty (Load  (PredTy PPR:$gp), GPR64:$base, MemVT)),
              (RegImmInst PPR:$gp, GPR64sp:$base, (i64 0))>;
  }

  // 2-element contiguous loads
  defm : ld1<LD1B_D,  LD1B_D_IMM,  nxv2i64, AArch64ld1_z,  nxv2i1, nxv2i8,  am_sve_regreg_lsl0>;
  defm : ld1<LD1SB_D, LD1SB_D_IMM, nxv2i64, AArch64ld1s_z, nxv2i1, nxv2i8,  am_sve_regreg_lsl0>;
  defm : ld1<LD1H_D,  LD1H_D_IMM,  nxv2i64, AArch64ld1_z,  nxv2i1, nxv2i16, am_sve_regreg_lsl1>;
  defm : ld1<LD1SH_D, LD1SH_D_IMM, nxv2i64, AArch64ld1s_z, nxv2i1, nxv2i16, am_sve_regreg_lsl1>;
  defm : ld1<LD1W_D,  LD1W_D_IMM,  nxv2i64, AArch64ld1_z,  nxv2i1, nxv2i32, am_sve_regreg_lsl2>;
  defm : ld1<LD1SW_D, LD1SW_D_IMM, nxv2i64, AArch64ld1s_z, nxv2i1, nxv2i32, am_sve_regreg_lsl2>;
  defm : ld1<LD1D,    LD1D_IMM,    nxv2i64, AArch64ld1_z,  nxv2i1, nxv2i64, am_sve_regreg_lsl3>;
  defm : ld1<LD1D,    LD1D_IMM,    nxv2f64, AArch64ld1_z,  nxv2i1, nxv2f64, am_sve_regreg_lsl3>;

  // 4-element contiguous loads
  defm : ld1<LD1B_S,  LD1B_S_IMM,  nxv4i32, AArch64ld1_z,  nxv4i1, nxv4i8,  am_sve_regreg_lsl0>;
  defm : ld1<LD1SB_S, LD1SB_S_IMM, nxv4i32, AArch64ld1s_z, nxv4i1, nxv4i8,  am_sve_regreg_lsl0>;
  defm : ld1<LD1H_S,  LD1H_S_IMM,  nxv4i32, AArch64ld1_z,  nxv4i1, nxv4i16, am_sve_regreg_lsl1>;
  defm : ld1<LD1SH_S, LD1SH_S_IMM, nxv4i32, AArch64ld1s_z, nxv4i1, nxv4i16, am_sve_regreg_lsl1>;
  defm : ld1<LD1W,    LD1W_IMM,    nxv4i32, AArch64ld1_z,  nxv4i1, nxv4i32, am_sve_regreg_lsl2>;
  defm : ld1<LD1W,    LD1W_IMM,    nxv4f32, AArch64ld1_z,  nxv4i1, nxv4f32, am_sve_regreg_lsl2>;

  // 8-element contiguous loads
  defm : ld1<LD1B_H,  LD1B_H_IMM,  nxv8i16,  AArch64ld1_z,  nxv8i1, nxv8i8,   am_sve_regreg_lsl0>;
  defm : ld1<LD1SB_H, LD1SB_H_IMM, nxv8i16,  AArch64ld1s_z, nxv8i1, nxv8i8,   am_sve_regreg_lsl0>;
  defm : ld1<LD1H,    LD1H_IMM,    nxv8i16,  AArch64ld1_z,  nxv8i1, nxv8i16,  am_sve_regreg_lsl1>;
  defm : ld1<LD1H,    LD1H_IMM,    nxv8f16,  AArch64ld1_z,  nxv8i1, nxv8f16,  am_sve_regreg_lsl1>;
  defm : ld1<LD1H,    LD1H_IMM,    nxv8bf16, AArch64ld1_z,  nxv8i1, nxv8bf16, am_sve_regreg_lsl1>;

  // 16-element contiguous loads
  defm : ld1<LD1B, LD1B_IMM, nxv16i8, AArch64ld1_z, nxv16i1, nxv16i8, am_sve_regreg_lsl0>;
} // End HasSVEorSME

let Predicates = [HasSVE] in {
  multiclass ldnf1<Instruction I, ValueType Ty, SDPatternOperator Load, ValueType PredTy, ValueType MemVT> {
    // scalar + immediate (mul vl)
    let AddedComplexity = 1 in {
      def : Pat<(Ty (Load  (PredTy PPR:$gp), (am_sve_indexed_s4 GPR64sp:$base, simm4s1:$offset), MemVT)),
                (I PPR:$gp, GPR64sp:$base, simm4s1:$offset)>;
    }

    // base
    def : Pat<(Ty (Load  (PredTy PPR:$gp), GPR64:$base, MemVT)),
              (I PPR:$gp, GPR64sp:$base, (i64 0))>;
  }

  // 2-element contiguous non-faulting loads
  defm : ldnf1<LDNF1B_D_IMM,  nxv2i64,  AArch64ldnf1_z,  nxv2i1, nxv2i8>;
  defm : ldnf1<LDNF1SB_D_IMM, nxv2i64,  AArch64ldnf1s_z, nxv2i1, nxv2i8>;
  defm : ldnf1<LDNF1H_D_IMM,  nxv2i64,  AArch64ldnf1_z,  nxv2i1, nxv2i16>;
  defm : ldnf1<LDNF1SH_D_IMM, nxv2i64,  AArch64ldnf1s_z, nxv2i1, nxv2i16>;
  defm : ldnf1<LDNF1W_D_IMM,  nxv2i64,  AArch64ldnf1_z,  nxv2i1, nxv2i32>;
  defm : ldnf1<LDNF1SW_D_IMM, nxv2i64,  AArch64ldnf1s_z, nxv2i1, nxv2i32>;
  defm : ldnf1<LDNF1D_IMM,    nxv2i64,  AArch64ldnf1_z,  nxv2i1, nxv2i64>;
  defm : ldnf1<LDNF1D_IMM,    nxv2f64,  AArch64ldnf1_z,  nxv2i1, nxv2f64>;

  // 4-element contiguous non-faulting loads
  defm : ldnf1<LDNF1B_S_IMM,  nxv4i32,  AArch64ldnf1_z,  nxv4i1, nxv4i8>;
  defm : ldnf1<LDNF1SB_S_IMM, nxv4i32,  AArch64ldnf1s_z, nxv4i1, nxv4i8>;
  defm : ldnf1<LDNF1H_S_IMM,  nxv4i32,  AArch64ldnf1_z,  nxv4i1, nxv4i16>;
  defm : ldnf1<LDNF1SH_S_IMM, nxv4i32,  AArch64ldnf1s_z, nxv4i1, nxv4i16>;
  defm : ldnf1<LDNF1W_IMM,    nxv4i32,  AArch64ldnf1_z,  nxv4i1, nxv4i32>;
  defm : ldnf1<LDNF1W_IMM,    nxv4f32,  AArch64ldnf1_z,  nxv4i1, nxv4f32>;

  // 8-element contiguous non-faulting loads
  defm : ldnf1<LDNF1B_H_IMM,  nxv8i16,  AArch64ldnf1_z,  nxv8i1, nxv8i8>;
  defm : ldnf1<LDNF1SB_H_IMM, nxv8i16,  AArch64ldnf1s_z, nxv8i1, nxv8i8>;
  defm : ldnf1<LDNF1H_IMM,    nxv8i16,  AArch64ldnf1_z,  nxv8i1, nxv8i16>;
  defm : ldnf1<LDNF1H_IMM,    nxv8f16,  AArch64ldnf1_z,  nxv8i1, nxv8f16>;
  defm : ldnf1<LDNF1H_IMM,    nxv8bf16, AArch64ldnf1_z,  nxv8i1, nxv8bf16>;

  // 16-element contiguous non-faulting loads
  defm : ldnf1<LDNF1B_IMM,    nxv16i8,  AArch64ldnf1_z, nxv16i1, nxv16i8>;

  multiclass ldff1<Instruction I, ValueType Ty, SDPatternOperator Load, ValueType PredTy, ValueType MemVT, ComplexPattern AddrCP> {
    // reg + reg
    let AddedComplexity = 1 in {
      def : Pat<(Ty (Load  (PredTy PPR:$gp), (AddrCP GPR64:$base, GPR64:$offset), MemVT)),
                (I PPR:$gp, GPR64sp:$base, GPR64:$offset)>;
    }

    // Base
    def : Pat<(Ty (Load  (PredTy PPR:$gp), GPR64:$base, MemVT)),
              (I PPR:$gp, GPR64sp:$base, XZR)>;
  }

  // 2-element contiguous first faulting loads
  defm : ldff1<LDFF1B_D,  nxv2i64,  AArch64ldff1_z,  nxv2i1, nxv2i8,   am_sve_regreg_lsl0>;
  defm : ldff1<LDFF1SB_D, nxv2i64,  AArch64ldff1s_z, nxv2i1, nxv2i8,   am_sve_regreg_lsl0>;
  defm : ldff1<LDFF1H_D,  nxv2i64,  AArch64ldff1_z,  nxv2i1, nxv2i16,  am_sve_regreg_lsl1>;
  defm : ldff1<LDFF1SH_D, nxv2i64,  AArch64ldff1s_z, nxv2i1, nxv2i16,  am_sve_regreg_lsl1>;
  defm : ldff1<LDFF1W_D,  nxv2i64,  AArch64ldff1_z,  nxv2i1, nxv2i32,  am_sve_regreg_lsl2>;
  defm : ldff1<LDFF1SW_D, nxv2i64,  AArch64ldff1s_z, nxv2i1, nxv2i32,  am_sve_regreg_lsl2>;
  defm : ldff1<LDFF1D,    nxv2i64,  AArch64ldff1_z,  nxv2i1, nxv2i64,  am_sve_regreg_lsl3>;
  defm : ldff1<LDFF1W_D,  nxv2f32,  AArch64ldff1_z,  nxv2i1, nxv2f32,  am_sve_regreg_lsl2>;
  defm : ldff1<LDFF1D,    nxv2f64,  AArch64ldff1_z,  nxv2i1, nxv2f64,  am_sve_regreg_lsl3>;

  // 4-element contiguous first faulting loads
  defm : ldff1<LDFF1B_S,  nxv4i32,  AArch64ldff1_z,  nxv4i1, nxv4i8,   am_sve_regreg_lsl0>;
  defm : ldff1<LDFF1SB_S, nxv4i32,  AArch64ldff1s_z, nxv4i1, nxv4i8,   am_sve_regreg_lsl0>;
  defm : ldff1<LDFF1H_S,  nxv4i32,  AArch64ldff1_z,  nxv4i1, nxv4i16,  am_sve_regreg_lsl1>;
  defm : ldff1<LDFF1SH_S, nxv4i32,  AArch64ldff1s_z, nxv4i1, nxv4i16,  am_sve_regreg_lsl1>;
  defm : ldff1<LDFF1W,    nxv4i32,  AArch64ldff1_z,  nxv4i1, nxv4i32,  am_sve_regreg_lsl2>;
  defm : ldff1<LDFF1W,    nxv4f32,  AArch64ldff1_z,  nxv4i1, nxv4f32,  am_sve_regreg_lsl2>;

  // 8-element contiguous first faulting loads
  defm : ldff1<LDFF1B_H,  nxv8i16,  AArch64ldff1_z,  nxv8i1, nxv8i8,   am_sve_regreg_lsl0>;
  defm : ldff1<LDFF1SB_H, nxv8i16,  AArch64ldff1s_z, nxv8i1, nxv8i8,   am_sve_regreg_lsl0>;
  defm : ldff1<LDFF1H,    nxv8i16,  AArch64ldff1_z,  nxv8i1, nxv8i16,  am_sve_regreg_lsl1>;
  defm : ldff1<LDFF1H,    nxv8f16,  AArch64ldff1_z,  nxv8i1, nxv8f16,  am_sve_regreg_lsl1>;
  defm : ldff1<LDFF1H,    nxv8bf16, AArch64ldff1_z,  nxv8i1, nxv8bf16, am_sve_regreg_lsl1>;

  // 16-element contiguous first faulting loads
  defm : ldff1<LDFF1B, nxv16i8, AArch64ldff1_z, nxv16i1, nxv16i8, am_sve_regreg_lsl0>;
} // End HasSVE

let Predicates = [HasSVEorSME] in {
  multiclass st1<Instruction RegRegInst, Instruction RegImmInst, ValueType Ty,
                 SDPatternOperator Store, ValueType PredTy, ValueType MemVT, ComplexPattern AddrCP> {
    // reg + reg
    let AddedComplexity = 1 in {
      def : Pat<(Store Ty:$vec, (AddrCP GPR64:$base, GPR64:$offset), PredTy:$gp, MemVT),
                (RegRegInst ZPR:$vec, PPR:$gp, GPR64sp:$base, GPR64:$offset)>;
    }

    // scalar + immediate (mul vl)
    let AddedComplexity = 2 in {
      def : Pat<(Store Ty:$vec, (am_sve_indexed_s4 GPR64sp:$base, simm4s1:$offset), PredTy:$gp, MemVT),
                (RegImmInst ZPR:$vec, PPR:$gp, GPR64sp:$base, simm4s1:$offset)>;
    }

    // base
    def : Pat<(Store Ty:$vec, GPR64:$base, (PredTy PPR:$gp), MemVT),
              (RegImmInst ZPR:$vec, PPR:$gp, GPR64:$base, (i64 0))>;
  }

  // 2-element contiguous store
  defm : st1<ST1B_D, ST1B_D_IMM, nxv2i64, AArch64st1, nxv2i1, nxv2i8,  am_sve_regreg_lsl0>;
  defm : st1<ST1H_D, ST1H_D_IMM, nxv2i64, AArch64st1, nxv2i1, nxv2i16, am_sve_regreg_lsl1>;
  defm : st1<ST1W_D, ST1W_D_IMM, nxv2i64, AArch64st1, nxv2i1, nxv2i32, am_sve_regreg_lsl2>;
  defm : st1<ST1D,   ST1D_IMM,   nxv2i64, AArch64st1, nxv2i1, nxv2i64, am_sve_regreg_lsl3>;

  // 4-element contiguous store
  defm : st1<ST1B_S, ST1B_S_IMM, nxv4i32, AArch64st1, nxv4i1, nxv4i8,  am_sve_regreg_lsl0>;
  defm : st1<ST1H_S, ST1H_S_IMM, nxv4i32, AArch64st1, nxv4i1, nxv4i16, am_sve_regreg_lsl1>;
  defm : st1<ST1W,   ST1W_IMM,   nxv4i32, AArch64st1, nxv4i1, nxv4i32, am_sve_regreg_lsl2>;

  // 8-element contiguous store
  defm : st1<ST1B_H, ST1B_H_IMM, nxv8i16, AArch64st1, nxv8i1, nxv8i8,  am_sve_regreg_lsl0>;
  defm : st1<ST1H,   ST1H_IMM,   nxv8i16, AArch64st1, nxv8i1, nxv8i16, am_sve_regreg_lsl1>;

  // 16-element contiguous store
  defm : st1<ST1B, ST1B_IMM,   nxv16i8, AArch64st1, nxv16i1, nxv16i8, am_sve_regreg_lsl0>;

  // Insert scalar into undef[0]
  def : Pat<(nxv16i8 (vector_insert (nxv16i8 (undef)), (i32 FPR32:$src), 0)),
            (INSERT_SUBREG (nxv16i8 (IMPLICIT_DEF)), FPR32:$src, ssub)>;
  def : Pat<(nxv8i16 (vector_insert (nxv8i16 (undef)), (i32 FPR32:$src), 0)),
            (INSERT_SUBREG (nxv8i16 (IMPLICIT_DEF)), FPR32:$src, ssub)>;
  def : Pat<(nxv4i32 (vector_insert (nxv4i32 (undef)), (i32 FPR32:$src), 0)),
            (INSERT_SUBREG (nxv4i32 (IMPLICIT_DEF)), FPR32:$src, ssub)>;
  def : Pat<(nxv2i64 (vector_insert (nxv2i64 (undef)), (i64 FPR64:$src), 0)),
            (INSERT_SUBREG (nxv2i64 (IMPLICIT_DEF)), FPR64:$src, dsub)>;

  def : Pat<(nxv8f16 (vector_insert (nxv8f16 (undef)), (f16 FPR16:$src), 0)),
            (INSERT_SUBREG (nxv8f16 (IMPLICIT_DEF)), FPR16:$src, hsub)>;
  def : Pat<(nxv4f16 (vector_insert (nxv4f16 (undef)), (f16 FPR16:$src), 0)),
            (INSERT_SUBREG (nxv4f16 (IMPLICIT_DEF)), FPR16:$src, hsub)>;
  def : Pat<(nxv2f16 (vector_insert (nxv2f16 (undef)), (f16 FPR16:$src), 0)),
            (INSERT_SUBREG (nxv2f16 (IMPLICIT_DEF)), FPR16:$src, hsub)>;
  def : Pat<(nxv8bf16 (vector_insert (nxv8bf16 (undef)), (bf16 FPR16:$src), 0)),
            (INSERT_SUBREG (nxv8bf16 (IMPLICIT_DEF)), FPR16:$src, hsub)>;
  def : Pat<(nxv4bf16 (vector_insert (nxv4bf16 (undef)), (bf16 FPR16:$src), 0)),
            (INSERT_SUBREG (nxv4bf16 (IMPLICIT_DEF)), FPR16:$src, hsub)>;
  def : Pat<(nxv2bf16 (vector_insert (nxv2bf16 (undef)), (bf16 FPR16:$src), 0)),
            (INSERT_SUBREG (nxv2bf16 (IMPLICIT_DEF)), FPR16:$src, hsub)>;
  def : Pat<(nxv4f32 (vector_insert (nxv4f32 (undef)), (f32 FPR32:$src), 0)),
            (INSERT_SUBREG (nxv4f32 (IMPLICIT_DEF)), FPR32:$src, ssub)>;
  def : Pat<(nxv2f32 (vector_insert (nxv2f32 (undef)), (f32 FPR32:$src), 0)),
            (INSERT_SUBREG (nxv2f32 (IMPLICIT_DEF)), FPR32:$src, ssub)>;
  def : Pat<(nxv2f64 (vector_insert (nxv2f64 (undef)), (f64 FPR64:$src), 0)),
            (INSERT_SUBREG (nxv2f64 (IMPLICIT_DEF)), FPR64:$src, dsub)>;

  // Insert scalar into vector[0]
  def : Pat<(nxv16i8 (vector_insert nxv16i8:$vec, (i32 GPR32:$src), 0)),
            (CPY_ZPmR_B ZPR:$vec, (PTRUE_B 1), GPR32:$src)>;
  def : Pat<(nxv8i16 (vector_insert nxv8i16:$vec, (i32 GPR32:$src), 0)),
            (CPY_ZPmR_H ZPR:$vec, (PTRUE_H 1), GPR32:$src)>;
  def : Pat<(nxv4i32 (vector_insert nxv4i32:$vec, (i32 GPR32:$src), 0)),
            (CPY_ZPmR_S ZPR:$vec, (PTRUE_S 1), GPR32:$src)>;
  def : Pat<(nxv2i64 (vector_insert nxv2i64:$vec, (i64 GPR64:$src), 0)),
            (CPY_ZPmR_D ZPR:$vec, (PTRUE_D 1), GPR64:$src)>;

  def : Pat<(nxv8f16 (vector_insert nxv8f16:$vec, (f16 FPR16:$src), 0)),
            (SEL_ZPZZ_H (PTRUE_H 1), (INSERT_SUBREG (IMPLICIT_DEF), FPR16:$src, hsub), ZPR:$vec)>;
  def : Pat<(nxv8bf16 (vector_insert nxv8bf16:$vec, (bf16 FPR16:$src), 0)),
            (SEL_ZPZZ_H (PTRUE_H 1), (INSERT_SUBREG (IMPLICIT_DEF), FPR16:$src, hsub), ZPR:$vec)>;
  def : Pat<(nxv4f32 (vector_insert nxv4f32:$vec, (f32 FPR32:$src), 0)),
            (SEL_ZPZZ_S (PTRUE_S 1), (INSERT_SUBREG (IMPLICIT_DEF), FPR32:$src, ssub), ZPR:$vec)>;
  def : Pat<(nxv2f64 (vector_insert nxv2f64:$vec, (f64 FPR64:$src), 0)),
            (SEL_ZPZZ_D (PTRUE_D 1), (INSERT_SUBREG (IMPLICIT_DEF), FPR64:$src, dsub), ZPR:$vec)>;

  // Insert scalar into vector with scalar index
  def : Pat<(nxv16i8 (vector_insert nxv16i8:$vec, GPR32:$src, GPR64:$index)),
            (CPY_ZPmR_B ZPR:$vec,
                        (CMPEQ_PPzZZ_B (PTRUE_B 31),
                                       (INDEX_II_B 0, 1),
                                       (DUP_ZR_B (i32 (EXTRACT_SUBREG GPR64:$index, sub_32)))),
                        GPR32:$src)>;
  def : Pat<(nxv8i16 (vector_insert nxv8i16:$vec, GPR32:$src, GPR64:$index)),
            (CPY_ZPmR_H ZPR:$vec,
                        (CMPEQ_PPzZZ_H (PTRUE_H 31),
                                       (INDEX_II_H 0, 1),
                                       (DUP_ZR_H (i32 (EXTRACT_SUBREG GPR64:$index, sub_32)))),
                        GPR32:$src)>;
  def : Pat<(nxv4i32 (vector_insert nxv4i32:$vec, GPR32:$src, GPR64:$index)),
            (CPY_ZPmR_S ZPR:$vec,
                        (CMPEQ_PPzZZ_S (PTRUE_S 31),
                                       (INDEX_II_S 0, 1),
                                       (DUP_ZR_S (i32 (EXTRACT_SUBREG GPR64:$index, sub_32)))),
                        GPR32:$src)>;
  def : Pat<(nxv2i64 (vector_insert nxv2i64:$vec, GPR64:$src, GPR64:$index)),
            (CPY_ZPmR_D ZPR:$vec,
                        (CMPEQ_PPzZZ_D (PTRUE_D 31),
                                       (INDEX_II_D 0, 1),
                                       (DUP_ZR_D GPR64:$index)),
                        GPR64:$src)>;

  // Insert FP scalar into vector with scalar index
  def : Pat<(nxv2f16 (vector_insert nxv2f16:$vec, (f16 FPR16:$src), GPR64:$index)),
            (CPY_ZPmV_H ZPR:$vec,
                        (CMPEQ_PPzZZ_D (PTRUE_D 31),
                                       (INDEX_II_D 0, 1),
                                       (DUP_ZR_D GPR64:$index)),
                        $src)>;
  def : Pat<(nxv4f16 (vector_insert nxv4f16:$vec, (f16 FPR16:$src), GPR64:$index)),
            (CPY_ZPmV_H ZPR:$vec,
                        (CMPEQ_PPzZZ_S (PTRUE_S 31),
                                       (INDEX_II_S 0, 1),
                                       (DUP_ZR_S (i32 (EXTRACT_SUBREG GPR64:$index, sub_32)))),
                        $src)>;
  def : Pat<(nxv8f16 (vector_insert nxv8f16:$vec, (f16 FPR16:$src), GPR64:$index)),
            (CPY_ZPmV_H ZPR:$vec,
                        (CMPEQ_PPzZZ_H (PTRUE_H 31),
                                       (INDEX_II_H 0, 1),
                                       (DUP_ZR_H (i32 (EXTRACT_SUBREG GPR64:$index, sub_32)))),
                        $src)>;
  def : Pat<(nxv2bf16 (vector_insert nxv2bf16:$vec, (bf16 FPR16:$src), GPR64:$index)),
            (CPY_ZPmV_H ZPR:$vec,
                        (CMPEQ_PPzZZ_D (PTRUE_D 31),
                                       (INDEX_II_D 0, 1),
                                       (DUP_ZR_D GPR64:$index)),
                        $src)>;
  def : Pat<(nxv4bf16 (vector_insert nxv4bf16:$vec, (bf16 FPR16:$src), GPR64:$index)),
            (CPY_ZPmV_H ZPR:$vec,
                        (CMPEQ_PPzZZ_S (PTRUE_S 31),
                                       (INDEX_II_S 0, 1),
                                       (DUP_ZR_S (i32 (EXTRACT_SUBREG GPR64:$index, sub_32)))),
                        $src)>;
  def : Pat<(nxv8bf16 (vector_insert nxv8bf16:$vec, (bf16 FPR16:$src), GPR64:$index)),
            (CPY_ZPmV_H ZPR:$vec,
                        (CMPEQ_PPzZZ_H (PTRUE_H 31),
                                       (INDEX_II_H 0, 1),
                                       (DUP_ZR_H (i32 (EXTRACT_SUBREG GPR64:$index, sub_32)))),
                        $src)>;
  def : Pat<(nxv2f32 (vector_insert nxv2f32:$vec, (f32 FPR32:$src), GPR64:$index)),
            (CPY_ZPmV_S ZPR:$vec,
                        (CMPEQ_PPzZZ_D (PTRUE_D 31),
                                       (INDEX_II_D 0, 1),
                                       (DUP_ZR_D GPR64:$index)),
                        $src) >;
  def : Pat<(nxv4f32 (vector_insert nxv4f32:$vec, (f32 FPR32:$src), GPR64:$index)),
            (CPY_ZPmV_S ZPR:$vec,
                        (CMPEQ_PPzZZ_S (PTRUE_S 31),
                                       (INDEX_II_S 0, 1),
                                       (DUP_ZR_S (i32 (EXTRACT_SUBREG GPR64:$index, sub_32)))),
                        $src)>;
  def : Pat<(nxv2f64 (vector_insert nxv2f64:$vec, (f64 FPR64:$src), GPR64:$index)),
            (CPY_ZPmV_D ZPR:$vec,
                        (CMPEQ_PPzZZ_D (PTRUE_D 31),
                                       (INDEX_II_D 0, 1),
                                       (DUP_ZR_D $index)),
                        $src)>;

  // Extract element from vector with scalar index
  def : Pat<(i32 (vector_extract nxv16i8:$vec, GPR64:$index)),
            (LASTB_RPZ_B (WHILELS_PXX_B XZR, GPR64:$index), ZPR:$vec)>;
  def : Pat<(i32 (vector_extract nxv8i16:$vec, GPR64:$index)),
            (LASTB_RPZ_H (WHILELS_PXX_H XZR, GPR64:$index), ZPR:$vec)>;
  def : Pat<(i32 (vector_extract nxv4i32:$vec, GPR64:$index)),
            (LASTB_RPZ_S (WHILELS_PXX_S XZR, GPR64:$index), ZPR:$vec)>;
  def : Pat<(i64 (vector_extract nxv2i64:$vec, GPR64:$index)),
            (LASTB_RPZ_D (WHILELS_PXX_D XZR, GPR64:$index), ZPR:$vec)>;
  def : Pat<(f16 (vector_extract nxv8f16:$vec, GPR64:$index)),
            (LASTB_VPZ_H (WHILELS_PXX_H XZR, GPR64:$index), ZPR:$vec)>;
  def : Pat<(f16 (vector_extract nxv4f16:$vec, GPR64:$index)),
            (LASTB_VPZ_H (WHILELS_PXX_S XZR, GPR64:$index), ZPR:$vec)>;
  def : Pat<(f16 (vector_extract nxv2f16:$vec, GPR64:$index)),
            (LASTB_VPZ_H (WHILELS_PXX_D XZR, GPR64:$index), ZPR:$vec)>;
  def : Pat<(bf16 (vector_extract nxv8bf16:$vec, GPR64:$index)),
            (LASTB_VPZ_H (WHILELS_PXX_H XZR, GPR64:$index), ZPR:$vec)>;
  def : Pat<(bf16 (vector_extract nxv4bf16:$vec, GPR64:$index)),
            (LASTB_VPZ_H (WHILELS_PXX_S XZR, GPR64:$index), ZPR:$vec)>;
  def : Pat<(bf16 (vector_extract nxv2bf16:$vec, GPR64:$index)),
            (LASTB_VPZ_H (WHILELS_PXX_D XZR, GPR64:$index), ZPR:$vec)>;
  def : Pat<(f32 (vector_extract nxv4f32:$vec, GPR64:$index)),
            (LASTB_VPZ_S (WHILELS_PXX_S XZR, GPR64:$index), ZPR:$vec)>;
  def : Pat<(f32 (vector_extract nxv2f32:$vec, GPR64:$index)),
            (LASTB_VPZ_S (WHILELS_PXX_D XZR, GPR64:$index), ZPR:$vec)>;
  def : Pat<(f64 (vector_extract nxv2f64:$vec, GPR64:$index)),
            (LASTB_VPZ_D (WHILELS_PXX_D XZR, GPR64:$index), ZPR:$vec)>;

  // Extract element from vector with immediate index
  def : Pat<(i32 (vector_extract nxv16i8:$vec, sve_elm_idx_extdup_b:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_B ZPR:$vec, sve_elm_idx_extdup_b:$index), ssub)>;
  def : Pat<(i32 (vector_extract nxv8i16:$vec, sve_elm_idx_extdup_h:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_H ZPR:$vec, sve_elm_idx_extdup_h:$index), ssub)>;
  def : Pat<(i32 (vector_extract nxv4i32:$vec, sve_elm_idx_extdup_s:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_S ZPR:$vec, sve_elm_idx_extdup_s:$index), ssub)>;
  def : Pat<(i64 (vector_extract nxv2i64:$vec, sve_elm_idx_extdup_d:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_D ZPR:$vec, sve_elm_idx_extdup_d:$index), dsub)>;
  def : Pat<(f16 (vector_extract nxv8f16:$vec, sve_elm_idx_extdup_h:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_H ZPR:$vec, sve_elm_idx_extdup_h:$index), hsub)>;
  def : Pat<(f16 (vector_extract nxv4f16:$vec, sve_elm_idx_extdup_s:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_S ZPR:$vec, sve_elm_idx_extdup_s:$index), hsub)>;
  def : Pat<(f16 (vector_extract nxv2f16:$vec, sve_elm_idx_extdup_d:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_D ZPR:$vec, sve_elm_idx_extdup_d:$index), hsub)>;
  def : Pat<(bf16 (vector_extract nxv8bf16:$vec, sve_elm_idx_extdup_h:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_H ZPR:$vec, sve_elm_idx_extdup_h:$index), hsub)>;
  def : Pat<(bf16 (vector_extract nxv4bf16:$vec, sve_elm_idx_extdup_s:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_S ZPR:$vec, sve_elm_idx_extdup_s:$index), hsub)>;
  def : Pat<(bf16 (vector_extract nxv2bf16:$vec, sve_elm_idx_extdup_d:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_D ZPR:$vec, sve_elm_idx_extdup_d:$index), hsub)>;
  def : Pat<(f32 (vector_extract nxv4f32:$vec, sve_elm_idx_extdup_s:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_S ZPR:$vec, sve_elm_idx_extdup_s:$index), ssub)>;
  def : Pat<(f32 (vector_extract nxv2f32:$vec, sve_elm_idx_extdup_d:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_D ZPR:$vec, sve_elm_idx_extdup_d:$index), ssub)>;
  def : Pat<(f64 (vector_extract nxv2f64:$vec, sve_elm_idx_extdup_d:$index)),
            (EXTRACT_SUBREG (DUP_ZZI_D ZPR:$vec, sve_elm_idx_extdup_d:$index), dsub)>;

  // Extract element from vector with immediate index that's within the bottom 128-bits.
  let Predicates = [HasNEON], AddedComplexity = 1 in {
  def : Pat<(i32 (vector_extract nxv16i8:$vec, VectorIndexB:$index)),
            (UMOVvi8 (v16i8 (EXTRACT_SUBREG ZPR:$vec, zsub)), VectorIndexB:$index)>;
  def : Pat<(i32 (vector_extract nxv8i16:$vec, VectorIndexH:$index)),
            (UMOVvi16 (v8i16 (EXTRACT_SUBREG ZPR:$vec, zsub)), VectorIndexH:$index)>;
  def : Pat<(i32 (vector_extract nxv4i32:$vec, VectorIndexS:$index)),
            (UMOVvi32 (v4i32 (EXTRACT_SUBREG ZPR:$vec, zsub)), VectorIndexS:$index)>;
  def : Pat<(i64 (vector_extract nxv2i64:$vec, VectorIndexD:$index)),
            (UMOVvi64 (v2i64 (EXTRACT_SUBREG ZPR:$vec, zsub)), VectorIndexD:$index)>;
  } // End HasNEON

  let Predicates = [HasNEON] in {
  def : Pat<(sext_inreg (vector_extract nxv16i8:$vec, VectorIndexB:$index), i8),
            (SMOVvi8to32 (v16i8 (EXTRACT_SUBREG ZPR:$vec, zsub)), VectorIndexB:$index)>;
  def : Pat<(sext_inreg (anyext (i32 (vector_extract nxv16i8:$vec, VectorIndexB:$index))), i8),
            (SMOVvi8to64 (v16i8 (EXTRACT_SUBREG ZPR:$vec, zsub)), VectorIndexB:$index)>;

  def : Pat<(sext_inreg (vector_extract nxv8i16:$vec, VectorIndexH:$index), i16),
            (SMOVvi16to32 (v8i16 (EXTRACT_SUBREG ZPR:$vec, zsub)), VectorIndexH:$index)>;
  def : Pat<(sext_inreg (anyext (i32 (vector_extract nxv8i16:$vec, VectorIndexH:$index))), i16),
            (SMOVvi16to64 (v8i16 (EXTRACT_SUBREG ZPR:$vec, zsub)), VectorIndexH:$index)>;

  def : Pat<(sext (i32 (vector_extract nxv4i32:$vec, VectorIndexS:$index))),
            (SMOVvi32to64 (v4i32 (EXTRACT_SUBREG ZPR:$vec, zsub)), VectorIndexS:$index)>;
  } // End HasNEON

  // Extract first element from vector.
  let AddedComplexity = 2 in {
  def : Pat<(i32 (vector_extract nxv16i8:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, ssub)>;
  def : Pat<(i32 (vector_extract nxv8i16:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, ssub)>;
  def : Pat<(i32 (vector_extract nxv4i32:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, ssub)>;
  def : Pat<(i64 (vector_extract nxv2i64:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, dsub)>;
  def : Pat<(f16 (vector_extract nxv8f16:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, hsub)>;
  def : Pat<(f16 (vector_extract nxv4f16:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, hsub)>;
  def : Pat<(f16 (vector_extract nxv2f16:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, hsub)>;
  def : Pat<(bf16 (vector_extract nxv8bf16:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, hsub)>;
  def : Pat<(bf16 (vector_extract nxv4bf16:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, hsub)>;
  def : Pat<(bf16 (vector_extract nxv2bf16:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, hsub)>;
  def : Pat<(f32 (vector_extract nxv4f32:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, ssub)>;
  def : Pat<(f32 (vector_extract nxv2f32:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, ssub)>;
  def : Pat<(f64 (vector_extract nxv2f64:$Zs, (i64 0))),
            (EXTRACT_SUBREG ZPR:$Zs, dsub)>;
  }

  multiclass sve_predicated_add<SDNode extend, int value> {
    def : Pat<(nxv16i8 (add ZPR:$op, (extend nxv16i1:$pred))),
              (ADD_ZPmZ_B PPR:$pred, ZPR:$op, (DUP_ZI_B value, 0))>;
    def : Pat<(nxv8i16 (add ZPR:$op, (extend nxv8i1:$pred))),
              (ADD_ZPmZ_H PPR:$pred, ZPR:$op, (DUP_ZI_H value, 0))>;
    def : Pat<(nxv4i32 (add ZPR:$op, (extend nxv4i1:$pred))),
              (ADD_ZPmZ_S PPR:$pred, ZPR:$op, (DUP_ZI_S value, 0))>;
    def : Pat<(nxv2i64 (add ZPR:$op, (extend nxv2i1:$pred))),
              (ADD_ZPmZ_D PPR:$pred, ZPR:$op, (DUP_ZI_D value, 0))>;
  }

  defm : sve_predicated_add<zext, 1>;
  defm : sve_predicated_add<sext, 255>;

  def : Pat<(nxv16i8 (sub ZPR:$op, (sext nxv16i1:$pred))),
            (SUB_ZPmZ_B PPR:$pred, ZPR:$op, (DUP_ZI_B 255, 0))>;
  def : Pat<(nxv8i16 (sub ZPR:$op, (sext nxv8i1:$pred))),
            (SUB_ZPmZ_H PPR:$pred, ZPR:$op, (DUP_ZI_H 255, 0))>;
  def : Pat<(nxv4i32 (sub ZPR:$op, (sext nxv4i1:$pred))),
            (SUB_ZPmZ_S PPR:$pred, ZPR:$op, (DUP_ZI_S 255, 0))>;
  def : Pat<(nxv2i64 (sub ZPR:$op, (sext nxv2i1:$pred))),
            (SUB_ZPmZ_D PPR:$pred, ZPR:$op, (DUP_ZI_D 255, 0))>;
} // End HasSVEorSME

let Predicates = [HasSVE, HasMatMulInt8] in {
  defm  SMMLA_ZZZ : sve_int_matmul<0b00, "smmla", int_aarch64_sve_smmla>;
  defm  UMMLA_ZZZ : sve_int_matmul<0b11, "ummla", int_aarch64_sve_ummla>;
  defm USMMLA_ZZZ : sve_int_matmul<0b10, "usmmla", int_aarch64_sve_usmmla>;
} // End HasSVE, HasMatMulInt8

let Predicates = [HasSVEorSME, HasMatMulInt8] in {
  defm USDOT_ZZZ  : sve_int_dot_mixed<"usdot", int_aarch64_sve_usdot>;
  defm USDOT_ZZZI : sve_int_dot_mixed_indexed<0, "usdot", int_aarch64_sve_usdot_lane>;
  defm SUDOT_ZZZI : sve_int_dot_mixed_indexed<1, "sudot", int_aarch64_sve_sudot_lane>;
} // End HasSVEorSME, HasMatMulInt8

let Predicates = [HasSVE, HasMatMulFP32] in {
  defm FMMLA_ZZZ_S : sve_fp_matrix_mla<0, "fmmla", ZPR32, int_aarch64_sve_fmmla, nxv4f32>;
} // End HasSVE, HasMatMulFP32

let Predicates = [HasSVE, HasMatMulFP64] in {
  defm FMMLA_ZZZ_D : sve_fp_matrix_mla<1, "fmmla", ZPR64, int_aarch64_sve_fmmla, nxv2f64>;
  defm LD1RO_B_IMM : sve_mem_ldor_si<0b00, "ld1rob", Z_b, ZPR8,  nxv16i8, nxv16i1, AArch64ld1ro_z>;
  defm LD1RO_H_IMM : sve_mem_ldor_si<0b01, "ld1roh", Z_h, ZPR16, nxv8i16, nxv8i1,  AArch64ld1ro_z>;
  defm LD1RO_W_IMM : sve_mem_ldor_si<0b10, "ld1row", Z_s, ZPR32, nxv4i32, nxv4i1,  AArch64ld1ro_z>;
  defm LD1RO_D_IMM : sve_mem_ldor_si<0b11, "ld1rod", Z_d, ZPR64, nxv2i64, nxv2i1,  AArch64ld1ro_z>;
  defm LD1RO_B     : sve_mem_ldor_ss<0b00, "ld1rob", Z_b, ZPR8,  GPR64NoXZRshifted8,  nxv16i8, nxv16i1, AArch64ld1ro_z, am_sve_regreg_lsl0>;
  defm LD1RO_H     : sve_mem_ldor_ss<0b01, "ld1roh", Z_h, ZPR16, GPR64NoXZRshifted16, nxv8i16, nxv8i1,  AArch64ld1ro_z, am_sve_regreg_lsl1>;
  defm LD1RO_W     : sve_mem_ldor_ss<0b10, "ld1row", Z_s, ZPR32, GPR64NoXZRshifted32, nxv4i32, nxv4i1,  AArch64ld1ro_z, am_sve_regreg_lsl2>;
  defm LD1RO_D     : sve_mem_ldor_ss<0b11, "ld1rod", Z_d, ZPR64, GPR64NoXZRshifted64, nxv2i64, nxv2i1,  AArch64ld1ro_z, am_sve_regreg_lsl3>;
} // End HasSVE, HasMatMulFP64

let Predicates = [HasSVEorSME, HasMatMulFP64] in {
  defm ZIP1_ZZZ_Q  : sve_int_perm_bin_perm_128_zz<0b00, 0, "zip1", int_aarch64_sve_zip1q>;
  defm ZIP2_ZZZ_Q  : sve_int_perm_bin_perm_128_zz<0b00, 1, "zip2", int_aarch64_sve_zip2q>;
  defm UZP1_ZZZ_Q  : sve_int_perm_bin_perm_128_zz<0b01, 0, "uzp1", int_aarch64_sve_uzp1q>;
  defm UZP2_ZZZ_Q  : sve_int_perm_bin_perm_128_zz<0b01, 1, "uzp2", int_aarch64_sve_uzp2q>;
  defm TRN1_ZZZ_Q  : sve_int_perm_bin_perm_128_zz<0b11, 0, "trn1", int_aarch64_sve_trn1q>;
  defm TRN2_ZZZ_Q  : sve_int_perm_bin_perm_128_zz<0b11, 1, "trn2", int_aarch64_sve_trn2q>;
} // End HasSVEorSME, HasMatMulFP64

let Predicates = [HasSVE2orSME] in {
  // SVE2 integer multiply-add (indexed)
  defm MLA_ZZZI : sve2_int_mla_by_indexed_elem<0b01, 0b0, "mla", int_aarch64_sve_mla_lane>;
  defm MLS_ZZZI : sve2_int_mla_by_indexed_elem<0b01, 0b1, "mls", int_aarch64_sve_mls_lane>;

  // SVE2 saturating multiply-add high (indexed)
  defm SQRDMLAH_ZZZI : sve2_int_mla_by_indexed_elem<0b10, 0b0, "sqrdmlah", int_aarch64_sve_sqrdmlah_lane>;
  defm SQRDMLSH_ZZZI : sve2_int_mla_by_indexed_elem<0b10, 0b1, "sqrdmlsh", int_aarch64_sve_sqrdmlsh_lane>;

  // SVE2 saturating multiply-add high (vectors, unpredicated)
  defm SQRDMLAH_ZZZ : sve2_int_mla<0b0, "sqrdmlah", int_aarch64_sve_sqrdmlah>;
  defm SQRDMLSH_ZZZ : sve2_int_mla<0b1, "sqrdmlsh", int_aarch64_sve_sqrdmlsh>;

  // SVE2 integer multiply (indexed)
  defm MUL_ZZZI : sve2_int_mul_by_indexed_elem<0b1110, "mul", int_aarch64_sve_mul_lane>;

  // SVE2 saturating multiply high (indexed)
  defm SQDMULH_ZZZI  : sve2_int_mul_by_indexed_elem<0b1100, "sqdmulh",  int_aarch64_sve_sqdmulh_lane>;
  defm SQRDMULH_ZZZI : sve2_int_mul_by_indexed_elem<0b1101, "sqrdmulh", int_aarch64_sve_sqrdmulh_lane>;

  // SVE2 signed saturating doubling multiply high (unpredicated)
  defm SQDMULH_ZZZ  : sve2_int_mul<0b100, "sqdmulh",  int_aarch64_sve_sqdmulh>;
  defm SQRDMULH_ZZZ : sve2_int_mul<0b101, "sqrdmulh", int_aarch64_sve_sqrdmulh>;

  // SVE2 integer multiply vectors (unpredicated)
  defm MUL_ZZZ    : sve2_int_mul<0b000,  "mul",   AArch64mul>;
  defm SMULH_ZZZ  : sve2_int_mul<0b010,  "smulh", AArch64smulh>;
  defm UMULH_ZZZ  : sve2_int_mul<0b011,  "umulh", AArch64umulh>;
  defm PMUL_ZZZ   : sve2_int_mul_single<0b001, "pmul", int_aarch64_sve_pmul>;

  // SVE2 complex integer dot product (indexed)
  defm CDOT_ZZZI : sve2_cintx_dot_by_indexed_elem<"cdot", int_aarch64_sve_cdot_lane>;

  // SVE2 complex integer dot product
  defm CDOT_ZZZ : sve2_cintx_dot<"cdot", int_aarch64_sve_cdot>;

  // SVE2 complex integer multiply-add (indexed)
  defm CMLA_ZZZI      : sve2_cmla_by_indexed_elem<0b0, "cmla", int_aarch64_sve_cmla_lane_x>;
  // SVE2 complex saturating multiply-add (indexed)
  defm SQRDCMLAH_ZZZI : sve2_cmla_by_indexed_elem<0b1, "sqrdcmlah", int_aarch64_sve_sqrdcmlah_lane_x>;

  // SVE2 complex integer multiply-add
  defm CMLA_ZZZ      : sve2_int_cmla<0b0, "cmla",      int_aarch64_sve_cmla_x>;
  defm SQRDCMLAH_ZZZ : sve2_int_cmla<0b1, "sqrdcmlah", int_aarch64_sve_sqrdcmlah_x>;

  // SVE2 integer multiply long (indexed)
  defm SMULLB_ZZZI : sve2_int_mul_long_by_indexed_elem<0b000, "smullb", int_aarch64_sve_smullb_lane>;
  defm SMULLT_ZZZI : sve2_int_mul_long_by_indexed_elem<0b001, "smullt", int_aarch64_sve_smullt_lane>;
  defm UMULLB_ZZZI : sve2_int_mul_long_by_indexed_elem<0b010, "umullb", int_aarch64_sve_umullb_lane>;
  defm UMULLT_ZZZI : sve2_int_mul_long_by_indexed_elem<0b011, "umullt", int_aarch64_sve_umullt_lane>;

  // SVE2 saturating multiply (indexed)
  defm SQDMULLB_ZZZI : sve2_int_mul_long_by_indexed_elem<0b100, "sqdmullb", int_aarch64_sve_sqdmullb_lane>;
  defm SQDMULLT_ZZZI : sve2_int_mul_long_by_indexed_elem<0b101, "sqdmullt", int_aarch64_sve_sqdmullt_lane>;

  // SVE2 integer multiply-add long (indexed)
  defm SMLALB_ZZZI : sve2_int_mla_long_by_indexed_elem<0b1000, "smlalb", int_aarch64_sve_smlalb_lane>;
  defm SMLALT_ZZZI : sve2_int_mla_long_by_indexed_elem<0b1001, "smlalt", int_aarch64_sve_smlalt_lane>;
  defm UMLALB_ZZZI : sve2_int_mla_long_by_indexed_elem<0b1010, "umlalb", int_aarch64_sve_umlalb_lane>;
  defm UMLALT_ZZZI : sve2_int_mla_long_by_indexed_elem<0b1011, "umlalt", int_aarch64_sve_umlalt_lane>;
  defm SMLSLB_ZZZI : sve2_int_mla_long_by_indexed_elem<0b1100, "smlslb", int_aarch64_sve_smlslb_lane>;
  defm SMLSLT_ZZZI : sve2_int_mla_long_by_indexed_elem<0b1101, "smlslt", int_aarch64_sve_smlslt_lane>;
  defm UMLSLB_ZZZI : sve2_int_mla_long_by_indexed_elem<0b1110, "umlslb", int_aarch64_sve_umlslb_lane>;
  defm UMLSLT_ZZZI : sve2_int_mla_long_by_indexed_elem<0b1111, "umlslt", int_aarch64_sve_umlslt_lane>;

  // SVE2 integer multiply-add long (vectors, unpredicated)
  defm SMLALB_ZZZ : sve2_int_mla_long<0b10000, "smlalb", int_aarch64_sve_smlalb>;
  defm SMLALT_ZZZ : sve2_int_mla_long<0b10001, "smlalt", int_aarch64_sve_smlalt>;
  defm UMLALB_ZZZ : sve2_int_mla_long<0b10010, "umlalb", int_aarch64_sve_umlalb>;
  defm UMLALT_ZZZ : sve2_int_mla_long<0b10011, "umlalt", int_aarch64_sve_umlalt>;
  defm SMLSLB_ZZZ : sve2_int_mla_long<0b10100, "smlslb", int_aarch64_sve_smlslb>;
  defm SMLSLT_ZZZ : sve2_int_mla_long<0b10101, "smlslt", int_aarch64_sve_smlslt>;
  defm UMLSLB_ZZZ : sve2_int_mla_long<0b10110, "umlslb", int_aarch64_sve_umlslb>;
  defm UMLSLT_ZZZ : sve2_int_mla_long<0b10111, "umlslt", int_aarch64_sve_umlslt>;

  // SVE2 saturating multiply-add long (indexed)
  defm SQDMLALB_ZZZI : sve2_int_mla_long_by_indexed_elem<0b0100, "sqdmlalb", int_aarch64_sve_sqdmlalb_lane>;
  defm SQDMLALT_ZZZI : sve2_int_mla_long_by_indexed_elem<0b0101, "sqdmlalt", int_aarch64_sve_sqdmlalt_lane>;
  defm SQDMLSLB_ZZZI : sve2_int_mla_long_by_indexed_elem<0b0110, "sqdmlslb", int_aarch64_sve_sqdmlslb_lane>;
  defm SQDMLSLT_ZZZI : sve2_int_mla_long_by_indexed_elem<0b0111, "sqdmlslt", int_aarch64_sve_sqdmlslt_lane>;

  // SVE2 saturating multiply-add long (vectors, unpredicated)
  defm SQDMLALB_ZZZ : sve2_int_mla_long<0b11000, "sqdmlalb", int_aarch64_sve_sqdmlalb>;
  defm SQDMLALT_ZZZ : sve2_int_mla_long<0b11001, "sqdmlalt", int_aarch64_sve_sqdmlalt>;
  defm SQDMLSLB_ZZZ : sve2_int_mla_long<0b11010, "sqdmlslb", int_aarch64_sve_sqdmlslb>;
  defm SQDMLSLT_ZZZ : sve2_int_mla_long<0b11011, "sqdmlslt", int_aarch64_sve_sqdmlslt>;

  // SVE2 saturating multiply-add interleaved long
  defm SQDMLALBT_ZZZ : sve2_int_mla_long<0b00010, "sqdmlalbt", int_aarch64_sve_sqdmlalbt>;
  defm SQDMLSLBT_ZZZ : sve2_int_mla_long<0b00011, "sqdmlslbt", int_aarch64_sve_sqdmlslbt>;

  // SVE2 integer halving add/subtract (predicated)
  defm SHADD_ZPmZ  : sve2_int_arith_pred<0b100000, "shadd",  AArch64shadd>;
  defm UHADD_ZPmZ  : sve2_int_arith_pred<0b100010, "uhadd",  AArch64uhadd>;
  defm SHSUB_ZPmZ  : sve2_int_arith_pred<0b100100, "shsub",  int_aarch64_sve_shsub>;
  defm UHSUB_ZPmZ  : sve2_int_arith_pred<0b100110, "uhsub",  int_aarch64_sve_uhsub>;
  defm SRHADD_ZPmZ : sve2_int_arith_pred<0b101000, "srhadd", AArch64srhadd>;
  defm URHADD_ZPmZ : sve2_int_arith_pred<0b101010, "urhadd", AArch64urhadd>;
  defm SHSUBR_ZPmZ : sve2_int_arith_pred<0b101100, "shsubr", int_aarch64_sve_shsubr>;
  defm UHSUBR_ZPmZ : sve2_int_arith_pred<0b101110, "uhsubr", int_aarch64_sve_uhsubr>;

  // SVE2 integer pairwise add and accumulate long
  defm SADALP_ZPmZ : sve2_int_sadd_long_accum_pairwise<0, "sadalp", int_aarch64_sve_sadalp>;
  defm UADALP_ZPmZ : sve2_int_sadd_long_accum_pairwise<1, "uadalp", int_aarch64_sve_uadalp>;

  // SVE2 integer pairwise arithmetic
  defm ADDP_ZPmZ  : sve2_int_arith_pred<0b100011, "addp",  int_aarch64_sve_addp>;
  defm SMAXP_ZPmZ : sve2_int_arith_pred<0b101001, "smaxp", int_aarch64_sve_smaxp>;
  defm UMAXP_ZPmZ : sve2_int_arith_pred<0b101011, "umaxp", int_aarch64_sve_umaxp>;
  defm SMINP_ZPmZ : sve2_int_arith_pred<0b101101, "sminp", int_aarch64_sve_sminp>;
  defm UMINP_ZPmZ : sve2_int_arith_pred<0b101111, "uminp", int_aarch64_sve_uminp>;

  // SVE2 integer unary operations (predicated)
  defm URECPE_ZPmZ  : sve2_int_un_pred_arit_s<0b000, "urecpe",  int_aarch64_sve_urecpe>;
  defm URSQRTE_ZPmZ : sve2_int_un_pred_arit_s<0b001, "ursqrte", int_aarch64_sve_ursqrte>;
  defm SQABS_ZPmZ   : sve2_int_un_pred_arit<0b100,   "sqabs",   int_aarch64_sve_sqabs>;
  defm SQNEG_ZPmZ   : sve2_int_un_pred_arit<0b101,   "sqneg",   int_aarch64_sve_sqneg>;

  // SVE2 saturating add/subtract
  defm SQADD_ZPmZ  : sve2_int_arith_pred<0b110000, "sqadd",  int_aarch64_sve_sqadd>;
  defm UQADD_ZPmZ  : sve2_int_arith_pred<0b110010, "uqadd",  int_aarch64_sve_uqadd>;
  defm SQSUB_ZPmZ  : sve2_int_arith_pred<0b110100, "sqsub",  int_aarch64_sve_sqsub>;
  defm UQSUB_ZPmZ  : sve2_int_arith_pred<0b110110, "uqsub",  int_aarch64_sve_uqsub>;
  defm SUQADD_ZPmZ : sve2_int_arith_pred<0b111000, "suqadd", int_aarch64_sve_suqadd>;
  defm USQADD_ZPmZ : sve2_int_arith_pred<0b111010, "usqadd", int_aarch64_sve_usqadd>;
  defm SQSUBR_ZPmZ : sve2_int_arith_pred<0b111100, "sqsubr", int_aarch64_sve_sqsubr>;
  defm UQSUBR_ZPmZ : sve2_int_arith_pred<0b111110, "uqsubr", int_aarch64_sve_uqsubr>;

  // SVE2 saturating/rounding bitwise shift left (predicated)
  defm SRSHL_ZPmZ   : sve2_int_arith_pred<0b000100, "srshl",   int_aarch64_sve_srshl,  "SRSHL_ZPZZ",   DestructiveBinaryCommWithRev, "SRSHLR_ZPmZ">;
  defm URSHL_ZPmZ   : sve2_int_arith_pred<0b000110, "urshl",   int_aarch64_sve_urshl,  "URSHL_ZPZZ",   DestructiveBinaryCommWithRev, "URSHLR_ZPmZ">;
  defm SRSHLR_ZPmZ  : sve2_int_arith_pred<0b001100, "srshlr",  null_frag,              "SRSHLR_ZPZZ",  DestructiveBinaryCommWithRev, "SRSHL_ZPmZ", /*isReverseInstr*/ 1>;
  defm URSHLR_ZPmZ  : sve2_int_arith_pred<0b001110, "urshlr",  null_frag,              "URSHLR_ZPZZ",  DestructiveBinaryCommWithRev, "URSHL_ZPmZ", /*isReverseInstr*/ 1>;
  defm SQSHL_ZPmZ   : sve2_int_arith_pred<0b010000, "sqshl",   int_aarch64_sve_sqshl,  "SQSHL_ZPZZ",   DestructiveBinaryCommWithRev, "SQSHLR_ZPmZ">;
  defm UQSHL_ZPmZ   : sve2_int_arith_pred<0b010010, "uqshl",   int_aarch64_sve_uqshl,  "UQSHL_ZPZZ",   DestructiveBinaryCommWithRev, "UQSHLR_ZPmZ">;
  defm SQRSHL_ZPmZ  : sve2_int_arith_pred<0b010100, "sqrshl",  int_aarch64_sve_sqrshl, "SQRSHL_ZPZZ",  DestructiveBinaryCommWithRev, "SQRSHLR_ZPmZ">;
  defm UQRSHL_ZPmZ  : sve2_int_arith_pred<0b010110, "uqrshl",  int_aarch64_sve_uqrshl, "UQRSHL_ZPZZ",  DestructiveBinaryCommWithRev, "UQRSHLR_ZPmZ">;
  defm SQSHLR_ZPmZ  : sve2_int_arith_pred<0b011000, "sqshlr",  null_frag,              "SQSHLR_ZPZZ",  DestructiveBinaryCommWithRev, "SQSHL_ZPmZ", /*isReverseInstr*/ 1>;
  defm UQSHLR_ZPmZ  : sve2_int_arith_pred<0b011010, "uqshlr",  null_frag,              "UQSHLR_ZPZZ",  DestructiveBinaryCommWithRev, "UQSHL_ZPmZ", /*isReverseInstr*/ 1>;
  defm SQRSHLR_ZPmZ : sve2_int_arith_pred<0b011100, "sqrshlr", null_frag,              "SQRSHLR_ZPZZ", DestructiveBinaryCommWithRev, "SQRSHL_ZPmZ", /*isReverseInstr*/ 1>;
  defm UQRSHLR_ZPmZ : sve2_int_arith_pred<0b011110, "uqrshlr", null_frag,              "UQRSHLR_ZPZZ", DestructiveBinaryCommWithRev, "UQRSHL_ZPmZ", /*isReverseInstr*/ 1>;

  defm SRSHL_ZPZZ   : sve_int_bin_pred_all_active_bhsd<int_aarch64_sve_srshl>;
  defm URSHL_ZPZZ   : sve_int_bin_pred_all_active_bhsd<int_aarch64_sve_urshl>;
  defm SQSHL_ZPZZ   : sve_int_bin_pred_all_active_bhsd<int_aarch64_sve_sqshl>;
  defm UQSHL_ZPZZ   : sve_int_bin_pred_all_active_bhsd<int_aarch64_sve_uqshl>;
  defm SQRSHL_ZPZZ  : sve_int_bin_pred_all_active_bhsd<int_aarch64_sve_sqrshl>;
  defm UQRSHL_ZPZZ  : sve_int_bin_pred_all_active_bhsd<int_aarch64_sve_uqrshl>;
} // End HasSVE2orSME

let Predicates = [HasSVE2orSME, UseExperimentalZeroingPseudos] in {
  defm SQSHL_ZPZI  : sve_int_bin_pred_shift_imm_left_zeroing_bhsd<null_frag>;
  defm UQSHL_ZPZI  : sve_int_bin_pred_shift_imm_left_zeroing_bhsd<null_frag>;
  defm SRSHR_ZPZI  : sve_int_bin_pred_shift_imm_right_zeroing_bhsd<int_aarch64_sve_srshr>;
  defm URSHR_ZPZI  : sve_int_bin_pred_shift_imm_right_zeroing_bhsd<int_aarch64_sve_urshr>;
  defm SQSHLU_ZPZI : sve_int_bin_pred_shift_imm_left_zeroing_bhsd<int_aarch64_sve_sqshlu>;
} // End HasSVE2orSME, UseExperimentalZeroingPseudos

let Predicates = [HasSVE2orSME] in {
  // SVE2 predicated shifts
  defm SQSHL_ZPmI  : sve_int_bin_pred_shift_imm_left_dup<0b0110, "sqshl",  "SQSHL_ZPZI",  int_aarch64_sve_sqshl>;
  defm UQSHL_ZPmI  : sve_int_bin_pred_shift_imm_left_dup<0b0111, "uqshl",  "UQSHL_ZPZI",  int_aarch64_sve_uqshl>;
  defm SRSHR_ZPmI  : sve_int_bin_pred_shift_imm_right<   0b1100, "srshr",  "SRSHR_ZPZI",  int_aarch64_sve_srshr>;
  defm URSHR_ZPmI  : sve_int_bin_pred_shift_imm_right<   0b1101, "urshr",  "URSHR_ZPZI",  AArch64urshri_p>;
  defm SQSHLU_ZPmI : sve_int_bin_pred_shift_imm_left<    0b1111, "sqshlu", "SQSHLU_ZPZI", int_aarch64_sve_sqshlu>;

  // SVE2 integer add/subtract long
  defm SADDLB_ZZZ : sve2_wide_int_arith_long<0b00000, "saddlb", int_aarch64_sve_saddlb>;
  defm SADDLT_ZZZ : sve2_wide_int_arith_long<0b00001, "saddlt", int_aarch64_sve_saddlt>;
  defm UADDLB_ZZZ : sve2_wide_int_arith_long<0b00010, "uaddlb", int_aarch64_sve_uaddlb>;
  defm UADDLT_ZZZ : sve2_wide_int_arith_long<0b00011, "uaddlt", int_aarch64_sve_uaddlt>;
  defm SSUBLB_ZZZ : sve2_wide_int_arith_long<0b00100, "ssublb", int_aarch64_sve_ssublb>;
  defm SSUBLT_ZZZ : sve2_wide_int_arith_long<0b00101, "ssublt", int_aarch64_sve_ssublt>;
  defm USUBLB_ZZZ : sve2_wide_int_arith_long<0b00110, "usublb", int_aarch64_sve_usublb>;
  defm USUBLT_ZZZ : sve2_wide_int_arith_long<0b00111, "usublt", int_aarch64_sve_usublt>;
  defm SABDLB_ZZZ : sve2_wide_int_arith_long<0b01100, "sabdlb", int_aarch64_sve_sabdlb>;
  defm SABDLT_ZZZ : sve2_wide_int_arith_long<0b01101, "sabdlt", int_aarch64_sve_sabdlt>;
  defm UABDLB_ZZZ : sve2_wide_int_arith_long<0b01110, "uabdlb", int_aarch64_sve_uabdlb>;
  defm UABDLT_ZZZ : sve2_wide_int_arith_long<0b01111, "uabdlt", int_aarch64_sve_uabdlt>;

  // SVE2 integer add/subtract wide
  defm SADDWB_ZZZ : sve2_wide_int_arith_wide<0b000, "saddwb", int_aarch64_sve_saddwb>;
  defm SADDWT_ZZZ : sve2_wide_int_arith_wide<0b001, "saddwt", int_aarch64_sve_saddwt>;
  defm UADDWB_ZZZ : sve2_wide_int_arith_wide<0b010, "uaddwb", int_aarch64_sve_uaddwb>;
  defm UADDWT_ZZZ : sve2_wide_int_arith_wide<0b011, "uaddwt", int_aarch64_sve_uaddwt>;
  defm SSUBWB_ZZZ : sve2_wide_int_arith_wide<0b100, "ssubwb", int_aarch64_sve_ssubwb>;
  defm SSUBWT_ZZZ : sve2_wide_int_arith_wide<0b101, "ssubwt", int_aarch64_sve_ssubwt>;
  defm USUBWB_ZZZ : sve2_wide_int_arith_wide<0b110, "usubwb", int_aarch64_sve_usubwb>;
  defm USUBWT_ZZZ : sve2_wide_int_arith_wide<0b111, "usubwt", int_aarch64_sve_usubwt>;

  // SVE2 integer multiply long
  defm SQDMULLB_ZZZ : sve2_wide_int_arith_long<0b11000, "sqdmullb", int_aarch64_sve_sqdmullb>;
  defm SQDMULLT_ZZZ : sve2_wide_int_arith_long<0b11001, "sqdmullt", int_aarch64_sve_sqdmullt>;
  defm SMULLB_ZZZ   : sve2_wide_int_arith_long<0b11100, "smullb",   int_aarch64_sve_smullb>;
  defm SMULLT_ZZZ   : sve2_wide_int_arith_long<0b11101, "smullt",   int_aarch64_sve_smullt>;
  defm UMULLB_ZZZ   : sve2_wide_int_arith_long<0b11110, "umullb",   int_aarch64_sve_umullb>;
  defm UMULLT_ZZZ   : sve2_wide_int_arith_long<0b11111, "umullt",   int_aarch64_sve_umullt>;
  defm PMULLB_ZZZ   : sve2_pmul_long<0b0, "pmullb", int_aarch64_sve_pmullb_pair>;
  defm PMULLT_ZZZ   : sve2_pmul_long<0b1, "pmullt", int_aarch64_sve_pmullt_pair>;

  // SVE2 bitwise shift and insert
  defm SRI_ZZI : sve2_int_bin_shift_imm_right<0b0, "sri", AArch64vsri>;
  defm SLI_ZZI : sve2_int_bin_shift_imm_left< 0b1, "sli", AArch64vsli>;

  // SVE2 bitwise shift right and accumulate
  defm SSRA_ZZI  : sve2_int_bin_accum_shift_imm_right<0b00, "ssra",  AArch64ssra>;
  defm USRA_ZZI  : sve2_int_bin_accum_shift_imm_right<0b01, "usra",  AArch64usra>;
  defm SRSRA_ZZI : sve2_int_bin_accum_shift_imm_right<0b10, "srsra", int_aarch64_sve_srsra, int_aarch64_sve_srshr>;
  defm URSRA_ZZI : sve2_int_bin_accum_shift_imm_right<0b11, "ursra", int_aarch64_sve_ursra, AArch64urshri_p>;

  // SVE2 complex integer add
  defm CADD_ZZI   : sve2_int_cadd<0b0, "cadd",   int_aarch64_sve_cadd_x>;
  defm SQCADD_ZZI : sve2_int_cadd<0b1, "sqcadd", int_aarch64_sve_sqcadd_x>;

  // SVE2 integer absolute difference and accumulate
  defm SABA_ZZZ : sve2_int_absdiff_accum<0b0, "saba", AArch64saba>;
  defm UABA_ZZZ : sve2_int_absdiff_accum<0b1, "uaba", AArch64uaba>;

  // SVE2 integer absolute difference and accumulate long
  defm SABALB_ZZZ : sve2_int_absdiff_accum_long<0b00, "sabalb", int_aarch64_sve_sabalb>;
  defm SABALT_ZZZ : sve2_int_absdiff_accum_long<0b01, "sabalt", int_aarch64_sve_sabalt>;
  defm UABALB_ZZZ : sve2_int_absdiff_accum_long<0b10, "uabalb", int_aarch64_sve_uabalb>;
  defm UABALT_ZZZ : sve2_int_absdiff_accum_long<0b11, "uabalt", int_aarch64_sve_uabalt>;

  // SVE2 integer add/subtract long with carry
  defm ADCLB_ZZZ : sve2_int_addsub_long_carry<0b00, "adclb", int_aarch64_sve_adclb>;
  defm ADCLT_ZZZ : sve2_int_addsub_long_carry<0b01, "adclt", int_aarch64_sve_adclt>;
  defm SBCLB_ZZZ : sve2_int_addsub_long_carry<0b10, "sbclb", int_aarch64_sve_sbclb>;
  defm SBCLT_ZZZ : sve2_int_addsub_long_carry<0b11, "sbclt", int_aarch64_sve_sbclt>;

  // SVE2 bitwise shift right narrow (bottom)
  defm SQSHRUNB_ZZI  : sve2_int_bin_shift_imm_right_narrow_bottom<0b000, "sqshrunb",  int_aarch64_sve_sqshrunb>;
  defm SQRSHRUNB_ZZI : sve2_int_bin_shift_imm_right_narrow_bottom<0b001, "sqrshrunb", int_aarch64_sve_sqrshrunb>;
  defm SHRNB_ZZI     : sve2_int_bin_shift_imm_right_narrow_bottom<0b010, "shrnb",     int_aarch64_sve_shrnb>;
  defm RSHRNB_ZZI    : sve2_int_bin_shift_imm_right_narrow_bottom<0b011, "rshrnb",    AArch64rshrnb_pf>;
  defm SQSHRNB_ZZI   : sve2_int_bin_shift_imm_right_narrow_bottom<0b100, "sqshrnb",   int_aarch64_sve_sqshrnb>;
  defm SQRSHRNB_ZZI  : sve2_int_bin_shift_imm_right_narrow_bottom<0b101, "sqrshrnb",  int_aarch64_sve_sqrshrnb>;
  defm UQSHRNB_ZZI   : sve2_int_bin_shift_imm_right_narrow_bottom<0b110, "uqshrnb",   int_aarch64_sve_uqshrnb>;
  defm UQRSHRNB_ZZI  : sve2_int_bin_shift_imm_right_narrow_bottom<0b111, "uqrshrnb",  int_aarch64_sve_uqrshrnb>;

  // SVE2 bitwise shift right narrow (top)
  defm SQSHRUNT_ZZI  : sve2_int_bin_shift_imm_right_narrow_top<0b000, "sqshrunt",  int_aarch64_sve_sqshrunt>;
  defm SQRSHRUNT_ZZI : sve2_int_bin_shift_imm_right_narrow_top<0b001, "sqrshrunt", int_aarch64_sve_sqrshrunt>;
  defm SHRNT_ZZI     : sve2_int_bin_shift_imm_right_narrow_top<0b010, "shrnt",     int_aarch64_sve_shrnt>;
  defm RSHRNT_ZZI    : sve2_int_bin_shift_imm_right_narrow_top<0b011, "rshrnt",    int_aarch64_sve_rshrnt>;
  defm SQSHRNT_ZZI   : sve2_int_bin_shift_imm_right_narrow_top<0b100, "sqshrnt",   int_aarch64_sve_sqshrnt>;
  defm SQRSHRNT_ZZI  : sve2_int_bin_shift_imm_right_narrow_top<0b101, "sqrshrnt",  int_aarch64_sve_sqrshrnt>;
  defm UQSHRNT_ZZI   : sve2_int_bin_shift_imm_right_narrow_top<0b110, "uqshrnt",   int_aarch64_sve_uqshrnt>;
  defm UQRSHRNT_ZZI  : sve2_int_bin_shift_imm_right_narrow_top<0b111, "uqrshrnt",  int_aarch64_sve_uqrshrnt>;

  // SVE2 integer add/subtract narrow high part (bottom)
  defm ADDHNB_ZZZ  : sve2_int_addsub_narrow_high_bottom<0b00, "addhnb",  int_aarch64_sve_addhnb>;
  defm RADDHNB_ZZZ : sve2_int_addsub_narrow_high_bottom<0b01, "raddhnb", int_aarch64_sve_raddhnb>;
  defm SUBHNB_ZZZ  : sve2_int_addsub_narrow_high_bottom<0b10, "subhnb",  int_aarch64_sve_subhnb>;
  defm RSUBHNB_ZZZ : sve2_int_addsub_narrow_high_bottom<0b11, "rsubhnb", int_aarch64_sve_rsubhnb>;

  // SVE2 integer add/subtract narrow high part (top)
  defm ADDHNT_ZZZ  : sve2_int_addsub_narrow_high_top<0b00, "addhnt",  int_aarch64_sve_addhnt>;
  defm RADDHNT_ZZZ : sve2_int_addsub_narrow_high_top<0b01, "raddhnt", int_aarch64_sve_raddhnt>;
  defm SUBHNT_ZZZ  : sve2_int_addsub_narrow_high_top<0b10, "subhnt",  int_aarch64_sve_subhnt>;
  defm RSUBHNT_ZZZ : sve2_int_addsub_narrow_high_top<0b11, "rsubhnt", int_aarch64_sve_rsubhnt>;

  // SVE2 saturating extract narrow (bottom)
  defm SQXTNB_ZZ  : sve2_int_sat_extract_narrow_bottom<0b00, "sqxtnb",  int_aarch64_sve_sqxtnb>;
  defm UQXTNB_ZZ  : sve2_int_sat_extract_narrow_bottom<0b01, "uqxtnb",  int_aarch64_sve_uqxtnb>;
  defm SQXTUNB_ZZ : sve2_int_sat_extract_narrow_bottom<0b10, "sqxtunb", int_aarch64_sve_sqxtunb>;

  // SVE2 saturating extract narrow (top)
  defm SQXTNT_ZZ  : sve2_int_sat_extract_narrow_top<0b00, "sqxtnt",  int_aarch64_sve_sqxtnt>;
  defm UQXTNT_ZZ  : sve2_int_sat_extract_narrow_top<0b01, "uqxtnt",  int_aarch64_sve_uqxtnt>;
  defm SQXTUNT_ZZ : sve2_int_sat_extract_narrow_top<0b10, "sqxtunt", int_aarch64_sve_sqxtunt>;
} // End HasSVE2orSME

let Predicates = [HasSVE2] in {
  // SVE2 character match
  defm MATCH_PPzZZ  : sve2_char_match<0b0, "match",  int_aarch64_sve_match>;
  defm NMATCH_PPzZZ : sve2_char_match<0b1, "nmatch", int_aarch64_sve_nmatch>;
} // End HasSVE2

let Predicates = [HasSVE2orSME] in {
  // SVE2 bitwise exclusive-or interleaved
  defm EORBT_ZZZ : sve2_bitwise_xor_interleaved<0b0, "eorbt", int_aarch64_sve_eorbt>;
  defm EORTB_ZZZ : sve2_bitwise_xor_interleaved<0b1, "eortb", int_aarch64_sve_eortb>;

  // SVE2 bitwise shift left long
  defm SSHLLB_ZZI : sve2_bitwise_shift_left_long<0b00, "sshllb", int_aarch64_sve_sshllb>;
  defm SSHLLT_ZZI : sve2_bitwise_shift_left_long<0b01, "sshllt", int_aarch64_sve_sshllt>;
  defm USHLLB_ZZI : sve2_bitwise_shift_left_long<0b10, "ushllb", int_aarch64_sve_ushllb>;
  defm USHLLT_ZZI : sve2_bitwise_shift_left_long<0b11, "ushllt", int_aarch64_sve_ushllt>;

  // SVE2 integer add/subtract interleaved long
  defm SADDLBT_ZZZ : sve2_misc_int_addsub_long_interleaved<0b00, "saddlbt", int_aarch64_sve_saddlbt>;
  defm SSUBLBT_ZZZ : sve2_misc_int_addsub_long_interleaved<0b10, "ssublbt", int_aarch64_sve_ssublbt>;
  defm SSUBLTB_ZZZ : sve2_misc_int_addsub_long_interleaved<0b11, "ssubltb", int_aarch64_sve_ssubltb>;
} // End HasSVE2orSME

let Predicates = [HasSVE2] in {
  // SVE2 histogram generation (segment)
  def HISTSEG_ZZZ : sve2_hist_gen_segment<"histseg", int_aarch64_sve_histseg>;

  // SVE2 histogram generation (vector)
  defm HISTCNT_ZPzZZ : sve2_hist_gen_vector<"histcnt", int_aarch64_sve_histcnt>;
} // End HasSVE2

let Predicates = [HasSVE2orSME] in {
  // SVE2 floating-point base 2 logarithm as integer
  defm FLOGB_ZPmZ : sve2_fp_flogb<"flogb", "FLOGB_ZPZZ", int_aarch64_sve_flogb>;
}

let Predicates = [HasSVE2orSME, UseExperimentalZeroingPseudos] in {
  defm FLOGB_ZPZZ : sve2_fp_un_pred_zeroing_hsd<int_aarch64_sve_flogb>;
} // End HasSVE2orSME, UseExperimentalZeroingPseudos

let Predicates = [HasSVE2orSME] in {
  // SVE2 floating-point convert precision
  defm FCVTXNT_ZPmZ : sve2_fp_convert_down_odd_rounding_top<"fcvtxnt", "int_aarch64_sve_fcvtxnt">;
  defm FCVTX_ZPmZ   : sve2_fp_convert_down_odd_rounding<"fcvtx",       "int_aarch64_sve_fcvtx">;
  defm FCVTNT_ZPmZ  : sve2_fp_convert_down_narrow<"fcvtnt",            "int_aarch64_sve_fcvtnt">;
  defm FCVTLT_ZPmZ  : sve2_fp_convert_up_long<"fcvtlt",                "int_aarch64_sve_fcvtlt">;

  // SVE2 floating-point pairwise operations
  defm FADDP_ZPmZZ   : sve2_fp_pairwise_pred<0b000, "faddp",   int_aarch64_sve_faddp>;
  defm FMAXNMP_ZPmZZ : sve2_fp_pairwise_pred<0b100, "fmaxnmp", int_aarch64_sve_fmaxnmp>;
  defm FMINNMP_ZPmZZ : sve2_fp_pairwise_pred<0b101, "fminnmp", int_aarch64_sve_fminnmp>;
  defm FMAXP_ZPmZZ   : sve2_fp_pairwise_pred<0b110, "fmaxp",   int_aarch64_sve_fmaxp>;
  defm FMINP_ZPmZZ   : sve2_fp_pairwise_pred<0b111, "fminp",   int_aarch64_sve_fminp>;

  // SVE2 floating-point multiply-add long (indexed)
  defm FMLALB_ZZZI_SHH : sve2_fp_mla_long_by_indexed_elem<0b000, "fmlalb", nxv4f32, nxv8f16, int_aarch64_sve_fmlalb_lane>;
  defm FMLALT_ZZZI_SHH : sve2_fp_mla_long_by_indexed_elem<0b001, "fmlalt", nxv4f32, nxv8f16, int_aarch64_sve_fmlalt_lane>;
  defm FMLSLB_ZZZI_SHH : sve2_fp_mla_long_by_indexed_elem<0b010, "fmlslb", nxv4f32, nxv8f16, int_aarch64_sve_fmlslb_lane>;
  defm FMLSLT_ZZZI_SHH : sve2_fp_mla_long_by_indexed_elem<0b011, "fmlslt", nxv4f32, nxv8f16, int_aarch64_sve_fmlslt_lane>;

  // SVE2 floating-point multiply-add long
  defm FMLALB_ZZZ_SHH : sve2_fp_mla_long<0b000, "fmlalb", nxv4f32, nxv8f16, int_aarch64_sve_fmlalb>;
  defm FMLALT_ZZZ_SHH : sve2_fp_mla_long<0b001, "fmlalt", nxv4f32, nxv8f16, int_aarch64_sve_fmlalt>;
  defm FMLSLB_ZZZ_SHH : sve2_fp_mla_long<0b010, "fmlslb", nxv4f32, nxv8f16, int_aarch64_sve_fmlslb>;
  defm FMLSLT_ZZZ_SHH : sve2_fp_mla_long<0b011, "fmlslt", nxv4f32, nxv8f16, int_aarch64_sve_fmlslt>;

  // SVE2 bitwise ternary operations
  defm EOR3_ZZZZ  : sve2_int_bitwise_ternary_op<0b000, "eor3",  AArch64eor3>;
  defm BCAX_ZZZZ  : sve2_int_bitwise_ternary_op<0b010, "bcax",  AArch64bcax>;
  defm BSL_ZZZZ   : sve2_int_bitwise_ternary_op<0b001, "bsl",   AArch64bsl>;
  defm BSL1N_ZZZZ : sve2_int_bitwise_ternary_op<0b011, "bsl1n", int_aarch64_sve_bsl1n>;
  defm BSL2N_ZZZZ : sve2_int_bitwise_ternary_op<0b101, "bsl2n", int_aarch64_sve_bsl2n>;
  defm NBSL_ZZZZ  : sve2_int_bitwise_ternary_op<0b111, "nbsl",  AArch64nbsl>;

  // SVE2 bitwise xor and rotate right by immediate
  defm XAR_ZZZI : sve2_int_rotate_right_imm<"xar", int_aarch64_sve_xar>;

  // SVE2 extract vector (immediate offset, constructive)
  def EXT_ZZI_B : sve2_int_perm_extract_i_cons<"ext">;
} // End HasSVE2orSME

let Predicates = [HasSVE2] in {
  // SVE2 non-temporal gather loads
  defm LDNT1SB_ZZR_S : sve2_mem_gldnt_vs_32_ptrs<0b00000, "ldnt1sb", AArch64ldnt1s_gather_z, nxv4i8>;
  defm LDNT1B_ZZR_S  : sve2_mem_gldnt_vs_32_ptrs<0b00001, "ldnt1b",  AArch64ldnt1_gather_z,  nxv4i8>;
  defm LDNT1SH_ZZR_S : sve2_mem_gldnt_vs_32_ptrs<0b00100, "ldnt1sh", AArch64ldnt1s_gather_z, nxv4i16>;
  defm LDNT1H_ZZR_S  : sve2_mem_gldnt_vs_32_ptrs<0b00101, "ldnt1h",  AArch64ldnt1_gather_z,  nxv4i16>;
  defm LDNT1W_ZZR_S  : sve2_mem_gldnt_vs_32_ptrs<0b01001, "ldnt1w",  AArch64ldnt1_gather_z,  nxv4i32>;

  defm LDNT1SB_ZZR_D : sve2_mem_gldnt_vs_64_ptrs<0b10000, "ldnt1sb", AArch64ldnt1s_gather_z, nxv2i8>;
  defm LDNT1B_ZZR_D  : sve2_mem_gldnt_vs_64_ptrs<0b10010, "ldnt1b",  AArch64ldnt1_gather_z,  nxv2i8>;
  defm LDNT1SH_ZZR_D : sve2_mem_gldnt_vs_64_ptrs<0b10100, "ldnt1sh", AArch64ldnt1s_gather_z, nxv2i16>;
  defm LDNT1H_ZZR_D  : sve2_mem_gldnt_vs_64_ptrs<0b10110, "ldnt1h",  AArch64ldnt1_gather_z,  nxv2i16>;
  defm LDNT1SW_ZZR_D : sve2_mem_gldnt_vs_64_ptrs<0b11000, "ldnt1sw", AArch64ldnt1s_gather_z, nxv2i32>;
  defm LDNT1W_ZZR_D  : sve2_mem_gldnt_vs_64_ptrs<0b11010, "ldnt1w",  AArch64ldnt1_gather_z,  nxv2i32>;
  defm LDNT1D_ZZR_D  : sve2_mem_gldnt_vs_64_ptrs<0b11110, "ldnt1d",  AArch64ldnt1_gather_z,  nxv2i64>;
} // End HasSVE2

let Predicates = [HasSVE2orSME] in {
  // SVE2 vector splice (constructive)
  defm SPLICE_ZPZZ : sve2_int_perm_splice_cons<"splice">;
} // End HasSVE2orSME

let Predicates = [HasSVE2] in {
  // SVE2 non-temporal scatter stores
  defm STNT1B_ZZR_S : sve2_mem_sstnt_vs_32_ptrs<0b001, "stnt1b", AArch64stnt1_scatter, nxv4i8>;
  defm STNT1H_ZZR_S : sve2_mem_sstnt_vs_32_ptrs<0b011, "stnt1h", AArch64stnt1_scatter, nxv4i16>;
  defm STNT1W_ZZR_S : sve2_mem_sstnt_vs_32_ptrs<0b101, "stnt1w", AArch64stnt1_scatter, nxv4i32>;

  defm STNT1B_ZZR_D : sve2_mem_sstnt_vs_64_ptrs<0b000, "stnt1b", AArch64stnt1_scatter, nxv2i8>;
  defm STNT1H_ZZR_D : sve2_mem_sstnt_vs_64_ptrs<0b010, "stnt1h", AArch64stnt1_scatter, nxv2i16>;
  defm STNT1W_ZZR_D : sve2_mem_sstnt_vs_64_ptrs<0b100, "stnt1w", AArch64stnt1_scatter, nxv2i32>;
  defm STNT1D_ZZR_D : sve2_mem_sstnt_vs_64_ptrs<0b110, "stnt1d", AArch64stnt1_scatter, nxv2i64>;
} // End HasSVE2

let Predicates = [HasSVE2orSME] in {
  // SVE2 table lookup (three sources)
  defm TBL_ZZZZ : sve2_int_perm_tbl<"tbl", int_aarch64_sve_tbl2>;
  defm TBX_ZZZ  : sve2_int_perm_tbx<"tbx", 0b01, int_aarch64_sve_tbx>;

  // SVE2 integer compare scalar count and limit
  defm WHILEGE_PWW : sve_int_while4_rr<0b000, "whilege", int_aarch64_sve_whilege, null_frag>;
  defm WHILEGT_PWW : sve_int_while4_rr<0b001, "whilegt", int_aarch64_sve_whilegt, int_aarch64_sve_whilelt>;
  defm WHILEHS_PWW : sve_int_while4_rr<0b100, "whilehs", int_aarch64_sve_whilehs, null_frag>;
  defm WHILEHI_PWW : sve_int_while4_rr<0b101, "whilehi", int_aarch64_sve_whilehi, int_aarch64_sve_whilelo>;

  defm WHILEGE_PXX : sve_int_while8_rr<0b000, "whilege", int_aarch64_sve_whilege, null_frag>;
  defm WHILEGT_PXX : sve_int_while8_rr<0b001, "whilegt", int_aarch64_sve_whilegt, int_aarch64_sve_whilelt>;
  defm WHILEHS_PXX : sve_int_while8_rr<0b100, "whilehs", int_aarch64_sve_whilehs, null_frag>;
  defm WHILEHI_PXX : sve_int_while8_rr<0b101, "whilehi", int_aarch64_sve_whilehi, int_aarch64_sve_whilelo>;

  // SVE2 pointer conflict compare
  defm WHILEWR_PXX : sve2_int_while_rr<0b0, "whilewr", "int_aarch64_sve_whilewr">;
  defm WHILERW_PXX : sve2_int_while_rr<0b1, "whilerw", "int_aarch64_sve_whilerw">;
} // End HasSVE2orSME

let Predicates = [HasSVE2AES] in {
  // SVE2 crypto destructive binary operations
  defm AESE_ZZZ_B : sve2_crypto_des_bin_op<0b00, "aese", ZPR8, int_aarch64_sve_aese, nxv16i8>;
  defm AESD_ZZZ_B : sve2_crypto_des_bin_op<0b01, "aesd", ZPR8, int_aarch64_sve_aesd, nxv16i8>;

  // SVE2 crypto unary operations
  defm AESMC_ZZ_B  : sve2_crypto_unary_op<0b0, "aesmc",  int_aarch64_sve_aesmc>;
  defm AESIMC_ZZ_B : sve2_crypto_unary_op<0b1, "aesimc", int_aarch64_sve_aesimc>;

  // PMULLB and PMULLT instructions which operate with 64-bit source and
  // 128-bit destination elements are enabled with crypto extensions, similar
  // to NEON PMULL2 instruction.
  defm PMULLB_ZZZ_Q : sve2_wide_int_arith_pmul<0b00, 0b11010, "pmullb", int_aarch64_sve_pmullb_pair>;
  defm PMULLT_ZZZ_Q : sve2_wide_int_arith_pmul<0b00, 0b11011, "pmullt", int_aarch64_sve_pmullt_pair>;
} // End HasSVE2AES

let Predicates = [HasSVE2SM4] in {
  // SVE2 crypto constructive binary operations
  defm SM4EKEY_ZZZ_S : sve2_crypto_cons_bin_op<0b0, "sm4ekey", ZPR32, int_aarch64_sve_sm4ekey, nxv4i32>;
  // SVE2 crypto destructive binary operations
  defm SM4E_ZZZ_S : sve2_crypto_des_bin_op<0b10, "sm4e", ZPR32, int_aarch64_sve_sm4e, nxv4i32>;
} // End HasSVE2SM4

let Predicates = [HasSVE2SHA3] in {
  // SVE2 crypto constructive binary operations
  defm RAX1_ZZZ_D : sve2_crypto_cons_bin_op<0b1, "rax1", ZPR64, int_aarch64_sve_rax1, nxv2i64>;
} // End HasSVE2SHA3

let Predicates = [HasSVE2BitPerm] in {
  // SVE2 bitwise permute
  defm BEXT_ZZZ : sve2_misc_bitwise<0b1100, "bext", int_aarch64_sve_bext_x>;
  defm BDEP_ZZZ : sve2_misc_bitwise<0b1101, "bdep", int_aarch64_sve_bdep_x>;
  defm BGRP_ZZZ : sve2_misc_bitwise<0b1110, "bgrp", int_aarch64_sve_bgrp_x>;
} // End HasSVE2BitPerm

//===----------------------------------------------------------------------===//
// SME or SVE2.1 instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasSVE2p1_or_HasSME] in {
defm REVD_ZPmZ : sve2_int_perm_revd<"revd", AArch64revd_mt>;

defm SCLAMP_ZZZ : sve2_clamp<"sclamp", 0b0, AArch64sclamp>;
defm UCLAMP_ZZZ : sve2_clamp<"uclamp", 0b1, AArch64uclamp>;

defm PSEL_PPPRI : sve2_int_perm_sel_p<"psel", int_aarch64_sve_psel>;
} // End HasSVE2p1_or_HasSME

//===----------------------------------------------------------------------===//
// SME2 or SVE2.1 instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasSVE2p1_or_HasSME2] in {
defm FCLAMP_ZZZ : sve2p1_fclamp<"fclamp", AArch64fclamp>;

defm FDOT_ZZZ_S  : sve_float_dot<0b0, 0b0, ZPR32, ZPR16, "fdot", nxv8f16, int_aarch64_sve_fdot_x2>;
defm FDOT_ZZZI_S : sve_float_dot_indexed<0b0, 0b00, ZPR16, ZPR3b16, "fdot", nxv8f16, int_aarch64_sve_fdot_lane_x2>;

defm BFMLSLB_ZZZ_S : sve2_fp_mla_long<0b110, "bfmlslb", nxv4f32, nxv8bf16, int_aarch64_sve_bfmlslb>;
defm BFMLSLT_ZZZ_S : sve2_fp_mla_long<0b111, "bfmlslt", nxv4f32, nxv8bf16, int_aarch64_sve_bfmlslt>;
defm BFMLSLB_ZZZI_S : sve2_fp_mla_long_by_indexed_elem<0b110, "bfmlslb", nxv4f32, nxv8bf16, int_aarch64_sve_bfmlslb_lane>;
defm BFMLSLT_ZZZI_S : sve2_fp_mla_long_by_indexed_elem<0b111, "bfmlslt", nxv4f32, nxv8bf16, int_aarch64_sve_bfmlslt_lane>;

defm SDOT_ZZZ_HtoS  : sve2p1_two_way_dot_vv<"sdot", 0b0, int_aarch64_sve_sdot_x2>;
defm UDOT_ZZZ_HtoS  : sve2p1_two_way_dot_vv<"udot", 0b1, int_aarch64_sve_udot_x2>;
defm SDOT_ZZZI_HtoS : sve2p1_two_way_dot_vvi<"sdot", 0b0, int_aarch64_sve_sdot_lane_x2>;
defm UDOT_ZZZI_HtoS : sve2p1_two_way_dot_vvi<"udot", 0b1, int_aarch64_sve_udot_lane_x2>;

defm CNTP_XCI : sve2p1_pcount_pn<"cntp", 0b000>;
defm PEXT_PCI : sve2p1_pred_as_ctr_to_mask<"pext", int_aarch64_sve_pext>;
defm PEXT_2PCI : sve2p1_pred_as_ctr_to_mask_pair<"pext">;
defm PTRUE_C  : sve2p1_ptrue_pn<"ptrue">;

defm SQCVTN_Z2Z_StoH  : sve2p1_multi_vec_extract_narrow<"sqcvtn", 0b00, int_aarch64_sve_sqcvtn_x2>;
defm UQCVTN_Z2Z_StoH  : sve2p1_multi_vec_extract_narrow<"uqcvtn", 0b01, int_aarch64_sve_uqcvtn_x2>;
defm SQCVTUN_Z2Z_StoH : sve2p1_multi_vec_extract_narrow<"sqcvtun", 0b10, int_aarch64_sve_sqcvtun_x2>;
defm SQRSHRN_Z2ZI_StoH  : sve2p1_multi_vec_shift_narrow<"sqrshrn", 0b101, int_aarch64_sve_sqrshrn_x2>;
defm UQRSHRN_Z2ZI_StoH  : sve2p1_multi_vec_shift_narrow<"uqrshrn", 0b111, int_aarch64_sve_uqrshrn_x2>;
defm SQRSHRUN_Z2ZI_StoH : sve2p1_multi_vec_shift_narrow<"sqrshrun", 0b001, int_aarch64_sve_sqrshrun_x2>;

// Load to two registers
defm LD1B_2Z       : sve2p1_mem_cld_ss_2z<"ld1b", 0b00, 0b0, ZZ_b_mul_r, GPR64shifted8, ZZ_b_strided_and_contiguous>;
defm LD1H_2Z       : sve2p1_mem_cld_ss_2z<"ld1h", 0b01, 0b0, ZZ_h_mul_r, GPR64shifted16, ZZ_h_strided_and_contiguous>;
defm LD1W_2Z       : sve2p1_mem_cld_ss_2z<"ld1w", 0b10, 0b0, ZZ_s_mul_r, GPR64shifted32, ZZ_s_strided_and_contiguous>;
defm LD1D_2Z       : sve2p1_mem_cld_ss_2z<"ld1d", 0b11, 0b0, ZZ_d_mul_r, GPR64shifted64, ZZ_d_strided_and_contiguous>;
defm LD1B_2Z_IMM   : sve2p1_mem_cld_si_2z<"ld1b", 0b00, 0b0, ZZ_b_mul_r, ZZ_b_strided_and_contiguous>;
defm LD1H_2Z_IMM   : sve2p1_mem_cld_si_2z<"ld1h", 0b01, 0b0, ZZ_h_mul_r, ZZ_h_strided_and_contiguous>;
defm LD1W_2Z_IMM   : sve2p1_mem_cld_si_2z<"ld1w", 0b10, 0b0, ZZ_s_mul_r, ZZ_s_strided_and_contiguous>;
defm LD1D_2Z_IMM   : sve2p1_mem_cld_si_2z<"ld1d", 0b11, 0b0, ZZ_d_mul_r, ZZ_d_strided_and_contiguous>;
defm LDNT1B_2Z     : sve2p1_mem_cld_ss_2z<"ldnt1b", 0b00, 0b1, ZZ_b_mul_r, GPR64shifted8, ZZ_b_strided_and_contiguous>;
defm LDNT1H_2Z     : sve2p1_mem_cld_ss_2z<"ldnt1h", 0b01, 0b1, ZZ_h_mul_r, GPR64shifted16, ZZ_h_strided_and_contiguous>;
defm LDNT1W_2Z     : sve2p1_mem_cld_ss_2z<"ldnt1w", 0b10, 0b1, ZZ_s_mul_r, GPR64shifted32, ZZ_s_strided_and_contiguous>;
defm LDNT1D_2Z     : sve2p1_mem_cld_ss_2z<"ldnt1d", 0b11, 0b1, ZZ_d_mul_r, GPR64shifted64, ZZ_d_strided_and_contiguous>;
defm LDNT1B_2Z_IMM : sve2p1_mem_cld_si_2z<"ldnt1b", 0b00, 0b1, ZZ_b_mul_r, ZZ_b_strided_and_contiguous>;
defm LDNT1H_2Z_IMM : sve2p1_mem_cld_si_2z<"ldnt1h", 0b01, 0b1, ZZ_h_mul_r, ZZ_h_strided_and_contiguous>;
defm LDNT1W_2Z_IMM : sve2p1_mem_cld_si_2z<"ldnt1w", 0b10, 0b1, ZZ_s_mul_r, ZZ_s_strided_and_contiguous>;
defm LDNT1D_2Z_IMM : sve2p1_mem_cld_si_2z<"ldnt1d", 0b11, 0b1, ZZ_d_mul_r, ZZ_d_strided_and_contiguous>;

// Load to four registers
defm LD1B_4Z       : sve2p1_mem_cld_ss_4z<"ld1b", 0b00, 0b0, ZZZZ_b_mul_r, GPR64shifted8, ZZZZ_b_strided_and_contiguous>;
defm LD1H_4Z       : sve2p1_mem_cld_ss_4z<"ld1h", 0b01, 0b0, ZZZZ_h_mul_r, GPR64shifted16, ZZZZ_h_strided_and_contiguous>;
defm LD1W_4Z       : sve2p1_mem_cld_ss_4z<"ld1w", 0b10, 0b0, ZZZZ_s_mul_r, GPR64shifted32, ZZZZ_s_strided_and_contiguous>;
defm LD1D_4Z       : sve2p1_mem_cld_ss_4z<"ld1d", 0b11, 0b0, ZZZZ_d_mul_r, GPR64shifted64, ZZZZ_d_strided_and_contiguous>;
defm LD1B_4Z_IMM   : sve2p1_mem_cld_si_4z<"ld1b", 0b00, 0b0, ZZZZ_b_mul_r, ZZZZ_b_strided_and_contiguous>;
defm LD1H_4Z_IMM   : sve2p1_mem_cld_si_4z<"ld1h", 0b01, 0b0, ZZZZ_h_mul_r, ZZZZ_h_strided_and_contiguous>;
defm LD1W_4Z_IMM   : sve2p1_mem_cld_si_4z<"ld1w", 0b10, 0b0, ZZZZ_s_mul_r, ZZZZ_s_strided_and_contiguous>;
defm LD1D_4Z_IMM   : sve2p1_mem_cld_si_4z<"ld1d", 0b11, 0b0, ZZZZ_d_mul_r, ZZZZ_d_strided_and_contiguous>;
defm LDNT1B_4Z     : sve2p1_mem_cld_ss_4z<"ldnt1b", 0b00, 0b1, ZZZZ_b_mul_r, GPR64shifted8, ZZZZ_b_strided_and_contiguous>;
defm LDNT1H_4Z     : sve2p1_mem_cld_ss_4z<"ldnt1h", 0b01, 0b1, ZZZZ_h_mul_r, GPR64shifted16, ZZZZ_h_strided_and_contiguous>;
defm LDNT1W_4Z     : sve2p1_mem_cld_ss_4z<"ldnt1w", 0b10, 0b1, ZZZZ_s_mul_r, GPR64shifted32, ZZZZ_s_strided_and_contiguous>;
defm LDNT1D_4Z     : sve2p1_mem_cld_ss_4z<"ldnt1d", 0b11, 0b1, ZZZZ_d_mul_r, GPR64shifted64, ZZZZ_d_strided_and_contiguous>;
defm LDNT1B_4Z_IMM : sve2p1_mem_cld_si_4z<"ldnt1b", 0b00, 0b1, ZZZZ_b_mul_r, ZZZZ_b_strided_and_contiguous>;
defm LDNT1H_4Z_IMM : sve2p1_mem_cld_si_4z<"ldnt1h", 0b01, 0b1, ZZZZ_h_mul_r, ZZZZ_h_strided_and_contiguous>;
defm LDNT1W_4Z_IMM : sve2p1_mem_cld_si_4z<"ldnt1w", 0b10, 0b1, ZZZZ_s_mul_r, ZZZZ_s_strided_and_contiguous>;
defm LDNT1D_4Z_IMM : sve2p1_mem_cld_si_4z<"ldnt1d", 0b11, 0b1, ZZZZ_d_mul_r, ZZZZ_d_strided_and_contiguous>;

// Stores of two registers
def ST1B_2Z        : sve2p1_mem_cst_ss_2z<"st1b", 0b00, 0b0, ZZ_b_mul_r, GPR64shifted8>;
def ST1H_2Z        : sve2p1_mem_cst_ss_2z<"st1h", 0b01, 0b0, ZZ_h_mul_r, GPR64shifted16>;
def ST1W_2Z        : sve2p1_mem_cst_ss_2z<"st1w", 0b10, 0b0, ZZ_s_mul_r, GPR64shifted32>;
def ST1D_2Z        : sve2p1_mem_cst_ss_2z<"st1d", 0b11, 0b0, ZZ_d_mul_r, GPR64shifted64>;
defm ST1B_2Z_IMM   : sve2p1_mem_cst_si_2z<"st1b", 0b00, 0b0, ZZ_b_mul_r>;
defm ST1H_2Z_IMM   : sve2p1_mem_cst_si_2z<"st1h", 0b01, 0b0, ZZ_h_mul_r>;
defm ST1W_2Z_IMM   : sve2p1_mem_cst_si_2z<"st1w", 0b10, 0b0, ZZ_s_mul_r>;
defm ST1D_2Z_IMM   : sve2p1_mem_cst_si_2z<"st1d", 0b11, 0b0, ZZ_d_mul_r>;
def STNT1B_2Z      : sve2p1_mem_cst_ss_2z<"stnt1b", 0b00, 0b1, ZZ_b_mul_r, GPR64shifted8>;
def STNT1H_2Z      : sve2p1_mem_cst_ss_2z<"stnt1h", 0b01, 0b1, ZZ_h_mul_r, GPR64shifted16>;
def STNT1W_2Z      : sve2p1_mem_cst_ss_2z<"stnt1w", 0b10, 0b1, ZZ_s_mul_r, GPR64shifted32>;
def STNT1D_2Z      : sve2p1_mem_cst_ss_2z<"stnt1d", 0b11, 0b1, ZZ_d_mul_r, GPR64shifted64>;
defm STNT1B_2Z_IMM : sve2p1_mem_cst_si_2z<"stnt1b", 0b00, 0b1, ZZ_b_mul_r>;
defm STNT1H_2Z_IMM : sve2p1_mem_cst_si_2z<"stnt1h", 0b01, 0b1, ZZ_h_mul_r>;
defm STNT1W_2Z_IMM : sve2p1_mem_cst_si_2z<"stnt1w", 0b10, 0b1, ZZ_s_mul_r>;
defm STNT1D_2Z_IMM : sve2p1_mem_cst_si_2z<"stnt1d", 0b11, 0b1, ZZ_d_mul_r>;

// Stores of four registers
def ST1B_4Z        : sve2p1_mem_cst_ss_4z<"st1b", 0b00, 0b0, ZZZZ_b_mul_r, GPR64shifted8>;
def ST1H_4Z        : sve2p1_mem_cst_ss_4z<"st1h", 0b01, 0b0, ZZZZ_h_mul_r, GPR64shifted16>;
def ST1W_4Z        : sve2p1_mem_cst_ss_4z<"st1w", 0b10, 0b0, ZZZZ_s_mul_r, GPR64shifted32>;
def ST1D_4Z        : sve2p1_mem_cst_ss_4z<"st1d", 0b11, 0b0, ZZZZ_d_mul_r, GPR64shifted64>;
defm ST1B_4Z_IMM   : sve2p1_mem_cst_si_4z<"st1b", 0b00, 0b0, ZZZZ_b_mul_r>;
defm ST1H_4Z_IMM   : sve2p1_mem_cst_si_4z<"st1h", 0b01, 0b0, ZZZZ_h_mul_r>;
defm ST1W_4Z_IMM   : sve2p1_mem_cst_si_4z<"st1w", 0b10, 0b0, ZZZZ_s_mul_r>;
defm ST1D_4Z_IMM   : sve2p1_mem_cst_si_4z<"st1d", 0b11, 0b0, ZZZZ_d_mul_r>;
def STNT1B_4Z      : sve2p1_mem_cst_ss_4z<"stnt1b", 0b00, 0b1, ZZZZ_b_mul_r, GPR64shifted8>;
def STNT1H_4Z      : sve2p1_mem_cst_ss_4z<"stnt1h", 0b01, 0b1, ZZZZ_h_mul_r, GPR64shifted16>;
def STNT1W_4Z      : sve2p1_mem_cst_ss_4z<"stnt1w", 0b10, 0b1, ZZZZ_s_mul_r, GPR64shifted32>;
def STNT1D_4Z      : sve2p1_mem_cst_ss_4z<"stnt1d", 0b11, 0b1, ZZZZ_d_mul_r, GPR64shifted64>;
defm STNT1B_4Z_IMM : sve2p1_mem_cst_si_4z<"stnt1b", 0b00, 0b1, ZZZZ_b_mul_r>;
defm STNT1H_4Z_IMM : sve2p1_mem_cst_si_4z<"stnt1h", 0b01, 0b1, ZZZZ_h_mul_r>;
defm STNT1W_4Z_IMM : sve2p1_mem_cst_si_4z<"stnt1w", 0b10, 0b1, ZZZZ_s_mul_r>;
defm STNT1D_4Z_IMM : sve2p1_mem_cst_si_4z<"stnt1d", 0b11, 0b1, ZZZZ_d_mul_r>;

multiclass store_pn_x2<ValueType Ty, SDPatternOperator Store,
                        Instruction RegImmInst> {
  def : Pat<(Store Ty:$vec0, Ty:$vec1, aarch64svcount:$PNg, GPR64:$base),
            (RegImmInst (REG_SEQUENCE ZPR2Mul2, Ty:$vec0, zsub0, Ty:$vec1, zsub1),
                         PNR:$PNg, GPR64:$base, (i64 0))>;
}

// Stores of 2 consecutive vectors
defm : store_pn_x2<nxv16i8, int_aarch64_sve_st1_pn_x2, ST1B_2Z_IMM>;
defm : store_pn_x2<nxv8i16, int_aarch64_sve_st1_pn_x2, ST1H_2Z_IMM>;
defm : store_pn_x2<nxv4i32, int_aarch64_sve_st1_pn_x2, ST1W_2Z_IMM>;
defm : store_pn_x2<nxv2i64, int_aarch64_sve_st1_pn_x2, ST1D_2Z_IMM>;
defm : store_pn_x2<nxv16i8, int_aarch64_sve_stnt1_pn_x2, STNT1B_2Z_IMM>;
defm : store_pn_x2<nxv8i16, int_aarch64_sve_stnt1_pn_x2, STNT1H_2Z_IMM>;
defm : store_pn_x2<nxv4i32, int_aarch64_sve_stnt1_pn_x2, STNT1W_2Z_IMM>;
defm : store_pn_x2<nxv2i64, int_aarch64_sve_stnt1_pn_x2, STNT1D_2Z_IMM>;
defm : store_pn_x2<nxv8f16, int_aarch64_sve_st1_pn_x2, ST1H_2Z_IMM>;
defm : store_pn_x2<nxv8bf16, int_aarch64_sve_st1_pn_x2, ST1H_2Z_IMM>;
defm : store_pn_x2<nxv4f32, int_aarch64_sve_st1_pn_x2, ST1W_2Z_IMM>;
defm : store_pn_x2<nxv2f64, int_aarch64_sve_st1_pn_x2, ST1D_2Z_IMM>;
defm : store_pn_x2<nxv8f16, int_aarch64_sve_stnt1_pn_x2, STNT1H_2Z_IMM>;
defm : store_pn_x2<nxv8bf16, int_aarch64_sve_stnt1_pn_x2, STNT1H_2Z_IMM>;
defm : store_pn_x2<nxv4f32, int_aarch64_sve_stnt1_pn_x2, STNT1W_2Z_IMM>;
defm : store_pn_x2<nxv2f64, int_aarch64_sve_stnt1_pn_x2, STNT1D_2Z_IMM>;

multiclass store_pn_x4<ValueType Ty, SDPatternOperator Store,
                        Instruction RegImmInst> {
  def : Pat<(Store Ty:$vec0, Ty:$vec1, Ty:$vec2, Ty:$vec3, aarch64svcount:$PNg, GPR64:$base),
            (RegImmInst (REG_SEQUENCE ZPR4Mul4, Ty:$vec0, zsub0, Ty:$vec1, zsub1,
                                                Ty:$vec2, zsub2, Ty:$vec3, zsub3),
                        PNR:$PNg, GPR64:$base, (i64 0))>;
}

// Stores of 4 consecutive vectors
defm : store_pn_x4<nxv16i8, int_aarch64_sve_st1_pn_x4, ST1B_4Z_IMM>;
defm : store_pn_x4<nxv8i16, int_aarch64_sve_st1_pn_x4, ST1H_4Z_IMM>;
defm : store_pn_x4<nxv4i32, int_aarch64_sve_st1_pn_x4, ST1W_4Z_IMM>;
defm : store_pn_x4<nxv2i64, int_aarch64_sve_st1_pn_x4, ST1D_4Z_IMM>;
defm : store_pn_x4<nxv16i8, int_aarch64_sve_stnt1_pn_x4, STNT1B_4Z_IMM>;
defm : store_pn_x4<nxv8i16, int_aarch64_sve_stnt1_pn_x4, STNT1H_4Z_IMM>;
defm : store_pn_x4<nxv4i32, int_aarch64_sve_stnt1_pn_x4, STNT1W_4Z_IMM>;
defm : store_pn_x4<nxv2i64, int_aarch64_sve_stnt1_pn_x4, STNT1D_4Z_IMM>;
defm : store_pn_x4<nxv8f16, int_aarch64_sve_st1_pn_x4, ST1H_4Z_IMM>;
defm : store_pn_x4<nxv8bf16, int_aarch64_sve_st1_pn_x4, ST1H_4Z_IMM>;
defm : store_pn_x4<nxv4f32, int_aarch64_sve_st1_pn_x4, ST1W_4Z_IMM>;
defm : store_pn_x4<nxv2f64, int_aarch64_sve_st1_pn_x4, ST1D_4Z_IMM>;
defm : store_pn_x4<nxv8f16, int_aarch64_sve_stnt1_pn_x4, STNT1H_4Z_IMM>;
defm : store_pn_x4<nxv8bf16, int_aarch64_sve_stnt1_pn_x4, STNT1H_4Z_IMM>;
defm : store_pn_x4<nxv4f32, int_aarch64_sve_stnt1_pn_x4, STNT1W_4Z_IMM>;
defm : store_pn_x4<nxv2f64, int_aarch64_sve_stnt1_pn_x4, STNT1D_4Z_IMM>;

defm WHILEGE_2PXX : sve2p1_int_while_rr_pair<"whilege", 0b000>;
defm WHILEGT_2PXX : sve2p1_int_while_rr_pair<"whilegt", 0b001>;
defm WHILELT_2PXX : sve2p1_int_while_rr_pair<"whilelt", 0b010>;
defm WHILELE_2PXX : sve2p1_int_while_rr_pair<"whilele", 0b011>;
defm WHILEHS_2PXX : sve2p1_int_while_rr_pair<"whilehs", 0b100>;
defm WHILEHI_2PXX : sve2p1_int_while_rr_pair<"whilehi", 0b101>;
defm WHILELO_2PXX : sve2p1_int_while_rr_pair<"whilelo", 0b110>;
defm WHILELS_2PXX : sve2p1_int_while_rr_pair<"whilels", 0b111>;
defm WHILEGE_CXX  : sve2p1_int_while_rr_pn<"whilege", 0b000>;
defm WHILEGT_CXX  : sve2p1_int_while_rr_pn<"whilegt", 0b001>;
defm WHILELT_CXX  : sve2p1_int_while_rr_pn<"whilelt", 0b010>;
defm WHILELE_CXX  : sve2p1_int_while_rr_pn<"whilele", 0b011>;
defm WHILEHS_CXX  : sve2p1_int_while_rr_pn<"whilehs", 0b100>;
defm WHILEHI_CXX  : sve2p1_int_while_rr_pn<"whilehi", 0b101>;
defm WHILELO_CXX  : sve2p1_int_while_rr_pn<"whilelo", 0b110>;
defm WHILELS_CXX  : sve2p1_int_while_rr_pn<"whilels", 0b111>;
} // End HasSVE2p1_or_HasSME2

let Predicates = [HasSVEorSME] in {

// Aliases for existing SVE instructions for which predicate-as-counter are
// accepted as an operand to the instruction
def : InstAlias<"mov $Pd, $Pn",
               (ORR_PPzPP PPRorPNR8:$Pd, PPRorPNR8:$Pn, PPRorPNR8:$Pn, PPRorPNR8:$Pn), 0>;

def : InstAlias<"pfalse\t$Pd", (PFALSE PPRorPNR8:$Pd), 0>;

}

//===----------------------------------------------------------------------===//
// Non-widening BFloat16 to BFloat16 instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasSVE2orSME2, HasB16B16, UseExperimentalZeroingPseudos] in {
defm BFADD_ZPZZ : sve2p1_bf_2op_p_zds_zeroing<int_aarch64_sve_fadd>;
defm BFSUB_ZPZZ : sve2p1_bf_2op_p_zds_zeroing<int_aarch64_sve_fsub>;
defm BFMUL_ZPZZ : sve2p1_bf_2op_p_zds_zeroing<int_aarch64_sve_fmul>;
defm BFMAXNM_ZPZZ : sve2p1_bf_2op_p_zds_zeroing<int_aarch64_sve_fmaxnm>;
defm BFMINNM_ZPZZ : sve2p1_bf_2op_p_zds_zeroing<int_aarch64_sve_fminnm>;
defm BFMIN_ZPZZ : sve2p1_bf_2op_p_zds_zeroing<int_aarch64_sve_fmin>;
defm BFMAX_ZPZZ : sve2p1_bf_2op_p_zds_zeroing<int_aarch64_sve_fmax>;
} // HasSVE2orSME2, HasB16B16, UseExperimentalZeroingPseudos

let Predicates = [HasSVE2orSME2, HasB16B16] in {

defm BFMLA_ZPmZZ : sve_fp_3op_p_zds_a_bf<0b00, "bfmla", "BFMLA_ZPZZZ", AArch64fmla_m1>;
defm BFMLS_ZPmZZ : sve_fp_3op_p_zds_a_bf<0b01, "bfmls", "BFMLS_ZPZZZ", AArch64fmls_m1>;

defm BFMLA_ZPZZZ : sve_fp_3op_pred_bf<AArch64fmla_p>;
defm BFMLS_ZPZZZ : sve_fp_3op_pred_bf<AArch64fmls_p>;

defm BFMLA_ZZZI : sve2p1_fp_bfma_by_indexed_elem<"bfmla", 0b10, int_aarch64_sve_fmla_lane>;
defm BFMLS_ZZZI : sve2p1_fp_bfma_by_indexed_elem<"bfmls", 0b11, int_aarch64_sve_fmls_lane>;

defm BFADD_ZPmZZ : sve2p1_bf_2op_p_zds<0b0000, "bfadd", "BFADD_ZPZZ", AArch64fadd_m1, DestructiveBinaryComm>;
defm BFSUB_ZPmZZ : sve2p1_bf_2op_p_zds<0b0001, "bfsub", "BFSUB_ZPZZ", AArch64fsub_m1, DestructiveBinaryComm>;
defm BFMUL_ZPmZZ : sve2p1_bf_2op_p_zds<0b0010, "bfmul", "BFMUL_ZPZZ", AArch64fmul_m1, DestructiveBinaryComm>;

defm BFADD_ZZZ : sve2p1_bf_3op_u_zd<0b000, "bfadd", AArch64fadd>;
defm BFSUB_ZZZ : sve2p1_bf_3op_u_zd<0b001, "bfsub", AArch64fsub>;
defm BFMUL_ZZZ : sve2p1_bf_3op_u_zd<0b010, "bfmul", AArch64fmul>;

defm BFADD_ZPZZ : sve2p1_bf_bin_pred_zds<AArch64fadd_p>;
defm BFSUB_ZPZZ : sve2p1_bf_bin_pred_zds<AArch64fsub_p>;
defm BFMUL_ZPZZ : sve2p1_bf_bin_pred_zds<AArch64fmul_p>;


defm BFMAX_ZPmZZ : sve2p1_bf_2op_p_zds<0b0110, "bfmax", "BFMAX_ZPZZ", int_aarch64_sve_fmax, DestructiveBinaryComm>;
defm BFMIN_ZPmZZ : sve2p1_bf_2op_p_zds<0b0111, "bfmin", "BFMIN_ZPZZ", int_aarch64_sve_fmin, DestructiveBinaryComm>;

defm BFMAX_ZPZZ : sve2p1_bf_bin_pred_zds<AArch64fmax_p>;
defm BFMIN_ZPZZ : sve2p1_bf_bin_pred_zds<AArch64fmin_p>;


defm BFMAXNM_ZPmZZ : sve2p1_bf_2op_p_zds<0b0100, "bfmaxnm", "BFMAXNM_ZPZZ", int_aarch64_sve_fmaxnm, DestructiveBinaryComm>;
defm BFMINNM_ZPmZZ : sve2p1_bf_2op_p_zds<0b0101, "bfminnm", "BFMINNM_ZPZZ", int_aarch64_sve_fminnm, DestructiveBinaryComm>;

defm BFMAXNM_ZPZZ : sve2p1_bf_bin_pred_zds<AArch64fmaxnm_p>;
defm BFMINNM_ZPZZ : sve2p1_bf_bin_pred_zds<AArch64fminnm_p>;

defm BFMUL_ZZZI : sve2p1_fp_bfmul_by_indexed_elem<"bfmul", int_aarch64_sve_fmul_lane>;

defm BFCLAMP_ZZZ : sve2p1_bfclamp<"bfclamp", AArch64fclamp>;
} // End HasSVE2orSME2, HasB16B16


//===----------------------------------------------------------------------===//
// SME2.1 or SVE2.1 instructions
//===----------------------------------------------------------------------===//
let Predicates = [HasSVE2p1_or_HasSME2p1] in {
defm FADDQV   : sve2p1_fp_reduction_q<0b000, "faddqv", int_aarch64_sve_faddqv>;
defm FMAXNMQV : sve2p1_fp_reduction_q<0b100, "fmaxnmqv", int_aarch64_sve_fmaxnmqv>;
defm FMINNMQV : sve2p1_fp_reduction_q<0b101, "fminnmqv", int_aarch64_sve_fminnmqv>;
defm FMAXQV   : sve2p1_fp_reduction_q<0b110, "fmaxqv", int_aarch64_sve_fmaxqv>;
defm FMINQV   : sve2p1_fp_reduction_q<0b111, "fminqv", int_aarch64_sve_fminqv>;

defm DUPQ_ZZI : sve2p1_dupq<"dupq", int_aarch64_sve_dup_laneq>;
defm EXTQ_ZZI : sve2p1_extq<"extq", int_aarch64_sve_extq>;

defm PMOV_PZI : sve2p1_vector_to_pred<"pmov", int_aarch64_sve_pmov_to_pred_lane, int_aarch64_sve_pmov_to_pred_lane_zero>;
defm PMOV_ZIP : sve2p1_pred_to_vector<"pmov", int_aarch64_sve_pmov_to_vector_lane_merging, int_aarch64_sve_pmov_to_vector_lane_zeroing>;

defm ORQV_VPZ   : sve2p1_int_reduce_q<0b1100, "orqv", int_aarch64_sve_orqv>;
defm EORQV_VPZ  : sve2p1_int_reduce_q<0b1101, "eorqv", int_aarch64_sve_eorqv>;
defm ANDQV_VPZ  : sve2p1_int_reduce_q<0b1110, "andqv", int_aarch64_sve_andqv>;
defm ADDQV_VPZ  : sve2p1_int_reduce_q<0b0001, "addqv", int_aarch64_sve_addqv>;
defm SMAXQV_VPZ : sve2p1_int_reduce_q<0b0100, "smaxqv", int_aarch64_sve_smaxqv>;
defm UMAXQV_VPZ : sve2p1_int_reduce_q<0b0101, "umaxqv", int_aarch64_sve_umaxqv>;
defm SMINQV_VPZ : sve2p1_int_reduce_q<0b0110, "sminqv", int_aarch64_sve_sminqv>;
defm UMINQV_VPZ : sve2p1_int_reduce_q<0b0111, "uminqv", int_aarch64_sve_uminqv>;

defm ZIPQ1_ZZZ : sve2p1_permute_vec_elems_q<0b000, "zipq1", int_aarch64_sve_zipq1>;
defm ZIPQ2_ZZZ : sve2p1_permute_vec_elems_q<0b001, "zipq2", int_aarch64_sve_zipq2>;
defm UZPQ1_ZZZ : sve2p1_permute_vec_elems_q<0b010, "uzpq1", int_aarch64_sve_uzpq1>;
defm UZPQ2_ZZZ : sve2p1_permute_vec_elems_q<0b011, "uzpq2", int_aarch64_sve_uzpq2>;
defm TBXQ_ZZZ : sve2_int_perm_tbx<"tbxq", 0b10, int_aarch64_sve_tbxq>;
defm TBLQ_ZZZ  : sve2p1_tblq<"tblq", int_aarch64_sve_tblq>;
} // End HasSVE2p1_or_HasSME2p1

//===----------------------------------------------------------------------===//
// SVE2 FP8 instructions
//===----------------------------------------------------------------------===//
let Predicates = [HasSVE2orSME2, HasFP8] in {
// FP8 upconvert
defm F1CVT_ZZ     : sve2_fp8_cvt_single<0b0, 0b00, "f1cvt">;
defm F2CVT_ZZ     : sve2_fp8_cvt_single<0b0, 0b01, "f2cvt">;
defm BF1CVT_ZZ    : sve2_fp8_cvt_single<0b0, 0b10, "bf1cvt">;
defm BF2CVT_ZZ    : sve2_fp8_cvt_single<0b0, 0b11, "bf2cvt">;
defm F1CVTLT_ZZ   : sve2_fp8_cvt_single<0b1, 0b00, "f1cvtlt">;
defm F2CVTLT_ZZ   : sve2_fp8_cvt_single<0b1, 0b01, "f2cvtlt">;
defm BF1CVTLT_ZZ  : sve2_fp8_cvt_single<0b1, 0b10, "bf1cvtlt">;
defm BF2CVTLT_ZZ  : sve2_fp8_cvt_single<0b1, 0b11, "bf2cvtlt">;

// FP8 downconvert
defm FCVTN_Z2Z_HtoB  : sve2_fp8_down_cvt_single<0b00, "fcvtn", ZZ_h_mul_r>;
defm FCVTNB_Z2Z_StoB : sve2_fp8_down_cvt_single<0b01, "fcvtnb", ZZ_s_mul_r>;
defm BFCVTN_Z2Z_HtoB : sve2_fp8_down_cvt_single<0b10, "bfcvtn", ZZ_h_mul_r>;
defm FCVTNT_Z2Z_StoB : sve2_fp8_down_cvt_single<0b11, "fcvtnt", ZZ_s_mul_r>;
} // End HasSVE2orSME2, HasFP8

let Predicates = [HasSVE2orSME2, HasFAMINMAX] in {
// FP8 Arithmetic - Predicated Group
defm FAMIN_ZPmZ : sve_fp_2op_p_zds<0b1111, "famin", "", null_frag, DestructiveOther>;
defm FAMAX_ZPmZ : sve_fp_2op_p_zds<0b1110, "famax", "", null_frag, DestructiveOther>;
} // End HasSVE2orSME2, HasFAMINMAX

let Predicates = [HasSSVE_FP8FMA] in {
// FP8 Widening Multiply-Add Long - Indexed Group
def FMLALB_ZZZI : sve2_fp8_mla_long_by_indexed_elem<0b0, "fmlalb">;
def FMLALT_ZZZI : sve2_fp8_mla_long_by_indexed_elem<0b1, "fmlalt">;
// FP8 Widening Multiply-Add Long Group
def FMLALB_ZZZ : sve2_fp8_mla<0b100, ZPR16, "fmlalb">;
def FMLALT_ZZZ : sve2_fp8_mla<0b101, ZPR16, "fmlalt">;
// FP8 Widening Multiply-Add Long Long - Indexed Group
def FMLALLBB_ZZZI : sve2_fp8_mla_long_long_by_indexed_elem<0b00, "fmlallbb">;
def FMLALLBT_ZZZI : sve2_fp8_mla_long_long_by_indexed_elem<0b01, "fmlallbt">;
def FMLALLTB_ZZZI : sve2_fp8_mla_long_long_by_indexed_elem<0b10, "fmlalltb">;
def FMLALLTT_ZZZI : sve2_fp8_mla_long_long_by_indexed_elem<0b11, "fmlalltt">;
// FP8 Widening Multiply-Add Long Long Group
def FMLALLBB_ZZZ : sve2_fp8_mla<0b000, ZPR32, "fmlallbb">;
def FMLALLBT_ZZZ : sve2_fp8_mla<0b001, ZPR32, "fmlallbt">;
def FMLALLTB_ZZZ : sve2_fp8_mla<0b010, ZPR32, "fmlalltb">;
def FMLALLTT_ZZZ : sve2_fp8_mla<0b011, ZPR32, "fmlalltt">;
} // End HasSSVE_FP8FMA

let Predicates = [HasSSVE_FP8DOT2] in {
// FP8 Widening Dot-Product - Indexed Group
defm FDOT_ZZZI_BtoH : sve2_fp8_dot_indexed<"fdot">;
// FP8 Widening Dot-Product - Group
// TODO: Replace nxv16i8 by nxv16f8
defm FDOT_ZZZ_BtoH : sve_float_dot<0b0, 0b1, ZPR16, ZPR8, "fdot", nxv16i8, null_frag>;
}

// TODO: Replace nxv16i8 by nxv16f8
let Predicates = [HasSSVE_FP8DOT4] in {
// FP8 Widening Dot-Product - Indexed Group
defm FDOT_ZZZI_BtoS : sve_float_dot_indexed<0b1, 0b01, ZPR8, ZPR3b8, "fdot",
                                            nxv16i8, null_frag>;
// FP8 Widening Dot-Product - Group
defm FDOT_ZZZ_BtoS : sve_float_dot<0b1, 0b1, ZPR32, ZPR8, "fdot", nxv16i8, null_frag>;
}

let Predicates = [HasSVE2orSME2, HasLUT] in {
// LUTI2
  defm LUTI2_ZZZI : sve2_luti2_vector_index<"luti2">;
// LUTI4
  defm LUTI4_ZZZI   : sve2_luti4_vector_index<"luti4">;
// LUTI4 (two contiguous registers)
  defm LUTI4_Z2ZZI  : sve2_luti4_vector_vg2_index<"luti4">;
} // End HasSVE2orSME2, HasLUT

//===----------------------------------------------------------------------===//
// Checked Pointer Arithmetic (FEAT_CPA)
//===----------------------------------------------------------------------===//
let Predicates = [HasSVE, HasCPA] in {
  // Add/subtract (vectors, unpredicated)
  def ADD_ZZZ_CPA : sve_int_bin_cons_arit_0<0b11, 0b010, "addpt", ZPR64>;
  def SUB_ZZZ_CPA : sve_int_bin_cons_arit_0<0b11, 0b011, "subpt", ZPR64>;

  // Add/subtract (vectors, predicated)
  let DestructiveInstType = DestructiveBinaryComm in {
    def ADD_ZPmZ_CPA : sve_int_bin_pred_arit_log<0b11, 0b00, 0b100, "addpt", ZPR64>;
    def SUB_ZPmZ_CPA : sve_int_bin_pred_arit_log<0b11, 0b00, 0b101, "subpt", ZPR64>;
  }

  // Multiply-add vectors, writing multiplicand
  def MAD_CPA : sve_int_mad_cpa<"madpt">;

  // Multiply-add vectors, writing addend
  def MLA_CPA : sve_int_mla_cpa<"mlapt">;
}
