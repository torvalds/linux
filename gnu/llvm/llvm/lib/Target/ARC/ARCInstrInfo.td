//===- ARCInstrInfo.td - Target Description for ARC --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the ARC instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "ARCInstrFormats.td"

//===----------------------------------------------------------------------===//
// Operand Pattern Stuff.
//===----------------------------------------------------------------------===//

// Operand for printing out a condition code.
let PrintMethod = "printCCOperand" in
  def CCOp : PredicateOperand<i32, (ops i32imm), (ops)>;

// The "u6" operand of a RRU6-type instruction
let PrintMethod = "printU6" in {
  def u6 : Operand<i32>, ImmLeaf<i32, [{
    return isUInt<6>(Imm);
  }]>;
  def wide_u6 : Operand<i64>, ImmLeaf<i64, [{
    return isUInt<6>(Imm);
  }]>;
}

// ---------------------------------------------------------------------------
// Selection DAG Nodes.
// ---------------------------------------------------------------------------

// Selection DAG types.
def SDT_ARCcmptst : SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>;
def SDT_ARCcmov : SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>]>;
def SDT_ARCmov : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>]>;
def SDT_ARCbrcc : SDTypeProfile<0, 4, []>;
def SDT_ARCBranchLink : SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>;
def SDT_ARCCallSeqStart : SDCallSeqStart<[ SDTCisVT<0, i32>,
                                           SDTCisVT<1, i32> ]>;
def SDT_ARCCallSeqEnd   : SDCallSeqEnd<[ SDTCisVT<0, i32>,
                                         SDTCisVT<1, i32> ]>;

// Global Address.
def ARCGAWrapper : SDNode<"ARCISD::GAWRAPPER", SDT_ARCmov, []>;

// Comparison
def ARCcmp : SDNode<"ARCISD::CMP", SDT_ARCcmptst, [SDNPOutGlue]>;

// Conditional mov
def ARCcmov : SDNode<"ARCISD::CMOV", SDT_ARCcmov, [SDNPInGlue]>;

// Conditional Branch
def ARCbrcc : SDNode<"ARCISD::BRcc", SDT_ARCbrcc,
                       [SDNPHasChain, SDNPInGlue, SDNPOutGlue]>;

// Direct Call
def ARCBranchLink     : SDNode<"ARCISD::BL",SDT_ARCBranchLink,
                            [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                             SDNPVariadic]>;

// Indirect Call
def ARCJumpLink       : SDNode<"ARCISD::JL",SDT_ARCBranchLink,
                                 [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                                  SDNPVariadic]>;
// Call return
def ret      : SDNode<"ARCISD::RET", SDTNone,
                      [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

// Call sequencing nodes.
// These are target-independent nodes, but have target-specific formats.
def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_ARCCallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end   : SDNode<"ISD::CALLSEQ_END",   SDT_ARCCallSeqEnd,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

//===----------------------------------------------------------------------===//
// Instruction predicates
//===----------------------------------------------------------------------===//

def HasNorm  :       Predicate<"Subtarget->hasNorm()">;

//===----------------------------------------------------------------------===//
// Instruction Pattern Stuff
//===----------------------------------------------------------------------===//

def imm32 : ImmLeaf<i32, [{
  return (Imm & 0xFFFFFFFF) == Imm;
}]>;

// Addressing modes
def FrameADDR_ri : ComplexPattern<i32, 2, "SelectFrameADDR_ri",
                                  [add, frameindex], []>;
def AddrModeS9 : ComplexPattern<i32, 2, "SelectAddrModeS9", []>;
def AddrModeImm : ComplexPattern<i32, 2, "SelectAddrModeImm", []>;
def AddrModeFar : ComplexPattern<i32, 2, "SelectAddrModeFar", []>;

//===----------------------------------------------------------------------===//
// Instruction Class Templates
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Pseudo Instructions
//===----------------------------------------------------------------------===//

let Defs = [SP], Uses = [SP] in {
def ADJCALLSTACKDOWN : PseudoInstARC<(outs), (ins i32imm:$amt, i32imm:$amt2),
                               "# ADJCALLSTACKDOWN $amt, $amt2",
                               [(callseq_start timm:$amt, timm:$amt2)]>;
def ADJCALLSTACKUP : PseudoInstARC<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                            "# ADJCALLSTACKUP $amt1",
                            [(callseq_end timm:$amt1, timm:$amt2)]>;
}

def GETFI : PseudoInstARC<(outs GPR32:$dst), (ins MEMii:$addr),
                             "pldfi $dst, $addr",
                             [(set GPR32:$dst, FrameADDR_ri:$addr)]>;


def ST_FAR : PseudoInstARC<(outs), (ins GPR32:$dst, MEMrlimm:$addr),
                             "ST_FAR $dst, $addr",
                             [(store GPR32:$dst, AddrModeFar:$addr)]>;

def STH_FAR : PseudoInstARC<(outs), (ins GPR32:$dst, MEMrlimm:$addr),
                             "STH_FAR $dst, $addr",
                             [(truncstorei16 GPR32:$dst, AddrModeFar:$addr)]>;

def STB_FAR : PseudoInstARC<(outs), (ins GPR32:$dst, MEMrlimm:$addr),
                             "STB_FAR $dst, $addr",
                             [(truncstorei8 GPR32:$dst, AddrModeFar:$addr)]>;

// TODO: Add `Requires<[HasBitScan]>` predicate to these when available.
let Defs = [STATUS32] in {
  def CTLZ : PseudoInstARC<(outs GPR32:$A),
                           (ins GPR32:$B),
                           "error.fls $A, $B",
                           [(set GPR32:$A, (ctlz i32:$B))]>;

  def CTTZ : PseudoInstARC<(outs GPR32:$A),
                           (ins GPR32:$B),
                           "error.ffs $A, $B",
                           [(set GPR32:$A, (cttz i32:$B))]>;
}

//===----------------------------------------------------------------------===//
// Instruction Generation multiclasses.
// Generate many variants of a single instruction with a single defining
// multiclass.  These classes do not contain Selection DAG patterns.
//===----------------------------------------------------------------------===//

// Generic 3 operand binary instructions (i.e., add r0, r1, r2).
multiclass ArcBinaryInst<bits<5> major, bits<6> mincode,
                       string opasm, bit Commutable> {
  // 3 register variant.
  def _rrr : F32_DOP_RR<major, mincode, 0, (outs GPR32:$A),
                        (ins GPR32:$B, GPR32:$C),
                        !strconcat(opasm, "\t$A, $B, $C"),
                        []>
  { let isCommutable = Commutable; }
  def _f_rrr : F32_DOP_RR<major, mincode, 1, (outs GPR32:$A),
                          (ins GPR32:$B, GPR32:$C),
                          !strconcat(opasm, ".f\t$A, $B, $C"),
                          []>
  { let Defs = [STATUS32]; }

  // 2 register with unsigned 6-bit immediate variant.
  def _rru6 : F32_DOP_RU6<major, mincode, 0, (outs GPR32:$A),
                          (ins GPR32:$B, immU6:$U6),
                          !strconcat(opasm, "\t$A, $B, $U6"),
                          []>;
  def _f_rru6 : F32_DOP_RU6<major, mincode, 1, (outs GPR32:$A),
                            (ins GPR32:$B, immU6:$U6),
                            !strconcat(opasm, ".f\t$A, $B, $U6"),
                            []>
  { let Defs = [STATUS32]; }

  def _cc_rru6 : F32_DOP_CC_RRU6<major, mincode, 0, (outs GPR32:$A),
                                 (ins immU6:$U6, ccond:$cc, GPR32:$B),
                                 !strconcat(opasm, ".$cc\t$A, $B, $U6"),
                                 []> {
                                   let Uses = [STATUS32];
                                   let Constraints = "$A = $B";
                                 }

  def _cc_f_rru6 : F32_DOP_CC_RRU6<major, mincode, 1, (outs GPR32:$A),
                                   (ins immU6:$U6, ccond:$cc, GPR32:$B),
                                   !strconcat(opasm, ".$cc.f\t$A, $B, $U6"),
                                   []> {
                                     let Defs = [STATUS32];
                                     let Uses = [STATUS32];
                                     let Constraints = "$A = $B";
                                   }

  // 2 register with 32-bit immediate variant.
  def _rrlimm : F32_DOP_RLIMM<major, mincode, 0,
                              (outs GPR32:$A),
                              (ins GPR32:$B, i32imm:$LImm),
                              !strconcat(opasm, "\t$A, $B, $LImm"),
                              []>;
  def _f_rrlimm : F32_DOP_RLIMM<major, mincode, 1,
                                (outs GPR32:$A),
                                (ins GPR32:$B, i32imm:$LImm),
                                !strconcat(opasm, ".f\t$A, $B, $LImm"),
                                []>
  { let Defs = [STATUS32]; }

  // 2 matched-register with signed 12-bit immediate variant (add r0, r0, -1).
  def _rrs12 : F32_DOP_RS12<major, mincode, 0,
                            (outs GPR32:$B),
                            (ins GPR32:$in, immS<12>:$S12),
                            !strconcat(opasm, "\t$B, $in, $S12"),
                            []>
  { let Constraints = "$B = $in"; }
  def _f_rrs12 : F32_DOP_RS12<major, mincode, 1,
                              (outs GPR32:$B),
                              (ins GPR32:$in, immS<12>:$S12),
                              !strconcat(opasm, ".f\t$B, $in, $S12"),
                              []>
  { let Constraints = "$B = $in"; let Defs = [STATUS32]; }
}

// Special multivariant GEN4 DOP format instruction that take 2 registers.
// This is the class that is used for various comparison instructions.
multiclass ArcSpecialDOPInst<bits<6> subop, string opasm, bit F> {
  def _rr : F32_DOP_RR<0b00100, subop, F, (outs), (ins GPR32:$B, GPR32:$C),
               !strconcat(opasm, "\t$B, $C"),
               []>;

  def _ru6 : F32_DOP_RU6<0b00100, subop, F, (outs), (ins GPR32:$B, i32imm:$U6),
               !strconcat(opasm, "\t$B, $U6"),
               []>;

  def _rlimm : F32_DOP_RLIMM<0b00100, subop, F, (outs),
               (ins GPR32:$B, i32imm:$LImm),
               !strconcat(opasm, "\t$B, $LImm"),
               []>;
}

// Generic 2-operand unary instructions.
multiclass ArcUnaryInst<bits<5> major, bits<6> subop,
                        string opasm> {
  def _rr : F32_SOP_RR<major, subop, 0, (outs GPR32:$B), (ins GPR32:$C),
                       !strconcat(opasm, "\t$B, $C"), []>;

  def _f_rr : F32_SOP_RR<major, subop, 1, (outs GPR32:$B), (ins GPR32:$C),
                       !strconcat(opasm, ".f\t$B, $C"), []>
  { let Defs = [STATUS32]; }
}


multiclass ArcBinaryGEN4Inst<bits<6> mincode, string opasm, bit Commutable = 0> :
  ArcBinaryInst<0b00100, mincode, opasm, Commutable>;
multiclass ArcBinaryEXT5Inst<bits<6> mincode, string opasm> :
  ArcBinaryInst<0b00101, mincode, opasm, 0>;

multiclass ArcUnaryGEN4Inst<bits<6> mincode, string opasm> :
  ArcUnaryInst<0b00100, mincode, opasm>;
multiclass ArcUnaryEXT5Inst<bits<6> mincode, string opasm> :
  ArcUnaryInst<0b00101, mincode, opasm>;

// Pattern generation for different instruction variants.
multiclass MultiPat<SDPatternOperator InFrag,
               Instruction RRR, Instruction RRU6, Instruction RRLImm> {
  def _rrr : Pat<(InFrag i32:$B, i32:$C), (RRR i32:$B, i32:$C)>;
  def _rru6 : Pat<(InFrag i32:$B, immU6:$U6), (RRU6 i32:$B, immU6:$U6)>;
  def _rrlimm : Pat<(InFrag i32:$B, imm32:$LImm), (RRLImm i32:$B, imm32:$LImm)>;
}

// NOTE: This could be specialized later with a custom `PrintMethod` for
//       displaying the aux register name. E.g. `[%count0]` instead of [33].
def AuxReg : Operand<i32>;

def LR_rs12 : F32_SOP_RS12<0b00100, 0b101010, 0,
                           (outs GPR32:$B), (ins AuxReg:$C),
                           "lr\t$B, [$C]", []>;
def LR_ru6 : F32_SOP_RU6<0b00100, 0b101010, 0,
                         (outs GPR32:$B), (ins AuxReg:$C),
                         "lr\t$B, [$C]", []>;

def: Pat<(i32 readcyclecounter), (LR_rs12 0x21) >;  // read timer

// ---------------------------------------------------------------------------
// Instruction definitions and patterns for 3 operand binary instructions.
// ---------------------------------------------------------------------------

// Definitions for 3 operand binary instructions.
defm ADD : ArcBinaryGEN4Inst<0b000000, "add",1>;
defm SUB : ArcBinaryGEN4Inst<0b000010, "sub">;
defm SUB1 : ArcBinaryGEN4Inst<0b010111, "sub1">;
defm SUB2 : ArcBinaryGEN4Inst<0b011000, "sub2">;
defm SUB3 : ArcBinaryGEN4Inst<0b011001, "sub3">;
defm RSUB : ArcBinaryGEN4Inst<0b001110, "rsub">;
defm OR  : ArcBinaryGEN4Inst<0b000101, "or",1>;
defm AND : ArcBinaryGEN4Inst<0b000100, "and",1>;
defm XOR : ArcBinaryGEN4Inst<0b000111, "xor",1>;
defm MAX : ArcBinaryGEN4Inst<0b001000, "max",1>;
defm MIN : ArcBinaryGEN4Inst<0b001001, "min",1>;
defm ASL : ArcBinaryEXT5Inst<0b000000, "asl">;
defm LSR : ArcBinaryEXT5Inst<0b000001, "lsr">;
defm ASR : ArcBinaryEXT5Inst<0b000010, "asr">;
defm ROR : ArcBinaryEXT5Inst<0b000011, "ror">;
defm MPY  : ArcBinaryGEN4Inst<0b011010, "mpy",1>;
defm MPYM : ArcBinaryGEN4Inst<0b011011, "mpym",1>;
defm MPYMU : ArcBinaryGEN4Inst<0b011100, "mpymu",1>;
defm SETEQ : ArcBinaryGEN4Inst<0b111000, "seteq",1>;
let Uses=[STATUS32], isAsCheapAsAMove=0, isReMaterializable=0 in {
  defm ADC : ArcBinaryGEN4Inst<0b000001, "adc",1>;
  defm SBC : ArcBinaryGEN4Inst<0b000011, "sbc">;
}

// Patterns for 3 operand binary instructions.
defm : MultiPat<add, ADD_rrr, ADD_rru6, ADD_rrlimm>;
defm : MultiPat<sub, SUB_rrr, SUB_rru6, SUB_rrlimm>;
defm : MultiPat<or, OR_rrr, OR_rru6, OR_rrlimm>;
defm : MultiPat<and, AND_rrr, AND_rru6, AND_rrlimm>;
defm : MultiPat<xor, XOR_rrr, XOR_rru6, XOR_rrlimm>;
defm : MultiPat<smax, MAX_rrr, MAX_rru6, MAX_rrlimm>;
defm : MultiPat<smin, MIN_rrr, MIN_rru6, MIN_rrlimm>;
defm : MultiPat<shl, ASL_rrr, ASL_rru6, ASL_rrlimm>;
defm : MultiPat<srl, LSR_rrr, LSR_rru6, LSR_rrlimm>;
defm : MultiPat<sra, ASR_rrr, ASR_rru6, ASR_rrlimm>;
defm : MultiPat<rotr, ROR_rrr, ROR_rru6, ROR_rrlimm>;
defm : MultiPat<mul, MPY_rrr, MPY_rru6, MPY_rrlimm>;
defm : MultiPat<mulhs, MPYM_rrr, MPYM_rru6, MPYM_rrlimm>;
defm : MultiPat<mulhu, MPYMU_rrr, MPYMU_rru6, MPYMU_rrlimm>;

defm : MultiPat<addc, ADD_f_rrr, ADD_f_rru6, ADD_f_rrlimm>;
defm : MultiPat<adde, ADC_f_rrr, ADC_f_rru6, ADC_f_rrlimm>;
defm : MultiPat<subc, SUB_f_rrr, SUB_f_rru6, SUB_f_rrlimm>;
defm : MultiPat<sube, SBC_f_rrr, SBC_f_rru6, SBC_f_rrlimm>;

// ---------------------------------------------------------------------------
// Unary Instruction definitions.
// ---------------------------------------------------------------------------
// General unary instruction definitions.
defm SEXB : ArcUnaryGEN4Inst<0b000101, "sexb">;
defm SEXH : ArcUnaryGEN4Inst<0b000110, "sexh">;

// Extension unary instruction definitions.
defm FFS : ArcUnaryEXT5Inst<0b010010, "ffs">;
defm FLS : ArcUnaryEXT5Inst<0b010011, "fls">;

let Predicates=[HasNorm] in {
  defm NORM  : ArcUnaryEXT5Inst<0b000001,"norm">;
  defm NORMH : ArcUnaryEXT5Inst<0b001000,"normh">;
}

// General Unary Instruction fragments.
def : Pat<(sext_inreg i32:$a, i8), (SEXB_rr i32:$a)>;
def : Pat<(sext_inreg i32:$a, i16), (SEXH_rr i32:$a)>;

// Comparison instruction definition
let isCompare = 1, Defs = [STATUS32] in {
defm CMP : ArcSpecialDOPInst<0b001100, "cmp", 1>;
}

def cmp : PatFrag<(ops node:$op1, node:$op2), (ARCcmp $op1, $op2)>;
defm : MultiPat<cmp, CMP_rr, CMP_ru6, CMP_rlimm>;

// ---------------------------------------------------------------------------
// MOV instruction and variants (conditional mov).
// ---------------------------------------------------------------------------
let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1 in {
def MOV_rs12 : F32_DOP_RS12<0b00100, 0b001010, 0,
                 (outs GPR32:$B), (ins immS<12>:$S12),
                 "mov\t$B, $S12",
                 [(set GPR32:$B, immS<12>:$S12)]>;
}

def MOV_rr : F32_DOP_RR<0b00100, 0b001010, 0,
                (outs GPR32:$B), (ins GPR32:$C),
                "mov\t$B, $C", []>;

def MOV_rlimm : F32_DOP_RLIMM<0b00100, 0b001010, 0,
                      (outs GPR32:$B), (ins i32imm:$LImm),
                      "mov\t$B, $LImm", []>;

def MOV_ru6 : F32_DOP_RU6<0b00100, 0b001010, 0,
                          (outs GPR32:$B), (ins immU6:$U6),
                          "mov\t$B, $U6", []>;

def MOV_f_ru6 : F32_DOP_RU6<0b00100, 0b001010, 1,
                          (outs GPR32:$B), (ins u6:$U6),
                          "mov.f\t$B, $U6", []> {
  let isAsCheapAsAMove=1;
  let Defs = [STATUS32];
}

def cmov : PatFrag<(ops node:$op1, node:$op2, node:$cc),
                   (ARCcmov $op1, $op2, $cc)>;
let Uses = [STATUS32], isAsCheapAsAMove = 1, isPredicable=1,
	  isReMaterializable = 0, Constraints = "$B = $B2" in {
  def MOV_cc : F32_DOP_CC_RR<0b00100, 0b001010, 0,
                 (outs GPR32:$B), (ins GPR32:$C, GPR32:$B2, CCOp:$cc),
                 "mov.$cc\t$B, $C",
                 [(set GPR32:$B, (cmov i32:$C, i32:$B2, timm:$cc))]>;

  def MOV_cc_ru6 : F32_DOP_CC_RU6<0b00100, 0b001010, 0,
                 (outs GPR32:$B), (ins u6:$C, CCOp:$cc, GPR32:$B2),
                 "mov.$cc\t$B, $C", []>;

  def MOV_cc_f_ru6 : F32_DOP_CC_RU6<0b00100, 0b001010, 1,
                    (outs GPR32:$B), (ins u6:$C, CCOp:$cc, GPR32:$B2),
                    "mov.$cc.f\t$B, $C", []> {
    let Defs = [STATUS32];
  }
}

def : Pat<(ARCGAWrapper tglobaladdr:$addr),
           (MOV_rlimm tglobaladdr:$addr)>;

def : Pat<(ARCGAWrapper tjumptable:$addr),
           (MOV_rlimm tjumptable:$addr)>;


// ---------------------------------------------------------------------------
// Control flow instructions (branch, return, calls, etc).
// ---------------------------------------------------------------------------

// Branch instructions
let isBranch = 1, isTerminator = 1 in {

  // Unconditional branch.
  let isBarrier = 1 in
  def BR : F32_BR0_UCOND_FAR<(outs), (ins btargetS25:$S25),
                             "b\t$S25", [(br bb:$S25)]>;

  let Uses=[STATUS32] in
  // Conditional branch.
  def Bcc : F32_BR0_COND<(outs), (ins btargetS21:$S21, ccond:$cc),
                         "b$cc\t$S21", []>;

  // Compare and branch (limited range).
  def BRcc_rr  : F32_BR1_BCC<(outs),
                             (ins btargetS9:$S9, GPR32:$B, GPR32:$C, brccond:$cc),
                             "br$cc\t$B, $C, $S9", 0, []>;
  def BRcc_ru6 : F32_BR1_BCC<(outs),
                             (ins btargetS9:$S9, GPR32:$B, immU6:$C, brccond:$cc),
                             "br$cc\t$B, $C, $S9", 1, []>;

  // Pseudo compare and branch.
  // After register allocation, this can expand into either a limited range
  // Compare and branch (BRcc), or into CMP + Bcc.
  // At worst, this expands into 2 4-byte instructions.
  def BRcc_rr_p : PseudoInstARC<(outs),
                                (ins btarget:$T, GPR32:$B, GPR32:$C, ccond:$cc),
                                "pbr$cc\t$B, $C, $T",
                                [(ARCbrcc bb:$T, i32:$B, i32:$C, imm32:$cc)]>
                                { let Size = 8; }

  def BRcc_ru6_p : PseudoInstARC<(outs),
                                 (ins btarget:$T, GPR32:$B, i32imm:$C, ccond:$cc),
                                 "pbr$cc\t$B, $C, $T",
                                 [(ARCbrcc bb:$T, i32:$B, immU6:$C, imm32:$cc)]>
                                 { let Size = 8; }
} // let isBranch, isTerminator

// Unconditional Jump.
let isBranch = 1, isTerminator = 1, isBarrier = 1 in {
  // Indirect.
  let isIndirectBranch = 1 in
  def J :  F32_DOP_RR<0b00100, 0b100000, 0,
                      (outs), (ins GPR32:$C),
                      "j\t[$C]", [(brind i32:$C)]>;

  // Direct.
  def J_LImm : F32_DOP_RLIMM<0b00100, 0b100000, 0,
                             (outs), (ins i32imm:$LImm),
                             "j\t$LImm", []>;
}

// Call instructions.
let isCall = 1, isBarrier = 1, Defs = [BLINK], Uses = [SP] in {
  // Direct unconditional call.
  def BL : F32_BR1_BL_UCOND_FAR<(outs), (ins calltargetS25:$S25),
                      "bl\t$S25", [(ARCBranchLink tglobaladdr:$S25)]>;

  // Indirect unconditional call.
  let isIndirectBranch = 1 in
  def JL : F32_DOP_RR<0b00100, 0b100010, 0, (outs), (ins GPR32:$C),
                     "jl\t[$C]", [(ARCJumpLink i32:$C)]>;

  // Direct unconditional call.
  def JL_LImm : F32_DOP_RLIMM<0b00100, 0b100010, 0, (outs), (ins i32imm:$LImm),
                              "jl\t$LImm", []>;
} // let isCall, isBarrier, Defs, Uses

// Pattern to generate BL instruction.
def : Pat<(ARCBranchLink texternalsym:$dst), (BL texternalsym:$dst)>;

// Return from call.
let isReturn = 1, isTerminator = 1, isBarrier = 1  in
// This is a specialized 2-byte instruction that doesn't generalize
// to any larger 2-byte class, so go ahead and define it here.
def J_S_BLINK : InstARC<2, (outs), (ins), "j_s\t[%blink]", [(ret)]> {
  let Inst{15-0} = 0b0111111011100000;
}

//----------------------------------------------------------------------------
// Compact stack-based operations.
//----------------------------------------------------------------------------

// 2-byte push/pop blink instructions commonly used for prolog/epilog
// generation.  These 2 instructions are actually specialized 2-byte
// format instructions that aren't generalized to a larger 2-byte
// class, so we might as well have them here.
let Uses = [BLINK], Defs = [SP] in
def PUSH_S_BLINK : F16_SP_OPS_buconst<0b111, "push_s">;

let Defs = [BLINK, SP] in
def POP_S_BLINK : F16_SP_OPS_buconst<0b110, "pop_s">;

def PUSH_S_r : F16_SP_OPS_uconst<0b110,
  (outs), (ins GPR32Reduced:$b3), "push_s">;
def POP_S_r : F16_SP_OPS_uconst<0b111,
  (outs GPR32Reduced:$b3), (ins), "pop_s">;

def SP_SUB_SP_S : F16_SP_OPS_bconst<0b001, "sub_s">;
def SP_ADD_SP_S : F16_SP_OPS_bconst<0b000, "add_s">;
def SP_ADD_S : F16_SP_OPS_u7_aligned<0b100,
                (outs GPR32Reduced:$b3), (ins immU<7>:$u7),
                "add_s\t$b3, %sp, $u7">;

def SP_LD_S : F16_SP_LD<0b000, "ld_s">;
def SP_LDB_S : F16_SP_LD<0b001, "ldb_s">;
def SP_ST_S : F16_SP_ST<0b010, "st_s">;
def SP_STB_S : F16_SP_ST<0b011, "stb_s">;

def LEAVE_S : F16_SP_OPS<0b110,
  (outs), (ins immU<7>:$u7), "leave_s\t$u7"> {

  bits<7> u7;

  let fieldB = u7{6-4};
  let fieldU{4-1} = u7{3-0};
  let fieldU{0} = 0b0;
}

def ENTER_S : F16_SP_OPS<0b111,
  (outs), (ins immU<6>:$u6), "enter_s\t$u6"> {

  bits<6> u6;

  let fieldB{2} = 0;
  let fieldB{1-0} = u6{5-4};
  let fieldU{4-1} = u6{3-0};
  let fieldU{0} = 0b0;
}

//----------------------------------------------------------------------------
// Compact Move/Load instructions.
//----------------------------------------------------------------------------
class COMPACT_MOV_S :
  F16_COMPACT<0b0, (outs GPR32:$g), (ins GPR32:$h),
          "mov_s\t$g, $h"> {
  let DecoderMethod = "DecodeMoveHRegInstruction";
}

def COMPACT_MOV_S_limm : COMPACT_MOV_S {
  bits<32> LImm;
  let Inst{47-16} = LImm;

  bits<5> LImmReg = 0b11110;
  let Inst{7-5} = LImmReg{2-0};
  let Inst{1-0} = LImmReg{4-3};

  let Size = 6;
}

def COMPACT_MOV_S_hreg : COMPACT_MOV_S;

def COMPACT_LD_S :
  F16_COMPACT<0b1, (outs GPR32:$r), (ins GPR32:$h, immU<5>:$u5),
          "ld_s\t$r, [$h, $u5]"> {
  bits<5> u5;
  bits<2> r;

  let Inst{10} = u5{4};
  let Inst{9-8} = r;
  let Inst{4-3} = u5{3-2};
  let u5{1-0} = 0b00;
}

//----------------------------------------------------------------------------
// Compact Load/Add/Sub.
//----------------------------------------------------------------------------
def LD_S_AS_rrr : F16_LD_SUB<0b0, "ld_s.as\t$a, [$b, $c]">;
def SUB_S_rrr : F16_LD_SUB<0b1, "sub_s\t$a, $b, $c">;
def ADD_S_rru6 : F16_ADD;

//----------------------------------------------------------------------------
// Compact Load/Store.
//----------------------------------------------------------------------------
def LD_S_s11 : F16_LD_ST_s11<0b0, "ld_s\t%r1, [%gp, $s11]">;
def ST_S_s11 : F16_LD_ST_s11<0b1, "st_s\t%r0, [%gp, $s11]">;
def LDI_S_u7 : F16_LDI_u7;

//----------------------------------------------------------------------------
// Indexed Jump or Execute.
//----------------------------------------------------------------------------
def JLI_S : F16_JLI_EI<0, "jli_s">;
def EI_S : F16_JLI_EI<1, "ei_s">;

//----------------------------------------------------------------------------
// Load/Add Register-Register.
//----------------------------------------------------------------------------
def LD_S_rrr : F16_LD_ADD_RR<0b00, "ld_s\t$a, [$b, $c]">;
def LDB_S_rrr : F16_LD_ADD_RR<0b01, "ldb_s\t$a, [$b, $c]">;
def LDH_S_rrr : F16_LD_ADD_RR<0b10, "ldh_s\t$a, [$b, $c]">;
def ADD_S_rrr : F16_LD_ADD_RR<0b11, "add_s\t$a, $b, $c">;

//----------------------------------------------------------------------------
// Load/Add GP-Relative.
//----------------------------------------------------------------------------
def GP_LD_S : F16_GP_LD_ADD<0b00, (ins immS<11>:$s),
  "ld_s\t%r0, [%gp, $s]"> {

  bits<11> s;
  let Inst{8-0} = s{10-2};
  let s{1-0} = 0b00;
}

def GP_LDB_S : F16_GP_LD_ADD<0b01, (ins immS<9>:$s),
  "ldb_s\t%r0, [%gp, $s]"> {

  bits<9> s;
  let Inst{8-0} = s{8-0};
}

def GP_LDH_S : F16_GP_LD_ADD<0b10, (ins immS<10>:$s),
  "ldh_s\t%r0, [%gp, $s]"> {

  bits<10> s;
  let Inst{8-0} = s{9-1};
  let s{0} = 0b0;
}

def GP_ADD_S : F16_GP_LD_ADD<0b11, (ins immS<11>:$s),
  "add_s\t%r0, %gp, $s"> {

  bits<11> s;
  let Inst{8-0} = s{10-2};
  let s{1-0} = 0b00;
}

//----------------------------------------------------------------------------
// Load PCL-Relative.
//----------------------------------------------------------------------------
def PCL_LD : InstARC<2, (outs GPR32:$b), (ins immU<10>:$u10),
 "ld_s\t$b, [%pcl, $u10]", []> {

  bits<3> b;
  bits<10> u10;

  let Inst{15-11} = 0b11010;
  let Inst{10-8} = b;
  let Inst{7-0} = u10{9-2};
  let u10{1-0} = 0b00;
}

let isBranch = 1 in {
  //----------------------------------------------------------------------------
  // Branch on Compare Register with Zero.
  //----------------------------------------------------------------------------
  def BREQ_S : F16_BCC_REG<0b0, "breq_s">;
  def BRNE_S : F16_BCC_REG<0b1, "brne_s">;

  //----------------------------------------------------------------------------
  // Branch Conditionally.
  //----------------------------------------------------------------------------
  let isBarrier = 1 in
  def B_S : F16_BCC_s10<0b00, "b_s">;

  def BEQ_S : F16_BCC_s10<0b01, "beq_s">;
  def BNE_S : F16_BCC_s10<0b10, "bne_s">;
  def BGT_S : F16_BCC_s7<0b000, "bgt_s">;
  def BGE_S : F16_BCC_s7<0b001, "bge_s">;
  def BLT_S : F16_BCC_s7<0b010, "blt_s">;
  def BLE_S : F16_BCC_s7<0b011, "ble_s">;
  def BHI_S : F16_BCC_s7<0b100, "bhi_s">;
  def BHS_S : F16_BCC_s7<0b101, "bhs_s">;
  def BLO_S : F16_BCC_s7<0b110, "blo_s">;
  def BLS_S : F16_BCC_s7<0b111, "bls_s">;
} // let isBranch

def BL_S :
  InstARC<2, (outs), (ins btargetS13:$s13), "bl_s\t$s13", []> {

  let Inst{15-11} = 0b11111;

  bits<13> s13;
  let Inst{10-0} = s13{12-2};
  let s13{1-0} = 0b00;

  let isCall = 1;
  let isBarrier = 1;
}

//----------------------------------------------------------------------------
// Add/Sub/Shift Register-Immediate.
//----------------------------------------------------------------------------
def ADD_S_ru3 : F16_ADD_IMM<0b00,"add_s">;
def SUB_S_ru3 : F16_ADD_IMM<0b01,"sub_s">;
def ASL_S_ru3 : F16_ADD_IMM<0b10,"asl_s">;
def ASR_S_ru3 : F16_ADD_IMM<0b11,"asr_s">;

//----------------------------------------------------------------------------
// Shift/Subtract/Bit Immediate.
//----------------------------------------------------------------------------
def ASL_S_ru5 : F16_SH_SUB_BIT_DST<0b000,"asl_s">;
def LSR_S_ru5 : F16_SH_SUB_BIT_DST<0b001,"lsr_s">;
def ASR_S_ru5 : F16_SH_SUB_BIT_DST<0b010,"asr_s">;
def SUB_S_ru5 : F16_SH_SUB_BIT_DST<0b011,"sub_s">;
def BSET_S_ru5 : F16_SH_SUB_BIT_DST<0b100,"bset_s">;
def BCLR_S_ru5 : F16_SH_SUB_BIT_DST<0b101,"bclr_s">;
def BMSK_S_ru5 : F16_SH_SUB_BIT_DST<0b110,"bmsk_s">;
def BTST_S_ru5 : F16_SH_SUB_BIT<0b111, "btst_s\t$b, $u5">;

//----------------------------------------------------------------------------
// Dual Register Operations.
//----------------------------------------------------------------------------
def ADD_S_rlimm :
  F16_OP_HREG_LIMM<0b000, (outs GPR32:$b_s3), (ins i32imm:$LImm),
          !strconcat("add_s", "\t$b_s3, $b_s3, $LImm")>;

def ADD_S_rr :
  F16_OP_HREG<0b000, (outs GPR32:$b_s3), (ins GPR32:$h),
          !strconcat("add_s", "\t$b_s3, $b_s3, $h")>;

def ADD_S_rs3 :
  F16_OP_HREG<0b001, (outs GPR32:$h), (ins immC<3>:$b_s3),
          !strconcat("add_s", "\t$h, $h, $b_s3")>;

def ADD_S_limms3 :
  F16_OP_HREG_LIMM<0b001, (outs), (ins immC<3>:$b_s3, i32imm:$LImm),
          !strconcat("add_s", "\t0, $LImm, $b_s3")>;

def MOV_S_NE_rlimm :
  F16_OP_HREG_LIMM<0b111, (outs GPR32:$b_s3), (ins i32imm:$LImm),
          !strconcat("mov_s.ne", "\t$b_s3, $LImm")>;

def MOV_S_NE_rr :
  F16_OP_HREG<0b111,(outs GPR32:$b_s3), (ins GPR32:$h),
          !strconcat("mov_s.ne", "\t$b_s3, $h")>;

def MOV_S_rs3 :
  F16_OP_HREG<0b011, (outs GPR32:$h), (ins immC<3>:$b_s3),
          !strconcat("mov_s", "\t$h, $b_s3")>;

def MOV_S_s3 :
  F16_OP_HREG30<0b011, (outs), (ins immC<3>:$b_s3),
          !strconcat("mov_s", "\t0, $b_s3")>;

def CMP_S_rlimm :
  F16_OP_HREG_LIMM<0b100, (outs GPR32:$b_s3), (ins i32imm:$LImm),
          !strconcat("cmp_s", "\t$b_s3, $LImm")>;

def CMP_S_rr :
  F16_OP_HREG<0b100, (outs GPR32:$b_s3), (ins GPR32:$h),
          !strconcat("cmp_s", "\t$b_s3, $h")>;

def CMP_S_rs3 :
  F16_OP_HREG<0b101, (outs GPR32:$h), (ins immC<3>:$b_s3),
          !strconcat("cmp_s", "\t$h, $b_s3")>;

def CMP_S_limms3 :
  F16_OP_HREG_LIMM<0b101, (outs), (ins immC<3>:$b_s3, i32imm:$LImm),
          !strconcat("cmp_s", "\t$LImm, $b_s3")>;

//----------------------------------------------------------------------------
// Compact MOV/ADD/CMP Immediate instructions.
//----------------------------------------------------------------------------
def MOV_S_u8 :
  F16_OP_IMM<0b11011, (outs GPR32:$b), (ins immU<8>:$u8),
          !strconcat("mov_s", "\t$b, $u8")> {
  bits<8> u8;
  let Inst{7-0} = u8;
}

def ADD_S_u7 :
  F16_OP_U7<0b0, !strconcat("add_s", "\t$b, $b, $u7")>;

def CMP_S_u7 :
  F16_OP_U7<0b1, !strconcat("cmp_s", "\t$b, $u7")>;

//----------------------------------------------------------------------------
// Compact Load/Store instructions with offset.
//----------------------------------------------------------------------------
def LD_S_OFF :
  F16_LD_ST_WORD_OFF<0x10, (outs GPR32:$c), (ins GPR32:$b, immU<7>:$off),
  "ld_s">;

def LDB_S_OFF :
  F16_LD_ST_BYTE_OFF<0x11, (outs GPR32:$c), (ins GPR32:$b, immU<5>:$off),
  "ldb_s">;

class F16_LDH_OFF<bits<5> opc, string asmstr> :
  F16_LD_ST_HALF_OFF<opc, (outs GPR32:$c), (ins GPR32:$b, immU<6>:$off),
  asmstr>;

def LDH_S_OFF : F16_LDH_OFF<0x12, "ldh_s">;
def LDH_S_X_OFF : F16_LDH_OFF<0x13, "ldh_s.x">;

def ST_S_OFF :
  F16_LD_ST_WORD_OFF<0x14, (outs), (ins GPR32:$c, GPR32:$b, immU<7>:$off),
  "st_s">;

def STB_S_OFF :
  F16_LD_ST_BYTE_OFF<0x15, (outs), (ins GPR32:$c, GPR32:$b, immU<5>:$off),
  "stb_s">;

def STH_S_OFF :
  F16_LD_ST_HALF_OFF<0x16, (outs), (ins GPR32:$c, GPR32:$b, immU<6>:$off),
  "sth_s">;

//----------------------------------------------------------------------------
// General compact instructions.
//----------------------------------------------------------------------------
def GEN_SUB_S : F16_GEN_DOP<0x02, "sub_s">;
def GEN_AND_S : F16_GEN_DOP<0x04, "and_s">;
def GEN_OR_S : F16_GEN_DOP<0x05, "or_s">;
def GEN_BIC_S : F16_GEN_DOP<0x06, "bic_s">;
def GEN_XOR_S : F16_GEN_DOP<0x07, "xor_s">;
def GEN_MPYW_S : F16_GEN_DOP<0x09, "mpyw_s">;
def GEN_MPYUW_S : F16_GEN_DOP<0x0a, "mpyuw_s">;
def GEN_TST_S : F16_GEN_DOP_NODST<0x0b, "tst_s">;
def GEN_MPY_S : F16_GEN_DOP<0x0c, "mpy_s">;
def GEN_SEXB_S : F16_GEN_DOP_SINGLESRC<0x0d, "sexb_s">;
def GEN_SEXH_S : F16_GEN_DOP_SINGLESRC<0x0e, "sexh_s">;
def GEN_EXTB_S : F16_GEN_DOP_SINGLESRC<0x0f, "extb_s">;
def GEN_EXTH_S : F16_GEN_DOP_SINGLESRC<0x10, "exth_s">;
def GEN_ABS_S : F16_GEN_DOP_SINGLESRC<0x11, "abs_s">;
def GEN_NOT_S : F16_GEN_DOP_SINGLESRC<0x12, "not_s">;
def GEN_NEG_S : F16_GEN_DOP_SINGLESRC<0x13, "neg_s">;
def GEN_ADD1_S : F16_GEN_DOP<0x14, "add1_s">;
def GEN_ADD2_S : F16_GEN_DOP<0x15, "add2_s">;
def GEN_ADD3_S : F16_GEN_DOP<0x16, "add3_s">;
def GEN_ASL_S : F16_GEN_DOP<0x18, "asl_s">;
def GEN_LSR_S : F16_GEN_DOP<0x19, "lsr_s">;
def GEN_ASR_S : F16_GEN_DOP<0x1a, "asr_s">;
def GEN_AS1L_S : F16_GEN_DOP_SINGLESRC<0x1b, "asl_s">;
def GEN_AS1R_S : F16_GEN_DOP_SINGLESRC<0x1c, "asr_s">;
def GEN_LS1R_S : F16_GEN_DOP_SINGLESRC<0x1d, "lsr_s">;
def GEN_TRAP_S : F16_GEN_DOP_BASE<0x1e, (outs), (ins immU6:$u6),
  "trap_s\t$u6"> {

  bits<6> u6;
  let b = u6{5-3};
  let c = u6{2-0};
}

def GEN_BRK_S : F16_GEN_DOP_BASE<0x1f, (outs), (ins),
  "brk_s"> {

  let b = 0b111;
  let c = 0b111;
}

let isBarrier = 1 in {
  let isBranch = 1 in {
    def GEN_J_S : F16_GEN_SOP<0x0, "j_s\t[$b]">;
    def GEN_J_S_D : F16_GEN_SOP<0x1, "j_s.d\t[$b]">;
  } // let isBranch

  let isCall = 1 in {
    def GEN_JL_S : F16_GEN_SOP<0x2, "jl_s\t[$b]">;
    def GEN_JL_S_D : F16_GEN_SOP<0x3, "jl_s.d\t[$b]">;
  } // let isCall
} // let isBarrier

def GEN_SUB_S_NE : F16_GEN_SOP<0x6, "sub_s.ne\t$b, $b, $b">;

def GEN_NOP_S : F16_GEN_ZOP<0x0, "nop_s">;
def GEN_UNIMP_S : F16_GEN_ZOP<0x1, "unimp_s">;
def GEN_SWI_S : F16_GEN_ZOP<0x2, "swi_s">;

let isReturn = 1, isTerminator = 1 in {
  def GEN_JEQ_S : F16_GEN_ZOP<0x4, "jeq_s\t[%blink]">;
  def GEN_JNE_S : F16_GEN_ZOP<0x5, "jne_s\t[%blink]">;
  let isBarrier = 1 in {
    //def GEN_J_S_BLINK : F16_GEN_ZOP<0x6, "j_s\t[%blink]">;
    def GEN_J_S_D_BLINK : F16_GEN_ZOP<0x7, "j_s.d\t[%blink]">;
  } // let isBarrier
} // let isReturn, isTerminator

//----------------------------------------------------------------------------
// Load/Store instructions.
//----------------------------------------------------------------------------

// Filter  class for load/store mappings
class ArcLdStRel;

// Load instruction variants:
// Control bits: x, aa, di, zz
// x - sign extend.
// aa - incrementing mode. (N/A for LIMM).
// di - uncached.
// zz - data size.
multiclass ArcLdInst<DataSizeMode zz, ExtMode x, CacheMode di, string asmop> {
  let mayLoad = 1, ZZ = zz, X = x, DI = di in {
    def _rs9: F32_LD_ADDR<x.Value, NoAM.Value, di.Value, zz.Value,
			   (outs GPR32:$A), (ins MEMrs9:$addr),
			   !strconcat(asmop, "\t$A, [$addr]"), []>, ArcLdStRel;

    def _limm: F32_LD_LIMM<x.Value, di.Value, zz.Value,
			   (outs GPR32:$A), (ins MEMii:$addr),
			   !strconcat(asmop, "\t$A, [$addr]"), []>, ArcLdStRel;

    def _rlimm: F32_LD_RLIMM<x.Value, NoAM.Value, di.Value, zz.Value,
			     (outs GPR32:$A), (ins MEMrlimm:$addr),
			     !strconcat(asmop, "\t$A, [$addr]"), []>, ArcLdStRel;

    foreach aa = [PreIncAM, PostIncAM] in {
      def aa.InstSuffix#_rs9: F32_LD_RS9<x.Value, aa.Value, di.Value, zz.Value,
					  (outs GPR32:$A, GPR32:$addrout),
					  (ins GPR32:$B, immS<9>:$S9),
					  asmop#aa.AsmSuffix#"\t$A, [$B,$S9]", []>, ArcLdStRel
			       { let Constraints = "$addrout = $B"; let AA = aa; }
    }
  }
}

foreach di = [NoCC, UncachedCC] in {
  defm LD#di.InstSuffix : ArcLdInst<WordSM, NoEM, di, "ld"#di.AsmSuffix>;
  foreach zz = [ByteSM, HalfSM] in {
    foreach x = [NoEM, SignedEM] in {
      defm LD#zz.InstSuffix#x.InstSuffix#di.InstSuffix : ArcLdInst<zz, x, di, "ld"#zz.AsmSuffix#x.AsmSuffix#di.AsmSuffix>;
    }
  }
}

// Load instruction patterns.
// 32-bit loads.
def : Pat<(load AddrModeS9:$addr), (LD_rs9 AddrModeS9:$addr)>;
def : Pat<(load AddrModeImm:$addr), (LD_limm AddrModeImm:$addr)>;
def : Pat<(load AddrModeFar:$addr), (LD_rs9 AddrModeFar:$addr)>;

// 16-bit loads
def : Pat<(zextloadi16 AddrModeS9:$addr), (LDH_rs9 AddrModeS9:$addr)>;
def : Pat<(extloadi16 AddrModeS9:$addr), (LDH_rs9 AddrModeS9:$addr)>;
def : Pat<(zextloadi16 AddrModeImm:$addr), (LDH_limm AddrModeImm:$addr)>;
def : Pat<(extloadi16 AddrModeImm:$addr), (LDH_limm AddrModeImm:$addr)>;
def : Pat<(zextloadi16 AddrModeFar:$addr), (LDH_rlimm AddrModeFar:$addr)>;
def : Pat<(extloadi16 AddrModeFar:$addr), (LDH_rlimm AddrModeFar:$addr)>;
def : Pat<(sextloadi16 AddrModeImm:$addr),(LDH_X_limm AddrModeImm:$addr)>;
def : Pat<(sextloadi16 AddrModeFar:$addr),(LDH_X_rlimm AddrModeFar:$addr)>;
def : Pat<(sextloadi16 AddrModeS9:$addr),(LDH_X_rs9 AddrModeS9:$addr)>;

// 8-bit loads.
def : Pat<(zextloadi8 AddrModeS9:$addr), (LDB_rs9 AddrModeS9:$addr)>;
def : Pat<(extloadi8 AddrModeS9:$addr), (LDB_rs9 AddrModeS9:$addr)>;
def : Pat<(zextloadi8 AddrModeImm:$addr), (LDB_limm AddrModeImm:$addr)>;
def : Pat<(extloadi8 AddrModeImm:$addr), (LDB_limm AddrModeImm:$addr)>;
def : Pat<(zextloadi8 AddrModeFar:$addr), (LDB_rlimm AddrModeFar:$addr)>;
def : Pat<(extloadi8 AddrModeFar:$addr), (LDB_rlimm AddrModeFar:$addr)>;
def : Pat<(zextloadi1 AddrModeS9:$addr), (LDB_rs9 AddrModeS9:$addr)>;
def : Pat<(extloadi1 AddrModeS9:$addr), (LDB_rs9 AddrModeS9:$addr)>;
def : Pat<(zextloadi1 AddrModeImm:$addr), (LDB_limm AddrModeImm:$addr)>;
def : Pat<(extloadi1 AddrModeImm:$addr), (LDB_limm AddrModeImm:$addr)>;
def : Pat<(zextloadi1 AddrModeFar:$addr), (LDB_rlimm AddrModeFar:$addr)>;
def : Pat<(extloadi1 AddrModeFar:$addr), (LDB_rlimm AddrModeFar:$addr)>;
def : Pat<(sextloadi8 AddrModeImm:$addr),(LDB_X_limm AddrModeImm:$addr)>;
def : Pat<(sextloadi8 AddrModeFar:$addr),(LDB_X_rlimm AddrModeFar:$addr)>;
def : Pat<(sextloadi8 AddrModeS9:$addr),(LDB_X_rs9 AddrModeS9:$addr)>;


// Store instruction variants:
// Control bits: aa, di, zz
// aa - incrementing mode. (N/A for LIMM).
// di - uncached.
// zz - data size.
multiclass ArcStInst<DataSizeMode zz, CacheMode di, string asmop> {
  let mayStore = 1, ZZ = zz, DI = di in {
    def _rs9: F32_ST_ADDR<NoAM.Value, di.Value, zz.Value,
			   (outs), (ins GPR32:$C, MEMrs9:$addr),
			   !strconcat(asmop, "\t$C, [$addr]"), []>, ArcLdStRel;

    def _limm: F32_ST_LIMM<di.Value, zz.Value,
			   (outs), (ins GPR32:$C, MEMii:$addr),
			   !strconcat(asmop, "\t$C, [$addr]"), []>, ArcLdStRel;


    foreach aa = [PreIncAM, PostIncAM] in {
      def aa.InstSuffix#_rs9: F32_ST_RS9<aa.Value, di.Value, zz.Value,
					  (outs GPR32:$addrout),
					  (ins GPR32:$C, GPR32:$B, immS<9>:$S9),
					  asmop#aa.AsmSuffix#"\t$C, [$B,$S9]", []>, ArcLdStRel
			       { let Constraints = "$addrout = $B"; let AA = aa; }
    }
  }
}

foreach di = [NoCC, UncachedCC] in {
  foreach zz = [ByteSM, HalfSM, WordSM] in {
      defm ST#zz.InstSuffix#di.InstSuffix : ArcStInst<zz, di, "st"#zz.AsmSuffix#di.AsmSuffix>;
  }
}

// Store instruction patterns.
// 32-bit stores
def : Pat<(store i32:$C, AddrModeS9:$addr),
          (ST_rs9 i32:$C, AddrModeS9:$addr)>;
def : Pat<(store i32:$C, AddrModeImm:$addr),
          (ST_limm i32:$C, AddrModeImm:$addr)>;

// 16-bit stores
def : Pat<(truncstorei16 i32:$C, AddrModeS9:$addr),
          (STH_rs9 i32:$C, AddrModeS9:$addr)>;
def : Pat<(truncstorei16 i32:$C, AddrModeImm:$addr),
          (STH_limm i32:$C, AddrModeImm:$addr)>;

// 8-bit stores
def : Pat<(truncstorei8 i32:$C, AddrModeS9:$addr),
          (STB_rs9 i32:$C, AddrModeS9:$addr)>;
def : Pat<(truncstorei8 i32:$C, AddrModeImm:$addr),
          (STB_limm i32:$C, AddrModeImm:$addr)>;

def getPostIncOpcode : InstrMapping {
  let FilterClass = "ArcLdStRel";
  let RowFields = [ "BaseOpcode", "ZZ", "DI", "X"];
  let ColFields = [ "AA" ];
  let KeyCol = [ "NoAM" ];
  let ValueCols = [["PostIncAM"]];
}
