//===- ReducerWorkItem.h - Wrapper for Module -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_REDUCE_REDUCERWORKITEM_H
#define LLVM_TOOLS_LLVM_REDUCE_REDUCERWORKITEM_H

#include "llvm/IR/Module.h"
#include <memory>

namespace llvm {
class LLVMContext;
class MachineModuleInfo;
class MemoryBufferRef;
class raw_ostream;
class TargetMachine;
class TestRunner;
struct BitcodeLTOInfo;

class ReducerWorkItem {
public:
  std::shared_ptr<Module> M;
  std::unique_ptr<BitcodeLTOInfo> LTOInfo;
  std::unique_ptr<MachineModuleInfo> MMI;

  ReducerWorkItem();
  ~ReducerWorkItem();
  ReducerWorkItem(ReducerWorkItem &) = delete;
  ReducerWorkItem(ReducerWorkItem &&) = default;

  bool isMIR() const { return MMI != nullptr; }

  LLVMContext &getContext() {
    return M->getContext();
  }

  Module &getModule() { return *M; }
  const Module &getModule() const { return *M; }
  operator Module &() const { return *M; }

  void print(raw_ostream &ROS, void *p = nullptr) const;
  bool verify(raw_fd_ostream *OS) const;
  std::unique_ptr<ReducerWorkItem> clone(const TargetMachine *TM) const;

  /// Return a number to indicate whether there was any reduction progress.
  uint64_t getComplexityScore() const {
    return isMIR() ? computeMIRComplexityScore() : computeIRComplexityScore();
  }

  void writeOutput(raw_ostream &OS, bool EmitBitcode) const;
  void readBitcode(MemoryBufferRef Data, LLVMContext &Ctx, StringRef ToolName);
  void writeBitcode(raw_ostream &OutStream) const;

  bool isReduced(const TestRunner &Test) const;

private:
  uint64_t computeIRComplexityScore() const;
  uint64_t computeMIRComplexityScore() const;
};

std::pair<std::unique_ptr<ReducerWorkItem>, bool>
parseReducerWorkItem(StringRef ToolName, StringRef Filename, LLVMContext &Ctxt,
                     std::unique_ptr<TargetMachine> &TM, bool IsMIR);
} // namespace llvm

#endif
