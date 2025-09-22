//===-- SystemZ.td - SystemZ processors and features ---------*- tblgen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Feature definitions.
//
//===----------------------------------------------------------------------===//

class SystemZFeature<string extname, string intname, dag featdag, string desc>
  : Predicate<"Subtarget->has"#intname#"()">,
    AssemblerPredicate<featdag, extname>,
    SubtargetFeature<extname, "Has"#intname, "true", desc>;

class SystemZMissingFeature<string intname>
  : Predicate<"!Subtarget->has"#intname#"()">;

class SystemZFeatureList<list<SystemZFeature> x> {
  list<SystemZFeature> List = x;
}

class SystemZFeatureAdd<list<SystemZFeature> x, list<SystemZFeature> y>
  : SystemZFeatureList<!listconcat(x, y)>;

// This feature is added as a subtarget feature whenever the function is
// compiled to use soft-float.
def FeatureSoftFloat : SystemZFeature<
  "soft-float", "SoftFloat", (all_of FeatureSoftFloat),
  "Use software emulation for floating point"
>;

def FeatureBackChain : SystemZFeature<
  "backchain", "BackChain", (all_of FeatureBackChain),
  "Store the address of the caller's frame into the callee's stack frame"
>;

def FeatureUnalignedSymbols : SystemZFeature<
  "unaligned-symbols", "UnalignedSymbols", (all_of FeatureUnalignedSymbols),
  "Don't apply the ABI minimum alignment to external symbols."
>;

//===----------------------------------------------------------------------===//
//
// New features added in the Ninth Edition of the z/Architecture
//
//===----------------------------------------------------------------------===//

def FeatureDistinctOps : SystemZFeature<
  "distinct-ops", "DistinctOps", (all_of FeatureDistinctOps),
  "Assume that the distinct-operands facility is installed"
>;

def FeatureFastSerialization : SystemZFeature<
  "fast-serialization", "FastSerialization", (all_of FeatureFastSerialization),
  "Assume that the fast-serialization facility is installed"
>;

def FeatureFPExtension : SystemZFeature<
  "fp-extension", "FPExtension", (all_of FeatureFPExtension),
  "Assume that the floating-point extension facility is installed"
>;

def FeatureHighWord : SystemZFeature<
  "high-word", "HighWord", (all_of FeatureHighWord),
  "Assume that the high-word facility is installed"
>;

def FeatureInterlockedAccess1 : SystemZFeature<
  "interlocked-access1", "InterlockedAccess1", (all_of FeatureInterlockedAccess1),
  "Assume that interlocked-access facility 1 is installed"
>;
def FeatureNoInterlockedAccess1 : SystemZMissingFeature<"InterlockedAccess1">;

def FeatureLoadStoreOnCond : SystemZFeature<
  "load-store-on-cond", "LoadStoreOnCond", (all_of FeatureLoadStoreOnCond),
  "Assume that the load/store-on-condition facility is installed"
>;
def FeatureNoLoadStoreOnCond : SystemZMissingFeature<"LoadStoreOnCond">;

def FeaturePopulationCount : SystemZFeature<
  "population-count", "PopulationCount", (all_of FeaturePopulationCount),
  "Assume that the population-count facility is installed"
>;

def FeatureMessageSecurityAssist3 : SystemZFeature<
  "message-security-assist-extension3", "MessageSecurityAssist3", (all_of FeatureMessageSecurityAssist3),
  "Assume that the message-security-assist extension facility 3 is installed"
>;

def FeatureMessageSecurityAssist4 : SystemZFeature<
  "message-security-assist-extension4", "MessageSecurityAssist4", (all_of FeatureMessageSecurityAssist4),
  "Assume that the message-security-assist extension facility 4 is installed"
>;

def FeatureResetReferenceBitsMultiple : SystemZFeature<
  "reset-reference-bits-multiple", "ResetReferenceBitsMultiple", (all_of FeatureResetReferenceBitsMultiple),
  "Assume that the reset-reference-bits-multiple facility is installed"
>;

def Arch9NewFeatures : SystemZFeatureList<[
    FeatureDistinctOps,
    FeatureFastSerialization,
    FeatureFPExtension,
    FeatureHighWord,
    FeatureInterlockedAccess1,
    FeatureLoadStoreOnCond,
    FeaturePopulationCount,
    FeatureMessageSecurityAssist3,
    FeatureMessageSecurityAssist4,
    FeatureResetReferenceBitsMultiple
]>;

//===----------------------------------------------------------------------===//
//
// New features added in the Tenth Edition of the z/Architecture
//
//===----------------------------------------------------------------------===//

def FeatureExecutionHint : SystemZFeature<
  "execution-hint", "ExecutionHint", (all_of FeatureExecutionHint),
  "Assume that the execution-hint facility is installed"
>;

def FeatureLoadAndTrap : SystemZFeature<
  "load-and-trap", "LoadAndTrap", (all_of FeatureLoadAndTrap),
  "Assume that the load-and-trap facility is installed"
>;

def FeatureMiscellaneousExtensions : SystemZFeature<
  "miscellaneous-extensions", "MiscellaneousExtensions", (all_of FeatureMiscellaneousExtensions),
  "Assume that the miscellaneous-extensions facility is installed"
>;

def FeatureProcessorAssist : SystemZFeature<
  "processor-assist", "ProcessorAssist", (all_of FeatureProcessorAssist),
  "Assume that the processor-assist facility is installed"
>;

def FeatureTransactionalExecution : SystemZFeature<
  "transactional-execution", "TransactionalExecution", (all_of FeatureTransactionalExecution),
  "Assume that the transactional-execution facility is installed"
>;

def FeatureDFPZonedConversion : SystemZFeature<
  "dfp-zoned-conversion", "DFPZonedConversion", (all_of FeatureDFPZonedConversion),
  "Assume that the DFP zoned-conversion facility is installed"
>;

def FeatureEnhancedDAT2 : SystemZFeature<
  "enhanced-dat-2", "EnhancedDAT2", (all_of FeatureEnhancedDAT2),
  "Assume that the enhanced-DAT facility 2 is installed"
>;

def Arch10NewFeatures : SystemZFeatureList<[
    FeatureExecutionHint,
    FeatureLoadAndTrap,
    FeatureMiscellaneousExtensions,
    FeatureProcessorAssist,
    FeatureTransactionalExecution,
    FeatureDFPZonedConversion,
    FeatureEnhancedDAT2
]>;

//===----------------------------------------------------------------------===//
//
// New features added in the Eleventh Edition of the z/Architecture
//
//===----------------------------------------------------------------------===//

def FeatureLoadAndZeroRightmostByte : SystemZFeature<
  "load-and-zero-rightmost-byte", "LoadAndZeroRightmostByte", (all_of FeatureLoadAndZeroRightmostByte),
  "Assume that the load-and-zero-rightmost-byte facility is installed"
>;

def FeatureLoadStoreOnCond2 : SystemZFeature<
  "load-store-on-cond-2", "LoadStoreOnCond2", (all_of FeatureLoadStoreOnCond2),
  "Assume that the load/store-on-condition facility 2 is installed"
>;

def FeatureMessageSecurityAssist5 : SystemZFeature<
  "message-security-assist-extension5", "MessageSecurityAssist5", (all_of FeatureMessageSecurityAssist5),
  "Assume that the message-security-assist extension facility 5 is installed"
>;

def FeatureDFPPackedConversion : SystemZFeature<
  "dfp-packed-conversion", "DFPPackedConversion", (all_of FeatureDFPPackedConversion),
  "Assume that the DFP packed-conversion facility is installed"
>;

def FeatureVector : SystemZFeature<
  "vector", "Vector", (all_of FeatureVector),
  "Assume that the vectory facility is installed"
>;
def FeatureNoVector : SystemZMissingFeature<"Vector">;

def Arch11NewFeatures : SystemZFeatureList<[
    FeatureLoadAndZeroRightmostByte,
    FeatureLoadStoreOnCond2,
    FeatureMessageSecurityAssist5,
    FeatureDFPPackedConversion,
    FeatureVector
]>;

//===----------------------------------------------------------------------===//
//
// New features added in the Twelfth Edition of the z/Architecture
//
//===----------------------------------------------------------------------===//

def FeatureMiscellaneousExtensions2 : SystemZFeature<
  "miscellaneous-extensions-2", "MiscellaneousExtensions2", (all_of FeatureMiscellaneousExtensions2),
  "Assume that the miscellaneous-extensions facility 2 is installed"
>;

def FeatureGuardedStorage : SystemZFeature<
  "guarded-storage", "GuardedStorage", (all_of FeatureGuardedStorage),
  "Assume that the guarded-storage facility is installed"
>;

def FeatureMessageSecurityAssist7 : SystemZFeature<
  "message-security-assist-extension7", "MessageSecurityAssist7", (all_of FeatureMessageSecurityAssist7),
  "Assume that the message-security-assist extension facility 7 is installed"
>;

def FeatureMessageSecurityAssist8 : SystemZFeature<
  "message-security-assist-extension8", "MessageSecurityAssist8", (all_of FeatureMessageSecurityAssist8),
  "Assume that the message-security-assist extension facility 8 is installed"
>;

def FeatureVectorEnhancements1 : SystemZFeature<
  "vector-enhancements-1", "VectorEnhancements1", (all_of FeatureVectorEnhancements1),
  "Assume that the vector enhancements facility 1 is installed"
>;
def FeatureNoVectorEnhancements1 : SystemZMissingFeature<"VectorEnhancements1">;

def FeatureVectorPackedDecimal : SystemZFeature<
  "vector-packed-decimal", "VectorPackedDecimal", (all_of FeatureVectorPackedDecimal),
  "Assume that the vector packed decimal facility is installed"
>;

def FeatureInsertReferenceBitsMultiple : SystemZFeature<
  "insert-reference-bits-multiple", "InsertReferenceBitsMultiple", (all_of FeatureInsertReferenceBitsMultiple),
  "Assume that the insert-reference-bits-multiple facility is installed"
>;

def FeatureTestPendingExternalInterruption : SystemZFeature<
  "test-pending-external-interruption", "TestPendingExternalInterruption", (all_of FeatureTestPendingExternalInterruption),
  "Assume that the test-pending-external-interruption facility is installed"
>;

def Arch12NewFeatures : SystemZFeatureList<[
    FeatureMiscellaneousExtensions2,
    FeatureGuardedStorage,
    FeatureMessageSecurityAssist7,
    FeatureMessageSecurityAssist8,
    FeatureVectorEnhancements1,
    FeatureVectorPackedDecimal,
    FeatureInsertReferenceBitsMultiple,
    FeatureTestPendingExternalInterruption
]>;

//===----------------------------------------------------------------------===//
//
// New features added in the Thirteenth Edition of the z/Architecture
//
//===----------------------------------------------------------------------===//

def FeatureMiscellaneousExtensions3 : SystemZFeature<
  "miscellaneous-extensions-3", "MiscellaneousExtensions3", (all_of FeatureMiscellaneousExtensions3),
  "Assume that the miscellaneous-extensions facility 3 is installed"
>;

def FeatureMessageSecurityAssist9 : SystemZFeature<
  "message-security-assist-extension9", "MessageSecurityAssist9", (all_of FeatureMessageSecurityAssist9),
  "Assume that the message-security-assist extension facility 9 is installed"
>;

def FeatureVectorEnhancements2 : SystemZFeature<
  "vector-enhancements-2", "VectorEnhancements2", (all_of FeatureVectorEnhancements2),
  "Assume that the vector enhancements facility 2 is installed"
>;

def FeatureVectorPackedDecimalEnhancement : SystemZFeature<
  "vector-packed-decimal-enhancement", "VectorPackedDecimalEnhancement", (all_of FeatureVectorPackedDecimalEnhancement),
  "Assume that the vector packed decimal enhancement facility is installed"
>;

def FeatureEnhancedSort : SystemZFeature<
  "enhanced-sort", "EnhancedSort", (all_of FeatureEnhancedSort),
  "Assume that the enhanced-sort facility is installed"
>;

def FeatureDeflateConversion : SystemZFeature<
  "deflate-conversion", "DeflateConversion", (all_of FeatureDeflateConversion),
  "Assume that the deflate-conversion facility is installed"
>;

def Arch13NewFeatures : SystemZFeatureList<[
    FeatureMiscellaneousExtensions3,
    FeatureMessageSecurityAssist9,
    FeatureVectorEnhancements2,
    FeatureVectorPackedDecimalEnhancement,
    FeatureEnhancedSort,
    FeatureDeflateConversion
]>;

//===----------------------------------------------------------------------===//
//
// New features added in the Fourteenth Edition of the z/Architecture
//
//===----------------------------------------------------------------------===//

def FeatureVectorPackedDecimalEnhancement2 : SystemZFeature<
  "vector-packed-decimal-enhancement-2", "VectorPackedDecimalEnhancement2", (all_of FeatureVectorPackedDecimalEnhancement2),
  "Assume that the vector packed decimal enhancement facility 2 is installed"
>;

def FeatureNNPAssist : SystemZFeature<
  "nnp-assist", "NNPAssist", (all_of FeatureNNPAssist),
  "Assume that the NNP-assist facility is installed"
>;

def FeatureBEAREnhancement : SystemZFeature<
  "bear-enhancement", "BEAREnhancement", (all_of FeatureBEAREnhancement),
  "Assume that the BEAR-enhancement facility is installed"
>;

def FeatureResetDATProtection : SystemZFeature<
  "reset-dat-protection", "ResetDATProtection", (all_of FeatureResetDATProtection),
  "Assume that the reset-DAT-protection facility is installed"
>;

def FeatureProcessorActivityInstrumentation : SystemZFeature<
  "processor-activity-instrumentation", "ProcessorActivityInstrumentation", (all_of FeatureProcessorActivityInstrumentation),
  "Assume that the processor-activity-instrumentation facility is installed"
>;

def Arch14NewFeatures : SystemZFeatureList<[
    FeatureVectorPackedDecimalEnhancement2,
    FeatureNNPAssist,
    FeatureBEAREnhancement,
    FeatureResetDATProtection,
    FeatureProcessorActivityInstrumentation
]>;

//===----------------------------------------------------------------------===//
//
// Cumulative supported and unsupported feature sets
//
//===----------------------------------------------------------------------===//

def Arch8SupportedFeatures
  : SystemZFeatureList<[]>;
def Arch9SupportedFeatures
  : SystemZFeatureAdd<Arch8SupportedFeatures.List,  Arch9NewFeatures.List>;
def Arch10SupportedFeatures
  : SystemZFeatureAdd<Arch9SupportedFeatures.List,  Arch10NewFeatures.List>;
def Arch11SupportedFeatures
  : SystemZFeatureAdd<Arch10SupportedFeatures.List, Arch11NewFeatures.List>;
def Arch12SupportedFeatures
  : SystemZFeatureAdd<Arch11SupportedFeatures.List, Arch12NewFeatures.List>;
def Arch13SupportedFeatures
  : SystemZFeatureAdd<Arch12SupportedFeatures.List, Arch13NewFeatures.List>;
def Arch14SupportedFeatures
  : SystemZFeatureAdd<Arch13SupportedFeatures.List, Arch14NewFeatures.List>;

def Arch14UnsupportedFeatures
  : SystemZFeatureList<[]>;
def Arch13UnsupportedFeatures
  : SystemZFeatureAdd<Arch14UnsupportedFeatures.List, Arch14NewFeatures.List>;
def Arch12UnsupportedFeatures
  : SystemZFeatureAdd<Arch13UnsupportedFeatures.List, Arch13NewFeatures.List>;
def Arch11UnsupportedFeatures
  : SystemZFeatureAdd<Arch12UnsupportedFeatures.List, Arch12NewFeatures.List>;
def Arch10UnsupportedFeatures
  : SystemZFeatureAdd<Arch11UnsupportedFeatures.List, Arch11NewFeatures.List>;
def Arch9UnsupportedFeatures
  : SystemZFeatureAdd<Arch10UnsupportedFeatures.List, Arch10NewFeatures.List>;
def Arch8UnsupportedFeatures
  : SystemZFeatureAdd<Arch9UnsupportedFeatures.List,  Arch9NewFeatures.List>;

