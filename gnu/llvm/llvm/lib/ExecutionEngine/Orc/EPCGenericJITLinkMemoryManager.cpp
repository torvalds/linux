//===---- EPCGenericJITLinkMemoryManager.cpp -- Mem management via EPC ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/EPCGenericJITLinkMemoryManager.h"

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/Orc/LookupAndRecordAddrs.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcRTBridge.h"

#include <limits>

using namespace llvm::jitlink;

namespace llvm {
namespace orc {

class EPCGenericJITLinkMemoryManager::InFlightAlloc
    : public jitlink::JITLinkMemoryManager::InFlightAlloc {
public:

  // FIXME: The C++98 initializer is an attempt to work around compile failures
  // due to http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_defects.html#1397.
  // We should be able to switch this back to member initialization once that
  // issue is fixed.
  struct SegInfo {
    SegInfo() : WorkingMem(nullptr), ContentSize(0), ZeroFillSize(0) {}

    char *WorkingMem;
    ExecutorAddr Addr;
    uint64_t ContentSize;
    uint64_t ZeroFillSize;
  };

  using SegInfoMap = AllocGroupSmallMap<SegInfo>;

  InFlightAlloc(EPCGenericJITLinkMemoryManager &Parent, LinkGraph &G,
                ExecutorAddr AllocAddr, SegInfoMap Segs)
      : Parent(Parent), G(G), AllocAddr(AllocAddr), Segs(std::move(Segs)) {}

  void finalize(OnFinalizedFunction OnFinalize) override {
    tpctypes::FinalizeRequest FR;
    for (auto &KV : Segs) {
      assert(KV.second.ContentSize <= std::numeric_limits<size_t>::max());
      FR.Segments.push_back(tpctypes::SegFinalizeRequest{
          KV.first,
          KV.second.Addr,
          alignTo(KV.second.ContentSize + KV.second.ZeroFillSize,
                  Parent.EPC.getPageSize()),
          {KV.second.WorkingMem, static_cast<size_t>(KV.second.ContentSize)}});
    }

    // Transfer allocation actions.
    std::swap(FR.Actions, G.allocActions());

    Parent.EPC.callSPSWrapperAsync<
        rt::SPSSimpleExecutorMemoryManagerFinalizeSignature>(
        Parent.SAs.Finalize,
        [OnFinalize = std::move(OnFinalize), AllocAddr = this->AllocAddr](
            Error SerializationErr, Error FinalizeErr) mutable {
          // FIXME: Release abandoned alloc.
          if (SerializationErr) {
            cantFail(std::move(FinalizeErr));
            OnFinalize(std::move(SerializationErr));
          } else if (FinalizeErr)
            OnFinalize(std::move(FinalizeErr));
          else
            OnFinalize(FinalizedAlloc(AllocAddr));
        },
        Parent.SAs.Allocator, std::move(FR));
  }

  void abandon(OnAbandonedFunction OnAbandoned) override {
    // FIXME: Return memory to pool instead.
    Parent.EPC.callSPSWrapperAsync<
        rt::SPSSimpleExecutorMemoryManagerDeallocateSignature>(
        Parent.SAs.Deallocate,
        [OnAbandoned = std::move(OnAbandoned)](Error SerializationErr,
                                               Error DeallocateErr) mutable {
          if (SerializationErr) {
            cantFail(std::move(DeallocateErr));
            OnAbandoned(std::move(SerializationErr));
          } else
            OnAbandoned(std::move(DeallocateErr));
        },
        Parent.SAs.Allocator, ArrayRef<ExecutorAddr>(AllocAddr));
  }

private:
  EPCGenericJITLinkMemoryManager &Parent;
  LinkGraph &G;
  ExecutorAddr AllocAddr;
  SegInfoMap Segs;
};

void EPCGenericJITLinkMemoryManager::allocate(const JITLinkDylib *JD,
                                              LinkGraph &G,
                                              OnAllocatedFunction OnAllocated) {
  BasicLayout BL(G);

  auto Pages = BL.getContiguousPageBasedLayoutSizes(EPC.getPageSize());
  if (!Pages)
    return OnAllocated(Pages.takeError());

  EPC.callSPSWrapperAsync<rt::SPSSimpleExecutorMemoryManagerReserveSignature>(
      SAs.Reserve,
      [this, BL = std::move(BL), OnAllocated = std::move(OnAllocated)](
          Error SerializationErr, Expected<ExecutorAddr> AllocAddr) mutable {
        if (SerializationErr) {
          cantFail(AllocAddr.takeError());
          return OnAllocated(std::move(SerializationErr));
        }
        if (!AllocAddr)
          return OnAllocated(AllocAddr.takeError());

        completeAllocation(*AllocAddr, std::move(BL), std::move(OnAllocated));
      },
      SAs.Allocator, Pages->total());
}

void EPCGenericJITLinkMemoryManager::deallocate(
    std::vector<FinalizedAlloc> Allocs, OnDeallocatedFunction OnDeallocated) {
  EPC.callSPSWrapperAsync<
      rt::SPSSimpleExecutorMemoryManagerDeallocateSignature>(
      SAs.Deallocate,
      [OnDeallocated = std::move(OnDeallocated)](Error SerErr,
                                                 Error DeallocErr) mutable {
        if (SerErr) {
          cantFail(std::move(DeallocErr));
          OnDeallocated(std::move(SerErr));
        } else
          OnDeallocated(std::move(DeallocErr));
      },
      SAs.Allocator, Allocs);
  for (auto &A : Allocs)
    A.release();
}

void EPCGenericJITLinkMemoryManager::completeAllocation(
    ExecutorAddr AllocAddr, BasicLayout BL, OnAllocatedFunction OnAllocated) {

  InFlightAlloc::SegInfoMap SegInfos;

  ExecutorAddr NextSegAddr = AllocAddr;
  for (auto &KV : BL.segments()) {
    const auto &AG = KV.first;
    auto &Seg = KV.second;

    Seg.Addr = NextSegAddr;
    KV.second.WorkingMem = BL.getGraph().allocateBuffer(Seg.ContentSize).data();
    NextSegAddr += ExecutorAddrDiff(
        alignTo(Seg.ContentSize + Seg.ZeroFillSize, EPC.getPageSize()));

    auto &SegInfo = SegInfos[AG];
    SegInfo.ContentSize = Seg.ContentSize;
    SegInfo.ZeroFillSize = Seg.ZeroFillSize;
    SegInfo.Addr = Seg.Addr;
    SegInfo.WorkingMem = Seg.WorkingMem;
  }

  if (auto Err = BL.apply())
    return OnAllocated(std::move(Err));

  OnAllocated(std::make_unique<InFlightAlloc>(*this, BL.getGraph(), AllocAddr,
                                              std::move(SegInfos)));
}

} // end namespace orc
} // end namespace llvm
