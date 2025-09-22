//===-- SystemZCallingConv.cpp - Calling conventions for SystemZ ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SystemZCallingConv.h"
#include "SystemZRegisterInfo.h"

using namespace llvm;

const MCPhysReg SystemZ::ELFArgGPRs[SystemZ::ELFNumArgGPRs] = {
  SystemZ::R2D, SystemZ::R3D, SystemZ::R4D, SystemZ::R5D, SystemZ::R6D
};

const MCPhysReg SystemZ::ELFArgFPRs[SystemZ::ELFNumArgFPRs] = {
  SystemZ::F0D, SystemZ::F2D, SystemZ::F4D, SystemZ::F6D
};

// The XPLINK64 ABI-defined param passing general purpose registers
const MCPhysReg SystemZ::XPLINK64ArgGPRs[SystemZ::XPLINK64NumArgGPRs] = {
    SystemZ::R1D, SystemZ::R2D, SystemZ::R3D
};

// The XPLINK64 ABI-defined param passing floating point registers
const MCPhysReg SystemZ::XPLINK64ArgFPRs[SystemZ::XPLINK64NumArgFPRs] = {
    SystemZ::F0D, SystemZ::F2D, SystemZ::F4D, SystemZ::F6D
};
