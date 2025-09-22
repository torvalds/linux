//===- SPIRVRegisterBankInfo.cpp ------------------------------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the targeting of the RegisterBankInfo class for SPIR-V.
//
//===----------------------------------------------------------------------===//

#include "SPIRVRegisterBankInfo.h"
#include "SPIRVRegisterInfo.h"
#include "llvm/CodeGen/RegisterBank.h"

#define GET_REGINFO_ENUM
#include "SPIRVGenRegisterInfo.inc"

#define GET_TARGET_REGBANK_IMPL
#include "SPIRVGenRegisterBank.inc"

using namespace llvm;

// This required for .td selection patterns to work or we'd end up with RegClass
// checks being redundant as all the classes would be mapped to the same bank.
const RegisterBank &
SPIRVRegisterBankInfo::getRegBankFromRegClass(const TargetRegisterClass &RC,
                                              LLT Ty) const {
  if (RC.getID() == SPIRV::TYPERegClassID)
    return SPIRV::TYPERegBank;
  return SPIRV::IDRegBank;
}
