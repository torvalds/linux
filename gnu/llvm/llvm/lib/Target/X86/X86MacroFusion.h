//===- X86MacroFusion.h - X86 Macro Fusion --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file contains the X86 definition of the DAG scheduling mutation
///  to pair instructions back to back.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86MACROFUSION_H
#define LLVM_LIB_TARGET_X86_X86MACROFUSION_H

#include <memory>

namespace llvm {

class ScheduleDAGMutation;

/// Note that you have to add:
///   DAG.addMutation(createX86MacroFusionDAGMutation());
/// to X86PassConfig::createMachineScheduler() to have an effect.
std::unique_ptr<ScheduleDAGMutation>
createX86MacroFusionDAGMutation();

} // end namespace llvm

#endif
