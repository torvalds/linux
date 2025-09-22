//===- ScheduleDAGMutation.h - MachineInstr Scheduling ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ScheduleDAGMutation class, which represents
// a target-specific mutation of the dependency graph for scheduling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULEDAGMUTATION_H
#define LLVM_CODEGEN_SCHEDULEDAGMUTATION_H

namespace llvm {

class ScheduleDAGInstrs;

/// Mutate the DAG as a postpass after normal DAG building.
class ScheduleDAGMutation {
  virtual void anchor();

public:
  virtual ~ScheduleDAGMutation() = default;

  virtual void apply(ScheduleDAGInstrs *DAG) = 0;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SCHEDULEDAGMUTATION_H
