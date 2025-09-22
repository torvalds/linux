//===-- VOP2Instructions.td - Vector Instruction Definitions --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// VOP2 Classes
//===----------------------------------------------------------------------===//

class VOP2e <bits<6> op, VOPProfile P> : Enc32 {
  bits<8> vdst;
  bits<9> src0;
  bits<8> src1;

  let Inst{8-0}   = !if(P.HasSrc0, src0, 0);
  let Inst{16-9}  = !if(P.HasSrc1, src1, 0);
  let Inst{24-17} = !if(P.EmitDst, vdst, 0);
  let Inst{30-25} = op;
  let Inst{31}    = 0x0; //encoding
}

class VOP2_MADKe <bits<6> op, VOPProfile P> : Enc64 {
  bits<8>  vdst;
  bits<9>  src0;
  bits<8>  src1;
  bits<32> imm;

  let Inst{8-0}   = !if(P.HasSrc0, src0, 0);
  let Inst{16-9}  = !if(P.HasSrc1, src1, 0);
  let Inst{24-17} = !if(P.EmitDst, vdst, 0);
  let Inst{30-25} = op;
  let Inst{31}    = 0x0; // encoding
  let Inst{63-32} = imm;
}

class VOP2_SDWAe <bits<6> op, VOPProfile P> : VOP_SDWAe <P> {
  bits<8> vdst;
  bits<8> src1;

  let Inst{8-0}   = 0xf9; // sdwa
  let Inst{16-9}  = !if(P.HasSrc1, src1{7-0}, 0);
  let Inst{24-17} = !if(P.EmitDst, vdst{7-0}, 0);
  let Inst{30-25} = op;
  let Inst{31}    = 0x0; // encoding
}

class VOP2_SDWA9Ae <bits<6> op, VOPProfile P> : VOP_SDWA9Ae <P> {
  bits<8> vdst;
  bits<9> src1;

  let Inst{8-0}   = 0xf9; // sdwa
  let Inst{16-9}  = !if(P.HasSrc1, src1{7-0}, 0);
  let Inst{24-17} = !if(P.EmitDst, vdst{7-0}, 0);
  let Inst{30-25} = op;
  let Inst{31}    = 0x0; // encoding
  let Inst{63}    = !if(P.HasSrc1, src1{8}, 0); // src1_sgpr
}

class VOP2_Pseudo <string opName, VOPProfile P, list<dag> pattern=[], string suffix = "_e32"> :
  VOP_Pseudo <opName, suffix, P, P.Outs32, P.Ins32, "", pattern> {

  let AsmOperands = P.Asm32;

  let Size = 4;
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;

  let ReadsModeReg = !or(P.DstVT.isFP, P.Src0VT.isFP);

  let mayRaiseFPException = ReadsModeReg;

  let VOP2 = 1;
  let VALU = 1;
  let Uses = !if(ReadsModeReg, [MODE, EXEC], [EXEC]);

  let AsmVariantName = AMDGPUAsmVariants.Default;
}

class VOP2_Real <VOP2_Pseudo ps, int EncodingFamily, string real_name = ps.Mnemonic> :
  VOP_Real <ps>,
  InstSI <ps.OutOperandList, ps.InOperandList, real_name # ps.AsmOperands, []>,
  SIMCInstr <ps.PseudoInstr, EncodingFamily> {

  let VALU = 1;
  let VOP2 = 1;
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
}

class VOP2_Real_Gen <VOP2_Pseudo ps, GFXGen Gen, string real_name = ps.Mnemonic> :
  VOP2_Real <ps, Gen.Subtarget, real_name> {
  let AssemblerPredicate = Gen.AssemblerPredicate;
  let True16Predicate = !if(ps.Pfl.IsRealTrue16, UseRealTrue16Insts, NoTrue16Predicate);
  let DecoderNamespace = Gen.DecoderNamespace#
                         !if(ps.Pfl.IsRealTrue16, "", "_FAKE16");
}

class VOP2_SDWA_Pseudo <string OpName, VOPProfile P, list<dag> pattern=[]> :
  VOP_SDWA_Pseudo <OpName, P, pattern> {
  let AsmMatchConverter = "cvtSdwaVOP2";
}

class VOP2_DPP_Pseudo <string OpName, VOPProfile P, list<dag> pattern=[]> :
  VOP_DPP_Pseudo <OpName, P, pattern> {
}


class getVOP2Pat64 <SDPatternOperator node, VOPProfile P> : LetDummies {
  list<dag> ret = !if(P.HasModifiers,
    [(set P.DstVT:$vdst,
      (node (P.Src0VT
              !if(P.HasOMod,
                  (VOP3Mods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp, i32:$omod),
                  (VOP3Mods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp))),
            (P.Src1VT (VOP3Mods P.Src1VT:$src1, i32:$src1_modifiers))))],
    [(set P.DstVT:$vdst, (node P.Src0VT:$src0, P.Src1VT:$src1))]);
}

multiclass VOP2Inst_e32<string opName,
                        VOPProfile P,
                        SDPatternOperator node = null_frag,
                        string revOp = opName,
                        bit GFX9Renamed = 0> {
  let renamedInGFX9 = GFX9Renamed in {
    def _e32 : VOP2_Pseudo <opName, P, VOPPatOrNull<node,P>.ret>,
               Commutable_REV<revOp#"_e32", !eq(revOp, opName)>;
  } // End renamedInGFX9 = GFX9Renamed
}
multiclass
    VOP2Inst_e32_VOPD<string opName, VOPProfile P, bits<5> VOPDOp,
                      string VOPDName, SDPatternOperator node = null_frag,
                      string revOp = opName, bit GFX9Renamed = 0> {
  defm NAME : VOP2Inst_e32<opName, P, node, revOp, GFX9Renamed>,
              VOPD_Component<VOPDOp, VOPDName>;
}
multiclass VOP2Inst_e64<string opName,
                        VOPProfile P,
                        SDPatternOperator node = null_frag,
                        string revOp = opName,
                        bit GFX9Renamed = 0> {
  let renamedInGFX9 = GFX9Renamed in {
    def _e64 : VOP3InstBase <opName, P, node, 1>,
               Commutable_REV<revOp#"_e64", !eq(revOp, opName)>;

    let SubtargetPredicate = isGFX11Plus in {
      if P.HasExtVOP3DPP then
        def _e64_dpp  : VOP3_DPP_Pseudo <opName, P>;
    } // End SubtargetPredicate = isGFX11Plus
  } // End renamedInGFX9 = GFX9Renamed
}

multiclass VOP2Inst_sdwa<string opName,
                         VOPProfile P,
                         bit GFX9Renamed = 0> {
  let renamedInGFX9 = GFX9Renamed in {
    if P.HasExtSDWA then
      def _sdwa : VOP2_SDWA_Pseudo <opName, P>;
  } // End renamedInGFX9 = GFX9Renamed
}

multiclass VOP2Inst<string opName,
                    VOPProfile P,
                    SDPatternOperator node = null_frag,
                    string revOp = opName,
                    bit GFX9Renamed = 0> :
    VOP2Inst_e32<opName, P, node, revOp, GFX9Renamed>,
    VOP2Inst_e64<opName, P, node, revOp, GFX9Renamed>,
    VOP2Inst_sdwa<opName, P, GFX9Renamed> {
  let renamedInGFX9 = GFX9Renamed in {
    if P.HasExtDPP then
      def _dpp  : VOP2_DPP_Pseudo <opName, P>;
  }
}

multiclass VOP2Inst_t16<string opName,
                        VOPProfile P,
                        SDPatternOperator node = null_frag,
                        string revOp = opName,
                        bit GFX9Renamed = 0> {
  let OtherPredicates = [Has16BitInsts], True16Predicate = NotHasTrue16BitInsts in {
    defm NAME : VOP2Inst<opName, P, node, revOp, GFX9Renamed>;
  }
  let SubtargetPredicate = UseRealTrue16Insts in {
    defm _t16 : VOP2Inst<opName#"_t16", VOPProfile_True16<P>, node, revOp#"_t16", GFX9Renamed>;
  }
  let SubtargetPredicate = UseFakeTrue16Insts in {
    defm _fake16 : VOP2Inst<opName#"_fake16", VOPProfile_Fake16<P>, node, revOp#"_fake16", GFX9Renamed>;
  }
}

// Creating a _t16_e32 pseudo when there is no corresponding real instruction on
// any subtarget is a problem. It makes getMCOpcodeGen return -1, which we
// assume means the instruction is already a real. The fix is to not create that
// _t16_e32 pseudo
multiclass VOP2Inst_e64_t16<string opName,
                        VOPProfile P,
                        SDPatternOperator node = null_frag,
                        string revOp = opName,
                        bit GFX9Renamed = 0> {
  let OtherPredicates = [Has16BitInsts], True16Predicate = NotHasTrue16BitInsts in {
    defm NAME : VOP2Inst<opName, P, node, revOp, GFX9Renamed>;
  }
  let SubtargetPredicate = HasTrue16BitInsts in {
    defm _t16 : VOP2Inst_e64<opName#"_t16", VOPProfile_Fake16<P>, node, revOp#"_t16", GFX9Renamed>;
  }
}

multiclass VOP2Inst_VOPD<string opName,
                         VOPProfile P,
                         bits<5> VOPDOp,
                         string VOPDName,
                         SDPatternOperator node = null_frag,
                         string revOp = opName,
                         bit GFX9Renamed = 0> :
    VOP2Inst_e32_VOPD<opName, P, VOPDOp, VOPDName, node, revOp, GFX9Renamed>,
    VOP2Inst_e64<opName, P, node, revOp, GFX9Renamed>,
    VOP2Inst_sdwa<opName, P, GFX9Renamed> {
  let renamedInGFX9 = GFX9Renamed in {
    if P.HasExtDPP then
      def _dpp  : VOP2_DPP_Pseudo <opName, P>;
  }
}

multiclass VOP2bInst <string opName,
                      VOPProfile P,
                      SDPatternOperator node = null_frag,
                      string revOp = opName,
                      bit GFX9Renamed = 0,
                      bit useSGPRInput = !eq(P.NumSrcArgs, 3)> {
  let renamedInGFX9 = GFX9Renamed in {
    let SchedRW = [Write32Bit, WriteSALU] in {
      let Uses = !if(useSGPRInput, [VCC, EXEC], [EXEC]), Defs = [VCC] in {
        def _e32 : VOP2_Pseudo <opName, P, VOPPatOrNull<node,P>.ret>,
                   Commutable_REV<revOp#"_e32", !eq(revOp, opName)> {
          let usesCustomInserter = true;
        }

        if P.HasExtSDWA then
          def _sdwa  : VOP2_SDWA_Pseudo <opName, P> {
            let AsmMatchConverter = "cvtSdwaVOP2b";
          }
        if P.HasExtDPP then
          def _dpp  : VOP2_DPP_Pseudo <opName, P>;
      } // End Uses = !if(useSGPRInput, [VCC, EXEC], [EXEC]), Defs = [VCC]

      def _e64 : VOP3InstBase <opName, P, node, 1>,
                 Commutable_REV<revOp#"_e64", !eq(revOp, opName)>;

      let SubtargetPredicate = isGFX11Plus in {
        if P.HasExtVOP3DPP then
          def _e64_dpp  : VOP3_DPP_Pseudo <opName, P>;
      } // End SubtargetPredicate = isGFX11Plus
    }
  }
}

class VOP2bInstAlias <VOP2_Pseudo ps, Instruction inst,
                      string OpName, string opnd> :
  InstAlias <OpName#" "#!subst("vcc", opnd, ps.Pfl.Asm32),
             (inst ps.Pfl.DstRC:$vdst, ps.Pfl.Src0RC32:$src0,
                   ps.Pfl.Src1RC32:$src1),
             1, inst.AsmVariantName>,
  PredicateControl {
}

multiclass VOP2bInstAliases<VOP2_Pseudo ps, VOP2_Real inst, string OpName> {
  let WaveSizePredicate = isWave32 in {
    def : VOP2bInstAlias<ps, inst, OpName, "vcc_lo">;
  }
  let WaveSizePredicate = isWave64 in {
    def : VOP2bInstAlias<ps, inst, OpName, "vcc">;
  }
}

multiclass
    VOP2eInst_Base<string opName, VOPProfile P, bits<5> VOPDOp, string VOPDName,
                   SDPatternOperator node, string revOp, bit useSGPRInput> {

  let SchedRW = [Write32Bit] in {
    let Uses = !if(useSGPRInput, [VCC, EXEC], [EXEC]) in {
      if !eq(VOPDOp, -1) then
        def _e32 : VOP2_Pseudo <opName, P>,
                   Commutable_REV<revOp#"_e32", !eq(revOp, opName)>;
      else
        def _e32 : VOP2_Pseudo <opName, P>,
                   Commutable_REV<revOp#"_e32", !eq(revOp, opName)>,
                   VOPD_Component<VOPDOp, VOPDName>;

      if P.HasExtSDWA then
        def _sdwa : VOP2_SDWA_Pseudo <opName, P> {
          let AsmMatchConverter = "cvtSdwaVOP2e";
        }

      if P.HasExtDPP then
        def _dpp  : VOP2_DPP_Pseudo <opName, P>;
    }

    def _e64 : VOP3InstBase <opName, P, node, 1>,
               Commutable_REV<revOp#"_e64", !eq(revOp, opName)> {
      let isReMaterializable = 1;
    }

    let SubtargetPredicate = isGFX11Plus in {
      if P.HasExtVOP3DPP then
        def _e64_dpp  : VOP3_DPP_Pseudo <opName, P>;
    } // End SubtargetPredicate = isGFX11Plus
  }
}

multiclass
    VOP2eInst<string opName, VOPProfile P, SDPatternOperator node = null_frag,
              string revOp = opName, bit useSGPRInput = !eq(P.NumSrcArgs, 3)>
    : VOP2eInst_Base<opName, P, -1, "", node, revOp, useSGPRInput>;

multiclass
    VOP2eInst_VOPD<string opName, VOPProfile P, bits<5> VOPDOp, string VOPDName,
                   SDPatternOperator node = null_frag, string revOp = opName,
                   bit useSGPRInput = !eq(P.NumSrcArgs, 3)>
    : VOP2eInst_Base<opName, P, VOPDOp, VOPDName, node, revOp, useSGPRInput>;

class VOP2eInstAlias <VOP2_Pseudo ps, Instruction inst, string opnd = ""> :
  InstAlias <ps.OpName#" "#ps.Pfl.Asm32#", "#opnd,
             (inst ps.Pfl.DstRC:$vdst, ps.Pfl.Src0RC32:$src0,
                   ps.Pfl.Src1RC32:$src1),
             1, inst.AsmVariantName>,
  PredicateControl;

class VOP2e64InstAlias <VOP3_Pseudo ps, Instruction inst> :
  InstAlias <ps.OpName#" "#ps.Pfl.Asm64,
             (inst ps.Pfl.DstRC:$vdst, VOPDstS64orS32:$sdst,
                   ps.Pfl.Src0RC32:$src0, ps.Pfl.Src1RC32:$src1, Clamp:$clamp),
             1, inst.AsmVariantName>,
  PredicateControl;

multiclass VOP2eInstAliases<VOP2_Pseudo ps, VOP2_Real inst> {
  let WaveSizePredicate = isWave32 in {
    def : VOP2eInstAlias<ps, inst, "vcc_lo">;
  }
  let WaveSizePredicate = isWave64 in {
    def : VOP2eInstAlias<ps, inst, "vcc">;
  }
}

class VOP_MADK_Base<ValueType vt> : VOPProfile <[vt, vt, vt, vt]> {
  string AsmVOPDXDeferred = ?;
}

class VOP_MADAK <ValueType vt> : VOP_MADK_Base<vt> {
  field Operand ImmOpType = !if(!eq(vt.Size, 32), KImmFP32, KImmFP16);
  field dag Ins32 = !if(!eq(vt.Size, 32),
                        (ins VSrc_f32_Deferred:$src0, VGPR_32:$src1, ImmOpType:$imm),
                        (ins VSrc_f16_Deferred:$src0, VGPR_32:$src1, ImmOpType:$imm));
  field dag InsVOPDX = (ins VSrc_f32_Deferred:$src0X, VGPR_32:$vsrc1X, ImmOpType:$imm);
  // Note that both src0X and imm are deferred
  let InsVOPDXDeferred = (ins VSrc_f32_Deferred:$src0X, VGPR_32:$vsrc1X, ImmOpType:$immDeferred);
  field dag InsVOPDY = (ins VSrc_f32_Deferred:$src0Y, VGPR_32:$vsrc1Y, ImmOpType:$imm);

  field string Asm32 = "$vdst, $src0, $src1, $imm";
  field string AsmVOPDX = "$vdstX, $src0X, $vsrc1X, $imm";
  let AsmVOPDXDeferred = "$vdstX, $src0X, $vsrc1X, $immDeferred";
  field string AsmVOPDY = "$vdstY, $src0Y, $vsrc1Y, $imm";
  field bit HasExt = 0;
  let IsSingle = 1;
}

def VOP_MADAK_F16 : VOP_MADAK <f16>;
def VOP_MADAK_F16_t16 : VOP_MADAK <f16> {
  let IsTrue16 = 1;
  let DstRC = VOPDstOperand<VGPR_32_Lo128>;
  let Ins32 = (ins VSrcFake16_f16_Lo128_Deferred:$src0, VGPR_32_Lo128:$src1, ImmOpType:$imm);
}
def VOP_MADAK_F32 : VOP_MADAK <f32>;

class VOP_MADMK <ValueType vt> : VOP_MADK_Base<vt> {
  field Operand ImmOpType = !if(!eq(vt.Size, 32), KImmFP32, KImmFP16);
  field dag Ins32 = !if(!eq(vt.Size, 32),
                        (ins VSrc_f32_Deferred:$src0, ImmOpType:$imm, VGPR_32:$src1),
                        (ins VSrc_f16_Deferred:$src0, ImmOpType:$imm, VGPR_32:$src1));
  field dag InsVOPDX = (ins VSrc_f32_Deferred:$src0X, ImmOpType:$imm, VGPR_32:$vsrc1X);
  let InsVOPDXDeferred = (ins VSrc_f32_Deferred:$src0X, ImmOpType:$immDeferred, VGPR_32:$vsrc1X);
  field dag InsVOPDY = (ins VSrc_f32_Deferred:$src0Y, ImmOpType:$imm, VGPR_32:$vsrc1Y);

  field string Asm32 = "$vdst, $src0, $imm, $src1";
  field string AsmVOPDX = "$vdstX, $src0X, $imm, $vsrc1X";
  let AsmVOPDXDeferred = "$vdstX, $src0X, $immDeferred, $vsrc1X";
  field string AsmVOPDY = "$vdstY, $src0Y, $imm, $vsrc1Y";
  field bit HasExt = 0;
  let IsSingle = 1;
}

def VOP_MADMK_F16 : VOP_MADMK <f16>;
def VOP_MADMK_F16_t16 : VOP_MADMK <f16> {
  let IsTrue16 = 1;
  let DstRC = VOPDstOperand<VGPR_32_Lo128>;
  let Ins32 = (ins VSrcFake16_f16_Lo128_Deferred:$src0, ImmOpType:$imm, VGPR_32_Lo128:$src1);
}
def VOP_MADMK_F32 : VOP_MADMK <f32>;

// FIXME: Remove src2_modifiers. It isn't used, so is wasting memory
// and processing time but it makes it easier to convert to mad.
class VOP_MAC <ValueType vt0, ValueType vt1=vt0> : VOPProfile <[vt0, vt1, vt1, vt0]> {
  let Ins32 = (ins Src0RC32:$src0, Src1RC32:$src1, getVregSrcForVT<Src2VT>.ret:$src2);
  let Ins64 = getIns64<Src0RC64, Src1RC64, getVregSrcForVT<Src2VT>.ret, 3,
                       0, HasModifiers, HasModifiers, HasOMod,
                       Src0Mod, Src1Mod, Src2Mod>.ret;
  let InsDPP = (ins Src0ModDPP:$src0_modifiers, Src0DPP:$src0,
                    Src1ModDPP:$src1_modifiers, Src1DPP:$src1,
                    getVregSrcForVT<Src2VT>.ret:$src2, // stub argument
                    dpp_ctrl:$dpp_ctrl, DppRowMask:$row_mask,
                    DppBankMask:$bank_mask, DppBoundCtrl:$bound_ctrl);
  let InsDPP16 = !con(InsDPP, (ins Dpp16FI:$fi));
  let InsVOP3Base = getInsVOP3Base<Src0VOP3DPP, Src1VOP3DPP, RegisterOperand<VGPR_32>, 3,
                       0, HasModifiers, HasModifiers, HasOMod,
                       Src0ModVOP3DPP, Src1ModVOP3DPP, Src2Mod, HasOpSel>.ret;
  // We need a dummy src2 tied to dst to track the use of that register for s_delay_alu
  let InsVOPDX = (ins Src0RC32:$src0X, Src1RC32:$vsrc1X, VGPRSrc_32:$src2X);
  let InsVOPDXDeferred =
    (ins !if(!eq(Src0VT.Size, 32), VSrc_f32_Deferred, VSrc_f16_Deferred):$src0X,
         VGPR_32:$vsrc1X, VGPRSrc_32:$src2X);
  let InsVOPDY = (ins Src0RC32:$src0Y, Src1RC32:$vsrc1Y, VGPRSrc_32:$src2Y);
  let InsVOPDYDeferred =
    (ins !if(!eq(Src1VT.Size, 32), VSrc_f32_Deferred, VSrc_f16_Deferred):$src0Y,
         VGPR_32:$vsrc1Y, VGPRSrc_32:$src2Y);

  let InsDPP8 = (ins Src0ModDPP:$src0_modifiers, Src0DPP:$src0,
                     Src1ModDPP:$src1_modifiers, Src1DPP:$src1,
                     getVregSrcForVT<Src2VT>.ret:$src2, // stub argument
                     dpp8:$dpp8, Dpp8FI:$fi);
  let InsSDWA = (ins Src0ModSDWA:$src0_modifiers, Src0SDWA:$src0,
                     Src1ModSDWA:$src1_modifiers, Src1SDWA:$src1,
                     getVregSrcForVT<Src2VT>.ret:$src2, // stub argument
                     Clamp:$clamp, omod:$omod,
                     dst_sel:$dst_sel, dst_unused:$dst_unused,
                     src0_sel:$src0_sel, src1_sel:$src1_sel);
  let Asm32 = getAsm32<1, 2, vt0>.ret;
  let AsmDPP = getAsmDPP<1, 2, HasModifiers, vt0>.ret;
  let AsmDPP16 = getAsmDPP16<1, 2, HasModifiers, vt0>.ret;
  let AsmDPP8 = getAsmDPP8<1, 2, 0, vt0>.ret;
  let AsmSDWA = getAsmSDWA<1, 2, vt0>.ret;
  let AsmSDWA9 = getAsmSDWA9<1, 1, 2, vt0>.ret;
  let AsmVOP3Base =
      getAsmVOP3Base<2 /*NumSrcArgs*/, HasDst, HasClamp,
                        HasOpSel, HasOMod, IsVOP3P, HasModifiers,
                        HasModifiers, HasModifiers,
                        0 /*Src2HasMods*/, DstVT>.ret;
  let HasSrc2 = 0;
  let HasSrc2Mods = 0;

  let HasExt = 1;
  let HasExtDPP = 1;
  let HasExt32BitDPP = 1;
  let HasExtSDWA = 1;
  let HasExtSDWA9 = 0;
  let TieRegDPP = "$src2";
}

def VOP_MAC_F16 : VOP_MAC <f16>;
def VOP_MAC_F16_t16 : VOP_MAC <f16> {
  let IsTrue16 = 1;
  let HasOpSel = 1;
  let AsmVOP3OpSel = getAsmVOP3OpSel<2/*NumSrcArgs*/, HasClamp, HasOMod,
                        HasSrc0FloatMods, HasSrc1FloatMods, HasSrc2FloatMods>.ret;
  let DstRC = VOPDstOperand<VGPR_32_Lo128>;
  let DstRC64 = VOPDstOperand<VGPR_32>;
  let Src1RC32 = VGPRSrc_32_Lo128;
  let Ins32 = (ins Src0RC32:$src0, Src1RC32:$src1, getVregSrcForVT<Src2VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret:$src2);
  let Src0DPP = getVregSrcForVT<Src0VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
  let Src1DPP = getVregSrcForVT<Src1VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
  let Src2DPP = getVregSrcForVT<Src2VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
  let Src0ModDPP = getSrcModDPP_t16<Src0VT>.ret;
  let Src1ModDPP = getSrcModDPP_t16<Src1VT>.ret;
  let Src2ModDPP = getSrcModDPP_t16<Src2VT>.ret;
  let InsDPP = (ins Src0ModDPP:$src0_modifiers, Src0DPP:$src0,
                    Src1ModDPP:$src1_modifiers, Src1DPP:$src1,
                    getVregSrcForVT<Src2VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret:$src2, // stub argument
                    dpp_ctrl:$dpp_ctrl, DppRowMask:$row_mask,
                    DppBankMask:$bank_mask, DppBoundCtrl:$bound_ctrl);
  let InsDPP8 = (ins Src0ModDPP:$src0_modifiers, Src0DPP:$src0,
                     Src1ModDPP:$src1_modifiers, Src1DPP:$src1,
                     getVregSrcForVT<Src2VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret:$src2, // stub argument
                     dpp8:$dpp8, Dpp8FI:$fi);
  let Src2Mod = FP32InputMods; // dummy unused modifiers
  let Src2RC64 = VGPRSrc_32;   // stub argument
  let Src1ModVOP3DPP = getSrcModVOP3DPP<Src1VT, 1/*IsFake16*/>.ret;
}
def VOP_MAC_F32 : VOP_MAC <f32>;
let HasExtDPP = 0, HasExt32BitDPP = 0 in
def VOP_MAC_LEGACY_F32 : VOP_MAC <f32>;
let HasExtSDWA = 0, HasExt32BitDPP = 0, HasExt64BitDPP = 1 in
def VOP_MAC_F64 : VOP_MAC <f64>;

class VOP_DOT_ACC<ValueType vt0, ValueType vt1> : VOP_MAC<vt0, vt1> {
  let HasClamp = 0;
  let HasExtSDWA = 0;
  let HasOpSel = 0;
  let IsPacked = 0;
}

def VOP_DOT_ACC_F32_V2F16 : VOP_DOT_ACC<f32, v2f16> {
  let Src0ModDPP = FPVRegInputMods;
  let Src1ModDPP = FPVRegInputMods;
  let HasClamp = 1;
}

def VOP_DOT_ACC_I32_I32   : VOP_DOT_ACC<i32, i32> {
  let HasExtVOP3DPP = 0;
  let HasSrc0Mods = 1;
  let HasSrc1Mods = 1;
  let HasClamp = 1;

  let Src0Mod = Int32InputMods;
  let Src1Mod = Int32InputMods;
  let Ins64 = getIns64<Src0RC64, Src1RC64, getVregSrcForVT<Src2VT>.ret,
                       3 /*NumSrcArgs*/, HasClamp, 1 /*HasModifiers*/,
                       1 /*HasSrc2Mods*/, HasOMod,
                       Src0Mod, Src1Mod, Src2Mod>.ret;
  let Asm64 = "$vdst, $src0, $src1$clamp";
}

// Write out to vcc or arbitrary SGPR.
def VOP2b_I32_I1_I32_I32 : VOPProfile<[i32, i32, i32, untyped], /*EnableClamp=*/1> {
  let Asm32 = "$vdst, vcc, $src0, $src1";
  let AsmVOP3Base = "$vdst, $sdst, $src0, $src1$clamp";
  let AsmSDWA = "$vdst, vcc, $src0_modifiers, $src1_modifiers$clamp $dst_sel $dst_unused $src0_sel $src1_sel";
  let AsmSDWA9 = "$vdst, vcc, $src0_modifiers, $src1_modifiers$clamp $dst_sel $dst_unused $src0_sel $src1_sel";
  let AsmDPP = "$vdst, vcc, $src0, $src1 $dpp_ctrl$row_mask$bank_mask$bound_ctrl";
  let AsmDPP8 = "$vdst, vcc, $src0, $src1 $dpp8$fi";
  let AsmDPP16 = AsmDPP#"$fi";
  let InsDPP = (ins DstRCDPP:$old,
                    Src0DPP:$src0,
                    Src1DPP:$src1,
                    dpp_ctrl:$dpp_ctrl, DppRowMask:$row_mask,
                    DppBankMask:$bank_mask, DppBoundCtrl:$bound_ctrl);
  let InsDPP16 = !con(InsDPP, (ins Dpp16FI:$fi));
  let InsDPP8 = (ins DstRCDPP:$old,
                    Src0DPP:$src0,
                    Src1DPP:$src1,
                    dpp8:$dpp8, Dpp8FI:$fi);
  let Outs32 = (outs DstRC:$vdst);
  let Outs64 = (outs DstRC:$vdst, VOPDstS64orS32:$sdst);
  let OutsVOP3DPP = Outs64;
  let OutsVOP3DPP8 = Outs64;
}

// Write out to vcc or arbitrary SGPR and read in from vcc or
// arbitrary SGPR.
def VOP2b_I32_I1_I32_I32_I1 : VOPProfile<[i32, i32, i32, i1], /*EnableClamp=*/1> {
  let HasSrc2Mods = 0;
  let Asm32 = "$vdst, vcc, $src0, $src1, vcc";
  let AsmSDWA = "$vdst, vcc, $src0_modifiers, $src1_modifiers, vcc$clamp $dst_sel $dst_unused $src0_sel $src1_sel";
  let AsmSDWA9 = "$vdst, vcc, $src0_modifiers, $src1_modifiers, vcc$clamp $dst_sel $dst_unused $src0_sel $src1_sel";
  let AsmDPP = "$vdst, vcc, $src0, $src1, vcc $dpp_ctrl$row_mask$bank_mask$bound_ctrl";
  let AsmDPP8 = "$vdst, vcc, $src0, $src1, vcc $dpp8$fi";
  let AsmDPP16 = AsmDPP#"$fi";
  let Outs32 = (outs DstRC:$vdst);
  let Outs64 = (outs DstRC:$vdst, VOPDstS64orS32:$sdst);
  let AsmVOP3Base = "$vdst, $sdst, $src0, $src1, $src2$clamp";
  let OutsVOP3DPP = Outs64;
  let OutsVOP3DPP8 = Outs64;

  // Suppress src2 implied by type since the 32-bit encoding uses an
  // implicit VCC use.
  let Ins32 = (ins Src0RC32:$src0, Src1RC32:$src1);

  let InsSDWA = (ins Src0ModSDWA:$src0_modifiers, Src0SDWA:$src0,
                     Src1ModSDWA:$src1_modifiers, Src1SDWA:$src1,
                     Clamp:$clamp,
                     dst_sel:$dst_sel, dst_unused:$dst_unused,
                     src0_sel:$src0_sel, src1_sel:$src1_sel);

  let InsDPP = (ins DstRCDPP:$old,
                    Src0DPP:$src0,
                    Src1DPP:$src1,
                    dpp_ctrl:$dpp_ctrl, DppRowMask:$row_mask,
                    DppBankMask:$bank_mask, DppBoundCtrl:$bound_ctrl);
  let InsDPP16 = !con(InsDPP, (ins Dpp16FI:$fi));
  let InsDPP8 = (ins DstRCDPP:$old,
                     Src0DPP:$src0,
                     Src1DPP:$src1,
                     dpp8:$dpp8, Dpp8FI:$fi);

  let HasExt = 1;
  let HasExtDPP = 1;
  let HasExt32BitDPP = 1;
  let HasExtSDWA = 1;
  let HasExtSDWA9 = 1;
}

// Read in from vcc or arbitrary SGPR.
class VOP2e_SGPR<list<ValueType> ArgVT> : VOPProfile<ArgVT> {
  let Asm32 = "$vdst, $src0, $src1";
  let AsmSDWA = "$vdst, $src0_modifiers, $src1_modifiers, vcc$clamp $dst_sel $dst_unused $src0_sel $src1_sel";
  let AsmSDWA9 = "$vdst, $src0_modifiers, $src1_modifiers, vcc$clamp $dst_sel $dst_unused $src0_sel $src1_sel";
  let AsmDPP = "$vdst, $src0_modifiers, $src1_modifiers, vcc $dpp_ctrl$row_mask$bank_mask$bound_ctrl";
  let AsmDPP8 = "$vdst, $src0, $src1, vcc $dpp8$fi";
  let AsmDPP16 = AsmDPP#"$fi";
  let AsmVOP3Base = "$vdst, $src0_modifiers, $src1_modifiers, $src2";

  let Outs32 = (outs DstRC:$vdst);
  let Outs64 = (outs DstRC64:$vdst);

  // Suppress src2 implied by type since the 32-bit encoding uses an
  // implicit VCC use.
  let Ins32 = (ins VSrc_f32:$src0, Src1RC32:$src1);

  let HasModifiers = 1;

  // Select FP modifiers for VOP3
  let Src0Mod = !if(!eq(Src0VT.Size, 16), FP16InputMods, FP32InputMods);
  let Src1Mod = Src0Mod;

  let HasSrc0IntMods = 0;
  let HasSrc1IntMods = 0;
  let HasSrc0FloatMods = 1;
  let HasSrc1FloatMods = 1;
  let InsSDWA = (ins FP32SDWAInputMods:$src0_modifiers, SDWASrc_f32:$src0,
                     FP32SDWAInputMods:$src1_modifiers, SDWASrc_f32:$src1,
                     Clamp:$clamp,
                     dst_sel:$dst_sel, dst_unused:$dst_unused,
                     src0_sel:$src0_sel, src1_sel:$src1_sel);

  let InsDPP = (ins DstRCDPP:$old,
                    FPVRegInputMods:$src0_modifiers, Src0DPP:$src0,
                    FPVRegInputMods:$src1_modifiers, Src1DPP:$src1,
                    dpp_ctrl:$dpp_ctrl, DppRowMask:$row_mask,
                    DppBankMask:$bank_mask, DppBoundCtrl:$bound_ctrl);
  let InsDPP16 = !con(InsDPP, (ins Dpp16FI:$fi));
  let InsDPP8 = (ins DstRCDPP:$old,
                     FPVRegInputMods:$src0_modifiers, Src0DPP:$src0,
                     FPVRegInputMods:$src1_modifiers, Src1DPP:$src1,
                     dpp8:$dpp8, Dpp8FI:$fi);

  let Src0ModVOP3DPP = FPVRegInputMods;
  let Src1ModVOP3DPP = FP32VCSrcInputMods;

  let HasExt = 1;
  let HasExtDPP = 1;
  let HasExt32BitDPP = 1;
  let HasExtSDWA = 1;
  let HasExtSDWA9 = 1;
}

def VOP2e_I32_I32_I32_I1 : VOP2e_SGPR<[i32, i32, i32, i1]>;
def VOP2e_I16_I16_I16_I1_fake16 : VOP2e_SGPR<[i16, i16, i16, i1]> {
  let IsTrue16 = 1;
  let DstRC64 = getVALUDstForVT<DstVT>.ret;

  let Src0Mod = getSrcMod<f16>.ret;
  let Src1Mod = getSrcMod<f16>.ret;

  let Src0VOP3DPP = VGPRSrc_32;
  let Src1VOP3DPP = getVOP3DPPSrcForVT<Src1VT>.ret;
  let Src1ModVOP3DPP = getSrcModVOP3DPP<f16, 1/*IsFake16*/>.ret;
}

def VOP_READLANE : VOPProfile<[i32, i32, i32, untyped]> {
  let Outs32 = (outs SReg_32:$vdst);
  let Outs64 = Outs32;
  let Ins32 = (ins VRegOrLdsSrc_32:$src0, SCSrc_b32:$src1);
  let Ins64 = Ins32;
  let Asm32 = " $vdst, $src0, $src1";
  let Asm64 = Asm32;

  let HasExt = 0;
  let HasExtDPP = 0;
  let HasExt32BitDPP = 0;
  let HasExt64BitDPP = 0;
  let HasExtSDWA = 0;
  let HasExtSDWA9 = 0;
}

def VOP_WRITELANE : VOPProfile<[i32, i32, i32, i32]> {
  let Outs32 = (outs VGPR_32:$vdst);
  let Outs64 = Outs32;
  let Ins32 = (ins SCSrc_b32:$src0, SCSrc_b32:$src1, VGPR_32:$vdst_in);
  let Ins64 = Ins32;
  let Asm32 = " $vdst, $src0, $src1";
  let Asm64 = Asm32;
  let HasSrc2 = 0;
  let HasSrc2Mods = 0;

  let HasExt = 0;
  let HasExtDPP = 0;
  let HasExt32BitDPP = 0;
  let HasExt64BitDPP = 0;
  let HasExtSDWA = 0;
  let HasExtSDWA9 = 0;
}

//===----------------------------------------------------------------------===//
// VOP2 Instructions
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isGFX11Plus in
defm V_CNDMASK_B16 : VOP2eInst <"v_cndmask_b16", VOP2e_I16_I16_I16_I1_fake16>;
defm V_CNDMASK_B32 : VOP2eInst_VOPD <"v_cndmask_b32", VOP2e_I32_I32_I32_I1, 0x9, "v_cndmask_b32">;
let SubtargetPredicate = HasMadMacF32Insts, isReMaterializable = 1 in
def V_MADMK_F32 : VOP2_Pseudo <"v_madmk_f32", VOP_MADMK_F32, []>;

let isCommutable = 1 in {
let isReMaterializable = 1 in {
defm V_ADD_F32 : VOP2Inst_VOPD <"v_add_f32", VOP_F32_F32_F32, 0x4, "v_add_f32", any_fadd>;
defm V_SUB_F32 : VOP2Inst_VOPD <"v_sub_f32", VOP_F32_F32_F32, 0x5, "v_sub_f32", any_fsub>;
defm V_SUBREV_F32 : VOP2Inst_VOPD <"v_subrev_f32", VOP_F32_F32_F32, 0x6, "v_subrev_f32", null_frag, "v_sub_f32">;
defm V_MUL_LEGACY_F32 : VOP2Inst_VOPD <"v_mul_legacy_f32", VOP_F32_F32_F32, 0x7, "v_mul_dx9_zero_f32", AMDGPUfmul_legacy>;
defm V_MUL_F32 : VOP2Inst_VOPD <"v_mul_f32", VOP_F32_F32_F32, 0x3, "v_mul_f32", any_fmul>;
defm V_MUL_I32_I24 : VOP2Inst <"v_mul_i32_i24", VOP_I32_I32_I32_ARITH, AMDGPUmul_i24>;
defm V_MUL_HI_I32_I24 : VOP2Inst <"v_mul_hi_i32_i24", VOP_I32_I32_I32, AMDGPUmulhi_i24>;
defm V_MUL_U32_U24 : VOP2Inst <"v_mul_u32_u24", VOP_I32_I32_I32_ARITH, AMDGPUmul_u24>;
defm V_MUL_HI_U32_U24 : VOP2Inst <"v_mul_hi_u32_u24", VOP_I32_I32_I32, AMDGPUmulhi_u24>;
defm V_MIN_F32 : VOP2Inst_VOPD <"v_min_f32", VOP_F32_F32_F32, 0xb, "v_min_f32", fminnum_like>;
defm V_MAX_F32 : VOP2Inst_VOPD <"v_max_f32", VOP_F32_F32_F32, 0xa, "v_max_f32", fmaxnum_like>;
defm V_MIN_I32 : VOP2Inst <"v_min_i32", VOP_PAT_GEN<VOP_I32_I32_I32>, smin>;
defm V_MAX_I32 : VOP2Inst <"v_max_i32", VOP_PAT_GEN<VOP_I32_I32_I32>, smax>;
defm V_MIN_U32 : VOP2Inst <"v_min_u32", VOP_PAT_GEN<VOP_I32_I32_I32>, umin>;
defm V_MAX_U32 : VOP2Inst <"v_max_u32", VOP_PAT_GEN<VOP_I32_I32_I32>, umax>;
defm V_LSHRREV_B32 : VOP2Inst <"v_lshrrev_b32", VOP_I32_I32_I32, clshr_rev_32, "v_lshr_b32">;
defm V_ASHRREV_I32 : VOP2Inst <"v_ashrrev_i32", VOP_I32_I32_I32, cashr_rev_32, "v_ashr_i32">;
defm V_LSHLREV_B32 : VOP2Inst_VOPD <"v_lshlrev_b32", VOP_I32_I32_I32, 0x11, "v_lshlrev_b32", clshl_rev_32, "v_lshl_b32">;
defm V_AND_B32 : VOP2Inst_VOPD <"v_and_b32", VOP_PAT_GEN<VOP_I32_I32_I32>, 0x12, "v_and_b32", and>;
defm V_OR_B32 : VOP2Inst <"v_or_b32", VOP_PAT_GEN<VOP_I32_I32_I32>, or>;
defm V_XOR_B32 : VOP2Inst <"v_xor_b32", VOP_PAT_GEN<VOP_I32_I32_I32>, xor>;
} // End isReMaterializable = 1

let mayRaiseFPException = 0 in {
let OtherPredicates = [HasMadMacF32Insts] in {
let Constraints = "$vdst = $src2", DisableEncoding="$src2",
    isConvertibleToThreeAddress = 1 in {
defm V_MAC_F32 : VOP2Inst <"v_mac_f32", VOP_MAC_F32>;

let SubtargetPredicate = isGFX6GFX7GFX10 in
defm V_MAC_LEGACY_F32 : VOP2Inst <"v_mac_legacy_f32", VOP_MAC_LEGACY_F32>;
} // End Constraints = "$vdst = $src2", DisableEncoding="$src2",
  //     isConvertibleToThreeAddress = 1

let isReMaterializable = 1 in
def V_MADAK_F32 : VOP2_Pseudo <"v_madak_f32", VOP_MADAK_F32, []>;
} // End OtherPredicates = [HasMadMacF32Insts]
} // End mayRaiseFPException = 0

// No patterns so that the scalar instructions are always selected.
// The scalar versions will be replaced with vector when needed later.
defm V_ADD_CO_U32 : VOP2bInst <"v_add_co_u32", VOP2b_I32_I1_I32_I32, null_frag, "v_add_co_u32", 1>;
defm V_SUB_CO_U32 : VOP2bInst <"v_sub_co_u32", VOP2b_I32_I1_I32_I32, null_frag, "v_sub_co_u32", 1>;
defm V_SUBREV_CO_U32 : VOP2bInst <"v_subrev_co_u32", VOP2b_I32_I1_I32_I32, null_frag, "v_sub_co_u32", 1>;
defm V_ADDC_U32 : VOP2bInst <"v_addc_u32", VOP2b_I32_I1_I32_I32_I1, null_frag, "v_addc_u32", 1>;
defm V_SUBB_U32 : VOP2bInst <"v_subb_u32", VOP2b_I32_I1_I32_I32_I1, null_frag, "v_subb_u32", 1>;
defm V_SUBBREV_U32 : VOP2bInst <"v_subbrev_u32", VOP2b_I32_I1_I32_I32_I1, null_frag, "v_subb_u32", 1>;


let SubtargetPredicate = HasAddNoCarryInsts, isReMaterializable = 1 in {
defm V_ADD_U32 : VOP2Inst_VOPD <"v_add_u32", VOP_I32_I32_I32_ARITH, 0x10, "v_add_nc_u32", null_frag, "v_add_u32", 1>;
defm V_SUB_U32 : VOP2Inst <"v_sub_u32", VOP_I32_I32_I32_ARITH, null_frag, "v_sub_u32", 1>;
defm V_SUBREV_U32 : VOP2Inst <"v_subrev_u32", VOP_I32_I32_I32_ARITH, null_frag, "v_sub_u32", 1>;
}

} // End isCommutable = 1

// These are special and do not read the exec mask.
let isConvergent = 1, Uses = []<Register>, IsInvalidSingleUseConsumer = 1 in {
def V_READLANE_B32 : VOP2_Pseudo<"v_readlane_b32", VOP_READLANE, []>;
let IsNeverUniform = 1, Constraints = "$vdst = $vdst_in", DisableEncoding="$vdst_in" in {
def V_WRITELANE_B32 : VOP2_Pseudo<"v_writelane_b32", VOP_WRITELANE, []> {
    let IsInvalidSingleUseProducer = 1;
  }
} // End IsNeverUniform, $vdst = $vdst_in, DisableEncoding $vdst_in
} // End isConvergent = 1

foreach vt = Reg32Types.types in {
  def : GCNPat<(vt (int_amdgcn_readlane vt:$src0, i32:$src1)),
        (V_READLANE_B32 VRegOrLdsSrc_32:$src0, SCSrc_b32:$src1)
  >;

  def : GCNPat<(vt (int_amdgcn_writelane vt:$src0, i32:$src1, vt:$src2)),
        (V_WRITELANE_B32 SCSrc_b32:$src0, SCSrc_b32:$src1, VGPR_32:$src2)
  >;
}

let isReMaterializable = 1 in {
defm V_BFM_B32 : VOP2Inst <"v_bfm_b32", VOP_I32_I32_I32>;
defm V_BCNT_U32_B32 : VOP2Inst <"v_bcnt_u32_b32", VOP_I32_I32_I32, add_ctpop>;
let IsNeverUniform = 1 in {
defm V_MBCNT_LO_U32_B32 : VOP2Inst <"v_mbcnt_lo_u32_b32", VOP_I32_I32_I32, int_amdgcn_mbcnt_lo>;
defm V_MBCNT_HI_U32_B32 : VOP2Inst <"v_mbcnt_hi_u32_b32", VOP_I32_I32_I32, int_amdgcn_mbcnt_hi>;
} // End IsNeverUniform = 1
defm V_LDEXP_F32 : VOP2Inst <"v_ldexp_f32", VOP_F32_F32_I32, any_fldexp>;

let ReadsModeReg = 0, mayRaiseFPException = 0 in {
defm V_CVT_PKNORM_I16_F32 : VOP2Inst <"v_cvt_pknorm_i16_f32", VOP_V2I16_F32_F32, AMDGPUpknorm_i16_f32>;
defm V_CVT_PKNORM_U16_F32 : VOP2Inst <"v_cvt_pknorm_u16_f32", VOP_V2I16_F32_F32, AMDGPUpknorm_u16_f32>;
}

defm V_CVT_PKRTZ_F16_F32 : VOP2Inst <"v_cvt_pkrtz_f16_f32", VOP_V2F16_F32_F32, AMDGPUpkrtz_f16_f32>;
defm V_CVT_PK_U16_U32 : VOP2Inst <"v_cvt_pk_u16_u32", VOP_V2I16_I32_I32, AMDGPUpk_u16_u32>;
defm V_CVT_PK_I16_I32 : VOP2Inst <"v_cvt_pk_i16_i32", VOP_V2I16_I32_I32, AMDGPUpk_i16_i32>;


let SubtargetPredicate = isGFX6GFX7 in {
defm V_MIN_LEGACY_F32 : VOP2Inst <"v_min_legacy_f32", VOP_F32_F32_F32, AMDGPUfmin_legacy>;
defm V_MAX_LEGACY_F32 : VOP2Inst <"v_max_legacy_f32", VOP_F32_F32_F32, AMDGPUfmax_legacy>;
} // End SubtargetPredicate = isGFX6GFX7

let isCommutable = 1 in {
let SubtargetPredicate = isGFX6GFX7 in {
defm V_LSHR_B32 : VOP2Inst <"v_lshr_b32", VOP_PAT_GEN<VOP_I32_I32_I32>, csrl_32>;
defm V_ASHR_I32 : VOP2Inst <"v_ashr_i32", VOP_PAT_GEN<VOP_I32_I32_I32>, csra_32>;
defm V_LSHL_B32 : VOP2Inst <"v_lshl_b32", VOP_PAT_GEN<VOP_I32_I32_I32>, cshl_32>;
} // End SubtargetPredicate = isGFX6GFX7
} // End isCommutable = 1
} // End isReMaterializable = 1

defm V_CVT_PKACCUM_U8_F32 : VOP2Inst <"v_cvt_pkaccum_u8_f32", VOP_NO_EXT<VOP_I32_F32_I32>>; // TODO: set "Uses = dst"

class DivergentBinOp<SDPatternOperator Op, VOP_Pseudo Inst> :
  GCNPat<
      (DivergentBinFrag<Op> Inst.Pfl.Src0VT:$src0, Inst.Pfl.Src1VT:$src1),
      !if(!cast<Commutable_REV>(Inst).IsOrig,
        (Inst $src0, $src1),
        (Inst $src1, $src0)
      )
  >;

class DivergentClampingBinOp<SDPatternOperator Op, VOP_Pseudo Inst> :
  GCNPat<
      (DivergentBinFrag<Op> Inst.Pfl.Src0VT:$src0, Inst.Pfl.Src1VT:$src1),
      !if(!cast<Commutable_REV>(Inst).IsOrig,
        (Inst $src0, $src1, 0),
        (Inst $src1, $src0, 0)
      )
  >;

def : DivergentBinOp<csrl_32, V_LSHRREV_B32_e64>;
def : DivergentBinOp<csra_32, V_ASHRREV_I32_e64>;
def : DivergentBinOp<cshl_32, V_LSHLREV_B32_e64>;

let SubtargetPredicate = HasAddNoCarryInsts in {
  def : DivergentClampingBinOp<add, V_ADD_U32_e64>;
  def : DivergentClampingBinOp<sub, V_SUB_U32_e64>;
}

let SubtargetPredicate = isGFX6GFX7GFX8GFX9, Predicates = [isGFX6GFX7GFX8GFX9] in {
def : DivergentClampingBinOp<add, V_ADD_CO_U32_e64>;
def : DivergentClampingBinOp<sub, V_SUB_CO_U32_e64>;
}

def : DivergentBinOp<adde, V_ADDC_U32_e32>;
def : DivergentBinOp<sube, V_SUBB_U32_e32>;

class divergent_i64_BinOp <SDPatternOperator Op, Instruction Inst> :
  GCNPat<
      (DivergentBinFrag<Op> i64:$src0, i64:$src1),
      (REG_SEQUENCE VReg_64,
        (Inst
          (i32 (EXTRACT_SUBREG $src0, sub0)),
          (i32 (EXTRACT_SUBREG $src1, sub0))
        ), sub0,
        (Inst
          (i32 (EXTRACT_SUBREG $src0, sub1)),
          (i32 (EXTRACT_SUBREG $src1, sub1))
        ), sub1
      )
  >;

def :  divergent_i64_BinOp <and, V_AND_B32_e64>;
def :  divergent_i64_BinOp <or,  V_OR_B32_e64>;
def :  divergent_i64_BinOp <xor, V_XOR_B32_e64>;

// mul24 w/ 64 bit output.
class mul24_64_Pat<SDPatternOperator Op, Instruction InstLo, Instruction InstHi> : GCNPat<
  (i64 (Op i32:$src0, i32:$src1)),
  (REG_SEQUENCE VReg_64,
    (InstLo $src0, $src1), sub0,
    (InstHi $src0, $src1), sub1)
>;

def : mul24_64_Pat<AMDGPUmul_i24, V_MUL_I32_I24_e64, V_MUL_HI_I32_I24_e64>;
def : mul24_64_Pat<AMDGPUmul_u24, V_MUL_U32_U24_e64, V_MUL_HI_U32_U24_e64>;

//===----------------------------------------------------------------------===//
// 16-Bit Operand Instructions
//===----------------------------------------------------------------------===//

// The ldexp.f16 intrinsic expects a integer src1 operand, though the hardware
// encoding treats src1 as an f16
def LDEXP_F16_VOPProfile : VOPProfile <[f16, f16, f16, untyped]> {
  let Src1Mod = Int32InputMods;
  let Src1ModDPP = IntVRegInputMods;
  let Src1ModVOP3DPP = IntVRegInputMods;
  // SDWA sext is the only modifier allowed.
  let HasSrc1IntMods = 1;
  let HasSrc1FloatMods = 0;
  let Src1ModSDWA = Int16SDWAInputMods;
}
def LDEXP_F16_VOPProfile_True16 : VOPProfile_Fake16<VOP_F16_F16_F16> {
  let Src1RC32 = RegisterOperand<VGPR_32_Lo128>;
  let Src1DPP = RegisterOperand<VGPR_32_Lo128>;
  let Src1ModDPP = IntT16VRegInputMods</* IsFake16= */ 1>;
}

let isReMaterializable = 1 in {
let FPDPRounding = 1 in {
  let OtherPredicates = [Has16BitInsts], True16Predicate = NotHasTrue16BitInsts in
    defm V_LDEXP_F16 : VOP2Inst <"v_ldexp_f16", LDEXP_F16_VOPProfile>;
  let SubtargetPredicate = HasTrue16BitInsts in
    defm V_LDEXP_F16_t16 : VOP2Inst <"v_ldexp_f16_t16", LDEXP_F16_VOPProfile_True16>;
} // End FPDPRounding = 1
// FIXME VOP3 Only instructions. NFC using VOPProfile_True16 for these until a planned change to use a new register class for VOP3 encoded True16 instuctions
defm V_LSHLREV_B16 : VOP2Inst_e64_t16 <"v_lshlrev_b16", VOP_I16_I16_I16, clshl_rev_16>;
defm V_LSHRREV_B16 : VOP2Inst_e64_t16 <"v_lshrrev_b16", VOP_I16_I16_I16, clshr_rev_16>;
defm V_ASHRREV_I16 : VOP2Inst_e64_t16 <"v_ashrrev_i16", VOP_I16_I16_I16, cashr_rev_16>;
let isCommutable = 1 in {
let FPDPRounding = 1 in {
defm V_ADD_F16 : VOP2Inst_t16 <"v_add_f16", VOP_F16_F16_F16, any_fadd>;
defm V_SUB_F16 : VOP2Inst_t16 <"v_sub_f16", VOP_F16_F16_F16, any_fsub>;
defm V_SUBREV_F16 : VOP2Inst_t16 <"v_subrev_f16", VOP_F16_F16_F16, null_frag, "v_sub_f16">;
defm V_MUL_F16 : VOP2Inst_t16 <"v_mul_f16", VOP_F16_F16_F16, any_fmul>;
} // End FPDPRounding = 1
defm V_MUL_LO_U16 : VOP2Inst_e64_t16 <"v_mul_lo_u16", VOP_I16_I16_I16, mul>;
defm V_MAX_F16 : VOP2Inst_t16 <"v_max_f16", VOP_F16_F16_F16, fmaxnum_like>;
defm V_MIN_F16 : VOP2Inst_t16 <"v_min_f16", VOP_F16_F16_F16, fminnum_like>;
defm V_MAX_U16 : VOP2Inst_e64_t16 <"v_max_u16", VOP_I16_I16_I16, umax>;
defm V_MAX_I16 : VOP2Inst_e64_t16 <"v_max_i16", VOP_I16_I16_I16, smax>;
defm V_MIN_U16 : VOP2Inst_e64_t16 <"v_min_u16", VOP_I16_I16_I16, umin>;
defm V_MIN_I16 : VOP2Inst_e64_t16 <"v_min_i16", VOP_I16_I16_I16, smin>;
} // End isCommutable = 1
} // End isReMaterializable = 1

class LDEXP_F16_Pat <SDPatternOperator op, VOP_Pseudo inst, VOPProfile P = inst.Pfl> : GCNPat <
  (P.DstVT (op (P.Src0VT (VOP3Mods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp, i32:$omod)),
               (i16 (VOP3Mods0 P.Src1VT:$src1, i32:$src1_modifiers)))),
  (inst $src0_modifiers, $src0,
        $src1_modifiers, $src1,
        $clamp, /* clamp */
        $omod /* omod */)
>;

let OtherPredicates = [NotHasTrue16BitInsts] in
def : LDEXP_F16_Pat<any_fldexp, V_LDEXP_F16_e64>;

let OtherPredicates = [HasTrue16BitInsts] in
def : LDEXP_F16_Pat<any_fldexp, V_LDEXP_F16_t16_e64>;

let SubtargetPredicate = isGFX11Plus in {
  let isCommutable = 1 in {
    defm V_AND_B16_t16 : VOP2Inst_e64 <"v_and_b16_t16", VOPProfile_Fake16<VOP_I16_I16_I16>, and>;
    defm V_OR_B16_t16  : VOP2Inst_e64 <"v_or_b16_t16", VOPProfile_Fake16<VOP_I16_I16_I16>, or>;
    defm V_XOR_B16_t16 : VOP2Inst_e64 <"v_xor_b16_t16", VOPProfile_Fake16<VOP_I16_I16_I16>, xor>;
  } // End isCommutable = 1
} // End SubtargetPredicate = isGFX11Plus

let FPDPRounding = 1, isReMaterializable = 1, FixedSize = 1 in {
let SubtargetPredicate = isGFX10Plus, True16Predicate = NotHasTrue16BitInsts in {
def V_FMAMK_F16 : VOP2_Pseudo <"v_fmamk_f16", VOP_MADMK_F16, [], "">;
}
let SubtargetPredicate = HasTrue16BitInsts in {
def V_FMAMK_F16_t16 : VOP2_Pseudo <"v_fmamk_f16_t16", VOP_MADMK_F16_t16, [], "">;
}

let isCommutable = 1 in {
let SubtargetPredicate = isGFX10Plus, True16Predicate = NotHasTrue16BitInsts in {
def V_FMAAK_F16 : VOP2_Pseudo <"v_fmaak_f16", VOP_MADAK_F16, [], "">;
}
let SubtargetPredicate = HasTrue16BitInsts in {
def V_FMAAK_F16_t16 : VOP2_Pseudo <"v_fmaak_f16_t16", VOP_MADAK_F16_t16, [], "">;
}
} // End isCommutable = 1
} // End FPDPRounding  = 1, isReMaterializable = 1, FixedSize = 1

let Constraints = "$vdst = $src2",
    DisableEncoding="$src2",
    isConvertibleToThreeAddress = 1,
    isCommutable = 1 in {
let SubtargetPredicate = isGFX10Plus, True16Predicate = NotHasTrue16BitInsts in {
defm V_FMAC_F16 : VOP2Inst <"v_fmac_f16", VOP_MAC_F16>;
}
let SubtargetPredicate = HasTrue16BitInsts in {
defm V_FMAC_F16_t16 : VOP2Inst <"v_fmac_f16_t16", VOP_MAC_F16_t16>;
}
} // End FMAC Constraints

let SubtargetPredicate = Has16BitInsts in {
let isReMaterializable = 1 in {
let FPDPRounding = 1 in {
def V_MADMK_F16 : VOP2_Pseudo <"v_madmk_f16", VOP_MADMK_F16, [], "">;
} // End FPDPRounding = 1
let isCommutable = 1 in {
let mayRaiseFPException = 0 in {
def V_MADAK_F16 : VOP2_Pseudo <"v_madak_f16", VOP_MADAK_F16, [], "">;
}
let SubtargetPredicate = isGFX8GFX9 in {
  defm V_ADD_U16 : VOP2Inst <"v_add_u16", VOP_I16_I16_I16_ARITH, add>;
  defm V_SUB_U16 : VOP2Inst <"v_sub_u16" , VOP_I16_I16_I16_ARITH, sub>;
  defm V_SUBREV_U16 : VOP2Inst <"v_subrev_u16", VOP_I16_I16_I16_ARITH, null_frag, "v_sub_u16">;
}
} // End isCommutable = 1
} // End isReMaterializable = 1

// FIXME: Missing FPDPRounding
let Constraints = "$vdst = $src2", DisableEncoding="$src2",
    isConvertibleToThreeAddress = 1, isCommutable = 1 in {
defm V_MAC_F16 : VOP2Inst <"v_mac_f16", VOP_MAC_F16>;
}
} // End SubtargetPredicate = Has16BitInsts


let SubtargetPredicate = HasDLInsts in {

let isReMaterializable = 1 in
defm V_XNOR_B32 : VOP2Inst <"v_xnor_b32", VOP_I32_I32_I32, xnor>;

def : GCNPat<
  (i32 (DivergentUnaryFrag<not> (xor_oneuse i32:$src0, i32:$src1))),
  (i32 (V_XNOR_B32_e64 $src0, $src1))
>;

def : GCNPat<
  (i32 (DivergentBinFrag<xor_oneuse> (not i32:$src0), i32:$src1)),
  (i32 (V_XNOR_B32_e64 $src0, $src1))
>;

def : GCNPat<
  (i64 (DivergentUnaryFrag<not> (xor_oneuse i64:$src0, i64:$src1))),
  (REG_SEQUENCE VReg_64, (i32 (V_XNOR_B32_e64
                            (i32 (EXTRACT_SUBREG $src0, sub0)),
                            (i32 (EXTRACT_SUBREG $src1, sub0)))), sub0,
                     (i32 (V_XNOR_B32_e64
                            (i32 (EXTRACT_SUBREG $src0, sub1)),
                            (i32 (EXTRACT_SUBREG $src1, sub1)))), sub1)
>;

def : GCNPat<
  (i64 (DivergentBinFrag<xor_oneuse> (not i64:$src0), i64:$src1)),
  (REG_SEQUENCE VReg_64, (i32 (V_XNOR_B32_e64
                            (i32 (EXTRACT_SUBREG $src0, sub0)),
                            (i32 (EXTRACT_SUBREG $src1, sub0)))), sub0,
                     (i32 (V_XNOR_B32_e64
                            (i32 (EXTRACT_SUBREG $src0, sub1)),
                            (i32 (EXTRACT_SUBREG $src1, sub1)))), sub1)
>;

let Constraints = "$vdst = $src2",
    DisableEncoding = "$src2",
    isConvertibleToThreeAddress = 1,
    isCommutable = 1 in
defm V_FMAC_F32 : VOP2Inst_VOPD <"v_fmac_f32", VOP_MAC_F32, 0x0, "v_fmac_f32">;
} // End SubtargetPredicate = HasDLInsts

let SubtargetPredicate = HasFmaLegacy32 in {

let Constraints = "$vdst = $src2",
    DisableEncoding = "$src2",
    isConvertibleToThreeAddress = 1,
    isCommutable = 1 in
defm V_FMAC_LEGACY_F32 : VOP2Inst <"v_fmac_legacy_f32", VOP_MAC_LEGACY_F32>;

} // End SubtargetPredicate = HasFmaLegacy32

let SubtargetPredicate = HasFmacF64Inst,
    Constraints = "$vdst = $src2",
    DisableEncoding="$src2",
    isConvertibleToThreeAddress = 1,
    isCommutable = 1,
    SchedRW = [WriteDoubleAdd] in
defm V_FMAC_F64 : VOP2Inst <"v_fmac_f64", VOP_MAC_F64>;

let Constraints = "$vdst = $src2",
      DisableEncoding="$src2",
      isConvertibleToThreeAddress = 1,
      isCommutable = 1,
      IsDOT = 1 in {
  let SubtargetPredicate = HasDot5Insts in
    defm V_DOT2C_F32_F16 : VOP2Inst_VOPD<"v_dot2c_f32_f16", VOP_DOT_ACC_F32_V2F16, 0xc, "v_dot2acc_f32_f16">;
  let SubtargetPredicate = HasDot6Insts in
    defm V_DOT4C_I32_I8  : VOP2Inst<"v_dot4c_i32_i8",  VOP_DOT_ACC_I32_I32>;

  let SubtargetPredicate = HasDot4Insts in
    defm V_DOT2C_I32_I16 : VOP2Inst<"v_dot2c_i32_i16", VOP_DOT_ACC_I32_I32>;
  let SubtargetPredicate = HasDot3Insts in
    defm V_DOT8C_I32_I4  : VOP2Inst<"v_dot8c_i32_i4",  VOP_DOT_ACC_I32_I32>;
}

let AddedComplexity = 30 in {
  def : GCNPat<
    (f32 (AMDGPUfdot2 v2f16:$src0, v2f16:$src1, f32:$src2, (i1 DSTCLAMP.NONE))),
    (f32 (V_DOT2C_F32_F16_e32 $src0, $src1, $src2))
  > {
    let SubtargetPredicate = HasDot5Insts;
  }
  def : GCNPat<
    (i32 (int_amdgcn_sdot4 i32:$src0, i32:$src1, i32:$src2, (i1 DSTCLAMP.NONE))),
    (i32 (V_DOT4C_I32_I8_e32 $src0, $src1, $src2))
  > {
    let SubtargetPredicate = HasDot6Insts;
  }
  def : GCNPat<
    (i32 (int_amdgcn_sdot2 v2i16:$src0, v2i16:$src1, i32:$src2, (i1 DSTCLAMP.NONE))),
    (i32 (V_DOT2C_I32_I16_e32 $src0, $src1, $src2))
  > {
    let SubtargetPredicate = HasDot4Insts;
  }
  def : GCNPat<
    (i32 (int_amdgcn_sdot8 i32:$src0, i32:$src1, i32:$src2, (i1 DSTCLAMP.NONE))),
    (i32 (V_DOT8C_I32_I4_e32 $src0, $src1, $src2))
  > {
    let SubtargetPredicate = HasDot3Insts;
  }
} // End AddedComplexity = 30

let SubtargetPredicate = HasFmaakFmamkF32Insts, isReMaterializable = 1, FixedSize = 1 in {
def V_FMAMK_F32 : VOP2_Pseudo<"v_fmamk_f32", VOP_MADMK_F32, [], "">, VOPD_Component<0x2, "v_fmamk_f32">;

let isCommutable = 1 in
def V_FMAAK_F32 : VOP2_Pseudo<"v_fmaak_f32", VOP_MADAK_F32, [], "">, VOPD_Component<0x1, "v_fmaak_f32">;
} // End SubtargetPredicate = HasFmaakFmamkF32Insts, isReMaterializable = 1, FixedSize = 1

let SubtargetPredicate = HasPkFmacF16Inst in {
defm V_PK_FMAC_F16 : VOP2Inst<"v_pk_fmac_f16", VOP_V2F16_V2F16_V2F16>;
} // End SubtargetPredicate = HasPkFmacF16Inst

// Note: 16-bit instructions produce a 0 result in the high 16-bits
// on GFX8 and GFX9 and preserve high 16 bits on GFX10+
multiclass Arithmetic_i16_0Hi_Pats <SDPatternOperator op, Instruction inst> {

def : GCNPat<
  (i32 (zext (op i16:$src0, i16:$src1))),
  (inst VSrc_b16:$src0, VSrc_b16:$src1)
>;

def : GCNPat<
  (i64 (zext (op i16:$src0, i16:$src1))),
   (REG_SEQUENCE VReg_64,
     (inst $src0, $src1), sub0,
     (V_MOV_B32_e32 (i32 0)), sub1)
>;
}

class ZExt_i16_i1_Pat <SDNode ext> : GCNPat <
  (i16 (ext i1:$src)),
  (V_CNDMASK_B32_e64 (i32 0/*src0mod*/), (i32 0/*src0*/),
                     (i32 0/*src1mod*/), (i32 1/*src1*/),
                     $src)
>;

foreach vt = [i16, v2i16] in {
def : GCNPat <
  (and vt:$src0, vt:$src1),
  (V_AND_B32_e64 VSrc_b32:$src0, VSrc_b32:$src1)
>;

def : GCNPat <
  (or vt:$src0, vt:$src1),
  (V_OR_B32_e64 VSrc_b32:$src0, VSrc_b32:$src1)
>;

def : GCNPat <
  (xor vt:$src0, vt:$src1),
  (V_XOR_B32_e64 VSrc_b32:$src0, VSrc_b32:$src1)
>;
}

let Predicates = [Has16BitInsts, isGFX8GFX9] in {

// Undo sub x, c -> add x, -c canonicalization since c is more likely
// an inline immediate than -c.
// TODO: Also do for 64-bit.
def : GCNPat<
  (add i16:$src0, (i16 NegSubInlineIntConst16:$src1)),
  (V_SUB_U16_e64 VSrc_b16:$src0, NegSubInlineIntConst16:$src1)
>;

def : GCNPat<
  (i32 (zext (add i16:$src0, (i16 NegSubInlineIntConst16:$src1)))),
  (V_SUB_U16_e64 VSrc_b16:$src0, NegSubInlineIntConst16:$src1)
>;

defm : Arithmetic_i16_0Hi_Pats<add, V_ADD_U16_e64>;
defm : Arithmetic_i16_0Hi_Pats<mul, V_MUL_LO_U16_e64>;
defm : Arithmetic_i16_0Hi_Pats<sub, V_SUB_U16_e64>;
defm : Arithmetic_i16_0Hi_Pats<smin, V_MIN_I16_e64>;
defm : Arithmetic_i16_0Hi_Pats<smax, V_MAX_I16_e64>;
defm : Arithmetic_i16_0Hi_Pats<umin, V_MIN_U16_e64>;
defm : Arithmetic_i16_0Hi_Pats<umax, V_MAX_U16_e64>;
defm : Arithmetic_i16_0Hi_Pats<clshl_rev_16, V_LSHLREV_B16_e64>;
defm : Arithmetic_i16_0Hi_Pats<clshr_rev_16, V_LSHRREV_B16_e64>;
defm : Arithmetic_i16_0Hi_Pats<cashr_rev_16, V_ASHRREV_I16_e64>;

}  // End Predicates = [Has16BitInsts, isGFX8GFX9]

let Predicates = [Has16BitInsts] in {

def : ZExt_i16_i1_Pat<zext>;
def : ZExt_i16_i1_Pat<anyext>;

def : GCNPat <
  (i16 (sext i1:$src)),
  (V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                     /*src1mod*/(i32 0), /*src1*/(i32 -1), $src)
>;

} // End Predicates = [Has16BitInsts]


let SubtargetPredicate = HasIntClamp in {
// Set clamp bit for saturation.
def : VOPBinOpClampPat<uaddsat, V_ADD_CO_U32_e64, i32>;
def : VOPBinOpClampPat<usubsat, V_SUB_CO_U32_e64, i32>;
}

let SubtargetPredicate = HasAddNoCarryInsts, OtherPredicates = [HasIntClamp] in {
let AddedComplexity = 1 in { // Prefer over form with carry-out.
def : VOPBinOpClampPat<uaddsat, V_ADD_U32_e64, i32>;
def : VOPBinOpClampPat<usubsat, V_SUB_U32_e64, i32>;
}
}

let SubtargetPredicate = Has16BitInsts, OtherPredicates = [HasIntClamp] in {
def : VOPBinOpClampPat<uaddsat, V_ADD_U16_e64, i16>;
def : VOPBinOpClampPat<usubsat, V_SUB_U16_e64, i16>;
}

let SubtargetPredicate = isGFX12Plus, isReMaterializable = 1 in {
  let SchedRW = [WriteDoubleAdd], isCommutable = 1 in {
    let FPDPRounding = 1 in {
      defm V_ADD_F64_pseudo : VOP2Inst <"v_add_f64_pseudo", VOP_F64_F64_F64, any_fadd>;
      defm V_MUL_F64_pseudo : VOP2Inst <"v_mul_f64_pseudo", VOP_F64_F64_F64, fmul>;
    } // End FPDPRounding = 1
    defm V_MIN_NUM_F64 : VOP2Inst <"v_min_num_f64", VOP_F64_F64_F64, fminnum_like>;
    defm V_MAX_NUM_F64 : VOP2Inst <"v_max_num_f64", VOP_F64_F64_F64, fmaxnum_like>;
  } // End SchedRW = [WriteDoubleAdd], isCommutable = 1
  let SchedRW = [Write64Bit] in {
    defm V_LSHLREV_B64_pseudo : VOP2Inst <"v_lshlrev_b64_pseudo", VOP_I64_I32_I64, clshl_rev_64>;
  } // End SchedRW = [Write64Bit]
} // End SubtargetPredicate = isGFX12Plus, isReMaterializable = 1

//===----------------------------------------------------------------------===//
// DPP Encodings
//===----------------------------------------------------------------------===//

class VOP2_DPP<bits<6> op, VOP2_DPP_Pseudo ps,
               string opName = ps.OpName, VOPProfile p = ps.Pfl,
               bit IsDPP16 = 0> :
    VOP_DPP<opName, p, IsDPP16> {
  let hasSideEffects = ps.hasSideEffects;
  let Defs = ps.Defs;
  let SchedRW = ps.SchedRW;
  let Uses = ps.Uses;

  bits<8> vdst;
  bits<8> src1;
  let Inst{8-0}   = 0xfa;
  let Inst{16-9}  = !if(p.HasSrc1, src1{7-0}, 0);
  let Inst{24-17} = !if(p.EmitDst, vdst{7-0}, 0);
  let Inst{30-25} = op;
  let Inst{31}    = 0x0;
}

class Base_VOP2_DPP16<bits<6> op, VOP2_DPP_Pseudo ps,
                 string opName = ps.OpName, VOPProfile p = ps.Pfl> :
    VOP2_DPP<op, ps, opName, p, 1> {
  let AssemblerPredicate = HasDPP16;
  let SubtargetPredicate = ps.SubtargetPredicate;
  let OtherPredicates = ps.OtherPredicates;
}

class VOP2_DPP16<bits<6> op, VOP2_DPP_Pseudo ps, int subtarget,
                 string opName = ps.OpName, VOPProfile p = ps.Pfl> :
    Base_VOP2_DPP16<op, ps, opName, p>,
    SIMCInstr <ps.PseudoInstr, subtarget>;

class VOP2_DPP16_Gen<bits<6> op, VOP2_DPP_Pseudo ps, GFXGen Gen,
                 string opName = ps.OpName, VOPProfile p = ps.Pfl> :
    VOP2_DPP16<op, ps, Gen.Subtarget, opName, p> {
  let AssemblerPredicate = Gen.AssemblerPredicate;
  let True16Predicate = !if(ps.Pfl.IsRealTrue16, UseRealTrue16Insts, NoTrue16Predicate);
  let DecoderNamespace = Gen.DecoderNamespace#
                         !if(ps.Pfl.IsRealTrue16, "", "_FAKE16");
}

class VOP2_DPP8<bits<6> op, VOP2_Pseudo ps,
                VOPProfile p = ps.Pfl> :
    VOP_DPP8<ps.OpName, p> {
  let hasSideEffects = ps.hasSideEffects;
  let Defs = ps.Defs;
  let SchedRW = ps.SchedRW;
  let Uses = ps.Uses;

  bits<8> vdst;
  bits<8> src1;

  let Inst{8-0}   = fi;
  let Inst{16-9}  = !if(p.HasSrc1, src1{7-0}, 0);
  let Inst{24-17} = !if(p.EmitDst, vdst{7-0}, 0);
  let Inst{30-25} = op;
  let Inst{31}    = 0x0;

  let SubtargetPredicate = ps.SubtargetPredicate;
  let OtherPredicates = ps.OtherPredicates;
}

class VOP2_DPP8_Gen<bits<6> op, VOP2_Pseudo ps, GFXGen Gen,
                    VOPProfile p = ps.Pfl> :
    VOP2_DPP8<op, ps, p> {
  let AssemblerPredicate = Gen.AssemblerPredicate;
  let True16Predicate = !if(ps.Pfl.IsRealTrue16, UseRealTrue16Insts, NoTrue16Predicate);
  let DecoderNamespace = Gen.DecoderNamespace#
                         !if(ps.Pfl.IsRealTrue16, "", "_FAKE16");
}

//===----------------------------------------------------------------------===//
// GFX11, GFX12
//===----------------------------------------------------------------------===//

//===------------------------------- VOP2 -------------------------------===//
multiclass VOP2Only_Real_MADK<GFXGen Gen, bits<6> op> {
  def Gen.Suffix :
    VOP2_Real_Gen<!cast<VOP2_Pseudo>(NAME), Gen>,
    VOP2_MADKe<op{5-0}, !cast<VOP2_Pseudo>(NAME).Pfl>;
}

multiclass VOP2Only_Real_MADK_with_name<GFXGen Gen, bits<6> op, string asmName,
                                        string opName = NAME> {
  def Gen.Suffix :
      VOP2_Real_Gen<!cast<VOP2_Pseudo>(opName), Gen>,
      VOP2_MADKe<op{5-0}, !cast<VOP2_Pseudo>(opName).Pfl> {
    VOP2_Pseudo ps = !cast<VOP2_Pseudo>(opName);
    let AsmString = asmName # ps.AsmOperands;
  }
}

multiclass VOP2_Real_e32<GFXGen Gen, bits<6> op> {
  def _e32#Gen.Suffix :
    VOP2_Real_Gen<!cast<VOP2_Pseudo>(NAME#"_e32"), Gen>,
    VOP2e<op{5-0}, !cast<VOP2_Pseudo>(NAME#"_e32").Pfl>;
}

multiclass VOP2Only_Real_e32<GFXGen Gen, bits<6> op> {
  let IsSingle = 1 in
    defm NAME: VOP2_Real_e32<Gen, op>;
}

multiclass VOP2_Real_e64<GFXGen Gen, bits<6> op> {
  def _e64#Gen.Suffix :
    VOP3_Real_Gen<!cast<VOP3_Pseudo>(NAME#"_e64"), Gen>,
    VOP3e_gfx11_gfx12<{0, 1, 0, 0, op{5-0}}, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
}

multiclass VOP2_Real_dpp<GFXGen Gen, bits<6> op> {
  if !cast<VOP2_Pseudo>(NAME#"_e32").Pfl.HasExtDPP then
  def _dpp#Gen.Suffix : VOP2_DPP16_Gen<op, !cast<VOP2_DPP_Pseudo>(NAME#"_dpp"), Gen>;
}

multiclass VOP2_Real_dpp8<GFXGen Gen, bits<6> op> {
  if !cast<VOP2_Pseudo>(NAME#"_e32").Pfl.HasExtDPP then
  def _dpp8#Gen.Suffix : VOP2_DPP8_Gen<op, !cast<VOP2_Pseudo>(NAME#"_e32"), Gen>;
}

//===------------------------- VOP2 (with name) -------------------------===//
multiclass VOP2_Real_e32_with_name<GFXGen Gen, bits<6> op, string opName,
                                   string asmName, bit single = 0> {
  defvar ps = !cast<VOP2_Pseudo>(opName#"_e32");
  def _e32#Gen.Suffix :
    VOP2_Real_Gen<ps, Gen, asmName>,
    VOP2e<op{5-0}, ps.Pfl> {
      let AsmString = asmName # ps.AsmOperands;
      let IsSingle = single;
    }
}
multiclass VOP2_Real_e64_with_name<GFXGen Gen, bits<6> op, string opName,
                                   string asmName> {
  defvar ps = !cast<VOP3_Pseudo>(opName#"_e64");
  def _e64#Gen.Suffix :
    VOP3_Real_Gen<ps, Gen>,
    VOP3e_gfx11_gfx12<{0, 1, 0, 0, op{5-0}}, ps.Pfl> {
      let AsmString = asmName # ps.AsmOperands;
    }
}

multiclass VOP2_Real_dpp_with_name<GFXGen Gen, bits<6> op, string opName,
                                   string asmName> {
  defvar ps = !cast<VOP2_Pseudo>(opName#"_e32");
  if ps.Pfl.HasExtDPP then
  def _dpp#Gen.Suffix : VOP2_DPP16_Gen<op, !cast<VOP2_DPP_Pseudo>(opName#"_dpp"), Gen> {
    let AsmString = asmName # ps.Pfl.AsmDPP16;
  }
}
multiclass VOP2_Real_dpp8_with_name<GFXGen Gen, bits<6> op, string opName,
                                    string asmName> {
  defvar ps = !cast<VOP2_Pseudo>(opName#"_e32");
  if ps.Pfl.HasExtDPP then
  def _dpp8#Gen.Suffix : VOP2_DPP8_Gen<op, ps, Gen> {
    let AsmString = asmName # ps.Pfl.AsmDPP8;
  }
}

//===------------------------------ VOP2be ------------------------------===//
multiclass VOP2be_Real_e32<GFXGen Gen, bits<6> op, string opName, string asmName> {
  defvar ps = !cast<VOP2_Pseudo>(opName#"_e32");
  def _e32#Gen.Suffix :
    VOP2_Real_Gen<ps, Gen>,
    VOP2e<op{5-0}, ps.Pfl> {
      let AsmString = asmName # !subst(", vcc", "", ps.AsmOperands);
    }
}
multiclass VOP2be_Real_dpp<GFXGen Gen, bits<6> op, string opName, string asmName> {
  if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExtDPP then
  def _dpp#Gen.Suffix :
    VOP2_DPP16_Gen<op, !cast<VOP2_DPP_Pseudo>(opName#"_dpp"), Gen, asmName> {
      string AsmDPP = !cast<VOP2_Pseudo>(opName#"_e32").Pfl.AsmDPP16;
      let AsmString = asmName # !subst(", vcc", "", AsmDPP);
    }
  if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExtDPP then
  def _dpp_w32#Gen.Suffix :
    Base_VOP2_DPP16<op, !cast<VOP2_DPP_Pseudo>(opName#"_dpp"), asmName> {
      string AsmDPP = !cast<VOP2_Pseudo>(opName#"_e32").Pfl.AsmDPP16;
      let AsmString = asmName # !subst("vcc", "vcc_lo", AsmDPP);
      let isAsmParserOnly = 1;
      let WaveSizePredicate = isWave32;
      let AssemblerPredicate = Gen.AssemblerPredicate;
      let DecoderNamespace = Gen.DecoderNamespace;
    }
  if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExtDPP then
  def _dpp_w64#Gen.Suffix :
    Base_VOP2_DPP16<op, !cast<VOP2_DPP_Pseudo>(opName#"_dpp"), asmName> {
      string AsmDPP = !cast<VOP2_Pseudo>(opName#"_e32").Pfl.AsmDPP16;
      let AsmString = asmName # AsmDPP;
      let isAsmParserOnly = 1;
      let WaveSizePredicate = isWave64;
      let AssemblerPredicate = Gen.AssemblerPredicate;
      let DecoderNamespace = Gen.DecoderNamespace;
    }
}
multiclass VOP2be_Real_dpp8<GFXGen Gen, bits<6> op, string opName, string asmName> {
  if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExtDPP then
  def _dpp8#Gen.Suffix :
    VOP2_DPP8_Gen<op, !cast<VOP2_Pseudo>(opName#"_e32"), Gen> {
      string AsmDPP8 = !cast<VOP2_Pseudo>(opName#"_e32").Pfl.AsmDPP8;
      let AsmString = asmName # !subst(", vcc", "", AsmDPP8);
    }
  if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExtDPP then
  def _dpp8_w32#Gen.Suffix :
    VOP2_DPP8<op, !cast<VOP2_Pseudo>(opName#"_e32")> {
      string AsmDPP8 = !cast<VOP2_Pseudo>(opName#"_e32").Pfl.AsmDPP8;
      let AsmString = asmName # !subst("vcc", "vcc_lo", AsmDPP8);
      let isAsmParserOnly = 1;
      let WaveSizePredicate = isWave32;
      let AssemblerPredicate = Gen.AssemblerPredicate;
      let DecoderNamespace = Gen.DecoderNamespace;
    }
  if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExtDPP then
  def _dpp8_w64#Gen.Suffix :
    VOP2_DPP8<op, !cast<VOP2_Pseudo>(opName#"_e32")> {
      string AsmDPP8 = !cast<VOP2_Pseudo>(opName#"_e32").Pfl.AsmDPP8;
      let AsmString = asmName # AsmDPP8;
      let isAsmParserOnly = 1;
      let WaveSizePredicate = isWave64;
      let AssemblerPredicate = Gen.AssemblerPredicate;
      let DecoderNamespace = Gen.DecoderNamespace;
    }
}

// We don't want to override separate decoderNamespaces within these
multiclass VOP2_Realtriple_e64<GFXGen Gen, bits<6> op> {
  defm NAME : VOP3_Realtriple<Gen, {0, 1, 0, 0, op{5-0}}, /*isSingle=*/ 0, NAME> ;
}

multiclass VOP2_Realtriple_e64_with_name<GFXGen Gen, bits<6> op, string opName,
                                               string asmName> {
  defm NAME : VOP3_Realtriple_with_name<Gen, {0, 1, 0, 0, op{5-0}}, opName, asmName> ;
}

multiclass VOP2be_Real<GFXGen Gen, bits<6> op, string opName, string asmName> :
  VOP2be_Real_e32<Gen, op, opName, asmName>,
  VOP3be_Realtriple<Gen, {0, 1, 0, 0, op{5-0}}, /*isSingle=*/ 0, opName, asmName>,
  VOP2be_Real_dpp<Gen, op, opName, asmName>,
  VOP2be_Real_dpp8<Gen, op, opName, asmName>;

// Only for CNDMASK
multiclass VOP2e_Real<GFXGen Gen, bits<6> op, string opName, string asmName> :
  VOP2_Real_e32<Gen, op>,
  VOP2_Realtriple_e64<Gen, op>,
  VOP2be_Real_dpp<Gen, op, opName, asmName>,
  VOP2be_Real_dpp8<Gen, op, opName, asmName>;

multiclass VOP2Only_Real<GFXGen Gen, bits<6> op> :
  VOP2Only_Real_e32<Gen, op>,
  VOP2_Real_dpp<Gen, op>,
  VOP2_Real_dpp8<Gen, op>;

multiclass VOP2_Real_FULL<GFXGen Gen, bits<6> op> :
  VOP2_Realtriple_e64<Gen, op>,
  VOP2_Real_e32<Gen, op>,
  VOP2_Real_dpp<Gen, op>,
  VOP2_Real_dpp8<Gen, op>;

multiclass VOP2_Real_NO_VOP3_with_name<GFXGen Gen, bits<6> op, string opName,
                                       string asmName, bit isSingle = 0> {
  defm NAME : VOP2_Real_e32_with_name<Gen, op, opName, asmName, isSingle>,
              VOP2_Real_dpp_with_name<Gen, op, opName, asmName>,
              VOP2_Real_dpp8_with_name<Gen, op, opName, asmName>;
  defvar ps = !cast<VOP2_Pseudo>(opName#"_e32");
  def Gen.Suffix#"_alias" : AMDGPUMnemonicAlias<ps.Mnemonic, asmName> {
    let AssemblerPredicate = Gen.AssemblerPredicate;
  }
}

multiclass VOP2_Real_FULL_with_name<GFXGen Gen, bits<6> op, string opName,
                                    string asmName> :
  VOP2_Realtriple_e64_with_name<Gen, op, opName, asmName>,
  VOP2_Real_NO_VOP3_with_name<Gen, op, opName, asmName>;

multiclass VOP2_Real_NO_DPP_with_name<GFXGen Gen, bits<6> op, string opName,
                                      string asmName> {
  defm NAME : VOP2_Real_e32_with_name<Gen, op, opName, asmName>,
              VOP2_Real_e64_with_name<Gen, op, opName, asmName>;
  defvar ps = !cast<VOP2_Pseudo>(opName#"_e32");
  def Gen.Suffix#"_alias" : AMDGPUMnemonicAlias<ps.Mnemonic, asmName> {
    let AssemblerPredicate = Gen.AssemblerPredicate;
  }
}

multiclass VOP2_Real_NO_DPP_with_alias<GFXGen Gen, bits<6> op, string alias> {
  defm NAME : VOP2_Real_e32<Gen, op>,
              VOP2_Real_e64<Gen, op>;
  def Gen.Suffix#"_alias" : AMDGPUMnemonicAlias<alias, NAME> {
    let AssemblerPredicate = Gen.AssemblerPredicate;
  }
}

//===----------------------------------------------------------------------===//
// GFX12.
//===----------------------------------------------------------------------===//

multiclass VOP2be_Real_gfx12<bits<6> op, string opName, string asmName> :
  VOP2be_Real<GFX12Gen, op, opName, asmName>;

// Only for CNDMASK
multiclass VOP2e_Real_gfx12<bits<6> op, string opName, string asmName> :
  VOP2e_Real<GFX12Gen, op, opName, asmName>;

multiclass VOP2_Real_FULL_with_name_gfx12<bits<6> op, string opName,
                                          string asmName> :
  VOP2_Real_FULL_with_name<GFX12Gen, op, opName, asmName>;

multiclass VOP2_Real_FULL_t16_with_name_gfx12<bits<6> op, string opName,
                                              string asmName, string alias> {
  defm NAME : VOP2_Real_FULL_with_name<GFX12Gen, op, opName, asmName>;
  def _gfx12_2nd_alias : AMDGPUMnemonicAlias<alias, asmName> {
    let AssemblerPredicate = isGFX12Only;
  }
}

multiclass VOP2_Real_NO_DPP_with_name_gfx12<bits<6> op, string opName,
                                            string asmName> :
  VOP2_Real_NO_DPP_with_name<GFX12Gen, op, opName, asmName>;

multiclass VOP2_Real_NO_DPP_with_alias_gfx12<bits<6> op, string alias> :
  VOP2_Real_NO_DPP_with_alias<GFX12Gen, op, alias>;

defm V_ADD_F64     : VOP2_Real_NO_DPP_with_name_gfx12<0x002, "V_ADD_F64_pseudo", "v_add_f64">;
defm V_MUL_F64     : VOP2_Real_NO_DPP_with_name_gfx12<0x006, "V_MUL_F64_pseudo", "v_mul_f64">;
defm V_LSHLREV_B64 : VOP2_Real_NO_DPP_with_name_gfx12<0x01f, "V_LSHLREV_B64_pseudo", "v_lshlrev_b64">;
defm V_MIN_NUM_F64 : VOP2_Real_NO_DPP_with_alias_gfx12<0x00d, "v_min_f64">;
defm V_MAX_NUM_F64 : VOP2_Real_NO_DPP_with_alias_gfx12<0x00e, "v_max_f64">;

defm V_CNDMASK_B32 : VOP2e_Real_gfx12<0x001, "V_CNDMASK_B32", "v_cndmask_b32">;
defm V_ADD_CO_CI_U32 :
  VOP2be_Real_gfx12<0x020, "V_ADDC_U32", "v_add_co_ci_u32">;
defm V_SUB_CO_CI_U32 :
  VOP2be_Real_gfx12<0x021, "V_SUBB_U32", "v_sub_co_ci_u32">;
defm V_SUBREV_CO_CI_U32 :
  VOP2be_Real_gfx12<0x022, "V_SUBBREV_U32", "v_subrev_co_ci_u32">;

defm V_MIN_NUM_F32 : VOP2_Real_FULL_with_name_gfx12<0x015, "V_MIN_F32", "v_min_num_f32">;
defm V_MAX_NUM_F32 : VOP2_Real_FULL_with_name_gfx12<0x016, "V_MAX_F32", "v_max_num_f32">;
defm V_MIN_NUM_F16 : VOP2_Real_FULL_t16_with_name_gfx12<0x030, "V_MIN_F16_t16", "v_min_num_f16", "v_min_f16">;
defm V_MIN_NUM_F16_fake16 : VOP2_Real_FULL_t16_with_name_gfx12<0x030, "V_MIN_F16_fake16", "v_min_num_f16", "v_min_f16">;
defm V_MAX_NUM_F16 : VOP2_Real_FULL_t16_with_name_gfx12<0x031, "V_MAX_F16_t16", "v_max_num_f16", "v_max_f16">;
defm V_MAX_NUM_F16_fake16 : VOP2_Real_FULL_t16_with_name_gfx12<0x031, "V_MAX_F16_fake16", "v_max_num_f16", "v_max_f16">;

let SubtargetPredicate = isGFX12Plus in {
  defm : VOP2eInstAliases<V_CNDMASK_B32_e32, V_CNDMASK_B32_e32_gfx12>;

  defm : VOP2bInstAliases<
    V_ADDC_U32_e32, V_ADD_CO_CI_U32_e32_gfx12, "v_add_co_ci_u32">;
  defm : VOP2bInstAliases<
    V_SUBB_U32_e32, V_SUB_CO_CI_U32_e32_gfx12, "v_sub_co_ci_u32">;
  defm : VOP2bInstAliases<
    V_SUBBREV_U32_e32, V_SUBREV_CO_CI_U32_e32_gfx12, "v_subrev_co_ci_u32">;
} // End SubtargetPredicate = isGFX12Plus

//===----------------------------------------------------------------------===//
// GFX11.
//===----------------------------------------------------------------------===//

multiclass VOP2be_Real_gfx11<bits<6> op, string opName, string asmName> :
  VOP2be_Real<GFX11Gen, op, opName, asmName>;

// Only for CNDMASK
multiclass VOP2e_Real_gfx11<bits<6> op, string opName, string asmName> :
  VOP2e_Real<GFX11Gen, op, opName, asmName>;

multiclass VOP2_Real_NO_VOP3_with_name_gfx11<bits<6> op, string opName,
                                           string asmName, bit isSingle = 0> {
  defm NAME : VOP2_Real_e32_with_name<GFX11Gen, op, opName, asmName, isSingle>,
              VOP2_Real_dpp_with_name<GFX11Gen, op, opName, asmName>,
              VOP2_Real_dpp8_with_name<GFX11Gen, op, opName, asmName>;
  defvar ps = !cast<VOP2_Pseudo>(opName#"_e32");
  def _gfx11_alias : AMDGPUMnemonicAlias<ps.Mnemonic, asmName> {
    let AssemblerPredicate = isGFX11Only;
  }
}

multiclass VOP2_Real_NO_DPP_with_name_gfx11<bits<6> op, string opName,
                                           string asmName> :
  VOP2_Real_NO_DPP_with_name<GFX11Gen, op, opName, asmName>;

multiclass VOP2_Real_FULL_gfx11_gfx12<bits<6> op> :
  VOP2_Real_FULL<GFX11Gen, op>, VOP2_Real_FULL<GFX12Gen, op>;

multiclass VOP2_Real_FULL_with_name_gfx11_gfx12<bits<6> op, string opName,
                                                string asmName> :
  VOP2_Real_FULL_with_name<GFX11Gen, op, opName, asmName>,
  VOP2_Real_FULL_with_name<GFX12Gen, op, opName, asmName>;

multiclass VOP2_Real_e32_gfx11_gfx12<bits<6> op> :
  VOP2Only_Real<GFX11Gen, op>, VOP2Only_Real<GFX12Gen, op>;

multiclass VOP3Only_Realtriple_gfx11_gfx12<bits<10> op> :
  VOP3Only_Realtriple<GFX11Gen, op>, VOP3Only_Realtriple<GFX12Gen, op>;

multiclass VOP3Only_Realtriple_t16_gfx11_gfx12<bits<10> op, string asmName> :
  VOP3Only_Realtriple_t16<GFX11Gen, op, asmName>,
  VOP3Only_Realtriple_t16<GFX12Gen, op, asmName>;

multiclass VOP3beOnly_Realtriple_gfx11_gfx12<bits<10> op> :
  VOP3beOnly_Realtriple<GFX11Gen, op>, VOP3beOnly_Realtriple<GFX12Gen, op>;

multiclass VOP2Only_Real_MADK_with_name_gfx11_gfx12<bits<6> op, string asmName,
                                                    string opName = NAME> :
  VOP2Only_Real_MADK_with_name<GFX11Gen, op, asmName, opName>,
  VOP2Only_Real_MADK_with_name<GFX12Gen, op, asmName, opName>;

multiclass VOP2_Real_FULL_t16_gfx11<bits<6> op, string asmName,
                                    string opName = NAME> :
  VOP2_Real_FULL_with_name<GFX11Gen, op, opName, asmName>;

multiclass VOP2_Real_FULL_t16_gfx11_gfx12<bits<6> op, string asmName,
                                          string opName = NAME> :
  VOP2_Real_FULL_with_name_gfx11_gfx12<op, opName, asmName>;

multiclass VOP2_Real_FULL_gfx11<bits<6> op> :
  VOP2_Real_FULL<GFX11Gen, op>;

defm V_CNDMASK_B32 : VOP2e_Real_gfx11<0x001, "V_CNDMASK_B32",
  "v_cndmask_b32">;
defm V_DOT2ACC_F32_F16 : VOP2_Real_NO_VOP3_with_name_gfx11<0x002,
  "V_DOT2C_F32_F16", "v_dot2acc_f32_f16", 1>;
defm V_FMAC_DX9_ZERO_F32 : VOP2_Real_NO_DPP_with_name_gfx11<0x006,
  "V_FMAC_LEGACY_F32", "v_fmac_dx9_zero_f32">;
defm V_MUL_DX9_ZERO_F32 : VOP2_Real_FULL_with_name_gfx11_gfx12<0x007,
  "V_MUL_LEGACY_F32", "v_mul_dx9_zero_f32">;
defm V_LSHLREV_B32        : VOP2_Real_FULL_gfx11_gfx12<0x018>;
defm V_LSHRREV_B32        : VOP2_Real_FULL_gfx11_gfx12<0x019>;
defm V_ASHRREV_I32        : VOP2_Real_FULL_gfx11_gfx12<0x01a>;
defm V_ADD_CO_CI_U32 :
  VOP2be_Real_gfx11<0x020, "V_ADDC_U32", "v_add_co_ci_u32">;
defm V_SUB_CO_CI_U32 :
  VOP2be_Real_gfx11<0x021, "V_SUBB_U32", "v_sub_co_ci_u32">;
defm V_SUBREV_CO_CI_U32 :
  VOP2be_Real_gfx11<0x022, "V_SUBBREV_U32", "v_subrev_co_ci_u32">;

defm V_CVT_PK_RTZ_F16_F32  : VOP2_Real_FULL_with_name_gfx11_gfx12<0x02f,
  "V_CVT_PKRTZ_F16_F32", "v_cvt_pk_rtz_f16_f32">;
defm V_PK_FMAC_F16     : VOP2_Real_e32_gfx11_gfx12<0x03c>;

defm V_ADD_F16_t16         : VOP2_Real_FULL_t16_gfx11_gfx12<0x032, "v_add_f16">;
defm V_ADD_F16_fake16      : VOP2_Real_FULL_t16_gfx11_gfx12<0x032, "v_add_f16">;
defm V_SUB_F16_t16         : VOP2_Real_FULL_t16_gfx11_gfx12<0x033, "v_sub_f16">;
defm V_SUB_F16_fake16      : VOP2_Real_FULL_t16_gfx11_gfx12<0x033, "v_sub_f16">;
defm V_SUBREV_F16_t16      : VOP2_Real_FULL_t16_gfx11_gfx12<0x034, "v_subrev_f16">;
defm V_SUBREV_F16_fake16   : VOP2_Real_FULL_t16_gfx11_gfx12<0x034, "v_subrev_f16">;
defm V_MUL_F16_t16         : VOP2_Real_FULL_t16_gfx11_gfx12<0x035, "v_mul_f16">;
defm V_MUL_F16_fake16      : VOP2_Real_FULL_t16_gfx11_gfx12<0x035, "v_mul_f16">;
defm V_FMAC_F16_t16        : VOP2_Real_FULL_t16_gfx11_gfx12<0x036, "v_fmac_f16">;
defm V_LDEXP_F16_t16       : VOP2_Real_FULL_t16_gfx11_gfx12<0x03b, "v_ldexp_f16">;
defm V_MAX_F16_t16         : VOP2_Real_FULL_t16_gfx11<0x039, "v_max_f16">;
defm V_MAX_F16_fake16      : VOP2_Real_FULL_t16_gfx11<0x039, "v_max_f16">;
defm V_MIN_F16_t16         : VOP2_Real_FULL_t16_gfx11<0x03a, "v_min_f16">;
defm V_MIN_F16_fake16      : VOP2_Real_FULL_t16_gfx11<0x03a, "v_min_f16">;
defm V_FMAMK_F16_t16       : VOP2Only_Real_MADK_with_name_gfx11_gfx12<0x037, "v_fmamk_f16">;
defm V_FMAAK_F16_t16       : VOP2Only_Real_MADK_with_name_gfx11_gfx12<0x038, "v_fmaak_f16">;

// VOP3 only.
defm V_CNDMASK_B16         : VOP3Only_Realtriple_gfx11_gfx12<0x25d>;
defm V_LDEXP_F32           : VOP3Only_Realtriple_gfx11_gfx12<0x31c>;
defm V_BFM_B32             : VOP3Only_Realtriple_gfx11_gfx12<0x31d>;
defm V_BCNT_U32_B32        : VOP3Only_Realtriple_gfx11_gfx12<0x31e>;
defm V_MBCNT_LO_U32_B32    : VOP3Only_Realtriple_gfx11_gfx12<0x31f>;
defm V_MBCNT_HI_U32_B32    : VOP3Only_Realtriple_gfx11_gfx12<0x320>;
defm V_CVT_PK_NORM_I16_F32 : VOP3Only_Realtriple_with_name_gfx11_gfx12<0x321, "V_CVT_PKNORM_I16_F32", "v_cvt_pk_norm_i16_f32">;
defm V_CVT_PK_NORM_U16_F32 : VOP3Only_Realtriple_with_name_gfx11_gfx12<0x322, "V_CVT_PKNORM_U16_F32", "v_cvt_pk_norm_u16_f32">;
defm V_CVT_PK_U16_U32      : VOP3Only_Realtriple_gfx11_gfx12<0x323>;
defm V_CVT_PK_I16_I32      : VOP3Only_Realtriple_gfx11_gfx12<0x324>;
defm V_ADD_CO_U32          : VOP3beOnly_Realtriple_gfx11_gfx12<0x300>;
defm V_SUB_CO_U32          : VOP3beOnly_Realtriple_gfx11_gfx12<0x301>;
defm V_SUBREV_CO_U32       : VOP3beOnly_Realtriple_gfx11_gfx12<0x302>;

let SubtargetPredicate = isGFX11Only in {
  defm : VOP2eInstAliases<V_CNDMASK_B32_e32, V_CNDMASK_B32_e32_gfx11>;

  defm : VOP2bInstAliases<
    V_ADDC_U32_e32, V_ADD_CO_CI_U32_e32_gfx11, "v_add_co_ci_u32">;
  defm : VOP2bInstAliases<
    V_SUBB_U32_e32, V_SUB_CO_CI_U32_e32_gfx11, "v_sub_co_ci_u32">;
  defm : VOP2bInstAliases<
    V_SUBBREV_U32_e32, V_SUBREV_CO_CI_U32_e32_gfx11, "v_subrev_co_ci_u32">;
} // End SubtargetPredicate = isGFX11Only

//===----------------------------------------------------------------------===//
// GFX10.
//===----------------------------------------------------------------------===//

let AssemblerPredicate = isGFX10Only, DecoderNamespace = "GFX10" in {
  //===------------------------------- VOP2 -------------------------------===//
  multiclass VOP2Only_Real_MADK_gfx10<bits<6> op> {
    def _gfx10 :
      VOP2_Real<!cast<VOP2_Pseudo>(NAME), SIEncodingFamily.GFX10>,
      VOP2_MADKe<op{5-0}, !cast<VOP2_Pseudo>(NAME).Pfl>;
  }
  multiclass VOP2Only_Real_MADK_gfx10_with_name<bits<6> op, string opName,
                                                string asmName> {
    def _gfx10 :
        VOP2_Real<!cast<VOP2_Pseudo>(opName), SIEncodingFamily.GFX10>,
        VOP2_MADKe<op{5-0}, !cast<VOP2_Pseudo>(opName).Pfl> {
      VOP2_Pseudo ps = !cast<VOP2_Pseudo>(opName);
      let AsmString = asmName # ps.AsmOperands;
    }
  }
  multiclass VOP2_Real_e32_gfx10<bits<6> op> {
    def _e32_gfx10 :
      VOP2_Real<!cast<VOP2_Pseudo>(NAME#"_e32"), SIEncodingFamily.GFX10>,
      VOP2e<op{5-0}, !cast<VOP2_Pseudo>(NAME#"_e32").Pfl>;
  }
  multiclass VOP2_Real_e64_gfx10<bits<6> op> {
    def _e64_gfx10 :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.GFX10>,
      VOP3e_gfx10<{0, 1, 0, 0, op{5-0}}, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
  }
  multiclass VOP2_Real_sdwa_gfx10<bits<6> op> {
    if !cast<VOP2_Pseudo>(NAME#"_e32").Pfl.HasExtSDWA9 then
    def _sdwa_gfx10 :
      VOP_SDWA10_Real<!cast<VOP2_SDWA_Pseudo>(NAME#"_sdwa")>,
      VOP2_SDWA9Ae<op{5-0}, !cast<VOP2_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;
  }
  multiclass VOP2_Real_dpp_gfx10<bits<6> op> {
    if !cast<VOP2_Pseudo>(NAME#"_e32").Pfl.HasExt32BitDPP then
    def _dpp_gfx10 : VOP2_DPP16<op, !cast<VOP2_DPP_Pseudo>(NAME#"_dpp"), SIEncodingFamily.GFX10>;
  }
  multiclass VOP2_Real_dpp8_gfx10<bits<6> op> {
    if !cast<VOP2_Pseudo>(NAME#"_e32").Pfl.HasExt32BitDPP then
    def _dpp8_gfx10 : VOP2_DPP8<op, !cast<VOP2_Pseudo>(NAME#"_e32")>;
  }

  //===------------------------- VOP2 (with name) -------------------------===//
  multiclass VOP2_Real_e32_gfx10_with_name<bits<6> op, string opName,
                                           string asmName> {
    def _e32_gfx10 :
      VOP2_Real<!cast<VOP2_Pseudo>(opName#"_e32"), SIEncodingFamily.GFX10>,
      VOP2e<op{5-0}, !cast<VOP2_Pseudo>(opName#"_e32").Pfl> {
        VOP2_Pseudo ps = !cast<VOP2_Pseudo>(opName#"_e32");
        let AsmString = asmName # ps.AsmOperands;
      }
  }
  multiclass VOP2_Real_e64_gfx10_with_name<bits<6> op, string opName,
                                           string asmName> {
    def _e64_gfx10 :
      VOP3_Real<!cast<VOP3_Pseudo>(opName#"_e64"), SIEncodingFamily.GFX10>,
      VOP3e_gfx10<{0, 1, 0, 0, op{5-0}},
                  !cast<VOP3_Pseudo>(opName#"_e64").Pfl> {
        VOP3_Pseudo ps = !cast<VOP3_Pseudo>(opName#"_e64");
        let AsmString = asmName # ps.AsmOperands;
      }
  }
  multiclass VOP2_Real_sdwa_gfx10_with_name<bits<6> op, string opName,
                                            string asmName> {
    if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExtSDWA9 then
    def _sdwa_gfx10 :
      VOP_SDWA10_Real<!cast<VOP2_SDWA_Pseudo>(opName#"_sdwa")>,
      VOP2_SDWA9Ae<op{5-0}, !cast<VOP2_SDWA_Pseudo>(opName#"_sdwa").Pfl> {
        VOP2_SDWA_Pseudo ps = !cast<VOP2_SDWA_Pseudo>(opName#"_sdwa");
        let AsmString = asmName # ps.AsmOperands;
      }
  }
  multiclass VOP2_Real_dpp_gfx10_with_name<bits<6> op, string opName,
                                           string asmName> {
    if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExt32BitDPP then
    def _dpp_gfx10 : VOP2_DPP16<op, !cast<VOP2_DPP_Pseudo>(opName#"_dpp"), SIEncodingFamily.GFX10> {
      VOP2_Pseudo ps = !cast<VOP2_Pseudo>(opName#"_e32");
      let AsmString = asmName # ps.Pfl.AsmDPP16;
    }
  }
  multiclass VOP2_Real_dpp8_gfx10_with_name<bits<6> op, string opName,
                                            string asmName> {
    if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExt32BitDPP then
    def _dpp8_gfx10 : VOP2_DPP8<op, !cast<VOP2_Pseudo>(opName#"_e32")> {
      VOP2_Pseudo ps = !cast<VOP2_Pseudo>(opName#"_e32");
      let AsmString = asmName # ps.Pfl.AsmDPP8;
    }
  }

  //===------------------------------ VOP2be ------------------------------===//
  multiclass VOP2be_Real_e32_gfx10<bits<6> op, string opName, string asmName> {
    def _e32_gfx10 :
      VOP2_Real<!cast<VOP2_Pseudo>(opName#"_e32"), SIEncodingFamily.GFX10>,
      VOP2e<op{5-0}, !cast<VOP2_Pseudo>(opName#"_e32").Pfl> {
        VOP2_Pseudo Ps = !cast<VOP2_Pseudo>(opName#"_e32");
        let AsmString = asmName # !subst(", vcc", "", Ps.AsmOperands);
      }
  }
  multiclass VOP2be_Real_e64_gfx10<bits<6> op, string opName, string asmName> {
    def _e64_gfx10 :
      VOP3_Real<!cast<VOP3_Pseudo>(opName#"_e64"), SIEncodingFamily.GFX10>,
      VOP3be_gfx10<{0, 1, 0, 0, op{5-0}},
                   !cast<VOP3_Pseudo>(opName#"_e64").Pfl> {
        VOP3_Pseudo Ps = !cast<VOP3_Pseudo>(opName#"_e64");
        let AsmString = asmName # Ps.AsmOperands;
      }
  }
  multiclass VOP2be_Real_sdwa_gfx10<bits<6> op, string opName, string asmName> {
    if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExtSDWA9 then
    def _sdwa_gfx10 :
      VOP_SDWA10_Real<!cast<VOP2_SDWA_Pseudo>(opName#"_sdwa")>,
      VOP2_SDWA9Ae<op{5-0}, !cast<VOP2_SDWA_Pseudo>(opName#"_sdwa").Pfl> {
        VOP2_SDWA_Pseudo Ps = !cast<VOP2_SDWA_Pseudo>(opName#"_sdwa");
        let AsmString = asmName # !subst(", vcc", "", Ps.AsmOperands);
      }
    if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExtSDWA9 then
    def _sdwa_w32_gfx10 :
      Base_VOP_SDWA10_Real<!cast<VOP2_SDWA_Pseudo>(opName#"_sdwa")>,
      VOP2_SDWA9Ae<op{5-0}, !cast<VOP2_SDWA_Pseudo>(opName#"_sdwa").Pfl> {
        VOP2_SDWA_Pseudo Ps = !cast<VOP2_SDWA_Pseudo>(opName#"_sdwa");
        let AsmString = asmName # !subst("vcc", "vcc_lo", Ps.AsmOperands);
        let isAsmParserOnly = 1;
        let WaveSizePredicate = isWave32;
     }
    if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExtSDWA9 then
    def _sdwa_w64_gfx10 :
      Base_VOP_SDWA10_Real<!cast<VOP2_SDWA_Pseudo>(opName#"_sdwa")>,
      VOP2_SDWA9Ae<op{5-0}, !cast<VOP2_SDWA_Pseudo>(opName#"_sdwa").Pfl> {
        VOP2_SDWA_Pseudo Ps = !cast<VOP2_SDWA_Pseudo>(opName#"_sdwa");
        let AsmString = asmName # Ps.AsmOperands;
        let isAsmParserOnly = 1;
        let WaveSizePredicate = isWave64;
      }
  }
  multiclass VOP2be_Real_dpp_gfx10<bits<6> op, string opName, string asmName> {
    if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExt32BitDPP then
    def _dpp_gfx10 :
      VOP2_DPP16<op, !cast<VOP2_DPP_Pseudo>(opName#"_dpp"), SIEncodingFamily.GFX10, asmName> {
        string AsmDPP = !cast<VOP2_Pseudo>(opName#"_e32").Pfl.AsmDPP16;
        let AsmString = asmName # !subst(", vcc", "", AsmDPP);
      }
    if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExt32BitDPP then
    def _dpp_w32_gfx10 :
      Base_VOP2_DPP16<op, !cast<VOP2_DPP_Pseudo>(opName#"_dpp"), asmName> {
        string AsmDPP = !cast<VOP2_Pseudo>(opName#"_e32").Pfl.AsmDPP16;
        let AsmString = asmName # !subst("vcc", "vcc_lo", AsmDPP);
        let isAsmParserOnly = 1;
        let WaveSizePredicate = isWave32;
      }
    if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExt32BitDPP then
    def _dpp_w64_gfx10 :
      Base_VOP2_DPP16<op, !cast<VOP2_DPP_Pseudo>(opName#"_dpp"), asmName> {
        string AsmDPP = !cast<VOP2_Pseudo>(opName#"_e32").Pfl.AsmDPP16;
        let AsmString = asmName # AsmDPP;
        let isAsmParserOnly = 1;
        let WaveSizePredicate = isWave64;
      }
  }
  multiclass VOP2be_Real_dpp8_gfx10<bits<6> op, string opName, string asmName> {
    if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExt32BitDPP then
    def _dpp8_gfx10 :
      VOP2_DPP8<op, !cast<VOP2_Pseudo>(opName#"_e32")> {
        string AsmDPP8 = !cast<VOP2_Pseudo>(opName#"_e32").Pfl.AsmDPP8;
        let AsmString = asmName # !subst(", vcc", "", AsmDPP8);
      }
    if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExt32BitDPP then
    def _dpp8_w32_gfx10 :
      VOP2_DPP8<op, !cast<VOP2_Pseudo>(opName#"_e32")> {
        string AsmDPP8 = !cast<VOP2_Pseudo>(opName#"_e32").Pfl.AsmDPP8;
        let AsmString = asmName # !subst("vcc", "vcc_lo", AsmDPP8);
        let isAsmParserOnly = 1;
        let WaveSizePredicate = isWave32;
      }
    if !cast<VOP2_Pseudo>(opName#"_e32").Pfl.HasExt32BitDPP then
    def _dpp8_w64_gfx10 :
      VOP2_DPP8<op, !cast<VOP2_Pseudo>(opName#"_e32")> {
        string AsmDPP8 = !cast<VOP2_Pseudo>(opName#"_e32").Pfl.AsmDPP8;
        let AsmString = asmName # AsmDPP8;
        let isAsmParserOnly = 1;
        let WaveSizePredicate = isWave64;
      }
  }

  //===----------------------------- VOP3Only -----------------------------===//
  multiclass VOP3Only_Real_gfx10<bits<10> op> {
    def _e64_gfx10 :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.GFX10>,
      VOP3e_gfx10<op, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl> {
        let IsSingle = 1;
      }
  }

  //===---------------------------- VOP3beOnly ----------------------------===//
  multiclass VOP3beOnly_Real_gfx10<bits<10> op> {
    def _e64_gfx10 :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.GFX10>,
      VOP3be_gfx10<op, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl> {
        let IsSingle = 1;
      }
  }
} // End AssemblerPredicate = isGFX10Only, DecoderNamespace = "GFX10"

multiclass VOP2Only_Real_MADK_gfx10_gfx11<bits<6> op> :
  VOP2Only_Real_MADK_gfx10<op>, VOP2Only_Real_MADK<GFX11Gen, op>;

multiclass VOP2Only_Real_MADK_gfx10_gfx11_gfx12<bits<6> op> :
  VOP2Only_Real_MADK_gfx10_gfx11<op>, VOP2Only_Real_MADK<GFX12Gen, op>;

multiclass VOP2be_Real_gfx10<bits<6> op, string opName, string asmName> :
  VOP2be_Real_e32_gfx10<op, opName, asmName>,
  VOP2be_Real_e64_gfx10<op, opName, asmName>,
  VOP2be_Real_sdwa_gfx10<op, opName, asmName>,
  VOP2be_Real_dpp_gfx10<op, opName, asmName>,
  VOP2be_Real_dpp8_gfx10<op, opName, asmName>;

multiclass VOP2e_Real_gfx10<bits<6> op, string opName, string asmName> :
  VOP2_Real_e32_gfx10<op>,
  VOP2_Real_e64_gfx10<op>,
  VOP2be_Real_sdwa_gfx10<op, opName, asmName>,
  VOP2be_Real_dpp_gfx10<op, opName, asmName>,
  VOP2be_Real_dpp8_gfx10<op, opName, asmName>;

multiclass VOP2_Real_gfx10<bits<6> op> :
  VOP2_Real_e32_gfx10<op>, VOP2_Real_e64_gfx10<op>,
  VOP2_Real_sdwa_gfx10<op>, VOP2_Real_dpp_gfx10<op>, VOP2_Real_dpp8_gfx10<op>;

multiclass VOP2_Real_gfx10_gfx11<bits<6> op> :
  VOP2_Real_gfx10<op>, VOP2_Real_FULL<GFX11Gen, op>;

multiclass VOP2_Real_gfx10_gfx11_gfx12<bits<6> op> :
  VOP2_Real_gfx10_gfx11<op>, VOP2_Real_FULL<GFX12Gen, op>;

multiclass VOP2_Real_with_name_gfx10<bits<6> op, string opName,
                                     string asmName> :
  VOP2_Real_e32_gfx10_with_name<op, opName, asmName>,
  VOP2_Real_e64_gfx10_with_name<op, opName, asmName>,
  VOP2_Real_sdwa_gfx10_with_name<op, opName, asmName>,
  VOP2_Real_dpp_gfx10_with_name<op, opName, asmName>,
  VOP2_Real_dpp8_gfx10_with_name<op, opName, asmName>;

multiclass VOP2_Real_with_name_gfx10_gfx11_gfx12<bits<6> op, string opName,
                                                 string asmName> :
  VOP2_Real_with_name_gfx10<op, opName, asmName>,
  VOP2_Real_FULL_with_name<GFX11Gen, op, opName, asmName>,
  VOP2_Real_FULL_with_name<GFX12Gen, op, opName, asmName>;

// NB: Same opcode as v_mac_legacy_f32
let DecoderNamespace = "GFX10_B" in
defm V_FMAC_LEGACY_F32 : VOP2_Real_gfx10<0x006>;

defm V_XNOR_B32        : VOP2_Real_gfx10_gfx11_gfx12<0x01e>;
defm V_FMAC_F32        : VOP2_Real_gfx10_gfx11_gfx12<0x02b>;
defm V_FMAMK_F32       : VOP2Only_Real_MADK_gfx10_gfx11_gfx12<0x02c>;
defm V_FMAAK_F32       : VOP2Only_Real_MADK_gfx10_gfx11_gfx12<0x02d>;
defm V_ADD_F16         : VOP2_Real_gfx10<0x032>;
defm V_SUB_F16         : VOP2_Real_gfx10<0x033>;
defm V_SUBREV_F16      : VOP2_Real_gfx10<0x034>;
defm V_MUL_F16         : VOP2_Real_gfx10<0x035>;
defm V_FMAC_F16        : VOP2_Real_gfx10<0x036>;
defm V_FMAMK_F16       : VOP2Only_Real_MADK_gfx10<0x037>;
defm V_FMAAK_F16       : VOP2Only_Real_MADK_gfx10<0x038>;
defm V_MAX_F16         : VOP2_Real_gfx10<0x039>;
defm V_MIN_F16         : VOP2_Real_gfx10<0x03a>;
defm V_LDEXP_F16       : VOP2_Real_gfx10<0x03b>;

let IsSingle = 1 in {
  defm V_PK_FMAC_F16     : VOP2_Real_e32_gfx10<0x03c>;
}

// VOP2 no carry-in, carry-out.
defm V_ADD_NC_U32 :
  VOP2_Real_with_name_gfx10_gfx11_gfx12<0x025, "V_ADD_U32", "v_add_nc_u32">;
defm V_SUB_NC_U32 :
  VOP2_Real_with_name_gfx10_gfx11_gfx12<0x026, "V_SUB_U32", "v_sub_nc_u32">;
defm V_SUBREV_NC_U32 :
  VOP2_Real_with_name_gfx10_gfx11_gfx12<0x027, "V_SUBREV_U32", "v_subrev_nc_u32">;

// VOP2 carry-in, carry-out.
defm V_ADD_CO_CI_U32 :
  VOP2be_Real_gfx10<0x028, "V_ADDC_U32", "v_add_co_ci_u32">;
defm V_SUB_CO_CI_U32 :
  VOP2be_Real_gfx10<0x029, "V_SUBB_U32", "v_sub_co_ci_u32">;
defm V_SUBREV_CO_CI_U32 :
  VOP2be_Real_gfx10<0x02a, "V_SUBBREV_U32", "v_subrev_co_ci_u32">;

defm V_CNDMASK_B32 :
  VOP2e_Real_gfx10<0x001, "V_CNDMASK_B32", "v_cndmask_b32">;

// VOP3 only.
defm V_BFM_B32            : VOP3Only_Real_gfx10<0x363>;
defm V_BCNT_U32_B32       : VOP3Only_Real_gfx10<0x364>;
defm V_MBCNT_LO_U32_B32   : VOP3Only_Real_gfx10<0x365>;
defm V_MBCNT_HI_U32_B32   : VOP3Only_Real_gfx10<0x366>;
defm V_LDEXP_F32          : VOP3Only_Real_gfx10<0x362>;
defm V_CVT_PKNORM_I16_F32 : VOP3Only_Real_gfx10<0x368>;
defm V_CVT_PKNORM_U16_F32 : VOP3Only_Real_gfx10<0x369>;
defm V_CVT_PK_U16_U32     : VOP3Only_Real_gfx10<0x36a>;
defm V_CVT_PK_I16_I32     : VOP3Only_Real_gfx10<0x36b>;

// VOP3 carry-out.
defm V_ADD_CO_U32 : VOP3beOnly_Real_gfx10<0x30f>;
defm V_SUB_CO_U32 : VOP3beOnly_Real_gfx10<0x310>;
defm V_SUBREV_CO_U32 : VOP3beOnly_Real_gfx10<0x319>;

let SubtargetPredicate = isGFX10Only in {
  defm : VOP2eInstAliases<V_CNDMASK_B32_e32, V_CNDMASK_B32_e32_gfx10>;

  defm : VOP2bInstAliases<
    V_ADDC_U32_e32, V_ADD_CO_CI_U32_e32_gfx10, "v_add_co_ci_u32">;
  defm : VOP2bInstAliases<
    V_SUBB_U32_e32, V_SUB_CO_CI_U32_e32_gfx10, "v_sub_co_ci_u32">;
  defm : VOP2bInstAliases<
    V_SUBBREV_U32_e32, V_SUBREV_CO_CI_U32_e32_gfx10, "v_subrev_co_ci_u32">;
} // End SubtargetPredicate = isGFX10Only

//===----------------------------------------------------------------------===//
// GFX6, GFX7, GFX10, GFX11
//===----------------------------------------------------------------------===//

class VOP2_DPPe <bits<6> op, VOP2_DPP_Pseudo ps, VOPProfile P = ps.Pfl> :
  VOP_DPPe <P> {
  bits<8> vdst;
  bits<8> src1;
  let Inst{8-0}   = 0xfa; //dpp
  let Inst{16-9}  = !if(P.HasSrc1, src1{7-0}, 0);
  let Inst{24-17} = !if(P.EmitDst, vdst{7-0}, 0);
  let Inst{30-25} = op;
  let Inst{31}    = 0x0; //encoding
}

let AssemblerPredicate = isGFX6GFX7, DecoderNamespace = "GFX6GFX7" in {
  multiclass VOP2_Lane_Real_gfx6_gfx7<bits<6> op> {
    def _gfx6_gfx7 :
      VOP2_Real<!cast<VOP2_Pseudo>(NAME), SIEncodingFamily.SI>,
      VOP2e<op{5-0}, !cast<VOP2_Pseudo>(NAME).Pfl>;
  }
  multiclass VOP2Only_Real_MADK_gfx6_gfx7<bits<6> op> {
    def _gfx6_gfx7 :
      VOP2_Real<!cast<VOP2_Pseudo>(NAME), SIEncodingFamily.SI>,
      VOP2_MADKe<op{5-0}, !cast<VOP2_Pseudo>(NAME).Pfl>;
  }
  multiclass VOP2_Real_e32_gfx6_gfx7<bits<6> op, string opName = NAME> {
    def _e32_gfx6_gfx7 :
      VOP2_Real<!cast<VOP2_Pseudo>(opName#"_e32"), SIEncodingFamily.SI>,
      VOP2e<op{5-0}, !cast<VOP2_Pseudo>(opName#"_e32").Pfl>;
  }
  multiclass VOP2_Real_e64_gfx6_gfx7<bits<6> op, string opName = NAME> {
    def _e64_gfx6_gfx7 :
      VOP3_Real<!cast<VOP3_Pseudo>(opName#"_e64"), SIEncodingFamily.SI>,
      VOP3e_gfx6_gfx7<{1, 0, 0, op{5-0}}, !cast<VOP3_Pseudo>(opName#"_e64").Pfl>;
  }
  multiclass VOP2be_Real_e64_gfx6_gfx7<bits<6> op, string opName = NAME> {
    def _e64_gfx6_gfx7 :
      VOP3_Real<!cast<VOP3_Pseudo>(opName#"_e64"), SIEncodingFamily.SI>,
      VOP3be_gfx6_gfx7<{1, 0, 0, op{5-0}}, !cast<VOP3_Pseudo>(opName#"_e64").Pfl>;
  }
} // End AssemblerPredicate = isGFX6GFX7, DecoderNamespace = "GFX6GFX7"

multiclass VOP2Only_Real_MADK_gfx6_gfx7_gfx10<bits<6> op> :
  VOP2Only_Real_MADK_gfx6_gfx7<op>, VOP2Only_Real_MADK_gfx10<op>;

multiclass VOP2_Real_gfx6_gfx7<bits<6> op> :
  VOP2_Real_e32_gfx6_gfx7<op>, VOP2_Real_e64_gfx6_gfx7<op>;

multiclass VOP2_Real_gfx6_gfx7_gfx10<bits<6> op> :
  VOP2_Real_gfx6_gfx7<op>, VOP2_Real_gfx10<op>;

multiclass VOP2_Real_gfx6_gfx7_gfx10_gfx11<bits<6> op> :
  VOP2_Real_gfx6_gfx7_gfx10<op>, VOP2_Real_FULL<GFX11Gen, op>;

multiclass VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<bits<6> op> :
  VOP2_Real_gfx6_gfx7_gfx10_gfx11<op>, VOP2_Real_FULL<GFX12Gen, op>;

multiclass VOP2be_Real_gfx6_gfx7<bits<6> op> :
  VOP2_Real_e32_gfx6_gfx7<op>, VOP2be_Real_e64_gfx6_gfx7<op>;

multiclass VOP2be_Real_gfx6_gfx7_with_name<bits<6> op,
  string opName, string asmName>  {
  defvar ps32 = !cast<VOP2_Pseudo>(opName#"_e32");
  defvar ps64 = !cast<VOP3_Pseudo>(opName#"_e64");

  let AsmString = asmName # ps32.AsmOperands in {
    defm "" : VOP2_Real_e32_gfx6_gfx7<op, opName>;
  }

   let AsmString = asmName # ps64.AsmOperands in {
    defm "" : VOP2be_Real_e64_gfx6_gfx7<op, opName>;
  }
}

defm V_CNDMASK_B32        : VOP2_Real_gfx6_gfx7<0x000>;
defm V_MIN_LEGACY_F32     : VOP2_Real_gfx6_gfx7<0x00d>;
defm V_MAX_LEGACY_F32     : VOP2_Real_gfx6_gfx7<0x00e>;
defm V_LSHR_B32           : VOP2_Real_gfx6_gfx7<0x015>;
defm V_ASHR_I32           : VOP2_Real_gfx6_gfx7<0x017>;
defm V_LSHL_B32           : VOP2_Real_gfx6_gfx7<0x019>;
defm V_BFM_B32            : VOP2_Real_gfx6_gfx7<0x01e>;
defm V_BCNT_U32_B32       : VOP2_Real_gfx6_gfx7<0x022>;
defm V_MBCNT_LO_U32_B32   : VOP2_Real_gfx6_gfx7<0x023>;
defm V_MBCNT_HI_U32_B32   : VOP2_Real_gfx6_gfx7<0x024>;
defm V_LDEXP_F32          : VOP2_Real_gfx6_gfx7<0x02b>;
defm V_CVT_PKACCUM_U8_F32 : VOP2_Real_gfx6_gfx7<0x02c>;
defm V_CVT_PKNORM_I16_F32 : VOP2_Real_gfx6_gfx7<0x02d>;
defm V_CVT_PKNORM_U16_F32 : VOP2_Real_gfx6_gfx7<0x02e>;
defm V_CVT_PK_U16_U32     : VOP2_Real_gfx6_gfx7<0x030>;
defm V_CVT_PK_I16_I32     : VOP2_Real_gfx6_gfx7<0x031>;

// V_ADD_I32, V_SUB_I32, and V_SUBREV_I32 where renamed to *_U32 in
// VI, but the VI instructions behave the same as the SI versions.
defm V_ADD_I32            : VOP2be_Real_gfx6_gfx7_with_name<0x025, "V_ADD_CO_U32", "v_add_i32">;
defm V_SUB_I32            : VOP2be_Real_gfx6_gfx7_with_name<0x026, "V_SUB_CO_U32", "v_sub_i32">;
defm V_SUBREV_I32         : VOP2be_Real_gfx6_gfx7_with_name<0x027, "V_SUBREV_CO_U32", "v_subrev_i32">;
defm V_ADDC_U32           : VOP2be_Real_gfx6_gfx7<0x028>;
defm V_SUBB_U32           : VOP2be_Real_gfx6_gfx7<0x029>;
defm V_SUBBREV_U32        : VOP2be_Real_gfx6_gfx7<0x02a>;

defm V_READLANE_B32 : VOP2_Lane_Real_gfx6_gfx7<0x001>;

let InOperandList = (ins SSrcOrLds_b32:$src0, SCSrc_b32:$src1, VGPR_32:$vdst_in) in {
  defm V_WRITELANE_B32 : VOP2_Lane_Real_gfx6_gfx7<0x002>;
} // End InOperandList = (ins SSrcOrLds_b32:$src0, SCSrc_b32:$src1, VGPR_32:$vdst_in)

let SubtargetPredicate = isGFX6GFX7 in {
  defm : VOP2eInstAliases<V_CNDMASK_B32_e32, V_CNDMASK_B32_e32_gfx6_gfx7>;
  defm : VOP2eInstAliases<V_ADD_CO_U32_e32, V_ADD_I32_e32_gfx6_gfx7>;
  defm : VOP2eInstAliases<V_SUB_CO_U32_e32, V_SUB_I32_e32_gfx6_gfx7>;
  defm : VOP2eInstAliases<V_SUBREV_CO_U32_e32, V_SUBREV_I32_e32_gfx6_gfx7>;

  def : VOP2e64InstAlias<V_ADD_CO_U32_e64, V_ADD_I32_e64_gfx6_gfx7>;
  def : VOP2e64InstAlias<V_SUB_CO_U32_e64, V_SUB_I32_e64_gfx6_gfx7>;
  def : VOP2e64InstAlias<V_SUBREV_CO_U32_e64, V_SUBREV_I32_e64_gfx6_gfx7>;
} // End SubtargetPredicate = isGFX6GFX7

defm V_ADD_F32            : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x003>;
defm V_SUB_F32            : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x004>;
defm V_SUBREV_F32         : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x005>;
defm V_MAC_LEGACY_F32     : VOP2_Real_gfx6_gfx7_gfx10<0x006>;
defm V_MUL_LEGACY_F32     : VOP2_Real_gfx6_gfx7_gfx10<0x007>;
defm V_MUL_F32            : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x008>;
defm V_MUL_I32_I24        : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x009>;
defm V_MUL_HI_I32_I24     : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x00a>;
defm V_MUL_U32_U24        : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x00b>;
defm V_MUL_HI_U32_U24     : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x00c>;
defm V_MIN_F32            : VOP2_Real_gfx6_gfx7_gfx10_gfx11<0x00f>;
defm V_MAX_F32            : VOP2_Real_gfx6_gfx7_gfx10_gfx11<0x010>;
defm V_MIN_I32            : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x011>;
defm V_MAX_I32            : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x012>;
defm V_MIN_U32            : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x013>;
defm V_MAX_U32            : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x014>;
defm V_LSHRREV_B32        : VOP2_Real_gfx6_gfx7_gfx10<0x016>;
defm V_ASHRREV_I32        : VOP2_Real_gfx6_gfx7_gfx10<0x018>;
defm V_LSHLREV_B32        : VOP2_Real_gfx6_gfx7_gfx10<0x01a>;
defm V_AND_B32            : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x01b>;
defm V_OR_B32             : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x01c>;
defm V_XOR_B32            : VOP2_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x01d>;
defm V_MAC_F32            : VOP2_Real_gfx6_gfx7_gfx10<0x01f>;
defm V_CVT_PKRTZ_F16_F32  : VOP2_Real_gfx6_gfx7_gfx10<0x02f>;
defm V_MADMK_F32          : VOP2Only_Real_MADK_gfx6_gfx7_gfx10<0x020>;
defm V_MADAK_F32          : VOP2Only_Real_MADK_gfx6_gfx7_gfx10<0x021>;

//===----------------------------------------------------------------------===//
// GFX8, GFX9 (VI).
//===----------------------------------------------------------------------===//

let AssemblerPredicate = isGFX8GFX9, DecoderNamespace = "GFX8" in {

multiclass VOP2_Real_MADK_vi <bits<6> op> {
  def _vi : VOP2_Real<!cast<VOP2_Pseudo>(NAME), SIEncodingFamily.VI>,
            VOP2_MADKe<op{5-0}, !cast<VOP2_Pseudo>(NAME).Pfl>;
}

multiclass VOP2_Real_MADK_gfx940 <bits<6> op> {
  def _gfx940 : VOP2_Real<!cast<VOP2_Pseudo>(NAME), SIEncodingFamily.GFX940>,
                VOP2_MADKe<op{5-0}, !cast<VOP2_Pseudo>(NAME).Pfl> {
    let DecoderNamespace = "GFX9";
  }
}

multiclass VOP2_Real_e32_vi <bits<6> op> {
  def _e32_vi :
    VOP2_Real<!cast<VOP2_Pseudo>(NAME#"_e32"), SIEncodingFamily.VI>,
    VOP2e<op{5-0}, !cast<VOP2_Pseudo>(NAME#"_e32").Pfl>;
}

multiclass VOP2_Real_e64_vi <bits<10> op> {
  def _e64_vi :
    VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.VI>,
    VOP3e_vi <op, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
}

multiclass VOP2_Real_e64only_vi <bits<10> op> {
  def _e64_vi :
    VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.VI>,
    VOP3e_vi <op, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl> {
      let IsSingle = 1;
    }
}

multiclass Base_VOP2_Real_e32e64_vi <bits<6> op> :
  VOP2_Real_e32_vi<op>,
  VOP2_Real_e64_vi<{0, 1, 0, 0, op{5-0}}>;

} // End AssemblerPredicate = isGFX8GFX9, DecoderNamespace = "GFX8"

multiclass VOP2_SDWA_Real <bits<6> op> {
  if !cast<VOP2_Pseudo>(NAME#"_e32").Pfl.HasExtSDWA then
  def _sdwa_vi :
    VOP_SDWA_Real <!cast<VOP2_SDWA_Pseudo>(NAME#"_sdwa")>,
    VOP2_SDWAe <op{5-0}, !cast<VOP2_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;
}

multiclass VOP2_SDWA9_Real <bits<6> op> {
  if !cast<VOP2_Pseudo>(NAME#"_e32").Pfl.HasExtSDWA9 then
  def _sdwa_gfx9 :
    VOP_SDWA9_Real <!cast<VOP2_SDWA_Pseudo>(NAME#"_sdwa")>,
    VOP2_SDWA9Ae <op{5-0}, !cast<VOP2_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;
}

let AssemblerPredicate = isGFX8Only, DecoderNamespace = "GFX8" in {

multiclass VOP2be_Real_e32e64_vi_only <bits<6> op, string OpName, string AsmName> {
  def _e32_vi :
    VOP2_Real<!cast<VOP2_Pseudo>(OpName#"_e32"), SIEncodingFamily.VI>,
    VOP2e<op{5-0}, !cast<VOP2_Pseudo>(OpName#"_e32").Pfl> {
      VOP2_Pseudo ps = !cast<VOP2_Pseudo>(OpName#"_e32");
      let AsmString = AsmName # ps.AsmOperands;
    }
  def _e64_vi :
    VOP3_Real<!cast<VOP3_Pseudo>(OpName#"_e64"), SIEncodingFamily.VI>,
    VOP3be_vi <{0, 1, 0, 0, op{5-0}}, !cast<VOP3_Pseudo>(OpName#"_e64").Pfl> {
      VOP3_Pseudo ps = !cast<VOP3_Pseudo>(OpName#"_e64");
      let AsmString = AsmName # ps.AsmOperands;
    }
  if !cast<VOP2_Pseudo>(OpName#"_e32").Pfl.HasExtSDWA then
    def _sdwa_vi :
      VOP_SDWA_Real <!cast<VOP2_SDWA_Pseudo>(OpName#"_sdwa")>,
      VOP2_SDWAe <op{5-0}, !cast<VOP2_SDWA_Pseudo>(OpName#"_sdwa").Pfl> {
        VOP2_SDWA_Pseudo ps = !cast<VOP2_SDWA_Pseudo>(OpName#"_sdwa");
        let AsmString = AsmName # ps.AsmOperands;
      }
  if !cast<VOP2_Pseudo>(OpName#"_e32").Pfl.HasExtDPP then
    def _dpp_vi :
      VOP_DPP_Real<!cast<VOP2_DPP_Pseudo>(OpName#"_dpp"), SIEncodingFamily.VI>,
      VOP2_DPPe<op, !cast<VOP2_DPP_Pseudo>(OpName#"_dpp")> {
        VOP2_DPP_Pseudo ps = !cast<VOP2_DPP_Pseudo>(OpName#"_dpp");
        let AsmString = AsmName # ps.AsmOperands;
      }
}

} // End AssemblerPredicate = isGFX8Only, DecoderNamespace = "GFX8"

let AssemblerPredicate = isGFX9Only, DecoderNamespace = "GFX9" in {

multiclass VOP2be_Real_e32e64_gfx9 <bits<6> op, string OpName, string AsmName> {
  def _e32_gfx9 :
    VOP2_Real<!cast<VOP2_Pseudo>(OpName#"_e32"), SIEncodingFamily.GFX9>,
    VOP2e<op{5-0}, !cast<VOP2_Pseudo>(OpName#"_e32").Pfl> {
      VOP2_Pseudo ps = !cast<VOP2_Pseudo>(OpName#"_e32");
      let AsmString = AsmName # ps.AsmOperands;
    }
  def _e64_gfx9 :
    VOP3_Real<!cast<VOP3_Pseudo>(OpName#"_e64"), SIEncodingFamily.GFX9>,
    VOP3be_vi <{0, 1, 0, 0, op{5-0}}, !cast<VOP3_Pseudo>(OpName#"_e64").Pfl> {
      VOP3_Pseudo ps = !cast<VOP3_Pseudo>(OpName#"_e64");
      let AsmString = AsmName # ps.AsmOperands;
    }
  if !cast<VOP2_Pseudo>(OpName#"_e32").Pfl.HasExtSDWA9 then
    def _sdwa_gfx9 :
      VOP_SDWA9_Real <!cast<VOP2_SDWA_Pseudo>(OpName#"_sdwa")>,
      VOP2_SDWA9Ae <op{5-0}, !cast<VOP2_SDWA_Pseudo>(OpName#"_sdwa").Pfl> {
        VOP2_SDWA_Pseudo ps = !cast<VOP2_SDWA_Pseudo>(OpName#"_sdwa");
        let AsmString = AsmName # ps.AsmOperands;
      }
  if !cast<VOP2_Pseudo>(OpName#"_e32").Pfl.HasExtDPP then
    def _dpp_gfx9 :
      VOP_DPP_Real<!cast<VOP2_DPP_Pseudo>(OpName#"_dpp"), SIEncodingFamily.GFX9>,
      VOP2_DPPe<op, !cast<VOP2_DPP_Pseudo>(OpName#"_dpp")> {
        VOP2_DPP_Pseudo ps = !cast<VOP2_DPP_Pseudo>(OpName#"_dpp");
        let AsmString = AsmName # ps.AsmOperands;
      }
}

multiclass VOP2_Real_e32e64_gfx9 <bits<6> op> {
  def _e32_gfx9 :
    VOP2_Real<!cast<VOP2_Pseudo>(NAME#"_e32"), SIEncodingFamily.GFX9>,
    VOP2e<op{5-0}, !cast<VOP2_Pseudo>(NAME#"_e32").Pfl>;
  def _e64_gfx9 :
    VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.GFX9>,
    VOP3e_vi <{0, 1, 0, 0, op{5-0}}, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
  if !cast<VOP2_Pseudo>(NAME#"_e32").Pfl.HasExtSDWA9 then
    def _sdwa_gfx9 :
      VOP_SDWA9_Real <!cast<VOP2_SDWA_Pseudo>(NAME#"_sdwa")>,
      VOP2_SDWA9Ae <op{5-0}, !cast<VOP2_SDWA_Pseudo>(NAME#"_sdwa").Pfl> {
      }
  if !cast<VOP2_Pseudo>(NAME#"_e32").Pfl.HasExtDPP then
    def _dpp_gfx9 :
      VOP_DPP_Real<!cast<VOP2_DPP_Pseudo>(NAME#"_dpp"), SIEncodingFamily.GFX9>,
      VOP2_DPPe<op, !cast<VOP2_DPP_Pseudo>(NAME#"_dpp")>;
}

} // End AssemblerPredicate = isGFX9Only, DecoderNamespace = "GFX9"

multiclass VOP2_Real_e32e64_vi <bits<6> op> :
  Base_VOP2_Real_e32e64_vi<op>, VOP2_SDWA_Real<op>, VOP2_SDWA9_Real<op> {

  if !cast<VOP2_Pseudo>(NAME#"_e32").Pfl.HasExtDPP then
    def _dpp_vi :
      VOP_DPP_Real<!cast<VOP2_DPP_Pseudo>(NAME#"_dpp"), SIEncodingFamily.VI>,
      VOP2_DPPe<op, !cast<VOP2_DPP_Pseudo>(NAME#"_dpp")>;
}

defm V_CNDMASK_B32        : VOP2_Real_e32e64_vi <0x0>;
defm V_ADD_F32            : VOP2_Real_e32e64_vi <0x1>;
defm V_SUB_F32            : VOP2_Real_e32e64_vi <0x2>;
defm V_SUBREV_F32         : VOP2_Real_e32e64_vi <0x3>;
let AssemblerPredicate = isGCN3ExcludingGFX90A in
defm V_MUL_LEGACY_F32     : VOP2_Real_e32e64_vi <0x4>;
defm V_MUL_F32            : VOP2_Real_e32e64_vi <0x5>;
defm V_MUL_I32_I24        : VOP2_Real_e32e64_vi <0x6>;
defm V_MUL_HI_I32_I24     : VOP2_Real_e32e64_vi <0x7>;
defm V_MUL_U32_U24        : VOP2_Real_e32e64_vi <0x8>;
defm V_MUL_HI_U32_U24     : VOP2_Real_e32e64_vi <0x9>;
defm V_MIN_F32            : VOP2_Real_e32e64_vi <0xa>;
defm V_MAX_F32            : VOP2_Real_e32e64_vi <0xb>;
defm V_MIN_I32            : VOP2_Real_e32e64_vi <0xc>;
defm V_MAX_I32            : VOP2_Real_e32e64_vi <0xd>;
defm V_MIN_U32            : VOP2_Real_e32e64_vi <0xe>;
defm V_MAX_U32            : VOP2_Real_e32e64_vi <0xf>;
defm V_LSHRREV_B32        : VOP2_Real_e32e64_vi <0x10>;
defm V_ASHRREV_I32        : VOP2_Real_e32e64_vi <0x11>;
defm V_LSHLREV_B32        : VOP2_Real_e32e64_vi <0x12>;
defm V_AND_B32            : VOP2_Real_e32e64_vi <0x13>;
defm V_OR_B32             : VOP2_Real_e32e64_vi <0x14>;
defm V_XOR_B32            : VOP2_Real_e32e64_vi <0x15>;
defm V_MAC_F32            : VOP2_Real_e32e64_vi <0x16>;
defm V_MADMK_F32          : VOP2_Real_MADK_vi <0x17>;
defm V_MADAK_F32          : VOP2_Real_MADK_vi <0x18>;

defm V_ADD_U32            : VOP2be_Real_e32e64_vi_only <0x19, "V_ADD_CO_U32",     "v_add_u32">;
defm V_SUB_U32            : VOP2be_Real_e32e64_vi_only <0x1a, "V_SUB_CO_U32",     "v_sub_u32">;
defm V_SUBREV_U32         : VOP2be_Real_e32e64_vi_only <0x1b, "V_SUBREV_CO_U32",  "v_subrev_u32">;
defm V_ADDC_U32           : VOP2be_Real_e32e64_vi_only <0x1c, "V_ADDC_U32",    "v_addc_u32">;
defm V_SUBB_U32           : VOP2be_Real_e32e64_vi_only <0x1d, "V_SUBB_U32",    "v_subb_u32">;
defm V_SUBBREV_U32        : VOP2be_Real_e32e64_vi_only <0x1e, "V_SUBBREV_U32", "v_subbrev_u32">;

defm V_ADD_CO_U32         : VOP2be_Real_e32e64_gfx9 <0x19, "V_ADD_CO_U32",     "v_add_co_u32">;
defm V_SUB_CO_U32         : VOP2be_Real_e32e64_gfx9 <0x1a, "V_SUB_CO_U32",     "v_sub_co_u32">;
defm V_SUBREV_CO_U32      : VOP2be_Real_e32e64_gfx9 <0x1b, "V_SUBREV_CO_U32",  "v_subrev_co_u32">;
defm V_ADDC_CO_U32        : VOP2be_Real_e32e64_gfx9 <0x1c, "V_ADDC_U32",    "v_addc_co_u32">;
defm V_SUBB_CO_U32        : VOP2be_Real_e32e64_gfx9 <0x1d, "V_SUBB_U32",    "v_subb_co_u32">;
defm V_SUBBREV_CO_U32     : VOP2be_Real_e32e64_gfx9 <0x1e, "V_SUBBREV_U32", "v_subbrev_co_u32">;

defm V_ADD_U32            : VOP2_Real_e32e64_gfx9 <0x34>;
defm V_SUB_U32            : VOP2_Real_e32e64_gfx9 <0x35>;
defm V_SUBREV_U32         : VOP2_Real_e32e64_gfx9 <0x36>;

defm V_BFM_B32            : VOP2_Real_e64only_vi <0x293>;
defm V_BCNT_U32_B32       : VOP2_Real_e64only_vi <0x28b>;
defm V_MBCNT_LO_U32_B32   : VOP2_Real_e64only_vi <0x28c>;
defm V_MBCNT_HI_U32_B32   : VOP2_Real_e64only_vi <0x28d>;
defm V_LDEXP_F32          : VOP2_Real_e64only_vi <0x288>;
defm V_CVT_PKACCUM_U8_F32 : VOP2_Real_e64only_vi <0x1f0>;
defm V_CVT_PKNORM_I16_F32 : VOP2_Real_e64only_vi <0x294>;
defm V_CVT_PKNORM_U16_F32 : VOP2_Real_e64only_vi <0x295>;
defm V_CVT_PKRTZ_F16_F32  : VOP2_Real_e64only_vi <0x296>;
defm V_CVT_PK_U16_U32     : VOP2_Real_e64only_vi <0x297>;
defm V_CVT_PK_I16_I32     : VOP2_Real_e64only_vi <0x298>;

defm V_ADD_F16            : VOP2_Real_e32e64_vi <0x1f>;
defm V_SUB_F16            : VOP2_Real_e32e64_vi <0x20>;
defm V_SUBREV_F16         : VOP2_Real_e32e64_vi <0x21>;
defm V_MUL_F16            : VOP2_Real_e32e64_vi <0x22>;
defm V_MAC_F16            : VOP2_Real_e32e64_vi <0x23>;
defm V_MADMK_F16          : VOP2_Real_MADK_vi <0x24>;
defm V_MADAK_F16          : VOP2_Real_MADK_vi <0x25>;
defm V_ADD_U16            : VOP2_Real_e32e64_vi <0x26>;
defm V_SUB_U16            : VOP2_Real_e32e64_vi <0x27>;
defm V_SUBREV_U16         : VOP2_Real_e32e64_vi <0x28>;
defm V_MUL_LO_U16         : VOP2_Real_e32e64_vi <0x29>;
defm V_LSHLREV_B16        : VOP2_Real_e32e64_vi <0x2a>;
defm V_LSHRREV_B16        : VOP2_Real_e32e64_vi <0x2b>;
defm V_ASHRREV_I16        : VOP2_Real_e32e64_vi <0x2c>;
defm V_MAX_F16            : VOP2_Real_e32e64_vi <0x2d>;
defm V_MIN_F16            : VOP2_Real_e32e64_vi <0x2e>;
defm V_MAX_U16            : VOP2_Real_e32e64_vi <0x2f>;
defm V_MAX_I16            : VOP2_Real_e32e64_vi <0x30>;
defm V_MIN_U16            : VOP2_Real_e32e64_vi <0x31>;
defm V_MIN_I16            : VOP2_Real_e32e64_vi <0x32>;
defm V_LDEXP_F16          : VOP2_Real_e32e64_vi <0x33>;

let SubtargetPredicate = isGFX8GFX9 in {

// Aliases to simplify matching of floating-point instructions that
// are VOP2 on SI and VOP3 on VI.
class SI2_VI3Alias <string name, VOP3_Real inst> : InstAlias <
  name#" $dst, $src0, $src1",
  !if(inst.Pfl.HasOMod,
      (inst VGPR_32:$dst, 0, VCSrc_f32:$src0, 0, VCSrc_f32:$src1, 0, 0),
      (inst VGPR_32:$dst, 0, VCSrc_f32:$src0, 0, VCSrc_f32:$src1, 0))
>, PredicateControl {
  let UseInstAsmMatchConverter = 0;
  let AsmVariantName = AMDGPUAsmVariants.VOP3;
}

def : SI2_VI3Alias <"v_ldexp_f32", V_LDEXP_F32_e64_vi>;
def : SI2_VI3Alias <"v_cvt_pkaccum_u8_f32", V_CVT_PKACCUM_U8_F32_e64_vi>;
def : SI2_VI3Alias <"v_cvt_pknorm_i16_f32", V_CVT_PKNORM_I16_F32_e64_vi>;
def : SI2_VI3Alias <"v_cvt_pknorm_u16_f32", V_CVT_PKNORM_U16_F32_e64_vi>;
def : SI2_VI3Alias <"v_cvt_pkrtz_f16_f32", V_CVT_PKRTZ_F16_F32_e64_vi>;

defm : VOP2eInstAliases<V_CNDMASK_B32_e32, V_CNDMASK_B32_e32_vi>;

} // End SubtargetPredicate = isGFX8GFX9

let SubtargetPredicate = isGFX9Only in {

defm : VOP2bInstAliases<V_ADD_U32_e32,     V_ADD_CO_U32_e32_gfx9,     "v_add_co_u32">;
defm : VOP2bInstAliases<V_ADDC_U32_e32,    V_ADDC_CO_U32_e32_gfx9,    "v_addc_co_u32">;
defm : VOP2bInstAliases<V_SUB_U32_e32,     V_SUB_CO_U32_e32_gfx9,     "v_sub_co_u32">;
defm : VOP2bInstAliases<V_SUBB_U32_e32,    V_SUBB_CO_U32_e32_gfx9,    "v_subb_co_u32">;
defm : VOP2bInstAliases<V_SUBREV_U32_e32,  V_SUBREV_CO_U32_e32_gfx9,  "v_subrev_co_u32">;
defm : VOP2bInstAliases<V_SUBBREV_U32_e32, V_SUBBREV_CO_U32_e32_gfx9, "v_subbrev_co_u32">;

} // End SubtargetPredicate = isGFX9Only

let SubtargetPredicate = HasDLInsts in {

defm V_FMAC_F32 : VOP2_Real_e32e64_vi <0x3b>;
defm V_XNOR_B32 : VOP2_Real_e32e64_vi <0x3d>;

} // End SubtargetPredicate = HasDLInsts

let AssemblerPredicate = isGFX90APlus, DecoderNamespace = "GFX90A" in {
  multiclass VOP2_Real_e32_gfx90a <bits<6> op> {
    def _e32_gfx90a :
      VOP2_Real<!cast<VOP2_Pseudo>(NAME#"_e32"), SIEncodingFamily.GFX90A>,
      VOP2e<op{5-0}, !cast<VOP2_Pseudo>(NAME#"_e32").Pfl>;
  }

  multiclass VOP2_Real_e64_gfx90a <bits<10> op> {
    def _e64_gfx90a :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.GFX90A>,
      VOP3e_vi <op, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
  }

  multiclass Base_VOP2_Real_e32e64_gfx90a <bits<6> op> :
    VOP2_Real_e32_gfx90a<op>,
    VOP2_Real_e64_gfx90a<{0, 1, 0, 0, op{5-0}}>;

  multiclass VOP2_Real_e32e64_gfx90a <bits<6> op> :
    Base_VOP2_Real_e32e64_gfx90a<op> {

    if !cast<VOP2_Pseudo>(NAME#"_e32").Pfl.HasExtDPP then
      def _dpp_gfx90a :
        VOP_DPP_Real<!cast<VOP2_DPP_Pseudo>(NAME#"_dpp"), SIEncodingFamily.GFX90A>,
        VOP2_DPPe<op, !cast<VOP2_DPP_Pseudo>(NAME#"_dpp")> {
          let DecoderNamespace = "GFX9";
        }
  }
} // End AssemblerPredicate = isGFX90APlus, DecoderNamespace = "GFX90A"

let SubtargetPredicate = HasFmacF64Inst in {
  defm V_FMAC_F64       : VOP2_Real_e32e64_gfx90a <0x4>;
} // End SubtargetPredicate = HasFmacF64Inst

let SubtargetPredicate = isGFX90APlus, IsSingle = 1 in {
  defm V_MUL_LEGACY_F32 : VOP2_Real_e64_gfx90a <0x2a1>;
}

let SubtargetPredicate = HasFmaakFmamkF32Insts in {
defm V_FMAMK_F32        : VOP2_Real_MADK_gfx940 <0x17>;
defm V_FMAAK_F32        : VOP2_Real_MADK_gfx940 <0x18>;
}

multiclass VOP2_Real_DOT_ACC_gfx9<bits<6> op> : Base_VOP2_Real_e32e64_vi<op> {
  let SubtargetPredicate = isGFX9Only in
  def _dpp_vi : VOP2_DPP<op, !cast<VOP2_DPP_Pseudo>(NAME#"_dpp")>;
}

multiclass VOP2_Real_DOT_ACC_gfx10<bits<6> op> :
  VOP2_Real_e32_gfx10<op>,
  VOP2_Real_dpp_gfx10<op>,
  VOP2_Real_dpp8_gfx10<op>;

multiclass VOP2Only_Real_DOT_ACC_gfx10<bits<6> op> : VOP2_Real_dpp_gfx10<op>,
                                                     VOP2_Real_dpp8_gfx10<op> {
  let IsSingle = 1 in
    defm NAME : VOP2_Real_e32_gfx10<op>;
}

let OtherPredicates = [HasDot5Insts] in {
  defm V_DOT2C_F32_F16 : VOP2_Real_DOT_ACC_gfx9<0x37>;
  // NB: Opcode conflicts with V_DOT8C_I32_I4
  // This opcode exists in gfx 10.1* only
  defm V_DOT2C_F32_F16 : VOP2Only_Real_DOT_ACC_gfx10<0x02>;
}

let OtherPredicates = [HasDot6Insts] in {
  defm V_DOT4C_I32_I8  : VOP2_Real_DOT_ACC_gfx9<0x39>;
  defm V_DOT4C_I32_I8  : VOP2Only_Real_DOT_ACC_gfx10<0x0d>;
}

let OtherPredicates = [HasDot4Insts] in {
  defm V_DOT2C_I32_I16 : VOP2_Real_DOT_ACC_gfx9<0x38>;
}
let OtherPredicates = [HasDot3Insts] in {
  defm V_DOT8C_I32_I4  : VOP2_Real_DOT_ACC_gfx9<0x3a>;
}

let SubtargetPredicate = HasPkFmacF16Inst in {
defm V_PK_FMAC_F16 : VOP2_Real_e32_vi<0x3c>;
} // End SubtargetPredicate = HasPkFmacF16Inst

let SubtargetPredicate = HasDot3Insts in {
  // NB: Opcode conflicts with V_DOT2C_F32_F16
  let DecoderNamespace = "GFX10_B" in
  defm V_DOT8C_I32_I4 : VOP2_Real_DOT_ACC_gfx10<0x02>;
}
