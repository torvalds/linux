//===-- VOPCInstructions.td - Vector Instruction Definitions --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Encodings
//===----------------------------------------------------------------------===//

class VOPCe <bits<8> op> : Enc32 {
  bits<9> src0;
  bits<8> src1;

  let Inst{8-0} = src0;
  let Inst{16-9} = src1;
  let Inst{24-17} = op;
  let Inst{31-25} = 0x3e;
}

class VOPC_SDWAe <bits<8> op, VOPProfile P> : VOP_SDWAe <P> {
  bits<8> src1;

  let Inst{8-0}   = 0xf9; // sdwa
  let Inst{16-9}  = !if(P.HasSrc1, src1{7-0}, 0);
  let Inst{24-17} = op;
  let Inst{31-25} = 0x3e; // encoding
}

class VOPC_SDWA9e <bits<8> op, VOPProfile P> : VOP_SDWA9Be <P> {
  bits<9> src1;

  let Inst{8-0}   = 0xf9; // sdwa
  let Inst{16-9}  = !if(P.HasSrc1, src1{7-0}, 0);
  let Inst{24-17} = op;
  let Inst{31-25} = 0x3e; // encoding
  let Inst{63}    = !if(P.HasSrc1, src1{8}, 0); // src1_sgpr
}


//===----------------------------------------------------------------------===//
// VOPC classes
//===----------------------------------------------------------------------===//

// VOPC instructions are a special case because for the 32-bit
// encoding, we want to display the implicit vcc write as if it were
// an explicit $dst.
class VOPC_Profile<list<SchedReadWrite> sched, ValueType vt0, ValueType vt1 = vt0> :
  VOPProfile <[i1, vt0, vt1, untyped]> {
  // We want to exclude instructions with 64bit operands
  let HasExtDPP = getHasVOP3DPP<DstVT, Src0VT, Src1VT, Src2VT>.ret;
  let Asm32 = "$src0, $src1";

  let AsmDPP = !if (HasModifiers,
                    "$src0_modifiers, $src1_modifiers "
                    "$dpp_ctrl$row_mask$bank_mask$bound_ctrl",
                    "$src0, $src1 $dpp_ctrl$row_mask$bank_mask$bound_ctrl");
  let AsmDPP8 = "$src0, $src1 $dpp8$fi";
  let AsmDPP16 = AsmDPP#"$fi";
  // VOPC DPP Instructions do not need an old operand
  let TieRegDPP = "";
  let InsDPP = getInsDPP<VOPDstOperand<Src0DPP.RegClass>, Src0DPP, Src1DPP, Src2DPP,
                         NumSrcArgs, HasModifiers, Src0ModDPP, Src1ModDPP,
                         Src2ModDPP, 0/*HasOld*/>.ret;
  let InsDPP16 = getInsDPP16<VOPDstOperand<Src0DPP.RegClass>, Src0DPP, Src1DPP, Src2DPP,
                             NumSrcArgs, HasModifiers, Src0ModDPP, Src1ModDPP,
                             Src2ModDPP, 0/*HasOld*/>.ret;
  let InsDPP8 = getInsDPP8<VOPDstOperand<Src0DPP.RegClass>, Src0DPP, Src1DPP, Src2DPP,
                           NumSrcArgs, HasModifiers, Src0ModDPP, Src1ModDPP,
                           Src2ModDPP, 0/*HasOld*/>.ret;

  // The destination for 32-bit encoding is implicit.
  let HasDst32 = 0;
  // VOPC disallows dst_sel and dst_unused as they have no effect on destination
  let EmitDstSel = 0;
  let Outs64 = (outs VOPDstS64orS32:$sdst);
  let OutsVOP3DPP = Outs64;
  let OutsVOP3DPP8 = Outs64;
  let InsVOP3DPP = getInsVOP3DPP<InsVOP3Base, Src0VOP3DPP, NumSrcArgs, 0/*HasOld*/>.ret;
  let InsVOP3DPP16 = getInsVOP3DPP16<InsVOP3Base, Src0VOP3DPP, NumSrcArgs, 0/*HasOld*/>.ret;
  let InsVOP3DPP8 = getInsVOP3DPP8<InsVOP3Base, Src0VOP3DPP, NumSrcArgs, 0/*HasOld*/>.ret;
  list<SchedReadWrite> Schedule = sched;
}

multiclass VOPC_Profile_t16<list<SchedReadWrite> sched, ValueType vt0, ValueType vt1 = vt0> {
  def NAME : VOPC_Profile<sched, vt0, vt1>;
  def _t16 : VOPC_Profile<sched, vt0, vt1> {
    let IsTrue16 = 1;
    let Src1RC32 = getVregSrcForVT<Src1VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src0DPP = getVregSrcForVT<Src0VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src1DPP = getVregSrcForVT<Src1VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src2DPP = getVregSrcForVT<Src2VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src0ModDPP = getSrcModDPP_t16<Src0VT>.ret;
    let Src1ModDPP = getSrcModDPP_t16<Src1VT>.ret;
    let Src2ModDPP = getSrcModDPP_t16<Src2VT>.ret;
  }
}

class VOPC_NoSdst_Profile<list<SchedReadWrite> sched, ValueType vt0,
                          ValueType vt1 = vt0> :
  VOPC_Profile<sched, vt0, vt1> {
  let Outs64 = (outs );
  let OutsVOP3DPP = Outs64;
  let OutsVOP3DPP8 = Outs64;
  let OutsSDWA = (outs );
  let InsSDWA = (ins Src0ModSDWA:$src0_modifiers, Src0SDWA:$src0,
                     Src1ModSDWA:$src1_modifiers, Src1SDWA:$src1,
                     src0_sel:$src0_sel, src1_sel:$src1_sel);
  let AsmVOP3Base = !if(Src0VT.isFP, "$src0_modifiers, $src1_modifiers$clamp",
                                     "$src0, $src1");
  let AsmSDWA9 = "$src0_modifiers, $src1_modifiers $src0_sel $src1_sel";
  let EmitDst = 0;
}

multiclass VOPC_NoSdst_Profile_t16<list<SchedReadWrite> sched, ValueType vt0, ValueType vt1 = vt0> {
  def NAME : VOPC_NoSdst_Profile<sched, vt0, vt1>;
  def _t16 : VOPC_NoSdst_Profile<sched, vt0, vt1> {
    let IsTrue16 = 1;
    let Src1RC32 = getVregSrcForVT<Src1VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src0DPP = getVregSrcForVT<Src0VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src1DPP = getVregSrcForVT<Src1VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src2DPP = getVregSrcForVT<Src2VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src0ModDPP = getSrcModDPP_t16<Src0VT>.ret;
    let Src1ModDPP = getSrcModDPP_t16<Src1VT>.ret;
    let Src2ModDPP = getSrcModDPP_t16<Src2VT>.ret;
  }
}

class VOPC_Pseudo <string opName, VOPC_Profile P, list<dag> pattern=[],
                   bit DefVcc = 1> :
  InstSI<(outs), P.Ins32, "", pattern>,
  VOP <opName>,
  SIMCInstr<opName#"_e32", SIEncodingFamily.NONE> {

  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let UseNamedOperandTable = 1;

  string Mnemonic = opName;
  string AsmOperands = P.Asm32;

  let Size = 4;
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;

  let ReadsModeReg = P.Src0VT.isFP;

  let VALU = 1;
  let VOPC = 1;
  let Uses = !if(ReadsModeReg, [MODE, EXEC], [EXEC]);
  let Defs = !if(DefVcc, [VCC], []);

  VOPProfile Pfl = P;
}

class VOPC_Real <VOPC_Pseudo ps, int EncodingFamily, string asm_name = ps.PseudoInstr> :
  InstSI <ps.OutOperandList, ps.InOperandList, asm_name # " " # ps.AsmOperands, []>,
  SIMCInstr <ps.PseudoInstr, EncodingFamily> {

  let VALU = 1;
  let VOPC = 1;
  let isPseudo = 0;
  let isCodeGenOnly = 0;

  let Constraints     = ps.Constraints;
  let DisableEncoding = ps.DisableEncoding;

  // copy relevant pseudo op flags
  let SubtargetPredicate = ps.SubtargetPredicate;
  let AsmMatchConverter  = ps.AsmMatchConverter;
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

class VOPC_SDWA_Pseudo <string OpName, VOPProfile P, list<dag> pattern=[]> :
  VOP_SDWA_Pseudo <OpName, P, pattern> {
  let AsmMatchConverter = "cvtSdwaVOPC";
}

// This class is used only with VOPC instructions. Use $sdst for out operand
class VOPCInstAlias <VOP3_Pseudo ps, Instruction inst,
                     string Asm32 = ps.Pfl.Asm32, string real_name = ps.OpName,
                     VOPProfile p = ps.Pfl> :
  InstAlias <real_name#" "#Asm32, (inst)>, PredicateControl {

  field bit isCompare;
  field bit isCommutable;

  let ResultInst =
    !if (p.HasDst32,
      !if (!eq(p.NumSrcArgs, 0),
        // 1 dst, 0 src
        (inst p.DstRC:$sdst),
      !if (!eq(p.NumSrcArgs, 1),
        // 1 dst, 1 src
        (inst p.DstRC:$sdst, p.Src0RC32:$src0),
      !if (!eq(p.NumSrcArgs, 2),
        // 1 dst, 2 src
        (inst p.DstRC:$sdst, p.Src0RC32:$src0, p.Src1RC32:$src1),
      // else - unreachable
        (inst)))),
    // else
      !if (!eq(p.NumSrcArgs, 2),
        // 0 dst, 2 src
        (inst p.Src0RC32:$src0, p.Src1RC32:$src1),
      !if (!eq(p.NumSrcArgs, 1),
        // 0 dst, 1 src
        (inst p.Src0RC32:$src1),
      // else
        // 0 dst, 0 src
        (inst))));

  let AsmVariantName = AMDGPUAsmVariants.Default;
  let SubtargetPredicate = AssemblerPredicate;

  string DecoderNamespace; // dummy
}

multiclass VOPCInstAliases <string old_name, string Arch, string real_name = old_name, string mnemonic_from = real_name> {
  def : VOPCInstAlias <!cast<VOP3_Pseudo>(old_name#"_e64"),
                       !cast<Instruction>(real_name#"_e32_"#Arch),
                       !cast<VOP3_Pseudo>(old_name#"_e64").Pfl.Asm32,
                       mnemonic_from>;
  let WaveSizePredicate = isWave32 in {
    def : VOPCInstAlias <!cast<VOP3_Pseudo>(old_name#"_e64"),
                         !cast<Instruction>(real_name#"_e32_"#Arch),
                         "vcc_lo, "#!cast<VOP3_Pseudo>(old_name#"_e64").Pfl.Asm32,
                         mnemonic_from>;
  }
  let WaveSizePredicate = isWave64 in {
    def : VOPCInstAlias <!cast<VOP3_Pseudo>(old_name#"_e64"),
                         !cast<Instruction>(real_name#"_e32_"#Arch),
                         "vcc, "#!cast<VOP3_Pseudo>(old_name#"_e64").Pfl.Asm32,
                         mnemonic_from>;
  }
}

multiclass VOPCXInstAliases <string old_name, string Arch, string real_name = old_name, string mnemonic_from = real_name> {
  def : VOPCInstAlias <!cast<VOP3_Pseudo>(old_name#"_e64"),
                       !cast<Instruction>(real_name#"_e32_"#Arch),
                       !cast<VOP3_Pseudo>(old_name#"_e64").Pfl.Asm32,
                       mnemonic_from>;
}

class getVOPCPat64 <SDPatternOperator cond, VOPProfile P> : LetDummies {
  list<dag> ret = !if(P.HasModifiers,
      [(set i1:$sdst,
        (setcc (P.Src0VT
                  !if(P.HasOMod,
                    (VOP3Mods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp, i32:$omod),
                    (VOP3Mods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp))),
               (P.Src1VT (VOP3Mods P.Src1VT:$src1, i32:$src1_modifiers)),
               cond))],
      [(set i1:$sdst, (setcc P.Src0VT:$src0, P.Src1VT:$src1, cond))]);
}

class VCMPXNoSDstTable <bit has_sdst, string Name> {
  bit HasSDst = has_sdst;
  string NoSDstOp = Name;
}

class VCMPVCMPXTable <string Name> {
  bit IsVCMPX = 0;
  string VCMPOp = Name;
}

multiclass VOPC_Pseudos <string opName,
                         VOPC_Profile P,
                         SDPatternOperator cond = COND_NULL,
                         string revOp = opName,
                         bit DefExec = 0> {

  def _e32 : VOPC_Pseudo <opName, P>,
             Commutable_REV<revOp#"_e32", !eq(revOp, opName)>,
             VCMPXNoSDstTable<1, opName#"_e32">,
             VCMPVCMPXTable<opName#"_e32"> {
    let Defs = !if(DefExec, [VCC, EXEC], [VCC]);
    let SchedRW = P.Schedule;
    let isConvergent = DefExec;
    let isCompare = 1;
    let isCommutable = 1;
  }

  def _e64 : VOP3_Pseudo<opName, P, getVOPCPat64<cond, P>.ret>,
    Commutable_REV<revOp#"_e64", !eq(revOp, opName)>,
    VCMPXNoSDstTable<1, opName#"_e64">,
    VCMPVCMPXTable<opName#"_e64"> {
    let Defs = !if(DefExec, [EXEC], []);
    let SchedRW = P.Schedule;
    let isCompare = 1;
    let isCommutable = 1;
  }

  if P.HasExtSDWA then
  def _sdwa : VOPC_SDWA_Pseudo <opName, P> {
    let Defs = !if(DefExec, [EXEC], []);
    let SchedRW = P.Schedule;
    let isConvergent = DefExec;
    let isCompare = 1;
  }

  let SubtargetPredicate = isGFX11Plus in {
  if P.HasExtDPP then
      def _e32_dpp : VOP_DPP_Pseudo<opName, P> {
        let Defs = !if(DefExec, [VCC, EXEC], [VCC]);
        let SchedRW = P.Schedule;
        let isConvergent = DefExec;
        let isCompare = 1;
        let VOPC = 1;
        let Constraints = "";
      }
  if P.HasExtVOP3DPP then
      def _e64_dpp : VOP3_DPP_Pseudo<opName, P> {
        let Defs = !if(DefExec, [EXEC], []);
        let SchedRW = P.Schedule;
        let isCompare = 1;
        let Constraints = "";
    }
  } // end SubtargetPredicate = isGFX11Plus

}

let SubtargetPredicate = HasSdstCMPX in {
multiclass VOPCX_Pseudos <string opName,
                          VOPC_Profile P, VOPC_Profile P_NoSDst,
                          SDPatternOperator cond = COND_NULL,
                          string revOp = opName> :
           VOPC_Pseudos <opName, P, cond, revOp, 1> {

  def _nosdst_e32 : VOPC_Pseudo <opName#"_nosdst", P_NoSDst, [], 0>,
             Commutable_REV<revOp#"_nosdst_e32", !eq(revOp, opName)>,
             VCMPXNoSDstTable<0, opName#"_e32">,
             VCMPVCMPXTable<!subst("v_cmpx", "v_cmp", opName#"_e32")> {
    let Defs = [EXEC];
    let SchedRW = P_NoSDst.Schedule;
    let isConvergent = 1;
    let isCompare = 1;
    let isCommutable = 1;
    let SubtargetPredicate = HasNoSdstCMPX;
    let IsVCMPX = 1;
  }

  def _nosdst_e64 : VOP3_Pseudo<opName#"_nosdst", P_NoSDst>,
    Commutable_REV<revOp#"_nosdst_e64", !eq(revOp, opName)>,
    VCMPXNoSDstTable<0, opName#"_e64">,
    VCMPVCMPXTable<!subst("v_cmpx", "v_cmp", opName#"_e64")> {
    let Defs = [EXEC];
    let SchedRW = P_NoSDst.Schedule;
    let isCompare = 1;
    let isCommutable = 1;
    let SubtargetPredicate = HasNoSdstCMPX;
    let IsVCMPX = 1;
  }

  if P_NoSDst.HasExtSDWA then
  def _nosdst_sdwa : VOPC_SDWA_Pseudo <opName#"_nosdst", P_NoSDst> {
    let Defs = [EXEC];
    let SchedRW = P_NoSDst.Schedule;
    let isConvergent = 1;
    let isCompare = 1;
    let SubtargetPredicate = HasNoSdstCMPX;
  }

  let SubtargetPredicate = isGFX11Plus in {
  if P.HasExtDPP then
      def _nosdst_e32_dpp : VOP_DPP_Pseudo<opName#"_nosdst", P_NoSDst> {
        let Defs = [EXEC];
        let SchedRW = P_NoSDst.Schedule;
        let isConvergent = 1;
        let isCompare = 1;
        let VOPC = 1;
        let Constraints = "";
      }
  if P.HasExtVOP3DPP then
      def _nosdst_e64_dpp : VOP3_DPP_Pseudo<opName#"_nosdst", P_NoSDst> {
        let Defs = [EXEC];
        let SchedRW = P_NoSDst.Schedule;
        let isCompare = 1;
        let Constraints = "";
    }
  } // end SubtargetPredicate = isGFX11Plus
}
} // End SubtargetPredicate = HasSdstCMPX

defm VOPC_I1_F16_F16 : VOPC_Profile_t16<[Write32Bit], f16>;
def VOPC_I1_F32_F32 : VOPC_Profile<[Write32Bit], f32>;
def VOPC_I1_F64_F64 : VOPC_Profile<[WriteDoubleAdd], f64>;
defm VOPC_I1_I16_I16 : VOPC_Profile_t16<[Write32Bit], i16>;
def VOPC_I1_I32_I32 : VOPC_Profile<[Write32Bit], i32>;
def VOPC_I1_I64_I64 : VOPC_Profile<[Write64Bit], i64>;

defm VOPC_F16_F16 : VOPC_NoSdst_Profile_t16<[Write32Bit], f16>;
def VOPC_F32_F32 : VOPC_NoSdst_Profile<[Write32Bit], f32>;
def VOPC_F64_F64 : VOPC_NoSdst_Profile<[Write64Bit], f64>;
defm VOPC_I16_I16 : VOPC_NoSdst_Profile_t16<[Write32Bit], i16>;
def VOPC_I32_I32 : VOPC_NoSdst_Profile<[Write32Bit], i32>;
def VOPC_I64_I64 : VOPC_NoSdst_Profile<[Write64Bit], i64>;

multiclass VOPC_F16 <string opName, SDPatternOperator cond = COND_NULL,
                     string revOp = opName> {
  let OtherPredicates = [Has16BitInsts], True16Predicate = NotHasTrue16BitInsts in {
    defm NAME : VOPC_Pseudos <opName, VOPC_I1_F16_F16, cond, revOp, 0>;
  }
  let OtherPredicates = [HasTrue16BitInsts] in {
    defm _t16 : VOPC_Pseudos <opName#"_t16", VOPC_I1_F16_F16_t16, cond, revOp#"_t16", 0>;
  }
}

multiclass VOPC_F32 <string opName, SDPatternOperator cond = COND_NULL, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_F32_F32, cond, revOp, 0>;

multiclass VOPC_F64 <string opName, SDPatternOperator cond = COND_NULL, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_F64_F64, cond, revOp, 0>;

multiclass VOPC_I16 <string opName, SDPatternOperator cond = COND_NULL,
                     string revOp = opName> {
  let OtherPredicates = [Has16BitInsts], True16Predicate = NotHasTrue16BitInsts in {
    defm NAME : VOPC_Pseudos <opName, VOPC_I1_I16_I16, cond, revOp, 0>;
  }
  let OtherPredicates = [HasTrue16BitInsts] in {
    defm _t16 : VOPC_Pseudos <opName#"_t16", VOPC_I1_I16_I16_t16, cond, revOp#"_t16", 0>;
  }
}

multiclass VOPC_I32 <string opName, SDPatternOperator cond = COND_NULL, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_I32_I32, cond, revOp, 0>;

let IsInvalidSingleUseConsumer = 1 in {
  multiclass VOPC_I64 <string opName, SDPatternOperator cond = COND_NULL, string revOp = opName> :
    VOPC_Pseudos <opName, VOPC_I1_I64_I64, cond, revOp, 0>;
}

multiclass VOPCX_F16<string opName, string revOp = opName> {
  let OtherPredicates = [Has16BitInsts], True16Predicate = NotHasTrue16BitInsts in {
    defm NAME : VOPCX_Pseudos <opName, VOPC_I1_F16_F16, VOPC_F16_F16, COND_NULL, revOp>;
  }
  let OtherPredicates = [HasTrue16BitInsts] in {
    defm _t16 : VOPCX_Pseudos <opName#"_t16", VOPC_I1_F16_F16_t16, VOPC_F16_F16_t16, COND_NULL, revOp#"_t16">;
  }
}

multiclass VOPCX_F32 <string opName, string revOp = opName> :
  VOPCX_Pseudos <opName, VOPC_I1_F32_F32, VOPC_F32_F32, COND_NULL, revOp>;

multiclass VOPCX_F64 <string opName, string revOp = opName> :
  VOPCX_Pseudos <opName, VOPC_I1_F64_F64, VOPC_F64_F64, COND_NULL, revOp>;

multiclass VOPCX_I16<string opName, string revOp = opName> {
  let OtherPredicates = [Has16BitInsts], True16Predicate = NotHasTrue16BitInsts in {
    defm NAME : VOPCX_Pseudos <opName, VOPC_I1_I16_I16, VOPC_I16_I16, COND_NULL, revOp>;
  }
  let OtherPredicates = [HasTrue16BitInsts] in {
    defm _t16 : VOPCX_Pseudos <opName#"_t16", VOPC_I1_I16_I16_t16, VOPC_I16_I16_t16, COND_NULL, revOp#"_t16">;
  }
}

multiclass VOPCX_I32 <string opName, string revOp = opName> :
  VOPCX_Pseudos <opName, VOPC_I1_I32_I32, VOPC_I32_I32, COND_NULL, revOp>;

let IsInvalidSingleUseConsumer = 1 in {
  multiclass VOPCX_I64 <string opName, string revOp = opName> :
    VOPCX_Pseudos <opName, VOPC_I1_I64_I64, VOPC_I64_I64, COND_NULL, revOp>;
}


//===----------------------------------------------------------------------===//
// Compare instructions
//===----------------------------------------------------------------------===//

defm V_CMP_F_F32 : VOPC_F32 <"v_cmp_f_f32">;
defm V_CMP_LT_F32 : VOPC_F32 <"v_cmp_lt_f32", COND_OLT, "v_cmp_gt_f32">;
defm V_CMP_EQ_F32 : VOPC_F32 <"v_cmp_eq_f32", COND_OEQ>;
defm V_CMP_LE_F32 : VOPC_F32 <"v_cmp_le_f32", COND_OLE, "v_cmp_ge_f32">;
defm V_CMP_GT_F32 : VOPC_F32 <"v_cmp_gt_f32", COND_OGT>;
defm V_CMP_LG_F32 : VOPC_F32 <"v_cmp_lg_f32", COND_ONE>;
defm V_CMP_GE_F32 : VOPC_F32 <"v_cmp_ge_f32", COND_OGE>;
defm V_CMP_O_F32 : VOPC_F32 <"v_cmp_o_f32", COND_O>;
defm V_CMP_U_F32 : VOPC_F32 <"v_cmp_u_f32", COND_UO>;
defm V_CMP_NGE_F32 : VOPC_F32 <"v_cmp_nge_f32",  COND_ULT, "v_cmp_nle_f32">;
defm V_CMP_NLG_F32 : VOPC_F32 <"v_cmp_nlg_f32", COND_UEQ>;
defm V_CMP_NGT_F32 : VOPC_F32 <"v_cmp_ngt_f32", COND_ULE, "v_cmp_nlt_f32">;
defm V_CMP_NLE_F32 : VOPC_F32 <"v_cmp_nle_f32", COND_UGT>;
defm V_CMP_NEQ_F32 : VOPC_F32 <"v_cmp_neq_f32", COND_UNE>;
defm V_CMP_NLT_F32 : VOPC_F32 <"v_cmp_nlt_f32", COND_UGE>;
defm V_CMP_TRU_F32 : VOPC_F32 <"v_cmp_tru_f32">;

defm V_CMPX_F_F32 : VOPCX_F32 <"v_cmpx_f_f32">;
defm V_CMPX_LT_F32 : VOPCX_F32 <"v_cmpx_lt_f32", "v_cmpx_gt_f32">;
defm V_CMPX_EQ_F32 : VOPCX_F32 <"v_cmpx_eq_f32">;
defm V_CMPX_LE_F32 : VOPCX_F32 <"v_cmpx_le_f32", "v_cmpx_ge_f32">;
defm V_CMPX_GT_F32 : VOPCX_F32 <"v_cmpx_gt_f32">;
defm V_CMPX_LG_F32 : VOPCX_F32 <"v_cmpx_lg_f32">;
defm V_CMPX_GE_F32 : VOPCX_F32 <"v_cmpx_ge_f32">;
defm V_CMPX_O_F32 : VOPCX_F32 <"v_cmpx_o_f32">;
defm V_CMPX_U_F32 : VOPCX_F32 <"v_cmpx_u_f32">;
defm V_CMPX_NGE_F32 : VOPCX_F32 <"v_cmpx_nge_f32", "v_cmpx_nle_f32">;
defm V_CMPX_NLG_F32 : VOPCX_F32 <"v_cmpx_nlg_f32">;
defm V_CMPX_NGT_F32 : VOPCX_F32 <"v_cmpx_ngt_f32", "v_cmpx_nlt_f32">;
defm V_CMPX_NLE_F32 : VOPCX_F32 <"v_cmpx_nle_f32">;
defm V_CMPX_NEQ_F32 : VOPCX_F32 <"v_cmpx_neq_f32">;
defm V_CMPX_NLT_F32 : VOPCX_F32 <"v_cmpx_nlt_f32">;
defm V_CMPX_TRU_F32 : VOPCX_F32 <"v_cmpx_tru_f32">;

defm V_CMP_F_F64 : VOPC_F64 <"v_cmp_f_f64">;
defm V_CMP_LT_F64 : VOPC_F64 <"v_cmp_lt_f64", COND_OLT, "v_cmp_gt_f64">;
defm V_CMP_EQ_F64 : VOPC_F64 <"v_cmp_eq_f64", COND_OEQ>;
defm V_CMP_LE_F64 : VOPC_F64 <"v_cmp_le_f64", COND_OLE, "v_cmp_ge_f64">;
defm V_CMP_GT_F64 : VOPC_F64 <"v_cmp_gt_f64", COND_OGT>;
defm V_CMP_LG_F64 : VOPC_F64 <"v_cmp_lg_f64", COND_ONE>;
defm V_CMP_GE_F64 : VOPC_F64 <"v_cmp_ge_f64", COND_OGE>;
defm V_CMP_O_F64 : VOPC_F64 <"v_cmp_o_f64", COND_O>;
defm V_CMP_U_F64 : VOPC_F64 <"v_cmp_u_f64", COND_UO>;
defm V_CMP_NGE_F64 : VOPC_F64 <"v_cmp_nge_f64", COND_ULT, "v_cmp_nle_f64">;
defm V_CMP_NLG_F64 : VOPC_F64 <"v_cmp_nlg_f64", COND_UEQ>;
defm V_CMP_NGT_F64 : VOPC_F64 <"v_cmp_ngt_f64", COND_ULE, "v_cmp_nlt_f64">;
defm V_CMP_NLE_F64 : VOPC_F64 <"v_cmp_nle_f64", COND_UGT>;
defm V_CMP_NEQ_F64 : VOPC_F64 <"v_cmp_neq_f64", COND_UNE>;
defm V_CMP_NLT_F64 : VOPC_F64 <"v_cmp_nlt_f64", COND_UGE>;
defm V_CMP_TRU_F64 : VOPC_F64 <"v_cmp_tru_f64">;

defm V_CMPX_F_F64 : VOPCX_F64 <"v_cmpx_f_f64">;
defm V_CMPX_LT_F64 : VOPCX_F64 <"v_cmpx_lt_f64", "v_cmpx_gt_f64">;
defm V_CMPX_EQ_F64 : VOPCX_F64 <"v_cmpx_eq_f64">;
defm V_CMPX_LE_F64 : VOPCX_F64 <"v_cmpx_le_f64", "v_cmpx_ge_f64">;
defm V_CMPX_GT_F64 : VOPCX_F64 <"v_cmpx_gt_f64">;
defm V_CMPX_LG_F64 : VOPCX_F64 <"v_cmpx_lg_f64">;
defm V_CMPX_GE_F64 : VOPCX_F64 <"v_cmpx_ge_f64">;
defm V_CMPX_O_F64 : VOPCX_F64 <"v_cmpx_o_f64">;
defm V_CMPX_U_F64 : VOPCX_F64 <"v_cmpx_u_f64">;
defm V_CMPX_NGE_F64 : VOPCX_F64 <"v_cmpx_nge_f64", "v_cmpx_nle_f64">;
defm V_CMPX_NLG_F64 : VOPCX_F64 <"v_cmpx_nlg_f64">;
defm V_CMPX_NGT_F64 : VOPCX_F64 <"v_cmpx_ngt_f64", "v_cmpx_nlt_f64">;
defm V_CMPX_NLE_F64 : VOPCX_F64 <"v_cmpx_nle_f64">;
defm V_CMPX_NEQ_F64 : VOPCX_F64 <"v_cmpx_neq_f64">;
defm V_CMPX_NLT_F64 : VOPCX_F64 <"v_cmpx_nlt_f64">;
defm V_CMPX_TRU_F64 : VOPCX_F64 <"v_cmpx_tru_f64">;

let SubtargetPredicate = isGFX6GFX7 in {

defm V_CMPS_F_F32 : VOPC_F32 <"v_cmps_f_f32">;
defm V_CMPS_LT_F32 : VOPC_F32 <"v_cmps_lt_f32", COND_NULL, "v_cmps_gt_f32">;
defm V_CMPS_EQ_F32 : VOPC_F32 <"v_cmps_eq_f32">;
defm V_CMPS_LE_F32 : VOPC_F32 <"v_cmps_le_f32", COND_NULL, "v_cmps_ge_f32">;
defm V_CMPS_GT_F32 : VOPC_F32 <"v_cmps_gt_f32">;
defm V_CMPS_LG_F32 : VOPC_F32 <"v_cmps_lg_f32">;
defm V_CMPS_GE_F32 : VOPC_F32 <"v_cmps_ge_f32">;
defm V_CMPS_O_F32 : VOPC_F32 <"v_cmps_o_f32">;
defm V_CMPS_U_F32 : VOPC_F32 <"v_cmps_u_f32">;
defm V_CMPS_NGE_F32 : VOPC_F32 <"v_cmps_nge_f32", COND_NULL, "v_cmps_nle_f32">;
defm V_CMPS_NLG_F32 : VOPC_F32 <"v_cmps_nlg_f32">;
defm V_CMPS_NGT_F32 : VOPC_F32 <"v_cmps_ngt_f32", COND_NULL, "v_cmps_nlt_f32">;
defm V_CMPS_NLE_F32 : VOPC_F32 <"v_cmps_nle_f32">;
defm V_CMPS_NEQ_F32 : VOPC_F32 <"v_cmps_neq_f32">;
defm V_CMPS_NLT_F32 : VOPC_F32 <"v_cmps_nlt_f32">;
defm V_CMPS_TRU_F32 : VOPC_F32 <"v_cmps_tru_f32">;

defm V_CMPSX_F_F32 : VOPCX_F32 <"v_cmpsx_f_f32">;
defm V_CMPSX_LT_F32 : VOPCX_F32 <"v_cmpsx_lt_f32", "v_cmpsx_gt_f32">;
defm V_CMPSX_EQ_F32 : VOPCX_F32 <"v_cmpsx_eq_f32">;
defm V_CMPSX_LE_F32 : VOPCX_F32 <"v_cmpsx_le_f32", "v_cmpsx_ge_f32">;
defm V_CMPSX_GT_F32 : VOPCX_F32 <"v_cmpsx_gt_f32">;
defm V_CMPSX_LG_F32 : VOPCX_F32 <"v_cmpsx_lg_f32">;
defm V_CMPSX_GE_F32 : VOPCX_F32 <"v_cmpsx_ge_f32">;
defm V_CMPSX_O_F32 : VOPCX_F32 <"v_cmpsx_o_f32">;
defm V_CMPSX_U_F32 : VOPCX_F32 <"v_cmpsx_u_f32">;
defm V_CMPSX_NGE_F32 : VOPCX_F32 <"v_cmpsx_nge_f32", "v_cmpsx_nle_f32">;
defm V_CMPSX_NLG_F32 : VOPCX_F32 <"v_cmpsx_nlg_f32">;
defm V_CMPSX_NGT_F32 : VOPCX_F32 <"v_cmpsx_ngt_f32", "v_cmpsx_nlt_f32">;
defm V_CMPSX_NLE_F32 : VOPCX_F32 <"v_cmpsx_nle_f32">;
defm V_CMPSX_NEQ_F32 : VOPCX_F32 <"v_cmpsx_neq_f32">;
defm V_CMPSX_NLT_F32 : VOPCX_F32 <"v_cmpsx_nlt_f32">;
defm V_CMPSX_TRU_F32 : VOPCX_F32 <"v_cmpsx_tru_f32">;

defm V_CMPS_F_F64 : VOPC_F64 <"v_cmps_f_f64">;
defm V_CMPS_LT_F64 : VOPC_F64 <"v_cmps_lt_f64", COND_NULL, "v_cmps_gt_f64">;
defm V_CMPS_EQ_F64 : VOPC_F64 <"v_cmps_eq_f64">;
defm V_CMPS_LE_F64 : VOPC_F64 <"v_cmps_le_f64", COND_NULL, "v_cmps_ge_f64">;
defm V_CMPS_GT_F64 : VOPC_F64 <"v_cmps_gt_f64">;
defm V_CMPS_LG_F64 : VOPC_F64 <"v_cmps_lg_f64">;
defm V_CMPS_GE_F64 : VOPC_F64 <"v_cmps_ge_f64">;
defm V_CMPS_O_F64 : VOPC_F64 <"v_cmps_o_f64">;
defm V_CMPS_U_F64 : VOPC_F64 <"v_cmps_u_f64">;
defm V_CMPS_NGE_F64 : VOPC_F64 <"v_cmps_nge_f64", COND_NULL, "v_cmps_nle_f64">;
defm V_CMPS_NLG_F64 : VOPC_F64 <"v_cmps_nlg_f64">;
defm V_CMPS_NGT_F64 : VOPC_F64 <"v_cmps_ngt_f64", COND_NULL, "v_cmps_nlt_f64">;
defm V_CMPS_NLE_F64 : VOPC_F64 <"v_cmps_nle_f64">;
defm V_CMPS_NEQ_F64 : VOPC_F64 <"v_cmps_neq_f64">;
defm V_CMPS_NLT_F64 : VOPC_F64 <"v_cmps_nlt_f64">;
defm V_CMPS_TRU_F64 : VOPC_F64 <"v_cmps_tru_f64">;

defm V_CMPSX_F_F64 : VOPCX_F64 <"v_cmpsx_f_f64">;
defm V_CMPSX_LT_F64 : VOPCX_F64 <"v_cmpsx_lt_f64", "v_cmpsx_gt_f64">;
defm V_CMPSX_EQ_F64 : VOPCX_F64 <"v_cmpsx_eq_f64">;
defm V_CMPSX_LE_F64 : VOPCX_F64 <"v_cmpsx_le_f64", "v_cmpsx_ge_f64">;
defm V_CMPSX_GT_F64 : VOPCX_F64 <"v_cmpsx_gt_f64">;
defm V_CMPSX_LG_F64 : VOPCX_F64 <"v_cmpsx_lg_f64">;
defm V_CMPSX_GE_F64 : VOPCX_F64 <"v_cmpsx_ge_f64">;
defm V_CMPSX_O_F64 : VOPCX_F64 <"v_cmpsx_o_f64">;
defm V_CMPSX_U_F64 : VOPCX_F64 <"v_cmpsx_u_f64">;
defm V_CMPSX_NGE_F64 : VOPCX_F64 <"v_cmpsx_nge_f64", "v_cmpsx_nle_f64">;
defm V_CMPSX_NLG_F64 : VOPCX_F64 <"v_cmpsx_nlg_f64">;
defm V_CMPSX_NGT_F64 : VOPCX_F64 <"v_cmpsx_ngt_f64", "v_cmpsx_nlt_f64">;
defm V_CMPSX_NLE_F64 : VOPCX_F64 <"v_cmpsx_nle_f64">;
defm V_CMPSX_NEQ_F64 : VOPCX_F64 <"v_cmpsx_neq_f64">;
defm V_CMPSX_NLT_F64 : VOPCX_F64 <"v_cmpsx_nlt_f64">;
defm V_CMPSX_TRU_F64 : VOPCX_F64 <"v_cmpsx_tru_f64">;

} // End SubtargetPredicate = isGFX6GFX7

let SubtargetPredicate = Has16BitInsts in {

defm V_CMP_F_F16    : VOPC_F16 <"v_cmp_f_f16">;
defm V_CMP_LT_F16   : VOPC_F16 <"v_cmp_lt_f16", COND_OLT, "v_cmp_gt_f16">;
defm V_CMP_EQ_F16   : VOPC_F16 <"v_cmp_eq_f16", COND_OEQ>;
defm V_CMP_LE_F16   : VOPC_F16 <"v_cmp_le_f16", COND_OLE, "v_cmp_ge_f16">;
defm V_CMP_GT_F16   : VOPC_F16 <"v_cmp_gt_f16", COND_OGT>;
defm V_CMP_LG_F16   : VOPC_F16 <"v_cmp_lg_f16", COND_ONE>;
defm V_CMP_GE_F16   : VOPC_F16 <"v_cmp_ge_f16", COND_OGE>;
defm V_CMP_O_F16    : VOPC_F16 <"v_cmp_o_f16", COND_O>;
defm V_CMP_U_F16    : VOPC_F16 <"v_cmp_u_f16", COND_UO>;
defm V_CMP_NGE_F16  : VOPC_F16 <"v_cmp_nge_f16", COND_ULT, "v_cmp_nle_f16">;
defm V_CMP_NLG_F16  : VOPC_F16 <"v_cmp_nlg_f16", COND_UEQ>;
defm V_CMP_NGT_F16  : VOPC_F16 <"v_cmp_ngt_f16", COND_ULE, "v_cmp_nlt_f16">;
defm V_CMP_NLE_F16  : VOPC_F16 <"v_cmp_nle_f16", COND_UGT>;
defm V_CMP_NEQ_F16  : VOPC_F16 <"v_cmp_neq_f16", COND_UNE>;
defm V_CMP_NLT_F16  : VOPC_F16 <"v_cmp_nlt_f16", COND_UGE>;
defm V_CMP_TRU_F16  : VOPC_F16 <"v_cmp_tru_f16">;

defm V_CMPX_F_F16   : VOPCX_F16 <"v_cmpx_f_f16">;
defm V_CMPX_LT_F16  : VOPCX_F16 <"v_cmpx_lt_f16", "v_cmpx_gt_f16">;
defm V_CMPX_EQ_F16  : VOPCX_F16 <"v_cmpx_eq_f16">;
defm V_CMPX_LE_F16  : VOPCX_F16 <"v_cmpx_le_f16", "v_cmpx_ge_f16">;
defm V_CMPX_GT_F16  : VOPCX_F16 <"v_cmpx_gt_f16">;
defm V_CMPX_LG_F16  : VOPCX_F16 <"v_cmpx_lg_f16">;
defm V_CMPX_GE_F16  : VOPCX_F16 <"v_cmpx_ge_f16">;
defm V_CMPX_O_F16   : VOPCX_F16 <"v_cmpx_o_f16">;
defm V_CMPX_U_F16   : VOPCX_F16 <"v_cmpx_u_f16">;
defm V_CMPX_NGE_F16 : VOPCX_F16 <"v_cmpx_nge_f16", "v_cmpx_nle_f16">;
defm V_CMPX_NLG_F16 : VOPCX_F16 <"v_cmpx_nlg_f16">;
defm V_CMPX_NGT_F16 : VOPCX_F16 <"v_cmpx_ngt_f16", "v_cmpx_nlt_f16">;
defm V_CMPX_NLE_F16 : VOPCX_F16 <"v_cmpx_nle_f16">;
defm V_CMPX_NEQ_F16 : VOPCX_F16 <"v_cmpx_neq_f16">;
defm V_CMPX_NLT_F16 : VOPCX_F16 <"v_cmpx_nlt_f16">;
defm V_CMPX_TRU_F16 : VOPCX_F16 <"v_cmpx_tru_f16">;

defm V_CMP_F_I16 : VOPC_I16 <"v_cmp_f_i16">;
defm V_CMP_LT_I16 : VOPC_I16 <"v_cmp_lt_i16", COND_SLT, "v_cmp_gt_i16">;
defm V_CMP_EQ_I16 : VOPC_I16 <"v_cmp_eq_i16">;
defm V_CMP_LE_I16 : VOPC_I16 <"v_cmp_le_i16", COND_SLE, "v_cmp_ge_i16">;
defm V_CMP_GT_I16 : VOPC_I16 <"v_cmp_gt_i16", COND_SGT>;
defm V_CMP_NE_I16 : VOPC_I16 <"v_cmp_ne_i16">;
defm V_CMP_GE_I16 : VOPC_I16 <"v_cmp_ge_i16", COND_SGE>;
defm V_CMP_T_I16 : VOPC_I16 <"v_cmp_t_i16">;

defm V_CMP_F_U16 : VOPC_I16 <"v_cmp_f_u16">;
defm V_CMP_LT_U16 : VOPC_I16 <"v_cmp_lt_u16", COND_ULT, "v_cmp_gt_u16">;
defm V_CMP_EQ_U16 : VOPC_I16 <"v_cmp_eq_u16", COND_EQ>;
defm V_CMP_LE_U16 : VOPC_I16 <"v_cmp_le_u16", COND_ULE, "v_cmp_ge_u16">;
defm V_CMP_GT_U16 : VOPC_I16 <"v_cmp_gt_u16", COND_UGT>;
defm V_CMP_NE_U16 : VOPC_I16 <"v_cmp_ne_u16", COND_NE>;
defm V_CMP_GE_U16 : VOPC_I16 <"v_cmp_ge_u16", COND_UGE>;
defm V_CMP_T_U16 : VOPC_I16 <"v_cmp_t_u16">;

defm V_CMPX_F_I16 : VOPCX_I16 <"v_cmpx_f_i16">;
defm V_CMPX_LT_I16 : VOPCX_I16 <"v_cmpx_lt_i16", "v_cmpx_gt_i16">;
defm V_CMPX_EQ_I16 : VOPCX_I16 <"v_cmpx_eq_i16">;
defm V_CMPX_LE_I16 : VOPCX_I16 <"v_cmpx_le_i16", "v_cmpx_ge_i16">;
defm V_CMPX_GT_I16 : VOPCX_I16 <"v_cmpx_gt_i16">;
defm V_CMPX_NE_I16 : VOPCX_I16 <"v_cmpx_ne_i16">;
defm V_CMPX_GE_I16 : VOPCX_I16 <"v_cmpx_ge_i16">;
defm V_CMPX_T_I16 : VOPCX_I16 <"v_cmpx_t_i16">;

defm V_CMPX_F_U16 : VOPCX_I16 <"v_cmpx_f_u16">;
defm V_CMPX_LT_U16 : VOPCX_I16 <"v_cmpx_lt_u16", "v_cmpx_gt_u16">;
defm V_CMPX_EQ_U16 : VOPCX_I16 <"v_cmpx_eq_u16">;
defm V_CMPX_LE_U16 : VOPCX_I16 <"v_cmpx_le_u16", "v_cmpx_ge_u16">;
defm V_CMPX_GT_U16 : VOPCX_I16 <"v_cmpx_gt_u16">;
defm V_CMPX_NE_U16 : VOPCX_I16 <"v_cmpx_ne_u16">;
defm V_CMPX_GE_U16 : VOPCX_I16 <"v_cmpx_ge_u16">;
defm V_CMPX_T_U16 : VOPCX_I16 <"v_cmpx_t_u16">;

} // End SubtargetPredicate = Has16BitInsts

defm V_CMP_F_I32 : VOPC_I32 <"v_cmp_f_i32">;
defm V_CMP_LT_I32 : VOPC_I32 <"v_cmp_lt_i32", COND_SLT, "v_cmp_gt_i32">;
defm V_CMP_EQ_I32 : VOPC_I32 <"v_cmp_eq_i32">;
defm V_CMP_LE_I32 : VOPC_I32 <"v_cmp_le_i32", COND_SLE, "v_cmp_ge_i32">;
defm V_CMP_GT_I32 : VOPC_I32 <"v_cmp_gt_i32", COND_SGT>;
defm V_CMP_NE_I32 : VOPC_I32 <"v_cmp_ne_i32">;
defm V_CMP_GE_I32 : VOPC_I32 <"v_cmp_ge_i32", COND_SGE>;
defm V_CMP_T_I32 : VOPC_I32 <"v_cmp_t_i32">;

defm V_CMPX_F_I32 : VOPCX_I32 <"v_cmpx_f_i32">;
defm V_CMPX_LT_I32 : VOPCX_I32 <"v_cmpx_lt_i32", "v_cmpx_gt_i32">;
defm V_CMPX_EQ_I32 : VOPCX_I32 <"v_cmpx_eq_i32">;
defm V_CMPX_LE_I32 : VOPCX_I32 <"v_cmpx_le_i32", "v_cmpx_ge_i32">;
defm V_CMPX_GT_I32 : VOPCX_I32 <"v_cmpx_gt_i32">;
defm V_CMPX_NE_I32 : VOPCX_I32 <"v_cmpx_ne_i32">;
defm V_CMPX_GE_I32 : VOPCX_I32 <"v_cmpx_ge_i32">;
defm V_CMPX_T_I32 : VOPCX_I32 <"v_cmpx_t_i32">;

defm V_CMP_F_I64 : VOPC_I64 <"v_cmp_f_i64">;
defm V_CMP_LT_I64 : VOPC_I64 <"v_cmp_lt_i64", COND_SLT, "v_cmp_gt_i64">;
defm V_CMP_EQ_I64 : VOPC_I64 <"v_cmp_eq_i64">;
defm V_CMP_LE_I64 : VOPC_I64 <"v_cmp_le_i64", COND_SLE, "v_cmp_ge_i64">;
defm V_CMP_GT_I64 : VOPC_I64 <"v_cmp_gt_i64", COND_SGT>;
defm V_CMP_NE_I64 : VOPC_I64 <"v_cmp_ne_i64">;
defm V_CMP_GE_I64 : VOPC_I64 <"v_cmp_ge_i64", COND_SGE>;
defm V_CMP_T_I64 : VOPC_I64 <"v_cmp_t_i64">;

defm V_CMPX_F_I64 : VOPCX_I64 <"v_cmpx_f_i64">;
defm V_CMPX_LT_I64 : VOPCX_I64 <"v_cmpx_lt_i64", "v_cmpx_gt_i64">;
defm V_CMPX_EQ_I64 : VOPCX_I64 <"v_cmpx_eq_i64">;
defm V_CMPX_LE_I64 : VOPCX_I64 <"v_cmpx_le_i64", "v_cmpx_ge_i64">;
defm V_CMPX_GT_I64 : VOPCX_I64 <"v_cmpx_gt_i64">;
defm V_CMPX_NE_I64 : VOPCX_I64 <"v_cmpx_ne_i64">;
defm V_CMPX_GE_I64 : VOPCX_I64 <"v_cmpx_ge_i64">;
defm V_CMPX_T_I64 : VOPCX_I64 <"v_cmpx_t_i64">;

defm V_CMP_F_U32 : VOPC_I32 <"v_cmp_f_u32">;
defm V_CMP_LT_U32 : VOPC_I32 <"v_cmp_lt_u32", COND_ULT, "v_cmp_gt_u32">;
defm V_CMP_EQ_U32 : VOPC_I32 <"v_cmp_eq_u32", COND_EQ>;
defm V_CMP_LE_U32 : VOPC_I32 <"v_cmp_le_u32", COND_ULE, "v_cmp_ge_u32">;
defm V_CMP_GT_U32 : VOPC_I32 <"v_cmp_gt_u32", COND_UGT>;
defm V_CMP_NE_U32 : VOPC_I32 <"v_cmp_ne_u32", COND_NE>;
defm V_CMP_GE_U32 : VOPC_I32 <"v_cmp_ge_u32", COND_UGE>;
defm V_CMP_T_U32 : VOPC_I32 <"v_cmp_t_u32">;

defm V_CMPX_F_U32 : VOPCX_I32 <"v_cmpx_f_u32">;
defm V_CMPX_LT_U32 : VOPCX_I32 <"v_cmpx_lt_u32", "v_cmpx_gt_u32">;
defm V_CMPX_EQ_U32 : VOPCX_I32 <"v_cmpx_eq_u32">;
defm V_CMPX_LE_U32 : VOPCX_I32 <"v_cmpx_le_u32", "v_cmpx_le_u32">;
defm V_CMPX_GT_U32 : VOPCX_I32 <"v_cmpx_gt_u32">;
defm V_CMPX_NE_U32 : VOPCX_I32 <"v_cmpx_ne_u32">;
defm V_CMPX_GE_U32 : VOPCX_I32 <"v_cmpx_ge_u32">;
defm V_CMPX_T_U32 : VOPCX_I32 <"v_cmpx_t_u32">;

defm V_CMP_F_U64 : VOPC_I64 <"v_cmp_f_u64">;
defm V_CMP_LT_U64 : VOPC_I64 <"v_cmp_lt_u64", COND_ULT, "v_cmp_gt_u64">;
defm V_CMP_EQ_U64 : VOPC_I64 <"v_cmp_eq_u64", COND_EQ>;
defm V_CMP_LE_U64 : VOPC_I64 <"v_cmp_le_u64", COND_ULE, "v_cmp_ge_u64">;
defm V_CMP_GT_U64 : VOPC_I64 <"v_cmp_gt_u64", COND_UGT>;
defm V_CMP_NE_U64 : VOPC_I64 <"v_cmp_ne_u64", COND_NE>;
defm V_CMP_GE_U64 : VOPC_I64 <"v_cmp_ge_u64", COND_UGE>;
defm V_CMP_T_U64 : VOPC_I64 <"v_cmp_t_u64">;

defm V_CMPX_F_U64 : VOPCX_I64 <"v_cmpx_f_u64">;
defm V_CMPX_LT_U64 : VOPCX_I64 <"v_cmpx_lt_u64", "v_cmpx_gt_u64">;
defm V_CMPX_EQ_U64 : VOPCX_I64 <"v_cmpx_eq_u64">;
defm V_CMPX_LE_U64 : VOPCX_I64 <"v_cmpx_le_u64", "v_cmpx_ge_u64">;
defm V_CMPX_GT_U64 : VOPCX_I64 <"v_cmpx_gt_u64">;
defm V_CMPX_NE_U64 : VOPCX_I64 <"v_cmpx_ne_u64">;
defm V_CMPX_GE_U64 : VOPCX_I64 <"v_cmpx_ge_u64">;
defm V_CMPX_T_U64 : VOPCX_I64 <"v_cmpx_t_u64">;

//===----------------------------------------------------------------------===//
// Class instructions
//===----------------------------------------------------------------------===//

class VOPC_Class_Profile<list<SchedReadWrite> sched, ValueType src0VT, ValueType src1VT = i32> :
  VOPC_Profile<sched, src0VT, src1VT> {
  let AsmDPP = "$src0_modifiers, $src1 $dpp_ctrl$row_mask$bank_mask$bound_ctrl";
  let AsmDPP16 = AsmDPP#"$fi";
    let InsDPP = (ins Src0ModDPP:$src0_modifiers, Src0DPP:$src0, Src1DPP:$src1, dpp_ctrl:$dpp_ctrl, DppRowMask:$row_mask, DppBankMask:$bank_mask, DppBoundCtrl:$bound_ctrl);
  let InsDPP16 = !con(InsDPP, (ins Dpp16FI:$fi));
  // DPP8 forbids modifiers and can inherit from VOPC_Profile

  let Ins64 = (ins Src0Mod:$src0_modifiers, Src0RC64:$src0, Src1RC64:$src1);
  dag InsPartVOP3DPP = (ins FPVRegInputMods:$src0_modifiers, VGPRSrc_32:$src0, VCSrc_b32:$src1);
  let InsVOP3Base = !con(InsPartVOP3DPP, !if(HasOpSel, (ins op_sel0:$op_sel),
                                                       (ins)));
  let AsmVOP3Base = "$sdst, $src0_modifiers, $src1";

  let InsSDWA = (ins Src0ModSDWA:$src0_modifiers, Src0SDWA:$src0,
                     Src1ModSDWA:$src1_modifiers, Src1SDWA:$src1,
                     Clamp:$clamp, src0_sel:$src0_sel, src1_sel:$src1_sel);

  let AsmSDWA = " vcc, $src0_modifiers, $src1_modifiers$clamp $src0_sel $src1_sel";
  let HasSrc1Mods = 0;
  let HasClamp = 0;
  let HasOMod = 0;
}

multiclass VOPC_Class_Profile_t16<list<SchedReadWrite> sched> {
  def NAME : VOPC_Class_Profile<sched, f16>;
  def _t16 : VOPC_Class_Profile<sched, f16, i16> {
    let IsTrue16 = 1;
    let Src1RC32 = getVregSrcForVT<Src1VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src1RC64 = VSrc_b32;
    let Src0DPP = getVregSrcForVT<Src0VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src1DPP = getVregSrcForVT<Src1VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src2DPP = getVregSrcForVT<Src2VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src0ModDPP = getSrcModDPP_t16<Src0VT>.ret;
    let Src1ModDPP = getSrcModDPP_t16<Src1VT>.ret;
    let Src2ModDPP = getSrcModDPP_t16<Src2VT>.ret;
  }
}

class VOPC_Class_NoSdst_Profile<list<SchedReadWrite> sched, ValueType src0VT, ValueType src1VT = i32> :
  VOPC_Class_Profile<sched, src0VT, src1VT> {
  let Outs64 = (outs );
  let OutsSDWA = (outs );
  let InsSDWA = (ins Src0ModSDWA:$src0_modifiers, Src0SDWA:$src0,
                     Src1ModSDWA:$src1_modifiers, Src1SDWA:$src1,
                     src0_sel:$src0_sel, src1_sel:$src1_sel);
  let AsmVOP3Base = "$src0_modifiers, $src1";
  let AsmSDWA9 = "$src0_modifiers, $src1_modifiers $src0_sel $src1_sel";
  let EmitDst = 0;
}

multiclass VOPC_Class_NoSdst_Profile_t16<list<SchedReadWrite> sched> {
  def NAME : VOPC_Class_NoSdst_Profile<sched, f16>;
  def _t16 : VOPC_Class_NoSdst_Profile<sched, f16, i16> {
    let IsTrue16 = 1;
    let Src1RC32 = getVregSrcForVT<Src1VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src1RC64 = VSrc_b32;
    let Src0DPP = getVregSrcForVT<Src0VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src1DPP = getVregSrcForVT<Src1VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src2DPP = getVregSrcForVT<Src2VT, 1/*IsTrue16*/, 1/*IsFake16*/>.ret;
    let Src0ModDPP = getSrcModDPP_t16<Src0VT>.ret;
    let Src1ModDPP = getSrcModDPP_t16<Src1VT>.ret;
    let Src2ModDPP = getSrcModDPP_t16<Src2VT>.ret;
  }
}

class getVOPCClassPat64 <VOPProfile P> {
  list<dag> ret =
    [(set i1:$sdst,
      (AMDGPUfp_class
        (P.Src0VT (VOP3ModsNonCanonicalizing P.Src0VT:$src0, i32:$src0_modifiers)),
        i32:$src1))];
}


// Special case for class instructions which only have modifiers on
// the 1st source operand.
multiclass VOPC_Class_Pseudos <string opName, VOPC_Profile p, bit DefExec,
                               bit DefVcc = 1> {
  def _e32 : VOPC_Pseudo <opName, p>,
             VCMPXNoSDstTable<1, opName#"_e32"> {
    let Defs = !if(DefExec, !if(DefVcc, [VCC, EXEC], [EXEC]),
                            !if(DefVcc, [VCC], []));
    let SchedRW = p.Schedule;
    let isConvergent = DefExec;
  }

  def _e64 : VOP3_Pseudo<opName, p, getVOPCClassPat64<p>.ret>,
             VCMPXNoSDstTable<1, opName#"_e64"> {
    let Defs = !if(DefExec, [EXEC], []);
    let SchedRW = p.Schedule;
  }

  if p.HasExtSDWA then
  def _sdwa : VOPC_SDWA_Pseudo <opName, p> {
    let Defs = !if(DefExec, !if(DefVcc, [VCC, EXEC], [EXEC]),
                            !if(DefVcc, [VCC], []));
    let SchedRW = p.Schedule;
    let isConvergent = DefExec;
  }

  let SubtargetPredicate = isGFX11Plus in {
  if p.HasExtDPP then
      def _e32_dpp : VOP_DPP_Pseudo<opName, p> {
        let Defs = !if(DefExec, !if(DefVcc, [VCC, EXEC], [EXEC]),
                                !if(DefVcc, [VCC], []));
        let SchedRW = p.Schedule;
        let isConvergent = DefExec;
        let VOPC = 1;
        let Constraints = "";
      }
  if p.HasExtVOP3DPP then
      def _e64_dpp : VOP3_DPP_Pseudo<opName, p> {
        let Defs = !if(DefExec, [EXEC], []);
        let SchedRW = p.Schedule;
        let Constraints = "";
    }
  } // end SubtargetPredicate = isGFX11Plus
}

let SubtargetPredicate = HasSdstCMPX in {
multiclass VOPCX_Class_Pseudos <string opName,
                                VOPC_Profile P,
                                VOPC_Profile P_NoSDst> :
           VOPC_Class_Pseudos <opName, P, 1, 1> {

  def _nosdst_e32 : VOPC_Pseudo <opName#"_nosdst", P_NoSDst, [], 0>,
                    VCMPXNoSDstTable<0, opName#"_e32"> {
    let Defs = [EXEC];
    let SchedRW = P_NoSDst.Schedule;
    let isConvergent = 1;
    let SubtargetPredicate = HasNoSdstCMPX;
  }

  def _nosdst_e64 : VOP3_Pseudo<opName#"_nosdst", P_NoSDst>,
                    VCMPXNoSDstTable<0, opName#"_e64"> {
    let Defs = [EXEC];
    let SchedRW = P_NoSDst.Schedule;
    let SubtargetPredicate = HasNoSdstCMPX;
  }

  if P_NoSDst.HasExtSDWA then
  def _nosdst_sdwa : VOPC_SDWA_Pseudo <opName#"_nosdst", P_NoSDst> {
    let Defs = [EXEC];
    let SchedRW = P_NoSDst.Schedule;
    let isConvergent = 1;
    let SubtargetPredicate = HasNoSdstCMPX;
  }

  let SubtargetPredicate = isGFX11Plus in {
  if P.HasExtDPP then
      def _nosdst_e32_dpp : VOP_DPP_Pseudo<opName#"_nosdst", P_NoSDst> {
        let Defs = [EXEC];
        let SchedRW = P_NoSDst.Schedule;
        let isConvergent = 1;
        let VOPC = 1;
        let Constraints = "";
      }
  if P.HasExtVOP3DPP then
      def _nosdst_e64_dpp : VOP3_DPP_Pseudo<opName#"_nosdst", P_NoSDst> {
        let Defs = [EXEC];
        let SchedRW = P_NoSDst.Schedule;
        let Constraints = "";
    }
  } // end SubtargetPredicate = isGFX11Plus
}
} // End SubtargetPredicate = HasSdstCMPX

defm VOPC_I1_F16_I16 : VOPC_Class_Profile_t16<[Write32Bit]>;
def VOPC_I1_F32_I32 : VOPC_Class_Profile<[Write32Bit], f32>;
def VOPC_I1_F64_I32 : VOPC_Class_Profile<[WriteDoubleAdd], f64>;

defm VOPC_F16_I16 : VOPC_Class_NoSdst_Profile_t16<[Write32Bit]>;
def VOPC_F32_I32 : VOPC_Class_NoSdst_Profile<[Write32Bit], f32>;
def VOPC_F64_I32 : VOPC_Class_NoSdst_Profile<[Write64Bit], f64>;

multiclass VOPC_CLASS_F16 <string opName> {
  let OtherPredicates = [Has16BitInsts], True16Predicate = NotHasTrue16BitInsts in {
    defm NAME : VOPC_Class_Pseudos <opName, VOPC_I1_F16_I16, 0>;
  }
  let OtherPredicates = [HasTrue16BitInsts] in {
    defm _t16 : VOPC_Class_Pseudos <opName#"_t16", VOPC_I1_F16_I16_t16, 0>;
  }
}

multiclass VOPCX_CLASS_F16 <string opName> {
  let OtherPredicates = [Has16BitInsts], True16Predicate = NotHasTrue16BitInsts in {
    defm NAME : VOPCX_Class_Pseudos <opName, VOPC_I1_F16_I16, VOPC_F16_I16>;
  }
  let OtherPredicates = [HasTrue16BitInsts] in {
    defm _t16 : VOPCX_Class_Pseudos <opName#"_t16", VOPC_I1_F16_I16_t16, VOPC_F16_I16_t16>;
  }
}

multiclass VOPC_CLASS_F32 <string opName> :
  VOPC_Class_Pseudos <opName, VOPC_I1_F32_I32, 0>;

multiclass VOPCX_CLASS_F32 <string opName> :
  VOPCX_Class_Pseudos <opName, VOPC_I1_F32_I32, VOPC_F32_I32>;

multiclass VOPC_CLASS_F64 <string opName> :
  VOPC_Class_Pseudos <opName, VOPC_I1_F64_I32, 0>;

multiclass VOPCX_CLASS_F64 <string opName> :
  VOPCX_Class_Pseudos <opName, VOPC_I1_F64_I32, VOPC_F64_I32>;

// cmp_class ignores the FP mode and faithfully reports the unmodified
// source value.
let ReadsModeReg = 0, mayRaiseFPException = 0 in {
defm V_CMP_CLASS_F32 : VOPC_CLASS_F32 <"v_cmp_class_f32">;
defm V_CMPX_CLASS_F32 : VOPCX_CLASS_F32 <"v_cmpx_class_f32">;
defm V_CMP_CLASS_F64 : VOPC_CLASS_F64 <"v_cmp_class_f64">;
defm V_CMPX_CLASS_F64 : VOPCX_CLASS_F64 <"v_cmpx_class_f64">;

defm V_CMP_CLASS_F16  : VOPC_CLASS_F16 <"v_cmp_class_f16">;
defm V_CMPX_CLASS_F16 : VOPCX_CLASS_F16 <"v_cmpx_class_f16">;
} // End ReadsModeReg = 0, mayRaiseFPException = 0

//===----------------------------------------------------------------------===//
// V_ICMPIntrinsic Pattern.
//===----------------------------------------------------------------------===//

// We need to use COPY_TO_REGCLASS to w/a the problem when ReplaceAllUsesWith()
// complaints it cannot replace i1 <-> i64/i32 if node was not morphed in place.
multiclass ICMP_Pattern <PatFrags cond, Instruction inst, ValueType vt> {
  let WaveSizePredicate = isWave64 in
  def : GCNPat <
    (i64 (AMDGPUsetcc vt:$src0, vt:$src1, cond)),
    (i64 (COPY_TO_REGCLASS (inst $src0, $src1), SReg_64))
  >;

  let WaveSizePredicate = isWave32 in {
    def : GCNPat <
      (i32 (AMDGPUsetcc vt:$src0, vt:$src1, cond)),
      (i32 (COPY_TO_REGCLASS (inst $src0, $src1), SReg_32))
    >;

    // Support codegen of i64 setcc in wave32 mode.
    def : GCNPat <
      (i64 (AMDGPUsetcc vt:$src0, vt:$src1, cond)),
      (i64 (REG_SEQUENCE SReg_64, (inst $src0, $src1), sub0, (S_MOV_B32 (i32 0)), sub1))
    >;
  }
}

defm : ICMP_Pattern <COND_EQ, V_CMP_EQ_U32_e64, i32>;
defm : ICMP_Pattern <COND_NE, V_CMP_NE_U32_e64, i32>;
defm : ICMP_Pattern <COND_UGT, V_CMP_GT_U32_e64, i32>;
defm : ICMP_Pattern <COND_UGE, V_CMP_GE_U32_e64, i32>;
defm : ICMP_Pattern <COND_ULT, V_CMP_LT_U32_e64, i32>;
defm : ICMP_Pattern <COND_ULE, V_CMP_LE_U32_e64, i32>;
defm : ICMP_Pattern <COND_SGT, V_CMP_GT_I32_e64, i32>;
defm : ICMP_Pattern <COND_SGE, V_CMP_GE_I32_e64, i32>;
defm : ICMP_Pattern <COND_SLT, V_CMP_LT_I32_e64, i32>;
defm : ICMP_Pattern <COND_SLE, V_CMP_LE_I32_e64, i32>;

defm : ICMP_Pattern <COND_EQ, V_CMP_EQ_U64_e64, i64>;
defm : ICMP_Pattern <COND_NE, V_CMP_NE_U64_e64, i64>;
defm : ICMP_Pattern <COND_UGT, V_CMP_GT_U64_e64, i64>;
defm : ICMP_Pattern <COND_UGE, V_CMP_GE_U64_e64, i64>;
defm : ICMP_Pattern <COND_ULT, V_CMP_LT_U64_e64, i64>;
defm : ICMP_Pattern <COND_ULE, V_CMP_LE_U64_e64, i64>;
defm : ICMP_Pattern <COND_SGT, V_CMP_GT_I64_e64, i64>;
defm : ICMP_Pattern <COND_SGE, V_CMP_GE_I64_e64, i64>;
defm : ICMP_Pattern <COND_SLT, V_CMP_LT_I64_e64, i64>;
defm : ICMP_Pattern <COND_SLE, V_CMP_LE_I64_e64, i64>;

let OtherPredicates = [HasTrue16BitInsts] in {
defm : ICMP_Pattern <COND_EQ, V_CMP_EQ_U16_t16_e64, i16>;
defm : ICMP_Pattern <COND_NE, V_CMP_NE_U16_t16_e64, i16>;
defm : ICMP_Pattern <COND_UGT, V_CMP_GT_U16_t16_e64, i16>;
defm : ICMP_Pattern <COND_UGE, V_CMP_GE_U16_t16_e64, i16>;
defm : ICMP_Pattern <COND_ULT, V_CMP_LT_U16_t16_e64, i16>;
defm : ICMP_Pattern <COND_ULE, V_CMP_LE_U16_t16_e64, i16>;
defm : ICMP_Pattern <COND_SGT, V_CMP_GT_I16_t16_e64, i16>;
defm : ICMP_Pattern <COND_SGE, V_CMP_GE_I16_t16_e64, i16>;
defm : ICMP_Pattern <COND_SLT, V_CMP_LT_I16_t16_e64, i16>;
defm : ICMP_Pattern <COND_SLE, V_CMP_LE_I16_t16_e64, i16>;
} // End OtherPredicates = [HasTrue16BitInsts]

let OtherPredicates = [NotHasTrue16BitInsts] in {
defm : ICMP_Pattern <COND_EQ, V_CMP_EQ_U16_e64, i16>;
defm : ICMP_Pattern <COND_NE, V_CMP_NE_U16_e64, i16>;
defm : ICMP_Pattern <COND_UGT, V_CMP_GT_U16_e64, i16>;
defm : ICMP_Pattern <COND_UGE, V_CMP_GE_U16_e64, i16>;
defm : ICMP_Pattern <COND_ULT, V_CMP_LT_U16_e64, i16>;
defm : ICMP_Pattern <COND_ULE, V_CMP_LE_U16_e64, i16>;
defm : ICMP_Pattern <COND_SGT, V_CMP_GT_I16_e64, i16>;
defm : ICMP_Pattern <COND_SGE, V_CMP_GE_I16_e64, i16>;
defm : ICMP_Pattern <COND_SLT, V_CMP_LT_I16_e64, i16>;
defm : ICMP_Pattern <COND_SLE, V_CMP_LE_I16_e64, i16>;
} // End OtherPredicates = [NotHasTrue16BitInsts]

multiclass FCMP_Pattern <PatFrags cond, Instruction inst, ValueType vt> {
  let WaveSizePredicate = isWave64 in
  def : GCNPat <
    (i64 (AMDGPUsetcc (vt (VOP3Mods vt:$src0, i32:$src0_modifiers)),
                 (vt (VOP3Mods vt:$src1, i32:$src1_modifiers)), cond)),
    (i64 (COPY_TO_REGCLASS (inst $src0_modifiers, $src0, $src1_modifiers, $src1,
                           DSTCLAMP.NONE), SReg_64))
  >;

  let WaveSizePredicate = isWave32 in {
    def : GCNPat <
      (i32 (AMDGPUsetcc (vt (VOP3Mods vt:$src0, i32:$src0_modifiers)),
                        (vt (VOP3Mods vt:$src1, i32:$src1_modifiers)), cond)),
      (i32 (COPY_TO_REGCLASS (inst $src0_modifiers, $src0, $src1_modifiers, $src1,
                              DSTCLAMP.NONE), SReg_32))
    >;

    def : GCNPat <
      (i64 (AMDGPUsetcc (vt (VOP3Mods vt:$src0, i32:$src0_modifiers)),
                        (vt (VOP3Mods vt:$src1, i32:$src1_modifiers)), cond)),
      (i64 (REG_SEQUENCE SReg_64, (inst $src0_modifiers, $src0, $src1_modifiers, $src1,
                                   DSTCLAMP.NONE), sub0,
                                  (S_MOV_B32 (i32 0)), sub1))
    >;
  }
}

defm : FCMP_Pattern <COND_O, V_CMP_O_F32_e64, f32>;
defm : FCMP_Pattern <COND_UO, V_CMP_U_F32_e64, f32>;
defm : FCMP_Pattern <COND_OEQ, V_CMP_EQ_F32_e64, f32>;
defm : FCMP_Pattern <COND_ONE, V_CMP_NEQ_F32_e64, f32>;
defm : FCMP_Pattern <COND_OGT, V_CMP_GT_F32_e64, f32>;
defm : FCMP_Pattern <COND_OGE, V_CMP_GE_F32_e64, f32>;
defm : FCMP_Pattern <COND_OLT, V_CMP_LT_F32_e64, f32>;
defm : FCMP_Pattern <COND_OLE, V_CMP_LE_F32_e64, f32>;

defm : FCMP_Pattern <COND_O, V_CMP_O_F64_e64, f64>;
defm : FCMP_Pattern <COND_UO, V_CMP_U_F64_e64, f64>;
defm : FCMP_Pattern <COND_OEQ, V_CMP_EQ_F64_e64, f64>;
defm : FCMP_Pattern <COND_ONE, V_CMP_NEQ_F64_e64, f64>;
defm : FCMP_Pattern <COND_OGT, V_CMP_GT_F64_e64, f64>;
defm : FCMP_Pattern <COND_OGE, V_CMP_GE_F64_e64, f64>;
defm : FCMP_Pattern <COND_OLT, V_CMP_LT_F64_e64, f64>;
defm : FCMP_Pattern <COND_OLE, V_CMP_LE_F64_e64, f64>;

defm : FCMP_Pattern <COND_UEQ, V_CMP_NLG_F32_e64, f32>;
defm : FCMP_Pattern <COND_UNE, V_CMP_NEQ_F32_e64, f32>;
defm : FCMP_Pattern <COND_UGT, V_CMP_NLE_F32_e64, f32>;
defm : FCMP_Pattern <COND_UGE, V_CMP_NLT_F32_e64, f32>;
defm : FCMP_Pattern <COND_ULT, V_CMP_NGE_F32_e64, f32>;
defm : FCMP_Pattern <COND_ULE, V_CMP_NGT_F32_e64, f32>;

defm : FCMP_Pattern <COND_UEQ, V_CMP_NLG_F64_e64, f64>;
defm : FCMP_Pattern <COND_UNE, V_CMP_NEQ_F64_e64, f64>;
defm : FCMP_Pattern <COND_UGT, V_CMP_NLE_F64_e64, f64>;
defm : FCMP_Pattern <COND_UGE, V_CMP_NLT_F64_e64, f64>;
defm : FCMP_Pattern <COND_ULT, V_CMP_NGE_F64_e64, f64>;
defm : FCMP_Pattern <COND_ULE, V_CMP_NGT_F64_e64, f64>;

let OtherPredicates = [HasTrue16BitInsts] in {
defm : FCMP_Pattern <COND_O, V_CMP_O_F16_t16_e64, f16>;
defm : FCMP_Pattern <COND_UO, V_CMP_U_F16_t16_e64, f16>;
defm : FCMP_Pattern <COND_OEQ, V_CMP_EQ_F16_t16_e64, f16>;
defm : FCMP_Pattern <COND_ONE, V_CMP_NEQ_F16_t16_e64, f16>;
defm : FCMP_Pattern <COND_OGT, V_CMP_GT_F16_t16_e64, f16>;
defm : FCMP_Pattern <COND_OGE, V_CMP_GE_F16_t16_e64, f16>;
defm : FCMP_Pattern <COND_OLT, V_CMP_LT_F16_t16_e64, f16>;
defm : FCMP_Pattern <COND_OLE, V_CMP_LE_F16_t16_e64, f16>;

defm : FCMP_Pattern <COND_UEQ, V_CMP_NLG_F16_t16_e64, f16>;
defm : FCMP_Pattern <COND_UNE, V_CMP_NEQ_F16_t16_e64, f16>;
defm : FCMP_Pattern <COND_UGT, V_CMP_NLE_F16_t16_e64, f16>;
defm : FCMP_Pattern <COND_UGE, V_CMP_NLT_F16_t16_e64, f16>;
defm : FCMP_Pattern <COND_ULT, V_CMP_NGE_F16_t16_e64, f16>;
defm : FCMP_Pattern <COND_ULE, V_CMP_NGT_F16_t16_e64, f16>;
} // End OtherPredicates = [HasTrue16BitInsts]

let OtherPredicates = [NotHasTrue16BitInsts] in {
defm : FCMP_Pattern <COND_O, V_CMP_O_F16_e64, f16>;
defm : FCMP_Pattern <COND_UO, V_CMP_U_F16_e64, f16>;
defm : FCMP_Pattern <COND_OEQ, V_CMP_EQ_F16_e64, f16>;
defm : FCMP_Pattern <COND_ONE, V_CMP_NEQ_F16_e64, f16>;
defm : FCMP_Pattern <COND_OGT, V_CMP_GT_F16_e64, f16>;
defm : FCMP_Pattern <COND_OGE, V_CMP_GE_F16_e64, f16>;
defm : FCMP_Pattern <COND_OLT, V_CMP_LT_F16_e64, f16>;
defm : FCMP_Pattern <COND_OLE, V_CMP_LE_F16_e64, f16>;

defm : FCMP_Pattern <COND_UEQ, V_CMP_NLG_F16_e64, f16>;
defm : FCMP_Pattern <COND_UNE, V_CMP_NEQ_F16_e64, f16>;
defm : FCMP_Pattern <COND_UGT, V_CMP_NLE_F16_e64, f16>;
defm : FCMP_Pattern <COND_UGE, V_CMP_NLT_F16_e64, f16>;
defm : FCMP_Pattern <COND_ULT, V_CMP_NGE_F16_e64, f16>;
defm : FCMP_Pattern <COND_ULE, V_CMP_NGT_F16_e64, f16>;
} // End OtherPredicates = [NotHasTrue16BitInsts]

//===----------------------------------------------------------------------===//
// DPP Encodings
//===----------------------------------------------------------------------===//

// VOPC32

class VOPC_DPPe_Common<bits<8> op> : Enc64 {
  bits<8> src1;
  let Inst{16-9} = src1;
  let Inst{24-17} = op;
  let Inst{31-25} = 0x3e;
}

class VOPC_DPP_Base<bits<8> op, string OpName, VOPProfile P>
    : VOP_DPP_Base<OpName, P, P.InsDPP16, " " #P.AsmDPP16>,
      VOPC_DPPe_Common<op> {
  Instruction Opcode = !cast<Instruction>(NAME);

  bits<2> src0_modifiers;
  bits<8> src0;
  bits<2> src1_modifiers;
  bits<9> dpp_ctrl;
  bits<1> bound_ctrl;
  bits<4> bank_mask;
  bits<4> row_mask;
  bit fi;

  let Inst{8-0} = 0xfa;

  let Inst{39-32} = !if (P.HasSrc0, src0{7-0}, 0);
  let Inst{48-40} = dpp_ctrl;
  let Inst{50} = fi;
  let Inst{51} = bound_ctrl;
  let Inst{52} = !if (P.HasSrc0Mods, src0_modifiers{0}, 0); // src0_neg
  let Inst{53} = !if (P.HasSrc0Mods, src0_modifiers{1}, 0); // src0_abs
  let Inst{54} = !if (P.HasSrc1Mods, src1_modifiers{0}, 0); // src1_neg
  let Inst{55} = !if (P.HasSrc1Mods, src1_modifiers{1}, 0); // src1_abs
  let Inst{59-56} = bank_mask;
  let Inst{63-60} = row_mask;

  let AsmMatchConverter = "cvtDPP";
  let VOPC = 1;
}

class VOPC_DPP8_Base<bits<8> op, string OpName, VOPProfile P>
    : VOP_DPP8_Base<OpName, P, P.InsDPP8, " " #P.AsmDPP8>,
      VOPC_DPPe_Common<op> {
  Instruction Opcode = !cast<Instruction>(NAME);

  bits<8> src0;
  bits<24> dpp8;
  bits<9> fi;

  let Inst{8-0} = fi;

  let Inst{39-32} = !if (P.HasSrc0, src0{7-0}, 0);
  let Inst{63-40} = dpp8{23-0};

  let AsmMatchConverter = "cvtDPP8";
  let VOPC = 1;
}

class VOPC_DPP16<bits<8> op, VOP_DPP_Pseudo ps, string opName = ps.OpName>
    : VOPC_DPP_Base<op, opName, ps.Pfl> {
  let AssemblerPredicate = HasDPP16;
  let SubtargetPredicate = HasDPP16;
  let hasSideEffects = ps.hasSideEffects;
  let Defs = ps.Defs;
  let SchedRW = ps.SchedRW;
  let Uses = ps.Uses;
  let OtherPredicates = ps.OtherPredicates;
  let Constraints = ps.Constraints;
}

class VOPC_DPP16_SIMC<bits<8> op, VOP_DPP_Pseudo ps, int subtarget,
                      string opName = ps.OpName>
    : VOPC_DPP16<op, ps, opName>, SIMCInstr<ps.PseudoInstr, subtarget>;

class VOPC_DPP8<bits<8> op, VOPC_Pseudo ps, string opName = ps.OpName>
    : VOPC_DPP8_Base<op, opName, ps.Pfl> {
  // Note ps is the non-dpp pseudo
  let hasSideEffects = ps.hasSideEffects;
  let Defs = ps.Defs;
  let SchedRW = ps.SchedRW;
  let Uses = ps.Uses;
  let OtherPredicates = ps.OtherPredicates;
  let Constraints = "";
}

// VOPC64

class VOPC64_DPP_Base<bits<10> op, string OpName, VOPProfile P>
    : VOP3_DPP_Base<OpName, P, 1>, VOP3_DPPe_Common<op, P> {
  Instruction Opcode = !cast<Instruction>(NAME);

  bits<8> src0;
  bits<9> dpp_ctrl;
  bits<1> bound_ctrl;
  bits<4> bank_mask;
  bits<4> row_mask;
  bit     fi;

  let Inst{40-32} = 0xfa;
  let Inst{71-64} = !if(P.HasSrc0, src0{7-0}, 0);
  let Inst{80-72} = dpp_ctrl;
  let Inst{82}    = fi;
  let Inst{83}    = bound_ctrl;
  // Inst{87-84} ignored by hw
  let Inst{91-88} = bank_mask;
  let Inst{95-92} = row_mask;
}

class VOPC64_DPP16<bits<10> op, VOP_DPP_Pseudo ps, string opName = ps.OpName>
    : VOPC64_DPP_Base<op, opName, ps.Pfl> {
  let AssemblerPredicate = HasDPP16;
  let SubtargetPredicate = HasDPP16;
  let hasSideEffects = ps.hasSideEffects;
  let Defs = ps.Defs;
  let SchedRW = ps.SchedRW;
  let Uses = ps.Uses;
  let OtherPredicates = ps.OtherPredicates;
  let Constraints = ps.Constraints;
}

class VOPC64_DPP16_Dst<bits<10> op, VOP_DPP_Pseudo ps,
                       string opName = ps.OpName>
    : VOPC64_DPP16<op, ps, opName> {
  bits<8> sdst;
  let Inst{7-0} = sdst;
}

class VOPC64_DPP16_NoDst<bits<10> op, VOP_DPP_Pseudo ps,
                         string opName = ps.OpName>
    : VOPC64_DPP16<op, ps, opName> {
  let Inst{7-0} = ? ;
}

class VOPC64_DPP8_Base<bits<10> op, string OpName, VOPProfile P>
    : VOP3_DPP8_Base<OpName, P>, VOP3_DPPe_Common<op, P> {
  Instruction Opcode = !cast<Instruction>(NAME);

  bits<8> src0;
  bits<24> dpp8;
  bits<9> fi;

  let Inst{40-32} = fi;
  let Inst{71-64} = !if(P.HasSrc0, src0{7-0}, 0);
  let Inst{95-72} = dpp8{23-0};
}

class VOPC64_DPP8<bits<10> op, VOP_Pseudo ps, string opName = ps.OpName>
    : VOPC64_DPP8_Base<op, opName, ps.Pfl> {
  // Note ps is the non-dpp pseudo
  let hasSideEffects = ps.hasSideEffects;
  let Defs = ps.Defs;
  let SchedRW = ps.SchedRW;
  let Uses = ps.Uses;
  let OtherPredicates = ps.OtherPredicates;
}

class VOPC64_DPP8_Dst<bits<10> op, VOP_Pseudo ps, string opName = ps.OpName>
    : VOPC64_DPP8<op, ps, opName> {
  bits<8> sdst;
  let Inst{7-0} = sdst;
  let Constraints = "";
}

class VOPC64_DPP8_NoDst<bits<10> op, VOP_Pseudo ps, string opName = ps.OpName>
    : VOPC64_DPP8<op, ps, opName> {
  let Inst{7-0} = ? ;
  let Constraints = "";
}

//===----------------------------------------------------------------------===//
// Target-specific instruction encodings.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// GFX11, GFX12
//===----------------------------------------------------------------------===//

multiclass VOPC_Real_Base<GFXGen Gen, bits<9> op> {
  let AssemblerPredicate = Gen.AssemblerPredicate, DecoderNamespace = Gen.DecoderNamespace in {
    defvar ps32 = !cast<VOPC_Pseudo>(NAME#"_e32");
    defvar ps64 = !cast<VOP3_Pseudo>(NAME#"_e64");
    def _e32#Gen.Suffix : VOPC_Real<ps32, Gen.Subtarget>,
                          VOPCe<op{7-0}>;
    def _e64#Gen.Suffix : VOP3_Real<ps64, Gen.Subtarget>,
                          VOP3a_gfx11_gfx12<{0, op}, ps64.Pfl> {
      // Encoding used for VOPC instructions encoded as VOP3 differs from
      // VOP3e by destination name (sdst) as VOPC doesn't have vector dst.
      bits<8> sdst;
      let Inst{7-0} = sdst;
    }

    defm : VOPCInstAliases<NAME, !substr(Gen.Suffix,1)>;

    if ps32.Pfl.HasExtDPP then {
      defvar psDPP = !cast<VOP_DPP_Pseudo>(NAME #"_e32" #"_dpp");
      defvar AsmDPP = ps32.Pfl.AsmDPP16;
      def _e32_dpp#Gen.Suffix : VOPC_DPP16_SIMC<op{7-0}, psDPP, Gen.Subtarget>;
      def _e32_dpp_w32#Gen.Suffix : VOPC_DPP16<op{7-0}, psDPP> {
        let AsmString = psDPP.OpName # " vcc_lo, " # AsmDPP;
        let isAsmParserOnly = 1;
        let WaveSizePredicate = isWave32;
      }
      def _e32_dpp_w64#Gen.Suffix : VOPC_DPP16<op{7-0}, psDPP> {
        let AsmString = psDPP.OpName # " vcc, " # AsmDPP;
        let isAsmParserOnly = 1;
        let WaveSizePredicate = isWave64;
      }
      defvar AsmDPP8 = ps32.Pfl.AsmDPP8;
      def _e32_dpp8#Gen.Suffix : VOPC_DPP8<op{7-0}, ps32>;
      def _e32_dpp8_w32#Gen.Suffix : VOPC_DPP8<op{7-0}, ps32> {
        let AsmString = ps32.OpName # " vcc_lo, " # AsmDPP8;
        let isAsmParserOnly = 1;
        let WaveSizePredicate = isWave32;
      }
      def _e32_dpp8_w64#Gen.Suffix : VOPC_DPP8<op{7-0}, ps32> {
        let AsmString = ps32.OpName # " vcc, " # AsmDPP8;
        let isAsmParserOnly = 1;
        let WaveSizePredicate = isWave64;
      }
    }
    if ps64.Pfl.HasExtVOP3DPP then {
      defvar psDPP = !cast<VOP_DPP_Pseudo>(NAME #"_e64" #"_dpp");
      def _e64_dpp#Gen.Suffix : VOPC64_DPP16_Dst<{0, op}, psDPP>,
                                SIMCInstr<psDPP.PseudoInstr, Gen.Subtarget>;
      def _e64_dpp8#Gen.Suffix : VOPC64_DPP8_Dst<{0, op}, ps64>;
    }
  } // AssemblerPredicate = Gen.AssemblerPredicate, DecoderNamespace = Gen.DecoderNamespace
}

multiclass VOPC_Real_with_name<GFXGen Gen, bits<9> op, string OpName,
                               string asm_name, string pseudo_mnemonic = ""> {
  defvar ps32 = !cast<VOPC_Pseudo>(OpName#"_e32");
  defvar ps64 = !cast<VOP3_Pseudo>(OpName#"_e64");
  let AssemblerPredicate = Gen.AssemblerPredicate in {
    def : AMDGPUMnemonicAlias<!if(!empty(pseudo_mnemonic), ps32.Mnemonic,
                                                     pseudo_mnemonic),
                              asm_name, ps32.AsmVariantName>;
    def : AMDGPUMnemonicAlias<!if(!empty(pseudo_mnemonic), ps64.Mnemonic,
                                                     pseudo_mnemonic),
                              asm_name, ps64.AsmVariantName>;

    let DecoderNamespace = Gen.DecoderNamespace in {
      def _e32#Gen.Suffix :
        // 32 and 64 bit forms of the instruction have _e32 and _e64
        // respectively appended to their assembly mnemonic.
        // _e64 is printed as part of the VOPDstS64orS32 operand, whereas
        // the destination-less 32bit forms add it to the asmString here.
        VOPC_Real<ps32, Gen.Subtarget, asm_name#"_e32">,
        VOPCe<op{7-0}>;
      def _e64#Gen.Suffix :
            VOP3_Real_Gen<ps64, Gen, asm_name>,
            VOP3a_gfx11_gfx12<{0, op}, ps64.Pfl> {
        // Encoding used for VOPC instructions encoded as VOP3 differs from
        // VOP3e by destination name (sdst) as VOPC doesn't have vector dst.
        bits<8> sdst;
        let Inst{7-0} = sdst;
      }

      defm : VOPCInstAliases<OpName, !substr(Gen.Suffix, 1), NAME, asm_name>;

      if ps32.Pfl.HasExtDPP then {
        defvar psDPP = !cast<VOP_DPP_Pseudo>(OpName #"_e32" #"_dpp");
        defvar AsmDPP = ps32.Pfl.AsmDPP16;
        def _e32_dpp#Gen.Suffix : VOPC_DPP16_SIMC<op{7-0}, psDPP,
                                                  Gen.Subtarget, asm_name>;
        def _e32_dpp_w32#Gen.Suffix
            : VOPC_DPP16<op{7-0}, psDPP, asm_name> {
          let AsmString = asm_name # " vcc_lo, " # AsmDPP;
          let isAsmParserOnly = 1;
          let WaveSizePredicate = isWave32;
        }
        def _e32_dpp_w64#Gen.Suffix
            : VOPC_DPP16<op{7-0}, psDPP, asm_name> {
          let AsmString = asm_name # " vcc, " # AsmDPP;
          let isAsmParserOnly = 1;
          let WaveSizePredicate = isWave64;
        }
        defvar AsmDPP8 = ps32.Pfl.AsmDPP8;
        def _e32_dpp8#Gen.Suffix : VOPC_DPP8<op{7-0}, ps32, asm_name>;
        def _e32_dpp8_w32#Gen.Suffix
            : VOPC_DPP8<op{7-0}, ps32, asm_name> {
          let AsmString = asm_name # " vcc_lo, " # AsmDPP8;
          let isAsmParserOnly = 1;
          let WaveSizePredicate = isWave32;
        }
        def _e32_dpp8_w64#Gen.Suffix
            : VOPC_DPP8<op{7-0}, ps32, asm_name> {
          let AsmString = asm_name # " vcc, " # AsmDPP8;
          let isAsmParserOnly = 1;
          let WaveSizePredicate = isWave64;
        }
      }

      if ps64.Pfl.HasExtVOP3DPP then {
        defvar psDPP = !cast<VOP_DPP_Pseudo>(OpName #"_e64" #"_dpp");
        def _e64_dpp#Gen.Suffix : VOPC64_DPP16_Dst<{0, op}, psDPP, asm_name>,
                                  SIMCInstr<psDPP.PseudoInstr, Gen.Subtarget>;
        def _e64_dpp8#Gen.Suffix : VOPC64_DPP8_Dst<{0, op}, ps64, asm_name>;
      } // end if ps64.Pfl.HasExtVOP3DPP
    } // End DecoderNamespace
  } // End AssemblerPredicate
}

multiclass VOPC_Real_t16<GFXGen Gen, bits<9> op, string asm_name,
                         string OpName = NAME, string pseudo_mnemonic = ""> :
  VOPC_Real_with_name<Gen, op, OpName, asm_name, pseudo_mnemonic>;

multiclass VOPCX_Real<GFXGen Gen, bits<9> op> {
  let AssemblerPredicate = Gen.AssemblerPredicate, DecoderNamespace = Gen.DecoderNamespace in {
    defvar ps32 = !cast<VOPC_Pseudo>(NAME#"_nosdst_e32");
    defvar ps64 = !cast<VOP3_Pseudo>(NAME#"_nosdst_e64");
    def _e32#Gen.Suffix :
      VOPC_Real<ps32, Gen.Subtarget>,
      VOPCe<op{7-0}> {
        let AsmString = !subst("_nosdst", "", ps32.PseudoInstr)
                        # " " # ps32.AsmOperands;
    }
    def _e64#Gen.Suffix :
      VOP3_Real<ps64, Gen.Subtarget>,
      VOP3a_gfx11_gfx12<{0, op}, ps64.Pfl> {
        let Inst{7-0} = ?; // sdst
        let AsmString = !subst("_nosdst", "", ps64.Mnemonic)
                        # "{_e64} " # ps64.AsmOperands;
    }

    defm : VOPCXInstAliases<NAME, !substr(Gen.Suffix, 1)>;

    if ps32.Pfl.HasExtDPP then {
      defvar psDPP = !cast<VOP_DPP_Pseudo>(NAME #"_nosdst_e32" #"_dpp");
      defvar AsmDPP = ps32.Pfl.AsmDPP16;
      def _e32_dpp#Gen.Suffix
          : VOPC_DPP16_SIMC<op{7-0}, psDPP, Gen.Subtarget> {
        let AsmString = !subst("_nosdst", "", psDPP.OpName) # " " # AsmDPP;
      }
      defvar AsmDPP8 = ps32.Pfl.AsmDPP8;
      def _e32_dpp8#Gen.Suffix : VOPC_DPP8<op{7-0}, ps32> {
        let AsmString = !subst("_nosdst", "", ps32.OpName) # " " # AsmDPP8;
      }
    }

    if ps64.Pfl.HasExtVOP3DPP then {
      defvar psDPP = !cast<VOP_DPP_Pseudo>(NAME #"_nosdst_e64" #"_dpp");
      defvar AsmDPP = ps64.Pfl.AsmVOP3DPP16;
      def _e64_dpp#Gen.Suffix
          : VOPC64_DPP16_NoDst<{0, op}, psDPP>,
            SIMCInstr<psDPP.PseudoInstr, Gen.Subtarget> {
        let AsmString = !subst("_nosdst", "", psDPP.OpName)
                        # "{_e64_dpp} " # AsmDPP;
      }
      defvar AsmDPP8 = ps64.Pfl.AsmVOP3DPP8;
      def _e64_dpp8#Gen.Suffix : VOPC64_DPP8_NoDst<{0, op}, ps64> {
        let AsmString = !subst("_nosdst", "", ps64.OpName)
                        # "{_e64_dpp} " # AsmDPP8;
      }
    }
  } // End AssemblerPredicate = Gen.AssemblerPredicate, DecoderNamespace = Gen.DecoderNamespace
}

multiclass VOPCX_Real_with_name<GFXGen Gen, bits<9> op, string OpName,
      string asm_name, string pseudo_mnemonic = ""> {
  defvar ps32 = !cast<VOPC_Pseudo>(OpName#"_nosdst_e32");
  defvar ps64 = !cast<VOP3_Pseudo>(OpName#"_nosdst_e64");
  let AssemblerPredicate = Gen.AssemblerPredicate in {
    def : AMDGPUMnemonicAlias<!if(!empty(pseudo_mnemonic), !subst("_nosdst", "", ps32.Mnemonic),
                                                     pseudo_mnemonic),
                              asm_name, ps32.AsmVariantName>;
    def : AMDGPUMnemonicAlias<!if(!empty(pseudo_mnemonic), !subst("_nosdst", "", ps64.Mnemonic),
                                                     pseudo_mnemonic),
                              asm_name, ps64.AsmVariantName>;

    let DecoderNamespace = Gen.DecoderNamespace in {
      def _e32#Gen.Suffix
          : VOPC_Real<ps32, Gen.Subtarget, asm_name>,
            VOPCe<op{7-0}> {
        let AsmString = asm_name # "{_e32} " # ps32.AsmOperands;
      }
      def _e64#Gen.Suffix
          : VOP3_Real_Gen<ps64, Gen, asm_name>,
            VOP3a_gfx11_gfx12<{0, op}, ps64.Pfl> {
        let Inst{7-0} = ? ; // sdst
        let AsmString = asm_name # "{_e64} " # ps64.AsmOperands;
      }

      defm : VOPCXInstAliases<OpName, !substr(Gen.Suffix, 1), NAME, asm_name>;

      if ps32.Pfl.HasExtDPP then {
        defvar psDPP = !cast<VOP_DPP_Pseudo>(OpName#"_nosdst_e32"#"_dpp");
        def _e32_dpp#Gen.Suffix : VOPC_DPP16_SIMC<op{7-0}, psDPP,
                                              Gen.Subtarget, asm_name>;
        def _e32_dpp8#Gen.Suffix : VOPC_DPP8<op{7-0}, ps32, asm_name>;
      }
      if ps64.Pfl.HasExtVOP3DPP then {
        defvar psDPP = !cast<VOP_DPP_Pseudo>(OpName#"_nosdst_e64"#"_dpp");
        defvar AsmDPP = ps64.Pfl.AsmVOP3DPP16;
        def _e64_dpp#Gen.Suffix
            : VOPC64_DPP16_NoDst<{0, op}, psDPP, asm_name>,
              SIMCInstr<psDPP.PseudoInstr, Gen.Subtarget> {
          let AsmString = asm_name # "{_e64_dpp} " # AsmDPP;
        }
        defvar AsmDPP8 = ps64.Pfl.AsmVOP3DPP8;
        def _e64_dpp8#Gen.Suffix : VOPC64_DPP8_NoDst<{0, op}, ps64, asm_name> {
          let AsmString = asm_name # "{_e64_dpp} " # AsmDPP8;
        }
      } // End if ps64.Pfl.HasExtVOP3DPP
    } // End DecoderNamespace
  } // End AssemblerPredicate
}

multiclass VOPCX_Real_t16<GFXGen Gen, bits<9> op, string asm_name,
      string OpName = NAME, string pseudo_mnemonic = ""> :
  VOPCX_Real_with_name<Gen, op, OpName, asm_name, pseudo_mnemonic>;

multiclass VOPC_Real_gfx11<bits<9> op> : VOPC_Real_Base<GFX11Gen, op>;

multiclass VOPC_Real_with_name_gfx11<bits<9> op, string OpName, string asm_name,
                                     string pseudo_mnemonic = "">
  : VOPC_Real_with_name<GFX11Gen, op, OpName, asm_name, pseudo_mnemonic>;

multiclass VOPCX_Real_gfx11<bits<9> op> : VOPCX_Real<GFX11Gen, op>;

multiclass VOPCX_Real_with_name_gfx11<bits<9> op, string OpName,
    string asm_name, string pseudo_mnemonic = ""> :
  VOPCX_Real_with_name<GFX11Gen, op, OpName, asm_name, pseudo_mnemonic>;

multiclass VOPC_Real_gfx11_gfx12<bits<9> op> :
  VOPC_Real_Base<GFX11Gen, op>, VOPC_Real_Base<GFX12Gen, op>;

multiclass VOPCX_Real_gfx11_gfx12<bits<9> op> :
  VOPCX_Real<GFX11Gen, op>, VOPCX_Real<GFX12Gen, op>;

multiclass VOPC_Real_t16_gfx11<bits <9> op, string asm_name,
    string OpName = NAME, string pseudo_mnemonic = ""> :
  VOPC_Real_t16<GFX11Gen, op, asm_name, OpName, pseudo_mnemonic>;

multiclass VOPC_Real_t16_gfx11_gfx12<bits <9> op, string asm_name,
    string OpName = NAME, string pseudo_mnemonic = ""> :
  VOPC_Real_t16<GFX11Gen, op, asm_name, OpName, pseudo_mnemonic>,
  VOPC_Real_t16<GFX12Gen, op, asm_name, OpName, pseudo_mnemonic>;

multiclass VOPCX_Real_t16_gfx11<bits<9> op, string asm_name,
    string OpName = NAME, string pseudo_mnemonic = ""> :
  VOPCX_Real_t16<GFX11Gen, op, asm_name, OpName, pseudo_mnemonic>;

multiclass VOPCX_Real_t16_gfx11_gfx12<bits<9> op, string asm_name,
    string OpName = NAME, string pseudo_mnemonic = ""> :
  VOPCX_Real_t16<GFX11Gen, op, asm_name, OpName, pseudo_mnemonic>,
  VOPCX_Real_t16<GFX12Gen, op, asm_name, OpName, pseudo_mnemonic>;

defm V_CMP_F_F16_t16      : VOPC_Real_t16_gfx11<0x000, "v_cmp_f_f16">;
defm V_CMP_LT_F16_t16     : VOPC_Real_t16_gfx11_gfx12<0x001, "v_cmp_lt_f16">;
defm V_CMP_EQ_F16_t16     : VOPC_Real_t16_gfx11_gfx12<0x002, "v_cmp_eq_f16">;
defm V_CMP_LE_F16_t16     : VOPC_Real_t16_gfx11_gfx12<0x003, "v_cmp_le_f16">;
defm V_CMP_GT_F16_t16     : VOPC_Real_t16_gfx11_gfx12<0x004, "v_cmp_gt_f16">;
defm V_CMP_LG_F16_t16     : VOPC_Real_t16_gfx11_gfx12<0x005, "v_cmp_lg_f16">;
defm V_CMP_GE_F16_t16     : VOPC_Real_t16_gfx11_gfx12<0x006, "v_cmp_ge_f16">;
defm V_CMP_O_F16_t16      : VOPC_Real_t16_gfx11_gfx12<0x007, "v_cmp_o_f16">;
defm V_CMP_U_F16_t16      : VOPC_Real_t16_gfx11_gfx12<0x008, "v_cmp_u_f16">;
defm V_CMP_NGE_F16_t16    : VOPC_Real_t16_gfx11_gfx12<0x009, "v_cmp_nge_f16">;
defm V_CMP_NLG_F16_t16    : VOPC_Real_t16_gfx11_gfx12<0x00a, "v_cmp_nlg_f16">;
defm V_CMP_NGT_F16_t16    : VOPC_Real_t16_gfx11_gfx12<0x00b, "v_cmp_ngt_f16">;
defm V_CMP_NLE_F16_t16    : VOPC_Real_t16_gfx11_gfx12<0x00c, "v_cmp_nle_f16">;
defm V_CMP_NEQ_F16_t16    : VOPC_Real_t16_gfx11_gfx12<0x00d, "v_cmp_neq_f16">;
defm V_CMP_NLT_F16_t16    : VOPC_Real_t16_gfx11_gfx12<0x00e, "v_cmp_nlt_f16">;
defm V_CMP_T_F16_t16      : VOPC_Real_with_name_gfx11<0x00f, "V_CMP_TRU_F16_t16", "v_cmp_t_f16", "v_cmp_tru_f16">;
defm V_CMP_F_F32      : VOPC_Real_gfx11<0x010>;
defm V_CMP_LT_F32     : VOPC_Real_gfx11_gfx12<0x011>;
defm V_CMP_EQ_F32     : VOPC_Real_gfx11_gfx12<0x012>;
defm V_CMP_LE_F32     : VOPC_Real_gfx11_gfx12<0x013>;
defm V_CMP_GT_F32     : VOPC_Real_gfx11_gfx12<0x014>;
defm V_CMP_LG_F32     : VOPC_Real_gfx11_gfx12<0x015>;
defm V_CMP_GE_F32     : VOPC_Real_gfx11_gfx12<0x016>;
defm V_CMP_O_F32      : VOPC_Real_gfx11_gfx12<0x017>;
defm V_CMP_U_F32      : VOPC_Real_gfx11_gfx12<0x018>;
defm V_CMP_NGE_F32    : VOPC_Real_gfx11_gfx12<0x019>;
defm V_CMP_NLG_F32    : VOPC_Real_gfx11_gfx12<0x01a>;
defm V_CMP_NGT_F32    : VOPC_Real_gfx11_gfx12<0x01b>;
defm V_CMP_NLE_F32    : VOPC_Real_gfx11_gfx12<0x01c>;
defm V_CMP_NEQ_F32    : VOPC_Real_gfx11_gfx12<0x01d>;
defm V_CMP_NLT_F32    : VOPC_Real_gfx11_gfx12<0x01e>;
defm V_CMP_T_F32      : VOPC_Real_with_name_gfx11<0x01f, "V_CMP_TRU_F32", "v_cmp_t_f32">;
defm V_CMP_T_F64      : VOPC_Real_with_name_gfx11<0x02f, "V_CMP_TRU_F64", "v_cmp_t_f64">;
defm V_CMP_LT_I16_t16     : VOPC_Real_t16_gfx11_gfx12<0x031, "v_cmp_lt_i16">;
defm V_CMP_EQ_I16_t16     : VOPC_Real_t16_gfx11_gfx12<0x032, "v_cmp_eq_i16">;
defm V_CMP_LE_I16_t16     : VOPC_Real_t16_gfx11_gfx12<0x033, "v_cmp_le_i16">;
defm V_CMP_GT_I16_t16     : VOPC_Real_t16_gfx11_gfx12<0x034, "v_cmp_gt_i16">;
defm V_CMP_NE_I16_t16     : VOPC_Real_t16_gfx11_gfx12<0x035, "v_cmp_ne_i16">;
defm V_CMP_GE_I16_t16     : VOPC_Real_t16_gfx11_gfx12<0x036, "v_cmp_ge_i16">;
defm V_CMP_LT_U16_t16     : VOPC_Real_t16_gfx11_gfx12<0x039, "v_cmp_lt_u16">;
defm V_CMP_EQ_U16_t16     : VOPC_Real_t16_gfx11_gfx12<0x03a, "v_cmp_eq_u16">;
defm V_CMP_LE_U16_t16     : VOPC_Real_t16_gfx11_gfx12<0x03b, "v_cmp_le_u16">;
defm V_CMP_GT_U16_t16     : VOPC_Real_t16_gfx11_gfx12<0x03c, "v_cmp_gt_u16">;
defm V_CMP_NE_U16_t16     : VOPC_Real_t16_gfx11_gfx12<0x03d, "v_cmp_ne_u16">;
defm V_CMP_GE_U16_t16     : VOPC_Real_t16_gfx11_gfx12<0x03e, "v_cmp_ge_u16">;
defm V_CMP_F_I32      : VOPC_Real_gfx11<0x040>;
defm V_CMP_LT_I32     : VOPC_Real_gfx11_gfx12<0x041>;
defm V_CMP_EQ_I32     : VOPC_Real_gfx11_gfx12<0x042>;
defm V_CMP_LE_I32     : VOPC_Real_gfx11_gfx12<0x043>;
defm V_CMP_GT_I32     : VOPC_Real_gfx11_gfx12<0x044>;
defm V_CMP_NE_I32     : VOPC_Real_gfx11_gfx12<0x045>;
defm V_CMP_GE_I32     : VOPC_Real_gfx11_gfx12<0x046>;
defm V_CMP_T_I32      : VOPC_Real_gfx11<0x047>;
defm V_CMP_F_U32      : VOPC_Real_gfx11<0x048>;
defm V_CMP_LT_U32     : VOPC_Real_gfx11_gfx12<0x049>;
defm V_CMP_EQ_U32     : VOPC_Real_gfx11_gfx12<0x04a>;
defm V_CMP_LE_U32     : VOPC_Real_gfx11_gfx12<0x04b>;
defm V_CMP_GT_U32     : VOPC_Real_gfx11_gfx12<0x04c>;
defm V_CMP_NE_U32     : VOPC_Real_gfx11_gfx12<0x04d>;
defm V_CMP_GE_U32     : VOPC_Real_gfx11_gfx12<0x04e>;
defm V_CMP_T_U32      : VOPC_Real_gfx11<0x04f>;

defm V_CMP_F_I64      : VOPC_Real_gfx11<0x050>;
defm V_CMP_LT_I64     : VOPC_Real_gfx11_gfx12<0x051>;
defm V_CMP_EQ_I64     : VOPC_Real_gfx11_gfx12<0x052>;
defm V_CMP_LE_I64     : VOPC_Real_gfx11_gfx12<0x053>;
defm V_CMP_GT_I64     : VOPC_Real_gfx11_gfx12<0x054>;
defm V_CMP_NE_I64     : VOPC_Real_gfx11_gfx12<0x055>;
defm V_CMP_GE_I64     : VOPC_Real_gfx11_gfx12<0x056>;
defm V_CMP_T_I64      : VOPC_Real_gfx11<0x057>;
defm V_CMP_F_U64      : VOPC_Real_gfx11<0x058>;
defm V_CMP_LT_U64     : VOPC_Real_gfx11_gfx12<0x059>;
defm V_CMP_EQ_U64     : VOPC_Real_gfx11_gfx12<0x05a>;
defm V_CMP_LE_U64     : VOPC_Real_gfx11_gfx12<0x05b>;
defm V_CMP_GT_U64     : VOPC_Real_gfx11_gfx12<0x05c>;
defm V_CMP_NE_U64     : VOPC_Real_gfx11_gfx12<0x05d>;
defm V_CMP_GE_U64     : VOPC_Real_gfx11_gfx12<0x05e>;
defm V_CMP_T_U64      : VOPC_Real_gfx11<0x05f>;

defm V_CMP_CLASS_F16_t16 : VOPC_Real_t16_gfx11_gfx12<0x07d, "v_cmp_class_f16">;
defm V_CMP_CLASS_F32     : VOPC_Real_gfx11_gfx12<0x07e>;
defm V_CMP_CLASS_F64     : VOPC_Real_gfx11_gfx12<0x07f>;

defm V_CMPX_F_F16_t16     : VOPCX_Real_t16_gfx11<0x080, "v_cmpx_f_f16">;
defm V_CMPX_LT_F16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x081, "v_cmpx_lt_f16">;
defm V_CMPX_EQ_F16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x082, "v_cmpx_eq_f16">;
defm V_CMPX_LE_F16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x083, "v_cmpx_le_f16">;
defm V_CMPX_GT_F16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x084, "v_cmpx_gt_f16">;
defm V_CMPX_LG_F16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x085, "v_cmpx_lg_f16">;
defm V_CMPX_GE_F16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x086, "v_cmpx_ge_f16">;
defm V_CMPX_O_F16_t16     : VOPCX_Real_t16_gfx11_gfx12<0x087, "v_cmpx_o_f16">;
defm V_CMPX_U_F16_t16     : VOPCX_Real_t16_gfx11_gfx12<0x088, "v_cmpx_u_f16">;
defm V_CMPX_NGE_F16_t16   : VOPCX_Real_t16_gfx11_gfx12<0x089, "v_cmpx_nge_f16">;
defm V_CMPX_NLG_F16_t16   : VOPCX_Real_t16_gfx11_gfx12<0x08a, "v_cmpx_nlg_f16">;
defm V_CMPX_NGT_F16_t16   : VOPCX_Real_t16_gfx11_gfx12<0x08b, "v_cmpx_ngt_f16">;
defm V_CMPX_NLE_F16_t16   : VOPCX_Real_t16_gfx11_gfx12<0x08c, "v_cmpx_nle_f16">;
defm V_CMPX_NEQ_F16_t16   : VOPCX_Real_t16_gfx11_gfx12<0x08d, "v_cmpx_neq_f16">;
defm V_CMPX_NLT_F16_t16   : VOPCX_Real_t16_gfx11_gfx12<0x08e, "v_cmpx_nlt_f16">;
defm V_CMPX_T_F16_t16     : VOPCX_Real_with_name_gfx11<0x08f, "V_CMPX_TRU_F16_t16", "v_cmpx_t_f16", "v_cmpx_tru_f16">;
defm V_CMPX_F_F32     : VOPCX_Real_gfx11<0x090>;
defm V_CMPX_LT_F32    : VOPCX_Real_gfx11_gfx12<0x091>;
defm V_CMPX_EQ_F32    : VOPCX_Real_gfx11_gfx12<0x092>;
defm V_CMPX_LE_F32    : VOPCX_Real_gfx11_gfx12<0x093>;
defm V_CMPX_GT_F32    : VOPCX_Real_gfx11_gfx12<0x094>;
defm V_CMPX_LG_F32    : VOPCX_Real_gfx11_gfx12<0x095>;
defm V_CMPX_GE_F32    : VOPCX_Real_gfx11_gfx12<0x096>;
defm V_CMPX_O_F32     : VOPCX_Real_gfx11_gfx12<0x097>;
defm V_CMPX_U_F32     : VOPCX_Real_gfx11_gfx12<0x098>;
defm V_CMPX_NGE_F32   : VOPCX_Real_gfx11_gfx12<0x099>;
defm V_CMPX_NLG_F32   : VOPCX_Real_gfx11_gfx12<0x09a>;
defm V_CMPX_NGT_F32   : VOPCX_Real_gfx11_gfx12<0x09b>;
defm V_CMPX_NLE_F32   : VOPCX_Real_gfx11_gfx12<0x09c>;
defm V_CMPX_NEQ_F32   : VOPCX_Real_gfx11_gfx12<0x09d>;
defm V_CMPX_NLT_F32   : VOPCX_Real_gfx11_gfx12<0x09e>;
defm V_CMPX_T_F32     : VOPCX_Real_with_name_gfx11<0x09f, "V_CMPX_TRU_F32", "v_cmpx_t_f32">;

defm V_CMPX_F_F64     : VOPCX_Real_gfx11<0x0a0>;
defm V_CMPX_LT_F64    : VOPCX_Real_gfx11_gfx12<0x0a1>;
defm V_CMPX_EQ_F64    : VOPCX_Real_gfx11_gfx12<0x0a2>;
defm V_CMPX_LE_F64    : VOPCX_Real_gfx11_gfx12<0x0a3>;
defm V_CMPX_GT_F64    : VOPCX_Real_gfx11_gfx12<0x0a4>;
defm V_CMPX_LG_F64    : VOPCX_Real_gfx11_gfx12<0x0a5>;
defm V_CMPX_GE_F64    : VOPCX_Real_gfx11_gfx12<0x0a6>;
defm V_CMPX_O_F64     : VOPCX_Real_gfx11_gfx12<0x0a7>;
defm V_CMPX_U_F64     : VOPCX_Real_gfx11_gfx12<0x0a8>;
defm V_CMPX_NGE_F64   : VOPCX_Real_gfx11_gfx12<0x0a9>;
defm V_CMPX_NLG_F64   : VOPCX_Real_gfx11_gfx12<0x0aa>;
defm V_CMPX_NGT_F64   : VOPCX_Real_gfx11_gfx12<0x0ab>;
defm V_CMPX_NLE_F64   : VOPCX_Real_gfx11_gfx12<0x0ac>;
defm V_CMPX_NEQ_F64   : VOPCX_Real_gfx11_gfx12<0x0ad>;
defm V_CMPX_NLT_F64   : VOPCX_Real_gfx11_gfx12<0x0ae>;
defm V_CMPX_T_F64     : VOPCX_Real_with_name_gfx11<0x0af, "V_CMPX_TRU_F64", "v_cmpx_t_f64">;

defm V_CMPX_LT_I16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x0b1, "v_cmpx_lt_i16">;
defm V_CMPX_EQ_I16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x0b2, "v_cmpx_eq_i16">;
defm V_CMPX_LE_I16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x0b3, "v_cmpx_le_i16">;
defm V_CMPX_GT_I16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x0b4, "v_cmpx_gt_i16">;
defm V_CMPX_NE_I16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x0b5, "v_cmpx_ne_i16">;
defm V_CMPX_GE_I16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x0b6, "v_cmpx_ge_i16">;
defm V_CMPX_LT_U16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x0b9, "v_cmpx_lt_u16">;
defm V_CMPX_EQ_U16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x0ba, "v_cmpx_eq_u16">;
defm V_CMPX_LE_U16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x0bb, "v_cmpx_le_u16">;
defm V_CMPX_GT_U16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x0bc, "v_cmpx_gt_u16">;
defm V_CMPX_NE_U16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x0bd, "v_cmpx_ne_u16">;
defm V_CMPX_GE_U16_t16    : VOPCX_Real_t16_gfx11_gfx12<0x0be, "v_cmpx_ge_u16">;
defm V_CMPX_F_I32     : VOPCX_Real_gfx11<0x0c0>;
defm V_CMPX_LT_I32    : VOPCX_Real_gfx11_gfx12<0x0c1>;
defm V_CMPX_EQ_I32    : VOPCX_Real_gfx11_gfx12<0x0c2>;
defm V_CMPX_LE_I32    : VOPCX_Real_gfx11_gfx12<0x0c3>;
defm V_CMPX_GT_I32    : VOPCX_Real_gfx11_gfx12<0x0c4>;
defm V_CMPX_NE_I32    : VOPCX_Real_gfx11_gfx12<0x0c5>;
defm V_CMPX_GE_I32    : VOPCX_Real_gfx11_gfx12<0x0c6>;
defm V_CMPX_T_I32     : VOPCX_Real_gfx11<0x0c7>;
defm V_CMPX_F_U32     : VOPCX_Real_gfx11<0x0c8>;
defm V_CMPX_LT_U32    : VOPCX_Real_gfx11_gfx12<0x0c9>;
defm V_CMPX_EQ_U32    : VOPCX_Real_gfx11_gfx12<0x0ca>;
defm V_CMPX_LE_U32    : VOPCX_Real_gfx11_gfx12<0x0cb>;
defm V_CMPX_GT_U32    : VOPCX_Real_gfx11_gfx12<0x0cc>;
defm V_CMPX_NE_U32    : VOPCX_Real_gfx11_gfx12<0x0cd>;
defm V_CMPX_GE_U32    : VOPCX_Real_gfx11_gfx12<0x0ce>;
defm V_CMPX_T_U32     : VOPCX_Real_gfx11<0x0cf>;

defm V_CMPX_F_I64     : VOPCX_Real_gfx11<0x0d0>;
defm V_CMPX_LT_I64    : VOPCX_Real_gfx11_gfx12<0x0d1>;
defm V_CMPX_EQ_I64    : VOPCX_Real_gfx11_gfx12<0x0d2>;
defm V_CMPX_LE_I64    : VOPCX_Real_gfx11_gfx12<0x0d3>;
defm V_CMPX_GT_I64    : VOPCX_Real_gfx11_gfx12<0x0d4>;
defm V_CMPX_NE_I64    : VOPCX_Real_gfx11_gfx12<0x0d5>;
defm V_CMPX_GE_I64    : VOPCX_Real_gfx11_gfx12<0x0d6>;
defm V_CMPX_T_I64     : VOPCX_Real_gfx11<0x0d7>;
defm V_CMPX_F_U64     : VOPCX_Real_gfx11<0x0d8>;
defm V_CMPX_LT_U64    : VOPCX_Real_gfx11_gfx12<0x0d9>;
defm V_CMPX_EQ_U64    : VOPCX_Real_gfx11_gfx12<0x0da>;
defm V_CMPX_LE_U64    : VOPCX_Real_gfx11_gfx12<0x0db>;
defm V_CMPX_GT_U64    : VOPCX_Real_gfx11_gfx12<0x0dc>;
defm V_CMPX_NE_U64    : VOPCX_Real_gfx11_gfx12<0x0dd>;
defm V_CMPX_GE_U64    : VOPCX_Real_gfx11_gfx12<0x0de>;
defm V_CMPX_T_U64     : VOPCX_Real_gfx11<0x0df>;
defm V_CMPX_CLASS_F16_t16 : VOPCX_Real_t16_gfx11_gfx12<0x0fd, "v_cmpx_class_f16">;
defm V_CMPX_CLASS_F32     : VOPCX_Real_gfx11_gfx12<0x0fe>;
defm V_CMPX_CLASS_F64     : VOPCX_Real_gfx11_gfx12<0x0ff>;

//===----------------------------------------------------------------------===//
// GFX10.
//===----------------------------------------------------------------------===//

let AssemblerPredicate = isGFX10Only, DecoderNamespace = "GFX10" in {
  multiclass VOPC_Real_gfx10<bits<9> op> {
    def _e32_gfx10 :
      VOPC_Real<!cast<VOPC_Pseudo>(NAME#"_e32"), SIEncodingFamily.GFX10>,
      VOPCe<op{7-0}>;
    def _e64_gfx10 :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.GFX10>,
      VOP3a_gfx10<{0, op}, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl> {
      // Encoding used for VOPC instructions encoded as VOP3 differs from
      // VOP3e by destination name (sdst) as VOPC doesn't have vector dst.
      bits<8> sdst;
      let Inst{7-0} = sdst;
    }

    if !cast<VOPC_Pseudo>(NAME#"_e32").Pfl.HasExtSDWA9 then
    def _sdwa_gfx10 :
      VOP_SDWA10_Real<!cast<VOPC_SDWA_Pseudo>(NAME#"_sdwa")>,
      VOPC_SDWA9e<op{7-0}, !cast<VOPC_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;

    defm : VOPCInstAliases<NAME, "gfx10">;
  }

  multiclass VOPCX_Real_gfx10<bits<9> op> {
    def _e32_gfx10 :
      VOPC_Real<!cast<VOPC_Pseudo>(NAME#"_nosdst_e32"), SIEncodingFamily.GFX10>,
      VOPCe<op{7-0}> {
        let AsmString = !subst("_nosdst", "", !cast<VOPC_Pseudo>(NAME#"_nosdst_e32").PseudoInstr)
                        # " " # !cast<VOPC_Pseudo>(NAME#"_nosdst_e32").AsmOperands;
    }

    def _e64_gfx10 :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_nosdst_e64"), SIEncodingFamily.GFX10>,
      VOP3a_gfx10<{0, op}, !cast<VOP3_Pseudo>(NAME#"_nosdst_e64").Pfl> {
        let Inst{7-0} = ?; // sdst
        let AsmString = !subst("_nosdst", "", !cast<VOP3_Pseudo>(NAME#"_nosdst_e64").Mnemonic)
                        # "{_e64} " # !cast<VOP3_Pseudo>(NAME#"_nosdst_e64").AsmOperands;
    }

    if !cast<VOPC_Pseudo>(NAME#"_nosdst_e32").Pfl.HasExtSDWA9 then
    def _sdwa_gfx10 :
      VOP_SDWA10_Real<!cast<VOPC_SDWA_Pseudo>(NAME#"_nosdst_sdwa")>,
      VOPC_SDWA9e<op{7-0}, !cast<VOPC_SDWA_Pseudo>(NAME#"_nosdst_sdwa").Pfl> {
        let AsmString = !subst("_nosdst", "", !cast<VOPC_SDWA_Pseudo>(NAME#"_nosdst_sdwa").Mnemonic)
                        # "{_sdwa} " # !cast<VOPC_SDWA_Pseudo>(NAME#"_nosdst_sdwa").AsmOperands9;
      }

    defm : VOPCXInstAliases<NAME, "gfx10">;
  }
} // End AssemblerPredicate = isGFX10Only, DecoderNamespace = "GFX10"

defm V_CMP_LT_I16     : VOPC_Real_gfx10<0x089>;
defm V_CMP_EQ_I16     : VOPC_Real_gfx10<0x08a>;
defm V_CMP_LE_I16     : VOPC_Real_gfx10<0x08b>;
defm V_CMP_GT_I16     : VOPC_Real_gfx10<0x08c>;
defm V_CMP_NE_I16     : VOPC_Real_gfx10<0x08d>;
defm V_CMP_GE_I16     : VOPC_Real_gfx10<0x08e>;
defm V_CMP_CLASS_F16  : VOPC_Real_gfx10<0x08f>;
defm V_CMPX_LT_I16    : VOPCX_Real_gfx10<0x099>;
defm V_CMPX_EQ_I16    : VOPCX_Real_gfx10<0x09a>;
defm V_CMPX_LE_I16    : VOPCX_Real_gfx10<0x09b>;
defm V_CMPX_GT_I16    : VOPCX_Real_gfx10<0x09c>;
defm V_CMPX_NE_I16    : VOPCX_Real_gfx10<0x09d>;
defm V_CMPX_GE_I16    : VOPCX_Real_gfx10<0x09e>;
defm V_CMPX_CLASS_F16 : VOPCX_Real_gfx10<0x09f>;
defm V_CMP_LT_U16     : VOPC_Real_gfx10<0x0a9>;
defm V_CMP_EQ_U16     : VOPC_Real_gfx10<0x0aa>;
defm V_CMP_LE_U16     : VOPC_Real_gfx10<0x0ab>;
defm V_CMP_GT_U16     : VOPC_Real_gfx10<0x0ac>;
defm V_CMP_NE_U16     : VOPC_Real_gfx10<0x0ad>;
defm V_CMP_GE_U16     : VOPC_Real_gfx10<0x0ae>;
defm V_CMPX_LT_U16    : VOPCX_Real_gfx10<0x0b9>;
defm V_CMPX_EQ_U16    : VOPCX_Real_gfx10<0x0ba>;
defm V_CMPX_LE_U16    : VOPCX_Real_gfx10<0x0bb>;
defm V_CMPX_GT_U16    : VOPCX_Real_gfx10<0x0bc>;
defm V_CMPX_NE_U16    : VOPCX_Real_gfx10<0x0bd>;
defm V_CMPX_GE_U16    : VOPCX_Real_gfx10<0x0be>;
defm V_CMP_F_F16      : VOPC_Real_gfx10<0x0c8>;
defm V_CMP_LT_F16     : VOPC_Real_gfx10<0x0c9>;
defm V_CMP_EQ_F16     : VOPC_Real_gfx10<0x0ca>;
defm V_CMP_LE_F16     : VOPC_Real_gfx10<0x0cb>;
defm V_CMP_GT_F16     : VOPC_Real_gfx10<0x0cc>;
defm V_CMP_LG_F16     : VOPC_Real_gfx10<0x0cd>;
defm V_CMP_GE_F16     : VOPC_Real_gfx10<0x0ce>;
defm V_CMP_O_F16      : VOPC_Real_gfx10<0x0cf>;
defm V_CMPX_F_F16     : VOPCX_Real_gfx10<0x0d8>;
defm V_CMPX_LT_F16    : VOPCX_Real_gfx10<0x0d9>;
defm V_CMPX_EQ_F16    : VOPCX_Real_gfx10<0x0da>;
defm V_CMPX_LE_F16    : VOPCX_Real_gfx10<0x0db>;
defm V_CMPX_GT_F16    : VOPCX_Real_gfx10<0x0dc>;
defm V_CMPX_LG_F16    : VOPCX_Real_gfx10<0x0dd>;
defm V_CMPX_GE_F16    : VOPCX_Real_gfx10<0x0de>;
defm V_CMPX_O_F16     : VOPCX_Real_gfx10<0x0df>;
defm V_CMP_U_F16      : VOPC_Real_gfx10<0x0e8>;
defm V_CMP_NGE_F16    : VOPC_Real_gfx10<0x0e9>;
defm V_CMP_NLG_F16    : VOPC_Real_gfx10<0x0ea>;
defm V_CMP_NGT_F16    : VOPC_Real_gfx10<0x0eb>;
defm V_CMP_NLE_F16    : VOPC_Real_gfx10<0x0ec>;
defm V_CMP_NEQ_F16    : VOPC_Real_gfx10<0x0ed>;
defm V_CMP_NLT_F16    : VOPC_Real_gfx10<0x0ee>;
defm V_CMP_TRU_F16    : VOPC_Real_gfx10<0x0ef>;
defm V_CMPX_U_F16     : VOPCX_Real_gfx10<0x0f8>;
defm V_CMPX_NGE_F16   : VOPCX_Real_gfx10<0x0f9>;
defm V_CMPX_NLG_F16   : VOPCX_Real_gfx10<0x0fa>;
defm V_CMPX_NGT_F16   : VOPCX_Real_gfx10<0x0fb>;
defm V_CMPX_NLE_F16   : VOPCX_Real_gfx10<0x0fc>;
defm V_CMPX_NEQ_F16   : VOPCX_Real_gfx10<0x0fd>;
defm V_CMPX_NLT_F16   : VOPCX_Real_gfx10<0x0fe>;
defm V_CMPX_TRU_F16   : VOPCX_Real_gfx10<0x0ff>;

//===----------------------------------------------------------------------===//
// GFX6, GFX7, GFX10.
//===----------------------------------------------------------------------===//

let AssemblerPredicate = isGFX6GFX7, DecoderNamespace = "GFX6GFX7" in {
  multiclass VOPC_Real_gfx6_gfx7<bits<9> op> {
    def _e32_gfx6_gfx7 :
      VOPC_Real<!cast<VOPC_Pseudo>(NAME#"_e32"), SIEncodingFamily.SI>,
      VOPCe<op{7-0}>;
    def _e64_gfx6_gfx7 :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.SI>,
      VOP3a_gfx6_gfx7<op, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl> {
      // Encoding used for VOPC instructions encoded as VOP3 differs from
      // VOP3e by destination name (sdst) as VOPC doesn't have vector dst.
      bits<8> sdst;
      let Inst{7-0} = sdst;
    }

    defm : VOPCInstAliases<NAME, "gfx6_gfx7">;
  }
} // End AssemblerPredicate = isGFX6GFX7, DecoderNamespace = "GFX6GFX7"

multiclass VOPC_Real_gfx6_gfx7_gfx10<bits<9> op> :
  VOPC_Real_gfx6_gfx7<op>, VOPC_Real_gfx10<op>;

multiclass VOPCX_Real_gfx6_gfx7<bits<9> op> :
  VOPC_Real_gfx6_gfx7<op>;

multiclass VOPCX_Real_gfx6_gfx7_gfx10 <bits<9> op> :
  VOPC_Real_gfx6_gfx7<op>, VOPCX_Real_gfx10<op>;

multiclass VOPC_Real_gfx6_gfx7_gfx10_gfx11<bits<9> op> :
  VOPC_Real_gfx6_gfx7_gfx10<op>, VOPC_Real_Base<GFX11Gen, op>;

multiclass VOPCX_Real_gfx6_gfx7_gfx10_gfx11<bits<9> op> :
  VOPCX_Real_gfx6_gfx7_gfx10<op>, VOPCX_Real<GFX11Gen, op>;

multiclass VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<bits<9> op> :
  VOPC_Real_gfx6_gfx7_gfx10_gfx11<op>, VOPC_Real_Base<GFX12Gen, op>;

defm V_CMP_F_F32      : VOPC_Real_gfx6_gfx7_gfx10<0x000>;
defm V_CMP_LT_F32     : VOPC_Real_gfx6_gfx7_gfx10<0x001>;
defm V_CMP_EQ_F32     : VOPC_Real_gfx6_gfx7_gfx10<0x002>;
defm V_CMP_LE_F32     : VOPC_Real_gfx6_gfx7_gfx10<0x003>;
defm V_CMP_GT_F32     : VOPC_Real_gfx6_gfx7_gfx10<0x004>;
defm V_CMP_LG_F32     : VOPC_Real_gfx6_gfx7_gfx10<0x005>;
defm V_CMP_GE_F32     : VOPC_Real_gfx6_gfx7_gfx10<0x006>;
defm V_CMP_O_F32      : VOPC_Real_gfx6_gfx7_gfx10<0x007>;
defm V_CMP_U_F32      : VOPC_Real_gfx6_gfx7_gfx10<0x008>;
defm V_CMP_NGE_F32    : VOPC_Real_gfx6_gfx7_gfx10<0x009>;
defm V_CMP_NLG_F32    : VOPC_Real_gfx6_gfx7_gfx10<0x00a>;
defm V_CMP_NGT_F32    : VOPC_Real_gfx6_gfx7_gfx10<0x00b>;
defm V_CMP_NLE_F32    : VOPC_Real_gfx6_gfx7_gfx10<0x00c>;
defm V_CMP_NEQ_F32    : VOPC_Real_gfx6_gfx7_gfx10<0x00d>;
defm V_CMP_NLT_F32    : VOPC_Real_gfx6_gfx7_gfx10<0x00e>;
defm V_CMP_TRU_F32    : VOPC_Real_gfx6_gfx7_gfx10<0x00f>;
defm V_CMPX_F_F32     : VOPCX_Real_gfx6_gfx7_gfx10<0x010>;
defm V_CMPX_LT_F32    : VOPCX_Real_gfx6_gfx7_gfx10<0x011>;
defm V_CMPX_EQ_F32    : VOPCX_Real_gfx6_gfx7_gfx10<0x012>;
defm V_CMPX_LE_F32    : VOPCX_Real_gfx6_gfx7_gfx10<0x013>;
defm V_CMPX_GT_F32    : VOPCX_Real_gfx6_gfx7_gfx10<0x014>;
defm V_CMPX_LG_F32    : VOPCX_Real_gfx6_gfx7_gfx10<0x015>;
defm V_CMPX_GE_F32    : VOPCX_Real_gfx6_gfx7_gfx10<0x016>;
defm V_CMPX_O_F32     : VOPCX_Real_gfx6_gfx7_gfx10<0x017>;
defm V_CMPX_U_F32     : VOPCX_Real_gfx6_gfx7_gfx10<0x018>;
defm V_CMPX_NGE_F32   : VOPCX_Real_gfx6_gfx7_gfx10<0x019>;
defm V_CMPX_NLG_F32   : VOPCX_Real_gfx6_gfx7_gfx10<0x01a>;
defm V_CMPX_NGT_F32   : VOPCX_Real_gfx6_gfx7_gfx10<0x01b>;
defm V_CMPX_NLE_F32   : VOPCX_Real_gfx6_gfx7_gfx10<0x01c>;
defm V_CMPX_NEQ_F32   : VOPCX_Real_gfx6_gfx7_gfx10<0x01d>;
defm V_CMPX_NLT_F32   : VOPCX_Real_gfx6_gfx7_gfx10<0x01e>;
defm V_CMPX_TRU_F32   : VOPCX_Real_gfx6_gfx7_gfx10<0x01f>;
defm V_CMP_F_F64      : VOPC_Real_gfx6_gfx7_gfx10_gfx11<0x020>;
defm V_CMP_LT_F64     : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x021>;
defm V_CMP_EQ_F64     : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x022>;
defm V_CMP_LE_F64     : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x023>;
defm V_CMP_GT_F64     : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x024>;
defm V_CMP_LG_F64     : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x025>;
defm V_CMP_GE_F64     : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x026>;
defm V_CMP_O_F64      : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x027>;
defm V_CMP_U_F64      : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x028>;
defm V_CMP_NGE_F64    : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x029>;
defm V_CMP_NLG_F64    : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x02a>;
defm V_CMP_NGT_F64    : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x02b>;
defm V_CMP_NLE_F64    : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x02c>;
defm V_CMP_NEQ_F64    : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x02d>;
defm V_CMP_NLT_F64    : VOPC_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x02e>;
defm V_CMP_TRU_F64    : VOPC_Real_gfx6_gfx7_gfx10<0x02f>;
defm V_CMPX_F_F64     : VOPCX_Real_gfx6_gfx7_gfx10<0x030>;
defm V_CMPX_LT_F64    : VOPCX_Real_gfx6_gfx7_gfx10<0x031>;
defm V_CMPX_EQ_F64    : VOPCX_Real_gfx6_gfx7_gfx10<0x032>;
defm V_CMPX_LE_F64    : VOPCX_Real_gfx6_gfx7_gfx10<0x033>;
defm V_CMPX_GT_F64    : VOPCX_Real_gfx6_gfx7_gfx10<0x034>;
defm V_CMPX_LG_F64    : VOPCX_Real_gfx6_gfx7_gfx10<0x035>;
defm V_CMPX_GE_F64    : VOPCX_Real_gfx6_gfx7_gfx10<0x036>;
defm V_CMPX_O_F64     : VOPCX_Real_gfx6_gfx7_gfx10<0x037>;
defm V_CMPX_U_F64     : VOPCX_Real_gfx6_gfx7_gfx10<0x038>;
defm V_CMPX_NGE_F64   : VOPCX_Real_gfx6_gfx7_gfx10<0x039>;
defm V_CMPX_NLG_F64   : VOPCX_Real_gfx6_gfx7_gfx10<0x03a>;
defm V_CMPX_NGT_F64   : VOPCX_Real_gfx6_gfx7_gfx10<0x03b>;
defm V_CMPX_NLE_F64   : VOPCX_Real_gfx6_gfx7_gfx10<0x03c>;
defm V_CMPX_NEQ_F64   : VOPCX_Real_gfx6_gfx7_gfx10<0x03d>;
defm V_CMPX_NLT_F64   : VOPCX_Real_gfx6_gfx7_gfx10<0x03e>;
defm V_CMPX_TRU_F64   : VOPCX_Real_gfx6_gfx7_gfx10<0x03f>;
defm V_CMPS_F_F32     : VOPC_Real_gfx6_gfx7<0x040>;
defm V_CMPS_LT_F32    : VOPC_Real_gfx6_gfx7<0x041>;
defm V_CMPS_EQ_F32    : VOPC_Real_gfx6_gfx7<0x042>;
defm V_CMPS_LE_F32    : VOPC_Real_gfx6_gfx7<0x043>;
defm V_CMPS_GT_F32    : VOPC_Real_gfx6_gfx7<0x044>;
defm V_CMPS_LG_F32    : VOPC_Real_gfx6_gfx7<0x045>;
defm V_CMPS_GE_F32    : VOPC_Real_gfx6_gfx7<0x046>;
defm V_CMPS_O_F32     : VOPC_Real_gfx6_gfx7<0x047>;
defm V_CMPS_U_F32     : VOPC_Real_gfx6_gfx7<0x048>;
defm V_CMPS_NGE_F32   : VOPC_Real_gfx6_gfx7<0x049>;
defm V_CMPS_NLG_F32   : VOPC_Real_gfx6_gfx7<0x04a>;
defm V_CMPS_NGT_F32   : VOPC_Real_gfx6_gfx7<0x04b>;
defm V_CMPS_NLE_F32   : VOPC_Real_gfx6_gfx7<0x04c>;
defm V_CMPS_NEQ_F32   : VOPC_Real_gfx6_gfx7<0x04d>;
defm V_CMPS_NLT_F32   : VOPC_Real_gfx6_gfx7<0x04e>;
defm V_CMPS_TRU_F32   : VOPC_Real_gfx6_gfx7<0x04f>;
defm V_CMPSX_F_F32    : VOPCX_Real_gfx6_gfx7<0x050>;
defm V_CMPSX_LT_F32   : VOPCX_Real_gfx6_gfx7<0x051>;
defm V_CMPSX_EQ_F32   : VOPCX_Real_gfx6_gfx7<0x052>;
defm V_CMPSX_LE_F32   : VOPCX_Real_gfx6_gfx7<0x053>;
defm V_CMPSX_GT_F32   : VOPCX_Real_gfx6_gfx7<0x054>;
defm V_CMPSX_LG_F32   : VOPCX_Real_gfx6_gfx7<0x055>;
defm V_CMPSX_GE_F32   : VOPCX_Real_gfx6_gfx7<0x056>;
defm V_CMPSX_O_F32    : VOPCX_Real_gfx6_gfx7<0x057>;
defm V_CMPSX_U_F32    : VOPCX_Real_gfx6_gfx7<0x058>;
defm V_CMPSX_NGE_F32  : VOPCX_Real_gfx6_gfx7<0x059>;
defm V_CMPSX_NLG_F32  : VOPCX_Real_gfx6_gfx7<0x05a>;
defm V_CMPSX_NGT_F32  : VOPCX_Real_gfx6_gfx7<0x05b>;
defm V_CMPSX_NLE_F32  : VOPCX_Real_gfx6_gfx7<0x05c>;
defm V_CMPSX_NEQ_F32  : VOPCX_Real_gfx6_gfx7<0x05d>;
defm V_CMPSX_NLT_F32  : VOPCX_Real_gfx6_gfx7<0x05e>;
defm V_CMPSX_TRU_F32  : VOPCX_Real_gfx6_gfx7<0x05f>;
defm V_CMPS_F_F64     : VOPC_Real_gfx6_gfx7<0x060>;
defm V_CMPS_LT_F64    : VOPC_Real_gfx6_gfx7<0x061>;
defm V_CMPS_EQ_F64    : VOPC_Real_gfx6_gfx7<0x062>;
defm V_CMPS_LE_F64    : VOPC_Real_gfx6_gfx7<0x063>;
defm V_CMPS_GT_F64    : VOPC_Real_gfx6_gfx7<0x064>;
defm V_CMPS_LG_F64    : VOPC_Real_gfx6_gfx7<0x065>;
defm V_CMPS_GE_F64    : VOPC_Real_gfx6_gfx7<0x066>;
defm V_CMPS_O_F64     : VOPC_Real_gfx6_gfx7<0x067>;
defm V_CMPS_U_F64     : VOPC_Real_gfx6_gfx7<0x068>;
defm V_CMPS_NGE_F64   : VOPC_Real_gfx6_gfx7<0x069>;
defm V_CMPS_NLG_F64   : VOPC_Real_gfx6_gfx7<0x06a>;
defm V_CMPS_NGT_F64   : VOPC_Real_gfx6_gfx7<0x06b>;
defm V_CMPS_NLE_F64   : VOPC_Real_gfx6_gfx7<0x06c>;
defm V_CMPS_NEQ_F64   : VOPC_Real_gfx6_gfx7<0x06d>;
defm V_CMPS_NLT_F64   : VOPC_Real_gfx6_gfx7<0x06e>;
defm V_CMPS_TRU_F64   : VOPC_Real_gfx6_gfx7<0x06f>;
defm V_CMPSX_F_F64    : VOPCX_Real_gfx6_gfx7<0x070>;
defm V_CMPSX_LT_F64   : VOPCX_Real_gfx6_gfx7<0x071>;
defm V_CMPSX_EQ_F64   : VOPCX_Real_gfx6_gfx7<0x072>;
defm V_CMPSX_LE_F64   : VOPCX_Real_gfx6_gfx7<0x073>;
defm V_CMPSX_GT_F64   : VOPCX_Real_gfx6_gfx7<0x074>;
defm V_CMPSX_LG_F64   : VOPCX_Real_gfx6_gfx7<0x075>;
defm V_CMPSX_GE_F64   : VOPCX_Real_gfx6_gfx7<0x076>;
defm V_CMPSX_O_F64    : VOPCX_Real_gfx6_gfx7<0x077>;
defm V_CMPSX_U_F64    : VOPCX_Real_gfx6_gfx7<0x078>;
defm V_CMPSX_NGE_F64  : VOPCX_Real_gfx6_gfx7<0x079>;
defm V_CMPSX_NLG_F64  : VOPCX_Real_gfx6_gfx7<0x07a>;
defm V_CMPSX_NGT_F64  : VOPCX_Real_gfx6_gfx7<0x07b>;
defm V_CMPSX_NLE_F64  : VOPCX_Real_gfx6_gfx7<0x07c>;
defm V_CMPSX_NEQ_F64  : VOPCX_Real_gfx6_gfx7<0x07d>;
defm V_CMPSX_NLT_F64  : VOPCX_Real_gfx6_gfx7<0x07e>;
defm V_CMPSX_TRU_F64  : VOPCX_Real_gfx6_gfx7<0x07f>;
defm V_CMP_F_I32      : VOPC_Real_gfx6_gfx7_gfx10<0x080>;
defm V_CMP_LT_I32     : VOPC_Real_gfx6_gfx7_gfx10<0x081>;
defm V_CMP_EQ_I32     : VOPC_Real_gfx6_gfx7_gfx10<0x082>;
defm V_CMP_LE_I32     : VOPC_Real_gfx6_gfx7_gfx10<0x083>;
defm V_CMP_GT_I32     : VOPC_Real_gfx6_gfx7_gfx10<0x084>;
defm V_CMP_NE_I32     : VOPC_Real_gfx6_gfx7_gfx10<0x085>;
defm V_CMP_GE_I32     : VOPC_Real_gfx6_gfx7_gfx10<0x086>;
defm V_CMP_T_I32      : VOPC_Real_gfx6_gfx7_gfx10<0x087>;
defm V_CMP_CLASS_F32  : VOPC_Real_gfx6_gfx7_gfx10<0x088>;
defm V_CMPX_F_I32     : VOPCX_Real_gfx6_gfx7_gfx10<0x090>;
defm V_CMPX_LT_I32    : VOPCX_Real_gfx6_gfx7_gfx10<0x091>;
defm V_CMPX_EQ_I32    : VOPCX_Real_gfx6_gfx7_gfx10<0x092>;
defm V_CMPX_LE_I32    : VOPCX_Real_gfx6_gfx7_gfx10<0x093>;
defm V_CMPX_GT_I32    : VOPCX_Real_gfx6_gfx7_gfx10<0x094>;
defm V_CMPX_NE_I32    : VOPCX_Real_gfx6_gfx7_gfx10<0x095>;
defm V_CMPX_GE_I32    : VOPCX_Real_gfx6_gfx7_gfx10<0x096>;
defm V_CMPX_T_I32     : VOPCX_Real_gfx6_gfx7_gfx10<0x097>;
defm V_CMPX_CLASS_F32 : VOPCX_Real_gfx6_gfx7_gfx10<0x098>;
defm V_CMP_F_I64      : VOPC_Real_gfx6_gfx7_gfx10<0x0a0>;
defm V_CMP_LT_I64     : VOPC_Real_gfx6_gfx7_gfx10<0x0a1>;
defm V_CMP_EQ_I64     : VOPC_Real_gfx6_gfx7_gfx10<0x0a2>;
defm V_CMP_LE_I64     : VOPC_Real_gfx6_gfx7_gfx10<0x0a3>;
defm V_CMP_GT_I64     : VOPC_Real_gfx6_gfx7_gfx10<0x0a4>;
defm V_CMP_NE_I64     : VOPC_Real_gfx6_gfx7_gfx10<0x0a5>;
defm V_CMP_GE_I64     : VOPC_Real_gfx6_gfx7_gfx10<0x0a6>;
defm V_CMP_T_I64      : VOPC_Real_gfx6_gfx7_gfx10<0x0a7>;
defm V_CMP_CLASS_F64  : VOPC_Real_gfx6_gfx7_gfx10<0x0a8>;
defm V_CMPX_F_I64     : VOPCX_Real_gfx6_gfx7_gfx10<0x0b0>;
defm V_CMPX_LT_I64    : VOPCX_Real_gfx6_gfx7_gfx10<0x0b1>;
defm V_CMPX_EQ_I64    : VOPCX_Real_gfx6_gfx7_gfx10<0x0b2>;
defm V_CMPX_LE_I64    : VOPCX_Real_gfx6_gfx7_gfx10<0x0b3>;
defm V_CMPX_GT_I64    : VOPCX_Real_gfx6_gfx7_gfx10<0x0b4>;
defm V_CMPX_NE_I64    : VOPCX_Real_gfx6_gfx7_gfx10<0x0b5>;
defm V_CMPX_GE_I64    : VOPCX_Real_gfx6_gfx7_gfx10<0x0b6>;
defm V_CMPX_T_I64     : VOPCX_Real_gfx6_gfx7_gfx10<0x0b7>;
defm V_CMPX_CLASS_F64 : VOPCX_Real_gfx6_gfx7_gfx10<0x0b8>;
defm V_CMP_F_U32      : VOPC_Real_gfx6_gfx7_gfx10<0x0c0>;
defm V_CMP_LT_U32     : VOPC_Real_gfx6_gfx7_gfx10<0x0c1>;
defm V_CMP_EQ_U32     : VOPC_Real_gfx6_gfx7_gfx10<0x0c2>;
defm V_CMP_LE_U32     : VOPC_Real_gfx6_gfx7_gfx10<0x0c3>;
defm V_CMP_GT_U32     : VOPC_Real_gfx6_gfx7_gfx10<0x0c4>;
defm V_CMP_NE_U32     : VOPC_Real_gfx6_gfx7_gfx10<0x0c5>;
defm V_CMP_GE_U32     : VOPC_Real_gfx6_gfx7_gfx10<0x0c6>;
defm V_CMP_T_U32      : VOPC_Real_gfx6_gfx7_gfx10<0x0c7>;
defm V_CMPX_F_U32     : VOPCX_Real_gfx6_gfx7_gfx10<0x0d0>;
defm V_CMPX_LT_U32    : VOPCX_Real_gfx6_gfx7_gfx10<0x0d1>;
defm V_CMPX_EQ_U32    : VOPCX_Real_gfx6_gfx7_gfx10<0x0d2>;
defm V_CMPX_LE_U32    : VOPCX_Real_gfx6_gfx7_gfx10<0x0d3>;
defm V_CMPX_GT_U32    : VOPCX_Real_gfx6_gfx7_gfx10<0x0d4>;
defm V_CMPX_NE_U32    : VOPCX_Real_gfx6_gfx7_gfx10<0x0d5>;
defm V_CMPX_GE_U32    : VOPCX_Real_gfx6_gfx7_gfx10<0x0d6>;
defm V_CMPX_T_U32     : VOPCX_Real_gfx6_gfx7_gfx10<0x0d7>;
defm V_CMP_F_U64      : VOPC_Real_gfx6_gfx7_gfx10<0x0e0>;
defm V_CMP_LT_U64     : VOPC_Real_gfx6_gfx7_gfx10<0x0e1>;
defm V_CMP_EQ_U64     : VOPC_Real_gfx6_gfx7_gfx10<0x0e2>;
defm V_CMP_LE_U64     : VOPC_Real_gfx6_gfx7_gfx10<0x0e3>;
defm V_CMP_GT_U64     : VOPC_Real_gfx6_gfx7_gfx10<0x0e4>;
defm V_CMP_NE_U64     : VOPC_Real_gfx6_gfx7_gfx10<0x0e5>;
defm V_CMP_GE_U64     : VOPC_Real_gfx6_gfx7_gfx10<0x0e6>;
defm V_CMP_T_U64      : VOPC_Real_gfx6_gfx7_gfx10<0x0e7>;
defm V_CMPX_F_U64     : VOPCX_Real_gfx6_gfx7_gfx10<0x0f0>;
defm V_CMPX_LT_U64    : VOPCX_Real_gfx6_gfx7_gfx10<0x0f1>;
defm V_CMPX_EQ_U64    : VOPCX_Real_gfx6_gfx7_gfx10<0x0f2>;
defm V_CMPX_LE_U64    : VOPCX_Real_gfx6_gfx7_gfx10<0x0f3>;
defm V_CMPX_GT_U64    : VOPCX_Real_gfx6_gfx7_gfx10<0x0f4>;
defm V_CMPX_NE_U64    : VOPCX_Real_gfx6_gfx7_gfx10<0x0f5>;
defm V_CMPX_GE_U64    : VOPCX_Real_gfx6_gfx7_gfx10<0x0f6>;
defm V_CMPX_T_U64     : VOPCX_Real_gfx6_gfx7_gfx10<0x0f7>;

//===----------------------------------------------------------------------===//
// GFX8, GFX9 (VI).
//===----------------------------------------------------------------------===//

multiclass VOPC_Real_vi <bits<10> op> {
  let AssemblerPredicate = isGFX8GFX9, DecoderNamespace = "GFX8" in {
    def _e32_vi :
      VOPC_Real<!cast<VOPC_Pseudo>(NAME#"_e32"), SIEncodingFamily.VI>,
      VOPCe<op{7-0}>;

    def _e64_vi :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.VI>,
      VOP3a_vi <op, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl> {
      // Encoding used for VOPC instructions encoded as VOP3
      // Differs from VOP3e by destination name (sdst) as VOPC doesn't have vector dst
      bits<8> sdst;
      let Inst{7-0} = sdst;
    }
  }

  if !cast<VOPC_Pseudo>(NAME#"_e32").Pfl.HasExtSDWA then
  def _sdwa_vi :
    VOP_SDWA_Real <!cast<VOPC_SDWA_Pseudo>(NAME#"_sdwa")>,
    VOPC_SDWAe <op{7-0}, !cast<VOPC_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;

  if !cast<VOPC_Pseudo>(NAME#"_e32").Pfl.HasExtSDWA9 then
  def _sdwa_gfx9 :
    VOP_SDWA9_Real <!cast<VOPC_SDWA_Pseudo>(NAME#"_sdwa")>,
    VOPC_SDWA9e <op{7-0}, !cast<VOPC_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;

  let AssemblerPredicate = isGFX8GFX9 in {
    defm : VOPCInstAliases<NAME, "vi">;
  }
}

defm V_CMP_CLASS_F32  : VOPC_Real_vi <0x10>;
defm V_CMPX_CLASS_F32 : VOPC_Real_vi <0x11>;
defm V_CMP_CLASS_F64  : VOPC_Real_vi <0x12>;
defm V_CMPX_CLASS_F64 : VOPC_Real_vi <0x13>;
defm V_CMP_CLASS_F16  : VOPC_Real_vi <0x14>;
defm V_CMPX_CLASS_F16 : VOPC_Real_vi <0x15>;

defm V_CMP_F_F16      : VOPC_Real_vi <0x20>;
defm V_CMP_LT_F16     : VOPC_Real_vi <0x21>;
defm V_CMP_EQ_F16     : VOPC_Real_vi <0x22>;
defm V_CMP_LE_F16     : VOPC_Real_vi <0x23>;
defm V_CMP_GT_F16     : VOPC_Real_vi <0x24>;
defm V_CMP_LG_F16     : VOPC_Real_vi <0x25>;
defm V_CMP_GE_F16     : VOPC_Real_vi <0x26>;
defm V_CMP_O_F16      : VOPC_Real_vi <0x27>;
defm V_CMP_U_F16      : VOPC_Real_vi <0x28>;
defm V_CMP_NGE_F16    : VOPC_Real_vi <0x29>;
defm V_CMP_NLG_F16    : VOPC_Real_vi <0x2a>;
defm V_CMP_NGT_F16    : VOPC_Real_vi <0x2b>;
defm V_CMP_NLE_F16    : VOPC_Real_vi <0x2c>;
defm V_CMP_NEQ_F16    : VOPC_Real_vi <0x2d>;
defm V_CMP_NLT_F16    : VOPC_Real_vi <0x2e>;
defm V_CMP_TRU_F16    : VOPC_Real_vi <0x2f>;

defm V_CMPX_F_F16     : VOPC_Real_vi <0x30>;
defm V_CMPX_LT_F16    : VOPC_Real_vi <0x31>;
defm V_CMPX_EQ_F16    : VOPC_Real_vi <0x32>;
defm V_CMPX_LE_F16    : VOPC_Real_vi <0x33>;
defm V_CMPX_GT_F16    : VOPC_Real_vi <0x34>;
defm V_CMPX_LG_F16    : VOPC_Real_vi <0x35>;
defm V_CMPX_GE_F16    : VOPC_Real_vi <0x36>;
defm V_CMPX_O_F16     : VOPC_Real_vi <0x37>;
defm V_CMPX_U_F16     : VOPC_Real_vi <0x38>;
defm V_CMPX_NGE_F16   : VOPC_Real_vi <0x39>;
defm V_CMPX_NLG_F16   : VOPC_Real_vi <0x3a>;
defm V_CMPX_NGT_F16   : VOPC_Real_vi <0x3b>;
defm V_CMPX_NLE_F16   : VOPC_Real_vi <0x3c>;
defm V_CMPX_NEQ_F16   : VOPC_Real_vi <0x3d>;
defm V_CMPX_NLT_F16   : VOPC_Real_vi <0x3e>;
defm V_CMPX_TRU_F16   : VOPC_Real_vi <0x3f>;

defm V_CMP_F_F32      : VOPC_Real_vi <0x40>;
defm V_CMP_LT_F32     : VOPC_Real_vi <0x41>;
defm V_CMP_EQ_F32     : VOPC_Real_vi <0x42>;
defm V_CMP_LE_F32     : VOPC_Real_vi <0x43>;
defm V_CMP_GT_F32     : VOPC_Real_vi <0x44>;
defm V_CMP_LG_F32     : VOPC_Real_vi <0x45>;
defm V_CMP_GE_F32     : VOPC_Real_vi <0x46>;
defm V_CMP_O_F32      : VOPC_Real_vi <0x47>;
defm V_CMP_U_F32      : VOPC_Real_vi <0x48>;
defm V_CMP_NGE_F32    : VOPC_Real_vi <0x49>;
defm V_CMP_NLG_F32    : VOPC_Real_vi <0x4a>;
defm V_CMP_NGT_F32    : VOPC_Real_vi <0x4b>;
defm V_CMP_NLE_F32    : VOPC_Real_vi <0x4c>;
defm V_CMP_NEQ_F32    : VOPC_Real_vi <0x4d>;
defm V_CMP_NLT_F32    : VOPC_Real_vi <0x4e>;
defm V_CMP_TRU_F32    : VOPC_Real_vi <0x4f>;

defm V_CMPX_F_F32     : VOPC_Real_vi <0x50>;
defm V_CMPX_LT_F32    : VOPC_Real_vi <0x51>;
defm V_CMPX_EQ_F32    : VOPC_Real_vi <0x52>;
defm V_CMPX_LE_F32    : VOPC_Real_vi <0x53>;
defm V_CMPX_GT_F32    : VOPC_Real_vi <0x54>;
defm V_CMPX_LG_F32    : VOPC_Real_vi <0x55>;
defm V_CMPX_GE_F32    : VOPC_Real_vi <0x56>;
defm V_CMPX_O_F32     : VOPC_Real_vi <0x57>;
defm V_CMPX_U_F32     : VOPC_Real_vi <0x58>;
defm V_CMPX_NGE_F32   : VOPC_Real_vi <0x59>;
defm V_CMPX_NLG_F32   : VOPC_Real_vi <0x5a>;
defm V_CMPX_NGT_F32   : VOPC_Real_vi <0x5b>;
defm V_CMPX_NLE_F32   : VOPC_Real_vi <0x5c>;
defm V_CMPX_NEQ_F32   : VOPC_Real_vi <0x5d>;
defm V_CMPX_NLT_F32   : VOPC_Real_vi <0x5e>;
defm V_CMPX_TRU_F32   : VOPC_Real_vi <0x5f>;

defm V_CMP_F_F64      : VOPC_Real_vi <0x60>;
defm V_CMP_LT_F64     : VOPC_Real_vi <0x61>;
defm V_CMP_EQ_F64     : VOPC_Real_vi <0x62>;
defm V_CMP_LE_F64     : VOPC_Real_vi <0x63>;
defm V_CMP_GT_F64     : VOPC_Real_vi <0x64>;
defm V_CMP_LG_F64     : VOPC_Real_vi <0x65>;
defm V_CMP_GE_F64     : VOPC_Real_vi <0x66>;
defm V_CMP_O_F64      : VOPC_Real_vi <0x67>;
defm V_CMP_U_F64      : VOPC_Real_vi <0x68>;
defm V_CMP_NGE_F64    : VOPC_Real_vi <0x69>;
defm V_CMP_NLG_F64    : VOPC_Real_vi <0x6a>;
defm V_CMP_NGT_F64    : VOPC_Real_vi <0x6b>;
defm V_CMP_NLE_F64    : VOPC_Real_vi <0x6c>;
defm V_CMP_NEQ_F64    : VOPC_Real_vi <0x6d>;
defm V_CMP_NLT_F64    : VOPC_Real_vi <0x6e>;
defm V_CMP_TRU_F64    : VOPC_Real_vi <0x6f>;

defm V_CMPX_F_F64     : VOPC_Real_vi <0x70>;
defm V_CMPX_LT_F64    : VOPC_Real_vi <0x71>;
defm V_CMPX_EQ_F64    : VOPC_Real_vi <0x72>;
defm V_CMPX_LE_F64    : VOPC_Real_vi <0x73>;
defm V_CMPX_GT_F64    : VOPC_Real_vi <0x74>;
defm V_CMPX_LG_F64    : VOPC_Real_vi <0x75>;
defm V_CMPX_GE_F64    : VOPC_Real_vi <0x76>;
defm V_CMPX_O_F64     : VOPC_Real_vi <0x77>;
defm V_CMPX_U_F64     : VOPC_Real_vi <0x78>;
defm V_CMPX_NGE_F64   : VOPC_Real_vi <0x79>;
defm V_CMPX_NLG_F64   : VOPC_Real_vi <0x7a>;
defm V_CMPX_NGT_F64   : VOPC_Real_vi <0x7b>;
defm V_CMPX_NLE_F64   : VOPC_Real_vi <0x7c>;
defm V_CMPX_NEQ_F64   : VOPC_Real_vi <0x7d>;
defm V_CMPX_NLT_F64   : VOPC_Real_vi <0x7e>;
defm V_CMPX_TRU_F64   : VOPC_Real_vi <0x7f>;

defm V_CMP_F_I16      : VOPC_Real_vi <0xa0>;
defm V_CMP_LT_I16     : VOPC_Real_vi <0xa1>;
defm V_CMP_EQ_I16     : VOPC_Real_vi <0xa2>;
defm V_CMP_LE_I16     : VOPC_Real_vi <0xa3>;
defm V_CMP_GT_I16     : VOPC_Real_vi <0xa4>;
defm V_CMP_NE_I16     : VOPC_Real_vi <0xa5>;
defm V_CMP_GE_I16     : VOPC_Real_vi <0xa6>;
defm V_CMP_T_I16      : VOPC_Real_vi <0xa7>;

defm V_CMP_F_U16      : VOPC_Real_vi <0xa8>;
defm V_CMP_LT_U16     : VOPC_Real_vi <0xa9>;
defm V_CMP_EQ_U16     : VOPC_Real_vi <0xaa>;
defm V_CMP_LE_U16     : VOPC_Real_vi <0xab>;
defm V_CMP_GT_U16     : VOPC_Real_vi <0xac>;
defm V_CMP_NE_U16     : VOPC_Real_vi <0xad>;
defm V_CMP_GE_U16     : VOPC_Real_vi <0xae>;
defm V_CMP_T_U16      : VOPC_Real_vi <0xaf>;

defm V_CMPX_F_I16 : VOPC_Real_vi <0xb0>;
defm V_CMPX_LT_I16 : VOPC_Real_vi <0xb1>;
defm V_CMPX_EQ_I16 : VOPC_Real_vi <0xb2>;
defm V_CMPX_LE_I16 : VOPC_Real_vi <0xb3>;
defm V_CMPX_GT_I16 : VOPC_Real_vi <0xb4>;
defm V_CMPX_NE_I16 : VOPC_Real_vi <0xb5>;
defm V_CMPX_GE_I16 : VOPC_Real_vi <0xb6>;
defm V_CMPX_T_I16 : VOPC_Real_vi <0xb7>;

defm V_CMPX_F_U16 : VOPC_Real_vi <0xb8>;
defm V_CMPX_LT_U16 : VOPC_Real_vi <0xb9>;
defm V_CMPX_EQ_U16 : VOPC_Real_vi <0xba>;
defm V_CMPX_LE_U16 : VOPC_Real_vi <0xbb>;
defm V_CMPX_GT_U16 : VOPC_Real_vi <0xbc>;
defm V_CMPX_NE_U16 : VOPC_Real_vi <0xbd>;
defm V_CMPX_GE_U16 : VOPC_Real_vi <0xbe>;
defm V_CMPX_T_U16 : VOPC_Real_vi <0xbf>;

defm V_CMP_F_I32      : VOPC_Real_vi <0xc0>;
defm V_CMP_LT_I32     : VOPC_Real_vi <0xc1>;
defm V_CMP_EQ_I32     : VOPC_Real_vi <0xc2>;
defm V_CMP_LE_I32     : VOPC_Real_vi <0xc3>;
defm V_CMP_GT_I32     : VOPC_Real_vi <0xc4>;
defm V_CMP_NE_I32     : VOPC_Real_vi <0xc5>;
defm V_CMP_GE_I32     : VOPC_Real_vi <0xc6>;
defm V_CMP_T_I32      : VOPC_Real_vi <0xc7>;

defm V_CMPX_F_I32     : VOPC_Real_vi <0xd0>;
defm V_CMPX_LT_I32    : VOPC_Real_vi <0xd1>;
defm V_CMPX_EQ_I32    : VOPC_Real_vi <0xd2>;
defm V_CMPX_LE_I32    : VOPC_Real_vi <0xd3>;
defm V_CMPX_GT_I32    : VOPC_Real_vi <0xd4>;
defm V_CMPX_NE_I32    : VOPC_Real_vi <0xd5>;
defm V_CMPX_GE_I32    : VOPC_Real_vi <0xd6>;
defm V_CMPX_T_I32     : VOPC_Real_vi <0xd7>;

defm V_CMP_F_I64      : VOPC_Real_vi <0xe0>;
defm V_CMP_LT_I64     : VOPC_Real_vi <0xe1>;
defm V_CMP_EQ_I64     : VOPC_Real_vi <0xe2>;
defm V_CMP_LE_I64     : VOPC_Real_vi <0xe3>;
defm V_CMP_GT_I64     : VOPC_Real_vi <0xe4>;
defm V_CMP_NE_I64     : VOPC_Real_vi <0xe5>;
defm V_CMP_GE_I64     : VOPC_Real_vi <0xe6>;
defm V_CMP_T_I64      : VOPC_Real_vi <0xe7>;

defm V_CMPX_F_I64     : VOPC_Real_vi <0xf0>;
defm V_CMPX_LT_I64    : VOPC_Real_vi <0xf1>;
defm V_CMPX_EQ_I64    : VOPC_Real_vi <0xf2>;
defm V_CMPX_LE_I64    : VOPC_Real_vi <0xf3>;
defm V_CMPX_GT_I64    : VOPC_Real_vi <0xf4>;
defm V_CMPX_NE_I64    : VOPC_Real_vi <0xf5>;
defm V_CMPX_GE_I64    : VOPC_Real_vi <0xf6>;
defm V_CMPX_T_I64     : VOPC_Real_vi <0xf7>;

defm V_CMP_F_U32      : VOPC_Real_vi <0xc8>;
defm V_CMP_LT_U32     : VOPC_Real_vi <0xc9>;
defm V_CMP_EQ_U32     : VOPC_Real_vi <0xca>;
defm V_CMP_LE_U32     : VOPC_Real_vi <0xcb>;
defm V_CMP_GT_U32     : VOPC_Real_vi <0xcc>;
defm V_CMP_NE_U32     : VOPC_Real_vi <0xcd>;
defm V_CMP_GE_U32     : VOPC_Real_vi <0xce>;
defm V_CMP_T_U32      : VOPC_Real_vi <0xcf>;

defm V_CMPX_F_U32     : VOPC_Real_vi <0xd8>;
defm V_CMPX_LT_U32    : VOPC_Real_vi <0xd9>;
defm V_CMPX_EQ_U32    : VOPC_Real_vi <0xda>;
defm V_CMPX_LE_U32    : VOPC_Real_vi <0xdb>;
defm V_CMPX_GT_U32    : VOPC_Real_vi <0xdc>;
defm V_CMPX_NE_U32    : VOPC_Real_vi <0xdd>;
defm V_CMPX_GE_U32    : VOPC_Real_vi <0xde>;
defm V_CMPX_T_U32     : VOPC_Real_vi <0xdf>;

defm V_CMP_F_U64      : VOPC_Real_vi <0xe8>;
defm V_CMP_LT_U64     : VOPC_Real_vi <0xe9>;
defm V_CMP_EQ_U64     : VOPC_Real_vi <0xea>;
defm V_CMP_LE_U64     : VOPC_Real_vi <0xeb>;
defm V_CMP_GT_U64     : VOPC_Real_vi <0xec>;
defm V_CMP_NE_U64     : VOPC_Real_vi <0xed>;
defm V_CMP_GE_U64     : VOPC_Real_vi <0xee>;
defm V_CMP_T_U64      : VOPC_Real_vi <0xef>;

defm V_CMPX_F_U64     : VOPC_Real_vi <0xf8>;
defm V_CMPX_LT_U64    : VOPC_Real_vi <0xf9>;
defm V_CMPX_EQ_U64    : VOPC_Real_vi <0xfa>;
defm V_CMPX_LE_U64    : VOPC_Real_vi <0xfb>;
defm V_CMPX_GT_U64    : VOPC_Real_vi <0xfc>;
defm V_CMPX_NE_U64    : VOPC_Real_vi <0xfd>;
defm V_CMPX_GE_U64    : VOPC_Real_vi <0xfe>;
defm V_CMPX_T_U64     : VOPC_Real_vi <0xff>;
