
//===-- VOP1Instructions.td - Vector Instruction Definitions --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// VOP1 Classes
//===----------------------------------------------------------------------===//

class VOP1e <bits<8> op, VOPProfile P> : Enc32 {
  bits<8> vdst;
  bits<9> src0;

  let Inst{8-0}   = !if(P.HasSrc0, src0{8-0}, ?);
  let Inst{16-9}  = op;
  let Inst{24-17} = !if(P.EmitDst, vdst{7-0}, 0);
  let Inst{31-25} = 0x3f; //encoding
}

class VOP1_SDWAe <bits<8> op, VOPProfile P> : VOP_SDWAe <P> {
  bits<8> vdst;

  let Inst{8-0}   = 0xf9; // sdwa
  let Inst{16-9}  = op;
  let Inst{24-17} = !if(P.EmitDst, vdst{7-0}, 0);
  let Inst{31-25} = 0x3f; // encoding
}

class VOP1_SDWA9Ae <bits<8> op, VOPProfile P> : VOP_SDWA9Ae <P> {
  bits<8> vdst;

  let Inst{8-0}   = 0xf9; // sdwa
  let Inst{16-9}  = op;
  let Inst{24-17} = !if(P.EmitDst, vdst{7-0}, 0);
  let Inst{31-25} = 0x3f; // encoding
}

class VOP1_Pseudo <string opName, VOPProfile P, list<dag> pattern=[], bit VOP1Only = 0> :
  VOP_Pseudo <opName, !if(VOP1Only, "", "_e32"), P, P.Outs32, P.Ins32, "", pattern> {

  let AsmOperands = P.Asm32;

  let Size = 4;
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;

  let ReadsModeReg = !or(P.DstVT.isFP, P.Src0VT.isFP);

  let mayRaiseFPException = ReadsModeReg;

  let VOP1 = 1;
  let VALU = 1;
  let Uses = !if(ReadsModeReg, [MODE, EXEC], [EXEC]);

  let AsmVariantName = AMDGPUAsmVariants.Default;
}

class VOP1_Real <VOP1_Pseudo ps, int EncodingFamily, string real_name = ps.Mnemonic > :
  VOP_Real <ps>,
  InstSI <ps.OutOperandList, ps.InOperandList, real_name # ps.AsmOperands, []>,
  SIMCInstr <ps.PseudoInstr, EncodingFamily> {

  let VALU = 1;
  let VOP1 = 1;
  let isPseudo = 0;
  let isCodeGenOnly = 0;

  let Constraints     = ps.Constraints;
  let DisableEncoding = ps.DisableEncoding;

  // copy relevant pseudo op flags
  let SubtargetPredicate = ps.SubtargetPredicate;
  let OtherPredicates    = ps.OtherPredicates;
  let AsmMatchConverter  = ps.AsmMatchConverter;
  let AsmVariantName     = ps.AsmVariantName;
  let Constraints        = ps.Constraints;
  let DisableEncoding    = ps.DisableEncoding;
  let TSFlags            = ps.TSFlags;
  let UseNamedOperandTable = ps.UseNamedOperandTable;
  let Uses                 = ps.Uses;
  let Defs                 = ps.Defs;
  let SchedRW              = ps.SchedRW;
  let mayLoad              = ps.mayLoad;
  let mayStore             = ps.mayStore;
  let TRANS                = ps.TRANS;
}

class VOP1_Real_Gen <VOP1_Pseudo ps, GFXGen Gen, string real_name = ps.Mnemonic> :
  VOP1_Real <ps, Gen.Subtarget, real_name> {
  let AssemblerPredicate = Gen.AssemblerPredicate;
  let DecoderNamespace = Gen.DecoderNamespace;
}

class VOP1_SDWA_Pseudo <string OpName, VOPProfile P, list<dag> pattern=[]> :
  VOP_SDWA_Pseudo <OpName, P, pattern> {
  let AsmMatchConverter = "cvtSdwaVOP1";
}

class VOP1_DPP_Pseudo <string OpName, VOPProfile P, list<dag> pattern=[]> :
  VOP_DPP_Pseudo <OpName, P, pattern> {
}

class getVOP1Pat <SDPatternOperator node, VOPProfile P> : LetDummies {
  list<dag> ret =
    !if(P.HasModifiers,
        [(set P.DstVT:$vdst, (node (P.Src0VT (VOP3Mods P.Src0VT:$src0, i32:$src0_modifiers))))],
        !if(P.HasOMod,
            [(set P.DstVT:$vdst, (node (P.Src0VT (VOP3OMods P.Src0VT:$src0,
                                                  i1:$clamp, i32:$omod))))],
            [(set P.DstVT:$vdst, (node (P.Src0VT P.Src0RC32:$src0)))]
        )
    );
}

multiclass VOP1Inst <string opName, VOPProfile P,
                     SDPatternOperator node = null_frag, int VOPDOp = -1> {
  // We only want to set this on the basic, non-SDWA or DPP forms.
  defvar should_mov_imm = !or(!eq(opName, "v_mov_b32"),
                              !eq(opName, "v_mov_b64"));

  let isMoveImm = should_mov_imm in {
    if !eq(VOPDOp, -1) then
      def _e32 : VOP1_Pseudo <opName, P>;
    else
      // Only for V_MOV_B32
      def _e32 : VOP1_Pseudo <opName, P>, VOPD_Component<VOPDOp, opName>;
    def _e64 : VOP3InstBase <opName, P, node>;
  }

  if P.HasExtSDWA then
    def _sdwa : VOP1_SDWA_Pseudo <opName, P>;

  if P.HasExtDPP then
    def _dpp : VOP1_DPP_Pseudo <opName, P>;

  let SubtargetPredicate = isGFX11Plus in {
    if P.HasExtVOP3DPP then
      def _e64_dpp  : VOP3_DPP_Pseudo <opName, P>;
  } // End SubtargetPredicate = isGFX11Plus

  def : LetDummies, AMDGPUMnemonicAlias<opName#"_e32", opName>;
  def : LetDummies, AMDGPUMnemonicAlias<opName#"_e64", opName>;

  if P.HasExtSDWA then
    def : LetDummies, AMDGPUMnemonicAlias<opName#"_sdwa", opName>;

  if P.HasExtDPP then
    def : LetDummies, AMDGPUMnemonicAlias<opName#"_dpp", opName, AMDGPUAsmVariants.DPP>;
}

multiclass VOP1Inst_t16_with_profiles<string opName,
                        VOPProfile P,
                        VOPProfile P_t16,
                        VOPProfile P_fake16,
                        SDPatternOperator node = null_frag> {
  let OtherPredicates = [NotHasTrue16BitInsts, Has16BitInsts]  in {
    defm NAME : VOP1Inst<opName, P, node>;
  }
  let OtherPredicates = [UseRealTrue16Insts] in {
    defm _t16 : VOP1Inst<opName#"_t16", P_t16, node>;
  }
  let OtherPredicates = [UseFakeTrue16Insts] in {
    defm _fake16 : VOP1Inst<opName#"_fake16", P_fake16, node>;
  }
}

multiclass VOP1Inst_t16<string opName, VOPProfile P,
                        SDPatternOperator node = null_frag> :
  VOP1Inst_t16_with_profiles<opName, P, VOPProfile_True16<P>, VOPProfile_Fake16<P>, node>;

// Special profile for instructions which have clamp
// and output modifiers (but have no input modifiers)
class VOPProfileI2F<ValueType dstVt, ValueType srcVt> :
  VOPProfile<[dstVt, srcVt, untyped, untyped]> {

  let Ins64 = (ins Src0RC64:$src0, Clamp:$clamp, omod:$omod);
  let InsVOP3Base = (ins Src0VOP3DPP:$src0, Clamp:$clamp, omod:$omod);
  let AsmVOP3Base = "$vdst, $src0$clamp$omod";

  let HasModifiers = 0;
  let HasClamp = 1;
}

class VOPProfileI2F_True16<ValueType dstVt, ValueType srcVt> :
  VOPProfile_Fake16<VOPProfile<[dstVt, srcVt, untyped, untyped]>> {

  let Ins64 = (ins Src0RC64:$src0, Clamp:$clamp, omod:$omod);
  let InsVOP3Base = (ins Src0VOP3DPP:$src0, Clamp:$clamp, omod:$omod);
  let AsmVOP3Base = "$vdst, $src0$clamp$omod";

  let HasModifiers = 0;
  let HasClamp = 1;
}

def VOP1_F64_I32 : VOPProfileI2F <f64, i32>;
def VOP1_F32_I32 : VOPProfileI2F <f32, i32>;
def VOP1_F16_I16 : VOPProfileI2F <f16, i16>;
def VOP1_F16_I16_t16 : VOPProfileI2F_True16 <f16, i16>;

def VOP_NOP_PROFILE : VOPProfile <[untyped, untyped, untyped, untyped]>{
  let HasExtVOP3DPP = 0;
}

// OMod clears exceptions when set. OMod was always an operand, but its
// now explicitly set.
class VOP_SPECIAL_OMOD_PROF<ValueType dstVt, ValueType srcVt> :
  VOPProfile<[dstVt, srcVt, untyped, untyped]> {

  let HasOMod = 1;
}
def VOP_I32_F32_SPECIAL_OMOD : VOP_SPECIAL_OMOD_PROF<i32, f32>;
def VOP_I32_F64_SPECIAL_OMOD : VOP_SPECIAL_OMOD_PROF<i32, f64>;
def VOP_I16_F16_SPECIAL_OMOD : VOP_SPECIAL_OMOD_PROF<i16, f16>;
def VOP_I16_F16_SPECIAL_OMOD_t16 : VOPProfile_Fake16<VOP_I16_F16> {
  let HasOMod = 1;
}

//===----------------------------------------------------------------------===//
// VOP1 Instructions
//===----------------------------------------------------------------------===//

defm V_NOP : VOP1Inst <"v_nop", VOP_NOP_PROFILE>;

def VOPProfile_MOV : VOPProfile <[i32, i32, untyped, untyped]> {
  let InsVOPDX = (ins Src0RC32:$src0X);
  let InsVOPDXDeferred = (ins VSrc_f32_Deferred:$src0X);
  let InsVOPDY = (ins Src0RC32:$src0Y);
  let InsVOPDYDeferred = (ins VSrc_f32_Deferred:$src0Y);
}

let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
defm V_MOV_B32 : VOP1Inst <"v_mov_b32", VOPProfile_MOV, null_frag, 0x8>;

let SubtargetPredicate = isGFX940Plus, SchedRW = [Write64Bit] in
defm V_MOV_B64 : VOP1Inst <"v_mov_b64", VOP_I64_I64>;
} // End isMoveImm = 1

def VOP_READFIRSTLANE : VOPProfile <[i32, i32, untyped, untyped]> {
  let DstRC = RegisterOperand<SReg_32>;
  let Src0RC32 = VRegOrLdsSrc_32;
  let Asm32 = " $vdst, $src0";
}

// FIXME: Specify SchedRW for READFIRSTLANE_B32
// TODO: There is VOP3 encoding also
def V_READFIRSTLANE_B32 : VOP1_Pseudo <"v_readfirstlane_b32", VOP_READFIRSTLANE,
                                       [], 1> {
  let isConvergent = 1;
  let IsInvalidSingleUseConsumer = 1;
}

foreach vt = Reg32Types.types in {
  def : GCNPat<(vt (int_amdgcn_readfirstlane (vt VRegOrLdsSrc_32:$src0))),
        (V_READFIRSTLANE_B32 (vt VRegOrLdsSrc_32:$src0))
  >;
}

let isReMaterializable = 1 in {
let SchedRW = [WriteDoubleCvt] in {
// OMod clears exceptions when set in this instruction
defm V_CVT_I32_F64 : VOP1Inst <"v_cvt_i32_f64", VOP_I32_F64_SPECIAL_OMOD,  fp_to_sint>;

let mayRaiseFPException = 0 in {
defm V_CVT_F64_I32 : VOP1Inst <"v_cvt_f64_i32", VOP1_F64_I32, sint_to_fp>;
}

defm V_CVT_F32_F64 : VOP1Inst <"v_cvt_f32_f64", VOP_F32_F64,  fpround>;
defm V_CVT_F64_F32 : VOP1Inst <"v_cvt_f64_f32", VOP_F64_F32,  any_fpextend>;
// OMod clears exceptions when set in this instruction
defm V_CVT_U32_F64 : VOP1Inst <"v_cvt_u32_f64", VOP_I32_F64_SPECIAL_OMOD,  fp_to_uint>;

let mayRaiseFPException = 0 in {
defm V_CVT_F64_U32 : VOP1Inst <"v_cvt_f64_u32", VOP1_F64_I32, uint_to_fp>;
}

} // End SchedRW = [WriteDoubleCvt]

let SchedRW = [WriteFloatCvt] in {

// XXX: Does this really not raise exceptions? The manual claims the
// 16-bit ones can.
let mayRaiseFPException = 0 in {
defm V_CVT_F32_I32 : VOP1Inst <"v_cvt_f32_i32", VOP1_F32_I32, sint_to_fp>;
defm V_CVT_F32_U32 : VOP1Inst <"v_cvt_f32_u32", VOP1_F32_I32, uint_to_fp>;
}

// OMod clears exceptions when set in these 2 instructions
defm V_CVT_U32_F32 : VOP1Inst <"v_cvt_u32_f32", VOP_I32_F32_SPECIAL_OMOD, fp_to_uint>;
defm V_CVT_I32_F32 : VOP1Inst <"v_cvt_i32_f32", VOP_I32_F32_SPECIAL_OMOD, fp_to_sint>;
let FPDPRounding = 1, isReMaterializable = 0 in {
  let OtherPredicates = [NotHasTrue16BitInsts] in
  defm V_CVT_F16_F32 : VOP1Inst <"v_cvt_f16_f32", VOP_F16_F32, any_fpround>;
  let OtherPredicates = [HasTrue16BitInsts] in
  defm V_CVT_F16_F32_t16 : VOP1Inst <"v_cvt_f16_f32_t16", VOPProfile_Fake16<VOP_F16_F32>, any_fpround>;
} // End FPDPRounding = 1, isReMaterializable = 0

let OtherPredicates = [NotHasTrue16BitInsts] in
defm V_CVT_F32_F16 : VOP1Inst <"v_cvt_f32_f16", VOP_F32_F16, any_fpextend>;
let OtherPredicates = [HasTrue16BitInsts] in
defm V_CVT_F32_F16_t16 : VOP1Inst <"v_cvt_f32_f16_t16", VOPProfile_Fake16<VOP_F32_F16>, any_fpextend>;

let ReadsModeReg = 0, mayRaiseFPException = 0 in {
defm V_CVT_RPI_I32_F32 : VOP1Inst <"v_cvt_rpi_i32_f32", VOP_I32_F32, cvt_rpi_i32_f32>;
defm V_CVT_FLR_I32_F32 : VOP1Inst <"v_cvt_flr_i32_f32", VOP_I32_F32, cvt_flr_i32_f32>;
defm V_CVT_OFF_F32_I4 : VOP1Inst  <"v_cvt_off_f32_i4", VOP1_F32_I32>;
} // End ReadsModeReg = 0, mayRaiseFPException = 0
} // End SchedRW = [WriteFloatCvt]

let ReadsModeReg = 0, mayRaiseFPException = 0 in {
defm V_CVT_F32_UBYTE0 : VOP1Inst <"v_cvt_f32_ubyte0", VOP1_F32_I32, AMDGPUcvt_f32_ubyte0>;
defm V_CVT_F32_UBYTE1 : VOP1Inst <"v_cvt_f32_ubyte1", VOP1_F32_I32, AMDGPUcvt_f32_ubyte1>;
defm V_CVT_F32_UBYTE2 : VOP1Inst <"v_cvt_f32_ubyte2", VOP1_F32_I32, AMDGPUcvt_f32_ubyte2>;
defm V_CVT_F32_UBYTE3 : VOP1Inst <"v_cvt_f32_ubyte3", VOP1_F32_I32, AMDGPUcvt_f32_ubyte3>;
} // ReadsModeReg = 0, mayRaiseFPException = 0

defm V_FRACT_F32 : VOP1Inst <"v_fract_f32", VOP_F32_F32, AMDGPUfract>;
defm V_TRUNC_F32 : VOP1Inst <"v_trunc_f32", VOP_F32_F32, ftrunc>;
defm V_CEIL_F32 : VOP1Inst <"v_ceil_f32", VOP_F32_F32, fceil>;
defm V_RNDNE_F32 : VOP1Inst <"v_rndne_f32", VOP_F32_F32, froundeven>;
defm V_FLOOR_F32 : VOP1Inst <"v_floor_f32", VOP_F32_F32, ffloor>;

let TRANS = 1, SchedRW = [WriteTrans32] in {
defm V_EXP_F32 : VOP1Inst <"v_exp_f32", VOP_F32_F32, AMDGPUexp>;
defm V_LOG_F32 : VOP1Inst <"v_log_f32", VOP_F32_F32, AMDGPUlog>;
defm V_RCP_F32 : VOP1Inst <"v_rcp_f32", VOP_F32_F32, AMDGPUrcp>;
defm V_RCP_IFLAG_F32 : VOP1Inst <"v_rcp_iflag_f32", VOP_F32_F32, AMDGPUrcp_iflag>;
defm V_RSQ_F32 : VOP1Inst <"v_rsq_f32", VOP_F32_F32, AMDGPUrsq>;
defm V_SQRT_F32 : VOP1Inst <"v_sqrt_f32", VOP_F32_F32, int_amdgcn_sqrt>;
} // End TRANS = 1, SchedRW = [WriteTrans32]

let TRANS = 1, SchedRW = [WriteTrans64] in {
defm V_RCP_F64 : VOP1Inst <"v_rcp_f64", VOP_F64_F64, AMDGPUrcp>;
defm V_RSQ_F64 : VOP1Inst <"v_rsq_f64", VOP_F64_F64, AMDGPUrsq>;
defm V_SQRT_F64 : VOP1Inst <"v_sqrt_f64", VOP_F64_F64, int_amdgcn_sqrt>;
} // End TRANS = 1, SchedRW = [WriteTrans64]

let TRANS = 1, SchedRW = [WriteTrans32] in {
defm V_SIN_F32 : VOP1Inst <"v_sin_f32", VOP_F32_F32, AMDGPUsin>;
defm V_COS_F32 : VOP1Inst <"v_cos_f32", VOP_F32_F32, AMDGPUcos>;
} // End TRANS = 1, SchedRW = [WriteTrans32]

defm V_NOT_B32 : VOP1Inst <"v_not_b32", VOP_I32_I32>;
defm V_BFREV_B32 : VOP1Inst <"v_bfrev_b32", VOP_I32_I32, DivergentUnaryFrag<bitreverse>>;
defm V_FFBH_U32 : VOP1Inst <"v_ffbh_u32", VOP_I32_I32, AMDGPUffbh_u32>;
defm V_FFBL_B32 : VOP1Inst <"v_ffbl_b32", VOP_I32_I32, AMDGPUffbl_b32>;
defm V_FFBH_I32 : VOP1Inst <"v_ffbh_i32", VOP_I32_I32, AMDGPUffbh_i32>;

let SchedRW = [WriteDoubleAdd] in {
defm V_FREXP_EXP_I32_F64 : VOP1Inst <"v_frexp_exp_i32_f64", VOP_I32_F64_SPECIAL_OMOD, int_amdgcn_frexp_exp>;
defm V_FREXP_MANT_F64 : VOP1Inst <"v_frexp_mant_f64", VOP_F64_F64, int_amdgcn_frexp_mant>;
let FPDPRounding = 1 in {
defm V_FRACT_F64 : VOP1Inst <"v_fract_f64", VOP_F64_F64, AMDGPUfract>;
} // End FPDPRounding = 1
} // End SchedRW = [WriteDoubleAdd]

defm V_FREXP_EXP_I32_F32 : VOP1Inst <"v_frexp_exp_i32_f32", VOP_I32_F32, int_amdgcn_frexp_exp>;
defm V_FREXP_MANT_F32 : VOP1Inst <"v_frexp_mant_f32", VOP_F32_F32, int_amdgcn_frexp_mant>;
} // End isReMaterializable = 1

defm V_CLREXCP : VOP1Inst <"v_clrexcp", VOP_NO_EXT<VOP_NONE>>;

// Restrict src0 to be VGPR
def VOP_MOVRELS : VOPProfile<[i32, i32, untyped, untyped]> {
  let Src0RC32 = VRegSrc_32;
  let Src0RC64 = VRegSrc_32;
  let IsInvalidSingleUseConsumer = 1;
}

// Special case because there are no true output operands.  Hack vdst
// to be a src operand. The custom inserter must add a tied implicit
// def and use of the super register since there seems to be no way to
// add an implicit def of a virtual register in tablegen.
class VOP_MOVREL<RegisterOperand Src1RC> : VOPProfile<[untyped, i32, untyped, untyped]> {
  let Src0RC32 = VOPDstOperand<VGPR_32>;
  let Src0RC64 = VOPDstOperand<VGPR_32>;

  let Outs = (outs);
  let Ins32 = (ins Src0RC32:$vdst, Src1RC:$src0);
  let Ins64 = (ins Src0RC64:$vdst, Src1RC:$src0);
  let Asm32 = getAsm32<1, 1>.ret;

  let OutsSDWA = (outs Src0RC32:$vdst);
  let InsSDWA = (ins Src0ModSDWA:$src0_modifiers, Src0SDWA:$src0,
                     Clamp:$clamp, dst_sel:$dst_sel, dst_unused:$dst_unused,
                     src0_sel:$src0_sel);
  let AsmSDWA9 = getAsmSDWA9<1, 0, 1>.ret;

  let OutsDPP = (outs Src0RC32:$vdst);
  let InsDPP16 = (ins Src0RC32:$old, Src0RC32:$src0,
                      dpp_ctrl:$dpp_ctrl, DppRowMask:$row_mask,
                      DppBankMask:$bank_mask, DppBoundCtrl:$bound_ctrl, Dpp16FI:$fi);
  let AsmDPP16 = getAsmDPP16<1, 1, 0>.ret;
  let InsDPP8 = (ins Src0RC32:$old, Src0RC32:$src0, dpp8:$dpp8, Dpp8FI:$fi);
  let AsmDPP8 = getAsmDPP8<1, 1, 0>.ret;

  let OutsVOP3DPP = (outs Src0RC64:$vdst);
  let InsVOP3DPP = getInsVOP3DPP<InsVOP3Base, Src0RC64, NumSrcArgs>.ret;
  let InsVOP3DPP16 = getInsVOP3DPP16<InsVOP3Base, Src0RC64, NumSrcArgs>.ret;
  let InsVOP3DPP8 = getInsVOP3DPP8<InsVOP3Base, Src0RC64, NumSrcArgs>.ret;

  let AsmVOP3Base =
      getAsmVOP3Base<NumSrcArgs, 1 /* HasDst */, HasClamp,
                        HasOpSel, HasOMod, IsVOP3P, HasModifiers,
                        HasModifiers, HasModifiers, HasModifiers>.ret;

  let HasDst = 0;
  let EmitDst = 1; // force vdst emission
}

let IsInvalidSingleUseProducer = 1 in {
  def VOP_MOVRELD : VOP_MOVREL<VSrc_b32>;
  def VOP_MOVRELSD : VOP_MOVREL<VRegSrc_32> {
    let IsInvalidSingleUseConsumer = 1;
  }
}

let SubtargetPredicate = HasMovrel, Uses = [M0, EXEC] in {
 // v_movreld_b32 is a special case because the destination output
 // register is really a source. It isn't actually read (but may be
 // written), and is only to provide the base register to start
 // indexing from. Tablegen seems to not let you define an implicit
 // virtual register output for the super register being written into,
 // so this must have an implicit def of the register added to it.
defm V_MOVRELD_B32 : VOP1Inst <"v_movreld_b32", VOP_MOVRELD>;
defm V_MOVRELS_B32 : VOP1Inst <"v_movrels_b32", VOP_MOVRELS>;
defm V_MOVRELSD_B32 : VOP1Inst <"v_movrelsd_b32", VOP_MOVRELSD>;
} // End Uses = [M0, EXEC]

let isReMaterializable = 1 in {
let SubtargetPredicate = isGFX6GFX7 in {
  let TRANS = 1, SchedRW = [WriteTrans32] in {
    defm V_LOG_CLAMP_F32 :
      VOP1Inst<"v_log_clamp_f32", VOP_F32_F32, int_amdgcn_log_clamp>;
    defm V_RCP_CLAMP_F32 :
      VOP1Inst<"v_rcp_clamp_f32", VOP_F32_F32>;
    defm V_RCP_LEGACY_F32 :
      VOP1Inst<"v_rcp_legacy_f32", VOP_F32_F32, AMDGPUrcp_legacy>;
    defm V_RSQ_CLAMP_F32 :
      VOP1Inst<"v_rsq_clamp_f32", VOP_F32_F32, AMDGPUrsq_clamp>;
    defm V_RSQ_LEGACY_F32 :
      VOP1Inst<"v_rsq_legacy_f32", VOP_F32_F32, int_amdgcn_rsq_legacy>;
  } // End TRANS = 1, SchedRW = [WriteTrans32]

  let SchedRW = [WriteTrans64] in {
    defm V_RCP_CLAMP_F64 :
      VOP1Inst<"v_rcp_clamp_f64", VOP_F64_F64>;
    defm V_RSQ_CLAMP_F64 :
      VOP1Inst<"v_rsq_clamp_f64", VOP_F64_F64, AMDGPUrsq_clamp>;
  } // End SchedRW = [WriteTrans64]
} // End SubtargetPredicate = isGFX6GFX7

let SubtargetPredicate = isGFX7GFX8GFX9 in {
  let TRANS = 1, SchedRW = [WriteTrans32] in {
    defm V_LOG_LEGACY_F32 : VOP1Inst<"v_log_legacy_f32", VOP_F32_F32>;
    defm V_EXP_LEGACY_F32 : VOP1Inst<"v_exp_legacy_f32", VOP_F32_F32>;
  } // End TRANS = 1, SchedRW = [WriteTrans32]
} // End SubtargetPredicate = isGFX7GFX8GFX9

let SubtargetPredicate = isGFX7Plus in {
  let SchedRW = [WriteDoubleAdd] in {
    defm V_TRUNC_F64 : VOP1Inst<"v_trunc_f64", VOP_F64_F64, ftrunc>;
    defm V_CEIL_F64  : VOP1Inst<"v_ceil_f64", VOP_F64_F64, fceil>;
    defm V_RNDNE_F64 : VOP1Inst<"v_rndne_f64", VOP_F64_F64, froundeven>;
    defm V_FLOOR_F64 : VOP1Inst<"v_floor_f64", VOP_F64_F64, ffloor>;
  } // End SchedRW = [WriteDoubleAdd]
} // End SubtargetPredicate = isGFX7Plus
} // End isReMaterializable = 1

let FPDPRounding = 1 in {
let OtherPredicates = [Has16BitInsts, NotHasTrue16BitInsts] in {
defm V_CVT_F16_U16 : VOP1Inst <"v_cvt_f16_u16", VOP1_F16_I16, uint_to_fp>;
defm V_CVT_F16_I16 : VOP1Inst <"v_cvt_f16_i16", VOP1_F16_I16, sint_to_fp>;
}
let OtherPredicates = [HasTrue16BitInsts] in {
defm V_CVT_F16_U16_t16 : VOP1Inst <"v_cvt_f16_u16_t16", VOP1_F16_I16_t16, uint_to_fp>;
defm V_CVT_F16_I16_t16 : VOP1Inst <"v_cvt_f16_i16_t16", VOP1_F16_I16_t16, sint_to_fp>;
}
} // End FPDPRounding = 1
// OMod clears exceptions when set in these two instructions
let OtherPredicates = [Has16BitInsts, NotHasTrue16BitInsts] in {
defm V_CVT_U16_F16 : VOP1Inst <"v_cvt_u16_f16", VOP_I16_F16_SPECIAL_OMOD, fp_to_uint>;
defm V_CVT_I16_F16 : VOP1Inst <"v_cvt_i16_f16", VOP_I16_F16_SPECIAL_OMOD, fp_to_sint>;
}
let OtherPredicates = [HasTrue16BitInsts] in {
defm V_CVT_U16_F16_t16 : VOP1Inst <"v_cvt_u16_f16_t16", VOP_I16_F16_SPECIAL_OMOD_t16, fp_to_uint>;
defm V_CVT_I16_F16_t16 : VOP1Inst <"v_cvt_i16_f16_t16", VOP_I16_F16_SPECIAL_OMOD_t16, fp_to_sint>;
}
let TRANS = 1, SchedRW = [WriteTrans32] in {
defm V_RCP_F16 : VOP1Inst_t16 <"v_rcp_f16", VOP_F16_F16, AMDGPUrcp>;
defm V_SQRT_F16 : VOP1Inst_t16 <"v_sqrt_f16", VOP_F16_F16, any_amdgcn_sqrt>;
defm V_RSQ_F16 : VOP1Inst_t16 <"v_rsq_f16", VOP_F16_F16, AMDGPUrsq>;
defm V_LOG_F16 : VOP1Inst_t16 <"v_log_f16", VOP_F16_F16, AMDGPUlogf16>;
defm V_EXP_F16 : VOP1Inst_t16 <"v_exp_f16", VOP_F16_F16, AMDGPUexpf16>;
defm V_SIN_F16 : VOP1Inst_t16 <"v_sin_f16", VOP_F16_F16, AMDGPUsin>;
defm V_COS_F16 : VOP1Inst_t16 <"v_cos_f16", VOP_F16_F16, AMDGPUcos>;
} // End TRANS = 1, SchedRW = [WriteTrans32]
defm V_FREXP_MANT_F16 : VOP1Inst_t16 <"v_frexp_mant_f16", VOP_F16_F16, int_amdgcn_frexp_mant>;
let OtherPredicates = [Has16BitInsts, NotHasTrue16BitInsts] in {
defm V_FREXP_EXP_I16_F16 : VOP1Inst <"v_frexp_exp_i16_f16", VOP_I16_F16_SPECIAL_OMOD, int_amdgcn_frexp_exp>;
}
let OtherPredicates = [HasTrue16BitInsts] in {
defm V_FREXP_EXP_I16_F16_t16 : VOP1Inst <"v_frexp_exp_i16_f16_t16", VOP_I16_F16_SPECIAL_OMOD_t16, int_amdgcn_frexp_exp>;
}
defm V_FLOOR_F16 : VOP1Inst_t16 <"v_floor_f16", VOP_F16_F16, ffloor>;
defm V_CEIL_F16 : VOP1Inst_t16 <"v_ceil_f16", VOP_F16_F16, fceil>;
defm V_TRUNC_F16 : VOP1Inst_t16 <"v_trunc_f16", VOP_F16_F16, ftrunc>;
defm V_RNDNE_F16 : VOP1Inst_t16 <"v_rndne_f16", VOP_F16_F16, froundeven>;
let FPDPRounding = 1 in {
defm V_FRACT_F16 : VOP1Inst_t16 <"v_fract_f16", VOP_F16_F16, AMDGPUfract>;
} // End FPDPRounding = 1

let OtherPredicates = [Has16BitInsts, NotHasTrue16BitInsts] in {
def : GCNPat<
    (f32 (f16_to_fp i16:$src)),
    (V_CVT_F32_F16_e32 $src)
>;
def : GCNPat<
    (i16 (AMDGPUfp_to_f16 f32:$src)),
    (V_CVT_F16_F32_e32 $src)
>;
}
let OtherPredicates = [HasTrue16BitInsts] in {
def : GCNPat<
    (f32 (f16_to_fp i16:$src)),
    (V_CVT_F32_F16_t16_e32 $src)
>;
def : GCNPat<
    (i16 (AMDGPUfp_to_f16 f32:$src)),
    (V_CVT_F16_F32_t16_e32 $src)
>;
}

def VOP_SWAP_I32 : VOPProfile<[i32, i32, untyped, untyped]> {
  let Outs32 = (outs VGPR_32:$vdst, VRegSrc_32:$vdst1);
  let Ins32 = (ins VRegSrc_32:$src0, VGPR_32:$src1);
  let Asm32 = " $vdst, $src0";
}

let SubtargetPredicate = isGFX9Plus in {
  def V_SWAP_B32 : VOP1_Pseudo<"v_swap_b32", VOP_SWAP_I32, [], 1> {
    let Constraints = "$vdst = $src1, $vdst1 = $src0";
    let DisableEncoding = "$vdst1,$src1";
    let SchedRW = [Write64Bit, Write64Bit];
    let IsInvalidSingleUseConsumer = 1;
  }

  let isReMaterializable = 1 in
  defm V_SAT_PK_U8_I16    : VOP1Inst_t16<"v_sat_pk_u8_i16", VOP_I16_I32>;

  let mayRaiseFPException = 0 in {
    let OtherPredicates = [Has16BitInsts, NotHasTrue16BitInsts] in {
      defm V_CVT_NORM_I16_F16 : VOP1Inst<"v_cvt_norm_i16_f16", VOP_I16_F16_SPECIAL_OMOD>;
      defm V_CVT_NORM_U16_F16 : VOP1Inst<"v_cvt_norm_u16_f16", VOP_I16_F16_SPECIAL_OMOD>;
    }
    let OtherPredicates = [HasTrue16BitInsts] in {
      defm V_CVT_NORM_I16_F16_t16 : VOP1Inst<"v_cvt_norm_i16_f16_t16", VOP_I16_F16_SPECIAL_OMOD_t16>;
      defm V_CVT_NORM_U16_F16_t16 : VOP1Inst<"v_cvt_norm_u16_f16_t16", VOP_I16_F16_SPECIAL_OMOD_t16>;
    }
  } // End mayRaiseFPException = 0
} // End SubtargetPredicate = isGFX9Plus

let SubtargetPredicate = isGFX9Only in {
  defm V_SCREEN_PARTITION_4SE_B32 : VOP1Inst <"v_screen_partition_4se_b32", VOP_I32_I32>;
} // End SubtargetPredicate = isGFX9Only

class VOPProfile_Base_CVT_F32_F8<ValueType vt> : VOPProfileI2F <vt, i32> {
  let HasExtDPP = 1;
  let HasExtSDWA = 1;
  let HasExtSDWA9 = 1;
  let HasExt = 1;
  let DstRCSDWA = getVALUDstForVT<vt>.ret;
  let InsSDWA = (ins Bin32SDWAInputMods:$src0_modifiers, Src0SDWA:$src0,
                     Clamp:$clamp, omod:$omod, src0_sel:$src0_sel);
  let AsmSDWA = "$vdst, $src0_modifiers$clamp$omod $src0_sel"; // No dst_sel
  let AsmSDWA9 = AsmSDWA;
  let EmitDstSel = 0;
}

def VOPProfileCVT_F32_F8    : VOPProfile_Base_CVT_F32_F8 <f32>;
def VOPProfileCVT_PK_F32_F8 : VOPProfile_Base_CVT_F32_F8 <v2f32>;

let OtherPredicates = [HasFP8ConversionInsts], mayRaiseFPException = 0,
    SchedRW = [WriteFloatCvt] in {
  defm V_CVT_F32_FP8    : VOP1Inst<"v_cvt_f32_fp8", VOPProfileCVT_F32_F8>;
  defm V_CVT_F32_BF8    : VOP1Inst<"v_cvt_f32_bf8", VOPProfileCVT_F32_F8>;
  defm V_CVT_PK_F32_FP8 : VOP1Inst<"v_cvt_pk_f32_fp8", VOPProfileCVT_PK_F32_F8>;
  defm V_CVT_PK_F32_BF8 : VOP1Inst<"v_cvt_pk_f32_bf8", VOPProfileCVT_PK_F32_F8>;
}

class Cvt_F32_F8_Pat<SDPatternOperator node, int index,
    VOP1_SDWA_Pseudo inst_sdwa> : GCNPat<
    (f32 (node i32:$src, index)),
    (inst_sdwa 0, $src, 0, 0, index)
>;

let SubtargetPredicate = isGFX9Only in {
let OtherPredicates = [HasCvtFP8VOP1Bug] in {
  def : GCNPat<(f32 (int_amdgcn_cvt_f32_fp8 i32:$src, 0)),
               (V_CVT_F32_FP8_sdwa 0, $src, 0, 0, 0)>;
  def : GCNPat<(f32 (int_amdgcn_cvt_f32_bf8 i32:$src, 0)),
               (V_CVT_F32_BF8_sdwa 0, $src, 0, 0, 0)>;
}

let OtherPredicates = [HasNoCvtFP8VOP1Bug] in {
  def : GCNPat<(f32 (int_amdgcn_cvt_f32_fp8 i32:$src, 0)),
               (V_CVT_F32_FP8_e32 $src)>;
  def : GCNPat<(f32 (int_amdgcn_cvt_f32_bf8 i32:$src, 0)),
               (V_CVT_F32_BF8_e32 $src)>;
}

foreach Index = [1, 2, 3] in {
  def : Cvt_F32_F8_Pat<int_amdgcn_cvt_f32_fp8, Index, V_CVT_F32_FP8_sdwa>;
  def : Cvt_F32_F8_Pat<int_amdgcn_cvt_f32_bf8, Index, V_CVT_F32_BF8_sdwa>;
}
} // End SubtargetPredicate = isGFX9Only

class Cvt_PK_F32_F8_Pat<SDPatternOperator node, int index,
    VOP1_Pseudo inst_e32, VOP1_SDWA_Pseudo inst_sdwa> : GCNPat<
    (v2f32 (node i32:$src, index)),
    !if (index,
         (inst_sdwa 0, $src, 0, 0, SDWA.WORD_1),
         (inst_e32 $src))
>;

let SubtargetPredicate = isGFX9Only in {
  foreach Index = [0, -1] in {
    def : Cvt_PK_F32_F8_Pat<int_amdgcn_cvt_pk_f32_fp8, Index,
                            V_CVT_PK_F32_FP8_e32, V_CVT_PK_F32_FP8_sdwa>;
    def : Cvt_PK_F32_F8_Pat<int_amdgcn_cvt_pk_f32_bf8, Index,
                            V_CVT_PK_F32_BF8_e32, V_CVT_PK_F32_BF8_sdwa>;
  }
}


// Similar to VOPProfile_Base_CVT_F32_F8, but for VOP3 instructions.
def VOPProfile_Base_CVT_PK_F32_F8_OpSel : VOPProfileI2F <v2f32, i32> {
  let HasOpSel = 1;
  let HasExtVOP3DPP = 0;
}

class VOPProfile_Base_CVT_F_F8_ByteSel<ValueType DstVT> : VOPProfile<[DstVT, i32, untyped, untyped]> {
  let IsFP8SrcByteSel = 1;
  let HasOpSel = 0;
  let HasExtDPP = 1;
  let HasExtVOP3DPP = 1;
  let HasExtSDWA = 0;
  let HasClamp = 0;
  let HasOMod = 0;
  let HasModifiers = 0;

  defvar bytesel = (ins ByteSel:$byte_sel);
  let Ins64 = !con(getIns64<Src0RC64, Src1RC64, Src2RC64, NumSrcArgs,
                            HasClamp, HasModifiers, HasSrc2Mods,
                            HasOMod, Src0Mod, Src1Mod, Src2Mod>.ret,
                   bytesel);
  let InsVOP3Base = !con(getInsVOP3Base<Src0VOP3DPP, Src1VOP3DPP, Src2VOP3DPP,
                                        NumSrcArgs, HasClamp, HasModifiers, HasSrc2Mods,
                                        HasOMod, Src0ModVOP3DPP, Src1ModVOP3DPP,
                                        Src2ModVOP3DPP, HasOpSel>.ret,
                         bytesel);
}

let SubtargetPredicate = isGFX12Plus, OtherPredicates = [HasFP8ConversionInsts],
    mayRaiseFPException = 0, SchedRW = [WriteFloatCvt] in {
  defm V_CVT_F32_FP8_OP_SEL    : VOP1Inst<"v_cvt_f32_fp8_op_sel", VOPProfile_Base_CVT_F_F8_ByteSel<f32>>;
  defm V_CVT_F32_BF8_OP_SEL    : VOP1Inst<"v_cvt_f32_bf8_op_sel", VOPProfile_Base_CVT_F_F8_ByteSel<f32>>;
  defm V_CVT_PK_F32_FP8_OP_SEL : VOP1Inst<"v_cvt_pk_f32_fp8_op_sel", VOPProfile_Base_CVT_PK_F32_F8_OpSel>;
  defm V_CVT_PK_F32_BF8_OP_SEL : VOP1Inst<"v_cvt_pk_f32_bf8_op_sel", VOPProfile_Base_CVT_PK_F32_F8_OpSel>;
}

class Cvt_F_F8_Pat_ByteSel<SDPatternOperator node, VOP3_Pseudo inst> : GCNPat<
  (node i32:$src0, timm:$byte_sel),
  (inst $src0, (as_i32timm $byte_sel))
>;

let SubtargetPredicate = isGFX12Plus, OtherPredicates = [HasFP8ConversionInsts] in {
  def : Cvt_F_F8_Pat_ByteSel<int_amdgcn_cvt_f32_fp8, V_CVT_F32_FP8_OP_SEL_e64>;
  def : Cvt_F_F8_Pat_ByteSel<int_amdgcn_cvt_f32_bf8, V_CVT_F32_BF8_OP_SEL_e64>;
}

class Cvt_PK_F32_F8_Pat_OpSel<SDPatternOperator node, int index,
    VOP1_Pseudo inst_e32, VOP3_Pseudo inst_e64> : GCNPat<
    (v2f32 (node i32:$src, index)),
    !if (index,
         (inst_e64 SRCMODS.OP_SEL_0, $src, 0, 0, SRCMODS.NONE),
         (inst_e32 $src))
>;

let SubtargetPredicate = isGFX12Plus, OtherPredicates = [HasFP8ConversionInsts] in {
  foreach Index = [0, -1] in {
    def : Cvt_PK_F32_F8_Pat_OpSel<int_amdgcn_cvt_pk_f32_fp8, Index,
                                  V_CVT_PK_F32_FP8_e32, V_CVT_PK_F32_FP8_OP_SEL_e64>;
    def : Cvt_PK_F32_F8_Pat_OpSel<int_amdgcn_cvt_pk_f32_bf8, Index,
                                  V_CVT_PK_F32_BF8_e32, V_CVT_PK_F32_BF8_OP_SEL_e64>;
  }
}

let SubtargetPredicate = isGFX10Plus in {
  defm V_PIPEFLUSH        : VOP1Inst<"v_pipeflush", VOP_NO_EXT<VOP_NONE>>;

  let Uses = [M0] in {
    defm V_MOVRELSD_2_B32 :
      VOP1Inst<"v_movrelsd_2_b32", VOP_MOVRELSD>;

    def V_SWAPREL_B32 : VOP1_Pseudo<"v_swaprel_b32", VOP_SWAP_I32, [], 1> {
      let Constraints = "$vdst = $src1, $vdst1 = $src0";
      let DisableEncoding = "$vdst1,$src1";
      let SchedRW = [Write64Bit, Write64Bit];
      let IsInvalidSingleUseConsumer = 1;
      let IsInvalidSingleUseProducer = 1;
    }
  } // End Uses = [M0]
} // End SubtargetPredicate = isGFX10Plus

def VOPProfileAccMov : VOP_NO_EXT<VOP_I32_I32> {
  let DstRC = RegisterOperand<AGPR_32>;
  let Src0RC32 = ARegSrc_32;
  let Asm32 = " $vdst, $src0";
}

def V_ACCVGPR_MOV_B32 : VOP1_Pseudo<"v_accvgpr_mov_b32", VOPProfileAccMov, [], 1> {
  let SubtargetPredicate = isGFX90APlus;
  let isReMaterializable = 1;
  let isAsCheapAsAMove = 1;
}

let SubtargetPredicate = isGFX11Plus in {
  // Restrict src0 to be VGPR
  def V_PERMLANE64_B32 : VOP1_Pseudo<"v_permlane64_b32", VOP_MOVRELS,
                                      [], /*VOP1Only=*/ 1> {
    let IsInvalidSingleUseConsumer = 1;
    let IsInvalidSingleUseProducer = 1;
  }
  defm V_MOV_B16_t16    : VOP1Inst<"v_mov_b16_t16", VOPProfile_True16<VOP_I16_I16>>;
  defm V_NOT_B16        : VOP1Inst_t16<"v_not_b16", VOP_I16_I16>;
  defm V_CVT_I32_I16    : VOP1Inst_t16<"v_cvt_i32_i16", VOP_I32_I16>;
  defm V_CVT_U32_U16    : VOP1Inst_t16<"v_cvt_u32_u16", VOP_I32_I16>;
} // End SubtargetPredicate = isGFX11Plus

foreach vt = Reg32Types.types in {
  def : GCNPat<(int_amdgcn_permlane64 (vt VRegSrc_32:$src0)),
        (vt (V_PERMLANE64_B32 (vt VRegSrc_32:$src0)))
  >;
}

//===----------------------------------------------------------------------===//
// Target-specific instruction encodings.
//===----------------------------------------------------------------------===//

class VOP1_DPP<bits<8> op, VOP1_DPP_Pseudo ps, VOPProfile p = ps.Pfl, bit isDPP16 = 0> :
    VOP_DPP<ps.OpName, p, isDPP16> {
  let hasSideEffects = ps.hasSideEffects;
  let Defs = ps.Defs;
  let SchedRW = ps.SchedRW;
  let Uses = ps.Uses;
  let TRANS = ps.TRANS;
  let SubtargetPredicate = ps.SubtargetPredicate;
  let OtherPredicates = ps.OtherPredicates;

  bits<8> vdst;
  let Inst{8-0}   = 0xfa;
  let Inst{16-9}  = op;
  let Inst{24-17} = !if(p.EmitDst, vdst{7-0}, 0);
  let Inst{31-25} = 0x3f;
}

class VOP1_DPP16<bits<8> op, VOP1_DPP_Pseudo ps, int subtarget, VOPProfile p = ps.Pfl> :
    VOP1_DPP<op, ps, p, 1>,
    SIMCInstr <ps.PseudoInstr, subtarget> {
  let AssemblerPredicate = HasDPP16;
}

class VOP1_DPP16_Gen<bits<8> op, VOP1_DPP_Pseudo ps, GFXGen Gen, VOPProfile p = ps.Pfl> :
    VOP1_DPP16 <op, ps, Gen.Subtarget, p> {
  let AssemblerPredicate = Gen.AssemblerPredicate;
  let DecoderNamespace = Gen.DecoderNamespace;
}

class VOP1_DPP8<bits<8> op, VOP1_Pseudo ps, VOPProfile p = ps.Pfl> :
    VOP_DPP8<ps.OpName, p> {
  let hasSideEffects = ps.hasSideEffects;
  let Defs = ps.Defs;
  let SchedRW = ps.SchedRW;
  let Uses = ps.Uses;
  let SubtargetPredicate = ps.SubtargetPredicate;
  let OtherPredicates = ps.OtherPredicates;

  bits<8> vdst;
  let Inst{8-0}   = fi;
  let Inst{16-9}  = op;
  let Inst{24-17} = !if(p.EmitDst, vdst{7-0}, 0);
  let Inst{31-25} = 0x3f;
}

class VOP1_DPP8_Gen<bits<8> op, VOP1_Pseudo ps, GFXGen Gen, VOPProfile p = ps.Pfl> :
    VOP1_DPP8<op, ps, p> {
  let AssemblerPredicate = Gen.AssemblerPredicate;
  let DecoderNamespace = Gen.DecoderNamespace;
}

//===----------------------------------------------------------------------===//
// GFX11, GFX12
//===----------------------------------------------------------------------===//

multiclass VOP1Only_Real<GFXGen Gen, bits<9> op> {
  let IsSingle = 1 in
    def Gen.Suffix :
      VOP1_Real_Gen<!cast<VOP1_Pseudo>(NAME), Gen>,
      VOP1e<op{7-0}, !cast<VOP1_Pseudo>(NAME).Pfl>;
}

multiclass VOP1_Real_e32<GFXGen Gen, bits<9> op, string opName = NAME> {
  defvar ps = !cast<VOP1_Pseudo>(opName#"_e32");
  def _e32#Gen.Suffix :
    VOP1_Real_Gen<ps, Gen>,
    VOP1e<op{7-0}, ps.Pfl>;
}

multiclass VOP1_Real_e32_with_name<GFXGen Gen, bits<9> op, string opName,
                                   string asmName> {
  defvar ps = !cast<VOP1_Pseudo>(opName#"_e32");
  let AsmString = asmName # ps.AsmOperands,
      DecoderNamespace = Gen.DecoderNamespace #
                         !if(ps.Pfl.IsRealTrue16, "", "_FAKE16") in {
    defm NAME : VOP1_Real_e32<Gen, op, opName>;
  }
}

multiclass VOP1_Real_e64<GFXGen Gen, bits<9> op> {
  def _e64#Gen.Suffix :
    VOP3_Real_Gen<!cast<VOP3_Pseudo>(NAME#"_e64"), Gen>,
    VOP3e_gfx11_gfx12<{0, 1, 1, op{6-0}}, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
}

multiclass VOP1_Real_dpp<GFXGen Gen, bits<9> op, string opName = NAME> {
  defvar ps = !cast<VOP1_Pseudo>(opName#"_e32");
  def _dpp#Gen.Suffix : VOP1_DPP16_Gen<op{7-0}, !cast<VOP1_DPP_Pseudo>(opName#"_dpp"), Gen>;
}

multiclass VOP1_Real_dpp_with_name<GFXGen Gen, bits<9> op, string opName,
                                   string asmName> {
  defvar ps = !cast<VOP1_Pseudo>(opName#"_e32");
  let AsmString = asmName # ps.Pfl.AsmDPP16,
      DecoderNamespace = Gen.DecoderNamespace #
                         !if(ps.Pfl.IsRealTrue16, "", "_FAKE16") in {
    defm NAME : VOP1_Real_dpp<Gen, op, opName>;
  }
}

multiclass VOP1_Real_dpp8<GFXGen Gen, bits<9> op, string opName = NAME> {
  defvar ps = !cast<VOP1_Pseudo>(opName#"_e32");
  def _dpp8#Gen.Suffix : VOP1_DPP8_Gen<op{7-0}, ps, Gen>;
}

multiclass VOP1_Real_dpp8_with_name<GFXGen Gen, bits<9> op, string opName,
                                    string asmName> {
  defvar ps = !cast<VOP1_Pseudo>(opName#"_e32");
  let AsmString = asmName # ps.Pfl.AsmDPP8,
      DecoderNamespace = Gen.DecoderNamespace #
                         !if(ps.Pfl.IsRealTrue16, "", "_FAKE16") in {
    defm NAME : VOP1_Real_dpp8<Gen, op, opName>;
  }
}

multiclass VOP1_Realtriple_e64<GFXGen Gen, bits<9> op> {
  defm NAME : VOP3_Realtriple<Gen, {0, 1, 1, op{6-0}}, /*isSingle=*/ 0, NAME>;
}

multiclass VOP1_Realtriple_e64_with_name<GFXGen Gen, bits<9> op, string opName,
  string asmName> {
  defm NAME : VOP3_Realtriple_with_name<Gen, {0, 1, 1, op{6-0}}, opName,
    asmName>;
}

multiclass VOP1_Real_FULL<GFXGen Gen, bits<9> op> :
  VOP1_Real_e32<Gen, op>, VOP1_Realtriple_e64<Gen, op>,
  VOP1_Real_dpp<Gen, op>, VOP1_Real_dpp8<Gen, op>;

multiclass VOP1_Real_NO_VOP3_with_name_gfx11<bits<9> op, string opName,
                                             string asmName> {
  defm NAME : VOP1_Real_e32_with_name<GFX11Gen, op, opName, asmName>,
              VOP1_Real_dpp_with_name<GFX11Gen, op, opName, asmName>,
              VOP1_Real_dpp8_with_name<GFX11Gen, op, opName, asmName>;
  defvar ps = !cast<VOP1_Pseudo>(opName#"_e32");
  def gfx11_alias : AMDGPUMnemonicAlias<ps.Mnemonic, asmName> {
    let AssemblerPredicate = isGFX11Plus;
  }
}

multiclass VOP1_Real_NO_VOP3_with_name_gfx12<bits<9> op, string opName,
                                             string asmName> {
  defm NAME : VOP1_Real_e32_with_name<GFX12Gen, op, opName, asmName>,
              VOP1_Real_dpp_with_name<GFX12Gen, op, opName, asmName>,
              VOP1_Real_dpp8_with_name<GFX12Gen, op, opName, asmName>;
}

multiclass VOP1_Real_FULL_with_name<GFXGen Gen, bits<9> op, string opName,
                                         string asmName> :
  VOP1_Real_e32_with_name<Gen, op, opName, asmName>,
  VOP1_Real_dpp_with_name<Gen, op, opName, asmName>,
  VOP1_Real_dpp8_with_name<Gen, op, opName, asmName>,
  VOP1_Realtriple_e64_with_name<Gen, op, opName, asmName>;

multiclass VOP1_Real_NO_DPP<GFXGen Gen, bits<9> op> :
  VOP1_Real_e32<Gen, op>, VOP1_Real_e64<Gen, op>;

multiclass VOP1_Real_FULL_t16_gfx11_gfx12<bits<9> op, string asmName,
                                          string opName = NAME> :
  VOP1_Real_FULL_with_name<GFX11Gen, op, opName, asmName>,
  VOP1_Real_FULL_with_name<GFX12Gen, op, opName, asmName>;

multiclass VOP1_Real_FULL_with_name_gfx11_gfx12<bits<9> op, string opName,
                                                string asmName> :
  VOP1_Real_FULL_with_name<GFX11Gen, op, opName, asmName>,
  VOP1_Real_FULL_with_name<GFX12Gen, op, opName, asmName>;

multiclass VOP1Only_Real_gfx11_gfx12<bits<9> op> :
  VOP1Only_Real<GFX11Gen, op>, VOP1Only_Real<GFX12Gen, op>;

multiclass VOP1_Real_FULL_gfx11_gfx12<bits<9> op> :
  VOP1_Real_FULL<GFX11Gen, op>, VOP1_Real_FULL<GFX12Gen, op>;

multiclass VOP1_Real_NO_DPP_OP_SEL_with_name<GFXGen Gen, bits<9> op,
                                             string opName, string asmName> :
  VOP1_Real_e32_with_name<Gen, op, opName, asmName>,
  VOP3_Real_with_name<Gen, {0, 1, 1, op{6-0}}, opName, asmName>;


defm V_CVT_F32_FP8      : VOP1_Real_FULL_with_name<GFX12Gen, 0x06c, "V_CVT_F32_FP8_OP_SEL", "v_cvt_f32_fp8">;
defm V_CVT_F32_BF8      : VOP1_Real_FULL_with_name<GFX12Gen, 0x06d, "V_CVT_F32_BF8_OP_SEL", "v_cvt_f32_bf8">;

// Define VOP1 instructions using the pseudo instruction with its old profile and
// VOP3 using the OpSel profile for the pseudo instruction.
defm V_CVT_PK_F32_FP8   : VOP1_Real_e32_with_name<GFX12Gen, 0x06e, "V_CVT_PK_F32_FP8", "v_cvt_pk_f32_fp8">;
defm V_CVT_PK_F32_FP8   : VOP3_Real_with_name<GFX12Gen, 0x1ee, "V_CVT_PK_F32_FP8_OP_SEL", "v_cvt_pk_f32_fp8">;

defm V_CVT_PK_F32_BF8   : VOP1_Real_e32_with_name<GFX12Gen, 0x06f, "V_CVT_PK_F32_BF8", "v_cvt_pk_f32_bf8">;
defm V_CVT_PK_F32_BF8   : VOP3_Real_with_name<GFX12Gen, 0x1ef, "V_CVT_PK_F32_BF8_OP_SEL", "v_cvt_pk_f32_bf8">;

defm V_CVT_NEAREST_I32_F32 : VOP1_Real_FULL_with_name_gfx11_gfx12<0x00c,
  "V_CVT_RPI_I32_F32", "v_cvt_nearest_i32_f32">;
defm V_CVT_FLOOR_I32_F32   : VOP1_Real_FULL_with_name_gfx11_gfx12<0x00d,
  "V_CVT_FLR_I32_F32", "v_cvt_floor_i32_f32">;
defm V_CLZ_I32_U32         : VOP1_Real_FULL_with_name_gfx11_gfx12<0x039,
  "V_FFBH_U32", "v_clz_i32_u32">;
defm V_CTZ_I32_B32         : VOP1_Real_FULL_with_name_gfx11_gfx12<0x03a,
  "V_FFBL_B32", "v_ctz_i32_b32">;
defm V_CLS_I32             : VOP1_Real_FULL_with_name_gfx11_gfx12<0x03b,
  "V_FFBH_I32", "v_cls_i32">;
defm V_PERMLANE64_B32      : VOP1Only_Real_gfx11_gfx12<0x067>;
defm V_MOV_B16_t16           : VOP1_Real_FULL_t16_gfx11_gfx12<0x01c, "v_mov_b16">;
defm V_NOT_B16_fake16        : VOP1_Real_FULL_t16_gfx11_gfx12<0x069, "v_not_b16">;
defm V_CVT_I32_I16_fake16    : VOP1_Real_FULL_t16_gfx11_gfx12<0x06a, "v_cvt_i32_i16">;
defm V_CVT_U32_U16_fake16    : VOP1_Real_FULL_t16_gfx11_gfx12<0x06b, "v_cvt_u32_u16">;

defm V_CVT_F16_U16_t16       : VOP1_Real_FULL_t16_gfx11_gfx12<0x050, "v_cvt_f16_u16">;
defm V_CVT_F16_I16_t16       : VOP1_Real_FULL_t16_gfx11_gfx12<0x051, "v_cvt_f16_i16">;
defm V_CVT_U16_F16_t16       : VOP1_Real_FULL_t16_gfx11_gfx12<0x052, "v_cvt_u16_f16">;
defm V_CVT_I16_F16_t16       : VOP1_Real_FULL_t16_gfx11_gfx12<0x053, "v_cvt_i16_f16">;
defm V_RCP_F16_t16           : VOP1_Real_FULL_t16_gfx11_gfx12<0x054, "v_rcp_f16">;
defm V_RCP_F16_fake16        : VOP1_Real_FULL_t16_gfx11_gfx12<0x054, "v_rcp_f16">;
defm V_SQRT_F16_t16          : VOP1_Real_FULL_t16_gfx11_gfx12<0x055, "v_sqrt_f16">;
defm V_SQRT_F16_fake16       : VOP1_Real_FULL_t16_gfx11_gfx12<0x055, "v_sqrt_f16">;
defm V_RSQ_F16_t16           : VOP1_Real_FULL_t16_gfx11_gfx12<0x056, "v_rsq_f16">;
defm V_RSQ_F16_fake16        : VOP1_Real_FULL_t16_gfx11_gfx12<0x056, "v_rsq_f16">;
defm V_LOG_F16_t16           : VOP1_Real_FULL_t16_gfx11_gfx12<0x057, "v_log_f16">;
defm V_LOG_F16_fake16        : VOP1_Real_FULL_t16_gfx11_gfx12<0x057, "v_log_f16">;
defm V_EXP_F16_t16           : VOP1_Real_FULL_t16_gfx11_gfx12<0x058, "v_exp_f16">;
defm V_EXP_F16_fake16        : VOP1_Real_FULL_t16_gfx11_gfx12<0x058, "v_exp_f16">;
defm V_FREXP_MANT_F16_fake16 : VOP1_Real_FULL_t16_gfx11_gfx12<0x059, "v_frexp_mant_f16">;
defm V_FREXP_EXP_I16_F16_t16 : VOP1_Real_FULL_t16_gfx11_gfx12<0x05a, "v_frexp_exp_i16_f16">;
defm V_FLOOR_F16_t16         : VOP1_Real_FULL_t16_gfx11_gfx12<0x05b, "v_floor_f16">;
defm V_FLOOR_F16_fake16      : VOP1_Real_FULL_t16_gfx11_gfx12<0x05b, "v_floor_f16">;
defm V_CEIL_F16_t16          : VOP1_Real_FULL_t16_gfx11_gfx12<0x05c, "v_ceil_f16">;
defm V_CEIL_F16_fake16       : VOP1_Real_FULL_t16_gfx11_gfx12<0x05c, "v_ceil_f16">;
defm V_TRUNC_F16_fake16      : VOP1_Real_FULL_t16_gfx11_gfx12<0x05d, "v_trunc_f16">;
defm V_RNDNE_F16_fake16      : VOP1_Real_FULL_t16_gfx11_gfx12<0x05e, "v_rndne_f16">;
defm V_FRACT_F16_fake16      : VOP1_Real_FULL_t16_gfx11_gfx12<0x05f, "v_fract_f16">;
defm V_SIN_F16_fake16        : VOP1_Real_FULL_t16_gfx11_gfx12<0x060, "v_sin_f16">;
defm V_COS_F16_fake16        : VOP1_Real_FULL_t16_gfx11_gfx12<0x061, "v_cos_f16">;
defm V_SAT_PK_U8_I16_fake16  : VOP1_Real_FULL_t16_gfx11_gfx12<0x062, "v_sat_pk_u8_i16">;
defm V_CVT_NORM_I16_F16_t16  : VOP1_Real_FULL_t16_gfx11_gfx12<0x063, "v_cvt_norm_i16_f16">;
defm V_CVT_NORM_U16_F16_t16  : VOP1_Real_FULL_t16_gfx11_gfx12<0x064, "v_cvt_norm_u16_f16">;

defm V_CVT_F16_F32_t16       : VOP1_Real_FULL_t16_gfx11_gfx12<0x00a, "v_cvt_f16_f32">;
defm V_CVT_F32_F16_t16       : VOP1_Real_FULL_t16_gfx11_gfx12<0x00b, "v_cvt_f32_f16">;

//===----------------------------------------------------------------------===//
// GFX10.
//===----------------------------------------------------------------------===//

let AssemblerPredicate = isGFX10Only, DecoderNamespace = "GFX10" in {
  multiclass VOP1Only_Real_gfx10<bits<9> op> {
    def _gfx10 :
      VOP1_Real<!cast<VOP1_Pseudo>(NAME), SIEncodingFamily.GFX10>,
      VOP1e<op{7-0}, !cast<VOP1_Pseudo>(NAME).Pfl>;
  }
  multiclass VOP1_Real_e32_gfx10<bits<9> op> {
    def _e32_gfx10 :
      VOP1_Real<!cast<VOP1_Pseudo>(NAME#"_e32"), SIEncodingFamily.GFX10>,
      VOP1e<op{7-0}, !cast<VOP1_Pseudo>(NAME#"_e32").Pfl>;
  }
  multiclass VOP1_Real_e64_gfx10<bits<9> op> {
    def _e64_gfx10 :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.GFX10>,
      VOP3e_gfx10<{0, 1, 1, op{6-0}}, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
  }
  multiclass VOP1_Real_sdwa_gfx10<bits<9> op> {
    if !cast<VOP1_Pseudo>(NAME#"_e32").Pfl.HasExtSDWA9 then
    def _sdwa_gfx10 :
      VOP_SDWA10_Real<!cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa")>,
      VOP1_SDWA9Ae<op{7-0}, !cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;
  }
  multiclass VOP1_Real_dpp_gfx10<bits<9> op> {
    if !cast<VOP1_Pseudo>(NAME#"_e32").Pfl.HasExt32BitDPP then
    def _dpp_gfx10 : VOP1_DPP16<op{7-0}, !cast<VOP1_DPP_Pseudo>(NAME#"_dpp"), SIEncodingFamily.GFX10>;
  }
  multiclass VOP1_Real_dpp8_gfx10<bits<9> op> {
    if !cast<VOP1_Pseudo>(NAME#"_e32").Pfl.HasExt32BitDPP then
    def _dpp8_gfx10 : VOP1_DPP8<op{7-0}, !cast<VOP1_Pseudo>(NAME#"_e32")>;
  }
} // End AssemblerPredicate = isGFX10Only, DecoderNamespace = "GFX10"

multiclass VOP1_Real_gfx10<bits<9> op> :
  VOP1_Real_e32_gfx10<op>, VOP1_Real_e64_gfx10<op>,
  VOP1_Real_sdwa_gfx10<op>, VOP1_Real_dpp_gfx10<op>,
  VOP1_Real_dpp8_gfx10<op>;

multiclass VOP1_Real_gfx10_FULL_gfx11_gfx12<bits<9> op> :
  VOP1_Real_gfx10<op>,
  VOP1_Real_FULL<GFX11Gen, op>,
  VOP1_Real_FULL<GFX12Gen, op>;

multiclass VOP1_Real_gfx10_NO_DPP_gfx11_gfx12<bits<9> op> :
  VOP1_Real_gfx10<op>,
  VOP1_Real_NO_DPP<GFX11Gen, op>,
  VOP1_Real_NO_DPP<GFX12Gen, op>;

multiclass VOP1Only_Real_gfx10_gfx11_gfx12<bits<9> op> :
  VOP1Only_Real_gfx10<op>,
  VOP1Only_Real<GFX11Gen, op>,
  VOP1Only_Real<GFX12Gen, op>;

defm V_PIPEFLUSH         : VOP1_Real_gfx10_NO_DPP_gfx11_gfx12<0x01b>;
defm V_MOVRELSD_2_B32    : VOP1_Real_gfx10_FULL_gfx11_gfx12<0x048>;
defm V_CVT_F16_U16       : VOP1_Real_gfx10<0x050>;
defm V_CVT_F16_I16       : VOP1_Real_gfx10<0x051>;
defm V_CVT_U16_F16       : VOP1_Real_gfx10<0x052>;
defm V_CVT_I16_F16       : VOP1_Real_gfx10<0x053>;
defm V_RCP_F16           : VOP1_Real_gfx10<0x054>;
defm V_SQRT_F16          : VOP1_Real_gfx10<0x055>;
defm V_RSQ_F16           : VOP1_Real_gfx10<0x056>;
defm V_LOG_F16           : VOP1_Real_gfx10<0x057>;
defm V_EXP_F16           : VOP1_Real_gfx10<0x058>;
defm V_FREXP_MANT_F16    : VOP1_Real_gfx10<0x059>;
defm V_FREXP_EXP_I16_F16 : VOP1_Real_gfx10<0x05a>;
defm V_FLOOR_F16         : VOP1_Real_gfx10<0x05b>;
defm V_CEIL_F16          : VOP1_Real_gfx10<0x05c>;
defm V_TRUNC_F16         : VOP1_Real_gfx10<0x05d>;
defm V_RNDNE_F16         : VOP1_Real_gfx10<0x05e>;
defm V_FRACT_F16         : VOP1_Real_gfx10<0x05f>;
defm V_SIN_F16           : VOP1_Real_gfx10<0x060>;
defm V_COS_F16           : VOP1_Real_gfx10<0x061>;
defm V_SAT_PK_U8_I16     : VOP1_Real_gfx10<0x062>;
defm V_CVT_NORM_I16_F16  : VOP1_Real_gfx10<0x063>;
defm V_CVT_NORM_U16_F16  : VOP1_Real_gfx10<0x064>;

defm V_SWAP_B32          : VOP1Only_Real_gfx10_gfx11_gfx12<0x065>;
defm V_SWAPREL_B32       : VOP1Only_Real_gfx10_gfx11_gfx12<0x068>;

//===----------------------------------------------------------------------===//
// GFX7, GFX10, GFX11, GFX12
//===----------------------------------------------------------------------===//

let AssemblerPredicate = isGFX7Only, DecoderNamespace = "GFX7" in {
  multiclass VOP1_Real_e32_gfx7<bits<9> op> {
    def _e32_gfx7 :
      VOP1_Real<!cast<VOP1_Pseudo>(NAME#"_e32"), SIEncodingFamily.SI>,
      VOP1e<op{7-0}, !cast<VOP1_Pseudo>(NAME#"_e32").Pfl>;
  }
  multiclass VOP1_Real_e64_gfx7<bits<9> op> {
    def _e64_gfx7 :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.SI>,
      VOP3e_gfx6_gfx7<{1, 1, op{6-0}}, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
  }
} // End AssemblerPredicate = isGFX7Only, DecoderNamespace = "GFX7"

multiclass VOP1_Real_gfx7<bits<9> op> :
  VOP1_Real_e32_gfx7<op>, VOP1_Real_e64_gfx7<op>;

multiclass VOP1_Real_gfx7_gfx10_NO_DPP_gfx11_gfx12<bits<9> op> :
  VOP1_Real_gfx7<op>, VOP1_Real_gfx10<op>, VOP1_Real_NO_DPP<GFX11Gen, op>,
  VOP1_Real_NO_DPP<GFX12Gen, op>;

defm V_LOG_LEGACY_F32 : VOP1_Real_gfx7<0x045>;
defm V_EXP_LEGACY_F32 : VOP1_Real_gfx7<0x046>;

defm V_TRUNC_F64      : VOP1_Real_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x017>;
defm V_CEIL_F64       : VOP1_Real_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x018>;
defm V_RNDNE_F64      : VOP1_Real_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x019>;
defm V_FLOOR_F64      : VOP1_Real_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x01a>;

//===----------------------------------------------------------------------===//
// GFX6, GFX7, GFX10, GFX11, GFX12
//===----------------------------------------------------------------------===//

let AssemblerPredicate = isGFX6GFX7, DecoderNamespace = "GFX6GFX7" in {
  multiclass VOP1_Real_e32_gfx6_gfx7<bits<9> op> {
    def _e32_gfx6_gfx7 :
      VOP1_Real<!cast<VOP1_Pseudo>(NAME#"_e32"), SIEncodingFamily.SI>,
      VOP1e<op{7-0}, !cast<VOP1_Pseudo>(NAME#"_e32").Pfl>;
  }
  multiclass VOP1_Real_e64_gfx6_gfx7<bits<9> op> {
    def _e64_gfx6_gfx7 :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.SI>,
      VOP3e_gfx6_gfx7<{1, 1, op{6-0}}, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
  }
  multiclass VOP1Only_Real_gfx6_gfx7<bits<9> op> {
    def _gfx6_gfx7 :
      VOP1_Real<!cast<VOP1_Pseudo>(NAME), SIEncodingFamily.SI>,
      VOP1e<op{7-0}, !cast<VOP1_Pseudo>(NAME).Pfl>;
  }
} // End AssemblerPredicate = isGFX6GFX7, DecoderNamespace = "GFX6GFX7"

multiclass VOP1_Real_gfx6_gfx7<bits<9> op> :
  VOP1_Real_e32_gfx6_gfx7<op>, VOP1_Real_e64_gfx6_gfx7<op>;

multiclass VOP1_Real_gfx6_gfx7_gfx10<bits<9> op> :
  VOP1_Real_gfx6_gfx7<op>, VOP1_Real_gfx10<op>;

multiclass VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<bits<9> op> :
  VOP1_Real_gfx6_gfx7_gfx10<op>, VOP1_Real_FULL<GFX11Gen, op>,
  VOP1_Real_FULL<GFX12Gen, op>;

multiclass VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<bits<9> op> :
  VOP1_Real_gfx6_gfx7_gfx10<op>, VOP1_Real_NO_DPP<GFX11Gen, op>,
  VOP1_Real_NO_DPP<GFX12Gen, op>;

multiclass VOP1Only_Real_gfx6_gfx7_gfx10_gfx11_gfx12<bits<9> op> :
  VOP1Only_Real_gfx6_gfx7<op>, VOP1Only_Real_gfx10_gfx11_gfx12<op>;

defm V_LOG_CLAMP_F32     : VOP1_Real_gfx6_gfx7<0x026>;
defm V_RCP_CLAMP_F32     : VOP1_Real_gfx6_gfx7<0x028>;
defm V_RCP_LEGACY_F32    : VOP1_Real_gfx6_gfx7<0x029>;
defm V_RSQ_CLAMP_F32     : VOP1_Real_gfx6_gfx7<0x02c>;
defm V_RSQ_LEGACY_F32    : VOP1_Real_gfx6_gfx7<0x02d>;
defm V_RCP_CLAMP_F64     : VOP1_Real_gfx6_gfx7<0x030>;
defm V_RSQ_CLAMP_F64     : VOP1_Real_gfx6_gfx7<0x032>;

defm V_NOP               : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x000>;
defm V_MOV_B32           : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x001>;
defm V_READFIRSTLANE_B32 : VOP1Only_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x002>;
defm V_CVT_I32_F64       : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x003>;
defm V_CVT_F64_I32       : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x004>;
defm V_CVT_F32_I32       : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x005>;
defm V_CVT_F32_U32       : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x006>;
defm V_CVT_U32_F32       : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x007>;
defm V_CVT_I32_F32       : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x008>;
defm V_CVT_F16_F32       : VOP1_Real_gfx6_gfx7_gfx10<0x00a>;
defm V_CVT_F32_F16       : VOP1_Real_gfx6_gfx7_gfx10<0x00b>;
defm V_CVT_RPI_I32_F32   : VOP1_Real_gfx6_gfx7_gfx10<0x00c>;
defm V_CVT_FLR_I32_F32   : VOP1_Real_gfx6_gfx7_gfx10<0x00d>;
defm V_CVT_OFF_F32_I4    : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x00e>;
defm V_CVT_F32_F64       : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x00f>;
defm V_CVT_F64_F32       : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x010>;
defm V_CVT_F32_UBYTE0    : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x011>;
defm V_CVT_F32_UBYTE1    : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x012>;
defm V_CVT_F32_UBYTE2    : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x013>;
defm V_CVT_F32_UBYTE3    : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x014>;
defm V_CVT_U32_F64       : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x015>;
defm V_CVT_F64_U32       : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x016>;
defm V_FRACT_F32         : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x020>;
defm V_TRUNC_F32         : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x021>;
defm V_CEIL_F32          : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x022>;
defm V_RNDNE_F32         : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x023>;
defm V_FLOOR_F32         : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x024>;
defm V_EXP_F32           : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x025>;
defm V_LOG_F32           : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x027>;
defm V_RCP_F32           : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x02a>;
defm V_RCP_IFLAG_F32     : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x02b>;
defm V_RSQ_F32           : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x02e>;
defm V_RCP_F64           : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x02f>;
defm V_RSQ_F64           : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x031>;
defm V_SQRT_F32          : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x033>;
defm V_SQRT_F64          : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x034>;
defm V_SIN_F32           : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x035>;
defm V_COS_F32           : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x036>;
defm V_NOT_B32           : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x037>;
defm V_BFREV_B32         : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x038>;
defm V_FFBH_U32          : VOP1_Real_gfx6_gfx7_gfx10<0x039>;
defm V_FFBL_B32          : VOP1_Real_gfx6_gfx7_gfx10<0x03a>;
defm V_FFBH_I32          : VOP1_Real_gfx6_gfx7_gfx10<0x03b>;
defm V_FREXP_EXP_I32_F64 : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x03c>;
defm V_FREXP_MANT_F64    : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x03d>;
defm V_FRACT_F64         : VOP1_Real_gfx6_gfx7_gfx10_NO_DPP_gfx11_gfx12<0x03e>;
defm V_FREXP_EXP_I32_F32 : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x03f>;
defm V_FREXP_MANT_F32    : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x040>;
defm V_CLREXCP           : VOP1_Real_gfx6_gfx7_gfx10<0x041>;
defm V_MOVRELD_B32       : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x042>;
defm V_MOVRELS_B32       : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x043>;
defm V_MOVRELSD_B32      : VOP1_Real_gfx6_gfx7_gfx10_FULL_gfx11_gfx12<0x044>;

//===----------------------------------------------------------------------===//
// GFX8, GFX9 (VI).
//===----------------------------------------------------------------------===//

class VOP1_DPPe <bits<8> op, VOP1_DPP_Pseudo ps, VOPProfile P = ps.Pfl> :
  VOP_DPPe <P> {
  bits<8> vdst;
  let Inst{8-0}   = 0xfa; // dpp
  let Inst{16-9}  = op;
  let Inst{24-17} = !if(P.EmitDst, vdst{7-0}, 0);
  let Inst{31-25} = 0x3f; //encoding
}

let AssemblerPredicate = isGFX8GFX9, DecoderNamespace = "GFX8" in {
  multiclass VOP1Only_Real_vi <bits<10> op> {
    def _vi :
      VOP1_Real<!cast<VOP1_Pseudo>(NAME), SIEncodingFamily.VI>,
      VOP1e<op{7-0}, !cast<VOP1_Pseudo>(NAME).Pfl>;
  }

  multiclass VOP1_Real_e32e64_vi <bits<10> op> {
    def _e32_vi :
      VOP1_Real<!cast<VOP1_Pseudo>(NAME#"_e32"), SIEncodingFamily.VI>,
      VOP1e<op{7-0}, !cast<VOP1_Pseudo>(NAME#"_e32").Pfl>;
    def _e64_vi :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.VI>,
      VOP3e_vi <!add(0x140, op), !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
  }
}

multiclass VOP1_Real_vi <bits<10> op> {
  defm NAME : VOP1_Real_e32e64_vi <op>;

  if !cast<VOP1_Pseudo>(NAME#"_e32").Pfl.HasExtSDWA then
  def _sdwa_vi :
    VOP_SDWA_Real <!cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa")>,
    VOP1_SDWAe <op{7-0}, !cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;

  if !cast<VOP1_Pseudo>(NAME#"_e32").Pfl.HasExtSDWA9 then
  def _sdwa_gfx9 :
    VOP_SDWA9_Real <!cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa")>,
    VOP1_SDWA9Ae <op{7-0}, !cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;

  if !cast<VOP1_Pseudo>(NAME#"_e32").Pfl.HasExtDPP then
    def _dpp_vi :
      VOP_DPP_Real<!cast<VOP1_DPP_Pseudo>(NAME#"_dpp"), SIEncodingFamily.VI>,
      VOP1_DPPe<op{7-0}, !cast<VOP1_DPP_Pseudo>(NAME#"_dpp")>;
}

defm V_NOP               : VOP1_Real_vi <0x0>;
defm V_MOV_B32           : VOP1_Real_vi <0x1>;
defm V_READFIRSTLANE_B32 : VOP1Only_Real_vi <0x2>;
defm V_CVT_I32_F64       : VOP1_Real_vi <0x3>;
defm V_CVT_F64_I32       : VOP1_Real_vi <0x4>;
defm V_CVT_F32_I32       : VOP1_Real_vi <0x5>;
defm V_CVT_F32_U32       : VOP1_Real_vi <0x6>;
defm V_CVT_U32_F32       : VOP1_Real_vi <0x7>;
defm V_CVT_I32_F32       : VOP1_Real_vi <0x8>;
defm V_CVT_F16_F32       : VOP1_Real_vi <0xa>;
defm V_CVT_F32_F16       : VOP1_Real_vi <0xb>;
defm V_CVT_RPI_I32_F32   : VOP1_Real_vi <0xc>;
defm V_CVT_FLR_I32_F32   : VOP1_Real_vi <0xd>;
defm V_CVT_OFF_F32_I4    : VOP1_Real_vi <0xe>;
defm V_CVT_F32_F64       : VOP1_Real_vi <0xf>;
defm V_CVT_F64_F32       : VOP1_Real_vi <0x10>;
defm V_CVT_F32_UBYTE0    : VOP1_Real_vi <0x11>;
defm V_CVT_F32_UBYTE1    : VOP1_Real_vi <0x12>;
defm V_CVT_F32_UBYTE2    : VOP1_Real_vi <0x13>;
defm V_CVT_F32_UBYTE3    : VOP1_Real_vi <0x14>;
defm V_CVT_U32_F64       : VOP1_Real_vi <0x15>;
defm V_CVT_F64_U32       : VOP1_Real_vi <0x16>;
defm V_FRACT_F32         : VOP1_Real_vi <0x1b>;
defm V_TRUNC_F32         : VOP1_Real_vi <0x1c>;
defm V_CEIL_F32          : VOP1_Real_vi <0x1d>;
defm V_RNDNE_F32         : VOP1_Real_vi <0x1e>;
defm V_FLOOR_F32         : VOP1_Real_vi <0x1f>;
defm V_EXP_F32           : VOP1_Real_vi <0x20>;
defm V_LOG_F32           : VOP1_Real_vi <0x21>;
defm V_RCP_F32           : VOP1_Real_vi <0x22>;
defm V_RCP_IFLAG_F32     : VOP1_Real_vi <0x23>;
defm V_RSQ_F32           : VOP1_Real_vi <0x24>;
defm V_RCP_F64           : VOP1_Real_vi <0x25>;
defm V_RSQ_F64           : VOP1_Real_vi <0x26>;
defm V_SQRT_F32          : VOP1_Real_vi <0x27>;
defm V_SQRT_F64          : VOP1_Real_vi <0x28>;
defm V_SIN_F32           : VOP1_Real_vi <0x29>;
defm V_COS_F32           : VOP1_Real_vi <0x2a>;
defm V_NOT_B32           : VOP1_Real_vi <0x2b>;
defm V_BFREV_B32         : VOP1_Real_vi <0x2c>;
defm V_FFBH_U32          : VOP1_Real_vi <0x2d>;
defm V_FFBL_B32          : VOP1_Real_vi <0x2e>;
defm V_FFBH_I32          : VOP1_Real_vi <0x2f>;
defm V_FREXP_EXP_I32_F64 : VOP1_Real_vi <0x30>;
defm V_FREXP_MANT_F64    : VOP1_Real_vi <0x31>;
defm V_FRACT_F64         : VOP1_Real_vi <0x32>;
defm V_FREXP_EXP_I32_F32 : VOP1_Real_vi <0x33>;
defm V_FREXP_MANT_F32    : VOP1_Real_vi <0x34>;
defm V_CLREXCP           : VOP1_Real_vi <0x35>;
defm V_MOVRELD_B32       : VOP1_Real_e32e64_vi <0x36>;
defm V_MOVRELS_B32       : VOP1_Real_e32e64_vi <0x37>;
defm V_MOVRELSD_B32      : VOP1_Real_e32e64_vi <0x38>;
defm V_TRUNC_F64         : VOP1_Real_vi <0x17>;
defm V_CEIL_F64          : VOP1_Real_vi <0x18>;
defm V_FLOOR_F64         : VOP1_Real_vi <0x1A>;
defm V_RNDNE_F64         : VOP1_Real_vi <0x19>;
defm V_LOG_LEGACY_F32    : VOP1_Real_vi <0x4c>;
defm V_EXP_LEGACY_F32    : VOP1_Real_vi <0x4b>;
defm V_CVT_F16_U16       : VOP1_Real_vi <0x39>;
defm V_CVT_F16_I16       : VOP1_Real_vi <0x3a>;
defm V_CVT_U16_F16       : VOP1_Real_vi <0x3b>;
defm V_CVT_I16_F16       : VOP1_Real_vi <0x3c>;
defm V_RCP_F16           : VOP1_Real_vi <0x3d>;
defm V_SQRT_F16          : VOP1_Real_vi <0x3e>;
defm V_RSQ_F16           : VOP1_Real_vi <0x3f>;
defm V_LOG_F16           : VOP1_Real_vi <0x40>;
defm V_EXP_F16           : VOP1_Real_vi <0x41>;
defm V_FREXP_MANT_F16    : VOP1_Real_vi <0x42>;
defm V_FREXP_EXP_I16_F16 : VOP1_Real_vi <0x43>;
defm V_FLOOR_F16         : VOP1_Real_vi <0x44>;
defm V_CEIL_F16          : VOP1_Real_vi <0x45>;
defm V_TRUNC_F16         : VOP1_Real_vi <0x46>;
defm V_RNDNE_F16         : VOP1_Real_vi <0x47>;
defm V_FRACT_F16         : VOP1_Real_vi <0x48>;
defm V_SIN_F16           : VOP1_Real_vi <0x49>;
defm V_COS_F16           : VOP1_Real_vi <0x4a>;
defm V_SWAP_B32          : VOP1Only_Real_vi <0x51>;

defm V_SAT_PK_U8_I16     : VOP1_Real_vi<0x4f>;
defm V_CVT_NORM_I16_F16  : VOP1_Real_vi<0x4d>;
defm V_CVT_NORM_U16_F16  : VOP1_Real_vi<0x4e>;

defm V_ACCVGPR_MOV_B32   : VOP1Only_Real_vi<0x52>;

let VOP1 = 1, SubtargetPredicate = isGFX8GFX9, Uses = [EXEC, M0], Size = V_MOV_B32_e32.Size in {

// Copy of v_mov_b32 with $vdst as a use operand for use with VGPR
// indexing mode. vdst can't be treated as a def for codegen purposes,
// and an implicit use and def of the super register should be added.
def V_MOV_B32_indirect_write : VPseudoInstSI<(outs),
  (ins getVALUDstForVT<i32>.ret:$vdst, getVOPSrc0ForVT<i32, 0>.ret:$src0)>,
  PseudoInstExpansion<(V_MOV_B32_e32_vi getVALUDstForVT<i32>.ret:$vdst,
                                        getVOPSrc0ForVT<i32, 0>.ret:$src0)>;

// Copy of v_mov_b32 for use with VGPR indexing mode. An implicit use of the
// super register should be added.
def V_MOV_B32_indirect_read : VPseudoInstSI<
  (outs getVALUDstForVT<i32>.ret:$vdst),
  (ins getVOPSrc0ForVT<i32, 0>.ret:$src0)>,
  PseudoInstExpansion<(V_MOV_B32_e32_vi getVALUDstForVT<i32>.ret:$vdst,
                                        getVOPSrc0ForVT<i32, 0>.ret:$src0)>;

} // End VOP1 = 1, SubtargetPredicate = isGFX8GFX9, Uses = [M0]

let OtherPredicates = [isGFX8Plus] in {

def : GCNPat <
  (i32 (int_amdgcn_mov_dpp i32:$src, timm:$dpp_ctrl, timm:$row_mask,
                           timm:$bank_mask, timm:$bound_ctrl)),
  (V_MOV_B32_dpp VGPR_32:$src, VGPR_32:$src, (as_i32timm $dpp_ctrl),
                       (as_i32timm $row_mask), (as_i32timm $bank_mask),
                       (as_i1timm $bound_ctrl))
>;

foreach vt = Reg32Types.types in {
def : GCNPat <
  (vt (int_amdgcn_update_dpp vt:$old, vt:$src, timm:$dpp_ctrl,
                              timm:$row_mask, timm:$bank_mask,
                              timm:$bound_ctrl)),
  (V_MOV_B32_dpp VGPR_32:$old, VGPR_32:$src, (as_i32timm $dpp_ctrl),
                 (as_i32timm $row_mask), (as_i32timm $bank_mask),
                 (as_i1timm $bound_ctrl))
>;
}

} // End OtherPredicates = [isGFX8Plus]

let OtherPredicates = [isGFX8Plus] in {
def : GCNPat<
  (i32 (anyext i16:$src)),
  (COPY $src)
>;

def : GCNPat<
   (i64 (anyext i16:$src)),
   (REG_SEQUENCE VReg_64,
     (i32 (COPY $src)), sub0,
     (V_MOV_B32_e32 (i32 0)), sub1)
>;

def : GCNPat<
  (i16 (trunc i32:$src)),
  (COPY $src)
>;

def : GCNPat <
  (i16 (trunc i64:$src)),
  (EXTRACT_SUBREG $src, sub0)
>;

} // End OtherPredicates = [isGFX8Plus]

//===----------------------------------------------------------------------===//
// GFX9
//===----------------------------------------------------------------------===//

let AssemblerPredicate = isGFX9Only, DecoderNamespace = "GFX9" in {
  multiclass VOP1_Real_gfx9 <bits<10> op> {
    defm NAME : VOP1_Real_e32e64_vi <op>;

    if !cast<VOP1_Pseudo>(NAME#"_e32").Pfl.HasExtSDWA9 then
    def _sdwa_gfx9 :
      VOP_SDWA9_Real <!cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa")>,
      VOP1_SDWA9Ae <op{7-0}, !cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;

    if !cast<VOP1_Pseudo>(NAME#"_e32").Pfl.HasExtDPP then
      def _dpp_gfx9 :
        VOP_DPP_Real<!cast<VOP1_DPP_Pseudo>(NAME#"_dpp"), SIEncodingFamily.GFX9>,
        VOP1_DPPe<op{7-0}, !cast<VOP1_DPP_Pseudo>(NAME#"_dpp")>;
  }

  multiclass VOP1_Real_NoDstSel_SDWA_gfx9 <bits<10> op> {
    defm NAME : VOP1_Real_e32e64_vi <op>;

    if !cast<VOP1_Pseudo>(NAME#"_e32").Pfl.HasExtSDWA9 then
    def _sdwa_gfx9 :
      VOP_SDWA9_Real <!cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa")>,
      VOP1_SDWA9Ae <op{7-0}, !cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa").Pfl> {
        let Inst{42-40} = 6;
      }

    if !cast<VOP1_Pseudo>(NAME#"_e32").Pfl.HasExtDPP then
      def _dpp_gfx9 :
        VOP_DPP_Real<!cast<VOP1_DPP_Pseudo>(NAME#"_dpp"), SIEncodingFamily.GFX9>,
        VOP1_DPPe<op{7-0}, !cast<VOP1_DPP_Pseudo>(NAME#"_dpp")>;
  }
}

defm V_SCREEN_PARTITION_4SE_B32 : VOP1_Real_gfx9 <0x37>;

let AssemblerPredicate = isGFX940Plus in
defm V_MOV_B64 : VOP1_Real_gfx9 <0x38>;

defm V_CVT_F32_FP8       : VOP1_Real_NoDstSel_SDWA_gfx9<0x54>;
defm V_CVT_F32_BF8       : VOP1_Real_NoDstSel_SDWA_gfx9<0x55>;
defm V_CVT_PK_F32_FP8    : VOP1_Real_NoDstSel_SDWA_gfx9<0x56>;
defm V_CVT_PK_F32_BF8    : VOP1_Real_NoDstSel_SDWA_gfx9<0x57>;

class MovDPP8Pattern<Predicate Pred, Instruction Inst> : GCNPat <
  (i32 (int_amdgcn_mov_dpp8 i32:$src, timm:$dpp8)),
  (Inst VGPR_32:$src, VGPR_32:$src, (as_i32timm $dpp8), (i32 DPP8Mode.FI_0))> {
  let OtherPredicates = [Pred];
}

def : MovDPP8Pattern<isGFX10Only, V_MOV_B32_dpp8_gfx10>;
def : MovDPP8Pattern<isGFX11Only, V_MOV_B32_dpp8_gfx11>;
def : MovDPP8Pattern<isGFX12Only, V_MOV_B32_dpp8_gfx12>;
