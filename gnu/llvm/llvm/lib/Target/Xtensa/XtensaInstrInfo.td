//===- XtensaInstrInfo.td - Target Description for Xtensa -*- tablegen -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the Xtensa instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "XtensaInstrFormats.td"
include "XtensaOperands.td"
include "XtensaOperators.td"

//===----------------------------------------------------------------------===//
// Arithmetic & Logical instructions
//===----------------------------------------------------------------------===//

class ArithLogic_RRR<bits<4> oper2, bits<4> oper1, string instrAsm,
      SDPatternOperator opNode, bit isComm = 0>
  : RRR_Inst<0x00, oper1, oper2, (outs AR:$r), (ins AR:$s, AR:$t),
             instrAsm#"\t$r, $s, $t",
            [(set AR:$r, (opNode AR:$s, AR:$t))]> {
  let isCommutable = isComm;
  let isReMaterializable = 0;
}

def ADD : ArithLogic_RRR<0x08, 0x00, "add", add, 1>;
def SUB : ArithLogic_RRR<0x0C, 0x00, "sub", sub>;
def AND : ArithLogic_RRR<0x01, 0x00, "and", and, 1>;
def OR  : ArithLogic_RRR<0x02, 0x00, "or", or, 1>;
def XOR : ArithLogic_RRR<0x03, 0x00, "xor", xor, 1>;

class ADDX<bits<4> oper, string instrAsm, list<dag> pattern>
  : RRR_Inst<0x00, 0x00, oper, (outs AR:$r), (ins AR:$s, AR:$t),
             instrAsm#"\t$r, $s, $t", pattern>;

def ADDX2 : ADDX<0x09, "addx2", [(set AR:$r, (add AR:$t, (shl AR:$s, (i32 1))))]>;
def ADDX4 : ADDX<0x0A, "addx4", [(set AR:$r, (add AR:$t, (shl AR:$s, (i32 2))))]>;
def ADDX8 : ADDX<0x0B, "addx8", [(set AR:$r, (add AR:$t, (shl AR:$s, (i32 3))))]>;

class SUBX<bits<4> oper, string instrAsm, list<dag> pattern>
  : RRR_Inst<0x00, 0x00, oper, (outs AR:$r), (ins AR:$s, AR:$t),
             instrAsm#"\t$r, $s, $t", pattern>;

def SUBX2 : SUBX<0x0D, "subx2", [(set AR:$r, (sub (shl AR:$s, (i32 1)), AR:$t))]>;
def SUBX4 : SUBX<0x0E, "subx4", [(set AR:$r, (sub (shl AR:$s, (i32 2)), AR:$t))]>;
def SUBX8 : SUBX<0x0F, "subx8", [(set AR:$r, (sub (shl AR:$s, (i32 3)), AR:$t))]>;

def ABS : RRR_Inst<0x00, 0x00, 0x06, (outs AR:$r), (ins AR:$t),
                  "abs\t$r, $t", []> {
  let s = 0x1;
}

def ADDI : RRI8_Inst<0x02, (outs AR:$t), (ins AR:$s, imm8:$imm8),
                    "addi\t$t, $s, $imm8",
                    [(set AR:$t, (add AR:$s, imm8:$imm8))]> {
  let r = 0x0C;
}

def ADDMI : RRI8_Inst<0x02, (outs AR:$t), (ins AR:$s, imm8_sh8:$imm_sh8),
                     "addmi\t$t, $s, $imm_sh8",
                     [(set AR:$t, (add AR:$s, imm8_sh8:$imm_sh8))]> {
  bits<16> imm_sh8;

  let r = 0x0D;
  let imm8 = imm_sh8{15-8};
}

def NEG : RRR_Inst<0x00, 0x00, 0x06, (outs AR:$r), (ins AR:$t),
                  "neg\t$r, $t",
                  [(set AR:$r, (ineg AR:$t))]> {
  let s = 0x00;
}

//===----------------------------------------------------------------------===//
// Move instructions
//===----------------------------------------------------------------------===//
def MOVI : RRI8_Inst<0x02, (outs AR:$t), (ins imm12m:$imm),
                    "movi\t$t, $imm",
                    [(set AR:$t, imm12m:$imm)]> {
  bits<12> imm;

  let imm8{7-0} = imm{7-0};
  let s{3-0} = imm{11-8};
  let r = 0xa;
}

def MOVEQZ : RRR_Inst<0x00, 0x03, 0x08, (outs AR:$r), (ins AR:$s, AR:$t),
                     "moveqz\t$r, $s, $t", []>;
def MOVNEZ : RRR_Inst<0x00, 0x03, 0x09, (outs AR:$r), (ins AR:$s, AR:$t),
                     "movnez\t$r, $s, $t", []>;
def MOVLTZ : RRR_Inst<0x00, 0x03, 0x0A, (outs AR:$r), (ins AR:$s, AR:$t),
                     "movltz\t$r, $s, $t", []>;
def MOVGEZ : RRR_Inst<0x00, 0x03, 0x0B, (outs AR:$r), (ins AR:$s, AR:$t),
                     "movgez\t$r, $s, $t", []>;

//===----------------------------------------------------------------------===//
// Shift instructions
//===----------------------------------------------------------------------===//

let Uses = [SAR] in {
  def SLL : RRR_Inst<0x00, 0x01, 0x0A, (outs AR:$r), (ins AR:$s),
                    "sll\t$r, $s", []> {
    let t = 0x00;
  }

  def SRA : RRR_Inst<0x00, 0x01, 0x0B, (outs AR:$r), (ins AR:$t),
                    "sra\t$r, $t", []> {
    let s = 0x00;
  }

  def SRC : RRR_Inst<0x00, 0x01, 0x08, (outs AR:$r), (ins AR:$s, AR:$t),
                    "src\t$r, $s, $t", []>;

  def SRL : RRR_Inst<0x00, 0x01, 0x09, (outs AR:$r), (ins AR:$t),
                    "srl\t$r, $t", []> {
    let s = 0x00;
  }
}

let Defs = [SAR] in {
  def SSL : RRR_Inst<0x00, 0x00, 0x04, (outs), (ins AR:$s),
                    "ssl\t$s", []> {
    let r = 0x01;
    let t = 0x00;
  }

  def SSR : RRR_Inst<0x00, 0x00, 0x04, (outs), (ins AR:$s),
                    "ssr\t$s", []> {
    let r = 0x00;
    let t = 0x00;
  }
}

def EXTUI : RRR_Inst<0x00, 0x04, 0x00, (outs AR:$r), (ins AR:$t, uimm5:$imm1, imm1_16:$imm2),
                    "extui\t$r, $t, $imm1, $imm2", []> {
  bits<5> imm1;
  bits<4> imm2;

  let s = imm1{3-0};
  let Inst{16} = imm1{4};
  let Inst{23-20} = imm2;
}

def SRAI : RRR_Inst<0x00, 0x01, 0x02, (outs AR:$r), (ins AR:$t, uimm5:$sa),
                   "srai\t$r, $t, $sa",
                   [(set AR:$r, (sra AR:$t, uimm5:$sa))]> {
  bits<5> sa;

  let Inst{20} = sa{4};
  let s = sa{3-0};
}

def SRLI : RRR_Inst<0x00, 0x01, 0x04, (outs AR:$r), (ins AR:$t, uimm4:$sa),
                   "srli\t$r, $t, $sa",
                   [(set AR:$r, (srl AR:$t, uimm4:$sa))]> {
  bits<4> sa;

  let s = sa;
}

def SLLI : RRR_Inst<0x00, 0x01, 0x00, (outs AR:$r), (ins AR:$s, shimm1_31:$sa),
                   "slli\t$r, $s, $sa",
                   [(set AR:$r, (shl AR:$s, shimm1_31:$sa))]> {
  bits<5> sa;

  let Inst{20} = sa{4};
  let t = sa{3-0};
}

def SSA8L : RRR_Inst<0x00, 0x00, 0x04, (outs), (ins AR:$s),
                    "ssa8l\t$s", []> {
  let r = 0x2;
  let t = 0x0;
}

def SSAI : RRR_Inst<0x00, 0x00, 0x04, (outs), (ins uimm5:$imm),
                   "ssai\t$imm", []> {
  bits<5> imm;

  let r = 0x04;
  let s = imm{3-0};
  let t{3-1} = 0;
  let t{0} = imm{4};
}

//===----------------------------------------------------------------------===//
// Load and store instructions
//===----------------------------------------------------------------------===//

// Load instructions
let mayLoad = 1 in {

  class Load_RRI8<bits<4> oper, string instrAsm, SDPatternOperator opNode,
        ComplexPattern addrOp, Operand memOp>
	  : RRI8_Inst<0x02, (outs AR:$t), (ins memOp:$addr),
                instrAsm#"\t$t, $addr",
               [(set AR:$t, (opNode addrOp:$addr))]> {
    bits<12> addr;

    let r = oper;
    let imm8{7-0} = addr{11-4};
    let s{3-0} = addr{3-0};
  }
}

def L8UI  : Load_RRI8<0x00, "l8ui", zextloadi8, addr_ish1, mem8>;
def L16SI : Load_RRI8<0x09, "l16si", sextloadi16, addr_ish2, mem16>;
def L16UI : Load_RRI8<0x01, "l16ui", zextloadi16, addr_ish2, mem16>;
def L32I  : Load_RRI8<0x02, "l32i", load, addr_ish4, mem32>;

// Store instructions
let mayStore = 1 in {
  class Store_II8<bits<4> oper, string instrAsm, SDPatternOperator opNode,
        ComplexPattern addrOp, Operand memOp>
	  : RRI8_Inst<0x02, (outs), (ins AR:$t, memOp:$addr),
                instrAsm#"\t$t, $addr",
               [(opNode AR:$t, addrOp:$addr)]> {
    bits<12> addr;

    let r = oper;
    let imm8{7-0} = addr{11-4};
    let s{3-0} = addr{3-0};
  }
}

def S8I  : Store_II8<0x04, "s8i", truncstorei8, addr_ish1, mem8>;
def S16I : Store_II8<0x05, "s16i", truncstorei16, addr_ish2, mem16>;
def S32I : Store_II8<0x06, "s32i", store, addr_ish4, mem32>;

def L32R : RI16_Inst<0x01, (outs AR:$t), (ins L32Rtarget:$label),
                    "l32r\t$t, $label", []> {
  bits<16> label;
  let imm16 = label;
}

// pcrel addr loading using L32R
def : Pat<(Xtensa_pcrel_wrapper tconstpool : $in), (L32R tconstpool : $in)>;

// FrameIndexes are legalized when they are operands from load/store
// instructions. The same not happens for stack address copies, so an
// add op with mem ComplexPattern is used and the stack address copy
// can be matched.
// Setting of attribute mayLoad is trick to process instruction operands
// in function XtensaRegisterInfo::eliminateFI

let isCodeGenOnly = 1, mayLoad = 1 in {

  def LEA_ADD : RRI8_Inst<0x02, (outs AR:$t), (ins mem32:$addr),
       "addi\t$t, $addr",
       [(set AR:$t, addr_ish4:$addr)]> {
    bits<12> addr;

    let r = 0x0C;
    let imm8{7-0} = addr{11-4};
    let s{3-0} = addr{3-0};
  }
}

//extending loads
def : Pat<(i32 (extloadi1  addr_ish1:$addr)), (L8UI addr_ish1:$addr)>;
def : Pat<(i32 (extloadi8  addr_ish1:$addr)), (L8UI addr_ish1:$addr)>;
def : Pat<(i32 (extloadi16 addr_ish2:$addr)), (L16UI addr_ish2:$addr)>;

//===----------------------------------------------------------------------===//
// Conditional branch instructions
//===----------------------------------------------------------------------===//
let isBranch = 1, isTerminator = 1 in {
  class Branch_RR<bits<4> oper, string instrAsm, CondCode CC>
      : RRI8_Inst<0x07, (outs),
                 (ins AR:$s, AR:$t, brtarget:$target),
                  instrAsm#"\t$s, $t, $target",
                 [(brcc CC, AR:$s, AR:$t,  bb:$target)]> {
    bits<8> target;

    let r = oper;
    let imm8 = target;
  }

  class Branch_RI<bits<4> oper, string instrAsm, CondCode CC>
      : RRI8_Inst<0x06, (outs),
                 (ins AR:$s, b4const:$imm, brtarget:$target),
                  instrAsm#"\t$s, $imm, $target",
                 [(brcc CC, AR:$s, b4const:$imm,  bb:$target)]> {
    bits<4> imm;
    bits<8> target;

    let t = oper;
    let r = imm;
    let imm8 = target;
  }

  class Branch_RIU<bits<4> oper, string instrAsm, CondCode CC>
    : RRI8_Inst<0x06, (outs),
               (ins AR:$s, b4constu:$imm, brtarget:$target),
                instrAsm#"\t$s, $imm, $target",
               [(brcc CC, AR:$s, b4constu:$imm,  bb:$target)]> {
    bits<4> imm;
    bits<8> target;

    let t = oper;
    let r = imm;
    let imm8 = target;
  }

  class Branch_RZ<bits<2> n, bits<2> m, string instrAsm, CondCode CC>
    : BRI12_Inst<0x06, n, m, (outs),
                (ins AR:$s, brtarget:$target),
                 instrAsm#"\t$s, $target",
                [(brcc CC, AR:$s, (i32 0),  bb:$target)]> {
    bits<12> target;

    let imm12 = target;
  }
}

def BEQ   : Branch_RR<0x01, "beq", SETEQ>;
def BNE   : Branch_RR<0x09, "bne", SETNE>;
def BGE   : Branch_RR<0x0A, "bge", SETGE>;
def BLT   : Branch_RR<0x02, "blt", SETLT>;
def BGEU  : Branch_RR<0x0B, "bgeu", SETUGE>;
def BLTU  : Branch_RR<0x03, "bltu", SETULT>;

def BEQI  : Branch_RI<0x02, "beqi", SETEQ>;
def BNEI  : Branch_RI<0x06, "bnei", SETNE>;
def BGEI  : Branch_RI<0x0E, "bgei", SETGE>;
def BLTI  : Branch_RI<0x0A, "blti", SETLT>;
def BGEUI : Branch_RIU<0x0F, "bgeui", SETUGE>;
def BLTUI : Branch_RIU<0x0B, "bltui", SETULT>;

def BEQZ  : Branch_RZ<0x01, 0x00, "beqz", SETEQ>;
def BNEZ  : Branch_RZ<0x01, 0x01, "bnez", SETNE>;
def BGEZ  : Branch_RZ<0x01, 0x03, "bgez", SETGE>;
def BLTZ  : Branch_RZ<0x01, 0x02, "bltz", SETLT>;

def BALL : RRI8_Inst<0x07, (outs),
                    (ins AR:$s, AR:$t, brtarget:$target),
                    "ball\t$s, $t, $target", []> {
  bits<8> target;

  let r = 0x04;
  let imm8 = target;
}

def BANY : RRI8_Inst<0x07, (outs),
                    (ins AR:$s, AR:$t, brtarget:$target),
                    "bany\t$s, $t, $target", []> {
  bits<8> target;

  let r = 0x08;
  let imm8 = target;
}

def BBC : RRI8_Inst<0x07, (outs),
                   (ins AR:$s, AR:$t, brtarget:$target),
                   "bbc\t$s, $t, $target", []> {
  bits<8> target;

  let r = 0x05;
  let imm8 = target;
}

def BBS : RRI8_Inst<0x07, (outs),
                   (ins AR:$s, AR:$t, brtarget:$target),
                   "bbs\t$s, $t, $target", []> {
  bits<8> target;

  let r = 0x0d;
  let imm8 = target;
}

def BNALL : RRI8_Inst<0x07, (outs),
                    (ins AR:$s, AR:$t, brtarget:$target),
                    "bnall\t$s, $t, $target", []> {
  bits<8> target;

  let r = 0x0c;
  let imm8 = target;
}

def BNONE : RRI8_Inst<0x07, (outs),
                     (ins AR:$s, AR:$t, brtarget:$target),
                     "bnone\t$s, $t, $target", []> {
  bits<8> target;

  let r = 0x00;
  let imm8 = target;
}

def BBCI : RRI8_Inst<0x07, (outs),
                    (ins AR:$s, uimm5:$imm, brtarget:$target),
                    "bbci\t$s, $imm, $target", []> {
  bits<8> target;
  bits<5> imm;

  let r{3-1} = 0x3;
  let r{0} = imm{4};
  let t{3-0} = imm{3-0};
  let imm8 = target;
}

def BBSI : RRI8_Inst<0x07, (outs),
                    (ins AR:$s, uimm5:$imm, brtarget:$target),
                    "bbsi\t$s, $imm, $target", []> {
  bits<8> target;
  bits<5> imm;

  let r{3-1} = 0x7;
  let r{0} = imm{4};
  let t{3-0} = imm{3-0};
  let imm8 = target;
}

//===----------------------------------------------------------------------===//
// Call and jump instructions
//===----------------------------------------------------------------------===//

let isBranch = 1, isTerminator = 1, isBarrier = 1 in {
  def J : CALL_Inst<0x06, (outs), (ins jumptarget:$offset),
                   "j\t$offset",
                   [(br bb:$offset)]> {
    let n = 0x0;
  }

  def JX : CALLX_Inst<0x00, 0x00, 0x00, (outs), (ins AR:$s),
                     "jx\t$s",
                     [(brind AR:$s)]> {
    let m = 0x2;
    let n = 0x2;
    let r = 0;
    let isIndirectBranch = 1;
  }
}

let isCall = 1, Defs = [A0] in {
  def CALL0 : CALL_Inst<0x05, (outs), (ins pcrel32call:$offset),
                       "call0\t$offset", []> {
    let n = 0;
  }

  def CALLX0 : CALLX_Inst<0x00, 0x00, 0x00, (outs), (ins AR:$s),
                         "callx0\t$s", []> {
    let m = 0x3;
    let n = 0x0;
    let r = 0;
  }
}

let isReturn = 1, isTerminator = 1,
    isBarrier = 1, Uses = [A0] in {

  def RET : CALLX_Inst<0x00, 0x00, 0x00, (outs), (ins),
                      "ret", [(Xtensa_ret)]> {
    let m = 0x2;
    let n = 0x0;
    let s = 0;
    let r = 0;
  }
}

// Call patterns
def : Pat<(Xtensa_call (i32 tglobaladdr:$dst)),
          (CALL0 tglobaladdr:$dst)>;
def : Pat<(Xtensa_call (i32 texternalsym:$dst)),
          (CALL0 texternalsym:$dst)>;
def : Pat<(Xtensa_call AR:$dst),
          (CALLX0 AR:$dst)>;

let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1, Size = 3 in {
  def BR_JT: Pseudo<(outs), (ins AR:$s, i32imm:$jt),
                    "!br_jt_p, $s, $jt",
                    [(Xtensa_brjt AR:$s, tjumptable:$jt)]>;
}

//===----------------------------------------------------------------------===//
// Mem barrier instructions
//===----------------------------------------------------------------------===//

def MEMW :  RRR_Inst<0x00, 0x00, 0x00, (outs), (ins),
                    "memw", []> {
  let r = 0x2;
  let t = 0x0c;
  let s = 0x0;
}

def EXTW : RRR_Inst<0x00, 0x00, 0x00, (outs), (ins),
                   "extw", []> {
  let r = 0x2;
  let s = 0x0;
  let t = 0xd;
  let hasSideEffects = 1;
}

//===----------------------------------------------------------------------===//
// Processor control instructions
//===----------------------------------------------------------------------===//

def DSYNC : RRR_Inst<0x00, 0x00, 0x00, (outs), (ins),
                    "dsync", []> {
  let r = 0x2;
  let s = 0x0;
  let t = 0x3;
  let hasSideEffects = 1;
}

def ISYNC : RRR_Inst<0x00, 0x00, 0x00, (outs), (ins),
                    "isync", []> {
  let r = 0x2;
  let s = 0x0;
  let t = 0x0;
  let hasSideEffects = 1;
}

def RSYNC : RRR_Inst<0x00, 0x00, 0x00, (outs), (ins),
                    "rsync", []> {
  let r = 0x2;
  let s = 0x0;
  let t = 0x1;
  let hasSideEffects = 1;
}

def ESYNC : RRR_Inst<0x00, 0x00, 0x00, (outs), (ins),
                    "esync", []> {
  let r = 0x2;
  let s = 0x0;
  let t = 0x2;
  let hasSideEffects = 1;
}

def NOP : RRR_Inst<0x00, 0x00, 0x00, (outs), (ins),
                  "nop", []> {
  let r = 0x02;
  let s = 0x00;
  let t = 0x0f;
}

def WSR : RSR_Inst<0x00, 0x03, 0x01, (outs SR:$sr), (ins AR:$t),
                  "wsr\t$t, $sr", []>;

def RSR : RSR_Inst<0x00, 0x03, 0x00, (outs AR:$t), (ins SR:$sr),
                  "rsr\t$t, $sr", []>;

def XSR : RSR_Inst<0x00, 0x01, 0x06, (outs AR:$ard, SR:$srd), (ins AR:$t, SR:$sr),
                  "xsr\t$t, $sr", []> {
  let Constraints = "$ard = $t, $srd = $sr";
}

//===----------------------------------------------------------------------===//
// Stack allocation
//===----------------------------------------------------------------------===//

// ADJCALLSTACKDOWN/UP implicitly use/def SP because they may be expanded into
// a stack adjustment and the codegen must know that they may modify the stack
// pointer before prolog-epilog rewriting occurs.
let Defs = [SP], Uses = [SP] in {
  def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                               "#ADJCALLSTACKDOWN",
                               [(Xtensa_callseq_start timm:$amt1, timm:$amt2)]>;
  def ADJCALLSTACKUP   : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                               "#ADJCALLSTACKUP",
                               [(Xtensa_callseq_end timm:$amt1, timm:$amt2)]>;
}

//===----------------------------------------------------------------------===//
// Generic select instruction
//===----------------------------------------------------------------------===//
let usesCustomInserter = 1 in {
  def SELECT : Pseudo<(outs AR:$dst), (ins AR:$lhs, AR:$rhs, AR:$t, AR:$f, i32imm:$cond),
                     "!select $dst, $lhs, $rhs, $t, $f, $cond",
                     [(set i32:$dst, (Xtensa_select_cc i32:$lhs, i32:$rhs, i32:$t, i32:$f, imm:$cond))]>;
}
