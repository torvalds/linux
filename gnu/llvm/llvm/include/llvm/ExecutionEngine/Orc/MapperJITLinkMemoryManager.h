//===--------------- MapperJITLinkMemoryManager.h -*- C++ -*---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements JITLinkMemoryManager using MemoryMapper
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MAPPERJITLINKMEMORYMANAGER_H
#define LLVM_EXECUTIONENGINE_ORC_MAPPERJITLINKMEMORYMANAGER_H

#include "llvm/ADT/IntervalMap.h"
#include "llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/MemoryMapper.h"

namespace llvm {
namespace orc {

class MapperJITLinkMemoryManager : public jitlink::JITLinkMemoryManager {
public:
  MapperJITLinkMemoryManager(size_t ReservationGranularity,
                             std::unique_ptr<MemoryMapper> Mapper);

  template <class MemoryMapperType, class... Args>
  static Expected<std::unique_ptr<MapperJITLinkMemoryManager>>
  CreateWithMapper(size_t ReservationGranularity, Args &&...A) {
    auto Mapper = MemoryMapperType::Create(std::forward<Args>(A)...);
    if (!Mapper)
      return Mapper.takeError();

    return std::make_unique<MapperJITLinkMemoryManager>(ReservationGranularity,
                                                        std::move(*Mapper));
  }

  void allocate(const jitlink::JITLinkDylib *JD, jitlink::LinkGraph &G,
                OnAllocatedFunction OnAllocated) override;
  // synchronous overload
  using JITLinkMemoryManager::allocate;

  void deallocate(std::vector<FinalizedAlloc> Allocs,
                  OnDeallocatedFunction OnDeallocated) override;
  // synchronous overload
  using JITLinkMemoryManager::deallocate;

private:
  class InFlightAlloc;

  std::mutex Mutex;

  // We reserve multiples of this from the executor address space
  size_t ReservationUnits;

  // Ranges that have been reserved in executor but not yet allocated
  using AvailableMemoryMap = IntervalMap<ExecutorAddr, bool>;
  AvailableMemoryMap::Allocator AMAllocator;
  IntervalMap<ExecutorAddr, bool> AvailableMemory;

  // Ranges that have been reserved in executor and already allocated
  DenseMap<ExecutorAddr, ExecutorAddrDiff> UsedMemory;

  std::unique_ptr<MemoryMapper> Mapper;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_MAPPERJITLINKMEMORYMANAGER_H
