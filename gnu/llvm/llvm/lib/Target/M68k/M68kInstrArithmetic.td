//===-- M68kInstrArithmetic.td - Integer Arith Instrs ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes the integer arithmetic instructions in the M68k
/// architecture. Here is the current status of the file:
///
///  Machine:
///
///    ADD       [~]   ADDA      [~]   ADDI        [~]   ADDQ [ ]   ADDX [~]
///    CLR       [ ]   CMP       [~]   CMPA        [~]   CMPI [~]   CMPM [ ]
///    CMP2      [ ]   DIVS/DIVU [~]   DIVSL/DIVUL [ ]   EXT  [~]   EXTB [ ]
///    MULS/MULU [~]   NEG       [~]   NEGX        [~]   NOT  [~]   SUB  [~]
///    SUBA      [~]   SUBI      [~]   SUBQ        [ ]   SUBX [~]
///
///  Map:
///
///   [ ] - was not touched at all
///   [!] - requires extarnal stuff implemented
///   [~] - functional implementation
///   [X] - complete implementation
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// OPMODE Encoding
//===----------------------------------------------------------------------===//
class MxOpModeEncoding<bits<3> encoding> {
  bits<3> Value = encoding;
}

// op EA, Dn
def MxOpMode8_d_EA  : MxOpModeEncoding<0b000>;
def MxOpMode16_d_EA : MxOpModeEncoding<0b001>;
def MxOpMode32_d_EA : MxOpModeEncoding<0b010>;

// op Dn, EA
def MxOpMode8_EA_d  : MxOpModeEncoding<0b100>;
def MxOpMode16_EA_d : MxOpModeEncoding<0b101>;
def MxOpMode32_EA_d : MxOpModeEncoding<0b110>;

// op EA, An
def MxOpMode16_a_EA : MxOpModeEncoding<0b011>;
def MxOpMode32_a_EA : MxOpModeEncoding<0b111>;


//===----------------------------------------------------------------------===//
// Encoding
//===----------------------------------------------------------------------===//

let Defs = [CCR] in {
let Constraints = "$src = $dst" in {

/// Encoding for Normal forms
/// ----------------------------------------------------
///  F  E  D  C | B  A  9 | 8  7  6 | 5  4  3 | 2  1  0
/// ----------------------------------------------------
///             |         |         | EFFECTIVE ADDRESS
///  x  x  x  x |   REG   | OP MODE |   MODE  |   REG
/// ----------------------------------------------------

// $reg, $ccr <- $reg op $reg
class MxBiArOp_R_RR_xEA<string MN, SDNode NODE, MxType DST_TYPE, MxType SRC_TYPE,
                        bits<4> CMD>
    : MxInst<(outs DST_TYPE.ROp:$dst), (ins DST_TYPE.ROp:$src, SRC_TYPE.ROp:$opd),
             MN#"."#DST_TYPE.Prefix#"\t$opd, $dst",
             [(set DST_TYPE.VT:$dst, CCR, (NODE DST_TYPE.VT:$src, SRC_TYPE.VT:$opd))]> {
  let Inst = (descend
    CMD, (operand "$dst", 3),
    !cast<MxOpModeEncoding>("MxOpMode"#DST_TYPE.Size#"_"#DST_TYPE.RLet#"_EA").Value,
    !cond(
      !eq(SRC_TYPE.RLet, "r") : (descend 0b00, (operand "$opd", 4)),
      !eq(SRC_TYPE.RLet, "d") : (descend 0b000, (operand "$opd", 3))
    )
  );
}

/// This Op is similar to the one above except it uses reversed opmode, some
/// commands(e.g. eor) do not support dEA or rEA modes and require EAd for
/// register only operations.
/// NOTE when using dd commands it is irrelevant which opmode to use(as it seems)
/// but some opcodes support address register and some do not which creates this
/// mess.
class MxBiArOp_R_RR_EAd<string MN, SDNode NODE, MxType TYPE, bits<4> CMD>
    : MxInst<(outs TYPE.ROp:$dst), (ins TYPE.ROp:$src, TYPE.ROp:$opd),
             MN#"."#TYPE.Prefix#"\t$opd, $dst",
             [(set TYPE.VT:$dst, CCR, (NODE TYPE.VT:$src, TYPE.VT:$opd))]> {
  let Inst = (descend
    CMD, (operand "$opd", 3),
    !cast<MxOpModeEncoding>("MxOpMode"#TYPE.Size#"_EA_"#TYPE.RLet).Value,
    /*Destination can only be a data register*/
    /*MODE*/0b000,
    /*REGISTER*/(operand "$dst", 3));
}

let mayLoad = 1 in
class MxBiArOp_R_RM<string MN, SDNode NODE, MxType TYPE, MxOperand OPD, ComplexPattern PAT,
                    bits<4> CMD, MxEncMemOp SRC_ENC>
    : MxInst<(outs TYPE.ROp:$dst), (ins TYPE.ROp:$src, OPD:$opd),
             MN#"."#TYPE.Prefix#"\t$opd, $dst",
             [(set TYPE.VT:$dst, CCR, (NODE TYPE.VT:$src, (TYPE.Load PAT:$opd)))]> {
  let Inst = (ascend
    (descend CMD, (operand "$dst", 3),
             !cast<MxOpModeEncoding>("MxOpMode"#TYPE.Size#"_"#TYPE.RLet#"_EA").Value,
             SRC_ENC.EA),
    SRC_ENC.Supplement
  );
}

/// Encoding for Immediate forms
/// ---------------------------------------------------
///  F  E  D  C  B  A  9  8 | 7  6 | 5  4  3 | 2  1  0
/// ---------------------------------------------------
///                         |      | EFFECTIVE ADDRESS
///  x  x  x  x  x  x  x  x | SIZE |   MODE  |   REG
/// ---------------------------------------------------
///     16-BIT WORD DATA    |     8-BIT BYTE DATA
/// ---------------------------------------------------
///                 32-BIT LONG DATA
/// ---------------------------------------------------
/// NOTE It is used to store an immediate to memory, imm-to-reg are handled with
/// normal version

// $reg <- $reg op $imm
class MxBiArOp_R_RI_xEA<string MN, SDNode NODE, MxType TYPE, bits<4> CMD>
    : MxInst<(outs TYPE.ROp:$dst), (ins TYPE.ROp:$src, TYPE.IOp:$opd),
             MN#"."#TYPE.Prefix#"\t$opd, $dst",
             [(set TYPE.VT:$dst, CCR, (NODE TYPE.VT:$src, TYPE.IPat:$opd))]> {
  let Inst = (ascend
    (descend CMD, (operand "$dst", 3),
             !cast<MxOpModeEncoding>("MxOpMode"#TYPE.Size#"_"#TYPE.RLet#"_EA").Value,
             MxEncAddrMode_i<"opd", TYPE.Size>.EA),
    MxEncAddrMode_i<"opd", TYPE.Size>.Supplement
  );
}

// Again, there are two ways to write an immediate to Dn register either dEA
// opmode or using *I encoding, and again some instructions also support address
// registers some do not.
class MxBiArOp_R_RI<string MN, SDNode NODE, MxType TYPE, bits<4> CMD>
    : MxInst<(outs TYPE.ROp:$dst), (ins TYPE.ROp:$src, TYPE.IOp:$opd),
             MN#"i."#TYPE.Prefix#"\t$opd, $dst",
             [(set TYPE.VT:$dst, CCR, (NODE TYPE.VT:$src, TYPE.IPat:$opd))]> {
  let Inst = (ascend
    (descend 0b0000, CMD,
             !cast<MxEncSize>("MxEncSize"#TYPE.Size).Value,
             // The destination cannot be address register, so it's always
             // the MODE for data register direct mode.
             /*MODE*/0b000,
             /*REGISTER*/(operand "$dst", 3)),
    // Source (i.e. immediate value) encoding
    MxEncAddrMode_i<"opd", TYPE.Size>.Supplement
  );
}
} // Constraints

let mayLoad = 1, mayStore = 1 in {

// FIXME MxBiArOp_FMR/FMI cannot consume CCR from MxAdd/MxSub which leads for
// MxAdd to survive the match and subsequent mismatch.
class MxBiArOp_MR<string MN, MxType TYPE,
                  MxOperand MEMOpd, bits<4> CMD, MxEncMemOp DST_ENC>
    : MxInst<(outs), (ins MEMOpd:$dst, TYPE.ROp:$opd),
             MN#"."#TYPE.Prefix#"\t$opd, $dst", []> {
  let Inst = (ascend
    (descend CMD, (operand "$opd", 3),
             !cast<MxOpModeEncoding>("MxOpMode"#TYPE.Size#"_EA_"#TYPE.RLet).Value,
             DST_ENC.EA),
    DST_ENC.Supplement
  );
}

class MxBiArOp_MI<string MN, MxType TYPE,
                  MxOperand MEMOpd, bits<4> CMD, MxEncMemOp DST_ENC>
    : MxInst<(outs), (ins MEMOpd:$dst, TYPE.IOp:$opd),
             MN#"."#TYPE.Prefix#"\t$opd, $dst", []> {
  let Inst = (ascend
    (descend 0b0000, CMD,
             !cast<MxEncSize>("MxEncSize"#TYPE.Size).Value,
             DST_ENC.EA),
    // Source (i.e. immediate value) encoding
    MxEncAddrMode_i<"opd", TYPE.Size>.Supplement,
    // Destination encoding
    DST_ENC.Supplement
  );
}
} // mayLoad, mayStore
} // Defs = [CCR]

multiclass MxBiArOp_DF<string MN, SDNode NODE, bit isComm,
                       bits<4> CMD, bits<4> CMDI> {

  foreach SZ = [8, 16, 32] in {
    // op $mem, $reg
    def NAME#SZ#"dk"  : MxBiArOp_R_RM<MN, NODE,
                                      !cast<MxType>("MxType"#SZ#"d"),
                                      !cast<MxType>("MxType"#SZ).KOp,
                                      !cast<MxType>("MxType"#SZ).KPat,
                                      CMD, MxEncAddrMode_k<"opd">>;

    def NAME#SZ#"dq"  : MxBiArOp_R_RM<MN, NODE,
                                      !cast<MxType>("MxType"#SZ#"d"),
                                      !cast<MxType>("MxType"#SZ).QOp,
                                      !cast<MxType>("MxType"#SZ).QPat,
                                      CMD, MxEncAddrMode_q<"opd">>;

    def NAME#SZ#"dp"  : MxBiArOp_R_RM<MN, NODE,
                                      !cast<MxType>("MxType"#SZ#"d"),
                                      !cast<MxType>("MxType"#SZ).POp,
                                      !cast<MxType>("MxType"#SZ).PPat,
                                      CMD, MxEncAddrMode_p<"opd">>;

    def NAME#SZ#"df"  : MxBiArOp_R_RM<MN, NODE,
                                      !cast<MxType>("MxType"#SZ#"d"),
                                      !cast<MxType>("MxType"#SZ).FOp,
                                      !cast<MxType>("MxType"#SZ).FPat,
                                      CMD, MxEncAddrMode_f<"opd">>;

    def NAME#SZ#"dj"  : MxBiArOp_R_RM<MN, NODE,
                                      !cast<MxType>("MxType"#SZ#"d"),
                                      !cast<MxType>("MxType"#SZ).JOp,
                                      !cast<MxType>("MxType"#SZ).JPat,
                                      CMD, MxEncAddrMode_j<"opd">>;
    // op $imm, $reg
    def NAME#SZ#"di"  : MxBiArOp_R_RI_xEA<MN, NODE,
                                          !cast<MxType>("MxType"#SZ#"d"),
                                          CMD>;
    // op $reg, $mem
    def NAME#SZ#"pd"  : MxBiArOp_MR<MN,
                                    !cast<MxType>("MxType"#SZ#"d"),
                                    !cast<MxType>("MxType"#SZ).POp,
                                    CMD, MxEncAddrMode_p<"dst">>;

    def NAME#SZ#"fd"  : MxBiArOp_MR<MN,
                                    !cast<MxType>("MxType"#SZ#"d"),
                                    !cast<MxType>("MxType"#SZ).FOp,
                                    CMD, MxEncAddrMode_f<"dst">>;

    def NAME#SZ#"jd"  : MxBiArOp_MR<MN,
                                    !cast<MxType>("MxType"#SZ#"d"),
                                    !cast<MxType>("MxType"#SZ).JOp,
                                    CMD, MxEncAddrMode_j<"dst">>;
    // op $imm, $mem
    def NAME#SZ#"pi"  : MxBiArOp_MI<MN,
                                    !cast<MxType>("MxType"#SZ),
                                    !cast<MxType>("MxType"#SZ).POp,
                                    CMDI, MxEncAddrMode_p<"dst">>;

    def NAME#SZ#"fi"  : MxBiArOp_MI<MN,
                                    !cast<MxType>("MxType"#SZ),
                                    !cast<MxType>("MxType"#SZ).FOp,
                                    CMDI, MxEncAddrMode_f<"dst">>;

    def NAME#SZ#"ji"  : MxBiArOp_MI<MN,
                                    !cast<MxType>("MxType"#SZ),
                                    !cast<MxType>("MxType"#SZ).JOp,
                                    CMDI, MxEncAddrMode_j<"dst">>;
    // op $reg, $reg
    let isCommutable = isComm in
    def NAME#SZ#"dd" : MxBiArOp_R_RR_xEA<MN, NODE,
                                         !cast<MxType>("MxType"#SZ#"d"),
                                         !cast<MxType>("MxType"#SZ#"d"),
                                         CMD>;
  } // foreach SZ

  foreach SZ = [16, 32] in
  def NAME#SZ#"dr" : MxBiArOp_R_RR_xEA<MN, NODE,
                                       !cast<MxType>("MxType"#SZ#"d"),
                                       !cast<MxType>("MxType"#SZ#"r"),
                                       CMD>;

} // MxBiArOp_DF


// These special snowflakes allowed to match address registers but since *A
// operations do not produce CCR we should not match them against Mx nodes that
// produce it.
let Pattern = [(null_frag)] in
multiclass MxBiArOp_AF<string MN, SDNode NODE, bits<4> CMD> {

  def NAME#"32ak" : MxBiArOp_R_RM<MN, NODE, MxType32a, MxType32.KOp, MxType32.KPat,
                                  CMD, MxEncAddrMode_k<"opd">>;
  def NAME#"32aq" : MxBiArOp_R_RM<MN, NODE, MxType32a, MxType32.QOp, MxType32.QPat,
                                  CMD, MxEncAddrMode_q<"opd">>;
  def NAME#"32af" : MxBiArOp_R_RM<MN, NODE, MxType32a, MxType32.FOp, MxType32.FPat,
                                  CMD, MxEncAddrMode_f<"opd">>;
  def NAME#"32ap" : MxBiArOp_R_RM<MN, NODE, MxType32a, MxType32.POp, MxType32.PPat,
                                  CMD, MxEncAddrMode_p<"opd">>;
  def NAME#"32aj" : MxBiArOp_R_RM<MN, NODE, MxType32a, MxType32.JOp, MxType32.JPat,
                                  CMD, MxEncAddrMode_j<"opd">>;
  def NAME#"32ab" : MxBiArOp_R_RM<MN, NODE, MxType32a, MxType32.BOp, MxType32.BPat,
                                  CMD, MxEncAddrMode_abs<"opd", true>>;
  def NAME#"32ai" : MxBiArOp_R_RI_xEA<MN, NODE, MxType32a, CMD>;

  def NAME#"32ar" : MxBiArOp_R_RR_xEA<MN, NODE, MxType32a, MxType32r, CMD>;

} // MxBiArOp_AF

// NOTE These naturally produce CCR

//===----------------------------------------------------------------------===//
// Add/Sub
//===----------------------------------------------------------------------===//

defm ADD : MxBiArOp_DF<"add",  MxAdd, 1, 0xD, 0x6>;
defm ADD : MxBiArOp_AF<"adda", MxAdd, 0xD>;
defm SUB : MxBiArOp_DF<"sub",  MxSub, 0, 0x9, 0x4>;
defm SUB : MxBiArOp_AF<"suba", MxSub, 0x9>;

// This pattern is used to enable the instruction selector to select ADD32ab
// for global values that are allocated in thread-local storage, i.e.:
//   t8: i32 = ISD::ADD GLOBAL_OFFSET_TABLE, TargetGlobalTLSAddress:i32<ptr @myvar>
//     ====>
//   t8: i32,i8 = ADD32ab GLOBAL_OFFSET_TABLE, TargetGlobalTLSAddress:i32<ptr @myvar>
def : Pat<(add MxARD32:$src, tglobaltlsaddr:$opd), (ADD32ab MxARD32:$src, MxAL32:$opd)>;

let Uses = [CCR], Defs = [CCR] in {
let Constraints = "$src = $dst" in {

/// Encoding for Extended forms
/// ------------------------------------------------------
///  F  E  D  C | B  A  9 | 8 | 7  6 | 5  4 | 3 | 2  1  0
/// ------------------------------------------------------
///  x  x  x  x |  REG Rx | 1 | SIZE | 0  0 | M |  REG Ry
/// ------------------------------------------------------
/// Rx - destination
/// Ry - source
/// M  - address mode switch

// $reg, ccr <- $reg op $reg op ccr
class MxBiArOp_R_RRX<string MN, SDNode NODE, MxType TYPE, bits<4> CMD>
    : MxInst<(outs TYPE.ROp:$dst), (ins TYPE.ROp:$src, TYPE.ROp:$opd),
             MN#"."#TYPE.Prefix#"\t$opd, $dst",
             [(set TYPE.VT:$dst, CCR, (NODE TYPE.VT:$src, TYPE.VT:$opd, CCR))]> {
  let Inst = (descend CMD,
    // Destination register
    (operand "$dst", 3),
    0b1,
    // SIZE
    !cond(!eq(TYPE.Size, 8): 0b00,
          !eq(TYPE.Size, 16): 0b01,
          !eq(TYPE.Size, 32): 0b10),
    0b00, /*R/M*/0b0,
    // Source register
    (operand "$opd", 3)
  );
}
} // Constraints
} // Uses, Defs

multiclass MxBiArOp_RFF<string MN, SDNode NODE, bit isComm, bits<4> CMD> {

let isCommutable = isComm in {
  foreach SZ = [8, 16, 32] in
  def NAME#SZ#"dd"  : MxBiArOp_R_RRX<MN, NODE, !cast<MxType>("MxType"#SZ#"d"), CMD>;
} // isComm

} // MxBiArOp_RFF

// NOTE These consume and produce CCR
defm ADDX : MxBiArOp_RFF<"addx", MxAddX, 1, 0xD>;
defm SUBX : MxBiArOp_RFF<"subx", MxSubX, 0, 0x9>;


//===----------------------------------------------------------------------===//
// And/Xor/Or
//===----------------------------------------------------------------------===//

defm AND : MxBiArOp_DF<"and", MxAnd, 1, 0xC, 0x2>;
defm OR  : MxBiArOp_DF<"or",  MxOr,  1, 0x8, 0x0>;

multiclass MxBiArOp_DF_EAd<string MN, SDNode NODE, bits<4> CMD, bits<4> CMDI> {
  foreach SZ = [8, 16, 32] in {
    let isCommutable = 1 in
    def NAME#SZ#"dd"  : MxBiArOp_R_RR_EAd<MN, NODE,
                                          !cast<MxType>("MxType"#SZ#"d"),
                                          CMD>;

    def NAME#SZ#"di"  : MxBiArOp_R_RI<MN, NODE,
                                      !cast<MxType>("MxType"#SZ#"d"),
                                      CMDI>;
  } // foreach SZ
} // MxBiArOp_DF_EAd

defm XOR : MxBiArOp_DF_EAd<"eor", MxXor, 0xB, 0xA>;


//===----------------------------------------------------------------------===//
// CMP
//===----------------------------------------------------------------------===//

let Defs = [CCR] in {
class MxCmp_RR<MxType LHS_TYPE, MxType RHS_TYPE = LHS_TYPE>
    : MxInst<(outs), (ins LHS_TYPE.ROp:$lhs, RHS_TYPE.ROp:$rhs),
             "cmp."#RHS_TYPE.Prefix#"\t$lhs, $rhs",
             [(set CCR, (MxCmp LHS_TYPE.VT:$lhs, RHS_TYPE.VT:$rhs))]> {
  let Inst = (descend 0b1011,
    // REGISTER
    (operand "$rhs", 3),
    // OPMODE
    !cast<MxOpModeEncoding>("MxOpMode"#RHS_TYPE.Size#"_"#RHS_TYPE.RLet#"_EA").Value,
    // MODE without last bit
    0b00,
    // REGISTER prefixed by D/A bit
    (operand "$lhs", 4)
  );
}

class MxCmp_RI<MxType TYPE>
    : MxInst<(outs), (ins TYPE.IOp:$imm, TYPE.ROp:$reg),
             "cmpi."#TYPE.Prefix#"\t$imm, $reg",
             [(set CCR, (MxCmp TYPE.IPat:$imm, TYPE.VT:$reg))]> {
  let Inst = (ascend
    (descend 0b00001100,
             !cast<MxEncSize>("MxEncSize"#TYPE.Size).Value,
             // The destination cannot be address register, so it's always
             // the MODE for data register direct mode.
             /*MODE*/0b000,
             /*REGISTER*/(operand "$reg", 3)),
    // Source (i.e. immediate value) encoding
    MxEncAddrMode_i<"imm", TYPE.Size>.Supplement
  );
}

let mayLoad = 1 in {

class MxCmp_MI<MxType TYPE, MxOperand MEMOpd, ComplexPattern MEMPat,
               MxEncMemOp MEM_ENC>
    : MxInst<(outs), (ins TYPE.IOp:$imm, MEMOpd:$mem),
             "cmpi."#TYPE.Prefix#"\t$imm, $mem",
             [(set CCR, (MxCmp TYPE.IPat:$imm, (load MEMPat:$mem)))]> {
  let Inst = (ascend
    (descend 0b00001100,
             !cast<MxEncSize>("MxEncSize"#TYPE.Size).Value,
             MEM_ENC.EA),
    // Source (i.e. immediate value) encoding
    MxEncAddrMode_i<"imm", TYPE.Size>.Supplement,
    // Destination (i.e. memory operand) encoding
    MEM_ENC.Supplement
  );
}

// FIXME: What about abs.W?
class MxCmp_BI<MxType TYPE>
    : MxInst<(outs), (ins TYPE.IOp:$imm, MxAL32:$abs),
             "cmpi."#TYPE.Prefix#"\t$imm, $abs",
             [(set CCR, (MxCmp TYPE.IPat:$imm,
                               (load (i32 (MxWrapper tglobaladdr:$abs)))))]> {
  defvar AbsEncoding = MxEncAddrMode_abs<"abs", true>;
  let Inst = (ascend
    (descend 0b00001100,
             !cast<MxEncSize>("MxEncSize"#TYPE.Size).Value,
             AbsEncoding.EA),
    // Source (i.e. immediate value) encoding
    MxEncAddrMode_i<"imm", TYPE.Size>.Supplement,
    // Destination (i.e. memory operand) encoding
    AbsEncoding.Supplement
  );
}

class MxCmp_RM<MxType TYPE, MxOperand MEMOpd, ComplexPattern MEMPat,
               MxEncMemOp MEM_ENC>
    : MxInst<(outs), (ins TYPE.ROp:$reg, MEMOpd:$mem),
             "cmp."#TYPE.Prefix#"\t$mem, $reg",
             [(set CCR, (MxCmp (load MEMPat:$mem), TYPE.ROp:$reg))]> {
  let Inst = (ascend
    (descend 0b1011,
      // REGISTER
      (operand "$reg", 3),
      // OPMODE
      !cast<MxOpModeEncoding>("MxOpMode"#TYPE.Size#"_d_EA").Value,
      MEM_ENC.EA),
    MEM_ENC.Supplement
  );
}
} // let mayLoad = 1

} // let Defs = [CCR]

multiclass MMxCmp_RM<MxType TYPE> {
  def NAME#TYPE.KOp.Letter : MxCmp_RM<TYPE, TYPE.KOp, TYPE.KPat, MxEncAddrMode_k<"mem">>;
  def NAME#TYPE.QOp.Letter : MxCmp_RM<TYPE, TYPE.QOp, TYPE.QPat, MxEncAddrMode_q<"mem">>;
  def NAME#TYPE.POp.Letter : MxCmp_RM<TYPE, TYPE.POp, TYPE.PPat, MxEncAddrMode_p<"mem">>;
  def NAME#TYPE.FOp.Letter : MxCmp_RM<TYPE, TYPE.FOp, TYPE.FPat, MxEncAddrMode_f<"mem">>;
  def NAME#TYPE.JOp.Letter : MxCmp_RM<TYPE, TYPE.JOp, TYPE.JPat, MxEncAddrMode_j<"mem">>;
}

multiclass MMxCmp_MI<MxType TYPE> {
  def NAME#TYPE.KOp.Letter#"i" : MxCmp_MI<TYPE, TYPE.KOp, TYPE.KPat,
                                          MxEncAddrMode_k<"mem">>;
  def NAME#TYPE.QOp.Letter#"i" : MxCmp_MI<TYPE, TYPE.QOp, TYPE.QPat,
                                          MxEncAddrMode_q<"mem">>;
  def NAME#TYPE.POp.Letter#"i" : MxCmp_MI<TYPE, TYPE.POp, TYPE.PPat,
                                          MxEncAddrMode_p<"mem">>;
  def NAME#TYPE.FOp.Letter#"i" : MxCmp_MI<TYPE, TYPE.FOp, TYPE.FPat,
                                          MxEncAddrMode_f<"mem">>;
  def NAME#TYPE.JOp.Letter#"i" : MxCmp_MI<TYPE, TYPE.JOp, TYPE.JPat,
                                          MxEncAddrMode_j<"mem">>;
}

foreach S = [8, 16, 32] in {
  def CMP#S#di : MxCmp_RI<!cast<MxType>("MxType"#S#"d")>;
  def CMP#S#bi : MxCmp_BI<!cast<MxType>("MxType"#S#"d")>;
} // foreach

def CMP8dd : MxCmp_RR<MxType8d>;
foreach S = [16, 32] in {
  def CMP#S#dr : MxCmp_RR<!cast<MxType>("MxType"#S#"r"),
                          !cast<MxType>("MxType"#S#"d")>;
}

// cmp mem, Dn
defm CMP8d  : MMxCmp_RM<MxType8d>;
defm CMP16d : MMxCmp_RM<MxType16d>;
defm CMP32d : MMxCmp_RM<MxType32d>;

// cmp #imm, mem
defm CMP8  : MMxCmp_MI<MxType8d>;
defm CMP16 : MMxCmp_MI<MxType16d>;
defm CMP32 : MMxCmp_MI<MxType32d>;


//===----------------------------------------------------------------------===//
// EXT
//===----------------------------------------------------------------------===//

/// ---------------------------------------------------
///  F  E  D  C  B  A  9 | 8  7  6 | 5  4  3 | 2  1  0
/// ---------------------------------------------------
///  0  1  0  0  1  0  0 |  OPMODE | 0  0  0 |   REG
/// ---------------------------------------------------
let Defs = [CCR] in
let Constraints = "$src = $dst" in
class MxExt<MxType TO, MxType FROM>
    : MxInst<(outs TO.ROp:$dst), (ins TO.ROp:$src),
             "ext."#TO.Prefix#"\t$src", []> {
  let Inst = (descend 0b0100100,
    // OPMODE
    !cond(
      // byte -> word
      !and(!eq(FROM.Size, 8), !eq(TO.Size, 16)): 0b010,
      // word -> long
      !and(!eq(FROM.Size, 16), !eq(TO.Size, 32)): 0b011,
      // byte -> long
      !and(!eq(FROM.Size, 8), !eq(TO.Size, 32)): 0b111
    ),
    0b000,
    // REGISTER
    (operand "$src", 3)
  );
}

def EXT16 : MxExt<MxType16d, MxType8d>;
def EXT32 : MxExt<MxType32d, MxType16d>;

def : Pat<(sext_inreg i16:$src, i8),  (EXT16 $src)>;
def : Pat<(sext_inreg i32:$src, i16), (EXT32 $src)>;
def : Pat<(sext_inreg i32:$src, i8),
          (EXT32 (MOVXd32d16 (EXT16 (EXTRACT_SUBREG $src, MxSubRegIndex16Lo))))>;


//===----------------------------------------------------------------------===//
// DIV/MUL
//===----------------------------------------------------------------------===//

/// Word operation:
/// ----------------------------------------------------
///  F  E  D  C | B  A  9 | 8  7  6 | 5  4  3 | 2  1  0
/// ----------------------------------------------------
///             |         |         | EFFECTIVE ADDRESS
///  x  x  x  x |   REG   | OP MODE |   MODE  |   REG
/// ----------------------------------------------------
let Defs = [CCR] in {
let Constraints = "$src = $dst" in {
// $dreg <- $dreg op $dreg
class MxDiMuOp_DD<string MN, bits<4> CMD, bit SIGNED = false,
                  MxOperand DST, MxOperand OPD>
    : MxInst<(outs DST:$dst), (ins DST:$src, OPD:$opd), MN#"\t$opd, $dst", []> {
  let Inst = (descend CMD,
    // REGISTER
    (operand "$dst", 3),
    !if(SIGNED, 0b111, 0b011),
    /*MODE*/0b000, /*REGISTER*/(operand "$opd", 3)
  );
}

// $dreg <- $dreg op $dreg
class MxDiMuOp_DD_Long<string MN, SDNode NODE, bits<10> CMD, bit SIGNED = false>
    : MxInst<(outs MxDRD32:$dst), (ins MxDRD32:$src, MxDRD32:$opd), MN#"\t$opd, $dst",
             [(set i32:$dst, CCR, (NODE i32:$src, i32:$opd))]> {
  let Inst = (ascend
    (descend CMD,
      /*MODE*/0b000, /*REGISTER*/(operand "$opd", 3)),
    (descend 0b0,
      // REGISTER
      (operand "$dst", 3),
      !if(SIGNED, 0b1, 0b0),
      /*SIZE*/0b0, 0b0000000,
      // Dr REGISTER
      0b000)
  );
}

// $reg <- $reg op $imm
class MxDiMuOp_DI<string MN, bits<4> CMD, bit SIGNED = false,
                  MxOperand DST, MxOperand OPD>
    : MxInst<(outs DST:$dst), (ins DST:$src, OPD:$opd), MN#"\t$opd, $dst", []> {
  // FIXME: Support immediates with different widths.
  defvar ImmEnc = MxEncAddrMode_i<"opd", 16>;
  let Inst = (ascend
    (descend CMD,
      // REGISTER
      (operand "$dst", 3),
      !if(SIGNED, 0b111, 0b011), ImmEnc.EA),
    ImmEnc.Supplement
  );
}
} // let Constraints
} // Defs = [CCR]

multiclass MxDiMuOp<string MN, bits<4> CMD> {
  def "S"#NAME#"d32d16" : MxDiMuOp_DD<MN#"s", CMD, /*SIGNED*/true, MxDRD32, MxDRD16>;
  def "U"#NAME#"d32d16" : MxDiMuOp_DD<MN#"u", CMD, /*SIGNED*/false, MxDRD32, MxDRD16>;

  def "S"#NAME#"d32i16" : MxDiMuOp_DI<MN#"s", CMD, /*SIGNED*/true, MxDRD32, Mxi16imm>;
  def "U"#NAME#"d32i16" : MxDiMuOp_DI<MN#"u", CMD, /*SIGNED*/false, MxDRD32, Mxi16imm>;
}

defm DIV : MxDiMuOp<"div", 0x8>;

def SDIVd32d32 : MxDiMuOp_DD_Long<"divs.l", sdiv, 0x131, /*SIGNED*/true>;
def UDIVd32d32 : MxDiMuOp_DD_Long<"divu.l", udiv, 0x131, /*SIGNED*/false>;

// This is used to cast immediates to 16-bits for operations which don't
// support smaller immediate sizes.
def as_i16imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getSExtValue(), SDLoc(N), MVT::i16);
}]>;

// RR i8
def : Pat<(sdiv i8:$dst, i8:$opd),
          (EXTRACT_SUBREG
            (SDIVd32d16 (MOVSXd32d8 $dst), (MOVSXd16d8 $opd)),
             MxSubRegIndex8Lo)>;

def : Pat<(udiv i8:$dst, i8:$opd),
          (EXTRACT_SUBREG
            (UDIVd32d16 (MOVZXd32d8 $dst), (MOVZXd16d8 $opd)),
             MxSubRegIndex8Lo)>;

def : Pat<(srem i8:$dst, i8:$opd),
          (EXTRACT_SUBREG
            (ASR32di (ASR32di (SDIVd32d16 (MOVSXd32d8 $dst), (MOVSXd16d8 $opd)), 8), 8),
             MxSubRegIndex8Lo)>;

def : Pat<(urem i8:$dst, i8:$opd),
          (EXTRACT_SUBREG
            (LSR32di (LSR32di (UDIVd32d16 (MOVZXd32d8 $dst), (MOVZXd16d8 $opd)), 8), 8),
             MxSubRegIndex8Lo)>;

// RR i16
def : Pat<(sdiv i16:$dst, i16:$opd),
          (EXTRACT_SUBREG
            (SDIVd32d16 (MOVSXd32d16 $dst), $opd),
             MxSubRegIndex16Lo)>;

def : Pat<(udiv i16:$dst, i16:$opd),
          (EXTRACT_SUBREG
            (UDIVd32d16 (MOVZXd32d16 $dst), $opd),
             MxSubRegIndex16Lo)>;

def : Pat<(srem i16:$dst, i16:$opd),
          (EXTRACT_SUBREG
            (ASR32di (ASR32di (SDIVd32d16 (MOVSXd32d16 $dst), $opd), 8), 8),
             MxSubRegIndex16Lo)>;

def : Pat<(urem i16:$dst, i16:$opd),
          (EXTRACT_SUBREG
            (LSR32di (LSR32di (UDIVd32d16 (MOVZXd32d16 $dst), $opd), 8), 8),
             MxSubRegIndex16Lo)>;

// RI i8
def : Pat<(sdiv i8:$dst, Mxi8immSExt8:$opd),
          (EXTRACT_SUBREG
            (SDIVd32i16 (MOVSXd32d8 $dst), (as_i16imm $opd)),
             MxSubRegIndex8Lo)>;

def : Pat<(udiv i8:$dst, Mxi8immSExt8:$opd),
          (EXTRACT_SUBREG
            (UDIVd32i16 (MOVZXd32d8 $dst), (as_i16imm $opd)),
             MxSubRegIndex8Lo)>;

def : Pat<(srem i8:$dst, Mxi8immSExt8:$opd),
          (EXTRACT_SUBREG
            (ASR32di (ASR32di (SDIVd32i16 (MOVSXd32d8 $dst), (as_i16imm $opd)), 8), 8),
             MxSubRegIndex8Lo)>;

def : Pat<(urem i8:$dst, Mxi8immSExt8:$opd),
          (EXTRACT_SUBREG
            (LSR32di (LSR32di (UDIVd32i16 (MOVZXd32d8 $dst), (as_i16imm $opd)), 8), 8),
             MxSubRegIndex8Lo)>;

// RI i16
def : Pat<(sdiv i16:$dst, Mxi16immSExt16:$opd),
          (EXTRACT_SUBREG
            (SDIVd32i16 (MOVSXd32d16 $dst), imm:$opd),
             MxSubRegIndex16Lo)>;

def : Pat<(udiv i16:$dst, Mxi16immSExt16:$opd),
          (EXTRACT_SUBREG
            (UDIVd32i16 (MOVZXd32d16 $dst), imm:$opd),
             MxSubRegIndex16Lo)>;

def : Pat<(srem i16:$dst, Mxi16immSExt16:$opd),
          (EXTRACT_SUBREG
            (ASR32di (ASR32di (SDIVd32i16 (MOVSXd32d16 $dst), imm:$opd), 8), 8),
             MxSubRegIndex16Lo)>;

def : Pat<(urem i16:$dst, Mxi16immSExt16:$opd),
          (EXTRACT_SUBREG
            (LSR32di (LSR32di (UDIVd32i16 (MOVZXd32d16 $dst), imm:$opd), 8), 8),
             MxSubRegIndex16Lo)>;


defm MUL : MxDiMuOp<"mul", 0xC>;

def SMULd32d32 : MxDiMuOp_DD_Long<"muls.l", MxSMul, 0x130, /*SIGNED*/true>;
def UMULd32d32 : MxDiMuOp_DD_Long<"mulu.l", MxUMul, 0x130, /*SIGNED*/false>;

// RR
def : Pat<(mul i16:$dst, i16:$opd),
          (EXTRACT_SUBREG
            (SMULd32d16 (MOVXd32d16 $dst), $opd),
             MxSubRegIndex16Lo)>;

def : Pat<(mulhs i16:$dst, i16:$opd),
          (EXTRACT_SUBREG
            (ASR32di (ASR32di (SMULd32d16 (MOVXd32d16 $dst), $opd), 8), 8),
             MxSubRegIndex16Lo)>;

def : Pat<(mulhu i16:$dst, i16:$opd),
          (EXTRACT_SUBREG
            (LSR32di (LSR32di (UMULd32d16 (MOVXd32d16 $dst), $opd), 8), 8),
             MxSubRegIndex16Lo)>;

def : Pat<(mul i32:$dst, i32:$opd), (SMULd32d32 $dst, $opd)>;


// RI
def : Pat<(mul i16:$dst, Mxi16immSExt16:$opd),
          (EXTRACT_SUBREG
            (SMULd32i16 (MOVXd32d16 $dst), imm:$opd),
             MxSubRegIndex16Lo)>;

def : Pat<(mulhs i16:$dst, Mxi16immSExt16:$opd),
          (EXTRACT_SUBREG
            (ASR32di (ASR32di (SMULd32i16 (MOVXd32d16 $dst), imm:$opd), 8), 8),
             MxSubRegIndex16Lo)>;

def : Pat<(mulhu i16:$dst, Mxi16immSExt16:$opd),
          (EXTRACT_SUBREG
            (LSR32di (LSR32di (UMULd32i16 (MOVXd32d16 $dst), imm:$opd), 8), 8),
             MxSubRegIndex16Lo)>;


//===----------------------------------------------------------------------===//
// NEG/NEGX/NOT
//===----------------------------------------------------------------------===//

/// ------------+------------+------+---------+---------
///  F  E  D  C | B  A  9  8 | 7  6 | 5  4  3 | 2  1  0
/// ------------+------------+------+-------------------
///             |            |      | EFFECTIVE ADDRESS
///  0  1  0  0 | x  x  x  x | SIZE |   MODE  |   REG
/// ------------+------------+------+---------+---------
let Defs = [CCR] in {
let Constraints = "$src = $dst" in {

class MxNeg_D<MxType TYPE>
    : MxInst<(outs TYPE.ROp:$dst), (ins TYPE.ROp:$src),
             "neg."#TYPE.Prefix#"\t$dst",
             [(set TYPE.VT:$dst, (ineg TYPE.VT:$src))]> {
  let Inst = (descend 0b01000100,
    /*SIZE*/!cast<MxEncSize>("MxEncSize"#TYPE.Size).Value,
    //MODE without last bit
    0b00,
    //REGISTER prefixed by D/A bit
    (operand "$dst", 4)
  );
}

let Uses = [CCR] in {
class MxNegX_D<MxType TYPE>
    : MxInst<(outs TYPE.ROp:$dst), (ins TYPE.ROp:$src),
             "negx."#TYPE.Prefix#"\t$dst",
             [(set TYPE.VT:$dst, (MxSubX 0, TYPE.VT:$src, CCR))]> {
  let Inst = (descend 0b01000000,
    /*SIZE*/!cast<MxEncSize>("MxEncSize"#TYPE.Size).Value,
    //MODE without last bit
    0b00,
    //REGISTER prefixed by D/A bit
    (operand "$dst", 4)
  );
}
}

class MxNot_D<MxType TYPE>
    : MxInst<(outs TYPE.ROp:$dst), (ins TYPE.ROp:$src),
             "not."#TYPE.Prefix#"\t$dst",
             [(set TYPE.VT:$dst, (not TYPE.VT:$src))]> {
  let Inst = (descend 0b01000110,
    /*SIZE*/!cast<MxEncSize>("MxEncSize"#TYPE.Size).Value,
    //MODE without last bit
    0b00,
    //REGISTER prefixed by D/A bit
    (operand "$dst", 4)
  );
}

} // let Constraints
} // let Defs = [CCR]

foreach S = [8, 16, 32] in {
  def NEG#S#d  : MxNeg_D<!cast<MxType>("MxType"#S#"d")>;
  def NEGX#S#d : MxNegX_D<!cast<MxType>("MxType"#S#"d")>;
  def NOT#S#d  : MxNot_D<!cast<MxType>("MxType"#S#"d")>;
}

def : Pat<(MxSub 0, i8 :$src), (NEG8d  MxDRD8 :$src)>;
def : Pat<(MxSub 0, i16:$src), (NEG16d MxDRD16:$src)>;
def : Pat<(MxSub 0, i32:$src), (NEG32d MxDRD32:$src)>;
// SExt of i1 values.
// Although we specify `ZeroOrOneBooleanContent` for boolean content,
// we're still adding an AND here as we don't know the origin of the i1 value.
def : Pat<(sext_inreg i8:$src,  i1), (NEG8d  (AND8di  MxDRD8:$src,  1))>;
def : Pat<(sext_inreg i16:$src, i1), (NEG16d (AND16di MxDRD16:$src, 1))>;
def : Pat<(sext_inreg i32:$src, i1), (NEG32d (AND32di MxDRD32:$src, 1))>;

//===----------------------------------------------------------------------===//
// no-CCR Patterns
//===----------------------------------------------------------------------===//

/// Basically the reason for this stuff is that add and addc share the same
/// operand types constraints for whatever reasons and I had to define a common
/// MxAdd and MxSub instructions that produce CCR and then pattern-map add and addc
/// to it.
/// NOTE On the other hand I see no reason why I cannot just drop explicit CCR
/// result. Anyway works for now, hopefully I will better understand how this stuff
/// is designed later
foreach N = ["add", "addc"] in {

  // add reg, reg
  def : Pat<(!cast<SDNode>(N) i8 :$src, i8 :$opd),
            (ADD8dd  MxDRD8 :$src, MxDRD8 :$opd)>;
  def : Pat<(!cast<SDNode>(N) i16:$src, i16:$opd),
            (ADD16dr MxXRD16:$src, MxDRD16:$opd)>;
  def : Pat<(!cast<SDNode>(N) i32:$src, i32:$opd),
            (ADD32dr MxXRD32:$src, MxDRD32:$opd)>;

  // add (An), reg
  def : Pat<(!cast<SDNode>(N) MxType8.VT:$src, (Mxloadi8 MxType8.JPat:$opd)),
            (ADD8dj MxDRD8:$src, MxType8.JOp:$opd)>;
  def : Pat<(!cast<SDNode>(N) MxType16.VT:$src, (Mxloadi16 MxType16.JPat:$opd)),
            (ADD16dj MxDRD16:$src, MxType16.JOp:$opd)>;
  def : Pat<(!cast<SDNode>(N) MxType32.VT:$src, (Mxloadi32 MxType32.JPat:$opd)),
            (ADD32dj MxDRD32:$src, MxType32.JOp:$opd)>;

  // add (i,An), reg
  def : Pat<(!cast<SDNode>(N) MxType8.VT:$src, (Mxloadi8 MxType8.PPat:$opd)),
            (ADD8dp MxDRD8:$src, MxType8.POp:$opd)>;
  def : Pat<(!cast<SDNode>(N) MxType16.VT:$src, (Mxloadi16 MxType16.PPat:$opd)),
            (ADD16dp MxDRD16:$src, MxType16.POp:$opd)>;
  def : Pat<(!cast<SDNode>(N) MxType32.VT:$src, (Mxloadi32 MxType32.PPat:$opd)),
            (ADD32dp MxDRD32:$src, MxType32.POp:$opd)>;

  // add (i,An,Xn), reg
  def : Pat<(!cast<SDNode>(N) MxType8.VT:$src, (Mxloadi8 MxType8.FPat:$opd)),
            (ADD8df MxDRD8:$src, MxType8.FOp:$opd)>;
  def : Pat<(!cast<SDNode>(N) MxType16.VT:$src, (Mxloadi16 MxType16.FPat:$opd)),
            (ADD16df MxDRD16:$src, MxType16.FOp:$opd)>;
  def : Pat<(!cast<SDNode>(N) MxType32.VT:$src, (Mxloadi32 MxType32.FPat:$opd)),
            (ADD32df MxDRD32:$src, MxType32.FOp:$opd)>;

  // add reg, imm
  def : Pat<(!cast<SDNode>(N) i8: $src, Mxi8immSExt8:$opd),
            (ADD8di  MxDRD8 :$src, imm:$opd)>;
  def : Pat<(!cast<SDNode>(N) i16:$src, Mxi16immSExt16:$opd),
            (ADD16di MxDRD16:$src, imm:$opd)>;

  // LEAp is more complex and thus will be selected over normal ADD32ri but it cannot
  // be used with data registers, here by adding complexity to a simple ADD32ri insts
  // we make sure it will be selected over LEAp
  let AddedComplexity = 15 in {
  def : Pat<(!cast<SDNode>(N) i32:$src, Mxi32immSExt32:$opd),
            (ADD32di MxDRD32:$src, imm:$opd)>;
  } // AddedComplexity = 15

  // add imm, (An)
  def : Pat<(store (!cast<SDNode>(N) (load MxType8.JPat:$dst), MxType8.IPat:$opd),
                   MxType8.JPat:$dst),
            (ADD8ji MxType8.JOp:$dst, imm:$opd)>;
  def : Pat<(store (!cast<SDNode>(N) (load MxType16.JPat:$dst), MxType16.IPat:$opd),
                   MxType16.JPat:$dst),
            (ADD16ji MxType16.JOp:$dst, imm:$opd)>;
  def : Pat<(store (!cast<SDNode>(N) (load MxType32.JPat:$dst), MxType32.IPat:$opd),
                   MxType32.JPat:$dst),
            (ADD32ji MxType32.JOp:$dst, imm:$opd)>;

} // foreach add, addc

def : Pat<(adde i8 :$src, i8 :$opd), (ADDX8dd  MxDRD8 :$src, MxDRD8 :$opd)>;
def : Pat<(adde i16:$src, i16:$opd), (ADDX16dd MxDRD16:$src, MxDRD16:$opd)>;
def : Pat<(adde i32:$src, i32:$opd), (ADDX32dd MxDRD32:$src, MxDRD32:$opd)>;



foreach N = ["sub", "subc"] in {

  // sub reg, reg
  def : Pat<(!cast<SDNode>(N) i8 :$src, i8 :$opd),
            (SUB8dd  MxDRD8 :$src, MxDRD8 :$opd)>;
  def : Pat<(!cast<SDNode>(N) i16:$src, i16:$opd),
            (SUB16dd MxDRD16:$src, MxDRD16:$opd)>;
  def : Pat<(!cast<SDNode>(N) i32:$src, i32:$opd),
            (SUB32dd MxDRD32:$src, MxDRD32:$opd)>;


  // sub (An), reg
  def : Pat<(!cast<SDNode>(N) MxType8.VT:$src, (Mxloadi8 MxType8.JPat:$opd)),
            (SUB8dj MxDRD8:$src, MxType8.JOp:$opd)>;
  def : Pat<(!cast<SDNode>(N) MxType16.VT:$src, (Mxloadi16 MxType16.JPat:$opd)),
            (SUB16dj MxDRD16:$src, MxType16.JOp:$opd)>;
  def : Pat<(!cast<SDNode>(N) MxType32.VT:$src, (Mxloadi32 MxType32.JPat:$opd)),
            (SUB32dj MxDRD32:$src, MxType32.JOp:$opd)>;

  // sub (i,An), reg
  def : Pat<(!cast<SDNode>(N) MxType8.VT:$src, (Mxloadi8 MxType8.PPat:$opd)),
            (SUB8dp MxDRD8:$src, MxType8.POp:$opd)>;
  def : Pat<(!cast<SDNode>(N) MxType16.VT:$src, (Mxloadi16 MxType16.PPat:$opd)),
            (SUB16dp MxDRD16:$src, MxType16.POp:$opd)>;
  def : Pat<(!cast<SDNode>(N) MxType32.VT:$src, (Mxloadi32 MxType32.PPat:$opd)),
            (SUB32dp MxDRD32:$src, MxType32.POp:$opd)>;

  // sub (i,An,Xn), reg
  def : Pat<(!cast<SDNode>(N) MxType8.VT:$src, (Mxloadi8 MxType8.FPat:$opd)),
            (SUB8df MxDRD8:$src, MxType8.FOp:$opd)>;
  def : Pat<(!cast<SDNode>(N) MxType16.VT:$src, (Mxloadi16 MxType16.FPat:$opd)),
            (SUB16df MxDRD16:$src, MxType16.FOp:$opd)>;
  def : Pat<(!cast<SDNode>(N) MxType32.VT:$src, (Mxloadi32 MxType32.FPat:$opd)),
            (SUB32df MxDRD32:$src, MxType32.FOp:$opd)>;

  // sub reg, imm
  def : Pat<(!cast<SDNode>(N) i8 :$src, Mxi8immSExt8 :$opd),
            (SUB8di  MxDRD8 :$src, imm:$opd)>;
  def : Pat<(!cast<SDNode>(N) i16:$src, Mxi16immSExt16:$opd),
            (SUB16di MxDRD16:$src, imm:$opd)>;
  def : Pat<(!cast<SDNode>(N) i32:$src, Mxi32immSExt32:$opd),
            (SUB32di MxDRD32:$src, imm:$opd)>;

  // sub imm, (An)
  def : Pat<(store (!cast<SDNode>(N) (load MxType8.JPat:$dst), MxType8.IPat:$opd),
                   MxType8.JPat:$dst),
            (SUB8ji MxType8.JOp:$dst, imm:$opd)>;
  def : Pat<(store (!cast<SDNode>(N) (load MxType16.JPat:$dst), MxType16.IPat:$opd),
                   MxType16.JPat:$dst),
            (SUB16ji MxType16.JOp:$dst, imm:$opd)>;
  def : Pat<(store (!cast<SDNode>(N) (load MxType32.JPat:$dst), MxType32.IPat:$opd),
                   MxType32.JPat:$dst),
            (SUB32ji MxType32.JOp:$dst, imm:$opd)>;

} // foreach sub, subx

def : Pat<(sube i8 :$src, i8 :$opd), (SUBX8dd  MxDRD8 :$src, MxDRD8 :$opd)>;
def : Pat<(sube i16:$src, i16:$opd), (SUBX16dd MxDRD16:$src, MxDRD16:$opd)>;
def : Pat<(sube i32:$src, i32:$opd), (SUBX32dd MxDRD32:$src, MxDRD32:$opd)>;

multiclass BitwisePat<string INST, SDNode OP> {
  // op reg, reg
  def : Pat<(OP i8 :$src, i8 :$opd),
            (!cast<MxInst>(INST#"8dd")  MxDRD8 :$src, MxDRD8 :$opd)>;
  def : Pat<(OP i16:$src, i16:$opd),
            (!cast<MxInst>(INST#"16dd") MxDRD16:$src, MxDRD16:$opd)>;
  def : Pat<(OP i32:$src, i32:$opd),
            (!cast<MxInst>(INST#"32dd") MxDRD32:$src, MxDRD32:$opd)>;
  // op reg, imm
  def : Pat<(OP i8: $src, Mxi8immSExt8 :$opd),
            (!cast<MxInst>(INST#"8di")  MxDRD8 :$src, imm:$opd)>;
  def : Pat<(OP i16:$src, Mxi16immSExt16:$opd),
            (!cast<MxInst>(INST#"16di") MxDRD16:$src, imm:$opd)>;
  def : Pat<(OP i32:$src, Mxi32immSExt32:$opd),
            (!cast<MxInst>(INST#"32di") MxDRD32:$src, imm:$opd)>;
}

defm : BitwisePat<"AND", and>;
defm : BitwisePat<"OR",  or>;
defm : BitwisePat<"XOR", xor>;

//===----------------------------------------------------------------------===//
// Floating point arithmetic instruction
//===----------------------------------------------------------------------===//

let Defs = [FPS] in
class MxFArithBase_FF<dag outs, dag ins, string asm, string rounding,
                      list<dag> patterns>
    : MxInst<outs, ins, asm, patterns> {
  let Uses = !if(!eq(rounding, ""), [FPC], []);

  let Predicates = !if(!eq(rounding, ""), [AtLeastM68881], [AtLeastM68040]);
}

class MxFPOpModeSelector<string rounding, bits<7> single, bits<7> double,
                         bits<7> extended> {
  bits<7> Mode = !cond(!eq(rounding, "s"): single,
                       !eq(rounding, "d"): double,
                       !eq(rounding, ""): extended);
}

//===----------------------------------------------------------------------===//
// Unary floating point instruction
//===----------------------------------------------------------------------===//

class MxFUnary_FF<MxOpBundle Opnd, string rounding,
                  string mnemonic, bits<7> opmode>
    : MxFArithBase_FF<(outs Opnd.Op:$dst), (ins Opnd.Op:$src),
                    "f"#rounding#mnemonic#".x\t$src, $dst", rounding, [(null_frag)]> {
  let Inst = (ascend
  (descend 0b1111,
    /*COPROCESSOR ID*/0b001,
    0b000,
    /*MODE+REGISTER*/0b000000),
  (descend 0b0, /* R/M */ 0b0, 0b0,
    /*SOURCE SPECIFIER*/
    (operand "$src", 3),
    /*DESTINATION*/
    (operand "$dst", 3),
    /*OPMODE*/
    opmode)
  );
}

multiclass MxFUnaryOp<string mnemonic, bits<7> single, bits<7> double,
                      bits<7> extended> {
  foreach rounding = ["", "s", "d"] in {
    defvar opmode = MxFPOpModeSelector<rounding, single, double, extended>.Mode;

    def F # !toupper(rounding) # !substr(NAME, 1) # "80fp_fp"
      : MxFUnary_FF<MxOp80AddrMode_fpr, rounding, mnemonic, opmode>;
  
    let isCodeGenOnly = 1 in
    foreach size = [32, 64] in
      def F # !toupper(rounding) # !substr(NAME, 1) # size # "fp_fp"
        : MxFUnary_FF<!cast<MxOpBundle>("MxOp"#size#"AddrMode_fpr"),
                      rounding, mnemonic, opmode>;
  }
}

defm FABS : MxFUnaryOp<"abs", 0b1011000, 0b1011100, 0b0011000>;
defm FNEG : MxFUnaryOp<"neg", 0b1011010, 0b1011110, 0b0011010>;

//===----------------------------------------------------------------------===//
// Binary floating point instruction
//===----------------------------------------------------------------------===//

let Constraints = "$src = $dst" in
class MxFBinary_FF<MxOpBundle Opnd, string rounding,
                   string mnemonic, bits<7> opmode>
    : MxFArithBase_FF<(outs Opnd.Op:$dst), (ins Opnd.Op:$src, Opnd.Op:$opd),
                      "f"#rounding#mnemonic#".x\t$opd, $dst", rounding, [(null_frag)]> {
  let Inst = (ascend
  (descend 0b1111,
    /*COPROCESSOR ID*/0b001,
    0b000,
    /*MODE+REGISTER*/0b000000),
  (descend 0b0, /* R/M */ 0b0, 0b0,
    /*SOURCE SPECIFIER*/
    (operand "$opd", 3),
    /*DESTINATION*/
    (operand "$dst", 3),
    /*OPMODE*/
    opmode)
  );
}

multiclass MxFBinaryOp<string mnemonic, bits<7> single, bits<7> double,
                      bits<7> extended> {
  foreach rounding = ["", "s", "d"] in {
    defvar opmode = MxFPOpModeSelector<rounding, single, double, extended>.Mode;

    def F # !toupper(rounding) # !substr(NAME, 1) # "80fp_fp"
      : MxFBinary_FF<MxOp80AddrMode_fpr, rounding, mnemonic, opmode>;

    let isCodeGenOnly = 1 in
    foreach size = [32, 64] in
      def F # !toupper(rounding) # !substr(NAME, 1) # size # "fp_fp"
        : MxFBinary_FF<!cast<MxOpBundle>("MxOp"#size#"AddrMode_fpr"),
                        rounding, mnemonic, opmode>;
  }
}

defm FADD : MxFBinaryOp<"add", 0b1100010, 0b1100110, 0b0100010>;
defm FSUB : MxFBinaryOp<"sub", 0b1101000, 0b1101100, 0b0101000>;
defm FMUL : MxFBinaryOp<"mul", 0b1100011, 0b1100111, 0b0100011>;
defm FDIV : MxFBinaryOp<"div", 0b1100000, 0b1100100, 0b0100000>;
