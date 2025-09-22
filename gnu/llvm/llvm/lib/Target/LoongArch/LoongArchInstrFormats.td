//===- LoongArchInstrFormats.td - LoongArch Instr. Formats -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe LoongArch instructions format
//
//  opcode       - operation code.
//  rd           - destination register operand.
//  r{j/k}       - source register operand.
//  immN         - immediate data operand.
//
//===----------------------------------------------------------------------===//

class LAInst<dag outs, dag ins, string opcstr, string opnstr,
             list<dag> pattern = []>
    : Instruction {
  field bits<32> Inst;
  // SoftFail is a field the disassembler can use to provide a way for
  // instructions to not match without killing the whole decode process. It is
  // mainly used for ARM, but Tablegen expects this field to exist or it fails
  // to build the decode table.
  field bits<32> SoftFail = 0;

  let Namespace = "LoongArch";
  let Size = 4;
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = opcstr # "\t" # opnstr;
  let Pattern = pattern;
}

// Pseudo instructions
class Pseudo<dag outs, dag ins, list<dag> pattern = [], string opcstr = "",
             string opnstr = "">
    : LAInst<outs, ins, opcstr, opnstr, pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

class deriveInsnMnemonic<string name> {
  string ret = !tolower(!subst("@", "_", !subst("_", ".", !subst("__", "@", name))));
}

// 2R-type
// <opcode | rj | rd>
class Fmt2R<bits<32> op, dag outs, dag ins, string opnstr,
            list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// 3R-type
// <opcode | rk | rj | rd>
class Fmt3R<bits<32> op, dag outs, dag ins, string opnstr,
            list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> rk;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{14-10} = rk;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// 3RI2-type
// <opcode | I2 | rk | rj | rd>
class Fmt3RI2<bits<32> op, dag outs, dag ins, string opnstr,
              list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<2> imm2;
  bits<5> rk;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{16-15} = imm2;
  let Inst{14-10} = rk;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// 3RI3-type
// <opcode | I3 | rk | rj | rd>
class Fmt3RI3<bits<32> op, dag outs, dag ins, string opnstr,
              list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<3> imm3;
  bits<5> rk;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{17-15} = imm3;
  let Inst{14-10} = rk;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// 2RI5-type
// <opcode | I5 | rj | rd>
class Fmt2RI5<bits<32> op, dag outs, dag ins, string opnstr,
              list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> imm5;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{14-10} = imm5;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// 2RI6-type
// <opcode | I6 | rj | rd>
class Fmt2RI6<bits<32> op, dag outs, dag ins, string opnstr,
              list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<6> imm6;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{15-10} = imm6;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// 2RI8-type
// <opcode | I8 | rj | rd>
class Fmt2RI8<bits<32> op, dag outs, dag ins, string opnstr,
              list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<8> imm8;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{17-10} = imm8;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// 2RI12-type
// <opcode | I12 | rj | rd>
class Fmt2RI12<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<12> imm12;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{21-10} = imm12;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// 2RI14-type
// <opcode | I14 | rj | rd>
class Fmt2RI14<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<14> imm14;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{23-10} = imm14;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// 2RI16-type
// <opcode | I16 | rj | rd>
class Fmt2RI16<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<16> imm16;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{25-10} = imm16;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// 1RI20-type
// <opcode | I20 | rd>
class Fmt1RI20<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<20> imm20;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{24-5} = imm20;
  let Inst{4-0} = rd;
}

// 1RI21-type
// <opcode | I21[15:0] | rj | I21[20:16]>
class Fmt1RI21<bits<32> op, dag outs, dag ins, string opnstr,
               list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<21> imm21;
  bits<5> rj;

  let Inst{31-0} = op;
  let Inst{25-10} = imm21{15-0};
  let Inst{9-5} = rj;
  let Inst{4-0} = imm21{20-16};
}

// I15-type
// <opcode | I15>
class FmtI15<bits<32> op, dag outs, dag ins, string opnstr,
             list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<15> imm15;

  let Inst{31-0} = op;
  let Inst{14-0} = imm15;
}

// I26-type
// <opcode | I26[15:0] | I26[25:16]>
class FmtI26<bits<32> op, dag outs, dag ins, string opnstr,
             list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<26> imm26;

  let Inst{31-0} = op;
  let Inst{25-10} = imm26{15-0};
  let Inst{9-0} = imm26{25-16};
}

// FmtBSTR_W
// <opcode | msbw | lsbw | rj | rd>
class FmtBSTR_W<bits<32> op, dag outs, dag ins, string opnstr,
                list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> msbw;
  bits<5> lsbw;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{20-16} = msbw;
  let Inst{14-10} = lsbw;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// FmtBSTR_D
// <opcode | msbd | lsbd | rj | rd>
class FmtBSTR_D<bits<32> op, dag outs, dag ins, string opnstr,
                list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<6> msbd;
  bits<6> lsbd;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{21-16} = msbd;
  let Inst{15-10} = lsbd;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// FmtASRT
// <opcode | rk | rj>
class FmtASRT<bits<32> op, dag outs, dag ins, string opnstr,
              list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> rk;
  bits<5> rj;

  let Inst{31-0} = op;
  let Inst{14-10} = rk;
  let Inst{9-5} = rj;
}

// FmtPRELD
// < 0b0010101011 | I12 | rj | I5>
class FmtPRELD<dag outs, dag ins, string opnstr, list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<12> imm12;
  bits<5> rj;
  bits<5> imm5;

  let Inst{31-22} = 0b0010101011;
  let Inst{21-10} = imm12;
  let Inst{9-5} = rj;
  let Inst{4-0} = imm5;
}

// FmtPRELDX
// < 0b00111000001011000 | rk | rj | I5>
class FmtPRELDX<dag outs, dag ins, string opnstr, list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> rk;
  bits<5> rj;
  bits<5> imm5;

  let Inst{31-15} = 0b00111000001011000;
  let Inst{14-10} = rk;
  let Inst{9-5} = rj;
  let Inst{4-0} = imm5;
}

// FmtCSR
// <opcode | csr_num | rd>
class FmtCSR<bits<32> op, dag outs, dag ins, string opnstr,
             list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<14> csr_num;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{23-10} = csr_num;
  let Inst{4-0} = rd;
}

// FmtCSRXCHG
// <opcode | csr_num | rj | rd>
class FmtCSRXCHG<bits<32> op, dag outs, dag ins, string opnstr,
                 list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<14> csr_num;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{23-10} = csr_num;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// FmtCACOP
// <0b0000011000 | I12 | rj | I5>
class FmtCACOP<dag outs, dag ins, string opnstr, list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<12> imm12;
  bits<5> rj;
  bits<5> op;

  let Inst{31-22} = 0b0000011000;
  let Inst{21-10} = imm12;
  let Inst{9-5} = rj;
  let Inst{4-0} = op;
}

// FmtIMM32
// <I32>
class FmtI32<bits<32> op, list<dag> pattern = []>
    : LAInst<(outs), (ins), deriveInsnMnemonic<NAME>.ret, "", pattern> {
  let Inst{31-0} = op;
}

// FmtINVTLB
// <0b00000110010010011 | rk | rj | I5>
class FmtINVTLB<dag outs, dag ins, string opnstr, list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<5> rk;
  bits<5> rj;
  bits<5> op;

  let Inst{31-15} = 0b00000110010010011;
  let Inst{14-10} = rk;
  let Inst{9-5} = rj;
  let Inst{4-0} = op;
}

// FmtLDPTE
// <0b00000110010001 | seq | rj | 00000>
class FmtLDPTE<dag outs, dag ins, string opnstr, list<dag> pattern = []>
    : LAInst<outs, ins, deriveInsnMnemonic<NAME>.ret, opnstr, pattern> {
  bits<8> seq;
  bits<5> rj;

  let Inst{31-18} = 0b00000110010001;
  let Inst{17-10} = seq;
  let Inst{9-5} = rj;
  let Inst{4-0} = 0b00000;
}
