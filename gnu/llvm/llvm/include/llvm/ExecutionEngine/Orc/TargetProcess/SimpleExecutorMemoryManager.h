//===---------------- SimpleExecutorMemoryManager.h -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A simple allocator class suitable for basic remote-JIT use.
//
// FIXME: The functionality in this file should be moved to the ORC runtime.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_SIMPLEEXECUTORMEMORYMANAGER_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_SIMPLEEXECUTORMEMORYMANAGER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/ExecutorBootstrapService.h"
#include "llvm/Support/Error.h"

#include <mutex>

namespace llvm {
namespace orc {
namespace rt_bootstrap {

/// Simple page-based allocator.
class SimpleExecutorMemoryManager : public ExecutorBootstrapService {
public:
  virtual ~SimpleExecutorMemoryManager();

  Expected<ExecutorAddr> allocate(uint64_t Size);
  Error finalize(tpctypes::FinalizeRequest &FR);
  Error deallocate(const std::vector<ExecutorAddr> &Bases);

  Error shutdown() override;
  void addBootstrapSymbols(StringMap<ExecutorAddr> &M) override;

private:
  struct Allocation {
    size_t Size = 0;
    std::vector<shared::WrapperFunctionCall> DeallocationActions;
  };

  using AllocationsMap = DenseMap<void *, Allocation>;

  Error deallocateImpl(void *Base, Allocation &A);

  static llvm::orc::shared::CWrapperFunctionResult
  reserveWrapper(const char *ArgData, size_t ArgSize);

  static llvm::orc::shared::CWrapperFunctionResult
  finalizeWrapper(const char *ArgData, size_t ArgSize);

  static llvm::orc::shared::CWrapperFunctionResult
  deallocateWrapper(const char *ArgData, size_t ArgSize);

  std::mutex M;
  AllocationsMap Allocations;
};

} // end namespace rt_bootstrap
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_SIMPLEEXECUTORMEMORYMANAGER_H
