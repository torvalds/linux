//===--- VTuneSupportPlugin.cpp -- Support for VTune profiler --*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Handles support for registering code with VIntel Tune's Amplfiier JIT API.
//
//===----------------------------------------------------------------------===//
#include "llvm/ExecutionEngine/Orc/Debugging/VTuneSupportPlugin.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/ExecutionEngine/Orc/Debugging/DebugInfoSupport.h"

using namespace llvm;
using namespace llvm::orc;
using namespace llvm::jitlink;

static constexpr StringRef RegisterVTuneImplName = "llvm_orc_registerVTuneImpl";
static constexpr StringRef UnregisterVTuneImplName =
    "llvm_orc_unregisterVTuneImpl";
static constexpr StringRef RegisterTestVTuneImplName =
    "llvm_orc_test_registerVTuneImpl";

static VTuneMethodBatch getMethodBatch(LinkGraph &G, bool EmitDebugInfo) {
  VTuneMethodBatch Batch;
  std::unique_ptr<DWARFContext> DC;
  StringMap<std::unique_ptr<MemoryBuffer>> DCBacking;
  if (EmitDebugInfo) {
    auto EDC = createDWARFContext(G);
    if (!EDC) {
      EmitDebugInfo = false;
    } else {
      DC = std::move(EDC->first);
      DCBacking = std::move(EDC->second);
    }
  }

  auto GetStringIdx = [Deduplicator = StringMap<uint32_t>(),
                       &Batch](StringRef S) mutable {
    auto I = Deduplicator.find(S);
    if (I != Deduplicator.end())
      return I->second;

    Batch.Strings.push_back(S.str());
    return Deduplicator[S] = Batch.Strings.size();
  };
  for (auto Sym : G.defined_symbols()) {
    if (!Sym->isCallable())
      continue;

    Batch.Methods.push_back(VTuneMethodInfo());
    auto &Method = Batch.Methods.back();
    Method.MethodID = 0;
    Method.ParentMI = 0;
    Method.LoadAddr = Sym->getAddress();
    Method.LoadSize = Sym->getSize();
    Method.NameSI = GetStringIdx(Sym->getName());
    Method.ClassFileSI = 0;
    Method.SourceFileSI = 0;

    if (!EmitDebugInfo)
      continue;

    auto &Section = Sym->getBlock().getSection();
    auto Addr = Sym->getAddress();
    auto SAddr =
        object::SectionedAddress{Addr.getValue(), Section.getOrdinal()};
    DILineInfoTable LinesInfo = DC->getLineInfoForAddressRange(
        SAddr, Sym->getSize(),
        DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath);
    Method.SourceFileSI = Batch.Strings.size();
    Batch.Strings.push_back(DC->getLineInfoForAddress(SAddr).FileName);
    for (auto &LInfo : LinesInfo) {
      Method.LineTable.push_back(
          std::pair<unsigned, unsigned>{/*unsigned*/ Sym->getOffset(),
                                        /*DILineInfo*/ LInfo.second.Line});
    }
  }
  return Batch;
}

void VTuneSupportPlugin::modifyPassConfig(MaterializationResponsibility &MR,
                                          LinkGraph &G,
                                          PassConfiguration &Config) {
  Config.PostFixupPasses.push_back([this, MR = &MR](LinkGraph &G) {
    // the object file is generated but not linked yet
    auto Batch = getMethodBatch(G, EmitDebugInfo);
    if (Batch.Methods.empty()) {
      return Error::success();
    }
    {
      std::lock_guard<std::mutex> Lock(PluginMutex);
      uint64_t Allocated = Batch.Methods.size();
      uint64_t Start = NextMethodID;
      NextMethodID += Allocated;
      for (size_t i = Start; i < NextMethodID; ++i) {
        Batch.Methods[i - Start].MethodID = i;
      }
      this->PendingMethodIDs[MR] = {Start, Allocated};
    }
    G.allocActions().push_back(
        {cantFail(shared::WrapperFunctionCall::Create<
                  shared::SPSArgList<shared::SPSVTuneMethodBatch>>(
             RegisterVTuneImplAddr, Batch)),
         {}});
    return Error::success();
  });
}

Error VTuneSupportPlugin::notifyEmitted(MaterializationResponsibility &MR) {
  if (auto Err = MR.withResourceKeyDo([this, MR = &MR](ResourceKey K) {
        std::lock_guard<std::mutex> Lock(PluginMutex);
        auto I = PendingMethodIDs.find(MR);
        if (I == PendingMethodIDs.end())
          return;

        LoadedMethodIDs[K].push_back(I->second);
        PendingMethodIDs.erase(I);
      })) {
    return Err;
  }
  return Error::success();
}

Error VTuneSupportPlugin::notifyFailed(MaterializationResponsibility &MR) {
  std::lock_guard<std::mutex> Lock(PluginMutex);
  PendingMethodIDs.erase(&MR);
  return Error::success();
}

Error VTuneSupportPlugin::notifyRemovingResources(JITDylib &JD, ResourceKey K) {
  // Unregistration not required if not provided
  if (!UnregisterVTuneImplAddr) {
    return Error::success();
  }
  VTuneUnloadedMethodIDs UnloadedIDs;
  {
    std::lock_guard<std::mutex> Lock(PluginMutex);
    auto I = LoadedMethodIDs.find(K);
    if (I == LoadedMethodIDs.end())
      return Error::success();

    UnloadedIDs = std::move(I->second);
    LoadedMethodIDs.erase(I);
  }
  if (auto Err = EPC.callSPSWrapper<void(shared::SPSVTuneUnloadedMethodIDs)>(
          UnregisterVTuneImplAddr, UnloadedIDs))
    return Err;

  return Error::success();
}

void VTuneSupportPlugin::notifyTransferringResources(JITDylib &JD,
                                                     ResourceKey DstKey,
                                                     ResourceKey SrcKey) {
  std::lock_guard<std::mutex> Lock(PluginMutex);
  auto I = LoadedMethodIDs.find(SrcKey);
  if (I == LoadedMethodIDs.end())
    return;

  auto &Dest = LoadedMethodIDs[DstKey];
  Dest.insert(Dest.end(), I->second.begin(), I->second.end());
  LoadedMethodIDs.erase(SrcKey);
}

Expected<std::unique_ptr<VTuneSupportPlugin>>
VTuneSupportPlugin::Create(ExecutorProcessControl &EPC, JITDylib &JD,
                           bool EmitDebugInfo, bool TestMode) {
  auto &ES = EPC.getExecutionSession();
  auto RegisterImplName =
      ES.intern(TestMode ? RegisterTestVTuneImplName : RegisterVTuneImplName);
  auto UnregisterImplName = ES.intern(UnregisterVTuneImplName);
  SymbolLookupSet SLS{RegisterImplName, UnregisterImplName};
  auto Res = ES.lookup(makeJITDylibSearchOrder({&JD}), std::move(SLS));
  if (!Res)
    return Res.takeError();
  ExecutorAddr RegisterImplAddr(
      Res->find(RegisterImplName)->second.getAddress());
  ExecutorAddr UnregisterImplAddr(
      Res->find(UnregisterImplName)->second.getAddress());
  return std::make_unique<VTuneSupportPlugin>(
      EPC, RegisterImplAddr, UnregisterImplAddr, EmitDebugInfo);
}
