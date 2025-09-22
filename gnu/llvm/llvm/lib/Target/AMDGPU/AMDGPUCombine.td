//=- AMDGPUCombine.td - Define AMDGPU Combine Rules ----------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/GlobalISel/Combine.td"

// TODO: This really belongs after legalization after scalarization.

def fmin_fmax_legacy_matchdata : GIDefMatchData<"FMinFMaxLegacyInfo">;

let Predicates = [HasFminFmaxLegacy] in
def fcmp_select_to_fmin_fmax_legacy : GICombineRule<
  (defs root:$select, fmin_fmax_legacy_matchdata:$matchinfo),
  (match (G_FCMP $cond, $pred, $lhs, $rhs):$fcmp,
         (G_SELECT f32:$dst, $cond, $true, $false):$select,
         [{ return matchFMinFMaxLegacy(*${select}, *${fcmp}, ${matchinfo}); }]),
  (apply [{ applySelectFCmpToFMinFMaxLegacy(*${select}, ${matchinfo}); }])>;


def uchar_to_float : GICombineRule<
  (defs root:$itofp),
  (match (wip_match_opcode G_UITOFP, G_SITOFP):$itofp,
         [{ return matchUCharToFloat(*${itofp}); }]),
  (apply [{ applyUCharToFloat(*${itofp}); }])>;


def rcp_sqrt_to_rsq : GICombineRule<
  (defs root:$rcp, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_INTRINSIC, G_FSQRT):$rcp,
         [{ return matchRcpSqrtToRsq(*${rcp}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${rcp}, ${matchinfo}); }])>;

def fdiv_by_sqrt_to_rsq_f16 : GICombineRule<
  (defs root:$root),
  (match (G_FSQRT f16:$sqrt, $x, (MIFlags FmContract)),
         (G_FDIV f16:$dst, $y, $sqrt, (MIFlags FmContract)):$root,
         [{ return matchFDivSqrtToRsqF16(*${root}); }]),
  (apply [{ applyFDivSqrtToRsqF16(*${root}, ${x}.getReg()); }])>;

def cvt_f32_ubyteN_matchdata : GIDefMatchData<"CvtF32UByteMatchInfo">;

def cvt_f32_ubyteN : GICombineRule<
  (defs root:$cvt_f32_ubyteN, cvt_f32_ubyteN_matchdata:$matchinfo),
  (match (wip_match_opcode G_AMDGPU_CVT_F32_UBYTE0,
                           G_AMDGPU_CVT_F32_UBYTE1,
                           G_AMDGPU_CVT_F32_UBYTE2,
                           G_AMDGPU_CVT_F32_UBYTE3):$cvt_f32_ubyteN,
         [{ return matchCvtF32UByteN(*${cvt_f32_ubyteN}, ${matchinfo}); }]),
  (apply [{ applyCvtF32UByteN(*${cvt_f32_ubyteN}, ${matchinfo}); }])>;

def clamp_i64_to_i16_matchdata : GIDefMatchData<"ClampI64ToI16MatchInfo">;

def clamp_i64_to_i16 : GICombineRule<
  (defs root:$clamp_i64_to_i16, clamp_i64_to_i16_matchdata:$matchinfo),
  (match (wip_match_opcode G_TRUNC):$clamp_i64_to_i16,
      [{ return matchClampI64ToI16(*${clamp_i64_to_i16}, MRI, MF, ${matchinfo}); }]),
  (apply [{ applyClampI64ToI16(*${clamp_i64_to_i16}, ${matchinfo}); }])>;

def med3_matchdata : GIDefMatchData<"Med3MatchInfo">;

def int_minmax_to_med3 : GICombineRule<
  (defs root:$min_or_max, med3_matchdata:$matchinfo),
  (match (wip_match_opcode G_SMAX,
                           G_SMIN,
                           G_UMAX,
                           G_UMIN):$min_or_max,
         [{ return matchIntMinMaxToMed3(*${min_or_max}, ${matchinfo}); }]),
  (apply [{ applyMed3(*${min_or_max}, ${matchinfo}); }])>;

def fp_minmax_to_med3 : GICombineRule<
  (defs root:$min_or_max, med3_matchdata:$matchinfo),
  (match (wip_match_opcode G_FMAXNUM,
                           G_FMINNUM,
                           G_FMAXNUM_IEEE,
                           G_FMINNUM_IEEE):$min_or_max,
         [{ return matchFPMinMaxToMed3(*${min_or_max}, ${matchinfo}); }]),
  (apply [{ applyMed3(*${min_or_max}, ${matchinfo}); }])>;

def fp_minmax_to_clamp : GICombineRule<
  (defs root:$min_or_max, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_FMAXNUM,
                           G_FMINNUM,
                           G_FMAXNUM_IEEE,
                           G_FMINNUM_IEEE):$min_or_max,
         [{ return matchFPMinMaxToClamp(*${min_or_max}, ${matchinfo}); }]),
  (apply [{ applyClamp(*${min_or_max}, ${matchinfo}); }])>;

def fmed3_intrinsic_to_clamp : GICombineRule<
  (defs root:$fmed3, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_AMDGPU_FMED3):$fmed3,
         [{ return matchFPMed3ToClamp(*${fmed3}, ${matchinfo}); }]),
  (apply [{ applyClamp(*${fmed3}, ${matchinfo}); }])>;

def remove_fcanonicalize : GICombineRule<
  (defs root:$fcanonicalize, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_FCANONICALIZE):$fcanonicalize,
         [{ return matchRemoveFcanonicalize(*${fcanonicalize}, ${matchinfo}); }]),
  (apply [{ Helper.replaceSingleDefInstWithReg(*${fcanonicalize}, ${matchinfo}); }])>;

def foldable_fneg_matchdata : GIDefMatchData<"MachineInstr *">;

def foldable_fneg : GICombineRule<
  (defs root:$ffn, foldable_fneg_matchdata:$matchinfo),
  (match (wip_match_opcode G_FNEG):$ffn,
         [{ return Helper.matchFoldableFneg(*${ffn}, ${matchinfo}); }]),
  (apply [{ Helper.applyFoldableFneg(*${ffn}, ${matchinfo}); }])>;

// Detects s_mul_u64 instructions whose higher bits are zero/sign extended.
def smulu64 : GICombineRule<
  (defs root:$smul, unsigned_matchinfo:$matchinfo),
  (match (wip_match_opcode G_MUL):$smul,
         [{ return matchCombine_s_mul_u64(*${smul}, ${matchinfo}); }]),
  (apply [{ Helper.replaceOpcodeWith(*${smul}, ${matchinfo}); }])>;

def sign_exension_in_reg_matchdata : GIDefMatchData<"std::pair<MachineInstr *, unsigned>">;

def sign_extension_in_reg : GICombineRule<
  (defs root:$sign_inreg, sign_exension_in_reg_matchdata:$matchinfo),
  (match (wip_match_opcode G_SEXT_INREG):$sign_inreg,
         [{ return matchCombineSignExtendInReg(*${sign_inreg}, ${matchinfo}); }]),
  (apply [{ applyCombineSignExtendInReg(*${sign_inreg}, ${matchinfo}); }])>;


let Predicates = [Has16BitInsts, NotHasMed3_16] in {
// For gfx8, expand f16-fmed3-as-f32 into a min/max f16 sequence. This
// saves one instruction compared to the promotion.
//
// FIXME: Should have ComplexPattern like in/out matchers
//
// FIXME: We should be able to match either G_AMDGPU_FMED3 or
// G_INTRINSIC @llvm.amdgcn.fmed3. Currently the legalizer will
// replace the intrinsic with G_AMDGPU_FMED3 since we can't write a
// pattern to match it.
def expand_promoted_fmed3 : GICombineRule<
  (defs root:$fptrunc_dst),
  (match (G_FPTRUNC $fptrunc_dst, $fmed3_dst):$fptrunc,
         (G_AMDGPU_FMED3 $fmed3_dst, $src0, $src1, $src2),
    [{ return Helper.matchExpandPromotedF16FMed3(*${fptrunc}, ${src0}.getReg(), ${src1}.getReg(), ${src2}.getReg()); }]),
  (apply [{ Helper.applyExpandPromotedF16FMed3(*${fptrunc}, ${src0}.getReg(), ${src1}.getReg(), ${src2}.getReg()); }])
>;

} // End Predicates = [NotHasMed3_16]

// Combines which should only apply on SI/CI
def gfx6gfx7_combines : GICombineGroup<[fcmp_select_to_fmin_fmax_legacy]>;

// Combines which should only apply on VI
def gfx8_combines : GICombineGroup<[expand_promoted_fmed3]>;

def AMDGPUPreLegalizerCombiner: GICombiner<
  "AMDGPUPreLegalizerCombinerImpl",
  [all_combines, clamp_i64_to_i16, foldable_fneg]> {
  let CombineAllMethodName = "tryCombineAllImpl";
}

def AMDGPUPostLegalizerCombiner: GICombiner<
  "AMDGPUPostLegalizerCombinerImpl",
  [all_combines, gfx6gfx7_combines, gfx8_combines,
   uchar_to_float, cvt_f32_ubyteN, remove_fcanonicalize, foldable_fneg,
   rcp_sqrt_to_rsq, fdiv_by_sqrt_to_rsq_f16, sign_extension_in_reg, smulu64]> {
  let CombineAllMethodName = "tryCombineAllImpl";
}

def AMDGPURegBankCombiner : GICombiner<
  "AMDGPURegBankCombinerImpl",
  [unmerge_merge, unmerge_cst, unmerge_undef,
   zext_trunc_fold, int_minmax_to_med3, ptr_add_immed_chain,
   fp_minmax_to_clamp, fp_minmax_to_med3, fmed3_intrinsic_to_clamp]> {
}
