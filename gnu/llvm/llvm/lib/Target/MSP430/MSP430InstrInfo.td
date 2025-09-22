//===-- MSP430InstrInfo.td - MSP430 Instruction defs -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the MSP430 instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "MSP430InstrFormats.td"

//===----------------------------------------------------------------------===//
// Type Constraints.
//===----------------------------------------------------------------------===//
class SDTCisI8<int OpNum> : SDTCisVT<OpNum, i8>;
class SDTCisI16<int OpNum> : SDTCisVT<OpNum, i16>;

//===----------------------------------------------------------------------===//
// Type Profiles.
//===----------------------------------------------------------------------===//
def SDT_MSP430Call         : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
def SDT_MSP430CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i16>,
                                             SDTCisVT<1, i16>]>;
def SDT_MSP430CallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDT_MSP430Wrapper      : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                                  SDTCisPtrTy<0>]>;
def SDT_MSP430Cmp          : SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>;
def SDT_MSP430BrCC         : SDTypeProfile<0, 2, [SDTCisVT<0, OtherVT>,
                                                  SDTCisVT<1, i8>]>;
def SDT_MSP430SelectCC     : SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>,
                                                  SDTCisSameAs<1, 2>, 
                                                  SDTCisVT<3, i8>]>;
def SDT_MSP430DAdd         : SDTypeProfile<1, 2, [SDTCisSameAs<0, 1>,
                                                  SDTCisSameAs<0, 2>,
                                                  SDTCisInt<0>]>;

//===----------------------------------------------------------------------===//
// MSP430 Specific Node Definitions.
//===----------------------------------------------------------------------===//
def MSP430retglue  : SDNode<"MSP430ISD::RET_GLUE", SDTNone,
                       [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def MSP430retiglue : SDNode<"MSP430ISD::RETI_GLUE", SDTNone,
                       [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def MSP430rra     : SDNode<"MSP430ISD::RRA", SDTIntUnaryOp, []>;
def MSP430rla     : SDNode<"MSP430ISD::RLA", SDTIntUnaryOp, []>;
def MSP430rrc     : SDNode<"MSP430ISD::RRC", SDTIntUnaryOp, []>;
def MSP430rrcl    : SDNode<"MSP430ISD::RRCL", SDTIntUnaryOp, []>;

def MSP430call    : SDNode<"MSP430ISD::CALL", SDT_MSP430Call,
                     [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;
def MSP430callseq_start :
                 SDNode<"ISD::CALLSEQ_START", SDT_MSP430CallSeqStart,
                        [SDNPHasChain, SDNPOutGlue]>;
def MSP430callseq_end :
                 SDNode<"ISD::CALLSEQ_END",   SDT_MSP430CallSeqEnd,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def MSP430Wrapper : SDNode<"MSP430ISD::Wrapper", SDT_MSP430Wrapper>;
def MSP430cmp     : SDNode<"MSP430ISD::CMP", SDT_MSP430Cmp, [SDNPOutGlue]>;
def MSP430brcc    : SDNode<"MSP430ISD::BR_CC", SDT_MSP430BrCC,
                            [SDNPHasChain, SDNPInGlue]>;
def MSP430selectcc: SDNode<"MSP430ISD::SELECT_CC", SDT_MSP430SelectCC,
                            [SDNPInGlue]>;
def MSP430dadd    : SDNode<"MSP430ISD::DADD", SDT_MSP430DAdd, []>;

//===----------------------------------------------------------------------===//
// MSP430 Operand Definitions.
//===----------------------------------------------------------------------===//

def MemAsmOperand : AsmOperandClass {
  let Name = "Mem";
}

// Address operands
def memsrc : Operand<i16> {
  let PrintMethod = "printSrcMemOperand";
  let MIOperandInfo = (ops GR16, i16imm);
  let ParserMatchClass = MemAsmOperand;
  let EncoderMethod = "getMemOpValue";
  let DecoderMethod = "DecodeMemOperand";
}

def memdst : Operand<i16> {
  let PrintMethod = "printSrcMemOperand";
  let MIOperandInfo = (ops GR16, i16imm);
  let ParserMatchClass = MemAsmOperand;
  let EncoderMethod = "getMemOpValue";
  let DecoderMethod = "DecodeMemOperand";
}

def IndRegAsmOperand : AsmOperandClass {
  let Name = "IndReg";
  let RenderMethod = "addRegOperands";
}

def indreg : Operand<i16> {
  let PrintMethod = "printIndRegOperand";
  let MIOperandInfo = (ops GR16);
  let ParserMatchClass = IndRegAsmOperand;
  let DecoderMethod = "DecodeGR16RegisterClass";
}

def PostIndRegAsmOperand : AsmOperandClass {
  let Name = "PostIndReg";
  let RenderMethod = "addRegOperands";
}

def postreg : Operand<i16> {
  let PrintMethod = "printPostIndRegOperand";
  let MIOperandInfo = (ops GR16);
  let ParserMatchClass = PostIndRegAsmOperand;
  let DecoderMethod = "DecodeGR16RegisterClass";
}

// Short jump targets have OtherVT type and are printed as pcrel imm values.
def jmptarget : Operand<OtherVT> {
  let PrintMethod = "printPCRelImmOperand";
  let EncoderMethod = "getPCRelImmOpValue";
}

// Operand for printing out a condition code.
def cc : Operand<i8> {
  let PrintMethod = "printCCOperand";
  let EncoderMethod = "getCCOpValue";
}

def CGImmAsmOperand : AsmOperandClass {
  let Name = "CGImm";
  let RenderMethod = "addImmOperands";
}

def cg8imm : Operand<i8>,
             ImmLeaf<i8, [{return Imm == 0 || Imm == 1 || Imm == 2 ||
                                  Imm == 4 || Imm == 8 || Imm == -1;}]> {
  let ParserMatchClass = CGImmAsmOperand;
  let EncoderMethod = "getCGImmOpValue";
  let DecoderMethod = "DecodeCGImm";
}

def cg16imm : Operand<i16>,
              ImmLeaf<i16, [{return Imm == 0 || Imm == 1 || Imm == 2 ||
                                    Imm == 4 || Imm == 8 || Imm == -1;}]> {
  let ParserMatchClass = CGImmAsmOperand;
  let EncoderMethod = "getCGImmOpValue";
  let DecoderMethod = "DecodeCGImm";
}

//===----------------------------------------------------------------------===//
// MSP430 Complex Pattern Definitions.
//===----------------------------------------------------------------------===//

def addr : ComplexPattern<iPTR, 2, "SelectAddr", [], []>;

//===----------------------------------------------------------------------===//
// Pattern Fragments
def zextloadi16i8 : PatFrag<(ops node:$ptr), (i16 (zextloadi8 node:$ptr))>;
def  extloadi16i8 : PatFrag<(ops node:$ptr), (i16 ( extloadi8 node:$ptr))>;
def bic : PatFrag<(ops node:$lhs, node:$rhs), (and node:$lhs, (not node:$rhs))>;
def and_su : PatFrag<(ops node:$lhs, node:$rhs), (and node:$lhs, node:$rhs), [{
  return N->hasOneUse();
}]>;
//===----------------------------------------------------------------------===//
// Instruction list..

// ADJCALLSTACKDOWN/UP implicitly use/def SP because they may be expanded into
// a stack adjustment and the codegen must know that they may modify the stack
// pointer before prolog-epilog rewriting occurs.
// Pessimistically assume ADJCALLSTACKDOWN / ADJCALLSTACKUP will become
// sub / add which can clobber SR.
let isCodeGenOnly = 1, Defs = [SP, SR], Uses = [SP] in {
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                              "#ADJCALLSTACKDOWN $amt1 $amt2",
                              [(MSP430callseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP   : Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                              "#ADJCALLSTACKUP $amt1 $amt2",
                              [(MSP430callseq_end timm:$amt1, timm:$amt2)]>;
}

let isCodeGenOnly = 1, Defs = [SR], Uses = [SP] in {
def ADDframe : Pseudo<(outs GR16:$dst), (ins i16imm:$base, i16imm:$offset),
                      "# ADDframe PSEUDO", []>;
}

let isCodeGenOnly = 1, usesCustomInserter = 1 in {
  let Uses = [SR] in {
  def Select8  : Pseudo<(outs GR8:$dst), (ins GR8:$src, GR8:$src2, i8imm:$cc),
                        "# Select8 PSEUDO",
                        [(set GR8:$dst,
                          (MSP430selectcc GR8:$src, GR8:$src2, imm:$cc))]>;
  def Select16 : Pseudo<(outs GR16:$dst), (ins GR16:$src, GR16:$src2, i8imm:$cc),
                        "# Select16 PSEUDO",
                        [(set GR16:$dst,
                          (MSP430selectcc GR16:$src, GR16:$src2, imm:$cc))]>;
  }
  let Defs = [SR] in {
  def Shl8     : Pseudo<(outs GR8:$dst), (ins GR8:$src, GR8:$cnt),
                        "# Shl8 PSEUDO",
                        [(set GR8:$dst, (shl GR8:$src, GR8:$cnt))]>;
  def Shl16    : Pseudo<(outs GR16:$dst), (ins GR16:$src, GR8:$cnt),
                        "# Shl16 PSEUDO",
                        [(set GR16:$dst, (shl GR16:$src, GR8:$cnt))]>;
  def Sra8     : Pseudo<(outs GR8:$dst), (ins GR8:$src, GR8:$cnt),
                        "# Sra8 PSEUDO",
                        [(set GR8:$dst, (sra GR8:$src, GR8:$cnt))]>;
  def Sra16    : Pseudo<(outs GR16:$dst), (ins GR16:$src, GR8:$cnt),
                        "# Sra16 PSEUDO",
                        [(set GR16:$dst, (sra GR16:$src, GR8:$cnt))]>;
  def Srl8     : Pseudo<(outs GR8:$dst), (ins GR8:$src, GR8:$cnt),
                        "# Srl8 PSEUDO",
                        [(set GR8:$dst, (srl GR8:$src, GR8:$cnt))]>;
  def Srl16    : Pseudo<(outs GR16:$dst), (ins GR16:$src, GR8:$cnt),
                        "# Srl16 PSEUDO",
                        [(set GR16:$dst, (srl GR16:$src, GR8:$cnt))]>;
  def Rrcl8    : Pseudo<(outs GR8:$dst), (ins GR8:$src), "",
                        [(set GR8:$dst, (MSP430rrcl GR8:$src))]>;
  def Rrcl16   : Pseudo<(outs GR16:$dst), (ins GR16:$src), "",
                        [(set GR16:$dst, (MSP430rrcl GR16:$src))]>;
  }
}

//===----------------------------------------------------------------------===//
//  Control Flow Instructions...
//

let isReturn = 1, isTerminator = 1, isBarrier = 1 in {
  def RET  : IForm16<0b0100, DstReg, SrcPostInc, 2,
                     (outs), (ins), "ret",  [(MSP430retglue)]> {
    let DecoderNamespace = "Delta";
    let rs = 1;
    let rd = 0;
  }
  def RETI : IIForm16<0b110, SrcReg, 2,
                      (outs), (ins), "reti", [(MSP430retiglue)]> {
    let rs = 0;
  }
}

let isBranch = 1, isTerminator = 1 in {

// FIXME: expand opcode & cond field for branches!

// Direct branch
let isBarrier = 1 in {
  // Short branch
  def JMP : CJForm<(outs), (ins jmptarget:$dst),
                   "jmp\t$dst",
                   [(br bb:$dst)]> {
    let cond = 0b111;
  }
  let isIndirectBranch = 1, rd = 0 in {
    // Long branches
    def Bi  : I16ri<0b0100, (outs), (ins i16imm:$imm),
                    "br\t$imm",
                    [(brind tblockaddress:$imm)]>;
    def Br  : I16rr<0b0100, (outs), (ins GR16:$rs),
                    "br\t$rs",
                    [(brind GR16:$rs)]>;
    def Bm  : I16rm<0b0100, (outs), (ins memsrc:$src),
                    "br\t$src",
                    [(brind (load addr:$src))]>;
  }
}

// Conditional branches
let Uses = [SR] in
  def JCC : CJForm<(outs), (ins jmptarget:$dst, cc:$cond),
                   "j$cond\t$dst",
                   [(MSP430brcc bb:$dst, imm:$cond)]>;
} // isBranch, isTerminator

//===----------------------------------------------------------------------===//
//  Call Instructions...
//
// All calls clobber the non-callee saved registers. SPW is marked as
// a use to prevent stack-pointer assignments that appear immediately
// before calls from potentially appearing dead. Uses for argument
// registers are added manually.
let isCall = 1,
    Defs = [R11, R12, R13, R14, R15, SR],
    Uses = [SP] in {
  def CALLi     : II16i<0b101,
                        (outs), (ins i16imm:$imm),
                        "call\t$imm", [(MSP430call imm:$imm)]>;
  def CALLr     : II16r<0b101,
                        (outs), (ins GR16:$rs),
                        "call\t$rs", [(MSP430call GR16:$rs)]>;
  def CALLm     : II16m<0b101,
                        (outs), (ins memsrc:$src),
                        "call\t$src", [(MSP430call (load addr:$src))]>;
  def CALLn     : II16n<0b101, (outs), (ins indreg:$rs), "call\t$rs", []>;
  def CALLp     : II16p<0b101, (outs), (ins postreg:$rs), "call\t$rs", []>;
}

//===----------------------------------------------------------------------===//
//  Miscellaneous Instructions...
//
let Defs = [SP], Uses = [SP], hasSideEffects = 0 in {
let mayLoad = 1 in
def POP16r   : IForm16<0b0100, DstReg, SrcPostInc, 2,
                       (outs GR16:$rd), (ins), "pop\t$rd", []> {
  let DecoderNamespace = "Delta";
  let rs = 1;
}

let mayStore = 1 in {
def PUSH8r :  II8r<0b100, (outs), (ins GR8:$rs), "push.b\t$rs", []>;
def PUSH16r : II16r<0b100, (outs), (ins GR16:$rs), "push\t$rs", []>;
def PUSH16c : II16c<0b100, (outs), (ins cg16imm:$imm), "push\t$imm", []>;
def PUSH16i : II16i<0b100, (outs), (ins i16imm:$imm), "push\t$imm", []>;
}
}

//===----------------------------------------------------------------------===//
// Move Instructions

let hasSideEffects = 0 in {
def MOV8rr  : I8rr<0b0100,
                   (outs GR8:$rd), (ins GR8:$rs),
                   "mov.b\t{$rs, $rd}",
                   []>;
def MOV16rr : I16rr<0b0100,
                    (outs GR16:$rd), (ins GR16:$rs),
                    "mov\t{$rs, $rd}",
                    []>;
}

let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
def MOV8rc : I8rc<0b0100,
                   (outs GR8:$rd), (ins cg8imm:$imm),
                   "mov.b\t$imm, $rd",
                   [(set GR8:$rd, cg8imm:$imm)]>;
def MOV16rc : I16rc<0b0100,
                    (outs GR16:$rd), (ins cg16imm:$imm),
                    "mov\t$imm, $rd",
                    [(set GR16:$rd, cg16imm:$imm)]>;
def MOV8ri  : I8ri<0b0100,
                   (outs GR8:$rd), (ins i8imm:$imm),
                   "mov.b\t{$imm, $rd}",
                   [(set GR8:$rd, imm:$imm)]>;
def MOV16ri : I16ri<0b0100,
                    (outs GR16:$rd), (ins i16imm:$imm),
                    "mov\t{$imm, $rd}",
                    [(set GR16:$rd, imm:$imm)]>;
}

let canFoldAsLoad = 1, isReMaterializable = 1 in {
def MOV8rm  : I8rm<0b0100,
                   (outs GR8:$rd), (ins memsrc:$src),
                   "mov.b\t{$src, $rd}",
                   [(set GR8:$rd, (load addr:$src))]>;
def MOV16rm : I16rm<0b0100,
                    (outs GR16:$rd), (ins memsrc:$src),
                    "mov\t{$src, $rd}",
                    [(set GR16:$rd, (load addr:$src))]>;
def MOV8rn  : I8rn<0b0100,
                   (outs GR8:$rd), (ins indreg:$rs),
                   "mov.b\t{$rs, $rd}",
                   [(set GR8:$rd, (load addr:$rs))]>;
def MOV16rn : I16rn<0b0100,
                    (outs GR16:$rd), (ins indreg:$rs),
                    "mov\t{$rs, $rd}",
                    [(set GR16:$rd, (load addr:$rs))]>;
}

let isCodeGenOnly = 1 in {
def MOVZX16rr8 : I8rr<0b0100,
                      (outs GR16:$rd), (ins GR8:$rs),
                      "mov.b\t{$rs, $rd}",
                      [(set GR16:$rd, (zext GR8:$rs))]>;
def MOVZX16rm8 : I8rm<0b0100,
                      (outs GR16:$rd), (ins memsrc:$src),
                      "mov.b\t{$src, $rd}",
                      [(set GR16:$rd, (zextloadi16i8 addr:$src))]>;
}

let mayLoad = 1, hasExtraDefRegAllocReq = 1, Constraints = "$rs = $wb" in {
def MOV8rp  : I8rp<0b0100,
                   (outs GR8:$rd, GR16:$wb), (ins postreg:$rs),
                   "mov.b\t{$rs, $rd}", []>;
def MOV16rp : I16rp<0b0100,
                    (outs GR16:$rd, GR16:$wb), (ins postreg:$rs),
                    "mov\t{$rs, $rd}", []>;
}

// Any instruction that defines a 8-bit result leaves the high half of the
// register. Truncate can be lowered to EXTRACT_SUBREG, and CopyFromReg may
// be copying from a truncate, but any other 8-bit operation will zero-extend
// up to 16 bits.
def def8 : PatLeaf<(i8 GR8:$src), [{
  return N->getOpcode() != ISD::TRUNCATE &&
         N->getOpcode() != TargetOpcode::EXTRACT_SUBREG &&
         N->getOpcode() != ISD::CopyFromReg;
}]>;

// In the case of a 8-bit def that is known to implicitly zero-extend,
// we can use a SUBREG_TO_REG.
def : Pat<(i16 (zext def8:$src)),
          (SUBREG_TO_REG (i16 0), GR8:$src, subreg_8bit)>;

def MOV8mc  : I8mc<0b0100,
                   (outs), (ins memdst:$dst, cg8imm:$imm),
                   "mov.b\t{$imm, $dst}",
                   [(store (i8 cg8imm:$imm), addr:$dst)]>;
def MOV16mc : I16mc<0b0100,
                    (outs), (ins memdst:$dst, cg16imm:$imm),
                    "mov\t{$imm, $dst}",
                    [(store (i16 cg16imm:$imm), addr:$dst)]>;

def MOV8mi  : I8mi<0b0100,
                   (outs), (ins memdst:$dst, i8imm:$imm),
                   "mov.b\t{$imm, $dst}",
                   [(store (i8 imm:$imm), addr:$dst)]>;
def MOV16mi : I16mi<0b0100,
                    (outs), (ins memdst:$dst, i16imm:$imm),
                    "mov\t{$imm, $dst}",
                    [(store (i16 imm:$imm), addr:$dst)]>;

def MOV8mr  : I8mr<0b0100,
                   (outs), (ins memdst:$dst, GR8:$rs),
                   "mov.b\t{$rs, $dst}",
                   [(store GR8:$rs, addr:$dst)]>;
def MOV16mr : I16mr<0b0100,
                    (outs), (ins memdst:$dst, GR16:$rs),
                    "mov\t{$rs, $dst}",
                    [(store GR16:$rs, addr:$dst)]>;

def MOV8mm  : I8mm<0b0100,
                   (outs), (ins memdst:$dst, memsrc:$src),
                   "mov.b\t{$src, $dst}",
                   [(store (i8 (load addr:$src)), addr:$dst)]>;
def MOV16mm : I16mm<0b0100,
                    (outs), (ins memdst:$dst, memsrc:$src),
                    "mov\t{$src, $dst}",
                    [(store (i16 (load addr:$src)), addr:$dst)]>;

def MOV8mn  : I8mn<0b0100, (outs), (ins memdst:$dst, indreg:$rs),
                   "mov.b\t{$rs, $dst}", []>;
def MOV16mn : I16mn<0b0100, (outs), (ins memdst:$dst, indreg:$rs),
                    "mov\t{$rs, $dst}", []>;

//===----------------------------------------------------------------------===//
// Arithmetic Instructions

multiclass Arith<bits<4> opcode, string asmstring, SDPatternOperator node,
                 bit commutes, list<Register> uses> {
  let Defs = [SR], Uses = uses in {
  let Constraints = "$src2 = $rd" in {
  let isCommutable = commutes in {
  def 8rr : I8rr<opcode, (outs GR8:$rd), (ins GR8:$src2, GR8:$rs),
                 !strconcat(asmstring, ".b\t$rs, $rd"),
                 [(set GR8:$rd, (node GR8:$src2, GR8:$rs)),
                  (implicit SR)]>;
  def 16rr : I16rr<opcode, (outs GR16:$rd), (ins GR16:$src2, GR16:$rs),
                   !strconcat(asmstring, "\t$rs, $rd"),
                   [(set GR16:$rd, (node GR16:$src2, GR16:$rs)),
                    (implicit SR)]>;
  }
  def 8rm : I8rm<opcode, (outs GR8:$rd), (ins GR8:$src2, memsrc:$src),
                 !strconcat(asmstring, ".b\t$src, $rd"),
                 [(set GR8:$rd, (node GR8:$src2, (load addr:$src))),
                  (implicit SR)]>;
  def 16rm : I16rm<opcode, (outs GR16:$rd), (ins GR16:$src2, memsrc:$src),
                   !strconcat(asmstring, "\t$src, $rd"),
                   [(set GR16:$rd, (node GR16:$src2, (load addr:$src))),
                    (implicit SR)]>;
  def 8rn : I8rn<opcode, (outs GR8:$rd), (ins GR8:$src2, indreg:$rs),
                 !strconcat(asmstring, ".b\t$rs, $rd"), []>;
  def 16rn : I16rn<opcode, (outs GR16:$rd), (ins GR16:$src2, indreg:$rs),
                   !strconcat(asmstring, "\t$rs, $rd"), []>;
  let mayLoad = 1,
      hasExtraDefRegAllocReq = 1,
      Constraints = "$rs = $wb, $src2 = $rd" in {
  def 8rp : I8rp<opcode, (outs GR8:$rd, GR16:$wb), (ins GR8:$src2, postreg:$rs),
                 !strconcat(asmstring, ".b\t$rs, $rd"), []>;
  def 16rp : I16rp<opcode, (outs GR16:$rd, GR16:$wb), (ins GR16:$src2, postreg:$rs),
                   !strconcat(asmstring, "\t$rs, $rd"), []>;
  }
  def 8rc : I8rc<opcode, (outs GR8:$rd), (ins GR8:$src2, cg8imm:$imm),
                 !strconcat(asmstring, ".b\t$imm, $rd"),
                 [(set GR8:$rd, (node GR8:$src2, cg8imm:$imm)),
                  (implicit SR)]>;
  def 16rc : I16rc<opcode, (outs GR16:$rd), (ins GR16:$src2, cg16imm:$imm),
                 !strconcat(asmstring, "\t$imm, $rd"),
                 [(set GR16:$rd, (node GR16:$src2, cg16imm:$imm)),
                  (implicit SR)]>;
  def 8ri : I8ri<opcode, (outs GR8:$rd), (ins GR8:$src2, i8imm:$imm),
                 !strconcat(asmstring, ".b\t$imm, $rd"),
                 [(set GR8:$rd, (node GR8:$src2, imm:$imm)),
                  (implicit SR)]>;
  def 16ri : I16ri<opcode, (outs GR16:$rd), (ins GR16:$src2, i16imm:$imm),
                 !strconcat(asmstring, "\t$imm, $rd"),
                 [(set GR16:$rd, (node GR16:$src2, imm:$imm)),
                  (implicit SR)]>;
  }
  def 8mr : I8mr<opcode, (outs), (ins memdst:$dst, GR8:$rs),
                 !strconcat(asmstring, ".b\t$rs, $dst"),
                 [(store (node (load addr:$dst), GR8:$rs), addr:$dst),
                  (implicit SR)]>;
  def 16mr : I16mr<opcode, (outs), (ins memdst:$dst, GR16:$rs),
                   !strconcat(asmstring, "\t$rs, $dst"),
                   [(store (node (load addr:$dst), GR16:$rs), addr:$dst),
                    (implicit SR)]>;
  def 8mc : I8mc<opcode, (outs), (ins memdst:$dst, cg8imm:$imm),
                 !strconcat(asmstring, ".b\t$imm, $dst"),
                 [(store (node (load addr:$dst), (i8 cg8imm:$imm)), addr:$dst),
                  (implicit SR)]>;
  def 16mc : I16mc<opcode, (outs), (ins memdst:$dst, cg16imm:$imm),
                   !strconcat(asmstring, "\t$imm, $dst"),
                   [(store (node (load addr:$dst), (i16 cg16imm:$imm)), addr:$dst),
                    (implicit SR)]>;
  def 8mi : I8mi<opcode, (outs), (ins memdst:$dst, i8imm:$imm),
                 !strconcat(asmstring, ".b\t$imm, $dst"),
                 [(store (node (load addr:$dst), (i8 imm:$imm)), addr:$dst),
                  (implicit SR)]>;
  def 16mi : I16mi<opcode, (outs), (ins memdst:$dst, i16imm:$imm),
                   !strconcat(asmstring, "\t$imm, $dst"),
                   [(store (node (load addr:$dst), (i16 imm:$imm)), addr:$dst),
                    (implicit SR)]>;
  def 8mm : I8mm<opcode, (outs), (ins memdst:$dst, memsrc:$src),
                 !strconcat(asmstring, ".b\t$src, $dst"),
                 [(store (node (load addr:$dst), 
                               (i8 (load addr:$src))), addr:$dst),
                  (implicit SR)]>;
  def 16mm : I16mm<opcode, (outs), (ins memdst:$dst, memsrc:$src),
                   !strconcat(asmstring, "\t$src, $dst"),
                   [(store (node (load addr:$dst), 
                                 (i16 (load addr:$src))), addr:$dst),
                    (implicit SR)]>;
  def 8mn : I8mn<opcode, (outs), (ins memdst:$dst, indreg:$rs),
                 !strconcat(asmstring, ".b\t$rs, $dst"), []>;
  def 16mn : I16mn<opcode, (outs), (ins memdst:$dst, indreg:$rs),
                   !strconcat(asmstring, "\t$rs, $dst"), []>;
  def 8mp : I8mp<opcode, (outs), (ins memdst:$dst, postreg:$rs),
                 !strconcat(asmstring, ".b\t$rs, $dst"), []>;
  def 16mp : I16mp<opcode, (outs), (ins memdst:$dst, postreg:$rs),
                   !strconcat(asmstring, "\t$rs, $dst"), []>;
  }
}

defm ADD  : Arith<0b0101, "add",  add,  1, []>;
defm ADDC : Arith<0b0110, "addc", adde, 1, [SR]>;
defm AND  : Arith<0b1111, "and",  and,  1, []>;
defm BIS  : Arith<0b1101, "bis",  or,   1, []>;
defm BIC  : Arith<0b1100, "bic",  bic,  0, []>;
defm XOR  : Arith<0b1110, "xor",  xor,  1, []>;
defm SUB  : Arith<0b1000, "sub",  sub,  0, []>;
defm SUBC : Arith<0b0111, "subc", sube, 0, [SR]>;
defm DADD : Arith<0b1010, "dadd", MSP430dadd, 1, [SR]>;

def ADC8r   : InstAlias<"adc.b\t$dst",  (ADDC8rc   GR8:$dst,     0)>;
def ADC16r  : InstAlias<"adc\t$dst",    (ADDC16rc  GR16:$dst,    0)>;
def ADC8m   : InstAlias<"adc.b\t$dst",  (ADDC8mc   memdst:$dst,  0)>;
def ADC16m  : InstAlias<"adc\t$dst",    (ADDC16mc  memdst:$dst,  0)>;

def DADC8r  : InstAlias<"dadc.b\t$dst", (DADD8rc   GR8:$dst,     0)>;
def DADC16r : InstAlias<"dadc\t$dst",   (DADD16rc  GR16:$dst,    0)>;
def DADC8m  : InstAlias<"dadc.b\t$dst", (DADD8mc   memdst:$dst,  0)>;
def DADC16m : InstAlias<"dadc\t$dst",   (DADD16mc  memdst:$dst,  0)>;

def DEC8r   : InstAlias<"dec.b\t$dst",  (SUB8rc    GR8:$dst,     1)>;
def DEC16r  : InstAlias<"dec\t$dst",    (SUB16rc   GR16:$dst,    1)>;
def DEC8m   : InstAlias<"dec.b\t$dst",  (SUB8mc    memdst:$dst,  1)>;
def DEC16m  : InstAlias<"dec\t$dst",    (SUB16mc   memdst:$dst,  1)>;

def DECD8r  : InstAlias<"decd.b\t$dst", (SUB8rc    GR8:$dst,     2)>;
def DECD16r : InstAlias<"decd\t$dst",   (SUB16rc   GR16:$dst,    2)>;
def DECD8m  : InstAlias<"decd.b\t$dst", (SUB8mc    memdst:$dst,  2)>;
def DECD16m : InstAlias<"decd\t$dst",   (SUB16mc   memdst:$dst,  2)>;

def INC8r   : InstAlias<"inc.b\t$dst",  (ADD8rc    GR8:$dst,     1)>;
def INC16r  : InstAlias<"inc\t$dst",    (ADD16rc   GR16:$dst,    1)>;
def INC8m   : InstAlias<"inc.b\t$dst",  (ADD8mc    memdst:$dst,  1)>;
def INC16m  : InstAlias<"inc\t$dst",    (ADD16mc   memdst:$dst,  1)>;

def INCD8r  : InstAlias<"incd.b\t$dst", (ADD8rc    GR8:$dst,     2)>;
def INCD16r : InstAlias<"incd\t$dst",   (ADD16rc   GR16:$dst,    2)>;
def INCD8m  : InstAlias<"incd.b\t$dst", (ADD8mc    memdst:$dst,  2)>;
def INCD16m : InstAlias<"incd\t$dst",   (ADD16mc   memdst:$dst,  2)>;

def SBC8r   : InstAlias<"sbc.b\t$dst",  (SUBC8rc   GR8:$dst,     0)>;
def SBC16r  : InstAlias<"sbc\t$dst",    (SUBC16rc  GR16:$dst,    0)>;
def SBC8m   : InstAlias<"sbc.b\t$dst",  (SUBC8mc   memdst:$dst,  0)>;
def SBC16m  : InstAlias<"sbc\t$dst",    (SUBC16mc  memdst:$dst,  0)>;

def INV8r   : InstAlias<"inv.b\t$dst",  (XOR8rc    GR8:$dst,    -1)>;
def INV16r  : InstAlias<"inv\t$dst",    (XOR16rc   GR16:$dst,   -1)>;
def INV8m   : InstAlias<"inv.b\t$dst",  (XOR8mc    memdst:$dst, -1)>;
def INV16m  : InstAlias<"inv\t$dst",    (XOR16mc   memdst:$dst, -1)>;

// printAliasInstr() doesn't check $dst operands are actually equal
// for RLA and RLC aliases below, so disable printing aliases.

def RLA8r   : InstAlias<"rla.b\t$dst",  (ADD8rr    GR8:$dst,     GR8:$dst),    0>;
def RLA16r  : InstAlias<"rla\t$dst",    (ADD16rr   GR16:$dst,    GR16:$dst),   0>;
def RLA8m   : InstAlias<"rla.b\t$dst",  (ADD8mm    memdst:$dst,  memdst:$dst), 0>;
def RLA16m  : InstAlias<"rla\t$dst",    (ADD16mm   memdst:$dst,  memdst:$dst), 0>;

def RLC8r   : InstAlias<"rlc.b\t$dst",  (ADDC8rr   GR8:$dst,     GR8:$dst),    0>;
def RLC16r  : InstAlias<"rlc\t$dst",    (ADDC16rr  GR16:$dst,    GR16:$dst),   0>;
def RLC8m   : InstAlias<"rlc.b\t$dst",  (ADDC8mm   memdst:$dst,  memdst:$dst), 0>;
def RLC16m  : InstAlias<"rlc\t$dst",    (ADDC16mm  memdst:$dst,  memdst:$dst), 0>;

def DINT : InstAlias<"dint", (BIC16rc SR, 8)>;
def EINT : InstAlias<"eint", (BIS16rc SR, 8)>;

def NOP  : InstAlias<"nop",  (MOV16rc CG, 0)>;

def CLR8r   : InstAlias<"clr.b\t$dst",  (MOV8rc    GR8:$dst,     0)>;
def CLR16r  : InstAlias<"clr\t$dst",    (MOV16rc   GR16:$dst,    0)>;
def CLR8m   : InstAlias<"clr.b\t$dst",  (MOV8mc    memdst:$dst,  0)>;
def CLR16m  : InstAlias<"clr\t$dst",    (MOV16mc   memdst:$dst,  0)>;

def CLRC : InstAlias<"clrc", (BIC16rc SR, 1)>;
def CLRN : InstAlias<"clrn", (BIC16rc SR, 4)>;
def CLRZ : InstAlias<"clrz", (BIC16rc SR, 2)>;
def SETC : InstAlias<"setc", (BIS16rc SR, 1)>;
def SETN : InstAlias<"setn", (BIS16rc SR, 4)>;
def SETZ : InstAlias<"setz", (BIS16rc SR, 2)>;

def : Pat<(MSP430rla GR8:$dst),  (ADD8rr  $dst, $dst)>;
def : Pat<(MSP430rla GR16:$dst), (ADD16rr $dst, $dst)>;

// Format-II (Single Operand) Instruction
// Register mode
let Constraints = "$rs = $rd" in {

let Defs = [SR] in {
def RRA8r :   II8r<0b010,
                   (outs GR8:$rd), (ins GR8:$rs),
                   "rra.b\t$rd",
                   [(set GR8:$rd, (MSP430rra GR8:$rs)),
                    (implicit SR)]>;
def RRA16r : II16r<0b010,
                    (outs GR16:$rd), (ins GR16:$rs),
                    "rra\t$rd",
                    [(set GR16:$rd, (MSP430rra GR16:$rs)),
                     (implicit SR)]>;

let Uses = [SR] in {
def RRC8r :   II8r<0b000,
                   (outs GR8:$rd), (ins GR8:$rs),
                   "rrc.b\t$rd",
                   [(set GR8:$rd, (MSP430rrc GR8:$rs)),
                    (implicit SR)]>;
def RRC16r : II16r<0b000,
                   (outs GR16:$rd), (ins GR16:$rs),
                   "rrc\t$rd",
                   [(set GR16:$rd, (MSP430rrc GR16:$rs)),
                    (implicit SR)]>;
} // Uses = [SR]

def SEXT16r : II16r<0b011,
                    (outs GR16:$rd), (ins GR16:$rs),
                    "sxt\t$rd",
                    [(set GR16:$rd, (sext_inreg GR16:$rs, i8)),
                     (implicit SR)]>;

} // Defs = [SR]

let isCodeGenOnly = 1 in
def ZEXT16r : I8rr<0b0100,
                   (outs GR16:$rd), (ins GR16:$rs),
                   "mov.b\t{$rs, $rd}",
                   [(set GR16:$rd, (zext (trunc GR16:$rs)))]>;

def SWPB16r : II16r<0b001,
                    (outs GR16:$rd), (ins GR16:$rs),
                    "swpb\t$rd",
                    [(set GR16:$rd, (bswap GR16:$rs))]>;

} // Constraints = "$src = $dst"

// Indexed, indirect register and indirect autoincrement modes
let Defs = [SR] in {
def RRA8m  : II8m<0b010,
                   (outs), (ins memsrc:$src),
                   "rra.b\t$src",
                   [(store (MSP430rra (i8 (load addr:$src))), addr:$src),
                    (implicit SR)]>;
def RRA16m : II16m<0b010,
                   (outs), (ins memsrc:$src),
                   "rra\t$src",
                   [(store (MSP430rra (i16 (load addr:$src))), addr:$src),
                    (implicit SR)]>;

def RRA8n  : II8n<0b010, (outs), (ins indreg:$rs), "rra.b\t$rs", []>;
def RRA16n : II16n<0b010, (outs), (ins indreg:$rs), "rra\t$rs", []>;
def RRA8p  : II8p<0b010, (outs), (ins postreg:$rs), "rra.b\t$rs", []>;
def RRA16p : II16p<0b010, (outs), (ins postreg:$rs), "rra\t$rs", []>;

let Uses = [SR] in {
def RRC8m  : II8m<0b000,
                   (outs), (ins memsrc:$src),
                   "rrc.b\t$src",
                   [(store (MSP430rrc (i8 (load addr:$src))), addr:$src),
                    (implicit SR)]>;
def RRC16m : II16m<0b000,
                   (outs), (ins memsrc:$src),
                   "rrc\t$src",
                   [(store (MSP430rrc (i16 (load addr:$src))), addr:$src),
                    (implicit SR)]>;

def RRC8n  : II8n<0b000, (outs), (ins indreg:$rs), "rrc.b\t$rs", []>;
def RRC16n : II16n<0b000, (outs), (ins indreg:$rs), "rrc\t$rs", []>;
def RRC8p  : II8p<0b000, (outs), (ins postreg:$rs), "rrc.b\t$rs", []>;
def RRC16p : II16p<0b000, (outs), (ins postreg:$rs), "rrc\t$rs", []>;

} // Uses = [SR]

def SEXT16m : II16m<0b011,
                    (outs), (ins memsrc:$src),
                    "sxt\t$src",
                    [(store (sext_inreg (extloadi16i8 addr:$src), i8),
                             addr:$src),
                     (implicit SR)]>;
def SEXT16n : II16n<0b011, (outs), (ins indreg:$rs), "sxt\t$rs", []>;
def SEXT16p : II16p<0b011, (outs), (ins postreg:$rs), "sxt\t$rs", []>;

} // Defs = [SR]

def SWPB16m : II16m<0b001,
                   (outs), (ins memsrc:$src),
                   "swpb\t$src",
                   [(store (bswap (i16 (load addr:$src))), addr:$src)]>;
def SWPB16n : II16n<0b001, (outs), (ins indreg:$rs), "swpb\t$rs", []>;
def SWPB16p : II16p<0b001, (outs), (ins postreg:$rs), "swpb\t$rs", []>;

// Integer comparisons
let Defs = [SR] in {
def CMP8rr  : I8rr<0b1001,
                   (outs), (ins GR8:$rd, GR8:$rs),
                   "cmp.b\t$rs, $rd",
                   [(MSP430cmp GR8:$rd, GR8:$rs), (implicit SR)]>;
def CMP16rr : I16rr<0b1001,
                    (outs), (ins GR16:$rd, GR16:$rs),
                    "cmp\t$rs, $rd",
                    [(MSP430cmp GR16:$rd, GR16:$rs), (implicit SR)]>;

def CMP8rc  : I8rc<0b1001,
                   (outs), (ins GR8:$rd, cg8imm:$imm),
                   "cmp.b\t$imm, $rd",
                   [(MSP430cmp GR8:$rd, cg8imm:$imm), (implicit SR)]>;
def CMP16rc : I16rc<0b1001,
                    (outs), (ins GR16:$rd, cg16imm:$imm),
                    "cmp\t$imm, $rd",
                    [(MSP430cmp GR16:$rd, cg16imm:$imm), (implicit SR)]>;

def CMP8ri  : I8ri<0b1001,
                   (outs), (ins GR8:$rd, i8imm:$imm),
                   "cmp.b\t$imm, $rd",
                   [(MSP430cmp GR8:$rd, imm:$imm), (implicit SR)]>;
def CMP16ri : I16ri<0b1001,
                    (outs), (ins GR16:$rd, i16imm:$imm),
                    "cmp\t$imm, $rd",
                    [(MSP430cmp GR16:$rd, imm:$imm), (implicit SR)]>;

def CMP8mc  : I8mc<0b1001,
                   (outs), (ins memsrc:$dst, cg8imm:$imm),
                   "cmp.b\t$imm, $dst",
                   [(MSP430cmp (load addr:$dst), (i8 cg8imm:$imm)),
                    (implicit SR)]>;
def CMP16mc : I16mc<0b1001,
                    (outs), (ins memsrc:$dst, cg16imm:$imm),
                    "cmp\t$imm, $dst",
                    [(MSP430cmp (load addr:$dst), (i16 cg16imm:$imm)),
                     (implicit SR)]>;

def CMP8mi  : I8mi<0b1001,
                   (outs), (ins memsrc:$dst, i8imm:$imm),
                   "cmp.b\t$imm, $dst",
                   [(MSP430cmp (load addr:$dst),
                               (i8 imm:$imm)), (implicit SR)]>;
def CMP16mi : I16mi<0b1001,
                    (outs), (ins memsrc:$dst, i16imm:$imm),
                    "cmp\t$imm, $dst",
                     [(MSP430cmp (load addr:$dst),
                                 (i16 imm:$imm)), (implicit SR)]>;

def CMP8rm  : I8rm<0b1001,
                   (outs), (ins GR8:$rd, memsrc:$src),
                   "cmp.b\t$src, $rd",
                   [(MSP430cmp GR8:$rd, (load addr:$src)), 
                    (implicit SR)]>;
def CMP16rm : I16rm<0b1001,
                    (outs), (ins GR16:$rd, memsrc:$src),
                    "cmp\t$src, $rd",
                    [(MSP430cmp GR16:$rd, (load addr:$src)),
                     (implicit SR)]>;

def CMP8rn  : I8rn<0b1001,
                   (outs), (ins GR8:$rd, indreg:$rs), "cmp.b\t$rs, $rd", []>;
def CMP16rn : I16rn<0b1001,
                    (outs), (ins GR16:$rd, indreg:$rs), "cmp\t$rs, $rd", []>;

def CMP8rp  : I8rp<0b1001,
                   (outs), (ins GR8:$rd, postreg:$rs), "cmp.b\t$rs, $rd", []>;
def CMP16rp : I16rp<0b1001,
                    (outs), (ins GR16:$rd, postreg:$rs), "cmp\t$rs, $rd", []>;

def CMP8mr  : I8mr<0b1001,
                   (outs), (ins memsrc:$dst, GR8:$rs),
                   "cmp.b\t$rs, $dst",
                   [(MSP430cmp (load addr:$dst), GR8:$rs),
                    (implicit SR)]>;
def CMP16mr : I16mr<0b1001,
                    (outs), (ins memsrc:$dst, GR16:$rs),
                    "cmp\t$rs, $dst",
                    [(MSP430cmp (load addr:$dst), GR16:$rs), 
                     (implicit SR)]>;
def CMP8mm  : I8mm<0b1001,
                   (outs), (ins memdst:$dst, memsrc:$src),
                   "cmp.b\t$src, $dst",
                   [(MSP430cmp (load addr:$dst), (i8 (load addr:$src))),
                    (implicit SR)]>;
def CMP16mm : I16mm<0b1001, (outs), (ins memdst:$dst, memsrc:$src),
                    "cmp\t$src, $dst",
                    [(MSP430cmp (load addr:$dst), (i16 (load addr:$src))),
                     (implicit SR)]>;

def CMP8mn  : I8mn<0b1001, (outs), (ins memsrc:$dst, indreg:$rs),
                   "cmp.b\t$rs, $dst", []>;
def CMP16mn : I16mn<0b1001, (outs), (ins memsrc:$dst, indreg:$rs),
                    "cmp\t$rs, $dst", []>;

def CMP8mp  : I8mp<0b1001, (outs), (ins memsrc:$dst, postreg:$rs),
                   "cmp.b\t$rs, $dst", []>;
def CMP16mp : I16mp<0b1001, (outs), (ins memsrc:$dst, postreg:$rs),
                    "cmp\t$rs, $dst", []>;

// BIT TESTS, just sets condition codes
// Note that the C condition is set differently than when using CMP.
let isCommutable = 1 in {
def BIT8rr  : I8rr<0b1011,
                   (outs), (ins GR8:$rd, GR8:$rs),
                   "bit.b\t$rs, $rd",
                   [(MSP430cmp (and_su GR8:$rd, GR8:$rs), 0),
                    (implicit SR)]>;
def BIT16rr : I16rr<0b1011,
                    (outs), (ins GR16:$rd, GR16:$rs),
                    "bit\t$rs, $rd",
                    [(MSP430cmp (and_su GR16:$rd, GR16:$rs), 0),
                     (implicit SR)]>;
}
def BIT8rc  : I8rc<0b1011,
                   (outs), (ins GR8:$rd, cg8imm:$imm),
                   "bit.b\t$imm, $rd",
                   [(MSP430cmp (and_su GR8:$rd, cg8imm:$imm), 0),
                    (implicit SR)]>;
def BIT16rc : I16rc<0b1011,
                    (outs), (ins GR16:$rd, cg16imm:$imm),
                    "bit\t$imm, $rd",
                    [(MSP430cmp (and_su GR16:$rd, cg16imm:$imm), 0),
                     (implicit SR)]>;

def BIT8ri  : I8ri<0b1011,
                   (outs), (ins GR8:$rd, i8imm:$imm),
                   "bit.b\t$imm, $rd",
                   [(MSP430cmp (and_su GR8:$rd, imm:$imm), 0),
                    (implicit SR)]>;
def BIT16ri : I16ri<0b1011,
                    (outs), (ins GR16:$rd, i16imm:$imm),
                    "bit\t$imm, $rd",
                    [(MSP430cmp (and_su GR16:$rd, imm:$imm), 0),
                     (implicit SR)]>;

def BIT8rm  : I8rm<0b1011,
                   (outs), (ins GR8:$rd, memdst:$src),
                   "bit.b\t$src, $rd",
                   [(MSP430cmp (and_su GR8:$rd,  (load addr:$src)), 0),
                    (implicit SR)]>;
def BIT16rm : I16rm<0b1011,
                    (outs), (ins GR16:$rd, memdst:$src),
                    "bit\t$src, $rd",
                    [(MSP430cmp (and_su GR16:$rd,  (load addr:$src)), 0),
                     (implicit SR)]>;

def BIT8rn  : I8rn<0b1011, (outs), (ins GR8:$rd, indreg:$rs),
                   "bit.b\t$rs, $rd", []>;
def BIT16rn : I16rn<0b1011, (outs), (ins GR16:$rd, indreg:$rs),
                    "bit\t$rs, $rd", []>;

def BIT8rp  : I8rp<0b1011, (outs), (ins GR8:$rd, postreg:$rs),
                   "bit.b\t$rs, $rd", []>;
def BIT16rp : I16rp<0b1011, (outs), (ins GR16:$rd, postreg:$rs),
                    "bit\t$rs, $rd", []>;

def BIT8mr  : I8mr<0b1011,
                  (outs), (ins memsrc:$dst, GR8:$rs),
                  "bit.b\t$rs, $dst",
                  [(MSP430cmp (and_su (load addr:$dst), GR8:$rs), 0),
                   (implicit SR)]>;
def BIT16mr : I16mr<0b1011,
                    (outs), (ins memsrc:$dst, GR16:$rs),
                    "bit\t$rs, $dst",
                    [(MSP430cmp (and_su (load addr:$dst), GR16:$rs), 0),
                     (implicit SR)]>;

def BIT8mc  : I8mc<0b1011,
                   (outs), (ins memsrc:$dst, cg8imm:$imm),
                   "bit.b\t$imm, $dst",
                   [(MSP430cmp (and_su (load addr:$dst), (i8 cg8imm:$imm)), 0),
                    (implicit SR)]>;
def BIT16mc : I16mc<0b1011,
                    (outs), (ins memdst:$dst, cg16imm:$imm),
                    "bit\t$imm, $dst",
                    [(MSP430cmp (and_su (load addr:$dst), (i16 cg16imm:$imm)), 0),
                     (implicit SR)]>;

def BIT8mi  : I8mi<0b1011,
                   (outs), (ins memsrc:$dst, i8imm:$imm),
                   "bit.b\t$imm, $dst",
                   [(MSP430cmp (and_su (load addr:$dst), (i8 imm:$imm)), 0),
                    (implicit SR)]>;
def BIT16mi : I16mi<0b1011,
                    (outs), (ins memsrc:$dst, i16imm:$imm),
                    "bit\t$imm, $dst",
                    [(MSP430cmp (and_su (load addr:$dst), (i16 imm:$imm)), 0),
                     (implicit SR)]>;

def BIT8mm  : I8mm<0b1011,
                   (outs), (ins memsrc:$dst, memsrc:$src),
                   "bit.b\t$src, $dst",
                   [(MSP430cmp (and_su (i8 (load addr:$dst)),
                                       (load addr:$src)),
                                 0),
                      (implicit SR)]>;
def BIT16mm : I16mm<0b1011,
                    (outs), (ins memsrc:$dst, memsrc:$src),
                    "bit\t$src, $dst",
                    [(MSP430cmp (and_su (i16 (load addr:$dst)),
                                        (load addr:$src)),
                                 0),
                     (implicit SR)]>;
def BIT8mn  : I8mn<0b1011, (outs), (ins memsrc:$dst, indreg:$rs),
                   "bit.b\t$rs, $dst", []>;
def BIT16mn : I16mn<0b1011, (outs), (ins memsrc:$dst, indreg:$rs),
                    "bit\t$rs, $dst", []>;

def BIT8mp  : I8mp<0b1011, (outs), (ins memsrc:$dst, postreg:$rs),
                   "bit.b\t$rs, $dst", []>;
def BIT16mp : I16mp<0b1011, (outs), (ins memsrc:$dst, postreg:$rs),
                    "bit\t$rs, $dst", []>;

} // Defs = [SR]

def TST8r   : InstAlias<"tst.b\t$dst",  (CMP8rc    GR8:$dst,     0)>;
def TST16r  : InstAlias<"tst\t$dst",    (CMP16rc   GR16:$dst,    0)>;
def TST8m   : InstAlias<"tst.b\t$dst",  (CMP8mc    memdst:$dst,  0)>;
def TST16m  : InstAlias<"tst\t$dst",    (CMP16mc   memdst:$dst,  0)>;

//===----------------------------------------------------------------------===//
// Non-Instruction Patterns

// extload
def : Pat<(extloadi16i8 addr:$src), (MOVZX16rm8 addr:$src)>;

// anyext
def : Pat<(i16 (anyext GR8:$src)),
          (SUBREG_TO_REG (i16 0), GR8:$src, subreg_8bit)>;

// truncs
def : Pat<(i8 (trunc GR16:$src)),
          (EXTRACT_SUBREG GR16:$src, subreg_8bit)>;

// GlobalAddress, ExternalSymbol
def : Pat<(i16 (MSP430Wrapper tglobaladdr:$dst)), (MOV16ri tglobaladdr:$dst)>;
def : Pat<(i16 (MSP430Wrapper texternalsym:$dst)), (MOV16ri texternalsym:$dst)>;
def : Pat<(i16 (MSP430Wrapper tblockaddress:$dst)), (MOV16ri tblockaddress:$dst)>;

def : Pat<(add GR16:$src, (MSP430Wrapper tglobaladdr :$src2)),
          (ADD16ri GR16:$src, tglobaladdr:$src2)>;
def : Pat<(add GR16:$src, (MSP430Wrapper texternalsym:$src2)),
          (ADD16ri GR16:$src, texternalsym:$src2)>;
def : Pat<(add GR16:$src, (MSP430Wrapper tblockaddress:$src2)),
          (ADD16ri GR16:$src, tblockaddress:$src2)>;

def : Pat<(store (i16 (MSP430Wrapper tglobaladdr:$src)), addr:$dst),
          (MOV16mi addr:$dst, tglobaladdr:$src)>;
def : Pat<(store (i16 (MSP430Wrapper texternalsym:$src)), addr:$dst),
          (MOV16mi addr:$dst, texternalsym:$src)>;
def : Pat<(store (i16 (MSP430Wrapper tblockaddress:$src)), addr:$dst),
          (MOV16mi addr:$dst, tblockaddress:$src)>;

// calls
def : Pat<(MSP430call (i16 tglobaladdr:$dst)),
          (CALLi tglobaladdr:$dst)>;
def : Pat<(MSP430call (i16 texternalsym:$dst)),
          (CALLi texternalsym:$dst)>;

// add and sub always produce carry
def : Pat<(addc GR16:$src, GR16:$src2),
          (ADD16rr GR16:$src, GR16:$src2)>;
def : Pat<(addc GR16:$src, (load addr:$src2)),
          (ADD16rm GR16:$src, addr:$src2)>;
def : Pat<(addc GR16:$src, imm:$src2),
          (ADD16ri GR16:$src, imm:$src2)>;
def : Pat<(store (addc (load addr:$dst), GR16:$src), addr:$dst),
          (ADD16mr addr:$dst, GR16:$src)>;
def : Pat<(store (addc (load addr:$dst), (i16 (load addr:$src))), addr:$dst),
          (ADD16mm addr:$dst, addr:$src)>;

def : Pat<(addc GR8:$src, GR8:$src2),
          (ADD8rr GR8:$src, GR8:$src2)>;
def : Pat<(addc GR8:$src, (load addr:$src2)),
          (ADD8rm GR8:$src, addr:$src2)>;
def : Pat<(addc GR8:$src, imm:$src2),
          (ADD8ri GR8:$src, imm:$src2)>;
def : Pat<(store (addc (load addr:$dst), GR8:$src), addr:$dst),
          (ADD8mr addr:$dst, GR8:$src)>;
def : Pat<(store (addc (load addr:$dst), (i8 (load addr:$src))), addr:$dst),
          (ADD8mm addr:$dst, addr:$src)>;

def : Pat<(subc GR16:$src, GR16:$src2),
          (SUB16rr GR16:$src, GR16:$src2)>;
def : Pat<(subc GR16:$src, (load addr:$src2)),
          (SUB16rm GR16:$src, addr:$src2)>;
def : Pat<(subc GR16:$src, imm:$src2),
          (SUB16ri GR16:$src, imm:$src2)>;
def : Pat<(store (subc (load addr:$dst), GR16:$src), addr:$dst),
          (SUB16mr addr:$dst, GR16:$src)>;
def : Pat<(store (subc (load addr:$dst), (i16 (load addr:$src))), addr:$dst),
          (SUB16mm addr:$dst, addr:$src)>;

def : Pat<(subc GR8:$src, GR8:$src2),
          (SUB8rr GR8:$src, GR8:$src2)>;
def : Pat<(subc GR8:$src, (load addr:$src2)),
          (SUB8rm GR8:$src, addr:$src2)>;
def : Pat<(subc GR8:$src, imm:$src2),
          (SUB8ri GR8:$src, imm:$src2)>;
def : Pat<(store (subc (load addr:$dst), GR8:$src), addr:$dst),
          (SUB8mr addr:$dst, GR8:$src)>;
def : Pat<(store (subc (load addr:$dst), (i8 (load addr:$src))), addr:$dst),
          (SUB8mm addr:$dst, addr:$src)>;

// peephole patterns
def : Pat<(and GR16:$src, 255), (ZEXT16r GR16:$src)>;
def : Pat<(MSP430cmp (trunc (and_su GR16:$src, GR16:$src2)), 0),
          (BIT8rr (EXTRACT_SUBREG GR16:$src, subreg_8bit),
                  (EXTRACT_SUBREG GR16:$src2, subreg_8bit))>;
