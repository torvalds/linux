// LoongArchFloatInstrFormats.td - LoongArch FP Instr Formats -*- tablegen -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe LoongArch floating-point instructions format
//
//  opcode       - operation code.
//  fd           - destination register operand.
//  {c/f}{j/k/a} - source register operand.
//  immN         - immediate data operand.
//
//===----------------------------------------------------------------------===//

// Some FP instructions are defined twice, for accepting FPR32 and FPR64, but
// with the same mnemonic. Also some are codegen-only definitions that
// nevertheless require a "normal" mnemonic.
//
// In order to accommodate these needs, the instruction defs have names
// suffixed with `_x[SD]` or `_64`, that will get trimmed before the mnemonics
// are derived.
class deriveFPInsnMnemonic<string name> {
  string ret = deriveInsnMnemonic<!subst("_64", "",
                                         !subst("_xD", "",
                                                !subst("_xS", "", name)))>.ret;
}

// 2R-type
// <opcode | fj | fd>
class FPFmt2R<bits<32> op, dag outs, dag ins, string opnstr,
              list<dag> pattern = []>
    : LAInst<outs, ins, deriveFPInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> fj;
  bits<5> fd;

  let Inst{31-0} = op;
  let Inst{9-5} = fj;
  let Inst{4-0} = fd;
}

// 3R-type
// <opcode | fk | fj | fd>
class FPFmt3R<bits<32> op, dag outs, dag ins, string opnstr,
              list<dag> pattern = []>
    : LAInst<outs, ins, deriveFPInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> fk;
  bits<5> fj;
  bits<5> fd;

  let Inst{31-0} = op;
  let Inst{14-10} = fk;
  let Inst{9-5} = fj;
  let Inst{4-0} = fd;
}

// 4R-type
// <opcode | fa | fk | fj | fd>
class FPFmt4R<bits<32> op, dag outs, dag ins, string opnstr,
              list<dag> pattern = []>
    : LAInst<outs, ins, deriveFPInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> fa;
  bits<5> fk;
  bits<5> fj;
  bits<5> fd;

  let Inst{31-0} = op;
  let Inst{19-15} = fa;
  let Inst{14-10} = fk;
  let Inst{9-5} = fj;
  let Inst{4-0} = fd;
}

// 2RI12-type
// <opcode | I12 | rj | fd>
class FPFmt2RI12<bits<32> op, dag outs, dag ins, string opnstr,
                 list<dag> pattern = []>
    : LAInst<outs, ins, deriveFPInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<12> imm12;
  bits<5> rj;
  bits<5> fd;

  let Inst{31-0} = op;
  let Inst{21-10} = imm12;
  let Inst{9-5} = rj;
  let Inst{4-0} = fd;
}

// FmtFCMP
// <opcode | fk | fj | cd>
class FPFmtFCMP<bits<32> op, dag outs, dag ins, string opnstr,
                list<dag> pattern = []>
    : LAInst<outs, ins, deriveFPInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> fk;
  bits<5> fj;
  bits<3> cd;

  let Inst{31-0} = op;
  let Inst{14-10} = fk;
  let Inst{9-5} = fj;
  let Inst{2-0} = cd;
}

// FPFmtBR
// <opcode | I21[15:0] | cj | I21[20:16]>
class FPFmtBR<bits<32> op, dag outs, dag ins, string opnstr,
              list<dag> pattern = []>
    : LAInst<outs, ins, deriveFPInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<21> imm21;
  bits<3> cj;

  let Inst{31-0} = op;
  let Inst{25-10} = imm21{15-0};
  let Inst{7-5} = cj;
  let Inst{4-0} = imm21{20-16};
}

// FmtFSEL
// <opcode | ca | fk | fj | fd>
class FPFmtFSEL<bits<32> op, dag outs, dag ins, string opnstr,
                list<dag> pattern = []>
    : LAInst<outs, ins, deriveFPInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<3> ca;
  bits<5> fk;
  bits<5> fj;
  bits<5> fd;

  let Inst{31-0} = op;
  let Inst{17-15} = ca;
  let Inst{14-10} = fk;
  let Inst{9-5} = fj;
  let Inst{4-0} = fd;
}

// FPFmtMOV
// <opcode | src | dst>
class FPFmtMOV<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveFPInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> src;
  bits<5> dst;

  let Inst{31-0} = op;
  let Inst{9-5} = src;
  let Inst{4-0} = dst;
}

// FPFmtMEM
// <opcode | rk | rj | fd>
class FPFmtMEM<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveFPInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> rk;
  bits<5> rj;
  bits<5> fd;

  let Inst{31-0} = op;
  let Inst{14-10} = rk;
  let Inst{9-5} = rj;
  let Inst{4-0} = fd;
}

//===----------------------------------------------------------------------===//
// Instruction class templates
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
class FP_ALU_2R<bits<32> op, RegisterClass rc = FPR32>
    : FPFmt2R<op, (outs rc:$fd), (ins rc:$fj), "$fd, $fj">;

class FP_ALU_3R<bits<32> op, RegisterClass rc = FPR32>
    : FPFmt3R<op, (outs rc:$fd), (ins rc:$fj, rc:$fk), "$fd, $fj, $fk">;

class FP_ALU_4R<bits<32> op, RegisterClass rc = FPR32>
    : FPFmt4R<op, (outs rc:$fd), (ins rc:$fj, rc:$fk, rc:$fa),
              "$fd, $fj, $fk, $fa">;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
class FP_CMP<bits<32> op, RegisterClass rc = FPR32>
    : FPFmtFCMP<op, (outs CFR:$cd), (ins rc:$fj, rc:$fk), "$cd, $fj, $fk">;

class FP_CONV<bits<32> op, RegisterClass rcd = FPR32, RegisterClass rcs = FPR32>
    : FPFmt2R<op, (outs rcd:$fd), (ins rcs:$fj), "$fd, $fj">;

class FP_MOV<bits<32> op, RegisterClass rcd = FPR32, RegisterClass rcs = FPR32>
    : FPFmtMOV<op, (outs rcd:$dst), (ins rcs:$src), "$dst, $src">;

class FP_SEL<bits<32> op, RegisterClass rc = FPR32>
    : FPFmtFSEL<op, (outs rc:$fd), (ins rc:$fj, rc:$fk, CFR:$ca),
                "$fd, $fj, $fk, $ca">;

class FP_BRANCH<bits<32> opcode>
    : FPFmtBR<opcode, (outs), (ins CFR:$cj, simm21_lsl2:$imm21),
              "$cj, $imm21"> {
  let isBranch = 1;
  let isTerminator = 1;
}
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in {
class FP_LOAD_3R<bits<32> op, RegisterClass rc = FPR32>
    : FPFmtMEM<op, (outs rc:$fd), (ins GPR:$rj, GPR:$rk),
               "$fd, $rj, $rk">;
class FP_LOAD_2RI12<bits<32> op, RegisterClass rc = FPR32>
    : FPFmt2RI12<op, (outs rc:$fd), (ins GPR:$rj, simm12:$imm12),
                 "$fd, $rj, $imm12">;
} // hasSideEffects = 0, mayLoad = 1, mayStore = 0

let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in {
class FP_STORE_3R<bits<32> op, RegisterClass rc = FPR32>
    : FPFmtMEM<op, (outs), (ins rc:$fd, GPR:$rj, GPR:$rk),
               "$fd, $rj, $rk">;
class FP_STORE_2RI12<bits<32> op, RegisterClass rc = FPR32>
    : FPFmt2RI12<op, (outs), (ins rc:$fd, GPR:$rj, simm12:$imm12),
                 "$fd, $rj, $imm12">;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 1

// This class is used to define `SET_CFR_{FALSE,TRUE}` instructions which are
// used to expand `PseudoCopyCFR`.
class SET_CFR<bits<32> op, string opcstr>
    : FP_CMP<op> {
  let isCodeGenOnly = 1;
  let fj = 0; // fa0
  let fk = 0; // fa0
  let AsmString = opcstr # "\t$cd, $$fa0, $$fa0";
  let OutOperandList = (outs CFR:$cd);
  let InOperandList = (ins);
}
