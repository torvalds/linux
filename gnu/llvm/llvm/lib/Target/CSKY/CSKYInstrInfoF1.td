//===- CSKYInstrInfoF1.td - CSKY Instruction Float1.0 ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the CSKY instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

def regseq_f1 : Operand<iPTR> {
  let EncoderMethod = "getRegisterSeqOpValue";
  let ParserMatchClass = RegSeqAsmOperand<"V1">;
  let PrintMethod = "printRegisterSeq";
  let DecoderMethod = "DecodeRegSeqOperandF1";
  let MIOperandInfo = (ops sFPR32, uimm5);
}

def regseq_d1 : Operand<iPTR> {
  let EncoderMethod = "getRegisterSeqOpValue";
  let ParserMatchClass = RegSeqAsmOperand<"V1">;
  let PrintMethod = "printRegisterSeq";
  let DecoderMethod = "DecodeRegSeqOperandD1";
  let MIOperandInfo = (ops sFPR64, uimm5);
}

def sFPR32Op : RegisterOperand<sFPR32, "printFPR">;
def sFPR64Op : RegisterOperand<sFPR64, "printFPR">;
def sFPR64_V_OP : RegisterOperand<sFPR64_V, "printFPR">;

include "CSKYInstrFormatsF1.td"

//===----------------------------------------------------------------------===//
// CSKY specific DAG Nodes.
//===----------------------------------------------------------------------===//

def SDT_BITCAST_TO_LOHI : SDTypeProfile<2, 1, [SDTCisSameAs<0, 1>]>;
def CSKY_BITCAST_TO_LOHI : SDNode<"CSKYISD::BITCAST_TO_LOHI", SDT_BITCAST_TO_LOHI>;
def SDT_BITCAST_FROM_LOHI : SDTypeProfile<1, 2, [SDTCisSameAs<1, 2>]>;
def CSKY_BITCAST_FROM_LOHI : SDNode<"CSKYISD::BITCAST_FROM_LOHI", SDT_BITCAST_FROM_LOHI>;
//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

def fpimm0 : PatLeaf<(fpimm), [{ return N->isExactlyValue(+0.0); }]>;

def fpimm32_hi16 : SDNodeXForm<fpimm, [{
  return CurDAG->getTargetConstant(
    (N->getValueAPF().bitcastToAPInt().getZExtValue() >> 16) & 0xFFFF,
    SDLoc(N), MVT::i32);
}]>;

def fpimm32_lo16 : SDNodeXForm<fpimm, [{
  return CurDAG->getTargetConstant(
    N->getValueAPF().bitcastToAPInt().getZExtValue() & 0xFFFF,
    SDLoc(N), MVT::i32);
}]>;

class fpimm_xform<int width, int shift = 0> : SDNodeXForm<fpimm,
  "return CurDAG->getTargetConstant(N->getValueAPF().bitcastToAPInt().lshr("#shift#").getLoBits("#width#"), SDLoc(N), MVT::i32);">;

class fpimm_xform_i16<int width, int shift = 0> : SDNodeXForm<fpimm,
  "return CurDAG->getTargetConstant(N->getValueAPF().bitcastToAPInt().lshr("#shift#").getLoBits("#width#"), SDLoc(N), MVT::i16);">;

class fpimm_t<int width, int shift = 0> : PatLeaf<(fpimm),
   "return isShiftedUInt<"#width#", "#shift#">(N->getValueAPF().bitcastToAPInt().getZExtValue());">;

def fpimm8 : fpimm_t<8>;
def fpimm8_8 : fpimm_t<8, 8>;
def fpimm8_16 : fpimm_t<8, 16>;
def fpimm8_24 : fpimm_t<8, 24>;
def fpimm16 : fpimm_t<16>;
def fpimm16_8 : fpimm_t<16, 8>;
def fpimm16_16 : fpimm_t<16, 16>;
def fpimm24 : fpimm_t<24>;
def fpimm24_8 : fpimm_t<24, 8>;
def fpimm32 : fpimm_t<32>;

def fpimm8_sr0_XFORM : fpimm_xform<8>;
def fpimm8_sr8_XFORM : fpimm_xform<8, 8>;
def fpimm8_sr16_XFORM : fpimm_xform<8, 16>;
def fpimm8_sr24_XFORM : fpimm_xform<8, 24>;

def fpimm8_sr0_i16_XFORM : fpimm_xform_i16<8>;
def fpimm8_sr8_i16_XFORM : fpimm_xform_i16<8, 8>;

def fconstpool_symbol : Operand<iPTR> {
  let ParserMatchClass = Constpool;
  let EncoderMethod =
    "getConstpoolSymbolOpValue<CSKY::fixup_csky_pcrel_uimm8_scale4>";
  let DecoderMethod = "decodeUImmOperand<8, 2>";
  let PrintMethod = "printConstpool";
  let OperandType = "OPERAND_PCREL";
}



//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

//arithmetic

def FABSM : F_XZ<0x2, 0b000110, "fabsm", "", UnOpFrag<(fabs node:$Src)>, sFPR64_V_OP>;
def FNEGM : F_XZ<0x2, 0b000111, "fnegm", "", UnOpFrag<(fneg node:$Src)>, sFPR64_V_OP>;
def FADDM : F_XYZ<0x2, 0b000000, "faddm", "", BinOpFrag<(fadd node:$LHS, node:$RHS)>, sFPR64_V_OP>;
def FSUBM : F_XYZ<0x2, 0b000001, "fsubm", "", BinOpFrag<(fsub node:$LHS, node:$RHS)>, sFPR64_V_OP>;
def FMULM : F_XYZ<0x2, 0b010000, "fmulm", "", BinOpFrag<(fmul node:$LHS, node:$RHS)>, sFPR64_V_OP>;
def FNMULM : F_XYZ<0x2, 0b010001, "fnmulm", "", BinOpFrag<(fneg (fmul node:$LHS, node:$RHS))>, sFPR64_V_OP>;
def FMACM : F_ACCUM_XYZ<0x2, 0b010100, "fmacm", "", TriOpFrag<(fadd node:$LHS, (fmul node:$MHS, node:$RHS))>, sFPR64_V_OP>;
def FMSCM : F_ACCUM_XYZ<0x2, 0b010101, "fmscm", "", TriOpFrag<(fsub (fmul node:$MHS, node:$RHS), node:$LHS)>, sFPR64_V_OP>;
def FNMACM : F_ACCUM_XYZ<0x2, 0b010110, "fnmacm", "", TriOpFrag<(fsub node:$LHS, (fmul node:$MHS, node:$RHS))>, sFPR64_V_OP>;
def FNMSCM : F_ACCUM_XYZ<0x2, 0b010111, "fnmscm", "", TriOpFrag<(fneg (fadd node:$LHS, (fmul node:$MHS, node:$RHS)))>, sFPR64_V_OP>;

def FMOVM :  F_MOV<0x2, 0b000100, "fmovm", "", sFPR64_V_OP>;

defm FABS   : FT_XZ<0b000110, "fabs", UnOpFrag<(fabs node:$Src)>>;
defm FNEG   : FT_XZ<0b000111, "fneg", UnOpFrag<(fneg node:$Src)>>;
defm FSQRT  : FT_XZ<0b011010, "fsqrt", UnOpFrag<(fsqrt node:$Src)>>;

defm FADD   : FT_XYZ<0b000000, "fadd", BinOpFrag<(fadd node:$LHS, node:$RHS)>>;
defm FSUB   : FT_XYZ<0b000001, "fsub", BinOpFrag<(fsub node:$LHS, node:$RHS)>>;
defm FDIV   : FT_XYZ<0b011000, "fdiv", BinOpFrag<(fdiv node:$LHS, node:$RHS)>>;
defm FMUL   : FT_XYZ<0b010000, "fmul", BinOpFrag<(fmul node:$LHS, node:$RHS)>>;
defm FNMUL  : FT_XYZ<0b010001, "fnmul", BinOpFrag<(fneg (fmul node:$LHS, node:$RHS))>>;
defm FMAC   : FT_ACCUM_XYZ<0b010100, "fmac", TriOpFrag<(fadd node:$LHS, (fmul node:$MHS, node:$RHS))>>;
defm FMSC   : FT_ACCUM_XYZ<0b010101, "fmsc", TriOpFrag<(fsub (fmul node:$MHS, node:$RHS), node:$LHS)>>;
defm FNMAC  : FT_ACCUM_XYZ<0b010110, "fnmac", TriOpFrag<(fsub node:$LHS, (fmul node:$MHS, node:$RHS))>>;
defm FNMSC  : FT_ACCUM_XYZ<0b010111, "fnmsc", TriOpFrag<(fneg (fadd node:$LHS, (fmul node:$MHS, node:$RHS)))>>;

defm FCMPHS : FT_CMPXY<0b001100, "fcmphs">;
defm FCMPLT : FT_CMPXY<0b001101, "fcmplt">;
defm FCMPNE : FT_CMPXY<0b001110, "fcmpne">;
defm FCMPUO : FT_CMPXY<0b001111, "fcmpuo">;
defm FCMPZHS : FT_CMPZX<0b001000, "fcmpzhs">;
defm FCMPZLS : FT_CMPZX<0b001001, "fcmpzls">;
defm FCMPZNE : FT_CMPZX<0b001010, "fcmpzne">;
defm FCMPZUO : FT_CMPZX<0b001011, "fcmpzuo">;

defm FRECIP   : FT_MOV<0b011001, "frecip">;

// multiplication
let Predicates = [HasFPUv2_SF] in {
  def : Pat<(f32 (fmul (fneg sFPR32Op:$vrx), sFPR32Op:$vry)),
            (FNMUL_S sFPR32Op:$vrx, sFPR32Op:$vry)>;
}
let Predicates = [HasFPUv2_DF] in {
  def : Pat<(f64 (fmul (fneg sFPR64Op:$vrx), sFPR64Op:$vry)),
            (FNMUL_D sFPR64Op:$vrx, sFPR64Op:$vry)>;
}

//fmov, fmtvr, fmfvr
defm FMOV : FT_MOV<0b000100, "fmov">;
def FMFVRL : F_XZ_GF<3, 0b011001, (outs GPR:$rz), (ins sFPR32Op:$vrx),
                     "fmfvrl\t$rz, $vrx", [(set GPR:$rz, (bitconvert sFPR32Op:$vrx))]>;
def FMTVRL : F_XZ_FG<3, 0b011011, (outs sFPR32Op:$vrz), (ins GPR:$rx),
                     "fmtvrl\t$vrz, $rx", [(set sFPR32Op:$vrz, (bitconvert GPR:$rx))]>;

let Predicates = [HasFPUv2_DF] in {
  let isCodeGenOnly = 1 in
  def FMFVRL_D : F_XZ_GF<3, 0b011001, (outs GPR:$rz), (ins sFPR64Op:$vrx),
                         "fmfvrl\t$rz, $vrx", []>;
  def FMFVRH_D : F_XZ_GF<3, 0b011000, (outs GPR:$rz), (ins sFPR64Op:$vrx),
                         "fmfvrh\t$rz, $vrx", []>;
  let isCodeGenOnly = 1 in
  def FMTVRL_D : F_XZ_FG<3, 0b011011, (outs sFPR64Op:$vrz), (ins GPR:$rx),
                         "fmtvrl\t$vrz, $rx", []>;
let Constraints = "$vrZ = $vrz" in
  def FMTVRH_D : F_XZ_FG<3, 0b011010, (outs sFPR64Op:$vrz), (ins sFPR64Op:$vrZ, GPR:$rx),
                         "fmtvrh\t$vrz, $rx", []>;
}

//fcvt

def FSITOS  : F_XZ_TRANS<0b010000, "fsitos", sFPR32Op, sFPR32Op>;
def : Pat<(f32 (sint_to_fp GPR:$a)),
          (FSITOS (COPY_TO_REGCLASS GPR:$a, sFPR32))>,
          Requires<[HasFPUv2_SF]>;

def FUITOS  : F_XZ_TRANS<0b010001, "fuitos", sFPR32Op, sFPR32Op>;
def : Pat<(f32 (uint_to_fp GPR:$a)),
          (FUITOS (COPY_TO_REGCLASS GPR:$a, sFPR32))>,
          Requires<[HasFPUv2_SF]>;

def FSITOD  : F_XZ_TRANS<0b010100, "fsitod", sFPR64Op, sFPR64Op>;
def : Pat<(f64 (sint_to_fp GPR:$a)),
            (FSITOD (COPY_TO_REGCLASS GPR:$a, sFPR64))>,
           Requires<[HasFPUv2_DF]>;

def FUITOD  : F_XZ_TRANS<0b010101, "fuitod", sFPR64Op, sFPR64Op>;
def : Pat<(f64 (uint_to_fp GPR:$a)),
            (FUITOD (COPY_TO_REGCLASS GPR:$a, sFPR64))>,
           Requires<[HasFPUv2_DF]>;

let Predicates = [HasFPUv2_DF] in {
def FDTOS   : F_XZ_TRANS_DS<0b010110,"fdtos", UnOpFrag<(fpround node:$Src)>>;
def FSTOD   : F_XZ_TRANS_SD<0b010111,"fstod", UnOpFrag<(fpextend node:$Src)>>;
}

def rpiFSTOSI : F_XZ_TRANS<0b000010, "fstosi.rpi", sFPR32Op, sFPR32Op>;
def rpiFSTOUI : F_XZ_TRANS<0b000110, "fstoui.rpi", sFPR32Op, sFPR32Op>;
def rzFSTOSI : F_XZ_TRANS<0b000001, "fstosi.rz", sFPR32Op, sFPR32Op>;
def rzFSTOUI : F_XZ_TRANS<0b000101, "fstoui.rz", sFPR32Op, sFPR32Op>;
def rnFSTOSI : F_XZ_TRANS<0b000000, "fstosi.rn", sFPR32Op, sFPR32Op>;
def rnFSTOUI : F_XZ_TRANS<0b000100, "fstoui.rn", sFPR32Op, sFPR32Op>;
def rniFSTOSI : F_XZ_TRANS<0b000011, "fstosi.rni", sFPR32Op, sFPR32Op>;
def rniFSTOUI : F_XZ_TRANS<0b000111, "fstoui.rni", sFPR32Op, sFPR32Op>;

let Predicates = [HasFPUv2_DF] in {
def rpiFDTOSI : F_XZ_TRANS<0b001010, "fdtosi.rpi", sFPR64Op, sFPR64Op>;
def rpiFDTOUI : F_XZ_TRANS<0b001110, "fdtoui.rpi", sFPR64Op, sFPR64Op>;
def rzFDTOSI : F_XZ_TRANS<0b001001, "fdtosi.rz", sFPR64Op, sFPR64Op>;
def rzFDTOUI : F_XZ_TRANS<0b001101, "fdtoui.rz", sFPR64Op, sFPR64Op>;
def rnFDTOSI : F_XZ_TRANS<0b001000, "fdtosi.rn", sFPR64Op, sFPR64Op>;
def rnFDTOUI : F_XZ_TRANS<0b001100, "fdtoui.rn", sFPR64Op, sFPR64Op>;
def rniFDTOSI : F_XZ_TRANS<0b001011, "fdtosi.rni", sFPR64Op, sFPR64Op>;
def rniFDTOUI : F_XZ_TRANS<0b001111, "fdtoui.rni", sFPR64Op, sFPR64Op>;
}

multiclass FPToIntegerPats<SDNode round, string SUFFIX> {
  def : Pat<(i32 (fp_to_sint (round sFPR32Op:$Rn))),
            (COPY_TO_REGCLASS (!cast<Instruction>(SUFFIX # FSTOSI) sFPR32Op:$Rn), GPR)>,
            Requires<[HasFPUv2_SF]>;
  def : Pat<(i32 (fp_to_uint (round sFPR32Op:$Rn))),
            (COPY_TO_REGCLASS (!cast<Instruction>(SUFFIX # FSTOUI) sFPR32Op:$Rn), GPR)>,
            Requires<[HasFPUv2_SF]>;
  def : Pat<(i32 (fp_to_sint (round sFPR64Op:$Rn))),
            (COPY_TO_REGCLASS (!cast<Instruction>(SUFFIX # FDTOSI) sFPR64Op:$Rn), GPR)>,
            Requires<[HasFPUv2_DF]>;
  def : Pat<(i32 (fp_to_uint (round sFPR64Op:$Rn))),
            (COPY_TO_REGCLASS (!cast<Instruction>(SUFFIX # FDTOUI) sFPR64Op:$Rn), GPR)>,
            Requires<[HasFPUv2_DF]>;
}

defm: FPToIntegerPats<fceil, "rpi">;
defm: FPToIntegerPats<fround, "rn">;
defm: FPToIntegerPats<ffloor, "rni">;

multiclass FPToIntegerTowardszeroPats<string SUFFIX> {
  def : Pat<(i32 (fp_to_sint sFPR32Op:$Rn)),
            (COPY_TO_REGCLASS (!cast<Instruction>(SUFFIX # FSTOSI) sFPR32Op:$Rn), GPR)>,
            Requires<[HasFPUv2_SF]>;
  def : Pat<(i32 (fp_to_uint sFPR32Op:$Rn)),
            (COPY_TO_REGCLASS (!cast<Instruction>(SUFFIX # FSTOUI) sFPR32Op:$Rn), GPR)>,
            Requires<[HasFPUv2_SF]>;
  def : Pat<(i32 (fp_to_sint sFPR64Op:$Rn)),
            (COPY_TO_REGCLASS (!cast<Instruction>(SUFFIX # FDTOSI) sFPR64Op:$Rn), GPR)>,
            Requires<[HasFPUv2_DF]>;
  def : Pat<(i32 (fp_to_uint sFPR64Op:$Rn)),
            (COPY_TO_REGCLASS (!cast<Instruction>(SUFFIX # FDTOUI) sFPR64Op:$Rn), GPR)>,
            Requires<[HasFPUv2_DF]>;
}

defm: FPToIntegerTowardszeroPats<"rz">;


//fld, fst
let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in {
  defm FLD : FT_XYAI_LD<0b0010000, "fld">;
  defm FLDR : FT_XYAR_LD<0b0010100, "fldr">;
  defm FLDM : FT_XYAR_LDM<0b0011000, "fldm">;

  let Predicates = [HasFPUv2_DF] in
  def FLDRM : F_XYAR_LD<0b0010101, 0, "fldrm", "", sFPR64Op>;
  let Predicates = [HasFPUv2_DF] in
  def FLDMM : F_I4_XY_MEM<0b0011001, 0,
    (outs), (ins GPR:$rx, regseq_d1:$regs, variable_ops), "fldmm\t$regs, (${rx})", []>;
  let Predicates = [HasFPUv2_DF] in
  def FLDM : F_XYAI_LD<0b0010001, 0, "fldm", "", sFPR64Op, uimm8_3>;
}



let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in {
  defm FST : FT_XYAI_ST<0b0010010, "fst">;
  defm FSTR : FT_XYAR_ST<0b0010110, "fstr">;
  defm FSTM : FT_XYAR_STM<0b0011010, "fstm">;

  let Predicates = [HasFPUv2_DF] in
  def FSTRM : F_XYAR_ST<0b0010111, 0, "fstrm", "", sFPR64Op>;
  let Predicates = [HasFPUv2_DF] in
  def FSTMM :  F_I4_XY_MEM<0b0011011, 0,
    (outs), (ins GPR:$rx, regseq_d1:$regs, variable_ops), "fstmm\t$regs, (${rx})", []>;
  let Predicates = [HasFPUv2_DF] in
  def FSTM : F_XYAI_ST<0b0010011, 0, "fstm", "", sFPR64Op, uimm8_3>;
}

defm : LdPat<load, uimm8_2, FLD_S, f32>, Requires<[HasFPUv2_SF]>;
defm : LdPat<load, uimm8_2, FLD_D, f64>, Requires<[HasFPUv2_DF]>;
defm : LdrPat<load, FLDR_S, f32>, Requires<[HasFPUv2_SF]>;
defm : LdrPat<load, FLDR_D, f64>, Requires<[HasFPUv2_DF]>;

defm : StPat<store, f32, uimm8_2, FST_S>, Requires<[HasFPUv2_SF]>;
defm : StPat<store, f64, uimm8_2, FST_D>, Requires<[HasFPUv2_DF]>;
defm : StrPat<store, f32, FSTR_S>, Requires<[HasFPUv2_SF]>;
defm : StrPat<store, f64, FSTR_D>, Requires<[HasFPUv2_DF]>;


def : Pat<(f32 fpimm16:$imm), (COPY_TO_REGCLASS (MOVI32 (fpimm32_lo16 fpimm16:$imm)), sFPR32)>,
        Requires<[HasFPUv2_SF]>;
def : Pat<(f32 fpimm16_16:$imm), (f32 (COPY_TO_REGCLASS (MOVIH32 (fpimm32_hi16 fpimm16_16:$imm)), sFPR32))>,
        Requires<[HasFPUv2_SF]>;
def : Pat<(f32 fpimm:$imm), (COPY_TO_REGCLASS (ORI32 (MOVIH32 (fpimm32_hi16 fpimm:$imm)), (fpimm32_lo16 fpimm:$imm)), sFPR32)>,
        Requires<[HasFPUv2_SF]>;

def : Pat<(f64(CSKY_BITCAST_FROM_LOHI GPR:$rs1, GPR:$rs2)), (FMTVRH_D(FMTVRL_D GPR:$rs1), GPR:$rs2)>,
        Requires<[HasFPUv2_DF]>;

multiclass BRCond_Bin<CondCode CC, string Instr, Instruction Br0, Instruction Br1, Instruction MV> {
  let Predicates = [HasFPUv2_SF] in
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, sFPR32Op:$rs2, CC)), bb:$imm16),
            (Br0 (!cast<Instruction>(Instr#_S) sFPR32Op:$rs1, sFPR32Op:$rs2), bb:$imm16)>;
  let Predicates = [HasFPUv2_SF] in
  def : Pat<(brcond (xor (i32 (setcc sFPR32Op:$rs1, sFPR32Op:$rs2, CC)), 1), bb:$imm16),
            (Br1 (!cast<Instruction>(Instr#_S) sFPR32Op:$rs1, sFPR32Op:$rs2), bb:$imm16)>;
  let Predicates = [HasFPUv2_DF] in
  def : Pat<(brcond (i32 (setcc sFPR64Op:$rs1, sFPR64Op:$rs2, CC)), bb:$imm16),
            (Br0 (!cast<Instruction>(Instr#_D) sFPR64Op:$rs1, sFPR64Op:$rs2), bb:$imm16)>;
  let Predicates = [HasFPUv2_DF] in
  def : Pat<(brcond (xor (i32 (setcc sFPR64Op:$rs1, sFPR64Op:$rs2, CC)), 1), bb:$imm16),
            (Br1 (!cast<Instruction>(Instr#_D) sFPR64Op:$rs1, sFPR64Op:$rs2), bb:$imm16)>;

  let Predicates = [HasFPUv2_SF] in
  def : Pat<(i32 (setcc sFPR32Op:$rs1, sFPR32Op:$rs2, CC)),
            (MV (!cast<Instruction>(Instr#_S) sFPR32Op:$rs1, sFPR32Op:$rs2))>;
  let Predicates = [HasFPUv2_DF] in
  def : Pat<(i32 (setcc sFPR64Op:$rs1, sFPR64Op:$rs2, CC)),
            (MV (!cast<Instruction>(Instr#_D) sFPR64Op:$rs1, sFPR64Op:$rs2))>;
}

multiclass BRCond_Bin_SWAP<CondCode CC, string Instr, Instruction Br0, Instruction Br1, Instruction MV> {
  let Predicates = [HasFPUv2_SF] in
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, sFPR32Op:$rs2, CC)), bb:$imm16),
            (Br0 (!cast<Instruction>(Instr#_S) sFPR32Op:$rs2, sFPR32Op:$rs1), bb:$imm16)>;
  let Predicates = [HasFPUv2_SF] in
  def : Pat<(brcond (xor (i32 (setcc sFPR32Op:$rs1, sFPR32Op:$rs2, CC)), 1), bb:$imm16),
            (Br1 (!cast<Instruction>(Instr#_S) sFPR32Op:$rs2, sFPR32Op:$rs1), bb:$imm16)>;
  let Predicates = [HasFPUv2_DF] in
  def : Pat<(brcond (i32 (setcc sFPR64Op:$rs1, sFPR64Op:$rs2, CC)), bb:$imm16),
            (Br0 (!cast<Instruction>(Instr#_D) sFPR64Op:$rs2, sFPR64Op:$rs1), bb:$imm16)>;
  let Predicates = [HasFPUv2_DF] in
  def : Pat<(brcond (xor (i32 (setcc sFPR64Op:$rs1, sFPR64Op:$rs2, CC)), 1), bb:$imm16),
            (Br1 (!cast<Instruction>(Instr#_D) sFPR64Op:$rs2, sFPR64Op:$rs1), bb:$imm16)>;

  let Predicates = [HasFPUv2_SF] in
  def : Pat<(i32 (setcc sFPR32Op:$rs1, sFPR32Op:$rs2, CC)),
            (MV (!cast<Instruction>(Instr#_S) sFPR32Op:$rs2, sFPR32Op:$rs1))>;
  let Predicates = [HasFPUv2_DF] in
  def : Pat<(i32 (setcc sFPR64Op:$rs1, sFPR64Op:$rs2, CC)),
            (MV (!cast<Instruction>(Instr#_D) sFPR64Op:$rs2, sFPR64Op:$rs1))>;
}

// inverse (order && compare) to (unorder || inverse(compare))

defm : BRCond_Bin<SETUNE, "FCMPNE", BT32, BF32, MVC32>;
defm : BRCond_Bin<SETOEQ, "FCMPNE", BF32, BT32, MVCV32>;
defm : BRCond_Bin<SETOGE, "FCMPHS", BT32, BF32, MVC32>;
defm : BRCond_Bin<SETOLT, "FCMPLT", BT32, BF32, MVC32>;
defm : BRCond_Bin<SETUO, "FCMPUO", BT32, BF32, MVC32>;
defm : BRCond_Bin<SETO, "FCMPUO", BF32, BT32, MVCV32>;
defm : BRCond_Bin_SWAP<SETOGT, "FCMPLT", BT32, BF32, MVC32>;
defm : BRCond_Bin_SWAP<SETOLE, "FCMPHS", BT32, BF32, MVC32>;

defm : BRCond_Bin<SETNE, "FCMPNE", BT32, BF32, MVC32>;
defm : BRCond_Bin<SETEQ, "FCMPNE", BF32, BT32, MVCV32>;
defm : BRCond_Bin<SETGE, "FCMPHS", BT32, BF32, MVC32>;
defm : BRCond_Bin<SETLT, "FCMPLT", BT32, BF32, MVC32>;
defm : BRCond_Bin_SWAP<SETGT, "FCMPLT", BT32, BF32, MVC32>;
defm : BRCond_Bin_SWAP<SETLE, "FCMPHS", BT32, BF32, MVC32>;

// -----------

let Predicates = [HasFPUv2_SF] in {
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm0, SETOGE)), bb:$imm16),
            (BT32 (FCMPZHS_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm0, SETOGE)),
            (MVC32 (FCMPZHS_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm0, SETOLT)), bb:$imm16),
            (BF32 (FCMPZHS_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm0, SETOLT)),
            (MVCV32 (FCMPZHS_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm0, SETOLE)), bb:$imm16),
            (BT32 (FCMPZLS_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm0, SETOLE)),
            (MVC32 (FCMPZLS_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm0, SETOGT)), bb:$imm16),
            (BF32 (FCMPZLS_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm0, SETOGT)),
            (MVCV32 (FCMPZLS_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm0, SETUNE)), bb:$imm16),
            (BT32 (FCMPZNE_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm0, SETUNE)),
            (MVC32 (FCMPZNE_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm0, SETOEQ)), bb:$imm16),
            (BF32 (FCMPZNE_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm0, SETOEQ)),
            (MVCV32 (FCMPZNE_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm, SETUO)), bb:$imm16),
            (BT32 (FCMPZUO_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm, SETUO)),
            (MVC32 (FCMPZUO_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm, SETO)), bb:$imm16),
            (BF32 (FCMPZUO_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm, SETO)),
            (MVCV32 (FCMPZUO_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm0, SETGE)), bb:$imm16),
            (BT32 (FCMPZHS_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm0, SETGE)),
            (MVC32 (FCMPZHS_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm0, SETLT)), bb:$imm16),
            (BF32 (FCMPZHS_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm0, SETLT)),
            (MVCV32 (FCMPZHS_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm0, SETLE)), bb:$imm16),
            (BT32 (FCMPZLS_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm0, SETLE)),
            (MVC32 (FCMPZLS_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm0, SETGT)), bb:$imm16),
            (BF32 (FCMPZLS_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm0, SETGT)),
            (MVCV32 (FCMPZLS_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm0, SETNE)), bb:$imm16),
            (BT32 (FCMPZNE_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm0, SETNE)),
            (MVC32 (FCMPZNE_S sFPR32Op:$rs1))>;
  def : Pat<(brcond (i32 (setcc sFPR32Op:$rs1, fpimm0, SETEQ)), bb:$imm16),
            (BF32 (FCMPZNE_S sFPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc sFPR32Op:$rs1, fpimm0, SETEQ)),
            (MVCV32 (FCMPZNE_S sFPR32Op:$rs1))>;
}

let usesCustomInserter = 1 in  {
  let Predicates = [HasFPUv2_SF] in
  def FSELS : CSKYPseudo<(outs sFPR32Op:$dst), (ins CARRY:$cond, sFPR32Op:$src1, sFPR32Op:$src2),
    "!fsels\t$dst, $src1, src2", [(set sFPR32Op:$dst, (select CARRY:$cond, sFPR32Op:$src1, sFPR32Op:$src2))]>;

  let Predicates = [HasFPUv2_DF] in
  def FSELD : CSKYPseudo<(outs sFPR64Op:$dst), (ins CARRY:$cond, sFPR64Op:$src1, sFPR64Op:$src2),
    "!fseld\t$dst, $src1, src2", [(set sFPR64Op:$dst, (select CARRY:$cond, sFPR64Op:$src1, sFPR64Op:$src2))]>;
}
