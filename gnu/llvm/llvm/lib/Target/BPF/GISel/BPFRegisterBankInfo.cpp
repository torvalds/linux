//===- BPFRegisterBankInfo.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the targeting of the RegisterBankInfo class for BPF
//===----------------------------------------------------------------------===//

#include "BPFRegisterBankInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "bpf-reg-bank-info"

#define GET_TARGET_REGBANK_IMPL
#include "BPFGenRegisterBank.inc"

using namespace llvm;

BPFRegisterBankInfo::BPFRegisterBankInfo(const TargetRegisterInfo &TRI)
    : BPFGenRegisterBankInfo() {}
