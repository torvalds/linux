//=- AArch64InstrInfo.td - Describe the AArch64 Instructions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AArch64 Instruction definitions.
//
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ARM Instruction Predicate Definitions.
//

class AssemblerPredicateWithAll<dag cond, string name="">
    : AssemblerPredicate<(any_of FeatureAll, cond), name>;

def HasV8_0a         : Predicate<"Subtarget->hasV8_0aOps()">,
                                 AssemblerPredicate<(all_of HasV8_0aOps), "armv8.0a">;
def HasV8_1a         : Predicate<"Subtarget->hasV8_1aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV8_1aOps), "armv8.1a">;
def HasV8_2a         : Predicate<"Subtarget->hasV8_2aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV8_2aOps), "armv8.2a">;
def HasV8_3a         : Predicate<"Subtarget->hasV8_3aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV8_3aOps), "armv8.3a">;
def HasV8_4a         : Predicate<"Subtarget->hasV8_4aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV8_4aOps), "armv8.4a">;
def HasV8_5a         : Predicate<"Subtarget->hasV8_5aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV8_5aOps), "armv8.5a">;
def HasV8_6a         : Predicate<"Subtarget->hasV8_6aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV8_6aOps), "armv8.6a">;
def HasV8_7a         : Predicate<"Subtarget->hasV8_7aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV8_7aOps), "armv8.7a">;
def HasV8_8a         : Predicate<"Subtarget->hasV8_8aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV8_8aOps), "armv8.8a">;
def HasV8_9a         : Predicate<"Subtarget->hasV8_9aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV8_9aOps), "armv8.9a">;
def HasV9_0a         : Predicate<"Subtarget->hasV9_0aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV9_0aOps), "armv9-a">;
def HasV9_1a         : Predicate<"Subtarget->hasV9_1aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV9_1aOps), "armv9.1a">;
def HasV9_2a         : Predicate<"Subtarget->hasV9_2aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV9_2aOps), "armv9.2a">;
def HasV9_3a         : Predicate<"Subtarget->hasV9_3aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV9_3aOps), "armv9.3a">;
def HasV9_4a         : Predicate<"Subtarget->hasV9_4aOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV9_4aOps), "armv9.4a">;
def HasV8_0r         : Predicate<"Subtarget->hasV8_0rOps()">,
                                 AssemblerPredicateWithAll<(all_of HasV8_0rOps), "armv8-r">;

def HasEL2VMSA       : Predicate<"Subtarget->hasEL2VMSA()">,
                       AssemblerPredicateWithAll<(all_of FeatureEL2VMSA), "el2vmsa">;

def HasEL3           : Predicate<"Subtarget->hasEL3()">,
                       AssemblerPredicateWithAll<(all_of FeatureEL3), "el3">;

def HasVH            : Predicate<"Subtarget->hasVH()">,
                       AssemblerPredicateWithAll<(all_of FeatureVH), "vh">;

def HasLOR           : Predicate<"Subtarget->hasLOR()">,
                       AssemblerPredicateWithAll<(all_of FeatureLOR), "lor">;

def HasPAuth         : Predicate<"Subtarget->hasPAuth()">,
                       AssemblerPredicateWithAll<(all_of FeaturePAuth), "pauth">;

def HasPAuthLR       : Predicate<"Subtarget->hasPAuthLR()">,
                       AssemblerPredicateWithAll<(all_of FeaturePAuthLR), "pauth-lr">;

def HasJS            : Predicate<"Subtarget->hasJS()">,
                       AssemblerPredicateWithAll<(all_of FeatureJS), "jsconv">;

def HasCCIDX         : Predicate<"Subtarget->hasCCIDX()">,
                       AssemblerPredicateWithAll<(all_of FeatureCCIDX), "ccidx">;

def HasComplxNum      : Predicate<"Subtarget->hasComplxNum()">,
                       AssemblerPredicateWithAll<(all_of FeatureComplxNum), "complxnum">;

def HasNV            : Predicate<"Subtarget->hasNV()">,
                       AssemblerPredicateWithAll<(all_of FeatureNV), "nv">;

def HasMPAM          : Predicate<"Subtarget->hasMPAM()">,
                       AssemblerPredicateWithAll<(all_of FeatureMPAM), "mpam">;

def HasDIT           : Predicate<"Subtarget->hasDIT()">,
                       AssemblerPredicateWithAll<(all_of FeatureDIT), "dit">;

def HasTRACEV8_4         : Predicate<"Subtarget->hasTRACEV8_4()">,
                       AssemblerPredicateWithAll<(all_of FeatureTRACEV8_4), "tracev8.4">;

def HasAM            : Predicate<"Subtarget->hasAM()">,
                       AssemblerPredicateWithAll<(all_of FeatureAM), "am">;

def HasSEL2          : Predicate<"Subtarget->hasSEL2()">,
                       AssemblerPredicateWithAll<(all_of FeatureSEL2), "sel2">;

def HasTLB_RMI          : Predicate<"Subtarget->hasTLB_RMI()">,
                       AssemblerPredicateWithAll<(all_of FeatureTLB_RMI), "tlb-rmi">;

def HasFlagM         : Predicate<"Subtarget->hasFlagM()">,
                       AssemblerPredicateWithAll<(all_of FeatureFlagM), "flagm">;

def HasRCPC_IMMO      : Predicate<"Subtarget->hasRCPC_IMMO()">,
                       AssemblerPredicateWithAll<(all_of FeatureRCPC_IMMO), "rcpc-immo">;

def HasFPARMv8       : Predicate<"Subtarget->hasFPARMv8()">,
                               AssemblerPredicateWithAll<(all_of FeatureFPARMv8), "fp-armv8">;
def HasNEON          : Predicate<"Subtarget->isNeonAvailable()">,
                                 AssemblerPredicateWithAll<(all_of FeatureNEON), "neon">;
def HasSM4           : Predicate<"Subtarget->hasSM4()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSM4), "sm4">;
def HasSHA3          : Predicate<"Subtarget->hasSHA3()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSHA3), "sha3">;
def HasSHA2          : Predicate<"Subtarget->hasSHA2()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSHA2), "sha2">;
def HasAES           : Predicate<"Subtarget->hasAES()">,
                                 AssemblerPredicateWithAll<(all_of FeatureAES), "aes">;
def HasDotProd       : Predicate<"Subtarget->hasDotProd()">,
                                 AssemblerPredicateWithAll<(all_of FeatureDotProd), "dotprod">;
def HasCRC           : Predicate<"Subtarget->hasCRC()">,
                                 AssemblerPredicateWithAll<(all_of FeatureCRC), "crc">;
def HasCSSC          : Predicate<"Subtarget->hasCSSC()">,
                                 AssemblerPredicateWithAll<(all_of FeatureCSSC), "cssc">;
def HasNoCSSC        : Predicate<"!Subtarget->hasCSSC()">;
def HasLSE           : Predicate<"Subtarget->hasLSE()">,
                                 AssemblerPredicateWithAll<(all_of FeatureLSE), "lse">;
def HasNoLSE         : Predicate<"!Subtarget->hasLSE()">;
def HasRAS           : Predicate<"Subtarget->hasRAS()">,
                                 AssemblerPredicateWithAll<(all_of FeatureRAS), "ras">;
def HasRDM           : Predicate<"Subtarget->hasRDM()">,
                                 AssemblerPredicateWithAll<(all_of FeatureRDM), "rdm">;
def HasFullFP16      : Predicate<"Subtarget->hasFullFP16()">,
                                 AssemblerPredicateWithAll<(all_of FeatureFullFP16), "fullfp16">;
def HasNoFullFP16    : Predicate<"!Subtarget->hasFullFP16()">;
def HasFP16FML       : Predicate<"Subtarget->hasFP16FML()">,
                                 AssemblerPredicateWithAll<(all_of FeatureFP16FML), "fp16fml">;
def HasSPE           : Predicate<"Subtarget->hasSPE()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSPE), "spe">;
def HasFuseAES       : Predicate<"Subtarget->hasFuseAES()">,
                                 AssemblerPredicateWithAll<(all_of FeatureFuseAES),
                                 "fuse-aes">;
def HasSVE           : Predicate<"Subtarget->isSVEAvailable()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSVE), "sve">;
def HasSVE2          : Predicate<"Subtarget->isSVEAvailable() && Subtarget->hasSVE2()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSVE2), "sve2">;
def HasSVE2p1        : Predicate<"Subtarget->isSVEAvailable() && Subtarget->hasSVE2p1()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSVE2p1), "sve2p1">;
def HasSVE2AES       : Predicate<"Subtarget->isSVEAvailable() && Subtarget->hasSVE2AES()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSVE2AES), "sve2-aes">;
def HasSVE2SM4       : Predicate<"Subtarget->isSVEAvailable() && Subtarget->hasSVE2SM4()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSVE2SM4), "sve2-sm4">;
def HasSVE2SHA3      : Predicate<"Subtarget->isSVEAvailable() && Subtarget->hasSVE2SHA3()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSVE2SHA3), "sve2-sha3">;
def HasSVE2BitPerm   : Predicate<"Subtarget->isSVEAvailable() && Subtarget->hasSVE2BitPerm()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSVE2BitPerm), "sve2-bitperm">;
def HasB16B16        : Predicate<"Subtarget->hasB16B16()">,
                                 AssemblerPredicateWithAll<(all_of FeatureB16B16), "b16b16">;
def HasSMEandIsNonStreamingSafe
                     : Predicate<"Subtarget->hasSME()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSME), "sme">;
def HasSME           : Predicate<"Subtarget->isStreaming() && Subtarget->hasSME()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSME), "sme">;
def HasSMEF64F64     : Predicate<"Subtarget->isStreaming() && Subtarget->hasSMEF64F64()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSMEF64F64), "sme-f64f64">;
def HasSMEF16F16     : Predicate<"Subtarget->isStreaming() && Subtarget->hasSMEF16F16()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSMEF16F16), "sme-f16f16">;
def HasSMEFA64       : Predicate<"Subtarget->isStreaming() && Subtarget->hasSMEFA64()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSMEFA64), "sme-fa64">;
def HasSMEI16I64     : Predicate<"Subtarget->isStreaming() && Subtarget->hasSMEI16I64()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSMEI16I64), "sme-i16i64">;
def HasSME2andIsNonStreamingSafe
                     : Predicate<"Subtarget->hasSME2()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSME2), "sme2">;
def HasSME2          : Predicate<"Subtarget->isStreaming() && Subtarget->hasSME2()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSME2), "sme2">;
def HasSME2p1        : Predicate<"Subtarget->isStreaming() && Subtarget->hasSME2p1()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSME2p1), "sme2p1">;
def HasFP8           : Predicate<"Subtarget->hasFP8()">,
                                 AssemblerPredicateWithAll<(all_of FeatureFP8), "fp8">;
def HasFAMINMAX      : Predicate<"Subtarget->hasFAMINMAX()">,
                                 AssemblerPredicateWithAll<(all_of FeatureFAMINMAX), "faminmax">;
def HasFP8FMA        : Predicate<"Subtarget->hasFP8FMA()">,
                                 AssemblerPredicateWithAll<(all_of FeatureFP8FMA), "fp8fma">;
def HasSSVE_FP8FMA   : Predicate<"Subtarget->hasSSVE_FP8FMA() || "
                                 "(Subtarget->hasSVE2() && Subtarget->hasFP8FMA())">,
                                 AssemblerPredicateWithAll<(any_of FeatureSSVE_FP8FMA,
                                                           (all_of FeatureSVE2, FeatureFP8FMA)),
                                                           "ssve-fp8fma or (sve2 and fp8fma)">;
def HasFP8DOT2       : Predicate<"Subtarget->hasFP8DOT2()">,
                                 AssemblerPredicateWithAll<(all_of FeatureFP8DOT2), "fp8dot2">;
def HasSSVE_FP8DOT2  : Predicate<"Subtarget->hasSSVE_FP8DOT2() || "
                                 "(Subtarget->hasSVE2() && Subtarget->hasFP8DOT2())">,
                                 AssemblerPredicateWithAll<(any_of FeatureSSVE_FP8DOT2,
                                                           (all_of FeatureSVE2, FeatureFP8DOT2)),
                                "ssve-fp8dot2 or (sve2 and fp8dot2)">;
def HasFP8DOT4       : Predicate<"Subtarget->hasFP8DOT4()">,
                                 AssemblerPredicateWithAll<(all_of FeatureFP8DOT4), "fp8dot4">;
def HasSSVE_FP8DOT4  : Predicate<"Subtarget->hasSSVE_FP8DOT4() || "
                                 "(Subtarget->hasSVE2() && Subtarget->hasFP8DOT4())">,
                                 AssemblerPredicateWithAll<(any_of FeatureSSVE_FP8DOT4,
                                                           (all_of FeatureSVE2, FeatureFP8DOT4)),
                                 "ssve-fp8dot4 or (sve2 and fp8dot4)">;
def HasLUT          : Predicate<"Subtarget->hasLUT()">,
                                 AssemblerPredicateWithAll<(all_of FeatureLUT), "lut">;
def HasSME_LUTv2     : Predicate<"Subtarget->isStreaming() && Subtarget->hasSME_LUTv2()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSME_LUTv2), "sme-lutv2">;
def HasSMEF8F16     : Predicate<"Subtarget->isStreaming() && Subtarget->hasSMEF8F16()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSMEF8F16), "sme-f8f16">;
def HasSMEF8F32     : Predicate<"Subtarget->isStreaming() && Subtarget->hasSMEF8F32()">,
                                 AssemblerPredicateWithAll<(all_of FeatureSMEF8F32), "sme-f8f32">;

// A subset of SVE(2) instructions are legal in Streaming SVE execution mode,
// they should be enabled if either has been specified.
def HasSVEorSME
    : Predicate<"Subtarget->hasSVE() || (Subtarget->isStreaming() && Subtarget->hasSME())">,
                AssemblerPredicateWithAll<(any_of FeatureSVE, FeatureSME),
                "sve or sme">;
def HasSVE2orSME
    : Predicate<"Subtarget->hasSVE2() || (Subtarget->isStreaming() && Subtarget->hasSME())">,
                AssemblerPredicateWithAll<(any_of FeatureSVE2, FeatureSME),
                "sve2 or sme">;
def HasSVE2orSME2
    : Predicate<"Subtarget->hasSVE2() || (Subtarget->isStreaming() && Subtarget->hasSME2())">,
                AssemblerPredicateWithAll<(any_of FeatureSVE2, FeatureSME2),
                "sve2 or sme2">;
def HasSVE2p1_or_HasSME
    : Predicate<"Subtarget->hasSVE2p1() || (Subtarget->isStreaming() && Subtarget->hasSME())">,
                 AssemblerPredicateWithAll<(any_of FeatureSME, FeatureSVE2p1), "sme or sve2p1">;
def HasSVE2p1_or_HasSME2
    : Predicate<"Subtarget->hasSVE2p1() || (Subtarget->isStreaming() && Subtarget->hasSME2())">,
                 AssemblerPredicateWithAll<(any_of FeatureSME2, FeatureSVE2p1), "sme2 or sve2p1">;
def HasSVE2p1_or_HasSME2p1
    : Predicate<"Subtarget->hasSVE2p1() || (Subtarget->isStreaming() && Subtarget->hasSME2p1())">,
                 AssemblerPredicateWithAll<(any_of FeatureSME2p1, FeatureSVE2p1), "sme2p1 or sve2p1">;

def HasSMEF16F16orSMEF8F16
    : Predicate<"Subtarget->isStreaming() && (Subtarget->hasSMEF16F16() || Subtarget->hasSMEF8F16())">,
                AssemblerPredicateWithAll<(any_of FeatureSMEF16F16, FeatureSMEF8F16),
                "sme-f16f16 or sme-f8f16">;

// A subset of NEON instructions are legal in Streaming SVE execution mode,
// so don't need the additional check for 'isNeonAvailable'.
def HasNEONandIsStreamingSafe
    : Predicate<"Subtarget->hasNEON()">,
      AssemblerPredicateWithAll<(any_of FeatureNEON), "neon">;
def HasRCPC          : Predicate<"Subtarget->hasRCPC()">,
                                 AssemblerPredicateWithAll<(all_of FeatureRCPC), "rcpc">;
def HasAltNZCV       : Predicate<"Subtarget->hasAlternativeNZCV()">,
                       AssemblerPredicateWithAll<(all_of FeatureAltFPCmp), "altnzcv">;
def HasFRInt3264     : Predicate<"Subtarget->hasFRInt3264()">,
                       AssemblerPredicateWithAll<(all_of FeatureFRInt3264), "frint3264">;
def HasSB            : Predicate<"Subtarget->hasSB()">,
                       AssemblerPredicateWithAll<(all_of FeatureSB), "sb">;
def HasPredRes      : Predicate<"Subtarget->hasPredRes()">,
                       AssemblerPredicateWithAll<(all_of FeaturePredRes), "predres">;
def HasCCDP          : Predicate<"Subtarget->hasCCDP()">,
                       AssemblerPredicateWithAll<(all_of FeatureCacheDeepPersist), "ccdp">;
def HasBTI           : Predicate<"Subtarget->hasBTI()">,
                       AssemblerPredicateWithAll<(all_of FeatureBranchTargetId), "bti">;
def HasMTE           : Predicate<"Subtarget->hasMTE()">,
                       AssemblerPredicateWithAll<(all_of FeatureMTE), "mte">;
def HasTME           : Predicate<"Subtarget->hasTME()">,
                       AssemblerPredicateWithAll<(all_of FeatureTME), "tme">;
def HasETE           : Predicate<"Subtarget->hasETE()">,
                       AssemblerPredicateWithAll<(all_of FeatureETE), "ete">;
def HasTRBE          : Predicate<"Subtarget->hasTRBE()">,
                       AssemblerPredicateWithAll<(all_of FeatureTRBE), "trbe">;
def HasBF16          : Predicate<"Subtarget->hasBF16()">,
                       AssemblerPredicateWithAll<(all_of FeatureBF16), "bf16">;
def HasNoBF16        : Predicate<"!Subtarget->hasBF16()">;
def HasMatMulInt8    : Predicate<"Subtarget->hasMatMulInt8()">,
                       AssemblerPredicateWithAll<(all_of FeatureMatMulInt8), "i8mm">;
def HasMatMulFP32    : Predicate<"Subtarget->hasMatMulFP32()">,
                       AssemblerPredicateWithAll<(all_of FeatureMatMulFP32), "f32mm">;
def HasMatMulFP64    : Predicate<"Subtarget->hasMatMulFP64()">,
                       AssemblerPredicateWithAll<(all_of FeatureMatMulFP64), "f64mm">;
def HasFPAC          : Predicate<"Subtarget->hasFPAC())">,
                       AssemblerPredicateWithAll<(all_of FeatureFPAC), "fpac">;
def HasXS            : Predicate<"Subtarget->hasXS()">,
                       AssemblerPredicateWithAll<(all_of FeatureXS), "xs">;
def HasWFxT          : Predicate<"Subtarget->hasWFxT()">,
                       AssemblerPredicateWithAll<(all_of FeatureWFxT), "wfxt">;
def HasLS64          : Predicate<"Subtarget->hasLS64()">,
                       AssemblerPredicateWithAll<(all_of FeatureLS64), "ls64">;
def HasBRBE          : Predicate<"Subtarget->hasBRBE()">,
                       AssemblerPredicateWithAll<(all_of FeatureBRBE), "brbe">;
def HasSPE_EEF       : Predicate<"Subtarget->hasSPE_EEF()">,
                       AssemblerPredicateWithAll<(all_of FeatureSPE_EEF), "spe-eef">;
def HasHBC           : Predicate<"Subtarget->hasHBC()">,
                       AssemblerPredicateWithAll<(all_of FeatureHBC), "hbc">;
def HasMOPS          : Predicate<"Subtarget->hasMOPS()">,
                       AssemblerPredicateWithAll<(all_of FeatureMOPS), "mops">;
def HasCLRBHB        : Predicate<"Subtarget->hasCLRBHB()">,
                       AssemblerPredicateWithAll<(all_of FeatureCLRBHB), "clrbhb">;
def HasSPECRES2      : Predicate<"Subtarget->hasSPECRES2()">,
                       AssemblerPredicateWithAll<(all_of FeatureSPECRES2), "specres2">;
def HasITE           : Predicate<"Subtarget->hasITE()">,
                       AssemblerPredicateWithAll<(all_of FeatureITE), "ite">;
def HasTHE           : Predicate<"Subtarget->hasTHE()">,
                       AssemblerPredicateWithAll<(all_of FeatureTHE), "the">;
def HasRCPC3         : Predicate<"Subtarget->hasRCPC3()">,
                       AssemblerPredicateWithAll<(all_of FeatureRCPC3), "rcpc3">;
def HasLSE128        : Predicate<"Subtarget->hasLSE128()">,
                       AssemblerPredicateWithAll<(all_of FeatureLSE128), "lse128">;
def HasD128          : Predicate<"Subtarget->hasD128()">,
                       AssemblerPredicateWithAll<(all_of FeatureD128), "d128">;
def HasCHK           : Predicate<"Subtarget->hasCHK()">,
                       AssemblerPredicateWithAll<(all_of FeatureCHK), "chk">;
def HasGCS           : Predicate<"Subtarget->hasGCS()">,
                       AssemblerPredicateWithAll<(all_of FeatureGCS), "gcs">;
def HasCPA           : Predicate<"Subtarget->hasCPA()">,
                       AssemblerPredicateWithAll<(all_of FeatureCPA), "cpa">;
def IsLE             : Predicate<"Subtarget->isLittleEndian()">;
def IsBE             : Predicate<"!Subtarget->isLittleEndian()">;
def IsWindows        : Predicate<"Subtarget->isTargetWindows()">;
def UseExperimentalZeroingPseudos
    : Predicate<"Subtarget->useExperimentalZeroingPseudos()">;
def UseAlternateSExtLoadCVTF32
    : Predicate<"Subtarget->useAlternateSExtLoadCVTF32Pattern()">;

def UseNegativeImmediates
    : Predicate<"false">, AssemblerPredicate<(all_of (not FeatureNoNegativeImmediates)),
                                             "NegativeImmediates">;

def UseScalarIncVL : Predicate<"Subtarget->useScalarIncVL()">;

def NoUseScalarIncVL : Predicate<"!Subtarget->useScalarIncVL()">;

def UseSVEFPLD1R : Predicate<"!Subtarget->noSVEFPLD1R()">;

def AArch64LocalRecover : SDNode<"ISD::LOCAL_RECOVER",
                                  SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                                       SDTCisInt<1>]>>;


//===----------------------------------------------------------------------===//
// AArch64-specific DAG Nodes.
//

// SDTBinaryArithWithFlagsOut - RES1, FLAGS = op LHS, RHS
def SDTBinaryArithWithFlagsOut : SDTypeProfile<2, 2,
                                              [SDTCisSameAs<0, 2>,
                                               SDTCisSameAs<0, 3>,
                                               SDTCisInt<0>, SDTCisVT<1, i32>]>;

// SDTBinaryArithWithFlagsIn - RES1, FLAGS = op LHS, RHS, FLAGS
def SDTBinaryArithWithFlagsIn : SDTypeProfile<1, 3,
                                            [SDTCisSameAs<0, 1>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisInt<0>,
                                             SDTCisVT<3, i32>]>;

// SDTBinaryArithWithFlagsInOut - RES1, FLAGS = op LHS, RHS, FLAGS
def SDTBinaryArithWithFlagsInOut : SDTypeProfile<2, 3,
                                            [SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisInt<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisVT<4, i32>]>;

def SDT_AArch64Brcond  : SDTypeProfile<0, 3,
                                     [SDTCisVT<0, OtherVT>, SDTCisVT<1, i32>,
                                      SDTCisVT<2, i32>]>;
def SDT_AArch64cbz : SDTypeProfile<0, 2, [SDTCisInt<0>, SDTCisVT<1, OtherVT>]>;
def SDT_AArch64tbz : SDTypeProfile<0, 3, [SDTCisInt<0>, SDTCisInt<1>,
                                        SDTCisVT<2, OtherVT>]>;


def SDT_AArch64CSel  : SDTypeProfile<1, 4,
                                   [SDTCisSameAs<0, 1>,
                                    SDTCisSameAs<0, 2>,
                                    SDTCisInt<3>,
                                    SDTCisVT<4, i32>]>;
def SDT_AArch64CCMP : SDTypeProfile<1, 5,
                                    [SDTCisVT<0, i32>,
                                     SDTCisInt<1>,
                                     SDTCisSameAs<1, 2>,
                                     SDTCisInt<3>,
                                     SDTCisInt<4>,
                                     SDTCisVT<5, i32>]>;
def SDT_AArch64FCCMP : SDTypeProfile<1, 5,
                                     [SDTCisVT<0, i32>,
                                      SDTCisFP<1>,
                                      SDTCisSameAs<1, 2>,
                                      SDTCisInt<3>,
                                      SDTCisInt<4>,
                                      SDTCisVT<5, i32>]>;
def SDT_AArch64FCmp   : SDTypeProfile<0, 2,
                                   [SDTCisFP<0>,
                                    SDTCisSameAs<0, 1>]>;
def SDT_AArch64Dup   : SDTypeProfile<1, 1, [SDTCisVec<0>]>;
def SDT_AArch64DupLane   : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisInt<2>]>;
def SDT_AArch64Insr  : SDTypeProfile<1, 2, [SDTCisVec<0>]>;
def SDT_AArch64Zip   : SDTypeProfile<1, 2, [SDTCisVec<0>,
                                          SDTCisSameAs<0, 1>,
                                          SDTCisSameAs<0, 2>]>;
def SDT_AArch64MOVIedit : SDTypeProfile<1, 1, [SDTCisInt<1>]>;
def SDT_AArch64MOVIshift : SDTypeProfile<1, 2, [SDTCisInt<1>, SDTCisInt<2>]>;
def SDT_AArch64vecimm : SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                           SDTCisInt<2>, SDTCisInt<3>]>;
def SDT_AArch64UnaryVec: SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisSameAs<0,1>]>;
def SDT_AArch64ExtVec: SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                          SDTCisSameAs<0,2>, SDTCisInt<3>]>;
def SDT_AArch64vshift : SDTypeProfile<1, 2, [SDTCisSameAs<0,1>, SDTCisInt<2>]>;
def SDT_AArch64Dot: SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                         SDTCisVec<2>, SDTCisSameAs<2,3>]>;

def SDT_AArch64vshiftinsert : SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisInt<3>,
                                                 SDTCisSameAs<0,1>,
                                                 SDTCisSameAs<0,2>]>;

def SDT_AArch64unvec : SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisSameAs<0,1>]>;
def SDT_AArch64fcmpz : SDTypeProfile<1, 1, []>;
def SDT_AArch64fcmp  : SDTypeProfile<1, 2, [SDTCisSameAs<1,2>]>;
def SDT_AArch64binvec : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                           SDTCisSameAs<0,2>]>;
def SDT_AArch64trivec : SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                           SDTCisSameAs<0,2>,
                                           SDTCisSameAs<0,3>]>;
def SDT_AArch64TCRET : SDTypeProfile<0, 2, [SDTCisPtrTy<0>]>;
def SDT_AArch64PREFETCH : SDTypeProfile<0, 2, [SDTCisVT<0, i32>, SDTCisPtrTy<1>]>;

def SDT_AArch64ITOF  : SDTypeProfile<1, 1, [SDTCisFP<0>, SDTCisSameAs<0,1>]>;

def SDT_AArch64TLSDescCall : SDTypeProfile<0, -2, [SDTCisPtrTy<0>,
                                                 SDTCisPtrTy<1>]>;

def SDT_AArch64uaddlp : SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisVec<1>]>;

def SDT_AArch64ldp : SDTypeProfile<2, 1, [SDTCisVT<0, i64>, SDTCisSameAs<0, 1>, SDTCisPtrTy<2>]>;
def SDT_AArch64ldiapp : SDTypeProfile<2, 1, [SDTCisVT<0, i64>, SDTCisSameAs<0, 1>, SDTCisPtrTy<2>]>;
def SDT_AArch64ldnp : SDTypeProfile<2, 1, [SDTCisVT<0, v4i32>, SDTCisSameAs<0, 1>, SDTCisPtrTy<2>]>;
def SDT_AArch64stp : SDTypeProfile<0, 3, [SDTCisVT<0, i64>, SDTCisSameAs<0, 1>, SDTCisPtrTy<2>]>;
def SDT_AArch64stilp : SDTypeProfile<0, 3, [SDTCisVT<0, i64>, SDTCisSameAs<0, 1>, SDTCisPtrTy<2>]>;
def SDT_AArch64stnp : SDTypeProfile<0, 3, [SDTCisVT<0, v4i32>, SDTCisSameAs<0, 1>, SDTCisPtrTy<2>]>;

// Generates the general dynamic sequences, i.e.
//  adrp  x0, :tlsdesc:var
//  ldr   x1, [x0, #:tlsdesc_lo12:var]
//  add   x0, x0, #:tlsdesc_lo12:var
//  .tlsdesccall var
//  blr   x1

// (the TPIDR_EL0 offset is put directly in X0, hence no "result" here)
// number of operands (the variable)
def SDT_AArch64TLSDescCallSeq : SDTypeProfile<0,1,
                                          [SDTCisPtrTy<0>]>;

def SDT_AArch64WrapperLarge : SDTypeProfile<1, 4,
                                        [SDTCisVT<0, i64>, SDTCisVT<1, i32>,
                                         SDTCisSameAs<1, 2>, SDTCisSameAs<1, 3>,
                                         SDTCisSameAs<1, 4>]>;

def SDT_AArch64TBL : SDTypeProfile<1, 2, [
  SDTCisVec<0>, SDTCisSameAs<0, 1>, SDTCisInt<2>
]>;

// non-extending masked load fragment.
def nonext_masked_load :
  PatFrag<(ops node:$ptr, node:$pred, node:$def),
          (masked_ld node:$ptr, undef, node:$pred, node:$def), [{
  return cast<MaskedLoadSDNode>(N)->getExtensionType() == ISD::NON_EXTLOAD &&
         cast<MaskedLoadSDNode>(N)->isUnindexed() &&
         !cast<MaskedLoadSDNode>(N)->isNonTemporal();
}]>;
// Any/Zero extending masked load fragments.
def azext_masked_load :
  PatFrag<(ops node:$ptr, node:$pred, node:$def),
          (masked_ld node:$ptr, undef, node:$pred, node:$def),[{
  return (cast<MaskedLoadSDNode>(N)->getExtensionType() == ISD::EXTLOAD ||
          cast<MaskedLoadSDNode>(N)->getExtensionType() == ISD::ZEXTLOAD) &&
         cast<MaskedLoadSDNode>(N)->isUnindexed();
}]>;
def azext_masked_load_i8 :
  PatFrag<(ops node:$ptr, node:$pred, node:$def),
          (azext_masked_load node:$ptr, node:$pred, node:$def), [{
  return cast<MaskedLoadSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def azext_masked_load_i16 :
  PatFrag<(ops node:$ptr, node:$pred, node:$def),
          (azext_masked_load node:$ptr, node:$pred, node:$def), [{
  return cast<MaskedLoadSDNode>(N)->getMemoryVT().getScalarType() == MVT::i16;
}]>;
def azext_masked_load_i32 :
  PatFrag<(ops node:$ptr, node:$pred, node:$def),
          (azext_masked_load node:$ptr, node:$pred, node:$def), [{
  return cast<MaskedLoadSDNode>(N)->getMemoryVT().getScalarType() == MVT::i32;
}]>;
// Sign extending masked load fragments.
def sext_masked_load :
  PatFrag<(ops node:$ptr, node:$pred, node:$def),
          (masked_ld node:$ptr, undef, node:$pred, node:$def), [{
  return cast<MaskedLoadSDNode>(N)->getExtensionType() == ISD::SEXTLOAD &&
         cast<MaskedLoadSDNode>(N)->isUnindexed();
}]>;
def sext_masked_load_i8 :
  PatFrag<(ops node:$ptr, node:$pred, node:$def),
          (sext_masked_load node:$ptr, node:$pred, node:$def), [{
  return cast<MaskedLoadSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def sext_masked_load_i16 :
  PatFrag<(ops node:$ptr, node:$pred, node:$def),
          (sext_masked_load node:$ptr, node:$pred, node:$def), [{
  return cast<MaskedLoadSDNode>(N)->getMemoryVT().getScalarType() == MVT::i16;
}]>;
def sext_masked_load_i32 :
  PatFrag<(ops node:$ptr, node:$pred, node:$def),
          (sext_masked_load node:$ptr, node:$pred, node:$def), [{
  return cast<MaskedLoadSDNode>(N)->getMemoryVT().getScalarType() == MVT::i32;
}]>;

def non_temporal_load :
   PatFrag<(ops node:$ptr, node:$pred, node:$def),
           (masked_ld node:$ptr, undef, node:$pred, node:$def), [{
   return cast<MaskedLoadSDNode>(N)->getExtensionType() == ISD::NON_EXTLOAD &&
          cast<MaskedLoadSDNode>(N)->isUnindexed() &&
          cast<MaskedLoadSDNode>(N)->isNonTemporal();
}]>;

// non-truncating masked store fragment.
def nontrunc_masked_store :
  PatFrag<(ops node:$val, node:$ptr, node:$pred),
          (masked_st node:$val, node:$ptr, undef, node:$pred), [{
  return !cast<MaskedStoreSDNode>(N)->isTruncatingStore() &&
         cast<MaskedStoreSDNode>(N)->isUnindexed() &&
         !cast<MaskedStoreSDNode>(N)->isNonTemporal();
}]>;
// truncating masked store fragments.
def trunc_masked_store :
  PatFrag<(ops node:$val, node:$ptr, node:$pred),
          (masked_st node:$val, node:$ptr, undef, node:$pred), [{
  return cast<MaskedStoreSDNode>(N)->isTruncatingStore() &&
         cast<MaskedStoreSDNode>(N)->isUnindexed();
}]>;
def trunc_masked_store_i8 :
  PatFrag<(ops node:$val, node:$ptr, node:$pred),
          (trunc_masked_store node:$val, node:$ptr, node:$pred), [{
  return cast<MaskedStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def trunc_masked_store_i16 :
  PatFrag<(ops node:$val, node:$ptr, node:$pred),
          (trunc_masked_store node:$val, node:$ptr, node:$pred), [{
  return cast<MaskedStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i16;
}]>;
def trunc_masked_store_i32 :
  PatFrag<(ops node:$val, node:$ptr, node:$pred),
          (trunc_masked_store node:$val, node:$ptr, node:$pred), [{
  return cast<MaskedStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i32;
}]>;

def non_temporal_store :
  PatFrag<(ops node:$val, node:$ptr, node:$pred),
          (masked_st node:$val, node:$ptr, undef, node:$pred), [{
  return !cast<MaskedStoreSDNode>(N)->isTruncatingStore() &&
         cast<MaskedStoreSDNode>(N)->isUnindexed() &&
         cast<MaskedStoreSDNode>(N)->isNonTemporal();
}]>;

multiclass masked_gather_scatter<PatFrags GatherScatterOp> {
  // offsets = (signed)Index << sizeof(elt)
  def NAME#_signed_scaled :
    PatFrag<(ops node:$val, node:$pred, node:$ptr, node:$idx),
            (GatherScatterOp node:$val, node:$pred, node:$ptr, node:$idx),[{
    auto MGS = cast<MaskedGatherScatterSDNode>(N);
    bool Signed = MGS->isIndexSigned() ||
        MGS->getIndex().getValueType().getVectorElementType() == MVT::i64;
    return Signed && MGS->isIndexScaled();
  }]>;
  // offsets = (signed)Index
  def NAME#_signed_unscaled :
    PatFrag<(ops node:$val, node:$pred, node:$ptr, node:$idx),
            (GatherScatterOp node:$val, node:$pred, node:$ptr, node:$idx),[{
    auto MGS = cast<MaskedGatherScatterSDNode>(N);
    bool Signed = MGS->isIndexSigned() ||
        MGS->getIndex().getValueType().getVectorElementType() == MVT::i64;
    return Signed && !MGS->isIndexScaled();
  }]>;
  // offsets = (unsigned)Index << sizeof(elt)
  def NAME#_unsigned_scaled :
    PatFrag<(ops node:$val, node:$pred, node:$ptr, node:$idx),
            (GatherScatterOp node:$val, node:$pred, node:$ptr, node:$idx),[{
    auto MGS = cast<MaskedGatherScatterSDNode>(N);
    bool Signed = MGS->isIndexSigned() ||
        MGS->getIndex().getValueType().getVectorElementType() == MVT::i64;
    return !Signed && MGS->isIndexScaled();
  }]>;
  // offsets = (unsigned)Index
  def NAME#_unsigned_unscaled :
    PatFrag<(ops node:$val, node:$pred, node:$ptr, node:$idx),
            (GatherScatterOp node:$val, node:$pred, node:$ptr, node:$idx),[{
    auto MGS = cast<MaskedGatherScatterSDNode>(N);
    bool Signed = MGS->isIndexSigned() ||
        MGS->getIndex().getValueType().getVectorElementType() == MVT::i64;
    return !Signed && !MGS->isIndexScaled();
  }]>;
}

defm nonext_masked_gather    : masked_gather_scatter<nonext_masked_gather>;
defm azext_masked_gather_i8  : masked_gather_scatter<azext_masked_gather_i8>;
defm azext_masked_gather_i16 : masked_gather_scatter<azext_masked_gather_i16>;
defm azext_masked_gather_i32 : masked_gather_scatter<azext_masked_gather_i32>;
defm sext_masked_gather_i8   : masked_gather_scatter<sext_masked_gather_i8>;
defm sext_masked_gather_i16  : masked_gather_scatter<sext_masked_gather_i16>;
defm sext_masked_gather_i32  : masked_gather_scatter<sext_masked_gather_i32>;

defm nontrunc_masked_scatter  : masked_gather_scatter<nontrunc_masked_scatter>;
defm trunc_masked_scatter_i8  : masked_gather_scatter<trunc_masked_scatter_i8>;
defm trunc_masked_scatter_i16 : masked_gather_scatter<trunc_masked_scatter_i16>;
defm trunc_masked_scatter_i32 : masked_gather_scatter<trunc_masked_scatter_i32>;

// top16Zero - answer true if the upper 16 bits of $src are 0, false otherwise
def top16Zero: PatLeaf<(i32 GPR32:$src), [{
  return SDValue(N,0)->getValueType(0) == MVT::i32 &&
         CurDAG->MaskedValueIsZero(SDValue(N,0), APInt::getHighBitsSet(32, 16));
  }]>;

// top32Zero - answer true if the upper 32 bits of $src are 0, false otherwise
def top32Zero: PatLeaf<(i64 GPR64:$src), [{
  return SDValue(N,0)->getValueType(0) == MVT::i64 &&
         CurDAG->MaskedValueIsZero(SDValue(N,0), APInt::getHighBitsSet(64, 32));
  }]>;

// topbitsallzero - Return true if all bits except the lowest bit are known zero
def topbitsallzero32: PatLeaf<(i32 GPR32:$src), [{
  return SDValue(N,0)->getValueType(0) == MVT::i32 &&
         CurDAG->MaskedValueIsZero(SDValue(N,0), APInt::getHighBitsSet(32, 31));
  }]>;
def topbitsallzero64: PatLeaf<(i64 GPR64:$src), [{
  return SDValue(N,0)->getValueType(0) == MVT::i64 &&
         CurDAG->MaskedValueIsZero(SDValue(N,0), APInt::getHighBitsSet(64, 63));
  }]>;

// Node definitions.
def AArch64adrp          : SDNode<"AArch64ISD::ADRP", SDTIntUnaryOp, []>;
def AArch64adr           : SDNode<"AArch64ISD::ADR", SDTIntUnaryOp, []>;
def AArch64addlow        : SDNode<"AArch64ISD::ADDlow", SDTIntBinOp, []>;
def AArch64LOADgot       : SDNode<"AArch64ISD::LOADgot", SDTIntUnaryOp>;
def AArch64callseq_start : SDNode<"ISD::CALLSEQ_START",
                                SDCallSeqStart<[ SDTCisVT<0, i32>,
                                                 SDTCisVT<1, i32> ]>,
                                [SDNPHasChain, SDNPOutGlue]>;
def AArch64callseq_end   : SDNode<"ISD::CALLSEQ_END",
                                SDCallSeqEnd<[ SDTCisVT<0, i32>,
                                               SDTCisVT<1, i32> ]>,
                                [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64call          : SDNode<"AArch64ISD::CALL",
                                SDTypeProfile<0, -1, [SDTCisPtrTy<0>]>,
                                [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                                 SDNPVariadic]>;

def AArch64call_bti      : SDNode<"AArch64ISD::CALL_BTI",
                                SDTypeProfile<0, -1, [SDTCisPtrTy<0>]>,
                                [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                                 SDNPVariadic]>;

def AArch64call_rvmarker: SDNode<"AArch64ISD::CALL_RVMARKER",
                             SDTypeProfile<0, -1, [SDTCisPtrTy<0>]>,
                             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                              SDNPVariadic]>;

def AArch64call_arm64ec_to_x64 : SDNode<"AArch64ISD::CALL_ARM64EC_TO_X64",
                                      SDTypeProfile<0, -1, [SDTCisPtrTy<0>]>,
                                      [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                                       SDNPVariadic]>;

def AArch64authcall : SDNode<"AArch64ISD::AUTH_CALL",
                             SDTypeProfile<0, -1, [SDTCisPtrTy<0>,
                                                   SDTCisVT<1, i32>,
                                                   SDTCisVT<2, i64>,
                                                   SDTCisVT<3, i64>]>,
                             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                              SDNPVariadic]>;

def AArch64authtcret: SDNode<"AArch64ISD::AUTH_TC_RETURN",
                             SDTypeProfile<0, 5, [SDTCisPtrTy<0>,
                                                  SDTCisVT<2, i32>,
                                                  SDTCisVT<3, i64>,
                                                  SDTCisVT<4, i64>]>,
                             [SDNPHasChain,  SDNPOptInGlue, SDNPVariadic]>;

def AArch64authcall_rvmarker : SDNode<"AArch64ISD::AUTH_CALL_RVMARKER",
                                 SDTypeProfile<0, -1, [SDTCisPtrTy<0>,
                                                       SDTCisPtrTy<1>,
                                                       SDTCisVT<2, i32>,
                                                       SDTCisVT<3, i64>,
                                                       SDTCisVT<4, i64>]>,
                                 [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                                  SDNPVariadic]>;

def AArch64brcond        : SDNode<"AArch64ISD::BRCOND", SDT_AArch64Brcond,
                                [SDNPHasChain]>;
def AArch64cbz           : SDNode<"AArch64ISD::CBZ", SDT_AArch64cbz,
                                [SDNPHasChain]>;
def AArch64cbnz           : SDNode<"AArch64ISD::CBNZ", SDT_AArch64cbz,
                                [SDNPHasChain]>;
def AArch64tbz           : SDNode<"AArch64ISD::TBZ", SDT_AArch64tbz,
                                [SDNPHasChain]>;
def AArch64tbnz           : SDNode<"AArch64ISD::TBNZ", SDT_AArch64tbz,
                                [SDNPHasChain]>;


def AArch64csel          : SDNode<"AArch64ISD::CSEL", SDT_AArch64CSel>;
def AArch64csinv         : SDNode<"AArch64ISD::CSINV", SDT_AArch64CSel>;
def AArch64csneg         : SDNode<"AArch64ISD::CSNEG", SDT_AArch64CSel>;
def AArch64csinc         : SDNode<"AArch64ISD::CSINC", SDT_AArch64CSel>;
def AArch64retglue       : SDNode<"AArch64ISD::RET_GLUE", SDTNone,
                                [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def AArch64adc       : SDNode<"AArch64ISD::ADC",  SDTBinaryArithWithFlagsIn >;
def AArch64sbc       : SDNode<"AArch64ISD::SBC",  SDTBinaryArithWithFlagsIn>;
def AArch64add_flag  : SDNode<"AArch64ISD::ADDS",  SDTBinaryArithWithFlagsOut,
                            [SDNPCommutative]>;
def AArch64sub_flag  : SDNode<"AArch64ISD::SUBS",  SDTBinaryArithWithFlagsOut>;
def AArch64and_flag  : SDNode<"AArch64ISD::ANDS",  SDTBinaryArithWithFlagsOut,
                            [SDNPCommutative]>;
def AArch64adc_flag  : SDNode<"AArch64ISD::ADCS",  SDTBinaryArithWithFlagsInOut>;
def AArch64sbc_flag  : SDNode<"AArch64ISD::SBCS",  SDTBinaryArithWithFlagsInOut>;

def AArch64ccmp      : SDNode<"AArch64ISD::CCMP",  SDT_AArch64CCMP>;
def AArch64ccmn      : SDNode<"AArch64ISD::CCMN",  SDT_AArch64CCMP>;
def AArch64fccmp     : SDNode<"AArch64ISD::FCCMP", SDT_AArch64FCCMP>;

def AArch64threadpointer : SDNode<"AArch64ISD::THREAD_POINTER", SDTPtrLeaf>;

def AArch64fcmp         : SDNode<"AArch64ISD::FCMP", SDT_AArch64FCmp>;
def AArch64strict_fcmp  : SDNode<"AArch64ISD::STRICT_FCMP", SDT_AArch64FCmp,
                                 [SDNPHasChain]>;
def AArch64strict_fcmpe : SDNode<"AArch64ISD::STRICT_FCMPE", SDT_AArch64FCmp,
                                 [SDNPHasChain]>;
def AArch64any_fcmp     : PatFrags<(ops node:$lhs, node:$rhs),
                                   [(AArch64strict_fcmp node:$lhs, node:$rhs),
                                    (AArch64fcmp node:$lhs, node:$rhs)]>;

def AArch64dup       : SDNode<"AArch64ISD::DUP", SDT_AArch64Dup>;
def AArch64duplane8  : SDNode<"AArch64ISD::DUPLANE8", SDT_AArch64DupLane>;
def AArch64duplane16 : SDNode<"AArch64ISD::DUPLANE16", SDT_AArch64DupLane>;
def AArch64duplane32 : SDNode<"AArch64ISD::DUPLANE32", SDT_AArch64DupLane>;
def AArch64duplane64 : SDNode<"AArch64ISD::DUPLANE64", SDT_AArch64DupLane>;
def AArch64duplane128 : SDNode<"AArch64ISD::DUPLANE128", SDT_AArch64DupLane>;

def AArch64insr      : SDNode<"AArch64ISD::INSR", SDT_AArch64Insr>;

def AArch64zip1      : SDNode<"AArch64ISD::ZIP1", SDT_AArch64Zip>;
def AArch64zip2      : SDNode<"AArch64ISD::ZIP2", SDT_AArch64Zip>;
def AArch64uzp1      : SDNode<"AArch64ISD::UZP1", SDT_AArch64Zip>;
def AArch64uzp2      : SDNode<"AArch64ISD::UZP2", SDT_AArch64Zip>;
def AArch64trn1      : SDNode<"AArch64ISD::TRN1", SDT_AArch64Zip>;
def AArch64trn2      : SDNode<"AArch64ISD::TRN2", SDT_AArch64Zip>;

def AArch64movi_edit : SDNode<"AArch64ISD::MOVIedit", SDT_AArch64MOVIedit>;
def AArch64movi_shift : SDNode<"AArch64ISD::MOVIshift", SDT_AArch64MOVIshift>;
def AArch64movi_msl : SDNode<"AArch64ISD::MOVImsl", SDT_AArch64MOVIshift>;
def AArch64mvni_shift : SDNode<"AArch64ISD::MVNIshift", SDT_AArch64MOVIshift>;
def AArch64mvni_msl : SDNode<"AArch64ISD::MVNImsl", SDT_AArch64MOVIshift>;
def AArch64movi : SDNode<"AArch64ISD::MOVI", SDT_AArch64MOVIedit>;
def AArch64fmov : SDNode<"AArch64ISD::FMOV", SDT_AArch64MOVIedit>;

def AArch64rev16 : SDNode<"AArch64ISD::REV16", SDT_AArch64UnaryVec>;
def AArch64rev32 : SDNode<"AArch64ISD::REV32", SDT_AArch64UnaryVec>;
def AArch64rev64 : SDNode<"AArch64ISD::REV64", SDT_AArch64UnaryVec>;
def AArch64ext : SDNode<"AArch64ISD::EXT", SDT_AArch64ExtVec>;

def AArch64vashr : SDNode<"AArch64ISD::VASHR", SDT_AArch64vshift>;

def AArch64vashr_exact : PatFrag<(ops          node:$lhs, node:$rhs),
                                 (AArch64vashr node:$lhs, node:$rhs), [{
  return N->getFlags().hasExact();
}]>;

def AArch64vlshr : SDNode<"AArch64ISD::VLSHR", SDT_AArch64vshift>;
def AArch64vshl : SDNode<"AArch64ISD::VSHL", SDT_AArch64vshift>;
def AArch64sqshli : SDNode<"AArch64ISD::SQSHL_I", SDT_AArch64vshift>;
def AArch64uqshli : SDNode<"AArch64ISD::UQSHL_I", SDT_AArch64vshift>;
def AArch64sqshlui : SDNode<"AArch64ISD::SQSHLU_I", SDT_AArch64vshift>;
def AArch64srshri : SDNode<"AArch64ISD::SRSHR_I", SDT_AArch64vshift>;
def AArch64urshri : SDNode<"AArch64ISD::URSHR_I", SDT_AArch64vshift>;
def AArch64vsli : SDNode<"AArch64ISD::VSLI", SDT_AArch64vshiftinsert>;
def AArch64vsri : SDNode<"AArch64ISD::VSRI", SDT_AArch64vshiftinsert>;

def AArch64bsp: SDNode<"AArch64ISD::BSP", SDT_AArch64trivec>;

def AArch64cmeq: SDNode<"AArch64ISD::CMEQ", SDT_AArch64binvec>;
def AArch64cmge: SDNode<"AArch64ISD::CMGE", SDT_AArch64binvec>;
def AArch64cmgt: SDNode<"AArch64ISD::CMGT", SDT_AArch64binvec>;
def AArch64cmhi: SDNode<"AArch64ISD::CMHI", SDT_AArch64binvec>;
def AArch64cmhs: SDNode<"AArch64ISD::CMHS", SDT_AArch64binvec>;

def AArch64fcmeq: SDNode<"AArch64ISD::FCMEQ", SDT_AArch64fcmp>;
def AArch64fcmge: SDNode<"AArch64ISD::FCMGE", SDT_AArch64fcmp>;
def AArch64fcmgt: SDNode<"AArch64ISD::FCMGT", SDT_AArch64fcmp>;

def AArch64cmeqz: SDNode<"AArch64ISD::CMEQz", SDT_AArch64unvec>;
def AArch64cmgez: SDNode<"AArch64ISD::CMGEz", SDT_AArch64unvec>;
def AArch64cmgtz: SDNode<"AArch64ISD::CMGTz", SDT_AArch64unvec>;
def AArch64cmlez: SDNode<"AArch64ISD::CMLEz", SDT_AArch64unvec>;
def AArch64cmltz: SDNode<"AArch64ISD::CMLTz", SDT_AArch64unvec>;
def AArch64cmtst : PatFrag<(ops node:$LHS, node:$RHS),
                        (vnot (AArch64cmeqz (and node:$LHS, node:$RHS)))>;

def AArch64fcmeqz: SDNode<"AArch64ISD::FCMEQz", SDT_AArch64fcmpz>;
def AArch64fcmgez: SDNode<"AArch64ISD::FCMGEz", SDT_AArch64fcmpz>;
def AArch64fcmgtz: SDNode<"AArch64ISD::FCMGTz", SDT_AArch64fcmpz>;
def AArch64fcmlez: SDNode<"AArch64ISD::FCMLEz", SDT_AArch64fcmpz>;
def AArch64fcmltz: SDNode<"AArch64ISD::FCMLTz", SDT_AArch64fcmpz>;

def AArch64fcvtxn_n: SDNode<"AArch64ISD::FCVTXN", SDTFPRoundOp>;
def AArch64fcvtxnsdr: PatFrags<(ops node:$Rn),
                               [(f32 (int_aarch64_sisd_fcvtxn (f64 node:$Rn))),
                                (f32 (AArch64fcvtxn_n (f64 node:$Rn)))]>;
def AArch64fcvtxnv: PatFrags<(ops node:$Rn),
                                 [(int_aarch64_neon_fcvtxn node:$Rn),
                                  (AArch64fcvtxn_n node:$Rn)]>;

//def Aarch64softf32tobf16v8: SDNode<"AArch64ISD::", SDTFPRoundOp>;

def AArch64bici: SDNode<"AArch64ISD::BICi", SDT_AArch64vecimm>;
def AArch64orri: SDNode<"AArch64ISD::ORRi", SDT_AArch64vecimm>;

def AArch64tcret: SDNode<"AArch64ISD::TC_RETURN", SDT_AArch64TCRET,
                  [SDNPHasChain,  SDNPOptInGlue, SDNPVariadic]>;

def AArch64Prefetch        : SDNode<"AArch64ISD::PREFETCH", SDT_AArch64PREFETCH,
                               [SDNPHasChain, SDNPSideEffect]>;

def AArch64sitof: SDNode<"AArch64ISD::SITOF", SDT_AArch64ITOF>;
def AArch64uitof: SDNode<"AArch64ISD::UITOF", SDT_AArch64ITOF>;

def AArch64tlsdesc_callseq : SDNode<"AArch64ISD::TLSDESC_CALLSEQ",
                                    SDT_AArch64TLSDescCallSeq,
                                    [SDNPInGlue, SDNPOutGlue, SDNPHasChain,
                                     SDNPVariadic]>;


def AArch64WrapperLarge : SDNode<"AArch64ISD::WrapperLarge",
                                 SDT_AArch64WrapperLarge>;

def AArch64NvCast : SDNode<"AArch64ISD::NVCAST", SDTUnaryOp>;

def SDT_AArch64mull : SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisInt<1>,
                                    SDTCisSameAs<1, 2>]>;
def AArch64pmull    : SDNode<"AArch64ISD::PMULL", SDT_AArch64mull,
                             [SDNPCommutative]>;
def AArch64smull    : SDNode<"AArch64ISD::SMULL", SDT_AArch64mull,
                             [SDNPCommutative]>;
def AArch64umull    : SDNode<"AArch64ISD::UMULL", SDT_AArch64mull,
                             [SDNPCommutative]>;

def AArch64frecpe   : SDNode<"AArch64ISD::FRECPE", SDTFPUnaryOp>;
def AArch64frecps   : SDNode<"AArch64ISD::FRECPS", SDTFPBinOp>;
def AArch64frsqrte  : SDNode<"AArch64ISD::FRSQRTE", SDTFPUnaryOp>;
def AArch64frsqrts  : SDNode<"AArch64ISD::FRSQRTS", SDTFPBinOp>;

def AArch64sdot     : SDNode<"AArch64ISD::SDOT", SDT_AArch64Dot>;
def AArch64udot     : SDNode<"AArch64ISD::UDOT", SDT_AArch64Dot>;

def AArch64saddv    : SDNode<"AArch64ISD::SADDV", SDT_AArch64UnaryVec>;
def AArch64uaddv    : SDNode<"AArch64ISD::UADDV", SDT_AArch64UnaryVec>;
def AArch64sminv    : SDNode<"AArch64ISD::SMINV", SDT_AArch64UnaryVec>;
def AArch64uminv    : SDNode<"AArch64ISD::UMINV", SDT_AArch64UnaryVec>;
def AArch64smaxv    : SDNode<"AArch64ISD::SMAXV", SDT_AArch64UnaryVec>;
def AArch64umaxv    : SDNode<"AArch64ISD::UMAXV", SDT_AArch64UnaryVec>;
def AArch64uaddlv   : SDNode<"AArch64ISD::UADDLV", SDT_AArch64uaddlp>;
def AArch64saddlv   : SDNode<"AArch64ISD::SADDLV", SDT_AArch64uaddlp>;

def AArch64uabd     : PatFrags<(ops node:$lhs, node:$rhs),
                               [(abdu node:$lhs, node:$rhs),
                                (int_aarch64_neon_uabd node:$lhs, node:$rhs)]>;
def AArch64sabd     : PatFrags<(ops node:$lhs, node:$rhs),
                               [(abds node:$lhs, node:$rhs),
                                (int_aarch64_neon_sabd node:$lhs, node:$rhs)]>;

def AArch64addp_n   : SDNode<"AArch64ISD::ADDP", SDT_AArch64Zip>;
def AArch64uaddlp_n : SDNode<"AArch64ISD::UADDLP", SDT_AArch64uaddlp>;
def AArch64saddlp_n : SDNode<"AArch64ISD::SADDLP", SDT_AArch64uaddlp>;
def AArch64addp     : PatFrags<(ops node:$Rn, node:$Rm),
                               [(AArch64addp_n node:$Rn, node:$Rm),
                                (int_aarch64_neon_addp node:$Rn, node:$Rm)]>;
def AArch64uaddlp   : PatFrags<(ops node:$src),
                               [(AArch64uaddlp_n node:$src),
                                (int_aarch64_neon_uaddlp node:$src)]>;
def AArch64saddlp   : PatFrags<(ops node:$src),
                               [(AArch64saddlp_n node:$src),
                                (int_aarch64_neon_saddlp node:$src)]>;
def AArch64faddp     : PatFrags<(ops node:$Rn, node:$Rm),
                                [(AArch64addp_n node:$Rn, node:$Rm),
                                 (int_aarch64_neon_faddp node:$Rn, node:$Rm)]>;
def AArch64roundingvlshr : ComplexPattern<vAny, 2, "SelectRoundingVLShr", [AArch64vlshr]>;
def AArch64rshrn : PatFrags<(ops node:$LHS, node:$RHS),
                            [(trunc (AArch64roundingvlshr node:$LHS, node:$RHS)),
                             (int_aarch64_neon_rshrn node:$LHS, node:$RHS)]>;
def AArch64facge     : PatFrags<(ops node:$Rn, node:$Rm),
                                [(AArch64fcmge (fabs node:$Rn), (fabs node:$Rm)),
                                 (int_aarch64_neon_facge node:$Rn, node:$Rm)]>;
def AArch64facgt     : PatFrags<(ops node:$Rn, node:$Rm),
                                [(AArch64fcmgt (fabs node:$Rn), (fabs node:$Rm)),
                                 (int_aarch64_neon_facgt node:$Rn, node:$Rm)]>;

def AArch64fmaxnmv : PatFrags<(ops node:$Rn),
                              [(vecreduce_fmax node:$Rn),
                               (int_aarch64_neon_fmaxnmv node:$Rn)]>;
def AArch64fminnmv : PatFrags<(ops node:$Rn),
                              [(vecreduce_fmin node:$Rn),
                               (int_aarch64_neon_fminnmv node:$Rn)]>;
def AArch64fmaxv : PatFrags<(ops node:$Rn),
                            [(vecreduce_fmaximum node:$Rn),
                             (int_aarch64_neon_fmaxv node:$Rn)]>;
def AArch64fminv : PatFrags<(ops node:$Rn),
                            [(vecreduce_fminimum node:$Rn),
                             (int_aarch64_neon_fminv node:$Rn)]>;

def SDT_AArch64SETTAG : SDTypeProfile<0, 2, [SDTCisPtrTy<0>, SDTCisPtrTy<1>]>;
def AArch64stg : SDNode<"AArch64ISD::STG", SDT_AArch64SETTAG, [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def AArch64stzg : SDNode<"AArch64ISD::STZG", SDT_AArch64SETTAG, [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def AArch64st2g : SDNode<"AArch64ISD::ST2G", SDT_AArch64SETTAG, [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def AArch64stz2g : SDNode<"AArch64ISD::STZ2G", SDT_AArch64SETTAG, [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def SDT_AArch64unpk : SDTypeProfile<1, 1, [
    SDTCisInt<0>, SDTCisInt<1>, SDTCisOpSmallerThanOp<1, 0>
]>;
def AArch64sunpkhi : SDNode<"AArch64ISD::SUNPKHI", SDT_AArch64unpk>;
def AArch64sunpklo : SDNode<"AArch64ISD::SUNPKLO", SDT_AArch64unpk>;
def AArch64uunpkhi : SDNode<"AArch64ISD::UUNPKHI", SDT_AArch64unpk>;
def AArch64uunpklo : SDNode<"AArch64ISD::UUNPKLO", SDT_AArch64unpk>;

def AArch64ldp : SDNode<"AArch64ISD::LDP", SDT_AArch64ldp, [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def AArch64ldiapp : SDNode<"AArch64ISD::LDIAPP", SDT_AArch64ldiapp, [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def AArch64ldnp : SDNode<"AArch64ISD::LDNP", SDT_AArch64ldnp, [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def AArch64stp : SDNode<"AArch64ISD::STP", SDT_AArch64stp, [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def AArch64stilp : SDNode<"AArch64ISD::STILP", SDT_AArch64stilp, [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def AArch64stnp : SDNode<"AArch64ISD::STNP", SDT_AArch64stnp, [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def AArch64tbl : SDNode<"AArch64ISD::TBL", SDT_AArch64TBL>;

def AArch64probedalloca
    : SDNode<"AArch64ISD::PROBED_ALLOCA",
             SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>,
             [SDNPHasChain, SDNPMayStore]>;

def AArch64mrs : SDNode<"AArch64ISD::MRS",
                        SDTypeProfile<1, 1, [SDTCisVT<0, i64>, SDTCisVT<1, i32>]>,
                        [SDNPHasChain, SDNPOutGlue]>;

def SD_AArch64rshrnb : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisVec<1>, SDTCisInt<2>]>;
def AArch64rshrnb : SDNode<"AArch64ISD::RSHRNB_I", SD_AArch64rshrnb>;
def AArch64rshrnb_pf : PatFrags<(ops node:$rs, node:$i),
                            [(AArch64rshrnb node:$rs, node:$i),
                            (int_aarch64_sve_rshrnb node:$rs, node:$i)]>;

def AArch64CttzElts : SDNode<"AArch64ISD::CTTZ_ELTS", SDTypeProfile<1, 1,
                             [SDTCisInt<0>, SDTCisVec<1>]>, []>;

// Match add node and also treat an 'or' node is as an 'add' if the or'ed operands
// have no common bits.
def add_and_or_is_add : PatFrags<(ops node:$lhs, node:$rhs),
                         [(add node:$lhs, node:$rhs), (or node:$lhs, node:$rhs)],[{
   if (N->getOpcode() == ISD::ADD)
     return true;
   return CurDAG->isADDLike(SDValue(N,0));
}]> {
  let GISelPredicateCode = [{
     // Only handle G_ADD for now. FIXME. build capability to compute whether
     // operands of G_OR have common bits set or not.
     return MI.getOpcode() == TargetOpcode::G_ADD;
  }];
}

// Match mul with enough sign-bits. Can be reduced to a smaller mul operand.
def smullwithsignbits : PatFrag<(ops node:$l, node:$r), (mul node:$l, node:$r), [{
  return CurDAG->ComputeNumSignBits(N->getOperand(0)) > 32 &&
         CurDAG->ComputeNumSignBits(N->getOperand(1)) > 32;
}]>;

//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//

// AArch64 Instruction Predicate Definitions.
// We could compute these on a per-module basis but doing so requires accessing
// the Function object through the <Target>Subtarget and objections were raised
// to that (see post-commit review comments for r301750).
let RecomputePerFunction = 1 in {
  def ForCodeSize   : Predicate<"shouldOptForSize(MF)">;
  def NotForCodeSize   : Predicate<"!shouldOptForSize(MF)">;
  // Avoid generating STRQro if it is slow, unless we're optimizing for code size.
  def UseSTRQro : Predicate<"!Subtarget->isSTRQroSlow() || shouldOptForSize(MF)">;

  // Register restrictions for indirect tail-calls:
  // - If branch target enforcement is enabled, indirect calls must use x16 or
  //   x17, because these are the only registers which can target the BTI C
  //   instruction.
  // - If PAuthLR is enabled, x16 is used in the epilogue to hold the address
  //   of the signing instruction. This can't be changed because it is used by a
  //   HINT instruction which only accepts x16. We can't load anything from the
  //   stack after this because the authentication instruction checks that SP is
  //   the same as it was at function entry, so we can't have anything on the
  //   stack.

  // BTI on, PAuthLR off: x16 or x17
  def TailCallX16X17 : Predicate<[{  MF->getInfo<AArch64FunctionInfo>()->branchTargetEnforcement() && !MF->getInfo<AArch64FunctionInfo>()->branchProtectionPAuthLR() }]>;
  // BTI on, PAuthLR on: x17 only
  def TailCallX17 : Predicate<[{ MF->getInfo<AArch64FunctionInfo>()->branchTargetEnforcement() && MF->getInfo<AArch64FunctionInfo>()->branchProtectionPAuthLR() }]>;
  // BTI off, PAuthLR on: Any non-callee-saved register except x16
  def TailCallNotX16 : Predicate<[{ !MF->getInfo<AArch64FunctionInfo>()->branchTargetEnforcement() && MF->getInfo<AArch64FunctionInfo>()->branchProtectionPAuthLR() }]>;
  // BTI off, PAuthLR off: Any non-callee-saved register
  def TailCallAny : Predicate<[{ !MF->getInfo<AArch64FunctionInfo>()->branchTargetEnforcement() && !MF->getInfo<AArch64FunctionInfo>()->branchProtectionPAuthLR() }]>;

  def SLSBLRMitigation : Predicate<[{ MF->getSubtarget<AArch64Subtarget>().hardenSlsBlr() }]>;
  def NoSLSBLRMitigation : Predicate<[{ !MF->getSubtarget<AArch64Subtarget>().hardenSlsBlr() }]>;
  // Toggles patterns which aren't beneficial in GlobalISel when we aren't
  // optimizing. This allows us to selectively use patterns without impacting
  // SelectionDAG's behaviour.
  // FIXME: One day there will probably be a nicer way to check for this, but
  // today is not that day.
  def OptimizedGISelOrOtherSelector : Predicate<"!MF->getFunction().hasOptNone() || MF->getProperties().hasProperty(MachineFunctionProperties::Property::FailedISel) || !MF->getProperties().hasProperty(MachineFunctionProperties::Property::Legalized)">;
}

include "AArch64InstrFormats.td"
include "SVEInstrFormats.td"
include "SMEInstrFormats.td"

//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Miscellaneous instructions.
//===----------------------------------------------------------------------===//

let hasSideEffects = 1, isCodeGenOnly = 1 in {
let Defs = [SP], Uses = [SP] in {
// We set Sched to empty list because we expect these instructions to simply get
// removed in most cases.
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                              [(AArch64callseq_start timm:$amt1, timm:$amt2)]>,
                              Sched<[]>;
def ADJCALLSTACKUP : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                            [(AArch64callseq_end timm:$amt1, timm:$amt2)]>,
                            Sched<[]>;

}

let Defs = [SP, NZCV], Uses = [SP] in {
// Probed stack allocation of a constant size, used in function prologues when
// stack-clash protection is enabled.
def PROBED_STACKALLOC : Pseudo<(outs GPR64:$scratch),
                               (ins i64imm:$stacksize, i64imm:$fixed_offset,
                                i64imm:$scalable_offset),
                               []>,
                               Sched<[]>;

// Probed stack allocation of a variable size, used in function prologues when
// stack-clash protection is enabled.
def PROBED_STACKALLOC_VAR : Pseudo<(outs),
                                   (ins GPR64sp:$target),
                                   []>,
                                   Sched<[]>;

// Probed stack allocations of a variable size, used for allocas of unknown size
// when stack-clash protection is enabled.
let usesCustomInserter = 1 in
def PROBED_STACKALLOC_DYN : Pseudo<(outs),
                                   (ins GPR64common:$target),
                                   [(AArch64probedalloca GPR64common:$target)]>,
                                   Sched<[]>;

} // Defs = [SP, NZCV], Uses = [SP] in 
} // hasSideEffects = 1, isCodeGenOnly = 1

let isReMaterializable = 1, isCodeGenOnly = 1 in {
// FIXME: The following pseudo instructions are only needed because remat
// cannot handle multiple instructions.  When that changes, they can be
// removed, along with the AArch64Wrapper node.

let AddedComplexity = 10 in
def LOADgot : Pseudo<(outs GPR64common:$dst), (ins i64imm:$addr),
                     [(set GPR64common:$dst, (AArch64LOADgot tglobaladdr:$addr))]>,
              Sched<[WriteLDAdr]>;

// The MOVaddr instruction should match only when the add is not folded
// into a load or store address.
def MOVaddr
    : Pseudo<(outs GPR64common:$dst), (ins i64imm:$hi, i64imm:$low),
             [(set GPR64common:$dst, (AArch64addlow (AArch64adrp tglobaladdr:$hi),
                                            tglobaladdr:$low))]>,
      Sched<[WriteAdrAdr]>;
def MOVaddrJT
    : Pseudo<(outs GPR64common:$dst), (ins i64imm:$hi, i64imm:$low),
             [(set GPR64common:$dst, (AArch64addlow (AArch64adrp tjumptable:$hi),
                                             tjumptable:$low))]>,
      Sched<[WriteAdrAdr]>;
def MOVaddrCP
    : Pseudo<(outs GPR64common:$dst), (ins i64imm:$hi, i64imm:$low),
             [(set GPR64common:$dst, (AArch64addlow (AArch64adrp tconstpool:$hi),
                                             tconstpool:$low))]>,
      Sched<[WriteAdrAdr]>;
def MOVaddrBA
    : Pseudo<(outs GPR64common:$dst), (ins i64imm:$hi, i64imm:$low),
             [(set GPR64common:$dst, (AArch64addlow (AArch64adrp tblockaddress:$hi),
                                             tblockaddress:$low))]>,
      Sched<[WriteAdrAdr]>;
def MOVaddrTLS
    : Pseudo<(outs GPR64common:$dst), (ins i64imm:$hi, i64imm:$low),
             [(set GPR64common:$dst, (AArch64addlow (AArch64adrp tglobaltlsaddr:$hi),
                                            tglobaltlsaddr:$low))]>,
      Sched<[WriteAdrAdr]>;
def MOVaddrEXT
    : Pseudo<(outs GPR64common:$dst), (ins i64imm:$hi, i64imm:$low),
             [(set GPR64common:$dst, (AArch64addlow (AArch64adrp texternalsym:$hi),
                                            texternalsym:$low))]>,
      Sched<[WriteAdrAdr]>;
// Normally AArch64addlow either gets folded into a following ldr/str,
// or together with an adrp into MOVaddr above. For cases with TLS, it
// might appear without either of them, so allow lowering it into a plain
// add.
def ADDlowTLS
    : Pseudo<(outs GPR64sp:$dst), (ins GPR64sp:$src, i64imm:$low),
             [(set GPR64sp:$dst, (AArch64addlow GPR64sp:$src,
                                            tglobaltlsaddr:$low))]>,
      Sched<[WriteAdr]>;

} // isReMaterializable, isCodeGenOnly

//===----------------------------------------------------------------------===//
// Pseudo instruction used by retguard
let isCodeGenOnly = 1, hasNoSchedulingInfo = 1 in {
    let Size = 8 in {
    def RETGUARD_JMP_TRAP: Pseudo<(outs), (ins GPR64:$reg), []>;
    }
}

def : Pat<(AArch64LOADgot tglobaltlsaddr:$addr),
          (LOADgot tglobaltlsaddr:$addr)>;

def : Pat<(AArch64LOADgot texternalsym:$addr),
          (LOADgot texternalsym:$addr)>;

def : Pat<(AArch64LOADgot tconstpool:$addr),
          (LOADgot tconstpool:$addr)>;

// In general these get lowered into a sequence of three 4-byte instructions.
// 32-bit jump table destination is actually only 2 instructions since we can
// use the table itself as a PC-relative base. But optimization occurs after
// branch relaxation so be pessimistic.
let Size = 12, Constraints = "@earlyclobber $dst,@earlyclobber $scratch",
    isNotDuplicable = 1 in {
def JumpTableDest32 : Pseudo<(outs GPR64:$dst, GPR64sp:$scratch),
                             (ins GPR64:$table, GPR64:$entry, i32imm:$jti), []>,
                      Sched<[]>;
def JumpTableDest16 : Pseudo<(outs GPR64:$dst, GPR64sp:$scratch),
                             (ins GPR64:$table, GPR64:$entry, i32imm:$jti), []>,
                      Sched<[]>;
def JumpTableDest8 : Pseudo<(outs GPR64:$dst, GPR64sp:$scratch),
                            (ins GPR64:$table, GPR64:$entry, i32imm:$jti), []>,
                     Sched<[]>;
}

// A hardened but more expensive version of jump-table dispatch.
// This combines the target address computation (otherwise done using the
// JumpTableDest pseudos above) with the branch itself (otherwise done using
// a plain BR) in a single non-attackable sequence.
//
// We take the final entry index as an operand to allow isel freedom. This does
// mean that the index can be attacker-controlled.  To address that, we also do
// limited checking of the offset, mainly ensuring it still points within the
// jump-table array.  When it doesn't, this branches to the first entry.
// We might want to trap instead.
//
// This is intended for use in conjunction with ptrauth for other code pointers,
// to avoid signing jump-table entries and turning them into pointers.
//
// Entry index is passed in x16.  Clobbers x16/x17/nzcv.
let isNotDuplicable = 1 in
def BR_JumpTable : Pseudo<(outs), (ins i32imm:$jti), []>, Sched<[]> {
  let isBranch = 1;
  let isTerminator = 1;
  let isIndirectBranch = 1;
  let isBarrier = 1;
  let isNotDuplicable = 1;
  let Defs = [X16,X17,NZCV];
  let Uses = [X16];
  let Size = 44; // 28 fixed + 16 variable, for table size materialization
}

// Space-consuming pseudo to aid testing of placement and reachability
// algorithms. Immediate operand is the number of bytes this "instruction"
// occupies; register operands can be used to enforce dependency and constrain
// the scheduler.
let hasSideEffects = 1, mayLoad = 1, mayStore = 1 in
def SPACE : Pseudo<(outs GPR64:$Rd), (ins i32imm:$size, GPR64:$Rn),
                   [(set GPR64:$Rd, (int_aarch64_space imm:$size, GPR64:$Rn))]>,
            Sched<[]>;

let hasSideEffects = 1, isCodeGenOnly = 1 in {
  def SpeculationSafeValueX
      : Pseudo<(outs GPR64:$dst), (ins GPR64:$src), []>, Sched<[]>;
  def SpeculationSafeValueW
      : Pseudo<(outs GPR32:$dst), (ins GPR32:$src), []>, Sched<[]>;
}

// SpeculationBarrierEndBB must only be used after an unconditional control
// flow, i.e. after a terminator for which isBarrier is True.
let hasSideEffects = 1, isCodeGenOnly = 1, isTerminator = 1, isBarrier = 1 in {
  // This gets lowered to a pair of 4-byte instructions.
  let Size = 8 in
  def SpeculationBarrierISBDSBEndBB
      : Pseudo<(outs), (ins), []>, Sched<[]>;
  // This gets lowered to a 4-byte instruction.
  let Size = 4 in
  def SpeculationBarrierSBEndBB
      : Pseudo<(outs), (ins), []>, Sched<[]>;
}

//===----------------------------------------------------------------------===//
// System instructions.
//===----------------------------------------------------------------------===//

def HINT : HintI<"hint">;
def : InstAlias<"nop",  (HINT 0b000)>;
def : InstAlias<"yield",(HINT 0b001)>;
def : InstAlias<"wfe",  (HINT 0b010)>;
def : InstAlias<"wfi",  (HINT 0b011)>;
def : InstAlias<"sev",  (HINT 0b100)>;
def : InstAlias<"sevl", (HINT 0b101)>;
def : InstAlias<"dgh",  (HINT 0b110)>;
def : InstAlias<"esb",  (HINT 0b10000)>, Requires<[HasRAS]>;
def : InstAlias<"csdb", (HINT 20)>;
// In order to be able to write readable assembly, LLVM should accept assembly
// inputs that use Branch Target Indentification mnemonics, even with BTI disabled.
// However, in order to be compatible with other assemblers (e.g. GAS), LLVM
// should not emit these mnemonics unless BTI is enabled.
def : InstAlias<"bti",  (HINT 32), 0>;
def : InstAlias<"bti $op", (HINT btihint_op:$op), 0>;
def : InstAlias<"bti",  (HINT 32)>, Requires<[HasBTI]>;
def : InstAlias<"bti $op", (HINT btihint_op:$op)>, Requires<[HasBTI]>;

// v8.2a Statistical Profiling extension
def : InstAlias<"psb $op",  (HINT psbhint_op:$op)>, Requires<[HasSPE]>;

// As far as LLVM is concerned this writes to the system's exclusive monitors.
let mayLoad = 1, mayStore = 1 in
def CLREX : CRmSystemI<imm0_15, 0b010, "clrex">;

// NOTE: ideally, this would have mayStore = 0, mayLoad = 0, but we cannot
// model patterns with sufficiently fine granularity.
let mayLoad = ?, mayStore = ? in {
def DMB   : CRmSystemI<barrier_op, 0b101, "dmb",
                       [(int_aarch64_dmb (i32 imm32_0_15:$CRm))]>;

def DSB   : CRmSystemI<barrier_op, 0b100, "dsb",
                       [(int_aarch64_dsb (i32 imm32_0_15:$CRm))]>;

def ISB   : CRmSystemI<barrier_op, 0b110, "isb",
                       [(int_aarch64_isb (i32 imm32_0_15:$CRm))]>;

def TSB   : CRmSystemI<barrier_op, 0b010, "tsb", []> {
  let CRm        = 0b0010;
  let Inst{12}   = 0;
  let Predicates = [HasTRACEV8_4];
}

def DSBnXS  : CRmSystemI<barrier_nxs_op, 0b001, "dsb"> {
  let CRm{1-0}   = 0b11;
  let Inst{9-8}  = 0b10;
  let Predicates = [HasXS];
}

let Predicates = [HasWFxT] in {
def WFET : RegInputSystemI<0b0000, 0b000, "wfet">;
def WFIT : RegInputSystemI<0b0000, 0b001, "wfit">;
}

// Branch Record Buffer two-word mnemonic instructions
class BRBEI<bits<3> op2, string keyword>
    : SimpleSystemI<0, (ins), "brb", keyword>, Sched<[WriteSys]> {
  let Inst{31-8} = 0b110101010000100101110010;
  let Inst{7-5} = op2;
  let Predicates = [HasBRBE];
}
def BRB_IALL: BRBEI<0b100, "\tiall">;
def BRB_INJ:  BRBEI<0b101, "\tinj">;

}

// Allow uppercase and lowercase keyword arguments for BRB IALL and BRB INJ
def : TokenAlias<"INJ", "inj">;
def : TokenAlias<"IALL", "iall">;


// ARMv9.4-A Guarded Control Stack
class GCSNoOp<bits<3> op2, string mnemonic>
    : SimpleSystemI<0, (ins), mnemonic, "">, Sched<[]> {
  let Inst{20-8} = 0b0100001110111;
  let Inst{7-5} = op2;
  let Predicates = [HasGCS];
}
def GCSPUSHX : GCSNoOp<0b100, "gcspushx">;
def GCSPOPCX : GCSNoOp<0b101, "gcspopcx">;
def GCSPOPX  : GCSNoOp<0b110, "gcspopx">;

class GCSRtIn<bits<3> op1, bits<3> op2, string mnemonic,
            list<dag> pattern = []>
    : RtSystemI<0, (outs), (ins GPR64:$Rt), mnemonic, "\t$Rt", pattern> {
  let Inst{20-19} = 0b01;
  let Inst{18-16} = op1;
  let Inst{15-8} = 0b01110111;
  let Inst{7-5} = op2;
  let Predicates = [HasGCS];
  let hasSideEffects = 1;
}

let mayStore = 1, mayLoad = 1 in
def GCSSS1   : GCSRtIn<0b011, 0b010, "gcsss1">;
let mayStore = 1 in
def GCSPUSHM : GCSRtIn<0b011, 0b000, "gcspushm">;

class GCSRtOut<bits<3> op1, bits<3> op2, string mnemonic,
            list<dag> pattern = []>
    : RtSystemI<1, (outs GPR64:$Rt), (ins GPR64:$src), mnemonic, "\t$Rt", pattern> {
  let Inst{20-19} = 0b01;
  let Inst{18-16} = op1;
  let Inst{15-8} = 0b01110111;
  let Inst{7-5} = op2;
  let Predicates = [HasGCS];
  let hasSideEffects = 1;
  // The input register is unchanged when GCS is disabled, so we need it as
  // both an input and output operand.
  let Constraints = "$src = $Rt";
}

let mayStore = 1, mayLoad = 1 in
def GCSSS2  : GCSRtOut<0b011, 0b011, "gcsss2">;
// FIXME: mayStore = 1 only needed to match the intrinsic definition
let mayStore = 1, mayLoad = 1 in
def GCSPOPM : GCSRtOut<0b011, 0b001, "gcspopm",
                       [(set GPR64:$Rt, (int_aarch64_gcspopm GPR64:$src))]>;
def GCSPOPM_NoOp : InstAlias<"gcspopm", (GCSPOPM XZR)>, Requires<[HasGCS]>; // Rt defaults to XZR if absent

def GCSB_DSYNC_disable : InstAlias<"gcsb\tdsync", (HINT 19), 0>;
def GCSB_DSYNC         : InstAlias<"gcsb\tdsync", (HINT 19), 1>, Requires<[HasGCS]>;

def : TokenAlias<"DSYNC", "dsync">;

let Uses = [X16], Defs = [X16], CRm = 0b0101 in {
  def CHKFEAT   : SystemNoOperands<0b000, "hint\t#40",
                                   [(set X16, (int_aarch64_chkfeat X16))]>;
}
def : InstAlias<"chkfeat\tx16", (CHKFEAT), 0>;
def : InstAlias<"chkfeat\tx16", (CHKFEAT), 1>, Requires<[HasCHK]>;

class GCSSt<string mnemonic, bits<3> op>
    : I<(outs), (ins GPR64:$Rt, GPR64sp:$Rn), mnemonic, "\t$Rt, [$Rn]", "", []>, Sched<[]> {
  bits<5> Rt;
  bits<5> Rn;
  let Inst{31-15} = 0b11011001000111110;
  let Inst{14-12} = op;
  let Inst{11-10} = 0b11;
  let Inst{9-5} = Rn;
  let Inst{4-0} = Rt;
  let Predicates = [HasGCS];
}
def GCSSTR  : GCSSt<"gcsstr",  0b000>;
def GCSSTTR : GCSSt<"gcssttr", 0b001>;

// ARMv8.2-A Dot Product
let Predicates = [HasDotProd] in {
defm SDOT : SIMDThreeSameVectorDot<0, 0, "sdot", AArch64sdot>;
defm UDOT : SIMDThreeSameVectorDot<1, 0, "udot", AArch64udot>;
defm SDOTlane : SIMDThreeSameVectorDotIndex<0, 0, 0b10, "sdot", AArch64sdot>;
defm UDOTlane : SIMDThreeSameVectorDotIndex<1, 0, 0b10, "udot", AArch64udot>;
}

// ARMv8.6-A BFloat
let Predicates = [HasNEON, HasBF16] in {
defm BFDOT       : SIMDThreeSameVectorBFDot<1, "bfdot">;
defm BF16DOTlane : SIMDThreeSameVectorBF16DotI<0, "bfdot">;
def BFMMLA       : SIMDThreeSameVectorBF16MatrixMul<"bfmmla">;
def BFMLALB      : SIMDBF16MLAL<0, "bfmlalb", int_aarch64_neon_bfmlalb>;
def BFMLALT      : SIMDBF16MLAL<1, "bfmlalt", int_aarch64_neon_bfmlalt>;
def BFMLALBIdx   : SIMDBF16MLALIndex<0, "bfmlalb", int_aarch64_neon_bfmlalb>;
def BFMLALTIdx   : SIMDBF16MLALIndex<1, "bfmlalt", int_aarch64_neon_bfmlalt>;
def BFCVTN       : SIMD_BFCVTN;
def BFCVTN2      : SIMD_BFCVTN2;

def : Pat<(v4bf16 (any_fpround (v4f32 V128:$Rn))),
          (EXTRACT_SUBREG (BFCVTN V128:$Rn), dsub)>;

// Vector-scalar BFDOT:
// The second source operand of the 64-bit variant of BF16DOTlane is a 128-bit
// register (the instruction uses a single 32-bit lane from it), so the pattern
// is a bit tricky.
def : Pat<(v2f32 (int_aarch64_neon_bfdot
                    (v2f32 V64:$Rd), (v4bf16 V64:$Rn),
                    (v4bf16 (bitconvert
                      (v2i32 (AArch64duplane32
                        (v4i32 (bitconvert
                          (v8bf16 (insert_subvector undef,
                            (v4bf16 V64:$Rm),
                            (i64 0))))),
                        VectorIndexS:$idx)))))),
          (BF16DOTlanev4bf16 (v2f32 V64:$Rd), (v4bf16 V64:$Rn),
                             (SUBREG_TO_REG (i32 0), V64:$Rm, dsub),
                             VectorIndexS:$idx)>;
}

let Predicates = [HasNEONandIsStreamingSafe, HasBF16] in {
def BFCVT : BF16ToSinglePrecision<"bfcvt">;
// Round FP32 to BF16.
def : Pat<(bf16 (any_fpround (f32 FPR32:$Rn))), (BFCVT $Rn)>;
}

// ARMv8.6A AArch64 matrix multiplication
let Predicates = [HasMatMulInt8] in {
def  SMMLA : SIMDThreeSameVectorMatMul<0, 0, "smmla", int_aarch64_neon_smmla>;
def  UMMLA : SIMDThreeSameVectorMatMul<0, 1, "ummla", int_aarch64_neon_ummla>;
def USMMLA : SIMDThreeSameVectorMatMul<1, 0, "usmmla", int_aarch64_neon_usmmla>;
defm USDOT : SIMDThreeSameVectorDot<0, 1, "usdot", int_aarch64_neon_usdot>;
defm USDOTlane : SIMDThreeSameVectorDotIndex<0, 1, 0b10, "usdot", int_aarch64_neon_usdot>;

// sudot lane has a pattern where usdot is expected (there is no sudot).
// The second operand is used in the dup operation to repeat the indexed
// element.
class BaseSIMDSUDOTIndex<bit Q, string dst_kind, string lhs_kind,
                         string rhs_kind, RegisterOperand RegType,
                         ValueType AccumType, ValueType InputType>
      : BaseSIMDThreeSameVectorIndexS<Q, 0, 0b00, 0b1111, "sudot", dst_kind,
                                        lhs_kind, rhs_kind, RegType, AccumType,
                                        InputType, null_frag> {
  let Pattern = [(set (AccumType RegType:$dst),
                      (AccumType (int_aarch64_neon_usdot (AccumType RegType:$Rd),
                                 (InputType (bitconvert (AccumType
                                    (AArch64duplane32 (v4i32 V128:$Rm),
                                        VectorIndexS:$idx)))),
                                 (InputType RegType:$Rn))))];
}

multiclass SIMDSUDOTIndex {
  def v8i8  : BaseSIMDSUDOTIndex<0, ".2s", ".8b", ".4b", V64, v2i32, v8i8>;
  def v16i8 : BaseSIMDSUDOTIndex<1, ".4s", ".16b", ".4b", V128, v4i32, v16i8>;
}

defm SUDOTlane : SIMDSUDOTIndex;

}

// ARMv8.2-A FP16 Fused Multiply-Add Long
let Predicates = [HasNEON, HasFP16FML] in {
defm FMLAL      : SIMDThreeSameVectorFML<0, 1, 0b001, "fmlal", int_aarch64_neon_fmlal>;
defm FMLSL      : SIMDThreeSameVectorFML<0, 1, 0b101, "fmlsl", int_aarch64_neon_fmlsl>;
defm FMLAL2     : SIMDThreeSameVectorFML<1, 0, 0b001, "fmlal2", int_aarch64_neon_fmlal2>;
defm FMLSL2     : SIMDThreeSameVectorFML<1, 0, 0b101, "fmlsl2", int_aarch64_neon_fmlsl2>;
defm FMLALlane  : SIMDThreeSameVectorFMLIndex<0, 0b0000, "fmlal", int_aarch64_neon_fmlal>;
defm FMLSLlane  : SIMDThreeSameVectorFMLIndex<0, 0b0100, "fmlsl", int_aarch64_neon_fmlsl>;
defm FMLAL2lane : SIMDThreeSameVectorFMLIndex<1, 0b1000, "fmlal2", int_aarch64_neon_fmlal2>;
defm FMLSL2lane : SIMDThreeSameVectorFMLIndex<1, 0b1100, "fmlsl2", int_aarch64_neon_fmlsl2>;
}

// Armv8.2-A Crypto extensions
let Predicates = [HasSHA3] in {
def SHA512H   : CryptoRRRTied<0b0, 0b00, "sha512h">;
def SHA512H2  : CryptoRRRTied<0b0, 0b01, "sha512h2">;
def SHA512SU0 : CryptoRRTied_2D<0b0, 0b00, "sha512su0">;
def SHA512SU1 : CryptoRRRTied_2D<0b0, 0b10, "sha512su1">;
def RAX1      : CryptoRRR_2D<0b0,0b11, "rax1">;
def EOR3      : CryptoRRRR_16B<0b00, "eor3">;
def BCAX      : CryptoRRRR_16B<0b01, "bcax">;
def XAR       : CryptoRRRi6<"xar">;

class SHA3_pattern<Instruction INST, Intrinsic OpNode, ValueType VecTy>
  : Pat<(VecTy (OpNode (VecTy V128:$Vd), (VecTy V128:$Vn), (VecTy V128:$Vm))),
        (INST (VecTy V128:$Vd), (VecTy V128:$Vn), (VecTy V128:$Vm))>;

def : Pat<(v2i64 (int_aarch64_crypto_sha512su0 (v2i64 V128:$Vn), (v2i64 V128:$Vm))),
          (SHA512SU0 (v2i64 V128:$Vn), (v2i64 V128:$Vm))>;

def : SHA3_pattern<SHA512H, int_aarch64_crypto_sha512h, v2i64>;
def : SHA3_pattern<SHA512H2, int_aarch64_crypto_sha512h2, v2i64>;
def : SHA3_pattern<SHA512SU1, int_aarch64_crypto_sha512su1, v2i64>;

def : SHA3_pattern<EOR3, int_aarch64_crypto_eor3u, v16i8>;
def : SHA3_pattern<EOR3, int_aarch64_crypto_eor3u, v8i16>;
def : SHA3_pattern<EOR3, int_aarch64_crypto_eor3u, v4i32>;
def : SHA3_pattern<EOR3, int_aarch64_crypto_eor3u, v2i64>;

class EOR3_pattern<ValueType VecTy>
  : Pat<(xor (xor (VecTy V128:$Vn), (VecTy V128:$Vm)), (VecTy V128:$Va)),
        (EOR3 (VecTy V128:$Vn), (VecTy V128:$Vm), (VecTy V128:$Va))>;

def : EOR3_pattern<v16i8>;
def : EOR3_pattern<v8i16>;
def : EOR3_pattern<v4i32>;
def : EOR3_pattern<v2i64>;

class BCAX_pattern<ValueType VecTy>
  : Pat<(xor (VecTy V128:$Vn), (and (VecTy V128:$Vm), (vnot (VecTy V128:$Va)))),
        (BCAX (VecTy V128:$Vn), (VecTy V128:$Vm), (VecTy V128:$Va))>;

def : BCAX_pattern<v16i8>;
def : BCAX_pattern<v8i16>;
def : BCAX_pattern<v4i32>;
def : BCAX_pattern<v2i64>;

def : SHA3_pattern<BCAX, int_aarch64_crypto_bcaxu, v16i8>;
def : SHA3_pattern<BCAX, int_aarch64_crypto_bcaxu, v8i16>;
def : SHA3_pattern<BCAX, int_aarch64_crypto_bcaxu, v4i32>;
def : SHA3_pattern<BCAX, int_aarch64_crypto_bcaxu, v2i64>;

def : SHA3_pattern<EOR3, int_aarch64_crypto_eor3s, v16i8>;
def : SHA3_pattern<EOR3, int_aarch64_crypto_eor3s, v8i16>;
def : SHA3_pattern<EOR3, int_aarch64_crypto_eor3s, v4i32>;
def : SHA3_pattern<EOR3, int_aarch64_crypto_eor3s, v2i64>;

def : SHA3_pattern<BCAX, int_aarch64_crypto_bcaxs, v16i8>;
def : SHA3_pattern<BCAX, int_aarch64_crypto_bcaxs, v8i16>;
def : SHA3_pattern<BCAX, int_aarch64_crypto_bcaxs, v4i32>;
def : SHA3_pattern<BCAX, int_aarch64_crypto_bcaxs, v2i64>;

def : Pat<(v2i64 (int_aarch64_crypto_rax1 (v2i64 V128:$Vn), (v2i64 V128:$Vm))),
          (RAX1 (v2i64 V128:$Vn), (v2i64 V128:$Vm))>;

def : Pat<(v2i64 (int_aarch64_crypto_xar (v2i64 V128:$Vn), (v2i64 V128:$Vm), (i64 timm0_63:$imm))),
          (XAR (v2i64 V128:$Vn), (v2i64 V128:$Vm), (timm0_63:$imm))>;

def : Pat<(xor  (v2i64 V128:$Vn), (or (AArch64vlshr (v2i64 V128:$Vm), (i32 63)), (AArch64vshl (v2i64 V128:$Vm), (i32 1)))),
          (RAX1 (v2i64 V128:$Vn), (v2i64 V128:$Vm))>;

} // HasSHA3

let Predicates = [HasSM4] in {
def SM3TT1A   : CryptoRRRi2Tied<0b0, 0b00, "sm3tt1a">;
def SM3TT1B   : CryptoRRRi2Tied<0b0, 0b01, "sm3tt1b">;
def SM3TT2A   : CryptoRRRi2Tied<0b0, 0b10, "sm3tt2a">;
def SM3TT2B   : CryptoRRRi2Tied<0b0, 0b11, "sm3tt2b">;
def SM3SS1    : CryptoRRRR_4S<0b10, "sm3ss1">;
def SM3PARTW1 : CryptoRRRTied_4S<0b1, 0b00, "sm3partw1">;
def SM3PARTW2 : CryptoRRRTied_4S<0b1, 0b01, "sm3partw2">;
def SM4ENCKEY : CryptoRRR_4S<0b1, 0b10, "sm4ekey">;
def SM4E      : CryptoRRTied_4S<0b0, 0b01, "sm4e">;

def : Pat<(v4i32 (int_aarch64_crypto_sm3ss1 (v4i32 V128:$Vn), (v4i32 V128:$Vm), (v4i32 V128:$Va))),
          (SM3SS1 (v4i32 V128:$Vn), (v4i32 V128:$Vm), (v4i32 V128:$Va))>;

class SM3PARTW_pattern<Instruction INST, Intrinsic OpNode>
  : Pat<(v4i32 (OpNode (v4i32 V128:$Vd), (v4i32 V128:$Vn), (v4i32 V128:$Vm))),
        (INST (v4i32 V128:$Vd), (v4i32 V128:$Vn), (v4i32 V128:$Vm))>;

class SM3TT_pattern<Instruction INST, Intrinsic OpNode>
  : Pat<(v4i32 (OpNode (v4i32 V128:$Vd), (v4i32 V128:$Vn), (v4i32 V128:$Vm), (i64 VectorIndexS_timm:$imm) )),
        (INST (v4i32 V128:$Vd), (v4i32 V128:$Vn), (v4i32 V128:$Vm), (VectorIndexS_timm:$imm))>;

class SM4_pattern<Instruction INST, Intrinsic OpNode>
  : Pat<(v4i32 (OpNode (v4i32 V128:$Vn), (v4i32 V128:$Vm))),
        (INST (v4i32 V128:$Vn), (v4i32 V128:$Vm))>;

def : SM3PARTW_pattern<SM3PARTW1, int_aarch64_crypto_sm3partw1>;
def : SM3PARTW_pattern<SM3PARTW2, int_aarch64_crypto_sm3partw2>;

def : SM3TT_pattern<SM3TT1A, int_aarch64_crypto_sm3tt1a>;
def : SM3TT_pattern<SM3TT1B, int_aarch64_crypto_sm3tt1b>;
def : SM3TT_pattern<SM3TT2A, int_aarch64_crypto_sm3tt2a>;
def : SM3TT_pattern<SM3TT2B, int_aarch64_crypto_sm3tt2b>;

def : SM4_pattern<SM4ENCKEY, int_aarch64_crypto_sm4ekey>;
def : SM4_pattern<SM4E, int_aarch64_crypto_sm4e>;
} // HasSM4

let Predicates = [HasRCPC] in {
  // v8.3 Release Consistent Processor Consistent support, optional in v8.2.
  def LDAPRB  : RCPCLoad<0b00, "ldaprb", GPR32>;
  def LDAPRH  : RCPCLoad<0b01, "ldaprh", GPR32>;
  def LDAPRW  : RCPCLoad<0b10, "ldapr", GPR32>;
  def LDAPRX  : RCPCLoad<0b11, "ldapr", GPR64>;
}

// v8.3a complex add and multiply-accumulate. No predicate here, that is done
// inside the multiclass as the FP16 versions need different predicates.
defm FCMLA : SIMDThreeSameVectorTiedComplexHSD<1, 0b110, complexrotateop,
                                               "fcmla", null_frag>;
defm FCADD : SIMDThreeSameVectorComplexHSD<1, 0b111, complexrotateopodd,
                                           "fcadd", null_frag>;
defm FCMLA : SIMDIndexedTiedComplexHSD<0, 1, complexrotateop, "fcmla">;

let Predicates = [HasComplxNum, HasNEON, HasFullFP16] in {
  def : Pat<(v4f16 (int_aarch64_neon_vcadd_rot90 (v4f16 V64:$Rn), (v4f16 V64:$Rm))),
            (FCADDv4f16 (v4f16 V64:$Rn), (v4f16 V64:$Rm), (i32 0))>;
  def : Pat<(v4f16 (int_aarch64_neon_vcadd_rot270 (v4f16 V64:$Rn), (v4f16 V64:$Rm))),
            (FCADDv4f16 (v4f16 V64:$Rn), (v4f16 V64:$Rm), (i32 1))>;
  def : Pat<(v8f16 (int_aarch64_neon_vcadd_rot90 (v8f16 V128:$Rn), (v8f16 V128:$Rm))),
            (FCADDv8f16 (v8f16 V128:$Rn), (v8f16 V128:$Rm), (i32 0))>;
  def : Pat<(v8f16 (int_aarch64_neon_vcadd_rot270 (v8f16 V128:$Rn), (v8f16 V128:$Rm))),
            (FCADDv8f16 (v8f16 V128:$Rn), (v8f16 V128:$Rm), (i32 1))>;
}

let Predicates = [HasComplxNum, HasNEON] in {
  def : Pat<(v2f32 (int_aarch64_neon_vcadd_rot90 (v2f32 V64:$Rn), (v2f32 V64:$Rm))),
            (FCADDv2f32 (v2f32 V64:$Rn), (v2f32 V64:$Rm), (i32 0))>;
  def : Pat<(v2f32 (int_aarch64_neon_vcadd_rot270 (v2f32 V64:$Rn), (v2f32 V64:$Rm))),
            (FCADDv2f32 (v2f32 V64:$Rn), (v2f32 V64:$Rm), (i32 1))>;
  foreach Ty = [v4f32, v2f64] in {
    def : Pat<(Ty (int_aarch64_neon_vcadd_rot90 (Ty V128:$Rn), (Ty V128:$Rm))),
              (!cast<Instruction>("FCADD"#Ty) (Ty V128:$Rn), (Ty V128:$Rm), (i32 0))>;
    def : Pat<(Ty (int_aarch64_neon_vcadd_rot270 (Ty V128:$Rn), (Ty V128:$Rm))),
              (!cast<Instruction>("FCADD"#Ty) (Ty V128:$Rn), (Ty V128:$Rm), (i32 1))>;
  }
}

multiclass FCMLA_PATS<ValueType ty, DAGOperand Reg> {
  def : Pat<(ty (int_aarch64_neon_vcmla_rot0 (ty Reg:$Rd), (ty Reg:$Rn), (ty Reg:$Rm))),
            (!cast<Instruction>("FCMLA" # ty) $Rd, $Rn, $Rm, 0)>;
  def : Pat<(ty (int_aarch64_neon_vcmla_rot90 (ty Reg:$Rd), (ty Reg:$Rn), (ty Reg:$Rm))),
            (!cast<Instruction>("FCMLA" # ty) $Rd, $Rn, $Rm, 1)>;
  def : Pat<(ty (int_aarch64_neon_vcmla_rot180 (ty Reg:$Rd), (ty Reg:$Rn), (ty Reg:$Rm))),
            (!cast<Instruction>("FCMLA" # ty) $Rd, $Rn, $Rm, 2)>;
  def : Pat<(ty (int_aarch64_neon_vcmla_rot270 (ty Reg:$Rd), (ty Reg:$Rn), (ty Reg:$Rm))),
            (!cast<Instruction>("FCMLA" # ty) $Rd, $Rn, $Rm, 3)>;
}

multiclass FCMLA_LANE_PATS<ValueType ty, DAGOperand Reg, dag RHSDup> {
  def : Pat<(ty (int_aarch64_neon_vcmla_rot0 (ty Reg:$Rd), (ty Reg:$Rn), RHSDup)),
            (!cast<Instruction>("FCMLA" # ty # "_indexed") $Rd, $Rn, $Rm, VectorIndexS:$idx, 0)>;
  def : Pat<(ty (int_aarch64_neon_vcmla_rot90 (ty Reg:$Rd), (ty Reg:$Rn), RHSDup)),
            (!cast<Instruction>("FCMLA" # ty # "_indexed") $Rd, $Rn, $Rm, VectorIndexS:$idx, 1)>;
  def : Pat<(ty (int_aarch64_neon_vcmla_rot180 (ty Reg:$Rd), (ty Reg:$Rn), RHSDup)),
            (!cast<Instruction>("FCMLA" # ty # "_indexed") $Rd, $Rn, $Rm, VectorIndexS:$idx, 2)>;
  def : Pat<(ty (int_aarch64_neon_vcmla_rot270 (ty Reg:$Rd), (ty Reg:$Rn), RHSDup)),
            (!cast<Instruction>("FCMLA" # ty # "_indexed") $Rd, $Rn, $Rm, VectorIndexS:$idx, 3)>;
}


let Predicates = [HasComplxNum, HasNEON, HasFullFP16] in {
  defm : FCMLA_PATS<v4f16, V64>;
  defm : FCMLA_PATS<v8f16, V128>;

  defm : FCMLA_LANE_PATS<v4f16, V64,
                         (v4f16 (bitconvert (v2i32 (AArch64duplane32 (v4i32 V128:$Rm), VectorIndexD:$idx))))>;
  defm : FCMLA_LANE_PATS<v8f16, V128,
                         (v8f16 (bitconvert (v4i32 (AArch64duplane32 (v4i32 V128:$Rm), VectorIndexS:$idx))))>;
}
let Predicates = [HasComplxNum, HasNEON] in {
  defm : FCMLA_PATS<v2f32, V64>;
  defm : FCMLA_PATS<v4f32, V128>;
  defm : FCMLA_PATS<v2f64, V128>;

  defm : FCMLA_LANE_PATS<v4f32, V128,
                         (v4f32 (bitconvert (v2i64 (AArch64duplane64 (v2i64 V128:$Rm), VectorIndexD:$idx))))>;
}

// v8.3a Pointer Authentication
// These instructions inhabit part of the hint space and so can be used for
// armv8 targets. Keeping the old HINT mnemonic when compiling without PA is
// important for compatibility with other assemblers (e.g. GAS) when building
// software compatible with both CPUs that do or don't implement PA.
let Uses = [LR], Defs = [LR] in {
  def PACIAZ   : SystemNoOperands<0b000, "hint\t#24">;
  def PACIBZ   : SystemNoOperands<0b010, "hint\t#26">;
  let isAuthenticated = 1 in {
    def AUTIAZ   : SystemNoOperands<0b100, "hint\t#28">;
    def AUTIBZ   : SystemNoOperands<0b110, "hint\t#30">;
  }
}
let Uses = [LR, SP], Defs = [LR] in {
  def PACIASP  : SystemNoOperands<0b001, "hint\t#25">;
  def PACIBSP  : SystemNoOperands<0b011, "hint\t#27">;
  let isAuthenticated = 1 in {
    def AUTIASP  : SystemNoOperands<0b101, "hint\t#29">;
    def AUTIBSP  : SystemNoOperands<0b111, "hint\t#31">;
  }
}
let Uses = [X16, X17], Defs = [X17], CRm = 0b0001 in {
  def PACIA1716  : SystemNoOperands<0b000, "hint\t#8">;
  def PACIB1716  : SystemNoOperands<0b010, "hint\t#10">;
  let isAuthenticated = 1 in {
    def AUTIA1716  : SystemNoOperands<0b100, "hint\t#12">;
    def AUTIB1716  : SystemNoOperands<0b110, "hint\t#14">;
  }
}

let Uses = [LR], Defs = [LR], CRm = 0b0000 in {
  def XPACLRI   : SystemNoOperands<0b111, "hint\t#7">;
}

// In order to be able to write readable assembly, LLVM should accept assembly
// inputs that use pointer authentication mnemonics, even with PA disabled.
// However, in order to be compatible with other assemblers (e.g. GAS), LLVM
// should not emit these mnemonics unless PA is enabled.
def : InstAlias<"paciaz", (PACIAZ), 0>;
def : InstAlias<"pacibz", (PACIBZ), 0>;
def : InstAlias<"autiaz", (AUTIAZ), 0>;
def : InstAlias<"autibz", (AUTIBZ), 0>;
def : InstAlias<"paciasp", (PACIASP), 0>;
def : InstAlias<"pacibsp", (PACIBSP), 0>;
def : InstAlias<"autiasp", (AUTIASP), 0>;
def : InstAlias<"autibsp", (AUTIBSP), 0>;
def : InstAlias<"pacia1716", (PACIA1716), 0>;
def : InstAlias<"pacib1716", (PACIB1716), 0>;
def : InstAlias<"autia1716", (AUTIA1716), 0>;
def : InstAlias<"autib1716", (AUTIB1716), 0>;
def : InstAlias<"xpaclri", (XPACLRI), 0>;

// Pseudos

let Uses = [LR, SP], Defs = [LR] in {
// Insertion point of LR signing code.
def PAUTH_PROLOGUE : Pseudo<(outs), (ins), []>, Sched<[]>;
// Insertion point of LR authentication code.
// The RET terminator of the containing machine basic block may be replaced
// with a combined RETA(A|B) instruction when rewriting this Pseudo.
def PAUTH_EPILOGUE : Pseudo<(outs), (ins), []>, Sched<[]>;
}

def PAUTH_BLEND : Pseudo<(outs GPR64:$disc),
                         (ins GPR64:$addr_disc, i32imm:$int_disc), []>, Sched<[]>;

// These pointer authentication instructions require armv8.3a
let Predicates = [HasPAuth] in {

  // When PA is enabled, a better mnemonic should be emitted.
  def : InstAlias<"paciaz", (PACIAZ), 1>;
  def : InstAlias<"pacibz", (PACIBZ), 1>;
  def : InstAlias<"autiaz", (AUTIAZ), 1>;
  def : InstAlias<"autibz", (AUTIBZ), 1>;
  def : InstAlias<"paciasp", (PACIASP), 1>;
  def : InstAlias<"pacibsp", (PACIBSP), 1>;
  def : InstAlias<"autiasp", (AUTIASP), 1>;
  def : InstAlias<"autibsp", (AUTIBSP), 1>;
  def : InstAlias<"pacia1716", (PACIA1716), 1>;
  def : InstAlias<"pacib1716", (PACIB1716), 1>;
  def : InstAlias<"autia1716", (AUTIA1716), 1>;
  def : InstAlias<"autib1716", (AUTIB1716), 1>;
  def : InstAlias<"xpaclri", (XPACLRI), 1>;

  multiclass SignAuth<bits<3> prefix, bits<3> prefix_z, string asm,
                      SDPatternOperator op> {
    def IA   : SignAuthOneData<prefix, 0b00, !strconcat(asm,  "ia"), op>;
    def IB   : SignAuthOneData<prefix, 0b01, !strconcat(asm,  "ib"), op>;
    def DA   : SignAuthOneData<prefix, 0b10, !strconcat(asm,  "da"), op>;
    def DB   : SignAuthOneData<prefix, 0b11, !strconcat(asm,  "db"), op>;
    def IZA  : SignAuthZero<prefix_z,  0b00, !strconcat(asm, "iza"), op>;
    def DZA  : SignAuthZero<prefix_z,  0b10, !strconcat(asm, "dza"), op>;
    def IZB  : SignAuthZero<prefix_z,  0b01, !strconcat(asm, "izb"), op>;
    def DZB  : SignAuthZero<prefix_z,  0b11, !strconcat(asm, "dzb"), op>;
  }

  defm PAC : SignAuth<0b000, 0b010, "pac", int_ptrauth_sign>;
  defm AUT : SignAuth<0b001, 0b011, "aut", null_frag>;

  def XPACI : ClearAuth<0, "xpaci">;
  def : Pat<(int_ptrauth_strip GPR64:$Rd, 0), (XPACI GPR64:$Rd)>;
  def : Pat<(int_ptrauth_strip GPR64:$Rd, 1), (XPACI GPR64:$Rd)>;

  def XPACD : ClearAuth<1, "xpacd">;
  def : Pat<(int_ptrauth_strip GPR64:$Rd, 2), (XPACD GPR64:$Rd)>;
  def : Pat<(int_ptrauth_strip GPR64:$Rd, 3), (XPACD GPR64:$Rd)>;

  def PACGA : SignAuthTwoOperand<0b1100, "pacga", int_ptrauth_sign_generic>;

  // Combined Instructions
  let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1  in {
    def BRAA    : AuthBranchTwoOperands<0, 0, "braa">;
    def BRAB    : AuthBranchTwoOperands<0, 1, "brab">;
  }
  let isCall = 1, Defs = [LR], Uses = [SP] in {
    def BLRAA   : AuthBranchTwoOperands<1, 0, "blraa">;
    def BLRAB   : AuthBranchTwoOperands<1, 1, "blrab">;
  }

  let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1  in {
    def BRAAZ   : AuthOneOperand<0b000, 0, "braaz">;
    def BRABZ   : AuthOneOperand<0b000, 1, "brabz">;
  }
  let isCall = 1, Defs = [LR], Uses = [SP] in {
    def BLRAAZ  : AuthOneOperand<0b001, 0, "blraaz">;
    def BLRABZ  : AuthOneOperand<0b001, 1, "blrabz">;
  }

  // BLRA pseudo, a generalized version of BLRAA/BLRAB/Z.
  // This directly manipulates x16/x17 to materialize the discriminator.
  // x16/x17 are generally used as the safe registers for sensitive ptrauth
  // operations (such as raw address manipulation or discriminator
  // materialization here), in part because they're handled in a safer way by
  // the kernel, notably on Darwin.
  def BLRA : Pseudo<(outs), (ins GPR64noip:$Rn, i32imm:$Key, i64imm:$Disc,
                                 GPR64noip:$AddrDisc),
                    [(AArch64authcall GPR64noip:$Rn, timm:$Key, timm:$Disc,
                                      GPR64noip:$AddrDisc)]>, Sched<[]> {
    let isCodeGenOnly = 1;
    let hasSideEffects = 1;
    let mayStore = 0;
    let mayLoad = 0;
    let isCall = 1;
    let Size = 12; // 4 fixed + 8 variable, to compute discriminator.
    let Defs = [X17,LR];
    let Uses = [SP];
  }

  def BLRA_RVMARKER : Pseudo<
        (outs), (ins i64imm:$rvfunc, GPR64noip:$Rn, i32imm:$Key, i64imm:$Disc,
                     GPR64noip:$AddrDisc),
        [(AArch64authcall_rvmarker tglobaladdr:$rvfunc,
                                   GPR64noip:$Rn, timm:$Key, timm:$Disc,
                                   GPR64noip:$AddrDisc)]>, Sched<[]> {
    let isCodeGenOnly = 1;
    let isCall = 1;
    let Defs = [X17,LR];
    let Uses = [SP];
  }

  // BRA pseudo, generalized version of BRAA/BRAB/Z.
  // This directly manipulates x16/x17, which are the only registers the OS
  // guarantees are safe to use for sensitive operations.
  def BRA : Pseudo<(outs), (ins GPR64noip:$Rn, i32imm:$Key, i64imm:$Disc,
                                GPR64noip:$AddrDisc), []>, Sched<[]> {
    let isCodeGenOnly = 1;
    let hasNoSchedulingInfo = 1;
    let hasSideEffects = 1;
    let mayStore = 0;
    let mayLoad = 0;
    let isBranch = 1;
    let isTerminator = 1;
    let isBarrier = 1;
    let isIndirectBranch = 1;
    let Size = 12; // 4 fixed + 8 variable, to compute discriminator.
    let Defs = [X17];
  }

  let isReturn = 1, isTerminator = 1, isBarrier = 1 in {
    def RETAA   : AuthReturn<0b010, 0, "retaa">;
    def RETAB   : AuthReturn<0b010, 1, "retab">;
    def ERETAA  : AuthReturn<0b100, 0, "eretaa">;
    def ERETAB  : AuthReturn<0b100, 1, "eretab">;
  }

  defm LDRAA  : AuthLoad<0, "ldraa", simm10Scaled>;
  defm LDRAB  : AuthLoad<1, "ldrab", simm10Scaled>;

  // AUT pseudo.
  // This directly manipulates x16/x17, which are the only registers the OS
  // guarantees are safe to use for sensitive operations.
  def AUT : Pseudo<(outs), (ins i32imm:$Key, i64imm:$Disc, GPR64noip:$AddrDisc),
                   []>, Sched<[WriteI, ReadI]> {
    let isCodeGenOnly = 1;
    let hasSideEffects = 1;
    let mayStore = 0;
    let mayLoad = 0;
    let Size = 32;
    let Defs = [X16,X17,NZCV];
    let Uses = [X16];
  }

  // AUT and re-PAC a value, using different keys/data.
  // This directly manipulates x16/x17, which are the only registers the OS
  // guarantees are safe to use for sensitive operations.
  def AUTPAC
      : Pseudo<(outs),
               (ins i32imm:$AUTKey, i64imm:$AUTDisc, GPR64noip:$AUTAddrDisc,
                    i32imm:$PACKey, i64imm:$PACDisc, GPR64noip:$PACAddrDisc),
               []>, Sched<[WriteI, ReadI]> {
    let isCodeGenOnly = 1;
    let hasSideEffects = 1;
    let mayStore = 0;
    let mayLoad = 0;
    let Size = 48;
    let Defs = [X16,X17,NZCV];
    let Uses = [X16];
  }

  // Materialize a signed global address, with adrp+add and PAC.
  def MOVaddrPAC : Pseudo<(outs),
                          (ins i64imm:$Addr, i32imm:$Key,
                               GPR64noip:$AddrDisc, i64imm:$Disc), []>,
               Sched<[WriteI, ReadI]> {
    let isReMaterializable = 1;
    let isCodeGenOnly = 1;
    let Size = 40; // 12 fixed + 28 variable, for pointer offset, and discriminator
    let Defs = [X16,X17];
  }

  // Materialize a signed global address, using a GOT load and PAC.
  def LOADgotPAC : Pseudo<(outs),
                          (ins i64imm:$Addr, i32imm:$Key,
                               GPR64noip:$AddrDisc, i64imm:$Disc), []>,
               Sched<[WriteI, ReadI]> {
    let isReMaterializable = 1;
    let isCodeGenOnly = 1;
    let Size = 40; // 12 fixed + 28 variable, for pointer offset, and discriminator
    let Defs = [X16,X17];
  }

  // Load a signed global address from a special $auth_ptr$ stub slot.
  def LOADauthptrstatic : Pseudo<(outs GPR64:$dst),
                              (ins i64imm:$Addr, i32imm:$Key,
                                   i64imm:$Disc), []>,
               Sched<[WriteI, ReadI]> {
    let isReMaterializable = 1;
    let isCodeGenOnly = 1;
    let Size = 8;
  }

  // Size 16: 4 fixed + 8 variable, to compute discriminator.
  let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Size = 16,
      Uses = [SP] in {
    def AUTH_TCRETURN
      : Pseudo<(outs), (ins tcGPR64:$dst, i32imm:$FPDiff, i32imm:$Key,
                            i64imm:$Disc, tcGPR64:$AddrDisc),
               []>, Sched<[WriteBrReg]>;
    def AUTH_TCRETURN_BTI
      : Pseudo<(outs), (ins tcGPRx16x17:$dst, i32imm:$FPDiff, i32imm:$Key,
                            i64imm:$Disc, tcGPR64:$AddrDisc),
               []>, Sched<[WriteBrReg]>;
  }

  let Predicates = [TailCallAny] in
    def : Pat<(AArch64authtcret tcGPR64:$dst, (i32 timm:$FPDiff), (i32 timm:$Key),
                                (i64 timm:$Disc), tcGPR64:$AddrDisc),
              (AUTH_TCRETURN tcGPR64:$dst, imm:$FPDiff, imm:$Key, imm:$Disc,
                             tcGPR64:$AddrDisc)>;

  let Predicates = [TailCallX16X17] in
    def : Pat<(AArch64authtcret tcGPRx16x17:$dst, (i32 timm:$FPDiff),
                                (i32 timm:$Key), (i64 timm:$Disc),
                                tcGPR64:$AddrDisc),
              (AUTH_TCRETURN_BTI tcGPRx16x17:$dst, imm:$FPDiff, imm:$Key,
                                 imm:$Disc, tcGPR64:$AddrDisc)>;
}

// v9.5-A pointer authentication extensions

// Always accept "pacm" as an alias for "hint #39", but don't emit it when
// disassembling if we don't have the pauth-lr feature.
let CRm = 0b0100 in {
  def PACM : SystemNoOperands<0b111, "hint\t#39">;
}
def : InstAlias<"pacm", (PACM), 0>;

let Predicates = [HasPAuthLR] in {
  let Defs = [LR], Uses = [LR, SP] in {
    //                                opcode2, opcode,   asm
    def PACIASPPC : SignAuthFixedRegs<0b00001, 0b101000, "paciasppc">;
    def PACIBSPPC : SignAuthFixedRegs<0b00001, 0b101001, "pacibsppc">;
    def PACNBIASPPC : SignAuthFixedRegs<0b00001, 0b100000, "pacnbiasppc">;
    def PACNBIBSPPC : SignAuthFixedRegs<0b00001, 0b100001, "pacnbibsppc">;
    //                             opc,  asm
    def AUTIASPPCi : SignAuthPCRel<0b00, "autiasppc">;
    def AUTIBSPPCi : SignAuthPCRel<0b01, "autibsppc">;
    //                              opcode2, opcode,   asm
    def AUTIASPPCr : SignAuthOneReg<0b00001, 0b100100, "autiasppcr">;
    def AUTIBSPPCr : SignAuthOneReg<0b00001, 0b100101, "autibsppcr">;
    //                                  opcode2, opcode,   asm
    def PACIA171615 : SignAuthFixedRegs<0b00001, 0b100010, "pacia171615">;
    def PACIB171615 : SignAuthFixedRegs<0b00001, 0b100011, "pacib171615">;
    def AUTIA171615 : SignAuthFixedRegs<0b00001, 0b101110, "autia171615">;
    def AUTIB171615 : SignAuthFixedRegs<0b00001, 0b101111, "autib171615">;
  }

  let Uses = [LR, SP], isReturn = 1, isTerminator = 1, isBarrier = 1 in {
    //                                   opc,   op2,     asm
    def RETAASPPCi : SignAuthReturnPCRel<0b000, 0b11111, "retaasppc">;
    def RETABSPPCi : SignAuthReturnPCRel<0b001, 0b11111, "retabsppc">;
    //                                 op3,      asm
    def RETAASPPCr : SignAuthReturnReg<0b000010, "retaasppcr">;
    def RETABSPPCr : SignAuthReturnReg<0b000011, "retabsppcr">;
  }
  def : InstAlias<"pacm", (PACM), 1>;
}


// v8.3a floating point conversion for javascript
let Predicates = [HasJS, HasFPARMv8], Defs = [NZCV] in
def FJCVTZS  : BaseFPToIntegerUnscaled<0b01, 0b11, 0b110, FPR64, GPR32,
                                      "fjcvtzs",
                                      [(set GPR32:$Rd,
                                         (int_aarch64_fjcvtzs FPR64:$Rn))]> {
  let Inst{31} = 0;
} // HasJS, HasFPARMv8

// v8.4 Flag manipulation instructions
let Predicates = [HasFlagM], Defs = [NZCV], Uses = [NZCV] in {
def CFINV : SimpleSystemI<0, (ins), "cfinv", "">, Sched<[WriteSys]> {
  let Inst{20-5} = 0b0000001000000000;
}
def SETF8  : BaseFlagManipulation<0, 0, (ins GPR32:$Rn), "setf8", "{\t$Rn}">;
def SETF16 : BaseFlagManipulation<0, 1, (ins GPR32:$Rn), "setf16", "{\t$Rn}">;
def RMIF   : FlagRotate<(ins GPR64:$Rn, uimm6:$imm, imm0_15:$mask), "rmif",
                        "{\t$Rn, $imm, $mask}">;
} // HasFlagM

// v8.5 flag manipulation instructions
let Predicates = [HasAltNZCV], Uses = [NZCV], Defs = [NZCV] in {

def XAFLAG : PstateWriteSimple<(ins), "xaflag", "">, Sched<[WriteSys]> {
  let Inst{18-16} = 0b000;
  let Inst{11-8} = 0b0000;
  let Unpredictable{11-8} = 0b1111;
  let Inst{7-5} = 0b001;
}

def AXFLAG : PstateWriteSimple<(ins), "axflag", "">, Sched<[WriteSys]> {
  let Inst{18-16} = 0b000;
  let Inst{11-8} = 0b0000;
  let Unpredictable{11-8} = 0b1111;
  let Inst{7-5} = 0b010;
}
} // HasAltNZCV


// Armv8.5-A speculation barrier
def SB : SimpleSystemI<0, (ins), "sb", "">, Sched<[]> {
  let Inst{20-5} = 0b0001100110000111;
  let Unpredictable{11-8} = 0b1111;
  let Predicates = [HasSB];
  let hasSideEffects = 1;
}

def : InstAlias<"clrex", (CLREX 0xf)>;
def : InstAlias<"isb", (ISB 0xf)>;
def : InstAlias<"ssbb", (DSB 0)>;
def : InstAlias<"pssbb", (DSB 4)>;
def : InstAlias<"dfb", (DSB 0b1100)>, Requires<[HasV8_0r]>;

def MRS    : MRSI;
def MSR    : MSRI;
def MSRpstateImm1 : MSRpstateImm0_1;
def MSRpstateImm4 : MSRpstateImm0_15;

def : Pat<(AArch64mrs imm:$id),
          (MRS imm:$id)>;

// The thread pointer (on Linux, at least, where this has been implemented) is
// TPIDR_EL0.
def MOVbaseTLS : Pseudo<(outs GPR64:$dst), (ins),
                       [(set GPR64:$dst, AArch64threadpointer)]>, Sched<[WriteSys]>;

// This gets lowered into a 24-byte instruction sequence
let Defs = [ X9, X16, X17, NZCV ], Size = 24 in {
def KCFI_CHECK : Pseudo<
  (outs), (ins GPR64:$ptr, i32imm:$type), []>, Sched<[]>;
}

let Uses = [ X9 ], Defs = [ X16, X17, LR, NZCV ] in {
def HWASAN_CHECK_MEMACCESS : Pseudo<
  (outs), (ins GPR64noip:$ptr, i32imm:$accessinfo),
  [(int_hwasan_check_memaccess X9, GPR64noip:$ptr, (i32 timm:$accessinfo))]>,
  Sched<[]>;
}

let Uses = [ X20 ], Defs = [ X16, X17, LR, NZCV ] in {
def HWASAN_CHECK_MEMACCESS_SHORTGRANULES : Pseudo<
  (outs), (ins GPR64noip:$ptr, i32imm:$accessinfo),
  [(int_hwasan_check_memaccess_shortgranules X20, GPR64noip:$ptr, (i32 timm:$accessinfo))]>,
  Sched<[]>;
}

let Defs = [ X16, X17, LR, NZCV ] in {
def HWASAN_CHECK_MEMACCESS_FIXEDSHADOW : Pseudo<
  (outs), (ins GPR64noip:$ptr, i32imm:$accessinfo, i64imm:$fixed_shadow),
  [(int_hwasan_check_memaccess_fixedshadow GPR64noip:$ptr, (i32 timm:$accessinfo), (i64 timm:$fixed_shadow))]>,
  Sched<[]>;
}

let Defs = [ X16, X17, LR, NZCV ] in {
def HWASAN_CHECK_MEMACCESS_SHORTGRANULES_FIXEDSHADOW : Pseudo<
  (outs), (ins GPR64noip:$ptr, i32imm:$accessinfo, i64imm:$fixed_shadow),
  [(int_hwasan_check_memaccess_shortgranules_fixedshadow GPR64noip:$ptr, (i32 timm:$accessinfo), (i64 timm:$fixed_shadow))]>,
  Sched<[]>;
}

// The virtual cycle counter register is CNTVCT_EL0.
def : Pat<(readcyclecounter), (MRS 0xdf02)>;

// FPCR and FPSR registers.
let Uses = [FPCR] in
def MRS_FPCR : Pseudo<(outs GPR64:$dst), (ins),
                      [(set GPR64:$dst, (int_aarch64_get_fpcr))]>,
               PseudoInstExpansion<(MRS GPR64:$dst, 0xda20)>,
               Sched<[WriteSys]>;
let Defs = [FPCR] in
def MSR_FPCR : Pseudo<(outs), (ins GPR64:$val),
                      [(int_aarch64_set_fpcr i64:$val)]>,
               PseudoInstExpansion<(MSR 0xda20, GPR64:$val)>,
               Sched<[WriteSys]>;

let Uses = [FPSR] in
def MRS_FPSR : Pseudo<(outs GPR64:$dst), (ins),
                      [(set GPR64:$dst, (int_aarch64_get_fpsr))]>,
               PseudoInstExpansion<(MRS GPR64:$dst, 0xda21)>,
               Sched<[WriteSys]>;
let Defs = [FPSR] in
def MSR_FPSR : Pseudo<(outs), (ins GPR64:$val),
                      [(int_aarch64_set_fpsr i64:$val)]>,
               PseudoInstExpansion<(MSR 0xda21, GPR64:$val)>,
               Sched<[WriteSys]>;

// Generic system instructions
def SYSxt  : SystemXtI<0, "sys">;
def SYSLxt : SystemLXtI<1, "sysl">;

def : InstAlias<"sys $op1, $Cn, $Cm, $op2",
                (SYSxt imm0_7:$op1, sys_cr_op:$Cn,
                 sys_cr_op:$Cm, imm0_7:$op2, XZR)>;


let Predicates = [HasTME] in {

def TSTART : TMSystemI<0b0000, "tstart",
                      [(set GPR64:$Rt, (int_aarch64_tstart))]>;

def TCOMMIT : TMSystemINoOperand<0b0000, "tcommit", [(int_aarch64_tcommit)]>;

def TCANCEL : TMSystemException<0b011, "tcancel",
                                [(int_aarch64_tcancel timm64_0_65535:$imm)]>;

def TTEST : TMSystemI<0b0001, "ttest", [(set GPR64:$Rt, (int_aarch64_ttest))]> {
  let mayLoad = 0;
  let mayStore = 0;
}
} // HasTME

//===----------------------------------------------------------------------===//
// Move immediate instructions.
//===----------------------------------------------------------------------===//

defm MOVK : InsertImmediate<0b11, "movk">;
defm MOVN : MoveImmediate<0b00, "movn">;

let PostEncoderMethod = "fixMOVZ" in
defm MOVZ : MoveImmediate<0b10, "movz">;

// First group of aliases covers an implicit "lsl #0".
def : InstAlias<"movk $dst, $imm", (MOVKWi GPR32:$dst, timm32_0_65535:$imm, 0), 0>;
def : InstAlias<"movk $dst, $imm", (MOVKXi GPR64:$dst, timm32_0_65535:$imm, 0), 0>;
def : InstAlias<"movn $dst, $imm", (MOVNWi GPR32:$dst, timm32_0_65535:$imm, 0)>;
def : InstAlias<"movn $dst, $imm", (MOVNXi GPR64:$dst, timm32_0_65535:$imm, 0)>;
def : InstAlias<"movz $dst, $imm", (MOVZWi GPR32:$dst, timm32_0_65535:$imm, 0)>;
def : InstAlias<"movz $dst, $imm", (MOVZXi GPR64:$dst, timm32_0_65535:$imm, 0)>;

// Next, we have various ELF relocations with the ":XYZ_g0:sym" syntax.
def : InstAlias<"movz $Rd, $sym", (MOVZXi GPR64:$Rd, movw_symbol_g3:$sym, 48)>;
def : InstAlias<"movz $Rd, $sym", (MOVZXi GPR64:$Rd, movw_symbol_g2:$sym, 32)>;
def : InstAlias<"movz $Rd, $sym", (MOVZXi GPR64:$Rd, movw_symbol_g1:$sym, 16)>;
def : InstAlias<"movz $Rd, $sym", (MOVZXi GPR64:$Rd, movw_symbol_g0:$sym, 0)>;

def : InstAlias<"movn $Rd, $sym", (MOVNXi GPR64:$Rd, movw_symbol_g3:$sym, 48)>;
def : InstAlias<"movn $Rd, $sym", (MOVNXi GPR64:$Rd, movw_symbol_g2:$sym, 32)>;
def : InstAlias<"movn $Rd, $sym", (MOVNXi GPR64:$Rd, movw_symbol_g1:$sym, 16)>;
def : InstAlias<"movn $Rd, $sym", (MOVNXi GPR64:$Rd, movw_symbol_g0:$sym, 0)>;

def : InstAlias<"movk $Rd, $sym", (MOVKXi GPR64:$Rd, movw_symbol_g3:$sym, 48), 0>;
def : InstAlias<"movk $Rd, $sym", (MOVKXi GPR64:$Rd, movw_symbol_g2:$sym, 32), 0>;
def : InstAlias<"movk $Rd, $sym", (MOVKXi GPR64:$Rd, movw_symbol_g1:$sym, 16), 0>;
def : InstAlias<"movk $Rd, $sym", (MOVKXi GPR64:$Rd, movw_symbol_g0:$sym, 0), 0>;

def : InstAlias<"movz $Rd, $sym", (MOVZWi GPR32:$Rd, movw_symbol_g1:$sym, 16)>;
def : InstAlias<"movz $Rd, $sym", (MOVZWi GPR32:$Rd, movw_symbol_g0:$sym, 0)>;

def : InstAlias<"movn $Rd, $sym", (MOVNWi GPR32:$Rd, movw_symbol_g1:$sym, 16)>;
def : InstAlias<"movn $Rd, $sym", (MOVNWi GPR32:$Rd, movw_symbol_g0:$sym, 0)>;

def : InstAlias<"movk $Rd, $sym", (MOVKWi GPR32:$Rd, movw_symbol_g1:$sym, 16), 0>;
def : InstAlias<"movk $Rd, $sym", (MOVKWi GPR32:$Rd, movw_symbol_g0:$sym, 0), 0>;

// Final group of aliases covers true "mov $Rd, $imm" cases.
multiclass movw_mov_alias<string basename,Instruction INST, RegisterClass GPR,
                          int width, int shift> {
  def _asmoperand : AsmOperandClass {
    let Name = basename # width # "_lsl" # shift # "MovAlias";
    let PredicateMethod = "is" # basename # "MovAlias<" # width # ", "
                               # shift # ">";
    let RenderMethod = "add" # basename # "MovAliasOperands<" # shift # ">";
  }

  def _movimm : Operand<i32> {
    let ParserMatchClass = !cast<AsmOperandClass>(NAME # "_asmoperand");
  }

  def : InstAlias<"mov $Rd, $imm",
                  (INST GPR:$Rd, !cast<Operand>(NAME # "_movimm"):$imm, shift)>;
}

defm : movw_mov_alias<"MOVZ", MOVZWi, GPR32, 32, 0>;
defm : movw_mov_alias<"MOVZ", MOVZWi, GPR32, 32, 16>;

defm : movw_mov_alias<"MOVZ", MOVZXi, GPR64, 64, 0>;
defm : movw_mov_alias<"MOVZ", MOVZXi, GPR64, 64, 16>;
defm : movw_mov_alias<"MOVZ", MOVZXi, GPR64, 64, 32>;
defm : movw_mov_alias<"MOVZ", MOVZXi, GPR64, 64, 48>;

defm : movw_mov_alias<"MOVN", MOVNWi, GPR32, 32, 0>;
defm : movw_mov_alias<"MOVN", MOVNWi, GPR32, 32, 16>;

defm : movw_mov_alias<"MOVN", MOVNXi, GPR64, 64, 0>;
defm : movw_mov_alias<"MOVN", MOVNXi, GPR64, 64, 16>;
defm : movw_mov_alias<"MOVN", MOVNXi, GPR64, 64, 32>;
defm : movw_mov_alias<"MOVN", MOVNXi, GPR64, 64, 48>;

let isReMaterializable = 1, isCodeGenOnly = 1, isMoveImm = 1,
    isAsCheapAsAMove = 1 in {
// FIXME: The following pseudo instructions are only needed because remat
// cannot handle multiple instructions.  When that changes, we can select
// directly to the real instructions and get rid of these pseudos.

def MOVi32imm
    : Pseudo<(outs GPR32:$dst), (ins i32imm:$src),
             [(set GPR32:$dst, imm:$src)]>,
      Sched<[WriteImm]>;
def MOVi64imm
    : Pseudo<(outs GPR64:$dst), (ins i64imm:$src),
             [(set GPR64:$dst, imm:$src)]>,
      Sched<[WriteImm]>;
} // isReMaterializable, isCodeGenOnly

// If possible, we want to use MOVi32imm even for 64-bit moves. This gives the
// eventual expansion code fewer bits to worry about getting right. Marshalling
// the types is a little tricky though:
def i64imm_32bit : ImmLeaf<i64, [{
  return (Imm & 0xffffffffULL) == static_cast<uint64_t>(Imm);
}]>;

def s64imm_32bit : ImmLeaf<i64, [{
  int64_t Imm64 = static_cast<int64_t>(Imm);
  return Imm64 >= std::numeric_limits<int32_t>::min() &&
         Imm64 <= std::numeric_limits<int32_t>::max();
}]>;

def trunc_imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue(), SDLoc(N), MVT::i32);
}]>;

def gi_trunc_imm : GICustomOperandRenderer<"renderTruncImm">,
  GISDNodeXFormEquiv<trunc_imm>;

let Predicates = [OptimizedGISelOrOtherSelector] in {
// The SUBREG_TO_REG isn't eliminated at -O0, which can result in pointless
// copies.
def : Pat<(i64 i64imm_32bit:$src),
          (SUBREG_TO_REG (i64 0), (MOVi32imm (trunc_imm imm:$src)), sub_32)>;
}

// Materialize FP constants via MOVi32imm/MOVi64imm (MachO large code model).
def bitcast_fpimm_to_i32 : SDNodeXForm<fpimm, [{
return CurDAG->getTargetConstant(
  N->getValueAPF().bitcastToAPInt().getZExtValue(), SDLoc(N), MVT::i32);
}]>;

def bitcast_fpimm_to_i64 : SDNodeXForm<fpimm, [{
return CurDAG->getTargetConstant(
  N->getValueAPF().bitcastToAPInt().getZExtValue(), SDLoc(N), MVT::i64);
}]>;


def : Pat<(f32 fpimm:$in),
  (COPY_TO_REGCLASS (MOVi32imm (bitcast_fpimm_to_i32 f32:$in)), FPR32)>;
def : Pat<(f64 fpimm:$in),
  (COPY_TO_REGCLASS (MOVi64imm (bitcast_fpimm_to_i64 f64:$in)), FPR64)>;


// Deal with the various forms of (ELF) large addressing with MOVZ/MOVK
// sequences.
def : Pat<(AArch64WrapperLarge tglobaladdr:$g3, tglobaladdr:$g2,
                             tglobaladdr:$g1, tglobaladdr:$g0),
          (MOVKXi (MOVKXi (MOVKXi (MOVZXi tglobaladdr:$g0, 0),
                                  tglobaladdr:$g1, 16),
                          tglobaladdr:$g2, 32),
                  tglobaladdr:$g3, 48)>;

def : Pat<(AArch64WrapperLarge tblockaddress:$g3, tblockaddress:$g2,
                             tblockaddress:$g1, tblockaddress:$g0),
          (MOVKXi (MOVKXi (MOVKXi (MOVZXi tblockaddress:$g0, 0),
                                  tblockaddress:$g1, 16),
                          tblockaddress:$g2, 32),
                  tblockaddress:$g3, 48)>;

def : Pat<(AArch64WrapperLarge tconstpool:$g3, tconstpool:$g2,
                             tconstpool:$g1, tconstpool:$g0),
          (MOVKXi (MOVKXi (MOVKXi (MOVZXi tconstpool:$g0, 0),
                                  tconstpool:$g1, 16),
                          tconstpool:$g2, 32),
                  tconstpool:$g3, 48)>;

def : Pat<(AArch64WrapperLarge tjumptable:$g3, tjumptable:$g2,
                             tjumptable:$g1, tjumptable:$g0),
          (MOVKXi (MOVKXi (MOVKXi (MOVZXi tjumptable:$g0, 0),
                                  tjumptable:$g1, 16),
                          tjumptable:$g2, 32),
                  tjumptable:$g3, 48)>;


//===----------------------------------------------------------------------===//
// Arithmetic instructions.
//===----------------------------------------------------------------------===//

// Add/subtract with carry.
defm ADC : AddSubCarry<0, "adc", "adcs", AArch64adc, AArch64adc_flag>;
defm SBC : AddSubCarry<1, "sbc", "sbcs", AArch64sbc, AArch64sbc_flag>;

def : InstAlias<"ngc $dst, $src",  (SBCWr  GPR32:$dst, WZR, GPR32:$src)>;
def : InstAlias<"ngc $dst, $src",  (SBCXr  GPR64:$dst, XZR, GPR64:$src)>;
def : InstAlias<"ngcs $dst, $src", (SBCSWr GPR32:$dst, WZR, GPR32:$src)>;
def : InstAlias<"ngcs $dst, $src", (SBCSXr GPR64:$dst, XZR, GPR64:$src)>;

// Add/subtract
defm ADD : AddSub<0, "add", "sub", add>;
defm SUB : AddSub<1, "sub", "add">;

def : InstAlias<"mov $dst, $src",
                (ADDWri GPR32sponly:$dst, GPR32sp:$src, 0, 0)>;
def : InstAlias<"mov $dst, $src",
                (ADDWri GPR32sp:$dst, GPR32sponly:$src, 0, 0)>;
def : InstAlias<"mov $dst, $src",
                (ADDXri GPR64sponly:$dst, GPR64sp:$src, 0, 0)>;
def : InstAlias<"mov $dst, $src",
                (ADDXri GPR64sp:$dst, GPR64sponly:$src, 0, 0)>;

defm ADDS : AddSubS<0, "adds", AArch64add_flag, "cmn", "subs", "cmp">;
defm SUBS : AddSubS<1, "subs", AArch64sub_flag, "cmp", "adds", "cmn">;

def copyFromSP: PatLeaf<(i64 GPR64:$src), [{
  return N->getOpcode() == ISD::CopyFromReg &&
         cast<RegisterSDNode>(N->getOperand(1))->getReg() == AArch64::SP;
}]>;

// Use SUBS instead of SUB to enable CSE between SUBS and SUB.
def : Pat<(sub GPR32sp:$Rn, addsub_shifted_imm32:$imm),
          (SUBSWri GPR32sp:$Rn, addsub_shifted_imm32:$imm)>;
def : Pat<(sub GPR64sp:$Rn, addsub_shifted_imm64:$imm),
          (SUBSXri GPR64sp:$Rn, addsub_shifted_imm64:$imm)>;
def : Pat<(sub GPR32:$Rn, GPR32:$Rm),
          (SUBSWrr GPR32:$Rn, GPR32:$Rm)>;
def : Pat<(sub GPR64:$Rn, GPR64:$Rm),
          (SUBSXrr GPR64:$Rn, GPR64:$Rm)>;
def : Pat<(sub GPR32:$Rn, arith_shifted_reg32:$Rm),
          (SUBSWrs GPR32:$Rn, arith_shifted_reg32:$Rm)>;
def : Pat<(sub GPR64:$Rn, arith_shifted_reg64:$Rm),
          (SUBSXrs GPR64:$Rn, arith_shifted_reg64:$Rm)>;
let AddedComplexity = 1 in {
def : Pat<(sub GPR32sp:$R2, arith_extended_reg32_i32:$R3),
          (SUBSWrx GPR32sp:$R2, arith_extended_reg32_i32:$R3)>;
def : Pat<(sub GPR64sp:$R2, arith_extended_reg32to64_i64:$R3),
          (SUBSXrx GPR64sp:$R2, arith_extended_reg32to64_i64:$R3)>;
def : Pat<(sub copyFromSP:$R2, (arith_uxtx GPR64:$R3, arith_extendlsl64:$imm)),
          (SUBXrx64 GPR64sp:$R2, GPR64:$R3, arith_extendlsl64:$imm)>;
}

// Because of the immediate format for add/sub-imm instructions, the
// expression (add x, -1) must be transformed to (SUB{W,X}ri x, 1).
//  These patterns capture that transformation.
let AddedComplexity = 1 in {
def : Pat<(add GPR32:$Rn, neg_addsub_shifted_imm32:$imm),
          (SUBSWri GPR32:$Rn, neg_addsub_shifted_imm32:$imm)>;
def : Pat<(add GPR64:$Rn, neg_addsub_shifted_imm64:$imm),
          (SUBSXri GPR64:$Rn, neg_addsub_shifted_imm64:$imm)>;
def : Pat<(sub GPR32:$Rn, neg_addsub_shifted_imm32:$imm),
          (ADDWri GPR32:$Rn, neg_addsub_shifted_imm32:$imm)>;
def : Pat<(sub GPR64:$Rn, neg_addsub_shifted_imm64:$imm),
          (ADDXri GPR64:$Rn, neg_addsub_shifted_imm64:$imm)>;
}

// Because of the immediate format for add/sub-imm instructions, the
// expression (add x, -1) must be transformed to (SUB{W,X}ri x, 1).
//  These patterns capture that transformation.
let AddedComplexity = 1 in {
def : Pat<(AArch64add_flag GPR32:$Rn, neg_addsub_shifted_imm32:$imm),
          (SUBSWri GPR32:$Rn, neg_addsub_shifted_imm32:$imm)>;
def : Pat<(AArch64add_flag GPR64:$Rn, neg_addsub_shifted_imm64:$imm),
          (SUBSXri GPR64:$Rn, neg_addsub_shifted_imm64:$imm)>;
def : Pat<(AArch64sub_flag GPR32:$Rn, neg_addsub_shifted_imm32:$imm),
          (ADDSWri GPR32:$Rn, neg_addsub_shifted_imm32:$imm)>;
def : Pat<(AArch64sub_flag GPR64:$Rn, neg_addsub_shifted_imm64:$imm),
          (ADDSXri GPR64:$Rn, neg_addsub_shifted_imm64:$imm)>;
}

def : InstAlias<"neg $dst, $src", (SUBWrs GPR32:$dst, WZR, GPR32:$src, 0), 3>;
def : InstAlias<"neg $dst, $src", (SUBXrs GPR64:$dst, XZR, GPR64:$src, 0), 3>;
def : InstAlias<"neg $dst, $src$shift",
                (SUBWrs GPR32:$dst, WZR, GPR32:$src, arith_shift32:$shift), 2>;
def : InstAlias<"neg $dst, $src$shift",
                (SUBXrs GPR64:$dst, XZR, GPR64:$src, arith_shift64:$shift), 2>;

def : InstAlias<"negs $dst, $src", (SUBSWrs GPR32:$dst, WZR, GPR32:$src, 0), 3>;
def : InstAlias<"negs $dst, $src", (SUBSXrs GPR64:$dst, XZR, GPR64:$src, 0), 3>;
def : InstAlias<"negs $dst, $src$shift",
                (SUBSWrs GPR32:$dst, WZR, GPR32:$src, arith_shift32:$shift), 2>;
def : InstAlias<"negs $dst, $src$shift",
                (SUBSXrs GPR64:$dst, XZR, GPR64:$src, arith_shift64:$shift), 2>;


// Unsigned/Signed divide
defm UDIV : Div<0, "udiv", udiv>;
defm SDIV : Div<1, "sdiv", sdiv>;

def : Pat<(int_aarch64_udiv GPR32:$Rn, GPR32:$Rm), (UDIVWr GPR32:$Rn, GPR32:$Rm)>;
def : Pat<(int_aarch64_udiv GPR64:$Rn, GPR64:$Rm), (UDIVXr GPR64:$Rn, GPR64:$Rm)>;
def : Pat<(int_aarch64_sdiv GPR32:$Rn, GPR32:$Rm), (SDIVWr GPR32:$Rn, GPR32:$Rm)>;
def : Pat<(int_aarch64_sdiv GPR64:$Rn, GPR64:$Rm), (SDIVXr GPR64:$Rn, GPR64:$Rm)>;

// Variable shift
defm ASRV : Shift<0b10, "asr", sra>;
defm LSLV : Shift<0b00, "lsl", shl>;
defm LSRV : Shift<0b01, "lsr", srl>;
defm RORV : Shift<0b11, "ror", rotr>;

def : ShiftAlias<"asrv", ASRVWr, GPR32>;
def : ShiftAlias<"asrv", ASRVXr, GPR64>;
def : ShiftAlias<"lslv", LSLVWr, GPR32>;
def : ShiftAlias<"lslv", LSLVXr, GPR64>;
def : ShiftAlias<"lsrv", LSRVWr, GPR32>;
def : ShiftAlias<"lsrv", LSRVXr, GPR64>;
def : ShiftAlias<"rorv", RORVWr, GPR32>;
def : ShiftAlias<"rorv", RORVXr, GPR64>;

// Multiply-add
let AddedComplexity = 5 in {
defm MADD : MulAccum<0, "madd">;
defm MSUB : MulAccum<1, "msub">;

def : Pat<(i32 (mul GPR32:$Rn, GPR32:$Rm)),
          (MADDWrrr GPR32:$Rn, GPR32:$Rm, WZR)>;
def : Pat<(i64 (mul GPR64:$Rn, GPR64:$Rm)),
          (MADDXrrr GPR64:$Rn, GPR64:$Rm, XZR)>;

def : Pat<(i32 (ineg (mul GPR32:$Rn, GPR32:$Rm))),
          (MSUBWrrr GPR32:$Rn, GPR32:$Rm, WZR)>;
def : Pat<(i64 (ineg (mul GPR64:$Rn, GPR64:$Rm))),
          (MSUBXrrr GPR64:$Rn, GPR64:$Rm, XZR)>;
def : Pat<(i32 (mul (ineg GPR32:$Rn), GPR32:$Rm)),
          (MSUBWrrr GPR32:$Rn, GPR32:$Rm, WZR)>;
def : Pat<(i64 (mul (ineg GPR64:$Rn), GPR64:$Rm)),
          (MSUBXrrr GPR64:$Rn, GPR64:$Rm, XZR)>;
} // AddedComplexity = 5

let AddedComplexity = 5 in {
def SMADDLrrr : WideMulAccum<0, 0b001, "smaddl", add, sext>;
def SMSUBLrrr : WideMulAccum<1, 0b001, "smsubl", sub, sext>;
def UMADDLrrr : WideMulAccum<0, 0b101, "umaddl", add, zext>;
def UMSUBLrrr : WideMulAccum<1, 0b101, "umsubl", sub, zext>;

def : Pat<(i64 (mul (sext_inreg GPR64:$Rn, i32), (sext_inreg GPR64:$Rm, i32))),
          (SMADDLrrr (EXTRACT_SUBREG $Rn, sub_32), (EXTRACT_SUBREG $Rm, sub_32), XZR)>;
def : Pat<(i64 (mul (sext_inreg GPR64:$Rn, i32), (sext GPR32:$Rm))),
          (SMADDLrrr (EXTRACT_SUBREG $Rn, sub_32), $Rm, XZR)>;
def : Pat<(i64 (mul (sext GPR32:$Rn), (sext GPR32:$Rm))),
          (SMADDLrrr GPR32:$Rn, GPR32:$Rm, XZR)>;
def : Pat<(i64 (mul (and GPR64:$Rn, 0xFFFFFFFF), (and GPR64:$Rm, 0xFFFFFFFF))),
          (UMADDLrrr (EXTRACT_SUBREG $Rn, sub_32), (EXTRACT_SUBREG $Rm, sub_32), XZR)>;
def : Pat<(i64 (mul (and GPR64:$Rn, 0xFFFFFFFF), (zext GPR32:$Rm))),
          (UMADDLrrr (EXTRACT_SUBREG $Rn, sub_32), $Rm, XZR)>;
def : Pat<(i64 (mul (zext GPR32:$Rn), (zext GPR32:$Rm))),
          (UMADDLrrr GPR32:$Rn, GPR32:$Rm, XZR)>;

def : Pat<(i64 (ineg (mul (sext GPR32:$Rn), (sext GPR32:$Rm)))),
          (SMSUBLrrr GPR32:$Rn, GPR32:$Rm, XZR)>;
def : Pat<(i64 (ineg (mul (zext GPR32:$Rn), (zext GPR32:$Rm)))),
          (UMSUBLrrr GPR32:$Rn, GPR32:$Rm, XZR)>;

def : Pat<(i64 (mul (sext GPR32:$Rn), (s64imm_32bit:$C))),
          (SMADDLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), XZR)>;
def : Pat<(i64 (mul (zext GPR32:$Rn), (i64imm_32bit:$C))),
          (UMADDLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), XZR)>;
def : Pat<(i64 (mul (sext_inreg GPR64:$Rn, i32), (s64imm_32bit:$C))),
          (SMADDLrrr (i32 (EXTRACT_SUBREG GPR64:$Rn, sub_32)),
                     (MOVi32imm (trunc_imm imm:$C)), XZR)>;

def : Pat<(i64 (ineg (mul (sext GPR32:$Rn), (s64imm_32bit:$C)))),
          (SMSUBLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), XZR)>;
def : Pat<(i64 (ineg (mul (zext GPR32:$Rn), (i64imm_32bit:$C)))),
          (UMSUBLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), XZR)>;
def : Pat<(i64 (ineg (mul (sext_inreg GPR64:$Rn, i32), (s64imm_32bit:$C)))),
          (SMSUBLrrr (i32 (EXTRACT_SUBREG GPR64:$Rn, sub_32)),
                     (MOVi32imm (trunc_imm imm:$C)), XZR)>;

def : Pat<(i64 (add (mul (sext GPR32:$Rn), (s64imm_32bit:$C)), GPR64:$Ra)),
          (SMADDLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), GPR64:$Ra)>;
def : Pat<(i64 (add (mul (zext GPR32:$Rn), (i64imm_32bit:$C)), GPR64:$Ra)),
          (UMADDLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), GPR64:$Ra)>;
def : Pat<(i64 (add (mul (sext_inreg GPR64:$Rn, i32), (s64imm_32bit:$C)),
                    GPR64:$Ra)),
          (SMADDLrrr (i32 (EXTRACT_SUBREG GPR64:$Rn, sub_32)),
                     (MOVi32imm (trunc_imm imm:$C)), GPR64:$Ra)>;

def : Pat<(i64 (sub GPR64:$Ra, (mul (sext GPR32:$Rn), (s64imm_32bit:$C)))),
          (SMSUBLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), GPR64:$Ra)>;
def : Pat<(i64 (sub GPR64:$Ra, (mul (zext GPR32:$Rn), (i64imm_32bit:$C)))),
          (UMSUBLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), GPR64:$Ra)>;
def : Pat<(i64 (sub GPR64:$Ra, (mul (sext_inreg GPR64:$Rn, i32),
                                    (s64imm_32bit:$C)))),
          (SMSUBLrrr (i32 (EXTRACT_SUBREG GPR64:$Rn, sub_32)),
                     (MOVi32imm (trunc_imm imm:$C)), GPR64:$Ra)>;

def : Pat<(i64 (smullwithsignbits GPR64:$Rn, GPR64:$Rm)),
          (SMADDLrrr (EXTRACT_SUBREG $Rn, sub_32), (EXTRACT_SUBREG $Rm, sub_32), XZR)>;
def : Pat<(i64 (smullwithsignbits GPR64:$Rn, (sext GPR32:$Rm))),
          (SMADDLrrr (EXTRACT_SUBREG $Rn, sub_32), $Rm, XZR)>;

def : Pat<(i64 (add (smullwithsignbits GPR64:$Rn, GPR64:$Rm), GPR64:$Ra)),
          (SMADDLrrr (EXTRACT_SUBREG $Rn, sub_32), (EXTRACT_SUBREG $Rm, sub_32), GPR64:$Ra)>;
def : Pat<(i64 (add (smullwithsignbits GPR64:$Rn, (sext GPR32:$Rm)), GPR64:$Ra)),
          (SMADDLrrr (EXTRACT_SUBREG $Rn, sub_32), $Rm, GPR64:$Ra)>;

def : Pat<(i64 (ineg (smullwithsignbits GPR64:$Rn, GPR64:$Rm))),
          (SMSUBLrrr (EXTRACT_SUBREG $Rn, sub_32), (EXTRACT_SUBREG $Rm, sub_32), XZR)>;
def : Pat<(i64 (ineg (smullwithsignbits GPR64:$Rn, (sext GPR32:$Rm)))),
          (SMSUBLrrr (EXTRACT_SUBREG $Rn, sub_32), $Rm, XZR)>;

def : Pat<(i64 (sub GPR64:$Ra, (smullwithsignbits GPR64:$Rn, GPR64:$Rm))),
          (SMSUBLrrr (EXTRACT_SUBREG $Rn, sub_32), (EXTRACT_SUBREG $Rm, sub_32), GPR64:$Ra)>;
def : Pat<(i64 (sub GPR64:$Ra, (smullwithsignbits GPR64:$Rn, (sext GPR32:$Rm)))),
          (SMSUBLrrr (EXTRACT_SUBREG $Rn, sub_32), $Rm, GPR64:$Ra)>;

def : Pat<(i64 (mul top32Zero:$Rn, top32Zero:$Rm)),
          (UMADDLrrr (EXTRACT_SUBREG $Rn, sub_32), (EXTRACT_SUBREG $Rm, sub_32), XZR)>;
def : Pat<(i64 (mul top32Zero:$Rn, (zext GPR32:$Rm))),
          (UMADDLrrr (EXTRACT_SUBREG $Rn, sub_32), $Rm, XZR)>;

def : Pat<(i64 (add (mul top32Zero:$Rn, top32Zero:$Rm), GPR64:$Ra)),
          (UMADDLrrr (EXTRACT_SUBREG $Rn, sub_32), (EXTRACT_SUBREG $Rm, sub_32), GPR64:$Ra)>;
def : Pat<(i64 (add (mul top32Zero:$Rn, (zext GPR32:$Rm)), GPR64:$Ra)),
          (UMADDLrrr (EXTRACT_SUBREG $Rn, sub_32), $Rm, GPR64:$Ra)>;

def : Pat<(i64 (ineg (mul top32Zero:$Rn, top32Zero:$Rm))),
          (UMSUBLrrr (EXTRACT_SUBREG $Rn, sub_32), (EXTRACT_SUBREG $Rm, sub_32), XZR)>;
def : Pat<(i64 (ineg (mul top32Zero:$Rn, (zext GPR32:$Rm)))),
          (UMSUBLrrr (EXTRACT_SUBREG $Rn, sub_32), $Rm, XZR)>;

def : Pat<(i64 (sub GPR64:$Ra, (mul top32Zero:$Rn, top32Zero:$Rm))),
          (UMSUBLrrr (EXTRACT_SUBREG $Rn, sub_32), (EXTRACT_SUBREG $Rm, sub_32), GPR64:$Ra)>;
def : Pat<(i64 (sub GPR64:$Ra, (mul top32Zero:$Rn, (zext GPR32:$Rm)))),
          (UMSUBLrrr (EXTRACT_SUBREG $Rn, sub_32), $Rm, GPR64:$Ra)>;
} // AddedComplexity = 5

def : MulAccumWAlias<"mul", MADDWrrr>;
def : MulAccumXAlias<"mul", MADDXrrr>;
def : MulAccumWAlias<"mneg", MSUBWrrr>;
def : MulAccumXAlias<"mneg", MSUBXrrr>;
def : WideMulAccumAlias<"smull", SMADDLrrr>;
def : WideMulAccumAlias<"smnegl", SMSUBLrrr>;
def : WideMulAccumAlias<"umull", UMADDLrrr>;
def : WideMulAccumAlias<"umnegl", UMSUBLrrr>;

// Multiply-high
def SMULHrr : MulHi<0b010, "smulh", mulhs>;
def UMULHrr : MulHi<0b110, "umulh", mulhu>;

// CRC32
def CRC32Brr : BaseCRC32<0, 0b00, 0, GPR32, int_aarch64_crc32b, "crc32b">;
def CRC32Hrr : BaseCRC32<0, 0b01, 0, GPR32, int_aarch64_crc32h, "crc32h">;
def CRC32Wrr : BaseCRC32<0, 0b10, 0, GPR32, int_aarch64_crc32w, "crc32w">;
def CRC32Xrr : BaseCRC32<1, 0b11, 0, GPR64, int_aarch64_crc32x, "crc32x">;

def CRC32CBrr : BaseCRC32<0, 0b00, 1, GPR32, int_aarch64_crc32cb, "crc32cb">;
def CRC32CHrr : BaseCRC32<0, 0b01, 1, GPR32, int_aarch64_crc32ch, "crc32ch">;
def CRC32CWrr : BaseCRC32<0, 0b10, 1, GPR32, int_aarch64_crc32cw, "crc32cw">;
def CRC32CXrr : BaseCRC32<1, 0b11, 1, GPR64, int_aarch64_crc32cx, "crc32cx">;

// v8.1 atomic CAS
defm CAS   : CompareAndSwap<0, 0, "">;
defm CASA  : CompareAndSwap<1, 0, "a">;
defm CASL  : CompareAndSwap<0, 1, "l">;
defm CASAL : CompareAndSwap<1, 1, "al">;

// v8.1 atomic CASP
defm CASP   : CompareAndSwapPair<0, 0, "">;
defm CASPA  : CompareAndSwapPair<1, 0, "a">;
defm CASPL  : CompareAndSwapPair<0, 1, "l">;
defm CASPAL : CompareAndSwapPair<1, 1, "al">;

// v8.1 atomic SWP
defm SWP   : Swap<0, 0, "">;
defm SWPA  : Swap<1, 0, "a">;
defm SWPL  : Swap<0, 1, "l">;
defm SWPAL : Swap<1, 1, "al">;

// v8.1 atomic LD<OP>(register). Performs load and then ST<OP>(register)
defm LDADD   : LDOPregister<0b000, "add", 0, 0, "">;
defm LDADDA  : LDOPregister<0b000, "add", 1, 0, "a">;
defm LDADDL  : LDOPregister<0b000, "add", 0, 1, "l">;
defm LDADDAL : LDOPregister<0b000, "add", 1, 1, "al">;

defm LDCLR   : LDOPregister<0b001, "clr", 0, 0, "">;
defm LDCLRA  : LDOPregister<0b001, "clr", 1, 0, "a">;
defm LDCLRL  : LDOPregister<0b001, "clr", 0, 1, "l">;
defm LDCLRAL : LDOPregister<0b001, "clr", 1, 1, "al">;

defm LDEOR   : LDOPregister<0b010, "eor", 0, 0, "">;
defm LDEORA  : LDOPregister<0b010, "eor", 1, 0, "a">;
defm LDEORL  : LDOPregister<0b010, "eor", 0, 1, "l">;
defm LDEORAL : LDOPregister<0b010, "eor", 1, 1, "al">;

defm LDSET   : LDOPregister<0b011, "set", 0, 0, "">;
defm LDSETA  : LDOPregister<0b011, "set", 1, 0, "a">;
defm LDSETL  : LDOPregister<0b011, "set", 0, 1, "l">;
defm LDSETAL : LDOPregister<0b011, "set", 1, 1, "al">;

defm LDSMAX   : LDOPregister<0b100, "smax", 0, 0, "">;
defm LDSMAXA  : LDOPregister<0b100, "smax", 1, 0, "a">;
defm LDSMAXL  : LDOPregister<0b100, "smax", 0, 1, "l">;
defm LDSMAXAL : LDOPregister<0b100, "smax", 1, 1, "al">;

defm LDSMIN   : LDOPregister<0b101, "smin", 0, 0, "">;
defm LDSMINA  : LDOPregister<0b101, "smin", 1, 0, "a">;
defm LDSMINL  : LDOPregister<0b101, "smin", 0, 1, "l">;
defm LDSMINAL : LDOPregister<0b101, "smin", 1, 1, "al">;

defm LDUMAX   : LDOPregister<0b110, "umax", 0, 0, "">;
defm LDUMAXA  : LDOPregister<0b110, "umax", 1, 0, "a">;
defm LDUMAXL  : LDOPregister<0b110, "umax", 0, 1, "l">;
defm LDUMAXAL : LDOPregister<0b110, "umax", 1, 1, "al">;

defm LDUMIN   : LDOPregister<0b111, "umin", 0, 0, "">;
defm LDUMINA  : LDOPregister<0b111, "umin", 1, 0, "a">;
defm LDUMINL  : LDOPregister<0b111, "umin", 0, 1, "l">;
defm LDUMINAL : LDOPregister<0b111, "umin", 1, 1, "al">;

// v8.1 atomic ST<OP>(register) as aliases to "LD<OP>(register) when Rt=xZR"
defm : STOPregister<"stadd","LDADD">; // STADDx
defm : STOPregister<"stclr","LDCLR">; // STCLRx
defm : STOPregister<"steor","LDEOR">; // STEORx
defm : STOPregister<"stset","LDSET">; // STSETx
defm : STOPregister<"stsmax","LDSMAX">;// STSMAXx
defm : STOPregister<"stsmin","LDSMIN">;// STSMINx
defm : STOPregister<"stumax","LDUMAX">;// STUMAXx
defm : STOPregister<"stumin","LDUMIN">;// STUMINx

// v8.5 Memory Tagging Extension
let Predicates = [HasMTE] in {

def IRG   : BaseTwoOperandRegReg<0b1, 0b0, 0b000100, GPR64sp, "irg",
                                 int_aarch64_irg, GPR64sp, GPR64>, Sched<[]>;

def GMI   : BaseTwoOperandRegReg<0b1, 0b0, 0b000101, GPR64, "gmi",
                                 int_aarch64_gmi, GPR64sp>, Sched<[]> {
  let isNotDuplicable = 1;
}
def ADDG  : AddSubG<0, "addg", null_frag>;
def SUBG  : AddSubG<1, "subg", null_frag>;

def : InstAlias<"irg $dst, $src", (IRG GPR64sp:$dst, GPR64sp:$src, XZR), 1>;

def SUBP : SUBP<0, "subp", int_aarch64_subp>, Sched<[]>;
def SUBPS : SUBP<1, "subps", null_frag>, Sched<[]>{
  let Defs = [NZCV];
}

def : InstAlias<"cmpp $lhs, $rhs", (SUBPS XZR, GPR64sp:$lhs, GPR64sp:$rhs), 0>;

def LDG : MemTagLoad<"ldg", "\t$Rt, [$Rn, $offset]">;

def : Pat<(int_aarch64_addg (am_indexedu6s128 GPR64sp:$Rn, uimm6s16:$imm6), imm0_15:$imm4),
          (ADDG GPR64sp:$Rn, imm0_63:$imm6, imm0_15:$imm4)>;
def : Pat<(int_aarch64_ldg GPR64:$Rt, (am_indexeds9s128 GPR64sp:$Rn,  simm9s16:$offset)),
          (LDG GPR64:$Rt, GPR64sp:$Rn,  simm9s16:$offset)>;

def : InstAlias<"ldg $Rt, [$Rn]", (LDG GPR64:$Rt, GPR64sp:$Rn, 0), 1>;

def LDGM : MemTagVector<1, "ldgm", "\t$Rt, [$Rn]",
                   (outs GPR64:$Rt), (ins GPR64sp:$Rn)>;
def STGM : MemTagVector<0, "stgm", "\t$Rt, [$Rn]",
                   (outs), (ins GPR64:$Rt, GPR64sp:$Rn)>;
def STZGM : MemTagVector<0, "stzgm", "\t$Rt, [$Rn]",
                   (outs), (ins GPR64:$Rt, GPR64sp:$Rn)> {
  let Inst{23} = 0;
}

defm STG   : MemTagStore<0b00, "stg">;
defm STZG  : MemTagStore<0b01, "stzg">;
defm ST2G  : MemTagStore<0b10, "st2g">;
defm STZ2G : MemTagStore<0b11, "stz2g">;

def : Pat<(AArch64stg GPR64sp:$Rn, (am_indexeds9s128 GPR64sp:$Rm, simm9s16:$imm)),
          (STGi $Rn, $Rm, $imm)>;
def : Pat<(AArch64stzg GPR64sp:$Rn, (am_indexeds9s128 GPR64sp:$Rm, simm9s16:$imm)),
          (STZGi $Rn, $Rm, $imm)>;
def : Pat<(AArch64st2g GPR64sp:$Rn, (am_indexeds9s128 GPR64sp:$Rm, simm9s16:$imm)),
          (ST2Gi $Rn, $Rm, $imm)>;
def : Pat<(AArch64stz2g GPR64sp:$Rn, (am_indexeds9s128 GPR64sp:$Rm, simm9s16:$imm)),
          (STZ2Gi $Rn, $Rm, $imm)>;

defm STGP     : StorePairOffset <0b01, 0, GPR64z, simm7s16, "stgp">;
def  STGPpre  : StorePairPreIdx <0b01, 0, GPR64z, simm7s16, "stgp">;
def  STGPpost : StorePairPostIdx<0b01, 0, GPR64z, simm7s16, "stgp">;

def : Pat<(int_aarch64_stg GPR64:$Rt, (am_indexeds9s128 GPR64sp:$Rn, simm9s16:$offset)),
          (STGi GPR64:$Rt, GPR64sp:$Rn,  simm9s16:$offset)>;

def : Pat<(int_aarch64_stgp (am_indexed7s128 GPR64sp:$Rn, simm7s16:$imm), GPR64:$Rt, GPR64:$Rt2),
          (STGPi $Rt, $Rt2, $Rn, $imm)>;

def IRGstack
    : Pseudo<(outs GPR64sp:$Rd), (ins GPR64sp:$Rsp, GPR64:$Rm), []>,
      Sched<[]>;
def TAGPstack
    : Pseudo<(outs GPR64sp:$Rd), (ins GPR64sp:$Rn, uimm6s16:$imm6, GPR64sp:$Rm, imm0_15:$imm4), []>,
      Sched<[]>;

// Explicit SP in the first operand prevents ShrinkWrap optimization
// from leaving this instruction out of the stack frame. When IRGstack
// is transformed into IRG, this operand is replaced with the actual
// register / expression for the tagged base pointer of the current function.
def : Pat<(int_aarch64_irg_sp i64:$Rm), (IRGstack SP, i64:$Rm)>;

// Large STG to be expanded into a loop. $sz is the size, $Rn is start address.
// $Rn_wback is one past the end of the range. $Rm is the loop counter.
let isCodeGenOnly=1, mayStore=1, Defs=[NZCV] in {
def STGloop_wback
    : Pseudo<(outs GPR64common:$Rm, GPR64sp:$Rn_wback), (ins i64imm:$sz, GPR64sp:$Rn),
             [], "$Rn = $Rn_wback,@earlyclobber $Rn_wback,@earlyclobber $Rm" >,
      Sched<[WriteAdr, WriteST]>;

def STZGloop_wback
    : Pseudo<(outs GPR64common:$Rm, GPR64sp:$Rn_wback), (ins i64imm:$sz, GPR64sp:$Rn),
             [], "$Rn = $Rn_wback,@earlyclobber $Rn_wback,@earlyclobber $Rm" >,
      Sched<[WriteAdr, WriteST]>;

// A variant of the above where $Rn2 is an independent register not tied to the input register $Rn.
// Their purpose is to use a FrameIndex operand as $Rn (which of course can not be written back).
def STGloop
    : Pseudo<(outs GPR64common:$Rm, GPR64sp:$Rn2), (ins i64imm:$sz, GPR64sp:$Rn),
             [], "@earlyclobber $Rn2,@earlyclobber $Rm" >,
      Sched<[WriteAdr, WriteST]>;

def STZGloop
    : Pseudo<(outs GPR64common:$Rm, GPR64sp:$Rn2), (ins i64imm:$sz, GPR64sp:$Rn),
             [], "@earlyclobber $Rn2,@earlyclobber $Rm" >,
      Sched<[WriteAdr, WriteST]>;
}

} // Predicates = [HasMTE]

//===----------------------------------------------------------------------===//
// Logical instructions.
//===----------------------------------------------------------------------===//

// (immediate)
defm ANDS : LogicalImmS<0b11, "ands", AArch64and_flag, "bics">;
defm AND  : LogicalImm<0b00, "and", and, "bic">;
defm EOR  : LogicalImm<0b10, "eor", xor, "eon">;
defm ORR  : LogicalImm<0b01, "orr", or, "orn">;

// FIXME: these aliases *are* canonical sometimes (when movz can't be
// used). Actually, it seems to be working right now, but putting logical_immXX
// here is a bit dodgy on the AsmParser side too.
def : InstAlias<"mov $dst, $imm", (ORRWri GPR32sp:$dst, WZR,
                                          logical_imm32:$imm), 0>;
def : InstAlias<"mov $dst, $imm", (ORRXri GPR64sp:$dst, XZR,
                                          logical_imm64:$imm), 0>;


// (register)
defm ANDS : LogicalRegS<0b11, 0, "ands", AArch64and_flag>;
defm BICS : LogicalRegS<0b11, 1, "bics",
                        BinOpFrag<(AArch64and_flag node:$LHS, (not node:$RHS))>>;
defm AND  : LogicalReg<0b00, 0, "and", and>;
defm BIC  : LogicalReg<0b00, 1, "bic",
                       BinOpFrag<(and node:$LHS, (not node:$RHS))>, 3>;
defm EON  : LogicalReg<0b10, 1, "eon",
                       BinOpFrag<(not (xor node:$LHS, node:$RHS))>>;
defm EOR  : LogicalReg<0b10, 0, "eor", xor>;
defm ORN  : LogicalReg<0b01, 1, "orn",
                       BinOpFrag<(or node:$LHS, (not node:$RHS))>>;
defm ORR  : LogicalReg<0b01, 0, "orr", or>;

def : InstAlias<"mov $dst, $src", (ORRWrs GPR32:$dst, WZR, GPR32:$src, 0), 2>;
def : InstAlias<"mov $dst, $src", (ORRXrs GPR64:$dst, XZR, GPR64:$src, 0), 2>;

def : InstAlias<"mvn $Wd, $Wm", (ORNWrs GPR32:$Wd, WZR, GPR32:$Wm, 0), 3>;
def : InstAlias<"mvn $Xd, $Xm", (ORNXrs GPR64:$Xd, XZR, GPR64:$Xm, 0), 3>;

def : InstAlias<"mvn $Wd, $Wm$sh",
                (ORNWrs GPR32:$Wd, WZR, GPR32:$Wm, logical_shift32:$sh), 2>;
def : InstAlias<"mvn $Xd, $Xm$sh",
                (ORNXrs GPR64:$Xd, XZR, GPR64:$Xm, logical_shift64:$sh), 2>;

def : InstAlias<"tst $src1, $src2",
                (ANDSWri WZR, GPR32:$src1, logical_imm32:$src2), 2>;
def : InstAlias<"tst $src1, $src2",
                (ANDSXri XZR, GPR64:$src1, logical_imm64:$src2), 2>;

def : InstAlias<"tst $src1, $src2",
                        (ANDSWrs WZR, GPR32:$src1, GPR32:$src2, 0), 3>;
def : InstAlias<"tst $src1, $src2",
                        (ANDSXrs XZR, GPR64:$src1, GPR64:$src2, 0), 3>;

def : InstAlias<"tst $src1, $src2$sh",
               (ANDSWrs WZR, GPR32:$src1, GPR32:$src2, logical_shift32:$sh), 2>;
def : InstAlias<"tst $src1, $src2$sh",
               (ANDSXrs XZR, GPR64:$src1, GPR64:$src2, logical_shift64:$sh), 2>;


def : Pat<(not GPR32:$Wm), (ORNWrr WZR, GPR32:$Wm)>;
def : Pat<(not GPR64:$Xm), (ORNXrr XZR, GPR64:$Xm)>;

// Emit (and 0xFFFFFFFF) as a ORRWrr move which may be eliminated.
let AddedComplexity = 6 in
def : Pat<(i64 (and GPR64:$Rn, 0xffffffff)),
          (SUBREG_TO_REG (i64 0), (ORRWrr WZR, (EXTRACT_SUBREG GPR64:$Rn, sub_32)), sub_32)>;


//===----------------------------------------------------------------------===//
// One operand data processing instructions.
//===----------------------------------------------------------------------===//

defm CLS    : OneOperandData<0b000101, "cls">;
defm CLZ    : OneOperandData<0b000100, "clz", ctlz>;
defm RBIT   : OneOperandData<0b000000, "rbit", bitreverse>;

def  REV16Wr : OneWRegData<0b000001, "rev16",
                                     UnOpFrag<(rotr (bswap node:$LHS), (i64 16))>>;
def  REV16Xr : OneXRegData<0b000001, "rev16", null_frag>;

def : Pat<(cttz GPR32:$Rn),
          (CLZWr (RBITWr GPR32:$Rn))>;
def : Pat<(cttz GPR64:$Rn),
          (CLZXr (RBITXr GPR64:$Rn))>;
def : Pat<(ctlz (or (shl (xor (sra GPR32:$Rn, (i64 31)), GPR32:$Rn), (i64 1)),
                (i32 1))),
          (CLSWr GPR32:$Rn)>;
def : Pat<(ctlz (or (shl (xor (sra GPR64:$Rn, (i64 63)), GPR64:$Rn), (i64 1)),
                (i64 1))),
          (CLSXr GPR64:$Rn)>;
def : Pat<(int_aarch64_cls GPR32:$Rn), (CLSWr GPR32:$Rn)>;
def : Pat<(int_aarch64_cls64 GPR64:$Rm), (EXTRACT_SUBREG (CLSXr GPR64:$Rm), sub_32)>;

// Unlike the other one operand instructions, the instructions with the "rev"
// mnemonic do *not* just different in the size bit, but actually use different
// opcode bits for the different sizes.
def REVWr   : OneWRegData<0b000010, "rev", bswap>;
def REVXr   : OneXRegData<0b000011, "rev", bswap>;
def REV32Xr : OneXRegData<0b000010, "rev32",
                                    UnOpFrag<(rotr (bswap node:$LHS), (i64 32))>>;

def : InstAlias<"rev64 $Rd, $Rn", (REVXr GPR64:$Rd, GPR64:$Rn), 0>;

// The bswap commutes with the rotr so we want a pattern for both possible
// orders.
def : Pat<(bswap (rotr GPR32:$Rn, (i64 16))), (REV16Wr GPR32:$Rn)>;
def : Pat<(bswap (rotr GPR64:$Rn, (i64 32))), (REV32Xr GPR64:$Rn)>;

// Match (srl (bswap x), C) -> revC if the upper bswap bits are known zero.
def : Pat<(srl (bswap top16Zero:$Rn), (i64 16)), (REV16Wr GPR32:$Rn)>;
def : Pat<(srl (bswap top32Zero:$Rn), (i64 32)), (REV32Xr GPR64:$Rn)>;

def : Pat<(or (and (srl GPR64:$Rn, (i64 8)), (i64 0x00ff00ff00ff00ff)),
              (and (shl GPR64:$Rn, (i64 8)), (i64 0xff00ff00ff00ff00))),
          (REV16Xr GPR64:$Rn)>;

//===----------------------------------------------------------------------===//
// Bitfield immediate extraction instruction.
//===----------------------------------------------------------------------===//
let hasSideEffects = 0 in
defm EXTR : ExtractImm<"extr">;
def : InstAlias<"ror $dst, $src, $shift",
            (EXTRWrri GPR32:$dst, GPR32:$src, GPR32:$src, imm0_31:$shift)>;
def : InstAlias<"ror $dst, $src, $shift",
            (EXTRXrri GPR64:$dst, GPR64:$src, GPR64:$src, imm0_63:$shift)>;

def : Pat<(rotr GPR32:$Rn, (i64 imm0_31:$imm)),
          (EXTRWrri GPR32:$Rn, GPR32:$Rn, imm0_31:$imm)>;
def : Pat<(rotr GPR64:$Rn, (i64 imm0_63:$imm)),
          (EXTRXrri GPR64:$Rn, GPR64:$Rn, imm0_63:$imm)>;

//===----------------------------------------------------------------------===//
// Other bitfield immediate instructions.
//===----------------------------------------------------------------------===//
let hasSideEffects = 0 in {
defm BFM  : BitfieldImmWith2RegArgs<0b01, "bfm">;
defm SBFM : BitfieldImm<0b00, "sbfm">;
defm UBFM : BitfieldImm<0b10, "ubfm">;
}

def i32shift_a : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = (32 - N->getZExtValue()) & 0x1f;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

def i32shift_b : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 31 - N->getZExtValue();
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

// min(7, 31 - shift_amt)
def i32shift_sext_i8 : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 31 - N->getZExtValue();
  enc = enc > 7 ? 7 : enc;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

// min(15, 31 - shift_amt)
def i32shift_sext_i16 : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 31 - N->getZExtValue();
  enc = enc > 15 ? 15 : enc;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

def i64shift_a : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = (64 - N->getZExtValue()) & 0x3f;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

def i64shift_b : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 63 - N->getZExtValue();
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

// min(7, 63 - shift_amt)
def i64shift_sext_i8 : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 63 - N->getZExtValue();
  enc = enc > 7 ? 7 : enc;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

// min(15, 63 - shift_amt)
def i64shift_sext_i16 : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 63 - N->getZExtValue();
  enc = enc > 15 ? 15 : enc;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

// min(31, 63 - shift_amt)
def i64shift_sext_i32 : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 63 - N->getZExtValue();
  enc = enc > 31 ? 31 : enc;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

def : Pat<(shl GPR32:$Rn, (i64 imm0_31:$imm)),
          (UBFMWri GPR32:$Rn, (i64 (i32shift_a imm0_31:$imm)),
                              (i64 (i32shift_b imm0_31:$imm)))>;
def : Pat<(shl GPR64:$Rn, (i64 imm0_63:$imm)),
          (UBFMXri GPR64:$Rn, (i64 (i64shift_a imm0_63:$imm)),
                              (i64 (i64shift_b imm0_63:$imm)))>;

let AddedComplexity = 10 in {
def : Pat<(sra GPR32:$Rn, (i64 imm0_31:$imm)),
          (SBFMWri GPR32:$Rn, imm0_31:$imm, 31)>;
def : Pat<(sra GPR64:$Rn, (i64 imm0_63:$imm)),
          (SBFMXri GPR64:$Rn, imm0_63:$imm, 63)>;
}

def : InstAlias<"asr $dst, $src, $shift",
                (SBFMWri GPR32:$dst, GPR32:$src, imm0_31:$shift, 31)>;
def : InstAlias<"asr $dst, $src, $shift",
                (SBFMXri GPR64:$dst, GPR64:$src, imm0_63:$shift, 63)>;
def : InstAlias<"sxtb $dst, $src", (SBFMWri GPR32:$dst, GPR32:$src, 0, 7)>;
def : InstAlias<"sxtb $dst, $src", (SBFMXri GPR64:$dst, GPR64:$src, 0, 7)>;
def : InstAlias<"sxth $dst, $src", (SBFMWri GPR32:$dst, GPR32:$src, 0, 15)>;
def : InstAlias<"sxth $dst, $src", (SBFMXri GPR64:$dst, GPR64:$src, 0, 15)>;
def : InstAlias<"sxtw $dst, $src", (SBFMXri GPR64:$dst, GPR64:$src, 0, 31)>;

def : Pat<(srl GPR32:$Rn, (i64 imm0_31:$imm)),
          (UBFMWri GPR32:$Rn, imm0_31:$imm, 31)>;
def : Pat<(srl GPR64:$Rn, (i64 imm0_63:$imm)),
          (UBFMXri GPR64:$Rn, imm0_63:$imm, 63)>;

def : InstAlias<"lsr $dst, $src, $shift",
                (UBFMWri GPR32:$dst, GPR32:$src, imm0_31:$shift, 31)>;
def : InstAlias<"lsr $dst, $src, $shift",
                (UBFMXri GPR64:$dst, GPR64:$src, imm0_63:$shift, 63)>;
def : InstAlias<"uxtb $dst, $src", (UBFMWri GPR32:$dst, GPR32:$src, 0, 7)>;
def : InstAlias<"uxtb $dst, $src", (UBFMXri GPR64:$dst, GPR64:$src, 0, 7)>;
def : InstAlias<"uxth $dst, $src", (UBFMWri GPR32:$dst, GPR32:$src, 0, 15)>;
def : InstAlias<"uxth $dst, $src", (UBFMXri GPR64:$dst, GPR64:$src, 0, 15)>;
def : InstAlias<"uxtw $dst, $src", (UBFMXri GPR64:$dst, GPR64:$src, 0, 31)>;

//===----------------------------------------------------------------------===//
// Conditional comparison instructions.
//===----------------------------------------------------------------------===//
defm CCMN : CondComparison<0, "ccmn", AArch64ccmn>;
defm CCMP : CondComparison<1, "ccmp", AArch64ccmp>;

//===----------------------------------------------------------------------===//
// Conditional select instructions.
//===----------------------------------------------------------------------===//
defm CSEL  : CondSelect<0, 0b00, "csel">;

def inc : PatFrag<(ops node:$in), (add_and_or_is_add node:$in, 1)>;
defm CSINC : CondSelectOp<0, 0b01, "csinc", inc>;
defm CSINV : CondSelectOp<1, 0b00, "csinv", not>;
defm CSNEG : CondSelectOp<1, 0b01, "csneg", ineg>;

def : Pat<(AArch64csinv GPR32:$tval, GPR32:$fval, (i32 imm:$cc), NZCV),
          (CSINVWr GPR32:$tval, GPR32:$fval, (i32 imm:$cc))>;
def : Pat<(AArch64csinv GPR64:$tval, GPR64:$fval, (i32 imm:$cc), NZCV),
          (CSINVXr GPR64:$tval, GPR64:$fval, (i32 imm:$cc))>;
def : Pat<(AArch64csneg GPR32:$tval, GPR32:$fval, (i32 imm:$cc), NZCV),
          (CSNEGWr GPR32:$tval, GPR32:$fval, (i32 imm:$cc))>;
def : Pat<(AArch64csneg GPR64:$tval, GPR64:$fval, (i32 imm:$cc), NZCV),
          (CSNEGXr GPR64:$tval, GPR64:$fval, (i32 imm:$cc))>;
def : Pat<(AArch64csinc GPR32:$tval, GPR32:$fval, (i32 imm:$cc), NZCV),
          (CSINCWr GPR32:$tval, GPR32:$fval, (i32 imm:$cc))>;
def : Pat<(AArch64csinc GPR64:$tval, GPR64:$fval, (i32 imm:$cc), NZCV),
          (CSINCXr GPR64:$tval, GPR64:$fval, (i32 imm:$cc))>;

def : Pat<(AArch64csel (i32 0), (i32 1), (i32 imm:$cc), NZCV),
          (CSINCWr WZR, WZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel (i64 0), (i64 1), (i32 imm:$cc), NZCV),
          (CSINCXr XZR, XZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel GPR32:$tval, (i32 1), (i32 imm:$cc), NZCV),
          (CSINCWr GPR32:$tval, WZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel GPR64:$tval, (i64 1), (i32 imm:$cc), NZCV),
          (CSINCXr GPR64:$tval, XZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel (i32 1), GPR32:$fval, (i32 imm:$cc), NZCV),
          (CSINCWr GPR32:$fval, WZR, (i32 (inv_cond_XFORM imm:$cc)))>;
def : Pat<(AArch64csel (i64 1), GPR64:$fval, (i32 imm:$cc), NZCV),
          (CSINCXr GPR64:$fval, XZR, (i32 (inv_cond_XFORM imm:$cc)))>;
def : Pat<(AArch64csel (i32 0), (i32 -1), (i32 imm:$cc), NZCV),
          (CSINVWr WZR, WZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel (i64 0), (i64 -1), (i32 imm:$cc), NZCV),
          (CSINVXr XZR, XZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel GPR32:$tval, (i32 -1), (i32 imm:$cc), NZCV),
          (CSINVWr GPR32:$tval, WZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel GPR64:$tval, (i64 -1), (i32 imm:$cc), NZCV),
          (CSINVXr GPR64:$tval, XZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel (i32 -1), GPR32:$fval, (i32 imm:$cc), NZCV),
          (CSINVWr GPR32:$fval, WZR, (i32 (inv_cond_XFORM imm:$cc)))>;
def : Pat<(AArch64csel (i64 -1), GPR64:$fval, (i32 imm:$cc), NZCV),
          (CSINVXr GPR64:$fval, XZR, (i32 (inv_cond_XFORM imm:$cc)))>;

def : Pat<(add_and_or_is_add GPR32:$val, (AArch64csel (i32 0), (i32 1), (i32 imm:$cc), NZCV)),
          (CSINCWr GPR32:$val, GPR32:$val, (i32 imm:$cc))>;
def : Pat<(add_and_or_is_add GPR64:$val, (zext (AArch64csel (i32 0), (i32 1), (i32 imm:$cc), NZCV))),
          (CSINCXr GPR64:$val, GPR64:$val, (i32 imm:$cc))>;

def : Pat<(or (topbitsallzero32:$val), (AArch64csel (i32 0), (i32 1), (i32 imm:$cc), NZCV)),
          (CSINCWr GPR32:$val, WZR, imm:$cc)>;
def : Pat<(or (topbitsallzero64:$val), (AArch64csel (i64 0), (i64 1), (i32 imm:$cc), NZCV)),
          (CSINCXr GPR64:$val, XZR, imm:$cc)>;
def : Pat<(or (topbitsallzero64:$val), (zext (AArch64csel (i32 0), (i32 1), (i32 imm:$cc), NZCV))),
          (CSINCXr GPR64:$val, XZR, imm:$cc)>;

def : Pat<(and (topbitsallzero32:$val), (AArch64csel (i32 0), (i32 1), (i32 imm:$cc), NZCV)),
          (CSELWr WZR, GPR32:$val, imm:$cc)>;
def : Pat<(and (topbitsallzero64:$val), (AArch64csel (i64 0), (i64 1), (i32 imm:$cc), NZCV)),
          (CSELXr XZR, GPR64:$val, imm:$cc)>;
def : Pat<(and (topbitsallzero64:$val), (zext (AArch64csel (i32 0), (i32 1), (i32 imm:$cc), NZCV))),
          (CSELXr XZR, GPR64:$val, imm:$cc)>;

// The inverse of the condition code from the alias instruction is what is used
// in the aliased instruction. The parser all ready inverts the condition code
// for these aliases.
def : InstAlias<"cset $dst, $cc",
                (CSINCWr GPR32:$dst, WZR, WZR, inv_ccode:$cc)>;
def : InstAlias<"cset $dst, $cc",
                (CSINCXr GPR64:$dst, XZR, XZR, inv_ccode:$cc)>;

def : InstAlias<"csetm $dst, $cc",
                (CSINVWr GPR32:$dst, WZR, WZR, inv_ccode:$cc)>;
def : InstAlias<"csetm $dst, $cc",
                (CSINVXr GPR64:$dst, XZR, XZR, inv_ccode:$cc)>;

def : InstAlias<"cinc $dst, $src, $cc",
                (CSINCWr GPR32:$dst, GPR32:$src, GPR32:$src, inv_ccode:$cc)>;
def : InstAlias<"cinc $dst, $src, $cc",
                (CSINCXr GPR64:$dst, GPR64:$src, GPR64:$src, inv_ccode:$cc)>;

def : InstAlias<"cinv $dst, $src, $cc",
                (CSINVWr GPR32:$dst, GPR32:$src, GPR32:$src, inv_ccode:$cc)>;
def : InstAlias<"cinv $dst, $src, $cc",
                (CSINVXr GPR64:$dst, GPR64:$src, GPR64:$src, inv_ccode:$cc)>;

def : InstAlias<"cneg $dst, $src, $cc",
                (CSNEGWr GPR32:$dst, GPR32:$src, GPR32:$src, inv_ccode:$cc)>;
def : InstAlias<"cneg $dst, $src, $cc",
                (CSNEGXr GPR64:$dst, GPR64:$src, GPR64:$src, inv_ccode:$cc)>;

//===----------------------------------------------------------------------===//
// PC-relative instructions.
//===----------------------------------------------------------------------===//
let isReMaterializable = 1 in {
let hasSideEffects = 0, mayStore = 0, mayLoad = 0 in {
def ADR  : ADRI<0, "adr", adrlabel,
                [(set GPR64:$Xd, (AArch64adr tglobaladdr:$label))]>;
} // hasSideEffects = 0

def ADRP : ADRI<1, "adrp", adrplabel,
                [(set GPR64:$Xd, (AArch64adrp tglobaladdr:$label))]>;
} // isReMaterializable = 1

// page address of a constant pool entry, block address
def : Pat<(AArch64adr tconstpool:$cp), (ADR tconstpool:$cp)>;
def : Pat<(AArch64adr tblockaddress:$cp), (ADR tblockaddress:$cp)>;
def : Pat<(AArch64adr texternalsym:$sym), (ADR texternalsym:$sym)>;
def : Pat<(AArch64adr tjumptable:$sym), (ADR tjumptable:$sym)>;
def : Pat<(AArch64adrp tconstpool:$cp), (ADRP tconstpool:$cp)>;
def : Pat<(AArch64adrp tblockaddress:$cp), (ADRP tblockaddress:$cp)>;
def : Pat<(AArch64adrp texternalsym:$sym), (ADRP texternalsym:$sym)>;

//===----------------------------------------------------------------------===//
// Unconditional branch (register) instructions.
//===----------------------------------------------------------------------===//

let isReturn = 1, isTerminator = 1, isBarrier = 1 in {
def RET  : BranchReg<0b0010, "ret", []>;
def DRPS : SpecialReturn<0b0101, "drps">;
def ERET : SpecialReturn<0b0100, "eret">;
} // isReturn = 1, isTerminator = 1, isBarrier = 1

// Default to the LR register.
def : InstAlias<"ret", (RET LR)>;

let isCall = 1, Defs = [LR], Uses = [SP] in {
  def BLR : BranchReg<0b0001, "blr", []>;
  def BLRNoIP : Pseudo<(outs), (ins GPR64noip:$Rn), []>,
                Sched<[WriteBrReg]>,
                PseudoInstExpansion<(BLR GPR64:$Rn)>;
  def BLR_RVMARKER : Pseudo<(outs), (ins variable_ops), []>,
                     Sched<[WriteBrReg]>;
  def BLR_BTI : Pseudo<(outs), (ins variable_ops), []>,
                Sched<[WriteBrReg]>;
  let Uses = [X16, SP] in
  def BLR_X16 : Pseudo<(outs), (ins), [(AArch64call_arm64ec_to_x64 X16)]>,
                Sched<[WriteBrReg]>,
                PseudoInstExpansion<(BLR X16)>;
} // isCall

def : Pat<(AArch64call GPR64:$Rn),
          (BLR GPR64:$Rn)>,
      Requires<[NoSLSBLRMitigation]>;
def : Pat<(AArch64call GPR64noip:$Rn),
          (BLRNoIP GPR64noip:$Rn)>,
      Requires<[SLSBLRMitigation]>;

def : Pat<(AArch64call_rvmarker (i64 tglobaladdr:$rvfunc), GPR64:$Rn),
          (BLR_RVMARKER tglobaladdr:$rvfunc, GPR64:$Rn)>,
      Requires<[NoSLSBLRMitigation]>;

def : Pat<(AArch64call_bti GPR64:$Rn),
          (BLR_BTI GPR64:$Rn)>,
      Requires<[NoSLSBLRMitigation]>;
def : Pat<(AArch64call_bti GPR64noip:$Rn),
          (BLR_BTI GPR64noip:$Rn)>,
      Requires<[SLSBLRMitigation]>;

let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1 in {
def BR  : BranchReg<0b0000, "br", [(brind GPR64:$Rn)]>;
} // isBranch, isTerminator, isBarrier, isIndirectBranch

// Create a separate pseudo-instruction for codegen to use so that we don't
// flag lr as used in every function. It'll be restored before the RET by the
// epilogue if it's legitimately used.
def RET_ReallyLR : Pseudo<(outs), (ins), [(AArch64retglue)]>,
                   Sched<[WriteBrReg]> {
  let isTerminator = 1;
  let isBarrier = 1;
  let isReturn = 1;
}

// This is a directive-like pseudo-instruction. The purpose is to insert an
// R_AARCH64_TLSDESC_CALL relocation at the offset of the following instruction
// (which in the usual case is a BLR).
let hasSideEffects = 1 in
def TLSDESCCALL : Pseudo<(outs), (ins i64imm:$sym), []>, Sched<[]> {
  let AsmString = ".tlsdesccall $sym";
}

// Pseudo instruction to tell the streamer to emit a 'B' character into the
// augmentation string.
def EMITBKEY : Pseudo<(outs), (ins), []>, Sched<[]> {}

// Pseudo instruction to tell the streamer to emit a 'G' character into the
// augmentation string.
def EMITMTETAGGED : Pseudo<(outs), (ins), []>, Sched<[]> {}

// FIXME: maybe the scratch register used shouldn't be fixed to X1?
// FIXME: can "hasSideEffects be dropped?
// This gets lowered to an instruction sequence which takes 16 bytes
let isCall = 1, Defs = [NZCV, LR, X0, X1], hasSideEffects = 1, Size = 16,
    isCodeGenOnly = 1 in
def TLSDESC_CALLSEQ
    : Pseudo<(outs), (ins i64imm:$sym),
             [(AArch64tlsdesc_callseq tglobaltlsaddr:$sym)]>,
      Sched<[WriteI, WriteLD, WriteI, WriteBrReg]>;
def : Pat<(AArch64tlsdesc_callseq texternalsym:$sym),
          (TLSDESC_CALLSEQ texternalsym:$sym)>;

//===----------------------------------------------------------------------===//
// Conditional branch (immediate) instruction.
//===----------------------------------------------------------------------===//
def Bcc : BranchCond<0, "b">;

// Armv8.8-A variant form which hints to the branch predictor that
// this branch is very likely to go the same way nearly all the time
// (even though it is not known at compile time _which_ way that is).
def BCcc : BranchCond<1, "bc">, Requires<[HasHBC]>;

//===----------------------------------------------------------------------===//
// Compare-and-branch instructions.
//===----------------------------------------------------------------------===//
defm CBZ  : CmpBranch<0, "cbz", AArch64cbz>;
defm CBNZ : CmpBranch<1, "cbnz", AArch64cbnz>;

//===----------------------------------------------------------------------===//
// Test-bit-and-branch instructions.
//===----------------------------------------------------------------------===//
defm TBZ  : TestBranch<0, "tbz", AArch64tbz>;
defm TBNZ : TestBranch<1, "tbnz", AArch64tbnz>;

//===----------------------------------------------------------------------===//
// Unconditional branch (immediate) instructions.
//===----------------------------------------------------------------------===//
let isBranch = 1, isTerminator = 1, isBarrier = 1 in {
def B  : BranchImm<0, "b", [(br bb:$addr)]>;
} // isBranch, isTerminator, isBarrier

let isCall = 1, Defs = [LR], Uses = [SP] in {
def BL : CallImm<1, "bl", [(AArch64call tglobaladdr:$addr)]>;
} // isCall
def : Pat<(AArch64call texternalsym:$func), (BL texternalsym:$func)>;

//===----------------------------------------------------------------------===//
// Exception generation instructions.
//===----------------------------------------------------------------------===//
let isTrap = 1 in {
def BRK   : ExceptionGeneration<0b001, 0b00, "brk",
                                [(int_aarch64_break timm32_0_65535:$imm)]>;
}
def DCPS1 : ExceptionGeneration<0b101, 0b01, "dcps1">;
def DCPS2 : ExceptionGeneration<0b101, 0b10, "dcps2">;
def DCPS3 : ExceptionGeneration<0b101, 0b11, "dcps3">, Requires<[HasEL3]>;
def HLT   : ExceptionGeneration<0b010, 0b00, "hlt",
                                [(int_aarch64_hlt timm32_0_65535:$imm)]>;
def HVC   : ExceptionGeneration<0b000, 0b10, "hvc">;
def SMC   : ExceptionGeneration<0b000, 0b11, "smc">, Requires<[HasEL3]>;
def SVC   : ExceptionGeneration<0b000, 0b01, "svc">;

// DCPSn defaults to an immediate operand of zero if unspecified.
def : InstAlias<"dcps1", (DCPS1 0)>;
def : InstAlias<"dcps2", (DCPS2 0)>;
def : InstAlias<"dcps3", (DCPS3 0)>, Requires<[HasEL3]>;

def UDF : UDFType<0, "udf">;

//===----------------------------------------------------------------------===//
// Load instructions.
//===----------------------------------------------------------------------===//

// Pair (indexed, offset)
defm LDPW : LoadPairOffset<0b00, 0, GPR32z, simm7s4, "ldp">;
defm LDPX : LoadPairOffset<0b10, 0, GPR64z, simm7s8, "ldp">;
let Predicates = [HasFPARMv8] in {
defm LDPS : LoadPairOffset<0b00, 1, FPR32Op, simm7s4, "ldp">;
defm LDPD : LoadPairOffset<0b01, 1, FPR64Op, simm7s8, "ldp">;
defm LDPQ : LoadPairOffset<0b10, 1, FPR128Op, simm7s16, "ldp">;
}

defm LDPSW : LoadPairOffset<0b01, 0, GPR64z, simm7s4, "ldpsw">;

// Pair (pre-indexed)
def LDPWpre : LoadPairPreIdx<0b00, 0, GPR32z, simm7s4, "ldp">;
def LDPXpre : LoadPairPreIdx<0b10, 0, GPR64z, simm7s8, "ldp">;
let Predicates = [HasFPARMv8] in {
def LDPSpre : LoadPairPreIdx<0b00, 1, FPR32Op, simm7s4, "ldp">;
def LDPDpre : LoadPairPreIdx<0b01, 1, FPR64Op, simm7s8, "ldp">;
def LDPQpre : LoadPairPreIdx<0b10, 1, FPR128Op, simm7s16, "ldp">;
}

def LDPSWpre : LoadPairPreIdx<0b01, 0, GPR64z, simm7s4, "ldpsw">;

// Pair (post-indexed)
def LDPWpost : LoadPairPostIdx<0b00, 0, GPR32z, simm7s4, "ldp">;
def LDPXpost : LoadPairPostIdx<0b10, 0, GPR64z, simm7s8, "ldp">;
let Predicates = [HasFPARMv8] in {
def LDPSpost : LoadPairPostIdx<0b00, 1, FPR32Op, simm7s4, "ldp">;
def LDPDpost : LoadPairPostIdx<0b01, 1, FPR64Op, simm7s8, "ldp">;
def LDPQpost : LoadPairPostIdx<0b10, 1, FPR128Op, simm7s16, "ldp">;
}

def LDPSWpost : LoadPairPostIdx<0b01, 0, GPR64z, simm7s4, "ldpsw">;


// Pair (no allocate)
defm LDNPW : LoadPairNoAlloc<0b00, 0, GPR32z, simm7s4, "ldnp">;
defm LDNPX : LoadPairNoAlloc<0b10, 0, GPR64z, simm7s8, "ldnp">;
let Predicates = [HasFPARMv8] in {
defm LDNPS : LoadPairNoAlloc<0b00, 1, FPR32Op, simm7s4, "ldnp">;
defm LDNPD : LoadPairNoAlloc<0b01, 1, FPR64Op, simm7s8, "ldnp">;
defm LDNPQ : LoadPairNoAlloc<0b10, 1, FPR128Op, simm7s16, "ldnp">;
}

def : Pat<(AArch64ldp (am_indexed7s64 GPR64sp:$Rn, simm7s8:$offset)),
          (LDPXi GPR64sp:$Rn, simm7s8:$offset)>;

def : Pat<(AArch64ldnp (am_indexed7s128 GPR64sp:$Rn, simm7s16:$offset)),
          (LDNPQi GPR64sp:$Rn, simm7s16:$offset)>;
//---
// (register offset)
//---

// Integer
defm LDRBB : Load8RO<0b00,  0, 0b01, GPR32, "ldrb", i32, zextloadi8>;
defm LDRHH : Load16RO<0b01, 0, 0b01, GPR32, "ldrh", i32, zextloadi16>;
defm LDRW  : Load32RO<0b10, 0, 0b01, GPR32, "ldr", i32, load>;
defm LDRX  : Load64RO<0b11, 0, 0b01, GPR64, "ldr", i64, load>;

// Floating-point
let Predicates = [HasFPARMv8] in {
defm LDRB : Load8RO<0b00,   1, 0b01, FPR8Op,   "ldr", i8, load>;
defm LDRH : Load16RO<0b01,  1, 0b01, FPR16Op,  "ldr", f16, load>;
defm LDRS : Load32RO<0b10,  1, 0b01, FPR32Op,  "ldr", f32, load>;
defm LDRD : Load64RO<0b11,  1, 0b01, FPR64Op,  "ldr", f64, load>;
defm LDRQ : Load128RO<0b00, 1, 0b11, FPR128Op, "ldr", f128, load>;
}

// Load sign-extended half-word
defm LDRSHW : Load16RO<0b01, 0, 0b11, GPR32, "ldrsh", i32, sextloadi16>;
defm LDRSHX : Load16RO<0b01, 0, 0b10, GPR64, "ldrsh", i64, sextloadi16>;

// Load sign-extended byte
defm LDRSBW : Load8RO<0b00, 0, 0b11, GPR32, "ldrsb", i32, sextloadi8>;
defm LDRSBX : Load8RO<0b00, 0, 0b10, GPR64, "ldrsb", i64, sextloadi8>;

// Load sign-extended word
defm LDRSW  : Load32RO<0b10, 0, 0b10, GPR64, "ldrsw", i64, sextloadi32>;

// Pre-fetch.
defm PRFM : PrefetchRO<0b11, 0, 0b10, "prfm">;

// For regular load, we do not have any alignment requirement.
// Thus, it is safe to directly map the vector loads with interesting
// addressing modes.
// FIXME: We could do the same for bitconvert to floating point vectors.
multiclass ScalToVecROLoadPat<ROAddrMode ro, SDPatternOperator loadop,
                              ValueType ScalTy, ValueType VecTy,
                              Instruction LOADW, Instruction LOADX,
                              SubRegIndex sub> {
  def : Pat<(VecTy (scalar_to_vector (ScalTy
              (loadop (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$offset))))),
            (INSERT_SUBREG (VecTy (IMPLICIT_DEF)),
                           (LOADW GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$offset),
                           sub)>;

  def : Pat<(VecTy (scalar_to_vector (ScalTy
              (loadop (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$offset))))),
            (INSERT_SUBREG (VecTy (IMPLICIT_DEF)),
                           (LOADX GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$offset),
                           sub)>;
}

let AddedComplexity = 10 in {
defm : ScalToVecROLoadPat<ro8,  extloadi8,  i32, v8i8,  LDRBroW, LDRBroX, bsub>;
defm : ScalToVecROLoadPat<ro8,  extloadi8,  i32, v16i8, LDRBroW, LDRBroX, bsub>;

defm : ScalToVecROLoadPat<ro16, extloadi16, i32, v4i16, LDRHroW, LDRHroX, hsub>;
defm : ScalToVecROLoadPat<ro16, extloadi16, i32, v8i16, LDRHroW, LDRHroX, hsub>;

defm : ScalToVecROLoadPat<ro16, load,       i32, v4f16, LDRHroW, LDRHroX, hsub>;
defm : ScalToVecROLoadPat<ro16, load,       i32, v8f16, LDRHroW, LDRHroX, hsub>;

defm : ScalToVecROLoadPat<ro32, load,       i32, v2i32, LDRSroW, LDRSroX, ssub>;
defm : ScalToVecROLoadPat<ro32, load,       i32, v4i32, LDRSroW, LDRSroX, ssub>;

defm : ScalToVecROLoadPat<ro32, load,       f32, v2f32, LDRSroW, LDRSroX, ssub>;
defm : ScalToVecROLoadPat<ro32, load,       f32, v4f32, LDRSroW, LDRSroX, ssub>;

defm : ScalToVecROLoadPat<ro64, load,       i64, v2i64, LDRDroW, LDRDroX, dsub>;

defm : ScalToVecROLoadPat<ro64, load,       f64, v2f64, LDRDroW, LDRDroX, dsub>;


def : Pat <(v1i64 (scalar_to_vector (i64
                      (load (ro_Windexed64 GPR64sp:$Rn, GPR32:$Rm,
                                           ro_Wextend64:$extend))))),
           (LDRDroW GPR64sp:$Rn, GPR32:$Rm, ro_Wextend64:$extend)>;

def : Pat <(v1i64 (scalar_to_vector (i64
                      (load (ro_Xindexed64 GPR64sp:$Rn, GPR64:$Rm,
                                           ro_Xextend64:$extend))))),
           (LDRDroX GPR64sp:$Rn, GPR64:$Rm, ro_Xextend64:$extend)>;
}

// Match all load 64 bits width whose type is compatible with FPR64
multiclass VecROLoadPat<ROAddrMode ro, ValueType VecTy,
                        Instruction LOADW, Instruction LOADX> {

  def : Pat<(VecTy (load (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend))),
            (LOADW GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)>;

  def : Pat<(VecTy (load (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend))),
            (LOADX GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)>;
}

let AddedComplexity = 10 in {
let Predicates = [IsLE] in {
  // We must do vector loads with LD1 in big-endian.
  defm : VecROLoadPat<ro64, v2i32, LDRDroW, LDRDroX>;
  defm : VecROLoadPat<ro64, v2f32, LDRDroW, LDRDroX>;
  defm : VecROLoadPat<ro64, v8i8,  LDRDroW, LDRDroX>;
  defm : VecROLoadPat<ro64, v4i16, LDRDroW, LDRDroX>;
  defm : VecROLoadPat<ro64, v4f16, LDRDroW, LDRDroX>;
  defm : VecROLoadPat<ro64, v4bf16, LDRDroW, LDRDroX>;
}

defm : VecROLoadPat<ro64, v1i64,  LDRDroW, LDRDroX>;
defm : VecROLoadPat<ro64, v1f64,  LDRDroW, LDRDroX>;

// Match all load 128 bits width whose type is compatible with FPR128
let Predicates = [IsLE] in {
  // We must do vector loads with LD1 in big-endian.
  defm : VecROLoadPat<ro128, v2i64,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v2f64,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v4i32,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v4f32,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v8i16,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v8f16,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v8bf16,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v16i8,  LDRQroW, LDRQroX>;
}
} // AddedComplexity = 10

// zextload -> i64
multiclass ExtLoadTo64ROPat<ROAddrMode ro, SDPatternOperator loadop,
                            Instruction INSTW, Instruction INSTX> {
  def : Pat<(i64 (loadop (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend))),
            (SUBREG_TO_REG (i64 0),
                           (INSTW GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend),
                           sub_32)>;

  def : Pat<(i64 (loadop (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend))),
            (SUBREG_TO_REG (i64 0),
                           (INSTX GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend),
                           sub_32)>;
}

let AddedComplexity = 10 in {
  defm : ExtLoadTo64ROPat<ro8,  zextloadi8,  LDRBBroW, LDRBBroX>;
  defm : ExtLoadTo64ROPat<ro16, zextloadi16, LDRHHroW, LDRHHroX>;
  defm : ExtLoadTo64ROPat<ro32, zextloadi32, LDRWroW,  LDRWroX>;

  // zextloadi1 -> zextloadi8
  defm : ExtLoadTo64ROPat<ro8,  zextloadi1,  LDRBBroW, LDRBBroX>;

  // extload -> zextload
  defm : ExtLoadTo64ROPat<ro8,  extloadi8,   LDRBBroW, LDRBBroX>;
  defm : ExtLoadTo64ROPat<ro16, extloadi16,  LDRHHroW, LDRHHroX>;
  defm : ExtLoadTo64ROPat<ro32, extloadi32,  LDRWroW,  LDRWroX>;

  // extloadi1 -> zextloadi8
  defm : ExtLoadTo64ROPat<ro8,  extloadi1,   LDRBBroW, LDRBBroX>;
}


// zextload -> i64
multiclass ExtLoadTo32ROPat<ROAddrMode ro, SDPatternOperator loadop,
                            Instruction INSTW, Instruction INSTX> {
  def : Pat<(i32 (loadop (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend))),
            (INSTW GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)>;

  def : Pat<(i32 (loadop (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend))),
            (INSTX GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)>;

}

let AddedComplexity = 10 in {
  // extload -> zextload
  defm : ExtLoadTo32ROPat<ro8,  extloadi8,   LDRBBroW, LDRBBroX>;
  defm : ExtLoadTo32ROPat<ro16, extloadi16,  LDRHHroW, LDRHHroX>;
  defm : ExtLoadTo32ROPat<ro32, extloadi32,  LDRWroW,  LDRWroX>;

  // zextloadi1 -> zextloadi8
  defm : ExtLoadTo32ROPat<ro8, zextloadi1, LDRBBroW, LDRBBroX>;
}

//---
// (unsigned immediate)
//---
defm LDRX : LoadUI<0b11, 0, 0b01, GPR64z, uimm12s8, "ldr",
                   [(set GPR64z:$Rt,
                         (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)))]>;
defm LDRW : LoadUI<0b10, 0, 0b01, GPR32z, uimm12s4, "ldr",
                   [(set GPR32z:$Rt,
                         (load (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset)))]>;
let Predicates = [HasFPARMv8] in {
defm LDRB : LoadUI<0b00, 1, 0b01, FPR8Op, uimm12s1, "ldr",
                   [(set FPR8Op:$Rt,
                         (load (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset)))]>;
defm LDRH : LoadUI<0b01, 1, 0b01, FPR16Op, uimm12s2, "ldr",
                   [(set (f16 FPR16Op:$Rt),
                         (load (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset)))]>;
defm LDRS : LoadUI<0b10, 1, 0b01, FPR32Op, uimm12s4, "ldr",
                   [(set (f32 FPR32Op:$Rt),
                         (load (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset)))]>;
defm LDRD : LoadUI<0b11, 1, 0b01, FPR64Op, uimm12s8, "ldr",
                   [(set (f64 FPR64Op:$Rt),
                         (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)))]>;
defm LDRQ : LoadUI<0b00, 1, 0b11, FPR128Op, uimm12s16, "ldr",
                 [(set (f128 FPR128Op:$Rt),
                       (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)))]>;
}

// bf16 load pattern
def : Pat <(bf16 (load (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))),
           (LDRHui GPR64sp:$Rn, uimm12s2:$offset)>;

// For regular load, we do not have any alignment requirement.
// Thus, it is safe to directly map the vector loads with interesting
// addressing modes.
// FIXME: We could do the same for bitconvert to floating point vectors.
def : Pat <(v8i8 (scalar_to_vector (i32
               (extloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))))),
           (INSERT_SUBREG (v8i8 (IMPLICIT_DEF)),
                          (LDRBui GPR64sp:$Rn, uimm12s1:$offset), bsub)>;
def : Pat <(v16i8 (scalar_to_vector (i32
               (extloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))))),
           (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
                          (LDRBui GPR64sp:$Rn, uimm12s1:$offset), bsub)>;
def : Pat <(v4i16 (scalar_to_vector (i32
               (extloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))))),
           (INSERT_SUBREG (v4i16 (IMPLICIT_DEF)),
                          (LDRHui GPR64sp:$Rn, uimm12s2:$offset), hsub)>;
def : Pat <(v8i16 (scalar_to_vector (i32
               (extloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))))),
           (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)),
                          (LDRHui GPR64sp:$Rn, uimm12s2:$offset), hsub)>;
def : Pat <(v2i32 (scalar_to_vector (i32
               (load (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))))),
           (INSERT_SUBREG (v2i32 (IMPLICIT_DEF)),
                          (LDRSui GPR64sp:$Rn, uimm12s4:$offset), ssub)>;
def : Pat <(v4i32 (scalar_to_vector (i32
               (load (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))))),
           (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)),
                          (LDRSui GPR64sp:$Rn, uimm12s4:$offset), ssub)>;
def : Pat <(v1i64 (scalar_to_vector (i64
               (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))))),
           (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
def : Pat <(v2i64 (scalar_to_vector (i64
               (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))))),
           (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)),
                          (LDRDui GPR64sp:$Rn, uimm12s8:$offset), dsub)>;

// Match all load 64 bits width whose type is compatible with FPR64
let Predicates = [IsLE] in {
  // We must use LD1 to perform vector loads in big-endian.
  def : Pat<(v2f32 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
            (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(v8i8 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
            (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(v4i16 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
            (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(v2i32 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
            (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(v4f16 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
            (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(v4bf16 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
            (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
}
def : Pat<(v1f64 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
          (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
def : Pat<(v1i64 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
          (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;

// Match all load 128 bits width whose type is compatible with FPR128
let Predicates = [IsLE] in {
  // We must use LD1 to perform vector loads in big-endian.
  def : Pat<(v4f32 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v2f64 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v16i8 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v8i16 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v4i32 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v2i64 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v8f16 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v8bf16 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
}
def : Pat<(f128  (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
          (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;

defm LDRHH : LoadUI<0b01, 0, 0b01, GPR32, uimm12s2, "ldrh",
                    [(set GPR32:$Rt,
                          (zextloadi16 (am_indexed16 GPR64sp:$Rn,
                                                     uimm12s2:$offset)))]>;
defm LDRBB : LoadUI<0b00, 0, 0b01, GPR32, uimm12s1, "ldrb",
                    [(set GPR32:$Rt,
                          (zextloadi8 (am_indexed8 GPR64sp:$Rn,
                                                   uimm12s1:$offset)))]>;
// zextload -> i64
def : Pat<(i64 (zextloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRBBui GPR64sp:$Rn, uimm12s1:$offset), sub_32)>;
def : Pat<(i64 (zextloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRHHui GPR64sp:$Rn, uimm12s2:$offset), sub_32)>;

// zextloadi1 -> zextloadi8
def : Pat<(i32 (zextloadi1 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
          (LDRBBui GPR64sp:$Rn, uimm12s1:$offset)>;
def : Pat<(i64 (zextloadi1 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRBBui GPR64sp:$Rn, uimm12s1:$offset), sub_32)>;

// extload -> zextload
def : Pat<(i32 (extloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))),
          (LDRHHui GPR64sp:$Rn, uimm12s2:$offset)>;
def : Pat<(i32 (extloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
          (LDRBBui GPR64sp:$Rn, uimm12s1:$offset)>;
def : Pat<(i32 (extloadi1 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
          (LDRBBui GPR64sp:$Rn, uimm12s1:$offset)>;
def : Pat<(i64 (extloadi32 (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRWui GPR64sp:$Rn, uimm12s4:$offset), sub_32)>;
def : Pat<(i64 (extloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRHHui GPR64sp:$Rn, uimm12s2:$offset), sub_32)>;
def : Pat<(i64 (extloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRBBui GPR64sp:$Rn, uimm12s1:$offset), sub_32)>;
def : Pat<(i64 (extloadi1 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRBBui GPR64sp:$Rn, uimm12s1:$offset), sub_32)>;

// load sign-extended half-word
defm LDRSHW : LoadUI<0b01, 0, 0b11, GPR32, uimm12s2, "ldrsh",
                     [(set GPR32:$Rt,
                           (sextloadi16 (am_indexed16 GPR64sp:$Rn,
                                                      uimm12s2:$offset)))]>;
defm LDRSHX : LoadUI<0b01, 0, 0b10, GPR64, uimm12s2, "ldrsh",
                     [(set GPR64:$Rt,
                           (sextloadi16 (am_indexed16 GPR64sp:$Rn,
                                                      uimm12s2:$offset)))]>;

// load sign-extended byte
defm LDRSBW : LoadUI<0b00, 0, 0b11, GPR32, uimm12s1, "ldrsb",
                     [(set GPR32:$Rt,
                           (sextloadi8 (am_indexed8 GPR64sp:$Rn,
                                                    uimm12s1:$offset)))]>;
defm LDRSBX : LoadUI<0b00, 0, 0b10, GPR64, uimm12s1, "ldrsb",
                     [(set GPR64:$Rt,
                           (sextloadi8 (am_indexed8 GPR64sp:$Rn,
                                                    uimm12s1:$offset)))]>;

// load sign-extended word
defm LDRSW  : LoadUI<0b10, 0, 0b10, GPR64, uimm12s4, "ldrsw",
                     [(set GPR64:$Rt,
                           (sextloadi32 (am_indexed32 GPR64sp:$Rn,
                                                      uimm12s4:$offset)))]>;

// load zero-extended word
def : Pat<(i64 (zextloadi32 (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))),
      (SUBREG_TO_REG (i64 0), (LDRWui GPR64sp:$Rn, uimm12s4:$offset), sub_32)>;

// Pre-fetch.
def PRFMui : PrefetchUI<0b11, 0, 0b10, "prfm",
                        [(AArch64Prefetch timm:$Rt,
                                        (am_indexed64 GPR64sp:$Rn,
                                                      uimm12s8:$offset))]>;

def : InstAlias<"prfm $Rt, [$Rn]", (PRFMui prfop:$Rt, GPR64sp:$Rn, 0)>;

//---
// (literal)

def alignedglobal : PatLeaf<(iPTR iPTR:$label), [{
  if (auto *G = dyn_cast<GlobalAddressSDNode>(N)) {
    const DataLayout &DL = MF->getDataLayout();
    Align Align = G->getGlobal()->getPointerAlignment(DL);
    return Align >= 4 && G->getOffset() % 4 == 0;
  }
  if (auto *C = dyn_cast<ConstantPoolSDNode>(N))
    return C->getAlign() >= 4 && C->getOffset() % 4 == 0;
  return false;
}]>;

def LDRWl : LoadLiteral<0b00, 0, GPR32z, "ldr",
  [(set GPR32z:$Rt, (load (AArch64adr alignedglobal:$label)))]>;
def LDRXl : LoadLiteral<0b01, 0, GPR64z, "ldr",
  [(set GPR64z:$Rt, (load (AArch64adr alignedglobal:$label)))]>;
let Predicates = [HasFPARMv8] in {
def LDRSl : LoadLiteral<0b00, 1, FPR32Op, "ldr",
  [(set (f32 FPR32Op:$Rt), (load (AArch64adr alignedglobal:$label)))]>;
def LDRDl : LoadLiteral<0b01, 1, FPR64Op, "ldr",
  [(set (f64 FPR64Op:$Rt), (load (AArch64adr alignedglobal:$label)))]>;
def LDRQl : LoadLiteral<0b10, 1, FPR128Op, "ldr",
  [(set (f128 FPR128Op:$Rt), (load (AArch64adr alignedglobal:$label)))]>;
}

// load sign-extended word
def LDRSWl : LoadLiteral<0b10, 0, GPR64z, "ldrsw",
  [(set GPR64z:$Rt, (sextloadi32 (AArch64adr alignedglobal:$label)))]>;

let AddedComplexity = 20 in {
def : Pat<(i64 (zextloadi32 (AArch64adr alignedglobal:$label))),
        (SUBREG_TO_REG (i64 0), (LDRWl $label), sub_32)>;
}

// prefetch
def PRFMl : PrefetchLiteral<0b11, 0, "prfm", []>;
//                   [(AArch64Prefetch imm:$Rt, tglobaladdr:$label)]>;

//---
// (unscaled immediate)
defm LDURX : LoadUnscaled<0b11, 0, 0b01, GPR64z, "ldur",
                    [(set GPR64z:$Rt,
                          (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURW : LoadUnscaled<0b10, 0, 0b01, GPR32z, "ldur",
                    [(set GPR32z:$Rt,
                          (load (am_unscaled32 GPR64sp:$Rn, simm9:$offset)))]>;
let Predicates = [HasFPARMv8] in {
defm LDURB : LoadUnscaled<0b00, 1, 0b01, FPR8Op, "ldur",
                    [(set FPR8Op:$Rt,
                          (load (am_unscaled8 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURH : LoadUnscaled<0b01, 1, 0b01, FPR16Op, "ldur",
                    [(set (f16 FPR16Op:$Rt),
                          (load (am_unscaled16 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURS : LoadUnscaled<0b10, 1, 0b01, FPR32Op, "ldur",
                    [(set (f32 FPR32Op:$Rt),
                          (load (am_unscaled32 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURD : LoadUnscaled<0b11, 1, 0b01, FPR64Op, "ldur",
                    [(set (f64 FPR64Op:$Rt),
                          (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURQ : LoadUnscaled<0b00, 1, 0b11, FPR128Op, "ldur",
                    [(set (f128 FPR128Op:$Rt),
                          (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset)))]>;
}

defm LDURHH
    : LoadUnscaled<0b01, 0, 0b01, GPR32, "ldurh",
             [(set GPR32:$Rt,
                    (zextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURBB
    : LoadUnscaled<0b00, 0, 0b01, GPR32, "ldurb",
             [(set GPR32:$Rt,
                    (zextloadi8 (am_unscaled16 GPR64sp:$Rn, simm9:$offset)))]>;

// bf16 load pattern
def : Pat <(bf16 (load (am_unscaled16 GPR64sp:$Rn, simm9:$offset))),
           (LDURHi GPR64sp:$Rn, simm9:$offset)>;

// Match all load 64 bits width whose type is compatible with FPR64
let Predicates = [IsLE] in {
  def : Pat<(v2f32 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
            (LDURDi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v2i32 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
            (LDURDi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v4i16 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
            (LDURDi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v8i8 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
            (LDURDi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v4f16 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
            (LDURDi GPR64sp:$Rn, simm9:$offset)>;
}
def : Pat<(v1f64 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
          (LDURDi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(v1i64 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
          (LDURDi GPR64sp:$Rn, simm9:$offset)>;

// Match all load 128 bits width whose type is compatible with FPR128
let Predicates = [IsLE] in {
  def : Pat<(v2f64 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v2i64 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v4f32 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v4i32 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v8i16 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v16i8 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v8f16 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
}

//  anyext -> zext
def : Pat<(i32 (extloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))),
          (LDURHHi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(i32 (extloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
          (LDURBBi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(i32 (extloadi1 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
          (LDURBBi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(i64 (extloadi32 (am_unscaled32 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURWi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (extloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURHHi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (extloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURBBi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (extloadi1 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURBBi GPR64sp:$Rn, simm9:$offset), sub_32)>;
// unscaled zext
def : Pat<(i32 (zextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))),
          (LDURHHi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(i32 (zextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
          (LDURBBi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(i32 (zextloadi1 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
          (LDURBBi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(i64 (zextloadi32 (am_unscaled32 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURWi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (zextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURHHi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (zextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURBBi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (zextloadi1 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURBBi GPR64sp:$Rn, simm9:$offset), sub_32)>;


//---
// LDR mnemonics fall back to LDUR for negative or unaligned offsets.

// Define new assembler match classes as we want to only match these when
// the don't otherwise match the scaled addressing mode for LDR/STR. Don't
// associate a DiagnosticType either, as we want the diagnostic for the
// canonical form (the scaled operand) to take precedence.
class SImm9OffsetOperand<int Width> : AsmOperandClass {
  let Name = "SImm9OffsetFB" # Width;
  let PredicateMethod = "isSImm9OffsetFB<" # Width # ">";
  let RenderMethod = "addImmOperands";
}

def SImm9OffsetFB8Operand : SImm9OffsetOperand<8>;
def SImm9OffsetFB16Operand : SImm9OffsetOperand<16>;
def SImm9OffsetFB32Operand : SImm9OffsetOperand<32>;
def SImm9OffsetFB64Operand : SImm9OffsetOperand<64>;
def SImm9OffsetFB128Operand : SImm9OffsetOperand<128>;

def simm9_offset_fb8 : Operand<i64> {
  let ParserMatchClass = SImm9OffsetFB8Operand;
}
def simm9_offset_fb16 : Operand<i64> {
  let ParserMatchClass = SImm9OffsetFB16Operand;
}
def simm9_offset_fb32 : Operand<i64> {
  let ParserMatchClass = SImm9OffsetFB32Operand;
}
def simm9_offset_fb64 : Operand<i64> {
  let ParserMatchClass = SImm9OffsetFB64Operand;
}
def simm9_offset_fb128 : Operand<i64> {
  let ParserMatchClass = SImm9OffsetFB128Operand;
}

def : InstAlias<"ldr $Rt, [$Rn, $offset]",
                (LDURXi GPR64:$Rt, GPR64sp:$Rn, simm9_offset_fb64:$offset), 0>;
def : InstAlias<"ldr $Rt, [$Rn, $offset]",
                (LDURWi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb32:$offset), 0>;
let Predicates = [HasFPARMv8] in {
def : InstAlias<"ldr $Rt, [$Rn, $offset]",
                (LDURBi FPR8Op:$Rt, GPR64sp:$Rn, simm9_offset_fb8:$offset), 0>;
def : InstAlias<"ldr $Rt, [$Rn, $offset]",
                (LDURHi FPR16Op:$Rt, GPR64sp:$Rn, simm9_offset_fb16:$offset), 0>;
def : InstAlias<"ldr $Rt, [$Rn, $offset]",
                (LDURSi FPR32Op:$Rt, GPR64sp:$Rn, simm9_offset_fb32:$offset), 0>;
def : InstAlias<"ldr $Rt, [$Rn, $offset]",
                (LDURDi FPR64Op:$Rt, GPR64sp:$Rn, simm9_offset_fb64:$offset), 0>;
def : InstAlias<"ldr $Rt, [$Rn, $offset]",
               (LDURQi FPR128Op:$Rt, GPR64sp:$Rn, simm9_offset_fb128:$offset), 0>;
}

// zextload -> i64
def : Pat<(i64 (zextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
  (SUBREG_TO_REG (i64 0), (LDURBBi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (zextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))),
  (SUBREG_TO_REG (i64 0), (LDURHHi GPR64sp:$Rn, simm9:$offset), sub_32)>;

// load sign-extended half-word
defm LDURSHW
    : LoadUnscaled<0b01, 0, 0b11, GPR32, "ldursh",
               [(set GPR32:$Rt,
                    (sextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURSHX
    : LoadUnscaled<0b01, 0, 0b10, GPR64, "ldursh",
              [(set GPR64:$Rt,
                    (sextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset)))]>;

// load sign-extended byte
defm LDURSBW
    : LoadUnscaled<0b00, 0, 0b11, GPR32, "ldursb",
                [(set GPR32:$Rt,
                      (sextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURSBX
    : LoadUnscaled<0b00, 0, 0b10, GPR64, "ldursb",
                [(set GPR64:$Rt,
                      (sextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset)))]>;

// load sign-extended word
defm LDURSW
    : LoadUnscaled<0b10, 0, 0b10, GPR64, "ldursw",
              [(set GPR64:$Rt,
                    (sextloadi32 (am_unscaled32 GPR64sp:$Rn, simm9:$offset)))]>;

// zero and sign extending aliases from generic LDR* mnemonics to LDUR*.
def : InstAlias<"ldrb $Rt, [$Rn, $offset]",
                (LDURBBi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb8:$offset), 0>;
def : InstAlias<"ldrh $Rt, [$Rn, $offset]",
                (LDURHHi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb16:$offset), 0>;
def : InstAlias<"ldrsb $Rt, [$Rn, $offset]",
                (LDURSBWi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb8:$offset), 0>;
def : InstAlias<"ldrsb $Rt, [$Rn, $offset]",
                (LDURSBXi GPR64:$Rt, GPR64sp:$Rn, simm9_offset_fb8:$offset), 0>;
def : InstAlias<"ldrsh $Rt, [$Rn, $offset]",
                (LDURSHWi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb16:$offset), 0>;
def : InstAlias<"ldrsh $Rt, [$Rn, $offset]",
                (LDURSHXi GPR64:$Rt, GPR64sp:$Rn, simm9_offset_fb16:$offset), 0>;
def : InstAlias<"ldrsw $Rt, [$Rn, $offset]",
                (LDURSWi GPR64:$Rt, GPR64sp:$Rn, simm9_offset_fb32:$offset), 0>;

// A LDR will implicitly zero the rest of the vector, so vector_insert(zeros,
// load, 0) can use a single load.
multiclass LoadInsertZeroPatterns<SDPatternOperator LoadOp, ValueType VT, ValueType HVT, ValueType SVT,
                                  ValueType ScalarVT, Instruction LoadInst, Instruction UnscaledLoadInst,
                                  ComplexPattern Addr, ComplexPattern UnscaledAddr, Operand AddrImm,
                                  SubRegIndex SubReg> {
  // Scaled
  def : Pat <(vector_insert (VT immAllZerosV),
                (ScalarVT (LoadOp (Addr GPR64sp:$Rn, AddrImm:$offset))), (i64 0)),
            (SUBREG_TO_REG (i64 0), (LoadInst GPR64sp:$Rn, AddrImm:$offset), SubReg)>;
  // Unscaled
  def : Pat <(vector_insert (VT immAllZerosV),
                 (ScalarVT (LoadOp (UnscaledAddr GPR64sp:$Rn, simm9:$offset))), (i64 0)),
             (SUBREG_TO_REG (i64 0), (UnscaledLoadInst GPR64sp:$Rn, simm9:$offset), SubReg)>;

  // Half-vector patterns
  def : Pat <(vector_insert (HVT immAllZerosV),
                 (ScalarVT (LoadOp (Addr GPR64sp:$Rn, AddrImm:$offset))), (i64 0)),
             (SUBREG_TO_REG (i64 0), (LoadInst GPR64sp:$Rn, AddrImm:$offset), SubReg)>;
  // Unscaled
  def : Pat <(vector_insert (HVT immAllZerosV),
                 (ScalarVT (LoadOp (UnscaledAddr GPR64sp:$Rn, simm9:$offset))), (i64 0)),
             (SUBREG_TO_REG (i64 0), (UnscaledLoadInst GPR64sp:$Rn, simm9:$offset), SubReg)>;

  // SVE patterns
  def : Pat <(vector_insert (SVT immAllZerosV),
                 (ScalarVT (LoadOp (Addr GPR64sp:$Rn, AddrImm:$offset))), (i64 0)),
             (SUBREG_TO_REG (i64 0), (LoadInst GPR64sp:$Rn, AddrImm:$offset), SubReg)>;
  // Unscaled
  def : Pat <(vector_insert (SVT immAllZerosV),
                 (ScalarVT (LoadOp (UnscaledAddr GPR64sp:$Rn, simm9:$offset))), (i64 0)),
             (SUBREG_TO_REG (i64 0), (UnscaledLoadInst GPR64sp:$Rn, simm9:$offset), SubReg)>;
}

defm : LoadInsertZeroPatterns<extloadi8,  v16i8,  v8i8,   nxv16i8,  i32,  LDRBui, LDURBi,
                              am_indexed8,  am_unscaled8,  uimm12s1, bsub>;
defm : LoadInsertZeroPatterns<extloadi16, v8i16,  v4i16,  nxv8i16,  i32,  LDRHui, LDURHi,
                              am_indexed16, am_unscaled16, uimm12s2, hsub>;
defm : LoadInsertZeroPatterns<load,       v4i32,  v2i32,  nxv4i32,  i32,  LDRSui, LDURSi,
                              am_indexed32, am_unscaled32, uimm12s4, ssub>;
defm : LoadInsertZeroPatterns<load,       v2i64,  v1i64,  nxv2i64,  i64,  LDRDui, LDURDi,
                              am_indexed64, am_unscaled64, uimm12s8, dsub>;
defm : LoadInsertZeroPatterns<load,       v8f16,  v4f16,  nxv8f16,  f16,  LDRHui, LDURHi,
                              am_indexed16, am_unscaled16, uimm12s2, hsub>;
defm : LoadInsertZeroPatterns<load,       v8bf16, v4bf16, nxv8bf16, bf16, LDRHui, LDURHi,
                              am_indexed16, am_unscaled16, uimm12s2, hsub>;
defm : LoadInsertZeroPatterns<load,       v4f32,  v2f32,  nxv4f32,  f32,  LDRSui, LDURSi,
                              am_indexed32, am_unscaled32, uimm12s4, ssub>;
defm : LoadInsertZeroPatterns<load,       v2f64,  v1f64,  nxv2f64,  f64,  LDRDui, LDURDi,
                              am_indexed64, am_unscaled64, uimm12s8, dsub>;

// Pre-fetch.
defm PRFUM : PrefetchUnscaled<0b11, 0, 0b10, "prfum",
                  [(AArch64Prefetch timm:$Rt,
                                  (am_unscaled64 GPR64sp:$Rn, simm9:$offset))]>;

//---
// (unscaled immediate, unprivileged)
defm LDTRX : LoadUnprivileged<0b11, 0, 0b01, GPR64, "ldtr">;
defm LDTRW : LoadUnprivileged<0b10, 0, 0b01, GPR32, "ldtr">;

defm LDTRH : LoadUnprivileged<0b01, 0, 0b01, GPR32, "ldtrh">;
defm LDTRB : LoadUnprivileged<0b00, 0, 0b01, GPR32, "ldtrb">;

// load sign-extended half-word
defm LDTRSHW : LoadUnprivileged<0b01, 0, 0b11, GPR32, "ldtrsh">;
defm LDTRSHX : LoadUnprivileged<0b01, 0, 0b10, GPR64, "ldtrsh">;

// load sign-extended byte
defm LDTRSBW : LoadUnprivileged<0b00, 0, 0b11, GPR32, "ldtrsb">;
defm LDTRSBX : LoadUnprivileged<0b00, 0, 0b10, GPR64, "ldtrsb">;

// load sign-extended word
defm LDTRSW  : LoadUnprivileged<0b10, 0, 0b10, GPR64, "ldtrsw">;

//---
// (immediate pre-indexed)
def LDRWpre : LoadPreIdx<0b10, 0, 0b01, GPR32z, "ldr">;
def LDRXpre : LoadPreIdx<0b11, 0, 0b01, GPR64z, "ldr">;
let Predicates = [HasFPARMv8] in {
def LDRBpre : LoadPreIdx<0b00, 1, 0b01, FPR8Op,  "ldr">;
def LDRHpre : LoadPreIdx<0b01, 1, 0b01, FPR16Op, "ldr">;
def LDRSpre : LoadPreIdx<0b10, 1, 0b01, FPR32Op, "ldr">;
def LDRDpre : LoadPreIdx<0b11, 1, 0b01, FPR64Op, "ldr">;
def LDRQpre : LoadPreIdx<0b00, 1, 0b11, FPR128Op, "ldr">;
}

// load sign-extended half-word
def LDRSHWpre : LoadPreIdx<0b01, 0, 0b11, GPR32z, "ldrsh">;
def LDRSHXpre : LoadPreIdx<0b01, 0, 0b10, GPR64z, "ldrsh">;

// load sign-extended byte
def LDRSBWpre : LoadPreIdx<0b00, 0, 0b11, GPR32z, "ldrsb">;
def LDRSBXpre : LoadPreIdx<0b00, 0, 0b10, GPR64z, "ldrsb">;

// load zero-extended byte
def LDRBBpre : LoadPreIdx<0b00, 0, 0b01, GPR32z, "ldrb">;
def LDRHHpre : LoadPreIdx<0b01, 0, 0b01, GPR32z, "ldrh">;

// load sign-extended word
def LDRSWpre : LoadPreIdx<0b10, 0, 0b10, GPR64z, "ldrsw">;

//---
// (immediate post-indexed)
def LDRWpost : LoadPostIdx<0b10, 0, 0b01, GPR32z, "ldr">;
def LDRXpost : LoadPostIdx<0b11, 0, 0b01, GPR64z, "ldr">;
let Predicates = [HasFPARMv8] in {
def LDRBpost : LoadPostIdx<0b00, 1, 0b01, FPR8Op,  "ldr">;
def LDRHpost : LoadPostIdx<0b01, 1, 0b01, FPR16Op, "ldr">;
def LDRSpost : LoadPostIdx<0b10, 1, 0b01, FPR32Op, "ldr">;
def LDRDpost : LoadPostIdx<0b11, 1, 0b01, FPR64Op, "ldr">;
def LDRQpost : LoadPostIdx<0b00, 1, 0b11, FPR128Op, "ldr">;
}

// load sign-extended half-word
def LDRSHWpost : LoadPostIdx<0b01, 0, 0b11, GPR32z, "ldrsh">;
def LDRSHXpost : LoadPostIdx<0b01, 0, 0b10, GPR64z, "ldrsh">;

// load sign-extended byte
def LDRSBWpost : LoadPostIdx<0b00, 0, 0b11, GPR32z, "ldrsb">;
def LDRSBXpost : LoadPostIdx<0b00, 0, 0b10, GPR64z, "ldrsb">;

// load zero-extended byte
def LDRBBpost : LoadPostIdx<0b00, 0, 0b01, GPR32z, "ldrb">;
def LDRHHpost : LoadPostIdx<0b01, 0, 0b01, GPR32z, "ldrh">;

// load sign-extended word
def LDRSWpost : LoadPostIdx<0b10, 0, 0b10, GPR64z, "ldrsw">;

//===----------------------------------------------------------------------===//
// Store instructions.
//===----------------------------------------------------------------------===//

// Pair (indexed, offset)
// FIXME: Use dedicated range-checked addressing mode operand here.
defm STPW : StorePairOffset<0b00, 0, GPR32z, simm7s4, "stp">;
defm STPX : StorePairOffset<0b10, 0, GPR64z, simm7s8, "stp">;
let Predicates = [HasFPARMv8] in {
defm STPS : StorePairOffset<0b00, 1, FPR32Op, simm7s4, "stp">;
defm STPD : StorePairOffset<0b01, 1, FPR64Op, simm7s8, "stp">;
defm STPQ : StorePairOffset<0b10, 1, FPR128Op, simm7s16, "stp">;
}

// Pair (pre-indexed)
def STPWpre : StorePairPreIdx<0b00, 0, GPR32z, simm7s4, "stp">;
def STPXpre : StorePairPreIdx<0b10, 0, GPR64z, simm7s8, "stp">;
let Predicates = [HasFPARMv8] in {
def STPSpre : StorePairPreIdx<0b00, 1, FPR32Op, simm7s4, "stp">;
def STPDpre : StorePairPreIdx<0b01, 1, FPR64Op, simm7s8, "stp">;
def STPQpre : StorePairPreIdx<0b10, 1, FPR128Op, simm7s16, "stp">;
}

// Pair (post-indexed)
def STPWpost : StorePairPostIdx<0b00, 0, GPR32z, simm7s4, "stp">;
def STPXpost : StorePairPostIdx<0b10, 0, GPR64z, simm7s8, "stp">;
let Predicates = [HasFPARMv8] in {
def STPSpost : StorePairPostIdx<0b00, 1, FPR32Op, simm7s4, "stp">;
def STPDpost : StorePairPostIdx<0b01, 1, FPR64Op, simm7s8, "stp">;
def STPQpost : StorePairPostIdx<0b10, 1, FPR128Op, simm7s16, "stp">;
}

// Pair (no allocate)
defm STNPW : StorePairNoAlloc<0b00, 0, GPR32z, simm7s4, "stnp">;
defm STNPX : StorePairNoAlloc<0b10, 0, GPR64z, simm7s8, "stnp">;
let Predicates = [HasFPARMv8] in {
defm STNPS : StorePairNoAlloc<0b00, 1, FPR32Op, simm7s4, "stnp">;
defm STNPD : StorePairNoAlloc<0b01, 1, FPR64Op, simm7s8, "stnp">;
defm STNPQ : StorePairNoAlloc<0b10, 1, FPR128Op, simm7s16, "stnp">;
}

def : Pat<(AArch64stp GPR64z:$Rt, GPR64z:$Rt2, (am_indexed7s64 GPR64sp:$Rn, simm7s8:$offset)),
          (STPXi GPR64z:$Rt, GPR64z:$Rt2, GPR64sp:$Rn, simm7s8:$offset)>;

def : Pat<(AArch64stnp FPR128:$Rt, FPR128:$Rt2, (am_indexed7s128 GPR64sp:$Rn, simm7s16:$offset)),
          (STNPQi FPR128:$Rt, FPR128:$Rt2, GPR64sp:$Rn, simm7s16:$offset)>;


//---
// (Register offset)

// Integer
defm STRBB : Store8RO< 0b00, 0, 0b00, GPR32, "strb", i32, truncstorei8>;
defm STRHH : Store16RO<0b01, 0, 0b00, GPR32, "strh", i32, truncstorei16>;
defm STRW  : Store32RO<0b10, 0, 0b00, GPR32, "str",  i32, store>;
defm STRX  : Store64RO<0b11, 0, 0b00, GPR64, "str",  i64, store>;


// Floating-point
let Predicates = [HasFPARMv8] in {
defm STRB : Store8RO< 0b00,  1, 0b00, FPR8Op,   "str", i8, store>;
defm STRH : Store16RO<0b01,  1, 0b00, FPR16Op,  "str", f16,     store>;
defm STRS : Store32RO<0b10,  1, 0b00, FPR32Op,  "str", f32,     store>;
defm STRD : Store64RO<0b11,  1, 0b00, FPR64Op,  "str", f64,     store>;
defm STRQ : Store128RO<0b00, 1, 0b10, FPR128Op, "str">;
}

let Predicates = [UseSTRQro], AddedComplexity = 10 in {
  def : Pat<(store (f128 FPR128:$Rt),
                        (ro_Windexed128 GPR64sp:$Rn, GPR32:$Rm,
                                        ro_Wextend128:$extend)),
            (STRQroW FPR128:$Rt, GPR64sp:$Rn, GPR32:$Rm, ro_Wextend128:$extend)>;
  def : Pat<(store (f128 FPR128:$Rt),
                        (ro_Xindexed128 GPR64sp:$Rn, GPR64:$Rm,
                                        ro_Xextend128:$extend)),
            (STRQroX FPR128:$Rt, GPR64sp:$Rn, GPR64:$Rm, ro_Wextend128:$extend)>;
}

multiclass TruncStoreFrom64ROPat<ROAddrMode ro, SDPatternOperator storeop,
                                 Instruction STRW, Instruction STRX> {

  def : Pat<(storeop GPR64:$Rt,
                     (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)),
            (STRW (EXTRACT_SUBREG GPR64:$Rt, sub_32),
                  GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)>;

  def : Pat<(storeop GPR64:$Rt,
                     (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)),
            (STRX (EXTRACT_SUBREG GPR64:$Rt, sub_32),
                  GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)>;
}

let AddedComplexity = 10 in {
  // truncstore i64
  defm : TruncStoreFrom64ROPat<ro8,  truncstorei8,  STRBBroW, STRBBroX>;
  defm : TruncStoreFrom64ROPat<ro16, truncstorei16, STRHHroW, STRHHroX>;
  defm : TruncStoreFrom64ROPat<ro32, truncstorei32, STRWroW,  STRWroX>;
}

multiclass VecROStorePat<ROAddrMode ro, ValueType VecTy, RegisterClass FPR,
                         Instruction STRW, Instruction STRX> {
  def : Pat<(store (VecTy FPR:$Rt),
                   (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)),
            (STRW FPR:$Rt, GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)>;

  def : Pat<(store (VecTy FPR:$Rt),
                   (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)),
            (STRX FPR:$Rt, GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)>;
}

let AddedComplexity = 10 in {
// Match all store 64 bits width whose type is compatible with FPR64
let Predicates = [IsLE] in {
  // We must use ST1 to store vectors in big-endian.
  defm : VecROStorePat<ro64, v2i32, FPR64, STRDroW, STRDroX>;
  defm : VecROStorePat<ro64, v2f32, FPR64, STRDroW, STRDroX>;
  defm : VecROStorePat<ro64, v4i16, FPR64, STRDroW, STRDroX>;
  defm : VecROStorePat<ro64, v8i8, FPR64, STRDroW, STRDroX>;
  defm : VecROStorePat<ro64, v4f16, FPR64, STRDroW, STRDroX>;
  defm : VecROStorePat<ro64, v4bf16, FPR64, STRDroW, STRDroX>;
}

defm : VecROStorePat<ro64, v1i64, FPR64, STRDroW, STRDroX>;
defm : VecROStorePat<ro64, v1f64, FPR64, STRDroW, STRDroX>;

// Match all store 128 bits width whose type is compatible with FPR128
let Predicates = [IsLE, UseSTRQro] in {
  // We must use ST1 to store vectors in big-endian.
  defm : VecROStorePat<ro128, v2i64, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v2f64, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v4i32, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v4f32, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v8i16, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v16i8, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v8f16, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v8bf16, FPR128, STRQroW, STRQroX>;
}
} // AddedComplexity = 10

// Match stores from lane 0 to the appropriate subreg's store.
multiclass VecROStoreLane0Pat<ROAddrMode ro, SDPatternOperator storeop,
                              ValueType VecTy, ValueType STy,
                              ValueType SubRegTy,
                              SubRegIndex SubRegIdx,
                              Instruction STRW, Instruction STRX> {

  def : Pat<(storeop (STy (vector_extract (VecTy VecListOne128:$Vt), (i64 0))),
                     (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)),
            (STRW (SubRegTy (EXTRACT_SUBREG VecListOne128:$Vt, SubRegIdx)),
                  GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)>;

  def : Pat<(storeop (STy (vector_extract (VecTy VecListOne128:$Vt), (i64 0))),
                     (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)),
            (STRX (SubRegTy (EXTRACT_SUBREG VecListOne128:$Vt, SubRegIdx)),
                  GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)>;
}

let AddedComplexity = 19 in {
  defm : VecROStoreLane0Pat<ro16, truncstorei16, v8i16, i32, f16, hsub, STRHroW, STRHroX>;
  defm : VecROStoreLane0Pat<ro16,         store, v8f16, f16, f16, hsub, STRHroW, STRHroX>;
  defm : VecROStoreLane0Pat<ro32,         store, v4i32, i32, i32, ssub, STRSroW, STRSroX>;
  defm : VecROStoreLane0Pat<ro32,         store, v4f32, f32, i32, ssub, STRSroW, STRSroX>;
  defm : VecROStoreLane0Pat<ro64,         store, v2i64, i64, i64, dsub, STRDroW, STRDroX>;
  defm : VecROStoreLane0Pat<ro64,         store, v2f64, f64, i64, dsub, STRDroW, STRDroX>;
}

//---
// (unsigned immediate)
defm STRX : StoreUIz<0b11, 0, 0b00, GPR64z, uimm12s8, "str",
                   [(store GPR64z:$Rt,
                            (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))]>;
defm STRW : StoreUIz<0b10, 0, 0b00, GPR32z, uimm12s4, "str",
                    [(store GPR32z:$Rt,
                            (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))]>;
let Predicates = [HasFPARMv8] in {
defm STRB : StoreUI<0b00, 1, 0b00, FPR8Op, uimm12s1, "str",
                    [(store FPR8Op:$Rt,
                            (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))]>;
defm STRH : StoreUI<0b01, 1, 0b00, FPR16Op, uimm12s2, "str",
                    [(store (f16 FPR16Op:$Rt),
                            (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))]>;
defm STRS : StoreUI<0b10, 1, 0b00, FPR32Op, uimm12s4, "str",
                    [(store (f32 FPR32Op:$Rt),
                            (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))]>;
defm STRD : StoreUI<0b11, 1, 0b00, FPR64Op, uimm12s8, "str",
                    [(store (f64 FPR64Op:$Rt),
                            (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))]>;
defm STRQ : StoreUI<0b00, 1, 0b10, FPR128Op, uimm12s16, "str", []>;
}

defm STRHH : StoreUIz<0b01, 0, 0b00, GPR32z, uimm12s2, "strh",
                     [(truncstorei16 GPR32z:$Rt,
                                     (am_indexed16 GPR64sp:$Rn,
                                                   uimm12s2:$offset))]>;
defm STRBB : StoreUIz<0b00, 0, 0b00, GPR32z, uimm12s1,  "strb",
                     [(truncstorei8 GPR32z:$Rt,
                                    (am_indexed8 GPR64sp:$Rn,
                                                 uimm12s1:$offset))]>;

// bf16 store pattern
def : Pat<(store (bf16 FPR16Op:$Rt),
                 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset)),
          (STRHui FPR16:$Rt, GPR64sp:$Rn, uimm12s2:$offset)>;

let AddedComplexity = 10 in {

// Match all store 64 bits width whose type is compatible with FPR64
def : Pat<(store (v1i64 FPR64:$Rt),
                 (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
          (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
def : Pat<(store (v1f64 FPR64:$Rt),
                 (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
          (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;

let Predicates = [IsLE] in {
  // We must use ST1 to store vectors in big-endian.
  def : Pat<(store (v2f32 FPR64:$Rt),
                   (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
            (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(store (v8i8 FPR64:$Rt),
                   (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
            (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(store (v4i16 FPR64:$Rt),
                   (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
            (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(store (v2i32 FPR64:$Rt),
                   (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
            (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(store (v4f16 FPR64:$Rt),
                   (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
            (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(store (v4bf16 FPR64:$Rt),
                   (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
            (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
}

// Match all store 128 bits width whose type is compatible with FPR128
def : Pat<(store (f128  FPR128:$Rt),
                 (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
          (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;

let Predicates = [IsLE] in {
  // We must use ST1 to store vectors in big-endian.
  def : Pat<(store (v4f32 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v2f64 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v16i8 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v8i16 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v4i32 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v2i64 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v8f16 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v8bf16 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
}

// truncstore i64
def : Pat<(truncstorei32 GPR64:$Rt,
                         (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset)),
  (STRWui (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$Rn, uimm12s4:$offset)>;
def : Pat<(truncstorei16 GPR64:$Rt,
                         (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset)),
  (STRHHui (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$Rn, uimm12s2:$offset)>;
def : Pat<(truncstorei8 GPR64:$Rt, (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset)),
  (STRBBui (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$Rn, uimm12s1:$offset)>;

} // AddedComplexity = 10

// Match stores from lane 0 to the appropriate subreg's store.
multiclass VecStoreLane0Pat<ComplexPattern UIAddrMode, SDPatternOperator storeop,
                            ValueType VTy, ValueType STy,
                            ValueType SubRegTy,
                            SubRegIndex SubRegIdx, Operand IndexType,
                            Instruction STR> {
  def : Pat<(storeop (STy (vector_extract (VTy VecListOne128:$Vt), (i64 0))),
                     (UIAddrMode GPR64sp:$Rn, IndexType:$offset)),
            (STR (SubRegTy (EXTRACT_SUBREG VecListOne128:$Vt, SubRegIdx)),
                 GPR64sp:$Rn, IndexType:$offset)>;
}

let AddedComplexity = 19 in {
  defm : VecStoreLane0Pat<am_indexed16, truncstorei16, v8i16, i32, f16, hsub, uimm12s2, STRHui>;
  defm : VecStoreLane0Pat<am_indexed16,         store, v8f16, f16, f16, hsub, uimm12s2, STRHui>;
  defm : VecStoreLane0Pat<am_indexed32,         store, v4i32, i32, i32, ssub, uimm12s4, STRSui>;
  defm : VecStoreLane0Pat<am_indexed32,         store, v4f32, f32, i32, ssub, uimm12s4, STRSui>;
  defm : VecStoreLane0Pat<am_indexed64,         store, v2i64, i64, i64, dsub, uimm12s8, STRDui>;
  defm : VecStoreLane0Pat<am_indexed64,         store, v2f64, f64, i64, dsub, uimm12s8, STRDui>;
}

//---
// (unscaled immediate)
defm STURX : StoreUnscaled<0b11, 0, 0b00, GPR64z, "stur",
                         [(store GPR64z:$Rt,
                                 (am_unscaled64 GPR64sp:$Rn, simm9:$offset))]>;
defm STURW : StoreUnscaled<0b10, 0, 0b00, GPR32z, "stur",
                         [(store GPR32z:$Rt,
                                 (am_unscaled32 GPR64sp:$Rn, simm9:$offset))]>;
let Predicates = [HasFPARMv8] in {
defm STURB : StoreUnscaled<0b00, 1, 0b00, FPR8Op, "stur",
                         [(store FPR8Op:$Rt,
                                 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))]>;
defm STURH : StoreUnscaled<0b01, 1, 0b00, FPR16Op, "stur",
                         [(store (f16 FPR16Op:$Rt),
                                 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))]>;
defm STURS : StoreUnscaled<0b10, 1, 0b00, FPR32Op, "stur",
                         [(store (f32 FPR32Op:$Rt),
                                 (am_unscaled32 GPR64sp:$Rn, simm9:$offset))]>;
defm STURD : StoreUnscaled<0b11, 1, 0b00, FPR64Op, "stur",
                         [(store (f64 FPR64Op:$Rt),
                                 (am_unscaled64 GPR64sp:$Rn, simm9:$offset))]>;
defm STURQ : StoreUnscaled<0b00, 1, 0b10, FPR128Op, "stur",
                         [(store (f128 FPR128Op:$Rt),
                                 (am_unscaled128 GPR64sp:$Rn, simm9:$offset))]>;
}
defm STURHH : StoreUnscaled<0b01, 0, 0b00, GPR32z, "sturh",
                         [(truncstorei16 GPR32z:$Rt,
                                 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))]>;
defm STURBB : StoreUnscaled<0b00, 0, 0b00, GPR32z, "sturb",
                         [(truncstorei8 GPR32z:$Rt,
                                  (am_unscaled8 GPR64sp:$Rn, simm9:$offset))]>;

// bf16 store pattern
def : Pat<(store (bf16 FPR16Op:$Rt),
                 (am_unscaled16 GPR64sp:$Rn, simm9:$offset)),
          (STURHi FPR16:$Rt, GPR64sp:$Rn, simm9:$offset)>;

// Armv8.4 Weaker Release Consistency enhancements
//         LDAPR & STLR with Immediate Offset instructions
let Predicates = [HasRCPC_IMMO] in {
defm STLURB     : BaseStoreUnscaleV84<"stlurb",  0b00, 0b00, GPR32>;
defm STLURH     : BaseStoreUnscaleV84<"stlurh",  0b01, 0b00, GPR32>;
defm STLURW     : BaseStoreUnscaleV84<"stlur",   0b10, 0b00, GPR32>;
defm STLURX     : BaseStoreUnscaleV84<"stlur",   0b11, 0b00, GPR64>;
defm LDAPURB    : BaseLoadUnscaleV84<"ldapurb",  0b00, 0b01, GPR32>;
defm LDAPURSBW  : BaseLoadUnscaleV84<"ldapursb", 0b00, 0b11, GPR32>;
defm LDAPURSBX  : BaseLoadUnscaleV84<"ldapursb", 0b00, 0b10, GPR64>;
defm LDAPURH    : BaseLoadUnscaleV84<"ldapurh",  0b01, 0b01, GPR32>;
defm LDAPURSHW  : BaseLoadUnscaleV84<"ldapursh", 0b01, 0b11, GPR32>;
defm LDAPURSHX  : BaseLoadUnscaleV84<"ldapursh", 0b01, 0b10, GPR64>;
defm LDAPUR     : BaseLoadUnscaleV84<"ldapur",   0b10, 0b01, GPR32>;
defm LDAPURSW   : BaseLoadUnscaleV84<"ldapursw", 0b10, 0b10, GPR64>;
defm LDAPURX    : BaseLoadUnscaleV84<"ldapur",   0b11, 0b01, GPR64>;
}

// Match all store 64 bits width whose type is compatible with FPR64
def : Pat<(store (v1f64 FPR64:$Rt), (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
          (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(store (v1i64 FPR64:$Rt), (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
          (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;

let AddedComplexity = 10 in {

let Predicates = [IsLE] in {
  // We must use ST1 to store vectors in big-endian.
  def : Pat<(store (v2f32 FPR64:$Rt),
                   (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
            (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v8i8 FPR64:$Rt),
                   (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
            (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v4i16 FPR64:$Rt),
                   (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
            (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v2i32 FPR64:$Rt),
                   (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
            (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v4f16 FPR64:$Rt),
                   (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
            (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v4bf16 FPR64:$Rt),
                   (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
            (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
}

// Match all store 128 bits width whose type is compatible with FPR128
def : Pat<(store (f128 FPR128:$Rt), (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
          (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;

let Predicates = [IsLE] in {
  // We must use ST1 to store vectors in big-endian.
  def : Pat<(store (v4f32 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v2f64 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v16i8 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v8i16 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v4i32 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v2i64 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v2f64 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v8f16 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v8bf16 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
}

} // AddedComplexity = 10

// unscaled i64 truncating stores
def : Pat<(truncstorei32 GPR64:$Rt, (am_unscaled32 GPR64sp:$Rn, simm9:$offset)),
  (STURWi (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(truncstorei16 GPR64:$Rt, (am_unscaled16 GPR64sp:$Rn, simm9:$offset)),
  (STURHHi (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(truncstorei8 GPR64:$Rt, (am_unscaled8 GPR64sp:$Rn, simm9:$offset)),
  (STURBBi (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$Rn, simm9:$offset)>;

// Match stores from lane 0 to the appropriate subreg's store.
multiclass VecStoreULane0Pat<SDPatternOperator StoreOp,
                             ValueType VTy, ValueType STy,
                             ValueType SubRegTy,
                             SubRegIndex SubRegIdx, Instruction STR> {
  defm : VecStoreLane0Pat<am_unscaled128, StoreOp, VTy, STy, SubRegTy, SubRegIdx, simm9, STR>;
}

let AddedComplexity = 19 in {
  defm : VecStoreULane0Pat<truncstorei16, v8i16, i32, f16, hsub, STURHi>;
  defm : VecStoreULane0Pat<store,         v8f16, f16, f16, hsub, STURHi>;
  defm : VecStoreULane0Pat<store,         v4i32, i32, i32, ssub, STURSi>;
  defm : VecStoreULane0Pat<store,         v4f32, f32, i32, ssub, STURSi>;
  defm : VecStoreULane0Pat<store,         v2i64, i64, i64, dsub, STURDi>;
  defm : VecStoreULane0Pat<store,         v2f64, f64, i64, dsub, STURDi>;
}

//---
// STR mnemonics fall back to STUR for negative or unaligned offsets.
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURXi GPR64:$Rt, GPR64sp:$Rn, simm9_offset_fb64:$offset), 0>;
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURWi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb32:$offset), 0>;
let Predicates = [HasFPARMv8] in {
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURBi FPR8Op:$Rt, GPR64sp:$Rn, simm9_offset_fb8:$offset), 0>;
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURHi FPR16Op:$Rt, GPR64sp:$Rn, simm9_offset_fb16:$offset), 0>;
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURSi FPR32Op:$Rt, GPR64sp:$Rn, simm9_offset_fb32:$offset), 0>;
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURDi FPR64Op:$Rt, GPR64sp:$Rn, simm9_offset_fb64:$offset), 0>;
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURQi FPR128Op:$Rt, GPR64sp:$Rn, simm9_offset_fb128:$offset), 0>;
}

def : InstAlias<"strb $Rt, [$Rn, $offset]",
                (STURBBi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb8:$offset), 0>;
def : InstAlias<"strh $Rt, [$Rn, $offset]",
                (STURHHi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb16:$offset), 0>;

//---
// (unscaled immediate, unprivileged)
defm STTRW : StoreUnprivileged<0b10, 0, 0b00, GPR32, "sttr">;
defm STTRX : StoreUnprivileged<0b11, 0, 0b00, GPR64, "sttr">;

defm STTRH : StoreUnprivileged<0b01, 0, 0b00, GPR32, "sttrh">;
defm STTRB : StoreUnprivileged<0b00, 0, 0b00, GPR32, "sttrb">;

//---
// (immediate pre-indexed)
def STRWpre : StorePreIdx<0b10, 0, 0b00, GPR32z, "str",  pre_store, i32>;
def STRXpre : StorePreIdx<0b11, 0, 0b00, GPR64z, "str",  pre_store, i64>;
let Predicates = [HasFPARMv8] in {
def STRBpre : StorePreIdx<0b00, 1, 0b00, FPR8Op,  "str",  pre_store, i8>;
def STRHpre : StorePreIdx<0b01, 1, 0b00, FPR16Op, "str",  pre_store, f16>;
def STRSpre : StorePreIdx<0b10, 1, 0b00, FPR32Op, "str",  pre_store, f32>;
def STRDpre : StorePreIdx<0b11, 1, 0b00, FPR64Op, "str",  pre_store, f64>;
def STRQpre : StorePreIdx<0b00, 1, 0b10, FPR128Op, "str", pre_store, f128>;
}

def STRBBpre : StorePreIdx<0b00, 0, 0b00, GPR32z, "strb", pre_truncsti8,  i32>;
def STRHHpre : StorePreIdx<0b01, 0, 0b00, GPR32z, "strh", pre_truncsti16, i32>;

// truncstore i64
def : Pat<(pre_truncsti32 GPR64:$Rt, GPR64sp:$addr, simm9:$off),
  (STRWpre (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$addr,
           simm9:$off)>;
def : Pat<(pre_truncsti16 GPR64:$Rt, GPR64sp:$addr, simm9:$off),
  (STRHHpre (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$addr,
            simm9:$off)>;
def : Pat<(pre_truncsti8 GPR64:$Rt, GPR64sp:$addr, simm9:$off),
  (STRBBpre (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$addr,
            simm9:$off)>;

def : Pat<(pre_store (v8i8 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v4i16 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v2i32 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v2f32 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v1i64 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v1f64 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v4f16 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;

def : Pat<(pre_store (v16i8 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v8i16 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v4i32 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v4f32 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v2i64 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v2f64 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v8f16 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;

//---
// (immediate post-indexed)
def STRWpost : StorePostIdx<0b10, 0, 0b00, GPR32z,  "str", post_store, i32>;
def STRXpost : StorePostIdx<0b11, 0, 0b00, GPR64z,  "str", post_store, i64>;
let Predicates = [HasFPARMv8] in {
def STRBpost : StorePostIdx<0b00, 1, 0b00, FPR8Op,   "str", post_store, i8>;
def STRHpost : StorePostIdx<0b01, 1, 0b00, FPR16Op,  "str", post_store, f16>;
def STRSpost : StorePostIdx<0b10, 1, 0b00, FPR32Op,  "str", post_store, f32>;
def STRDpost : StorePostIdx<0b11, 1, 0b00, FPR64Op,  "str", post_store, f64>;
def STRQpost : StorePostIdx<0b00, 1, 0b10, FPR128Op, "str", post_store, f128>;
}

def STRBBpost : StorePostIdx<0b00, 0, 0b00, GPR32z, "strb", post_truncsti8, i32>;
def STRHHpost : StorePostIdx<0b01, 0, 0b00, GPR32z, "strh", post_truncsti16, i32>;

// truncstore i64
def : Pat<(post_truncsti32 GPR64:$Rt, GPR64sp:$addr, simm9:$off),
  (STRWpost (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$addr,
            simm9:$off)>;
def : Pat<(post_truncsti16 GPR64:$Rt, GPR64sp:$addr, simm9:$off),
  (STRHHpost (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$addr,
             simm9:$off)>;
def : Pat<(post_truncsti8 GPR64:$Rt, GPR64sp:$addr, simm9:$off),
  (STRBBpost (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$addr,
             simm9:$off)>;

def : Pat<(post_store (bf16 FPR16:$Rt), GPR64sp:$addr, simm9:$off),
          (STRHpost FPR16:$Rt, GPR64sp:$addr, simm9:$off)>;

def : Pat<(post_store (v8i8 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v4i16 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v2i32 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v2f32 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v1i64 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v1f64 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v4f16 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v4bf16 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;

def : Pat<(post_store (v16i8 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v8i16 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v4i32 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v4f32 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v2i64 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v2f64 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v8f16 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v8bf16 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;

//===----------------------------------------------------------------------===//
// Load/store exclusive instructions.
//===----------------------------------------------------------------------===//

def LDARW  : LoadAcquire   <0b10, 1, 1, 0, 1, GPR32, "ldar">;
def LDARX  : LoadAcquire   <0b11, 1, 1, 0, 1, GPR64, "ldar">;
def LDARB  : LoadAcquire   <0b00, 1, 1, 0, 1, GPR32, "ldarb">;
def LDARH  : LoadAcquire   <0b01, 1, 1, 0, 1, GPR32, "ldarh">;

def LDAXRW : LoadExclusive <0b10, 0, 1, 0, 1, GPR32, "ldaxr">;
def LDAXRX : LoadExclusive <0b11, 0, 1, 0, 1, GPR64, "ldaxr">;
def LDAXRB : LoadExclusive <0b00, 0, 1, 0, 1, GPR32, "ldaxrb">;
def LDAXRH : LoadExclusive <0b01, 0, 1, 0, 1, GPR32, "ldaxrh">;

def LDXRW  : LoadExclusive <0b10, 0, 1, 0, 0, GPR32, "ldxr">;
def LDXRX  : LoadExclusive <0b11, 0, 1, 0, 0, GPR64, "ldxr">;
def LDXRB  : LoadExclusive <0b00, 0, 1, 0, 0, GPR32, "ldxrb">;
def LDXRH  : LoadExclusive <0b01, 0, 1, 0, 0, GPR32, "ldxrh">;

def STLRW  : StoreRelease  <0b10, 1, 0, 0, 1, GPR32, "stlr">;
def STLRX  : StoreRelease  <0b11, 1, 0, 0, 1, GPR64, "stlr">;
def STLRB  : StoreRelease  <0b00, 1, 0, 0, 1, GPR32, "stlrb">;
def STLRH  : StoreRelease  <0b01, 1, 0, 0, 1, GPR32, "stlrh">;

/*
Aliases for when offset=0. Note that in contrast to LoadAcquire which has a $Rn
of type GPR64sp0, we deliberately choose to make $Rn of type GPR64sp and add an
alias for the case of immediate #0. This is because new STLR versions (from
LRCPC3 extension) do have a non-zero immediate value, so GPR64sp0 is not
appropriate anymore (it parses and discards the optional zero). This is not the
case for LoadAcquire because the new LRCPC3 LDAR instructions are post-indexed,
and the immediate values are not inside the [] brackets and thus not accepted
by GPR64sp0 parser.
*/
def STLRW0  : InstAlias<"stlr\t$Rt, [$Rn, #0]" , (STLRW   GPR32: $Rt, GPR64sp:$Rn)>;
def STLRX0  : InstAlias<"stlr\t$Rt, [$Rn, #0]" , (STLRX   GPR64: $Rt, GPR64sp:$Rn)>;
def STLRB0  : InstAlias<"stlrb\t$Rt, [$Rn, #0]", (STLRB   GPR32: $Rt, GPR64sp:$Rn)>;
def STLRH0  : InstAlias<"stlrh\t$Rt, [$Rn, #0]", (STLRH   GPR32: $Rt, GPR64sp:$Rn)>;

def STLXRW : StoreExclusive<0b10, 0, 0, 0, 1, GPR32, "stlxr">;
def STLXRX : StoreExclusive<0b11, 0, 0, 0, 1, GPR64, "stlxr">;
def STLXRB : StoreExclusive<0b00, 0, 0, 0, 1, GPR32, "stlxrb">;
def STLXRH : StoreExclusive<0b01, 0, 0, 0, 1, GPR32, "stlxrh">;

def STXRW  : StoreExclusive<0b10, 0, 0, 0, 0, GPR32, "stxr">;
def STXRX  : StoreExclusive<0b11, 0, 0, 0, 0, GPR64, "stxr">;
def STXRB  : StoreExclusive<0b00, 0, 0, 0, 0, GPR32, "stxrb">;
def STXRH  : StoreExclusive<0b01, 0, 0, 0, 0, GPR32, "stxrh">;

def LDAXPW : LoadExclusivePair<0b10, 0, 1, 1, 1, GPR32, "ldaxp">;
def LDAXPX : LoadExclusivePair<0b11, 0, 1, 1, 1, GPR64, "ldaxp">;

def LDXPW  : LoadExclusivePair<0b10, 0, 1, 1, 0, GPR32, "ldxp">;
def LDXPX  : LoadExclusivePair<0b11, 0, 1, 1, 0, GPR64, "ldxp">;

def STLXPW : StoreExclusivePair<0b10, 0, 0, 1, 1, GPR32, "stlxp">;
def STLXPX : StoreExclusivePair<0b11, 0, 0, 1, 1, GPR64, "stlxp">;

def STXPW  : StoreExclusivePair<0b10, 0, 0, 1, 0, GPR32, "stxp">;
def STXPX  : StoreExclusivePair<0b11, 0, 0, 1, 0, GPR64, "stxp">;

let Predicates = [HasLOR] in {
  // v8.1a "Limited Order Region" extension load-acquire instructions
  def LDLARW  : LoadAcquire   <0b10, 1, 1, 0, 0, GPR32, "ldlar">;
  def LDLARX  : LoadAcquire   <0b11, 1, 1, 0, 0, GPR64, "ldlar">;
  def LDLARB  : LoadAcquire   <0b00, 1, 1, 0, 0, GPR32, "ldlarb">;
  def LDLARH  : LoadAcquire   <0b01, 1, 1, 0, 0, GPR32, "ldlarh">;

  // v8.1a "Limited Order Region" extension store-release instructions
  def STLLRW  : StoreRelease   <0b10, 1, 0, 0, 0, GPR32, "stllr">;
  def STLLRX  : StoreRelease   <0b11, 1, 0, 0, 0, GPR64, "stllr">;
  def STLLRB  : StoreRelease   <0b00, 1, 0, 0, 0, GPR32, "stllrb">;
  def STLLRH  : StoreRelease   <0b01, 1, 0, 0, 0, GPR32, "stllrh">;

  // Aliases for when offset=0
  def STLLRW0 : InstAlias<"stllr\t$Rt,  [$Rn, #0]",  (STLLRW   GPR32: $Rt, GPR64sp:$Rn)>;
  def STLLRX0 : InstAlias<"stllr\t$Rt,  [$Rn, #0]",  (STLLRX   GPR64: $Rt, GPR64sp:$Rn)>;
  def STLLRB0 : InstAlias<"stllrb\t$Rt, [$Rn, #0]",  (STLLRB   GPR32: $Rt, GPR64sp:$Rn)>;
  def STLLRH0 : InstAlias<"stllrh\t$Rt, [$Rn, #0]",  (STLLRH   GPR32: $Rt, GPR64sp:$Rn)>;
}

//===----------------------------------------------------------------------===//
// Scaled floating point to integer conversion instructions.
//===----------------------------------------------------------------------===//

defm FCVTAS : FPToIntegerUnscaled<0b00, 0b100, "fcvtas", int_aarch64_neon_fcvtas>;
defm FCVTAU : FPToIntegerUnscaled<0b00, 0b101, "fcvtau", int_aarch64_neon_fcvtau>;
defm FCVTMS : FPToIntegerUnscaled<0b10, 0b000, "fcvtms", int_aarch64_neon_fcvtms>;
defm FCVTMU : FPToIntegerUnscaled<0b10, 0b001, "fcvtmu", int_aarch64_neon_fcvtmu>;
defm FCVTNS : FPToIntegerUnscaled<0b00, 0b000, "fcvtns", int_aarch64_neon_fcvtns>;
defm FCVTNU : FPToIntegerUnscaled<0b00, 0b001, "fcvtnu", int_aarch64_neon_fcvtnu>;
defm FCVTPS : FPToIntegerUnscaled<0b01, 0b000, "fcvtps", int_aarch64_neon_fcvtps>;
defm FCVTPU : FPToIntegerUnscaled<0b01, 0b001, "fcvtpu", int_aarch64_neon_fcvtpu>;
defm FCVTZS : FPToIntegerUnscaled<0b11, 0b000, "fcvtzs", any_fp_to_sint>;
defm FCVTZU : FPToIntegerUnscaled<0b11, 0b001, "fcvtzu", any_fp_to_uint>;
defm FCVTZS : FPToIntegerScaled<0b11, 0b000, "fcvtzs", any_fp_to_sint>;
defm FCVTZU : FPToIntegerScaled<0b11, 0b001, "fcvtzu", any_fp_to_uint>;

// AArch64's FCVT instructions saturate when out of range.
multiclass FPToIntegerSatPats<SDNode to_int_sat, string INST> {
  let Predicates = [HasFullFP16] in {
  def : Pat<(i32 (to_int_sat f16:$Rn, i32)),
            (!cast<Instruction>(INST # UWHr) f16:$Rn)>;
  def : Pat<(i64 (to_int_sat f16:$Rn, i64)),
            (!cast<Instruction>(INST # UXHr) f16:$Rn)>;
  }
  def : Pat<(i32 (to_int_sat f32:$Rn, i32)),
            (!cast<Instruction>(INST # UWSr) f32:$Rn)>;
  def : Pat<(i64 (to_int_sat f32:$Rn, i64)),
            (!cast<Instruction>(INST # UXSr) f32:$Rn)>;
  def : Pat<(i32 (to_int_sat f64:$Rn, i32)),
            (!cast<Instruction>(INST # UWDr) f64:$Rn)>;
  def : Pat<(i64 (to_int_sat f64:$Rn, i64)),
            (!cast<Instruction>(INST # UXDr) f64:$Rn)>;

  let Predicates = [HasFullFP16] in {
  def : Pat<(i32 (to_int_sat (fmul f16:$Rn, fixedpoint_f16_i32:$scale), i32)),
            (!cast<Instruction>(INST # SWHri) $Rn, $scale)>;
  def : Pat<(i64 (to_int_sat (fmul f16:$Rn, fixedpoint_f16_i64:$scale), i64)),
            (!cast<Instruction>(INST # SXHri) $Rn, $scale)>;
  }
  def : Pat<(i32 (to_int_sat (fmul f32:$Rn, fixedpoint_f32_i32:$scale), i32)),
            (!cast<Instruction>(INST # SWSri) $Rn, $scale)>;
  def : Pat<(i64 (to_int_sat (fmul f32:$Rn, fixedpoint_f32_i64:$scale), i64)),
            (!cast<Instruction>(INST # SXSri) $Rn, $scale)>;
  def : Pat<(i32 (to_int_sat (fmul f64:$Rn, fixedpoint_f64_i32:$scale), i32)),
            (!cast<Instruction>(INST # SWDri) $Rn, $scale)>;
  def : Pat<(i64 (to_int_sat (fmul f64:$Rn, fixedpoint_f64_i64:$scale), i64)),
            (!cast<Instruction>(INST # SXDri) $Rn, $scale)>;
}

defm : FPToIntegerSatPats<fp_to_sint_sat, "FCVTZS">;
defm : FPToIntegerSatPats<fp_to_uint_sat, "FCVTZU">;

multiclass FPToIntegerIntPats<Intrinsic round, string INST> {
  let Predicates = [HasFullFP16] in {
  def : Pat<(i32 (round f16:$Rn)), (!cast<Instruction>(INST # UWHr) $Rn)>;
  def : Pat<(i64 (round f16:$Rn)), (!cast<Instruction>(INST # UXHr) $Rn)>;
  }
  def : Pat<(i32 (round f32:$Rn)), (!cast<Instruction>(INST # UWSr) $Rn)>;
  def : Pat<(i64 (round f32:$Rn)), (!cast<Instruction>(INST # UXSr) $Rn)>;
  def : Pat<(i32 (round f64:$Rn)), (!cast<Instruction>(INST # UWDr) $Rn)>;
  def : Pat<(i64 (round f64:$Rn)), (!cast<Instruction>(INST # UXDr) $Rn)>;

  let Predicates = [HasFullFP16] in {
  def : Pat<(i32 (round (fmul f16:$Rn, fixedpoint_f16_i32:$scale))),
            (!cast<Instruction>(INST # SWHri) $Rn, $scale)>;
  def : Pat<(i64 (round (fmul f16:$Rn, fixedpoint_f16_i64:$scale))),
            (!cast<Instruction>(INST # SXHri) $Rn, $scale)>;
  }
  def : Pat<(i32 (round (fmul f32:$Rn, fixedpoint_f32_i32:$scale))),
            (!cast<Instruction>(INST # SWSri) $Rn, $scale)>;
  def : Pat<(i64 (round (fmul f32:$Rn, fixedpoint_f32_i64:$scale))),
            (!cast<Instruction>(INST # SXSri) $Rn, $scale)>;
  def : Pat<(i32 (round (fmul f64:$Rn, fixedpoint_f64_i32:$scale))),
            (!cast<Instruction>(INST # SWDri) $Rn, $scale)>;
  def : Pat<(i64 (round (fmul f64:$Rn, fixedpoint_f64_i64:$scale))),
            (!cast<Instruction>(INST # SXDri) $Rn, $scale)>;
}

defm : FPToIntegerIntPats<int_aarch64_neon_fcvtzs, "FCVTZS">;
defm : FPToIntegerIntPats<int_aarch64_neon_fcvtzu, "FCVTZU">;

multiclass FPToIntegerPats<SDNode to_int, SDNode to_int_sat, SDNode round, string INST> {
  def : Pat<(i32 (to_int (round f32:$Rn))),
            (!cast<Instruction>(INST # UWSr) f32:$Rn)>;
  def : Pat<(i64 (to_int (round f32:$Rn))),
            (!cast<Instruction>(INST # UXSr) f32:$Rn)>;
  def : Pat<(i32 (to_int (round f64:$Rn))),
            (!cast<Instruction>(INST # UWDr) f64:$Rn)>;
  def : Pat<(i64 (to_int (round f64:$Rn))),
            (!cast<Instruction>(INST # UXDr) f64:$Rn)>;

  // These instructions saturate like fp_to_[su]int_sat.
  let Predicates = [HasFullFP16] in {
  def : Pat<(i32 (to_int_sat (round f16:$Rn), i32)),
            (!cast<Instruction>(INST # UWHr) f16:$Rn)>;
  def : Pat<(i64 (to_int_sat (round f16:$Rn), i64)),
            (!cast<Instruction>(INST # UXHr) f16:$Rn)>;
  }
  def : Pat<(i32 (to_int_sat (round f32:$Rn), i32)),
            (!cast<Instruction>(INST # UWSr) f32:$Rn)>;
  def : Pat<(i64 (to_int_sat (round f32:$Rn), i64)),
            (!cast<Instruction>(INST # UXSr) f32:$Rn)>;
  def : Pat<(i32 (to_int_sat (round f64:$Rn), i32)),
            (!cast<Instruction>(INST # UWDr) f64:$Rn)>;
  def : Pat<(i64 (to_int_sat (round f64:$Rn), i64)),
            (!cast<Instruction>(INST # UXDr) f64:$Rn)>;
}

defm : FPToIntegerPats<fp_to_sint, fp_to_sint_sat, fceil,  "FCVTPS">;
defm : FPToIntegerPats<fp_to_uint, fp_to_uint_sat, fceil,  "FCVTPU">;
defm : FPToIntegerPats<fp_to_sint, fp_to_sint_sat, ffloor, "FCVTMS">;
defm : FPToIntegerPats<fp_to_uint, fp_to_uint_sat, ffloor, "FCVTMU">;
defm : FPToIntegerPats<fp_to_sint, fp_to_sint_sat, ftrunc, "FCVTZS">;
defm : FPToIntegerPats<fp_to_uint, fp_to_uint_sat, ftrunc, "FCVTZU">;
defm : FPToIntegerPats<fp_to_sint, fp_to_sint_sat, fround, "FCVTAS">;
defm : FPToIntegerPats<fp_to_uint, fp_to_uint_sat, fround, "FCVTAU">;



let Predicates = [HasFullFP16] in {
  def : Pat<(i32 (any_lround f16:$Rn)),
            (FCVTASUWHr f16:$Rn)>;
  def : Pat<(i64 (any_lround f16:$Rn)),
            (FCVTASUXHr f16:$Rn)>;
  def : Pat<(i64 (any_llround f16:$Rn)),
            (FCVTASUXHr f16:$Rn)>;
}
def : Pat<(i32 (any_lround f32:$Rn)),
          (FCVTASUWSr f32:$Rn)>;
def : Pat<(i32 (any_lround f64:$Rn)),
          (FCVTASUWDr f64:$Rn)>;
def : Pat<(i64 (any_lround f32:$Rn)),
          (FCVTASUXSr f32:$Rn)>;
def : Pat<(i64 (any_lround f64:$Rn)),
          (FCVTASUXDr f64:$Rn)>;
def : Pat<(i64 (any_llround f32:$Rn)),
          (FCVTASUXSr f32:$Rn)>;
def : Pat<(i64 (any_llround f64:$Rn)),
          (FCVTASUXDr f64:$Rn)>;

//===----------------------------------------------------------------------===//
// Scaled integer to floating point conversion instructions.
//===----------------------------------------------------------------------===//

defm SCVTF : IntegerToFP<0, "scvtf", any_sint_to_fp>;
defm UCVTF : IntegerToFP<1, "ucvtf", any_uint_to_fp>;

def : Pat<(f16 (fdiv (f16 (any_sint_to_fp (i32 GPR32:$Rn))), fixedpoint_f16_i32:$scale)),
          (SCVTFSWHri GPR32:$Rn, fixedpoint_f16_i32:$scale)>;
def : Pat<(f32 (fdiv (f32 (any_sint_to_fp (i32 GPR32:$Rn))), fixedpoint_f32_i32:$scale)),
          (SCVTFSWSri GPR32:$Rn, fixedpoint_f32_i32:$scale)>;
def : Pat<(f64 (fdiv (f64 (any_sint_to_fp (i32 GPR32:$Rn))), fixedpoint_f64_i32:$scale)),
          (SCVTFSWDri GPR32:$Rn, fixedpoint_f64_i32:$scale)>;

def : Pat<(f16 (fdiv (f16 (any_sint_to_fp (i64 GPR64:$Rn))), fixedpoint_f16_i64:$scale)),
          (SCVTFSXHri GPR64:$Rn, fixedpoint_f16_i64:$scale)>;
def : Pat<(f32 (fdiv (f32 (any_sint_to_fp (i64 GPR64:$Rn))), fixedpoint_f32_i64:$scale)),
          (SCVTFSXSri GPR64:$Rn, fixedpoint_f32_i64:$scale)>;
def : Pat<(f64 (fdiv (f64 (any_sint_to_fp (i64 GPR64:$Rn))), fixedpoint_f64_i64:$scale)),
          (SCVTFSXDri GPR64:$Rn, fixedpoint_f64_i64:$scale)>;

def : Pat<(f16 (fdiv (f16 (any_uint_to_fp (i64 GPR64:$Rn))), fixedpoint_f16_i64:$scale)),
          (UCVTFSXHri GPR64:$Rn, fixedpoint_f16_i64:$scale)>;
def : Pat<(f32 (fdiv (f32 (any_uint_to_fp (i64 GPR64:$Rn))), fixedpoint_f32_i64:$scale)),
          (UCVTFSXSri GPR64:$Rn, fixedpoint_f32_i64:$scale)>;
def : Pat<(f64 (fdiv (f64 (any_uint_to_fp (i64 GPR64:$Rn))), fixedpoint_f64_i64:$scale)),
          (UCVTFSXDri GPR64:$Rn, fixedpoint_f64_i64:$scale)>;

def : Pat<(f16 (fdiv (f16 (any_uint_to_fp (i32 GPR32:$Rn))), fixedpoint_f16_i32:$scale)),
          (UCVTFSWHri GPR32:$Rn, fixedpoint_f16_i32:$scale)>;
def : Pat<(f32 (fdiv (f32 (any_uint_to_fp (i32 GPR32:$Rn))), fixedpoint_f32_i32:$scale)),
          (UCVTFSWSri GPR32:$Rn, fixedpoint_f32_i32:$scale)>;
def : Pat<(f64 (fdiv (f64 (any_uint_to_fp (i32 GPR32:$Rn))), fixedpoint_f64_i32:$scale)),
          (UCVTFSWDri GPR32:$Rn, fixedpoint_f64_i32:$scale)>;

//===----------------------------------------------------------------------===//
// Unscaled integer to floating point conversion instruction.
//===----------------------------------------------------------------------===//

defm FMOV : UnscaledConversion<"fmov">;

// Add pseudo ops for FMOV 0 so we can mark them as isReMaterializable
let isReMaterializable = 1, isCodeGenOnly = 1, isAsCheapAsAMove = 1,
    Predicates = [HasFPARMv8] in {
def FMOVH0 : Pseudo<(outs FPR16:$Rd), (ins), [(set f16:$Rd, (fpimm0))]>,
    Sched<[WriteF]>;
def FMOVS0 : Pseudo<(outs FPR32:$Rd), (ins), [(set f32:$Rd, (fpimm0))]>,
    Sched<[WriteF]>;
def FMOVD0 : Pseudo<(outs FPR64:$Rd), (ins), [(set f64:$Rd, (fpimm0))]>,
    Sched<[WriteF]>;
}

// Similarly add aliases
def : InstAlias<"fmov $Rd, #0.0", (FMOVWHr FPR16:$Rd, WZR), 0>,
    Requires<[HasFullFP16]>;
let Predicates = [HasFPARMv8] in {
def : InstAlias<"fmov $Rd, #0.0", (FMOVWSr FPR32:$Rd, WZR), 0>;
def : InstAlias<"fmov $Rd, #0.0", (FMOVXDr FPR64:$Rd, XZR), 0>;
}

def : Pat<(bf16 fpimm0),
          (FMOVH0)>;

// Pattern for FP16 and BF16 immediates
let Predicates = [HasFullFP16] in {
  def : Pat<(f16 fpimm:$in),
            (FMOVWHr (MOVi32imm (bitcast_fpimm_to_i32 f16:$in)))>;

  def : Pat<(bf16 fpimm:$in),
            (FMOVWHr (MOVi32imm (bitcast_fpimm_to_i32 bf16:$in)))>;
}

//===----------------------------------------------------------------------===//
// Floating point conversion instruction.
//===----------------------------------------------------------------------===//

defm FCVT : FPConversion<"fcvt">;
// Helper to get bf16 into fp32.
def cvt_bf16_to_fp32 :
  OutPatFrag<(ops node:$Rn),
             (f32 (COPY_TO_REGCLASS
	         (i32 (UBFMWri
		   (i32 (COPY_TO_REGCLASS (INSERT_SUBREG (f32 (IMPLICIT_DEF)),
		                           node:$Rn, hsub), GPR32)),
	           (i64 (i32shift_a (i64 16))),
                   (i64 (i32shift_b (i64 16))))),
	         FPR32))>;
// Pattern for bf16 -> fp32.
def : Pat<(f32 (any_fpextend (bf16 FPR16:$Rn))),
          (cvt_bf16_to_fp32 FPR16:$Rn)>;
// Pattern for bf16 -> fp64.
def : Pat<(f64 (any_fpextend (bf16 FPR16:$Rn))),
          (FCVTDSr (f32 (cvt_bf16_to_fp32 FPR16:$Rn)))>;

//===----------------------------------------------------------------------===//
// Floating point single operand instructions.
//===----------------------------------------------------------------------===//

defm FABS   : SingleOperandFPDataNoException<0b0001, "fabs", fabs>;
defm FMOV   : SingleOperandFPDataNoException<0b0000, "fmov">;
defm FNEG   : SingleOperandFPDataNoException<0b0010, "fneg", fneg>;
defm FRINTA : SingleOperandFPData<0b1100, "frinta", any_fround>;
defm FRINTI : SingleOperandFPData<0b1111, "frinti", any_fnearbyint>;
defm FRINTM : SingleOperandFPData<0b1010, "frintm", any_ffloor>;
defm FRINTN : SingleOperandFPData<0b1000, "frintn", any_froundeven>;
defm FRINTP : SingleOperandFPData<0b1001, "frintp", any_fceil>;

defm FRINTX : SingleOperandFPData<0b1110, "frintx", any_frint>;
defm FRINTZ : SingleOperandFPData<0b1011, "frintz", any_ftrunc>;

let SchedRW = [WriteFDiv] in {
defm FSQRT  : SingleOperandFPData<0b0011, "fsqrt", any_fsqrt>;
}

let Predicates = [HasFRInt3264] in {
  defm FRINT32Z : FRIntNNT<0b00, "frint32z", int_aarch64_frint32z>;
  defm FRINT64Z : FRIntNNT<0b10, "frint64z", int_aarch64_frint64z>;
  defm FRINT32X : FRIntNNT<0b01, "frint32x", int_aarch64_frint32x>;
  defm FRINT64X : FRIntNNT<0b11, "frint64x", int_aarch64_frint64x>;
} // HasFRInt3264

// Pattern to convert 1x64 vector intrinsics to equivalent scalar instructions
def : Pat<(v1f64 (int_aarch64_neon_frint32z (v1f64 FPR64:$Rn))),
          (FRINT32ZDr FPR64:$Rn)>;
def : Pat<(v1f64 (int_aarch64_neon_frint64z (v1f64 FPR64:$Rn))),
          (FRINT64ZDr FPR64:$Rn)>;
def : Pat<(v1f64 (int_aarch64_neon_frint32x (v1f64 FPR64:$Rn))),
          (FRINT32XDr FPR64:$Rn)>;
def : Pat<(v1f64 (int_aarch64_neon_frint64x (v1f64 FPR64:$Rn))),
          (FRINT64XDr FPR64:$Rn)>;

// Emitting strict_lrint as two instructions is valid as any exceptions that
// occur will happen in exactly one of the instructions (e.g. if the input is
// not an integer the inexact exception will happen in the FRINTX but not then
// in the FCVTZS as the output of FRINTX is an integer).
let Predicates = [HasFullFP16] in {
  def : Pat<(i32 (any_lrint f16:$Rn)),
            (FCVTZSUWHr (FRINTXHr f16:$Rn))>;
  def : Pat<(i64 (any_lrint f16:$Rn)),
            (FCVTZSUXHr (FRINTXHr f16:$Rn))>;
  def : Pat<(i64 (any_llrint f16:$Rn)),
            (FCVTZSUXHr (FRINTXHr f16:$Rn))>;
}
def : Pat<(i32 (any_lrint f32:$Rn)),
          (FCVTZSUWSr (FRINTXSr f32:$Rn))>;
def : Pat<(i32 (any_lrint f64:$Rn)),
          (FCVTZSUWDr (FRINTXDr f64:$Rn))>;
def : Pat<(i64 (any_lrint f32:$Rn)),
          (FCVTZSUXSr (FRINTXSr f32:$Rn))>;
def : Pat<(i64 (any_lrint f64:$Rn)),
          (FCVTZSUXDr (FRINTXDr f64:$Rn))>;
def : Pat<(i64 (any_llrint f32:$Rn)),
          (FCVTZSUXSr (FRINTXSr f32:$Rn))>;
def : Pat<(i64 (any_llrint f64:$Rn)),
          (FCVTZSUXDr (FRINTXDr f64:$Rn))>;

//===----------------------------------------------------------------------===//
// Floating point two operand instructions.
//===----------------------------------------------------------------------===//

defm FADD   : TwoOperandFPData<0b0010, "fadd", any_fadd>;
let SchedRW = [WriteFDiv] in {
defm FDIV   : TwoOperandFPData<0b0001, "fdiv", any_fdiv>;
}
defm FMAXNM : TwoOperandFPData<0b0110, "fmaxnm", any_fmaxnum>;
defm FMAX   : TwoOperandFPData<0b0100, "fmax", any_fmaximum>;
defm FMINNM : TwoOperandFPData<0b0111, "fminnm", any_fminnum>;
defm FMIN   : TwoOperandFPData<0b0101, "fmin", any_fminimum>;
let SchedRW = [WriteFMul] in {
defm FMUL   : TwoOperandFPData<0b0000, "fmul", any_fmul>;
defm FNMUL  : TwoOperandFPDataNeg<0b1000, "fnmul", any_fmul>;
}
defm FSUB   : TwoOperandFPData<0b0011, "fsub", any_fsub>;

multiclass FMULScalarFromIndexedLane0Patterns<string inst,
                                              string inst_f16_suffix,
                                              string inst_f32_suffix,
                                              string inst_f64_suffix,
                                              SDPatternOperator OpNode,
                                              list<Predicate> preds = []> {
  let Predicates = !listconcat(preds, [HasFullFP16]) in {
  def : Pat<(f16 (OpNode (f16 FPR16:$Rn),
                         (f16 (vector_extract (v8f16 V128:$Rm), (i64 0))))),
            (!cast<Instruction>(inst # inst_f16_suffix)
              FPR16:$Rn, (f16 (EXTRACT_SUBREG V128:$Rm, hsub)))>;
  }
  let Predicates = preds in {
  def : Pat<(f32 (OpNode (f32 FPR32:$Rn),
                         (f32 (vector_extract (v4f32 V128:$Rm), (i64 0))))),
            (!cast<Instruction>(inst # inst_f32_suffix)
              FPR32:$Rn, (EXTRACT_SUBREG V128:$Rm, ssub))>;
  def : Pat<(f64 (OpNode (f64 FPR64:$Rn),
                         (f64 (vector_extract (v2f64 V128:$Rm), (i64 0))))),
            (!cast<Instruction>(inst # inst_f64_suffix)
              FPR64:$Rn, (EXTRACT_SUBREG V128:$Rm, dsub))>;
  }
}

defm : FMULScalarFromIndexedLane0Patterns<"FMUL", "Hrr", "Srr", "Drr",
                                          any_fmul>;

// Match reassociated forms of FNMUL.
def : Pat<(fmul (fneg FPR16:$a), (f16 FPR16:$b)),
          (FNMULHrr FPR16:$a, FPR16:$b)>,
          Requires<[HasFullFP16]>;
def : Pat<(fmul (fneg FPR32:$a), (f32 FPR32:$b)),
          (FNMULSrr FPR32:$a, FPR32:$b)>;
def : Pat<(fmul (fneg FPR64:$a), (f64 FPR64:$b)),
          (FNMULDrr FPR64:$a, FPR64:$b)>;

def : Pat<(v1f64 (fmaximum (v1f64 FPR64:$Rn), (v1f64 FPR64:$Rm))),
          (FMAXDrr FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(v1f64 (fminimum (v1f64 FPR64:$Rn), (v1f64 FPR64:$Rm))),
          (FMINDrr FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(v1f64 (fmaxnum (v1f64 FPR64:$Rn), (v1f64 FPR64:$Rm))),
          (FMAXNMDrr FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(v1f64 (fminnum (v1f64 FPR64:$Rn), (v1f64 FPR64:$Rm))),
          (FMINNMDrr FPR64:$Rn, FPR64:$Rm)>;

//===----------------------------------------------------------------------===//
// Floating point three operand instructions.
//===----------------------------------------------------------------------===//

defm FMADD  : ThreeOperandFPData<0, 0, "fmadd", any_fma>;
defm FMSUB  : ThreeOperandFPData<0, 1, "fmsub",
     TriOpFrag<(any_fma node:$LHS, (fneg node:$MHS), node:$RHS)> >;
defm FNMADD : ThreeOperandFPData<1, 0, "fnmadd",
     TriOpFrag<(fneg (any_fma node:$LHS, node:$MHS, node:$RHS))> >;
defm FNMSUB : ThreeOperandFPData<1, 1, "fnmsub",
     TriOpFrag<(any_fma node:$LHS, node:$MHS, (fneg node:$RHS))> >;

// The following def pats catch the case where the LHS of an FMA is negated.
// The TriOpFrag above catches the case where the middle operand is negated.

// N.b. FMSUB etc have the accumulator at the *end* of (outs), unlike
// the NEON variant.

// Here we handle first -(a + b*c) for FNMADD:

let Predicates = [HasNEON, HasFullFP16] in
def : Pat<(f16 (fma (fneg FPR16:$Rn), FPR16:$Rm, FPR16:$Ra)),
          (FMSUBHrrr FPR16:$Rn, FPR16:$Rm, FPR16:$Ra)>;

def : Pat<(f32 (fma (fneg FPR32:$Rn), FPR32:$Rm, FPR32:$Ra)),
          (FMSUBSrrr FPR32:$Rn, FPR32:$Rm, FPR32:$Ra)>;

def : Pat<(f64 (fma (fneg FPR64:$Rn), FPR64:$Rm, FPR64:$Ra)),
          (FMSUBDrrr FPR64:$Rn, FPR64:$Rm, FPR64:$Ra)>;

// Now it's time for "(-a) + (-b)*c"

let Predicates = [HasNEON, HasFullFP16] in
def : Pat<(f16 (fma (fneg FPR16:$Rn), FPR16:$Rm, (fneg FPR16:$Ra))),
          (FNMADDHrrr FPR16:$Rn, FPR16:$Rm, FPR16:$Ra)>;

def : Pat<(f32 (fma (fneg FPR32:$Rn), FPR32:$Rm, (fneg FPR32:$Ra))),
          (FNMADDSrrr FPR32:$Rn, FPR32:$Rm, FPR32:$Ra)>;

def : Pat<(f64 (fma (fneg FPR64:$Rn), FPR64:$Rm, (fneg FPR64:$Ra))),
          (FNMADDDrrr FPR64:$Rn, FPR64:$Rm, FPR64:$Ra)>;

//===----------------------------------------------------------------------===//
// Floating point comparison instructions.
//===----------------------------------------------------------------------===//

defm FCMPE : FPComparison<1, "fcmpe", AArch64strict_fcmpe>;
defm FCMP  : FPComparison<0, "fcmp", AArch64any_fcmp>;

//===----------------------------------------------------------------------===//
// Floating point conditional comparison instructions.
//===----------------------------------------------------------------------===//

defm FCCMPE : FPCondComparison<1, "fccmpe">;
defm FCCMP  : FPCondComparison<0, "fccmp", AArch64fccmp>;

//===----------------------------------------------------------------------===//
// Floating point conditional select instruction.
//===----------------------------------------------------------------------===//

defm FCSEL : FPCondSelect<"fcsel">;

let Predicates = [HasFullFP16] in
def : Pat<(bf16 (AArch64csel (bf16 FPR16:$Rn), (bf16 FPR16:$Rm), (i32 imm:$cond), NZCV)),
          (FCSELHrrr FPR16:$Rn, FPR16:$Rm, imm:$cond)>;

// CSEL instructions providing f128 types need to be handled by a
// pseudo-instruction since the eventual code will need to introduce basic
// blocks and control flow.
let Predicates = [HasFPARMv8] in
def F128CSEL : Pseudo<(outs FPR128:$Rd),
                      (ins FPR128:$Rn, FPR128:$Rm, ccode:$cond),
                      [(set (f128 FPR128:$Rd),
                            (AArch64csel FPR128:$Rn, FPR128:$Rm,
                                       (i32 imm:$cond), NZCV))]> {
  let Uses = [NZCV];
  let usesCustomInserter = 1;
  let hasNoSchedulingInfo = 1;
}

//===----------------------------------------------------------------------===//
// Instructions used for emitting unwind opcodes on ARM64 Windows.
//===----------------------------------------------------------------------===//
let isPseudo = 1 in {
  def SEH_StackAlloc : Pseudo<(outs), (ins i32imm:$size), []>, Sched<[]>;
  def SEH_SaveFPLR : Pseudo<(outs), (ins i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveFPLR_X : Pseudo<(outs), (ins i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveReg : Pseudo<(outs), (ins i32imm:$reg, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveReg_X : Pseudo<(outs), (ins i32imm:$reg, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveRegP : Pseudo<(outs), (ins i32imm:$reg0, i32imm:$reg1, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveRegP_X : Pseudo<(outs), (ins i32imm:$reg0, i32imm:$reg1, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveFReg : Pseudo<(outs), (ins i32imm:$reg, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveFReg_X :  Pseudo<(outs), (ins i32imm:$reg, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveFRegP : Pseudo<(outs), (ins i32imm:$reg0, i32imm:$reg1, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveFRegP_X : Pseudo<(outs), (ins i32imm:$reg0, i32imm:$reg1, i32imm:$offs), []>, Sched<[]>;
  def SEH_SetFP : Pseudo<(outs), (ins), []>, Sched<[]>;
  def SEH_AddFP : Pseudo<(outs), (ins i32imm:$offs), []>, Sched<[]>;
  def SEH_Nop : Pseudo<(outs), (ins), []>, Sched<[]>;
  def SEH_PrologEnd : Pseudo<(outs), (ins), []>, Sched<[]>;
  def SEH_EpilogStart : Pseudo<(outs), (ins), []>, Sched<[]>;
  def SEH_EpilogEnd : Pseudo<(outs), (ins), []>, Sched<[]>;
  def SEH_PACSignLR : Pseudo<(outs), (ins), []>, Sched<[]>;
  def SEH_SaveAnyRegQP : Pseudo<(outs), (ins i32imm:$reg0, i32imm:$reg1, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveAnyRegQPX : Pseudo<(outs), (ins i32imm:$reg0, i32imm:$reg1, i32imm:$offs), []>, Sched<[]>;
}

// Pseudo instructions for Windows EH
//===----------------------------------------------------------------------===//
let isTerminator = 1, hasSideEffects = 1, isBarrier = 1, hasCtrlDep = 1,
    isCodeGenOnly = 1, isReturn = 1, isEHScopeReturn = 1, isPseudo = 1 in {
   def CLEANUPRET : Pseudo<(outs), (ins), [(cleanupret bb)]>, Sched<[]>;
   let usesCustomInserter = 1 in
     def CATCHRET : Pseudo<(outs), (ins am_brcond:$dst, am_brcond:$src), [(catchret bb:$dst, bb:$src)]>,
                    Sched<[]>;
}

// Pseudo instructions for homogeneous prolog/epilog
let isPseudo = 1 in {
  // Save CSRs in order, {FPOffset}
  def HOM_Prolog : Pseudo<(outs), (ins variable_ops), []>, Sched<[]>;
  // Restore CSRs in order
  def HOM_Epilog : Pseudo<(outs), (ins variable_ops), []>, Sched<[]>;
}

//===----------------------------------------------------------------------===//
// Floating point immediate move.
//===----------------------------------------------------------------------===//

let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
defm FMOV : FPMoveImmediate<"fmov">;
}

let Predicates = [HasFullFP16] in {
  def : Pat<(bf16 fpimmbf16:$in),
            (FMOVHi (fpimm16XForm bf16:$in))>;
}

//===----------------------------------------------------------------------===//
// Advanced SIMD two vector instructions.
//===----------------------------------------------------------------------===//

defm UABDL   : SIMDLongThreeVectorBHSabdl<1, 0b0111, "uabdl",
                                          AArch64uabd>;
// Match UABDL in log2-shuffle patterns.
def : Pat<(abs (v8i16 (sub (zext (v8i8 V64:$opA)),
                           (zext (v8i8 V64:$opB))))),
          (UABDLv8i8_v8i16 V64:$opA, V64:$opB)>;
def : Pat<(abs (v8i16 (sub (zext (extract_high_v16i8 (v16i8 V128:$opA))),
                           (zext (extract_high_v16i8 (v16i8 V128:$opB)))))),
          (UABDLv16i8_v8i16 V128:$opA, V128:$opB)>;
def : Pat<(abs (v4i32 (sub (zext (v4i16 V64:$opA)),
                           (zext (v4i16 V64:$opB))))),
          (UABDLv4i16_v4i32 V64:$opA, V64:$opB)>;
def : Pat<(abs (v4i32 (sub (zext (extract_high_v8i16 (v8i16 V128:$opA))),
                           (zext (extract_high_v8i16 (v8i16 V128:$opB)))))),
          (UABDLv8i16_v4i32 V128:$opA, V128:$opB)>;
def : Pat<(abs (v2i64 (sub (zext (v2i32 V64:$opA)),
                           (zext (v2i32 V64:$opB))))),
          (UABDLv2i32_v2i64 V64:$opA, V64:$opB)>;
def : Pat<(abs (v2i64 (sub (zext (extract_high_v4i32 (v4i32 V128:$opA))),
                           (zext (extract_high_v4i32 (v4i32 V128:$opB)))))),
          (UABDLv4i32_v2i64 V128:$opA, V128:$opB)>;

defm ABS    : SIMDTwoVectorBHSD<0, 0b01011, "abs", abs>;
defm CLS    : SIMDTwoVectorBHS<0, 0b00100, "cls", int_aarch64_neon_cls>;
defm CLZ    : SIMDTwoVectorBHS<1, 0b00100, "clz", ctlz>;
defm CMEQ   : SIMDCmpTwoVector<0, 0b01001, "cmeq", AArch64cmeqz>;
defm CMGE   : SIMDCmpTwoVector<1, 0b01000, "cmge", AArch64cmgez>;
defm CMGT   : SIMDCmpTwoVector<0, 0b01000, "cmgt", AArch64cmgtz>;
defm CMLE   : SIMDCmpTwoVector<1, 0b01001, "cmle", AArch64cmlez>;
defm CMLT   : SIMDCmpTwoVector<0, 0b01010, "cmlt", AArch64cmltz>;
defm CNT    : SIMDTwoVectorB<0, 0b00, 0b00101, "cnt", ctpop>;
defm FABS   : SIMDTwoVectorFPNoException<0, 1, 0b01111, "fabs", fabs>;

def : Pat<(v8i8 (AArch64vashr (v8i8 V64:$Rn), (i32 7))),
          (CMLTv8i8rz V64:$Rn)>;
def : Pat<(v4i16 (AArch64vashr (v4i16 V64:$Rn), (i32 15))),
          (CMLTv4i16rz V64:$Rn)>;
def : Pat<(v2i32 (AArch64vashr (v2i32 V64:$Rn), (i32 31))),
          (CMLTv2i32rz V64:$Rn)>;
def : Pat<(v16i8 (AArch64vashr (v16i8 V128:$Rn), (i32 7))),
          (CMLTv16i8rz V128:$Rn)>;
def : Pat<(v8i16 (AArch64vashr (v8i16 V128:$Rn), (i32 15))),
          (CMLTv8i16rz V128:$Rn)>;
def : Pat<(v4i32 (AArch64vashr (v4i32 V128:$Rn), (i32 31))),
          (CMLTv4i32rz V128:$Rn)>;
def : Pat<(v2i64 (AArch64vashr (v2i64 V128:$Rn), (i32 63))),
          (CMLTv2i64rz V128:$Rn)>;

defm FCMEQ  : SIMDFPCmpTwoVector<0, 1, 0b01101, "fcmeq", AArch64fcmeqz>;
defm FCMGE  : SIMDFPCmpTwoVector<1, 1, 0b01100, "fcmge", AArch64fcmgez>;
defm FCMGT  : SIMDFPCmpTwoVector<0, 1, 0b01100, "fcmgt", AArch64fcmgtz>;
defm FCMLE  : SIMDFPCmpTwoVector<1, 1, 0b01101, "fcmle", AArch64fcmlez>;
defm FCMLT  : SIMDFPCmpTwoVector<0, 1, 0b01110, "fcmlt", AArch64fcmltz>;
defm FCVTAS : SIMDTwoVectorFPToInt<0,0,0b11100, "fcvtas",int_aarch64_neon_fcvtas>;
defm FCVTAU : SIMDTwoVectorFPToInt<1,0,0b11100, "fcvtau",int_aarch64_neon_fcvtau>;
defm FCVTL  : SIMDFPWidenTwoVector<0, 0, 0b10111, "fcvtl">;
def : Pat<(v4f32 (int_aarch64_neon_vcvthf2fp (v4i16 V64:$Rn))),
          (FCVTLv4i16 V64:$Rn)>;
def : Pat<(v4f32 (int_aarch64_neon_vcvthf2fp (extract_subvector (v8i16 V128:$Rn),
                                                                (i64 4)))),
          (FCVTLv8i16 V128:$Rn)>;
def : Pat<(v2f64 (any_fpextend (v2f32 V64:$Rn))),
          (FCVTLv2i32 V64:$Rn)>;
def : Pat<(v2f64 (any_fpextend (v2f32 (extract_high_v4f32 (v4f32 V128:$Rn))))),
          (FCVTLv4i32 V128:$Rn)>;
def : Pat<(v4f32 (any_fpextend (v4f16 V64:$Rn))),
          (FCVTLv4i16 V64:$Rn)>;
def : Pat<(v4f32 (any_fpextend (v4f16 (extract_high_v8f16 (v8f16 V128:$Rn))))),
          (FCVTLv8i16 V128:$Rn)>;

defm FCVTMS : SIMDTwoVectorFPToInt<0,0,0b11011, "fcvtms",int_aarch64_neon_fcvtms>;
defm FCVTMU : SIMDTwoVectorFPToInt<1,0,0b11011, "fcvtmu",int_aarch64_neon_fcvtmu>;
defm FCVTNS : SIMDTwoVectorFPToInt<0,0,0b11010, "fcvtns",int_aarch64_neon_fcvtns>;
defm FCVTNU : SIMDTwoVectorFPToInt<1,0,0b11010, "fcvtnu",int_aarch64_neon_fcvtnu>;
defm FCVTN  : SIMDFPNarrowTwoVector<0, 0, 0b10110, "fcvtn">;
def : Pat<(v4i16 (int_aarch64_neon_vcvtfp2hf (v4f32 V128:$Rn))),
          (FCVTNv4i16 V128:$Rn)>;
def : Pat<(concat_vectors V64:$Rd,
                          (v4i16 (int_aarch64_neon_vcvtfp2hf (v4f32 V128:$Rn)))),
          (FCVTNv8i16 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub), V128:$Rn)>;
def : Pat<(v2f32 (any_fpround (v2f64 V128:$Rn))),
          (FCVTNv2i32 V128:$Rn)>;
def : Pat<(v4f16 (any_fpround (v4f32 V128:$Rn))),
          (FCVTNv4i16 V128:$Rn)>;
def : Pat<(concat_vectors V64:$Rd, (v2f32 (any_fpround (v2f64 V128:$Rn)))),
          (FCVTNv4i32 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub), V128:$Rn)>;
def : Pat<(concat_vectors V64:$Rd, (v4f16 (any_fpround (v4f32 V128:$Rn)))),
          (FCVTNv8i16 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub), V128:$Rn)>;
defm FCVTPS : SIMDTwoVectorFPToInt<0,1,0b11010, "fcvtps",int_aarch64_neon_fcvtps>;
defm FCVTPU : SIMDTwoVectorFPToInt<1,1,0b11010, "fcvtpu",int_aarch64_neon_fcvtpu>;
defm FCVTXN : SIMDFPInexactCvtTwoVector<1, 0, 0b10110, "fcvtxn",
                                        AArch64fcvtxnv>;
defm FCVTZS : SIMDTwoVectorFPToInt<0, 1, 0b11011, "fcvtzs", any_fp_to_sint>;
defm FCVTZU : SIMDTwoVectorFPToInt<1, 1, 0b11011, "fcvtzu", any_fp_to_uint>;

// AArch64's FCVT instructions saturate when out of range.
multiclass SIMDTwoVectorFPToIntSatPats<SDNode to_int_sat, string INST> {
  let Predicates = [HasFullFP16] in {
  def : Pat<(v4i16 (to_int_sat v4f16:$Rn, i16)),
            (!cast<Instruction>(INST # v4f16) v4f16:$Rn)>;
  def : Pat<(v8i16 (to_int_sat v8f16:$Rn, i16)),
            (!cast<Instruction>(INST # v8f16) v8f16:$Rn)>;
  }
  def : Pat<(v2i32 (to_int_sat v2f32:$Rn, i32)),
            (!cast<Instruction>(INST # v2f32) v2f32:$Rn)>;
  def : Pat<(v4i32 (to_int_sat v4f32:$Rn, i32)),
            (!cast<Instruction>(INST # v4f32) v4f32:$Rn)>;
  def : Pat<(v2i64 (to_int_sat v2f64:$Rn, i64)),
            (!cast<Instruction>(INST # v2f64) v2f64:$Rn)>;
}
defm : SIMDTwoVectorFPToIntSatPats<fp_to_sint_sat, "FCVTZS">;
defm : SIMDTwoVectorFPToIntSatPats<fp_to_uint_sat, "FCVTZU">;

def : Pat<(v4i16 (int_aarch64_neon_fcvtzs v4f16:$Rn)), (FCVTZSv4f16 $Rn)>;
def : Pat<(v8i16 (int_aarch64_neon_fcvtzs v8f16:$Rn)), (FCVTZSv8f16 $Rn)>;
def : Pat<(v2i32 (int_aarch64_neon_fcvtzs v2f32:$Rn)), (FCVTZSv2f32 $Rn)>;
def : Pat<(v4i32 (int_aarch64_neon_fcvtzs v4f32:$Rn)), (FCVTZSv4f32 $Rn)>;
def : Pat<(v2i64 (int_aarch64_neon_fcvtzs v2f64:$Rn)), (FCVTZSv2f64 $Rn)>;

def : Pat<(v4i16 (int_aarch64_neon_fcvtzu v4f16:$Rn)), (FCVTZUv4f16 $Rn)>;
def : Pat<(v8i16 (int_aarch64_neon_fcvtzu v8f16:$Rn)), (FCVTZUv8f16 $Rn)>;
def : Pat<(v2i32 (int_aarch64_neon_fcvtzu v2f32:$Rn)), (FCVTZUv2f32 $Rn)>;
def : Pat<(v4i32 (int_aarch64_neon_fcvtzu v4f32:$Rn)), (FCVTZUv4f32 $Rn)>;
def : Pat<(v2i64 (int_aarch64_neon_fcvtzu v2f64:$Rn)), (FCVTZUv2f64 $Rn)>;

defm FNEG   : SIMDTwoVectorFPNoException<1, 1, 0b01111, "fneg", fneg>;
defm FRECPE : SIMDTwoVectorFP<0, 1, 0b11101, "frecpe", int_aarch64_neon_frecpe>;
defm FRINTA : SIMDTwoVectorFP<1, 0, 0b11000, "frinta", any_fround>;
defm FRINTI : SIMDTwoVectorFP<1, 1, 0b11001, "frinti", any_fnearbyint>;
defm FRINTM : SIMDTwoVectorFP<0, 0, 0b11001, "frintm", any_ffloor>;
defm FRINTN : SIMDTwoVectorFP<0, 0, 0b11000, "frintn", any_froundeven>;
defm FRINTP : SIMDTwoVectorFP<0, 1, 0b11000, "frintp", any_fceil>;
defm FRINTX : SIMDTwoVectorFP<1, 0, 0b11001, "frintx", any_frint>;
defm FRINTZ : SIMDTwoVectorFP<0, 1, 0b11001, "frintz", any_ftrunc>;

let Predicates = [HasFRInt3264] in {
  defm FRINT32Z : FRIntNNTVector<0, 0, "frint32z", int_aarch64_neon_frint32z>;
  defm FRINT64Z : FRIntNNTVector<0, 1, "frint64z", int_aarch64_neon_frint64z>;
  defm FRINT32X : FRIntNNTVector<1, 0, "frint32x", int_aarch64_neon_frint32x>;
  defm FRINT64X : FRIntNNTVector<1, 1, "frint64x", int_aarch64_neon_frint64x>;
} // HasFRInt3264

defm FRSQRTE: SIMDTwoVectorFP<1, 1, 0b11101, "frsqrte", int_aarch64_neon_frsqrte>;
defm FSQRT  : SIMDTwoVectorFP<1, 1, 0b11111, "fsqrt", any_fsqrt>;
defm NEG    : SIMDTwoVectorBHSD<1, 0b01011, "neg",
                               UnOpFrag<(sub immAllZerosV, node:$LHS)> >;
defm NOT    : SIMDTwoVectorB<1, 0b00, 0b00101, "not", vnot>;
// Aliases for MVN -> NOT.
let Predicates = [HasNEON] in {
def : InstAlias<"mvn{ $Vd.8b, $Vn.8b|.8b $Vd, $Vn}",
                (NOTv8i8 V64:$Vd, V64:$Vn)>;
def : InstAlias<"mvn{ $Vd.16b, $Vn.16b|.16b $Vd, $Vn}",
                (NOTv16i8 V128:$Vd, V128:$Vn)>;
}

def : Pat<(vnot (v4i16 V64:$Rn)),  (NOTv8i8  V64:$Rn)>;
def : Pat<(vnot (v8i16 V128:$Rn)), (NOTv16i8 V128:$Rn)>;
def : Pat<(vnot (v2i32 V64:$Rn)),  (NOTv8i8  V64:$Rn)>;
def : Pat<(vnot (v4i32 V128:$Rn)), (NOTv16i8 V128:$Rn)>;
def : Pat<(vnot (v1i64 V64:$Rn)),  (NOTv8i8  V64:$Rn)>;
def : Pat<(vnot (v2i64 V128:$Rn)), (NOTv16i8 V128:$Rn)>;

defm RBIT   : SIMDTwoVectorB<1, 0b01, 0b00101, "rbit", bitreverse>;
defm REV16  : SIMDTwoVectorB<0, 0b00, 0b00001, "rev16", AArch64rev16>;
defm REV32  : SIMDTwoVectorBH<1, 0b00000, "rev32", AArch64rev32>;
defm REV64  : SIMDTwoVectorBHS<0, 0b00000, "rev64", AArch64rev64>;
defm SADALP : SIMDLongTwoVectorTied<0, 0b00110, "sadalp",
       BinOpFrag<(add node:$LHS, (AArch64saddlp node:$RHS))> >;
defm SADDLP : SIMDLongTwoVector<0, 0b00010, "saddlp", AArch64saddlp>;
defm SCVTF  : SIMDTwoVectorIntToFP<0, 0, 0b11101, "scvtf", any_sint_to_fp>;
defm SHLL   : SIMDVectorLShiftLongBySizeBHS;
defm SQABS  : SIMDTwoVectorBHSD<0, 0b00111, "sqabs", int_aarch64_neon_sqabs>;
defm SQNEG  : SIMDTwoVectorBHSD<1, 0b00111, "sqneg", int_aarch64_neon_sqneg>;
defm SQXTN  : SIMDMixedTwoVector<0, 0b10100, "sqxtn", int_aarch64_neon_sqxtn>;
defm SQXTUN : SIMDMixedTwoVector<1, 0b10010, "sqxtun", int_aarch64_neon_sqxtun>;
defm SUQADD : SIMDTwoVectorBHSDTied<0, 0b00011, "suqadd",int_aarch64_neon_suqadd>;
defm UADALP : SIMDLongTwoVectorTied<1, 0b00110, "uadalp",
       BinOpFrag<(add node:$LHS, (AArch64uaddlp node:$RHS))> >;
defm UADDLP : SIMDLongTwoVector<1, 0b00010, "uaddlp", AArch64uaddlp>;
defm UCVTF  : SIMDTwoVectorIntToFP<1, 0, 0b11101, "ucvtf", any_uint_to_fp>;
defm UQXTN  : SIMDMixedTwoVector<1, 0b10100, "uqxtn", int_aarch64_neon_uqxtn>;
defm URECPE : SIMDTwoVectorS<0, 1, 0b11100, "urecpe", int_aarch64_neon_urecpe>;
defm URSQRTE: SIMDTwoVectorS<1, 1, 0b11100, "ursqrte", int_aarch64_neon_ursqrte>;
defm USQADD : SIMDTwoVectorBHSDTied<1, 0b00011, "usqadd",int_aarch64_neon_usqadd>;
defm XTN    : SIMDMixedTwoVector<0, 0b10010, "xtn", trunc>;

def : Pat<(v4f16  (AArch64rev32 V64:$Rn)),  (REV32v4i16 V64:$Rn)>;
def : Pat<(v4f16  (AArch64rev64 V64:$Rn)),  (REV64v4i16 V64:$Rn)>;
def : Pat<(v4bf16 (AArch64rev32 V64:$Rn)),  (REV32v4i16 V64:$Rn)>;
def : Pat<(v4bf16 (AArch64rev64 V64:$Rn)),  (REV64v4i16 V64:$Rn)>;
def : Pat<(v8f16  (AArch64rev32 V128:$Rn)), (REV32v8i16 V128:$Rn)>;
def : Pat<(v8f16  (AArch64rev64 V128:$Rn)), (REV64v8i16 V128:$Rn)>;
def : Pat<(v8bf16 (AArch64rev32 V128:$Rn)), (REV32v8i16 V128:$Rn)>;
def : Pat<(v8bf16 (AArch64rev64 V128:$Rn)), (REV64v8i16 V128:$Rn)>;
def : Pat<(v2f32  (AArch64rev64 V64:$Rn)),  (REV64v2i32 V64:$Rn)>;
def : Pat<(v4f32  (AArch64rev64 V128:$Rn)), (REV64v4i32 V128:$Rn)>;

// Patterns for vector long shift (by element width). These need to match all
// three of zext, sext and anyext so it's easier to pull the patterns out of the
// definition.
multiclass SIMDVectorLShiftLongBySizeBHSPats<SDPatternOperator ext> {
  def : Pat<(AArch64vshl (v8i16 (ext (v8i8 V64:$Rn))), (i32 8)),
            (SHLLv8i8 V64:$Rn)>;
  def : Pat<(AArch64vshl (v8i16 (ext (extract_high_v16i8 (v16i8 V128:$Rn)))), (i32 8)),
            (SHLLv16i8 V128:$Rn)>;
  def : Pat<(AArch64vshl (v4i32 (ext (v4i16 V64:$Rn))), (i32 16)),
            (SHLLv4i16 V64:$Rn)>;
  def : Pat<(AArch64vshl (v4i32 (ext (extract_high_v8i16 (v8i16 V128:$Rn)))), (i32 16)),
            (SHLLv8i16 V128:$Rn)>;
  def : Pat<(AArch64vshl (v2i64 (ext (v2i32 V64:$Rn))), (i32 32)),
            (SHLLv2i32 V64:$Rn)>;
  def : Pat<(AArch64vshl (v2i64 (ext (extract_high_v4i32 (v4i32 V128:$Rn)))), (i32 32)),
            (SHLLv4i32 V128:$Rn)>;
}

defm : SIMDVectorLShiftLongBySizeBHSPats<anyext>;
defm : SIMDVectorLShiftLongBySizeBHSPats<zext>;
defm : SIMDVectorLShiftLongBySizeBHSPats<sext>;

// Constant vector values, used in the S/UQXTN patterns below.
def VImmFF:   PatLeaf<(AArch64NvCast (v2i64 (AArch64movi_edit (i32 85))))>;
def VImmFFFF: PatLeaf<(AArch64NvCast (v2i64 (AArch64movi_edit (i32 51))))>;
def VImm7F:   PatLeaf<(AArch64movi_shift (i32 127), (i32 0))>;
def VImm80:   PatLeaf<(AArch64mvni_shift (i32 127), (i32 0))>;
def VImm7FFF: PatLeaf<(AArch64movi_msl (i32 127), (i32 264))>;
def VImm8000: PatLeaf<(AArch64mvni_msl (i32 127), (i32 264))>;

// trunc(umin(X, 255)) -> UQXTRN v8i8
def : Pat<(v8i8 (trunc (umin (v8i16 V128:$Vn), (v8i16 VImmFF)))),
          (UQXTNv8i8 V128:$Vn)>;
// trunc(umin(X, 65535)) -> UQXTRN v4i16
def : Pat<(v4i16 (trunc (umin (v4i32 V128:$Vn), (v4i32 VImmFFFF)))),
          (UQXTNv4i16 V128:$Vn)>;
// trunc(smin(smax(X, -128), 128)) -> SQXTRN
//  with reversed min/max
def : Pat<(v8i8 (trunc (smin (smax (v8i16 V128:$Vn), (v8i16 VImm80)),
                             (v8i16 VImm7F)))),
          (SQXTNv8i8 V128:$Vn)>;
def : Pat<(v8i8 (trunc (smax (smin (v8i16 V128:$Vn), (v8i16 VImm7F)),
                             (v8i16 VImm80)))),
          (SQXTNv8i8 V128:$Vn)>;
// trunc(smin(smax(X, -32768), 32767)) -> SQXTRN
//  with reversed min/max
def : Pat<(v4i16 (trunc (smin (smax (v4i32 V128:$Vn), (v4i32 VImm8000)),
                              (v4i32 VImm7FFF)))),
          (SQXTNv4i16 V128:$Vn)>;
def : Pat<(v4i16 (trunc (smax (smin (v4i32 V128:$Vn), (v4i32 VImm7FFF)),
                              (v4i32 VImm8000)))),
          (SQXTNv4i16 V128:$Vn)>;

// concat_vectors(Vd, trunc(umin(X, 255))) -> UQXTRN(Vd, Vn)
def : Pat<(v16i8 (concat_vectors
                 (v8i8 V64:$Vd),
                 (v8i8 (trunc (umin (v8i16 V128:$Vn), (v8i16 VImmFF)))))),
          (UQXTNv16i8 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Vd, dsub), V128:$Vn)>;
// concat_vectors(Vd, trunc(umin(X, 65535))) -> UQXTRN(Vd, Vn)
def : Pat<(v8i16 (concat_vectors
                 (v4i16 V64:$Vd),
                 (v4i16 (trunc (umin (v4i32 V128:$Vn), (v4i32 VImmFFFF)))))),
          (UQXTNv8i16 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Vd, dsub), V128:$Vn)>;

// concat_vectors(Vd, trunc(smin(smax Vm, -128), 127) ~> SQXTN2(Vd, Vn)
// with reversed min/max
def : Pat<(v16i8 (concat_vectors
                 (v8i8 V64:$Vd),
                 (v8i8 (trunc (smin (smax (v8i16 V128:$Vn), (v8i16 VImm80)),
                                          (v8i16 VImm7F)))))),
          (SQXTNv16i8 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Vd, dsub), V128:$Vn)>;
def : Pat<(v16i8 (concat_vectors
                 (v8i8 V64:$Vd),
                 (v8i8 (trunc (smax (smin (v8i16 V128:$Vn), (v8i16 VImm7F)),
                                          (v8i16 VImm80)))))),
          (SQXTNv16i8 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Vd, dsub), V128:$Vn)>;

// concat_vectors(Vd, trunc(smin(smax Vm, -32768), 32767) ~> SQXTN2(Vd, Vn)
// with reversed min/max
def : Pat<(v8i16 (concat_vectors
                 (v4i16 V64:$Vd),
                 (v4i16 (trunc (smin (smax (v4i32 V128:$Vn), (v4i32 VImm8000)),
                                           (v4i32 VImm7FFF)))))),
          (SQXTNv8i16 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Vd, dsub), V128:$Vn)>;
def : Pat<(v8i16 (concat_vectors
                 (v4i16 V64:$Vd),
                 (v4i16 (trunc (smax (smin (v4i32 V128:$Vn), (v4i32 VImm7FFF)),
                                           (v4i32 VImm8000)))))),
          (SQXTNv8i16 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Vd, dsub), V128:$Vn)>;

// Select BSWAP vector instructions into REV instructions
def : Pat<(v4i16 (bswap (v4i16 V64:$Rn))), 
          (v4i16 (REV16v8i8 (v4i16 V64:$Rn)))>;
def : Pat<(v8i16 (bswap (v8i16 V128:$Rn))), 
          (v8i16 (REV16v16i8 (v8i16 V128:$Rn)))>;
def : Pat<(v2i32 (bswap (v2i32 V64:$Rn))), 
          (v2i32 (REV32v8i8 (v2i32 V64:$Rn)))>;
def : Pat<(v4i32 (bswap (v4i32 V128:$Rn))), 
          (v4i32 (REV32v16i8 (v4i32 V128:$Rn)))>;
def : Pat<(v2i64 (bswap (v2i64 V128:$Rn))), 
          (v2i64 (REV64v16i8 (v2i64 V128:$Rn)))>;

//===----------------------------------------------------------------------===//
// Advanced SIMD three vector instructions.
//===----------------------------------------------------------------------===//

defm ADD     : SIMDThreeSameVector<0, 0b10000, "add", add>;
defm ADDP    : SIMDThreeSameVector<0, 0b10111, "addp", AArch64addp>;
defm CMEQ    : SIMDThreeSameVector<1, 0b10001, "cmeq", AArch64cmeq>;
defm CMGE    : SIMDThreeSameVector<0, 0b00111, "cmge", AArch64cmge>;
defm CMGT    : SIMDThreeSameVector<0, 0b00110, "cmgt", AArch64cmgt>;
defm CMHI    : SIMDThreeSameVector<1, 0b00110, "cmhi", AArch64cmhi>;
defm CMHS    : SIMDThreeSameVector<1, 0b00111, "cmhs", AArch64cmhs>;
defm CMTST   : SIMDThreeSameVector<0, 0b10001, "cmtst", AArch64cmtst>;
foreach VT = [ v8i8, v16i8, v4i16, v8i16, v2i32, v4i32, v2i64 ] in {
def : Pat<(vnot (AArch64cmeqz VT:$Rn)), (!cast<Instruction>("CMTST"#VT) VT:$Rn, VT:$Rn)>;
}
defm FABD    : SIMDThreeSameVectorFP<1,1,0b010,"fabd", int_aarch64_neon_fabd>;
let Predicates = [HasNEON] in {
foreach VT = [ v2f32, v4f32, v2f64 ] in
def : Pat<(fabs (fsub VT:$Rn, VT:$Rm)), (!cast<Instruction>("FABD"#VT) VT:$Rn, VT:$Rm)>;
}
let Predicates = [HasNEON, HasFullFP16] in {
foreach VT = [ v4f16, v8f16 ] in
def : Pat<(fabs (fsub VT:$Rn, VT:$Rm)), (!cast<Instruction>("FABD"#VT) VT:$Rn, VT:$Rm)>;
}
defm FACGE   : SIMDThreeSameVectorFPCmp<1,0,0b101,"facge",AArch64facge>;
defm FACGT   : SIMDThreeSameVectorFPCmp<1,1,0b101,"facgt",AArch64facgt>;
defm FADDP   : SIMDThreeSameVectorFP<1,0,0b010,"faddp", AArch64faddp>;
defm FADD    : SIMDThreeSameVectorFP<0,0,0b010,"fadd", any_fadd>;
defm FCMEQ   : SIMDThreeSameVectorFPCmp<0, 0, 0b100, "fcmeq", AArch64fcmeq>;
defm FCMGE   : SIMDThreeSameVectorFPCmp<1, 0, 0b100, "fcmge", AArch64fcmge>;
defm FCMGT   : SIMDThreeSameVectorFPCmp<1, 1, 0b100, "fcmgt", AArch64fcmgt>;
defm FDIV    : SIMDThreeSameVectorFP<1,0,0b111,"fdiv", any_fdiv>;
defm FMAXNMP : SIMDThreeSameVectorFP<1,0,0b000,"fmaxnmp", int_aarch64_neon_fmaxnmp>;
defm FMAXNM  : SIMDThreeSameVectorFP<0,0,0b000,"fmaxnm", any_fmaxnum>;
defm FMAXP   : SIMDThreeSameVectorFP<1,0,0b110,"fmaxp", int_aarch64_neon_fmaxp>;
defm FMAX    : SIMDThreeSameVectorFP<0,0,0b110,"fmax", any_fmaximum>;
defm FMINNMP : SIMDThreeSameVectorFP<1,1,0b000,"fminnmp", int_aarch64_neon_fminnmp>;
defm FMINNM  : SIMDThreeSameVectorFP<0,1,0b000,"fminnm", any_fminnum>;
defm FMINP   : SIMDThreeSameVectorFP<1,1,0b110,"fminp", int_aarch64_neon_fminp>;
defm FMIN    : SIMDThreeSameVectorFP<0,1,0b110,"fmin", any_fminimum>;

// NOTE: The operands of the PatFrag are reordered on FMLA/FMLS because the
// instruction expects the addend first, while the fma intrinsic puts it last.
defm FMLA     : SIMDThreeSameVectorFPTied<0, 0, 0b001, "fmla",
            TriOpFrag<(any_fma node:$RHS, node:$MHS, node:$LHS)> >;
defm FMLS     : SIMDThreeSameVectorFPTied<0, 1, 0b001, "fmls",
            TriOpFrag<(any_fma node:$MHS, (fneg node:$RHS), node:$LHS)> >;

defm FMULX    : SIMDThreeSameVectorFP<0,0,0b011,"fmulx", int_aarch64_neon_fmulx>;
defm FMUL     : SIMDThreeSameVectorFP<1,0,0b011,"fmul", any_fmul>;
defm FRECPS   : SIMDThreeSameVectorFP<0,0,0b111,"frecps", int_aarch64_neon_frecps>;
defm FRSQRTS  : SIMDThreeSameVectorFP<0,1,0b111,"frsqrts", int_aarch64_neon_frsqrts>;
defm FSUB     : SIMDThreeSameVectorFP<0,1,0b010,"fsub", any_fsub>;

// MLA and MLS are generated in MachineCombine
defm MLA      : SIMDThreeSameVectorBHSTied<0, 0b10010, "mla", null_frag>;
defm MLS      : SIMDThreeSameVectorBHSTied<1, 0b10010, "mls", null_frag>;

defm MUL      : SIMDThreeSameVectorBHS<0, 0b10011, "mul", mul>;
defm PMUL     : SIMDThreeSameVectorB<1, 0b10011, "pmul", int_aarch64_neon_pmul>;
defm SABA     : SIMDThreeSameVectorBHSTied<0, 0b01111, "saba",
      TriOpFrag<(add node:$LHS, (AArch64sabd node:$MHS, node:$RHS))> >;
defm SABD     : SIMDThreeSameVectorBHS<0,0b01110,"sabd", AArch64sabd>;
defm SHADD    : SIMDThreeSameVectorBHS<0,0b00000,"shadd", avgfloors>;
defm SHSUB    : SIMDThreeSameVectorBHS<0,0b00100,"shsub", int_aarch64_neon_shsub>;
defm SMAXP    : SIMDThreeSameVectorBHS<0,0b10100,"smaxp", int_aarch64_neon_smaxp>;
defm SMAX     : SIMDThreeSameVectorBHS<0,0b01100,"smax", smax>;
defm SMINP    : SIMDThreeSameVectorBHS<0,0b10101,"sminp", int_aarch64_neon_sminp>;
defm SMIN     : SIMDThreeSameVectorBHS<0,0b01101,"smin", smin>;
defm SQADD    : SIMDThreeSameVector<0,0b00001,"sqadd", int_aarch64_neon_sqadd>;
defm SQDMULH  : SIMDThreeSameVectorHS<0,0b10110,"sqdmulh",int_aarch64_neon_sqdmulh>;
defm SQRDMULH : SIMDThreeSameVectorHS<1,0b10110,"sqrdmulh",int_aarch64_neon_sqrdmulh>;
defm SQRSHL   : SIMDThreeSameVector<0,0b01011,"sqrshl", int_aarch64_neon_sqrshl>;
defm SQSHL    : SIMDThreeSameVector<0,0b01001,"sqshl", int_aarch64_neon_sqshl>;
defm SQSUB    : SIMDThreeSameVector<0,0b00101,"sqsub", int_aarch64_neon_sqsub>;
defm SRHADD   : SIMDThreeSameVectorBHS<0,0b00010,"srhadd", avgceils>;
defm SRSHL    : SIMDThreeSameVector<0,0b01010,"srshl", int_aarch64_neon_srshl>;
defm SSHL     : SIMDThreeSameVector<0,0b01000,"sshl", int_aarch64_neon_sshl>;
defm SUB      : SIMDThreeSameVector<1,0b10000,"sub", sub>;
defm UABA     : SIMDThreeSameVectorBHSTied<1, 0b01111, "uaba",
      TriOpFrag<(add node:$LHS, (AArch64uabd node:$MHS, node:$RHS))> >;
defm UABD     : SIMDThreeSameVectorBHS<1,0b01110,"uabd", AArch64uabd>;
defm UHADD    : SIMDThreeSameVectorBHS<1,0b00000,"uhadd", avgflooru>;
defm UHSUB    : SIMDThreeSameVectorBHS<1,0b00100,"uhsub", int_aarch64_neon_uhsub>;
defm UMAXP    : SIMDThreeSameVectorBHS<1,0b10100,"umaxp", int_aarch64_neon_umaxp>;
defm UMAX     : SIMDThreeSameVectorBHS<1,0b01100,"umax", umax>;
defm UMINP    : SIMDThreeSameVectorBHS<1,0b10101,"uminp", int_aarch64_neon_uminp>;
defm UMIN     : SIMDThreeSameVectorBHS<1,0b01101,"umin", umin>;
defm UQADD    : SIMDThreeSameVector<1,0b00001,"uqadd", int_aarch64_neon_uqadd>;
defm UQRSHL   : SIMDThreeSameVector<1,0b01011,"uqrshl", int_aarch64_neon_uqrshl>;
defm UQSHL    : SIMDThreeSameVector<1,0b01001,"uqshl", int_aarch64_neon_uqshl>;
defm UQSUB    : SIMDThreeSameVector<1,0b00101,"uqsub", int_aarch64_neon_uqsub>;
defm URHADD   : SIMDThreeSameVectorBHS<1,0b00010,"urhadd", avgceilu>;
defm URSHL    : SIMDThreeSameVector<1,0b01010,"urshl", int_aarch64_neon_urshl>;
defm USHL     : SIMDThreeSameVector<1,0b01000,"ushl", int_aarch64_neon_ushl>;
defm SQRDMLAH : SIMDThreeSameVectorSQRDMLxHTiedHS<1,0b10000,"sqrdmlah",
                                                  int_aarch64_neon_sqrdmlah>;
defm SQRDMLSH : SIMDThreeSameVectorSQRDMLxHTiedHS<1,0b10001,"sqrdmlsh",
                                                    int_aarch64_neon_sqrdmlsh>;

// Extra saturate patterns, other than the intrinsics matches above
defm : SIMDThreeSameVectorExtraPatterns<"SQADD", saddsat>;
defm : SIMDThreeSameVectorExtraPatterns<"UQADD", uaddsat>;
defm : SIMDThreeSameVectorExtraPatterns<"SQSUB", ssubsat>;
defm : SIMDThreeSameVectorExtraPatterns<"UQSUB", usubsat>;

defm AND : SIMDLogicalThreeVector<0, 0b00, "and", and>;
defm BIC : SIMDLogicalThreeVector<0, 0b01, "bic",
                                  BinOpFrag<(and node:$LHS, (vnot node:$RHS))> >;
defm EOR : SIMDLogicalThreeVector<1, 0b00, "eor", xor>;
defm ORN : SIMDLogicalThreeVector<0, 0b11, "orn",
                                  BinOpFrag<(or node:$LHS, (vnot node:$RHS))> >;
defm ORR : SIMDLogicalThreeVector<0, 0b10, "orr", or>;

// Pseudo bitwise select pattern BSP.
// It is expanded into BSL/BIT/BIF after register allocation.
defm BSP : SIMDLogicalThreeVectorPseudo<TriOpFrag<(or (and node:$LHS, node:$MHS),
                                                      (and (vnot node:$LHS), node:$RHS))>>;
defm BSL : SIMDLogicalThreeVectorTied<1, 0b01, "bsl">;
defm BIT : SIMDLogicalThreeVectorTied<1, 0b10, "bit">;
defm BIF : SIMDLogicalThreeVectorTied<1, 0b11, "bif">;

def : Pat<(AArch64bsp (v8i8 V64:$Rd), V64:$Rn, V64:$Rm),
          (BSPv8i8 V64:$Rd, V64:$Rn, V64:$Rm)>;
def : Pat<(AArch64bsp (v4i16 V64:$Rd), V64:$Rn, V64:$Rm),
          (BSPv8i8 V64:$Rd, V64:$Rn, V64:$Rm)>;
def : Pat<(AArch64bsp (v2i32 V64:$Rd), V64:$Rn, V64:$Rm),
          (BSPv8i8 V64:$Rd, V64:$Rn, V64:$Rm)>;
def : Pat<(AArch64bsp (v1i64 V64:$Rd), V64:$Rn, V64:$Rm),
          (BSPv8i8 V64:$Rd, V64:$Rn, V64:$Rm)>;

def : Pat<(AArch64bsp (v16i8 V128:$Rd), V128:$Rn, V128:$Rm),
          (BSPv16i8 V128:$Rd, V128:$Rn, V128:$Rm)>;
def : Pat<(AArch64bsp (v8i16 V128:$Rd), V128:$Rn, V128:$Rm),
          (BSPv16i8 V128:$Rd, V128:$Rn, V128:$Rm)>;
def : Pat<(AArch64bsp (v4i32 V128:$Rd), V128:$Rn, V128:$Rm),
          (BSPv16i8 V128:$Rd, V128:$Rn, V128:$Rm)>;
def : Pat<(AArch64bsp (v2i64 V128:$Rd), V128:$Rn, V128:$Rm),
          (BSPv16i8 V128:$Rd, V128:$Rn, V128:$Rm)>;

// The following SetCC patterns are used for GlobalISel only
multiclass SelectSetCC<PatFrags InFrag, string INST> {
  def : Pat<(v8i8 (InFrag (v8i8 V64:$Rn), (v8i8 V64:$Rm))),
            (v8i8 (!cast<Instruction>(INST # v8i8) (v8i8 V64:$Rn), (v8i8 V64:$Rm)))>;
  def : Pat<(v16i8 (InFrag (v16i8 V128:$Rn), (v16i8 V128:$Rm))),
            (v16i8 (!cast<Instruction>(INST # v16i8) (v16i8 V128:$Rn), (v16i8 V128:$Rm)))>;
  def : Pat<(v4i16 (InFrag (v4i16 V64:$Rn), (v4i16 V64:$Rm))),
            (v4i16 (!cast<Instruction>(INST # v4i16) (v4i16 V64:$Rn), (v4i16 V64:$Rm)))>;
  def : Pat<(v8i16 (InFrag (v8i16 V128:$Rn), (v8i16 V128:$Rm))),
            (v8i16 (!cast<Instruction>(INST # v8i16) (v8i16 V128:$Rn), (v8i16 V128:$Rm)))>;
  def : Pat<(v2i32 (InFrag (v2i32 V64:$Rn), (v2i32 V64:$Rm))),
            (v2i32 (!cast<Instruction>(INST # v2i32) (v2i32 V64:$Rn), (v2i32 V64:$Rm)))>;
  def : Pat<(v4i32 (InFrag (v4i32 V128:$Rn), (v4i32 V128:$Rm))),
            (v4i32 (!cast<Instruction>(INST # v4i32) (v4i32 V128:$Rn), (v4i32 V128:$Rm)))>;
  def : Pat<(v2i64 (InFrag (v2i64 V128:$Rn), (v2i64 V128:$Rm))),
            (v2i64 (!cast<Instruction>(INST # v2i64) (v2i64 V128:$Rn), (v2i64 V128:$Rm)))>;
}

defm : SelectSetCC<seteq, "CMEQ">;
defm : SelectSetCC<setgt, "CMGT">;
defm : SelectSetCC<setge, "CMGE">;
defm : SelectSetCC<setugt, "CMHI">;
defm : SelectSetCC<setuge, "CMHS">;

multiclass SelectSetCCSwapOperands<PatFrags InFrag, string INST> {
  def : Pat<(v8i8 (InFrag (v8i8 V64:$Rn), (v8i8 V64:$Rm))),
            (v8i8 (!cast<Instruction>(INST # v8i8) (v8i8 V64:$Rm), (v8i8 V64:$Rn)))>;
  def : Pat<(v16i8 (InFrag (v16i8 V128:$Rn), (v16i8 V128:$Rm))),
            (v16i8 (!cast<Instruction>(INST # v16i8) (v16i8 V128:$Rm), (v16i8 V128:$Rn)))>;
  def : Pat<(v4i16 (InFrag (v4i16 V64:$Rn), (v4i16 V64:$Rm))),
            (v4i16 (!cast<Instruction>(INST # v4i16) (v4i16 V64:$Rm), (v4i16 V64:$Rn)))>;
  def : Pat<(v8i16 (InFrag (v8i16 V128:$Rn), (v8i16 V128:$Rm))),
            (v8i16 (!cast<Instruction>(INST # v8i16) (v8i16 V128:$Rm), (v8i16 V128:$Rn)))>;
  def : Pat<(v2i32 (InFrag (v2i32 V64:$Rn), (v2i32 V64:$Rm))),
            (v2i32 (!cast<Instruction>(INST # v2i32) (v2i32 V64:$Rm), (v2i32 V64:$Rn)))>;
  def : Pat<(v4i32 (InFrag (v4i32 V128:$Rn), (v4i32 V128:$Rm))),
            (v4i32 (!cast<Instruction>(INST # v4i32) (v4i32 V128:$Rm), (v4i32 V128:$Rn)))>;
  def : Pat<(v2i64 (InFrag (v2i64 V128:$Rn), (v2i64 V128:$Rm))),
            (v2i64 (!cast<Instruction>(INST # v2i64) (v2i64 V128:$Rm), (v2i64 V128:$Rn)))>;
}

defm : SelectSetCCSwapOperands<setlt, "CMGT">;
defm : SelectSetCCSwapOperands<setle, "CMGE">;
defm : SelectSetCCSwapOperands<setult, "CMHI">;
defm : SelectSetCCSwapOperands<setule, "CMHS">;

multiclass SelectSetCCZeroRHS<PatFrags InFrag, string INST> {
  def : Pat<(v8i8 (InFrag (v8i8 V64:$Rn), immAllZerosV)),
            (v8i8 (!cast<Instruction>(INST # v8i8rz) (v8i8 V64:$Rn)))>;
  def : Pat<(v16i8 (InFrag (v16i8 V128:$Rn), immAllZerosV)),
            (v16i8 (!cast<Instruction>(INST # v16i8rz) (v16i8 V128:$Rn)))>;
  def : Pat<(v4i16 (InFrag (v4i16 V64:$Rn), immAllZerosV)),
            (v4i16 (!cast<Instruction>(INST # v4i16rz) (v4i16 V64:$Rn)))>;
  def : Pat<(v8i16 (InFrag (v8i16 V128:$Rn), immAllZerosV)),
            (v8i16 (!cast<Instruction>(INST # v8i16rz) (v8i16 V128:$Rn)))>;
  def : Pat<(v2i32 (InFrag (v2i32 V64:$Rn), immAllZerosV)),
            (v2i32 (!cast<Instruction>(INST # v2i32rz) (v2i32 V64:$Rn)))>;
  def : Pat<(v4i32 (InFrag (v4i32 V128:$Rn), immAllZerosV)),
            (v4i32 (!cast<Instruction>(INST # v4i32rz) (v4i32 V128:$Rn)))>;
  def : Pat<(v2i64 (InFrag (v2i64 V128:$Rn), immAllZerosV)),
            (v2i64 (!cast<Instruction>(INST # v2i64rz) (v2i64 V128:$Rn)))>;
}

defm : SelectSetCCZeroRHS<seteq, "CMEQ">;
defm : SelectSetCCZeroRHS<setgt, "CMGT">;
defm : SelectSetCCZeroRHS<setge, "CMGE">;
defm : SelectSetCCZeroRHS<setlt, "CMLT">;
defm : SelectSetCCZeroRHS<setle, "CMLE">;

multiclass SelectSetCCZeroLHS<PatFrags InFrag, string INST> {
  def : Pat<(v8i8 (InFrag immAllZerosV, (v8i8 V64:$Rn))),
            (v8i8 (!cast<Instruction>(INST # v8i8rz) (v8i8 V64:$Rn)))>;
  def : Pat<(v16i8 (InFrag immAllZerosV, (v16i8 V128:$Rn))),
            (v16i8 (!cast<Instruction>(INST # v16i8rz) (v16i8 V128:$Rn)))>;
  def : Pat<(v4i16 (InFrag immAllZerosV, (v4i16 V64:$Rn))),
            (v4i16 (!cast<Instruction>(INST # v4i16rz) (v4i16 V64:$Rn)))>;
  def : Pat<(v8i16 (InFrag immAllZerosV, (v8i16 V128:$Rn))),
            (v8i16 (!cast<Instruction>(INST # v8i16rz) (v8i16 V128:$Rn)))>;
  def : Pat<(v2i32 (InFrag immAllZerosV, (v2i32 V64:$Rn))),
            (v2i32 (!cast<Instruction>(INST # v2i32rz) (v2i32 V64:$Rn)))>;
  def : Pat<(v4i32 (InFrag immAllZerosV, (v4i32 V128:$Rn))),
            (v4i32 (!cast<Instruction>(INST # v4i32rz) (v4i32 V128:$Rn)))>;
  def : Pat<(v2i64 (InFrag immAllZerosV, (v2i64 V128:$Rn))),
            (v2i64 (!cast<Instruction>(INST # v2i64rz) (v2i64 V128:$Rn)))>;
}

defm : SelectSetCCZeroLHS<seteq, "CMEQ">;
defm : SelectSetCCZeroLHS<setgt, "CMLT">;
defm : SelectSetCCZeroLHS<setge, "CMLE">;
defm : SelectSetCCZeroLHS<setlt, "CMGT">;
defm : SelectSetCCZeroLHS<setle, "CMGE">;

let Predicates = [HasNEON] in {
def : InstAlias<"mov{\t$dst.16b, $src.16b|.16b\t$dst, $src}",
                (ORRv16i8 V128:$dst, V128:$src, V128:$src), 1>;
def : InstAlias<"mov{\t$dst.8h, $src.8h|.8h\t$dst, $src}",
                (ORRv16i8 V128:$dst, V128:$src, V128:$src), 0>;
def : InstAlias<"mov{\t$dst.4s, $src.4s|.4s\t$dst, $src}",
                (ORRv16i8 V128:$dst, V128:$src, V128:$src), 0>;
def : InstAlias<"mov{\t$dst.2d, $src.2d|.2d\t$dst, $src}",
                (ORRv16i8 V128:$dst, V128:$src, V128:$src), 0>;

def : InstAlias<"mov{\t$dst.8b, $src.8b|.8b\t$dst, $src}",
                (ORRv8i8 V64:$dst, V64:$src, V64:$src), 1>;
def : InstAlias<"mov{\t$dst.4h, $src.4h|.4h\t$dst, $src}",
                (ORRv8i8 V64:$dst, V64:$src, V64:$src), 0>;
def : InstAlias<"mov{\t$dst.2s, $src.2s|.2s\t$dst, $src}",
                (ORRv8i8 V64:$dst, V64:$src, V64:$src), 0>;
def : InstAlias<"mov{\t$dst.1d, $src.1d|.1d\t$dst, $src}",
                (ORRv8i8 V64:$dst, V64:$src, V64:$src), 0>;

def : InstAlias<"{cmls\t$dst.8b, $src1.8b, $src2.8b" #
                "|cmls.8b\t$dst, $src1, $src2}",
                (CMHSv8i8 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmls\t$dst.16b, $src1.16b, $src2.16b" #
                "|cmls.16b\t$dst, $src1, $src2}",
                (CMHSv16i8 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmls\t$dst.4h, $src1.4h, $src2.4h" #
                "|cmls.4h\t$dst, $src1, $src2}",
                (CMHSv4i16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmls\t$dst.8h, $src1.8h, $src2.8h" #
                "|cmls.8h\t$dst, $src1, $src2}",
                (CMHSv8i16 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmls\t$dst.2s, $src1.2s, $src2.2s" #
                "|cmls.2s\t$dst, $src1, $src2}",
                (CMHSv2i32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmls\t$dst.4s, $src1.4s, $src2.4s" #
                "|cmls.4s\t$dst, $src1, $src2}",
                (CMHSv4i32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmls\t$dst.2d, $src1.2d, $src2.2d" #
                "|cmls.2d\t$dst, $src1, $src2}",
                (CMHSv2i64 V128:$dst, V128:$src2, V128:$src1), 0>;

def : InstAlias<"{cmlo\t$dst.8b, $src1.8b, $src2.8b" #
                "|cmlo.8b\t$dst, $src1, $src2}",
                (CMHIv8i8 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmlo\t$dst.16b, $src1.16b, $src2.16b" #
                "|cmlo.16b\t$dst, $src1, $src2}",
                (CMHIv16i8 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmlo\t$dst.4h, $src1.4h, $src2.4h" #
                "|cmlo.4h\t$dst, $src1, $src2}",
                (CMHIv4i16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmlo\t$dst.8h, $src1.8h, $src2.8h" #
                "|cmlo.8h\t$dst, $src1, $src2}",
                (CMHIv8i16 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmlo\t$dst.2s, $src1.2s, $src2.2s" #
                "|cmlo.2s\t$dst, $src1, $src2}",
                (CMHIv2i32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmlo\t$dst.4s, $src1.4s, $src2.4s" #
                "|cmlo.4s\t$dst, $src1, $src2}",
                (CMHIv4i32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmlo\t$dst.2d, $src1.2d, $src2.2d" #
                "|cmlo.2d\t$dst, $src1, $src2}",
                (CMHIv2i64 V128:$dst, V128:$src2, V128:$src1), 0>;

def : InstAlias<"{cmle\t$dst.8b, $src1.8b, $src2.8b" #
                "|cmle.8b\t$dst, $src1, $src2}",
                (CMGEv8i8 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmle\t$dst.16b, $src1.16b, $src2.16b" #
                "|cmle.16b\t$dst, $src1, $src2}",
                (CMGEv16i8 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmle\t$dst.4h, $src1.4h, $src2.4h" #
                "|cmle.4h\t$dst, $src1, $src2}",
                (CMGEv4i16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmle\t$dst.8h, $src1.8h, $src2.8h" #
                "|cmle.8h\t$dst, $src1, $src2}",
                (CMGEv8i16 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmle\t$dst.2s, $src1.2s, $src2.2s" #
                "|cmle.2s\t$dst, $src1, $src2}",
                (CMGEv2i32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmle\t$dst.4s, $src1.4s, $src2.4s" #
                "|cmle.4s\t$dst, $src1, $src2}",
                (CMGEv4i32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmle\t$dst.2d, $src1.2d, $src2.2d" #
                "|cmle.2d\t$dst, $src1, $src2}",
                (CMGEv2i64 V128:$dst, V128:$src2, V128:$src1), 0>;

def : InstAlias<"{cmlt\t$dst.8b, $src1.8b, $src2.8b" #
                "|cmlt.8b\t$dst, $src1, $src2}",
                (CMGTv8i8 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmlt\t$dst.16b, $src1.16b, $src2.16b" #
                "|cmlt.16b\t$dst, $src1, $src2}",
                (CMGTv16i8 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmlt\t$dst.4h, $src1.4h, $src2.4h" #
                "|cmlt.4h\t$dst, $src1, $src2}",
                (CMGTv4i16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmlt\t$dst.8h, $src1.8h, $src2.8h" #
                "|cmlt.8h\t$dst, $src1, $src2}",
                (CMGTv8i16 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmlt\t$dst.2s, $src1.2s, $src2.2s" #
                "|cmlt.2s\t$dst, $src1, $src2}",
                (CMGTv2i32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmlt\t$dst.4s, $src1.4s, $src2.4s" #
                "|cmlt.4s\t$dst, $src1, $src2}",
                (CMGTv4i32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmlt\t$dst.2d, $src1.2d, $src2.2d" #
                "|cmlt.2d\t$dst, $src1, $src2}",
                (CMGTv2i64 V128:$dst, V128:$src2, V128:$src1), 0>;

let Predicates = [HasNEON, HasFullFP16] in {
def : InstAlias<"{fcmle\t$dst.4h, $src1.4h, $src2.4h" #
                "|fcmle.4h\t$dst, $src1, $src2}",
                (FCMGEv4f16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{fcmle\t$dst.8h, $src1.8h, $src2.8h" #
                "|fcmle.8h\t$dst, $src1, $src2}",
                (FCMGEv8f16 V128:$dst, V128:$src2, V128:$src1), 0>;
}
def : InstAlias<"{fcmle\t$dst.2s, $src1.2s, $src2.2s" #
                "|fcmle.2s\t$dst, $src1, $src2}",
                (FCMGEv2f32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{fcmle\t$dst.4s, $src1.4s, $src2.4s" #
                "|fcmle.4s\t$dst, $src1, $src2}",
                (FCMGEv4f32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{fcmle\t$dst.2d, $src1.2d, $src2.2d" #
                "|fcmle.2d\t$dst, $src1, $src2}",
                (FCMGEv2f64 V128:$dst, V128:$src2, V128:$src1), 0>;

let Predicates = [HasNEON, HasFullFP16] in {
def : InstAlias<"{fcmlt\t$dst.4h, $src1.4h, $src2.4h" #
                "|fcmlt.4h\t$dst, $src1, $src2}",
                (FCMGTv4f16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{fcmlt\t$dst.8h, $src1.8h, $src2.8h" #
                "|fcmlt.8h\t$dst, $src1, $src2}",
                (FCMGTv8f16 V128:$dst, V128:$src2, V128:$src1), 0>;
}
def : InstAlias<"{fcmlt\t$dst.2s, $src1.2s, $src2.2s" #
                "|fcmlt.2s\t$dst, $src1, $src2}",
                (FCMGTv2f32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{fcmlt\t$dst.4s, $src1.4s, $src2.4s" #
                "|fcmlt.4s\t$dst, $src1, $src2}",
                (FCMGTv4f32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{fcmlt\t$dst.2d, $src1.2d, $src2.2d" #
                "|fcmlt.2d\t$dst, $src1, $src2}",
                (FCMGTv2f64 V128:$dst, V128:$src2, V128:$src1), 0>;

let Predicates = [HasNEON, HasFullFP16] in {
def : InstAlias<"{facle\t$dst.4h, $src1.4h, $src2.4h" #
                "|facle.4h\t$dst, $src1, $src2}",
                (FACGEv4f16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{facle\t$dst.8h, $src1.8h, $src2.8h" #
                "|facle.8h\t$dst, $src1, $src2}",
                (FACGEv8f16 V128:$dst, V128:$src2, V128:$src1), 0>;
}
def : InstAlias<"{facle\t$dst.2s, $src1.2s, $src2.2s" #
                "|facle.2s\t$dst, $src1, $src2}",
                (FACGEv2f32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{facle\t$dst.4s, $src1.4s, $src2.4s" #
                "|facle.4s\t$dst, $src1, $src2}",
                (FACGEv4f32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{facle\t$dst.2d, $src1.2d, $src2.2d" #
                "|facle.2d\t$dst, $src1, $src2}",
                (FACGEv2f64 V128:$dst, V128:$src2, V128:$src1), 0>;

let Predicates = [HasNEON, HasFullFP16] in {
def : InstAlias<"{faclt\t$dst.4h, $src1.4h, $src2.4h" #
                "|faclt.4h\t$dst, $src1, $src2}",
                (FACGTv4f16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{faclt\t$dst.8h, $src1.8h, $src2.8h" #
                "|faclt.8h\t$dst, $src1, $src2}",
                (FACGTv8f16 V128:$dst, V128:$src2, V128:$src1), 0>;
}
def : InstAlias<"{faclt\t$dst.2s, $src1.2s, $src2.2s" #
                "|faclt.2s\t$dst, $src1, $src2}",
                (FACGTv2f32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{faclt\t$dst.4s, $src1.4s, $src2.4s" #
                "|faclt.4s\t$dst, $src1, $src2}",
                (FACGTv4f32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{faclt\t$dst.2d, $src1.2d, $src2.2d" #
                "|faclt.2d\t$dst, $src1, $src2}",
                (FACGTv2f64 V128:$dst, V128:$src2, V128:$src1), 0>;
}

//===----------------------------------------------------------------------===//
// Advanced SIMD three scalar instructions.
//===----------------------------------------------------------------------===//

defm ADD      : SIMDThreeScalarD<0, 0b10000, "add", add>;
defm CMEQ     : SIMDThreeScalarD<1, 0b10001, "cmeq", AArch64cmeq>;
defm CMGE     : SIMDThreeScalarD<0, 0b00111, "cmge", AArch64cmge>;
defm CMGT     : SIMDThreeScalarD<0, 0b00110, "cmgt", AArch64cmgt>;
defm CMHI     : SIMDThreeScalarD<1, 0b00110, "cmhi", AArch64cmhi>;
defm CMHS     : SIMDThreeScalarD<1, 0b00111, "cmhs", AArch64cmhs>;
defm CMTST    : SIMDThreeScalarD<0, 0b10001, "cmtst", AArch64cmtst>;
defm FABD     : SIMDFPThreeScalar<1, 1, 0b010, "fabd", int_aarch64_sisd_fabd>;
def : Pat<(v1f64 (int_aarch64_neon_fabd (v1f64 FPR64:$Rn), (v1f64 FPR64:$Rm))),
          (FABD64 FPR64:$Rn, FPR64:$Rm)>;
let Predicates = [HasNEON, HasFullFP16] in {
def : Pat<(fabs (fsub f16:$Rn, f16:$Rm)), (FABD16 f16:$Rn, f16:$Rm)>;
}
let Predicates = [HasNEON] in {
def : Pat<(fabs (fsub f32:$Rn, f32:$Rm)), (FABD32 f32:$Rn, f32:$Rm)>;
def : Pat<(fabs (fsub f64:$Rn, f64:$Rm)), (FABD64 f64:$Rn, f64:$Rm)>;
}
defm FACGE    : SIMDThreeScalarFPCmp<1, 0, 0b101, "facge",
                                     int_aarch64_neon_facge>;
defm FACGT    : SIMDThreeScalarFPCmp<1, 1, 0b101, "facgt",
                                     int_aarch64_neon_facgt>;
defm FCMEQ    : SIMDThreeScalarFPCmp<0, 0, 0b100, "fcmeq", AArch64fcmeq>;
defm FCMGE    : SIMDThreeScalarFPCmp<1, 0, 0b100, "fcmge", AArch64fcmge>;
defm FCMGT    : SIMDThreeScalarFPCmp<1, 1, 0b100, "fcmgt", AArch64fcmgt>;
defm FMULX    : SIMDFPThreeScalar<0, 0, 0b011, "fmulx", int_aarch64_neon_fmulx, HasNEONandIsStreamingSafe>;
defm FRECPS   : SIMDFPThreeScalar<0, 0, 0b111, "frecps", int_aarch64_neon_frecps, HasNEONandIsStreamingSafe>;
defm FRSQRTS  : SIMDFPThreeScalar<0, 1, 0b111, "frsqrts", int_aarch64_neon_frsqrts, HasNEONandIsStreamingSafe>;
defm SQADD    : SIMDThreeScalarBHSD<0, 0b00001, "sqadd", int_aarch64_neon_sqadd>;
defm SQDMULH  : SIMDThreeScalarHS<  0, 0b10110, "sqdmulh", int_aarch64_neon_sqdmulh>;
defm SQRDMULH : SIMDThreeScalarHS<  1, 0b10110, "sqrdmulh", int_aarch64_neon_sqrdmulh>;
defm SQRSHL   : SIMDThreeScalarBHSD<0, 0b01011, "sqrshl",int_aarch64_neon_sqrshl>;
defm SQSHL    : SIMDThreeScalarBHSD<0, 0b01001, "sqshl", int_aarch64_neon_sqshl>;
defm SQSUB    : SIMDThreeScalarBHSD<0, 0b00101, "sqsub", int_aarch64_neon_sqsub>;
defm SRSHL    : SIMDThreeScalarD<   0, 0b01010, "srshl", int_aarch64_neon_srshl>;
defm SSHL     : SIMDThreeScalarD<   0, 0b01000, "sshl", int_aarch64_neon_sshl>;
defm SUB      : SIMDThreeScalarD<   1, 0b10000, "sub", sub>;
defm UQADD    : SIMDThreeScalarBHSD<1, 0b00001, "uqadd", int_aarch64_neon_uqadd>;
defm UQRSHL   : SIMDThreeScalarBHSD<1, 0b01011, "uqrshl",int_aarch64_neon_uqrshl>;
defm UQSHL    : SIMDThreeScalarBHSD<1, 0b01001, "uqshl", int_aarch64_neon_uqshl>;
defm UQSUB    : SIMDThreeScalarBHSD<1, 0b00101, "uqsub", int_aarch64_neon_uqsub>;
defm URSHL    : SIMDThreeScalarD<   1, 0b01010, "urshl", int_aarch64_neon_urshl>;
defm USHL     : SIMDThreeScalarD<   1, 0b01000, "ushl", int_aarch64_neon_ushl>;
let Predicates = [HasRDM] in {
  defm SQRDMLAH : SIMDThreeScalarHSTied<1, 0, 0b10000, "sqrdmlah">;
  defm SQRDMLSH : SIMDThreeScalarHSTied<1, 0, 0b10001, "sqrdmlsh">;
  def : Pat<(i32 (int_aarch64_neon_sqrdmlah (i32 FPR32:$Rd), (i32 FPR32:$Rn),
                                            (i32 FPR32:$Rm))),
            (SQRDMLAHv1i32 FPR32:$Rd, FPR32:$Rn, FPR32:$Rm)>;
  def : Pat<(i32 (int_aarch64_neon_sqrdmlsh (i32 FPR32:$Rd), (i32 FPR32:$Rn),
                                            (i32 FPR32:$Rm))),
            (SQRDMLSHv1i32 FPR32:$Rd, FPR32:$Rn, FPR32:$Rm)>;
}

defm : FMULScalarFromIndexedLane0Patterns<"FMULX", "16", "32", "64",
                                          int_aarch64_neon_fmulx,
                                          [HasNEONandIsStreamingSafe]>;

let Predicates = [HasNEON] in {
def : InstAlias<"cmls $dst, $src1, $src2",
                (CMHSv1i64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"cmle $dst, $src1, $src2",
                (CMGEv1i64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"cmlo $dst, $src1, $src2",
                (CMHIv1i64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"cmlt $dst, $src1, $src2",
                (CMGTv1i64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
}
let Predicates = [HasFPARMv8] in {
def : InstAlias<"fcmle $dst, $src1, $src2",
                (FCMGE32 FPR32:$dst, FPR32:$src2, FPR32:$src1), 0>;
def : InstAlias<"fcmle $dst, $src1, $src2",
                (FCMGE64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"fcmlt $dst, $src1, $src2",
                (FCMGT32 FPR32:$dst, FPR32:$src2, FPR32:$src1), 0>;
def : InstAlias<"fcmlt $dst, $src1, $src2",
                (FCMGT64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"facle $dst, $src1, $src2",
                (FACGE32 FPR32:$dst, FPR32:$src2, FPR32:$src1), 0>;
def : InstAlias<"facle $dst, $src1, $src2",
                (FACGE64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"faclt $dst, $src1, $src2",
                (FACGT32 FPR32:$dst, FPR32:$src2, FPR32:$src1), 0>;
def : InstAlias<"faclt $dst, $src1, $src2",
                (FACGT64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
}

//===----------------------------------------------------------------------===//
// Advanced SIMD three scalar instructions (mixed operands).
//===----------------------------------------------------------------------===//
defm SQDMULL  : SIMDThreeScalarMixedHS<0, 0b11010, "sqdmull",
                                       int_aarch64_neon_sqdmulls_scalar>;
defm SQDMLAL  : SIMDThreeScalarMixedTiedHS<0, 0b10010, "sqdmlal">;
defm SQDMLSL  : SIMDThreeScalarMixedTiedHS<0, 0b10110, "sqdmlsl">;

def : Pat<(i64 (int_aarch64_neon_sqadd (i64 FPR64:$Rd),
                   (i64 (int_aarch64_neon_sqdmulls_scalar (i32 FPR32:$Rn),
                                                        (i32 FPR32:$Rm))))),
          (SQDMLALi32 FPR64:$Rd, FPR32:$Rn, FPR32:$Rm)>;
def : Pat<(i64 (int_aarch64_neon_sqsub (i64 FPR64:$Rd),
                   (i64 (int_aarch64_neon_sqdmulls_scalar (i32 FPR32:$Rn),
                                                        (i32 FPR32:$Rm))))),
          (SQDMLSLi32 FPR64:$Rd, FPR32:$Rn, FPR32:$Rm)>;

//===----------------------------------------------------------------------===//
// Advanced SIMD two scalar instructions.
//===----------------------------------------------------------------------===//

defm ABS    : SIMDTwoScalarD<    0, 0b01011, "abs", abs, [HasNoCSSC]>;
defm CMEQ   : SIMDCmpTwoScalarD< 0, 0b01001, "cmeq", AArch64cmeqz>;
defm CMGE   : SIMDCmpTwoScalarD< 1, 0b01000, "cmge", AArch64cmgez>;
defm CMGT   : SIMDCmpTwoScalarD< 0, 0b01000, "cmgt", AArch64cmgtz>;
defm CMLE   : SIMDCmpTwoScalarD< 1, 0b01001, "cmle", AArch64cmlez>;
defm CMLT   : SIMDCmpTwoScalarD< 0, 0b01010, "cmlt", AArch64cmltz>;
defm FCMEQ  : SIMDFPCmpTwoScalar<0, 1, 0b01101, "fcmeq", AArch64fcmeqz>;
defm FCMGE  : SIMDFPCmpTwoScalar<1, 1, 0b01100, "fcmge", AArch64fcmgez>;
defm FCMGT  : SIMDFPCmpTwoScalar<0, 1, 0b01100, "fcmgt", AArch64fcmgtz>;
defm FCMLE  : SIMDFPCmpTwoScalar<1, 1, 0b01101, "fcmle", AArch64fcmlez>;
defm FCMLT  : SIMDFPCmpTwoScalar<0, 1, 0b01110, "fcmlt", AArch64fcmltz>;
defm FCVTAS : SIMDFPTwoScalar<   0, 0, 0b11100, "fcvtas">;
defm FCVTAU : SIMDFPTwoScalar<   1, 0, 0b11100, "fcvtau">;
defm FCVTMS : SIMDFPTwoScalar<   0, 0, 0b11011, "fcvtms">;
defm FCVTMU : SIMDFPTwoScalar<   1, 0, 0b11011, "fcvtmu">;
defm FCVTNS : SIMDFPTwoScalar<   0, 0, 0b11010, "fcvtns">;
defm FCVTNU : SIMDFPTwoScalar<   1, 0, 0b11010, "fcvtnu">;
defm FCVTPS : SIMDFPTwoScalar<   0, 1, 0b11010, "fcvtps">;
defm FCVTPU : SIMDFPTwoScalar<   1, 1, 0b11010, "fcvtpu">;
def  FCVTXNv1i64 : SIMDInexactCvtTwoScalar<0b10110, "fcvtxn">;
defm FCVTZS : SIMDFPTwoScalar<   0, 1, 0b11011, "fcvtzs">;
defm FCVTZU : SIMDFPTwoScalar<   1, 1, 0b11011, "fcvtzu">;
defm FRECPE : SIMDFPTwoScalar<   0, 1, 0b11101, "frecpe">;
defm FRECPX : SIMDFPTwoScalar<   0, 1, 0b11111, "frecpx">;
defm FRSQRTE : SIMDFPTwoScalar<  1, 1, 0b11101, "frsqrte">;
defm NEG    : SIMDTwoScalarD<    1, 0b01011, "neg",
                                 UnOpFrag<(sub immAllZerosV, node:$LHS)> >;
defm SCVTF  : SIMDFPTwoScalarCVT<   0, 0, 0b11101, "scvtf", AArch64sitof>;
defm SQABS  : SIMDTwoScalarBHSD< 0, 0b00111, "sqabs", int_aarch64_neon_sqabs>;
defm SQNEG  : SIMDTwoScalarBHSD< 1, 0b00111, "sqneg", int_aarch64_neon_sqneg>;
defm SQXTN  : SIMDTwoScalarMixedBHS< 0, 0b10100, "sqxtn", int_aarch64_neon_scalar_sqxtn>;
defm SQXTUN : SIMDTwoScalarMixedBHS< 1, 0b10010, "sqxtun", int_aarch64_neon_scalar_sqxtun>;
defm SUQADD : SIMDTwoScalarBHSDTied< 0, 0b00011, "suqadd",
                                     int_aarch64_neon_suqadd>;
defm UCVTF  : SIMDFPTwoScalarCVT<   1, 0, 0b11101, "ucvtf", AArch64uitof>;
defm UQXTN  : SIMDTwoScalarMixedBHS<1, 0b10100, "uqxtn", int_aarch64_neon_scalar_uqxtn>;
defm USQADD : SIMDTwoScalarBHSDTied< 1, 0b00011, "usqadd",
                                    int_aarch64_neon_usqadd>;

def : Pat<(v1i64 (AArch64vashr (v1i64 V64:$Rn), (i32 63))),
          (CMLTv1i64rz V64:$Rn)>;

// Round FP64 to BF16.
let Predicates = [HasNEONandIsStreamingSafe, HasBF16] in
def : Pat<(bf16 (any_fpround (f64 FPR64:$Rn))),
          (BFCVT (FCVTXNv1i64 $Rn))>;

def : Pat<(v1i64 (int_aarch64_neon_fcvtas (v1f64 FPR64:$Rn))),
          (FCVTASv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtau (v1f64 FPR64:$Rn))),
          (FCVTAUv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtms (v1f64 FPR64:$Rn))),
          (FCVTMSv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtmu (v1f64 FPR64:$Rn))),
          (FCVTMUv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtns (v1f64 FPR64:$Rn))),
          (FCVTNSv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtnu (v1f64 FPR64:$Rn))),
          (FCVTNUv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtps (v1f64 FPR64:$Rn))),
          (FCVTPSv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtpu (v1f64 FPR64:$Rn))),
          (FCVTPUv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtzs (v1f64 FPR64:$Rn))),
          (FCVTZSv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtzu (v1f64 FPR64:$Rn))),
          (FCVTZUv1i64 FPR64:$Rn)>;

def : Pat<(f16 (int_aarch64_neon_frecpe (f16 FPR16:$Rn))),
          (FRECPEv1f16 FPR16:$Rn)>;
def : Pat<(f32 (int_aarch64_neon_frecpe (f32 FPR32:$Rn))),
          (FRECPEv1i32 FPR32:$Rn)>;
def : Pat<(f64 (int_aarch64_neon_frecpe (f64 FPR64:$Rn))),
          (FRECPEv1i64 FPR64:$Rn)>;
def : Pat<(v1f64 (int_aarch64_neon_frecpe (v1f64 FPR64:$Rn))),
          (FRECPEv1i64 FPR64:$Rn)>;

def : Pat<(f32 (AArch64frecpe (f32 FPR32:$Rn))),
          (FRECPEv1i32 FPR32:$Rn)>;
def : Pat<(v2f32 (AArch64frecpe (v2f32 V64:$Rn))),
          (FRECPEv2f32 V64:$Rn)>;
def : Pat<(v4f32 (AArch64frecpe (v4f32 FPR128:$Rn))),
          (FRECPEv4f32 FPR128:$Rn)>;
def : Pat<(f64 (AArch64frecpe (f64 FPR64:$Rn))),
          (FRECPEv1i64 FPR64:$Rn)>;
def : Pat<(v1f64 (AArch64frecpe (v1f64 FPR64:$Rn))),
          (FRECPEv1i64 FPR64:$Rn)>;
def : Pat<(v2f64 (AArch64frecpe (v2f64 FPR128:$Rn))),
          (FRECPEv2f64 FPR128:$Rn)>;

def : Pat<(f32 (AArch64frecps (f32 FPR32:$Rn), (f32 FPR32:$Rm))),
          (FRECPS32 FPR32:$Rn, FPR32:$Rm)>;
def : Pat<(v2f32 (AArch64frecps (v2f32 V64:$Rn), (v2f32 V64:$Rm))),
          (FRECPSv2f32 V64:$Rn, V64:$Rm)>;
def : Pat<(v4f32 (AArch64frecps (v4f32 FPR128:$Rn), (v4f32 FPR128:$Rm))),
          (FRECPSv4f32 FPR128:$Rn, FPR128:$Rm)>;
def : Pat<(f64 (AArch64frecps (f64 FPR64:$Rn), (f64 FPR64:$Rm))),
          (FRECPS64 FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(v2f64 (AArch64frecps (v2f64 FPR128:$Rn), (v2f64 FPR128:$Rm))),
          (FRECPSv2f64 FPR128:$Rn, FPR128:$Rm)>;

def : Pat<(f16 (int_aarch64_neon_frecpx (f16 FPR16:$Rn))),
          (FRECPXv1f16 FPR16:$Rn)>;
def : Pat<(f32 (int_aarch64_neon_frecpx (f32 FPR32:$Rn))),
          (FRECPXv1i32 FPR32:$Rn)>;
def : Pat<(f64 (int_aarch64_neon_frecpx (f64 FPR64:$Rn))),
          (FRECPXv1i64 FPR64:$Rn)>;

def : Pat<(f16 (int_aarch64_neon_frsqrte (f16 FPR16:$Rn))),
          (FRSQRTEv1f16 FPR16:$Rn)>;
def : Pat<(f32 (int_aarch64_neon_frsqrte (f32 FPR32:$Rn))),
          (FRSQRTEv1i32 FPR32:$Rn)>;
def : Pat<(f64 (int_aarch64_neon_frsqrte (f64 FPR64:$Rn))),
          (FRSQRTEv1i64 FPR64:$Rn)>;
def : Pat<(v1f64 (int_aarch64_neon_frsqrte (v1f64 FPR64:$Rn))),
          (FRSQRTEv1i64 FPR64:$Rn)>;

def : Pat<(f32 (AArch64frsqrte (f32 FPR32:$Rn))),
          (FRSQRTEv1i32 FPR32:$Rn)>;
def : Pat<(v2f32 (AArch64frsqrte (v2f32 V64:$Rn))),
          (FRSQRTEv2f32 V64:$Rn)>;
def : Pat<(v4f32 (AArch64frsqrte (v4f32 FPR128:$Rn))),
          (FRSQRTEv4f32 FPR128:$Rn)>;
def : Pat<(f64 (AArch64frsqrte (f64 FPR64:$Rn))),
          (FRSQRTEv1i64 FPR64:$Rn)>;
def : Pat<(v1f64 (AArch64frsqrte (v1f64 FPR64:$Rn))),
          (FRSQRTEv1i64 FPR64:$Rn)>;
def : Pat<(v2f64 (AArch64frsqrte (v2f64 FPR128:$Rn))),
          (FRSQRTEv2f64 FPR128:$Rn)>;

def : Pat<(f32 (AArch64frsqrts (f32 FPR32:$Rn), (f32 FPR32:$Rm))),
          (FRSQRTS32 FPR32:$Rn, FPR32:$Rm)>;
def : Pat<(v2f32 (AArch64frsqrts (v2f32 V64:$Rn), (v2f32 V64:$Rm))),
          (FRSQRTSv2f32 V64:$Rn, V64:$Rm)>;
def : Pat<(v4f32 (AArch64frsqrts (v4f32 FPR128:$Rn), (v4f32 FPR128:$Rm))),
          (FRSQRTSv4f32 FPR128:$Rn, FPR128:$Rm)>;
def : Pat<(f64 (AArch64frsqrts (f64 FPR64:$Rn), (f64 FPR64:$Rm))),
          (FRSQRTS64 FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(v2f64 (AArch64frsqrts (v2f64 FPR128:$Rn), (v2f64 FPR128:$Rm))),
          (FRSQRTSv2f64 FPR128:$Rn, FPR128:$Rm)>;

// Some float -> int -> float conversion patterns for which we want to keep the
// int values in FP registers using the corresponding NEON instructions to
// avoid more costly int <-> fp register transfers.
let Predicates = [HasNEONandIsStreamingSafe] in {
def : Pat<(f64 (any_sint_to_fp (i64 (any_fp_to_sint f64:$Rn)))),
          (SCVTFv1i64 (i64 (FCVTZSv1i64 f64:$Rn)))>;
def : Pat<(f32 (any_sint_to_fp (i32 (any_fp_to_sint f32:$Rn)))),
          (SCVTFv1i32 (i32 (FCVTZSv1i32 f32:$Rn)))>;
def : Pat<(f64 (any_uint_to_fp (i64 (any_fp_to_uint f64:$Rn)))),
          (UCVTFv1i64 (i64 (FCVTZUv1i64 f64:$Rn)))>;
def : Pat<(f32 (any_uint_to_fp (i32 (any_fp_to_uint f32:$Rn)))),
          (UCVTFv1i32 (i32 (FCVTZUv1i32 f32:$Rn)))>;

let Predicates = [HasNEONandIsStreamingSafe, HasFullFP16] in {
def : Pat<(f16 (any_sint_to_fp (i32 (any_fp_to_sint f16:$Rn)))),
          (SCVTFv1i16 (f16 (FCVTZSv1f16 f16:$Rn)))>;
def : Pat<(f16 (any_uint_to_fp (i32 (any_fp_to_uint f16:$Rn)))),
          (UCVTFv1i16 (f16 (FCVTZUv1f16 f16:$Rn)))>;
}

// int -> float conversion of value in lane 0 of simd vector should use 
// correct cvtf variant to avoid costly fpr <-> gpr register transfers.
def : Pat<(f32 (sint_to_fp (i32 (vector_extract (v4i32 FPR128:$Rn), (i64 0))))),
          (SCVTFv1i32 (i32 (EXTRACT_SUBREG (v4i32 FPR128:$Rn), ssub)))>;

def : Pat<(f32 (uint_to_fp (i32 (vector_extract (v4i32 FPR128:$Rn), (i64 0))))),
          (UCVTFv1i32 (i32 (EXTRACT_SUBREG (v4i32 FPR128:$Rn), ssub)))>;

def : Pat<(f64 (sint_to_fp (i64 (vector_extract (v2i64 FPR128:$Rn), (i64 0))))),
          (SCVTFv1i64 (i64 (EXTRACT_SUBREG (v2i64 FPR128:$Rn), dsub)))>;

def : Pat<(f64 (uint_to_fp (i64 (vector_extract (v2i64 FPR128:$Rn), (i64 0))))),
          (UCVTFv1i64 (i64 (EXTRACT_SUBREG (v2i64 FPR128:$Rn), dsub)))>;

// fp16: integer extraction from vector must be at least 32-bits to be legal.
// Actual extraction result is then an in-reg sign-extension of lower 16-bits.
let Predicates = [HasNEONandIsStreamingSafe, HasFullFP16] in {
def : Pat<(f16 (sint_to_fp (i32 (sext_inreg (i32 (vector_extract 
                (v8i16 FPR128:$Rn), (i64 0))), i16)))), 
          (SCVTFv1i16 (f16 (EXTRACT_SUBREG (v8i16 FPR128:$Rn), hsub)))>;

// unsigned 32-bit extracted element is truncated to 16-bits using AND
def : Pat<(f16 (uint_to_fp (i32 (and (i32 (vector_extract 
                (v8i16 FPR128:$Rn), (i64 0))), (i32 65535))))),
          (UCVTFv1i16 (f16 (EXTRACT_SUBREG (v8i16 FPR128:$Rn), hsub)))>;
}

// If an integer is about to be converted to a floating point value,
// just load it on the floating point unit.
// Here are the patterns for 8 and 16-bits to float.
// 8-bits -> float.
multiclass UIntToFPROLoadPat<ValueType DstTy, ValueType SrcTy,
                             SDPatternOperator loadop, Instruction UCVTF,
                             ROAddrMode ro, Instruction LDRW, Instruction LDRX,
                             SubRegIndex sub> {
  def : Pat<(DstTy (uint_to_fp (SrcTy
                     (loadop (ro.Wpat GPR64sp:$Rn, GPR32:$Rm,
                                      ro.Wext:$extend))))),
           (UCVTF (INSERT_SUBREG (DstTy (IMPLICIT_DEF)),
                                 (LDRW GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend),
                                 sub))>;

  def : Pat<(DstTy (uint_to_fp (SrcTy
                     (loadop (ro.Xpat GPR64sp:$Rn, GPR64:$Rm,
                                      ro.Wext:$extend))))),
           (UCVTF (INSERT_SUBREG (DstTy (IMPLICIT_DEF)),
                                 (LDRX GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend),
                                 sub))>;
}

defm : UIntToFPROLoadPat<f32, i32, zextloadi8,
                         UCVTFv1i32, ro8, LDRBroW, LDRBroX, bsub>;
def : Pat <(f32 (uint_to_fp (i32
               (zextloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))))),
           (UCVTFv1i32 (INSERT_SUBREG (f32 (IMPLICIT_DEF)),
                          (LDRBui GPR64sp:$Rn, uimm12s1:$offset), bsub))>;
def : Pat <(f32 (uint_to_fp (i32
                     (zextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))))),
           (UCVTFv1i32 (INSERT_SUBREG (f32 (IMPLICIT_DEF)),
                          (LDURBi GPR64sp:$Rn, simm9:$offset), bsub))>;
// 16-bits -> float.
defm : UIntToFPROLoadPat<f32, i32, zextloadi16,
                         UCVTFv1i32, ro16, LDRHroW, LDRHroX, hsub>;
def : Pat <(f32 (uint_to_fp (i32
                  (zextloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))))),
           (UCVTFv1i32 (INSERT_SUBREG (f32 (IMPLICIT_DEF)),
                          (LDRHui GPR64sp:$Rn, uimm12s2:$offset), hsub))>;
def : Pat <(f32 (uint_to_fp (i32
                  (zextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))))),
           (UCVTFv1i32 (INSERT_SUBREG (f32 (IMPLICIT_DEF)),
                          (LDURHi GPR64sp:$Rn, simm9:$offset), hsub))>;
// 32-bits are handled in target specific dag combine:
// performIntToFpCombine.
// 64-bits integer to 32-bits floating point, not possible with
// UCVTF on floating point registers (both source and destination
// must have the same size).

// Here are the patterns for 8, 16, 32, and 64-bits to double.
// 8-bits -> double.
defm : UIntToFPROLoadPat<f64, i32, zextloadi8,
                         UCVTFv1i64, ro8, LDRBroW, LDRBroX, bsub>;
def : Pat <(f64 (uint_to_fp (i32
                    (zextloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))))),
           (UCVTFv1i64 (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                          (LDRBui GPR64sp:$Rn, uimm12s1:$offset), bsub))>;
def : Pat <(f64 (uint_to_fp (i32
                  (zextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))))),
           (UCVTFv1i64 (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                          (LDURBi GPR64sp:$Rn, simm9:$offset), bsub))>;
// 16-bits -> double.
defm : UIntToFPROLoadPat<f64, i32, zextloadi16,
                         UCVTFv1i64, ro16, LDRHroW, LDRHroX, hsub>;
def : Pat <(f64 (uint_to_fp (i32
                  (zextloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))))),
           (UCVTFv1i64 (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                          (LDRHui GPR64sp:$Rn, uimm12s2:$offset), hsub))>;
def : Pat <(f64 (uint_to_fp (i32
                  (zextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))))),
           (UCVTFv1i64 (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                          (LDURHi GPR64sp:$Rn, simm9:$offset), hsub))>;
// 32-bits -> double.
defm : UIntToFPROLoadPat<f64, i32, load,
                         UCVTFv1i64, ro32, LDRSroW, LDRSroX, ssub>;
def : Pat <(f64 (uint_to_fp (i32
                  (load (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))))),
           (UCVTFv1i64 (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                          (LDRSui GPR64sp:$Rn, uimm12s4:$offset), ssub))>;
def : Pat <(f64 (uint_to_fp (i32
                  (load (am_unscaled32 GPR64sp:$Rn, simm9:$offset))))),
           (UCVTFv1i64 (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                          (LDURSi GPR64sp:$Rn, simm9:$offset), ssub))>;
// 64-bits -> double are handled in target specific dag combine:
// performIntToFpCombine.
} // let Predicates = [HasNEONandIsStreamingSafe]

//===----------------------------------------------------------------------===//
// Advanced SIMD three different-sized vector instructions.
//===----------------------------------------------------------------------===//

defm ADDHN  : SIMDNarrowThreeVectorBHS<0,0b0100,"addhn", int_aarch64_neon_addhn>;
defm SUBHN  : SIMDNarrowThreeVectorBHS<0,0b0110,"subhn", int_aarch64_neon_subhn>;
defm RADDHN : SIMDNarrowThreeVectorBHS<1,0b0100,"raddhn",int_aarch64_neon_raddhn>;
defm RSUBHN : SIMDNarrowThreeVectorBHS<1,0b0110,"rsubhn",int_aarch64_neon_rsubhn>;
defm PMULL  : SIMDDifferentThreeVectorBD<0,0b1110,"pmull", AArch64pmull>;
defm SABAL  : SIMDLongThreeVectorTiedBHSabal<0,0b0101,"sabal",
                                             AArch64sabd>;
defm SABDL   : SIMDLongThreeVectorBHSabdl<0, 0b0111, "sabdl",
                                          AArch64sabd>;
defm SADDL   : SIMDLongThreeVectorBHS<   0, 0b0000, "saddl",
            BinOpFrag<(add (sext node:$LHS), (sext node:$RHS))>>;
defm SADDW   : SIMDWideThreeVectorBHS<   0, 0b0001, "saddw",
                 BinOpFrag<(add node:$LHS, (sext node:$RHS))>>;
defm SMLAL   : SIMDLongThreeVectorTiedBHS<0, 0b1000, "smlal",
    TriOpFrag<(add node:$LHS, (AArch64smull node:$MHS, node:$RHS))>>;
defm SMLSL   : SIMDLongThreeVectorTiedBHS<0, 0b1010, "smlsl",
    TriOpFrag<(sub node:$LHS, (AArch64smull node:$MHS, node:$RHS))>>;
defm SMULL   : SIMDLongThreeVectorBHS<0, 0b1100, "smull", AArch64smull>;
defm SQDMLAL : SIMDLongThreeVectorSQDMLXTiedHS<0, 0b1001, "sqdmlal",
                                               int_aarch64_neon_sqadd>;
defm SQDMLSL : SIMDLongThreeVectorSQDMLXTiedHS<0, 0b1011, "sqdmlsl",
                                               int_aarch64_neon_sqsub>;
defm SQDMULL : SIMDLongThreeVectorHS<0, 0b1101, "sqdmull",
                                     int_aarch64_neon_sqdmull>;
defm SSUBL   : SIMDLongThreeVectorBHS<0, 0b0010, "ssubl",
                 BinOpFrag<(sub (sext node:$LHS), (sext node:$RHS))>>;
defm SSUBW   : SIMDWideThreeVectorBHS<0, 0b0011, "ssubw",
                 BinOpFrag<(sub node:$LHS, (sext node:$RHS))>>;
defm UABAL   : SIMDLongThreeVectorTiedBHSabal<1, 0b0101, "uabal",
                                              AArch64uabd>;
defm UADDL   : SIMDLongThreeVectorBHS<1, 0b0000, "uaddl",
                 BinOpFrag<(add (zanyext node:$LHS), (zanyext node:$RHS))>>;
defm UADDW   : SIMDWideThreeVectorBHS<1, 0b0001, "uaddw",
                 BinOpFrag<(add node:$LHS, (zanyext node:$RHS))>>;
defm UMLAL   : SIMDLongThreeVectorTiedBHS<1, 0b1000, "umlal",
    TriOpFrag<(add node:$LHS, (AArch64umull node:$MHS, node:$RHS))>>;
defm UMLSL   : SIMDLongThreeVectorTiedBHS<1, 0b1010, "umlsl",
    TriOpFrag<(sub node:$LHS, (AArch64umull node:$MHS, node:$RHS))>>;
defm UMULL   : SIMDLongThreeVectorBHS<1, 0b1100, "umull", AArch64umull>;
defm USUBL   : SIMDLongThreeVectorBHS<1, 0b0010, "usubl",
                 BinOpFrag<(sub (zanyext node:$LHS), (zanyext node:$RHS))>>;
defm USUBW   : SIMDWideThreeVectorBHS<   1, 0b0011, "usubw",
                 BinOpFrag<(sub node:$LHS, (zanyext node:$RHS))>>;

// Additional patterns for [SU]ML[AS]L
multiclass Neon_mul_acc_widen_patterns<SDPatternOperator opnode, SDPatternOperator vecopnode,
  Instruction INST8B, Instruction INST4H, Instruction INST2S> {
  def : Pat<(v4i16 (opnode
                    V64:$Ra,
                    (v4i16 (extract_subvector
                            (vecopnode (v8i8 V64:$Rn),(v8i8 V64:$Rm)),
                            (i64 0))))),
             (EXTRACT_SUBREG (v8i16 (INST8B
                                     (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)), V64:$Ra, dsub),
                                     V64:$Rn, V64:$Rm)), dsub)>;
  def : Pat<(v2i32 (opnode
                    V64:$Ra,
                    (v2i32 (extract_subvector
                            (vecopnode (v4i16 V64:$Rn),(v4i16 V64:$Rm)),
                            (i64 0))))),
             (EXTRACT_SUBREG (v4i32 (INST4H
                                     (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), V64:$Ra, dsub),
                                     V64:$Rn, V64:$Rm)), dsub)>;
  def : Pat<(v1i64 (opnode
                    V64:$Ra,
                    (v1i64 (extract_subvector
                            (vecopnode (v2i32 V64:$Rn),(v2i32 V64:$Rm)),
                            (i64 0))))),
             (EXTRACT_SUBREG (v2i64 (INST2S
                                     (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)), V64:$Ra, dsub),
                                     V64:$Rn, V64:$Rm)), dsub)>;
}

defm : Neon_mul_acc_widen_patterns<add, AArch64umull,
     UMLALv8i8_v8i16, UMLALv4i16_v4i32, UMLALv2i32_v2i64>;
defm : Neon_mul_acc_widen_patterns<add, AArch64smull,
     SMLALv8i8_v8i16, SMLALv4i16_v4i32, SMLALv2i32_v2i64>;
defm : Neon_mul_acc_widen_patterns<sub, AArch64umull,
     UMLSLv8i8_v8i16, UMLSLv4i16_v4i32, UMLSLv2i32_v2i64>;
defm : Neon_mul_acc_widen_patterns<sub, AArch64smull,
     SMLSLv8i8_v8i16, SMLSLv4i16_v4i32, SMLSLv2i32_v2i64>;


multiclass Neon_addl_extract_patterns<SDPatternOperator opnode, SDPatternOperator ext, string Inst> {
  def : Pat<(v4i16 (opnode (extract_subvector (ext (v8i8 V64:$Rn)), (i64 0)),
                           (extract_subvector (ext (v8i8 V64:$Rm)), (i64 0)))),
            (EXTRACT_SUBREG (v8i16 (!cast<Instruction>(Inst#"Lv8i8_v8i16") V64:$Rn, V64:$Rm)), dsub)>;
  def : Pat<(v2i32 (opnode (extract_subvector (ext (v4i16 V64:$Rn)), (i64 0)),
                           (extract_subvector (ext (v4i16 V64:$Rm)), (i64 0)))),
            (EXTRACT_SUBREG (v4i32 (!cast<Instruction>(Inst#"Lv4i16_v4i32") V64:$Rn, V64:$Rm)), dsub)>;
  def : Pat<(v1i64 (opnode (extract_subvector (ext (v2i32 V64:$Rn)), (i64 0)),
                           (extract_subvector (ext (v2i32 V64:$Rm)), (i64 0)))),
            (EXTRACT_SUBREG (v2i64 (!cast<Instruction>(Inst#"Lv2i32_v2i64") V64:$Rn, V64:$Rm)), dsub)>;

  def : Pat<(v4i16 (opnode (v4i16 V64:$Rn),
                           (extract_subvector (ext (v8i8 V64:$Rm)), (i64 0)))),
            (EXTRACT_SUBREG (v8i16 (!cast<Instruction>(Inst#"Wv8i8_v8i16") (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)), V64:$Rn, dsub), V64:$Rm)), dsub)>;
  def : Pat<(v2i32 (opnode (v2i32 V64:$Rn),
                           (extract_subvector (ext (v4i16 V64:$Rm)), (i64 0)))),
            (EXTRACT_SUBREG (v4i32 (!cast<Instruction>(Inst#"Wv4i16_v4i32") (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), V64:$Rn, dsub), V64:$Rm)), dsub)>;
  def : Pat<(v1i64 (opnode (v1i64 V64:$Rn),
                           (extract_subvector (ext (v2i32 V64:$Rm)), (i64 0)))),
            (EXTRACT_SUBREG (v2i64 (!cast<Instruction>(Inst#"Wv2i32_v2i64") (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)), V64:$Rn, dsub), V64:$Rm)), dsub)>;
}

defm : Neon_addl_extract_patterns<add, zanyext, "UADD">;
defm : Neon_addl_extract_patterns<add, sext, "SADD">;
defm : Neon_addl_extract_patterns<sub, zanyext, "USUB">;
defm : Neon_addl_extract_patterns<sub, sext, "SSUB">;

// CodeGen patterns for addhn and subhn instructions, which can actually be
// written in LLVM IR without too much difficulty.

// Prioritize ADDHN and SUBHN over UZP2.
let AddedComplexity = 10 in {

// ADDHN
def : Pat<(v8i8 (trunc (v8i16 (AArch64vlshr (add V128:$Rn, V128:$Rm), (i32 8))))),
          (ADDHNv8i16_v8i8 V128:$Rn, V128:$Rm)>;
def : Pat<(v4i16 (trunc (v4i32 (AArch64vlshr (add V128:$Rn, V128:$Rm),
                                           (i32 16))))),
          (ADDHNv4i32_v4i16 V128:$Rn, V128:$Rm)>;
def : Pat<(v2i32 (trunc (v2i64 (AArch64vlshr (add V128:$Rn, V128:$Rm),
                                           (i32 32))))),
          (ADDHNv2i64_v2i32 V128:$Rn, V128:$Rm)>;
def : Pat<(concat_vectors (v8i8 V64:$Rd),
                          (trunc (v8i16 (AArch64vlshr (add V128:$Rn, V128:$Rm),
                                                    (i32 8))))),
          (ADDHNv8i16_v16i8 (SUBREG_TO_REG (i32 0), V64:$Rd, dsub),
                            V128:$Rn, V128:$Rm)>;
def : Pat<(concat_vectors (v4i16 V64:$Rd),
                          (trunc (v4i32 (AArch64vlshr (add V128:$Rn, V128:$Rm),
                                                    (i32 16))))),
          (ADDHNv4i32_v8i16 (SUBREG_TO_REG (i32 0), V64:$Rd, dsub),
                            V128:$Rn, V128:$Rm)>;
def : Pat<(concat_vectors (v2i32 V64:$Rd),
                          (trunc (v2i64 (AArch64vlshr (add V128:$Rn, V128:$Rm),
                                                    (i32 32))))),
          (ADDHNv2i64_v4i32 (SUBREG_TO_REG (i32 0), V64:$Rd, dsub),
                            V128:$Rn, V128:$Rm)>;

// SUBHN
def : Pat<(v8i8 (trunc (v8i16 (AArch64vlshr (sub V128:$Rn, V128:$Rm), (i32 8))))),
          (SUBHNv8i16_v8i8 V128:$Rn, V128:$Rm)>;
def : Pat<(v4i16 (trunc (v4i32 (AArch64vlshr (sub V128:$Rn, V128:$Rm),
                                           (i32 16))))),
          (SUBHNv4i32_v4i16 V128:$Rn, V128:$Rm)>;
def : Pat<(v2i32 (trunc (v2i64 (AArch64vlshr (sub V128:$Rn, V128:$Rm),
                                           (i32 32))))),
          (SUBHNv2i64_v2i32 V128:$Rn, V128:$Rm)>;
def : Pat<(concat_vectors (v8i8 V64:$Rd),
                          (trunc (v8i16 (AArch64vlshr (sub V128:$Rn, V128:$Rm),
                                                    (i32 8))))),
          (SUBHNv8i16_v16i8 (SUBREG_TO_REG (i32 0), V64:$Rd, dsub),
                            V128:$Rn, V128:$Rm)>;
def : Pat<(concat_vectors (v4i16 V64:$Rd),
                          (trunc (v4i32 (AArch64vlshr (sub V128:$Rn, V128:$Rm),
                                                    (i32 16))))),
          (SUBHNv4i32_v8i16 (SUBREG_TO_REG (i32 0), V64:$Rd, dsub),
                            V128:$Rn, V128:$Rm)>;
def : Pat<(concat_vectors (v2i32 V64:$Rd),
                          (trunc (v2i64 (AArch64vlshr (sub V128:$Rn, V128:$Rm),
                                                    (i32 32))))),
          (SUBHNv2i64_v4i32 (SUBREG_TO_REG (i32 0), V64:$Rd, dsub),
                            V128:$Rn, V128:$Rm)>;

} // AddedComplexity = 10

//----------------------------------------------------------------------------
// AdvSIMD bitwise extract from vector instruction.
//----------------------------------------------------------------------------

defm EXT : SIMDBitwiseExtract<"ext">;

def AdjustExtImm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(8 + N->getZExtValue(), SDLoc(N), MVT::i32);
}]>;
multiclass ExtPat<ValueType VT64, ValueType VT128, int N> {
  def : Pat<(VT64 (AArch64ext V64:$Rn, V64:$Rm, (i32 imm:$imm))),
            (EXTv8i8 V64:$Rn, V64:$Rm, imm:$imm)>;
  def : Pat<(VT128 (AArch64ext V128:$Rn, V128:$Rm, (i32 imm:$imm))),
            (EXTv16i8 V128:$Rn, V128:$Rm, imm:$imm)>;
  // We use EXT to handle extract_subvector to copy the upper 64-bits of a
  // 128-bit vector.
  def : Pat<(VT64 (extract_subvector V128:$Rn, (i64 N))),
            (EXTRACT_SUBREG (EXTv16i8 V128:$Rn, V128:$Rn, 8), dsub)>;
  // A 64-bit EXT of two halves of the same 128-bit register can be done as a
  // single 128-bit EXT.
  def : Pat<(VT64 (AArch64ext (extract_subvector V128:$Rn, (i64 0)),
                              (extract_subvector V128:$Rn, (i64 N)),
                              (i32 imm:$imm))),
            (EXTRACT_SUBREG (EXTv16i8 V128:$Rn, V128:$Rn, imm:$imm), dsub)>;
  // A 64-bit EXT of the high half of a 128-bit register can be done using a
  // 128-bit EXT of the whole register with an adjustment to the immediate. The
  // top half of the other operand will be unset, but that doesn't matter as it
  // will not be used.
  def : Pat<(VT64 (AArch64ext (extract_subvector V128:$Rn, (i64 N)),
                              V64:$Rm,
                              (i32 imm:$imm))),
            (EXTRACT_SUBREG (EXTv16i8 V128:$Rn,
                                      (SUBREG_TO_REG (i32 0), V64:$Rm, dsub),
                                      (AdjustExtImm imm:$imm)), dsub)>;
}

defm : ExtPat<v8i8, v16i8, 8>;
defm : ExtPat<v4i16, v8i16, 4>;
defm : ExtPat<v4f16, v8f16, 4>;
defm : ExtPat<v4bf16, v8bf16, 4>;
defm : ExtPat<v2i32, v4i32, 2>;
defm : ExtPat<v2f32, v4f32, 2>;
defm : ExtPat<v1i64, v2i64, 1>;
defm : ExtPat<v1f64, v2f64, 1>;

//----------------------------------------------------------------------------
// AdvSIMD zip vector
//----------------------------------------------------------------------------

defm TRN1 : SIMDZipVector<0b010, "trn1", AArch64trn1>;
defm TRN2 : SIMDZipVector<0b110, "trn2", AArch64trn2>;
defm UZP1 : SIMDZipVector<0b001, "uzp1", AArch64uzp1>;
defm UZP2 : SIMDZipVector<0b101, "uzp2", AArch64uzp2>;
defm ZIP1 : SIMDZipVector<0b011, "zip1", AArch64zip1>;
defm ZIP2 : SIMDZipVector<0b111, "zip2", AArch64zip2>;

def trunc_optional_assert_ext : PatFrags<(ops node:$op0),
                                         [(trunc node:$op0),
                                          (assertzext (trunc node:$op0)),
                                          (assertsext (trunc node:$op0))]>;

// concat_vectors(trunc(x), trunc(y)) -> uzp1(x, y)
// concat_vectors(assertzext(trunc(x)), assertzext(trunc(y))) -> uzp1(x, y)
// concat_vectors(assertsext(trunc(x)), assertsext(trunc(y))) -> uzp1(x, y)
class concat_trunc_to_uzp1_pat<ValueType SrcTy, ValueType TruncTy, ValueType ConcatTy>
  : Pat<(ConcatTy (concat_vectors (TruncTy (trunc_optional_assert_ext (SrcTy V128:$Vn))),
                                  (TruncTy (trunc_optional_assert_ext (SrcTy V128:$Vm))))),
        (!cast<Instruction>("UZP1"#ConcatTy) V128:$Vn, V128:$Vm)>;
def : concat_trunc_to_uzp1_pat<v8i16, v8i8, v16i8>;
def : concat_trunc_to_uzp1_pat<v4i32, v4i16, v8i16>;
def : concat_trunc_to_uzp1_pat<v2i64, v2i32, v4i32>;

// trunc(concat_vectors(trunc(x), trunc(y))) -> xtn(uzp1(x, y))
// trunc(concat_vectors(assertzext(trunc(x)), assertzext(trunc(y)))) -> xtn(uzp1(x, y))
// trunc(concat_vectors(assertsext(trunc(x)), assertsext(trunc(y)))) -> xtn(uzp1(x, y))
class trunc_concat_trunc_to_xtn_uzp1_pat<ValueType SrcTy, ValueType TruncTy, ValueType ConcatTy,
                                         ValueType Ty>
  : Pat<(Ty (trunc_optional_assert_ext
                    (ConcatTy (concat_vectors
                                  (TruncTy (trunc_optional_assert_ext (SrcTy V128:$Vn))),
                                  (TruncTy (trunc_optional_assert_ext (SrcTy V128:$Vm))))))),
        (!cast<Instruction>("XTN"#Ty) (!cast<Instruction>("UZP1"#ConcatTy) V128:$Vn, V128:$Vm))>;
def : trunc_concat_trunc_to_xtn_uzp1_pat<v4i32, v4i16, v8i16, v8i8>;
def : trunc_concat_trunc_to_xtn_uzp1_pat<v2i64, v2i32, v4i32, v4i16>;

def : Pat<(v8i8 (trunc (concat_vectors (v4i16 V64:$Vn), (v4i16 V64:$Vm)))),
          (UZP1v8i8 V64:$Vn, V64:$Vm)>;
def : Pat<(v4i16 (trunc (concat_vectors (v2i32 V64:$Vn), (v2i32 V64:$Vm)))),
          (UZP1v4i16 V64:$Vn, V64:$Vm)>;

def : Pat<(v16i8 (concat_vectors
                 (v8i8 (trunc (AArch64vlshr (v8i16 V128:$Vn), (i32 8)))),
                 (v8i8 (trunc (AArch64vlshr (v8i16 V128:$Vm), (i32 8)))))),
          (UZP2v16i8 V128:$Vn, V128:$Vm)>;
def : Pat<(v8i16 (concat_vectors
                 (v4i16 (trunc (AArch64vlshr (v4i32 V128:$Vn), (i32 16)))),
                 (v4i16 (trunc (AArch64vlshr (v4i32 V128:$Vm), (i32 16)))))),
          (UZP2v8i16 V128:$Vn, V128:$Vm)>;
def : Pat<(v4i32 (concat_vectors
                 (v2i32 (trunc (AArch64vlshr (v2i64 V128:$Vn), (i32 32)))),
                 (v2i32 (trunc (AArch64vlshr (v2i64 V128:$Vm), (i32 32)))))),
          (UZP2v4i32 V128:$Vn, V128:$Vm)>;

//----------------------------------------------------------------------------
// AdvSIMD TBL/TBX instructions
//----------------------------------------------------------------------------

defm TBL : SIMDTableLookup<    0, "tbl">;
defm TBX : SIMDTableLookupTied<1, "tbx">;

def : Pat<(v8i8 (int_aarch64_neon_tbl1 (v16i8 VecListOne128:$Rn), (v8i8 V64:$Ri))),
          (TBLv8i8One VecListOne128:$Rn, V64:$Ri)>;
def : Pat<(v16i8 (int_aarch64_neon_tbl1 (v16i8 V128:$Ri), (v16i8 V128:$Rn))),
          (TBLv16i8One V128:$Ri, V128:$Rn)>;

def : Pat<(v8i8 (int_aarch64_neon_tbx1 (v8i8 V64:$Rd),
                  (v16i8 VecListOne128:$Rn), (v8i8 V64:$Ri))),
          (TBXv8i8One V64:$Rd, VecListOne128:$Rn, V64:$Ri)>;
def : Pat<(v16i8 (int_aarch64_neon_tbx1 (v16i8 V128:$Rd),
                   (v16i8 V128:$Ri), (v16i8 V128:$Rn))),
          (TBXv16i8One V128:$Rd, V128:$Ri, V128:$Rn)>;

//----------------------------------------------------------------------------
// AdvSIMD LUT instructions
//----------------------------------------------------------------------------
let Predicates = [HasLUT] in {
  defm LUT2 : BaseSIMDTableLookupIndexed2<"luti2">;
  defm LUT4 : BaseSIMDTableLookupIndexed4<"luti4">;
}

//----------------------------------------------------------------------------
// AdvSIMD scalar DUP instruction
//----------------------------------------------------------------------------

defm DUP : SIMDScalarDUP<"mov">;

//----------------------------------------------------------------------------
// AdvSIMD scalar pairwise instructions
//----------------------------------------------------------------------------

defm ADDP    : SIMDPairwiseScalarD<0, 0b11011, "addp">;
defm FADDP   : SIMDFPPairwiseScalar<0, 0b01101, "faddp">;
defm FMAXNMP : SIMDFPPairwiseScalar<0, 0b01100, "fmaxnmp">;
defm FMAXP   : SIMDFPPairwiseScalar<0, 0b01111, "fmaxp">;
defm FMINNMP : SIMDFPPairwiseScalar<1, 0b01100, "fminnmp">;
defm FMINP   : SIMDFPPairwiseScalar<1, 0b01111, "fminp">;

// Only the lower half of the result of the inner FADDP is used in the patterns
// below, so the second operand does not matter. Re-use the first input
// operand, so no additional dependencies need to be introduced.
let Predicates = [HasFullFP16] in {
def : Pat<(f16 (vecreduce_fadd (v8f16 V128:$Rn))),
            (FADDPv2i16p
              (EXTRACT_SUBREG
                 (FADDPv8f16 (FADDPv8f16 V128:$Rn, V128:$Rn), V128:$Rn),
               dsub))>;
def : Pat<(f16 (vecreduce_fadd (v4f16 V64:$Rn))),
          (FADDPv2i16p (FADDPv4f16 V64:$Rn, V64:$Rn))>;
}
def : Pat<(f32 (vecreduce_fadd (v4f32 V128:$Rn))),
          (FADDPv2i32p
            (EXTRACT_SUBREG
              (FADDPv4f32 V128:$Rn, V128:$Rn),
             dsub))>;
def : Pat<(f32 (vecreduce_fadd (v2f32 V64:$Rn))),
          (FADDPv2i32p V64:$Rn)>;
def : Pat<(f64 (vecreduce_fadd (v2f64 V128:$Rn))),
          (FADDPv2i64p V128:$Rn)>;

def : Pat<(v2i64 (AArch64saddv V128:$Rn)),
          (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)), (ADDPv2i64p V128:$Rn), dsub)>;
def : Pat<(v2i64 (AArch64uaddv V128:$Rn)),
          (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)), (ADDPv2i64p V128:$Rn), dsub)>;
def : Pat<(f32 (int_aarch64_neon_faddv (v2f32 V64:$Rn))),
          (FADDPv2i32p V64:$Rn)>;
def : Pat<(f32 (int_aarch64_neon_faddv (v4f32 V128:$Rn))),
          (FADDPv2i32p (EXTRACT_SUBREG (FADDPv4f32 V128:$Rn, V128:$Rn), dsub))>;
def : Pat<(f64 (int_aarch64_neon_faddv (v2f64 V128:$Rn))),
          (FADDPv2i64p V128:$Rn)>;
def : Pat<(f32 (AArch64fmaxnmv (v2f32 V64:$Rn))),
          (FMAXNMPv2i32p V64:$Rn)>;
def : Pat<(f64 (AArch64fmaxnmv (v2f64 V128:$Rn))),
          (FMAXNMPv2i64p V128:$Rn)>;
def : Pat<(f32 (AArch64fmaxv (v2f32 V64:$Rn))),
          (FMAXPv2i32p V64:$Rn)>;
def : Pat<(f64 (AArch64fmaxv (v2f64 V128:$Rn))),
          (FMAXPv2i64p V128:$Rn)>;
def : Pat<(f32 (AArch64fminnmv (v2f32 V64:$Rn))),
          (FMINNMPv2i32p V64:$Rn)>;
def : Pat<(f64 (AArch64fminnmv (v2f64 V128:$Rn))),
          (FMINNMPv2i64p V128:$Rn)>;
def : Pat<(f32 (AArch64fminv (v2f32 V64:$Rn))),
          (FMINPv2i32p V64:$Rn)>;
def : Pat<(f64 (AArch64fminv (v2f64 V128:$Rn))),
          (FMINPv2i64p V128:$Rn)>;

//----------------------------------------------------------------------------
// AdvSIMD INS/DUP instructions
//----------------------------------------------------------------------------

def DUPv8i8gpr  : SIMDDupFromMain<0, {?,?,?,?,1}, ".8b", v8i8, V64, GPR32>;
def DUPv16i8gpr : SIMDDupFromMain<1, {?,?,?,?,1}, ".16b", v16i8, V128, GPR32>;
def DUPv4i16gpr : SIMDDupFromMain<0, {?,?,?,1,0}, ".4h", v4i16, V64, GPR32>;
def DUPv8i16gpr : SIMDDupFromMain<1, {?,?,?,1,0}, ".8h", v8i16, V128, GPR32>;
def DUPv2i32gpr : SIMDDupFromMain<0, {?,?,1,0,0}, ".2s", v2i32, V64, GPR32>;
def DUPv4i32gpr : SIMDDupFromMain<1, {?,?,1,0,0}, ".4s", v4i32, V128, GPR32>;
def DUPv2i64gpr : SIMDDupFromMain<1, {?,1,0,0,0}, ".2d", v2i64, V128, GPR64>;

def DUPv2i64lane : SIMDDup64FromElement;
def DUPv2i32lane : SIMDDup32FromElement<0, ".2s", v2i32, V64>;
def DUPv4i32lane : SIMDDup32FromElement<1, ".4s", v4i32, V128>;
def DUPv4i16lane : SIMDDup16FromElement<0, ".4h", v4i16, V64>;
def DUPv8i16lane : SIMDDup16FromElement<1, ".8h", v8i16, V128>;
def DUPv8i8lane  : SIMDDup8FromElement <0, ".8b", v8i8, V64>;
def DUPv16i8lane : SIMDDup8FromElement <1, ".16b", v16i8, V128>;

// DUP from a 64-bit register to a 64-bit register is just a copy
def : Pat<(v1i64 (AArch64dup (i64 GPR64:$Rn))),
          (COPY_TO_REGCLASS GPR64:$Rn, FPR64)>;
def : Pat<(v1f64 (AArch64dup (f64 FPR64:$Rn))),
          (COPY_TO_REGCLASS FPR64:$Rn, FPR64)>;

def : Pat<(v2f32 (AArch64dup (f32 FPR32:$Rn))),
          (v2f32 (DUPv2i32lane
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR32:$Rn, ssub),
            (i64 0)))>;
def : Pat<(v4f32 (AArch64dup (f32 FPR32:$Rn))),
          (v4f32 (DUPv4i32lane
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR32:$Rn, ssub),
            (i64 0)))>;
def : Pat<(v2f64 (AArch64dup (f64 FPR64:$Rn))),
          (v2f64 (DUPv2i64lane
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR64:$Rn, dsub),
            (i64 0)))>;
def : Pat<(v4f16 (AArch64dup (f16 FPR16:$Rn))),
          (v4f16 (DUPv4i16lane
            (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)), FPR16:$Rn, hsub),
            (i64 0)))>;
def : Pat<(v4bf16 (AArch64dup (bf16 FPR16:$Rn))),
          (v4bf16 (DUPv4i16lane
            (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)), FPR16:$Rn, hsub),
            (i64 0)))>;
def : Pat<(v8f16 (AArch64dup (f16 FPR16:$Rn))),
          (v8f16 (DUPv8i16lane
            (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)), FPR16:$Rn, hsub),
            (i64 0)))>;
def : Pat<(v8bf16 (AArch64dup (bf16 FPR16:$Rn))),
          (v8bf16 (DUPv8i16lane
            (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)), FPR16:$Rn, hsub),
            (i64 0)))>;

def : Pat<(v4f16 (AArch64duplane16 (v8f16 V128:$Rn), VectorIndexH:$imm)),
          (DUPv4i16lane V128:$Rn, VectorIndexH:$imm)>;
def : Pat<(v8f16 (AArch64duplane16 (v8f16 V128:$Rn), VectorIndexH:$imm)),
          (DUPv8i16lane V128:$Rn, VectorIndexH:$imm)>;

def : Pat<(v4bf16 (AArch64duplane16 (v8bf16 V128:$Rn), VectorIndexH:$imm)),
          (DUPv4i16lane V128:$Rn, VectorIndexH:$imm)>;
def : Pat<(v8bf16 (AArch64duplane16 (v8bf16 V128:$Rn), VectorIndexH:$imm)),
          (DUPv8i16lane V128:$Rn, VectorIndexH:$imm)>;

def : Pat<(v2f32 (AArch64duplane32 (v4f32 V128:$Rn), VectorIndexS:$imm)),
          (DUPv2i32lane V128:$Rn, VectorIndexS:$imm)>;
def : Pat<(v4f32 (AArch64duplane32 (v4f32 V128:$Rn), VectorIndexS:$imm)),
         (DUPv4i32lane V128:$Rn, VectorIndexS:$imm)>;
def : Pat<(v2f64 (AArch64duplane64 (v2f64 V128:$Rn), VectorIndexD:$imm)),
          (DUPv2i64lane V128:$Rn, VectorIndexD:$imm)>;

// If there's an (AArch64dup (vector_extract ...) ...), we can use a duplane
// instruction even if the types don't match: we just have to remap the lane
// carefully. N.b. this trick only applies to truncations.
def VecIndex_x2 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(2 * N->getZExtValue(), SDLoc(N), MVT::i64);
}]>;
def VecIndex_x4 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(4 * N->getZExtValue(), SDLoc(N), MVT::i64);
}]>;
def VecIndex_x8 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(8 * N->getZExtValue(), SDLoc(N), MVT::i64);
}]>;

multiclass DUPWithTruncPats<ValueType ResVT, ValueType Src64VT,
                            ValueType Src128VT, ValueType ScalVT,
                            Instruction DUP, SDNodeXForm IdxXFORM> {
  def : Pat<(ResVT (AArch64dup (ScalVT (vector_extract (Src128VT V128:$Rn),
                                                     imm:$idx)))),
            (DUP V128:$Rn, (IdxXFORM imm:$idx))>;

  def : Pat<(ResVT (AArch64dup (ScalVT (vector_extract (Src64VT V64:$Rn),
                                                     imm:$idx)))),
            (DUP (SUBREG_TO_REG (i64 0), V64:$Rn, dsub), (IdxXFORM imm:$idx))>;
}

defm : DUPWithTruncPats<v8i8,   v4i16, v8i16, i32, DUPv8i8lane,  VecIndex_x2>;
defm : DUPWithTruncPats<v8i8,   v2i32, v4i32, i32, DUPv8i8lane,  VecIndex_x4>;
defm : DUPWithTruncPats<v4i16,  v2i32, v4i32, i32, DUPv4i16lane, VecIndex_x2>;

defm : DUPWithTruncPats<v16i8,  v4i16, v8i16, i32, DUPv16i8lane, VecIndex_x2>;
defm : DUPWithTruncPats<v16i8,  v2i32, v4i32, i32, DUPv16i8lane, VecIndex_x4>;
defm : DUPWithTruncPats<v8i16,  v2i32, v4i32, i32, DUPv8i16lane, VecIndex_x2>;

multiclass DUPWithTrunci64Pats<ValueType ResVT, Instruction DUP,
                               SDNodeXForm IdxXFORM> {
  def : Pat<(ResVT (AArch64dup (i32 (trunc (extractelt (v2i64 V128:$Rn),
                                                         imm:$idx))))),
            (DUP V128:$Rn, (IdxXFORM imm:$idx))>;

  def : Pat<(ResVT (AArch64dup (i32 (trunc (extractelt (v1i64 V64:$Rn),
                                                       imm:$idx))))),
            (DUP (SUBREG_TO_REG (i64 0), V64:$Rn, dsub), (IdxXFORM imm:$idx))>;
}

defm : DUPWithTrunci64Pats<v8i8,  DUPv8i8lane,   VecIndex_x8>;
defm : DUPWithTrunci64Pats<v4i16, DUPv4i16lane,  VecIndex_x4>;
defm : DUPWithTrunci64Pats<v2i32, DUPv2i32lane,  VecIndex_x2>;

defm : DUPWithTrunci64Pats<v16i8, DUPv16i8lane, VecIndex_x8>;
defm : DUPWithTrunci64Pats<v8i16, DUPv8i16lane, VecIndex_x4>;
defm : DUPWithTrunci64Pats<v4i32, DUPv4i32lane, VecIndex_x2>;

// SMOV and UMOV definitions, with some extra patterns for convenience
defm SMOV : SMov;
defm UMOV : UMov;

def : Pat<(sext_inreg (vector_extract (v16i8 V128:$Rn), VectorIndexB:$idx), i8),
          (i32 (SMOVvi8to32 V128:$Rn, VectorIndexB:$idx))>;
def : Pat<(sext_inreg (vector_extract (v16i8 V128:$Rn), VectorIndexB:$idx), i8),
          (i64 (SMOVvi8to64 V128:$Rn, VectorIndexB:$idx))>;
def : Pat<(sext_inreg (vector_extract (v8i16 V128:$Rn), VectorIndexH:$idx),i16),
          (i32 (SMOVvi16to32 V128:$Rn, VectorIndexH:$idx))>;
def : Pat<(sext_inreg (vector_extract (v8i16 V128:$Rn), VectorIndexH:$idx),i16),
          (i64 (SMOVvi16to64 V128:$Rn, VectorIndexH:$idx))>;
def : Pat<(sext_inreg (vector_extract (v8i16 V128:$Rn), VectorIndexH:$idx),i16),
          (i32 (SMOVvi16to32 V128:$Rn, VectorIndexH:$idx))>;
def : Pat<(sext (i32 (vector_extract (v4i32 V128:$Rn), VectorIndexS:$idx))),
          (i64 (SMOVvi32to64 V128:$Rn, VectorIndexS:$idx))>;

def : Pat<(sext_inreg (i64 (anyext (i32 (vector_extract (v16i8 V128:$Rn),
            VectorIndexB:$idx)))), i8),
          (i64 (SMOVvi8to64 V128:$Rn, VectorIndexB:$idx))>;
def : Pat<(sext_inreg (i64 (anyext (i32 (vector_extract (v8i16 V128:$Rn),
            VectorIndexH:$idx)))), i16),
          (i64 (SMOVvi16to64 V128:$Rn, VectorIndexH:$idx))>;

// Extracting i8 or i16 elements will have the zero-extend transformed to
// an 'and' mask by type legalization since neither i8 nor i16 are legal types
// for AArch64. Match these patterns here since UMOV already zeroes out the high
// bits of the destination register.
def : Pat<(and (vector_extract (v16i8 V128:$Rn), VectorIndexB:$idx),
               (i32 0xff)),
          (i32 (UMOVvi8 V128:$Rn, VectorIndexB:$idx))>;
def : Pat<(and (vector_extract (v8i16 V128:$Rn), VectorIndexH:$idx),
               (i32 0xffff)),
          (i32 (UMOVvi16 V128:$Rn, VectorIndexH:$idx))>;

def : Pat<(i64 (and (i64 (anyext (i32 (vector_extract (v16i8 V128:$Rn),
            VectorIndexB:$idx)))), (i64 0xff))),
          (SUBREG_TO_REG (i64 0), (i32 (UMOVvi8 V128:$Rn, VectorIndexB:$idx)), sub_32)>;
def : Pat<(i64 (and (i64 (anyext (i32 (vector_extract (v8i16 V128:$Rn),
            VectorIndexH:$idx)))), (i64 0xffff))),
          (SUBREG_TO_REG (i64 0), (i32 (UMOVvi16 V128:$Rn, VectorIndexH:$idx)), sub_32)>;

defm INS : SIMDIns;

def : Pat<(v16i8 (scalar_to_vector GPR32:$Rn)),
          (SUBREG_TO_REG (i32 0),
                         (f32 (COPY_TO_REGCLASS GPR32:$Rn, FPR32)), ssub)>;
def : Pat<(v8i8 (scalar_to_vector GPR32:$Rn)),
          (SUBREG_TO_REG (i32 0),
                         (f32 (COPY_TO_REGCLASS GPR32:$Rn, FPR32)), ssub)>;

// The top bits will be zero from the FMOVWSr
def : Pat<(v8i8 (bitconvert (i64 (zext GPR32:$Rn)))),
          (SUBREG_TO_REG (i32 0), (f32 (FMOVWSr GPR32:$Rn)), ssub)>;

def : Pat<(v8i16 (scalar_to_vector GPR32:$Rn)),
          (SUBREG_TO_REG (i32 0),
                         (f32 (COPY_TO_REGCLASS GPR32:$Rn, FPR32)), ssub)>;
def : Pat<(v4i16 (scalar_to_vector GPR32:$Rn)),
          (SUBREG_TO_REG (i32 0),
                         (f32 (COPY_TO_REGCLASS GPR32:$Rn, FPR32)), ssub)>;

def : Pat<(v4f16 (scalar_to_vector (f16 FPR16:$Rn))),
          (INSERT_SUBREG (v4f16 (IMPLICIT_DEF)), FPR16:$Rn, hsub)>;
def : Pat<(v8f16 (scalar_to_vector (f16 FPR16:$Rn))),
          (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), FPR16:$Rn, hsub)>;

def : Pat<(v4bf16 (scalar_to_vector (bf16 FPR16:$Rn))),
          (INSERT_SUBREG (v4bf16 (IMPLICIT_DEF)), FPR16:$Rn, hsub)>;
def : Pat<(v8bf16 (scalar_to_vector (bf16 FPR16:$Rn))),
          (INSERT_SUBREG (v8bf16 (IMPLICIT_DEF)), FPR16:$Rn, hsub)>;

def : Pat<(v2i32 (scalar_to_vector (i32 FPR32:$Rn))),
            (v2i32 (INSERT_SUBREG (v2i32 (IMPLICIT_DEF)),
                                  (i32 FPR32:$Rn), ssub))>;
def : Pat<(v4i32 (scalar_to_vector (i32 FPR32:$Rn))),
            (v4i32 (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)),
                                  (i32 FPR32:$Rn), ssub))>;

def : Pat<(v2i64 (scalar_to_vector (i64 FPR64:$Rn))),
            (v2i64 (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)),
                                  (i64 FPR64:$Rn), dsub))>;

def : Pat<(v4f16 (scalar_to_vector (f16 FPR16:$Rn))),
          (INSERT_SUBREG (v4f16 (IMPLICIT_DEF)), FPR16:$Rn, hsub)>;
def : Pat<(v8f16 (scalar_to_vector (f16 FPR16:$Rn))),
          (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), FPR16:$Rn, hsub)>;

def : Pat<(v4bf16 (scalar_to_vector (bf16 FPR16:$Rn))),
          (INSERT_SUBREG (v4bf16 (IMPLICIT_DEF)), FPR16:$Rn, hsub)>;
def : Pat<(v8bf16 (scalar_to_vector (bf16 FPR16:$Rn))),
          (INSERT_SUBREG (v8bf16 (IMPLICIT_DEF)), FPR16:$Rn, hsub)>;

def : Pat<(v4f32 (scalar_to_vector (f32 FPR32:$Rn))),
          (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), FPR32:$Rn, ssub)>;
def : Pat<(v2f32 (scalar_to_vector (f32 FPR32:$Rn))),
          (INSERT_SUBREG (v2f32 (IMPLICIT_DEF)), FPR32:$Rn, ssub)>;

def : Pat<(v2f64 (scalar_to_vector (f64 FPR64:$Rn))),
          (INSERT_SUBREG (v2f64 (IMPLICIT_DEF)), FPR64:$Rn, dsub)>;

def : Pat<(v4f16 (vector_insert (v4f16 V64:$Rn),
            (f16 FPR16:$Rm), (i64 VectorIndexS:$imm))),
          (EXTRACT_SUBREG
            (INSvi16lane
              (v8f16 (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), V64:$Rn, dsub)),
              VectorIndexS:$imm,
              (v8f16 (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), FPR16:$Rm, hsub)),
              (i64 0)),
            dsub)>;

def : Pat<(vector_insert (v8f16 V128:$Rn), (f16 fpimm0), (i64 VectorIndexH:$imm)),
          (INSvi16gpr V128:$Rn, VectorIndexH:$imm, WZR)>;
def : Pat<(vector_insert (v4f16 V64:$Rn), (f16 fpimm0), (i64 VectorIndexH:$imm)),
          (EXTRACT_SUBREG (INSvi16gpr (v8f16 (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), V64:$Rn, dsub)), VectorIndexH:$imm, WZR), dsub)>;
def : Pat<(vector_insert (v4f32 V128:$Rn), (f32 fpimm0), (i64 VectorIndexS:$imm)),
          (INSvi32gpr V128:$Rn, VectorIndexS:$imm, WZR)>;
def : Pat<(vector_insert (v2f32 V64:$Rn), (f32 fpimm0), (i64 VectorIndexS:$imm)),
          (EXTRACT_SUBREG (INSvi32gpr (v4f32 (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), V64:$Rn, dsub)), VectorIndexS:$imm, WZR), dsub)>;
def : Pat<(vector_insert v2f64:$Rn, (f64 fpimm0), (i64 VectorIndexD:$imm)),
          (INSvi64gpr V128:$Rn, VectorIndexS:$imm, XZR)>;

def : Pat<(v8f16 (vector_insert (v8f16 V128:$Rn),
            (f16 FPR16:$Rm), (i64 VectorIndexH:$imm))),
          (INSvi16lane
            V128:$Rn, VectorIndexH:$imm,
            (v8f16 (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), FPR16:$Rm, hsub)),
            (i64 0))>;

def : Pat<(v4bf16 (vector_insert (v4bf16 V64:$Rn),
            (bf16 FPR16:$Rm), (i64 VectorIndexS:$imm))),
          (EXTRACT_SUBREG
            (INSvi16lane
              (v8bf16 (INSERT_SUBREG (v8bf16 (IMPLICIT_DEF)), V64:$Rn, dsub)),
              VectorIndexS:$imm,
              (v8bf16 (INSERT_SUBREG (v8bf16 (IMPLICIT_DEF)), FPR16:$Rm, hsub)),
              (i64 0)),
            dsub)>;

def : Pat<(v8bf16 (vector_insert (v8bf16 V128:$Rn),
            (bf16 FPR16:$Rm), (i64 VectorIndexH:$imm))),
          (INSvi16lane
            V128:$Rn, VectorIndexH:$imm,
            (v8bf16 (INSERT_SUBREG (v8bf16 (IMPLICIT_DEF)), FPR16:$Rm, hsub)),
            (i64 0))>;

def : Pat<(v2f32 (vector_insert (v2f32 V64:$Rn),
            (f32 FPR32:$Rm), (i64 VectorIndexS:$imm))),
          (EXTRACT_SUBREG
            (INSvi32lane
              (v4f32 (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), V64:$Rn, dsub)),
              VectorIndexS:$imm,
              (v4f32 (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), FPR32:$Rm, ssub)),
              (i64 0)),
            dsub)>;
def : Pat<(v4f32 (vector_insert (v4f32 V128:$Rn),
            (f32 FPR32:$Rm), (i64 VectorIndexS:$imm))),
          (INSvi32lane
            V128:$Rn, VectorIndexS:$imm,
            (v4f32 (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), FPR32:$Rm, ssub)),
            (i64 0))>;
def : Pat<(v2f64 (vector_insert (v2f64 V128:$Rn),
            (f64 FPR64:$Rm), (i64 VectorIndexD:$imm))),
          (INSvi64lane
            V128:$Rn, VectorIndexD:$imm,
            (v2f64 (INSERT_SUBREG (v2f64 (IMPLICIT_DEF)), FPR64:$Rm, dsub)),
            (i64 0))>;

def : Pat<(v2i32 (vector_insert (v2i32 V64:$Rn), (i32 GPR32:$Rm), (i64 VectorIndexS:$imm))),
          (EXTRACT_SUBREG
            (INSvi32gpr (v4i32 (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), V64:$Rn, dsub)),
                        VectorIndexS:$imm, GPR32:$Rm),
            dsub)>;
def : Pat<(v4i16 (vector_insert (v4i16 V64:$Rn), (i32 GPR32:$Rm), (i64 VectorIndexH:$imm))),
          (EXTRACT_SUBREG
            (INSvi16gpr (v8i16 (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)), V64:$Rn, dsub)),
                        VectorIndexH:$imm, GPR32:$Rm),
            dsub)>;
def : Pat<(v8i8 (vector_insert (v8i8 V64:$Rn), (i32 GPR32:$Rm), (i64 VectorIndexB:$imm))),
          (EXTRACT_SUBREG
            (INSvi8gpr (v16i8 (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)), V64:$Rn, dsub)),
                       VectorIndexB:$imm, GPR32:$Rm),
            dsub)>;

def : Pat<(v8i8 (vector_insert (v8i8 V64:$Rn), (i8 FPR8:$Rm), (i64 VectorIndexB:$imm))),
          (EXTRACT_SUBREG
            (INSvi8lane (v16i8 (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)), V64:$Rn, dsub)),
                       VectorIndexB:$imm, (v16i8 (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)), FPR8:$Rm, bsub)), (i64 0)),
            dsub)>;
def : Pat<(v16i8 (vector_insert (v16i8 V128:$Rn), (i8 FPR8:$Rm), (i64 VectorIndexB:$imm))),
          (INSvi8lane V128:$Rn, VectorIndexB:$imm,
             (v16i8 (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)), FPR8:$Rm, bsub)), (i64 0))>;

// Copy an element at a constant index in one vector into a constant indexed
// element of another.
// FIXME refactor to a shared class/dev parameterized on vector type, vector
// index type and INS extension
def : Pat<(v16i8 (int_aarch64_neon_vcopy_lane
                   (v16i8 V128:$Vd), VectorIndexB:$idx, (v16i8 V128:$Vs),
                   VectorIndexB:$idx2)),
          (v16i8 (INSvi8lane
                   V128:$Vd, VectorIndexB:$idx, V128:$Vs, VectorIndexB:$idx2)
          )>;
def : Pat<(v8i16 (int_aarch64_neon_vcopy_lane
                   (v8i16 V128:$Vd), VectorIndexH:$idx, (v8i16 V128:$Vs),
                   VectorIndexH:$idx2)),
          (v8i16 (INSvi16lane
                   V128:$Vd, VectorIndexH:$idx, V128:$Vs, VectorIndexH:$idx2)
          )>;
def : Pat<(v4i32 (int_aarch64_neon_vcopy_lane
                   (v4i32 V128:$Vd), VectorIndexS:$idx, (v4i32 V128:$Vs),
                   VectorIndexS:$idx2)),
          (v4i32 (INSvi32lane
                   V128:$Vd, VectorIndexS:$idx, V128:$Vs, VectorIndexS:$idx2)
          )>;
def : Pat<(v2i64 (int_aarch64_neon_vcopy_lane
                   (v2i64 V128:$Vd), VectorIndexD:$idx, (v2i64 V128:$Vs),
                   VectorIndexD:$idx2)),
          (v2i64 (INSvi64lane
                   V128:$Vd, VectorIndexD:$idx, V128:$Vs, VectorIndexD:$idx2)
          )>;

multiclass Neon_INS_elt_pattern<ValueType VT128, ValueType VT64,
                                ValueType VTScal, Instruction INS> {
  def : Pat<(VT128 (vector_insert V128:$src,
                        (VTScal (vector_extract (VT128 V128:$Rn), (i64 imm:$Immn))),
                        (i64 imm:$Immd))),
            (INS V128:$src, imm:$Immd, V128:$Rn, imm:$Immn)>;

  def : Pat<(VT128 (vector_insert V128:$src,
                        (VTScal (vector_extract (VT64 V64:$Rn), (i64 imm:$Immn))),
                        (i64 imm:$Immd))),
            (INS V128:$src, imm:$Immd,
                 (SUBREG_TO_REG (i64 0), V64:$Rn, dsub), imm:$Immn)>;

  def : Pat<(VT64 (vector_insert V64:$src,
                        (VTScal (vector_extract (VT128 V128:$Rn), (i64 imm:$Immn))),
                        (i64 imm:$Immd))),
            (EXTRACT_SUBREG (INS (SUBREG_TO_REG (i64 0), V64:$src, dsub),
                                 imm:$Immd, V128:$Rn, imm:$Immn),
                            dsub)>;

  def : Pat<(VT64 (vector_insert V64:$src,
                        (VTScal (vector_extract (VT64 V64:$Rn), (i64 imm:$Immn))),
                        (i64 imm:$Immd))),
            (EXTRACT_SUBREG
                (INS (SUBREG_TO_REG (i64 0), V64:$src, dsub), imm:$Immd,
                     (SUBREG_TO_REG (i64 0), V64:$Rn, dsub), imm:$Immn),
                dsub)>;
}

defm : Neon_INS_elt_pattern<v8f16, v4f16, f16, INSvi16lane>;
defm : Neon_INS_elt_pattern<v8bf16, v4bf16, bf16, INSvi16lane>;
defm : Neon_INS_elt_pattern<v4f32, v2f32, f32, INSvi32lane>;
defm : Neon_INS_elt_pattern<v2f64, v1f64, f64, INSvi64lane>;

defm : Neon_INS_elt_pattern<v16i8, v8i8, i32, INSvi8lane>;
defm : Neon_INS_elt_pattern<v8i16, v4i16, i32, INSvi16lane>;
defm : Neon_INS_elt_pattern<v4i32, v2i32, i32, INSvi32lane>;
defm : Neon_INS_elt_pattern<v2i64, v1i64, i64, INSvi64lane>;

// Insert from bitcast
// vector_insert(bitcast(f32 src), n, lane) -> INSvi32lane(src, lane, INSERT_SUBREG(-, n), 0)
def : Pat<(v4i32 (vector_insert v4i32:$src, (i32 (bitconvert (f32 FPR32:$Sn))), (i64 imm:$Immd))),
          (INSvi32lane V128:$src, imm:$Immd, (INSERT_SUBREG (IMPLICIT_DEF), FPR32:$Sn, ssub), 0)>;
def : Pat<(v2i32 (vector_insert v2i32:$src, (i32 (bitconvert (f32 FPR32:$Sn))), (i64 imm:$Immd))),
          (EXTRACT_SUBREG
            (INSvi32lane (v4i32 (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), V64:$src, dsub)),
                         imm:$Immd, (INSERT_SUBREG (IMPLICIT_DEF), FPR32:$Sn, ssub), 0),
            dsub)>;
def : Pat<(v2i64 (vector_insert v2i64:$src, (i64 (bitconvert (f64 FPR64:$Sn))), (i64 imm:$Immd))),
          (INSvi64lane V128:$src, imm:$Immd, (INSERT_SUBREG (IMPLICIT_DEF), FPR64:$Sn, dsub), 0)>;

// bitcast of an extract
// f32 bitcast(vector_extract(v4i32 src, lane)) -> EXTRACT_SUBREG(INSvi32lane(-, 0, src, lane))
def : Pat<(f32 (bitconvert (i32 (vector_extract v4i32:$src, imm:$Immd)))),
          (EXTRACT_SUBREG (INSvi32lane (IMPLICIT_DEF), 0, V128:$src, imm:$Immd), ssub)>;
def : Pat<(f32 (bitconvert (i32 (vector_extract v4i32:$src, (i64 0))))),
          (EXTRACT_SUBREG V128:$src, ssub)>;
def : Pat<(f64 (bitconvert (i64 (vector_extract v2i64:$src, imm:$Immd)))),
          (EXTRACT_SUBREG (INSvi64lane (IMPLICIT_DEF), 0, V128:$src, imm:$Immd), dsub)>;
def : Pat<(f64 (bitconvert (i64 (vector_extract v2i64:$src, (i64 0))))),
          (EXTRACT_SUBREG V128:$src, dsub)>;

// Floating point vector extractions are codegen'd as either a sequence of
// subregister extractions, or a MOV (aka DUP here) if
// the lane number is anything other than zero.
def : Pat<(f64 (vector_extract (v2f64 V128:$Rn), (i64 0))),
          (f64 (EXTRACT_SUBREG V128:$Rn, dsub))>;
def : Pat<(f32 (vector_extract (v4f32 V128:$Rn), (i64 0))),
          (f32 (EXTRACT_SUBREG V128:$Rn, ssub))>;
def : Pat<(f16 (vector_extract (v8f16 V128:$Rn), (i64 0))),
          (f16 (EXTRACT_SUBREG V128:$Rn, hsub))>;
def : Pat<(bf16 (vector_extract (v8bf16 V128:$Rn), (i64 0))),
          (bf16 (EXTRACT_SUBREG V128:$Rn, hsub))>;


def : Pat<(vector_extract (v2f64 V128:$Rn), VectorIndexD:$idx),
          (f64 (DUPi64 V128:$Rn, VectorIndexD:$idx))>;
def : Pat<(vector_extract (v4f32 V128:$Rn), VectorIndexS:$idx),
          (f32 (DUPi32 V128:$Rn, VectorIndexS:$idx))>;
def : Pat<(vector_extract (v8f16 V128:$Rn), VectorIndexH:$idx),
          (f16 (DUPi16 V128:$Rn, VectorIndexH:$idx))>;
def : Pat<(vector_extract (v8bf16 V128:$Rn), VectorIndexH:$idx),
          (bf16 (DUPi16 V128:$Rn, VectorIndexH:$idx))>;

// All concat_vectors operations are canonicalised to act on i64 vectors for
// AArch64. In the general case we need an instruction, which had just as well be
// INS.
multiclass ConcatPat<ValueType DstTy, ValueType SrcTy> {
  def : Pat<(DstTy (concat_vectors (SrcTy V64:$Rd), V64:$Rn)),
            (INSvi64lane (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub), 1,
                         (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rn, dsub), 0)>;

  // If the high lanes are zero we can instead emit a d->d register mov, which
  // will implicitly clear the upper bits.
  def : Pat<(DstTy (concat_vectors (SrcTy V64:$Rn), immAllZerosV)),
            (SUBREG_TO_REG (i64 0), (FMOVDr V64:$Rn), dsub)>;

  // If the high lanes are undef we can just ignore them:
  def : Pat<(DstTy (concat_vectors (SrcTy V64:$Rn), undef)),
            (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rn, dsub)>;
}

defm : ConcatPat<v2i64, v1i64>;
defm : ConcatPat<v2f64, v1f64>;
defm : ConcatPat<v4i32, v2i32>;
defm : ConcatPat<v4f32, v2f32>;
defm : ConcatPat<v8i16, v4i16>;
defm : ConcatPat<v8f16, v4f16>;
defm : ConcatPat<v8bf16, v4bf16>;
defm : ConcatPat<v16i8, v8i8>;

//----------------------------------------------------------------------------
// AdvSIMD across lanes instructions
//----------------------------------------------------------------------------

defm ADDV    : SIMDAcrossLanesBHS<0, 0b11011, "addv">;
defm SMAXV   : SIMDAcrossLanesBHS<0, 0b01010, "smaxv">;
defm SMINV   : SIMDAcrossLanesBHS<0, 0b11010, "sminv">;
defm UMAXV   : SIMDAcrossLanesBHS<1, 0b01010, "umaxv">;
defm UMINV   : SIMDAcrossLanesBHS<1, 0b11010, "uminv">;
defm SADDLV  : SIMDAcrossLanesHSD<0, 0b00011, "saddlv">;
defm UADDLV  : SIMDAcrossLanesHSD<1, 0b00011, "uaddlv">;
defm FMAXNMV : SIMDFPAcrossLanes<0b01100, 0, "fmaxnmv", AArch64fmaxnmv>;
defm FMAXV   : SIMDFPAcrossLanes<0b01111, 0, "fmaxv", AArch64fmaxv>;
defm FMINNMV : SIMDFPAcrossLanes<0b01100, 1, "fminnmv", AArch64fminnmv>;
defm FMINV   : SIMDFPAcrossLanes<0b01111, 1, "fminv", AArch64fminv>;

multiclass SIMDAcrossLaneLongPairIntrinsic<string Opc, SDPatternOperator addlp> {
  // Patterns for addv(addlp(x)) ==> addlv
  def : Pat<(i32 (vector_extract (v8i16 (insert_subvector undef,
              (v4i16 (AArch64uaddv (v4i16 (addlp (v8i8 V64:$op))))),
              (i64 0))), (i64 0))),
            (EXTRACT_SUBREG (INSERT_SUBREG (v4i16 (IMPLICIT_DEF)),
              (!cast<Instruction>(Opc#"v8i8v") V64:$op), hsub), ssub)>;
  def : Pat<(i32 (vector_extract (v8i16 (AArch64uaddv (v8i16 (addlp (v16i8 V128:$op))))), (i64 0))),
            (EXTRACT_SUBREG (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)),
              (!cast<Instruction>(Opc#"v16i8v") V128:$op), hsub), ssub)>;
  def : Pat<(v4i32 (AArch64uaddv (v4i32 (addlp (v8i16 V128:$op))))),
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), (!cast<Instruction>(Opc#"v8i16v") V128:$op), ssub)>;

  // Patterns for addp(addlp(x))) ==> addlv
  def : Pat<(v2i32 (AArch64uaddv (v2i32 (addlp (v4i16 V64:$op))))),
            (INSERT_SUBREG (v2i32 (IMPLICIT_DEF)), (!cast<Instruction>(Opc#"v4i16v") V64:$op), ssub)>;
  def : Pat<(v2i64 (AArch64uaddv (v2i64 (addlp (v4i32 V128:$op))))),
            (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)), (!cast<Instruction>(Opc#"v4i32v") V128:$op), dsub)>;
}

defm : SIMDAcrossLaneLongPairIntrinsic<"UADDLV", AArch64uaddlp>;
defm : SIMDAcrossLaneLongPairIntrinsic<"SADDLV", AArch64saddlp>;

// Pattern is used for GlobalISel
multiclass SIMDAcrossLaneLongPairIntrinsicGISel<string Opc, SDPatternOperator addlp> {
  // Patterns for addv(addlp(x)) ==> addlv
  def : Pat<(i16 (vecreduce_add (v4i16 (addlp (v8i8 V64:$Rn))))),
            (!cast<Instruction>(Opc#"v8i8v") V64:$Rn)>;
  def : Pat<(i16 (vecreduce_add (v8i16 (addlp (v16i8 V128:$Rn))))),
            (!cast<Instruction>(Opc#"v16i8v") V128:$Rn)>;
  def : Pat<(i32 (vecreduce_add (v4i32 (addlp (v8i16 V128:$Rn))))),
            (!cast<Instruction>(Opc#"v8i16v") V128:$Rn)>;

  // Patterns for addp(addlp(x))) ==> addlv
  def : Pat<(i32 (vecreduce_add (v2i32 (addlp (v4i16 V64:$Rn))))),
            (!cast<Instruction>(Opc#"v4i16v") V64:$Rn)>;
  def : Pat<(i64 (vecreduce_add (v2i64 (addlp (v4i32 V128:$Rn))))),
            (!cast<Instruction>(Opc#"v4i32v") V128:$Rn)>;
}

defm : SIMDAcrossLaneLongPairIntrinsicGISel<"UADDLV", AArch64uaddlp>;
defm : SIMDAcrossLaneLongPairIntrinsicGISel<"SADDLV", AArch64saddlp>;

// Patterns for uaddlv(uaddlp(x)) ==> uaddlv
def : Pat<(i64 (int_aarch64_neon_uaddlv (v4i32 (AArch64uaddlp (v8i16 V128:$op))))),
          (i64 (EXTRACT_SUBREG
            (v4i32 (SUBREG_TO_REG (i64 0), (UADDLVv8i16v V128:$op), ssub)),
            dsub))>;

def : Pat<(i32 (int_aarch64_neon_uaddlv (v8i16 (AArch64uaddlp (v16i8 V128:$op))))),
          (i32 (EXTRACT_SUBREG
            (v8i16 (SUBREG_TO_REG (i64 0), (UADDLVv16i8v V128:$op), hsub)),
            ssub))>;

def : Pat<(v2i64 (AArch64uaddlv (v4i32 (AArch64uaddlp (v8i16 V128:$op))))),
          (v2i64 (SUBREG_TO_REG (i64 0), (UADDLVv8i16v V128:$op), ssub))>;

def : Pat<(v4i32 (AArch64uaddlv (v8i16 (AArch64uaddlp (v16i8 V128:$op))))),
          (v4i32 (SUBREG_TO_REG (i64 0), (UADDLVv16i8v V128:$op), hsub))>;

def : Pat<(v4i32 (AArch64uaddlv (v4i16 (AArch64uaddlp (v8i8 V64:$op))))),
          (v4i32 (SUBREG_TO_REG (i64 0), (UADDLVv8i8v V64:$op), hsub))>;

multiclass SIMDAcrossLaneLongReductionIntrinsic<string Opc, SDPatternOperator addlv> {
  def : Pat<(v4i32 (addlv (v8i8 V64:$Rn))),
            (v4i32 (SUBREG_TO_REG (i64 0), (!cast<Instruction>(Opc#"v8i8v") V64:$Rn), hsub))>;

  def : Pat<(v4i32 (addlv (v4i16 V64:$Rn))),
            (v4i32 (SUBREG_TO_REG (i64 0), (!cast<Instruction>(Opc#"v4i16v") V64:$Rn), ssub))>;

  def : Pat<(v4i32 (addlv (v16i8 V128:$Rn))),
            (v4i32 (SUBREG_TO_REG (i64 0), (!cast<Instruction>(Opc#"v16i8v") V128:$Rn), hsub))>;

  def : Pat<(v4i32 (addlv (v8i16 V128:$Rn))),
            (v4i32 (SUBREG_TO_REG (i64 0), (!cast<Instruction>(Opc#"v8i16v") V128:$Rn), ssub))>;

  def : Pat<(v2i64 (addlv (v4i32 V128:$Rn))),
            (v2i64 (SUBREG_TO_REG (i64 0), (!cast<Instruction>(Opc#"v4i32v") V128:$Rn), dsub))>;
}

defm : SIMDAcrossLaneLongReductionIntrinsic<"UADDLV", AArch64uaddlv>;
defm : SIMDAcrossLaneLongReductionIntrinsic<"SADDLV", AArch64saddlv>;

// Patterns for across-vector intrinsics, that have a node equivalent, that
// returns a vector (with only the low lane defined) instead of a scalar.
// In effect, opNode is the same as (scalar_to_vector (IntNode)).
multiclass SIMDAcrossLanesIntrinsic<string baseOpc,
                                    SDPatternOperator opNode> {
// If a lane instruction caught the vector_extract around opNode, we can
// directly match the latter to the instruction.
def : Pat<(v8i8 (opNode V64:$Rn)),
          (INSERT_SUBREG (v8i8 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) V64:$Rn), bsub)>;
def : Pat<(v16i8 (opNode V128:$Rn)),
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) V128:$Rn), bsub)>;
def : Pat<(v4i16 (opNode V64:$Rn)),
          (INSERT_SUBREG (v4i16 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) V64:$Rn), hsub)>;
def : Pat<(v8i16 (opNode V128:$Rn)),
          (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) V128:$Rn), hsub)>;
def : Pat<(v4i32 (opNode V128:$Rn)),
          (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v4i32v")) V128:$Rn), ssub)>;


// If none did, fallback to the explicit patterns, consuming the vector_extract.
def : Pat<(i32 (vector_extract (insert_subvector undef, (v8i8 (opNode V64:$Rn)),
            (i64 0)), (i64 0))),
          (EXTRACT_SUBREG (INSERT_SUBREG (v8i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) V64:$Rn),
            bsub), ssub)>;
def : Pat<(i32 (vector_extract (v16i8 (opNode V128:$Rn)), (i64 0))),
          (EXTRACT_SUBREG (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) V128:$Rn),
            bsub), ssub)>;
def : Pat<(i32 (vector_extract (insert_subvector undef,
            (v4i16 (opNode V64:$Rn)), (i64 0)), (i64 0))),
          (EXTRACT_SUBREG (INSERT_SUBREG (v4i16 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) V64:$Rn),
            hsub), ssub)>;
def : Pat<(i32 (vector_extract (v8i16 (opNode V128:$Rn)), (i64 0))),
          (EXTRACT_SUBREG (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) V128:$Rn),
            hsub), ssub)>;
def : Pat<(i32 (vector_extract (v4i32 (opNode V128:$Rn)), (i64 0))),
          (EXTRACT_SUBREG (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v4i32v")) V128:$Rn),
            ssub), ssub)>;

}

multiclass SIMDAcrossLanesSignedIntrinsic<string baseOpc,
                                          SDPatternOperator opNode>
    : SIMDAcrossLanesIntrinsic<baseOpc, opNode> {
// If there is a sign extension after this intrinsic, consume it as smov already
// performed it
def : Pat<(i32 (sext_inreg (i32 (vector_extract (insert_subvector undef,
            (opNode (v8i8 V64:$Rn)), (i64 0)), (i64 0))), i8)),
          (i32 (SMOVvi8to32
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
              (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) V64:$Rn), bsub),
            (i64 0)))>;
def : Pat<(i32 (sext_inreg (i32 (vector_extract
            (opNode (v16i8 V128:$Rn)), (i64 0))), i8)),
          (i32 (SMOVvi8to32
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
             (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) V128:$Rn), bsub),
            (i64 0)))>;
def : Pat<(i32 (sext_inreg (i32 (vector_extract (insert_subvector undef,
            (opNode (v4i16 V64:$Rn)), (i64 0)), (i64 0))), i16)),
          (i32 (SMOVvi16to32
           (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) V64:$Rn), hsub),
           (i64 0)))>;
def : Pat<(i32 (sext_inreg (i32 (vector_extract
            (opNode (v8i16 V128:$Rn)), (i64 0))), i16)),
          (i32 (SMOVvi16to32
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
             (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) V128:$Rn), hsub),
            (i64 0)))>;
}

multiclass SIMDAcrossLanesUnsignedIntrinsic<string baseOpc,
                                            SDPatternOperator opNode>
    : SIMDAcrossLanesIntrinsic<baseOpc, opNode> {
// If there is a masking operation keeping only what has been actually
// generated, consume it.
def : Pat<(i32 (and (i32 (vector_extract (insert_subvector undef,
            (opNode (v8i8 V64:$Rn)), (i64 0)), (i64 0))), maski8_or_more)),
      (i32 (EXTRACT_SUBREG
        (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
          (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) V64:$Rn), bsub),
        ssub))>;
def : Pat<(i32 (and (i32 (vector_extract (opNode (v16i8 V128:$Rn)), (i64 0))),
            maski8_or_more)),
        (i32 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) V128:$Rn), bsub),
          ssub))>;
def : Pat<(i32 (and (i32 (vector_extract (insert_subvector undef,
            (opNode (v4i16 V64:$Rn)), (i64 0)), (i64 0))), maski16_or_more)),
          (i32 (EXTRACT_SUBREG
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
              (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) V64:$Rn), hsub),
            ssub))>;
def : Pat<(i32 (and (i32 (vector_extract (opNode (v8i16 V128:$Rn)), (i64 0))),
            maski16_or_more)),
        (i32 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) V128:$Rn), hsub),
          ssub))>;
}

// For vecreduce_add, used by GlobalISel not SDAG
def : Pat<(i8 (vecreduce_add (v8i8 V64:$Rn))), 
          (i8 (ADDVv8i8v V64:$Rn))>;
def : Pat<(i8 (vecreduce_add (v16i8 V128:$Rn))), 
          (i8 (ADDVv16i8v V128:$Rn))>;
def : Pat<(i16 (vecreduce_add (v4i16 V64:$Rn))), 
          (i16 (ADDVv4i16v V64:$Rn))>;
def : Pat<(i16 (vecreduce_add (v8i16 V128:$Rn))), 
          (i16 (ADDVv8i16v V128:$Rn))>;
def : Pat<(i32 (vecreduce_add (v2i32 V64:$Rn))), 
          (i32 (EXTRACT_SUBREG (ADDPv2i32 V64:$Rn, V64:$Rn), ssub))>;
def : Pat<(i32 (vecreduce_add (v4i32 V128:$Rn))), 
          (i32 (ADDVv4i32v V128:$Rn))>;
def : Pat<(i64 (vecreduce_add (v2i64 V128:$Rn))), 
          (i64 (ADDPv2i64p V128:$Rn))>;

defm : SIMDAcrossLanesSignedIntrinsic<"ADDV",  AArch64saddv>;
// vaddv_[su]32 is special; -> ADDP Vd.2S,Vn.2S,Vm.2S; return Vd.s[0];Vn==Vm
def : Pat<(v2i32 (AArch64saddv (v2i32 V64:$Rn))),
          (ADDPv2i32 V64:$Rn, V64:$Rn)>;

defm : SIMDAcrossLanesUnsignedIntrinsic<"ADDV", AArch64uaddv>;
// vaddv_[su]32 is special; -> ADDP Vd.2S,Vn.2S,Vm.2S; return Vd.s[0];Vn==Vm
def : Pat<(v2i32 (AArch64uaddv (v2i32 V64:$Rn))),
          (ADDPv2i32 V64:$Rn, V64:$Rn)>;

defm : SIMDAcrossLanesSignedIntrinsic<"SMAXV", AArch64smaxv>;
def : Pat<(v2i32 (AArch64smaxv (v2i32 V64:$Rn))),
          (SMAXPv2i32 V64:$Rn, V64:$Rn)>;

defm : SIMDAcrossLanesSignedIntrinsic<"SMINV", AArch64sminv>;
def : Pat<(v2i32 (AArch64sminv (v2i32 V64:$Rn))),
          (SMINPv2i32 V64:$Rn, V64:$Rn)>;

defm : SIMDAcrossLanesUnsignedIntrinsic<"UMAXV", AArch64umaxv>;
def : Pat<(v2i32 (AArch64umaxv (v2i32 V64:$Rn))),
          (UMAXPv2i32 V64:$Rn, V64:$Rn)>;

defm : SIMDAcrossLanesUnsignedIntrinsic<"UMINV", AArch64uminv>;
def : Pat<(v2i32 (AArch64uminv (v2i32 V64:$Rn))),
          (UMINPv2i32 V64:$Rn, V64:$Rn)>;

// For vecreduce_{opc} used by GlobalISel, not SDAG at the moment
// because GlobalISel allows us to specify the return register to be a FPR
multiclass SIMDAcrossLanesVecReductionIntrinsic<string baseOpc,
                                               SDPatternOperator opNode> {
def : Pat<(i8 (opNode (v8i8 FPR64:$Rn))),
          (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) FPR64:$Rn)>;

def : Pat<(i8 (opNode (v16i8 FPR128:$Rn))),
          (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) FPR128:$Rn)>;

def : Pat<(i16 (opNode (v4i16 FPR64:$Rn))),
          (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) FPR64:$Rn)>;

def : Pat<(i16 (opNode (v8i16 FPR128:$Rn))),
          (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) FPR128:$Rn)>;

def : Pat<(i32 (opNode (v4i32 V128:$Rn))), 
          (!cast<Instruction>(!strconcat(baseOpc, "v4i32v")) V128:$Rn)>;
}

// For v2i32 source type, the pairwise instruction can be used instead
defm : SIMDAcrossLanesVecReductionIntrinsic<"UMINV", vecreduce_umin>;
def : Pat<(i32 (vecreduce_umin (v2i32 V64:$Rn))), 
          (i32 (EXTRACT_SUBREG (UMINPv2i32 V64:$Rn, V64:$Rn), ssub))>;

defm : SIMDAcrossLanesVecReductionIntrinsic<"UMAXV", vecreduce_umax>;
def : Pat<(i32 (vecreduce_umax (v2i32 V64:$Rn))), 
          (i32 (EXTRACT_SUBREG (UMAXPv2i32 V64:$Rn, V64:$Rn), ssub))>;

defm : SIMDAcrossLanesVecReductionIntrinsic<"SMINV", vecreduce_smin>;
def : Pat<(i32 (vecreduce_smin (v2i32 V64:$Rn))), 
          (i32 (EXTRACT_SUBREG (SMINPv2i32 V64:$Rn, V64:$Rn), ssub))>;

defm : SIMDAcrossLanesVecReductionIntrinsic<"SMAXV", vecreduce_smax>;
def : Pat<(i32 (vecreduce_smax (v2i32 V64:$Rn))), 
          (i32 (EXTRACT_SUBREG (SMAXPv2i32 V64:$Rn, V64:$Rn), ssub))>;

multiclass SIMDAcrossLanesSignedLongIntrinsic<string baseOpc, Intrinsic intOp> {
  def : Pat<(i32 (intOp (v8i8 V64:$Rn))),
        (i32 (SMOVvi16to32
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) V64:$Rn), hsub),
          (i64 0)))>;
def : Pat<(i32 (intOp (v16i8 V128:$Rn))),
        (i32 (SMOVvi16to32
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) V128:$Rn), hsub),
          (i64 0)))>;

def : Pat<(i32 (intOp (v4i16 V64:$Rn))),
          (i32 (EXTRACT_SUBREG
           (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) V64:$Rn), ssub),
           ssub))>;
def : Pat<(i32 (intOp (v8i16 V128:$Rn))),
        (i32 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) V128:$Rn), ssub),
          ssub))>;

def : Pat<(i64 (intOp (v4i32 V128:$Rn))),
        (i64 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v4i32v")) V128:$Rn), dsub),
          dsub))>;
}

multiclass SIMDAcrossLanesUnsignedLongIntrinsic<string baseOpc,
                                                Intrinsic intOp> {
  def : Pat<(i32 (intOp (v8i8 V64:$Rn))),
        (i32 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) V64:$Rn), hsub),
          ssub))>;
def : Pat<(i32 (intOp (v16i8 V128:$Rn))),
        (i32 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) V128:$Rn), hsub),
          ssub))>;

def : Pat<(i32 (intOp (v4i16 V64:$Rn))),
          (i32 (EXTRACT_SUBREG
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
              (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) V64:$Rn), ssub),
            ssub))>;
def : Pat<(i32 (intOp (v8i16 V128:$Rn))),
        (i32 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) V128:$Rn), ssub),
          ssub))>;

def : Pat<(i64 (intOp (v4i32 V128:$Rn))),
        (i64 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v4i32v")) V128:$Rn), dsub),
          dsub))>;
}

defm : SIMDAcrossLanesSignedLongIntrinsic<"SADDLV", int_aarch64_neon_saddlv>;
defm : SIMDAcrossLanesUnsignedLongIntrinsic<"UADDLV", int_aarch64_neon_uaddlv>;

// The vaddlv_s32 intrinsic gets mapped to SADDLP.
def : Pat<(i64 (int_aarch64_neon_saddlv (v2i32 V64:$Rn))),
          (i64 (EXTRACT_SUBREG
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
              (SADDLPv2i32_v1i64 V64:$Rn), dsub),
            dsub))>;
// The vaddlv_u32 intrinsic gets mapped to UADDLP.
def : Pat<(i64 (int_aarch64_neon_uaddlv (v2i32 V64:$Rn))),
          (i64 (EXTRACT_SUBREG
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
              (UADDLPv2i32_v1i64 V64:$Rn), dsub),
            dsub))>;

//------------------------------------------------------------------------------
// AdvSIMD modified immediate instructions
//------------------------------------------------------------------------------

// AdvSIMD BIC
defm BIC : SIMDModifiedImmVectorShiftTied<1, 0b11, 0b01, "bic", AArch64bici>;
// AdvSIMD ORR
defm ORR : SIMDModifiedImmVectorShiftTied<0, 0b11, 0b01, "orr", AArch64orri>;

let Predicates = [HasNEON] in {
def : InstAlias<"bic $Vd.4h, $imm", (BICv4i16 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"bic $Vd.8h, $imm", (BICv8i16 V128:$Vd, imm0_255:$imm, 0)>;
def : InstAlias<"bic $Vd.2s, $imm", (BICv2i32 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"bic $Vd.4s, $imm", (BICv4i32 V128:$Vd, imm0_255:$imm, 0)>;

def : InstAlias<"bic.4h $Vd, $imm", (BICv4i16 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"bic.8h $Vd, $imm", (BICv8i16 V128:$Vd, imm0_255:$imm, 0)>;
def : InstAlias<"bic.2s $Vd, $imm", (BICv2i32 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"bic.4s $Vd, $imm", (BICv4i32 V128:$Vd, imm0_255:$imm, 0)>;

def : InstAlias<"orr $Vd.4h, $imm", (ORRv4i16 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"orr $Vd.8h, $imm", (ORRv8i16 V128:$Vd, imm0_255:$imm, 0)>;
def : InstAlias<"orr $Vd.2s, $imm", (ORRv2i32 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"orr $Vd.4s, $imm", (ORRv4i32 V128:$Vd, imm0_255:$imm, 0)>;

def : InstAlias<"orr.4h $Vd, $imm", (ORRv4i16 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"orr.8h $Vd, $imm", (ORRv8i16 V128:$Vd, imm0_255:$imm, 0)>;
def : InstAlias<"orr.2s $Vd, $imm", (ORRv2i32 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"orr.4s $Vd, $imm", (ORRv4i32 V128:$Vd, imm0_255:$imm, 0)>;
}

// AdvSIMD FMOV
def FMOVv2f64_ns : SIMDModifiedImmVectorNoShift<1, 1, 0, 0b1111, V128, fpimm8,
                                              "fmov", ".2d",
                       [(set (v2f64 V128:$Rd), (AArch64fmov imm0_255:$imm8))]>;
def FMOVv2f32_ns : SIMDModifiedImmVectorNoShift<0, 0, 0, 0b1111, V64,  fpimm8,
                                              "fmov", ".2s",
                       [(set (v2f32 V64:$Rd), (AArch64fmov imm0_255:$imm8))]>;
def FMOVv4f32_ns : SIMDModifiedImmVectorNoShift<1, 0, 0, 0b1111, V128, fpimm8,
                                              "fmov", ".4s",
                       [(set (v4f32 V128:$Rd), (AArch64fmov imm0_255:$imm8))]>;
let Predicates = [HasNEON, HasFullFP16] in {
def FMOVv4f16_ns : SIMDModifiedImmVectorNoShift<0, 0, 1, 0b1111, V64,  fpimm8,
                                              "fmov", ".4h",
                       [(set (v4f16 V64:$Rd), (AArch64fmov imm0_255:$imm8))]>;
def FMOVv8f16_ns : SIMDModifiedImmVectorNoShift<1, 0, 1, 0b1111, V128, fpimm8,
                                              "fmov", ".8h",
                       [(set (v8f16 V128:$Rd), (AArch64fmov imm0_255:$imm8))]>;
} // Predicates = [HasNEON, HasFullFP16]

// AdvSIMD MOVI

// EDIT byte mask: scalar
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
def MOVID      : SIMDModifiedImmScalarNoShift<0, 1, 0b1110, "movi",
                    [(set FPR64:$Rd, simdimmtype10:$imm8)]>;
// The movi_edit node has the immediate value already encoded, so we use
// a plain imm0_255 here.
def : Pat<(f64 (AArch64movi_edit imm0_255:$shift)),
          (MOVID imm0_255:$shift)>;

// EDIT byte mask: 2d

// The movi_edit node has the immediate value already encoded, so we use
// a plain imm0_255 in the pattern
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
def MOVIv2d_ns   : SIMDModifiedImmVectorNoShift<1, 1, 0, 0b1110, V128,
                                                simdimmtype10,
                                                "movi", ".2d",
                   [(set (v2i64 V128:$Rd), (AArch64movi_edit imm0_255:$imm8))]>;

def : Pat<(v2i64 immAllZerosV), (MOVIv2d_ns (i32 0))>;
def : Pat<(v4i32 immAllZerosV), (MOVIv2d_ns (i32 0))>;
def : Pat<(v8i16 immAllZerosV), (MOVIv2d_ns (i32 0))>;
def : Pat<(v16i8 immAllZerosV), (MOVIv2d_ns (i32 0))>;
def : Pat<(v2f64 immAllZerosV), (MOVIv2d_ns (i32 0))>;
def : Pat<(v4f32 immAllZerosV), (MOVIv2d_ns (i32 0))>;
def : Pat<(v8f16 immAllZerosV), (MOVIv2d_ns (i32 0))>;
def : Pat<(v8bf16 immAllZerosV), (MOVIv2d_ns (i32 0))>;

def : Pat<(v2i64 immAllOnesV), (MOVIv2d_ns (i32 255))>;
def : Pat<(v4i32 immAllOnesV), (MOVIv2d_ns (i32 255))>;
def : Pat<(v8i16 immAllOnesV), (MOVIv2d_ns (i32 255))>;
def : Pat<(v16i8 immAllOnesV), (MOVIv2d_ns (i32 255))>;

// Set 64-bit vectors to all 0/1 by extracting from a 128-bit register as the
// extract is free and this gives better MachineCSE results.
def : Pat<(v1i64 immAllZerosV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 0)), dsub)>;
def : Pat<(v2i32 immAllZerosV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 0)), dsub)>;
def : Pat<(v4i16 immAllZerosV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 0)), dsub)>;
def : Pat<(v8i8  immAllZerosV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 0)), dsub)>;
def : Pat<(v1f64 immAllZerosV), (MOVID (i32 0))>;
def : Pat<(v2f32 immAllZerosV), (MOVID (i32 0))>;
def : Pat<(v4f16 immAllZerosV), (MOVID (i32 0))>;
def : Pat<(v4bf16 immAllZerosV), (MOVID (i32 0))>;

def : Pat<(v1i64 immAllOnesV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 255)), dsub)>;
def : Pat<(v2i32 immAllOnesV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 255)), dsub)>;
def : Pat<(v4i16 immAllOnesV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 255)), dsub)>;
def : Pat<(v8i8  immAllOnesV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 255)), dsub)>;

// EDIT per word & halfword: 2s, 4h, 4s, & 8h
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
defm MOVI      : SIMDModifiedImmVectorShift<0, 0b10, 0b00, "movi">;

let Predicates = [HasNEON] in {
  // Using the MOVI to materialize fp constants.
  def : Pat<(f32 fpimm32SIMDModImmType4:$in),
            (EXTRACT_SUBREG (MOVIv2i32 (fpimm32SIMDModImmType4XForm f32:$in),
                                       (i32 24)),
                            ssub)>;
}

let Predicates = [HasNEON] in {
def : InstAlias<"movi $Vd.4h, $imm", (MOVIv4i16 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"movi $Vd.8h, $imm", (MOVIv8i16 V128:$Vd, imm0_255:$imm, 0), 0>;
def : InstAlias<"movi $Vd.2s, $imm", (MOVIv2i32 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"movi $Vd.4s, $imm", (MOVIv4i32 V128:$Vd, imm0_255:$imm, 0), 0>;

def : InstAlias<"movi.4h $Vd, $imm", (MOVIv4i16 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"movi.8h $Vd, $imm", (MOVIv8i16 V128:$Vd, imm0_255:$imm, 0), 0>;
def : InstAlias<"movi.2s $Vd, $imm", (MOVIv2i32 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"movi.4s $Vd, $imm", (MOVIv4i32 V128:$Vd, imm0_255:$imm, 0), 0>;
}

def : Pat<(v2i32 (AArch64movi_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MOVIv2i32 imm0_255:$imm8, imm:$shift)>;
def : Pat<(v4i32 (AArch64movi_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MOVIv4i32 imm0_255:$imm8, imm:$shift)>;
def : Pat<(v4i16 (AArch64movi_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MOVIv4i16 imm0_255:$imm8, imm:$shift)>;
def : Pat<(v8i16 (AArch64movi_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MOVIv8i16 imm0_255:$imm8, imm:$shift)>;

let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
// EDIT per word: 2s & 4s with MSL shifter
def MOVIv2s_msl  : SIMDModifiedImmMoveMSL<0, 0, {1,1,0,?}, V64, "movi", ".2s",
                      [(set (v2i32 V64:$Rd),
                            (AArch64movi_msl imm0_255:$imm8, (i32 imm:$shift)))]>;
def MOVIv4s_msl  : SIMDModifiedImmMoveMSL<1, 0, {1,1,0,?}, V128, "movi", ".4s",
                      [(set (v4i32 V128:$Rd),
                            (AArch64movi_msl imm0_255:$imm8, (i32 imm:$shift)))]>;

// Per byte: 8b & 16b
def MOVIv8b_ns   : SIMDModifiedImmVectorNoShift<0, 0, 0, 0b1110, V64,  imm0_255,
                                                 "movi", ".8b",
                       [(set (v8i8 V64:$Rd), (AArch64movi imm0_255:$imm8))]>;

def MOVIv16b_ns  : SIMDModifiedImmVectorNoShift<1, 0, 0, 0b1110, V128, imm0_255,
                                                 "movi", ".16b",
                       [(set (v16i8 V128:$Rd), (AArch64movi imm0_255:$imm8))]>;
}

// AdvSIMD MVNI

// EDIT per word & halfword: 2s, 4h, 4s, & 8h
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
defm MVNI      : SIMDModifiedImmVectorShift<1, 0b10, 0b00, "mvni">;

let Predicates = [HasNEON] in {
def : InstAlias<"mvni $Vd.4h, $imm", (MVNIv4i16 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"mvni $Vd.8h, $imm", (MVNIv8i16 V128:$Vd, imm0_255:$imm, 0), 0>;
def : InstAlias<"mvni $Vd.2s, $imm", (MVNIv2i32 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"mvni $Vd.4s, $imm", (MVNIv4i32 V128:$Vd, imm0_255:$imm, 0), 0>;

def : InstAlias<"mvni.4h $Vd, $imm", (MVNIv4i16 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"mvni.8h $Vd, $imm", (MVNIv8i16 V128:$Vd, imm0_255:$imm, 0), 0>;
def : InstAlias<"mvni.2s $Vd, $imm", (MVNIv2i32 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"mvni.4s $Vd, $imm", (MVNIv4i32 V128:$Vd, imm0_255:$imm, 0), 0>;
}

def : Pat<(v2i32 (AArch64mvni_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MVNIv2i32 imm0_255:$imm8, imm:$shift)>;
def : Pat<(v4i32 (AArch64mvni_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MVNIv4i32 imm0_255:$imm8, imm:$shift)>;
def : Pat<(v4i16 (AArch64mvni_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MVNIv4i16 imm0_255:$imm8, imm:$shift)>;
def : Pat<(v8i16 (AArch64mvni_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MVNIv8i16 imm0_255:$imm8, imm:$shift)>;

// EDIT per word: 2s & 4s with MSL shifter
let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
def MVNIv2s_msl   : SIMDModifiedImmMoveMSL<0, 1, {1,1,0,?}, V64, "mvni", ".2s",
                      [(set (v2i32 V64:$Rd),
                            (AArch64mvni_msl imm0_255:$imm8, (i32 imm:$shift)))]>;
def MVNIv4s_msl   : SIMDModifiedImmMoveMSL<1, 1, {1,1,0,?}, V128, "mvni", ".4s",
                      [(set (v4i32 V128:$Rd),
                            (AArch64mvni_msl imm0_255:$imm8, (i32 imm:$shift)))]>;
}

//----------------------------------------------------------------------------
// AdvSIMD indexed element
//----------------------------------------------------------------------------

let hasSideEffects = 0 in {
  defm FMLA  : SIMDFPIndexedTied<0, 0b0001, "fmla">;
  defm FMLS  : SIMDFPIndexedTied<0, 0b0101, "fmls">;
}

// NOTE: Operands are reordered in the FMLA/FMLS PatFrags because the
// instruction expects the addend first, while the intrinsic expects it last.

// On the other hand, there are quite a few valid combinatorial options due to
// the commutativity of multiplication and the fact that (-x) * y = x * (-y).
defm : SIMDFPIndexedTiedPatterns<"FMLA",
           TriOpFrag<(any_fma node:$RHS, node:$MHS, node:$LHS)>>;
defm : SIMDFPIndexedTiedPatterns<"FMLA",
           TriOpFrag<(any_fma node:$MHS, node:$RHS, node:$LHS)>>;

defm : SIMDFPIndexedTiedPatterns<"FMLS",
           TriOpFrag<(any_fma node:$MHS, (fneg node:$RHS), node:$LHS)> >;
defm : SIMDFPIndexedTiedPatterns<"FMLS",
           TriOpFrag<(any_fma node:$RHS, (fneg node:$MHS), node:$LHS)> >;
defm : SIMDFPIndexedTiedPatterns<"FMLS",
           TriOpFrag<(any_fma (fneg node:$RHS), node:$MHS, node:$LHS)> >;
defm : SIMDFPIndexedTiedPatterns<"FMLS",
           TriOpFrag<(any_fma (fneg node:$MHS), node:$RHS, node:$LHS)> >;

multiclass FMLSIndexedAfterNegPatterns<SDPatternOperator OpNode> {
  // 3 variants for the .2s version: DUPLANE from 128-bit, DUPLANE from 64-bit
  // and DUP scalar.
  def : Pat<(v2f32 (OpNode (v2f32 V64:$Rd), (v2f32 V64:$Rn),
                           (AArch64duplane32 (v4f32 (fneg V128:$Rm)),
                                           VectorIndexS:$idx))),
            (FMLSv2i32_indexed V64:$Rd, V64:$Rn, V128:$Rm, VectorIndexS:$idx)>;
  def : Pat<(v2f32 (OpNode (v2f32 V64:$Rd), (v2f32 V64:$Rn),
                           (v2f32 (AArch64duplane32
                                      (v4f32 (insert_subvector undef,
                                                 (v2f32 (fneg V64:$Rm)),
                                                 (i64 0))),
                                      VectorIndexS:$idx)))),
            (FMLSv2i32_indexed V64:$Rd, V64:$Rn,
                               (SUBREG_TO_REG (i32 0), V64:$Rm, dsub),
                               VectorIndexS:$idx)>;
  def : Pat<(v2f32 (OpNode (v2f32 V64:$Rd), (v2f32 V64:$Rn),
                           (AArch64dup (f32 (fneg FPR32Op:$Rm))))),
            (FMLSv2i32_indexed V64:$Rd, V64:$Rn,
                (SUBREG_TO_REG (i32 0), FPR32Op:$Rm, ssub), (i64 0))>;

  // 3 variants for the .4s version: DUPLANE from 128-bit, DUPLANE from 64-bit
  // and DUP scalar.
  def : Pat<(v4f32 (OpNode (v4f32 V128:$Rd), (v4f32 V128:$Rn),
                           (AArch64duplane32 (v4f32 (fneg V128:$Rm)),
                                           VectorIndexS:$idx))),
            (FMLSv4i32_indexed V128:$Rd, V128:$Rn, V128:$Rm,
                               VectorIndexS:$idx)>;
  def : Pat<(v4f32 (OpNode (v4f32 V128:$Rd), (v4f32 V128:$Rn),
                           (v4f32 (AArch64duplane32
                                      (v4f32 (insert_subvector undef,
                                                 (v2f32 (fneg V64:$Rm)),
                                                 (i64 0))),
                                      VectorIndexS:$idx)))),
            (FMLSv4i32_indexed V128:$Rd, V128:$Rn,
                               (SUBREG_TO_REG (i32 0), V64:$Rm, dsub),
                               VectorIndexS:$idx)>;
  def : Pat<(v4f32 (OpNode (v4f32 V128:$Rd), (v4f32 V128:$Rn),
                           (AArch64dup (f32 (fneg FPR32Op:$Rm))))),
            (FMLSv4i32_indexed V128:$Rd, V128:$Rn,
                (SUBREG_TO_REG (i32 0), FPR32Op:$Rm, ssub), (i64 0))>;

  // 2 variants for the .2d version: DUPLANE from 128-bit, and DUP scalar
  // (DUPLANE from 64-bit would be trivial).
  def : Pat<(v2f64 (OpNode (v2f64 V128:$Rd), (v2f64 V128:$Rn),
                           (AArch64duplane64 (v2f64 (fneg V128:$Rm)),
                                           VectorIndexD:$idx))),
            (FMLSv2i64_indexed
                V128:$Rd, V128:$Rn, V128:$Rm, VectorIndexS:$idx)>;
  def : Pat<(v2f64 (OpNode (v2f64 V128:$Rd), (v2f64 V128:$Rn),
                           (AArch64dup (f64 (fneg FPR64Op:$Rm))))),
            (FMLSv2i64_indexed V128:$Rd, V128:$Rn,
                (SUBREG_TO_REG (i32 0), FPR64Op:$Rm, dsub), (i64 0))>;

  // 2 variants for 32-bit scalar version: extract from .2s or from .4s
  def : Pat<(f32 (OpNode (f32 FPR32:$Rd), (f32 FPR32:$Rn),
                         (vector_extract (v4f32 (fneg V128:$Rm)),
                                         VectorIndexS:$idx))),
            (FMLSv1i32_indexed FPR32:$Rd, FPR32:$Rn,
                V128:$Rm, VectorIndexS:$idx)>;
  def : Pat<(f32 (OpNode (f32 FPR32:$Rd), (f32 FPR32:$Rn),
                         (vector_extract (v4f32 (insert_subvector undef,
                                                    (v2f32 (fneg V64:$Rm)),
                                                    (i64 0))),
                                         VectorIndexS:$idx))),
            (FMLSv1i32_indexed FPR32:$Rd, FPR32:$Rn,
                (SUBREG_TO_REG (i32 0), V64:$Rm, dsub), VectorIndexS:$idx)>;

  // 1 variant for 64-bit scalar version: extract from .1d or from .2d
  def : Pat<(f64 (OpNode (f64 FPR64:$Rd), (f64 FPR64:$Rn),
                         (vector_extract (v2f64 (fneg V128:$Rm)),
                                         VectorIndexS:$idx))),
            (FMLSv1i64_indexed FPR64:$Rd, FPR64:$Rn,
                V128:$Rm, VectorIndexS:$idx)>;
}

defm : FMLSIndexedAfterNegPatterns<
           TriOpFrag<(any_fma node:$RHS, node:$MHS, node:$LHS)> >;
defm : FMLSIndexedAfterNegPatterns<
           TriOpFrag<(any_fma node:$MHS, node:$RHS, node:$LHS)> >;

defm FMULX : SIMDFPIndexed<1, 0b1001, "fmulx", int_aarch64_neon_fmulx>;
defm FMUL  : SIMDFPIndexed<0, 0b1001, "fmul", any_fmul>;

def : Pat<(v2f32 (any_fmul V64:$Rn, (AArch64dup (f32 FPR32:$Rm)))),
          (FMULv2i32_indexed V64:$Rn,
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR32:$Rm, ssub),
            (i64 0))>;
def : Pat<(v4f32 (any_fmul V128:$Rn, (AArch64dup (f32 FPR32:$Rm)))),
          (FMULv4i32_indexed V128:$Rn,
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR32:$Rm, ssub),
            (i64 0))>;
def : Pat<(v2f64 (any_fmul V128:$Rn, (AArch64dup (f64 FPR64:$Rm)))),
          (FMULv2i64_indexed V128:$Rn,
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR64:$Rm, dsub),
            (i64 0))>;

defm SQDMULH : SIMDIndexedHS<0, 0b1100, "sqdmulh", int_aarch64_neon_sqdmulh>;
defm SQRDMULH : SIMDIndexedHS<0, 0b1101, "sqrdmulh", int_aarch64_neon_sqrdmulh>;

defm SQDMULH : SIMDIndexedHSPatterns<int_aarch64_neon_sqdmulh_lane,
                                     int_aarch64_neon_sqdmulh_laneq>;
defm SQRDMULH : SIMDIndexedHSPatterns<int_aarch64_neon_sqrdmulh_lane,
                                      int_aarch64_neon_sqrdmulh_laneq>;

// Generated by MachineCombine
defm MLA   : SIMDVectorIndexedHSTied<1, 0b0000, "mla", null_frag>;
defm MLS   : SIMDVectorIndexedHSTied<1, 0b0100, "mls", null_frag>;

defm MUL   : SIMDVectorIndexedHS<0, 0b1000, "mul", mul>;
defm SMLAL : SIMDVectorIndexedLongSDTied<0, 0b0010, "smlal",
    TriOpFrag<(add node:$LHS, (AArch64smull node:$MHS, node:$RHS))>>;
defm SMLSL : SIMDVectorIndexedLongSDTied<0, 0b0110, "smlsl",
    TriOpFrag<(sub node:$LHS, (AArch64smull node:$MHS, node:$RHS))>>;
defm SMULL : SIMDVectorIndexedLongSD<0, 0b1010, "smull", AArch64smull>;
defm SQDMLAL : SIMDIndexedLongSQDMLXSDTied<0, 0b0011, "sqdmlal",
                                           int_aarch64_neon_sqadd>;
defm SQDMLSL : SIMDIndexedLongSQDMLXSDTied<0, 0b0111, "sqdmlsl",
                                           int_aarch64_neon_sqsub>;
defm SQRDMLAH : SIMDIndexedSQRDMLxHSDTied<1, 0b1101, "sqrdmlah",
                                          int_aarch64_neon_sqrdmlah>;
defm SQRDMLSH : SIMDIndexedSQRDMLxHSDTied<1, 0b1111, "sqrdmlsh",
                                          int_aarch64_neon_sqrdmlsh>;
defm SQDMULL : SIMDIndexedLongSD<0, 0b1011, "sqdmull", int_aarch64_neon_sqdmull>;
defm UMLAL   : SIMDVectorIndexedLongSDTied<1, 0b0010, "umlal",
    TriOpFrag<(add node:$LHS, (AArch64umull node:$MHS, node:$RHS))>>;
defm UMLSL   : SIMDVectorIndexedLongSDTied<1, 0b0110, "umlsl",
    TriOpFrag<(sub node:$LHS, (AArch64umull node:$MHS, node:$RHS))>>;
defm UMULL   : SIMDVectorIndexedLongSD<1, 0b1010, "umull", AArch64umull>;

// A scalar sqdmull with the second operand being a vector lane can be
// handled directly with the indexed instruction encoding.
def : Pat<(int_aarch64_neon_sqdmulls_scalar (i32 FPR32:$Rn),
                                          (vector_extract (v4i32 V128:$Vm),
                                                           VectorIndexS:$idx)),
          (SQDMULLv1i64_indexed FPR32:$Rn, V128:$Vm, VectorIndexS:$idx)>;

//----------------------------------------------------------------------------
// AdvSIMD scalar shift instructions
//----------------------------------------------------------------------------
defm FCVTZS : SIMDFPScalarRShift<0, 0b11111, "fcvtzs">;
defm FCVTZU : SIMDFPScalarRShift<1, 0b11111, "fcvtzu">;
defm SCVTF  : SIMDFPScalarRShift<0, 0b11100, "scvtf">;
defm UCVTF  : SIMDFPScalarRShift<1, 0b11100, "ucvtf">;
// Codegen patterns for the above. We don't put these directly on the
// instructions because TableGen's type inference can't handle the truth.
// Having the same base pattern for fp <--> int totally freaks it out.
def : Pat<(int_aarch64_neon_vcvtfp2fxs FPR32:$Rn, vecshiftR32:$imm),
          (FCVTZSs FPR32:$Rn, vecshiftR32:$imm)>;
def : Pat<(int_aarch64_neon_vcvtfp2fxu FPR32:$Rn, vecshiftR32:$imm),
          (FCVTZUs FPR32:$Rn, vecshiftR32:$imm)>;
def : Pat<(i64 (int_aarch64_neon_vcvtfp2fxs (f64 FPR64:$Rn), vecshiftR64:$imm)),
          (FCVTZSd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(i64 (int_aarch64_neon_vcvtfp2fxu (f64 FPR64:$Rn), vecshiftR64:$imm)),
          (FCVTZUd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(v1i64 (int_aarch64_neon_vcvtfp2fxs (v1f64 FPR64:$Rn),
                                            vecshiftR64:$imm)),
          (FCVTZSd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(v1i64 (int_aarch64_neon_vcvtfp2fxu (v1f64 FPR64:$Rn),
                                            vecshiftR64:$imm)),
          (FCVTZUd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(int_aarch64_neon_vcvtfxu2fp FPR32:$Rn, vecshiftR32:$imm),
          (UCVTFs FPR32:$Rn, vecshiftR32:$imm)>;
def : Pat<(f64 (int_aarch64_neon_vcvtfxu2fp (i64 FPR64:$Rn), vecshiftR64:$imm)),
          (UCVTFd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(v1f64 (int_aarch64_neon_vcvtfxs2fp (v1i64 FPR64:$Rn),
                                            vecshiftR64:$imm)),
          (SCVTFd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(f64 (int_aarch64_neon_vcvtfxs2fp (i64 FPR64:$Rn), vecshiftR64:$imm)),
          (SCVTFd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(v1f64 (int_aarch64_neon_vcvtfxu2fp (v1i64 FPR64:$Rn),
                                            vecshiftR64:$imm)),
          (UCVTFd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(int_aarch64_neon_vcvtfxs2fp FPR32:$Rn, vecshiftR32:$imm),
          (SCVTFs FPR32:$Rn, vecshiftR32:$imm)>;

// Patterns for FP16 Intrinsics - requires reg copy to/from as i16s not supported.

def : Pat<(f16 (int_aarch64_neon_vcvtfxs2fp (i32 (sext_inreg FPR32:$Rn, i16)), vecshiftR16:$imm)),
          (SCVTFh (f16 (EXTRACT_SUBREG FPR32:$Rn, hsub)), vecshiftR16:$imm)>;
def : Pat<(f16 (int_aarch64_neon_vcvtfxs2fp (i32 FPR32:$Rn), vecshiftR16:$imm)),
          (SCVTFh (f16 (EXTRACT_SUBREG FPR32:$Rn, hsub)), vecshiftR16:$imm)>;
def : Pat<(f16 (int_aarch64_neon_vcvtfxs2fp (i64 FPR64:$Rn), vecshiftR16:$imm)),
          (SCVTFh (f16 (EXTRACT_SUBREG FPR64:$Rn, hsub)), vecshiftR16:$imm)>;
def : Pat<(f16 (int_aarch64_neon_vcvtfxu2fp
            (and FPR32:$Rn, (i32 65535)),
            vecshiftR16:$imm)),
          (UCVTFh (f16 (EXTRACT_SUBREG FPR32:$Rn, hsub)), vecshiftR16:$imm)>;
def : Pat<(f16 (int_aarch64_neon_vcvtfxu2fp FPR32:$Rn, vecshiftR16:$imm)),
          (UCVTFh (f16 (EXTRACT_SUBREG FPR32:$Rn, hsub)), vecshiftR16:$imm)>;
def : Pat<(f16 (int_aarch64_neon_vcvtfxu2fp (i64 FPR64:$Rn), vecshiftR16:$imm)),
          (UCVTFh (f16 (EXTRACT_SUBREG FPR64:$Rn, hsub)), vecshiftR16:$imm)>;
def : Pat<(i32 (int_aarch64_neon_vcvtfp2fxs (f16 FPR16:$Rn), vecshiftR32:$imm)),
          (i32 (INSERT_SUBREG
            (i32 (IMPLICIT_DEF)),
            (FCVTZSh FPR16:$Rn, vecshiftR32:$imm),
            hsub))>;
def : Pat<(i64 (int_aarch64_neon_vcvtfp2fxs (f16 FPR16:$Rn), vecshiftR64:$imm)),
          (i64 (INSERT_SUBREG
            (i64 (IMPLICIT_DEF)),
            (FCVTZSh FPR16:$Rn, vecshiftR64:$imm),
            hsub))>;
def : Pat<(i32 (int_aarch64_neon_vcvtfp2fxu (f16 FPR16:$Rn), vecshiftR32:$imm)),
          (i32 (INSERT_SUBREG
            (i32 (IMPLICIT_DEF)),
            (FCVTZUh FPR16:$Rn, vecshiftR32:$imm),
            hsub))>;
def : Pat<(i64 (int_aarch64_neon_vcvtfp2fxu (f16 FPR16:$Rn), vecshiftR64:$imm)),
          (i64 (INSERT_SUBREG
            (i64 (IMPLICIT_DEF)),
            (FCVTZUh FPR16:$Rn, vecshiftR64:$imm),
            hsub))>;
def : Pat<(i32 (int_aarch64_neon_facge (f16 FPR16:$Rn), (f16 FPR16:$Rm))),
          (i32 (INSERT_SUBREG
            (i32 (IMPLICIT_DEF)),
            (FACGE16 FPR16:$Rn, FPR16:$Rm),
            hsub))>;
def : Pat<(i32 (int_aarch64_neon_facgt (f16 FPR16:$Rn), (f16 FPR16:$Rm))),
          (i32 (INSERT_SUBREG
            (i32 (IMPLICIT_DEF)),
            (FACGT16 FPR16:$Rn, FPR16:$Rm),
            hsub))>;

defm SHL      : SIMDScalarLShiftD<   0, 0b01010, "shl", AArch64vshl>;
defm SLI      : SIMDScalarLShiftDTied<1, 0b01010, "sli">;
defm SQRSHRN  : SIMDScalarRShiftBHS< 0, 0b10011, "sqrshrn",
                                     int_aarch64_neon_sqrshrn>;
defm SQRSHRUN : SIMDScalarRShiftBHS< 1, 0b10001, "sqrshrun",
                                     int_aarch64_neon_sqrshrun>;
defm SQSHLU   : SIMDScalarLShiftBHSD<1, 0b01100, "sqshlu", AArch64sqshlui>;
defm SQSHL    : SIMDScalarLShiftBHSD<0, 0b01110, "sqshl", AArch64sqshli>;
defm SQSHRN   : SIMDScalarRShiftBHS< 0, 0b10010, "sqshrn",
                                     int_aarch64_neon_sqshrn>;
defm SQSHRUN  : SIMDScalarRShiftBHS< 1, 0b10000, "sqshrun",
                                     int_aarch64_neon_sqshrun>;
defm SRI      : SIMDScalarRShiftDTied<   1, 0b01000, "sri">;
defm SRSHR    : SIMDScalarRShiftD<   0, 0b00100, "srshr", AArch64srshri>;
defm SRSRA    : SIMDScalarRShiftDTied<   0, 0b00110, "srsra",
    TriOpFrag<(add node:$LHS,
                   (AArch64srshri node:$MHS, node:$RHS))>>;
defm SSHR     : SIMDScalarRShiftD<   0, 0b00000, "sshr", AArch64vashr>;
defm SSRA     : SIMDScalarRShiftDTied<   0, 0b00010, "ssra",
    TriOpFrag<(add_and_or_is_add node:$LHS,
                   (AArch64vashr node:$MHS, node:$RHS))>>;
defm UQRSHRN  : SIMDScalarRShiftBHS< 1, 0b10011, "uqrshrn",
                                     int_aarch64_neon_uqrshrn>;
defm UQSHL    : SIMDScalarLShiftBHSD<1, 0b01110, "uqshl", AArch64uqshli>;
defm UQSHRN   : SIMDScalarRShiftBHS< 1, 0b10010, "uqshrn",
                                     int_aarch64_neon_uqshrn>;
defm URSHR    : SIMDScalarRShiftD<   1, 0b00100, "urshr", AArch64urshri>;
defm URSRA    : SIMDScalarRShiftDTied<   1, 0b00110, "ursra",
    TriOpFrag<(add node:$LHS,
                   (AArch64urshri node:$MHS, node:$RHS))>>;
defm USHR     : SIMDScalarRShiftD<   1, 0b00000, "ushr", AArch64vlshr>;
defm USRA     : SIMDScalarRShiftDTied<   1, 0b00010, "usra",
    TriOpFrag<(add_and_or_is_add node:$LHS,
                   (AArch64vlshr node:$MHS, node:$RHS))>>;

//----------------------------------------------------------------------------
// AdvSIMD vector shift instructions
//----------------------------------------------------------------------------
defm FCVTZS:SIMDVectorRShiftSD<0, 0b11111, "fcvtzs", int_aarch64_neon_vcvtfp2fxs>;
defm FCVTZU:SIMDVectorRShiftSD<1, 0b11111, "fcvtzu", int_aarch64_neon_vcvtfp2fxu>;
defm SCVTF: SIMDVectorRShiftToFP<0, 0b11100, "scvtf",
                                   int_aarch64_neon_vcvtfxs2fp>;
defm RSHRN   : SIMDVectorRShiftNarrowBHS<0, 0b10001, "rshrn", AArch64rshrn>;
defm SHL     : SIMDVectorLShiftBHSD<0, 0b01010, "shl", AArch64vshl>;

let Predicates = [HasNEON] in {
def : Pat<(v2f32 (sint_to_fp (v2i32 (AArch64vashr_exact v2i32:$Vn, i32:$shift)))),
          (SCVTFv2i32_shift $Vn, vecshiftR32:$shift)>;

def : Pat<(v4f32 (sint_to_fp (v4i32 (AArch64vashr_exact v4i32:$Vn, i32:$shift)))),
          (SCVTFv4i32_shift $Vn, vecshiftR32:$shift)>;

def : Pat<(v2f64 (sint_to_fp (v2i64 (AArch64vashr_exact v2i64:$Vn, i32:$shift)))),
          (SCVTFv2i64_shift $Vn, vecshiftR64:$shift)>;
}

let Predicates = [HasNEON, HasFullFP16] in {
def : Pat<(v4f16 (sint_to_fp (v4i16 (AArch64vashr_exact v4i16:$Vn, i32:$shift)))),
          (SCVTFv4i16_shift $Vn, vecshiftR16:$shift)>;

def : Pat<(v8f16 (sint_to_fp (v8i16 (AArch64vashr_exact v8i16:$Vn, i32:$shift)))),
          (SCVTFv8i16_shift $Vn, vecshiftR16:$shift)>;
}

// X << 1 ==> X + X
class SHLToADDPat<ValueType ty, RegisterClass regtype>
  : Pat<(ty (AArch64vshl (ty regtype:$Rn), (i32 1))),
            (!cast<Instruction>("ADD"#ty) regtype:$Rn, regtype:$Rn)>;

def : SHLToADDPat<v16i8, FPR128>;
def : SHLToADDPat<v8i16, FPR128>;
def : SHLToADDPat<v4i32, FPR128>;
def : SHLToADDPat<v2i64, FPR128>;
def : SHLToADDPat<v8i8,  FPR64>;
def : SHLToADDPat<v4i16, FPR64>;
def : SHLToADDPat<v2i32, FPR64>;

defm SHRN    : SIMDVectorRShiftNarrowBHS<0, 0b10000, "shrn",
                          BinOpFrag<(trunc (AArch64vashr node:$LHS, node:$RHS))>>;
defm SLI     : SIMDVectorLShiftBHSDTied<1, 0b01010, "sli", AArch64vsli>;
def : Pat<(v1i64 (AArch64vsli (v1i64 FPR64:$Rd), (v1i64 FPR64:$Rn),
                                      (i32 vecshiftL64:$imm))),
          (SLId FPR64:$Rd, FPR64:$Rn, vecshiftL64:$imm)>;
defm SQRSHRN : SIMDVectorRShiftNarrowBHS<0, 0b10011, "sqrshrn",
                                         int_aarch64_neon_sqrshrn>;
defm SQRSHRUN: SIMDVectorRShiftNarrowBHS<1, 0b10001, "sqrshrun",
                                         int_aarch64_neon_sqrshrun>;
defm SQSHLU : SIMDVectorLShiftBHSD<1, 0b01100, "sqshlu", AArch64sqshlui>;
defm SQSHL  : SIMDVectorLShiftBHSD<0, 0b01110, "sqshl", AArch64sqshli>;
defm SQSHRN  : SIMDVectorRShiftNarrowBHS<0, 0b10010, "sqshrn",
                                         int_aarch64_neon_sqshrn>;
defm SQSHRUN : SIMDVectorRShiftNarrowBHS<1, 0b10000, "sqshrun",
                                         int_aarch64_neon_sqshrun>;
defm SRI     : SIMDVectorRShiftBHSDTied<1, 0b01000, "sri", AArch64vsri>;
def : Pat<(v1i64 (AArch64vsri (v1i64 FPR64:$Rd), (v1i64 FPR64:$Rn),
                                      (i32 vecshiftR64:$imm))),
          (SRId FPR64:$Rd, FPR64:$Rn, vecshiftR64:$imm)>;
defm SRSHR   : SIMDVectorRShiftBHSD<0, 0b00100, "srshr", AArch64srshri>;
defm SRSRA   : SIMDVectorRShiftBHSDTied<0, 0b00110, "srsra",
                 TriOpFrag<(add node:$LHS,
                                (AArch64srshri node:$MHS, node:$RHS))> >;
defm SSHLL   : SIMDVectorLShiftLongBHSD<0, 0b10100, "sshll",
                BinOpFrag<(AArch64vshl (sext node:$LHS), node:$RHS)>>;

defm SSHR    : SIMDVectorRShiftBHSD<0, 0b00000, "sshr", AArch64vashr>;
defm SSRA    : SIMDVectorRShiftBHSDTied<0, 0b00010, "ssra",
                TriOpFrag<(add_and_or_is_add node:$LHS, (AArch64vashr node:$MHS, node:$RHS))>>;
defm UCVTF   : SIMDVectorRShiftToFP<1, 0b11100, "ucvtf",
                        int_aarch64_neon_vcvtfxu2fp>;
defm UQRSHRN : SIMDVectorRShiftNarrowBHS<1, 0b10011, "uqrshrn",
                                         int_aarch64_neon_uqrshrn>;
defm UQSHL   : SIMDVectorLShiftBHSD<1, 0b01110, "uqshl", AArch64uqshli>;
defm UQSHRN  : SIMDVectorRShiftNarrowBHS<1, 0b10010, "uqshrn",
                                         int_aarch64_neon_uqshrn>;
defm URSHR   : SIMDVectorRShiftBHSD<1, 0b00100, "urshr", AArch64urshri>;
defm URSRA   : SIMDVectorRShiftBHSDTied<1, 0b00110, "ursra",
                TriOpFrag<(add node:$LHS,
                               (AArch64urshri node:$MHS, node:$RHS))> >;
defm USHLL   : SIMDVectorLShiftLongBHSD<1, 0b10100, "ushll",
                BinOpFrag<(AArch64vshl (zext node:$LHS), node:$RHS)>>;
defm USHR    : SIMDVectorRShiftBHSD<1, 0b00000, "ushr", AArch64vlshr>;
defm USRA    : SIMDVectorRShiftBHSDTied<1, 0b00010, "usra",
                TriOpFrag<(add_and_or_is_add node:$LHS, (AArch64vlshr node:$MHS, node:$RHS))> >;

def VImm0080:         PatLeaf<(AArch64movi_shift (i32 128), (i32 0))>;
def VImm00008000:     PatLeaf<(AArch64movi_shift (i32 128), (i32 8))>;
def VImm0000000080000000: PatLeaf<(AArch64NvCast (v2f64 (fneg (AArch64NvCast (v4i32 (AArch64movi_shift (i32 128), (i32 24)))))))>;

// RADDHN patterns for when RSHRN shifts by half the size of the vector element
def : Pat<(v8i8 (trunc (AArch64vlshr (add (v8i16 V128:$Vn), VImm0080), (i32 8)))),
          (RADDHNv8i16_v8i8 V128:$Vn, (v8i16 (MOVIv2d_ns (i32 0))))>;
def : Pat<(v4i16 (trunc (AArch64vlshr (add (v4i32 V128:$Vn), VImm00008000), (i32 16)))),
          (RADDHNv4i32_v4i16 V128:$Vn, (v4i32 (MOVIv2d_ns (i32 0))))>;
let AddedComplexity = 5 in
def : Pat<(v2i32 (trunc (AArch64vlshr (add (v2i64 V128:$Vn), VImm0000000080000000), (i32 32)))),
          (RADDHNv2i64_v2i32 V128:$Vn, (v2i64 (MOVIv2d_ns (i32 0))))>;
def : Pat<(v8i8 (int_aarch64_neon_rshrn (v8i16 V128:$Vn), (i32 8))),
          (RADDHNv8i16_v8i8 V128:$Vn, (v8i16 (MOVIv2d_ns (i32 0))))>;
def : Pat<(v4i16 (int_aarch64_neon_rshrn (v4i32 V128:$Vn), (i32 16))),
          (RADDHNv4i32_v4i16 V128:$Vn, (v4i32 (MOVIv2d_ns (i32 0))))>;
def : Pat<(v2i32 (int_aarch64_neon_rshrn (v2i64 V128:$Vn), (i32 32))),
          (RADDHNv2i64_v2i32 V128:$Vn, (v2i64 (MOVIv2d_ns (i32 0))))>;

// RADDHN2 patterns for when RSHRN shifts by half the size of the vector element
def : Pat<(v16i8 (concat_vectors
                 (v8i8 V64:$Vd),
                 (v8i8 (trunc (AArch64vlshr (add (v8i16 V128:$Vn), VImm0080), (i32 8)))))),
          (RADDHNv8i16_v16i8
                 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Vd, dsub), V128:$Vn,
                 (v8i16 (MOVIv2d_ns (i32 0))))>;
def : Pat<(v8i16 (concat_vectors
                 (v4i16 V64:$Vd),
                 (v4i16 (trunc (AArch64vlshr (add (v4i32 V128:$Vn), VImm00008000), (i32 16)))))),
          (RADDHNv4i32_v8i16
                 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Vd, dsub), V128:$Vn,
                 (v4i32 (MOVIv2d_ns (i32 0))))>;
let AddedComplexity = 5 in
def : Pat<(v4i32 (concat_vectors
                 (v2i32 V64:$Vd),
                 (v2i32 (trunc (AArch64vlshr (add (v2i64 V128:$Vn), VImm0000000080000000), (i32 32)))))),
          (RADDHNv2i64_v4i32
                 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Vd, dsub), V128:$Vn,
                 (v2i64 (MOVIv2d_ns (i32 0))))>;
def : Pat<(v16i8 (concat_vectors
                 (v8i8 V64:$Vd),
                 (v8i8 (int_aarch64_neon_rshrn (v8i16 V128:$Vn), (i32 8))))),
          (RADDHNv8i16_v16i8
                 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Vd, dsub), V128:$Vn,
                 (v8i16 (MOVIv2d_ns (i32 0))))>;
def : Pat<(v8i16 (concat_vectors
                 (v4i16 V64:$Vd),
                 (v4i16 (int_aarch64_neon_rshrn (v4i32 V128:$Vn), (i32 16))))),
          (RADDHNv4i32_v8i16
                 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Vd, dsub), V128:$Vn,
                 (v4i32 (MOVIv2d_ns (i32 0))))>;
def : Pat<(v4i32 (concat_vectors
                 (v2i32 V64:$Vd),
                 (v2i32 (int_aarch64_neon_rshrn (v2i64 V128:$Vn), (i32 32))))),
          (RADDHNv2i64_v4i32
                 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Vd, dsub), V128:$Vn,
                 (v2i64 (MOVIv2d_ns (i32 0))))>;

// SHRN patterns for when a logical right shift was used instead of arithmetic
// (the immediate guarantees no sign bits actually end up in the result so it
// doesn't matter).
def : Pat<(v8i8 (trunc (AArch64vlshr (v8i16 V128:$Rn), vecshiftR16Narrow:$imm))),
          (SHRNv8i8_shift V128:$Rn, vecshiftR16Narrow:$imm)>;
def : Pat<(v4i16 (trunc (AArch64vlshr (v4i32 V128:$Rn), vecshiftR32Narrow:$imm))),
          (SHRNv4i16_shift V128:$Rn, vecshiftR32Narrow:$imm)>;
def : Pat<(v2i32 (trunc (AArch64vlshr (v2i64 V128:$Rn), vecshiftR64Narrow:$imm))),
          (SHRNv2i32_shift V128:$Rn, vecshiftR64Narrow:$imm)>;

def : Pat<(v16i8 (concat_vectors (v8i8 V64:$Rd),
                                 (trunc (AArch64vlshr (v8i16 V128:$Rn),
                                                    vecshiftR16Narrow:$imm)))),
          (SHRNv16i8_shift (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub),
                           V128:$Rn, vecshiftR16Narrow:$imm)>;
def : Pat<(v8i16 (concat_vectors (v4i16 V64:$Rd),
                                 (trunc (AArch64vlshr (v4i32 V128:$Rn),
                                                    vecshiftR32Narrow:$imm)))),
          (SHRNv8i16_shift (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub),
                           V128:$Rn, vecshiftR32Narrow:$imm)>;
def : Pat<(v4i32 (concat_vectors (v2i32 V64:$Rd),
                                 (trunc (AArch64vlshr (v2i64 V128:$Rn),
                                                    vecshiftR64Narrow:$imm)))),
          (SHRNv4i32_shift (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub),
                           V128:$Rn, vecshiftR32Narrow:$imm)>;

// Vector sign and zero extensions are implemented with SSHLL and USSHLL.
// Anyexts are implemented as zexts.
def : Pat<(v8i16 (sext   (v8i8 V64:$Rn))),  (SSHLLv8i8_shift  V64:$Rn, (i32 0))>;
def : Pat<(v8i16 (zext   (v8i8 V64:$Rn))),  (USHLLv8i8_shift  V64:$Rn, (i32 0))>;
def : Pat<(v8i16 (anyext (v8i8 V64:$Rn))),  (USHLLv8i8_shift  V64:$Rn, (i32 0))>;
def : Pat<(v4i32 (sext   (v4i16 V64:$Rn))), (SSHLLv4i16_shift V64:$Rn, (i32 0))>;
def : Pat<(v4i32 (zext   (v4i16 V64:$Rn))), (USHLLv4i16_shift V64:$Rn, (i32 0))>;
def : Pat<(v4i32 (anyext (v4i16 V64:$Rn))), (USHLLv4i16_shift V64:$Rn, (i32 0))>;
def : Pat<(v2i64 (sext   (v2i32 V64:$Rn))), (SSHLLv2i32_shift V64:$Rn, (i32 0))>;
def : Pat<(v2i64 (zext   (v2i32 V64:$Rn))), (USHLLv2i32_shift V64:$Rn, (i32 0))>;
def : Pat<(v2i64 (anyext (v2i32 V64:$Rn))), (USHLLv2i32_shift V64:$Rn, (i32 0))>;
// Vector bf16 -> fp32 is implemented morally as a zext + shift.
def : Pat<(v4f32 (any_fpextend (v4bf16 V64:$Rn))), (SHLLv4i16 V64:$Rn)>;
// Also match an extend from the upper half of a 128 bit source register.
def : Pat<(v8i16 (anyext (v8i8 (extract_high_v16i8 (v16i8 V128:$Rn)) ))),
          (USHLLv16i8_shift V128:$Rn, (i32 0))>;
def : Pat<(v8i16 (zext   (v8i8 (extract_high_v16i8 (v16i8 V128:$Rn)) ))),
          (USHLLv16i8_shift V128:$Rn, (i32 0))>;
def : Pat<(v8i16 (sext   (v8i8 (extract_high_v16i8 (v16i8 V128:$Rn)) ))),
          (SSHLLv16i8_shift V128:$Rn, (i32 0))>;
def : Pat<(v4i32 (anyext (v4i16 (extract_high_v8i16 (v8i16 V128:$Rn)) ))),
          (USHLLv8i16_shift V128:$Rn, (i32 0))>;
def : Pat<(v4i32 (zext   (v4i16 (extract_high_v8i16 (v8i16 V128:$Rn)) ))),
          (USHLLv8i16_shift V128:$Rn, (i32 0))>;
def : Pat<(v4i32 (sext   (v4i16 (extract_high_v8i16 (v8i16 V128:$Rn)) ))),
          (SSHLLv8i16_shift V128:$Rn, (i32 0))>;
def : Pat<(v2i64 (anyext (v2i32 (extract_high_v4i32 (v4i32 V128:$Rn)) ))),
          (USHLLv4i32_shift V128:$Rn, (i32 0))>;
def : Pat<(v2i64 (zext   (v2i32 (extract_high_v4i32 (v4i32 V128:$Rn)) ))),
          (USHLLv4i32_shift V128:$Rn, (i32 0))>;
def : Pat<(v2i64 (sext   (v2i32 (extract_high_v4i32 (v4i32 V128:$Rn)) ))),
          (SSHLLv4i32_shift V128:$Rn, (i32 0))>;

let Predicates = [HasNEON] in {
// Vector shift sxtl aliases
def : InstAlias<"sxtl.8h $dst, $src1",
                (SSHLLv8i8_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"sxtl $dst.8h, $src1.8b",
                (SSHLLv8i8_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"sxtl.4s $dst, $src1",
                (SSHLLv4i16_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"sxtl $dst.4s, $src1.4h",
                (SSHLLv4i16_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"sxtl.2d $dst, $src1",
                (SSHLLv2i32_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"sxtl $dst.2d, $src1.2s",
                (SSHLLv2i32_shift V128:$dst, V64:$src1, 0)>;

// Vector shift sxtl2 aliases
def : InstAlias<"sxtl2.8h $dst, $src1",
                (SSHLLv16i8_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"sxtl2 $dst.8h, $src1.16b",
                (SSHLLv16i8_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"sxtl2.4s $dst, $src1",
                (SSHLLv8i16_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"sxtl2 $dst.4s, $src1.8h",
                (SSHLLv8i16_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"sxtl2.2d $dst, $src1",
                (SSHLLv4i32_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"sxtl2 $dst.2d, $src1.4s",
                (SSHLLv4i32_shift V128:$dst, V128:$src1, 0)>;

// Vector shift uxtl aliases
def : InstAlias<"uxtl.8h $dst, $src1",
                (USHLLv8i8_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"uxtl $dst.8h, $src1.8b",
                (USHLLv8i8_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"uxtl.4s $dst, $src1",
                (USHLLv4i16_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"uxtl $dst.4s, $src1.4h",
                (USHLLv4i16_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"uxtl.2d $dst, $src1",
                (USHLLv2i32_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"uxtl $dst.2d, $src1.2s",
                (USHLLv2i32_shift V128:$dst, V64:$src1, 0)>;

// Vector shift uxtl2 aliases
def : InstAlias<"uxtl2.8h $dst, $src1",
                (USHLLv16i8_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"uxtl2 $dst.8h, $src1.16b",
                (USHLLv16i8_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"uxtl2.4s $dst, $src1",
                (USHLLv8i16_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"uxtl2 $dst.4s, $src1.8h",
                (USHLLv8i16_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"uxtl2.2d $dst, $src1",
                (USHLLv4i32_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"uxtl2 $dst.2d, $src1.4s",
                (USHLLv4i32_shift V128:$dst, V128:$src1, 0)>;
}

def abs_f16 :
  OutPatFrag<(ops node:$Rn),
             (EXTRACT_SUBREG (f32 (COPY_TO_REGCLASS
	         (i32 (ANDWri
		   (i32 (COPY_TO_REGCLASS (INSERT_SUBREG (f32 (IMPLICIT_DEF)),
		                           node:$Rn, hsub), GPR32)),
	           (i32 (logical_imm32_XFORM(i32 0x7fff))))),
	         FPR32)), hsub)>;

def : Pat<(f16 (fabs (f16 FPR16:$Rn))), (f16 (abs_f16 (f16 FPR16:$Rn)))>;
def : Pat<(bf16 (fabs (bf16 FPR16:$Rn))), (bf16 (abs_f16 (bf16 FPR16:$Rn)))>;

def neg_f16 :
  OutPatFrag<(ops node:$Rn),
             (EXTRACT_SUBREG (f32 (COPY_TO_REGCLASS
	         (i32 (EORWri
		   (i32 (COPY_TO_REGCLASS (INSERT_SUBREG (f32 (IMPLICIT_DEF)),
		                           node:$Rn, hsub), GPR32)),
	           (i32 (logical_imm32_XFORM(i32 0x8000))))),
	         FPR32)), hsub)>;

def : Pat<(f16 (fneg (f16 FPR16:$Rn))), (f16 (neg_f16 (f16 FPR16:$Rn)))>;
def : Pat<(bf16 (fneg (bf16 FPR16:$Rn))), (bf16 (neg_f16 (bf16 FPR16:$Rn)))>;

let Predicates = [HasNEON] in {
def : Pat<(v4f16 (fabs (v4f16 V64:$Rn))), (v4f16 (BICv4i16 (v4f16 V64:$Rn), (i32 128), (i32 8)))>;
def : Pat<(v4bf16 (fabs (v4bf16 V64:$Rn))), (v4bf16 (BICv4i16 (v4bf16 V64:$Rn), (i32 128), (i32 8)))>;
def : Pat<(v8f16 (fabs (v8f16 V128:$Rn))), (v8f16 (BICv8i16 (v8f16 V128:$Rn), (i32 128), (i32 8)))>;
def : Pat<(v8bf16 (fabs (v8bf16 V128:$Rn))), (v8bf16 (BICv8i16 (v8bf16 V128:$Rn), (i32 128), (i32 8)))>;

def : Pat<(v4f16 (fneg (v4f16 V64:$Rn))), (v4f16 (EORv8i8 (v4f16 V64:$Rn), (MOVIv4i16 (i32 128), (i32 8))))>;
def : Pat<(v4bf16 (fneg (v4bf16 V64:$Rn))), (v4bf16 (EORv8i8 (v4bf16 V64:$Rn), (v4i16 (MOVIv4i16 (i32 0x80), (i32 8)))))>;
def : Pat<(v8f16 (fneg (v8f16 V128:$Rn))), (v8f16 (EORv16i8 (v8f16 V128:$Rn), (MOVIv8i16 (i32 128), (i32 8))))>;
def : Pat<(v8bf16 (fneg (v8bf16 V128:$Rn))), (v8bf16 (EORv16i8 (v8bf16 V128:$Rn), (v8i16 (MOVIv8i16 (i32 0x80), (i32 8)))))>;
}

// If an integer is about to be converted to a floating point value,
// just load it on the floating point unit.
// These patterns are more complex because floating point loads do not
// support sign extension.
// The sign extension has to be explicitly added and is only supported for
// one step: byte-to-half, half-to-word, word-to-doubleword.
// SCVTF GPR -> FPR is 9 cycles.
// SCVTF FPR -> FPR is 4 cyclces.
// (sign extension with lengthen) SXTL FPR -> FPR is 2 cycles.
// Therefore, we can do 2 sign extensions and one SCVTF FPR -> FPR
// and still being faster.
// However, this is not good for code size.
// 8-bits -> float. 2 sizes step-up.
class SExtLoadi8CVTf32Pat<dag addrmode, dag INST>
  : Pat<(f32 (sint_to_fp (i32 (sextloadi8 addrmode)))),
        (SCVTFv1i32 (f32 (EXTRACT_SUBREG
                            (SSHLLv4i16_shift
                              (f64
                                (EXTRACT_SUBREG
                                  (SSHLLv8i8_shift
                                    (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                                        INST,
                                        bsub),
                                    0),
                                  dsub)),
                               0),
                             ssub)))>,
    Requires<[NotForCodeSize, UseAlternateSExtLoadCVTF32, HasNEON]>;

def : SExtLoadi8CVTf32Pat<(ro8.Wpat GPR64sp:$Rn, GPR32:$Rm, ro8.Wext:$ext),
                          (LDRBroW  GPR64sp:$Rn, GPR32:$Rm, ro8.Wext:$ext)>;
def : SExtLoadi8CVTf32Pat<(ro8.Xpat GPR64sp:$Rn, GPR64:$Rm, ro8.Xext:$ext),
                          (LDRBroX  GPR64sp:$Rn, GPR64:$Rm, ro8.Xext:$ext)>;
def : SExtLoadi8CVTf32Pat<(am_indexed8 GPR64sp:$Rn, uimm12s1:$offset),
                          (LDRBui GPR64sp:$Rn, uimm12s1:$offset)>;
def : SExtLoadi8CVTf32Pat<(am_unscaled8 GPR64sp:$Rn, simm9:$offset),
                          (LDURBi GPR64sp:$Rn, simm9:$offset)>;

// 16-bits -> float. 1 size step-up.
class SExtLoadi16CVTf32Pat<dag addrmode, dag INST>
  : Pat<(f32 (sint_to_fp (i32 (sextloadi16 addrmode)))),
        (SCVTFv1i32 (f32 (EXTRACT_SUBREG
                            (SSHLLv4i16_shift
                                (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                                  INST,
                                  hsub),
                                0),
                            ssub)))>,
    Requires<[NotForCodeSize, UseAlternateSExtLoadCVTF32, HasNEON]>;

def : SExtLoadi16CVTf32Pat<(ro16.Wpat GPR64sp:$Rn, GPR32:$Rm, ro16.Wext:$ext),
                           (LDRHroW   GPR64sp:$Rn, GPR32:$Rm, ro16.Wext:$ext)>;
def : SExtLoadi16CVTf32Pat<(ro16.Xpat GPR64sp:$Rn, GPR64:$Rm, ro16.Xext:$ext),
                           (LDRHroX   GPR64sp:$Rn, GPR64:$Rm, ro16.Xext:$ext)>;
def : SExtLoadi16CVTf32Pat<(am_indexed16 GPR64sp:$Rn, uimm12s2:$offset),
                           (LDRHui GPR64sp:$Rn, uimm12s2:$offset)>;
def : SExtLoadi16CVTf32Pat<(am_unscaled16 GPR64sp:$Rn, simm9:$offset),
                           (LDURHi GPR64sp:$Rn, simm9:$offset)>;

// 32-bits to 32-bits are handled in target specific dag combine:
// performIntToFpCombine.
// 64-bits integer to 32-bits floating point, not possible with
// SCVTF on floating point registers (both source and destination
// must have the same size).

// Here are the patterns for 8, 16, 32, and 64-bits to double.
// 8-bits -> double. 3 size step-up: give up.
// 16-bits -> double. 2 size step.
class SExtLoadi16CVTf64Pat<dag addrmode, dag INST>
  : Pat <(f64 (sint_to_fp (i32 (sextloadi16 addrmode)))),
           (SCVTFv1i64 (f64 (EXTRACT_SUBREG
                              (SSHLLv2i32_shift
                                 (f64
                                  (EXTRACT_SUBREG
                                    (SSHLLv4i16_shift
                                      (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                                        INST,
                                        hsub),
                                     0),
                                   dsub)),
                               0),
                             dsub)))>,
    Requires<[NotForCodeSize, UseAlternateSExtLoadCVTF32, HasNEON]>;

def : SExtLoadi16CVTf64Pat<(ro16.Wpat GPR64sp:$Rn, GPR32:$Rm, ro16.Wext:$ext),
                           (LDRHroW GPR64sp:$Rn, GPR32:$Rm, ro16.Wext:$ext)>;
def : SExtLoadi16CVTf64Pat<(ro16.Xpat GPR64sp:$Rn, GPR64:$Rm, ro16.Xext:$ext),
                           (LDRHroX GPR64sp:$Rn, GPR64:$Rm, ro16.Xext:$ext)>;
def : SExtLoadi16CVTf64Pat<(am_indexed16 GPR64sp:$Rn, uimm12s2:$offset),
                           (LDRHui GPR64sp:$Rn, uimm12s2:$offset)>;
def : SExtLoadi16CVTf64Pat<(am_unscaled16 GPR64sp:$Rn, simm9:$offset),
                           (LDURHi GPR64sp:$Rn, simm9:$offset)>;
// 32-bits -> double. 1 size step-up.
class SExtLoadi32CVTf64Pat<dag addrmode, dag INST>
  : Pat <(f64 (sint_to_fp (i32 (load addrmode)))),
           (SCVTFv1i64 (f64 (EXTRACT_SUBREG
                              (SSHLLv2i32_shift
                                (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                                  INST,
                                  ssub),
                               0),
                             dsub)))>,
    Requires<[NotForCodeSize, UseAlternateSExtLoadCVTF32, HasNEON]>;

def : SExtLoadi32CVTf64Pat<(ro32.Wpat GPR64sp:$Rn, GPR32:$Rm, ro32.Wext:$ext),
                           (LDRSroW GPR64sp:$Rn, GPR32:$Rm, ro32.Wext:$ext)>;
def : SExtLoadi32CVTf64Pat<(ro32.Xpat GPR64sp:$Rn, GPR64:$Rm, ro32.Xext:$ext),
                           (LDRSroX GPR64sp:$Rn, GPR64:$Rm, ro32.Xext:$ext)>;
def : SExtLoadi32CVTf64Pat<(am_indexed32 GPR64sp:$Rn, uimm12s4:$offset),
                           (LDRSui GPR64sp:$Rn, uimm12s4:$offset)>;
def : SExtLoadi32CVTf64Pat<(am_unscaled32 GPR64sp:$Rn, simm9:$offset),
                           (LDURSi GPR64sp:$Rn, simm9:$offset)>;

// 64-bits -> double are handled in target specific dag combine:
// performIntToFpCombine.


//----------------------------------------------------------------------------
// AdvSIMD Load-Store Structure
//----------------------------------------------------------------------------
defm LD1 : SIMDLd1Multiple<"ld1">;
defm LD2 : SIMDLd2Multiple<"ld2">;
defm LD3 : SIMDLd3Multiple<"ld3">;
defm LD4 : SIMDLd4Multiple<"ld4">;

defm ST1 : SIMDSt1Multiple<"st1">;
defm ST2 : SIMDSt2Multiple<"st2">;
defm ST3 : SIMDSt3Multiple<"st3">;
defm ST4 : SIMDSt4Multiple<"st4">;

class Ld1Pat<ValueType ty, Instruction INST>
  : Pat<(ty (load GPR64sp:$Rn)), (INST GPR64sp:$Rn)>;

def : Ld1Pat<v16i8, LD1Onev16b>;
def : Ld1Pat<v8i16, LD1Onev8h>;
def : Ld1Pat<v4i32, LD1Onev4s>;
def : Ld1Pat<v2i64, LD1Onev2d>;
def : Ld1Pat<v8i8,  LD1Onev8b>;
def : Ld1Pat<v4i16, LD1Onev4h>;
def : Ld1Pat<v2i32, LD1Onev2s>;
def : Ld1Pat<v1i64, LD1Onev1d>;

class St1Pat<ValueType ty, Instruction INST>
  : Pat<(store ty:$Vt, GPR64sp:$Rn),
        (INST ty:$Vt, GPR64sp:$Rn)>;

def : St1Pat<v16i8, ST1Onev16b>;
def : St1Pat<v8i16, ST1Onev8h>;
def : St1Pat<v4i32, ST1Onev4s>;
def : St1Pat<v2i64, ST1Onev2d>;
def : St1Pat<v8i8,  ST1Onev8b>;
def : St1Pat<v4i16, ST1Onev4h>;
def : St1Pat<v2i32, ST1Onev2s>;
def : St1Pat<v1i64, ST1Onev1d>;

//---
// Single-element
//---

defm LD1R          : SIMDLdR<0, 0b110, 0, "ld1r", "One", 1, 2, 4, 8>;
defm LD2R          : SIMDLdR<1, 0b110, 0, "ld2r", "Two", 2, 4, 8, 16>;
defm LD3R          : SIMDLdR<0, 0b111, 0, "ld3r", "Three", 3, 6, 12, 24>;
defm LD4R          : SIMDLdR<1, 0b111, 0, "ld4r", "Four", 4, 8, 16, 32>;
let mayLoad = 1, hasSideEffects = 0 in {
defm LD1 : SIMDLdSingleBTied<0, 0b000,       "ld1", VecListOneb,   GPR64pi1>;
defm LD1 : SIMDLdSingleHTied<0, 0b010, 0,    "ld1", VecListOneh,   GPR64pi2>;
defm LD1 : SIMDLdSingleSTied<0, 0b100, 0b00, "ld1", VecListOnes,   GPR64pi4>;
defm LD1 : SIMDLdSingleDTied<0, 0b100, 0b01, "ld1", VecListOned,   GPR64pi8>;
defm LD2 : SIMDLdSingleBTied<1, 0b000,       "ld2", VecListTwob,   GPR64pi2>;
defm LD2 : SIMDLdSingleHTied<1, 0b010, 0,    "ld2", VecListTwoh,   GPR64pi4>;
defm LD2 : SIMDLdSingleSTied<1, 0b100, 0b00, "ld2", VecListTwos,   GPR64pi8>;
defm LD2 : SIMDLdSingleDTied<1, 0b100, 0b01, "ld2", VecListTwod,   GPR64pi16>;
defm LD3 : SIMDLdSingleBTied<0, 0b001,       "ld3", VecListThreeb, GPR64pi3>;
defm LD3 : SIMDLdSingleHTied<0, 0b011, 0,    "ld3", VecListThreeh, GPR64pi6>;
defm LD3 : SIMDLdSingleSTied<0, 0b101, 0b00, "ld3", VecListThrees, GPR64pi12>;
defm LD3 : SIMDLdSingleDTied<0, 0b101, 0b01, "ld3", VecListThreed, GPR64pi24>;
defm LD4 : SIMDLdSingleBTied<1, 0b001,       "ld4", VecListFourb,  GPR64pi4>;
defm LD4 : SIMDLdSingleHTied<1, 0b011, 0,    "ld4", VecListFourh,  GPR64pi8>;
defm LD4 : SIMDLdSingleSTied<1, 0b101, 0b00, "ld4", VecListFours,  GPR64pi16>;
defm LD4 : SIMDLdSingleDTied<1, 0b101, 0b01, "ld4", VecListFourd,  GPR64pi32>;
}

def : Pat<(v8i8 (AArch64dup (i32 (extloadi8 GPR64sp:$Rn)))),
          (LD1Rv8b GPR64sp:$Rn)>;
def : Pat<(v16i8 (AArch64dup (i32 (extloadi8 GPR64sp:$Rn)))),
          (LD1Rv16b GPR64sp:$Rn)>;
def : Pat<(v4i16 (AArch64dup (i32 (extloadi16 GPR64sp:$Rn)))),
          (LD1Rv4h GPR64sp:$Rn)>;
def : Pat<(v8i16 (AArch64dup (i32 (extloadi16 GPR64sp:$Rn)))),
          (LD1Rv8h GPR64sp:$Rn)>;
def : Pat<(v2i32 (AArch64dup (i32 (load GPR64sp:$Rn)))),
          (LD1Rv2s GPR64sp:$Rn)>;
def : Pat<(v4i32 (AArch64dup (i32 (load GPR64sp:$Rn)))),
          (LD1Rv4s GPR64sp:$Rn)>;
def : Pat<(v2i64 (AArch64dup (i64 (load GPR64sp:$Rn)))),
          (LD1Rv2d GPR64sp:$Rn)>;
def : Pat<(v1i64 (AArch64dup (i64 (load GPR64sp:$Rn)))),
          (LD1Rv1d GPR64sp:$Rn)>;

def : Pat<(v8i8 (AArch64duplane8 (v16i8 (insert_subvector undef, (v8i8 (load GPR64sp:$Rn)), (i64 0))), (i64 0))),
          (LD1Rv8b GPR64sp:$Rn)>;
def : Pat<(v16i8 (AArch64duplane8 (v16i8 (load GPR64sp:$Rn)), (i64 0))),
          (LD1Rv16b GPR64sp:$Rn)>;
def : Pat<(v4i16 (AArch64duplane16 (v8i16 (insert_subvector undef, (v4i16 (load GPR64sp:$Rn)), (i64 0))), (i64 0))),
          (LD1Rv4h GPR64sp:$Rn)>;
def : Pat<(v8i16 (AArch64duplane16 (v8i16 (load GPR64sp:$Rn)), (i64 0))),
          (LD1Rv8h GPR64sp:$Rn)>;
def : Pat<(v2i32 (AArch64duplane32 (v4i32 (insert_subvector undef, (v2i32 (load GPR64sp:$Rn)), (i64 0))), (i64 0))),
          (LD1Rv2s GPR64sp:$Rn)>;
def : Pat<(v4i32 (AArch64duplane32 (v4i32 (load GPR64sp:$Rn)), (i64 0))),
          (LD1Rv4s GPR64sp:$Rn)>;
def : Pat<(v2i64 (AArch64duplane64 (v2i64 (load GPR64sp:$Rn)), (i64 0))),
          (LD1Rv2d GPR64sp:$Rn)>;

// Grab the floating point version too
def : Pat<(v2f32 (AArch64dup (f32 (load GPR64sp:$Rn)))),
          (LD1Rv2s GPR64sp:$Rn)>;
def : Pat<(v4f32 (AArch64dup (f32 (load GPR64sp:$Rn)))),
          (LD1Rv4s GPR64sp:$Rn)>;
def : Pat<(v2f64 (AArch64dup (f64 (load GPR64sp:$Rn)))),
          (LD1Rv2d GPR64sp:$Rn)>;
def : Pat<(v1f64 (AArch64dup (f64 (load GPR64sp:$Rn)))),
          (LD1Rv1d GPR64sp:$Rn)>;
def : Pat<(v4f16 (AArch64dup (f16 (load GPR64sp:$Rn)))),
          (LD1Rv4h GPR64sp:$Rn)>;
def : Pat<(v8f16 (AArch64dup (f16 (load GPR64sp:$Rn)))),
          (LD1Rv8h GPR64sp:$Rn)>;
def : Pat<(v4bf16 (AArch64dup (bf16 (load GPR64sp:$Rn)))),
          (LD1Rv4h GPR64sp:$Rn)>;
def : Pat<(v8bf16 (AArch64dup (bf16 (load GPR64sp:$Rn)))),
          (LD1Rv8h GPR64sp:$Rn)>;

class Ld1Lane128Pat<SDPatternOperator scalar_load, Operand VecIndex,
                    ValueType VTy, ValueType STy, Instruction LD1>
  : Pat<(vector_insert (VTy VecListOne128:$Rd),
           (STy (scalar_load GPR64sp:$Rn)), (i64 VecIndex:$idx)),
        (LD1 VecListOne128:$Rd, VecIndex:$idx, GPR64sp:$Rn)>;

def : Ld1Lane128Pat<extloadi8,  VectorIndexB, v16i8, i32, LD1i8>;
def : Ld1Lane128Pat<extloadi16, VectorIndexH, v8i16, i32, LD1i16>;
def : Ld1Lane128Pat<load,       VectorIndexS, v4i32, i32, LD1i32>;
def : Ld1Lane128Pat<load,       VectorIndexS, v4f32, f32, LD1i32>;
def : Ld1Lane128Pat<load,       VectorIndexD, v2i64, i64, LD1i64>;
def : Ld1Lane128Pat<load,       VectorIndexD, v2f64, f64, LD1i64>;
def : Ld1Lane128Pat<load,       VectorIndexH, v8f16, f16, LD1i16>;
def : Ld1Lane128Pat<load,       VectorIndexH, v8bf16, bf16, LD1i16>;

// Generate LD1 for extload if memory type does not match the
// destination type, for example:
//
//   (v4i32 (insert_vector_elt (load anyext from i8) idx))
//
// In this case, the index must be adjusted to match LD1 type.
//
class Ld1Lane128IdxOpPat<SDPatternOperator scalar_load, Operand
                         VecIndex, ValueType VTy, ValueType STy,
                         Instruction LD1, SDNodeXForm IdxOp>
  : Pat<(vector_insert (VTy VecListOne128:$Rd),
                       (STy (scalar_load GPR64sp:$Rn)), (i64 VecIndex:$idx)),
        (LD1 VecListOne128:$Rd, (IdxOp VecIndex:$idx), GPR64sp:$Rn)>;

class Ld1Lane64IdxOpPat<SDPatternOperator scalar_load, Operand VecIndex,
                        ValueType VTy, ValueType STy, Instruction LD1,
                        SDNodeXForm IdxOp>
  : Pat<(vector_insert (VTy VecListOne64:$Rd),
                       (STy (scalar_load GPR64sp:$Rn)), (i64 VecIndex:$idx)),
        (EXTRACT_SUBREG
            (LD1 (SUBREG_TO_REG (i32 0), VecListOne64:$Rd, dsub),
                (IdxOp VecIndex:$idx), GPR64sp:$Rn),
            dsub)>;

def VectorIndexStoH : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue() * 2, SDLoc(N), MVT::i64);
}]>;
def VectorIndexStoB : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue() * 4, SDLoc(N), MVT::i64);
}]>;
def VectorIndexHtoB : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue() * 2, SDLoc(N), MVT::i64);
}]>;

def : Ld1Lane128IdxOpPat<extloadi16, VectorIndexS, v4i32, i32, LD1i16, VectorIndexStoH>;
def : Ld1Lane128IdxOpPat<extloadi8, VectorIndexS, v4i32, i32, LD1i8, VectorIndexStoB>;
def : Ld1Lane128IdxOpPat<extloadi8, VectorIndexH, v8i16, i32, LD1i8, VectorIndexHtoB>;

def : Ld1Lane64IdxOpPat<extloadi16, VectorIndexS, v2i32, i32, LD1i16, VectorIndexStoH>;
def : Ld1Lane64IdxOpPat<extloadi8, VectorIndexS, v2i32, i32, LD1i8, VectorIndexStoB>;
def : Ld1Lane64IdxOpPat<extloadi8, VectorIndexH, v4i16, i32, LD1i8, VectorIndexHtoB>;

// Same as above, but the first element is populated using
// scalar_to_vector + insert_subvector instead of insert_vector_elt.
let Predicates = [HasNEON] in {
  class Ld1Lane128FirstElm<ValueType ResultTy, ValueType VecTy,
                          SDPatternOperator ExtLoad, Instruction LD1>
    : Pat<(ResultTy (scalar_to_vector (i32 (ExtLoad GPR64sp:$Rn)))),
            (ResultTy (EXTRACT_SUBREG
              (LD1 (VecTy (IMPLICIT_DEF)), 0, GPR64sp:$Rn), dsub))>;

  def : Ld1Lane128FirstElm<v2i32, v8i16, extloadi16, LD1i16>;
  def : Ld1Lane128FirstElm<v2i32, v16i8, extloadi8, LD1i8>;
  def : Ld1Lane128FirstElm<v4i16, v16i8, extloadi8, LD1i8>;
}
class Ld1Lane64Pat<SDPatternOperator scalar_load, Operand VecIndex,
                   ValueType VTy, ValueType STy, Instruction LD1>
  : Pat<(vector_insert (VTy VecListOne64:$Rd),
           (STy (scalar_load GPR64sp:$Rn)), (i64 VecIndex:$idx)),
        (EXTRACT_SUBREG
            (LD1 (SUBREG_TO_REG (i32 0), VecListOne64:$Rd, dsub),
                          VecIndex:$idx, GPR64sp:$Rn),
            dsub)>;

def : Ld1Lane64Pat<extloadi8,  VectorIndexB, v8i8,  i32, LD1i8>;
def : Ld1Lane64Pat<extloadi16, VectorIndexH, v4i16, i32, LD1i16>;
def : Ld1Lane64Pat<load,       VectorIndexS, v2i32, i32, LD1i32>;
def : Ld1Lane64Pat<load,       VectorIndexS, v2f32, f32, LD1i32>;
def : Ld1Lane64Pat<load,       VectorIndexH, v4f16, f16, LD1i16>;
def : Ld1Lane64Pat<load,       VectorIndexH, v4bf16, bf16, LD1i16>;


defm LD1 : SIMDLdSt1SingleAliases<"ld1">;
defm LD2 : SIMDLdSt2SingleAliases<"ld2">;
defm LD3 : SIMDLdSt3SingleAliases<"ld3">;
defm LD4 : SIMDLdSt4SingleAliases<"ld4">;

// Stores
defm ST1 : SIMDStSingleB<0, 0b000,       "st1", VecListOneb, GPR64pi1>;
defm ST1 : SIMDStSingleH<0, 0b010, 0,    "st1", VecListOneh, GPR64pi2>;
defm ST1 : SIMDStSingleS<0, 0b100, 0b00, "st1", VecListOnes, GPR64pi4>;
defm ST1 : SIMDStSingleD<0, 0b100, 0b01, "st1", VecListOned, GPR64pi8>;

let AddedComplexity = 19 in
class St1Lane128Pat<SDPatternOperator scalar_store, Operand VecIndex,
                    ValueType VTy, ValueType STy, Instruction ST1>
  : Pat<(scalar_store
             (STy (vector_extract (VTy VecListOne128:$Vt), VecIndex:$idx)),
             GPR64sp:$Rn),
        (ST1 VecListOne128:$Vt, VecIndex:$idx, GPR64sp:$Rn)>;

def : St1Lane128Pat<truncstorei8,  VectorIndexB, v16i8, i32, ST1i8>;
def : St1Lane128Pat<truncstorei16, VectorIndexH, v8i16, i32, ST1i16>;
def : St1Lane128Pat<store,         VectorIndexS, v4i32, i32, ST1i32>;
def : St1Lane128Pat<store,         VectorIndexS, v4f32, f32, ST1i32>;
def : St1Lane128Pat<store,         VectorIndexD, v2i64, i64, ST1i64>;
def : St1Lane128Pat<store,         VectorIndexD, v2f64, f64, ST1i64>;
def : St1Lane128Pat<store,         VectorIndexH, v8f16, f16, ST1i16>;
def : St1Lane128Pat<store,         VectorIndexH, v8bf16, bf16, ST1i16>;

let AddedComplexity = 19 in
class St1Lane64Pat<SDPatternOperator scalar_store, Operand VecIndex,
                   ValueType VTy, ValueType STy, Instruction ST1>
  : Pat<(scalar_store
             (STy (vector_extract (VTy VecListOne64:$Vt), VecIndex:$idx)),
             GPR64sp:$Rn),
        (ST1 (SUBREG_TO_REG (i32 0), VecListOne64:$Vt, dsub),
             VecIndex:$idx, GPR64sp:$Rn)>;

def : St1Lane64Pat<truncstorei8,  VectorIndexB, v8i8, i32, ST1i8>;
def : St1Lane64Pat<truncstorei16, VectorIndexH, v4i16, i32, ST1i16>;
def : St1Lane64Pat<store,         VectorIndexS, v2i32, i32, ST1i32>;
def : St1Lane64Pat<store,         VectorIndexS, v2f32, f32, ST1i32>;
def : St1Lane64Pat<store,         VectorIndexH, v4f16, f16, ST1i16>;
def : St1Lane64Pat<store,         VectorIndexH, v4bf16, bf16, ST1i16>;

multiclass St1LanePost64Pat<SDPatternOperator scalar_store, Operand VecIndex,
                             ValueType VTy, ValueType STy, Instruction ST1,
                             int offset> {
  def : Pat<(scalar_store
              (STy (vector_extract (VTy VecListOne64:$Vt), VecIndex:$idx)),
              GPR64sp:$Rn, offset),
        (ST1 (SUBREG_TO_REG (i32 0), VecListOne64:$Vt, dsub),
             VecIndex:$idx, GPR64sp:$Rn, XZR)>;

  def : Pat<(scalar_store
              (STy (vector_extract (VTy VecListOne64:$Vt), VecIndex:$idx)),
              GPR64sp:$Rn, GPR64:$Rm),
        (ST1 (SUBREG_TO_REG (i32 0), VecListOne64:$Vt, dsub),
             VecIndex:$idx, GPR64sp:$Rn, $Rm)>;
}

defm : St1LanePost64Pat<post_truncsti8, VectorIndexB, v8i8, i32, ST1i8_POST, 1>;
defm : St1LanePost64Pat<post_truncsti16, VectorIndexH, v4i16, i32, ST1i16_POST,
                        2>;
defm : St1LanePost64Pat<post_store, VectorIndexS, v2i32, i32, ST1i32_POST, 4>;
defm : St1LanePost64Pat<post_store, VectorIndexS, v2f32, f32, ST1i32_POST, 4>;
defm : St1LanePost64Pat<post_store, VectorIndexD, v1i64, i64, ST1i64_POST, 8>;
defm : St1LanePost64Pat<post_store, VectorIndexD, v1f64, f64, ST1i64_POST, 8>;
defm : St1LanePost64Pat<post_store, VectorIndexH, v4f16, f16, ST1i16_POST, 2>;
defm : St1LanePost64Pat<post_store, VectorIndexH, v4bf16, bf16, ST1i16_POST, 2>;

multiclass St1LanePost128Pat<SDPatternOperator scalar_store, Operand VecIndex,
                             ValueType VTy, ValueType STy, Instruction ST1,
                             int offset> {
  def : Pat<(scalar_store
              (STy (vector_extract (VTy VecListOne128:$Vt), VecIndex:$idx)),
              GPR64sp:$Rn, offset),
        (ST1 VecListOne128:$Vt, VecIndex:$idx, GPR64sp:$Rn, XZR)>;

  def : Pat<(scalar_store
              (STy (vector_extract (VTy VecListOne128:$Vt), VecIndex:$idx)),
              GPR64sp:$Rn, GPR64:$Rm),
        (ST1 VecListOne128:$Vt, VecIndex:$idx, GPR64sp:$Rn, $Rm)>;
}

defm : St1LanePost128Pat<post_truncsti8, VectorIndexB, v16i8, i32, ST1i8_POST,
                         1>;
defm : St1LanePost128Pat<post_truncsti16, VectorIndexH, v8i16, i32, ST1i16_POST,
                         2>;
defm : St1LanePost128Pat<post_store, VectorIndexS, v4i32, i32, ST1i32_POST, 4>;
defm : St1LanePost128Pat<post_store, VectorIndexS, v4f32, f32, ST1i32_POST, 4>;
defm : St1LanePost128Pat<post_store, VectorIndexD, v2i64, i64, ST1i64_POST, 8>;
defm : St1LanePost128Pat<post_store, VectorIndexD, v2f64, f64, ST1i64_POST, 8>;
defm : St1LanePost128Pat<post_store, VectorIndexH, v8f16, f16, ST1i16_POST, 2>;
defm : St1LanePost128Pat<post_store, VectorIndexH, v8bf16, bf16, ST1i16_POST, 2>;

let mayStore = 1, hasSideEffects = 0 in {
defm ST2 : SIMDStSingleB<1, 0b000,       "st2", VecListTwob,   GPR64pi2>;
defm ST2 : SIMDStSingleH<1, 0b010, 0,    "st2", VecListTwoh,   GPR64pi4>;
defm ST2 : SIMDStSingleS<1, 0b100, 0b00, "st2", VecListTwos,   GPR64pi8>;
defm ST2 : SIMDStSingleD<1, 0b100, 0b01, "st2", VecListTwod,   GPR64pi16>;
defm ST3 : SIMDStSingleB<0, 0b001,       "st3", VecListThreeb, GPR64pi3>;
defm ST3 : SIMDStSingleH<0, 0b011, 0,    "st3", VecListThreeh, GPR64pi6>;
defm ST3 : SIMDStSingleS<0, 0b101, 0b00, "st3", VecListThrees, GPR64pi12>;
defm ST3 : SIMDStSingleD<0, 0b101, 0b01, "st3", VecListThreed, GPR64pi24>;
defm ST4 : SIMDStSingleB<1, 0b001,       "st4", VecListFourb,  GPR64pi4>;
defm ST4 : SIMDStSingleH<1, 0b011, 0,    "st4", VecListFourh,  GPR64pi8>;
defm ST4 : SIMDStSingleS<1, 0b101, 0b00, "st4", VecListFours,  GPR64pi16>;
defm ST4 : SIMDStSingleD<1, 0b101, 0b01, "st4", VecListFourd,  GPR64pi32>;
}

defm ST1 : SIMDLdSt1SingleAliases<"st1">;
defm ST2 : SIMDLdSt2SingleAliases<"st2">;
defm ST3 : SIMDLdSt3SingleAliases<"st3">;
defm ST4 : SIMDLdSt4SingleAliases<"st4">;

//----------------------------------------------------------------------------
// Crypto extensions
//----------------------------------------------------------------------------

let Predicates = [HasAES] in {
let isCommutable = 1 in {
def AESErr   : AESTiedInst<0b0100, "aese",   int_aarch64_crypto_aese>;
def AESDrr   : AESTiedInst<0b0101, "aesd",   int_aarch64_crypto_aesd>;
}
def AESMCrr  : AESInst<    0b0110, "aesmc",  int_aarch64_crypto_aesmc>;
def AESIMCrr : AESInst<    0b0111, "aesimc", int_aarch64_crypto_aesimc>;
}

// Pseudo instructions for AESMCrr/AESIMCrr with a register constraint required
// for AES fusion on some CPUs.
let hasSideEffects = 0, mayStore = 0, mayLoad = 0, Predicates = [HasAES] in {
def AESMCrrTied: Pseudo<(outs V128:$Rd), (ins V128:$Rn), [], "$Rn = $Rd">,
                        Sched<[WriteVq]>;
def AESIMCrrTied: Pseudo<(outs V128:$Rd), (ins V128:$Rn), [], "$Rn = $Rd">,
                         Sched<[WriteVq]>;
}

// Only use constrained versions of AES(I)MC instructions if they are paired with
// AESE/AESD.
def : Pat<(v16i8 (int_aarch64_crypto_aesmc
            (v16i8 (int_aarch64_crypto_aese (v16i8 V128:$src1),
                                            (v16i8 V128:$src2))))),
          (v16i8 (AESMCrrTied (v16i8 (AESErr (v16i8 V128:$src1),
                                             (v16i8 V128:$src2)))))>,
          Requires<[HasFuseAES]>;

def : Pat<(v16i8 (int_aarch64_crypto_aesimc
            (v16i8 (int_aarch64_crypto_aesd (v16i8 V128:$src1),
                                            (v16i8 V128:$src2))))),
          (v16i8 (AESIMCrrTied (v16i8 (AESDrr (v16i8 V128:$src1),
                                              (v16i8 V128:$src2)))))>,
          Requires<[HasFuseAES]>;

let Predicates = [HasSHA2] in {
def SHA1Crrr     : SHATiedInstQSV<0b000, "sha1c",   int_aarch64_crypto_sha1c>;
def SHA1Prrr     : SHATiedInstQSV<0b001, "sha1p",   int_aarch64_crypto_sha1p>;
def SHA1Mrrr     : SHATiedInstQSV<0b010, "sha1m",   int_aarch64_crypto_sha1m>;
def SHA1SU0rrr   : SHATiedInstVVV<0b011, "sha1su0", int_aarch64_crypto_sha1su0>;
def SHA256Hrrr   : SHATiedInstQQV<0b100, "sha256h", int_aarch64_crypto_sha256h>;
def SHA256H2rrr  : SHATiedInstQQV<0b101, "sha256h2",int_aarch64_crypto_sha256h2>;
def SHA256SU1rrr :SHATiedInstVVV<0b110, "sha256su1",int_aarch64_crypto_sha256su1>;

def SHA1Hrr     : SHAInstSS<    0b0000, "sha1h",    int_aarch64_crypto_sha1h>;
def SHA1SU1rr   : SHATiedInstVV<0b0001, "sha1su1",  int_aarch64_crypto_sha1su1>;
def SHA256SU0rr : SHATiedInstVV<0b0010, "sha256su0",int_aarch64_crypto_sha256su0>;
}

//----------------------------------------------------------------------------
// Compiler-pseudos
//----------------------------------------------------------------------------
// FIXME: Like for X86, these should go in their own separate .td file.

// For an anyext, we don't care what the high bits are, so we can perform an
// INSERT_SUBREF into an IMPLICIT_DEF.
def : Pat<(i64 (anyext GPR32:$src)),
          (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GPR32:$src, sub_32)>;

// When we need to explicitly zero-extend, we use a 32-bit MOV instruction and
// then assert the extension has happened.
def : Pat<(i64 (zext GPR32:$src)),
          (SUBREG_TO_REG (i32 0), (ORRWrs WZR, GPR32:$src, 0), sub_32)>;

// To sign extend, we use a signed bitfield move instruction (SBFM) on the
// containing super-reg.
def : Pat<(i64 (sext GPR32:$src)),
   (SBFMXri (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GPR32:$src, sub_32), 0, 31)>;
def : Pat<(i64 (sext_inreg GPR64:$src, i32)), (SBFMXri GPR64:$src, 0, 31)>;
def : Pat<(i64 (sext_inreg GPR64:$src, i16)), (SBFMXri GPR64:$src, 0, 15)>;
def : Pat<(i64 (sext_inreg GPR64:$src, i8)),  (SBFMXri GPR64:$src, 0, 7)>;
def : Pat<(i64 (sext_inreg GPR64:$src, i1)),  (SBFMXri GPR64:$src, 0, 0)>;
def : Pat<(i32 (sext_inreg GPR32:$src, i16)), (SBFMWri GPR32:$src, 0, 15)>;
def : Pat<(i32 (sext_inreg GPR32:$src, i8)),  (SBFMWri GPR32:$src, 0, 7)>;
def : Pat<(i32 (sext_inreg GPR32:$src, i1)),  (SBFMWri GPR32:$src, 0, 0)>;

def : Pat<(shl (sext_inreg GPR32:$Rn, i8), (i64 imm0_31:$imm)),
          (SBFMWri GPR32:$Rn, (i64 (i32shift_a       imm0_31:$imm)),
                              (i64 (i32shift_sext_i8 imm0_31:$imm)))>;
def : Pat<(shl (sext_inreg GPR64:$Rn, i8), (i64 imm0_63:$imm)),
          (SBFMXri GPR64:$Rn, (i64 (i64shift_a imm0_63:$imm)),
                              (i64 (i64shift_sext_i8 imm0_63:$imm)))>;

def : Pat<(shl (sext_inreg GPR32:$Rn, i16), (i64 imm0_31:$imm)),
          (SBFMWri GPR32:$Rn, (i64 (i32shift_a        imm0_31:$imm)),
                              (i64 (i32shift_sext_i16 imm0_31:$imm)))>;
def : Pat<(shl (sext_inreg GPR64:$Rn, i16), (i64 imm0_63:$imm)),
          (SBFMXri GPR64:$Rn, (i64 (i64shift_a        imm0_63:$imm)),
                              (i64 (i64shift_sext_i16 imm0_63:$imm)))>;

def : Pat<(shl (i64 (sext GPR32:$Rn)), (i64 imm0_63:$imm)),
          (SBFMXri (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GPR32:$Rn, sub_32),
                   (i64 (i64shift_a        imm0_63:$imm)),
                   (i64 (i64shift_sext_i32 imm0_63:$imm)))>;

def : Pat<(shl (i64 (zext GPR32:$Rn)), (i64 imm0_63:$imm)),
          (UBFMXri (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GPR32:$Rn, sub_32),
                   (i64 (i64shift_a        imm0_63:$imm)),
                   (i64 (i64shift_sext_i32 imm0_63:$imm)))>;

// sra patterns have an AddedComplexity of 10, so make sure we have a higher
// AddedComplexity for the following patterns since we want to match sext + sra
// patterns before we attempt to match a single sra node.
let AddedComplexity = 20 in {
// We support all sext + sra combinations which preserve at least one bit of the
// original value which is to be sign extended. E.g. we support shifts up to
// bitwidth-1 bits.
def : Pat<(sra (sext_inreg GPR32:$Rn, i8), (i64 imm0_7:$imm)),
          (SBFMWri GPR32:$Rn, (i64 imm0_7:$imm), 7)>;
def : Pat<(sra (sext_inreg GPR64:$Rn, i8), (i64 imm0_7:$imm)),
          (SBFMXri GPR64:$Rn, (i64 imm0_7:$imm), 7)>;

def : Pat<(sra (sext_inreg GPR32:$Rn, i16), (i64 imm0_15:$imm)),
          (SBFMWri GPR32:$Rn, (i64 imm0_15:$imm), 15)>;
def : Pat<(sra (sext_inreg GPR64:$Rn, i16), (i64 imm0_15:$imm)),
          (SBFMXri GPR64:$Rn, (i64 imm0_15:$imm), 15)>;

def : Pat<(sra (i64 (sext GPR32:$Rn)), (i64 imm0_31:$imm)),
          (SBFMXri (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GPR32:$Rn, sub_32),
                   (i64 imm0_31:$imm), 31)>;
} // AddedComplexity = 20

// To truncate, we can simply extract from a subregister.
def : Pat<(i32 (trunc GPR64sp:$src)),
          (i32 (EXTRACT_SUBREG GPR64sp:$src, sub_32))>;

// __builtin_trap() uses the BRK instruction on AArch64.
def : Pat<(trap), (BRK 1)>;
def : Pat<(debugtrap), (BRK 0xF000)>;

def ubsan_trap_xform : SDNodeXForm<timm, [{
  return CurDAG->getTargetConstant(N->getZExtValue() | ('U' << 8), SDLoc(N), MVT::i32);
}]>;

def gi_ubsan_trap_xform : GICustomOperandRenderer<"renderUbsanTrap">,
  GISDNodeXFormEquiv<ubsan_trap_xform>;

def ubsan_trap_imm : TImmLeaf<i32, [{
  return isUInt<8>(Imm);
}], ubsan_trap_xform>;

def : Pat<(ubsantrap ubsan_trap_imm:$kind), (BRK ubsan_trap_imm:$kind)>;

// Multiply high patterns which multiply the lower subvector using smull/umull
// and the upper subvector with smull2/umull2. Then shuffle the high the high
// part of both results together.
def : Pat<(v16i8 (mulhs V128:$Rn, V128:$Rm)),
          (UZP2v16i8
           (SMULLv8i8_v8i16 (EXTRACT_SUBREG V128:$Rn, dsub),
                            (EXTRACT_SUBREG V128:$Rm, dsub)),
           (SMULLv16i8_v8i16 V128:$Rn, V128:$Rm))>;
def : Pat<(v8i16 (mulhs V128:$Rn, V128:$Rm)),
          (UZP2v8i16
           (SMULLv4i16_v4i32 (EXTRACT_SUBREG V128:$Rn, dsub),
                             (EXTRACT_SUBREG V128:$Rm, dsub)),
           (SMULLv8i16_v4i32 V128:$Rn, V128:$Rm))>;
def : Pat<(v4i32 (mulhs V128:$Rn, V128:$Rm)),
          (UZP2v4i32
           (SMULLv2i32_v2i64 (EXTRACT_SUBREG V128:$Rn, dsub),
                             (EXTRACT_SUBREG V128:$Rm, dsub)),
           (SMULLv4i32_v2i64 V128:$Rn, V128:$Rm))>;

def : Pat<(v16i8 (mulhu V128:$Rn, V128:$Rm)),
          (UZP2v16i8
           (UMULLv8i8_v8i16 (EXTRACT_SUBREG V128:$Rn, dsub),
                            (EXTRACT_SUBREG V128:$Rm, dsub)),
           (UMULLv16i8_v8i16 V128:$Rn, V128:$Rm))>;
def : Pat<(v8i16 (mulhu V128:$Rn, V128:$Rm)),
          (UZP2v8i16
           (UMULLv4i16_v4i32 (EXTRACT_SUBREG V128:$Rn, dsub),
                             (EXTRACT_SUBREG V128:$Rm, dsub)),
           (UMULLv8i16_v4i32 V128:$Rn, V128:$Rm))>;
def : Pat<(v4i32 (mulhu V128:$Rn, V128:$Rm)),
          (UZP2v4i32
           (UMULLv2i32_v2i64 (EXTRACT_SUBREG V128:$Rn, dsub),
                             (EXTRACT_SUBREG V128:$Rm, dsub)),
           (UMULLv4i32_v2i64 V128:$Rn, V128:$Rm))>;

// Conversions within AdvSIMD types in the same register size are free.
// But because we need a consistent lane ordering, in big endian many
// conversions require one or more REV instructions.
//
// Consider a simple memory load followed by a bitconvert then a store.
//   v0 = load v2i32
//   v1 = BITCAST v2i32 v0 to v4i16
//        store v4i16 v2
//
// In big endian mode every memory access has an implicit byte swap. LDR and
// STR do a 64-bit byte swap, whereas LD1/ST1 do a byte swap per lane - that
// is, they treat the vector as a sequence of elements to be byte-swapped.
// The two pairs of instructions are fundamentally incompatible. We've decided
// to use LD1/ST1 only to simplify compiler implementation.
//
// LD1/ST1 perform the equivalent of a sequence of LDR/STR + REV. This makes
// the original code sequence:
//   v0 = load v2i32
//   v1 = REV v2i32                  (implicit)
//   v2 = BITCAST v2i32 v1 to v4i16
//   v3 = REV v4i16 v2               (implicit)
//        store v4i16 v3
//
// But this is now broken - the value stored is different to the value loaded
// due to lane reordering. To fix this, on every BITCAST we must perform two
// other REVs:
//   v0 = load v2i32
//   v1 = REV v2i32                  (implicit)
//   v2 = REV v2i32
//   v3 = BITCAST v2i32 v2 to v4i16
//   v4 = REV v4i16
//   v5 = REV v4i16 v4               (implicit)
//        store v4i16 v5
//
// This means an extra two instructions, but actually in most cases the two REV
// instructions can be combined into one. For example:
//   (REV64_2s (REV64_4h X)) === (REV32_4h X)
//
// There is also no 128-bit REV instruction. This must be synthesized with an
// EXT instruction.
//
// Most bitconverts require some sort of conversion. The only exceptions are:
//   a) Identity conversions -  vNfX <-> vNiX
//   b) Single-lane-to-scalar - v1fX <-> fX or v1iX <-> iX
//

// Natural vector casts (64 bit)
foreach VT = [ v8i8, v4i16, v4f16, v4bf16, v2i32, v2f32, v1i64, v1f64, f64 ] in
  foreach VT2 = [ v8i8, v4i16, v4f16, v4bf16, v2i32, v2f32, v1i64, v1f64, f64 ] in
    def : Pat<(VT (AArch64NvCast (VT2 FPR64:$src))),
              (VT FPR64:$src)>;

// Natural vector casts (128 bit)
foreach VT = [ v16i8, v8i16, v8f16, v8bf16, v4i32, v4f32, v2i64, v2f64 ] in
  foreach VT2 = [ v16i8, v8i16, v8f16, v8bf16, v4i32, v4f32, v2i64, v2f64 ] in
    def : Pat<(VT (AArch64NvCast (VT2 FPR128:$src))),
              (VT FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v8i8  (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v4i16 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v2i32 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v4f16 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v4bf16 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v2f32 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;

def : Pat<(i64 (bitconvert (v8i8  V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(i64 (bitconvert (v4i16 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(i64 (bitconvert (v2i32 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(i64 (bitconvert (v4f16 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(i64 (bitconvert (v4bf16 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(i64 (bitconvert (v2f32 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(i64 (bitconvert (v1f64 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
}
let Predicates = [IsBE] in {
def : Pat<(v8i8  (bitconvert GPR64:$Xn)),
                 (REV64v8i8 (COPY_TO_REGCLASS GPR64:$Xn, FPR64))>;
def : Pat<(v4i16 (bitconvert GPR64:$Xn)),
                 (REV64v4i16 (COPY_TO_REGCLASS GPR64:$Xn, FPR64))>;
def : Pat<(v2i32 (bitconvert GPR64:$Xn)),
                 (REV64v2i32 (COPY_TO_REGCLASS GPR64:$Xn, FPR64))>;
def : Pat<(v4f16 (bitconvert GPR64:$Xn)),
                 (REV64v4i16 (COPY_TO_REGCLASS GPR64:$Xn, FPR64))>;
def : Pat<(v4bf16 (bitconvert GPR64:$Xn)),
                  (REV64v4i16 (COPY_TO_REGCLASS GPR64:$Xn, FPR64))>;
def : Pat<(v2f32 (bitconvert GPR64:$Xn)),
                 (REV64v2i32 (COPY_TO_REGCLASS GPR64:$Xn, FPR64))>;

def : Pat<(i64 (bitconvert (v8i8  V64:$Vn))),
          (REV64v8i8 (COPY_TO_REGCLASS V64:$Vn, GPR64))>;
def : Pat<(i64 (bitconvert (v4i16 V64:$Vn))),
          (REV64v4i16 (COPY_TO_REGCLASS V64:$Vn, GPR64))>;
def : Pat<(i64 (bitconvert (v2i32 V64:$Vn))),
          (REV64v2i32 (COPY_TO_REGCLASS V64:$Vn, GPR64))>;
def : Pat<(i64 (bitconvert (v4f16 V64:$Vn))),
          (REV64v4i16 (COPY_TO_REGCLASS V64:$Vn, GPR64))>;
def : Pat<(i64 (bitconvert (v4bf16 V64:$Vn))),
          (REV64v4i16 (COPY_TO_REGCLASS V64:$Vn, GPR64))>;
def : Pat<(i64 (bitconvert (v2f32 V64:$Vn))),
          (REV64v2i32 (COPY_TO_REGCLASS V64:$Vn, GPR64))>;
}
def : Pat<(v1i64 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v1f64 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(i64 (bitconvert (v1i64 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(v1i64 (scalar_to_vector GPR64:$Xn)),
          (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v1f64 (scalar_to_vector GPR64:$Xn)),
          (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v1f64 (scalar_to_vector (f64 FPR64:$Xn))), (v1f64 FPR64:$Xn)>;

def : Pat<(f32 (bitconvert (i32 GPR32:$Xn))),
          (COPY_TO_REGCLASS GPR32:$Xn, FPR32)>;
def : Pat<(i32 (bitconvert (f32 FPR32:$Xn))),
          (COPY_TO_REGCLASS FPR32:$Xn, GPR32)>;
def : Pat<(f64 (bitconvert (i64 GPR64:$Xn))),
          (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(i64 (bitconvert (f64 FPR64:$Xn))),
          (COPY_TO_REGCLASS FPR64:$Xn, GPR64)>;
def : Pat<(i64 (bitconvert (v1f64 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;

def : Pat<(f16 (bitconvert (bf16 FPR16:$src))), (f16 FPR16:$src)>;
def : Pat<(bf16 (bitconvert (f16 FPR16:$src))), (bf16 FPR16:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v1i64 (bitconvert (v2i32 FPR64:$src))), (v1i64 FPR64:$src)>;
def : Pat<(v1i64 (bitconvert (v4i16 FPR64:$src))), (v1i64 FPR64:$src)>;
def : Pat<(v1i64 (bitconvert (v8i8  FPR64:$src))), (v1i64 FPR64:$src)>;
def : Pat<(v1i64 (bitconvert (v4f16 FPR64:$src))), (v1i64 FPR64:$src)>;
def : Pat<(v1i64 (bitconvert (v4bf16 FPR64:$src))), (v1i64 FPR64:$src)>;
def : Pat<(v1i64 (bitconvert (v2f32 FPR64:$src))), (v1i64 FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v1i64 (bitconvert (v2i32 FPR64:$src))),
                             (v1i64 (REV64v2i32 FPR64:$src))>;
def : Pat<(v1i64 (bitconvert (v4i16 FPR64:$src))),
                             (v1i64 (REV64v4i16 FPR64:$src))>;
def : Pat<(v1i64 (bitconvert (v8i8  FPR64:$src))),
                             (v1i64 (REV64v8i8 FPR64:$src))>;
def : Pat<(v1i64 (bitconvert (v4f16 FPR64:$src))),
                             (v1i64 (REV64v4i16 FPR64:$src))>;
def : Pat<(v1i64 (bitconvert (v4bf16 FPR64:$src))),
                             (v1i64 (REV64v4i16 FPR64:$src))>;
def : Pat<(v1i64 (bitconvert (v2f32 FPR64:$src))),
                             (v1i64 (REV64v2i32 FPR64:$src))>;
}
def : Pat<(v1i64 (bitconvert (v1f64 FPR64:$src))), (v1i64 FPR64:$src)>;
def : Pat<(v1i64 (bitconvert (f64   FPR64:$src))), (v1i64 FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v2i32 (bitconvert (v1i64 FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2i32 (bitconvert (v4i16 FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2i32 (bitconvert (v8i8  FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2i32 (bitconvert (f64   FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2i32 (bitconvert (v1f64 FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2i32 (bitconvert (v4f16 FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2i32 (bitconvert (v4bf16 FPR64:$src))), (v2i32 FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v2i32 (bitconvert (v1i64 FPR64:$src))),
                             (v2i32 (REV64v2i32 FPR64:$src))>;
def : Pat<(v2i32 (bitconvert (v4i16 FPR64:$src))),
                             (v2i32 (REV32v4i16 FPR64:$src))>;
def : Pat<(v2i32 (bitconvert (v8i8  FPR64:$src))),
                             (v2i32 (REV32v8i8 FPR64:$src))>;
def : Pat<(v2i32 (bitconvert (f64   FPR64:$src))),
                             (v2i32 (REV64v2i32 FPR64:$src))>;
def : Pat<(v2i32 (bitconvert (v1f64 FPR64:$src))),
                             (v2i32 (REV64v2i32 FPR64:$src))>;
def : Pat<(v2i32 (bitconvert (v4f16 FPR64:$src))),
                             (v2i32 (REV32v4i16 FPR64:$src))>;
def : Pat<(v2i32 (bitconvert (v4bf16 FPR64:$src))),
                             (v2i32 (REV32v4i16 FPR64:$src))>;
}
def : Pat<(v2i32 (bitconvert (v2f32 FPR64:$src))), (v2i32 FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v4i16 (bitconvert (v1i64 FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4i16 (bitconvert (v2i32 FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4i16 (bitconvert (v8i8  FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4i16 (bitconvert (f64   FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4i16 (bitconvert (v2f32 FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4i16 (bitconvert (v1f64 FPR64:$src))), (v4i16 FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v4i16 (bitconvert (v1i64 FPR64:$src))),
                             (v4i16 (REV64v4i16 FPR64:$src))>;
def : Pat<(v4i16 (bitconvert (v2i32 FPR64:$src))),
                             (v4i16 (REV32v4i16 FPR64:$src))>;
def : Pat<(v4i16 (bitconvert (v8i8  FPR64:$src))),
                             (v4i16 (REV16v8i8 FPR64:$src))>;
def : Pat<(v4i16 (bitconvert (f64   FPR64:$src))),
                             (v4i16 (REV64v4i16 FPR64:$src))>;
def : Pat<(v4i16 (bitconvert (v2f32 FPR64:$src))),
                             (v4i16 (REV32v4i16 FPR64:$src))>;
def : Pat<(v4i16 (bitconvert (v1f64 FPR64:$src))),
                             (v4i16 (REV64v4i16 FPR64:$src))>;
}
def : Pat<(v4i16 (bitconvert (v4f16 FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4i16 (bitconvert (v4bf16 FPR64:$src))), (v4i16 FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v4f16 (bitconvert (v1i64 FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v4f16 (bitconvert (v2i32 FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v4f16 (bitconvert (v8i8  FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v4f16 (bitconvert (f64   FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v4f16 (bitconvert (v2f32 FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v4f16 (bitconvert (v1f64 FPR64:$src))), (v4f16 FPR64:$src)>;

def : Pat<(v4bf16 (bitconvert (v1i64 FPR64:$src))), (v4bf16 FPR64:$src)>;
def : Pat<(v4bf16 (bitconvert (v2i32 FPR64:$src))), (v4bf16 FPR64:$src)>;
def : Pat<(v4bf16 (bitconvert (v8i8  FPR64:$src))), (v4bf16 FPR64:$src)>;
def : Pat<(v4bf16 (bitconvert (f64   FPR64:$src))), (v4bf16 FPR64:$src)>;
def : Pat<(v4bf16 (bitconvert (v2f32 FPR64:$src))), (v4bf16 FPR64:$src)>;
def : Pat<(v4bf16 (bitconvert (v1f64 FPR64:$src))), (v4bf16 FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v4f16 (bitconvert (v1i64 FPR64:$src))),
                             (v4f16 (REV64v4i16 FPR64:$src))>;
def : Pat<(v4f16 (bitconvert (v2i32 FPR64:$src))),
                             (v4f16 (REV32v4i16 FPR64:$src))>;
def : Pat<(v4f16 (bitconvert (v8i8  FPR64:$src))),
                             (v4f16 (REV16v8i8 FPR64:$src))>;
def : Pat<(v4f16 (bitconvert (f64   FPR64:$src))),
                             (v4f16 (REV64v4i16 FPR64:$src))>;
def : Pat<(v4f16 (bitconvert (v2f32 FPR64:$src))),
                             (v4f16 (REV32v4i16 FPR64:$src))>;
def : Pat<(v4f16 (bitconvert (v1f64 FPR64:$src))),
                             (v4f16 (REV64v4i16 FPR64:$src))>;

def : Pat<(v4bf16 (bitconvert (v1i64 FPR64:$src))),
                             (v4bf16 (REV64v4i16 FPR64:$src))>;
def : Pat<(v4bf16 (bitconvert (v2i32 FPR64:$src))),
                             (v4bf16 (REV32v4i16 FPR64:$src))>;
def : Pat<(v4bf16 (bitconvert (v8i8  FPR64:$src))),
                             (v4bf16 (REV16v8i8 FPR64:$src))>;
def : Pat<(v4bf16 (bitconvert (f64   FPR64:$src))),
                             (v4bf16 (REV64v4i16 FPR64:$src))>;
def : Pat<(v4bf16 (bitconvert (v2f32 FPR64:$src))),
                             (v4bf16 (REV32v4i16 FPR64:$src))>;
def : Pat<(v4bf16 (bitconvert (v1f64 FPR64:$src))),
                             (v4bf16 (REV64v4i16 FPR64:$src))>;
}
def : Pat<(v4f16 (bitconvert (v4i16 FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v4bf16 (bitconvert (v4i16 FPR64:$src))), (v4bf16 FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v8i8  (bitconvert (v1i64 FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (v2i32 FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (v4i16 FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (f64   FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (v2f32 FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (v1f64 FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (v4f16 FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (v4bf16 FPR64:$src))), (v8i8  FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v8i8  (bitconvert (v1i64 FPR64:$src))),
                             (v8i8 (REV64v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (v2i32 FPR64:$src))),
                             (v8i8 (REV32v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (v4i16 FPR64:$src))),
                             (v8i8 (REV16v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (f64   FPR64:$src))),
                             (v8i8 (REV64v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (v2f32 FPR64:$src))),
                             (v8i8 (REV32v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (v1f64 FPR64:$src))),
                             (v8i8 (REV64v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (v4f16 FPR64:$src))),
                             (v8i8 (REV16v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (v4bf16 FPR64:$src))),
                             (v8i8 (REV16v8i8 FPR64:$src))>;
}

let Predicates = [IsLE] in {
def : Pat<(f64   (bitconvert (v2i32 FPR64:$src))), (f64   FPR64:$src)>;
def : Pat<(f64   (bitconvert (v4i16 FPR64:$src))), (f64   FPR64:$src)>;
def : Pat<(f64   (bitconvert (v2f32 FPR64:$src))), (f64   FPR64:$src)>;
def : Pat<(f64   (bitconvert (v8i8  FPR64:$src))), (f64   FPR64:$src)>;
def : Pat<(f64   (bitconvert (v4f16 FPR64:$src))), (f64   FPR64:$src)>;
def : Pat<(f64   (bitconvert (v4bf16 FPR64:$src))), (f64   FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(f64   (bitconvert (v2i32 FPR64:$src))),
                             (f64 (REV64v2i32 FPR64:$src))>;
def : Pat<(f64   (bitconvert (v4i16 FPR64:$src))),
                             (f64 (REV64v4i16 FPR64:$src))>;
def : Pat<(f64   (bitconvert (v2f32 FPR64:$src))),
                             (f64 (REV64v2i32 FPR64:$src))>;
def : Pat<(f64   (bitconvert (v8i8  FPR64:$src))),
                             (f64 (REV64v8i8 FPR64:$src))>;
def : Pat<(f64   (bitconvert (v4f16 FPR64:$src))),
                             (f64 (REV64v4i16 FPR64:$src))>;
def : Pat<(f64   (bitconvert (v4bf16 FPR64:$src))),
                             (f64 (REV64v4i16 FPR64:$src))>;
}
def : Pat<(f64   (bitconvert (v1i64 FPR64:$src))), (f64   FPR64:$src)>;
def : Pat<(f64   (bitconvert (v1f64 FPR64:$src))), (f64   FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v1f64 (bitconvert (v2i32 FPR64:$src))), (v1f64 FPR64:$src)>;
def : Pat<(v1f64 (bitconvert (v4i16 FPR64:$src))), (v1f64 FPR64:$src)>;
def : Pat<(v1f64 (bitconvert (v8i8  FPR64:$src))), (v1f64 FPR64:$src)>;
def : Pat<(v1f64 (bitconvert (v2f32 FPR64:$src))), (v1f64 FPR64:$src)>;
def : Pat<(v1f64 (bitconvert (v4f16 FPR64:$src))), (v1f64 FPR64:$src)>;
def : Pat<(v1f64 (bitconvert (v4bf16 FPR64:$src))), (v1f64 FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v1f64 (bitconvert (v2i32 FPR64:$src))),
                             (v1f64 (REV64v2i32 FPR64:$src))>;
def : Pat<(v1f64 (bitconvert (v4i16 FPR64:$src))),
                             (v1f64 (REV64v4i16 FPR64:$src))>;
def : Pat<(v1f64 (bitconvert (v8i8  FPR64:$src))),
                             (v1f64 (REV64v8i8 FPR64:$src))>;
def : Pat<(v1f64 (bitconvert (v2f32 FPR64:$src))),
                             (v1f64 (REV64v2i32 FPR64:$src))>;
def : Pat<(v1f64 (bitconvert (v4f16 FPR64:$src))),
                             (v1f64 (REV64v4i16 FPR64:$src))>;
def : Pat<(v1f64 (bitconvert (v4bf16 FPR64:$src))),
                             (v1f64 (REV64v4i16 FPR64:$src))>;
}
def : Pat<(v1f64 (bitconvert (v1i64 FPR64:$src))), (v1f64 FPR64:$src)>;
def : Pat<(v1f64 (bitconvert (f64   FPR64:$src))), (v1f64 FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v2f32 (bitconvert (v1i64 FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v2f32 (bitconvert (v4i16 FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v2f32 (bitconvert (v8i8  FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v2f32 (bitconvert (v1f64 FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v2f32 (bitconvert (f64   FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v2f32 (bitconvert (v4f16 FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v2f32 (bitconvert (v4bf16 FPR64:$src))), (v2f32 FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v2f32 (bitconvert (v1i64 FPR64:$src))),
                             (v2f32 (REV64v2i32 FPR64:$src))>;
def : Pat<(v2f32 (bitconvert (v4i16 FPR64:$src))),
                             (v2f32 (REV32v4i16 FPR64:$src))>;
def : Pat<(v2f32 (bitconvert (v8i8  FPR64:$src))),
                             (v2f32 (REV32v8i8 FPR64:$src))>;
def : Pat<(v2f32 (bitconvert (v1f64 FPR64:$src))),
                             (v2f32 (REV64v2i32 FPR64:$src))>;
def : Pat<(v2f32 (bitconvert (f64   FPR64:$src))),
                             (v2f32 (REV64v2i32 FPR64:$src))>;
def : Pat<(v2f32 (bitconvert (v4f16 FPR64:$src))),
                             (v2f32 (REV32v4i16 FPR64:$src))>;
def : Pat<(v2f32 (bitconvert (v4bf16 FPR64:$src))),
                             (v2f32 (REV32v4i16 FPR64:$src))>;
}
def : Pat<(v2f32 (bitconvert (v2i32 FPR64:$src))), (v2f32 FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(f128 (bitconvert (v2i64 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v4i32 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v8i16 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v2f64 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v4f32 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v8f16 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v8bf16 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v16i8 FPR128:$src))), (f128 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(f128 (bitconvert (v2i64 FPR128:$src))),
                            (f128 (EXTv16i8 FPR128:$src, FPR128:$src, (i32 8)))>;
def : Pat<(f128 (bitconvert (v4i32 FPR128:$src))),
                            (f128 (EXTv16i8 (REV64v4i32 FPR128:$src),
                                            (REV64v4i32 FPR128:$src), (i32 8)))>;
def : Pat<(f128 (bitconvert (v8i16 FPR128:$src))),
                            (f128 (EXTv16i8 (REV64v8i16 FPR128:$src),
                                            (REV64v8i16 FPR128:$src), (i32 8)))>;
def : Pat<(f128 (bitconvert (v8f16 FPR128:$src))),
                            (f128 (EXTv16i8 (REV64v8i16 FPR128:$src),
                                            (REV64v8i16 FPR128:$src), (i32 8)))>;
def : Pat<(f128 (bitconvert (v8bf16 FPR128:$src))),
                            (f128 (EXTv16i8 (REV64v8i16 FPR128:$src),
                                            (REV64v8i16 FPR128:$src), (i32 8)))>;
def : Pat<(f128 (bitconvert (v2f64 FPR128:$src))),
                            (f128 (EXTv16i8 FPR128:$src, FPR128:$src, (i32 8)))>;
def : Pat<(f128 (bitconvert (v4f32 FPR128:$src))),
                            (f128 (EXTv16i8 (REV64v4i32 FPR128:$src),
                                            (REV64v4i32 FPR128:$src), (i32 8)))>;
def : Pat<(f128 (bitconvert (v16i8 FPR128:$src))),
                            (f128 (EXTv16i8 (REV64v16i8 FPR128:$src),
                                            (REV64v16i8 FPR128:$src), (i32 8)))>;
}

let Predicates = [IsLE] in {
def : Pat<(v2f64 (bitconvert (f128  FPR128:$src))), (v2f64 FPR128:$src)>;
def : Pat<(v2f64 (bitconvert (v4i32 FPR128:$src))), (v2f64 FPR128:$src)>;
def : Pat<(v2f64 (bitconvert (v8i16 FPR128:$src))), (v2f64 FPR128:$src)>;
def : Pat<(v2f64 (bitconvert (v8f16 FPR128:$src))), (v2f64 FPR128:$src)>;
def : Pat<(v2f64 (bitconvert (v8bf16 FPR128:$src))), (v2f64 FPR128:$src)>;
def : Pat<(v2f64 (bitconvert (v16i8 FPR128:$src))), (v2f64 FPR128:$src)>;
def : Pat<(v2f64 (bitconvert (v4f32 FPR128:$src))), (v2f64 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v2f64 (bitconvert (f128  FPR128:$src))),
                             (v2f64 (EXTv16i8 FPR128:$src,
                                              FPR128:$src, (i32 8)))>;
def : Pat<(v2f64 (bitconvert (v4i32 FPR128:$src))),
                             (v2f64 (REV64v4i32 FPR128:$src))>;
def : Pat<(v2f64 (bitconvert (v8i16 FPR128:$src))),
                             (v2f64 (REV64v8i16 FPR128:$src))>;
def : Pat<(v2f64 (bitconvert (v8f16 FPR128:$src))),
                             (v2f64 (REV64v8i16 FPR128:$src))>;
def : Pat<(v2f64 (bitconvert (v8bf16 FPR128:$src))),
                             (v2f64 (REV64v8i16 FPR128:$src))>;
def : Pat<(v2f64 (bitconvert (v16i8 FPR128:$src))),
                             (v2f64 (REV64v16i8 FPR128:$src))>;
def : Pat<(v2f64 (bitconvert (v4f32 FPR128:$src))),
                             (v2f64 (REV64v4i32 FPR128:$src))>;
}
def : Pat<(v2f64 (bitconvert (v2i64 FPR128:$src))), (v2f64 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v4f32 (bitconvert (f128  FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v4f32 (bitconvert (v8i16 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v4f32 (bitconvert (v8f16 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v4f32 (bitconvert (v8bf16 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v4f32 (bitconvert (v16i8 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v4f32 (bitconvert (v2i64 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v4f32 (bitconvert (v2f64 FPR128:$src))), (v4f32 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v4f32 (bitconvert (f128  FPR128:$src))),
                             (v4f32 (EXTv16i8 (REV64v4i32 FPR128:$src),
                                    (REV64v4i32 FPR128:$src), (i32 8)))>;
def : Pat<(v4f32 (bitconvert (v8i16 FPR128:$src))),
                             (v4f32 (REV32v8i16 FPR128:$src))>;
def : Pat<(v4f32 (bitconvert (v8f16 FPR128:$src))),
                             (v4f32 (REV32v8i16 FPR128:$src))>;
def : Pat<(v4f32 (bitconvert (v8bf16 FPR128:$src))),
                             (v4f32 (REV32v8i16 FPR128:$src))>;
def : Pat<(v4f32 (bitconvert (v16i8 FPR128:$src))),
                             (v4f32 (REV32v16i8 FPR128:$src))>;
def : Pat<(v4f32 (bitconvert (v2i64 FPR128:$src))),
                             (v4f32 (REV64v4i32 FPR128:$src))>;
def : Pat<(v4f32 (bitconvert (v2f64 FPR128:$src))),
                             (v4f32 (REV64v4i32 FPR128:$src))>;
}
def : Pat<(v4f32 (bitconvert (v4i32 FPR128:$src))), (v4f32 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v2i64 (bitconvert (f128  FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2i64 (bitconvert (v4i32 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2i64 (bitconvert (v8i16 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2i64 (bitconvert (v16i8 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2i64 (bitconvert (v4f32 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2i64 (bitconvert (v8f16 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2i64 (bitconvert (v8bf16 FPR128:$src))), (v2i64 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v2i64 (bitconvert (f128  FPR128:$src))),
                             (v2i64 (EXTv16i8 FPR128:$src,
                                              FPR128:$src, (i32 8)))>;
def : Pat<(v2i64 (bitconvert (v4i32 FPR128:$src))),
                             (v2i64 (REV64v4i32 FPR128:$src))>;
def : Pat<(v2i64 (bitconvert (v8i16 FPR128:$src))),
                             (v2i64 (REV64v8i16 FPR128:$src))>;
def : Pat<(v2i64 (bitconvert (v16i8 FPR128:$src))),
                             (v2i64 (REV64v16i8 FPR128:$src))>;
def : Pat<(v2i64 (bitconvert (v4f32 FPR128:$src))),
                             (v2i64 (REV64v4i32 FPR128:$src))>;
def : Pat<(v2i64 (bitconvert (v8f16 FPR128:$src))),
                             (v2i64 (REV64v8i16 FPR128:$src))>;
def : Pat<(v2i64 (bitconvert (v8bf16 FPR128:$src))),
                             (v2i64 (REV64v8i16 FPR128:$src))>;
}
def : Pat<(v2i64 (bitconvert (v2f64 FPR128:$src))), (v2i64 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v4i32 (bitconvert (f128  FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4i32 (bitconvert (v2i64 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4i32 (bitconvert (v8i16 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4i32 (bitconvert (v16i8 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4i32 (bitconvert (v2f64 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4i32 (bitconvert (v8f16 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4i32 (bitconvert (v8bf16 FPR128:$src))), (v4i32 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v4i32 (bitconvert (f128  FPR128:$src))),
                             (v4i32 (EXTv16i8 (REV64v4i32 FPR128:$src),
                                              (REV64v4i32 FPR128:$src),
                                              (i32 8)))>;
def : Pat<(v4i32 (bitconvert (v2i64 FPR128:$src))),
                             (v4i32 (REV64v4i32 FPR128:$src))>;
def : Pat<(v4i32 (bitconvert (v8i16 FPR128:$src))),
                             (v4i32 (REV32v8i16 FPR128:$src))>;
def : Pat<(v4i32 (bitconvert (v16i8 FPR128:$src))),
                             (v4i32 (REV32v16i8 FPR128:$src))>;
def : Pat<(v4i32 (bitconvert (v2f64 FPR128:$src))),
                             (v4i32 (REV64v4i32 FPR128:$src))>;
def : Pat<(v4i32 (bitconvert (v8f16 FPR128:$src))),
                             (v4i32 (REV32v8i16 FPR128:$src))>;
def : Pat<(v4i32 (bitconvert (v8bf16 FPR128:$src))),
                             (v4i32 (REV32v8i16 FPR128:$src))>;
}
def : Pat<(v4i32 (bitconvert (v4f32 FPR128:$src))), (v4i32 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v8i16 (bitconvert (f128  FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8i16 (bitconvert (v2i64 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8i16 (bitconvert (v4i32 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8i16 (bitconvert (v16i8 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8i16 (bitconvert (v2f64 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8i16 (bitconvert (v4f32 FPR128:$src))), (v8i16 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v8i16 (bitconvert (f128  FPR128:$src))),
                             (v8i16 (EXTv16i8 (REV64v8i16 FPR128:$src),
                                              (REV64v8i16 FPR128:$src),
                                              (i32 8)))>;
def : Pat<(v8i16 (bitconvert (v2i64 FPR128:$src))),
                             (v8i16 (REV64v8i16 FPR128:$src))>;
def : Pat<(v8i16 (bitconvert (v4i32 FPR128:$src))),
                             (v8i16 (REV32v8i16 FPR128:$src))>;
def : Pat<(v8i16 (bitconvert (v16i8 FPR128:$src))),
                             (v8i16 (REV16v16i8 FPR128:$src))>;
def : Pat<(v8i16 (bitconvert (v2f64 FPR128:$src))),
                             (v8i16 (REV64v8i16 FPR128:$src))>;
def : Pat<(v8i16 (bitconvert (v4f32 FPR128:$src))),
                             (v8i16 (REV32v8i16 FPR128:$src))>;
}
def : Pat<(v8i16 (bitconvert (v8f16 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8i16 (bitconvert (v8bf16 FPR128:$src))), (v8i16 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v8f16 (bitconvert (f128  FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v8f16 (bitconvert (v2i64 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v8f16 (bitconvert (v4i32 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v8f16 (bitconvert (v16i8 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v8f16 (bitconvert (v2f64 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v8f16 (bitconvert (v4f32 FPR128:$src))), (v8f16 FPR128:$src)>;

def : Pat<(v8bf16 (bitconvert (f128  FPR128:$src))), (v8bf16 FPR128:$src)>;
def : Pat<(v8bf16 (bitconvert (v2i64 FPR128:$src))), (v8bf16 FPR128:$src)>;
def : Pat<(v8bf16 (bitconvert (v4i32 FPR128:$src))), (v8bf16 FPR128:$src)>;
def : Pat<(v8bf16 (bitconvert (v16i8 FPR128:$src))), (v8bf16 FPR128:$src)>;
def : Pat<(v8bf16 (bitconvert (v2f64 FPR128:$src))), (v8bf16 FPR128:$src)>;
def : Pat<(v8bf16 (bitconvert (v4f32 FPR128:$src))), (v8bf16 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v8f16 (bitconvert (f128  FPR128:$src))),
                             (v8f16 (EXTv16i8 (REV64v8i16 FPR128:$src),
                                              (REV64v8i16 FPR128:$src),
                                              (i32 8)))>;
def : Pat<(v8f16 (bitconvert (v2i64 FPR128:$src))),
                             (v8f16 (REV64v8i16 FPR128:$src))>;
def : Pat<(v8f16 (bitconvert (v4i32 FPR128:$src))),
                             (v8f16 (REV32v8i16 FPR128:$src))>;
def : Pat<(v8f16 (bitconvert (v16i8 FPR128:$src))),
                             (v8f16 (REV16v16i8 FPR128:$src))>;
def : Pat<(v8f16 (bitconvert (v2f64 FPR128:$src))),
                             (v8f16 (REV64v8i16 FPR128:$src))>;
def : Pat<(v8f16 (bitconvert (v4f32 FPR128:$src))),
                             (v8f16 (REV32v8i16 FPR128:$src))>;

def : Pat<(v8bf16 (bitconvert (f128  FPR128:$src))),
                             (v8bf16 (EXTv16i8 (REV64v8i16 FPR128:$src),
                                              (REV64v8i16 FPR128:$src),
                                              (i32 8)))>;
def : Pat<(v8bf16 (bitconvert (v2i64 FPR128:$src))),
                             (v8bf16 (REV64v8i16 FPR128:$src))>;
def : Pat<(v8bf16 (bitconvert (v4i32 FPR128:$src))),
                             (v8bf16 (REV32v8i16 FPR128:$src))>;
def : Pat<(v8bf16 (bitconvert (v16i8 FPR128:$src))),
                             (v8bf16 (REV16v16i8 FPR128:$src))>;
def : Pat<(v8bf16 (bitconvert (v2f64 FPR128:$src))),
                             (v8bf16 (REV64v8i16 FPR128:$src))>;
def : Pat<(v8bf16 (bitconvert (v4f32 FPR128:$src))),
                             (v8bf16 (REV32v8i16 FPR128:$src))>;
}
def : Pat<(v8f16 (bitconvert (v8i16 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v8bf16 (bitconvert (v8i16 FPR128:$src))), (v8bf16 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v16i8 (bitconvert (f128  FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v2i64 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v4i32 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v8i16 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v2f64 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v4f32 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v8f16 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v8bf16 FPR128:$src))), (v16i8 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v16i8 (bitconvert (f128  FPR128:$src))),
                             (v16i8 (EXTv16i8 (REV64v16i8 FPR128:$src),
                                              (REV64v16i8 FPR128:$src),
                                              (i32 8)))>;
def : Pat<(v16i8 (bitconvert (v2i64 FPR128:$src))),
                             (v16i8 (REV64v16i8 FPR128:$src))>;
def : Pat<(v16i8 (bitconvert (v4i32 FPR128:$src))),
                             (v16i8 (REV32v16i8 FPR128:$src))>;
def : Pat<(v16i8 (bitconvert (v8i16 FPR128:$src))),
                             (v16i8 (REV16v16i8 FPR128:$src))>;
def : Pat<(v16i8 (bitconvert (v2f64 FPR128:$src))),
                             (v16i8 (REV64v16i8 FPR128:$src))>;
def : Pat<(v16i8 (bitconvert (v4f32 FPR128:$src))),
                             (v16i8 (REV32v16i8 FPR128:$src))>;
def : Pat<(v16i8 (bitconvert (v8f16 FPR128:$src))),
                             (v16i8 (REV16v16i8 FPR128:$src))>;
def : Pat<(v16i8 (bitconvert (v8bf16 FPR128:$src))),
                             (v16i8 (REV16v16i8 FPR128:$src))>;
}

def : Pat<(v4i16 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v8i8 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v2f32 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v4f16 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v4bf16 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v2i32 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v1i64 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v1f64 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;

def : Pat<(v8i8 (extract_subvector (v16i8 FPR128:$Rn), (i64 1))),
          (EXTRACT_SUBREG (DUPv2i64lane FPR128:$Rn, 1), dsub)>;
def : Pat<(v4i16 (extract_subvector (v8i16 FPR128:$Rn), (i64 1))),
          (EXTRACT_SUBREG (DUPv2i64lane FPR128:$Rn, 1), dsub)>;
def : Pat<(v2i32 (extract_subvector (v4i32 FPR128:$Rn), (i64 1))),
          (EXTRACT_SUBREG (DUPv2i64lane FPR128:$Rn, 1), dsub)>;
def : Pat<(v1i64 (extract_subvector (v2i64 FPR128:$Rn), (i64 1))),
          (EXTRACT_SUBREG (DUPv2i64lane FPR128:$Rn, 1), dsub)>;

// A 64-bit subvector insert to the first 128-bit vector position
// is a subregister copy that needs no instruction.
multiclass InsertSubvectorUndef<ValueType Ty> {
  def : Pat<(insert_subvector undef, (v1i64 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v1f64 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v2f64 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v2i32 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v2f32 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v4i16 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v4f16 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v4bf16 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v8bf16 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v8i8 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
}

defm : InsertSubvectorUndef<i32>;
defm : InsertSubvectorUndef<i64>;

// Use pair-wise add instructions when summing up the lanes for v2f64, v2i64
// or v2f32.
def : Pat<(i64 (add (vector_extract (v2i64 FPR128:$Rn), (i64 0)),
                    (vector_extract (v2i64 FPR128:$Rn), (i64 1)))),
           (i64 (ADDPv2i64p (v2i64 FPR128:$Rn)))>;
def : Pat<(f64 (any_fadd (vector_extract (v2f64 FPR128:$Rn), (i64 0)),
                         (vector_extract (v2f64 FPR128:$Rn), (i64 1)))),
           (f64 (FADDPv2i64p (v2f64 FPR128:$Rn)))>;
    // vector_extract on 64-bit vectors gets promoted to a 128 bit vector,
    // so we match on v4f32 here, not v2f32. This will also catch adding
    // the low two lanes of a true v4f32 vector.
def : Pat<(any_fadd (vector_extract (v4f32 FPR128:$Rn), (i64 0)),
                    (vector_extract (v4f32 FPR128:$Rn), (i64 1))),
          (f32 (FADDPv2i32p (EXTRACT_SUBREG FPR128:$Rn, dsub)))>;
def : Pat<(any_fadd (vector_extract (v8f16 FPR128:$Rn), (i64 0)),
                    (vector_extract (v8f16 FPR128:$Rn), (i64 1))),
          (f16 (FADDPv2i16p (EXTRACT_SUBREG FPR128:$Rn, dsub)))>;

// Prefer using the bottom lanes of addp Rn, Rn compared to
// addp extractlow(Rn), extracthigh(Rn)
def : Pat<(AArch64addp (v2i32 (extract_subvector (v4i32 FPR128:$Rn), (i64 0))),
                       (v2i32 (extract_subvector (v4i32 FPR128:$Rn), (i64 2)))),
          (v2i32 (EXTRACT_SUBREG (ADDPv4i32 $Rn, $Rn), dsub))>;
def : Pat<(AArch64addp (v4i16 (extract_subvector (v8i16 FPR128:$Rn), (i64 0))),
                       (v4i16 (extract_subvector (v8i16 FPR128:$Rn), (i64 4)))),
          (v4i16 (EXTRACT_SUBREG (ADDPv8i16 $Rn, $Rn), dsub))>;
def : Pat<(AArch64addp (v8i8 (extract_subvector (v16i8 FPR128:$Rn), (i64 0))),
                       (v8i8 (extract_subvector (v16i8 FPR128:$Rn), (i64 8)))),
          (v8i8 (EXTRACT_SUBREG (ADDPv16i8 $Rn, $Rn), dsub))>;

def : Pat<(AArch64faddp (v2f32 (extract_subvector (v4f32 FPR128:$Rn), (i64 0))),
                        (v2f32 (extract_subvector (v4f32 FPR128:$Rn), (i64 2)))),
          (v2f32 (EXTRACT_SUBREG (FADDPv4f32 $Rn, $Rn), dsub))>;
def : Pat<(AArch64faddp (v4f16 (extract_subvector (v8f16 FPR128:$Rn), (i64 0))),
                        (v4f16 (extract_subvector (v8f16 FPR128:$Rn), (i64 4)))),
          (v4f16 (EXTRACT_SUBREG (FADDPv8f16 $Rn, $Rn), dsub))>;

// add(uzp1(X, Y), uzp2(X, Y)) -> addp(X, Y)
def : Pat<(v2i64 (add (AArch64zip1 (v2i64 FPR128:$Rn), (v2i64 FPR128:$Rm)),
                      (AArch64zip2 (v2i64 FPR128:$Rn), (v2i64 FPR128:$Rm)))),
          (v2i64 (ADDPv2i64 $Rn, $Rm))>;
def : Pat<(v4i32 (add (AArch64uzp1 (v4i32 FPR128:$Rn), (v4i32 FPR128:$Rm)),
                      (AArch64uzp2 (v4i32 FPR128:$Rn), (v4i32 FPR128:$Rm)))),
          (v4i32 (ADDPv4i32 $Rn, $Rm))>;
def : Pat<(v8i16 (add (AArch64uzp1 (v8i16 FPR128:$Rn), (v8i16 FPR128:$Rm)),
                      (AArch64uzp2 (v8i16 FPR128:$Rn), (v8i16 FPR128:$Rm)))),
          (v8i16 (ADDPv8i16 $Rn, $Rm))>;
def : Pat<(v16i8 (add (AArch64uzp1 (v16i8 FPR128:$Rn), (v16i8 FPR128:$Rm)),
                      (AArch64uzp2 (v16i8 FPR128:$Rn), (v16i8 FPR128:$Rm)))),
          (v16i8 (ADDPv16i8 $Rn, $Rm))>;

def : Pat<(v2f64 (fadd (AArch64zip1 (v2f64 FPR128:$Rn), (v2f64 FPR128:$Rm)),
                       (AArch64zip2 (v2f64 FPR128:$Rn), (v2f64 FPR128:$Rm)))),
          (v2f64 (FADDPv2f64 $Rn, $Rm))>;
def : Pat<(v4f32 (fadd (AArch64uzp1 (v4f32 FPR128:$Rn), (v4f32 FPR128:$Rm)),
                       (AArch64uzp2 (v4f32 FPR128:$Rn), (v4f32 FPR128:$Rm)))),
          (v4f32 (FADDPv4f32 $Rn, $Rm))>;
let Predicates = [HasFullFP16] in
def : Pat<(v8f16 (fadd (AArch64uzp1 (v8f16 FPR128:$Rn), (v8f16 FPR128:$Rm)),
                       (AArch64uzp2 (v8f16 FPR128:$Rn), (v8f16 FPR128:$Rm)))),
          (v8f16 (FADDPv8f16 $Rn, $Rm))>;

// Scalar 64-bit shifts in FPR64 registers.
def : Pat<(i64 (int_aarch64_neon_sshl (i64 FPR64:$Rn), (i64 FPR64:$Rm))),
          (SSHLv1i64 FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(i64 (int_aarch64_neon_ushl (i64 FPR64:$Rn), (i64 FPR64:$Rm))),
          (USHLv1i64 FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(i64 (int_aarch64_neon_srshl (i64 FPR64:$Rn), (i64 FPR64:$Rm))),
          (SRSHLv1i64 FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(i64 (int_aarch64_neon_urshl (i64 FPR64:$Rn), (i64 FPR64:$Rm))),
          (URSHLv1i64 FPR64:$Rn, FPR64:$Rm)>;

// Patterns for nontemporal/no-allocate stores.
// We have to resort to tricks to turn a single-input store into a store pair,
// because there is no single-input nontemporal store, only STNP.
let Predicates = [IsLE] in {
let AddedComplexity = 15 in {
class NTStore128Pat<ValueType VT> :
  Pat<(nontemporalstore (VT FPR128:$Rt),
        (am_indexed7s64 GPR64sp:$Rn, simm7s8:$offset)),
      (STNPDi (EXTRACT_SUBREG FPR128:$Rt, dsub),
              (DUPi64 FPR128:$Rt, (i64 1)),
              GPR64sp:$Rn, simm7s8:$offset)>;

def : NTStore128Pat<v2i64>;
def : NTStore128Pat<v4i32>;
def : NTStore128Pat<v8i16>;
def : NTStore128Pat<v16i8>;

class NTStore64Pat<ValueType VT> :
  Pat<(nontemporalstore (VT FPR64:$Rt),
        (am_indexed7s32 GPR64sp:$Rn, simm7s4:$offset)),
      (STNPSi (EXTRACT_SUBREG FPR64:$Rt, ssub),
              (DUPi32 (SUBREG_TO_REG (i64 0), FPR64:$Rt, dsub), (i64 1)),
              GPR64sp:$Rn, simm7s4:$offset)>;

// FIXME: Shouldn't v1f64 loads/stores be promoted to v1i64?
def : NTStore64Pat<v1f64>;
def : NTStore64Pat<v1i64>;
def : NTStore64Pat<v2i32>;
def : NTStore64Pat<v4i16>;
def : NTStore64Pat<v8i8>;

def : Pat<(nontemporalstore GPR64:$Rt,
            (am_indexed7s32 GPR64sp:$Rn, simm7s4:$offset)),
          (STNPWi (EXTRACT_SUBREG GPR64:$Rt, sub_32),
                  (EXTRACT_SUBREG (UBFMXri GPR64:$Rt, 32, 63), sub_32),
                  GPR64sp:$Rn, simm7s4:$offset)>;
} // AddedComplexity=10
} // Predicates = [IsLE]

// Tail call return handling. These are all compiler pseudo-instructions,
// so no encoding information or anything like that.
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [SP] in {
  def TCRETURNdi : Pseudo<(outs), (ins i64imm:$dst, i32imm:$FPDiff), []>,
                   Sched<[WriteBrReg]>;
  def TCRETURNri : Pseudo<(outs), (ins tcGPR64:$dst, i32imm:$FPDiff), []>,
                   Sched<[WriteBrReg]>;
  // Indirect tail-call with any register allowed, used by MachineOutliner when
  // this is proven safe.
  // FIXME: If we have to add any more hacks like this, we should instead relax
  // some verifier checks for outlined functions.
  def TCRETURNriALL : Pseudo<(outs), (ins GPR64:$dst, i32imm:$FPDiff), []>,
                      Sched<[WriteBrReg]>;

  // Indirect tail-calls with reduced register classes, needed for BTI and
  // PAuthLR.
  def TCRETURNrix16x17 : Pseudo<(outs), (ins tcGPRx16x17:$dst, i32imm:$FPDiff), []>,
                      Sched<[WriteBrReg]>;
  def TCRETURNrix17 : Pseudo<(outs), (ins tcGPRx17:$dst, i32imm:$FPDiff), []>,
                      Sched<[WriteBrReg]>;
  def TCRETURNrinotx16 : Pseudo<(outs), (ins tcGPRnotx16:$dst, i32imm:$FPDiff), []>,
                      Sched<[WriteBrReg]>;
}

def : Pat<(AArch64tcret tcGPR64:$dst, (i32 timm:$FPDiff)),
          (TCRETURNri tcGPR64:$dst, imm:$FPDiff)>,
      Requires<[TailCallAny]>;
def : Pat<(AArch64tcret tcGPRx16x17:$dst, (i32 timm:$FPDiff)),
          (TCRETURNrix16x17 tcGPRx16x17:$dst, imm:$FPDiff)>,
      Requires<[TailCallX16X17]>;
def : Pat<(AArch64tcret tcGPRx17:$dst, (i32 timm:$FPDiff)),
          (TCRETURNrix17 tcGPRx17:$dst, imm:$FPDiff)>,
      Requires<[TailCallX17]>;
def : Pat<(AArch64tcret tcGPRnotx16:$dst, (i32 timm:$FPDiff)),
          (TCRETURNrinotx16 tcGPRnotx16:$dst, imm:$FPDiff)>,
      Requires<[TailCallNotX16]>;

def : Pat<(AArch64tcret tglobaladdr:$dst, (i32 timm:$FPDiff)),
          (TCRETURNdi texternalsym:$dst, imm:$FPDiff)>;
def : Pat<(AArch64tcret texternalsym:$dst, (i32 timm:$FPDiff)),
          (TCRETURNdi texternalsym:$dst, imm:$FPDiff)>;

def MOVMCSym : Pseudo<(outs GPR64:$dst), (ins i64imm:$sym), []>, Sched<[]>;
def : Pat<(i64 (AArch64LocalRecover mcsym:$sym)), (MOVMCSym mcsym:$sym)>;

// Extracting lane zero is a special case where we can just use a plain
// EXTRACT_SUBREG instruction, which will become FMOV. This is easier for the
// rest of the compiler, especially the register allocator and copy propagation,
// to reason about, so is preferred when it's possible to use it.
let AddedComplexity = 10 in {
  def : Pat<(i64 (extractelt (v2i64 V128:$V), (i64 0))), (EXTRACT_SUBREG V128:$V, dsub)>;
  def : Pat<(i32 (extractelt (v4i32 V128:$V), (i64 0))), (EXTRACT_SUBREG V128:$V, ssub)>;
  def : Pat<(i32 (extractelt (v2i32 V64:$V), (i64 0))), (EXTRACT_SUBREG V64:$V, ssub)>;
}

// dot_v4i8
class mul_v4i8<SDPatternOperator ldop> :
  PatFrag<(ops node:$Rn, node:$Rm, node:$offset),
          (mul (ldop (add node:$Rn, node:$offset)),
               (ldop (add node:$Rm, node:$offset)))>;
class mulz_v4i8<SDPatternOperator ldop> :
  PatFrag<(ops node:$Rn, node:$Rm),
          (mul (ldop node:$Rn), (ldop node:$Rm))>;

def load_v4i8 :
  OutPatFrag<(ops node:$R),
             (INSERT_SUBREG
              (v2i32 (IMPLICIT_DEF)),
               (i32 (COPY_TO_REGCLASS (LDRWui node:$R, (i64 0)), FPR32)),
              ssub)>;

class dot_v4i8<Instruction DOT, SDPatternOperator ldop> :
  Pat<(i32 (add (mul_v4i8<ldop> GPR64sp:$Rn, GPR64sp:$Rm, (i64 3)),
           (add (mul_v4i8<ldop> GPR64sp:$Rn, GPR64sp:$Rm, (i64 2)),
           (add (mul_v4i8<ldop> GPR64sp:$Rn, GPR64sp:$Rm, (i64 1)),
                (mulz_v4i8<ldop> GPR64sp:$Rn, GPR64sp:$Rm))))),
      (EXTRACT_SUBREG (i64 (DOT (DUPv2i32gpr WZR),
                                (load_v4i8 GPR64sp:$Rn),
                                (load_v4i8 GPR64sp:$Rm))),
                      sub_32)>, Requires<[HasDotProd]>;

// dot_v8i8
class ee_v8i8<SDPatternOperator extend> :
  PatFrag<(ops node:$V, node:$K),
          (v4i16 (extract_subvector (v8i16 (extend node:$V)), node:$K))>;

class mul_v8i8<SDPatternOperator mulop, SDPatternOperator extend> :
  PatFrag<(ops node:$M, node:$N, node:$K),
          (mulop (v4i16 (ee_v8i8<extend> node:$M, node:$K)),
                 (v4i16 (ee_v8i8<extend> node:$N, node:$K)))>;

class idot_v8i8<SDPatternOperator mulop, SDPatternOperator extend> :
  PatFrag<(ops node:$M, node:$N),
          (i32 (extractelt
           (v4i32 (AArch64uaddv
            (add (mul_v8i8<mulop, extend> node:$M, node:$N, (i64 0)),
                 (mul_v8i8<mulop, extend> node:$M, node:$N, (i64 4))))),
           (i64 0)))>;

// vaddv_[su]32 is special; -> ADDP Vd.2S,Vn.2S,Vm.2S; return Vd.s[0];Vn==Vm
def VADDV_32 : OutPatFrag<(ops node:$R), (ADDPv2i32 node:$R, node:$R)>;

class odot_v8i8<Instruction DOT> :
  OutPatFrag<(ops node:$Vm, node:$Vn),
             (EXTRACT_SUBREG
              (VADDV_32
               (i64 (DOT (DUPv2i32gpr WZR),
                         (v8i8 node:$Vm),
                         (v8i8 node:$Vn)))),
              sub_32)>;

class dot_v8i8<Instruction DOT, SDPatternOperator mulop,
                    SDPatternOperator extend> :
  Pat<(idot_v8i8<mulop, extend> V64:$Vm, V64:$Vn),
      (odot_v8i8<DOT> V64:$Vm, V64:$Vn)>,
  Requires<[HasDotProd]>;

// dot_v16i8
class ee_v16i8<SDPatternOperator extend> :
  PatFrag<(ops node:$V, node:$K1, node:$K2),
          (v4i16 (extract_subvector
           (v8i16 (extend
            (v8i8 (extract_subvector node:$V, node:$K1)))), node:$K2))>;

class mul_v16i8<SDPatternOperator mulop, SDPatternOperator extend> :
  PatFrag<(ops node:$M, node:$N, node:$K1, node:$K2),
          (v4i32
           (mulop (v4i16 (ee_v16i8<extend> node:$M, node:$K1, node:$K2)),
                  (v4i16 (ee_v16i8<extend> node:$N, node:$K1, node:$K2))))>;

class idot_v16i8<SDPatternOperator m, SDPatternOperator x> :
  PatFrag<(ops node:$M, node:$N),
          (i32 (extractelt
           (v4i32 (AArch64uaddv
            (add
             (add (mul_v16i8<m, x> node:$M, node:$N, (i64 0), (i64 0)),
                  (mul_v16i8<m, x> node:$M, node:$N, (i64 8), (i64 0))),
             (add (mul_v16i8<m, x> node:$M, node:$N, (i64 0), (i64 4)),
                  (mul_v16i8<m, x> node:$M, node:$N, (i64 8), (i64 4)))))),
           (i64 0)))>;

class odot_v16i8<Instruction DOT> :
  OutPatFrag<(ops node:$Vm, node:$Vn),
             (i32 (ADDVv4i32v
              (DOT (DUPv4i32gpr WZR), node:$Vm, node:$Vn)))>;

class dot_v16i8<Instruction DOT, SDPatternOperator mulop,
                SDPatternOperator extend> :
  Pat<(idot_v16i8<mulop, extend> V128:$Vm, V128:$Vn),
      (odot_v16i8<DOT> V128:$Vm, V128:$Vn)>,
  Requires<[HasDotProd]>;

let AddedComplexity = 10 in {
  def : dot_v4i8<SDOTv8i8, sextloadi8>;
  def : dot_v4i8<UDOTv8i8, zextloadi8>;
  def : dot_v8i8<SDOTv8i8, AArch64smull, sext>;
  def : dot_v8i8<UDOTv8i8, AArch64umull, zext>;
  def : dot_v16i8<SDOTv16i8, AArch64smull, sext>;
  def : dot_v16i8<UDOTv16i8, AArch64umull, zext>;

  // FIXME: add patterns to generate vector by element dot product.
  // FIXME: add SVE dot-product patterns.
}

// Custom DAG nodes and isel rules to make a 64-byte block out of eight GPRs,
// so that it can be used as input to inline asm, and vice versa.
def LS64_BUILD : SDNode<"AArch64ISD::LS64_BUILD", SDTypeProfile<1, 8, []>>;
def LS64_EXTRACT : SDNode<"AArch64ISD::LS64_EXTRACT", SDTypeProfile<1, 2, []>>;
def : Pat<(i64x8 (LS64_BUILD GPR64:$x0, GPR64:$x1, GPR64:$x2, GPR64:$x3,
                             GPR64:$x4, GPR64:$x5, GPR64:$x6, GPR64:$x7)),
          (REG_SEQUENCE GPR64x8Class,
              $x0, x8sub_0, $x1, x8sub_1, $x2, x8sub_2, $x3, x8sub_3,
              $x4, x8sub_4, $x5, x8sub_5, $x6, x8sub_6, $x7, x8sub_7)>;
foreach i = 0-7 in {
  def : Pat<(i64 (LS64_EXTRACT (i64x8 GPR64x8:$val), (i32 i))),
            (EXTRACT_SUBREG $val, !cast<SubRegIndex>("x8sub_"#i))>;
}

let Predicates = [HasLS64] in {
  def LD64B: LoadStore64B<0b101, "ld64b", (ins GPR64sp:$Rn),
                                          (outs GPR64x8:$Rt)>;
  def ST64B: LoadStore64B<0b001, "st64b", (ins GPR64x8:$Rt, GPR64sp:$Rn),
                                          (outs)>;
  def ST64BV:   Store64BV<0b011, "st64bv">;
  def ST64BV0:  Store64BV<0b010, "st64bv0">;

  class ST64BPattern<Intrinsic intrinsic, Instruction instruction>
    : Pat<(intrinsic GPR64sp:$addr, GPR64:$x0, GPR64:$x1, GPR64:$x2, GPR64:$x3, GPR64:$x4, GPR64:$x5, GPR64:$x6, GPR64:$x7),
          (instruction (REG_SEQUENCE GPR64x8Class, $x0, x8sub_0, $x1, x8sub_1, $x2, x8sub_2, $x3, x8sub_3, $x4, x8sub_4, $x5, x8sub_5, $x6, x8sub_6, $x7, x8sub_7), $addr)>;

  def : ST64BPattern<int_aarch64_st64b, ST64B>;
  def : ST64BPattern<int_aarch64_st64bv, ST64BV>;
  def : ST64BPattern<int_aarch64_st64bv0, ST64BV0>;
}

let Predicates = [HasMOPS] in {
  let Defs = [NZCV] in {
    defm CPYFP : MOPSMemoryCopyInsns<0b00, "cpyfp">;

    defm CPYP : MOPSMemoryMoveInsns<0b00, "cpyp">;

    defm SETP : MOPSMemorySetInsns<0b00, "setp">;
  }
  let Uses = [NZCV] in {
    defm CPYFM : MOPSMemoryCopyInsns<0b01, "cpyfm">;
    defm CPYFE : MOPSMemoryCopyInsns<0b10, "cpyfe">;

    defm CPYM : MOPSMemoryMoveInsns<0b01, "cpym">;
    defm CPYE : MOPSMemoryMoveInsns<0b10, "cpye">;

    defm SETM : MOPSMemorySetInsns<0b01, "setm">;
    defm SETE : MOPSMemorySetInsns<0b10, "sete">;
  }
}
let Predicates = [HasMOPS, HasMTE] in {
  let Defs = [NZCV] in {
    defm SETGP     : MOPSMemorySetTaggingInsns<0b00, "setgp">;
  }
  let Uses = [NZCV] in {
    defm SETGM     : MOPSMemorySetTaggingInsns<0b01, "setgm">;
    // Can't use SETGE because it's a reserved name in TargetSelectionDAG.td
    defm MOPSSETGE : MOPSMemorySetTaggingInsns<0b10, "setge">;
  }
}

// MOPS Node operands: 0: Dst, 1: Src or Value, 2: Size, 3: Chain
// MOPS Node results: 0: Dst writeback, 1: Size writeback, 2: Chain
def SDT_AArch64mops : SDTypeProfile<2, 3, [ SDTCisInt<0>, SDTCisInt<1>, SDTCisInt<2> ]>;
def AArch64mops_memset : SDNode<"AArch64ISD::MOPS_MEMSET", SDT_AArch64mops>;
def AArch64mops_memset_tagging : SDNode<"AArch64ISD::MOPS_MEMSET_TAGGING", SDT_AArch64mops>;
def AArch64mops_memcopy : SDNode<"AArch64ISD::MOPS_MEMCOPY", SDT_AArch64mops>;
def AArch64mops_memmove : SDNode<"AArch64ISD::MOPS_MEMMOVE", SDT_AArch64mops>;

// MOPS operations always contain three 4-byte instructions
let Predicates = [HasMOPS], Defs = [NZCV], Size = 12, mayStore = 1 in {
  let mayLoad = 1 in {
    def MOPSMemoryCopyPseudo : Pseudo<(outs GPR64common:$Rd_wb, GPR64common:$Rs_wb, GPR64:$Rn_wb),
                                      (ins GPR64common:$Rd, GPR64common:$Rs, GPR64:$Rn),
                                      [], "$Rd = $Rd_wb,$Rs = $Rs_wb,$Rn = $Rn_wb">, Sched<[]>;
    def MOPSMemoryMovePseudo : Pseudo<(outs GPR64common:$Rd_wb, GPR64common:$Rs_wb, GPR64:$Rn_wb),
                                      (ins GPR64common:$Rd, GPR64common:$Rs, GPR64:$Rn),
                                      [], "$Rd = $Rd_wb,$Rs = $Rs_wb,$Rn = $Rn_wb">, Sched<[]>;
  }
  let mayLoad = 0 in {
    def MOPSMemorySetPseudo  : Pseudo<(outs GPR64common:$Rd_wb, GPR64:$Rn_wb),
                                      (ins GPR64common:$Rd, GPR64:$Rn, GPR64:$Rm),
                                      [], "$Rd = $Rd_wb,$Rn = $Rn_wb,@earlyclobber $Rn_wb">, Sched<[]>;
  }
}
let Predicates = [HasMOPS, HasMTE], Defs = [NZCV], Size = 12, mayLoad = 0, mayStore = 1 in {
  def MOPSMemorySetTaggingPseudo : Pseudo<(outs GPR64common:$Rd_wb, GPR64:$Rn_wb),
                                          (ins GPR64common:$Rd, GPR64:$Rn, GPR64:$Rm),
                                          [], "$Rd = $Rd_wb,$Rn = $Rn_wb">, Sched<[]>;
}

//-----------------------------------------------------------------------------
// v8.3 Pointer Authentication late patterns

def : Pat<(int_ptrauth_blend GPR64:$Rd, imm64_0_65535:$imm),
          (PAUTH_BLEND GPR64:$Rd, (trunc_imm imm64_0_65535:$imm))>;
def : Pat<(int_ptrauth_blend GPR64:$Rd, GPR64:$Rn),
          (BFMXri GPR64:$Rd, GPR64:$Rn, 16, 15)>;

//-----------------------------------------------------------------------------

// This gets lowered into an instruction sequence of 20 bytes
let Defs = [X16, X17], mayStore = 1, isCodeGenOnly = 1, Size = 20 in
def StoreSwiftAsyncContext
      : Pseudo<(outs), (ins GPR64:$ctx, GPR64sp:$base, simm9:$offset),
               []>, Sched<[]>;

def AArch64AssertZExtBool : SDNode<"AArch64ISD::ASSERT_ZEXT_BOOL", SDT_assert>;
def : Pat<(AArch64AssertZExtBool GPR32:$op),
          (i32 GPR32:$op)>;

//===----------------------------===//
// 2022 Architecture Extensions:
//===----------------------------===//

def : InstAlias<"clrbhb",  (HINT 22), 0>;
let Predicates = [HasCLRBHB] in {
  def : InstAlias<"clrbhb",  (HINT 22), 1>;
}

//===----------------------------------------------------------------------===//
// Translation Hardening Extension (FEAT_THE)
//===----------------------------------------------------------------------===//
defm RCW     : ReadCheckWriteCompareAndSwap;

defm RCWCLR  : ReadCheckWriteOperation<0b001, "clr">;
defm RCWSET  : ReadCheckWriteOperation<0b011, "set">;
defm RCWSWP  : ReadCheckWriteOperation<0b010, "swp">;

//===----------------------------------------------------------------------===//
// General Data-Processing Instructions (FEAT_V94_DP)
//===----------------------------------------------------------------------===//
defm ABS : OneOperandData<0b001000, "abs", abs>, Requires<[HasCSSC]>;
defm CNT : OneOperandData<0b000111, "cnt", ctpop>, Requires<[HasCSSC]>;
defm CTZ : OneOperandData<0b000110, "ctz", cttz>, Requires<[HasCSSC]>;

defm SMAX : ComparisonOp<0, 0, "smax", smax>, Requires<[HasCSSC]>;
defm SMIN : ComparisonOp<0, 1, "smin", smin>, Requires<[HasCSSC]>;
defm UMAX : ComparisonOp<1, 0, "umax", umax>, Requires<[HasCSSC]>;
defm UMIN : ComparisonOp<1, 1, "umin", umin>, Requires<[HasCSSC]>;

def RPRFM:
    I<(outs), (ins rprfop:$Rt, GPR64:$Rm, GPR64sp:$Rn),
      "rprfm", "\t$Rt, $Rm, [$Rn]", "", []>,
    Sched<[]> {
  bits<6> Rt;
  bits<5> Rn;
  bits<5> Rm;
  let Inst{2-0} = Rt{2-0};
  let Inst{4-3} = 0b11;
  let Inst{9-5} = Rn;
  let Inst{11-10} = 0b10;
  let Inst{13-12} = Rt{4-3};
  let Inst{14} = 0b1;
  let Inst{15} = Rt{5};
  let Inst{20-16} = Rm;
  let Inst{31-21} = 0b11111000101;
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 1;
  // RPRFM overlaps with PRFM (reg), when the decoder method of PRFM returns
  // Fail, the decoder should attempt to decode RPRFM. This requires setting
  // the decoder namespace to "Fallback".
  let DecoderNamespace = "Fallback";
}

//===----------------------------------------------------------------------===//
// 128-bit Atomics (FEAT_LSE128)
//===----------------------------------------------------------------------===//
let Predicates = [HasLSE128] in {
  def SWPP     : LSE128Base<0b000, 0b00, 0b1, "swpp">;
  def SWPPA    : LSE128Base<0b000, 0b10, 0b1, "swppa">;
  def SWPPAL   : LSE128Base<0b000, 0b11, 0b1, "swppal">;
  def SWPPL    : LSE128Base<0b000, 0b01, 0b1, "swppl">;
  def LDCLRP   : LSE128Base<0b001, 0b00, 0b0, "ldclrp">;
  def LDCLRPA  : LSE128Base<0b001, 0b10, 0b0, "ldclrpa">;
  def LDCLRPAL : LSE128Base<0b001, 0b11, 0b0, "ldclrpal">;
  def LDCLRPL  : LSE128Base<0b001, 0b01, 0b0, "ldclrpl">;
  def LDSETP   : LSE128Base<0b011, 0b00, 0b0, "ldsetp">;
  def LDSETPA  : LSE128Base<0b011, 0b10, 0b0, "ldsetpa">;
  def LDSETPAL : LSE128Base<0b011, 0b11, 0b0, "ldsetpal">;
  def LDSETPL  : LSE128Base<0b011, 0b01, 0b0, "ldsetpl">;
}

//===----------------------------------------------------------------------===//
// RCPC Instructions (FEAT_LRCPC3)
//===----------------------------------------------------------------------===//

let Predicates = [HasRCPC3] in {
  //                                              size   opc    opc2
  def STILPWpre:   BaseLRCPC3IntegerLoadStorePair<0b10, 0b00, 0b0000, (outs GPR64sp:$wback), (ins GPR32:$Rt, GPR32:$Rt2, GPR64sp:$Rn), "stilp", "\t$Rt, $Rt2, [$Rn, #-8]!", "$Rn = $wback">;
  def STILPXpre:   BaseLRCPC3IntegerLoadStorePair<0b11, 0b00, 0b0000, (outs GPR64sp:$wback), (ins GPR64:$Rt, GPR64:$Rt2, GPR64sp:$Rn), "stilp", "\t$Rt, $Rt2, [$Rn, #-16]!", "$Rn = $wback">;
  def STILPW:      BaseLRCPC3IntegerLoadStorePair<0b10, 0b00, 0b0001, (outs), (ins GPR32:$Rt, GPR32:$Rt2, GPR64sp:$Rn), "stilp", "\t$Rt, $Rt2, [$Rn]", "">;
  def STILPX:      BaseLRCPC3IntegerLoadStorePair<0b11, 0b00, 0b0001, (outs), (ins GPR64:$Rt, GPR64:$Rt2, GPR64sp:$Rn), "stilp", "\t$Rt, $Rt2, [$Rn]", "">;
  def LDIAPPWpost: BaseLRCPC3IntegerLoadStorePair<0b10, 0b01, 0b0000, (outs GPR64sp:$wback, GPR32:$Rt, GPR32:$Rt2), (ins GPR64sp:$Rn), "ldiapp", "\t$Rt, $Rt2, [$Rn], #8", "$Rn = $wback">;
  def LDIAPPXpost: BaseLRCPC3IntegerLoadStorePair<0b11, 0b01, 0b0000, (outs GPR64sp:$wback, GPR64:$Rt, GPR64:$Rt2), (ins GPR64sp:$Rn), "ldiapp", "\t$Rt, $Rt2, [$Rn], #16", "$Rn = $wback">;
  def LDIAPPW:     BaseLRCPC3IntegerLoadStorePair<0b10, 0b01, 0b0001, (outs GPR32:$Rt, GPR32:$Rt2), (ins GPR64sp0:$Rn), "ldiapp", "\t$Rt, $Rt2, [$Rn]", "">;
  def LDIAPPX:     BaseLRCPC3IntegerLoadStorePair<0b11, 0b01, 0b0001, (outs GPR64:$Rt, GPR64:$Rt2), (ins GPR64sp0:$Rn), "ldiapp", "\t$Rt, $Rt2, [$Rn]", "">;

  def : Pat<(AArch64ldiapp GPR64sp:$Rn), (LDIAPPX GPR64sp:$Rn)>;
  def : Pat<(AArch64stilp GPR64:$Rt, GPR64:$Rt2, GPR64sp:$Rn), (STILPX GPR64:$Rt, GPR64:$Rt2, GPR64sp:$Rn)>;

  // Aliases for when offset=0
  def : InstAlias<"stilp\t$Rt, $Rt2, [$Rn, #0]", (STILPW GPR32: $Rt, GPR32: $Rt2, GPR64sp:$Rn)>;
  def : InstAlias<"stilp\t$Rt, $Rt2, [$Rn, #0]", (STILPX GPR64: $Rt, GPR64: $Rt2, GPR64sp:$Rn)>;

  //                                         size   opc
  def STLRWpre:   BaseLRCPC3IntegerLoadStore<0b10, 0b10, (outs GPR64sp:$wback),            (ins GPR32:$Rt, GPR64sp:$Rn), "stlr",  "\t$Rt, [$Rn, #-4]!", "$Rn = $wback">;
  def STLRXpre:   BaseLRCPC3IntegerLoadStore<0b11, 0b10, (outs GPR64sp:$wback),            (ins GPR64:$Rt, GPR64sp:$Rn), "stlr",  "\t$Rt, [$Rn, #-8]!", "$Rn = $wback">;
  def LDAPRWpost: BaseLRCPC3IntegerLoadStore<0b10, 0b11, (outs GPR64sp:$wback, GPR32:$Rt), (ins GPR64sp:$Rn),            "ldapr", "\t$Rt, [$Rn], #4",   "$Rn = $wback">;
  def LDAPRXpost: BaseLRCPC3IntegerLoadStore<0b11, 0b11, (outs GPR64sp:$wback, GPR64:$Rt), (ins GPR64sp:$Rn),            "ldapr", "\t$Rt, [$Rn], #8",   "$Rn = $wback">;
}

let Predicates = [HasRCPC3, HasNEON] in {
  //                                              size   opc regtype
  defm STLURb:  LRCPC3NEONLoadStoreUnscaledOffset<0b00, 0b00, FPR8  , (outs), (ins FPR8  :$Rt, GPR64sp:$Rn, simm9:$simm), "stlur">;
  defm STLURh:  LRCPC3NEONLoadStoreUnscaledOffset<0b01, 0b00, FPR16 , (outs), (ins FPR16 :$Rt, GPR64sp:$Rn, simm9:$simm), "stlur">;
  defm STLURs:  LRCPC3NEONLoadStoreUnscaledOffset<0b10, 0b00, FPR32 , (outs), (ins FPR32 :$Rt, GPR64sp:$Rn, simm9:$simm), "stlur">;
  defm STLURd:  LRCPC3NEONLoadStoreUnscaledOffset<0b11, 0b00, FPR64 , (outs), (ins FPR64 :$Rt, GPR64sp:$Rn, simm9:$simm), "stlur">;
  defm STLURq:  LRCPC3NEONLoadStoreUnscaledOffset<0b00, 0b10, FPR128, (outs), (ins FPR128:$Rt, GPR64sp:$Rn, simm9:$simm), "stlur">;
  defm LDAPURb: LRCPC3NEONLoadStoreUnscaledOffset<0b00, 0b01, FPR8  , (outs FPR8  :$Rt), (ins GPR64sp:$Rn, simm9:$simm), "ldapur">;
  defm LDAPURh: LRCPC3NEONLoadStoreUnscaledOffset<0b01, 0b01, FPR16 , (outs FPR16 :$Rt), (ins GPR64sp:$Rn, simm9:$simm), "ldapur">;
  defm LDAPURs: LRCPC3NEONLoadStoreUnscaledOffset<0b10, 0b01, FPR32 , (outs FPR32 :$Rt), (ins GPR64sp:$Rn, simm9:$simm), "ldapur">;
  defm LDAPURd: LRCPC3NEONLoadStoreUnscaledOffset<0b11, 0b01, FPR64 , (outs FPR64 :$Rt), (ins GPR64sp:$Rn, simm9:$simm), "ldapur">;
  defm LDAPURq: LRCPC3NEONLoadStoreUnscaledOffset<0b00, 0b11, FPR128, (outs FPR128:$Rt), (ins GPR64sp:$Rn, simm9:$simm), "ldapur">;

  //                                L
  def STL1:  LRCPC3NEONLdStSingle<0b0, (outs), (ins VecListOned:$Vt, VectorIndexD:$Q, GPR64sp:$Rn) , "stl1", "">;
  def LDAP1: LRCPC3NEONLdStSingle<0b1, (outs VecListOned:$dst), (ins VecListOned:$Vt, VectorIndexD:$Q, GPR64sp0:$Rn), "ldap1", "$Vt = $dst">;

  // Aliases for when offset=0
  def : InstAlias<"stl1\t$Vt$Q, [$Rn, #0]", (STL1 VecListOned:$Vt, VectorIndexD:$Q, GPR64sp:$Rn)>;
}

//===----------------------------------------------------------------------===//
// 128-bit System Instructions (FEAT_SYSINSTR128)
//===----------------------------------------------------------------------===//
let Predicates = [HasD128] in {
  def SYSPxt  : SystemPXtI<0, "sysp">;

  def SYSPxt_XZR
    : BaseSystemI<0, (outs),
        (ins imm0_7:$op1, sys_cr_op:$Cn, sys_cr_op:$Cm, imm0_7:$op2, SyspXzrPairOperand:$xzr_pair),
        "sysp", "\t$op1, $Cn, $Cm, $op2, $xzr_pair">,
      Sched<[WriteSys]>
  {
    // Had to use a custom decoder because tablegen interprets this as having 4 fields (why?)
    // and therefore autogenerates a decoder that builds an MC representation that has 4 fields
    // (decodeToMCInst), but when printing we expect the MC representation to have 5 fields (one
    // extra for the XZR) because AArch64InstPrinter::printInstruction in AArch64GenAsmWriter.inc
    // is based off of the asm template (maybe) and therefore wants to print 5 operands.
    // I could add a bits<5> xzr_pair. But without a way to constrain it to 0b11111 here it would
    // overlap with the main SYSP instruction.
    let DecoderMethod = "DecodeSyspXzrInstruction";
    bits<3> op1;
    bits<4> Cn;
    bits<4> Cm;
    bits<3> op2;
    let Inst{22}    = 0b1; // override BaseSystemI
    let Inst{20-19} = 0b01;
    let Inst{18-16} = op1;
    let Inst{15-12} = Cn;
    let Inst{11-8}  = Cm;
    let Inst{7-5}   = op2;
    let Inst{4-0}   = 0b11111;
  }

  def : InstAlias<"sysp $op1, $Cn, $Cm, $op2",
                  (SYSPxt_XZR imm0_7:$op1, sys_cr_op:$Cn, sys_cr_op:$Cm, imm0_7:$op2, XZR)>;
}

//---
// 128-bit System Registers (FEAT_SYSREG128)
//---

// Instruction encoding:
//
//          31       22|21|20|19|18 16|15 12|11 8|7 5|4 0
// MRRS      1101010101| 1| 1|o0|  op1|   Cn|  Cm|op2| Rt
// MSRR      1101010101| 0| 1|o0|  op1|   Cn|  Cm|op2| Rt

// Instruction syntax:
//
// MRRS <Xt>, <Xt+1>, <sysreg|S<op0>_<op1>_<Cn>_<Cm>_<op2>>
// MSRR <sysreg|S<op0>_<op1>_<Cn>_<Cm>_<op2>>, <Xt>, <Xt+1>
//
// ...where t is even (X0, X2, etc).

let Predicates = [HasD128] in {
  def MRRS : RtSystemI128<1,
    (outs MrrsMssrPairClassOperand:$Rt), (ins mrs_sysreg_op:$systemreg),
    "mrrs", "\t$Rt, $systemreg">
  {
    bits<16> systemreg;
    let Inst{20-5} = systemreg;
  }

  def MSRR : RtSystemI128<0,
    (outs),  (ins msr_sysreg_op:$systemreg, MrrsMssrPairClassOperand:$Rt),
    "msrr", "\t$systemreg, $Rt">
  {
    bits<16> systemreg;
    let Inst{20-5} = systemreg;
  }
}

//===----------------------------===//
// 2023 Architecture Extensions:
//===----------------------------===//

let Predicates = [HasFP8] in {
  defm F1CVTL  : SIMDMixedTwoVectorFP8<0b00, "f1cvtl">;
  defm F2CVTL  : SIMDMixedTwoVectorFP8<0b01, "f2cvtl">;
  defm BF1CVTL : SIMDMixedTwoVectorFP8<0b10, "bf1cvtl">;
  defm BF2CVTL : SIMDMixedTwoVectorFP8<0b11, "bf2cvtl">;
  defm FCVTN_F16_F8 : SIMDThreeSameSizeVectorCvt<"fcvtn">;
  defm FCVTN_F32_F8 : SIMDThreeVectorCvt<"fcvtn">;
  defm FSCALE : SIMDThreeSameVectorFP<0b1, 0b1, 0b111, "fscale", null_frag>;
} // End let Predicates = [HasFP8]

let Predicates = [HasFAMINMAX] in {
 defm FAMAX : SIMDThreeSameVectorFP<0b0, 0b1, 0b011, "famax", null_frag>;
 defm FAMIN : SIMDThreeSameVectorFP<0b1, 0b1, 0b011, "famin", null_frag>;
} // End let Predicates = [HasFAMAXMIN]

let Predicates = [HasFP8FMA] in {
 defm FMLALBlane : SIMDThreeSameVectorMLAIndex<0b0, "fmlalb">;
 defm FMLALTlane : SIMDThreeSameVectorMLAIndex<0b1, "fmlalt">;
 defm FMLALLBBlane : SIMDThreeSameVectorMLALIndex<0b0, 0b00, "fmlallbb">;
 defm FMLALLBTlane : SIMDThreeSameVectorMLALIndex<0b0, 0b01, "fmlallbt">;
 defm FMLALLTBlane : SIMDThreeSameVectorMLALIndex<0b1, 0b00, "fmlalltb">;
 defm FMLALLTTlane : SIMDThreeSameVectorMLALIndex<0b1, 0b01, "fmlalltt">;

 defm FMLALB : SIMDThreeSameVectorMLA<0b0, "fmlalb">;
 defm FMLALT : SIMDThreeSameVectorMLA<0b1, "fmlalt">;
 defm FMLALLBB : SIMDThreeSameVectorMLAL<0b0, 0b00, "fmlallbb">;
 defm FMLALLBT : SIMDThreeSameVectorMLAL<0b0, 0b01, "fmlallbt">;
 defm FMLALLTB : SIMDThreeSameVectorMLAL<0b1, 0b00, "fmlalltb">;
 defm FMLALLTT : SIMDThreeSameVectorMLAL<0b1, 0b01, "fmlalltt">;
} // End let Predicates = [HasFP8FMA]

let Predicates = [HasFP8DOT2] in {
 defm FDOTlane : SIMDThreeSameVectorFP8DOT2Index<"fdot">;
 defm FDOT : SIMDThreeSameVectorDOT2<"fdot">;
} // End let Predicates = [HasFP8DOT2]

let Predicates = [HasFP8DOT4] in {
 defm FDOTlane : SIMDThreeSameVectorFP8DOT4Index<"fdot">;
 defm FDOT : SIMDThreeSameVectorDOT4<"fdot">;
} // End let Predicates = [HasFP8DOT4]

//===----------------------------------------------------------------------===//
// Checked Pointer Arithmetic (FEAT_CPA)
//===----------------------------------------------------------------------===//
let Predicates = [HasCPA] in {
  // Scalar add/subtract
  defm ADDPT : AddSubCPA<0, "addpt">;
  defm SUBPT : AddSubCPA<1, "subpt">;

  // Scalar multiply-add/subtract
  def MADDPT : MulAccumCPA<0, "maddpt">;
  def MSUBPT : MulAccumCPA<1, "msubpt">;
}

def round_v4fp32_to_v4bf16 :
  OutPatFrag<(ops node:$Rn),
             // NaN? Round : Quiet(NaN)
             (BSPv16i8 (FCMEQv4f32 $Rn, $Rn),
                       (ADDv4i32
                         (ADDv4i32 $Rn,
                           // Extract the LSB of the fp32 *truncated* to bf16.
                           (ANDv16i8 (USHRv4i32_shift V128:$Rn, (i32 16)),
                                     (MOVIv4i32 (i32 1), (i32 0)))),
                         // Bias which will help us break ties correctly.
                         (MOVIv4s_msl (i32 127), (i32 264))),
                       // Set the quiet bit in the NaN.
                       (ORRv4i32 $Rn, (i32 64), (i32 16)))>;

multiclass PromoteUnaryv8f16Tov4f32<SDPatternOperator InOp, Instruction OutInst> {
  let Predicates = [HasNoFullFP16] in
  def : Pat<(InOp (v8f16 V128:$Rn)),
            (v8f16 (FCVTNv8i16
              (INSERT_SUBREG (IMPLICIT_DEF),
                             (v4f16 (FCVTNv4i16
                               (v4f32 (OutInst
                                 (v4f32 (FCVTLv4i16 (v4i16 (EXTRACT_SUBREG V128:$Rn, dsub)))))))),
               dsub),
              (v4f32 (OutInst (v4f32 (FCVTLv8i16 V128:$Rn))))))>;

  let Predicates = [HasBF16] in
  def : Pat<(InOp (v8bf16 V128:$Rn)),
            (v8bf16 (BFCVTN2
              (v8bf16 (BFCVTN
                (v4f32 (OutInst
                  (v4f32 (SHLLv4i16 (v4i16 (EXTRACT_SUBREG V128:$Rn, dsub)))))))),
              (v4f32 (OutInst (v4f32 (SHLLv8i16 V128:$Rn))))))>;

  let Predicates = [HasNoBF16] in
  def : Pat<(InOp (v8bf16 V128:$Rn)),
            (UZP2v8i16
              (round_v4fp32_to_v4bf16 (v4f32 (OutInst
                  (v4f32 (SHLLv4i16 (v4i16 (EXTRACT_SUBREG V128:$Rn, dsub))))))),
              (round_v4fp32_to_v4bf16 (v4f32 (OutInst
                  (v4f32 (SHLLv8i16 V128:$Rn))))))>;
}
defm : PromoteUnaryv8f16Tov4f32<any_fceil,  	FRINTPv4f32>;
defm : PromoteUnaryv8f16Tov4f32<any_ffloor, 	FRINTMv4f32>;
defm : PromoteUnaryv8f16Tov4f32<any_fnearbyint, FRINTIv4f32>;
defm : PromoteUnaryv8f16Tov4f32<any_fround, 	FRINTAv4f32>;
defm : PromoteUnaryv8f16Tov4f32<any_froundeven, FRINTNv4f32>;
defm : PromoteUnaryv8f16Tov4f32<any_frint,  	FRINTXv4f32>;
defm : PromoteUnaryv8f16Tov4f32<any_ftrunc, 	FRINTZv4f32>;

multiclass PromoteBinaryv8f16Tov4f32<SDPatternOperator InOp, Instruction OutInst> {
  let Predicates = [HasNoFullFP16] in
  def : Pat<(InOp (v8f16 V128:$Rn), (v8f16 V128:$Rm)),
            (v8f16 (FCVTNv8i16
              (INSERT_SUBREG (IMPLICIT_DEF),
                             (v4f16 (FCVTNv4i16
                               (v4f32 (OutInst
                                 (v4f32 (FCVTLv4i16 (v4i16 (EXTRACT_SUBREG V128:$Rn, dsub)))),
                                 (v4f32 (FCVTLv4i16 (v4i16 (EXTRACT_SUBREG V128:$Rm, dsub)))))))),
               dsub),
              (v4f32 (OutInst (v4f32 (FCVTLv8i16 V128:$Rn)),
                              (v4f32 (FCVTLv8i16 V128:$Rm))))))>;

  let Predicates = [HasBF16] in
  def : Pat<(InOp (v8bf16 V128:$Rn), (v8bf16 V128:$Rm)),
            (v8bf16 (BFCVTN2
              (v8bf16 (BFCVTN
                (v4f32 (OutInst
                  (v4f32 (SHLLv4i16 (v4i16 (EXTRACT_SUBREG V128:$Rn, dsub)))),
                  (v4f32 (SHLLv4i16 (v4i16 (EXTRACT_SUBREG V128:$Rm, dsub)))))))),
              (v4f32 (OutInst (v4f32 (SHLLv8i16 V128:$Rn)),
                              (v4f32 (SHLLv8i16 V128:$Rm))))))>;

  let Predicates = [HasNoBF16] in
  def : Pat<(InOp (v8bf16 V128:$Rn), (v8bf16 V128:$Rm)),
            (UZP2v8i16
              (round_v4fp32_to_v4bf16 (v4f32 (OutInst
                  (v4f32 (SHLLv4i16 (v4i16 (EXTRACT_SUBREG V128:$Rn, dsub)))),
                  (v4f32 (SHLLv4i16 (v4i16 (EXTRACT_SUBREG V128:$Rm, dsub))))))),
              (round_v4fp32_to_v4bf16 (v4f32 (OutInst
                  (v4f32 (SHLLv8i16 V128:$Rn)),
                  (v4f32 (SHLLv8i16 V128:$Rm))))))>;
}
defm : PromoteBinaryv8f16Tov4f32<any_fadd, FADDv4f32>;
defm : PromoteBinaryv8f16Tov4f32<any_fdiv, FDIVv4f32>;
defm : PromoteBinaryv8f16Tov4f32<any_fmul, FMULv4f32>;
defm : PromoteBinaryv8f16Tov4f32<any_fsub, FSUBv4f32>;

include "AArch64InstrAtomics.td"
include "AArch64SVEInstrInfo.td"
include "AArch64SMEInstrInfo.td"
include "AArch64InstrGISel.td"
