//===-- RemoteJITUtils.h - Utilities for remote-JITing with LLI -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for remote-JITing with LLI.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLI_FORWARDINGMEMORYMANAGER_H
#define LLVM_TOOLS_LLI_FORWARDINGMEMORYMANAGER_H

#include "llvm/ExecutionEngine/Orc/EPCGenericDylibManager.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"

namespace llvm {

// ForwardingMM - Adapter to connect MCJIT to Orc's Remote
// memory manager.
class ForwardingMemoryManager : public llvm::RTDyldMemoryManager {
public:
  void setMemMgr(std::unique_ptr<RuntimeDyld::MemoryManager> MemMgr) {
    this->MemMgr = std::move(MemMgr);
  }

  void setResolver(std::shared_ptr<LegacyJITSymbolResolver> Resolver) {
    this->Resolver = std::move(Resolver);
  }

  uint8_t *allocateCodeSection(uintptr_t Size, unsigned Alignment,
                               unsigned SectionID,
                               StringRef SectionName) override {
    return MemMgr->allocateCodeSection(Size, Alignment, SectionID, SectionName);
  }

  uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                               unsigned SectionID, StringRef SectionName,
                               bool IsReadOnly) override {
    return MemMgr->allocateDataSection(Size, Alignment, SectionID, SectionName,
                                       IsReadOnly);
  }

  void reserveAllocationSpace(uintptr_t CodeSize, Align CodeAlign,
                              uintptr_t RODataSize, Align RODataAlign,
                              uintptr_t RWDataSize,
                              Align RWDataAlign) override {
    MemMgr->reserveAllocationSpace(CodeSize, CodeAlign, RODataSize, RODataAlign,
                                   RWDataSize, RWDataAlign);
  }

  bool needsToReserveAllocationSpace() override {
    return MemMgr->needsToReserveAllocationSpace();
  }

  void registerEHFrames(uint8_t *Addr, uint64_t LoadAddr,
                        size_t Size) override {
    MemMgr->registerEHFrames(Addr, LoadAddr, Size);
  }

  void deregisterEHFrames() override { MemMgr->deregisterEHFrames(); }

  bool finalizeMemory(std::string *ErrMsg = nullptr) override {
    return MemMgr->finalizeMemory(ErrMsg);
  }

  void notifyObjectLoaded(RuntimeDyld &RTDyld,
                          const object::ObjectFile &Obj) override {
    MemMgr->notifyObjectLoaded(RTDyld, Obj);
  }

  // Don't hide the sibling notifyObjectLoaded from RTDyldMemoryManager.
  using RTDyldMemoryManager::notifyObjectLoaded;

  JITSymbol findSymbol(const std::string &Name) override {
    return Resolver->findSymbol(Name);
  }

  JITSymbol findSymbolInLogicalDylib(const std::string &Name) override {
    return Resolver->findSymbolInLogicalDylib(Name);
  }

private:
  std::unique_ptr<RuntimeDyld::MemoryManager> MemMgr;
  std::shared_ptr<LegacyJITSymbolResolver> Resolver;
};

class RemoteResolver : public LegacyJITSymbolResolver {
public:
  static Expected<std::unique_ptr<RemoteResolver>>
  Create(orc::ExecutorProcessControl &EPC) {
    auto DylibMgr =
        orc::EPCGenericDylibManager::CreateWithDefaultBootstrapSymbols(EPC);
    if (!DylibMgr)
      return DylibMgr.takeError();
    auto H = DylibMgr->open("", 0);
    if (!H)
      return H.takeError();
    return std::make_unique<RemoteResolver>(std::move(*DylibMgr),
                                            std::move(*H));
  }

  JITSymbol findSymbol(const std::string &Name) override {
    orc::RemoteSymbolLookupSet R;
    R.push_back({std::move(Name), false});
    if (auto Syms = DylibMgr.lookup(H, R)) {
      if (Syms->size() != 1)
        return make_error<StringError>("Unexpected remote lookup result",
                                       inconvertibleErrorCode());
      return JITSymbol(Syms->front().getAddress().getValue(),
                       Syms->front().getFlags());
    } else
      return Syms.takeError();
  }

  JITSymbol findSymbolInLogicalDylib(const std::string &Name) override {
    return nullptr;
  }

public:
  RemoteResolver(orc::EPCGenericDylibManager DylibMgr,
                 orc::tpctypes::DylibHandle H)
      : DylibMgr(std::move(DylibMgr)), H(std::move(H)) {}

  orc::EPCGenericDylibManager DylibMgr;
  orc::tpctypes::DylibHandle H;
};
} // namespace llvm

#endif // LLVM_TOOLS_LLI_FORWARDINGMEMORYMANAGER_H
