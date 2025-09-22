//===--- EPCIndirectionUtils.h - EPC based indirection utils ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Indirection utilities (stubs, trampolines, lazy call-throughs) that use the
// ExecutorProcessControl API to interact with the executor process.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EPCINDIRECTIONUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_EPCINDIRECTIONUTILS_H

#include "llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/LazyReexports.h"

#include <mutex>

namespace llvm {
namespace orc {

class ExecutorProcessControl;

/// Provides ExecutorProcessControl based indirect stubs, trampoline pool and
/// lazy call through manager.
class EPCIndirectionUtils {
  friend class EPCIndirectionUtilsAccess;

public:
  /// ABI support base class. Used to write resolver, stub, and trampoline
  /// blocks.
  class ABISupport {
  protected:
    ABISupport(unsigned PointerSize, unsigned TrampolineSize, unsigned StubSize,
               unsigned StubToPointerMaxDisplacement, unsigned ResolverCodeSize)
        : PointerSize(PointerSize), TrampolineSize(TrampolineSize),
          StubSize(StubSize),
          StubToPointerMaxDisplacement(StubToPointerMaxDisplacement),
          ResolverCodeSize(ResolverCodeSize) {}

  public:
    virtual ~ABISupport();

    unsigned getPointerSize() const { return PointerSize; }
    unsigned getTrampolineSize() const { return TrampolineSize; }
    unsigned getStubSize() const { return StubSize; }
    unsigned getStubToPointerMaxDisplacement() const {
      return StubToPointerMaxDisplacement;
    }
    unsigned getResolverCodeSize() const { return ResolverCodeSize; }

    virtual void writeResolverCode(char *ResolverWorkingMem,
                                   ExecutorAddr ResolverTargetAddr,
                                   ExecutorAddr ReentryFnAddr,
                                   ExecutorAddr ReentryCtxAddr) const = 0;

    virtual void writeTrampolines(char *TrampolineBlockWorkingMem,
                                  ExecutorAddr TrampolineBlockTragetAddr,
                                  ExecutorAddr ResolverAddr,
                                  unsigned NumTrampolines) const = 0;

    virtual void writeIndirectStubsBlock(
        char *StubsBlockWorkingMem, ExecutorAddr StubsBlockTargetAddress,
        ExecutorAddr PointersBlockTargetAddress, unsigned NumStubs) const = 0;

  private:
    unsigned PointerSize = 0;
    unsigned TrampolineSize = 0;
    unsigned StubSize = 0;
    unsigned StubToPointerMaxDisplacement = 0;
    unsigned ResolverCodeSize = 0;
  };

  /// Create using the given ABI class.
  template <typename ORCABI>
  static std::unique_ptr<EPCIndirectionUtils>
  CreateWithABI(ExecutorProcessControl &EPC);

  /// Create based on the ExecutorProcessControl triple.
  static Expected<std::unique_ptr<EPCIndirectionUtils>>
  Create(ExecutorProcessControl &EPC);

  /// Create based on the ExecutorProcessControl triple.
  static Expected<std::unique_ptr<EPCIndirectionUtils>>
  Create(ExecutionSession &ES) {
    return Create(ES.getExecutorProcessControl());
  }

  /// Return a reference to the ExecutorProcessControl object.
  ExecutorProcessControl &getExecutorProcessControl() const { return EPC; }

  /// Return a reference to the ABISupport object for this instance.
  ABISupport &getABISupport() const { return *ABI; }

  /// Release memory for resources held by this instance. This *must* be called
  /// prior to destruction of the class.
  Error cleanup();

  /// Write resolver code to the executor process and return its address.
  /// This must be called before any call to createTrampolinePool or
  /// createLazyCallThroughManager.
  Expected<ExecutorAddr> writeResolverBlock(ExecutorAddr ReentryFnAddr,
                                            ExecutorAddr ReentryCtxAddr);

  /// Returns the address of the Resolver block. Returns zero if the
  /// writeResolverBlock method has not previously been called.
  ExecutorAddr getResolverBlockAddress() const { return ResolverBlockAddr; }

  /// Create an IndirectStubsManager for the executor process.
  std::unique_ptr<IndirectStubsManager> createIndirectStubsManager();

  /// Create a TrampolinePool for the executor process.
  TrampolinePool &getTrampolinePool();

  /// Create a LazyCallThroughManager.
  /// This function should only be called once.
  LazyCallThroughManager &
  createLazyCallThroughManager(ExecutionSession &ES,
                               ExecutorAddr ErrorHandlerAddr);

  /// Create a LazyCallThroughManager for the executor process.
  LazyCallThroughManager &getLazyCallThroughManager() {
    assert(LCTM && "createLazyCallThroughManager must be called first");
    return *LCTM;
  }

private:
  using FinalizedAlloc = jitlink::JITLinkMemoryManager::FinalizedAlloc;

  struct IndirectStubInfo {
    IndirectStubInfo() = default;
    IndirectStubInfo(ExecutorAddr StubAddress, ExecutorAddr PointerAddress)
        : StubAddress(StubAddress), PointerAddress(PointerAddress) {}
    ExecutorAddr StubAddress;
    ExecutorAddr PointerAddress;
  };

  using IndirectStubInfoVector = std::vector<IndirectStubInfo>;

  /// Create an EPCIndirectionUtils instance.
  EPCIndirectionUtils(ExecutorProcessControl &EPC,
                      std::unique_ptr<ABISupport> ABI);

  Expected<IndirectStubInfoVector> getIndirectStubs(unsigned NumStubs);

  std::mutex EPCUIMutex;
  ExecutorProcessControl &EPC;
  std::unique_ptr<ABISupport> ABI;
  ExecutorAddr ResolverBlockAddr;
  FinalizedAlloc ResolverBlock;
  std::unique_ptr<TrampolinePool> TP;
  std::unique_ptr<LazyCallThroughManager> LCTM;

  std::vector<IndirectStubInfo> AvailableIndirectStubs;
  std::vector<FinalizedAlloc> IndirectStubAllocs;
};

/// This will call writeResolver on the given EPCIndirectionUtils instance
/// to set up re-entry via a function that will directly return the trampoline
/// landing address.
///
/// The EPCIndirectionUtils' LazyCallThroughManager must have been previously
/// created via EPCIndirectionUtils::createLazyCallThroughManager.
///
/// The EPCIndirectionUtils' writeResolver method must not have been previously
/// called.
///
/// This function is experimental and likely subject to revision.
Error setUpInProcessLCTMReentryViaEPCIU(EPCIndirectionUtils &EPCIU);

namespace detail {

template <typename ORCABI>
class ABISupportImpl : public EPCIndirectionUtils::ABISupport {
public:
  ABISupportImpl()
      : ABISupport(ORCABI::PointerSize, ORCABI::TrampolineSize,
                   ORCABI::StubSize, ORCABI::StubToPointerMaxDisplacement,
                   ORCABI::ResolverCodeSize) {}

  void writeResolverCode(char *ResolverWorkingMem,
                         ExecutorAddr ResolverTargetAddr,
                         ExecutorAddr ReentryFnAddr,
                         ExecutorAddr ReentryCtxAddr) const override {
    ORCABI::writeResolverCode(ResolverWorkingMem, ResolverTargetAddr,
                              ReentryFnAddr, ReentryCtxAddr);
  }

  void writeTrampolines(char *TrampolineBlockWorkingMem,
                        ExecutorAddr TrampolineBlockTargetAddr,
                        ExecutorAddr ResolverAddr,
                        unsigned NumTrampolines) const override {
    ORCABI::writeTrampolines(TrampolineBlockWorkingMem,
                             TrampolineBlockTargetAddr, ResolverAddr,
                             NumTrampolines);
  }

  void writeIndirectStubsBlock(char *StubsBlockWorkingMem,
                               ExecutorAddr StubsBlockTargetAddress,
                               ExecutorAddr PointersBlockTargetAddress,
                               unsigned NumStubs) const override {
    ORCABI::writeIndirectStubsBlock(StubsBlockWorkingMem,
                                    StubsBlockTargetAddress,
                                    PointersBlockTargetAddress, NumStubs);
  }
};

} // end namespace detail

template <typename ORCABI>
std::unique_ptr<EPCIndirectionUtils>
EPCIndirectionUtils::CreateWithABI(ExecutorProcessControl &EPC) {
  return std::unique_ptr<EPCIndirectionUtils>(new EPCIndirectionUtils(
      EPC, std::make_unique<detail::ABISupportImpl<ORCABI>>()));
}

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_EPCINDIRECTIONUTILS_H
