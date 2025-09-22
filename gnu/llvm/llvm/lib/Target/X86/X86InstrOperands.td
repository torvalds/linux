//===------- X86InstrOperands.td - X86 Operand Definitions --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// A version of ptr_rc which excludes SP, ESP, and RSP. This is used for
// the index operand of an address, to conform to x86 encoding restrictions.
def ptr_rc_nosp : PointerLikeRegClass<1>;

// *mem - Operand definitions for the funky X86 addressing mode operands.
//
def X86MemAsmOperand : AsmOperandClass {
 let Name = "Mem";
}
let RenderMethod = "addMemOperands", SuperClasses = [X86MemAsmOperand] in {
  def X86Mem8AsmOperand   : AsmOperandClass { let Name = "Mem8"; }
  def X86Mem16AsmOperand  : AsmOperandClass { let Name = "Mem16"; }
  def X86Mem32AsmOperand  : AsmOperandClass { let Name = "Mem32"; }
  def X86Mem64AsmOperand  : AsmOperandClass { let Name = "Mem64"; }
  def X86Mem80AsmOperand  : AsmOperandClass { let Name = "Mem80"; }
  def X86Mem128AsmOperand : AsmOperandClass { let Name = "Mem128"; }
  def X86Mem256AsmOperand : AsmOperandClass { let Name = "Mem256"; }
  def X86Mem512AsmOperand : AsmOperandClass { let Name = "Mem512"; }
  // Gather mem operands
  def X86Mem64_RC128Operand  : AsmOperandClass { let Name = "Mem64_RC128"; }
  def X86Mem128_RC128Operand : AsmOperandClass { let Name = "Mem128_RC128"; }
  def X86Mem256_RC128Operand : AsmOperandClass { let Name = "Mem256_RC128"; }
  def X86Mem128_RC256Operand : AsmOperandClass { let Name = "Mem128_RC256"; }
  def X86Mem256_RC256Operand : AsmOperandClass { let Name = "Mem256_RC256"; }

  def X86Mem64_RC128XOperand  : AsmOperandClass { let Name = "Mem64_RC128X"; }
  def X86Mem128_RC128XOperand : AsmOperandClass { let Name = "Mem128_RC128X"; }
  def X86Mem256_RC128XOperand : AsmOperandClass { let Name = "Mem256_RC128X"; }
  def X86Mem128_RC256XOperand : AsmOperandClass { let Name = "Mem128_RC256X"; }
  def X86Mem256_RC256XOperand : AsmOperandClass { let Name = "Mem256_RC256X"; }
  def X86Mem512_RC256XOperand : AsmOperandClass { let Name = "Mem512_RC256X"; }
  def X86Mem256_RC512Operand  : AsmOperandClass { let Name = "Mem256_RC512"; }
  def X86Mem512_RC512Operand  : AsmOperandClass { let Name = "Mem512_RC512"; }
  def X86Mem512_GR16Operand : AsmOperandClass { let Name = "Mem512_GR16"; }
  def X86Mem512_GR32Operand : AsmOperandClass { let Name = "Mem512_GR32"; }
  def X86Mem512_GR64Operand : AsmOperandClass { let Name = "Mem512_GR64"; }

  def X86SibMemOperand : AsmOperandClass { let Name = "SibMem"; }
}

def X86AbsMemAsmOperand : AsmOperandClass {
  let Name = "AbsMem";
  let SuperClasses = [X86MemAsmOperand];
}

class X86MemOperand<string printMethod,
                    AsmOperandClass parserMatchClass = X86MemAsmOperand,
                    int size = 0> : Operand<iPTR> {
  let PrintMethod = printMethod;
  let MIOperandInfo = (ops ptr_rc, i8imm, ptr_rc_nosp, i32imm, SEGMENT_REG);
  let ParserMatchClass = parserMatchClass;
  let OperandType = "OPERAND_MEMORY";
  int Size = size;
}

// Gather mem operands
class X86VMemOperand<RegisterClass RC, string printMethod,
                     AsmOperandClass parserMatchClass, int size = 0>
    : X86MemOperand<printMethod, parserMatchClass, size> {
  let MIOperandInfo = (ops ptr_rc, i8imm, RC, i32imm, SEGMENT_REG);
}

def anymem : X86MemOperand<"printMemReference">;

// FIXME: Right now we allow any size during parsing, but we might want to
// restrict to only unsized memory.
def opaquemem : X86MemOperand<"printMemReference">;

def sibmem: X86MemOperand<"printMemReference", X86SibMemOperand>;

def i8mem   : X86MemOperand<"printbytemem",   X86Mem8AsmOperand, 8>;
def i16mem  : X86MemOperand<"printwordmem",  X86Mem16AsmOperand, 16>;
def i32mem  : X86MemOperand<"printdwordmem",  X86Mem32AsmOperand, 32>;
def i64mem  : X86MemOperand<"printqwordmem",  X86Mem64AsmOperand, 64>;
def i128mem : X86MemOperand<"printxmmwordmem", X86Mem128AsmOperand, 128>;
def i256mem : X86MemOperand<"printymmwordmem", X86Mem256AsmOperand, 256>;
def i512mem : X86MemOperand<"printzmmwordmem", X86Mem512AsmOperand, 512>;
def f16mem  : X86MemOperand<"printwordmem",   X86Mem16AsmOperand, 16>;
def f32mem  : X86MemOperand<"printdwordmem",  X86Mem32AsmOperand, 32>;
def f64mem  : X86MemOperand<"printqwordmem",  X86Mem64AsmOperand, 64>;
def f80mem  : X86MemOperand<"printtbytemem",  X86Mem80AsmOperand, 80>;
def f128mem : X86MemOperand<"printxmmwordmem", X86Mem128AsmOperand, 128>;
def f256mem : X86MemOperand<"printymmwordmem", X86Mem256AsmOperand, 256>;
def f512mem : X86MemOperand<"printzmmwordmem", X86Mem512AsmOperand, 512>;

// 32/64 mode specific mem operands
def i512mem_GR16 : X86MemOperand<"printzmmwordmem", X86Mem512_GR16Operand, 512>;
def i512mem_GR32 : X86MemOperand<"printzmmwordmem", X86Mem512_GR32Operand, 512>;
def i512mem_GR64 : X86MemOperand<"printzmmwordmem", X86Mem512_GR64Operand, 512>;

// Gather mem operands
def vx64mem  : X86VMemOperand<VR128,  "printqwordmem",  X86Mem64_RC128Operand, 64>;
def vx128mem : X86VMemOperand<VR128,  "printxmmwordmem", X86Mem128_RC128Operand, 128>;
def vx256mem : X86VMemOperand<VR128,  "printymmwordmem", X86Mem256_RC128Operand, 256>;
def vy128mem : X86VMemOperand<VR256,  "printxmmwordmem", X86Mem128_RC256Operand, 128>;
def vy256mem : X86VMemOperand<VR256,  "printymmwordmem", X86Mem256_RC256Operand, 256>;

def vx64xmem  : X86VMemOperand<VR128X, "printqwordmem",  X86Mem64_RC128XOperand, 64>;
def vx128xmem : X86VMemOperand<VR128X, "printxmmwordmem", X86Mem128_RC128XOperand, 128>;
def vx256xmem : X86VMemOperand<VR128X, "printymmwordmem", X86Mem256_RC128XOperand, 256>;
def vy128xmem : X86VMemOperand<VR256X, "printxmmwordmem", X86Mem128_RC256XOperand, 128>;
def vy256xmem : X86VMemOperand<VR256X, "printymmwordmem", X86Mem256_RC256XOperand, 256>;
def vy512xmem : X86VMemOperand<VR256X, "printzmmwordmem", X86Mem512_RC256XOperand, 512>;
def vz256mem  : X86VMemOperand<VR512,  "printymmwordmem", X86Mem256_RC512Operand, 256>;
def vz512mem  : X86VMemOperand<VR512,  "printzmmwordmem", X86Mem512_RC512Operand, 512>;

def shmem : X86MemOperand<"printwordmem", X86Mem16AsmOperand>;
def ssmem : X86MemOperand<"printdwordmem", X86Mem32AsmOperand>;
def sdmem : X86MemOperand<"printqwordmem", X86Mem64AsmOperand>;

// A version of i8mem for use on x86-64 and x32 that uses a NOREX GPR instead
// of a plain GPR, so that it doesn't potentially require a REX prefix.
def ptr_rc_norex : PointerLikeRegClass<2>;
def ptr_rc_norex_nosp : PointerLikeRegClass<3>;

def i8mem_NOREX : X86MemOperand<"printbytemem", X86Mem8AsmOperand, 8> {
  let MIOperandInfo = (ops ptr_rc_norex, i8imm, ptr_rc_norex_nosp, i32imm,
                       SEGMENT_REG);
}

// GPRs available for tailcall.
// It represents GR32_TC, GR64_TC or GR64_TCW64.
def ptr_rc_tailcall : PointerLikeRegClass<4>;

// Special i32mem for addresses of load folding tail calls. These are not
// allowed to use callee-saved registers since they must be scheduled
// after callee-saved register are popped.
def i32mem_TC : X86MemOperand<"printdwordmem", X86Mem32AsmOperand, 32> {
  let MIOperandInfo = (ops ptr_rc_tailcall, i8imm, ptr_rc_tailcall,
                       i32imm, SEGMENT_REG);
}

// Special i64mem for addresses of load folding tail calls. These are not
// allowed to use callee-saved registers since they must be scheduled
// after callee-saved register are popped.
def i64mem_TC : X86MemOperand<"printqwordmem", X86Mem64AsmOperand, 64> {
  let MIOperandInfo = (ops ptr_rc_tailcall, i8imm,
                       ptr_rc_tailcall, i32imm, SEGMENT_REG);
}

// Special parser to detect 16-bit mode to select 16-bit displacement.
def X86AbsMem16AsmOperand : AsmOperandClass {
  let Name = "AbsMem16";
  let RenderMethod = "addAbsMemOperands";
  let SuperClasses = [X86AbsMemAsmOperand];
}

// Branch targets print as pc-relative values.
class BranchTargetOperand<ValueType ty> : Operand<ty> {
  let OperandType = "OPERAND_PCREL";
  let PrintMethod = "printPCRelImm";
  let ParserMatchClass = X86AbsMemAsmOperand;
}

def i32imm_brtarget : BranchTargetOperand<i32>;
def i16imm_brtarget : BranchTargetOperand<i16>;

// 64-bits but only 32 bits are significant, and those bits are treated as being
// pc relative.
def i64i32imm_brtarget : BranchTargetOperand<i64>;

def brtarget : BranchTargetOperand<OtherVT>;
def brtarget8 : BranchTargetOperand<OtherVT>;
def brtarget16 : BranchTargetOperand<OtherVT> {
  let ParserMatchClass = X86AbsMem16AsmOperand;
}
def brtarget32 : BranchTargetOperand<OtherVT>;

let RenderMethod = "addSrcIdxOperands" in {
  def X86SrcIdx8Operand : AsmOperandClass {
    let Name = "SrcIdx8";
    let SuperClasses = [X86Mem8AsmOperand];
  }
  def X86SrcIdx16Operand : AsmOperandClass {
    let Name = "SrcIdx16";
    let SuperClasses = [X86Mem16AsmOperand];
  }
  def X86SrcIdx32Operand : AsmOperandClass {
    let Name = "SrcIdx32";
    let SuperClasses = [X86Mem32AsmOperand];
  }
  def X86SrcIdx64Operand : AsmOperandClass {
    let Name = "SrcIdx64";
    let SuperClasses = [X86Mem64AsmOperand];
  }
} // RenderMethod = "addSrcIdxOperands"

let RenderMethod = "addDstIdxOperands" in {
 def X86DstIdx8Operand : AsmOperandClass {
   let Name = "DstIdx8";
   let SuperClasses = [X86Mem8AsmOperand];
 }
 def X86DstIdx16Operand : AsmOperandClass {
   let Name = "DstIdx16";
   let SuperClasses = [X86Mem16AsmOperand];
 }
 def X86DstIdx32Operand : AsmOperandClass {
   let Name = "DstIdx32";
   let SuperClasses = [X86Mem32AsmOperand];
 }
 def X86DstIdx64Operand : AsmOperandClass {
   let Name = "DstIdx64";
   let SuperClasses = [X86Mem64AsmOperand];
 }
} // RenderMethod = "addDstIdxOperands"

let RenderMethod = "addMemOffsOperands" in {
  def X86MemOffs16_8AsmOperand : AsmOperandClass {
    let Name = "MemOffs16_8";
    let SuperClasses = [X86Mem8AsmOperand];
  }
  def X86MemOffs16_16AsmOperand : AsmOperandClass {
    let Name = "MemOffs16_16";
    let SuperClasses = [X86Mem16AsmOperand];
  }
  def X86MemOffs16_32AsmOperand : AsmOperandClass {
    let Name = "MemOffs16_32";
    let SuperClasses = [X86Mem32AsmOperand];
  }
  def X86MemOffs32_8AsmOperand : AsmOperandClass {
    let Name = "MemOffs32_8";
    let SuperClasses = [X86Mem8AsmOperand];
  }
  def X86MemOffs32_16AsmOperand : AsmOperandClass {
    let Name = "MemOffs32_16";
    let SuperClasses = [X86Mem16AsmOperand];
  }
  def X86MemOffs32_32AsmOperand : AsmOperandClass {
    let Name = "MemOffs32_32";
    let SuperClasses = [X86Mem32AsmOperand];
  }
  def X86MemOffs32_64AsmOperand : AsmOperandClass {
    let Name = "MemOffs32_64";
    let SuperClasses = [X86Mem64AsmOperand];
  }
  def X86MemOffs64_8AsmOperand : AsmOperandClass {
    let Name = "MemOffs64_8";
    let SuperClasses = [X86Mem8AsmOperand];
  }
  def X86MemOffs64_16AsmOperand : AsmOperandClass {
    let Name = "MemOffs64_16";
    let SuperClasses = [X86Mem16AsmOperand];
  }
  def X86MemOffs64_32AsmOperand : AsmOperandClass {
    let Name = "MemOffs64_32";
    let SuperClasses = [X86Mem32AsmOperand];
  }
  def X86MemOffs64_64AsmOperand : AsmOperandClass {
    let Name = "MemOffs64_64";
    let SuperClasses = [X86Mem64AsmOperand];
  }
} // RenderMethod = "addMemOffsOperands"

class X86SrcIdxOperand<string printMethod, AsmOperandClass parserMatchClass>
    : X86MemOperand<printMethod, parserMatchClass> {
  let MIOperandInfo = (ops ptr_rc, SEGMENT_REG);
}

class X86DstIdxOperand<string printMethod, AsmOperandClass parserMatchClass>
    : X86MemOperand<printMethod, parserMatchClass> {
  let MIOperandInfo = (ops ptr_rc);
}

def srcidx8  : X86SrcIdxOperand<"printSrcIdx8",  X86SrcIdx8Operand>;
def srcidx16 : X86SrcIdxOperand<"printSrcIdx16", X86SrcIdx16Operand>;
def srcidx32 : X86SrcIdxOperand<"printSrcIdx32", X86SrcIdx32Operand>;
def srcidx64 : X86SrcIdxOperand<"printSrcIdx64", X86SrcIdx64Operand>;
def dstidx8  : X86DstIdxOperand<"printDstIdx8",  X86DstIdx8Operand>;
def dstidx16 : X86DstIdxOperand<"printDstIdx16", X86DstIdx16Operand>;
def dstidx32 : X86DstIdxOperand<"printDstIdx32", X86DstIdx32Operand>;
def dstidx64 : X86DstIdxOperand<"printDstIdx64", X86DstIdx64Operand>;

class X86MemOffsOperand<Operand immOperand, string printMethod,
                        AsmOperandClass parserMatchClass>
    : X86MemOperand<printMethod, parserMatchClass> {
  let MIOperandInfo = (ops immOperand, SEGMENT_REG);
}

def offset16_8  : X86MemOffsOperand<i16imm, "printMemOffs8",
                                    X86MemOffs16_8AsmOperand>;
def offset16_16 : X86MemOffsOperand<i16imm, "printMemOffs16",
                                    X86MemOffs16_16AsmOperand>;
def offset16_32 : X86MemOffsOperand<i16imm, "printMemOffs32",
                                    X86MemOffs16_32AsmOperand>;
def offset32_8  : X86MemOffsOperand<i32imm, "printMemOffs8",
                                    X86MemOffs32_8AsmOperand>;
def offset32_16 : X86MemOffsOperand<i32imm, "printMemOffs16",
                                    X86MemOffs32_16AsmOperand>;
def offset32_32 : X86MemOffsOperand<i32imm, "printMemOffs32",
                                    X86MemOffs32_32AsmOperand>;
def offset32_64 : X86MemOffsOperand<i32imm, "printMemOffs64",
                                    X86MemOffs32_64AsmOperand>;
def offset64_8  : X86MemOffsOperand<i64imm, "printMemOffs8",
                                    X86MemOffs64_8AsmOperand>;
def offset64_16 : X86MemOffsOperand<i64imm, "printMemOffs16",
                                    X86MemOffs64_16AsmOperand>;
def offset64_32 : X86MemOffsOperand<i64imm, "printMemOffs32",
                                    X86MemOffs64_32AsmOperand>;
def offset64_64 : X86MemOffsOperand<i64imm, "printMemOffs64",
                                    X86MemOffs64_64AsmOperand>;

def ccode : Operand<i8> {
  let PrintMethod = "printCondCode";
  let OperandNamespace = "X86";
  let OperandType = "OPERAND_COND_CODE";
}

class ImmSExtAsmOperandClass : AsmOperandClass {
  let SuperClasses = [ImmAsmOperand];
  let RenderMethod = "addImmOperands";
}

def X86GR32orGR64AsmOperand : AsmOperandClass {
  let Name = "GR32orGR64";
}
def GR32orGR64 : RegisterOperand<GR32> {
  let ParserMatchClass = X86GR32orGR64AsmOperand;
}

def X86GR16orGR32orGR64AsmOperand : AsmOperandClass {
  let Name = "GR16orGR32orGR64";
}
def GR16orGR32orGR64 : RegisterOperand<GR16> {
  let ParserMatchClass = X86GR16orGR32orGR64AsmOperand;
}

def AVX512RCOperand : AsmOperandClass {
  let Name = "AVX512RC";
}
def AVX512RC : Operand<i32> {
  let PrintMethod = "printRoundingControl";
  let OperandNamespace = "X86";
  let OperandType = "OPERAND_ROUNDING_CONTROL";
  let ParserMatchClass = AVX512RCOperand;
}

// Sign-extended immediate classes. We don't need to define the full lattice
// here because there is no instruction with an ambiguity between ImmSExti64i32
// and ImmSExti32i8.
//
// The strange ranges come from the fact that the assembler always works with
// 64-bit immediates, but for a 16-bit target value we want to accept both "-1"
// (which will be a -1ULL), and "0xFF" (-1 in 16-bits).

// [0, 0x7FFFFFFF]                                            |
//   [0xFFFFFFFF80000000, 0xFFFFFFFFFFFFFFFF]
def ImmSExti64i32AsmOperand : ImmSExtAsmOperandClass {
  let Name = "ImmSExti64i32";
}

// [0, 0x0000007F] | [0x000000000000FF80, 0x000000000000FFFF] |
//   [0xFFFFFFFFFFFFFF80, 0xFFFFFFFFFFFFFFFF]
def ImmSExti16i8AsmOperand : ImmSExtAsmOperandClass {
  let Name = "ImmSExti16i8";
  let SuperClasses = [ImmSExti64i32AsmOperand];
}

// [0, 0x0000007F] | [0x00000000FFFFFF80, 0x00000000FFFFFFFF] |
//   [0xFFFFFFFFFFFFFF80, 0xFFFFFFFFFFFFFFFF]
def ImmSExti32i8AsmOperand : ImmSExtAsmOperandClass {
  let Name = "ImmSExti32i8";
}

// [0, 0x0000007F]                                            |
//   [0xFFFFFFFFFFFFFF80, 0xFFFFFFFFFFFFFFFF]
def ImmSExti64i8AsmOperand : ImmSExtAsmOperandClass {
  let Name = "ImmSExti64i8";
  let SuperClasses = [ImmSExti16i8AsmOperand, ImmSExti32i8AsmOperand,
                      ImmSExti64i32AsmOperand];
}

// 4-bit immediate used by some XOP instructions
// [0, 0xF]
def ImmUnsignedi4AsmOperand : AsmOperandClass {
  let Name = "ImmUnsignedi4";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidImmUnsignedi4";
}

// Unsigned immediate used by SSE/AVX instructions
// [0, 0xFF]
//   [0xFFFFFFFFFFFFFF80, 0xFFFFFFFFFFFFFFFF]
def ImmUnsignedi8AsmOperand : AsmOperandClass {
  let Name = "ImmUnsignedi8";
  let RenderMethod = "addImmOperands";
}

// A couple of more descriptive operand definitions.
// 16-bits but only 8 bits are significant.
def i16i8imm  : Operand<i16> {
  let ParserMatchClass = ImmSExti16i8AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}
// 32-bits but only 8 bits are significant.
def i32i8imm  : Operand<i32> {
  let ParserMatchClass = ImmSExti32i8AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// 64-bits but only 32 bits are significant.
def i64i32imm  : Operand<i64> {
  let ParserMatchClass = ImmSExti64i32AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// 64-bits but only 8 bits are significant.
def i64i8imm   : Operand<i64> {
  let ParserMatchClass = ImmSExti64i8AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// Unsigned 4-bit immediate used by some XOP instructions.
def u4imm : Operand<i8> {
  let PrintMethod = "printU8Imm";
  let ParserMatchClass = ImmUnsignedi4AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

def cflags : Operand<i8> {
  let PrintMethod = "printCondFlags";
  let ParserMatchClass = ImmUnsignedi4AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// Unsigned 8-bit immediate used by SSE/AVX instructions.
def u8imm : Operand<i8> {
  let PrintMethod = "printU8Imm";
  let ParserMatchClass = ImmUnsignedi8AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// 16-bit immediate but only 8-bits are significant and they are unsigned.
// Used by BT instructions.
def i16u8imm : Operand<i16> {
  let PrintMethod = "printU8Imm";
  let ParserMatchClass = ImmUnsignedi8AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// 32-bit immediate but only 8-bits are significant and they are unsigned.
// Used by some SSE/AVX instructions that use intrinsics.
def i32u8imm : Operand<i32> {
  let PrintMethod = "printU8Imm";
  let ParserMatchClass = ImmUnsignedi8AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// 64-bit immediate but only 8-bits are significant and they are unsigned.
// Used by BT instructions.
def i64u8imm : Operand<i64> {
  let PrintMethod = "printU8Imm";
  let ParserMatchClass = ImmUnsignedi8AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

def lea64_32mem : Operand<i32> {
  let PrintMethod = "printMemReference";
  let MIOperandInfo = (ops GR64, i8imm, GR64_NOSP, i32imm, SEGMENT_REG);
  let ParserMatchClass = X86MemAsmOperand;
}

// Memory operands that use 64-bit pointers in both ILP32 and LP64.
def lea64mem : Operand<i64> {
  let PrintMethod = "printMemReference";
  let MIOperandInfo = (ops GR64, i8imm, GR64_NOSP, i32imm, SEGMENT_REG);
  let ParserMatchClass = X86MemAsmOperand;
}

let RenderMethod = "addMaskPairOperands" in {
  def VK1PairAsmOperand : AsmOperandClass { let Name = "VK1Pair"; }
  def VK2PairAsmOperand : AsmOperandClass { let Name = "VK2Pair"; }
  def VK4PairAsmOperand : AsmOperandClass { let Name = "VK4Pair"; }
  def VK8PairAsmOperand : AsmOperandClass { let Name = "VK8Pair"; }
  def VK16PairAsmOperand : AsmOperandClass { let Name = "VK16Pair"; }
}

def VK1Pair : RegisterOperand<VK1PAIR, "printVKPair"> {
  let ParserMatchClass = VK1PairAsmOperand;
}

def VK2Pair : RegisterOperand<VK2PAIR, "printVKPair"> {
  let ParserMatchClass = VK2PairAsmOperand;
}

def VK4Pair : RegisterOperand<VK4PAIR, "printVKPair"> {
  let ParserMatchClass = VK4PairAsmOperand;
}

def VK8Pair : RegisterOperand<VK8PAIR, "printVKPair"> {
  let ParserMatchClass = VK8PairAsmOperand;
}

def VK16Pair : RegisterOperand<VK16PAIR, "printVKPair"> {
  let ParserMatchClass = VK16PairAsmOperand;
}
