//===- SimpleExecuorMemoryManagare.cpp - Simple executor-side memory mgmt -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/TargetProcess/SimpleExecutorMemoryManager.h"

#include "llvm/ExecutionEngine/Orc/Shared/OrcRTBridge.h"
#include "llvm/Support/FormatVariadic.h"

#define DEBUG_TYPE "orc"

namespace llvm {
namespace orc {
namespace rt_bootstrap {

SimpleExecutorMemoryManager::~SimpleExecutorMemoryManager() {
  assert(Allocations.empty() && "shutdown not called?");
}

Expected<ExecutorAddr> SimpleExecutorMemoryManager::allocate(uint64_t Size) {
  std::error_code EC;
  auto MB = sys::Memory::allocateMappedMemory(
      Size, nullptr, sys::Memory::MF_READ | sys::Memory::MF_WRITE, EC);
  if (EC)
    return errorCodeToError(EC);
  std::lock_guard<std::mutex> Lock(M);
  assert(!Allocations.count(MB.base()) && "Duplicate allocation addr");
  Allocations[MB.base()].Size = Size;
  return ExecutorAddr::fromPtr(MB.base());
}

Error SimpleExecutorMemoryManager::finalize(tpctypes::FinalizeRequest &FR) {
  ExecutorAddr Base(~0ULL);
  std::vector<shared::WrapperFunctionCall> DeallocationActions;
  size_t SuccessfulFinalizationActions = 0;

  if (FR.Segments.empty()) {
    // NOTE: Finalizing nothing is currently a no-op. Should it be an error?
    if (FR.Actions.empty())
      return Error::success();
    else
      return make_error<StringError>("Finalization actions attached to empty "
                                     "finalization request",
                                     inconvertibleErrorCode());
  }

  for (auto &Seg : FR.Segments)
    Base = std::min(Base, Seg.Addr);

  for (auto &ActPair : FR.Actions)
    if (ActPair.Dealloc)
      DeallocationActions.push_back(ActPair.Dealloc);

  // Get the Allocation for this finalization.
  size_t AllocSize = 0;
  {
    std::lock_guard<std::mutex> Lock(M);
    auto I = Allocations.find(Base.toPtr<void *>());
    if (I == Allocations.end())
      return make_error<StringError>("Attempt to finalize unrecognized "
                                     "allocation " +
                                         formatv("{0:x}", Base.getValue()),
                                     inconvertibleErrorCode());
    AllocSize = I->second.Size;
    I->second.DeallocationActions = std::move(DeallocationActions);
  }
  ExecutorAddr AllocEnd = Base + ExecutorAddrDiff(AllocSize);

  // Bail-out function: this will run deallocation actions corresponding to any
  // completed finalization actions, then deallocate memory.
  auto BailOut = [&](Error Err) {
    std::pair<void *, Allocation> AllocToDestroy;

    // Get allocation to destroy.
    {
      std::lock_guard<std::mutex> Lock(M);
      auto I = Allocations.find(Base.toPtr<void *>());

      // Check for missing allocation (effective a double free).
      if (I == Allocations.end())
        return joinErrors(
            std::move(Err),
            make_error<StringError>("No allocation entry found "
                                    "for " +
                                        formatv("{0:x}", Base.getValue()),
                                    inconvertibleErrorCode()));
      AllocToDestroy = std::move(*I);
      Allocations.erase(I);
    }

    // Run deallocation actions for all completed finalization actions.
    while (SuccessfulFinalizationActions)
      Err =
          joinErrors(std::move(Err), FR.Actions[--SuccessfulFinalizationActions]
                                         .Dealloc.runWithSPSRetErrorMerged());

    // Deallocate memory.
    sys::MemoryBlock MB(AllocToDestroy.first, AllocToDestroy.second.Size);
    if (auto EC = sys::Memory::releaseMappedMemory(MB))
      Err = joinErrors(std::move(Err), errorCodeToError(EC));

    return Err;
  };

  // Copy content and apply permissions.
  for (auto &Seg : FR.Segments) {

    // Check segment ranges.
    if (LLVM_UNLIKELY(Seg.Size < Seg.Content.size()))
      return BailOut(make_error<StringError>(
          formatv("Segment {0:x} content size ({1:x} bytes) "
                  "exceeds segment size ({2:x} bytes)",
                  Seg.Addr.getValue(), Seg.Content.size(), Seg.Size),
          inconvertibleErrorCode()));
    ExecutorAddr SegEnd = Seg.Addr + ExecutorAddrDiff(Seg.Size);
    if (LLVM_UNLIKELY(Seg.Addr < Base || SegEnd > AllocEnd))
      return BailOut(make_error<StringError>(
          formatv("Segment {0:x} -- {1:x} crosses boundary of "
                  "allocation {2:x} -- {3:x}",
                  Seg.Addr.getValue(), SegEnd.getValue(), Base.getValue(),
                  AllocEnd.getValue()),
          inconvertibleErrorCode()));

    char *Mem = Seg.Addr.toPtr<char *>();
    if (!Seg.Content.empty())
      memcpy(Mem, Seg.Content.data(), Seg.Content.size());
    memset(Mem + Seg.Content.size(), 0, Seg.Size - Seg.Content.size());
    assert(Seg.Size <= std::numeric_limits<size_t>::max());
    if (auto EC = sys::Memory::protectMappedMemory(
            {Mem, static_cast<size_t>(Seg.Size)},
            toSysMemoryProtectionFlags(Seg.RAG.Prot)))
      return BailOut(errorCodeToError(EC));
    if ((Seg.RAG.Prot & MemProt::Exec) == MemProt::Exec)
      sys::Memory::InvalidateInstructionCache(Mem, Seg.Size);
  }

  // Run finalization actions.
  for (auto &ActPair : FR.Actions) {
    if (auto Err = ActPair.Finalize.runWithSPSRetErrorMerged())
      return BailOut(std::move(Err));
    ++SuccessfulFinalizationActions;
  }

  return Error::success();
}

Error SimpleExecutorMemoryManager::deallocate(
    const std::vector<ExecutorAddr> &Bases) {
  std::vector<std::pair<void *, Allocation>> AllocPairs;
  AllocPairs.reserve(Bases.size());

  // Get allocation to destroy.
  Error Err = Error::success();
  {
    std::lock_guard<std::mutex> Lock(M);
    for (auto &Base : Bases) {
      auto I = Allocations.find(Base.toPtr<void *>());

      // Check for missing allocation (effective a double free).
      if (I != Allocations.end()) {
        AllocPairs.push_back(std::move(*I));
        Allocations.erase(I);
      } else
        Err = joinErrors(
            std::move(Err),
            make_error<StringError>("No allocation entry found "
                                    "for " +
                                        formatv("{0:x}", Base.getValue()),
                                    inconvertibleErrorCode()));
    }
  }

  while (!AllocPairs.empty()) {
    auto &P = AllocPairs.back();
    Err = joinErrors(std::move(Err), deallocateImpl(P.first, P.second));
    AllocPairs.pop_back();
  }

  return Err;
}

Error SimpleExecutorMemoryManager::shutdown() {

  AllocationsMap AM;
  {
    std::lock_guard<std::mutex> Lock(M);
    AM = std::move(Allocations);
  }

  Error Err = Error::success();
  for (auto &KV : AM)
    Err = joinErrors(std::move(Err), deallocateImpl(KV.first, KV.second));
  return Err;
}

void SimpleExecutorMemoryManager::addBootstrapSymbols(
    StringMap<ExecutorAddr> &M) {
  M[rt::SimpleExecutorMemoryManagerInstanceName] = ExecutorAddr::fromPtr(this);
  M[rt::SimpleExecutorMemoryManagerReserveWrapperName] =
      ExecutorAddr::fromPtr(&reserveWrapper);
  M[rt::SimpleExecutorMemoryManagerFinalizeWrapperName] =
      ExecutorAddr::fromPtr(&finalizeWrapper);
  M[rt::SimpleExecutorMemoryManagerDeallocateWrapperName] =
      ExecutorAddr::fromPtr(&deallocateWrapper);
}

Error SimpleExecutorMemoryManager::deallocateImpl(void *Base, Allocation &A) {
  Error Err = Error::success();

  while (!A.DeallocationActions.empty()) {
    Err = joinErrors(std::move(Err),
                     A.DeallocationActions.back().runWithSPSRetErrorMerged());
    A.DeallocationActions.pop_back();
  }

  sys::MemoryBlock MB(Base, A.Size);
  if (auto EC = sys::Memory::releaseMappedMemory(MB))
    Err = joinErrors(std::move(Err), errorCodeToError(EC));

  return Err;
}

llvm::orc::shared::CWrapperFunctionResult
SimpleExecutorMemoryManager::reserveWrapper(const char *ArgData,
                                            size_t ArgSize) {
  return shared::WrapperFunction<
             rt::SPSSimpleExecutorMemoryManagerReserveSignature>::
      handle(ArgData, ArgSize,
             shared::makeMethodWrapperHandler(
                 &SimpleExecutorMemoryManager::allocate))
          .release();
}

llvm::orc::shared::CWrapperFunctionResult
SimpleExecutorMemoryManager::finalizeWrapper(const char *ArgData,
                                             size_t ArgSize) {
  return shared::WrapperFunction<
             rt::SPSSimpleExecutorMemoryManagerFinalizeSignature>::
      handle(ArgData, ArgSize,
             shared::makeMethodWrapperHandler(
                 &SimpleExecutorMemoryManager::finalize))
          .release();
}

llvm::orc::shared::CWrapperFunctionResult
SimpleExecutorMemoryManager::deallocateWrapper(const char *ArgData,
                                               size_t ArgSize) {
  return shared::WrapperFunction<
             rt::SPSSimpleExecutorMemoryManagerDeallocateSignature>::
      handle(ArgData, ArgSize,
             shared::makeMethodWrapperHandler(
                 &SimpleExecutorMemoryManager::deallocate))
          .release();
}

} // namespace rt_bootstrap
} // end namespace orc
} // end namespace llvm
