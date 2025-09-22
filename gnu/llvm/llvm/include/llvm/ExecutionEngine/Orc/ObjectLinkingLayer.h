//===-- ObjectLinkingLayer.h - JITLink-based jit linking layer --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains the definition for an JITLink-based, in-process object linking
// layer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_OBJECTLINKINGLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_OBJECTLINKINGLAYER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <list>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

namespace jitlink {
class EHFrameRegistrar;
class LinkGraph;
class Symbol;
} // namespace jitlink

namespace orc {

class ObjectLinkingLayerJITLinkContext;

/// An ObjectLayer implementation built on JITLink.
///
/// Clients can use this class to add relocatable object files to an
/// ExecutionSession, and it typically serves as the base layer (underneath
/// a compiling layer like IRCompileLayer) for the rest of the JIT.
class ObjectLinkingLayer : public RTTIExtends<ObjectLinkingLayer, ObjectLayer>,
                           private ResourceManager {
  friend class ObjectLinkingLayerJITLinkContext;

public:
  static char ID;

  /// Plugin instances can be added to the ObjectLinkingLayer to receive
  /// callbacks when code is loaded or emitted, and when JITLink is being
  /// configured.
  class Plugin {
  public:
    using JITLinkSymbolSet = DenseSet<jitlink::Symbol *>;
    using SyntheticSymbolDependenciesMap =
        DenseMap<SymbolStringPtr, JITLinkSymbolSet>;

    virtual ~Plugin();
    virtual void modifyPassConfig(MaterializationResponsibility &MR,
                                  jitlink::LinkGraph &G,
                                  jitlink::PassConfiguration &Config) {}

    // Deprecated. Don't use this in new code. There will be a proper mechanism
    // for capturing object buffers.
    virtual void notifyMaterializing(MaterializationResponsibility &MR,
                                     jitlink::LinkGraph &G,
                                     jitlink::JITLinkContext &Ctx,
                                     MemoryBufferRef InputObject) {}

    virtual void notifyLoaded(MaterializationResponsibility &MR) {}
    virtual Error notifyEmitted(MaterializationResponsibility &MR) {
      return Error::success();
    }
    virtual Error notifyFailed(MaterializationResponsibility &MR) = 0;
    virtual Error notifyRemovingResources(JITDylib &JD, ResourceKey K) = 0;
    virtual void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                             ResourceKey SrcKey) = 0;

    /// Return any dependencies that synthetic symbols (e.g. init symbols)
    /// have on symbols in the LinkGraph.
    /// This is used by the ObjectLinkingLayer to update the dependencies for
    /// the synthetic symbols.
    virtual SyntheticSymbolDependenciesMap
    getSyntheticSymbolDependencies(MaterializationResponsibility &MR) {
      return SyntheticSymbolDependenciesMap();
    }
  };

  using ReturnObjectBufferFunction =
      std::function<void(std::unique_ptr<MemoryBuffer>)>;

  /// Construct an ObjectLinkingLayer using the ExecutorProcessControl
  /// instance's memory manager.
  ObjectLinkingLayer(ExecutionSession &ES);

  /// Construct an ObjectLinkingLayer using a custom memory manager.
  ObjectLinkingLayer(ExecutionSession &ES,
                     jitlink::JITLinkMemoryManager &MemMgr);

  /// Construct an ObjectLinkingLayer. Takes ownership of the given
  /// JITLinkMemoryManager. This method is a temporary hack to simplify
  /// co-existence with RTDyldObjectLinkingLayer (which also owns its
  /// allocators).
  ObjectLinkingLayer(ExecutionSession &ES,
                     std::unique_ptr<jitlink::JITLinkMemoryManager> MemMgr);

  /// Destruct an ObjectLinkingLayer.
  ~ObjectLinkingLayer();

  /// Set an object buffer return function. By default object buffers are
  /// deleted once the JIT has linked them. If a return function is set then
  /// it will be called to transfer ownership of the buffer instead.
  void setReturnObjectBuffer(ReturnObjectBufferFunction ReturnObjectBuffer) {
    this->ReturnObjectBuffer = std::move(ReturnObjectBuffer);
  }

  /// Add a plugin.
  ObjectLinkingLayer &addPlugin(std::shared_ptr<Plugin> P) {
    std::lock_guard<std::mutex> Lock(LayerMutex);
    Plugins.push_back(std::move(P));
    return *this;
  }

  /// Remove a plugin. This remove applies only to subsequent links (links
  /// already underway will continue to use the plugin), and does not of itself
  /// destroy the plugin -- destruction will happen once all shared pointers
  /// (including those held by in-progress links) are destroyed.
  void removePlugin(Plugin &P) {
    std::lock_guard<std::mutex> Lock(LayerMutex);
    auto I = llvm::find_if(Plugins, [&](const std::shared_ptr<Plugin> &Elem) {
      return Elem.get() == &P;
    });
    assert(I != Plugins.end() && "Plugin not present");
    Plugins.erase(I);
  }

  /// Add a LinkGraph to the JITDylib targeted by the given tracker.
  Error add(ResourceTrackerSP, std::unique_ptr<jitlink::LinkGraph> G);

  /// Add a LinkGraph to the given JITDylib.
  Error add(JITDylib &JD, std::unique_ptr<jitlink::LinkGraph> G) {
    return add(JD.getDefaultResourceTracker(), std::move(G));
  }

  // Un-hide ObjectLayer add methods.
  using ObjectLayer::add;

  /// Emit an object file.
  void emit(std::unique_ptr<MaterializationResponsibility> R,
            std::unique_ptr<MemoryBuffer> O) override;

  /// Emit a LinkGraph.
  void emit(std::unique_ptr<MaterializationResponsibility> R,
            std::unique_ptr<jitlink::LinkGraph> G);

  /// Instructs this ObjectLinkingLayer instance to override the symbol flags
  /// found in the AtomGraph with the flags supplied by the
  /// MaterializationResponsibility instance. This is a workaround to support
  /// symbol visibility in COFF, which does not use the libObject's
  /// SF_Exported flag. Use only when generating / adding COFF object files.
  ///
  /// FIXME: We should be able to remove this if/when COFF properly tracks
  /// exported symbols.
  ObjectLinkingLayer &
  setOverrideObjectFlagsWithResponsibilityFlags(bool OverrideObjectFlags) {
    this->OverrideObjectFlags = OverrideObjectFlags;
    return *this;
  }

  /// If set, this ObjectLinkingLayer instance will claim responsibility
  /// for any symbols provided by a given object file that were not already in
  /// the MaterializationResponsibility instance. Setting this flag allows
  /// higher-level program representations (e.g. LLVM IR) to be added based on
  /// only a subset of the symbols they provide, without having to write
  /// intervening layers to scan and add the additional symbols. This trades
  /// diagnostic quality for convenience however: If all symbols are enumerated
  /// up-front then clashes can be detected and reported early (and usually
  /// deterministically). If this option is set, clashes for the additional
  /// symbols may not be detected until late, and detection may depend on
  /// the flow of control through JIT'd code. Use with care.
  ObjectLinkingLayer &
  setAutoClaimResponsibilityForObjectSymbols(bool AutoClaimObjectSymbols) {
    this->AutoClaimObjectSymbols = AutoClaimObjectSymbols;
    return *this;
  }

private:
  using FinalizedAlloc = jitlink::JITLinkMemoryManager::FinalizedAlloc;

  Error recordFinalizedAlloc(MaterializationResponsibility &MR,
                             FinalizedAlloc FA);

  Error handleRemoveResources(JITDylib &JD, ResourceKey K) override;
  void handleTransferResources(JITDylib &JD, ResourceKey DstKey,
                               ResourceKey SrcKey) override;

  mutable std::mutex LayerMutex;
  jitlink::JITLinkMemoryManager &MemMgr;
  std::unique_ptr<jitlink::JITLinkMemoryManager> MemMgrOwnership;
  bool OverrideObjectFlags = false;
  bool AutoClaimObjectSymbols = false;
  ReturnObjectBufferFunction ReturnObjectBuffer;
  DenseMap<ResourceKey, std::vector<FinalizedAlloc>> Allocs;
  std::vector<std::shared_ptr<Plugin>> Plugins;
};

class EHFrameRegistrationPlugin : public ObjectLinkingLayer::Plugin {
public:
  EHFrameRegistrationPlugin(
      ExecutionSession &ES,
      std::unique_ptr<jitlink::EHFrameRegistrar> Registrar);
  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &G,
                        jitlink::PassConfiguration &PassConfig) override;
  Error notifyEmitted(MaterializationResponsibility &MR) override;
  Error notifyFailed(MaterializationResponsibility &MR) override;
  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override;
  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override;

private:
  std::mutex EHFramePluginMutex;
  ExecutionSession &ES;
  std::unique_ptr<jitlink::EHFrameRegistrar> Registrar;
  DenseMap<MaterializationResponsibility *, ExecutorAddrRange> InProcessLinks;
  DenseMap<ResourceKey, std::vector<ExecutorAddrRange>> EHFrameRanges;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_OBJECTLINKINGLAYER_H
