//===-- M68kInstrData.td - M68k Data Movement Instructions -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes the Motorola 680x0 data movement instructions which are
/// the basic means of transferring and storing addresses and data. Here is the
/// current status of the file:
///
///  Machine:
///
///     EXG   [ ]     FMOVE [ ]     FSMOVE [ ]     FDMOVE [ ]     FMOVEM [ ]
///     LEA   [~]     PEA   [ ]     MOVE   [~]     MOVE16 [ ]     MOVEA  [ ]
///     MOVEM [ ]     MOVEP [ ]     MOVEQ  [ ]     LINK   [~]     UNLK   [~]
///
///  Pseudo:
///
///     MOVI  [x]     MOVSX [x]     MOVZX [x]     MOVX   [x]
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
// MOVE
//===----------------------------------------------------------------------===//

/// -----------------------------------------------------
///  F  E | D  C | B  A  9 | 8  7  6 | 5  4  3 | 2  1  0
/// -----------------------------------------------------
///       |      |    DESTINATION    |       SOURCE
///  0  0 | SIZE |   REG   |   MODE  |   MODE  |   REG
/// -----------------------------------------------------
///
/// NOTE Move requires EA X version for direct register destination(0)

// MOVE has a different size encoding.
class MxMoveSize<bits<2> value> {
  bits<2> Value = value;
}
def MxMoveSize8  : MxMoveSize<0b01>;
def MxMoveSize16 : MxMoveSize<0b11>;
def MxMoveSize32 : MxMoveSize<0b10>;

class MxMoveEncoding<MxMoveSize size, MxEncMemOp dst_enc, MxEncMemOp src_enc> {
  dag Value = (ascend
    (descend 0b00, size.Value,
             !cond(
               !eq(!getdagop(dst_enc.EA), descend): !setdagop(dst_enc.EA, ascend),
               !eq(!getdagop(dst_enc.EA), ascend): !setdagop(dst_enc.EA, descend)),
             src_enc.EA),
    // Source extension
    src_enc.Supplement,
    // Destination extension
    dst_enc.Supplement
  );
}

// Special encoding for Xn
class MxMoveEncAddrMode_r<string reg_opnd> : MxEncMemOp {
  let EA = (descend (descend 0b00, (slice "$"#reg_opnd, 3, 3)),
                    (operand "$"#reg_opnd, 3));
}

// TODO: Generalize and adopt this utility in other .td files as well.
multiclass MxMoveOperandEncodings<string opnd_name> {
  // Dn
  def MxMove#NAME#OpEnc_d : MxEncAddrMode_d<opnd_name>;
  // An
  def MxMove#NAME#OpEnc_a : MxEncAddrMode_a<opnd_name>;
  // Xn
  def MxMove#NAME#OpEnc_r : MxMoveEncAddrMode_r<opnd_name>;
  // (An)+
  def MxMove#NAME#OpEnc_o : MxEncAddrMode_o<opnd_name>;
  // -(An)
  def MxMove#NAME#OpEnc_e : MxEncAddrMode_e<opnd_name>;
  // (i,PC,Xn)
  def MxMove#NAME#OpEnc_k : MxEncAddrMode_k<opnd_name>;
  // (i,PC)
  def MxMove#NAME#OpEnc_q : MxEncAddrMode_q<opnd_name>;
  // (i,An,Xn)
  def MxMove#NAME#OpEnc_f : MxEncAddrMode_f<opnd_name>;
  // (i,An)
  def MxMove#NAME#OpEnc_p : MxEncAddrMode_p<opnd_name>;
  // (ABS).L
  def MxMove#NAME#OpEnc_b : MxEncAddrMode_abs<opnd_name, /*W/L=*/true>;
  // (An)
  def MxMove#NAME#OpEnc_j : MxEncAddrMode_j<opnd_name>;
}

defm Src : MxMoveOperandEncodings<"src">;
defm Dst : MxMoveOperandEncodings<"dst">;

defvar MxMoveSupportedAMs = ["o", "e", "k", "q", "f", "p", "b", "j"];

let Defs = [CCR] in
class MxMove<string size, dag outs, dag ins, list<dag> pattern, MxMoveEncoding enc>
    : MxInst<outs, ins, "move."#size#"\t$src, $dst", pattern> {
  let Inst = enc.Value;
}

// R <- R
class MxMove_RR<MxType TYPE, string DST_REG, string SRC_REG,
                MxMoveEncoding ENC,
                MxOpBundle DST = !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#DST_REG),
                MxOpBundle SRC = !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#SRC_REG)>
    : MxMove<TYPE.Prefix,
             (outs DST.Op:$dst), (ins SRC.Op:$src),
             [(null_frag)], ENC>;

foreach DST_REG = ["r", "a"] in {
  foreach SRC_REG = ["r", "a"] in
  foreach TYPE = [MxType16, MxType32] in
  def MOV # TYPE.Size # DST_REG # SRC_REG # TYPE.Postfix
      : MxMove_RR<TYPE, DST_REG, SRC_REG,
                  MxMoveEncoding<!cast<MxMoveSize>("MxMoveSize"#TYPE.Size),
                                 !cast<MxEncMemOp>("MxMoveDstOpEnc_"#DST_REG),
                                 !cast<MxEncMemOp>("MxMoveSrcOpEnc_"#SRC_REG)>>;
} // foreach DST_REG
foreach TYPE = [MxType8, MxType16, MxType32] in
def MOV # TYPE.Size # dd # TYPE.Postfix
    : MxMove_RR<TYPE, "d", "d",
                MxMoveEncoding<!cast<MxMoveSize>("MxMoveSize"#TYPE.Size),
                               MxMoveDstOpEnc_d, MxMoveSrcOpEnc_d>>;

// M <- R
let mayStore = 1 in {
class MxMove_MR<MxType TYPE, MxOpBundle DST, string SRC_REG, MxMoveEncoding ENC,
                MxOpBundle SRC = !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#SRC_REG)>
    : MxMove<TYPE.Prefix, (outs), (ins DST.Op:$dst, SRC.Op:$src),
             [(store TYPE.VT:$src, DST.Pat:$dst)], ENC>;

class MxMove_MI<MxType TYPE, MxOpBundle DST, MxMoveEncoding ENC,
                MxImmOpBundle SRC = !cast<MxImmOpBundle>("MxOp"#TYPE.Size#"AddrMode_i")>
    : MxMove<TYPE.Prefix, (outs), (ins DST.Op:$dst, SRC.Op:$src),
             [(store SRC.ImmPat:$src, DST.Pat:$dst)], ENC>;
} // let mayStore = 1

foreach REG = ["r", "a", "d"] in
foreach AM = MxMoveSupportedAMs in {
  foreach TYPE = !if(!eq(REG, "d"), [MxType8, MxType16, MxType32], [MxType16, MxType32]) in
  def MOV # TYPE.Size # AM # REG # TYPE.Postfix
      : MxMove_MR<TYPE, !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#AM), REG,
                  MxMoveEncoding<!cast<MxMoveSize>("MxMoveSize"#TYPE.Size),
                                 !cast<MxEncMemOp>("MxMoveDstOpEnc_"#AM),
                                 !cast<MxEncMemOp>("MxMoveSrcOpEnc_"#REG)>>;
} // foreach AM

foreach AM = MxMoveSupportedAMs in {
  foreach TYPE = [MxType8, MxType16, MxType32] in
  def MOV # TYPE.Size # AM # i # TYPE.Postfix
      : MxMove_MI<TYPE, !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#AM),
                  MxMoveEncoding<!cast<MxMoveSize>("MxMoveSize"#TYPE.Size),
                                 !cast<MxEncMemOp>("MxMoveDstOpEnc_"#AM),
                                 MxEncAddrMode_i<"src", TYPE.Size>>>;
} // foreach AM

// R <- I
// No pattern, as all immediate -> register moves are matched to the MOVI pseudo
class MxMove_RI<MxType TYPE, string DST_REG, MxMoveEncoding ENC,
                MxImmOpBundle SRC = !cast<MxImmOpBundle>("MxOp"#TYPE.Size#"AddrMode_i"),
                MxOpBundle DST = !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#DST_REG)>
    : MxMove<TYPE.Prefix, (outs DST.Op:$dst), (ins SRC.Op:$src),
              [(null_frag)], ENC>;

foreach REG = ["r", "a", "d"] in {
  foreach TYPE = !if(!eq(REG, "d"), [MxType8, MxType16, MxType32], [MxType16, MxType32]) in
  def MOV # TYPE.Size # REG # i # TYPE.Postfix
      : MxMove_RI<TYPE, REG,
                  MxMoveEncoding<!cast<MxMoveSize>("MxMoveSize"#TYPE.Size),
                                 !cast<MxEncMemOp>("MxMoveDstOpEnc_"#REG),
                                 MxEncAddrMode_i<"src", TYPE.Size>>>;
} // foreach REG

// R <- M
let mayLoad = 1 in
class MxMove_RM<MxType TYPE, string DST_REG, MxOpBundle SRC, MxEncMemOp SRC_ENC,
                MxMoveSize SIZE_ENC = !cast<MxMoveSize>("MxMoveSize"#TYPE.Size),
                MxOpBundle DST = !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#DST_REG),
                MxEncMemOp DST_ENC = !cast<MxEncMemOp>("MxMoveDstOpEnc_"#DST_REG)>
    : MxMove<TYPE.Prefix, (outs DST.Op:$dst), (ins SRC.Op:$src),
             [(set TYPE.VT:$dst, (TYPE.Load SRC.Pat:$src))],
             MxMoveEncoding<SIZE_ENC, DST_ENC, SRC_ENC>>;

foreach REG = ["r", "a", "d"] in
foreach AM = MxMoveSupportedAMs in {
  foreach TYPE = !if(!eq(REG, "d"), [MxType8, MxType16, MxType32], [MxType16, MxType32]) in
  def MOV # TYPE.Size # REG # AM # TYPE.Postfix
      : MxMove_RM<TYPE, REG, !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#AM),
                  !cast<MxEncMemOp>("MxMoveSrcOpEnc_"#AM)>;
} // foreach AM

// Tail call version
let Pattern = [(null_frag)] in {
  foreach REG = ["r", "a"] in
  foreach AM = MxMoveSupportedAMs in {
    foreach TYPE = [MxType16, MxType32] in
    def MOV # TYPE.Size # REG # AM # _TC
        : MxMove_RM<TYPE, REG, !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#AM),
                    !cast<MxEncMemOp>("MxMoveSrcOpEnc_"#AM)> {
      let isCodeGenOnly = true;
    }
  } // foreach AM
} // let Pattern

let mayLoad = 1, mayStore = 1 in
class MxMove_MM<MxType TYPE, MxOpBundle DST, MxOpBundle SRC,
                MxEncMemOp DST_ENC, MxEncMemOp SRC_ENC>
    : MxMove<TYPE.Prefix, (outs), (ins DST.Op:$dst, SRC.Op:$src),
             [(store (TYPE.Load SRC.Pat:$src), DST.Pat:$dst)],
             MxMoveEncoding<!cast<MxMoveSize>("MxMoveSize"#TYPE.Size),
                            DST_ENC, SRC_ENC>>;

foreach DST_AM = MxMoveSupportedAMs in
foreach SRC_AM = MxMoveSupportedAMs in {
  foreach TYPE = [MxType8, MxType16, MxType32] in
  def MOV # TYPE.Size # DST_AM # SRC_AM # TYPE.Postfix
      : MxMove_MM<TYPE, !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#DST_AM),
                  !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#SRC_AM),
                  !cast<MxEncMemOp>("MxMoveDstOpEnc_"#DST_AM),
                  !cast<MxEncMemOp>("MxMoveSrcOpEnc_"#SRC_AM)>;
} // foreach SRC_AM

// Store ABS(basically pointer) as Immdiate to Mem
def : Pat<(store   MxType32.BPat :$src, MxType32.PPat :$dst),
          (MOV32pi MxType32.POp  :$dst, MxType32.IOp  :$src)>;

def : Pat<(store   MxType32.BPat :$src, MxType32.FPat :$dst),
          (MOV32fi MxType32.FOp  :$dst, MxType32.IOp  :$src)>;

def : Pat<(store   MxType32.BPat :$src, MxType32.BPat :$dst),
          (MOV32bi MxType32.BOp  :$dst, MxType32.IOp  :$src)>;

def : Pat<(store   MxType32.BPat :$src, MxType32.JPat :$dst),
          (MOV32ji MxType32.JOp  :$dst, MxType32.IOp  :$src)>;

//===----------------------------------------------------------------------===//
// MOVEQ
//===----------------------------------------------------------------------===//

/// ------------+---------+---+-----------------------
///  F  E  D  C | B  A  9 | 8 | 7  6  5  4  3  2  1  0
/// ------------+---------+---+-----------------------
///  0  1  1  1 |   REG   | 0 |          DATA
/// ------------+---------+---+-----------------------

// No pattern, as all immediate -> register moves are matched to the MOVI pseudo
let Defs = [CCR] in
def MOVQ : MxInst<(outs MxDRD32:$dst), (ins Mxi8imm:$imm),
                  "moveq\t$imm, $dst",
                  [(null_frag)]> {
  let Inst = (descend 0b0111, (operand "$dst", 3), 0b0, (operand "$imm", 8));
}

//===----------------------------------------------------------------------===//
// MOVEM
//
// The mask is already pre-processed by the save/restore spill hook
//===----------------------------------------------------------------------===//

// Direction
defvar MxMOVEM_MR = false;
defvar MxMOVEM_RM = true;

// Size
defvar MxMOVEM_W = false;
defvar MxMOVEM_L = true;

/// ---------------+-------------+-------------+---------
///  F  E  D  C  B | A | 9  8  7 | 6 | 5  4  3 | 2  1  0
/// ---------------+---+---------+---+---------+---------
///  0  1  0  0  1 | D | 0  0  1 | S |   MODE  |   REG
/// ---------------+---+---------+---+---------+---------
///                  REGISTER LIST MASK
/// -----------------------------------------------------
/// D - direction(RM,MR)
/// S - size(W,L)
class MxMOVEMEncoding<MxEncMemOp opnd_enc, bit size, bit direction,
                      string mask_op_name> {
  dag Value = (ascend
    (descend 0b01001, direction, 0b001, size, opnd_enc.EA),
    // Mask
    (operand "$"#mask_op_name, 16),
    opnd_enc.Supplement
  );
}

let mayStore = 1 in
class MxMOVEM_MR<MxType TYPE, bit SIZE_ENC,
                 MxOperand MEMOp, MxEncMemOp MEM_ENC>
    : MxInst<(outs), (ins MEMOp:$dst, MxMoveMask:$mask),
             "movem."#TYPE.Prefix#"\t$mask, $dst", []> {
  let Inst = MxMOVEMEncoding<MEM_ENC, SIZE_ENC, MxMOVEM_MR, "mask">.Value;
}

foreach AM = MxMoveSupportedAMs in {
  foreach TYPE = [MxType16, MxType32] in
  def MOVM # TYPE.Size # AM # m # TYPE.Postfix
      : MxMOVEM_MR<TYPE, !if(!eq(TYPE, MxType16), MxMOVEM_W, MxMOVEM_L),
                   !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#AM).Op,
                   !cast<MxEncMemOp>("MxMoveDstOpEnc_"#AM)>;
} // foreach AM

let mayLoad = 1 in
class MxMOVEM_RM<MxType TYPE, bit SIZE_ENC,
                 MxOperand MEMOp, MxEncMemOp MEM_ENC>
    : MxInst<(outs), (ins MxMoveMask:$mask, MEMOp:$src),
             "movem."#TYPE.Prefix#"\t$src, $mask", []> {
  let Inst = MxMOVEMEncoding<MEM_ENC, SIZE_ENC, MxMOVEM_RM, "mask">.Value;
}

foreach AM = MxMoveSupportedAMs in {
  foreach TYPE = [MxType16, MxType32] in
  def MOVM # TYPE.Size # m # AM # TYPE.Postfix
      : MxMOVEM_RM<TYPE, !if(!eq(TYPE, MxType16), MxMOVEM_W, MxMOVEM_L),
                   !cast<MxOpBundle>("MxOp"#TYPE.Size#"AddrMode_"#AM).Op,
                   !cast<MxEncMemOp>("MxMoveSrcOpEnc_"#AM)>;
} // foreach AM

// Pseudo versions. These a required by virtual register spill/restore since
// the mask requires real register to encode. These instruction will be expanded
// into real MOVEM after RA finishes.
let mayStore = 1 in
class MxMOVEM_MR_Pseudo<MxType TYPE, MxOperand MEMOp>
    : MxPseudo<(outs), (ins MEMOp:$dst, TYPE.ROp:$reg)>;
let mayLoad = 1 in
class MxMOVEM_RM_Pseudo<MxType TYPE, MxOperand MEMOp>
    : MxPseudo<(outs TYPE.ROp:$dst), (ins MEMOp:$src)>;

// Mem <- Reg
def MOVM8jm_P  : MxMOVEM_MR_Pseudo<MxType8d,  MxType8.JOp>;
def MOVM16jm_P : MxMOVEM_MR_Pseudo<MxType16r, MxType16.JOp>;
def MOVM32jm_P : MxMOVEM_MR_Pseudo<MxType32r, MxType32.JOp>;

def MOVM8pm_P  : MxMOVEM_MR_Pseudo<MxType8d,  MxType8.POp>;
def MOVM16pm_P : MxMOVEM_MR_Pseudo<MxType16r, MxType16.POp>;
def MOVM32pm_P : MxMOVEM_MR_Pseudo<MxType32r, MxType32.POp>;

// Reg <- Mem
def MOVM8mj_P  : MxMOVEM_RM_Pseudo<MxType8d,  MxType8.JOp>;
def MOVM16mj_P : MxMOVEM_RM_Pseudo<MxType16r, MxType16.JOp>;
def MOVM32mj_P : MxMOVEM_RM_Pseudo<MxType32r, MxType32.JOp>;

def MOVM8mp_P  : MxMOVEM_RM_Pseudo<MxType8d,  MxType8.POp>;
def MOVM16mp_P : MxMOVEM_RM_Pseudo<MxType16r, MxType16.POp>;
def MOVM32mp_P : MxMOVEM_RM_Pseudo<MxType32r, MxType32.POp>;


//===----------------------------------------------------------------------===//
// MOVE to/from SR/CCR
//
// A special care must be taken working with to/from CCR since it is basically
// word-size SR register truncated for user mode thus it only supports word-size
// instructions. Plus the original M68000 does not support moves from CCR. So in
// order to use CCR effectively one MUST use proper byte-size pseudo instructi-
// ons that will be resolved sometime after RA pass.
//===----------------------------------------------------------------------===//

/// --------------------------------------------------
///  F  E  D  C  B  A  9  8  7  6 | 5  4  3 | 2  1  0
/// --------------------------------------------------
///                               | EFFECTIVE ADDRESS
///  0  1  0  0  0  1  0  0  1  1 |   MODE  |   REG
/// --------------------------------------------------
let Defs = [CCR] in
class MxMoveToCCR<MxOperand MEMOp, MxEncMemOp SRC_ENC>
    : MxInst<(outs CCRC:$dst), (ins MEMOp:$src), "move.w\t$src, $dst", []> {
  let Inst = (ascend
    (descend 0b0100010011, SRC_ENC.EA),
    SRC_ENC.Supplement
  );
}

class MxMoveToCCRPseudo<MxOperand MEMOp>
    : MxPseudo<(outs CCRC:$dst), (ins MEMOp:$src)>;

let mayLoad = 1 in
foreach AM = MxMoveSupportedAMs in {
  def MOV16c # AM : MxMoveToCCR<!cast<MxOpBundle>("MxOp16AddrMode_"#AM).Op,
                                !cast<MxEncMemOp>("MxMoveSrcOpEnc_"#AM)>;
  def MOV8c # AM  : MxMoveToCCRPseudo<!cast<MxOpBundle>("MxOp8AddrMode_"#AM).Op>;
} // foreach AM

// Only data register is allowed.
def MOV16cd : MxMoveToCCR<MxOp16AddrMode_d.Op, MxMoveSrcOpEnc_d>;
def MOV8cd  : MxMoveToCCRPseudo<MxOp8AddrMode_d.Op>;

/// Move from CCR
/// --------------------------------------------------
///  F  E  D  C  B  A  9  8  7  6 | 5  4  3 | 2  1  0
/// --------------------------------------------------
///                               | EFFECTIVE ADDRESS
///  0  1  0  0  0  0  1  0  1  1 |   MODE  |   REG
/// --------------------------------------------------
let Uses = [CCR] in {
class MxMoveFromCCR_R
    : MxInst<(outs MxDRD16:$dst), (ins CCRC:$src), "move.w\t$src, $dst", []>,
      Requires<[ AtLeastM68010 ]> {
  let Inst = (descend 0b0100001011, MxEncAddrMode_d<"dst">.EA);
}

class MxMoveFromCCR_M<MxOperand MEMOp, MxEncMemOp DST_ENC>
    : MxInst<(outs), (ins MEMOp:$dst, CCRC:$src), "move.w\t$src, $dst", []>,
      Requires<[ AtLeastM68010 ]> {
  let Inst = (ascend
    (descend 0b0100001011, DST_ENC.EA),
    DST_ENC.Supplement
  );
}

class MxMoveFromCCRPseudo<MxOperand MEMOp>
    : MxPseudo<(outs), (ins MEMOp:$dst, CCRC:$src)>;
} // let Uses = [CCR]

let mayStore = 1 in
foreach AM = MxMoveSupportedAMs in {
  def MOV16 # AM # c
    : MxMoveFromCCR_M<!cast<MxOpBundle>("MxOp16AddrMode_"#AM).Op,
                      !cast<MxEncMemOp>("MxMoveDstOpEnc_"#AM)>;
  def MOV8 # AM # c
    : MxMoveFromCCRPseudo<!cast<MxOpBundle>("MxOp8AddrMode_"#AM).Op>;
} // foreach AM

// Only data register is allowed.
def MOV16dc : MxMoveFromCCR_R;
def MOV8dc  : MxMoveFromCCRPseudo<MxOp8AddrMode_d.Op>;

//===----------------------------------------------------------------------===//
// LEA
//===----------------------------------------------------------------------===//

/// ----------------------------------------------------
///  F  E  D  C | B  A  9 | 8  7  6 | 5  4  3 | 2  1  0
/// ----------------------------------------------------
///  0  1  0  0 | DST REG | 1  1  1 |   MODE  |   REG
/// ----------------------------------------------------
class MxLEA<MxOpBundle SRC, MxEncMemOp SRC_ENC>
    : MxInst<(outs MxARD32:$dst), (ins SRC.Op:$src),
             "lea\t$src, $dst", [(set i32:$dst, SRC.Pat:$src)]> {
  let Inst = (ascend
    (descend 0b0100, (operand "$dst", 3), 0b111, SRC_ENC.EA),
    SRC_ENC.Supplement
  );
}

foreach AM = ["p", "f", "b", "q", "k"] in
def LEA32 # AM : MxLEA<!cast<MxOpBundle>("MxOp32AddrMode_"#AM),
                       !cast<MxEncMemOp>("MxMoveSrcOpEnc_"#AM)>;

//===----------------------------------------------------------------------===//
// LINK/UNLK
//===----------------------------------------------------------------------===//

let Uses = [SP], Defs = [SP] in {
let mayStore = 1 in {

def LINK16 : MxInst<(outs), (ins MxARD16:$src, Mxi16imm:$disp), "link.w\t$src, $disp", []> {
  let Inst = (ascend
    (descend 0b0100111001010, (operand "$src", 3)),
    (operand "$disp", 16)
  );
}

def LINK32 : MxInst<(outs), (ins MxARD16:$src, Mxi32imm:$disp), "link.l\t$src, $disp", []> {
  let Inst = (ascend
    (descend 0b0100100000001, (operand "$src", 3)),
    (slice "$disp", 31, 16),
    (slice "$disp", 15, 0)
  );
}

def UNLK : MxInst<(outs), (ins MxARD32:$src), "unlk\t$src", []> {
  let Inst = (descend 0b0100111001011, (operand "$src", 3));
}

} // let mayStore = 1
} // let Uses = [SP], Defs = [SP]

//===----------------------------------------------------------------------===//
// Pseudos
//===----------------------------------------------------------------------===//

/// Pushe/Pop to/from SP for simplicity
let Uses = [SP], Defs = [SP], hasSideEffects = 0 in {

// SP <- SP - <size>; (SP) <- Dn
let mayStore = 1 in {
def PUSH8d  : MxPseudo<(outs), (ins DR8:$reg)>;
def PUSH16d : MxPseudo<(outs), (ins DR16:$reg)>;
def PUSH32r : MxPseudo<(outs), (ins XR32:$reg)>;
} // let mayStore = 1

// Dn <- (SP); SP <- SP + <size>
let mayLoad = 1 in {
def POP8d  : MxPseudo<(outs DR8:$reg),  (ins)>;
def POP16d : MxPseudo<(outs DR16:$reg), (ins)>;
def POP32r : MxPseudo<(outs XR32:$reg), (ins)>;
} // let mayLoad = 1

} // let Uses/Defs = [SP], hasSideEffects = 0


let Defs = [CCR] in {
class MxPseudoMove_RR<MxType DST, MxType SRC, list<dag> PAT = []>
    : MxPseudo<(outs DST.ROp:$dst), (ins SRC.ROp:$src), PAT>;

class MxPseudoMove_RM<MxType DST, MxOperand SRCOpd, list<dag> PAT = []>
    : MxPseudo<(outs DST.ROp:$dst), (ins SRCOpd:$src), PAT>;


// These Pseudos handle loading immediates to registers.
// They are expanded post-RA into either move or moveq instructions,
// depending on size, destination register class, and immediate value.
// This is done with pseudoinstructions in order to not constrain RA to
// data registers if moveq matches.
class MxPseudoMove_DI<MxType TYPE>
    : MxPseudo<(outs TYPE.ROp:$dst), (ins TYPE.IOp:$src),
               [(set TYPE.ROp:$dst, imm:$src)]>;

// i8 imm -> reg can always be converted to moveq,
// but we still emit a pseudo for consistency.
def MOVI8di  : MxPseudoMove_DI<MxType8d>;
def MOVI16ri : MxPseudoMove_DI<MxType16r>;
def MOVI32ri : MxPseudoMove_DI<MxType32r>;
} // let Defs = [CCR]

/// This group of Pseudos is analogues to the real x86 extending moves, but
/// since M68k does not have those we need to emulate. These instructions
/// will be expanded right after RA completed because we need to know precisely
/// what registers are allocated for the operands and if they overlap we just
/// extend the value if the registers are completely different we need to move
/// first.
foreach EXT = ["S", "Z"] in {
  let hasSideEffects = 0 in {

    def MOV#EXT#Xd16d8  : MxPseudoMove_RR<MxType16d,  MxType8d>;
    def MOV#EXT#Xd32d8  : MxPseudoMove_RR<MxType32d,  MxType8d>;
    def MOV#EXT#Xd32d16 : MxPseudoMove_RR<MxType32r, MxType16r>;

    let mayLoad = 1 in {

      def MOV#EXT#Xd16j8   : MxPseudoMove_RM<MxType16d,  MxType8.JOp>;
      def MOV#EXT#Xd32j8   : MxPseudoMove_RM<MxType32d,  MxType8.JOp>;
      def MOV#EXT#Xd32j16  : MxPseudoMove_RM<MxType32d, MxType16.JOp>;

      def MOV#EXT#Xd16p8   : MxPseudoMove_RM<MxType16d,  MxType8.POp>;
      def MOV#EXT#Xd32p8   : MxPseudoMove_RM<MxType32d,  MxType8.POp>;
      def MOV#EXT#Xd32p16  : MxPseudoMove_RM<MxType32d, MxType16.POp>;

      def MOV#EXT#Xd16f8   : MxPseudoMove_RM<MxType16d,  MxType8.FOp>;
      def MOV#EXT#Xd32f8   : MxPseudoMove_RM<MxType32d,  MxType8.FOp>;
      def MOV#EXT#Xd32f16  : MxPseudoMove_RM<MxType32d, MxType16.FOp>;

      def MOV#EXT#Xd16q8   : MxPseudoMove_RM<MxType16d,  MxType8.QOp>;
      def MOV#EXT#Xd32q8   : MxPseudoMove_RM<MxType32d,  MxType8.QOp>;
      def MOV#EXT#Xd32q16  : MxPseudoMove_RM<MxType32d,  MxType16.QOp>;

    }
  }
}

/// This group of instructions is similar to the group above but DOES NOT do
/// any value extension, they just load a smaller register into the lower part
/// of another register if operands' real registers are different or does
/// nothing if they are the same.
def MOVXd16d8  : MxPseudoMove_RR<MxType16d,  MxType8d>;
def MOVXd32d8  : MxPseudoMove_RR<MxType32d,  MxType8d>;
def MOVXd32d16 : MxPseudoMove_RR<MxType32r, MxType16r>;

//===----------------------------------------------------------------------===//
// Extend/Truncate Patterns
//===----------------------------------------------------------------------===//

// i16 <- sext i8
def: Pat<(i16 (sext i8:$src)),
          (EXTRACT_SUBREG (MOVSXd32d8 MxDRD8:$src), MxSubRegIndex16Lo)>;
def: Pat<(MxSExtLoadi16i8 MxCP_ARI:$src),
          (EXTRACT_SUBREG (MOVSXd32j8 MxARI8:$src), MxSubRegIndex16Lo)>;
def: Pat<(MxSExtLoadi16i8 MxCP_ARID:$src),
          (EXTRACT_SUBREG (MOVSXd32p8 MxARID8:$src), MxSubRegIndex16Lo)>;
def: Pat<(MxSExtLoadi16i8 MxCP_ARII:$src),
          (EXTRACT_SUBREG (MOVSXd32f8 MxARII8:$src), MxSubRegIndex16Lo)>;
def: Pat<(MxSExtLoadi16i8 MxCP_PCD:$src), (MOVSXd16q8 MxPCD8:$src)>;

// i32 <- sext i8
def: Pat<(i32 (sext i8:$src)), (MOVSXd32d8 MxDRD8:$src)>;
def: Pat<(MxSExtLoadi32i8 MxCP_ARI :$src), (MOVSXd32j8 MxARI8 :$src)>;
def: Pat<(MxSExtLoadi32i8 MxCP_ARID:$src), (MOVSXd32p8 MxARID8:$src)>;
def: Pat<(MxSExtLoadi32i8 MxCP_ARII:$src), (MOVSXd32f8 MxARII8:$src)>;
def: Pat<(MxSExtLoadi32i8 MxCP_PCD:$src),  (MOVSXd32q8 MxPCD8:$src)>;

// i32 <- sext i16
def: Pat<(i32 (sext i16:$src)), (MOVSXd32d16 MxDRD16:$src)>;
def: Pat<(MxSExtLoadi32i16 MxCP_ARI :$src), (MOVSXd32j16 MxARI16 :$src)>;
def: Pat<(MxSExtLoadi32i16 MxCP_ARID:$src), (MOVSXd32p16 MxARID16:$src)>;
def: Pat<(MxSExtLoadi32i16 MxCP_ARII:$src), (MOVSXd32f16 MxARII16:$src)>;
def: Pat<(MxSExtLoadi32i16 MxCP_PCD:$src),  (MOVSXd32q16 MxPCD16:$src)>;

// i16 <- zext i8
def: Pat<(i16 (zext i8:$src)),
          (EXTRACT_SUBREG (MOVZXd32d8 MxDRD8:$src), MxSubRegIndex16Lo)>;
def: Pat<(MxZExtLoadi16i8 MxCP_ARI:$src),
          (EXTRACT_SUBREG (MOVZXd32j8 MxARI8:$src), MxSubRegIndex16Lo)>;
def: Pat<(MxZExtLoadi16i8 MxCP_ARID:$src),
          (EXTRACT_SUBREG (MOVZXd32p8 MxARID8:$src), MxSubRegIndex16Lo)>;
def: Pat<(MxZExtLoadi16i8 MxCP_ARII:$src),
          (EXTRACT_SUBREG (MOVZXd32f8 MxARII8:$src), MxSubRegIndex16Lo)>;
def: Pat<(MxZExtLoadi16i8 MxCP_PCD :$src), (MOVZXd16q8 MxPCD8 :$src)>;

// i32 <- zext i8
def: Pat<(i32 (zext i8:$src)), (MOVZXd32d8 MxDRD8:$src)>;
def: Pat<(MxZExtLoadi32i8 MxCP_ARI :$src), (MOVZXd32j8 MxARI8 :$src)>;
def: Pat<(MxZExtLoadi32i8 MxCP_ARID:$src), (MOVZXd32p8 MxARID8:$src)>;
def: Pat<(MxZExtLoadi32i8 MxCP_ARII:$src), (MOVZXd32f8 MxARII8:$src)>;
def: Pat<(MxZExtLoadi32i8 MxCP_PCD :$src), (MOVZXd32q8 MxPCD8 :$src)>;

// i32 <- zext i16
def: Pat<(i32 (zext i16:$src)), (MOVZXd32d16 MxDRD16:$src)>;
def: Pat<(MxZExtLoadi32i16 MxCP_ARI :$src), (MOVZXd32j16 MxARI16 :$src)>;
def: Pat<(MxZExtLoadi32i16 MxCP_ARID:$src), (MOVZXd32p16 MxARID16:$src)>;
def: Pat<(MxZExtLoadi32i16 MxCP_ARII:$src), (MOVZXd32f16 MxARII16:$src)>;
def: Pat<(MxZExtLoadi32i16 MxCP_PCD :$src), (MOVZXd32q16 MxPCD16 :$src)>;

// i16 <- anyext i8
def: Pat<(i16 (anyext i8:$src)),
          (EXTRACT_SUBREG (MOVZXd32d8 MxDRD8:$src), MxSubRegIndex16Lo)>;
def: Pat<(MxExtLoadi16i8 MxCP_ARI:$src),
          (EXTRACT_SUBREG (MOVZXd32j8 MxARI8:$src), MxSubRegIndex16Lo)>;
def: Pat<(MxExtLoadi16i8 MxCP_ARID:$src),
          (EXTRACT_SUBREG (MOVZXd32p8 MxARID8:$src), MxSubRegIndex16Lo)>;
def: Pat<(MxExtLoadi16i8 MxCP_ARII:$src),
          (EXTRACT_SUBREG (MOVZXd32f8 MxARII8:$src), MxSubRegIndex16Lo)>;

// i32 <- anyext i8
def: Pat<(i32 (anyext i8:$src)), (MOVZXd32d8 MxDRD8:$src)>;
def: Pat<(MxExtLoadi32i8 MxCP_ARI :$src), (MOVZXd32j8 MxARI8 :$src)>;
def: Pat<(MxExtLoadi32i8 MxCP_ARID:$src), (MOVZXd32p8 MxARID8:$src)>;
def: Pat<(MxExtLoadi32i8 MxCP_ARII:$src), (MOVZXd32f8 MxARII8:$src)>;

// i32 <- anyext i16
def: Pat<(i32 (anyext i16:$src)), (MOVZXd32d16 MxDRD16:$src)>;
def: Pat<(MxExtLoadi32i16 MxCP_ARI :$src), (MOVZXd32j16 MxARI16 :$src)>;
def: Pat<(MxExtLoadi32i16 MxCP_ARID:$src), (MOVZXd32p16 MxARID16:$src)>;
def: Pat<(MxExtLoadi32i16 MxCP_ARII:$src), (MOVZXd32f16 MxARII16:$src)>;

// trunc patterns
def : Pat<(i16 (trunc i32:$src)),
          (EXTRACT_SUBREG MxXRD32:$src, MxSubRegIndex16Lo)>;
def : Pat<(i8  (trunc i32:$src)),
          (EXTRACT_SUBREG MxXRD32:$src, MxSubRegIndex8Lo)>;
def : Pat<(i8  (trunc i16:$src)),
          (EXTRACT_SUBREG MxXRD16:$src, MxSubRegIndex8Lo)>;

//===----------------------------------------------------------------------===//
// FMOVE
//===----------------------------------------------------------------------===//

let Defs = [FPS] in
class MxFMove<string size, dag outs, dag ins, list<dag> pattern,
              string rounding = "">
    : MxInst<outs, ins,
             "f"#rounding#"move."#size#"\t$src, $dst", pattern> {
  // Only FMOVE uses FPC
  let Uses = !if(!eq(rounding, ""), [FPC], []);

  // FSMOVE and FDMOVE are only available after M68040
  let Predicates = [!if(!eq(rounding, ""), AtLeastM68881, AtLeastM68040)];
}

// FPDR <- FPDR
class MxFMove_FF<string rounding, int size,
                 MxOpBundle Opnd = !cast<MxOpBundle>("MxOp"#size#"AddrMode_fpr")>
    : MxFMove<"x", (outs Opnd.Op:$dst), (ins Opnd.Op:$src),
              [(null_frag)], rounding> {
  let Inst = (ascend
    (descend 0b1111,
      /*COPROCESSOR ID*/0b001,
      0b000,
      /*MODE + REGISTER*/0b000000
    ),
    (descend 0b0, /* R/M */0b0, 0b0,
      /*SOURCE SPECIFIER*/
      (operand "$src", 3),
      /*DESTINATION*/
      (operand "$dst", 3),
      /*OPMODE*/
      !cond(!eq(rounding, "s"): 0b1000000,
            !eq(rounding, "d"): 0b1000100,
            true: 0b0000000)
    )
  );
}

foreach rounding = ["", "s", "d"] in {
  def F # !toupper(rounding) # MOV80fp_fp : MxFMove_FF<rounding, 80>;

  // We don't have `fmove.s` or `fmove.d` because values will be converted to
  // f80 upon storing into the register, but FMOV32/64fp_fp are still needed
  // to make codegen easier.
  let isCodeGenOnly = true in
  foreach size = [32, 64] in
    def F # !toupper(rounding) # MOV # size # fp_fp : MxFMove_FF<rounding, size>;
}
// Direction
defvar MxFMove_FP_EA = false;
defvar MxFMove_EA_FP = true;

// Encoding scheme for FPSYS <-> R/M
class MxEncFSysMove<bit dir, MxEncMemOp EAEnc, string fsys_reg> {
  dag Value = (ascend
    (descend 0b1111,
      /*COPROCESSOR ID*/0b001,
      0b000,
      /*MODE + REGISTER*/
      EAEnc.EA
    ),
    (descend 0b10, /*dir*/ dir,
      /*REGISTER SELECT*/
      (operand "$"#fsys_reg, 3, (encoder "encodeFPSYSSelect")),
      0b0000000000
    )
  );
}

// FPSYS <-> R
class MxFMove_FSYS_R<string src_reg,
                     MxOpBundle SrcOpnd = !cast<MxOpBundle>("MxOp32AddrMode_"#src_reg),
                     MxOpBundle DstOpnd = !cond(!eq(src_reg, "d"): MxOp32AddrMode_fpcs,
                                                !eq(src_reg, "a"): MxOp32AddrMode_fpi),
                     MxEncMemOp SrcEnc = !cast<MxEncMemOp>("MxMoveSrcOpEnc_"#src_reg)>
    : MxFMove<"l", (outs DstOpnd.Op:$dst), (ins SrcOpnd.Op:$src),
              [(null_frag)]> {
  let Inst = MxEncFSysMove<MxFMove_FP_EA, SrcEnc, "dst">.Value;
}

class MxFMove_R_FSYS<string dst_reg,
                     MxOpBundle SrcOpnd = !cond(!eq(dst_reg, "d"): MxOp32AddrMode_fpcs,
                                                !eq(dst_reg, "a"): MxOp32AddrMode_fpi),
                     MxOpBundle DstOpnd = !cast<MxOpBundle>("MxOp32AddrMode_"#dst_reg),
                     MxEncMemOp DstEnc = !cast<MxEncMemOp>("MxMoveDstOpEnc_"#dst_reg)>
    : MxFMove<"l", (outs DstOpnd.Op:$dst), (ins SrcOpnd.Op:$src),
              [(null_frag)]> {
  let Inst = MxEncFSysMove<MxFMove_EA_FP, DstEnc, "src">.Value;
}

def FMOVE32fpcs_d : MxFMove_FSYS_R<"d">;
def FMOVE32d_fpcs : MxFMove_R_FSYS<"d">;
def FMOVE32fpi_a  : MxFMove_FSYS_R<"a">;
def FMOVE32a_fpi  : MxFMove_R_FSYS<"a">;
