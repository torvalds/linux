//=- AMDGPURegisterBank.td - Describe the AMDGPU Banks -------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def SGPRRegBank : RegisterBank<"SGPR",
  [SReg_LO16, SReg_32, SReg_64, SReg_96, SReg_128, SReg_160, SReg_192, SReg_224, SReg_256, SReg_288, SReg_320, SReg_352, SReg_384, SReg_512, SReg_1024]
>;

def VGPRRegBank : RegisterBank<"VGPR",
  [VGPR_32, VReg_64, VReg_96, VReg_128, VReg_160, VReg_192, VReg_224, VReg_256, VReg_288, VReg_320, VReg_352, VReg_384, VReg_512, VReg_1024]
>;

// It is helpful to distinguish conditions from ordinary SGPRs.
def VCCRegBank : RegisterBank <"VCC", [SReg_1]>;

def AGPRRegBank : RegisterBank <"AGPR",
  [AGPR_LO16, AGPR_32, AReg_64, AReg_96, AReg_128, AReg_160, AReg_192, AReg_224, AReg_256, AReg_288, AReg_320, AReg_352, AReg_384, AReg_512, AReg_1024]
>;
