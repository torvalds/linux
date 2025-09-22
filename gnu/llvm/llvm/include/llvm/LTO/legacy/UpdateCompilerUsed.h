//==------ UpdateCompilerUsed.h - LLVM Link Time Optimizer Utility --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a helper class to update llvm.compiler_used metadata.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LEGACY_UPDATECOMPILERUSED_H
#define LLVM_LTO_LEGACY_UPDATECOMPILERUSED_H

#include "llvm/ADT/StringSet.h"
#include "llvm/IR/GlobalValue.h"

namespace llvm {
class Module;
class TargetMachine;

/// Find all globals in \p TheModule that are referenced in
/// \p AsmUndefinedRefs, as well as the user-supplied functions definitions that
/// are also libcalls, and create or update the magic "llvm.compiler_used"
/// global in \p TheModule.
void updateCompilerUsed(Module &TheModule, const TargetMachine &TM,
                        const StringSet<> &AsmUndefinedRefs);
}

#endif // LLVM_LTO_LEGACY_UPDATECOMPILERUSED_H
