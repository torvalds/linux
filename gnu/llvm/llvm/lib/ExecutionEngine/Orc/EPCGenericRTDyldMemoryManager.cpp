//===----- EPCGenericRTDyldMemoryManager.cpp - EPC-bbasde MemMgr -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/EPCGenericRTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/EPCGenericMemoryAccess.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcRTBridge.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/FormatVariadic.h"

#define DEBUG_TYPE "orc"

using namespace llvm::orc::shared;

namespace llvm {
namespace orc {

Expected<std::unique_ptr<EPCGenericRTDyldMemoryManager>>
EPCGenericRTDyldMemoryManager::CreateWithDefaultBootstrapSymbols(
    ExecutorProcessControl &EPC) {
  SymbolAddrs SAs;
  if (auto Err = EPC.getBootstrapSymbols(
          {{SAs.Instance, rt::SimpleExecutorMemoryManagerInstanceName},
           {SAs.Reserve, rt::SimpleExecutorMemoryManagerReserveWrapperName},
           {SAs.Finalize, rt::SimpleExecutorMemoryManagerFinalizeWrapperName},
           {SAs.Deallocate,
            rt::SimpleExecutorMemoryManagerDeallocateWrapperName},
           {SAs.RegisterEHFrame, rt::RegisterEHFrameSectionWrapperName},
           {SAs.DeregisterEHFrame, rt::DeregisterEHFrameSectionWrapperName}}))
    return std::move(Err);
  return std::make_unique<EPCGenericRTDyldMemoryManager>(EPC, std::move(SAs));
}

EPCGenericRTDyldMemoryManager::EPCGenericRTDyldMemoryManager(
    ExecutorProcessControl &EPC, SymbolAddrs SAs)
    : EPC(EPC), SAs(std::move(SAs)) {
  LLVM_DEBUG(dbgs() << "Created remote allocator " << (void *)this << "\n");
}

EPCGenericRTDyldMemoryManager::~EPCGenericRTDyldMemoryManager() {
  LLVM_DEBUG(dbgs() << "Destroyed remote allocator " << (void *)this << "\n");
  if (!ErrMsg.empty())
    errs() << "Destroying with existing errors:\n" << ErrMsg << "\n";

  Error Err = Error::success();
  if (auto Err2 = EPC.callSPSWrapper<
                  rt::SPSSimpleExecutorMemoryManagerDeallocateSignature>(
          SAs.Reserve, Err, SAs.Instance, FinalizedAllocs)) {
    // FIXME: Report errors through EPC once that functionality is available.
    logAllUnhandledErrors(std::move(Err2), errs(), "");
    return;
  }

  if (Err)
    logAllUnhandledErrors(std::move(Err), errs(), "");
}

uint8_t *EPCGenericRTDyldMemoryManager::allocateCodeSection(
    uintptr_t Size, unsigned Alignment, unsigned SectionID,
    StringRef SectionName) {
  std::lock_guard<std::mutex> Lock(M);
  LLVM_DEBUG({
    dbgs() << "Allocator " << (void *)this << " allocating code section "
           << SectionName << ": size = " << formatv("{0:x}", Size)
           << " bytes, alignment = " << Alignment << "\n";
  });
  auto &Seg = Unmapped.back().CodeAllocs;
  Seg.emplace_back(Size, Alignment);
  return reinterpret_cast<uint8_t *>(
      alignAddr(Seg.back().Contents.get(), Align(Alignment)));
}

uint8_t *EPCGenericRTDyldMemoryManager::allocateDataSection(
    uintptr_t Size, unsigned Alignment, unsigned SectionID,
    StringRef SectionName, bool IsReadOnly) {
  std::lock_guard<std::mutex> Lock(M);
  LLVM_DEBUG({
    dbgs() << "Allocator " << (void *)this << " allocating "
           << (IsReadOnly ? "ro" : "rw") << "-data section " << SectionName
           << ": size = " << formatv("{0:x}", Size) << " bytes, alignment "
           << Alignment << ")\n";
  });

  auto &Seg =
      IsReadOnly ? Unmapped.back().RODataAllocs : Unmapped.back().RWDataAllocs;

  Seg.emplace_back(Size, Alignment);
  return reinterpret_cast<uint8_t *>(
      alignAddr(Seg.back().Contents.get(), Align(Alignment)));
}

void EPCGenericRTDyldMemoryManager::reserveAllocationSpace(
    uintptr_t CodeSize, Align CodeAlign, uintptr_t RODataSize,
    Align RODataAlign, uintptr_t RWDataSize, Align RWDataAlign) {

  {
    std::lock_guard<std::mutex> Lock(M);
    // If there's already an error then bail out.
    if (!ErrMsg.empty())
      return;

    if (CodeAlign > EPC.getPageSize()) {
      ErrMsg = "Invalid code alignment in reserveAllocationSpace";
      return;
    }
    if (RODataAlign > EPC.getPageSize()) {
      ErrMsg = "Invalid ro-data alignment in reserveAllocationSpace";
      return;
    }
    if (RWDataAlign > EPC.getPageSize()) {
      ErrMsg = "Invalid rw-data alignment in reserveAllocationSpace";
      return;
    }
  }

  uint64_t TotalSize = 0;
  TotalSize += alignTo(CodeSize, EPC.getPageSize());
  TotalSize += alignTo(RODataSize, EPC.getPageSize());
  TotalSize += alignTo(RWDataSize, EPC.getPageSize());

  LLVM_DEBUG({
    dbgs() << "Allocator " << (void *)this << " reserving "
           << formatv("{0:x}", TotalSize) << " bytes.\n";
  });

  Expected<ExecutorAddr> TargetAllocAddr((ExecutorAddr()));
  if (auto Err = EPC.callSPSWrapper<
                 rt::SPSSimpleExecutorMemoryManagerReserveSignature>(
          SAs.Reserve, TargetAllocAddr, SAs.Instance, TotalSize)) {
    std::lock_guard<std::mutex> Lock(M);
    ErrMsg = toString(std::move(Err));
    return;
  }
  if (!TargetAllocAddr) {
    std::lock_guard<std::mutex> Lock(M);
    ErrMsg = toString(TargetAllocAddr.takeError());
    return;
  }

  std::lock_guard<std::mutex> Lock(M);
  Unmapped.push_back(SectionAllocGroup());
  Unmapped.back().RemoteCode = {
      *TargetAllocAddr, ExecutorAddrDiff(alignTo(CodeSize, EPC.getPageSize()))};
  Unmapped.back().RemoteROData = {
      Unmapped.back().RemoteCode.End,
      ExecutorAddrDiff(alignTo(RODataSize, EPC.getPageSize()))};
  Unmapped.back().RemoteRWData = {
      Unmapped.back().RemoteROData.End,
      ExecutorAddrDiff(alignTo(RWDataSize, EPC.getPageSize()))};
}

bool EPCGenericRTDyldMemoryManager::needsToReserveAllocationSpace() {
  return true;
}

void EPCGenericRTDyldMemoryManager::registerEHFrames(uint8_t *Addr,
                                                     uint64_t LoadAddr,
                                                     size_t Size) {
  LLVM_DEBUG({
    dbgs() << "Allocator " << (void *)this << " added unfinalized eh-frame "
           << formatv("[ {0:x} {1:x} ]", LoadAddr, LoadAddr + Size) << "\n";
  });
  std::lock_guard<std::mutex> Lock(M);
  // Bail out early if there's already an error.
  if (!ErrMsg.empty())
    return;

  ExecutorAddr LA(LoadAddr);
  for (auto &SecAllocGroup : llvm::reverse(Unfinalized)) {
    if (SecAllocGroup.RemoteCode.contains(LA) ||
        SecAllocGroup.RemoteROData.contains(LA) ||
        SecAllocGroup.RemoteRWData.contains(LA)) {
      SecAllocGroup.UnfinalizedEHFrames.push_back({LA, Size});
      return;
    }
  }
  ErrMsg = "eh-frame does not lie inside unfinalized alloc";
}

void EPCGenericRTDyldMemoryManager::deregisterEHFrames() {
  // This is a no-op for us: We've registered a deallocation action for it.
}

void EPCGenericRTDyldMemoryManager::notifyObjectLoaded(
    RuntimeDyld &Dyld, const object::ObjectFile &Obj) {
  std::lock_guard<std::mutex> Lock(M);
  LLVM_DEBUG(dbgs() << "Allocator " << (void *)this << " applied mappings:\n");
  for (auto &ObjAllocs : Unmapped) {
    mapAllocsToRemoteAddrs(Dyld, ObjAllocs.CodeAllocs,
                           ObjAllocs.RemoteCode.Start);
    mapAllocsToRemoteAddrs(Dyld, ObjAllocs.RODataAllocs,
                           ObjAllocs.RemoteROData.Start);
    mapAllocsToRemoteAddrs(Dyld, ObjAllocs.RWDataAllocs,
                           ObjAllocs.RemoteRWData.Start);
    Unfinalized.push_back(std::move(ObjAllocs));
  }
  Unmapped.clear();
}

bool EPCGenericRTDyldMemoryManager::finalizeMemory(std::string *ErrMsg) {
  LLVM_DEBUG(dbgs() << "Allocator " << (void *)this << " finalizing:\n");

  // If there's an error then bail out here.
  std::vector<SectionAllocGroup> SecAllocGroups;
  {
    std::lock_guard<std::mutex> Lock(M);
    if (ErrMsg && !this->ErrMsg.empty()) {
      *ErrMsg = std::move(this->ErrMsg);
      return true;
    }
    std::swap(SecAllocGroups, Unfinalized);
  }

  // Loop over unfinalized objects to make finalization requests.
  for (auto &SecAllocGroup : SecAllocGroups) {

    MemProt SegMemProts[3] = {MemProt::Read | MemProt::Exec, MemProt::Read,
                              MemProt::Read | MemProt::Write};

    ExecutorAddrRange *RemoteAddrs[3] = {&SecAllocGroup.RemoteCode,
                                         &SecAllocGroup.RemoteROData,
                                         &SecAllocGroup.RemoteRWData};

    std::vector<SectionAlloc> *SegSections[3] = {&SecAllocGroup.CodeAllocs,
                                                 &SecAllocGroup.RODataAllocs,
                                                 &SecAllocGroup.RWDataAllocs};

    tpctypes::FinalizeRequest FR;
    std::unique_ptr<char[]> AggregateContents[3];

    for (unsigned I = 0; I != 3; ++I) {
      FR.Segments.push_back({});
      auto &Seg = FR.Segments.back();
      Seg.RAG = SegMemProts[I];
      Seg.Addr = RemoteAddrs[I]->Start;
      for (auto &SecAlloc : *SegSections[I]) {
        Seg.Size = alignTo(Seg.Size, SecAlloc.Align);
        Seg.Size += SecAlloc.Size;
      }
      AggregateContents[I] = std::make_unique<char[]>(Seg.Size);
      size_t SecOffset = 0;
      for (auto &SecAlloc : *SegSections[I]) {
        SecOffset = alignTo(SecOffset, SecAlloc.Align);
        memcpy(&AggregateContents[I][SecOffset],
               reinterpret_cast<const char *>(
                   alignAddr(SecAlloc.Contents.get(), Align(SecAlloc.Align))),
               SecAlloc.Size);
        SecOffset += SecAlloc.Size;
        // FIXME: Can we reset SecAlloc.Content here, now that it's copied into
        // the aggregated content?
      }
      Seg.Content = {AggregateContents[I].get(), SecOffset};
    }

    for (auto &Frame : SecAllocGroup.UnfinalizedEHFrames)
      FR.Actions.push_back(
          {cantFail(
               WrapperFunctionCall::Create<SPSArgList<SPSExecutorAddrRange>>(
                   SAs.RegisterEHFrame, Frame)),
           cantFail(
               WrapperFunctionCall::Create<SPSArgList<SPSExecutorAddrRange>>(
                   SAs.DeregisterEHFrame, Frame))});

    // We'll also need to make an extra allocation for the eh-frame wrapper call
    // arguments.
    Error FinalizeErr = Error::success();
    if (auto Err = EPC.callSPSWrapper<
                   rt::SPSSimpleExecutorMemoryManagerFinalizeSignature>(
            SAs.Finalize, FinalizeErr, SAs.Instance, std::move(FR))) {
      std::lock_guard<std::mutex> Lock(M);
      this->ErrMsg = toString(std::move(Err));
      dbgs() << "Serialization error: " << this->ErrMsg << "\n";
      if (ErrMsg)
        *ErrMsg = this->ErrMsg;
      return true;
    }
    if (FinalizeErr) {
      std::lock_guard<std::mutex> Lock(M);
      this->ErrMsg = toString(std::move(FinalizeErr));
      dbgs() << "Finalization error: " << this->ErrMsg << "\n";
      if (ErrMsg)
        *ErrMsg = this->ErrMsg;
      return true;
    }
  }

  return false;
}

void EPCGenericRTDyldMemoryManager::mapAllocsToRemoteAddrs(
    RuntimeDyld &Dyld, std::vector<SectionAlloc> &Allocs,
    ExecutorAddr NextAddr) {
  for (auto &Alloc : Allocs) {
    NextAddr.setValue(alignTo(NextAddr.getValue(), Alloc.Align));
    LLVM_DEBUG({
      dbgs() << "     " << static_cast<void *>(Alloc.Contents.get()) << " -> "
             << format("0x%016" PRIx64, NextAddr.getValue()) << "\n";
    });
    Dyld.mapSectionAddress(reinterpret_cast<const void *>(alignAddr(
                               Alloc.Contents.get(), Align(Alloc.Align))),
                           NextAddr.getValue());
    Alloc.RemoteAddr = NextAddr;
    // Only advance NextAddr if it was non-null to begin with,
    // otherwise leave it as null.
    if (NextAddr)
      NextAddr += ExecutorAddrDiff(Alloc.Size);
  }
}

} // end namespace orc
} // end namespace llvm
