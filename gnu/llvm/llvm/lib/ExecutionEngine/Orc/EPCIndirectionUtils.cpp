//===------- EPCIndirectionUtils.cpp -- EPC based indirection APIs --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/EPCIndirectionUtils.h"

#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/Support/MathExtras.h"

#include <future>

using namespace llvm;
using namespace llvm::orc;

namespace llvm {
namespace orc {

class EPCIndirectionUtilsAccess {
public:
  using IndirectStubInfo = EPCIndirectionUtils::IndirectStubInfo;
  using IndirectStubInfoVector = EPCIndirectionUtils::IndirectStubInfoVector;

  static Expected<IndirectStubInfoVector>
  getIndirectStubs(EPCIndirectionUtils &EPCIU, unsigned NumStubs) {
    return EPCIU.getIndirectStubs(NumStubs);
  };
};

} // end namespace orc
} // end namespace llvm

namespace {

class EPCTrampolinePool : public TrampolinePool {
public:
  EPCTrampolinePool(EPCIndirectionUtils &EPCIU);
  Error deallocatePool();

protected:
  Error grow() override;

  using FinalizedAlloc = jitlink::JITLinkMemoryManager::FinalizedAlloc;

  EPCIndirectionUtils &EPCIU;
  unsigned TrampolineSize = 0;
  unsigned TrampolinesPerPage = 0;
  std::vector<FinalizedAlloc> TrampolineBlocks;
};

class EPCIndirectStubsManager : public IndirectStubsManager,
                                private EPCIndirectionUtilsAccess {
public:
  EPCIndirectStubsManager(EPCIndirectionUtils &EPCIU) : EPCIU(EPCIU) {}

  Error deallocateStubs();

  Error createStub(StringRef StubName, ExecutorAddr StubAddr,
                   JITSymbolFlags StubFlags) override;

  Error createStubs(const StubInitsMap &StubInits) override;

  ExecutorSymbolDef findStub(StringRef Name, bool ExportedStubsOnly) override;

  ExecutorSymbolDef findPointer(StringRef Name) override;

  Error updatePointer(StringRef Name, ExecutorAddr NewAddr) override;

private:
  using StubInfo = std::pair<IndirectStubInfo, JITSymbolFlags>;

  std::mutex ISMMutex;
  EPCIndirectionUtils &EPCIU;
  StringMap<StubInfo> StubInfos;
};

EPCTrampolinePool::EPCTrampolinePool(EPCIndirectionUtils &EPCIU)
    : EPCIU(EPCIU) {
  auto &EPC = EPCIU.getExecutorProcessControl();
  auto &ABI = EPCIU.getABISupport();

  TrampolineSize = ABI.getTrampolineSize();
  TrampolinesPerPage =
      (EPC.getPageSize() - ABI.getPointerSize()) / TrampolineSize;
}

Error EPCTrampolinePool::deallocatePool() {
  std::promise<MSVCPError> DeallocResultP;
  auto DeallocResultF = DeallocResultP.get_future();

  EPCIU.getExecutorProcessControl().getMemMgr().deallocate(
      std::move(TrampolineBlocks),
      [&](Error Err) { DeallocResultP.set_value(std::move(Err)); });

  return DeallocResultF.get();
}

Error EPCTrampolinePool::grow() {
  using namespace jitlink;

  assert(AvailableTrampolines.empty() &&
         "Grow called with trampolines still available");

  auto ResolverAddress = EPCIU.getResolverBlockAddress();
  assert(ResolverAddress && "Resolver address can not be null");

  auto &EPC = EPCIU.getExecutorProcessControl();
  auto PageSize = EPC.getPageSize();
  auto Alloc = SimpleSegmentAlloc::Create(
      EPC.getMemMgr(), nullptr,
      {{MemProt::Read | MemProt::Exec, {PageSize, Align(PageSize)}}});
  if (!Alloc)
    return Alloc.takeError();

  unsigned NumTrampolines = TrampolinesPerPage;

  auto SegInfo = Alloc->getSegInfo(MemProt::Read | MemProt::Exec);
  EPCIU.getABISupport().writeTrampolines(
      SegInfo.WorkingMem.data(), SegInfo.Addr, ResolverAddress, NumTrampolines);
  for (unsigned I = 0; I < NumTrampolines; ++I)
    AvailableTrampolines.push_back(SegInfo.Addr + (I * TrampolineSize));

  auto FA = Alloc->finalize();
  if (!FA)
    return FA.takeError();

  TrampolineBlocks.push_back(std::move(*FA));

  return Error::success();
}

Error EPCIndirectStubsManager::createStub(StringRef StubName,
                                          ExecutorAddr StubAddr,
                                          JITSymbolFlags StubFlags) {
  StubInitsMap SIM;
  SIM[StubName] = std::make_pair(StubAddr, StubFlags);
  return createStubs(SIM);
}

Error EPCIndirectStubsManager::createStubs(const StubInitsMap &StubInits) {
  auto AvailableStubInfos = getIndirectStubs(EPCIU, StubInits.size());
  if (!AvailableStubInfos)
    return AvailableStubInfos.takeError();

  {
    std::lock_guard<std::mutex> Lock(ISMMutex);
    unsigned ASIdx = 0;
    for (auto &SI : StubInits) {
      auto &A = (*AvailableStubInfos)[ASIdx++];
      StubInfos[SI.first()] = std::make_pair(A, SI.second.second);
    }
  }

  auto &MemAccess = EPCIU.getExecutorProcessControl().getMemoryAccess();
  switch (EPCIU.getABISupport().getPointerSize()) {
  case 4: {
    unsigned ASIdx = 0;
    std::vector<tpctypes::UInt32Write> PtrUpdates;
    for (auto &SI : StubInits)
      PtrUpdates.push_back({(*AvailableStubInfos)[ASIdx++].PointerAddress,
                            static_cast<uint32_t>(SI.second.first.getValue())});
    return MemAccess.writeUInt32s(PtrUpdates);
  }
  case 8: {
    unsigned ASIdx = 0;
    std::vector<tpctypes::UInt64Write> PtrUpdates;
    for (auto &SI : StubInits)
      PtrUpdates.push_back({(*AvailableStubInfos)[ASIdx++].PointerAddress,
                            static_cast<uint64_t>(SI.second.first.getValue())});
    return MemAccess.writeUInt64s(PtrUpdates);
  }
  default:
    return make_error<StringError>("Unsupported pointer size",
                                   inconvertibleErrorCode());
  }
}

ExecutorSymbolDef EPCIndirectStubsManager::findStub(StringRef Name,
                                                    bool ExportedStubsOnly) {
  std::lock_guard<std::mutex> Lock(ISMMutex);
  auto I = StubInfos.find(Name);
  if (I == StubInfos.end())
    return ExecutorSymbolDef();
  return {I->second.first.StubAddress, I->second.second};
}

ExecutorSymbolDef EPCIndirectStubsManager::findPointer(StringRef Name) {
  std::lock_guard<std::mutex> Lock(ISMMutex);
  auto I = StubInfos.find(Name);
  if (I == StubInfos.end())
    return ExecutorSymbolDef();
  return {I->second.first.PointerAddress, I->second.second};
}

Error EPCIndirectStubsManager::updatePointer(StringRef Name,
                                             ExecutorAddr NewAddr) {

  ExecutorAddr PtrAddr;
  {
    std::lock_guard<std::mutex> Lock(ISMMutex);
    auto I = StubInfos.find(Name);
    if (I == StubInfos.end())
      return make_error<StringError>("Unknown stub name",
                                     inconvertibleErrorCode());
    PtrAddr = I->second.first.PointerAddress;
  }

  auto &MemAccess = EPCIU.getExecutorProcessControl().getMemoryAccess();
  switch (EPCIU.getABISupport().getPointerSize()) {
  case 4: {
    tpctypes::UInt32Write PUpdate(PtrAddr, NewAddr.getValue());
    return MemAccess.writeUInt32s(PUpdate);
  }
  case 8: {
    tpctypes::UInt64Write PUpdate(PtrAddr, NewAddr.getValue());
    return MemAccess.writeUInt64s(PUpdate);
  }
  default:
    return make_error<StringError>("Unsupported pointer size",
                                   inconvertibleErrorCode());
  }
}

} // end anonymous namespace.

namespace llvm {
namespace orc {

EPCIndirectionUtils::ABISupport::~ABISupport() = default;

Expected<std::unique_ptr<EPCIndirectionUtils>>
EPCIndirectionUtils::Create(ExecutorProcessControl &EPC) {
  const auto &TT = EPC.getTargetTriple();
  switch (TT.getArch()) {
  default:
    return make_error<StringError>(
        std::string("No EPCIndirectionUtils available for ") + TT.str(),
        inconvertibleErrorCode());
  case Triple::aarch64:
  case Triple::aarch64_32:
    return CreateWithABI<OrcAArch64>(EPC);

  case Triple::x86:
    return CreateWithABI<OrcI386>(EPC);

  case Triple::loongarch64:
    return CreateWithABI<OrcLoongArch64>(EPC);

  case Triple::mips:
    return CreateWithABI<OrcMips32Be>(EPC);

  case Triple::mipsel:
    return CreateWithABI<OrcMips32Le>(EPC);

  case Triple::mips64:
  case Triple::mips64el:
    return CreateWithABI<OrcMips64>(EPC);

  case Triple::riscv64:
    return CreateWithABI<OrcRiscv64>(EPC);

  case Triple::x86_64:
    if (TT.getOS() == Triple::OSType::Win32)
      return CreateWithABI<OrcX86_64_Win32>(EPC);
    else
      return CreateWithABI<OrcX86_64_SysV>(EPC);
  }
}

Error EPCIndirectionUtils::cleanup() {

  auto &MemMgr = EPC.getMemMgr();
  auto Err = MemMgr.deallocate(std::move(IndirectStubAllocs));

  if (TP)
    Err = joinErrors(std::move(Err),
                     static_cast<EPCTrampolinePool &>(*TP).deallocatePool());

  if (ResolverBlock)
    Err =
        joinErrors(std::move(Err), MemMgr.deallocate(std::move(ResolverBlock)));

  return Err;
}

Expected<ExecutorAddr>
EPCIndirectionUtils::writeResolverBlock(ExecutorAddr ReentryFnAddr,
                                        ExecutorAddr ReentryCtxAddr) {
  using namespace jitlink;

  assert(ABI && "ABI can not be null");
  auto ResolverSize = ABI->getResolverCodeSize();

  auto Alloc =
      SimpleSegmentAlloc::Create(EPC.getMemMgr(), nullptr,
                                 {{MemProt::Read | MemProt::Exec,
                                   {ResolverSize, Align(EPC.getPageSize())}}});

  if (!Alloc)
    return Alloc.takeError();

  auto SegInfo = Alloc->getSegInfo(MemProt::Read | MemProt::Exec);
  ResolverBlockAddr = SegInfo.Addr;
  ABI->writeResolverCode(SegInfo.WorkingMem.data(), ResolverBlockAddr,
                         ReentryFnAddr, ReentryCtxAddr);

  auto FA = Alloc->finalize();
  if (!FA)
    return FA.takeError();

  ResolverBlock = std::move(*FA);
  return ResolverBlockAddr;
}

std::unique_ptr<IndirectStubsManager>
EPCIndirectionUtils::createIndirectStubsManager() {
  return std::make_unique<EPCIndirectStubsManager>(*this);
}

TrampolinePool &EPCIndirectionUtils::getTrampolinePool() {
  if (!TP)
    TP = std::make_unique<EPCTrampolinePool>(*this);
  return *TP;
}

LazyCallThroughManager &EPCIndirectionUtils::createLazyCallThroughManager(
    ExecutionSession &ES, ExecutorAddr ErrorHandlerAddr) {
  assert(!LCTM &&
         "createLazyCallThroughManager can not have been called before");
  LCTM = std::make_unique<LazyCallThroughManager>(ES, ErrorHandlerAddr,
                                                  &getTrampolinePool());
  return *LCTM;
}

EPCIndirectionUtils::EPCIndirectionUtils(ExecutorProcessControl &EPC,
                                         std::unique_ptr<ABISupport> ABI)
    : EPC(EPC), ABI(std::move(ABI)) {
  assert(this->ABI && "ABI can not be null");

  assert(EPC.getPageSize() > getABISupport().getStubSize() &&
         "Stubs larger than one page are not supported");
}

Expected<EPCIndirectionUtils::IndirectStubInfoVector>
EPCIndirectionUtils::getIndirectStubs(unsigned NumStubs) {
  using namespace jitlink;

  std::lock_guard<std::mutex> Lock(EPCUIMutex);

  // If there aren't enough stubs available then allocate some more.
  if (NumStubs > AvailableIndirectStubs.size()) {
    auto NumStubsToAllocate = NumStubs;
    auto PageSize = EPC.getPageSize();
    auto StubBytes = alignTo(NumStubsToAllocate * ABI->getStubSize(), PageSize);
    NumStubsToAllocate = StubBytes / ABI->getStubSize();
    auto PtrBytes =
        alignTo(NumStubsToAllocate * ABI->getPointerSize(), PageSize);

    auto StubProt = MemProt::Read | MemProt::Exec;
    auto PtrProt = MemProt::Read | MemProt::Write;

    auto Alloc = SimpleSegmentAlloc::Create(
        EPC.getMemMgr(), nullptr,
        {{StubProt, {static_cast<size_t>(StubBytes), Align(PageSize)}},
         {PtrProt, {static_cast<size_t>(PtrBytes), Align(PageSize)}}});

    if (!Alloc)
      return Alloc.takeError();

    auto StubSeg = Alloc->getSegInfo(StubProt);
    auto PtrSeg = Alloc->getSegInfo(PtrProt);

    ABI->writeIndirectStubsBlock(StubSeg.WorkingMem.data(), StubSeg.Addr,
                                 PtrSeg.Addr, NumStubsToAllocate);

    auto FA = Alloc->finalize();
    if (!FA)
      return FA.takeError();

    IndirectStubAllocs.push_back(std::move(*FA));

    auto StubExecutorAddr = StubSeg.Addr;
    auto PtrExecutorAddr = PtrSeg.Addr;
    for (unsigned I = 0; I != NumStubsToAllocate; ++I) {
      AvailableIndirectStubs.push_back(
          IndirectStubInfo(StubExecutorAddr, PtrExecutorAddr));
      StubExecutorAddr += ABI->getStubSize();
      PtrExecutorAddr += ABI->getPointerSize();
    }
  }

  assert(NumStubs <= AvailableIndirectStubs.size() &&
         "Sufficient stubs should have been allocated above");

  IndirectStubInfoVector Result;
  while (NumStubs--) {
    Result.push_back(AvailableIndirectStubs.back());
    AvailableIndirectStubs.pop_back();
  }

  return std::move(Result);
}

static JITTargetAddress reentry(JITTargetAddress LCTMAddr,
                                JITTargetAddress TrampolineAddr) {
  auto &LCTM = *jitTargetAddressToPointer<LazyCallThroughManager *>(LCTMAddr);
  std::promise<ExecutorAddr> LandingAddrP;
  auto LandingAddrF = LandingAddrP.get_future();
  LCTM.resolveTrampolineLandingAddress(
      ExecutorAddr(TrampolineAddr),
      [&](ExecutorAddr Addr) { LandingAddrP.set_value(Addr); });
  return LandingAddrF.get().getValue();
}

Error setUpInProcessLCTMReentryViaEPCIU(EPCIndirectionUtils &EPCIU) {
  auto &LCTM = EPCIU.getLazyCallThroughManager();
  return EPCIU
      .writeResolverBlock(ExecutorAddr::fromPtr(&reentry),
                          ExecutorAddr::fromPtr(&LCTM))
      .takeError();
}

} // end namespace orc
} // end namespace llvm
