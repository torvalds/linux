//===-- llvm/CodeGen/MachineModuleInfo.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEMODULESLOTTRACKER_H
#define LLVM_CODEGEN_MACHINEMODULESLOTTRACKER_H

#include "llvm/IR/ModuleSlotTracker.h"

namespace llvm {

class AbstractSlotTrackerStorage;
class Function;
class MachineModuleInfo;
class MachineFunction;
class Module;

class MachineModuleSlotTracker : public ModuleSlotTracker {
  const Function &TheFunction;
  const MachineModuleInfo &TheMMI;
  unsigned MDNStartSlot = 0, MDNEndSlot = 0;

  void processMachineFunctionMetadata(AbstractSlotTrackerStorage *AST,
                                      const MachineFunction &MF);
  void processMachineModule(AbstractSlotTrackerStorage *AST, const Module *M,
                            bool ShouldInitializeAllMetadata);
  void processMachineFunction(AbstractSlotTrackerStorage *AST,
                              const Function *F,
                              bool ShouldInitializeAllMetadata);

public:
  MachineModuleSlotTracker(const MachineFunction *MF,
                           bool ShouldInitializeAllMetadata = true);
  ~MachineModuleSlotTracker();

  void collectMachineMDNodes(MachineMDNodeListType &L) const;
};

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINEMODULESLOTTRACKER_H
