//===- StackProtector.h - Stack Protector Insertion -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass inserts stack protectors into functions which need them. A variable
// with a random value in it is stored onto the stack before the local variables
// are allocated. Upon exiting the block, the stored value is checked. If it's
// changed, then there was some sort of violation and the program aborts.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_STACKPROTECTOR_H
#define LLVM_CODEGEN_STACKPROTECTOR_H

#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

class BasicBlock;
class Function;
class Module;
class TargetLoweringBase;
class TargetMachine;

class SSPLayoutInfo {
  friend class StackProtectorPass;
  friend class SSPLayoutAnalysis;
  friend class StackProtector;
  static constexpr unsigned DefaultSSPBufferSize = 8;

  /// A mapping of AllocaInsts to their required SSP layout.
  using SSPLayoutMap =
      DenseMap<const AllocaInst *, MachineFrameInfo::SSPLayoutKind>;

  /// Layout - Mapping of allocations to the required SSPLayoutKind.
  /// StackProtector analysis will update this map when determining if an
  /// AllocaInst triggers a stack protector.
  SSPLayoutMap Layout;

  /// The minimum size of buffers that will receive stack smashing
  /// protection when -fstack-protection is used.
  unsigned SSPBufferSize = DefaultSSPBufferSize;

  bool RequireStackProtector = false;

  // A prologue is generated.
  bool HasPrologue = false;

  // IR checking code is generated.
  bool HasIRCheck = false;

public:
  // Return true if StackProtector is supposed to be handled by SelectionDAG.
  bool shouldEmitSDCheck(const BasicBlock &BB) const;

  void copyToMachineFrameInfo(MachineFrameInfo &MFI) const;
};

class SSPLayoutAnalysis : public AnalysisInfoMixin<SSPLayoutAnalysis> {
  friend AnalysisInfoMixin<SSPLayoutAnalysis>;
  using SSPLayoutMap = SSPLayoutInfo::SSPLayoutMap;

  static AnalysisKey Key;

public:
  using Result = SSPLayoutInfo;

  Result run(Function &F, FunctionAnalysisManager &FAM);

  /// Check whether or not \p F needs a stack protector based upon the stack
  /// protector level.
  static bool requiresStackProtector(Function *F,
                                     SSPLayoutMap *Layout = nullptr);
};

class StackProtectorPass : public PassInfoMixin<StackProtectorPass> {
  const TargetMachine *TM;

public:
  explicit StackProtectorPass(const TargetMachine *TM) : TM(TM) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

class StackProtector : public FunctionPass {
private:
  /// A mapping of AllocaInsts to their required SSP layout.
  using SSPLayoutMap = SSPLayoutInfo::SSPLayoutMap;

  const TargetMachine *TM = nullptr;

  Function *F = nullptr;
  Module *M = nullptr;

  std::optional<DomTreeUpdater> DTU;

  SSPLayoutInfo LayoutInfo;

public:
  static char ID; // Pass identification, replacement for typeid.

  StackProtector();

  SSPLayoutInfo &getLayoutInfo() { return LayoutInfo; }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  // Return true if StackProtector is supposed to be handled by SelectionDAG.
  bool shouldEmitSDCheck(const BasicBlock &BB) const {
    return LayoutInfo.shouldEmitSDCheck(BB);
  }

  bool runOnFunction(Function &Fn) override;

  void copyToMachineFrameInfo(MachineFrameInfo &MFI) const {
    LayoutInfo.copyToMachineFrameInfo(MFI);
  }

  /// Check whether or not \p F needs a stack protector based upon the stack
  /// protector level.
  static bool requiresStackProtector(Function *F,
                                     SSPLayoutMap *Layout = nullptr) {
    return SSPLayoutAnalysis::requiresStackProtector(F, Layout);
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_STACKPROTECTOR_H
