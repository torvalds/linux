//===-- BPFRegisterBankInfo.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the targeting of the RegisterBankInfo class for BPF.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_GISEL_BPFREGISTERBANKINFO_H
#define LLVM_LIB_TARGET_BPF_GISEL_BPFREGISTERBANKINFO_H

#include "MCTargetDesc/BPFMCTargetDesc.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGBANK_DECLARATIONS
#include "BPFGenRegisterBank.inc"

namespace llvm {
class TargetRegisterInfo;

class BPFGenRegisterBankInfo : public RegisterBankInfo {
protected:
#define GET_TARGET_REGBANK_CLASS
#include "BPFGenRegisterBank.inc"
};

class BPFRegisterBankInfo final : public BPFGenRegisterBankInfo {
public:
  BPFRegisterBankInfo(const TargetRegisterInfo &TRI);
};
} // namespace llvm

#endif
