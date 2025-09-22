//===-- R600Processors.td - R600 Processor definitions --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class SubtargetFeatureFetchLimit <string Value> :
                          SubtargetFeature <"fetch"#Value,
  "TexVTXClauseSize",
  Value,
  "Limit the maximum number of fetches in a clause to "#Value
>;

def FeatureR600ALUInst : SubtargetFeature<"R600ALUInst",
  "R600ALUInst",
  "false",
  "Older version of ALU instructions encoding"
>;

def FeatureFetchLimit8 : SubtargetFeatureFetchLimit <"8">;
def FeatureFetchLimit16 : SubtargetFeatureFetchLimit <"16">;

def FeatureVertexCache : SubtargetFeature<"HasVertexCache",
  "HasVertexCache",
  "true",
  "Specify use of dedicated vertex cache"
>;

def FeatureCaymanISA : SubtargetFeature<"caymanISA",
  "CaymanISA",
  "true",
  "Use Cayman ISA"
>;

def FeatureCFALUBug : SubtargetFeature<"cfalubug",
  "CFALUBug",
  "true",
  "GPU has CF_ALU bug"
>;

class R600SubtargetFeatureGeneration <string Value, string FeatureName,
                                  list<SubtargetFeature> Implies> :
        SubtargetFeatureGeneration <Value, FeatureName, "R600Subtarget", Implies>;

def FeatureR600 : R600SubtargetFeatureGeneration<"R600", "r600",
  [FeatureR600ALUInst, FeatureFetchLimit8]
>;

def FeatureR700 : R600SubtargetFeatureGeneration<"R700", "r700",
  [FeatureFetchLimit16]
>;

def FeatureEvergreen : R600SubtargetFeatureGeneration<"EVERGREEN", "evergreen",
  [FeatureFetchLimit16, FeatureLocalMemorySize32768]
>;

def FeatureNorthernIslands : R600SubtargetFeatureGeneration<"NORTHERN_ISLANDS",
  "northern-islands",
  [FeatureFetchLimit16, FeatureWavefrontSize64,
   FeatureLocalMemorySize32768]
>;


//===----------------------------------------------------------------------===//
// Radeon HD 2000/3000 Series (R600).
//===----------------------------------------------------------------------===//

def : Processor<"r600", R600_VLIW5_Itin,
  [FeatureR600, FeatureWavefrontSize64, FeatureVertexCache]
>;

def : Processor<"r630", R600_VLIW5_Itin,
  [FeatureR600, FeatureWavefrontSize32, FeatureVertexCache]
>;

def : Processor<"rs880", R600_VLIW5_Itin,
  [FeatureR600, FeatureWavefrontSize16]
>;

def : Processor<"rv670", R600_VLIW5_Itin,
  [FeatureR600, FeatureWavefrontSize64, FeatureVertexCache]
>;

//===----------------------------------------------------------------------===//
// Radeon HD 4000 Series (R700).
//===----------------------------------------------------------------------===//

def : Processor<"rv710", R600_VLIW5_Itin,
  [FeatureR700, FeatureWavefrontSize32, FeatureVertexCache]
>;

def : Processor<"rv730", R600_VLIW5_Itin,
  [FeatureR700, FeatureWavefrontSize32, FeatureVertexCache]
>;

def : Processor<"rv770", R600_VLIW5_Itin,
  [FeatureR700, FeatureWavefrontSize64, FeatureVertexCache]
>;

//===----------------------------------------------------------------------===//
// Radeon HD 5000 Series (Evergreen).
//===----------------------------------------------------------------------===//

def : Processor<"cedar", R600_VLIW5_Itin,
  [FeatureEvergreen, FeatureWavefrontSize32, FeatureVertexCache,
   FeatureCFALUBug]
>;

def : Processor<"cypress", R600_VLIW5_Itin,
  [FeatureEvergreen, FeatureWavefrontSize64, FeatureVertexCache, FeatureFMA]
>;

def : Processor<"juniper", R600_VLIW5_Itin,
  [FeatureEvergreen, FeatureWavefrontSize64, FeatureVertexCache]
>;

def : Processor<"redwood", R600_VLIW5_Itin,
  [FeatureEvergreen, FeatureWavefrontSize64, FeatureVertexCache,
   FeatureCFALUBug]
>;

def : Processor<"sumo", R600_VLIW5_Itin,
  [FeatureEvergreen, FeatureWavefrontSize64, FeatureCFALUBug]
>;

//===----------------------------------------------------------------------===//
// Radeon HD 6000 Series (Northern Islands).
//===----------------------------------------------------------------------===//

def : Processor<"barts", R600_VLIW5_Itin,
  [FeatureNorthernIslands, FeatureVertexCache, FeatureCFALUBug]
>;

def : Processor<"caicos", R600_VLIW5_Itin,
  [FeatureNorthernIslands, FeatureCFALUBug]
>;

def : Processor<"cayman", R600_VLIW4_Itin,
  [FeatureNorthernIslands, FeatureCaymanISA, FeatureFMA]
>;

def : Processor<"turks", R600_VLIW5_Itin,
  [FeatureNorthernIslands, FeatureVertexCache, FeatureCFALUBug]
>;
