//=- AArch64Processors.td - Describe AArch64 Processors ------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// AArch64 Processor subtarget features.
//===----------------------------------------------------------------------===//


def TuneA35     : SubtargetFeature<"a35", "ARMProcFamily", "CortexA35",
                                "Cortex-A35 ARM processors">;

def TuneA53     : SubtargetFeature<"a53", "ARMProcFamily", "CortexA53",
                                   "Cortex-A53 ARM processors", [
                                   FeatureFuseAES,
                                   FeatureFuseAdrpAdd,
                                   FeatureBalanceFPOps,
                                   FeaturePostRAScheduler]>;

def TuneA55     : SubtargetFeature<"a55", "ARMProcFamily", "CortexA55",
                                   "Cortex-A55 ARM processors", [
                                   FeatureFuseAES,
                                   FeatureFuseAdrpAdd,
                                   FeaturePostRAScheduler,
                                   FeatureFuseAddress]>;

def TuneA510    : SubtargetFeature<"a510", "ARMProcFamily", "CortexA510",
                                   "Cortex-A510 ARM processors", [
                                   FeatureFuseAES,
                                   FeatureFuseAdrpAdd,
                                   FeaturePostRAScheduler
                                   ]>;

def TuneA520    : SubtargetFeature<"a520", "ARMProcFamily", "CortexA520",
                                   "Cortex-A520 ARM processors", [
                                   FeatureFuseAES,
                                   FeatureFuseAdrpAdd,
                                   FeaturePostRAScheduler]>;

def TuneA520AE  : SubtargetFeature<"a520ae", "ARMProcFamily", "CortexA520",
                                   "Cortex-A520AE ARM processors", [
                                   FeatureFuseAES,
                                   FeatureFuseAdrpAdd,
                                   FeaturePostRAScheduler]>;

def TuneA57     : SubtargetFeature<"a57", "ARMProcFamily", "CortexA57",
                                   "Cortex-A57 ARM processors", [
                                   FeatureFuseAES,
                                   FeatureBalanceFPOps,
                                   FeatureFuseAdrpAdd,
                                   FeatureFuseLiterals,
                                   FeatureAddrLSLSlow14,
                                   FeaturePostRAScheduler,
                                   FeatureEnableSelectOptimize,
                                   FeaturePredictableSelectIsExpensive]>;

def TuneA65     : SubtargetFeature<"a65", "ARMProcFamily", "CortexA65",
                                   "Cortex-A65 ARM processors", [
                                   FeatureFuseAES,
                                   FeatureFuseAddress,
                                   FeatureFuseAdrpAdd,
                                   FeatureFuseLiterals,
                                   FeatureEnableSelectOptimize,
                                   FeaturePredictableSelectIsExpensive]>;

def TuneA72     : SubtargetFeature<"a72", "ARMProcFamily", "CortexA72",
                                   "Cortex-A72 ARM processors", [
                                   FeatureFuseAES,
                                   FeatureFuseAdrpAdd,
                                   FeatureFuseLiterals,
                                   FeatureAddrLSLSlow14,
                                   FeatureEnableSelectOptimize,
                                   FeaturePredictableSelectIsExpensive]>;

def TuneA73     : SubtargetFeature<"a73", "ARMProcFamily", "CortexA73",
                                   "Cortex-A73 ARM processors", [
                                   FeatureFuseAES,
                                   FeatureFuseAdrpAdd,
                                   FeatureAddrLSLSlow14,
                                   FeatureEnableSelectOptimize,
                                   FeaturePredictableSelectIsExpensive]>;

def TuneA75     : SubtargetFeature<"a75", "ARMProcFamily", "CortexA75",
                                   "Cortex-A75 ARM processors", [
                                   FeatureFuseAES,
                                   FeatureFuseAdrpAdd,
                                   FeatureAddrLSLSlow14,
                                   FeatureEnableSelectOptimize,
                                   FeaturePredictableSelectIsExpensive]>;

def TuneA76     : SubtargetFeature<"a76", "ARMProcFamily", "CortexA76",
                                   "Cortex-A76 ARM processors", [
                                   FeatureFuseAES,
                                   FeatureFuseAdrpAdd,
                                   FeatureAddrLSLSlow14,
                                   FeatureALULSLFast,
                                   FeatureEnableSelectOptimize,
                                   FeaturePredictableSelectIsExpensive]>;

def TuneA77     : SubtargetFeature<"a77", "ARMProcFamily", "CortexA77",
                                   "Cortex-A77 ARM processors", [
                                   FeatureCmpBccFusion,
                                   FeatureFuseAES,
                                   FeatureFuseAdrpAdd,
                                   FeatureAddrLSLSlow14,
                                   FeatureALULSLFast,
                                   FeatureEnableSelectOptimize,
                                   FeaturePredictableSelectIsExpensive]>;

def TuneA78 : SubtargetFeature<"a78", "ARMProcFamily", "CortexA78",
                               "Cortex-A78 ARM processors", [
                               FeatureCmpBccFusion,
                               FeatureFuseAES,
                               FeatureFuseAdrpAdd,
                               FeatureAddrLSLSlow14,
                               FeatureALULSLFast,
                               FeaturePostRAScheduler,
                               FeatureEnableSelectOptimize,
                               FeaturePredictableSelectIsExpensive]>;

def TuneA78AE : SubtargetFeature<"a78ae", "ARMProcFamily",
                                 "CortexA78AE",
                                 "Cortex-A78AE ARM processors", [
                                 FeatureCmpBccFusion,
                                 FeatureFuseAES,
                                 FeatureFuseAdrpAdd,
                                 FeatureAddrLSLSlow14,
                                 FeatureALULSLFast,
                                 FeaturePostRAScheduler,
                                 FeatureEnableSelectOptimize,
                                 FeaturePredictableSelectIsExpensive]>;

def TuneA78C : SubtargetFeature<"a78c", "ARMProcFamily",
                                "CortexA78C",
                                "Cortex-A78C ARM processors", [
                                FeatureCmpBccFusion,
                                FeatureFuseAES,
                                FeatureFuseAdrpAdd,
                                FeatureAddrLSLSlow14,
                                FeatureALULSLFast,
                                FeaturePostRAScheduler,
                                FeatureEnableSelectOptimize,
                                FeaturePredictableSelectIsExpensive]>;

def TuneA710    : SubtargetFeature<"a710", "ARMProcFamily", "CortexA710",
                                   "Cortex-A710 ARM processors", [
                                   FeatureCmpBccFusion,
                                   FeatureFuseAES,
                                   FeatureFuseAdrpAdd,
                                   FeatureALULSLFast,
                                   FeaturePostRAScheduler,
                                   FeatureEnableSelectOptimize,
                                   FeaturePredictableSelectIsExpensive]>;

def TuneA715 : SubtargetFeature<"a715", "ARMProcFamily", "CortexA715",
                                 "Cortex-A715 ARM processors", [
                                 FeatureFuseAES,
                                 FeaturePostRAScheduler,
                                 FeatureCmpBccFusion,
                                 FeatureALULSLFast,
                                 FeatureFuseAdrpAdd,
                                 FeatureEnableSelectOptimize,
                                 FeaturePredictableSelectIsExpensive]>;

def TuneA720 : SubtargetFeature<"a720", "ARMProcFamily", "CortexA720",
                                "Cortex-A720 ARM processors", [
                                 FeatureFuseAES,
                                 FeaturePostRAScheduler,
                                 FeatureCmpBccFusion,
                                 FeatureALULSLFast,
                                 FeatureFuseAdrpAdd,
                                 FeatureEnableSelectOptimize,
                                 FeaturePredictableSelectIsExpensive]>;

def TuneA720AE : SubtargetFeature<"a720ae", "ARMProcFamily", "CortexA720",
                                "Cortex-A720AE ARM processors", [
                                 FeatureFuseAES,
                                 FeaturePostRAScheduler,
                                 FeatureCmpBccFusion,
                                 FeatureALULSLFast,
                                 FeatureFuseAdrpAdd,
                                 FeatureEnableSelectOptimize,
                                 FeaturePredictableSelectIsExpensive]>;

def TuneA725 : SubtargetFeature<"cortex-a725", "ARMProcFamily",
                                "CortexA725",
                                "Cortex-A725 ARM processors", [
                                FeatureFuseAES,
                                FeaturePostRAScheduler,
                                FeatureCmpBccFusion,
                                FeatureALULSLFast,
                                FeatureFuseAdrpAdd,
                                FeatureEnableSelectOptimize,
                                FeaturePredictableSelectIsExpensive]>;

def TuneR82 : SubtargetFeature<"cortex-r82", "ARMProcFamily",
                               "CortexR82",
                               "Cortex-R82 ARM processors", [
                               FeaturePostRAScheduler]>;

def TuneR82AE : SubtargetFeature<"cortex-r82ae", "ARMProcFamily",
                                 "CortexR82AE",
                                 "Cortex-R82-AE ARM processors",
                                 [FeaturePostRAScheduler]>;

def TuneX1 : SubtargetFeature<"cortex-x1", "ARMProcFamily", "CortexX1",
                                  "Cortex-X1 ARM processors", [
                                  FeatureCmpBccFusion,
                                  FeatureFuseAES,
                                  FeatureFuseAdrpAdd,
                                  FeatureAddrLSLSlow14,
                                  FeatureALULSLFast,
                                  FeaturePostRAScheduler,
                                  FeatureEnableSelectOptimize,
                                  FeaturePredictableSelectIsExpensive]>;

def TuneX2 : SubtargetFeature<"cortex-x2", "ARMProcFamily", "CortexX2",
                                  "Cortex-X2 ARM processors", [
                                  FeatureCmpBccFusion,
                                  FeatureFuseAES,
                                  FeatureFuseAdrpAdd,
                                  FeatureALULSLFast,
                                  FeaturePostRAScheduler,
                                  FeatureEnableSelectOptimize,
                                  FeaturePredictableSelectIsExpensive]>;

def TuneX3 : SubtargetFeature<"cortex-x3", "ARMProcFamily", "CortexX3",
                              "Cortex-X3 ARM processors", [
                               FeatureALULSLFast,
                               FeatureFuseAdrpAdd,
                               FeatureFuseAES,
                               FeaturePostRAScheduler,
                               FeatureEnableSelectOptimize,
                               FeaturePredictableSelectIsExpensive]>;

def TuneX4 : SubtargetFeature<"cortex-x4", "ARMProcFamily", "CortexX4",
                              "Cortex-X4 ARM processors", [
                               FeatureALULSLFast,
                               FeatureFuseAdrpAdd,
                               FeatureFuseAES,
                               FeaturePostRAScheduler,
                               FeatureEnableSelectOptimize,
                               FeaturePredictableSelectIsExpensive]>;

def TuneX925 : SubtargetFeature<"cortex-x925", "ARMProcFamily",
                                "CortexX925", "Cortex-X925 ARM processors",[
                                FeatureALULSLFast,
                                FeatureFuseAdrpAdd,
                                FeatureFuseAES,
                                FeaturePostRAScheduler,
                                FeatureEnableSelectOptimize,
                                FeaturePredictableSelectIsExpensive]>;

def TuneA64FX : SubtargetFeature<"a64fx", "ARMProcFamily", "A64FX",
                                 "Fujitsu A64FX processors", [
                                 FeaturePostRAScheduler,
                                 FeatureAggressiveFMA,
                                 FeatureArithmeticBccFusion,
                                 FeatureStorePairSuppress,
                                 FeaturePredictableSelectIsExpensive]>;

def TuneCarmel : SubtargetFeature<"carmel", "ARMProcFamily", "Carmel",
                                  "Nvidia Carmel processors">;

// Note that cyclone does not fuse AES instructions, but newer apple chips do
// perform the fusion and cyclone is used by default when targetting apple OSes.
def TuneAppleA7  : SubtargetFeature<"apple-a7", "ARMProcFamily", "AppleA7",
                                    "Apple A7 (the CPU formerly known as Cyclone)", [
                                    FeatureAlternateSExtLoadCVTF32Pattern,
                                    FeatureArithmeticBccFusion,
                                    FeatureArithmeticCbzFusion,
                                    FeatureDisableLatencySchedHeuristic,
                                    FeatureFuseAES, FeatureFuseCryptoEOR,
                                    FeatureStorePairSuppress,
                                    FeatureZCRegMove,
                                    FeatureZCZeroing,
                                    FeatureZCZeroingFPWorkaround]>;

def TuneAppleA10 : SubtargetFeature<"apple-a10", "ARMProcFamily", "AppleA10",
                                    "Apple A10", [
                                    FeatureAlternateSExtLoadCVTF32Pattern,
                                    FeatureArithmeticBccFusion,
                                    FeatureArithmeticCbzFusion,
                                    FeatureDisableLatencySchedHeuristic,
                                    FeatureFuseAES,
                                    FeatureFuseCryptoEOR,
                                    FeatureStorePairSuppress,
                                    FeatureZCRegMove,
                                    FeatureZCZeroing]>;

def TuneAppleA11 : SubtargetFeature<"apple-a11", "ARMProcFamily", "AppleA11",
                                    "Apple A11", [
                                    FeatureAlternateSExtLoadCVTF32Pattern,
                                    FeatureArithmeticBccFusion,
                                    FeatureArithmeticCbzFusion,
                                    FeatureDisableLatencySchedHeuristic,
                                    FeatureFuseAES,
                                    FeatureFuseCryptoEOR,
                                    FeatureStorePairSuppress,
                                    FeatureZCRegMove,
                                    FeatureZCZeroing]>;

def TuneAppleA12 : SubtargetFeature<"apple-a12", "ARMProcFamily", "AppleA12",
                                    "Apple A12", [
                                    FeatureAlternateSExtLoadCVTF32Pattern,
                                    FeatureArithmeticBccFusion,
                                    FeatureArithmeticCbzFusion,
                                    FeatureDisableLatencySchedHeuristic,
                                    FeatureFuseAES,
                                    FeatureFuseCryptoEOR,
                                    FeatureStorePairSuppress,
                                    FeatureZCRegMove,
                                    FeatureZCZeroing]>;

def TuneAppleA13 : SubtargetFeature<"apple-a13", "ARMProcFamily", "AppleA13",
                                    "Apple A13", [
                                    FeatureAlternateSExtLoadCVTF32Pattern,
                                    FeatureArithmeticBccFusion,
                                    FeatureArithmeticCbzFusion,
                                    FeatureDisableLatencySchedHeuristic,
                                    FeatureFuseAES,
                                    FeatureFuseCryptoEOR,
                                    FeatureStorePairSuppress,
                                    FeatureZCRegMove,
                                    FeatureZCZeroing]>;

def TuneAppleA14 : SubtargetFeature<"apple-a14", "ARMProcFamily", "AppleA14",
                                    "Apple A14", [
                                    FeatureAggressiveFMA,
                                    FeatureAlternateSExtLoadCVTF32Pattern,
                                    FeatureArithmeticBccFusion,
                                    FeatureArithmeticCbzFusion,
                                    FeatureDisableLatencySchedHeuristic,
                                    FeatureFuseAddress,
                                    FeatureFuseAES,
                                    FeatureFuseArithmeticLogic,
                                    FeatureFuseCCSelect,
                                    FeatureFuseCryptoEOR,
                                    FeatureFuseLiterals,
                                    FeatureStorePairSuppress,
                                    FeatureZCRegMove,
                                    FeatureZCZeroing]>;

def TuneAppleA15 : SubtargetFeature<"apple-a15", "ARMProcFamily", "AppleA15",
                                    "Apple A15", [
                                    FeatureAlternateSExtLoadCVTF32Pattern,
                                    FeatureArithmeticBccFusion,
                                    FeatureArithmeticCbzFusion,
                                    FeatureDisableLatencySchedHeuristic,
                                    FeatureFuseAddress,
                                    FeatureFuseAES,
                                    FeatureFuseArithmeticLogic,
                                    FeatureFuseCCSelect,
                                    FeatureFuseCryptoEOR,
                                    FeatureFuseLiterals,
                                    FeatureStorePairSuppress,
                                    FeatureZCRegMove,
                                    FeatureZCZeroing]>;

def TuneAppleA16 : SubtargetFeature<"apple-a16", "ARMProcFamily", "AppleA16",
                                    "Apple A16", [
                                    FeatureAlternateSExtLoadCVTF32Pattern,
                                    FeatureArithmeticBccFusion,
                                    FeatureArithmeticCbzFusion,
                                    FeatureDisableLatencySchedHeuristic,
                                    FeatureFuseAddress,
                                    FeatureFuseAdrpAdd,
                                    FeatureFuseAES,
                                    FeatureFuseArithmeticLogic,
                                    FeatureFuseCCSelect,
                                    FeatureFuseCryptoEOR,
                                    FeatureFuseLiterals,
                                    FeatureStorePairSuppress,
                                    FeatureZCRegMove,
                                    FeatureZCZeroing]>;

def TuneAppleA17 : SubtargetFeature<"apple-a17", "ARMProcFamily", "AppleA17",
                                    "Apple A17", [
                                    FeatureAlternateSExtLoadCVTF32Pattern,
                                    FeatureArithmeticBccFusion,
                                    FeatureArithmeticCbzFusion,
                                    FeatureDisableLatencySchedHeuristic,
                                    FeatureFuseAddress,
                                    FeatureFuseAdrpAdd,
                                    FeatureFuseAES,
                                    FeatureFuseArithmeticLogic,
                                    FeatureFuseCCSelect,
                                    FeatureFuseCryptoEOR,
                                    FeatureFuseLiterals,
                                    FeatureStorePairSuppress,
                                    FeatureZCRegMove,
                                    FeatureZCZeroing]>;

def TuneAppleM4 : SubtargetFeature<"apple-m4", "ARMProcFamily", "AppleM4",
                                     "Apple M4", [
                                     FeatureAlternateSExtLoadCVTF32Pattern,
                                     FeatureArithmeticBccFusion,
                                     FeatureArithmeticCbzFusion,
                                     FeatureDisableLatencySchedHeuristic,
                                     FeatureFuseAddress,
                                     FeatureFuseAES,
                                     FeatureFuseArithmeticLogic,
                                     FeatureFuseCCSelect,
                                     FeatureFuseCryptoEOR,
                                     FeatureFuseLiterals,
                                     FeatureZCRegMove,
                                     FeatureZCZeroing
                                     ]>;

def TuneExynosM3 : SubtargetFeature<"exynosm3", "ARMProcFamily", "ExynosM3",
                                    "Samsung Exynos-M3 processors",
                                    [FeatureExynosCheapAsMoveHandling,
                                     FeatureForce32BitJumpTables,
                                     FeatureFuseAddress,
                                     FeatureFuseAES,
                                     FeatureFuseCCSelect,
                                     FeatureFuseAdrpAdd,
                                     FeatureFuseLiterals,
                                     FeatureStorePairSuppress,
                                     FeatureALULSLFast,
                                     FeaturePostRAScheduler,
                                     FeaturePredictableSelectIsExpensive]>;

// Re-uses some scheduling and tunings from the ExynosM3 proc family.
def TuneExynosM4 : SubtargetFeature<"exynosm4", "ARMProcFamily", "ExynosM3",
                                    "Samsung Exynos-M4 processors",
                                    [FeatureArithmeticBccFusion,
                                     FeatureArithmeticCbzFusion,
                                     FeatureExynosCheapAsMoveHandling,
                                     FeatureForce32BitJumpTables,
                                     FeatureFuseAddress,
                                     FeatureFuseAES,
                                     FeatureFuseArithmeticLogic,
                                     FeatureFuseCCSelect,
                                     FeatureFuseAdrpAdd,
                                     FeatureFuseLiterals,
                                     FeatureStorePairSuppress,
                                     FeatureALULSLFast,
                                     FeaturePostRAScheduler,
                                     FeatureZCZeroing]>;

def TuneKryo    : SubtargetFeature<"kryo", "ARMProcFamily", "Kryo",
                                   "Qualcomm Kryo processors", [
                                   FeaturePostRAScheduler,
                                   FeaturePredictableSelectIsExpensive,
                                   FeatureZCZeroing,
                                   FeatureALULSLFast,
                                   FeatureStorePairSuppress]>;

def TuneFalkor  : SubtargetFeature<"falkor", "ARMProcFamily", "Falkor",
                                   "Qualcomm Falkor processors", [
                                   FeaturePostRAScheduler,
                                   FeaturePredictableSelectIsExpensive,
                                   FeatureZCZeroing,
                                   FeatureStorePairSuppress,
                                   FeatureALULSLFast,
                                   FeatureSlowSTRQro]>;

def TuneNeoverseE1 : SubtargetFeature<"neoversee1", "ARMProcFamily", "NeoverseE1",
                                      "Neoverse E1 ARM processors", [
                                      FeatureFuseAES,
                                      FeatureFuseAdrpAdd,
                                      FeaturePostRAScheduler]>;

def TuneNeoverseN1 : SubtargetFeature<"neoversen1", "ARMProcFamily", "NeoverseN1",
                                      "Neoverse N1 ARM processors", [
                                      FeatureFuseAES,
                                      FeatureFuseAdrpAdd,
                                      FeatureAddrLSLSlow14,
                                      FeatureALULSLFast,
                                      FeaturePostRAScheduler,
                                      FeatureEnableSelectOptimize,
                                      FeaturePredictableSelectIsExpensive]>;

def TuneNeoverseN2 : SubtargetFeature<"neoversen2", "ARMProcFamily", "NeoverseN2",
                                      "Neoverse N2 ARM processors", [
                                      FeatureFuseAES,
                                      FeatureFuseAdrpAdd,
                                      FeatureALULSLFast,
                                      FeaturePostRAScheduler,
                                      FeatureEnableSelectOptimize,
                                      FeaturePredictableSelectIsExpensive]>;

def TuneNeoverseN3 : SubtargetFeature<"neoversen3", "ARMProcFamily", "NeoverseN3",
                                      "Neoverse N3 ARM processors", [
                                      FeatureFuseAES,
                                      FeaturePostRAScheduler,
                                      FeatureALULSLFast,
                                      FeatureFuseAdrpAdd,
                                      FeatureEnableSelectOptimize,
                                      FeaturePredictableSelectIsExpensive]>;

def TuneNeoverse512TVB : SubtargetFeature<"neoverse512tvb", "ARMProcFamily", "Neoverse512TVB",
                                      "Neoverse 512-TVB ARM processors", [
                                      FeatureFuseAES,
                                      FeatureFuseAdrpAdd,
                                      FeatureALULSLFast,
                                      FeaturePostRAScheduler,
                                      FeatureEnableSelectOptimize,
                                      FeaturePredictableSelectIsExpensive]>;

def TuneNeoverseV1 : SubtargetFeature<"neoversev1", "ARMProcFamily", "NeoverseV1",
                                      "Neoverse V1 ARM processors", [
                                      FeatureFuseAES,
                                      FeatureFuseAdrpAdd,
                                      FeatureAddrLSLSlow14,
                                      FeatureALULSLFast,
                                      FeaturePostRAScheduler,
                                      FeatureEnableSelectOptimize,
                                      FeaturePredictableSelectIsExpensive,
                                      FeatureNoSVEFPLD1R]>;

def TuneNeoverseV2 : SubtargetFeature<"neoversev2", "ARMProcFamily", "NeoverseV2",
                                      "Neoverse V2 ARM processors", [
                                      FeatureFuseAES,
                                      FeatureCmpBccFusion,
                                      FeatureFuseAdrpAdd,
                                      FeatureALULSLFast,
                                      FeaturePostRAScheduler,
                                      FeatureEnableSelectOptimize,
                                      FeatureUseFixedOverScalableIfEqualCost,
                                      FeaturePredictableSelectIsExpensive]>;

def TuneNeoverseV3 : SubtargetFeature<"neoversev3", "ARMProcFamily", "NeoverseV3",
                                      "Neoverse V3 ARM processors", [
                                      FeatureFuseAES,
                                      FeatureALULSLFast,
                                      FeatureFuseAdrpAdd,
                                      FeaturePostRAScheduler,
                                      FeatureEnableSelectOptimize,
                                      FeaturePredictableSelectIsExpensive]>;

def TuneNeoverseV3AE : SubtargetFeature<"neoversev3AE", "ARMProcFamily", "NeoverseV3",
                                      "Neoverse V3AE ARM processors", [
                                      FeatureFuseAES,
                                      FeatureALULSLFast,
                                      FeatureFuseAdrpAdd,
                                      FeaturePostRAScheduler,
                                      FeatureEnableSelectOptimize,
                                      FeaturePredictableSelectIsExpensive]>;

def TuneSaphira  : SubtargetFeature<"saphira", "ARMProcFamily", "Saphira",
                                   "Qualcomm Saphira processors", [
                                   FeaturePostRAScheduler,
                                   FeaturePredictableSelectIsExpensive,
                                   FeatureZCZeroing,
                                   FeatureStorePairSuppress,
                                   FeatureALULSLFast]>;

def TuneThunderX2T99  : SubtargetFeature<"thunderx2t99", "ARMProcFamily", "ThunderX2T99",
                                         "Cavium ThunderX2 processors", [
                                          FeatureAggressiveFMA,
                                          FeatureArithmeticBccFusion,
                                          FeaturePostRAScheduler,
                                          FeatureStorePairSuppress,
                                          FeaturePredictableSelectIsExpensive]>;

def TuneThunderX3T110  : SubtargetFeature<"thunderx3t110", "ARMProcFamily",
                                          "ThunderX3T110",
                                          "Marvell ThunderX3 processors", [
                                           FeatureAggressiveFMA,
                                           FeatureArithmeticBccFusion,
                                           FeaturePostRAScheduler,
                                           FeaturePredictableSelectIsExpensive,
                                           FeatureBalanceFPOps,
                                           FeatureStorePairSuppress,
                                           FeatureStrictAlign]>;

def TuneThunderX : SubtargetFeature<"thunderx", "ARMProcFamily", "ThunderX",
                                    "Cavium ThunderX processors", [
                                    FeaturePostRAScheduler,
                                    FeatureStorePairSuppress,
                                    FeaturePredictableSelectIsExpensive]>;

def TuneThunderXT88 : SubtargetFeature<"thunderxt88", "ARMProcFamily",
                                       "ThunderXT88",
                                       "Cavium ThunderX processors", [
                                       FeaturePostRAScheduler,
                                       FeatureStorePairSuppress,
                                       FeaturePredictableSelectIsExpensive]>;

def TuneThunderXT81 : SubtargetFeature<"thunderxt81", "ARMProcFamily",
                                       "ThunderXT81",
                                       "Cavium ThunderX processors", [
                                       FeaturePostRAScheduler,
                                       FeatureStorePairSuppress,
                                       FeaturePredictableSelectIsExpensive]>;

def TuneThunderXT83 : SubtargetFeature<"thunderxt83", "ARMProcFamily",
                                       "ThunderXT83",
                                       "Cavium ThunderX processors", [
                                       FeaturePostRAScheduler,
                                       FeatureStorePairSuppress,
                                       FeaturePredictableSelectIsExpensive]>;

def TuneTSV110 : SubtargetFeature<"tsv110", "ARMProcFamily", "TSV110",
                                  "HiSilicon TS-V110 processors", [
                                  FeatureFuseAES,
                                  FeatureStorePairSuppress,
                                  FeaturePostRAScheduler]>;

def TuneAmpere1 : SubtargetFeature<"ampere1", "ARMProcFamily", "Ampere1",
                                   "Ampere Computing Ampere-1 processors", [
                                   FeaturePostRAScheduler,
                                   FeatureFuseAES,
                                   FeatureFuseAdrpAdd,
                                   FeatureALULSLFast,
                                   FeatureAggressiveFMA,
                                   FeatureArithmeticBccFusion,
                                   FeatureCmpBccFusion,
                                   FeatureFuseAddress,
                                   FeatureFuseLiterals,
                                   FeatureStorePairSuppress,
                                   FeatureLdpAlignedOnly,
                                   FeatureStpAlignedOnly]>;

def TuneAmpere1A : SubtargetFeature<"ampere1a", "ARMProcFamily", "Ampere1A",
                                    "Ampere Computing Ampere-1A processors", [
                                    FeaturePostRAScheduler,
                                    FeatureFuseAES,
                                    FeatureFuseAdrpAdd,
                                    FeatureALULSLFast,
                                    FeatureAggressiveFMA,
                                    FeatureArithmeticBccFusion,
                                    FeatureCmpBccFusion,
                                    FeatureFuseAddress,
                                    FeatureFuseLiterals,
                                    FeatureFuseAddSub2RegAndConstOne,
                                    FeatureStorePairSuppress,
                                    FeatureLdpAlignedOnly,
                                    FeatureStpAlignedOnly]>;

def TuneAmpere1B : SubtargetFeature<"ampere1b", "ARMProcFamily", "Ampere1B",
                                    "Ampere Computing Ampere-1B processors", [
                                    FeaturePostRAScheduler,
                                    FeatureFuseAES,
                                    FeatureFuseAdrpAdd,
                                    FeatureALULSLFast,
                                    FeatureAggressiveFMA,
                                    FeatureArithmeticBccFusion,
                                    FeatureCmpBccFusion,
                                    FeatureFuseAddress,
                                    FeatureFuseLiterals,
                                    FeatureStorePairSuppress,
                                    FeatureEnableSelectOptimize,
                                    FeaturePredictableSelectIsExpensive,
                                    FeatureLdpAlignedOnly,
                                    FeatureStpAlignedOnly]>;

def TuneOryon  : SubtargetFeature<"oryon-1", "ARMProcFamily",
                                    "Oryon",
                                    "Nuvia Inc Oryon processors", [
                                    FeatureSHA2,
                                    FeatureAES,
                                    FeatureFPARMv8,
                                    FeatureNEON,
                                    FeatureFuseAES,
                                    FeatureFuseAdrpAdd,
                                    FeatureEnableSelectOptimize,
                                    FeatureFuseCryptoEOR,
                                    FeatureFuseAddress,
                                    FeatureSM4,
                                    FeatureSHA2,
                                    FeatureSHA3,
                                    FeatureAES,
                                    FeatureFullFP16,
                                    FeatureFP16FML,
                                    FeaturePerfMon,
                                    FeatureSPE,
                                    FeaturePostRAScheduler,
                                    HasV8_6aOps]>;

def ProcessorFeatures {
  list<SubtargetFeature> A53  = [HasV8_0aOps, FeatureCRC, FeatureSHA2, FeatureAES,
                                 FeatureFPARMv8, FeatureNEON, FeaturePerfMon];
  list<SubtargetFeature> A55  = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                 FeatureNEON, FeatureFullFP16, FeatureDotProd,
                                 FeatureRCPC, FeaturePerfMon, FeatureCRC,
                                 FeatureLSE, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> A510 = [HasV9_0aOps, FeatureNEON, FeaturePerfMon,
                                 FeatureMatMulInt8, FeatureBF16, FeatureAM,
                                 FeatureMTE, FeatureETE, FeatureSVE2BitPerm,
                                 FeatureFP16FML,
                                 FeatureCCIDX,
                                 FeatureSB, FeaturePAuth, FeatureSSBS, FeatureSVE, FeatureSVE2,
                                 FeatureComplxNum, FeatureCRC, FeatureDotProd,
                                 FeatureFPARMv8,FeatureFullFP16, FeatureJS, FeatureLSE,
                                 FeatureRAS, FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> A520 = [HasV9_2aOps, FeaturePerfMon, FeatureAM,
                                 FeatureMTE, FeatureETE, FeatureSVE2BitPerm,
                                 FeatureFP16FML,
                                 FeatureCCIDX,
                                 FeatureSB, FeatureSSBS, FeaturePAuth, FeatureFlagM, FeaturePredRes,
                                 FeatureSVE, FeatureSVE2, FeatureBF16, FeatureComplxNum, FeatureCRC,
                                 FeatureFPARMv8, FeatureFullFP16, FeatureMatMulInt8, FeatureJS,
                                 FeatureNEON, FeatureLSE, FeatureRAS, FeatureRCPC, FeatureRDM,
                                 FeatureDotProd];
  list<SubtargetFeature> A520AE = [HasV9_2aOps, FeaturePerfMon, FeatureAM,
                                 FeatureMTE, FeatureETE, FeatureSVE2BitPerm,
                                 FeatureFP16FML,
                                 FeatureCCIDX,
                                 FeatureSB, FeatureSSBS, FeaturePAuth, FeatureFlagM, FeaturePredRes,
                                 FeatureSVE, FeatureSVE2, FeatureBF16, FeatureComplxNum, FeatureCRC,
                                 FeatureFPARMv8, FeatureFullFP16, FeatureMatMulInt8, FeatureJS,
                                 FeatureNEON, FeatureLSE, FeatureRAS, FeatureRCPC, FeatureRDM,
                                 FeatureDotProd];
  list<SubtargetFeature> A65  = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                 FeatureNEON, FeatureFullFP16, FeatureDotProd,
                                 FeatureRCPC, FeatureSSBS, FeatureRAS,
                                 FeaturePerfMon, FeatureCRC, FeatureLSE, FeatureRDM];
  list<SubtargetFeature> A76  = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                 FeatureNEON, FeatureFullFP16, FeatureDotProd,
                                 FeatureRCPC, FeatureSSBS, FeaturePerfMon,
                                 FeatureCRC, FeatureLSE, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> A77  = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                 FeatureNEON, FeatureFullFP16, FeatureDotProd,
                                 FeatureRCPC, FeaturePerfMon, FeatureSSBS,
                                 FeatureCRC, FeatureLSE, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> A78  = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                 FeatureNEON, FeatureFullFP16, FeatureDotProd,
                                 FeatureRCPC, FeaturePerfMon, FeatureSPE,
                                 FeatureSSBS, FeatureCRC, FeatureLSE, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> A78AE = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                  FeatureNEON, FeatureFullFP16, FeatureDotProd,
                                  FeatureRCPC, FeaturePerfMon, FeatureSPE,
                                  FeatureSSBS, FeatureCRC, FeatureLSE, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> A78C = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                 FeatureNEON, FeatureFullFP16, FeatureDotProd,
                                 FeatureFlagM, FeaturePAuth,
                                 FeaturePerfMon, FeatureRCPC, FeatureSPE,
                                 FeatureSSBS, FeatureCRC, FeatureLSE, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> A710 = [HasV9_0aOps, FeatureNEON, FeaturePerfMon,
                                 FeatureCCIDX, FeatureSSBS,
                                 FeatureETE, FeatureMTE, FeatureFP16FML,
                                 FeatureSVE2BitPerm, FeatureBF16, FeatureMatMulInt8,
                                 FeaturePAuth, FeatureFlagM, FeatureSB, FeatureSVE, FeatureSVE2,
                                 FeatureComplxNum, FeatureCRC, FeatureDotProd, FeatureFPARMv8,
                                 FeatureFullFP16, FeatureJS, FeatureLSE, FeatureRAS, FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> A715 = [HasV9_0aOps, FeatureNEON, FeatureMTE,
                                 FeatureCCIDX,
                                 FeatureFP16FML, FeatureSVE, FeatureTRBE,
                                 FeatureSVE2BitPerm, FeatureBF16, FeatureETE,
                                 FeaturePerfMon, FeatureMatMulInt8, FeatureSPE,
                                 FeatureSB, FeatureSSBS, FeatureFullFP16, FeaturePAuth, FeaturePredRes, FeatureFlagM,
                                 FeatureSVE2, FeatureComplxNum, FeatureCRC,
                                 FeatureDotProd, FeatureFPARMv8,
                                 FeatureJS, FeatureLSE, FeatureRAS,
                                 FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> A720 = [HasV9_2aOps, FeatureMTE, FeatureFP16FML,
                                 FeatureCCIDX,
                                 FeatureTRBE, FeatureSVE2BitPerm, FeatureETE,
                                 FeaturePerfMon, FeatureSPE, FeatureSPE_EEF,
                                 FeatureSB, FeatureSSBS, FeaturePAuth, FeatureFlagM, FeaturePredRes,
                                 FeatureSVE, FeatureSVE2, FeatureBF16, FeatureComplxNum, FeatureCRC,
                                 FeatureDotProd, FeatureFPARMv8, FeatureFullFP16, FeatureMatMulInt8,
                                 FeatureJS, FeatureLSE, FeatureNEON, FeatureRAS,
                                 FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> A720AE = [HasV9_2aOps, FeatureMTE, FeatureFP16FML,
                                 FeatureCCIDX,
                                 FeatureTRBE, FeatureSVE2BitPerm, FeatureETE,
                                 FeaturePerfMon, FeatureSPE, FeatureSPE_EEF,
                                 FeatureSB, FeatureSSBS, FeaturePAuth, FeatureFlagM, FeaturePredRes,
                                 FeatureSVE, FeatureSVE2, FeatureBF16, FeatureComplxNum, FeatureCRC,
                                 FeatureDotProd, FeatureFPARMv8, FeatureFullFP16, FeatureMatMulInt8,
                                 FeatureJS, FeatureLSE, FeatureNEON, FeatureRAS,
                                 FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> A725 = [HasV9_2aOps, FeatureMTE, FeatureFP16FML,
                                 FeatureCCIDX,
                                 FeatureETE, FeaturePerfMon, FeatureSPE,
                                 FeatureSVE2BitPerm, FeatureSPE_EEF, FeatureTRBE,
                                 FeatureFlagM, FeaturePredRes, FeatureSB, FeatureSSBS,
                                 FeatureSVE, FeatureSVE2, FeatureBF16, FeatureComplxNum, FeatureCRC,
                                 FeatureDotProd, FeatureFPARMv8, FeatureFullFP16, FeatureMatMulInt8,
                                 FeatureJS, FeatureLSE, FeatureNEON, FeaturePAuth, FeatureRAS,
                                 FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> R82  = [HasV8_0rOps, FeaturePerfMon, FeatureFullFP16,
                                 FeatureFP16FML, FeatureSSBS, FeaturePredRes,
                                 FeatureSB, FeatureRDM, FeatureDotProd,
                                 FeatureComplxNum, FeatureJS,
                                 FeatureCacheDeepPersist,
                                 FeatureFlagM, FeatureCRC, FeatureLSE, FeatureRAS, FeatureFPARMv8,
                                 FeatureNEON, FeaturePAuth, FeatureRCPC];
  list<SubtargetFeature> R82AE = [HasV8_0rOps, FeaturePerfMon, FeatureFullFP16,
                                  FeatureFP16FML, FeatureSSBS, FeaturePredRes,
                                  FeatureSB, FeatureRDM, FeatureDotProd,
                                  FeatureComplxNum, FeatureJS,
                                  FeatureCacheDeepPersist,
                                  FeatureLSE, FeatureFlagM, FeatureCRC, FeatureFPARMv8, FeatureNEON,
                                  FeaturePAuth, FeatureRAS, FeatureRCPC];
  list<SubtargetFeature> X1   = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                 FeatureNEON, FeatureRCPC, FeaturePerfMon,
                                 FeatureSPE, FeatureFullFP16, FeatureDotProd,
                                 FeatureSSBS, FeatureCRC, FeatureLSE, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> X1C  = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                 FeatureNEON, FeatureRCPC_IMMO, FeaturePerfMon,
                                 FeatureSPE, FeatureFullFP16, FeatureDotProd,
                                 FeaturePAuth, FeatureSSBS, FeatureFlagM,
                                 FeatureLSE2,
                                 FeatureRCPC, FeatureCRC, FeatureLSE, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> X2   = [HasV9_0aOps, FeatureNEON, FeaturePerfMon,
                                 FeatureMatMulInt8, FeatureBF16, FeatureAM,
                                 FeatureMTE, FeatureETE, FeatureSVE2BitPerm,
                                 FeatureFP16FML,
                                 FeatureCCIDX,
                                 FeaturePAuth, FeatureSSBS, FeatureSB, FeatureSVE, FeatureSVE2, FeatureFlagM,
                                 FeatureComplxNum, FeatureCRC, FeatureDotProd, FeatureFPARMv8, FeatureFullFP16,
                                 FeatureJS, FeatureLSE, FeatureRAS, FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> X3 =   [HasV9_0aOps, FeatureSVE, FeatureNEON,
                                 FeaturePerfMon, FeatureETE, FeatureTRBE,
                                 FeatureSPE, FeatureBF16, FeatureMatMulInt8,
                                 FeatureMTE, FeatureSVE2BitPerm, FeatureFullFP16,
                                 FeatureFP16FML,
                                 FeatureCCIDX,
                                 FeatureSB, FeaturePAuth, FeaturePredRes, FeatureFlagM, FeatureSSBS,
                                 FeatureSVE2, FeatureComplxNum, FeatureCRC, FeatureFPARMv8, FeatureJS,
                                 FeatureLSE, FeatureRAS, FeatureRCPC, FeatureRDM, FeatureDotProd];
  list<SubtargetFeature> X4 =   [HasV9_2aOps,
                                 FeaturePerfMon, FeatureETE, FeatureTRBE,
                                 FeatureSPE, FeatureMTE, FeatureSVE2BitPerm,
                                 FeatureFP16FML, FeatureSPE_EEF,
                                 FeatureCCIDX,
                                 FeatureSB, FeatureSSBS, FeaturePAuth, FeatureFlagM, FeaturePredRes,
                                 FeatureSVE, FeatureSVE2, FeatureComplxNum, FeatureCRC, FeatureDotProd,
                                 FeatureFPARMv8, FeatureFullFP16, FeatureMatMulInt8, FeatureJS, FeatureLSE,
                                 FeatureNEON, FeatureRAS, FeatureRCPC, FeatureRDM, FeatureBF16];
  list<SubtargetFeature> X925 = [HasV9_2aOps, FeatureMTE, FeatureFP16FML,
                                 FeatureCCIDX,
                                 FeatureETE, FeaturePerfMon, FeatureSPE,
                                 FeatureSVE2BitPerm, FeatureSPE_EEF, FeatureTRBE,
                                 FeatureFlagM, FeaturePredRes, FeatureSB, FeatureSSBS,
                                 FeatureSVE, FeatureSVE2, FeatureBF16, FeatureComplxNum, FeatureCRC,
                                 FeatureDotProd, FeatureFPARMv8, FeatureFullFP16, FeatureMatMulInt8,
                                 FeatureJS, FeatureLSE, FeatureNEON, FeaturePAuth, FeatureRAS,
                                 FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> A64FX    = [HasV8_2aOps, FeatureFPARMv8, FeatureNEON,
                                     FeatureSHA2, FeaturePerfMon, FeatureFullFP16,
                                     FeatureSVE, FeatureComplxNum,
                                     FeatureAES, FeatureCRC, FeatureLSE, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> Carmel   = [HasV8_2aOps, FeatureNEON, FeatureSHA2, FeatureAES,
                                     FeatureFullFP16, FeatureCRC, FeatureLSE, FeatureRAS, FeatureRDM,
                                     FeatureFPARMv8];
  list<SubtargetFeature> AppleA7  = [HasV8_0aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                     FeatureNEON,FeaturePerfMon, FeatureAppleA7SysReg];
  list<SubtargetFeature> AppleA10 = [HasV8_0aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                     FeatureNEON, FeaturePerfMon, FeatureCRC,
                                     FeatureRDM, FeaturePAN, FeatureLOR, FeatureVH];
  list<SubtargetFeature> AppleA11 = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                     FeatureNEON, FeaturePerfMon, FeatureFullFP16, FeatureCRC,
                                     FeatureLSE, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> AppleA12 = [HasV8_3aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                     FeatureNEON, FeaturePerfMon, FeatureFullFP16,
                                     FeatureComplxNum, FeatureCRC, FeatureJS, FeatureLSE,
                                     FeaturePAuth, FeatureRAS, FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> AppleA13 = [HasV8_4aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                     FeatureNEON, FeaturePerfMon, FeatureFullFP16,
                                     FeatureFP16FML, FeatureSHA3, FeatureComplxNum, FeatureCRC, FeatureJS, FeatureLSE,
                                     FeaturePAuth, FeatureRAS, FeatureRCPC, FeatureRDM, FeatureDotProd];
  list<SubtargetFeature> AppleA14 = [HasV8_4aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                     FeatureNEON, FeaturePerfMon,
                                     FeatureFullFP16, FeatureFP16FML, FeatureSHA3,
                                     // ArmV8.5-a extensions, excluding BTI:
                                     FeatureAltFPCmp, FeatureFRInt3264,
                                     FeatureSpecRestrict, FeatureSSBS, FeatureSB,
                                     FeaturePredRes, FeatureCacheDeepPersist,
                                     FeatureComplxNum, FeatureCRC, FeatureJS, FeatureLSE,
                                     FeaturePAuth, FeatureRAS, FeatureRCPC, FeatureRDM,
                                     FeatureDotProd];
  list<SubtargetFeature> AppleA15 = [HasV8_6aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                     FeatureNEON, FeaturePerfMon, FeatureSHA3,
                                     FeatureFullFP16, FeatureFP16FML,
                                     FeatureComplxNum, FeatureCRC, FeatureJS,
                                     FeatureLSE, FeaturePAuth,
                                     FeatureRAS, FeatureRCPC, FeatureRDM,
                                     FeatureBF16, FeatureDotProd, FeatureMatMulInt8, FeatureSSBS];
  list<SubtargetFeature> AppleA16 = [HasV8_6aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                     FeatureNEON, FeaturePerfMon, FeatureSHA3,
                                     FeatureFullFP16, FeatureFP16FML,
                                     FeatureHCX,
                                     FeatureComplxNum, FeatureCRC, FeatureJS,
                                     FeatureLSE, FeaturePAuth,
                                     FeatureRAS, FeatureRCPC, FeatureRDM,
                                     FeatureBF16, FeatureDotProd, FeatureMatMulInt8, FeatureSSBS];
  list<SubtargetFeature> AppleA17 = [HasV8_6aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                     FeatureNEON, FeaturePerfMon, FeatureSHA3,
                                     FeatureFullFP16, FeatureFP16FML,
                                     FeatureHCX,
                                     FeatureComplxNum, FeatureCRC, FeatureJS,
                                     FeatureLSE, FeaturePAuth,
                                     FeatureRAS, FeatureRCPC, FeatureRDM,
                                     FeatureBF16, FeatureDotProd, FeatureMatMulInt8, FeatureSSBS];
  // Technically apple-m4 is v9.2a, but we can't use that here.
  // Historically, llvm defined v9.0a as requiring SVE, but it's optional
  // according to the Arm ARM, and not supported by the core.  We decoupled the
  // two in the clang driver and in the backend subtarget features, but it's
  // still an issue in the clang frontend.  v8.7a is the next closest choice.
  list<SubtargetFeature> AppleM4 = [HasV8_7aOps, FeatureSHA2, FeatureFPARMv8,
                                    FeatureNEON, FeaturePerfMon, FeatureSHA3,
                                    FeatureFullFP16, FeatureFP16FML,
                                    FeatureAES, FeatureBF16,
                                    FeatureSME, FeatureSME2,
                                    FeatureSMEF64F64, FeatureSMEI16I64,
                                    FeatureComplxNum, FeatureCRC, FeatureJS, FeatureLSE,
                                    FeaturePAuth, FeatureRAS, FeatureRCPC, FeatureRDM,
                                    FeatureDotProd, FeatureMatMulInt8];
  list<SubtargetFeature> ExynosM3 = [HasV8_0aOps, FeatureCRC, FeatureSHA2, FeatureAES,
                                     FeaturePerfMon, FeatureNEON, FeatureFPARMv8];
  list<SubtargetFeature> ExynosM4 = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureDotProd,
                                     FeatureFullFP16, FeaturePerfMon, FeatureCRC, FeatureFPARMv8,
                                     FeatureLSE, FeatureNEON, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> Falkor   = [HasV8_0aOps, FeatureCRC, FeatureSHA2, FeatureAES,
                                     FeatureFPARMv8, FeatureNEON, FeaturePerfMon,
                                     FeatureRDM];
  list<SubtargetFeature> NeoverseE1 = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureDotProd,
                                       FeatureFPARMv8, FeatureFullFP16, FeatureNEON,
                                       FeatureRCPC, FeatureSSBS, FeaturePerfMon, FeatureCRC,
                                       FeatureLSE, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> NeoverseN1 = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureDotProd,
                                       FeatureFPARMv8, FeatureFullFP16, FeatureNEON,
                                       FeatureRCPC, FeatureSPE, FeatureSSBS,
                                       FeaturePerfMon, FeatureCRC, FeatureLSE, FeatureRAS, FeatureRDM];
  list<SubtargetFeature> NeoverseN2 = [HasV9_0aOps, FeatureBF16, FeatureETE, FeatureFP16FML,
                                       FeatureMatMulInt8, FeatureMTE, FeatureSVE2,
                                       FeatureSVE2BitPerm, FeatureTRBE,
                                       FeaturePerfMon,
                                       FeatureCCIDX,
                                       FeatureDotProd, FeatureFullFP16, FeatureSB, FeatureSSBS, FeatureSVE,
                                       FeatureComplxNum, FeatureCRC, FeatureFPARMv8, FeatureJS, FeatureLSE,
                                       FeatureNEON, FeaturePAuth, FeatureRAS, FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> NeoverseN3 = [HasV9_2aOps, FeatureETE, FeatureFP16FML,
                                      FeatureFullFP16, FeatureMTE, FeaturePerfMon,
                                      FeatureRandGen, FeatureSPE, FeatureSPE_EEF,
                                      FeatureSVE2BitPerm,
                                      FeatureCCIDX,
                                      FeatureSSBS, FeatureSB, FeaturePredRes, FeaturePAuth, FeatureFlagM,
                                      FeatureSVE, FeatureSVE2, FeatureBF16, FeatureComplxNum,
                                      FeatureCRC, FeatureDotProd, FeatureFPARMv8, FeatureMatMulInt8,
                                      FeatureJS, FeatureLSE, FeatureRAS, FeatureRCPC, FeatureRDM,
                                      FeatureNEON];
  list<SubtargetFeature> Neoverse512TVB = [HasV8_4aOps, FeatureBF16, FeatureCacheDeepPersist,
                                           FeatureSHA2, FeatureAES, FeatureFPARMv8, FeatureFP16FML,
                                           FeatureFullFP16, FeatureMatMulInt8, FeatureNEON,
                                           FeaturePerfMon, FeatureRandGen, FeatureSPE,
                                           FeatureSSBS, FeatureSVE,
                                           FeatureCCIDX,
                                           FeatureSHA3, FeatureSM4, FeatureDotProd, FeatureComplxNum,
                                           FeatureCRC, FeatureJS, FeatureLSE, FeaturePAuth, FeatureRAS,
                                           FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> NeoverseV1 = [HasV8_4aOps, FeatureBF16, FeatureCacheDeepPersist,
                                       FeatureSHA2, FeatureAES, FeatureFPARMv8, FeatureFP16FML,
                                       FeatureFullFP16, FeatureMatMulInt8, FeatureNEON,
                                       FeaturePerfMon, FeatureRandGen, FeatureSPE,
                                       FeatureSSBS, FeatureSVE,
                                       FeatureCCIDX,
                                       FeatureSHA3, FeatureSM4, FeatureDotProd, FeatureComplxNum,
                                       FeatureCRC, FeatureJS, FeatureLSE, FeaturePAuth, FeatureRAS,
                                       FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> NeoverseV2 = [HasV9_0aOps, FeatureBF16, FeatureSPE,
                                       FeaturePerfMon, FeatureETE, FeatureMatMulInt8,
                                       FeatureNEON, FeatureSVE2BitPerm, FeatureFP16FML,
                                       FeatureMTE, FeatureRandGen,
                                       FeatureCCIDX,
                                       FeatureSVE, FeatureSVE2, FeatureSSBS, FeatureFullFP16, FeatureDotProd,
                                       FeatureComplxNum, FeatureCRC, FeatureFPARMv8, FeatureJS, FeatureLSE,
                                       FeaturePAuth, FeatureRAS, FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> NeoverseV3 = [HasV9_2aOps, FeatureETE, FeatureFP16FML,
                                      FeatureFullFP16, FeatureLS64, FeatureMTE,
                                      FeaturePerfMon, FeatureRandGen, FeatureSPE,
                                      FeatureCCIDX,
                                      FeatureSPE_EEF, FeatureSVE2BitPerm, FeatureBRBE,
                                      FeatureSSBS, FeatureSB, FeaturePredRes, FeaturePAuth, FeatureFlagM,
                                      FeatureSVE, FeatureSVE2, FeatureBF16, FeatureComplxNum, FeatureCRC,
                                      FeatureDotProd, FeatureFPARMv8, FeatureMatMulInt8, FeatureJS, FeatureLSE,
                                      FeatureNEON, FeatureRAS, FeatureRCPC, FeatureRDM, FeatureRME];
  list<SubtargetFeature> NeoverseV3AE = [HasV9_2aOps, FeatureETE, FeatureFP16FML,
                                      FeatureFullFP16, FeatureLS64, FeatureMTE,
                                      FeaturePerfMon, FeatureRandGen, FeatureSPE,
                                      FeatureSPE_EEF, FeatureSVE2BitPerm, FeatureBRBE,
                                      FeatureSSBS, FeatureSB, FeaturePredRes, FeaturePAuth, FeatureFlagM,
                                      FeatureCCIDX,
                                      FeatureSVE, FeatureSVE2, FeatureBF16, FeatureComplxNum, FeatureCRC,
                                      FeatureDotProd, FeatureFPARMv8, FeatureMatMulInt8, FeatureJS,
                                      FeatureLSE, FeatureNEON, FeatureRAS, FeatureRCPC, FeatureRDM,
                                      FeatureRME];
  list<SubtargetFeature> Saphira    = [HasV8_4aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                       FeatureNEON, FeatureSPE, FeaturePerfMon, FeatureCRC,
                                       FeatureCCIDX,
                                       FeatureLSE, FeatureRDM, FeatureRAS, FeatureRCPC];
  list<SubtargetFeature> ThunderX   = [HasV8_0aOps, FeatureCRC, FeatureSHA2, FeatureAES,
                                       FeatureFPARMv8, FeaturePerfMon, FeatureNEON];
  list<SubtargetFeature> ThunderX2T99  = [HasV8_1aOps, FeatureCRC, FeatureSHA2, FeatureAES,
                                          FeatureFPARMv8, FeatureNEON, FeatureLSE,
                                          FeatureRDM];
  list<SubtargetFeature> ThunderX3T110 = [HasV8_3aOps, FeatureCRC, FeatureSHA2, FeatureAES,
                                          FeatureFPARMv8, FeatureNEON, FeatureLSE,
                                          FeatureCCIDX,
                                          FeaturePAuth, FeaturePerfMon, FeatureComplxNum,
                                          FeatureJS, FeatureRAS, FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> TSV110 = [HasV8_2aOps, FeatureSHA2, FeatureAES, FeatureFPARMv8,
                                   FeatureNEON, FeaturePerfMon, FeatureSPE,
                                   FeatureFullFP16, FeatureFP16FML, FeatureDotProd,
                                   FeatureJS, FeatureComplxNum, FeatureCRC, FeatureLSE,
                                   FeatureRAS, FeatureRDM];
  list<SubtargetFeature> Ampere1 = [HasV8_6aOps, FeatureNEON, FeaturePerfMon,
                                    FeatureSSBS, FeatureRandGen, FeatureSB,
                                    FeatureSHA2, FeatureSHA3, FeatureAES,
                                    FeatureFullFP16, FeatureBF16, FeatureComplxNum, FeatureCRC,
                                    FeatureDotProd, FeatureFPARMv8, FeatureMatMulInt8, FeatureJS,
                                    FeatureCCIDX,
                                    FeatureLSE, FeaturePAuth, FeatureRAS, FeatureRCPC, FeatureRDM];
  list<SubtargetFeature> Ampere1A = [HasV8_6aOps, FeatureNEON, FeaturePerfMon,
                                     FeatureMTE, FeatureSSBS, FeatureRandGen,
                                     FeatureSB, FeatureSM4, FeatureSHA2,
                                     FeatureSHA3, FeatureAES,
                                     FeatureFullFP16, FeatureBF16, FeatureComplxNum,
                                     FeatureCRC, FeatureDotProd, FeatureFPARMv8, FeatureMatMulInt8,
                                     FeatureJS, FeatureLSE, FeaturePAuth, FeatureRAS, FeatureRCPC,
                                     FeatureCCIDX,
                                     FeatureRDM];
  list<SubtargetFeature> Ampere1B = [HasV8_7aOps, FeatureNEON, FeaturePerfMon,
                                     FeatureMTE, FeatureSSBS, FeatureRandGen,
                                     FeatureSB, FeatureSM4, FeatureSHA2,
                                     FeatureSHA3, FeatureAES, FeatureCSSC,
                                     FeatureWFxT, FeatureFullFP16, FeatureBF16, FeatureComplxNum,
                                     FeatureCRC, FeatureDotProd, FeatureFPARMv8, FeatureMatMulInt8,
                                     FeatureJS, FeatureLSE, FeaturePAuth, FeatureRAS, FeatureRCPC,
                                     FeatureCCIDX,
                                     FeatureRDM];

  list<SubtargetFeature> Oryon = [HasV8_6aOps, FeatureNEON, FeaturePerfMon,
                                     FeatureRandGen,
                                     FeaturePAuth, FeatureSM4, FeatureSHA2,
                                     FeatureSHA3, FeatureAES,
                                     FeatureSPE, FeatureBF16, FeatureComplxNum, FeatureCRC,
                                     FeatureDotProd, FeatureFPARMv8, FeatureMatMulInt8,
                                     FeatureSSBS, FeatureCCIDX,
                                     FeatureJS, FeatureLSE, FeatureRAS, FeatureRCPC, FeatureRDM];

  // ETE and TRBE are future architecture extensions. We temporarily enable them
  // by default for users targeting generic AArch64. The extensions do not
  // affect code generated by the compiler and can be used only by explicitly
  // mentioning the new system register names in assembly.
  list<SubtargetFeature> Generic = [FeatureFPARMv8, FeatureNEON, FeatureETE];
}

// Define an alternative name for a given Processor.
class ProcessorAlias<string n, string alias> {
  string Name = n;
  string Alias = alias;
}

// FeatureFuseAdrpAdd is enabled under Generic to allow linker merging
// optimizations.
def : ProcessorModel<"generic", CortexA510Model, ProcessorFeatures.Generic,
                     [FeatureFuseAES, FeatureFuseAdrpAdd, FeaturePostRAScheduler,
                      FeatureEnableSelectOptimize]>;
def : ProcessorModel<"cortex-a35", CortexA53Model, ProcessorFeatures.A53,
                     [TuneA35]>;
def : ProcessorModel<"cortex-a34", CortexA53Model, ProcessorFeatures.A53,
                     [TuneA35]>;
def : ProcessorModel<"cortex-a53", CortexA53Model, ProcessorFeatures.A53,
                     [TuneA53]>;
def : ProcessorModel<"cortex-a55", CortexA55Model, ProcessorFeatures.A55,
                     [TuneA55]>;
def : ProcessorModel<"cortex-a510", CortexA510Model, ProcessorFeatures.A510,
                     [TuneA510]>;
def : ProcessorModel<"cortex-a520", CortexA510Model, ProcessorFeatures.A520,
                     [TuneA520]>;
def : ProcessorModel<"cortex-a520ae", CortexA510Model, ProcessorFeatures.A520AE,
                     [TuneA520AE]>;
def : ProcessorModel<"cortex-a57", CortexA57Model, ProcessorFeatures.A53,
                     [TuneA57]>;
def : ProcessorModel<"cortex-a65", CortexA53Model, ProcessorFeatures.A65,
                     [TuneA65]>;
def : ProcessorModel<"cortex-a65ae", CortexA53Model, ProcessorFeatures.A65,
                     [TuneA65]>;
def : ProcessorModel<"cortex-a72", CortexA57Model, ProcessorFeatures.A53,
                     [TuneA72]>;
def : ProcessorModel<"cortex-a73", CortexA57Model, ProcessorFeatures.A53,
                     [TuneA73]>;
def : ProcessorModel<"cortex-a75", CortexA57Model, ProcessorFeatures.A55,
                     [TuneA75]>;
def : ProcessorModel<"cortex-a76", CortexA57Model, ProcessorFeatures.A76,
                     [TuneA76]>;
def : ProcessorModel<"cortex-a76ae", CortexA57Model, ProcessorFeatures.A76,
                     [TuneA76]>;
def : ProcessorModel<"cortex-a77", CortexA57Model, ProcessorFeatures.A77,
                     [TuneA77]>;
def : ProcessorModel<"cortex-a78", CortexA57Model, ProcessorFeatures.A78,
                     [TuneA78]>;
def : ProcessorModel<"cortex-a78ae", CortexA57Model, ProcessorFeatures.A78AE,
                     [TuneA78AE]>;
def : ProcessorModel<"cortex-a78c", CortexA57Model, ProcessorFeatures.A78C,
                     [TuneA78C]>;
def : ProcessorModel<"cortex-a710", NeoverseN2Model, ProcessorFeatures.A710,
                     [TuneA710]>;
def : ProcessorModel<"cortex-a715", NeoverseN2Model, ProcessorFeatures.A715,
                     [TuneA715]>;
def : ProcessorModel<"cortex-a720", NeoverseN2Model, ProcessorFeatures.A720,
                     [TuneA720]>;
def : ProcessorModel<"cortex-a720ae", NeoverseN2Model, ProcessorFeatures.A720AE,
                     [TuneA720AE]>;
def : ProcessorModel<"cortex-a725", NeoverseN2Model, ProcessorFeatures.A725,
                     [TuneA725]>;
def : ProcessorModel<"cortex-r82", CortexA55Model, ProcessorFeatures.R82,
                     [TuneR82]>;
def : ProcessorModel<"cortex-r82ae", CortexA55Model, ProcessorFeatures.R82AE,
                     [TuneR82AE]>;
def : ProcessorModel<"cortex-x1", CortexA57Model, ProcessorFeatures.X1,
                     [TuneX1]>;
def : ProcessorModel<"cortex-x1c", CortexA57Model, ProcessorFeatures.X1C,
                     [TuneX1]>;
def : ProcessorModel<"cortex-x2", NeoverseN2Model, ProcessorFeatures.X2,
                     [TuneX2]>;
def : ProcessorModel<"cortex-x3", NeoverseN2Model, ProcessorFeatures.X3,
                     [TuneX3]>;
def : ProcessorModel<"cortex-x4", NeoverseN2Model, ProcessorFeatures.X4,
                     [TuneX4]>;
def : ProcessorModel<"cortex-x925", NeoverseV2Model, ProcessorFeatures.X925,
                     [TuneX925]>;
def : ProcessorModel<"neoverse-e1", CortexA53Model,
                     ProcessorFeatures.NeoverseE1, [TuneNeoverseE1]>;
def : ProcessorModel<"neoverse-n1", NeoverseN1Model,
                     ProcessorFeatures.NeoverseN1, [TuneNeoverseN1]>;
def : ProcessorModel<"neoverse-n2", NeoverseN2Model,
                     ProcessorFeatures.NeoverseN2, [TuneNeoverseN2]>;
def : ProcessorAlias<"cobalt-100", "neoverse-n2">;
def : ProcessorModel<"neoverse-n3", NeoverseN2Model,
                     ProcessorFeatures.NeoverseN3, [TuneNeoverseN3]>;
def : ProcessorModel<"neoverse-512tvb", NeoverseV1Model,
                     ProcessorFeatures.Neoverse512TVB, [TuneNeoverse512TVB]>;
def : ProcessorModel<"neoverse-v1", NeoverseV1Model,
                     ProcessorFeatures.NeoverseV1, [TuneNeoverseV1]>;
def : ProcessorModel<"neoverse-v2", NeoverseV2Model,
                     ProcessorFeatures.NeoverseV2, [TuneNeoverseV2]>;
def : ProcessorAlias<"grace", "neoverse-v2">;
def : ProcessorModel<"neoverse-v3", NeoverseV2Model,
                     ProcessorFeatures.NeoverseV3, [TuneNeoverseV3]>;
def : ProcessorModel<"neoverse-v3ae", NeoverseV2Model,
                     ProcessorFeatures.NeoverseV3AE, [TuneNeoverseV3AE]>;
def : ProcessorModel<"exynos-m3", ExynosM3Model, ProcessorFeatures.ExynosM3,
                     [TuneExynosM3]>;
def : ProcessorModel<"exynos-m4", ExynosM4Model, ProcessorFeatures.ExynosM4,
                     [TuneExynosM4]>;
def : ProcessorModel<"exynos-m5", ExynosM5Model, ProcessorFeatures.ExynosM4,
                     [TuneExynosM4]>;
def : ProcessorModel<"falkor", FalkorModel, ProcessorFeatures.Falkor,
                     [TuneFalkor]>;
def : ProcessorModel<"saphira", FalkorModel, ProcessorFeatures.Saphira,
                     [TuneSaphira]>;
def : ProcessorModel<"kryo", KryoModel, ProcessorFeatures.A53, [TuneKryo]>;

// Cavium ThunderX/ThunderX T8X  Processors
def : ProcessorModel<"thunderx", ThunderXT8XModel,  ProcessorFeatures.ThunderX,
                     [TuneThunderX]>;
def : ProcessorModel<"thunderxt88", ThunderXT8XModel,
                     ProcessorFeatures.ThunderX, [TuneThunderXT88]>;
def : ProcessorModel<"thunderxt81", ThunderXT8XModel,
                     ProcessorFeatures.ThunderX, [TuneThunderXT81]>;
def : ProcessorModel<"thunderxt83", ThunderXT8XModel,
                     ProcessorFeatures.ThunderX, [TuneThunderXT83]>;
// Cavium ThunderX2T9X  Processors. Formerly Broadcom Vulcan.
def : ProcessorModel<"thunderx2t99", ThunderX2T99Model,
                     ProcessorFeatures.ThunderX2T99, [TuneThunderX2T99]>;
// Marvell ThunderX3T110 Processors.
def : ProcessorModel<"thunderx3t110", ThunderX3T110Model,
                     ProcessorFeatures.ThunderX3T110, [TuneThunderX3T110]>;
def : ProcessorModel<"tsv110", TSV110Model, ProcessorFeatures.TSV110,
                     [TuneTSV110]>;


// Apple CPUs

def : ProcessorModel<"apple-a7", CycloneModel, ProcessorFeatures.AppleA7,
                     [TuneAppleA7]>;
// Support cyclone as an alias for apple-a7 so we can still LTO old bitcode.
def : ProcessorAlias<"cyclone", "apple-a7">;
def : ProcessorAlias<"apple-a8", "apple-a7">;
def : ProcessorAlias<"apple-a9", "apple-a7">;

def : ProcessorModel<"apple-a10", CycloneModel, ProcessorFeatures.AppleA10,
                     [TuneAppleA10]>;

def : ProcessorModel<"apple-a11", CycloneModel, ProcessorFeatures.AppleA11,
                     [TuneAppleA11]>;

def : ProcessorModel<"apple-a12", CycloneModel, ProcessorFeatures.AppleA12,
                     [TuneAppleA12]>;
def : ProcessorAlias<"apple-s4", "apple-a12">;
def : ProcessorAlias<"apple-s5", "apple-a12">;

def : ProcessorModel<"apple-a13", CycloneModel, ProcessorFeatures.AppleA13,
                     [TuneAppleA13]>;

def : ProcessorModel<"apple-a14", CycloneModel, ProcessorFeatures.AppleA14,
                     [TuneAppleA14]>;
def : ProcessorAlias<"apple-m1", "apple-a14">;

def : ProcessorModel<"apple-a15", CycloneModel, ProcessorFeatures.AppleA15,
                     [TuneAppleA15]>;
def : ProcessorAlias<"apple-m2", "apple-a15">;

def : ProcessorModel<"apple-a16", CycloneModel, ProcessorFeatures.AppleA16,
                     [TuneAppleA16]>;
def : ProcessorAlias<"apple-m3", "apple-a16">;

def : ProcessorModel<"apple-a17", CycloneModel, ProcessorFeatures.AppleA17,
                     [TuneAppleA17]>;

def : ProcessorModel<"apple-m4", CycloneModel, ProcessorFeatures.AppleM4,
                     [TuneAppleM4]>;

// Alias for the latest Apple processor model supported by LLVM.
def : ProcessorAlias<"apple-latest", "apple-m4">;


// Fujitsu A64FX
def : ProcessorModel<"a64fx", A64FXModel, ProcessorFeatures.A64FX,
                     [TuneA64FX]>;

// Nvidia Carmel
def : ProcessorModel<"carmel", NoSchedModel, ProcessorFeatures.Carmel,
                     [TuneCarmel]>;

// Ampere Computing
def : ProcessorModel<"ampere1", Ampere1Model, ProcessorFeatures.Ampere1,
                     [TuneAmpere1]>;

def : ProcessorModel<"ampere1a", Ampere1Model, ProcessorFeatures.Ampere1A,
                     [TuneAmpere1A]>;

def : ProcessorModel<"ampere1b", Ampere1BModel, ProcessorFeatures.Ampere1B,
                     [TuneAmpere1B]>;

// Qualcomm Oryon
def : ProcessorModel<"oryon-1", OryonModel, ProcessorFeatures.Oryon,
                       [TuneOryon]>;
