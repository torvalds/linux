//===- X86RegisterInfo.td - Describe the X86 Register File --*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the X86 Register file, defining the registers themselves,
// aliases between the registers, and the register classes built out of the
// registers.
//
//===----------------------------------------------------------------------===//

class X86Reg<string n, bits<16> Enc, list<Register> subregs = []> : Register<n> {
  let Namespace = "X86";
  let HWEncoding = Enc;
  let SubRegs = subregs;
}

// Subregister indices.
let Namespace = "X86" in {
  def sub_8bit     : SubRegIndex<8>;
  def sub_8bit_hi  : SubRegIndex<8, 8>;
  def sub_8bit_hi_phony  : SubRegIndex<8, 8>;
  def sub_16bit    : SubRegIndex<16>;
  def sub_16bit_hi : SubRegIndex<16, 16>;
  def sub_32bit    : SubRegIndex<32>;
  def sub_xmm      : SubRegIndex<128>;
  def sub_ymm      : SubRegIndex<256>;
  def sub_mask_0   : SubRegIndex<-1>;
  def sub_mask_1   : SubRegIndex<-1, -1>;
}

//===----------------------------------------------------------------------===//
//  Register definitions...
//

// In the register alias definitions below, we define which registers alias
// which others.  We only specify which registers the small registers alias,
// because the register file generator is smart enough to figure out that
// AL aliases AX if we tell it that AX aliased AL (for example).

// Dwarf numbering is different for 32-bit and 64-bit, and there are
// variations by target as well. Currently the first entry is for X86-64,
// second - for EH on X86-32/Darwin and third is 'generic' one (X86-32/Linux
// and debug information on X86-32/Darwin)

// 8-bit registers
// Low registers
def AL : X86Reg<"al", 0>;
def DL : X86Reg<"dl", 2>;
def CL : X86Reg<"cl", 1>;
def BL : X86Reg<"bl", 3>;

// High registers. On x86-64, these cannot be used in any instruction
// with a REX prefix.
def AH : X86Reg<"ah", 4>;
def DH : X86Reg<"dh", 6>;
def CH : X86Reg<"ch", 5>;
def BH : X86Reg<"bh", 7>;

// X86-64 only, requires REX.
def SIL  : X86Reg<"sil",   6>;
def DIL  : X86Reg<"dil",   7>;
def BPL  : X86Reg<"bpl",   5>;
def SPL  : X86Reg<"spl",   4>;
def R8B  : X86Reg<"r8b",   8>;
def R9B  : X86Reg<"r9b",   9>;
def R10B : X86Reg<"r10b", 10>;
def R11B : X86Reg<"r11b", 11>;
def R12B : X86Reg<"r12b", 12>;
def R13B : X86Reg<"r13b", 13>;
def R14B : X86Reg<"r14b", 14>;
def R15B : X86Reg<"r15b", 15>;
// RAGreedy prefers to select a cheaper register
// For x86,
//   Cost(caller-save reg) < Cost(callee-save reg)
// b/c callee-save register needs push/pop in prolog/epilog.
// If both registers are callee-saved or caller-saved,
//   Cost(short-encoding reg) < Cost(long-encoding reg)
//
// To achieve this, we do the following things:
//   1. Set CostPerUse=1 for registers that need prefix
//   2. Consider callee-save register is never cheaper than a register w/ cost 1
//   3. List caller-save register before callee-save regsiter in RegisterClass
//      or AllocationOrder
//
// NOTE:
//   D133902 stopped assigning register costs for R8-R15, which brought gain
//   and regression. We don't know if we should assign cost to R16-R31 w/o
//   performance data.
// TODO:
//   Update the comment/cost after tuning.
// APX only, requires REX2 or EVEX.
let PositionOrder = 4 in {
def R16B : X86Reg<"r16b", 16>;
def R17B : X86Reg<"r17b", 17>;
def R18B : X86Reg<"r18b", 18>;
def R19B : X86Reg<"r19b", 19>;
def R20B : X86Reg<"r20b", 20>;
def R21B : X86Reg<"r21b", 21>;
def R22B : X86Reg<"r22b", 22>;
def R23B : X86Reg<"r23b", 23>;
def R24B : X86Reg<"r24b", 24>;
def R25B : X86Reg<"r25b", 25>;
def R26B : X86Reg<"r26b", 26>;
def R27B : X86Reg<"r27b", 27>;
def R28B : X86Reg<"r28b", 28>;
def R29B : X86Reg<"r29b", 29>;
def R30B : X86Reg<"r30b", 30>;
def R31B : X86Reg<"r31b", 31>;
}

let isArtificial = 1 in {
// High byte of the low 16 bits of the super-register:
def SIH   : X86Reg<"", -1>;
def DIH   : X86Reg<"", -1>;
def BPH   : X86Reg<"", -1>;
def SPH   : X86Reg<"", -1>;
def R8BH  : X86Reg<"", -1>;
def R9BH  : X86Reg<"", -1>;
def R10BH : X86Reg<"", -1>;
def R11BH : X86Reg<"", -1>;
def R12BH : X86Reg<"", -1>;
def R13BH : X86Reg<"", -1>;
def R14BH : X86Reg<"", -1>;
def R15BH : X86Reg<"", -1>;
let PositionOrder = 4 in {
def R16BH : X86Reg<"", -1>;
def R17BH : X86Reg<"", -1>;
def R18BH : X86Reg<"", -1>;
def R19BH : X86Reg<"", -1>;
def R20BH : X86Reg<"", -1>;
def R21BH : X86Reg<"", -1>;
def R22BH : X86Reg<"", -1>;
def R23BH : X86Reg<"", -1>;
def R24BH : X86Reg<"", -1>;
def R25BH : X86Reg<"", -1>;
def R26BH : X86Reg<"", -1>;
def R27BH : X86Reg<"", -1>;
def R28BH : X86Reg<"", -1>;
def R29BH : X86Reg<"", -1>;
def R30BH : X86Reg<"", -1>;
def R31BH : X86Reg<"", -1>;
}
// High word of the low 32 bits of the super-register:
def HAX   : X86Reg<"", -1>;
def HDX   : X86Reg<"", -1>;
def HCX   : X86Reg<"", -1>;
def HBX   : X86Reg<"", -1>;
def HSI   : X86Reg<"", -1>;
def HDI   : X86Reg<"", -1>;
def HBP   : X86Reg<"", -1>;
def HSP   : X86Reg<"", -1>;
def HIP   : X86Reg<"", -1>;
def R8WH  : X86Reg<"", -1>;
def R9WH  : X86Reg<"", -1>;
def R10WH : X86Reg<"", -1>;
def R11WH : X86Reg<"", -1>;
def R12WH : X86Reg<"", -1>;
def R13WH : X86Reg<"", -1>;
def R14WH : X86Reg<"", -1>;
def R15WH : X86Reg<"", -1>;
let PositionOrder = 4 in {
def R16WH : X86Reg<"", -1>;
def R17WH : X86Reg<"", -1>;
def R18WH : X86Reg<"", -1>;
def R19WH : X86Reg<"", -1>;
def R20WH : X86Reg<"", -1>;
def R21WH : X86Reg<"", -1>;
def R22WH : X86Reg<"", -1>;
def R23WH : X86Reg<"", -1>;
def R24WH : X86Reg<"", -1>;
def R25WH : X86Reg<"", -1>;
def R26WH : X86Reg<"", -1>;
def R27WH : X86Reg<"", -1>;
def R28WH : X86Reg<"", -1>;
def R29WH : X86Reg<"", -1>;
def R30WH : X86Reg<"", -1>;
def R31WH : X86Reg<"", -1>;
}
}

// 16-bit registers
let SubRegIndices = [sub_8bit, sub_8bit_hi], CoveredBySubRegs = 1 in {
def AX : X86Reg<"ax", 0, [AL,AH]>;
def DX : X86Reg<"dx", 2, [DL,DH]>;
def CX : X86Reg<"cx", 1, [CL,CH]>;
def BX : X86Reg<"bx", 3, [BL,BH]>;
}
let SubRegIndices = [sub_8bit, sub_8bit_hi_phony], CoveredBySubRegs = 1 in {
def SI : X86Reg<"si", 6, [SIL,SIH]>;
def DI : X86Reg<"di", 7, [DIL,DIH]>;
def BP : X86Reg<"bp", 5, [BPL,BPH]>;
def SP : X86Reg<"sp", 4, [SPL,SPH]>;
}
def IP : X86Reg<"ip", 0>;

// X86-64 only, requires REX.
let SubRegIndices = [sub_8bit, sub_8bit_hi_phony], CoveredBySubRegs = 1 in {
def R8W  : X86Reg<"r8w",   8, [R8B,R8BH]>;
def R9W  : X86Reg<"r9w",   9, [R9B,R9BH]>;
def R10W : X86Reg<"r10w", 10, [R10B,R10BH]>;
def R11W : X86Reg<"r11w", 11, [R11B,R11BH]>;
def R12W : X86Reg<"r12w", 12, [R12B,R12BH]>;
def R13W : X86Reg<"r13w", 13, [R13B,R13BH]>;
def R14W : X86Reg<"r14w", 14, [R14B,R14BH]>;
def R15W : X86Reg<"r15w", 15, [R15B,R15BH]>;
}
// APX only, requires REX2 or EVEX.
let SubRegIndices = [sub_8bit, sub_8bit_hi_phony], CoveredBySubRegs = 1 in {
let PositionOrder = 4 in {
def R16W : X86Reg<"r16w", 16, [R16B,R16BH]>;
def R17W : X86Reg<"r17w", 17, [R17B,R17BH]>;
def R18W : X86Reg<"r18w", 18, [R18B,R18BH]>;
def R19W : X86Reg<"r19w", 19, [R19B,R19BH]>;
def R20W : X86Reg<"r20w", 20, [R20B,R20BH]>;
def R21W : X86Reg<"r21w", 21, [R21B,R21BH]>;
def R22W : X86Reg<"r22w", 22, [R22B,R22BH]>;
def R23W : X86Reg<"r23w", 23, [R23B,R23BH]>;
def R24W : X86Reg<"r24w", 24, [R24B,R24BH]>;
def R25W : X86Reg<"r25w", 25, [R25B,R25BH]>;
def R26W : X86Reg<"r26w", 26, [R26B,R26BH]>;
def R27W : X86Reg<"r27w", 27, [R27B,R27BH]>;
def R28W : X86Reg<"r28w", 28, [R28B,R28BH]>;
def R29W : X86Reg<"r29w", 29, [R29B,R29BH]>;
def R30W : X86Reg<"r30w", 30, [R30B,R30BH]>;
def R31W : X86Reg<"r31w", 31, [R31B,R31BH]>;
}
}

// 32-bit registers
let SubRegIndices = [sub_16bit, sub_16bit_hi], CoveredBySubRegs = 1 in {
def EAX : X86Reg<"eax", 0, [AX, HAX]>, DwarfRegNum<[-2, 0, 0]>;
def EDX : X86Reg<"edx", 2, [DX, HDX]>, DwarfRegNum<[-2, 2, 2]>;
def ECX : X86Reg<"ecx", 1, [CX, HCX]>, DwarfRegNum<[-2, 1, 1]>;
def EBX : X86Reg<"ebx", 3, [BX, HBX]>, DwarfRegNum<[-2, 3, 3]>;
def ESI : X86Reg<"esi", 6, [SI, HSI]>, DwarfRegNum<[-2, 6, 6]>;
def EDI : X86Reg<"edi", 7, [DI, HDI]>, DwarfRegNum<[-2, 7, 7]>;
def EBP : X86Reg<"ebp", 5, [BP, HBP]>, DwarfRegNum<[-2, 4, 5]>;
def ESP : X86Reg<"esp", 4, [SP, HSP]>, DwarfRegNum<[-2, 5, 4]>;
def EIP : X86Reg<"eip", 0, [IP, HIP]>, DwarfRegNum<[-2, 8, 8]>;
}

// X86-64 only, requires REX
let SubRegIndices = [sub_16bit, sub_16bit_hi], CoveredBySubRegs = 1 in {
def R8D  : X86Reg<"r8d",   8, [R8W,R8WH]>;
def R9D  : X86Reg<"r9d",   9, [R9W,R9WH]>;
def R10D : X86Reg<"r10d", 10, [R10W,R10WH]>;
def R11D : X86Reg<"r11d", 11, [R11W,R11WH]>;
def R12D : X86Reg<"r12d", 12, [R12W,R12WH]>;
def R13D : X86Reg<"r13d", 13, [R13W,R13WH]>;
def R14D : X86Reg<"r14d", 14, [R14W,R14WH]>;
def R15D : X86Reg<"r15d", 15, [R15W,R15WH]>;
}

// APX only, requires REX2 or EVEX.
let SubRegIndices = [sub_16bit, sub_16bit_hi], CoveredBySubRegs = 1 in {
let PositionOrder = 4 in {
def R16D : X86Reg<"r16d", 16, [R16W,R16WH]>;
def R17D : X86Reg<"r17d", 17, [R17W,R17WH]>;
def R18D : X86Reg<"r18d", 18, [R18W,R18WH]>;
def R19D : X86Reg<"r19d", 19, [R19W,R19WH]>;
def R20D : X86Reg<"r20d", 20, [R20W,R20WH]>;
def R21D : X86Reg<"r21d", 21, [R21W,R21WH]>;
def R22D : X86Reg<"r22d", 22, [R22W,R22WH]>;
def R23D : X86Reg<"r23d", 23, [R23W,R23WH]>;
def R24D : X86Reg<"r24d", 24, [R24W,R24WH]>;
def R25D : X86Reg<"r25d", 25, [R25W,R25WH]>;
def R26D : X86Reg<"r26d", 26, [R26W,R26WH]>;
def R27D : X86Reg<"r27d", 27, [R27W,R27WH]>;
def R28D : X86Reg<"r28d", 28, [R28W,R28WH]>;
def R29D : X86Reg<"r29d", 29, [R29W,R29WH]>;
def R30D : X86Reg<"r30d", 30, [R30W,R30WH]>;
def R31D : X86Reg<"r31d", 31, [R31W,R31WH]>;
}
}
// 64-bit registers, X86-64 only
let SubRegIndices = [sub_32bit] in {
def RAX : X86Reg<"rax", 0, [EAX]>, DwarfRegNum<[0, -2, -2]>;
def RDX : X86Reg<"rdx", 2, [EDX]>, DwarfRegNum<[1, -2, -2]>;
def RCX : X86Reg<"rcx", 1, [ECX]>, DwarfRegNum<[2, -2, -2]>;
def RBX : X86Reg<"rbx", 3, [EBX]>, DwarfRegNum<[3, -2, -2]>;
def RSI : X86Reg<"rsi", 6, [ESI]>, DwarfRegNum<[4, -2, -2]>;
def RDI : X86Reg<"rdi", 7, [EDI]>, DwarfRegNum<[5, -2, -2]>;
def RBP : X86Reg<"rbp", 5, [EBP]>, DwarfRegNum<[6, -2, -2]>;
def RSP : X86Reg<"rsp", 4, [ESP]>, DwarfRegNum<[7, -2, -2]>;

// These also require REX.
def R8  : X86Reg<"r8",   8, [R8D]>,  DwarfRegNum<[ 8, -2, -2]>;
def R9  : X86Reg<"r9",   9, [R9D]>,  DwarfRegNum<[ 9, -2, -2]>;
def R10 : X86Reg<"r10", 10, [R10D]>, DwarfRegNum<[10, -2, -2]>;
def R11 : X86Reg<"r11", 11, [R11D]>, DwarfRegNum<[11, -2, -2]>;
def R12 : X86Reg<"r12", 12, [R12D]>, DwarfRegNum<[12, -2, -2]>;
def R13 : X86Reg<"r13", 13, [R13D]>, DwarfRegNum<[13, -2, -2]>;
def R14 : X86Reg<"r14", 14, [R14D]>, DwarfRegNum<[14, -2, -2]>;
def R15 : X86Reg<"r15", 15, [R15D]>, DwarfRegNum<[15, -2, -2]>;
def RIP : X86Reg<"rip",  0, [EIP]>,  DwarfRegNum<[16, -2, -2]>;
// APX only, requires REX2 or EVEX.
let PositionOrder = 4 in {
def R16 : X86Reg<"r16", 16, [R16D]>, DwarfRegNum<[130, -2, -2]>;
def R17 : X86Reg<"r17", 17, [R17D]>, DwarfRegNum<[131, -2, -2]>;
def R18 : X86Reg<"r18", 18, [R18D]>, DwarfRegNum<[132, -2, -2]>;
def R19 : X86Reg<"r19", 19, [R19D]>, DwarfRegNum<[133, -2, -2]>;
def R20 : X86Reg<"r20", 20, [R20D]>, DwarfRegNum<[134, -2, -2]>;
def R21 : X86Reg<"r21", 21, [R21D]>, DwarfRegNum<[135, -2, -2]>;
def R22 : X86Reg<"r22", 22, [R22D]>, DwarfRegNum<[136, -2, -2]>;
def R23 : X86Reg<"r23", 23, [R23D]>, DwarfRegNum<[137, -2, -2]>;
def R24 : X86Reg<"r24", 24, [R24D]>, DwarfRegNum<[138, -2, -2]>;
def R25 : X86Reg<"r25", 25, [R25D]>, DwarfRegNum<[139, -2, -2]>;
def R26 : X86Reg<"r26", 26, [R26D]>, DwarfRegNum<[140, -2, -2]>;
def R27 : X86Reg<"r27", 27, [R27D]>, DwarfRegNum<[141, -2, -2]>;
def R28 : X86Reg<"r28", 28, [R28D]>, DwarfRegNum<[142, -2, -2]>;
def R29 : X86Reg<"r29", 29, [R29D]>, DwarfRegNum<[143, -2, -2]>;
def R30 : X86Reg<"r30", 30, [R30D]>, DwarfRegNum<[144, -2, -2]>;
def R31 : X86Reg<"r31", 31, [R31D]>, DwarfRegNum<[145, -2, -2]>;
}
}

// MMX Registers. These are actually aliased to ST0 .. ST7
def MM0 : X86Reg<"mm0", 0>, DwarfRegNum<[41, 29, 29]>;
def MM1 : X86Reg<"mm1", 1>, DwarfRegNum<[42, 30, 30]>;
def MM2 : X86Reg<"mm2", 2>, DwarfRegNum<[43, 31, 31]>;
def MM3 : X86Reg<"mm3", 3>, DwarfRegNum<[44, 32, 32]>;
def MM4 : X86Reg<"mm4", 4>, DwarfRegNum<[45, 33, 33]>;
def MM5 : X86Reg<"mm5", 5>, DwarfRegNum<[46, 34, 34]>;
def MM6 : X86Reg<"mm6", 6>, DwarfRegNum<[47, 35, 35]>;
def MM7 : X86Reg<"mm7", 7>, DwarfRegNum<[48, 36, 36]>;

// Pseudo Floating Point registers
def FP0 : X86Reg<"fp0", 0>;
def FP1 : X86Reg<"fp1", 0>;
def FP2 : X86Reg<"fp2", 0>;
def FP3 : X86Reg<"fp3", 0>;
def FP4 : X86Reg<"fp4", 0>;
def FP5 : X86Reg<"fp5", 0>;
def FP6 : X86Reg<"fp6", 0>;
def FP7 : X86Reg<"fp7", 0>;

// XMM Registers, used by the various SSE instruction set extensions.
def XMM0: X86Reg<"xmm0", 0>, DwarfRegNum<[17, 21, 21]>;
def XMM1: X86Reg<"xmm1", 1>, DwarfRegNum<[18, 22, 22]>;
def XMM2: X86Reg<"xmm2", 2>, DwarfRegNum<[19, 23, 23]>;
def XMM3: X86Reg<"xmm3", 3>, DwarfRegNum<[20, 24, 24]>;
def XMM4: X86Reg<"xmm4", 4>, DwarfRegNum<[21, 25, 25]>;
def XMM5: X86Reg<"xmm5", 5>, DwarfRegNum<[22, 26, 26]>;
def XMM6: X86Reg<"xmm6", 6>, DwarfRegNum<[23, 27, 27]>;
def XMM7: X86Reg<"xmm7", 7>, DwarfRegNum<[24, 28, 28]>;

// X86-64 only
def XMM8:  X86Reg<"xmm8",   8>, DwarfRegNum<[25, -2, -2]>;
def XMM9:  X86Reg<"xmm9",   9>, DwarfRegNum<[26, -2, -2]>;
def XMM10: X86Reg<"xmm10", 10>, DwarfRegNum<[27, -2, -2]>;
def XMM11: X86Reg<"xmm11", 11>, DwarfRegNum<[28, -2, -2]>;
def XMM12: X86Reg<"xmm12", 12>, DwarfRegNum<[29, -2, -2]>;
def XMM13: X86Reg<"xmm13", 13>, DwarfRegNum<[30, -2, -2]>;
def XMM14: X86Reg<"xmm14", 14>, DwarfRegNum<[31, -2, -2]>;
def XMM15: X86Reg<"xmm15", 15>, DwarfRegNum<[32, -2, -2]>;

let PositionOrder = 2 in {
// XMM16-31 registers, used by AVX-512 instructions.
def XMM16:  X86Reg<"xmm16", 16>, DwarfRegNum<[67, -2, -2]>;
def XMM17:  X86Reg<"xmm17", 17>, DwarfRegNum<[68, -2, -2]>;
def XMM18:  X86Reg<"xmm18", 18>, DwarfRegNum<[69, -2, -2]>;
def XMM19:  X86Reg<"xmm19", 19>, DwarfRegNum<[70, -2, -2]>;
def XMM20:  X86Reg<"xmm20", 20>, DwarfRegNum<[71, -2, -2]>;
def XMM21:  X86Reg<"xmm21", 21>, DwarfRegNum<[72, -2, -2]>;
def XMM22:  X86Reg<"xmm22", 22>, DwarfRegNum<[73, -2, -2]>;
def XMM23:  X86Reg<"xmm23", 23>, DwarfRegNum<[74, -2, -2]>;
def XMM24:  X86Reg<"xmm24", 24>, DwarfRegNum<[75, -2, -2]>;
def XMM25:  X86Reg<"xmm25", 25>, DwarfRegNum<[76, -2, -2]>;
def XMM26:  X86Reg<"xmm26", 26>, DwarfRegNum<[77, -2, -2]>;
def XMM27:  X86Reg<"xmm27", 27>, DwarfRegNum<[78, -2, -2]>;
def XMM28:  X86Reg<"xmm28", 28>, DwarfRegNum<[79, -2, -2]>;
def XMM29:  X86Reg<"xmm29", 29>, DwarfRegNum<[80, -2, -2]>;
def XMM30:  X86Reg<"xmm30", 30>, DwarfRegNum<[81, -2, -2]>;
def XMM31:  X86Reg<"xmm31", 31>, DwarfRegNum<[82, -2, -2]>;
}

// YMM0-15 registers, used by AVX instructions and
// YMM16-31 registers, used by AVX-512 instructions.
let SubRegIndices = [sub_xmm], PositionOrder = 1 in {
  foreach  Index = 0-15 in {
    def YMM#Index : X86Reg<"ymm"#Index, Index, [!cast<X86Reg>("XMM"#Index)]>,
                    DwarfRegAlias<!cast<X86Reg>("XMM"#Index)>;
  }
}
let SubRegIndices = [sub_xmm], PositionOrder = 2 in {
  foreach  Index = 16-31 in {
    def YMM#Index : X86Reg<"ymm"#Index, Index, [!cast<X86Reg>("XMM"#Index)]>,
                    DwarfRegAlias<!cast<X86Reg>("XMM"#Index)>;
  }
}


// ZMM Registers, used by AVX-512 instructions.
let SubRegIndices = [sub_ymm], PositionOrder = 2 in {
  foreach  Index = 0-31 in {
    def ZMM#Index : X86Reg<"zmm"#Index, Index, [!cast<X86Reg>("YMM"#Index)]>,
                    DwarfRegAlias<!cast<X86Reg>("XMM"#Index)>;
  }
}

let PositionOrder = 2 in {
// Mask Registers, used by AVX-512 instructions.
def K0 : X86Reg<"k0", 0>, DwarfRegNum<[118,  93,  93]>;
def K1 : X86Reg<"k1", 1>, DwarfRegNum<[119,  94,  94]>;
def K2 : X86Reg<"k2", 2>, DwarfRegNum<[120,  95,  95]>;
def K3 : X86Reg<"k3", 3>, DwarfRegNum<[121,  96,  96]>;
def K4 : X86Reg<"k4", 4>, DwarfRegNum<[122,  97,  97]>;
def K5 : X86Reg<"k5", 5>, DwarfRegNum<[123,  98,  98]>;
def K6 : X86Reg<"k6", 6>, DwarfRegNum<[124,  99,  99]>;
def K7 : X86Reg<"k7", 7>, DwarfRegNum<[125, 100, 100]>;
// Mask register pairs
def KPAIRS : RegisterTuples<[sub_mask_0, sub_mask_1],
                             [(add K0, K2, K4, K6), (add K1, K3, K5, K7)]>;
}

// TMM registers, used by AMX instructions.
let PositionOrder = 3 in {
// Tile config registers.
def TMMCFG: X86Reg<"tmmcfg", 0>;
// Tile "registers".
def TMM0:  X86Reg<"tmm0",   0>;
def TMM1:  X86Reg<"tmm1",   1>;
def TMM2:  X86Reg<"tmm2",   2>;
def TMM3:  X86Reg<"tmm3",   3>;
def TMM4:  X86Reg<"tmm4",   4>;
def TMM5:  X86Reg<"tmm5",   5>;
def TMM6:  X86Reg<"tmm6",   6>;
def TMM7:  X86Reg<"tmm7",   7>;
}

// Floating point stack registers. These don't map one-to-one to the FP
// pseudo registers, but we still mark them as aliasing FP registers. That
// way both kinds can be live without exceeding the stack depth. ST registers
// are only live around inline assembly.
def ST0 : X86Reg<"st", 0>, DwarfRegNum<[33, 12, 11]>;
def ST1 : X86Reg<"st(1)", 1>, DwarfRegNum<[34, 13, 12]>;
def ST2 : X86Reg<"st(2)", 2>, DwarfRegNum<[35, 14, 13]>;
def ST3 : X86Reg<"st(3)", 3>, DwarfRegNum<[36, 15, 14]>;
def ST4 : X86Reg<"st(4)", 4>, DwarfRegNum<[37, 16, 15]>;
def ST5 : X86Reg<"st(5)", 5>, DwarfRegNum<[38, 17, 16]>;
def ST6 : X86Reg<"st(6)", 6>, DwarfRegNum<[39, 18, 17]>;
def ST7 : X86Reg<"st(7)", 7>, DwarfRegNum<[40, 19, 18]>;

// Floating-point status word
def FPSW : X86Reg<"fpsr", 0>;

// Floating-point control word
def FPCW : X86Reg<"fpcr", 0>;

// SIMD Floating-point control register.
// Note: We only model the "Uses" of the control bits: current rounding modes,
// DAZ, FTZ and exception masks. We don't model the "Defs" of flag bits.
def MXCSR : X86Reg<"mxcsr", 0>;

// Status flags register.
//
// Note that some flags that are commonly thought of as part of the status
// flags register are modeled separately. Typically this is due to instructions
// reading and updating those flags independently of all the others. We don't
// want to create false dependencies between these instructions and so we use
// a separate register to model them.
def EFLAGS : X86Reg<"flags", 0>, DwarfRegNum<[49, 9, 9]>;
def _EFLAGS : X86Reg<"eflags", 0>, DwarfRegAlias<EFLAGS>;
def RFLAGS : X86Reg<"rflags", 0>, DwarfRegNum<[49, -2, -2]>;

// The direction flag.
def DF : X86Reg<"dirflag", 0>;


// Segment registers
def CS : X86Reg<"cs", 1>, DwarfRegNum<[51, -2, 41]>;
def DS : X86Reg<"ds", 3>, DwarfRegNum<[53, -2, 43]>;
def SS : X86Reg<"ss", 2>, DwarfRegNum<[52, -2, 42]>;
def ES : X86Reg<"es", 0>, DwarfRegNum<[50, -2, 40]>;
def FS : X86Reg<"fs", 4>, DwarfRegNum<[54, -2, 44]>;
def GS : X86Reg<"gs", 5>, DwarfRegNum<[55, -2, 45]>;

def FS_BASE : X86Reg<"fs.base", 0>, DwarfRegNum<[58, -2, -2]>;
def GS_BASE : X86Reg<"gs.base", 0>, DwarfRegNum<[59, -2, -2]>;

// Debug registers
def DR0  : X86Reg<"dr0",   0>;
def DR1  : X86Reg<"dr1",   1>;
def DR2  : X86Reg<"dr2",   2>;
def DR3  : X86Reg<"dr3",   3>;
def DR4  : X86Reg<"dr4",   4>;
def DR5  : X86Reg<"dr5",   5>;
def DR6  : X86Reg<"dr6",   6>;
def DR7  : X86Reg<"dr7",   7>;
def DR8  : X86Reg<"dr8",   8>;
def DR9  : X86Reg<"dr9",   9>;
def DR10 : X86Reg<"dr10", 10>;
def DR11 : X86Reg<"dr11", 11>;
def DR12 : X86Reg<"dr12", 12>;
def DR13 : X86Reg<"dr13", 13>;
def DR14 : X86Reg<"dr14", 14>;
def DR15 : X86Reg<"dr15", 15>;

// Control registers
def CR0  : X86Reg<"cr0",   0>;
def CR1  : X86Reg<"cr1",   1>;
def CR2  : X86Reg<"cr2",   2>;
def CR3  : X86Reg<"cr3",   3>;
def CR4  : X86Reg<"cr4",   4>;
def CR5  : X86Reg<"cr5",   5>;
def CR6  : X86Reg<"cr6",   6>;
def CR7  : X86Reg<"cr7",   7>;
def CR8  : X86Reg<"cr8",   8>;
def CR9  : X86Reg<"cr9",   9>;
def CR10 : X86Reg<"cr10", 10>;
def CR11 : X86Reg<"cr11", 11>;
def CR12 : X86Reg<"cr12", 12>;
def CR13 : X86Reg<"cr13", 13>;
def CR14 : X86Reg<"cr14", 14>;
def CR15 : X86Reg<"cr15", 15>;

// Pseudo index registers
def EIZ : X86Reg<"eiz", 4>;
def RIZ : X86Reg<"riz", 4>;

// CET registers - Shadow Stack Pointer
def SSP : X86Reg<"ssp", 0>;

//===----------------------------------------------------------------------===//
// Register Class Definitions... now that we have all of the pieces, define the
// top-level register classes.  The order specified in the register list is
// implicitly defined to be the register allocation order.
//

// List call-clobbered registers before callee-save registers. RBX, RBP, (and
// R12, R13, R14, and R15 for X86-64) are callee-save registers.
// In 64-mode, there are 12 additional i8 registers, SIL, DIL, BPL, SPL, and
// R8B, ... R15B.
// Allocate R12 and R13 last, as these require an extra byte when
// encoded in x86_64 instructions.
// FIXME: Allow AH, CH, DH, BH to be used as general-purpose registers in
// 64-bit mode. The main complication is that they cannot be encoded in an
// instruction requiring a REX prefix, while SIL, DIL, BPL, R8D, etc.
// require a REX prefix. For example, "addb %ah, %dil" and "movzbl %ah, %r8d"
// cannot be encoded.
def GR8 : RegisterClass<"X86", [i8], 8,
                        (add AL, CL, DL, AH, CH, DH, BL, BH, SIL, DIL, BPL, SPL,
                             R8B, R9B, R10B, R11B, R16B, R17B, R18B, R19B, R20B,
                             R21B, R22B, R23B, R24B, R25B, R26B, R27B, R28B, R29B,
                             R30B, R31B, R14B, R15B, R12B, R13B)> {
  let AltOrders = [(sub GR8, AH, BH, CH, DH)];
  let AltOrderSelect = [{
    return MF.getSubtarget<X86Subtarget>().is64Bit();
  }];
}

let isAllocatable = 0 in
def GRH8 : RegisterClass<"X86", [i8], 8,
                         (add SIH, DIH, BPH, SPH, R8BH, R9BH, R10BH, R11BH,
                              R12BH, R13BH, R14BH, R15BH, R16BH, R17BH, R18BH,
                              R19BH, R20BH, R21BH, R22BH, R23BH, R24BH, R25BH,
                              R26BH, R27BH, R28BH, R29BH, R30BH, R31BH)>;
def GR16 : RegisterClass<"X86", [i16], 16,
                         (add AX, CX, DX, SI, DI, BX, BP, SP, R8W, R9W, R10W,
                              R11W, R16W, R17W, R18W, R19W, R20W, R21W, R22W, R23W,
                              R24W, R25W, R26W, R27W, R28W, R29W, R30W, R31W, R14W,
                              R15W, R12W, R13W)>;

let isAllocatable = 0 in
def GRH16 : RegisterClass<"X86", [i16], 16,
                    (add HAX, HCX, HDX, HSI, HDI, HBX, HBP, HSP, HIP, R8WH,
                         R9WH, R10WH, R11WH, R12WH, R13WH, R14WH, R15WH, R16WH,
                         R17WH, R18WH, R19WH, R20WH, R21WH, R22WH, R23WH, R24WH,
                         R25WH, R26WH, R27WH, R28WH, R29WH, R30WH, R31WH)>;
def GR32 : RegisterClass<"X86", [i32], 32,
                         (add EAX, ECX, EDX, ESI, EDI, R8D, R9D,
                              R10D, R11D, R16D, R17D, R18D, R19D, R20D, R21D, R22D,
                              R23D, R24D, R25D, R26D, R27D, R28D, R29D, R30D, R31D,
                              R14D, R15D, R12D, R13D, EBX, EBP, ESP)>;

// GR64 - 64-bit GPRs. This oddly includes RIP, which isn't accurate, since
// RIP isn't really a register and it can't be used anywhere except in an
// address, but it doesn't cause trouble.
// FIXME: it *does* cause trouble - CheckBaseRegAndIndexReg() has extra
// tests because of the inclusion of RIP in this register class.
def GR64 : RegisterClass<"X86", [i64], 64,
                    (add RAX, RCX, RDX, RSI, RDI, R8, R9, R10, R11, R16, R17,
                         R18, R19, R20, R21, R22, R23, R24, R25, R26, R27, R28, R29,
                         R30, R31, R14, R15, R12, R13, RBX, RBP, RSP, RIP)>;

// GR64PLTSafe - 64-bit GPRs without R10, R11, RSP and RIP. Could be used when
// emitting code for intrinsics, which use implict input registers.
def GR64PLTSafe : RegisterClass<"X86", [i64], 64,
                              (add RAX, RCX, RDX, RSI, RDI, R8, R9,
                                   RBX, R14, R15, R12, R13, RBP)>;

// It includes the GPR that are used as scratch register for Linux64 calling
// convention.
def GR64_ArgRef: RegisterClass<"X86", [i64], 64, (add R10, R11)> {
  let GeneratePressureSet = 0;
}

// It includes the GPR that are used as scratch register for Linux32 calling
// convention.
def GR32_ArgRef: RegisterClass<"X86", [i32], 32, (add ECX, EDX)> {
  let GeneratePressureSet = 0;
}

// Segment registers for use by MOV instructions (and others) that have a
//   segment register as one operand.  Always contain a 16-bit segment
//   descriptor.
def SEGMENT_REG : RegisterClass<"X86", [i16], 16, (add CS, DS, SS, ES, FS, GS)>;

// Debug registers.
def DEBUG_REG : RegisterClass<"X86", [i32], 32, (sequence "DR%u", 0, 15)>;

// Control registers.
def CONTROL_REG : RegisterClass<"X86", [i64], 64, (sequence "CR%u", 0, 15)>;

// GR8_ABCD_L, GR8_ABCD_H, GR16_ABCD, GR32_ABCD, GR64_ABCD - Subclasses of
// GR8, GR16, GR32, and GR64 which contain just the "a" "b", "c", and "d"
// registers. On x86-32, GR16_ABCD and GR32_ABCD are classes for registers
// that support 8-bit subreg operations. On x86-64, GR16_ABCD, GR32_ABCD,
// and GR64_ABCD are classes for registers that support 8-bit h-register
// operations.
def GR8_ABCD_L : RegisterClass<"X86", [i8], 8, (add AL, CL, DL, BL)>;
def GR8_ABCD_H : RegisterClass<"X86", [i8], 8, (add AH, CH, DH, BH)>;
def GR16_ABCD : RegisterClass<"X86", [i16], 16, (add AX, CX, DX, BX)>;
def GR32_ABCD : RegisterClass<"X86", [i32], 32, (add EAX, ECX, EDX, EBX)>;
def GR64_ABCD : RegisterClass<"X86", [i64], 64, (add RAX, RCX, RDX, RBX)>;
def GR32_TC   : RegisterClass<"X86", [i32], 32, (add EAX, ECX, EDX, ESP)>;
def GR64_TC   : RegisterClass<"X86", [i64], 64, (add RAX, RCX, RDX, RSI, RDI,
                                                     R8, R9, R11, RIP, RSP)>;
def GR64_TCW64 : RegisterClass<"X86", [i64], 64, (add RAX, RCX, RDX,
                                                      R8, R9, R10, R11,
                                                      RIP, RSP)>;

// GR8_NOREX - GR8 registers which do not require a REX prefix.
def GR8_NOREX : RegisterClass<"X86", [i8], 8,
                              (add AL, CL, DL, AH, CH, DH, BL, BH)> {
  let AltOrders = [(sub GR8_NOREX, AH, BH, CH, DH)];
  let AltOrderSelect = [{
    return MF.getSubtarget<X86Subtarget>().is64Bit();
  }];
}
// GR16_NOREX - GR16 registers which do not require a REX prefix.
def GR16_NOREX : RegisterClass<"X86", [i16], 16,
                               (add AX, CX, DX, SI, DI, BX, BP, SP)>;
// GR32_NOREX - GR32 registers which do not require a REX prefix.
def GR32_NOREX : RegisterClass<"X86", [i32], 32,
                               (add EAX, ECX, EDX, ESI, EDI, EBX, EBP, ESP)>;
// GR64_NOREX - GR64 registers which do not require a REX prefix.
def GR64_NOREX : RegisterClass<"X86", [i64], 64,
                            (add RAX, RCX, RDX, RSI, RDI, RBX, RBP, RSP, RIP)>;
// GeneratePressureSet = 0 here is a temporary workaround for lots of
// LIT fail. Whether enabling in the future still needs discussion.
let GeneratePressureSet = 0 in {
// GR8_NOREX2 - GR8 registers which do not require a REX2 prefix.
def GR8_NOREX2 : RegisterClass<"X86", [i8], 8,
                               (sub GR8,  (sequence "R%uB", 16, 31))> {
  let AltOrders = [(sub GR8_NOREX2, AH, BH, CH, DH)];
  let AltOrderSelect = [{
    return MF.getSubtarget<X86Subtarget>().is64Bit();
  }];
}
// GR16_NOREX2 - GR16 registers which do not require a REX2 prefix.
def GR16_NOREX2 : RegisterClass<"X86", [i16], 16,
                               (sub GR16,  (sequence "R%uW", 16, 31))>;
// GR32_NOREX2 - GR32 registers which do not require a REX2 prefix.
def GR32_NOREX2 : RegisterClass<"X86", [i32], 32,
                               (sub GR32,  (sequence "R%uD", 16, 31))>;
// GR64_NOREX2 - GR64 registers which do not require a REX2 prefix.
def GR64_NOREX2 : RegisterClass<"X86", [i64], 64,
                               (sub GR64,  (sequence "R%u", 16, 31))>;
}

// GR32_NOSP - GR32 registers except ESP.
def GR32_NOSP : RegisterClass<"X86", [i32], 32, (sub GR32, ESP)>;

// GR64_NOSP - GR64 registers except RSP (and RIP).
def GR64_NOSP : RegisterClass<"X86", [i64], 64, (sub GR64, RSP, RIP)>;

// GR32_NOREX_NOSP - GR32 registers which do not require a REX prefix except
// ESP.
def GR32_NOREX_NOSP : RegisterClass<"X86", [i32], 32,
                                    (and GR32_NOREX, GR32_NOSP)>;

// GR64_NOREX_NOSP - GR64_NOREX registers except RSP.
def GR64_NOREX_NOSP : RegisterClass<"X86", [i64], 64,
                                    (and GR64_NOREX, GR64_NOSP)>;
let GeneratePressureSet = 0 in {
// GR32_NOREX2_NOSP - GR32_NOREX2 registers except ESP.
def GR32_NOREX2_NOSP : RegisterClass<"X86", [i32], 32,
                                    (sub GR32_NOREX2, ESP)>;

// GR64_NOREX2_NOSP - GR64_NOREX2 registers except RSP, RIP.
def GR64_NOREX2_NOSP : RegisterClass<"X86", [i64], 64,
                                    (sub GR64_NOREX2, RSP, RIP)>;
}

// Register classes used for ABIs that use 32-bit address accesses,
// while using the whole x86_64 ISA.

// In such cases, it is fine to use RIP as we are sure the 32 high
// bits are not set. We do not need variants for NOSP as RIP is not
// allowed there.
// RIP is not spilled anywhere for now, so stick to 32-bit alignment
// to save on memory space.
// FIXME: We could allow all 64bit registers, but we would need
// something to check that the 32 high bits are not set,
// which we do not have right now.
def LOW32_ADDR_ACCESS : RegisterClass<"X86", [i32], 32, (add GR32, RIP)>;

// When RBP is used as a base pointer in a 32-bit addresses environment,
// this is also safe to use the full register to access addresses.
// Since RBP will never be spilled, stick to a 32 alignment to save
// on memory consumption.
def LOW32_ADDR_ACCESS_RBP : RegisterClass<"X86", [i32], 32,
                                          (add LOW32_ADDR_ACCESS, RBP)>;

// A class to support the 'A' assembler constraint: [ER]AX then [ER]DX.
def GR32_AD : RegisterClass<"X86", [i32], 32, (add EAX, EDX)>;
def GR64_AD : RegisterClass<"X86", [i64], 64, (add RAX, RDX)>;

// Classes to support the 64-bit assembler constraint tied to a fixed
// register in 32-bit mode. The second register is always the next in
// the list. Wrap around causes an error.
def GR32_DC : RegisterClass<"X86", [i32], 32, (add EDX, ECX)>;
def GR32_CB : RegisterClass<"X86", [i32], 32, (add ECX, EBX)>;
def GR32_BSI : RegisterClass<"X86", [i32], 32, (add EBX, ESI)>;
def GR32_SIDI : RegisterClass<"X86", [i32], 32, (add ESI, EDI)>;
def GR32_DIBP : RegisterClass<"X86", [i32], 32, (add EDI, EBP)>;
def GR32_BPSP : RegisterClass<"X86", [i32], 32, (add EBP, ESP)>;

// Scalar SSE2 floating point registers.
def FR32 : RegisterClass<"X86", [f32], 32, (sequence "XMM%u", 0, 15)>;

def FR64 : RegisterClass<"X86", [f64], 64, (add FR32)>;

def FR16 : RegisterClass<"X86", [f16], 16, (add FR32)> {let Size = 32;}


// FIXME: This sets up the floating point register files as though they are f64
// values, though they really are f80 values.  This will cause us to spill
// values as 64-bit quantities instead of 80-bit quantities, which is much much
// faster on common hardware.  In reality, this should be controlled by a
// command line option or something.


def RFP32 : RegisterClass<"X86",[f32], 32, (sequence "FP%u", 0, 6)>;
def RFP64 : RegisterClass<"X86",[f64], 32, (add RFP32)>;
def RFP80 : RegisterClass<"X86",[f80], 32, (add RFP32)>;

// st(7) may be is not allocatable.
def RFP80_7 : RegisterClass<"X86",[f80], 32, (add FP7)> {
  let isAllocatable = 0;
}

// Floating point stack registers (these are not allocatable by the
// register allocator - the floating point stackifier is responsible
// for transforming FPn allocations to STn registers)
def RST : RegisterClass<"X86", [f80, f64, f32], 32, (sequence "ST%u", 0, 7)> {
  let isAllocatable = 0;
}

// Helper to allow %st to print as %st(0) when its encoded in the instruction.
def RSTi : RegisterOperand<RST, "printSTiRegOperand">;

// Generic vector registers: VR64 and VR128.
// Ensure that float types are declared first - only float is legal on SSE1.
def VR64: RegisterClass<"X86", [x86mmx], 64, (sequence "MM%u", 0, 7)>;
def VR128 : RegisterClass<"X86", [v4f32, v2f64, v8f16, v8bf16, v16i8, v8i16, v4i32, v2i64, f128],
                          128, (add FR32)>;
def VR256 : RegisterClass<"X86", [v8f32, v4f64, v16f16, v16bf16, v32i8, v16i16, v8i32, v4i64],
                          256, (sequence "YMM%u", 0, 15)>;

// Status flags registers.
def CCR : RegisterClass<"X86", [i32], 32, (add EFLAGS)> {
  let CopyCost = -1;  // Don't allow copying of status registers.
  let isAllocatable = 0;
}
def FPCCR : RegisterClass<"X86", [i16], 16, (add FPSW)> {
  let CopyCost = -1;  // Don't allow copying of status registers.
  let isAllocatable = 0;
}
def DFCCR : RegisterClass<"X86", [i32], 32, (add DF)> {
  let CopyCost = -1;  // Don't allow copying of status registers.
  let isAllocatable = 0;
}

// AVX-512 vector/mask registers.
def VR512 : RegisterClass<"X86", [v16f32, v8f64, v32f16, v32bf16, v64i8, v32i16, v16i32, v8i64],
                          512, (sequence "ZMM%u", 0, 31)>;

// Represents the lower 16 registers that have VEX/legacy encodable subregs.
def VR512_0_15 : RegisterClass<"X86", [v16f32, v8f64, v64i8, v32i16, v16i32, v8i64],
                               512, (sequence "ZMM%u", 0, 15)>;

// Scalar AVX-512 floating point registers.
def FR32X : RegisterClass<"X86", [f32], 32, (sequence "XMM%u", 0, 31)>;

def FR64X : RegisterClass<"X86", [f64], 64, (add FR32X)>;

def FR16X : RegisterClass<"X86", [f16], 16, (add FR32X)> {let Size = 32;}

// Extended VR128 and VR256 for AVX-512 instructions
def VR128X : RegisterClass<"X86", [v4f32, v2f64, v8f16, v8bf16, v16i8, v8i16, v4i32, v2i64, f128],
                           128, (add FR32X)>;
def VR256X : RegisterClass<"X86", [v8f32, v4f64, v16f16, v16bf16, v32i8, v16i16, v8i32, v4i64],
                           256, (sequence "YMM%u", 0, 31)>;

// Mask registers
def VK1     : RegisterClass<"X86", [v1i1],  16,  (sequence "K%u", 0, 7)> {let Size = 16;}
def VK2     : RegisterClass<"X86", [v2i1],  16,  (add VK1)> {let Size = 16;}
def VK4     : RegisterClass<"X86", [v4i1],  16,  (add VK2)> {let Size = 16;}
def VK8     : RegisterClass<"X86", [v8i1],  16,  (add VK4)> {let Size = 16;}
def VK16    : RegisterClass<"X86", [v16i1], 16, (add VK8)> {let Size = 16;}
def VK32    : RegisterClass<"X86", [v32i1], 32, (add VK16)> {let Size = 32;}
def VK64    : RegisterClass<"X86", [v64i1], 64, (add VK32)> {let Size = 64;}

def VK1PAIR   : RegisterClass<"X86", [untyped], 16, (add KPAIRS)> {let Size = 32;}
def VK2PAIR   : RegisterClass<"X86", [untyped], 16, (add KPAIRS)> {let Size = 32;}
def VK4PAIR   : RegisterClass<"X86", [untyped], 16, (add KPAIRS)> {let Size = 32;}
def VK8PAIR   : RegisterClass<"X86", [untyped], 16, (add KPAIRS)> {let Size = 32;}
def VK16PAIR  : RegisterClass<"X86", [untyped], 16, (add KPAIRS)> {let Size = 32;}

def VK1WM   : RegisterClass<"X86", [v1i1],  16,  (sub VK1, K0)> {let Size = 16;}
def VK2WM   : RegisterClass<"X86", [v2i1],  16,  (sub VK2, K0)> {let Size = 16;}
def VK4WM   : RegisterClass<"X86", [v4i1],  16,  (sub VK4, K0)> {let Size = 16;}
def VK8WM   : RegisterClass<"X86", [v8i1],  16,  (sub VK8, K0)> {let Size = 16;}
def VK16WM  : RegisterClass<"X86", [v16i1], 16, (add VK8WM)>   {let Size = 16;}
def VK32WM  : RegisterClass<"X86", [v32i1], 32, (add VK16WM)> {let Size = 32;}
def VK64WM  : RegisterClass<"X86", [v64i1], 64, (add VK32WM)> {let Size = 64;}

// Tiles
let CopyCost = -1 in // Don't allow copying of tile registers
def TILE : RegisterClass<"X86", [x86amx], 8192,
                         (sequence "TMM%u", 0, 7)> {let Size = 8192;}

//===----------------------------------------------------------------------===//
// Register categories.
//

// The TILE and VK*PAIR registers may not be "fixed", but we don't want them
// anyway.
def FixedRegisters : RegisterCategory<[DEBUG_REG, CONTROL_REG, CCR, FPCCR,
                                       DFCCR, TILE, VK1PAIR, VK2PAIR, VK4PAIR,
                                       VK8PAIR, VK16PAIR]>;
def GeneralPurposeRegisters : RegisterCategory<[GR64, GR32, GR16, GR8]>;
