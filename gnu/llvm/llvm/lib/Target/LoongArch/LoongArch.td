//===-- LoongArch.td - Describe the LoongArch Target -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// LoongArch subtarget features and instruction predicates.
//===----------------------------------------------------------------------===//

// LoongArch is divided into two versions, the 32-bit version (LA32) and the
// 64-bit version (LA64).
def Feature64Bit
    : SubtargetFeature<"64bit", "HasLA64", "true",
                       "LA64 Basic Integer and Privilege Instruction Set">;
def Feature32Bit
    : SubtargetFeature<"32bit", "HasLA32", "true",
                       "LA32 Basic Integer and Privilege Instruction Set">;
def IsLA64
    : Predicate<"Subtarget->is64Bit()">,
      AssemblerPredicate<(all_of Feature64Bit),
                         "LA64 Basic Integer and Privilege Instruction Set">;
def IsLA32
    : Predicate<"!Subtarget->is64Bit()">,
      AssemblerPredicate<(all_of(not Feature64Bit)),
                         "LA32 Basic Integer and Privilege Instruction Set">;

defvar LA32 = DefaultMode;
def LA64 : HwMode<"+64bit", [IsLA64]>;

// Single Precision floating point
def FeatureBasicF
    : SubtargetFeature<"f", "HasBasicF", "true",
                       "'F' (Single-Precision Floating-Point)">;
def HasBasicF : Predicate<"Subtarget->hasBasicF()">;

// Double Precision floating point
def FeatureBasicD
    : SubtargetFeature<"d", "HasBasicD", "true",
                       "'D' (Double-Precision Floating-Point)",
                       [FeatureBasicF]>;
def HasBasicD : Predicate<"Subtarget->hasBasicD()">;

// Loongson SIMD eXtension (LSX)
def FeatureExtLSX
    : SubtargetFeature<"lsx", "HasExtLSX", "true",
                       "'LSX' (Loongson SIMD Extension)", [FeatureBasicD]>;
def HasExtLSX : Predicate<"Subtarget->hasExtLSX()">;

// Loongson Advanced SIMD eXtension (LASX)
def FeatureExtLASX
    : SubtargetFeature<"lasx", "HasExtLASX", "true",
                       "'LASX' (Loongson Advanced SIMD Extension)",
                       [FeatureExtLSX]>;
def HasExtLASX : Predicate<"Subtarget->hasExtLASX()">;

// Loongson VirtualiZation (LVZ)
def FeatureExtLVZ
    : SubtargetFeature<"lvz", "HasExtLVZ", "true",
                       "'LVZ' (Loongson Virtualization Extension)">;
def HasExtLVZ : Predicate<"Subtarget->hasExtLVZ()">;

// Loongson Binary Translation (LBT)
def FeatureExtLBT
    : SubtargetFeature<"lbt", "HasExtLBT", "true",
                       "'LBT' (Loongson Binary Translation Extension)">;
def HasExtLBT : Predicate<"Subtarget->hasExtLBT()">;

// Expand la.global as la.pcrel
def LaGlobalWithPcrel
    : SubtargetFeature<"la-global-with-pcrel", "HasLaGlobalWithPcrel", "true",
                       "Expand la.global as la.pcrel">;
def HasLaGlobalWithPcrel
    : Predicate<"Subtarget->hasLaGlobalWithPcrel()">,
      AssemblerPredicate<(all_of LaGlobalWithPcrel),
                         "Expand la.global as la.pcrel">;

// Expand la.global as la.abs
def LaGlobalWithAbs
    : SubtargetFeature<"la-global-with-abs", "HasLaGlobalWithAbs", "true",
                       "Expand la.global as la.abs">;
def HasLaGlobalWithAbs
    : Predicate<"Subtarget->hasLaGlobalWithAbs()">,
      AssemblerPredicate<(all_of LaGlobalWithAbs),
                         "Expand la.global as la.abs">;

// Expand la.local as la.abs
def LaLocalWithAbs
    : SubtargetFeature<"la-local-with-abs", "HasLaLocalWithAbs", "true",
                       "Expand la.local as la.abs">;
def HasLaLocalWithAbs
    : Predicate<"Subtarget->hasLaLocalWithAbs()">,
      AssemblerPredicate<(all_of LaLocalWithAbs),
                         "Expand la.local as la.abs">;

// Unaligned memory access
def FeatureUAL
    : SubtargetFeature<"ual", "HasUAL", "true",
                       "Allow memory accesses to be unaligned">;

def FeatureRelax
    : SubtargetFeature<"relax", "HasLinkerRelax", "true",
                       "Enable Linker relaxation">;

// Floating point approximation operation
def FeatureFrecipe
    : SubtargetFeature<"frecipe", "HasFrecipe", "true",
                       "Support frecipe.{s/d} and frsqrte.{s/d} instructions.">;
def HasFrecipe : Predicate<"Subtarget->hasFrecipe()">;

def TunePreferWInst
    : SubtargetFeature<"prefer-w-inst", "PreferWInst", "true",
                       "Prefer instructions with W suffix">;

//===----------------------------------------------------------------------===//
// Registers, instruction descriptions ...
//===----------------------------------------------------------------------===//

include "LoongArchRegisterInfo.td"
include "LoongArchCallingConv.td"
include "LoongArchInstrInfo.td"

//===----------------------------------------------------------------------===//
// LoongArch processors supported.
//===----------------------------------------------------------------------===//

def : ProcessorModel<"generic-la32", NoSchedModel, [Feature32Bit]>;
def : ProcessorModel<"generic-la64", NoSchedModel, [Feature64Bit, FeatureUAL]>;

// Generic 64-bit processor with double-precision floating-point support.
def : ProcessorModel<"loongarch64", NoSchedModel, [Feature64Bit,
                                                   FeatureUAL,
                                                   FeatureBasicD]>;

// Support generic for compatibility with other targets. The triple will be used
// to change to the appropriate la32/la64 version.
def : ProcessorModel<"generic", NoSchedModel, []>;

def : ProcessorModel<"la464", NoSchedModel, [Feature64Bit,
                                             FeatureUAL,
                                             FeatureExtLASX,
                                             FeatureExtLVZ,
                                             FeatureExtLBT]>;

def : ProcessorModel<"la664", NoSchedModel, [Feature64Bit,
                                             FeatureUAL,
                                             FeatureExtLASX,
                                             FeatureExtLVZ,
                                             FeatureExtLBT,
                                             FeatureFrecipe]>;

//===----------------------------------------------------------------------===//
// Define the LoongArch target.
//===----------------------------------------------------------------------===//

def LoongArchInstrInfo : InstrInfo {
  let guessInstructionProperties = 0;
}

def LoongArchAsmParser : AsmParser {
  let ShouldEmitMatchRegisterAltName = 1;
  let AllowDuplicateRegisterNames = 1;
}

def LoongArchAsmParserVariant : AsmParserVariant {
  int Variant = 0;
  // Recognize hard coded registers.
  string RegisterPrefix = "$";
}

def LoongArchAsmWriter : AsmWriter {
  int PassSubtarget = 1;
}

def LoongArch : Target {
  let InstructionSet = LoongArchInstrInfo;
  let AssemblyParsers = [LoongArchAsmParser];
  let AssemblyParserVariants = [LoongArchAsmParserVariant];
  let AssemblyWriters = [LoongArchAsmWriter];
  let AllowRegisterRenaming = 1;
}
