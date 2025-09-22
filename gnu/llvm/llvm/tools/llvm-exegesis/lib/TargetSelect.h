//===-- TargetSelect.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// Utilities to handle the creation of the enabled exegesis target(s).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_TARGET_SELECT_H
#define LLVM_TOOLS_LLVM_EXEGESIS_TARGET_SELECT_H

#include "llvm/Config/llvm-config.h"

namespace llvm {
namespace exegesis {

// Forward declare all of the initialize methods for targets compiled in
#define LLVM_EXEGESIS(TargetName) void Initialize##TargetName##ExegesisTarget();
#include "llvm/Config/TargetExegesis.def"

// Initializes all exegesis targets compiled in.
inline void InitializeAllExegesisTargets() {
#define LLVM_EXEGESIS(TargetName) Initialize##TargetName##ExegesisTarget();
#include "llvm/Config/TargetExegesis.def"
}

} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_TARGET_SELECT_H
