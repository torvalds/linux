//===-- RISCVInstrFormatsC.td - RISC-V C Instruction Formats -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file describes the RISC-V C extension instruction formats.
//
//===----------------------------------------------------------------------===//

class RVInst16<dag outs, dag ins, string opcodestr, string argstr,
               list<dag> pattern, InstFormat format>
    : RVInstCommon<outs, ins, opcodestr, argstr, pattern, format> {
  field bits<16> Inst;
  // SoftFail is a field the disassembler can use to provide a way for
  // instructions to not match without killing the whole decode process. It is
  // mainly used for ARM, but Tablegen expects this field to exist or it fails
  // to build the decode table.
  field bits<16> SoftFail = 0;
  let Size = 2;
}

class RVInst16CR<bits<4> funct4, bits<2> opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCR> {
  bits<5> rs1;
  bits<5> rs2;

  let Inst{15-12} = funct4;
  let Inst{11-7} = rs1;
  let Inst{6-2} = rs2;
  let Inst{1-0} = opcode;
}

// The immediate value encoding differs for each instruction, so each subclass
// is responsible for setting the appropriate bits in the Inst field.
// The bits Inst{6-2} must be set for each instruction.
class RVInst16CI<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCI> {
  bits<10> imm;
  bits<5> rd;

  let Inst{15-13} = funct3;
  let Inst{12} = imm{5};
  let Inst{11-7} = rd;
  let Inst{1-0} = opcode;
}

// The immediate value encoding differs for each instruction, so each subclass
// is responsible for setting the appropriate bits in the Inst field.
// The bits Inst{12-7} must be set for each instruction.
class RVInst16CSS<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                  string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCSS> {
  bits<10> imm;
  bits<5> rs2;
  bits<5> rs1;

  let Inst{15-13} = funct3;
  let Inst{6-2} = rs2;
  let Inst{1-0} = opcode;
}

class RVInst16CIW<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                  string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCIW> {
  bits<10> imm;
  bits<3> rd;

  let Inst{15-13} = funct3;
  let Inst{4-2} = rd;
  let Inst{1-0} = opcode;
}

// The immediate value encoding differs for each instruction, so each subclass
// is responsible for setting the appropriate bits in the Inst field.
// The bits Inst{12-10} and Inst{6-5} must be set for each instruction.
class RVInst16CL<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCL> {
  bits<3> rd;
  bits<3> rs1;

  let Inst{15-13} = funct3;
  let Inst{9-7} = rs1;
  let Inst{4-2} = rd;
  let Inst{1-0} = opcode;
}

// The immediate value encoding differs for each instruction, so each subclass
// is responsible for setting the appropriate bits in the Inst field.
// The bits Inst{12-10} and Inst{6-5} must be set for each instruction.
class RVInst16CS<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCS> {
  bits<3> rs2;
  bits<3> rs1;

  let Inst{15-13} = funct3;
  let Inst{9-7} = rs1;
  let Inst{4-2} = rs2;
  let Inst{1-0} = opcode;
}

class RVInst16CA<bits<6> funct6, bits<2> funct2, bits<2> opcode, dag outs,
                 dag ins, string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCA> {
  bits<3> rs2;
  bits<3> rs1;

  let Inst{15-10} = funct6;
  let Inst{9-7} = rs1;
  let Inst{6-5} = funct2;
  let Inst{4-2} = rs2;
  let Inst{1-0} = opcode;
}

class RVInst16CB<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCB> {
  bits<9> imm;
  bits<3> rs1;

  let Inst{15-13} = funct3;
  let Inst{9-7} = rs1;
  let Inst{1-0} = opcode;
}

class RVInst16CJ<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCJ> {
  bits<11> offset;

  let Inst{15-13} = funct3;
  let Inst{12} = offset{10};
  let Inst{11} = offset{3};
  let Inst{10-9} = offset{8-7};
  let Inst{8} = offset{9};
  let Inst{7} = offset{5};
  let Inst{6} = offset{6};
  let Inst{5-3} = offset{2-0};
  let Inst{2} = offset{4};
  let Inst{1-0} = opcode;
}

class RVInst16CU<bits<6> funct6, bits<5> funct5, bits<2> opcode, dag outs,
                 dag ins, string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCU>{
  bits<3> rd;

  let Inst{15-10} = funct6;
  let Inst{9-7} = rd;
  let Inst{6-2} = funct5;
  let Inst{1-0} = opcode;
}

// The immediate value encoding differs for each instruction, so each subclass
// is responsible for setting the appropriate bits in the Inst field.
// The bits Inst{6-5} must be set for each instruction.
class RVInst16CLB<bits<6> funct6, bits<2> opcode, dag outs, dag ins,
                  string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCLB> {
  bits<3> rd;
  bits<3> rs1;

  let Inst{15-10} = funct6;
  let Inst{9-7} = rs1;
  let Inst{4-2} = rd;
  let Inst{1-0} = opcode;
}

// The immediate value encoding differs for each instruction, so each subclass
// is responsible for setting the appropriate bits in the Inst field.
// The bits Inst{5} must be set for each instruction.
class RVInst16CLH<bits<6> funct6, bit funct1, bits<2> opcode, dag outs,
                  dag ins, string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCLH> {
  bits<3> rd;
  bits<3> rs1;

  let Inst{15-10} = funct6;
  let Inst{9-7} = rs1;
  let Inst{6} = funct1;
  let Inst{4-2} = rd;
  let Inst{1-0} = opcode;
}

// The immediate value encoding differs for each instruction, so each subclass
// is responsible for setting the appropriate bits in the Inst field.
// The bits Inst{6-5} must be set for each instruction.
class RVInst16CSB<bits<6> funct6, bits<2> opcode, dag outs, dag ins,
                  string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCSB> {
  bits<3> rs2;
  bits<3> rs1;

  let Inst{15-10} = funct6;
  let Inst{9-7} = rs1;
  let Inst{4-2} = rs2;
  let Inst{1-0} = opcode;
}

// The immediate value encoding differs for each instruction, so each subclass
// is responsible for setting the appropriate bits in the Inst field.
// The bits Inst{5} must be set for each instruction.
class RVInst16CSH<bits<6> funct6, bit funct1, bits<2> opcode, dag outs,
                  dag ins, string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCSH> {
  bits<3> rs2;
  bits<3> rs1;

  let Inst{15-10} = funct6;
  let Inst{9-7} = rs1;
  let Inst{6} = funct1;
  let Inst{4-2} = rs2;
  let Inst{1-0} = opcode;
}

//===----------------------------------------------------------------------===//
// Instruction classes for .insn directives
//===----------------------------------------------------------------------===//

class DirectiveInsnCR<dag outs, dag ins, string argstr>
  : RVInst16<outs, ins, "", "", [], InstFormatCR> {
  bits<2> opcode;
  bits<4> funct4;

  bits<5> rs2;
  bits<5> rd;

  let Inst{15-12} = funct4;
  let Inst{11-7} = rd;
  let Inst{6-2} = rs2;
  let Inst{1-0} = opcode;

  let AsmString = ".insn cr " # argstr;
}

class DirectiveInsnCI<dag outs, dag ins, string argstr>
  : RVInst16<outs, ins, "", "", [], InstFormatCI> {
  bits<2> opcode;
  bits<3> funct3;

  bits<6> imm6;
  bits<5> rd;

  let Inst{15-13} = funct3;
  let Inst{12} = imm6{5};
  let Inst{11-7} = rd;
  let Inst{6-2} = imm6{4-0};
  let Inst{1-0} = opcode;

  let AsmString = ".insn ci " # argstr;
}

class DirectiveInsnCIW<dag outs, dag ins, string argstr>
  : RVInst16<outs, ins, "", "", [], InstFormatCIW> {
  bits<2> opcode;
  bits<3> funct3;

  bits<8> imm8;
  bits<3> rd;

  let Inst{15-13} = funct3;
  let Inst{12-5} = imm8;
  let Inst{4-2} = rd;
  let Inst{1-0} = opcode;

  let AsmString = ".insn ciw " # argstr;
}

class DirectiveInsnCSS<dag outs, dag ins, string argstr>
  : RVInst16<outs, ins, "", "", [], InstFormatCSS> {
  bits<2> opcode;
  bits<3> funct3;

  bits<6> imm6;
  bits<5> rs2;

  let Inst{15-13} = funct3;
  let Inst{12-7} = imm6;
  let Inst{6-2} = rs2;
  let Inst{1-0} = opcode;

  let AsmString = ".insn css " # argstr;
}

class DirectiveInsnCL<dag outs, dag ins, string argstr>
  : RVInst16<outs, ins, "", "", [], InstFormatCL> {
  bits<2> opcode;
  bits<3> funct3;

  bits<5> imm5;
  bits<3> rd;
  bits<3> rs1;

  let Inst{15-13} = funct3;
  let Inst{12-10} = imm5{4-2};
  let Inst{9-7} = rs1;
  let Inst{6-5} = imm5{1-0};
  let Inst{4-2} = rd;
  let Inst{1-0} = opcode;

  let AsmString = ".insn cl " # argstr;
}

class DirectiveInsnCS<dag outs, dag ins, string argstr>
  : RVInst16<outs, ins, "", "", [], InstFormatCS> {
  bits<2> opcode;
  bits<3> funct3;

  bits<5> imm5;
  bits<3> rs2;
  bits<3> rs1;

  let Inst{15-13} = funct3;
  let Inst{12-10} = imm5{4-2};
  let Inst{9-7} = rs1;
  let Inst{6-5} = imm5{1-0};
  let Inst{4-2} = rs2;
  let Inst{1-0} = opcode;

  let AsmString = ".insn cs " # argstr;
}

class DirectiveInsnCA<dag outs, dag ins, string argstr>
  : RVInst16<outs, ins, "", "", [], InstFormatCA> {
  bits<2> opcode;
  bits<6> funct6;
  bits<2> funct2;

  bits<3> rd;
  bits<3> rs2;

  let Inst{15-10} = funct6;
  let Inst{9-7} = rd;
  let Inst{6-5} = funct2;
  let Inst{4-2} = rs2;
  let Inst{1-0} = opcode;

  let AsmString = ".insn ca " # argstr;
}

class DirectiveInsnCB<dag outs, dag ins, string argstr>
  : RVInst16<outs, ins, "", "", [], InstFormatCB> {
  bits<2> opcode;
  bits<3> funct3;

  bits<8> imm8;
  bits<3> rs1;

  let Inst{15-13} = funct3;
  let Inst{12} = imm8{7};
  let Inst{11-10} = imm8{3-2};
  let Inst{9-7} = rs1;
  let Inst{6-5} = imm8{6-5};
  let Inst{4-3} = imm8{1-0};
  let Inst{2} = imm8{4};
  let Inst{1-0} = opcode;

  let AsmString = ".insn cb " # argstr;
}

class DirectiveInsnCJ<dag outs, dag ins, string argstr>
  : RVInst16<outs, ins, "", "", [], InstFormatCJ> {
  bits<2> opcode;
  bits<3> funct3;

  bits<11> imm11;

  let Inst{15-13} = funct3;
  let Inst{12} = imm11{10};
  let Inst{11} = imm11{3};
  let Inst{10-9} = imm11{8-7};
  let Inst{8} = imm11{9};
  let Inst{7} = imm11{5};
  let Inst{6} = imm11{6};
  let Inst{5-3} = imm11{2-0};
  let Inst{2} = imm11{4};
  let Inst{1-0} = opcode;

  let AsmString = ".insn cj " # argstr;
}
