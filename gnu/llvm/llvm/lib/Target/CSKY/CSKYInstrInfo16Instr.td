//===-- CSKYInstrInfo16Instr.td - CSKY 16-bit Instruction --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the CSKY 16-bit instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// CSKY specific DAG Nodes.
//===----------------------------------------------------------------------===//

// Target-dependent nodes.
def CSKY_NIE : SDNode<"CSKYISD::NIE", SDTNone,
    [SDNPHasChain, SDNPOptInGlue]>;
def CSKY_NIR : SDNode<"CSKYISD::NIR", SDTNone,
    [SDNPHasChain, SDNPOptInGlue]>;

//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

def br_symbol_16bit : Operand<OtherVT> {
  let EncoderMethod =
    "getBranchSymbolOpValue<CSKY::fixup_csky_pcrel_imm10_scale2>";
  let ParserMatchClass = CSKYSymbol;
  let DecoderMethod = "decodeSImmOperand<10, 1>";
  let PrintMethod = "printCSKYSymbolOperand";
  let OperandType = "OPERAND_PCREL";
}

def constpool_symbol_16bit : Operand<iPTR> {
  let ParserMatchClass = Constpool;
  let EncoderMethod =
    "getConstpoolSymbolOpValue<CSKY::fixup_csky_pcrel_uimm7_scale4>";
  let DecoderMethod = "decodeLRW16Imm8";
  let PrintMethod = "printConstpool";
  let OperandType = "OPERAND_PCREL";
}

//===----------------------------------------------------------------------===//
// Instruction Formats
//===----------------------------------------------------------------------===//

include "CSKYInstrFormats16Instr.td"

//===----------------------------------------------------------------------===//
// Instruction definitions.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Basic ALU instructions.
//===----------------------------------------------------------------------===//

let isCommutable = 1, isAdd = 1 in
  def ADDU16 : R16_XYZ<0, "addu16", add>;
let Pattern = [(set mGPR:$rz, (sub mGPR:$rx, mGPR:$ry))] in
  def SUBU16 : R16_XYZ<1, "subu16", sub>;

let isCommutable = 1, isAdd = 1 in
  def ADDC16 : R16_XZ_BINOP_C<0b1000, 0b01, "addc16">;
def SUBC16 : R16_XZ_BINOP_C<0b1000, 0b11, "subc16">;

let isCommutable = 1 in {
  let isAdd = 1 in
  def ADDU16XZ : R16_XZ_BINOP<0b1000, 0b00, "addu16", BinOpFrag<(add node:$LHS, node:$RHS)>>;
  def AND16 : R16_XZ_BINOP<0b1010, 0b00, "and16", BinOpFrag<(and node:$LHS, node:$RHS)>>;
  def OR16 : R16_XZ_BINOP<0b1011, 0b00, "or16", BinOpFrag<(or node:$LHS, node:$RHS)>>;
  def XOR16 : R16_XZ_BINOP<0b1011, 0b01, "xor16", BinOpFrag<(xor node:$LHS, node:$RHS)>>;
  def NOR16 : R16_XZ_BINOP<0b1011, 0b10, "nor16", BinOpFrag<(not (or node:$LHS, node:$RHS))>>;
  let isCodeGenOnly = 1 in
  def NOT16 : R16_Z_UNOP<0b1011, 0b10, "not16">;
  def MULT16 :  R16_XZ_BINOP<0b1111, 0b00, "mult16", BinOpFrag<(mul node:$LHS, node:$RHS)>>;
}
def SUBU16XZ : R16_XZ_BINOP<0b1000, 0b10, "subu16", BinOpFrag<(sub node:$LHS, node:$RHS)>>;
def ANDN16 : R16_XZ_BINOP<0b1010, 0b01, "andn16", BinOpFrag<(and node:$LHS, (not node:$RHS))>>;
def LSL16 : R16_XZ_BINOP<0b1100, 0b00, "lsl16", BinOpFrag<(shl node:$LHS, node:$RHS)>>;
def LSR16 : R16_XZ_BINOP<0b1100, 0b01, "lsr16", BinOpFrag<(srl node:$LHS, node:$RHS)>>;
def ASR16 : R16_XZ_BINOP<0b1100, 0b10, "asr16", BinOpFrag<(sra node:$LHS, node:$RHS)>>;
def ROTL16 : R16_XZ_BINOP<0b1100, 0b11, "rotl16", BinOpFrag<(rotl node:$LHS, (and node:$RHS, 0x1f))>>;

def MULSH16 : R16_XZ_BINOP_NOPat<0b1111, 0b01, "mulsh16">;

def ZEXTB16 : R16_XZ_UNOP<0b1101, 0b00, "zextb16">;
def ZEXTH16 : R16_XZ_UNOP<0b1101, 0b01, "zexth16">;
def SEXTB16 : R16_XZ_UNOP<0b1101, 0b10, "sextb16">;
def SEXTH16 : R16_XZ_UNOP<0b1101, 0b11, "sexth16">;

let Constraints = "$rZ = $rz", isReMaterializable = 1, isAsCheapAsAMove = 1 in {
  let isAdd = 1, Pattern = [(set mGPR:$rz, (add mGPR:$rZ, oimm8:$imm8))] in
  def ADDI16 : I16_Z_8<0b100, (ins mGPR:$rZ, oimm8:$imm8), "addi16\t$rz, $imm8">;
  let Pattern = [(set mGPR:$rz, (sub mGPR:$rZ, oimm8:$imm8))] in
  def SUBI16 : I16_Z_8<0b101, (ins mGPR:$rZ, oimm8:$imm8), "subi16\t$rz, $imm8">;
}

def : Pat<(add GPR:$rs1, (oimm8_neg:$im)),
          (SUBI16 GPR:$rs1, (imm_neg_XFORM oimm8_neg:$im))>;

let isAdd = 1 in
def ADDI16ZSP : I16_Z_8<0b011, (ins GPRSP:$sp, uimm8_2:$imm8),
                        "addi16\t$rz, $sp, $imm8">;

let isAdd = 1 in
def ADDI16SPSP : I16_SP_IMM7<0b000,"addi16">;
def SUBI16SPSP : I16_SP_IMM7<0b001,"subi16">;

let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
  def LSLI16 : I16_XZ_IMM5<0, "lsli16", shl>;
  def LSRI16 : I16_XZ_IMM5<1, "lsri16", srl>;
  def ASRI16 : I16_XZ_IMM5<2, "asri16", sra>;
}

let isAdd = 1 in
def ADDI16XZ : I16_XZ_IMM3<0b10, "addi16", add>;
def SUBI16XZ : I16_XZ_IMM3<0b11, "subi16", sub>;

let Size = 4 in
def NEG16 : CSKYPseudo<(outs mGPR:$rd), (ins mGPR:$rx), "neg16 $rd, $rx", []>;

let Size = 4 in
def RSUBI16 : CSKYPseudo<(outs mGPR:$rd),
  (ins mGPR:$rx, uimm8:$imm8), "rsubi16 $rd, $rx, $imm8", []>;

//===----------------------------------------------------------------------===//
// Load & Store instructions.
//===----------------------------------------------------------------------===//

def LD16B : I16_XZ_LDST<AddrMode16B, 0b000, "ld16.b",
  (outs mGPR:$rz), (ins mGPR:$rx, uimm5:$imm)>;
def LD16H : I16_XZ_LDST<AddrMode16H, 0b001, "ld16.h",
  (outs mGPR:$rz), (ins mGPR:$rx, uimm5_1:$imm)>;
def LD16W : I16_XZ_LDST<AddrMode16W, 0b010, "ld16.w",
  (outs mGPR:$rz), (ins mGPR:$rx, uimm5_2:$imm)>;
def ST16B : I16_XZ_LDST<AddrMode16B, 0b100, "st16.b",
  (outs), (ins mGPR:$rz, mGPR:$rx, uimm5:$imm)>;
def ST16H : I16_XZ_LDST<AddrMode16H, 0b101, "st16.h",
  (outs), (ins mGPR:$rz, mGPR:$rx, uimm5_1:$imm)>;
def ST16W : I16_XZ_LDST<AddrMode16W, 0b110, "st16.w",
  (outs), (ins mGPR:$rz, mGPR:$rx, uimm5_2:$imm)>;

def LD16WSP : I16_ZSP_LDST<AddrMode16W, 0b011, "ld16.w",
  (outs mGPR:$rz), (ins GPRSP:$sp, uimm8_2:$addr)>;
def ST16WSP : I16_ZSP_LDST<AddrMode16W, 0b111, "st16.w",
  (outs), (ins mGPR:$rz, GPRSP:$sp, uimm8_2:$addr)>;

//===----------------------------------------------------------------------===//
// Compare instructions.
//===----------------------------------------------------------------------===//

def CMPHS16 : R16_XY_CMP<0, "cmphs16">;
def CMPLT16 : R16_XY_CMP<1, "cmplt16">;
let isCommutable = 1 in
def CMPNE16 : R16_XY_CMP<2, "cmpne16">;


def CMPHSI16 : I16_X_CMP<0, "cmphsi16", oimm5>;
def CMPLTI16 : I16_X_CMP<1, "cmplti16", oimm5>;
def CMPLEI16 : CSKYPseudo<(outs CARRY:$ca), (ins mGPR:$rx, uimm5:$imm5),
    "cmplei16\t$rx, $imm5", []>;
def CMPNEI16 : I16_X_CMP<2, "cmpnei16", uimm5>;

//===----------------------------------------------------------------------===//
// Data move instructions.
//===----------------------------------------------------------------------===//


def MOVI16 : I16_Z_8<0b110, (ins uimm8:$imm8), "movi16\t$rz, $imm8"> {
  let isReMaterializable = 1;
  let isAsCheapAsAMove = 1;
  let isMoveImm = 1;
  let Pattern = [(set mGPR:$rz, uimm8:$imm8)];
}

def MOV16 : CSKY16Inst<AddrModeNone, (outs sGPR:$rz), (ins sGPR:$rx),
                       "mov16\t$rz, $rx", []> {
  bits<4> rz;
  bits<4> rx;
  let Inst{15,14} = 0b01;
  let Inst{13 - 10} = 0b1011;
  let Inst{9 - 6} = rz;
  let Inst{5 - 2} = rx;
  let Inst{1,0} = 0b11;
}

// MVC16 is not in "cskyv2 instructions reference manul"
def MVCV16 : CSKY16Inst<AddrModeNone,
  (outs sGPR:$rz), (ins CARRY:$ca), "mvcv16\t$rz", []> {
  bits<4> rz;
  let Inst{15,14} = 0b01;
  let Inst{13 - 10} = 0b1001;
  let Inst{9 - 6} = rz;
  let Inst{5 - 2} = 0;
  let Inst{1,0} = 0b11;
}


//===----------------------------------------------------------------------===//
// Branch and call instructions.
//===----------------------------------------------------------------------===//

let isBranch = 1, isTerminator = 1 in {
  let isBarrier = 1, isPredicable = 1 in
    def BR16 : J16<1, "br16", (ins br_symbol_16bit:$offset)>;

  def BT16 : J16_B<2, "bt16">;
  def BF16 : J16_B<3, "bf16">;
}

def JMP16 : R16_X_J<0b11100000, 0b00, "jmp16"> {
  let isBranch = 1;
  let isTerminator = 1;
  let isBarrier = 1;
  let isIndirectBranch = 1;
  let Pattern = [(brind sGPR:$rx)];
}

def JSR16 : R16_X_J<0b11101111, 0b01, "jsr16"> {
  let isCall = 1;
  let Defs = [ R15 ];
}

def RTS16 : CSKY16Inst<AddrModeNone, (outs), (ins), "rts16", [(CSKY_RET)]> {
  let isTerminator = 1;
  let isReturn = 1;
  let isBarrier = 1;
  let Inst = 0b0111100000111100;
  let Uses = [R15];
  let isCodeGenOnly = 1;
}

def JMPIX16 :  CSKY16Inst<AddrModeNone, (outs),
  (ins mGPR:$rx, uimm2_jmpix:$indeximm2), "jmpix16\t$rx, $indeximm2", []> {
  bits<3> rx;
  bits<2> indeximm2;
  let Inst{15,14} = 0b00;
  let Inst{13 - 11} = 0b111;
  let Inst{10 - 8} = rx;
  let Inst{7 - 2} = 0b111000;
  let Inst{1,0} = indeximm2;
  let Predicates = [HasJAVA];
  let Uses = [R30];
}

//===----------------------------------------------------------------------===//
// Symbol address instructions.
//===----------------------------------------------------------------------===//

def LRW16 : CSKY16Inst<AddrModeNone, (outs mGPR:$rz),
  (ins constpool_symbol_16bit:$label), "lrw16\t$rz, $label", []> {
  bits<3> rz;
  bits<8> label;
  let Inst{15 - 13} = 0b000;
  let Inst{12} = label{7};
  let Inst{11,10} = 0b00;
  let Inst{9,8} = label{6,5};
  let Inst{7 - 5} = rz;
  let Inst{4 - 0} = label{4-0};
  let mayLoad = 1;
  let mayStore = 0;
}

def LRW16_Gen : CSKY16Inst<AddrModeNone, (outs mGPR:$rz),
  (ins bare_symbol:$src, constpool_symbol_16bit:$label),
  "lrw16\t$rz, $label", []> {
  bits<3> rz;
  bits<8> label;
  let Inst{15 - 13} = 0b000;
  let Inst{12} = label{7};
  let Inst{11,10} = 0b00;
  let Inst{9,8} = label{6,5};
  let Inst{7 - 5} = rz;
  let Inst{4 - 0} = label{4-0};
  let mayLoad = 1;
  let mayStore = 0;
  let isCodeGenOnly = 1;
}


//===----------------------------------------------------------------------===//
// Other operation instructions.
//===----------------------------------------------------------------------===//

def REVB16 :  R16_XZ_UNOP<0b1110, 0b10, "revb16">;
def REVH16 :  R16_XZ_UNOP<0b1110, 0b11, "revh16">;

let isCodeGenOnly = 1 in
def SETC16 : CSKY16Inst<AddrModeNone,
  (outs CARRY:$ca), (ins), "setc16", []> {
  let Inst{15, 14} = 0b01;
  let Inst{13 - 10} = 0b1001;
  let Inst{9 - 6} = 0;
  let Inst{5 - 2} = 0;
  let Inst{1, 0} = 0;
  let isCompare = 1;
}

let isCodeGenOnly = 1 in
def CLRC16 : CSKY16Inst<AddrModeNone,
  (outs CARRY:$ca), (ins), "clrc16", []> {
  let Inst{15, 14} = 0b01;
  let Inst{13 - 10} = 0b1001;
  let Inst{9 - 6} = 0;
  let Inst{5 - 2} = 0;
  let Inst{1, 0} = 2;
  let isCompare = 1;
}

let Constraints = "$rZ = $rz" in {
  def BCLRI16 : I16_Z_5<0b100, (outs mGPR:$rz), (ins mGPR:$rZ, uimm5:$imm5),
                        "bclri16">;
  def BSETI16 : I16_Z_5<0b101, (outs mGPR:$rz), (ins mGPR:$rZ, uimm5:$imm5),
                        "bseti16">;
}

let Predicates = [HasBTST16] in
  def BTSTI16 : I16_Z_5<0b110, (outs CARRY:$ca), (ins mGPR:$rz, uimm5:$imm5),
                        "btsti16">;

def TST16 : CSKY16Inst<AddrModeNone, (outs CARRY:$ca), (ins sGPR:$rx, sGPR:$ry),
                        "tst16\t$rx, $ry", []> {
  bits<4> ry;
  bits<4> rx;
  let Inst{15,14} = 0b01;
  let Inst{13 - 10} = 0b1010;
  let Inst{9 - 6} = ry;
  let Inst{5 - 2} = rx;
  let Inst{1,0} = 0b10;
  let isCompare = 1;
}

def TSTNBZ16 : CSKY16Inst<AddrModeNone, (outs CARRY:$ca), (ins sGPR:$rx),
                          "tstnbz16\t$rx", []> {
  bits<4> rx;
  let Inst{15,14} = 0b01;
  let Inst{13 - 10} = 0b1010;
  let Inst{9 - 6} = 0b0000;
  let Inst{5 - 2} = rx;
  let Inst{1,0} = 0b11;
  let isCompare = 1;
}

//===----------------------------------------------------------------------===//
// Special instructions.
//===----------------------------------------------------------------------===//

def BKPT : CSKY16Inst<AddrModeNone, (outs), (ins), "bkpt", []> {
  let Inst = 0;
}

let mayStore = 1 in {
def BPUSHH : I16_BPushPop<0b00010100111, 0, (outs), (ins mGPR:$rz), "bpush.h $rz">;
def BPUSHW : I16_BPushPop<0b00010100111, 0b10, (outs), (ins mGPR:$rz), "bpush.w $rz">;
}

let mayLoad = 1 in {
def BPOPH : I16_BPushPop<0b00010100101, 0, (outs mGPR:$rz), (ins),  "bpop.h $rz">;
def BPOPW : I16_BPushPop<0b00010100101, 0b10, (outs mGPR:$rz), (ins), "bpop.w $rz">;
}

def NIE : CSKY16Inst<AddrModeNone, (outs), (ins), "nie", [(CSKY_NIE)]> {
  let Inst = 0b0001010001100000;
}

let isBarrier = 1, isReturn = 1, isTerminator = 1 in
def NIR : CSKY16Inst<AddrModeNone, (outs), (ins), "nir", [(CSKY_NIR)]> {
  let Inst = 0b0001010001100001;
}

def IPUSH16 : CSKY16Inst<AddrModeNone, (outs), (ins), "ipush16", []> {
  let Inst{15- 5} = 0b00010100011;
  let Inst{4-0} = 0b00010;
  let Predicates = [iHasE1];
  let Defs  = [R14];
  let Uses  = [R14, R0, R1, R2, R3, R12, R13];
  let mayStore = 1;
}

def IPOP16 : CSKY16Inst<AddrModeNone, (outs), (ins), "ipop16", []> {
  let Inst{15- 5} = 0b00010100011;
  let Inst{4-0} = 0b00011;
  let Predicates = [iHasE1];
  let Defs  = [R14, R0, R1, R2, R3, R12, R13];
  let Uses  = [R14];
  let mayLoad = 1;
}

def PUSH16 : CSKY16Inst<AddrModeNone, (outs),
  (ins reglist:$regs, variable_ops), "push16 $regs", []> {
  bits<5> regs;

  let Inst{15- 5} = 0b00010100110;
  let Inst{4-0} = regs;
  let Predicates = [iHasE1];
  let Defs  = [R14];
  let Uses  = [R14];
  let mayStore = 1;
}

def POP16 : CSKY16Inst<AddrModeNone, (outs),
  (ins reglist:$regs, variable_ops), "pop16 $regs", []> {
  bits<5> regs;

  let Inst{15- 5} = 0b00010100100;
  let Inst{4-0} = regs;
  let Predicates = [iHasE1];
  let Defs  = [R14];
  let Uses  = [R14];
  let mayLoad = 1;
}

//===----------------------------------------------------------------------===//
// CSKYPseudo
//===----------------------------------------------------------------------===//

let usesCustomInserter = 1 in  {
  def ISEL16 : CSKYPseudo<(outs sGPR:$dst),
    (ins CARRY:$cond, sGPR:$src1, sGPR:$src2),
    "!isel16\t$dst, $src1, src2",
    [(set sGPR:$dst, (select CARRY:$cond, sGPR:$src1, sGPR:$src2))]>;
}

class JBranchPseudo<dag out, dag ins, string opstr> :
  CSKYPseudo<out, ins, opstr, []> {
  let isBranch = 1;
  let isTerminator = 1;
  let isIndirectBranch = 1;
  let mayLoad = 1;
  let Size = 2;
}

let isBarrier = 1 in
def JBR16 : JBranchPseudo<(outs),
  (ins br_symbol_16bit:$src1), "jbr16\t$src1">;
def JBT16 : JBranchPseudo<(outs),
  (ins CARRY:$ca, br_symbol_16bit:$src1), "jbt16\t$src1">;
def JBF16 : JBranchPseudo<(outs),
  (ins CARRY:$ca, br_symbol_16bit:$src1), "jbf16\t$src1">;

let mayLoad = 1, Size = 2, isCodeGenOnly = 0 in
def PseudoLRW16 : CSKYPseudo<(outs mGPR:$rz),
  (ins bare_symbol:$src), "lrw16 $rz, $src", []>;

//===----------------------------------------------------------------------===//
// Instruction Patterns.
//===----------------------------------------------------------------------===//

def : Pat<(sext_inreg mGPR:$src, i1), (ASRI16 (LSLI16 mGPR:$src, 7), 7)>;
def : Pat<(sext_inreg sGPR:$src, i8), (SEXTB16 sGPR:$src)>;
def : Pat<(sext_inreg sGPR:$src, i16), (SEXTH16 sGPR:$src)>;

// Load & Store Patterns

defm : LdPat<extloadi8, uimm5, LD16B, i32>;
defm : LdPat<zextloadi8, uimm5, LD16B, i32>;

defm : LdPat<extloadi16, uimm5_1, LD16H, i32>;
defm : LdPat<zextloadi16, uimm5_1, LD16H, i32>;

defm : LdPat<load, uimm5_2, LD16W, i32>;


defm : StPat<truncstorei8, i32, uimm5, ST16B>;
defm : StPat<truncstorei16, i32, uimm5_1, ST16H>;
defm : StPat<store, i32, uimm5_2, ST16W>;

def : Pat<(CSKY_CALLReg sGPR:$src), (JSR16 sGPR:$src)>;
def : Pat<(CSKY_TAILReg sGPR:$src), (JMP16 sGPR:$src)>;

// Symbol address Patterns
def : Pat<(CSKY_LOAD_ADDR tglobaladdr, tconstpool:$src2), (LRW16 tconstpool:$src2)>;
def : Pat<(CSKY_LOAD_ADDR tblockaddress, tconstpool:$src2), (LRW16 tconstpool:$src2)>;
def : Pat<(CSKY_LOAD_ADDR tjumptable:$src1, tconstpool:$src2), (LRW16_Gen tjumptable:$src1, tconstpool:$src2)>;
def : Pat<(CSKY_LOAD_ADDR texternalsym, tconstpool:$src2), (LRW16 tconstpool:$src2)>;
def : Pat<(CSKY_LOAD_ADDR tconstpool:$src1, tconstpool:$src2), (LRW16_Gen tconstpool:$src1, tconstpool:$src2)>;

def : Pat<(i32 (load constpool:$src)), (LRW16 (to_tconstpool tconstpool:$src))>;

// Branch Patterns.

def : Pat<(brcond CARRY:$ca, bb:$offset),
          (BT16 CARRY:$ca, bb:$offset)>;

def : Pat<(br bb:$offset), (BR16 bb:$offset)>;

multiclass BTF16Pat0<PatFrag cond0, PatFrag cond1, ImmLeaf imm_ty, Instruction inst> {
  def : Pat<(brcond (i32 (cond0 mGPR:$rs1, imm_ty:$rs2)), bb:$offset),
            (BT16 (inst mGPR:$rs1, imm_ty:$rs2), bb:$offset)>;
  def : Pat<(brcond (i32 (cond1 mGPR:$rs1, imm_ty:$rs2)), bb:$offset),
            (BF16 (inst mGPR:$rs1, imm_ty:$rs2), bb:$offset)>;
}

defm : BTF16Pat0<setne, seteq, uimm5, CMPNEI16>;
defm : BTF16Pat0<setuge, setult, oimm5, CMPHSI16>;
defm : BTF16Pat0<setlt, setge, oimm5, CMPLTI16>;

def : Pat<(brcond (i32 (setne sGPR:$rs1, sGPR:$rs2)), bb:$offset),
          (BT16 (CMPNE16 sGPR:$rs1, sGPR:$rs2), bb:$offset)>;
def : Pat<(brcond (i32 (seteq sGPR:$rs1, sGPR:$rs2)), bb:$offset),
          (BF16 (CMPNE16 sGPR:$rs1, sGPR:$rs2), bb:$offset)>;

multiclass BTF16Pat1<PatFrag cond0, PatFrag cond1, Instruction cmp,
                     Instruction br>  {
  def : Pat<(brcond (i32 (cond0 sGPR:$rs1, sGPR:$rs2)), bb:$offset),
            (br (cmp sGPR:$rs1, sGPR:$rs2), bb:$offset)>;
  def : Pat<(brcond (i32 (cond1 sGPR:$rs1, sGPR:$rs2)), bb:$offset),
            (br (cmp sGPR:$rs2, sGPR:$rs1), bb:$offset)>;
}

defm : BTF16Pat1<setuge, setule, CMPHS16, BT16>;
defm : BTF16Pat1<setult, setugt, CMPHS16, BF16>;
defm : BTF16Pat1<setlt, setgt, CMPLT16, BT16>;
defm : BTF16Pat1<setge, setle, CMPLT16, BF16>;

// Compare Patterns.
def : Pat<(setne sGPR:$rs1, sGPR:$rs2),
          (SUBU16XZ (MOVI16 1), (MVCV16 (CMPNE16 sGPR:$rs1, sGPR:$rs2)))>;
def : Pat<(seteq sGPR:$rs1, sGPR:$rs2),
          (MVCV16 (CMPNE16 sGPR:$rs1, sGPR:$rs2))>;
def : Pat<(setuge sGPR:$rs1, sGPR:$rs2),
          (SUBU16XZ (MOVI16 1), (MVCV16 (CMPHS16 sGPR:$rs1, sGPR:$rs2)))>;
def : Pat<(setule sGPR:$rs1, sGPR:$rs2),
          (SUBU16XZ (MOVI16 1), (MVCV16 (CMPHS16 sGPR:$rs2, sGPR:$rs1)))>;
def : Pat<(setult sGPR:$rs1, sGPR:$rs2),
          (MVCV16 (CMPHS16 sGPR:$rs1, sGPR:$rs2))>;
def : Pat<(setugt sGPR:$rs1, sGPR:$rs2),
          (MVCV16 (CMPHS16 sGPR:$rs2, sGPR:$rs1))>;
def : Pat<(setlt sGPR:$rs1, sGPR:$rs2),
          (SUBU16XZ (MOVI16 1), (MVCV16 (CMPLT16 sGPR:$rs1, sGPR:$rs2)))>;
def : Pat<(setgt sGPR:$rs1, sGPR:$rs2),
          (SUBU16XZ (MOVI16 1), (MVCV16 (CMPLT16 sGPR:$rs2, sGPR:$rs1)))>;
def : Pat<(setge sGPR:$rs1, sGPR:$rs2),
          (MVCV16 (CMPLT16 sGPR:$rs1, sGPR:$rs2))>;
def : Pat<(setle sGPR:$rs1, sGPR:$rs2),
          (MVCV16 (CMPLT16 sGPR:$rs2, sGPR:$rs1))>;


def : Pat<(setne mGPR:$rs1, uimm5:$rs2),
          (SUBU16XZ (MOVI16 1), (MVCV16 (CMPNEI16 mGPR:$rs1, uimm5:$rs2)))>;
def : Pat<(seteq mGPR:$rs1, uimm5:$rs2),
          (MVCV16 (CMPNEI16 mGPR:$rs1, uimm5:$rs2))>;
def : Pat<(setuge mGPR:$rs1, oimm5:$rs2),
          (SUBU16XZ (MOVI16 1), (MVCV16 (CMPHSI16 mGPR:$rs1, oimm5:$rs2)))>;
def : Pat<(setult mGPR:$rs1, oimm5:$rs2),
          (MVCV16 (CMPHSI16 mGPR:$rs1, oimm5:$rs2))>;
def : Pat<(setlt mGPR:$rs1, oimm5:$rs2),
          (SUBU16XZ (MOVI16 1), (MVCV16 (CMPLTI16 mGPR:$rs1, oimm5:$rs2)))>;
def : Pat<(setge mGPR:$rs1, oimm5:$rs2),
          (MVCV16 (CMPLTI16 mGPR:$rs1, oimm5:$rs2))>;

def : Pat<(select CARRY:$ca, sGPR:$rx, sGPR:$false),
          (ISEL16 CARRY:$ca, sGPR:$rx, sGPR:$false)>;
def : Pat<(select (and CARRY:$ca, 1), sGPR:$rx, sGPR:$false),
          (ISEL16 CARRY:$ca, sGPR:$rx, sGPR:$false)>;

def : Pat<(rotl sGPR:$rs1, sGPR:$rs2),
          (ROTL16 sGPR:$rs1, (AND16 sGPR:$rs2, (MOVI16 0x1f)))>;


// FIXME: This is a temporary treatment for the e801.
def : Pat<(i32 imm:$imm),
          (OR16 (MOVI16 (uimm8SRL_0 imm:$imm)),
	              (OR16 (LSLI16 (MOVI16 (uimm8SRL_8 imm:$imm)), 8),
	                    (OR16 (LSLI16 (MOVI16 (uimm8SRL_16 imm:$imm)), 16),
                            (LSLI16 (MOVI16 (uimm8SRL_24 imm:$imm)), 24))))>;

// Other operations.
let Predicates = [iHasE2] in {
  def : Pat<(bswap sGPR:$rx), (REVB16 sGPR:$rx)>;
}

//===----------------------------------------------------------------------===//
// Compress Instruction tablegen backend.
//===----------------------------------------------------------------------===//

def : CompressPat<(ADDU32 sGPR:$rd, sGPR:$rd, sGPR:$rs2),
                  (ADDU16XZ sGPR:$rd, sGPR:$rs2)>;
def : CompressPat<(ADDU32 sGPR:$rd, sGPR:$rs1, sGPR:$rd),
                  (ADDU16XZ sGPR:$rd, sGPR:$rs1)>;
def : CompressPat<(ADDU32 mGPR:$rd, mGPR:$rs1, mGPR:$rs2),
                  (ADDU16 mGPR:$rd, mGPR:$rs1, mGPR:$rs2)>;
def : CompressPat<(SUBU32 sGPR:$rd, sGPR:$rd, sGPR:$rs2),
                  (SUBU16XZ sGPR:$rd, sGPR:$rs2)>;
def : CompressPat<(SUBU32 mGPR:$rd, mGPR:$rs1, mGPR:$rs2),
                  (SUBU16 mGPR:$rd, mGPR:$rs1, mGPR:$rs2)>;

def : CompressPat<
  (ADDC32 sGPR:$rd, CARRY:$cout, sGPR:$rd, sGPR:$rs2, CARRY:$cout),
  (ADDC16 sGPR:$rd, CARRY:$cout, sGPR:$rs2, CARRY:$cout)
  >;
def : CompressPat<
  (SUBC32 sGPR:$rd, CARRY:$cout, sGPR:$rd, sGPR:$rs2, CARRY:$cout),
  (SUBC16 sGPR:$rd, CARRY:$cout, sGPR:$rs2, CARRY:$cout)
  >;

def : CompressPat<(ADDI32 mGPR:$rd, mGPR:$rs, oimm3:$imm),
                  (ADDI16XZ mGPR:$rd, mGPR:$rs, oimm3:$imm)>;
def : CompressPat<(SUBI32 mGPR:$rd, mGPR:$rs, oimm3:$imm),
                  (SUBI16XZ mGPR:$rd, mGPR:$rs, oimm3:$imm)>;

def : CompressPat<(ADDI32 mGPR:$rd, mGPR:$rd, oimm8:$imm),
                  (ADDI16 mGPR:$rd, oimm8:$imm)>;
def : CompressPat<(SUBI32 mGPR:$rd, mGPR:$rd, oimm8:$imm),
                  (SUBI16 mGPR:$rd, oimm8:$imm)>;

def : CompressPat<(ADDI32 GPRSP:$sp, GPRSP:$sp, uimm7_2:$imm),
                  (ADDI16SPSP GPRSP:$sp, GPRSP:$sp, uimm7_2:$imm)>;
def : CompressPat<(SUBI32 GPRSP:$sp, GPRSP:$sp, uimm7_2:$imm),
                  (SUBI16SPSP GPRSP:$sp, GPRSP:$sp, uimm7_2:$imm)>;

def : CompressPat<(ADDI32 mGPR:$rd, GPRSP:$sp, uimm8_2:$imm),
                  (ADDI16ZSP mGPR:$rd, GPRSP:$sp, uimm8_2:$imm)>;

def : CompressPat<(MULT32 sGPR:$rd, sGPR:$rd, sGPR:$rs2),
                  (MULT16 sGPR:$rd, sGPR:$rs2)>;
def : CompressPat<(MULT32 sGPR:$rd, sGPR:$rs1, sGPR:$rd),
                  (MULT16 sGPR:$rd, sGPR:$rs1)>;
def : CompressPat<(AND32 sGPR:$rd, sGPR:$rd, sGPR:$rs2),
                  (AND16 sGPR:$rd, sGPR:$rs2)>;
def : CompressPat<(AND32 sGPR:$rd, sGPR:$rs1, sGPR:$rd),
                  (AND16 sGPR:$rd, sGPR:$rs1)>;
def : CompressPat<(OR32 sGPR:$rd, sGPR:$rd, sGPR:$rs2),
                  (OR16 sGPR:$rd, sGPR:$rs2)>;
def : CompressPat<(OR32 sGPR:$rd, sGPR:$rs1, sGPR:$rd),
                  (OR16 sGPR:$rd, sGPR:$rs1)>;
def : CompressPat<(XOR32 sGPR:$rd, sGPR:$rd, sGPR:$rs2),
                  (XOR16 sGPR:$rd, sGPR:$rs2)>;
def : CompressPat<(XOR32 sGPR:$rd, sGPR:$rs1, sGPR:$rd),
                  (XOR16 sGPR:$rd, sGPR:$rs1)>;

def : CompressPat<(ANDN32 sGPR:$rd, sGPR:$rd, sGPR:$rs2),
                  (ANDN16 sGPR:$rd, sGPR:$rs2)>;
def : CompressPat<(NOR32 sGPR:$rd, sGPR:$rd, sGPR:$rs2),
                  (NOR16 sGPR:$rd, sGPR:$rs2)>;
def : CompressPat<(LSL32 sGPR:$rd, sGPR:$rd, sGPR:$rs2),
                  (LSL16 sGPR:$rd, sGPR:$rs2)>;
def : CompressPat<(LSR32 sGPR:$rd, sGPR:$rd, sGPR:$rs2),
                  (LSR16 sGPR:$rd, sGPR:$rs2)>;
def : CompressPat<(ASR32 sGPR:$rd, sGPR:$rd, sGPR:$rs2),
                  (ASR16 sGPR:$rd, sGPR:$rs2)>;
def : CompressPat<(ROTL32 sGPR:$rd, sGPR:$rd, sGPR:$rs2),
                  (ROTL16 sGPR:$rd, sGPR:$rs2)>;

def : CompressPat<(NOT32 sGPR:$rd, sGPR:$rd),
                  (NOT16 sGPR:$rd)>;

let Predicates = [iHas2E3] in
def : CompressPat<(REVB32 sGPR:$rd, sGPR:$rs),
                  (REVB16 sGPR:$rd, sGPR:$rs)>;

def : CompressPat<(LSLI32 mGPR:$rd, mGPR:$rs, uimm5:$imm),
                  (LSLI16 mGPR:$rd, mGPR:$rs, uimm5:$imm)>;
def : CompressPat<(LSRI32 mGPR:$rd, mGPR:$rs, uimm5:$imm),
                  (LSRI16 mGPR:$rd, mGPR:$rs, uimm5:$imm)>;
def : CompressPat<(ASRI32 mGPR:$rd, mGPR:$rs, uimm5:$imm),
                  (ASRI16 mGPR:$rd, mGPR:$rs, uimm5:$imm)>;

def : CompressPat<(CMPHS32 CARRY:$ca, sGPR:$rs1, sGPR:$rs2),
                  (CMPHS16 CARRY:$ca, sGPR:$rs1, sGPR:$rs2)>;
def : CompressPat<(CMPLT32 CARRY:$ca, sGPR:$rs1, sGPR:$rs2),
                  (CMPLT16 CARRY:$ca, sGPR:$rs1, sGPR:$rs2)>;
def : CompressPat<(CMPNE32 CARRY:$ca, sGPR:$rs1, sGPR:$rs2),
                  (CMPNE16 CARRY:$ca, sGPR:$rs1, sGPR:$rs2)>;

def : CompressPat<(CMPHSI32 CARRY:$ca, mGPR:$rs, oimm5:$imm),
                  (CMPHSI16 CARRY:$ca, mGPR:$rs, oimm5:$imm)>;
def : CompressPat<(CMPLTI32 CARRY:$ca, mGPR:$rs, oimm5:$imm),
                  (CMPLTI16 CARRY:$ca, mGPR:$rs, oimm5:$imm)>;
def : CompressPat<(CMPNEI32 CARRY:$ca, mGPR:$rs, uimm5:$imm),
                  (CMPNEI16 CARRY:$ca, mGPR:$rs, uimm5:$imm)>;

def : CompressPat<(JSR32 sGPR:$rd),
                  (JSR16 sGPR:$rd)>;


def : CompressPat<(MVCV32 sGPR:$rd, CARRY:$ca),
                  (MVCV16 sGPR:$rd, CARRY:$ca)>;
def : CompressPat<(MOV32 sGPR:$rd, sGPR:$ca),
                  (MOV16 sGPR:$rd, sGPR:$ca)>;
def : CompressPat<(MOVI32 mGPR:$rd, uimm8:$imm),
                  (MOVI16 mGPR:$rd, uimm8:$imm)>;

def : CompressPat<(LD32B mGPR:$rd, mGPR:$rs, uimm5:$imm),
                  (LD16B mGPR:$rd, mGPR:$rs, uimm5:$imm)>;
def : CompressPat<(LD32H mGPR:$rd, mGPR:$rs, uimm5_1:$imm),
                  (LD16H mGPR:$rd, mGPR:$rs, uimm5_1:$imm)>;
def : CompressPat<(LD32W mGPR:$rd, mGPR:$rs, uimm5_2:$imm),
                  (LD16W mGPR:$rd, mGPR:$rs, uimm5_2:$imm)>;
def : CompressPat<(LD32W mGPR:$rd, GPRSP:$sp, uimm8_2:$imm),
                  (LD16WSP mGPR:$rd, GPRSP:$sp, uimm8_2:$imm)>;

def : CompressPat<(ST32B mGPR:$rd, mGPR:$rs, uimm5:$imm),
                  (ST16B mGPR:$rd, mGPR:$rs, uimm5:$imm)>;
def : CompressPat<(ST32H mGPR:$rd, mGPR:$rs, uimm5_1:$imm),
                  (ST16H mGPR:$rd, mGPR:$rs, uimm5_1:$imm)>;
def : CompressPat<(ST32W mGPR:$rd, mGPR:$rs, uimm5_2:$imm),
                  (ST16W mGPR:$rd, mGPR:$rs, uimm5_2:$imm)>;
def : CompressPat<(ST32W mGPR:$rd, GPRSP:$sp, uimm8_2:$imm),
                  (ST16WSP mGPR:$rd, GPRSP:$sp, uimm8_2:$imm)>;

let Predicates = [HasBTST16] in
def : CompressPat<(BTSTI32 CARRY:$ca, mGPR:$rs, uimm5:$imm),
                  (BTSTI16 CARRY:$ca, mGPR:$rs, uimm5:$imm)>;
def : CompressPat<(BCLRI32 mGPR:$rd, mGPR:$rd, uimm5:$imm),
                  (BCLRI16 mGPR:$rd, uimm5:$imm)>;
def : CompressPat<(BSETI32 mGPR:$rd, mGPR:$rd, uimm5:$imm),
                  (BSETI16 mGPR:$rd, uimm5:$imm)>;

def : CompressPat<(ZEXTB32 sGPR:$rd, sGPR:$rs),
                  (ZEXTB16 sGPR:$rd, sGPR:$rs)>;
def : CompressPat<(ZEXTH32 sGPR:$rd, sGPR:$rs),
                  (ZEXTH16 sGPR:$rd, sGPR:$rs)>;
def : CompressPat<(SEXTB32 sGPR:$rd, sGPR:$rs),
                  (SEXTB16 sGPR:$rd, sGPR:$rs)>;
def : CompressPat<(SEXTH32 sGPR:$rd, sGPR:$rs),
                  (SEXTH16 sGPR:$rd, sGPR:$rs)>;
