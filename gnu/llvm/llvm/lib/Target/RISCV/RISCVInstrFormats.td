//===-- RISCVInstrFormats.td - RISC-V Instruction Formats --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//
//  These instruction format definitions are structured to match the
//  description in the RISC-V User-Level ISA specification as closely as
//  possible. For instance, the specification describes instructions with the
//  MSB (31st bit) on the left and the LSB (0th bit) on the right. This is
//  reflected in the order of parameters to each instruction class.
//
//  One area of divergence is in the description of immediates. The
//  specification describes immediate encoding in terms of bit-slicing
//  operations on the logical value represented. The immediate argument to
//  these instruction formats instead represents the bit sequence that will be
//  inserted into the instruction. e.g. although JAL's immediate is logically
//  a 21-bit value (where the LSB is always zero), we describe it as an imm20
//  to match how it is encoded.
//
//===----------------------------------------------------------------------===//

// Format specifies the encoding used by the instruction. This is used by
// RISCVMCCodeEmitter to determine which form of fixup to use. These
// definitions must be kept in-sync with RISCVBaseInfo.h.
class InstFormat<bits<5> val> {
  bits<5> Value = val;
}
def InstFormatPseudo : InstFormat<0>;
def InstFormatR      : InstFormat<1>;
def InstFormatR4     : InstFormat<2>;
def InstFormatI      : InstFormat<3>;
def InstFormatS      : InstFormat<4>;
def InstFormatB      : InstFormat<5>;
def InstFormatU      : InstFormat<6>;
def InstFormatJ      : InstFormat<7>;
def InstFormatCR     : InstFormat<8>;
def InstFormatCI     : InstFormat<9>;
def InstFormatCSS    : InstFormat<10>;
def InstFormatCIW    : InstFormat<11>;
def InstFormatCL     : InstFormat<12>;
def InstFormatCS     : InstFormat<13>;
def InstFormatCA     : InstFormat<14>;
def InstFormatCB     : InstFormat<15>;
def InstFormatCJ     : InstFormat<16>;
def InstFormatCU     : InstFormat<17>;
def InstFormatCLB    : InstFormat<18>;
def InstFormatCLH    : InstFormat<19>;
def InstFormatCSB    : InstFormat<20>;
def InstFormatCSH    : InstFormat<21>;
def InstFormatOther  : InstFormat<22>;

class RISCVVConstraint<bits<3> val> {
  bits<3> Value = val;
}
def NoConstraint  : RISCVVConstraint<0b000>;
def VS2Constraint : RISCVVConstraint<0b001>;
def VS1Constraint : RISCVVConstraint<0b010>;
def VMConstraint  : RISCVVConstraint<0b100>;

// Illegal instructions:
//
// * The destination vector register group for a masked vector instruction
// cannot overlap the source mask register (v0), unless the destination vector
// register is being written with a mask value (e.g., comparisons) or the
// scalar result of a reduction.
//
// * Widening: The destination EEW is greater than the source EEW, the source
// EMUL is at least 1. The destination vector register group cannot overlap
// with the source vector register groups besides the highest-numbered part of
// the destination register group.
//
// * Narrowing: The destination EEW is smaller than the source EEW. The
// destination vector register group cannot overlap with the source vector
// register groups besides the lowest-numbered part of the source register
// group.
//
// * vmsbf.m/vmsif.m/vmsof.m: The destination register cannot overlap the
// source register and, if masked, cannot overlap the mask register ('v0').
//
// * viota: The destination register cannot overlap the source register and,
// if masked, cannot overlap the mask register ('v0').
//
// * v[f]slide[1]up: The destination vector register group for vslideup cannot
// overlap the source vector register group.
//
// * vrgather: The destination vector register group cannot overlap with the
// source vector register groups.
//
// * vcompress: The destination vector register group cannot overlap the
// source vector register group or the source mask register
def WidenV       : RISCVVConstraint<!or(VS2Constraint.Value,
                                        VS1Constraint.Value,
                                        VMConstraint.Value)>;
def WidenW       : RISCVVConstraint<!or(VS1Constraint.Value,
                                        VMConstraint.Value)>;
def WidenCvt     : RISCVVConstraint<!or(VS2Constraint.Value,
                                        VMConstraint.Value)>;
def Iota         : RISCVVConstraint<!or(VS2Constraint.Value,
                                        VMConstraint.Value)>;
def SlideUp      : RISCVVConstraint<!or(VS2Constraint.Value,
                                        VMConstraint.Value)>;
def Vrgather     : RISCVVConstraint<!or(VS2Constraint.Value,
                                        VS1Constraint.Value,
                                        VMConstraint.Value)>;
def Vcompress    : RISCVVConstraint<!or(VS2Constraint.Value,
                                        VS1Constraint.Value)>;
def Sha2Constraint : RISCVVConstraint<!or(VS2Constraint.Value,
                                          VS1Constraint.Value)>;

// The following opcode names match those given in Table 19.1 in the
// RISC-V User-level ISA specification ("RISC-V base opcode map").
class RISCVOpcode<string name, bits<7> val> {
  string Name = name;
  bits<7> Value = val;
}
def RISCVOpcodesList : GenericTable {
  let FilterClass = "RISCVOpcode";
  let Fields = [
    "Name", "Value"
  ];
  let PrimaryKey = [ "Value" ];
  let PrimaryKeyName = "lookupRISCVOpcodeByValue";
}
def lookupRISCVOpcodeByName : SearchIndex {
  let Table = RISCVOpcodesList;
  let Key = [ "Name" ];
}
def OPC_LOAD      : RISCVOpcode<"LOAD",      0b0000011>;
def OPC_LOAD_FP   : RISCVOpcode<"LOAD_FP",   0b0000111>;
def OPC_CUSTOM_0  : RISCVOpcode<"CUSTOM_0",  0b0001011>;
def OPC_MISC_MEM  : RISCVOpcode<"MISC_MEM",  0b0001111>;
def OPC_OP_IMM    : RISCVOpcode<"OP_IMM",    0b0010011>;
def OPC_AUIPC     : RISCVOpcode<"AUIPC",     0b0010111>;
def OPC_OP_IMM_32 : RISCVOpcode<"OP_IMM_32", 0b0011011>;
def OPC_STORE     : RISCVOpcode<"STORE",     0b0100011>;
def OPC_STORE_FP  : RISCVOpcode<"STORE_FP",  0b0100111>;
def OPC_CUSTOM_1  : RISCVOpcode<"CUSTOM_1",  0b0101011>;
def OPC_AMO       : RISCVOpcode<"AMO",       0b0101111>;
def OPC_OP        : RISCVOpcode<"OP",        0b0110011>;
def OPC_LUI       : RISCVOpcode<"LUI",       0b0110111>;
def OPC_OP_32     : RISCVOpcode<"OP_32",     0b0111011>;
def OPC_MADD      : RISCVOpcode<"MADD",      0b1000011>;
def OPC_MSUB      : RISCVOpcode<"MSUB",      0b1000111>;
def OPC_NMSUB     : RISCVOpcode<"NMSUB",     0b1001011>;
def OPC_NMADD     : RISCVOpcode<"NMADD",     0b1001111>;
def OPC_OP_FP     : RISCVOpcode<"OP_FP",     0b1010011>;
def OPC_OP_V      : RISCVOpcode<"OP_V",      0b1010111>;
def OPC_CUSTOM_2  : RISCVOpcode<"CUSTOM_2",  0b1011011>;
def OPC_BRANCH    : RISCVOpcode<"BRANCH",    0b1100011>;
def OPC_JALR      : RISCVOpcode<"JALR",      0b1100111>;
def OPC_JAL       : RISCVOpcode<"JAL",       0b1101111>;
def OPC_SYSTEM    : RISCVOpcode<"SYSTEM",    0b1110011>;
def OPC_OP_VE     : RISCVOpcode<"OP_VE",     0b1110111>;
def OPC_CUSTOM_3  : RISCVOpcode<"CUSTOM_3",  0b1111011>;

class RVInstCommon<dag outs, dag ins, string opcodestr, string argstr,
                   list<dag> pattern, InstFormat format> : Instruction {
  let Namespace = "RISCV";

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = opcodestr # !if(!empty(argstr), "", "\t" # argstr);
  let Pattern = pattern;

  let TSFlags{4-0} = format.Value;

  // Defaults
  RISCVVConstraint RVVConstraint = NoConstraint;
  let TSFlags{7-5} = RVVConstraint.Value;

  bits<3> VLMul = 0;
  let TSFlags{10-8} = VLMul;

  bit ForceTailAgnostic = false;
  let TSFlags{11} = ForceTailAgnostic;

  bit IsTiedPseudo = 0;
  let TSFlags{12} = IsTiedPseudo;

  bit HasSEWOp = 0;
  let TSFlags{13} = HasSEWOp;

  bit HasVLOp = 0;
  let TSFlags{14} = HasVLOp;

  bit HasVecPolicyOp = 0;
  let TSFlags{15} = HasVecPolicyOp;

  bit IsRVVWideningReduction = 0;
  let TSFlags{16} = IsRVVWideningReduction;

  bit UsesMaskPolicy = 0;
  let TSFlags{17} = UsesMaskPolicy;

  // Indicates that the result can be considered sign extended from bit 31. Some
  // instructions with this flag aren't W instructions, but are either sign
  // extended from a smaller size, always outputs a small integer, or put zeros
  // in bits 63:31. Used by the SExtWRemoval pass.
  bit IsSignExtendingOpW = 0;
  let TSFlags{18} = IsSignExtendingOpW;

  bit HasRoundModeOp = 0;
  let TSFlags{19} =  HasRoundModeOp;

  // This is only valid when HasRoundModeOp is set to 1. HasRoundModeOp is set
  // to 1 for vector fixed-point or floating-point intrinsics. This bit is
  // processed under pass 'RISCVInsertReadWriteCSR' pass to distinguish between
  // fixed-point / floating-point instructions and emit appropriate read/write
  // to the correct CSR.
  bit UsesVXRM = 0;
  let TSFlags{20} =  UsesVXRM;

  // Indicates whther these instructions can partially overlap between source
  // registers and destination registers according to the vector spec.
  // 0 -> not a vector pseudo
  // 1 -> default value for vector pseudos. not widening or narrowing.
  // 2 -> narrowing case
  // 3 -> widening case
  bits<2> TargetOverlapConstraintType = 0;
  let TSFlags{22-21} = TargetOverlapConstraintType;
}

class RVInst<dag outs, dag ins, string opcodestr, string argstr,
             list<dag> pattern, InstFormat format>
    : RVInstCommon<outs, ins, opcodestr, argstr, pattern, format> {
  field bits<32> Inst;
  // SoftFail is a field the disassembler can use to provide a way for
  // instructions to not match without killing the whole decode process. It is
  // mainly used for ARM, but Tablegen expects this field to exist or it fails
  // to build the decode table.
  field bits<32> SoftFail = 0;
  let Size = 4;
}

// Pseudo instructions
class Pseudo<dag outs, dag ins, list<dag> pattern, string opcodestr = "", string argstr = "">
    : RVInst<outs, ins, opcodestr, argstr, pattern, InstFormatPseudo> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

class PseudoQuietFCMP<DAGOperand Ty>
    : Pseudo<(outs GPR:$rd), (ins Ty:$rs1, Ty:$rs2), []> {
  let hasSideEffects = 1;
  let mayLoad = 0;
  let mayStore = 0;
}

// Pseudo load instructions.
class PseudoLoad<string opcodestr>
    : Pseudo<(outs GPR:$rd), (ins bare_symbol:$addr), [], opcodestr, "$rd, $addr"> {
  let hasSideEffects = 0;
  let mayLoad = 1;
  let mayStore = 0;
  let isCodeGenOnly = 0;
  let isAsmParserOnly = 1;
}

class PseudoFloatLoad<string opcodestr, RegisterClass rdty>
    : Pseudo<(outs GPR:$tmp, rdty:$rd), (ins bare_symbol:$addr), [], opcodestr, "$rd, $addr, $tmp"> {
  let hasSideEffects = 0;
  let mayLoad = 1;
  let mayStore = 0;
  let isCodeGenOnly = 0;
  let isAsmParserOnly = 1;
}

// Pseudo store instructions.
class PseudoStore<string opcodestr, RegisterClass rsty = GPR>
    : Pseudo<(outs GPR:$tmp), (ins rsty:$rs, bare_symbol:$addr), [], opcodestr, "$rs, $addr, $tmp"> {
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 1;
  let isCodeGenOnly = 0;
  let isAsmParserOnly = 1;
}

// Instruction formats are listed in the order they appear in the RISC-V
// instruction set manual (R, R4, I, S, B, U, J).

// Common base class for R format instructions. Bits {31-25} should be set by
// the subclasses.
class RVInstRBase<bits<3> funct3, RISCVOpcode opcode, dag outs,
                  dag ins, string opcodestr, string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatR> {
  bits<5> rs2;
  bits<5> rs1;
  bits<5> rd;

  let Inst{24-20} = rs2;
  let Inst{19-15} = rs1;
  let Inst{14-12} = funct3;
  let Inst{11-7} = rd;
  let Inst{6-0} = opcode.Value;
}

class RVInstR<bits<7> funct7, bits<3> funct3, RISCVOpcode opcode, dag outs,
              dag ins, string opcodestr, string argstr>
    : RVInstRBase<funct3, opcode, outs, ins, opcodestr, argstr> {
  let Inst{31-25} = funct7;
}

class RVInstRAtomic<bits<5> funct5, bit aq, bit rl, bits<3> funct3,
                    RISCVOpcode opcode, dag outs, dag ins, string opcodestr,
                    string argstr>
    : RVInstRBase<funct3, opcode, outs, ins, opcodestr, argstr> {
  let Inst{31-27} = funct5;
  let Inst{26} = aq;
  let Inst{25} = rl;
}

class RVInstRFrm<bits<7> funct7, RISCVOpcode opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatR> {
  bits<5> rs2;
  bits<5> rs1;
  bits<3> frm;
  bits<5> rd;

  let Inst{31-25} = funct7;
  let Inst{24-20} = rs2;
  let Inst{19-15} = rs1;
  let Inst{14-12} = frm;
  let Inst{11-7} = rd;
  let Inst{6-0} = opcode.Value;
}

class RVInstR4<bits<2> funct2, bits<3> funct3, RISCVOpcode opcode, dag outs,
               dag ins, string opcodestr, string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatR4> {
  bits<5> rs3;
  bits<5> rs2;
  bits<5> rs1;
  bits<5> rd;

  let Inst{31-27} = rs3;
  let Inst{26-25} = funct2;
  let Inst{24-20} = rs2;
  let Inst{19-15} = rs1;
  let Inst{14-12} = funct3;
  let Inst{11-7} = rd;
  let Inst{6-0} = opcode.Value;
}

class RVInstR4Frm<bits<2> funct2, RISCVOpcode opcode, dag outs, dag ins,
                  string opcodestr, string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatR4> {
  bits<5> rs3;
  bits<5> rs2;
  bits<5> rs1;
  bits<3> frm;
  bits<5> rd;

  let Inst{31-27} = rs3;
  let Inst{26-25} = funct2;
  let Inst{24-20} = rs2;
  let Inst{19-15} = rs1;
  let Inst{14-12} = frm;
  let Inst{11-7} = rd;
  let Inst{6-0} = opcode.Value;
}

// Common base class for I format instructions. Bits {31-20} should be set by
// the subclasses.
class RVInstIBase<bits<3> funct3, RISCVOpcode opcode, dag outs, dag ins,
                  string opcodestr, string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatI> {
  bits<5> rs1;
  bits<5> rd;

  let Inst{19-15} = rs1;
  let Inst{14-12} = funct3;
  let Inst{11-7} = rd;
  let Inst{6-0} = opcode.Value;
}

class RVInstI<bits<3> funct3, RISCVOpcode opcode, dag outs, dag ins,
              string opcodestr, string argstr>
    : RVInstIBase<funct3, opcode, outs, ins, opcodestr, argstr> {
  bits<12> imm12;

  let Inst{31-20} = imm12;
}

class RVInstIShift<bits<5> imm11_7, bits<3> funct3, RISCVOpcode opcode,
                   dag outs, dag ins, string opcodestr, string argstr>
    : RVInstIBase<funct3, opcode, outs, ins, opcodestr, argstr> {
  bits<6> shamt;

  let Inst{31-27} = imm11_7;
  let Inst{26} = 0;
  let Inst{25-20} = shamt;
}

class RVInstIShiftW<bits<7> imm11_5, bits<3> funct3, RISCVOpcode opcode,
                    dag outs, dag ins, string opcodestr, string argstr>
    : RVInstIBase<funct3, opcode, outs, ins, opcodestr, argstr> {
  bits<5> shamt;

  let Inst{31-25} = imm11_5;
  let Inst{24-20} = shamt;
}

class RVInstIUnary<bits<12> imm12, bits<3> funct3, RISCVOpcode opcode,
                   dag outs, dag ins, string opcodestr, string argstr>
    : RVInstIBase<funct3, opcode, outs, ins, opcodestr, argstr> {
  let Inst{31-20} = imm12;
}

class RVInstS<bits<3> funct3, RISCVOpcode opcode, dag outs, dag ins,
              string opcodestr, string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatS> {
  bits<12> imm12;
  bits<5> rs2;
  bits<5> rs1;

  let Inst{31-25} = imm12{11-5};
  let Inst{24-20} = rs2;
  let Inst{19-15} = rs1;
  let Inst{14-12} = funct3;
  let Inst{11-7} = imm12{4-0};
  let Inst{6-0} = opcode.Value;
}

class RVInstB<bits<3> funct3, RISCVOpcode opcode, dag outs, dag ins,
              string opcodestr, string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatB> {
  bits<12> imm12;
  bits<5> rs2;
  bits<5> rs1;

  let Inst{31} = imm12{11};
  let Inst{30-25} = imm12{9-4};
  let Inst{24-20} = rs2;
  let Inst{19-15} = rs1;
  let Inst{14-12} = funct3;
  let Inst{11-8} = imm12{3-0};
  let Inst{7} = imm12{10};
  let Inst{6-0} = opcode.Value;
}

class RVInstU<RISCVOpcode opcode, dag outs, dag ins, string opcodestr,
              string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatU> {
  bits<20> imm20;
  bits<5> rd;

  let Inst{31-12} = imm20;
  let Inst{11-7} = rd;
  let Inst{6-0} = opcode.Value;
}

class RVInstJ<RISCVOpcode opcode, dag outs, dag ins, string opcodestr,
              string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatJ> {
  bits<20> imm20;
  bits<5> rd;

  let Inst{31} = imm20{19};
  let Inst{30-21} = imm20{9-0};
  let Inst{20} = imm20{10};
  let Inst{19-12} = imm20{18-11};
  let Inst{11-7} = rd;
  let Inst{6-0} = opcode.Value;
}

//===----------------------------------------------------------------------===//
// Instruction classes for .insn directives
//===----------------------------------------------------------------------===//

class DirectiveInsnR<dag outs, dag ins, string argstr>
  : RVInst<outs, ins, "", "", [], InstFormatR> {
  bits<7> opcode;
  bits<7> funct7;
  bits<3> funct3;

  bits<5> rs2;
  bits<5> rs1;
  bits<5> rd;

  let Inst{31-25} = funct7;
  let Inst{24-20} = rs2;
  let Inst{19-15} = rs1;
  let Inst{14-12} = funct3;
  let Inst{11-7} = rd;
  let Inst{6-0} = opcode;

  let AsmString = ".insn r " # argstr;
}

class DirectiveInsnR4<dag outs, dag ins, string argstr>
  : RVInst<outs, ins, "", "", [], InstFormatR4> {
  bits<7> opcode;
  bits<2> funct2;
  bits<3> funct3;

  bits<5> rs3;
  bits<5> rs2;
  bits<5> rs1;
  bits<5> rd;

  let Inst{31-27} = rs3;
  let Inst{26-25} = funct2;
  let Inst{24-20} = rs2;
  let Inst{19-15} = rs1;
  let Inst{14-12} = funct3;
  let Inst{11-7} = rd;
  let Inst{6-0} = opcode;

  let AsmString = ".insn r4 " # argstr;
}

class DirectiveInsnI<dag outs, dag ins, string argstr>
  : RVInst<outs, ins, "", "", [], InstFormatI> {
  bits<7> opcode;
  bits<3> funct3;

  bits<12> imm12;
  bits<5> rs1;
  bits<5> rd;

  let Inst{31-20} = imm12;
  let Inst{19-15} = rs1;
  let Inst{14-12} = funct3;
  let Inst{11-7} = rd;
  let Inst{6-0} = opcode;

  let AsmString = ".insn i " # argstr;
}

class DirectiveInsnS<dag outs, dag ins, string argstr>
  : RVInst<outs, ins, "", "", [], InstFormatS> {
  bits<7> opcode;
  bits<3> funct3;

  bits<12> imm12;
  bits<5> rs2;
  bits<5> rs1;

  let Inst{31-25} = imm12{11-5};
  let Inst{24-20} = rs2;
  let Inst{19-15} = rs1;
  let Inst{14-12} = funct3;
  let Inst{11-7} = imm12{4-0};
  let Inst{6-0} = opcode;

  let AsmString = ".insn s " # argstr;
}

class DirectiveInsnB<dag outs, dag ins, string argstr>
  : RVInst<outs, ins, "", "", [], InstFormatB> {
  bits<7> opcode;
  bits<3> funct3;

  bits<12> imm12;
  bits<5> rs2;
  bits<5> rs1;

  let Inst{31} = imm12{11};
  let Inst{30-25} = imm12{9-4};
  let Inst{24-20} = rs2;
  let Inst{19-15} = rs1;
  let Inst{14-12} = funct3;
  let Inst{11-8} = imm12{3-0};
  let Inst{7} = imm12{10};
  let Inst{6-0} = opcode;

  let AsmString = ".insn b " # argstr;
}

class DirectiveInsnU<dag outs, dag ins, string argstr>
  : RVInst<outs, ins, "", "", [], InstFormatU> {
  bits<7> opcode;

  bits<20> imm20;
  bits<5> rd;

  let Inst{31-12} = imm20;
  let Inst{11-7} = rd;
  let Inst{6-0} = opcode;

  let AsmString = ".insn u " # argstr;
}

class DirectiveInsnJ<dag outs, dag ins, string argstr>
  : RVInst<outs, ins, "", "", [], InstFormatJ> {
  bits<7> opcode;

  bits<20> imm20;
  bits<5> rd;

  let Inst{31-12} = imm20;
  let Inst{11-7} = rd;
  let Inst{6-0} = opcode;

  let AsmString = ".insn j " # argstr;
}
