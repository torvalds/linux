//===-- M68kInstrInfo.td - Main M68k Instruction Definition -*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes the M68k instruction set, defining the instructions
/// and properties of the instructions which are needed for code generation,
/// machine code emission, and analysis.
///
//===----------------------------------------------------------------------===//

include "M68kInstrFormats.td"

//===----------------------------------------------------------------------===//
// Profiles
//===----------------------------------------------------------------------===//

def MxSDT_CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def MxSDT_CallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;

def MxSDT_Call    : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;

def MxSDT_Ret     : SDTypeProfile<0, -1, [
  /* ADJ */ SDTCisVT<0, i32>
]>;

def MxSDT_TCRet   : SDTypeProfile<0, 2, [SDTCisPtrTy<0>, SDTCisVT<1, i32>]>;

def MxSDT_Wrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;

def MxSDT_UnArithCCROut : SDTypeProfile<2, 1, [
  /* RES */ SDTCisInt<0>,
  /* CCR */ SDTCisVT<1, i8>,
  /* OPD */ SDTCisSameAs<0, 2>
]>;

// RES, CCR <- op LHS, RHS
def MxSDT_BiArithCCROut : SDTypeProfile<2, 2, [
  /* RES */ SDTCisInt<0>,
  /* CCR */ SDTCisVT<1, i8>,
  /* LHS */ SDTCisSameAs<0, 2>,
  /* RHS */ SDTCisSameAs<0, 3>
]>;

// RES, CCR <- op LHS, RHS, CCR
def MxSDT_BiArithCCRInOut : SDTypeProfile<2, 3, [
  /* RES 1 */ SDTCisInt<0>,
  /*   CCR */ SDTCisVT<1, i8>,
  /*   LHS */ SDTCisSameAs<0, 2>,
  /*   RHS */ SDTCisSameAs<0, 3>,
  /*   CCR */ SDTCisSameAs<1, 4>
]>;

def MxSDT_CmpTest : SDTypeProfile<1, 2, [
   /* CCR */ SDTCisVT<0, i8>,
   /* Ops */ SDTCisSameAs<1, 2>
]>;

def MxSDT_Cmov : SDTypeProfile<1, 4, [
  /*  ARG */ SDTCisSameAs<0, 1>,
  /*  ARG */ SDTCisSameAs<1, 2>,
  /* Cond */ SDTCisVT<3, i8>,
  /*  CCR */ SDTCisVT<4, i8>
]>;

def MxSDT_BrCond : SDTypeProfile<0, 3, [
  /* Dest */ SDTCisVT<0, OtherVT>,
  /* Cond */ SDTCisVT<1, i8>,
  /*  CCR */ SDTCisVT<2, i8>
]>;

def MxSDT_SetCC : SDTypeProfile<1, 2, [
  /* BOOL */ SDTCisVT<0, i8>,
  /* Cond */ SDTCisVT<1, i8>,
  /*  CCR */ SDTCisVT<2, i8>
]>;

def MxSDT_SetCC_C : SDTypeProfile<1, 2, [
  /* BOOL */ SDTCisInt<0>,
  /* Cond */ SDTCisVT<1, i8>,
  /*  CCR */ SDTCisVT<2, i8>
]>;


def MxSDT_SEG_ALLOCA : SDTypeProfile<1, 1,[
  /*  MEM */ SDTCisVT<0, iPTR>,
  /* SIZE */ SDTCisVT<1, iPTR>
]>;


//===----------------------------------------------------------------------===//
// Nodes
//===----------------------------------------------------------------------===//

def MxCallSeqStart : SDNode<"ISD::CALLSEQ_START", MxSDT_CallSeqStart,
                            [SDNPHasChain, SDNPOutGlue]>;

def MxCallSeqEnd   : SDNode<"ISD::CALLSEQ_END", MxSDT_CallSeqEnd,
                            [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def MxCall         : SDNode<"M68kISD::CALL", MxSDT_Call,
                            [SDNPHasChain, SDNPOutGlue,
                             SDNPOptInGlue, SDNPVariadic]>;

def MxRet   : SDNode<"M68kISD::RET", MxSDT_Ret,
                     [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def MxTCRet : SDNode<"M68kISD::TC_RETURN", MxSDT_TCRet,
                     [SDNPHasChain,  SDNPOptInGlue, SDNPVariadic]>;

def MxWrapper   : SDNode<"M68kISD::Wrapper",   MxSDT_Wrapper>;
def MxWrapperPC : SDNode<"M68kISD::WrapperPC", MxSDT_Wrapper>;

def MxAdd  : SDNode<"M68kISD::ADD",  MxSDT_BiArithCCROut, [SDNPCommutative]>;
def MxSub  : SDNode<"M68kISD::SUB",  MxSDT_BiArithCCROut>;
def MxOr   : SDNode<"M68kISD::OR",   MxSDT_BiArithCCROut, [SDNPCommutative]>;
def MxXor  : SDNode<"M68kISD::XOR",  MxSDT_BiArithCCROut, [SDNPCommutative]>;
def MxAnd  : SDNode<"M68kISD::AND",  MxSDT_BiArithCCROut, [SDNPCommutative]>;

def MxAddX : SDNode<"M68kISD::ADDX", MxSDT_BiArithCCRInOut>;
def MxSubX : SDNode<"M68kISD::SUBX", MxSDT_BiArithCCRInOut>;

def MxSMul : SDNode<"M68kISD::SMUL", MxSDT_BiArithCCROut, [SDNPCommutative]>;
def MxUMul : SDNode<"M68kISD::UMUL", MxSDT_BiArithCCROut, [SDNPCommutative]>;

def MxCmp     : SDNode<"M68kISD::CMP", MxSDT_CmpTest>;
def MxBtst    : SDNode<"M68kISD::BTST", MxSDT_CmpTest>;

def MxCmov    : SDNode<"M68kISD::CMOV",        MxSDT_Cmov>;
def MxBrCond  : SDNode<"M68kISD::BRCOND",      MxSDT_BrCond, [SDNPHasChain]>;
def MxSetCC   : SDNode<"M68kISD::SETCC",       MxSDT_SetCC>;
def MxSetCC_C : SDNode<"M68kISD::SETCC_CARRY", MxSDT_SetCC_C>;


def MxSegAlloca : SDNode<"M68kISD::SEG_ALLOCA", MxSDT_SEG_ALLOCA,
                         [SDNPHasChain]>;


//===----------------------------------------------------------------------===//
// Operands
//===----------------------------------------------------------------------===//

/// Size is the size of the data, either bits of a register or number of bits
/// addressed in memory. Size id is a letter that identifies size.
class MxSize<int num, string id, string full> {
  int Num = num;
  string Id = id;
  string Full = full;
}

def MxSize8  : MxSize<8,  "b", "byte">;
def MxSize16 : MxSize<16, "w", "word">;
def MxSize32 : MxSize<32, "l", "long">;
def MxSizeF32 : MxSize<32, "s", "f32">;
def MxSizeF64 : MxSize<64, "d", "f64">;
def MxSizeF80 : MxSize<80, "x", "f80">;

class MxOpClass<string name,
                list<AsmOperandClass> superClasses = []> : AsmOperandClass {
  let Name = name;
  let ParserMethod = "parseMemOp";
  let SuperClasses = superClasses;
}

def MxRegClass : MxOpClass<"Reg">;
// Splitting asm register class to avoid ambiguous on operands'
// MatchClassKind. For instance, without this separation,
// both ADD32dd and ADD32dr has {MCK_RegClass, MCK_RegClass} for
// their operands, which makes AsmParser unable to pick the correct
// one in a deterministic way.
let RenderMethod = "addRegOperands", SuperClasses = [MxRegClass]in {
  def MxARegClass : MxOpClass<"AReg">;
  def MxDRegClass : MxOpClass<"DReg">;

  def MxFPDRegClass : MxOpClass<"FPDReg">;
  def MxFPCRegClass : MxOpClass<"FPCReg">;
}

class MxOperand<ValueType vt, MxSize size, string letter, RegisterClass rc, dag pat = (null_frag)> {
  ValueType VT = vt;
  string Letter = letter;
  MxSize Size = size;
  RegisterClass RC = rc;
  dag Pat = pat;
}

class MxRegOp<ValueType vt,
              RegisterClass rc,
              MxSize size,
              string letter,
              string pm = "printOperand">
    : RegisterOperand<rc, pm>,
      MxOperand<vt, size, letter, rc> {
  let ParserMatchClass = MxRegClass;
}

// REGISTER DIRECT. The operand is in the data register specified by
// the effective address register field.
foreach size = [16, 32] in {
  def MxXRD # size : MxRegOp<!cast<ValueType>("i"#size), !cast<MxRegClass>("XR"#size),
                             !cast<MxSize>("MxSize"#size), "r">;
  def MxXRD # size # _TC : MxRegOp<!cast<ValueType>("i"#size), !cast<MxRegClass>("XR"#size#"_TC"),
                                   !cast<MxSize>("MxSize"#size), "r">;
} // foreach size

// DATA REGISTER DIRECT. The operand is in the data register specified by
// the effective address register field.
let ParserMatchClass = MxDRegClass in {
foreach size = [8, 16, 32] in {
  def MxDRD # size : MxRegOp<!cast<ValueType>("i"#size), !cast<MxRegClass>("DR"#size),
                             !cast<MxSize>("MxSize"#size), "d">;
  if !gt(size, 8) then
  def MxDRD # size # _TC : MxRegOp<!cast<ValueType>("i"#size), !cast<MxRegClass>("DR"#size#"_TC"),
                                   !cast<MxSize>("MxSize"#size), "d">;
} // foreach size
} // let ParserMatchClass

// ADDRESS REGISTER DIRECT. The operand is in the address register specified by
// the effective address register field.
let ParserMatchClass = MxARegClass in {
foreach size = [16, 32] in {
  def MxARD # size : MxRegOp<!cast<ValueType>("i"#size), !cast<MxRegClass>("AR"#size),
                             !cast<MxSize>("MxSize"#size), "a">;
  def MxARD # size # _TC : MxRegOp<!cast<ValueType>("i"#size), !cast<MxRegClass>("AR"#size#"_TC"),
                                   !cast<MxSize>("MxSize"#size), "a">;
} // foreach size
} // let ParserMatchClass

// FLOATING POINT DATA REGISTER.
let ParserMatchClass = MxFPDRegClass in {
  def MxFPR32 : MxRegOp<f32, FPDR32, MxSizeF32, "fp">;
  def MxFPR64 : MxRegOp<f64, FPDR64, MxSizeF64, "fp">;
  def MxFPR80 : MxRegOp<f80, FPDR80, MxSizeF80, "fp">;
}

// FLOATING POINT SYSTEM CONTROL REGISTER
let ParserMatchClass = MxFPCRegClass in {
  def MxFPCSR : MxRegOp<i32, FPCSC, MxSize32, "fpcs">;
  def MxFPIR  : MxRegOp<i32, FPIC,  MxSize32, "fpi">;
}

class MxMemOp<dag ops, MxSize size, string letter,
              string printMethod = "printOperand",
              AsmOperandClass parserMatchClass = ImmAsmOperand>
    : Operand<iPTR>, MxOperand<iPTR, size, letter, ?> {
  let PrintMethod = printMethod;
  let MIOperandInfo = ops;
  let ParserMatchClass = parserMatchClass;
  let OperandType = "OPERAND_MEMORY";
}

// ADDRESS REGISTER INDIRECT. The address of the operand is in the address
// register specified by the register field. The reference is classified as
// a data reference with the exception of the jump and jump-to-subroutine
// instructions.
def MxARI         : MxOpClass<"ARI">;
foreach size = ["8", "16", "32"] in {
  defvar ResSize = !cast<MxSize>("MxSize"#size);
  def MxARI # size       : MxMemOp<(ops AR32),    ResSize, "j", "printARI"#size#"Mem", MxARI>;
  def MxARI # size # _TC : MxMemOp<(ops AR32_TC), ResSize, "j", "printARI"#size#"Mem", MxARI>;
} // foreach size

// ADDRESS REGISTER INDIRECT WITH POSTINCREMENT. The address of the operand is
// in the address register specified by the register field. After the operand
// address is used, it is incremented by one, two, or four depending upon whether
// the size of the operand is byte, word, or long word. If the address register
// is the stack pointer and the operand size is byte, the address is incremented
// by two rather than one to keep the stack pointer on a word boundary.
// The reference is classified as a data reference.
def MxARIPI       : MxOpClass<"ARIPI">;
foreach size = ["8", "16", "32"] in {
  defvar ResSize = !cast<MxSize>("MxSize"#size);
  def MxARIPI # size       : MxMemOp<(ops AR32),    ResSize, "o", "printARIPI"#size#"Mem", MxARIPI>;
  def MxARIPI # size # _TC : MxMemOp<(ops AR32_TC), ResSize, "o", "printARIPI"#size#"Mem", MxARIPI>;
} // foreach size

// ADDRESS REGISTER INDIRECT WITH PREDECREMENT. The address of the operand is in
// the address register specified by the register field. Before the operand
// address is used, it is decremented by one, two, or four depending upon whether
// the operand size is byte, word, or long word. If the address register is
// the stack pointer and the operand size is byte, the address is decremented by
// two rather than one to keep the stack pointer on a word boundary.
// The reference is classified as a data reference.
def MxARIPD       : MxOpClass<"ARIPD">;
foreach size = ["8", "16", "32"] in {
  defvar ResSize = !cast<MxSize>("MxSize"#size);
  def MxARIPD # size       : MxMemOp<(ops AR32),    ResSize, "e", "printARIPD"#size#"Mem", MxARIPD>;
  def MxARIPD # size # _TC : MxMemOp<(ops AR32_TC), ResSize, "e", "printARIPD"#size#"Mem", MxARIPD>;
} // foreach size

// ADDRESS REGISTER INDIRECT WITH DISPLACEMENT. This addressing mode requires one
// word of extension. The address of the operand is the sum of the address in
// the address register and the sign-extended 16-bit displacement integer in the
// extension word. The reference is classified as a data reference with the
// exception of the jump and jump-to-subroutine instructions.
def MxARID        : MxOpClass<"ARID">;
foreach size = ["8", "16", "32"] in {
  defvar ResSize = !cast<MxSize>("MxSize"#size);
  def MxARID # size       : MxMemOp<(ops i16imm:$disp, AR32:$reg),    ResSize, "p", "printARID"#size#"Mem", MxARID>;
  def MxARID # size # _TC : MxMemOp<(ops i16imm:$disp, AR32_TC:$reg), ResSize, "p", "printARID"#size#"Mem", MxARID>;
} // foreach size

// ADDRESS REGISTER INDIRECT WITH INDEX. This addressing mode requires one word
// of extension. The address of the operand is the sum of the address in the
// address register, the signextended displacement integer in the low order eight
// bits of the extension word, and the contents of the index register.
// The reference is classified as a data reference with the exception of the
// jump and jump-to-subroutine instructions
def MxARII       : MxOpClass<"ARII">;
foreach size = ["8", "16", "32"] in {
  defvar ResSize = !cast<MxSize>("MxSize"#size);
  def MxARII # size       : MxMemOp<(ops i8imm:$disp, AR32:$reg,    XR32:$index), ResSize, "f", "printARII"#size#"Mem", MxARII>;
  def MxARII # size # _TC : MxMemOp<(ops i8imm:$disp, AR32_TC:$reg, XR32:$index), ResSize, "f", "printARII"#size#"Mem", MxARII>;
} // foreach size

// ABSOLUTE SHORT ADDRESS. This addressing mode requires one word of extension.
// The address of the operand is the extension word. The 16-bit address is sign
// extended before it is used.  The reference is classified as a data reference
// with the exception of the jump and jump-tosubroutine instructions.
def MxAddr     : MxOpClass<"Addr">;
let RenderMethod = "addAddrOperands" in {
  // This hierarchy ensures Addr8 will always be parsed
  // before other larger-width variants.
  def MxAddr32   : MxOpClass<"Addr32", [MxAddr]>;
  def MxAddr16   : MxOpClass<"Addr16", [MxAddr32]>;
  def MxAddr8    : MxOpClass<"Addr8",  [MxAddr16]>;
}

def MxAS8      : MxMemOp<(ops OtherVT), MxSize8,  "B", "printAS8Mem",  MxAddr8>;
def MxAS16     : MxMemOp<(ops OtherVT), MxSize16, "B", "printAS16Mem", MxAddr16>;
def MxAS32     : MxMemOp<(ops OtherVT), MxSize32, "B", "printAS32Mem", MxAddr32>;

// ABSOLUTE LONG ADDRESS. This addressing mode requires two words of extension.
// The address of the operand is developed by the concatenation of the extension
// words. The high order part of the address is the first extension word; the low
// order part of the address is the second extension word. The reference is
// classified as a data reference with the exception of the jump and jump
// to-subroutine instructions.
def MxAL8      : MxMemOp<(ops OtherVT), MxSize8,  "b", "printAL8Mem",  MxAddr8>;
def MxAL16     : MxMemOp<(ops OtherVT), MxSize16, "b", "printAL16Mem", MxAddr16>;
def MxAL32     : MxMemOp<(ops OtherVT), MxSize32, "b", "printAL32Mem", MxAddr32>;

def MxPCD : MxOpClass<"PCD">;
def MxPCI : MxOpClass<"PCI">;

let OperandType = "OPERAND_PCREL" in {
foreach size = ["8", "16", "32"] in {
defvar ResSize = !cast<MxSize>("MxSize"#size);
// PROGRAM COUNTER WITH DISPLACEMENT. This addressing mode requires one word of
// extension. The address of the operand is the sum of the address in the program
// counter and the Sign-extended 16-bit displacement integer in the extension
// word. The value in the program counter is the address of the extension word.
// The reference is classified as a program reference.
def MxPCD # size : MxMemOp<(ops i16imm), ResSize,  "q", "printPCD"#size#"Mem", MxPCD>;

// PROGRAM COUNTER WITH INDEX. This addressing mode requires one word of
// extension. The address is the sum of the address in the program counter, the
// sign-extended displacement integer in the lower eight bits of the extension
// word, and the contents of the index register.  The value in the program
// counter is the address of the extension word. This reference is classified as
// a program reference.
def MxPCI # size : MxMemOp<(ops i8imm:$disp, XR32:$index), ResSize,  "k", "printPCI"#size#"Mem", MxPCI>;
} // foreach size
} // OPERAND_PCREL

def MxImm : AsmOperandClass {
  let Name = "MxImm";
  let PredicateMethod = "isImm";
  let RenderMethod = "addImmOperands";
  let ParserMethod = "parseImm";
}

class MxOp<ValueType vt, MxSize size, string letter>
    : Operand<vt>,
      MxOperand<vt, size, letter, ?> {
  let ParserMatchClass = MxImm;
}

let OperandType = "OPERAND_IMMEDIATE",
    PrintMethod = "printImmediate" in {
// IMMEDIATE DATA. This addressing mode requires either one or two words of
// extension depending on the size of the operation.
//     Byte Operation - operand is low order byte of extension word
//     Word Operation - operand is extension word
//     Long Word Operation - operand is in the two extension words,
//                           high order 16 bits are in the first
//                           extension word, low order 16 bits are
//                           in the second extension word.
def Mxi8imm  : MxOp<i8,  MxSize8,  "i">;
def Mxi16imm : MxOp<i16, MxSize16, "i">;
def Mxi32imm : MxOp<i32, MxSize32, "i">;
} // OPERAND_IMMEDIATE

class MxBrTargetOperand<int N> : Operand<OtherVT> {
  let OperandType = "OPERAND_PCREL";
  let PrintMethod = "printPCRelImm";
  let ParserMatchClass = !cast<AsmOperandClass>("MxAddr"#N);
}
// Branch targets have OtherVT type and print as pc-relative values.
def MxBrTarget8  : MxBrTargetOperand<8>;
def MxBrTarget16 : MxBrTargetOperand<16>;
def MxBrTarget32 : MxBrTargetOperand<32>;

// Used with MOVEM
def MxMoveMaskClass : MxOpClass<"MoveMask">;
def MxMoveMask : MxOp<i16, MxSize16, "m"> {
  let OperandType = "OPERAND_IMMEDIATE";
  let PrintMethod = "printMoveMask";
  let ParserMatchClass = MxMoveMaskClass;
}

//===----------------------------------------------------------------------===//
// Predicates
//===----------------------------------------------------------------------===//

def SmallCode    : Predicate<"TM.getCodeModel() == CodeModel::Small">;
def KernelCode   : Predicate<"TM.getCodeModel() == CodeModel::Kernel">;
def FarData      : Predicate<"TM.getCodeModel() != CodeModel::Small &&"
                             "TM.getCodeModel() != CodeModel::Kernel">;
def NearData     : Predicate<"TM.getCodeModel() == CodeModel::Small ||"
                             "TM.getCodeModel() == CodeModel::Kernel">;
def IsPIC        : Predicate<"TM.isPositionIndependent()">;
def IsNotPIC     : Predicate<"!TM.isPositionIndependent()">;

// ISA versions
foreach i = [0,1,2,4,6] in
def AtLeastM680 # i # "0" : Predicate<"Subtarget->atLeastM680"#i#"0()">,
                            AssemblerPredicate<(all_of
                                                !cast<SubtargetFeature>("FeatureISA"#i#"0"))>;
def AtLeastM68881 : Predicate<"Subtarget->atLeastM68881()">,
                    AssemblerPredicate<(all_of FeatureISA881)>;
def AtLeastM68882 : Predicate<"Subtarget->atLeastM68882()">,
                    AssemblerPredicate<(all_of FeatureISA882)>;

//===----------------------------------------------------------------------===//
// Condition Codes
//
// These MUST be kept in sync with codes enum in M68kInstrInfo.h
//===----------------------------------------------------------------------===//

def MxCONDt   : PatLeaf<(i8 0)>;  // True
def MxCONDf   : PatLeaf<(i8 1)>;  // False
def MxCONDhi  : PatLeaf<(i8 2)>;  // High
def MxCONDls  : PatLeaf<(i8 3)>;  // Less or Same
def MxCONDcc  : PatLeaf<(i8 4)>;  // Carry Clear
def MxCONDcs  : PatLeaf<(i8 5)>;  // Carry Set
def MxCONDne  : PatLeaf<(i8 6)>;  // Not Equal
def MxCONDeq  : PatLeaf<(i8 7)>;  // Equal
def MxCONDvc  : PatLeaf<(i8 8)>;  // Overflow Clear
def MxCONDvs  : PatLeaf<(i8 9)>;  // Overflow Set
def MxCONDpl  : PatLeaf<(i8 10)>; // Plus
def MxCONDmi  : PatLeaf<(i8 11)>; // Minus
def MxCONDge  : PatLeaf<(i8 12)>; // Greater or Equal
def MxCONDlt  : PatLeaf<(i8 13)>; // Less Than
def MxCONDgt  : PatLeaf<(i8 14)>; // Greater Than
def MxCONDle  : PatLeaf<(i8 15)>; // Less or Equal


//===----------------------------------------------------------------------===//
// Complex Patterns
//===----------------------------------------------------------------------===//

// NOTE Though this CP is not strictly necessarily it will simplify instruciton
// definitions
def MxCP_ARI   : ComplexPattern<iPTR, 1, "SelectARI",
                                [], [SDNPWantParent]>;

def MxCP_ARIPI : ComplexPattern<iPTR, 1, "SelectARIPI",
                                [], [SDNPWantParent]>;

def MxCP_ARIPD : ComplexPattern<iPTR, 1, "SelectARIPD",
                                [], [SDNPWantParent]>;

def MxCP_ARID  : ComplexPattern<iPTR, 2, "SelectARID",
                                [add, sub, mul, or, shl, frameindex],
                                [SDNPWantParent]>;

def MxCP_ARII  : ComplexPattern<iPTR, 3, "SelectARII",
                                [add, sub, mul, or, shl, frameindex],
                                [SDNPWantParent]>;

def MxCP_AL    : ComplexPattern<iPTR, 1, "SelectAL",
                                [add, sub, mul, or, shl],
                                [SDNPWantParent]>;

def MxCP_PCD   : ComplexPattern<iPTR, 1, "SelectPCD",
                                [add, sub, mul, or, shl],
                                [SDNPWantParent]>;

def MxCP_PCI   : ComplexPattern<iPTR, 2, "SelectPCI",
                                [add, sub, mul, or, shl], [SDNPWantParent]>;


//===----------------------------------------------------------------------===//
// Pattern Fragments
//===----------------------------------------------------------------------===//

def Mxi8immSExt8  : PatLeaf<(i8  imm)>;
def MximmSExt8    : PatLeaf<(imm), [{ return isInt<8>(N->getSExtValue()); }]>;

def Mxi16immSExt16 : PatLeaf<(i16 imm)>;
def MximmSExt16    : PatLeaf<(imm), [{ return isInt<16>(N->getSExtValue()); }]>;

def Mxi32immSExt32 : PatLeaf<(i32 imm)>;
def MximmSExt32    : PatLeaf<(imm), [{ return isInt<32>(N->getSExtValue()); }]>;

// Used for Shifts and Rotations, since M68k immediates in these instructions
// are 1 <= i <= 8. Generally, if immediate is bigger than 8 it will be moved
// to a register and then an operation is performed.
//
// TODO Need to evaluate whether splitting one big shift(or rotate)
// into a few smaller is faster than doing a move, if so do custom lowering
def Mximm8_1to8   : ImmLeaf<i8,  [{ return Imm >= 1 && Imm <= 8; }]>;
def Mximm16_1to8  : ImmLeaf<i16, [{ return Imm >= 1 && Imm <= 8; }]>;
def Mximm32_1to8  : ImmLeaf<i32, [{ return Imm >= 1 && Imm <= 8; }]>;

// Helper fragments for loads.
// It's always safe to treat a anyext i16 load as a i32 load if the i16 is
// known to be 32-bit aligned or better. Ditto for i8 to i16.
def Mxloadi16 : PatFrag<(ops node:$ptr), (i16 (unindexedload node:$ptr)), [{
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::LoadExtType ExtType = LD->getExtensionType();
  if (ExtType == ISD::NON_EXTLOAD)
    return true;
  if (ExtType == ISD::EXTLOAD)
    return LD->getAlign() >= 2 && !LD->isSimple();
  return false;
}]>;

def Mxloadi32 : PatFrag<(ops node:$ptr), (i32 (unindexedload node:$ptr)), [{
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::LoadExtType ExtType = LD->getExtensionType();
  if (ExtType == ISD::NON_EXTLOAD)
    return true;
  if (ExtType == ISD::EXTLOAD)
    return LD->getAlign() >= 4 && !LD->isSimple();
  return false;
}]>;

def Mxloadi8         : PatFrag<(ops node:$ptr), (i8  (load node:$ptr))>;

def MxSExtLoadi16i8  : PatFrag<(ops node:$ptr), (i16 (sextloadi8 node:$ptr))>;
def MxSExtLoadi32i8  : PatFrag<(ops node:$ptr), (i32 (sextloadi8 node:$ptr))>;
def MxSExtLoadi32i16 : PatFrag<(ops node:$ptr), (i32 (sextloadi16 node:$ptr))>;

def MxZExtLoadi8i1   : PatFrag<(ops node:$ptr), (i8  (zextloadi1 node:$ptr))>;
def MxZExtLoadi16i1  : PatFrag<(ops node:$ptr), (i16 (zextloadi1 node:$ptr))>;
def MxZExtLoadi32i1  : PatFrag<(ops node:$ptr), (i32 (zextloadi1 node:$ptr))>;
def MxZExtLoadi16i8  : PatFrag<(ops node:$ptr), (i16 (zextloadi8 node:$ptr))>;
def MxZExtLoadi32i8  : PatFrag<(ops node:$ptr), (i32 (zextloadi8 node:$ptr))>;
def MxZExtLoadi32i16 : PatFrag<(ops node:$ptr), (i32 (zextloadi16 node:$ptr))>;

def MxExtLoadi8i1    : PatFrag<(ops node:$ptr), (i8  (extloadi1 node:$ptr))>;
def MxExtLoadi16i1   : PatFrag<(ops node:$ptr), (i16 (extloadi1 node:$ptr))>;
def MxExtLoadi32i1   : PatFrag<(ops node:$ptr), (i32 (extloadi1 node:$ptr))>;
def MxExtLoadi16i8   : PatFrag<(ops node:$ptr), (i16 (extloadi8 node:$ptr))>;
def MxExtLoadi32i8   : PatFrag<(ops node:$ptr), (i32 (extloadi8 node:$ptr))>;
def MxExtLoadi32i16  : PatFrag<(ops node:$ptr), (i32 (extloadi16 node:$ptr))>;


//===----------------------------------------------------------------------===//
// Type Fixtures
//
// Type Fixtures are ValueType related information sets that usually go together
//===----------------------------------------------------------------------===//

// TODO make it folded like MxType8.F.Op nad MxType8.F.Pat
// TODO move strings into META subclass
// vt: Type of data this fixture refers to
// prefix: Prefix used to identify type
// postfix: Prefix used to qualify type
class MxType<ValueType vt, string prefix, string postfix,
             // rLet: Register letter
             // rOp:  Supported any register operand
             string rLet, MxOperand rOp,
             // jOp:  Supported ARI operand
             // jPat: What ARI pattern to use
             MxOperand jOp, ComplexPattern jPat,
             // oOp:  Supported ARIPI operand
             // oPat: What ARIPI pattern is used
             MxOperand oOp, ComplexPattern oPat,
             // eOp:  Supported ARIPD operand
             // ePat: What ARIPD pattern is used
             MxOperand eOp, ComplexPattern ePat,
             // pOp:  Supported ARID operand
             // pPat: What ARID pattern is used
             MxOperand pOp, ComplexPattern pPat,
             // fOp:  Supported ARII operand
             // fPat: What ARII pattern is used
             MxOperand fOp, ComplexPattern fPat,
             // bOp:  Supported absolute operand
             // bPat: What absolute pattern is used
             MxOperand bOp, ComplexPattern bPat,
             // qOp:  Supported PCD operand
             // qPat: What PCD pattern is used
             MxOperand qOp, ComplexPattern qPat,
             // kOp:  Supported PCI operand
             // kPat: What PCI pattern is used
             MxOperand kOp, ComplexPattern kPat,
             // iOp:  Supported immediate operand
             // iPat: What immediate pattern is used
             MxOperand iOp, PatFrag iPat,
             // load: What load operation is used with MEM
             PatFrag load> {
  int Size = vt.Size;
  ValueType VT = vt;
  string Prefix = prefix;
  string Postfix = postfix;

  string RLet = rLet;
  MxOperand ROp = rOp;

  MxOperand JOp = jOp;
  ComplexPattern JPat = jPat;

  MxOperand OOp = oOp;
  ComplexPattern OPat = oPat;

  MxOperand EOp = eOp;
  ComplexPattern EPat = ePat;

  MxOperand POp = pOp;
  ComplexPattern PPat = pPat;

  MxOperand FOp = fOp;
  ComplexPattern FPat = fPat;

  MxOperand BOp = bOp;
  ComplexPattern BPat = bPat;

  MxOperand QOp = qOp;
  ComplexPattern QPat = qPat;

  MxOperand KOp = kOp;
  ComplexPattern KPat = kPat;

  MxOperand IOp = iOp;
  PatFrag IPat = iPat;

  PatFrag Load = load;
}

// Provides an alternative way to access the MxOperand and
// patterns w.r.t a specific addressing mode.
class MxOpBundle<int size, MxOperand op, ComplexPattern pat> {
  int Size = size;
  MxOperand Op = op;
  ComplexPattern Pat = pat;
}

class MxImmOpBundle<int size, MxOperand op, PatFrag pat>
  : MxOpBundle<size, op, ?> {
  PatFrag ImmPat = pat;
}

// TODO: We can use MxOp<S>AddrMode_<AM> in more places to
// replace MxType-based operand factoring.
foreach size = [8, 16, 32] in {
  // Dn
  def MxOp#size#AddrMode_d
    : MxOpBundle<size, !cast<MxOperand>("MxDRD"#size), ?>;

  // (An)
  def MxOp#size#AddrMode_j
    : MxOpBundle<size, !cast<MxOperand>("MxARI"#size), MxCP_ARI>;

  // (An)+
  def MxOp#size#AddrMode_o
    : MxOpBundle<size, !cast<MxOperand>("MxARIPI"#size), MxCP_ARIPI>;

  // -(An)
  def MxOp#size#AddrMode_e
    : MxOpBundle<size, !cast<MxOperand>("MxARIPD"#size), MxCP_ARIPD>;

  // (i,An)
  def MxOp#size#AddrMode_p
    : MxOpBundle<size, !cast<MxOperand>("MxARID"#size), MxCP_ARID>;

  // (i,An,Xn)
  def MxOp#size#AddrMode_f
    : MxOpBundle<size, !cast<MxOperand>("MxARII"#size), MxCP_ARII>;

  // (ABS).L
  def MxOp#size#AddrMode_b
    : MxOpBundle<size, !cast<MxOperand>("MxAL"#size), MxCP_AL>;

  // (i,PC)
  def MxOp#size#AddrMode_q
    : MxOpBundle<size, !cast<MxOperand>("MxPCD"#size), MxCP_PCD>;

  // (i,PC,Xn)
  def MxOp#size#AddrMode_k
    : MxOpBundle<size, !cast<MxOperand>("MxPCI"#size), MxCP_PCI>;

  // #imm
  def MxOp#size#AddrMode_i
    : MxImmOpBundle<size, !cast<MxOperand>("Mxi"#size#"imm"),
                    !cast<PatFrag>("Mxi"#size#"immSExt"#size)>;
} // foreach size = [8, 16, 32]

foreach size = [16, 32] in {
  // An
  def MxOp#size#AddrMode_a
    : MxOpBundle<size, !cast<MxOperand>("MxARD"#size), ?>;

  // Xn
  def MxOp#size#AddrMode_r
    : MxOpBundle<size, !cast<MxOperand>("MxXRD"#size), ?>;
} // foreach size = [16, 32]

foreach size = [32, 64, 80] in
def MxOp#size#AddrMode_fpr
  : MxOpBundle<size, !cast<MxOperand>("MxFPR"#size), ?>;

def MxOp32AddrMode_fpcs : MxOpBundle<32, MxFPCSR, ?>;
def MxOp32AddrMode_fpi  : MxOpBundle<32, MxFPIR, ?>;

class MxType8Class<string rLet, MxOperand reg>
    : MxType<i8, "b", "", rLet, reg,
             MxARI8,   MxCP_ARI,
             MxARIPI8, MxCP_ARIPI,
             MxARIPD8, MxCP_ARIPD,
             MxARID8,  MxCP_ARID,
             MxARII8,  MxCP_ARII,
             MxAL8,    MxCP_AL,
             MxPCD8,   MxCP_PCD,
             MxPCI8,   MxCP_PCI,
             Mxi8imm,  Mxi8immSExt8,
             Mxloadi8>;

def MxType8 : MxType8Class<?,?>;

class MxType16Class<string rLet, MxOperand reg>
    : MxType<i16, "w", "", rLet, reg,
             MxARI16,   MxCP_ARI,
             MxARIPI16, MxCP_ARIPI,
             MxARIPD16, MxCP_ARIPD,
             MxARID16,  MxCP_ARID,
             MxARII16,  MxCP_ARII,
             MxAL16,    MxCP_AL,
             MxPCD16,   MxCP_PCD,
             MxPCI16,   MxCP_PCI,
             Mxi16imm,  Mxi16immSExt16,
             Mxloadi16>;

def MxType16 : MxType16Class<?,?>;

class MxType32Class<string rLet, MxOperand reg>
    : MxType<i32, "l", "", rLet, reg,
             MxARI32,   MxCP_ARI,
             MxARIPI32, MxCP_ARIPI,
             MxARIPD32, MxCP_ARIPD,
             MxARID32,  MxCP_ARID,
             MxARII32,  MxCP_ARII,
             MxAL32,    MxCP_AL,
             MxPCD32,   MxCP_PCD,
             MxPCI32,   MxCP_PCI,
             Mxi32imm,  Mxi32immSExt32,
             Mxloadi32>;

def MxType32 : MxType32Class<?,?>;


def MxType8d : MxType8Class<"d", MxDRD8>;

def MxType16d : MxType16Class<"d", MxDRD16>;
def MxType16a : MxType16Class<"a", MxARD16>;
def MxType16r : MxType16Class<"r", MxXRD16>;
def MxType32d : MxType32Class<"d", MxDRD32>;
def MxType32a : MxType32Class<"a", MxARD32>;
def MxType32r : MxType32Class<"r", MxXRD32>;

let Postfix = "_TC" in {
def MxType16d_TC : MxType16Class<"d", MxDRD16_TC>;
def MxType16a_TC : MxType16Class<"a", MxARD16_TC>;
def MxType16r_TC : MxType16Class<"r", MxXRD16_TC>;
def MxType32d_TC : MxType32Class<"d", MxDRD32_TC>;
def MxType32a_TC : MxType32Class<"a", MxARD32_TC>;
def MxType32r_TC : MxType32Class<"r", MxXRD32_TC>;
}


//===----------------------------------------------------------------------===//
// Subsystems
//===----------------------------------------------------------------------===//

include "M68kInstrData.td"
include "M68kInstrShiftRotate.td"
include "M68kInstrBits.td"
include "M68kInstrArithmetic.td"
include "M68kInstrControl.td"
include "M68kInstrAtomics.td"

include "M68kInstrCompiler.td"
