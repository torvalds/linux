//===-- MSP430RegisterInfo.td - MSP430 Register defs -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the MSP430 register file
//===----------------------------------------------------------------------===//

class MSP430Reg<bits<4> num, string n, list<string> alt = []> : Register<n> {
  field bits<4> Num = num;
  let Namespace = "MSP430";
  let HWEncoding{3-0} = num;
  let AltNames = alt;
  let DwarfNumbers = [num];
}

class MSP430RegWithSubregs<bits<4> num, string n, list<Register> subregs,
                           list<string> alt = []> 
  : RegisterWithSubRegs<n, subregs> {
  field bits<4> Num = num;
  let Namespace = "MSP430";
  let HWEncoding{3-0} = num;
  let AltNames = alt;
  let DwarfNumbers = [num];
}

//===----------------------------------------------------------------------===//
//  Registers
//===----------------------------------------------------------------------===//

def PCB  : MSP430Reg<0,  "r0", ["pc"]>, DwarfRegNum<[16]>;
def SPB  : MSP430Reg<1,  "r1", ["sp"]>, DwarfRegNum<[17]>;
def SRB  : MSP430Reg<2,  "r2", ["sr"]>, DwarfRegNum<[18]>;
def CGB  : MSP430Reg<3,  "r3", ["cg"]>, DwarfRegNum<[19]>;
def R4B  : MSP430Reg<4,  "r4", ["fp"]>, DwarfRegNum<[20]>;
def R5B  : MSP430Reg<5,  "r5">,  DwarfRegNum<[21]>;
def R6B  : MSP430Reg<6,  "r6">,  DwarfRegNum<[22]>;
def R7B  : MSP430Reg<7,  "r7">,  DwarfRegNum<[23]>;
def R8B  : MSP430Reg<8,  "r8">,  DwarfRegNum<[24]>;
def R9B  : MSP430Reg<9,  "r9">,  DwarfRegNum<[25]>;
def R10B : MSP430Reg<10, "r10">, DwarfRegNum<[26]>;
def R11B : MSP430Reg<11, "r11">, DwarfRegNum<[27]>;
def R12B : MSP430Reg<12, "r12">, DwarfRegNum<[28]>;
def R13B : MSP430Reg<13, "r13">, DwarfRegNum<[29]>;
def R14B : MSP430Reg<14, "r14">, DwarfRegNum<[30]>;
def R15B : MSP430Reg<15, "r15">, DwarfRegNum<[31]>;

def subreg_8bit : SubRegIndex<8> { let Namespace = "MSP430"; }

let SubRegIndices = [subreg_8bit] in {
def PC  : MSP430RegWithSubregs<0,  "r0",  [PCB], ["pc"]>, DwarfRegNum<[0]>;
def SP  : MSP430RegWithSubregs<1,  "r1",  [SPB], ["sp"]>, DwarfRegNum<[1]>;
def SR  : MSP430RegWithSubregs<2,  "r2",  [SRB], ["sr"]>, DwarfRegNum<[2]>;
def CG  : MSP430RegWithSubregs<3,  "r3",  [CGB], ["cg"]>, DwarfRegNum<[3]>;
def R4  : MSP430RegWithSubregs<4,  "r4",  [R4B], ["fp"]>, DwarfRegNum<[4]>;
def R5  : MSP430RegWithSubregs<5,  "r5",  [R5B]>,  DwarfRegNum<[5]>;
def R6  : MSP430RegWithSubregs<6,  "r6",  [R6B]>,  DwarfRegNum<[6]>;
def R7  : MSP430RegWithSubregs<7,  "r7",  [R7B]>,  DwarfRegNum<[7]>;
def R8  : MSP430RegWithSubregs<8,  "r8",  [R8B]>,  DwarfRegNum<[8]>;
def R9  : MSP430RegWithSubregs<9,  "r9",  [R9B]>,  DwarfRegNum<[9]>;
def R10 : MSP430RegWithSubregs<10, "r10", [R10B]>, DwarfRegNum<[10]>;
def R11 : MSP430RegWithSubregs<11, "r11", [R11B]>, DwarfRegNum<[11]>;
def R12 : MSP430RegWithSubregs<12, "r12", [R12B]>, DwarfRegNum<[12]>;
def R13 : MSP430RegWithSubregs<13, "r13", [R13B]>, DwarfRegNum<[13]>;
def R14 : MSP430RegWithSubregs<14, "r14", [R14B]>, DwarfRegNum<[14]>;
def R15 : MSP430RegWithSubregs<15, "r15", [R15B]>, DwarfRegNum<[15]>;
}

def GR8 : RegisterClass<"MSP430", [i8], 8,
   // Volatile registers
  (add R12B, R13B, R14B, R15B, R11B, R10B, R9B, R8B, R7B, R6B, R5B,
   // Frame pointer, sometimes allocable
   R4B,
   // Volatile, but not allocable
   PCB, SPB, SRB, CGB)>;

def GR16 : RegisterClass<"MSP430", [i16], 16,
   // Volatile registers
  (add R12, R13, R14, R15, R11, R10, R9, R8, R7, R6, R5,
   // Frame pointer, sometimes allocable
   R4,
   // Volatile, but not allocable
   PC, SP, SR, CG)>;
