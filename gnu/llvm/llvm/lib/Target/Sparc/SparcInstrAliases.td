//===-- SparcInstrAliases.td - Instruction Aliases for Sparc Target -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains instruction aliases for Sparc.
//===----------------------------------------------------------------------===//

// Instruction aliases for conditional moves.

// mov<cond> <ccreg> rs2, rd
multiclass intcond_mov_alias<string cond, int condVal, string ccreg,
                          Instruction movrr, Instruction movri,
                          Instruction fmovs, Instruction fmovd> {

  // mov<cond> (%icc|%xcc), rs2, rd
  def : InstAlias<!strconcat(!strconcat(!strconcat("mov", cond), ccreg),
                             ", $rs2, $rd"),
                  (movrr IntRegs:$rd, IntRegs:$rs2, condVal)>;

  // mov<cond> (%icc|%xcc), simm11, rd
  def : InstAlias<!strconcat(!strconcat(!strconcat("mov", cond), ccreg),
                             ", $simm11, $rd"),
                  (movri IntRegs:$rd, i32imm:$simm11, condVal)>;

  // fmovs<cond> (%icc|%xcc), $rs2, $rd
  def : InstAlias<!strconcat(!strconcat(!strconcat("fmovs", cond), ccreg),
                             ", $rs2, $rd"),
                  (fmovs FPRegs:$rd, FPRegs:$rs2, condVal)>;

  // fmovd<cond> (%icc|%xcc), $rs2, $rd
  def : InstAlias<!strconcat(!strconcat(!strconcat("fmovd", cond), ccreg),
                             ", $rs2, $rd"),
                  (fmovd DFPRegs:$rd, DFPRegs:$rs2, condVal)>;
}

// mov<cond> <ccreg> rs2, rd
multiclass fpcond_mov_alias<string cond, int condVal,
                           Instruction movrr, Instruction movri,
                           Instruction fmovs, Instruction fmovd> {

  // mov<cond> %fcc[0-3], rs2, rd
  def : InstAlias<!strconcat(!strconcat("mov", cond), " $cc, $rs2, $rd"),
                  (movrr IntRegs:$rd, FCCRegs:$cc, IntRegs:$rs2, condVal)>;

  // mov<cond> %fcc[0-3], simm11, rd
  def : InstAlias<!strconcat(!strconcat("mov", cond), " $cc, $simm11, $rd"),
                  (movri IntRegs:$rd, FCCRegs:$cc, i32imm:$simm11, condVal)>;

  // fmovs<cond> %fcc[0-3], $rs2, $rd
  def : InstAlias<!strconcat(!strconcat("fmovs", cond), " $cc, $rs2, $rd"),
                  (fmovs FPRegs:$rd, FCCRegs:$cc, FPRegs:$rs2, condVal)>;

  // fmovd<cond> %fcc[0-3], $rs2, $rd
  def : InstAlias<!strconcat(!strconcat("fmovd", cond), " $cc, $rs2, $rd"),
                  (fmovd DFPRegs:$rd, FCCRegs:$cc, DFPRegs:$rs2, condVal)>;
}

// movr<cond> rs1, rs2, rd
multiclass regcond_mov_alias<string rcond, int condVal,
                          Instruction movrrr, Instruction movrri,
                          Instruction fmovrs, Instruction fmovrd,
                          Instruction fmovrq> {

  // movr<cond> $rs1, $rs2, $rd
  def : InstAlias<!strconcat(!strconcat("movr", rcond), " $rs1, $rs2, $rd"),
                  (movrrr IntRegs:$rd, I64Regs:$rs1, IntRegs:$rs2, condVal)>;

  // movr<cond> $rs1, $simm10, $rd
  def : InstAlias<!strconcat(!strconcat("movr", rcond), " $rs1, $simm10, $rd"),
                  (movrri IntRegs:$rd, I64Regs:$rs1, i32imm:$simm10, condVal)>;

  // fmovrs<cond> $rs1, $rs2, $rd
  def : InstAlias<!strconcat(!strconcat("fmovrs", rcond), " $rs1, $rs2, $rd"),
                  (fmovrs FPRegs:$rd, I64Regs:$rs1, FPRegs:$rs2, condVal)>;

  // fmovrd<cond> $rs1, $rs2, $rd
  def : InstAlias<!strconcat(!strconcat("fmovrd", rcond), " $rs1, $rs2, $rd"),
                  (fmovrd DFPRegs:$rd, I64Regs:$rs1, DFPRegs:$rs2, condVal)>;

  // fmovrq<cond> $rs1, $rs2, $rd
  let Predicates = [HasHardQuad] in
  def : InstAlias<!strconcat(!strconcat("fmovrq", rcond), " $rs1, $rs2, $rd"),
                  (fmovrq QFPRegs:$rd, I64Regs:$rs1, QFPRegs:$rs2, condVal)>;
}

// Instruction aliases for integer conditional branches and moves.
multiclass int_cond_alias<string cond, int condVal> {

  // b<cond> $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), " $imm"),
                  (BCOND brtarget:$imm, condVal)>;

  // b<cond>,a $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), ",a $imm"),
                  (BCONDA brtarget:$imm, condVal)>;

  // b<cond> %icc, $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), " %icc, $imm"),
                  (BPICC brtarget:$imm, condVal)>, Requires<[HasV9]>;

  // b<cond>,pt %icc, $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), ",pt %icc, $imm"),
                  (BPICC brtarget:$imm, condVal)>, Requires<[HasV9]>;

  // b<cond>,a %icc, $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), ",a %icc, $imm"),
                  (BPICCA brtarget:$imm, condVal)>, Requires<[HasV9]>;

  // b<cond>,a,pt %icc, $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), ",a,pt %icc, $imm"),
                  (BPICCA brtarget:$imm, condVal)>, Requires<[HasV9]>;

  // b<cond>,pn %icc, $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), ",pn %icc, $imm"),
                  (BPICCNT brtarget:$imm, condVal)>, Requires<[HasV9]>;

  // b<cond>,a,pn %icc, $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), ",a,pn %icc, $imm"),
                  (BPICCANT brtarget:$imm, condVal)>, Requires<[HasV9]>;

  // b<cond> %xcc, $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), " %xcc, $imm"),
                  (BPXCC brtarget:$imm, condVal)>, Requires<[Is64Bit]>;

  // b<cond>,pt %xcc, $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), ",pt %xcc, $imm"),
                  (BPXCC brtarget:$imm, condVal)>, Requires<[Is64Bit]>;

  // b<cond>,a %xcc, $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), ",a %xcc, $imm"),
                  (BPXCCA brtarget:$imm, condVal)>, Requires<[Is64Bit]>;

  // b<cond>,a,pt %xcc, $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), ",a,pt %xcc, $imm"),
                  (BPXCCA brtarget:$imm, condVal)>, Requires<[Is64Bit]>;

  // b<cond>,pn %xcc, $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), ",pn %xcc, $imm"),
                  (BPXCCNT brtarget:$imm, condVal)>, Requires<[Is64Bit]>;

  // b<cond>,a,pn %xcc, $imm
  def : InstAlias<!strconcat(!strconcat("b", cond), ",a,pn %xcc, $imm"),
                  (BPXCCANT brtarget:$imm, condVal)>, Requires<[Is64Bit]>;


  defm : intcond_mov_alias<cond, condVal, " %icc",
                            MOVICCrr, MOVICCri,
                            FMOVS_ICC, FMOVD_ICC>, Requires<[HasV9]>;

  defm : intcond_mov_alias<cond, condVal, " %xcc",
                            MOVXCCrr, MOVXCCri,
                            FMOVS_XCC, FMOVD_XCC>, Requires<[Is64Bit]>;

  // fmovq<cond> (%icc|%xcc), $rs2, $rd
  def : InstAlias<!strconcat(!strconcat("fmovq", cond), " %icc, $rs2, $rd"),
                  (FMOVQ_ICC QFPRegs:$rd, QFPRegs:$rs2, condVal)>,
                  Requires<[HasV9, HasHardQuad]>;
  def : InstAlias<!strconcat(!strconcat("fmovq", cond), " %xcc, $rs2, $rd"),
                  (FMOVQ_XCC QFPRegs:$rd, QFPRegs:$rs2, condVal)>,
                  Requires<[Is64Bit, HasHardQuad]>;

  // t<cond> %icc,  rs => t<cond> %icc, G0 + rs
  def : InstAlias<!strconcat(!strconcat("t", cond), " %icc, $rs2"),
                  (TICCrr G0, IntRegs:$rs2, condVal)>,
                  Requires<[HasV9]>;
  // t<cond> %icc, rs1 + rs2
  def : InstAlias<!strconcat(!strconcat("t", cond), " %icc, $rs1 + $rs2"),
                  (TICCrr IntRegs:$rs1, IntRegs:$rs2, condVal)>,
                  Requires<[HasV9]>;


  // t<cond> %xcc, rs => t<cond> %xcc, G0 + rs
  def : InstAlias<!strconcat(!strconcat("t", cond), " %xcc, $rs2"),
                  (TXCCrr G0, IntRegs:$rs2, condVal)>,
                  Requires<[HasV9]>;
  // t<cond> %xcc, rs1 + rs2
  def : InstAlias<!strconcat(!strconcat("t", cond), " %xcc, $rs1 + $rs2"),
                  (TXCCrr IntRegs:$rs1, IntRegs:$rs2, condVal)>,
                  Requires<[HasV9]>;


  // t<cond> rs=> t<cond> %icc,  G0 + rs2
  //def : InstAlias<!strconcat(!strconcat("t", cond), " $rs2"),
  //                (TICCrr G0, IntRegs:$rs2, condVal)>,
  //                Requires<[HasV9]>;

  // t<cond> rs1 + rs2 => t<cond> %icc, rs1 + rs2
  //def : InstAlias<!strconcat(!strconcat("t", cond), " $rs1 + $rs2"),
  //                (TICCrr IntRegs:$rs1, IntRegs:$rs2, condVal)>,
  //                Requires<[HasV9]>;

  // t<cond> %icc, imm => t<cond> %icc, G0 + imm
  def : InstAlias<!strconcat(!strconcat("t", cond), " %icc, $imm"),
                  (TICCri G0, i32imm:$imm, condVal)>,
                  Requires<[HasV9]>;
  // t<cond> %icc, rs1 + imm
  def : InstAlias<!strconcat(!strconcat("t", cond), " %icc, $rs1 + $imm"),
                  (TICCri IntRegs:$rs1, i32imm:$imm, condVal)>,
                  Requires<[HasV9]>;
  // t<cond> %xcc, imm => t<cond> %xcc, G0 + imm
  def : InstAlias<!strconcat(!strconcat("t", cond), " %xcc, $imm"),
                  (TXCCri G0, i32imm:$imm, condVal)>,
                  Requires<[HasV9]>;
  // t<cond> %xcc, rs1 + imm
  def : InstAlias<!strconcat(!strconcat("t", cond), " %xcc, $rs1 + $imm"),
                  (TXCCri IntRegs:$rs1, i32imm:$imm, condVal)>,
                  Requires<[HasV9]>;

  // t<cond> imm => t<cond> G0 + imm
  def : InstAlias<!strconcat(!strconcat("t", cond), " $imm"),
                  (TRAPri G0, i32imm:$imm, condVal)>;

  // t<cond> rs1 + imm => t<cond> rs1 + imm
  def : InstAlias<!strconcat(!strconcat("t", cond), " $rs1 + $imm"),
                  (TRAPri IntRegs:$rs1, i32imm:$imm, condVal)>;

  // t<cond> rs1 => t<cond> G0 + rs1
  def : InstAlias<!strconcat(!strconcat("t", cond), " $rs1"),
                  (TRAPrr G0, IntRegs:$rs1, condVal)>;

  // t<cond> rs1 + rs2
  def : InstAlias<!strconcat(!strconcat("t", cond), " $rs1 + $rs2"),
                  (TRAPrr IntRegs:$rs1, IntRegs:$rs2, condVal)>;
}


// Instruction aliases for floating point conditional branches and moves.
multiclass fp_cond_alias<string cond, int condVal> {

  // fb<cond> $imm
  def : InstAlias<!strconcat(!strconcat("fb", cond), " $imm"),
                  (FBCOND brtarget:$imm, condVal), 0>;

  // fb<cond>,a $imm
  def : InstAlias<!strconcat(!strconcat("fb", cond), ",a $imm"),
                  (FBCONDA brtarget:$imm, condVal), 0>;

  // fb<cond> %fcc0, $imm
  def : InstAlias<!strconcat(!strconcat("fb", cond), " $cc, $imm"),
                  (BPFCC brtarget:$imm, condVal, FCCRegs:$cc)>,
                  Requires<[HasV9]>;

  // fb<cond>,pt %fcc0, $imm
  def : InstAlias<!strconcat(!strconcat("fb", cond), ",pt $cc, $imm"),
                  (BPFCC brtarget:$imm, condVal, FCCRegs:$cc)>,
                  Requires<[HasV9]>;

  // fb<cond>,a %fcc0, $imm
  def : InstAlias<!strconcat(!strconcat("fb", cond), ",a $cc, $imm"),
                  (BPFCCA brtarget:$imm, condVal, FCCRegs:$cc)>,
                  Requires<[HasV9]>;

  // fb<cond>,a,pt %fcc0, $imm
  def : InstAlias<!strconcat(!strconcat("fb", cond), ",a,pt $cc, $imm"),
                  (BPFCCA brtarget:$imm, condVal, FCCRegs:$cc)>,
                   Requires<[HasV9]>;

  // fb<cond>,pn %fcc0, $imm
  def : InstAlias<!strconcat(!strconcat("fb", cond), ",pn $cc, $imm"),
                  (BPFCCNT brtarget:$imm, condVal, FCCRegs:$cc)>,
                   Requires<[HasV9]>;

  // fb<cond>,a,pn %fcc0, $imm
  def : InstAlias<!strconcat(!strconcat("fb", cond), ",a,pn $cc, $imm"),
                  (BPFCCANT brtarget:$imm, condVal, FCCRegs:$cc)>,
                  Requires<[HasV9]>;

  defm : fpcond_mov_alias<cond, condVal,
                          V9MOVFCCrr, V9MOVFCCri,
                          V9FMOVS_FCC, V9FMOVD_FCC>, Requires<[HasV9]>;

  // fmovq<cond> %fcc0, $rs2, $rd
  def : InstAlias<!strconcat(!strconcat("fmovq", cond), " $cc, $rs2, $rd"),
                  (V9FMOVQ_FCC QFPRegs:$rd, FCCRegs:$cc, QFPRegs:$rs2,
                                                          condVal)>,
                  Requires<[HasV9, HasHardQuad]>;
}


// Instruction aliases for co-processor conditional branches.
multiclass cp_cond_alias<string cond, int condVal> {

  // cb<cond> $imm
  def : InstAlias<!strconcat(!strconcat("cb", cond), " $imm"),
                  (CBCOND brtarget:$imm, condVal), 0>;

  // cb<cond>,a $imm
  def : InstAlias<!strconcat(!strconcat("cb", cond), ",a $imm"),
                  (CBCONDA brtarget:$imm, condVal), 0>;
}

// Instruction aliases for register conditional branches and moves.
multiclass reg_cond_alias<string rcond, int condVal> {
  // br<rcond> $rs1, $imm
  def : InstAlias<!strconcat(!strconcat("br", rcond), " $rs1, $imm"),
                  (BPR bprtarget16:$imm, condVal, I64Regs:$rs1)>,
                  Requires<[Is64Bit]>;

  // br<rcond>,pt $rs1, $imm
  def : InstAlias<!strconcat(!strconcat("br", rcond), ",pt $rs1, $imm"),
                  (BPR bprtarget16:$imm, condVal, I64Regs:$rs1)>,
                  Requires<[Is64Bit]>;

  // br<rcond>,pn $rs1, $imm
  def : InstAlias<!strconcat(!strconcat("br", rcond), ",pn $rs1, $imm"),
                  (BPRNT bprtarget16:$imm, condVal, I64Regs:$rs1)>,
                  Requires<[Is64Bit]>;

  // br<rcond>,a $rs1, $imm
  def : InstAlias<!strconcat(!strconcat("br", rcond), ",a $rs1, $imm"),
                  (BPRA bprtarget16:$imm, condVal, I64Regs:$rs1)>,
                  Requires<[Is64Bit]>;

  // br<rcond>,a,pt $rs1, $imm
  def : InstAlias<!strconcat(!strconcat("br", rcond), ",a,pt $rs1, $imm"),
                  (BPRA bprtarget16:$imm, condVal, I64Regs:$rs1)>,
                  Requires<[Is64Bit]>;

  // br<rcond>,a,pn $rs1, $imm
  def : InstAlias<!strconcat(!strconcat("br", rcond), ",a,pn $rs1, $imm"),
                  (BPRANT bprtarget16:$imm, condVal, I64Regs:$rs1)>,
                  Requires<[Is64Bit]>;

  defm : regcond_mov_alias<rcond, condVal,
                            MOVRrr, MOVRri,
                            FMOVRS, FMOVRD, FMOVRQ>,
                            Requires<[Is64Bit]>;
}

defm : int_cond_alias<"a",    0b1000>;
defm : int_cond_alias<"n",    0b0000>;
defm : int_cond_alias<"ne",   0b1001>;
defm : int_cond_alias<"e",    0b0001>;
defm : int_cond_alias<"g",    0b1010>;
defm : int_cond_alias<"le",   0b0010>;
defm : int_cond_alias<"ge",   0b1011>;
defm : int_cond_alias<"l",    0b0011>;
defm : int_cond_alias<"gu",   0b1100>;
defm : int_cond_alias<"leu",  0b0100>;
defm : int_cond_alias<"cc",   0b1101>;
defm : int_cond_alias<"cs",   0b0101>;
defm : int_cond_alias<"pos",  0b1110>;
defm : int_cond_alias<"neg",  0b0110>;
defm : int_cond_alias<"vc",   0b1111>;
defm : int_cond_alias<"vs",   0b0111>;
let EmitPriority = 0 in
{
  defm : int_cond_alias<"",     0b1000>; // same as a; gnu asm, not in manual
  defm : int_cond_alias<"gt",   0b1010>; // same as g; gnu asm, not in manual
  defm : int_cond_alias<"lt",   0b0011>; // same as l; gnu asm, not in manual
  defm : int_cond_alias<"nz",   0b1001>; // same as ne
  defm : int_cond_alias<"eq",   0b0001>; // same as e
  defm : int_cond_alias<"z",    0b0001>; // same as e
  defm : int_cond_alias<"geu",  0b1101>; // same as cc
  defm : int_cond_alias<"lu",   0b0101>; // same as cs
}
defm : fp_cond_alias<"a",     0b1000>;
defm : fp_cond_alias<"n",     0b0000>;
defm : fp_cond_alias<"u",     0b0111>;
defm : fp_cond_alias<"g",     0b0110>;
defm : fp_cond_alias<"ug",    0b0101>;
defm : fp_cond_alias<"l",     0b0100>;
defm : fp_cond_alias<"ul",    0b0011>;
defm : fp_cond_alias<"lg",    0b0010>;
defm : fp_cond_alias<"ne",    0b0001>;
defm : fp_cond_alias<"e",     0b1001>;
defm : fp_cond_alias<"ue",    0b1010>;
defm : fp_cond_alias<"ge",    0b1011>;
defm : fp_cond_alias<"uge",   0b1100>;
defm : fp_cond_alias<"le",    0b1101>;
defm : fp_cond_alias<"ule",   0b1110>;
defm : fp_cond_alias<"o",     0b1111>;
let EmitPriority = 0 in
{
  defm : fp_cond_alias<"",      0b1000>; // same as a; gnu asm, not in manual
  defm : fp_cond_alias<"nz",    0b0001>; // same as ne
  defm : fp_cond_alias<"z",     0b1001>; // same as e
}

defm : cp_cond_alias<"a",     0b1000>;
defm : cp_cond_alias<"n",     0b0000>;
defm : cp_cond_alias<"3",     0b0111>;
defm : cp_cond_alias<"2",     0b0110>;
defm : cp_cond_alias<"23",    0b0101>;
defm : cp_cond_alias<"1",     0b0100>;
defm : cp_cond_alias<"13",    0b0011>;
defm : cp_cond_alias<"12",    0b0010>;
defm : cp_cond_alias<"123",   0b0001>;
defm : cp_cond_alias<"0",     0b1001>;
defm : cp_cond_alias<"03",    0b1010>;
defm : cp_cond_alias<"02",    0b1011>;
defm : cp_cond_alias<"023",   0b1100>;
defm : cp_cond_alias<"01",    0b1101>;
defm : cp_cond_alias<"013",   0b1110>;
defm : cp_cond_alias<"012",   0b1111>;
let EmitPriority = 0 in defm : cp_cond_alias<"",      0b1000>; // same as a; gnu asm, not in manual

defm : reg_cond_alias<"z",    0b001>;
defm : reg_cond_alias<"e",    0b001>;
defm : reg_cond_alias<"lez",  0b010>;
defm : reg_cond_alias<"lz",   0b011>;
defm : reg_cond_alias<"nz",   0b101>;
defm : reg_cond_alias<"ne",   0b101>;
defm : reg_cond_alias<"gz",   0b110>;
defm : reg_cond_alias<"gez",  0b111>;

// Section A.3 Synthetic Instructions

// Most are marked as Emit=0, so that they are not used for disassembly. This is
// an aesthetic issue, but the chosen policy is to typically prefer using the
// non-alias form, except for the most obvious and clarifying aliases: cmp, jmp,
// call, tst, ret, retl.

// Note: jmp/call/ret/retl have special case handling for output in
//       SparcInstPrinter.cpp

// cmp rs1, reg_or_imm -> subcc rs1, reg_or_imm, %g0
def : InstAlias<"cmp $rs1, $rs2", (SUBCCrr G0, IntRegs:$rs1, IntRegs:$rs2)>;
def : InstAlias<"cmp $rs1, $imm", (SUBCCri G0, IntRegs:$rs1, simm13Op:$imm)>;

// jmp addr -> jmpl addr, %g0
def : InstAlias<"jmp $addr", (JMPLrr G0, MEMrr:$addr), 0>;
def : InstAlias<"jmp $addr", (JMPLri G0, MEMri:$addr), 0>;

// call addr -> jmpl addr, %o7
def : InstAlias<"call $addr", (JMPLrr O7, MEMrr:$addr), 0>;
def : InstAlias<"call $addr", (JMPLri O7, MEMri:$addr), 0>;

// tst reg -> orcc %g0, reg, %g0
def : InstAlias<"tst $rs2", (ORCCrr G0, IntRegs:$rs2, G0)>;

// ret -> jmpl %i7+8, %g0 (aka RET 8)
def : InstAlias<"ret", (RET 8)>;

// retl -> jmpl %o7+8, %g0 (aka RETL 8)
def : InstAlias<"retl", (RETL 8)>;

// restore -> restore %g0, %g0, %g0
def : InstAlias<"restore", (RESTORErr G0, G0, G0)>;

// save -> restore %g0, %g0, %g0
def : InstAlias<"save", (SAVErr G0, G0, G0)>;

// set value, rd
// (turns into a sequence of sethi+or, depending on the value)
// def : InstAlias<"set $val, $rd", (ORri IntRegs:$rd, (SETHIi (HI22 imm:$val)), (LO10 imm:$val))>;
def SET : AsmPseudoInst<(outs IntRegs:$rd), (ins i32imm:$val), "set $val, $rd">;

// setx value, tmp, rd
// (turns into a sequence of sethi+or+shift, depending on the value)
def SETX : AsmPseudoInst<(outs I64Regs:$rd),
                         (ins i64imm:$val, I64Regs:$tmp),
                         "setx $val, $tmp, $rd">,
                         Requires<[Is64Bit, HasV9]>;

// not rd -> xnor rd, %g0, rd
def : InstAlias<"not $rd", (XNORrr IntRegs:$rd, IntRegs:$rd, G0), 0>;

// not reg, rd -> xnor reg, %g0, rd
def : InstAlias<"not $rs1, $rd", (XNORrr IntRegs:$rd, IntRegs:$rs1, G0), 0>;

// neg rd -> sub %g0, rd, rd
def : InstAlias<"neg $rd", (SUBrr IntRegs:$rd, G0, IntRegs:$rd), 0>;

// neg reg, rd -> sub %g0, reg, rd
def : InstAlias<"neg $rs2, $rd", (SUBrr IntRegs:$rd, G0, IntRegs:$rs2), 0>;

let Predicates = [HasV9] in {
  // cas [rs1], rs2, rd -> casa [rs1] #ASI_P, rs2, rd
  def : InstAlias<"cas [$rs1], $rs2, $rd",
                  (CASArr IntRegs:$rd, IntRegs:$rs1, IntRegs:$rs2, 0x80)>;

  // casl [rs1], rs2, rd -> casa [rs1] #ASI_P_L, rs2, rd
  def : InstAlias<"casl [$rs1], $rs2, $rd",
                  (CASArr IntRegs:$rd, IntRegs:$rs1, IntRegs:$rs2, 0x88)>;

  // casx [rs1], rs2, rd -> casxa [rs1] #ASI_P, rs2, rd
  def : InstAlias<"casx [$rs1], $rs2, $rd",
                  (CASXArr I64Regs:$rd, I64Regs:$rs1, I64Regs:$rs2, 0x80)>;

  // casxl [rs1], rs2, rd -> casxa [rs1] #ASI_P_L, rs2, rd
  def : InstAlias<"casxl [$rs1], $rs2, $rd",
                  (CASXArr I64Regs:$rd, I64Regs:$rs1, I64Regs:$rs2, 0x88)>;
}

// inc rd -> add rd, 1, rd
def : InstAlias<"inc $rd", (ADDri IntRegs:$rd, IntRegs:$rd, 1), 0>;

// inc simm13, rd -> add rd, simm13, rd
def : InstAlias<"inc $simm13, $rd", (ADDri IntRegs:$rd, IntRegs:$rd, simm13Op:$simm13), 0>;

// inccc rd -> addcc rd, 1, rd
def : InstAlias<"inccc $rd", (ADDCCri IntRegs:$rd, IntRegs:$rd, 1), 0>;

// inccc simm13, rd -> addcc rd, simm13, rd
def : InstAlias<"inccc $simm13, $rd", (ADDCCri IntRegs:$rd, IntRegs:$rd, simm13Op:$simm13), 0>;

// dec rd -> sub rd, 1, rd
def : InstAlias<"dec $rd", (SUBri IntRegs:$rd, IntRegs:$rd, 1), 0>;

// dec simm13, rd -> sub rd, simm13, rd
def : InstAlias<"dec $simm13, $rd", (SUBri IntRegs:$rd, IntRegs:$rd, simm13Op:$simm13), 0>;

// deccc rd -> subcc rd, 1, rd
def : InstAlias<"deccc $rd", (SUBCCri IntRegs:$rd, IntRegs:$rd, 1), 0>;

// deccc simm13, rd -> subcc rd, simm13, rd
def : InstAlias<"deccc $simm13, $rd", (SUBCCri IntRegs:$rd, IntRegs:$rd, simm13Op:$simm13), 0>;

// btst reg_or_imm, reg -> andcc reg,reg_or_imm,%g0
def : InstAlias<"btst $rs2, $rs1", (ANDCCrr G0, IntRegs:$rs1, IntRegs:$rs2), 0>;
def : InstAlias<"btst $simm13, $rs1", (ANDCCri G0, IntRegs:$rs1, simm13Op:$simm13), 0>;

// bset reg_or_imm, rd -> or rd,reg_or_imm,rd
def : InstAlias<"bset $rs2, $rd", (ORrr IntRegs:$rd, IntRegs:$rd, IntRegs:$rs2), 0>;
def : InstAlias<"bset $simm13, $rd", (ORri IntRegs:$rd, IntRegs:$rd, simm13Op:$simm13), 0>;

// bclr reg_or_imm, rd -> andn rd,reg_or_imm,rd
def : InstAlias<"bclr $rs2, $rd", (ANDNrr IntRegs:$rd, IntRegs:$rd, IntRegs:$rs2), 0>;
def : InstAlias<"bclr $simm13, $rd", (ANDNri IntRegs:$rd, IntRegs:$rd, simm13Op:$simm13), 0>;

// btog reg_or_imm, rd -> xor rd,reg_or_imm,rd
def : InstAlias<"btog $rs2, $rd", (XORrr IntRegs:$rd, IntRegs:$rd, IntRegs:$rs2), 0>;
def : InstAlias<"btog $simm13, $rd", (XORri IntRegs:$rd, IntRegs:$rd, simm13Op:$simm13), 0>;


// clr rd -> or %g0, %g0, rd
def : InstAlias<"clr $rd", (ORrr IntRegs:$rd, G0, G0), 0>;

// clr{b,h,} [addr] -> st{b,h,} %g0, [addr]
def : InstAlias<"clrb [$addr]", (STBrr MEMrr:$addr, G0), 0>;
def : InstAlias<"clrb [$addr]", (STBri MEMri:$addr, G0), 0>;
def : InstAlias<"clrh [$addr]", (STHrr MEMrr:$addr, G0), 0>;
def : InstAlias<"clrh [$addr]", (STHri MEMri:$addr, G0), 0>;
def : InstAlias<"clr [$addr]", (STrr MEMrr:$addr, G0), 0>;
def : InstAlias<"clr [$addr]", (STri MEMri:$addr, G0), 0>;


// mov reg_or_imm, rd -> or %g0, reg_or_imm, rd
def : InstAlias<"mov $rs2, $rd", (ORrr IntRegs:$rd, G0, IntRegs:$rs2)>;
def : InstAlias<"mov $simm13, $rd", (ORri IntRegs:$rd, G0, simm13Op:$simm13)>;

// mov specialreg, rd -> rd specialreg, rd
def : InstAlias<"mov $asr, $rd", (RDASR IntRegs:$rd, ASRRegs:$asr), 0>;
def : InstAlias<"mov %psr, $rd", (RDPSR IntRegs:$rd), 0>;
def : InstAlias<"mov %wim, $rd", (RDWIM IntRegs:$rd), 0>;
def : InstAlias<"mov %tbr, $rd", (RDTBR IntRegs:$rd), 0>;

// mov reg_or_imm, specialreg -> wr %g0, reg_or_imm, specialreg
def : InstAlias<"mov $rs2, $asr", (WRASRrr ASRRegs:$asr, G0, IntRegs:$rs2), 0>;
def : InstAlias<"mov $simm13, $asr", (WRASRri ASRRegs:$asr, G0, simm13Op:$simm13), 0>;
def : InstAlias<"mov $rs2, %psr", (WRPSRrr G0, IntRegs:$rs2), 0>;
def : InstAlias<"mov $simm13, %psr", (WRPSRri G0, simm13Op:$simm13), 0>;
def : InstAlias<"mov $rs2, %wim", (WRWIMrr G0, IntRegs:$rs2), 0>;
def : InstAlias<"mov $simm13, %wim", (WRWIMri G0, simm13Op:$simm13), 0>;
def : InstAlias<"mov $rs2, %tbr", (WRTBRrr G0, IntRegs:$rs2), 0>;
def : InstAlias<"mov $simm13, %tbr", (WRTBRri G0, simm13Op:$simm13), 0>;

// End of Section A.3


// Nonstandard GNU extensions.
let EmitPriority = 0 in {
  // or imm, reg, rd -> or reg, imm, rd
  def : InstAlias<"or $simm13, $rs1, $rd", (ORri IntRegs:$rd, IntRegs:$rs1, simm13Op:$simm13)>;

  // addc/addx imm, reg, rd -> addc/addx reg, imm, rd
  def : InstAlias<"addx $simm13, $rs1, $rd", (ADDCri IntRegs:$rd, IntRegs:$rs1, simm13Op:$simm13)>;
}

// wr reg_or_imm, specialreg -> wr %g0, reg_or_imm, specialreg
// (aka: omit the first arg when it's g0. This is not in the manual, but is
// supported by gnu and solaris as)
def : InstAlias<"wr $rs2, $asr", (WRASRrr ASRRegs:$asr, G0, IntRegs:$rs2), 0>;
def : InstAlias<"wr $simm13, $asr", (WRASRri ASRRegs:$asr, G0, simm13Op:$simm13), 0>;
def : InstAlias<"wr $rs2, %psr", (WRPSRrr G0, IntRegs:$rs2), 0>;
def : InstAlias<"wr $simm13, %psr", (WRPSRri G0, simm13Op:$simm13), 0>;
def : InstAlias<"wr $rs2, %wim", (WRWIMrr G0, IntRegs:$rs2), 0>;
def : InstAlias<"wr $simm13, %wim", (WRWIMri G0, simm13Op:$simm13), 0>;
def : InstAlias<"wr $rs2, %tbr", (WRTBRrr G0, IntRegs:$rs2), 0>;
def : InstAlias<"wr $simm13, %tbr", (WRTBRri G0, simm13Op:$simm13), 0>;

def : InstAlias<"pwr $rs2, %psr", (PWRPSRrr G0, IntRegs:$rs2), 0>;
def : InstAlias<"pwr $simm13, %psr", (PWRPSRri G0, simm13Op:$simm13), 0>;

// wrpr %reg, %rd -> wrpr %reg, %g0, %rd
// wrpr  imm, %rd -> wrpr  %g0, imm, %rd
// Nonstandard GNU extensions.
let Predicates = [HasV9] in {
  def : InstAlias<"wrpr $rs1, $rd", (WRPRrr PRRegs:$rd, IntRegs:$rs1, G0), 0>;
  def : InstAlias<"wrpr $simm13, $rd", (WRPRri PRRegs:$rd, G0, simm13Op:$simm13), 0>;
}

// flush -> flush %g0
def : InstAlias<"flush", (FLUSH), 0>;

// unimp -> unimp 0
def : InstAlias<"unimp", (UNIMP 0), 0>;

def : MnemonicAlias<"iflush", "flush">;

def : MnemonicAlias<"stub", "stb">;
def : MnemonicAlias<"stsb", "stb">;

def : MnemonicAlias<"stuba", "stba">;
def : MnemonicAlias<"stsba", "stba">;

def : MnemonicAlias<"stuh", "sth">;
def : MnemonicAlias<"stsh", "sth">;

def : MnemonicAlias<"stuha", "stha">;
def : MnemonicAlias<"stsha", "stha">;

def : MnemonicAlias<"stw", "st">, Requires<[HasV9]>;
def : MnemonicAlias<"stuw", "st">, Requires<[HasV9]>;
def : MnemonicAlias<"stsw", "st">, Requires<[HasV9]>;

def : MnemonicAlias<"stwa", "sta">, Requires<[HasV9]>;
def : MnemonicAlias<"stuwa", "sta">, Requires<[HasV9]>;
def : MnemonicAlias<"stswa", "sta">, Requires<[HasV9]>;

def : MnemonicAlias<"lduw", "ld">, Requires<[HasV9]>;
def : MnemonicAlias<"lduwa", "lda">, Requires<[HasV9]>;

def : MnemonicAlias<"return", "rett">, Requires<[HasV9]>;

def : MnemonicAlias<"addc", "addx">, Requires<[HasV9]>;
def : MnemonicAlias<"addccc", "addxcc">, Requires<[HasV9]>;

def : MnemonicAlias<"subc", "subx">, Requires<[HasV9]>;
def : MnemonicAlias<"subccc", "subxcc">, Requires<[HasV9]>;


def : InstAlias<"fcmps $rs1, $rs2", (V9FCMPS FCC0, FPRegs:$rs1, FPRegs:$rs2)>;
def : InstAlias<"fcmpd $rs1, $rs2", (V9FCMPD FCC0, DFPRegs:$rs1, DFPRegs:$rs2)>;
def : InstAlias<"fcmpq $rs1, $rs2", (V9FCMPQ FCC0, QFPRegs:$rs1, QFPRegs:$rs2)>,
                Requires<[HasHardQuad]>;

def : InstAlias<"fcmpes $rs1, $rs2", (V9FCMPES FCC0, FPRegs:$rs1, FPRegs:$rs2)>;
def : InstAlias<"fcmped $rs1, $rs2", (V9FCMPED FCC0, DFPRegs:$rs1,
                                                     DFPRegs:$rs2)>;
def : InstAlias<"fcmpeq $rs1, $rs2", (V9FCMPEQ FCC0, QFPRegs:$rs1,
                                                     QFPRegs:$rs2)>,
                Requires<[HasHardQuad]>;

// signx rd -> sra rd, %g0, rd
def : InstAlias<"signx $rd", (SRArr IntRegs:$rd, IntRegs:$rd, G0), 0>, Requires<[HasV9]>;

// signx reg, rd -> sra reg, %g0, rd
def : InstAlias<"signx $rs1, $rd", (SRArr IntRegs:$rd, IntRegs:$rs1, G0), 0>, Requires<[HasV9]>;

// sir -> sir 0
def : InstAlias<"sir", (SIR 0), 0>;
