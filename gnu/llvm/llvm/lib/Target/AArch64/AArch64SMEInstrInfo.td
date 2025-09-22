//=- AArch64SMEInstrInfo.td -  AArch64 SME Instructions -*- tablegen -*-----=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AArch64 Scalable Matrix Extension (SME) Instruction definitions.
//
//===----------------------------------------------------------------------===//

def AArch64_smstart : SDNode<"AArch64ISD::SMSTART", SDTypeProfile<0, 2,
                             [SDTCisInt<0>, SDTCisInt<0>]>,
                             [SDNPHasChain, SDNPSideEffect, SDNPVariadic,
                              SDNPOptInGlue, SDNPOutGlue]>;
def AArch64_smstop  : SDNode<"AArch64ISD::SMSTOP", SDTypeProfile<0, 2,
                             [SDTCisInt<0>, SDTCisInt<0>]>,
                             [SDNPHasChain, SDNPSideEffect, SDNPVariadic,
                              SDNPOptInGlue, SDNPOutGlue]>;
def AArch64_restore_za : SDNode<"AArch64ISD::RESTORE_ZA", SDTypeProfile<0, 3,
                             [SDTCisInt<0>, SDTCisPtrTy<1>]>,
                             [SDNPHasChain, SDNPSideEffect, SDNPVariadic,
                              SDNPOptInGlue]>;
def AArch64_restore_zt : SDNode<"AArch64ISD::RESTORE_ZT", SDTypeProfile<0, 2,
                                [SDTCisInt<0>, SDTCisPtrTy<1>]>,
                                [SDNPHasChain, SDNPSideEffect, SDNPMayLoad]>;
def AArch64_save_zt : SDNode<"AArch64ISD::SAVE_ZT", SDTypeProfile<0, 2,
                             [SDTCisInt<0>, SDTCisPtrTy<1>]>,
                             [SDNPHasChain, SDNPSideEffect, SDNPMayStore]>;
def AArch64CoalescerBarrier
    : SDNode<"AArch64ISD::COALESCER_BARRIER", SDTypeProfile<1, 1, []>, [SDNPOptInGlue, SDNPOutGlue]>;

def AArch64VGSave : SDNode<"AArch64ISD::VG_SAVE", SDTypeProfile<0, 0, []>,
                           [SDNPHasChain, SDNPSideEffect, SDNPOptInGlue, SDNPOutGlue]>;

def AArch64VGRestore : SDNode<"AArch64ISD::VG_RESTORE", SDTypeProfile<0, 0, []>,
                              [SDNPHasChain, SDNPSideEffect, SDNPOptInGlue, SDNPOutGlue]>;

def AArch64AllocateZABuffer : SDNode<"AArch64ISD::ALLOCATE_ZA_BUFFER", SDTypeProfile<1, 1,
                              [SDTCisInt<0>, SDTCisInt<1>]>,
                              [SDNPHasChain, SDNPSideEffect]>;
let usesCustomInserter = 1, Defs = [SP], Uses = [SP] in {
  def AllocateZABuffer : Pseudo<(outs GPR64sp:$dst), (ins GPR64:$size), []>, Sched<[WriteI]> {}
}
def : Pat<(i64 (AArch64AllocateZABuffer GPR64:$size)),
          (AllocateZABuffer $size)>;

def AArch64InitTPIDR2Obj  : SDNode<"AArch64ISD::INIT_TPIDR2OBJ", SDTypeProfile<0, 1,
                              [SDTCisInt<0>]>, [SDNPHasChain, SDNPMayStore]>;
let usesCustomInserter = 1 in {
  def InitTPIDR2Obj : Pseudo<(outs), (ins GPR64:$buffer), [(AArch64InitTPIDR2Obj GPR64:$buffer)]>, Sched<[WriteI]> {}
}

//===----------------------------------------------------------------------===//
// Instruction naming conventions.
//===----------------------------------------------------------------------===//

// M = SME array register (ZA)
// P = Predicate register
// C = Predicate-as-counter register
// I = immediate
// Z = SVE vector register
// T = ZT0 register
//

//===----------------------------------------------------------------------===//
// Add vector elements horizontally or vertically to ZA tile.
//===----------------------------------------------------------------------===//

def SDT_AArch64RDSVL  : SDTypeProfile<1, 1, [SDTCisInt<0>, SDTCisInt<1>]>;
def AArch64rdsvl : SDNode<"AArch64ISD::RDSVL", SDT_AArch64RDSVL>;

let Predicates = [HasSMEandIsNonStreamingSafe] in {
def RDSVLI_XI  : sve_int_read_vl_a<0b0, 0b11111, "rdsvl", /*streaming_sve=*/0b1>;
def ADDSPL_XXI : sve_int_arith_vl<0b1, "addspl", /*streaming_sve=*/0b1>;
def ADDSVL_XXI : sve_int_arith_vl<0b0, "addsvl", /*streaming_sve=*/0b1>;

def : Pat<(AArch64rdsvl (i32 simm6_32b:$imm)), (RDSVLI_XI simm6_32b:$imm)>;
}

let Predicates = [HasSME] in {
defm ADDHA_MPPZ_S : sme_add_vector_to_tile_u32<0b0, "addha", int_aarch64_sme_addha>;
defm ADDVA_MPPZ_S : sme_add_vector_to_tile_u32<0b1, "addva", int_aarch64_sme_addva>;
}

let Predicates = [HasSMEI16I64] in {
defm ADDHA_MPPZ_D : sme_add_vector_to_tile_u64<0b0, "addha", int_aarch64_sme_addha>;
defm ADDVA_MPPZ_D : sme_add_vector_to_tile_u64<0b1, "addva", int_aarch64_sme_addva>;
}

let Predicates = [HasSME] in {
//===----------------------------------------------------------------------===//
// Outer products
//===----------------------------------------------------------------------===//

defm BFMOPA_MPPZZ  : sme_bf16_outer_product<0b000, "bfmopa", int_aarch64_sme_mopa_wide>;
defm BFMOPS_MPPZZ  : sme_bf16_outer_product<0b001, "bfmops", int_aarch64_sme_mops_wide>;

defm FMOPA_MPPZZ_S : sme_outer_product_fp32<0b0, 0b00, ZPR32, "fmopa", int_aarch64_sme_mopa>;
defm FMOPS_MPPZZ_S : sme_outer_product_fp32<0b1, 0b00, ZPR32, "fmops", int_aarch64_sme_mops>;
}

let Predicates = [HasSMEF64F64] in {
defm FMOPA_MPPZZ_D : sme_outer_product_fp64<0b0, "fmopa", int_aarch64_sme_mopa>;
defm FMOPS_MPPZZ_D : sme_outer_product_fp64<0b1, "fmops", int_aarch64_sme_mops>;
}

let Predicates = [HasSME] in {
defm FMOPAL_MPPZZ  : sme_f16_outer_product<0b010, "fmopa", int_aarch64_sme_mopa_wide>;
defm FMOPSL_MPPZZ  : sme_f16_outer_product<0b011, "fmops", int_aarch64_sme_mops_wide>;

defm SMOPA_MPPZZ_S  : sme_int_outer_product_i32<0b000, "smopa",  int_aarch64_sme_smopa_wide>;
defm SMOPS_MPPZZ_S  : sme_int_outer_product_i32<0b001, "smops",  int_aarch64_sme_smops_wide>;
defm UMOPA_MPPZZ_S  : sme_int_outer_product_i32<0b110, "umopa",  int_aarch64_sme_umopa_wide>;
defm UMOPS_MPPZZ_S  : sme_int_outer_product_i32<0b111, "umops",  int_aarch64_sme_umops_wide>;
defm SUMOPA_MPPZZ_S : sme_int_outer_product_i32<0b010, "sumopa", int_aarch64_sme_sumopa_wide>;
defm SUMOPS_MPPZZ_S : sme_int_outer_product_i32<0b011, "sumops", int_aarch64_sme_sumops_wide>;
defm USMOPA_MPPZZ_S : sme_int_outer_product_i32<0b100, "usmopa", int_aarch64_sme_usmopa_wide>;
defm USMOPS_MPPZZ_S : sme_int_outer_product_i32<0b101, "usmops", int_aarch64_sme_usmops_wide>;
}

let Predicates = [HasSMEI16I64] in {
defm SMOPA_MPPZZ_D  : sme_int_outer_product_i64<0b000, "smopa",  int_aarch64_sme_smopa_wide>;
defm SMOPS_MPPZZ_D  : sme_int_outer_product_i64<0b001, "smops",  int_aarch64_sme_smops_wide>;
defm UMOPA_MPPZZ_D  : sme_int_outer_product_i64<0b110, "umopa",  int_aarch64_sme_umopa_wide>;
defm UMOPS_MPPZZ_D  : sme_int_outer_product_i64<0b111, "umops",  int_aarch64_sme_umops_wide>;
defm SUMOPA_MPPZZ_D : sme_int_outer_product_i64<0b010, "sumopa", int_aarch64_sme_sumopa_wide>;
defm SUMOPS_MPPZZ_D : sme_int_outer_product_i64<0b011, "sumops", int_aarch64_sme_sumops_wide>;
defm USMOPA_MPPZZ_D : sme_int_outer_product_i64<0b100, "usmopa", int_aarch64_sme_usmopa_wide>;
defm USMOPS_MPPZZ_D : sme_int_outer_product_i64<0b101, "usmops", int_aarch64_sme_usmops_wide>;
}

let Predicates = [HasSME] in {
//===----------------------------------------------------------------------===//
// Loads and stores
//===----------------------------------------------------------------------===//

defm LD1_MXIPXX : sme_mem_ld_ss<"ld1">;
defm ST1_MXIPXX : sme_mem_st_ss<"st1">;

//===----------------------------------------------------------------------===//
// Move instructions
//===----------------------------------------------------------------------===//

defm INSERT_MXIPZ  : sme_vector_to_tile<"mova">;
defm EXTRACT_ZPMXI : sme_tile_to_vector<"mova">;
} // End let Predicates = [HasSME]

let Predicates = [HasSMEandIsNonStreamingSafe] in {
//===----------------------------------------------------------------------===//
// Spill + fill
//===----------------------------------------------------------------------===//

defm LDR_ZA : sme_fill<"ldr">;
defm STR_ZA : sme_spill<"str">;

//===----------------------------------------------------------------------===//
// Zero instruction
//===----------------------------------------------------------------------===//

defm ZERO_M : sme_zero<"zero">;

//===----------------------------------------------------------------------===//
// Mode selection and state access instructions
//===----------------------------------------------------------------------===//

// Pseudo to conditionally restore ZA state. This expands:
//
//   pseudonode tpidr2_el0, tpidr2obj, restore_routine
//
// Into:
//
//   if (tpidr2_el0 == 0)
//     BL restore_routine, implicit-use tpidr2obj
//
def RestoreZAPseudo :
  Pseudo<(outs),
         (ins GPR64:$tpidr2_el0, GPR64sp:$tpidr2obj, i64imm:$restore_routine, variable_ops), []>,
         Sched<[]>;

def : Pat<(AArch64_restore_za
            (i64 GPR64:$tpidr2_el0), (i64 GPR64sp:$tpidr2obj), (i64 texternalsym:$restore_routine)),
          (RestoreZAPseudo GPR64:$tpidr2_el0, GPR64sp:$tpidr2obj, texternalsym:$restore_routine)>;

// Read and write TPIDR2_EL0
def : Pat<(int_aarch64_sme_set_tpidr2 i64:$val),
          (MSR 0xde85, GPR64:$val)>;
def : Pat<(i64 (int_aarch64_sme_get_tpidr2)),
          (MRS 0xde85)>;

} // End let Predicates = [HasSMEandIsNonStreamingSafe]

multiclass CoalescerBarrierPseudo<RegisterClass rc, list<ValueType> vts> {
  def NAME : Pseudo<(outs rc:$dst), (ins rc:$src), []>, Sched<[]> {
    let Constraints = "$dst = $src";
  }
  foreach vt = vts in {
    def : Pat<(vt (AArch64CoalescerBarrier (vt rc:$src))),
              (!cast<Instruction>(NAME) rc:$src)>;
  }
}

multiclass CoalescerBarriers {
  defm _FPR16  : CoalescerBarrierPseudo<FPR16, [bf16, f16]>;
  defm _FPR32  : CoalescerBarrierPseudo<FPR32, [f32]>;
  defm _FPR64  : CoalescerBarrierPseudo<FPR64, [f64, v8i8, v4i16, v2i32, v1i64, v4f16, v2f32, v1f64, v4bf16]>;
  defm _FPR128 : CoalescerBarrierPseudo<FPR128, [f128, v16i8, v8i16, v4i32, v2i64, v8f16, v4f32, v2f64, v8bf16]>;
}

defm COALESCER_BARRIER : CoalescerBarriers;

// Pseudo to match to smstart/smstop. This expands:
//
//  pseudonode (pstate_za|pstate_sm), before_call, expected_value
//
// Into:
//
//   if (before_call != expected_value)
//     node (pstate_za|pstate_sm)
//
// where node can be either 'smstart' or 'smstop'.
//
// This pseudo and corresponding patterns don't need to be predicated by SME,
// because when they're emitted for streaming-compatible functions and run
// in a non-SME context the generated code-paths will never execute any
// SME instructions.
def MSRpstatePseudo :
  Pseudo<(outs),
           (ins svcr_op:$pstatefield, timm0_1:$imm, timm0_31:$condition, variable_ops), []>,
    Sched<[WriteSys]> {
  let hasPostISelHook = 1;
  let Uses = [VG];
  let Defs = [VG];
}

def : Pat<(AArch64_smstart (i32 svcr_op:$pstate), (i64 timm0_31:$condition)),
          (MSRpstatePseudo svcr_op:$pstate, 0b1, timm0_31:$condition)>;
def : Pat<(AArch64_smstop (i32 svcr_op:$pstate), (i64 timm0_31:$condition)),
          (MSRpstatePseudo svcr_op:$pstate, 0b0, timm0_31:$condition)>;

// Unconditional start/stop
def : Pat<(AArch64_smstart (i32 svcr_op:$pstate), (i64 /*AArch64SME::Always*/0)),
          (MSRpstatesvcrImm1 svcr_op:$pstate, 0b1)>;
def : Pat<(AArch64_smstop (i32 svcr_op:$pstate), (i64 /*AArch64SME::Always*/0)),
          (MSRpstatesvcrImm1 svcr_op:$pstate, 0b0)>;


// Pseudo to insert cfi_offset/cfi_restore instructions. Used to save or restore
// the streaming value of VG around streaming-mode changes in locally-streaming
// functions.
def VGSavePseudo : Pseudo<(outs), (ins), []>, Sched<[]>;
def : Pat<(AArch64VGSave), (VGSavePseudo)>;

def VGRestorePseudo : Pseudo<(outs), (ins), []>, Sched<[]>;
def : Pat<(AArch64VGRestore), (VGRestorePseudo)>;

//===----------------------------------------------------------------------===//
// SME2 Instructions
//===----------------------------------------------------------------------===//
let Predicates = [HasSME2] in {
defm ADD_VG2_M2ZZ_S  : sme2_dot_mla_add_sub_array_vg2_single<"add", 0b0011010, MatrixOp32, ZZ_s, ZPR4b32, nxv4i32, int_aarch64_sme_add_write_single_za_vg1x2>;
defm ADD_VG4_M4ZZ_S  : sme2_dot_mla_add_sub_array_vg4_single<"add", 0b0111010, MatrixOp32, ZZZZ_s, ZPR4b32, nxv4i32, int_aarch64_sme_add_write_single_za_vg1x4>;
defm ADD_VG2_M2Z2Z_S : sme2_dot_mla_add_sub_array_vg2_multi<"add", 0b0110010, MatrixOp32, ZZ_s_mul_r, nxv4i32, int_aarch64_sme_add_write_za_vg1x2>;
defm ADD_VG4_M4Z4Z_S : sme2_dot_mla_add_sub_array_vg4_multi<"add", 0b0110010, MatrixOp32, ZZZZ_s_mul_r, nxv4i32, int_aarch64_sme_add_write_za_vg1x4>;

defm ADD_VG2_2ZZ  : sme2_int_sve_destructive_vector_vg2_single<"add", 0b0110000>;
defm ADD_VG4_4ZZ  : sme2_int_sve_destructive_vector_vg4_single<"add", 0b0110000>;

defm SUB_VG2_M2ZZ_S  : sme2_dot_mla_add_sub_array_vg2_single<"sub", 0b0011011, MatrixOp32, ZZ_s, ZPR4b32, nxv4i32, int_aarch64_sme_sub_write_single_za_vg1x2>;
defm SUB_VG4_M4ZZ_S  : sme2_dot_mla_add_sub_array_vg4_single<"sub", 0b0111011, MatrixOp32, ZZZZ_s, ZPR4b32, nxv4i32, int_aarch64_sme_sub_write_single_za_vg1x4>;
defm SUB_VG2_M2Z2Z_S : sme2_dot_mla_add_sub_array_vg2_multi<"sub", 0b0110011, MatrixOp32, ZZ_s_mul_r, nxv4i32, int_aarch64_sme_sub_write_za_vg1x2>;
defm SUB_VG4_M4Z4Z_S : sme2_dot_mla_add_sub_array_vg4_multi<"sub", 0b0110011, MatrixOp32, ZZZZ_s_mul_r, nxv4i32, int_aarch64_sme_sub_write_za_vg1x4>;

defm FMLA_VG2_M2ZZ_S  : sme2_dot_mla_add_sub_array_vg2_single<"fmla", 0b0011000, MatrixOp32, ZZ_s, ZPR4b32, nxv4f32, int_aarch64_sme_fmla_single_vg1x2>;
defm FMLA_VG4_M4ZZ_S  : sme2_dot_mla_add_sub_array_vg4_single<"fmla", 0b0111000, MatrixOp32, ZZZZ_s, ZPR4b32, nxv4f32, int_aarch64_sme_fmla_single_vg1x4>;
defm FMLA_VG2_M2Z2Z_S : sme2_dot_mla_add_sub_array_vg2_multi<"fmla", 0b0110000, MatrixOp32, ZZ_s_mul_r, nxv4f32, int_aarch64_sme_fmla_vg1x2>;
defm FMLA_VG4_M4Z4Z_S : sme2_dot_mla_add_sub_array_vg4_multi<"fmla", 0b0110000, MatrixOp32, ZZZZ_s_mul_r, nxv4f32, int_aarch64_sme_fmla_vg1x4>;
defm FMLA_VG2_M2ZZI_S : sme2_multi_vec_array_vg2_index_32b<"fmla", 0b01, 0b0000, ZZ_s_mul_r, ZPR4b32, nxv4f32, int_aarch64_sme_fmla_lane_vg1x2>;
defm FMLA_VG4_M4ZZI_S : sme2_multi_vec_array_vg4_index_32b<"fmla", 0b0000, ZZZZ_s_mul_r, ZPR4b32, nxv4f32, int_aarch64_sme_fmla_lane_vg1x4>;

defm FMLS_VG2_M2ZZ_S  : sme2_dot_mla_add_sub_array_vg2_single<"fmls", 0b0011001, MatrixOp32, ZZ_s, ZPR4b32, nxv4f32, int_aarch64_sme_fmls_single_vg1x2>;
defm FMLS_VG4_M4ZZ_S  : sme2_dot_mla_add_sub_array_vg4_single<"fmls", 0b0111001, MatrixOp32, ZZZZ_s, ZPR4b32, nxv4f32, int_aarch64_sme_fmls_single_vg1x4>;
defm FMLS_VG2_M2Z2Z_S : sme2_dot_mla_add_sub_array_vg2_multi<"fmls", 0b0110001, MatrixOp32, ZZ_s_mul_r, nxv4f32, int_aarch64_sme_fmls_vg1x2>;
defm FMLS_VG4_M4Z4Z_S : sme2_dot_mla_add_sub_array_vg4_multi<"fmls", 0b0110001, MatrixOp32, ZZZZ_s_mul_r, nxv4f32, int_aarch64_sme_fmls_vg1x4>;
defm FMLS_VG2_M2ZZI_S : sme2_multi_vec_array_vg2_index_32b<"fmls", 0b01, 0b0010, ZZ_s_mul_r, ZPR4b32, nxv4f32, int_aarch64_sme_fmls_lane_vg1x2>;
defm FMLS_VG4_M4ZZI_S : sme2_multi_vec_array_vg4_index_32b<"fmls", 0b0010, ZZZZ_s_mul_r, ZPR4b32, nxv4f32, int_aarch64_sme_fmls_lane_vg1x4>;

defm ADD_VG2_M2Z_S : sme2_multivec_accum_add_sub_vg2<"add", 0b0010, MatrixOp32, ZZ_s_mul_r, nxv4i32, int_aarch64_sme_add_za32_vg1x2>;
defm ADD_VG4_M4Z_S : sme2_multivec_accum_add_sub_vg4<"add", 0b0010, MatrixOp32, ZZZZ_s_mul_r, nxv4i32, int_aarch64_sme_add_za32_vg1x4>;

defm SUB_VG2_M2Z_S : sme2_multivec_accum_add_sub_vg2<"sub", 0b0011, MatrixOp32, ZZ_s_mul_r, nxv4i32, int_aarch64_sme_sub_za32_vg1x2>;
defm SUB_VG4_M4Z_S : sme2_multivec_accum_add_sub_vg4<"sub", 0b0011, MatrixOp32, ZZZZ_s_mul_r, nxv4i32, int_aarch64_sme_sub_za32_vg1x4>;

defm FADD_VG2_M2Z_S : sme2_multivec_accum_add_sub_vg2<"fadd", 0b0000, MatrixOp32, ZZ_s_mul_r, nxv4f32, int_aarch64_sme_add_za32_vg1x2>;
defm FADD_VG4_M4Z_S : sme2_multivec_accum_add_sub_vg4<"fadd", 0b0000, MatrixOp32, ZZZZ_s_mul_r, nxv4f32, int_aarch64_sme_add_za32_vg1x4>;

defm FSUB_VG2_M2Z_S : sme2_multivec_accum_add_sub_vg2<"fsub", 0b0001, MatrixOp32, ZZ_s_mul_r, nxv4f32, int_aarch64_sme_sub_za32_vg1x2>;
defm FSUB_VG4_M4Z_S : sme2_multivec_accum_add_sub_vg4<"fsub", 0b0001, MatrixOp32, ZZZZ_s_mul_r, nxv4f32, int_aarch64_sme_sub_za32_vg1x4>;

defm SQDMULH_VG2_2ZZ : sme2_int_sve_destructive_vector_vg2_single<"sqdmulh", 0b1000000>;
defm SQDMULH_VG4_4ZZ : sme2_int_sve_destructive_vector_vg4_single<"sqdmulh", 0b1000000>;
defm SQDMULH_VG2_2Z2Z : sme2_int_sve_destructive_vector_vg2_multi<"sqdmulh", 0b1000000>;
defm SQDMULH_VG4_4Z4Z : sme2_int_sve_destructive_vector_vg4_multi<"sqdmulh", 0b1000000>;

defm FMLAL_MZZI      : sme2_mla_long_array_index<"fmlal",  0b10,   0b00, nxv8f16, int_aarch64_sme_fmlal_lane_vg2x1>;
defm FMLAL_VG2_M2ZZI : sme2_fp_mla_long_array_vg2_index<"fmlal",   0b00, nxv8f16, int_aarch64_sme_fmlal_lane_vg2x2>;
defm FMLAL_VG4_M4ZZI : sme2_fp_mla_long_array_vg4_index<"fmlal",   0b00, nxv8f16, int_aarch64_sme_fmlal_lane_vg2x4>;
defm FMLAL_MZZ       : sme2_mla_long_array_single<"fmlal", 0b00,   0b00, nxv8f16, int_aarch64_sme_fmlal_single_vg2x1>;
defm FMLAL_VG2_M2ZZ_HtoS  : sme2_fp_mla_long_array_vg2_single<"fmlal",  0b000, MatrixOp32, ZZ_h, ZPR4b16, nxv8f16, int_aarch64_sme_fmlal_single_vg2x2>;
defm FMLAL_VG4_M4ZZ_HtoS  : sme2_fp_mla_long_array_vg4_single<"fmlal",  0b000, MatrixOp32, ZZZZ_h, ZPR4b16, nxv8f16, int_aarch64_sme_fmlal_single_vg2x4>;
defm FMLAL_VG2_M2Z2Z_HtoS : sme2_fp_mla_long_array_vg2_multi<"fmlal",   0b000, MatrixOp32, ZZ_h_mul_r, nxv8f16, int_aarch64_sme_fmlal_vg2x2>;
defm FMLAL_VG4_M4Z4Z_HtoS : sme2_fp_mla_long_array_vg4_multi<"fmlal",   0b000, MatrixOp32, ZZZZ_h_mul_r, nxv8f16, int_aarch64_sme_fmlal_vg2x4>;

defm FMLSL_MZZI      : sme2_mla_long_array_index<"fmlsl",  0b10,   0b01, nxv8f16, int_aarch64_sme_fmlsl_lane_vg2x1>;
defm FMLSL_VG2_M2ZZI : sme2_fp_mla_long_array_vg2_index<"fmlsl",   0b01, nxv8f16, int_aarch64_sme_fmlsl_lane_vg2x2>;
defm FMLSL_VG4_M4ZZI : sme2_fp_mla_long_array_vg4_index<"fmlsl",   0b01, nxv8f16, int_aarch64_sme_fmlsl_lane_vg2x4>;
defm FMLSL_MZZ       : sme2_mla_long_array_single<"fmlsl", 0b00,   0b01, nxv8f16, int_aarch64_sme_fmlsl_single_vg2x1>;
defm FMLSL_VG2_M2ZZ_HtoS  : sme2_fp_mla_long_array_vg2_single<"fmlsl",  0b010,  MatrixOp32, ZZ_h, ZPR4b16, nxv8f16, int_aarch64_sme_fmlsl_single_vg2x2>;
defm FMLSL_VG4_M4ZZ_HtoS  : sme2_fp_mla_long_array_vg4_single<"fmlsl",  0b010, MatrixOp32, ZZZZ_h, ZPR4b16, nxv8f16, int_aarch64_sme_fmlsl_single_vg2x4>;
defm FMLSL_VG2_M2Z2Z_HtoS : sme2_fp_mla_long_array_vg2_multi<"fmlsl",   0b001, MatrixOp32, ZZ_h_mul_r, nxv8f16, int_aarch64_sme_fmlsl_vg2x2>;
defm FMLSL_VG4_M4Z4Z_HtoS : sme2_fp_mla_long_array_vg4_multi<"fmlsl",   0b001, MatrixOp32, ZZZZ_h_mul_r, nxv8f16, int_aarch64_sme_fmlsl_vg2x4>;

defm BFMLAL_MZZI      : sme2_mla_long_array_index<"bfmlal",  0b10,   0b10, nxv8bf16, int_aarch64_sme_fmlal_lane_vg2x1>;
defm BFMLAL_VG2_M2ZZI : sme2_fp_mla_long_array_vg2_index<"bfmlal",   0b10, nxv8bf16, int_aarch64_sme_fmlal_lane_vg2x2>;
defm BFMLAL_VG4_M4ZZI : sme2_fp_mla_long_array_vg4_index<"bfmlal",   0b10, nxv8bf16, int_aarch64_sme_fmlal_lane_vg2x4>;
defm BFMLAL_MZZ       : sme2_mla_long_array_single<"bfmlal", 0b00,   0b10, nxv8bf16, int_aarch64_sme_fmlal_single_vg2x1>;
defm BFMLAL_VG2_M2ZZ_HtoS  : sme2_fp_mla_long_array_vg2_single<"bfmlal",  0b100, MatrixOp32, ZZ_h, ZPR4b16, nxv8bf16, int_aarch64_sme_fmlal_single_vg2x2>;
defm BFMLAL_VG4_M4ZZ_HtoS  : sme2_fp_mla_long_array_vg4_single<"bfmlal",  0b100, MatrixOp32, ZZZZ_h, ZPR4b16, nxv8bf16, int_aarch64_sme_fmlal_single_vg2x4>;
defm BFMLAL_VG2_M2Z2Z_HtoS : sme2_fp_mla_long_array_vg2_multi<"bfmlal",   0b010, MatrixOp32, ZZ_h_mul_r, nxv8bf16, int_aarch64_sme_fmlal_vg2x2>;
defm BFMLAL_VG4_M4Z4Z_HtoS : sme2_fp_mla_long_array_vg4_multi<"bfmlal",   0b010, MatrixOp32, ZZZZ_h_mul_r, nxv8bf16, int_aarch64_sme_fmlal_vg2x4>;

defm BFMLSL_MZZI      : sme2_mla_long_array_index<"bfmlsl",  0b10,   0b11, nxv8bf16, int_aarch64_sme_fmlsl_lane_vg2x1>;
defm BFMLSL_VG2_M2ZZI : sme2_fp_mla_long_array_vg2_index<"bfmlsl",   0b11, nxv8bf16, int_aarch64_sme_fmlsl_lane_vg2x2>;
defm BFMLSL_VG4_M4ZZI : sme2_fp_mla_long_array_vg4_index<"bfmlsl",   0b11, nxv8bf16, int_aarch64_sme_fmlsl_lane_vg2x4>;
defm BFMLSL_MZZ       : sme2_mla_long_array_single<"bfmlsl", 0b00,   0b11, nxv8bf16, int_aarch64_sme_fmlsl_single_vg2x1>;
defm BFMLSL_VG2_M2ZZ_HtoS  : sme2_fp_mla_long_array_vg2_single<"bfmlsl",  0b110, MatrixOp32, ZZ_h, ZPR4b16, nxv8bf16, int_aarch64_sme_fmlsl_single_vg2x2>;
defm BFMLSL_VG4_M4ZZ_HtoS  : sme2_fp_mla_long_array_vg4_single<"bfmlsl",  0b110, MatrixOp32, ZZZZ_h, ZPR4b16, nxv8bf16, int_aarch64_sme_fmlsl_single_vg2x4>;
defm BFMLSL_VG2_M2Z2Z_HtoS : sme2_fp_mla_long_array_vg2_multi<"bfmlsl",   0b011, MatrixOp32, ZZ_h_mul_r, nxv8bf16, int_aarch64_sme_fmlsl_vg2x2>;
defm BFMLSL_VG4_M4Z4Z_HtoS : sme2_fp_mla_long_array_vg4_multi<"bfmlsl",   0b011, MatrixOp32, ZZZZ_h_mul_r, nxv8bf16, int_aarch64_sme_fmlsl_vg2x4>;

defm SMLAL_MZZI      : sme2_mla_long_array_index<"smlal", 0b11,    0b00, nxv8i16, int_aarch64_sme_smlal_lane_vg2x1>;
defm SMLAL_VG2_M2ZZI : sme2_int_mla_long_array_vg2_index<"smlal",  0b00, int_aarch64_sme_smlal_lane_vg2x2>;
defm SMLAL_VG4_M4ZZI : sme2_int_mla_long_array_vg4_index<"smlal",  0b00, int_aarch64_sme_smlal_lane_vg2x4>;
defm SMLAL_MZZ       : sme2_mla_long_array_single<"smlal",0b01,    0b00, nxv8i16, int_aarch64_sme_smlal_single_vg2x1>;
defm SMLAL_VG2_M2ZZ  : sme2_int_mla_long_array_vg2_single<"smlal", 0b00, int_aarch64_sme_smlal_single_vg2x2>;
defm SMLAL_VG4_M4ZZ  : sme2_int_mla_long_array_vg4_single<"smlal", 0b00, int_aarch64_sme_smlal_single_vg2x4>;
defm SMLAL_VG2_M2Z2Z : sme2_int_mla_long_array_vg2_multi<"smlal",  0b00, int_aarch64_sme_smlal_vg2x2>;
defm SMLAL_VG4_M4Z4Z : sme2_int_mla_long_array_vg4_multi<"smlal",  0b00, int_aarch64_sme_smlal_vg2x4>;

defm SMLSL_MZZI      : sme2_mla_long_array_index<"smlsl", 0b11,    0b01, nxv8i16, int_aarch64_sme_smlsl_lane_vg2x1>;
defm SMLSL_VG2_M2ZZI : sme2_int_mla_long_array_vg2_index<"smlsl",  0b01, int_aarch64_sme_smlsl_lane_vg2x2>;
defm SMLSL_VG4_M4ZZI : sme2_int_mla_long_array_vg4_index<"smlsl",  0b01, int_aarch64_sme_smlsl_lane_vg2x4>;
defm SMLSL_MZZ       : sme2_mla_long_array_single<"smlsl",0b01,    0b01, nxv8i16, int_aarch64_sme_smlsl_single_vg2x1>;
defm SMLSL_VG2_M2ZZ  : sme2_int_mla_long_array_vg2_single<"smlsl", 0b01, int_aarch64_sme_smlsl_single_vg2x2>;
defm SMLSL_VG4_M4ZZ  : sme2_int_mla_long_array_vg4_single<"smlsl", 0b01, int_aarch64_sme_smlsl_single_vg2x4>;
defm SMLSL_VG2_M2Z2Z : sme2_int_mla_long_array_vg2_multi<"smlsl",  0b01, int_aarch64_sme_smlsl_vg2x2>;
defm SMLSL_VG4_M4Z4Z : sme2_int_mla_long_array_vg4_multi<"smlsl",  0b01, int_aarch64_sme_smlsl_vg2x4>;

defm UMLAL_MZZI      : sme2_mla_long_array_index<"umlal", 0b11,    0b10, nxv8i16, int_aarch64_sme_umlal_lane_vg2x1>;
defm UMLAL_VG2_M2ZZI : sme2_int_mla_long_array_vg2_index<"umlal",  0b10, int_aarch64_sme_umlal_lane_vg2x2>;
defm UMLAL_VG4_M4ZZI : sme2_int_mla_long_array_vg4_index<"umlal",  0b10, int_aarch64_sme_umlal_lane_vg2x4>;
defm UMLAL_MZZ       : sme2_mla_long_array_single<"umlal",0b01,    0b10, nxv8i16, int_aarch64_sme_umlal_single_vg2x1>;
defm UMLAL_VG2_M2ZZ  : sme2_int_mla_long_array_vg2_single<"umlal", 0b10, int_aarch64_sme_umlal_single_vg2x2>;
defm UMLAL_VG4_M4ZZ  : sme2_int_mla_long_array_vg4_single<"umlal", 0b10, int_aarch64_sme_umlal_single_vg2x4>;
defm UMLAL_VG2_M2Z2Z : sme2_int_mla_long_array_vg2_multi<"umlal",  0b10, int_aarch64_sme_umlal_vg2x2>;
defm UMLAL_VG4_M4Z4Z : sme2_int_mla_long_array_vg4_multi<"umlal",  0b10, int_aarch64_sme_umlal_vg2x4>;

defm UMLSL_MZZI      : sme2_mla_long_array_index<"umlsl", 0b11,    0b11, nxv8i16, int_aarch64_sme_umlsl_lane_vg2x1>;
defm UMLSL_VG2_M2ZZI : sme2_int_mla_long_array_vg2_index<"umlsl",  0b11, int_aarch64_sme_umlsl_lane_vg2x2>;
defm UMLSL_VG4_M4ZZI : sme2_int_mla_long_array_vg4_index<"umlsl",  0b11, int_aarch64_sme_umlsl_lane_vg2x4>;
defm UMLSL_MZZ       : sme2_mla_long_array_single<"umlsl",0b01,    0b11, nxv8i16, int_aarch64_sme_umlsl_single_vg2x1>;
defm UMLSL_VG2_M2ZZ  : sme2_int_mla_long_array_vg2_single<"umlsl", 0b11, int_aarch64_sme_umlsl_single_vg2x2>;
defm UMLSL_VG4_M4ZZ  : sme2_int_mla_long_array_vg4_single<"umlsl", 0b11, int_aarch64_sme_umlsl_single_vg2x4>;
defm UMLSL_VG2_M2Z2Z : sme2_int_mla_long_array_vg2_multi<"umlsl",  0b11, int_aarch64_sme_umlsl_vg2x2>;
defm UMLSL_VG4_M4Z4Z : sme2_int_mla_long_array_vg4_multi<"umlsl",  0b11, int_aarch64_sme_umlsl_vg2x4>;

defm FCVT_Z2Z_StoH   : sme2_cvt_vg2_single<"fcvt",   0b00000, nxv8f16, nxv4f32, int_aarch64_sve_fcvt_x2>;
defm FCVTN_Z2Z_StoH  : sme2_cvt_vg2_single<"fcvtn",  0b00001, nxv8f16, nxv4f32, int_aarch64_sve_fcvtn_x2>;
defm BFCVT_Z2Z_StoH  : sme2_cvt_vg2_single<"bfcvt",  0b10000, nxv8bf16, nxv4f32, int_aarch64_sve_bfcvt_x2>;
defm BFCVTN_Z2Z_StoH : sme2_cvt_vg2_single<"bfcvtn", 0b10001, nxv8bf16, nxv4f32, int_aarch64_sve_bfcvtn_x2>;

defm SQCVT_Z2Z_StoH  : sme2_cvt_vg2_single<"sqcvt",  0b00110, nxv8i16, nxv4i32, int_aarch64_sve_sqcvt_x2>;
defm UQCVT_Z2Z_StoH  : sme2_cvt_vg2_single<"uqcvt",  0b00111, nxv8i16, nxv4i32, int_aarch64_sve_uqcvt_x2>;
defm SQCVTU_Z2Z_StoH : sme2_cvt_vg2_single<"sqcvtu", 0b10110, nxv8i16, nxv4i32, int_aarch64_sve_sqcvtu_x2>;
defm SQCVT_Z4Z      : sme2_int_cvt_vg4_single<"sqcvt", 0b000, int_aarch64_sve_sqcvt_x4>;
defm UQCVT_Z4Z      : sme2_int_cvt_vg4_single<"uqcvt", 0b001, int_aarch64_sve_uqcvt_x4>;
defm SQCVTU_Z4Z     : sme2_int_cvt_vg4_single<"sqcvtu", 0b100, int_aarch64_sve_sqcvtu_x4>;
defm SQCVTN_Z4Z     : sme2_int_cvt_vg4_single<"sqcvtn", 0b010, int_aarch64_sve_sqcvtn_x4>;
defm SQCVTUN_Z4Z    : sme2_int_cvt_vg4_single<"sqcvtun", 0b110, int_aarch64_sve_sqcvtun_x4>;
defm UQCVTN_Z4Z     : sme2_int_cvt_vg4_single<"uqcvtn", 0b011, int_aarch64_sve_uqcvtn_x4>;

defm FCVTZS_2Z2Z_StoS : sme2_fp_cvt_vg2_multi<"fcvtzs", 0b00010>;
defm FCVTZS_4Z4Z_StoS : sme2_fp_cvt_vg4_multi<"fcvtzs", 0b0001000>;
defm FCVTZU_2Z2Z_StoS : sme2_fp_cvt_vg2_multi<"fcvtzu", 0b00011>;
defm FCVTZU_4Z4Z_StoS : sme2_fp_cvt_vg4_multi<"fcvtzu", 0b0001010>;
defm SCVTF_2Z2Z_StoS  : sme2_fp_cvt_vg2_multi<"scvtf", 0b00100>;
defm SCVTF_4Z4Z_StoS  : sme2_fp_cvt_vg4_multi<"scvtf", 0b0010000>;
defm UCVTF_2Z2Z_StoS  : sme2_fp_cvt_vg2_multi<"ucvtf", 0b00101>;
defm UCVTF_4Z4Z_StoS  : sme2_fp_cvt_vg4_multi<"ucvtf", 0b0010010>;

defm SMAX_VG2_2ZZ  : sme2_int_sve_destructive_vector_vg2_single<"smax", 0b0000000>;
defm SMAX_VG4_4ZZ  : sme2_int_sve_destructive_vector_vg4_single<"smax", 0b0000000>;
defm SMAX_VG2_2Z2Z : sme2_int_sve_destructive_vector_vg2_multi<"smax",  0b0000000>;
defm SMAX_VG4_4Z4Z : sme2_int_sve_destructive_vector_vg4_multi<"smax",  0b0000000>;

defm UMAX_VG2_2ZZ  : sme2_int_sve_destructive_vector_vg2_single<"umax", 0b0000001>;
defm UMAX_VG4_4ZZ  : sme2_int_sve_destructive_vector_vg4_single<"umax", 0b0000001>;
defm UMAX_VG2_2Z2Z : sme2_int_sve_destructive_vector_vg2_multi<"umax",  0b0000001>;
defm UMAX_VG4_4Z4Z : sme2_int_sve_destructive_vector_vg4_multi<"umax",  0b0000001>;

defm SMIN_VG2_2ZZ  : sme2_int_sve_destructive_vector_vg2_single<"smin", 0b0000010>;
defm SMIN_VG4_4ZZ  : sme2_int_sve_destructive_vector_vg4_single<"smin", 0b0000010>;
defm SMIN_VG2_2Z2Z : sme2_int_sve_destructive_vector_vg2_multi<"smin",  0b0000010>;
defm SMIN_VG4_4Z4Z : sme2_int_sve_destructive_vector_vg4_multi<"smin",  0b0000010>;

defm UMIN_VG2_2ZZ  : sme2_int_sve_destructive_vector_vg2_single<"umin", 0b0000011>;
defm UMIN_VG4_4ZZ  : sme2_int_sve_destructive_vector_vg4_single<"umin", 0b0000011>;
defm UMIN_VG2_2Z2Z : sme2_int_sve_destructive_vector_vg2_multi<"umin",  0b0000011>;
defm UMIN_VG4_4Z4Z : sme2_int_sve_destructive_vector_vg4_multi<"umin",  0b0000011>;

defm FMAX_VG2_2ZZ  : sme2_fp_sve_destructive_vector_vg2_single<"fmax", 0b0010000>;
defm FMAX_VG4_4ZZ  : sme2_fp_sve_destructive_vector_vg4_single<"fmax", 0b0010000>;
defm FMAX_VG2_2Z2Z : sme2_fp_sve_destructive_vector_vg2_multi<"fmax",  0b0010000>;
defm FMAX_VG4_4Z4Z : sme2_fp_sve_destructive_vector_vg4_multi<"fmax",  0b0010000>;

defm FMIN_VG2_2ZZ  : sme2_fp_sve_destructive_vector_vg2_single<"fmin", 0b0010001>;
defm FMIN_VG4_4ZZ  : sme2_fp_sve_destructive_vector_vg4_single<"fmin", 0b0010001>;
defm FMIN_VG2_2Z2Z : sme2_fp_sve_destructive_vector_vg2_multi<"fmin",  0b0010001>;
defm FMIN_VG4_4Z4Z : sme2_fp_sve_destructive_vector_vg4_multi<"fmin",  0b0010001>;

defm FMAXNM_VG2_2ZZ  : sme2_fp_sve_destructive_vector_vg2_single<"fmaxnm", 0b0010010>;
defm FMAXNM_VG4_4ZZ  : sme2_fp_sve_destructive_vector_vg4_single<"fmaxnm", 0b0010010>;
defm FMAXNM_VG2_2Z2Z : sme2_fp_sve_destructive_vector_vg2_multi<"fmaxnm",  0b0010010>;
defm FMAXNM_VG4_4Z4Z : sme2_fp_sve_destructive_vector_vg4_multi<"fmaxnm",  0b0010010>;

defm FMINNM_VG2_2ZZ  : sme2_fp_sve_destructive_vector_vg2_single<"fminnm", 0b0010011>;
defm FMINNM_VG4_4ZZ  : sme2_fp_sve_destructive_vector_vg4_single<"fminnm", 0b0010011>;
defm FMINNM_VG2_2Z2Z : sme2_fp_sve_destructive_vector_vg2_multi<"fminnm",  0b0010011>;
defm FMINNM_VG4_4Z4Z : sme2_fp_sve_destructive_vector_vg4_multi<"fminnm",  0b0010011>;

defm SRSHL_VG2_2ZZ  :  sme2_int_sve_destructive_vector_vg2_single<"srshl", 0b0100010>;
defm SRSHL_VG4_4ZZ  :  sme2_int_sve_destructive_vector_vg4_single<"srshl", 0b0100010>;
defm SRSHL_VG2_2Z2Z :  sme2_int_sve_destructive_vector_vg2_multi<"srshl",  0b0100010>;
defm SRSHL_VG4_4Z4Z :  sme2_int_sve_destructive_vector_vg4_multi<"srshl",  0b0100010>;

defm URSHL_VG2_2ZZ  :  sme2_int_sve_destructive_vector_vg2_single<"urshl", 0b0100011>;
defm URSHL_VG4_4ZZ  :  sme2_int_sve_destructive_vector_vg4_single<"urshl", 0b0100011>;
defm URSHL_VG2_2Z2Z :  sme2_int_sve_destructive_vector_vg2_multi<"urshl",  0b0100011>;
defm URSHL_VG4_4Z4Z :  sme2_int_sve_destructive_vector_vg4_multi<"urshl",  0b0100011>;

defm FCLAMP_VG2_2Z2Z : sme2_fp_clamp_vector_vg2_multi<"fclamp">;
defm FCLAMP_VG4_4Z4Z : sme2_fp_clamp_vector_vg4_multi<"fclamp">;

defm SCLAMP_VG2_2Z2Z : sme2_int_clamp_vector_vg2_multi<"sclamp", 0b0>;
defm SCLAMP_VG4_4Z4Z : sme2_int_clamp_vector_vg4_multi<"sclamp", 0b0>;

defm UCLAMP_VG2_2Z2Z : sme2_int_clamp_vector_vg2_multi<"uclamp", 0b1>;
defm UCLAMP_VG4_4Z4Z : sme2_int_clamp_vector_vg4_multi<"uclamp", 0b1>;

defm FDOT_VG2_M2ZZI_HtoS : sme2_multi_vec_array_vg2_index_32b<"fdot", 0b01, 0b1001, ZZ_h_mul_r, ZPR4b16, nxv8f16, int_aarch64_sme_fdot_lane_za32_vg1x2>;
defm FDOT_VG4_M4ZZI_HtoS : sme2_multi_vec_array_vg4_index_32b<"fdot", 0b1001, ZZZZ_h_mul_r, ZPR4b16, nxv8f16, int_aarch64_sme_fdot_lane_za32_vg1x4>;
defm FDOT_VG2_M2ZZ_HtoS  : sme2_dot_mla_add_sub_array_vg2_single<"fdot", 0b0010000, MatrixOp32, ZZ_h, ZPR4b16, nxv8f16, int_aarch64_sme_fdot_single_za32_vg1x2>;
defm FDOT_VG4_M4ZZ_HtoS  : sme2_dot_mla_add_sub_array_vg4_single<"fdot", 0b0110000, MatrixOp32, ZZZZ_h, ZPR4b16, nxv8f16, int_aarch64_sme_fdot_single_za32_vg1x4>;
defm FDOT_VG2_M2Z2Z_HtoS : sme2_dot_mla_add_sub_array_vg2_multi<"fdot",  0b0100000, MatrixOp32, ZZ_h_mul_r, nxv8f16, int_aarch64_sme_fdot_za32_vg1x2>;
defm FDOT_VG4_M4Z4Z_HtoS : sme2_dot_mla_add_sub_array_vg4_multi<"fdot",  0b0100000, MatrixOp32, ZZZZ_h_mul_r, nxv8f16, int_aarch64_sme_fdot_za32_vg1x4>;

defm BFDOT_VG2_M2ZZI_HtoS : sme2_multi_vec_array_vg2_index_32b<"bfdot", 0b01, 0b1011, ZZ_h_mul_r, ZPR4b16, nxv8bf16, int_aarch64_sme_fdot_lane_za32_vg1x2>;
defm BFDOT_VG4_M4ZZI_HtoS : sme2_multi_vec_array_vg4_index_32b<"bfdot", 0b1011, ZZZZ_h_mul_r, ZPR4b16, nxv8bf16, int_aarch64_sme_fdot_lane_za32_vg1x4>;
defm BFDOT_VG2_M2ZZ_HtoS  : sme2_dot_mla_add_sub_array_vg2_single<"bfdot", 0b0010010, MatrixOp32, ZZ_h, ZPR4b16, nxv8bf16, int_aarch64_sme_fdot_single_za32_vg1x2>;
defm BFDOT_VG4_M4ZZ_HtoS  : sme2_dot_mla_add_sub_array_vg4_single<"bfdot", 0b0110010, MatrixOp32, ZZZZ_h, ZPR4b16, nxv8bf16, int_aarch64_sme_fdot_single_za32_vg1x4>;
defm BFDOT_VG2_M2Z2Z_HtoS : sme2_dot_mla_add_sub_array_vg2_multi<"bfdot",  0b0100010, MatrixOp32, ZZ_h_mul_r, nxv8bf16, int_aarch64_sme_fdot_za32_vg1x2>;
defm BFDOT_VG4_M4Z4Z_HtoS : sme2_dot_mla_add_sub_array_vg4_multi<"bfdot",  0b0100010, MatrixOp32, ZZZZ_h_mul_r, nxv8bf16, int_aarch64_sme_fdot_za32_vg1x4>;

defm BFVDOT_VG2_M2ZZI_HtoS : sme2_multi_vec_array_vg2_index_32b<"bfvdot", 0b01,  0b0011, ZZ_h_mul_r, ZPR4b16, nxv8bf16, int_aarch64_sme_fvdot_lane_za32_vg1x2>;

defm FVDOT_VG2_M2ZZI_HtoS : sme2_multi_vec_array_vg2_index_32b<"fvdot", 0b01, 0b0001, ZZ_h_mul_r, ZPR4b16, nxv8f16, int_aarch64_sme_fvdot_lane_za32_vg1x2>;

defm SDOT_VG2_M2ZZI_HToS : sme2_multi_vec_array_vg2_index_32b<"sdot", 0b01,  0b1000, ZZ_h_mul_r, ZPR4b16, nxv8i16, int_aarch64_sme_sdot_lane_za32_vg1x2>;
defm SDOT_VG2_M2ZZI_BToS : sme2_multi_vec_array_vg2_index_32b<"sdot", 0b01, 0b1100, ZZ_b_mul_r, ZPR4b8, nxv16i8, int_aarch64_sme_sdot_lane_za32_vg1x2>;
defm SDOT_VG4_M4ZZI_HToS : sme2_multi_vec_array_vg4_index_32b<"sdot", 0b1000, ZZZZ_h_mul_r, ZPR4b16, nxv8i16, int_aarch64_sme_sdot_lane_za32_vg1x4>;
defm SDOT_VG4_M4ZZI_BToS : sme2_multi_vec_array_vg4_index_32b<"sdot", 0b1100, ZZZZ_b_mul_r, ZPR4b8, nxv16i8, int_aarch64_sme_sdot_lane_za32_vg1x4>;
defm SDOT_VG2_M2ZZ_HtoS  : sme2_dot_mla_add_sub_array_vg2_single<"sdot", 0b1010101, MatrixOp32, ZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_sdot_single_za32_vg1x2>;
defm SDOT_VG4_M4ZZ_HtoS  : sme2_dot_mla_add_sub_array_vg4_single<"sdot", 0b1110101, MatrixOp32, ZZZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_sdot_single_za32_vg1x4>;
defm SDOT_VG2_M2Z2Z_HtoS : sme2_dot_mla_add_sub_array_vg2_multi<"sdot", 0b1101001, MatrixOp32, ZZ_h_mul_r, nxv8i16, int_aarch64_sme_sdot_za32_vg1x2>;
defm SDOT_VG4_M4Z4Z_HtoS : sme2_dot_mla_add_sub_array_vg4_multi<"sdot", 0b1101001, MatrixOp32, ZZZZ_h_mul_r, nxv8i16, int_aarch64_sme_sdot_za32_vg1x4>;
defm SDOT_VG2_M2ZZ_BtoS : sme2_dot_mla_add_sub_array_vg2_single<"sdot", 0b0010100, MatrixOp32, ZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_sdot_single_za32_vg1x2>;
defm SDOT_VG4_M4ZZ_BtoS : sme2_dot_mla_add_sub_array_vg4_single<"sdot", 0b0110100, MatrixOp32, ZZZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_sdot_single_za32_vg1x4>;
defm SDOT_VG2_M2Z2Z_BtoS : sme2_dot_mla_add_sub_array_vg2_multi<"sdot", 0b0101000, MatrixOp32, ZZ_b_mul_r, nxv16i8, int_aarch64_sme_sdot_za32_vg1x2>;
defm SDOT_VG4_M4Z4Z_BtoS : sme2_dot_mla_add_sub_array_vg4_multi<"sdot", 0b0101000, MatrixOp32, ZZZZ_b_mul_r, nxv16i8, int_aarch64_sme_sdot_za32_vg1x4>;

defm SUDOT_VG2_M2ZZI_BToS : sme2_multi_vec_array_vg2_index_32b<"sudot", 0b01, 0b1111, ZZ_b_mul_r, ZPR4b8, nxv16i8, int_aarch64_sme_sudot_lane_za32_vg1x2>;
defm SUDOT_VG4_M4ZZI_BToS : sme2_multi_vec_array_vg4_index_32b<"sudot", 0b1111, ZZZZ_b_mul_r, ZPR4b8, nxv16i8, int_aarch64_sme_sudot_lane_za32_vg1x4>;
defm SUDOT_VG2_M2ZZ_BToS  : sme2_dot_mla_add_sub_array_vg2_single<"sudot", 0b0010111, MatrixOp32, ZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_sudot_single_za32_vg1x2>;
defm SUDOT_VG4_M4ZZ_BToS  : sme2_dot_mla_add_sub_array_vg4_single<"sudot", 0b0110111, MatrixOp32, ZZZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_sudot_single_za32_vg1x4>;

defm SVDOT_VG2_M2ZZI_HtoS : sme2_multi_vec_array_vg2_index_32b<"svdot", 0b01, 0b0100, ZZ_h_mul_r, ZPR4b16, nxv8i16, int_aarch64_sme_svdot_lane_za32_vg1x2>;
defm SVDOT_VG4_M4ZZI_BtoS : sme2_multi_vec_array_vg4_index_32b<"svdot", 0b0100, ZZZZ_b_mul_r, ZPR4b8, nxv16i8, int_aarch64_sme_svdot_lane_za32_vg1x4>;

defm SUVDOT_VG4_M4ZZI_BToS : sme2_multi_vec_array_vg4_index_32b<"suvdot", 0b0111, ZZZZ_b_mul_r, ZPR4b8, nxv16i8, int_aarch64_sme_suvdot_lane_za32_vg1x4>;

defm UDOT_VG2_M2ZZI_HToS : sme2_multi_vec_array_vg2_index_32b<"udot", 0b01, 0b1010, ZZ_h_mul_r, ZPR4b16, nxv8i16, int_aarch64_sme_udot_lane_za32_vg1x2>;
defm UDOT_VG2_M2ZZI_BToS : sme2_multi_vec_array_vg2_index_32b<"udot", 0b01, 0b1110, ZZ_b_mul_r, ZPR4b8, nxv16i8, int_aarch64_sme_udot_lane_za32_vg1x2>;
defm UDOT_VG4_M4ZZI_BtoS : sme2_multi_vec_array_vg4_index_32b<"udot", 0b1110, ZZZZ_b_mul_r, ZPR4b8, nxv16i8, int_aarch64_sme_udot_lane_za32_vg1x4>;
defm UDOT_VG4_M4ZZI_HToS : sme2_multi_vec_array_vg4_index_32b<"udot", 0b1010, ZZZZ_h_mul_r, ZPR4b16, nxv8i16, int_aarch64_sme_udot_lane_za32_vg1x4>;
defm UDOT_VG2_M2ZZ_HtoS  : sme2_dot_mla_add_sub_array_vg2_single<"udot", 0b1010111, MatrixOp32, ZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_udot_single_za32_vg1x2>;
defm UDOT_VG4_M4ZZ_HtoS  : sme2_dot_mla_add_sub_array_vg4_single<"udot", 0b1110111, MatrixOp32, ZZZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_udot_single_za32_vg1x4>;
defm UDOT_VG2_M2Z2Z_HtoS : sme2_dot_mla_add_sub_array_vg2_multi<"udot", 0b1101011, MatrixOp32, ZZ_h_mul_r, nxv8i16, int_aarch64_sme_udot_za32_vg1x2>;
defm UDOT_VG4_M4Z4Z_HtoS : sme2_dot_mla_add_sub_array_vg4_multi<"udot", 0b1101011, MatrixOp32, ZZZZ_h_mul_r, nxv8i16, int_aarch64_sme_udot_za32_vg1x4>;
defm UDOT_VG2_M2ZZ_BtoS : sme2_dot_mla_add_sub_array_vg2_single<"udot", 0b0010110, MatrixOp32, ZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_udot_single_za32_vg1x2>;
defm UDOT_VG4_M4ZZ_BtoS : sme2_dot_mla_add_sub_array_vg4_single<"udot", 0b0110110, MatrixOp32, ZZZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_udot_single_za32_vg1x4>;
defm UDOT_VG2_M2Z2Z_BtoS : sme2_dot_mla_add_sub_array_vg2_multi<"udot", 0b0101010, MatrixOp32, ZZ_b_mul_r, nxv16i8, int_aarch64_sme_udot_za32_vg1x2>;
defm UDOT_VG4_M4Z4Z_BtoS : sme2_dot_mla_add_sub_array_vg4_multi<"udot", 0b0101010, MatrixOp32, ZZZZ_b_mul_r, nxv16i8, int_aarch64_sme_udot_za32_vg1x4>;

defm USDOT_VG2_M2ZZI_BToS: sme2_multi_vec_array_vg2_index_32b<"usdot", 0b01, 0b1101, ZZ_b_mul_r, ZPR4b8, nxv16i8, int_aarch64_sme_usdot_lane_za32_vg1x2>;
defm USDOT_VG4_M4ZZI_BToS: sme2_multi_vec_array_vg4_index_32b<"usdot", 0b1101, ZZZZ_b_mul_r, ZPR4b8, nxv16i8, int_aarch64_sme_usdot_lane_za32_vg1x4>;
defm USDOT_VG2_M2ZZ_BToS : sme2_dot_mla_add_sub_array_vg2_single<"usdot",  0b0010101, MatrixOp32, ZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_usdot_single_za32_vg1x2>;
defm USDOT_VG4_M4ZZ_BToS  : sme2_dot_mla_add_sub_array_vg4_single<"usdot", 0b0110101, MatrixOp32, ZZZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_usdot_single_za32_vg1x4>;
defm USDOT_VG2_M2Z2Z_BToS : sme2_dot_mla_add_sub_array_vg2_multi<"usdot", 0b0101001, MatrixOp32, ZZ_b_mul_r, nxv16i8, int_aarch64_sme_usdot_za32_vg1x2>;
defm USDOT_VG4_M4Z4Z_BToS : sme2_dot_mla_add_sub_array_vg4_multi<"usdot", 0b0101001, MatrixOp32, ZZZZ_b_mul_r, nxv16i8, int_aarch64_sme_usdot_za32_vg1x4>;

defm USVDOT_VG4_M4ZZI_BToS : sme2_multi_vec_array_vg4_index_32b<"usvdot", 0b0101, ZZZZ_b_mul_r, ZPR4b8, nxv16i8, int_aarch64_sme_usvdot_lane_za32_vg1x4>;

defm UVDOT_VG2_M2ZZI_HtoS : sme2_multi_vec_array_vg2_index_32b<"uvdot", 0b01, 0b0110, ZZ_h_mul_r, ZPR4b16, nxv8i16, int_aarch64_sme_uvdot_lane_za32_vg1x2>;
defm UVDOT_VG4_M4ZZI_BtoS : sme2_multi_vec_array_vg4_index_32b<"uvdot", 0b0110, ZZZZ_b_mul_r, ZPR4b8, nxv16i8, int_aarch64_sme_uvdot_lane_za32_vg1x4>;

defm SMLALL_MZZI_BtoS      : sme2_mla_ll_array_index_32b<"smlall", 0b00, 0b000, int_aarch64_sme_smla_za32_lane_vg4x1>;
defm SMLALL_VG2_M2ZZI_BtoS : sme2_mla_ll_array_vg2_index_32b<"smlall", 0b00, 0b000, int_aarch64_sme_smla_za32_lane_vg4x2>;
defm SMLALL_VG4_M4ZZI_BtoS : sme2_mla_ll_array_vg4_index_32b<"smlall", 0b00, 0b0000, int_aarch64_sme_smla_za32_lane_vg4x4>;
defm SMLALL_MZZ_BtoS       : sme2_mla_ll_array_single<"smlall", 0b00000, MatrixOp32, ZPR8, ZPR4b8, nxv16i8, int_aarch64_sme_smla_za32_single_vg4x1>;
defm SMLALL_VG2_M2ZZ_BtoS  : sme2_mla_ll_array_vg2_single<"smlall", 0b00000, MatrixOp32, ZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_smla_za32_single_vg4x2>;
defm SMLALL_VG4_M4ZZ_BtoS  : sme2_mla_ll_array_vg4_single<"smlall", 0b01000, MatrixOp32, ZZZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_smla_za32_single_vg4x4>;
defm SMLALL_VG2_M2Z2Z_BtoS : sme2_mla_ll_array_vg2_multi<"smlall", 0b00000, MatrixOp32, ZZ_b_mul_r, nxv16i8, int_aarch64_sme_smla_za32_vg4x2>;
defm SMLALL_VG4_M4Z4Z_BtoS : sme2_mla_ll_array_vg4_multi<"smlall", 0b00000, MatrixOp32, ZZZZ_b_mul_r, nxv16i8, int_aarch64_sme_smla_za32_vg4x4>;

defm USMLALL_MZZI_BtoS      : sme2_mla_ll_array_index_32b<"usmlall", 0b00, 0b001, int_aarch64_sme_usmla_za32_lane_vg4x1>;
defm USMLALL_VG2_M2ZZI_BtoS : sme2_mla_ll_array_vg2_index_32b<"usmlall", 0b00, 0b100, int_aarch64_sme_usmla_za32_lane_vg4x2>;
defm USMLALL_VG4_M4ZZI_BtoS : sme2_mla_ll_array_vg4_index_32b<"usmlall", 0b00, 0b0100, int_aarch64_sme_usmla_za32_lane_vg4x4>;
defm USMLALL_MZZ_BtoS       : sme2_mla_ll_array_single<"usmlall", 0b00001, MatrixOp32, ZPR8, ZPR4b8, nxv16i8, int_aarch64_sme_usmla_za32_single_vg4x1>;
defm USMLALL_VG2_M2ZZ_BtoS  : sme2_mla_ll_array_vg2_single<"usmlall", 0b00001, MatrixOp32, ZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_usmla_za32_single_vg4x2>;
defm USMLALL_VG4_M4ZZ_BtoS  : sme2_mla_ll_array_vg4_single<"usmlall", 0b01001, MatrixOp32, ZZZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_usmla_za32_single_vg4x4>;
defm USMLALL_VG2_M2Z2Z_BtoS : sme2_mla_ll_array_vg2_multi<"usmlall", 0b00001, MatrixOp32, ZZ_b_mul_r, nxv16i8, int_aarch64_sme_usmla_za32_vg4x2>;
defm USMLALL_VG4_M4Z4Z_BtoS : sme2_mla_ll_array_vg4_multi<"usmlall", 0b00001, MatrixOp32, ZZZZ_b_mul_r, nxv16i8, int_aarch64_sme_usmla_za32_vg4x4>;

defm SMLSLL_MZZI_BtoS      : sme2_mla_ll_array_index_32b<"smlsll", 0b00, 0b010, int_aarch64_sme_smls_za32_lane_vg4x1>;
defm SMLSLL_VG2_M2ZZI_BtoS : sme2_mla_ll_array_vg2_index_32b<"smlsll", 0b00, 0b001, int_aarch64_sme_smls_za32_lane_vg4x2>;
defm SMLSLL_VG4_M4ZZI_BtoS : sme2_mla_ll_array_vg4_index_32b<"smlsll", 0b00, 0b0001, int_aarch64_sme_smls_za32_lane_vg4x4>;
defm SMLSLL_MZZ_BtoS       : sme2_mla_ll_array_single<"smlsll", 0b00010, MatrixOp32, ZPR8, ZPR4b8, nxv16i8, int_aarch64_sme_smls_za32_single_vg4x1>;
defm SMLSLL_VG2_M2ZZ_BtoS  : sme2_mla_ll_array_vg2_single<"smlsll", 0b00010, MatrixOp32, ZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_smls_za32_single_vg4x2>;
defm SMLSLL_VG4_M4ZZ_BtoS  : sme2_mla_ll_array_vg4_single<"smlsll", 0b01010, MatrixOp32, ZZZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_smls_za32_single_vg4x4>;
defm SMLSLL_VG2_M2Z2Z_BtoS : sme2_mla_ll_array_vg2_multi<"smlsll", 0b00010, MatrixOp32, ZZ_b_mul_r, nxv16i8, int_aarch64_sme_smls_za32_vg4x2>;
defm SMLSLL_VG4_M4Z4Z_BtoS : sme2_mla_ll_array_vg4_multi<"smlsll", 0b00010, MatrixOp32, ZZZZ_b_mul_r, nxv16i8, int_aarch64_sme_smls_za32_vg4x4>;

defm UMLALL_MZZI_BtoS      : sme2_mla_ll_array_index_32b<"umlall", 0b00, 0b100, int_aarch64_sme_umla_za32_lane_vg4x1>;
defm UMLALL_VG2_M2ZZI_BtoS : sme2_mla_ll_array_vg2_index_32b<"umlall", 0b00, 0b010, int_aarch64_sme_umla_za32_lane_vg4x2>;
defm UMLALL_VG4_M4ZZI_BtoS : sme2_mla_ll_array_vg4_index_32b<"umlall", 0b00, 0b0010, int_aarch64_sme_umla_za32_lane_vg4x4>;
defm UMLALL_MZZ_BtoS       : sme2_mla_ll_array_single<"umlall", 0b00100, MatrixOp32, ZPR8, ZPR4b8, nxv16i8, int_aarch64_sme_umla_za32_single_vg4x1>;
defm UMLALL_VG2_M2ZZ_BtoS  : sme2_mla_ll_array_vg2_single<"umlall", 0b00100, MatrixOp32, ZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_umla_za32_single_vg4x2>;
defm UMLALL_VG4_M4ZZ_BtoS  : sme2_mla_ll_array_vg4_single<"umlall", 0b01100, MatrixOp32, ZZZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_umla_za32_single_vg4x4>;
defm UMLALL_VG2_M2Z2Z_BtoS : sme2_mla_ll_array_vg2_multi<"umlall", 0b00100, MatrixOp32, ZZ_b_mul_r, nxv16i8, int_aarch64_sme_umla_za32_vg4x2>;
defm UMLALL_VG4_M4Z4Z_BtoS : sme2_mla_ll_array_vg4_multi<"umlall", 0b00100, MatrixOp32, ZZZZ_b_mul_r, nxv16i8, int_aarch64_sme_umla_za32_vg4x4>;

defm SUMLALL_MZZI_BtoS      : sme2_mla_ll_array_index_32b<"sumlall", 0b00, 0b101, int_aarch64_sme_sumla_za32_lane_vg4x1>;
defm SUMLALL_VG2_M2ZZI_BtoS : sme2_mla_ll_array_vg2_index_32b<"sumlall", 0b00, 0b110, int_aarch64_sme_sumla_za32_lane_vg4x2>;
defm SUMLALL_VG4_M4ZZI_BtoS : sme2_mla_ll_array_vg4_index_32b<"sumlall", 0b00, 0b0110, int_aarch64_sme_sumla_za32_lane_vg4x4>;
defm SUMLALL_VG2_M2ZZ_BtoS  : sme2_mla_ll_array_vg2_single<"sumlall", 0b00101, MatrixOp32, ZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_sumla_za32_single_vg4x2>;
defm SUMLALL_VG4_M4ZZ_BtoS  : sme2_mla_ll_array_vg4_single<"sumlall", 0b01101, MatrixOp32, ZZZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_sumla_za32_single_vg4x4>;

defm UMLSLL_MZZI_BtoS      : sme2_mla_ll_array_index_32b<"umlsll", 0b00, 0b110, int_aarch64_sme_umls_za32_lane_vg4x1>;
defm UMLSLL_VG2_M2ZZI_BtoS : sme2_mla_ll_array_vg2_index_32b<"umlsll", 0b00, 0b011, int_aarch64_sme_umls_za32_lane_vg4x2>;
defm UMLSLL_VG4_M4ZZI_BtoS : sme2_mla_ll_array_vg4_index_32b<"umlsll", 0b00, 0b0011, int_aarch64_sme_umls_za32_lane_vg4x4>;
defm UMLSLL_MZZ_BtoS       : sme2_mla_ll_array_single<"umlsll", 0b00110, MatrixOp32, ZPR8, ZPR4b8, nxv16i8, int_aarch64_sme_umls_za32_single_vg4x1>;
defm UMLSLL_VG2_M2ZZ_BtoS  : sme2_mla_ll_array_vg2_single<"umlsll", 0b00110, MatrixOp32, ZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_umls_za32_single_vg4x2>;
defm UMLSLL_VG4_M4ZZ_BtoS  : sme2_mla_ll_array_vg4_single<"umlsll", 0b01110, MatrixOp32, ZZZZ_b, ZPR4b8, nxv16i8, int_aarch64_sme_umls_za32_single_vg4x4>;
defm UMLSLL_VG2_M2Z2Z_BtoS : sme2_mla_ll_array_vg2_multi<"umlsll", 0b00110, MatrixOp32, ZZ_b_mul_r, nxv16i8, int_aarch64_sme_umls_za32_vg4x2>;
defm UMLSLL_VG4_M4Z4Z_BtoS : sme2_mla_ll_array_vg4_multi<"umlsll", 0b00110, MatrixOp32, ZZZZ_b_mul_r, nxv16i8, int_aarch64_sme_umls_za32_vg4x4>;

defm BMOPA_MPPZZ_S : sme2_int_bmopx_tile<"bmopa", 0b100, int_aarch64_sme_bmopa_za32>;
defm BMOPS_MPPZZ_S : sme2_int_bmopx_tile<"bmops", 0b101, int_aarch64_sme_bmops_za32>;

defm SMOPA_MPPZZ_HtoS : sme2_int_mopx_tile<"smopa", 0b000, int_aarch64_sme_smopa_za32>;
defm SMOPS_MPPZZ_HtoS : sme2_int_mopx_tile<"smops", 0b001, int_aarch64_sme_smops_za32>;

defm UMOPA_MPPZZ_HtoS : sme2_int_mopx_tile<"umopa", 0b100, int_aarch64_sme_umopa_za32>;
defm UMOPS_MPPZZ_HtoS : sme2_int_mopx_tile<"umops", 0b101, int_aarch64_sme_umops_za32>;

def MOVT_XTI : sme2_movt_zt_to_scalar<"movt", 0b0011111>;
def MOVT_TIX : sme2_movt_scalar_to_zt<"movt", 0b0011111>;

defm LUTI2_ZTZI  : sme2_luti2_vector_index<"luti2", int_aarch64_sme_luti2_lane_zt>;
defm LUTI2_2ZTZI : sme2_luti2_vector_vg2_index<"luti2">;
defm LUTI2_4ZTZI : sme2_luti2_vector_vg4_index<"luti2">;

defm LUTI4_ZTZI  : sme2_luti4_vector_index<"luti4", int_aarch64_sme_luti4_lane_zt>;
defm LUTI4_2ZTZI : sme2_luti4_vector_vg2_index<"luti4">;
defm LUTI4_4ZTZI : sme2_luti4_vector_vg4_index<"luti4">;

defm SUNPK_VG2_2ZZ  : sme2_unpk_vector_vg2<"sunpk", 0b0>;
defm SUNPK_VG4_4Z2Z : sme2_unpk_vector_vg4<"sunpk", 0b0>;
defm UUNPK_VG2_2ZZ  : sme2_unpk_vector_vg2<"uunpk", 0b1>;
defm UUNPK_VG4_4Z2Z : sme2_unpk_vector_vg4<"uunpk", 0b1>;

defm ZIP_VG2_2ZZZ : sme2_zip_vector_vg2<"zip", 0b0>;
defm UZP_VG2_2ZZZ : sme2_zip_vector_vg2<"uzp", 0b1>;
defm ZIP_VG4_4Z4Z : sme2_zip_vector_vg4<"zip", 0b0110000>;
defm UZP_VG4_4Z4Z : sme2_zip_vector_vg4<"uzp", 0b0110001>;
defm ZIP_VG4_4Z4Z_Q : sme2_zip_vector_vg4_Q<"zip", 0b0111000>;
defm UZP_VG4_4Z4Z_Q : sme2_zip_vector_vg4_Q<"uzp", 0b0111001>;

defm FRINTA_2Z2Z: sme2_frint_vector_vg2_multi<"frinta", 0b11000>;
defm FRINTA_4Z4Z: sme2_frint_vector_vg4_multi<"frinta", 0b1100000>;
defm FRINTM_2Z2Z: sme2_frint_vector_vg2_multi<"frintm", 0b10100>;
defm FRINTM_4Z4Z: sme2_frint_vector_vg4_multi<"frintm", 0b1010000>;
defm FRINTN_2Z2Z: sme2_frint_vector_vg2_multi<"frintn", 0b10000>;
defm FRINTN_4Z4Z: sme2_frint_vector_vg4_multi<"frintn", 0b1000000>;
defm FRINTP_2Z2Z: sme2_frint_vector_vg2_multi<"frintp", 0b10010>;
defm FRINTP_4Z4Z: sme2_frint_vector_vg4_multi<"frintp", 0b1001000>;

defm MOVA_MXI2Z   : sme2_mova_vec_to_tile_vg2_multi<"mova", int_aarch64_sme_write_hor_vg2, int_aarch64_sme_write_ver_vg2>;
defm MOVA_MXI4Z   : sme2_mova_vec_to_tile_vg4_multi<"mova", int_aarch64_sme_write_hor_vg4, int_aarch64_sme_write_ver_vg4>;
defm MOVA_2ZMXI  : sme2_mova_tile_to_vec_vg2_multi<"mova">;
defm MOVA_4ZMXI : sme2_mova_tile_to_vec_vg4_multi<"mova">;

defm MOVA_VG2_MXI2Z  : sme2_mova_vec_to_array_vg2_multi<"mova", int_aarch64_sme_write_vg1x2>;
defm MOVA_VG4_MXI4Z  : sme2_mova_vec_to_array_vg4_multi<"mova", int_aarch64_sme_write_vg1x4>;
defm MOVA_VG2_2ZMXI : sme2_mova_array_to_vec_vg2_multi<0b000, "mova">;
defm MOVA_VG4_4ZMXI : sme2_mova_array_to_vec_vg4_multi<0b1000, "mova">;

defm SQRSHR_VG2_Z2ZI : sme2_sat_shift_vector_vg2<"sqrshr", 0b0, 0b0, int_aarch64_sve_sqrshr_x2>;
defm SQRSHR_VG4_Z4ZI : sme2_sat_shift_vector_vg4<"sqrshr", 0b000, int_aarch64_sve_sqrshr_x4>;

defm UQRSHR_VG2_Z2ZI : sme2_sat_shift_vector_vg2<"uqrshr", 0b0, 0b1, int_aarch64_sve_uqrshr_x2>;
defm UQRSHR_VG4_Z4ZI : sme2_sat_shift_vector_vg4<"uqrshr", 0b001, int_aarch64_sve_uqrshr_x4>;

defm SQRSHRU_VG2_Z2ZI : sme2_sat_shift_vector_vg2<"sqrshru", 0b1, 0b0, int_aarch64_sve_sqrshru_x2>;
defm SQRSHRU_VG4_Z4ZI : sme2_sat_shift_vector_vg4<"sqrshru", 0b010, int_aarch64_sve_sqrshru_x4>;

defm SQRSHRN_VG4_Z4ZI : sme2_sat_shift_vector_vg4<"sqrshrn", 0b100, int_aarch64_sve_sqrshrn_x4>;
defm UQRSHRN_VG4_Z4ZI : sme2_sat_shift_vector_vg4<"uqrshrn", 0b101, int_aarch64_sve_uqrshrn_x4>;
defm SQRSHRUN_VG4_Z4ZI : sme2_sat_shift_vector_vg4<"sqrshrun", 0b110, int_aarch64_sve_sqrshrun_x4>;

defm SEL_VG2_2ZC2Z2Z: sme2_sel_vector_vg2<"sel">;
defm SEL_VG4_4ZC4Z4Z: sme2_sel_vector_vg4<"sel">;

def  LD1B_2Z_STRIDED     : sme2_ld_vector_vg2_multi_scalar_scalar<0b00, 0b0,    ZZ_b_strided,    GPR64shifted8, "ld1b">;
def  LD1B_4Z_STRIDED     : sme2_ld_vector_vg4_multi_scalar_scalar<0b00, 0b0,    ZZZZ_b_strided,  GPR64shifted8, "ld1b">;
defm LD1B_2Z_STRIDED_IMM : sme2_ld_vector_vg2_multi_scalar_immediate<0b00, 0b0, ZZ_b_strided,    simm4s2, "ld1b">;
defm LD1B_4Z_STRIDED_IMM : sme2_ld_vector_vg4_multi_scalar_immediate<0b00, 0b0, ZZZZ_b_strided,  simm4s4, "ld1b">;
def  LD1H_2Z_STRIDED     : sme2_ld_vector_vg2_multi_scalar_scalar<0b01, 0b0,    ZZ_h_strided,    GPR64shifted16, "ld1h">;
def  LD1H_4Z_STRIDED     : sme2_ld_vector_vg4_multi_scalar_scalar<0b01, 0b0,    ZZZZ_h_strided,  GPR64shifted16, "ld1h">;
defm LD1H_2Z_STRIDED_IMM : sme2_ld_vector_vg2_multi_scalar_immediate<0b01, 0b0, ZZ_h_strided,    simm4s2, "ld1h">;
defm LD1H_4Z_STRIDED_IMM : sme2_ld_vector_vg4_multi_scalar_immediate<0b01, 0b0, ZZZZ_h_strided,  simm4s4, "ld1h">;
def  LD1W_2Z_STRIDED     : sme2_ld_vector_vg2_multi_scalar_scalar<0b10, 0b0,    ZZ_s_strided,    GPR64shifted32, "ld1w">;
def  LD1W_4Z_STRIDED     : sme2_ld_vector_vg4_multi_scalar_scalar<0b10, 0b0,    ZZZZ_s_strided,  GPR64shifted32, "ld1w">;
defm LD1W_2Z_STRIDED_IMM : sme2_ld_vector_vg2_multi_scalar_immediate<0b10, 0b0, ZZ_s_strided,    simm4s2, "ld1w">;
defm LD1W_4Z_STRIDED_IMM : sme2_ld_vector_vg4_multi_scalar_immediate<0b10, 0b0, ZZZZ_s_strided,  simm4s4, "ld1w">;
def  LD1D_2Z_STRIDED     : sme2_ld_vector_vg2_multi_scalar_scalar<0b11, 0b0,    ZZ_d_strided,    GPR64shifted64, "ld1d">;
def  LD1D_4Z_STRIDED     : sme2_ld_vector_vg4_multi_scalar_scalar<0b11, 0b0,    ZZZZ_d_strided,  GPR64shifted64, "ld1d">;
defm LD1D_2Z_STRIDED_IMM : sme2_ld_vector_vg2_multi_scalar_immediate<0b11, 0b0, ZZ_d_strided,    simm4s2, "ld1d">;
defm LD1D_4Z_STRIDED_IMM : sme2_ld_vector_vg4_multi_scalar_immediate<0b11, 0b0, ZZZZ_d_strided,  simm4s4, "ld1d">;

def  LDNT1B_2Z_STRIDED     : sme2_ld_vector_vg2_multi_scalar_scalar<0b00, 0b1,    ZZ_b_strided,   GPR64shifted8, "ldnt1b">;
def  LDNT1B_4Z_STRIDED     : sme2_ld_vector_vg4_multi_scalar_scalar<0b00, 0b1,    ZZZZ_b_strided, GPR64shifted8, "ldnt1b">;
defm LDNT1B_2Z_STRIDED_IMM : sme2_ld_vector_vg2_multi_scalar_immediate<0b00, 0b1, ZZ_b_strided,   simm4s2, "ldnt1b">;
defm LDNT1B_4Z_STRIDED_IMM : sme2_ld_vector_vg4_multi_scalar_immediate<0b00, 0b1, ZZZZ_b_strided, simm4s4, "ldnt1b">;
def  LDNT1H_2Z_STRIDED     : sme2_ld_vector_vg2_multi_scalar_scalar<0b01, 0b1,    ZZ_h_strided,   GPR64shifted16, "ldnt1h">;
def  LDNT1H_4Z_STRIDED     : sme2_ld_vector_vg4_multi_scalar_scalar<0b01, 0b1,    ZZZZ_h_strided, GPR64shifted16, "ldnt1h">;
defm LDNT1H_2Z_STRIDED_IMM : sme2_ld_vector_vg2_multi_scalar_immediate<0b01, 0b1, ZZ_h_strided,   simm4s2, "ldnt1h">;
defm LDNT1H_4Z_STRIDED_IMM : sme2_ld_vector_vg4_multi_scalar_immediate<0b01, 0b1, ZZZZ_h_strided, simm4s4, "ldnt1h">;
def  LDNT1W_2Z_STRIDED     : sme2_ld_vector_vg2_multi_scalar_scalar<0b10, 0b1,    ZZ_s_strided,   GPR64shifted32, "ldnt1w">;
def  LDNT1W_4Z_STRIDED     : sme2_ld_vector_vg4_multi_scalar_scalar<0b10, 0b1,    ZZZZ_s_strided, GPR64shifted32, "ldnt1w">;
defm LDNT1W_2Z_STRIDED_IMM : sme2_ld_vector_vg2_multi_scalar_immediate<0b10, 0b1, ZZ_s_strided,   simm4s2, "ldnt1w">;
defm LDNT1W_4Z_STRIDED_IMM : sme2_ld_vector_vg4_multi_scalar_immediate<0b10, 0b1, ZZZZ_s_strided, simm4s4, "ldnt1w">;
def  LDNT1D_2Z_STRIDED     : sme2_ld_vector_vg2_multi_scalar_scalar<0b11, 0b1,    ZZ_d_strided,   GPR64shifted64, "ldnt1d">;
def  LDNT1D_4Z_STRIDED     : sme2_ld_vector_vg4_multi_scalar_scalar<0b11, 0b1,    ZZZZ_d_strided, GPR64shifted64, "ldnt1d">;
defm LDNT1D_2Z_STRIDED_IMM : sme2_ld_vector_vg2_multi_scalar_immediate<0b11, 0b1, ZZ_d_strided,   simm4s2, "ldnt1d">;
defm LDNT1D_4Z_STRIDED_IMM : sme2_ld_vector_vg4_multi_scalar_immediate<0b11, 0b1, ZZZZ_d_strided, simm4s4, "ldnt1d">;

def  ST1B_2Z_STRIDED     : sme2_st_vector_vg2_multi_scalar_scalar<0b00, 0b0,     ZZ_b_strided,   GPR64shifted8, "st1b">;
def  ST1B_4Z_STRIDED     : sme2_st_vector_vg4_multi_scalar_scalar<0b00, 0b0,     ZZZZ_b_strided, GPR64shifted8, "st1b">;
defm ST1B_2Z_STRIDED_IMM : sme2_st_vector_vg2_multi_scalar_immediate<0b00, 0b0,  ZZ_b_strided,   simm4s2, "st1b">;
defm ST1B_4Z_STRIDED_IMM : sme2_st_vector_vg4_multi_scalar_immediate<0b00, 0b0,  ZZZZ_b_strided, simm4s4, "st1b">;
def  ST1H_2Z_STRIDED     : sme2_st_vector_vg2_multi_scalar_scalar<0b01, 0b0,     ZZ_h_strided,   GPR64shifted16, "st1h">;
def  ST1H_4Z_STRIDED     : sme2_st_vector_vg4_multi_scalar_scalar<0b01, 0b0,     ZZZZ_h_strided, GPR64shifted16, "st1h">;
defm ST1H_2Z_STRIDED_IMM : sme2_st_vector_vg2_multi_scalar_immediate<0b01, 0b0,  ZZ_h_strided,   simm4s2, "st1h">;
defm ST1H_4Z_STRIDED_IMM : sme2_st_vector_vg4_multi_scalar_immediate<0b01, 0b0,  ZZZZ_h_strided, simm4s4, "st1h">;
def  ST1W_2Z_STRIDED     : sme2_st_vector_vg2_multi_scalar_scalar<0b10, 0b0,     ZZ_s_strided,   GPR64shifted32, "st1w">;
def  ST1W_4Z_STRIDED     : sme2_st_vector_vg4_multi_scalar_scalar<0b10, 0b0,     ZZZZ_s_strided, GPR64shifted32, "st1w">;
defm ST1W_2Z_STRIDED_IMM : sme2_st_vector_vg2_multi_scalar_immediate<0b10, 0b0,  ZZ_s_strided,   simm4s2, "st1w">;
defm ST1W_4Z_STRIDED_IMM : sme2_st_vector_vg4_multi_scalar_immediate<0b10, 0b0,  ZZZZ_s_strided, simm4s4, "st1w">;
def  ST1D_2Z_STRIDED     : sme2_st_vector_vg2_multi_scalar_scalar<0b11, 0b0,     ZZ_d_strided,   GPR64shifted64, "st1d">;
def  ST1D_4Z_STRIDED     : sme2_st_vector_vg4_multi_scalar_scalar<0b11, 0b0,     ZZZZ_d_strided, GPR64shifted64, "st1d">;
defm ST1D_2Z_STRIDED_IMM : sme2_st_vector_vg2_multi_scalar_immediate<0b11, 0b0,  ZZ_d_strided,   simm4s2, "st1d">;
defm ST1D_4Z_STRIDED_IMM : sme2_st_vector_vg4_multi_scalar_immediate<0b11, 0b0,  ZZZZ_d_strided, simm4s4, "st1d">;

def  STNT1B_2Z_STRIDED     : sme2_st_vector_vg2_multi_scalar_scalar<0b00, 0b1,    ZZ_b_strided,   GPR64shifted8, "stnt1b">;
def  STNT1B_4Z_STRIDED     : sme2_st_vector_vg4_multi_scalar_scalar<0b00, 0b1,    ZZZZ_b_strided, GPR64shifted8, "stnt1b">;
defm STNT1B_2Z_STRIDED_IMM : sme2_st_vector_vg2_multi_scalar_immediate<0b00, 0b1, ZZ_b_strided,   simm4s2, "stnt1b">;
defm STNT1B_4Z_STRIDED_IMM : sme2_st_vector_vg4_multi_scalar_immediate<0b00, 0b1, ZZZZ_b_strided, simm4s4, "stnt1b">;
def  STNT1H_2Z_STRIDED     : sme2_st_vector_vg2_multi_scalar_scalar<0b01, 0b1,    ZZ_h_strided,   GPR64shifted16, "stnt1h">;
def  STNT1H_4Z_STRIDED     : sme2_st_vector_vg4_multi_scalar_scalar<0b01, 0b1,    ZZZZ_h_strided, GPR64shifted16, "stnt1h">;
defm STNT1H_2Z_STRIDED_IMM : sme2_st_vector_vg2_multi_scalar_immediate<0b01, 0b1, ZZ_h_strided,   simm4s2, "stnt1h">;
defm STNT1H_4Z_STRIDED_IMM : sme2_st_vector_vg4_multi_scalar_immediate<0b01, 0b1, ZZZZ_h_strided, simm4s4, "stnt1h">;
def  STNT1W_2Z_STRIDED     : sme2_st_vector_vg2_multi_scalar_scalar<0b10, 0b1,    ZZ_s_strided,   GPR64shifted32, "stnt1w">;
def  STNT1W_4Z_STRIDED     : sme2_st_vector_vg4_multi_scalar_scalar<0b10, 0b1,    ZZZZ_s_strided, GPR64shifted32, "stnt1w">;
defm STNT1W_2Z_STRIDED_IMM : sme2_st_vector_vg2_multi_scalar_immediate<0b10, 0b1, ZZ_s_strided,   simm4s2, "stnt1w">;
defm STNT1W_4Z_STRIDED_IMM : sme2_st_vector_vg4_multi_scalar_immediate<0b10, 0b1, ZZZZ_s_strided, simm4s4, "stnt1w">;
def  STNT1D_2Z_STRIDED     : sme2_st_vector_vg2_multi_scalar_scalar<0b11, 0b1,    ZZ_d_strided,   GPR64shifted64, "stnt1d">;
def  STNT1D_4Z_STRIDED     : sme2_st_vector_vg4_multi_scalar_scalar<0b11, 0b1,    ZZZZ_d_strided, GPR64shifted64, "stnt1d">;
defm STNT1D_2Z_STRIDED_IMM : sme2_st_vector_vg2_multi_scalar_immediate<0b11, 0b1, ZZ_d_strided,   simm4s2, "stnt1d">;
defm STNT1D_4Z_STRIDED_IMM : sme2_st_vector_vg4_multi_scalar_immediate<0b11, 0b1, ZZZZ_d_strided, simm4s4, "stnt1d">;
} // End let Predicates = [HasSME2]


let Predicates = [HasSME2andIsNonStreamingSafe] in {
defm ZERO_T : sme2_zero_zt<"zero", 0b0001>;

defm LDR_TX : sme2_spill_fill_vector<"ldr", 0b01111100, AArch64_restore_zt>;
defm STR_TX : sme2_spill_fill_vector<"str", 0b11111100, AArch64_save_zt>;
} // End let Predicates = [HasSME2andIsNonStreamingSafe]

let Predicates = [HasSME2, HasSMEI16I64] in {
defm ADD_VG2_M2ZZ_D  : sme2_dot_mla_add_sub_array_vg2_single<"add", 0b1011010, MatrixOp64, ZZ_d, ZPR4b64, nxv2i64, int_aarch64_sme_add_write_single_za_vg1x2>;
defm ADD_VG4_M4ZZ_D  : sme2_dot_mla_add_sub_array_vg4_single<"add", 0b1111010, MatrixOp64, ZZZZ_d, ZPR4b64, nxv2i64, int_aarch64_sme_add_write_single_za_vg1x4>;
defm ADD_VG2_M2Z2Z_D : sme2_dot_mla_add_sub_array_vg2_multi<"add", 0b1110010, MatrixOp64, ZZ_d_mul_r, nxv2i64, int_aarch64_sme_add_write_za_vg1x2>;
defm ADD_VG4_M4Z4Z_D : sme2_dot_mla_add_sub_array_vg4_multi<"add", 0b1110010, MatrixOp64, ZZZZ_d_mul_r, nxv2i64, int_aarch64_sme_add_write_za_vg1x4>;

defm SUB_VG2_M2ZZ_D  : sme2_dot_mla_add_sub_array_vg2_single<"sub", 0b1011011, MatrixOp64, ZZ_d, ZPR4b64, nxv2i64, int_aarch64_sme_sub_write_single_za_vg1x2>;
defm SUB_VG4_M4ZZ_D  : sme2_dot_mla_add_sub_array_vg4_single<"sub", 0b1111011, MatrixOp64, ZZZZ_d, ZPR4b64, nxv2i64, int_aarch64_sme_sub_write_single_za_vg1x4>;
defm SUB_VG2_M2Z2Z_D : sme2_dot_mla_add_sub_array_vg2_multi<"sub", 0b1110011, MatrixOp64, ZZ_d_mul_r, nxv2i64, int_aarch64_sme_sub_write_za_vg1x2>;
defm SUB_VG4_M4Z4Z_D : sme2_dot_mla_add_sub_array_vg4_multi<"sub", 0b1110011, MatrixOp64, ZZZZ_d_mul_r, nxv2i64, int_aarch64_sme_sub_write_za_vg1x4>;

defm ADD_VG2_M2Z_D : sme2_multivec_accum_add_sub_vg2<"add", 0b1010, MatrixOp64, ZZ_d_mul_r, nxv2i64, int_aarch64_sme_add_za64_vg1x2>;
defm ADD_VG4_M4Z_D : sme2_multivec_accum_add_sub_vg4<"add", 0b1010, MatrixOp64, ZZZZ_d_mul_r, nxv2i64, int_aarch64_sme_add_za64_vg1x4>;

defm SUB_VG2_M2Z_D : sme2_multivec_accum_add_sub_vg2<"sub", 0b1011, MatrixOp64, ZZ_d_mul_r, nxv2i64, int_aarch64_sme_sub_za64_vg1x2>;
defm SUB_VG4_M4Z_D : sme2_multivec_accum_add_sub_vg4<"sub", 0b1011, MatrixOp64, ZZZZ_d_mul_r, nxv2i64, int_aarch64_sme_sub_za64_vg1x4>;

defm SDOT_VG2_M2ZZI_HtoD : sme2_multi_vec_array_vg2_index_64b<"sdot", 0b01, ZZ_h_mul_r, ZPR4b16, nxv8i16, int_aarch64_sme_sdot_lane_za64_vg1x2>;
defm SDOT_VG4_M4ZZI_HtoD : sme2_multi_vec_array_vg4_index_64b<"sdot", 0b001, ZZZZ_h_mul_r, ZPR4b16, nxv8i16, int_aarch64_sme_sdot_lane_za64_vg1x4>;
defm SDOT_VG2_M2ZZ_HtoD : sme2_dot_mla_add_sub_array_vg2_single<"sdot", 0b1010100, MatrixOp64, ZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_sdot_single_za64_vg1x2>;
defm SDOT_VG4_M4ZZ_HtoD : sme2_dot_mla_add_sub_array_vg4_single<"sdot", 0b1110100, MatrixOp64, ZZZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_sdot_single_za64_vg1x4>;
defm SDOT_VG2_M2Z2Z_HtoD : sme2_dot_mla_add_sub_array_vg2_multi<"sdot", 0b1101000, MatrixOp64, ZZ_h_mul_r, nxv8i16, int_aarch64_sme_sdot_za64_vg1x2>;
defm SDOT_VG4_M4Z4Z_HtoD : sme2_dot_mla_add_sub_array_vg4_multi<"sdot", 0b1101000, MatrixOp64, ZZZZ_h_mul_r, nxv8i16, int_aarch64_sme_sdot_za64_vg1x4>;

defm SVDOT_VG4_M4ZZI_HtoD : sme2_multi_vec_array_vg4_index_64b<"svdot", 0b101, ZZZZ_h_mul_r, ZPR4b16, nxv8i16, int_aarch64_sme_svdot_lane_za64_vg1x4>;

defm UDOT_VG2_M2ZZI_HtoD : sme2_multi_vec_array_vg2_index_64b<"udot", 0b11, ZZ_h_mul_r, ZPR4b16, nxv8i16, int_aarch64_sme_udot_lane_za64_vg1x2>;
defm UDOT_VG4_M4ZZI_HtoD : sme2_multi_vec_array_vg4_index_64b<"udot", 0b011, ZZZZ_h_mul_r, ZPR4b16, nxv8i16, int_aarch64_sme_udot_lane_za64_vg1x4>;
defm UDOT_VG2_M2ZZ_HtoD : sme2_dot_mla_add_sub_array_vg2_single<"udot", 0b1010110, MatrixOp64, ZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_udot_single_za64_vg1x2>;
defm UDOT_VG4_M4ZZ_HtoD : sme2_dot_mla_add_sub_array_vg4_single<"udot", 0b1110110, MatrixOp64, ZZZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_udot_single_za64_vg1x4>;
defm UDOT_VG2_M2Z2Z_HtoD : sme2_dot_mla_add_sub_array_vg2_multi<"udot", 0b1101010, MatrixOp64, ZZ_h_mul_r, nxv8i16, int_aarch64_sme_udot_za64_vg1x2>;
defm UDOT_VG4_M4Z4Z_HtoD : sme2_dot_mla_add_sub_array_vg4_multi<"udot", 0b1101010, MatrixOp64, ZZZZ_h_mul_r, nxv8i16, int_aarch64_sme_udot_za64_vg1x4>;

defm UVDOT_VG4_M4ZZI_HtoD : sme2_multi_vec_array_vg4_index_64b<"uvdot", 0b111, ZZZZ_h_mul_r, ZPR4b16, nxv8i16, int_aarch64_sme_uvdot_lane_za64_vg1x4>;

defm SMLALL_MZZI_HtoD      : sme2_mla_ll_array_index_64b<"smlall", 0b00, int_aarch64_sme_smla_za64_lane_vg4x1>;
defm SMLALL_VG2_M2ZZI_HtoD : sme2_mla_ll_array_vg2_index_64b<"smlall", 0b00, int_aarch64_sme_smla_za64_lane_vg4x2>;
defm SMLALL_VG4_M4ZZI_HtoD : sme2_mla_ll_array_vg4_index_64b<"smlall", 0b00, int_aarch64_sme_smla_za64_lane_vg4x4>;
defm SMLALL_MZZ_HtoD       : sme2_mla_ll_array_single<"smlall", 0b10000, MatrixOp64, ZPR16, ZPR4b16, nxv8i16, int_aarch64_sme_smla_za64_single_vg4x1>;
defm SMLALL_VG2_M2ZZ_HtoD  : sme2_mla_ll_array_vg2_single<"smlall", 0b10000, MatrixOp64, ZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_smla_za64_single_vg4x2>;
defm SMLALL_VG4_M4ZZ_HtoD  : sme2_mla_ll_array_vg4_single<"smlall", 0b11000, MatrixOp64, ZZZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_smla_za64_single_vg4x4>;
defm SMLALL_VG2_M2Z2Z_HtoD : sme2_mla_ll_array_vg2_multi<"smlall",  0b10000, MatrixOp64, ZZ_h_mul_r, nxv8i16, int_aarch64_sme_smla_za64_vg4x2>;
defm SMLALL_VG4_M4Z4Z_HtoD : sme2_mla_ll_array_vg4_multi<"smlall",  0b10000, MatrixOp64, ZZZZ_h_mul_r, nxv8i16, int_aarch64_sme_smla_za64_vg4x4>;

defm SMLSLL_MZZI_HtoD      : sme2_mla_ll_array_index_64b<"smlsll", 0b01, int_aarch64_sme_smls_za64_lane_vg4x1>;
defm SMLSLL_VG2_M2ZZI_HtoD : sme2_mla_ll_array_vg2_index_64b<"smlsll", 0b01, int_aarch64_sme_smls_za64_lane_vg4x2>;
defm SMLSLL_VG4_M4ZZI_HtoD : sme2_mla_ll_array_vg4_index_64b<"smlsll", 0b01, int_aarch64_sme_smls_za64_lane_vg4x4>;
defm SMLSLL_MZZ_HtoD       : sme2_mla_ll_array_single<"smlsll", 0b10010, MatrixOp64, ZPR16, ZPR4b16, nxv8i16, int_aarch64_sme_smls_za64_single_vg4x1>;
defm SMLSLL_VG2_M2ZZ_HtoD  : sme2_mla_ll_array_vg2_single<"smlsll", 0b10010, MatrixOp64, ZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_smls_za64_single_vg4x2>;
defm SMLSLL_VG4_M4ZZ_HtoD  : sme2_mla_ll_array_vg4_single<"smlsll", 0b11010, MatrixOp64, ZZZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_smls_za64_single_vg4x4>;
defm SMLSLL_VG2_M2Z2Z_HtoD : sme2_mla_ll_array_vg2_multi<"smlsll",  0b10010, MatrixOp64, ZZ_h_mul_r, nxv8i16, int_aarch64_sme_smls_za64_vg4x2>;
defm SMLSLL_VG4_M4Z4Z_HtoD : sme2_mla_ll_array_vg4_multi<"smlsll",  0b10010, MatrixOp64, ZZZZ_h_mul_r, nxv8i16, int_aarch64_sme_smls_za64_vg4x4>;

defm UMLALL_MZZI_HtoD      : sme2_mla_ll_array_index_64b<"umlall", 0b10, int_aarch64_sme_umla_za64_lane_vg4x1>;
defm UMLALL_VG2_M2ZZI_HtoD : sme2_mla_ll_array_vg2_index_64b<"umlall", 0b10, int_aarch64_sme_umla_za64_lane_vg4x2>;
defm UMLALL_VG4_M4ZZI_HtoD : sme2_mla_ll_array_vg4_index_64b<"umlall", 0b10, int_aarch64_sme_umla_za64_lane_vg4x4>;
defm UMLALL_MZZ_HtoD       : sme2_mla_ll_array_single<"umlall", 0b10100, MatrixOp64, ZPR16, ZPR4b16, nxv8i16, int_aarch64_sme_umla_za64_single_vg4x1>;
defm UMLALL_VG2_M2ZZ_HtoD  : sme2_mla_ll_array_vg2_single<"umlall", 0b10100, MatrixOp64, ZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_umla_za64_single_vg4x2>;
defm UMLALL_VG4_M4ZZ_HtoD  : sme2_mla_ll_array_vg4_single<"umlall", 0b11100, MatrixOp64, ZZZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_umla_za64_single_vg4x4>;
defm UMLALL_VG2_M2Z2Z_HtoD : sme2_mla_ll_array_vg2_multi<"umlall",  0b10100, MatrixOp64, ZZ_h_mul_r, nxv8i16, int_aarch64_sme_umla_za64_vg4x2>;
defm UMLALL_VG4_M4Z4Z_HtoD : sme2_mla_ll_array_vg4_multi<"umlall",  0b10100, MatrixOp64, ZZZZ_h_mul_r, nxv8i16, int_aarch64_sme_umla_za64_vg4x4>;

defm UMLSLL_MZZI_HtoD      : sme2_mla_ll_array_index_64b<"umlsll", 0b11, int_aarch64_sme_umls_za64_lane_vg4x1>;
defm UMLSLL_VG2_M2ZZI_HtoD : sme2_mla_ll_array_vg2_index_64b<"umlsll", 0b11, int_aarch64_sme_umls_za64_lane_vg4x2>;
defm UMLSLL_VG4_M4ZZI_HtoD : sme2_mla_ll_array_vg4_index_64b<"umlsll", 0b11, int_aarch64_sme_umls_za64_lane_vg4x4>;
defm UMLSLL_MZZ_HtoD       : sme2_mla_ll_array_single<"umlsll", 0b10110, MatrixOp64, ZPR16, ZPR4b16, nxv8i16, int_aarch64_sme_umls_za64_single_vg4x1>;
defm UMLSLL_VG2_M2ZZ_HtoD  : sme2_mla_ll_array_vg2_single<"umlsll", 0b10110, MatrixOp64, ZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_umls_za64_single_vg4x2>;
defm UMLSLL_VG4_M4ZZ_HtoD  : sme2_mla_ll_array_vg4_single<"umlsll", 0b11110, MatrixOp64, ZZZZ_h, ZPR4b16, nxv8i16, int_aarch64_sme_umls_za64_single_vg4x4>;
defm UMLSLL_VG2_M2Z2Z_HtoD : sme2_mla_ll_array_vg2_multi<"umlsll",  0b10110, MatrixOp64, ZZ_h_mul_r, nxv8i16, int_aarch64_sme_umls_za64_vg4x2>;
defm UMLSLL_VG4_M4Z4Z_HtoD : sme2_mla_ll_array_vg4_multi<"umlsll",  0b10110, MatrixOp64, ZZZZ_h_mul_r, nxv8i16, int_aarch64_sme_umls_za64_vg4x4>;
}

let Predicates = [HasSME2, HasSMEF64F64] in {
defm FMLA_VG2_M2ZZI_D : sme2_multi_vec_array_vg2_index_64b<"fmla", 0b00, ZZ_d_mul_r, ZPR4b64, nxv2f64, int_aarch64_sme_fmla_lane_vg1x2>;
defm FMLA_VG4_M4ZZI_D : sme2_multi_vec_array_vg4_index_64b<"fmla", 0b000, ZZZZ_d_mul_r, ZPR4b64, nxv2f64, int_aarch64_sme_fmla_lane_vg1x4>;
defm FMLA_VG2_M2ZZ_D  : sme2_dot_mla_add_sub_array_vg2_single<"fmla", 0b1011000, MatrixOp64, ZZ_d, ZPR4b64, nxv2f64, int_aarch64_sme_fmla_single_vg1x2>;
defm FMLA_VG4_M4ZZ_D  : sme2_dot_mla_add_sub_array_vg4_single<"fmla", 0b1111000, MatrixOp64, ZZZZ_d, ZPR4b64, nxv2f64, int_aarch64_sme_fmla_single_vg1x4>;
defm FMLA_VG2_M2Z2Z_D : sme2_dot_mla_add_sub_array_vg2_multi<"fmla", 0b1110000, MatrixOp64, ZZ_d_mul_r, nxv2f64, int_aarch64_sme_fmla_vg1x2>;
defm FMLA_VG4_M4Z4Z_D : sme2_dot_mla_add_sub_array_vg4_multi<"fmla", 0b1110000, MatrixOp64, ZZZZ_d_mul_r, nxv2f64, int_aarch64_sme_fmla_vg1x4>;

defm FMLS_VG2_M2ZZI_D : sme2_multi_vec_array_vg2_index_64b<"fmls", 0b10, ZZ_d_mul_r, ZPR4b64, nxv2f64, int_aarch64_sme_fmls_lane_vg1x2>;
defm FMLS_VG4_M4ZZI_D : sme2_multi_vec_array_vg4_index_64b<"fmls", 0b010, ZZZZ_d_mul_r, ZPR4b64, nxv2f64, int_aarch64_sme_fmls_lane_vg1x4>;
defm FMLS_VG2_M2ZZ_D  : sme2_dot_mla_add_sub_array_vg2_single<"fmls", 0b1011001, MatrixOp64, ZZ_d, ZPR4b64, nxv2f64, int_aarch64_sme_fmls_single_vg1x2>;
defm FMLS_VG4_M4ZZ_D  : sme2_dot_mla_add_sub_array_vg4_single<"fmls", 0b1111001, MatrixOp64, ZZZZ_d, ZPR4b64, nxv2f64, int_aarch64_sme_fmls_single_vg1x4>;
defm FMLS_VG2_M2Z2Z_D : sme2_dot_mla_add_sub_array_vg2_multi<"fmls", 0b1110001, MatrixOp64, ZZ_d_mul_r, nxv2f64, int_aarch64_sme_fmls_vg1x2>;
defm FMLS_VG4_M4Z4Z_D : sme2_dot_mla_add_sub_array_vg4_multi<"fmls", 0b1110001, MatrixOp64, ZZZZ_d_mul_r, nxv2f64, int_aarch64_sme_fmls_vg1x4>;

defm FADD_VG2_M2Z_D : sme2_multivec_accum_add_sub_vg2<"fadd", 0b1000, MatrixOp64, ZZ_d_mul_r, nxv2f64, int_aarch64_sme_add_za64_vg1x2>;
defm FADD_VG4_M4Z_D : sme2_multivec_accum_add_sub_vg4<"fadd", 0b1000, MatrixOp64, ZZZZ_d_mul_r, nxv2f64, int_aarch64_sme_add_za64_vg1x4>;

defm FSUB_VG2_M2Z_D : sme2_multivec_accum_add_sub_vg2<"fsub", 0b1001, MatrixOp64, ZZ_d_mul_r, nxv2f64, int_aarch64_sme_sub_za64_vg1x2>;
defm FSUB_VG4_M4Z_D : sme2_multivec_accum_add_sub_vg4<"fsub", 0b1001, MatrixOp64, ZZZZ_d_mul_r, nxv2f64, int_aarch64_sme_sub_za64_vg1x4>;
}

let Predicates = [HasSME2p1] in {
defm MOVAZ_ZMI  : sme2p1_movaz_tile_to_vec<"movaz", int_aarch64_sme_readz_horiz, int_aarch64_sme_readz_vert,
                                           int_aarch64_sme_readz_q_horiz, int_aarch64_sme_readz_q_vert>;
defm MOVAZ_2ZMI  : sme2p1_movaz_tile_to_vec_vg2<"movaz">;
defm MOVAZ_4ZMI  : sme2p1_movaz_tile_to_vec_vg4<"movaz">;
defm MOVAZ_VG2_2ZMXI : sme2_movaz_array_to_vec_vg2_multi<"movaz">;
defm MOVAZ_VG4_4ZMXI : sme2_movaz_array_to_vec_vg4_multi<"movaz">;

defm ZERO_MXI :  sme2p1_zero_matrix<"zero">;

defm LUTI2_S_2ZTZI : sme2p1_luti2_vector_vg2_index<"luti2">;
defm LUTI2_S_4ZTZI : sme2p1_luti2_vector_vg4_index<"luti2">;

defm LUTI4_S_2ZTZI : sme2p1_luti4_vector_vg2_index<"luti4">;
defm LUTI4_S_4ZTZI : sme2p1_luti4_vector_vg4_index<"luti4">;
}

let Predicates = [HasSMEF16F16orSMEF8F16] in {
defm FADD_VG2_M2Z_H : sme2_multivec_accum_add_sub_vg2<"fadd", 0b0100, MatrixOp16, ZZ_h_mul_r, nxv8f16, int_aarch64_sme_add_za16_vg1x2>;
defm FADD_VG4_M4Z_H : sme2_multivec_accum_add_sub_vg4<"fadd", 0b0100, MatrixOp16, ZZZZ_h_mul_r, nxv8f16,  int_aarch64_sme_add_za16_vg1x4>;
defm FSUB_VG2_M2Z_H : sme2_multivec_accum_add_sub_vg2<"fsub", 0b0101, MatrixOp16, ZZ_h_mul_r, nxv8f16,  int_aarch64_sme_sub_za16_vg1x2>;
defm FSUB_VG4_M4Z_H : sme2_multivec_accum_add_sub_vg4<"fsub", 0b0101, MatrixOp16, ZZZZ_h_mul_r, nxv8f16,  int_aarch64_sme_sub_za16_vg1x4>;

defm FMLA_VG2_M2ZZI_H : sme2p1_multi_vec_array_vg2_index_16b<"fmla", 0b00, 0b100, ZZ_h_mul_r, ZPR4b16, nxv8f16, int_aarch64_sme_fmla_lane_vg1x2>;
defm FMLA_VG4_M4ZZI_H : sme2p1_multi_vec_array_vg4_index_16b<"fmla", 0b000, ZZZZ_h_mul_r, ZPR4b16, nxv8f16, int_aarch64_sme_fmla_lane_vg1x4>;
defm FMLA_VG2_M2ZZ_H :  sme2_dot_mla_add_sub_array_vg2_single<"fmla", 0b0011100, MatrixOp16, ZZ_h, ZPR4b16, nxv8f16, int_aarch64_sme_fmla_single_vg1x2>;
defm FMLA_VG4_M4ZZ_H :  sme2_dot_mla_add_sub_array_vg4_single<"fmla", 0b0111100, MatrixOp16, ZZZZ_h, ZPR4b16, nxv8f16, int_aarch64_sme_fmla_single_vg1x4>;
defm FMLA_VG2_M2Z4Z_H : sme2_dot_mla_add_sub_array_vg2_multi<"fmla", 0b0100001, MatrixOp16, ZZ_h_mul_r, nxv8f16, int_aarch64_sme_fmla_vg1x2>;
defm FMLA_VG4_M4Z4Z_H : sme2_dot_mla_add_sub_array_vg4_multi<"fmla", 0b0100001, MatrixOp16, ZZZZ_h_mul_r, nxv8f16, int_aarch64_sme_fmla_vg1x4>;

defm FMLS_VG2_M2ZZI_H : sme2p1_multi_vec_array_vg2_index_16b<"fmls", 0b00, 0b101, ZZ_h_mul_r, ZPR4b16, nxv8f16, int_aarch64_sme_fmls_lane_vg1x2>;
defm FMLS_VG4_M4ZZI_H : sme2p1_multi_vec_array_vg4_index_16b<"fmls", 0b001, ZZZZ_h_mul_r, ZPR4b16, nxv8f16, int_aarch64_sme_fmls_lane_vg1x4>;
defm FMLS_VG2_M2ZZ_H :  sme2_dot_mla_add_sub_array_vg2_single<"fmls", 0b0011101, MatrixOp16, ZZ_h, ZPR4b16, nxv8f16, int_aarch64_sme_fmls_single_vg1x2>;
defm FMLS_VG4_M4ZZ_H :  sme2_dot_mla_add_sub_array_vg4_single<"fmls", 0b0111101, MatrixOp16, ZZZZ_h, ZPR4b16, nxv8f16, int_aarch64_sme_fmls_single_vg1x4>;
defm FMLS_VG2_M2Z2Z_H : sme2_dot_mla_add_sub_array_vg2_multi<"fmls", 0b0100011, MatrixOp16, ZZ_h_mul_r, nxv8f16, int_aarch64_sme_fmls_vg1x2>;
defm FMLS_VG4_M4Z2Z_H : sme2_dot_mla_add_sub_array_vg4_multi<"fmls", 0b0100011, MatrixOp16, ZZZZ_h_mul_r, nxv8f16, int_aarch64_sme_fmls_vg1x4>;

defm FCVT_2ZZ_H  : sme2p1_fp_cvt_vector_vg2_single<"fcvt", 0b0>;
defm FCVTL_2ZZ_H : sme2p1_fp_cvt_vector_vg2_single<"fcvtl", 0b1>;

defm FMOPA_MPPZZ_H : sme2p1_fmop_tile_fp16<"fmopa", 0b0, 0b0, nxv8f16, int_aarch64_sme_mopa>;
defm FMOPS_MPPZZ_H : sme2p1_fmop_tile_fp16<"fmops", 0b0, 0b1, nxv8f16, int_aarch64_sme_mops>;
}

let Predicates = [HasSME2, HasB16B16] in {
defm BFADD_VG2_M2Z_H : sme2_multivec_accum_add_sub_vg2<"bfadd", 0b1100, MatrixOp16, ZZ_h_mul_r, nxv8bf16,    int_aarch64_sme_add_za16_vg1x2>;
defm BFADD_VG4_M4Z_H : sme2_multivec_accum_add_sub_vg4<"bfadd", 0b1100, MatrixOp16, ZZZZ_h_mul_r, nxv8bf16,  int_aarch64_sme_add_za16_vg1x4>;
defm BFSUB_VG2_M2Z_H : sme2_multivec_accum_add_sub_vg2<"bfsub", 0b1101, MatrixOp16, ZZ_h_mul_r,  nxv8bf16,   int_aarch64_sme_sub_za16_vg1x2>;
defm BFSUB_VG4_M4Z_H : sme2_multivec_accum_add_sub_vg4<"bfsub", 0b1101, MatrixOp16, ZZZZ_h_mul_r,  nxv8bf16, int_aarch64_sme_sub_za16_vg1x4>;

defm BFMLA_VG2_M2ZZI : sme2p1_multi_vec_array_vg2_index_16b<"bfmla", 0b00, 0b110, ZZ_h_mul_r, ZPR4b16, nxv8bf16, int_aarch64_sme_fmla_lane_vg1x2>;
defm BFMLA_VG4_M4ZZI : sme2p1_multi_vec_array_vg4_index_16b<"bfmla", 0b010, ZZZZ_h_mul_r, ZPR4b16, nxv8bf16, int_aarch64_sme_fmla_lane_vg1x4>;
defm BFMLA_VG2_M2ZZ  : sme2_dot_mla_add_sub_array_vg2_single<"bfmla", 0b1011100, MatrixOp16, ZZ_h, ZPR4b16, nxv8bf16, int_aarch64_sme_fmla_single_vg1x2>;
defm BFMLA_VG4_M4ZZ  : sme2_dot_mla_add_sub_array_vg4_single<"bfmla", 0b1111100, MatrixOp16, ZZZZ_h, ZPR4b16, nxv8bf16, int_aarch64_sme_fmla_single_vg1x4>;
defm BFMLA_VG2_M2Z2Z : sme2_dot_mla_add_sub_array_vg2_multi<"bfmla", 0b1100001, MatrixOp16, ZZ_h_mul_r, nxv8bf16, int_aarch64_sme_fmla_vg1x2>;
defm BFMLA_VG4_M4Z4Z : sme2_dot_mla_add_sub_array_vg4_multi<"bfmla", 0b1100001, MatrixOp16, ZZZZ_h_mul_r, nxv8bf16, int_aarch64_sme_fmla_vg1x4>;

defm BFMLS_VG2_M2ZZI : sme2p1_multi_vec_array_vg2_index_16b<"bfmls", 0b00, 0b111, ZZ_h_mul_r, ZPR4b16, nxv8bf16, int_aarch64_sme_fmls_lane_vg1x2>;
defm BFMLS_VG4_M4ZZI : sme2p1_multi_vec_array_vg4_index_16b<"bfmls", 0b011, ZZZZ_h_mul_r, ZPR4b16, nxv8bf16, int_aarch64_sme_fmls_lane_vg1x4>;
defm BFMLS_VG2_M2ZZ  : sme2_dot_mla_add_sub_array_vg2_single<"bfmls", 0b1011101, MatrixOp16, ZZ_h, ZPR4b16, nxv8bf16, int_aarch64_sme_fmls_single_vg1x2>;
defm BFMLS_VG4_M4ZZ  : sme2_dot_mla_add_sub_array_vg4_single<"bfmls", 0b1111101, MatrixOp16, ZZZZ_h, ZPR4b16, nxv8bf16, int_aarch64_sme_fmls_single_vg1x4>;
defm BFMLS_VG2_M2Z2Z : sme2_dot_mla_add_sub_array_vg2_multi<"bfmls", 0b1100011, MatrixOp16, ZZ_h_mul_r, nxv8bf16, int_aarch64_sme_fmls_vg1x2>;
defm BFMLS_VG4_M4Z4Z : sme2_dot_mla_add_sub_array_vg4_multi<"bfmls", 0b1100011, MatrixOp16, ZZZZ_h_mul_r, nxv8bf16, int_aarch64_sme_fmls_vg1x4>;

defm BFMAX_VG2_2ZZ  : sme2p1_bf_max_min_vector_vg2_single<"bfmax", 0b0010000>;
defm BFMAX_VG4_4ZZ  : sme2p1_bf_max_min_vector_vg4_single<"bfmax", 0b0010000>;
defm BFMAX_VG2_2Z2Z : sme2p1_bf_max_min_vector_vg2_multi<"bfmax",  0b0010000>;
defm BFMAX_VG4_4Z2Z : sme2p1_bf_max_min_vector_vg4_multi<"bfmax",  0b0010000>;

defm BFMIN_VG2_2ZZ  : sme2p1_bf_max_min_vector_vg2_single<"bfmin", 0b0010001>;
defm BFMIN_VG4_4ZZ  : sme2p1_bf_max_min_vector_vg4_single<"bfmin", 0b0010001>;
defm BFMIN_VG2_2Z2Z : sme2p1_bf_max_min_vector_vg2_multi<"bfmin",  0b0010001>;
defm BFMIN_VG4_4Z2Z : sme2p1_bf_max_min_vector_vg4_multi<"bfmin",  0b0010001>;

defm BFMAXNM_VG2_2ZZ  : sme2p1_bf_max_min_vector_vg2_single<"bfmaxnm", 0b0010010>;
defm BFMAXNM_VG4_4ZZ  : sme2p1_bf_max_min_vector_vg4_single<"bfmaxnm", 0b0010010>;
defm BFMAXNM_VG2_2Z2Z : sme2p1_bf_max_min_vector_vg2_multi<"bfmaxnm",  0b0010010>;
defm BFMAXNM_VG4_4Z2Z : sme2p1_bf_max_min_vector_vg4_multi<"bfmaxnm",  0b0010010>;

defm BFMINNM_VG2_2ZZ  : sme2p1_bf_max_min_vector_vg2_single<"bfminnm", 0b0010011>;
defm BFMINNM_VG4_4ZZ  : sme2p1_bf_max_min_vector_vg4_single<"bfminnm", 0b0010011>;
defm BFMINNM_VG2_2Z2Z : sme2p1_bf_max_min_vector_vg2_multi<"bfminnm",  0b0010011>;
defm BFMINNM_VG4_4Z2Z : sme2p1_bf_max_min_vector_vg4_multi<"bfminnm",  0b0010011>;

defm BFCLAMP_VG2_2ZZZ: sme2p1_bfclamp_vector_vg2_multi<"bfclamp">;
defm BFCLAMP_VG4_4ZZZ: sme2p1_bfclamp_vector_vg4_multi<"bfclamp">;

defm BFMOPA_MPPZZ_H : sme2p1_fmop_tile_fp16<"bfmopa", 0b1, 0b0, nxv8bf16, int_aarch64_sme_mopa>;
defm BFMOPS_MPPZZ_H : sme2p1_fmop_tile_fp16<"bfmops", 0b1, 0b1, nxv8bf16, int_aarch64_sme_mops>;
}

let Predicates = [HasSME2, HasFP8] in {
defm F1CVT_2ZZ_BtoH    : sme2p1_fp8_cvt_vector_vg2_single<"f1cvt",   0b00, 0b0>;
defm F1CVTL_2ZZ_BtoH   : sme2p1_fp8_cvt_vector_vg2_single<"f1cvtl",  0b00, 0b1>;
defm BF1CVT_2ZZ_BtoH   : sme2p1_fp8_cvt_vector_vg2_single<"bf1cvt",  0b01, 0b0>;
defm BF1CVTL_2ZZ_BtoH  : sme2p1_fp8_cvt_vector_vg2_single<"bf1cvtl", 0b01, 0b1>;
defm F2CVT_2ZZ_BtoH    : sme2p1_fp8_cvt_vector_vg2_single<"f2cvt",   0b10, 0b0>;
defm F2CVTL_2ZZ_BtoH   : sme2p1_fp8_cvt_vector_vg2_single<"f2cvtl",  0b10, 0b1>;
defm BF2CVT_2ZZ_BtoH   : sme2p1_fp8_cvt_vector_vg2_single<"bf2cvt",  0b11, 0b0>;
defm BF2CVTL_2ZZ_BtoH  : sme2p1_fp8_cvt_vector_vg2_single<"bf2cvtl", 0b11, 0b1>;

defm FCVT_Z2Z_HtoB  : sme2_fp8_cvt_vg2_single<"fcvt",   0b0>;
defm BFCVT_Z2Z_HtoB : sme2_fp8_cvt_vg2_single<"bfcvt",  0b1>;
defm FCVT_Z4Z_StoB  : sme2_fp8_cvt_vg4_single<"fcvt",   0b0>;
defm FCVTN_Z4Z_StoB : sme2_fp8_cvt_vg4_single<"fcvtn",  0b1>;

defm FSCALE_2ZZ   : sme2_fp_sve_destructive_vector_vg2_single<"fscale", 0b0011000>;
defm FSCALE_4ZZ   : sme2_fp_sve_destructive_vector_vg4_single<"fscale", 0b0011000>;
defm FSCALE_2Z2Z  : sme2_fp_sve_destructive_vector_vg2_multi<"fscale",  0b0011000>;
defm FSCALE_4Z4Z  : sme2_fp_sve_destructive_vector_vg4_multi<"fscale",  0b0011000>;

} // [HasSME2, HasFP8]

let Predicates = [HasSME2, HasFAMINMAX] in {
defm FAMAX_2Z2Z : sme2_fp_sve_destructive_vector_vg2_multi<"famax", 0b0010100>;
defm FAMIN_2Z2Z : sme2_fp_sve_destructive_vector_vg2_multi<"famin", 0b0010101>;

defm FAMAX_4Z4Z : sme2_fp_sve_destructive_vector_vg4_multi<"famax", 0b0010100>;
defm FAMIN_4Z4Z : sme2_fp_sve_destructive_vector_vg4_multi<"famin", 0b0010101>;
} //[HasSME2, HasFAMINMAX]

let Predicates = [HasSME2, HasSME_LUTv2] in {
defm MOVT : sme2_movt_zt_to_zt<"movt",  0b0011111>;
def LUTI4_4ZZT2Z    : sme2_luti4_vector_vg4<0b00, 0b00,"luti4">;
} //[HasSME2, HasSME_LUTv2]

let Predicates = [HasSME2p1, HasSME_LUTv2] in {
def LUTI4_S_4ZZT2Z  : sme2_luti4_vector_vg4_strided<0b00, 0b00, "luti4">;
} //[HasSME2p1, HasSME_LUTv2]

let Predicates = [HasSMEF8F16] in {
defm FVDOT_VG2_M2ZZI_BtoH : sme2p1_multi_vec_array_vg2_index_f8f16<"fvdot", 0b11, 0b110, ZZ_b_mul_r, ZPR4b8>;
defm FDOT_VG2_M2ZZI_BtoH  : sme2p1_multi_vec_array_vg2_index_f8f16<"fdot",  0b11, 0b010, ZZ_b_mul_r, ZPR4b8>;
defm FDOT_VG4_M4ZZI_BtoH  : sme2p1_multi_vec_array_vg4_index_f8f16<"fdot",    0b100, ZZZZ_b_mul_r, ZPR4b8>;
defm FDOT_VG2_M2ZZ_BtoH   :  sme2_dot_mla_add_sub_array_vg24_single<"fdot", 0b0010001, MatrixOp16, ZZ_b, ZPR4b8>;
defm FDOT_VG4_M4ZZ_BtoH   :  sme2_dot_mla_add_sub_array_vg24_single<"fdot", 0b0110001, MatrixOp16, ZZZZ_b, ZPR4b8>;
// TODO: Replace nxv16i8 by nxv16f8
defm FDOT_VG2_M2Z2Z_BtoH  : sme2_dot_mla_add_sub_array_vg2_multi<"fdot",    0b0100100, MatrixOp16, ZZ_b_mul_r, nxv16i8, null_frag>;
defm FDOT_VG4_M4Z4Z_BtoH  : sme2_dot_mla_add_sub_array_vg4_multi<"fdot",    0b0100100, MatrixOp16, ZZZZ_b_mul_r, nxv16i8, null_frag>;

def  FMLAL_MZZI_BtoH      : sme2_mla_ll_array_index_16b<"fmlal", 0b11, 0b00>;
defm FMLAL_VG2_M2ZZI_BtoH : sme2_multi_vec_array_vg2_index_16b<"fmlal", 0b10, 0b111>;
defm FMLAL_VG4_M4ZZI_BtoH : sme2_multi_vec_array_vg4_index_16b<"fmlal", 0b10, 0b110>;
def  FMLAL_VG2_MZZ_BtoH   : sme2_mla_long_array_single_16b<"fmlal">;
// TODO: Replace nxv16i8 by nxv16f8
defm FMLAL_VG2_M2ZZ_BtoH  : sme2_fp_mla_long_array_vg2_single<"fmlal",  0b001, MatrixOp16, ZZ_b, ZPR4b8, nxv16i8, null_frag>;
defm FMLAL_VG4_M4ZZ_BtoH  :  sme2_fp_mla_long_array_vg4_single<"fmlal", 0b001, MatrixOp16, ZZZZ_b, ZPR4b8, nxv16i8, null_frag>;
defm FMLAL_VG2_M2Z2Z_BtoH : sme2_fp_mla_long_array_vg2_multi<"fmlal",   0b100, MatrixOp16, ZZ_b_mul_r, nxv16i8, null_frag>;
defm FMLAL_VG4_M4Z4Z_BtoH : sme2_fp_mla_long_array_vg4_multi<"fmlal",   0b100, MatrixOp16, ZZZZ_b_mul_r, nxv16i8, null_frag>;

defm FMOPA_MPPZZ_BtoH     : sme2p1_fmop_tile_f8f16<"fmopa", 0b1, 0b0, 0b01>;

} //[HasSMEF8F16]

let Predicates = [HasSMEF8F32] in {
// TODO : Replace nxv16i8 by nxv16f8
defm FDOT_VG2_M2ZZI_BtoS : sme2_multi_vec_array_vg2_index_32b<"fdot", 0b01, 0b0111, ZZ_b_mul_r, ZPR4b8, nxv16i8, null_frag>;
defm FDOT_VG4_M4ZZI_BtoS : sme2_multi_vec_array_vg4_index_32b<"fdot", 0b0001, ZZZZ_b_mul_r, ZPR4b8, nxv16i8, null_frag>;
defm FDOT_VG2_M2ZZ_BtoS  : sme2_dot_mla_add_sub_array_vg24_single<"fdot", 0b0010011, MatrixOp32, ZZ_b, ZPR4b8>;
defm FDOT_VG4_M4ZZ_BtoS  : sme2_dot_mla_add_sub_array_vg24_single<"fdot", 0b0110011, MatrixOp32, ZZZZ_b, ZPR4b8>;
// TODO : Replace nxv16i8 by nxv16f8
defm FDOT_VG2_M2Z2Z_BtoS : sme2_dot_mla_add_sub_array_vg2_multi<"fdot",   0b0100110, MatrixOp32, ZZ_b_mul_r, nxv16i8, null_frag>;
defm FDOT_VG4_M4Z4Z_BtoS : sme2_dot_mla_add_sub_array_vg4_multi<"fdot",   0b0100110, MatrixOp32, ZZZZ_b_mul_r, nxv16i8, null_frag>;

def FVDOTB_VG4_M2ZZI_BtoS : sme2_fp8_multi_vec_array_vg4_index<"fvdotb", 0b0>;
def FVDOTT_VG4_M2ZZI_BtoS : sme2_fp8_multi_vec_array_vg4_index<"fvdott", 0b1>;

defm FMLALL_MZZI_BtoS      : sme2_mla_ll_array_index_32b<"fmlall",     0b01, 0b000, null_frag>;
defm FMLALL_VG2_M2ZZI_BtoS : sme2_mla_ll_array_vg2_index_32b<"fmlall", 0b10, 0b100, null_frag>;
defm FMLALL_VG4_M4ZZI_BtoS : sme2_mla_ll_array_vg4_index_32b<"fmlall", 0b00, 0b1000, null_frag>;
// TODO: Replace nxv16i8 by nxv16f8
defm FMLALL_MZZ_BtoS       : sme2_mla_ll_array_single<"fmlall",      0b01000, MatrixOp32, ZPR8, ZPR4b8, nxv16i8, null_frag>;
defm FMLALL_VG2_M2ZZ_BtoS  : sme2_mla_ll_array_vg24_single<"fmlall", 0b000001, MatrixOp32, ZZ_b, ZPR4b8>;
defm FMLALL_VG4_M4ZZ_BtoS  : sme2_mla_ll_array_vg24_single<"fmlall", 0b010001, MatrixOp32, ZZZZ_b, ZPR4b8>;
defm FMLALL_VG2_M2Z2Z_BtoS : sme2_mla_ll_array_vg2_multi<"fmlall",   0b01000, MatrixOp32, ZZ_b_mul_r, nxv16i8, null_frag>;
defm FMLALL_VG4_M4Z4Z_BtoS : sme2_mla_ll_array_vg4_multi<"fmlall",   0b01000, MatrixOp32, ZZZZ_b_mul_r, nxv16i8, null_frag>;


defm FMOPA_MPPZZ_BtoS : sme_outer_product_fp32<0b0, 0b01, ZPR8, "fmopa", null_frag>;

} //[HasSMEF8F32]

