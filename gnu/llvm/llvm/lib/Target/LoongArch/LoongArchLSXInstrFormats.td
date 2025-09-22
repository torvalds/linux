// LoongArchLSXInstrFormats.td - LoongArch LSX Instr Formats -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe LoongArch LSX instructions format
//
//  opcode       - operation code.
//  vd/rd/cd     - destination register operand.
//  {r/v}{j/k}   - source register operand.
//  immN         - immediate data operand.
//
//===----------------------------------------------------------------------===//

// 1RI13-type
// <opcode | I13 | vd>
class Fmt1RI13_VI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<13> imm13;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{17-5} = imm13;
  let Inst{4-0} = vd;
}

// 2R-type
// <opcode | vj | vd>
class Fmt2R_VV<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> vj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{9-5} = vj;
  let Inst{4-0} = vd;
}

// <opcode | rj | vd>
class Fmt2R_VR<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// <opcode | vj | cd>
class Fmt2R_CV<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> vj;
  bits<3> cd;

  let Inst{31-0} = op;
  let Inst{9-5} = vj;
  let Inst{2-0} = cd;
}

// 2RI1-type
// <opcode | I1 | vj | vd>
class Fmt2RI1_VVI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<1> imm1;
  bits<5> vj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{10} = imm1;
  let Inst{9-5} = vj;
  let Inst{4-0} = vd;
}

// <opcode | I1 | rj | vd>
class Fmt2RI1_VRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<1> imm1;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{10} = imm1;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// <opcode | I1 | vj | rd>
class Fmt2RI1_RVI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<1> imm1;
  bits<5> vj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{10} = imm1;
  let Inst{9-5} = vj;
  let Inst{4-0} = rd;
}

// 2RI2-type
// <opcode | I2 | vj | vd>
class Fmt2RI2_VVI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<2> imm2;
  bits<5> vj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{11-10} = imm2;
  let Inst{9-5} = vj;
  let Inst{4-0} = vd;
}

// <opcode | I2 | rj | vd>
class Fmt2RI2_VRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<2> imm2;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{11-10} = imm2;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// <opcode | I2 | vj | rd>
class Fmt2RI2_RVI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<2> imm2;
  bits<5> vj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{11-10} = imm2;
  let Inst{9-5} = vj;
  let Inst{4-0} = rd;
}

// 2RI3-type
// <opcode | I3 | vj | vd>
class Fmt2RI3_VVI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<3> imm3;
  bits<5> vj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{12-10} = imm3;
  let Inst{9-5} = vj;
  let Inst{4-0} = vd;
}

// <opcode | I3 | rj | vd>
class Fmt2RI3_VRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<3> imm3;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{12-10} = imm3;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// <opcode | I3 | vj | rd>
class Fmt2RI3_RVI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<3> imm3;
  bits<5> vj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{12-10} = imm3;
  let Inst{9-5} = vj;
  let Inst{4-0} = rd;
}

// 2RI4-type
// <opcode | I4 | vj | vd>
class Fmt2RI4_VVI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<4> imm4;
  bits<5> vj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{13-10} = imm4;
  let Inst{9-5} = vj;
  let Inst{4-0} = vd;
}

// <opcode | I4 | rj | vd>
class Fmt2RI4_VRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<4> imm4;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{13-10} = imm4;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// <opcode | I4 | vj | rd>
class Fmt2RI4_RVI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<4> imm4;
  bits<5> vj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{13-10} = imm4;
  let Inst{9-5} = vj;
  let Inst{4-0} = rd;
}

// 2RI5-type
// <opcode | I5 | vj | vd>
class Fmt2RI5_VVI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> imm5;
  bits<5> vj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{14-10} = imm5;
  let Inst{9-5} = vj;
  let Inst{4-0} = vd;
}

// 2RI6-type
// <opcode | I6 | vj | vd>
class Fmt2RI6_VVI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<6> imm6;
  bits<5> vj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{15-10} = imm6;
  let Inst{9-5} = vj;
  let Inst{4-0} = vd;
}

// 2RI7-type
// <opcode | I7 | vj | vd>
class Fmt2RI7_VVI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<7> imm7;
  bits<5> vj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{16-10} = imm7;
  let Inst{9-5} = vj;
  let Inst{4-0} = vd;
}

// 2RI8-type
// <opcode | I8 | vj | vd>
class Fmt2RI8_VVI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<8> imm8;
  bits<5> vj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{17-10} = imm8;
  let Inst{9-5} = vj;
  let Inst{4-0} = vd;
}

// 2RI8I1-type
// <opcode | I1 | I8 | vj | vd>
class Fmt2RI8I1_VRII<bits<32> op, dag outs, dag ins, string opnstr,
                     list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<1> imm1;
  bits<8> imm8;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{18} = imm1;
  let Inst{17-10} = imm8;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// 2RI8I2-type
// <opcode | I2 | I8 | vj | vd>
class Fmt2RI8I2_VRII<bits<32> op, dag outs, dag ins, string opnstr,
                     list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<2> imm2;
  bits<8> imm8;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{19-18} = imm2;
  let Inst{17-10} = imm8;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// 2RI8I3-type
// <opcode | I3 | I8 | vj | vd>
class Fmt2RI8I3_VRII<bits<32> op, dag outs, dag ins, string opnstr,
                     list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<3> imm3;
  bits<8> imm8;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{20-18} = imm3;
  let Inst{17-10} = imm8;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// 2RI8I4-type
// <opcode | I4 | I8 | vj | vd>
class Fmt2RI8I4_VRII<bits<32> op, dag outs, dag ins, string opnstr,
                     list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<4> imm4;
  bits<8> imm8;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{21-18} = imm4;
  let Inst{17-10} = imm8;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}
// 2RI9-type
// <opcode | I9 | rj | vd>
class Fmt2RI9_VRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<9> imm9;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{18-10} = imm9;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// 2RI10-type
// <opcode | I10 | rj | vd>
class Fmt2RI10_VRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<10> imm10;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{19-10} = imm10;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// 2RI11-type
// <opcode | I11 | rj | vd>
class Fmt2RI11_VRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<11> imm11;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{20-10} = imm11;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// 2RI12-type
// <opcode | I12 | rj | vd>
class Fmt2RI12_VRI<bits<32> op, dag outs, dag ins, string opnstr,
                  list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<12> imm12;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{21-10} = imm12;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// 3R-type
// <opcode | vk | vj | vd>
class Fmt3R_VVV<bits<32> op, dag outs, dag ins, string opnstr,
                list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> vk;
  bits<5> vj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{14-10} = vk;
  let Inst{9-5} = vj;
  let Inst{4-0} = vd;
}

// <opcode | rk | vj | vd>
class Fmt3R_VVR<bits<32> op, dag outs, dag ins, string opnstr,
                list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> rk;
  bits<5> vj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{14-10} = rk;
  let Inst{9-5} = vj;
  let Inst{4-0} = vd;
}

// <opcode | rk | rj | vd>
class Fmt3R_VRR<bits<32> op, dag outs, dag ins, string opnstr,
                list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> rk;
  bits<5> rj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{14-10} = rk;
  let Inst{9-5} = rj;
  let Inst{4-0} = vd;
}

// 4R-type
// <opcode | va | vk | vj | vd>
class Fmt4R_VVVV<bits<32> op, dag outs, dag ins, string opnstr,
                 list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> va;
  bits<5> vk;
  bits<5> vj;
  bits<5> vd;

  let Inst{31-0} = op;
  let Inst{19-15} = va;
  let Inst{14-10} = vk;
  let Inst{9-5} = vj;
  let Inst{4-0} = vd;
}
