//===-- VINTERPInstructions.td - VINTERP Instruction Definitions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// VINTERP encoding
//===----------------------------------------------------------------------===//

class VINTERPe <VOPProfile P> : Enc64 {
  bits<8> vdst;
  bits<4> src0_modifiers;
  bits<9> src0;
  bits<3> src1_modifiers;
  bits<9> src1;
  bits<3> src2_modifiers;
  bits<9> src2;
  bits<1> clamp;
  bits<3> waitexp;

  let Inst{31-26} = 0x33; // VOP3P encoding
  let Inst{25-24} = 0x1; // VINTERP sub-encoding

  let Inst{7-0}   = vdst;
  let Inst{10-8}  = waitexp;
  let Inst{11}    = !if(P.HasOpSel, src0_modifiers{2}, 0); // op_sel(0)
  let Inst{12}    = !if(P.HasOpSel, src1_modifiers{2}, 0); // op_sel(1)
  let Inst{13}    = !if(P.HasOpSel, src2_modifiers{2}, 0); // op_sel(2)
  let Inst{14}    = !if(P.HasOpSel, src0_modifiers{3}, 0); // op_sel(3)
  let Inst{15}    = clamp;
  let Inst{40-32} = src0;
  let Inst{49-41} = src1;
  let Inst{58-50} = src2;
  let Inst{61}    = src0_modifiers{0}; // neg(0)
  let Inst{62}    = src1_modifiers{0}; // neg(1)
  let Inst{63}    = src2_modifiers{0}; // neg(2)
}

class VINTERPe_gfx11 <bits<7> op, VOPProfile P> : VINTERPe<P> {
  let Inst{22-16} = op;
}

class VINTERPe_gfx12 <bits<7> op, VOPProfile P> : VINTERPe<P> {
  let Inst{20-16} = op{4-0};
}

//===----------------------------------------------------------------------===//
// VOP3 VINTERP
//===----------------------------------------------------------------------===//

class VINTERP_Pseudo <string OpName, VOPProfile P, list<dag> pattern = []> :
  VOP3_Pseudo<OpName, P, pattern, 0, 0> {
  let AsmMatchConverter = "cvtVINTERP";
  let mayRaiseFPException = 0;

  let VOP3_OPSEL = 1;
  let VINTERP = 1;
}

class VINTERP_Real <VOP_Pseudo ps, int EncodingFamily> :
  VOP3_Real <ps, EncodingFamily> {
  let VINTERP = 1;
}

def VOP3_VINTERP_F32 : VOPProfile<[f32, f32, f32, f32]> {
  let HasOpSel = 0;
  let HasModifiers = 1;

  let Src0Mod = FPVRegInputMods;
  let Src1Mod = FPVRegInputMods;
  let Src2Mod = FPVRegInputMods;

  let Outs64 = (outs VGPR_32:$vdst);
  let Ins64 = (ins Src0Mod:$src0_modifiers, VRegSrc_32:$src0,
                   Src1Mod:$src1_modifiers, VRegSrc_32:$src1,
                   Src2Mod:$src2_modifiers, VRegSrc_32:$src2,
                   Clamp:$clamp,
                   WaitEXP:$waitexp);

  let Asm64 = " $vdst, $src0_modifiers, $src1_modifiers, $src2_modifiers$clamp$waitexp";
}

class VOP3_VINTERP_F16 <list<ValueType> ArgVT> : VOPProfile<ArgVT> {
  let HasOpSel = 1;
  let HasModifiers = 1;

  let Src0Mod = FPVRegInputMods;
  let Src1Mod = FPVRegInputMods;
  let Src2Mod = FPVRegInputMods;

  let Outs64 = (outs VGPR_32:$vdst);
  let Ins64 = (ins Src0Mod:$src0_modifiers, VRegSrc_32:$src0,
                   Src1Mod:$src1_modifiers, VRegSrc_32:$src1,
                   Src2Mod:$src2_modifiers, VRegSrc_32:$src2,
                   Clamp:$clamp, op_sel0:$op_sel,
                   WaitEXP:$waitexp);

  let Asm64 = " $vdst, $src0_modifiers, $src1_modifiers, $src2_modifiers$clamp$op_sel$waitexp";
}

//===----------------------------------------------------------------------===//
// VINTERP Pseudo Instructions
//===----------------------------------------------------------------------===//

let SubtargetPredicate = HasVINTERPEncoding in {

let Uses = [M0, EXEC, MODE] in {
def V_INTERP_P10_F32_inreg : VINTERP_Pseudo <"v_interp_p10_f32", VOP3_VINTERP_F32>;
def V_INTERP_P2_F32_inreg : VINTERP_Pseudo <"v_interp_p2_f32", VOP3_VINTERP_F32>;
def V_INTERP_P10_F16_F32_inreg :
  VINTERP_Pseudo <"v_interp_p10_f16_f32", VOP3_VINTERP_F16<[f32, f32, f32, f32]>>;
def V_INTERP_P2_F16_F32_inreg :
  VINTERP_Pseudo <"v_interp_p2_f16_f32", VOP3_VINTERP_F16<[f16, f32, f32, f32]>>;
} // Uses = [M0, EXEC, MODE]

let Uses = [M0, EXEC] in {
def V_INTERP_P10_RTZ_F16_F32_inreg :
  VINTERP_Pseudo <"v_interp_p10_rtz_f16_f32", VOP3_VINTERP_F16<[f32, f32, f32, f32]>>;
def V_INTERP_P2_RTZ_F16_F32_inreg :
  VINTERP_Pseudo <"v_interp_p2_rtz_f16_f32", VOP3_VINTERP_F16<[f16, f32, f32, f32]>>;
} // Uses = [M0, EXEC]

} // SubtargetPredicate = HasVINTERPEncoding.

class VInterpF32Pat <SDPatternOperator op, Instruction inst> : GCNPat <
   (f32 (op
      (VINTERPMods f32:$src0, i32:$src0_modifiers),
      (VINTERPMods f32:$src1, i32:$src1_modifiers),
      (VINTERPMods f32:$src2, i32:$src2_modifiers))),
    (inst $src0_modifiers, $src0,
          $src1_modifiers, $src1,
          $src2_modifiers, $src2,
          0, /* clamp */
          7) /* wait_exp */
>;

def VINTERP_OPSEL {
  int LOW = 0;
  int HIGH = 0xa;
}

class VInterpF16Pat <SDPatternOperator op, Instruction inst,
                     ValueType dst_type, bit high,
                     list<ComplexPattern> pat> : GCNPat <
   (dst_type (op
      (pat[0] f32:$src0, i32:$src0_modifiers),
      (pat[1] f32:$src1, i32:$src1_modifiers),
      (pat[2] f32:$src2, i32:$src2_modifiers),
      !if(high, (i1 -1), (i1 0)))),
    (inst $src0_modifiers, $src0,
          $src1_modifiers, $src1,
          $src2_modifiers, $src2,
          0, /* clamp */
          /* op_sel = 0 */
          7) /* wait_exp */
>;

multiclass VInterpF16Pat <SDPatternOperator op, Instruction inst,
                          ValueType dst_type, list<ComplexPattern> high_pat> {
  def : VInterpF16Pat<op, inst, dst_type, 0,
                      [VINTERPMods, VINTERPMods, VINTERPMods]>;
  def : VInterpF16Pat<op, inst, dst_type, 1, high_pat>;
}

def : VInterpF32Pat<int_amdgcn_interp_inreg_p10, V_INTERP_P10_F32_inreg>;
def : VInterpF32Pat<int_amdgcn_interp_inreg_p2, V_INTERP_P2_F32_inreg>;
defm : VInterpF16Pat<int_amdgcn_interp_inreg_p10_f16,
                     V_INTERP_P10_F16_F32_inreg, f32,
                     [VINTERPModsHi, VINTERPMods, VINTERPModsHi]>;
defm : VInterpF16Pat<int_amdgcn_interp_inreg_p2_f16,
                     V_INTERP_P2_F16_F32_inreg, f16,
                     [VINTERPModsHi, VINTERPMods, VINTERPMods]>;
defm : VInterpF16Pat<int_amdgcn_interp_p10_rtz_f16,
                     V_INTERP_P10_RTZ_F16_F32_inreg, f32,
                     [VINTERPModsHi, VINTERPMods, VINTERPModsHi]>;
defm : VInterpF16Pat<int_amdgcn_interp_p2_rtz_f16,
                     V_INTERP_P2_RTZ_F16_F32_inreg, f16,
                     [VINTERPModsHi, VINTERPMods, VINTERPMods]>;

//===----------------------------------------------------------------------===//
// VINTERP Real Instructions
//===----------------------------------------------------------------------===//

multiclass VINTERP_Real_gfx11 <bits<7> op> {
  let AssemblerPredicate = isGFX11Only, DecoderNamespace = "GFX11" in {
    def _gfx11 :
      VINTERP_Real<!cast<VOP3_Pseudo>(NAME), SIEncodingFamily.GFX11>,
      VINTERPe_gfx11<op, !cast<VOP3_Pseudo>(NAME).Pfl>;
  }
}

multiclass VINTERP_Real_gfx12 <bits<7> op> {
  let AssemblerPredicate = isGFX12Only, DecoderNamespace = "GFX12" in {
    def _gfx12 :
      VINTERP_Real<!cast<VOP3_Pseudo>(NAME), SIEncodingFamily.GFX12>,
      VINTERPe_gfx12<op, !cast<VOP3_Pseudo>(NAME).Pfl>;
  }
}

multiclass VINTERP_Real_gfx11_gfx12 <bits<7> op> :
  VINTERP_Real_gfx11<op>, VINTERP_Real_gfx12<op>;

defm V_INTERP_P10_F32_inreg : VINTERP_Real_gfx11_gfx12<0x000>;
defm V_INTERP_P2_F32_inreg : VINTERP_Real_gfx11_gfx12<0x001>;
defm V_INTERP_P10_F16_F32_inreg : VINTERP_Real_gfx11_gfx12<0x002>;
defm V_INTERP_P2_F16_F32_inreg : VINTERP_Real_gfx11_gfx12<0x003>;
defm V_INTERP_P10_RTZ_F16_F32_inreg : VINTERP_Real_gfx11_gfx12<0x004>;
defm V_INTERP_P2_RTZ_F16_F32_inreg : VINTERP_Real_gfx11_gfx12<0x005>;
