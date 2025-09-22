class ProcNoItin<string Name, list<SubtargetFeature> Features>
  : Processor<Name, NoItineraries, Features>;

//===----------------------------------------------------------------------===//
// ARM Processor subtarget features.
//

def ProcA5      : SubtargetFeature<"a5", "ARMProcFamily", "CortexA5",
                                   "Cortex-A5 ARM processors", []>;
def ProcA7      : SubtargetFeature<"a7", "ARMProcFamily", "CortexA7",
                                   "Cortex-A7 ARM processors", []>;
def ProcA8      : SubtargetFeature<"a8", "ARMProcFamily", "CortexA8",
                                   "Cortex-A8 ARM processors", []>;
def ProcA9      : SubtargetFeature<"a9", "ARMProcFamily", "CortexA9",
                                   "Cortex-A9 ARM processors", []>;
def ProcA12     : SubtargetFeature<"a12", "ARMProcFamily", "CortexA12",
                                   "Cortex-A12 ARM processors", []>;
def ProcA15     : SubtargetFeature<"a15", "ARMProcFamily", "CortexA15",
                                   "Cortex-A15 ARM processors", []>;
def ProcA17     : SubtargetFeature<"a17", "ARMProcFamily", "CortexA17",
                                   "Cortex-A17 ARM processors", []>;
def ProcA32     : SubtargetFeature<"a32", "ARMProcFamily", "CortexA32",
                                   "Cortex-A32 ARM processors", []>;
def ProcA35     : SubtargetFeature<"a35", "ARMProcFamily", "CortexA35",
                                   "Cortex-A35 ARM processors", []>;
def ProcA53     : SubtargetFeature<"a53", "ARMProcFamily", "CortexA53",
                                   "Cortex-A53 ARM processors", []>;
def ProcA55     : SubtargetFeature<"a55", "ARMProcFamily", "CortexA55",
                                   "Cortex-A55 ARM processors", []>;
def ProcA57     : SubtargetFeature<"a57", "ARMProcFamily", "CortexA57",
                                   "Cortex-A57 ARM processors", []>;
def ProcA72     : SubtargetFeature<"a72", "ARMProcFamily", "CortexA72",
                                   "Cortex-A72 ARM processors", []>;
def ProcA73     : SubtargetFeature<"a73", "ARMProcFamily", "CortexA73",
                                   "Cortex-A73 ARM processors", []>;
def ProcA75     : SubtargetFeature<"a75", "ARMProcFamily", "CortexA75",
                                   "Cortex-A75 ARM processors", []>;
def ProcA76     : SubtargetFeature<"a76", "ARMProcFamily", "CortexA76",
                                   "Cortex-A76 ARM processors", []>;
def ProcA77     : SubtargetFeature<"a77", "ARMProcFamily", "CortexA77",
                                   "Cortex-A77 ARM processors", []>;
def ProcA78     : SubtargetFeature<"cortex-a78", "ARMProcFamily", "CortexA78",
                                   "Cortex-A78 ARM processors", []>;
def ProcA78AE   : SubtargetFeature<"cortex-a78ae", "ARMProcFamily", "CortexA78AE",
                                   "Cortex-A78AE ARM processors", []>;
def ProcA78C    : SubtargetFeature<"a78c", "ARMProcFamily", "CortexA78C",
                                   "Cortex-A78C ARM processors", []>;
def ProcA710    : SubtargetFeature<"cortex-a710", "ARMProcFamily",
                                   "CortexA710", "Cortex-A710 ARM processors", []>;
def ProcX1      : SubtargetFeature<"cortex-x1", "ARMProcFamily", "CortexX1",
                                   "Cortex-X1 ARM processors", []>;
def ProcX1C     : SubtargetFeature<"cortex-x1c", "ARMProcFamily", "CortexX1C",
                                   "Cortex-X1C ARM processors", []>;

def ProcV1      : SubtargetFeature<"neoverse-v1", "ARMProcFamily",
                                   "NeoverseV1", "Neoverse-V1 ARM processors", []>;

def ProcKrait   : SubtargetFeature<"krait", "ARMProcFamily", "Krait",
                                   "Qualcomm Krait processors", []>;
def ProcKryo    : SubtargetFeature<"kryo", "ARMProcFamily", "Kryo",
                                   "Qualcomm Kryo processors", []>;
def ProcSwift   : SubtargetFeature<"swift", "ARMProcFamily", "Swift",
                                   "Swift ARM processors", []>;

def ProcExynos  : SubtargetFeature<"exynos", "ARMProcFamily", "Exynos",
                                   "Samsung Exynos processors",
                                   [FeatureZCZeroing,
                                    FeatureUseWideStrideVFP,
                                    FeatureSplatVFPToNeon,
                                    FeatureSlowVGETLNi32,
                                    FeatureSlowVDUP32,
                                    FeatureSlowFPBrcc,
                                    FeatureProfUnpredicate,
                                    FeatureHWDivThumb,
                                    FeatureHWDivARM,
                                    FeatureHasSlowFPVMLx,
                                    FeatureHasSlowFPVFMx,
                                    FeatureHasRetAddrStack,
                                    FeatureFuseLiterals,
                                    FeatureFuseAES,
                                    FeatureExpandMLx,
                                    FeatureCrypto,
                                    FeatureCRC]>;

def ProcR4      : SubtargetFeature<"r4", "ARMProcFamily", "CortexR4",
                                   "Cortex-R4 ARM processors", []>;
def ProcR5      : SubtargetFeature<"r5", "ARMProcFamily", "CortexR5",
                                   "Cortex-R5 ARM processors", []>;
def ProcR7      : SubtargetFeature<"r7", "ARMProcFamily", "CortexR7",
                                   "Cortex-R7 ARM processors", []>;
def ProcR52     : SubtargetFeature<"r52", "ARMProcFamily", "CortexR52",
                                   "Cortex-R52 ARM processors", []>;
def ProcR52plus  : SubtargetFeature<"r52plus", "ARMProcFamily", "CortexR52plus",
                                   "Cortex-R52plus ARM processors", []>;

def ProcM3      : SubtargetFeature<"m3", "ARMProcFamily", "CortexM3",
                                   "Cortex-M3 ARM processors", []>;
def ProcM7      : SubtargetFeature<"m7", "ARMProcFamily", "CortexM7",
                                   "Cortex-M7 ARM processors", []>;

//===----------------------------------------------------------------------===//
// ARM processors
//
// Dummy CPU, used to target architectures
def : ProcessorModel<"generic",     CortexA8Model,      []>;

// FIXME: Several processors below are not using their own scheduler
// model, but one of similar/previous processor. These should be fixed.

def : ProcNoItin<"arm8",                                [ARMv4]>;
def : ProcNoItin<"arm810",                              [ARMv4]>;
def : ProcNoItin<"strongarm",                           [ARMv4]>;
def : ProcNoItin<"strongarm110",                        [ARMv4]>;
def : ProcNoItin<"strongarm1100",                       [ARMv4]>;
def : ProcNoItin<"strongarm1110",                       [ARMv4]>;

def : ProcNoItin<"arm7tdmi",                            [ARMv4t]>;
def : ProcNoItin<"arm7tdmi-s",                          [ARMv4t]>;
def : ProcNoItin<"arm710t",                             [ARMv4t]>;
def : ProcNoItin<"arm720t",                             [ARMv4t]>;
def : ProcNoItin<"arm9",                                [ARMv4t]>;
def : ProcNoItin<"arm9tdmi",                            [ARMv4t]>;
def : ProcNoItin<"arm920",                              [ARMv4t]>;
def : ProcNoItin<"arm920t",                             [ARMv4t]>;
def : ProcNoItin<"arm922t",                             [ARMv4t]>;
def : ProcNoItin<"arm940t",                             [ARMv4t]>;
def : ProcNoItin<"ep9312",                              [ARMv4t]>;

def : ProcNoItin<"arm10tdmi",                           [ARMv5t]>;
def : ProcNoItin<"arm1020t",                            [ARMv5t]>;

def : ProcNoItin<"arm9e",                               [ARMv5te]>;
def : ProcNoItin<"arm926ej-s",                          [ARMv5te]>;
def : ProcNoItin<"arm946e-s",                           [ARMv5te]>;
def : ProcNoItin<"arm966e-s",                           [ARMv5te]>;
def : ProcNoItin<"arm968e-s",                           [ARMv5te]>;
def : ProcNoItin<"arm10e",                              [ARMv5te]>;
def : ProcNoItin<"arm1020e",                            [ARMv5te]>;
def : ProcNoItin<"arm1022e",                            [ARMv5te]>;
def : ProcNoItin<"xscale",                              [ARMv5te]>;
def : ProcNoItin<"iwmmxt",                              [ARMv5te]>;

def : Processor<"arm1136j-s",       ARMV6Itineraries,   [ARMv6]>;
def : Processor<"arm1136jf-s",      ARMV6Itineraries,   [ARMv6,
                                                         FeatureVFP2,
                                                         FeatureHasSlowFPVMLx]>;

def : Processor<"cortex-m0",        ARMV6Itineraries,   [ARMv6m,
                                                         FeatureHasNoBranchPredictor]>;
def : Processor<"cortex-m0plus",    ARMV6Itineraries,   [ARMv6m,
                                                         FeatureHasNoBranchPredictor]>;
def : Processor<"cortex-m1",        ARMV6Itineraries,   [ARMv6m,
                                                         FeatureHasNoBranchPredictor]>;
def : Processor<"sc000",            ARMV6Itineraries,   [ARMv6m,
                                                         FeatureHasNoBranchPredictor]>;

def : Processor<"arm1176jz-s",      ARMV6Itineraries,   [ARMv6kz]>;
def : Processor<"arm1176jzf-s",     ARMV6Itineraries,   [ARMv6kz,
                                                         FeatureVFP2,
                                                         FeatureHasSlowFPVMLx]>;

def : Processor<"mpcorenovfp",      ARMV6Itineraries,   [ARMv6k]>;
def : Processor<"mpcore",           ARMV6Itineraries,   [ARMv6k,
                                                         FeatureVFP2,
                                                         FeatureHasSlowFPVMLx]>;

def : Processor<"arm1156t2-s",      ARMV6Itineraries,   [ARMv6t2]>;
def : Processor<"arm1156t2f-s",     ARMV6Itineraries,   [ARMv6t2,
                                                         FeatureVFP2,
                                                         FeatureHasSlowFPVMLx]>;

def : ProcessorModel<"cortex-a5",   CortexA8Model,      [ARMv7a, ProcA5,
                                                         FeatureHasRetAddrStack,
                                                         FeatureTrustZone,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasSlowFPVFMx,
                                                         FeatureVMLxForwarding,
                                                         FeatureMP,
                                                         FeatureVFP4]>;

def : ProcessorModel<"cortex-a7",   CortexA8Model,      [ARMv7a, ProcA7,
                                                         FeatureHasRetAddrStack,
                                                         FeatureTrustZone,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHasVMLxHazards,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasSlowFPVFMx,
                                                         FeatureVMLxForwarding,
                                                         FeatureMP,
                                                         FeatureVFP4,
                                                         FeatureVirtualization]>;

def : ProcessorModel<"cortex-a8",   CortexA8Model,      [ARMv7a, ProcA8,
                                                         FeatureHasRetAddrStack,
                                                         FeatureNonpipelinedVFP,
                                                         FeatureTrustZone,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHasVMLxHazards,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasSlowFPVFMx,
                                                         FeatureVMLxForwarding]>;

def : ProcessorModel<"cortex-a9",   CortexA9Model,      [ARMv7a, ProcA9,
                                                         FeatureHasRetAddrStack,
                                                         FeatureTrustZone,
                                                         FeatureHasVMLxHazards,
                                                         FeatureVMLxForwarding,
                                                         FeatureFP16,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureExpandMLx,
                                                         FeaturePreferVMOVSR,
                                                         FeatureMuxedUnits,
                                                         FeatureNEONForFPMovs,
                                                         FeatureCheckVLDnAlign,
                                                         FeatureMP]>;

def : ProcessorModel<"cortex-a12",  CortexA9Model,      [ARMv7a, ProcA12,
                                                         FeatureHasRetAddrStack,
                                                         FeatureTrustZone,
                                                         FeatureVMLxForwarding,
                                                         FeatureVFP4,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureVirtualization,
                                                         FeatureMP]>;

def : ProcessorModel<"cortex-a15",  CortexA9Model,      [ARMv7a, ProcA15,
                                                         FeatureDontWidenVMOVS,
                                                         FeatureSplatVFPToNeon,
                                                         FeatureHasRetAddrStack,
                                                         FeatureMuxedUnits,
                                                         FeatureTrustZone,
                                                         FeatureVFP4,
                                                         FeatureMP,
                                                         FeatureCheckVLDnAlign,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureVirtualization]>;

def : ProcessorModel<"cortex-a17",  CortexA9Model,      [ARMv7a, ProcA17,
                                                         FeatureHasRetAddrStack,
                                                         FeatureTrustZone,
                                                         FeatureMP,
                                                         FeatureVMLxForwarding,
                                                         FeatureVFP4,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureVirtualization]>;

// FIXME: krait has currently the same features as A9 plus VFP4 and  HWDiv
def : ProcessorModel<"krait",       CortexA9Model,      [ARMv7a, ProcKrait,
                                                         FeatureHasRetAddrStack,
                                                         FeatureMuxedUnits,
                                                         FeatureCheckVLDnAlign,
                                                         FeatureVMLxForwarding,
                                                         FeatureFP16,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureVFP4,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM]>;

def : ProcessorModel<"swift",       SwiftModel,         [ARMv7a, ProcSwift,
                                                         FeatureHasRetAddrStack,
                                                         FeatureNEONForFP,
                                                         FeatureVFP4,
                                                         FeatureUseWideStrideVFP,
                                                         FeatureMP,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureAvoidMOVsShOp,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasSlowFPVFMx,
                                                         FeatureHasVMLxHazards,
                                                         FeatureProfUnpredicate,
                                                         FeaturePrefISHSTBarrier,
                                                         FeatureSlowOddRegister,
                                                         FeatureSlowLoadDSubreg,
                                                         FeatureSlowVGETLNi32,
                                                         FeatureSlowVDUP32,
                                                         FeatureUseMISched,
                                                         FeatureNoPostRASched]>;

def : ProcessorModel<"cortex-r4",   CortexA8Model,      [ARMv7r, ProcR4,
                                                         FeatureHasRetAddrStack,
                                                         FeatureAvoidPartialCPSR]>;

def : ProcessorModel<"cortex-r4f",  CortexA8Model,      [ARMv7r, ProcR4,
                                                         FeatureHasRetAddrStack,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasSlowFPVFMx,
                                                         FeatureVFP3_D16,
                                                         FeatureAvoidPartialCPSR]>;

def : ProcessorModel<"cortex-r5",   CortexA8Model,      [ARMv7r, ProcR5,
                                                         FeatureHasRetAddrStack,
                                                         FeatureVFP3_D16,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHWDivARM,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasSlowFPVFMx,
                                                         FeatureAvoidPartialCPSR]>;

def : ProcessorModel<"cortex-r7",   CortexA8Model,      [ARMv7r, ProcR7,
                                                         FeatureHasRetAddrStack,
                                                         FeatureVFP3_D16,
                                                         FeatureFP16,
                                                         FeatureMP,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHWDivARM,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasSlowFPVFMx,
                                                         FeatureAvoidPartialCPSR]>;

def : ProcessorModel<"cortex-r8",   CortexA8Model,      [ARMv7r,
                                                         FeatureHasRetAddrStack,
                                                         FeatureVFP3_D16,
                                                         FeatureFP16,
                                                         FeatureMP,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHWDivARM,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasSlowFPVFMx,
                                                         FeatureAvoidPartialCPSR]>;

def : ProcessorModel<"cortex-m3",   CortexM4Model,      [ARMv7m,
                                                         ProcM3,
                                                         FeaturePrefLoopAlign32,
                                                         FeatureUseMISched,
                                                         FeatureHasNoBranchPredictor]>;

def : ProcessorModel<"sc300",       CortexM4Model,      [ARMv7m,
                                                         ProcM3,
                                                         FeatureUseMISched,
                                                         FeatureHasNoBranchPredictor]>;

def : ProcessorModel<"cortex-m4", CortexM4Model,        [ARMv7em,
                                                         FeatureVFP4_D16_SP,
                                                         FeaturePrefLoopAlign32,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasSlowFPVFMx,
                                                         FeatureUseMISched,
                                                         FeatureHasNoBranchPredictor]>;

def : ProcessorModel<"cortex-m7", CortexM7Model,        [ARMv7em,
                                                         ProcM7,
                                                         FeatureFPARMv8_D16,
                                                         FeatureUseMIPipeliner,
                                                         FeatureUseMISched]>;

def : ProcNoItin<"cortex-m23",                          [ARMv8mBaseline,
                                                         FeatureNoMovt,
                                                         FeatureHasNoBranchPredictor]>;

def : ProcessorModel<"cortex-m33", CortexM4Model,       [ARMv8mMainline,
                                                         FeatureDSP,
                                                         FeatureFPARMv8_D16_SP,
                                                         FeaturePrefLoopAlign32,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasSlowFPVFMx,
                                                         FeatureUseMISched,
                                                         FeatureHasNoBranchPredictor,
                                                         FeatureFixCMSE_CVE_2021_35465]>;

def : ProcessorModel<"cortex-m35p", CortexM4Model,      [ARMv8mMainline,
                                                         FeatureDSP,
                                                         FeatureFPARMv8_D16_SP,
                                                         FeaturePrefLoopAlign32,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasSlowFPVFMx,
                                                         FeatureUseMISched,
                                                         FeatureHasNoBranchPredictor,
                                                         FeatureFixCMSE_CVE_2021_35465]>;

def : ProcessorModel<"cortex-m55", CortexM55Model,      [ARMv81mMainline,
                                                         FeatureDSP,
                                                         FeatureFPARMv8_D16,
                                                         FeatureUseMISched,
                                                         FeatureHasNoBranchPredictor,
                                                         FeaturePrefLoopAlign32,
                                                         FeatureHasSlowFPVMLx,
                                                         HasMVEFloatOps,
                                                         FeatureFixCMSE_CVE_2021_35465]>;

def : ProcessorModel<"cortex-m85", CortexM85Model,      [ARMv81mMainline,
                                                         FeatureDSP,
                                                         FeatureFPARMv8_D16,
                                                         FeaturePACBTI,
                                                         FeatureUseMISched,
                                                         HasMVEFloatOps]>;

def : ProcessorModel<"cortex-m52", CortexM55Model,      [ARMv81mMainline,
                                                         FeatureDSP,
                                                         FeatureFPARMv8_D16,
                                                         FeatureHasNoBranchPredictor,
                                                         FeaturePACBTI,
                                                         FeatureUseMISched,
                                                         FeaturePrefLoopAlign32,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureMVEVectorCostFactor1,
                                                         HasMVEFloatOps]>;

def : ProcNoItin<"cortex-a32",                           [ARMv8a,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC]>;

def : ProcNoItin<"cortex-a35",                          [ARMv8a, ProcA35,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC]>;

def : ProcNoItin<"cortex-a53",                          [ARMv8a, ProcA53,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFPAO]>;

def : ProcNoItin<"cortex-a55",                          [ARMv82a, ProcA55,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureDotProd]>;

def : ProcessorModel<"cortex-a57",  CortexA57Model,     [ARMv8a, ProcA57,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFPAO,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureCheapPredicableCPSR,
                                                         FeatureFixCortexA57AES1742098]>;

def : ProcessorModel<"cortex-a72",  CortexA57Model,     [ARMv8a, ProcA72,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFixCortexA57AES1742098]>;

def : ProcNoItin<"cortex-a73",                          [ARMv8a, ProcA73,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC]>;

def : ProcNoItin<"cortex-a75",                          [ARMv82a, ProcA75,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureDotProd]>;

def : ProcNoItin<"cortex-a76",                          [ARMv82a, ProcA76,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFullFP16,
                                                         FeatureDotProd]>;

def : ProcNoItin<"cortex-a76ae",                        [ARMv82a, ProcA76,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFullFP16,
                                                         FeatureDotProd]>;

def : ProcNoItin<"cortex-a77",                          [ARMv82a, ProcA77,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFullFP16,
                                                         FeatureDotProd]>;

def : ProcNoItin<"cortex-a78",                          [ARMv82a, ProcA78,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFullFP16,
                                                         FeatureDotProd]>;

def : ProcNoItin<"cortex-a78ae",                        [ARMv82a, ProcA78AE,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFullFP16,
                                                         FeatureDotProd]>;

def : ProcNoItin<"cortex-a78c",                         [ARMv82a, ProcA78C,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureDotProd,
                                                         FeatureFullFP16]>;

def : ProcNoItin<"cortex-a710",                         [ARMv9a, ProcA710,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureFP16FML,
                                                         FeatureBF16,
                                                         FeatureMatMulInt8,
                                                         FeatureSB]>;

def : ProcNoItin<"cortex-x1",                           [ARMv82a, ProcX1,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFullFP16,
                                                         FeatureDotProd]>;

def : ProcNoItin<"cortex-x1c",                          [ARMv82a, ProcX1C,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFullFP16,
                                                         FeatureDotProd]>;

def : ProcNoItin<"neoverse-v1",                         [ARMv84a,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFullFP16,
                                                         FeatureBF16,
                                                         FeatureMatMulInt8]>;

def : ProcNoItin<"neoverse-n1",                         [ARMv82a,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureDotProd]>;

def : ProcNoItin<"neoverse-n2",                         [ARMv9a,
                                                         FeatureBF16,
                                                         FeatureFP16FML,
                                                         FeatureMatMulInt8]>;

def : ProcessorModel<"cyclone",     SwiftModel,         [ARMv8a, ProcSwift,
                                                         FeatureHasRetAddrStack,
                                                         FeatureNEONForFP,
                                                         FeatureVFP4,
                                                         FeatureMP,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureAvoidMOVsShOp,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasSlowFPVFMx,
                                                         FeatureCrypto,
                                                         FeatureUseMISched,
                                                         FeatureZCZeroing,
                                                         FeatureNoPostRASched]>;

def : ProcNoItin<"exynos-m3",                           [ARMv8a, ProcExynos]>;
def : ProcNoItin<"exynos-m4",                           [ARMv82a, ProcExynos,
                                                         FeatureFullFP16,
                                                         FeatureDotProd]>;
def : ProcNoItin<"exynos-m5",                           [ARMv82a, ProcExynos,
                                                         FeatureFullFP16,
                                                         FeatureDotProd]>;

def : ProcNoItin<"kryo",                                [ARMv8a, ProcKryo,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC]>;

def : ProcessorModel<"cortex-r52", CortexR52Model,      [ARMv8r, ProcR52,
                                                         FeatureFPARMv8,
                                                         FeatureNEON,
                                                         FeatureUseMISched,
                                                         FeatureFPAO]>;

def : ProcessorModel<"cortex-r52plus", CortexR52Model,  [ARMv8r, ProcR52plus,
                                                         FeatureFPARMv8,
                                                         FeatureNEON,
                                                         FeatureUseMISched,
                                                         FeatureFPAO]>;
