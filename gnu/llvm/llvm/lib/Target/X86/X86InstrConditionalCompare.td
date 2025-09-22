//===-- X86InstrConditionalCompare.td - Conditional Compare --*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the X86 conditional compare instructions.
//
//===----------------------------------------------------------------------===//

class BinCondOp<bits<8> o, Format f, X86TypeInfo t, DAGOperand op1, DAGOperand op2, string m>
  : ITy<o, f, t, (outs), (ins op1:$src1, op2:$src2, cflags:$dcf, ccode:$cond),
        m#"${cond}", "$dcf\t{$src2, $src1|$src1, $src2}" , []>, T_MAP4, EVEX, Requires<[In64BitMode]> {
  let isCodeGenOnly = 1;
  let ForceDisassemble = 1;
  let Uses = [EFLAGS];
  let Defs = [EFLAGS];
  let hasTwoConditionalOps = 1;
  let ImmT = !if(!eq(op2, i16i8imm), Imm8,
             !if(!eq(op2, i32i8imm), Imm8,
             !if(!eq(op2, i64i8imm), Imm8,
             !if(!eq(op2, i8imm), Imm8,
             !if(!eq(op2, i16imm), Imm16,
             !if(!eq(op2, i32imm), Imm32,
             !if(!eq(op2, i64i32imm), Imm32S, NoImm)))))));
}

class Ccmp<bits<8> o, Format f, X86TypeInfo t, DAGOperand op1, DAGOperand op2>:
  BinCondOp<o, f, t, op1, op2, "ccmp">;

class Ctest<bits<8> o, Format f, X86TypeInfo t, DAGOperand op1, DAGOperand op2>:
  BinCondOp<o, f, t, op1, op2, "ctest">;

//===----------------------------------------------------------------------===//
// CCMP Instructions
//
let SchedRW = [WriteALU] in {
  def CCMP8rr : Ccmp<0x38, MRMDestReg, Xi8,  GR8,  GR8>;
  def CCMP16rr: Ccmp<0x39, MRMDestReg, Xi16, GR16, GR16>, PD;
  def CCMP32rr: Ccmp<0x39, MRMDestReg, Xi32, GR32, GR32>;
  def CCMP64rr: Ccmp<0x39, MRMDestReg, Xi64, GR64, GR64>;
  def CCMP8rr_REV : Ccmp<0x3a, MRMSrcReg, Xi8,  GR8,  GR8>;
  def CCMP16rr_REV: Ccmp<0x3b, MRMSrcReg, Xi16, GR16, GR16>, PD;
  def CCMP32rr_REV: Ccmp<0x3b, MRMSrcReg, Xi32, GR32, GR32>;
  def CCMP64rr_REV: Ccmp<0x3b, MRMSrcReg, Xi64, GR64, GR64>;
  def CCMP16ri8: Ccmp<0x83, MRM7r, Xi16, GR16, i16i8imm>, PD;
  def CCMP32ri8: Ccmp<0x83, MRM7r, Xi32, GR32, i32i8imm>;
  def CCMP64ri8: Ccmp<0x83, MRM7r, Xi64, GR64, i64i8imm>;

  def CCMP8ri : Ccmp<0x80, MRM7r, Xi8,   GR8, i8imm>;
  def CCMP16ri: Ccmp<0x81, MRM7r, Xi16, GR16, i16imm>, PD;
  def CCMP32ri: Ccmp<0x81, MRM7r, Xi32, GR32, i32imm>;
  def CCMP64ri32: Ccmp<0x81, MRM7r, Xi64, GR64, i64i32imm>;
}

let mayLoad = 1 in {
  let SchedRW = [WriteALU.Folded] in {
    def CCMP16mi8: Ccmp<0x83, MRM7m, Xi16, i16mem, i16i8imm>, PD;
    def CCMP32mi8: Ccmp<0x83, MRM7m, Xi32, i32mem, i32i8imm>;
    def CCMP64mi8: Ccmp<0x83, MRM7m, Xi64, i64mem, i64i8imm>;
    def CCMP8mi : Ccmp<0x80, MRM7m, Xi8,   i8mem, i8imm>;
    def CCMP16mi: Ccmp<0x81, MRM7m, Xi16, i16mem, i16imm>, PD;
    def CCMP32mi: Ccmp<0x81, MRM7m, Xi32, i32mem, i32imm>;
    def CCMP64mi32: Ccmp<0x81, MRM7m, Xi64, i64mem, i64i32imm>;
  }
  let SchedRW = [WriteALU.Folded, WriteALU.ReadAfterFold] in {
    def CCMP8rm : Ccmp<0x3a, MRMSrcMem, Xi8,  GR8,  i8mem>;
    def CCMP16rm: Ccmp<0x3b, MRMSrcMem, Xi16, GR16, i16mem>, PD;
    def CCMP32rm: Ccmp<0x3b, MRMSrcMem, Xi32, GR32, i32mem>;
    def CCMP64rm: Ccmp<0x3b, MRMSrcMem, Xi64, GR64, i64mem>;

    def CCMP8mr : Ccmp<0x38, MRMDestMem, Xi8,  i8mem,  GR8>;
    def CCMP16mr: Ccmp<0x39, MRMDestMem, Xi16, i16mem, GR16>, PD;
    def CCMP32mr: Ccmp<0x39, MRMDestMem, Xi32, i32mem, GR32>;
    def CCMP64mr: Ccmp<0x39, MRMDestMem, Xi64, i64mem, GR64>;
  }
}

def : Pat<(X86ccmp GR8:$src1, GR8:$src2, timm:$dcf, timm:$cond, EFLAGS),
          (CCMP8rr GR8:$src1, GR8:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ccmp GR16:$src1, GR16:$src2, timm:$dcf, timm:$cond, EFLAGS),
          (CCMP16rr GR16:$src1, GR16:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ccmp GR32:$src1, GR32:$src2, timm:$dcf, timm:$cond, EFLAGS),
          (CCMP32rr GR32:$src1, GR32:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ccmp GR64:$src1, GR64:$src2, timm:$dcf, timm:$cond, EFLAGS),
          (CCMP64rr GR64:$src1, GR64:$src2, timm:$dcf, timm:$cond)>;

def : Pat<(X86ccmp GR8:$src1, (i8 imm:$src2), timm:$dcf, timm:$cond, EFLAGS),
          (CCMP8ri GR8:$src1, imm:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ccmp GR16:$src1, (i16 imm:$src2), timm:$dcf, timm:$cond, EFLAGS),
          (CCMP16ri GR16:$src1, imm:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ccmp GR32:$src1, (i32 imm:$src2), timm:$dcf, timm:$cond, EFLAGS),
          (CCMP32ri GR32:$src1, imm:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ccmp GR64:$src1, i64immSExt32_su:$src2, timm:$dcf, timm:$cond, EFLAGS),
          (CCMP64ri32 GR64:$src1, i64immSExt32_su:$src2, timm:$dcf, timm:$cond)>;

def : Pat<(X86ccmp GR8:$src1, (loadi8 addr:$src2), timm:$dcf, timm:$cond, EFLAGS),
          (CCMP8rm GR8:$src1, addr:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ccmp GR16:$src1, (loadi16 addr:$src2), timm:$dcf, timm:$cond, EFLAGS),
          (CCMP16rm GR16:$src1, addr:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ccmp GR32:$src1, (loadi32 addr:$src2), timm:$dcf, timm:$cond, EFLAGS),
          (CCMP32rm GR32:$src1, addr:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ccmp GR64:$src1, (loadi64 addr:$src2), timm:$dcf, timm:$cond, EFLAGS),
          (CCMP64rm GR64:$src1, addr:$src2, timm:$dcf, timm:$cond)>;


//===----------------------------------------------------------------------===//
// CTEST Instructions
//
let SchedRW = [WriteALU] in {
  let isCommutable = 1 in {
    def CTEST8rr : Ctest<0x84, MRMDestReg, Xi8,  GR8,  GR8>;
    def CTEST16rr: Ctest<0x85, MRMDestReg, Xi16, GR16, GR16>, PD;
    def CTEST32rr: Ctest<0x85, MRMDestReg, Xi32, GR32, GR32>;
    def CTEST64rr: Ctest<0x85, MRMDestReg, Xi64, GR64, GR64>;
  }
  def CTEST8ri : Ctest<0xF6, MRM0r, Xi8,   GR8, i8imm>;
  def CTEST16ri: Ctest<0xF7, MRM0r, Xi16, GR16, i16imm>, PD;
  def CTEST32ri: Ctest<0xF7, MRM0r, Xi32, GR32, i32imm>;
  def CTEST64ri32: Ctest<0xF7, MRM0r, Xi64, GR64, i64i32imm>;
}

let mayLoad = 1 in {
  let SchedRW = [WriteALU.Folded] in {
    def CTEST8mi : Ctest<0xF6, MRM0m, Xi8,   i8mem, i8imm>;
    def CTEST16mi: Ctest<0xF7, MRM0m, Xi16, i16mem, i16imm>, PD;
    def CTEST32mi: Ctest<0xF7, MRM0m, Xi32, i32mem, i32imm>;
    def CTEST64mi32: Ctest<0xF7, MRM0m, Xi64, i64mem, i64i32imm>;
  }
  let SchedRW = [WriteALU.Folded, WriteALU.ReadAfterFold] in {
    def CTEST8mr : Ctest<0x84, MRMDestMem, Xi8,  i8mem,  GR8>;
    def CTEST16mr: Ctest<0x85, MRMDestMem, Xi16, i16mem, GR16>, PD;
    def CTEST32mr: Ctest<0x85, MRMDestMem, Xi32, i32mem, GR32>;
    def CTEST64mr: Ctest<0x85, MRMDestMem, Xi64, i64mem, GR64>;
  }
}

def : Pat<(X86ctest GR8:$src1, GR8:$src2, timm:$dcf, timm:$cond, EFLAGS),
          (CTEST8rr GR8:$src1, GR8:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ctest GR16:$src1, GR16:$src2, timm:$dcf, timm:$cond, EFLAGS),
          (CTEST16rr GR16:$src1, GR16:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ctest GR32:$src1, GR32:$src2, timm:$dcf, timm:$cond, EFLAGS),
          (CTEST32rr GR32:$src1, GR32:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ctest GR64:$src1, GR64:$src2, timm:$dcf, timm:$cond, EFLAGS),
          (CTEST64rr GR64:$src1, GR64:$src2, timm:$dcf, timm:$cond)>;

def : Pat<(X86ctestpat GR8:$src1, imm:$src2, timm:$dcf, timm:$cond),
          (CTEST8ri GR8:$src1, imm:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ctestpat GR16:$src1, imm:$src2, timm:$dcf, timm:$cond),
          (CTEST16ri GR16:$src1, imm:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ctestpat GR32:$src1, imm:$src2, timm:$dcf, timm:$cond),
          (CTEST32ri GR32:$src1, imm:$src2, timm:$dcf, timm:$cond)>;
def : Pat<(X86ctestpat GR64:$src1, i64immSExt32_su:$src2, timm:$dcf, timm:$cond),
          (CTEST64ri32 GR64:$src1, i64immSExt32_su:$src2, timm:$dcf, timm:$cond)>;
