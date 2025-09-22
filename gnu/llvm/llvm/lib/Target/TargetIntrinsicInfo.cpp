//===-- TargetIntrinsicInfo.cpp - Target Instruction Information ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TargetIntrinsicInfo class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/ADT/StringMapEntry.h"
#include "llvm/IR/Function.h"
using namespace llvm;

TargetIntrinsicInfo::TargetIntrinsicInfo() = default;

TargetIntrinsicInfo::~TargetIntrinsicInfo() = default;

unsigned TargetIntrinsicInfo::getIntrinsicID(const Function *F) const {
  const ValueName *ValName = F->getValueName();
  if (!ValName)
    return 0;
  return lookupName(ValName->getKeyData(), ValName->getKeyLength());
}
