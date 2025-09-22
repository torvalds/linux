//===- MachineConvergenceVerifier.h - Verify convergencectrl ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file declares the MIR specialization of the GenericConvergenceVerifier
/// template.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINECONVERGENCEVERIFIER_H
#define LLVM_CODEGEN_MACHINECONVERGENCEVERIFIER_H

#include "llvm/ADT/GenericConvergenceVerifier.h"
#include "llvm/CodeGen/MachineSSAContext.h"

namespace llvm {

using MachineConvergenceVerifier =
    GenericConvergenceVerifier<MachineSSAContext>;

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINECONVERGENCEVERIFIER_H
