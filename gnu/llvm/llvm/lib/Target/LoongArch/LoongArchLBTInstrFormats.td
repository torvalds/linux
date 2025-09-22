// LoongArchLBTInstrFormats.td - LoongArch LBT Instr Formats -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe LoongArch LBT instructions format
//
//  opcode        - operation code.
//  rd/sd         - destination register operand.
//  rj/rk/sj      - source register operand.
//  immN/ptr      - immediate data operand.
//
//  Note: The definition of "NoDstFmt..." conveys the meaning of no explicit
//  output operand. In other words, there will be no output operand in the
//  assembly notation of these instructions. In fact, they always manipulate
//  the "EFLAGS" register.
//  Since these instructions are currently not used for code generation,
//  we do not need to add `let Defs/Uses = [EFLAGS]`.
//===----------------------------------------------------------------------===//

// 1R-type (no outs)
// <opcode | rj>
class NoDstFmt1R<bits<32> op>
    : LAInst<(outs), (ins GPR:$rj),
             deriveInsnMnemonic<NAME>.ret, "$rj"> {
  bits<5> rj;

  let Inst{31-0} = op;
  let Inst{9-5} = rj;
}

// 1RI3-type (no outs)
// <opcode | I3 | rj>
class NoDstFmt1RI3<bits<32> op>
    : LAInst<(outs), (ins GPR:$rj, uimm3:$imm3),
             deriveInsnMnemonic<NAME>.ret, "$rj, $imm3"> {
  bits<3> imm3;
  bits<5> rj;

  let Inst{31-0} = op;
  let Inst{12-10} = imm3;
  let Inst{9-5} = rj;
}

// 1RI4-type (no outs)
// <opcode | I4 | rj>
class NoDstFmt1RI4<bits<32> op>
    : LAInst<(outs), (ins GPR:$rj, uimm4:$imm4),
             deriveInsnMnemonic<NAME>.ret, "$rj, $imm4"> {
  bits<4> imm4;
  bits<5> rj;

  let Inst{31-0} = op;
  let Inst{13-10} = imm4;
  let Inst{9-5} = rj;
}

// 1RI4-type
// <opcode | I4 | rd>
class Fmt1RI4<bits<32> op>
    : LAInst<(outs GPR:$rd), (ins uimm4:$imm4),
             deriveInsnMnemonic<NAME>.ret, "$rd, $imm4"> {
  bits<4> imm4;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{13-10} = imm4;
  let Inst{4-0} = rd;
}

// 1RI5-type (no outs)
// <opcode | I5 | rj>
class NoDstFmt1RI5<bits<32> op>
    : LAInst<(outs), (ins GPR:$rj, uimm5:$imm5),
             deriveInsnMnemonic<NAME>.ret, "$rj, $imm5"> {
  bits<5> imm5;
  bits<5> rj;

  let Inst{31-0} = op;
  let Inst{14-10} = imm5;
  let Inst{9-5} = rj;
}

// 1RI5I4-type (no outs)
// <opcode | rd | I5 | I4>
class NoDstFmt1RI5I4<bits<32> op>
    : LAInst<(outs), (ins  GPR:$rj, uimm5:$imm5, uimm4:$imm4),
             deriveInsnMnemonic<NAME>.ret, "$rj, $imm5, $imm4"> {
  bits<5> imm5;
  bits<5> rj;
  bits<4> imm4;

  let Inst{31-0} = op;
  let Inst{14-10} = imm5;
  let Inst{9-5} = rj;
  let Inst{3-0} = imm4;
}

// 1RI5I8-type
// <opcode | rd | I5 | I8>
class Fmt1RI5I8<bits<32> op>
    : LAInst<(outs GPR:$rd), (ins uimm5:$imm5, uimm8:$imm8),
             deriveInsnMnemonic<NAME>.ret, "$rd, $imm5, $imm8"> {
  bits<8> imm8;
  bits<5> imm5;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{17-10} = imm8;
  let Inst{9-5} = imm5;
  let Inst{4-0} = rd;
}

// 1RI6-type (no outs)
// <opcode | I6 | rj>
class NoDstFmt1RI6<bits<32> op>
    : LAInst<(outs), (ins GPR:$rj, uimm6:$imm6),
             deriveInsnMnemonic<NAME>.ret, "$rj, $imm6"> {
  bits<6> imm6;
  bits<5> rj;

  let Inst{31-0} = op;
  let Inst{15-10} = imm6;
  let Inst{9-5} = rj;
}

// 1RI8-type
// <opcode | I8 | rd>
class Fmt1RI8<bits<32> op>
    : LAInst<(outs GPR:$rd), (ins uimm8:$imm8),
             deriveInsnMnemonic<NAME>.ret, "$rd, $imm8"> {
  bits<8> imm8;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{17-10} = imm8;
  let Inst{4-0} = rd;
}

// 2R-type (no outs)
// <opcode | rk | rj>
class NoDstFmt2R<bits<32> op>
    : LAInst<(outs), (ins GPR:$rj, GPR:$rk),
             deriveInsnMnemonic<NAME>.ret, "$rj, $rk"> {
  bits<5> rk;
  bits<5> rj;

  let Inst{31-0} = op;
  let Inst{14-10} = rk;
  let Inst{9-5} = rj;
}

// 2RI4-type (no outs)
// <opcode | rk | rj | imm4>
class NoDstFmt2RI4<bits<32> op>
    : LAInst<(outs), (ins GPR:$rj, GPR:$rk, uimm4:$imm4),
             deriveInsnMnemonic<NAME>.ret, "$rj, $rk, $imm4"> {
  bits<4> imm4;
  bits<5> rk;
  bits<5> rj;

  let Inst{31-0} = op;
  let Inst{14-10} = rk;
  let Inst{9-5} = rj;
  let Inst{3-0} = imm4;
}

// 2RI3-type
// <opcode | I3 | rj | rd>
class Fmt2RI3<bits<32> op>
    : LAInst<(outs GPR:$rd), (ins GPR:$rj, uimm3:$imm3),
             deriveInsnMnemonic<NAME>.ret, "$rd, $rj, $imm3"> {
  bits<3> imm3;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{12-10} = imm3;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// 2RI4-type
// <opcode | I4 | rj | rd>
class Fmt2RI4<bits<32> op>
    : LAInst<(outs GPR:$rd), (ins GPR:$rj, uimm4:$imm4),
             deriveInsnMnemonic<NAME>.ret, "$rd, $rj, $imm4"> {
  bits<4> imm4;
  bits<5> rj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{13-10} = imm4;
  let Inst{9-5} = rj;
  let Inst{4-0} = rd;
}

// <opcode | rj | sd>
class FmtGR2SCR<bits<32> op>
    : LAInst<(outs SCR:$sd), (ins GPR:$rj), deriveInsnMnemonic<NAME>.ret,
             "$sd, $rj"> {
  bits<5> rj;
  bits<2> sd;

  let Inst{31-0} = op;
  let Inst{9-5} = rj;
  let Inst{1-0} = sd;
}

// <opcode | sj | rd>
class FmtSCR2GR<bits<32> op>
    : LAInst<(outs GPR:$rd), (ins SCR:$sj), deriveInsnMnemonic<NAME>.ret,
             "$rd, $sj"> {
  bits<2> sj;
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{6-5} = sj;
  let Inst{4-0} = rd;
}

// <opcode | I21[15:0] | I21[20:16]>
class FmtJISCR<bits<32> op>
    : LAInst<(outs), (ins simm21_lsl2:$imm21), deriveInsnMnemonic<NAME>.ret,
              "$imm21"> {
  bits<21> imm21;
  bits<5> rj;

  let Inst{31-0} = op;
  let Inst{25-10} = imm21{15-0};
  let Inst{4-0} = imm21{20-16};
}

// <opcode | rd>
class FmtMFTOP<bits<32> op>
    : LAInst<(outs GPR:$rd), (ins), deriveInsnMnemonic<NAME>.ret,
             "$rd"> {
  bits<5> rd;

  let Inst{31-0} = op;
  let Inst{4-0} = rd;
}

// <opcode | ptr>
class FmtMTTOP<bits<32> op>
    : LAInst<(outs), (ins uimm3:$ptr), deriveInsnMnemonic<NAME>.ret,
             "$ptr"> {
  bits<3> ptr;

  let Inst{31-0} = op;
  let Inst{7-5} = ptr;
}
