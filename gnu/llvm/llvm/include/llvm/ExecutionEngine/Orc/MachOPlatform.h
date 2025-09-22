//===-- MachOPlatform.h - Utilities for executing MachO in Orc --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for executing JIT'd MachO in Orc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MACHOPLATFORM_H
#define LLVM_EXECUTIONENGINE_ORC_MACHOPLATFORM_H

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

/// Mediates between MachO initialization and ExecutionSession state.
class MachOPlatform : public Platform {
public:
  // Used internally by MachOPlatform, but made public to enable serialization.
  struct MachOJITDylibDepInfo {
    bool Sealed = false;
    std::vector<ExecutorAddr> DepHeaders;
  };

  // Used internally by MachOPlatform, but made public to enable serialization.
  using MachOJITDylibDepInfoMap =
      std::vector<std::pair<ExecutorAddr, MachOJITDylibDepInfo>>;

  // Used internally by MachOPlatform, but made public to enable serialization.
  enum class MachOExecutorSymbolFlags : uint8_t {
    None = 0,
    Weak = 1U << 0,
    Callable = 1U << 1,
    LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ Callable)
  };

  /// Configuration for the mach-o header of a JITDylib. Specify common load
  /// commands that should be added to the header.
  struct HeaderOptions {
    /// A dylib for use with a dylib command (e.g. LC_ID_DYLIB, LC_LOAD_DYLIB).
    struct Dylib {
      std::string Name;
      uint32_t Timestamp;
      uint32_t CurrentVersion;
      uint32_t CompatibilityVersion;
    };

    struct BuildVersionOpts {

      // Derive platform from triple if possible.
      static std::optional<BuildVersionOpts>
      fromTriple(const Triple &TT, uint32_t MinOS, uint32_t SDK);

      uint32_t Platform; // Platform.
      uint32_t MinOS;    // X.Y.Z is encoded in nibbles xxxx.yy.zz
      uint32_t SDK;      // X.Y.Z is encoded in nibbles xxxx.yy.zz
    };

    /// Override for LC_IC_DYLIB. If this is nullopt, {JD.getName(), 0, 0, 0}
    /// will be used.
    std::optional<Dylib> IDDylib;

    /// List of LC_LOAD_DYLIBs.
    std::vector<Dylib> LoadDylibs;
    /// List of LC_RPATHs.
    std::vector<std::string> RPaths;
    /// List of LC_BUILD_VERSIONs.
    std::vector<BuildVersionOpts> BuildVersions;

    HeaderOptions() = default;
    HeaderOptions(Dylib D) : IDDylib(std::move(D)) {}
  };

  /// Used by setupJITDylib to create MachO header MaterializationUnits for
  /// JITDylibs.
  using MachOHeaderMUBuilder =
      unique_function<std::unique_ptr<MaterializationUnit>(MachOPlatform &MOP,
                                                           HeaderOptions Opts)>;

  /// Simple MachO header graph builder.
  static inline std::unique_ptr<MaterializationUnit>
  buildSimpleMachOHeaderMU(MachOPlatform &MOP, HeaderOptions Opts);

  /// Try to create a MachOPlatform instance, adding the ORC runtime to the
  /// given JITDylib.
  ///
  /// The ORC runtime requires access to a number of symbols in libc++, and
  /// requires access to symbols in libobjc, and libswiftCore to support
  /// Objective-C and Swift code. It is up to the caller to ensure that the
  /// required symbols can be referenced by code added to PlatformJD. The
  /// standard way to achieve this is to first attach dynamic library search
  /// generators for either the given process, or for the specific required
  /// libraries, to PlatformJD, then to create the platform instance:
  ///
  /// \code{.cpp}
  ///   auto &PlatformJD = ES.createBareJITDylib("stdlib");
  ///   PlatformJD.addGenerator(
  ///     ExitOnErr(EPCDynamicLibrarySearchGenerator
  ///                 ::GetForTargetProcess(EPC)));
  ///   ES.setPlatform(
  ///     ExitOnErr(MachOPlatform::Create(ES, ObjLayer, EPC, PlatformJD,
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
  /// By default, MachOPlatform will add the set of aliases returned by the
  /// standardPlatformAliases function. This includes both required aliases
  /// (e.g. __cxa_atexit -> __orc_rt_macho_cxa_atexit for static destructor
  /// support), and optional aliases that provide JIT versions of common
  /// functions (e.g. dlopen -> __orc_rt_macho_jit_dlopen). Clients can
  /// override these defaults by passing a non-None value for the
  /// RuntimeAliases function, in which case the client is responsible for
  /// setting up all aliases (including the required ones).
  static Expected<std::unique_ptr<MachOPlatform>>
  Create(ExecutionSession &ES, ObjectLinkingLayer &ObjLinkingLayer,
         JITDylib &PlatformJD, std::unique_ptr<DefinitionGenerator> OrcRuntime,
         HeaderOptions PlatformJDOpts = {},
         MachOHeaderMUBuilder BuildMachOHeaderMU = buildSimpleMachOHeaderMU,
         std::optional<SymbolAliasMap> RuntimeAliases = std::nullopt);

  /// Construct using a path to the ORC runtime.
  static Expected<std::unique_ptr<MachOPlatform>>
  Create(ExecutionSession &ES, ObjectLinkingLayer &ObjLinkingLayer,
         JITDylib &PlatformJD, const char *OrcRuntimePath,
         HeaderOptions PlatformJDOpts = {},
         MachOHeaderMUBuilder BuildMachOHeaderMU = buildSimpleMachOHeaderMU,
         std::optional<SymbolAliasMap> RuntimeAliases = std::nullopt);

  ExecutionSession &getExecutionSession() const { return ES; }
  ObjectLinkingLayer &getObjectLinkingLayer() const { return ObjLinkingLayer; }

  NonOwningSymbolStringPtr getMachOHeaderStartSymbol() const {
    return NonOwningSymbolStringPtr(MachOHeaderStartSymbol);
  }

  Error setupJITDylib(JITDylib &JD) override;

  /// Install any platform-specific symbols (e.g. `__dso_handle`) and create a
  /// mach-o header based on the given options.
  Error setupJITDylib(JITDylib &JD, HeaderOptions Opts);

  Error teardownJITDylib(JITDylib &JD) override;
  Error notifyAdding(ResourceTracker &RT,
                     const MaterializationUnit &MU) override;
  Error notifyRemoving(ResourceTracker &RT) override;

  /// Returns an AliasMap containing the default aliases for the MachOPlatform.
  /// This can be modified by clients when constructing the platform to add
  /// or remove aliases.
  static SymbolAliasMap standardPlatformAliases(ExecutionSession &ES);

  /// Returns the array of required CXX aliases.
  static ArrayRef<std::pair<const char *, const char *>> requiredCXXAliases();

  /// Returns the array of standard runtime utility aliases for MachO.
  static ArrayRef<std::pair<const char *, const char *>>
  standardRuntimeUtilityAliases();

private:
  using SymbolTableVector = SmallVector<
      std::tuple<ExecutorAddr, ExecutorAddr, MachOExecutorSymbolFlags>>;

  // Data needed for bootstrap only.
  struct BootstrapInfo {
    std::mutex Mutex;
    std::condition_variable CV;
    size_t ActiveGraphs = 0;
    shared::AllocActions DeferredAAs;
    ExecutorAddr MachOHeaderAddr;
    SymbolTableVector SymTab;
  };

  // The MachOPlatformPlugin scans/modifies LinkGraphs to support MachO
  // platform features including initializers, exceptions, TLV, and language
  // runtime registration.
  class MachOPlatformPlugin : public ObjectLinkingLayer::Plugin {
  public:
    MachOPlatformPlugin(MachOPlatform &MP) : MP(MP) {}

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

    struct UnwindSections {
      SmallVector<ExecutorAddrRange> CodeRanges;
      ExecutorAddrRange DwarfSection;
      ExecutorAddrRange CompactUnwindSection;
    };

    struct ObjCImageInfo {
      uint32_t Version = 0;
      uint32_t Flags = 0;
      /// Whether this image info can no longer be mutated, as it may have been
      /// registered with the objc runtime.
      bool Finalized = false;
    };

    struct SymbolTablePair {
      jitlink::Symbol *OriginalSym = nullptr;
      jitlink::Symbol *NameSym = nullptr;
    };
    using JITSymTabVector = SmallVector<SymbolTablePair>;

    Error bootstrapPipelineStart(jitlink::LinkGraph &G);
    Error bootstrapPipelineRecordRuntimeFunctions(jitlink::LinkGraph &G);
    Error bootstrapPipelineEnd(jitlink::LinkGraph &G);

    Error associateJITDylibHeaderSymbol(jitlink::LinkGraph &G,
                                        MaterializationResponsibility &MR);

    Error preserveImportantSections(jitlink::LinkGraph &G,
                                    MaterializationResponsibility &MR);

    Error processObjCImageInfo(jitlink::LinkGraph &G,
                               MaterializationResponsibility &MR);
    Error mergeImageInfoFlags(jitlink::LinkGraph &G,
                              MaterializationResponsibility &MR,
                              ObjCImageInfo &Info, uint32_t NewFlags);

    Error fixTLVSectionsAndEdges(jitlink::LinkGraph &G, JITDylib &JD);

    std::optional<UnwindSections> findUnwindSectionInfo(jitlink::LinkGraph &G);
    Error registerObjectPlatformSections(jitlink::LinkGraph &G, JITDylib &JD,
                                         bool InBootstrapPhase);

    Error createObjCRuntimeObject(jitlink::LinkGraph &G);
    Error populateObjCRuntimeObject(jitlink::LinkGraph &G,
                                    MaterializationResponsibility &MR);

    Error prepareSymbolTableRegistration(jitlink::LinkGraph &G,
                                         JITSymTabVector &JITSymTabInfo);
    Error addSymbolTableRegistration(jitlink::LinkGraph &G,
                                     MaterializationResponsibility &MR,
                                     JITSymTabVector &JITSymTabInfo,
                                     bool InBootstrapPhase);

    std::mutex PluginMutex;
    MachOPlatform &MP;

    // FIXME: ObjCImageInfos and HeaderAddrs need to be cleared when
    // JITDylibs are removed.
    DenseMap<JITDylib *, ObjCImageInfo> ObjCImageInfos;
    DenseMap<JITDylib *, ExecutorAddr> HeaderAddrs;
    InitSymbolDepMap InitSymbolDeps;
  };

  using GetJITDylibHeaderSendResultFn =
      unique_function<void(Expected<ExecutorAddr>)>;
  using GetJITDylibNameSendResultFn =
      unique_function<void(Expected<StringRef>)>;
  using PushInitializersSendResultFn =
      unique_function<void(Expected<MachOJITDylibDepInfoMap>)>;
  using SendSymbolAddressFn = unique_function<void(Expected<ExecutorAddr>)>;
  using PushSymbolsInSendResultFn = unique_function<void(Error)>;

  static bool supportedTarget(const Triple &TT);

  static jitlink::Edge::Kind getPointerEdgeKind(jitlink::LinkGraph &G);

  static MachOExecutorSymbolFlags flagsForSymbol(jitlink::Symbol &Sym);

  MachOPlatform(ExecutionSession &ES, ObjectLinkingLayer &ObjLinkingLayer,
                JITDylib &PlatformJD,
                std::unique_ptr<DefinitionGenerator> OrcRuntimeGenerator,
                HeaderOptions PlatformJDOpts,
                MachOHeaderMUBuilder BuildMachOHeaderMU, Error &Err);

  // Associate MachOPlatform JIT-side runtime support functions with handlers.
  Error associateRuntimeSupportFunctions();

  // Implements rt_pushInitializers by making repeat async lookups for
  // initializer symbols (each lookup may spawn more initializer symbols if
  // it pulls in new materializers, e.g. from objects in a static library).
  void pushInitializersLoop(PushInitializersSendResultFn SendResult,
                            JITDylibSP JD);

  // Handle requests from the ORC runtime to push MachO initializer info.
  void rt_pushInitializers(PushInitializersSendResultFn SendResult,
                           ExecutorAddr JDHeaderAddr);

  // Request that that the given symbols be materialized. The bool element of
  // each pair indicates whether the symbol must be initialized, or whether it
  // is optional. If any required symbol is not found then the pushSymbols
  // function will return an error.
  void rt_pushSymbols(PushSymbolsInSendResultFn SendResult, ExecutorAddr Handle,
                      const std::vector<std::pair<StringRef, bool>> &Symbols);

  // Call the ORC runtime to create a pthread key.
  Expected<uint64_t> createPThreadKey();

  ExecutionSession &ES;
  JITDylib &PlatformJD;
  ObjectLinkingLayer &ObjLinkingLayer;
  MachOHeaderMUBuilder BuildMachOHeaderMU;

  SymbolStringPtr MachOHeaderStartSymbol = ES.intern("___dso_handle");

  struct RuntimeFunction {
    RuntimeFunction(SymbolStringPtr Name) : Name(std::move(Name)) {}
    SymbolStringPtr Name;
    ExecutorAddr Addr;
  };

  RuntimeFunction PlatformBootstrap{
      ES.intern("___orc_rt_macho_platform_bootstrap")};
  RuntimeFunction PlatformShutdown{
      ES.intern("___orc_rt_macho_platform_shutdown")};
  RuntimeFunction RegisterEHFrameSection{
      ES.intern("___orc_rt_macho_register_ehframe_section")};
  RuntimeFunction DeregisterEHFrameSection{
      ES.intern("___orc_rt_macho_deregister_ehframe_section")};
  RuntimeFunction RegisterJITDylib{
      ES.intern("___orc_rt_macho_register_jitdylib")};
  RuntimeFunction DeregisterJITDylib{
      ES.intern("___orc_rt_macho_deregister_jitdylib")};
  RuntimeFunction RegisterObjectSymbolTable{
      ES.intern("___orc_rt_macho_register_object_symbol_table")};
  RuntimeFunction DeregisterObjectSymbolTable{
      ES.intern("___orc_rt_macho_deregister_object_symbol_table")};
  RuntimeFunction RegisterObjectPlatformSections{
      ES.intern("___orc_rt_macho_register_object_platform_sections")};
  RuntimeFunction DeregisterObjectPlatformSections{
      ES.intern("___orc_rt_macho_deregister_object_platform_sections")};
  RuntimeFunction CreatePThreadKey{
      ES.intern("___orc_rt_macho_create_pthread_key")};
  RuntimeFunction RegisterObjCRuntimeObject{
      ES.intern("___orc_rt_macho_register_objc_runtime_object")};
  RuntimeFunction DeregisterObjCRuntimeObject{
      ES.intern("___orc_rt_macho_deregister_objc_runtime_object")};

  DenseMap<JITDylib *, SymbolLookupSet> RegisteredInitSymbols;

  std::mutex PlatformMutex;
  DenseMap<JITDylib *, ExecutorAddr> JITDylibToHeaderAddr;
  DenseMap<ExecutorAddr, JITDylib *> HeaderAddrToJITDylib;
  DenseMap<JITDylib *, uint64_t> JITDylibToPThreadKey;

  std::atomic<BootstrapInfo *> Bootstrap;
};

// Generates a MachO header.
class SimpleMachOHeaderMU : public MaterializationUnit {
public:
  SimpleMachOHeaderMU(MachOPlatform &MOP, SymbolStringPtr HeaderStartSymbol,
                      MachOPlatform::HeaderOptions Opts);
  StringRef getName() const override { return "MachOHeaderMU"; }
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Sym) override;

protected:
  virtual jitlink::Block &createHeaderBlock(JITDylib &JD, jitlink::LinkGraph &G,
                                            jitlink::Section &HeaderSection);

  MachOPlatform &MOP;
  MachOPlatform::HeaderOptions Opts;

private:
  struct HeaderSymbol {
    const char *Name;
    uint64_t Offset;
  };

  static constexpr HeaderSymbol AdditionalHeaderSymbols[] = {
      {"___mh_executable_header", 0}};

  void addMachOHeader(JITDylib &JD, jitlink::LinkGraph &G,
                      const SymbolStringPtr &InitializerSymbol);
  static MaterializationUnit::Interface
  createHeaderInterface(MachOPlatform &MOP,
                        const SymbolStringPtr &HeaderStartSymbol);
};

/// Simple MachO header graph builder.
inline std::unique_ptr<MaterializationUnit>
MachOPlatform::buildSimpleMachOHeaderMU(MachOPlatform &MOP,
                                        HeaderOptions Opts) {
  return std::make_unique<SimpleMachOHeaderMU>(MOP, MOP.MachOHeaderStartSymbol,
                                               std::move(Opts));
}

struct MachOHeaderInfo {
  size_t PageSize = 0;
  uint32_t CPUType = 0;
  uint32_t CPUSubType = 0;
};
MachOHeaderInfo getMachOHeaderInfoFromTriple(const Triple &TT);

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_MACHOPLATFORM_H
