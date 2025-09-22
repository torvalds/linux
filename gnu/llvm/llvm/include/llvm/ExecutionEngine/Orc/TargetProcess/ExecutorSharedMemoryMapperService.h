//===----------- ExecutorSharedMemoryMapperService.h ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_EXECUTORSHAREDMEMORYMAPPERSERVICE_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_EXECUTORSHAREDMEMORYMAPPERSERVICE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/ExecutorBootstrapService.h"

#include <atomic>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace llvm {
namespace orc {
namespace rt_bootstrap {

class ExecutorSharedMemoryMapperService final
    : public ExecutorBootstrapService {
public:
  ~ExecutorSharedMemoryMapperService(){};

  Expected<std::pair<ExecutorAddr, std::string>> reserve(uint64_t Size);
  Expected<ExecutorAddr> initialize(ExecutorAddr Reservation,
                                    tpctypes::SharedMemoryFinalizeRequest &FR);

  Error deinitialize(const std::vector<ExecutorAddr> &Bases);
  Error release(const std::vector<ExecutorAddr> &Bases);

  Error shutdown() override;
  void addBootstrapSymbols(StringMap<ExecutorAddr> &M) override;

private:
  struct Allocation {
    std::vector<shared::WrapperFunctionCall> DeinitializationActions;
  };
  using AllocationMap = DenseMap<ExecutorAddr, Allocation>;

  struct Reservation {
    size_t Size;
    std::vector<ExecutorAddr> Allocations;
#if defined(_WIN32)
    HANDLE SharedMemoryFile;
#endif
  };
  using ReservationMap = DenseMap<void *, Reservation>;

  static llvm::orc::shared::CWrapperFunctionResult
  reserveWrapper(const char *ArgData, size_t ArgSize);

  static llvm::orc::shared::CWrapperFunctionResult
  initializeWrapper(const char *ArgData, size_t ArgSize);

  static llvm::orc::shared::CWrapperFunctionResult
  deinitializeWrapper(const char *ArgData, size_t ArgSize);

  static llvm::orc::shared::CWrapperFunctionResult
  releaseWrapper(const char *ArgData, size_t ArgSize);

#if (defined(LLVM_ON_UNIX) && !defined(__ANDROID__)) || defined(_WIN32)
  std::atomic<int> SharedMemoryCount{0};
#endif

  std::mutex Mutex;
  ReservationMap Reservations;
  AllocationMap Allocations;
};

} // namespace rt_bootstrap
} // namespace orc
} // namespace llvm
#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_EXECUTORSHAREDMEMORYMAPPERSERVICE_H
