//===--- JITLinkMemoryManager.cpp - JITLinkMemoryManager implementation ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Process.h"

#define DEBUG_TYPE "jitlink"

using namespace llvm;

namespace llvm {
namespace jitlink {

JITLinkMemoryManager::~JITLinkMemoryManager() = default;
JITLinkMemoryManager::InFlightAlloc::~InFlightAlloc() = default;

BasicLayout::BasicLayout(LinkGraph &G) : G(G) {

  for (auto &Sec : G.sections()) {
    // Skip empty sections, and sections with NoAlloc lifetime policies.
    if (Sec.blocks().empty() ||
        Sec.getMemLifetime() == orc::MemLifetime::NoAlloc)
      continue;

    auto &Seg = Segments[{Sec.getMemProt(), Sec.getMemLifetime()}];
    for (auto *B : Sec.blocks())
      if (LLVM_LIKELY(!B->isZeroFill()))
        Seg.ContentBlocks.push_back(B);
      else
        Seg.ZeroFillBlocks.push_back(B);
  }

  // Build Segments map.
  auto CompareBlocks = [](const Block *LHS, const Block *RHS) {
    // Sort by section, address and size
    if (LHS->getSection().getOrdinal() != RHS->getSection().getOrdinal())
      return LHS->getSection().getOrdinal() < RHS->getSection().getOrdinal();
    if (LHS->getAddress() != RHS->getAddress())
      return LHS->getAddress() < RHS->getAddress();
    return LHS->getSize() < RHS->getSize();
  };

  LLVM_DEBUG(dbgs() << "Generated BasicLayout for " << G.getName() << ":\n");
  for (auto &KV : Segments) {
    auto &Seg = KV.second;

    llvm::sort(Seg.ContentBlocks, CompareBlocks);
    llvm::sort(Seg.ZeroFillBlocks, CompareBlocks);

    for (auto *B : Seg.ContentBlocks) {
      Seg.ContentSize = alignToBlock(Seg.ContentSize, *B);
      Seg.ContentSize += B->getSize();
      Seg.Alignment = std::max(Seg.Alignment, Align(B->getAlignment()));
    }

    uint64_t SegEndOffset = Seg.ContentSize;
    for (auto *B : Seg.ZeroFillBlocks) {
      SegEndOffset = alignToBlock(SegEndOffset, *B);
      SegEndOffset += B->getSize();
      Seg.Alignment = std::max(Seg.Alignment, Align(B->getAlignment()));
    }
    Seg.ZeroFillSize = SegEndOffset - Seg.ContentSize;

    LLVM_DEBUG({
      dbgs() << "  Seg " << KV.first
             << ": content-size=" << formatv("{0:x}", Seg.ContentSize)
             << ", zero-fill-size=" << formatv("{0:x}", Seg.ZeroFillSize)
             << ", align=" << formatv("{0:x}", Seg.Alignment.value()) << "\n";
    });
  }
}

Expected<BasicLayout::ContiguousPageBasedLayoutSizes>
BasicLayout::getContiguousPageBasedLayoutSizes(uint64_t PageSize) {
  ContiguousPageBasedLayoutSizes SegsSizes;

  for (auto &KV : segments()) {
    auto &AG = KV.first;
    auto &Seg = KV.second;

    if (Seg.Alignment > PageSize)
      return make_error<StringError>("Segment alignment greater than page size",
                                     inconvertibleErrorCode());

    uint64_t SegSize = alignTo(Seg.ContentSize + Seg.ZeroFillSize, PageSize);
    if (AG.getMemLifetime() == orc::MemLifetime::Standard)
      SegsSizes.StandardSegs += SegSize;
    else
      SegsSizes.FinalizeSegs += SegSize;
  }

  return SegsSizes;
}

Error BasicLayout::apply() {
  for (auto &KV : Segments) {
    auto &Seg = KV.second;

    assert(!(Seg.ContentBlocks.empty() && Seg.ZeroFillBlocks.empty()) &&
           "Empty section recorded?");

    for (auto *B : Seg.ContentBlocks) {
      // Align addr and working-mem-offset.
      Seg.Addr = alignToBlock(Seg.Addr, *B);
      Seg.NextWorkingMemOffset = alignToBlock(Seg.NextWorkingMemOffset, *B);

      // Update block addr.
      B->setAddress(Seg.Addr);
      Seg.Addr += B->getSize();

      // Copy content to working memory, then update content to point at working
      // memory.
      memcpy(Seg.WorkingMem + Seg.NextWorkingMemOffset, B->getContent().data(),
             B->getSize());
      B->setMutableContent(
          {Seg.WorkingMem + Seg.NextWorkingMemOffset, B->getSize()});
      Seg.NextWorkingMemOffset += B->getSize();
    }

    for (auto *B : Seg.ZeroFillBlocks) {
      // Align addr.
      Seg.Addr = alignToBlock(Seg.Addr, *B);
      // Update block addr.
      B->setAddress(Seg.Addr);
      Seg.Addr += B->getSize();
    }

    Seg.ContentBlocks.clear();
    Seg.ZeroFillBlocks.clear();
  }

  return Error::success();
}

orc::shared::AllocActions &BasicLayout::graphAllocActions() {
  return G.allocActions();
}

void SimpleSegmentAlloc::Create(JITLinkMemoryManager &MemMgr,
                                const JITLinkDylib *JD, SegmentMap Segments,
                                OnCreatedFunction OnCreated) {

  static_assert(orc::AllocGroup::NumGroups == 32,
                "AllocGroup has changed. Section names below must be updated");
  StringRef AGSectionNames[] = {
      "__---.standard", "__R--.standard", "__-W-.standard", "__RW-.standard",
      "__--X.standard", "__R-X.standard", "__-WX.standard", "__RWX.standard",
      "__---.finalize", "__R--.finalize", "__-W-.finalize", "__RW-.finalize",
      "__--X.finalize", "__R-X.finalize", "__-WX.finalize", "__RWX.finalize"};

  auto G = std::make_unique<LinkGraph>("", Triple(), 0,
                                       llvm::endianness::native, nullptr);
  orc::AllocGroupSmallMap<Block *> ContentBlocks;

  orc::ExecutorAddr NextAddr(0x100000);
  for (auto &KV : Segments) {
    auto &AG = KV.first;
    auto &Seg = KV.second;

    assert(AG.getMemLifetime() != orc::MemLifetime::NoAlloc &&
           "NoAlloc segments are not supported by SimpleSegmentAlloc");

    auto AGSectionName =
        AGSectionNames[static_cast<unsigned>(AG.getMemProt()) |
                       static_cast<bool>(AG.getMemLifetime()) << 3];

    auto &Sec = G->createSection(AGSectionName, AG.getMemProt());
    Sec.setMemLifetime(AG.getMemLifetime());

    if (Seg.ContentSize != 0) {
      NextAddr =
          orc::ExecutorAddr(alignTo(NextAddr.getValue(), Seg.ContentAlign));
      auto &B =
          G->createMutableContentBlock(Sec, G->allocateBuffer(Seg.ContentSize),
                                       NextAddr, Seg.ContentAlign.value(), 0);
      ContentBlocks[AG] = &B;
      NextAddr += Seg.ContentSize;
    }
  }

  // GRef declared separately since order-of-argument-eval isn't specified.
  auto &GRef = *G;
  MemMgr.allocate(JD, GRef,
                  [G = std::move(G), ContentBlocks = std::move(ContentBlocks),
                   OnCreated = std::move(OnCreated)](
                      JITLinkMemoryManager::AllocResult Alloc) mutable {
                    if (!Alloc)
                      OnCreated(Alloc.takeError());
                    else
                      OnCreated(SimpleSegmentAlloc(std::move(G),
                                                   std::move(ContentBlocks),
                                                   std::move(*Alloc)));
                  });
}

Expected<SimpleSegmentAlloc>
SimpleSegmentAlloc::Create(JITLinkMemoryManager &MemMgr, const JITLinkDylib *JD,
                           SegmentMap Segments) {
  std::promise<MSVCPExpected<SimpleSegmentAlloc>> AllocP;
  auto AllocF = AllocP.get_future();
  Create(MemMgr, JD, std::move(Segments),
         [&](Expected<SimpleSegmentAlloc> Result) {
           AllocP.set_value(std::move(Result));
         });
  return AllocF.get();
}

SimpleSegmentAlloc::SimpleSegmentAlloc(SimpleSegmentAlloc &&) = default;
SimpleSegmentAlloc &
SimpleSegmentAlloc::operator=(SimpleSegmentAlloc &&) = default;
SimpleSegmentAlloc::~SimpleSegmentAlloc() = default;

SimpleSegmentAlloc::SegmentInfo
SimpleSegmentAlloc::getSegInfo(orc::AllocGroup AG) {
  auto I = ContentBlocks.find(AG);
  if (I != ContentBlocks.end()) {
    auto &B = *I->second;
    return {B.getAddress(), B.getAlreadyMutableContent()};
  }
  return {};
}

SimpleSegmentAlloc::SimpleSegmentAlloc(
    std::unique_ptr<LinkGraph> G,
    orc::AllocGroupSmallMap<Block *> ContentBlocks,
    std::unique_ptr<JITLinkMemoryManager::InFlightAlloc> Alloc)
    : G(std::move(G)), ContentBlocks(std::move(ContentBlocks)),
      Alloc(std::move(Alloc)) {}

class InProcessMemoryManager::IPInFlightAlloc
    : public JITLinkMemoryManager::InFlightAlloc {
public:
  IPInFlightAlloc(InProcessMemoryManager &MemMgr, LinkGraph &G, BasicLayout BL,
                  sys::MemoryBlock StandardSegments,
                  sys::MemoryBlock FinalizationSegments)
      : MemMgr(MemMgr), G(&G), BL(std::move(BL)),
        StandardSegments(std::move(StandardSegments)),
        FinalizationSegments(std::move(FinalizationSegments)) {}

  ~IPInFlightAlloc() {
    assert(!G && "InFlight alloc neither abandoned nor finalized");
  }

  void finalize(OnFinalizedFunction OnFinalized) override {

    // Apply memory protections to all segments.
    if (auto Err = applyProtections()) {
      OnFinalized(std::move(Err));
      return;
    }

    // Run finalization actions.
    auto DeallocActions = runFinalizeActions(G->allocActions());
    if (!DeallocActions) {
      OnFinalized(DeallocActions.takeError());
      return;
    }

    // Release the finalize segments slab.
    if (auto EC = sys::Memory::releaseMappedMemory(FinalizationSegments)) {
      OnFinalized(errorCodeToError(EC));
      return;
    }

#ifndef NDEBUG
    // Set 'G' to null to flag that we've been successfully finalized.
    // This allows us to assert at destruction time that a call has been made
    // to either finalize or abandon.
    G = nullptr;
#endif

    // Continue with finalized allocation.
    OnFinalized(MemMgr.createFinalizedAlloc(std::move(StandardSegments),
                                            std::move(*DeallocActions)));
  }

  void abandon(OnAbandonedFunction OnAbandoned) override {
    Error Err = Error::success();
    if (auto EC = sys::Memory::releaseMappedMemory(FinalizationSegments))
      Err = joinErrors(std::move(Err), errorCodeToError(EC));
    if (auto EC = sys::Memory::releaseMappedMemory(StandardSegments))
      Err = joinErrors(std::move(Err), errorCodeToError(EC));

#ifndef NDEBUG
    // Set 'G' to null to flag that we've been successfully finalized.
    // This allows us to assert at destruction time that a call has been made
    // to either finalize or abandon.
    G = nullptr;
#endif

    OnAbandoned(std::move(Err));
  }

private:
  Error applyProtections() {
    for (auto &KV : BL.segments()) {
      const auto &AG = KV.first;
      auto &Seg = KV.second;

      auto Prot = toSysMemoryProtectionFlags(AG.getMemProt());

      uint64_t SegSize =
          alignTo(Seg.ContentSize + Seg.ZeroFillSize, MemMgr.PageSize);
      sys::MemoryBlock MB(Seg.WorkingMem, SegSize);
      if (auto EC = sys::Memory::protectMappedMemory(MB, Prot))
        return errorCodeToError(EC);
      if (Prot & sys::Memory::MF_EXEC)
        sys::Memory::InvalidateInstructionCache(MB.base(), MB.allocatedSize());
    }
    return Error::success();
  }

  InProcessMemoryManager &MemMgr;
  LinkGraph *G;
  BasicLayout BL;
  sys::MemoryBlock StandardSegments;
  sys::MemoryBlock FinalizationSegments;
};

Expected<std::unique_ptr<InProcessMemoryManager>>
InProcessMemoryManager::Create() {
  if (auto PageSize = sys::Process::getPageSize())
    return std::make_unique<InProcessMemoryManager>(*PageSize);
  else
    return PageSize.takeError();
}

void InProcessMemoryManager::allocate(const JITLinkDylib *JD, LinkGraph &G,
                                      OnAllocatedFunction OnAllocated) {

  // FIXME: Just check this once on startup.
  if (!isPowerOf2_64((uint64_t)PageSize)) {
    OnAllocated(make_error<StringError>("Page size is not a power of 2",
                                        inconvertibleErrorCode()));
    return;
  }

  BasicLayout BL(G);

  /// Scan the request and calculate the group and total sizes.
  /// Check that segment size is no larger than a page.
  auto SegsSizes = BL.getContiguousPageBasedLayoutSizes(PageSize);
  if (!SegsSizes) {
    OnAllocated(SegsSizes.takeError());
    return;
  }

  /// Check that the total size requested (including zero fill) is not larger
  /// than a size_t.
  if (SegsSizes->total() > std::numeric_limits<size_t>::max()) {
    OnAllocated(make_error<JITLinkError>(
        "Total requested size " + formatv("{0:x}", SegsSizes->total()) +
        " for graph " + G.getName() + " exceeds address space"));
    return;
  }

  // Allocate one slab for the whole thing (to make sure everything is
  // in-range), then partition into standard and finalization blocks.
  //
  // FIXME: Make two separate allocations in the future to reduce
  // fragmentation: finalization segments will usually be a single page, and
  // standard segments are likely to be more than one page. Where multiple
  // allocations are in-flight at once (likely) the current approach will leave
  // a lot of single-page holes.
  sys::MemoryBlock Slab;
  sys::MemoryBlock StandardSegsMem;
  sys::MemoryBlock FinalizeSegsMem;
  {
    const sys::Memory::ProtectionFlags ReadWrite =
        static_cast<sys::Memory::ProtectionFlags>(sys::Memory::MF_READ |
                                                  sys::Memory::MF_WRITE);

    std::error_code EC;
    Slab = sys::Memory::allocateMappedMemory(SegsSizes->total(), nullptr,
                                             ReadWrite, EC);

    if (EC) {
      OnAllocated(errorCodeToError(EC));
      return;
    }

    // Zero-fill the whole slab up-front.
    memset(Slab.base(), 0, Slab.allocatedSize());

    StandardSegsMem = {Slab.base(),
                       static_cast<size_t>(SegsSizes->StandardSegs)};
    FinalizeSegsMem = {(void *)((char *)Slab.base() + SegsSizes->StandardSegs),
                       static_cast<size_t>(SegsSizes->FinalizeSegs)};
  }

  auto NextStandardSegAddr = orc::ExecutorAddr::fromPtr(StandardSegsMem.base());
  auto NextFinalizeSegAddr = orc::ExecutorAddr::fromPtr(FinalizeSegsMem.base());

  LLVM_DEBUG({
    dbgs() << "InProcessMemoryManager allocated:\n";
    if (SegsSizes->StandardSegs)
      dbgs() << formatv("  [ {0:x16} -- {1:x16} ]", NextStandardSegAddr,
                        NextStandardSegAddr + StandardSegsMem.allocatedSize())
             << " to stardard segs\n";
    else
      dbgs() << "  no standard segs\n";
    if (SegsSizes->FinalizeSegs)
      dbgs() << formatv("  [ {0:x16} -- {1:x16} ]", NextFinalizeSegAddr,
                        NextFinalizeSegAddr + FinalizeSegsMem.allocatedSize())
             << " to finalize segs\n";
    else
      dbgs() << "  no finalize segs\n";
  });

  // Build ProtMap, assign addresses.
  for (auto &KV : BL.segments()) {
    auto &AG = KV.first;
    auto &Seg = KV.second;

    auto &SegAddr = (AG.getMemLifetime() == orc::MemLifetime::Standard)
                        ? NextStandardSegAddr
                        : NextFinalizeSegAddr;

    Seg.WorkingMem = SegAddr.toPtr<char *>();
    Seg.Addr = SegAddr;

    SegAddr += alignTo(Seg.ContentSize + Seg.ZeroFillSize, PageSize);
  }

  if (auto Err = BL.apply()) {
    OnAllocated(std::move(Err));
    return;
  }

  OnAllocated(std::make_unique<IPInFlightAlloc>(*this, G, std::move(BL),
                                                std::move(StandardSegsMem),
                                                std::move(FinalizeSegsMem)));
}

void InProcessMemoryManager::deallocate(std::vector<FinalizedAlloc> Allocs,
                                        OnDeallocatedFunction OnDeallocated) {
  std::vector<sys::MemoryBlock> StandardSegmentsList;
  std::vector<std::vector<orc::shared::WrapperFunctionCall>> DeallocActionsList;

  {
    std::lock_guard<std::mutex> Lock(FinalizedAllocsMutex);
    for (auto &Alloc : Allocs) {
      auto *FA = Alloc.release().toPtr<FinalizedAllocInfo *>();
      StandardSegmentsList.push_back(std::move(FA->StandardSegments));
      DeallocActionsList.push_back(std::move(FA->DeallocActions));
      FA->~FinalizedAllocInfo();
      FinalizedAllocInfos.Deallocate(FA);
    }
  }

  Error DeallocErr = Error::success();

  while (!DeallocActionsList.empty()) {
    auto &DeallocActions = DeallocActionsList.back();
    auto &StandardSegments = StandardSegmentsList.back();

    /// Run any deallocate calls.
    while (!DeallocActions.empty()) {
      if (auto Err = DeallocActions.back().runWithSPSRetErrorMerged())
        DeallocErr = joinErrors(std::move(DeallocErr), std::move(Err));
      DeallocActions.pop_back();
    }

    /// Release the standard segments slab.
    if (auto EC = sys::Memory::releaseMappedMemory(StandardSegments))
      DeallocErr = joinErrors(std::move(DeallocErr), errorCodeToError(EC));

    DeallocActionsList.pop_back();
    StandardSegmentsList.pop_back();
  }

  OnDeallocated(std::move(DeallocErr));
}

JITLinkMemoryManager::FinalizedAlloc
InProcessMemoryManager::createFinalizedAlloc(
    sys::MemoryBlock StandardSegments,
    std::vector<orc::shared::WrapperFunctionCall> DeallocActions) {
  std::lock_guard<std::mutex> Lock(FinalizedAllocsMutex);
  auto *FA = FinalizedAllocInfos.Allocate<FinalizedAllocInfo>();
  new (FA) FinalizedAllocInfo(
      {std::move(StandardSegments), std::move(DeallocActions)});
  return FinalizedAlloc(orc::ExecutorAddr::fromPtr(FA));
}

} // end namespace jitlink
} // end namespace llvm
