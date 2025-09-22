//===--- arm_neon.td - ARM NEON compiler interface ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TableGen definitions from which the ARM NEON header
//  file will be generated.  See ARM document DUI0348B.
//
//===----------------------------------------------------------------------===//

include "arm_neon_incl.td"

def OP_ADD      : Op<(op "+", $p0, $p1)>;
def OP_ADDL     : Op<(op "+", (call "vmovl", $p0), (call "vmovl", $p1))>;
def OP_ADDLHi   : Op<(op "+", (call "vmovl_high", $p0),
                              (call "vmovl_high", $p1))>;
def OP_ADDW     : Op<(op "+", $p0, (call "vmovl", $p1))>;
def OP_ADDWHi   : Op<(op "+", $p0, (call "vmovl_high", $p1))>;
def OP_SUB      : Op<(op "-", $p0, $p1)>;
def OP_SUBL     : Op<(op "-", (call "vmovl", $p0), (call "vmovl", $p1))>;
def OP_SUBLHi   : Op<(op "-", (call "vmovl_high", $p0),
                              (call "vmovl_high", $p1))>;
def OP_SUBW     : Op<(op "-", $p0, (call "vmovl", $p1))>;
def OP_SUBWHi   : Op<(op "-", $p0, (call "vmovl_high", $p1))>;
def OP_MUL      : Op<(op "*", $p0, $p1)>;
def OP_MLA      : Op<(op "+", $p0, (op "*", $p1, $p2))>;
def OP_MLAL     : Op<(op "+", $p0, (call "vmull", $p1, $p2))>;
def OP_MULLHi   : Op<(call "vmull", (call "vget_high", $p0),
                                    (call "vget_high", $p1))>;
def OP_MULLHi_P64 : Op<(call "vmull",
                         (cast "poly64_t", (call "vget_high", $p0)),
                         (cast "poly64_t", (call "vget_high", $p1)))>;
def OP_MULLHi_N : Op<(call "vmull_n", (call "vget_high", $p0), $p1)>;
def OP_MLALHi   : Op<(call "vmlal", $p0, (call "vget_high", $p1),
                                         (call "vget_high", $p2))>;
def OP_MLALHi_N : Op<(call "vmlal_n", $p0, (call "vget_high", $p1), $p2)>;
def OP_MLS      : Op<(op "-", $p0, (op "*", $p1, $p2))>;
def OP_FMLS     : Op<(call "vfma", $p0, (op "-", $p1), $p2)>;
def OP_MLSL     : Op<(op "-", $p0, (call "vmull", $p1, $p2))>;
def OP_MLSLHi   : Op<(call "vmlsl", $p0, (call "vget_high", $p1),
                                         (call "vget_high", $p2))>;
def OP_MLSLHi_N : Op<(call "vmlsl_n", $p0, (call "vget_high", $p1), $p2)>;
def OP_MUL_N    : Op<(op "*", $p0, (dup $p1))>;
def OP_MULX_N   : Op<(call "vmulx", $p0, (dup $p1))>;
def OP_MLA_N    : Op<(op "+", $p0, (op "*", $p1, (dup $p2)))>;
def OP_MLS_N    : Op<(op "-", $p0, (op "*", $p1, (dup $p2)))>;
def OP_FMLA_N   : Op<(call "vfma", $p0, $p1, (dup $p2))>;
def OP_FMLS_N   : Op<(call "vfma", $p0, (op "-", $p1), (dup $p2))>;
def OP_MLAL_N   : Op<(op "+", $p0, (call "vmull", $p1, (dup $p2)))>;
def OP_MLSL_N   : Op<(op "-", $p0, (call "vmull", $p1, (dup $p2)))>;
def OP_MUL_LN   : Op<(op "*", $p0, (call_mangled "splat_lane", $p1, $p2))>;
def OP_MULX_LN  : Op<(call "vmulx", $p0, (call_mangled "splat_lane", $p1, $p2))>;
def OP_MULL_N  : Op<(call "vmull", $p0, (dup $p1))>;
def OP_MULL_LN  : Op<(call "vmull", $p0, (call_mangled "splat_lane", $p1, $p2))>;
def OP_MULLHi_LN: Op<(call "vmull", (call "vget_high", $p0), (call_mangled "splat_lane", $p1, $p2))>;
def OP_MLA_LN   : Op<(op "+", $p0, (op "*", $p1, (call_mangled "splat_lane", $p2, $p3)))>;
def OP_MLS_LN   : Op<(op "-", $p0, (op "*", $p1, (call_mangled "splat_lane", $p2, $p3)))>;
def OP_MLAL_LN  : Op<(op "+", $p0, (call "vmull", $p1, (call_mangled "splat_lane", $p2, $p3)))>;
def OP_MLALHi_LN: Op<(op "+", $p0, (call "vmull", (call "vget_high", $p1),
                                                  (call_mangled "splat_lane", $p2, $p3)))>;
def OP_MLSL_LN  : Op<(op "-", $p0, (call "vmull", $p1, (call_mangled "splat_lane", $p2, $p3)))>;
def OP_MLSLHi_LN : Op<(op "-", $p0, (call "vmull", (call "vget_high", $p1),
                                                   (call_mangled "splat_lane", $p2, $p3)))>;
def OP_QDMULL_N : Op<(call "vqdmull", $p0, (dup $p1))>;
def OP_QDMULL_LN : Op<(call "vqdmull", $p0, (call_mangled "splat_lane", $p1, $p2))>;
def OP_QDMULLHi_LN : Op<(call "vqdmull", (call "vget_high", $p0),
                                         (call_mangled "splat_lane", $p1, $p2))>;
def OP_QDMLAL_N : Op<(call "vqdmlal", $p0, $p1, (dup $p2))>;
def OP_QDMLAL_LN : Op<(call "vqdmlal", $p0, $p1, (call_mangled "splat_lane", $p2, $p3))>;
def OP_QDMLALHi_LN : Op<(call "vqdmlal", $p0, (call "vget_high", $p1),
                                              (call_mangled "splat_lane", $p2, $p3))>;
def OP_QDMLSL_N : Op<(call "vqdmlsl", $p0, $p1, (dup $p2))>;
def OP_QDMLSL_LN : Op<(call "vqdmlsl", $p0, $p1, (call_mangled "splat_lane", $p2, $p3))>;
def OP_QDMLSLHi_LN : Op<(call "vqdmlsl", $p0, (call "vget_high", $p1),
                                              (call_mangled "splat_lane", $p2, $p3))>;
def OP_QDMULH_N : Op<(call "vqdmulh", $p0, (dup $p1))>;
def OP_QDMULH_LN : Op<(call "vqdmulh", $p0, (call_mangled "splat_lane", $p1, $p2))>;
def OP_QRDMULH_LN : Op<(call "vqrdmulh", $p0, (call_mangled "splat_lane", $p1, $p2))>;
def OP_QRDMULH_N : Op<(call "vqrdmulh", $p0, (dup $p1))>;
def OP_QRDMLAH_LN : Op<(call "vqrdmlah", $p0, $p1, (call_mangled "splat_lane", $p2, $p3))>;
def OP_QRDMLSH_LN : Op<(call "vqrdmlsh", $p0, $p1, (call_mangled "splat_lane", $p2, $p3))>;
def OP_FMS_LN   : Op<(call "vfma_lane", $p0, (op "-", $p1), $p2, $p3)>;
def OP_FMS_LNQ  : Op<(call "vfma_laneq", $p0, (op "-", $p1), $p2, $p3)>;
def OP_TRN1     : Op<(shuffle $p0, $p1, (interleave (decimate mask0, 2),
                                                    (decimate mask1, 2)))>;
def OP_ZIP1     : Op<(shuffle $p0, $p1, (lowhalf (interleave mask0, mask1)))>;
def OP_UZP1     : Op<(shuffle $p0, $p1, (add (decimate mask0, 2),
                                             (decimate mask1, 2)))>;
def OP_TRN2     : Op<(shuffle $p0, $p1, (interleave
                                          (decimate (rotl mask0, 1), 2),
                                          (decimate (rotl mask1, 1), 2)))>;
def OP_ZIP2     : Op<(shuffle $p0, $p1, (highhalf (interleave mask0, mask1)))>;
def OP_UZP2     : Op<(shuffle $p0, $p1, (add (decimate (rotl mask0, 1), 2),
                                             (decimate (rotl mask1, 1), 2)))>;
def OP_EQ       : Op<(cast "R", (op "==", $p0, $p1))>;
def OP_GE       : Op<(cast "R", (op ">=", $p0, $p1))>;
def OP_LE       : Op<(cast "R", (op "<=", $p0, $p1))>;
def OP_GT       : Op<(cast "R", (op ">", $p0, $p1))>;
def OP_LT       : Op<(cast "R", (op "<", $p0, $p1))>;
def OP_NEG      : Op<(op "-", $p0)>;
def OP_NOT      : Op<(op "~", $p0)>;
def OP_AND      : Op<(op "&", $p0, $p1)>;
def OP_OR       : Op<(op "|", $p0, $p1)>;
def OP_XOR      : Op<(op "^", $p0, $p1)>;
def OP_ANDN     : Op<(op "&", $p0, (op "~", $p1))>;
def OP_ORN      : Op<(op "|", $p0, (op "~", $p1))>;
def OP_CAST     : LOp<[(save_temp $promote, $p0),
                       (cast "R", $promote)]>;
def OP_HI       : Op<(shuffle $p0, $p0, (highhalf mask0))>;
def OP_LO       : Op<(shuffle $p0, $p0, (lowhalf mask0))>;
def OP_CONC     : Op<(shuffle $p0, $p1, (add mask0, mask1))>;
def OP_DUP      : Op<(dup $p0)>;
def OP_DUP_LN   : Op<(call_mangled "splat_lane", $p0, $p1)>;
def OP_SEL      : Op<(cast "R", (op "|",
                                    (op "&", $p0, (cast $p0, $p1)),
                                    (op "&", (op "~", $p0), (cast $p0, $p2))))>;
def OP_REV16    : Op<(shuffle $p0, $p0, (rev 16, mask0))>;
def OP_REV32    : Op<(shuffle $p0, $p0, (rev 32, mask0))>;
def OP_REV64    : Op<(shuffle $p0, $p0, (rev 64, mask0))>;
def OP_XTN      : Op<(call "vcombine", $p0, (call "vmovn", $p1))>;
def OP_SQXTUN   : Op<(call "vcombine", (cast $p0, "U", $p0),
                                       (call "vqmovun", $p1))>;
def OP_QXTN     : Op<(call "vcombine", $p0, (call "vqmovn", $p1))>;
def OP_VCVT_NA_HI_F16 : Op<(call "vcombine", $p0, (call "vcvt_f16_f32", $p1))>;
def OP_VCVT_NA_HI_F32 : Op<(call "vcombine", $p0, (call "vcvt_f32_f64", $p1))>;
def OP_VCVT_EX_HI_F32 : Op<(call "vcvt_f32_f16", (call "vget_high", $p0))>;
def OP_VCVT_EX_HI_F64 : Op<(call "vcvt_f64_f32", (call "vget_high", $p0))>;
def OP_VCVTX_HI : Op<(call "vcombine", $p0, (call "vcvtx_f32", $p1))>;
def OP_REINT    : Op<(cast "R", $p0)>;
def OP_ADDHNHi  : Op<(call "vcombine", $p0, (call "vaddhn", $p1, $p2))>;
def OP_RADDHNHi : Op<(call "vcombine", $p0, (call "vraddhn", $p1, $p2))>;
def OP_SUBHNHi  : Op<(call "vcombine", $p0, (call "vsubhn", $p1, $p2))>;
def OP_RSUBHNHi : Op<(call "vcombine", $p0, (call "vrsubhn", $p1, $p2))>;
def OP_ABDL     : Op<(cast "R", (call "vmovl", (cast $p0, "U",
                                                     (call "vabd", $p0, $p1))))>;
def OP_ABDLHi   : Op<(call "vabdl", (call "vget_high", $p0),
                                    (call "vget_high", $p1))>;
def OP_ABA      : Op<(op "+", $p0, (call "vabd", $p1, $p2))>;
def OP_ABAL     : Op<(op "+", $p0, (call "vabdl", $p1, $p2))>;
def OP_ABALHi   : Op<(call "vabal", $p0, (call "vget_high", $p1),
                                       (call "vget_high", $p2))>;
def OP_QDMULLHi : Op<(call "vqdmull", (call "vget_high", $p0),
                                      (call "vget_high", $p1))>;
def OP_QDMULLHi_N : Op<(call "vqdmull_n", (call "vget_high", $p0), $p1)>;
def OP_QDMLALHi : Op<(call "vqdmlal", $p0, (call "vget_high", $p1),
                                           (call "vget_high", $p2))>;
def OP_QDMLALHi_N : Op<(call "vqdmlal_n", $p0, (call "vget_high", $p1), $p2)>;
def OP_QDMLSLHi : Op<(call "vqdmlsl", $p0, (call "vget_high", $p1),
                                           (call "vget_high", $p2))>;
def OP_QDMLSLHi_N : Op<(call "vqdmlsl_n", $p0, (call "vget_high", $p1), $p2)>;
def OP_DIV  : Op<(op "/", $p0, $p1)>;
def OP_LONG_HI : Op<(cast "R", (call (name_replace "_high_", "_"),
                                                (call "vget_high", $p0), $p1))>;
def OP_NARROW_HI : Op<(cast "R", (call "vcombine",
                                       (cast "R", "H", $p0),
                                       (cast "R", "H",
                                           (call (name_replace "_high_", "_"),
                                                 $p1, $p2))))>;
def OP_MOVL_HI  : LOp<[(save_temp $a1, (call "vget_high", $p0)),
                       (cast "R",
                            (call "vshll_n", $a1, (literal "int32_t", "0")))]>;
def OP_COPY_LN : Op<(call "vset_lane", (call "vget_lane", $p2, $p3), $p0, $p1)>;
def OP_SCALAR_MUL_LN : Op<(op "*", $p0, (call "vget_lane", $p1, $p2))>;
def OP_SCALAR_MULX_LN : Op<(call "vmulx", $p0, (call "vget_lane", $p1, $p2))>;
def OP_SCALAR_VMULX_LN : LOp<[(save_temp $x, (call "vget_lane", $p0,
                                                    (literal "int32_t", "0"))),
                              (save_temp $y, (call "vget_lane", $p1, $p2)),
                              (save_temp $z, (call "vmulx", $x, $y)),
                              (call "vset_lane", $z, $p0, $p2)]>;
def OP_SCALAR_VMULX_LNQ : LOp<[(save_temp $x, (call "vget_lane", $p0,
                                                     (literal "int32_t", "0"))),
                               (save_temp $y, (call "vget_lane", $p1, $p2)),
                               (save_temp $z, (call "vmulx", $x, $y)),
                               (call "vset_lane", $z, $p0, (literal "int32_t",
                                                                     "0"))]>;
class ScalarMulOp<string opname> :
  Op<(call opname, $p0, (call "vget_lane", $p1, $p2))>;

def OP_SCALAR_QDMULL_LN : ScalarMulOp<"vqdmull">;
def OP_SCALAR_QDMULH_LN : ScalarMulOp<"vqdmulh">;
def OP_SCALAR_QRDMULH_LN : ScalarMulOp<"vqrdmulh">;

def OP_SCALAR_QRDMLAH_LN : Op<(call "vqrdmlah", $p0, $p1,
                                (call "vget_lane", $p2, $p3))>;
def OP_SCALAR_QRDMLSH_LN : Op<(call "vqrdmlsh", $p0, $p1,
                                (call "vget_lane", $p2, $p3))>;

def OP_SCALAR_HALF_GET_LN : Op<(bitcast "float16_t",
                                   (call "vget_lane",
                                         (bitcast "int16x4_t", $p0), $p1))>;
def OP_SCALAR_HALF_GET_LNQ : Op<(bitcast "float16_t",
                                    (call "vget_lane",
                                          (bitcast "int16x8_t", $p0), $p1))>;
def OP_SCALAR_HALF_SET_LN : Op<(bitcast "float16x4_t",
                                   (call "vset_lane",
                                         (bitcast "int16_t", $p0),
                                         (bitcast "int16x4_t", $p1), $p2))>;
def OP_SCALAR_HALF_SET_LNQ : Op<(bitcast "float16x8_t",
                                    (call "vset_lane",
                                          (bitcast "int16_t", $p0),
                                          (bitcast "int16x8_t", $p1), $p2))>;

def OP_DOT_LN
    : Op<(call "vdot", $p0, $p1,
          (bitcast $p1, (call_mangled "splat_lane", (bitcast "32", $p2), $p3)))>;
def OP_DOT_LNQ
    : Op<(call "vdot", $p0, $p1,
          (bitcast $p1, (call_mangled "splat_lane", (bitcast "32", $p2), $p3)))>;

def OP_FMLAL_LN     : Op<(call "vfmlal_low", $p0, $p1,
                           (dup_typed $p1, (call "vget_lane", $p2, $p3)))>;
def OP_FMLSL_LN     : Op<(call "vfmlsl_low", $p0, $p1,
                           (dup_typed $p1, (call "vget_lane", $p2, $p3)))>;
def OP_FMLAL_LN_Hi  : Op<(call "vfmlal_high", $p0, $p1,
                           (dup_typed $p1, (call "vget_lane", $p2, $p3)))>;
def OP_FMLSL_LN_Hi  : Op<(call "vfmlsl_high", $p0, $p1,
                           (dup_typed $p1, (call "vget_lane", $p2, $p3)))>;

def OP_USDOT_LN
    : Op<(call "vusdot", $p0, $p1,
          (cast "8", "S", (call_mangled "splat_lane", (bitcast "int32x2_t", $p2), $p3)))>;
def OP_USDOT_LNQ
    : Op<(call "vusdot", $p0, $p1,
          (cast "8", "S", (call_mangled "splat_lane", (bitcast "int32x4_t", $p2), $p3)))>;

// sudot splats the second vector and then calls vusdot
def OP_SUDOT_LN
    : Op<(call "vusdot", $p0,
          (cast "8", "U", (call_mangled "splat_lane", (bitcast "int32x2_t", $p2), $p3)), $p1)>;
def OP_SUDOT_LNQ
    : Op<(call "vusdot", $p0,
          (cast "8", "U", (call_mangled "splat_lane", (bitcast "int32x4_t", $p2), $p3)), $p1)>;

def OP_BFDOT_LN
    : Op<(call "vbfdot", $p0, $p1,
          (bitcast $p1, (call_mangled "splat_lane", (bitcast "float32x2_t", $p2), $p3)))>;

def OP_BFDOT_LNQ
    : Op<(call "vbfdot", $p0, $p1,
          (bitcast $p1, (call_mangled "splat_lane", (bitcast "float32x4_t", $p2), $p3)))>;

def OP_BFMLALB_LN
    : Op<(call "vbfmlalb", $p0, $p1,
          (dup_typed $p1, (call "vget_lane", $p2, $p3)))>;

def OP_BFMLALT_LN
    : Op<(call "vbfmlalt", $p0, $p1,
          (dup_typed $p1, (call "vget_lane", $p2, $p3)))>;

def OP_VCVT_F32_BF16
    : Op<(bitcast "R",
          (call "vshll_n", (bitcast "int16x4_t", $p0),
                           (literal "int32_t", "16")))>;
def OP_VCVT_F32_BF16_LO
    : Op<(call "vcvt_f32_bf16", (call "vget_low", $p0))>;
def OP_VCVT_F32_BF16_HI
    : Op<(call "vcvt_f32_bf16", (call "vget_high", $p0))>;

def OP_VCVT_BF16_F32_LO_A64
    : Op<(call "__a64_vcvtq_low_bf16", $p0)>;
def OP_VCVT_BF16_F32_A64
    : Op<(call "vget_low", (call "__a64_vcvtq_low_bf16", $p0))>;

def OP_VCVT_BF16_F32_A32
    : Op<(call "__a32_vcvt_bf16", $p0)>;

def OP_VCVT_BF16_F32_LO_A32
    : Op<(call "vcombine", (cast "bfloat16x4_t", (literal "uint64_t", "0ULL")),
                           (call "__a32_vcvt_bf16", $p0))>;
def OP_VCVT_BF16_F32_HI_A32
    : Op<(call "vcombine", (call "__a32_vcvt_bf16", $p1),
                           (call "vget_low", $p0))>;

def OP_CVT_F32_BF16
    : Op<(bitcast "R", (op "<<", (cast "int32_t", (bitcast "int16_t", $p0)),
                                 (literal "int32_t", "16")))>;

//===----------------------------------------------------------------------===//
// Auxiliary Instructions
//===----------------------------------------------------------------------===//

// Splat operation - performs a range-checked splat over a vector
def SPLAT  : WInst<"splat_lane", ".(!q)I",
                   "UcUsUicsilPcPsfQUcQUsQUiQcQsQiQPcQPsQflUlQlQUlhdQhQdPlQPl">;
def SPLATQ : WInst<"splat_laneq", ".(!Q)I",
                   "UcUsUicsilPcPsfQUcQUsQUiQcQsQiQPcQPsQflUlQlQUlhdQhQdPlQPl"> {
  let isLaneQ = 1;
}
let TargetGuard = "bf16,neon" in {
  def SPLAT_BF  : WInst<"splat_lane", ".(!q)I", "bQb">;
  def SPLATQ_BF : WInst<"splat_laneq", ".(!Q)I", "bQb"> {
    let isLaneQ = 1;
  }
}

//===----------------------------------------------------------------------===//
// Intrinsics
//===----------------------------------------------------------------------===//

////////////////////////////////////////////////////////////////////////////////
// E.3.1 Addition
def VADD    : IOpInst<"vadd", "...",
                      "csilfUcUsUiUlQcQsQiQlQfQUcQUsQUiQUl", OP_ADD>;
def VADDL   : SOpInst<"vaddl", "(>Q)..", "csiUcUsUi", OP_ADDL>;
def VADDW   : SOpInst<"vaddw", "(>Q)(>Q).", "csiUcUsUi", OP_ADDW>;
def VHADD   : SInst<"vhadd", "...", "csiUcUsUiQcQsQiQUcQUsQUi">;
def VRHADD  : SInst<"vrhadd", "...", "csiUcUsUiQcQsQiQUcQUsQUi">;
def VQADD   : SInst<"vqadd", "...", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl">;
def VADDHN  : IInst<"vaddhn", "<QQ", "silUsUiUl">;
def VRADDHN : IInst<"vraddhn", "<QQ", "silUsUiUl">;

////////////////////////////////////////////////////////////////////////////////
// E.3.2 Multiplication
def VMUL     : IOpInst<"vmul", "...", "csifUcUsUiQcQsQiQfQUcQUsQUi", OP_MUL>;
def VMULP    : SInst<"vmul", "...", "PcQPc">;
def VMLA     : IOpInst<"vmla", "....", "csifUcUsUiQcQsQiQfQUcQUsQUi", OP_MLA>;
def VMLAL    : SOpInst<"vmlal", "(>Q)(>Q)..", "csiUcUsUi", OP_MLAL>;
def VMLS     : IOpInst<"vmls", "....", "csifUcUsUiQcQsQiQfQUcQUsQUi", OP_MLS>;
def VMLSL    : SOpInst<"vmlsl", "(>Q)(>Q)..", "csiUcUsUi", OP_MLSL>;
def VQDMULH  : SInst<"vqdmulh", "...", "siQsQi">;
def VQRDMULH : SInst<"vqrdmulh", "...", "siQsQi">;

let TargetGuard = "v8.1a,neon" in {
def VQRDMLAH : SInst<"vqrdmlah", "....", "siQsQi">;
def VQRDMLSH : SInst<"vqrdmlsh", "....", "siQsQi">;
}

def VQDMLAL  : SInst<"vqdmlal", "(>Q)(>Q)..", "si">;
def VQDMLSL  : SInst<"vqdmlsl", "(>Q)(>Q)..", "si">;
def VMULL    : SInst<"vmull", "(>Q)..", "csiUcUsUiPc">;
def VQDMULL  : SInst<"vqdmull", "(>Q)..", "si">;

////////////////////////////////////////////////////////////////////////////////
// E.3.3 Subtraction
def VSUB    : IOpInst<"vsub", "...",
                      "csilfUcUsUiUlQcQsQiQlQfQUcQUsQUiQUl", OP_SUB>;
def VSUBL   : SOpInst<"vsubl", "(>Q)..", "csiUcUsUi", OP_SUBL>;
def VSUBW   : SOpInst<"vsubw", "(>Q)(>Q).", "csiUcUsUi", OP_SUBW>;
def VQSUB   : SInst<"vqsub", "...", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl">;
def VHSUB   : SInst<"vhsub", "...", "csiUcUsUiQcQsQiQUcQUsQUi">;
def VSUBHN  : IInst<"vsubhn", "<QQ", "silUsUiUl">;
def VRSUBHN : IInst<"vrsubhn", "<QQ", "silUsUiUl">;

////////////////////////////////////////////////////////////////////////////////
// E.3.4 Comparison
def VCEQ  : IOpInst<"vceq", "U..", "csifUcUsUiPcQcQsQiQfQUcQUsQUiQPc", OP_EQ>;
def VCGE  : SOpInst<"vcge", "U..", "csifUcUsUiQcQsQiQfQUcQUsQUi", OP_GE>;
let InstName = "vcge" in
def VCLE  : SOpInst<"vcle", "U..", "csifUcUsUiQcQsQiQfQUcQUsQUi", OP_LE>;
def VCGT  : SOpInst<"vcgt", "U..", "csifUcUsUiQcQsQiQfQUcQUsQUi", OP_GT>;
let InstName = "vcgt" in
def VCLT  : SOpInst<"vclt", "U..", "csifUcUsUiQcQsQiQfQUcQUsQUi", OP_LT>;
let InstName = "vacge" in {
def VCAGE : IInst<"vcage", "U..", "fQf">;
def VCALE : IInst<"vcale", "U..", "fQf">;
}
let InstName = "vacgt" in {
def VCAGT : IInst<"vcagt", "U..", "fQf">;
def VCALT : IInst<"vcalt", "U..", "fQf">;
}
def VTST  : WInst<"vtst", "U..", "csiUcUsUiPcPsQcQsQiQUcQUsQUiQPcQPs">;

////////////////////////////////////////////////////////////////////////////////
// E.3.5 Absolute Difference
def VABD  : SInst<"vabd", "...",  "csiUcUsUifQcQsQiQUcQUsQUiQf">;
def VABDL : SOpInst<"vabdl", "(>Q)..",  "csiUcUsUi", OP_ABDL>;
def VABA  : SOpInst<"vaba", "....", "csiUcUsUiQcQsQiQUcQUsQUi", OP_ABA>;
def VABAL : SOpInst<"vabal", "(>Q)(>Q)..", "csiUcUsUi", OP_ABAL>;

////////////////////////////////////////////////////////////////////////////////
// E.3.6 Max/Min
def VMAX : SInst<"vmax", "...", "csiUcUsUifQcQsQiQUcQUsQUiQf">;
def VMIN : SInst<"vmin", "...", "csiUcUsUifQcQsQiQUcQUsQUiQf">;

////////////////////////////////////////////////////////////////////////////////
// E.3.7 Pairwise Addition
def VPADD  : IInst<"vpadd", "...", "csiUcUsUif">;
def VPADDL : SInst<"vpaddl", ">.",  "csiUcUsUiQcQsQiQUcQUsQUi">;
def VPADAL : SInst<"vpadal", ">>.", "csiUcUsUiQcQsQiQUcQUsQUi">;

////////////////////////////////////////////////////////////////////////////////
// E.3.8-9 Folding Max/Min
def VPMAX : SInst<"vpmax", "...", "csiUcUsUif">;
def VPMIN : SInst<"vpmin", "...", "csiUcUsUif">;

////////////////////////////////////////////////////////////////////////////////
// E.3.10 Reciprocal/Sqrt
def VRECPS  : IInst<"vrecps", "...", "fQf">;
def VRSQRTS : IInst<"vrsqrts", "...", "fQf">;

////////////////////////////////////////////////////////////////////////////////
// E.3.11 Shifts by signed variable
def VSHL   : SInst<"vshl", "..S", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl">;
def VQSHL  : SInst<"vqshl", "..S", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl">;
def VRSHL  : SInst<"vrshl", "..S", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl">;
def VQRSHL : SInst<"vqrshl", "..S", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl">;

////////////////////////////////////////////////////////////////////////////////
// E.3.12 Shifts by constant
let isShift = 1 in {
def VSHR_N     : SInst<"vshr_n", "..I", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl">;
def VSHL_N     : IInst<"vshl_n", "..I", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl">;
def VRSHR_N    : SInst<"vrshr_n", "..I", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl">;
def VSRA_N     : SInst<"vsra_n", "...I", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl">;
def VRSRA_N    : SInst<"vrsra_n", "...I", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl">;
def VQSHL_N    : SInst<"vqshl_n", "..I", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl">;
def VQSHLU_N   : SInst<"vqshlu_n", "U.I", "csilQcQsQiQl">;
def VSHRN_N    : IInst<"vshrn_n", "<QI", "silUsUiUl">;
def VQSHRUN_N  : SInst<"vqshrun_n", "(<U)QI", "sil">;
def VQRSHRUN_N : SInst<"vqrshrun_n", "(<U)QI", "sil">;
def VQSHRN_N   : SInst<"vqshrn_n", "<QI", "silUsUiUl">;
def VRSHRN_N   : IInst<"vrshrn_n", "<QI", "silUsUiUl">;
def VQRSHRN_N  : SInst<"vqrshrn_n", "<QI", "silUsUiUl">;
def VSHLL_N    : SInst<"vshll_n", "(>Q).I", "csiUcUsUi">;

////////////////////////////////////////////////////////////////////////////////
// E.3.13 Shifts with insert
def VSRI_N : WInst<"vsri_n", "...I",
                   "csilUcUsUiUlPcPsQcQsQiQlQUcQUsQUiQUlQPcQPs">;
def VSLI_N : WInst<"vsli_n", "...I",
                   "csilUcUsUiUlPcPsQcQsQiQlQUcQUsQUiQUlQPcQPs">;
}

////////////////////////////////////////////////////////////////////////////////
// E.3.14 Loads and stores of a single vector
def VLD1      : WInst<"vld1", ".(c*!)",
                      "QUcQUsQUiQUlQcQsQiQlQfQPcQPsUcUsUiUlcsilfPcPs">;
def VLD1_X2   : WInst<"vld1_x2", "2(c*!)",
                      "cfilsUcUiUlUsQcQfQiQlQsQUcQUiQUlQUsPcPsQPcQPs">;
def VLD1_X3   : WInst<"vld1_x3", "3(c*!)",
                      "cfilsUcUiUlUsQcQfQiQlQsQUcQUiQUlQUsPcPsQPcQPs">;
def VLD1_X4   : WInst<"vld1_x4", "4(c*!)",
                      "cfilsUcUiUlUsQcQfQiQlQsQUcQUiQUlQUsPcPsQPcQPs">;
def VLD1_LANE : WInst<"vld1_lane", ".(c*!).I",
                      "QUcQUsQUiQUlQcQsQiQlQfQPcQPsUcUsUiUlcsilfPcPs">;
def VLD1_DUP  : WInst<"vld1_dup", ".(c*!)",
                      "QUcQUsQUiQUlQcQsQiQlQfQPcQPsUcUsUiUlcsilfPcPs">;
def VST1      : WInst<"vst1", "v*(.!)",
                      "QUcQUsQUiQUlQcQsQiQlQfQPcQPsUcUsUiUlcsilfPcPs">;
def VST1_X2   : WInst<"vst1_x2", "v*(2!)",
                      "cfilsUcUiUlUsQcQfQiQlQsQUcQUiQUlQUsPcPsQPcQPs">;
def VST1_X3   : WInst<"vst1_x3", "v*(3!)",
                      "cfilsUcUiUlUsQcQfQiQlQsQUcQUiQUlQUsPcPsQPcQPs">;
def VST1_X4   : WInst<"vst1_x4", "v*(4!)",
                      "cfilsUcUiUlUsQcQfQiQlQsQUcQUiQUlQUsPcPsQPcQPs">;
def VST1_LANE : WInst<"vst1_lane", "v*(.!)I",
                      "QUcQUsQUiQUlQcQsQiQlQfQPcQPsUcUsUiUlcsilfPcPs">;
let ArchGuard = "(__ARM_FP & 2)" in {
def VLD1_F16      : WInst<"vld1", ".(c*!)", "hQh">;
def VLD1_X2_F16   : WInst<"vld1_x2", "2(c*!)", "hQh">;
def VLD1_X3_F16   : WInst<"vld1_x3", "3(c*!)", "hQh">;
def VLD1_X4_F16   : WInst<"vld1_x4", "4(c*!)", "hQh">;
def VLD1_LANE_F16 : WInst<"vld1_lane", ".(c*!).I", "hQh">;
def VLD1_DUP_F16  : WInst<"vld1_dup", ".(c*!)", "hQh">;
def VST1_F16      : WInst<"vst1", "v*(.!)", "hQh">;
def VST1_X2_F16   : WInst<"vst1_x2", "v*(2!)", "hQh">;
def VST1_X3_F16   : WInst<"vst1_x3", "v*(3!)", "hQh">;
def VST1_X4_F16   : WInst<"vst1_x4", "v*(4!)", "hQh">;
def VST1_LANE_F16 : WInst<"vst1_lane", "v*(.!)I", "hQh">;
}

////////////////////////////////////////////////////////////////////////////////
// E.3.15 Loads and stores of an N-element structure
def VLD2 : WInst<"vld2", "2(c*!)", "QUcQUsQUiQcQsQiQfQPcQPsUcUsUiUlcsilfPcPs">;
def VLD3 : WInst<"vld3", "3(c*!)", "QUcQUsQUiQcQsQiQfQPcQPsUcUsUiUlcsilfPcPs">;
def VLD4 : WInst<"vld4", "4(c*!)", "QUcQUsQUiQcQsQiQfQPcQPsUcUsUiUlcsilfPcPs">;
def VLD2_DUP  : WInst<"vld2_dup", "2(c*!)",
                      "UcUsUiUlcsilfPcPsQcQfQiQlQsQPcQPsQUcQUiQUlQUs">;
def VLD3_DUP  : WInst<"vld3_dup", "3(c*!)",
                      "UcUsUiUlcsilfPcPsQcQfQiQlQsQPcQPsQUcQUiQUlQUs">;
def VLD4_DUP  : WInst<"vld4_dup", "4(c*!)",
                      "UcUsUiUlcsilfPcPsQcQfQiQlQsQPcQPsQUcQUiQUlQUs">;
def VLD2_LANE : WInst<"vld2_lane", "2(c*!)2I", "QUsQUiQsQiQfQPsUcUsUicsifPcPs">;
def VLD3_LANE : WInst<"vld3_lane", "3(c*!)3I", "QUsQUiQsQiQfQPsUcUsUicsifPcPs">;
def VLD4_LANE : WInst<"vld4_lane", "4(c*!)4I", "QUsQUiQsQiQfQPsUcUsUicsifPcPs">;
def VST2 : WInst<"vst2", "v*(2!)", "QUcQUsQUiQcQsQiQfQPcQPsUcUsUiUlcsilfPcPs">;
def VST3 : WInst<"vst3", "v*(3!)", "QUcQUsQUiQcQsQiQfQPcQPsUcUsUiUlcsilfPcPs">;
def VST4 : WInst<"vst4", "v*(4!)", "QUcQUsQUiQcQsQiQfQPcQPsUcUsUiUlcsilfPcPs">;
def VST2_LANE : WInst<"vst2_lane", "v*(2!)I", "QUsQUiQsQiQfQPsUcUsUicsifPcPs">;
def VST3_LANE : WInst<"vst3_lane", "v*(3!)I", "QUsQUiQsQiQfQPsUcUsUicsifPcPs">;
def VST4_LANE : WInst<"vst4_lane", "v*(4!)I", "QUsQUiQsQiQfQPsUcUsUicsifPcPs">;
let ArchGuard = "(__ARM_FP & 2)" in {
def VLD2_F16      : WInst<"vld2", "2(c*!)", "hQh">;
def VLD3_F16      : WInst<"vld3", "3(c*!)", "hQh">;
def VLD4_F16      : WInst<"vld4", "4(c*!)", "hQh">;
def VLD2_DUP_F16  : WInst<"vld2_dup", "2(c*!)", "hQh">;
def VLD3_DUP_F16  : WInst<"vld3_dup", "3(c*!)", "hQh">;
def VLD4_DUP_F16  : WInst<"vld4_dup", "4(c*!)", "hQh">;
def VLD2_LANE_F16 : WInst<"vld2_lane", "2(c*!)2I", "hQh">;
def VLD3_LANE_F16 : WInst<"vld3_lane", "3(c*!)3I", "hQh">;
def VLD4_LANE_F16 : WInst<"vld4_lane", "4(c*!)4I", "hQh">;
def VST2_F16      : WInst<"vst2", "v*(2!)", "hQh">;
def VST3_F16      : WInst<"vst3", "v*(3!)", "hQh">;
def VST4_F16      : WInst<"vst4", "v*(4!)", "hQh">;
def VST2_LANE_F16 : WInst<"vst2_lane", "v*(2!)I", "hQh">;
def VST3_LANE_F16 : WInst<"vst3_lane", "v*(3!)I", "hQh">;
def VST4_LANE_F16 : WInst<"vst4_lane", "v*(4!)I", "hQh">;
}

////////////////////////////////////////////////////////////////////////////////
// E.3.16 Extract lanes from a vector
let InstName = "vmov" in
def VGET_LANE : IInst<"vget_lane", "1.I",
                      "UcUsUicsiPcPsfQUcQUsQUiQcQsQiQPcQPsQflUlQlQUl">;

////////////////////////////////////////////////////////////////////////////////
// E.3.17 Set lanes within a vector
let InstName = "vmov" in
def VSET_LANE : IInst<"vset_lane", ".1.I",
                      "UcUsUicsiPcPsfQUcQUsQUiQcQsQiQPcQPsQflUlQlQUl">;

////////////////////////////////////////////////////////////////////////////////
// E.3.18 Initialize a vector from bit pattern
def VCREATE : NoTestOpInst<"vcreate", ".(IU>)", "csihfUcUsUiUlPcPsl", OP_CAST> {
  let BigEndianSafe = 1;
}

////////////////////////////////////////////////////////////////////////////////
// E.3.19 Set all lanes to same value
let InstName = "vmov" in {
def VDUP_N   : WOpInst<"vdup_n", ".1",
                       "UcUsUicsiPcPshfQUcQUsQUiQcQsQiQPcQPsQhQflUlQlQUl",
                       OP_DUP>;
def VMOV_N   : WOpInst<"vmov_n", ".1",
                       "UcUsUicsiPcPshfQUcQUsQUiQcQsQiQPcQPsQhQflUlQlQUl",
                       OP_DUP>;
}
let InstName = "" in
def VDUP_LANE: WOpInst<"vdup_lane", ".qI",
                       "UcUsUicsiPcPshfQUcQUsQUiQcQsQiQPcQPsQhQflUlQlQUl",
                       OP_DUP_LN>;

////////////////////////////////////////////////////////////////////////////////
// E.3.20 Combining vectors
def VCOMBINE : NoTestOpInst<"vcombine", "Q..", "csilhfUcUsUiUlPcPs", OP_CONC>;

////////////////////////////////////////////////////////////////////////////////
// E.3.21 Splitting vectors
// Note that the ARM NEON Reference 2.0 mistakenly document the vget_high_f16()
// and vget_low_f16() intrinsics as AArch64-only. We (and GCC) support all
// versions of these intrinsics in both AArch32 and AArch64 architectures. See
// D45668 for more details.
let InstName = "vmov" in {
def VGET_HIGH : NoTestOpInst<"vget_high", ".Q", "csilhfUcUsUiUlPcPs", OP_HI>;
def VGET_LOW  : NoTestOpInst<"vget_low", ".Q", "csilhfUcUsUiUlPcPs", OP_LO>;
}

////////////////////////////////////////////////////////////////////////////////
// E.3.22 Converting vectors

let ArchGuard = "(__ARM_FP & 2)" in {
  def VCVT_F16_F32 : SInst<"vcvt_f16_f32", "(<q)(.!)", "Hf">;
  def VCVT_F32_F16 : SInst<"vcvt_f32_f16", "(>Q)(.!)", "h">;
}

def VCVT_S32     : SInst<"vcvt_s32", "S.",  "fQf">;
def VCVT_U32     : SInst<"vcvt_u32", "U.",  "fQf">;
def VCVT_F32     : SInst<"vcvt_f32", "F(.!)",  "iUiQiQUi">;
let isVCVT_N = 1 in {
def VCVT_N_S32   : SInst<"vcvt_n_s32", "S.I", "fQf">;
def VCVT_N_U32   : SInst<"vcvt_n_u32", "U.I", "fQf">;
def VCVT_N_F32   : SInst<"vcvt_n_f32", "F(.!)I", "iUiQiQUi">;
}

def VMOVN        : IInst<"vmovn", "<Q",  "silUsUiUl">;
def VMOVL        : SInst<"vmovl", "(>Q).",  "csiUcUsUi">;
def VQMOVN       : SInst<"vqmovn", "<Q",  "silUsUiUl">;
def VQMOVUN      : SInst<"vqmovun", "(<U)Q",  "sil">;

////////////////////////////////////////////////////////////////////////////////
// E.3.23-24 Table lookup, Extended table lookup
let InstName = "vtbl" in {
def VTBL1 : WInst<"vtbl1", "..p",  "UccPc">;
def VTBL2 : WInst<"vtbl2", ".2p",  "UccPc">;
def VTBL3 : WInst<"vtbl3", ".3p",  "UccPc">;
def VTBL4 : WInst<"vtbl4", ".4p",  "UccPc">;
}
let InstName = "vtbx" in {
def VTBX1 : WInst<"vtbx1", "...p", "UccPc">;
def VTBX2 : WInst<"vtbx2", "..2p", "UccPc">;
def VTBX3 : WInst<"vtbx3", "..3p", "UccPc">;
def VTBX4 : WInst<"vtbx4", "..4p", "UccPc">;
}

////////////////////////////////////////////////////////////////////////////////
// E.3.25 Operations with a scalar value
def VMLA_LANE     : IOpInst<"vmla_lane", "...qI",
                            "siUsUifQsQiQUsQUiQf", OP_MLA_LN>;
def VMLAL_LANE    : SOpInst<"vmlal_lane", "(>Q)(>Q)..I", "siUsUi", OP_MLAL_LN>;
def VQDMLAL_LANE  : SOpInst<"vqdmlal_lane", "(>Q)(>Q)..I", "si", OP_QDMLAL_LN>;
def VMLS_LANE     : IOpInst<"vmls_lane", "...qI",
                            "siUsUifQsQiQUsQUiQf", OP_MLS_LN>;
def VMLSL_LANE    : SOpInst<"vmlsl_lane", "(>Q)(>Q)..I", "siUsUi", OP_MLSL_LN>;
def VQDMLSL_LANE  : SOpInst<"vqdmlsl_lane", "(>Q)(>Q)..I", "si", OP_QDMLSL_LN>;
def VMUL_N        : IOpInst<"vmul_n", "..1", "sifUsUiQsQiQfQUsQUi", OP_MUL_N>;
def VMUL_LANE     : IOpInst<"vmul_lane", "..qI",
                            "sifUsUiQsQiQfQUsQUi", OP_MUL_LN>;
def VMULL_N       : SOpInst<"vmull_n", "(>Q).1", "siUsUi", OP_MULL_N>;
def VMULL_LANE    : SOpInst<"vmull_lane", "(>Q)..I", "siUsUi", OP_MULL_LN>;
def VQDMULL_N     : SOpInst<"vqdmull_n", "(>Q).1", "si", OP_QDMULL_N>;
def VQDMULL_LANE  : SOpInst<"vqdmull_lane", "(>Q)..I", "si", OP_QDMULL_LN>;
def VQDMULH_N     : SOpInst<"vqdmulh_n", "..1", "siQsQi", OP_QDMULH_N>;
def VQRDMULH_N    : SOpInst<"vqrdmulh_n", "..1", "siQsQi", OP_QRDMULH_N>;

let ArchGuard = "!defined(__aarch64__) && !defined(__arm64ec__)" in {
def VQDMULH_LANE  : SOpInst<"vqdmulh_lane", "..qI", "siQsQi", OP_QDMULH_LN>;
def VQRDMULH_LANE : SOpInst<"vqrdmulh_lane", "..qI", "siQsQi", OP_QRDMULH_LN>;
}
let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)" in {
def A64_VQDMULH_LANE  : SInst<"vqdmulh_lane", "..(!q)I", "siQsQi">;
def A64_VQRDMULH_LANE : SInst<"vqrdmulh_lane", "..(!q)I", "siQsQi">;
}

let TargetGuard = "v8.1a,neon" in {
def VQRDMLAH_LANE : SOpInst<"vqrdmlah_lane", "...qI", "siQsQi", OP_QRDMLAH_LN>;
def VQRDMLSH_LANE : SOpInst<"vqrdmlsh_lane", "...qI", "siQsQi", OP_QRDMLSH_LN>;
}

def VMLA_N        : IOpInst<"vmla_n", "...1", "siUsUifQsQiQUsQUiQf", OP_MLA_N>;
def VMLAL_N       : SOpInst<"vmlal_n", "(>Q)(>Q).1", "siUsUi", OP_MLAL_N>;
def VQDMLAL_N     : SOpInst<"vqdmlal_n", "(>Q)(>Q).1", "si", OP_QDMLAL_N>;
def VMLS_N        : IOpInst<"vmls_n", "...1", "siUsUifQsQiQUsQUiQf", OP_MLS_N>;
def VMLSL_N       : SOpInst<"vmlsl_n", "(>Q)(>Q).1", "siUsUi", OP_MLSL_N>;
def VQDMLSL_N     : SOpInst<"vqdmlsl_n", "(>Q)(>Q).1", "si", OP_QDMLSL_N>;

////////////////////////////////////////////////////////////////////////////////
// E.3.26 Vector Extract
def VEXT : WInst<"vext", "...I",
                 "cUcPcsUsPsiUilUlfQcQUcQPcQsQUsQPsQiQUiQlQUlQf">;

////////////////////////////////////////////////////////////////////////////////
// E.3.27 Reverse vector elements
def VREV64 : WOpInst<"vrev64", "..", "csiUcUsUiPcPsfQcQsQiQUcQUsQUiQPcQPsQf",
                  OP_REV64>;
def VREV32 : WOpInst<"vrev32", "..", "csUcUsPcPsQcQsQUcQUsQPcQPs", OP_REV32>;
def VREV16 : WOpInst<"vrev16", "..", "cUcPcQcQUcQPc", OP_REV16>;

////////////////////////////////////////////////////////////////////////////////
// E.3.28 Other single operand arithmetic
def VABS    : SInst<"vabs", "..", "csifQcQsQiQf">;
def VQABS   : SInst<"vqabs", "..", "csiQcQsQi">;
def VNEG    : SOpInst<"vneg", "..", "csifQcQsQiQf", OP_NEG>;
def VQNEG   : SInst<"vqneg", "..", "csiQcQsQi">;
def VCLS    : SInst<"vcls", "S.", "csiUcUsUiQcQsQiQUcQUsQUi">;
def VCLZ    : IInst<"vclz", "..", "csiUcUsUiQcQsQiQUcQUsQUi">;
def VCNT    : WInst<"vcnt", "..", "UccPcQUcQcQPc">;
def VRECPE  : SInst<"vrecpe", "..", "fUiQfQUi">;
def VRSQRTE : SInst<"vrsqrte", "..", "fUiQfQUi">;

////////////////////////////////////////////////////////////////////////////////
// E.3.29 Logical operations
def VMVN : LOpInst<"vmvn", "..", "csiUcUsUiPcQcQsQiQUcQUsQUiQPc", OP_NOT>;
def VAND : LOpInst<"vand", "...", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl", OP_AND>;
def VORR : LOpInst<"vorr", "...", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl", OP_OR>;
def VEOR : LOpInst<"veor", "...", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl", OP_XOR>;
def VBIC : LOpInst<"vbic", "...", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl", OP_ANDN>;
def VORN : LOpInst<"vorn", "...", "csilUcUsUiUlQcQsQiQlQUcQUsQUiQUl", OP_ORN>;
let isHiddenLInst = 1 in
def VBSL : SInst<"vbsl", ".U..",
                "csilUcUsUiUlfPcPsQcQsQiQlQUcQUsQUiQUlQfQPcQPs">;

////////////////////////////////////////////////////////////////////////////////
// E.3.30 Transposition operations
def VTRN : WInst<"vtrn", "2..", "csiUcUsUifPcPsQcQsQiQUcQUsQUiQfQPcQPs">;
def VZIP : WInst<"vzip", "2..", "csiUcUsUifPcPsQcQsQiQUcQUsQUiQfQPcQPs">;
def VUZP : WInst<"vuzp", "2..", "csiUcUsUifPcPsQcQsQiQUcQUsQUiQfQPcQPs">;

////////////////////////////////////////////////////////////////////////////////

class REINTERPRET_CROSS_SELF<string Types> :
  NoTestOpInst<"vreinterpret", "..", Types, OP_REINT> {
    let CartesianProductWith = Types;
}

multiclass REINTERPRET_CROSS_TYPES<string TypesA, string TypesB> {
  def AXB: NoTestOpInst<"vreinterpret", "..", TypesA, OP_REINT> {
    let CartesianProductWith = TypesB;
  }
  def BXA: NoTestOpInst<"vreinterpret", "..", TypesB, OP_REINT> {
    let CartesianProductWith = TypesA;
  }
}

// E.3.31 Vector reinterpret cast operations
def VREINTERPRET : REINTERPRET_CROSS_SELF<"csilUcUsUiUlhfPcPsQcQsQiQlQUcQUsQUiQUlQhQfQPcQPs"> {
  let ArchGuard = "!defined(__aarch64__) && !defined(__arm64ec__)";
  let BigEndianSafe = 1;
}

////////////////////////////////////////////////////////////////////////////////
// Vector fused multiply-add operations

let ArchGuard = "defined(__ARM_FEATURE_FMA)" in {
  def VFMA : SInst<"vfma", "....", "fQf">;
  def VFMS : SOpInst<"vfms", "....", "fQf", OP_FMLS>;
  def FMLA_N_F32 : SOpInst<"vfma_n", "...1", "fQf", OP_FMLA_N>;
}

////////////////////////////////////////////////////////////////////////////////
// fp16 vector operations
def SCALAR_HALF_GET_LANE : IOpInst<"vget_lane", "1.I", "h", OP_SCALAR_HALF_GET_LN>;
def SCALAR_HALF_SET_LANE : IOpInst<"vset_lane", ".1.I", "h", OP_SCALAR_HALF_SET_LN>;
def SCALAR_HALF_GET_LANEQ : IOpInst<"vget_lane", "1.I", "Qh", OP_SCALAR_HALF_GET_LNQ>;
def SCALAR_HALF_SET_LANEQ : IOpInst<"vset_lane", ".1.I", "Qh", OP_SCALAR_HALF_SET_LNQ>;

////////////////////////////////////////////////////////////////////////////////
// Non poly128_t vaddp for Arm and AArch64
// TODO: poly128_t not implemented on arm32
def VADDP   : WInst<"vadd", "...", "PcPsPlQPcQPsQPl">;

////////////////////////////////////////////////////////////////////////////////
// AArch64 Intrinsics

let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)" in {

////////////////////////////////////////////////////////////////////////////////
// Load/Store
def LD1 : WInst<"vld1", ".(c*!)", "dQdPlQPl">;
def LD2 : WInst<"vld2", "2(c*!)", "QUlQldQdPlQPl">;
def LD3 : WInst<"vld3", "3(c*!)", "QUlQldQdPlQPl">;
def LD4 : WInst<"vld4", "4(c*!)", "QUlQldQdPlQPl">;
def ST1 : WInst<"vst1", "v*(.!)", "dQdPlQPl">;
def ST2 : WInst<"vst2", "v*(2!)", "QUlQldQdPlQPl">;
def ST3 : WInst<"vst3", "v*(3!)", "QUlQldQdPlQPl">;
def ST4 : WInst<"vst4", "v*(4!)", "QUlQldQdPlQPl">;

def LD1_X2 : WInst<"vld1_x2", "2(c*!)",
                   "dQdPlQPl">;
def LD1_X3 : WInst<"vld1_x3", "3(c*!)",
                   "dQdPlQPl">;
def LD1_X4 : WInst<"vld1_x4", "4(c*!)",
                   "dQdPlQPl">;

def ST1_X2 : WInst<"vst1_x2", "v*(2!)", "dQdPlQPl">;
def ST1_X3 : WInst<"vst1_x3", "v*(3!)", "dQdPlQPl">;
def ST1_X4 : WInst<"vst1_x4", "v*(4!)", "dQdPlQPl">;

def LD1_LANE : WInst<"vld1_lane", ".(c*!).I", "dQdPlQPl">;
def LD2_LANE : WInst<"vld2_lane", "2(c*!)2I", "lUlQcQUcQPcQlQUldQdPlQPl">;
def LD3_LANE : WInst<"vld3_lane", "3(c*!)3I", "lUlQcQUcQPcQlQUldQdPlQPl">;
def LD4_LANE : WInst<"vld4_lane", "4(c*!)4I", "lUlQcQUcQPcQlQUldQdPlQPl">;
def ST1_LANE : WInst<"vst1_lane", "v*(.!)I", "dQdPlQPl">;
def ST2_LANE : WInst<"vst2_lane", "v*(2!)I", "lUlQcQUcQPcQlQUldQdPlQPl">;
def ST3_LANE : WInst<"vst3_lane", "v*(3!)I", "lUlQcQUcQPcQlQUldQdPlQPl">;
def ST4_LANE : WInst<"vst4_lane", "v*(4!)I", "lUlQcQUcQPcQlQUldQdPlQPl">;

def LD1_DUP  : WInst<"vld1_dup", ".(c*!)", "dQdPlQPl">;
def LD2_DUP  : WInst<"vld2_dup", "2(c*!)", "dQdPlQPl">;
def LD3_DUP  : WInst<"vld3_dup", "3(c*!)", "dQdPlQPl">;
def LD4_DUP  : WInst<"vld4_dup", "4(c*!)", "dQdPlQPl">;

def VLDRQ : WInst<"vldrq", "1(c*!)", "Pk">;
def VSTRQ : WInst<"vstrq", "v*(1!)", "Pk">;

////////////////////////////////////////////////////////////////////////////////
// Addition
def ADD : IOpInst<"vadd", "...", "dQd", OP_ADD>;

////////////////////////////////////////////////////////////////////////////////
// Subtraction
def SUB : IOpInst<"vsub", "...", "dQd", OP_SUB>;

////////////////////////////////////////////////////////////////////////////////
// Multiplication
def MUL     : IOpInst<"vmul", "...", "dQd", OP_MUL>;
def MLA     : IOpInst<"vmla", "....", "dQd", OP_MLA>;
def MLS     : IOpInst<"vmls", "....", "dQd", OP_MLS>;

////////////////////////////////////////////////////////////////////////////////
// Multiplication Extended
def MULX : SInst<"vmulx", "...", "fdQfQd">;

////////////////////////////////////////////////////////////////////////////////
// Division
def FDIV : IOpInst<"vdiv", "...",  "fdQfQd", OP_DIV>;

////////////////////////////////////////////////////////////////////////////////
// Vector fused multiply-add operations
def FMLA : SInst<"vfma", "....", "dQd">;
def FMLS : SOpInst<"vfms", "....", "dQd", OP_FMLS>;

////////////////////////////////////////////////////////////////////////////////
// MUL, MLA, MLS, FMA, FMS definitions with scalar argument
def VMUL_N_A64 : IOpInst<"vmul_n", "..1", "Qd", OP_MUL_N>;

def FMLA_N : SOpInst<"vfma_n", "...1", "dQd", OP_FMLA_N>;
def FMLS_N : SOpInst<"vfms_n", "...1", "fdQfQd", OP_FMLS_N>;

////////////////////////////////////////////////////////////////////////////////
// Logical operations
def BSL : SInst<"vbsl", ".U..", "dPlQdQPl">;

////////////////////////////////////////////////////////////////////////////////
// Absolute Difference
def ABD  : SInst<"vabd", "...",  "dQd">;

////////////////////////////////////////////////////////////////////////////////
// saturating absolute/negate
def ABS    : SInst<"vabs", "..", "dQdlQl">;
def QABS   : SInst<"vqabs", "..", "lQl">;
def NEG    : SOpInst<"vneg", "..", "dlQdQl", OP_NEG>;
def QNEG   : SInst<"vqneg", "..", "lQl">;

////////////////////////////////////////////////////////////////////////////////
// Signed Saturating Accumulated of Unsigned Value
def SUQADD : SInst<"vuqadd", "..U", "csilQcQsQiQl">;

////////////////////////////////////////////////////////////////////////////////
// Unsigned Saturating Accumulated of Signed Value
def USQADD : SInst<"vsqadd", "..S", "UcUsUiUlQUcQUsQUiQUl">;

////////////////////////////////////////////////////////////////////////////////
// Reciprocal/Sqrt
def FRECPS  : IInst<"vrecps", "...", "dQd">;
def FRSQRTS : IInst<"vrsqrts", "...", "dQd">;
def FRECPE  : SInst<"vrecpe", "..", "dQd">;
def FRSQRTE : SInst<"vrsqrte", "..", "dQd">;
def FSQRT   : SInst<"vsqrt", "..", "fdQfQd">;

////////////////////////////////////////////////////////////////////////////////
// bitwise reverse
def RBIT : IInst<"vrbit", "..", "cUcPcQcQUcQPc">;

////////////////////////////////////////////////////////////////////////////////
// Integer extract and narrow to high
def XTN2 : SOpInst<"vmovn_high", "(<Q)<Q", "silUsUiUl", OP_XTN>;

////////////////////////////////////////////////////////////////////////////////
// Signed integer saturating extract and unsigned narrow to high
def SQXTUN2 : SOpInst<"vqmovun_high", "(<U)(<Uq).", "HsHiHl", OP_SQXTUN>;

////////////////////////////////////////////////////////////////////////////////
// Integer saturating extract and narrow to high
def QXTN2 : SOpInst<"vqmovn_high", "(<Q)<Q", "silUsUiUl", OP_QXTN>;

////////////////////////////////////////////////////////////////////////////////
// Converting vectors

def VCVT_F32_F64 : SInst<"vcvt_f32_f64", "(<q).", "Qd">;
def VCVT_F64_F32 : SInst<"vcvt_f64_f32", "(>Q).", "f">;

def VCVT_S64 : SInst<"vcvt_s64", "S.",  "dQd">;
def VCVT_U64 : SInst<"vcvt_u64", "U.",  "dQd">;
def VCVT_F64 : SInst<"vcvt_f64", "F(.!)",  "lUlQlQUl">;

def VCVT_HIGH_F16_F32 : SOpInst<"vcvt_high_f16", "<(<q!)Q", "Hf", OP_VCVT_NA_HI_F16>;
def VCVT_HIGH_F32_F16 : SOpInst<"vcvt_high_f32", "(>Q)(Q!)", "h", OP_VCVT_EX_HI_F32>;
def VCVT_HIGH_F32_F64 : SOpInst<"vcvt_high_f32", "(<Q)(F<!)Q", "d", OP_VCVT_NA_HI_F32>;
def VCVT_HIGH_F64_F32 : SOpInst<"vcvt_high_f64", "(>Q)(Q!)", "f", OP_VCVT_EX_HI_F64>;

def VCVTX_F32_F64      : SInst<"vcvtx_f32", "(F<)(Q!)",  "d">;
def VCVTX_HIGH_F32_F64 : SOpInst<"vcvtx_high_f32", "(<Q)(F<!)Q", "d", OP_VCVTX_HI>;

////////////////////////////////////////////////////////////////////////////////
// Comparison
def FCAGE : IInst<"vcage", "U..", "dQd">;
def FCAGT : IInst<"vcagt", "U..", "dQd">;
def FCALE : IInst<"vcale", "U..", "dQd">;
def FCALT : IInst<"vcalt", "U..", "dQd">;
def CMTST  : WInst<"vtst", "U..", "lUlPlQlQUlQPl">;
def CFMEQ  : SOpInst<"vceq", "U..", "lUldQdQlQUlPlQPl", OP_EQ>;
def CFMGE  : SOpInst<"vcge", "U..", "lUldQdQlQUl", OP_GE>;
def CFMLE  : SOpInst<"vcle", "U..", "lUldQdQlQUl", OP_LE>;
def CFMGT  : SOpInst<"vcgt", "U..", "lUldQdQlQUl", OP_GT>;
def CFMLT  : SOpInst<"vclt", "U..", "lUldQdQlQUl", OP_LT>;

def CMEQ  : SInst<"vceqz", "U.",
                  "csilfUcUsUiUlPcPlQcQsQiQlQfQUcQUsQUiQUlQPcdQdQPl">;
def CMGE  : SInst<"vcgez", "U.", "csilfdQcQsQiQlQfQd">;
def CMLE  : SInst<"vclez", "U.", "csilfdQcQsQiQlQfQd">;
def CMGT  : SInst<"vcgtz", "U.", "csilfdQcQsQiQlQfQd">;
def CMLT  : SInst<"vcltz", "U.", "csilfdQcQsQiQlQfQd">;

////////////////////////////////////////////////////////////////////////////////
// Max/Min Integer
def MAX : SInst<"vmax", "...", "dQd">;
def MIN : SInst<"vmin", "...", "dQd">;

////////////////////////////////////////////////////////////////////////////////
// Pairwise Max/Min
def MAXP : SInst<"vpmax", "...", "QcQsQiQUcQUsQUiQfQd">;
def MINP : SInst<"vpmin", "...", "QcQsQiQUcQUsQUiQfQd">;

////////////////////////////////////////////////////////////////////////////////
// Pairwise MaxNum/MinNum Floating Point
def FMAXNMP : SInst<"vpmaxnm", "...", "fQfQd">;
def FMINNMP : SInst<"vpminnm", "...", "fQfQd">;

////////////////////////////////////////////////////////////////////////////////
// Pairwise Addition
def ADDP  : IInst<"vpadd", "...", "QcQsQiQlQUcQUsQUiQUlQfQd">;

////////////////////////////////////////////////////////////////////////////////
// Shifts by constant
let isShift = 1 in {
// Left shift long high
def SHLL_HIGH_N    : SOpInst<"vshll_high_n", ">.I", "HcHsHiHUcHUsHUi",
                             OP_LONG_HI>;

////////////////////////////////////////////////////////////////////////////////
def SRI_N : WInst<"vsri_n", "...I", "PlQPl">;
def SLI_N : WInst<"vsli_n", "...I", "PlQPl">;

// Right shift narrow high
def SHRN_HIGH_N    : IOpInst<"vshrn_high_n", "<(<q).I",
                             "HsHiHlHUsHUiHUl", OP_NARROW_HI>;
def QSHRUN_HIGH_N  : SOpInst<"vqshrun_high_n", "<(<q).I",
                             "HsHiHl", OP_NARROW_HI>;
def RSHRN_HIGH_N   : IOpInst<"vrshrn_high_n", "<(<q).I",
                             "HsHiHlHUsHUiHUl", OP_NARROW_HI>;
def QRSHRUN_HIGH_N : SOpInst<"vqrshrun_high_n", "<(<q).I",
                             "HsHiHl", OP_NARROW_HI>;
def QSHRN_HIGH_N   : SOpInst<"vqshrn_high_n", "<(<q).I",
                             "HsHiHlHUsHUiHUl", OP_NARROW_HI>;
def QRSHRN_HIGH_N  : SOpInst<"vqrshrn_high_n", "<(<q).I",
                             "HsHiHlHUsHUiHUl", OP_NARROW_HI>;
}

////////////////////////////////////////////////////////////////////////////////
// Converting vectors
def VMOVL_HIGH   : SOpInst<"vmovl_high", ">.", "HcHsHiHUcHUsHUi", OP_MOVL_HI>;

let isVCVT_N = 1 in {
def CVTF_N_F64   : SInst<"vcvt_n_f64", "F(.!)I", "lUlQlQUl">;
def FCVTZS_N_S64 : SInst<"vcvt_n_s64", "S.I", "dQd">;
def FCVTZS_N_U64 : SInst<"vcvt_n_u64", "U.I", "dQd">;
}

////////////////////////////////////////////////////////////////////////////////
// 3VDiff class using high 64-bit in operands
def VADDL_HIGH   : SOpInst<"vaddl_high", "(>Q)QQ", "csiUcUsUi", OP_ADDLHi>;
def VADDW_HIGH   : SOpInst<"vaddw_high", "(>Q)(>Q)Q", "csiUcUsUi", OP_ADDWHi>;
def VSUBL_HIGH   : SOpInst<"vsubl_high", "(>Q)QQ", "csiUcUsUi", OP_SUBLHi>;
def VSUBW_HIGH   : SOpInst<"vsubw_high", "(>Q)(>Q)Q", "csiUcUsUi", OP_SUBWHi>;

def VABDL_HIGH   : SOpInst<"vabdl_high", "(>Q)QQ",  "csiUcUsUi", OP_ABDLHi>;
def VABAL_HIGH   : SOpInst<"vabal_high", "(>Q)(>Q)QQ", "csiUcUsUi", OP_ABALHi>;

def VMULL_HIGH   : SOpInst<"vmull_high", "(>Q)QQ", "csiUcUsUiPc", OP_MULLHi>;
def VMULL_HIGH_N : SOpInst<"vmull_high_n", "(>Q)Q1", "siUsUi", OP_MULLHi_N>;
def VMLAL_HIGH   : SOpInst<"vmlal_high", "(>Q)(>Q)QQ", "csiUcUsUi", OP_MLALHi>;
def VMLAL_HIGH_N : SOpInst<"vmlal_high_n", "(>Q)(>Q)Q1", "siUsUi", OP_MLALHi_N>;
def VMLSL_HIGH   : SOpInst<"vmlsl_high", "(>Q)(>Q)QQ", "csiUcUsUi", OP_MLSLHi>;
def VMLSL_HIGH_N : SOpInst<"vmlsl_high_n", "(>Q)(>Q)Q1", "siUsUi", OP_MLSLHi_N>;

def VADDHN_HIGH  : SOpInst<"vaddhn_high", "(<Q)<QQ", "silUsUiUl", OP_ADDHNHi>;
def VRADDHN_HIGH : SOpInst<"vraddhn_high", "(<Q)<QQ", "silUsUiUl", OP_RADDHNHi>;
def VSUBHN_HIGH  : SOpInst<"vsubhn_high", "(<Q)<QQ", "silUsUiUl", OP_SUBHNHi>;
def VRSUBHN_HIGH : SOpInst<"vrsubhn_high", "(<Q)<QQ", "silUsUiUl", OP_RSUBHNHi>;

def VQDMULL_HIGH : SOpInst<"vqdmull_high", "(>Q)QQ", "si", OP_QDMULLHi>;
def VQDMULL_HIGH_N : SOpInst<"vqdmull_high_n", "(>Q)Q1", "si", OP_QDMULLHi_N>;
def VQDMLAL_HIGH : SOpInst<"vqdmlal_high", "(>Q)(>Q)QQ", "si", OP_QDMLALHi>;
def VQDMLAL_HIGH_N : SOpInst<"vqdmlal_high_n", "(>Q)(>Q)Q1", "si", OP_QDMLALHi_N>;
def VQDMLSL_HIGH : SOpInst<"vqdmlsl_high", "(>Q)(>Q)QQ", "si", OP_QDMLSLHi>;
def VQDMLSL_HIGH_N : SOpInst<"vqdmlsl_high_n", "(>Q)(>Q)Q1", "si", OP_QDMLSLHi_N>;
let TargetGuard = "aes,neon" in {
  def VMULL_P64    : SInst<"vmull", "(1>)11", "Pl">;
  def VMULL_HIGH_P64 : SOpInst<"vmull_high", "(1>)..", "HPl", OP_MULLHi_P64>;
}


////////////////////////////////////////////////////////////////////////////////
// Extract or insert element from vector
def GET_LANE : IInst<"vget_lane", "1.I", "dQdPlQPl">;
def SET_LANE : IInst<"vset_lane", ".1.I", "dQdPlQPl">;
def COPY_LANE : IOpInst<"vcopy_lane", "..I.I",
                        "csilUcUsUiUlPcPsPlfd", OP_COPY_LN>;
def COPYQ_LANE : IOpInst<"vcopy_lane", "..IqI",
                        "QcQsQiQlQUcQUsQUiQUlQPcQPsQfQdQPl", OP_COPY_LN>;
def COPY_LANEQ : IOpInst<"vcopy_laneq", "..IQI",
                     "csilPcPsPlUcUsUiUlfd", OP_COPY_LN> {
  let isLaneQ = 1;
}
def COPYQ_LANEQ : IOpInst<"vcopy_laneq", "..I.I",
                     "QcQsQiQlQUcQUsQUiQUlQPcQPsQfQdQPl", OP_COPY_LN> {
  let isLaneQ = 1;
}

////////////////////////////////////////////////////////////////////////////////
// Set all lanes to same value
def VDUP_LANE1: WOpInst<"vdup_lane", ".qI", "dQdPlQPl", OP_DUP_LN>;
def VDUP_LANE2: WOpInst<"vdup_laneq", ".QI",
                  "csilUcUsUiUlPcPshfdQcQsQiQlQPcQPsQUcQUsQUiQUlQhQfQdPlQPl",
                        OP_DUP_LN> {
  let isLaneQ = 1;
}
def DUP_N   : WOpInst<"vdup_n", ".1", "dQdPlQPl", OP_DUP>;
def MOV_N   : WOpInst<"vmov_n", ".1", "dQdPlQPl", OP_DUP>;

////////////////////////////////////////////////////////////////////////////////
def COMBINE : NoTestOpInst<"vcombine", "Q..", "dPl", OP_CONC>;

////////////////////////////////////////////////////////////////////////////////
//Initialize a vector from bit pattern
def CREATE : NoTestOpInst<"vcreate", ".(IU>)", "dPl", OP_CAST> {
  let BigEndianSafe = 1;
}

////////////////////////////////////////////////////////////////////////////////

def VMLA_LANEQ   : IOpInst<"vmla_laneq", "...QI",
                           "siUsUifQsQiQUsQUiQf", OP_MLA_LN> {
  let isLaneQ = 1;
}
def VMLS_LANEQ   : IOpInst<"vmls_laneq", "...QI",
                           "siUsUifQsQiQUsQUiQf", OP_MLS_LN> {
  let isLaneQ = 1;
}

def VFMA_LANE    : IInst<"vfma_lane", "...qI", "fdQfQd">;
def VFMA_LANEQ   : IInst<"vfma_laneq", "...QI", "fdQfQd"> {
  let isLaneQ = 1;
}
def VFMS_LANE    : IOpInst<"vfms_lane", "...qI", "fdQfQd", OP_FMS_LN>;
def VFMS_LANEQ   : IOpInst<"vfms_laneq", "...QI", "fdQfQd", OP_FMS_LNQ> {
  let isLaneQ = 1;
}

def VMLAL_LANEQ  : SOpInst<"vmlal_laneq", "(>Q)(>Q).QI", "siUsUi", OP_MLAL_LN> {
  let isLaneQ = 1;
}
def VMLAL_HIGH_LANE   : SOpInst<"vmlal_high_lane", "(>Q)(>Q)Q.I", "siUsUi",
                                OP_MLALHi_LN>;
def VMLAL_HIGH_LANEQ  : SOpInst<"vmlal_high_laneq", "(>Q)(>Q)QQI", "siUsUi",
                                OP_MLALHi_LN> {
  let isLaneQ = 1;
}
def VMLSL_LANEQ  : SOpInst<"vmlsl_laneq", "(>Q)(>Q).QI", "siUsUi", OP_MLSL_LN> {
  let isLaneQ = 1;
}
def VMLSL_HIGH_LANE   : SOpInst<"vmlsl_high_lane", "(>Q)(>Q)Q.I", "siUsUi",
                                OP_MLSLHi_LN>;
def VMLSL_HIGH_LANEQ  : SOpInst<"vmlsl_high_laneq", "(>Q)(>Q)QQI", "siUsUi",
                                OP_MLSLHi_LN> {
  let isLaneQ = 1;
}

def VQDMLAL_LANEQ  : SOpInst<"vqdmlal_laneq", "(>Q)(>Q).QI", "si", OP_QDMLAL_LN> {
  let isLaneQ = 1;
}
def VQDMLAL_HIGH_LANE   : SOpInst<"vqdmlal_high_lane", "(>Q)(>Q)Q.I", "si",
                                OP_QDMLALHi_LN>;
def VQDMLAL_HIGH_LANEQ  : SOpInst<"vqdmlal_high_laneq", "(>Q)(>Q)QQI", "si",
                                OP_QDMLALHi_LN> {
  let isLaneQ = 1;
}
def VQDMLSL_LANEQ  : SOpInst<"vqdmlsl_laneq", "(>Q)(>Q).QI", "si", OP_QDMLSL_LN> {
  let isLaneQ = 1;
}
def VQDMLSL_HIGH_LANE   : SOpInst<"vqdmlsl_high_lane", "(>Q)(>Q)Q.I", "si",
                                OP_QDMLSLHi_LN>;
def VQDMLSL_HIGH_LANEQ  : SOpInst<"vqdmlsl_high_laneq", "(>Q)(>Q)QQI", "si",
                                OP_QDMLSLHi_LN> {
  let isLaneQ = 1;
}

// Newly add double parameter for vmul_lane in aarch64
// Note: d type is handled by SCALAR_VMUL_LANE
def VMUL_LANE_A64 : IOpInst<"vmul_lane", "..qI", "Qd", OP_MUL_LN>;

// Note: d type is handled by SCALAR_VMUL_LANEQ
def VMUL_LANEQ   : IOpInst<"vmul_laneq", "..QI",
                           "sifUsUiQsQiQUsQUiQfQd", OP_MUL_LN> {
  let isLaneQ = 1;
}
def VMULL_LANEQ  : SOpInst<"vmull_laneq", "(>Q).QI", "siUsUi", OP_MULL_LN> {
  let isLaneQ = 1;
}
def VMULL_HIGH_LANE   : SOpInst<"vmull_high_lane", "(>Q)Q.I", "siUsUi",
                                OP_MULLHi_LN>;
def VMULL_HIGH_LANEQ  : SOpInst<"vmull_high_laneq", "(>Q)QQI", "siUsUi",
                                OP_MULLHi_LN> {
  let isLaneQ = 1;
}

def VQDMULL_LANEQ  : SOpInst<"vqdmull_laneq", "(>Q).QI", "si", OP_QDMULL_LN> {
  let isLaneQ = 1;
}
def VQDMULL_HIGH_LANE   : SOpInst<"vqdmull_high_lane", "(>Q)Q.I", "si",
                                  OP_QDMULLHi_LN>;
def VQDMULL_HIGH_LANEQ  : SOpInst<"vqdmull_high_laneq", "(>Q)QQI", "si",
                                  OP_QDMULLHi_LN> {
  let isLaneQ = 1;
}

let isLaneQ = 1 in {
def VQDMULH_LANEQ  : SInst<"vqdmulh_laneq", "..QI", "siQsQi">;
def VQRDMULH_LANEQ : SInst<"vqrdmulh_laneq", "..QI", "siQsQi">;
}
let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "v8.1a,neon" in {
def VQRDMLAH_LANEQ : SOpInst<"vqrdmlah_laneq", "...QI", "siQsQi", OP_QRDMLAH_LN> {
  let isLaneQ = 1;
}
def VQRDMLSH_LANEQ : SOpInst<"vqrdmlsh_laneq", "...QI", "siQsQi", OP_QRDMLSH_LN> {
  let isLaneQ = 1;
}
} // ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "v8.1a"

// Note: d type implemented by SCALAR_VMULX_LANE
def VMULX_LANE : IOpInst<"vmulx_lane", "..qI", "fQfQd", OP_MULX_LN>;
// Note: d type is implemented by SCALAR_VMULX_LANEQ
def VMULX_LANEQ : IOpInst<"vmulx_laneq", "..QI", "fQfQd", OP_MULX_LN> {
  let isLaneQ = 1;
}

////////////////////////////////////////////////////////////////////////////////
// Across vectors class
def VADDLV  : SInst<"vaddlv", "(1>).", "csiUcUsUiQcQsQiQUcQUsQUi">;
def VMAXV   : SInst<"vmaxv", "1.", "csifUcUsUiQcQsQiQUcQUsQUiQfQd">;
def VMINV   : SInst<"vminv", "1.", "csifUcUsUiQcQsQiQUcQUsQUiQfQd">;
def VADDV   : SInst<"vaddv", "1.", "csifUcUsUiQcQsQiQUcQUsQUiQfQdQlQUl">;
def FMAXNMV : SInst<"vmaxnmv", "1.", "fQfQd">;
def FMINNMV : SInst<"vminnmv", "1.", "fQfQd">;

////////////////////////////////////////////////////////////////////////////////
// Newly added Vector Extract for f64
def VEXT_A64 : WInst<"vext", "...I", "dQdPlQPl">;

////////////////////////////////////////////////////////////////////////////////
// Crypto
let ArchGuard = "__ARM_ARCH >= 8", TargetGuard = "aes,neon" in {
def AESE : SInst<"vaese", "...", "QUc">;
def AESD : SInst<"vaesd", "...", "QUc">;
def AESMC : SInst<"vaesmc", "..", "QUc">;
def AESIMC : SInst<"vaesimc", "..", "QUc">;
}

let ArchGuard = "__ARM_ARCH >= 8", TargetGuard = "sha2,neon" in {
def SHA1H : SInst<"vsha1h", "11", "Ui">;
def SHA1SU1 : SInst<"vsha1su1", "...", "QUi">;
def SHA256SU0 : SInst<"vsha256su0", "...", "QUi">;

def SHA1C : SInst<"vsha1c", "..1.", "QUi">;
def SHA1P : SInst<"vsha1p", "..1.", "QUi">;
def SHA1M : SInst<"vsha1m", "..1.", "QUi">;
def SHA1SU0 : SInst<"vsha1su0", "....", "QUi">;
def SHA256H : SInst<"vsha256h", "....", "QUi">;
def SHA256H2 : SInst<"vsha256h2", "....", "QUi">;
def SHA256SU1 : SInst<"vsha256su1", "....", "QUi">;
}

let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "sha3,neon" in {
def BCAX : SInst<"vbcax", "....", "QUcQUsQUiQUlQcQsQiQl">;
def EOR3 : SInst<"veor3", "....", "QUcQUsQUiQUlQcQsQiQl">;
def RAX1 : SInst<"vrax1", "...", "QUl">;

let isVXAR = 1 in {
def XAR :  SInst<"vxar", "...I", "QUl">;
}
}

let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "sha3,neon" in {
def SHA512SU0 : SInst<"vsha512su0", "...", "QUl">;
def SHA512su1 : SInst<"vsha512su1", "....", "QUl">;
def SHA512H : SInst<"vsha512h", "....", "QUl">;
def SHA512H2 : SInst<"vsha512h2", "....", "QUl">;
}

let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "sm4,neon" in {
def SM3SS1 : SInst<"vsm3ss1", "....", "QUi">;
def SM3TT1A : SInst<"vsm3tt1a", "....I", "QUi">;
def SM3TT1B : SInst<"vsm3tt1b", "....I", "QUi">;
def SM3TT2A : SInst<"vsm3tt2a", "....I", "QUi">;
def SM3TT2B : SInst<"vsm3tt2b", "....I", "QUi">;
def SM3PARTW1 : SInst<"vsm3partw1", "....", "QUi">;
def SM3PARTW2 : SInst<"vsm3partw2", "....", "QUi">;
}

let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "sm4,neon" in {
def SM4E : SInst<"vsm4e", "...", "QUi">;
def SM4EKEY : SInst<"vsm4ekey", "...", "QUi">;
}

////////////////////////////////////////////////////////////////////////////////
// poly128_t vadd for AArch64 only see VADDP for the rest
def VADDP_Q   : WInst<"vadd", "...", "QPk">;

////////////////////////////////////////////////////////////////////////////////
// Float -> Int conversions with explicit rounding mode

let ArchGuard = "__ARM_ARCH >= 8" in {
def FCVTNS_S32 : SInst<"vcvtn_s32", "S.", "fQf">;
def FCVTNU_S32 : SInst<"vcvtn_u32", "U.", "fQf">;
def FCVTPS_S32 : SInst<"vcvtp_s32", "S.", "fQf">;
def FCVTPU_S32 : SInst<"vcvtp_u32", "U.", "fQf">;
def FCVTMS_S32 : SInst<"vcvtm_s32", "S.", "fQf">;
def FCVTMU_S32 : SInst<"vcvtm_u32", "U.", "fQf">;
def FCVTAS_S32 : SInst<"vcvta_s32", "S.", "fQf">;
def FCVTAU_S32 : SInst<"vcvta_u32", "U.", "fQf">;
}

let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)" in {
def FCVTNS_S64 : SInst<"vcvtn_s64", "S.", "dQd">;
def FCVTNU_S64 : SInst<"vcvtn_u64", "U.", "dQd">;
def FCVTPS_S64 : SInst<"vcvtp_s64", "S.", "dQd">;
def FCVTPU_S64 : SInst<"vcvtp_u64", "U.", "dQd">;
def FCVTMS_S64 : SInst<"vcvtm_s64", "S.", "dQd">;
def FCVTMU_S64 : SInst<"vcvtm_u64", "U.", "dQd">;
def FCVTAS_S64 : SInst<"vcvta_s64", "S.", "dQd">;
def FCVTAU_S64 : SInst<"vcvta_u64", "U.", "dQd">;
}

////////////////////////////////////////////////////////////////////////////////
// Round to Integral

let ArchGuard = "__ARM_ARCH >= 8 && defined(__ARM_FEATURE_DIRECTED_ROUNDING)" in {
def FRINTN_S32 : SInst<"vrndn", "..", "fQf">;
def FRINTA_S32 : SInst<"vrnda", "..", "fQf">;
def FRINTP_S32 : SInst<"vrndp", "..", "fQf">;
def FRINTM_S32 : SInst<"vrndm", "..", "fQf">;
def FRINTX_S32 : SInst<"vrndx", "..", "fQf">;
def FRINTZ_S32 : SInst<"vrnd", "..", "fQf">;
def FRINTI_S32 : SInst<"vrndi", "..", "fQf">;
}

let ArchGuard = "(defined(__aarch64__) || defined(__arm64ec__)) && defined(__ARM_FEATURE_DIRECTED_ROUNDING)" in {
def FRINTN_S64 : SInst<"vrndn", "..", "dQd">;
def FRINTA_S64 : SInst<"vrnda", "..", "dQd">;
def FRINTP_S64 : SInst<"vrndp", "..", "dQd">;
def FRINTM_S64 : SInst<"vrndm", "..", "dQd">;
def FRINTX_S64 : SInst<"vrndx", "..", "dQd">;
def FRINTZ_S64 : SInst<"vrnd", "..", "dQd">;
def FRINTI_S64 : SInst<"vrndi", "..", "dQd">;
}

let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "v8.5a,neon" in {
def FRINT32X_S32 : SInst<"vrnd32x", "..", "fQf">;
def FRINT32Z_S32 : SInst<"vrnd32z", "..", "fQf">;
def FRINT64X_S32 : SInst<"vrnd64x", "..", "fQf">;
def FRINT64Z_S32 : SInst<"vrnd64z", "..", "fQf">;

def FRINT32X_S64 : SInst<"vrnd32x", "..", "dQd">;
def FRINT32Z_S64 : SInst<"vrnd32z", "..", "dQd">;
def FRINT64X_S64 : SInst<"vrnd64x", "..", "dQd">;
def FRINT64Z_S64 : SInst<"vrnd64z", "..", "dQd">;
}

////////////////////////////////////////////////////////////////////////////////
// MaxNum/MinNum Floating Point

let ArchGuard = "__ARM_ARCH >= 8 && defined(__ARM_FEATURE_NUMERIC_MAXMIN)" in {
def FMAXNM_S32 : SInst<"vmaxnm", "...", "fQf">;
def FMINNM_S32 : SInst<"vminnm", "...", "fQf">;
}

let ArchGuard = "(defined(__aarch64__)  || defined(__arm64ec__)) && defined(__ARM_FEATURE_NUMERIC_MAXMIN)" in {
def FMAXNM_S64 : SInst<"vmaxnm", "...", "dQd">;
def FMINNM_S64 : SInst<"vminnm", "...", "dQd">;
}

////////////////////////////////////////////////////////////////////////////////
// Permutation
def VTRN1 : SOpInst<"vtrn1", "...",
                    "csiUcUsUifPcPsQcQsQiQlQUcQUsQUiQUlQfQdQPcQPsQPl", OP_TRN1>;
def VZIP1 : SOpInst<"vzip1", "...",
                    "csiUcUsUifPcPsQcQsQiQlQUcQUsQUiQUlQfQdQPcQPsQPl", OP_ZIP1>;
def VUZP1 : SOpInst<"vuzp1", "...",
                    "csiUcUsUifPcPsQcQsQiQlQUcQUsQUiQUlQfQdQPcQPsQPl", OP_UZP1>;
def VTRN2 : SOpInst<"vtrn2", "...",
                    "csiUcUsUifPcPsQcQsQiQlQUcQUsQUiQUlQfQdQPcQPsQPl", OP_TRN2>;
def VZIP2 : SOpInst<"vzip2", "...",
                    "csiUcUsUifPcPsQcQsQiQlQUcQUsQUiQUlQfQdQPcQPsQPl", OP_ZIP2>;
def VUZP2 : SOpInst<"vuzp2", "...",
                    "csiUcUsUifPcPsQcQsQiQlQUcQUsQUiQUlQfQdQPcQPsQPl", OP_UZP2>;

////////////////////////////////////////////////////////////////////////////////
// Table lookup
let InstName = "vtbl" in {
def VQTBL1_A64 : WInst<"vqtbl1", ".QU",  "UccPcQUcQcQPc">;
def VQTBL2_A64 : WInst<"vqtbl2", ".(2Q)U",  "UccPcQUcQcQPc">;
def VQTBL3_A64 : WInst<"vqtbl3", ".(3Q)U",  "UccPcQUcQcQPc">;
def VQTBL4_A64 : WInst<"vqtbl4", ".(4Q)U",  "UccPcQUcQcQPc">;
}
let InstName = "vtbx" in {
def VQTBX1_A64 : WInst<"vqtbx1", "..QU", "UccPcQUcQcQPc">;
def VQTBX2_A64 : WInst<"vqtbx2", "..(2Q)U", "UccPcQUcQcQPc">;
def VQTBX3_A64 : WInst<"vqtbx3", "..(3Q)U", "UccPcQUcQcQPc">;
def VQTBX4_A64 : WInst<"vqtbx4", "..(4Q)U", "UccPcQUcQcQPc">;
}

////////////////////////////////////////////////////////////////////////////////
// Vector reinterpret cast operations

// NeonEmitter implicitly takes the cartesian product of the type string with
// itself during generation so, unlike all other intrinsics, this one should
// include *all* types, not just additional ones.
def VVREINTERPRET : REINTERPRET_CROSS_SELF<"csilUcUsUiUlhfdPcPsPlQcQsQiQlQUcQUsQUiQUlQhQfQdQPcQPsQPlQPk"> {
  let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)";
  let BigEndianSafe = 1;
}

////////////////////////////////////////////////////////////////////////////////
// Scalar Intrinsics
// Scalar Arithmetic

// Scalar Addition
def SCALAR_ADD : SInst<"vadd", "111",  "SlSUl">;
// Scalar  Saturating Add
def SCALAR_QADD   : SInst<"vqadd", "111", "ScSsSiSlSUcSUsSUiSUl">;

// Scalar Subtraction
def SCALAR_SUB : SInst<"vsub", "111",  "SlSUl">;
// Scalar  Saturating Sub
def SCALAR_QSUB   : SInst<"vqsub", "111", "ScSsSiSlSUcSUsSUiSUl">;

let InstName = "vmov" in {
def VGET_HIGH_A64 : NoTestOpInst<"vget_high", ".Q", "dPl", OP_HI>;
def VGET_LOW_A64  : NoTestOpInst<"vget_low", ".Q", "dPl", OP_LO>;
}

////////////////////////////////////////////////////////////////////////////////
// Scalar Shift
// Scalar Shift Left
def SCALAR_SHL: SInst<"vshl", "11(S1)", "SlSUl">;
// Scalar Saturating Shift Left
def SCALAR_QSHL: SInst<"vqshl", "11(S1)", "ScSsSiSlSUcSUsSUiSUl">;
// Scalar Saturating Rounding Shift Left
def SCALAR_QRSHL: SInst<"vqrshl", "11(S1)", "ScSsSiSlSUcSUsSUiSUl">;
// Scalar Shift Rounding Left
def SCALAR_RSHL: SInst<"vrshl", "11(S1)", "SlSUl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Shift (Immediate)
let isScalarShift = 1 in {
// Signed/Unsigned Shift Right (Immediate)
def SCALAR_SSHR_N: SInst<"vshr_n", "11I", "SlSUl">;
// Signed/Unsigned Rounding Shift Right (Immediate)
def SCALAR_SRSHR_N: SInst<"vrshr_n", "11I", "SlSUl">;

// Signed/Unsigned Shift Right and Accumulate (Immediate)
def SCALAR_SSRA_N: SInst<"vsra_n", "111I", "SlSUl">;
// Signed/Unsigned Rounding Shift Right and Accumulate (Immediate)
def SCALAR_SRSRA_N: SInst<"vrsra_n", "111I", "SlSUl">;

// Shift Left (Immediate)
def SCALAR_SHL_N: SInst<"vshl_n", "11I", "SlSUl">;
// Signed/Unsigned Saturating Shift Left (Immediate)
def SCALAR_SQSHL_N: SInst<"vqshl_n", "11I", "ScSsSiSlSUcSUsSUiSUl">;
// Signed Saturating Shift Left Unsigned (Immediate)
def SCALAR_SQSHLU_N: SInst<"vqshlu_n", "11I", "ScSsSiSl">;

// Shift Right And Insert (Immediate)
def SCALAR_SRI_N: SInst<"vsri_n", "111I", "SlSUl">;
// Shift Left And Insert (Immediate)
def SCALAR_SLI_N: SInst<"vsli_n", "111I", "SlSUl">;

let isScalarNarrowShift = 1 in {
  // Signed/Unsigned Saturating Shift Right Narrow (Immediate)
  def SCALAR_SQSHRN_N: SInst<"vqshrn_n", "(1<)1I", "SsSiSlSUsSUiSUl">;
  // Signed/Unsigned Saturating Rounded Shift Right Narrow (Immediate)
  def SCALAR_SQRSHRN_N: SInst<"vqrshrn_n", "(1<)1I", "SsSiSlSUsSUiSUl">;
  // Signed Saturating Shift Right Unsigned Narrow (Immediate)
  def SCALAR_SQSHRUN_N: SInst<"vqshrun_n", "(1<U)1I", "SsSiSl">;
  // Signed Saturating Rounded Shift Right Unsigned Narrow (Immediate)
  def SCALAR_SQRSHRUN_N: SInst<"vqrshrun_n", "(1<U)1I", "SsSiSl">;
}

////////////////////////////////////////////////////////////////////////////////
// Scalar Signed/Unsigned Fixed-point Convert To Floating-Point (Immediate)
def SCALAR_SCVTF_N_F32: SInst<"vcvt_n_f32", "(1F)(1!)I", "SiSUi">;
def SCALAR_SCVTF_N_F64: SInst<"vcvt_n_f64", "(1F)(1!)I", "SlSUl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Floating-point Convert To Signed/Unsigned Fixed-point (Immediate)
def SCALAR_FCVTZS_N_S32 : SInst<"vcvt_n_s32", "(1S)1I", "Sf">;
def SCALAR_FCVTZU_N_U32 : SInst<"vcvt_n_u32", "(1U)1I", "Sf">;
def SCALAR_FCVTZS_N_S64 : SInst<"vcvt_n_s64", "(1S)1I", "Sd">;
def SCALAR_FCVTZU_N_U64 : SInst<"vcvt_n_u64", "(1U)1I", "Sd">;
}

////////////////////////////////////////////////////////////////////////////////
// Scalar Floating-point Round to Integral
let ArchGuard = "__ARM_ARCH >= 8 && defined(__ARM_FEATURE_DIRECTED_ROUNDING)" in {
def SCALAR_FRINTN_S32 : SInst<"vrndn", "11", "Sf">;
}

////////////////////////////////////////////////////////////////////////////////
// Scalar Reduce Pairwise Addition (Scalar and Floating Point)
def SCALAR_ADDP  : SInst<"vpadd", "1.", "SfSHlSHdSHUl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Reduce Floating Point Pairwise Max/Min
def SCALAR_FMAXP : SInst<"vpmax", "1.", "SfSQd">;

def SCALAR_FMINP : SInst<"vpmin", "1.", "SfSQd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Reduce Floating Point Pairwise maxNum/minNum
def SCALAR_FMAXNMP : SInst<"vpmaxnm", "1.", "SfSQd">;
def SCALAR_FMINNMP : SInst<"vpminnm", "1.", "SfSQd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Integer Saturating Doubling Multiply Half High
def SCALAR_SQDMULH : SInst<"vqdmulh", "111", "SsSi">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Integer Saturating Rounding Doubling Multiply Half High
def SCALAR_SQRDMULH : SInst<"vqrdmulh", "111", "SsSi">;

let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "v8.1a,neon" in {
////////////////////////////////////////////////////////////////////////////////
// Signed Saturating Rounding Doubling Multiply Accumulate Returning High Half
def SCALAR_SQRDMLAH : SInst<"vqrdmlah", "1111", "SsSi">;

////////////////////////////////////////////////////////////////////////////////
// Signed Saturating Rounding Doubling Multiply Subtract Returning High Half
def SCALAR_SQRDMLSH : SInst<"vqrdmlsh", "1111", "SsSi">;
} // ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "v8.1a"

////////////////////////////////////////////////////////////////////////////////
// Scalar Floating-point Multiply Extended
def SCALAR_FMULX : IInst<"vmulx", "111", "SfSd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Floating-point Reciprocal Step
def SCALAR_FRECPS : IInst<"vrecps", "111", "SfSd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Floating-point Reciprocal Square Root Step
def SCALAR_FRSQRTS : IInst<"vrsqrts", "111", "SfSd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Signed Integer Convert To Floating-point
def SCALAR_SCVTFS : SInst<"vcvt_f32", "(1F)(1!)", "Si">;
def SCALAR_SCVTFD : SInst<"vcvt_f64", "(1F)(1!)", "Sl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Unsigned Integer Convert To Floating-point
def SCALAR_UCVTFS : SInst<"vcvt_f32", "(1F)(1!)", "SUi">;
def SCALAR_UCVTFD : SInst<"vcvt_f64", "(1F)(1!)", "SUl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Floating-point Converts
def SCALAR_FCVTXN  : IInst<"vcvtx_f32", "(1F<)(1!)", "Sd">;
def SCALAR_FCVTNSS : SInst<"vcvtn_s32", "(1S)1", "Sf">;
def SCALAR_FCVTNUS : SInst<"vcvtn_u32", "(1U)1", "Sf">;
def SCALAR_FCVTNSD : SInst<"vcvtn_s64", "(1S)1", "Sd">;
def SCALAR_FCVTNUD : SInst<"vcvtn_u64", "(1U)1", "Sd">;
def SCALAR_FCVTMSS : SInst<"vcvtm_s32", "(1S)1", "Sf">;
def SCALAR_FCVTMUS : SInst<"vcvtm_u32", "(1U)1", "Sf">;
def SCALAR_FCVTMSD : SInst<"vcvtm_s64", "(1S)1", "Sd">;
def SCALAR_FCVTMUD : SInst<"vcvtm_u64", "(1U)1", "Sd">;
def SCALAR_FCVTASS : SInst<"vcvta_s32", "(1S)1", "Sf">;
def SCALAR_FCVTAUS : SInst<"vcvta_u32", "(1U)1", "Sf">;
def SCALAR_FCVTASD : SInst<"vcvta_s64", "(1S)1", "Sd">;
def SCALAR_FCVTAUD : SInst<"vcvta_u64", "(1U)1", "Sd">;
def SCALAR_FCVTPSS : SInst<"vcvtp_s32", "(1S)1", "Sf">;
def SCALAR_FCVTPUS : SInst<"vcvtp_u32", "(1U)1", "Sf">;
def SCALAR_FCVTPSD : SInst<"vcvtp_s64", "(1S)1", "Sd">;
def SCALAR_FCVTPUD : SInst<"vcvtp_u64", "(1U)1", "Sd">;
def SCALAR_FCVTZSS : SInst<"vcvt_s32", "(1S)1", "Sf">;
def SCALAR_FCVTZUS : SInst<"vcvt_u32", "(1U)1", "Sf">;
def SCALAR_FCVTZSD : SInst<"vcvt_s64", "(1S)1", "Sd">;
def SCALAR_FCVTZUD : SInst<"vcvt_u64", "(1U)1", "Sd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Floating-point Reciprocal Estimate
def SCALAR_FRECPE : IInst<"vrecpe", "11", "SfSd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Floating-point Reciprocal Exponent
def SCALAR_FRECPX : IInst<"vrecpx", "11", "SfSd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Floating-point Reciprocal Square Root Estimate
def SCALAR_FRSQRTE : IInst<"vrsqrte", "11", "SfSd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Integer Comparison
def SCALAR_CMEQ : SInst<"vceq", "(U1)11", "SlSUl">;
def SCALAR_CMEQZ : SInst<"vceqz", "(U1)1", "SlSUl">;
def SCALAR_CMGE : SInst<"vcge", "(U1)11", "Sl">;
def SCALAR_CMGEZ : SInst<"vcgez", "(U1)1", "Sl">;
def SCALAR_CMHS : SInst<"vcge", "(U1)11", "SUl">;
def SCALAR_CMLE : SInst<"vcle", "(U1)11", "SlSUl">;
def SCALAR_CMLEZ : SInst<"vclez", "(U1)1", "Sl">;
def SCALAR_CMLT : SInst<"vclt", "(U1)11", "SlSUl">;
def SCALAR_CMLTZ : SInst<"vcltz", "(U1)1", "Sl">;
def SCALAR_CMGT : SInst<"vcgt", "(U1)11", "Sl">;
def SCALAR_CMGTZ : SInst<"vcgtz", "(U1)1", "Sl">;
def SCALAR_CMHI : SInst<"vcgt", "(U1)11", "SUl">;
def SCALAR_CMTST : SInst<"vtst", "(U1)11", "SlSUl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Floating-point Comparison
def SCALAR_FCMEQ : IInst<"vceq", "(1U)11", "SfSd">;
def SCALAR_FCMEQZ : IInst<"vceqz", "(1U)1", "SfSd">;
def SCALAR_FCMGE : IInst<"vcge", "(1U)11", "SfSd">;
def SCALAR_FCMGEZ : IInst<"vcgez", "(1U)1", "SfSd">;
def SCALAR_FCMGT : IInst<"vcgt", "(1U)11", "SfSd">;
def SCALAR_FCMGTZ : IInst<"vcgtz", "(1U)1", "SfSd">;
def SCALAR_FCMLE : IInst<"vcle", "(1U)11", "SfSd">;
def SCALAR_FCMLEZ : IInst<"vclez", "(1U)1", "SfSd">;
def SCALAR_FCMLT : IInst<"vclt", "(1U)11", "SfSd">;
def SCALAR_FCMLTZ : IInst<"vcltz", "(1U)1", "SfSd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Floating-point Absolute Compare Mask Greater Than Or Equal
def SCALAR_FACGE : IInst<"vcage", "(1U)11", "SfSd">;
def SCALAR_FACLE : IInst<"vcale", "(1U)11", "SfSd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Floating-point Absolute Compare Mask Greater Than
def SCALAR_FACGT : IInst<"vcagt", "(1U)11", "SfSd">;
def SCALAR_FACLT : IInst<"vcalt", "(1U)11", "SfSd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Absolute Value
def SCALAR_ABS : SInst<"vabs", "11", "Sl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Absolute Difference
def SCALAR_ABD : IInst<"vabd", "111", "SfSd">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Signed Saturating Absolute Value
def SCALAR_SQABS : SInst<"vqabs", "11", "ScSsSiSl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Negate
def SCALAR_NEG : SInst<"vneg", "11", "Sl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Signed Saturating Negate
def SCALAR_SQNEG : SInst<"vqneg", "11", "ScSsSiSl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Signed Saturating Accumulated of Unsigned Value
def SCALAR_SUQADD : SInst<"vuqadd", "11(1U)", "ScSsSiSl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Unsigned Saturating Accumulated of Signed Value
def SCALAR_USQADD : SInst<"vsqadd", "11(1S)", "SUcSUsSUiSUl">;

////////////////////////////////////////////////////////////////////////////////
// Signed Saturating Doubling Multiply-Add Long
def SCALAR_SQDMLAL : SInst<"vqdmlal", "(1>)(1>)11", "SsSi">;

////////////////////////////////////////////////////////////////////////////////
// Signed Saturating Doubling Multiply-Subtract Long
def SCALAR_SQDMLSL : SInst<"vqdmlsl", "(1>)(1>)11", "SsSi">;

////////////////////////////////////////////////////////////////////////////////
// Signed Saturating Doubling Multiply Long
def SCALAR_SQDMULL : SInst<"vqdmull", "(1>)11", "SsSi">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Signed Saturating Extract Unsigned Narrow
def SCALAR_SQXTUN : SInst<"vqmovun", "(U1<)1", "SsSiSl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Signed Saturating Extract Narrow
def SCALAR_SQXTN : SInst<"vqmovn", "(1<)1", "SsSiSl">;

////////////////////////////////////////////////////////////////////////////////
// Scalar Unsigned Saturating Extract Narrow
def SCALAR_UQXTN : SInst<"vqmovn", "(1<)1", "SUsSUiSUl">;

// Scalar Floating Point  multiply (scalar, by element)
def SCALAR_FMUL_LANE : IOpInst<"vmul_lane", "11.I", "SfSd", OP_SCALAR_MUL_LN>;
def SCALAR_FMUL_LANEQ : IOpInst<"vmul_laneq", "11QI", "SfSd", OP_SCALAR_MUL_LN> {
  let isLaneQ = 1;
}

// Scalar Floating Point  multiply extended (scalar, by element)
def SCALAR_FMULX_LANE : IOpInst<"vmulx_lane", "11.I", "SfSd", OP_SCALAR_MULX_LN>;
def SCALAR_FMULX_LANEQ : IOpInst<"vmulx_laneq", "11QI", "SfSd", OP_SCALAR_MULX_LN> {
  let isLaneQ = 1;
}

def SCALAR_VMUL_N : IInst<"vmul_n", "..1", "d">;

// VMUL_LANE_A64 d type implemented using scalar mul lane
def SCALAR_VMUL_LANE : IInst<"vmul_lane", "..qI", "d">;

// VMUL_LANEQ d type implemented using scalar mul lane
def SCALAR_VMUL_LANEQ   : IInst<"vmul_laneq", "..QI", "d"> {
  let isLaneQ = 1;
}

// VMULX_LANE d type implemented using scalar vmulx_lane
def SCALAR_VMULX_LANE : IOpInst<"vmulx_lane", "..qI", "d", OP_SCALAR_VMULX_LN>;

// VMULX_LANEQ d type implemented using scalar vmulx_laneq
def SCALAR_VMULX_LANEQ : IOpInst<"vmulx_laneq", "..QI", "d", OP_SCALAR_VMULX_LNQ> {
  let isLaneQ = 1;
}

// Scalar Floating Point fused multiply-add (scalar, by element)
def SCALAR_FMLA_LANE : IInst<"vfma_lane", "111.I", "SfSd">;
def SCALAR_FMLA_LANEQ : IInst<"vfma_laneq", "111QI", "SfSd"> {
  let isLaneQ = 1;
}

// Scalar Floating Point fused multiply-subtract (scalar, by element)
def SCALAR_FMLS_LANE : IOpInst<"vfms_lane", "111.I", "SfSd", OP_FMS_LN>;
def SCALAR_FMLS_LANEQ : IOpInst<"vfms_laneq", "111QI", "SfSd", OP_FMS_LNQ> {
  let isLaneQ = 1;
}

// Signed Saturating Doubling Multiply Long (scalar by element)
def SCALAR_SQDMULL_LANE : SOpInst<"vqdmull_lane", "(1>)1.I", "SsSi", OP_SCALAR_QDMULL_LN>;
def SCALAR_SQDMULL_LANEQ : SOpInst<"vqdmull_laneq", "(1>)1QI", "SsSi", OP_SCALAR_QDMULL_LN> {
  let isLaneQ = 1;
}

// Signed Saturating Doubling Multiply-Add Long (scalar by element)
def SCALAR_SQDMLAL_LANE : SInst<"vqdmlal_lane", "(1>)(1>)1.I", "SsSi">;
def SCALAR_SQDMLAL_LANEQ : SInst<"vqdmlal_laneq", "(1>)(1>)1QI", "SsSi"> {
  let isLaneQ = 1;
}

// Signed Saturating Doubling Multiply-Subtract Long (scalar by element)
def SCALAR_SQDMLS_LANE : SInst<"vqdmlsl_lane", "(1>)(1>)1.I", "SsSi">;
def SCALAR_SQDMLS_LANEQ : SInst<"vqdmlsl_laneq", "(1>)(1>)1QI", "SsSi"> {
  let isLaneQ = 1;
}

// Scalar Integer Saturating Doubling Multiply Half High (scalar by element)
def SCALAR_SQDMULH_LANE : SOpInst<"vqdmulh_lane", "11.I", "SsSi", OP_SCALAR_QDMULH_LN>;
def SCALAR_SQDMULH_LANEQ : SOpInst<"vqdmulh_laneq", "11QI", "SsSi", OP_SCALAR_QDMULH_LN> {
  let isLaneQ = 1;
}

// Scalar Integer Saturating Rounding Doubling Multiply Half High
def SCALAR_SQRDMULH_LANE : SOpInst<"vqrdmulh_lane", "11.I", "SsSi", OP_SCALAR_QRDMULH_LN>;
def SCALAR_SQRDMULH_LANEQ : SOpInst<"vqrdmulh_laneq", "11QI", "SsSi", OP_SCALAR_QRDMULH_LN> {
  let isLaneQ = 1;
}

let TargetGuard = "v8.1a,neon" in {
// Signed Saturating Rounding Doubling Multiply Accumulate Returning High Half
def SCALAR_SQRDMLAH_LANE : SOpInst<"vqrdmlah_lane", "111.I", "SsSi", OP_SCALAR_QRDMLAH_LN>;
def SCALAR_SQRDMLAH_LANEQ : SOpInst<"vqrdmlah_laneq", "111QI", "SsSi", OP_SCALAR_QRDMLAH_LN> {
  let isLaneQ = 1;
}

// Signed Saturating Rounding Doubling Multiply Subtract Returning High Half
def SCALAR_SQRDMLSH_LANE : SOpInst<"vqrdmlsh_lane", "111.I", "SsSi", OP_SCALAR_QRDMLSH_LN>;
def SCALAR_SQRDMLSH_LANEQ : SOpInst<"vqrdmlsh_laneq", "111QI", "SsSi", OP_SCALAR_QRDMLSH_LN> {
  let isLaneQ = 1;
}
} // TargetGuard = "v8.1a"

def SCALAR_VDUP_LANE : IInst<"vdup_lane", "1.I", "ScSsSiSlSfSdSUcSUsSUiSUlSPcSPs">;
def SCALAR_VDUP_LANEQ : IInst<"vdup_laneq", "1QI", "ScSsSiSlSfSdSUcSUsSUiSUlSPcSPs"> {
  let isLaneQ = 1;
}

} // ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)"

// ARMv8.2-A FP16 vector intrinsics for A32/A64.
let TargetGuard = "fullfp16,neon" in {

  // ARMv8.2-A FP16 one-operand vector intrinsics.

  // Comparison
  def CMEQH    : SInst<"vceqz", "U.", "hQh">;
  def CMGEH    : SInst<"vcgez", "U.", "hQh">;
  def CMGTH    : SInst<"vcgtz", "U.", "hQh">;
  def CMLEH    : SInst<"vclez", "U.", "hQh">;
  def CMLTH    : SInst<"vcltz", "U.", "hQh">;

  // Vector conversion
  def VCVT_F16     : SInst<"vcvt_f16", "F(.!)",  "sUsQsQUs">;
  def VCVT_S16     : SInst<"vcvt_s16", "S.",  "hQh">;
  def VCVT_U16     : SInst<"vcvt_u16", "U.",  "hQh">;
  def VCVTA_S16    : SInst<"vcvta_s16", "S.", "hQh">;
  def VCVTA_U16    : SInst<"vcvta_u16", "U.", "hQh">;
  def VCVTM_S16    : SInst<"vcvtm_s16", "S.", "hQh">;
  def VCVTM_U16    : SInst<"vcvtm_u16", "U.", "hQh">;
  def VCVTN_S16    : SInst<"vcvtn_s16", "S.", "hQh">;
  def VCVTN_U16    : SInst<"vcvtn_u16", "U.", "hQh">;
  def VCVTP_S16    : SInst<"vcvtp_s16", "S.", "hQh">;
  def VCVTP_U16    : SInst<"vcvtp_u16", "U.", "hQh">;

  // Vector rounding
  let ArchGuard = "__ARM_ARCH >= 8 && defined(__ARM_FEATURE_DIRECTED_ROUNDING)", TargetGuard = "fullfp16,neon" in {
    def FRINTZH      : SInst<"vrnd",  "..", "hQh">;
    def FRINTNH      : SInst<"vrndn", "..", "hQh">;
    def FRINTAH      : SInst<"vrnda", "..", "hQh">;
    def FRINTPH      : SInst<"vrndp", "..", "hQh">;
    def FRINTMH      : SInst<"vrndm", "..", "hQh">;
    def FRINTXH      : SInst<"vrndx", "..", "hQh">;
  }

  // Misc.
  def VABSH        : SInst<"vabs", "..", "hQh">;
  def VNEGH        : SOpInst<"vneg", "..", "hQh", OP_NEG>;
  def VRECPEH      : SInst<"vrecpe", "..", "hQh">;
  def FRSQRTEH     : SInst<"vrsqrte", "..", "hQh">;

  // ARMv8.2-A FP16 two-operands vector intrinsics.

  // Misc.
  def VADDH        : SOpInst<"vadd", "...", "hQh", OP_ADD>;
  def VABDH        : SInst<"vabd", "...",  "hQh">;
  def VSUBH         : SOpInst<"vsub", "...", "hQh", OP_SUB>;

  // Comparison
  let InstName = "vacge" in {
    def VCAGEH     : SInst<"vcage", "U..", "hQh">;
    def VCALEH     : SInst<"vcale", "U..", "hQh">;
  }
  let InstName = "vacgt" in {
    def VCAGTH     : SInst<"vcagt", "U..", "hQh">;
    def VCALTH     : SInst<"vcalt", "U..", "hQh">;
  }
  def VCEQH        : SOpInst<"vceq", "U..", "hQh", OP_EQ>;
  def VCGEH        : SOpInst<"vcge", "U..", "hQh", OP_GE>;
  def VCGTH        : SOpInst<"vcgt", "U..", "hQh", OP_GT>;
  let InstName = "vcge" in
    def VCLEH      : SOpInst<"vcle", "U..", "hQh", OP_LE>;
  let InstName = "vcgt" in
    def VCLTH      : SOpInst<"vclt", "U..", "hQh", OP_LT>;

  // Vector conversion
  let isVCVT_N = 1 in {
    def VCVT_N_F16 : SInst<"vcvt_n_f16", "F(.!)I", "sUsQsQUs">;
    def VCVT_N_S16 : SInst<"vcvt_n_s16", "S.I", "hQh">;
    def VCVT_N_U16 : SInst<"vcvt_n_u16", "U.I", "hQh">;
  }

  // Max/Min
  def VMAXH         : SInst<"vmax", "...", "hQh">;
  def VMINH         : SInst<"vmin", "...", "hQh">;
  let ArchGuard = "__ARM_ARCH >= 8 && defined(__ARM_FEATURE_NUMERIC_MAXMIN)", TargetGuard = "fullfp16,neon" in {
    def FMAXNMH       : SInst<"vmaxnm", "...", "hQh">;
    def FMINNMH       : SInst<"vminnm", "...", "hQh">;
  }

  // Multiplication/Division
  def VMULH         : SOpInst<"vmul", "...", "hQh", OP_MUL>;

  // Pairwise addition
  def VPADDH        : SInst<"vpadd", "...", "h">;

  // Pairwise Max/Min
  def VPMAXH        : SInst<"vpmax", "...", "h">;
  def VPMINH        : SInst<"vpmin", "...", "h">;

  // Reciprocal/Sqrt
  def VRECPSH       : SInst<"vrecps", "...", "hQh">;
  def VRSQRTSH      : SInst<"vrsqrts", "...", "hQh">;

  // ARMv8.2-A FP16 three-operands vector intrinsics.

  // Vector fused multiply-add operations
  def VFMAH        : SInst<"vfma", "....", "hQh">;
  def VFMSH        : SOpInst<"vfms", "....", "hQh", OP_FMLS>;

  // ARMv8.2-A FP16 lane vector intrinsics.

  // Mul lane
  def VMUL_LANEH    : IOpInst<"vmul_lane", "..qI", "hQh", OP_MUL_LN>;
  def VMUL_NH       : IOpInst<"vmul_n", "..1", "hQh", OP_MUL_N>;
}

// Data processing intrinsics - section 5. Do not require fullfp16.

// Logical operations
let isHiddenLInst = 1 in
def VBSLH    : SInst<"vbsl", ".U..", "hQh">;
// Transposition operations
def VZIPH    : WInst<"vzip", "2..", "hQh">;
def VUZPH    : WInst<"vuzp", "2..", "hQh">;
def VTRNH    : WInst<"vtrn", "2..", "hQh">;
// Vector Extract
def VEXTH      : WInst<"vext", "...I", "hQh">;
// Reverse vector elements
def VREV64H    : WOpInst<"vrev64", "..", "hQh", OP_REV64>;

// ARMv8.2-A FP16 vector intrinsics for A64 only.
let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "fullfp16,neon" in {

  // Vector rounding
  def FRINTIH      : SInst<"vrndi", "..", "hQh">;

  // Misc.
  def FSQRTH       : SInst<"vsqrt", "..", "hQh">;

  // Multiplication/Division
  def MULXH         : SInst<"vmulx", "...", "hQh">;
  def FDIVH         : IOpInst<"vdiv", "...",  "hQh", OP_DIV>;

  // Pairwise addition
  def VPADDH1       : SInst<"vpadd", "...", "Qh">;

  // Pairwise Max/Min
  def VPMAXH1       : SInst<"vpmax", "...", "Qh">;
  def VPMINH1       : SInst<"vpmin", "...", "Qh">;

  // Pairwise MaxNum/MinNum
  def FMAXNMPH      : SInst<"vpmaxnm", "...", "hQh">;
  def FMINNMPH      : SInst<"vpminnm", "...", "hQh">;

  // ARMv8.2-A FP16 lane vector intrinsics.

  // FMA lane
  def VFMA_LANEH   : IInst<"vfma_lane", "...qI", "hQh">;
  def VFMA_LANEQH  : IInst<"vfma_laneq", "...QI", "hQh"> {
    let isLaneQ = 1;
  }

  // FMA lane with scalar argument
  def FMLA_NH      : SOpInst<"vfma_n", "...1", "hQh", OP_FMLA_N>;
  // Scalar floating point fused multiply-add (scalar, by element)
  def SCALAR_FMLA_LANEH  : IInst<"vfma_lane", "111.I", "Sh">;
  def SCALAR_FMLA_LANEQH : IInst<"vfma_laneq", "111QI", "Sh"> {
    let isLaneQ = 1;
  }

  // FMS lane
  def VFMS_LANEH   : IOpInst<"vfms_lane", "...qI", "hQh", OP_FMS_LN>;
  def VFMS_LANEQH  : IOpInst<"vfms_laneq", "...QI", "hQh", OP_FMS_LNQ> {
    let isLaneQ = 1;
  }
  // FMS lane with scalar argument
  def FMLS_NH      : SOpInst<"vfms_n", "...1", "hQh", OP_FMLS_N>;
  // Scalar floating foint fused multiply-subtract (scalar, by element)
  def SCALAR_FMLS_LANEH  : IOpInst<"vfms_lane", "111.I", "Sh", OP_FMS_LN>;
  def SCALAR_FMLS_LANEQH : IOpInst<"vfms_laneq", "111QI", "Sh", OP_FMS_LNQ> {
    let isLaneQ = 1;
  }

  // Mul lane
  def VMUL_LANEQH   : IOpInst<"vmul_laneq", "..QI", "hQh", OP_MUL_LN> {
    let isLaneQ = 1;
  }
  // Scalar floating point  multiply (scalar, by element)
  def SCALAR_FMUL_LANEH  : IOpInst<"vmul_lane", "11.I", "Sh", OP_SCALAR_MUL_LN>;
  def SCALAR_FMUL_LANEQH : IOpInst<"vmul_laneq", "11QI", "Sh", OP_SCALAR_MUL_LN> {
    let isLaneQ = 1;
  }

  // Mulx lane
  def VMULX_LANEH   : IOpInst<"vmulx_lane", "..qI", "hQh", OP_MULX_LN>;
  def VMULX_LANEQH  : IOpInst<"vmulx_laneq", "..QI", "hQh", OP_MULX_LN> {
    let isLaneQ = 1;
  }
  def VMULX_NH      : IOpInst<"vmulx_n", "..1", "hQh", OP_MULX_N>;
  // Scalar floating point  mulx (scalar, by element)
  def SCALAR_FMULX_LANEH : IInst<"vmulx_lane", "11.I", "Sh">;
  def SCALAR_FMULX_LANEQH : IInst<"vmulx_laneq", "11QI", "Sh"> {
    let isLaneQ = 1;
  }

  // ARMv8.2-A FP16 reduction vector intrinsics.
  def VMAXVH   : SInst<"vmaxv", "1.", "hQh">;
  def VMINVH   : SInst<"vminv", "1.", "hQh">;
  def FMAXNMVH : SInst<"vmaxnmv", "1.", "hQh">;
  def FMINNMVH : SInst<"vminnmv", "1.", "hQh">;
}

let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)" in {
  // Permutation
  def VTRN1H     : SOpInst<"vtrn1", "...", "hQh", OP_TRN1>;
  def VZIP1H     : SOpInst<"vzip1", "...", "hQh", OP_ZIP1>;
  def VUZP1H     : SOpInst<"vuzp1", "...", "hQh", OP_UZP1>;
  def VTRN2H     : SOpInst<"vtrn2", "...", "hQh", OP_TRN2>;
  def VZIP2H     : SOpInst<"vzip2", "...", "hQh", OP_ZIP2>;
  def VUZP2H     : SOpInst<"vuzp2", "...", "hQh", OP_UZP2>;

  def SCALAR_VDUP_LANEH  : IInst<"vdup_lane", "1.I", "Sh">;
  def SCALAR_VDUP_LANEQH : IInst<"vdup_laneq", "1QI", "Sh"> {
    let isLaneQ = 1;
  }
}

// v8.2-A dot product instructions.
let TargetGuard = "dotprod,neon" in {
  def DOT : SInst<"vdot", "..(<<)(<<)", "iQiUiQUi">;
  def DOT_LANE : SOpInst<"vdot_lane", "..(<<)(<<q)I", "iUiQiQUi", OP_DOT_LN>;
}
let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "dotprod,neon" in {
  // Variants indexing into a 128-bit vector are A64 only.
  def UDOT_LANEQ : SOpInst<"vdot_laneq", "..(<<)(<<Q)I", "iUiQiQUi", OP_DOT_LNQ> {
    let isLaneQ = 1;
  }
}

// v8.2-A FP16 fused multiply-add long instructions.
let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "fp16fml,neon" in {
  def VFMLAL_LOW  : SInst<"vfmlal_low",  ">>..", "hQh">;
  def VFMLSL_LOW  : SInst<"vfmlsl_low",  ">>..", "hQh">;
  def VFMLAL_HIGH : SInst<"vfmlal_high", ">>..", "hQh">;
  def VFMLSL_HIGH : SInst<"vfmlsl_high", ">>..", "hQh">;

  def VFMLAL_LANE_LOW  : SOpInst<"vfmlal_lane_low",  "(F>)(F>)F(Fq)I", "hQh", OP_FMLAL_LN>;
  def VFMLSL_LANE_LOW  : SOpInst<"vfmlsl_lane_low",  "(F>)(F>)F(Fq)I", "hQh", OP_FMLSL_LN>;
  def VFMLAL_LANE_HIGH : SOpInst<"vfmlal_lane_high", "(F>)(F>)F(Fq)I", "hQh", OP_FMLAL_LN_Hi>;
  def VFMLSL_LANE_HIGH : SOpInst<"vfmlsl_lane_high", "(F>)(F>)F(Fq)I", "hQh", OP_FMLSL_LN_Hi>;

  def VFMLAL_LANEQ_LOW  : SOpInst<"vfmlal_laneq_low",  "(F>)(F>)F(FQ)I", "hQh", OP_FMLAL_LN> {
    let isLaneQ = 1;
  }
  def VFMLSL_LANEQ_LOW  : SOpInst<"vfmlsl_laneq_low",  "(F>)(F>)F(FQ)I", "hQh", OP_FMLSL_LN> {
    let isLaneQ = 1;
  }
  def VFMLAL_LANEQ_HIGH : SOpInst<"vfmlal_laneq_high", "(F>)(F>)F(FQ)I", "hQh", OP_FMLAL_LN_Hi> {
    let isLaneQ = 1;
  }
  def VFMLSL_LANEQ_HIGH : SOpInst<"vfmlsl_laneq_high", "(F>)(F>)F(FQ)I", "hQh", OP_FMLSL_LN_Hi> {
    let isLaneQ = 1;
  }
}

let TargetGuard = "i8mm,neon" in {
  def VMMLA   : SInst<"vmmla", "..(<<)(<<)", "QUiQi">;
  def VUSMMLA : SInst<"vusmmla", "..(<<U)(<<)", "Qi">;

  def VUSDOT  : SInst<"vusdot", "..(<<U)(<<)", "iQi">;

  def VUSDOT_LANE  : SOpInst<"vusdot_lane", "..(<<U)(<<q)I", "iQi", OP_USDOT_LN>;
  def VSUDOT_LANE  : SOpInst<"vsudot_lane", "..(<<)(<<qU)I", "iQi", OP_SUDOT_LN>;

  let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)" in {
    let isLaneQ = 1 in {
      def VUSDOT_LANEQ  : SOpInst<"vusdot_laneq", "..(<<U)(<<Q)I", "iQi", OP_USDOT_LNQ>;
      def VSUDOT_LANEQ  : SOpInst<"vsudot_laneq", "..(<<)(<<QU)I", "iQi", OP_SUDOT_LNQ>;
    }
  }
}

let TargetGuard = "bf16,neon" in {
  def VDOT_BF : SInst<"vbfdot", "..BB", "fQf">;
  def VDOT_LANE_BF : SOpInst<"vbfdot_lane", "..B(Bq)I", "fQf", OP_BFDOT_LN>;
  def VDOT_LANEQ_BF : SOpInst<"vbfdot_laneq", "..B(BQ)I", "fQf", OP_BFDOT_LNQ> {
    let isLaneQ = 1;
  }

  def VFMMLA_BF : SInst<"vbfmmla", "..BB", "Qf">;

  def VFMLALB_BF : SInst<"vbfmlalb", "..BB", "Qf">;
  def VFMLALT_BF : SInst<"vbfmlalt", "..BB", "Qf">;

  def VFMLALB_LANE_BF : SOpInst<"vbfmlalb_lane", "..B(Bq)I", "Qf", OP_BFMLALB_LN>;
  def VFMLALB_LANEQ_BF : SOpInst<"vbfmlalb_laneq", "..B(BQ)I", "Qf", OP_BFMLALB_LN>;

  def VFMLALT_LANE_BF : SOpInst<"vbfmlalt_lane", "..B(Bq)I", "Qf", OP_BFMLALT_LN>;
  def VFMLALT_LANEQ_BF : SOpInst<"vbfmlalt_laneq", "..B(BQ)I", "Qf", OP_BFMLALT_LN>;
}

multiclass VCMLA_ROTS<string type, string lanety, string laneqty> {
  foreach ROT = ["", "_rot90", "_rot180", "_rot270" ] in {
    def   : SInst<"vcmla" # ROT, "....", type # "Q" # type>;

    // vcmla{ROT}_lane
    def : SOpInst<"vcmla" # ROT # "_lane", "...qI", type, Op<(call "vcmla" # ROT, $p0, $p1,
           (bitcast $p0, (dup_typed lanety , (call "vget_lane", (bitcast lanety, $p2), $p3))))>>;

    // vcmlaq{ROT}_lane
    def : SOpInst<"vcmla" # ROT # "_lane", "...qI", "Q" # type, Op<(call "vcmla" # ROT, $p0, $p1,
           (bitcast $p0, (dup_typed laneqty , (call "vget_lane", (bitcast lanety, $p2), $p3))))>>;

    let isLaneQ = 1 in  {
      // vcmla{ROT}_laneq
      def : SOpInst<"vcmla" # ROT # "_laneq", "...QI", type,  Op<(call "vcmla" # ROT, $p0, $p1,
              (bitcast $p0, (dup_typed lanety, (call "vget_lane", (bitcast laneqty, $p2), $p3))))>>;

      // vcmlaq{ROT}_laneq
      def : SOpInst<"vcmla" # ROT # "_laneq", "...QI", "Q" # type, Op<(call "vcmla" # ROT, $p0, $p1,
             (bitcast $p0, (dup_typed laneqty , (call "vget_lane", (bitcast laneqty, $p2), $p3))))>>;
    }
  }
}

// v8.3-A Vector complex addition intrinsics
let TargetGuard = "v8.3a,fullfp16,neon" in {
  def VCADD_ROT90_FP16   : SInst<"vcadd_rot90", "...", "h">;
  def VCADD_ROT270_FP16  : SInst<"vcadd_rot270", "...", "h">;
  def VCADDQ_ROT90_FP16  : SInst<"vcaddq_rot90", "QQQ", "h">;
  def VCADDQ_ROT270_FP16 : SInst<"vcaddq_rot270", "QQQ", "h">;

  defm VCMLA_FP16  : VCMLA_ROTS<"h", "uint32x2_t", "uint32x4_t">;
}
let TargetGuard = "v8.3a,neon" in {
  def VCADD_ROT90   : SInst<"vcadd_rot90", "...", "f">;
  def VCADD_ROT270  : SInst<"vcadd_rot270", "...", "f">;
  def VCADDQ_ROT90  : SInst<"vcaddq_rot90", "QQQ", "f">;
  def VCADDQ_ROT270 : SInst<"vcaddq_rot270", "QQQ", "f">;

  defm VCMLA_F32        : VCMLA_ROTS<"f", "uint64x1_t", "uint64x2_t">;
}
let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "v8.3a,neon" in {
  def VCADDQ_ROT90_FP64  : SInst<"vcaddq_rot90", "QQQ", "d">;
  def VCADDQ_ROT270_FP64 : SInst<"vcaddq_rot270", "QQQ", "d">;

  defm VCMLA_FP64 : VCMLA_ROTS<"d", "uint64x2_t", "uint64x2_t">;
}

// V8.2-A BFloat intrinsics
let TargetGuard = "bf16,neon" in {
  def VCREATE_BF : NoTestOpInst<"vcreate", ".(IU>)", "b", OP_CAST> {
    let BigEndianSafe = 1;
  }

  def VDUP_N_BF    : WOpInst<"vdup_n", ".1", "bQb", OP_DUP>;

  def VDUP_LANE_BF : WOpInst<"vdup_lane", ".qI", "bQb", OP_DUP_LN>;
  def VDUP_LANEQ_BF: WOpInst<"vdup_laneq", ".QI", "bQb", OP_DUP_LN> {
    let isLaneQ = 1;
  }

  def VCOMBINE_BF  : NoTestOpInst<"vcombine", "Q..", "b", OP_CONC>;

  def VGET_HIGH_BF : NoTestOpInst<"vget_high", ".Q", "b", OP_HI>;
  def VGET_LOW_BF  : NoTestOpInst<"vget_low", ".Q", "b", OP_LO>;

  def VGET_LANE_BF : IInst<"vget_lane", "1.I", "bQb">;
  def VSET_LANE_BF : IInst<"vset_lane", ".1.I", "bQb">;
  def SCALAR_VDUP_LANE_BF : IInst<"vdup_lane", "1.I", "Sb">;
  def SCALAR_VDUP_LANEQ_BF : IInst<"vdup_laneq", "1QI", "Sb"> {
    let isLaneQ = 1;
  }

  def VLD1_BF : WInst<"vld1", ".(c*!)", "bQb">;
  def VLD2_BF : WInst<"vld2", "2(c*!)", "bQb">;
  def VLD3_BF : WInst<"vld3", "3(c*!)", "bQb">;
  def VLD4_BF : WInst<"vld4", "4(c*!)", "bQb">;

  def VST1_BF : WInst<"vst1", "v*(.!)", "bQb">;
  def VST2_BF : WInst<"vst2", "v*(2!)", "bQb">;
  def VST3_BF : WInst<"vst3", "v*(3!)", "bQb">;
  def VST4_BF : WInst<"vst4", "v*(4!)", "bQb">;

  def VLD1_X2_BF : WInst<"vld1_x2", "2(c*!)", "bQb">;
  def VLD1_X3_BF : WInst<"vld1_x3", "3(c*!)", "bQb">;
  def VLD1_X4_BF : WInst<"vld1_x4", "4(c*!)", "bQb">;

  def VST1_X2_BF : WInst<"vst1_x2", "v*(2!)", "bQb">;
  def VST1_X3_BF : WInst<"vst1_x3", "v*(3!)", "bQb">;
  def VST1_X4_BF : WInst<"vst1_x4", "v*(4!)", "bQb">;

  def VLD1_LANE_BF : WInst<"vld1_lane", ".(c*!).I", "bQb">;
  def VLD2_LANE_BF : WInst<"vld2_lane", "2(c*!)2I", "bQb">;
  def VLD3_LANE_BF : WInst<"vld3_lane", "3(c*!)3I", "bQb">;
  def VLD4_LANE_BF : WInst<"vld4_lane", "4(c*!)4I", "bQb">;
  def VST1_LANE_BF : WInst<"vst1_lane", "v*(.!)I", "bQb">;
  def VST2_LANE_BF : WInst<"vst2_lane", "v*(2!)I", "bQb">;
  def VST3_LANE_BF : WInst<"vst3_lane", "v*(3!)I", "bQb">;
  def VST4_LANE_BF : WInst<"vst4_lane", "v*(4!)I", "bQb">;

  def VLD1_DUP_BF : WInst<"vld1_dup", ".(c*!)", "bQb">;
  def VLD2_DUP_BF : WInst<"vld2_dup", "2(c*!)", "bQb">;
  def VLD3_DUP_BF : WInst<"vld3_dup", "3(c*!)", "bQb">;
  def VLD4_DUP_BF : WInst<"vld4_dup", "4(c*!)", "bQb">;

  def VCVT_F32_BF16 : SOpInst<"vcvt_f32_bf16", "(F>)(Bq!)",  "Qb", OP_VCVT_F32_BF16>;
  def VCVT_LOW_F32_BF16 : SOpInst<"vcvt_low_f32", "(F>)(BQ!)",  "Qb", OP_VCVT_F32_BF16_LO>;
  def VCVT_HIGH_F32_BF16 : SOpInst<"vcvt_high_f32", "(F>)(BQ!)", "Qb", OP_VCVT_F32_BF16_HI>;

  def SCALAR_CVT_BF16_F32 : SInst<"vcvth_bf16", "(1B)1", "f">;
  def SCALAR_CVT_F32_BF16 : SOpInst<"vcvtah_f32", "(1F>)(1!)", "b", OP_CVT_F32_BF16>;
}

let ArchGuard = "!defined(__aarch64__) && !defined(__arm64ec__)", TargetGuard = "bf16,neon" in {
  def VCVT_BF16_F32_A32_INTERNAL : WInst<"__a32_vcvt_bf16", "BQ", "f">;
  def VCVT_BF16_F32_A32 : SOpInst<"vcvt_bf16", "BQ", "f", OP_VCVT_BF16_F32_A32>;
  def VCVT_LOW_BF16_F32_A32 : SOpInst<"vcvt_low_bf16",  "BQ", "Qf", OP_VCVT_BF16_F32_LO_A32>;
  def VCVT_HIGH_BF16_F32_A32 : SOpInst<"vcvt_high_bf16", "BBQ", "Qf", OP_VCVT_BF16_F32_HI_A32>;
}

let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "bf16,neon" in {
  def VCVT_LOW_BF16_F32_A64_INTERNAL : WInst<"__a64_vcvtq_low_bf16", "BQ", "Hf">;
  def VCVT_LOW_BF16_F32_A64 : SOpInst<"vcvt_low_bf16", "BQ", "Qf", OP_VCVT_BF16_F32_LO_A64>;
  def VCVT_HIGH_BF16_F32_A64 : SInst<"vcvt_high_bf16", "BBQ", "Qf">;
  def VCVT_BF16_F32 : SOpInst<"vcvt_bf16",    "BQ", "f", OP_VCVT_BF16_F32_A64>;

  def COPY_LANE_BF16 : IOpInst<"vcopy_lane", "..I.I", "b", OP_COPY_LN>;
  def COPYQ_LANE_BF16 : IOpInst<"vcopy_lane", "..IqI", "Qb", OP_COPY_LN>;
  def COPY_LANEQ_BF16 : IOpInst<"vcopy_laneq", "..IQI", "b", OP_COPY_LN>;
  def COPYQ_LANEQ_BF16 : IOpInst<"vcopy_laneq", "..I.I", "Qb", OP_COPY_LN>;
}

let ArchGuard = "!defined(__aarch64__) && !defined(__arm64ec__)", TargetGuard = "bf16,neon" in {
  let BigEndianSafe = 1 in {
    defm VREINTERPRET_BF : REINTERPRET_CROSS_TYPES<
        "csilUcUsUiUlhfPcPsPlQcQsQiQlQUcQUsQUiQUlQhQfQPcQPsQPl", "bQb">;
  }
}

let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "bf16,neon" in {
  let BigEndianSafe = 1 in {
    defm VVREINTERPRET_BF : REINTERPRET_CROSS_TYPES<
        "csilUcUsUiUlhfdPcPsPlQcQsQiQlQUcQUsQUiQUlQhQfQdQPcQPsQPlQPk", "bQb">;
  }
}

// v8.9a/v9.4a LRCPC3 intrinsics
let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "rcpc3,neon" in {
  def VLDAP1_LANE : WInst<"vldap1_lane", ".(c*!).I", "QUlQlUlldQdPlQPl">;
  def VSTL1_LANE  : WInst<"vstl1_lane", "v*(.!)I", "QUlQlUlldQdPlQPl">;
}
