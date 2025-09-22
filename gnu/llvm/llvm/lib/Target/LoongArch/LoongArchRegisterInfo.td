//===-- LoongArchRegisterInfo.td - LoongArch Register defs -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the LoongArch register files
//===----------------------------------------------------------------------===//

let Namespace = "LoongArch" in {
class LoongArchReg<bits<16> Enc, string n, list<string> alt = []>
    : Register<n> {
  let HWEncoding = Enc;
  let AltNames = alt;
}

class LoongArchRegWithSubRegs<bits<16> Enc, string n, list<Register> subregs,
                              list<string> alt = []>
    : RegisterWithSubRegs<n, subregs> {
  let HWEncoding = Enc;
  let AltNames = alt;
}

class LoongArchReg32<bits<16> Enc, string n, list<string> alt = []>
    : Register<n> {
  let HWEncoding = Enc;
  let AltNames = alt;
}

def sub_32 : SubRegIndex<32>;
class LoongArchReg64<LoongArchReg32 subreg>
    : LoongArchRegWithSubRegs<subreg.HWEncoding, subreg.AsmName, [subreg],
                              subreg.AltNames> {
  let SubRegIndices = [sub_32];
}

def sub_64 : SubRegIndex<64>;
class LoongArchReg128<LoongArchReg64 subreg, string n>
    : LoongArchRegWithSubRegs<subreg.HWEncoding, n, [subreg]> {
  let SubRegIndices = [sub_64];
}

def sub_128 : SubRegIndex<128>;
class LoongArchReg256<LoongArchReg128 subreg, string n>
    : LoongArchRegWithSubRegs<subreg.HWEncoding, n, [subreg]> {
  let SubRegIndices = [sub_128];
}

let FallbackRegAltNameIndex = NoRegAltName in
def RegAliasName : RegAltNameIndex;
} // Namespace = "LoongArch"

// Integer registers

let RegAltNameIndices = [RegAliasName] in {
  let isConstant = true in
  def R0  : LoongArchReg<0,  "r0", ["zero"]>, DwarfRegNum<[0]>;
  def R1  : LoongArchReg<1,  "r1", ["ra"]>, DwarfRegNum<[1]>;
  def R2  : LoongArchReg<2,  "r2", ["tp"]>, DwarfRegNum<[2]>;
  def R3  : LoongArchReg<3,  "r3", ["sp"]>, DwarfRegNum<[3]>;
  def R4  : LoongArchReg<4,  "r4", ["a0"]>, DwarfRegNum<[4]>;
  def R5  : LoongArchReg<5,  "r5", ["a1"]>, DwarfRegNum<[5]>;
  def R6  : LoongArchReg<6,  "r6", ["a2"]>, DwarfRegNum<[6]>;
  def R7  : LoongArchReg<7,  "r7", ["a3"]>, DwarfRegNum<[7]>;
  def R8  : LoongArchReg<8,  "r8", ["a4"]>, DwarfRegNum<[8]>;
  def R9  : LoongArchReg<9,  "r9", ["a5"]>, DwarfRegNum<[9]>;
  def R10 : LoongArchReg<10, "r10", ["a6"]>, DwarfRegNum<[10]>;
  def R11 : LoongArchReg<11, "r11", ["a7"]>, DwarfRegNum<[11]>;
  def R12 : LoongArchReg<12, "r12", ["t0"]>, DwarfRegNum<[12]>;
  def R13 : LoongArchReg<13, "r13", ["t1"]>, DwarfRegNum<[13]>;
  def R14 : LoongArchReg<14, "r14", ["t2"]>, DwarfRegNum<[14]>;
  def R15 : LoongArchReg<15, "r15", ["t3"]>, DwarfRegNum<[15]>;
  def R16 : LoongArchReg<16, "r16", ["t4"]>, DwarfRegNum<[16]>;
  def R17 : LoongArchReg<17, "r17", ["t5"]>, DwarfRegNum<[17]>;
  def R18 : LoongArchReg<18, "r18", ["t6"]>, DwarfRegNum<[18]>;
  def R19 : LoongArchReg<19, "r19", ["t7"]>, DwarfRegNum<[19]>;
  def R20 : LoongArchReg<20, "r20", ["t8"]>, DwarfRegNum<[20]>;
  def R21 : LoongArchReg<21, "r21", [""]>, DwarfRegNum<[21]>;
  def R22 : LoongArchReg<22, "r22", ["fp", "s9"]>, DwarfRegNum<[22]>;
  def R23 : LoongArchReg<23, "r23", ["s0"]>, DwarfRegNum<[23]>;
  def R24 : LoongArchReg<24, "r24", ["s1"]>, DwarfRegNum<[24]>;
  def R25 : LoongArchReg<25, "r25", ["s2"]>, DwarfRegNum<[25]>;
  def R26 : LoongArchReg<26, "r26", ["s3"]>, DwarfRegNum<[26]>;
  def R27 : LoongArchReg<27, "r27", ["s4"]>, DwarfRegNum<[27]>;
  def R28 : LoongArchReg<28, "r28", ["s5"]>, DwarfRegNum<[28]>;
  def R29 : LoongArchReg<29, "r29", ["s6"]>, DwarfRegNum<[29]>;
  def R30 : LoongArchReg<30, "r30", ["s7"]>, DwarfRegNum<[30]>;
  def R31 : LoongArchReg<31, "r31", ["s8"]>, DwarfRegNum<[31]>;
} // RegAltNameIndices = [RegAliasName]

def GRLenVT : ValueTypeByHwMode<[LA32, LA64],
                                [i32,  i64]>;
def GRLenRI : RegInfoByHwMode<
      [LA32,              LA64],
      [RegInfo<32,32,32>, RegInfo<64,64,64>]>;

// The order of registers represents the preferred allocation sequence.
// Registers are listed in the order caller-save, callee-save, specials.
def GPR : RegisterClass<"LoongArch", [GRLenVT], 32, (add
    // Argument registers (a0...a7)
    (sequence "R%u", 4, 11),
    // Temporary registers (t0...t8)
    (sequence "R%u", 12, 20),
    // Static register (s9/fp, s0...s8)
    (sequence "R%u", 22, 31),
    // Specials (r0, ra, tp, sp)
    (sequence "R%u", 0, 3),
    // Reserved (Non-allocatable)
    R21
  )> {
  let RegInfos = GRLenRI;
}

// GPR for indirect tail calls. We can't use callee-saved registers, as they are
// restored to the saved value before the tail call, which would clobber a call
// address.
def GPRT : RegisterClass<"LoongArch", [GRLenVT], 32, (add
    // a0...a7, t0...t8
    (sequence "R%u", 4, 20)
  )> {
  let RegInfos = GRLenRI;
}

// Floating point registers

let RegAltNameIndices = [RegAliasName] in {
  def F0  : LoongArchReg32<0, "f0", ["fa0"]>, DwarfRegNum<[32]>;
  def F1  : LoongArchReg32<1, "f1", ["fa1"]>, DwarfRegNum<[33]>;
  def F2  : LoongArchReg32<2, "f2", ["fa2"]>, DwarfRegNum<[34]>;
  def F3  : LoongArchReg32<3, "f3", ["fa3"]>, DwarfRegNum<[35]>;
  def F4  : LoongArchReg32<4, "f4", ["fa4"]>, DwarfRegNum<[36]>;
  def F5  : LoongArchReg32<5, "f5", ["fa5"]>, DwarfRegNum<[37]>;
  def F6  : LoongArchReg32<6, "f6", ["fa6"]>, DwarfRegNum<[38]>;
  def F7  : LoongArchReg32<7, "f7", ["fa7"]>, DwarfRegNum<[39]>;
  def F8  : LoongArchReg32<8, "f8", ["ft0"]>, DwarfRegNum<[40]>;
  def F9  : LoongArchReg32<9, "f9", ["ft1"]>, DwarfRegNum<[41]>;
  def F10 : LoongArchReg32<10,"f10", ["ft2"]>, DwarfRegNum<[42]>;
  def F11 : LoongArchReg32<11,"f11", ["ft3"]>, DwarfRegNum<[43]>;
  def F12 : LoongArchReg32<12,"f12", ["ft4"]>, DwarfRegNum<[44]>;
  def F13 : LoongArchReg32<13,"f13", ["ft5"]>, DwarfRegNum<[45]>;
  def F14 : LoongArchReg32<14,"f14", ["ft6"]>, DwarfRegNum<[46]>;
  def F15 : LoongArchReg32<15,"f15", ["ft7"]>, DwarfRegNum<[47]>;
  def F16 : LoongArchReg32<16,"f16", ["ft8"]>, DwarfRegNum<[48]>;
  def F17 : LoongArchReg32<17,"f17", ["ft9"]>, DwarfRegNum<[49]>;
  def F18 : LoongArchReg32<18,"f18", ["ft10"]>, DwarfRegNum<[50]>;
  def F19 : LoongArchReg32<19,"f19", ["ft11"]>, DwarfRegNum<[51]>;
  def F20 : LoongArchReg32<20,"f20", ["ft12"]>, DwarfRegNum<[52]>;
  def F21 : LoongArchReg32<21,"f21", ["ft13"]>, DwarfRegNum<[53]>;
  def F22 : LoongArchReg32<22,"f22", ["ft14"]>, DwarfRegNum<[54]>;
  def F23 : LoongArchReg32<23,"f23", ["ft15"]>, DwarfRegNum<[55]>;
  def F24 : LoongArchReg32<24,"f24", ["fs0"]>, DwarfRegNum<[56]>;
  def F25 : LoongArchReg32<25,"f25", ["fs1"]>, DwarfRegNum<[57]>;
  def F26 : LoongArchReg32<26,"f26", ["fs2"]>, DwarfRegNum<[58]>;
  def F27 : LoongArchReg32<27,"f27", ["fs3"]>, DwarfRegNum<[59]>;
  def F28 : LoongArchReg32<28,"f28", ["fs4"]>, DwarfRegNum<[60]>;
  def F29 : LoongArchReg32<29,"f29", ["fs5"]>, DwarfRegNum<[61]>;
  def F30 : LoongArchReg32<30,"f30", ["fs6"]>, DwarfRegNum<[62]>;
  def F31 : LoongArchReg32<31,"f31", ["fs7"]>, DwarfRegNum<[63]>;

  foreach I = 0-31 in {
    def F#I#_64 : LoongArchReg64<!cast<LoongArchReg32>("F"#I)>,
      DwarfRegNum<[!add(I, 32)]>;
  }
}

// The order of registers represents the preferred allocation sequence.
def FPR32 : RegisterClass<"LoongArch", [f32], 32, (sequence "F%u", 0, 31)>;
def FPR64 : RegisterClass<"LoongArch", [f64], 64, (sequence "F%u_64", 0, 31)>;

// Condition flag registers

foreach I = 0-7 in
def FCC#I : LoongArchReg<I, "fcc"#I>;

def CFR : RegisterClass<"LoongArch", [GRLenVT], 32, (sequence "FCC%u", 0, 7)> {
  let RegInfos = GRLenRI;
}

// Control and status registers

foreach I = 0-3 in
def FCSR#I : LoongArchReg<I, "fcsr"#I>;

let isAllocatable = false in
def FCSR : RegisterClass<"LoongArch", [i32], 32, (sequence "FCSR%u", 0, 3)>;

// LSX registers

foreach I = 0-31 in
def VR#I : LoongArchReg128<!cast<LoongArchReg64>("F"#I#"_64"), "vr"#I>,
           DwarfRegAlias<!cast<LoongArchReg64>("F"#I#"_64")>;

def LSX128 : RegisterClass<"LoongArch",
                           [v4f32, v2f64, v16i8, v8i16, v4i32, v2i64],
                           128, (sequence "VR%u", 0, 31)>;

// LASX registers

foreach I = 0-31 in
def XR#I : LoongArchReg256<!cast<LoongArchReg128>("VR"#I), "xr"#I>,
           DwarfRegAlias<!cast<LoongArchReg128>("VR"#I)>;

def LASX256 : RegisterClass<"LoongArch",
                            [v8f32, v4f64, v32i8, v16i16, v8i32, v4i64],
                            256, (sequence "XR%u", 0, 31)>;

// Scratchpad registers

foreach I = 0-3 in
def SCR#I : LoongArchReg<I, "scr"#I>;

let isAllocatable = false, RegInfos = GRLenRI in
def SCR : RegisterClass<"LoongArch", [GRLenVT], 32, (sequence "SCR%u", 0, 3)>;
