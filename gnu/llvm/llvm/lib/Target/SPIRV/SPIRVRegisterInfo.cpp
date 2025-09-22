//===-- SPIRVRegisterInfo.cpp - SPIR-V Register Information -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the SPIR-V implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "SPIRVRegisterInfo.h"
#include "SPIRV.h"
#include "SPIRVSubtarget.h"
#include "llvm/CodeGen/MachineFunction.h"

#define GET_REGINFO_TARGET_DESC
#include "SPIRVGenRegisterInfo.inc"
using namespace llvm;

SPIRVRegisterInfo::SPIRVRegisterInfo() : SPIRVGenRegisterInfo(SPIRV::ID0) {}

BitVector SPIRVRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  return BitVector(getNumRegs());
}

const MCPhysReg *
SPIRVRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  static const MCPhysReg CalleeSavedReg = {0};
  return &CalleeSavedReg;
}
