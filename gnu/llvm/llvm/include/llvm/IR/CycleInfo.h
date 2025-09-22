//===- CycleInfo.h - Cycle Info for LLVM IR -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file declares the LLVM IR specialization of the GenericCycle
/// templates.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CYCLEINFO_H
#define LLVM_IR_CYCLEINFO_H

#include "llvm/ADT/GenericCycleInfo.h"
#include "llvm/IR/SSAContext.h"

namespace llvm {

using CycleInfo = GenericCycleInfo<SSAContext>;
using Cycle = CycleInfo::CycleT;

} // namespace llvm

#endif // LLVM_IR_CYCLEINFO_H
