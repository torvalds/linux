//=- AArch64Features.td - Describe AArch64 SubtargetFeatures -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

// A SubtargetFeature that represents one or more Architecture Extensions, as
// defined by the Arm ARM and typically identified by a 'FEAT_*' name.
// Each Extension record defines an ExtensionInfo entry in the Target Parser
// with a corresponding 'AEK_*' entry in the ArchExtKind enum.
class Extension<
  string TargetFeatureName,            // String used for -target-feature, unless overridden.
  string Spelling,                     // The XYZ in HasXYZ and AEK_XYZ.
  string ArchitectureFeatureName,      // The extension's "FEAT_*"" name(s) defined by the architecture
  string Desc,                         // Description.
  list<SubtargetFeature> Implies = []  // List of dependent features.
> : SubtargetFeature<TargetFeatureName, "Has" # Spelling, "true", Desc, Implies>
{
    string ArchExtKindSpelling = "AEK_" # Spelling; // ArchExtKind enum name.

    string ArchFeatureName = ArchitectureFeatureName;

    // The user visible name used by -march/-mcpu modifiers and target attribute
    // values. Extensions are not available on these by default.
    string UserVisibleName = "";

    // An alias that can be used on the command line, if the extension has one.
    // Used for correcting historical names while remaining backwards compatible.
    string UserVisibleAlias = "";
}

// An Extension that can be toggled via a '-march'/'-mcpu' modifier or a target
// attribute, e.g. '+sm4".
class ExtensionWithMArch<
  string TargetFeatureName,            // String used for -target-feature and -march, unless overridden.
  string Spelling,                     // The XYZ in HasXYZ and AEK_XYZ.
  string ArchitectureFeatureName,      // The extension's "FEAT_*"" name(s) defined by the architecture
  string Desc,                         // Description.
  list<SubtargetFeature> Implies = []  // List of dependent features.
> : Extension<TargetFeatureName, Spelling, ArchitectureFeatureName, Desc, Implies> {
    // In general, the name written on the command line should match the name
    // used for -target-feature. However, there are exceptions. Therefore we
    // add a separate field for this, to allow overriding it. Strongly prefer
    // not doing so.
    let UserVisibleName = TargetFeatureName;
}



// Each SubtargetFeature which corresponds to an Arm Architecture feature should
// be annotated with the respective FEAT_ feature name from the Architecture
// Reference Manual. If a SubtargetFeature enables instructions from multiple
// Arm Architecture Features, it should list all the relevant features. Not all
// FEAT_ features have a corresponding SubtargetFeature.


//===----------------------------------------------------------------------===//
//  Armv8.0 Architecture Extensions
//===----------------------------------------------------------------------===//

let ArchExtKindSpelling = "AEK_FP", UserVisibleName = "fp" in
def FeatureFPARMv8 : ExtensionWithMArch<"fp-armv8", "FPARMv8", "FEAT_FP",
  "Enable Armv8.0-A Floating Point Extensions">;

let ArchExtKindSpelling = "AEK_SIMD", UserVisibleName = "simd" in
def FeatureNEON : ExtensionWithMArch<"neon", "NEON", "FEAT_AdvSIMD",
  "Enable Advanced SIMD instructions", [FeatureFPARMv8]>;

def FeatureSHA2 : ExtensionWithMArch<"sha2", "SHA2", "FEAT_SHA1, FEAT_SHA256",
  "Enable SHA1 and SHA256 support", [FeatureNEON]>;

def FeatureAES : ExtensionWithMArch<"aes", "AES", "FEAT_AES, FEAT_PMULL",
  "Enable AES support", [FeatureNEON]>;

// Crypto has been split up and any combination is now valid (see the
// crypto definitions above). Also, crypto is now context sensitive:
// it has a different meaning for e.g. Armv8.4 than it has for Armv8.2.
// Therefore, we rely on Clang, the user interfacing tool, to pass on the
// appropriate crypto options. But here in the backend, crypto has very little
// meaning anymore. We kept the Crypto definition here for backward
// compatibility, and now imply features SHA2 and AES, which was the
// "traditional" meaning of Crypto.
def FeatureCrypto : ExtensionWithMArch<"crypto", "Crypto", "FEAT_Crypto",
  "Enable cryptographic instructions", [FeatureNEON, FeatureSHA2, FeatureAES]>;

def FeatureCRC : ExtensionWithMArch<"crc", "CRC", "FEAT_CRC32",
  "Enable Armv8.0-A CRC-32 checksum instructions">;

// This SubtargetFeature is special. It controls only whether codegen will turn
// `llvm.readcyclecounter()` into an access to a PMUv3 System Register. The
// `FEAT_PMUv3*` system registers are always available for assembly/disassembly.
let UserVisibleName = "pmuv3" in
def FeaturePerfMon : ExtensionWithMArch<"perfmon", "PerfMon", "FEAT_PMUv3",
  "Enable Armv8.0-A PMUv3 Performance Monitors extension">;

def FeatureSpecRestrict : Extension<"specrestrict", "SpecRestrict", "FEAT_CSV2_2",
  "Enable architectural speculation restriction">;

//===----------------------------------------------------------------------===//
//  Armv8.1 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureLSE : ExtensionWithMArch<"lse", "LSE", "FEAT_LSE",
  "Enable Armv8.1-A Large System Extension (LSE) atomic instructions">;

let UserVisibleAlias = "rdma" in
def FeatureRDM : ExtensionWithMArch<"rdm", "RDM", "FEAT_RDM",
  "Enable Armv8.1-A Rounding Double Multiply Add/Subtract instructions",
  [FeatureNEON]>;

def FeaturePAN : Extension<"pan", "PAN", "FEAT_PAN",
  "Enable Armv8.1-A Privileged Access-Never extension">;

def FeatureLOR : Extension<"lor", "LOR", "FEAT_LOR",
  "Enable Armv8.1-A Limited Ordering Regions extension">;

def FeatureCONTEXTIDREL2 : SubtargetFeature<"CONTEXTIDREL2", "HasCONTEXTIDREL2",
  "true", "Enable RW operand CONTEXTIDR_EL2">;

def FeatureVH : Extension<"vh", "VH", "FEAT_VHE",
  "Enable Armv8.1-A Virtual Host extension", [FeatureCONTEXTIDREL2]>;

//===----------------------------------------------------------------------===//
//  Armv8.2 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureSM4 : ExtensionWithMArch<"sm4", "SM4", "FEAT_SM4, FEAT_SM3",
  "Enable SM3 and SM4 support", [FeatureNEON]>;

def FeatureSHA3 : ExtensionWithMArch<"sha3", "SHA3", "FEAT_SHA3, FEAT_SHA512",
  "Enable SHA512 and SHA3 support", [FeatureNEON, FeatureSHA2]>;

def FeatureRAS : ExtensionWithMArch<"ras", "RAS", "FEAT_RAS, FEAT_RASv1p1",
  "Enable Armv8.0-A Reliability, Availability and Serviceability Extensions">;

let ArchExtKindSpelling = "AEK_FP16", UserVisibleName = "fp16" in
def FeatureFullFP16 : ExtensionWithMArch<"fullfp16", "FullFP16", "FEAT_FP16",
  "Enable half-precision floating-point data processing", [FeatureFPARMv8]>;

let ArchExtKindSpelling = "AEK_PROFILE", UserVisibleName = "profile" in
def FeatureSPE : ExtensionWithMArch<"spe", "SPE", "FEAT_SPE",
  "Enable Statistical Profiling extension">;

def FeaturePAN_RWV : Extension<"pan-rwv", "PAN_RWV", "FEAT_PAN2",
  "Enable Armv8.2-A PAN s1e1R and s1e1W Variants", [FeaturePAN]>;

def FeaturePsUAO : Extension<"uaops", "PsUAO", "FEAT_UAO",
  "Enable Armv8.2-A UAO PState">;

def FeatureCCPP : Extension<"ccpp", "CCPP", "FEAT_DPB",
  "Enable Armv8.2-A data Cache Clean to Point of Persistence">;

def FeatureSVE : ExtensionWithMArch<"sve", "SVE", "FEAT_SVE",
  "Enable Scalable Vector Extension (SVE) instructions", [FeatureFullFP16]>;

let ArchExtKindSpelling = "AEK_I8MM" in
def FeatureMatMulInt8 : ExtensionWithMArch<"i8mm", "MatMulInt8", "FEAT_I8MM",
  "Enable Matrix Multiply Int8 Extension">;

let ArchExtKindSpelling = "AEK_F32MM" in
def FeatureMatMulFP32 : ExtensionWithMArch<"f32mm", "MatMulFP32", "FEAT_F32MM",
  "Enable Matrix Multiply FP32 Extension", [FeatureSVE]>;

let ArchExtKindSpelling = "AEK_F64MM" in
def FeatureMatMulFP64 : ExtensionWithMArch<"f64mm", "MatMulFP64", "FEAT_F64MM",
  "Enable Matrix Multiply FP64 Extension", [FeatureSVE]>;

//===----------------------------------------------------------------------===//
//  Armv8.3 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureRCPC : ExtensionWithMArch<"rcpc", "RCPC", "FEAT_LRCPC",
  "Enable support for RCPC extension">;

def FeaturePAuth : ExtensionWithMArch<"pauth", "PAuth", "FEAT_PAuth",
  "Enable Armv8.3-A Pointer Authentication extension">;

let ArchExtKindSpelling = "AEK_JSCVT", UserVisibleName = "jscvt" in
def FeatureJS : ExtensionWithMArch<"jsconv", "JS", "FEAT_JSCVT",
  "Enable Armv8.3-A JavaScript FP conversion instructions",
  [FeatureFPARMv8]>;

def FeatureFPAC : Extension<"fpac", "FPAC", "FEAT_FPAC",
  "Enable v8.3-A Pointer Authentication Faulting enhancement">;

def FeatureCCIDX : Extension<"ccidx", "CCIDX", "FEAT_CCIDX",
  "Enable Armv8.3-A Extend of the CCSIDR number of sets">;

let ArchExtKindSpelling = "AEK_FCMA", UserVisibleName = "fcma" in
def FeatureComplxNum : ExtensionWithMArch<"complxnum", "ComplxNum", "FEAT_FCMA",
  "Enable Armv8.3-A Floating-point complex number support",
  [FeatureNEON]>;

def FeatureNV : Extension<"nv", "NV", "FEAT_NV, FEAT_NV2",
  "Enable Armv8.4-A Nested Virtualization Enchancement">;

//===----------------------------------------------------------------------===//
//  Armv8.4 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureLSE2 : Extension<"lse2", "LSE2", "FEAT_LSE2",
  "Enable Armv8.4-A Large System Extension 2 (LSE2) atomicity rules">;

def FeatureFP16FML : ExtensionWithMArch<"fp16fml", "FP16FML", "FEAT_FHM",
  "Enable FP16 FML instructions", [FeatureFullFP16]>;

def FeatureDotProd : ExtensionWithMArch<"dotprod", "DotProd", "FEAT_DotProd",
  "Enable dot product support", [FeatureNEON]>;

def FeatureMPAM : Extension<"mpam", "MPAM", "FEAT_MPAM",
  "Enable Armv8.4-A Memory system Partitioning and Monitoring extension">;

def FeatureDIT : ExtensionWithMArch<"dit", "DIT", "FEAT_DIT",
  "Enable Armv8.4-A Data Independent Timing instructions">;

def FeatureTRACEV8_4 : Extension<"tracev8.4", "TRACEV8_4", "FEAT_TRF",
  "Enable Armv8.4-A Trace extension">;

def FeatureAM : Extension<"am", "AM", "FEAT_AMUv1",
  "Enable Armv8.4-A Activity Monitors extension">;

def FeatureSEL2 : Extension<"sel2", "SEL2", "FEAT_SEL2",
  "Enable Armv8.4-A Secure Exception Level 2 extension">;

def FeatureTLB_RMI : Extension<"tlb-rmi", "TLB_RMI",
  "FEAT_TLBIOS, FEAT_TLBIRANGE",
  "Enable Armv8.4-A TLB Range and Maintenance instructions">;

def FeatureFlagM : ExtensionWithMArch<"flagm", "FlagM", "FEAT_FlagM",
  "Enable Armv8.4-A Flag Manipulation instructions">;

def FeatureRCPC_IMMO : Extension<"rcpc-immo", "RCPC_IMMO", "FEAT_LRCPC2",
  "Enable Armv8.4-A RCPC instructions with Immediate Offsets",
  [FeatureRCPC]>;

//===----------------------------------------------------------------------===//
//  Armv8.5 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureAltFPCmp : Extension<"altnzcv", "AlternativeNZCV", "FEAT_FlagM2",
  "Enable alternative NZCV format for floating point comparisons">;

def FeatureFRInt3264 : Extension<"fptoint", "FRInt3264", "FEAT_FRINTTS",
  "Enable FRInt[32|64][Z|X] instructions that round a floating-point number to "
  "an integer (in FP format) forcing it to fit into a 32- or 64-bit int">;

def FeatureSB : ExtensionWithMArch<"sb", "SB", "FEAT_SB",
  "Enable Armv8.5-A Speculation Barrier">;

def FeatureSSBS : ExtensionWithMArch<"ssbs", "SSBS", "FEAT_SSBS, FEAT_SSBS2",
  "Enable Speculative Store Bypass Safe bit">;

def FeaturePredRes : ExtensionWithMArch<"predres", "PredRes", "FEAT_SPECRES",
  "Enable Armv8.5-A execution and data prediction invalidation instructions">;

def FeatureCacheDeepPersist : Extension<"ccdp", "CCDP", "FEAT_DPB2",
  "Enable Armv8.5-A Cache Clean to Point of Deep Persistence">;

def FeatureBranchTargetId : ExtensionWithMArch<"bti", "BTI", "FEAT_BTI",
  "Enable Branch Target Identification">;

let ArchExtKindSpelling = "AEK_RAND", UserVisibleName = "rng" in
def FeatureRandGen : ExtensionWithMArch<"rand", "RandGen", "FEAT_RNG",
  "Enable Random Number generation instructions">;

// NOTE: "memtag" means FEAT_MTE + FEAT_MTE2 for -march or
// __attribute((target(...))), but only FEAT_MTE for FMV.
let UserVisibleName = "memtag" in
def FeatureMTE : ExtensionWithMArch<"mte", "MTE", "FEAT_MTE, FEAT_MTE2",
  "Enable Memory Tagging Extension">;

//===----------------------------------------------------------------------===//
//  Armv8.6 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureBF16 : ExtensionWithMArch<"bf16", "BF16", "FEAT_BF16",
  "Enable BFloat16 Extension">;

def FeatureAMVS : Extension<"amvs", "AMVS", "FEAT_AMUv1p1",
  "Enable Armv8.6-A Activity Monitors Virtualization support",
  [FeatureAM]>;

def FeatureFineGrainedTraps : Extension<"fgt", "FineGrainedTraps", "FEAT_FGT",
  "Enable fine grained virtualization traps extension">;

def FeatureEnhancedCounterVirtualization :
    Extension<"ecv", "EnhancedCounterVirtualization", "FEAT_ECV",
      "Enable enhanced counter virtualization extension">;

//===----------------------------------------------------------------------===//
//  Armv8.7 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureXS : Extension<"xs", "XS", "FEAT_XS",
  "Enable Armv8.7-A limited-TLB-maintenance instruction">;

def FeatureWFxT : ExtensionWithMArch<"wfxt", "WFxT", "FEAT_WFxT",
  "Enable Armv8.7-A WFET and WFIT instruction">;

def FeatureHCX : Extension<"hcx", "HCX", "FEAT_HCX",
  "Enable Armv8.7-A HCRX_EL2 system register">;

def FeatureLS64 : ExtensionWithMArch<"ls64", "LS64",
  "FEAT_LS64, FEAT_LS64_V, FEAT_LS64_ACCDATA",
  "Enable Armv8.7-A LD64B/ST64B Accelerator Extension">;

def FeatureSPE_EEF : Extension<"spe-eef", "SPE_EEF", "FEAT_SPEv1p2",
  "Enable extra register in the Statistical Profiling Extension">;

//===----------------------------------------------------------------------===//
//  Armv8.8 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureHBC : ExtensionWithMArch<"hbc", "HBC", "FEAT_HBC",
    "Enable Armv8.8-A Hinted Conditional Branches Extension">;

def FeatureMOPS : ExtensionWithMArch<"mops", "MOPS", "FEAT_MOPS",
    "Enable Armv8.8-A memcpy and memset acceleration instructions">;

def FeatureNMI : Extension<"nmi", "NMI", "FEAT_NMI, FEAT_GICv3_NMI",
  "Enable Armv8.8-A Non-maskable Interrupts">;

//===----------------------------------------------------------------------===//
//  Armv8.9 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureRASv2 : ExtensionWithMArch<"rasv2", "RASv2", "FEAT_RASv2",
  "Enable Armv8.9-A Reliability, Availability and Serviceability Extensions",
  [FeatureRAS]>;

def FeatureCSSC : ExtensionWithMArch<"cssc", "CSSC", "FEAT_CSSC",
  "Enable Common Short Sequence Compression (CSSC) instructions">;

def FeatureCLRBHB : Extension<"clrbhb", "CLRBHB", "FEAT_CLRBHB",
  "Enable Clear BHB instruction">;

def FeaturePRFM_SLC : Extension<"prfm-slc-target", "PRFM_SLC", "FEAT_PRFMSLC",
  "Enable SLC target for PRFM instruction">;

let UserVisibleName = "predres2" in
def FeatureSPECRES2 : ExtensionWithMArch<"specres2", "SPECRES2", "FEAT_SPECRES2",
  "Enable Speculation Restriction Instruction",
  [FeaturePredRes]>;

def FeatureRCPC3 : ExtensionWithMArch<"rcpc3", "RCPC3", "FEAT_LRCPC3",
  "Enable Armv8.9-A RCPC instructions for A64 and Advanced SIMD and floating-point instruction set",
  [FeatureRCPC_IMMO]>;

def FeatureTHE : ExtensionWithMArch<"the", "THE", "FEAT_THE",
  "Enable Armv8.9-A Translation Hardening Extension">;

//===----------------------------------------------------------------------===//
//  Armv9.0 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureUseFixedOverScalableIfEqualCost: SubtargetFeature<"use-fixed-over-scalable-if-equal-cost",
  "UseFixedOverScalableIfEqualCost", "true",
  "Prefer fixed width loop vectorization over scalable if the cost-model assigns equal costs">;

def FeatureUseScalarIncVL : SubtargetFeature<"use-scalar-inc-vl",
  "UseScalarIncVL", "true", "Prefer inc/dec over add+cnt">;

def FeatureSVE2 : ExtensionWithMArch<"sve2", "SVE2", "FEAT_SVE2",
  "Enable Scalable Vector Extension 2 (SVE2) instructions",
  [FeatureSVE, FeatureUseScalarIncVL]>;

def FeatureSVE2AES : ExtensionWithMArch<"sve2-aes", "SVE2AES",
  "FEAT_SVE_AES, FEAT_SVE_PMULL128",
  "Enable AES SVE2 instructions", [FeatureSVE2, FeatureAES]>;

def FeatureSVE2SM4 : ExtensionWithMArch<"sve2-sm4", "SVE2SM4", "FEAT_SVE_SM4",
  "Enable SM4 SVE2 instructions", [FeatureSVE2, FeatureSM4]>;

def FeatureSVE2SHA3 : ExtensionWithMArch<"sve2-sha3", "SVE2SHA3", "FEAT_SVE_SHA3",
  "Enable SHA3 SVE2 instructions", [FeatureSVE2, FeatureSHA3]>;

def FeatureSVE2BitPerm : ExtensionWithMArch<"sve2-bitperm", "SVE2BitPerm",
  "FEAT_SVE_BitPerm",
  "Enable bit permutation SVE2 instructions", [FeatureSVE2]>;

def FeatureTRBE : Extension<"trbe", "TRBE", "FEAT_TRBE",
  "Enable Trace Buffer Extension">;

def FeatureETE : Extension<"ete", "ETE", "FEAT_ETE",
  "Enable Embedded Trace Extension", [FeatureTRBE]>;

def FeatureTME : ExtensionWithMArch<"tme", "TME", "FEAT_TME",
  "Enable Transactional Memory Extension">;

//===----------------------------------------------------------------------===//
//  Armv9.1 Architecture Extensions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Armv9.2 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureBRBE : ExtensionWithMArch<"brbe", "BRBE", "FEAT_BRBE",
  "Enable Branch Record Buffer Extension">;

def FeatureRME : Extension<"rme", "RME", "FEAT_RME",
  "Enable Realm Management Extension">;

def FeatureSME : ExtensionWithMArch<"sme", "SME", "FEAT_SME",
  "Enable Scalable Matrix Extension (SME)", [FeatureBF16, FeatureUseScalarIncVL]>;

def FeatureSMEF64F64 : ExtensionWithMArch<"sme-f64f64", "SMEF64F64", "FEAT_SME_F64F64",
  "Enable Scalable Matrix Extension (SME) F64F64 instructions", [FeatureSME]>;

def FeatureSMEI16I64 : ExtensionWithMArch<"sme-i16i64", "SMEI16I64", "FEAT_SME_I16I64",
  "Enable Scalable Matrix Extension (SME) I16I64 instructions", [FeatureSME]>;

def FeatureSMEFA64 : ExtensionWithMArch<"sme-fa64", "SMEFA64", "FEAT_SME_FA64",
  "Enable the full A64 instruction set in streaming SVE mode", [FeatureSME, FeatureSVE2]>;

//===----------------------------------------------------------------------===//
//  Armv9.3 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureSME2 : ExtensionWithMArch<"sme2", "SME2", "FEAT_SME2",
  "Enable Scalable Matrix Extension 2 (SME2) instructions", [FeatureSME]>;

def FeatureMEC : Extension<"mec", "MEC", "FEAT_MEC",
  "Enable Memory Encryption Contexts Extension", [FeatureRME]>;

//===----------------------------------------------------------------------===//
//  Armv9.4 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureSVE2p1: ExtensionWithMArch<"sve2p1", "SVE2p1", "FEAT_SVE2p1",
  "Enable Scalable Vector Extension 2.1 instructions", [FeatureSVE2]>;

def FeatureB16B16 : ExtensionWithMArch<"b16b16", "B16B16", "FEAT_SVE_B16B16",
  "Enable SVE2.1 or SME2.1 non-widening BFloat16 to BFloat16 instructions", [FeatureBF16]>;

// FeatureSVEB16B16 and FeatureSMEB16B16 act as aliases for {FeatureB16B16}, and
// {FeatureB16B16, FeatureSME2} respectively. This allows LLVM-20 interfacing programs
// that use '+sve-b16b16' and '+sme-b16b16' to compile in LLVM-19.
def FeatureSVEB16B16 : ExtensionWithMArch<"sve-b16b16", "SVEB16B16", "FEAT_SVE_B16B16",
  "Enable SVE2 non-widening and SME2 Z-targeting non-widening BFloat16 instructions", [FeatureB16B16]>;

def FeatureSMEB16B16 : ExtensionWithMArch<"sme-b16b16", "SMEB16B16", "FEAT_SME_B16B16",
  "Enable SME2.1 ZA-targeting non-widening BFloat16 instructions", [FeatureSME2, FeatureB16B16]>;

def FeatureSMEF16F16 : ExtensionWithMArch<"sme-f16f16", "SMEF16F16", "FEAT_SME_F16F16",
  "Enable SME non-widening Float16 instructions", [FeatureSME2]>;

def FeatureSME2p1 : ExtensionWithMArch<"sme2p1", "SME2p1", "FEAT_SME2p1",
  "Enable Scalable Matrix Extension 2.1 instructions", [FeatureSME2]>;

def FeatureCHK : Extension<"chk", "CHK", "FEAT_CHK",
  "Enable Armv8.0-A Check Feature Status Extension">;

def FeatureGCS : ExtensionWithMArch<"gcs", "GCS", "FEAT_GCS",
  "Enable Armv9.4-A Guarded Call Stack Extension", [FeatureCHK]>;

def FeatureITE : ExtensionWithMArch<"ite", "ITE", "FEAT_ITE",
  "Enable Armv9.4-A Instrumentation Extension", [FeatureETE, FeatureTRBE]>;

def FeatureLSE128 : ExtensionWithMArch<"lse128", "LSE128", "FEAT_LSE128",
  "Enable Armv9.4-A 128-bit Atomic instructions",
  [FeatureLSE]>;

// FEAT_D128, FEAT_LVA3, FEAT_SYSREG128, and FEAT_SYSINSTR128 are mutually implicit.
// Therefore group them all under a single feature flag, d128:
def FeatureD128 : ExtensionWithMArch<"d128", "D128",
  "FEAT_D128, FEAT_LVA3, FEAT_SYSREG128, FEAT_SYSINSTR128",
  "Enable Armv9.4-A 128-bit Page Table Descriptors, System Registers "
  "and instructions",
  [FeatureLSE128]>;

//===----------------------------------------------------------------------===//
//  Armv9.5 Architecture Extensions
//===----------------------------------------------------------------------===//

def FeatureFAMINMAX: ExtensionWithMArch<"faminmax", "FAMINMAX", "FEAT_FAMINMAX",
 "Enable FAMIN and FAMAX instructions">;

def FeatureLUT: ExtensionWithMArch<"lut", "LUT", "FEAT_LUT",
 "Enable Lookup Table instructions">;
   
def FeatureFP8 : ExtensionWithMArch<"fp8", "FP8", "FEAT_FP8",
  "Enable FP8 instructions", [FeatureFAMINMAX, FeatureLUT, FeatureBF16]>;
  
def FeatureFP8FMA : ExtensionWithMArch<"fp8fma", "FP8FMA", "FEAT_FP8FMA",
  "Enable Armv9.5-A FP8 multiply-add instructions", [FeatureFP8]>;

def FeatureSSVE_FP8FMA : ExtensionWithMArch<"ssve-fp8fma", "SSVE_FP8FMA", "FEAT_SSVE_FP8FMA",
  "Enable SVE2 FP8 multiply-add instructions", [FeatureSME2, FeatureFP8]>;

def FeatureFP8DOT4: ExtensionWithMArch<"fp8dot4", "FP8DOT4", "FEAT_FP8DOT4",
  "Enable FP8 4-way dot instructions", [FeatureFP8FMA]>;
  
def FeatureFP8DOT2: ExtensionWithMArch<"fp8dot2", "FP8DOT2", "FEAT_FP8DOT2",
  "Enable FP8 2-way dot instructions", [FeatureFP8DOT4]>;

def FeatureSSVE_FP8DOT4 : ExtensionWithMArch<"ssve-fp8dot4", "SSVE_FP8DOT4", "FEAT_SSVE_FP8DOT4",
  "Enable SVE2 FP8 4-way dot product instructions", [FeatureSSVE_FP8FMA]>;

def FeatureSSVE_FP8DOT2 : ExtensionWithMArch<"ssve-fp8dot2", "SSVE_FP8DOT2", "FEAT_SSVE_FP8DOT2",
  "Enable SVE2 FP8 2-way dot product instructions", [FeatureSSVE_FP8DOT4]>;

def FeatureSME_LUTv2 : ExtensionWithMArch<"sme-lutv2", "SME_LUTv2", "FEAT_SME_LUTv2",
  "Enable Scalable Matrix Extension (SME) LUTv2 instructions">;

def FeatureSMEF8F32 : ExtensionWithMArch<"sme-f8f32", "SMEF8F32", "FEAT_SME_F8F32",
  "Enable Scalable Matrix Extension (SME) F8F32 instructions", [FeatureSME2, FeatureFP8]>;

def FeatureSMEF8F16 : ExtensionWithMArch<"sme-f8f16", "SMEF8F16", "FEAT_SME_F8F16",
  "Enable Scalable Matrix Extension (SME) F8F16 instructions", [FeatureSMEF8F32]>;

def FeatureCPA : ExtensionWithMArch<"cpa", "CPA", "FEAT_CPA",
  "Enable Armv9.5-A Checked Pointer Arithmetic">;

def FeaturePAuthLR : ExtensionWithMArch<"pauth-lr", "PAuthLR", "FEAT_PAuth_LR",
  "Enable Armv9.5-A PAC enhancements">;

def FeatureTLBIW : ExtensionWithMArch<"tlbiw", "TLBIW", "FEAT_TLBIW",
  "Enable Armv9.5-A TLBI VMALL for Dirty State">;

//===----------------------------------------------------------------------===//
//  Other Features
//===----------------------------------------------------------------------===//

def FeatureOutlineAtomics : SubtargetFeature<"outline-atomics", "OutlineAtomics", "true",
  "Enable out of line atomics to support LSE instructions">;

def FeatureFMV : SubtargetFeature<"fmv", "HasFMV", "true",
  "Enable Function Multi Versioning support.">;

// This flag is currently still labeled as Experimental, but when fully
// implemented this should tell the compiler to use the zeroing pseudos to
// benefit from the reverse instructions (e.g. SUB vs SUBR) if the inactive
// lanes are known to be zero. The pseudos will then be expanded using the
// MOVPRFX instruction to zero the inactive lanes. This feature should only be
// enabled if MOVPRFX instructions are known to merge with the destructive
// operations they prefix.
//
// This feature could similarly be extended to support cheap merging of _any_
// value into the inactive lanes using the MOVPRFX instruction that uses
// merging-predication.
def FeatureExperimentalZeroingPseudos
    : SubtargetFeature<"use-experimental-zeroing-pseudos",
                       "UseExperimentalZeroingPseudos", "true",
                       "Hint to the compiler that the MOVPRFX instruction is "
                       "merged with destructive operations",
                       []>;

def FeatureNoSVEFPLD1R : SubtargetFeature<"no-sve-fp-ld1r",
  "NoSVEFPLD1R", "true", "Avoid using LD1RX instructions for FP">;

def FeatureZCRegMove : SubtargetFeature<"zcm", "HasZeroCycleRegMove", "true",
                                        "Has zero-cycle register moves">;

def FeatureZCZeroingGP : SubtargetFeature<"zcz-gp", "HasZeroCycleZeroingGP", "true",
                                        "Has zero-cycle zeroing instructions for generic registers">;

// It is generally beneficial to rewrite "fmov s0, wzr" to "movi d0, #0".
// as movi is more efficient across all cores. Newer cores can eliminate
// fmovs early and there is no difference with movi, but this not true for
// all implementations.
def FeatureNoZCZeroingFP : SubtargetFeature<"no-zcz-fp", "HasZeroCycleZeroingFP", "false",
                                        "Has no zero-cycle zeroing instructions for FP registers">;

def FeatureZCZeroing : SubtargetFeature<"zcz", "HasZeroCycleZeroing", "true",
                                        "Has zero-cycle zeroing instructions",
                                        [FeatureZCZeroingGP]>;

/// ... but the floating-point version doesn't quite work in rare cases on older
/// CPUs.
def FeatureZCZeroingFPWorkaround : SubtargetFeature<"zcz-fp-workaround",
    "HasZeroCycleZeroingFPWorkaround", "true",
    "The zero-cycle floating-point zeroing instruction has a bug">;

def FeatureStrictAlign : SubtargetFeature<"strict-align",
                                          "RequiresStrictAlign", "true",
                                          "Disallow all unaligned memory "
                                          "access">;

foreach i = {1-7,9-15,18,20-28} in
    def FeatureReserveX#i : SubtargetFeature<"reserve-x"#i, "ReserveXRegister["#i#"]", "true",
                                             "Reserve X"#i#", making it unavailable "
                                             "as a GPR">;

def FeatureReserveLRForRA : SubtargetFeature<"reserve-lr-for-ra",
                                             "ReserveLRForRA", "true",
                                             "Reserve LR for call use only">;

foreach i = {8-15,18} in
    def FeatureCallSavedX#i : SubtargetFeature<"call-saved-x"#i,
         "CustomCallSavedXRegs["#i#"]", "true", "Make X"#i#" callee saved.">;

def FeatureBalanceFPOps : SubtargetFeature<"balance-fp-ops", "BalanceFPOps",
    "true",
    "balance mix of odd and even D-registers for fp multiply(-accumulate) ops">;

def FeaturePredictableSelectIsExpensive : SubtargetFeature<
    "predictable-select-expensive", "PredictableSelectIsExpensive", "true",
    "Prefer likely predicted branches over selects">;

def FeatureEnableSelectOptimize : SubtargetFeature<
    "enable-select-opt", "EnableSelectOptimize", "true",
    "Enable the select optimize pass for select loop heuristics">;

def FeatureExynosCheapAsMoveHandling : SubtargetFeature<"exynos-cheap-as-move",
    "HasExynosCheapAsMoveHandling", "true",
    "Use Exynos specific handling of cheap instructions">;

def FeaturePostRAScheduler : SubtargetFeature<"use-postra-scheduler",
    "UsePostRAScheduler", "true", "Schedule again after register allocation">;

def FeatureSlowMisaligned128Store : SubtargetFeature<"slow-misaligned-128store",
    "IsMisaligned128StoreSlow", "true", "Misaligned 128 bit stores are slow">;

def FeatureSlowPaired128 : SubtargetFeature<"slow-paired-128",
    "IsPaired128Slow", "true", "Paired 128 bit loads and stores are slow">;

def FeatureAscendStoreAddress : SubtargetFeature<"ascend-store-address",
    "IsStoreAddressAscend", "true",
    "Schedule vector stores by ascending address">;

def FeatureSlowSTRQro : SubtargetFeature<"slow-strqro-store", "IsSTRQroSlow",
    "true", "STR of Q register with register offset is slow">;

def FeatureAlternateSExtLoadCVTF32Pattern : SubtargetFeature<
    "alternate-sextload-cvt-f32-pattern", "UseAlternateSExtLoadCVTF32Pattern",
    "true", "Use alternative pattern for sextload convert to f32">;

def FeatureArithmeticBccFusion : SubtargetFeature<
    "arith-bcc-fusion", "HasArithmeticBccFusion", "true",
    "CPU fuses arithmetic+bcc operations">;

def FeatureArithmeticCbzFusion : SubtargetFeature<
    "arith-cbz-fusion", "HasArithmeticCbzFusion", "true",
    "CPU fuses arithmetic + cbz/cbnz operations">;

def FeatureCmpBccFusion : SubtargetFeature<
    "cmp-bcc-fusion", "HasCmpBccFusion", "true",
    "CPU fuses cmp+bcc operations">;

def FeatureFuseAddress : SubtargetFeature<
    "fuse-address", "HasFuseAddress", "true",
    "CPU fuses address generation and memory operations">;

def FeatureFuseAES : SubtargetFeature<
    "fuse-aes", "HasFuseAES", "true",
    "CPU fuses AES crypto operations">;

def FeatureFuseArithmeticLogic : SubtargetFeature<
    "fuse-arith-logic", "HasFuseArithmeticLogic", "true",
    "CPU fuses arithmetic and logic operations">;

def FeatureFuseCCSelect : SubtargetFeature<
    "fuse-csel", "HasFuseCCSelect", "true",
    "CPU fuses conditional select operations">;

def FeatureFuseCryptoEOR : SubtargetFeature<
    "fuse-crypto-eor", "HasFuseCryptoEOR", "true",
    "CPU fuses AES/PMULL and EOR operations">;

def FeatureFuseAdrpAdd : SubtargetFeature<
    "fuse-adrp-add", "HasFuseAdrpAdd", "true",
    "CPU fuses adrp+add operations">;

def FeatureFuseLiterals : SubtargetFeature<
    "fuse-literals", "HasFuseLiterals", "true",
    "CPU fuses literal generation operations">;

def FeatureFuseAddSub2RegAndConstOne : SubtargetFeature<
   "fuse-addsub-2reg-const1", "HasFuseAddSub2RegAndConstOne", "true",
   "CPU fuses (a + b + 1) and (a - b - 1)">;

def FeatureDisableLatencySchedHeuristic : SubtargetFeature<
    "disable-latency-sched-heuristic", "DisableLatencySchedHeuristic", "true",
    "Disable latency scheduling heuristic">;

def FeatureStorePairSuppress : SubtargetFeature<
    "store-pair-suppress", "EnableStorePairSuppress", "true",
    "Enable Store Pair Suppression heuristics">;

def FeatureForce32BitJumpTables
   : SubtargetFeature<"force-32bit-jump-tables", "Force32BitJumpTables", "true",
                      "Force jump table entries to be 32-bits wide except at MinSize">;

def FeatureUseRSqrt : SubtargetFeature<
    "use-reciprocal-square-root", "UseRSqrt", "true",
    "Use the reciprocal square root approximation">;

def FeatureNoNegativeImmediates : SubtargetFeature<"no-neg-immediates",
                                        "NegativeImmediates", "false",
                                        "Convert immediates and instructions "
                                        "to their negated or complemented "
                                        "equivalent when the immediate does "
                                        "not fit in the encoding.">;

// Address operands with shift amount 2 or 3 are fast on all Arm chips except
// some old Apple cores (A7-A10?) which handle all shifts slowly. Cortex-A57
// and derived designs through Cortex-X1 take an extra micro-op for shifts
// of 1 or 4. Other Arm chips handle all shifted operands at the same speed
// as unshifted operands.
//
// We don't try to model the behavior of the old Apple cores because new code
// targeting A7 is very unlikely to actually run on an A7. The Cortex cores
// are modeled by FeatureAddrLSLSlow14.
def FeatureAddrLSLSlow14 : SubtargetFeature<
    "addr-lsl-slow-14", "HasAddrLSLSlow14", "true",
    "Address operands with shift amount of 1 or 4 are slow">;

def FeatureALULSLFast : SubtargetFeature<
    "alu-lsl-fast", "HasALULSLFast", "true",
    "Add/Sub operations with lsl shift <= 4 are cheap">;

def FeatureAggressiveFMA :
  SubtargetFeature<"aggressive-fma",
                   "HasAggressiveFMA",
                   "true",
                   "Enable Aggressive FMA for floating-point.">;

def FeatureTaggedGlobals : SubtargetFeature<"tagged-globals",
    "AllowTaggedGlobals",
    "true", "Use an instruction sequence for taking the address of a global "
    "that allows a memory tag in the upper address bits">;

def FeatureAppleA7SysReg  : SubtargetFeature<"apple-a7-sysreg", "HasAppleA7SysReg", "true",
  "Apple A7 (the CPU formerly known as Cyclone)">;

def FeatureEL2VMSA : SubtargetFeature<"el2vmsa", "HasEL2VMSA", "true",
  "Enable Exception Level 2 Virtual Memory System Architecture">;

def FeatureEL3 : SubtargetFeature<"el3", "HasEL3", "true",
  "Enable Exception Level 3">;

def FeatureFixCortexA53_835769 : SubtargetFeature<"fix-cortex-a53-835769",
  "FixCortexA53_835769", "true", "Mitigate Cortex-A53 Erratum 835769">;

def FeatureNoBTIAtReturnTwice : SubtargetFeature<"no-bti-at-return-twice",
                                                 "NoBTIAtReturnTwice", "true",
                                                 "Don't place a BTI instruction "
                                                 "after a return-twice">;

def FeatureDisableLdp : SubtargetFeature<"disable-ldp", "HasDisableLdp",
    "true", "Do not emit ldp">;

def FeatureDisableStp : SubtargetFeature<"disable-stp", "HasDisableStp",
    "true", "Do not emit stp">;

def FeatureLdpAlignedOnly : SubtargetFeature<"ldp-aligned-only", "HasLdpAlignedOnly",
    "true", "In order to emit ldp, first check if the load will be aligned to 2 * element_size">;

def FeatureStpAlignedOnly : SubtargetFeature<"stp-aligned-only", "HasStpAlignedOnly",
    "true", "In order to emit stp, first check if the store will be aligned to 2 * element_size">;

//===----------------------------------------------------------------------===//
// Architectures.
//
class Architecture64<
  int major, int minor, string profile,
  string target_feature_name,
  list<SubtargetFeature> implied_features,
  list<Extension> default_extensions
> : SubtargetFeature<target_feature_name,
    "HasV" # major # "_" # minor # profile # "Ops", "true",
    "Support ARM " # target_feature_name # " architecture",
    implied_features
> {
  int Major = major;
  int Minor = minor;
  string Profile = profile;

  // Extensions enabled by default. Not the same as implied SubtargetFeatures.
  list<Extension> DefaultExts = default_extensions;
}

def HasV8_0aOps : Architecture64<8, 0, "a", "v8a",
  [FeatureEL2VMSA, FeatureEL3],
  [FeatureFPARMv8, FeatureNEON]>;
def HasV8_1aOps : Architecture64<8, 1, "a", "v8.1a",
  [HasV8_0aOps, FeatureCRC, FeatureLSE, FeatureRDM, FeaturePAN, FeatureLOR,
   FeatureVH],
  !listconcat(HasV8_0aOps.DefaultExts, [FeatureCRC, FeatureLSE, FeatureRDM])>;
def HasV8_2aOps : Architecture64<8, 2, "a", "v8.2a",
  [HasV8_1aOps, FeaturePsUAO, FeaturePAN_RWV, FeatureRAS, FeatureCCPP],
  !listconcat(HasV8_1aOps.DefaultExts, [FeatureRAS])>;
def HasV8_3aOps : Architecture64<8, 3, "a", "v8.3a",
  [HasV8_2aOps, FeatureRCPC, FeaturePAuth, FeatureJS, FeatureComplxNum],
  !listconcat(HasV8_2aOps.DefaultExts, [FeatureComplxNum, FeatureJS,
    FeaturePAuth, FeatureRCPC,  FeatureCCIDX])>;
def HasV8_4aOps : Architecture64<8, 4, "a", "v8.4a",
  [HasV8_3aOps, FeatureDotProd, FeatureNV, FeatureMPAM, FeatureDIT,
    FeatureTRACEV8_4, FeatureAM, FeatureSEL2, FeatureTLB_RMI, FeatureFlagM,
    FeatureRCPC_IMMO, FeatureLSE2],
  !listconcat(HasV8_3aOps.DefaultExts, [FeatureDotProd, FeatureDIT, FeatureFlagM])>;
def HasV8_5aOps : Architecture64<8, 5, "a", "v8.5a",
  [HasV8_4aOps, FeatureAltFPCmp, FeatureFRInt3264, FeatureSpecRestrict,
    FeatureSB, FeaturePredRes, FeatureCacheDeepPersist,
    FeatureBranchTargetId],
  !listconcat(HasV8_4aOps.DefaultExts, [FeaturePredRes, FeatureSSBS, FeatureBranchTargetId, FeatureSB])>;
def HasV8_6aOps : Architecture64<8, 6, "a", "v8.6a",
  [HasV8_5aOps, FeatureAMVS, FeatureBF16, FeatureFineGrainedTraps,
    FeatureEnhancedCounterVirtualization, FeatureMatMulInt8],
  !listconcat(HasV8_5aOps.DefaultExts, [FeatureBF16, FeatureMatMulInt8])>;
def HasV8_7aOps : Architecture64<8, 7, "a", "v8.7a",
  [HasV8_6aOps, FeatureXS, FeatureWFxT, FeatureHCX],
  !listconcat(HasV8_6aOps.DefaultExts, [FeatureWFxT])>;
def HasV8_8aOps : Architecture64<8, 8, "a", "v8.8a",
  [HasV8_7aOps, FeatureHBC, FeatureMOPS, FeatureNMI],
  !listconcat(HasV8_7aOps.DefaultExts, [FeatureMOPS, FeatureHBC])>;
def HasV8_9aOps : Architecture64<8, 9, "a", "v8.9a",
  [HasV8_8aOps, FeatureCLRBHB, FeaturePRFM_SLC, FeatureSPECRES2,
   FeatureCSSC, FeatureRASv2, FeatureCHK],
  !listconcat(HasV8_8aOps.DefaultExts, [FeatureSPECRES2, FeatureCSSC,
    FeatureRASv2])>;
def HasV9_0aOps : Architecture64<9, 0, "a", "v9a",
  [HasV8_5aOps],
  !listconcat(HasV8_5aOps.DefaultExts, [FeatureFullFP16, FeatureSVE,
    FeatureSVE2])>;
def HasV9_1aOps : Architecture64<9, 1, "a", "v9.1a",
  [HasV8_6aOps, HasV9_0aOps],
  !listconcat(HasV9_0aOps.DefaultExts, [FeatureBF16, FeatureMatMulInt8, FeatureRME])>;
def HasV9_2aOps : Architecture64<9, 2, "a", "v9.2a",
  [HasV8_7aOps, HasV9_1aOps],
  !listconcat(HasV9_1aOps.DefaultExts, [FeatureMEC, FeatureWFxT])>;
def HasV9_3aOps : Architecture64<9, 3, "a", "v9.3a",
  [HasV8_8aOps, HasV9_2aOps],
  !listconcat(HasV9_2aOps.DefaultExts, [FeatureMOPS, FeatureHBC])>;
def HasV9_4aOps : Architecture64<9, 4, "a", "v9.4a",
  [HasV8_9aOps, HasV9_3aOps],
  !listconcat(HasV9_3aOps.DefaultExts, [FeatureSPECRES2, FeatureCSSC,
    FeatureRASv2])>;
def HasV9_5aOps : Architecture64<9, 5, "a", "v9.5a",
  [HasV9_4aOps, FeatureCPA],
  !listconcat(HasV9_4aOps.DefaultExts, [FeatureCPA,  FeatureLUT, FeatureFAMINMAX])>;
def HasV8_0rOps : Architecture64<8, 0, "r", "v8r",
  [ //v8.1
    FeatureCRC, FeaturePAN, FeatureLSE, FeatureCONTEXTIDREL2,
    //v8.2
    FeatureRAS, FeaturePsUAO, FeatureCCPP, FeaturePAN_RWV,
    //v8.3
    FeaturePAuth, FeatureRCPC,
    //v8.4
    FeatureTRACEV8_4, FeatureTLB_RMI, FeatureFlagM, FeatureDIT, FeatureSEL2,
    FeatureRCPC_IMMO,
    // Not mandatory in v8.0-R, but included here on the grounds that it
    // only enables names of system registers
    FeatureSpecRestrict
  ],
  // For v8-R, we do not enable crypto and align with GCC that enables a more
  // minimal set of optional architecture extensions.
  !listconcat(
    !listremove(HasV8_5aOps.DefaultExts, [FeatureBranchTargetId, FeaturePredRes]),
    [FeatureSSBS, FeatureFullFP16, FeatureFP16FML, FeatureSB]
  )>;

//===----------------------------------------------------------------------===//
// Access to privileged registers
//===----------------------------------------------------------------------===//

foreach i = 1-3 in
def FeatureUseEL#i#ForTP : SubtargetFeature<"tpidr-el"#i, "UseEL"#i#"ForTP",
  "true", "Permit use of TPIDR_EL"#i#" for the TLS base">;
def FeatureUseROEL0ForTP : SubtargetFeature<"tpidrro-el0", "UseROEL0ForTP",
  "true", "Permit use of TPIDRRO_EL0 for the TLS base">;

//===----------------------------------------------------------------------===//
// Control codegen mitigation against Straight Line Speculation vulnerability.
//===----------------------------------------------------------------------===//

def FeatureHardenSlsRetBr : SubtargetFeature<"harden-sls-retbr",
  "HardenSlsRetBr", "true",
  "Harden against straight line speculation across RET and BR instructions">;
def FeatureHardenSlsBlr : SubtargetFeature<"harden-sls-blr",
  "HardenSlsBlr", "true",
  "Harden against straight line speculation across BLR instructions">;
def FeatureHardenSlsNoComdat : SubtargetFeature<"harden-sls-nocomdat",
  "HardenSlsNoComdat", "true",
  "Generate thunk code for SLS mitigation in the normal text section">;


// Only intended to be used by disassemblers.
def FeatureAll
    : SubtargetFeature<"all", "IsAll", "true", "Enable all instructions">;
