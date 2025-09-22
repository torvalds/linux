//===-- VOP3PInstructions.td - Vector Instruction Definitions -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// VOP3P Classes
//===----------------------------------------------------------------------===//

class VOP3P_Profile<VOPProfile P, VOP3Features Features = VOP3_REGULAR,
                    bit HasDPP = 0> : VOP3_Profile<P, Features> {
  let IsVOP3P = 1;
  let HasExtVOP3DPP = HasDPP;
  // We do not want to print src modifiers for vop3p because the bits are
  // overloaded in meaning and the logic in printOperandAndFPInputMods is
  // wrong for vop3p
  let AsmVOP3Base = AsmVOP3P;
}

// Used for FMA_MIX* and MAD_MIX* insts
// Their operands are only sort of f16 operands. Depending on
// op_sel_hi, these may be interpreted as f32. The inline immediate
// values are really f16 converted to f32, so we treat these as f16
// operands.
class VOP3P_Mix_Profile<VOPProfile P, VOP3Features Features = VOP3_REGULAR,
                    bit useTiedOutput = 0> : VOP3P_Profile<P, Features, 1> {
    bit UseTiedOutput = useTiedOutput;

    dag srcs =
          (ins FP16InputMods:$src0_modifiers, VCSrc_f16:$src0,
               FP16InputMods:$src1_modifiers, VCSrc_f16:$src1,
               FP16InputMods:$src2_modifiers, VCSrc_f16:$src2);
    dag dpp_srcs =
          (ins FPVRegInputMods:$src0_modifiers, VGPRSrc_32:$src0,
               FPVRegInputMods:$src1_modifiers, VRegSrc_32:$src1,
               FP16InputMods:$src2_modifiers, VCSrc_f16:$src2);

           // FIXME: Clamp0 misbehaves with the non-default vdst_in
           // following it. For now workaround this by requiring clamp
           // in tied patterns. This should use undef_tied_input, but it
           // seems underdeveloped and doesn't apply the right register
           // class constraints.
    dag mods = !con(!if(UseTiedOutput, (ins Clamp:$clamp, VGPR_32:$vdst_in),
                        (ins Clamp0:$clamp)),
                    (ins op_sel0:$op_sel, op_sel_hi0:$op_sel_hi));
    // We use Ins64 because that is the one which populates InOperandList
    // due to the logic in class VOP3_Pseudo
    let Ins64 = !con(srcs, mods);
    let InsVOP3Base = !con(dpp_srcs, mods);
    let AsmVOP3Base =
      "$vdst, $src0_modifiers, $src1_modifiers, $src2_modifiers$op_sel$op_sel_hi$clamp";
}

multiclass VOP3PInst<string OpName, VOPProfile P,
                     SDPatternOperator node = null_frag, bit IsDOT = 0> {
  def NAME : VOP3P_Pseudo<OpName, P,
                          !if (P.HasModifiers,
                               getVOP3PModPat<P, node, IsDOT, IsDOT>.ret,
                               getVOP3Pat<P, node>.ret)>;
  let SubtargetPredicate = isGFX11Plus in {
  if P.HasExtVOP3DPP then
    def _dpp : VOP3_DPP_Pseudo<OpName, P> {
      let VOP3P = 1;
      let PseudoInstr = OpName #"_dpp";
    }
  } // end SubtargetPredicate = isGFX11Plus
}

// Non-packed instructions that use the VOP3P encoding.
// VOP3 neg/abs and VOP3P opsel/opsel_hi modifiers are allowed.
multiclass VOP3_VOP3PInst<string OpName, VOP3P_Mix_Profile P> {
  def NAME : VOP3P_Pseudo<OpName, P> {
    let Constraints = !if(P.UseTiedOutput, "$vdst = $vdst_in", "");
    let DisableEncoding = !if(P.UseTiedOutput, "$vdst_in", "");
  }
  let SubtargetPredicate = isGFX11Plus in {
    if P.HasExtVOP3DPP then
      def _dpp : VOP3_DPP_Pseudo<OpName, P> {
        let VOP3P = 1;
        let PseudoInstr = OpName#"_dpp";
        let Constraints = !if(P.UseTiedOutput, "$vdst = $vdst_in", "");
        let DisableEncoding = !if(P.UseTiedOutput, "$vdst_in", "");
      }
  } // end SubtargetPredicate = isGFX11Plus
}

let isReMaterializable = 1 in {
let isCommutable = 1 in {
defm V_PK_MAD_I16 : VOP3PInst<"v_pk_mad_i16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16_V2I16>>;
defm V_PK_MAD_U16 : VOP3PInst<"v_pk_mad_u16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16_V2I16>, imad>;

let FPDPRounding = 1 in {
defm V_PK_FMA_F16 : VOP3PInst<"v_pk_fma_f16", VOP3P_Profile<VOP_V2F16_V2F16_V2F16_V2F16>, any_fma>;
defm V_PK_ADD_F16 : VOP3PInst<"v_pk_add_f16", VOP3P_Profile<VOP_V2F16_V2F16_V2F16>, any_fadd>;
defm V_PK_MUL_F16 : VOP3PInst<"v_pk_mul_f16", VOP3P_Profile<VOP_V2F16_V2F16_V2F16>, any_fmul>;
} // End FPDPRounding = 1
defm V_PK_MAX_F16 : VOP3PInst<"v_pk_max_f16", VOP3P_Profile<VOP_V2F16_V2F16_V2F16>, fmaxnum_like>;
defm V_PK_MIN_F16 : VOP3PInst<"v_pk_min_f16", VOP3P_Profile<VOP_V2F16_V2F16_V2F16>, fminnum_like>;

defm V_PK_ADD_U16 : VOP3PInst<"v_pk_add_u16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16>, add>;
defm V_PK_ADD_I16 : VOP3PInst<"v_pk_add_i16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16>>;
defm V_PK_MUL_LO_U16 : VOP3PInst<"v_pk_mul_lo_u16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16>, mul>;

defm V_PK_MIN_I16 : VOP3PInst<"v_pk_min_i16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16>, smin>;
defm V_PK_MIN_U16 : VOP3PInst<"v_pk_min_u16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16>, umin>;
defm V_PK_MAX_I16 : VOP3PInst<"v_pk_max_i16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16>, smax>;
defm V_PK_MAX_U16 : VOP3PInst<"v_pk_max_u16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16>, umax>;

let SubtargetPredicate = isGFX12Plus, ReadsModeReg = 0 in {
defm V_PK_MAXIMUM_F16 : VOP3PInst<"v_pk_maximum_f16", VOP3P_Profile<VOP_V2F16_V2F16_V2F16, VOP3_PACKED>, fmaximum>;
defm V_PK_MINIMUM_F16 : VOP3PInst<"v_pk_minimum_f16", VOP3P_Profile<VOP_V2F16_V2F16_V2F16, VOP3_PACKED>, fminimum>;
} // End SubtargetPredicate = isGFX12Plus, ReadsModeReg = 0
}

defm V_PK_SUB_U16 : VOP3PInst<"v_pk_sub_u16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16>>;
defm V_PK_SUB_I16 : VOP3PInst<"v_pk_sub_i16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16>, sub>;

defm V_PK_LSHLREV_B16 : VOP3PInst<"v_pk_lshlrev_b16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16>, clshl_rev_16>;
defm V_PK_ASHRREV_I16 : VOP3PInst<"v_pk_ashrrev_i16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16>, cashr_rev_16>;
defm V_PK_LSHRREV_B16 : VOP3PInst<"v_pk_lshrrev_b16", VOP3P_Profile<VOP_V2I16_V2I16_V2I16>, clshr_rev_16>;
} // End isReMaterializable = 1

let SubtargetPredicate = HasVOP3PInsts in {

// Integer operations with clamp bit set.
class VOP3PSatPat<SDPatternOperator pat, Instruction inst> : GCNPat<
  (pat (v2i16 (VOP3PMods v2i16:$src0, i32:$src0_modifiers)),
       (v2i16 (VOP3PMods v2i16:$src1, i32:$src1_modifiers))),
  (inst $src0_modifiers, $src0, $src1_modifiers, $src1, DSTCLAMP.ENABLE)
>;

def : VOP3PSatPat<uaddsat, V_PK_ADD_U16>;
def : VOP3PSatPat<saddsat, V_PK_ADD_I16>;
def : VOP3PSatPat<usubsat, V_PK_SUB_U16>;
def : VOP3PSatPat<ssubsat, V_PK_SUB_I16>;
} // End SubtargetPredicate = HasVOP3PInsts

// TODO: Make sure we're doing the right thing with denormals. Note
// that FMA and MAD will differ.
multiclass MadFmaMixPats<SDPatternOperator fma_like,
                         Instruction mix_inst,
                         Instruction mixlo_inst,
                         Instruction mixhi_inst> {
  // At least one of the operands needs to be an fpextend of an f16
  // for this to be worthwhile, so we need three patterns here.
  // TODO: Could we use a predicate to inspect src1/2/3 instead?
  def : GCNPat <
    (f32 (fma_like (f32 (VOP3PMadMixModsExt f16:$src0, i32:$src0_mods)),
                   (f32 (VOP3PMadMixMods f16:$src1, i32:$src1_mods)),
                   (f32 (VOP3PMadMixMods f16:$src2, i32:$src2_mods)))),
    (mix_inst $src0_mods, $src0, $src1_mods, $src1, $src2_mods, $src2,
              DSTCLAMP.NONE)>;
  def : GCNPat <
    (f32 (fma_like (f32 (VOP3PMadMixMods f16:$src0, i32:$src0_mods)),
                   (f32 (VOP3PMadMixModsExt f16:$src1, i32:$src1_mods)),
                   (f32 (VOP3PMadMixMods f32:$src2, i32:$src2_mods)))),
    (mix_inst $src0_mods, $src0, $src1_mods, $src1, $src2_mods, $src2,
              DSTCLAMP.NONE)>;
  def : GCNPat <
    (f32 (fma_like (f32 (VOP3PMadMixMods f16:$src0, i32:$src0_mods)),
                   (f32 (VOP3PMadMixMods f32:$src1, i32:$src1_mods)),
                   (f32 (VOP3PMadMixModsExt f16:$src2, i32:$src2_mods)))),
    (mix_inst $src0_mods, $src0, $src1_mods, $src1, $src2_mods, $src2,
              DSTCLAMP.NONE)>;

  def : GCNPat <
    (f16 (fpround (fma_like (f32 (VOP3PMadMixMods f16:$src0, i32:$src0_modifiers)),
                            (f32 (VOP3PMadMixMods f16:$src1, i32:$src1_modifiers)),
                            (f32 (VOP3PMadMixMods f16:$src2, i32:$src2_modifiers))))),
    (mixlo_inst $src0_modifiers, $src0,
                $src1_modifiers, $src1,
                $src2_modifiers, $src2,
                DSTCLAMP.NONE,
                (i32 (IMPLICIT_DEF)))
  >;

  // FIXME: Special case handling for maxhi (especially for clamp)
  // because dealing with the write to high half of the register is
  // difficult.
  def : GCNPat <
    (build_vector f16:$elt0, (f16 (fpround (fma_like (f32 (VOP3PMadMixMods f16:$src0, i32:$src0_modifiers)),
                                                     (f32 (VOP3PMadMixMods f16:$src1, i32:$src1_modifiers)),
                                                     (f32 (VOP3PMadMixMods f16:$src2, i32:$src2_modifiers)))))),
    (v2f16 (mixhi_inst $src0_modifiers, $src0,
                       $src1_modifiers, $src1,
                       $src2_modifiers, $src2,
                       DSTCLAMP.NONE,
                       VGPR_32:$elt0))
  >;

  def : GCNPat <
    (build_vector
      f16:$elt0,
      (AMDGPUclamp (f16 (fpround (fma_like (f32 (VOP3PMadMixMods f16:$src0, i32:$src0_modifiers)),
                                      (f32 (VOP3PMadMixMods f16:$src1, i32:$src1_modifiers)),
                                      (f32 (VOP3PMadMixMods f16:$src2, i32:$src2_modifiers))))))),
    (v2f16 (mixhi_inst $src0_modifiers, $src0,
                       $src1_modifiers, $src1,
                       $src2_modifiers, $src2,
                       DSTCLAMP.ENABLE,
                       VGPR_32:$elt0))
  >;

  def : GCNPat <
    (AMDGPUclamp (build_vector
      (f16 (fpround (fma_like (f32 (VOP3PMadMixMods f16:$lo_src0, i32:$lo_src0_modifiers)),
                         (f32 (VOP3PMadMixMods f16:$lo_src1, i32:$lo_src1_modifiers)),
                         (f32 (VOP3PMadMixMods f16:$lo_src2, i32:$lo_src2_modifiers))))),
      (f16 (fpround (fma_like (f32 (VOP3PMadMixMods f16:$hi_src0, i32:$hi_src0_modifiers)),
                         (f32 (VOP3PMadMixMods f16:$hi_src1, i32:$hi_src1_modifiers)),
                         (f32 (VOP3PMadMixMods f16:$hi_src2, i32:$hi_src2_modifiers))))))),
    (v2f16 (mixhi_inst $hi_src0_modifiers, $hi_src0,
                       $hi_src1_modifiers, $hi_src1,
                       $hi_src2_modifiers, $hi_src2,
                       DSTCLAMP.ENABLE,
                       (mixlo_inst $lo_src0_modifiers, $lo_src0,
                                   $lo_src1_modifiers, $lo_src1,
                                   $lo_src2_modifiers, $lo_src2,
                                   DSTCLAMP.ENABLE,
                                   (i32 (IMPLICIT_DEF)))))
  >;

  def : GCNPat <
    (f16 (fpround (fmul (f32 (VOP3PMadMixMods f32:$src0, i32:$src0_modifiers)),
                        (f32 (VOP3PMadMixMods f32:$src1, i32:$src1_modifiers))))),
    (mixlo_inst $src0_modifiers, $src0,
                $src1_modifiers, $src1,
                (i32 0), (i32 0),
                DSTCLAMP.NONE,
                (i32 (IMPLICIT_DEF)))
  >;

  def : GCNPat <
    (build_vector f16:$elt0, (f16 (fpround (fmul (f32 (VOP3PMadMixMods f32:$src0, i32:$src0_modifiers)),
                                            (f32 (VOP3PMadMixMods f32:$src1, i32:$src1_modifiers)))))),
    (v2f16 (mixhi_inst $src0_modifiers, $src0,
                       $src1_modifiers, $src1,
                       (i32 0), (i32 0),
                       DSTCLAMP.NONE,
                       VGPR_32:$elt0))
  >;
}

let SubtargetPredicate = HasMadMixInsts, OtherPredicates = [NoFP32Denormals] in {

// These are VOP3a-like opcodes which accept no omod.
// Size of src arguments (16/32) is controlled by op_sel.
// For 16-bit src arguments their location (hi/lo) are controlled by op_sel_hi.
let isCommutable = 1, mayRaiseFPException = 0 in {
let isReMaterializable = 1 in
defm V_MAD_MIX_F32 : VOP3_VOP3PInst<"v_mad_mix_f32", VOP3P_Mix_Profile<VOP_F32_F16_F16_F16, VOP3_OPSEL>>;

let FPDPRounding = 1 in {
// Clamp modifier is applied after conversion to f16.
defm V_MAD_MIXLO_F16 : VOP3_VOP3PInst<"v_mad_mixlo_f16", VOP3P_Mix_Profile<VOP_F16_F16_F16_F16, VOP3_OPSEL, 1>>;

let ClampLo = 0, ClampHi = 1 in {
defm V_MAD_MIXHI_F16 : VOP3_VOP3PInst<"v_mad_mixhi_f16", VOP3P_Mix_Profile<VOP_F16_F16_F16_F16, VOP3_OPSEL, 1>>;
}
} // End FPDPRounding = 1
}

defm : MadFmaMixPats<fmad, V_MAD_MIX_F32, V_MAD_MIXLO_F16, V_MAD_MIXHI_F16>;
} // End SubtargetPredicate = HasMadMixInsts, OtherPredicates = [NoFP32Denormals]


// Essentially the same as the mad_mix versions
let SubtargetPredicate = HasFmaMixInsts in {
let isCommutable = 1 in {

let isReMaterializable = 1 in
defm V_FMA_MIX_F32 : VOP3_VOP3PInst<"v_fma_mix_f32", VOP3P_Mix_Profile<VOP_F32_F16_F16_F16, VOP3_OPSEL>>;

let FPDPRounding = 1 in {
// Clamp modifier is applied after conversion to f16.
defm V_FMA_MIXLO_F16 : VOP3_VOP3PInst<"v_fma_mixlo_f16", VOP3P_Mix_Profile<VOP_F16_F16_F16_F16, VOP3_OPSEL, 1>>;

let ClampLo = 0, ClampHi = 1 in {
defm V_FMA_MIXHI_F16 : VOP3_VOP3PInst<"v_fma_mixhi_f16", VOP3P_Mix_Profile<VOP_F16_F16_F16_F16, VOP3_OPSEL, 1>>;
}
} // End FPDPRounding = 1
}

defm : MadFmaMixPats<fma, V_FMA_MIX_F32, V_FMA_MIXLO_F16, V_FMA_MIXHI_F16>;
}

// Defines patterns that extract signed 4bit from each Idx[0].
foreach Idx = [[0,28],[4,24],[8,20],[12,16],[16,12],[20,8],[24,4]] in
  def ExtractSigned4bit_#Idx[0] : PatFrag<(ops node:$src),
                                          (sra (shl node:$src, (i32 Idx[1])), (i32 28))>;

// Defines code pattern that extracts U(unsigned/signed) 4/8bit from FromBitIndex.
class Extract<int FromBitIndex, int BitMask, bit U>: PatFrag<
  (ops node:$src),
  !if (!or (!and (!eq (BitMask, 255), !eq (FromBitIndex, 24)), !eq (FromBitIndex, 28)), // last element
       !if (U, (srl node:$src, (i32 FromBitIndex)), (sra node:$src, (i32 FromBitIndex))),
       !if (!eq (FromBitIndex, 0), // first element
            !if (U, (and node:$src, (i32 BitMask)),
                 !if (!eq (BitMask, 15), (!cast<PatFrag>("ExtractSigned4bit_"#FromBitIndex) node:$src),
                                         (sext_inreg node:$src, i8))),
            !if (U, (and (srl node:$src, (i32 FromBitIndex)), (i32 BitMask)),
                 !if (!eq (BitMask, 15), (!cast<PatFrag>("ExtractSigned4bit_"#FromBitIndex) node:$src),
                      (sext_inreg (srl node:$src, (i32 FromBitIndex)), i8)))))>;


foreach Type = ["I", "U"] in
  foreach Index = 0-3 in {
    // Defines patterns that extract each Index'ed 8bit from an unsigned
    // 32bit scalar value;
    def Type#Index#"_8bit" : Extract<!shl(Index, 3), 255, !eq (Type, "U")>;

    // Defines multiplication patterns where the multiplication is happening on each
    // Index'ed 8bit of a 32bit scalar value.

    def Mul#Type#_Elt#Index : PatFrag<
      (ops node:$src0, node:$src1),
      (!cast<HasOneUseBinOp>(!if (!eq (Type, "I"), AMDGPUmul_i24_oneuse, AMDGPUmul_u24_oneuse))
                            (!cast<Extract>(Type#Index#"_8bit") node:$src0),
                            (!cast<Extract>(Type#Index#"_8bit") node:$src1))>;
  }

// Different variants of dot8 patterns cause a huge increase in the compile time.
// Define non-associative/commutative add/mul to prevent permutation in the dot8
// pattern.
def NonACAdd        : SDNode<"ISD::ADD"       , SDTIntBinOp>;
def NonACAdd_oneuse : HasOneUseBinOp<NonACAdd>;

def NonACAMDGPUmul_u24        : SDNode<"AMDGPUISD::MUL_U24"       , SDTIntBinOp>;
def NonACAMDGPUmul_u24_oneuse : HasOneUseBinOp<NonACAMDGPUmul_u24>;

def NonACAMDGPUmul_i24        : SDNode<"AMDGPUISD::MUL_I24"       , SDTIntBinOp>;
def NonACAMDGPUmul_i24_oneuse : HasOneUseBinOp<NonACAMDGPUmul_i24>;

foreach Type = ["I", "U"] in
  foreach Index = 0-7 in {
    // Defines patterns that extract each Index'ed 4bit from an unsigned
    // 32bit scalar value;
    def Type#Index#"_4bit" : Extract<!shl(Index, 2), 15, !eq (Type, "U")>;

    // Defines multiplication patterns where the multiplication is happening on each
    // Index'ed 8bit of a 32bit scalar value.
    def Mul#Type#Index#"_4bit" : PatFrag<
      (ops node:$src0, node:$src1),
      (!cast<HasOneUseBinOp>(!if (!eq (Type, "I"), NonACAMDGPUmul_i24_oneuse, NonACAMDGPUmul_u24_oneuse))
                             (!cast<Extract>(Type#Index#"_4bit") node:$src0),
                             (!cast<Extract>(Type#Index#"_4bit") node:$src1))>;
  }

class UDot2Pat<VOP_Pseudo Inst> : GCNPat <
  (add (add_oneuse (AMDGPUmul_u24_oneuse (srl i32:$src0, (i32 16)),
                                         (srl i32:$src1, (i32 16))), i32:$src2),
       (AMDGPUmul_u24_oneuse (and i32:$src0, (i32 65535)),
                             (and i32:$src1, (i32 65535)))
   ),
  (Inst (i32 8), $src0, (i32 8), $src1, (i32 8), $src2, (i1 0))> {
  let Predicates = Inst.Predicates;
}

class SDot2Pat<VOP_Pseudo Inst> : GCNPat <
  (add (add_oneuse (AMDGPUmul_i24_oneuse (sra i32:$src0, (i32 16)),
                                         (sra i32:$src1, (i32 16))), i32:$src2),
       (AMDGPUmul_i24_oneuse (sext_inreg i32:$src0, i16),
                             (sext_inreg i32:$src1, i16))),
  (Inst (i32 8), $src0, (i32 8), $src1, (i32 8), $src2, (i1 0))> {
  let Predicates = Inst.Predicates;
}

let IsDOT = 1 in {
let OtherPredicates = [HasDot2Insts] in {
defm V_DOT2_I32_I16 : VOP3PInst<"v_dot2_i32_i16",
  VOP3P_Profile<VOP_I32_V2I16_V2I16_I32>, int_amdgcn_sdot2, 1>;
defm V_DOT2_U32_U16 : VOP3PInst<"v_dot2_u32_u16",
  VOP3P_Profile<VOP_I32_V2I16_V2I16_I32>, int_amdgcn_udot2, 1>;
} // End OtherPredicates = [HasDot2Insts]

let OtherPredicates = [HasDot10Insts] in
defm V_DOT2_F32_F16 : VOP3PInst<"v_dot2_f32_f16",
  VOP3P_Profile<VOP_F32_V2F16_V2F16_F32, VOP3_REGULAR, /*HasDPP*/ 1>,
  AMDGPUfdot2, 1/*ExplicitClamp*/>;

let OtherPredicates = [HasDot7Insts] in {
let IsInvalidSingleUseConsumer = 1 in {
  defm V_DOT4_U32_U8  : VOP3PInst<"v_dot4_u32_u8",
    VOP3P_Profile<VOP_I32_I32_I32_I32, VOP3_PACKED>, int_amdgcn_udot4, 1>;
}
defm V_DOT8_U32_U4  : VOP3PInst<"v_dot8_u32_u4",
  VOP3P_Profile<VOP_I32_I32_I32_I32, VOP3_PACKED>, int_amdgcn_udot8, 1>;
} // End OtherPredicates = [HasDot7Insts]

let OtherPredicates = [HasDot1Insts] in {
let IsInvalidSingleUseConsumer = 1 in {
  defm V_DOT4_I32_I8  : VOP3PInst<"v_dot4_i32_i8",
    VOP3P_Profile<VOP_I32_I32_I32_I32, VOP3_PACKED>, int_amdgcn_sdot4, 1>;
}
defm V_DOT8_I32_I4  : VOP3PInst<"v_dot8_i32_i4",
  VOP3P_Profile<VOP_I32_I32_I32_I32, VOP3_PACKED>, int_amdgcn_sdot8, 1>;
} // End OtherPredicates = [HasDot1Insts]

def DOT2_BF16_Profile
  : VOP3P_Profile<VOP_F32_V2BF16_V2BF16_F32, VOP3_REGULAR, /*HasDPP*/ 1> {
  let HasSrc1Mods = 1;
}

let SubtargetPredicate = HasDot9Insts  in {

defm V_DOT2_F32_BF16 : VOP3PInst<"v_dot2_f32_bf16", DOT2_BF16_Profile,
  int_amdgcn_fdot2_f32_bf16, 1>;

} // End SubtargetPredicate = HasDot9Insts

} // End let IsDOT = 1

multiclass VOP3PDOTIUInst <string OpName, SDPatternOperator intrinsic_node> {
  let IsDOT = 1 in
  defm NAME : VOP3PInst<OpName, VOP3P_Profile<VOP_I32_I32_I32_I32, VOP3_PACKED>,
                        null_frag, 1>;
  // Dot-iu instructions consider input as signed if imod neg bits are set. Thus
  // Dot-iu Intrinsics have extra operands and require separate codegen pattern.
  def : GCNPat < (intrinsic_node (VOP3PModsNeg i32:$src0_mods), i32:$src0,
                                 (VOP3PModsNeg i32:$src1_mods), i32:$src1,
                                 i32:$src2, (i1 timm:$clamp)),
                 (!cast<Instruction>(NAME) $src0_mods, i32:$src0,
                                           $src1_mods, i32:$src1,
                                           (i32 8), i32:$src2, i1:$clamp)
  >;
}

let SubtargetPredicate = HasDot8Insts  in {
defm V_DOT4_I32_IU8 : VOP3PDOTIUInst<"v_dot4_i32_iu8", int_amdgcn_sudot4>;
defm V_DOT8_I32_IU4 : VOP3PDOTIUInst<"v_dot8_i32_iu4", int_amdgcn_sudot8>;

def : GCNPat < (int_amdgcn_sdot8 i32:$src0,
                                 i32:$src1,
                                 i32:$src2, (i1 timm:$clamp)),
               (V_DOT8_I32_IU4  (i32 9), i32:$src0,
                                (i32 9), i32:$src1, (i32 8), i32:$src2, i1:$clamp)
>;

def : GCNPat < (int_amdgcn_sdot4 i32:$src0,
                                 i32:$src1,
                                 i32:$src2, (i1 timm:$clamp)),
               (V_DOT4_I32_IU8  (i32 9), i32:$src0,
                                (i32 9), i32:$src1, (i32 8), i32:$src2, i1:$clamp)
>;
} // End SubtargetPredicate = HasDot8Insts

// Does not use opsel, no src_modifiers on src0 and src1.
// src_modifiers on src2(f32) are f32 fneg(neg_lo[2]) and f32 fabs(neg_hi[2]).
def VOP3P_DOTF8_Profile : VOP3P_Profile<VOPProfile <[f32, i32, i32, f32]>,
                                        VOP3_PACKED, 1> {
  let HasClamp = 0;
  let HasOpSel = 0;
  let HasOMod = 0;
  let IsDOT = 1;
  let HasSrc0Mods = 0;
  let HasSrc1Mods = 0;
  let HasSrc2Mods = 1;

  let InsVOP3P = (ins VSrc_b32:$src0, VSrc_b32:$src1,
                      PackedF16InputMods:$src2_modifiers, VSrc_f32:$src2,
                      neg_lo0:$neg_lo, neg_hi0:$neg_hi);

  let InsVOP3DPP8 = (ins DstRC:$old, VGPR_32:$src0, VRegSrc_32:$src1,
                         PackedF16InputMods:$src2_modifiers, VRegSrc_32:$src2,
                         neg_lo0:$neg_lo, neg_hi0:$neg_hi, dpp8:$dpp8, Dpp8FI:$fi);

  let InsVOP3DPP16 = (ins DstRC:$old, VGPR_32:$src0, VRegSrc_32:$src1,
                          PackedF16InputMods:$src2_modifiers, VRegSrc_32:$src2,
                          neg_lo0:$neg_lo, neg_hi0:$neg_hi, dpp_ctrl:$dpp_ctrl,
                          DppRowMask:$row_mask, DppBankMask:$bank_mask,
                          DppBoundCtrl:$bound_ctrl, Dpp16FI:$fi);
}

multiclass VOP3PDOTF8Inst <string OpName, SDPatternOperator intrinsic_node> {
  defm NAME : VOP3PInst<OpName, VOP3P_DOTF8_Profile, null_frag, 1>;

  let SubtargetPredicate = isGFX12Plus in
  def : GCNPat <(intrinsic_node i32:$src0, i32:$src1,
                                (VOP3Mods f32:$src2, i32:$src2_modifiers)),
                (!cast<Instruction>(NAME) i32:$src0, i32:$src1,
                                          i32:$src2_modifiers, f32:$src2)>;
}

let OtherPredicates = [HasDot11Insts] in {
defm V_DOT4_F32_FP8_BF8 : VOP3PDOTF8Inst<"v_dot4_f32_fp8_bf8", int_amdgcn_dot4_f32_fp8_bf8>;
defm V_DOT4_F32_BF8_FP8 : VOP3PDOTF8Inst<"v_dot4_f32_bf8_fp8", int_amdgcn_dot4_f32_bf8_fp8>;
defm V_DOT4_F32_FP8_FP8 : VOP3PDOTF8Inst<"v_dot4_f32_fp8_fp8", int_amdgcn_dot4_f32_fp8_fp8>;
defm V_DOT4_F32_BF8_BF8 : VOP3PDOTF8Inst<"v_dot4_f32_bf8_bf8", int_amdgcn_dot4_f32_bf8_bf8>;
}

def : UDot2Pat<V_DOT2_U32_U16>;
def : SDot2Pat<V_DOT2_I32_I16>;

foreach Type = ["U", "I"] in
  let Predicates = !cast<VOP_Pseudo>("V_DOT4_"#Type#"32_"#Type#8).Predicates in
  def : GCNPat <
    !cast<dag>(!foldl((i32 i32:$src2), [0, 1, 2, 3], lhs, y,
                      (add_oneuse lhs, (!cast<PatFrag>("Mul"#Type#"_Elt"#y) i32:$src0, i32:$src1)))),
    (!cast<VOP3P_Pseudo>("V_DOT4_"#Type#"32_"#Type#8) (i32 8), $src0, (i32 8), $src1, (i32 8), $src2, (i1 0))>;

foreach Type = ["U", "I"] in
  let Predicates = !cast<VOP_Pseudo>("V_DOT8_"#Type#"32_"#Type#4).Predicates in
  def : GCNPat <
    !cast<dag>(!foldl((add_oneuse i32:$src2, (!cast<PatFrag>("Mul"#Type#"0_4bit") i32:$src0, i32:$src1)),
                      [1, 2, 3, 4, 5, 6, 7], lhs, y,
                      (NonACAdd_oneuse lhs, (!cast<PatFrag>("Mul"#Type#y#"_4bit") i32:$src0, i32:$src1)))),
    (!cast<VOP3P_Pseudo>("V_DOT8_"#Type#"32_"#Type#4) (i32 8), $src0, (i32 8), $src1, (i32 8), $src2, (i1 0))>;

// Different variants of dot8 code-gen dag patterns are not generated through table-gen due to a huge increase
// in the compile time. Directly handle the pattern generated by the FE here.
foreach Type = ["U", "I"] in
  let Predicates = !cast<VOP_Pseudo>("V_DOT8_"#Type#"32_"#Type#4).Predicates in
  def : GCNPat <
    !cast<dag>(!foldl((add_oneuse i32:$src2, (!cast<PatFrag>("Mul"#Type#"0_4bit") i32:$src0, i32:$src1)),
                      [7, 1, 2, 3, 4, 5, 6], lhs, y,
                      (NonACAdd_oneuse lhs, (!cast<PatFrag>("Mul"#Type#y#"_4bit") i32:$src0, i32:$src1)))),
    (!cast<VOP3P_Pseudo>("V_DOT8_"#Type#"32_"#Type#4) (i32 8), $src0, (i32 8), $src1, (i32 8), $src2, (i1 0))>;

def ADst_32   : VOPDstOperand<AGPR_32>;
def ADst_64   : VOPDstOperand<AReg_64>;
def ADst_128  : VOPDstOperand<AReg_128>;
def ADst_256  : VOPDstOperand<AReg_256>;
def ADst_512  : VOPDstOperand<AReg_512>;
def ADst_1024 : VOPDstOperand<AReg_1024>;
def VDst_64   : VOPDstOperand<VReg_64>;
def VDst_128  : VOPDstOperand<VReg_128>;
def VDst_256  : VOPDstOperand<VReg_256>;
def VDst_512  : VOPDstOperand<VReg_512>;
def VDst_1024 : VOPDstOperand<VReg_1024>;

def VOPProfileAccRead : VOP3P_Profile<VOP_I32_I32, VOP3_MAI> {
  let Src0RC64 = ARegSrc_32;
}

def VOPProfileAccWrite : VOP3P_Profile<VOP_I32_I32, VOP3_MAI> {
  let DstRC = ADst_32;
  let Src0RC64 = VCSrc_b32;
}

class VOPProfileMAI<VOPProfile P, RegisterOperand _SrcRC, RegisterOperand _DstRC,
                    RegisterOperand SrcABRC = AVSrc_32>
  : VOP3P_Profile<P, VOP3_MAI> {
  let DstRC = _DstRC;
  let Src0RC64 = SrcABRC;
  let Src1RC64 = SrcABRC;
  let Src2RC64 = _SrcRC;
  let HasOpSel = 0;
  let HasClamp = 0;
  let HasIntClamp = 0;
  let HasOMod = 0;
  let HasModifiers = 0;
  let AsmVOP3Base = "$vdst, $src0, $src1, $src2$cbsz$abid$blgp";
  let Ins64 = (ins Src0RC64:$src0, Src1RC64:$src1, Src2RC64:$src2, CBSZ:$cbsz, ABID:$abid, blgp:$blgp);
  let InsVOP3Base = Ins64;
  // Dst and SrcC cannot partially overlap if SrcC/Dst is bigger than 4 VGPRs.
  // We then create two versions of the instruction: with tied dst and src2
  // and with the earlyclobber flag on the dst. This is stricter than the
  // actual HW restriction. In particular earlyclobber also affects src0 and
  // src1 allocation which is not required.
  bit NoDstOverlap = !gt(DstVT.Size, 128);
}

class VOPProfileSMFMAC<VOPProfile P, RegisterOperand _DstRC,
                       RegisterOperand _SrcARC, RegisterOperand _SrcBRC>
  : VOPProfileMAI<P, _DstRC, _DstRC, _SrcARC> {
  let Src1RC64 = _SrcBRC;
  let Src2VT = DstVT;
  let Asm64 = " $vdst, $src0, $src1, $idx$cbsz$abid";
  let Outs64 = (outs DstRC:$vdst);
  let Ins64 = (ins Src0RC64:$src0, Src1RC64:$src1, VRegSrc_32:$idx, CBSZ:$cbsz, ABID:$abid, Src2RC64:$src2);
}

def VOPProfileMAI_F32_F32_X4    : VOPProfileMAI<VOP_V4F32_F32_F32_V4F32,       AISrc_128_f32,  ADst_128>;
def VOPProfileMAI_F32_F32_X16   : VOPProfileMAI<VOP_V16F32_F32_F32_V16F32,     AISrc_512_f32,  ADst_512>;
def VOPProfileMAI_F32_F32_X32   : VOPProfileMAI<VOP_V32F32_F32_F32_V32F32,     AISrc_1024_f32, ADst_1024>;
def VOPProfileMAI_I32_I32_X4    : VOPProfileMAI<VOP_V4I32_I32_I32_V4I32,       AISrc_128_b32,  ADst_128>;
def VOPProfileMAI_I32_I32_X16   : VOPProfileMAI<VOP_V16I32_I32_I32_V16I32,     AISrc_512_b32,  ADst_512>;
def VOPProfileMAI_I32_I32_X32   : VOPProfileMAI<VOP_V32I32_I32_I32_V32I32,     AISrc_1024_b32, ADst_1024>;
def VOPProfileMAI_F32_V2I16_X4  : VOPProfileMAI<VOP_V4F32_V2I16_V2I16_V4F32,   AISrc_128_b32,  ADst_128>;
def VOPProfileMAI_F32_V2I16_X16 : VOPProfileMAI<VOP_V16F32_V2I16_V2I16_V16F32, AISrc_512_b32,  ADst_512>;
def VOPProfileMAI_F32_V2I16_X32 : VOPProfileMAI<VOP_V32F32_V2I16_V2I16_V32F32, AISrc_1024_b32, ADst_1024>;
def VOPProfileMAI_F32_V4F16_X4  : VOPProfileMAI<VOP_V4F32_V4F16_V4F16_V4F32,   AISrc_128_b32,  ADst_128,  AVSrc_64>;
def VOPProfileMAI_F32_V4F16_X16 : VOPProfileMAI<VOP_V16F32_V4F16_V4F16_V16F32, AISrc_512_b32,  ADst_512,  AVSrc_64>;
def VOPProfileMAI_F32_V4F16_X32 : VOPProfileMAI<VOP_V32F32_V4F16_V4F16_V32F32, AISrc_1024_b32, ADst_1024, AVSrc_64>;
def VOPProfileMAI_F32_V4I16_X4  : VOPProfileMAI<VOP_V4F32_V4I16_V4I16_V4F32,   AISrc_128_b32,  ADst_128,  AVSrc_64>;
def VOPProfileMAI_F32_V4I16_X16 : VOPProfileMAI<VOP_V16F32_V4I16_V4I16_V16F32, AISrc_512_b32,  ADst_512,  AVSrc_64>;
def VOPProfileMAI_F32_V4I16_X32 : VOPProfileMAI<VOP_V32F32_V4I16_V4I16_V32F32, AISrc_1024_b32, ADst_1024, AVSrc_64>;
def VOPProfileMAI_F64_16X16X4F64 : VOPProfileMAI<VOP_V4F64_F64_F64_V4F64,      AISrc_256_f64,  ADst_256,  AVSrc_64>;
def VOPProfileMAI_F64_4X4X4F64   : VOPProfileMAI<VOP_F64_F64_F64_F64,          AISrc_64_f64,   ADst_64,   AVSrc_64>;
def VOPProfileMAI_I32_I64_X16   : VOPProfileMAI<VOP_V4I32_I64_I64_V4I32,       AISrc_128_b32,  ADst_128,  AVSrc_64>;
def VOPProfileMAI_I32_I64_X32   : VOPProfileMAI<VOP_V16I32_I64_I64_V16I32,     AISrc_512_b32,  ADst_512,  AVSrc_64>;
def VOPProfileMAI_F32_V2F32_X16 : VOPProfileMAI<VOP_V4F32_V2F32_V2F32_V4F32,   AISrc_128_b32,  ADst_128,  AVSrc_64>;
def VOPProfileMAI_F32_V2F32_X32 : VOPProfileMAI<VOP_V16F32_V2F32_V2F32_V16F32, AISrc_512_b32,  ADst_512,  AVSrc_64>;
def VOPProfileMAI_F32_I64_X32   : VOPProfileMAI<VOP_V4F32_I64_I64_V4F32,       AISrc_128_b32,  ADst_128,  AVSrc_64>;
def VOPProfileMAI_F32_I64_X16   : VOPProfileMAI<VOP_V16F32_I64_I64_V16F32,     AISrc_512_b32,  ADst_512,  AVSrc_64>;

def VOPProfileMAI_F32_F32_X4_VCD     : VOPProfileMAI<VOP_V4F32_F32_F32_V4F32,       VISrc_128_f32,  VDst_128>;
def VOPProfileMAI_F32_F32_X16_VCD    : VOPProfileMAI<VOP_V16F32_F32_F32_V16F32,     VISrc_512_f32,  VDst_512>;
def VOPProfileMAI_F32_F32_X32_VCD    : VOPProfileMAI<VOP_V32F32_F32_F32_V32F32,     VISrc_1024_f32, VDst_1024>;
def VOPProfileMAI_I32_I32_X4_VCD     : VOPProfileMAI<VOP_V4I32_I32_I32_V4I32,       VISrc_128_b32,  VDst_128>;
def VOPProfileMAI_I32_I32_X16_VCD    : VOPProfileMAI<VOP_V16I32_I32_I32_V16I32,     VISrc_512_b32,  VDst_512>;
def VOPProfileMAI_I32_I32_X32_VCD    : VOPProfileMAI<VOP_V32I32_I32_I32_V32I32,     VISrc_1024_b32, VDst_1024>;
def VOPProfileMAI_F32_V2I16_X4_VCD   : VOPProfileMAI<VOP_V4F32_V2I16_V2I16_V4F32,   VISrc_128_b32,  VDst_128>;
def VOPProfileMAI_F32_V2I16_X16_VCD  : VOPProfileMAI<VOP_V16F32_V2I16_V2I16_V16F32, VISrc_512_b32,  VDst_512>;
def VOPProfileMAI_F32_V2I16_X32_VCD  : VOPProfileMAI<VOP_V32F32_V2I16_V2I16_V32F32, VISrc_1024_b32, VDst_1024>;
def VOPProfileMAI_F32_V4F16_X4_VCD   : VOPProfileMAI<VOP_V4F32_V4F16_V4F16_V4F32,   VISrc_128_b32,  VDst_128,  AVSrc_64>;
def VOPProfileMAI_F32_V4F16_X16_VCD  : VOPProfileMAI<VOP_V16F32_V4F16_V4F16_V16F32, VISrc_512_b32,  VDst_512,  AVSrc_64>;
def VOPProfileMAI_F32_V4F16_X32_VCD  : VOPProfileMAI<VOP_V32F32_V4F16_V4F16_V32F32, VISrc_1024_b32, VDst_1024, AVSrc_64>;
def VOPProfileMAI_F32_V4I16_X4_VCD   : VOPProfileMAI<VOP_V4F32_V4I16_V4I16_V4F32,   VISrc_128_b32,  VDst_128,  AVSrc_64>;
def VOPProfileMAI_F32_V4I16_X16_VCD  : VOPProfileMAI<VOP_V16F32_V4I16_V4I16_V16F32, VISrc_512_b32,  VDst_512,  AVSrc_64>;
def VOPProfileMAI_F32_V4I16_X32_VCD  : VOPProfileMAI<VOP_V32F32_V4I16_V4I16_V32F32, VISrc_1024_b32, VDst_1024, AVSrc_64>;
def VOPProfileMAI_F64_16X16X4F64_VCD : VOPProfileMAI<VOP_V4F64_F64_F64_V4F64,       VISrc_256_f64,  VDst_256,  AVSrc_64>;
def VOPProfileMAI_F64_4X4X4F64_VCD   : VOPProfileMAI<VOP_F64_F64_F64_F64,           VISrc_64_f64,   VDst_64,   AVSrc_64>;
def VOPProfileMAI_I32_I64_X16_VCD    : VOPProfileMAI<VOP_V4I32_I64_I64_V4I32,       VISrc_128_b32,  VDst_128,  AVSrc_64>;
def VOPProfileMAI_I32_I64_X32_VCD    : VOPProfileMAI<VOP_V16I32_I64_I64_V16I32,     VISrc_512_b32,  VDst_512,  AVSrc_64>;
def VOPProfileMAI_F32_V2F32_X16_VCD  : VOPProfileMAI<VOP_V4F32_V2F32_V2F32_V4F32,   VISrc_128_b32,  VDst_128,  AVSrc_64>;
def VOPProfileMAI_F32_V2F32_X32_VCD  : VOPProfileMAI<VOP_V16F32_V2F32_V2F32_V16F32, VISrc_512_b32,  VDst_512,  AVSrc_64>;
def VOPProfileMAI_F32_I64_X32_VCD    : VOPProfileMAI<VOP_V4F32_I64_I64_V4F32,       VISrc_128_b32,  VDst_128,  AVSrc_64>;
def VOPProfileMAI_F32_I64_X16_VCD    : VOPProfileMAI<VOP_V16F32_I64_I64_V16F32,     VISrc_512_b32,  VDst_512,  AVSrc_64>;

def VOPProfileSMFMAC_F32_16X16X32_F16 : VOPProfileSMFMAC<VOP_V4F32_V4F16_V8F16_I32,  AVDst_128, AVSrc_64, AVSrc_128>;
def VOPProfileSMFMAC_F32_32X32X16_F16 : VOPProfileSMFMAC<VOP_V16F32_V4F16_V8F16_I32, AVDst_512, AVSrc_64, AVSrc_128>;
def VOPProfileSMFMAC_F32_16X16X32_I16 : VOPProfileSMFMAC<VOP_V4F32_V4I16_V8I16_I32,  AVDst_128, AVSrc_64, AVSrc_128>;
def VOPProfileSMFMAC_F32_32X32X16_I16 : VOPProfileSMFMAC<VOP_V16F32_V4I16_V8I16_I32, AVDst_512, AVSrc_64, AVSrc_128>;
def VOPProfileSMFMAC_I32_16X16X64_I8  : VOPProfileSMFMAC<VOP_V4I32_V2I32_V4I32_I32,  AVDst_128, AVSrc_64, AVSrc_128>;
def VOPProfileSMFMAC_I32_32X32X32_I8  : VOPProfileSMFMAC<VOP_V16I32_V2I32_V4I32_I32, AVDst_512, AVSrc_64, AVSrc_128>;
def VOPProfileSMFMAC_F32_16X16X64_F8  : VOPProfileSMFMAC<VOP_V4F32_V2I32_V4I32_I32,  AVDst_128, AVSrc_64, AVSrc_128>;
def VOPProfileSMFMAC_F32_32X32X32_F8  : VOPProfileSMFMAC<VOP_V16F32_V2I32_V4I32_I32, AVDst_512, AVSrc_64, AVSrc_128>;

class MFMATable <bit is_mac, string Name> {
  bit IsMac = is_mac;
  string FMAOp = Name;
}

class MAIFrag<SDPatternOperator Op, code pred> : PatFrag <
  (ops node:$src0, node:$src1, node:$src2, node:$cbsz, node:$abid, node:$blgp),
  (Op $src0, $src1, $src2, $cbsz, $abid, $blgp),
  pred
>;

defvar MayNeedAGPRs = [{
  return MF->getInfo<SIMachineFunctionInfo>()->mayNeedAGPRs();
}];

defvar MayNeedAGPRs_gisel = [{
  return MF.getInfo<SIMachineFunctionInfo>()->mayNeedAGPRs();
}];

defvar MayNotNeedAGPRs = [{
  return !MF->getInfo<SIMachineFunctionInfo>()->mayNeedAGPRs();
}];

defvar MayNotNeedAGPRs_gisel = [{
  return !MF.getInfo<SIMachineFunctionInfo>()->mayNeedAGPRs();
}];

class AgprMAIFrag<SDPatternOperator Op> : MAIFrag<Op, MayNeedAGPRs> {
  let GISelPredicateCode = MayNeedAGPRs_gisel;
}

class VgprMAIFrag<SDPatternOperator Op> : MAIFrag<Op, MayNotNeedAGPRs> {
  let GISelPredicateCode = MayNotNeedAGPRs_gisel;
}

let SubtargetPredicate = HasMAIInsts in {

let isAsCheapAsAMove = 1, isReMaterializable = 1 in {
  defm V_ACCVGPR_READ_B32  : VOP3Inst<"v_accvgpr_read_b32",  VOPProfileAccRead>;
  let isMoveImm = 1 in {
    defm V_ACCVGPR_WRITE_B32 : VOP3Inst<"v_accvgpr_write_b32", VOPProfileAccWrite>;
  } // End isMoveImm = 1
} // End isAsCheapAsAMove = 1, isReMaterializable = 1

class MAIInst<string OpName, VOPProfile P, SDPatternOperator node>
  : VOP3InstBase<OpName, P, node> {
  Instruction Opcode = !cast<Instruction>(NAME);
  bit is_dgemm = 0;
  bit is_gfx940_xdl = 0;
}

multiclass MAIInst<string OpName, string P, SDPatternOperator node> {
  defvar NoDstOverlap = !cast<VOPProfileMAI>("VOPProfileMAI_" # P).NoDstOverlap;

  let isConvergent = 1, mayRaiseFPException = 0, ReadsModeReg = 1 in {
    // FP32 denorm mode is respected, rounding mode is not. Exceptions are not supported.
    let Constraints = !if(NoDstOverlap, "@earlyclobber $vdst", "") in {
      def _e64 : MAIInst<OpName, !cast<VOPProfileMAI>("VOPProfileMAI_" # P),
                         !if(!or(NoDstOverlap, !eq(node, null_frag)), null_frag, AgprMAIFrag<node>)>,
                 MFMATable<0, NAME # "_e64">;

      let SubtargetPredicate = isGFX90APlus, Mnemonic = OpName in
      def _vgprcd_e64 : MAIInst<OpName # "_vgprcd", !cast<VOPProfileMAI>("VOPProfileMAI_" # P # "_VCD"),
                                !if(!or(NoDstOverlap, !eq(node, null_frag)), null_frag, VgprMAIFrag<node>)>,
                        MFMATable<0, NAME # "_vgprcd_e64">;
    }

    if NoDstOverlap then {
      let Constraints = !if(NoDstOverlap, "$vdst = $src2", ""),
          isConvertibleToThreeAddress = NoDstOverlap,
          Mnemonic = OpName in {
        def "_mac_e64" : MAIInst<OpName # "_mac", !cast<VOPProfileMAI>("VOPProfileMAI_" # P),
                                 !if(!eq(node, null_frag), null_frag, AgprMAIFrag<node>)>,
                         MFMATable<1, NAME # "_e64">;

        let SubtargetPredicate = isGFX90APlus in
        def _mac_vgprcd_e64 : MAIInst<OpName # "_mac_vgprcd", !cast<VOPProfileMAI>("VOPProfileMAI_" # P # "_VCD"),
                                      !if(!eq(node, null_frag), null_frag, VgprMAIFrag<node>)>,
                              MFMATable<1, NAME # "_vgprcd_e64">;
      }
    }
  } // End isConvergent = 1, mayRaiseFPException = 0, ReadsModeReg = 1
}

defm V_MFMA_F32_4X4X1F32    : MAIInst<"v_mfma_f32_4x4x1f32",    "F32_F32_X4",    int_amdgcn_mfma_f32_4x4x1f32>;
defm V_MFMA_F32_16X16X1F32  : MAIInst<"v_mfma_f32_16x16x1f32",  "F32_F32_X16",   int_amdgcn_mfma_f32_16x16x1f32>;
defm V_MFMA_F32_16X16X4F32  : MAIInst<"v_mfma_f32_16x16x4f32",  "F32_F32_X4",    int_amdgcn_mfma_f32_16x16x4f32>;
defm V_MFMA_F32_32X32X1F32  : MAIInst<"v_mfma_f32_32x32x1f32",  "F32_F32_X32",   int_amdgcn_mfma_f32_32x32x1f32>;
defm V_MFMA_F32_32X32X2F32  : MAIInst<"v_mfma_f32_32x32x2f32",  "F32_F32_X16",   int_amdgcn_mfma_f32_32x32x2f32>;

let is_gfx940_xdl = 1 in {
defm V_MFMA_F32_4X4X4F16    : MAIInst<"v_mfma_f32_4x4x4f16",    "F32_V4F16_X4",  int_amdgcn_mfma_f32_4x4x4f16>;
defm V_MFMA_I32_4X4X4I8     : MAIInst<"v_mfma_i32_4x4x4i8",     "I32_I32_X4",    int_amdgcn_mfma_i32_4x4x4i8>;
defm V_MFMA_F32_16X16X4F16  : MAIInst<"v_mfma_f32_16x16x4f16",  "F32_V4F16_X16", int_amdgcn_mfma_f32_16x16x4f16>;
defm V_MFMA_F32_16X16X16F16 : MAIInst<"v_mfma_f32_16x16x16f16", "F32_V4F16_X4",  int_amdgcn_mfma_f32_16x16x16f16>;
defm V_MFMA_I32_16X16X4I8   : MAIInst<"v_mfma_i32_16x16x4i8",   "I32_I32_X16",   int_amdgcn_mfma_i32_16x16x4i8>;
defm V_MFMA_F32_32X32X4F16  : MAIInst<"v_mfma_f32_32x32x4f16",  "F32_V4F16_X32", int_amdgcn_mfma_f32_32x32x4f16>;
defm V_MFMA_F32_32X32X8F16  : MAIInst<"v_mfma_f32_32x32x8f16",  "F32_V4F16_X16", int_amdgcn_mfma_f32_32x32x8f16>;
defm V_MFMA_I32_32X32X4I8   : MAIInst<"v_mfma_i32_32x32x4i8",   "I32_I32_X32",   int_amdgcn_mfma_i32_32x32x4i8>;
}

let Predicates = [isGFX908orGFX90A] in {
defm V_MFMA_I32_16X16X16I8  : MAIInst<"v_mfma_i32_16x16x16i8",  "I32_I32_X4",    int_amdgcn_mfma_i32_16x16x16i8>;
defm V_MFMA_I32_32X32X8I8   : MAIInst<"v_mfma_i32_32x32x8i8",   "I32_I32_X16",   int_amdgcn_mfma_i32_32x32x8i8>;
defm V_MFMA_F32_4X4X2BF16   : MAIInst<"v_mfma_f32_4x4x2bf16",   "F32_V2I16_X4",  int_amdgcn_mfma_f32_4x4x2bf16>;
defm V_MFMA_F32_16X16X2BF16 : MAIInst<"v_mfma_f32_16x16x2bf16", "F32_V2I16_X16", int_amdgcn_mfma_f32_16x16x2bf16>;
defm V_MFMA_F32_16X16X8BF16 : MAIInst<"v_mfma_f32_16x16x8bf16", "F32_V2I16_X4",  int_amdgcn_mfma_f32_16x16x8bf16>;
defm V_MFMA_F32_32X32X2BF16 : MAIInst<"v_mfma_f32_32x32x2bf16", "F32_V2I16_X32", int_amdgcn_mfma_f32_32x32x2bf16>;
defm V_MFMA_F32_32X32X4BF16 : MAIInst<"v_mfma_f32_32x32x4bf16", "F32_V2I16_X16", int_amdgcn_mfma_f32_32x32x4bf16>;
}

} // End SubtargetPredicate = HasMAIInsts

let Predicates = [isGFX90APlus] in {
  let is_gfx940_xdl = 1 in {
  defm V_MFMA_F32_32X32X4BF16_1K  : MAIInst<"v_mfma_f32_32x32x4bf16_1k",  "F32_V4I16_X32",  int_amdgcn_mfma_f32_32x32x4bf16_1k>;
  defm V_MFMA_F32_16X16X4BF16_1K  : MAIInst<"v_mfma_f32_16x16x4bf16_1k",  "F32_V4I16_X16",  int_amdgcn_mfma_f32_16x16x4bf16_1k>;
  defm V_MFMA_F32_4X4X4BF16_1K    : MAIInst<"v_mfma_f32_4x4x4bf16_1k",    "F32_V4I16_X4",   int_amdgcn_mfma_f32_4x4x4bf16_1k>;
  defm V_MFMA_F32_32X32X8BF16_1K  : MAIInst<"v_mfma_f32_32x32x8bf16_1k",  "F32_V4I16_X16",  int_amdgcn_mfma_f32_32x32x8bf16_1k>;
  defm V_MFMA_F32_16X16X16BF16_1K : MAIInst<"v_mfma_f32_16x16x16bf16_1k", "F32_V4I16_X4",   int_amdgcn_mfma_f32_16x16x16bf16_1k>;
  }

  let is_dgemm = 1 in {
  defm V_MFMA_F64_16X16X4F64      : MAIInst<"v_mfma_f64_16x16x4f64",      "F64_16X16X4F64", int_amdgcn_mfma_f64_16x16x4f64>;
  defm V_MFMA_F64_4X4X4F64        : MAIInst<"v_mfma_f64_4x4x4f64",        "F64_4X4X4F64",   int_amdgcn_mfma_f64_4x4x4f64>;
  }
} // End Predicates = [isGFX90APlus]

let SubtargetPredicate = isGFX940Plus, is_gfx940_xdl = 1 in {
  defm V_MFMA_I32_32X32X16I8       : MAIInst<"v_mfma_i32_32x32x16i8",       "I32_I64_X32",    int_amdgcn_mfma_i32_32x32x16_i8>;
  defm V_MFMA_I32_16X16X32I8       : MAIInst<"v_mfma_i32_16x16x32i8",       "I32_I64_X16",    int_amdgcn_mfma_i32_16x16x32_i8>;
  defm V_MFMA_F32_16X16X8XF32      : MAIInst<"v_mfma_f32_16x16x8xf32",      "F32_V2F32_X16",  int_amdgcn_mfma_f32_16x16x8_xf32>;
  defm V_MFMA_F32_32X32X4XF32      : MAIInst<"v_mfma_f32_32x32x4xf32",      "F32_V2F32_X32",  int_amdgcn_mfma_f32_32x32x4_xf32>;
  defm V_MFMA_F32_16X16X32_BF8_BF8 : MAIInst<"v_mfma_f32_16x16x32_bf8_bf8", "F32_I64_X32",    int_amdgcn_mfma_f32_16x16x32_bf8_bf8>;
  defm V_MFMA_F32_16X16X32_BF8_FP8 : MAIInst<"v_mfma_f32_16x16x32_bf8_fp8", "F32_I64_X32",    int_amdgcn_mfma_f32_16x16x32_bf8_fp8>;
  defm V_MFMA_F32_16X16X32_FP8_BF8 : MAIInst<"v_mfma_f32_16x16x32_fp8_bf8", "F32_I64_X32",    int_amdgcn_mfma_f32_16x16x32_fp8_bf8>;
  defm V_MFMA_F32_16X16X32_FP8_FP8 : MAIInst<"v_mfma_f32_16x16x32_fp8_fp8", "F32_I64_X32",    int_amdgcn_mfma_f32_16x16x32_fp8_fp8>;
  defm V_MFMA_F32_32X32X16_BF8_BF8 : MAIInst<"v_mfma_f32_32x32x16_bf8_bf8", "F32_I64_X16",    int_amdgcn_mfma_f32_32x32x16_bf8_bf8>;
  defm V_MFMA_F32_32X32X16_BF8_FP8 : MAIInst<"v_mfma_f32_32x32x16_bf8_fp8", "F32_I64_X16",    int_amdgcn_mfma_f32_32x32x16_bf8_fp8>;
  defm V_MFMA_F32_32X32X16_FP8_BF8 : MAIInst<"v_mfma_f32_32x32x16_fp8_bf8", "F32_I64_X16",    int_amdgcn_mfma_f32_32x32x16_fp8_bf8>;
  defm V_MFMA_F32_32X32X16_FP8_FP8 : MAIInst<"v_mfma_f32_32x32x16_fp8_fp8", "F32_I64_X16",    int_amdgcn_mfma_f32_32x32x16_fp8_fp8>;
} // End SubtargetPredicate = isGFX940Plus, is_gfx940_xdl = 1

multiclass SMFMACInst<string OpName, string P, SDPatternOperator node> {
  let Constraints = "$vdst = $src2", DisableEncoding = "$src2",
      isConvergent = 1, mayRaiseFPException = 0, ReadsModeReg = 1, is_gfx940_xdl = 1 in {
    def _e64 : MAIInst<OpName, !cast<VOPProfileSMFMAC>("VOPProfileSMFMAC_" # P), node>;
  }
}

let SubtargetPredicate = isGFX940Plus in {
defm V_SMFMAC_F32_16X16X32_F16     : SMFMACInst<"v_smfmac_f32_16x16x32_f16",     "F32_16X16X32_F16", int_amdgcn_smfmac_f32_16x16x32_f16>;
defm V_SMFMAC_F32_32X32X16_F16     : SMFMACInst<"v_smfmac_f32_32x32x16_f16",     "F32_32X32X16_F16", int_amdgcn_smfmac_f32_32x32x16_f16>;
defm V_SMFMAC_F32_16X16X32_BF16    : SMFMACInst<"v_smfmac_f32_16x16x32_bf16",    "F32_16X16X32_I16", int_amdgcn_smfmac_f32_16x16x32_bf16>;
defm V_SMFMAC_F32_32X32X16_BF16    : SMFMACInst<"v_smfmac_f32_32x32x16_bf16",    "F32_32X32X16_I16", int_amdgcn_smfmac_f32_32x32x16_bf16>;
defm V_SMFMAC_I32_16X16X64_I8      : SMFMACInst<"v_smfmac_i32_16x16x64_i8",      "I32_16X16X64_I8",  int_amdgcn_smfmac_i32_16x16x64_i8>;
defm V_SMFMAC_I32_32X32X32_I8      : SMFMACInst<"v_smfmac_i32_32x32x32_i8",      "I32_32X32X32_I8",  int_amdgcn_smfmac_i32_32x32x32_i8>;
defm V_SMFMAC_F32_16X16X64_BF8_BF8 : SMFMACInst<"v_smfmac_f32_16x16x64_bf8_bf8", "F32_16X16X64_F8",  int_amdgcn_smfmac_f32_16x16x64_bf8_bf8>;
defm V_SMFMAC_F32_16X16X64_BF8_FP8 : SMFMACInst<"v_smfmac_f32_16x16x64_bf8_fp8", "F32_16X16X64_F8",  int_amdgcn_smfmac_f32_16x16x64_bf8_fp8>;
defm V_SMFMAC_F32_16X16X64_FP8_BF8 : SMFMACInst<"v_smfmac_f32_16x16x64_fp8_bf8", "F32_16X16X64_F8",  int_amdgcn_smfmac_f32_16x16x64_fp8_bf8>;
defm V_SMFMAC_F32_16X16X64_FP8_FP8 : SMFMACInst<"v_smfmac_f32_16x16x64_fp8_fp8", "F32_16X16X64_F8",  int_amdgcn_smfmac_f32_16x16x64_fp8_fp8>;
defm V_SMFMAC_F32_32X32X32_BF8_BF8 : SMFMACInst<"v_smfmac_f32_32x32x32_bf8_bf8", "F32_32X32X32_F8",  int_amdgcn_smfmac_f32_32x32x32_bf8_bf8>;
defm V_SMFMAC_F32_32X32X32_BF8_FP8 : SMFMACInst<"v_smfmac_f32_32x32x32_bf8_fp8", "F32_32X32X32_F8",  int_amdgcn_smfmac_f32_32x32x32_bf8_fp8>;
defm V_SMFMAC_F32_32X32X32_FP8_BF8 : SMFMACInst<"v_smfmac_f32_32x32x32_fp8_bf8", "F32_32X32X32_F8",  int_amdgcn_smfmac_f32_32x32x32_fp8_bf8>;
defm V_SMFMAC_F32_32X32X32_FP8_FP8 : SMFMACInst<"v_smfmac_f32_32x32x32_fp8_fp8", "F32_32X32X32_F8",  int_amdgcn_smfmac_f32_32x32x32_fp8_fp8>;
}

def MAIInstInfoTable : GenericTable {
  let FilterClass = "MAIInst";
  let CppTypeName = "MAIInstInfo";
  let Fields = [
    "Opcode", "is_dgemm", "is_gfx940_xdl"
  ];

  let PrimaryKey = ["Opcode"];
  let PrimaryKeyName = "getMAIInstInfoHelper";
}

let isCommutable = 1, isReMaterializable = 1 in {
  let SubtargetPredicate = HasPackedFP32Ops in {
    defm V_PK_FMA_F32 : VOP3PInst<"v_pk_fma_f32", VOP3P_Profile<VOP_V2F32_V2F32_V2F32_V2F32, VOP3_PACKED>, any_fma>;
    defm V_PK_MUL_F32 : VOP3PInst<"v_pk_mul_f32", VOP3P_Profile<VOP_V2F32_V2F32_V2F32, VOP3_PACKED>, any_fmul>;
    defm V_PK_ADD_F32 : VOP3PInst<"v_pk_add_f32", VOP3P_Profile<VOP_V2F32_V2F32_V2F32, VOP3_PACKED>, any_fadd>;
  } // End SubtargetPredicate = HasPackedFP32Ops

  let SubtargetPredicate = HasPkMovB32 in
  defm V_PK_MOV_B32 : VOP3PInst<"v_pk_mov_b32", VOP3P_Profile<VOP_V2I32_V2I32_V2I32, VOP3_PACKED>>;
} // End isCommutable = 1, isReMaterializable = 1

def : AMDGPUMnemonicAlias<"v_accvgpr_read",  "v_accvgpr_read_b32">;
def : AMDGPUMnemonicAlias<"v_accvgpr_write", "v_accvgpr_write_b32">;

class VOPProfileWMMA<VOPProfile P, string Suffix, RegisterOperand _Src01RC64, bit _HasClamp, bit _HasOpSel> : VOP3P_Profile<P> {
  let DstRC = !if(!eq(Suffix, "_w32"), VDst_256, VDst_128);
  let Src0RC64 = _Src01RC64;
  let Src1RC64 = _Src01RC64;
  let Src2RC64 = !if(!eq(Suffix, "_w32"), VISrc_256_f64, VISrc_128_f32);
  let HasClamp = _HasClamp;
  let HasOpSel = _HasOpSel;
  let IsPacked = 1;
  let IsWMMA = 1;
}

def VOP_V8F32_V16F16_V16F16_V8F32 : VOPProfile <[v8f32, v16f16, v16f16, v8f32]>;
def VOP_V8F32_V16I16_V16I16_V8F32 : VOPProfile <[v8f32, v16i16, v16i16, v8f32]>;
def VOP_V16F16_V16F16_V16F16_V16F16 : VOPProfile <[v16f16, v16f16, v16f16, v16f16]>;
def VOP_V16I16_V16I16_V16I16_V16I16 : VOPProfile <[v16i16, v16i16, v16i16, v16i16]>;
def VOP_V8I32_V4I32_V4I32_V8I32 : VOPProfile <[v8i32, v4i32, v4i32, v8i32]>;
def VOP_V8I32_V2I32_V2I32_V8I32 : VOPProfile <[v8i32, v2i32, v2i32, v8i32]>;

def VOP_V4F32_V16F16_V16F16_V4F32 : VOPProfile <[v4f32, v16f16, v16f16, v4f32]>;
def VOP_V4F32_V16I16_V16I16_V4F32 : VOPProfile <[v4f32, v16i16, v16i16, v4f32]>;
def VOP_V8F16_V16F16_V16F16_V8F16 : VOPProfile <[v8f16, v16f16, v16f16, v8f16]>;
def VOP_V8I16_V16I16_V16I16_V8I16 : VOPProfile <[v8i16, v16i16, v16i16, v8i16]>;
def VOP_V4I32_V4I32_V4I32_V4I32 : VOPProfile <[v4i32, v4i32, v4i32, v4i32]>;
def VOP_V4I32_V2I32_V2I32_V4I32 : VOPProfile <[v4i32, v2i32, v2i32, v4i32]>;


class WMMAType <bits<2> val> {
  bit hasClamp = val{0};
  bit hasOpsel = val{1};
}

def WMMARegular      : WMMAType<0b00>;
def WMMAUIClamp      : WMMAType<0b01>;
def WMMAOpSel        : WMMAType<0b10>;

class WMMARegularPat<Instruction Inst, SDPatternOperator node, VOPProfile P> :
  GCNPat < (P.DstVT (node
                                (P.Src0VT (VOP3PMods P.Src0VT:$src0, i32:$src0_modifiers)),
                                (P.Src1VT (VOP3PMods P.Src1VT:$src1, i32:$src1_modifiers)),
                                (P.Src2VT (VOP3PMods P.Src2VT:$src2, i32:$src2_modifiers))
                   )),
                   (P.DstVT (Inst i32:$src0_modifiers, P.Src0VT:$src0, i32:$src1_modifiers, P.Src1VT:$src1, $src2_modifiers, P.Src2VT:$src2))
>;

class WMMAOpSelPat<Instruction Inst, SDPatternOperator node, VOPProfile P> :
  GCNPat < (P.DstVT (node
                                (P.Src0VT P.Src0VT:$src0),
                                (P.Src1VT P.Src1VT:$src1),
                                (P.Src2VT P.Src2VT:$src2), (WMMAOpSelVOP3PMods i32:$src2_modifiers)
                   )),
                   (P.DstVT (Inst (i32 8), P.Src0VT:$src0, (i32 8), P.Src1VT:$src1, i32:$src2_modifiers, P.Src2VT:$src2))
>;

class WMMAUIClampPat<Instruction Inst, SDPatternOperator node, VOPProfile P> :
  GCNPat < (P.DstVT (node
                                (VOP3PModsNeg i32:$src0_modifiers), (P.Src0VT P.Src0VT:$src0),
                                (VOP3PModsNeg i32:$src1_modifiers), (P.Src1VT P.Src1VT:$src1),
                                (P.Src2VT P.Src2VT:$src2), (i1 timm:$clamp)
                   )),
                   (P.DstVT (Inst i32:$src0_modifiers, P.Src0VT:$src0, i32:$src1_modifiers, P.Src1VT:$src1, (i32 8), P.Src2VT:$src2, i1:$clamp))
>;

class WMMAOpcodeMapping<Instruction TwoAddr, Instruction ThreeAddr> {
  Instruction Opcode2Addr = TwoAddr;
  Instruction Opcode3Addr = ThreeAddr;
  Predicate WaveSizePredicate;
}

def WMMAOpcode : GenericEnum {
  let FilterClass = "VOP3P_Pseudo";
}

class WMMAMappingTable : GenericTable {
  let FilterClass = "WMMAOpcodeMapping";
  let CppTypeName = "WMMAOpcodeMappingInfo";
  let Fields = ["Opcode2Addr", "Opcode3Addr"];
  string TypeOf_Opcode2Addr = "WMMAOpcode";
  string TypeOf_Opcode3Addr = "WMMAOpcode";
}

def WMMAOpcode2AddrMappingTable : WMMAMappingTable {
  let PrimaryKey = ["Opcode2Addr"];
  let PrimaryKeyName = "getWMMAMappingInfoFrom2AddrOpcode";
}

def WMMAOpcode3AddrMappingTable : WMMAMappingTable {
  let PrimaryKey = ["Opcode3Addr"];
  let PrimaryKeyName = "getWMMAMappingInfoFrom3AddrOpcode";
}

// The WMMA instruction has extra constraints:
// Matrices A and B cannot overlap with D. C cannot partially overlap with D,
// but it is OK for them to be the same (which is a typical case).
//
// We implement it as follows:
// 1) Map the intrinsic to the pseudo where D is tied to C ($vdst = $src2).
// 2) The pass twoaddressinstruction checks if src2 is live and if that is the case
//    it converts the default pseudo to the pseudo where src2 is not the same as vdst.
// 3) @earlyclobber on the destination satisfies the constraint during RA.

multiclass WMMAInst<string Suffix, string Instr, VOPProfile P, SDPatternOperator node = null_frag, RegisterOperand _Src01RC64 = VRegSrc_256, WMMAType Type, bit convertibleTo3Addr> {

  defvar WMMAConstraints2Addr = "@earlyclobber $vdst,$vdst = $src2";
  defvar WMMAConstraints3Addr = "@earlyclobber $vdst";

  defvar WMMAProfile = VOPProfileWMMA<P, Suffix, _Src01RC64, Type.hasClamp, Type.hasOpsel>;
  let Mnemonic = Instr, mayRaiseFPException = 0, ReadsModeReg = 0 in {
    let Constraints = WMMAConstraints2Addr, isConvertibleToThreeAddress = convertibleTo3Addr in {
      def _twoaddr # Suffix : VOP3P_Pseudo<Instr # Suffix, WMMAProfile>;
    }
  }
  if convertibleTo3Addr then {
    let Mnemonic = Instr, mayRaiseFPException = 0, ReadsModeReg = 0 in {
      let Constraints = WMMAConstraints3Addr, SchedRW = [Write32Bit, Write32Bit] in {
        def _threeaddr # Suffix : VOP3P_Pseudo<Instr # Suffix, WMMAProfile>;
      }
    }
    def : WMMAOpcodeMapping<!cast<Instruction>(NAME # _twoaddr # Suffix),
                          !cast<Instruction>(NAME # _threeaddr # Suffix)>;
  }

  let SubtargetPredicate = isGFX11Only in {
    if !eq(Type, WMMAOpSel) then {
      def : WMMAOpSelPat<!cast<Instruction>(NAME # _twoaddr # Suffix), node, P>;
    } else if !eq(Type, WMMAUIClamp) then {
      def : WMMAUIClampPat<!cast<Instruction>(NAME # _twoaddr # Suffix), node, P>;
    } else {
      def : WMMARegularPat<!cast<Instruction>(NAME # _twoaddr # Suffix), node, P>;
    }
  }
}



let WaveSizePredicate = isWave32 in {
  defm V_WMMA_F32_16X16X16_F16   : WMMAInst<"_w32", "v_wmma_f32_16x16x16_f16",  VOP_V8F32_V16F16_V16F16_V8F32, int_amdgcn_wmma_f32_16x16x16_f16, VRegSrc_256, WMMARegular, 1>;
  defm V_WMMA_F32_16X16X16_BF16  : WMMAInst<"_w32", "v_wmma_f32_16x16x16_bf16", VOP_V8F32_V16I16_V16I16_V8F32, int_amdgcn_wmma_f32_16x16x16_bf16, VRegSrc_256, WMMARegular, 1>;
  defm V_WMMA_F16_16X16X16_F16   : WMMAInst<"_w32", "v_wmma_f16_16x16x16_f16",   VOP_V16F16_V16F16_V16F16_V16F16, int_amdgcn_wmma_f16_16x16x16_f16, VRegSrc_256, WMMAOpSel, 1>;
  defm V_WMMA_BF16_16X16X16_BF16 : WMMAInst<"_w32", "v_wmma_bf16_16x16x16_bf16", VOP_V16I16_V16I16_V16I16_V16I16, int_amdgcn_wmma_bf16_16x16x16_bf16, VRegSrc_256, WMMAOpSel, 1>;
  defm V_WMMA_F16_16X16X16_F16_TIED   : WMMAInst<"_w32", "v_wmma_f16_16x16x16_f16",   VOP_V16F16_V16F16_V16F16_V16F16, int_amdgcn_wmma_f16_16x16x16_f16_tied, VRegSrc_256, WMMAOpSel, 0>;
  defm V_WMMA_BF16_16X16X16_BF16_TIED : WMMAInst<"_w32", "v_wmma_bf16_16x16x16_bf16", VOP_V16I16_V16I16_V16I16_V16I16, int_amdgcn_wmma_bf16_16x16x16_bf16_tied, VRegSrc_256, WMMAOpSel, 0>;
  defm V_WMMA_I32_16X16X16_IU8   : WMMAInst<"_w32", "v_wmma_i32_16x16x16_iu8",   VOP_V8I32_V4I32_V4I32_V8I32, int_amdgcn_wmma_i32_16x16x16_iu8, VRegSrc_128, WMMAUIClamp, 1>;
  defm V_WMMA_I32_16X16X16_IU4   : WMMAInst<"_w32", "v_wmma_i32_16x16x16_iu4",   VOP_V8I32_V2I32_V2I32_V8I32, int_amdgcn_wmma_i32_16x16x16_iu4, VRegSrc_64,  WMMAUIClamp, 1>;
}

let WaveSizePredicate = isWave64 in {
  defm V_WMMA_F32_16X16X16_F16   : WMMAInst<"_w64", "v_wmma_f32_16x16x16_f16",   VOP_V4F32_V16F16_V16F16_V4F32, int_amdgcn_wmma_f32_16x16x16_f16, VRegSrc_256, WMMARegular, 1>;
  defm V_WMMA_F32_16X16X16_BF16  : WMMAInst<"_w64", "v_wmma_f32_16x16x16_bf16",  VOP_V4F32_V16I16_V16I16_V4F32, int_amdgcn_wmma_f32_16x16x16_bf16, VRegSrc_256, WMMARegular, 1>;
  defm V_WMMA_F16_16X16X16_F16   : WMMAInst<"_w64", "v_wmma_f16_16x16x16_f16",   VOP_V8F16_V16F16_V16F16_V8F16, int_amdgcn_wmma_f16_16x16x16_f16, VRegSrc_256, WMMAOpSel, 1>;
  defm V_WMMA_BF16_16X16X16_BF16 : WMMAInst<"_w64", "v_wmma_bf16_16x16x16_bf16", VOP_V8I16_V16I16_V16I16_V8I16, int_amdgcn_wmma_bf16_16x16x16_bf16, VRegSrc_256, WMMAOpSel, 1>;
  defm V_WMMA_F16_16X16X16_F16_TIED   : WMMAInst<"_w64", "v_wmma_f16_16x16x16_f16",   VOP_V8F16_V16F16_V16F16_V8F16, int_amdgcn_wmma_f16_16x16x16_f16_tied, VRegSrc_256, WMMAOpSel, 0>;
  defm V_WMMA_BF16_16X16X16_BF16_TIED : WMMAInst<"_w64", "v_wmma_bf16_16x16x16_bf16", VOP_V8I16_V16I16_V16I16_V8I16, int_amdgcn_wmma_bf16_16x16x16_bf16_tied, VRegSrc_256, WMMAOpSel, 0>;
  defm V_WMMA_I32_16X16X16_IU8   : WMMAInst<"_w64", "v_wmma_i32_16x16x16_iu8",   VOP_V4I32_V4I32_V4I32_V4I32, int_amdgcn_wmma_i32_16x16x16_iu8, VRegSrc_128, WMMAUIClamp, 1>;
  defm V_WMMA_I32_16X16X16_IU4   : WMMAInst<"_w64", "v_wmma_i32_16x16x16_iu4",   VOP_V4I32_V2I32_V2I32_V4I32, int_amdgcn_wmma_i32_16x16x16_iu4, VRegSrc_64, WMMAUIClamp, 1>;

}

class VOP3PWMMA_Profile<list<ValueType> ArgTy, bit _IsSWMMAC, int _IndexType,
                        bit _IsIU, bit _IsFP8BF8>
    : VOP3P_Profile<VOPProfile<ArgTy>> {
  bit IsIU = _IsIU;
  bit IsFP8BF8 = _IsFP8BF8;
  bit IsF16BF16 = !not(!or(IsIU, IsFP8BF8));

  int IndexType = _IndexType;

  let IsPacked = 1;
  let IsWMMA = !not(_IsSWMMAC);
  let IsSWMMAC = _IsSWMMAC;

  bit IsAB_F16 = !and(IsF16BF16, ArgTy[1].isFP);
  bit IsAB_BF16 = !and(IsF16BF16, isIntType<ArgTy[1]>.ret);
  bit IsC_F32 = !or(!eq(ArgTy[3], v8f32), !eq(ArgTy[3], v4f32));
  bit IsC_BF16 = !or(!eq(ArgTy[3], v8i16), !eq(ArgTy[3], v4i16));
  bit IsC_F16 = !or(!eq(ArgTy[3], v8f16), !eq(ArgTy[3], v4f16));

  bit NegLo01 = !or(IsF16BF16, IsIU);
  bit NegLo2 = !and(!or(IsF16BF16, IsFP8BF8), IsWMMA);
  bit NegHi01 = IsF16BF16;
  bit NegHi2 = !and(!or(IsF16BF16, IsFP8BF8), IsWMMA);
  bit NegLoAny = !or(NegLo01, NegLo2);
  bit NegHiAny = !or(NegHi01, NegHi2);

  let DstRC = !cast<RegisterOperand>("VDst_"#ArgTy[0].Size);
  let Src0RC64 = !cast<RegisterOperand>("VRegSrc_"#ArgTy[1].Size);
  let Src1RC64 = !cast<RegisterOperand>("VRegSrc_"#ArgTy[2].Size);
  let Src2RC64 = !if(IsSWMMAC, DstRC,
                               !cast<RegisterOperand>("VISrc_"#ArgTy[3].Size#
                                                      !cond(IsC_F32: "_f32",
                                                            IsC_F16: "_f16",
                                                            IsC_BF16: "_bf16",
                                                            1: "_b32")));

  // For f16 and bf16 matrices A and B, each element can be modified by
  // fneg(neg_lo,neg_hi = 1). For iu4 and iu8 matrices A and B neg_lo is
  // overloaded to mean unsigned/signed: neg_lo = 0 (u4 and u8) unsigned(zext)
  // neg_lo = 1 (i4 and i8) signed(sext). For f16, bf16 and f32 matrix C each
  // element can be modified by fneg(neg_lo = 1) or fabs(neg_hi = 1).

  // Opcode             | src0/src1 - matrix A/B | src2 - matrix C or Index
  // ---------------------------------------------------------------------------
  // wmma f32_f16       | both neg_lo,neg_hi = 1 | neg_lo = 1  neg C(f32)
  // wmma f32_bf16      | neg A/B (f16 or bf16)  | neg_hi = 1  abs C(f32)
  // ---------------------------------------------------------------------------
  // wmma f16_f16       | both neg_lo,neg_hi = 1 | neg_lo = 1 neg C(f16 or bf16)
  // wmma bf16_bf16     | neg A/B (f16 or bf16)  | neg_hi = 1 abs C(f16 or bf16)
  // ---------------------------------------------------------------------------
  // wmma i32_iu8/iu4   | neg_lo = 0 u4/u8(zext) | not allowed for
  //                    | neg_lo = 1 i4/i8(sext) | i32 matrices
  // ---------------------------------------------------------------------------
  // wmma f32_fp8/bf8   | not allowed for        | neg_lo = 1  neg C(f32)
  // (4 instructions)   | f8 and bf8 matrices    | neg_hi = 1  abs C(f32)
  // ---------------------------------------------------------------------------
  // swmmac f32_f16     | both neg_lo,neg_hi = 1 | not allowed for sparse matrix
  // swmmac f32_bf16    | neg A/B (f16 or bf16)  | A Index - matrix C is in dst
  // ---------------------------------------------------------------------------
  // swmmac f16_f16     | both neg_lo,neg_hi = 1 | not allowed for sparse matrix
  // swmmac bf16_bf16   | neg A/B (f16 or bf16)  | A Index - matrix C is in dst
  // ---------------------------------------------------------------------------
  // swmmac i32_iu8/iu4 | neg_lo = 0 u4/u8(zext) | not allowed for sparse matrix
  //                    | neg_lo = 1 i4/i8(sext) | A Index - matrix C is in dst
  // ---------------------------------------------------------------------------
  // swmmac f32_fp8/bf8 | not allowed for        | not allowed for sparse matrix
  // (4 instructions)   | f8 and bf8 matrices    | A Index - matrix C is in dst

  // pseudo

  // fp8bf8 wmmas don't use src (0 and 1) modifiers, iu use neg_lo, f16 and bf16
  // use neg_lo and neg_hi. iu wmmas (C is i32) don't use src 2 modifiers,
  // remaining wmmas(f16, bf16 and f8bf8) use neg_lo and neg_hi for C (C is f32
  // f16 or bf16). swmmac use index_key and don't use src 2 modifiers.

  dag Src0Mods = !if(IsFP8BF8, (ins), (ins PackedF16InputMods:$src0_modifiers));
  dag Src1Mods = !if(IsFP8BF8, (ins), (ins PackedF16InputMods:$src1_modifiers));
  dag Src2Mods = !if(IsIU, (ins), (ins PackedF16InputMods:$src2_modifiers));
  dag IndexKey = !cond(!eq(IndexType, 0) : (ins),
                       !eq(IndexType, 8) : (ins IndexKey8bit:$index_key_8bit),
                       !eq(IndexType, 16): (ins IndexKey16bit:$index_key_16bit));
  dag Clamp = !if(IsIU, (ins Clamp0:$clamp), (ins));
  dag Neg = !cond(!and(NegLoAny, NegHiAny)             : (ins neg_lo0:$neg_lo, neg_hi0:$neg_hi),
                  !and(NegLoAny, !not(NegHiAny))       : (ins neg_lo0:$neg_lo),
                  !and(!not(NegLoAny), !not(NegHiAny)) : (ins));

  let InsVOP3P = !con(Src0Mods, (ins Src0RC64:$src0), Src1Mods, (ins Src1RC64:$src1),
                      !cond(IsWMMA   : !con(Src2Mods, (ins Src2RC64:$src2)),
                            IsSWMMAC : !con((ins DstRC:$srcTiedDef), (ins VRegSrc_32:$src2), IndexKey)),
                      Clamp, Neg);

  // asm

  string IndexKeyAsm = !cond(!eq(IndexType, 0)  : "",
                             !eq(IndexType, 8)  : "$index_key_8bit",
                             !eq(IndexType, 16) : "$index_key_16bit");
  string ClampAsm = !if(IsIU, "$clamp", "");
  string NegAsm = !cond(!and(NegLoAny, NegHiAny)             : "$neg_lo$neg_hi",
                        !and(NegLoAny, !not(NegHiAny))       : "$neg_lo",
                        !and(!not(NegLoAny), !not(NegHiAny)) : "");

  let AsmVOP3P = "$vdst, $src0, $src1, $src2"#IndexKeyAsm#NegAsm#ClampAsm;

  // isel patterns

  dag Src0InPat  = !cond(IsAB_F16  : (ins (Src0VT (WMMAModsF16Neg Src0VT:$src0, i32:$src0_modifiers))),
                         IsAB_BF16 : (ins Src0VT:$src0),
                         IsIU      : (ins (VOP3PModsNeg i32:$src0_modifiers), Src0VT:$src0),
                         IsFP8BF8  : (ins Src0VT:$src0));
  dag Src0OutPat = !cond(IsAB_F16  : (ins i32:$src0_modifiers, Src0VT:$src0),
                         IsAB_BF16 : (ins (i32 8), Src0VT:$src0),
                         IsIU      : (ins i32:$src0_modifiers, Src0VT:$src0),
                         IsFP8BF8  : (ins Src0VT:$src0));
  dag Src1InPat  = !cond(IsAB_F16  : (ins (Src1VT (WMMAModsF16Neg Src1VT:$src1, i32:$src1_modifiers))),
                         IsAB_BF16 : (ins Src1VT:$src1),
                         IsIU      : (ins (VOP3PModsNeg i32:$src1_modifiers), Src1VT:$src1),
                         IsFP8BF8  : (ins Src1VT:$src1));
  dag Src1OutPat = !cond(IsAB_F16  : (ins i32:$src1_modifiers, Src1VT:$src1),
                         IsAB_BF16 : (ins (i32 8), Src1VT:$src1),
                         IsIU      : (ins i32:$src1_modifiers, Src1VT:$src1),
                         IsFP8BF8  : (ins Src1VT:$src1));
  dag Src2InPatWmma  = !cond(IsC_F32  : (ins (Src2VT (WMMAModsF32NegAbs Src2VT:$src2, i32:$src2_modifiers))),
                             IsC_F16  : (ins (Src2VT (WMMAModsF16NegAbs Src2VT:$src2, i32:$src2_modifiers))),
                             IsC_BF16 : (ins Src2VT:$src2),
                             IsIU     : (ins Src2VT:$src2),
                             IsSWMMAC : (ins));
  dag Src2OutPatWmma = !cond(IsC_F32  : (ins i32:$src2_modifiers, Src2VT:$src2),
                             IsC_F16  : (ins i32:$src2_modifiers, Src2VT:$src2),
                             IsC_BF16 : (ins (i32 8), Src2VT:$src2),
                             IsIU     : (ins Src2VT:$src2),
                             IsSWMMAC : (ins));
  dag ClampPat = !if(IsIU, (ins i1:$clamp), (ins));
  dag IndexInPat = !cond(!eq(IndexType, 0) : (ins i32:$src2),
                         !eq(IndexType, 8) : (ins (i32 (SWMMACIndex8 i32:$src2, i32:$index_key_8bit))),
                         !eq(IndexType, 16): (ins (i32 (SWMMACIndex16 i32:$src2, i32:$index_key_16bit))));
  dag IndexOutPat = !cond(!eq(IndexType, 0) : (ins i32:$src2),
                          !eq(IndexType, 8) : (ins i32:$src2, i32:$index_key_8bit),
                          !eq(IndexType, 16): (ins i32:$src2, i32:$index_key_16bit));
  dag Src2InlineInPat = (ins (Src2VT (WMMAVISrc Src2VT:$src2)));
  dag Src2InlineOutPat = !con(!if(IsIU, (ins), (ins (i32 8))), (ins Src2VT:$src2));


  dag WmmaInPat  = !con(Src0InPat, Src1InPat, Src2InPatWmma, ClampPat);
  dag WmmaOutPat = !con(Src0OutPat, Src1OutPat, Src2OutPatWmma, ClampPat);

  dag SwmmacInPat  = !con(Src0InPat, Src1InPat, (ins Src2VT:$srcTiedDef), IndexInPat, ClampPat);
  dag SwmmacOutPat = !con(Src0OutPat, Src1OutPat, (ins Src2VT:$srcTiedDef), IndexOutPat, ClampPat);

  // wmma pattern where src2 is inline imm uses _threeaddr pseudo,
  // can't use _twoaddr since it would violate src2 tied to vdst constraint.
  dag WmmaInlineInPat  = !con(Src0InPat, Src1InPat, Src2InlineInPat,  ClampPat);
  dag WmmaInlineOutPat = !con(Src0OutPat, Src1OutPat, Src2InlineOutPat, ClampPat);
}

multiclass WMMAInstGFX12<string Instr, VOP3PWMMA_Profile WMMAProfile, string PseudoInstrSuffix> {
  let Mnemonic = Instr, mayRaiseFPException = 0, ReadsModeReg = 0 in {
    let Constraints = "@earlyclobber $vdst,$vdst = $src2", isConvertibleToThreeAddress = 1 in
      def _twoaddr : VOP3P_Pseudo<Instr, WMMAProfile>{
        let PseudoInstr = Instr#PseudoInstrSuffix;
      }

    let Constraints = "@earlyclobber $vdst", SchedRW = [Write32Bit, Write32Bit] in
      def _threeaddr : VOP3P_Pseudo<Instr, WMMAProfile>{
        let PseudoInstr = Instr#PseudoInstrSuffix;
      }

  }
  def : WMMAOpcodeMapping<!cast<Instruction>(NAME # _twoaddr),
                          !cast<Instruction>(NAME # _threeaddr)>;
}

multiclass SWMMACInstGFX12<string Instr, VOP3PWMMA_Profile WMMAProfile, string PseudoInstrSuffix> {
  def _twoaddr : VOP3P_Pseudo<Instr, WMMAProfile>{
    let Mnemonic = Instr;
    let PseudoInstr = Instr#PseudoInstrSuffix;
    let mayRaiseFPException = 0;
    let ReadsModeReg = 0;
    let AsmMatchConverter = "cvtSWMMAC";

    let Constraints = "@earlyclobber $vdst,$vdst = $srcTiedDef";
  }
}

// First argument in Profile is types for matrices D, A, B and C (D = A * B + C)
// as used by llvm ir, types are vectors(with matrix elements)
// wave32:
// For 16x16 matrices, lanes 0 to 31 will have 8 matrix elts,
// for 16 x 32 16 elts and for 16 x 64 lanes have 32 elts.
// wave64:
// lanes will have half the size of elements in lanes compared to wave32 with
// exception of 16x16_iu4: lanes0-31 will have 8xi4, remaining lanes are ignored

// general idea on element distribution differences:
// wave32: lane n has 8 matrix elements
// wave64: lane n has first 4, lane n+32 has other 4 elements

// index size, for each 2 elements in lane you need 4bits in index

// Non-standard types (iu8, iu4, fp8, bf8) will be packed in vectors of i32s.
// Original type for them is in comment on the right and refers to A and B.

def F32_F16_WMMA_w32    : VOP3PWMMA_Profile<[v8f32, v8f16, v8f16, v8f32], 0, 0, 0, 0>;
def F32_BF16_WMMA_w32   : VOP3PWMMA_Profile<[v8f32, v8i16, v8i16, v8f32], 0, 0, 0, 0>;
def F16_F16_WMMA_w32    : VOP3PWMMA_Profile<[v8f16, v8f16, v8f16, v8f16], 0, 0, 0, 0>;
def BF16_BF16_WMMA_w32  : VOP3PWMMA_Profile<[v8i16, v8i16, v8i16, v8i16], 0, 0, 0, 0>;
def I32_IU8_WMMA_w32    : VOP3PWMMA_Profile<[v8i32, v2i32, v2i32, v8i32], 0, 0, 1, 0>; // 8xi8
def I32_IU4X16_WMMA_w32 : VOP3PWMMA_Profile<[v8i32,   i32,   i32, v8i32], 0, 0, 1, 0>; // 8xi4
def F32_FP8BF8_WMMA_w32 : VOP3PWMMA_Profile<[v8f32, v2i32, v2i32, v8f32], 0, 0, 0, 1>; // 8xf8
def I32_IU4X32_WMMA_w32 : VOP3PWMMA_Profile<[v8i32, v2i32, v2i32, v8i32], 0, 0, 1, 0>; // 16xi4

def F32_F16_WMMA_w64    : VOP3PWMMA_Profile<[v4f32, v4f16, v4f16, v4f32], 0, 0, 0, 0>;
def F32_BF16_WMMA_w64   : VOP3PWMMA_Profile<[v4f32, v4i16, v4i16, v4f32], 0, 0, 0, 0>;
def F16_F16_WMMA_w64    : VOP3PWMMA_Profile<[v4f16, v4f16, v4f16, v4f16], 0, 0, 0, 0>;
def BF16_BF16_WMMA_w64  : VOP3PWMMA_Profile<[v4i16, v4i16, v4i16, v4i16], 0, 0, 0, 0>;
def I32_IU8_WMMA_w64    : VOP3PWMMA_Profile<[v4i32,   i32,   i32, v4i32], 0, 0, 1, 0>; // 4xi8
def I32_IU4X16_WMMA_w64 : VOP3PWMMA_Profile<[v4i32,   i32,   i32, v4i32], 0, 0, 1, 0>; // 8xi4 *
def F32_FP8BF8_WMMA_w64 : VOP3PWMMA_Profile<[v4f32,   i32,   i32, v4f32], 0, 0, 0, 1>; // 4xf8
def I32_IU4X32_WMMA_w64 : VOP3PWMMA_Profile<[v4i32,   i32,   i32, v4i32], 0, 0, 1, 0>; // 8xi4

def F32_F16_SWMMAC_w32    : VOP3PWMMA_Profile<[v8f32, v8f16, v16f16, v8f32], 1, 16, 0, 0>;
def F32_BF16_SWMMAC_w32   : VOP3PWMMA_Profile<[v8f32, v8i16, v16i16, v8f32], 1, 16, 0, 0>;
def F16_F16_SWMMAC_w32    : VOP3PWMMA_Profile<[v8f16, v8f16, v16f16, v8f16], 1, 16, 0, 0>;
def BF16_BF16_SWMMAC_w32  : VOP3PWMMA_Profile<[v8i16, v8i16, v16i16, v8i16], 1, 16, 0, 0>;
def I32_IU8_SWMMAC_w32    : VOP3PWMMA_Profile<[v8i32, v2i32,  v4i32, v8i32], 1, 16, 1, 0>; // 8xi8, 16xi8
def I32_IU4X32_SWMMAC_w32 : VOP3PWMMA_Profile<[v8i32,   i32,  v2i32, v8i32], 1, 16, 1, 0>; // 8xi4, 16xi4
def I32_IU4X64_SWMMAC_w32 : VOP3PWMMA_Profile<[v8i32, v2i32,  v4i32, v8i32], 1,  0, 1, 0>; // 16xi4, 32xi4 **
def F32_FP8BF8_SWMMAC_w32 : VOP3PWMMA_Profile<[v8f32, v2i32,  v4i32, v8f32], 1, 16, 0, 1>; // 8xf8, 16xf8

def F32_F16_SWMMAC_w64    : VOP3PWMMA_Profile<[v4f32, v4f16, v8f16, v4f32], 1,  8, 0, 0>;
def F32_BF16_SWMMAC_w64   : VOP3PWMMA_Profile<[v4f32, v4i16, v8i16, v4f32], 1,  8, 0, 0>;
def F16_F16_SWMMAC_w64    : VOP3PWMMA_Profile<[v4f16, v4f16, v8f16, v4f16], 1,  8, 0, 0>;
def BF16_BF16_SWMMAC_w64  : VOP3PWMMA_Profile<[v4i16, v4i16, v8i16, v4i16], 1,  8, 0, 0>;
def I32_IU8_SWMMAC_w64    : VOP3PWMMA_Profile<[v4i32,   i32, v2i32, v4i32], 1,  8, 1, 0>; // 4xi8, 8xi8
def I32_IU4X32_SWMMAC_w64 : VOP3PWMMA_Profile<[v4i32,   i32,   i32, v4i32], 1, 16, 1, 0>; // 8xi4, 8xi4 ***
def I32_IU4X64_SWMMAC_w64 : VOP3PWMMA_Profile<[v4i32,   i32, v2i32, v4i32], 1, 16, 1, 0>; // 8xi4, 16xi4
def F32_FP8BF8_SWMMAC_w64 : VOP3PWMMA_Profile<[v4f32,   i32, v2i32, v4f32], 1,  8, 0, 1>; // 4xf8, 8xf8

// *   IU4X16_WMMA_w64 lanes 0-31 will have 8xi4, remaining lanes are ignored
// **  IU4X64_SWMMAC_w32 index is i32, index_key is not used
// *** IU4X32_SWMMAC_w64 lanes 0-31 will have 8xi4 remaining lanes are ignored
//                       for matrix A, index is i16; Matrix B uses all lanes

let WaveSizePredicate = isWave32 in {
defm V_WMMA_F32_16X16X16_F16_w32     : WMMAInstGFX12<"v_wmma_f32_16x16x16_f16",     F32_F16_WMMA_w32, "_w32">;
defm V_WMMA_F32_16X16X16_BF16_w32    : WMMAInstGFX12<"v_wmma_f32_16x16x16_bf16",    F32_BF16_WMMA_w32, "_w32">;
defm V_WMMA_F16_16X16X16_F16_w32     : WMMAInstGFX12<"v_wmma_f16_16x16x16_f16",     F16_F16_WMMA_w32, "_w32">;
defm V_WMMA_BF16_16X16X16_BF16_w32   : WMMAInstGFX12<"v_wmma_bf16_16x16x16_bf16",   BF16_BF16_WMMA_w32, "_w32">;
defm V_WMMA_I32_16X16X16_IU8_w32     : WMMAInstGFX12<"v_wmma_i32_16x16x16_iu8",     I32_IU8_WMMA_w32, "_w32">;
defm V_WMMA_I32_16X16X16_IU4_w32     : WMMAInstGFX12<"v_wmma_i32_16x16x16_iu4",     I32_IU4X16_WMMA_w32, "_w32">;
defm V_WMMA_F32_16X16X16_FP8_FP8_w32 : WMMAInstGFX12<"v_wmma_f32_16x16x16_fp8_fp8", F32_FP8BF8_WMMA_w32, "_w32">;
defm V_WMMA_F32_16X16X16_FP8_BF8_w32 : WMMAInstGFX12<"v_wmma_f32_16x16x16_fp8_bf8", F32_FP8BF8_WMMA_w32, "_w32">;
defm V_WMMA_F32_16X16X16_BF8_FP8_w32 : WMMAInstGFX12<"v_wmma_f32_16x16x16_bf8_fp8", F32_FP8BF8_WMMA_w32, "_w32">;
defm V_WMMA_F32_16X16X16_BF8_BF8_w32 : WMMAInstGFX12<"v_wmma_f32_16x16x16_bf8_bf8", F32_FP8BF8_WMMA_w32, "_w32">;
defm V_WMMA_I32_16X16X32_IU4_w32     : WMMAInstGFX12<"v_wmma_i32_16x16x32_iu4",     I32_IU4X32_WMMA_w32, "_w32">;

defm V_SWMMAC_F32_16X16X32_F16_w32     : SWMMACInstGFX12<"v_swmmac_f32_16x16x32_f16",     F32_F16_SWMMAC_w32, "_w32">;
defm V_SWMMAC_F32_16X16X32_BF16_w32    : SWMMACInstGFX12<"v_swmmac_f32_16x16x32_bf16",    F32_BF16_SWMMAC_w32, "_w32">;
defm V_SWMMAC_F16_16X16X32_F16_w32     : SWMMACInstGFX12<"v_swmmac_f16_16x16x32_f16",     F16_F16_SWMMAC_w32, "_w32">;
defm V_SWMMAC_BF16_16X16X32_BF16_w32   : SWMMACInstGFX12<"v_swmmac_bf16_16x16x32_bf16",   BF16_BF16_SWMMAC_w32, "_w32">;
defm V_SWMMAC_I32_16X16X32_IU8_w32     : SWMMACInstGFX12<"v_swmmac_i32_16x16x32_iu8",     I32_IU8_SWMMAC_w32, "_w32">;
defm V_SWMMAC_I32_16X16X32_IU4_w32     : SWMMACInstGFX12<"v_swmmac_i32_16x16x32_iu4",     I32_IU4X32_SWMMAC_w32, "_w32">;
defm V_SWMMAC_I32_16X16X64_IU4_w32     : SWMMACInstGFX12<"v_swmmac_i32_16x16x64_iu4",     I32_IU4X64_SWMMAC_w32, "_w32">;
defm V_SWMMAC_F32_16X16X32_FP8_FP8_w32 : SWMMACInstGFX12<"v_swmmac_f32_16x16x32_fp8_fp8", F32_FP8BF8_SWMMAC_w32, "_w32">;
defm V_SWMMAC_F32_16X16X32_FP8_BF8_w32 : SWMMACInstGFX12<"v_swmmac_f32_16x16x32_fp8_bf8", F32_FP8BF8_SWMMAC_w32, "_w32">;
defm V_SWMMAC_F32_16X16X32_BF8_FP8_w32 : SWMMACInstGFX12<"v_swmmac_f32_16x16x32_bf8_fp8", F32_FP8BF8_SWMMAC_w32, "_w32">;
defm V_SWMMAC_F32_16X16X32_BF8_BF8_w32 : SWMMACInstGFX12<"v_swmmac_f32_16x16x32_bf8_bf8", F32_FP8BF8_SWMMAC_w32, "_w32">;
}

let WaveSizePredicate = isWave64 in {
defm V_WMMA_F32_16X16X16_F16_w64     : WMMAInstGFX12<"v_wmma_f32_16x16x16_f16",     F32_F16_WMMA_w64, "_w64">;
defm V_WMMA_F32_16X16X16_BF16_w64    : WMMAInstGFX12<"v_wmma_f32_16x16x16_bf16",    F32_BF16_WMMA_w64, "_w64">;
defm V_WMMA_F16_16X16X16_F16_w64     : WMMAInstGFX12<"v_wmma_f16_16x16x16_f16",     F16_F16_WMMA_w64, "_w64">;
defm V_WMMA_BF16_16X16X16_BF16_w64   : WMMAInstGFX12<"v_wmma_bf16_16x16x16_bf16",   BF16_BF16_WMMA_w64, "_w64">;
defm V_WMMA_I32_16X16X16_IU8_w64     : WMMAInstGFX12<"v_wmma_i32_16x16x16_iu8",     I32_IU8_WMMA_w64, "_w64">;
defm V_WMMA_I32_16X16X16_IU4_w64     : WMMAInstGFX12<"v_wmma_i32_16x16x16_iu4",     I32_IU4X16_WMMA_w64, "_w64">;
defm V_WMMA_F32_16X16X16_FP8_FP8_w64 : WMMAInstGFX12<"v_wmma_f32_16x16x16_fp8_fp8", F32_FP8BF8_WMMA_w64, "_w64">;
defm V_WMMA_F32_16X16X16_FP8_BF8_w64 : WMMAInstGFX12<"v_wmma_f32_16x16x16_fp8_bf8", F32_FP8BF8_WMMA_w64, "_w64">;
defm V_WMMA_F32_16X16X16_BF8_FP8_w64 : WMMAInstGFX12<"v_wmma_f32_16x16x16_bf8_fp8", F32_FP8BF8_WMMA_w64, "_w64">;
defm V_WMMA_F32_16X16X16_BF8_BF8_w64 : WMMAInstGFX12<"v_wmma_f32_16x16x16_bf8_bf8", F32_FP8BF8_WMMA_w64, "_w64">;
defm V_WMMA_I32_16X16X32_IU4_w64     : WMMAInstGFX12<"v_wmma_i32_16x16x32_iu4",     I32_IU4X32_WMMA_w64, "_w64">;

defm V_SWMMAC_F32_16X16X32_F16_w64     : SWMMACInstGFX12<"v_swmmac_f32_16x16x32_f16",     F32_F16_SWMMAC_w64, "_w64">;
defm V_SWMMAC_F32_16X16X32_BF16_w64    : SWMMACInstGFX12<"v_swmmac_f32_16x16x32_bf16",    F32_BF16_SWMMAC_w64, "_w64">;
defm V_SWMMAC_F16_16X16X32_F16_w64     : SWMMACInstGFX12<"v_swmmac_f16_16x16x32_f16",     F16_F16_SWMMAC_w64, "_w64">;
defm V_SWMMAC_BF16_16X16X32_BF16_w64   : SWMMACInstGFX12<"v_swmmac_bf16_16x16x32_bf16",   BF16_BF16_SWMMAC_w64, "_w64">;
defm V_SWMMAC_I32_16X16X32_IU8_w64     : SWMMACInstGFX12<"v_swmmac_i32_16x16x32_iu8",     I32_IU8_SWMMAC_w64, "_w64">;
defm V_SWMMAC_I32_16X16X32_IU4_w64     : SWMMACInstGFX12<"v_swmmac_i32_16x16x32_iu4",     I32_IU4X32_SWMMAC_w64, "_w64">;
defm V_SWMMAC_I32_16X16X64_IU4_w64     : SWMMACInstGFX12<"v_swmmac_i32_16x16x64_iu4",     I32_IU4X64_SWMMAC_w64, "_w64">;
defm V_SWMMAC_F32_16X16X32_FP8_FP8_w64 : SWMMACInstGFX12<"v_swmmac_f32_16x16x32_fp8_fp8", F32_FP8BF8_SWMMAC_w64, "_w64">;
defm V_SWMMAC_F32_16X16X32_FP8_BF8_w64 : SWMMACInstGFX12<"v_swmmac_f32_16x16x32_fp8_bf8", F32_FP8BF8_SWMMAC_w64, "_w64">;
defm V_SWMMAC_F32_16X16X32_BF8_FP8_w64 : SWMMACInstGFX12<"v_swmmac_f32_16x16x32_bf8_fp8", F32_FP8BF8_SWMMAC_w64, "_w64">;
defm V_SWMMAC_F32_16X16X32_BF8_BF8_w64 : SWMMACInstGFX12<"v_swmmac_f32_16x16x32_bf8_bf8", F32_FP8BF8_SWMMAC_w64, "_w64">;
}

// IsGFX11OpselIntrinsic: f16_f16 and bf16_bf16 Intrinsics have imm operand that
// controls opsel. Used by gfx11, removed in gfx12 (operand must be 0).
multiclass WMMAPat<string Inst, SDPatternOperator node, VOP3PWMMA_Profile P, bit IsGFX11OpselIntrinsic = 0> {
  def : GCNPat <(P.DstVT !setdagop(!con(P.WmmaInPat, !if(IsGFX11OpselIntrinsic, (ins 0), (ins))), node)),
                (P.DstVT !setdagop(P.WmmaOutPat, !cast<Instruction>(Inst#"_twoaddr")))>;
  let AddedComplexity = 4 in
  def : GCNPat <(P.DstVT !setdagop(!con(P.WmmaInlineInPat, !if(IsGFX11OpselIntrinsic, (ins 0), (ins))), node)),
                (P.DstVT !setdagop(P.WmmaInlineOutPat, !cast<Instruction>(Inst#"_threeaddr")))>;
}

class SWMMACPat<Instruction Inst, SDPatternOperator node, VOP3PWMMA_Profile P> :
  GCNPat <(P.DstVT !setdagop(P.SwmmacInPat, node)),
          (P.DstVT !setdagop(P.SwmmacOutPat, Inst))>;

class SWMMACPat_w64<Instruction Inst, SDPatternOperator node, VOP3PWMMA_Profile P> :
  GCNPat <(P.DstVT !setdagop(P.SwmmacInPat, node)),
          (P.DstVT !setdagop(P.SwmmacOutPat, Inst))>{
            let WaveSizePredicate = isWave64;
          }

let WaveSizePredicate = isWave32, SubtargetPredicate = isGFX12Plus in {
  defm : WMMAPat<"V_WMMA_F32_16X16X16_F16_w32",     int_amdgcn_wmma_f32_16x16x16_f16,     F32_F16_WMMA_w32>;
  defm : WMMAPat<"V_WMMA_F32_16X16X16_BF16_w32",    int_amdgcn_wmma_f32_16x16x16_bf16,    F32_BF16_WMMA_w32>;
  defm : WMMAPat<"V_WMMA_F16_16X16X16_F16_w32",     int_amdgcn_wmma_f16_16x16x16_f16,     F16_F16_WMMA_w32,1>;
  defm : WMMAPat<"V_WMMA_BF16_16X16X16_BF16_w32",   int_amdgcn_wmma_bf16_16x16x16_bf16,   BF16_BF16_WMMA_w32,1>;
  defm : WMMAPat<"V_WMMA_I32_16X16X16_IU8_w32",     int_amdgcn_wmma_i32_16x16x16_iu8,     I32_IU8_WMMA_w32>;
  defm : WMMAPat<"V_WMMA_I32_16X16X16_IU4_w32",     int_amdgcn_wmma_i32_16x16x16_iu4,     I32_IU4X16_WMMA_w32>;
  defm : WMMAPat<"V_WMMA_F32_16X16X16_FP8_FP8_w32", int_amdgcn_wmma_f32_16x16x16_fp8_fp8, F32_FP8BF8_WMMA_w32>;
  defm : WMMAPat<"V_WMMA_F32_16X16X16_FP8_BF8_w32", int_amdgcn_wmma_f32_16x16x16_fp8_bf8, F32_FP8BF8_WMMA_w32>;
  defm : WMMAPat<"V_WMMA_F32_16X16X16_BF8_FP8_w32", int_amdgcn_wmma_f32_16x16x16_bf8_fp8, F32_FP8BF8_WMMA_w32>;
  defm : WMMAPat<"V_WMMA_F32_16X16X16_BF8_BF8_w32", int_amdgcn_wmma_f32_16x16x16_bf8_bf8, F32_FP8BF8_WMMA_w32>;
  defm : WMMAPat<"V_WMMA_I32_16X16X32_IU4_w32",     int_amdgcn_wmma_i32_16x16x32_iu4,     I32_IU4X32_WMMA_w32>;

  def : SWMMACPat<V_SWMMAC_F32_16X16X32_F16_w32_twoaddr,     int_amdgcn_swmmac_f32_16x16x32_f16,     F32_F16_SWMMAC_w32>;
  def : SWMMACPat<V_SWMMAC_F32_16X16X32_BF16_w32_twoaddr,    int_amdgcn_swmmac_f32_16x16x32_bf16,    F32_BF16_SWMMAC_w32>;
  def : SWMMACPat<V_SWMMAC_F16_16X16X32_F16_w32_twoaddr,     int_amdgcn_swmmac_f16_16x16x32_f16,     F16_F16_SWMMAC_w32>;
  def : SWMMACPat<V_SWMMAC_BF16_16X16X32_BF16_w32_twoaddr,   int_amdgcn_swmmac_bf16_16x16x32_bf16,   BF16_BF16_SWMMAC_w32>;
  def : SWMMACPat<V_SWMMAC_I32_16X16X32_IU8_w32_twoaddr,     int_amdgcn_swmmac_i32_16x16x32_iu8,     I32_IU8_SWMMAC_w32>;
  def : SWMMACPat<V_SWMMAC_I32_16X16X32_IU4_w32_twoaddr,     int_amdgcn_swmmac_i32_16x16x32_iu4,     I32_IU4X32_SWMMAC_w32>;
  def : GCNPat <(I32_IU4X64_SWMMAC_w32.DstVT !setdagop(I32_IU4X64_SWMMAC_w32.SwmmacInPat,  int_amdgcn_swmmac_i32_16x16x64_iu4)),
                (I32_IU4X64_SWMMAC_w32.DstVT !setdagop(I32_IU4X64_SWMMAC_w32.SwmmacOutPat, V_SWMMAC_I32_16X16X64_IU4_w32_twoaddr))>;
  def : SWMMACPat<V_SWMMAC_F32_16X16X32_FP8_FP8_w32_twoaddr, int_amdgcn_swmmac_f32_16x16x32_fp8_fp8, F32_FP8BF8_SWMMAC_w32>;
  def : SWMMACPat<V_SWMMAC_F32_16X16X32_FP8_BF8_w32_twoaddr, int_amdgcn_swmmac_f32_16x16x32_fp8_bf8, F32_FP8BF8_SWMMAC_w32>;
  def : SWMMACPat<V_SWMMAC_F32_16X16X32_BF8_FP8_w32_twoaddr, int_amdgcn_swmmac_f32_16x16x32_bf8_fp8, F32_FP8BF8_SWMMAC_w32>;
  def : SWMMACPat<V_SWMMAC_F32_16X16X32_BF8_BF8_w32_twoaddr, int_amdgcn_swmmac_f32_16x16x32_bf8_bf8, F32_FP8BF8_SWMMAC_w32>;
}

let WaveSizePredicate = isWave64, SubtargetPredicate = isGFX12Plus in {
  defm : WMMAPat<"V_WMMA_F32_16X16X16_F16_w64",     int_amdgcn_wmma_f32_16x16x16_f16,     F32_F16_WMMA_w64>;
  defm : WMMAPat<"V_WMMA_F32_16X16X16_BF16_w64",    int_amdgcn_wmma_f32_16x16x16_bf16,    F32_BF16_WMMA_w64>;
  defm : WMMAPat<"V_WMMA_F16_16X16X16_F16_w64",     int_amdgcn_wmma_f16_16x16x16_f16,     F16_F16_WMMA_w64,1>;
  defm : WMMAPat<"V_WMMA_BF16_16X16X16_BF16_w64",   int_amdgcn_wmma_bf16_16x16x16_bf16,   BF16_BF16_WMMA_w64,1>;
  defm : WMMAPat<"V_WMMA_I32_16X16X16_IU8_w64",     int_amdgcn_wmma_i32_16x16x16_iu8,     I32_IU8_WMMA_w64>;
  defm : WMMAPat<"V_WMMA_I32_16X16X16_IU4_w64",     int_amdgcn_wmma_i32_16x16x16_iu4,     I32_IU4X16_WMMA_w64>;
  defm : WMMAPat<"V_WMMA_F32_16X16X16_FP8_FP8_w64", int_amdgcn_wmma_f32_16x16x16_fp8_fp8, F32_FP8BF8_WMMA_w64>;
  defm : WMMAPat<"V_WMMA_F32_16X16X16_FP8_BF8_w64", int_amdgcn_wmma_f32_16x16x16_fp8_bf8, F32_FP8BF8_WMMA_w64>;
  defm : WMMAPat<"V_WMMA_F32_16X16X16_BF8_FP8_w64", int_amdgcn_wmma_f32_16x16x16_bf8_fp8, F32_FP8BF8_WMMA_w64>;
  defm : WMMAPat<"V_WMMA_F32_16X16X16_BF8_BF8_w64", int_amdgcn_wmma_f32_16x16x16_bf8_bf8, F32_FP8BF8_WMMA_w64>;
  defm : WMMAPat<"V_WMMA_I32_16X16X32_IU4_w64",     int_amdgcn_wmma_i32_16x16x32_iu4,     I32_IU4X32_WMMA_w64>;

  def : SWMMACPat<V_SWMMAC_F32_16X16X32_F16_w64_twoaddr,     int_amdgcn_swmmac_f32_16x16x32_f16,     F32_F16_SWMMAC_w64>;
  def : SWMMACPat<V_SWMMAC_F32_16X16X32_BF16_w64_twoaddr,    int_amdgcn_swmmac_f32_16x16x32_bf16,    F32_BF16_SWMMAC_w64>;
  def : SWMMACPat<V_SWMMAC_F16_16X16X32_F16_w64_twoaddr,     int_amdgcn_swmmac_f16_16x16x32_f16,     F16_F16_SWMMAC_w64>;
  def : SWMMACPat<V_SWMMAC_BF16_16X16X32_BF16_w64_twoaddr,   int_amdgcn_swmmac_bf16_16x16x32_bf16,   BF16_BF16_SWMMAC_w64>;
  def : SWMMACPat<V_SWMMAC_I32_16X16X32_IU8_w64_twoaddr,     int_amdgcn_swmmac_i32_16x16x32_iu8,     I32_IU8_SWMMAC_w64>;
  def : SWMMACPat<V_SWMMAC_I32_16X16X32_IU4_w64_twoaddr,     int_amdgcn_swmmac_i32_16x16x32_iu4,     I32_IU4X32_SWMMAC_w64>;
  def : SWMMACPat<V_SWMMAC_I32_16X16X64_IU4_w64_twoaddr,     int_amdgcn_swmmac_i32_16x16x64_iu4,     I32_IU4X64_SWMMAC_w64>;
  def : SWMMACPat<V_SWMMAC_F32_16X16X32_FP8_FP8_w64_twoaddr, int_amdgcn_swmmac_f32_16x16x32_fp8_fp8, F32_FP8BF8_SWMMAC_w64>;
  def : SWMMACPat<V_SWMMAC_F32_16X16X32_FP8_BF8_w64_twoaddr, int_amdgcn_swmmac_f32_16x16x32_fp8_bf8, F32_FP8BF8_SWMMAC_w64>;
  def : SWMMACPat<V_SWMMAC_F32_16X16X32_BF8_FP8_w64_twoaddr, int_amdgcn_swmmac_f32_16x16x32_bf8_fp8, F32_FP8BF8_SWMMAC_w64>;
  def : SWMMACPat<V_SWMMAC_F32_16X16X32_BF8_BF8_w64_twoaddr, int_amdgcn_swmmac_f32_16x16x32_bf8_bf8, F32_FP8BF8_SWMMAC_w64>;
}


//===----------------------------------------------------------------------===//
// Begin Real Encodings
//===----------------------------------------------------------------------===//

class VOP3P_DPP16<bits<7> op, VOP_DPP_Pseudo ps, int subtarget,
                  string opName = ps.OpName>
    : VOP3P_DPP<op, opName, ps.Pfl, 1>, SIMCInstr<ps.PseudoInstr, subtarget> {
  let hasSideEffects = ps.hasSideEffects;
  let Defs = ps.Defs;
  let SchedRW = ps.SchedRW;
  let Uses = ps.Uses;
  let AssemblerPredicate = HasDPP16;
  let SubtargetPredicate = ps.SubtargetPredicate;
  let OtherPredicates = ps.OtherPredicates;
  let IsPacked = ps.IsPacked;
}

class VOP3P_DPP8_Base<bits<7> op, VOP_Pseudo ps, string opName = ps.OpName>
    : VOP3P_DPP8<op, opName, ps.Pfl> {
  let hasSideEffects = ps.hasSideEffects;
  let Defs = ps.Defs;
  let SchedRW = ps.SchedRW;
  let Uses = ps.Uses;
  let SubtargetPredicate = ps.SubtargetPredicate;
  let OtherPredicates = ps.OtherPredicates;
  let IsPacked = ps.IsPacked;
}

//===----------------------------------------------------------------------===//
// GFX11, GFX12
//===----------------------------------------------------------------------===//

multiclass VOP3P_Real_Base<GFXGen Gen, bits<7> op, string backing_ps_name = NAME,
                      string asmName = !cast<VOP3P_Pseudo>(NAME).Mnemonic> {
  def Gen.Suffix :
    VOP3P_Real_Gen<!cast<VOP3P_Pseudo>(backing_ps_name), Gen, asmName>,
    VOP3Pe_gfx11_gfx12<op, !cast<VOP3P_Pseudo>(backing_ps_name).Pfl>;
}

class VOP3PeWmma<bits<7> op, VOPProfile P, VOP3PWMMA_Profile WMMAP>
    : VOP3Pe_gfx11_gfx12<op, P>{
  // opsel
  let Inst{11} = !cond(!eq(WMMAP.IndexType, 0)  : 0,
                       !eq(WMMAP.IndexType, 8)  : index_key_8bit{0},
                       !eq(WMMAP.IndexType, 16) : index_key_16bit{0});
  let Inst{12} = !if(!eq(WMMAP.IndexType, 8), index_key_8bit{1}, 0);
  let Inst{13} = 0;
  // opsel_hi
  let Inst{59} = 1;
  let Inst{60} = 1;
  let Inst{14} = 1;
  // neg_lo
  let Inst{61} = !if(WMMAP.NegLo01, src0_modifiers{0}, 0);
  let Inst{62} = !if(WMMAP.NegLo01, src1_modifiers{0}, 0);
  let Inst{63} = !if(WMMAP.NegLo2, src2_modifiers{0}, 0);
  // neg_hi
  let Inst{8}  = !if(WMMAP.NegHi01, src0_modifiers{1}, 0);
  let Inst{9}  = !if(WMMAP.NegHi01, src1_modifiers{1}, 0);
  let Inst{10} = !if(WMMAP.NegHi2, src2_modifiers{1}, 0);
  // clamp
  let Inst{15} = !if(WMMAP.IsIU, clamp{0}, 0);
}

multiclass VOP3P_WMMA_Real_Base<GFXGen Gen, bits<7> op, VOP3PWMMA_Profile WMMAP,
                                string backing_ps_name = NAME,
                                string asmName = !cast<VOP3P_Pseudo>(NAME).Mnemonic> {
  def Gen.Suffix :
    VOP3P_Real_Gen<!cast<VOP3P_Pseudo>(backing_ps_name), Gen, asmName>,
    VOP3PeWmma<op, !cast<VOP3P_Pseudo>(backing_ps_name).Pfl, WMMAP>;
}

multiclass VOP3P_Real_WMMA_gfx12 <bits<7> op, VOP3PWMMA_Profile WMMAP> {
  let WaveSizePredicate = isWave32, DecoderNamespace = "GFX12" in {
    defm _twoaddr : VOP3P_WMMA_Real_Base <GFX12Gen, op, WMMAP>;
  }
}

multiclass VOP3P_Real_WMMA_gfx12w64 <bits<7> op, VOP3PWMMA_Profile WMMAP> {
  let WaveSizePredicate = isWave64, DecoderNamespace = "GFX12W64" in {
    defm _twoaddr : VOP3P_WMMA_Real_Base <GFX12Gen, op, WMMAP>;
  }
}

defm V_WMMA_F32_16X16X16_F16_w32     : VOP3P_Real_WMMA_gfx12 <0x040, F32_F16_WMMA_w32>;
defm V_WMMA_F32_16X16X16_BF16_w32    : VOP3P_Real_WMMA_gfx12 <0x041, F32_BF16_WMMA_w32>;
defm V_WMMA_F16_16X16X16_F16_w32     : VOP3P_Real_WMMA_gfx12 <0x042, F16_F16_WMMA_w32>;
defm V_WMMA_BF16_16X16X16_BF16_w32   : VOP3P_Real_WMMA_gfx12 <0x043, BF16_BF16_WMMA_w32>;
defm V_WMMA_I32_16X16X16_IU8_w32     : VOP3P_Real_WMMA_gfx12 <0x044, I32_IU8_WMMA_w32>;
defm V_WMMA_I32_16X16X16_IU4_w32     : VOP3P_Real_WMMA_gfx12 <0x045, I32_IU4X16_WMMA_w32>;
defm V_WMMA_F32_16X16X16_FP8_FP8_w32 : VOP3P_Real_WMMA_gfx12 <0x046, F32_FP8BF8_WMMA_w32>;
defm V_WMMA_F32_16X16X16_FP8_BF8_w32 : VOP3P_Real_WMMA_gfx12 <0x047, F32_FP8BF8_WMMA_w32>;
defm V_WMMA_F32_16X16X16_BF8_FP8_w32 : VOP3P_Real_WMMA_gfx12 <0x048, F32_FP8BF8_WMMA_w32>;
defm V_WMMA_F32_16X16X16_BF8_BF8_w32 : VOP3P_Real_WMMA_gfx12 <0x049, F32_FP8BF8_WMMA_w32>;
defm V_WMMA_I32_16X16X32_IU4_w32     : VOP3P_Real_WMMA_gfx12 <0x04a, I32_IU4X32_WMMA_w32>;

defm V_WMMA_F32_16X16X16_F16_w64     : VOP3P_Real_WMMA_gfx12w64 <0x040, F32_F16_WMMA_w64>;
defm V_WMMA_F32_16X16X16_BF16_w64    : VOP3P_Real_WMMA_gfx12w64 <0x041, F32_BF16_WMMA_w64>;
defm V_WMMA_F16_16X16X16_F16_w64     : VOP3P_Real_WMMA_gfx12w64 <0x042, F16_F16_WMMA_w64>;
defm V_WMMA_BF16_16X16X16_BF16_w64   : VOP3P_Real_WMMA_gfx12w64 <0x043, BF16_BF16_WMMA_w64>;
defm V_WMMA_I32_16X16X16_IU8_w64     : VOP3P_Real_WMMA_gfx12w64 <0x044, I32_IU8_WMMA_w64>;
defm V_WMMA_I32_16X16X16_IU4_w64     : VOP3P_Real_WMMA_gfx12w64 <0x045, I32_IU4X16_WMMA_w64>;
defm V_WMMA_F32_16X16X16_FP8_FP8_w64 : VOP3P_Real_WMMA_gfx12w64 <0x046, F32_FP8BF8_WMMA_w64>;
defm V_WMMA_F32_16X16X16_FP8_BF8_w64 : VOP3P_Real_WMMA_gfx12w64 <0x047, F32_FP8BF8_WMMA_w64>;
defm V_WMMA_F32_16X16X16_BF8_FP8_w64 : VOP3P_Real_WMMA_gfx12w64 <0x048, F32_FP8BF8_WMMA_w64>;
defm V_WMMA_F32_16X16X16_BF8_BF8_w64 : VOP3P_Real_WMMA_gfx12w64 <0x049, F32_FP8BF8_WMMA_w64>;
defm V_WMMA_I32_16X16X32_IU4_w64     : VOP3P_Real_WMMA_gfx12w64 <0x04a, I32_IU4X32_WMMA_w64>;


defm V_SWMMAC_F32_16X16X32_F16_w32     : VOP3P_Real_WMMA_gfx12 <0x050, F32_F16_SWMMAC_w32>;
defm V_SWMMAC_F32_16X16X32_BF16_w32    : VOP3P_Real_WMMA_gfx12 <0x051, F32_BF16_SWMMAC_w32>;
defm V_SWMMAC_F16_16X16X32_F16_w32     : VOP3P_Real_WMMA_gfx12 <0x052, F16_F16_SWMMAC_w32>;
defm V_SWMMAC_BF16_16X16X32_BF16_w32   : VOP3P_Real_WMMA_gfx12 <0x053, BF16_BF16_SWMMAC_w32>;
defm V_SWMMAC_I32_16X16X32_IU8_w32     : VOP3P_Real_WMMA_gfx12 <0x054, I32_IU8_SWMMAC_w32>;
defm V_SWMMAC_I32_16X16X32_IU4_w32     : VOP3P_Real_WMMA_gfx12 <0x055, I32_IU4X32_SWMMAC_w32>;
defm V_SWMMAC_I32_16X16X64_IU4_w32     : VOP3P_Real_WMMA_gfx12 <0x056, I32_IU4X64_SWMMAC_w32>;
defm V_SWMMAC_F32_16X16X32_FP8_FP8_w32 : VOP3P_Real_WMMA_gfx12 <0x057, F32_FP8BF8_SWMMAC_w32>;
defm V_SWMMAC_F32_16X16X32_FP8_BF8_w32 : VOP3P_Real_WMMA_gfx12 <0x058, F32_FP8BF8_SWMMAC_w32>;
defm V_SWMMAC_F32_16X16X32_BF8_FP8_w32 : VOP3P_Real_WMMA_gfx12 <0x059, F32_FP8BF8_SWMMAC_w32>;
defm V_SWMMAC_F32_16X16X32_BF8_BF8_w32 : VOP3P_Real_WMMA_gfx12 <0x05a, F32_FP8BF8_SWMMAC_w32>;

defm V_SWMMAC_F32_16X16X32_F16_w64     : VOP3P_Real_WMMA_gfx12w64 <0x050, F32_F16_SWMMAC_w64>;
defm V_SWMMAC_F32_16X16X32_BF16_w64    : VOP3P_Real_WMMA_gfx12w64 <0x051, F32_BF16_SWMMAC_w64>;
defm V_SWMMAC_F16_16X16X32_F16_w64     : VOP3P_Real_WMMA_gfx12w64 <0x052, F16_F16_SWMMAC_w64>;
defm V_SWMMAC_BF16_16X16X32_BF16_w64   : VOP3P_Real_WMMA_gfx12w64 <0x053, BF16_BF16_SWMMAC_w64>;
defm V_SWMMAC_I32_16X16X32_IU8_w64     : VOP3P_Real_WMMA_gfx12w64 <0x054, I32_IU8_SWMMAC_w64>;
defm V_SWMMAC_I32_16X16X32_IU4_w64     : VOP3P_Real_WMMA_gfx12w64 <0x055, I32_IU4X32_SWMMAC_w64>;
defm V_SWMMAC_I32_16X16X64_IU4_w64     : VOP3P_Real_WMMA_gfx12w64 <0x056, I32_IU4X64_SWMMAC_w64>;
defm V_SWMMAC_F32_16X16X32_FP8_FP8_w64 : VOP3P_Real_WMMA_gfx12w64 <0x057, F32_FP8BF8_SWMMAC_w64>;
defm V_SWMMAC_F32_16X16X32_FP8_BF8_w64 : VOP3P_Real_WMMA_gfx12w64 <0x058, F32_FP8BF8_SWMMAC_w64>;
defm V_SWMMAC_F32_16X16X32_BF8_FP8_w64 : VOP3P_Real_WMMA_gfx12w64 <0x059, F32_FP8BF8_SWMMAC_w64>;
defm V_SWMMAC_F32_16X16X32_BF8_BF8_w64 : VOP3P_Real_WMMA_gfx12w64 <0x05a, F32_FP8BF8_SWMMAC_w64>;

multiclass VOP3P_Real_with_name<GFXGen Gen, bits<7> op,
                          string backing_ps_name = NAME,
                          string asmName = !cast<VOP3P_Pseudo>(NAME).Mnemonic> {
  defvar ps = !cast<VOP3P_Pseudo>(backing_ps_name);
  let AsmString = asmName # ps.AsmOperands in
    def Gen.Suffix :
      VOP3P_Real_Gen<!cast<VOP3P_Pseudo>(backing_ps_name), Gen, asmName>,
      VOP3Pe_gfx11_gfx12<op, !cast<VOP3P_Pseudo>(backing_ps_name).Pfl>;

  def : AMDGPUMnemonicAlias<ps.Mnemonic, asmName> {
    let AssemblerPredicate = Gen.AssemblerPredicate;
  }
}

multiclass VOP3P_Real_dpp<GFXGen Gen, bits<7> op, string backing_ps_name = NAME,
                          string asmName = !cast<VOP3P_Pseudo>(NAME).Mnemonic> {
  defvar ps = !cast<VOP3P_Pseudo>(backing_ps_name);
  def _dpp#Gen.Suffix
      : VOP3P_DPP16<op, !cast<VOP_DPP_Pseudo>(backing_ps_name #"_dpp"),
                    Gen.Subtarget> {
    let AsmString = asmName #ps.Pfl.AsmVOP3DPP16;
    let DecoderNamespace = Gen.DecoderNamespace;
    let AssemblerPredicate = Gen.AssemblerPredicate;
  }
}

multiclass VOP3P_Real_dpp8<GFXGen Gen, bits<7> op, string backing_ps_name = NAME,
                           string asmName = !cast<VOP3P_Pseudo>(NAME).Mnemonic> {
  defvar ps = !cast<VOP3P_Pseudo>(backing_ps_name);
  def _dpp8#Gen.Suffix : VOP3P_DPP8_Base<op, ps> {
    let AsmString = asmName #ps.Pfl.AsmVOP3DPP8;
    let DecoderNamespace = Gen.DecoderNamespace;
    let AssemblerPredicate = Gen.AssemblerPredicate;
  }
}

multiclass VOP3P_Realtriple<GFXGen Gen, bits<7> op, string backing_ps_name = NAME,
                            string asmName = !cast<VOP3P_Pseudo>(NAME).Mnemonic>
    : VOP3P_Real_Base<Gen, op, backing_ps_name, asmName>,
      VOP3P_Real_dpp<Gen, op, backing_ps_name, asmName>,
      VOP3P_Real_dpp8<Gen, op, backing_ps_name, asmName>;

//===----------------------------------------------------------------------===//
// GFX12
//===----------------------------------------------------------------------===//

multiclass VOP3P_Real_gfx12<bits<7> op> : VOP3P_Real_Base<GFX12Gen, op>;

multiclass VOP3P_Real_with_name_gfx12<bits<7> op,
                          string backing_ps_name = NAME,
                          string asmName = !cast<VOP3P_Pseudo>(NAME).Mnemonic> :
  VOP3P_Real_with_name<GFX12Gen, op, backing_ps_name, asmName>;

defm V_PK_MIN_NUM_F16 : VOP3P_Real_with_name_gfx12<0x1b, "V_PK_MIN_F16", "v_pk_min_num_f16">;
defm V_PK_MAX_NUM_F16 : VOP3P_Real_with_name_gfx12<0x1c, "V_PK_MAX_F16", "v_pk_max_num_f16">;

defm V_PK_MINIMUM_F16 : VOP3P_Real_gfx12<0x1d>;
defm V_PK_MAXIMUM_F16 : VOP3P_Real_gfx12<0x1e>;

defm V_DOT4_F32_FP8_BF8 : VOP3P_Realtriple<GFX12Gen, 0x24>;
defm V_DOT4_F32_BF8_FP8 : VOP3P_Realtriple<GFX12Gen, 0x25>;
defm V_DOT4_F32_FP8_FP8 : VOP3P_Realtriple<GFX12Gen, 0x26>;
defm V_DOT4_F32_BF8_BF8 : VOP3P_Realtriple<GFX12Gen, 0x27>;

//===----------------------------------------------------------------------===//
// GFX11
//===----------------------------------------------------------------------===//

multiclass VOP3P_Real_gfx11_gfx12<bits<7> op> :
   VOP3P_Real_Base<GFX11Gen, op>, VOP3P_Real_Base<GFX12Gen, op>;

defm V_DOT4_I32_IU8  : VOP3P_Real_gfx11_gfx12<0x16>;
defm V_DOT8_I32_IU4  : VOP3P_Real_gfx11_gfx12<0x18>;
defm V_DOT2_F32_BF16 : VOP3P_Real_gfx11_gfx12<0x1a>;

multiclass VOP3P_Real_WMMA <bits<7> op> {
  let WaveSizePredicate = isWave32, DecoderNamespace = "GFX11" in {
    defm _twoaddr_w32 : VOP3P_Real_Base <GFX11Gen, op>;
  }
  let WaveSizePredicate = isWave64, DecoderNamespace = "GFX11W64" in {
    defm _twoaddr_w64 : VOP3P_Real_Base <GFX11Gen, op>;
  }
}

defm V_WMMA_F32_16X16X16_F16   : VOP3P_Real_WMMA <0x040>;
defm V_WMMA_F32_16X16X16_BF16  : VOP3P_Real_WMMA <0x041>;
defm V_WMMA_F16_16X16X16_F16   : VOP3P_Real_WMMA <0x042>;
defm V_WMMA_BF16_16X16X16_BF16 : VOP3P_Real_WMMA <0x043>;
defm V_WMMA_I32_16X16X16_IU8   : VOP3P_Real_WMMA <0x044>;
defm V_WMMA_I32_16X16X16_IU4   : VOP3P_Real_WMMA <0x045>;

//===----------------------------------------------------------------------===//
// GFX8 (VI)
//===----------------------------------------------------------------------===//

multiclass VOP3P_Real_vi<bits<7> op> {
  def _vi : VOP3P_Real<!cast<VOP3_Pseudo>(NAME), SIEncodingFamily.VI>,
            VOP3Pe <op, !cast<VOP3_Pseudo>(NAME).Pfl> {
    let AssemblerPredicate = HasVOP3PInsts;
    let DecoderNamespace = "GFX8";
    let VOP3P = 1;
  }
}

multiclass VOP3P_Real_MAI<bits<7> op> {
  def _vi : VOP3P_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.VI>,
            VOP3Pe_MAI <op, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl, ?> {
    let AssemblerPredicate = HasMAIInsts;
    let DecoderNamespace = "GFX8";
    let Inst{14} = ?; // op_sel_hi(2)
    let Inst{59} = ?; // op_sel_hi(0)
    let Inst{60} = ?; // op_sel_hi(1)
  }
}

let Constraints = "" in {
multiclass VOP3P_Real_MFMA_gfx90a<bits<7> op> {
  let SubtargetPredicate = isGFX90AOnly,
      AssemblerPredicate = isGFX90AOnly, DecoderNamespace = "GFX90A" in {
  def _gfx90a_acd : VOP3P_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.GFX90A>,
             VOP3Pe_MAI <op, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl, 1>;

  def _gfx90a_vcd : VOP3P_Real<!cast<VOP3_Pseudo>(NAME # "_vgprcd" # "_e64"), SIEncodingFamily.GFX90A>,
             VOP3Pe_MAI <op, !cast<VOP3_Pseudo>(NAME # "_vgprcd" # "_e64").Pfl, 0>;
  } // End AssemblerPredicate = isGFX90AOnly, DecoderNamespace = "GFX90A"
}
}

multiclass VOP3P_Real_MFMA_gfx940_aliases<string NameFrom, string NameTo, string Op,
                                          VOP3_Pseudo PS_ACD = !cast<VOP3_Pseudo>(Op # "_e64"),
                                          VOP3_Pseudo PS_VCD = !cast<VOP3_Pseudo>(Op # "_vgprcd" # "_e64"),
                                          VOPProfile Pfl_ACD = PS_ACD.Pfl,
                                          VOPProfile Pfl_VCD = PS_VCD.Pfl> {
  if !ne(NameFrom, NameTo) then {
    def : InstAlias <NameTo # " " # PS_ACD.AsmOperands,
                     (!cast<VOP3P_Real>(Op # "_gfx940_acd") Pfl_ACD.DstRC:$vdst,
                         Pfl_ACD.Src0RC64:$src0, Pfl_ACD.Src1RC64:$src1, Pfl_ACD.Src2RC64:$src2,
                         CBSZ:$cbsz, ABID:$abid, blgp:$blgp)>, PredicateControl;
    def : InstAlias <NameTo # " " # PS_VCD.AsmOperands,
                     (!cast<VOP3P_Real>(Op # "_gfx940_vcd") Pfl_VCD.DstRC:$vdst,
                         Pfl_VCD.Src0RC64:$src0, Pfl_VCD.Src1RC64:$src1, Pfl_VCD.Src2RC64:$src2,
                         CBSZ:$cbsz, ABID:$abid, blgp:$blgp)>, PredicateControl;
  }
}

multiclass VOP3P_Real_MFMA_gfx940<bits<7> op, string Name = !cast<VOP3_Pseudo>(NAME#"_e64").Mnemonic,
                                  VOP3_Pseudo PS_ACD = !cast<VOP3_Pseudo>(NAME # "_e64"),
                                  VOP3_Pseudo PS_VCD = !cast<VOP3_Pseudo>(NAME # "_vgprcd" # "_e64")> {
  let AssemblerPredicate = isGFX940Plus,
      DecoderNamespace = "GFX940",
      AsmString = Name # PS_ACD.AsmOperands, Constraints = "" in {
  def _gfx940_acd : VOP3P_Real<PS_ACD, SIEncodingFamily.GFX940>,
                    VOP3Pe_MAI <op, PS_ACD.Pfl, 1>;

  def _gfx940_vcd : VOP3P_Real<PS_VCD, SIEncodingFamily.GFX940>,
                    VOP3Pe_MAI <op, PS_VCD.Pfl, 0>;
  } // End AssemblerPredicate = isGFX940Plus, DecoderNamespace = "GFX940"

  let SubtargetPredicate = isGFX940Plus in {
    defm : VOP3P_Real_MFMA_gfx940_aliases<Name, PS_ACD.Mnemonic, NAME>;

    if !ne(!subst("_1k", "", PS_ACD.Mnemonic), PS_ACD.Mnemonic) then
    defm : VOP3P_Real_MFMA_gfx940_aliases<Name, !subst("_1k", "", PS_ACD.Mnemonic), NAME>;
  }
}

multiclass VOP3P_Real_MFMA_vi<bits<7> op> {
  def _vi : VOP3P_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.VI>,
            VOP3Pe_MAI <op, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl, ?> {
    let SubtargetPredicate = isGFX8GFX9NotGFX90A;
    let AssemblerPredicate = HasMAIInsts;
    let DecoderNamespace = "GFX8";
    let Constraints = "";
  }
}

multiclass VOP3P_Real_MFMA_vi_gfx90a<bits<7> op> :
  VOP3P_Real_MFMA_gfx90a <op>,
  VOP3P_Real_MFMA_vi <op>;

multiclass VOP3P_Real_MFMA<bits<7> op, string GFX940Name = !cast<VOP3_Pseudo>(NAME#"_e64").Mnemonic> :
  VOP3P_Real_MFMA_vi_gfx90a <op>,
  VOP3P_Real_MFMA_gfx940 <op, GFX940Name>;

multiclass VOP3P_Real_SMFMAC<bits<7> op, string alias> {
  def _gfx940 : VOP3P_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.VI>,
                VOP3Pe_SMFMAC <op> {
    let AssemblerPredicate = isGFX940Plus;
    let DecoderNamespace = "GFX8";
  }
  def : AMDGPUMnemonicAlias<alias, !cast<VOP3_Pseudo>(NAME#"_e64").Mnemonic> {
    let AssemblerPredicate = isGFX940Plus;
  }
}

let SubtargetPredicate = isGFX8GFX9 in {
defm V_PK_MAD_I16 : VOP3P_Real_vi <0x00>;
defm V_PK_MUL_LO_U16 : VOP3P_Real_vi <0x01>;
defm V_PK_ADD_I16 : VOP3P_Real_vi <0x02>;
defm V_PK_SUB_I16 : VOP3P_Real_vi <0x03>;
defm V_PK_LSHLREV_B16 : VOP3P_Real_vi <0x04>;
defm V_PK_LSHRREV_B16 : VOP3P_Real_vi <0x05>;
defm V_PK_ASHRREV_I16 : VOP3P_Real_vi <0x06>;
defm V_PK_MAX_I16 : VOP3P_Real_vi <0x07>;
defm V_PK_MIN_I16 : VOP3P_Real_vi <0x08>;
defm V_PK_MAD_U16 : VOP3P_Real_vi <0x09>;

defm V_PK_ADD_U16 : VOP3P_Real_vi <0x0a>;
defm V_PK_SUB_U16 : VOP3P_Real_vi <0x0b>;
defm V_PK_MAX_U16 : VOP3P_Real_vi <0x0c>;
defm V_PK_MIN_U16 : VOP3P_Real_vi <0x0d>;
defm V_PK_FMA_F16 : VOP3P_Real_vi <0x0e>;
defm V_PK_ADD_F16 : VOP3P_Real_vi <0x0f>;
defm V_PK_MUL_F16 : VOP3P_Real_vi <0x10>;
defm V_PK_MIN_F16 : VOP3P_Real_vi <0x11>;
defm V_PK_MAX_F16 : VOP3P_Real_vi <0x12>;

let OtherPredicates = [HasMadMixInsts] in {
defm V_MAD_MIX_F32 : VOP3P_Real_vi <0x20>;
defm V_MAD_MIXLO_F16 : VOP3P_Real_vi <0x21>;
defm V_MAD_MIXHI_F16 : VOP3P_Real_vi <0x22>;
}

let OtherPredicates = [HasFmaMixInsts],
    DecoderNamespace = "GFX9_DL" in {
// The mad_mix instructions were renamed and their behaviors changed,
// but the opcode stayed the same so we need to put these in a
// different DecoderNamespace to avoid the ambiguity.
defm V_FMA_MIX_F32 : VOP3P_Real_vi <0x20>;
defm V_FMA_MIXLO_F16 : VOP3P_Real_vi <0x21>;
defm V_FMA_MIXHI_F16 : VOP3P_Real_vi <0x22>;
}

defm V_DOT2_I32_I16 : VOP3P_Real_vi <0x26>;
defm V_DOT2_U32_U16 : VOP3P_Real_vi <0x27>;

defm V_DOT2_F32_F16 : VOP3P_Real_vi <0x23>;
defm V_DOT4_U32_U8  : VOP3P_Real_vi <0x29>;
defm V_DOT8_U32_U4  : VOP3P_Real_vi <0x2b>;

defm V_DOT4_I32_I8  : VOP3P_Real_vi <0x28>;
defm V_DOT8_I32_I4  : VOP3P_Real_vi <0x2a>;
} // End SubtargetPredicate = isGFX8GFX9

let OtherPredicates = [HasMAIInsts] in {

defm V_ACCVGPR_READ_B32  : VOP3P_Real_MAI <0x58>;
defm V_ACCVGPR_WRITE_B32 : VOP3P_Real_MAI <0x59>;
defm V_MFMA_F32_32X32X1F32  : VOP3P_Real_MFMA <0x40, "v_mfma_f32_32x32x1_2b_f32">;
defm V_MFMA_F32_16X16X1F32  : VOP3P_Real_MFMA <0x41, "v_mfma_f32_16x16x1_4b_f32">;
defm V_MFMA_F32_4X4X1F32    : VOP3P_Real_MFMA <0x42, "v_mfma_f32_4x4x1_16b_f32">;
defm V_MFMA_F32_32X32X2F32  : VOP3P_Real_MFMA <0x44, "v_mfma_f32_32x32x2_f32">;
defm V_MFMA_F32_16X16X4F32  : VOP3P_Real_MFMA <0x45, "v_mfma_f32_16x16x4_f32">;
defm V_MFMA_F32_32X32X4F16  : VOP3P_Real_MFMA <0x48, "v_mfma_f32_32x32x4_2b_f16">;
defm V_MFMA_F32_16X16X4F16  : VOP3P_Real_MFMA <0x49, "v_mfma_f32_16x16x4_4b_f16">;
defm V_MFMA_F32_4X4X4F16    : VOP3P_Real_MFMA <0x4a, "v_mfma_f32_4x4x4_16b_f16">;
defm V_MFMA_F32_32X32X8F16  : VOP3P_Real_MFMA <0x4c, "v_mfma_f32_32x32x8_f16">;
defm V_MFMA_F32_16X16X16F16 : VOP3P_Real_MFMA <0x4d, "v_mfma_f32_16x16x16_f16">;
defm V_MFMA_I32_32X32X4I8   : VOP3P_Real_MFMA <0x50, "v_mfma_i32_32x32x4_2b_i8">;
defm V_MFMA_I32_16X16X4I8   : VOP3P_Real_MFMA <0x51, "v_mfma_i32_16x16x4_4b_i8">;
defm V_MFMA_I32_4X4X4I8     : VOP3P_Real_MFMA <0x52, "v_mfma_i32_4x4x4_16b_i8">;

defm V_MFMA_I32_16X16X16I8  : VOP3P_Real_MFMA_vi_gfx90a <0x55>;
defm V_MFMA_I32_32X32X8I8   : VOP3P_Real_MFMA_vi_gfx90a <0x54>;
defm V_MFMA_F32_32X32X2BF16 : VOP3P_Real_MFMA_vi_gfx90a <0x68>;
defm V_MFMA_F32_16X16X2BF16 : VOP3P_Real_MFMA_vi_gfx90a <0x69>;
defm V_MFMA_F32_4X4X2BF16   : VOP3P_Real_MFMA_vi_gfx90a <0x6b>;
defm V_MFMA_F32_32X32X4BF16 : VOP3P_Real_MFMA_vi_gfx90a <0x6c>;
defm V_MFMA_F32_16X16X8BF16 : VOP3P_Real_MFMA_vi_gfx90a <0x6d>;

} // End OtherPredicates = [HasMAIInsts]

defm V_MFMA_F32_32X32X4BF16_1K  : VOP3P_Real_MFMA_gfx90a <0x63>;
defm V_MFMA_F32_16X16X4BF16_1K  : VOP3P_Real_MFMA_gfx90a <0x64>;
defm V_MFMA_F32_4X4X4BF16_1K    : VOP3P_Real_MFMA_gfx90a <0x65>;
defm V_MFMA_F32_32X32X8BF16_1K  : VOP3P_Real_MFMA_gfx90a <0x66>;
defm V_MFMA_F32_16X16X16BF16_1K : VOP3P_Real_MFMA_gfx90a <0x67>;
defm V_MFMA_F64_16X16X4F64      : VOP3P_Real_MFMA_gfx90a <0x6e>;
defm V_MFMA_F64_4X4X4F64        : VOP3P_Real_MFMA_gfx90a <0x6f>;

defm V_MFMA_I32_32X32X16I8       : VOP3P_Real_MFMA_gfx940 <0x56, "v_mfma_i32_32x32x16_i8">;
defm V_MFMA_I32_16X16X32I8       : VOP3P_Real_MFMA_gfx940 <0x57, "v_mfma_i32_16x16x32_i8">;
defm V_MFMA_F32_16X16X8XF32      : VOP3P_Real_MFMA_gfx940 <0x3e, "v_mfma_f32_16x16x8_xf32">;
defm V_MFMA_F32_32X32X4XF32      : VOP3P_Real_MFMA_gfx940 <0x3f, "v_mfma_f32_32x32x4_xf32">;
defm V_MFMA_F32_16X16X32_BF8_BF8 : VOP3P_Real_MFMA_gfx940 <0x70>;
defm V_MFMA_F32_16X16X32_BF8_FP8 : VOP3P_Real_MFMA_gfx940 <0x71>;
defm V_MFMA_F32_16X16X32_FP8_BF8 : VOP3P_Real_MFMA_gfx940 <0x72>;
defm V_MFMA_F32_16X16X32_FP8_FP8 : VOP3P_Real_MFMA_gfx940 <0x73>;
defm V_MFMA_F32_32X32X16_BF8_BF8 : VOP3P_Real_MFMA_gfx940 <0x74>;
defm V_MFMA_F32_32X32X16_BF8_FP8 : VOP3P_Real_MFMA_gfx940 <0x75>;
defm V_MFMA_F32_32X32X16_FP8_BF8 : VOP3P_Real_MFMA_gfx940 <0x76>;
defm V_MFMA_F32_32X32X16_FP8_FP8 : VOP3P_Real_MFMA_gfx940 <0x77>;

defm V_MFMA_F32_32X32X4BF16_1K   : VOP3P_Real_MFMA_gfx940 <0x5d, "v_mfma_f32_32x32x4_2b_bf16">;
defm V_MFMA_F32_16X16X4BF16_1K   : VOP3P_Real_MFMA_gfx940 <0x5e, "v_mfma_f32_16x16x4_4b_bf16">;
defm V_MFMA_F32_4X4X4BF16_1K     : VOP3P_Real_MFMA_gfx940 <0x5f, "v_mfma_f32_4x4x4_16b_bf16">;
defm V_MFMA_F32_32X32X8BF16_1K   : VOP3P_Real_MFMA_gfx940 <0x60, "v_mfma_f32_32x32x8_bf16">;
defm V_MFMA_F32_16X16X16BF16_1K  : VOP3P_Real_MFMA_gfx940 <0x61, "v_mfma_f32_16x16x16_bf16">;

defm V_MFMA_F64_16X16X4F64       : VOP3P_Real_MFMA_gfx940 <0x6e, "v_mfma_f64_16x16x4_f64">;
defm V_MFMA_F64_4X4X4F64         : VOP3P_Real_MFMA_gfx940 <0x6f, "v_mfma_f64_4x4x4_4b_f64">;

defm V_SMFMAC_F32_16X16X32_F16     : VOP3P_Real_SMFMAC <0x62, "v_smfmac_f32_16x16x32f16">;
defm V_SMFMAC_F32_32X32X16_F16     : VOP3P_Real_SMFMAC <0x64, "v_smfmac_f32_32x32x16f16">;
defm V_SMFMAC_F32_16X16X32_BF16    : VOP3P_Real_SMFMAC <0x66, "v_smfmac_f32_16x16x32bf16">;
defm V_SMFMAC_F32_32X32X16_BF16    : VOP3P_Real_SMFMAC <0x68, "v_smfmac_f32_32x32x16bf16">;
defm V_SMFMAC_I32_16X16X64_I8      : VOP3P_Real_SMFMAC <0x6a, "v_smfmac_i32_16x16x64i8">;
defm V_SMFMAC_I32_32X32X32_I8      : VOP3P_Real_SMFMAC <0x6c, "v_smfmac_i32_32x32x32i8">;
defm V_SMFMAC_F32_16X16X64_BF8_BF8 : VOP3P_Real_SMFMAC <0x78, "v_smfmac_f32_16x16x64bf8bf8">;
defm V_SMFMAC_F32_16X16X64_BF8_FP8 : VOP3P_Real_SMFMAC <0x79, "v_smfmac_f32_16x16x64bf8fp8">;
defm V_SMFMAC_F32_16X16X64_FP8_BF8 : VOP3P_Real_SMFMAC <0x7a, "v_smfmac_f32_16x16x64fp8bf8">;
defm V_SMFMAC_F32_16X16X64_FP8_FP8 : VOP3P_Real_SMFMAC <0x7b, "v_smfmac_f32_16x16x64fp8fp8">;
defm V_SMFMAC_F32_32X32X32_BF8_BF8 : VOP3P_Real_SMFMAC <0x7c, "v_smfmac_f32_32x32x32bf8bf8">;
defm V_SMFMAC_F32_32X32X32_BF8_FP8 : VOP3P_Real_SMFMAC <0x7d, "v_smfmac_f32_32x32x32bf8fp8">;
defm V_SMFMAC_F32_32X32X32_FP8_BF8 : VOP3P_Real_SMFMAC <0x7e, "v_smfmac_f32_32x32x32fp8bf8">;
defm V_SMFMAC_F32_32X32X32_FP8_FP8 : VOP3P_Real_SMFMAC <0x7f, "v_smfmac_f32_32x32x32fp8fp8">;

defm V_PK_FMA_F32 : VOP3P_Real_vi <0x30>;
defm V_PK_MUL_F32 : VOP3P_Real_vi <0x31>;
defm V_PK_ADD_F32 : VOP3P_Real_vi <0x32>;
defm V_PK_MOV_B32 : VOP3P_Real_vi <0x33>;

//===----------------------------------------------------------------------===//
// GFX10.
//===----------------------------------------------------------------------===//

let AssemblerPredicate = isGFX10Only, DecoderNamespace = "GFX10", VOP3P = 1 in {
  multiclass VOP3P_Real_gfx10<bits<7> op> {
    def _gfx10 : VOP3P_Real<!cast<VOP3P_Pseudo>(NAME), SIEncodingFamily.GFX10>,
                 VOP3Pe_gfx10 <op, !cast<VOP3P_Pseudo>(NAME).Pfl>;
  }
} // End AssemblerPredicate = isGFX10Only, DecoderNamespace = "GFX10", VOP3P = 1

multiclass VOP3P_Real_gfx10_gfx11<bits<7> op> :
  VOP3P_Real_gfx10<op>, VOP3P_Real_Base<GFX11Gen, op>;

multiclass VOP3P_Real_gfx10_gfx11_gfx12<bits<7> op> :
  VOP3P_Real_gfx10_gfx11<op>, VOP3P_Real_Base<GFX12Gen, op>;

multiclass VOP3P_Real_gfx10_gfx11_gfx12_Triple<bits<7> op> :
  VOP3P_Real_gfx10<op>, VOP3P_Realtriple<GFX11Gen, op>,
  VOP3P_Realtriple<GFX12Gen, op>;

defm V_PK_MAD_I16     : VOP3P_Real_gfx10_gfx11_gfx12<0x00>;
defm V_PK_MUL_LO_U16  : VOP3P_Real_gfx10_gfx11_gfx12<0x01>;
defm V_PK_ADD_I16     : VOP3P_Real_gfx10_gfx11_gfx12<0x02>;
defm V_PK_SUB_I16     : VOP3P_Real_gfx10_gfx11_gfx12<0x03>;
defm V_PK_LSHLREV_B16 : VOP3P_Real_gfx10_gfx11_gfx12<0x04>;
defm V_PK_LSHRREV_B16 : VOP3P_Real_gfx10_gfx11_gfx12<0x05>;
defm V_PK_ASHRREV_I16 : VOP3P_Real_gfx10_gfx11_gfx12<0x06>;
defm V_PK_MAX_I16     : VOP3P_Real_gfx10_gfx11_gfx12<0x07>;
defm V_PK_MIN_I16     : VOP3P_Real_gfx10_gfx11_gfx12<0x08>;
defm V_PK_MAD_U16     : VOP3P_Real_gfx10_gfx11_gfx12<0x09>;
defm V_PK_ADD_U16     : VOP3P_Real_gfx10_gfx11_gfx12<0x0a>;
defm V_PK_SUB_U16     : VOP3P_Real_gfx10_gfx11_gfx12<0x0b>;
defm V_PK_MAX_U16     : VOP3P_Real_gfx10_gfx11_gfx12<0x0c>;
defm V_PK_MIN_U16     : VOP3P_Real_gfx10_gfx11_gfx12<0x0d>;
defm V_PK_FMA_F16     : VOP3P_Real_gfx10_gfx11_gfx12<0x0e>;
defm V_PK_ADD_F16     : VOP3P_Real_gfx10_gfx11_gfx12<0x0f>;
defm V_PK_MUL_F16     : VOP3P_Real_gfx10_gfx11_gfx12<0x10>;
defm V_PK_MIN_F16     : VOP3P_Real_gfx10_gfx11<0x11>;
defm V_PK_MAX_F16     : VOP3P_Real_gfx10_gfx11<0x12>;
defm V_FMA_MIX_F32    : VOP3P_Real_gfx10_gfx11_gfx12_Triple<0x20>;
defm V_FMA_MIXLO_F16  : VOP3P_Real_gfx10_gfx11_gfx12_Triple<0x21>;
defm V_FMA_MIXHI_F16  : VOP3P_Real_gfx10_gfx11_gfx12_Triple<0x22>;

defm V_DOT2_I32_I16 : VOP3P_Real_gfx10 <0x14>;
defm V_DOT2_U32_U16 : VOP3P_Real_gfx10 <0x15>;

defm V_DOT2_F32_F16 : VOP3P_Real_gfx10_gfx11_gfx12_Triple<0x13>;
defm V_DOT4_U32_U8  : VOP3P_Real_gfx10_gfx11_gfx12<0x17>;
defm V_DOT8_U32_U4  : VOP3P_Real_gfx10_gfx11_gfx12<0x19>;

defm V_DOT4_I32_I8  : VOP3P_Real_gfx10 <0x16>;
defm V_DOT8_I32_I4  : VOP3P_Real_gfx10 <0x18>;
