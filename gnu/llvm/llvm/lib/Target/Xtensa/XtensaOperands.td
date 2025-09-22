//===- XtensaOperands.td - Xtensa instruction operands -------*- tblgen-*--===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Immediate operands with a shared generic render method.
class ImmAsmOperand<string name> : AsmOperandClass {
  let Name = name;
  let RenderMethod = "addImmOperands";
  let DiagnosticType = !strconcat("Invalid", name);
}

class Immediate<ValueType vt, code pred, string asmop>
  : Operand<vt>, ImmLeaf<vt, pred> {
  let PrintMethod = "print"#asmop;
  let ParserMatchClass = !cast<AsmOperandClass>(asmop);
}

// imm8 predicate - Immediate in the range [-128,127]
def Imm8_AsmOperand : ImmAsmOperand<"Imm8">;
def imm8 : Immediate<i32, [{ return Imm >= -128 && Imm <= 127; }], "Imm8_AsmOperand"> {
  let EncoderMethod = "getImm8OpValue";
  let DecoderMethod = "decodeImm8Operand";
}

// imm8_sh8 predicate - Immediate in the range [-32768,32512] with (bits[7-0] == 0)
// imm8 value left shifted by 8 bits
def Imm8_sh8_AsmOperand : ImmAsmOperand<"Imm8_sh8">;
def imm8_sh8 : Immediate<i32, [{ return Imm >= -32768 && Imm <= 32512 && ((Imm & 0xFF) == 0); }],
                        "Imm8_sh8_AsmOperand"> {
  let EncoderMethod = "getImm8_sh8OpValue";
  let DecoderMethod = "decodeImm8_sh8Operand";
}

// imm12 predicate - Immediate in the range [-2048,2047]
def Imm12_AsmOperand : ImmAsmOperand<"Imm12">;
def imm12 : Immediate<i32, [{ return Imm >= -2048 && Imm <= 2047; }], "Imm12_AsmOperand"> {
  let EncoderMethod = "getImm12OpValue";
  let DecoderMethod = "decodeImm12Operand";
}

// imm12m predicate - Immediate for MOV operation
def Imm12m_AsmOperand : ImmAsmOperand<"Imm12m">;
def imm12m : Immediate<i32, [{ return Imm >= -2048 && Imm <= 2047; }], "Imm12m_AsmOperand"> {
  let EncoderMethod = "getImm12OpValue";
  let DecoderMethod = "decodeImm12Operand";
}

// uimm4 predicate - Immediate in the range [0,15]
def Uimm4_AsmOperand : ImmAsmOperand<"Uimm4">;
def uimm4 : Immediate<i32, [{ return Imm >= 0 && Imm <= 15; }], "Uimm4_AsmOperand"> {
  let EncoderMethod = "getUimm4OpValue";
  let DecoderMethod = "decodeUimm4Operand";
}

// uimm5 predicate - Immediate in the range [0,31]
def Uimm5_AsmOperand : ImmAsmOperand<"Uimm5">;
def uimm5 : Immediate<i32, [{ return Imm >= 0 && Imm <= 31; }], "Uimm5_AsmOperand"> {
  let EncoderMethod = "getUimm5OpValue";
  let DecoderMethod = "decodeUimm5Operand";
}

// imm1_16 predicate - Immediate in the range [1,16]
def Imm1_16_AsmOperand : ImmAsmOperand<"Imm1_16">;
def imm1_16 : Immediate<i32, [{ return Imm >= 1 && Imm <= 16; }], "Imm1_16_AsmOperand"> {
  let EncoderMethod = "getImm1_16OpValue";
  let DecoderMethod = "decodeImm1_16Operand";
}

// shimm1_31 predicate - Immediate in the range [1,31]
def Shimm1_31_AsmOperand : ImmAsmOperand<"Shimm1_31">;
def shimm1_31 : Immediate<i32, [{ return Imm >= 1 && Imm <= 31; }], "Shimm1_31_AsmOperand"> {
  let EncoderMethod = "getShimm1_31OpValue";
  let DecoderMethod = "decodeShimm1_31Operand";
}

// Memory offset 0..255 for 8-bit memory accesses
def Offset8m8_AsmOperand : ImmAsmOperand<"Offset8m8">;
def offset8m8 : Immediate<i32,
    [{ return Imm >= 0 && Imm <= 255; }],
    "Offset8m8_AsmOperand">;

// Memory offset 0..510 for 16-bit memory accesses
def Offset8m16_AsmOperand : ImmAsmOperand<"Offset8m16">;
def offset8m16 : Immediate<i32,
    [{ return Imm >= 0 && Imm <= 510 && (Imm & 0x1 == 0); }],
    "Offset8m16_AsmOperand">;

// Memory offset 0..1020 for 32-bit memory accesses
def Offset8m32_AsmOperand : ImmAsmOperand<"Offset8m32">;
def offset8m32 : Immediate<i32,
    [{ return Imm >= 0 && Imm <= 1020 && (Imm & 0x3 == 0); }],
    "Offset8m32_AsmOperand">;

// Memory offset 0..60 for 32-bit memory accesses
def Offset4m32_AsmOperand : ImmAsmOperand<"Offset4m32">;
def offset4m32 : Immediate<i32,
    [{ return Imm >= 0 && Imm <= 60 && (Imm & 0x3 == 0); }],
    "Offset4m32_AsmOperand">;

// b4const predicate - Branch Immediate 4-bit signed operand
def B4const_AsmOperand: ImmAsmOperand<"B4const">;
def b4const: Immediate<i32,
  [{ switch (Imm) {
        case -1: case 1: case 2: case 3:  case 4:
        case 5:  case 6: case 7: case 8: case 10: case 12:
        case 16: case 32: case 64: case 128: case 256: return 1;
        default: return 0;
     }
  }],
  "B4const_AsmOperand"> {
  let EncoderMethod = "getB4constOpValue";
  let DecoderMethod = "decodeB4constOperand";
}

// b4constu predicate - Branch Immediate 4-bit unsigned operand
def B4constu_AsmOperand: ImmAsmOperand<"B4constu">;
def b4constu: Immediate<i32,
  [{ switch (Imm) {
        case 32768: case 65536: case 2: case 3:  case 4:
        case 5:  case 6: case 7: case 8: case 10: case 12:
        case 16: case 32: case 64: case 128: case 256: return 1;
        default: return 0;
     }
  }],
  "B4constu_AsmOperand"> {
  let EncoderMethod = "getB4constuOpValue";
  let DecoderMethod = "decodeB4constuOperand";
}
//===----------------------------------------------------------------------===//
// Memory address operands
//===----------------------------------------------------------------------===//

class mem<Operand offset> : Operand<i32> {
  let MIOperandInfo = (ops AR, offset);
  let EncoderMethod = "getMemRegEncoding";
  let OperandType = "OPERAND_MEMORY";
  let PrintMethod = "printMemOperand";
}

def mem8   : mem<offset8m8> {
  let DecoderMethod = "decodeMem8Operand";
}

def mem16  : mem<offset8m16> {
  let DecoderMethod = "decodeMem16Operand";
}

def mem32  : mem<offset8m32> {
  let DecoderMethod = "decodeMem32Operand";
}

def mem32n : mem<offset4m32> {
  let DecoderMethod = "decodeMem32nOperand";
}

//Add patterns for future use in stack addressing mode
def addr_ish1 : ComplexPattern<iPTR, 2, "selectMemRegAddrISH1", [frameindex]>;
def addr_ish2 : ComplexPattern<iPTR, 2, "selectMemRegAddrISH2", [frameindex]>;
def addr_ish4 : ComplexPattern<iPTR, 2, "selectMemRegAddrISH4", [frameindex]>;

//===----------------------------------------------------------------------===//
// Symbolic address operands
//===----------------------------------------------------------------------===//
def XtensaPCRelTargetAsmOperand : AsmOperandClass {
  let Name = "PCRelTarget";
  let ParserMethod = "parsePCRelTarget";
  let PredicateMethod = "isImm";
  let RenderMethod = "addImmOperands";
}

def  pcrel32call : Operand<iPTR> {
  let PrintMethod = "printCallOperand";
  let EncoderMethod = "getCallEncoding";
  let DecoderMethod = "decodeCallOperand";
  let ParserMatchClass = XtensaPCRelTargetAsmOperand;
}

def brtarget : Operand<OtherVT> {
  let PrintMethod = "printBranchTarget";
  let EncoderMethod = "getBranchTargetEncoding";
  let DecoderMethod = "decodeBranchOperand";
  let ParserMatchClass = XtensaPCRelTargetAsmOperand;
}

def jumptarget : Operand<OtherVT> {
  let PrintMethod = "printJumpTarget";
  let EncoderMethod = "getJumpTargetEncoding";
  let DecoderMethod = "decodeJumpOperand";
  let ParserMatchClass = XtensaPCRelTargetAsmOperand;
}

def L32Rtarget : Operand<i32> {
  let PrintMethod = "printL32RTarget";
  let EncoderMethod = "getL32RTargetEncoding";
  let DecoderMethod = "decodeL32ROperand";
  let ParserMatchClass = XtensaPCRelTargetAsmOperand;
}
