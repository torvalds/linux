//===- PPCRegisterInfoDMR.td - The PowerPC Register File *- tablegen -*----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Register info specific to Power PC Dense Math Registers(DMR).
//
// Register classes in this file are related to the Dense Math Registers (DMR).
// There are a total of 8 DMR registers numbered 0 to 7.
// The 4 different views of each DMR register.
//
// [                             DMR0                              ]
// |             WACC0             |           WACC_HI0            |
// |    DMRROWp0   |    DMRROWp1   |    DMRROWp2   |    DMRROWp3   |
// |DMRROW0|DMRROW1|DMRROW2|DMRROW3|DMRROW4|DMRROW5|DMRROW6|DMRROW7|
// [128bits|128bits|128bits|128bits|128bits|128bits|128bits|128bits]
//
// In addition to the above classes two consecutive DMR registers make a DMR
// DMR pair (DMRp) that is 2048 bits.
//===----------------------------------------------------------------------===//

let Namespace = "PPC" in {
def sub_dmrrow0 : SubRegIndex<128>;
def sub_dmrrow1 : SubRegIndex<128, 128>;
def sub_dmrrowp0 : SubRegIndex<256>;
def sub_dmrrowp1 : SubRegIndex<256, 256>;
def sub_wacc_lo : SubRegIndex<512>;
def sub_wacc_hi : SubRegIndex<512, 512>;
def sub_dmr0 : SubRegIndex<1024>;
def sub_dmr1 : SubRegIndex<1024, 1024>;
}

// A single row in a DMR register.
// There are 8 128 bit rows in each DMR register and 8 DMR registers so that
// makes 64 DMRROW registers in total.
class DMRROW<bits<6> num, string n> : PPCReg<n> {
  let HWEncoding{5-0} = num;
}

// A consecutive pair of DMR row registers.
class DMRROWp<bits<5> num, string n, list<Register> subregs> : PPCReg<n> {
  let HWEncoding{4-0} = num;
  let SubRegs = subregs;
}

// WACC - Wide ACC registers. Accumulator registers that are subregs of DMR.
// These ACC registers no longer include VSR regs as subregs.
class WACC<bits<3> num, string n, list<Register> subregs> : PPCReg<n> {
  let HWEncoding{2-0} = num;
  let SubRegs = subregs;
}

// High bits for the ACC registers.
// When the ACC register is used these bits are ignored.
// When the ACC register is the target, these bits are set to zero.
class WACC_HI<bits<3> num, string n, list<Register> subregs> : PPCReg<n> {
  let HWEncoding{2-0} = num;
  let SubRegs = subregs;
}

class DMR<bits<3> num, string n, list<Register> subregs> : PPCReg<n> {
  let HWEncoding{2-0} = num;
  let SubRegs = subregs;
}

class DMRp<bits<2> num, string n, list<Register> subregs> : PPCReg<n> {
  let HWEncoding{1-0} = num;
  let SubRegs = subregs;
}

// The DMR Row type registers are the lowest level of registers and have no
// subregs.
foreach Index = 0-63 in {
  def DMRROW#Index : DMRROW<Index, "dmrrow"#Index>, DwarfRegNum<[-1, -1]>;
}

// DMRROW pairs are consecutive pairs.
// DMRROWp0 = DMRROW0, DMRROW1
// DMRROWp1 = DMRROW2, DMRROW3
// DMRROWp2 = DMRROW4, DMRROW5
// etc...
let SubRegIndices = [sub_dmrrow0, sub_dmrrow1] in {
  foreach Index = 0-31 in {
    def DMRROWp#Index : DMRROWp<Index, "dmrrowp"#Index,
    [!cast<DMRROW>("DMRROW"#!mul(Index, 2)),
     !cast<DMRROW>("DMRROW"#!add(!mul(Index, 2), 1))]>, DwarfRegNum<[-1, -1]>;
  }
}

let SubRegIndices = [sub_dmrrowp0, sub_dmrrowp1] in {
  // WACC0 = DMRROWp0, DMRROWp1
  // WACC1 = DMRROWp4, DMRROWp5
  // WACC2 = DMRROWp8, DMRROWp9
  // etc...
  foreach Index = 0-7 in {
    def WACC#Index : WACC<Index, "wacc"#Index,
    [!cast<DMRROWp>("DMRROWp"#!mul(Index, 4)),
     !cast<DMRROWp>("DMRROWp"#!add(!mul(Index, 4), 1))]>, DwarfRegNum<[-1, -1]>;
  }

  // WACC_HI0 = DMRROWp2, DMRROWp3
  // WACC_HI1 = DMRROWp6, DMRROWp7
  // WACC_HI2 = DMRROWp10, DMRROWp11
  // etc...
  foreach Index = 0-7 in {
    def WACC_HI#Index : WACC_HI<Index, "wacc_hi"#Index,
    [!cast<DMRROWp>("DMRROWp"#!add(!mul(Index, 4), 2)),
     !cast<DMRROWp>("DMRROWp"#!add(!mul(Index, 4), 3))]>, DwarfRegNum<[-1, -1]>;
  }
}

// DMR0 = WACC0, WACC_HI0
// DMR1 = WACC1, WACC_HI1
// DMR2 = WACC2, WACC_HI2
// etc...
let SubRegIndices = [sub_wacc_lo, sub_wacc_hi] in {
  foreach Index = 0-7 in {
    def DMR#Index : DMR<Index, "dmr"#Index, [!cast<WACC>("WACC"#Index), !cast<WACC_HI>("WACC_HI"#Index)]>, DwarfRegNum<[-1, -1]>;
  }
}

// DMRp0 = DMR0, DMR1
// DMRp1 = DMR2, DMR3
// DMRp2 = DMR4, DMR5
// DMRp3 = DMR6, DMR7
let SubRegIndices = [sub_dmr0, sub_dmr1] in {
  def DMRp0 : DMRp<0, "dmrp0", [DMR0, DMR1]>, DwarfRegNum<[-1, -1]>;
  def DMRp1 : DMRp<1, "dmrp1", [DMR2, DMR3]>, DwarfRegNum<[-1, -1]>;
  def DMRp2 : DMRp<2, "dmrp2", [DMR4, DMR5]>, DwarfRegNum<[-1, -1]>;
  def DMRp3 : DMRp<3, "dmrp3", [DMR6, DMR7]>, DwarfRegNum<[-1, -1]>;
}

def DMRROWRC :  RegisterClass<"PPC", [v128i1], 128,
                               (add (sequence "DMRROW%u", 0, 63))> {
  let Size = 128;
}

def DMRROWpRC :  RegisterClass<"PPC", [v256i1], 128,
                               (add (sequence "DMRROWp%u", 0, 31))> {
  let Size = 256;
}

def WACCRC : RegisterClass<"PPC", [v512i1], 128,
                           (add (sequence "WACC%u", 0, 7))> {
  let Size = 512;
}

def WACC_HIRC : RegisterClass<"PPC", [v512i1], 128,
                              (add (sequence "WACC_HI%u", 0, 7))> {
  let Size = 512;
}

def DMRRC : RegisterClass<"PPC", [v1024i1], 128,
                           (add (sequence "DMR%u", 0, 7))> {
  let Size = 1024;
}

def DMRpRC : RegisterClass<"PPC", [v2048i1], 128,
                           (add DMRp0, DMRp1, DMRp2, DMRp3)> {
  let Size = 2048;
}
