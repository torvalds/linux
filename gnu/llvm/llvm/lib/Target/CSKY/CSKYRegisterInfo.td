//===-- CSKYRegisterInfo.td - CSKY Register defs -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the CSKY registers.
//===----------------------------------------------------------------------===//

let Namespace = "CSKY" in {
  class CSKYReg<bits<6> Enc, string n, list<string> alt = []> : Register<n> {
    let HWEncoding{5 - 0} = Enc;
    let AltNames = alt;
  }

  class CSKYFReg32<bits<5> Enc, string n, list<string> alt = []> : Register<n> {
    let HWEncoding{4 - 0} = Enc;
    let AltNames = alt;
  }

  // Because CSKYFReg64 register have AsmName and AltNames that alias with their
  // 32-bit sub-register, CSKYAsmParser will need to coerce a register number
  // from a CSKYFReg32 to the equivalent CSKYFReg64 when appropriate.
  def sub32_0 : SubRegIndex<32, 0>;
  def sub32_32 : SubRegIndex<32, 32>;
  def sub64_0 : SubRegIndex<64, 0>;
  def sub64_64 : SubRegIndex<64,64>;

  class CSKYFReg64<CSKYFReg32 subreg> : Register<""> {
    let HWEncoding{4 - 0} = subreg.HWEncoding{4 - 0};
    let SubRegs = [subreg];
    let SubRegIndices = [sub32_0];
    let AsmName = subreg.AsmName;
    let AltNames = subreg.AltNames;
  }

  class CSKYFReg128<CSKYFReg64 subreg> : Register<""> {
    let HWEncoding{4 - 0} = subreg.HWEncoding{4 - 0};
    let SubRegs = [subreg];
    let SubRegIndices = [sub64_0];
    let AsmName = subreg.AsmName;
    let AltNames = subreg.AltNames;
  }

  def ABIRegAltName : RegAltNameIndex;
} // Namespace = "CSKY"

let RegAltNameIndices = [ABIRegAltName] in {
  def R0 : CSKYReg<0, "r0", ["a0"]>, DwarfRegNum<[0]>;
  def R1 : CSKYReg<1, "r1", ["a1"]>, DwarfRegNum<[1]>;
  def R2 : CSKYReg<2, "r2", ["a2"]>, DwarfRegNum<[2]>;
  def R3 : CSKYReg<3, "r3", ["a3"]>, DwarfRegNum<[3]>;
  def R4 : CSKYReg<4, "r4", ["l0"]>, DwarfRegNum<[4]>;
  def R5 : CSKYReg<5, "r5", ["l1"]>, DwarfRegNum<[5]>;
  def R6 : CSKYReg<6, "r6", ["l2"]>, DwarfRegNum<[6]>;
  def R7 : CSKYReg<7, "r7", ["l3"]>, DwarfRegNum<[7]>;
  def R8 : CSKYReg<8, "r8", ["l4"]>, DwarfRegNum<[8]>;
  def R9 : CSKYReg<9, "r9", ["l5"]>, DwarfRegNum<[9]>;
  def R10 : CSKYReg<10, "r10", ["l6"]>, DwarfRegNum<[10]>;
  def R11 : CSKYReg<11, "r11", ["l7"]>, DwarfRegNum<[11]>;
  def R12 : CSKYReg<12, "r12", ["t0"]>, DwarfRegNum<[12]>;
  def R13 : CSKYReg<13, "r13", ["t1"]>, DwarfRegNum<[13]>;
  def R14 : CSKYReg<14, "r14", ["sp"]>, DwarfRegNum<[14]>;
  def R15 : CSKYReg<15, "r15", ["lr"]>, DwarfRegNum<[15]>;
  def R16 : CSKYReg<16, "r16", ["l8"]>, DwarfRegNum<[16]>;
  def R17 : CSKYReg<17, "r17", ["l9"]>, DwarfRegNum<[17]>;
  def R18 : CSKYReg<18, "r18", ["t2"]>, DwarfRegNum<[18]>;
  def R19 : CSKYReg<19, "r19", ["t3"]>, DwarfRegNum<[19]>;
  def R20 : CSKYReg<20, "r20", ["t4"]>, DwarfRegNum<[20]>;
  def R21 : CSKYReg<21, "r21", ["t5"]>, DwarfRegNum<[21]>;
  def R22 : CSKYReg<22, "r22", ["t6"]>, DwarfRegNum<[22]>;
  def R23 : CSKYReg<23, "r23", ["t7"]>, DwarfRegNum<[23]>;
  def R24 : CSKYReg<24, "r24", ["t8"]>, DwarfRegNum<[24]>;
  def R25 : CSKYReg<25, "r25", ["t9"]>, DwarfRegNum<[25]>;
  def R26 : CSKYReg<26, "r26", ["r26"]>, DwarfRegNum<[26]>;
  def R27 : CSKYReg<27, "r27", ["r27"]>, DwarfRegNum<[27]>;
  def R28 : CSKYReg<28, "r28", ["rgb"]>, DwarfRegNum<[28]>;
  def R29 : CSKYReg<29, "r29", ["rtb"]>, DwarfRegNum<[29]>;
  def R30 : CSKYReg<30, "r30", ["svbr"]>, DwarfRegNum<[30]>;
  def R31 : CSKYReg<31, "r31", ["tls"]>, DwarfRegNum<[31]>;

  // Faked for GPRTuple
  def R32 : CSKYReg<32, "r32", ["r32"]>, DwarfRegNum<[32]>;

  def C : CSKYReg<33, "cr0", ["psr"]>;

}

def GPRTuple : RegisterTuples<
          [sub32_0, sub32_32],
          [(add (sequence "R%u", 0, 31)), (add (sequence "R%u", 1, 32))],
          [ "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
            "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
            "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
            "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31"
          ]>;

// Floating point registers
let RegAltNameIndices = [ABIRegAltName] in {
  def F0_32 : CSKYFReg32<0, "fr0", ["vr0"]>, DwarfRegNum<[32]>;
  def F1_32 : CSKYFReg32<1, "fr1", ["vr1"]>, DwarfRegNum<[33]>;
  def F2_32 : CSKYFReg32<2, "fr2", ["vr2"]>, DwarfRegNum<[34]>;
  def F3_32 : CSKYFReg32<3, "fr3", ["vr3"]>, DwarfRegNum<[35]>;
  def F4_32 : CSKYFReg32<4, "fr4", ["vr4"]>, DwarfRegNum<[36]>;
  def F5_32 : CSKYFReg32<5, "fr5", ["vr5"]>, DwarfRegNum<[37]>;
  def F6_32 : CSKYFReg32<6, "fr6", ["vr6"]>, DwarfRegNum<[38]>;
  def F7_32 : CSKYFReg32<7, "fr7", ["vr7"]>, DwarfRegNum<[39]>;
  def F8_32 : CSKYFReg32<8, "fr8", ["vr8"]>, DwarfRegNum<[40]>;
  def F9_32 : CSKYFReg32<9, "fr9", ["vr9"]>, DwarfRegNum<[41]>;
  def F10_32 : CSKYFReg32<10, "fr10", ["vr10"]>, DwarfRegNum<[42]>;
  def F11_32 : CSKYFReg32<11, "fr11", ["vr11"]>, DwarfRegNum<[43]>;
  def F12_32 : CSKYFReg32<12, "fr12", ["vr12"]>, DwarfRegNum<[44]>;
  def F13_32 : CSKYFReg32<13, "fr13", ["vr13"]>, DwarfRegNum<[45]>;
  def F14_32 : CSKYFReg32<14, "fr14", ["vr14"]>, DwarfRegNum<[46]>;
  def F15_32 : CSKYFReg32<15, "fr15", ["vr15"]>, DwarfRegNum<[47]>;
  def F16_32 : CSKYFReg32<16, "fr16", ["vr16"]>, DwarfRegNum<[48]>;
  def F17_32 : CSKYFReg32<17, "fr17", ["vr17"]>, DwarfRegNum<[49]>;
  def F18_32 : CSKYFReg32<18, "fr18", ["vr18"]>, DwarfRegNum<[50]>;
  def F19_32 : CSKYFReg32<19, "fr19", ["vr19"]>, DwarfRegNum<[51]>;
  def F20_32 : CSKYFReg32<20, "fr20", ["vr20"]>, DwarfRegNum<[52]>;
  def F21_32 : CSKYFReg32<21, "fr21", ["vr21"]>, DwarfRegNum<[53]>;
  def F22_32 : CSKYFReg32<22, "fr22", ["vr22"]>, DwarfRegNum<[54]>;
  def F23_32 : CSKYFReg32<23, "fr23", ["vr23"]>, DwarfRegNum<[55]>;
  def F24_32 : CSKYFReg32<24, "fr24", ["vr24"]>, DwarfRegNum<[56]>;
  def F25_32 : CSKYFReg32<25, "fr25", ["vr25"]>, DwarfRegNum<[57]>;
  def F26_32 : CSKYFReg32<26, "fr26", ["vr26"]>, DwarfRegNum<[58]>;
  def F27_32 : CSKYFReg32<27, "fr27", ["vr27"]>, DwarfRegNum<[59]>;
  def F28_32 : CSKYFReg32<28, "fr28", ["vr28"]>, DwarfRegNum<[60]>;
  def F29_32 : CSKYFReg32<29, "fr29", ["vr29"]>, DwarfRegNum<[61]>;
  def F30_32 : CSKYFReg32<30, "fr30", ["vr30"]>, DwarfRegNum<[62]>;
  def F31_32 : CSKYFReg32<31, "fr31", ["vr31"]>, DwarfRegNum<[63]>;

  foreach Index = 0 - 31 in {
    def F#Index#_64 : CSKYFReg64<!cast<CSKYFReg32>("F"#Index#"_32")>,
                      DwarfRegNum<[!add(Index, 32)]>;

    def F#Index#_128 : CSKYFReg128<!cast<CSKYFReg64>("F"#Index#"_64")>,
                       DwarfRegNum<[!add(Index, 32)]>;
  }
}


//===----------------------------------------------------------------------===//
// Declarations that describe the CSKY register class.
//===----------------------------------------------------------------------===//

// The order of registers represents the preferred allocation sequence.
// Registers are listed in the order caller-save, callee-save, specials.
def GPR : RegisterClass<"CSKY", [i32], 32,
                        (add (sequence "R%u", 0, 3), (sequence "R%u", 12, 13),
                             (sequence "R%u", 18, 25), R15, (sequence "R%u", 4, 11),
                             (sequence "R%u", 16, 17), (sequence "R%u", 26, 27), R28,
                             (sequence "R%u", 29, 30), R14, R31)> {
  let Size = 32;
}

// Register class for R0 - R15.
// Some 16-bit integer instructions can only access R0 - R15.
def sGPR : RegisterClass<"CSKY", [i32], 32,
                        (add (sequence "R%u", 0, 3), (sequence "R%u", 12, 13), R15,
                        (sequence "R%u", 4, 11), R14)> {
  let Size = 32;
}

// Register class for R0 - R7.
// Some 16-bit integer instructions can only access R0 - R7.
def mGPR : RegisterClass<"CSKY", [i32], 32,
                        (add (sequence "R%u", 0, 7))> {
  let Size = 32;
}

// Register class for SP only.
def GPRSP : RegisterClass<"CSKY", [i32], 32, (add R14)> {
  let Size = 32;
}

def GPRPair : RegisterClass<"CSKY", [untyped], 32, (add GPRTuple)> {
  let Size = 64;
}

def CARRY : RegisterClass<"CSKY", [i32], 32, (add C)> {
  let Size = 32;
  let CopyCost = -1;
}

// The order of registers represents the preferred allocation sequence.
// Registers are listed in the order caller-save, callee-save, specials.
def FPR32 : RegisterClass<"CSKY", [f32], 32,
                         (add (sequence "F%u_32", 0, 31))>;
def sFPR32 : RegisterClass<"CSKY", [f32], 32,
                         (add (sequence "F%u_32", 0, 15))>;

def FPR64 : RegisterClass<"CSKY", [f64], 32,
                         (add (sequence "F%u_64", 0, 31))>;
def sFPR64 : RegisterClass<"CSKY", [f64], 32,
                         (add (sequence "F%u_64", 0, 15))>;

def sFPR64_V : RegisterClass<"CSKY", [v2f32], 32, (add sFPR64)>;

def FPR128 : RegisterClass<"CSKY",
             [v16i8, v8i16, v4i32, v2i64, v4f32, v2f64, v8f16], 128,
             (add (sequence "F%u_128", 0, 31))>;
def sFPR128 : RegisterClass<"CSKY",
              [v16i8, v8i16, v4i32, v2i64, v4f32, v2f64, v8f16], 128,
              (add (sequence "F%u_128", 0, 15))>;
