//===-- X86DisassemblerDecoderInternal.h - Disassembler decoder -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the X86 Disassembler.
// It contains the public interface of the instruction decoder.
// Documentation for the disassembler can be found in X86Disassembler.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_DISASSEMBLER_X86DISASSEMBLERDECODER_H
#define LLVM_LIB_TARGET_X86_DISASSEMBLER_X86DISASSEMBLERDECODER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/X86DisassemblerDecoderCommon.h"

namespace llvm {
namespace X86Disassembler {
// Helper macros
#define bitFromOffset0(val) ((val) & 0x1)
#define bitFromOffset1(val) (((val) >> 1) & 0x1)
#define bitFromOffset2(val) (((val) >> 2) & 0x1)
#define bitFromOffset3(val) (((val) >> 3) & 0x1)
#define bitFromOffset4(val) (((val) >> 4) & 0x1)
#define bitFromOffset5(val) (((val) >> 5) & 0x1)
#define bitFromOffset6(val) (((val) >> 6) & 0x1)
#define bitFromOffset7(val) (((val) >> 7) & 0x1)
#define twoBitsFromOffset0(val) ((val) & 0x3)
#define twoBitsFromOffset6(val) (((val) >> 6) & 0x3)
#define threeBitsFromOffset0(val) ((val) & 0x7)
#define threeBitsFromOffset3(val) (((val) >> 3) & 0x7)
#define fourBitsFromOffset0(val) ((val) & 0xf)
#define fourBitsFromOffset3(val) (((val) >> 3) & 0xf)
#define fiveBitsFromOffset0(val) ((val) & 0x1f)
#define invertedBitFromOffset2(val) (((~(val)) >> 2) & 0x1)
#define invertedBitFromOffset3(val) (((~(val)) >> 3) & 0x1)
#define invertedBitFromOffset4(val) (((~(val)) >> 4) & 0x1)
#define invertedBitFromOffset5(val) (((~(val)) >> 5) & 0x1)
#define invertedBitFromOffset6(val) (((~(val)) >> 6) & 0x1)
#define invertedBitFromOffset7(val) (((~(val)) >> 7) & 0x1)
#define invertedFourBitsFromOffset3(val) (((~(val)) >> 3) & 0xf)
// MOD/RM
#define modFromModRM(modRM) twoBitsFromOffset6(modRM)
#define regFromModRM(modRM) threeBitsFromOffset3(modRM)
#define rmFromModRM(modRM) threeBitsFromOffset0(modRM)
// SIB
#define scaleFromSIB(sib) twoBitsFromOffset6(sib)
#define indexFromSIB(sib) threeBitsFromOffset3(sib)
#define baseFromSIB(sib) threeBitsFromOffset0(sib)
// REX
#define wFromREX(rex) bitFromOffset3(rex)
#define rFromREX(rex) bitFromOffset2(rex)
#define xFromREX(rex) bitFromOffset1(rex)
#define bFromREX(rex) bitFromOffset0(rex)
// REX2
#define mFromREX2(rex2) bitFromOffset7(rex2)
#define r2FromREX2(rex2) bitFromOffset6(rex2)
#define x2FromREX2(rex2) bitFromOffset5(rex2)
#define b2FromREX2(rex2) bitFromOffset4(rex2)
#define wFromREX2(rex2) bitFromOffset3(rex2)
#define rFromREX2(rex2) bitFromOffset2(rex2)
#define xFromREX2(rex2) bitFromOffset1(rex2)
#define bFromREX2(rex2) bitFromOffset0(rex2)
// XOP
#define rFromXOP2of3(xop) invertedBitFromOffset7(xop)
#define xFromXOP2of3(xop) invertedBitFromOffset6(xop)
#define bFromXOP2of3(xop) invertedBitFromOffset5(xop)
#define mmmmmFromXOP2of3(xop) fiveBitsFromOffset0(xop)
#define wFromXOP3of3(xop) bitFromOffset7(xop)
#define vvvvFromXOP3of3(xop) invertedFourBitsFromOffset3(xop)
#define lFromXOP3of3(xop) bitFromOffset2(xop)
#define ppFromXOP3of3(xop) twoBitsFromOffset0(xop)
// VEX2
#define rFromVEX2of2(vex) invertedBitFromOffset7(vex)
#define vvvvFromVEX2of2(vex) invertedFourBitsFromOffset3(vex)
#define lFromVEX2of2(vex) bitFromOffset2(vex)
#define ppFromVEX2of2(vex) twoBitsFromOffset0(vex)
// VEX3
#define rFromVEX2of3(vex) invertedBitFromOffset7(vex)
#define xFromVEX2of3(vex) invertedBitFromOffset6(vex)
#define bFromVEX2of3(vex) invertedBitFromOffset5(vex)
#define mmmmmFromVEX2of3(vex) fiveBitsFromOffset0(vex)
#define wFromVEX3of3(vex) bitFromOffset7(vex)
#define vvvvFromVEX3of3(vex) invertedFourBitsFromOffset3(vex)
#define lFromVEX3of3(vex) bitFromOffset2(vex)
#define ppFromVEX3of3(vex) twoBitsFromOffset0(vex)
// EVEX
#define rFromEVEX2of4(evex) invertedBitFromOffset7(evex)
#define xFromEVEX2of4(evex) invertedBitFromOffset6(evex)
#define bFromEVEX2of4(evex) invertedBitFromOffset5(evex)
#define r2FromEVEX2of4(evex) invertedBitFromOffset4(evex)
#define b2FromEVEX2of4(evex) bitFromOffset3(evex)
#define mmmFromEVEX2of4(evex) threeBitsFromOffset0(evex)
#define wFromEVEX3of4(evex) bitFromOffset7(evex)
#define vvvvFromEVEX3of4(evex) invertedFourBitsFromOffset3(evex)
#define x2FromEVEX3of4(evex) invertedBitFromOffset2(evex)
#define ppFromEVEX3of4(evex) twoBitsFromOffset0(evex)
#define oszcFromEVEX3of4(evex) fourBitsFromOffset3(evex)
#define zFromEVEX4of4(evex) bitFromOffset7(evex)
#define l2FromEVEX4of4(evex) bitFromOffset6(evex)
#define lFromEVEX4of4(evex) bitFromOffset5(evex)
#define bFromEVEX4of4(evex) bitFromOffset4(evex)
#define v2FromEVEX4of4(evex) invertedBitFromOffset3(evex)
#define aaaFromEVEX4of4(evex) threeBitsFromOffset0(evex)
#define nfFromEVEX4of4(evex) bitFromOffset2(evex)
#define scFromEVEX4of4(evex) fourBitsFromOffset0(evex)

// These enums represent Intel registers for use by the decoder.
#define REGS_8BIT                                                              \
  ENTRY(AL)                                                                    \
  ENTRY(CL)                                                                    \
  ENTRY(DL)                                                                    \
  ENTRY(BL)                                                                    \
  ENTRY(AH)                                                                    \
  ENTRY(CH)                                                                    \
  ENTRY(DH)                                                                    \
  ENTRY(BH)                                                                    \
  ENTRY(R8B)                                                                   \
  ENTRY(R9B)                                                                   \
  ENTRY(R10B)                                                                  \
  ENTRY(R11B)                                                                  \
  ENTRY(R12B)                                                                  \
  ENTRY(R13B)                                                                  \
  ENTRY(R14B)                                                                  \
  ENTRY(R15B)                                                                  \
  ENTRY(R16B)                                                                  \
  ENTRY(R17B)                                                                  \
  ENTRY(R18B)                                                                  \
  ENTRY(R19B)                                                                  \
  ENTRY(R20B)                                                                  \
  ENTRY(R21B)                                                                  \
  ENTRY(R22B)                                                                  \
  ENTRY(R23B)                                                                  \
  ENTRY(R24B)                                                                  \
  ENTRY(R25B)                                                                  \
  ENTRY(R26B)                                                                  \
  ENTRY(R27B)                                                                  \
  ENTRY(R28B)                                                                  \
  ENTRY(R29B)                                                                  \
  ENTRY(R30B)                                                                  \
  ENTRY(R31B)                                                                  \
  ENTRY(SPL)                                                                   \
  ENTRY(BPL)                                                                   \
  ENTRY(SIL)                                                                   \
  ENTRY(DIL)

#define EA_BASES_16BIT                                                         \
  ENTRY(BX_SI)                                                                 \
  ENTRY(BX_DI)                                                                 \
  ENTRY(BP_SI)                                                                 \
  ENTRY(BP_DI)                                                                 \
  ENTRY(SI)                                                                    \
  ENTRY(DI)                                                                    \
  ENTRY(BP)                                                                    \
  ENTRY(BX)                                                                    \
  ENTRY(R8W)                                                                   \
  ENTRY(R9W)                                                                   \
  ENTRY(R10W)                                                                  \
  ENTRY(R11W)                                                                  \
  ENTRY(R12W)                                                                  \
  ENTRY(R13W)                                                                  \
  ENTRY(R14W)                                                                  \
  ENTRY(R15W)                                                                  \
  ENTRY(R16W)                                                                  \
  ENTRY(R17W)                                                                  \
  ENTRY(R18W)                                                                  \
  ENTRY(R19W)                                                                  \
  ENTRY(R20W)                                                                  \
  ENTRY(R21W)                                                                  \
  ENTRY(R22W)                                                                  \
  ENTRY(R23W)                                                                  \
  ENTRY(R24W)                                                                  \
  ENTRY(R25W)                                                                  \
  ENTRY(R26W)                                                                  \
  ENTRY(R27W)                                                                  \
  ENTRY(R28W)                                                                  \
  ENTRY(R29W)                                                                  \
  ENTRY(R30W)                                                                  \
  ENTRY(R31W)

#define REGS_16BIT                                                             \
  ENTRY(AX)                                                                    \
  ENTRY(CX)                                                                    \
  ENTRY(DX)                                                                    \
  ENTRY(BX)                                                                    \
  ENTRY(SP)                                                                    \
  ENTRY(BP)                                                                    \
  ENTRY(SI)                                                                    \
  ENTRY(DI)                                                                    \
  ENTRY(R8W)                                                                   \
  ENTRY(R9W)                                                                   \
  ENTRY(R10W)                                                                  \
  ENTRY(R11W)                                                                  \
  ENTRY(R12W)                                                                  \
  ENTRY(R13W)                                                                  \
  ENTRY(R14W)                                                                  \
  ENTRY(R15W)                                                                  \
  ENTRY(R16W)                                                                  \
  ENTRY(R17W)                                                                  \
  ENTRY(R18W)                                                                  \
  ENTRY(R19W)                                                                  \
  ENTRY(R20W)                                                                  \
  ENTRY(R21W)                                                                  \
  ENTRY(R22W)                                                                  \
  ENTRY(R23W)                                                                  \
  ENTRY(R24W)                                                                  \
  ENTRY(R25W)                                                                  \
  ENTRY(R26W)                                                                  \
  ENTRY(R27W)                                                                  \
  ENTRY(R28W)                                                                  \
  ENTRY(R29W)                                                                  \
  ENTRY(R30W)                                                                  \
  ENTRY(R31W)

#define EA_BASES_32BIT                                                         \
  ENTRY(EAX)                                                                   \
  ENTRY(ECX)                                                                   \
  ENTRY(EDX)                                                                   \
  ENTRY(EBX)                                                                   \
  ENTRY(sib)                                                                   \
  ENTRY(EBP)                                                                   \
  ENTRY(ESI)                                                                   \
  ENTRY(EDI)                                                                   \
  ENTRY(R8D)                                                                   \
  ENTRY(R9D)                                                                   \
  ENTRY(R10D)                                                                  \
  ENTRY(R11D)                                                                  \
  ENTRY(R12D)                                                                  \
  ENTRY(R13D)                                                                  \
  ENTRY(R14D)                                                                  \
  ENTRY(R15D)                                                                  \
  ENTRY(R16D)                                                                  \
  ENTRY(R17D)                                                                  \
  ENTRY(R18D)                                                                  \
  ENTRY(R19D)                                                                  \
  ENTRY(R20D)                                                                  \
  ENTRY(R21D)                                                                  \
  ENTRY(R22D)                                                                  \
  ENTRY(R23D)                                                                  \
  ENTRY(R24D)                                                                  \
  ENTRY(R25D)                                                                  \
  ENTRY(R26D)                                                                  \
  ENTRY(R27D)                                                                  \
  ENTRY(R28D)                                                                  \
  ENTRY(R29D)                                                                  \
  ENTRY(R30D)                                                                  \
  ENTRY(R31D)

#define REGS_32BIT                                                             \
  ENTRY(EAX)                                                                   \
  ENTRY(ECX)                                                                   \
  ENTRY(EDX)                                                                   \
  ENTRY(EBX)                                                                   \
  ENTRY(ESP)                                                                   \
  ENTRY(EBP)                                                                   \
  ENTRY(ESI)                                                                   \
  ENTRY(EDI)                                                                   \
  ENTRY(R8D)                                                                   \
  ENTRY(R9D)                                                                   \
  ENTRY(R10D)                                                                  \
  ENTRY(R11D)                                                                  \
  ENTRY(R12D)                                                                  \
  ENTRY(R13D)                                                                  \
  ENTRY(R14D)                                                                  \
  ENTRY(R15D)                                                                  \
  ENTRY(R16D)                                                                  \
  ENTRY(R17D)                                                                  \
  ENTRY(R18D)                                                                  \
  ENTRY(R19D)                                                                  \
  ENTRY(R20D)                                                                  \
  ENTRY(R21D)                                                                  \
  ENTRY(R22D)                                                                  \
  ENTRY(R23D)                                                                  \
  ENTRY(R24D)                                                                  \
  ENTRY(R25D)                                                                  \
  ENTRY(R26D)                                                                  \
  ENTRY(R27D)                                                                  \
  ENTRY(R28D)                                                                  \
  ENTRY(R29D)                                                                  \
  ENTRY(R30D)                                                                  \
  ENTRY(R31D)

#define EA_BASES_64BIT                                                         \
  ENTRY(RAX)                                                                   \
  ENTRY(RCX)                                                                   \
  ENTRY(RDX)                                                                   \
  ENTRY(RBX)                                                                   \
  ENTRY(sib64)                                                                 \
  ENTRY(RBP)                                                                   \
  ENTRY(RSI)                                                                   \
  ENTRY(RDI)                                                                   \
  ENTRY(R8)                                                                    \
  ENTRY(R9)                                                                    \
  ENTRY(R10)                                                                   \
  ENTRY(R11)                                                                   \
  ENTRY(R12)                                                                   \
  ENTRY(R13)                                                                   \
  ENTRY(R14)                                                                   \
  ENTRY(R15)                                                                   \
  ENTRY(R16)                                                                   \
  ENTRY(R17)                                                                   \
  ENTRY(R18)                                                                   \
  ENTRY(R19)                                                                   \
  ENTRY(R20)                                                                   \
  ENTRY(R21)                                                                   \
  ENTRY(R22)                                                                   \
  ENTRY(R23)                                                                   \
  ENTRY(R24)                                                                   \
  ENTRY(R25)                                                                   \
  ENTRY(R26)                                                                   \
  ENTRY(R27)                                                                   \
  ENTRY(R28)                                                                   \
  ENTRY(R29)                                                                   \
  ENTRY(R30)                                                                   \
  ENTRY(R31)

#define REGS_64BIT                                                             \
  ENTRY(RAX)                                                                   \
  ENTRY(RCX)                                                                   \
  ENTRY(RDX)                                                                   \
  ENTRY(RBX)                                                                   \
  ENTRY(RSP)                                                                   \
  ENTRY(RBP)                                                                   \
  ENTRY(RSI)                                                                   \
  ENTRY(RDI)                                                                   \
  ENTRY(R8)                                                                    \
  ENTRY(R9)                                                                    \
  ENTRY(R10)                                                                   \
  ENTRY(R11)                                                                   \
  ENTRY(R12)                                                                   \
  ENTRY(R13)                                                                   \
  ENTRY(R14)                                                                   \
  ENTRY(R15)                                                                   \
  ENTRY(R16)                                                                   \
  ENTRY(R17)                                                                   \
  ENTRY(R18)                                                                   \
  ENTRY(R19)                                                                   \
  ENTRY(R20)                                                                   \
  ENTRY(R21)                                                                   \
  ENTRY(R22)                                                                   \
  ENTRY(R23)                                                                   \
  ENTRY(R24)                                                                   \
  ENTRY(R25)                                                                   \
  ENTRY(R26)                                                                   \
  ENTRY(R27)                                                                   \
  ENTRY(R28)                                                                   \
  ENTRY(R29)                                                                   \
  ENTRY(R30)                                                                   \
  ENTRY(R31)

#define REGS_MMX                                                               \
  ENTRY(MM0)                                                                   \
  ENTRY(MM1)                                                                   \
  ENTRY(MM2)                                                                   \
  ENTRY(MM3)                                                                   \
  ENTRY(MM4)                                                                   \
  ENTRY(MM5)                                                                   \
  ENTRY(MM6)                                                                   \
  ENTRY(MM7)

#define REGS_XMM                                                               \
  ENTRY(XMM0)                                                                  \
  ENTRY(XMM1)                                                                  \
  ENTRY(XMM2)                                                                  \
  ENTRY(XMM3)                                                                  \
  ENTRY(XMM4)                                                                  \
  ENTRY(XMM5)                                                                  \
  ENTRY(XMM6)                                                                  \
  ENTRY(XMM7)                                                                  \
  ENTRY(XMM8)                                                                  \
  ENTRY(XMM9)                                                                  \
  ENTRY(XMM10)                                                                 \
  ENTRY(XMM11)                                                                 \
  ENTRY(XMM12)                                                                 \
  ENTRY(XMM13)                                                                 \
  ENTRY(XMM14)                                                                 \
  ENTRY(XMM15)                                                                 \
  ENTRY(XMM16)                                                                 \
  ENTRY(XMM17)                                                                 \
  ENTRY(XMM18)                                                                 \
  ENTRY(XMM19)                                                                 \
  ENTRY(XMM20)                                                                 \
  ENTRY(XMM21)                                                                 \
  ENTRY(XMM22)                                                                 \
  ENTRY(XMM23)                                                                 \
  ENTRY(XMM24)                                                                 \
  ENTRY(XMM25)                                                                 \
  ENTRY(XMM26)                                                                 \
  ENTRY(XMM27)                                                                 \
  ENTRY(XMM28)                                                                 \
  ENTRY(XMM29)                                                                 \
  ENTRY(XMM30)                                                                 \
  ENTRY(XMM31)

#define REGS_YMM                                                               \
  ENTRY(YMM0)                                                                  \
  ENTRY(YMM1)                                                                  \
  ENTRY(YMM2)                                                                  \
  ENTRY(YMM3)                                                                  \
  ENTRY(YMM4)                                                                  \
  ENTRY(YMM5)                                                                  \
  ENTRY(YMM6)                                                                  \
  ENTRY(YMM7)                                                                  \
  ENTRY(YMM8)                                                                  \
  ENTRY(YMM9)                                                                  \
  ENTRY(YMM10)                                                                 \
  ENTRY(YMM11)                                                                 \
  ENTRY(YMM12)                                                                 \
  ENTRY(YMM13)                                                                 \
  ENTRY(YMM14)                                                                 \
  ENTRY(YMM15)                                                                 \
  ENTRY(YMM16)                                                                 \
  ENTRY(YMM17)                                                                 \
  ENTRY(YMM18)                                                                 \
  ENTRY(YMM19)                                                                 \
  ENTRY(YMM20)                                                                 \
  ENTRY(YMM21)                                                                 \
  ENTRY(YMM22)                                                                 \
  ENTRY(YMM23)                                                                 \
  ENTRY(YMM24)                                                                 \
  ENTRY(YMM25)                                                                 \
  ENTRY(YMM26)                                                                 \
  ENTRY(YMM27)                                                                 \
  ENTRY(YMM28)                                                                 \
  ENTRY(YMM29)                                                                 \
  ENTRY(YMM30)                                                                 \
  ENTRY(YMM31)

#define REGS_ZMM                                                               \
  ENTRY(ZMM0)                                                                  \
  ENTRY(ZMM1)                                                                  \
  ENTRY(ZMM2)                                                                  \
  ENTRY(ZMM3)                                                                  \
  ENTRY(ZMM4)                                                                  \
  ENTRY(ZMM5)                                                                  \
  ENTRY(ZMM6)                                                                  \
  ENTRY(ZMM7)                                                                  \
  ENTRY(ZMM8)                                                                  \
  ENTRY(ZMM9)                                                                  \
  ENTRY(ZMM10)                                                                 \
  ENTRY(ZMM11)                                                                 \
  ENTRY(ZMM12)                                                                 \
  ENTRY(ZMM13)                                                                 \
  ENTRY(ZMM14)                                                                 \
  ENTRY(ZMM15)                                                                 \
  ENTRY(ZMM16)                                                                 \
  ENTRY(ZMM17)                                                                 \
  ENTRY(ZMM18)                                                                 \
  ENTRY(ZMM19)                                                                 \
  ENTRY(ZMM20)                                                                 \
  ENTRY(ZMM21)                                                                 \
  ENTRY(ZMM22)                                                                 \
  ENTRY(ZMM23)                                                                 \
  ENTRY(ZMM24)                                                                 \
  ENTRY(ZMM25)                                                                 \
  ENTRY(ZMM26)                                                                 \
  ENTRY(ZMM27)                                                                 \
  ENTRY(ZMM28)                                                                 \
  ENTRY(ZMM29)                                                                 \
  ENTRY(ZMM30)                                                                 \
  ENTRY(ZMM31)

#define REGS_MASKS                                                             \
  ENTRY(K0)                                                                    \
  ENTRY(K1)                                                                    \
  ENTRY(K2)                                                                    \
  ENTRY(K3)                                                                    \
  ENTRY(K4)                                                                    \
  ENTRY(K5)                                                                    \
  ENTRY(K6)                                                                    \
  ENTRY(K7)

#define REGS_MASK_PAIRS                                                        \
  ENTRY(K0_K1)                                                                 \
  ENTRY(K2_K3)                                                                 \
  ENTRY(K4_K5)                                                                 \
  ENTRY(K6_K7)

#define REGS_SEGMENT                                                           \
  ENTRY(ES)                                                                    \
  ENTRY(CS)                                                                    \
  ENTRY(SS)                                                                    \
  ENTRY(DS)                                                                    \
  ENTRY(FS)                                                                    \
  ENTRY(GS)

#define REGS_DEBUG                                                             \
  ENTRY(DR0)                                                                   \
  ENTRY(DR1)                                                                   \
  ENTRY(DR2)                                                                   \
  ENTRY(DR3)                                                                   \
  ENTRY(DR4)                                                                   \
  ENTRY(DR5)                                                                   \
  ENTRY(DR6)                                                                   \
  ENTRY(DR7)                                                                   \
  ENTRY(DR8)                                                                   \
  ENTRY(DR9)                                                                   \
  ENTRY(DR10)                                                                  \
  ENTRY(DR11)                                                                  \
  ENTRY(DR12)                                                                  \
  ENTRY(DR13)                                                                  \
  ENTRY(DR14)                                                                  \
  ENTRY(DR15)

#define REGS_CONTROL                                                           \
  ENTRY(CR0)                                                                   \
  ENTRY(CR1)                                                                   \
  ENTRY(CR2)                                                                   \
  ENTRY(CR3)                                                                   \
  ENTRY(CR4)                                                                   \
  ENTRY(CR5)                                                                   \
  ENTRY(CR6)                                                                   \
  ENTRY(CR7)                                                                   \
  ENTRY(CR8)                                                                   \
  ENTRY(CR9)                                                                   \
  ENTRY(CR10)                                                                  \
  ENTRY(CR11)                                                                  \
  ENTRY(CR12)                                                                  \
  ENTRY(CR13)                                                                  \
  ENTRY(CR14)                                                                  \
  ENTRY(CR15)

#undef REGS_TMM
#define REGS_TMM                                                               \
  ENTRY(TMM0)                                                                  \
  ENTRY(TMM1)                                                                  \
  ENTRY(TMM2)                                                                  \
  ENTRY(TMM3)                                                                  \
  ENTRY(TMM4)                                                                  \
  ENTRY(TMM5)                                                                  \
  ENTRY(TMM6)                                                                  \
  ENTRY(TMM7)

#define ALL_EA_BASES                                                           \
  EA_BASES_16BIT                                                               \
  EA_BASES_32BIT                                                               \
  EA_BASES_64BIT

#define ALL_SIB_BASES                                                          \
  REGS_32BIT                                                                   \
  REGS_64BIT

#define ALL_REGS                                                               \
  REGS_8BIT                                                                    \
  REGS_16BIT                                                                   \
  REGS_32BIT                                                                   \
  REGS_64BIT                                                                   \
  REGS_MMX                                                                     \
  REGS_XMM                                                                     \
  REGS_YMM                                                                     \
  REGS_ZMM                                                                     \
  REGS_MASKS                                                                   \
  REGS_MASK_PAIRS                                                              \
  REGS_SEGMENT                                                                 \
  REGS_DEBUG                                                                   \
  REGS_CONTROL                                                                 \
  REGS_TMM                                                                     \
  ENTRY(RIP)

/// All possible values of the base field for effective-address
/// computations, a.k.a. the Mod and R/M fields of the ModR/M byte.
/// We distinguish between bases (EA_BASE_*) and registers that just happen
/// to be referred to when Mod == 0b11 (EA_REG_*).
enum EABase {
  // clang-format off
  EA_BASE_NONE,
#define ENTRY(x) EA_BASE_##x,
  ALL_EA_BASES
#undef ENTRY
#define ENTRY(x) EA_REG_##x,
  ALL_REGS
#undef ENTRY
  EA_max
  // clang-format on
};

/// All possible values of the SIB index field.
/// borrows entries from ALL_EA_BASES with the special case that
/// sib is synonymous with NONE.
/// Vector SIB: index can be XMM or YMM.
enum SIBIndex {
  // clang-format off
  SIB_INDEX_NONE,
#define ENTRY(x) SIB_INDEX_##x,
  ALL_EA_BASES
  REGS_XMM
  REGS_YMM
  REGS_ZMM
#undef ENTRY
  SIB_INDEX_max
  // clang-format on
};

/// All possible values of the SIB base field.
enum SIBBase {
  // clang-format off
  SIB_BASE_NONE,
#define ENTRY(x) SIB_BASE_##x,
  ALL_SIB_BASES
#undef ENTRY
  SIB_BASE_max
  // clang-format on
};

/// Possible displacement types for effective-address computations.
enum EADisplacement { EA_DISP_NONE, EA_DISP_8, EA_DISP_16, EA_DISP_32 };

/// All possible values of the reg field in the ModR/M byte.
// clang-format off
enum Reg {
#define ENTRY(x) MODRM_REG_##x,
  ALL_REGS
#undef ENTRY
  MODRM_REG_max
};
// clang-format on

/// All possible segment overrides.
enum SegmentOverride {
  SEG_OVERRIDE_NONE,
  SEG_OVERRIDE_CS,
  SEG_OVERRIDE_SS,
  SEG_OVERRIDE_DS,
  SEG_OVERRIDE_ES,
  SEG_OVERRIDE_FS,
  SEG_OVERRIDE_GS,
  SEG_OVERRIDE_max
};

/// Possible values for the VEX.m-mmmm field
enum VEXLeadingOpcodeByte {
  VEX_LOB_0F = 0x1,
  VEX_LOB_0F38 = 0x2,
  VEX_LOB_0F3A = 0x3,
  VEX_LOB_MAP4 = 0x4,
  VEX_LOB_MAP5 = 0x5,
  VEX_LOB_MAP6 = 0x6,
  VEX_LOB_MAP7 = 0x7
};

enum XOPMapSelect {
  XOP_MAP_SELECT_8 = 0x8,
  XOP_MAP_SELECT_9 = 0x9,
  XOP_MAP_SELECT_A = 0xA
};

/// Possible values for the VEX.pp/EVEX.pp field
enum VEXPrefixCode {
  VEX_PREFIX_NONE = 0x0,
  VEX_PREFIX_66 = 0x1,
  VEX_PREFIX_F3 = 0x2,
  VEX_PREFIX_F2 = 0x3
};

enum VectorExtensionType {
  TYPE_NO_VEX_XOP = 0x0,
  TYPE_VEX_2B = 0x1,
  TYPE_VEX_3B = 0x2,
  TYPE_EVEX = 0x3,
  TYPE_XOP = 0x4
};

/// The specification for how to extract and interpret a full instruction and
/// its operands.
struct InstructionSpecifier {
  uint16_t operands;
};

/// The x86 internal instruction, which is produced by the decoder.
struct InternalInstruction {
  // Opaque value passed to the reader
  llvm::ArrayRef<uint8_t> bytes;
  // The address of the next byte to read via the reader
  uint64_t readerCursor;

  // General instruction information

  // The mode to disassemble for (64-bit, protected, real)
  DisassemblerMode mode;
  // The start of the instruction, usable with the reader
  uint64_t startLocation;
  // The length of the instruction, in bytes
  size_t length;

  // Prefix state

  // The possible mandatory prefix
  uint8_t mandatoryPrefix;
  // The value of the vector extension prefix(EVEX/VEX/XOP), if present
  uint8_t vectorExtensionPrefix[4];
  // The type of the vector extension prefix
  VectorExtensionType vectorExtensionType;
  // The value of the REX2 prefix, if present
  uint8_t rex2ExtensionPrefix[2];
  // The value of the REX prefix, if present
  uint8_t rexPrefix;
  // The segment override type
  SegmentOverride segmentOverride;
  // 1 if the prefix byte, 0xf2 or 0xf3 is xacquire or xrelease
  bool xAcquireRelease;

  // Address-size override
  bool hasAdSize;
  // Operand-size override
  bool hasOpSize;
  // Lock prefix
  bool hasLockPrefix;
  // The repeat prefix if any
  uint8_t repeatPrefix;

  // Sizes of various critical pieces of data, in bytes
  uint8_t registerSize;
  uint8_t addressSize;
  uint8_t displacementSize;
  uint8_t immediateSize;

  // Offsets from the start of the instruction to the pieces of data, which is
  // needed to find relocation entries for adding symbolic operands.
  uint8_t displacementOffset;
  uint8_t immediateOffset;

  // opcode state

  // The last byte of the opcode, not counting any ModR/M extension
  uint8_t opcode;

  // decode state

  // The type of opcode, used for indexing into the array of decode tables
  OpcodeType opcodeType;
  // The instruction ID, extracted from the decode table
  uint16_t instructionID;
  // The specifier for the instruction, from the instruction info table
  const InstructionSpecifier *spec;

  // state for additional bytes, consumed during operand decode.  Pattern:
  // consumed___ indicates that the byte was already consumed and does not
  // need to be consumed again.

  // The VEX.vvvv field, which contains a third register operand for some AVX
  // instructions.
  Reg vvvv;

  // The writemask for AVX-512 instructions which is contained in EVEX.aaa
  Reg writemask;

  // The ModR/M byte, which contains most register operands and some portion of
  // all memory operands.
  bool consumedModRM;
  uint8_t modRM;

  // The SIB byte, used for more complex 32- or 64-bit memory operands
  uint8_t sib;

  // The displacement, used for memory operands
  int32_t displacement;

  // Immediates.  There can be three in some cases
  uint8_t numImmediatesConsumed;
  uint8_t numImmediatesTranslated;
  uint64_t immediates[3];

  // A register or immediate operand encoded into the opcode
  Reg opcodeRegister;

  // Portions of the ModR/M byte

  // These fields determine the allowable values for the ModR/M fields, which
  // depend on operand and address widths.
  EABase eaRegBase;
  Reg regBase;

  // The Mod and R/M fields can encode a base for an effective address, or a
  // register.  These are separated into two fields here.
  EABase eaBase;
  EADisplacement eaDisplacement;
  // The reg field always encodes a register
  Reg reg;

  // SIB state
  SIBIndex sibIndexBase;
  SIBIndex sibIndex;
  uint8_t sibScale;
  SIBBase sibBase;

  // Embedded rounding control.
  uint8_t RC;

  ArrayRef<OperandSpecifier> operands;
};

} // namespace X86Disassembler
} // namespace llvm

#endif
