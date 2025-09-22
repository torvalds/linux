//===-- VERegisterInfo.td - VE Register defs ---------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the VE register file
//===----------------------------------------------------------------------===//

class VEReg<bits<7> enc, string n, list<Register> subregs = [],
            list<string> altNames = [], list<Register> aliases = []>
        : Register<n, altNames> {
  let HWEncoding{15-7} = 0;
  let HWEncoding{6-0} = enc;
  let Namespace = "VE";
  let SubRegs = subregs;
  let Aliases = aliases;
}

class VEMiscReg<bits<6> enc, string n>: Register<n> {
  let HWEncoding{15-6} = 0;
  let HWEncoding{5-0} = enc;
  let Namespace = "VE";
}

class VEVecReg<bits<8> enc, string n, list<Register> subregs = [],
               list<string> altNames = [], list<Register> aliases = []>
        : Register<n, altNames> {
  let HWEncoding{15-8} = 0;
  let HWEncoding{7-0} = enc;
  let Namespace = "VE";
  let SubRegs = subregs;
  let Aliases = aliases;
}

class VEMaskReg<bits<4> enc, string n, list<Register> subregs = [],
                list<string> altNames = [], list<Register> aliases = []>
        : Register<n, altNames> {
  let HWEncoding{15-4} = 0;
  let HWEncoding{3-0} = enc;
  let Namespace = "VE";
  let SubRegs = subregs;
  let Aliases = aliases;
}

let Namespace = "VE" in {
  def sub_i32     : SubRegIndex<32, 32>;        // Low 32 bit (32..63)
  def sub_f32     : SubRegIndex<32>;            // High 32 bit (0..31)
  def sub_even    : SubRegIndex<64>;            // High 64 bit (0..63)
  def sub_odd     : SubRegIndex<64, 64>;        // Low 64 bit (64..127)
  def sub_vm_even : SubRegIndex<256>;           // High 256 bit (0..255)
  def sub_vm_odd  : SubRegIndex<256, 256>;      // Low 256 bit (256..511)
  def AsmName     : RegAltNameIndex;
}

//-----------------------------------------------------------------------------
// Miscellaneous Registers
//-----------------------------------------------------------------------------

def USRCC : VEMiscReg<0, "usrcc">;      // User clock counter
def PSW : VEMiscReg<1, "psw">;          // Program status word
def SAR : VEMiscReg<2, "sar">;          // Store address register
def PMMR : VEMiscReg<7, "pmmr">;        // Performance monitor mode register

// Performance monitor configuration registers
foreach I = 0-3 in
  def PMCR#I : VEMiscReg<!add(8,I), "pmcr"#I>;

// Performance monitor counter
foreach I = 0-14 in
  def PMC#I : VEMiscReg<!add(16,I), "pmc"#I>;

// Register classes.
def MISC : RegisterClass<"VE", [i64], 64,
                         (add USRCC, PSW, SAR, PMMR,
                              (sequence "PMCR%u", 0, 3),
                              (sequence "PMC%u", 0, 14))>;

//-----------------------------------------------------------------------------
// Instruction Counter Register
//-----------------------------------------------------------------------------

def IC : VEMiscReg<62, "ic">;

//-----------------------------------------------------------------------------
// Vector Length Register
//-----------------------------------------------------------------------------

def VL : VEMiscReg<63, "vl">;

// Register classes.
def VLS : RegisterClass<"VE", [i32], 64, (add VL)>;

//-----------------------------------------------------------------------------
// Generic Registers
//-----------------------------------------------------------------------------

let RegAltNameIndices = [AsmName] in {

// Generic integer registers - 32 bits wide
foreach I = 0-63 in
  def SW#I : VEReg<I, "sw"#I, [], ["s"#I]>, DwarfRegNum<[I]>;

// Generic floating point registers - 32 bits wide
//   NOTE: Mark SF#I as alias of SW#I temporary to avoid register allocation
//         problem.
foreach I = 0-63 in
  def SF#I : VEReg<I, "sf"#I, [], ["s"#I], [!cast<VEReg>("SW"#I)]>,
                   DwarfRegNum<[I]>;

// Generic integer registers - 64 bits wide
let SubRegIndices = [sub_i32, sub_f32], CoveredBySubRegs = 1 in {
  // Several registers have specific names, so add them to one of aliases.
  def SX8 : VEReg<8, "s8", [SW8, SF8], ["s8", "sl"]>, DwarfRegNum<[8]>;
  def SX9 : VEReg<9, "s9", [SW9, SF9], ["s9", "fp"]>, DwarfRegNum<[9]>;
  def SX10 : VEReg<10, "s10", [SW10, SF10], ["s10", "lr"]>, DwarfRegNum<[10]>;
  def SX11 : VEReg<11, "s11", [SW11, SF11], ["s11", "sp"]>, DwarfRegNum<[11]>;
  def SX14 : VEReg<14, "s14", [SW14, SF14], ["s14", "tp"]>, DwarfRegNum<[14]>;
  def SX15 : VEReg<15, "s15", [SW15, SF15], ["s15", "got"]>, DwarfRegNum<[15]>;
  def SX16 : VEReg<16, "s16", [SW16, SF16], ["s16", "plt"]>, DwarfRegNum<[16]>;

  // Other generic registers.
  foreach I = { 0-7, 12-13, 17-63 } in
    def SX#I : VEReg<I, "s"#I, [!cast<VEReg>("SW"#I), !cast<VEReg>("SF"#I)],
                     ["s"#I]>, DwarfRegNum<[I]>;
}

// Aliases of the S* registers used to hold 128-bit for values (long doubles).
// Following foreach represents something like:
//   def Q0 : VEReg<0, "q0", [SX0, SX1], ["s0"]>;
//   def Q1 : VEReg<2, "q2", [SX2, SX3], ["s2"]>;
//   ...
let SubRegIndices = [sub_even, sub_odd], CoveredBySubRegs = 1 in
foreach I = 0-31 in
  def Q#I : VEReg<!shl(I,1), "q"#I,
                  [!cast<VEReg>("SX"#!shl(I,1)),
                   !cast<VEReg>("SX"#!add(!shl(I,1),1))],
                  ["s"#!shl(I,1)]>;

// Vector registers - 64 bits wide 256 elements
foreach I = 0-63 in
  def V#I : VEVecReg<I, "v"#I, [], ["v"#I]>, DwarfRegNum<[!add(64,I)]>;

// Vector Index Register
def VIX : VEVecReg<255, "vix", [], ["vix"]>;

// Vector mask registers - 256 bits wide
let isConstant = true in
def VM0 : VEMaskReg<0, "vm0", [], ["vm0"]>, DwarfRegNum<[128]>;
foreach I = 1-15 in
  def VM#I : VEMaskReg<I, "vm"#I, [], ["vm"#I]>, DwarfRegNum<[!add(128,I)]>;

// Aliases of VMs to use as a pair of two VM for packed instructions
let isConstant = true in
def VMP0 : VEMaskReg<0, "vm0", [], ["vm0"]>;

let SubRegIndices = [sub_vm_even, sub_vm_odd], CoveredBySubRegs = 1 in
foreach I = 1-7 in
  def VMP#I : VEMaskReg<!shl(I,1), "vmp"#I,
                        [!cast<VEMaskReg>("VM"#!shl(I,1)),
                         !cast<VEMaskReg>("VM"#!add(!shl(I,1),1))],
                        ["vm"#!shl(I,1)]>;

} // RegAltNameIndices = [AsmName]

// Register classes.
//
// The register order is defined in terms of the preferred
// allocation order.
def I32 : RegisterClass<"VE", [i32], 32,
                        (add (sequence "SW%u", 0, 7),
                             (sequence "SW%u", 34, 63),
                             (sequence "SW%u", 8, 33))>;
def I64 : RegisterClass<"VE", [i64, f64], 64,
                        (add (sequence "SX%u", 0, 7),
                             (sequence "SX%u", 34, 63),
                             (sequence "SX%u", 8, 33))>;
def F32 : RegisterClass<"VE", [f32], 32,
                        (add (sequence "SF%u", 0, 7),
                             (sequence "SF%u", 34, 63),
                             (sequence "SF%u", 8, 33))>;
def F128 : RegisterClass<"VE", [f128], 128,
                        (add (sequence "Q%u", 0, 3),
                             (sequence "Q%u", 17, 31),
                             (sequence "Q%u", 4, 16))>;

def V64 : RegisterClass<"VE",
                        [v256f64, // default type for vector registers
                         v512i32, v512f32,
                         v256i64, v256i32, v256f32, /* v256f64, */], 64,
                        (add (sequence "V%u", 0, 63),
                             VIX)>;

// vm0 is reserved for always true
def VM : RegisterClass<"VE", [v256i1], 64, (sequence "VM%u", 0, 15)>;
def VM512 : RegisterClass<"VE", [v512i1], 64, (sequence "VMP%u", 0, 7)>;
