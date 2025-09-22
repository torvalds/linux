//===-- ELFNixPlatform.h -- Utilities for executing ELF in Orc --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Linux/BSD support for executing JIT'd ELF in Orc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_ELFNIXPLATFORM_H
#define LLVM_EXECUTIONENGINE_ORC_ELFNIXPLATFORM_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"

#include <future>
#include <thread>
#include <vector>

namespace llvm {
namespace orc {

struct ELFPerObjectSectionsToRegister {
  ExecutorAddrRange EHFrameSection;
  ExecutorAddrRange ThreadDataSection;
};

struct ELFNixJITDylibInitializers {
  using SectionList = std::vector<ExecutorAddrRange>;

  ELFNixJITDylibInitializers(std::string Name, ExecutorAddr DSOHandleAddress)
      : Name(std::move(Name)), DSOHandleAddress(std::move(DSOHandleAddress)) {}

  std::string Name;
  ExecutorAddr DSOHandleAddress;

  StringMap<SectionList> InitSections;
};

class ELFNixJITDylibDeinitializers {};

using ELFNixJITDylibInitializerSequence =
    std::vector<ELFNixJITDylibInitializers>;

using ELFNixJITDylibDeinitializerSequence =
    std::vector<ELFNixJITDylibDeinitializers>;

/// Mediates between ELFNix initialization and ExecutionSession state.
class ELFNixPlatform : public Platform {
public:
  /// Try to create a ELFNixPlatform instance, adding the ORC runtime to the
  /// given JITDylib.
  ///
  /// The ORC runtime requires access to a number of symbols in
  /// libc++. It is up to the caller to ensure that the required
  /// symbols can be referenced by code added to PlatformJD. The
  /// standard way to achieve this is to first attach dynamic library
  /// search generators for either the given process, or for the
  /// specific required libraries, to PlatformJD, then to create the
  /// platform instance:
  ///
  /// \code{.cpp}
  ///   auto &PlatformJD = ES.createBareJITDylib("stdlib");
  ///   PlatformJD.addGenerator(
  ///     ExitOnErr(EPCDynamicLibrarySearchGenerator
  ///                 ::GetForTargetProcess(EPC)));
  ///   ES.setPlatform(
  ///     ExitOnErr(ELFNixPlatform::Create(ES, ObjLayer, EPC, PlatformJD,
  ///                                     "/path/to/orc/runtime")));
  /// \endcode
  ///
  /// Alternatively, these symbols could be added to another JITDylib that
  /// PlatformJD links against.
  ///
  /// Clients are also responsible for ensuring that any JIT'd code that
  /// depends on runtime functions (including any code using TLV or static
  /// destructors) can reference the runtime symbols. This is usually achieved
  /// by linking any JITDylibs containing regular code against
  /// PlatformJD.
  ///
  /// By default, ELFNixPlatform will add the set of aliases returned by the
  /// standardPlatformAliases function. This includes both required aliases
  /// (e.g. __cxa_atexit -> __orc_rt_elf_cxa_atexit for static destructor
  /// support), and optional aliases that provide JIT versions of common
  /// functions (e.g. dlopen -> __orc_rt_elf_jit_dlopen). Clients can
  /// override these defaults by passing a non-None value for the
  /// RuntimeAliases function, in which case the client is responsible for
  /// setting up all aliases (including the required ones).
  static Expected<std::unique_ptr<ELFNixPlatform>>
  Create(ExecutionSession &ES, ObjectLinkingLayer &ObjLinkingLayer,
         JITDylib &PlatformJD, std::unique_ptr<DefinitionGenerator> OrcRuntime,
         std::optional<SymbolAliasMap> RuntimeAliases = std::nullopt);

  /// Construct using a path to the ORC runtime.
  static Expected<std::unique_ptr<ELFNixPlatform>>
  Create(ExecutionSession &ES, ObjectLinkingLayer &ObjLinkingLayer,
         JITDylib &PlatformJD, const char *OrcRuntimePath,
         std::optional<SymbolAliasMap> RuntimeAliases = std::nullopt);

  ExecutionSession &getExecutionSession() const { return ES; }
  ObjectLinkingLayer &getObjectLinkingLayer() const { return ObjLinkingLayer; }

  Error setupJITDylib(JITDylib &JD) override;
  Error teardownJITDylib(JITDylib &JD) override;
  Error notifyAdding(ResourceTracker &RT,
                     const MaterializationUnit &MU) override;
  Error notifyRemoving(ResourceTracker &RT) override;

  /// Returns an AliasMap containing the default aliases for the ELFNixPlatform.
  /// This can be modified by clients when constructing the platform to add
  /// or remove aliases.
  static Expected<SymbolAliasMap> standardPlatformAliases(ExecutionSession &ES,
                                                          JITDylib &PlatformJD);

  /// Returns the array of required CXX aliases.
  static ArrayRef<std::pair<const char *, const char *>> requiredCXXAliases();

  /// Returns the array of standard runtime utility aliases for ELF.
  static ArrayRef<std::pair<const char *, const char *>>
  standardRuntimeUtilityAliases();

private:
  // The ELFNixPlatformPlugin scans/modifies LinkGraphs to support ELF
  // platform features including initializers, exceptions, TLV, and language
  // runtime registration.
  class ELFNixPlatformPlugin : public ObjectLinkingLayer::Plugin {
  public:
    ELFNixPlatformPlugin(ELFNixPlatform &MP) : MP(MP) {}

    void modifyPassConfig(MaterializationResponsibility &MR,
                          jitlink::LinkGraph &G,
                          jitlink::PassConfiguration &Config) override;

    SyntheticSymbolDependenciesMap
    getSyntheticSymbolDependencies(MaterializationResponsibility &MR) override;

    // FIXME: We should be tentatively tracking scraped sections and discarding
    // if the MR fails.
    Error notifyFailed(MaterializationResponsibility &MR) override {
      return Error::success();
    }

    Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
      return Error::success();
    }

    void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                     ResourceKey SrcKey) override {}

  private:
    using InitSymbolDepMap =
        DenseMap<MaterializationResponsibility *, JITLinkSymbolSet>;

    void addInitializerSupportPasses(MaterializationResponsibility &MR,
                                     jitlink::PassConfiguration &Config);

    void addDSOHandleSupportPasses(MaterializationResponsibility &MR,
                                   jitlink::PassConfiguration &Config);

    void addEHAndTLVSupportPasses(MaterializationResponsibility &MR,
                                  jitlink::PassConfiguration &Config);

    Error preserveInitSections(jitlink::LinkGraph &G,
                               MaterializationResponsibility &MR);

    Error registerInitSections(jitlink::LinkGraph &G, JITDylib &JD);

    Error fixTLVSectionsAndEdges(jitlink::LinkGraph &G, JITDylib &JD);

    std::mutex PluginMutex;
    ELFNixPlatform &MP;
    InitSymbolDepMap InitSymbolDeps;
  };

  using SendInitializerSequenceFn =
      unique_function<void(Expected<ELFNixJITDylibInitializerSequence>)>;

  using SendDeinitializerSequenceFn =
      unique_function<void(Expected<ELFNixJITDylibDeinitializerSequence>)>;

  using SendSymbolAddressFn = unique_function<void(Expected<ExecutorAddr>)>;

  static bool supportedTarget(const Triple &TT);

  ELFNixPlatform(ExecutionSession &ES, ObjectLinkingLayer &ObjLinkingLayer,
                 JITDylib &PlatformJD,
                 std::unique_ptr<DefinitionGenerator> OrcRuntimeGenerator,
                 Error &Err);

  // Associate ELFNixPlatform JIT-side runtime support functions with handlers.
  Error associateRuntimeSupportFunctions(JITDylib &PlatformJD);

  void getInitializersBuildSequencePhase(SendInitializerSequenceFn SendResult,
                                         JITDylib &JD,
                                         std::vector<JITDylibSP> DFSLinkOrder);

  void getInitializersLookupPhase(SendInitializerSequenceFn SendResult,
                                  JITDylib &JD);

  void rt_getInitializers(SendInitializerSequenceFn SendResult,
                          StringRef JDName);

  void rt_getDeinitializers(SendDeinitializerSequenceFn SendResult,
                            ExecutorAddr Handle);

  void rt_lookupSymbol(SendSymbolAddressFn SendResult, ExecutorAddr Handle,
                       StringRef SymbolName);

  // Records the addresses of runtime symbols used by the platform.
  Error bootstrapELFNixRuntime(JITDylib &PlatformJD);

  Error registerInitInfo(JITDylib &JD,
                         ArrayRef<jitlink::Section *> InitSections);

  Error registerPerObjectSections(const ELFPerObjectSectionsToRegister &POSR);

  Expected<uint64_t> createPThreadKey();

  ExecutionSession &ES;
  ObjectLinkingLayer &ObjLinkingLayer;

  SymbolStringPtr DSOHandleSymbol;
  std::atomic<bool> RuntimeBootstrapped{false};

  ExecutorAddr orc_rt_elfnix_platform_bootstrap;
  ExecutorAddr orc_rt_elfnix_platform_shutdown;
  ExecutorAddr orc_rt_elfnix_register_object_sections;
  ExecutorAddr orc_rt_elfnix_create_pthread_key;

  DenseMap<JITDylib *, SymbolLookupSet> RegisteredInitSymbols;

  // InitSeqs gets its own mutex to avoid locking the whole session when
  // aggregating data from the jitlink.
  std::mutex PlatformMutex;
  DenseMap<JITDylib *, ELFNixJITDylibInitializers> InitSeqs;
  std::vector<ELFPerObjectSectionsToRegister> BootstrapPOSRs;

  DenseMap<ExecutorAddr, JITDylib *> HandleAddrToJITDylib;
  DenseMap<JITDylib *, uint64_t> JITDylibToPThreadKey;
};

namespace shared {

using SPSELFPerObjectSectionsToRegister =
    SPSTuple<SPSExecutorAddrRange, SPSExecutorAddrRange>;

template <>
class SPSSerializationTraits<SPSELFPerObjectSectionsToRegister,
                             ELFPerObjectSectionsToRegister> {

public:
  static size_t size(const ELFPerObjectSectionsToRegister &MOPOSR) {
    return SPSELFPerObjectSectionsToRegister::AsArgList::size(
        MOPOSR.EHFrameSection, MOPOSR.ThreadDataSection);
  }

  static bool serialize(SPSOutputBuffer &OB,
                        const ELFPerObjectSectionsToRegister &MOPOSR) {
    return SPSELFPerObjectSectionsToRegister::AsArgList::serialize(
        OB, MOPOSR.EHFrameSection, MOPOSR.ThreadDataSection);
  }

  static bool deserialize(SPSInputBuffer &IB,
                          ELFPerObjectSectionsToRegister &MOPOSR) {
    return SPSELFPerObjectSectionsToRegister::AsArgList::deserialize(
        IB, MOPOSR.EHFrameSection, MOPOSR.ThreadDataSection);
  }
};

using SPSNamedExecutorAddrRangeSequenceMap =
    SPSSequence<SPSTuple<SPSString, SPSExecutorAddrRangeSequence>>;

using SPSELFNixJITDylibInitializers =
    SPSTuple<SPSString, SPSExecutorAddr, SPSNamedExecutorAddrRangeSequenceMap>;

using SPSELFNixJITDylibInitializerSequence =
    SPSSequence<SPSELFNixJITDylibInitializers>;

/// Serialization traits for ELFNixJITDylibInitializers.
template <>
class SPSSerializationTraits<SPSELFNixJITDylibInitializers,
                             ELFNixJITDylibInitializers> {
public:
  static size_t size(const ELFNixJITDylibInitializers &MOJDIs) {
    return SPSELFNixJITDylibInitializers::AsArgList::size(
        MOJDIs.Name, MOJDIs.DSOHandleAddress, MOJDIs.InitSections);
  }

  static bool serialize(SPSOutputBuffer &OB,
                        const ELFNixJITDylibInitializers &MOJDIs) {
    return SPSELFNixJITDylibInitializers::AsArgList::serialize(
        OB, MOJDIs.Name, MOJDIs.DSOHandleAddress, MOJDIs.InitSections);
  }

  static bool deserialize(SPSInputBuffer &IB,
                          ELFNixJITDylibInitializers &MOJDIs) {
    return SPSELFNixJITDylibInitializers::AsArgList::deserialize(
        IB, MOJDIs.Name, MOJDIs.DSOHandleAddress, MOJDIs.InitSections);
  }
};

using SPSELFJITDylibDeinitializers = SPSEmpty;

using SPSELFJITDylibDeinitializerSequence =
    SPSSequence<SPSELFJITDylibDeinitializers>;

template <>
class SPSSerializationTraits<SPSELFJITDylibDeinitializers,
                             ELFNixJITDylibDeinitializers> {
public:
  static size_t size(const ELFNixJITDylibDeinitializers &MOJDDs) { return 0; }

  static bool serialize(SPSOutputBuffer &OB,
                        const ELFNixJITDylibDeinitializers &MOJDDs) {
    return true;
  }

  static bool deserialize(SPSInputBuffer &IB,
                          ELFNixJITDylibDeinitializers &MOJDDs) {
    MOJDDs = ELFNixJITDylibDeinitializers();
    return true;
  }
};

} // end namespace shared
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_ELFNIXPLATFORM_H
