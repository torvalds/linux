//===-- Mips.td - Describe the Mips Target Machine ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This is the top level entry point for the Mips target.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

// The overall idea of the PredicateControl class is to chop the Predicates list
// into subsets that are usually overridden independently. This allows
// subclasses to partially override the predicates of their superclasses without
// having to re-add all the existing predicates.
class PredicateControl {
  // Predicates for the encoding scheme in use such as HasStdEnc
  list<Predicate> EncodingPredicates = [];
  // Predicates for the GPR size such as IsGP64bit
  list<Predicate> GPRPredicates = [];
  // Predicates for the PTR size such as IsPTR64bit
  list<Predicate> PTRPredicates = [];
  // Predicates for a symbol's size such as hasSym32.
  list<Predicate> SYMPredicates = [];
  // Predicates for the FGR size and layout such as IsFP64bit
  list<Predicate> FGRPredicates = [];
  // Predicates for the instruction group membership such as ISA's.
  list<Predicate> InsnPredicates = [];
  // Predicate for the ASE that an instruction belongs to.
  list<Predicate> ASEPredicate = [];
  // Predicate for marking the instruction as usable in hard-float mode only.
  list<Predicate> HardFloatPredicate = [];
  // Predicates for anything else
  list<Predicate> AdditionalPredicates = [];
  list<Predicate> Predicates = !listconcat(EncodingPredicates,
                                           GPRPredicates,
                                           PTRPredicates,
                                           SYMPredicates,
                                           FGRPredicates,
                                           InsnPredicates,
                                           HardFloatPredicate,
                                           ASEPredicate,
                                           AdditionalPredicates);
}

// Like Requires<> but for the AdditionalPredicates list
class AdditionalRequires<list<Predicate> preds> {
  list<Predicate> AdditionalPredicates = preds;
}

//===----------------------------------------------------------------------===//
// Mips Subtarget features                                                    //
//===----------------------------------------------------------------------===//

def FeatureNoABICalls  : SubtargetFeature<"noabicalls", "NoABICalls", "true",
                                "Disable SVR4-style position-independent code">;
def FeaturePTR64Bit    : SubtargetFeature<"ptr64", "IsPTR64bit", "true",
                                "Pointers are 64-bit wide">;
def FeatureGP64Bit     : SubtargetFeature<"gp64", "IsGP64bit", "true",
                                "General Purpose Registers are 64-bit wide">;
def FeatureFP64Bit     : SubtargetFeature<"fp64", "IsFP64bit", "true",
                                "Support 64-bit FP registers">;
def FeatureFPXX        : SubtargetFeature<"fpxx", "IsFPXX", "true",
                                "Support for FPXX">;
def FeatureNaN2008     : SubtargetFeature<"nan2008", "IsNaN2008bit", "true",
                                "IEEE 754-2008 NaN encoding">;
def FeatureAbs2008     : SubtargetFeature<"abs2008", "Abs2008", "true",
                                          "Disable IEEE 754-2008 abs.fmt mode">;
def FeatureSingleFloat : SubtargetFeature<"single-float", "IsSingleFloat",
                                "true", "Only supports single precision float">;
def FeatureSoftFloat   : SubtargetFeature<"soft-float", "IsSoftFloat", "true",
                                "Does not support floating point instructions">;
def FeatureNoOddSPReg  : SubtargetFeature<"nooddspreg", "UseOddSPReg", "false",
                              "Disable odd numbered single-precision "
                              "registers">;
def FeatureVFPU        : SubtargetFeature<"vfpu", "HasVFPU",
                                "true", "Enable vector FPU instructions">;
def FeatureMips1       : SubtargetFeature<"mips1", "MipsArchVersion", "Mips1",
                                "Mips I ISA Support [highly experimental]">;
def FeatureMips2       : SubtargetFeature<"mips2", "MipsArchVersion", "Mips2",
                                "Mips II ISA Support [highly experimental]",
                                [FeatureMips1]>;
def FeatureMips3_32    : SubtargetFeature<"mips3_32", "HasMips3_32", "true",
                                "Subset of MIPS-III that is also in MIPS32 "
                                "[highly experimental]">;
def FeatureMips3_32r2  : SubtargetFeature<"mips3_32r2", "HasMips3_32r2", "true",
                                "Subset of MIPS-III that is also in MIPS32r2 "
                                "[highly experimental]">;
def FeatureMips3       : SubtargetFeature<"mips3", "MipsArchVersion", "Mips3",
                                "MIPS III ISA Support [highly experimental]",
                                [FeatureMips2, FeatureMips3_32,
                                 FeatureMips3_32r2, FeatureGP64Bit,
                                 FeatureFP64Bit]>;
def FeatureMips4_32    : SubtargetFeature<"mips4_32", "HasMips4_32", "true",
                                "Subset of MIPS-IV that is also in MIPS32 "
                                "[highly experimental]">;
def FeatureMips4_32r2  : SubtargetFeature<"mips4_32r2", "HasMips4_32r2", "true",
                                "Subset of MIPS-IV that is also in MIPS32r2 "
                                "[highly experimental]">;
def FeatureMips4       : SubtargetFeature<"mips4", "MipsArchVersion",
                                "Mips4", "MIPS IV ISA Support",
                                [FeatureMips3, FeatureMips4_32,
                                 FeatureMips4_32r2]>;
def FeatureMips5_32r2  : SubtargetFeature<"mips5_32r2", "HasMips5_32r2", "true",
                                "Subset of MIPS-V that is also in MIPS32r2 "
                                "[highly experimental]">;
def FeatureMips5       : SubtargetFeature<"mips5", "MipsArchVersion", "Mips5",
                                "MIPS V ISA Support [highly experimental]",
                                [FeatureMips4, FeatureMips5_32r2]>;
def FeatureMips32      : SubtargetFeature<"mips32", "MipsArchVersion", "Mips32",
                                "Mips32 ISA Support",
                                [FeatureMips2, FeatureMips3_32,
                                 FeatureMips4_32]>;
def FeatureMips32r2    : SubtargetFeature<"mips32r2", "MipsArchVersion",
                                "Mips32r2", "Mips32r2 ISA Support",
                                [FeatureMips3_32r2, FeatureMips4_32r2,
                                 FeatureMips5_32r2, FeatureMips32]>;
def FeatureMips32r3    : SubtargetFeature<"mips32r3", "MipsArchVersion",
                                "Mips32r3", "Mips32r3 ISA Support",
                                [FeatureMips32r2]>;
def FeatureMips32r5    : SubtargetFeature<"mips32r5", "MipsArchVersion",
                                "Mips32r5", "Mips32r5 ISA Support",
                                [FeatureMips32r3]>;
def FeatureMips32r6    : SubtargetFeature<"mips32r6", "MipsArchVersion",
                                "Mips32r6",
                                "Mips32r6 ISA Support [experimental]",
                                [FeatureMips32r5, FeatureFP64Bit,
                                 FeatureNaN2008, FeatureAbs2008]>;
def FeatureMips64      : SubtargetFeature<"mips64", "MipsArchVersion",
                                "Mips64", "Mips64 ISA Support",
                                [FeatureMips5, FeatureMips32]>;
def FeatureMips64r2    : SubtargetFeature<"mips64r2", "MipsArchVersion",
                                "Mips64r2", "Mips64r2 ISA Support",
                                [FeatureMips64, FeatureMips32r2]>;
def FeatureMips64r3    : SubtargetFeature<"mips64r3", "MipsArchVersion",
                                "Mips64r3", "Mips64r3 ISA Support",
                                [FeatureMips64r2, FeatureMips32r3]>;
def FeatureMips64r5    : SubtargetFeature<"mips64r5", "MipsArchVersion",
                                "Mips64r5", "Mips64r5 ISA Support",
                                [FeatureMips64r3, FeatureMips32r5]>;
def FeatureMips64r6    : SubtargetFeature<"mips64r6", "MipsArchVersion",
                                "Mips64r6",
                                "Mips64r6 ISA Support [experimental]",
                                [FeatureMips32r6, FeatureMips64r5,
                                 FeatureNaN2008, FeatureAbs2008]>;
def FeatureSym32       : SubtargetFeature<"sym32", "HasSym32", "true",
                                          "Symbols are 32 bit on Mips64">;

def FeatureMips16  : SubtargetFeature<"mips16", "InMips16Mode", "true",
                                      "Mips16 mode">;

def FeatureDSP : SubtargetFeature<"dsp", "HasDSP", "true", "Mips DSP ASE">;
def FeatureDSPR2 : SubtargetFeature<"dspr2", "HasDSPR2", "true",
                                    "Mips DSP-R2 ASE", [FeatureDSP]>;
def FeatureDSPR3
    : SubtargetFeature<"dspr3", "HasDSPR3", "true", "Mips DSP-R3 ASE",
                       [ FeatureDSP, FeatureDSPR2 ]>;

def FeatureMips3D : SubtargetFeature<"mips3d", "Has3D", "true", "Mips 3D ASE">;

def FeatureMSA : SubtargetFeature<"msa", "HasMSA", "true", "Mips MSA ASE">;

def FeatureEVA : SubtargetFeature<"eva", "HasEVA", "true", "Mips EVA ASE">;

def FeatureCRC : SubtargetFeature<"crc", "HasCRC", "true", "Mips R6 CRC ASE">;

def FeatureVirt : SubtargetFeature<"virt", "HasVirt", "true",
                                   "Mips Virtualization ASE">;

def FeatureGINV : SubtargetFeature<"ginv", "HasGINV", "true",
                                   "Mips Global Invalidate ASE">;

def FeatureMicroMips  : SubtargetFeature<"micromips", "InMicroMipsMode", "true",
                                         "microMips mode">;

def FeatureCnMips : SubtargetFeature<"cnmips", "HasCnMips",
                                     "true", "Octeon cnMIPS Support",
                                     [FeatureMips64r2]>;

def FeatureCnMipsP : SubtargetFeature<"cnmipsp", "HasCnMipsP",
                                      "true", "Octeon+ cnMIPS Support",
                                      [FeatureCnMips]>;

def FeatureUseTCCInDIV : SubtargetFeature<
                               "use-tcc-in-div",
                               "UseTCCInDIV", "false",
                               "Force the assembler to use trapping">;

def FeatureNoMadd4
    : SubtargetFeature<"nomadd4", "DisableMadd4", "true",
                       "Disable 4-operand madd.fmt and related instructions">;

def FeatureMT : SubtargetFeature<"mt", "HasMT", "true", "Mips MT ASE">;

def FeatureLongCalls : SubtargetFeature<"long-calls", "UseLongCalls", "true",
                                        "Disable use of the jal instruction">;

def FeatureXGOT
    : SubtargetFeature<"xgot", "UseXGOT", "true", "Assume 32-bit GOT">;

def FeatureUseIndirectJumpsHazard : SubtargetFeature<"use-indirect-jump-hazard",
                                                    "UseIndirectJumpsHazard",
                                                    "true", "Use indirect jump"
                        " guards to prevent certain speculation based attacks">;

def FeatureStrictAlign
    : SubtargetFeature<"strict-align", "StrictAlign", "true",
                       "Disable unaligned load store for r6">;

//===----------------------------------------------------------------------===//
// Register File, Calling Conv, Instruction Descriptions
//===----------------------------------------------------------------------===//

include "MipsRegisterInfo.td"
include "MipsSchedule.td"
include "MipsInstrInfo.td"
include "MipsCallingConv.td"
include "MipsRegisterBanks.td"
include "MipsCombine.td"

// Avoid forward declaration issues.
include "MipsScheduleP5600.td"
include "MipsScheduleGeneric.td"

def MipsInstrInfo : InstrInfo {
}

//===----------------------------------------------------------------------===//
// Mips processors supported.
//===----------------------------------------------------------------------===//

def ImplP5600 : SubtargetFeature<"p5600", "ProcImpl",
                                 "MipsSubtarget::CPU::P5600",
                                 "The P5600 Processor", [FeatureMips32r5]>;

class Proc<string Name, list<SubtargetFeature> Features>
 : ProcessorModel<Name, MipsGenericModel, Features>;

def : Proc<"generic", [FeatureMips32]>;
def : Proc<"mips1", [FeatureMips1]>;
def : Proc<"mips2", [FeatureMips2]>;
def : Proc<"mips32", [FeatureMips32]>;
def : Proc<"mips32r2", [FeatureMips32r2]>;
def : Proc<"mips32r3", [FeatureMips32r3]>;
def : Proc<"mips32r5", [FeatureMips32r5]>;
def : Proc<"mips32r6", [FeatureMips32r6]>;

def : Proc<"mips3", [FeatureMips3]>;
def : Proc<"mips4", [FeatureMips4]>;
def : Proc<"mips5", [FeatureMips5]>;
def : Proc<"mips64", [FeatureMips64]>;
def : Proc<"mips64r2", [FeatureMips64r2]>;
def : Proc<"mips64r3", [FeatureMips64r3]>;
def : Proc<"mips64r5", [FeatureMips64r5]>;
def : Proc<"mips64r6", [FeatureMips64r6]>;
def : Proc<"octeon", [FeatureMips64r2, FeatureCnMips]>;
def : Proc<"octeon+", [FeatureMips64r2, FeatureCnMips, FeatureCnMipsP]>;
def : ProcessorModel<"p5600", MipsP5600Model, [ImplP5600]>;

def MipsAsmParser : AsmParser {
  let ShouldEmitMatchRegisterName = 0;
}

def MipsAsmParserVariant : AsmParserVariant {
  int Variant = 0;

  // Recognize hard coded registers.
  string RegisterPrefix = "$";
}

def MipsAsmWriter : AsmWriter {
  int PassSubtarget = 1;
}

def Mips : Target {
  let InstructionSet = MipsInstrInfo;
  let AssemblyWriters = [MipsAsmWriter];
  let AssemblyParsers = [MipsAsmParser];
  let AssemblyParserVariants = [MipsAsmParserVariant];
  let AllowRegisterRenaming = 1;
}

//===----------------------------------------------------------------------===//
// Pfm Counters
//===----------------------------------------------------------------------===//

include "MipsPfmCounters.td"
