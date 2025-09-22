//===-- X86InstrMMX.td - Describe the MMX Instruction Set --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the X86 MMX instruction set, defining the instructions,
// and properties of the instructions which are needed for code generation,
// machine code emission, and analysis.
//
// All instructions that use MMX should be in this file, even if they also use
// SSE.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MMX Multiclasses
//===----------------------------------------------------------------------===//

// Alias instruction that maps zero vector to pxor mmx.
// This is expanded by ExpandPostRAPseudos to an pxor.
// We set canFoldAsLoad because this can be converted to a constant-pool
// load of an all-zeros value if folding it would be beneficial.
let isReMaterializable = 1, isAsCheapAsAMove = 1, canFoldAsLoad = 1,
    isPseudo = 1, SchedRW = [WriteZero], Predicates = [HasMMX] in {
def MMX_SET0 : I<0, Pseudo, (outs VR64:$dst), (ins), "",
                 [(set VR64:$dst, (x86mmx (MMX_X86movw2d (i32 0))))]>;
}

let Constraints = "$src1 = $dst" in {
  // MMXI_binop_rm_int - Simple MMX binary operator based on intrinsic.
  multiclass MMXI_binop_rm_int<bits<8> opc, string OpcodeStr, Intrinsic IntId,
                               X86FoldableSchedWrite sched, bit Commutable = 0,
                               X86MemOperand OType = i64mem> {
    def rr : MMXI<opc, MRMSrcReg, (outs VR64:$dst),
                 (ins VR64:$src1, VR64:$src2),
                 !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
                 [(set VR64:$dst, (IntId VR64:$src1, VR64:$src2))]>,
              Sched<[sched]> {
      let isCommutable = Commutable;
    }
    def rm : MMXI<opc, MRMSrcMem, (outs VR64:$dst),
                 (ins VR64:$src1, OType:$src2),
                 !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
                 [(set VR64:$dst, (IntId VR64:$src1, (load_mmx addr:$src2)))]>,
                 Sched<[sched.Folded, sched.ReadAfterFold]>;
  }

  multiclass MMXI_binop_rmi_int<bits<8> opc, bits<8> opc2, Format ImmForm,
                                string OpcodeStr, Intrinsic IntId,
                                Intrinsic IntId2, X86FoldableSchedWrite sched,
                                X86FoldableSchedWrite schedImm> {
    def rr : MMXI<opc, MRMSrcReg, (outs VR64:$dst),
                                  (ins VR64:$src1, VR64:$src2),
                  !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
                  [(set VR64:$dst, (IntId VR64:$src1, VR64:$src2))]>,
             Sched<[sched]>;
    def rm : MMXI<opc, MRMSrcMem, (outs VR64:$dst),
                                  (ins VR64:$src1, i64mem:$src2),
                  !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
                  [(set VR64:$dst, (IntId VR64:$src1, (load_mmx addr:$src2)))]>,
                  Sched<[sched.Folded, sched.ReadAfterFold]>;
    def ri : MMXIi8<opc2, ImmForm, (outs VR64:$dst),
                                   (ins VR64:$src1, i32u8imm:$src2),
                    !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
           [(set VR64:$dst, (IntId2 VR64:$src1, timm:$src2))]>,
           Sched<[schedImm]>;
  }
}

/// Unary MMX instructions requiring SSSE3.
multiclass SS3I_unop_rm_int_mm<bits<8> opc, string OpcodeStr,
                               Intrinsic IntId64, X86FoldableSchedWrite sched> {
  def rr : MMXSS38I<opc, MRMSrcReg, (outs VR64:$dst), (ins VR64:$src),
                 !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                 [(set VR64:$dst, (IntId64 VR64:$src))]>,
           Sched<[sched]>;

  def rm : MMXSS38I<opc, MRMSrcMem, (outs VR64:$dst), (ins i64mem:$src),
                 !strconcat(OpcodeStr, "\t{$src, $dst|$dst, $src}"),
                 [(set VR64:$dst, (IntId64 (load_mmx addr:$src)))]>,
                 Sched<[sched.Folded]>;
}

/// Binary MMX instructions requiring SSSE3.
let ImmT = NoImm, Constraints = "$src1 = $dst" in {
multiclass SS3I_binop_rm_int_mm<bits<8> opc, string OpcodeStr,
                             Intrinsic IntId64, X86FoldableSchedWrite sched,
                             bit Commutable = 0> {
  let isCommutable = Commutable in
  def rr : MMXSS38I<opc, MRMSrcReg, (outs VR64:$dst),
       (ins VR64:$src1, VR64:$src2),
        !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
       [(set VR64:$dst, (IntId64 VR64:$src1, VR64:$src2))]>,
      Sched<[sched]>;
  def rm : MMXSS38I<opc, MRMSrcMem, (outs VR64:$dst),
       (ins VR64:$src1, i64mem:$src2),
        !strconcat(OpcodeStr, "\t{$src2, $dst|$dst, $src2}"),
       [(set VR64:$dst,
         (IntId64 VR64:$src1, (load_mmx addr:$src2)))]>,
      Sched<[sched.Folded, sched.ReadAfterFold]>;
}
}

/// PALIGN MMX instructions (require SSSE3).
multiclass ssse3_palign_mm<string asm, Intrinsic IntId,
                           X86FoldableSchedWrite sched> {
  def rri  : MMXSS3AI<0x0F, MRMSrcReg, (outs VR64:$dst),
      (ins VR64:$src1, VR64:$src2, u8imm:$src3),
      !strconcat(asm, "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
      [(set VR64:$dst, (IntId VR64:$src1, VR64:$src2, (i8 timm:$src3)))]>,
      Sched<[sched]>;
  def rmi  : MMXSS3AI<0x0F, MRMSrcMem, (outs VR64:$dst),
      (ins VR64:$src1, i64mem:$src2, u8imm:$src3),
      !strconcat(asm, "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
      [(set VR64:$dst, (IntId VR64:$src1, (load_mmx addr:$src2),
                                          (i8 timm:$src3)))]>,
      Sched<[sched.Folded, sched.ReadAfterFold]>;
}

multiclass sse12_cvt_pint<bits<8> opc, RegisterClass SrcRC, RegisterClass DstRC,
                         Intrinsic Int, X86MemOperand x86memop, PatFrag ld_frag,
                         string asm, X86FoldableSchedWrite sched, Domain d> {
  def rr : MMXPI<opc, MRMSrcReg, (outs DstRC:$dst), (ins SrcRC:$src), asm,
                 [(set DstRC:$dst, (Int SrcRC:$src))], d>,
           Sched<[sched]>;
  def rm : MMXPI<opc, MRMSrcMem, (outs DstRC:$dst), (ins x86memop:$src), asm,
                 [(set DstRC:$dst, (Int (ld_frag addr:$src)))], d>,
           Sched<[sched.Folded]>;
}

multiclass sse12_cvt_pint_3addr<bits<8> opc, RegisterClass SrcRC,
                    RegisterClass DstRC, Intrinsic Int, X86MemOperand x86memop,
                    PatFrag ld_frag, string asm, Domain d> {
  def rr : MMXPI<opc, MRMSrcReg, (outs DstRC:$dst),
                 (ins DstRC:$src1, SrcRC:$src2), asm,
                 [(set DstRC:$dst, (Int DstRC:$src1, SrcRC:$src2))], d>,
                 Sched<[WriteCvtI2PS]>;
  def rm : MMXPI<opc, MRMSrcMem, (outs DstRC:$dst),
                 (ins DstRC:$src1, x86memop:$src2), asm,
                 [(set DstRC:$dst, (Int DstRC:$src1, (ld_frag addr:$src2)))], d>,
                 Sched<[WriteCvtI2PS.Folded]>;
}

//===----------------------------------------------------------------------===//
// MMX EMMS Instruction
//===----------------------------------------------------------------------===//

let SchedRW = [WriteEMMS],
    Defs = [MM0, MM1, MM2, MM3, MM4, MM5, MM6, MM7,
            ST0, ST1, ST2, ST3, ST4, ST5, ST6, ST7] in
def MMX_EMMS  : MMXI<0x77, RawFrm, (outs), (ins), "emms", [(int_x86_mmx_emms)]>;

//===----------------------------------------------------------------------===//
// MMX Scalar Instructions
//===----------------------------------------------------------------------===//

// Data Transfer Instructions
def MMX_MOVD64rr : MMXI<0x6E, MRMSrcReg, (outs VR64:$dst), (ins GR32:$src),
                        "movd\t{$src, $dst|$dst, $src}",
                        [(set VR64:$dst,
                         (x86mmx (MMX_X86movw2d GR32:$src)))]>,
                        Sched<[WriteVecMoveFromGpr]>;
def MMX_MOVD64rm : MMXI<0x6E, MRMSrcMem, (outs VR64:$dst), (ins i32mem:$src),
                        "movd\t{$src, $dst|$dst, $src}",
                        [(set VR64:$dst,
                          (x86mmx (MMX_X86movw2d (loadi32 addr:$src))))]>,
                        Sched<[WriteVecLoad]>;

let mayStore = 1 in
def MMX_MOVD64mr : MMXI<0x7E, MRMDestMem, (outs), (ins i32mem:$dst, VR64:$src),
                        "movd\t{$src, $dst|$dst, $src}", []>,
                   Sched<[WriteVecStore]>;

def MMX_MOVD64grr : MMXI<0x7E, MRMDestReg, (outs GR32:$dst), (ins VR64:$src),
                         "movd\t{$src, $dst|$dst, $src}",
                         [(set GR32:$dst,
                          (MMX_X86movd2w (x86mmx VR64:$src)))]>,
                         Sched<[WriteVecMoveToGpr]>;

let isBitcast = 1 in
def MMX_MOVD64to64rr : MMXRI<0x6E, MRMSrcReg, (outs VR64:$dst), (ins GR64:$src),
                             "movq\t{$src, $dst|$dst, $src}",
                             [(set VR64:$dst, (bitconvert GR64:$src))]>,
                             Sched<[WriteVecMoveFromGpr]>;

let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0, mayLoad = 1 in
def MMX_MOVD64to64rm : MMXRI<0x6E, MRMSrcMem, (outs VR64:$dst),
                             (ins i64mem:$src), "movq\t{$src, $dst|$dst, $src}",
                             []>, Sched<[SchedWriteVecMoveLS.MMX.RM]>;

let isBitcast = 1 in {
def MMX_MOVD64from64rr : MMXRI<0x7E, MRMDestReg,
                               (outs GR64:$dst), (ins VR64:$src),
                               "movq\t{$src, $dst|$dst, $src}",
                               [(set GR64:$dst, (bitconvert VR64:$src))]>,
                               Sched<[WriteVecMoveToGpr]>;
let SchedRW = [WriteVecMove], hasSideEffects = 0, isMoveReg = 1 in {
def MMX_MOVQ64rr : MMXI<0x6F, MRMSrcReg, (outs VR64:$dst), (ins VR64:$src),
                        "movq\t{$src, $dst|$dst, $src}", []>;
let isCodeGenOnly = 1, ForceDisassemble = 1 in
def MMX_MOVQ64rr_REV : MMXI<0x7F, MRMDestReg, (outs VR64:$dst), (ins VR64:$src),
                            "movq\t{$src, $dst|$dst, $src}", []>;
} // SchedRW, hasSideEffects, isMoveReg
} // isBitcast

let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0, mayStore = 1 in
def MMX_MOVD64from64mr : MMXRI<0x7E, MRMDestMem,
                               (outs), (ins i64mem:$dst, VR64:$src),
                               "movq\t{$src, $dst|$dst, $src}", []>,
                               Sched<[SchedWriteVecMoveLS.MMX.MR]>;

let SchedRW = [SchedWriteVecMoveLS.MMX.RM] in {
let canFoldAsLoad = 1 in
def MMX_MOVQ64rm : MMXI<0x6F, MRMSrcMem, (outs VR64:$dst), (ins i64mem:$src),
                        "movq\t{$src, $dst|$dst, $src}",
                        [(set VR64:$dst, (load_mmx addr:$src))]>;
} // SchedRW

let SchedRW = [SchedWriteVecMoveLS.MMX.MR] in
def MMX_MOVQ64mr : MMXI<0x7F, MRMDestMem, (outs), (ins i64mem:$dst, VR64:$src),
                        "movq\t{$src, $dst|$dst, $src}",
                        [(store (x86mmx VR64:$src), addr:$dst)]>;

def MMX_X86movdq2q : SDNode<"X86ISD::MOVDQ2Q", SDTypeProfile<1, 1,
                            [SDTCisVT<0, x86mmx>, SDTCisVT<1, v2i64>]>>;
def MMX_X86movq2dq : SDNode<"X86ISD::MOVQ2DQ", SDTypeProfile<1, 1,
                            [SDTCisVT<0, v2i64>, SDTCisVT<1, x86mmx>]>>;

let SchedRW = [SchedWriteVecMoveLS.XMM.RR] in {
def MMX_MOVDQ2Qrr : MMXSDIi8<0xD6, MRMSrcReg, (outs VR64:$dst),
                             (ins VR128:$src), "movdq2q\t{$src, $dst|$dst, $src}",
                             [(set VR64:$dst,
                               (x86mmx (MMX_X86movdq2q VR128:$src)))]>;

def MMX_MOVQ2DQrr : MMXS2SIi8<0xD6, MRMSrcReg, (outs VR128:$dst),
                              (ins VR64:$src), "movq2dq\t{$src, $dst|$dst, $src}",
                              [(set VR128:$dst,
                                (v2i64 (MMX_X86movq2dq VR64:$src)))]>;

let isCodeGenOnly = 1, hasSideEffects = 1 in {
def MMX_MOVQ2FR64rr: MMXS2SIi8<0xD6, MRMSrcReg, (outs FR64:$dst),
                               (ins VR64:$src), "movq2dq\t{$src, $dst|$dst, $src}",
                               []>;

def MMX_MOVFR642Qrr: MMXSDIi8<0xD6, MRMSrcReg, (outs VR64:$dst),
                              (ins FR64:$src), "movdq2q\t{$src, $dst|$dst, $src}",
                              []>;
}
} // SchedRW

let Predicates = [HasMMX, HasSSE1] in
def MMX_MOVNTQmr  : MMXI<0xE7, MRMDestMem, (outs), (ins i64mem:$dst, VR64:$src),
                         "movntq\t{$src, $dst|$dst, $src}",
                         [(int_x86_mmx_movnt_dq addr:$dst, VR64:$src)]>,
                         Sched<[SchedWriteVecMoveLSNT.MMX.MR]>;

// Arithmetic Instructions
defm MMX_PABSB : SS3I_unop_rm_int_mm<0x1C, "pabsb", int_x86_ssse3_pabs_b,
                                     SchedWriteVecALU.MMX>;
defm MMX_PABSW : SS3I_unop_rm_int_mm<0x1D, "pabsw", int_x86_ssse3_pabs_w,
                                     SchedWriteVecALU.MMX>;
defm MMX_PABSD : SS3I_unop_rm_int_mm<0x1E, "pabsd", int_x86_ssse3_pabs_d,
                                     SchedWriteVecALU.MMX>;
// -- Addition
defm MMX_PADDB : MMXI_binop_rm_int<0xFC, "paddb", int_x86_mmx_padd_b,
                                   SchedWriteVecALU.MMX, 1>;
defm MMX_PADDW : MMXI_binop_rm_int<0xFD, "paddw", int_x86_mmx_padd_w,
                                   SchedWriteVecALU.MMX, 1>;
defm MMX_PADDD : MMXI_binop_rm_int<0xFE, "paddd", int_x86_mmx_padd_d,
                                   SchedWriteVecALU.MMX, 1>;
let Predicates = [HasMMX, HasSSE2] in
defm MMX_PADDQ : MMXI_binop_rm_int<0xD4, "paddq", int_x86_mmx_padd_q,
                                   SchedWriteVecALU.MMX, 1>;
defm MMX_PADDSB  : MMXI_binop_rm_int<0xEC, "paddsb" , int_x86_mmx_padds_b,
                                     SchedWriteVecALU.MMX, 1>;
defm MMX_PADDSW  : MMXI_binop_rm_int<0xED, "paddsw" , int_x86_mmx_padds_w,
                                     SchedWriteVecALU.MMX, 1>;

defm MMX_PADDUSB : MMXI_binop_rm_int<0xDC, "paddusb", int_x86_mmx_paddus_b,
                                   SchedWriteVecALU.MMX, 1>;
defm MMX_PADDUSW : MMXI_binop_rm_int<0xDD, "paddusw", int_x86_mmx_paddus_w,
                                   SchedWriteVecALU.MMX, 1>;

defm MMX_PHADDW  : SS3I_binop_rm_int_mm<0x01, "phaddw", int_x86_ssse3_phadd_w,
                                        SchedWritePHAdd.MMX>;
defm MMX_PHADDD  : SS3I_binop_rm_int_mm<0x02, "phaddd", int_x86_ssse3_phadd_d,
                                        SchedWritePHAdd.MMX>;
defm MMX_PHADDSW : SS3I_binop_rm_int_mm<0x03, "phaddsw",int_x86_ssse3_phadd_sw,
                                        SchedWritePHAdd.MMX>;

// -- Subtraction
defm MMX_PSUBB : MMXI_binop_rm_int<0xF8, "psubb", int_x86_mmx_psub_b,
                                   SchedWriteVecALU.MMX>;
defm MMX_PSUBW : MMXI_binop_rm_int<0xF9, "psubw", int_x86_mmx_psub_w,
                                   SchedWriteVecALU.MMX>;
defm MMX_PSUBD : MMXI_binop_rm_int<0xFA, "psubd", int_x86_mmx_psub_d,
                                   SchedWriteVecALU.MMX>;
let Predicates = [HasMMX, HasSSE2] in
defm MMX_PSUBQ : MMXI_binop_rm_int<0xFB, "psubq", int_x86_mmx_psub_q,
                                   SchedWriteVecALU.MMX>;

defm MMX_PSUBSB  : MMXI_binop_rm_int<0xE8, "psubsb" , int_x86_mmx_psubs_b,
                                   SchedWriteVecALU.MMX>;
defm MMX_PSUBSW  : MMXI_binop_rm_int<0xE9, "psubsw" , int_x86_mmx_psubs_w,
                                   SchedWriteVecALU.MMX>;

defm MMX_PSUBUSB : MMXI_binop_rm_int<0xD8, "psubusb", int_x86_mmx_psubus_b,
                                   SchedWriteVecALU.MMX>;
defm MMX_PSUBUSW : MMXI_binop_rm_int<0xD9, "psubusw", int_x86_mmx_psubus_w,
                                   SchedWriteVecALU.MMX>;

defm MMX_PHSUBW  : SS3I_binop_rm_int_mm<0x05, "phsubw", int_x86_ssse3_phsub_w,
                                        SchedWritePHAdd.MMX>;
defm MMX_PHSUBD  : SS3I_binop_rm_int_mm<0x06, "phsubd", int_x86_ssse3_phsub_d,
                                        SchedWritePHAdd.MMX>;
defm MMX_PHSUBSW : SS3I_binop_rm_int_mm<0x07, "phsubsw",int_x86_ssse3_phsub_sw,
                                        SchedWritePHAdd.MMX>;

// -- Multiplication
defm MMX_PMULLW  : MMXI_binop_rm_int<0xD5, "pmullw", int_x86_mmx_pmull_w,
                                     SchedWriteVecIMul.MMX, 1>;

defm MMX_PMULHW  : MMXI_binop_rm_int<0xE5, "pmulhw",  int_x86_mmx_pmulh_w,
                                     SchedWriteVecIMul.MMX, 1>;
let Predicates = [HasMMX, HasSSE1] in
defm MMX_PMULHUW : MMXI_binop_rm_int<0xE4, "pmulhuw", int_x86_mmx_pmulhu_w,
                                     SchedWriteVecIMul.MMX, 1>;
let Predicates = [HasMMX, HasSSE2] in
defm MMX_PMULUDQ : MMXI_binop_rm_int<0xF4, "pmuludq", int_x86_mmx_pmulu_dq,
                                     SchedWriteVecIMul.MMX, 1>;
defm MMX_PMULHRSW : SS3I_binop_rm_int_mm<0x0B, "pmulhrsw",
                                     int_x86_ssse3_pmul_hr_sw,
                                     SchedWriteVecIMul.MMX, 1>;

// -- Miscellanea
defm MMX_PMADDWD : MMXI_binop_rm_int<0xF5, "pmaddwd", int_x86_mmx_pmadd_wd,
                                     SchedWriteVecIMul.MMX, 1>;

defm MMX_PMADDUBSW : SS3I_binop_rm_int_mm<0x04, "pmaddubsw",
                                          int_x86_ssse3_pmadd_ub_sw,
                                          SchedWriteVecIMul.MMX>;
let Predicates = [HasMMX, HasSSE1] in {
defm MMX_PAVGB   : MMXI_binop_rm_int<0xE0, "pavgb", int_x86_mmx_pavg_b,
                                     SchedWriteVecALU.MMX, 1>;
defm MMX_PAVGW   : MMXI_binop_rm_int<0xE3, "pavgw", int_x86_mmx_pavg_w,
                                     SchedWriteVecALU.MMX, 1>;

defm MMX_PMINUB  : MMXI_binop_rm_int<0xDA, "pminub", int_x86_mmx_pminu_b,
                                     SchedWriteVecALU.MMX, 1>;
defm MMX_PMINSW  : MMXI_binop_rm_int<0xEA, "pminsw", int_x86_mmx_pmins_w,
                                     SchedWriteVecALU.MMX, 1>;

defm MMX_PMAXUB  : MMXI_binop_rm_int<0xDE, "pmaxub", int_x86_mmx_pmaxu_b,
                                     SchedWriteVecALU.MMX, 1>;
defm MMX_PMAXSW  : MMXI_binop_rm_int<0xEE, "pmaxsw", int_x86_mmx_pmaxs_w,
                                     SchedWriteVecALU.MMX, 1>;

defm MMX_PSADBW  : MMXI_binop_rm_int<0xF6, "psadbw", int_x86_mmx_psad_bw,
                                     SchedWritePSADBW.MMX, 1>;
}

defm MMX_PSIGNB :  SS3I_binop_rm_int_mm<0x08, "psignb", int_x86_ssse3_psign_b,
                                        SchedWriteVecALU.MMX>;
defm MMX_PSIGNW :  SS3I_binop_rm_int_mm<0x09, "psignw", int_x86_ssse3_psign_w,
                                        SchedWriteVecALU.MMX>;
defm MMX_PSIGND :  SS3I_binop_rm_int_mm<0x0A, "psignd", int_x86_ssse3_psign_d,
                                        SchedWriteVecALU.MMX>;
let Constraints = "$src1 = $dst" in
  defm MMX_PALIGNR : ssse3_palign_mm<"palignr", int_x86_mmx_palignr_b,
                                     SchedWriteShuffle.MMX>;

// Logical Instructions
defm MMX_PAND : MMXI_binop_rm_int<0xDB, "pand", int_x86_mmx_pand,
                                  SchedWriteVecLogic.MMX, 1>;
defm MMX_POR  : MMXI_binop_rm_int<0xEB, "por" , int_x86_mmx_por,
                                  SchedWriteVecLogic.MMX, 1>;
defm MMX_PXOR : MMXI_binop_rm_int<0xEF, "pxor", int_x86_mmx_pxor,
                                  SchedWriteVecLogic.MMX, 1>;
defm MMX_PANDN : MMXI_binop_rm_int<0xDF, "pandn", int_x86_mmx_pandn,
                                   SchedWriteVecLogic.MMX>;

// Shift Instructions
defm MMX_PSRLW : MMXI_binop_rmi_int<0xD1, 0x71, MRM2r, "psrlw",
                                    int_x86_mmx_psrl_w, int_x86_mmx_psrli_w,
                                    SchedWriteVecShift.MMX,
                                    SchedWriteVecShiftImm.MMX>;
defm MMX_PSRLD : MMXI_binop_rmi_int<0xD2, 0x72, MRM2r, "psrld",
                                    int_x86_mmx_psrl_d, int_x86_mmx_psrli_d,
                                    SchedWriteVecShift.MMX,
                                    SchedWriteVecShiftImm.MMX>;
defm MMX_PSRLQ : MMXI_binop_rmi_int<0xD3, 0x73, MRM2r, "psrlq",
                                    int_x86_mmx_psrl_q, int_x86_mmx_psrli_q,
                                    SchedWriteVecShift.MMX,
                                    SchedWriteVecShiftImm.MMX>;

defm MMX_PSLLW : MMXI_binop_rmi_int<0xF1, 0x71, MRM6r, "psllw",
                                    int_x86_mmx_psll_w, int_x86_mmx_pslli_w,
                                    SchedWriteVecShift.MMX,
                                    SchedWriteVecShiftImm.MMX>;
defm MMX_PSLLD : MMXI_binop_rmi_int<0xF2, 0x72, MRM6r, "pslld",
                                    int_x86_mmx_psll_d, int_x86_mmx_pslli_d,
                                    SchedWriteVecShift.MMX,
                                    SchedWriteVecShiftImm.MMX>;
defm MMX_PSLLQ : MMXI_binop_rmi_int<0xF3, 0x73, MRM6r, "psllq",
                                    int_x86_mmx_psll_q, int_x86_mmx_pslli_q,
                                    SchedWriteVecShift.MMX,
                                    SchedWriteVecShiftImm.MMX>;

defm MMX_PSRAW : MMXI_binop_rmi_int<0xE1, 0x71, MRM4r, "psraw",
                                    int_x86_mmx_psra_w, int_x86_mmx_psrai_w,
                                    SchedWriteVecShift.MMX,
                                    SchedWriteVecShiftImm.MMX>;
defm MMX_PSRAD : MMXI_binop_rmi_int<0xE2, 0x72, MRM4r, "psrad",
                                    int_x86_mmx_psra_d, int_x86_mmx_psrai_d,
                                    SchedWriteVecShift.MMX,
                                    SchedWriteVecShiftImm.MMX>;

// Comparison Instructions
defm MMX_PCMPEQB : MMXI_binop_rm_int<0x74, "pcmpeqb", int_x86_mmx_pcmpeq_b,
                                     SchedWriteVecALU.MMX>;
defm MMX_PCMPEQW : MMXI_binop_rm_int<0x75, "pcmpeqw", int_x86_mmx_pcmpeq_w,
                                     SchedWriteVecALU.MMX>;
defm MMX_PCMPEQD : MMXI_binop_rm_int<0x76, "pcmpeqd", int_x86_mmx_pcmpeq_d,
                                     SchedWriteVecALU.MMX>;

defm MMX_PCMPGTB : MMXI_binop_rm_int<0x64, "pcmpgtb", int_x86_mmx_pcmpgt_b,
                                     SchedWriteVecALU.MMX>;
defm MMX_PCMPGTW : MMXI_binop_rm_int<0x65, "pcmpgtw", int_x86_mmx_pcmpgt_w,
                                     SchedWriteVecALU.MMX>;
defm MMX_PCMPGTD : MMXI_binop_rm_int<0x66, "pcmpgtd", int_x86_mmx_pcmpgt_d,
                                     SchedWriteVecALU.MMX>;

// -- Unpack Instructions
defm MMX_PUNPCKHBW : MMXI_binop_rm_int<0x68, "punpckhbw",
                                       int_x86_mmx_punpckhbw,
                                       SchedWriteShuffle.MMX>;
defm MMX_PUNPCKHWD : MMXI_binop_rm_int<0x69, "punpckhwd",
                                       int_x86_mmx_punpckhwd,
                                       SchedWriteShuffle.MMX>;
defm MMX_PUNPCKHDQ : MMXI_binop_rm_int<0x6A, "punpckhdq",
                                       int_x86_mmx_punpckhdq,
                                       SchedWriteShuffle.MMX>;
defm MMX_PUNPCKLBW : MMXI_binop_rm_int<0x60, "punpcklbw",
                                       int_x86_mmx_punpcklbw,
                                       SchedWriteShuffle.MMX,
                                       0, i32mem>;
defm MMX_PUNPCKLWD : MMXI_binop_rm_int<0x61, "punpcklwd",
                                       int_x86_mmx_punpcklwd,
                                       SchedWriteShuffle.MMX,
                                       0, i32mem>;
defm MMX_PUNPCKLDQ : MMXI_binop_rm_int<0x62, "punpckldq",
                                       int_x86_mmx_punpckldq,
                                       SchedWriteShuffle.MMX,
                                       0, i32mem>;

// -- Pack Instructions
defm MMX_PACKSSWB : MMXI_binop_rm_int<0x63, "packsswb", int_x86_mmx_packsswb,
                                      SchedWriteShuffle.MMX>;
defm MMX_PACKSSDW : MMXI_binop_rm_int<0x6B, "packssdw", int_x86_mmx_packssdw,
                                      SchedWriteShuffle.MMX>;
defm MMX_PACKUSWB : MMXI_binop_rm_int<0x67, "packuswb", int_x86_mmx_packuswb,
                                      SchedWriteShuffle.MMX>;

// -- Shuffle Instructions
defm MMX_PSHUFB : SS3I_binop_rm_int_mm<0x00, "pshufb", int_x86_ssse3_pshuf_b,
                                       SchedWriteVarShuffle.MMX>;

let Predicates = [HasMMX, HasSSE1] in {
def MMX_PSHUFWri : MMXIi8<0x70, MRMSrcReg,
                          (outs VR64:$dst), (ins VR64:$src1, u8imm:$src2),
                          "pshufw\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                          [(set VR64:$dst,
                             (int_x86_sse_pshuf_w VR64:$src1, timm:$src2))]>,
                          Sched<[SchedWriteShuffle.MMX]>;
def MMX_PSHUFWmi : MMXIi8<0x70, MRMSrcMem,
                          (outs VR64:$dst), (ins i64mem:$src1, u8imm:$src2),
                          "pshufw\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                          [(set VR64:$dst,
                             (int_x86_sse_pshuf_w (load_mmx addr:$src1),
                                                   timm:$src2))]>,
                          Sched<[SchedWriteShuffle.MMX.Folded]>;
}

// -- Conversion Instructions
defm MMX_CVTPS2PI : sse12_cvt_pint<0x2D, VR128, VR64, int_x86_sse_cvtps2pi,
                      f64mem, load, "cvtps2pi\t{$src, $dst|$dst, $src}",
                      WriteCvtPS2I, SSEPackedSingle>, TB, SIMD_EXC;
defm MMX_CVTPD2PI : sse12_cvt_pint<0x2D, VR128, VR64, int_x86_sse_cvtpd2pi,
                      f128mem, memop, "cvtpd2pi\t{$src, $dst|$dst, $src}",
                      WriteCvtPD2I, SSEPackedDouble>, TB, PD, SIMD_EXC;
defm MMX_CVTTPS2PI : sse12_cvt_pint<0x2C, VR128, VR64, int_x86_sse_cvttps2pi,
                       f64mem, load, "cvttps2pi\t{$src, $dst|$dst, $src}",
                       WriteCvtPS2I, SSEPackedSingle>, TB, SIMD_EXC;
defm MMX_CVTTPD2PI : sse12_cvt_pint<0x2C, VR128, VR64, int_x86_sse_cvttpd2pi,
                       f128mem, memop, "cvttpd2pi\t{$src, $dst|$dst, $src}",
                       WriteCvtPD2I, SSEPackedDouble>, TB, PD, SIMD_EXC;
defm MMX_CVTPI2PD : sse12_cvt_pint<0x2A, VR64, VR128, int_x86_sse_cvtpi2pd,
                         i64mem, load, "cvtpi2pd\t{$src, $dst|$dst, $src}",
                         WriteCvtI2PD, SSEPackedDouble>, TB, PD;
let Constraints = "$src1 = $dst" in {
  defm MMX_CVTPI2PS : sse12_cvt_pint_3addr<0x2A, VR64, VR128,
                         int_x86_sse_cvtpi2ps,
                         i64mem, load, "cvtpi2ps\t{$src2, $dst|$dst, $src2}",
                         SSEPackedSingle>, TB, SIMD_EXC;
}

// Extract / Insert
let Predicates = [HasMMX, HasSSE1] in
def MMX_PEXTRWrr: MMXIi8<0xC5, MRMSrcReg,
                     (outs GR32orGR64:$dst), (ins VR64:$src1, i32u8imm:$src2),
                     "pextrw\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                     [(set GR32orGR64:$dst, (int_x86_mmx_pextr_w VR64:$src1,
                                             timm:$src2))]>,
                     Sched<[WriteVecExtract]>;
let Constraints = "$src1 = $dst" in {
let Predicates = [HasMMX, HasSSE1] in {
  def MMX_PINSRWrr : MMXIi8<0xC4, MRMSrcReg,
                    (outs VR64:$dst),
                    (ins VR64:$src1, GR32orGR64:$src2, i32u8imm:$src3),
                    "pinsrw\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                    [(set VR64:$dst, (int_x86_mmx_pinsr_w VR64:$src1,
                                      GR32orGR64:$src2, timm:$src3))]>,
                    Sched<[WriteVecInsert, ReadDefault, ReadInt2Fpu]>;

  def MMX_PINSRWrm : MMXIi8<0xC4, MRMSrcMem,
                   (outs VR64:$dst),
                   (ins VR64:$src1, i16mem:$src2, i32u8imm:$src3),
                   "pinsrw\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                   [(set VR64:$dst, (int_x86_mmx_pinsr_w VR64:$src1,
                                       (i32 (anyext (loadi16 addr:$src2))),
                                     timm:$src3))]>,
                   Sched<[WriteVecInsert.Folded, WriteVecInsert.ReadAfterFold]>;
}
}

// Mask creation
let Predicates = [HasMMX, HasSSE1] in
def MMX_PMOVMSKBrr : MMXI<0xD7, MRMSrcReg, (outs GR32orGR64:$dst),
                          (ins VR64:$src),
                          "pmovmskb\t{$src, $dst|$dst, $src}",
                          [(set GR32orGR64:$dst,
                                (int_x86_mmx_pmovmskb VR64:$src))]>,
                          Sched<[WriteMMXMOVMSK]>;

// Misc.
let SchedRW = [SchedWriteShuffle.MMX] in {
let Uses = [EDI], Predicates = [HasMMX, HasSSE1,Not64BitMode] in
def MMX_MASKMOVQ : MMXI<0xF7, MRMSrcReg, (outs), (ins VR64:$src, VR64:$mask),
                        "maskmovq\t{$mask, $src|$src, $mask}",
                        [(int_x86_mmx_maskmovq VR64:$src, VR64:$mask, EDI)]>;
let Uses = [RDI], Predicates = [HasMMX, HasSSE1,In64BitMode] in
def MMX_MASKMOVQ64: MMXI<0xF7, MRMSrcReg, (outs), (ins VR64:$src, VR64:$mask),
                         "maskmovq\t{$mask, $src|$src, $mask}",
                         [(int_x86_mmx_maskmovq VR64:$src, VR64:$mask, RDI)]>;
}

// 64-bit bit convert.
let Predicates = [HasMMX, HasSSE2] in {
def : Pat<(f64 (bitconvert (x86mmx VR64:$src))),
          (MMX_MOVQ2FR64rr VR64:$src)>;
def : Pat<(x86mmx (bitconvert (f64 FR64:$src))),
          (MMX_MOVFR642Qrr FR64:$src)>;
def : Pat<(x86mmx (MMX_X86movdq2q
                   (bc_v2i64 (v4i32 (X86cvtp2Int (v4f32 VR128:$src)))))),
          (MMX_CVTPS2PIrr VR128:$src)>;
def : Pat<(x86mmx (MMX_X86movdq2q
                   (bc_v2i64 (v4i32 (X86cvttp2si (v4f32 VR128:$src)))))),
          (MMX_CVTTPS2PIrr VR128:$src)>;
def : Pat<(x86mmx (MMX_X86movdq2q
                   (bc_v2i64 (v4i32 (X86cvtp2Int (v2f64 VR128:$src)))))),
          (MMX_CVTPD2PIrr VR128:$src)>;
def : Pat<(x86mmx (MMX_X86movdq2q
                   (bc_v2i64 (v4i32 (X86cvttp2si (v2f64 VR128:$src)))))),
          (MMX_CVTTPD2PIrr VR128:$src)>;
}
