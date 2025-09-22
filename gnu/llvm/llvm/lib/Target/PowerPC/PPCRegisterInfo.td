//===-- PPCRegisterInfo.td - The PowerPC Register File -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

let Namespace = "PPC" in {
def sub_lt : SubRegIndex<1>;
def sub_gt : SubRegIndex<1, 1>;
def sub_eq : SubRegIndex<1, 2>;
def sub_un : SubRegIndex<1, 3>;
def sub_32 : SubRegIndex<32>;
def sub_32_hi_phony : SubRegIndex<32,32>;
def sub_64 : SubRegIndex<64>;
def sub_64_hi_phony : SubRegIndex<64,64>;
def sub_vsx0 : SubRegIndex<128>;
def sub_vsx1 : SubRegIndex<128, 128>;
def sub_gp8_x0 : SubRegIndex<64>;
def sub_gp8_x1 : SubRegIndex<64, 64>;
def sub_fp0 : SubRegIndex<64>;
def sub_fp1 : SubRegIndex<64, 64>;
}


class PPCReg<string n> : Register<n> {
  let Namespace = "PPC";
}

// We identify all our registers with a 5-bit ID, for consistency's sake.

// GPR - One of the 32 32-bit general-purpose registers
class GPR<bits<5> num, string n> : PPCReg<n> {
  let HWEncoding{4-0} = num;
}

// GP8 - One of the 32 64-bit general-purpose registers
class GP8<GPR SubReg, string n> : PPCReg<n> {
  let HWEncoding = SubReg.HWEncoding;
  let SubRegs = [SubReg];
  let SubRegIndices = [sub_32];
}

class SPE<string n, bits<5> Enc, list<Register> subregs = []> : PPCReg<n> {
  let HWEncoding{4-0} = Enc;
  let SubRegs = subregs;
  let SubRegIndices = [sub_32, sub_32_hi_phony];
  let CoveredBySubRegs = 1;
}
// SPR - One of the 32-bit special-purpose registers
class SPR<bits<10> num, string n> : PPCReg<n> {
  let HWEncoding{9-0} = num;
}

// FPR - One of the 32 64-bit floating-point registers
class FPR<bits<5> num, string n> : PPCReg<n> {
  let HWEncoding{4-0} = num;
}

// FPPair - A pair of 64-bit floating-point registers.
class FPPair<string n, bits<5> EvenIndex> : PPCReg<n> {
  assert !eq(EvenIndex{0}, 0), "Index should be even.";
  let HWEncoding{4-0} = EvenIndex;
  let SubRegs = [!cast<FPR>("F"#EvenIndex), !cast<FPR>("F"#!add(EvenIndex, 1))];
  let DwarfNumbers = [-1, -1];
  let SubRegIndices = [sub_fp0, sub_fp1];
}

// VF - One of the 32 64-bit floating-point subregisters of the vector
// registers (used by VSX).
class VF<bits<5> num, string n> : PPCReg<n> {
  let HWEncoding{4-0} = num;
  let HWEncoding{5} = 1;
}

// VR - One of the 32 128-bit vector registers
class VR<VF SubReg, VF SubRegH, string n> : PPCReg<n> {
  let HWEncoding{4-0} = SubReg.HWEncoding{4-0};
  let HWEncoding{5} = 0;
  let SubRegs = [SubReg, SubRegH];
  let SubRegIndices = [sub_64, sub_64_hi_phony];
}

// VSRL - One of the 32 128-bit VSX registers that overlap with the scalar
// floating-point registers.
class VSRL<FPR SubReg, FPR SubRegH, string n> : PPCReg<n> {
  let HWEncoding = SubReg.HWEncoding;
  let SubRegs = [SubReg, SubRegH];
  let SubRegIndices = [sub_64, sub_64_hi_phony];
}

// VSXReg - One of the VSX registers in the range vs32-vs63 with numbering
// and encoding to match.
class VSXReg<bits<6> num, string n> : PPCReg<n> {
  let HWEncoding{5-0} = num;
}

// CR - One of the 8 4-bit condition registers
class CR<bits<3> num, string n, list<Register> subregs> : PPCReg<n> {
  let HWEncoding{2-0} = num;
  let SubRegs = subregs;
}

// CRBIT - One of the 32 1-bit condition register fields
class CRBIT<bits<5> num, string n> : PPCReg<n> {
  let HWEncoding{4-0} = num;
}

// VSR Pairs - One of the 32 paired even-odd consecutive VSRs.
class VSRPair<bits<5> num, string n, list<Register> subregs> : PPCReg<n> {
  let HWEncoding{4-0} = num;
  let SubRegs = subregs;
}

// GP8Pair - Consecutive even-odd paired GP8.
class GP8Pair<string n, bits<5> EvenIndex> : PPCReg<n> {
  assert !eq(EvenIndex{0}, 0), "Index should be even.";
  let HWEncoding{4-0} = EvenIndex;
  let SubRegs = [!cast<GP8>("X"#EvenIndex), !cast<GP8>("X"#!add(EvenIndex, 1))];
  let DwarfNumbers = [-1, -1];
  let SubRegIndices = [sub_gp8_x0, sub_gp8_x1];
}

// General-purpose registers
foreach Index = 0-31 in {
  def R#Index : GPR<Index, "r"#Index>, DwarfRegNum<[-2, Index]>;
}

let isArtificial = 1 in {
  foreach Index = 0-31 in {
    def H#Index : GPR<-1,"">;
  }
}

// 64-bit General-purpose registers
foreach Index = 0-31 in {
  def X#Index : GP8<!cast<GPR>("R"#Index), "r"#Index>,
                    DwarfRegNum<[Index, -2]>;
}

// SPE registers
foreach Index = 0-31 in {
  def S#Index : SPE<"r"#Index, Index, [!cast<GPR>("R"#Index), !cast<GPR>("H"#Index)]>,
                    DwarfRegNum<[!add(Index, 1200), !add(Index, 1200)]>;

}


// Floating-point registers
foreach Index = 0-31 in {
  def F#Index : FPR<Index, "f"#Index>,
                DwarfRegNum<[!add(Index, 32), !add(Index, 32)]>;
}

// The FH and VFH registers have been marked as Artifical because there are no
// instructions on PowerPC that use those register classes. They only exist
// in order to ensure that the super registers (V and VSL) are covered by their
// subregisters and have correct subregister lane masks.
let isArtificial = 1 in {
  foreach Index = 0-31 in {
    def FH#Index : FPR<-1, "">;
    def VFH#Index : VF<-1, "">;
  }
}

let isAllocatable = 0, CopyCost = -1 in {
  def VFHRC : RegisterClass<"PPC", [f64], 64, (sequence "VFH%u", 0, 31)>;
  def FHRC : RegisterClass<"PPC", [f64], 64, (sequence "FH%u", 0, 31)>;
}

// Floating-point pair registers
foreach Index = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 } in {
  def Fpair#Index : FPPair<"fp"#Index, Index>;
}

// 64-bit Floating-point subregisters of Altivec registers
// Note: the register names are v0-v31 or vs32-vs63 depending on the use.
//       Custom C++ code is used to produce the correct name and encoding.
foreach Index = 0-31 in {
  def VF#Index : VF<Index, "v" #Index>,
                 DwarfRegNum<[!add(Index, 77), !add(Index, 77)]>;
}

let CoveredBySubRegs = 1 in {
// Vector registers
foreach Index = 0-31 in {
  def V#Index : VR<!cast<VF>("VF"#Index), !cast<VF>("VFH"#Index), "v"#Index>,
                DwarfRegNum<[!add(Index, 77), !add(Index, 77)]>;
}

// VSX registers
foreach Index = 0-31 in {
  def VSL#Index : VSRL<!cast<FPR>("F"#Index), !cast<FPR>("FH"#Index), "vs"#Index>,
                  DwarfRegAlias<!cast<FPR>("F"#Index)>;
}
}

// Dummy VSX registers, this defines string: "vs32"-"vs63", and is only used for
// asm printing.
foreach Index = 32-63 in {
  def VSX#Index : VSXReg<Index, "vs"#Index>;
}

let SubRegIndices = [sub_vsx0, sub_vsx1] in {
  // VSR pairs 0 - 15 (corresponding to VSRs 0 - 30 paired with 1 - 31).
  foreach Index = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 } in {
    def VSRp#!srl(Index, 1) : VSRPair<!srl(Index, 1), "vsp"#Index,
                                      [!cast<VSRL>("VSL"#Index), !cast<VSRL>("VSL"#!add(Index, 1))]>,
                              DwarfRegNum<[-1, -1]>;
  }

  // VSR pairs 16 - 31 (corresponding to VSRs 32 - 62 paired with 33 - 63).
  foreach Index = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 } in {
    def VSRp#!add(!srl(Index, 1), 16) :
      VSRPair<!add(!srl(Index, 1), 16), "vsp"#!add(Index, 32),
              [!cast<VR>("V"#Index), !cast<VR>("V"#!add(Index, 1))]>,
              DwarfRegAlias<!cast<VR>("V"#Index)>;
  }
}

// 16 paired even-odd consecutive GP8s.
foreach Index = { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 } in {
  def G8p#!srl(Index, 1) : GP8Pair<"r"#Index, Index>;
}

// The representation of r0 when treated as the constant 0.
let isConstant = true in {
def ZERO  : GPR<0, "0">,    DwarfRegAlias<R0>;
def ZERO8 : GP8<ZERO, "0">, DwarfRegAlias<X0>;
} // isConstant = true

// Representations of the frame pointer used by ISD::FRAMEADDR.
def FP   : GPR<0 /* arbitrary */, "**FRAME POINTER**">;
def FP8  : GP8<FP, "**FRAME POINTER**">;

// Representations of the base pointer used by setjmp.
def BP   : GPR<0 /* arbitrary */, "**BASE POINTER**">;
def BP8  : GP8<BP, "**BASE POINTER**">;

// Condition register bits
def CR0LT : CRBIT< 0, "0">;
def CR0GT : CRBIT< 1, "1">;
def CR0EQ : CRBIT< 2, "2">;
def CR0UN : CRBIT< 3, "3">;
def CR1LT : CRBIT< 4, "4">;
def CR1GT : CRBIT< 5, "5">;
def CR1EQ : CRBIT< 6, "6">;
def CR1UN : CRBIT< 7, "7">;
def CR2LT : CRBIT< 8, "8">;
def CR2GT : CRBIT< 9, "9">;
def CR2EQ : CRBIT<10, "10">;
def CR2UN : CRBIT<11, "11">;
def CR3LT : CRBIT<12, "12">;
def CR3GT : CRBIT<13, "13">;
def CR3EQ : CRBIT<14, "14">;
def CR3UN : CRBIT<15, "15">;
def CR4LT : CRBIT<16, "16">;
def CR4GT : CRBIT<17, "17">;
def CR4EQ : CRBIT<18, "18">;
def CR4UN : CRBIT<19, "19">;
def CR5LT : CRBIT<20, "20">;
def CR5GT : CRBIT<21, "21">;
def CR5EQ : CRBIT<22, "22">;
def CR5UN : CRBIT<23, "23">;
def CR6LT : CRBIT<24, "24">;
def CR6GT : CRBIT<25, "25">;
def CR6EQ : CRBIT<26, "26">;
def CR6UN : CRBIT<27, "27">;
def CR7LT : CRBIT<28, "28">;
def CR7GT : CRBIT<29, "29">;
def CR7EQ : CRBIT<30, "30">;
def CR7UN : CRBIT<31, "31">;

// Condition registers
let SubRegIndices = [sub_lt, sub_gt, sub_eq, sub_un] in {
def CR0 : CR<0, "cr0", [CR0LT, CR0GT, CR0EQ, CR0UN]>, DwarfRegNum<[68, 68]>;
def CR1 : CR<1, "cr1", [CR1LT, CR1GT, CR1EQ, CR1UN]>, DwarfRegNum<[69, 69]>;
def CR2 : CR<2, "cr2", [CR2LT, CR2GT, CR2EQ, CR2UN]>, DwarfRegNum<[70, 70]>;
def CR3 : CR<3, "cr3", [CR3LT, CR3GT, CR3EQ, CR3UN]>, DwarfRegNum<[71, 71]>;
def CR4 : CR<4, "cr4", [CR4LT, CR4GT, CR4EQ, CR4UN]>, DwarfRegNum<[72, 72]>;
def CR5 : CR<5, "cr5", [CR5LT, CR5GT, CR5EQ, CR5UN]>, DwarfRegNum<[73, 73]>;
def CR6 : CR<6, "cr6", [CR6LT, CR6GT, CR6EQ, CR6UN]>, DwarfRegNum<[74, 74]>;
def CR7 : CR<7, "cr7", [CR7LT, CR7GT, CR7EQ, CR7UN]>, DwarfRegNum<[75, 75]>;
}

// Link register
def LR  : SPR<8, "lr">, DwarfRegNum<[-2, 65]>;
def LR8 : SPR<8, "lr">, DwarfRegNum<[65, -2]> {
  let Aliases = [LR];
}

// Count register
def CTR  : SPR<9, "ctr">, DwarfRegNum<[-2, 66]>;
def CTR8 : SPR<9, "ctr">, DwarfRegNum<[66, -2]> {
  let Aliases = [CTR];
}

// VRsave register
def VRSAVE: SPR<256, "vrsave">, DwarfRegNum<[109]>;

// SPE extra registers
def SPEFSCR: SPR<512, "spefscr">, DwarfRegNum<[612, 112]>;

def XER: SPR<1, "xer">, DwarfRegNum<[76]>;

// Carry bit.  In the architecture this is really bit 0 of the XER register
// (which really is SPR register 1);  this is the only bit interesting to a
// compiler.
def CARRY: SPR<1, "xer">, DwarfRegNum<[76]> {
  let Aliases = [XER];
}

// FP rounding mode:  bits 30 and 31 of the FP status and control register
// This is not allocated as a normal register; it appears only in
// Uses and Defs.  The ABI says it needs to be preserved by a function,
// but this is not achieved by saving and restoring it as with
// most registers, it has to be done in code; to make this work all the
// return and call instructions are described as Uses of RM, so instructions
// that do nothing but change RM will not get deleted.
def RM: PPCReg<"**ROUNDING MODE**">;

let isAllocatable = 0 in
def GPRC32 : RegisterClass<"PPC", [i32,f32], 32, (add (sequence "H%u", 2, 12),
                                                      (sequence "H%u", 30, 13),
                                                      H31, H0, H1)>;

/// Register classes
// Allocate volatiles first
// then nonvolatiles in reverse order since stmw/lmw save from rN to r31
def GPRC : RegisterClass<"PPC", [i32,f32], 32, (add (sequence "R%u", 2, 12),
                                                    (sequence "R%u", 30, 13),
                                                    R31, R0, R1, FP, BP)> {
  // On non-Darwin PPC64 systems, R2 can be allocated, but must be restored, so
  // put it at the end of the list.
  // On AIX, CSRs are allocated starting from R31 according to:
  // https://www.ibm.com/docs/en/ssw_aix_72/assembler/assembler_pdf.pdf.
  // This also helps setting the correct `NumOfGPRsSaved' in traceback table.
  let AltOrders = [(add (sub GPRC, R2), R2),
                   (add (sequence "R%u", 2, 12),
                        (sequence "R%u", 31, 13), R0, R1, FP, BP)];
  let AltOrderSelect = [{
    return MF.getSubtarget<PPCSubtarget>().getGPRAllocationOrderIdx();
  }];
}

def G8RC : RegisterClass<"PPC", [i64], 64, (add (sequence "X%u", 2, 12),
                                                (sequence "X%u", 30, 14),
                                                X31, X13, X0, X1, FP8, BP8)> {
  // On non-Darwin PPC64 systems, R2 can be allocated, but must be restored, so
  // put it at the end of the list.
  let AltOrders = [(add (sub G8RC, X2), X2),
                   (add (sequence "X%u", 2, 12),
                        (sequence "X%u", 31, 13), X0, X1, FP8, BP8)];
  let AltOrderSelect = [{
    return MF.getSubtarget<PPCSubtarget>().getGPRAllocationOrderIdx();
  }];
}

// For some instructions r0 is special (representing the value 0 instead of
// the value in the r0 register), and we use these register subclasses to
// prevent r0 from being allocated for use by those instructions.
def GPRC_NOR0 : RegisterClass<"PPC", [i32,f32], 32, (add (sub GPRC, R0), ZERO)> {
  // On non-Darwin PPC64 systems, R2 can be allocated, but must be restored, so
  // put it at the end of the list.
  let AltOrders = [(add (sub GPRC_NOR0, R2), R2),
                   (add (sequence "R%u", 2, 12),
                        (sequence "R%u", 31, 13), R1, FP, BP, ZERO)];
  let AltOrderSelect = [{
    return MF.getSubtarget<PPCSubtarget>().getGPRAllocationOrderIdx();
  }];
}

def G8RC_NOX0 : RegisterClass<"PPC", [i64], 64, (add (sub G8RC, X0), ZERO8)> {
  // On non-Darwin PPC64 systems, R2 can be allocated, but must be restored, so
  // put it at the end of the list.
  let AltOrders = [(add (sub G8RC_NOX0, X2), X2),
                   (add (sequence "X%u", 2, 12),
                        (sequence "X%u", 31, 13), X1, FP8, BP8, ZERO8)];
  let AltOrderSelect = [{
    return MF.getSubtarget<PPCSubtarget>().getGPRAllocationOrderIdx();
  }];
}

def SPERC : RegisterClass<"PPC", [f64], 64, (add (sequence "S%u", 2, 12),
                                                (sequence "S%u", 30, 13),
                                                S31, S0, S1)>;

// Allocate volatiles first, then non-volatiles in reverse order. With the SVR4
// ABI the size of the Floating-point register save area is determined by the
// allocated non-volatile register with the lowest register number, as FP
// register N is spilled to offset 8 * (32 - N) below the back chain word of the
// previous stack frame. By allocating non-volatiles in reverse order we make
// sure that the Floating-point register save area is always as small as
// possible because there aren't any unused spill slots.
def F8RC : RegisterClass<"PPC", [f64], 64, (add (sequence "F%u", 0, 13),
                                                (sequence "F%u", 31, 14))>;
def F4RC : RegisterClass<"PPC", [f32], 32, (add F8RC)>;

// Floating point pair registers.
// Note that the type used for this register class is ppcf128. This is not
// completely correct. However, since we are not pattern matching any
// instructions for these registers and we are not register allocating or
// scheduling any of these instructions it should be safe to do this.
// The reason we didn't use the correct type (Decimal Floating Point) is that
// at the time of this implementation the correct type was not available.
def FpRC :
  RegisterClass<"PPC", [ppcf128], 128,
                (add Fpair0, Fpair2, Fpair4, Fpair6, Fpair8, Fpair10, Fpair12,
                     Fpair14, Fpair16, Fpair18, Fpair20, Fpair22, Fpair24,
                     Fpair26, Fpair28, Fpair30)> {
  let Size = 128;
}

def VRRC : RegisterClass<"PPC",
                         [v16i8,v8i16,v4i32,v2i64,v1i128,v4f32,v2f64, f128],
                         128,
                         (add V2, V3, V4, V5, V0, V1, V6, V7, V8, V9, V10, V11,
                             V12, V13, V14, V15, V16, V17, V18, V19, V31, V30,
                             V29, V28, V27, V26, V25, V24, V23, V22, V21, V20)>;

// VSX register classes (the allocation order mirrors that of the corresponding
// subregister classes).
def VSLRC : RegisterClass<"PPC", [v4i32,v4f32,v2f64,v2i64], 128,
                          (add (sequence "VSL%u", 0, 13),
                               (sequence "VSL%u", 31, 14))>;
def VSRC  : RegisterClass<"PPC", [v4i32,v4f32,v2f64,v2i64], 128,
                          (add VSLRC, VRRC)>;

// Register classes for the 64-bit "scalar" VSX subregisters.
def VFRC :  RegisterClass<"PPC", [f64], 64,
                          (add VF2, VF3, VF4, VF5, VF0, VF1, VF6, VF7,
                               VF8, VF9, VF10, VF11, VF12, VF13, VF14,
                               VF15, VF16, VF17, VF18, VF19, VF31, VF30,
                               VF29, VF28, VF27, VF26, VF25, VF24, VF23,
                               VF22, VF21, VF20)>;
def VSFRC : RegisterClass<"PPC", [f64], 64, (add F8RC, VFRC)>;

// Allow spilling GPR's into caller-saved VSR's.
def SPILLTOVSRRC : RegisterClass<"PPC", [i64, f64], 64, (add G8RC, (sub VSFRC,
				(sequence "VF%u", 31, 20),
				(sequence "F%u", 31, 14)))>;

// Register class for single precision scalars in VSX registers
def VSSRC : RegisterClass<"PPC", [f32], 32, (add VSFRC)>;

def CRBITRC : RegisterClass<"PPC", [i1], 32,
  (add CR2LT, CR2GT, CR2EQ, CR2UN,
       CR3LT, CR3GT, CR3EQ, CR3UN,
       CR4LT, CR4GT, CR4EQ, CR4UN,
       CR5LT, CR5GT, CR5EQ, CR5UN,
       CR6LT, CR6GT, CR6EQ, CR6UN,
       CR7LT, CR7GT, CR7EQ, CR7UN,
       CR1LT, CR1GT, CR1EQ, CR1UN,
       CR0LT, CR0GT, CR0EQ, CR0UN)> {
  let Size = 32;
  let AltOrders = [(sub CRBITRC, CR2LT, CR2GT, CR2EQ, CR2UN, CR3LT, CR3GT,
                        CR3EQ, CR3UN, CR4LT, CR4GT, CR4EQ, CR4UN)];
  let AltOrderSelect = [{
    return MF.getSubtarget<PPCSubtarget>().isELFv2ABI() &&
           MF.getInfo<PPCFunctionInfo>()->isNonVolatileCRDisabled();
  }];
}

def CRRC : RegisterClass<"PPC", [i32], 32,
  (add CR0, CR1, CR5, CR6,
       CR7, CR2, CR3, CR4)> {
  let AltOrders = [(sub CRRC, CR2, CR3, CR4)];
  let AltOrderSelect = [{
    return MF.getSubtarget<PPCSubtarget>().isELFv2ABI() &&
           MF.getInfo<PPCFunctionInfo>()->isNonVolatileCRDisabled();
  }];
}
// The CTR registers are not allocatable because they're used by the
// decrement-and-branch instructions, and thus need to stay live across
// multiple basic blocks.
def CTRRC : RegisterClass<"PPC", [i32], 32, (add CTR)> {
  let isAllocatable = 0;
}
def CTRRC8 : RegisterClass<"PPC", [i64], 64, (add CTR8)> {
  let isAllocatable = 0;
}

def LRRC : RegisterClass<"PPC", [i32], 32, (add LR)> {
  let isAllocatable = 0;
}
def LR8RC : RegisterClass<"PPC", [i64], 64, (add LR8)> {
  let isAllocatable = 0;
}

def VRSAVERC : RegisterClass<"PPC", [i32], 32, (add VRSAVE)>;
def CARRYRC : RegisterClass<"PPC", [i32], 32, (add CARRY, XER)> {
  let CopyCost = -1;
}

// Make AllocationOrder as similar as G8RC's to avoid potential spilling.
// Similarly, we have an AltOrder for 64-bit ELF ABI which r2 is allocated
// at last.
def G8pRC :
  RegisterClass<"PPC", [i128], 128,
                (add (sequence "G8p%u", 1, 5),
                     (sequence "G8p%u", 14, 7),
                     G8p15, G8p6, G8p0)> {
  let AltOrders = [(add (sub G8pRC, G8p1), G8p1)];
  let AltOrderSelect = [{
    return MF.getSubtarget<PPCSubtarget>().is64BitELFABI();
  }];
  let Size = 128;
}

include "PPCRegisterInfoMMA.td"
include "PPCRegisterInfoDMR.td"

//===----------------------------------------------------------------------===//
// PowerPC Operand Definitions.

// In the default PowerPC assembler syntax, registers are specified simply
// by number, so they cannot be distinguished from immediate values (without
// looking at the opcode).  This means that the default operand matching logic
// for the asm parser does not work, and we need to specify custom matchers.
// Since those can only be specified with RegisterOperand classes and not
// directly on the RegisterClass, all instructions patterns used by the asm
// parser need to use a RegisterOperand (instead of a RegisterClass) for
// all their register operands.
// For this purpose, we define one RegisterOperand for each RegisterClass,
// using the same name as the class, just in lower case.

def PPCRegGPRCAsmOperand : AsmOperandClass {
  let Name = "RegGPRC"; let PredicateMethod = "isRegNumber";
}
def gprc : RegisterOperand<GPRC> {
  let ParserMatchClass = PPCRegGPRCAsmOperand;
}
def PPCRegG8RCAsmOperand : AsmOperandClass {
  let Name = "RegG8RC"; let PredicateMethod = "isRegNumber";
}
def g8rc : RegisterOperand<G8RC> {
  let ParserMatchClass = PPCRegG8RCAsmOperand;
}
def PPCRegG8pRCAsmOperand : AsmOperandClass {
  let Name = "RegG8pRC"; let PredicateMethod = "isEvenRegNumber";
}
def g8prc : RegisterOperand<G8pRC> {
  let ParserMatchClass = PPCRegG8pRCAsmOperand;
}
def PPCRegGPRCNoR0AsmOperand : AsmOperandClass {
  let Name = "RegGPRCNoR0"; let PredicateMethod = "isRegNumber";
}
def gprc_nor0 : RegisterOperand<GPRC_NOR0> {
  let ParserMatchClass = PPCRegGPRCNoR0AsmOperand;
}
def PPCRegG8RCNoX0AsmOperand : AsmOperandClass {
  let Name = "RegG8RCNoX0"; let PredicateMethod = "isRegNumber";
}
def g8rc_nox0 : RegisterOperand<G8RC_NOX0> {
  let ParserMatchClass = PPCRegG8RCNoX0AsmOperand;
}
def PPCRegF8RCAsmOperand : AsmOperandClass {
  let Name = "RegF8RC"; let PredicateMethod = "isRegNumber";
}
def f8rc : RegisterOperand<F8RC> {
  let ParserMatchClass = PPCRegF8RCAsmOperand;
}
def PPCRegF4RCAsmOperand : AsmOperandClass {
  let Name = "RegF4RC"; let PredicateMethod = "isRegNumber";
}
def f4rc : RegisterOperand<F4RC> {
  let ParserMatchClass = PPCRegF4RCAsmOperand;
}
def PPCRegFpRCAsmOperand : AsmOperandClass {
  let Name = "RegFpRC"; let PredicateMethod = "isEvenRegNumber";
}
def fpairrc : RegisterOperand<FpRC> {
  let ParserMatchClass = PPCRegFpRCAsmOperand;
}
def PPCRegVRRCAsmOperand : AsmOperandClass {
  let Name = "RegVRRC"; let PredicateMethod = "isRegNumber";
}
def vrrc : RegisterOperand<VRRC> {
  let ParserMatchClass = PPCRegVRRCAsmOperand;
}
def PPCRegVFRCAsmOperand : AsmOperandClass {
  let Name = "RegVFRC"; let PredicateMethod = "isRegNumber";
}
def vfrc : RegisterOperand<VFRC> {
  let ParserMatchClass = PPCRegVFRCAsmOperand;
}
def PPCRegCRBITRCAsmOperand : AsmOperandClass {
  let Name = "RegCRBITRC"; let PredicateMethod = "isCRBitNumber";
}
def crbitrc : RegisterOperand<CRBITRC> {
  let ParserMatchClass = PPCRegCRBITRCAsmOperand;
}
def PPCRegCRRCAsmOperand : AsmOperandClass {
  let Name = "RegCRRC"; let PredicateMethod = "isCCRegNumber";
}
def crrc : RegisterOperand<CRRC> {
  let ParserMatchClass = PPCRegCRRCAsmOperand;
}
def PPCRegSPERCAsmOperand : AsmOperandClass {
  let Name = "RegSPERC"; let PredicateMethod = "isRegNumber";
}
def sperc : RegisterOperand<SPERC> {
  let ParserMatchClass = PPCRegSPERCAsmOperand;
}
def PPCRegSPE4RCAsmOperand : AsmOperandClass {
  let Name = "RegSPE4RC"; let PredicateMethod = "isRegNumber";
}
def spe4rc : RegisterOperand<GPRC> {
  let ParserMatchClass = PPCRegSPE4RCAsmOperand;
}

def PPCU1ImmAsmOperand : AsmOperandClass {
  let Name = "U1Imm"; let PredicateMethod = "isU1Imm";
  let RenderMethod = "addImmOperands";
}
def u1imm   : Operand<i32> {
  let PrintMethod = "printU1ImmOperand";
  let ParserMatchClass = PPCU1ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<1>";
  let OperandType = "OPERAND_IMMEDIATE";
}

def PPCU2ImmAsmOperand : AsmOperandClass {
  let Name = "U2Imm"; let PredicateMethod = "isU2Imm";
  let RenderMethod = "addImmOperands";
}
def u2imm   : Operand<i32> {
  let PrintMethod = "printU2ImmOperand";
  let ParserMatchClass = PPCU2ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<2>";
  let OperandType = "OPERAND_IMMEDIATE";
}

def PPCATBitsAsHintAsmOperand : AsmOperandClass {
  let Name = "ATBitsAsHint"; let PredicateMethod = "isATBitsAsHint";
  let RenderMethod = "addImmOperands"; // Irrelevant, predicate always fails.
}
def atimm   : Operand<i32> {
  let PrintMethod = "printATBitsAsHint";
  let ParserMatchClass = PPCATBitsAsHintAsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

def PPCU3ImmAsmOperand : AsmOperandClass {
  let Name = "U3Imm"; let PredicateMethod = "isU3Imm";
  let RenderMethod = "addImmOperands";
}
def u3imm   : Operand<i32> {
  let PrintMethod = "printU3ImmOperand";
  let ParserMatchClass = PPCU3ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<3>";
  let OperandType = "OPERAND_IMMEDIATE";
}

def PPCU4ImmAsmOperand : AsmOperandClass {
  let Name = "U4Imm"; let PredicateMethod = "isU4Imm";
  let RenderMethod = "addImmOperands";
}
def u4imm   : Operand<i32> {
  let PrintMethod = "printU4ImmOperand";
  let ParserMatchClass = PPCU4ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<4>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def PPCS5ImmAsmOperand : AsmOperandClass {
  let Name = "S5Imm"; let PredicateMethod = "isS5Imm";
  let RenderMethod = "addImmOperands";
}
def s5imm   : Operand<i32> {
  let PrintMethod = "printS5ImmOperand";
  let ParserMatchClass = PPCS5ImmAsmOperand;
  let DecoderMethod = "decodeSImmOperand<5>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def PPCU5ImmAsmOperand : AsmOperandClass {
  let Name = "U5Imm"; let PredicateMethod = "isU5Imm";
  let RenderMethod = "addImmOperands";
}
def u5imm   : Operand<i32> {
  let PrintMethod = "printU5ImmOperand";
  let ParserMatchClass = PPCU5ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<5>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def PPCU6ImmAsmOperand : AsmOperandClass {
  let Name = "U6Imm"; let PredicateMethod = "isU6Imm";
  let RenderMethod = "addImmOperands";
}
def u6imm   : Operand<i32> {
  let PrintMethod = "printU6ImmOperand";
  let ParserMatchClass = PPCU6ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<6>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def PPCU7ImmAsmOperand : AsmOperandClass {
  let Name = "U7Imm"; let PredicateMethod = "isU7Imm";
  let RenderMethod = "addImmOperands";
}
def u7imm   : Operand<i32> {
  let PrintMethod = "printU7ImmOperand";
  let ParserMatchClass = PPCU7ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<7>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def PPCU8ImmAsmOperand : AsmOperandClass {
  let Name = "U8Imm"; let PredicateMethod = "isU8Imm";
  let RenderMethod = "addImmOperands";
}
def u8imm   : Operand<i32> {
  let PrintMethod = "printU8ImmOperand";
  let ParserMatchClass = PPCU8ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<8>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def PPCU10ImmAsmOperand : AsmOperandClass {
  let Name = "U10Imm"; let PredicateMethod = "isU10Imm";
  let RenderMethod = "addImmOperands";
}
def u10imm  : Operand<i32> {
  let PrintMethod = "printU10ImmOperand";
  let ParserMatchClass = PPCU10ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<10>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def PPCU12ImmAsmOperand : AsmOperandClass {
  let Name = "U12Imm"; let PredicateMethod = "isU12Imm";
  let RenderMethod = "addImmOperands";
}
def u12imm  : Operand<i32> {
  let PrintMethod = "printU12ImmOperand";
  let ParserMatchClass = PPCU12ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<12>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def PPCS16ImmAsmOperand : AsmOperandClass {
  let Name = "S16Imm"; let PredicateMethod = "isS16Imm";
  let RenderMethod = "addS16ImmOperands";
}
def s16imm  : Operand<i32> {
  let PrintMethod = "printS16ImmOperand";
  let EncoderMethod = "getImm16Encoding";
  let ParserMatchClass = PPCS16ImmAsmOperand;
  let DecoderMethod = "decodeSImmOperand<16>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def PPCU16ImmAsmOperand : AsmOperandClass {
  let Name = "U16Imm"; let PredicateMethod = "isU16Imm";
  let RenderMethod = "addU16ImmOperands";
}
def u16imm  : Operand<i32> {
  let PrintMethod = "printU16ImmOperand";
  let EncoderMethod = "getImm16Encoding";
  let ParserMatchClass = PPCU16ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<16>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def PPCS17ImmAsmOperand : AsmOperandClass {
  let Name = "S17Imm"; let PredicateMethod = "isS17Imm";
  let RenderMethod = "addS16ImmOperands";
}
def s17imm  : Operand<i32> {
  // This operand type is used for addis/lis to allow the assembler parser
  // to accept immediates in the range -65536..65535 for compatibility with
  // the GNU assembler.  The operand is treated as 16-bit otherwise.
  let PrintMethod = "printS16ImmOperand";
  let EncoderMethod = "getImm16Encoding";
  let ParserMatchClass = PPCS17ImmAsmOperand;
  let DecoderMethod = "decodeSImmOperand<16>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def PPCS34ImmAsmOperand : AsmOperandClass {
  let Name = "S34Imm";
  let PredicateMethod = "isS34Imm";
  let RenderMethod = "addImmOperands";
}
def s34imm : Operand<i64> {
  let PrintMethod = "printS34ImmOperand";
  let EncoderMethod = "getImm34EncodingNoPCRel";
  let ParserMatchClass = PPCS34ImmAsmOperand;
  let DecoderMethod = "decodeSImmOperand<34>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def s34imm_pcrel : Operand<i64> {
  let PrintMethod = "printS34ImmOperand";
  let EncoderMethod = "getImm34EncodingPCRel";
  let ParserMatchClass = PPCS34ImmAsmOperand;
  let DecoderMethod = "decodeSImmOperand<34>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def PPCImmZeroAsmOperand : AsmOperandClass {
  let Name = "ImmZero";
  let PredicateMethod = "isImmZero";
  let RenderMethod = "addImmOperands";
}
def immZero : Operand<i32> {
  let PrintMethod = "printImmZeroOperand";
  let ParserMatchClass = PPCImmZeroAsmOperand;
  let DecoderMethod = "decodeImmZeroOperand";
  let OperandType = "OPERAND_IMMEDIATE";
}

def fpimm0 : PatLeaf<(fpimm), [{ return N->isExactlyValue(+0.0); }]>;
def fpimm0neg : PatLeaf<(fpimm), [{return N->isExactlyValue(-0.0);}]>;

def PPCDirectBrAsmOperand : AsmOperandClass {
  let Name = "DirectBr"; let PredicateMethod = "isDirectBr";
  let RenderMethod = "addBranchTargetOperands";
}
def directbrtarget : Operand<OtherVT> {
  let PrintMethod = "printBranchOperand";
  let EncoderMethod = "getDirectBrEncoding";
  let DecoderMethod = "decodeDirectBrTarget";
  let ParserMatchClass = PPCDirectBrAsmOperand;
  let OperandType = "OPERAND_PCREL";
}
def absdirectbrtarget : Operand<OtherVT> {
  let PrintMethod = "printAbsBranchOperand";
  let EncoderMethod = "getAbsDirectBrEncoding";
  let DecoderMethod = "decodeDirectBrTarget";
  let ParserMatchClass = PPCDirectBrAsmOperand;
}
def PPCCondBrAsmOperand : AsmOperandClass {
  let Name = "CondBr"; let PredicateMethod = "isCondBr";
  let RenderMethod = "addBranchTargetOperands";
}
def condbrtarget : Operand<OtherVT> {
  let PrintMethod = "printBranchOperand";
  let EncoderMethod = "getCondBrEncoding";
  let DecoderMethod = "decodeCondBrTarget";
  let ParserMatchClass = PPCCondBrAsmOperand;
  let OperandType = "OPERAND_PCREL";
}
def abscondbrtarget : Operand<OtherVT> {
  let PrintMethod = "printAbsBranchOperand";
  let EncoderMethod = "getAbsCondBrEncoding";
  let DecoderMethod = "decodeCondBrTarget";
  let ParserMatchClass = PPCCondBrAsmOperand;
}
def calltarget : Operand<iPTR> {
  let PrintMethod = "printBranchOperand";
  let EncoderMethod = "getDirectBrEncoding";
  let DecoderMethod = "decodeDirectBrTarget";
  let ParserMatchClass = PPCDirectBrAsmOperand;
  let OperandType = "OPERAND_PCREL";
}
def abscalltarget : Operand<iPTR> {
  let PrintMethod = "printAbsBranchOperand";
  let EncoderMethod = "getAbsDirectBrEncoding";
  let DecoderMethod = "decodeDirectBrTarget";
  let ParserMatchClass = PPCDirectBrAsmOperand;
}
def PPCCRBitMaskOperand : AsmOperandClass {
 let Name = "CRBitMask"; let PredicateMethod = "isCRBitMask";
}
def crbitm: Operand<i8> {
  let PrintMethod = "printcrbitm";
  let EncoderMethod = "get_crbitm_encoding";
  let DecoderMethod = "decodeCRBitMOperand";
  let ParserMatchClass = PPCCRBitMaskOperand;
}
// Address operands
// A version of ptr_rc which excludes R0 (or X0 in 64-bit mode).
def PPCRegGxRCNoR0Operand : AsmOperandClass {
  let Name = "RegGxRCNoR0"; let PredicateMethod = "isRegNumber";
}
def ptr_rc_nor0 : Operand<iPTR>, PointerLikeRegClass<1> {
  let ParserMatchClass = PPCRegGxRCNoR0Operand;
}

// New addressing modes with 34 bit immediates.
def PPCDispRI34Operand : AsmOperandClass {
  let Name = "DispRI34"; let PredicateMethod = "isS34Imm";
  let RenderMethod = "addImmOperands";
}
def dispRI34 : Operand<iPTR> {
  let ParserMatchClass = PPCDispRI34Operand;
  let EncoderMethod = "getDispRI34Encoding";
  let DecoderMethod = "decodeSImmOperand<34>";
}
def dispRI34_pcrel : Operand<iPTR> {
  let ParserMatchClass = PPCDispRI34Operand;
  let EncoderMethod = "getDispRI34PCRelEncoding";
  let DecoderMethod = "decodeSImmOperand<34>";
}
def memri34 : Operand<iPTR> { // memri, imm is a 34-bit value.
  let PrintMethod = "printMemRegImm34";
  let MIOperandInfo = (ops dispRI34:$imm, ptr_rc_nor0:$reg);
}
// memri, imm is a 34-bit value for pc-relative instructions where
// base register is set to zero.
def memri34_pcrel : Operand<iPTR> { // memri, imm is a 34-bit value.
  let PrintMethod = "printMemRegImm34PCRel";
  let MIOperandInfo = (ops dispRI34_pcrel:$imm, immZero:$reg);
}

// A version of ptr_rc usable with the asm parser.
def PPCRegGxRCOperand : AsmOperandClass {
  let Name = "RegGxRC"; let PredicateMethod = "isRegNumber";
}
def ptr_rc_idx : Operand<iPTR>, PointerLikeRegClass<0> {
  let ParserMatchClass = PPCRegGxRCOperand;
}

def PPCDispRIOperand : AsmOperandClass {
 let Name = "DispRI"; let PredicateMethod = "isS16Imm";
 let RenderMethod = "addS16ImmOperands";
}
def dispRI : Operand<iPTR> {
  let ParserMatchClass = PPCDispRIOperand;
  let EncoderMethod = "getDispRIEncoding";
}
def PPCDispRIXOperand : AsmOperandClass {
 let Name = "DispRIX"; let PredicateMethod = "isS16ImmX4";
 let RenderMethod = "addS16ImmOperands";
}
def dispRIX : Operand<iPTR> {
  let ParserMatchClass = PPCDispRIXOperand;
  let EncoderMethod = "getDispRIXEncoding";
  let DecoderMethod = "decodeDispRIXOperand";
}
def PPCDispRIHashOperand : AsmOperandClass {
  let Name = "DispRIHash"; let PredicateMethod = "isHashImmX8";
  let RenderMethod = "addImmOperands";
}
def dispRIHash : Operand<iPTR> {
  let ParserMatchClass = PPCDispRIHashOperand;
  let EncoderMethod = "getDispRIHashEncoding";
  let DecoderMethod = "decodeDispRIHashOperand";
}
def PPCDispRIX16Operand : AsmOperandClass {
 let Name = "DispRIX16"; let PredicateMethod = "isS16ImmX16";
 let RenderMethod = "addS16ImmOperands";
}
def dispRIX16 : Operand<iPTR> {
  let ParserMatchClass = PPCDispRIX16Operand;
  let EncoderMethod = "getDispRIX16Encoding";
  let DecoderMethod = "decodeDispRIX16Operand";

}
def PPCDispSPE8Operand : AsmOperandClass {
 let Name = "DispSPE8"; let PredicateMethod = "isU8ImmX8";
 let RenderMethod = "addImmOperands";
}
def dispSPE8 : Operand<iPTR> {
  let ParserMatchClass = PPCDispSPE8Operand;
  let DecoderMethod = "decodeDispSPE8Operand";
  let EncoderMethod = "getDispSPE8Encoding";
}
def PPCDispSPE4Operand : AsmOperandClass {
 let Name = "DispSPE4"; let PredicateMethod = "isU7ImmX4";
 let RenderMethod = "addImmOperands";
}
def dispSPE4 : Operand<iPTR> {
  let ParserMatchClass = PPCDispSPE4Operand;
  let DecoderMethod = "decodeDispSPE4Operand";
  let EncoderMethod = "getDispSPE4Encoding";
}
def PPCDispSPE2Operand : AsmOperandClass {
 let Name = "DispSPE2"; let PredicateMethod = "isU6ImmX2";
 let RenderMethod = "addImmOperands";
}
def dispSPE2 : Operand<iPTR> {
  let ParserMatchClass = PPCDispSPE2Operand;
  let DecoderMethod = "decodeDispSPE2Operand";
  let EncoderMethod = "getDispSPE2Encoding";
}

def memri : Operand<iPTR> {
  let PrintMethod = "printMemRegImm";
  let MIOperandInfo = (ops dispRI:$imm, ptr_rc_nor0:$reg);
  let OperandType = "OPERAND_MEMORY";
}
def memrr : Operand<iPTR> {
  let PrintMethod = "printMemRegReg";
  let MIOperandInfo = (ops ptr_rc_nor0:$ptrreg, ptr_rc_idx:$offreg);
  let OperandType = "OPERAND_MEMORY";
}
def memrix : Operand<iPTR> {   // memri where the imm is 4-aligned.
  let PrintMethod = "printMemRegImm";
  let MIOperandInfo = (ops dispRIX:$imm, ptr_rc_nor0:$reg);
  let OperandType = "OPERAND_MEMORY";
}
def memrihash : Operand<iPTR> {
  // memrihash 8-aligned for ROP Protection Instructions.
  let PrintMethod = "printMemRegImmHash";
  let MIOperandInfo = (ops dispRIHash:$imm, ptr_rc_nor0:$reg);
  let OperandType = "OPERAND_MEMORY";
}
def memrix16 : Operand<iPTR> { // memri, imm is 16-aligned, 12-bit, Inst{16:27}
  let PrintMethod = "printMemRegImm";
  let MIOperandInfo = (ops dispRIX16:$imm, ptr_rc_nor0:$reg);
  let OperandType = "OPERAND_MEMORY";
}
def spe8dis : Operand<iPTR> {   // SPE displacement where the imm is 8-aligned.
  let PrintMethod = "printMemRegImm";
  let MIOperandInfo = (ops dispSPE8:$imm, ptr_rc_nor0:$reg);
  let OperandType = "OPERAND_MEMORY";
}
def spe4dis : Operand<iPTR> {   // SPE displacement where the imm is 4-aligned.
  let PrintMethod = "printMemRegImm";
  let MIOperandInfo = (ops dispSPE4:$imm, ptr_rc_nor0:$reg);
  let OperandType = "OPERAND_MEMORY";
}
def spe2dis : Operand<iPTR> {   // SPE displacement where the imm is 2-aligned.
  let PrintMethod = "printMemRegImm";
  let MIOperandInfo = (ops dispSPE2:$imm, ptr_rc_nor0:$reg);
  let OperandType = "OPERAND_MEMORY";
}

// A single-register address. This is used with the SjLj
// pseudo-instructions which translates to LD/LWZ.  These instructions requires
// G8RC_NOX0 registers.
def memr : Operand<iPTR> {
  let MIOperandInfo = (ops ptr_rc_nor0:$ptrreg);
  let OperandType = "OPERAND_MEMORY";
}
def PPCTLSRegOperand : AsmOperandClass {
  let Name = "TLSReg"; let PredicateMethod = "isTLSReg";
  let RenderMethod = "addTLSRegOperands";
}
def tlsreg32 : Operand<i32> {
  let EncoderMethod = "getTLSRegEncoding";
  let ParserMatchClass = PPCTLSRegOperand;
}
def tlsgd32 : Operand<i32> {}
def tlscall32 : Operand<i32> {
  let PrintMethod = "printTLSCall";
  let MIOperandInfo = (ops calltarget:$func, tlsgd32:$sym);
  let EncoderMethod = "getTLSCallEncoding";
}

// PowerPC Predicate operand.
def pred : Operand<OtherVT> {
  let PrintMethod = "printPredicateOperand";
  let MIOperandInfo = (ops i32imm:$bibo, crrc:$reg);
}

def PPCRegVSRCAsmOperand : AsmOperandClass {
  let Name = "RegVSRC"; let PredicateMethod = "isVSRegNumber";
}
def vsrc : RegisterOperand<VSRC> {
  let ParserMatchClass = PPCRegVSRCAsmOperand;
}

def PPCRegVSFRCAsmOperand : AsmOperandClass {
  let Name = "RegVSFRC"; let PredicateMethod = "isVSRegNumber";
}
def vsfrc : RegisterOperand<VSFRC> {
  let ParserMatchClass = PPCRegVSFRCAsmOperand;
}

def PPCRegVSSRCAsmOperand : AsmOperandClass {
  let Name = "RegVSSRC"; let PredicateMethod = "isVSRegNumber";
}
def vssrc : RegisterOperand<VSSRC> {
  let ParserMatchClass = PPCRegVSSRCAsmOperand;
}

def PPCRegSPILLTOVSRRCAsmOperand : AsmOperandClass {
  let Name = "RegSPILLTOVSRRC"; let PredicateMethod = "isVSRegNumber";
}

def spilltovsrrc : RegisterOperand<SPILLTOVSRRC> {
  let ParserMatchClass = PPCRegSPILLTOVSRRCAsmOperand;
}

def PPCRegVSRpRCAsmOperand : AsmOperandClass {
  let Name = "RegVSRpRC"; let PredicateMethod = "isVSRpEvenRegNumber";
}

def vsrprc : RegisterOperand<VSRpRC> {
  let ParserMatchClass = PPCRegVSRpRCAsmOperand;
}

def PPCRegVSRpEvenRCAsmOperand : AsmOperandClass {
  let Name = "RegVSRpEvenRC"; let PredicateMethod = "isVSRpEvenRegNumber";
}

def vsrpevenrc : RegisterOperand<VSRpRC> {
  let ParserMatchClass = PPCRegVSRpEvenRCAsmOperand;
  let EncoderMethod = "getVSRpEvenEncoding";
  let DecoderMethod = "decodeVSRpEvenOperands";
}

def PPCRegACCRCAsmOperand : AsmOperandClass {
  let Name = "RegACCRC"; let PredicateMethod = "isACCRegNumber";
}

def acc : RegisterOperand<ACCRC> {
  let ParserMatchClass = PPCRegACCRCAsmOperand;
}

def uacc : RegisterOperand<UACCRC> {
  let ParserMatchClass = PPCRegACCRCAsmOperand;
}

// DMR Register Operands
def PPCRegDMRROWRCAsmOperand : AsmOperandClass {
  let Name = "RegDMRROWRC";
  let PredicateMethod = "isDMRROWRegNumber";
}

def dmrrow : RegisterOperand<DMRROWRC> {
  let ParserMatchClass = PPCRegDMRROWRCAsmOperand;
}

def PPCRegDMRROWpRCAsmOperand : AsmOperandClass {
  let Name = "RegDMRROWpRC";
  let PredicateMethod = "isDMRROWpRegNumber";
}

def dmrrowp : RegisterOperand<DMRROWpRC> {
  let ParserMatchClass = PPCRegDMRROWpRCAsmOperand;
}

def wacc : RegisterOperand<WACCRC> {
  let ParserMatchClass = PPCRegACCRCAsmOperand;
}

def wacc_hi : RegisterOperand<WACC_HIRC> {
  let ParserMatchClass = PPCRegACCRCAsmOperand;
}

def PPCRegDMRRCAsmOperand : AsmOperandClass {
  let Name = "RegDMRRC";
  let PredicateMethod = "isDMRRegNumber";
}

def dmr : RegisterOperand<DMRRC> {
  let ParserMatchClass = PPCRegDMRRCAsmOperand;
}

def PPCRegDMRpRCAsmOperand : AsmOperandClass {
  let Name = "RegDMRpRC";
  let PredicateMethod = "isDMRpRegNumber";
}

def dmrp : RegisterOperand<DMRpRC> {
  let ParserMatchClass = PPCRegDMRpRCAsmOperand;
}
