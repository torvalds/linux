//===-- PPC.td - Describe the PowerPC Target Machine -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the top level entry point for the PowerPC target.
//
//===----------------------------------------------------------------------===//

// Get the target-independent interfaces which we are implementing.
//
include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// PowerPC Subtarget features.
//

//===----------------------------------------------------------------------===//
// CPU Directives                                                             //
//===----------------------------------------------------------------------===//

def Directive440 : SubtargetFeature<"", "CPUDirective", "PPC::DIR_440", "">;
def Directive601 : SubtargetFeature<"", "CPUDirective", "PPC::DIR_601", "">;
def Directive602 : SubtargetFeature<"", "CPUDirective", "PPC::DIR_602", "">;
def Directive603 : SubtargetFeature<"", "CPUDirective", "PPC::DIR_603", "">;
def Directive604 : SubtargetFeature<"", "CPUDirective", "PPC::DIR_603", "">;
def Directive620 : SubtargetFeature<"", "CPUDirective", "PPC::DIR_603", "">;
def Directive7400: SubtargetFeature<"", "CPUDirective", "PPC::DIR_7400", "">;
def Directive750 : SubtargetFeature<"", "CPUDirective", "PPC::DIR_750", "">;
def Directive970 : SubtargetFeature<"", "CPUDirective", "PPC::DIR_970", "">;
def Directive32  : SubtargetFeature<"", "CPUDirective", "PPC::DIR_32", "">;
def Directive64  : SubtargetFeature<"", "CPUDirective", "PPC::DIR_64", "">;
def DirectiveA2  : SubtargetFeature<"", "CPUDirective", "PPC::DIR_A2", "">;
def DirectiveE500   : SubtargetFeature<"", "CPUDirective",
                                       "PPC::DIR_E500", "">;
def DirectiveE500mc : SubtargetFeature<"", "CPUDirective",
                                       "PPC::DIR_E500mc", "">;
def DirectiveE5500  : SubtargetFeature<"", "CPUDirective",
                                       "PPC::DIR_E5500", "">;
def DirectivePwr3: SubtargetFeature<"", "CPUDirective", "PPC::DIR_PWR3", "">;
def DirectivePwr4: SubtargetFeature<"", "CPUDirective", "PPC::DIR_PWR4", "">;
def DirectivePwr5: SubtargetFeature<"", "CPUDirective", "PPC::DIR_PWR5", "">;
def DirectivePwr5x
    : SubtargetFeature<"", "CPUDirective", "PPC::DIR_PWR5X", "">;
def DirectivePwr6: SubtargetFeature<"", "CPUDirective", "PPC::DIR_PWR6", "">;
def DirectivePwr6x
    : SubtargetFeature<"", "CPUDirective", "PPC::DIR_PWR6X", "">;
def DirectivePwr7: SubtargetFeature<"", "CPUDirective", "PPC::DIR_PWR7", "">;
def DirectivePwr8: SubtargetFeature<"", "CPUDirective", "PPC::DIR_PWR8", "">;
def DirectivePwr9: SubtargetFeature<"", "CPUDirective", "PPC::DIR_PWR9", "">;
def DirectivePwr10: SubtargetFeature<"", "CPUDirective", "PPC::DIR_PWR10", "">;
def DirectivePwr11: SubtargetFeature<"", "CPUDirective", "PPC::DIR_PWR11", "">;
def DirectivePwrFuture
    : SubtargetFeature<"", "CPUDirective", "PPC::DIR_PWR_FUTURE", "">;

// Specifies that the selected CPU supports 64-bit instructions, regardless of
// whether we are in 32-bit or 64-bit mode.
def Feature64Bit     : SubtargetFeature<"64bit","Has64BitSupport", "true",
                                        "Enable 64-bit instructions">;
def AIXOS: SubtargetFeature<"aix", "IsAIX", "true", "AIX OS">;
def FeatureModernAIXAs
    : SubtargetFeature<"modern-aix-as", "HasModernAIXAs", "true",
                       "AIX system assembler is modern enough to support new mnes">;
def FeatureHardFloat : SubtargetFeature<"hard-float", "HasHardFloat", "true",
                              "Enable floating-point instructions">;

// Specifies that we are in 64-bit mode or that we should use 64-bit registers
// in 32-bit mode when possible. Requires Feature64Bit to be enabled.
def Feature64BitRegs : SubtargetFeature<"64bitregs","Use64BitRegs", "true",
                              "Enable 64-bit registers usage for ppc32 [beta]">;

// Specify if we should store and manipulate i1 values in the individual
// condition register bits.
def FeatureCRBits    : SubtargetFeature<"crbits", "UseCRBits", "true",
                              "Use condition-register bits individually">;
def FeatureFPU       : SubtargetFeature<"fpu","HasFPU","true",
                                        "Enable classic FPU instructions",
                                        [FeatureHardFloat]>;
def FeatureAltivec   : SubtargetFeature<"altivec","HasAltivec", "true",
                                        "Enable Altivec instructions",
                                        [FeatureFPU]>;
def FeatureSPE       : SubtargetFeature<"spe","HasSPE", "true",
                                        "Enable SPE instructions",
                                        [FeatureHardFloat]>;
def FeatureEFPU2 : SubtargetFeature<"efpu2", "HasEFPU2", "true", 
                                        "Enable Embedded Floating-Point APU 2 instructions",
                                        [FeatureSPE]>;
def FeatureMFOCRF    : SubtargetFeature<"mfocrf","HasMFOCRF", "true",
                                        "Enable the MFOCRF instruction">;
def FeatureFSqrt     : SubtargetFeature<"fsqrt","HasFSQRT", "true",
                                        "Enable the fsqrt instruction",
                                        [FeatureFPU]>;
def FeatureFCPSGN    : SubtargetFeature<"fcpsgn", "HasFCPSGN", "true",
                                        "Enable the fcpsgn instruction",
                                        [FeatureFPU]>;
def FeatureFRE       : SubtargetFeature<"fre", "HasFRE", "true",
                                        "Enable the fre instruction",
                                        [FeatureFPU]>;
def FeatureFRES      : SubtargetFeature<"fres", "HasFRES", "true",
                                        "Enable the fres instruction",
                                        [FeatureFPU]>;
def FeatureFRSQRTE   : SubtargetFeature<"frsqrte", "HasFRSQRTE", "true",
                                        "Enable the frsqrte instruction",
                                        [FeatureFPU]>;
def FeatureFRSQRTES  : SubtargetFeature<"frsqrtes", "HasFRSQRTES", "true",
                                        "Enable the frsqrtes instruction",
                                        [FeatureFPU]>;
def FeatureRecipPrec : SubtargetFeature<"recipprec", "HasRecipPrec", "true",
                              "Assume higher precision reciprocal estimates">;
def FeatureSTFIWX    : SubtargetFeature<"stfiwx","HasSTFIWX", "true",
                                        "Enable the stfiwx instruction",
                                        [FeatureFPU]>;
def FeatureLFIWAX    : SubtargetFeature<"lfiwax","HasLFIWAX", "true",
                                        "Enable the lfiwax instruction",
                                        [FeatureFPU]>;
def FeatureFPRND     : SubtargetFeature<"fprnd", "HasFPRND", "true",
                                        "Enable the fri[mnpz] instructions",
                                        [FeatureFPU]>;
def FeatureFPCVT     : SubtargetFeature<"fpcvt", "HasFPCVT", "true",
  "Enable fc[ft]* (unsigned and single-precision) and lfiwzx instructions",
                                        [FeatureFPU]>;
def FeatureISEL      : SubtargetFeature<"isel","HasISEL", "true",
                                        "Enable the isel instruction">;
def FeatureBPERMD    : SubtargetFeature<"bpermd", "HasBPERMD", "true",
                                        "Enable the bpermd instruction">;
def FeatureExtDiv    : SubtargetFeature<"extdiv", "HasExtDiv", "true",
                                        "Enable extended divide instructions">;
def FeatureLDBRX     : SubtargetFeature<"ldbrx","HasLDBRX", "true",
                                        "Enable the ldbrx instruction">;
def FeatureCMPB      : SubtargetFeature<"cmpb", "HasCMPB", "true",
                                        "Enable the cmpb instruction">;
def FeatureICBT      : SubtargetFeature<"icbt","HasICBT", "true",
                                        "Enable icbt instruction">;
def FeatureBookE     : SubtargetFeature<"booke", "IsBookE", "true",
                                        "Enable Book E instructions",
                                        [FeatureICBT]>;
def FeatureMSYNC     : SubtargetFeature<"msync", "HasOnlyMSYNC", "true",
                              "Has only the msync instruction instead of sync",
                              [FeatureBookE]>;
def FeatureE500      : SubtargetFeature<"e500", "IsE500", "true",
                                        "Enable E500/E500mc instructions">;
def FeatureSecurePlt : SubtargetFeature<"secure-plt","IsSecurePlt", "true",
                                        "Enable secure plt mode">;
def FeaturePPC4xx    : SubtargetFeature<"ppc4xx", "IsPPC4xx", "true",
                                        "Enable PPC 4xx instructions">;
def FeaturePPC6xx    : SubtargetFeature<"ppc6xx", "IsPPC6xx", "true",
                                        "Enable PPC 6xx instructions">;
def FeatureVSX       : SubtargetFeature<"vsx","HasVSX", "true",
                                        "Enable VSX instructions",
                                        [FeatureAltivec]>;
def FeatureTwoConstNR :
  SubtargetFeature<"two-const-nr", "NeedsTwoConstNR", "true",
                   "Requires two constant Newton-Raphson computation">;
def FeatureP8Altivec : SubtargetFeature<"power8-altivec", "HasP8Altivec", "true",
                                        "Enable POWER8 Altivec instructions",
                                        [FeatureAltivec]>;
def FeatureP8Crypto : SubtargetFeature<"crypto", "HasP8Crypto", "true",
                                       "Enable POWER8 Crypto instructions",
                                       [FeatureP8Altivec]>;
def FeatureP8Vector  : SubtargetFeature<"power8-vector", "HasP8Vector", "true",
                                        "Enable POWER8 vector instructions",
                                        [FeatureVSX, FeatureP8Altivec]>;
def FeatureDirectMove :
  SubtargetFeature<"direct-move", "HasDirectMove", "true",
                   "Enable Power8 direct move instructions",
                   [FeatureVSX]>;
def FeaturePartwordAtomic : SubtargetFeature<"partword-atomics",
                                             "HasPartwordAtomics", "true",
                                             "Enable l[bh]arx and st[bh]cx.">;
def FeatureQuadwordAtomic : SubtargetFeature<"quadword-atomics",
                                             "HasQuadwordAtomics", "true",
                                             "Enable lqarx and stqcx.">;
def FeatureInvariantFunctionDescriptors :
  SubtargetFeature<"invariant-function-descriptors",
                   "HasInvariantFunctionDescriptors", "true",
                   "Assume function descriptors are invariant">;
def FeatureLongCall : SubtargetFeature<"longcall", "UseLongCalls", "true",
                                       "Always use indirect calls">;
def FeatureHTM : SubtargetFeature<"htm", "HasHTM", "true",
                                  "Enable Hardware Transactional Memory instructions">;
def FeatureMFTB   : SubtargetFeature<"", "IsFeatureMFTB", "true",
                                        "Implement mftb using the mfspr instruction">;
def FeatureFusion : SubtargetFeature<"fusion", "HasFusion", "true",
                                     "Target supports instruction fusion">;
def FeatureAddiLoadFusion : SubtargetFeature<"fuse-addi-load",
                                             "HasAddiLoadFusion", "true",
                                             "Power8 Addi-Load fusion",
                                             [FeatureFusion]>;
def FeatureAddisLoadFusion : SubtargetFeature<"fuse-addis-load",
                                              "HasAddisLoadFusion", "true",
                                              "Power8 Addis-Load fusion",
                                              [FeatureFusion]>;
def FeatureStoreFusion : SubtargetFeature<"fuse-store", "HasStoreFusion", "true",
                                          "Target supports store clustering",
                                          [FeatureFusion]>;
def FeatureArithAddFusion :
  SubtargetFeature<"fuse-arith-add", "HasArithAddFusion", "true",
                   "Target supports Arithmetic Operations with Add fusion",
                   [FeatureFusion]>;
def FeatureAddLogicalFusion :
  SubtargetFeature<"fuse-add-logical", "HasAddLogicalFusion", "true",
                   "Target supports Add with Logical Operations fusion",
                   [FeatureFusion]>;
def FeatureLogicalAddFusion :
  SubtargetFeature<"fuse-logical-add", "HasLogicalAddFusion", "true",
                   "Target supports Logical with Add Operations fusion",
                   [FeatureFusion]>;
def FeatureLogicalFusion :
  SubtargetFeature<"fuse-logical", "HasLogicalFusion", "true",
                   "Target supports Logical Operations fusion",
                   [FeatureFusion]>;
def FeatureSha3Fusion :
  SubtargetFeature<"fuse-sha3", "HasSha3Fusion", "true",
                   "Target supports SHA3 assist fusion",
                   [FeatureFusion]>;
def FeatureCompareFusion:
  SubtargetFeature<"fuse-cmp", "HasCompareFusion", "true",
                   "Target supports Comparison Operations fusion",
                   [FeatureFusion]>;
def FeatureWideImmFusion:
  SubtargetFeature<"fuse-wideimm", "HasWideImmFusion", "true",
                   "Target supports Wide-Immediate fusion",
                   [FeatureFusion]>;
def FeatureZeroMoveFusion:
  SubtargetFeature<"fuse-zeromove", "HasZeroMoveFusion", "true",
                   "Target supports move to SPR with branch fusion",
                   [FeatureFusion]>;
def FeatureBack2BackFusion:
  SubtargetFeature<"fuse-back2back", "HasBack2BackFusion", "true",
                   "Target supports general back to back fusion",
                   [FeatureFusion]>;
def FeatureUnalignedFloats :
  SubtargetFeature<"allow-unaligned-fp-access", "AllowsUnalignedFPAccess",
                   "true", "CPU does not trap on unaligned FP access">;
def FeaturePPCPreRASched:
  SubtargetFeature<"ppc-prera-sched", "UsePPCPreRASchedStrategy", "true",
                   "Use PowerPC pre-RA scheduling strategy">;
def FeaturePPCPostRASched:
  SubtargetFeature<"ppc-postra-sched", "UsePPCPostRASchedStrategy", "true",
                   "Use PowerPC post-RA scheduling strategy">;
def FeatureFloat128 :
  SubtargetFeature<"float128", "HasFloat128", "true",
                   "Enable the __float128 data type for IEEE-754R Binary128.",
                   [FeatureVSX]>;
def FeaturePOPCNTD   : SubtargetFeature<"popcntd","HasPOPCNTD",
                                        "POPCNTD_Fast",
                                        "Enable the popcnt[dw] instructions">;
// Note that for the a2 processor models we should not use popcnt[dw] by
// default. These processors do support the instructions, but they're
// microcoded, and the software emulation is about twice as fast.
def FeatureSlowPOPCNTD : SubtargetFeature<"slow-popcntd","HasPOPCNTD",
                                          "POPCNTD_Slow",
                                          "Has slow popcnt[dw] instructions">;

def DeprecatedDST    : SubtargetFeature<"", "IsDeprecatedDST", "true",
  "Treat vector data stream cache control instructions as deprecated">;

def FeatureISA2_06 : SubtargetFeature<"isa-v206-instructions", "IsISA2_06",
                                      "true",
                                      "Enable instructions in ISA 2.06.">;
def FeatureISA2_07 : SubtargetFeature<"isa-v207-instructions", "IsISA2_07",
                                      "true",
                                      "Enable instructions in ISA 2.07.">;
def FeatureISA3_0 : SubtargetFeature<"isa-v30-instructions", "IsISA3_0",
                                     "true",
                                     "Enable instructions in ISA 3.0.",
                                     [FeatureISA2_07]>;
def FeatureISA3_1 : SubtargetFeature<"isa-v31-instructions", "IsISA3_1",
                                     "true",
                                     "Enable instructions in ISA 3.1.",
                                     [FeatureISA3_0]>;
def FeatureISAFuture : SubtargetFeature<"isa-future-instructions",
                                        "IsISAFuture", "true",
                                        "Enable instructions for Future ISA.",
                                        [FeatureISA3_1]>;
def FeatureP9Altivec : SubtargetFeature<"power9-altivec", "HasP9Altivec", "true",
                                        "Enable POWER9 Altivec instructions",
                                        [FeatureISA3_0, FeatureP8Altivec]>;
def FeatureP9Vector  : SubtargetFeature<"power9-vector", "HasP9Vector", "true",
                                        "Enable POWER9 vector instructions",
                                        [FeatureISA3_0, FeatureP8Vector,
                                         FeatureP9Altivec]>;
def FeatureP10Vector  : SubtargetFeature<"power10-vector", "HasP10Vector",
                                         "true",
                                         "Enable POWER10 vector instructions",
                                         [FeatureISA3_1, FeatureP9Vector]>;
// A separate feature for this even though it is equivalent to P9Vector
// because this is a feature of the implementation rather than the architecture
// and may go away with future CPU's.
def FeatureVectorsUseTwoUnits : SubtargetFeature<"vectors-use-two-units",
                                                 "VectorsUseTwoUnits",
                                                 "true",
                                                 "Vectors use two units">;
def FeaturePrefixInstrs : SubtargetFeature<"prefix-instrs", "HasPrefixInstrs",
                                           "true",
                                           "Enable prefixed instructions",
                                           [FeatureISA3_1]>;
def FeaturePCRelativeMemops :
  SubtargetFeature<"pcrelative-memops", "HasPCRelativeMemops", "true",
                   "Enable PC relative Memory Ops",
                   [FeatureISA3_0, FeaturePrefixInstrs]>;
def FeaturePairedVectorMemops:
  SubtargetFeature<"paired-vector-memops", "PairedVectorMemops", "true",
                   "32Byte load and store instructions",
                   [FeatureISA3_0]>;
def FeatureMMA : SubtargetFeature<"mma", "HasMMA", "true",
                                  "Enable MMA instructions",
                                  [FeatureP8Vector, FeatureP9Altivec,
                                   FeaturePairedVectorMemops]>;
def FeatureROPProtect :
  SubtargetFeature<"rop-protect", "HasROPProtect", "true",
                   "Add ROP protect">;

def FeaturePrivileged :
  SubtargetFeature<"privileged", "HasPrivileged", "true",
                   "Add privileged instructions">;

// Specifies that local-exec TLS accesses in any function with this target
// attribute should use the optimized TOC-free sequence (where the offset is an
// immediate off of R13 for which the linker might add fix-up code if the
// immediate is too large).
// Clearly, this isn't really a feature of the subtarget, but is used as a
// convenient way to affect code generation for individual functions.
def FeatureAIXLocalExecTLS :
  SubtargetFeature<"aix-small-local-exec-tls", "HasAIXSmallLocalExecTLS", "true",
                   "Produce a TOC-free local-exec TLS sequence for this function "
                   "for 64-bit AIX">;

// Specifies that local-dynamic TLS accesses in any function with this target
// attribute should use the optimized sequence (where the offset is an immediate
// off the module-handle for which the linker might add fix-up code for if the
// immediate is too large).
def FeatureAIXLocalDynamicTLS :
  SubtargetFeature<"aix-small-local-dynamic-tls", "HasAIXSmallLocalDynamicTLS",
                   "true", "Produce a faster local-dynamic TLS sequence for this "
                   "function for 64-bit AIX">;

def FeatureAIXSharedLibTLSModelOpt :
  SubtargetFeature<"aix-shared-lib-tls-model-opt",
                   "HasAIXShLibTLSModelOpt", "true",
                   "Tune TLS model at function level in shared library loaded "
                   "with the main program (for 64-bit AIX only)">;

def FeaturePredictableSelectIsExpensive :
  SubtargetFeature<"predictable-select-expensive",
                   "PredictableSelectIsExpensive",
                   "true",
                   "Prefer likely predicted branches over selects">;

def FeatureFastMFLR : SubtargetFeature<"fast-MFLR", "HasFastMFLR", "true",
                                       "MFLR is a fast instruction">;

// Since new processors generally contain a superset of features of those that
// came before them, the idea is to make implementations of new processors
// less error prone and easier to read.
// Namely:
//     list<SubtargetFeature> P8InheritableFeatures = ...
//     list<SubtargetFeature> FutureProcessorAddtionalFeatures =
//         [ features that Power8 does not support but inheritable ]
//     list<SubtargetFeature> FutureProcessorSpecificFeatures =
//         [ features that Power8 does not support and not inheritable ]
//     list<SubtargetFeature> FutureProcessorInheritableFeatures =
//         !listconcat(P8InheritableFeatures, FutureProcessorAddtionalFeatures)
//     list<SubtargetFeature> FutureProcessorFeatures =
//         !listconcat(FutureProcessorInheritableFeatures,
//                     FutureProcessorSpecificFeatures)

// Makes it explicit and obvious what is new in FutureProcessor vs. Power8 as
// well as providing a single point of definition if the feature set will be
// used elsewhere.
def ProcessorFeatures {
  // Power7
  list<SubtargetFeature> P7InheritableFeatures = [DirectivePwr7,
                                                  FeatureAltivec,
                                                  FeatureVSX,
                                                  FeatureMFOCRF,
                                                  FeatureFCPSGN,
                                                  FeatureFSqrt,
                                                  FeatureFRE,
                                                  FeatureFRES,
                                                  FeatureFRSQRTE,
                                                  FeatureFRSQRTES,
                                                  FeatureRecipPrec,
                                                  FeatureSTFIWX,
                                                  FeatureLFIWAX,
                                                  FeatureFPRND,
                                                  FeatureFPCVT,
                                                  FeatureISEL,
                                                  FeaturePOPCNTD,
                                                  FeatureCMPB,
                                                  FeatureLDBRX,
                                                  Feature64Bit,
                                                  /* Feature64BitRegs, */
                                                  FeatureBPERMD,
                                                  FeatureExtDiv,
                                                  FeatureMFTB,
                                                  DeprecatedDST,
                                                  FeatureTwoConstNR,
                                                  FeatureUnalignedFloats,
                                                  FeatureISA2_06];
  list<SubtargetFeature> P7SpecificFeatures = [];
  list<SubtargetFeature> P7Features =
    !listconcat(P7InheritableFeatures, P7SpecificFeatures);

  // Power8
  list<SubtargetFeature> P8AdditionalFeatures =
    [DirectivePwr8,
     FeatureP8Altivec,
     FeatureP8Vector,
     FeatureP8Crypto,
     FeatureHTM,
     FeatureDirectMove,
     FeatureICBT,
     FeaturePartwordAtomic,
     FeatureQuadwordAtomic,
     FeaturePredictableSelectIsExpensive,
     FeatureISA2_07,
     FeatureCRBits
    ];

  list<SubtargetFeature> P8SpecificFeatures = [FeatureAddiLoadFusion,
                                               FeatureAddisLoadFusion];
  list<SubtargetFeature> P8InheritableFeatures =
    !listconcat(P7InheritableFeatures, P8AdditionalFeatures);
  list<SubtargetFeature> P8Features =
    !listconcat(P8InheritableFeatures, P8SpecificFeatures);

  // Power9
  list<SubtargetFeature> P9AdditionalFeatures =
    [DirectivePwr9,
     FeatureP9Altivec,
     FeatureP9Vector,
     FeaturePPCPreRASched,
     FeaturePPCPostRASched,
     FeatureISA3_0,
     FeaturePredictableSelectIsExpensive
    ];

  // Some features are unique to Power9 and there is no reason to assume
  // they will be part of any future CPUs. One example is the narrower
  // dispatch for vector operations than scalar ones. For the time being,
  // this list also includes scheduling-related features since we do not have
  // enough info to create custom scheduling strategies for future CPUs.
  list<SubtargetFeature> P9SpecificFeatures = [FeatureVectorsUseTwoUnits];
  list<SubtargetFeature> P9InheritableFeatures =
    !listconcat(P8InheritableFeatures, P9AdditionalFeatures);
  list<SubtargetFeature> P9Features =
    !listconcat(P9InheritableFeatures, P9SpecificFeatures);

  // Power10
  // For P10 CPU we assume that all of the existing features from Power9
  // still exist with the exception of those we know are Power9 specific.
  list<SubtargetFeature> FusionFeatures = [
    FeatureStoreFusion, FeatureAddLogicalFusion, FeatureLogicalAddFusion,
    FeatureLogicalFusion, FeatureArithAddFusion, FeatureSha3Fusion,
  ];
  list<SubtargetFeature> P10AdditionalFeatures =
    !listconcat(FusionFeatures, [
       DirectivePwr10, FeatureISA3_1, FeaturePrefixInstrs,
       FeaturePCRelativeMemops, FeatureP10Vector, FeatureMMA,
       FeaturePairedVectorMemops, FeatureFastMFLR]);
  list<SubtargetFeature> P10SpecificFeatures = [];
  list<SubtargetFeature> P10InheritableFeatures =
    !listconcat(P9InheritableFeatures, P10AdditionalFeatures);
  list<SubtargetFeature> P10Features =
    !listconcat(P10InheritableFeatures, P10SpecificFeatures);

  // Power11
  // For P11 CPU we assume that all the existing features from Power10
  // still exist with the exception of those we know are Power10 specific.
  list<SubtargetFeature> P11AdditionalFeatures =
    [DirectivePwr11];
  list<SubtargetFeature> P11SpecificFeatures =
    [];
  list<SubtargetFeature> P11InheritableFeatures =
    !listconcat(P10InheritableFeatures, P11AdditionalFeatures);
  list<SubtargetFeature> P11Features =
    !listconcat(P11InheritableFeatures, P11SpecificFeatures);

  // Future
  // For future CPU we assume that all of the existing features from Power11
  // still exist with the exception of those we know are Power11 specific.
  list<SubtargetFeature> FutureAdditionalFeatures = [FeatureISAFuture];
  list<SubtargetFeature> FutureSpecificFeatures = [];
  list<SubtargetFeature> FutureInheritableFeatures =
    !listconcat(P11InheritableFeatures, FutureAdditionalFeatures);
  list<SubtargetFeature> FutureFeatures =
    !listconcat(FutureInheritableFeatures, FutureSpecificFeatures);
}

// Note: Future features to add when support is extended to more
// recent ISA levels:
//
// DFP          p6, p6x, p7        decimal floating-point instructions
// POPCNTB      p5 through p7      popcntb and related instructions

//===----------------------------------------------------------------------===//
// Classes used for relation maps.
//===----------------------------------------------------------------------===//
// RecFormRel - Filter class used to relate non-record-form instructions with
// their record-form variants.
class RecFormRel;

// AltVSXFMARel - Filter class used to relate the primary addend-killing VSX
// FMA instruction forms with their corresponding factor-killing forms.
class AltVSXFMARel {
  bit IsVSXFMAAlt = 0;
}

//===----------------------------------------------------------------------===//
// Relation Map Definitions.
//===----------------------------------------------------------------------===//

def getRecordFormOpcode : InstrMapping {
  let FilterClass = "RecFormRel";
  // Instructions with the same BaseName and Interpretation64Bit values
  // form a row.
  let RowFields = ["BaseName", "Interpretation64Bit"];
  // Instructions with the same RC value form a column.
  let ColFields = ["RC"];
  // The key column are the non-record-form instructions.
  let KeyCol = ["0"];
  // Value columns RC=1
  let ValueCols = [["1"]];
}

def getNonRecordFormOpcode : InstrMapping {
  let FilterClass = "RecFormRel";
  // Instructions with the same BaseName and Interpretation64Bit values
  // form a row.
  let RowFields = ["BaseName", "Interpretation64Bit"];
  // Instructions with the same RC value form a column.
  let ColFields = ["RC"];
  // The key column are the record-form instructions.
  let KeyCol = ["1"];
  // Value columns are RC=0
  let ValueCols = [["0"]];
}

def getAltVSXFMAOpcode : InstrMapping {
  let FilterClass = "AltVSXFMARel";
  // Instructions with the same BaseName value form a row.
  let RowFields = ["BaseName"];
  // Instructions with the same IsVSXFMAAlt value form a column.
  let ColFields = ["IsVSXFMAAlt"];
  // The key column are the (default) addend-killing instructions.
  let KeyCol = ["0"];
  // Value columns IsVSXFMAAlt=1
  let ValueCols = [["1"]];
}

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

include "PPCRegisterInfo.td"
include "PPCSchedule.td"
include "GISel/PPCRegisterBanks.td"

//===----------------------------------------------------------------------===//
// PowerPC processors supported.
//

def : Processor<"generic", G3Itineraries, [Directive32, FeatureHardFloat,
                                           FeatureMFTB]>;
def : ProcessorModel<"440", PPC440Model, [Directive440, FeatureISEL,
                                          FeatureFRES, FeatureFRSQRTE,
                                          FeatureICBT, FeatureBookE,
                                          FeatureMSYNC, FeatureMFTB]>;
def : ProcessorModel<"450", PPC440Model, [Directive440, FeatureISEL,
                                          FeatureFRES, FeatureFRSQRTE,
                                          FeatureICBT, FeatureBookE,
                                          FeatureMSYNC, FeatureMFTB]>;
def : Processor<"601", G3Itineraries, [Directive601, FeatureFPU]>;
def : Processor<"602", G3Itineraries, [Directive602, FeatureFPU,
                                       FeatureMFTB]>;
def : Processor<"603", G3Itineraries, [Directive603,
                                       FeatureFRES, FeatureFRSQRTE,
                                       FeatureMFTB]>;
def : Processor<"603e", G3Itineraries, [Directive603,
                                        FeatureFRES, FeatureFRSQRTE,
                                        FeatureMFTB]>;
def : Processor<"603ev", G3Itineraries, [Directive603,
                                         FeatureFRES, FeatureFRSQRTE,
                                         FeatureMFTB]>;
def : Processor<"604", G3Itineraries, [Directive604,
                                       FeatureFRES, FeatureFRSQRTE,
                                       FeatureMFTB]>;
def : Processor<"604e", G3Itineraries, [Directive604,
                                        FeatureFRES, FeatureFRSQRTE,
                                        FeatureMFTB]>;
def : Processor<"620", G3Itineraries, [Directive620,
                                       FeatureFRES, FeatureFRSQRTE,
                                       FeatureMFTB]>;
def : Processor<"750", G4Itineraries, [Directive750,
                                       FeatureFRES, FeatureFRSQRTE,
                                       FeatureMFTB]>;
def : Processor<"g3", G3Itineraries, [Directive750,
                                      FeatureFRES, FeatureFRSQRTE,
                                      FeatureMFTB]>;
def : Processor<"7400", G4Itineraries, [Directive7400, FeatureAltivec,
                                        FeatureFRES, FeatureFRSQRTE,
                                        FeatureMFTB]>;
def : Processor<"g4", G4Itineraries, [Directive7400, FeatureAltivec,
                                      FeatureFRES, FeatureFRSQRTE,
                                      FeatureMFTB]>;
def : Processor<"7450", G4PlusItineraries, [Directive7400, FeatureAltivec,
                                            FeatureFRES, FeatureFRSQRTE,
                                            FeatureMFTB]>;
def : Processor<"g4+", G4PlusItineraries, [Directive7400, FeatureAltivec,
                                           FeatureFRES, FeatureFRSQRTE,
                                           FeatureMFTB]>;

def : ProcessorModel<"970", G5Model,
                  [Directive970, FeatureAltivec,
                   FeatureMFOCRF, FeatureFSqrt,
                   FeatureFRES, FeatureFRSQRTE, FeatureSTFIWX,
                   Feature64Bit /*, Feature64BitRegs */,
                   FeatureMFTB]>;
def : ProcessorModel<"g5", G5Model,
                  [Directive970, FeatureAltivec,
                   FeatureMFOCRF, FeatureFSqrt, FeatureSTFIWX,
                   FeatureFRES, FeatureFRSQRTE,
                   Feature64Bit /*, Feature64BitRegs */,
                   FeatureMFTB, DeprecatedDST]>;
def : ProcessorModel<"e500", PPCE500Model,
                  [DirectiveE500,
                   FeatureICBT, FeatureBookE,
                   FeatureISEL, FeatureMFTB, FeatureMSYNC, FeatureSPE]>;
def : ProcessorModel<"e500mc", PPCE500mcModel,
                  [DirectiveE500mc,
                   FeatureSTFIWX, FeatureICBT, FeatureBookE,
                   FeatureISEL, FeatureMFTB]>;
def : ProcessorModel<"e5500", PPCE5500Model,
                  [DirectiveE5500, FeatureMFOCRF, Feature64Bit,
                   FeatureSTFIWX, FeatureICBT, FeatureBookE,
                   FeatureISEL, FeatureMFTB]>;
def : ProcessorModel<"a2", PPCA2Model,
                  [DirectiveA2, FeatureICBT, FeatureBookE, FeatureMFOCRF,
                   FeatureFCPSGN, FeatureFSqrt, FeatureFRE, FeatureFRES,
                   FeatureFRSQRTE, FeatureFRSQRTES, FeatureRecipPrec,
                   FeatureSTFIWX, FeatureLFIWAX,
                   FeatureFPRND, FeatureFPCVT, FeatureISEL,
                   FeatureSlowPOPCNTD, FeatureCMPB, FeatureLDBRX,
                   Feature64Bit /*, Feature64BitRegs */, FeatureMFTB,
                   FeatureISA2_06]>;
def : ProcessorModel<"pwr3", G5Model,
                  [DirectivePwr3, FeatureAltivec,
                   FeatureFRES, FeatureFRSQRTE, FeatureMFOCRF,
                   FeatureSTFIWX, Feature64Bit]>;
def : ProcessorModel<"pwr4", G5Model,
                  [DirectivePwr4, FeatureAltivec, FeatureMFOCRF,
                   FeatureFSqrt, FeatureFRES, FeatureFRSQRTE,
                   FeatureSTFIWX, Feature64Bit, FeatureMFTB]>;
def : ProcessorModel<"pwr5", G5Model,
                  [DirectivePwr5, FeatureAltivec, FeatureMFOCRF,
                   FeatureFSqrt, FeatureFRE, FeatureFRES,
                   FeatureFRSQRTE, FeatureFRSQRTES,
                   FeatureSTFIWX, Feature64Bit,
                   FeatureMFTB, DeprecatedDST]>;
def : ProcessorModel<"pwr5x", G5Model,
                  [DirectivePwr5x, FeatureAltivec, FeatureMFOCRF,
                   FeatureFSqrt, FeatureFRE, FeatureFRES,
                   FeatureFRSQRTE, FeatureFRSQRTES,
                   FeatureSTFIWX, FeatureFPRND, Feature64Bit,
                   FeatureMFTB, DeprecatedDST]>;
def : ProcessorModel<"pwr6", G5Model,
                  [DirectivePwr6, FeatureAltivec,
                   FeatureMFOCRF, FeatureFCPSGN, FeatureFSqrt, FeatureFRE,
                   FeatureFRES, FeatureFRSQRTE, FeatureFRSQRTES,
                   FeatureRecipPrec, FeatureSTFIWX, FeatureLFIWAX, FeatureCMPB,
                   FeatureFPRND, Feature64Bit /*, Feature64BitRegs */,
                   FeatureMFTB, DeprecatedDST]>;
def : ProcessorModel<"pwr6x", G5Model,
                  [DirectivePwr5x, FeatureAltivec, FeatureMFOCRF,
                   FeatureFCPSGN, FeatureFSqrt, FeatureFRE, FeatureFRES,
                   FeatureFRSQRTE, FeatureFRSQRTES, FeatureRecipPrec,
                   FeatureSTFIWX, FeatureLFIWAX, FeatureCMPB,
                   FeatureFPRND, Feature64Bit,
                   FeatureMFTB, DeprecatedDST]>;
def : ProcessorModel<"pwr7", P7Model, ProcessorFeatures.P7Features>;
def : ProcessorModel<"pwr8", P8Model, ProcessorFeatures.P8Features>;
def : ProcessorModel<"pwr9", P9Model, ProcessorFeatures.P9Features>;
def : ProcessorModel<"pwr10", P10Model, ProcessorFeatures.P10Features>;
def : ProcessorModel<"pwr11", P10Model, ProcessorFeatures.P11Features>;
// No scheduler model for future CPU.
def : ProcessorModel<"future", NoSchedModel,
                  ProcessorFeatures.FutureFeatures>;
def : Processor<"ppc", G3Itineraries, [Directive32, FeatureHardFloat,
                                       FeatureMFTB]>;
def : Processor<"ppc32", G3Itineraries, [Directive32, FeatureHardFloat,
                                         FeatureMFTB]>;
def : ProcessorModel<"ppc64", G5Model,
                  [Directive64, FeatureAltivec,
                   FeatureMFOCRF, FeatureFSqrt, FeatureFRES,
                   FeatureFRSQRTE, FeatureSTFIWX,
                   Feature64Bit /*, Feature64BitRegs */,
                   FeatureMFTB]>;
def : ProcessorModel<"ppc64le", P8Model, ProcessorFeatures.P8Features>;

//===----------------------------------------------------------------------===//
// Calling Conventions
//===----------------------------------------------------------------------===//

include "PPCCallingConv.td"

def PPCInstrInfo : InstrInfo {
  let isLittleEndianEncoding = 1;
}

def PPCAsmWriter : AsmWriter {
  string AsmWriterClassName  = "InstPrinter";
  int PassSubtarget = 1;
  int Variant = 0;
  bit isMCAsmWriter = 1;
}

def PPCAsmParser : AsmParser {
  let ShouldEmitMatchRegisterName = 0;
}

def PPCAsmParserVariant : AsmParserVariant {
  int Variant = 0;

  // We do not use hard coded registers in asm strings.  However, some
  // InstAlias definitions use immediate literals.  Set RegisterPrefix
  // so that those are not misinterpreted as registers.
  string RegisterPrefix = "%";
  string BreakCharacters = ".";
}

def PPC : Target {
  // Information about the instructions.
  let InstructionSet = PPCInstrInfo;

  let AssemblyWriters = [PPCAsmWriter];
  let AssemblyParsers = [PPCAsmParser];
  let AssemblyParserVariants = [PPCAsmParserVariant];
  let AllowRegisterRenaming = 1;
}

//===----------------------------------------------------------------------===//
// Pfm Counters
//===----------------------------------------------------------------------===//

include "PPCPfmCounters.td"
