//===- MemoryMapper.h - Cross-process memory mapper -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Cross-process (and in-process) memory mapping and transfer
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MEMORYMAPPER_H
#define LLVM_EXECUTIONENGINE_ORC_MEMORYMAPPER_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Shared/MemoryFlags.h"
#include "llvm/Support/Process.h"

#include <mutex>

namespace llvm {
namespace orc {

/// Manages mapping, content transfer and protections for JIT memory
class MemoryMapper {
public:
  /// Represents a single allocation containing multiple segments and
  /// initialization and deinitialization actions
  struct AllocInfo {
    struct SegInfo {
      ExecutorAddrDiff Offset;
      const char *WorkingMem;
      size_t ContentSize;
      size_t ZeroFillSize;
      AllocGroup AG;
    };

    ExecutorAddr MappingBase;
    std::vector<SegInfo> Segments;
    shared::AllocActions Actions;
  };

  using OnReservedFunction = unique_function<void(Expected<ExecutorAddrRange>)>;

  // Page size of the target process
  virtual unsigned int getPageSize() = 0;

  /// Reserves address space in executor process
  virtual void reserve(size_t NumBytes, OnReservedFunction OnReserved) = 0;

  /// Provides working memory
  virtual char *prepare(ExecutorAddr Addr, size_t ContentSize) = 0;

  using OnInitializedFunction = unique_function<void(Expected<ExecutorAddr>)>;

  /// Ensures executor memory is synchronized with working copy memory, sends
  /// functions to be called after initilization and before deinitialization and
  /// applies memory protections
  /// Returns a unique address identifying the allocation. This address should
  /// be passed to deinitialize to run deallocation actions (and reset
  /// permissions where possible).
  virtual void initialize(AllocInfo &AI,
                          OnInitializedFunction OnInitialized) = 0;

  using OnDeinitializedFunction = unique_function<void(Error)>;

  /// Runs previously specified deinitialization actions
  /// Executor addresses returned by initialize should be passed
  virtual void deinitialize(ArrayRef<ExecutorAddr> Allocations,
                            OnDeinitializedFunction OnDeInitialized) = 0;

  using OnReleasedFunction = unique_function<void(Error)>;

  /// Release address space acquired through reserve()
  virtual void release(ArrayRef<ExecutorAddr> Reservations,
                       OnReleasedFunction OnRelease) = 0;

  virtual ~MemoryMapper();
};

class InProcessMemoryMapper : public MemoryMapper {
public:
  InProcessMemoryMapper(size_t PageSize);

  static Expected<std::unique_ptr<InProcessMemoryMapper>> Create();

  unsigned int getPageSize() override { return PageSize; }

  void reserve(size_t NumBytes, OnReservedFunction OnReserved) override;

  void initialize(AllocInfo &AI, OnInitializedFunction OnInitialized) override;

  char *prepare(ExecutorAddr Addr, size_t ContentSize) override;

  void deinitialize(ArrayRef<ExecutorAddr> Allocations,
                    OnDeinitializedFunction OnDeInitialized) override;

  void release(ArrayRef<ExecutorAddr> Reservations,
               OnReleasedFunction OnRelease) override;

  ~InProcessMemoryMapper() override;

private:
  struct Allocation {
    size_t Size;
    std::vector<shared::WrapperFunctionCall> DeinitializationActions;
  };
  using AllocationMap = DenseMap<ExecutorAddr, Allocation>;

  struct Reservation {
    size_t Size;
    std::vector<ExecutorAddr> Allocations;
  };
  using ReservationMap = DenseMap<void *, Reservation>;

  std::mutex Mutex;
  ReservationMap Reservations;
  AllocationMap Allocations;

  size_t PageSize;
};

class SharedMemoryMapper final : public MemoryMapper {
public:
  struct SymbolAddrs {
    ExecutorAddr Instance;
    ExecutorAddr Reserve;
    ExecutorAddr Initialize;
    ExecutorAddr Deinitialize;
    ExecutorAddr Release;
  };

  SharedMemoryMapper(ExecutorProcessControl &EPC, SymbolAddrs SAs,
                     size_t PageSize);

  static Expected<std::unique_ptr<SharedMemoryMapper>>
  Create(ExecutorProcessControl &EPC, SymbolAddrs SAs);

  unsigned int getPageSize() override { return PageSize; }

  void reserve(size_t NumBytes, OnReservedFunction OnReserved) override;

  char *prepare(ExecutorAddr Addr, size_t ContentSize) override;

  void initialize(AllocInfo &AI, OnInitializedFunction OnInitialized) override;

  void deinitialize(ArrayRef<ExecutorAddr> Allocations,
                    OnDeinitializedFunction OnDeInitialized) override;

  void release(ArrayRef<ExecutorAddr> Reservations,
               OnReleasedFunction OnRelease) override;

  ~SharedMemoryMapper() override;

private:
  struct Reservation {
    void *LocalAddr;
    size_t Size;
  };

  ExecutorProcessControl &EPC;
  SymbolAddrs SAs;

  std::mutex Mutex;

  std::map<ExecutorAddr, Reservation> Reservations;

  size_t PageSize;
};

} // namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_MEMORYMAPPER_H
