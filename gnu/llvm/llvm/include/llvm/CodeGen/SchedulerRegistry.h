//===- llvm/CodeGen/SchedulerRegistry.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation for instruction scheduler function
// pass registry (RegisterScheduler).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULERREGISTRY_H
#define LLVM_CODEGEN_SCHEDULERREGISTRY_H

#include "llvm/CodeGen/MachinePassRegistry.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {

//===----------------------------------------------------------------------===//
///
/// RegisterScheduler class - Track the registration of instruction schedulers.
///
//===----------------------------------------------------------------------===//

class ScheduleDAGSDNodes;
class SelectionDAGISel;

class RegisterScheduler
    : public MachinePassRegistryNode<ScheduleDAGSDNodes *(*)(SelectionDAGISel *,
                                                             CodeGenOptLevel)> {
public:
  using FunctionPassCtor = ScheduleDAGSDNodes *(*)(SelectionDAGISel *,
                                                   CodeGenOptLevel);

  static MachinePassRegistry<FunctionPassCtor> Registry;

  RegisterScheduler(const char *N, const char *D, FunctionPassCtor C)
      : MachinePassRegistryNode(N, D, C) {
    Registry.Add(this);
  }
  ~RegisterScheduler() { Registry.Remove(this); }


  // Accessors.
  RegisterScheduler *getNext() const {
    return (RegisterScheduler *)MachinePassRegistryNode::getNext();
  }

  static RegisterScheduler *getList() {
    return (RegisterScheduler *)Registry.getList();
  }

  static void setListener(MachinePassRegistryListener<FunctionPassCtor> *L) {
    Registry.setListener(L);
  }
};

/// createBURRListDAGScheduler - This creates a bottom up register usage
/// reduction list scheduler.
ScheduleDAGSDNodes *createBURRListDAGScheduler(SelectionDAGISel *IS,
                                               CodeGenOptLevel OptLevel);

/// createSourceListDAGScheduler - This creates a bottom up list scheduler that
/// schedules nodes in source code order when possible.
ScheduleDAGSDNodes *createSourceListDAGScheduler(SelectionDAGISel *IS,
                                                 CodeGenOptLevel OptLevel);

/// createHybridListDAGScheduler - This creates a bottom up register pressure
/// aware list scheduler that make use of latency information to avoid stalls
/// for long latency instructions in low register pressure mode. In high
/// register pressure mode it schedules to reduce register pressure.
ScheduleDAGSDNodes *createHybridListDAGScheduler(SelectionDAGISel *IS,
                                                 CodeGenOptLevel);

/// createILPListDAGScheduler - This creates a bottom up register pressure
/// aware list scheduler that tries to increase instruction level parallelism
/// in low register pressure mode. In high register pressure mode it schedules
/// to reduce register pressure.
ScheduleDAGSDNodes *createILPListDAGScheduler(SelectionDAGISel *IS,
                                              CodeGenOptLevel);

/// createFastDAGScheduler - This creates a "fast" scheduler.
///
ScheduleDAGSDNodes *createFastDAGScheduler(SelectionDAGISel *IS,
                                           CodeGenOptLevel OptLevel);

/// createVLIWDAGScheduler - Scheduler for VLIW targets. This creates top down
/// DFA driven list scheduler with clustering heuristic to control
/// register pressure.
ScheduleDAGSDNodes *createVLIWDAGScheduler(SelectionDAGISel *IS,
                                           CodeGenOptLevel OptLevel);
/// createDefaultScheduler - This creates an instruction scheduler appropriate
/// for the target.
ScheduleDAGSDNodes *createDefaultScheduler(SelectionDAGISel *IS,
                                           CodeGenOptLevel OptLevel);

/// createDAGLinearizer - This creates a "no-scheduling" scheduler which
/// linearize the DAG using topological order.
ScheduleDAGSDNodes *createDAGLinearizer(SelectionDAGISel *IS,
                                        CodeGenOptLevel OptLevel);

} // end namespace llvm

#endif // LLVM_CODEGEN_SCHEDULERREGISTRY_H
