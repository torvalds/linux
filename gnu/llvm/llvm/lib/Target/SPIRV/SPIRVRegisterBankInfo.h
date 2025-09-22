//===- SPIRVRegisterBankInfo.h -----------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the targeting of the RegisterBankInfo class for SPIR-V.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRVREGISTERBANKINFO_H
#define LLVM_LIB_TARGET_SPIRV_SPIRVREGISTERBANKINFO_H

#include "llvm/CodeGen/RegisterBankInfo.h"

#define GET_REGBANK_DECLARATIONS
#include "SPIRVGenRegisterBank.inc"

namespace llvm {

class TargetRegisterInfo;

class SPIRVGenRegisterBankInfo : public RegisterBankInfo {
protected:
#define GET_TARGET_REGBANK_CLASS
#include "SPIRVGenRegisterBank.inc"
};

// This class provides the information for the target register banks.
class SPIRVRegisterBankInfo final : public SPIRVGenRegisterBankInfo {
public:
  const RegisterBank &getRegBankFromRegClass(const TargetRegisterClass &RC,
                                             LLT Ty) const override;
};
} // namespace llvm
#endif // LLVM_LIB_TARGET_SPIRV_SPIRVREGISTERBANKINFO_H
