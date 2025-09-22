//===-- CSKY.td - Describe the CSKY Target Machine ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// CSKY subtarget features and instruction predicates.
//===----------------------------------------------------------------------===//
def ModeHardFloat :
  SubtargetFeature<"hard-float", "UseHardFloat",
                   "true", "Use hard floating point features">;
def ModeHardFloatABI :
  SubtargetFeature<"hard-float-abi", "UseHardFloatABI",
                   "true", "Use hard floating point ABI to pass args">;

def FeatureFPUV2_SF
    : SubtargetFeature<"fpuv2_sf", "HasFPUv2SingleFloat", "true",
                       "Enable FPUv2 single float instructions">;
def HasFPUv2_SF : Predicate<"Subtarget->hasFPUv2SingleFloat()">,
                  AssemblerPredicate<(all_of FeatureFPUV2_SF),
                  "Enable FPUv2 single float instructions">;

def FeatureFPUV2_DF
    : SubtargetFeature<"fpuv2_df", "HasFPUv2DoubleFloat", "true",
                       "Enable FPUv2 double float instructions">;
def HasFPUv2_DF : Predicate<"Subtarget->hasFPUv2DoubleFloat()">,
                  AssemblerPredicate<(all_of FeatureFPUV2_DF),
                  "Enable FPUv2 double float instructions">;

def FeatureFdivdu : SubtargetFeature<"fdivdu", "HasFdivdu", "true",
                                     "Enable float divide instructions">;
def HasFdivdu : Predicate<"Subtarget->hasFdivdu()">,
                AssemblerPredicate<(all_of FeatureFdivdu),
                "Enable float divide instructions">;

def FeatureFPUV3_HI
    : SubtargetFeature<"fpuv3_hi", "HasFPUv3HalfWord", "true",
                       "Enable FPUv3 half word converting instructions">;
def HasFPUv3_HI : Predicate<"Subtarget->hasFPUv3HalfWord()">,
                  AssemblerPredicate<(all_of FeatureFPUV3_HI),
                  "Enable FPUv3 half word converting instructions">;

def FeatureFPUV3_HF
    : SubtargetFeature<"fpuv3_hf", "HasFPUv3HalfFloat", "true",
                       "Enable FPUv3 half precision operate instructions">;
def HasFPUv3_HF : Predicate<"Subtarget->hasFPUv3HalfFloat()">,
                  AssemblerPredicate<(all_of FeatureFPUV3_HF),
                  "Enable FPUv3 half precision operate instructions">;

def FeatureFPUV3_SF
    : SubtargetFeature<"fpuv3_sf", "HasFPUv3SingleFloat", "true",
                       "Enable FPUv3 single float instructions">;
def HasFPUv3_SF : Predicate<"Subtarget->hasFPUv3SingleFloat()">,
                  AssemblerPredicate<(all_of FeatureFPUV3_SF),
                  "Enable FPUv3 single float instructions">;

def FeatureFPUV3_DF
    : SubtargetFeature<"fpuv3_df", "HasFPUv3DoubleFloat", "true",
                       "Enable FPUv3 double float instructions">;
def HasFPUv3_DF : Predicate<"Subtarget->hasFPUv3DoubleFloat()">,
                  AssemblerPredicate<(all_of FeatureFPUV3_DF),
                  "Enable FPUv3 double float instructions">;

def HasFLOATE1
    : SubtargetFeature<"floate1", "HasFLOATE1", "true", "Support CSKY floate1 instructions">;
def iHasFLOATE1 : Predicate<"Subtarget->hasFLOATE1()">,
             AssemblerPredicate<(all_of HasFLOATE1),
             "Support CSKY floate1 instructions">;

def HasFLOAT1E2
    : SubtargetFeature<"float1e2", "HasFLOAT1E2", "true", "Support CSKY float1e2 instructions">;
def iHasFLOAT1E2 : Predicate<"Subtarget->hasFLOAT1E2()">,
             AssemblerPredicate<(all_of HasFLOAT1E2),
             "Support CSKY float1e2 instructions">;

def HasFLOAT1E3
    : SubtargetFeature<"float1e3", "HasFLOAT1E3", "true", "Support CSKY float1e3 instructions">;
def iHasFLOAT1E3 : Predicate<"Subtarget->hasFLOAT1E3()">,
             AssemblerPredicate<(all_of HasFLOAT1E3),
             "Support CSKY float1e3 instructions">;

def HasFLOAT3E4
    : SubtargetFeature<"float3e4", "HasFLOAT3E4", "true", "Support CSKY float3e4 instructions">;
def iHasFLOAT3E4 : Predicate<"Subtarget->hasFLOAT3E4()">,
             AssemblerPredicate<(all_of HasFLOAT3E4),
             "Support CSKY float3e4 instructions">;

def HasFLOAT7E60
    : SubtargetFeature<"float7e60", "HasFLOAT7E60", "true", "Support CSKY float7e60 instructions">;
def iHasFLOAT7E60 : Predicate<"Subtarget->hasFLOAT7E60()">,
             AssemblerPredicate<(all_of HasFLOAT7E60),
             "Support CSKY float7e60 instructions">;

def FeatureHWDiv : SubtargetFeature<"hwdiv", "HasHardwareDivide", "true",
                                    "Enable divide instrutions">;
def HasHWDiv : Predicate<"Subtarget->hasHardwareDivide()">,
               AssemblerPredicate<(all_of FeatureHWDiv),
               "Enable divide instrutions">;

def FeatureSTM : SubtargetFeature<"multiple_stld", "HasSTM", "true",
                                  "Enable multiple load/store instrutions">;
def HasSTM : Predicate<"Subtarget->hasSTM()">,
             AssemblerPredicate<(all_of FeatureSTM),
             "Enable multiple load/store instrutions">;

def FeaturePushPop : SubtargetFeature<"pushpop", "HasPushPop", "true",
                                      "Enable push/pop instrutions">;
def HasPushPop : Predicate<"Subtarget->hasPushPop()">,
                 AssemblerPredicate<(all_of FeaturePushPop),
                 "Enable push/pop instrutions">;

def FeatureDSP
    : SubtargetFeature<"edsp", "HasDSP", "true", "Enable DSP instrutions">;
def HasDSP : Predicate<"Subtarget->hasDSP()">,
             AssemblerPredicate<(all_of FeatureDSP),
             "Enable DSP instrutions">;

def HasDSP1E2
    : SubtargetFeature<"dsp1e2", "HasDSP1E2", "true", "Support CSKY dsp1e2 instructions">;
def iHasDSP1E2 : Predicate<"Subtarget->hasDSP1E2()">,
             AssemblerPredicate<(all_of HasDSP1E2),
             "Support CSKY dsp1e2 instructions">;

def HasDSPE60
    : SubtargetFeature<"dspe60", "HasDSPE60", "true", "Support CSKY dspe60 instructions">;
def iHasDSPE60 : Predicate<"Subtarget->hasDSPE60()">,
             AssemblerPredicate<(all_of HasDSPE60),
             "Support CSKY dspe60 instructions">;

def FeatureDSPV2 : SubtargetFeature<"dspv2", "HasDSPV2", "true",
                                    "Enable DSP V2.0 instrutions">;
def HasDSPV2 : Predicate<"Subtarget->hasDSPV2()">,
               AssemblerPredicate<(all_of FeatureDSPV2),
               "Enable DSP V2.0 instrutions">;

def FeatureDSP_Silan : SubtargetFeature<"dsp_silan", "HasDSP_Silan", "true",
                                    "Enable DSP Silan instrutions">;
def HasDSP_Silan : Predicate<"Subtarget->hasDSP_Silan()">,
               AssemblerPredicate<(all_of FeatureDSP_Silan),
               "Enable DSP Silan instrutions">;

// Atomic Support
def FeatureBTST16 : SubtargetFeature<"btst16", "HasBTST16", "true",
                                     "Use the 16-bit btsti instruction">;
def HasBTST16 : Predicate<"Subtarget->hasBTST16()">,
                   AssemblerPredicate<(all_of FeatureBTST16),
                   "Use the 16-bit btsti instruction">;

// Atomic Support
def FeatureExtendLrw : SubtargetFeature<"elrw", "HasExtendLrw", "true",
                                        "Use the extend LRW instruction">;
def HasExtendLrw : Predicate<"Subtarget->hasExtendLrw()">,
                   AssemblerPredicate<(all_of FeatureExtendLrw),
                   "Use the extend LRW instruction">;

def FeatureTrust : SubtargetFeature<"trust", "HasTrust", "true",
                                    "Enable trust instructions">;
def HasTrust : Predicate<"Subtarget->hasTrust()">,
               AssemblerPredicate<(all_of FeatureTrust),
               "Enable trust instructions">;

def FeatureJAVA
    : SubtargetFeature<"java", "HasJAVA", "true", "Enable java instructions">;
def HasJAVA : Predicate<"Subtarget->hasJAVA()">,
              AssemblerPredicate<(all_of FeatureJAVA),
              "Enable java instructions">;

def FeatureCache
    : SubtargetFeature<"cache", "HasCache", "true", "Enable cache">;
def HasCache : Predicate<"Subtarget->hasCache()">,
              AssemblerPredicate<(all_of FeatureCache),
              "Enable cache">;

def FeatureNVIC
    : SubtargetFeature<"nvic", "HasNVIC", "true", "Enable NVIC">;
def HasNVIC : Predicate<"Subtarget->hasNVIC()">,
              AssemblerPredicate<(all_of FeatureNVIC),
              "Enable NVIC">;

def FeatureDoloop : SubtargetFeature<"doloop", "HasDoloop", "true",
                                     "Enable doloop instructions">;
def HasDoloop : Predicate<"Subtarget->hasDoloop()">,
                AssemblerPredicate<(all_of FeatureDoloop),
                "Enable doloop instructions">;

// Other features than instructions
def FeatureHighreg : SubtargetFeature<"high-registers", "HasHighRegisters",
                                      "true", "Enable r16-r31 registers">;
def HasHighRegisters : Predicate<"Subtarget->hasHighRegisters()">,
                       AssemblerPredicate<(all_of FeatureHighreg),
                       "Enable r16-r31 registers">;

def FeatureSmart : SubtargetFeature<"smart", "SmartMode", "true",
                                    "Let CPU work in Smart Mode">;
def SmartMode : Predicate<"Subtarget->smartMode()">,
                AssemblerPredicate<(all_of FeatureSmart),
                "Let CPU work in Smart Mode">;

def FeatureVDSPV2 : SubtargetFeature<"vdspv2", "HasVDSPV2", "true",
                                     "Enable vdsp-v2 instructions">;
def HasVDSPV2 : Predicate<"Subtarget->hasVDSPV2()">,
                AssemblerPredicate<(all_of FeatureVDSPV2),
                "Enable vdsp-v2 instructions">;

def HasVDSPV2_FLOAT : Predicate<"Subtarget->hasVDSPV2_FLOAT()">;
def HasVDSPV2_HALF: Predicate<"Subtarget->hasVDSPV2_HALF()">;

def HasVDSP2E3
    : SubtargetFeature<"vdsp2e3", "HasVDSP2E3", "true", "Support CSKY vdsp2e3 instructions">;
def iHasVDSP2E3 : Predicate<"Subtarget->hasVDSP2E3()">,
             AssemblerPredicate<(all_of HasVDSP2E3),
             "Support CSKY vdsp2e3 instructions">;

def HasVDSP2E60F
    : SubtargetFeature<"vdsp2e60f", "HasVDSP2E60F", "true", "Support CSKY vdsp2e60f instructions">;
def iHasVDSP2E60F : Predicate<"Subtarget->hasVDSP2E60F()">,
             AssemblerPredicate<(all_of HasVDSP2E60F),
             "Support CSKY vdsp2e60f instructions">;

def FeatureHardTP : SubtargetFeature<"hard-tp", "ReadTPHard", "true",
                                     "Enable TLS Pointer register">;
def ReadTPHard : Predicate<"Subtarget->readTPHard()">,
                 AssemblerPredicate<(all_of FeatureHardTP),
                 "Enable TLS Pointer register">;

def FeatureSoftTP : SubtargetFeature<"soft-tp", "ReadTPHard", "false",
                                     "Disable TLS Pointer register">;

def FeatureIstack : SubtargetFeature<"istack", "EnableInterruptAttribute",
                                     "true", "Enable interrput attribute">;
def EnableInterruptAttribute
    : Predicate<"Subtarget->enableInterruptAttribute()">,
      AssemblerPredicate<(all_of FeatureIstack),
      "Enable interrput attribute">;

def FeatureConstPool : SubtargetFeature<"constpool", "DumpConstPool", "true",
                                        "Dump the constant pool by compiler">;
def DumpConstPool : Predicate<"Subtarget->dumpConstPool()">,
                    AssemblerPredicate<(all_of FeatureConstPool),
                    "Dump the constant pool by compiler">;

def FeatureStackSize : SubtargetFeature<"stack-size", "EnableStackSize", "true",
                                        "Output stack size information">;
def EnableStackSize : Predicate<"Subtarget->enableStackSize()">,
                      AssemblerPredicate<(all_of FeatureStackSize),
                      "Output stack size information">;

def FeatureCCRT
    : SubtargetFeature<"ccrt", "UseCCRT", "true", "Use CSKY compiler runtime">;
def UseCCRT : Predicate<"Subtarget->useCCRT()">,
              AssemblerPredicate<(all_of FeatureCCRT),
              "Use CSKY compiler runtime">;

def FeatureVDSPV1_128 : SubtargetFeature<"vdspv1", "HasVDSPV1_128", "true",
                                         "Enable 128bit vdsp-v1 instructions">;
def HasVDSPV1_128 : Predicate<"Subtarget->hasVDSPV1_128()">,
                    AssemblerPredicate<(all_of FeatureVDSPV1_128),
                    "Enable 128bit vdsp-v1 instructions">;

def HasE1
    : SubtargetFeature<"e1", "HasE1", "true", "Support CSKY e1 instructions",
                       [FeatureExtendLrw]>;
def iHasE1 : Predicate<"Subtarget->hasE1()">,
             AssemblerPredicate<(all_of HasE1),
             "Support CSKY e1 instructions">;

def HasE2
    : SubtargetFeature<"e2", "HasE2", "true", "Support CSKY e2 instructions",
                       [HasE1]>;
def iHasE2 : Predicate<"Subtarget->hasE2()">,
             AssemblerPredicate<(all_of HasE2),
             "Support CSKY e2 instructions">;

def Has2E3 : SubtargetFeature<"2e3", "Has2E3", "true",
                              "Support CSKY 2e3 instructions", [HasE2]>;
def iHas2E3 : Predicate<"Subtarget->has2E3()">,
              AssemblerPredicate<(all_of Has2E3),
              "Support CSKY 2e3 instructions">;

def HasMP : SubtargetFeature<"mp", "HasMP", "true",
                                "Support CSKY mp instructions", [Has2E3]>;
def iHasMP : Predicate<"Subtarget->hasMP()">,
                AssemblerPredicate<(all_of HasMP),
                "Support CSKY mp instructions">;

def Has3E3r1 : SubtargetFeature<"3e3r1", "Has3E3r1", "true",
                                "Support CSKY 3e3r1 instructions">;
def iHas3E3r1 : Predicate<"Subtarget->has3E3r1()">,
                AssemblerPredicate<(all_of Has3E3r1),
                "Support CSKY 3e3r1 instructions">;

def Has3r1E3r2 : SubtargetFeature<"3e3r2", "Has3r1E3r2", "true",
                                  "Support CSKY 3e3r2 instructions",
                                  [Has3E3r1, FeatureDoloop]>;
def iHas3r1E3r2 : Predicate<"Subtarget->has3r1E3r2()">,
                  AssemblerPredicate<(all_of Has3r1E3r2),
                  "Support CSKY 3e3r2 instructions">;

def Has3r2E3r3
    : SubtargetFeature<"3e3r3", "Has3r2E3r3", "true",
                       "Support CSKY 3e3r3 instructions", [FeatureDoloop]>;
def iHas3r2E3r3 : Predicate<"Subtarget->has3r2E3r3()">,
                  AssemblerPredicate<(all_of Has3r2E3r3),
                  "Support CSKY 3e3r3 instructions">;

def Has3E7 : SubtargetFeature<"3e7", "Has3E7", "true",
                              "Support CSKY 3e7 instructions", [Has2E3]>;
def iHas3E7 : Predicate<"Subtarget->has3E7()">,
              AssemblerPredicate<(all_of Has3E7),
              "Support CSKY 3e7 instructions">;

def HasMP1E2 : SubtargetFeature<"mp1e2", "HasMP1E2", "true",
                                "Support CSKY mp1e2 instructions", [Has3E7]>;
def iHasMP1E2 : Predicate<"Subtarget->hasMP1E2()">,
                AssemblerPredicate<(all_of HasMP1E2),
                "Support CSKY mp1e2 instructions">;

def Has7E10 : SubtargetFeature<"7e10", "Has7E10", "true",
                               "Support CSKY 7e10 instructions", [Has3E7]>;
def iHas7E10 : Predicate<"Subtarget->has7E10()">,
               AssemblerPredicate<(all_of Has7E10),
               "Support CSKY 7e10 instructions">;

def Has10E60 : SubtargetFeature<"10e60", "Has10E60", "true",
                               "Support CSKY 10e60 instructions", [Has7E10]>;
def iHas10E60 : Predicate<"Subtarget->has10E60()">,
               AssemblerPredicate<(all_of Has10E60),
               "Support CSKY 10e60 instructions">;

//===----------------------------------------------------------------------===//
// CSKY Processor subtarget features.
//===----------------------------------------------------------------------===//

def ProcCK801 : SubtargetFeature<"ck801", "CSKYProcFamily", "CK801",
                                 "CSKY ck801 processors", []>;
def isCK801 : Predicate<"Subtarget->isCK801()">,
              AssemblerPredicate<(all_of ProcCK801)>;
def ProcCK802 : SubtargetFeature<"ck802", "CSKYProcFamily", "CK802",
                                 "CSKY ck802 processors", []>;
def ProcCK803 : SubtargetFeature<"ck803", "CSKYProcFamily", "CK803",
                                 "CSKY ck803 processors", []>;
def ProcCK803S : SubtargetFeature<"ck803s", "CSKYProcFamily", "CK803S",
                                 "CSKY ck803s processors", []>;
def ProcCK804 : SubtargetFeature<"ck804", "CSKYProcFamily", "CK804",
                                 "CSKY ck804 processors", []>;
def ProcCK805 : SubtargetFeature<"ck805", "CSKYProcFamily", "CK805",
                                 "CSKY ck805 processors", []>;
def ProcCK807 : SubtargetFeature<"ck807", "CSKYProcFamily", "CK807",
                                 "CSKY ck807 processors", []>;
def ProcCK810 : SubtargetFeature<"ck810", "CSKYProcFamily", "CK810",
                                 "CSKY ck810 processors", []>;
def ProcCK810V : SubtargetFeature<"ck810v", "CSKYProcFamily", "CK810V",
                                 "CSKY ck810v processors", []>;
def ProcCK860 : SubtargetFeature<"ck860", "CSKYProcFamily", "CK860",
                                 "CSKY ck860 processors", []>;
def ProcCK860V : SubtargetFeature<"ck860v", "CSKYProcFamily", "CK860V",
                                 "CSKY ck860v processors", []>;

//===----------------------------------------------------------------------===//
// Registers, calling conventions, instruction descriptions.
//===----------------------------------------------------------------------===//

include "CSKYRegisterInfo.td"
include "CSKYCallingConv.td"
include "CSKYInstrInfo.td"

//===----------------------------------------------------------------------===//
// CSKY processors supported.
//===----------------------------------------------------------------------===//

// btsti16 is one of most basic instructions should be enable
// even in generic processor to avoid failure codegen.
def : ProcessorModel<"generic", NoSchedModel, [FeatureBTST16]>;

// CK801 series
class CK801<string n, SchedMachineModel m, list<SubtargetFeature> f,
            list<SubtargetFeature> tunef = []>
    : ProcessorModel<n, m, !listconcat(f, [HasE1, FeatureTrust, FeatureBTST16, ProcCK801]), !listconcat(tunef, [])>;

def : CK801<"ck801", NoSchedModel, []>;
def : CK801<"ck801t", NoSchedModel, []>;
def : CK801<"e801", NoSchedModel, []>;

// CK802 series
class CK802<string n, SchedMachineModel m, list<SubtargetFeature> f,
            list<SubtargetFeature> tunef = []>
    : ProcessorModel<n, m, !listconcat(f, [HasE2, FeatureTrust, FeatureBTST16, FeatureNVIC, ProcCK802]), !listconcat(tunef, [])>;

def : CK802<"ck802", NoSchedModel, []>;
def : CK802<"ck802t", NoSchedModel, []>;
def : CK802<"ck802j", NoSchedModel, [FeatureJAVA]>;
def : CK802<"e802", NoSchedModel, []>;
def : CK802<"e802t", NoSchedModel, []>;
def : CK802<"s802", NoSchedModel, []>;
def : CK802<"s802t", NoSchedModel, []>;

// CK803 series
class CK803<string n, SchedMachineModel m, list<SubtargetFeature> f,
            list<SubtargetFeature> tunef = []>
    : ProcessorModel<n, m, !listconcat(f, [Has2E3, HasMP, FeatureTrust, FeatureBTST16, FeatureNVIC, FeatureHWDiv, ProcCK803]), !listconcat(tunef, [])>;

def : CK803<"ck803", NoSchedModel, []>;
def : CK803<"ck803h", NoSchedModel, []>;
def : CK803<"ck803t", NoSchedModel, []>;
def : CK803<"ck803ht", NoSchedModel, []>;
def : CK803<"ck803f", NoSchedModel, [FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803fh", NoSchedModel, [FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803e", NoSchedModel, [FeatureDSP, HasDSP1E2, HasDSPE60]>;
def : CK803<"ck803eh", NoSchedModel, [FeatureDSP, HasDSP1E2, HasDSPE60]>;
def : CK803<"ck803et", NoSchedModel, [FeatureDSP, HasDSP1E2, HasDSPE60]>;
def : CK803<"ck803eht", NoSchedModel, [FeatureDSP, HasDSP1E2, HasDSPE60]>;
def : CK803<"ck803ef", NoSchedModel,
            [FeatureDSP, HasDSP1E2, HasDSPE60, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803efh", NoSchedModel,
            [FeatureDSP, HasDSP1E2, HasDSPE60, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803ft", NoSchedModel, [FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803eft", NoSchedModel,
            [FeatureDSP, HasDSP1E2, HasDSPE60, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803efht", NoSchedModel,
            [FeatureDSP, HasDSP1E2, HasDSPE60, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803r1", NoSchedModel, [Has3E3r1, Has3r2E3r3, FeatureDSPV2]>;
def : CK803<"ck803hr1", NoSchedModel, [Has3E3r1, Has3r2E3r3, FeatureDSPV2]>;
def : CK803<"ck803tr1", NoSchedModel, [Has3E3r1, Has3r2E3r3, FeatureDSPV2]>;
def : CK803<"ck803htr1", NoSchedModel, [Has3E3r1, Has3r2E3r3, FeatureDSPV2]>;
def : CK803<"ck803fr1", NoSchedModel,
            [Has3E3r1, Has3r2E3r3, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureDSPV2]>;
def : CK803<"ck803fhr1", NoSchedModel,
            [Has3E3r1, Has3r2E3r3, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureDSPV2]>;
def : CK803<"ck803er1", NoSchedModel,
            [Has3E3r1, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureHighreg]>;
def : CK803<"ck803etr1", NoSchedModel,
            [Has3E3r1, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureHighreg]>;
def : CK803<"ck803ehr1", NoSchedModel,
            [Has3E3r1, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureHighreg]>;
def : CK803<"ck803ehtr1", NoSchedModel,
            [Has3E3r1, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureHighreg]>;
def : CK803<"ck803efr1", NoSchedModel,
            [Has3E3r1, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK803<"ck803efhr1", NoSchedModel,
            [Has3E3r1, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK803<"ck803ftr1", NoSchedModel, [Has3E3r1, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureDSPV2]>;
def : CK803<"ck803eftr1", NoSchedModel,
            [Has3E3r1, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK803<"ck803efhtr1", NoSchedModel,
            [Has3E3r1, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK803<"ck803r2", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2]>;
def : CK803<"ck803hr2", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2]>;
def : CK803<"ck803tr2", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2]>;
def : CK803<"ck803htr2", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2]>;
def : CK803<"ck803fr2", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803fhr2", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803er2", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureHighreg]>;
def : CK803<"ck803etr2", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureHighreg]>;
def : CK803<"ck803ehr2", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureHighreg]>;
def : CK803<"ck803ehtr2", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureHighreg]>;
def : CK803<"ck803efr2", NoSchedModel,
            [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK803<"ck803efhr2", NoSchedModel,
            [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK803<"ck803ftr2", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803eftr2", NoSchedModel,
            [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK803<"ck803efhtr2", NoSchedModel,
            [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK803<"ck803r3", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2]>;
def : CK803<"ck803hr3", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2]>;
def : CK803<"ck803tr3", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2]>;
def : CK803<"ck803htr3", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2]>;
def : CK803<"ck803fr3", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803fhr3", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803er3", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureHighreg]>;
def : CK803<"ck803etr3", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureHighreg]>;
def : CK803<"ck803ehr3", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureHighreg]>;
def : CK803<"ck803ehtr3", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureHighreg]>;
def : CK803<"ck803efr3", NoSchedModel,
            [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK803<"ck803efhr3", NoSchedModel,
            [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK803<"ck803ftr3", NoSchedModel, [Has3r1E3r2, Has3r2E3r3, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803<"ck803eftr3", NoSchedModel,
            [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK803<"ck803efhtr3", NoSchedModel,
            [Has3r1E3r2, Has3r2E3r3, FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK803<"s803", NoSchedModel, [Has3r1E3r2, Has3r2E3r3]>;
def : CK803<"s803t", NoSchedModel, [Has3r1E3r2, Has3r2E3r3]>;
def : CK803<"e803", NoSchedModel, [Has3r1E3r2, Has3r2E3r3]>;
def : CK803<"e803t", NoSchedModel, [Has3r1E3r2, Has3r2E3r3]>;

// CK803S series
class CK803S<string n, SchedMachineModel m, list<SubtargetFeature> f,
list<SubtargetFeature> tunef = []> : CK803<n, m, !listconcat(f, [Has3E3r1, ProcCK803S]), tunef>;

def : CK803S<"ck803s", NoSchedModel, []>;
def : CK803S<"ck803sn", NoSchedModel, [FeatureDSP_Silan]>;
def : CK803S<"ck803st", NoSchedModel, []>;
def : CK803S<"ck803snt", NoSchedModel, [FeatureDSP_Silan]>;
def : CK803S<"ck803sf", NoSchedModel, [FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803S<"ck803sfn", NoSchedModel, [FeatureFPUV2_SF, FeatureDSP_Silan, HasFLOATE1, HasFLOAT1E3]>;
def : CK803S<"ck803se", NoSchedModel, [FeatureDSP, HasDSP1E2, HasDSPE60]>;
def : CK803S<"ck803sen", NoSchedModel, [FeatureDSP, HasDSP1E2, HasDSPE60, FeatureDSP_Silan]>;
def : CK803S<"ck803sef", NoSchedModel,
             [FeatureDSP, HasDSP1E2, HasDSPE60, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803S<"ck803sefn", NoSchedModel,
             [FeatureDSP, HasDSP1E2, HasDSPE60, FeatureFPUV2_SF, FeatureDSP_Silan,
              HasFLOATE1, HasFLOAT1E3]>;
def : CK803S<"ck803seft", NoSchedModel,
             [FeatureDSP, HasDSP1E2, HasDSPE60, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK803S<"ck803sefnt", NoSchedModel,
             [FeatureDSP, HasDSP1E2, HasDSPE60, FeatureFPUV2_SF, FeatureDSP_Silan,
              HasFLOATE1, HasFLOAT1E3]>;

// CK804 series
class CK804<string n, SchedMachineModel m, list<SubtargetFeature> f,
            list<SubtargetFeature> tunef = []>
    : CK803<n, m, !listconcat(f, [Has3r1E3r2, Has3r2E3r3, ProcCK804]), !listconcat(tunef, [])>;

def : CK804<"ck804", NoSchedModel, []>;
def : CK804<"ck804h", NoSchedModel, []>;
def : CK804<"ck804t", NoSchedModel, []>;
def : CK804<"ck804ht", NoSchedModel, []>;
def : CK804<"ck804f", NoSchedModel, [FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK804<"ck804fh", NoSchedModel, [FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK804<"ck804e", NoSchedModel, [FeatureDSPV2, FeatureHighreg]>;
def : CK804<"ck804et", NoSchedModel, [FeatureDSPV2, FeatureHighreg]>;
def : CK804<"ck804eh", NoSchedModel, [FeatureDSPV2, FeatureHighreg]>;
def : CK804<"ck804eht", NoSchedModel, [FeatureDSPV2, FeatureHighreg]>;
def : CK804<"ck804ef", NoSchedModel, [FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK804<"ck804efh", NoSchedModel, [FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK804<"ck804ft", NoSchedModel, [FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK804<"ck804eft", NoSchedModel, [FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK804<"ck804efht", NoSchedModel, [FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK804<"e804d", NoSchedModel, [FeatureDSPV2, FeatureHighreg]>;
def : CK804<"e804dt", NoSchedModel, [FeatureDSPV2, FeatureHighreg]>;
def : CK804<"e804f", NoSchedModel, [FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK804<"e804ft", NoSchedModel, [FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK804<"e804df", NoSchedModel, [FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;
def : CK804<"e804dft", NoSchedModel, [FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3, FeatureHighreg]>;

// CK805 series
class CK805<string n, SchedMachineModel m, list<SubtargetFeature> f,
            list<SubtargetFeature> tunef = []>
    : CK803<n, m, !listconcat(f, [FeatureHighreg, FeatureVDSPV2, HasVDSP2E3, Has3r1E3r2, Has3r2E3r3, ProcCK805]),
            !listconcat(tunef, [])>;

def : CK805<"ck805", NoSchedModel, []>;
def : CK805<"i805", NoSchedModel, []>;
def : CK805<"ck805t", NoSchedModel, []>;
def : CK805<"i805f", NoSchedModel, [FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK805<"ck805f", NoSchedModel, [FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK805<"ck805e", NoSchedModel, [FeatureDSPV2]>;
def : CK805<"ck805ef", NoSchedModel, [FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK805<"ck805et", NoSchedModel, [FeatureDSPV2]>;
def : CK805<"ck805ft", NoSchedModel, [FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;
def : CK805<"ck805eft", NoSchedModel, [FeatureDSPV2, FeatureFPUV2_SF, HasFLOATE1, HasFLOAT1E3]>;

// CK807 series
class CK807<string n, SchedMachineModel m, list<SubtargetFeature> f,
            list<SubtargetFeature> tunef = []>
    : ProcessorModel<n, m, !listconcat(f,
    [ Has3E7, HasMP, HasMP1E2, FeatureTrust, FeatureHWDiv, FeatureDSP, HasDSP1E2, HasDSPE60,
      FeatureHighreg, FeatureHardTP, FeatureNVIC, FeatureCache, ProcCK807]), !listconcat(tunef, [])>;

def : CK807<"ck807", NoSchedModel, []>;
def : CK807<"c807", NoSchedModel, []>;
def : CK807<"r807", NoSchedModel, []>;
def : CK807<"ck807e", NoSchedModel, [FeatureDSP, HasDSP1E2, HasDSPE60]>;
def : CK807<"ck807f", NoSchedModel,
            [FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
             HasFLOATE1, HasFLOAT1E2, HasFLOAT1E3, HasFLOAT3E4]>;
def : CK807<"c807f", NoSchedModel,
            [FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
             HasFLOATE1, HasFLOAT1E2, HasFLOAT1E3, HasFLOAT3E4]>;
def : CK807<"r807f", NoSchedModel,
            [FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
             HasFLOATE1, HasFLOAT1E2, HasFLOAT1E3, HasFLOAT3E4]>;
def : CK807<"ck807ef", NoSchedModel, [
  FeatureDSP, HasDSP1E2, HasDSPE60, FeatureFPUV2_SF, FeatureFPUV2_DF,
  FeatureFdivdu, HasFLOATE1, HasFLOAT1E2, HasFLOAT1E3, HasFLOAT3E4]>;

// CK810 series
class CK810<string n, SchedMachineModel m, list<SubtargetFeature> f,
            list<SubtargetFeature> tunef = []>
    : ProcessorModel<n, m, !listconcat(f,
    [ Has7E10, HasMP, HasMP1E2, FeatureTrust, FeatureHWDiv, FeatureDSP, HasDSP1E2, HasDSPE60,
      FeatureHighreg, FeatureHardTP, FeatureNVIC, FeatureCache, ProcCK810]), !listconcat(tunef, [])>;

def : CK810<"ck810", NoSchedModel, []>;
def : CK810<"ck810e", NoSchedModel, []>;
def : CK810<"ck810t", NoSchedModel, []>;
def : CK810<"ck810et", NoSchedModel, []>;
def : CK810<"c810", NoSchedModel,
            [FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
             HasFLOATE1, HasFLOAT1E2]>;
def : CK810<"ck810f", NoSchedModel,
            [FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
             HasFLOATE1, HasFLOAT1E2]>;
def : CK810<"ck810ef", NoSchedModel,
            [FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
             HasFLOATE1, HasFLOAT1E2]>;
def : CK810<"ck810ft", NoSchedModel,
            [FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
             HasFLOATE1, HasFLOAT1E2]>;
def : CK810<"ck810eft", NoSchedModel,
            [FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
             HasFLOATE1, HasFLOAT1E2]>;
def : CK810<"c810t", NoSchedModel,
            [FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
             HasFLOATE1, HasFLOAT1E2]>;

class CK810V<string n, SchedMachineModel m, list<SubtargetFeature> f,
             list<SubtargetFeature> tunef = []>
    : CK810<n, m, !listconcat(f, [FeatureVDSPV1_128, ProcCK810V]), !listconcat(tunef, [])>;

def : CK810V<"ck810v", NoSchedModel, []>;
def : CK810V<"ck810ev", NoSchedModel, []>;
def : CK810V<"ck810tv", NoSchedModel, []>;
def : CK810V<"ck810etv", NoSchedModel, []>;
def : CK810V<"ck810fv", NoSchedModel, [
  FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
  HasFLOATE1, HasFLOAT1E2
]>;
def : CK810V<"ck810efv", NoSchedModel, [
  FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
  HasFLOATE1, HasFLOAT1E2
]>;
def : CK810V<"c810v", NoSchedModel, [
  FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
  HasFLOATE1, HasFLOAT1E2
]>;
def : CK810V<"ck810ftv", NoSchedModel, [
  FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
  HasFLOATE1, HasFLOAT1E2
]>;
def : CK810V<"ck810eftv", NoSchedModel, [
  FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
  HasFLOATE1, HasFLOAT1E2
]>;
def : CK810V<"c810tv", NoSchedModel, [
  FeatureFPUV2_SF, FeatureFPUV2_DF, FeatureFdivdu,
  HasFLOATE1, HasFLOAT1E2
]>;

// CK860 series
class CK860<string n, SchedMachineModel m, list<SubtargetFeature> f,
            list<SubtargetFeature> tunef = []>
    : ProcessorModel<n, m, !listconcat(f,
    [ Has10E60, HasMP, HasMP1E2, Has3r1E3r2, Has3r2E3r3, FeatureTrust, FeatureBTST16, FeatureHWDiv, HasDSPE60,
      FeatureHighreg, FeatureHardTP, FeatureNVIC, FeatureCache, ProcCK860]), !listconcat(tunef, [])>;

class CK860V<string n, SchedMachineModel m, list<SubtargetFeature> f,
             list<SubtargetFeature> tunef = []>
    : CK860<n, m, !listconcat(f, [FeatureVDSPV2, HasVDSP2E60F, ProcCK860V]), !listconcat(tunef, [])>;

def : CK860<"ck860", NoSchedModel, []>;
def : CK860<"ck860f", NoSchedModel,
            [FeatureFPUV3_HI, FeatureFPUV3_HF, FeatureFPUV3_SF, FeatureFPUV3_DF, HasFLOAT7E60]>;
def : CK860<"c860", NoSchedModel,
            [FeatureFPUV3_HI, FeatureFPUV3_HF, FeatureFPUV3_SF, FeatureFPUV3_DF, HasFLOAT7E60]>;
def : CK860V<"c860v", NoSchedModel,
             [FeatureFPUV3_HI, FeatureFPUV3_HF, FeatureFPUV3_SF, FeatureFPUV3_DF, HasFLOAT7E60]>;
def : CK860V<"ck860v", NoSchedModel, []>;
def : CK860V<"ck860fv", NoSchedModel,
             [FeatureFPUV3_HI, FeatureFPUV3_HF, FeatureFPUV3_SF, FeatureFPUV3_DF, HasFLOAT7E60]>;

//===----------------------------------------------------------------------===//
// Define the CSKY target.
//===----------------------------------------------------------------------===//

def CSKYInstrInfo : InstrInfo;


def CSKYAsmParser : AsmParser {
  let ShouldEmitMatchRegisterAltName = 1;
  let AllowDuplicateRegisterNames = 1;
}

def CSKYAsmWriter : AsmWriter {
  int PassSubtarget = 1;
}

def CSKY : Target {
  let InstructionSet = CSKYInstrInfo;
  let AssemblyParsers = [CSKYAsmParser];
  let AssemblyWriters = [CSKYAsmWriter];
  let AllowRegisterRenaming = 1;
}
