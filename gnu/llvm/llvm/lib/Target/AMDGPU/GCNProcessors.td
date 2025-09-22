//===-- GCNProcessors.td - GCN Processor definitions ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// The code produced for "generic" is only useful for tests and cannot
// reasonably be expected to execute on any particular target.
def : ProcessorModel<"generic", NoSchedModel,
  [FeatureWavefrontSize64, FeatureGDS, FeatureGWS]
>;

def : ProcessorModel<"generic-hsa", NoSchedModel,
  [FeatureWavefrontSize64, FeatureGDS, FeatureGWS, FeatureFlatAddressSpace]
>;

//===------------------------------------------------------------===//
// GCN GFX6 (Southern Islands (SI)).
//===------------------------------------------------------------===//

def : ProcessorModel<"gfx600", SIFullSpeedModel,
  FeatureISAVersion6_0_0.Features
>;

def : ProcessorModel<"tahiti", SIFullSpeedModel,
  FeatureISAVersion6_0_0.Features
>;

def : ProcessorModel<"gfx601", SIQuarterSpeedModel,
  FeatureISAVersion6_0_1.Features
>;

def : ProcessorModel<"pitcairn", SIQuarterSpeedModel,
  FeatureISAVersion6_0_1.Features
>;

def : ProcessorModel<"verde", SIQuarterSpeedModel,
  FeatureISAVersion6_0_1.Features
>;

def : ProcessorModel<"gfx602", SIQuarterSpeedModel,
  FeatureISAVersion6_0_2.Features
>;

def : ProcessorModel<"hainan", SIQuarterSpeedModel,
  FeatureISAVersion6_0_2.Features
>;

def : ProcessorModel<"oland", SIQuarterSpeedModel,
  FeatureISAVersion6_0_2.Features
>;

//===------------------------------------------------------------===//
// GCN GFX7 (Sea Islands (CI)).
//===------------------------------------------------------------===//

def : ProcessorModel<"gfx700", SIQuarterSpeedModel,
  FeatureISAVersion7_0_0.Features
>;

def : ProcessorModel<"kaveri", SIQuarterSpeedModel,
  FeatureISAVersion7_0_0.Features
>;

def : ProcessorModel<"gfx701", SIFullSpeedModel,
  FeatureISAVersion7_0_1.Features
>;

def : ProcessorModel<"hawaii", SIFullSpeedModel,
  FeatureISAVersion7_0_1.Features
>;

def : ProcessorModel<"gfx702", SIQuarterSpeedModel,
  FeatureISAVersion7_0_2.Features
>;

def : ProcessorModel<"gfx703", SIQuarterSpeedModel,
  FeatureISAVersion7_0_3.Features
>;

def : ProcessorModel<"kabini", SIQuarterSpeedModel,
  FeatureISAVersion7_0_3.Features
>;

def : ProcessorModel<"mullins", SIQuarterSpeedModel,
  FeatureISAVersion7_0_3.Features
>;

def : ProcessorModel<"gfx704", SIQuarterSpeedModel,
  FeatureISAVersion7_0_4.Features
>;

def : ProcessorModel<"bonaire", SIQuarterSpeedModel,
  FeatureISAVersion7_0_4.Features
>;

def : ProcessorModel<"gfx705", SIQuarterSpeedModel,
  FeatureISAVersion7_0_5.Features
>;

//===------------------------------------------------------------===//
// GCN GFX8 (Volcanic Islands (VI)).
//===------------------------------------------------------------===//

def : ProcessorModel<"gfx801", SIQuarterSpeedModel,
  FeatureISAVersion8_0_1.Features
>;

def : ProcessorModel<"carrizo", SIQuarterSpeedModel,
  FeatureISAVersion8_0_1.Features
>;

def : ProcessorModel<"gfx802", SIQuarterSpeedModel,
  FeatureISAVersion8_0_2.Features
>;

def : ProcessorModel<"iceland", SIQuarterSpeedModel,
  FeatureISAVersion8_0_2.Features
>;

def : ProcessorModel<"tonga", SIQuarterSpeedModel,
  FeatureISAVersion8_0_2.Features
>;

def : ProcessorModel<"gfx803", SIQuarterSpeedModel,
  FeatureISAVersion8_0_3.Features
>;

def : ProcessorModel<"fiji", SIQuarterSpeedModel,
  FeatureISAVersion8_0_3.Features
>;

def : ProcessorModel<"polaris10", SIQuarterSpeedModel,
  FeatureISAVersion8_0_3.Features
>;

def : ProcessorModel<"polaris11", SIQuarterSpeedModel,
  FeatureISAVersion8_0_3.Features
>;

def : ProcessorModel<"gfx805", SIQuarterSpeedModel,
  FeatureISAVersion8_0_5.Features
>;

def : ProcessorModel<"tongapro", SIQuarterSpeedModel,
  FeatureISAVersion8_0_5.Features
>;

def : ProcessorModel<"gfx810", SIQuarterSpeedModel,
  FeatureISAVersion8_1_0.Features
>;

def : ProcessorModel<"stoney", SIQuarterSpeedModel,
  FeatureISAVersion8_1_0.Features
>;

//===------------------------------------------------------------===//
// GCN GFX9.
//===------------------------------------------------------------===//

def : ProcessorModel<"gfx900", SIQuarterSpeedModel,
  FeatureISAVersion9_0_0.Features
>;

def : ProcessorModel<"gfx902", SIQuarterSpeedModel,
  FeatureISAVersion9_0_2.Features
>;

def : ProcessorModel<"gfx904", SIQuarterSpeedModel,
  FeatureISAVersion9_0_4.Features
>;

def : ProcessorModel<"gfx906", SIQuarterSpeedModel,
  FeatureISAVersion9_0_6.Features
>;

def : ProcessorModel<"gfx908", SIQuarterSpeedModel,
  FeatureISAVersion9_0_8.Features
>;

def : ProcessorModel<"gfx909", SIQuarterSpeedModel,
  FeatureISAVersion9_0_9.Features
>;

def : ProcessorModel<"gfx90a", SIDPFullSpeedModel,
  FeatureISAVersion9_0_A.Features
>;

def : ProcessorModel<"gfx90c", SIQuarterSpeedModel,
  FeatureISAVersion9_0_C.Features
>;

def : ProcessorModel<"gfx940", SIDPGFX940FullSpeedModel,
  FeatureISAVersion9_4_0.Features
>;

def : ProcessorModel<"gfx941", SIDPGFX940FullSpeedModel,
  FeatureISAVersion9_4_1.Features
>;

def : ProcessorModel<"gfx942", SIDPGFX940FullSpeedModel,
  FeatureISAVersion9_4_2.Features
>;

// [gfx900, gfx902, gfx904, gfx906, gfx909, gfx90c]
def : ProcessorModel<"gfx9-generic", SIQuarterSpeedModel,
  FeatureISAVersion9_Generic.Features
>;

//===----------------------------------------------------------------------===//
// GCN GFX10.
//===----------------------------------------------------------------------===//

def : ProcessorModel<"gfx1010", GFX10SpeedModel,
  FeatureISAVersion10_1_0.Features
>;

def : ProcessorModel<"gfx1011", GFX10SpeedModel,
  FeatureISAVersion10_1_1.Features
>;

def : ProcessorModel<"gfx1012", GFX10SpeedModel,
  FeatureISAVersion10_1_2.Features
>;

def : ProcessorModel<"gfx1013", GFX10SpeedModel,
  FeatureISAVersion10_1_3.Features
>;

def : ProcessorModel<"gfx1030", GFX10SpeedModel,
  FeatureISAVersion10_3_0.Features
>;

def : ProcessorModel<"gfx1031", GFX10SpeedModel,
  FeatureISAVersion10_3_0.Features
>;

def : ProcessorModel<"gfx1032", GFX10SpeedModel,
  FeatureISAVersion10_3_0.Features
>;

def : ProcessorModel<"gfx1033", GFX10SpeedModel,
  FeatureISAVersion10_3_0.Features
>;

def : ProcessorModel<"gfx1034", GFX10SpeedModel,
  FeatureISAVersion10_3_0.Features
>;

def : ProcessorModel<"gfx1035", GFX10SpeedModel,
  FeatureISAVersion10_3_0.Features
>;

def : ProcessorModel<"gfx1036", GFX10SpeedModel,
  FeatureISAVersion10_3_0.Features
>;

// [gfx1010, gfx1011, gfx1012, gfx1013]
def : ProcessorModel<"gfx10-1-generic", GFX10SpeedModel,
  FeatureISAVersion10_1_Generic.Features
>;

// [gfx1030, gfx1031, gfx1032, gfx1033, gfx1034, gfx1035, gfx1036]
def : ProcessorModel<"gfx10-3-generic", GFX10SpeedModel,
  FeatureISAVersion10_3_Generic.Features
>;

//===----------------------------------------------------------------------===//
// GCN GFX11.
//===----------------------------------------------------------------------===//

def : ProcessorModel<"gfx1100", GFX11SpeedModel,
  FeatureISAVersion11_0_0.Features
>;

def : ProcessorModel<"gfx1101", GFX11SpeedModel,
  FeatureISAVersion11_0_1.Features
>;

def : ProcessorModel<"gfx1102", GFX11SpeedModel,
  FeatureISAVersion11_0_2.Features
>;

def : ProcessorModel<"gfx1103", GFX11SpeedModel,
  FeatureISAVersion11_0_3.Features
>;

def : ProcessorModel<"gfx1150", GFX11SpeedModel,
  FeatureISAVersion11_5_0.Features
>;

def : ProcessorModel<"gfx1151", GFX11SpeedModel,
  FeatureISAVersion11_5_1.Features
>;

def : ProcessorModel<"gfx1152", GFX11SpeedModel,
  FeatureISAVersion11_5_2.Features
>;

// [gfx1100, gfx1101, gfx1102, gfx1103, gfx1150, gfx1151, gfx1152]
def : ProcessorModel<"gfx11-generic", GFX11SpeedModel,
  FeatureISAVersion11_Generic.Features
>;

//===----------------------------------------------------------------------===//
// GCN GFX12.
//===----------------------------------------------------------------------===//

def : ProcessorModel<"gfx1200", GFX12SpeedModel,
  FeatureISAVersion12.Features
>;

def : ProcessorModel<"gfx1201", GFX12SpeedModel,
  FeatureISAVersion12.Features
>;

// [gfx1200, gfx1201]
def : ProcessorModel<"gfx12-generic", GFX12SpeedModel,
  FeatureISAVersion12_Generic.Features
>;
