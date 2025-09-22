//===-- MipsCondMov.td - Describe Mips Conditional Moves --*- tablegen -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the Conditional Moves implementation.
//
//===----------------------------------------------------------------------===//

// Conditional moves:
// These instructions are expanded in
// MipsISelLowering::EmitInstrWithCustomInserter if target does not have
// conditional move instructions.
// cond:int, data:int
class CMov_I_I_FT<string opstr, RegisterOperand CRC, RegisterOperand DRC,
                  InstrItinClass Itin> :
  InstSE<(outs DRC:$rd), (ins DRC:$rs, CRC:$rt, DRC:$F),
         !strconcat(opstr, "\t$rd, $rs, $rt"), [], Itin, FrmFR, opstr> {
  let Constraints = "$F = $rd";
}

// cond:int, data:float
class CMov_I_F_FT<string opstr, RegisterOperand CRC, RegisterOperand DRC,
                  InstrItinClass Itin> :
  InstSE<(outs DRC:$fd), (ins DRC:$fs, CRC:$rt, DRC:$F),
         !strconcat(opstr, "\t$fd, $fs, $rt"), [], Itin, FrmFR, opstr>,
  HARDFLOAT {
  let Constraints = "$F = $fd";
}

// cond:float, data:int
class CMov_F_I_FT<string opstr, RegisterOperand RC, InstrItinClass Itin,
                  SDPatternOperator OpNode = null_frag> :
  InstSE<(outs RC:$rd), (ins RC:$rs, FCCRegsOpnd:$fcc, RC:$F),
         !strconcat(opstr, "\t$rd, $rs, $fcc"),
         [(set RC:$rd, (OpNode RC:$rs, FCCRegsOpnd:$fcc, RC:$F))],
         Itin, FrmFR, opstr>, HARDFLOAT {
  let Constraints = "$F = $rd";
}

// cond:float, data:float
class CMov_F_F_FT<string opstr, RegisterOperand RC, InstrItinClass Itin,
                  SDPatternOperator OpNode = null_frag> :
  InstSE<(outs RC:$fd), (ins RC:$fs, FCCRegsOpnd:$fcc, RC:$F),
         !strconcat(opstr, "\t$fd, $fs, $fcc"),
         [(set RC:$fd, (OpNode RC:$fs, FCCRegsOpnd:$fcc, RC:$F))],
         Itin, FrmFR, opstr>, HARDFLOAT {
  let Constraints = "$F = $fd";
}

// select patterns
multiclass MovzPats0<RegisterClass CRC, RegisterClass DRC,
                     Instruction MOVZInst, Instruction SLTOp,
                     Instruction SLTuOp, Instruction SLTiOp,
                     Instruction SLTiuOp> {
  def : MipsPat<(select (i32 (setge CRC:$lhs, CRC:$rhs)), DRC:$T, DRC:$F),
                (MOVZInst DRC:$T, (SLTOp CRC:$lhs, CRC:$rhs), DRC:$F)>;
  def : MipsPat<(select (i32 (setuge CRC:$lhs, CRC:$rhs)), DRC:$T, DRC:$F),
                (MOVZInst DRC:$T, (SLTuOp CRC:$lhs, CRC:$rhs), DRC:$F)>;
  def : MipsPat<(select (i32 (setge CRC:$lhs, immSExt16:$rhs)), DRC:$T, DRC:$F),
                (MOVZInst DRC:$T, (SLTiOp CRC:$lhs, immSExt16:$rhs), DRC:$F)>;
  def : MipsPat<(select (i32 (setuge CRC:$lh, immSExt16:$rh)), DRC:$T, DRC:$F),
                (MOVZInst DRC:$T, (SLTiuOp CRC:$lh, immSExt16:$rh), DRC:$F)>;
  def : MipsPat<(select (i32 (setle CRC:$lhs, CRC:$rhs)), DRC:$T, DRC:$F),
                (MOVZInst DRC:$T, (SLTOp CRC:$rhs, CRC:$lhs), DRC:$F)>;
  def : MipsPat<(select (i32 (setule CRC:$lhs, CRC:$rhs)), DRC:$T, DRC:$F),
                (MOVZInst DRC:$T, (SLTuOp CRC:$rhs, CRC:$lhs), DRC:$F)>;
  def : MipsPat<(select (i32 (setgt CRC:$lhs, immSExt16Plus1:$rhs)),
                        DRC:$T, DRC:$F),
                (MOVZInst DRC:$T, (SLTiOp CRC:$lhs, (Plus1 imm:$rhs)), DRC:$F)>;
  def : MipsPat<(select (i32 (setugt CRC:$lhs, immSExt16Plus1:$rhs)),
                        DRC:$T, DRC:$F),
                (MOVZInst DRC:$T, (SLTiuOp CRC:$lhs, (Plus1 imm:$rhs)),
                          DRC:$F)>;
}

multiclass MovzPats1<RegisterClass CRC, RegisterClass DRC,
                     Instruction MOVZInst, Instruction XOROp> {
  def : MipsPat<(select (i32 (seteq CRC:$lhs, CRC:$rhs)), DRC:$T, DRC:$F),
                (MOVZInst DRC:$T, (XOROp CRC:$lhs, CRC:$rhs), DRC:$F)>;
  def : MipsPat<(select (i32 (seteq CRC:$lhs, 0)), DRC:$T, DRC:$F),
                (MOVZInst DRC:$T, CRC:$lhs, DRC:$F)>;
}

multiclass MovzPats2<RegisterClass CRC, RegisterClass DRC,
                     Instruction MOVZInst, Instruction XORiOp> {
  def : MipsPat<
            (select (i32 (seteq CRC:$lhs, immZExt16:$uimm16)), DRC:$T, DRC:$F),
            (MOVZInst DRC:$T, (XORiOp CRC:$lhs, immZExt16:$uimm16), DRC:$F)>;
}

multiclass MovnPats<RegisterClass CRC, RegisterClass DRC, Instruction MOVNInst,
                    Instruction XOROp> {
  def : MipsPat<(select (i32 (setne CRC:$lhs, CRC:$rhs)), DRC:$T, DRC:$F),
                (MOVNInst DRC:$T, (XOROp CRC:$lhs, CRC:$rhs), DRC:$F)>;
  def : MipsPat<(select CRC:$cond, DRC:$T, DRC:$F),
                (MOVNInst DRC:$T, CRC:$cond, DRC:$F)>;
  def : MipsPat<(select (i32 (setne CRC:$lhs, 0)),DRC:$T, DRC:$F),
                (MOVNInst DRC:$T, CRC:$lhs, DRC:$F)>;
}

// Instantiation of instructions.
let AdditionalPredicates = [NotInMicroMips] in {
  def MOVZ_I_I : MMRel, CMov_I_I_FT<"movz", GPR32Opnd, GPR32Opnd, II_MOVZ>,
                 ADD_FM<0, 0xa>, INSN_MIPS4_32_NOT_32R6_64R6;

  let isCodeGenOnly = 1 in {
    def MOVZ_I_I64   : CMov_I_I_FT<"movz", GPR32Opnd, GPR64Opnd, II_MOVZ>,
                       ADD_FM<0, 0xa>, INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
    def MOVZ_I64_I   : CMov_I_I_FT<"movz", GPR64Opnd, GPR32Opnd, II_MOVZ>,
                       ADD_FM<0, 0xa>, INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
    def MOVZ_I64_I64 : CMov_I_I_FT<"movz", GPR64Opnd, GPR64Opnd, II_MOVZ>,
                       ADD_FM<0, 0xa>, INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  }

  def MOVN_I_I     : MMRel, CMov_I_I_FT<"movn", GPR32Opnd, GPR32Opnd, II_MOVN>,
                     ADD_FM<0, 0xb>, INSN_MIPS4_32_NOT_32R6_64R6;

  let isCodeGenOnly = 1 in {
    def MOVN_I_I64   : CMov_I_I_FT<"movn", GPR32Opnd, GPR64Opnd, II_MOVN>,
                       ADD_FM<0, 0xb>, INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
    def MOVN_I64_I   : CMov_I_I_FT<"movn", GPR64Opnd, GPR32Opnd, II_MOVN>,
                       ADD_FM<0, 0xb>, INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
    def MOVN_I64_I64 : CMov_I_I_FT<"movn", GPR64Opnd, GPR64Opnd, II_MOVN>,
                       ADD_FM<0, 0xb>, INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  }
  def MOVZ_I_S : MMRel, CMov_I_F_FT<"movz.s", GPR32Opnd, FGR32Opnd, II_MOVZ_S>,
                 CMov_I_F_FM<18, 16>, INSN_MIPS4_32_NOT_32R6_64R6;

  let isCodeGenOnly = 1 in
  def MOVZ_I64_S : CMov_I_F_FT<"movz.s", GPR64Opnd, FGR32Opnd, II_MOVZ_S>,
                   CMov_I_F_FM<18, 16>, INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;

  def MOVN_I_S : MMRel, CMov_I_F_FT<"movn.s", GPR32Opnd, FGR32Opnd, II_MOVN_S>,
                 CMov_I_F_FM<19, 16>, INSN_MIPS4_32_NOT_32R6_64R6;

  let isCodeGenOnly = 1 in
  def MOVN_I64_S : CMov_I_F_FT<"movn.s", GPR64Opnd, FGR32Opnd, II_MOVN_S>,
                   CMov_I_F_FM<19, 16>, INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;

  def MOVZ_I_D32 : MMRel, CMov_I_F_FT<"movz.d", GPR32Opnd, AFGR64Opnd,
                                      II_MOVZ_D>, CMov_I_F_FM<18, 17>,
                   INSN_MIPS4_32_NOT_32R6_64R6, FGR_32;
  def MOVN_I_D32 : MMRel, CMov_I_F_FT<"movn.d", GPR32Opnd, AFGR64Opnd,
                                      II_MOVN_D>, CMov_I_F_FM<19, 17>,
                   INSN_MIPS4_32_NOT_32R6_64R6, FGR_32;

  let DecoderNamespace = "MipsFP64" in {
    def MOVZ_I_D64 : CMov_I_F_FT<"movz.d", GPR32Opnd, FGR64Opnd, II_MOVZ_D>,
                     CMov_I_F_FM<18, 17>, INSN_MIPS4_32_NOT_32R6_64R6, FGR_64;
    def MOVN_I_D64 : CMov_I_F_FT<"movn.d", GPR32Opnd, FGR64Opnd, II_MOVN_D>,
                     CMov_I_F_FM<19, 17>, INSN_MIPS4_32_NOT_32R6_64R6, FGR_64;
    let isCodeGenOnly = 1 in {
      def MOVZ_I64_D64 : CMov_I_F_FT<"movz.d", GPR64Opnd, FGR64Opnd, II_MOVZ_D>,
                         CMov_I_F_FM<18, 17>,
                         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64, FGR_64;
      def MOVN_I64_D64 : CMov_I_F_FT<"movn.d", GPR64Opnd, FGR64Opnd, II_MOVN_D>,
                         CMov_I_F_FM<19, 17>,
                         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64, FGR_64;
    }
  }

  def MOVT_I : MMRel, CMov_F_I_FT<"movt", GPR32Opnd, II_MOVT, MipsCMovFP_T>,
               CMov_F_I_FM<1>, INSN_MIPS4_32_NOT_32R6_64R6;

  let isCodeGenOnly = 1 in
  def MOVT_I64 : CMov_F_I_FT<"movt", GPR64Opnd, II_MOVT, MipsCMovFP_T>,
                 CMov_F_I_FM<1>, INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;

  def MOVF_I : MMRel, CMov_F_I_FT<"movf", GPR32Opnd, II_MOVF, MipsCMovFP_F>,
               CMov_F_I_FM<0>, INSN_MIPS4_32_NOT_32R6_64R6;

  let isCodeGenOnly = 1 in
  def MOVF_I64 : CMov_F_I_FT<"movf", GPR64Opnd, II_MOVF, MipsCMovFP_F>,
                 CMov_F_I_FM<0>, INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  def MOVT_S : MMRel, CMov_F_F_FT<"movt.s", FGR32Opnd, II_MOVT_S, MipsCMovFP_T>,
               CMov_F_F_FM<16, 1>, INSN_MIPS4_32_NOT_32R6_64R6;
  def MOVF_S : MMRel, CMov_F_F_FT<"movf.s", FGR32Opnd, II_MOVF_S, MipsCMovFP_F>,
               CMov_F_F_FM<16, 0>, INSN_MIPS4_32_NOT_32R6_64R6;

  def MOVT_D32 : MMRel, CMov_F_F_FT<"movt.d", AFGR64Opnd, II_MOVT_D,
                                    MipsCMovFP_T>, CMov_F_F_FM<17, 1>,
                 INSN_MIPS4_32_NOT_32R6_64R6, FGR_32;
  def MOVF_D32 : MMRel, CMov_F_F_FT<"movf.d", AFGR64Opnd, II_MOVF_D,
                                    MipsCMovFP_F>, CMov_F_F_FM<17, 0>,
                 INSN_MIPS4_32_NOT_32R6_64R6, FGR_32;

  let DecoderNamespace = "MipsFP64" in {
    def MOVT_D64 : CMov_F_F_FT<"movt.d", FGR64Opnd, II_MOVT_D, MipsCMovFP_T>,
                   CMov_F_F_FM<17, 1>, INSN_MIPS4_32_NOT_32R6_64R6, FGR_64;
    def MOVF_D64 : CMov_F_F_FT<"movf.d", FGR64Opnd, II_MOVF_D, MipsCMovFP_F>,
                   CMov_F_F_FM<17, 0>, INSN_MIPS4_32_NOT_32R6_64R6, FGR_64;
  }

  // Instantiation of conditional move patterns.
  defm : MovzPats0<GPR32, GPR32, MOVZ_I_I, SLT, SLTu, SLTi, SLTiu>,
         INSN_MIPS4_32_NOT_32R6_64R6;
  defm : MovzPats1<GPR32, GPR32, MOVZ_I_I, XOR>, INSN_MIPS4_32_NOT_32R6_64R6;
  defm : MovzPats2<GPR32, GPR32, MOVZ_I_I, XORi>, INSN_MIPS4_32_NOT_32R6_64R6;

  defm : MovzPats0<GPR32, GPR64, MOVZ_I_I64, SLT, SLTu, SLTi, SLTiu>,
         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  defm : MovzPats0<GPR64, GPR32, MOVZ_I_I, SLT64, SLTu64, SLTi64, SLTiu64>,
         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  defm : MovzPats0<GPR64, GPR64, MOVZ_I_I64, SLT64, SLTu64, SLTi64, SLTiu64>,
         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  defm : MovzPats1<GPR32, GPR64, MOVZ_I_I64, XOR>,
         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  defm : MovzPats1<GPR64, GPR32, MOVZ_I64_I, XOR64>,
         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  defm : MovzPats1<GPR64, GPR64, MOVZ_I64_I64, XOR64>,
         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  defm : MovzPats2<GPR32, GPR64, MOVZ_I_I64, XORi>,
         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  defm : MovzPats2<GPR64, GPR32, MOVZ_I64_I, XORi64>,
         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  defm : MovzPats2<GPR64, GPR64, MOVZ_I64_I64, XORi64>,
         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;

  defm : MovnPats<GPR32, GPR32, MOVN_I_I, XOR>, INSN_MIPS4_32_NOT_32R6_64R6;

  defm : MovnPats<GPR32, GPR64, MOVN_I_I64, XOR>, INSN_MIPS4_32_NOT_32R6_64R6,
         GPR_64;
  defm : MovnPats<GPR64, GPR32, MOVN_I64_I, XOR64>, INSN_MIPS4_32_NOT_32R6_64R6,
         GPR_64;
  defm : MovnPats<GPR64, GPR64, MOVN_I64_I64, XOR64>,
         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;

  defm : MovzPats0<GPR32, FGR32, MOVZ_I_S, SLT, SLTu, SLTi, SLTiu>,
         INSN_MIPS4_32_NOT_32R6_64R6;
  defm : MovzPats1<GPR32, FGR32, MOVZ_I_S, XOR>, INSN_MIPS4_32_NOT_32R6_64R6;
  defm : MovnPats<GPR32, FGR32, MOVN_I_S, XOR>, INSN_MIPS4_32_NOT_32R6_64R6;

  defm : MovzPats0<GPR64, FGR32, MOVZ_I_S, SLT64, SLTu64, SLTi64, SLTiu64>,
         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  defm : MovzPats1<GPR64, FGR32, MOVZ_I64_S, XOR64>,
         INSN_MIPS4_32_NOT_32R6_64R6, GPR_64;
  defm : MovnPats<GPR64, FGR32, MOVN_I64_S, XOR64>, INSN_MIPS4_32_NOT_32R6_64R6,
         GPR_64;

  defm : MovzPats0<GPR32, AFGR64, MOVZ_I_D32, SLT, SLTu, SLTi, SLTiu>,
         INSN_MIPS4_32_NOT_32R6_64R6, FGR_32;
  defm : MovzPats1<GPR32, AFGR64, MOVZ_I_D32, XOR>, INSN_MIPS4_32_NOT_32R6_64R6,
         FGR_32;
  defm : MovnPats<GPR32, AFGR64, MOVN_I_D32, XOR>, INSN_MIPS4_32_NOT_32R6_64R6,
         FGR_32;

  defm : MovzPats0<GPR32, FGR64, MOVZ_I_D64, SLT, SLTu, SLTi, SLTiu>,
         INSN_MIPS4_32_NOT_32R6_64R6, FGR_64;
  defm : MovzPats0<GPR64, FGR64, MOVZ_I_D64, SLT64, SLTu64, SLTi64, SLTiu64>,
         INSN_MIPS4_32_NOT_32R6_64R6, FGR_64;
  defm : MovzPats1<GPR32, FGR64, MOVZ_I_D64, XOR>, INSN_MIPS4_32_NOT_32R6_64R6,
         FGR_64;
  defm : MovzPats1<GPR64, FGR64, MOVZ_I64_D64, XOR64>,
         INSN_MIPS4_32_NOT_32R6_64R6, FGR_64;
  defm : MovnPats<GPR32, FGR64, MOVN_I_D64, XOR>, INSN_MIPS4_32_NOT_32R6_64R6,
         FGR_64;
  defm : MovnPats<GPR64, FGR64, MOVN_I64_D64, XOR64>,
         INSN_MIPS4_32_NOT_32R6_64R6, FGR_64;
}
// For targets that don't have conditional-move instructions
// we have to match SELECT nodes with pseudo instructions.
let usesCustomInserter = 1, hasNoSchedulingInfo = 1 in {
  class Select_Pseudo<RegisterOperand RC> :
    PseudoSE<(outs RC:$dst), (ins GPR32Opnd:$cond, RC:$T, RC:$F),
            [(set RC:$dst, (select GPR32Opnd:$cond, RC:$T, RC:$F))]>,
    ISA_MIPS1_NOT_4_32;

  class SelectFP_Pseudo_T<RegisterOperand RC> :
    PseudoSE<(outs RC:$dst), (ins FCCRegsOpnd:$cond, RC:$T, RC:$F),
             [(set RC:$dst, (MipsCMovFP_T RC:$T, FCCRegsOpnd:$cond, RC:$F))]>,
    ISA_MIPS1_NOT_4_32;

  class SelectFP_Pseudo_F<RegisterOperand RC> :
    PseudoSE<(outs RC:$dst), (ins FCCRegsOpnd:$cond, RC:$T, RC:$F),
             [(set RC:$dst, (MipsCMovFP_F RC:$T, FCCRegsOpnd:$cond, RC:$F))]>,
    ISA_MIPS1_NOT_4_32;
}

def PseudoSELECT_I : Select_Pseudo<GPR32Opnd>;
def PseudoSELECT_I64 : Select_Pseudo<GPR64Opnd>;
def PseudoSELECT_S : Select_Pseudo<FGR32Opnd>;
def PseudoSELECT_D32 : Select_Pseudo<AFGR64Opnd>, FGR_32;
def PseudoSELECT_D64 : Select_Pseudo<FGR64Opnd>, FGR_64;

def PseudoSELECTFP_T_I : SelectFP_Pseudo_T<GPR32Opnd>;
def PseudoSELECTFP_T_I64 : SelectFP_Pseudo_T<GPR64Opnd>;
def PseudoSELECTFP_T_S : SelectFP_Pseudo_T<FGR32Opnd>;
def PseudoSELECTFP_T_D32 : SelectFP_Pseudo_T<AFGR64Opnd>, FGR_32;
def PseudoSELECTFP_T_D64 : SelectFP_Pseudo_T<FGR64Opnd>, FGR_64;

def PseudoSELECTFP_F_I : SelectFP_Pseudo_F<GPR32Opnd>;
def PseudoSELECTFP_F_I64 : SelectFP_Pseudo_F<GPR64Opnd>;
def PseudoSELECTFP_F_S : SelectFP_Pseudo_F<FGR32Opnd>;
def PseudoSELECTFP_F_D32 : SelectFP_Pseudo_F<AFGR64Opnd>, FGR_32;
def PseudoSELECTFP_F_D64 : SelectFP_Pseudo_F<FGR64Opnd>, FGR_64;

let usesCustomInserter = 1, hasNoSchedulingInfo = 1 in {
class D_SELECT_CLASS<RegisterOperand RC> :
  PseudoSE<(outs RC:$dst1, RC:$dst2),
           (ins GPR32Opnd:$cond, RC:$a1, RC:$a2, RC:$b1, RC:$b2), []>,
  ISA_MIPS1_NOT_4_32;
}

def PseudoD_SELECT_I   : D_SELECT_CLASS<GPR32Opnd>;
def PseudoD_SELECT_I64 : D_SELECT_CLASS<GPR64Opnd>;
