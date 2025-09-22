//===- CSKYInstrInfoF2.td - CSKY Instruction Float2.0 ------*- tablegen -*-===//
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

def regseq_f2 : Operand<i32> {
  let EncoderMethod = "getRegisterSeqOpValue";
  let ParserMatchClass = RegSeqAsmOperand<"V2">;
  let PrintMethod = "printRegisterSeq";
  let DecoderMethod = "DecodeRegSeqOperandF2";
  let MIOperandInfo = (ops FPR32, uimm5);
}

def regseq_d2 : Operand<i32> {
  let EncoderMethod = "getRegisterSeqOpValue";
  let ParserMatchClass = RegSeqAsmOperand<"V2">;
  let PrintMethod = "printRegisterSeq";
  let DecoderMethod = "DecodeRegSeqOperandD2";
  let MIOperandInfo = (ops FPR64, uimm5);
}

def FPR32Op : RegisterOperand<FPR32, "printFPR">;
def FPR64Op : RegisterOperand<FPR64, "printFPR">;

include "CSKYInstrFormatsF2.td"

// Predicates
def IsOrAdd: PatFrag<(ops node:$A, node:$B), (or node:$A, node:$B), [{
  return isOrEquivalentToAdd(N);
}]>;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

defm f2FADD   : F2_XYZ_T<0b000000, "fadd", BinOpFrag<(fadd node:$LHS, node:$RHS)>>;
defm f2FSUB   : F2_XYZ_T<0b000001, "fsub", BinOpFrag<(fsub node:$LHS, node:$RHS)>>;
defm f2FDIV   : F2_XYZ_T<0b011000, "fdiv", BinOpFrag<(fdiv node:$LHS, node:$RHS)>>;
defm f2FMUL   : F2_XYZ_T<0b010000, "fmul", BinOpFrag<(fmul node:$LHS, node:$RHS)>>;

defm f2FMAXNM : F2_XYZ_T<0b101000, "fmaxnm", BinOpFrag<(fmaxnum node:$LHS, node:$RHS)>>;
defm f2FMINNM : F2_XYZ_T<0b101001, "fminnm", BinOpFrag<(fminnum node:$LHS, node:$RHS)>>;

defm f2FABS   : F2_XZ_T<0b000110, "fabs", fabs>;
defm f2FNEG   : F2_XZ_T<0b000111, "fneg", fneg>;
defm f2FSQRT  : F2_XZ_T<0b011010, "fsqrt", fsqrt>;
defm f2FMOV   : F2_XZ_SET_T<0b000100, "fmov">;
def f2FMOVX   : F2_XZ_SET<0b00001, FPR32Op, 0b000101, "fmovx.32">;

defm f2RECIP   : F2_XZ_SET_T<0b011001, "frecip">;

// fld/fst
let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in {
  def f2FLD_S : F2_LDST_S<0b0, "fld", (outs FPR32Op:$vrz), (ins GPR:$rx, uimm8_2:$imm8)>;
  let Predicates = [HasFPUv3_DF] in
  def f2FLD_D : F2_LDST_D<0b0, "fld", (outs FPR64Op:$vrz), (ins GPR:$rx, uimm8_2:$imm8)>;
}
let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in {
  def f2FST_S : F2_LDST_S<0b1, "fst", (outs), (ins FPR32Op:$vrz, GPR:$rx, uimm8_2:$imm8)>;
  let Predicates = [HasFPUv3_DF] in
  def f2FST_D : F2_LDST_D<0b1, "fst", (outs), (ins FPR64Op:$vrz, GPR:$rx, uimm8_2:$imm8)>;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in {
  def f2FSTM_S : F2_LDSTM_S<0b1, 0, "fstm", (outs), (ins GPR:$rx, regseq_f2:$regs, variable_ops)>;
  let Predicates = [HasFPUv3_DF] in
  def f2FSTM_D : F2_LDSTM_D<0b1, 0, "fstm", (outs), (ins GPR:$rx, regseq_d2:$regs, variable_ops)>;

  def f2FSTMU_S : F2_LDSTM_S<0b1, 0b100, "fstmu", (outs), (ins GPR:$rx, regseq_f2:$regs, variable_ops)>;
  let Predicates = [HasFPUv3_DF] in
  def f2FSTMU_D : F2_LDSTM_D<0b1, 0b100, "fstmu", (outs), (ins GPR:$rx, regseq_d2:$regs, variable_ops)>;
}

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in {
  def f2FLDM_S : F2_LDSTM_S<0b0, 0, "fldm", (outs), (ins GPR:$rx, regseq_f2:$regs, variable_ops)>;
  let Predicates = [HasFPUv3_DF] in
  def f2FLDM_D : F2_LDSTM_D<0b0, 0, "fldm", (outs), (ins GPR:$rx,  regseq_d2:$regs, variable_ops)>;

  def f2FLDMU_S : F2_LDSTM_S<0b0, 0b100, "fldmu", (outs), (ins GPR:$rx, regseq_f2:$regs, variable_ops)>;
  let Predicates = [HasFPUv3_DF] in
  def f2FLDMU_D : F2_LDSTM_D<0b0, 0b100, "fldmu", (outs), (ins GPR:$rx, regseq_d2:$regs, variable_ops)>;
}

multiclass FLSR {
  let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in {
    def FLDR_S : F2_LDSTR_S<0b0, "fldr", (outs FPR32Op:$rz), (ins GPR:$rx, GPR:$ry, uimm2:$imm)>;
    let Predicates = [HasFPUv3_DF] in
    def FLDR_D : F2_LDSTR_D<0b0, "fldr", (outs FPR64Op:$rz), (ins GPR:$rx, GPR:$ry, uimm2:$imm)>;
  }
  let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in {
    def FSTR_S : F2_LDSTR_S<0b1, "fstr", (outs), (ins FPR32Op:$rz, GPR:$rx, GPR:$ry, uimm2:$imm)>;
    let Predicates = [HasFPUv3_DF] in
    def FSTR_D : F2_LDSTR_D<0b1, "fstr", (outs), (ins FPR64Op:$rz, GPR:$rx, GPR:$ry, uimm2:$imm)>;
  }
}

defm f2: FLSR;

def f2FLRW_S : F2_LRW<0b00, 0b0, "flrw.32", (outs FPR32Op:$vrz), (ins fconstpool_symbol:$imm8)>;
def f2FLRW_D : F2_LRW<0b01, 0b0, "flrw.64", (outs FPR64Op:$vrz), (ins fconstpool_symbol:$imm8)>;

def : Pat<(f32 (load constpool:$src)), (f2FLRW_S (to_tconstpool tconstpool:$src))>, Requires<[HasFPUv3_SF]>;
def : Pat<(f64 (load constpool:$src)), (f2FLRW_D (to_tconstpool tconstpool:$src))>, Requires<[HasFPUv3_DF]>;

defm : LdPat<load, uimm8_2, f2FLD_S, f32>, Requires<[HasFPUv3_SF]>;
defm : LdPat<load, uimm8_2, f2FLD_D, f64>, Requires<[HasFPUv3_DF]>;
defm : LdrPat<load, f2FLDR_S, f32>, Requires<[HasFPUv3_SF]>;
defm : LdrPat<load, f2FLDR_D, f64>, Requires<[HasFPUv3_DF]>;

defm : StPat<store, f32, uimm8_2, f2FST_S>, Requires<[HasFPUv3_SF]>;
defm : StPat<store, f64, uimm8_2, f2FST_D>, Requires<[HasFPUv3_DF]>;
defm : StrPat<store, f32, f2FSTR_S>, Requires<[HasFPUv3_SF]>;
defm : StrPat<store, f64, f2FSTR_D>, Requires<[HasFPUv3_DF]>;

// fmfvr
let vry = 0 in
def f2FMFVRL   : F2_XYZ<0b00011, 0b011001, "fmfvr.32.1\t$vrz, $vrx",
                        (outs GPR:$vrz), (ins FPR32Op:$vrx),
                        [(set GPR:$vrz, (bitconvert FPR32Op:$vrx))]>;
// TODO: vrz and vrz+1
def f2FMFVRL_2 : F2_XYZ<0b00011, 0b111010, "fmfvr.32.2\t$vrz, $vry, $vrx",
                        (outs GPR:$vrz, GPR:$vry), (ins FPR64Op:$vrx),
                        []>;

let Predicates = [HasFPUv3_DF] in {
let vry = 0 in {
let isCodeGenOnly = 1 in
def f2FMFVRL_D : F2_XYZ<0b00011, 0b011001, "fmfvr.32.1\t$vrz, $vrx",
                        (outs GPR:$vrz), (ins FPR64Op:$vrx),
                        []>;
def f2FMFVRH_D : F2_XYZ<0b00011, 0b011000, "fmfvrh\t$vrz, $vrx",
                        (outs GPR:$vrz), (ins FPR64Op:$vrx),
                        []>;
}
def f2FMFVR_D  : F2_XYZ<0b00011, 0b111000, "fmfvr.64\t$vrz, $vry, $vrx",
                        (outs GPR:$vrz, GPR:$vry), (ins FPR64Op:$vrx),
                        [(set GPR:$vrz, GPR:$vry, (CSKY_BITCAST_TO_LOHI FPR64Op:$vrx))]>;
}

// fmtvr
def f2FMTVRL   : F2_XZ_P<0b00011, 0b011011, "fmtvr.32.1",
                         [(set FPR32Op:$vrz, (bitconvert GPR:$vrx))],
                         (outs FPR32Op:$vrz), (ins GPR:$vrx)>;
// TODO: vrz and vrz+1
def f2FMTVRL_2 : F2_XYZ<0b00011, 0b111110, "fmtvr.32.2\t$vrz, $vrx, $vry",
                        (outs FPR32Op:$vrz), (ins GPR:$vrx, GPR:$vry),
                        []>;

let Predicates = [HasFPUv3_DF] in {
let isCodeGenOnly = 1 in
def f2FMTVRL_D : F2_XZ_P<0b00011, 0b011011, "fmtvr.32.1",
                         [],
                         (outs FPR64Op:$vrz), (ins GPR:$vrx)>;
let Constraints = "$vrZ = $vrz" in
def f2FMTVRH_D : F2_XZ_P<0b00011, 0b011010, "fmtvrh",
                         [],
                         (outs FPR64Op:$vrz), (ins FPR64Op:$vrZ, GPR:$vrx)>;
def f2FMTVR_D  : F2_XYZ<0b00011, 0b111100, "fmtvr.64\t$vrz, $vrx, $vry",
                        (outs FPR64Op:$vrz), (ins GPR:$vrx, GPR:$vry),
                        [(set FPR64Op:$vrz, (CSKY_BITCAST_FROM_LOHI GPR:$vrx, GPR:$vry))]>;
}

// fcmp

defm f2FCMPHS: F2_CXY_T<0b001100, "fcmphs">;
defm f2FCMPLT: F2_CXY_T<0b001101, "fcmplt">;
defm f2FCMPNE: F2_CXY_T<0b001110, "fcmpne">;
defm f2FCMPUO: F2_CXY_T<0b001111, "fcmpuo">;

defm f2FCMPHSZ: F2_CX_T<0b001000, "fcmphsz">;
defm f2FCMPHZ : F2_CX_T<0b101010, "fcmphz">;
defm f2FCMPLSZ: F2_CX_T<0b101011, "fcmplsz">;
defm f2FCMPLTZ: F2_CX_T<0b001001, "fcmpltz">;
defm f2FCMPNEZ: F2_CX_T<0b001010, "fcmpnez">;
defm f2FCMPUOZ: F2_CX_T<0b001011, "fcmpuoz">;

defm f2FMULA : F2_XYZZ_T<0b010100, "fmula",
  TriOpFrag<(fadd (fmul node:$LHS, node:$MHS), node:$RHS)>>;

defm f2FMULS : F2_XYZZ_T<0b010110, "fmuls",
  TriOpFrag<(fsub node:$RHS, (fmul node:$LHS, node:$MHS))>>;

defm f2FFMULA : F2_XYZZ_T<0b110000, "ffmula",
  TriOpFrag<(fma node:$LHS, node:$MHS, node:$RHS)>>;

defm f2FFMULS : F2_XYZZ_T<0b110001, "ffmuls",
  TriOpFrag<(fma (fneg node:$LHS), node:$MHS, node:$RHS)>>;

defm f2FFNMULA : F2_XYZZ_T<0b110010, "ffnmula",
  TriOpFrag<(fneg (fma node:$LHS, node:$MHS, node:$RHS))>>;

defm f2FFNMULS : F2_XYZZ_T<0b110011, "ffnmuls",
  TriOpFrag<(fma node:$LHS, node:$MHS, (fneg node:$RHS))>>;

defm f2FNMULA : F2_XYZZ_T<0b010111, "fnmula",
  TriOpFrag<(fneg (fadd (fmul node:$LHS, node:$MHS), node:$RHS))>>;

defm f2FNMULS : F2_XYZZ_T<0b010101, "fnmuls",
  TriOpFrag<(fneg (fsub node:$RHS, (fmul node:$LHS, node:$MHS)))>>;

defm f2FNMUL : F2_XYZ_T<0b010001, "fnmul",
  BinOpFrag<(fneg (fmul node:$LHS, node:$RHS))>>;

// multiplication
let Predicates = [HasFPUv3_SF] in {
  def : Pat<(f32 (fmul (fneg FPR32Op:$vrx), FPR32Op:$vry)),
            (f2FNMUL_S FPR32Op:$vrx, FPR32Op:$vry)>;
}
let Predicates = [HasFPUv3_DF] in {
  def : Pat<(f64 (fmul (fneg FPR64Op:$vrx), FPR64Op:$vry)),
            (f2FNMUL_D FPR64Op:$vrx, FPR64Op:$vry)>;
}

// fcvt
def f2FFTOS32_S  : F2_XZ_P<0b01000, 0b011011, "fftoi.f32.s32", [], (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
def f2FFTOU32_S  : F2_XZ_P<0b01000, 0b011010, "fftoi.f32.u32", [], (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
def f2FS32TOF_S  : F2_XZ_P<0b01001, 0b011011, "fitof.s32.f32", [], (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
def f2FU32TOF_S  : F2_XZ_P<0b01001, 0b011010, "fitof.u32.f32", [], (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
def f2FFTOXU32_S  : F2_XZ_P<0b01000, 0b001010, "fftox.f32.u32", [], (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
def f2FFTOXS32_S  : F2_XZ_P<0b01000, 0b001011, "fftox.f32.s32", [], (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
def f2FXTOFU32_S  : F2_XZ_P<0b01001, 0b001010, "fxtof.u32.f32", [], (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
def f2FXTOFS32_S  : F2_XZ_P<0b01001, 0b001011, "fxtof.s32.f32", [], (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
let Predicates = [HasFPUv3_DF] in {
def f2FFTOS32_D  : F2_XZ_P<0b01000, 0b011101, "fftoi.f64.s32", [], (outs FPR32Op:$vrz), (ins FPR64Op:$vrx)>;
def f2FFTOU32_D  : F2_XZ_P<0b01000, 0b011100, "fftoi.f64.u32", [], (outs FPR32Op:$vrz), (ins FPR64Op:$vrx)>;
def f2FS32TOF_D  : F2_XZ_P<0b01001, 0b011101, "fitof.s32.f64", [], (outs FPR64Op:$vrz), (ins FPR32Op:$vrx)>;
def f2FU32TOF_D  : F2_XZ_P<0b01001, 0b011100, "fitof.u32.f64", [], (outs FPR64Op:$vrz), (ins FPR32Op:$vrx)>;
def f2FFTOXU32_D  : F2_XZ_P<0b01000, 0b001100, "fftox.f64.u32", [], (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
def f2FFTOXS32_D  : F2_XZ_P<0b01000, 0b001101, "fftox.f64.s32", [], (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
def f2FXTOFU32_D  : F2_XZ_P<0b01001, 0b001100, "fxtof.u32.f64", [], (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
def f2FXTOFS32_D  : F2_XZ_P<0b01001, 0b001101, "fxtof.s32.f64", [], (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
}

defm f2FF32TOSI32 : F2_XZ_RM<0b00011, 0b0000, "fftoi.f32.s32", (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
defm f2FF32TOUI32 : F2_XZ_RM<0b00011, 0b0001, "fftoi.f32.u32", (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
defm f2FF32TOFI32 : F2_XZ_RM<0b01000, 0b1001, "fftofi.f32", (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
let Predicates = [HasFPUv3_DF] in {
defm f2FF64TOSI32 : F2_XZ_RM<0b00011, 0b0010, "fftoi.f64.s32", (outs FPR32Op:$vrz), (ins FPR64Op:$vrx)>;
defm f2FF64TOUI32 : F2_XZ_RM<0b00011, 0b0011, "fftoi.f64.u32", (outs FPR32Op:$vrz), (ins FPR64Op:$vrx)>;
defm f2FF64TOFI32 : F2_XZ_RM<0b01000, 0b1010, "fftofi.f64", (outs FPR32Op:$vrz), (ins FPR32Op:$vrx)>;
}

def : Pat<(i32 (fp_to_sint (fround FPR32Op:$vrx))), (COPY_TO_REGCLASS (f2FF32TOSI32_RN  $vrx), GPR)>, Requires<[HasFPUv3_SF]>;
def : Pat<(i32 (fp_to_uint (fround FPR32Op:$vrx))), (COPY_TO_REGCLASS (f2FF32TOUI32_RN  $vrx), GPR)>, Requires<[HasFPUv3_SF]>;
def : Pat<(i32 (fp_to_sint (fceil  FPR32Op:$vrx))), (COPY_TO_REGCLASS (f2FF32TOSI32_RPI $vrx), GPR)>, Requires<[HasFPUv3_SF]>;
def : Pat<(i32 (fp_to_uint (fceil  FPR32Op:$vrx))), (COPY_TO_REGCLASS (f2FF32TOUI32_RPI $vrx), GPR)>, Requires<[HasFPUv3_SF]>;
def : Pat<(i32 (fp_to_sint (ffloor FPR32Op:$vrx))), (COPY_TO_REGCLASS (f2FF32TOSI32_RNI $vrx), GPR)>, Requires<[HasFPUv3_SF]>;
def : Pat<(i32 (fp_to_uint (ffloor FPR32Op:$vrx))), (COPY_TO_REGCLASS (f2FF32TOUI32_RNI $vrx), GPR)>, Requires<[HasFPUv3_SF]>;
def : Pat<(i32 (fp_to_sint (ftrunc FPR32Op:$vrx))), (COPY_TO_REGCLASS (f2FF32TOSI32_RZ  $vrx), GPR)>, Requires<[HasFPUv3_SF]>;
def : Pat<(i32 (fp_to_uint (ftrunc FPR32Op:$vrx))), (COPY_TO_REGCLASS (f2FF32TOUI32_RZ  $vrx), GPR)>, Requires<[HasFPUv3_SF]>;
def : Pat<(i32 (fp_to_sint FPR32Op:$vrx)), (COPY_TO_REGCLASS (f2FF32TOSI32_RZ $vrx), GPR)>, Requires<[HasFPUv3_SF]>;
def : Pat<(i32 (fp_to_uint FPR32Op:$vrx)), (COPY_TO_REGCLASS (f2FF32TOUI32_RZ $vrx), GPR)>, Requires<[HasFPUv3_SF]>;

def : Pat<(i32 (fp_to_sint (fround FPR64Op:$vrx))), (COPY_TO_REGCLASS (f2FF64TOSI32_RN  $vrx), GPR)>, Requires<[HasFPUv3_DF]>;
def : Pat<(i32 (fp_to_uint (fround FPR64Op:$vrx))), (COPY_TO_REGCLASS (f2FF64TOUI32_RN  $vrx), GPR)>, Requires<[HasFPUv3_DF]>;
def : Pat<(i32 (fp_to_sint (fceil  FPR64Op:$vrx))), (COPY_TO_REGCLASS (f2FF64TOSI32_RPI $vrx), GPR)>, Requires<[HasFPUv3_DF]>;
def : Pat<(i32 (fp_to_uint (fceil  FPR64Op:$vrx))), (COPY_TO_REGCLASS (f2FF64TOUI32_RPI $vrx), GPR)>, Requires<[HasFPUv3_DF]>;
def : Pat<(i32 (fp_to_sint (ffloor FPR64Op:$vrx))), (COPY_TO_REGCLASS (f2FF64TOSI32_RNI $vrx), GPR)>, Requires<[HasFPUv3_DF]>;
def : Pat<(i32 (fp_to_uint (ffloor FPR64Op:$vrx))), (COPY_TO_REGCLASS (f2FF64TOUI32_RNI $vrx), GPR)>, Requires<[HasFPUv3_DF]>;
def : Pat<(i32 (fp_to_sint (ftrunc FPR64Op:$vrx))), (COPY_TO_REGCLASS (f2FF64TOSI32_RZ  $vrx), GPR)>, Requires<[HasFPUv3_DF]>;
def : Pat<(i32 (fp_to_uint (ftrunc FPR64Op:$vrx))), (COPY_TO_REGCLASS (f2FF64TOUI32_RZ  $vrx), GPR)>, Requires<[HasFPUv3_DF]>;
def : Pat<(i32 (fp_to_sint FPR64Op:$vrx)), (COPY_TO_REGCLASS (f2FF64TOSI32_RZ $vrx), GPR)>, Requires<[HasFPUv3_DF]>;
def : Pat<(i32 (fp_to_uint FPR64Op:$vrx)), (COPY_TO_REGCLASS (f2FF64TOUI32_RZ $vrx), GPR)>, Requires<[HasFPUv3_DF]>;

def : Pat<(sint_to_fp GPR:$vrx), (f2FS32TOF_S (COPY_TO_REGCLASS $vrx, FPR32))>, Requires<[HasFPUv3_SF]>;
def : Pat<(uint_to_fp GPR:$vrx), (f2FU32TOF_S (COPY_TO_REGCLASS $vrx, FPR32))>, Requires<[HasFPUv3_SF]>;
def : Pat<(sint_to_fp GPR:$vrx), (f2FS32TOF_D (COPY_TO_REGCLASS $vrx, FPR32))>, Requires<[HasFPUv3_DF]>;
def : Pat<(uint_to_fp GPR:$vrx), (f2FU32TOF_D (COPY_TO_REGCLASS $vrx, FPR32))>, Requires<[HasFPUv3_DF]>;

let Predicates = [HasFPUv3_DF] in {
def f2FDTOS   : F2_XZ_P<0b00011, 0b010110, "fdtos", [(set FPR32Op:$vrz, (fpround FPR64Op:$vrx))], (outs FPR32Op:$vrz),
                        (ins FPR64Op:$vrx)>;
def f2FSTOD   : F2_XZ_P<0b00011, 0b010111, "fstod", [(set FPR64Op:$vrz, (fpextend FPR32Op:$vrx))], (outs FPR64Op:$vrz),
                        (ins FPR32Op:$vrx)>;
}

// fsel
defm f2FSEL: F2_CXYZ_T<0b111001, "fsel">;

def f2FINS: F2_XZ_SET<0b00000, FPR32Op, 0b011011, "fins.32">;

def : Pat<(f32 fpimm16:$imm),(COPY_TO_REGCLASS (MOVI32 (fpimm32_lo16 fpimm16:$imm)), FPR32)>,
        Requires<[HasFPUv3_SF]>;
def : Pat<(f32 fpimm16_16:$imm), (COPY_TO_REGCLASS (MOVIH32 (fpimm32_hi16 fpimm16_16:$imm)), FPR32)>,
        Requires<[HasFPUv3_SF]>;
def : Pat<(f32 fpimm:$imm),(COPY_TO_REGCLASS (ORI32 (MOVIH32 (fpimm32_hi16 fpimm:$imm)), (fpimm32_lo16 fpimm:$imm)), FPR32)>,
        Requires<[HasFPUv3_SF]>;


multiclass BRCond_Bin_F2<CondCode CC, string Instr, Instruction Br0, Instruction Br1, Instruction MV, bit IsSelectSwap = 0> {
  let Predicates = [HasFPUv3_SF] in
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, FPR32Op:$rs2, CC)), bb:$imm16),
            (Br0 (!cast<Instruction>(Instr#_S) FPR32Op:$rs1, FPR32Op:$rs2), bb:$imm16)>;
  let Predicates = [HasFPUv3_SF] in
  def : Pat<(brcond (xor (i32 (setcc FPR32Op:$rs1, FPR32Op:$rs2, CC)), 1), bb:$imm16),
            (Br1 (!cast<Instruction>(Instr#_S) FPR32Op:$rs1, FPR32Op:$rs2), bb:$imm16)>;
  let Predicates = [HasFPUv3_DF] in
  def : Pat<(brcond (i32 (setcc FPR64Op:$rs1, FPR64Op:$rs2, CC)), bb:$imm16),
            (Br0 (!cast<Instruction>(Instr#_D) FPR64Op:$rs1, FPR64Op:$rs2), bb:$imm16)>;
  let Predicates = [HasFPUv3_DF] in
  def : Pat<(brcond (xor (i32 (setcc FPR64Op:$rs1, FPR64Op:$rs2, CC)), 1), bb:$imm16),
            (Br1 (!cast<Instruction>(Instr#_D) FPR64Op:$rs1, FPR64Op:$rs2), bb:$imm16)>;

  let Predicates = [HasFPUv3_SF] in
  def : Pat<(i32 (setcc FPR32Op:$rs1, FPR32Op:$rs2, CC)),
            (MV (!cast<Instruction>(Instr#_S) FPR32Op:$rs1, FPR32Op:$rs2))>;
  let Predicates = [HasFPUv3_DF] in
  def : Pat<(i32 (setcc FPR64Op:$rs1, FPR64Op:$rs2, CC)),
            (MV (!cast<Instruction>(Instr#_D) FPR64Op:$rs1, FPR64Op:$rs2))>;

  let Predicates = [HasFPUv3_SF] in {
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, FPR32Op:$rs2, CC)), FPR32Op:$rx, FPR32Op:$false),
            !if(
                !eq(IsSelectSwap, 0),
                (f2FSEL_S (!cast<Instruction>(Instr#_S) FPR32Op:$rs1, FPR32Op:$rs2), FPR32Op:$rx, FPR32Op:$false),
                (f2FSEL_S (!cast<Instruction>(Instr#_S) FPR32Op:$rs1, FPR32Op:$rs2), FPR32Op:$false, FPR32Op:$rx)
               )>;
  }
  let Predicates = [HasFPUv3_DF] in {
  def : Pat<(select (i32 (setcc FPR64Op:$rs1, FPR64Op:$rs2, CC)), FPR64Op:$rx, FPR64Op:$false),
            !if(
                !eq(IsSelectSwap, 0),
                (f2FSEL_D (!cast<Instruction>(Instr#_D) FPR64Op:$rs1, FPR64Op:$rs2), FPR64Op:$rx, FPR64Op:$false),
                (f2FSEL_D (!cast<Instruction>(Instr#_D) FPR64Op:$rs1, FPR64Op:$rs2), FPR64Op:$false, FPR64Op:$rx)
               )>;
  }
}

multiclass BRCond_Bin_SWAP_F2<CondCode CC, string Instr, Instruction Br0, Instruction Br1, Instruction MV, bit IsSelectSwap = 0> {
  let Predicates = [HasFPUv3_SF] in
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, FPR32Op:$rs2, CC)), bb:$imm16),
            (Br0 (!cast<Instruction>(Instr#_S) FPR32Op:$rs2, FPR32Op:$rs1), bb:$imm16)>;
  let Predicates = [HasFPUv3_SF] in
  def : Pat<(brcond (xor (i32 (setcc FPR32Op:$rs1, FPR32Op:$rs2, CC)), 1), bb:$imm16),
            (Br1 (!cast<Instruction>(Instr#_S) FPR32Op:$rs2, FPR32Op:$rs1), bb:$imm16)>;
  let Predicates = [HasFPUv3_DF] in
  def : Pat<(brcond (i32 (setcc FPR64Op:$rs1, FPR64Op:$rs2, CC)), bb:$imm16),
            (Br0 (!cast<Instruction>(Instr#_D) FPR64Op:$rs2, FPR64Op:$rs1), bb:$imm16)>;
  let Predicates = [HasFPUv3_DF] in
  def : Pat<(brcond (xor (i32 (setcc FPR64Op:$rs1, FPR64Op:$rs2, CC)), 1), bb:$imm16),
            (Br1 (!cast<Instruction>(Instr#_D) FPR64Op:$rs2, FPR64Op:$rs1), bb:$imm16)>;

  let Predicates = [HasFPUv3_SF] in
  def : Pat<(i32 (setcc FPR32Op:$rs1, FPR32Op:$rs2, CC)),
            (MV (!cast<Instruction>(Instr#_S) FPR32Op:$rs2, FPR32Op:$rs1))>;
  let Predicates = [HasFPUv3_DF] in
  def : Pat<(i32 (setcc FPR64Op:$rs1, FPR64Op:$rs2, CC)),
            (MV (!cast<Instruction>(Instr#_D) FPR64Op:$rs2, FPR64Op:$rs1))>;

  let Predicates = [HasFPUv3_SF] in {
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, FPR32Op:$rs2, CC)), FPR32Op:$rx, FPR32Op:$false),
            !if(
                !eq(IsSelectSwap, 0),
                (f2FSEL_S (!cast<Instruction>(Instr#_S) FPR32Op:$rs2, FPR32Op:$rs1), FPR32Op:$rx, FPR32Op:$false),
                (f2FSEL_S (!cast<Instruction>(Instr#_S) FPR32Op:$rs2, FPR32Op:$rs1), FPR32Op:$false, FPR32Op:$rx)
               )>;
  }
  let Predicates = [HasFPUv3_DF] in {
  def : Pat<(select (i32 (setcc FPR64Op:$rs1, FPR64Op:$rs2, CC)), FPR64Op:$rx, FPR64Op:$false),
            !if(
                !eq(IsSelectSwap, 0),
                (f2FSEL_D (!cast<Instruction>(Instr#_D) FPR64Op:$rs2, FPR64Op:$rs1), FPR64Op:$rx, FPR64Op:$false),
                (f2FSEL_D (!cast<Instruction>(Instr#_D) FPR64Op:$rs2, FPR64Op:$rs1), FPR64Op:$false, FPR64Op:$rx)
               )>;
  }
}

// inverse (order && compare) to (unorder || inverse(compare))

defm : BRCond_Bin_F2<SETUNE, "f2FCMPNE", BT32, BF32, MVC32>;
defm : BRCond_Bin_F2<SETOEQ, "f2FCMPNE", BF32, BT32, MVCV32, 1>;
defm : BRCond_Bin_F2<SETOGE, "f2FCMPHS", BT32, BF32, MVC32>;
defm : BRCond_Bin_F2<SETOLT, "f2FCMPLT", BT32, BF32, MVC32>;
defm : BRCond_Bin_F2<SETUO, "f2FCMPUO", BT32, BF32, MVC32>;
defm : BRCond_Bin_F2<SETO, "f2FCMPUO", BF32, BT32, MVCV32, 1>;
defm : BRCond_Bin_SWAP_F2<SETOGT, "f2FCMPLT", BT32, BF32, MVC32>;
defm : BRCond_Bin_SWAP_F2<SETOLE, "f2FCMPHS", BT32, BF32, MVC32>;

defm : BRCond_Bin_F2<SETNE, "f2FCMPNE", BT32, BF32, MVC32>;
defm : BRCond_Bin_F2<SETEQ, "f2FCMPNE", BF32, BT32, MVCV32, 1>;
defm : BRCond_Bin_F2<SETGE, "f2FCMPHS", BT32, BF32, MVC32>;
defm : BRCond_Bin_F2<SETLT, "f2FCMPLT", BT32, BF32, MVC32>;
defm : BRCond_Bin_SWAP_F2<SETGT, "f2FCMPLT", BT32, BF32, MVC32>;
defm : BRCond_Bin_SWAP_F2<SETLE, "f2FCMPHS", BT32, BF32, MVC32>;

// ------

let Predicates = [HasFPUv3_SF] in {
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm0, SETOGE)), bb:$imm16),
            (BT32 (f2FCMPHSZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm0, SETOGE)),
            (MVC32 (f2FCMPHSZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm0, SETOGE)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPHSZ_S FPR32Op:$rs1), FPR32Op:$rx, FPR32Op:$false)>;
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm0, SETOLT)), bb:$imm16),
            (BT32 (f2FCMPLTZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm0, SETOLT)),
            (MVC32 (f2FCMPLTZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm0, SETOLT)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPLTZ_S FPR32Op:$rs1), FPR32Op:$rx, FPR32Op:$false)>;
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm0, SETOLE)), bb:$imm16),
            (BT32 (f2FCMPLSZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm0, SETOLE)),
            (MVC32 (f2FCMPLSZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm0, SETOLE)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPLSZ_S FPR32Op:$rs1), FPR32Op:$rx, FPR32Op:$false)>;
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm0, SETOGT)), bb:$imm16),
            (BT32 (f2FCMPHZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm0, SETOGT)),
            (MVC32 (f2FCMPHZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm0, SETOGT)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPHZ_S FPR32Op:$rs1), FPR32Op:$rx, FPR32Op:$false)>;
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm0, SETUNE)), bb:$imm16),
            (BT32 (f2FCMPNEZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm0, SETUNE)),
            (MVC32 (f2FCMPNEZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm0, SETUNE)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPNEZ_S FPR32Op:$rs1), FPR32Op:$rx, FPR32Op:$false)>;
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm, SETUO)), bb:$imm16),
            (BT32 (f2FCMPUOZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm, SETUO)),
            (MVC32 (f2FCMPUOZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm, SETUO)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPUOZ_S FPR32Op:$rs1), FPR32Op:$rx, FPR32Op:$false)>;
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm0, SETGE)), bb:$imm16),
            (BT32 (f2FCMPHSZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm0, SETGE)),
            (MVC32 (f2FCMPHSZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm0, SETGE)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPHSZ_S FPR32Op:$rs1), FPR32Op:$rx, FPR32Op:$false)>;
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm0, SETLT)), bb:$imm16),
            (BT32 (f2FCMPLTZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm0, SETLT)),
            (MVC32 (f2FCMPLTZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm0, SETLT)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPLTZ_S FPR32Op:$rs1), FPR32Op:$rx, FPR32Op:$false)>;
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm0, SETLE)), bb:$imm16),
            (BT32 (f2FCMPLSZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm0, SETLE)),
            (MVC32 (f2FCMPLSZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm0, SETLE)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPLSZ_S FPR32Op:$rs1), FPR32Op:$rx, FPR32Op:$false)>;
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm0, SETGT)), bb:$imm16),
            (BT32 (f2FCMPHZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm0, SETGT)),
            (MVC32 (f2FCMPHZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm0, SETGT)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPHZ_S FPR32Op:$rs1), FPR32Op:$rx, FPR32Op:$false)>;


  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm, SETO)), bb:$imm16),
            (BF32 (f2FCMPUOZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm, SETO)),
            (MVCV32 (f2FCMPUOZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm, SETO)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPUOZ_S FPR32Op:$rs1), FPR32Op:$false, FPR32Op:$rx)>;
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm0, SETOEQ)), bb:$imm16),
            (BF32 (f2FCMPNEZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm0, SETOEQ)),
            (MVCV32 (f2FCMPNEZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm0, SETOEQ)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPNEZ_S FPR32Op:$rs1), FPR32Op:$false, FPR32Op:$rx)>;
  def : Pat<(brcond (i32 (setcc FPR32Op:$rs1, fpimm0, SETEQ)), bb:$imm16),
            (BF32 (f2FCMPNEZ_S FPR32Op:$rs1), bb:$imm16)>;
  def : Pat<(i32 (setcc FPR32Op:$rs1, fpimm0, SETEQ)),
            (MVCV32 (f2FCMPNEZ_S FPR32Op:$rs1))>;
  def : Pat<(select (i32 (setcc FPR32Op:$rs1, fpimm0, SETEQ)), FPR32Op:$rx, FPR32Op:$false),
            (f2FSEL_S (f2FCMPNEZ_S FPR32Op:$rs1), FPR32Op:$false, FPR32Op:$rx)>;
}


let Predicates = [HasFPUv3_SF] in
def : Pat<(select CARRY:$ca, FPR32Op:$rx, FPR32Op:$false),
          (f2FSEL_S CARRY:$ca, FPR32Op:$rx, FPR32Op:$false)>;
let Predicates = [HasFPUv3_DF] in
def : Pat<(select CARRY:$ca, FPR64Op:$rx, FPR64Op:$false),
          (f2FSEL_D CARRY:$ca, FPR64Op:$rx, FPR64Op:$false)>;
