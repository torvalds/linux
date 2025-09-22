//===- EPCGenericJITLinkMemoryManager.h - EPC-based mem manager -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements JITLinkMemoryManager by making remove calls via
// ExecutorProcessControl::callWrapperAsync.
//
// This simplifies the implementaton of new ExecutorProcessControl instances,
// as this implementation will always work (at the cost of some performance
// overhead for the calls).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EPCGENERICJITLINKMEMORYMANAGER_H
#define LLVM_EXECUTIONENGINE_ORC_EPCGENERICJITLINKMEMORYMANAGER_H

#include "llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/Core.h"

namespace llvm {
namespace orc {

class EPCGenericJITLinkMemoryManager : public jitlink::JITLinkMemoryManager {
public:
  /// Function addresses for memory access.
  struct SymbolAddrs {
    ExecutorAddr Allocator;
    ExecutorAddr Reserve;
    ExecutorAddr Finalize;
    ExecutorAddr Deallocate;
  };

  /// Create an EPCGenericJITLinkMemoryManager instance from a given set of
  /// function addrs.
  EPCGenericJITLinkMemoryManager(ExecutorProcessControl &EPC, SymbolAddrs SAs)
      : EPC(EPC), SAs(SAs) {}

  void allocate(const jitlink::JITLinkDylib *JD, jitlink::LinkGraph &G,
                OnAllocatedFunction OnAllocated) override;

  // Use overloads from base class.
  using JITLinkMemoryManager::allocate;

  void deallocate(std::vector<FinalizedAlloc> Allocs,
                  OnDeallocatedFunction OnDeallocated) override;

  // Use overloads from base class.
  using JITLinkMemoryManager::deallocate;

private:
  class InFlightAlloc;

  void completeAllocation(ExecutorAddr AllocAddr, jitlink::BasicLayout BL,
                          OnAllocatedFunction OnAllocated);

  ExecutorProcessControl &EPC;
  SymbolAddrs SAs;
};

namespace shared {

/// FIXME: This specialization should be moved into TargetProcessControlTypes.h
///        (or wherever those types get merged to) once ORC depends on JITLink.
template <>
class SPSSerializationTraits<SPSExecutorAddr,
                             jitlink::JITLinkMemoryManager::FinalizedAlloc> {
public:
  static size_t size(const jitlink::JITLinkMemoryManager::FinalizedAlloc &FA) {
    return SPSArgList<SPSExecutorAddr>::size(ExecutorAddr(FA.getAddress()));
  }

  static bool
  serialize(SPSOutputBuffer &OB,
            const jitlink::JITLinkMemoryManager::FinalizedAlloc &FA) {
    return SPSArgList<SPSExecutorAddr>::serialize(
        OB, ExecutorAddr(FA.getAddress()));
  }

  static bool deserialize(SPSInputBuffer &IB,
                          jitlink::JITLinkMemoryManager::FinalizedAlloc &FA) {
    ExecutorAddr A;
    if (!SPSArgList<SPSExecutorAddr>::deserialize(IB, A))
      return false;
    FA = jitlink::JITLinkMemoryManager::FinalizedAlloc(A);
    return true;
  }
};

} // end namespace shared
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_EPCGENERICJITLINKMEMORYMANAGER_H
