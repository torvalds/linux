//===-- ARMRegisterInfo.h - ARM Register Information Impl -------*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_ARM_ARMREGISTERINFO_H
#define LLVM_LIB_TARGET_ARM_ARMREGISTERINFO_H

#include "ARMBaseRegisterInfo.h"

namespace llvm {

struct ARMRegisterInfo : public ARMBaseRegisterInfo {
  virtual void anchor();
public:
  ARMRegisterInfo();
};

} // end namespace llvm

#endif
