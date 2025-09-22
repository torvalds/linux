//===-- ARMRegisterInfo.cpp - ARM Register Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the ARM implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "ARMRegisterInfo.h"
using namespace llvm;

void ARMRegisterInfo::anchor() { }

ARMRegisterInfo::ARMRegisterInfo() = default;
