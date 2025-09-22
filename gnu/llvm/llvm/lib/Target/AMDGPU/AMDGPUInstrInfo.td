//===-- AMDGPUInstrInfo.td - AMDGPU DAG nodes --------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains DAG node definitions for the AMDGPU target.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// AMDGPU DAG Profiles
//===----------------------------------------------------------------------===//

def AMDGPUDTIntTernaryOp : SDTypeProfile<1, 3, [
  SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisInt<0>, SDTCisInt<3>
]>;

def AMDGPUFPClassOp : SDTypeProfile<1, 2,
  [SDTCisInt<0>, SDTCisFP<1>, SDTCisInt<2>]
>;

def AMDGPUFPPackOp : SDTypeProfile<1, 2,
  [SDTCisFP<1>, SDTCisSameAs<1, 2>]
>;

def AMDGPUIntPackOp : SDTypeProfile<1, 2,
  [SDTCisInt<1>, SDTCisSameAs<1, 2>]
>;

def AMDGPUDivScaleOp : SDTypeProfile<2, 3,
  [SDTCisFP<0>, SDTCisInt<1>, SDTCisSameAs<0, 2>, SDTCisSameAs<0, 3>, SDTCisSameAs<0, 4>]
>;

// float, float, float, vcc
def AMDGPUFmasOp : SDTypeProfile<1, 4,
  [SDTCisFP<0>, SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisSameAs<0, 3>, SDTCisInt<4>]
>;

def ImmOp : SDTypeProfile<0, 1, [SDTCisInt<0>]>;
def AMDGPUKillSDT : SDTypeProfile<0, 1, [SDTCisInt<0>]>;

def AMDGPUIfOp : SDTypeProfile<1, 2,
  [SDTCisVT<0, i1>, SDTCisVT<1, i1>, SDTCisVT<2, OtherVT>]
>;

def AMDGPUElseOp : SDTypeProfile<1, 2,
  [SDTCisVT<0, i1>, SDTCisVT<1, i1>, SDTCisVT<2, OtherVT>]
>;

def AMDGPULoopOp : SDTypeProfile<0, 2,
  [SDTCisVT<0, i1>, SDTCisVT<1, OtherVT>]
>;

def AMDGPUIfBreakOp : SDTypeProfile<1, 2,
  [SDTCisVT<0, i1>, SDTCisVT<1, i1>, SDTCisVT<2, i1>]
>;

//===----------------------------------------------------------------------===//
// AMDGPU DAG Nodes
//

def AMDGPUif : SDNode<"AMDGPUISD::IF", AMDGPUIfOp, [SDNPHasChain]>;
def AMDGPUelse : SDNode<"AMDGPUISD::ELSE", AMDGPUElseOp, [SDNPHasChain]>;
def AMDGPUloop : SDNode<"AMDGPUISD::LOOP", AMDGPULoopOp, [SDNPHasChain]>;

def callseq_start : SDNode<"ISD::CALLSEQ_START",
  SDCallSeqStart<[ SDTCisVT<0, i32>, SDTCisVT<1, i32> ]>,
  [SDNPHasChain, SDNPOutGlue]
>;

def callseq_end : SDNode<"ISD::CALLSEQ_END",
 SDCallSeqEnd<[ SDTCisVT<0, i32>, SDTCisVT<1, i32> ]>,
  [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]
>;

def AMDGPUcall : SDNode<"AMDGPUISD::CALL",
  SDTypeProfile<0, -1, [SDTCisPtrTy<0>]>,
  [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
  SDNPVariadic]
>;

def AMDGPUTCReturnTP : SDTypeProfile<0, 3, [
  SDTCisPtrTy<0>
]>;

def AMDGPUtc_return: SDNode<"AMDGPUISD::TC_RETURN", AMDGPUTCReturnTP,
[SDNPHasChain, SDNPOptInGlue, SDNPVariadic]
>;

def AMDGPUtc_return_gfx: SDNode<"AMDGPUISD::TC_RETURN_GFX", AMDGPUTCReturnTP,
[SDNPHasChain, SDNPOptInGlue, SDNPVariadic]
>;

def AMDGPUtc_return_chain: SDNode<"AMDGPUISD::TC_RETURN_CHAIN",
  SDTypeProfile<0, -1, [SDTCisPtrTy<0>]>,
  [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]
>;

def AMDGPUtrap : SDNode<"AMDGPUISD::TRAP",
  SDTypeProfile<0, -1, [SDTCisVT<0, i16>]>,
    [SDNPHasChain, SDNPVariadic, SDNPSideEffect, SDNPInGlue]
>;

def AMDGPUconstdata_ptr : SDNode<
  "AMDGPUISD::CONST_DATA_PTR", SDTypeProfile <1, 1, [SDTCisVT<0, iPTR>,
                                                     SDTCisVT<0, iPTR>]>
>;

// This argument to this node is a dword address.
def AMDGPUdwordaddr : SDNode<"AMDGPUISD::DWORDADDR", SDTIntUnaryOp>;

def AMDGPUcos_impl : SDNode<"AMDGPUISD::COS_HW", SDTFPUnaryOp>;
def AMDGPUsin_impl : SDNode<"AMDGPUISD::SIN_HW", SDTFPUnaryOp>;
// out = a - floor(a)
def AMDGPUfract_impl : SDNode<"AMDGPUISD::FRACT", SDTFPUnaryOp>;

// out = 1.0 / a
def AMDGPUrcp_impl : SDNode<"AMDGPUISD::RCP", SDTFPUnaryOp>;

// v_log_f32, which is log2
def AMDGPUlog_impl : SDNode<"AMDGPUISD::LOG", SDTFPUnaryOp>;

// v_exp_f32, which is exp2
def AMDGPUexp_impl : SDNode<"AMDGPUISD::EXP", SDTFPUnaryOp>;

// out = 1.0 / sqrt(a)
def AMDGPUrsq_impl : SDNode<"AMDGPUISD::RSQ", SDTFPUnaryOp>;

def AMDGPUrcp_legacy_impl : SDNode<"AMDGPUISD::RCP_LEGACY", SDTFPUnaryOp>;

def AMDGPUrcp_iflag : SDNode<"AMDGPUISD::RCP_IFLAG", SDTFPUnaryOp>;

// out = 1.0 / sqrt(a) result clamped to +/- max_float.
def AMDGPUrsq_clamp_impl : SDNode<"AMDGPUISD::RSQ_CLAMP", SDTFPUnaryOp>;

def AMDGPUpkrtz_f16_f32_impl : SDNode<"AMDGPUISD::CVT_PKRTZ_F16_F32", AMDGPUFPPackOp>;
def AMDGPUpknorm_i16_f32_impl : SDNode<"AMDGPUISD::CVT_PKNORM_I16_F32", AMDGPUFPPackOp>;
def AMDGPUpknorm_u16_f32_impl : SDNode<"AMDGPUISD::CVT_PKNORM_U16_F32", AMDGPUFPPackOp>;
def AMDGPUpk_i16_i32_impl : SDNode<"AMDGPUISD::CVT_PK_I16_I32", AMDGPUIntPackOp>;
def AMDGPUpk_u16_u32_impl : SDNode<"AMDGPUISD::CVT_PK_U16_U32", AMDGPUIntPackOp>;
def AMDGPUfp_to_f16 : SDNode<"AMDGPUISD::FP_TO_FP16" , SDTFPToIntOp>;


def AMDGPUfp_class_impl : SDNode<"AMDGPUISD::FP_CLASS", AMDGPUFPClassOp>;

// out = max(a, b) a and b are floats, where a nan comparison fails.
// This is not commutative because this gives the second operand:
//   x < nan ? x : nan -> nan
//   nan < x ? nan : x -> x
def AMDGPUfmax_legacy : SDNode<"AMDGPUISD::FMAX_LEGACY", SDTFPBinOp,
  []
>;

def AMDGPUfmul_legacy_impl : SDNode<"AMDGPUISD::FMUL_LEGACY", SDTFPBinOp,
  [SDNPCommutative, SDNPAssociative]
>;

// out = min(a, b) a and b are floats, where a nan comparison fails.
def AMDGPUfmin_legacy : SDNode<"AMDGPUISD::FMIN_LEGACY", SDTFPBinOp,
  []
>;

// FIXME: TableGen doesn't like commutative instructions with more
// than 2 operands.
// out = max(a, b, c) a, b and c are floats
def AMDGPUfmax3 : SDNode<"AMDGPUISD::FMAX3", SDTFPTernaryOp,
  [/*SDNPCommutative, SDNPAssociative*/]
>;

// out = max(a, b, c) a, b and c are floats. Operation is IEEE2019 compliant.
def AMDGPUfmaximum3 : SDNode<"AMDGPUISD::FMAXIMUM3", SDTFPTernaryOp,
  [/*SDNPCommutative, SDNPAssociative*/]
>;

// out = max(a, b, c) a, b, and c are signed ints
def AMDGPUsmax3 : SDNode<"AMDGPUISD::SMAX3", AMDGPUDTIntTernaryOp,
  [/*SDNPCommutative, SDNPAssociative*/]
>;

// out = max(a, b, c) a, b and c are unsigned ints
def AMDGPUumax3 : SDNode<"AMDGPUISD::UMAX3", AMDGPUDTIntTernaryOp,
  [/*SDNPCommutative, SDNPAssociative*/]
>;

// out = min(a, b, c) a, b and c are floats
def AMDGPUfmin3 : SDNode<"AMDGPUISD::FMIN3", SDTFPTernaryOp,
  [/*SDNPCommutative, SDNPAssociative*/]
>;

// out = min(a, b, c) a, b and c are floats. Operation is IEEE2019 compliant.
def AMDGPUfminimum3 : SDNode<"AMDGPUISD::FMINIMUM3", SDTFPTernaryOp,
  [/*SDNPCommutative, SDNPAssociative*/]
>;

// out = min(a, b, c) a, b and c are signed ints
def AMDGPUsmin3 : SDNode<"AMDGPUISD::SMIN3", AMDGPUDTIntTernaryOp,
  [/*SDNPCommutative, SDNPAssociative*/]
>;

// out = min(a, b) a and b are unsigned ints
def AMDGPUumin3 : SDNode<"AMDGPUISD::UMIN3", AMDGPUDTIntTernaryOp,
  [/*SDNPCommutative, SDNPAssociative*/]
>;

// out = (src0 + src1 > 0xFFFFFFFF) ? 1 : 0
def AMDGPUcarry : SDNode<"AMDGPUISD::CARRY", SDTIntBinOp, []>;

// out = (src1 > src0) ? 1 : 0
def AMDGPUborrow : SDNode<"AMDGPUISD::BORROW", SDTIntBinOp, []>;

def AMDGPUSetCCOp : SDTypeProfile<1, 3, [        // setcc
  SDTCisInt<0>, SDTCisSameAs<1, 2>, SDTCisVT<3, OtherVT>
]>;

def AMDGPUsetcc : SDNode<"AMDGPUISD::SETCC", AMDGPUSetCCOp>;

def AMDGPUfma : SDNode<"AMDGPUISD::FMA_W_CHAIN", SDTFPTernaryOp, [
   SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def AMDGPUmul : SDNode<"AMDGPUISD::FMUL_W_CHAIN", SDTFPBinOp, [
  SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def AMDGPUcvt_f32_ubyte0 : SDNode<"AMDGPUISD::CVT_F32_UBYTE0",
  SDTIntToFPOp, []>;
def AMDGPUcvt_f32_ubyte1 : SDNode<"AMDGPUISD::CVT_F32_UBYTE1",
  SDTIntToFPOp, []>;
def AMDGPUcvt_f32_ubyte2 : SDNode<"AMDGPUISD::CVT_F32_UBYTE2",
  SDTIntToFPOp, []>;
def AMDGPUcvt_f32_ubyte3 : SDNode<"AMDGPUISD::CVT_F32_UBYTE3",
  SDTIntToFPOp, []>;

def AMDGPUcvt_pk_i16_i32 : SDNode<"AMDGPUISD::CVT_PK_I16_I32",
  AMDGPUIntPackOp, []>;

// urecip - This operation is a helper for integer division, it returns the
// result of 1 / a as a fractional unsigned integer.
// out = (2^32 / a) + e
// e is rounding error
def AMDGPUurecip : SDNode<"AMDGPUISD::URECIP", SDTIntUnaryOp>;

// Special case divide preop and flags.
def AMDGPUdiv_scale : SDNode<"AMDGPUISD::DIV_SCALE", AMDGPUDivScaleOp>;

//  Special case divide FMA with scale and flags (src0 = Quotient,
//  src1 = Denominator, src2 = Numerator).
def AMDGPUdiv_fmas_impl : SDNode<"AMDGPUISD::DIV_FMAS", AMDGPUFmasOp,
                            [SDNPOptInGlue]>;

// Single or double precision division fixup.
// Special case divide fixup and flags(src0 = Quotient, src1 =
// Denominator, src2 = Numerator).
def AMDGPUdiv_fixup_impl : SDNode<"AMDGPUISD::DIV_FIXUP", SDTFPTernaryOp>;

def AMDGPUfmad_ftz_impl : SDNode<"AMDGPUISD::FMAD_FTZ", SDTFPTernaryOp>;

def AMDGPUregister_load : SDNode<"AMDGPUISD::REGISTER_LOAD",
                          SDTypeProfile<1, 2, [SDTCisPtrTy<1>, SDTCisInt<2>]>,
                          [SDNPHasChain, SDNPMayLoad]>;

def AMDGPUregister_store : SDNode<"AMDGPUISD::REGISTER_STORE",
                           SDTypeProfile<0, 3, [SDTCisPtrTy<1>, SDTCisInt<2>]>,
                           [SDNPHasChain, SDNPMayStore]>;

// MSKOR instructions are atomic memory instructions used mainly for storing
// 8-bit and 16-bit values.  The definition is:
//
// MSKOR(dst, mask, src) MEM[dst] = ((MEM[dst] & ~mask) | src)
//
// src0: vec4(src, 0, 0, mask)
// src1: dst - rat offset (aka pointer) in dwords
def AMDGPUstore_mskor : SDNode<"AMDGPUISD::STORE_MSKOR",
                        SDTypeProfile<0, 2, []>,
                        [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def AMDGPUatomic_cmp_swap : SDNode<"AMDGPUISD::ATOMIC_CMP_SWAP",
                            SDTypeProfile<1, 2, [SDTCisPtrTy<1>, SDTCisVec<2>]>,
                            [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                             SDNPMemOperand]>;

def AMDGPUbfe_u32_impl : SDNode<"AMDGPUISD::BFE_U32", AMDGPUDTIntTernaryOp>;
def AMDGPUbfe_i32_impl : SDNode<"AMDGPUISD::BFE_I32", AMDGPUDTIntTernaryOp>;
def AMDGPUbfi : SDNode<"AMDGPUISD::BFI", AMDGPUDTIntTernaryOp>;
def AMDGPUbfm : SDNode<"AMDGPUISD::BFM", SDTIntBinOp>;

def AMDGPUffbh_u32_impl : SDNode<"AMDGPUISD::FFBH_U32", SDTIntBitCountUnaryOp>;
def AMDGPUffbh_i32_impl : SDNode<"AMDGPUISD::FFBH_I32", SDTIntBitCountUnaryOp>;

def AMDGPUffbl_b32_impl : SDNode<"AMDGPUISD::FFBL_B32", SDTIntBitCountUnaryOp>;

// Signed and unsigned 24-bit multiply. The highest 8-bits are ignore
// when performing the multiply. The result is a 32 or 64 bit value.
def AMDGPUMul24Op : SDTypeProfile<1, 2, [
  SDTCisInt<0>, SDTCisInt<1>, SDTCisSameAs<1, 2>
]>;

def AMDGPUmul_u24_impl : SDNode<"AMDGPUISD::MUL_U24", AMDGPUMul24Op,
  [SDNPCommutative, SDNPAssociative]
>;
def AMDGPUmul_i24_impl : SDNode<"AMDGPUISD::MUL_I24", AMDGPUMul24Op,
  [SDNPCommutative, SDNPAssociative]
>;

// mulhi24 yields the high-order 16 bits of the 48-bit result. Here's an example
// that shows mulhi24 is not associative:
//
// Given a = 0x10002, b = c = 0xffffff:
// mulhi24(mulhi24(a, b), c) = mulhi24(0x100, 0xffffff) = 0
// Which is not equal to:
// mulhi24(a, mulhi24(b, c)) = mulhi24(0x10002, 0xffff) = 1
def AMDGPUmulhi_u24_impl : SDNode<"AMDGPUISD::MULHI_U24", SDTIntBinOp,
  [SDNPCommutative]
>;
def AMDGPUmulhi_i24_impl : SDNode<"AMDGPUISD::MULHI_I24", SDTIntBinOp,
  [SDNPCommutative]
>;

def AMDGPUmad_u24 : SDNode<"AMDGPUISD::MAD_U24", AMDGPUDTIntTernaryOp,
  []
>;
def AMDGPUmad_i24 : SDNode<"AMDGPUISD::MAD_I24", AMDGPUDTIntTernaryOp,
  []
>;

def AMDGPUsmed3 : SDNode<"AMDGPUISD::SMED3", AMDGPUDTIntTernaryOp,
  []
>;

def AMDGPUumed3 : SDNode<"AMDGPUISD::UMED3", AMDGPUDTIntTernaryOp,
  []
>;

def AMDGPUfmed3_impl : SDNode<"AMDGPUISD::FMED3", SDTFPTernaryOp, []>;

def AMDGPUfdot2_impl : SDNode<"AMDGPUISD::FDOT2",
                  SDTypeProfile<1, 4, [SDTCisSameAs<0, 3>, SDTCisSameAs<1, 2>,
                                       SDTCisFP<0>, SDTCisVec<1>,
                                       SDTCisInt<4>]>,
                  []>;

def AMDGPUperm_impl : SDNode<"AMDGPUISD::PERM", AMDGPUDTIntTernaryOp, []>;

// SI+ export
def AMDGPUExportOp : SDTypeProfile<0, 8, [
  SDTCisInt<0>,       // i8 tgt
  SDTCisInt<1>,       // i8 en
                      // i32 or f32 src0
  SDTCisSameAs<3, 2>, // f32 src1
  SDTCisSameAs<4, 2>, // f32 src2
  SDTCisSameAs<5, 2>, // f32 src3
  SDTCisInt<6>,       // i1 compr
  // skip done
  SDTCisInt<1>        // i1 vm

]>;


//===----------------------------------------------------------------------===//
// Flow Control Profile Types
//===----------------------------------------------------------------------===//
// Branch instruction where second and third are basic blocks
def SDTIL_BRCond : SDTypeProfile<0, 2, [
    SDTCisVT<0, OtherVT>
    ]>;

//===----------------------------------------------------------------------===//
// Flow Control DAG Nodes
//===----------------------------------------------------------------------===//
def IL_brcond      : SDNode<"AMDGPUISD::BRANCH_COND", SDTIL_BRCond, [SDNPHasChain]>;

//===----------------------------------------------------------------------===//
// Call/Return DAG Nodes
//===----------------------------------------------------------------------===//
def AMDGPUendpgm : SDNode<"AMDGPUISD::ENDPGM", SDTNone,
    [SDNPHasChain, SDNPOptInGlue]>;
def AMDGPUendpgm_trap : SDNode<"AMDGPUISD::ENDPGM_TRAP", SDTNone,
    [SDNPHasChain]>;
def AMDGPUsimulated_trap : SDNode<"AMDGPUISD::SIMULATED_TRAP", SDTNone,
    [SDNPHasChain]>;

def AMDGPUreturn_to_epilog : SDNode<"AMDGPUISD::RETURN_TO_EPILOG", SDTNone,
    [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def AMDGPUret_glue : SDNode<"AMDGPUISD::RET_GLUE", SDTNone,
  [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]
>;


//===----------------------------------------------------------------------===//
// Intrinsic/Custom node compatibility PatFrags
//===----------------------------------------------------------------------===//

def AMDGPUrcp : PatFrags<(ops node:$src), [(int_amdgcn_rcp node:$src),
                                           (AMDGPUrcp_impl node:$src)]>;
def AMDGPUrcp_legacy : PatFrags<(ops node:$src), [(int_amdgcn_rcp_legacy node:$src),
                                                  (AMDGPUrcp_legacy_impl node:$src)]>;

def AMDGPUrsq : PatFrags<(ops node:$src), [(int_amdgcn_rsq node:$src),
                                           (AMDGPUrsq_impl node:$src)]>;

def AMDGPUrsq_clamp : PatFrags<(ops node:$src), [(int_amdgcn_rsq_clamp node:$src),
                                                 (AMDGPUrsq_clamp_impl node:$src)]>;

def AMDGPUsin : PatFrags<(ops node:$src), [(int_amdgcn_sin node:$src),
                                           (AMDGPUsin_impl node:$src)]>;
def AMDGPUcos : PatFrags<(ops node:$src), [(int_amdgcn_cos node:$src),
                                           (AMDGPUcos_impl node:$src)]>;
def AMDGPUfract : PatFrags<(ops node:$src), [(int_amdgcn_fract node:$src),
                                             (AMDGPUfract_impl node:$src)]>;
def AMDGPUlog : PatFrags<(ops node:$src), [(int_amdgcn_log node:$src),
                                           (AMDGPUlog_impl node:$src)]>;
def AMDGPUlogf16 : PatFrags<(ops node:$src), [(int_amdgcn_log node:$src),
                                              (flog2 node:$src)]>;

def AMDGPUexp : PatFrags<(ops node:$src), [(int_amdgcn_exp2 node:$src),
                                           (AMDGPUexp_impl node:$src)]>;
def AMDGPUexpf16 : PatFrags<(ops node:$src), [(int_amdgcn_exp2 node:$src),
                                              (fexp2 node:$src)]>;

def AMDGPUfp_class : PatFrags<(ops node:$src0, node:$src1),
  [(int_amdgcn_class node:$src0, node:$src1),
   (AMDGPUfp_class_impl node:$src0, node:$src1)]>;

def AMDGPUfmed3 : PatFrags<(ops node:$src0, node:$src1, node:$src2),
  [(int_amdgcn_fmed3 node:$src0, node:$src1, node:$src2),
   (AMDGPUfmed3_impl node:$src0, node:$src1, node:$src2)]>;

def AMDGPUdiv_fixup : PatFrags<(ops node:$src0, node:$src1, node:$src2),
  [(int_amdgcn_div_fixup node:$src0, node:$src1, node:$src2),
   (AMDGPUdiv_fixup_impl node:$src0, node:$src1, node:$src2)]>;

def AMDGPUffbh_i32 : PatFrags<(ops node:$src),
  [(int_amdgcn_sffbh node:$src),
   (AMDGPUffbh_i32_impl node:$src)]>;

def AMDGPUffbh_u32 : PatFrags<(ops node:$src),
  [(ctlz_zero_undef node:$src),
   (AMDGPUffbh_u32_impl node:$src)]>;

def AMDGPUffbl_b32 : PatFrags<(ops node:$src),
  [(cttz_zero_undef node:$src),
   (AMDGPUffbl_b32_impl node:$src)]>;

def AMDGPUpkrtz_f16_f32 : PatFrags<(ops node:$src0, node:$src1),
  [(int_amdgcn_cvt_pkrtz node:$src0, node:$src1),
  (AMDGPUpkrtz_f16_f32_impl node:$src0, node:$src1)]>;

def AMDGPUpknorm_i16_f32 : PatFrags<(ops node:$src0, node:$src1),
  [(int_amdgcn_cvt_pknorm_i16 node:$src0, node:$src1),
  (AMDGPUpknorm_i16_f32_impl node:$src0, node:$src1)]>;

def AMDGPUpknorm_u16_f32 : PatFrags<(ops node:$src0, node:$src1),
  [(int_amdgcn_cvt_pknorm_u16 node:$src0, node:$src1),
  (AMDGPUpknorm_u16_f32_impl node:$src0, node:$src1)]>;

def AMDGPUpk_i16_i32 : PatFrags<(ops node:$src0, node:$src1),
  [(int_amdgcn_cvt_pk_i16 node:$src0, node:$src1),
  (AMDGPUpk_i16_i32_impl node:$src0, node:$src1)]>;

def AMDGPUpk_u16_u32 : PatFrags<(ops node:$src0, node:$src1),
  [(int_amdgcn_cvt_pk_u16 node:$src0, node:$src1),
  (AMDGPUpk_u16_u32_impl node:$src0, node:$src1)]>;

def AMDGPUfmad_ftz : PatFrags<(ops node:$src0, node:$src1, node:$src2),
  [(int_amdgcn_fmad_ftz node:$src0, node:$src1, node:$src2),
   (AMDGPUfmad_ftz_impl node:$src0, node:$src1, node:$src2)]>;

def AMDGPUmul_u24 : PatFrags<(ops node:$src0, node:$src1),
  [(int_amdgcn_mul_u24 node:$src0, node:$src1),
   (AMDGPUmul_u24_impl node:$src0, node:$src1)]>;

def AMDGPUmul_i24 : PatFrags<(ops node:$src0, node:$src1),
  [(int_amdgcn_mul_i24 node:$src0, node:$src1),
   (AMDGPUmul_i24_impl node:$src0, node:$src1)]>;

def AMDGPUmulhi_u24 : PatFrags<(ops node:$src0, node:$src1),
  [(int_amdgcn_mulhi_u24 node:$src0, node:$src1),
   (AMDGPUmulhi_u24_impl node:$src0, node:$src1)]>;

def AMDGPUmulhi_i24 : PatFrags<(ops node:$src0, node:$src1),
  [(int_amdgcn_mulhi_i24 node:$src0, node:$src1),
   (AMDGPUmulhi_i24_impl node:$src0, node:$src1)]>;

def AMDGPUbfe_i32 : PatFrags<(ops node:$src0, node:$src1, node:$src2),
  [(int_amdgcn_sbfe node:$src0, node:$src1, node:$src2),
   (AMDGPUbfe_i32_impl node:$src0, node:$src1, node:$src2)]>;

def AMDGPUbfe_u32 : PatFrags<(ops node:$src0, node:$src1, node:$src2),
  [(int_amdgcn_ubfe node:$src0, node:$src1, node:$src2),
   (AMDGPUbfe_u32_impl node:$src0, node:$src1, node:$src2)]>;

def AMDGPUfmul_legacy : PatFrags<(ops node:$src0, node:$src1),
  [(int_amdgcn_fmul_legacy node:$src0, node:$src1),
   (AMDGPUfmul_legacy_impl node:$src0, node:$src1)]>;

def AMDGPUfdot2 : PatFrags<(ops node:$src0, node:$src1, node:$src2, node:$clamp),
  [(int_amdgcn_fdot2 node:$src0, node:$src1, node:$src2, node:$clamp),
   (AMDGPUfdot2_impl node:$src0, node:$src1, node:$src2, node:$clamp)]>;

def AMDGPUdiv_fmas : PatFrags<(ops node:$src0, node:$src1, node:$src2, node:$vcc),
  [(int_amdgcn_div_fmas node:$src0, node:$src1, node:$src2, node:$vcc),
   (AMDGPUdiv_fmas_impl node:$src0, node:$src1, node:$src2, node:$vcc)]>;

def AMDGPUperm : PatFrags<(ops node:$src0, node:$src1, node:$src2),
  [(int_amdgcn_perm node:$src0, node:$src1, node:$src2),
   (AMDGPUperm_impl node:$src0, node:$src1, node:$src2)]>;
