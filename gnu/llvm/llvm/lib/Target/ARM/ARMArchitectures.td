class Architecture<string fname, string aname, list<SubtargetFeature> features>
  : SubtargetFeature<fname, "ARMArch", aname,
                     !strconcat(aname, " architecture"), features>;

//===----------------------------------------------------------------------===//
// ARM architectures
//

def ARMv4     : Architecture<"armv4",     "ARMv4",    []>;

def ARMv4t    : Architecture<"armv4t",    "ARMv4t",   [HasV4TOps]>;

def ARMv5t    : Architecture<"armv5t",    "ARMv5t",   [HasV5TOps]>;

def ARMv5te   : Architecture<"armv5te",   "ARMv5te",  [HasV5TEOps]>;

def ARMv5tej  : Architecture<"armv5tej",  "ARMv5tej", [HasV5TEOps]>;

def ARMv6     : Architecture<"armv6",     "ARMv6",    [HasV6Ops,
                                                       FeatureDSP]>;

def ARMv6t2   : Architecture<"armv6t2",   "ARMv6t2",  [HasV6T2Ops,
                                                       FeatureDSP]>;

def ARMv6k    : Architecture<"armv6k",    "ARMv6k",   [HasV6KOps]>;

def ARMv6kz   : Architecture<"armv6kz",   "ARMv6kz",  [HasV6KOps,
                                                       FeatureTrustZone]>;

def ARMv6m    : Architecture<"armv6-m",   "ARMv6m",   [HasV6MOps,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureMClass,
                                                       FeatureStrictAlign]>;

def ARMv6sm   : Architecture<"armv6s-m",  "ARMv6sm",  [HasV6MOps,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureMClass,
                                                       FeatureStrictAlign]>;

def ARMv7a    : Architecture<"armv7-a",   "ARMv7a",   [HasV7Ops,
                                                       FeatureNEON,
                                                       FeatureDB,
                                                       FeatureDSP,
                                                       FeatureAClass,
                                                       FeaturePerfMon]>;

def ARMv7ve   : Architecture<"armv7ve",   "ARMv7ve",  [HasV7Ops,
                                                       FeatureNEON,
                                                       FeatureDB,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureAClass,
                                                       FeaturePerfMon]>;

def ARMv7r    : Architecture<"armv7-r",   "ARMv7r",   [HasV7Ops,
                                                       FeatureDB,
                                                       FeatureDSP,
                                                       FeatureHWDivThumb,
                                                       FeatureRClass,
                                                       FeaturePerfMon]>;

def ARMv7m    : Architecture<"armv7-m",   "ARMv7m",   [HasV7Ops,
                                                       FeatureThumb2,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureHWDivThumb,
                                                       FeatureMClass]>;

def ARMv7em   : Architecture<"armv7e-m",  "ARMv7em",  [HasV7Ops,
                                                       FeatureThumb2,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureHWDivThumb,
                                                       FeatureMClass,
                                                       FeatureDSP]>;

def ARMv8a    : Architecture<"armv8-a",   "ARMv8a",   [HasV8Ops,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC]>;

def ARMv81a   : Architecture<"armv8.1-a", "ARMv81a",  [HasV8_1aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC]>;

def ARMv82a   : Architecture<"armv8.2-a", "ARMv82a",  [HasV8_2aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS]>;

def ARMv83a   : Architecture<"armv8.3-a", "ARMv83a",  [HasV8_3aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS]>;

def ARMv84a   : Architecture<"armv8.4-a", "ARMv84a",  [HasV8_4aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;

def ARMv85a   : Architecture<"armv8.5-a", "ARMv85a",  [HasV8_5aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;
def ARMv86a   : Architecture<"armv8.6-a", "ARMv86a",  [HasV8_6aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;
def ARMv87a   : Architecture<"armv8.7-a", "ARMv87a",  [HasV8_7aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;
def ARMv88a   : Architecture<"armv8.8-a", "ARMv88a",  [HasV8_8aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;
def ARMv89a   : Architecture<"armv8.9-a", "ARMv89a",  [HasV8_9aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;

def ARMv9a   : Architecture<"armv9-a", "ARMv9a",       [HasV9_0aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;
def ARMv91a   : Architecture<"armv9.1-a", "ARMv91a",   [HasV9_1aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;
def ARMv92a   : Architecture<"armv9.2-a", "ARMv92a",  [HasV9_2aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;
def ARMv93a   : Architecture<"armv9.3-a", "ARMv93a",  [HasV9_3aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;
def ARMv94a   : Architecture<"armv9.4-a", "ARMv94a",  [HasV9_4aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;
def ARMv95a   : Architecture<"armv9.5-a", "ARMv95a",  [HasV9_5aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;

def ARMv8r    : Architecture<"armv8-r",   "ARMv8r",   [HasV8Ops,
                                                       FeatureRClass,
                                                       FeatureDB,
                                                       FeatureDFB,
                                                       FeatureDSP,
                                                       FeatureCRC,
                                                       FeatureMP,
                                                       FeatureFPARMv8_D16_SP,
                                                       FeatureVirtualization]>;

def ARMv8mBaseline : Architecture<"armv8-m.base", "ARMv8mBaseline",
                                                      [HasV8MBaselineOps,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureHWDivThumb,
                                                       FeatureV7Clrex,
                                                       Feature8MSecExt,
                                                       FeatureAcquireRelease,
                                                       FeatureMClass,
                                                       FeatureStrictAlign]>;

def ARMv8mMainline : Architecture<"armv8-m.main", "ARMv8mMainline",
                                                      [HasV8MMainlineOps,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureHWDivThumb,
                                                       Feature8MSecExt,
                                                       FeatureAcquireRelease,
                                                       FeatureMClass]>;

def ARMv81mMainline : Architecture<"armv8.1-m.main", "ARMv81mMainline",
                                                      [HasV8_1MMainlineOps,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureHWDivThumb,
                                                       Feature8MSecExt,
                                                       FeatureAcquireRelease,
                                                       FeatureMClass,
                                                       FeatureRAS,
                                                       FeatureLOB]>;

// Aliases
def IWMMXT   : Architecture<"iwmmxt",      "ARMv5te",  [ARMv5te]>;
def IWMMXT2  : Architecture<"iwmmxt2",     "ARMv5te",  [ARMv5te]>;
def XScale   : Architecture<"xscale",      "ARMv5te",  [ARMv5te]>;
def ARMv6j   : Architecture<"armv6j",      "ARMv7a",   [ARMv6]>;
def ARMv7k   : Architecture<"armv7k",      "ARMv7a",   [ARMv7a]>;
def ARMv7s   : Architecture<"armv7s",      "ARMv7a",   [ARMv7a]>;
