// LoongArchLASXInstrFormats.td - LoongArch LASX Instr Formats - tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe LoongArch LASX instructions format
//
//  opcode       - operation code.
//  xd/rd/cd     - destination register operand.
//  {r/x}{j/k}   - source register operand.
//  immN         - immediate data operand.
//
//===----------------------------------------------------------------------===//

// 1RI13-type
// <opcode | I13 | xd>
class Fmt1RI13_XI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<13> imm13;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{17-5} = imm13;
  let Inst{4-0} = xd;
}

// 2R-type
// <opcode | xj | xd>
class Fmt2R_XX<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> xj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{9-5} = xj;
  let Inst{4-0} = xd;
}

// <opcode | rj | xd>
class Fmt2R_XR<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// <opcode | xj | cd>
class Fmt2R_CX<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> xj;
  bits<3> cd;

  let Inst{31-0} = op;
  let Inst{9-5} = xj;
  let Inst{2-0} = cd;
}

// 2RI1-type
// <opcode | I1 | xj | xd>
class Fmt2RI1_XXI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<1> imm1;
  bits<5> xj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{10} = imm1;
  let Inst{9-5} = xj;
  let Inst{4-0} = xd;
}

// 2RI2-type
// <opcode | I2 | xj | xd>
class Fmt2RI2_XXI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<2> imm2;
  bits<5> xj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{11-10} = imm2;
  let Inst{9-5} = xj;
  let Inst{4-0} = xd;
}

// <opcode | I2 | rj | xd>
class Fmt2RI2_XRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<2> imm2;
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{11-10} = imm2;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// <opcode | I2 | xj | rd>
class Fmt2RI2_RXI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<2> imm2;
  bits<5> xj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{11-10} = imm2;
  let Inst{9-5} = xj;
  let Inst{4-0} = rd;
}

// 2RI3-type
// <opcode | I3 | xj | xd>
class Fmt2RI3_XXI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<3> imm3;
  bits<5> xj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{12-10} = imm3;
  let Inst{9-5} = xj;
  let Inst{4-0} = xd;
}

// <opcode | I3 | rj | xd>
class Fmt2RI3_XRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<3> imm3;
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{12-10} = imm3;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// <opcode | I3 | xj | rd>
class Fmt2RI3_RXI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<3> imm3;
  bits<5> xj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{12-10} = imm3;
  let Inst{9-5} = xj;
  let Inst{4-0} = rd;
}

// 2RI4-type
// <opcode | I4 | xj | xd>
class Fmt2RI4_XXI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<4> imm4;
  bits<5> xj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{13-10} = imm4;
  let Inst{9-5} = xj;
  let Inst{4-0} = xd;
}

// <opcode | I4 | rj | xd>
class Fmt2RI4_XRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<4> imm4;
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{13-10} = imm4;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// <opcode | I4 | xj | rd>
class Fmt2RI4_RXI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<4> imm4;
  bits<5> xj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{13-10} = imm4;
  let Inst{9-5} = xj;
  let Inst{4-0} = rd;
}

// 2RI5-type
// <opcode | I5 | xj | xd>
class Fmt2RI5_XXI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> imm5;
  bits<5> xj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{14-10} = imm5;
  let Inst{9-5} = xj;
  let Inst{4-0} = xd;
}

// 2RI6-type
// <opcode | I6 | xj | xd>
class Fmt2RI6_XXI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<6> imm6;
  bits<5> xj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{15-10} = imm6;
  let Inst{9-5} = xj;
  let Inst{4-0} = xd;
}

// 2RI7-type
// <opcode | I7 | xj | xd>
class Fmt2RI7_XXI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<7> imm7;
  bits<5> xj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{16-10} = imm7;
  let Inst{9-5} = xj;
  let Inst{4-0} = xd;
}

// 2RI8-type
// <opcode | I8 | xj | xd>
class Fmt2RI8_XXI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<8> imm8;
  bits<5> xj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{17-10} = imm8;
  let Inst{9-5} = xj;
  let Inst{4-0} = xd;
}

// 2RI8I2-type
// <opcode | I2 | I8 | xj | xd>
class Fmt2RI8I2_XRII<bits<32> op, dag outs, dag ins, string opnstr,
                     list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<2> imm2;
  bits<8> imm8;
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{19-18} = imm2;
  let Inst{17-10} = imm8;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// 2RI8I3-type
// <opcode | I3 | I8 | xj | xd>
class Fmt2RI8I3_XRII<bits<32> op, dag outs, dag ins, string opnstr,
                     list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<3> imm3;
  bits<8> imm8;
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{20-18} = imm3;
  let Inst{17-10} = imm8;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// 2RI8I4-type
// <opcode | I4 | I8 | xj | xd>
class Fmt2RI8I4_XRII<bits<32> op, dag outs, dag ins, string opnstr,
                     list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<4> imm4;
  bits<8> imm8;
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{21-18} = imm4;
  let Inst{17-10} = imm8;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// 2RI8I5-type
// <opcode | I5 | I8 | xj | xd>
class Fmt2RI8I5_XRII<bits<32> op, dag outs, dag ins, string opnstr,
                     list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> imm5;
  bits<8> imm8;
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{22-18} = imm5;
  let Inst{17-10} = imm8;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// 2RI9-type
// <opcode | I9 | rj | xd>
class Fmt2RI9_XRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<9> imm9;
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{18-10} = imm9;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// 2RI10-type
// <opcode | I10 | rj | xd>
class Fmt2RI10_XRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<10> imm10;
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{19-10} = imm10;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// 2RI11-type
// <opcode | I11 | rj | xd>
class Fmt2RI11_XRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<11> imm11;
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{20-10} = imm11;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// 2RI12-type
// <opcode | I12 | rj | xd>
class Fmt2RI12_XRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<12> imm12;
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{21-10} = imm12;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// 3R-type
// <opcode | xk | xj | xd>
class Fmt3R_XXX<bits<32> op, dag outs, dag ins, string opnstr,
                list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> xk;
  bits<5> xj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{14-10} = xk;
  let Inst{9-5} = xj;
  let Inst{4-0} = xd;
}

// <opcode | rk | xj | xd>
class Fmt3R_XXR<bits<32> op, dag outs, dag ins, string opnstr,
                list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> rk;
  bits<5> xj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{14-10} = rk;
  let Inst{9-5} = xj;
  let Inst{4-0} = xd;
}

// <opcode | rk | rj | xd>
class Fmt3R_XRR<bits<32> op, dag outs, dag ins, string opnstr,
                list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> rk;
  bits<5> rj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{14-10} = rk;
  let Inst{9-5} = rj;
  let Inst{4-0} = xd;
}

// 4R-type
// <opcode | xa | xk | xj | xd>
class Fmt4R_XXXX<bits<32> op, dag outs, dag ins, string opnstr,
                 list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> xa;
  bits<5> xk;
  bits<5> xj;
  bits<5> xd;

  let Inst{31-0} = op;
  let Inst{19-15} = xa;
  let Inst{14-10} = xk;
  let Inst{9-5} = xj;
  let Inst{4-0} = xd;
}
