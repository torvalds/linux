//===- RISCVInstrInfoC.td - Compressed RISC-V instructions -*- tblgen-*----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "RISCVInstrFormatsC.td"

//===----------------------------------------------------------------------===//
// Operand definitions.
//===----------------------------------------------------------------------===//

def UImmLog2XLenNonZeroAsmOperand : AsmOperandClass {
  let Name = "UImmLog2XLenNonZero";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidUImmLog2XLenNonZero";
}

def uimmlog2xlennonzero : RISCVOp, ImmLeaf<XLenVT, [{
  if (Subtarget->is64Bit())
    return isUInt<6>(Imm) && (Imm != 0);
  return isUInt<5>(Imm) && (Imm != 0);
}]> {
  let ParserMatchClass = UImmLog2XLenNonZeroAsmOperand;
  // TODO: should ensure invalid shamt is rejected when decoding.
  let DecoderMethod = "decodeUImmNonZeroOperand<6>";
  let OperandType = "OPERAND_UIMMLOG2XLEN_NONZERO";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    if (STI.getTargetTriple().isArch64Bit())
      return  isUInt<6>(Imm) && (Imm != 0);
    return isUInt<5>(Imm) && (Imm != 0);
  }];
}

def simm6 : RISCVSImmLeafOp<6> {
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isInt<6>(Imm);
    return MCOp.isBareSymbolRef();
  }];
}

def simm6nonzero : RISCVOp,
                   ImmLeaf<XLenVT, [{return (Imm != 0) && isInt<6>(Imm);}]> {
  let ParserMatchClass = SImmAsmOperand<6, "NonZero">;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeSImmNonZeroOperand<6>";
  let OperandType = "OPERAND_SIMM6_NONZERO";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return (Imm != 0) && isInt<6>(Imm);
    return MCOp.isBareSymbolRef();
  }];
}

def immzero : RISCVOp,
              ImmLeaf<XLenVT, [{return (Imm == 0);}]> {
  let ParserMatchClass = ImmZeroAsmOperand;
  let OperandType = "OPERAND_ZERO";
}

def CLUIImmAsmOperand : AsmOperandClass {
  let Name = "CLUIImm";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = !strconcat("Invalid", Name);
}


// c_lui_imm checks the immediate range is in [1, 31] or [0xfffe0, 0xfffff].
// The RISC-V ISA describes the constraint as [1, 63], with that value being
// loaded in to bits 17-12 of the destination register and sign extended from
// bit 17. Therefore, this 6-bit immediate can represent values in the ranges
// [1, 31] and [0xfffe0, 0xfffff].
def c_lui_imm : RISCVOp,
                ImmLeaf<XLenVT, [{return (Imm != 0) &&
                                 (isUInt<5>(Imm) ||
                                  (Imm >= 0xfffe0 && Imm <= 0xfffff));}]> {
  let ParserMatchClass = CLUIImmAsmOperand;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeCLUIImmOperand";
  let OperandType = "OPERAND_CLUI_IMM";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return (Imm != 0) && (isUInt<5>(Imm) ||
             (Imm >= 0xfffe0 && Imm <= 0xfffff));
    return MCOp.isBareSymbolRef();
  }];
}

// A 7-bit unsigned immediate where the least significant two bits are zero.
def uimm7_lsb00 : RISCVOp,
                  ImmLeaf<XLenVT, [{return isShiftedUInt<5, 2>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<7, "Lsb00">;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeUImmOperand<7>";
  let OperandType = "OPERAND_UIMM7_LSB00";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isShiftedUInt<5, 2>(Imm);
  }];
}

// A 8-bit unsigned immediate where the least significant two bits are zero.
def uimm8_lsb00 : RISCVOp,
                  ImmLeaf<XLenVT, [{return isShiftedUInt<6, 2>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<8, "Lsb00">;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeUImmOperand<8>";
  let OperandType = "OPERAND_UIMM8_LSB00";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isShiftedUInt<6, 2>(Imm);
  }];
}

// A 8-bit unsigned immediate where the least significant three bits are zero.
def uimm8_lsb000 : RISCVOp,
                   ImmLeaf<XLenVT, [{return isShiftedUInt<5, 3>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<8, "Lsb000">;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeUImmOperand<8>";
  let OperandType = "OPERAND_UIMM8_LSB000";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isShiftedUInt<5, 3>(Imm);
  }];
}

// A 9-bit signed immediate where the least significant bit is zero.
def simm9_lsb0 : Operand<OtherVT>,
                 ImmLeaf<XLenVT, [{return isShiftedInt<8, 1>(Imm);}]> {
  let ParserMatchClass = SImmAsmOperand<9, "Lsb0">;
  let PrintMethod = "printBranchOperand";
  let EncoderMethod = "getImmOpValueAsr1";
  let DecoderMethod = "decodeSImmOperandAndLsl1<9>";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isShiftedInt<8, 1>(Imm);
    return MCOp.isBareSymbolRef();

  }];
  let OperandType = "OPERAND_PCREL";
}

// A 9-bit unsigned immediate where the least significant three bits are zero.
def uimm9_lsb000 : RISCVOp,
                   ImmLeaf<XLenVT, [{return isShiftedUInt<6, 3>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<9, "Lsb000">;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeUImmOperand<9>";
  let OperandType = "OPERAND_UIMM9_LSB000";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isShiftedUInt<6, 3>(Imm);
  }];
}

// A 10-bit unsigned immediate where the least significant two bits are zero
// and the immediate can't be zero.
def uimm10_lsb00nonzero : RISCVOp,
                          ImmLeaf<XLenVT,
                          [{return isShiftedUInt<8, 2>(Imm) && (Imm != 0);}]> {
  let ParserMatchClass = UImmAsmOperand<10, "Lsb00NonZero">;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeUImmNonZeroOperand<10>";
  let OperandType = "OPERAND_UIMM10_LSB00_NONZERO";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isShiftedUInt<8, 2>(Imm) && (Imm != 0);
  }];
}

// A 10-bit signed immediate where the least significant four bits are zero.
def simm10_lsb0000nonzero : RISCVOp,
                            ImmLeaf<XLenVT,
                            [{return (Imm != 0) && isShiftedInt<6, 4>(Imm);}]> {
  let ParserMatchClass = SImmAsmOperand<10, "Lsb0000NonZero">;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeSImmNonZeroOperand<10>";
  let OperandType = "OPERAND_SIMM10_LSB0000_NONZERO";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isShiftedInt<6, 4>(Imm) && (Imm != 0);
  }];
}

// A 12-bit signed immediate where the least significant bit is zero.
def simm12_lsb0 : Operand<XLenVT>,
                  ImmLeaf<XLenVT, [{return isShiftedInt<11, 1>(Imm);}]> {
  let ParserMatchClass = SImmAsmOperand<12, "Lsb0">;
  let PrintMethod = "printBranchOperand";
  let EncoderMethod = "getImmOpValueAsr1";
  let DecoderMethod = "decodeSImmOperandAndLsl1<12>";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isShiftedInt<11, 1>(Imm);
    return MCOp.isBareSymbolRef();
  }];
  let OperandType = "OPERAND_PCREL";
}

def InsnCDirectiveOpcode : AsmOperandClass {
  let Name = "InsnCDirectiveOpcode";
  let ParserMethod = "parseInsnCDirectiveOpcode";
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isImm";
}

def uimm2_opcode : RISCVOp {
  let ParserMatchClass = InsnCDirectiveOpcode;
  let DecoderMethod = "decodeUImmOperand<2>";
  let OperandType = "OPERAND_UIMM2";
}

//===----------------------------------------------------------------------===//
// Instruction Class Templates
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
class CStackLoad<bits<3> funct3, string OpcodeStr,
                 RegisterClass cls, DAGOperand opnd>
    : RVInst16CI<funct3, 0b10, (outs cls:$rd), (ins SPMem:$rs1, opnd:$imm),
                 OpcodeStr, "$rd, ${imm}(${rs1})">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
class CStackStore<bits<3> funct3, string OpcodeStr,
                  RegisterClass cls, DAGOperand opnd>
    : RVInst16CSS<funct3, 0b10, (outs), (ins cls:$rs2, SPMem:$rs1, opnd:$imm),
                  OpcodeStr, "$rs2, ${imm}(${rs1})">;

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
class CLoad_ri<bits<3> funct3, string OpcodeStr,
               RegisterClass cls, DAGOperand opnd>
    : RVInst16CL<funct3, 0b00, (outs cls:$rd), (ins GPRCMem:$rs1, opnd:$imm),
                 OpcodeStr, "$rd, ${imm}(${rs1})">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
class CStore_rri<bits<3> funct3, string OpcodeStr,
                 RegisterClass cls, DAGOperand opnd>
    : RVInst16CS<funct3, 0b00, (outs), (ins cls:$rs2,GPRCMem:$rs1, opnd:$imm),
                 OpcodeStr, "$rs2, ${imm}(${rs1})">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class Bcz<bits<3> funct3, string OpcodeStr,
          RegisterClass cls>
    : RVInst16CB<funct3, 0b01, (outs), (ins cls:$rs1, simm9_lsb0:$imm),
                 OpcodeStr, "$rs1, $imm"> {
  let isBranch = 1;
  let isTerminator = 1;
  let Inst{12} = imm{7};
  let Inst{11-10} = imm{3-2};
  let Inst{6-5} = imm{6-5};
  let Inst{4-3} = imm{1-0};
  let Inst{2} = imm{4};
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class Shift_right<bits<2> funct2, string OpcodeStr, RegisterClass cls,
                  Operand ImmOpnd>
    : RVInst16CB<0b100, 0b01, (outs cls:$rs1_wb), (ins cls:$rs1, ImmOpnd:$imm),
                 OpcodeStr, "$rs1, $imm"> {
  let Constraints = "$rs1 = $rs1_wb";
  let Inst{12} = imm{5};
  let Inst{11-10} = funct2;
  let Inst{6-2} = imm{4-0};
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class CA_ALU<bits<6> funct6, bits<2> funct2, string OpcodeStr,
             RegisterClass cls>
    : RVInst16CA<funct6, funct2, 0b01, (outs cls:$rd_wb), (ins cls:$rd, cls:$rs2),
                 OpcodeStr, "$rd, $rs2"> {
  bits<3> rd;
  let Constraints = "$rd = $rd_wb";
  let Inst{9-7} = rd;
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtCOrZca] in {

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, Uses = [X2] in
def C_ADDI4SPN : RVInst16CIW<0b000, 0b00, (outs GPRC:$rd),
                             (ins SP:$rs1, uimm10_lsb00nonzero:$imm),
                             "c.addi4spn", "$rd, $rs1, $imm">,
                             Sched<[WriteIALU, ReadIALU]> {
  bits<5> rs1;
  let Inst{12-11} = imm{5-4};
  let Inst{10-7} = imm{9-6};
  let Inst{6} = imm{2};
  let Inst{5} = imm{3};
}

let Predicates = [HasStdExtCOrZcd, HasStdExtD] in
def C_FLD  : CLoad_ri<0b001, "c.fld", FPR64C, uimm8_lsb000>,
             Sched<[WriteFLD64, ReadFMemBase]> {
  bits<8> imm;
  let Inst{12-10} = imm{5-3};
  let Inst{6-5} = imm{7-6};
}

def C_LW : CLoad_ri<0b010, "c.lw", GPRC, uimm7_lsb00>,
           Sched<[WriteLDW, ReadMemBase]> {
  bits<7> imm;
  let Inst{12-10} = imm{5-3};
  let Inst{6} = imm{2};
  let Inst{5} = imm{6};
}

let DecoderNamespace = "RISCV32Only_",
    Predicates = [HasStdExtCOrZcfOrZce, HasStdExtF, IsRV32] in
def C_FLW  : CLoad_ri<0b011, "c.flw", FPR32C, uimm7_lsb00>,
             Sched<[WriteFLD32, ReadFMemBase]> {
  bits<7> imm;
  let Inst{12-10} = imm{5-3};
  let Inst{6} = imm{2};
  let Inst{5} = imm{6};
}

let Predicates = [HasStdExtCOrZca, IsRV64] in
def C_LD : CLoad_ri<0b011, "c.ld", GPRC, uimm8_lsb000>,
           Sched<[WriteLDD, ReadMemBase]> {
  bits<8> imm;
  let Inst{12-10} = imm{5-3};
  let Inst{6-5} = imm{7-6};
}

let Predicates = [HasStdExtCOrZcd, HasStdExtD] in
def C_FSD  : CStore_rri<0b101, "c.fsd", FPR64C, uimm8_lsb000>,
             Sched<[WriteFST64, ReadFStoreData, ReadFMemBase]> {
  bits<8> imm;
  let Inst{12-10} = imm{5-3};
  let Inst{6-5} = imm{7-6};
}

def C_SW : CStore_rri<0b110, "c.sw", GPRC, uimm7_lsb00>,
           Sched<[WriteSTW, ReadStoreData, ReadMemBase]> {
  bits<7> imm;
  let Inst{12-10} = imm{5-3};
  let Inst{6} = imm{2};
  let Inst{5} = imm{6};
}

let DecoderNamespace = "RISCV32Only_",
    Predicates = [HasStdExtCOrZcfOrZce, HasStdExtF, IsRV32]  in
def C_FSW  : CStore_rri<0b111, "c.fsw", FPR32C, uimm7_lsb00>,
             Sched<[WriteFST32, ReadFStoreData, ReadFMemBase]> {
  bits<7> imm;
  let Inst{12-10} = imm{5-3};
  let Inst{6} = imm{2};
  let Inst{5} = imm{6};
}

let Predicates = [HasStdExtCOrZca, IsRV64] in
def C_SD : CStore_rri<0b111, "c.sd", GPRC, uimm8_lsb000>,
           Sched<[WriteSTD, ReadStoreData, ReadMemBase]> {
  bits<8> imm;
  let Inst{12-10} = imm{5-3};
  let Inst{6-5} = imm{7-6};
}

let rd = 0, imm = 0, hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def C_NOP : RVInst16CI<0b000, 0b01, (outs), (ins), "c.nop", "">,
            Sched<[WriteNop]> {
  let Inst{6-2} = 0;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def C_ADDI : RVInst16CI<0b000, 0b01, (outs GPRNoX0:$rd_wb),
                        (ins GPRNoX0:$rd, simm6nonzero:$imm),
                        "c.addi", "$rd, $imm">,
             Sched<[WriteIALU, ReadIALU]> {
  let Constraints = "$rd = $rd_wb";
  let Inst{6-2} = imm{4-0};
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def C_ADDI_NOP : RVInst16CI<0b000, 0b01, (outs GPRX0:$rd_wb),
                            (ins GPRX0:$rd, immzero:$imm),
                            "c.addi", "$rd, $imm">,
                 Sched<[WriteIALU, ReadIALU]> {
  let Constraints = "$rd = $rd_wb";
  let Inst{6-2} = 0;
  let isAsmParserOnly = 1;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isCall = 1,
    DecoderNamespace = "RISCV32Only_", Defs = [X1],
    Predicates = [HasStdExtCOrZca, IsRV32]  in
def C_JAL : RVInst16CJ<0b001, 0b01, (outs), (ins simm12_lsb0:$offset),
                       "c.jal", "$offset">, Sched<[WriteJal]>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0,
    Predicates = [HasStdExtCOrZca, IsRV64] in
def C_ADDIW : RVInst16CI<0b001, 0b01, (outs GPRNoX0:$rd_wb),
                         (ins GPRNoX0:$rd, simm6:$imm),
                         "c.addiw", "$rd, $imm">,
              Sched<[WriteIALU32, ReadIALU32]> {
  let Constraints = "$rd = $rd_wb";
  let Inst{6-2} = imm{4-0};
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def C_LI : RVInst16CI<0b010, 0b01, (outs GPRNoX0:$rd), (ins simm6:$imm),
                      "c.li", "$rd, $imm">,
           Sched<[WriteIALU]> {
  let Inst{6-2} = imm{4-0};
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def C_ADDI16SP : RVInst16CI<0b011, 0b01, (outs SP:$rd_wb),
                            (ins SP:$rd, simm10_lsb0000nonzero:$imm),
                            "c.addi16sp", "$rd, $imm">,
                 Sched<[WriteIALU, ReadIALU]> {
  let Constraints = "$rd = $rd_wb";
  let Inst{12} = imm{9};
  let Inst{11-7} = 2;
  let Inst{6} = imm{4};
  let Inst{5} = imm{6};
  let Inst{4-3} = imm{8-7};
  let Inst{2} = imm{5};
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def C_LUI : RVInst16CI<0b011, 0b01, (outs GPRNoX0X2:$rd),
                       (ins c_lui_imm:$imm),
                       "c.lui", "$rd, $imm">,
            Sched<[WriteIALU]> {
  let Inst{6-2} = imm{4-0};
}

def C_SRLI : Shift_right<0b00, "c.srli", GPRC, uimmlog2xlennonzero>,
             Sched<[WriteShiftImm, ReadShiftImm]>;
def C_SRAI : Shift_right<0b01, "c.srai", GPRC, uimmlog2xlennonzero>,
             Sched<[WriteShiftImm, ReadShiftImm]>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def C_ANDI : RVInst16CB<0b100, 0b01, (outs GPRC:$rs1_wb), (ins GPRC:$rs1, simm6:$imm),
                        "c.andi", "$rs1, $imm">,
             Sched<[WriteIALU, ReadIALU]> {
  let Constraints = "$rs1 = $rs1_wb";
  let Inst{12} = imm{5};
  let Inst{11-10} = 0b10;
  let Inst{6-2} = imm{4-0};
}

def C_SUB  : CA_ALU<0b100011, 0b00, "c.sub", GPRC>,
             Sched<[WriteIALU, ReadIALU, ReadIALU]>;
def C_XOR  : CA_ALU<0b100011, 0b01, "c.xor", GPRC>,
             Sched<[WriteIALU, ReadIALU, ReadIALU]>;
def C_OR   : CA_ALU<0b100011, 0b10, "c.or" , GPRC>,
             Sched<[WriteIALU, ReadIALU, ReadIALU]>;
def C_AND  : CA_ALU<0b100011, 0b11, "c.and", GPRC>,
             Sched<[WriteIALU, ReadIALU, ReadIALU]>;

let Predicates = [HasStdExtCOrZca, IsRV64] in {
def C_SUBW : CA_ALU<0b100111, 0b00, "c.subw", GPRC>,
             Sched<[WriteIALU32, ReadIALU32, ReadIALU32]>;
def C_ADDW : CA_ALU<0b100111, 0b01, "c.addw", GPRC>,
             Sched<[WriteIALU32, ReadIALU32, ReadIALU32]>;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def C_J : RVInst16CJ<0b101, 0b01, (outs), (ins simm12_lsb0:$offset),
                     "c.j", "$offset">, Sched<[WriteJmp]> {
  let isBranch = 1;
  let isTerminator=1;
  let isBarrier=1;
}

def C_BEQZ : Bcz<0b110, "c.beqz", GPRC>, Sched<[WriteJmp, ReadJmp]>;
def C_BNEZ : Bcz<0b111, "c.bnez", GPRC>, Sched<[WriteJmp, ReadJmp]>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def C_SLLI : RVInst16CI<0b000, 0b10, (outs GPRNoX0:$rd_wb),
                        (ins GPRNoX0:$rd, uimmlog2xlennonzero:$imm),
                        "c.slli", "$rd, $imm">,
             Sched<[WriteShiftImm, ReadShiftImm]> {
  let Constraints = "$rd = $rd_wb";
  let Inst{6-2} = imm{4-0};
}

let Predicates = [HasStdExtCOrZcd, HasStdExtD] in
def C_FLDSP  : CStackLoad<0b001, "c.fldsp", FPR64, uimm9_lsb000>,
               Sched<[WriteFLD64, ReadFMemBase]> {
  let Inst{6-5} = imm{4-3};
  let Inst{4-2} = imm{8-6};
}

def C_LWSP : CStackLoad<0b010, "c.lwsp", GPRNoX0, uimm8_lsb00>,
             Sched<[WriteLDW, ReadMemBase]> {
  let Inst{6-4} = imm{4-2};
  let Inst{3-2} = imm{7-6};
}

let DecoderNamespace = "RISCV32Only_",
    Predicates = [HasStdExtCOrZcfOrZce, HasStdExtF, IsRV32] in
def C_FLWSP  : CStackLoad<0b011, "c.flwsp", FPR32, uimm8_lsb00>,
               Sched<[WriteFLD32, ReadFMemBase]> {
  let Inst{6-4} = imm{4-2};
  let Inst{3-2} = imm{7-6};
}

let Predicates = [HasStdExtCOrZca, IsRV64] in
def C_LDSP : CStackLoad<0b011, "c.ldsp", GPRNoX0, uimm9_lsb000>,
             Sched<[WriteLDD, ReadMemBase]> {
  let Inst{6-5} = imm{4-3};
  let Inst{4-2} = imm{8-6};
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def C_JR : RVInst16CR<0b1000, 0b10, (outs), (ins GPRNoX0:$rs1),
                      "c.jr", "$rs1">, Sched<[WriteJalr, ReadJalr]> {
  let isBarrier = 1;
  let isTerminator = 1;
  let rs2 = 0;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isMoveReg = 1,
    isAsCheapAsAMove = 1 in
def C_MV : RVInst16CR<0b1000, 0b10, (outs GPRNoX0:$rs1), (ins GPRNoX0:$rs2),
                      "c.mv", "$rs1, $rs2">,
           Sched<[WriteIALU, ReadIALU]>;

let rs1 = 0, rs2 = 0, hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
def C_EBREAK : RVInst16CR<0b1001, 0b10, (outs), (ins), "c.ebreak", "">, Sched<[]>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0,
    isCall=1, Defs=[X1], rs2 = 0 in
def C_JALR : RVInst16CR<0b1001, 0b10, (outs), (ins GPRNoX0:$rs1),
                        "c.jalr", "$rs1">, Sched<[WriteJalr, ReadJalr]>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def C_ADD : RVInst16CR<0b1001, 0b10, (outs GPRNoX0:$rs1_wb),
                       (ins GPRNoX0:$rs1, GPRNoX0:$rs2),
                       "c.add", "$rs1, $rs2">,
            Sched<[WriteIALU, ReadIALU, ReadIALU]> {
  let Constraints = "$rs1 = $rs1_wb";
}

let Predicates = [HasStdExtCOrZcd, HasStdExtD] in
def C_FSDSP  : CStackStore<0b101, "c.fsdsp", FPR64, uimm9_lsb000>,
               Sched<[WriteFST64, ReadFStoreData, ReadFMemBase]> {
  let Inst{12-10} = imm{5-3};
  let Inst{9-7}   = imm{8-6};
}

def C_SWSP : CStackStore<0b110, "c.swsp", GPR, uimm8_lsb00>,
             Sched<[WriteSTW, ReadStoreData, ReadMemBase]> {
  let Inst{12-9} = imm{5-2};
  let Inst{8-7}  = imm{7-6};
}

let DecoderNamespace = "RISCV32Only_",
    Predicates = [HasStdExtCOrZcfOrZce, HasStdExtF, IsRV32] in
def C_FSWSP  : CStackStore<0b111, "c.fswsp", FPR32, uimm8_lsb00>,
               Sched<[WriteFST32, ReadFStoreData, ReadFMemBase]> {
  let Inst{12-9} = imm{5-2};
  let Inst{8-7}  = imm{7-6};
}

let Predicates = [HasStdExtCOrZca, IsRV64] in
def C_SDSP : CStackStore<0b111, "c.sdsp", GPR, uimm9_lsb000>,
             Sched<[WriteSTD, ReadStoreData, ReadMemBase]> {
  let Inst{12-10} = imm{5-3};
  let Inst{9-7}   = imm{8-6};
}

// The all zeros pattern isn't a valid RISC-V instruction. It's used by GNU
// binutils as 16-bit instruction known to be unimplemented (i.e., trapping).
let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
def C_UNIMP : RVInst16<(outs), (ins), "c.unimp", "", [], InstFormatOther>,
              Sched<[]> {
  let Inst{15-0} = 0;
}

} // Predicates = [HasStdExtCOrZca]

//===----------------------------------------------------------------------===//
// HINT Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtCOrZca, HasRVCHints], hasSideEffects = 0, mayLoad = 0,
    mayStore = 0 in {

let rd = 0 in
def C_NOP_HINT : RVInst16CI<0b000, 0b01, (outs), (ins simm6nonzero:$imm),
                            "c.nop", "$imm">, Sched<[WriteNop]> {
  let Inst{6-2} = imm{4-0};
}

def C_ADDI_HINT_IMM_ZERO : RVInst16CI<0b000, 0b01, (outs GPRNoX0:$rd_wb),
                                      (ins GPRNoX0:$rd, immzero:$imm),
                                      "c.addi", "$rd, $imm">,
                           Sched<[WriteIALU, ReadIALU]> {
  let Constraints = "$rd = $rd_wb";
  let Inst{12} = 0;
  let Inst{6-2} = 0;
  let DecoderMethod = "decodeRVCInstrRdRs1ImmZero";
}

def C_LI_HINT : RVInst16CI<0b010, 0b01, (outs GPRX0:$rd), (ins simm6:$imm),
                           "c.li", "$rd, $imm">,
                Sched<[WriteIALU]> {
  let Inst{6-2} = imm{4-0};
  let Inst{11-7} = 0;
  let DecoderMethod = "decodeRVCInstrRdSImm";
}

def C_LUI_HINT : RVInst16CI<0b011, 0b01, (outs GPRX0:$rd),
                            (ins c_lui_imm:$imm),
                            "c.lui", "$rd, $imm">,
                 Sched<[WriteIALU]> {
  let Inst{6-2} = imm{4-0};
  let Inst{11-7} = 0;
  let DecoderMethod = "decodeRVCInstrRdSImm";
}

def C_MV_HINT : RVInst16CR<0b1000, 0b10, (outs GPRX0:$rs1), (ins GPRNoX0:$rs2),
                           "c.mv", "$rs1, $rs2">, Sched<[WriteIALU, ReadIALU]> {
  let Inst{11-7} = 0;
  let DecoderMethod = "decodeRVCInstrRdRs2";
}

def C_ADD_HINT : RVInst16CR<0b1001, 0b10, (outs GPRX0:$rs1_wb),
                            (ins GPRX0:$rs1, GPRNoX0:$rs2),
                            "c.add", "$rs1, $rs2">,
                 Sched<[WriteIALU, ReadIALU, ReadIALU]> {
  let Constraints = "$rs1 = $rs1_wb";
  let Inst{11-7} = 0;
  let DecoderMethod = "decodeRVCInstrRdRs1Rs2";
}

def C_SLLI_HINT : RVInst16CI<0b000, 0b10, (outs GPRX0:$rd_wb),
                             (ins GPRX0:$rd, uimmlog2xlennonzero:$imm),
                             "c.slli", "$rd, $imm">,
                  Sched<[WriteShiftImm, ReadShiftImm]> {
  let Constraints = "$rd = $rd_wb";
  let Inst{6-2} = imm{4-0};
  let Inst{11-7} = 0;
  let DecoderMethod = "decodeRVCInstrRdRs1UImm";
}

def C_SLLI64_HINT : RVInst16CI<0b000, 0b10, (outs GPR:$rd_wb), (ins GPR:$rd),
                               "c.slli64", "$rd">,
                    Sched<[WriteShiftImm, ReadShiftImm]> {
  let Constraints = "$rd = $rd_wb";
  let Inst{6-2} = 0;
  let Inst{12} = 0;
}

def C_SRLI64_HINT : RVInst16CI<0b100, 0b01, (outs GPRC:$rd_wb),
                               (ins GPRC:$rd),
                               "c.srli64", "$rd">,
                    Sched<[WriteShiftImm, ReadShiftImm]> {
  let Constraints = "$rd = $rd_wb";
  let Inst{6-2} = 0;
  let Inst{11-10} = 0;
  let Inst{12} = 0;
}

def C_SRAI64_HINT : RVInst16CI<0b100, 0b01, (outs GPRC:$rd_wb),
                               (ins GPRC:$rd),
                               "c.srai64", "$rd">,
                    Sched<[WriteShiftImm, ReadShiftImm]> {
  let Constraints = "$rd = $rd_wb";
  let Inst{6-2} = 0;
  let Inst{11-10} = 1;
  let Inst{12} = 0;
}

} // Predicates = [HasStdExtCOrZca, HasRVCHints], hasSideEffects = 0, mayLoad = 0,
  // mayStore = 0

//===----------------------------------------------------------------------===//
// Assembler Pseudo Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtCOrZca, HasRVCHints] in {
// Just a different syntax for the c.nop hint: c.addi x0, simm6 vs c.nop simm6.
def : InstAlias<"c.addi x0, $imm", (C_NOP_HINT simm6nonzero:$imm), 0>;
}

let Predicates = [HasStdExtC, HasRVCHints, HasStdExtZihintntl] in {
def : InstAlias<"c.ntl.p1", (C_ADD_HINT X0, X2)>;
def : InstAlias<"c.ntl.pall", (C_ADD_HINT X0, X3)>;
def : InstAlias<"c.ntl.s1", (C_ADD_HINT X0, X4)>;
def : InstAlias<"c.ntl.all", (C_ADD_HINT X0, X5)>;
} // Predicates = [HasStdExtC, HasRVCHints, HasStdExtZihintntl]

let EmitPriority = 0 in {
let Predicates = [HasStdExtCOrZca] in {
def : InstAlias<"c.lw $rd, (${rs1})", (C_LW GPRC:$rd, GPRCMem:$rs1, 0)>;
def : InstAlias<"c.sw $rs2, (${rs1})", (C_SW GPRC:$rs2, GPRCMem:$rs1, 0)>;
def : InstAlias<"c.lwsp $rd, (${rs1})", (C_LWSP GPRNoX0:$rd, SPMem:$rs1, 0)>;
def : InstAlias<"c.swsp $rs2, (${rs1})", (C_SWSP GPRNoX0:$rs2, SPMem:$rs1, 0)>;
}

let Predicates = [HasStdExtCOrZca, IsRV64] in {
def : InstAlias<"c.ld $rd, (${rs1})", (C_LD GPRC:$rd, GPRCMem:$rs1, 0)>;
def : InstAlias<"c.sd $rs2, (${rs1})", (C_SD GPRC:$rs2, GPRCMem:$rs1, 0)>;
def : InstAlias<"c.ldsp $rd, (${rs1})", (C_LDSP GPRNoX0:$rd, SPMem:$rs1, 0)>;
def : InstAlias<"c.sdsp $rs2, (${rs1})", (C_SDSP GPRNoX0:$rs2, SPMem:$rs1, 0)>;
}

let Predicates = [HasStdExtCOrZcfOrZce, HasStdExtF, IsRV32] in {
def : InstAlias<"c.flw $rd, (${rs1})", (C_FLW FPR32C:$rd, GPRCMem:$rs1, 0)>;
def : InstAlias<"c.fsw $rs2, (${rs1})", (C_FSW FPR32C:$rs2, GPRCMem:$rs1, 0)>;
def : InstAlias<"c.flwsp $rd, (${rs1})", (C_FLWSP FPR32:$rd, SPMem:$rs1, 0)>;
def : InstAlias<"c.fswsp $rs2, (${rs1})", (C_FSWSP FPR32:$rs2, SPMem:$rs1, 0)>;
}

let Predicates = [HasStdExtCOrZcd, HasStdExtD] in {
def : InstAlias<"c.fld $rd, (${rs1})", (C_FLD FPR64C:$rd, GPRCMem:$rs1, 0)>;
def : InstAlias<"c.fsd $rs2, (${rs1})", (C_FSD FPR64C:$rs2, GPRCMem:$rs1, 0)>;
def : InstAlias<"c.fldsp $rd, (${rs1})", (C_FLDSP FPR64:$rd, SPMem:$rs1, 0)>;
def : InstAlias<"c.fsdsp $rs2, (${rs1})", (C_FSDSP FPR64:$rs2, SPMem:$rs1, 0)>;
}
} // EmitPriority = 0

//===----------------------------------------------------------------------===//
// .insn directive instructions
//===----------------------------------------------------------------------===//

def AnyRegCOperand : AsmOperandClass {
  let Name = "AnyRegCOperand";
  let RenderMethod = "addRegOperands";
  let PredicateMethod = "isAnyRegC";
}

def AnyRegC : Operand<XLenVT> {
  let OperandType = "OPERAND_REGISTER";
  let ParserMatchClass = AnyRegCOperand;
}

// isCodeGenOnly = 1 to hide them from the tablegened assembly parser.
let isCodeGenOnly = 1, hasSideEffects = 1, mayLoad = 1, mayStore = 1,
    hasNoSchedulingInfo = 1, Predicates = [HasStdExtCOrZca] in {
def InsnCR : DirectiveInsnCR<(outs AnyReg:$rd), (ins uimm2_opcode:$opcode,
                                                     uimm4:$funct4,
                                                     AnyReg:$rs2),
                             "$opcode, $funct4, $rd, $rs2">;
def InsnCI : DirectiveInsnCI<(outs AnyReg:$rd), (ins uimm2_opcode:$opcode,
                                                     uimm3:$funct3,
                                                     simm6:$imm6),
                             "$opcode, $funct3, $rd, $imm6">;
def InsnCIW : DirectiveInsnCIW<(outs AnyRegC:$rd), (ins uimm2_opcode:$opcode,
                                                        uimm3:$funct3,
                                                        uimm8:$imm8),
                               "$opcode, $funct3, $rd, $imm8">;
def InsnCSS : DirectiveInsnCSS<(outs), (ins uimm2_opcode:$opcode,
                                            uimm3:$funct3,
                                            AnyReg:$rs2,
                                            uimm6:$imm6),
                               "$opcode, $funct3, $rs2, $imm6">;
def InsnCL : DirectiveInsnCL<(outs AnyRegC:$rd), (ins uimm2_opcode:$opcode,
                                                      uimm3:$funct3,
                                                      AnyRegC:$rs1,
                                                      uimm5:$imm5),
                             "$opcode, $funct3, $rd, ${imm5}(${rs1})">;
def InsnCS : DirectiveInsnCS<(outs), (ins uimm2_opcode:$opcode,
                                          uimm3:$funct3,
                                          AnyRegC:$rs2,
                                          AnyRegC:$rs1,
                                          uimm5:$imm5),
                             "$opcode, $funct3, $rs2, ${imm5}(${rs1})">;
def InsnCA : DirectiveInsnCA<(outs AnyRegC:$rd), (ins uimm2_opcode:$opcode,
                                                      uimm6:$funct6,
                                                      uimm2:$funct2,
                                                      AnyRegC:$rs2),
                             "$opcode, $funct6, $funct2, $rd, $rs2">;
def InsnCB : DirectiveInsnCB<(outs), (ins uimm2_opcode:$opcode, uimm3:$funct3,
                                          AnyRegC:$rs1,
                                          simm9_lsb0:$imm8),
                             "$opcode, $funct3, $rs1, $imm8">;
def InsnCJ : DirectiveInsnCJ<(outs), (ins uimm2_opcode:$opcode,
                                          uimm3:$funct3,
                                          simm12_lsb0:$imm11),
                             "$opcode, $funct3, $imm11">;
def Insn16 : RVInst16<(outs), (ins uimm16:$value), "", "", [], InstFormatOther> {
  bits<16> value;

  let Inst{15-0} = value;
  let AsmString = ".insn 0x2, $value";
}
}

// Use InstAliases to match these so that we can combine the insn and format
// into a mnemonic to use as the key for the tablegened asm matcher table. The
// parser will take care of creating these fake mnemonics and will only do it
// for known formats.
let EmitPriority = 0, Predicates = [HasStdExtCOrZca] in {
def : InstAlias<".insn_cr $opcode, $funct4, $rd, $rs2",
                (InsnCR AnyReg:$rd, uimm2_opcode:$opcode, uimm4:$funct4,
                        AnyReg:$rs2)>;
def : InstAlias<".insn_ci $opcode, $funct3, $rd, $imm6",
                (InsnCI AnyReg:$rd, uimm2_opcode:$opcode, uimm3:$funct3,
                        simm6:$imm6)>;
def : InstAlias<".insn_ciw $opcode, $funct3, $rd, $imm8",
                (InsnCIW AnyRegC:$rd, uimm2_opcode:$opcode, uimm3:$funct3,
                         uimm8:$imm8)>;
def : InstAlias<".insn_css $opcode, $funct3, $rs2, $imm6",
                (InsnCSS uimm2_opcode:$opcode, uimm3:$funct3, AnyReg:$rs2,
                         uimm6:$imm6)>;
def : InstAlias<".insn_cl $opcode, $funct3, $rd, ${imm5}(${rs1})",
                (InsnCL AnyRegC:$rd, uimm2_opcode:$opcode, uimm3:$funct3,
                        AnyRegC:$rs1, uimm5:$imm5)>;
def : InstAlias<".insn_cl $opcode, $funct3, $rd, (${rs1})",
                (InsnCL AnyRegC:$rd, uimm2_opcode:$opcode, uimm3:$funct3,
                        AnyRegC:$rs1, 0)>;
def : InstAlias<".insn_cs $opcode, $funct3, $rs2, ${imm5}(${rs1})",
                (InsnCS uimm2_opcode:$opcode, uimm3:$funct3, AnyRegC:$rs2,
                        AnyRegC:$rs1, uimm5:$imm5)>;
def : InstAlias<".insn_cs $opcode, $funct3, $rs2, (${rs1})",
                (InsnCS uimm2_opcode:$opcode, uimm3:$funct3, AnyRegC:$rs2,
                        AnyRegC:$rs1, 0)>;
def : InstAlias<".insn_ca $opcode, $funct6, $funct2, $rd, $rs2",
                (InsnCA AnyRegC:$rd, uimm2_opcode:$opcode, uimm6:$funct6,
                        uimm2:$funct2, AnyRegC:$rs2)>;
def : InstAlias<".insn_cb $opcode, $funct3, $rs1, $imm8",
                (InsnCB uimm2_opcode:$opcode, uimm3:$funct3, AnyRegC:$rs1,
                        simm9_lsb0:$imm8)>;
def : InstAlias<".insn_cj $opcode, $funct3, $imm11",
                (InsnCJ uimm2_opcode:$opcode, uimm3:$funct3, simm12_lsb0:$imm11)>;
}

//===----------------------------------------------------------------------===/i
// Compress Instruction tablegen backend.
//===----------------------------------------------------------------------===//

// Patterns are defined in the same order the compressed instructions appear
// under the "RVC Instruction Set Listings" section of the ISA manual.

// Quadrant 0
let Predicates = [HasStdExtCOrZca] in {
def : CompressPat<(ADDI GPRC:$rd, SP:$rs1, uimm10_lsb00nonzero:$imm),
                  (C_ADDI4SPN GPRC:$rd, SP:$rs1, uimm10_lsb00nonzero:$imm)>;
} // Predicates = [HasStdExtCOrZca]

let Predicates = [HasStdExtCOrZcd, HasStdExtD] in {
def : CompressPat<(FLD FPR64C:$rd, GPRCMem:$rs1, uimm8_lsb000:$imm),
                  (C_FLD FPR64C:$rd, GPRCMem:$rs1, uimm8_lsb000:$imm)>;
} // Predicates = [HasStdExtCOrZcd, HasStdExtD]

let Predicates = [HasStdExtCOrZca] in {
def : CompressPat<(LW GPRC:$rd, GPRCMem:$rs1, uimm7_lsb00:$imm),
                  (C_LW GPRC:$rd, GPRCMem:$rs1, uimm7_lsb00:$imm)>;
} // Predicates = [HasStdExtCOrZca]

let Predicates = [HasStdExtCOrZcfOrZce, HasStdExtF, IsRV32] in {
def : CompressPat<(FLW FPR32C:$rd, GPRCMem:$rs1, uimm7_lsb00:$imm),
                  (C_FLW FPR32C:$rd, GPRCMem:$rs1, uimm7_lsb00:$imm)>;
} // Predicates = [HasStdExtC, HasStdExtF, IsRV32]

let Predicates = [HasStdExtCOrZca, IsRV64] in {
def : CompressPat<(LD GPRC:$rd, GPRCMem:$rs1, uimm8_lsb000:$imm),
                  (C_LD GPRC:$rd, GPRCMem:$rs1, uimm8_lsb000:$imm)>;
} // Predicates = [HasStdExtCOrZca, IsRV64]

let Predicates = [HasStdExtCOrZcd, HasStdExtD] in {
def : CompressPat<(FSD FPR64C:$rs2, GPRCMem:$rs1, uimm8_lsb000:$imm),
                  (C_FSD FPR64C:$rs2, GPRCMem:$rs1, uimm8_lsb000:$imm)>;
} // Predicates = [HasStdExtCOrZcd, HasStdExtD]

let Predicates = [HasStdExtCOrZca] in {
def : CompressPat<(SW GPRC:$rs2, GPRCMem:$rs1, uimm7_lsb00:$imm),
                  (C_SW GPRC:$rs2, GPRCMem:$rs1, uimm7_lsb00:$imm)>;
} // Predicates = [HasStdExtCOrZca]

let Predicates = [HasStdExtCOrZcfOrZce, HasStdExtF, IsRV32] in {
def : CompressPat<(FSW FPR32C:$rs2, GPRCMem:$rs1, uimm7_lsb00:$imm),
                  (C_FSW FPR32C:$rs2, GPRCMem:$rs1, uimm7_lsb00:$imm)>;
} // Predicates = [HasStdExtC, HasStdExtF, IsRV32]

let Predicates = [HasStdExtCOrZca, IsRV64] in {
def : CompressPat<(SD GPRC:$rs2, GPRCMem:$rs1, uimm8_lsb000:$imm),
                  (C_SD GPRC:$rs2, GPRCMem:$rs1, uimm8_lsb000:$imm)>;
} // Predicates = [HasStdExtCOrZca, IsRV64]

// Quadrant 1
let Predicates = [HasStdExtCOrZca] in {
def : CompressPat<(ADDI X0, X0, 0), (C_NOP)>;
def : CompressPat<(ADDI GPRNoX0:$rs1, GPRNoX0:$rs1, simm6nonzero:$imm),
                  (C_ADDI GPRNoX0:$rs1, simm6nonzero:$imm)>;
} // Predicates = [HasStdExtCOrZca]

let Predicates = [HasStdExtCOrZca, IsRV32] in {
def : CompressPat<(JAL X1, simm12_lsb0:$offset),
                  (C_JAL simm12_lsb0:$offset)>;
} // Predicates = [HasStdExtCOrZca, IsRV32]

let Predicates = [HasStdExtCOrZca, IsRV64] in {
def : CompressPat<(ADDIW GPRNoX0:$rs1, GPRNoX0:$rs1, simm6:$imm),
                  (C_ADDIW GPRNoX0:$rs1, simm6:$imm)>;
} // Predicates = [HasStdExtCOrZca, IsRV64]

let Predicates = [HasStdExtCOrZca] in {
def : CompressPat<(ADDI GPRNoX0:$rd, X0, simm6:$imm),
                  (C_LI GPRNoX0:$rd, simm6:$imm)>;
def : CompressPat<(ADDI X2, X2, simm10_lsb0000nonzero:$imm),
                  (C_ADDI16SP X2, simm10_lsb0000nonzero:$imm)>;
def : CompressPat<(LUI GPRNoX0X2:$rd, c_lui_imm:$imm),
                  (C_LUI GPRNoX0X2:$rd, c_lui_imm:$imm)>;
def : CompressPat<(SRLI GPRC:$rs1, GPRC:$rs1, uimmlog2xlennonzero:$imm),
                  (C_SRLI GPRC:$rs1, uimmlog2xlennonzero:$imm)>;
def : CompressPat<(SRAI GPRC:$rs1, GPRC:$rs1, uimmlog2xlennonzero:$imm),
                  (C_SRAI GPRC:$rs1, uimmlog2xlennonzero:$imm)>;
def : CompressPat<(ANDI GPRC:$rs1, GPRC:$rs1, simm6:$imm),
                  (C_ANDI GPRC:$rs1, simm6:$imm)>;
def : CompressPat<(SUB GPRC:$rs1, GPRC:$rs1, GPRC:$rs2),
                  (C_SUB GPRC:$rs1, GPRC:$rs2)>;
def : CompressPat<(XOR GPRC:$rs1, GPRC:$rs1, GPRC:$rs2),
                  (C_XOR GPRC:$rs1, GPRC:$rs2)>;
let isCompressOnly = true in
def : CompressPat<(XOR GPRC:$rs1, GPRC:$rs2, GPRC:$rs1),
                  (C_XOR GPRC:$rs1, GPRC:$rs2)>;
def : CompressPat<(OR GPRC:$rs1, GPRC:$rs1, GPRC:$rs2),
                  (C_OR GPRC:$rs1, GPRC:$rs2)>;
let isCompressOnly = true in
def : CompressPat<(OR GPRC:$rs1, GPRC:$rs2, GPRC:$rs1),
                  (C_OR GPRC:$rs1, GPRC:$rs2)>;
def : CompressPat<(AND GPRC:$rs1, GPRC:$rs1, GPRC:$rs2),
                  (C_AND GPRC:$rs1, GPRC:$rs2)>;
let isCompressOnly = true in
def : CompressPat<(AND GPRC:$rs1, GPRC:$rs2, GPRC:$rs1),
                  (C_AND GPRC:$rs1, GPRC:$rs2)>;
} // Predicates = [HasStdExtCOrZca]

let Predicates = [HasStdExtCOrZca, IsRV64] in {
let isCompressOnly = true in
def : CompressPat<(ADDIW GPRNoX0:$rd, X0, simm6:$imm),
                  (C_LI GPRNoX0:$rd, simm6:$imm)>;
def : CompressPat<(SUBW GPRC:$rs1, GPRC:$rs1, GPRC:$rs2),
                  (C_SUBW GPRC:$rs1, GPRC:$rs2)>;
def : CompressPat<(ADDW GPRC:$rs1, GPRC:$rs1, GPRC:$rs2),
                   (C_ADDW GPRC:$rs1, GPRC:$rs2)>;
let isCompressOnly = true in
def : CompressPat<(ADDW GPRC:$rs1, GPRC:$rs2, GPRC:$rs1),
                   (C_ADDW GPRC:$rs1, GPRC:$rs2)>;
} // Predicates = [HasStdExtCOrZca, IsRV64]

let Predicates = [HasStdExtCOrZca] in {
def : CompressPat<(JAL X0, simm12_lsb0:$offset),
                  (C_J simm12_lsb0:$offset)>;
def : CompressPat<(BEQ GPRC:$rs1, X0, simm9_lsb0:$imm),
                  (C_BEQZ GPRC:$rs1, simm9_lsb0:$imm)>;
let isCompressOnly = true in
def : CompressPat<(BEQ X0, GPRC:$rs1, simm9_lsb0:$imm),
                  (C_BEQZ GPRC:$rs1, simm9_lsb0:$imm)>;
def : CompressPat<(BNE GPRC:$rs1, X0, simm9_lsb0:$imm),
                  (C_BNEZ GPRC:$rs1, simm9_lsb0:$imm)>;
let isCompressOnly = true in
def : CompressPat<(BNE X0, GPRC:$rs1, simm9_lsb0:$imm),
                  (C_BNEZ GPRC:$rs1, simm9_lsb0:$imm)>;
} // Predicates = [HasStdExtCOrZca]

// Quadrant 2
let Predicates = [HasStdExtCOrZca] in {
def : CompressPat<(SLLI GPRNoX0:$rs1, GPRNoX0:$rs1, uimmlog2xlennonzero:$imm),
                  (C_SLLI GPRNoX0:$rs1, uimmlog2xlennonzero:$imm)>;
} // Predicates = [HasStdExtCOrZca]

let Predicates = [HasStdExtCOrZcd, HasStdExtD] in {
def : CompressPat<(FLD FPR64:$rd, SPMem:$rs1, uimm9_lsb000:$imm),
                  (C_FLDSP FPR64:$rd, SPMem:$rs1, uimm9_lsb000:$imm)>;
} // Predicates = [HasStdExtCOrZcd, HasStdExtD]

let Predicates = [HasStdExtCOrZca] in {
def : CompressPat<(LW GPRNoX0:$rd, SPMem:$rs1,  uimm8_lsb00:$imm),
                  (C_LWSP GPRNoX0:$rd, SPMem:$rs1, uimm8_lsb00:$imm)>;
} // Predicates = [HasStdExtCOrZca]

let Predicates = [HasStdExtCOrZcfOrZce, HasStdExtF, IsRV32] in {
def : CompressPat<(FLW FPR32:$rd, SPMem:$rs1, uimm8_lsb00:$imm),
                  (C_FLWSP FPR32:$rd, SPMem:$rs1, uimm8_lsb00:$imm)>;
} // Predicates = [HasStdExtC, HasStdExtF, IsRV32]

let Predicates = [HasStdExtCOrZca, IsRV64] in {
def : CompressPat<(LD GPRNoX0:$rd, SPMem:$rs1, uimm9_lsb000:$imm),
                  (C_LDSP GPRNoX0:$rd, SPMem:$rs1, uimm9_lsb000:$imm)>;
} // Predicates = [HasStdExtCOrZca, IsRV64]

let Predicates = [HasStdExtCOrZca] in {
def : CompressPat<(JALR X0, GPRNoX0:$rs1, 0),
                  (C_JR GPRNoX0:$rs1)>;
let isCompressOnly = true in {
def : CompressPat<(ADD GPRNoX0:$rs1, X0, GPRNoX0:$rs2),
                  (C_MV GPRNoX0:$rs1, GPRNoX0:$rs2)>;
def : CompressPat<(ADD GPRNoX0:$rs1, GPRNoX0:$rs2, X0),
                  (C_MV GPRNoX0:$rs1, GPRNoX0:$rs2)>;
}
def : CompressPat<(ADDI GPRNoX0:$rs1, GPRNoX0:$rs2, 0),
                  (C_MV GPRNoX0:$rs1, GPRNoX0:$rs2)>;
def : CompressPat<(EBREAK), (C_EBREAK)>;
def : CompressPat<(UNIMP), (C_UNIMP)>;
def : CompressPat<(JALR X1, GPRNoX0:$rs1, 0),
                  (C_JALR GPRNoX0:$rs1)>;
def : CompressPat<(ADD GPRNoX0:$rs1, GPRNoX0:$rs1, GPRNoX0:$rs2),
                  (C_ADD GPRNoX0:$rs1, GPRNoX0:$rs2)>;
let isCompressOnly = true in
def : CompressPat<(ADD GPRNoX0:$rs1, GPRNoX0:$rs2, GPRNoX0:$rs1),
                  (C_ADD GPRNoX0:$rs1, GPRNoX0:$rs2)>;
} // Predicates = [HasStdExtCOrZca]

let Predicates = [HasStdExtCOrZcd, HasStdExtD] in {
def : CompressPat<(FSD FPR64:$rs2, SPMem:$rs1, uimm9_lsb000:$imm),
                  (C_FSDSP FPR64:$rs2, SPMem:$rs1, uimm9_lsb000:$imm)>;
} // Predicates = [HasStdExtCOrZcd, HasStdExtD]

let Predicates = [HasStdExtCOrZca] in {
def : CompressPat<(SW GPR:$rs2, SPMem:$rs1, uimm8_lsb00:$imm),
                  (C_SWSP GPR:$rs2, SPMem:$rs1, uimm8_lsb00:$imm)>;
} // Predicates = [HasStdExtCOrZca]

let Predicates = [HasStdExtCOrZcfOrZce, HasStdExtF, IsRV32] in {
def : CompressPat<(FSW FPR32:$rs2, SPMem:$rs1, uimm8_lsb00:$imm),
                  (C_FSWSP FPR32:$rs2, SPMem:$rs1, uimm8_lsb00:$imm)>;
} // Predicates = [HasStdExtC, HasStdExtF, IsRV32]

let Predicates = [HasStdExtCOrZca, IsRV64] in {
def : CompressPat<(SD GPR:$rs2, SPMem:$rs1, uimm9_lsb000:$imm),
                  (C_SDSP GPR:$rs2, SPMem:$rs1, uimm9_lsb000:$imm)>;
} // Predicates = [HasStdExtCOrZca, IsRV64]
