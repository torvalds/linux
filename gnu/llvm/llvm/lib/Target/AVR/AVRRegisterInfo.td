//===-- AVRRegisterInfo.td - AVR Register defs -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the AVR register file
//===----------------------------------------------------------------------===//

// 8-bit General purpose register definition.
class AVRReg<bits<16> num, string name, list<Register> subregs = [],
             list<string> altNames = []> : RegisterWithSubRegs<name, subregs> {
  field bits<16> Num = num;

  let HWEncoding = num;
  let Namespace = "AVR";
  let SubRegs = subregs;
  let AltNames = altNames;
}

// Subregister indices.
let Namespace = "AVR" in {
  def sub_lo : SubRegIndex<8>;
  def sub_hi : SubRegIndex<8, 8>;
}

let Namespace = "AVR" in { def ptr : RegAltNameIndex; }

//===----------------------------------------------------------------------===//
//  8-bit general purpose registers
//===----------------------------------------------------------------------===//

def R0 : AVRReg<0, "r0">, DwarfRegNum<[0]>;
def R1 : AVRReg<1, "r1">, DwarfRegNum<[1]>;
def R2 : AVRReg<2, "r2">, DwarfRegNum<[2]>;
def R3 : AVRReg<3, "r3">, DwarfRegNum<[3]>;
def R4 : AVRReg<4, "r4">, DwarfRegNum<[4]>;
def R5 : AVRReg<5, "r5">, DwarfRegNum<[5]>;
def R6 : AVRReg<6, "r6">, DwarfRegNum<[6]>;
def R7 : AVRReg<7, "r7">, DwarfRegNum<[7]>;
def R8 : AVRReg<8, "r8">, DwarfRegNum<[8]>;
def R9 : AVRReg<9, "r9">, DwarfRegNum<[9]>;
def R10 : AVRReg<10, "r10">, DwarfRegNum<[10]>;
def R11 : AVRReg<11, "r11">, DwarfRegNum<[11]>;
def R12 : AVRReg<12, "r12">, DwarfRegNum<[12]>;
def R13 : AVRReg<13, "r13">, DwarfRegNum<[13]>;
def R14 : AVRReg<14, "r14">, DwarfRegNum<[14]>;
def R15 : AVRReg<15, "r15">, DwarfRegNum<[15]>;
def R16 : AVRReg<16, "r16">, DwarfRegNum<[16]>;
def R17 : AVRReg<17, "r17">, DwarfRegNum<[17]>;
def R18 : AVRReg<18, "r18">, DwarfRegNum<[18]>;
def R19 : AVRReg<19, "r19">, DwarfRegNum<[19]>;
def R20 : AVRReg<20, "r20">, DwarfRegNum<[20]>;
def R21 : AVRReg<21, "r21">, DwarfRegNum<[21]>;
def R22 : AVRReg<22, "r22">, DwarfRegNum<[22]>;
def R23 : AVRReg<23, "r23">, DwarfRegNum<[23]>;
def R24 : AVRReg<24, "r24">, DwarfRegNum<[24]>;
def R25 : AVRReg<25, "r25">, DwarfRegNum<[25]>;
def R26 : AVRReg<26, "r26", [], ["xl"]>, DwarfRegNum<[26]>;
def R27 : AVRReg<27, "r27", [], ["xh"]>, DwarfRegNum<[27]>;
def R28 : AVRReg<28, "r28", [], ["yl"]>, DwarfRegNum<[28]>;
def R29 : AVRReg<29, "r29", [], ["yh"]>, DwarfRegNum<[29]>;
def R30 : AVRReg<30, "r30", [], ["zl"]>, DwarfRegNum<[30]>;
def R31 : AVRReg<31, "r31", [], ["zh"]>, DwarfRegNum<[31]>;
def SPL : AVRReg<32, "SPL">, DwarfRegNum<[32]>;
def SPH : AVRReg<33, "SPH">, DwarfRegNum<[33]>;

let SubRegIndices = [sub_lo, sub_hi], CoveredBySubRegs = 1 in {
  // 16 bit GPR pairs.
  def SP : AVRReg<32, "SP", [SPL, SPH]>, DwarfRegNum<[32]>;

  // The pointer registers (X,Y,Z) are a special case because they
  // are printed as a `high:low` pair when a DREG is expected,
  // but printed using `X`, `Y`, `Z` when a pointer register is expected.
  let RegAltNameIndices = [ptr] in {
    def R31R30 : AVRReg<30, "r31:r30", [R30, R31], ["Z"]>, DwarfRegNum<[30]>;
    def R29R28 : AVRReg<28, "r29:r28", [R28, R29], ["Y"]>, DwarfRegNum<[28]>;
    def R27R26 : AVRReg<26, "r27:r26", [R26, R27], ["X"]>, DwarfRegNum<[26]>;
  }
  def R25R24 : AVRReg<24, "r25:r24", [R24, R25]>, DwarfRegNum<[24]>;
  def R23R22 : AVRReg<22, "r23:r22", [R22, R23]>, DwarfRegNum<[22]>;
  def R21R20 : AVRReg<20, "r21:r20", [R20, R21]>, DwarfRegNum<[20]>;
  def R19R18 : AVRReg<18, "r19:r18", [R18, R19]>, DwarfRegNum<[18]>;
  def R17R16 : AVRReg<16, "r17:r16", [R16, R17]>, DwarfRegNum<[16]>;
  def R15R14 : AVRReg<14, "r15:r14", [R14, R15]>, DwarfRegNum<[14]>;
  def R13R12 : AVRReg<12, "r13:r12", [R12, R13]>, DwarfRegNum<[12]>;
  def R11R10 : AVRReg<10, "r11:r10", [R10, R11]>, DwarfRegNum<[10]>;
  def R9R8 : AVRReg<8, "r9:r8", [R8, R9]>, DwarfRegNum<[8]>;
  def R7R6 : AVRReg<6, "r7:r6", [R6, R7]>, DwarfRegNum<[6]>;
  def R5R4 : AVRReg<4, "r5:r4", [R4, R5]>, DwarfRegNum<[4]>;
  def R3R2 : AVRReg<2, "r3:r2", [R2, R3]>, DwarfRegNum<[2]>;
  def R1R0 : AVRReg<0, "r1:r0", [R0, R1]>, DwarfRegNum<[0]>;

  // Pseudo registers for unaligned i16
  def R26R25 : AVRReg<25, "r26:r25", [R25, R26]>, DwarfRegNum<[25]>;
  def R24R23 : AVRReg<23, "r24:r23", [R23, R24]>, DwarfRegNum<[23]>;
  def R22R21 : AVRReg<21, "r22:r21", [R21, R22]>, DwarfRegNum<[21]>;
  def R20R19 : AVRReg<19, "r20:r19", [R19, R20]>, DwarfRegNum<[19]>;
  def R18R17 : AVRReg<17, "r18:r17", [R17, R18]>, DwarfRegNum<[17]>;
  def R16R15 : AVRReg<15, "r16:r15", [R15, R16]>, DwarfRegNum<[15]>;
  def R14R13 : AVRReg<13, "r14:r13", [R13, R14]>, DwarfRegNum<[13]>;
  def R12R11 : AVRReg<11, "r12:r11", [R11, R12]>, DwarfRegNum<[11]>;
  def R10R9 : AVRReg<9, "r10:r9", [R9, R10]>, DwarfRegNum<[9]>;
}

//===----------------------------------------------------------------------===//
// Register Classes
//===----------------------------------------------------------------------===//

// Main 8-bit register class.
def GPR8 : RegisterClass<"AVR", [i8], 8,
                         (
                             // Return value and argument registers.
                             add R24, R25, R18, R19, R20, R21, R22, R23,
                             // Scratch registers.
                             R30, R31, R26, R27,
                             // Callee saved registers.
                             R28, R29, R17, R16, R15, R14, R13, R12, R11, R10,
                             R9, R8, R7, R6, R5, R4, R3, R2, R0, R1)>;

// Simple lower registers r0..r15
def GPR8lo : RegisterClass<"AVR", [i8], 8,
                           (add R15, R14, R13, R12, R11, R10, R9, R8, R7, R6,
                            R5, R4, R3, R2, R0, R1)>;

// 8-bit register class for instructions which take immediates.
def LD8 : RegisterClass<"AVR", [i8], 8,
                        (
                            // Return value and arguments.
                            add R24, R25, R18, R19, R20, R21, R22, R23,
                            // Scratch registers.
                            R30, R31, R26, R27,
                            // Callee saved registers.
                            R28, R29, R17, R16)>;

// Simple lower registers r16..r23
def LD8lo : RegisterClass<"AVR", [i8], 8,
                          (add R23, R22, R21, R20, R19, R18, R17, R16)>;

// Main 16-bit pair register class.
def DREGS : RegisterClass<"AVR", [i16], 8,
                          (
                              // Return value and arguments.
                              add R25R24, R19R18, R21R20, R23R22,
                              // Scratch registers.
                              R31R30, R27R26,
                              // Callee saved registers.
                              R29R28, R17R16, R15R14, R13R12, R11R10, R9R8,
                              R7R6, R5R4, R3R2, R1R0,
                              // Pseudo regs for unaligned 16-bits
                              R26R25, R24R23, R22R21, R20R19, R18R17, R16R15,
                              R14R13, R12R11, R10R9)>;

// Lower 16-bit pair registers in R0..R15, only used in inline assembly.
def DREGSlo
    : RegisterClass<"AVR", [i16], 8,
                    (add R15R14, R13R12, R11R10, R9R8, R7R6, R5R4, R3R2, R1R0)>;

// Lower 16-bit pair registers in r16..r23, only used in inline assembly.
def DREGSLD8lo : RegisterClass<"AVR", [i16], 8,
                               (
                                   // Return value and arguments.
                                   add R19R18, R21R20, R23R22,
                                   // Callee saved registers.
                                   R17R16)>;

// 16-bit pair register class for movw
def DREGSMOVW : RegisterClass<"AVR", [i16], 8,
                              (
                                  // Return value and arguments.
                                  add R25R24, R19R18, R21R20, R23R22,
                                  // Scratch registers.
                                  R31R30, R27R26,
                                  // Callee saved registers.
                                  R29R28, R17R16, R15R14, R13R12, R11R10, R9R8,
                                  R7R6, R5R4, R3R2, R1R0)>;

// 16-bit register class for immediate instructions.
def DLDREGS : RegisterClass<"AVR", [i16], 8,
                            (
                                // Return value and arguments.
                                add R25R24, R19R18, R21R20, R23R22,
                                // Scratch registers.
                                R31R30, R27R26,
                                // Callee saved registers.
                                R29R28, R17R16)>;

// 16-bit register class for the adiw/sbiw instructions.
def IWREGS : RegisterClass<"AVR", [i16], 8,
                           (
                               // Return value and arguments.
                               add R25R24,
                               // Scratch registers.
                               R31R30, R27R26,
                               // Callee saved registers.
                               R29R28)>;

// 16-bit register class for the ld and st instructions.
// AKA X,Y, and Z
def PTRREGS : RegisterClass<"AVR", [i16], 8,
                            (add R27R26, // X
                             R29R28,     // Y
                             R31R30),    // Z
                            ptr>;

// 16-bit register class for the ldd and std instructions.
// AKA Y and Z.
def PTRDISPREGS : RegisterClass<"AVR", [i16], 8, (add R31R30, R29R28), ptr>;

// We have a bunch of instructions with an explicit Z register argument. We
// model this using a register class containing only the Z register.
def ZREG : RegisterClass<"AVR", [i16], 8, (add R31R30)>;

// Register class used for the stack read pseudo instruction.
def GPRSP : RegisterClass<"AVR", [i16], 8, (add SP)>;

// Status register.
def SREG : AVRReg<14, "FLAGS">, DwarfRegNum<[88]>;
def CCR : RegisterClass<"AVR", [i8], 8, (add SREG)> {
  let CopyCost = -1; // Don't allow copying of status registers
}
