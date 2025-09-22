//===- IntrinsicsMips.td - Defines Mips intrinsics ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the MIPS-specific intrinsics.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MIPS DSP data types
def mips_v2q15_ty: LLVMType<v2i16>;
def mips_v4q7_ty: LLVMType<v4i8>;
def mips_q31_ty: LLVMType<i32>;

let TargetPrefix = "mips" in {  // All intrinsics start with "llvm.mips.".

//===----------------------------------------------------------------------===//
// MIPS DSP Rev 1

//===----------------------------------------------------------------------===//
// Addition/subtraction

def int_mips_addu_qb : ClangBuiltin<"__builtin_mips_addu_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_v4i8_ty],
            [Commutative, IntrNoMem]>;
def int_mips_addu_s_qb : ClangBuiltin<"__builtin_mips_addu_s_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_v4i8_ty],
            [Commutative, IntrNoMem]>;
def int_mips_subu_qb : ClangBuiltin<"__builtin_mips_subu_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_v4i8_ty], [IntrNoMem]>;
def int_mips_subu_s_qb : ClangBuiltin<"__builtin_mips_subu_s_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_v4i8_ty], [IntrNoMem]>;

def int_mips_addq_ph : ClangBuiltin<"__builtin_mips_addq_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, mips_v2q15_ty],
            [Commutative, IntrNoMem]>;
def int_mips_addq_s_ph : ClangBuiltin<"__builtin_mips_addq_s_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, mips_v2q15_ty],
            [Commutative, IntrNoMem]>;
def int_mips_subq_ph : ClangBuiltin<"__builtin_mips_subq_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, mips_v2q15_ty], [IntrNoMem]>;
def int_mips_subq_s_ph : ClangBuiltin<"__builtin_mips_subq_s_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, mips_v2q15_ty], [IntrNoMem]>;

def int_mips_madd: ClangBuiltin<"__builtin_mips_madd">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, Commutative]>;
def int_mips_maddu: ClangBuiltin<"__builtin_mips_maddu">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, Commutative]>;

def int_mips_msub: ClangBuiltin<"__builtin_mips_msub">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem]>;
def int_mips_msubu: ClangBuiltin<"__builtin_mips_msubu">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem]>;

def int_mips_addq_s_w: ClangBuiltin<"__builtin_mips_addq_s_w">,
  Intrinsic<[mips_q31_ty], [mips_q31_ty, mips_q31_ty], [Commutative]>;
def int_mips_subq_s_w: ClangBuiltin<"__builtin_mips_subq_s_w">,
  Intrinsic<[mips_q31_ty], [mips_q31_ty, mips_q31_ty], []>;

def int_mips_addsc: ClangBuiltin<"__builtin_mips_addsc">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [Commutative]>;
def int_mips_addwc: ClangBuiltin<"__builtin_mips_addwc">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [Commutative]>;

def int_mips_modsub: ClangBuiltin<"__builtin_mips_modsub">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;

def int_mips_raddu_w_qb: ClangBuiltin<"__builtin_mips_raddu_w_qb">,
  Intrinsic<[llvm_i32_ty], [llvm_v4i8_ty], [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// Absolute value

def int_mips_absq_s_ph: ClangBuiltin<"__builtin_mips_absq_s_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty], []>;
def int_mips_absq_s_w: ClangBuiltin<"__builtin_mips_absq_s_w">,
  Intrinsic<[mips_q31_ty], [mips_q31_ty], []>;

//===----------------------------------------------------------------------===//
// Precision reduce/expand

def int_mips_precrq_qb_ph: ClangBuiltin<"__builtin_mips_precrq_qb_ph">,
  Intrinsic<[llvm_v4i8_ty], [mips_v2q15_ty, mips_v2q15_ty], [IntrNoMem]>;
def int_mips_precrqu_s_qb_ph: ClangBuiltin<"__builtin_mips_precrqu_s_qb_ph">,
  Intrinsic<[llvm_v4i8_ty], [mips_v2q15_ty, mips_v2q15_ty], []>;
def int_mips_precrq_ph_w: ClangBuiltin<"__builtin_mips_precrq_ph_w">,
  Intrinsic<[mips_v2q15_ty], [mips_q31_ty, mips_q31_ty], [IntrNoMem]>;
def int_mips_precrq_rs_ph_w: ClangBuiltin<"__builtin_mips_precrq_rs_ph_w">,
  Intrinsic<[mips_v2q15_ty], [mips_q31_ty, mips_q31_ty], []>;
def int_mips_preceq_w_phl: ClangBuiltin<"__builtin_mips_preceq_w_phl">,
  Intrinsic<[mips_q31_ty], [mips_v2q15_ty], [IntrNoMem]>;
def int_mips_preceq_w_phr: ClangBuiltin<"__builtin_mips_preceq_w_phr">,
  Intrinsic<[mips_q31_ty], [mips_v2q15_ty], [IntrNoMem]>;
def int_mips_precequ_ph_qbl: ClangBuiltin<"__builtin_mips_precequ_ph_qbl">,
  Intrinsic<[mips_v2q15_ty], [llvm_v4i8_ty], [IntrNoMem]>;
def int_mips_precequ_ph_qbr: ClangBuiltin<"__builtin_mips_precequ_ph_qbr">,
  Intrinsic<[mips_v2q15_ty], [llvm_v4i8_ty], [IntrNoMem]>;
def int_mips_precequ_ph_qbla: ClangBuiltin<"__builtin_mips_precequ_ph_qbla">,
  Intrinsic<[mips_v2q15_ty], [llvm_v4i8_ty], [IntrNoMem]>;
def int_mips_precequ_ph_qbra: ClangBuiltin<"__builtin_mips_precequ_ph_qbra">,
  Intrinsic<[mips_v2q15_ty], [llvm_v4i8_ty], [IntrNoMem]>;
def int_mips_preceu_ph_qbl: ClangBuiltin<"__builtin_mips_preceu_ph_qbl">,
  Intrinsic<[mips_v2q15_ty], [llvm_v4i8_ty], [IntrNoMem]>;
def int_mips_preceu_ph_qbr: ClangBuiltin<"__builtin_mips_preceu_ph_qbr">,
  Intrinsic<[mips_v2q15_ty], [llvm_v4i8_ty], [IntrNoMem]>;
def int_mips_preceu_ph_qbla: ClangBuiltin<"__builtin_mips_preceu_ph_qbla">,
  Intrinsic<[mips_v2q15_ty], [llvm_v4i8_ty], [IntrNoMem]>;
def int_mips_preceu_ph_qbra: ClangBuiltin<"__builtin_mips_preceu_ph_qbra">,
  Intrinsic<[mips_v2q15_ty], [llvm_v4i8_ty], [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// Shift

def int_mips_shll_qb: ClangBuiltin<"__builtin_mips_shll_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_i32_ty], []>;
def int_mips_shrl_qb: ClangBuiltin<"__builtin_mips_shrl_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_shll_ph: ClangBuiltin<"__builtin_mips_shll_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, llvm_i32_ty], []>;
def int_mips_shll_s_ph: ClangBuiltin<"__builtin_mips_shll_s_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, llvm_i32_ty], []>;
def int_mips_shra_ph: ClangBuiltin<"__builtin_mips_shra_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_shra_r_ph: ClangBuiltin<"__builtin_mips_shra_r_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_shll_s_w: ClangBuiltin<"__builtin_mips_shll_s_w">,
  Intrinsic<[mips_q31_ty], [mips_q31_ty, llvm_i32_ty], []>;
def int_mips_shra_r_w: ClangBuiltin<"__builtin_mips_shra_r_w">,
  Intrinsic<[mips_q31_ty], [mips_q31_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_shilo: ClangBuiltin<"__builtin_mips_shilo">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty], [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// Multiplication

def int_mips_muleu_s_ph_qbl: ClangBuiltin<"__builtin_mips_muleu_s_ph_qbl">,
  Intrinsic<[mips_v2q15_ty], [llvm_v4i8_ty, mips_v2q15_ty], []>;
def int_mips_muleu_s_ph_qbr: ClangBuiltin<"__builtin_mips_muleu_s_ph_qbr">,
  Intrinsic<[mips_v2q15_ty], [llvm_v4i8_ty, mips_v2q15_ty], []>;
def int_mips_mulq_rs_ph: ClangBuiltin<"__builtin_mips_mulq_rs_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, mips_v2q15_ty], [Commutative]>;
def int_mips_muleq_s_w_phl: ClangBuiltin<"__builtin_mips_muleq_s_w_phl">,
  Intrinsic<[mips_q31_ty], [mips_v2q15_ty, mips_v2q15_ty], [Commutative]>;
def int_mips_muleq_s_w_phr: ClangBuiltin<"__builtin_mips_muleq_s_w_phr">,
  Intrinsic<[mips_q31_ty], [mips_v2q15_ty, mips_v2q15_ty], [Commutative]>;
def int_mips_mulsaq_s_w_ph: ClangBuiltin<"__builtin_mips_mulsaq_s_w_ph">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_v2q15_ty, mips_v2q15_ty], []>;
def int_mips_maq_s_w_phl: ClangBuiltin<"__builtin_mips_maq_s_w_phl">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_v2q15_ty, mips_v2q15_ty], []>;
def int_mips_maq_s_w_phr: ClangBuiltin<"__builtin_mips_maq_s_w_phr">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_v2q15_ty, mips_v2q15_ty], []>;
def int_mips_maq_sa_w_phl: ClangBuiltin<"__builtin_mips_maq_sa_w_phl">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_v2q15_ty, mips_v2q15_ty], []>;
def int_mips_maq_sa_w_phr: ClangBuiltin<"__builtin_mips_maq_sa_w_phr">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_v2q15_ty, mips_v2q15_ty], []>;
def int_mips_mult: ClangBuiltin<"__builtin_mips_mult">,
  Intrinsic<[llvm_i64_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, Commutative]>;
def int_mips_multu: ClangBuiltin<"__builtin_mips_multu">,
  Intrinsic<[llvm_i64_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, Commutative]>;

//===----------------------------------------------------------------------===//
// Dot product with accumulate/subtract

def int_mips_dpau_h_qbl: ClangBuiltin<"__builtin_mips_dpau_h_qbl">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_v4i8_ty, llvm_v4i8_ty],
            [IntrNoMem]>;
def int_mips_dpau_h_qbr: ClangBuiltin<"__builtin_mips_dpau_h_qbr">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_v4i8_ty, llvm_v4i8_ty],
            [IntrNoMem]>;
def int_mips_dpsu_h_qbl: ClangBuiltin<"__builtin_mips_dpsu_h_qbl">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_v4i8_ty, llvm_v4i8_ty],
            [IntrNoMem]>;
def int_mips_dpsu_h_qbr: ClangBuiltin<"__builtin_mips_dpsu_h_qbr">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_v4i8_ty, llvm_v4i8_ty],
            [IntrNoMem]>;
def int_mips_dpaq_s_w_ph: ClangBuiltin<"__builtin_mips_dpaq_s_w_ph">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_v2q15_ty, mips_v2q15_ty], []>;
def int_mips_dpsq_s_w_ph: ClangBuiltin<"__builtin_mips_dpsq_s_w_ph">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_v2q15_ty, mips_v2q15_ty], []>;
def int_mips_dpaq_sa_l_w: ClangBuiltin<"__builtin_mips_dpaq_sa_l_w">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_q31_ty, mips_q31_ty], []>;
def int_mips_dpsq_sa_l_w: ClangBuiltin<"__builtin_mips_dpsq_sa_l_w">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_q31_ty, mips_q31_ty], []>;

//===----------------------------------------------------------------------===//
// Comparison

def int_mips_cmpu_eq_qb: ClangBuiltin<"__builtin_mips_cmpu_eq_qb">,
  Intrinsic<[], [llvm_v4i8_ty, llvm_v4i8_ty], [Commutative]>;
def int_mips_cmpu_lt_qb: ClangBuiltin<"__builtin_mips_cmpu_lt_qb">,
  Intrinsic<[], [llvm_v4i8_ty, llvm_v4i8_ty], []>;
def int_mips_cmpu_le_qb: ClangBuiltin<"__builtin_mips_cmpu_le_qb">,
  Intrinsic<[], [llvm_v4i8_ty, llvm_v4i8_ty], []>;
def int_mips_cmpgu_eq_qb: ClangBuiltin<"__builtin_mips_cmpgu_eq_qb">,
  Intrinsic<[llvm_i32_ty], [llvm_v4i8_ty, llvm_v4i8_ty], [Commutative]>;
def int_mips_cmpgu_lt_qb: ClangBuiltin<"__builtin_mips_cmpgu_lt_qb">,
  Intrinsic<[llvm_i32_ty], [llvm_v4i8_ty, llvm_v4i8_ty], []>;
def int_mips_cmpgu_le_qb: ClangBuiltin<"__builtin_mips_cmpgu_le_qb">,
  Intrinsic<[llvm_i32_ty], [llvm_v4i8_ty, llvm_v4i8_ty], []>;
def int_mips_cmp_eq_ph: ClangBuiltin<"__builtin_mips_cmp_eq_ph">,
  Intrinsic<[], [mips_v2q15_ty, mips_v2q15_ty], [Commutative]>;
def int_mips_cmp_lt_ph: ClangBuiltin<"__builtin_mips_cmp_lt_ph">,
  Intrinsic<[], [mips_v2q15_ty, mips_v2q15_ty], []>;
def int_mips_cmp_le_ph: ClangBuiltin<"__builtin_mips_cmp_le_ph">,
  Intrinsic<[], [mips_v2q15_ty, mips_v2q15_ty], []>;

//===----------------------------------------------------------------------===//
// Extracting

def int_mips_extr_s_h: ClangBuiltin<"__builtin_mips_extr_s_h">,
  Intrinsic<[llvm_i32_ty], [llvm_i64_ty, llvm_i32_ty], []>;
def int_mips_extr_w: ClangBuiltin<"__builtin_mips_extr_w">,
  Intrinsic<[llvm_i32_ty], [llvm_i64_ty, llvm_i32_ty], []>;
def int_mips_extr_rs_w: ClangBuiltin<"__builtin_mips_extr_rs_w">,
  Intrinsic<[llvm_i32_ty], [llvm_i64_ty, llvm_i32_ty], []>;
def int_mips_extr_r_w: ClangBuiltin<"__builtin_mips_extr_r_w">,
  Intrinsic<[llvm_i32_ty], [llvm_i64_ty, llvm_i32_ty], []>;
def int_mips_extp: ClangBuiltin<"__builtin_mips_extp">,
  Intrinsic<[llvm_i32_ty], [llvm_i64_ty, llvm_i32_ty], []>;
def int_mips_extpdp: ClangBuiltin<"__builtin_mips_extpdp">,
  Intrinsic<[llvm_i32_ty], [llvm_i64_ty, llvm_i32_ty], []>;

//===----------------------------------------------------------------------===//
// Misc

def int_mips_wrdsp: ClangBuiltin<"__builtin_mips_wrdsp">,
  Intrinsic<[], [llvm_i32_ty, llvm_i32_ty], [ImmArg<ArgIndex<1>>]>;
def int_mips_rddsp: ClangBuiltin<"__builtin_mips_rddsp">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrReadMem, ImmArg<ArgIndex<0>>]>;

def int_mips_insv: ClangBuiltin<"__builtin_mips_insv">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty], [IntrReadMem]>;
def int_mips_bitrev: ClangBuiltin<"__builtin_mips_bitrev">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem]>;

def int_mips_packrl_ph: ClangBuiltin<"__builtin_mips_packrl_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, mips_v2q15_ty], [IntrNoMem]>;

def int_mips_repl_qb: ClangBuiltin<"__builtin_mips_repl_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_i32_ty], [IntrNoMem]>;
def int_mips_repl_ph: ClangBuiltin<"__builtin_mips_repl_ph">,
  Intrinsic<[mips_v2q15_ty], [llvm_i32_ty], [IntrNoMem]>;

def int_mips_pick_qb: ClangBuiltin<"__builtin_mips_pick_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_v4i8_ty], [IntrReadMem]>;
def int_mips_pick_ph: ClangBuiltin<"__builtin_mips_pick_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, mips_v2q15_ty], [IntrReadMem]>;

def int_mips_mthlip: ClangBuiltin<"__builtin_mips_mthlip">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty], []>;

def int_mips_bposge32: ClangBuiltin<"__builtin_mips_bposge32">,
  Intrinsic<[llvm_i32_ty], [], [IntrReadMem]>;

def int_mips_lbux: ClangBuiltin<"__builtin_mips_lbux">,
  Intrinsic<[llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty], [IntrReadMem, IntrArgMemOnly]>;
def int_mips_lhx: ClangBuiltin<"__builtin_mips_lhx">,
  Intrinsic<[llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty], [IntrReadMem, IntrArgMemOnly]>;
def int_mips_lwx: ClangBuiltin<"__builtin_mips_lwx">,
  Intrinsic<[llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty], [IntrReadMem, IntrArgMemOnly]>;

//===----------------------------------------------------------------------===//
// MIPS DSP Rev 2

def int_mips_absq_s_qb: ClangBuiltin<"__builtin_mips_absq_s_qb">,
  Intrinsic<[mips_v4q7_ty], [mips_v4q7_ty], []>;

def int_mips_addqh_ph: ClangBuiltin<"__builtin_mips_addqh_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, mips_v2q15_ty],
            [IntrNoMem, Commutative]>;
def int_mips_addqh_r_ph: ClangBuiltin<"__builtin_mips_addqh_r_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, mips_v2q15_ty],
            [IntrNoMem, Commutative]>;
def int_mips_addqh_w: ClangBuiltin<"__builtin_mips_addqh_w">,
  Intrinsic<[mips_q31_ty], [mips_q31_ty, mips_q31_ty],
            [IntrNoMem, Commutative]>;
def int_mips_addqh_r_w: ClangBuiltin<"__builtin_mips_addqh_r_w">,
  Intrinsic<[mips_q31_ty], [mips_q31_ty, mips_q31_ty],
            [IntrNoMem, Commutative]>;

def int_mips_addu_ph: ClangBuiltin<"__builtin_mips_addu_ph">,
  Intrinsic<[llvm_v2i16_ty], [llvm_v2i16_ty, llvm_v2i16_ty], [Commutative]>;
def int_mips_addu_s_ph: ClangBuiltin<"__builtin_mips_addu_s_ph">,
  Intrinsic<[llvm_v2i16_ty], [llvm_v2i16_ty, llvm_v2i16_ty], [Commutative]>;

def int_mips_adduh_qb: ClangBuiltin<"__builtin_mips_adduh_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_v4i8_ty],
            [IntrNoMem, Commutative]>;
def int_mips_adduh_r_qb: ClangBuiltin<"__builtin_mips_adduh_r_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_v4i8_ty],
            [IntrNoMem, Commutative]>;

def int_mips_append: ClangBuiltin<"__builtin_mips_append">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_mips_balign: ClangBuiltin<"__builtin_mips_balign">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_mips_cmpgdu_eq_qb: ClangBuiltin<"__builtin_mips_cmpgdu_eq_qb">,
  Intrinsic<[llvm_i32_ty], [llvm_v4i8_ty, llvm_v4i8_ty], [Commutative]>;
def int_mips_cmpgdu_lt_qb: ClangBuiltin<"__builtin_mips_cmpgdu_lt_qb">,
  Intrinsic<[llvm_i32_ty], [llvm_v4i8_ty, llvm_v4i8_ty], []>;
def int_mips_cmpgdu_le_qb: ClangBuiltin<"__builtin_mips_cmpgdu_le_qb">,
  Intrinsic<[llvm_i32_ty], [llvm_v4i8_ty, llvm_v4i8_ty], []>;

def int_mips_dpa_w_ph: ClangBuiltin<"__builtin_mips_dpa_w_ph">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_v2i16_ty, llvm_v2i16_ty],
            [IntrNoMem]>;
def int_mips_dps_w_ph: ClangBuiltin<"__builtin_mips_dps_w_ph">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_v2i16_ty, llvm_v2i16_ty],
            [IntrNoMem]>;

def int_mips_dpaqx_s_w_ph: ClangBuiltin<"__builtin_mips_dpaqx_s_w_ph">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_v2q15_ty, mips_v2q15_ty], []>;
def int_mips_dpaqx_sa_w_ph: ClangBuiltin<"__builtin_mips_dpaqx_sa_w_ph">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_v2q15_ty, mips_v2q15_ty], []>;
def int_mips_dpax_w_ph: ClangBuiltin<"__builtin_mips_dpax_w_ph">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_v2i16_ty, llvm_v2i16_ty],
            [IntrNoMem]>;
def int_mips_dpsx_w_ph: ClangBuiltin<"__builtin_mips_dpsx_w_ph">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_v2i16_ty, llvm_v2i16_ty],
            [IntrNoMem]>;
def int_mips_dpsqx_s_w_ph: ClangBuiltin<"__builtin_mips_dpsqx_s_w_ph">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_v2q15_ty, mips_v2q15_ty], []>;
def int_mips_dpsqx_sa_w_ph: ClangBuiltin<"__builtin_mips_dpsqx_sa_w_ph">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, mips_v2q15_ty, mips_v2q15_ty], []>;

def int_mips_mul_ph: ClangBuiltin<"__builtin_mips_mul_ph">,
  Intrinsic<[llvm_v2i16_ty], [llvm_v2i16_ty, llvm_v2i16_ty], [Commutative]>;
def int_mips_mul_s_ph: ClangBuiltin<"__builtin_mips_mul_s_ph">,
  Intrinsic<[llvm_v2i16_ty], [llvm_v2i16_ty, llvm_v2i16_ty], [Commutative]>;

def int_mips_mulq_rs_w: ClangBuiltin<"__builtin_mips_mulq_rs_w">,
  Intrinsic<[mips_q31_ty], [mips_q31_ty, mips_q31_ty], [Commutative]>;
def int_mips_mulq_s_ph: ClangBuiltin<"__builtin_mips_mulq_s_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, mips_v2q15_ty], [Commutative]>;
def int_mips_mulq_s_w: ClangBuiltin<"__builtin_mips_mulq_s_w">,
  Intrinsic<[mips_q31_ty], [mips_q31_ty, mips_q31_ty], [Commutative]>;
def int_mips_mulsa_w_ph: ClangBuiltin<"__builtin_mips_mulsa_w_ph">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_v2i16_ty, llvm_v2i16_ty],
            [IntrNoMem]>;

def int_mips_precr_qb_ph: ClangBuiltin<"__builtin_mips_precr_qb_ph">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v2i16_ty, llvm_v2i16_ty], []>;
def int_mips_precr_sra_ph_w: ClangBuiltin<"__builtin_mips_precr_sra_ph_w">,
  Intrinsic<[llvm_v2i16_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_mips_precr_sra_r_ph_w: ClangBuiltin<"__builtin_mips_precr_sra_r_ph_w">,
  Intrinsic<[llvm_v2i16_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_mips_prepend: ClangBuiltin<"__builtin_mips_prepend">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_mips_shra_qb: ClangBuiltin<"__builtin_mips_shra_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_shra_r_qb: ClangBuiltin<"__builtin_mips_shra_r_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_shrl_ph: ClangBuiltin<"__builtin_mips_shrl_ph">,
  Intrinsic<[llvm_v2i16_ty], [llvm_v2i16_ty, llvm_i32_ty], [IntrNoMem]>;

def int_mips_subqh_ph: ClangBuiltin<"__builtin_mips_subqh_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, mips_v2q15_ty], [IntrNoMem]>;
def int_mips_subqh_r_ph: ClangBuiltin<"__builtin_mips_subqh_r_ph">,
  Intrinsic<[mips_v2q15_ty], [mips_v2q15_ty, mips_v2q15_ty], [IntrNoMem]>;
def int_mips_subqh_w: ClangBuiltin<"__builtin_mips_subqh_w">,
  Intrinsic<[mips_q31_ty], [mips_q31_ty, mips_q31_ty], [IntrNoMem]>;
def int_mips_subqh_r_w: ClangBuiltin<"__builtin_mips_subqh_r_w">,
  Intrinsic<[mips_q31_ty], [mips_q31_ty, mips_q31_ty], [IntrNoMem]>;

def int_mips_subu_ph: ClangBuiltin<"__builtin_mips_subu_ph">,
  Intrinsic<[llvm_v2i16_ty], [llvm_v2i16_ty, llvm_v2i16_ty], []>;
def int_mips_subu_s_ph: ClangBuiltin<"__builtin_mips_subu_s_ph">,
  Intrinsic<[llvm_v2i16_ty], [llvm_v2i16_ty, llvm_v2i16_ty], []>;

def int_mips_subuh_qb: ClangBuiltin<"__builtin_mips_subuh_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_v4i8_ty], [IntrNoMem]>;
def int_mips_subuh_r_qb: ClangBuiltin<"__builtin_mips_subuh_r_qb">,
  Intrinsic<[llvm_v4i8_ty], [llvm_v4i8_ty, llvm_v4i8_ty], [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// MIPS MSA

//===----------------------------------------------------------------------===//
// Addition/subtraction

def int_mips_add_a_b : ClangBuiltin<"__builtin_msa_add_a_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
  [Commutative, IntrNoMem]>;
def int_mips_add_a_h : ClangBuiltin<"__builtin_msa_add_a_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
  [Commutative, IntrNoMem]>;
def int_mips_add_a_w : ClangBuiltin<"__builtin_msa_add_a_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
  [Commutative, IntrNoMem]>;
def int_mips_add_a_d : ClangBuiltin<"__builtin_msa_add_a_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
  [Commutative, IntrNoMem]>;

def int_mips_adds_a_b : ClangBuiltin<"__builtin_msa_adds_a_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
  [Commutative, IntrNoMem]>;
def int_mips_adds_a_h : ClangBuiltin<"__builtin_msa_adds_a_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
  [Commutative, IntrNoMem]>;
def int_mips_adds_a_w : ClangBuiltin<"__builtin_msa_adds_a_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
  [Commutative, IntrNoMem]>;
def int_mips_adds_a_d : ClangBuiltin<"__builtin_msa_adds_a_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
  [Commutative, IntrNoMem]>;

def int_mips_adds_s_b : ClangBuiltin<"__builtin_msa_adds_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
  [Commutative, IntrNoMem]>;
def int_mips_adds_s_h : ClangBuiltin<"__builtin_msa_adds_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
  [Commutative, IntrNoMem]>;
def int_mips_adds_s_w : ClangBuiltin<"__builtin_msa_adds_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
  [Commutative, IntrNoMem]>;
def int_mips_adds_s_d : ClangBuiltin<"__builtin_msa_adds_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
  [Commutative, IntrNoMem]>;

def int_mips_adds_u_b : ClangBuiltin<"__builtin_msa_adds_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
  [Commutative, IntrNoMem]>;
def int_mips_adds_u_h : ClangBuiltin<"__builtin_msa_adds_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
  [Commutative, IntrNoMem]>;
def int_mips_adds_u_w : ClangBuiltin<"__builtin_msa_adds_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
  [Commutative, IntrNoMem]>;
def int_mips_adds_u_d : ClangBuiltin<"__builtin_msa_adds_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
  [Commutative, IntrNoMem]>;

def int_mips_addv_b : ClangBuiltin<"__builtin_msa_addv_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
  [Commutative, IntrNoMem]>;
def int_mips_addv_h : ClangBuiltin<"__builtin_msa_addv_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
  [Commutative, IntrNoMem]>;
def int_mips_addv_w : ClangBuiltin<"__builtin_msa_addv_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
  [Commutative, IntrNoMem]>;
def int_mips_addv_d : ClangBuiltin<"__builtin_msa_addv_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
  [Commutative, IntrNoMem]>;

def int_mips_addvi_b : ClangBuiltin<"__builtin_msa_addvi_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty],
  [Commutative, IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_addvi_h : ClangBuiltin<"__builtin_msa_addvi_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty],
  [Commutative, IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_addvi_w : ClangBuiltin<"__builtin_msa_addvi_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty],
  [Commutative, IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_addvi_d : ClangBuiltin<"__builtin_msa_addvi_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty],
  [Commutative, IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_and_v : ClangBuiltin<"__builtin_msa_and_v">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;

def int_mips_andi_b : ClangBuiltin<"__builtin_msa_andi_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_asub_s_b : ClangBuiltin<"__builtin_msa_asub_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_asub_s_h : ClangBuiltin<"__builtin_msa_asub_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_asub_s_w : ClangBuiltin<"__builtin_msa_asub_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_asub_s_d : ClangBuiltin<"__builtin_msa_asub_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_asub_u_b : ClangBuiltin<"__builtin_msa_asub_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_asub_u_h : ClangBuiltin<"__builtin_msa_asub_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_asub_u_w : ClangBuiltin<"__builtin_msa_asub_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_asub_u_d : ClangBuiltin<"__builtin_msa_asub_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_ave_s_b : ClangBuiltin<"__builtin_msa_ave_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
  [Commutative, IntrNoMem]>;
def int_mips_ave_s_h : ClangBuiltin<"__builtin_msa_ave_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
  [Commutative, IntrNoMem]>;
def int_mips_ave_s_w : ClangBuiltin<"__builtin_msa_ave_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
  [Commutative, IntrNoMem]>;
def int_mips_ave_s_d : ClangBuiltin<"__builtin_msa_ave_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
  [Commutative, IntrNoMem]>;

def int_mips_ave_u_b : ClangBuiltin<"__builtin_msa_ave_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
  [Commutative, IntrNoMem]>;
def int_mips_ave_u_h : ClangBuiltin<"__builtin_msa_ave_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
  [Commutative, IntrNoMem]>;
def int_mips_ave_u_w : ClangBuiltin<"__builtin_msa_ave_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
  [Commutative, IntrNoMem]>;
def int_mips_ave_u_d : ClangBuiltin<"__builtin_msa_ave_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
  [Commutative, IntrNoMem]>;

def int_mips_aver_s_b : ClangBuiltin<"__builtin_msa_aver_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
  [Commutative, IntrNoMem]>;
def int_mips_aver_s_h : ClangBuiltin<"__builtin_msa_aver_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
  [Commutative, IntrNoMem]>;
def int_mips_aver_s_w : ClangBuiltin<"__builtin_msa_aver_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
  [Commutative, IntrNoMem]>;
def int_mips_aver_s_d : ClangBuiltin<"__builtin_msa_aver_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
  [Commutative, IntrNoMem]>;

def int_mips_aver_u_b : ClangBuiltin<"__builtin_msa_aver_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
  [Commutative, IntrNoMem]>;
def int_mips_aver_u_h : ClangBuiltin<"__builtin_msa_aver_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
  [Commutative, IntrNoMem]>;
def int_mips_aver_u_w : ClangBuiltin<"__builtin_msa_aver_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
  [Commutative, IntrNoMem]>;
def int_mips_aver_u_d : ClangBuiltin<"__builtin_msa_aver_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
  [Commutative, IntrNoMem]>;

def int_mips_bclr_b : ClangBuiltin<"__builtin_msa_bclr_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_bclr_h : ClangBuiltin<"__builtin_msa_bclr_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_bclr_w : ClangBuiltin<"__builtin_msa_bclr_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_bclr_d : ClangBuiltin<"__builtin_msa_bclr_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_bclri_b : ClangBuiltin<"__builtin_msa_bclri_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_bclri_h : ClangBuiltin<"__builtin_msa_bclri_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_bclri_w : ClangBuiltin<"__builtin_msa_bclri_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_bclri_d : ClangBuiltin<"__builtin_msa_bclri_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_binsl_b : ClangBuiltin<"__builtin_msa_binsl_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
            [IntrNoMem]>;
def int_mips_binsl_h : ClangBuiltin<"__builtin_msa_binsl_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
            [IntrNoMem]>;
def int_mips_binsl_w : ClangBuiltin<"__builtin_msa_binsl_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
            [IntrNoMem]>;
def int_mips_binsl_d : ClangBuiltin<"__builtin_msa_binsl_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
            [IntrNoMem]>;

def int_mips_binsli_b : ClangBuiltin<"__builtin_msa_binsli_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_mips_binsli_h : ClangBuiltin<"__builtin_msa_binsli_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_mips_binsli_w : ClangBuiltin<"__builtin_msa_binsli_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_mips_binsli_d : ClangBuiltin<"__builtin_msa_binsli_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_mips_binsr_b : ClangBuiltin<"__builtin_msa_binsr_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
            [IntrNoMem]>;
def int_mips_binsr_h : ClangBuiltin<"__builtin_msa_binsr_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
            [IntrNoMem]>;
def int_mips_binsr_w : ClangBuiltin<"__builtin_msa_binsr_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
            [IntrNoMem]>;
def int_mips_binsr_d : ClangBuiltin<"__builtin_msa_binsr_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
            [IntrNoMem]>;

def int_mips_binsri_b : ClangBuiltin<"__builtin_msa_binsri_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_mips_binsri_h : ClangBuiltin<"__builtin_msa_binsri_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_mips_binsri_w : ClangBuiltin<"__builtin_msa_binsri_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_mips_binsri_d : ClangBuiltin<"__builtin_msa_binsri_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_mips_bmnz_v : ClangBuiltin<"__builtin_msa_bmnz_v">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
            [IntrNoMem]>;

def int_mips_bmnzi_b : ClangBuiltin<"__builtin_msa_bmnzi_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_mips_bmz_v : ClangBuiltin<"__builtin_msa_bmz_v">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
            [IntrNoMem]>;

def int_mips_bmzi_b : ClangBuiltin<"__builtin_msa_bmzi_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_mips_bneg_b : ClangBuiltin<"__builtin_msa_bneg_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_bneg_h : ClangBuiltin<"__builtin_msa_bneg_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_bneg_w : ClangBuiltin<"__builtin_msa_bneg_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_bneg_d : ClangBuiltin<"__builtin_msa_bneg_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_bnegi_b : ClangBuiltin<"__builtin_msa_bnegi_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_bnegi_h : ClangBuiltin<"__builtin_msa_bnegi_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_bnegi_w : ClangBuiltin<"__builtin_msa_bnegi_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_bnegi_d : ClangBuiltin<"__builtin_msa_bnegi_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_bnz_b : ClangBuiltin<"__builtin_msa_bnz_b">,
  Intrinsic<[llvm_i32_ty], [llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_bnz_h : ClangBuiltin<"__builtin_msa_bnz_h">,
  Intrinsic<[llvm_i32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_bnz_w : ClangBuiltin<"__builtin_msa_bnz_w">,
  Intrinsic<[llvm_i32_ty], [llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_bnz_d : ClangBuiltin<"__builtin_msa_bnz_d">,
  Intrinsic<[llvm_i32_ty], [llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_bnz_v : ClangBuiltin<"__builtin_msa_bnz_v">,
  Intrinsic<[llvm_i32_ty], [llvm_v16i8_ty], [IntrNoMem]>;

def int_mips_bsel_v : ClangBuiltin<"__builtin_msa_bsel_v">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
            [IntrNoMem]>;

def int_mips_bseli_b : ClangBuiltin<"__builtin_msa_bseli_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_mips_bset_b : ClangBuiltin<"__builtin_msa_bset_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_bset_h : ClangBuiltin<"__builtin_msa_bset_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_bset_w : ClangBuiltin<"__builtin_msa_bset_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_bset_d : ClangBuiltin<"__builtin_msa_bset_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_bseti_b : ClangBuiltin<"__builtin_msa_bseti_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_bseti_h : ClangBuiltin<"__builtin_msa_bseti_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_bseti_w : ClangBuiltin<"__builtin_msa_bseti_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_bseti_d : ClangBuiltin<"__builtin_msa_bseti_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_bz_b : ClangBuiltin<"__builtin_msa_bz_b">,
  Intrinsic<[llvm_i32_ty], [llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_bz_h : ClangBuiltin<"__builtin_msa_bz_h">,
  Intrinsic<[llvm_i32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_bz_w : ClangBuiltin<"__builtin_msa_bz_w">,
  Intrinsic<[llvm_i32_ty], [llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_bz_d : ClangBuiltin<"__builtin_msa_bz_d">,
  Intrinsic<[llvm_i32_ty], [llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_bz_v : ClangBuiltin<"__builtin_msa_bz_v">,
  Intrinsic<[llvm_i32_ty], [llvm_v16i8_ty], [IntrNoMem]>;

def int_mips_ceq_b : ClangBuiltin<"__builtin_msa_ceq_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_ceq_h : ClangBuiltin<"__builtin_msa_ceq_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_ceq_w : ClangBuiltin<"__builtin_msa_ceq_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_ceq_d : ClangBuiltin<"__builtin_msa_ceq_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_ceqi_b : ClangBuiltin<"__builtin_msa_ceqi_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_ceqi_h : ClangBuiltin<"__builtin_msa_ceqi_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_ceqi_w : ClangBuiltin<"__builtin_msa_ceqi_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_ceqi_d : ClangBuiltin<"__builtin_msa_ceqi_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_cfcmsa : ClangBuiltin<"__builtin_msa_cfcmsa">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [ImmArg<ArgIndex<0>>]>;

def int_mips_cle_s_b : ClangBuiltin<"__builtin_msa_cle_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_cle_s_h : ClangBuiltin<"__builtin_msa_cle_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_cle_s_w : ClangBuiltin<"__builtin_msa_cle_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_cle_s_d : ClangBuiltin<"__builtin_msa_cle_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_cle_u_b : ClangBuiltin<"__builtin_msa_cle_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_cle_u_h : ClangBuiltin<"__builtin_msa_cle_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_cle_u_w : ClangBuiltin<"__builtin_msa_cle_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_cle_u_d : ClangBuiltin<"__builtin_msa_cle_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_clei_s_b : ClangBuiltin<"__builtin_msa_clei_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_clei_s_h : ClangBuiltin<"__builtin_msa_clei_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_clei_s_w : ClangBuiltin<"__builtin_msa_clei_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_clei_s_d : ClangBuiltin<"__builtin_msa_clei_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_clei_u_b : ClangBuiltin<"__builtin_msa_clei_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_clei_u_h : ClangBuiltin<"__builtin_msa_clei_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_clei_u_w : ClangBuiltin<"__builtin_msa_clei_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_clei_u_d : ClangBuiltin<"__builtin_msa_clei_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_clt_s_b : ClangBuiltin<"__builtin_msa_clt_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_clt_s_h : ClangBuiltin<"__builtin_msa_clt_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_clt_s_w : ClangBuiltin<"__builtin_msa_clt_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_clt_s_d : ClangBuiltin<"__builtin_msa_clt_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_clt_u_b : ClangBuiltin<"__builtin_msa_clt_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_clt_u_h : ClangBuiltin<"__builtin_msa_clt_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_clt_u_w : ClangBuiltin<"__builtin_msa_clt_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_clt_u_d : ClangBuiltin<"__builtin_msa_clt_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_clti_s_b : ClangBuiltin<"__builtin_msa_clti_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_clti_s_h : ClangBuiltin<"__builtin_msa_clti_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_clti_s_w : ClangBuiltin<"__builtin_msa_clti_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_clti_s_d : ClangBuiltin<"__builtin_msa_clti_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_clti_u_b : ClangBuiltin<"__builtin_msa_clti_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_clti_u_h : ClangBuiltin<"__builtin_msa_clti_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_clti_u_w : ClangBuiltin<"__builtin_msa_clti_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_clti_u_d : ClangBuiltin<"__builtin_msa_clti_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_copy_s_b : ClangBuiltin<"__builtin_msa_copy_s_b">,
  Intrinsic<[llvm_i32_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_copy_s_h : ClangBuiltin<"__builtin_msa_copy_s_h">,
  Intrinsic<[llvm_i32_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_copy_s_w : ClangBuiltin<"__builtin_msa_copy_s_w">,
  Intrinsic<[llvm_i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_copy_s_d : ClangBuiltin<"__builtin_msa_copy_s_d">,
  Intrinsic<[llvm_i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem]>;

def int_mips_copy_u_b : ClangBuiltin<"__builtin_msa_copy_u_b">,
  Intrinsic<[llvm_i32_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_copy_u_h : ClangBuiltin<"__builtin_msa_copy_u_h">,
  Intrinsic<[llvm_i32_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_copy_u_w : ClangBuiltin<"__builtin_msa_copy_u_w">,
  Intrinsic<[llvm_i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_copy_u_d : ClangBuiltin<"__builtin_msa_copy_u_d">,
  Intrinsic<[llvm_i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem]>;

def int_mips_ctcmsa : ClangBuiltin<"__builtin_msa_ctcmsa">,
  Intrinsic<[], [llvm_i32_ty, llvm_i32_ty], [ImmArg<ArgIndex<0>>]>;

def int_mips_div_s_b : ClangBuiltin<"__builtin_msa_div_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_div_s_h : ClangBuiltin<"__builtin_msa_div_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_div_s_w : ClangBuiltin<"__builtin_msa_div_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_div_s_d : ClangBuiltin<"__builtin_msa_div_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_div_u_b : ClangBuiltin<"__builtin_msa_div_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_div_u_h : ClangBuiltin<"__builtin_msa_div_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_div_u_w : ClangBuiltin<"__builtin_msa_div_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_div_u_d : ClangBuiltin<"__builtin_msa_div_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

// This instruction is part of the MSA spec but it does not share the
// __builtin_msa prefix because it operates on GP registers.
def int_mips_dlsa : ClangBuiltin<"__builtin_mips_dlsa">,
  Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty],
            [IntrNoMem]>;

def int_mips_dotp_s_h : ClangBuiltin<"__builtin_msa_dotp_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_dotp_s_w : ClangBuiltin<"__builtin_msa_dotp_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_dotp_s_d : ClangBuiltin<"__builtin_msa_dotp_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;

def int_mips_dotp_u_h : ClangBuiltin<"__builtin_msa_dotp_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_dotp_u_w : ClangBuiltin<"__builtin_msa_dotp_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_dotp_u_d : ClangBuiltin<"__builtin_msa_dotp_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;

def int_mips_dpadd_s_h : ClangBuiltin<"__builtin_msa_dpadd_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v16i8_ty, llvm_v16i8_ty],
  [IntrNoMem]>;
def int_mips_dpadd_s_w : ClangBuiltin<"__builtin_msa_dpadd_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v8i16_ty, llvm_v8i16_ty],
  [IntrNoMem]>;
def int_mips_dpadd_s_d : ClangBuiltin<"__builtin_msa_dpadd_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v4i32_ty, llvm_v4i32_ty],
  [IntrNoMem]>;

def int_mips_dpadd_u_h : ClangBuiltin<"__builtin_msa_dpadd_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v16i8_ty, llvm_v16i8_ty],
  [IntrNoMem]>;
def int_mips_dpadd_u_w : ClangBuiltin<"__builtin_msa_dpadd_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v8i16_ty, llvm_v8i16_ty],
  [IntrNoMem]>;
def int_mips_dpadd_u_d : ClangBuiltin<"__builtin_msa_dpadd_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v4i32_ty, llvm_v4i32_ty],
  [IntrNoMem]>;

def int_mips_dpsub_s_h : ClangBuiltin<"__builtin_msa_dpsub_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v16i8_ty, llvm_v16i8_ty],
  [IntrNoMem]>;
def int_mips_dpsub_s_w : ClangBuiltin<"__builtin_msa_dpsub_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v8i16_ty, llvm_v8i16_ty],
  [IntrNoMem]>;
def int_mips_dpsub_s_d : ClangBuiltin<"__builtin_msa_dpsub_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v4i32_ty, llvm_v4i32_ty],
  [IntrNoMem]>;

def int_mips_dpsub_u_h : ClangBuiltin<"__builtin_msa_dpsub_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v16i8_ty, llvm_v16i8_ty],
  [IntrNoMem]>;
def int_mips_dpsub_u_w : ClangBuiltin<"__builtin_msa_dpsub_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v8i16_ty, llvm_v8i16_ty],
  [IntrNoMem]>;
def int_mips_dpsub_u_d : ClangBuiltin<"__builtin_msa_dpsub_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v4i32_ty, llvm_v4i32_ty],
  [IntrNoMem]>;

def int_mips_fadd_w : ClangBuiltin<"__builtin_msa_fadd_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fadd_d : ClangBuiltin<"__builtin_msa_fadd_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fcaf_w : ClangBuiltin<"__builtin_msa_fcaf_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fcaf_d : ClangBuiltin<"__builtin_msa_fcaf_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fceq_w : ClangBuiltin<"__builtin_msa_fceq_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fceq_d : ClangBuiltin<"__builtin_msa_fceq_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fcle_w : ClangBuiltin<"__builtin_msa_fcle_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fcle_d : ClangBuiltin<"__builtin_msa_fcle_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fclt_w : ClangBuiltin<"__builtin_msa_fclt_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fclt_d : ClangBuiltin<"__builtin_msa_fclt_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fclass_w : ClangBuiltin<"__builtin_msa_fclass_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fclass_d : ClangBuiltin<"__builtin_msa_fclass_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fcne_w : ClangBuiltin<"__builtin_msa_fcne_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fcne_d : ClangBuiltin<"__builtin_msa_fcne_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fcor_w : ClangBuiltin<"__builtin_msa_fcor_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fcor_d : ClangBuiltin<"__builtin_msa_fcor_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fcueq_w : ClangBuiltin<"__builtin_msa_fcueq_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fcueq_d : ClangBuiltin<"__builtin_msa_fcueq_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fcule_w : ClangBuiltin<"__builtin_msa_fcule_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fcule_d : ClangBuiltin<"__builtin_msa_fcule_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fcult_w : ClangBuiltin<"__builtin_msa_fcult_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fcult_d : ClangBuiltin<"__builtin_msa_fcult_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fcun_w : ClangBuiltin<"__builtin_msa_fcun_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fcun_d : ClangBuiltin<"__builtin_msa_fcun_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fcune_w : ClangBuiltin<"__builtin_msa_fcune_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fcune_d : ClangBuiltin<"__builtin_msa_fcune_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fdiv_w : ClangBuiltin<"__builtin_msa_fdiv_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fdiv_d : ClangBuiltin<"__builtin_msa_fdiv_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fexdo_h : ClangBuiltin<"__builtin_msa_fexdo_h">,
  Intrinsic<[llvm_v8f16_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fexdo_w : ClangBuiltin<"__builtin_msa_fexdo_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fexp2_w : ClangBuiltin<"__builtin_msa_fexp2_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_fexp2_d : ClangBuiltin<"__builtin_msa_fexp2_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_fexupl_w : ClangBuiltin<"__builtin_msa_fexupl_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v8f16_ty], [IntrNoMem]>;
def int_mips_fexupl_d : ClangBuiltin<"__builtin_msa_fexupl_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v4f32_ty], [IntrNoMem]>;

def int_mips_fexupr_w : ClangBuiltin<"__builtin_msa_fexupr_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v8f16_ty], [IntrNoMem]>;
def int_mips_fexupr_d : ClangBuiltin<"__builtin_msa_fexupr_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v4f32_ty], [IntrNoMem]>;

def int_mips_ffint_s_w : ClangBuiltin<"__builtin_msa_ffint_s_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_ffint_s_d : ClangBuiltin<"__builtin_msa_ffint_s_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_ffint_u_w : ClangBuiltin<"__builtin_msa_ffint_u_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_ffint_u_d : ClangBuiltin<"__builtin_msa_ffint_u_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_ffql_w : ClangBuiltin<"__builtin_msa_ffql_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_ffql_d : ClangBuiltin<"__builtin_msa_ffql_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v4i32_ty], [IntrNoMem]>;

def int_mips_ffqr_w : ClangBuiltin<"__builtin_msa_ffqr_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_ffqr_d : ClangBuiltin<"__builtin_msa_ffqr_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v4i32_ty], [IntrNoMem]>;

def int_mips_fill_b : ClangBuiltin<"__builtin_msa_fill_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_i32_ty], [IntrNoMem]>;
def int_mips_fill_h : ClangBuiltin<"__builtin_msa_fill_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_i32_ty], [IntrNoMem]>;
def int_mips_fill_w : ClangBuiltin<"__builtin_msa_fill_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_i32_ty], [IntrNoMem]>;
def int_mips_fill_d : ClangBuiltin<"__builtin_msa_fill_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_i64_ty], [IntrNoMem]>;

def int_mips_flog2_w : ClangBuiltin<"__builtin_msa_flog2_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_flog2_d : ClangBuiltin<"__builtin_msa_flog2_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fmadd_w : ClangBuiltin<"__builtin_msa_fmadd_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty],
            [IntrNoMem]>;
def int_mips_fmadd_d : ClangBuiltin<"__builtin_msa_fmadd_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty],
            [IntrNoMem]>;

def int_mips_fmax_w : ClangBuiltin<"__builtin_msa_fmax_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fmax_d : ClangBuiltin<"__builtin_msa_fmax_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fmax_a_w : ClangBuiltin<"__builtin_msa_fmax_a_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fmax_a_d : ClangBuiltin<"__builtin_msa_fmax_a_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fmin_w : ClangBuiltin<"__builtin_msa_fmin_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fmin_d : ClangBuiltin<"__builtin_msa_fmin_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fmin_a_w : ClangBuiltin<"__builtin_msa_fmin_a_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fmin_a_d : ClangBuiltin<"__builtin_msa_fmin_a_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fmsub_w : ClangBuiltin<"__builtin_msa_fmsub_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty],
            [IntrNoMem]>;
def int_mips_fmsub_d : ClangBuiltin<"__builtin_msa_fmsub_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty],
            [IntrNoMem]>;

def int_mips_fmul_w : ClangBuiltin<"__builtin_msa_fmul_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fmul_d : ClangBuiltin<"__builtin_msa_fmul_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_frint_w : ClangBuiltin<"__builtin_msa_frint_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_frint_d : ClangBuiltin<"__builtin_msa_frint_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_frcp_w : ClangBuiltin<"__builtin_msa_frcp_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_frcp_d : ClangBuiltin<"__builtin_msa_frcp_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_frsqrt_w : ClangBuiltin<"__builtin_msa_frsqrt_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_frsqrt_d : ClangBuiltin<"__builtin_msa_frsqrt_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fsaf_w : ClangBuiltin<"__builtin_msa_fsaf_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fsaf_d : ClangBuiltin<"__builtin_msa_fsaf_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fseq_w : ClangBuiltin<"__builtin_msa_fseq_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fseq_d : ClangBuiltin<"__builtin_msa_fseq_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fsle_w : ClangBuiltin<"__builtin_msa_fsle_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fsle_d : ClangBuiltin<"__builtin_msa_fsle_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fslt_w : ClangBuiltin<"__builtin_msa_fslt_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fslt_d : ClangBuiltin<"__builtin_msa_fslt_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fsne_w : ClangBuiltin<"__builtin_msa_fsne_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fsne_d : ClangBuiltin<"__builtin_msa_fsne_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fsor_w : ClangBuiltin<"__builtin_msa_fsor_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fsor_d : ClangBuiltin<"__builtin_msa_fsor_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fsqrt_w : ClangBuiltin<"__builtin_msa_fsqrt_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fsqrt_d : ClangBuiltin<"__builtin_msa_fsqrt_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fsub_w : ClangBuiltin<"__builtin_msa_fsub_w">,
  Intrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fsub_d : ClangBuiltin<"__builtin_msa_fsub_d">,
  Intrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fsueq_w : ClangBuiltin<"__builtin_msa_fsueq_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fsueq_d : ClangBuiltin<"__builtin_msa_fsueq_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fsule_w : ClangBuiltin<"__builtin_msa_fsule_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fsule_d : ClangBuiltin<"__builtin_msa_fsule_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fsult_w : ClangBuiltin<"__builtin_msa_fsult_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fsult_d : ClangBuiltin<"__builtin_msa_fsult_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fsun_w : ClangBuiltin<"__builtin_msa_fsun_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fsun_d : ClangBuiltin<"__builtin_msa_fsun_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_fsune_w : ClangBuiltin<"__builtin_msa_fsune_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_fsune_d : ClangBuiltin<"__builtin_msa_fsune_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_ftint_s_w : ClangBuiltin<"__builtin_msa_ftint_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_ftint_s_d : ClangBuiltin<"__builtin_msa_ftint_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_ftint_u_w : ClangBuiltin<"__builtin_msa_ftint_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_ftint_u_d : ClangBuiltin<"__builtin_msa_ftint_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_ftq_h : ClangBuiltin<"__builtin_msa_ftq_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_ftq_w : ClangBuiltin<"__builtin_msa_ftq_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_ftrunc_s_w : ClangBuiltin<"__builtin_msa_ftrunc_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_ftrunc_s_d : ClangBuiltin<"__builtin_msa_ftrunc_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_ftrunc_u_w : ClangBuiltin<"__builtin_msa_ftrunc_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_mips_ftrunc_u_d : ClangBuiltin<"__builtin_msa_ftrunc_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

def int_mips_hadd_s_h : ClangBuiltin<"__builtin_msa_hadd_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_hadd_s_w : ClangBuiltin<"__builtin_msa_hadd_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_hadd_s_d : ClangBuiltin<"__builtin_msa_hadd_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;

def int_mips_hadd_u_h : ClangBuiltin<"__builtin_msa_hadd_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_hadd_u_w : ClangBuiltin<"__builtin_msa_hadd_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_hadd_u_d : ClangBuiltin<"__builtin_msa_hadd_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;

def int_mips_hsub_s_h : ClangBuiltin<"__builtin_msa_hsub_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_hsub_s_w : ClangBuiltin<"__builtin_msa_hsub_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_hsub_s_d : ClangBuiltin<"__builtin_msa_hsub_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;

def int_mips_hsub_u_h : ClangBuiltin<"__builtin_msa_hsub_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_hsub_u_w : ClangBuiltin<"__builtin_msa_hsub_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_hsub_u_d : ClangBuiltin<"__builtin_msa_hsub_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;

def int_mips_ilvev_b : ClangBuiltin<"__builtin_msa_ilvev_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_ilvev_h : ClangBuiltin<"__builtin_msa_ilvev_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_ilvev_w : ClangBuiltin<"__builtin_msa_ilvev_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_ilvev_d : ClangBuiltin<"__builtin_msa_ilvev_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_ilvl_b : ClangBuiltin<"__builtin_msa_ilvl_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_ilvl_h : ClangBuiltin<"__builtin_msa_ilvl_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_ilvl_w : ClangBuiltin<"__builtin_msa_ilvl_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_ilvl_d : ClangBuiltin<"__builtin_msa_ilvl_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_ilvod_b : ClangBuiltin<"__builtin_msa_ilvod_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_ilvod_h : ClangBuiltin<"__builtin_msa_ilvod_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_ilvod_w : ClangBuiltin<"__builtin_msa_ilvod_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_ilvod_d : ClangBuiltin<"__builtin_msa_ilvod_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_ilvr_b : ClangBuiltin<"__builtin_msa_ilvr_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_ilvr_h : ClangBuiltin<"__builtin_msa_ilvr_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_ilvr_w : ClangBuiltin<"__builtin_msa_ilvr_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_ilvr_d : ClangBuiltin<"__builtin_msa_ilvr_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_insert_b : ClangBuiltin<"__builtin_msa_insert_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem]>;
def int_mips_insert_h : ClangBuiltin<"__builtin_msa_insert_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem]>;
def int_mips_insert_w : ClangBuiltin<"__builtin_msa_insert_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem]>;
def int_mips_insert_d : ClangBuiltin<"__builtin_msa_insert_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty, llvm_i64_ty],
  [IntrNoMem]>;

def int_mips_insve_b : ClangBuiltin<"__builtin_msa_insve_b">,
  Intrinsic<[llvm_v16i8_ty],
            [llvm_v16i8_ty, llvm_i32_ty, llvm_v16i8_ty],
            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_insve_h : ClangBuiltin<"__builtin_msa_insve_h">,
  Intrinsic<[llvm_v8i16_ty],
            [llvm_v8i16_ty, llvm_i32_ty, llvm_v8i16_ty],
            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_insve_w : ClangBuiltin<"__builtin_msa_insve_w">,
  Intrinsic<[llvm_v4i32_ty],
            [llvm_v4i32_ty, llvm_i32_ty, llvm_v4i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_insve_d : ClangBuiltin<"__builtin_msa_insve_d">,
  Intrinsic<[llvm_v2i64_ty],
            [llvm_v2i64_ty, llvm_i32_ty, llvm_v2i64_ty],
            [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_ld_b : ClangBuiltin<"__builtin_msa_ld_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_ptr_ty, llvm_i32_ty],
  [IntrReadMem, IntrArgMemOnly]>;
def int_mips_ld_h : ClangBuiltin<"__builtin_msa_ld_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_ptr_ty, llvm_i32_ty],
  [IntrReadMem, IntrArgMemOnly]>;
def int_mips_ld_w : ClangBuiltin<"__builtin_msa_ld_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_ptr_ty, llvm_i32_ty],
  [IntrReadMem, IntrArgMemOnly]>;
def int_mips_ld_d : ClangBuiltin<"__builtin_msa_ld_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_ptr_ty, llvm_i32_ty],
  [IntrReadMem, IntrArgMemOnly]>;

def int_mips_ldr_d : ClangBuiltin<"__builtin_msa_ldr_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_ptr_ty, llvm_i32_ty],
  [IntrReadMem, IntrArgMemOnly]>;
def int_mips_ldr_w : ClangBuiltin<"__builtin_msa_ldr_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_ptr_ty, llvm_i32_ty],
  [IntrReadMem, IntrArgMemOnly]>;

def int_mips_ldi_b : ClangBuiltin<"__builtin_msa_ldi_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_mips_ldi_h : ClangBuiltin<"__builtin_msa_ldi_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_mips_ldi_w : ClangBuiltin<"__builtin_msa_ldi_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_mips_ldi_d : ClangBuiltin<"__builtin_msa_ldi_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<0>>]>;

// This instruction is part of the MSA spec but it does not share the
// __builtin_msa prefix because it operates on the GPR registers.
def int_mips_lsa : ClangBuiltin<"__builtin_mips_lsa">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem]>;

def int_mips_madd_q_h : ClangBuiltin<"__builtin_msa_madd_q_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
  [IntrNoMem]>;
def int_mips_madd_q_w : ClangBuiltin<"__builtin_msa_madd_q_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
  [IntrNoMem]>;

def int_mips_maddr_q_h : ClangBuiltin<"__builtin_msa_maddr_q_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
  [IntrNoMem]>;
def int_mips_maddr_q_w : ClangBuiltin<"__builtin_msa_maddr_q_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
  [IntrNoMem]>;

def int_mips_maddv_b : ClangBuiltin<"__builtin_msa_maddv_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
  [IntrNoMem]>;
def int_mips_maddv_h : ClangBuiltin<"__builtin_msa_maddv_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
  [IntrNoMem]>;
def int_mips_maddv_w : ClangBuiltin<"__builtin_msa_maddv_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
  [IntrNoMem]>;
def int_mips_maddv_d : ClangBuiltin<"__builtin_msa_maddv_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
  [IntrNoMem]>;

def int_mips_max_a_b : ClangBuiltin<"__builtin_msa_max_a_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_max_a_h : ClangBuiltin<"__builtin_msa_max_a_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_max_a_w : ClangBuiltin<"__builtin_msa_max_a_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_max_a_d : ClangBuiltin<"__builtin_msa_max_a_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_max_s_b : ClangBuiltin<"__builtin_msa_max_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_max_s_h : ClangBuiltin<"__builtin_msa_max_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_max_s_w : ClangBuiltin<"__builtin_msa_max_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_max_s_d : ClangBuiltin<"__builtin_msa_max_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_max_u_b : ClangBuiltin<"__builtin_msa_max_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_max_u_h : ClangBuiltin<"__builtin_msa_max_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_max_u_w : ClangBuiltin<"__builtin_msa_max_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_max_u_d : ClangBuiltin<"__builtin_msa_max_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_maxi_s_b : ClangBuiltin<"__builtin_msa_maxi_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_maxi_s_h : ClangBuiltin<"__builtin_msa_maxi_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_maxi_s_w : ClangBuiltin<"__builtin_msa_maxi_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_maxi_s_d : ClangBuiltin<"__builtin_msa_maxi_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_maxi_u_b : ClangBuiltin<"__builtin_msa_maxi_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_maxi_u_h : ClangBuiltin<"__builtin_msa_maxi_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_maxi_u_w : ClangBuiltin<"__builtin_msa_maxi_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_maxi_u_d : ClangBuiltin<"__builtin_msa_maxi_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_min_a_b : ClangBuiltin<"__builtin_msa_min_a_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_min_a_h : ClangBuiltin<"__builtin_msa_min_a_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_min_a_w : ClangBuiltin<"__builtin_msa_min_a_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_min_a_d : ClangBuiltin<"__builtin_msa_min_a_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_min_s_b : ClangBuiltin<"__builtin_msa_min_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_min_s_h : ClangBuiltin<"__builtin_msa_min_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_min_s_w : ClangBuiltin<"__builtin_msa_min_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_min_s_d : ClangBuiltin<"__builtin_msa_min_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_min_u_b : ClangBuiltin<"__builtin_msa_min_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_min_u_h : ClangBuiltin<"__builtin_msa_min_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_min_u_w : ClangBuiltin<"__builtin_msa_min_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_min_u_d : ClangBuiltin<"__builtin_msa_min_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_mini_s_b : ClangBuiltin<"__builtin_msa_mini_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_mini_s_h : ClangBuiltin<"__builtin_msa_mini_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_mini_s_w : ClangBuiltin<"__builtin_msa_mini_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_mini_s_d : ClangBuiltin<"__builtin_msa_mini_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_mini_u_b : ClangBuiltin<"__builtin_msa_mini_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_mini_u_h : ClangBuiltin<"__builtin_msa_mini_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_mini_u_w : ClangBuiltin<"__builtin_msa_mini_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_mini_u_d : ClangBuiltin<"__builtin_msa_mini_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_mod_s_b : ClangBuiltin<"__builtin_msa_mod_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_mod_s_h : ClangBuiltin<"__builtin_msa_mod_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_mod_s_w : ClangBuiltin<"__builtin_msa_mod_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_mod_s_d : ClangBuiltin<"__builtin_msa_mod_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_mod_u_b : ClangBuiltin<"__builtin_msa_mod_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_mod_u_h : ClangBuiltin<"__builtin_msa_mod_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_mod_u_w : ClangBuiltin<"__builtin_msa_mod_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_mod_u_d : ClangBuiltin<"__builtin_msa_mod_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_move_v : ClangBuiltin<"__builtin_msa_move_v">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty], [IntrNoMem]>;

def int_mips_msub_q_h : ClangBuiltin<"__builtin_msa_msub_q_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
  [IntrNoMem]>;
def int_mips_msub_q_w : ClangBuiltin<"__builtin_msa_msub_q_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
  [IntrNoMem]>;

def int_mips_msubr_q_h : ClangBuiltin<"__builtin_msa_msubr_q_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
  [IntrNoMem]>;
def int_mips_msubr_q_w : ClangBuiltin<"__builtin_msa_msubr_q_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
  [IntrNoMem]>;

def int_mips_msubv_b : ClangBuiltin<"__builtin_msa_msubv_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
  [IntrNoMem]>;
def int_mips_msubv_h : ClangBuiltin<"__builtin_msa_msubv_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
  [IntrNoMem]>;
def int_mips_msubv_w : ClangBuiltin<"__builtin_msa_msubv_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
  [IntrNoMem]>;
def int_mips_msubv_d : ClangBuiltin<"__builtin_msa_msubv_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
  [IntrNoMem]>;

def int_mips_mul_q_h : ClangBuiltin<"__builtin_msa_mul_q_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_mul_q_w : ClangBuiltin<"__builtin_msa_mul_q_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;

def int_mips_mulr_q_h : ClangBuiltin<"__builtin_msa_mulr_q_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_mulr_q_w : ClangBuiltin<"__builtin_msa_mulr_q_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;

def int_mips_mulv_b : ClangBuiltin<"__builtin_msa_mulv_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_mulv_h : ClangBuiltin<"__builtin_msa_mulv_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_mulv_w : ClangBuiltin<"__builtin_msa_mulv_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_mulv_d : ClangBuiltin<"__builtin_msa_mulv_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_nloc_b : ClangBuiltin<"__builtin_msa_nloc_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_nloc_h : ClangBuiltin<"__builtin_msa_nloc_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_nloc_w : ClangBuiltin<"__builtin_msa_nloc_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_nloc_d : ClangBuiltin<"__builtin_msa_nloc_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_nlzc_b : ClangBuiltin<"__builtin_msa_nlzc_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_nlzc_h : ClangBuiltin<"__builtin_msa_nlzc_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_nlzc_w : ClangBuiltin<"__builtin_msa_nlzc_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_nlzc_d : ClangBuiltin<"__builtin_msa_nlzc_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_nor_v : ClangBuiltin<"__builtin_msa_nor_v">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;

def int_mips_nori_b : ClangBuiltin<"__builtin_msa_nori_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_or_v : ClangBuiltin<"__builtin_msa_or_v">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;

def int_mips_ori_b : ClangBuiltin<"__builtin_msa_ori_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_pckev_b : ClangBuiltin<"__builtin_msa_pckev_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_pckev_h : ClangBuiltin<"__builtin_msa_pckev_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_pckev_w : ClangBuiltin<"__builtin_msa_pckev_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_pckev_d : ClangBuiltin<"__builtin_msa_pckev_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_pckod_b : ClangBuiltin<"__builtin_msa_pckod_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_pckod_h : ClangBuiltin<"__builtin_msa_pckod_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_pckod_w : ClangBuiltin<"__builtin_msa_pckod_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_pckod_d : ClangBuiltin<"__builtin_msa_pckod_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_pcnt_b : ClangBuiltin<"__builtin_msa_pcnt_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_pcnt_h : ClangBuiltin<"__builtin_msa_pcnt_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_pcnt_w : ClangBuiltin<"__builtin_msa_pcnt_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_pcnt_d : ClangBuiltin<"__builtin_msa_pcnt_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_sat_s_b : ClangBuiltin<"__builtin_msa_sat_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_sat_s_h : ClangBuiltin<"__builtin_msa_sat_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_sat_s_w : ClangBuiltin<"__builtin_msa_sat_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_sat_s_d : ClangBuiltin<"__builtin_msa_sat_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_sat_u_b : ClangBuiltin<"__builtin_msa_sat_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_sat_u_h : ClangBuiltin<"__builtin_msa_sat_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_sat_u_w : ClangBuiltin<"__builtin_msa_sat_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_sat_u_d : ClangBuiltin<"__builtin_msa_sat_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_shf_b : ClangBuiltin<"__builtin_msa_shf_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_shf_h : ClangBuiltin<"__builtin_msa_shf_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_shf_w : ClangBuiltin<"__builtin_msa_shf_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_sld_b : ClangBuiltin<"__builtin_msa_sld_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_sld_h : ClangBuiltin<"__builtin_msa_sld_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_sld_w : ClangBuiltin<"__builtin_msa_sld_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_sld_d : ClangBuiltin<"__builtin_msa_sld_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem]>;

def int_mips_sldi_b : ClangBuiltin<"__builtin_msa_sldi_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_mips_sldi_h : ClangBuiltin<"__builtin_msa_sldi_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_mips_sldi_w : ClangBuiltin<"__builtin_msa_sldi_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_mips_sldi_d : ClangBuiltin<"__builtin_msa_sldi_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_mips_sll_b : ClangBuiltin<"__builtin_msa_sll_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_sll_h : ClangBuiltin<"__builtin_msa_sll_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_sll_w : ClangBuiltin<"__builtin_msa_sll_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_sll_d : ClangBuiltin<"__builtin_msa_sll_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_slli_b : ClangBuiltin<"__builtin_msa_slli_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_slli_h : ClangBuiltin<"__builtin_msa_slli_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_slli_w : ClangBuiltin<"__builtin_msa_slli_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_slli_d : ClangBuiltin<"__builtin_msa_slli_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_splat_b : ClangBuiltin<"__builtin_msa_splat_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_splat_h : ClangBuiltin<"__builtin_msa_splat_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_splat_w : ClangBuiltin<"__builtin_msa_splat_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_mips_splat_d : ClangBuiltin<"__builtin_msa_splat_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem]>;

def int_mips_splati_b : ClangBuiltin<"__builtin_msa_splati_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_splati_h : ClangBuiltin<"__builtin_msa_splati_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_splati_w : ClangBuiltin<"__builtin_msa_splati_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_splati_d : ClangBuiltin<"__builtin_msa_splati_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_sra_b : ClangBuiltin<"__builtin_msa_sra_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_sra_h : ClangBuiltin<"__builtin_msa_sra_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_sra_w : ClangBuiltin<"__builtin_msa_sra_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_sra_d : ClangBuiltin<"__builtin_msa_sra_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_srai_b : ClangBuiltin<"__builtin_msa_srai_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_srai_h : ClangBuiltin<"__builtin_msa_srai_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_srai_w : ClangBuiltin<"__builtin_msa_srai_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_srai_d : ClangBuiltin<"__builtin_msa_srai_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_srar_b : ClangBuiltin<"__builtin_msa_srar_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_srar_h : ClangBuiltin<"__builtin_msa_srar_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_srar_w : ClangBuiltin<"__builtin_msa_srar_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_srar_d : ClangBuiltin<"__builtin_msa_srar_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_srari_b : ClangBuiltin<"__builtin_msa_srari_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_srari_h : ClangBuiltin<"__builtin_msa_srari_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_srari_w : ClangBuiltin<"__builtin_msa_srari_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_srari_d : ClangBuiltin<"__builtin_msa_srari_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_srl_b : ClangBuiltin<"__builtin_msa_srl_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_srl_h : ClangBuiltin<"__builtin_msa_srl_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_srl_w : ClangBuiltin<"__builtin_msa_srl_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_srl_d : ClangBuiltin<"__builtin_msa_srl_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_srli_b : ClangBuiltin<"__builtin_msa_srli_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_srli_h : ClangBuiltin<"__builtin_msa_srli_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_srli_w : ClangBuiltin<"__builtin_msa_srli_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_srli_d : ClangBuiltin<"__builtin_msa_srli_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_srlr_b : ClangBuiltin<"__builtin_msa_srlr_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_srlr_h : ClangBuiltin<"__builtin_msa_srlr_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_srlr_w : ClangBuiltin<"__builtin_msa_srlr_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_srlr_d : ClangBuiltin<"__builtin_msa_srlr_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_srlri_b : ClangBuiltin<"__builtin_msa_srlri_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_srlri_h : ClangBuiltin<"__builtin_msa_srlri_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_srlri_w : ClangBuiltin<"__builtin_msa_srlri_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_srlri_d : ClangBuiltin<"__builtin_msa_srlri_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_st_b : ClangBuiltin<"__builtin_msa_st_b">,
  Intrinsic<[], [llvm_v16i8_ty, llvm_ptr_ty, llvm_i32_ty],
  [IntrArgMemOnly]>;
def int_mips_st_h : ClangBuiltin<"__builtin_msa_st_h">,
  Intrinsic<[], [llvm_v8i16_ty, llvm_ptr_ty, llvm_i32_ty],
  [IntrArgMemOnly]>;
def int_mips_st_w : ClangBuiltin<"__builtin_msa_st_w">,
  Intrinsic<[], [llvm_v4i32_ty, llvm_ptr_ty, llvm_i32_ty],
  [IntrArgMemOnly]>;
def int_mips_st_d : ClangBuiltin<"__builtin_msa_st_d">,
  Intrinsic<[], [llvm_v2i64_ty, llvm_ptr_ty, llvm_i32_ty],
  [IntrArgMemOnly]>;

def int_mips_str_d : ClangBuiltin<"__builtin_msa_str_d">,
  Intrinsic<[], [llvm_v2i64_ty, llvm_ptr_ty, llvm_i32_ty],
  [IntrArgMemOnly]>;
def int_mips_str_w : ClangBuiltin<"__builtin_msa_str_w">,
  Intrinsic<[], [llvm_v4i32_ty, llvm_ptr_ty, llvm_i32_ty],
  [IntrArgMemOnly]>;

def int_mips_subs_s_b : ClangBuiltin<"__builtin_msa_subs_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_subs_s_h : ClangBuiltin<"__builtin_msa_subs_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_subs_s_w : ClangBuiltin<"__builtin_msa_subs_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_subs_s_d : ClangBuiltin<"__builtin_msa_subs_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_subs_u_b : ClangBuiltin<"__builtin_msa_subs_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_subs_u_h : ClangBuiltin<"__builtin_msa_subs_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_subs_u_w : ClangBuiltin<"__builtin_msa_subs_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_subs_u_d : ClangBuiltin<"__builtin_msa_subs_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_subsus_u_b : ClangBuiltin<"__builtin_msa_subsus_u_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_subsus_u_h : ClangBuiltin<"__builtin_msa_subsus_u_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_subsus_u_w : ClangBuiltin<"__builtin_msa_subsus_u_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_subsus_u_d : ClangBuiltin<"__builtin_msa_subsus_u_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_subsuu_s_b : ClangBuiltin<"__builtin_msa_subsuu_s_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_subsuu_s_h : ClangBuiltin<"__builtin_msa_subsuu_s_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_subsuu_s_w : ClangBuiltin<"__builtin_msa_subsuu_s_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_subsuu_s_d : ClangBuiltin<"__builtin_msa_subsuu_s_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_subv_b : ClangBuiltin<"__builtin_msa_subv_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;
def int_mips_subv_h : ClangBuiltin<"__builtin_msa_subv_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty], [IntrNoMem]>;
def int_mips_subv_w : ClangBuiltin<"__builtin_msa_subv_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_mips_subv_d : ClangBuiltin<"__builtin_msa_subv_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

def int_mips_subvi_b : ClangBuiltin<"__builtin_msa_subvi_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_subvi_h : ClangBuiltin<"__builtin_msa_subvi_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_subvi_w : ClangBuiltin<"__builtin_msa_subvi_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_mips_subvi_d : ClangBuiltin<"__builtin_msa_subvi_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_mips_vshf_b : ClangBuiltin<"__builtin_msa_vshf_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
            [IntrNoMem]>;
def int_mips_vshf_h : ClangBuiltin<"__builtin_msa_vshf_h">,
  Intrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
            [IntrNoMem]>;
def int_mips_vshf_w : ClangBuiltin<"__builtin_msa_vshf_w">,
  Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
            [IntrNoMem]>;
def int_mips_vshf_d : ClangBuiltin<"__builtin_msa_vshf_d">,
  Intrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
            [IntrNoMem]>;

def int_mips_xor_v : ClangBuiltin<"__builtin_msa_xor_v">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;

def int_mips_xori_b : ClangBuiltin<"__builtin_msa_xori_b">,
  Intrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
}
