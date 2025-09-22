//===-- XCoreInstrInfo.td - Target Description for XCore ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the XCore instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

// Uses of CP, DP are not currently reflected in the patterns, since
// having a physical register as an operand prevents loop hoisting and
// since the value of these registers never changes during the life of the
// function.

//===----------------------------------------------------------------------===//
// Instruction format superclass.
//===----------------------------------------------------------------------===//

include "XCoreInstrFormats.td"

//===----------------------------------------------------------------------===//
// XCore specific DAG Nodes.
//

// Call
def SDT_XCoreBranchLink : SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>;
def XCoreBranchLink     : SDNode<"XCoreISD::BL",SDT_XCoreBranchLink,
                            [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                             SDNPVariadic]>;

def XCoreRetsp : SDNode<"XCoreISD::RETSP", SDTBrind,
                      [SDNPHasChain, SDNPOptInGlue, SDNPMayLoad, SDNPVariadic]>;

def SDT_XCoreEhRet : SDTypeProfile<0, 2,
                            [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;
def XCoreEhRet       : SDNode<"XCoreISD::EH_RETURN", SDT_XCoreEhRet,
                         [SDNPHasChain, SDNPOptInGlue]>;

def SDT_XCoreBR_JT    : SDTypeProfile<0, 2,
                                      [SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;

def XCoreBR_JT : SDNode<"XCoreISD::BR_JT", SDT_XCoreBR_JT,
                        [SDNPHasChain]>;

def XCoreBR_JT32 : SDNode<"XCoreISD::BR_JT32", SDT_XCoreBR_JT,
                        [SDNPHasChain]>;

def SDT_XCoreAddress    : SDTypeProfile<1, 1,
                            [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;

def pcrelwrapper : SDNode<"XCoreISD::PCRelativeWrapper", SDT_XCoreAddress,
                           []>;

def dprelwrapper : SDNode<"XCoreISD::DPRelativeWrapper", SDT_XCoreAddress,
                           []>;

def cprelwrapper : SDNode<"XCoreISD::CPRelativeWrapper", SDT_XCoreAddress,
                           []>;

def frametoargsoffset : SDNode<"XCoreISD::FRAME_TO_ARGS_OFFSET", SDTIntLeaf,
                               []>;

def SDT_XCoreStwsp    : SDTypeProfile<0, 2, [SDTCisInt<1>]>;
def XCoreStwsp        : SDNode<"XCoreISD::STWSP", SDT_XCoreStwsp,
                               [SDNPHasChain, SDNPMayStore]>;

def SDT_XCoreLdwsp    : SDTypeProfile<1, 1, [SDTCisInt<1>]>;
def XCoreLdwsp        : SDNode<"XCoreISD::LDWSP", SDT_XCoreLdwsp,
                               [SDNPHasChain, SDNPMayLoad]>;

// These are target-independent nodes, but have target-specific formats.
def SDT_XCoreCallSeqStart : SDCallSeqStart<[ SDTCisVT<0, i32>,
                                             SDTCisVT<1, i32> ]>;
def SDT_XCoreCallSeqEnd   : SDCallSeqEnd<[ SDTCisVT<0, i32>,
                                           SDTCisVT<1, i32> ]>;

def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_XCoreCallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end   : SDNode<"ISD::CALLSEQ_END",   SDT_XCoreCallSeqEnd,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

//===----------------------------------------------------------------------===//
// Instruction Pattern Stuff
//===----------------------------------------------------------------------===//

def div4_xform : SDNodeXForm<imm, [{
  // Transformation function: imm/4
  assert(N->getZExtValue() % 4 == 0);
  return getI32Imm(N->getZExtValue()/4, SDLoc(N));
}]>;

def msksize_xform : SDNodeXForm<imm, [{
  // Transformation function: get the size of a mask
  assert(isMask_32(N->getZExtValue()));
  // look for the first non-zero bit
  return getI32Imm(llvm::bit_width((uint32_t)N->getZExtValue()),
                   SDLoc(N));
}]>;

def neg_xform : SDNodeXForm<imm, [{
  // Transformation function: -imm
  uint32_t value = N->getZExtValue();
  return getI32Imm(-value, SDLoc(N));
}]>;

def bpwsub_xform : SDNodeXForm<imm, [{
  // Transformation function: 32-imm
  uint32_t value = N->getZExtValue();
  return getI32Imm(32 - value, SDLoc(N));
}]>;

def div4neg_xform : SDNodeXForm<imm, [{
  // Transformation function: -imm/4
  uint32_t value = N->getZExtValue();
  assert(-value % 4 == 0);
  return getI32Imm(-value/4, SDLoc(N));
}]>;

def immUs4Neg : PatLeaf<(imm), [{
  uint32_t value = (uint32_t)N->getZExtValue();
  return (-value)%4 == 0 && (-value)/4 <= 11;
}]>;

def immUs4 : PatLeaf<(imm), [{
  uint32_t value = (uint32_t)N->getZExtValue();
  return value%4 == 0 && value/4 <= 11;
}]>;

def immUsNeg : PatLeaf<(imm), [{
  return -((uint32_t)N->getZExtValue()) <= 11;
}]>;

def immUs : PatLeaf<(imm), [{
  return (uint32_t)N->getZExtValue() <= 11;
}]>;

def immU6 : PatLeaf<(imm), [{
  return (uint32_t)N->getZExtValue() < (1 << 6);
}]>;

def immU16 : PatLeaf<(imm), [{
  return (uint32_t)N->getZExtValue() < (1 << 16);
}]>;

def immMskBitp : PatLeaf<(imm), [{ return immMskBitp(N); }]>;

def immBitp : PatLeaf<(imm), [{
  uint32_t value = (uint32_t)N->getZExtValue();
  return (value >= 1 && value <= 8)
          || value == 16
          || value == 24
          || value == 32;
}]>;

def immBpwSubBitp : PatLeaf<(imm), [{
  uint32_t value = (uint32_t)N->getZExtValue();
  return (value >= 24 && value <= 31)
          || value == 16
          || value == 8
          || value == 0;
}]>;

def lda16f : PatFrag<(ops node:$addr, node:$offset),
                     (add node:$addr, (shl node:$offset, 1))>;
def lda16b : PatFrag<(ops node:$addr, node:$offset),
                     (sub node:$addr, (shl node:$offset, 1))>;
def ldawf : PatFrag<(ops node:$addr, node:$offset),
                     (add node:$addr, (shl node:$offset, 2))>;
def ldawb : PatFrag<(ops node:$addr, node:$offset),
                     (sub node:$addr, (shl node:$offset, 2))>;

// Instruction operand types
def pcrel_imm  : Operand<i32>;
def pcrel_imm_neg  : Operand<i32> {
  let DecoderMethod = "DecodeNegImmOperand";
}
def brtarget : Operand<OtherVT>;
def brtarget_neg : Operand<OtherVT> {
  let DecoderMethod = "DecodeNegImmOperand";
}

// Addressing modes
def ADDRspii : ComplexPattern<i32, 2, "SelectADDRspii", [add, frameindex], []>;

// Address operands
def MEMii : Operand<i32> {
  let MIOperandInfo = (ops i32imm, i32imm);
}

// Jump tables.
def InlineJT : Operand<i32> {
  let PrintMethod = "printInlineJT";
}

def InlineJT32 : Operand<i32> {
  let PrintMethod = "printInlineJT32";
}

//===----------------------------------------------------------------------===//
// Instruction Class Templates
//===----------------------------------------------------------------------===//

// Three operand short

multiclass F3R_2RUS<bits<5> opc1, bits<5> opc2, string OpcStr, SDNode OpNode> {
  def _3r: _F3R<opc1, (outs GRRegs:$dst), (ins GRRegs:$b, GRRegs:$c),
                !strconcat(OpcStr, " $dst, $b, $c"),
                [(set GRRegs:$dst, (OpNode GRRegs:$b, GRRegs:$c))]>;
  def _2rus : _F2RUS<opc2, (outs GRRegs:$dst), (ins GRRegs:$b, i32imm:$c),
                     !strconcat(OpcStr, " $dst, $b, $c"),
                     [(set GRRegs:$dst, (OpNode GRRegs:$b, immUs:$c))]>;
}

multiclass F3R_2RUS_np<bits<5> opc1, bits<5> opc2, string OpcStr> {
  def _3r: _F3R<opc1, (outs GRRegs:$dst), (ins GRRegs:$b, GRRegs:$c),
                !strconcat(OpcStr, " $dst, $b, $c"), []>;
  def _2rus : _F2RUS<opc2, (outs GRRegs:$dst), (ins GRRegs:$b, i32imm:$c),
                     !strconcat(OpcStr, " $dst, $b, $c"), []>;
}

multiclass F3R_2RBITP<bits<5> opc1, bits<5> opc2, string OpcStr,
                      SDNode OpNode> {
  def _3r: _F3R<opc1, (outs GRRegs:$dst), (ins GRRegs:$b, GRRegs:$c),
                !strconcat(OpcStr, " $dst, $b, $c"),
                [(set GRRegs:$dst, (OpNode GRRegs:$b, GRRegs:$c))]>;
  def _2rus : _F2RUSBitp<opc2, (outs GRRegs:$dst), (ins GRRegs:$b, i32imm:$c),
                         !strconcat(OpcStr, " $dst, $b, $c"),
                         [(set GRRegs:$dst, (OpNode GRRegs:$b, immBitp:$c))]>;
}

class F3R<bits<5> opc, string OpcStr, SDNode OpNode> :
  _F3R<opc, (outs GRRegs:$dst), (ins GRRegs:$b, GRRegs:$c),
       !strconcat(OpcStr, " $dst, $b, $c"),
       [(set GRRegs:$dst, (OpNode GRRegs:$b, GRRegs:$c))]>;

class F3R_np<bits<5> opc, string OpcStr> :
  _F3R<opc, (outs GRRegs:$dst), (ins GRRegs:$b, GRRegs:$c),
       !strconcat(OpcStr, " $dst, $b, $c"), []>;
// Three operand long

/// FL3R_L2RUS multiclass - Define a normal FL3R/FL2RUS pattern in one shot.
multiclass FL3R_L2RUS<bits<9> opc1, bits<9> opc2, string OpcStr,
                      SDNode OpNode> {
  def _l3r: _FL3R<opc1, (outs GRRegs:$dst), (ins GRRegs:$b, GRRegs:$c),
                  !strconcat(OpcStr, " $dst, $b, $c"),
                  [(set GRRegs:$dst, (OpNode GRRegs:$b, GRRegs:$c))]>;
  def _l2rus : _FL2RUS<opc2, (outs GRRegs:$dst), (ins GRRegs:$b, i32imm:$c),
                       !strconcat(OpcStr, " $dst, $b, $c"),
                       [(set GRRegs:$dst, (OpNode GRRegs:$b, immUs:$c))]>;
}

/// FL3R_L2RUS multiclass - Define a normal FL3R/FL2RUS pattern in one shot.
multiclass FL3R_L2RBITP<bits<9> opc1, bits<9> opc2, string OpcStr,
                        SDNode OpNode> {
  def _l3r: _FL3R<opc1, (outs GRRegs:$dst), (ins GRRegs:$b, GRRegs:$c),
                  !strconcat(OpcStr, " $dst, $b, $c"),
                  [(set GRRegs:$dst, (OpNode GRRegs:$b, GRRegs:$c))]>;
  def _l2rus : _FL2RUSBitp<opc2, (outs GRRegs:$dst), (ins GRRegs:$b, i32imm:$c),
                           !strconcat(OpcStr, " $dst, $b, $c"),
                           [(set GRRegs:$dst, (OpNode GRRegs:$b, immBitp:$c))]>;
}

class FL3R<bits<9> opc, string OpcStr, SDNode OpNode> :
  _FL3R<opc, (outs GRRegs:$dst), (ins GRRegs:$b, GRRegs:$c),
        !strconcat(OpcStr, " $dst, $b, $c"),
        [(set GRRegs:$dst, (OpNode GRRegs:$b, GRRegs:$c))]>;

// Register - U6
// Operand register - U6
multiclass FRU6_LRU6_branch<bits<6> opc, string OpcStr> {
  def _ru6: _FRU6<opc, (outs), (ins GRRegs:$a, brtarget:$b),
                  !strconcat(OpcStr, " $a, $b"), []>;
  def _lru6: _FLRU6<opc, (outs), (ins GRRegs:$a, brtarget:$b),
                    !strconcat(OpcStr, " $a, $b"), []>;
}

multiclass FRU6_LRU6_backwards_branch<bits<6> opc, string OpcStr> {
  def _ru6: _FRU6<opc, (outs), (ins GRRegs:$a, brtarget_neg:$b),
                  !strconcat(OpcStr, " $a, $b"), []>;
  def _lru6: _FLRU6<opc, (outs), (ins GRRegs:$a, brtarget_neg:$b),
                    !strconcat(OpcStr, " $a, $b"), []>;
}


// U6
multiclass FU6_LU6<bits<10> opc, string OpcStr, SDNode OpNode> {
  def _u6: _FU6<opc, (outs), (ins i32imm:$a), !strconcat(OpcStr, " $a"),
                [(OpNode immU6:$a)]>;
  def _lu6: _FLU6<opc, (outs), (ins i32imm:$a), !strconcat(OpcStr, " $a"),
                  [(OpNode immU16:$a)]>;
}

multiclass FU6_LU6_int<bits<10> opc, string OpcStr, Intrinsic Int> {
  def _u6: _FU6<opc, (outs), (ins i32imm:$a), !strconcat(OpcStr, " $a"),
                [(Int immU6:$a)]>;
  def _lu6: _FLU6<opc, (outs), (ins i32imm:$a), !strconcat(OpcStr, " $a"),
                  [(Int immU16:$a)]>;
}

multiclass FU6_LU6_np<bits<10> opc, string OpcStr> {
  def _u6: _FU6<opc, (outs), (ins i32imm:$a), !strconcat(OpcStr, " $a"), []>;
  def _lu6: _FLU6<opc, (outs), (ins i32imm:$a), !strconcat(OpcStr, " $a"), []>;
}

// Two operand short

class F2R_np<bits<6> opc, string OpcStr> :
  _F2R<opc, (outs GRRegs:$dst), (ins GRRegs:$b),
       !strconcat(OpcStr, " $dst, $b"), []>;

// Two operand long

//===----------------------------------------------------------------------===//
// Pseudo Instructions
//===----------------------------------------------------------------------===//

let Defs = [SP], Uses = [SP] in {
def ADJCALLSTACKDOWN : PseudoInstXCore<(outs), (ins i32imm:$amt, i32imm:$amt2),
                               "# ADJCALLSTACKDOWN $amt, $amt2",
                               [(callseq_start timm:$amt, timm:$amt2)]>;
def ADJCALLSTACKUP : PseudoInstXCore<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                            "# ADJCALLSTACKUP $amt1",
                            [(callseq_end timm:$amt1, timm:$amt2)]>;
}

let isReMaterializable = 1 in
def FRAME_TO_ARGS_OFFSET : PseudoInstXCore<(outs GRRegs:$dst), (ins),
                               "# FRAME_TO_ARGS_OFFSET $dst",
                               [(set GRRegs:$dst, (frametoargsoffset))]>;

let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def EH_RETURN : PseudoInstXCore<(outs), (ins GRRegs:$s, GRRegs:$handler),
                               "# EH_RETURN $s, $handler",
                               [(XCoreEhRet GRRegs:$s, GRRegs:$handler)]>;

def LDWFI : PseudoInstXCore<(outs GRRegs:$dst), (ins MEMii:$addr),
                             "# LDWFI $dst, $addr",
                             [(set GRRegs:$dst, (load ADDRspii:$addr))]>;

def LDAWFI : PseudoInstXCore<(outs GRRegs:$dst), (ins MEMii:$addr),
                             "# LDAWFI $dst, $addr",
                             [(set GRRegs:$dst, ADDRspii:$addr)]>;

def STWFI : PseudoInstXCore<(outs), (ins GRRegs:$src, MEMii:$addr),
                            "# STWFI $src, $addr",
                            [(store GRRegs:$src, ADDRspii:$addr)]>;

// SELECT_CC_* - Used to implement the SELECT_CC DAG operation.  Expanded after
// instruction selection into a branch sequence.
let usesCustomInserter = 1 in {
  def SELECT_CC : PseudoInstXCore<(outs GRRegs:$dst),
                              (ins GRRegs:$cond, GRRegs:$T, GRRegs:$F),
                              "# SELECT_CC PSEUDO!",
                              [(set GRRegs:$dst,
                                 (select GRRegs:$cond, GRRegs:$T, GRRegs:$F))]>;
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

// Three operand short
defm ADD : F3R_2RUS<0b00010, 0b10010, "add", add>;
defm SUB : F3R_2RUS<0b00011, 0b10011, "sub", sub>;
let hasSideEffects = 0 in {
defm EQ : F3R_2RUS_np<0b00110, 0b10110, "eq">;
def LSS_3r : F3R_np<0b11000, "lss">;
def LSU_3r : F3R_np<0b11001, "lsu">;
}
def AND_3r : F3R<0b00111, "and", and>;
def OR_3r : F3R<0b01000, "or", or>;

let mayLoad=1 in {
def LDW_3r : _F3R<0b01001, (outs GRRegs:$dst),
                  (ins GRRegs:$addr, GRRegs:$offset),
                  "ldw $dst, $addr[$offset]", []>;

def LDW_2rus : _F2RUS<0b00001, (outs GRRegs:$dst),
                      (ins GRRegs:$addr, i32imm:$offset),
                      "ldw $dst, $addr[$offset]", []>;

def LD16S_3r :  _F3R<0b10000, (outs GRRegs:$dst),
                     (ins GRRegs:$addr, GRRegs:$offset),
                     "ld16s $dst, $addr[$offset]", []>;

def LD8U_3r :  _F3R<0b10001, (outs GRRegs:$dst),
                    (ins GRRegs:$addr, GRRegs:$offset),
                    "ld8u $dst, $addr[$offset]", []>;
}

let mayStore=1 in {
def STW_l3r : _FL3R<0b000001100, (outs),
                    (ins GRRegs:$val, GRRegs:$addr, GRRegs:$offset),
                    "stw $val, $addr[$offset]", []>;

def STW_2rus : _F2RUS<0b00000, (outs),
                      (ins GRRegs:$val, GRRegs:$addr, i32imm:$offset),
                      "stw $val, $addr[$offset]", []>;
}

defm SHL : F3R_2RBITP<0b00100, 0b10100, "shl", shl>;
defm SHR : F3R_2RBITP<0b00101, 0b10101, "shr", srl>;

// The first operand is treated as an immediate since it refers to a register
// number in another thread.
def TSETR_3r : _F3RImm<0b10111, (outs), (ins i32imm:$a, GRRegs:$b, GRRegs:$c),
                       "set t[$c]:r$a, $b", []>;

// Three operand long
def LDAWF_l3r : _FL3R<0b000111100, (outs GRRegs:$dst),
                      (ins GRRegs:$addr, GRRegs:$offset),
                      "ldaw $dst, $addr[$offset]",
                      [(set GRRegs:$dst,
                         (ldawf GRRegs:$addr, GRRegs:$offset))]>;

let hasSideEffects = 0 in
def LDAWF_l2rus : _FL2RUS<0b100111100, (outs GRRegs:$dst),
                          (ins GRRegs:$addr, i32imm:$offset),
                          "ldaw $dst, $addr[$offset]", []>;

def LDAWB_l3r : _FL3R<0b001001100, (outs GRRegs:$dst),
                      (ins GRRegs:$addr, GRRegs:$offset),
                      "ldaw $dst, $addr[-$offset]",
                      [(set GRRegs:$dst,
                         (ldawb GRRegs:$addr, GRRegs:$offset))]>;

let hasSideEffects = 0 in
def LDAWB_l2rus : _FL2RUS<0b101001100, (outs GRRegs:$dst),
                         (ins GRRegs:$addr, i32imm:$offset),
                         "ldaw $dst, $addr[-$offset]", []>;

def LDA16F_l3r : _FL3R<0b001011100, (outs GRRegs:$dst),
                       (ins GRRegs:$addr, GRRegs:$offset),
                       "lda16 $dst, $addr[$offset]",
                       [(set GRRegs:$dst,
                          (lda16f GRRegs:$addr, GRRegs:$offset))]>;

def LDA16B_l3r : _FL3R<0b001101100, (outs GRRegs:$dst),
                       (ins GRRegs:$addr, GRRegs:$offset),
                       "lda16 $dst, $addr[-$offset]",
                       [(set GRRegs:$dst,
                          (lda16b GRRegs:$addr, GRRegs:$offset))]>;

def MUL_l3r : FL3R<0b001111100, "mul", mul>;
// Instructions which may trap are marked as side effecting.
let hasSideEffects = 1 in {
def DIVS_l3r : FL3R<0b010001100, "divs", sdiv>;
def DIVU_l3r : FL3R<0b010011100, "divu", udiv>;
def REMS_l3r : FL3R<0b110001100, "rems", srem>;
def REMU_l3r : FL3R<0b110011100, "remu", urem>;
}
def XOR_l3r : FL3R<0b000011100, "xor", xor>;
defm ASHR : FL3R_L2RBITP<0b000101100, 0b100101100, "ashr", sra>;

let Constraints = "$src1 = $dst" in
def CRC_l3r : _FL3RSrcDst<0b101011100, (outs GRRegs:$dst),
                          (ins GRRegs:$src1, GRRegs:$src2, GRRegs:$src3),
                          "crc32 $dst, $src2, $src3",
                          [(set GRRegs:$dst,
                             (int_xcore_crc32 GRRegs:$src1, GRRegs:$src2,
                                              GRRegs:$src3))]>;

let mayStore=1 in {
def ST16_l3r : _FL3R<0b100001100, (outs),
                     (ins GRRegs:$val, GRRegs:$addr, GRRegs:$offset),
                     "st16 $val, $addr[$offset]", []>;

def ST8_l3r : _FL3R<0b100011100, (outs),
                    (ins GRRegs:$val, GRRegs:$addr, GRRegs:$offset),
                    "st8 $val, $addr[$offset]", []>;
}

def INPW_l2rus : _FL2RUSBitp<0b100101110, (outs GRRegs:$a),
                             (ins GRRegs:$b, i32imm:$c), "inpw $a, res[$b], $c",
                             []>;

def OUTPW_l2rus : _FL2RUSBitp<0b100101101, (outs),
                              (ins GRRegs:$a, GRRegs:$b, i32imm:$c),
                              "outpw res[$b], $a, $c", []>;

// Four operand long
let Constraints = "$e = $a,$f = $b" in {
def MACCU_l4r : _FL4RSrcDstSrcDst<
  0b000001, (outs GRRegs:$a, GRRegs:$b),
  (ins GRRegs:$e, GRRegs:$f, GRRegs:$c, GRRegs:$d), "maccu $a, $b, $c, $d", []>;

def MACCS_l4r : _FL4RSrcDstSrcDst<
  0b000010, (outs GRRegs:$a, GRRegs:$b),
  (ins GRRegs:$e, GRRegs:$f, GRRegs:$c, GRRegs:$d), "maccs $a, $b, $c, $d", []>;
}

let Constraints = "$e = $b" in
def CRC8_l4r : _FL4RSrcDst<0b000000, (outs GRRegs:$a, GRRegs:$b),
                           (ins GRRegs:$e, GRRegs:$c, GRRegs:$d),
                           "crc8 $b, $a, $c, $d", []>;

// Five operand long

def LADD_l5r : _FL5R<0b000001, (outs GRRegs:$dst1, GRRegs:$dst2),
                     (ins GRRegs:$src1, GRRegs:$src2, GRRegs:$src3),
                     "ladd $dst2, $dst1, $src1, $src2, $src3",
                     []>;

def LSUB_l5r : _FL5R<0b000010, (outs GRRegs:$dst1, GRRegs:$dst2),
                     (ins GRRegs:$src1, GRRegs:$src2, GRRegs:$src3),
                     "lsub $dst2, $dst1, $src1, $src2, $src3", []>;

def LDIVU_l5r : _FL5R<0b000000, (outs GRRegs:$dst1, GRRegs:$dst2),
                      (ins GRRegs:$src1, GRRegs:$src2, GRRegs:$src3),
                      "ldivu $dst1, $dst2, $src3, $src1, $src2", []>;

// Six operand long

def LMUL_l6r : _FL6R<
  0b00000, (outs GRRegs:$dst1, GRRegs:$dst2),
  (ins GRRegs:$src1, GRRegs:$src2, GRRegs:$src3, GRRegs:$src4),
  "lmul $dst1, $dst2, $src1, $src2, $src3, $src4", []>;

// Register - U6

//let Uses = [DP] in ...
let hasSideEffects = 0, isReMaterializable = 1 in
def LDAWDP_ru6: _FRU6<0b011000, (outs RRegs:$a), (ins i32imm:$b),
                      "ldaw $a, dp[$b]", []>;

let isReMaterializable = 1 in
def LDAWDP_lru6: _FLRU6<0b011000, (outs RRegs:$a), (ins i32imm:$b),
                        "ldaw $a, dp[$b]",
                        [(set RRegs:$a, (dprelwrapper tglobaladdr:$b))]>;

let mayLoad=1 in
def LDWDP_ru6: _FRU6<0b010110, (outs RRegs:$a), (ins i32imm:$b),
                     "ldw $a, dp[$b]", []>;

def LDWDP_lru6: _FLRU6<0b010110, (outs RRegs:$a), (ins i32imm:$b),
                       "ldw $a, dp[$b]",
                       [(set RRegs:$a, (load (dprelwrapper tglobaladdr:$b)))]>;

let mayStore=1 in
def STWDP_ru6 : _FRU6<0b010100, (outs), (ins RRegs:$a, i32imm:$b),
                      "stw $a, dp[$b]", []>;

def STWDP_lru6 : _FLRU6<0b010100, (outs), (ins RRegs:$a, i32imm:$b),
                        "stw $a, dp[$b]",
                        [(store RRegs:$a, (dprelwrapper tglobaladdr:$b))]>;

//let Uses = [CP] in ..
let mayLoad = 1, isReMaterializable = 1, hasSideEffects = 0 in {
def LDWCP_ru6 : _FRU6<0b011011, (outs RRegs:$a), (ins i32imm:$b),
                      "ldw $a, cp[$b]", []>;
def LDWCP_lru6: _FLRU6<0b011011, (outs RRegs:$a), (ins i32imm:$b),
                       "ldw $a, cp[$b]",
                       [(set RRegs:$a, (load (cprelwrapper tglobaladdr:$b)))]>;
}

let Uses = [SP] in {
let mayStore=1 in {
def STWSP_ru6 : _FRU6<0b010101, (outs), (ins RRegs:$a, i32imm:$b),
                      "stw $a, sp[$b]",
                      [(XCoreStwsp RRegs:$a, immU6:$b)]>;

def STWSP_lru6 : _FLRU6<0b010101, (outs), (ins RRegs:$a, i32imm:$b),
                        "stw $a, sp[$b]",
                        [(XCoreStwsp RRegs:$a, immU16:$b)]>;
}

let mayLoad=1 in {
def LDWSP_ru6 : _FRU6<0b010111, (outs RRegs:$a), (ins i32imm:$b),
                      "ldw $a, sp[$b]",
                      [(set RRegs:$a, (XCoreLdwsp immU6:$b))]>;

def LDWSP_lru6 : _FLRU6<0b010111, (outs RRegs:$a), (ins i32imm:$b),
                        "ldw $a, sp[$b]",
                        [(set RRegs:$a, (XCoreLdwsp immU16:$b))]>;
}

let hasSideEffects = 0 in {
def LDAWSP_ru6 : _FRU6<0b011001, (outs RRegs:$a), (ins i32imm:$b),
                       "ldaw $a, sp[$b]", []>;

def LDAWSP_lru6 : _FLRU6<0b011001, (outs RRegs:$a), (ins i32imm:$b),
                         "ldaw $a, sp[$b]", []>;
}
}

let isReMaterializable = 1 in {
def LDC_ru6 : _FRU6<0b011010, (outs RRegs:$a), (ins i32imm:$b),
                    "ldc $a, $b", [(set RRegs:$a, immU6:$b)]>;

def LDC_lru6 : _FLRU6<0b011010, (outs RRegs:$a), (ins i32imm:$b),
                      "ldc $a, $b", [(set RRegs:$a, immU16:$b)]>;
}

def SETC_ru6 : _FRU6<0b111010, (outs), (ins GRRegs:$a, i32imm:$b),
                     "setc res[$a], $b",
                     [(int_xcore_setc GRRegs:$a, immU6:$b)]>;

def SETC_lru6 : _FLRU6<0b111010, (outs), (ins GRRegs:$a, i32imm:$b),
                       "setc res[$a], $b",
                       [(int_xcore_setc GRRegs:$a, immU16:$b)]>;

// Operand register - U6
let isBranch = 1, isTerminator = 1 in {
defm BRFT: FRU6_LRU6_branch<0b011100, "bt">;
defm BRBT: FRU6_LRU6_backwards_branch<0b011101, "bt">;
defm BRFF: FRU6_LRU6_branch<0b011110, "bf">;
defm BRBF: FRU6_LRU6_backwards_branch<0b011111, "bf">;
}

// U6
let Defs = [SP], Uses = [SP] in {
let hasSideEffects = 0 in
defm EXTSP : FU6_LU6_np<0b0111011110, "extsp">;

let mayStore = 1 in
defm ENTSP : FU6_LU6_np<0b0111011101, "entsp">;

let isReturn = 1, isTerminator = 1, mayLoad = 1, isBarrier = 1 in {
defm RETSP : FU6_LU6<0b0111011111, "retsp", XCoreRetsp>;
}
}

let hasSideEffects = 0 in
defm EXTDP : FU6_LU6_np<0b0111001110, "extdp">;

let Uses = [R11], isCall=1 in
defm BLAT : FU6_LU6_np<0b0111001101, "blat">;

let isBranch = 1, isTerminator = 1, isBarrier = 1 in {
def BRBU_u6 : _FU6<0b0111011100, (outs), (ins brtarget_neg:$a), "bu $a", []>;

def BRBU_lu6 : _FLU6<0b0111011100, (outs), (ins brtarget_neg:$a), "bu $a", []>;

def BRFU_u6 : _FU6<0b0111001100, (outs), (ins brtarget:$a), "bu $a", []>;

def BRFU_lu6 : _FLU6<0b0111001100, (outs), (ins brtarget:$a), "bu $a", []>;
}

//let Uses = [CP] in ...
let Defs = [R11], hasSideEffects = 0, isReMaterializable = 1 in
def LDAWCP_u6: _FU6<0b0111111101, (outs), (ins i32imm:$a), "ldaw r11, cp[$a]",
                    []>;

let Defs = [R11], isReMaterializable = 1 in
def LDAWCP_lu6: _FLU6<0b0111111101, (outs), (ins i32imm:$a), "ldaw r11, cp[$a]",
                      [(set R11, (cprelwrapper tglobaladdr:$a))]>;

let Defs = [R11] in
defm GETSR : FU6_LU6_np<0b0111111100, "getsr r11,">;

defm SETSR : FU6_LU6_int<0b0111101101, "setsr", int_xcore_setsr>;

defm CLRSR : FU6_LU6_int<0b0111101100, "clrsr", int_xcore_clrsr>;

// setsr may cause a branch if it is used to enable events. clrsr may
// branch if it is executed while events are enabled.
let isBranch=1, isIndirectBranch=1, isTerminator=1, isBarrier = 1,
    isCodeGenOnly = 1 in {
defm SETSR_branch : FU6_LU6_np<0b0111101101, "setsr">;
defm CLRSR_branch : FU6_LU6_np<0b0111101100, "clrsr">;
}

defm KCALL : FU6_LU6_np<0b0111001111, "kcall">;

let Uses = [SP], Defs = [SP], mayStore = 1 in
defm KENTSP : FU6_LU6_np<0b0111101110, "kentsp">;

let Uses = [SP], Defs = [SP], mayLoad = 1 in
defm KRESTSP : FU6_LU6_np<0b0111101111, "krestsp">;

// U10

let Defs = [R11], isReMaterializable = 1 in {
let hasSideEffects = 0 in
def LDAPF_u10 : _FU10<0b110110, (outs), (ins pcrel_imm:$a), "ldap r11, $a", []>;

def LDAPF_lu10 : _FLU10<0b110110, (outs), (ins pcrel_imm:$a), "ldap r11, $a",
                        [(set R11, (pcrelwrapper tglobaladdr:$a))]>;

let hasSideEffects = 0 in
def LDAPB_u10 : _FU10<0b110111, (outs), (ins pcrel_imm_neg:$a), "ldap r11, $a",
                      []>;

let hasSideEffects = 0 in
def LDAPB_lu10 : _FLU10<0b110111, (outs), (ins pcrel_imm_neg:$a),
                        "ldap r11, $a",
                        [(set R11, (pcrelwrapper tglobaladdr:$a))]>;

let isCodeGenOnly = 1 in
def LDAPF_lu10_ba : _FLU10<0b110110, (outs), (ins pcrel_imm:$a), "ldap r11, $a",
                           [(set R11, (pcrelwrapper tblockaddress:$a))]>;
}

let isCall=1,
// All calls clobber the link register and the non-callee-saved registers:
Defs = [R0, R1, R2, R3, R11, LR], Uses = [SP] in {
def BLACP_u10 : _FU10<0b111000, (outs), (ins i32imm:$a), "bla cp[$a]", []>;

def BLACP_lu10 : _FLU10<0b111000, (outs), (ins i32imm:$a), "bla cp[$a]", []>;

def BLRF_u10 : _FU10<0b110100, (outs), (ins pcrel_imm:$a), "bl $a",
                     []>;

def BLRF_lu10 : _FLU10<0b110100, (outs), (ins pcrel_imm:$a), "bl $a",
                       [(XCoreBranchLink tglobaladdr:$a)]>;

def BLRB_u10 : _FU10<0b110101, (outs), (ins pcrel_imm_neg:$a), "bl $a", []>;

def BLRB_lu10 : _FLU10<0b110101, (outs), (ins pcrel_imm_neg:$a), "bl $a", []>;
}

let Defs = [R11], mayLoad = 1, isReMaterializable = 1,
    hasSideEffects = 0 in {
def LDWCP_u10 : _FU10<0b111001, (outs), (ins i32imm:$a), "ldw r11, cp[$a]", []>;

def LDWCP_lu10 : _FLU10<0b111001, (outs), (ins i32imm:$a), "ldw r11, cp[$a]",
                        []>;
}

// Two operand short
def NOT : _F2R<0b100010, (outs GRRegs:$dst), (ins GRRegs:$b),
                "not $dst, $b", [(set GRRegs:$dst, (not GRRegs:$b))]>;

def NEG : _F2R<0b100100, (outs GRRegs:$dst), (ins GRRegs:$b),
                "neg $dst, $b", [(set GRRegs:$dst, (ineg GRRegs:$b))]>;

let Constraints = "$src1 = $dst" in {
def SEXT_rus :
  _FRUSSrcDstBitp<0b001101, (outs GRRegs:$dst), (ins GRRegs:$src1, i32imm:$src2),
                  "sext $dst, $src2",
                  [(set GRRegs:$dst, (int_xcore_sext GRRegs:$src1,
                                                     immBitp:$src2))]>;

def SEXT_2r :
  _F2RSrcDst<0b001100, (outs GRRegs:$dst), (ins GRRegs:$src1, GRRegs:$src2),
             "sext $dst, $src2",
             [(set GRRegs:$dst, (int_xcore_sext GRRegs:$src1, GRRegs:$src2))]>;

def ZEXT_rus :
  _FRUSSrcDstBitp<0b010001, (outs GRRegs:$dst), (ins GRRegs:$src1, i32imm:$src2),
                  "zext $dst, $src2",
                  [(set GRRegs:$dst, (int_xcore_zext GRRegs:$src1,
                                                     immBitp:$src2))]>;

def ZEXT_2r :
  _F2RSrcDst<0b010000, (outs GRRegs:$dst), (ins GRRegs:$src1, GRRegs:$src2),
             "zext $dst, $src2",
             [(set GRRegs:$dst, (int_xcore_zext GRRegs:$src1, GRRegs:$src2))]>;

def ANDNOT_2r :
  _F2RSrcDst<0b001010, (outs GRRegs:$dst), (ins GRRegs:$src1, GRRegs:$src2),
             "andnot $dst, $src2",
             [(set GRRegs:$dst, (and GRRegs:$src1, (not GRRegs:$src2)))]>;
}

let isReMaterializable = 1, hasSideEffects = 0 in
def MKMSK_rus : _FRUSBitp<0b101001, (outs GRRegs:$dst), (ins i32imm:$size),
                          "mkmsk $dst, $size", []>;

def MKMSK_2r : _F2R<0b101000, (outs GRRegs:$dst), (ins GRRegs:$size),
                    "mkmsk $dst, $size",
                    [(set GRRegs:$dst, (add (shl 1, GRRegs:$size), -1))]>;

def GETR_rus : _FRUS<0b100000, (outs GRRegs:$dst), (ins i32imm:$type),
                     "getr $dst, $type",
                     [(set GRRegs:$dst, (int_xcore_getr immUs:$type))]>;

def GETTS_2r : _F2R<0b001110, (outs GRRegs:$dst), (ins GRRegs:$r),
                    "getts $dst, res[$r]",
                    [(set GRRegs:$dst, (int_xcore_getts GRRegs:$r))]>;

def SETPT_2r : _FR2R<0b001111, (outs), (ins GRRegs:$r, GRRegs:$val),
                     "setpt res[$r], $val",
                     [(int_xcore_setpt GRRegs:$r, GRRegs:$val)]>;

def OUTCT_2r : _F2R<0b010010, (outs), (ins GRRegs:$r, GRRegs:$val),
                    "outct res[$r], $val",
                    [(int_xcore_outct GRRegs:$r, GRRegs:$val)]>;

def OUTCT_rus : _FRUS<0b010011, (outs), (ins GRRegs:$r, i32imm:$val),
                       "outct res[$r], $val",
                       [(int_xcore_outct GRRegs:$r, immUs:$val)]>;

def OUTT_2r : _FR2R<0b000011, (outs), (ins GRRegs:$r, GRRegs:$val),
                    "outt res[$r], $val",
                    [(int_xcore_outt GRRegs:$r, GRRegs:$val)]>;

def OUT_2r : _FR2R<0b101010, (outs), (ins GRRegs:$r, GRRegs:$val),
                   "out res[$r], $val",
                   [(int_xcore_out GRRegs:$r, GRRegs:$val)]>;

let Constraints = "$src = $dst" in
def OUTSHR_2r :
  _F2RSrcDst<0b101011, (outs GRRegs:$dst), (ins GRRegs:$src, GRRegs:$r),
             "outshr res[$r], $src",
             [(set GRRegs:$dst, (int_xcore_outshr GRRegs:$r, GRRegs:$src))]>;

def INCT_2r : _F2R<0b100001, (outs GRRegs:$dst), (ins GRRegs:$r),
                   "inct $dst, res[$r]",
                   [(set GRRegs:$dst, (int_xcore_inct GRRegs:$r))]>;

def INT_2r : _F2R<0b100011, (outs GRRegs:$dst), (ins GRRegs:$r),
                  "int $dst, res[$r]",
                  [(set GRRegs:$dst, (int_xcore_int GRRegs:$r))]>;

def IN_2r : _F2R<0b101100, (outs GRRegs:$dst), (ins GRRegs:$r),
                 "in $dst, res[$r]",
                 [(set GRRegs:$dst, (int_xcore_in GRRegs:$r))]>;

let Constraints = "$src = $dst" in
def INSHR_2r :
  _F2RSrcDst<0b101101, (outs GRRegs:$dst), (ins GRRegs:$src, GRRegs:$r),
             "inshr $dst, res[$r]",
             [(set GRRegs:$dst, (int_xcore_inshr GRRegs:$r, GRRegs:$src))]>;

def CHKCT_2r : _F2R<0b110010, (outs), (ins GRRegs:$r, GRRegs:$val),
                    "chkct res[$r], $val",
                    [(int_xcore_chkct GRRegs:$r, GRRegs:$val)]>;

def CHKCT_rus : _FRUSBitp<0b110011, (outs), (ins GRRegs:$r, i32imm:$val),
                          "chkct res[$r], $val",
                          [(int_xcore_chkct GRRegs:$r, immUs:$val)]>;

def TESTCT_2r : _F2R<0b101111, (outs GRRegs:$dst), (ins GRRegs:$src),
                     "testct $dst, res[$src]",
                     [(set GRRegs:$dst, (int_xcore_testct GRRegs:$src))]>;

def TESTWCT_2r : _F2R<0b110001, (outs GRRegs:$dst), (ins GRRegs:$src),
                      "testwct $dst, res[$src]",
                      [(set GRRegs:$dst, (int_xcore_testwct GRRegs:$src))]>;

def SETD_2r : _FR2R<0b000101, (outs), (ins GRRegs:$r, GRRegs:$val),
                    "setd res[$r], $val",
                    [(int_xcore_setd GRRegs:$r, GRRegs:$val)]>;

def SETPSC_2r : _FR2R<0b110000, (outs), (ins GRRegs:$src1, GRRegs:$src2),
                      "setpsc res[$src1], $src2",
                      [(int_xcore_setpsc GRRegs:$src1, GRRegs:$src2)]>;

def GETST_2r : _F2R<0b000001, (outs GRRegs:$dst), (ins GRRegs:$r),
                    "getst $dst, res[$r]",
                    [(set GRRegs:$dst, (int_xcore_getst GRRegs:$r))]>;

def INITSP_2r : _F2R<0b000100, (outs), (ins GRRegs:$src, GRRegs:$t),
                     "init t[$t]:sp, $src",
                     [(int_xcore_initsp GRRegs:$t, GRRegs:$src)]>;

def INITPC_2r : _F2R<0b000000, (outs), (ins GRRegs:$src, GRRegs:$t),
                     "init t[$t]:pc, $src",
                     [(int_xcore_initpc GRRegs:$t, GRRegs:$src)]>;

def INITCP_2r : _F2R<0b000110, (outs), (ins GRRegs:$src, GRRegs:$t),
                     "init t[$t]:cp, $src",
                     [(int_xcore_initcp GRRegs:$t, GRRegs:$src)]>;

def INITDP_2r : _F2R<0b000010, (outs), (ins GRRegs:$src, GRRegs:$t),
                     "init t[$t]:dp, $src",
                     [(int_xcore_initdp GRRegs:$t, GRRegs:$src)]>;

def PEEK_2r : _F2R<0b101110, (outs GRRegs:$dst), (ins GRRegs:$src),
                    "peek $dst, res[$src]",
                    [(set GRRegs:$dst, (int_xcore_peek GRRegs:$src))]>;

def ENDIN_2r : _F2R<0b100101, (outs GRRegs:$dst), (ins GRRegs:$src),
                     "endin $dst, res[$src]",
                     [(set GRRegs:$dst, (int_xcore_endin GRRegs:$src))]>;

def EEF_2r : _F2R<0b001011, (outs), (ins GRRegs:$a, GRRegs:$b),
                  "eef $a, res[$b]", []>;

def EET_2r : _F2R<0b001001, (outs), (ins GRRegs:$a, GRRegs:$b),
                  "eet $a, res[$b]", []>;

def TSETMR_2r : _F2RImm<0b000111, (outs), (ins i32imm:$a, GRRegs:$b),
                        "tsetmr r$a, $b", []>;

// Two operand long
def BITREV_l2r : _FL2R<0b0000011000, (outs GRRegs:$dst), (ins GRRegs:$src),
                       "bitrev $dst, $src",
                       [(set GRRegs:$dst, (int_xcore_bitrev GRRegs:$src))]>;

def BYTEREV_l2r : _FL2R<0b0000011001, (outs GRRegs:$dst), (ins GRRegs:$src),
                        "byterev $dst, $src",
                        [(set GRRegs:$dst, (bswap GRRegs:$src))]>;

def CLZ_l2r : _FL2R<0b0000111000, (outs GRRegs:$dst), (ins GRRegs:$src),
                    "clz $dst, $src",
                    [(set GRRegs:$dst, (ctlz GRRegs:$src))]>;

def GETD_l2r : _FL2R<0b0001111001, (outs GRRegs:$dst), (ins GRRegs:$src),
                     "getd $dst, res[$src]", []>;

def GETN_l2r : _FL2R<0b0011011001, (outs GRRegs:$dst), (ins GRRegs:$src),
                     "getn $dst, res[$src]", []>;

def SETC_l2r : _FL2R<0b0010111001, (outs), (ins GRRegs:$r, GRRegs:$val),
                     "setc res[$r], $val",
                     [(int_xcore_setc GRRegs:$r, GRRegs:$val)]>;

def SETTW_l2r : _FLR2R<0b0010011001, (outs), (ins GRRegs:$r, GRRegs:$val),
                       "settw res[$r], $val",
                       [(int_xcore_settw GRRegs:$r, GRRegs:$val)]>;

def GETPS_l2r : _FL2R<0b0001011001, (outs GRRegs:$dst), (ins GRRegs:$src),
                      "get $dst, ps[$src]",
                      [(set GRRegs:$dst, (int_xcore_getps GRRegs:$src))]>;

def SETPS_l2r : _FLR2R<0b0001111000, (outs), (ins GRRegs:$src1, GRRegs:$src2),
                       "set ps[$src1], $src2",
                       [(int_xcore_setps GRRegs:$src1, GRRegs:$src2)]>;

def INITLR_l2r : _FL2R<0b0001011000, (outs), (ins GRRegs:$src, GRRegs:$t),
                       "init t[$t]:lr, $src",
                       [(int_xcore_initlr GRRegs:$t, GRRegs:$src)]>;

def SETCLK_l2r : _FLR2R<0b0000111001, (outs), (ins GRRegs:$src1, GRRegs:$src2),
                        "setclk res[$src1], $src2",
                        [(int_xcore_setclk GRRegs:$src1, GRRegs:$src2)]>;

def SETN_l2r : _FLR2R<0b0011011000, (outs), (ins GRRegs:$src1, GRRegs:$src2),
                      "setn res[$src1], $src2", []>;

def SETRDY_l2r : _FLR2R<0b0010111000, (outs), (ins GRRegs:$src1, GRRegs:$src2),
                        "setrdy res[$src1], $src2",
                        [(int_xcore_setrdy GRRegs:$src1, GRRegs:$src2)]>;

def TESTLCL_l2r : _FL2R<0b0010011000, (outs GRRegs:$dst), (ins GRRegs:$src),
                        "testlcl $dst, res[$src]", []>;

// One operand short
def MSYNC_1r : _F1R<0b000111, (outs), (ins GRRegs:$a),
                    "msync res[$a]",
                    [(int_xcore_msync GRRegs:$a)]>;
def MJOIN_1r : _F1R<0b000101, (outs), (ins GRRegs:$a),
                    "mjoin res[$a]",
                    [(int_xcore_mjoin GRRegs:$a)]>;

let isBranch=1, isIndirectBranch=1, isTerminator=1, isBarrier = 1 in
def BAU_1r : _F1R<0b001001, (outs), (ins GRRegs:$a),
                 "bau $a",
                 [(brind GRRegs:$a)]>;

let isBranch=1, isIndirectBranch=1, isTerminator=1, isBarrier = 1 in
def BR_JT : PseudoInstXCore<(outs), (ins InlineJT:$t, GRRegs:$i),
                            "bru $i\n$t",
                            [(XCoreBR_JT tjumptable:$t, GRRegs:$i)]>;

let isBranch=1, isIndirectBranch=1, isTerminator=1, isBarrier = 1 in
def BR_JT32 : PseudoInstXCore<(outs), (ins InlineJT32:$t, GRRegs:$i),
                              "bru $i\n$t",
                              [(XCoreBR_JT32 tjumptable:$t, GRRegs:$i)]>;

let isBranch=1, isIndirectBranch=1, isTerminator=1, isBarrier = 1 in
def BRU_1r : _F1R<0b001010, (outs), (ins GRRegs:$a), "bru $a", []>;

let Defs=[SP], hasSideEffects=0 in
def SETSP_1r : _F1R<0b001011, (outs), (ins GRRegs:$a), "set sp, $a", []>;

let hasSideEffects=0 in
def SETDP_1r : _F1R<0b001100, (outs), (ins GRRegs:$a), "set dp, $a", []>;

let hasSideEffects=0 in
def SETCP_1r : _F1R<0b001101, (outs), (ins GRRegs:$a), "set cp, $a", []>;

let hasCtrlDep = 1 in
def ECALLT_1r : _F1R<0b010011, (outs), (ins GRRegs:$a),
                 "ecallt $a",
                 []>;

let hasCtrlDep = 1 in
def ECALLF_1r : _F1R<0b010010, (outs), (ins GRRegs:$a),
                 "ecallf $a",
                 []>;

let isCall=1,
// All calls clobber the link register and the non-callee-saved registers:
Defs = [R0, R1, R2, R3, R11, LR], Uses = [SP] in {
def BLA_1r : _F1R<0b001000, (outs), (ins GRRegs:$a),
                 "bla $a",
                 [(XCoreBranchLink GRRegs:$a)]>;
}

def SYNCR_1r : _F1R<0b100001, (outs), (ins GRRegs:$a),
                 "syncr res[$a]",
                 [(int_xcore_syncr GRRegs:$a)]>;

def FREER_1r : _F1R<0b000100, (outs), (ins GRRegs:$a),
               "freer res[$a]",
               [(int_xcore_freer GRRegs:$a)]>;

let Uses=[R11] in {
def SETV_1r : _F1R<0b010001, (outs), (ins GRRegs:$a),
                   "setv res[$a], r11",
                   [(int_xcore_setv GRRegs:$a, R11)]>;

def SETEV_1r : _F1R<0b001111, (outs), (ins GRRegs:$a),
                    "setev res[$a], r11",
                    [(int_xcore_setev GRRegs:$a, R11)]>;
}

def DGETREG_1r : _F1R<0b001110, (outs GRRegs:$a), (ins), "dgetreg $a", []>;

def EDU_1r : _F1R<0b000000, (outs), (ins GRRegs:$a), "edu res[$a]",
                  [(int_xcore_edu GRRegs:$a)]>;

def EEU_1r : _F1R<0b000001, (outs), (ins GRRegs:$a),
               "eeu res[$a]",
               [(int_xcore_eeu GRRegs:$a)]>;

def KCALL_1r : _F1R<0b010000, (outs), (ins GRRegs:$a), "kcall $a", []>;

def WAITEF_1R : _F1R<0b000011, (outs), (ins GRRegs:$a), "waitef $a", []>;

def WAITET_1R : _F1R<0b000010, (outs), (ins GRRegs:$a), "waitet $a", []>;

def TSTART_1R : _F1R<0b000110, (outs), (ins GRRegs:$a), "start t[$a]", []>;

def CLRPT_1R : _F1R<0b100000, (outs), (ins GRRegs:$a), "clrpt res[$a]",
                    [(int_xcore_clrpt GRRegs:$a)]>;

// Zero operand short

def CLRE_0R : _F0R<0b0000001101, (outs), (ins), "clre", [(int_xcore_clre)]>;

def DCALL_0R : _F0R<0b0000011100, (outs), (ins), "dcall", []>;

let Defs = [SP], Uses = [SP] in
def DENTSP_0R : _F0R<0b0001001100, (outs), (ins), "dentsp", []>;

let Defs = [SP] in
def DRESTSP_0R : _F0R<0b0001001101, (outs), (ins), "drestsp", []>;

def DRET_0R : _F0R<0b0000011110, (outs), (ins), "dret", []>;

def FREET_0R : _F0R<0b0000001111, (outs), (ins), "freet", []>;

let Defs = [R11] in {
def GETID_0R : _F0R<0b0001001110, (outs), (ins),
                    "get r11, id",
                    [(set R11, (int_xcore_getid))]>;

def GETED_0R : _F0R<0b0000111110, (outs), (ins),
                    "get r11, ed",
                    [(set R11, (int_xcore_geted))]>;

def GETET_0R : _F0R<0b0000111111, (outs), (ins),
                    "get r11, et",
                    [(set R11, (int_xcore_getet))]>;

def GETKEP_0R : _F0R<0b0001001111, (outs), (ins),
                     "get r11, kep", []>;

def GETKSP_0R : _F0R<0b0001011100, (outs), (ins),
                     "get r11, ksp", []>;
}

let Defs = [SP] in
def KRET_0R : _F0R<0b0000011101, (outs), (ins), "kret", []>;

let Uses = [SP], mayLoad = 1 in {
def LDET_0R : _F0R<0b0001011110, (outs), (ins), "ldw et, sp[4]", []>;

def LDSED_0R : _F0R<0b0001011101, (outs), (ins), "ldw sed, sp[3]", []>;

def LDSPC_0R : _F0R<0b0000101100, (outs), (ins), "ldw spc, sp[1]", []>;

def LDSSR_0R : _F0R<0b0000101110, (outs), (ins), "ldw ssr, sp[2]", []>;
}

let Uses=[R11] in
def SETKEP_0R : _F0R<0b0000011111, (outs), (ins), "set kep, r11", []>;

def SSYNC_0r : _F0R<0b0000001110, (outs), (ins),
                    "ssync",
                    [(int_xcore_ssync)]>;

let Uses = [SP], mayStore = 1 in {
def STET_0R : _F0R<0b0000111101, (outs), (ins), "stw et, sp[4]", []>;

def STSED_0R : _F0R<0b0000111100, (outs), (ins), "stw sed, sp[3]", []>;

def STSPC_0R : _F0R<0b0000101101, (outs), (ins), "stw spc, sp[1]", []>;

def STSSR_0R : _F0R<0b0000101111, (outs), (ins), "stw ssr, sp[2]", []>;
}

let isBranch=1, isIndirectBranch=1, isTerminator=1, isBarrier = 1,
    hasSideEffects = 1 in
def WAITEU_0R : _F0R<0b0000001100, (outs), (ins),
                     "waiteu",
                     [(brind (int_xcore_waitevent))]>;

//===----------------------------------------------------------------------===//
// Non-Instruction Patterns
//===----------------------------------------------------------------------===//

def : Pat<(XCoreBranchLink texternalsym:$addr), (BLRF_lu10 texternalsym:$addr)>;

/// sext_inreg
def : Pat<(sext_inreg GRRegs:$b, i1), (SEXT_rus GRRegs:$b, 1)>;
def : Pat<(sext_inreg GRRegs:$b, i8), (SEXT_rus GRRegs:$b, 8)>;
def : Pat<(sext_inreg GRRegs:$b, i16), (SEXT_rus GRRegs:$b, 16)>;

/// loads
def : Pat<(zextloadi8 (add GRRegs:$addr, GRRegs:$offset)),
          (LD8U_3r GRRegs:$addr, GRRegs:$offset)>;
def : Pat<(zextloadi8 GRRegs:$addr), (LD8U_3r GRRegs:$addr, (LDC_ru6 0))>;

def : Pat<(sextloadi16 (lda16f GRRegs:$addr, GRRegs:$offset)),
          (LD16S_3r GRRegs:$addr, GRRegs:$offset)>;
def : Pat<(sextloadi16 GRRegs:$addr), (LD16S_3r GRRegs:$addr, (LDC_ru6 0))>;

def : Pat<(load (ldawf GRRegs:$addr, GRRegs:$offset)),
          (LDW_3r GRRegs:$addr, GRRegs:$offset)>;
def : Pat<(load (add GRRegs:$addr, immUs4:$offset)),
          (LDW_2rus GRRegs:$addr, (div4_xform immUs4:$offset))>;
def : Pat<(load GRRegs:$addr), (LDW_2rus GRRegs:$addr, 0)>;

/// anyext
def : Pat<(extloadi8 (add GRRegs:$addr, GRRegs:$offset)),
          (LD8U_3r GRRegs:$addr, GRRegs:$offset)>;
def : Pat<(extloadi8 GRRegs:$addr), (LD8U_3r GRRegs:$addr, (LDC_ru6 0))>;
def : Pat<(extloadi16 (lda16f GRRegs:$addr, GRRegs:$offset)),
          (LD16S_3r GRRegs:$addr, GRRegs:$offset)>;
def : Pat<(extloadi16 GRRegs:$addr), (LD16S_3r GRRegs:$addr, (LDC_ru6 0))>;

/// stores
def : Pat<(truncstorei8 GRRegs:$val, (add GRRegs:$addr, GRRegs:$offset)),
          (ST8_l3r GRRegs:$val, GRRegs:$addr, GRRegs:$offset)>;
def : Pat<(truncstorei8 GRRegs:$val, GRRegs:$addr),
          (ST8_l3r GRRegs:$val, GRRegs:$addr, (LDC_ru6 0))>;

def : Pat<(truncstorei16 GRRegs:$val, (lda16f GRRegs:$addr, GRRegs:$offset)),
          (ST16_l3r GRRegs:$val, GRRegs:$addr, GRRegs:$offset)>;
def : Pat<(truncstorei16 GRRegs:$val, GRRegs:$addr),
          (ST16_l3r GRRegs:$val, GRRegs:$addr, (LDC_ru6 0))>;

def : Pat<(store GRRegs:$val, (ldawf GRRegs:$addr, GRRegs:$offset)),
          (STW_l3r GRRegs:$val, GRRegs:$addr, GRRegs:$offset)>;
def : Pat<(store GRRegs:$val, (add GRRegs:$addr, immUs4:$offset)),
          (STW_2rus GRRegs:$val, GRRegs:$addr, (div4_xform immUs4:$offset))>;
def : Pat<(store GRRegs:$val, GRRegs:$addr),
          (STW_2rus GRRegs:$val, GRRegs:$addr, 0)>;

/// bitrev
def : Pat<(bitreverse GRRegs:$src), (BITREV_l2r GRRegs:$src)>;

/// cttz
def : Pat<(cttz GRRegs:$src), (CLZ_l2r (BITREV_l2r GRRegs:$src))>;

/// trap
def : Pat<(trap), (ECALLF_1r (LDC_ru6 0))>;

///
/// branch patterns
///

// unconditional branch
def : Pat<(br bb:$addr), (BRFU_lu6 bb:$addr)>;

// direct match equal/notequal zero brcond
def : Pat<(brcond (setne GRRegs:$lhs, 0), bb:$dst),
          (BRFT_lru6 GRRegs:$lhs, bb:$dst)>;
def : Pat<(brcond (seteq GRRegs:$lhs, 0), bb:$dst),
          (BRFF_lru6 GRRegs:$lhs, bb:$dst)>;

def : Pat<(brcond (setle GRRegs:$lhs, GRRegs:$rhs), bb:$dst),
          (BRFF_lru6 (LSS_3r GRRegs:$rhs, GRRegs:$lhs), bb:$dst)>;
def : Pat<(brcond (setule GRRegs:$lhs, GRRegs:$rhs), bb:$dst),
          (BRFF_lru6 (LSU_3r GRRegs:$rhs, GRRegs:$lhs), bb:$dst)>;
def : Pat<(brcond (setge GRRegs:$lhs, GRRegs:$rhs), bb:$dst),
          (BRFF_lru6 (LSS_3r GRRegs:$lhs, GRRegs:$rhs), bb:$dst)>;
def : Pat<(brcond (setuge GRRegs:$lhs, GRRegs:$rhs), bb:$dst),
          (BRFF_lru6 (LSU_3r GRRegs:$lhs, GRRegs:$rhs), bb:$dst)>;
def : Pat<(brcond (setne GRRegs:$lhs, GRRegs:$rhs), bb:$dst),
          (BRFF_lru6 (EQ_3r GRRegs:$lhs, GRRegs:$rhs), bb:$dst)>;
def : Pat<(brcond (setne GRRegs:$lhs, immUs:$rhs), bb:$dst),
          (BRFF_lru6 (EQ_2rus GRRegs:$lhs, immUs:$rhs), bb:$dst)>;

// generic brcond pattern
def : Pat<(brcond GRRegs:$cond, bb:$addr), (BRFT_lru6 GRRegs:$cond, bb:$addr)>;


///
/// Select patterns
///

// direct match equal/notequal zero select
def : Pat<(select (setne GRRegs:$lhs, 0), GRRegs:$T, GRRegs:$F),
        (SELECT_CC GRRegs:$lhs, GRRegs:$T, GRRegs:$F)>;

def : Pat<(select (seteq GRRegs:$lhs, 0), GRRegs:$T, GRRegs:$F),
        (SELECT_CC GRRegs:$lhs, GRRegs:$F, GRRegs:$T)>;

def : Pat<(select (setle GRRegs:$lhs, GRRegs:$rhs), GRRegs:$T, GRRegs:$F),
          (SELECT_CC (LSS_3r GRRegs:$rhs, GRRegs:$lhs), GRRegs:$F, GRRegs:$T)>;
def : Pat<(select (setule GRRegs:$lhs, GRRegs:$rhs), GRRegs:$T, GRRegs:$F),
          (SELECT_CC (LSU_3r GRRegs:$rhs, GRRegs:$lhs), GRRegs:$F, GRRegs:$T)>;
def : Pat<(select (setge GRRegs:$lhs, GRRegs:$rhs), GRRegs:$T, GRRegs:$F),
          (SELECT_CC (LSS_3r GRRegs:$lhs, GRRegs:$rhs), GRRegs:$F, GRRegs:$T)>;
def : Pat<(select (setuge GRRegs:$lhs, GRRegs:$rhs), GRRegs:$T, GRRegs:$F),
          (SELECT_CC (LSU_3r GRRegs:$lhs, GRRegs:$rhs), GRRegs:$F, GRRegs:$T)>;
def : Pat<(select (setne GRRegs:$lhs, GRRegs:$rhs), GRRegs:$T, GRRegs:$F),
          (SELECT_CC (EQ_3r GRRegs:$lhs, GRRegs:$rhs), GRRegs:$F, GRRegs:$T)>;
def : Pat<(select (setne GRRegs:$lhs, immUs:$rhs), GRRegs:$T, GRRegs:$F),
          (SELECT_CC (EQ_2rus GRRegs:$lhs, immUs:$rhs), GRRegs:$F, GRRegs:$T)>;

///
/// setcc patterns, only matched when none of the above brcond
/// patterns match
///

// setcc 2 register operands
def : Pat<(setle GRRegs:$lhs, GRRegs:$rhs),
          (EQ_2rus (LSS_3r GRRegs:$rhs, GRRegs:$lhs), 0)>;
def : Pat<(setule GRRegs:$lhs, GRRegs:$rhs),
          (EQ_2rus (LSU_3r GRRegs:$rhs, GRRegs:$lhs), 0)>;

def : Pat<(setgt GRRegs:$lhs, GRRegs:$rhs),
          (LSS_3r GRRegs:$rhs, GRRegs:$lhs)>;
def : Pat<(setugt GRRegs:$lhs, GRRegs:$rhs),
          (LSU_3r GRRegs:$rhs, GRRegs:$lhs)>;

def : Pat<(setge GRRegs:$lhs, GRRegs:$rhs),
          (EQ_2rus (LSS_3r GRRegs:$lhs, GRRegs:$rhs), 0)>;
def : Pat<(setuge GRRegs:$lhs, GRRegs:$rhs),
          (EQ_2rus (LSU_3r GRRegs:$lhs, GRRegs:$rhs), 0)>;

def : Pat<(setlt GRRegs:$lhs, GRRegs:$rhs),
          (LSS_3r GRRegs:$lhs, GRRegs:$rhs)>;
def : Pat<(setult GRRegs:$lhs, GRRegs:$rhs),
          (LSU_3r GRRegs:$lhs, GRRegs:$rhs)>;

def : Pat<(setne GRRegs:$lhs, GRRegs:$rhs),
          (EQ_2rus (EQ_3r GRRegs:$lhs, GRRegs:$rhs), 0)>;

def : Pat<(seteq GRRegs:$lhs, GRRegs:$rhs),
          (EQ_3r GRRegs:$lhs, GRRegs:$rhs)>;

// setcc reg/imm operands
def : Pat<(seteq GRRegs:$lhs, immUs:$rhs),
          (EQ_2rus GRRegs:$lhs, immUs:$rhs)>;
def : Pat<(setne GRRegs:$lhs, immUs:$rhs),
          (EQ_2rus (EQ_2rus GRRegs:$lhs, immUs:$rhs), 0)>;

// misc
def : Pat<(add GRRegs:$addr, immUs4:$offset),
          (LDAWF_l2rus GRRegs:$addr, (div4_xform immUs4:$offset))>;

def : Pat<(sub GRRegs:$addr, immUs4:$offset),
          (LDAWB_l2rus GRRegs:$addr, (div4_xform immUs4:$offset))>;

def : Pat<(and GRRegs:$val, immMskBitp:$mask),
          (ZEXT_rus GRRegs:$val, (msksize_xform immMskBitp:$mask))>;

// (sub X, imm) gets canonicalized to (add X, -imm).  Match this form.
def : Pat<(add GRRegs:$src1, immUsNeg:$src2),
          (SUB_2rus GRRegs:$src1, (neg_xform immUsNeg:$src2))>;

def : Pat<(add GRRegs:$src1, immUs4Neg:$src2),
          (LDAWB_l2rus GRRegs:$src1, (div4neg_xform immUs4Neg:$src2))>;

///
/// Some peepholes
///

def : Pat<(mul GRRegs:$src, 3),
          (LDA16F_l3r GRRegs:$src, GRRegs:$src)>;

def : Pat<(mul GRRegs:$src, 5),
          (LDAWF_l3r GRRegs:$src, GRRegs:$src)>;

def : Pat<(mul GRRegs:$src, -3),
          (LDAWB_l3r GRRegs:$src, GRRegs:$src)>;

// ashr X, 32 is equivalent to ashr X, 31 on the XCore.
def : Pat<(sra GRRegs:$src, 31),
          (ASHR_l2rus GRRegs:$src, 32)>;

def : Pat<(brcond (setlt GRRegs:$lhs, 0), bb:$dst),
          (BRFT_lru6 (ASHR_l2rus GRRegs:$lhs, 32), bb:$dst)>;

// setge X, 0 is canonicalized to setgt X, -1
def : Pat<(brcond (setgt GRRegs:$lhs, -1), bb:$dst),
          (BRFF_lru6 (ASHR_l2rus GRRegs:$lhs, 32), bb:$dst)>;

def : Pat<(select (setlt GRRegs:$lhs, 0), GRRegs:$T, GRRegs:$F),
          (SELECT_CC (ASHR_l2rus GRRegs:$lhs, 32), GRRegs:$T, GRRegs:$F)>;

def : Pat<(select (setgt GRRegs:$lhs, -1), GRRegs:$T, GRRegs:$F),
          (SELECT_CC (ASHR_l2rus GRRegs:$lhs, 32), GRRegs:$F, GRRegs:$T)>;

def : Pat<(setgt GRRegs:$lhs, -1),
          (EQ_2rus (ASHR_l2rus GRRegs:$lhs, 32), 0)>;

def : Pat<(sra (shl GRRegs:$src, immBpwSubBitp:$imm), immBpwSubBitp:$imm),
          (SEXT_rus GRRegs:$src, (bpwsub_xform immBpwSubBitp:$imm))>;

def : Pat<(load (cprelwrapper tconstpool:$b)),
          (LDWCP_lru6 tconstpool:$b)>;

def : Pat<(cprelwrapper tconstpool:$b),
          (LDAWCP_lu6 tconstpool:$b)>;
