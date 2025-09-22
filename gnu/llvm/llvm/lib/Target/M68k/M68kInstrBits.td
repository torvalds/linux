//===-- M68kInstrBits.td - Bit Manipulation Instrs ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes the bit manipulation instructions in the M68k
/// architecture. Here is the current status of the file:
///
///  Machine:
///
///    BCHG    [~]   BCLR    [~]   BSET     [~]   BTST     [~]
///
///  Map:
///
///   [ ] - was not touched at all
///   [!] - requires extarnal stuff implemented
///   [~] - in progress but usable
///   [x] - done
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// BTST
//===----------------------------------------------------------------------===//

/// ------------+---------+---------+---------+---------
///  F  E  D  C | B  A  9 | 8  7  6 | 5  4  3 | 2  1  0
/// ------------+---------+---------+---------+---------
///  0  0  0  0 |   REG   | OP MODE |   MODE  |   REG
/// ------------+---------+---------+---------+---------
class MxBITEnc_R<bits<3> opmode, MxEncMemOp dst_enc, string bitno_name> {
  dag Value = (ascend
    (descend 0b0000,
      (operand "$"#bitno_name, 3),
      opmode, dst_enc.EA
    ),
    dst_enc.Supplement
  );
}

/// ---------------------+---------+---------+---------
///  F  E  D  C  B  A  9 | 8  7  6 | 5  4  3 | 2  1  0
/// ---------------------+---------+---------+---------
///  0  0  0  0  1  0  0 | OP MODE |   MODE  |   REG
/// ---------------------+--+------+---------+---------
///  0  0  0  0  0  0  0  0 |        BIT NUMBER
/// ------------------------+--------------------------
class MxBITEnc_I<bits<3> opmode, MxEncMemOp dst_enc, string bitno_name> {
  dag Value = (ascend
    (descend 0b0000100, opmode, dst_enc.EA),
    (descend 0b00000000, (operand "$"#bitno_name, 8)),
    dst_enc.Supplement
  );
}

let Defs = [CCR] in {
class MxBIT_RR<string MN, bits<3> OPMODE, MxType TYPE>
    : MxInst<(outs), (ins TYPE.ROp:$dst, TYPE.ROp:$bitno),
             MN#"\t$bitno, $dst"> {
  let Inst = MxBITEnc_R<OPMODE, MxEncAddrMode_r<"dst">, "bitno">.Value;
}

class MxBIT_RI<string MN, bits<3> OPMODE, MxType TYPE>
    : MxInst<(outs), (ins TYPE.ROp:$dst, TYPE.IOp:$bitno),
             MN#"\t$bitno, $dst"> {
  let Inst = MxBITEnc_I<OPMODE, MxEncAddrMode_r<"dst">, "bitno">.Value;
}

class MxBIT_MR<string MN, bits<3> OPMODE, MxType TYPE,
                MxOperand MEMOpd, MxEncMemOp DST_ENC>
    : MxInst<(outs), (ins MEMOpd:$dst, TYPE.ROp:$bitno),
             MN#"\t$bitno, $dst"> {
  let Inst = MxBITEnc_R<OPMODE, DST_ENC, "bitno">.Value;
}

class MxBIT_MI<string MN, bits<3> OPMODE, MxType TYPE,
                MxOperand MEMOpd, MxEncMemOp DST_ENC>
    : MxInst<(outs), (ins MEMOpd:$dst, TYPE.IOp:$bitno),
             MN#"\t$bitno, $dst"> {
  let Inst = MxBITEnc_I<OPMODE, DST_ENC, "bitno">.Value;
}
} // Defs = [CCR]

def BTST8qd : MxBIT_MR<"btst", 0b100, MxType8d, MxType8.QOp,
                        MxEncAddrMode_q<"dst">>;
def BTST8kd : MxBIT_MR<"btst", 0b100, MxType8d, MxType8.KOp,
                        MxEncAddrMode_k<"dst">>;
def BTST8qi : MxBIT_MI<"btst", 0b000, MxType8d, MxType8.QOp,
                        MxEncAddrMode_q<"dst">>;
def BTST8ki : MxBIT_MI<"btst", 0b000, MxType8d, MxType8.KOp,
                        MxEncAddrMode_k<"dst">>;

multiclass MxBIT<string MN, bits<3> OP, bits<3> OPI> {
  // Register Bit manipulation limited to 32 bits only
  def NAME#32dd : MxBIT_RR<MN, OP, MxType32d>;
  def NAME#32di : MxBIT_RI<MN, OPI, MxType32d>;

  // Memory Bit manipulation limited to 8 bits only
  def NAME#8jd : MxBIT_MR<MN, OP, MxType8d,
                          MxType8.JOp, MxEncAddrMode_j<"dst">>;
  def NAME#8od : MxBIT_MR<MN, OP, MxType8d,
                          MxType8.OOp, MxEncAddrMode_o<"dst">>;
  def NAME#8ed : MxBIT_MR<MN, OP, MxType8d,
                          MxType8.EOp, MxEncAddrMode_e<"dst">>;
  def NAME#8pd : MxBIT_MR<MN, OP, MxType8d,
                          MxType8.POp, MxEncAddrMode_p<"dst">>;
  def NAME#8fd : MxBIT_MR<MN, OP, MxType8d,
                          MxType8.FOp, MxEncAddrMode_f<"dst">>;

  def NAME#8ji : MxBIT_MI<MN, OPI, MxType8d,
                          MxType8.JOp, MxEncAddrMode_j<"dst">>;
  def NAME#8oi : MxBIT_MI<MN, OPI, MxType8d,
                          MxType8.OOp, MxEncAddrMode_o<"dst">>;
  def NAME#8ei : MxBIT_MI<MN, OPI, MxType8d,
                          MxType8.EOp, MxEncAddrMode_e<"dst">>;
  def NAME#8pi : MxBIT_MI<MN, OPI, MxType8d,
                          MxType8.POp, MxEncAddrMode_p<"dst">>;
  def NAME#8fi : MxBIT_MI<MN, OPI, MxType8d,
                          MxType8.FOp, MxEncAddrMode_f<"dst">>;
}

defm BCHG : MxBIT<"bchg", 0b101, 0b001>;
defm BCLR : MxBIT<"bclr", 0b110, 0b010>;
defm BSET : MxBIT<"bset", 0b111, 0b011>;
defm BTST : MxBIT<"btst", 0b100, 0b000>;

// Codegen patterns

multiclass MxBITPatR<MxInst INSTd, MxInst INSTi, SDNode NODE> {
def : Pat<(NODE MxType32d.VT:$dst, MxType32d.VT:$bitno),
          (INSTd MxType32d.ROp:$dst, MxType32d.ROp:$bitno)>;
def : Pat<(NODE MxType32d.VT:$dst, MxType32d.IPat:$bitno),
          (INSTi MxType32d.ROp:$dst, MxType32d.IOp:$bitno)>;
}

defm : MxBITPatR<BTST32dd, BTST32di, MxBtst>;

multiclass MxBITPatM<MxInst INSTd, MxInst INSTi, SDNode NODE, MxType TYPE,
                     MxOperand MEMOpd, ComplexPattern MEMPat> {
def : Pat<(NODE (TYPE.Load MEMPat:$dst), TYPE.VT:$bitno),
          (INSTd MEMOpd:$dst, TYPE.ROp:$bitno)>;
def : Pat<(NODE (TYPE.Load MEMPat:$dst), TYPE.IPat:$bitno),
          (INSTi MEMOpd:$dst, TYPE.IOp:$bitno)>;
}

defm : MxBITPatM<BTST8qd, BTST8qi, MxBtst,
                 MxType8d, MxType8.QOp, MxType8.QPat>;
defm : MxBITPatM<BTST8kd, BTST8ki, MxBtst,
                 MxType8d, MxType8.KOp, MxType8.KPat>;
defm : MxBITPatM<BTST8jd, BTST8ji, MxBtst,
                 MxType8d, MxType8.JOp, MxType8.JPat>;
defm : MxBITPatM<BTST8od, BTST8oi, MxBtst,
                 MxType8d, MxType8.OOp, MxType8.OPat>;
defm : MxBITPatM<BTST8ed, BTST8ei, MxBtst,
                 MxType8d, MxType8.EOp, MxType8.EPat>;
defm : MxBITPatM<BTST8pd, BTST8pi, MxBtst,
                 MxType8d, MxType8.POp, MxType8.PPat>;
defm : MxBITPatM<BTST8fd, BTST8fi, MxBtst,
                 MxType8d, MxType8.FOp, MxType8.FPat>;
