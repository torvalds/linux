//===----- LLJIT.h -- An ORC-based JIT for compiling LLVM IR ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An ORC-based JIT for compiling LLVM IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LLJIT_H
#define LLVM_EXECUTIONENGINE_ORC_LLJIT_H

#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ThreadPool.h"
#include <variant>

namespace llvm {
namespace orc {

class LLJITBuilderState;
class LLLazyJITBuilderState;
class ObjectTransformLayer;
class ExecutorProcessControl;

/// A pre-fabricated ORC JIT stack that can serve as an alternative to MCJIT.
///
/// Create instances using LLJITBuilder.
class LLJIT {
  template <typename, typename, typename> friend class LLJITBuilderSetters;

  friend Expected<JITDylibSP> setUpGenericLLVMIRPlatform(LLJIT &J);

public:
  /// Initializer support for LLJIT.
  class PlatformSupport {
  public:
    virtual ~PlatformSupport();

    virtual Error initialize(JITDylib &JD) = 0;

    virtual Error deinitialize(JITDylib &JD) = 0;

  protected:
    static void setInitTransform(LLJIT &J,
                                 IRTransformLayer::TransformFunction T);
  };

  /// Destruct this instance. If a multi-threaded instance, waits for all
  /// compile threads to complete.
  virtual ~LLJIT();

  /// Returns the ExecutionSession for this instance.
  ExecutionSession &getExecutionSession() { return *ES; }

  /// Returns a reference to the triple for this instance.
  const Triple &getTargetTriple() const { return TT; }

  /// Returns a reference to the DataLayout for this instance.
  const DataLayout &getDataLayout() const { return DL; }

  /// Returns a reference to the JITDylib representing the JIT'd main program.
  JITDylib &getMainJITDylib() { return *Main; }

  /// Returns the ProcessSymbols JITDylib, which by default reflects non-JIT'd
  /// symbols in the host process.
  ///
  /// Note: JIT'd code should not be added to the ProcessSymbols JITDylib. Use
  /// the main JITDylib or a custom JITDylib instead.
  JITDylibSP getProcessSymbolsJITDylib();

  /// Returns the Platform JITDylib, which will contain the ORC runtime (if
  /// given) and any platform symbols.
  ///
  /// Note: JIT'd code should not be added to the Platform JITDylib. Use the
  /// main JITDylib or a custom JITDylib instead.
  JITDylibSP getPlatformJITDylib();

  /// Returns the JITDylib with the given name, or nullptr if no JITDylib with
  /// that name exists.
  JITDylib *getJITDylibByName(StringRef Name) {
    return ES->getJITDylibByName(Name);
  }

  /// Load a (real) dynamic library and make its symbols available through a
  /// new JITDylib with the same name.
  ///
  /// If the given *executor* path contains a valid platform dynamic library
  /// then that library will be loaded, and a new bare JITDylib whose name is
  /// the given path will be created to make the library's symbols available to
  /// JIT'd code.
  Expected<JITDylib &> loadPlatformDynamicLibrary(const char *Path);

  /// Link a static library into the given JITDylib.
  ///
  /// If the given MemoryBuffer contains a valid static archive (or a universal
  /// binary with an archive slice that fits the LLJIT instance's platform /
  /// architecture) then it will be added to the given JITDylib using a
  /// StaticLibraryDefinitionGenerator.
  Error linkStaticLibraryInto(JITDylib &JD,
                              std::unique_ptr<MemoryBuffer> LibBuffer);

  /// Link a static library into the given JITDylib.
  ///
  /// If the given *host* path contains a valid static archive (or a universal
  /// binary with an archive slice that fits the LLJIT instance's platform /
  /// architecture) then it will be added to the given JITDylib using a
  /// StaticLibraryDefinitionGenerator.
  Error linkStaticLibraryInto(JITDylib &JD, const char *Path);

  /// Create a new JITDylib with the given name and return a reference to it.
  ///
  /// JITDylib names must be unique. If the given name is derived from user
  /// input or elsewhere in the environment then the client should check
  /// (e.g. by calling getJITDylibByName) that the given name is not already in
  /// use.
  Expected<JITDylib &> createJITDylib(std::string Name);

  /// Returns the default link order for this LLJIT instance. This link order
  /// will be appended to the link order of JITDylibs created by LLJIT's
  /// createJITDylib method.
  JITDylibSearchOrder defaultLinkOrder() { return DefaultLinks; }

  /// Adds an IR module with the given ResourceTracker.
  Error addIRModule(ResourceTrackerSP RT, ThreadSafeModule TSM);

  /// Adds an IR module to the given JITDylib.
  Error addIRModule(JITDylib &JD, ThreadSafeModule TSM);

  /// Adds an IR module to the Main JITDylib.
  Error addIRModule(ThreadSafeModule TSM) {
    return addIRModule(*Main, std::move(TSM));
  }

  /// Adds an object file to the given JITDylib.
  Error addObjectFile(ResourceTrackerSP RT, std::unique_ptr<MemoryBuffer> Obj);

  /// Adds an object file to the given JITDylib.
  Error addObjectFile(JITDylib &JD, std::unique_ptr<MemoryBuffer> Obj);

  /// Adds an object file to the given JITDylib.
  Error addObjectFile(std::unique_ptr<MemoryBuffer> Obj) {
    return addObjectFile(*Main, std::move(Obj));
  }

  /// Look up a symbol in JITDylib JD by the symbol's linker-mangled name (to
  /// look up symbols based on their IR name use the lookup function instead).
  Expected<ExecutorAddr> lookupLinkerMangled(JITDylib &JD,
                                             SymbolStringPtr Name);

  /// Look up a symbol in JITDylib JD by the symbol's linker-mangled name (to
  /// look up symbols based on their IR name use the lookup function instead).
  Expected<ExecutorAddr> lookupLinkerMangled(JITDylib &JD,
                                             StringRef Name) {
    return lookupLinkerMangled(JD, ES->intern(Name));
  }

  /// Look up a symbol in the main JITDylib by the symbol's linker-mangled name
  /// (to look up symbols based on their IR name use the lookup function
  /// instead).
  Expected<ExecutorAddr> lookupLinkerMangled(StringRef Name) {
    return lookupLinkerMangled(*Main, Name);
  }

  /// Look up a symbol in JITDylib JD based on its IR symbol name.
  Expected<ExecutorAddr> lookup(JITDylib &JD, StringRef UnmangledName) {
    return lookupLinkerMangled(JD, mangle(UnmangledName));
  }

  /// Look up a symbol in the main JITDylib based on its IR symbol name.
  Expected<ExecutorAddr> lookup(StringRef UnmangledName) {
    return lookup(*Main, UnmangledName);
  }

  /// Set the PlatformSupport instance.
  void setPlatformSupport(std::unique_ptr<PlatformSupport> PS) {
    this->PS = std::move(PS);
  }

  /// Get the PlatformSupport instance.
  PlatformSupport *getPlatformSupport() { return PS.get(); }

  /// Run the initializers for the given JITDylib.
  Error initialize(JITDylib &JD) {
    DEBUG_WITH_TYPE("orc", {
      dbgs() << "LLJIT running initializers for JITDylib \"" << JD.getName()
             << "\"\n";
    });
    assert(PS && "PlatformSupport must be set to run initializers.");
    return PS->initialize(JD);
  }

  /// Run the deinitializers for the given JITDylib.
  Error deinitialize(JITDylib &JD) {
    DEBUG_WITH_TYPE("orc", {
      dbgs() << "LLJIT running deinitializers for JITDylib \"" << JD.getName()
             << "\"\n";
    });
    assert(PS && "PlatformSupport must be set to run initializers.");
    return PS->deinitialize(JD);
  }

  /// Returns a reference to the ObjLinkingLayer
  ObjectLayer &getObjLinkingLayer() { return *ObjLinkingLayer; }

  /// Returns a reference to the object transform layer.
  ObjectTransformLayer &getObjTransformLayer() { return *ObjTransformLayer; }

  /// Returns a reference to the IR transform layer.
  IRTransformLayer &getIRTransformLayer() { return *TransformLayer; }

  /// Returns a reference to the IR compile layer.
  IRCompileLayer &getIRCompileLayer() { return *CompileLayer; }

  /// Returns a linker-mangled version of UnmangledName.
  std::string mangle(StringRef UnmangledName) const;

  /// Returns an interned, linker-mangled version of UnmangledName.
  SymbolStringPtr mangleAndIntern(StringRef UnmangledName) const {
    return ES->intern(mangle(UnmangledName));
  }

protected:
  static Expected<std::unique_ptr<ObjectLayer>>
  createObjectLinkingLayer(LLJITBuilderState &S, ExecutionSession &ES);

  static Expected<std::unique_ptr<IRCompileLayer::IRCompiler>>
  createCompileFunction(LLJITBuilderState &S, JITTargetMachineBuilder JTMB);

  /// Create an LLJIT instance with a single compile thread.
  LLJIT(LLJITBuilderState &S, Error &Err);

  Error applyDataLayout(Module &M);

  void recordCtorDtors(Module &M);

  std::unique_ptr<ExecutionSession> ES;
  std::unique_ptr<PlatformSupport> PS;

  JITDylib *ProcessSymbols = nullptr;
  JITDylib *Platform = nullptr;
  JITDylib *Main = nullptr;

  JITDylibSearchOrder DefaultLinks;

  DataLayout DL;
  Triple TT;

  std::unique_ptr<ObjectLayer> ObjLinkingLayer;
  std::unique_ptr<ObjectTransformLayer> ObjTransformLayer;
  std::unique_ptr<IRCompileLayer> CompileLayer;
  std::unique_ptr<IRTransformLayer> TransformLayer;
  std::unique_ptr<IRTransformLayer> InitHelperTransformLayer;
};

/// An extended version of LLJIT that supports lazy function-at-a-time
/// compilation of LLVM IR.
class LLLazyJIT : public LLJIT {
  template <typename, typename, typename> friend class LLJITBuilderSetters;

public:

  /// Sets the partition function.
  void
  setPartitionFunction(CompileOnDemandLayer::PartitionFunction Partition) {
    CODLayer->setPartitionFunction(std::move(Partition));
  }

  /// Returns a reference to the on-demand layer.
  CompileOnDemandLayer &getCompileOnDemandLayer() { return *CODLayer; }

  /// Add a module to be lazily compiled to JITDylib JD.
  Error addLazyIRModule(JITDylib &JD, ThreadSafeModule M);

  /// Add a module to be lazily compiled to the main JITDylib.
  Error addLazyIRModule(ThreadSafeModule M) {
    return addLazyIRModule(*Main, std::move(M));
  }

private:

  // Create a single-threaded LLLazyJIT instance.
  LLLazyJIT(LLLazyJITBuilderState &S, Error &Err);

  std::unique_ptr<LazyCallThroughManager> LCTMgr;
  std::unique_ptr<CompileOnDemandLayer> CODLayer;
};

class LLJITBuilderState {
public:
  using ObjectLinkingLayerCreator =
      std::function<Expected<std::unique_ptr<ObjectLayer>>(ExecutionSession &,
                                                           const Triple &)>;

  using CompileFunctionCreator =
      std::function<Expected<std::unique_ptr<IRCompileLayer::IRCompiler>>(
          JITTargetMachineBuilder JTMB)>;

  using ProcessSymbolsJITDylibSetupFunction =
      unique_function<Expected<JITDylibSP>(LLJIT &J)>;

  using PlatformSetupFunction = unique_function<Expected<JITDylibSP>(LLJIT &J)>;

  using NotifyCreatedFunction = std::function<Error(LLJIT &)>;

  std::unique_ptr<ExecutorProcessControl> EPC;
  std::unique_ptr<ExecutionSession> ES;
  std::optional<JITTargetMachineBuilder> JTMB;
  std::optional<DataLayout> DL;
  bool LinkProcessSymbolsByDefault = true;
  ProcessSymbolsJITDylibSetupFunction SetupProcessSymbolsJITDylib;
  ObjectLinkingLayerCreator CreateObjectLinkingLayer;
  CompileFunctionCreator CreateCompileFunction;
  unique_function<Error(LLJIT &)> PrePlatformSetup;
  PlatformSetupFunction SetUpPlatform;
  NotifyCreatedFunction NotifyCreated;
  unsigned NumCompileThreads = 0;
  std::optional<bool> SupportConcurrentCompilation;

  /// Called prior to JIT class construcion to fix up defaults.
  Error prepareForConstruction();
};

template <typename JITType, typename SetterImpl, typename State>
class LLJITBuilderSetters {
public:
  /// Set an ExecutorProcessControl for this instance.
  /// This should not be called if ExecutionSession has already been set.
  SetterImpl &
  setExecutorProcessControl(std::unique_ptr<ExecutorProcessControl> EPC) {
    assert(
        !impl().ES &&
        "setExecutorProcessControl should not be called if an ExecutionSession "
        "has already been set");
    impl().EPC = std::move(EPC);
    return impl();
  }

  /// Set an ExecutionSession for this instance.
  SetterImpl &setExecutionSession(std::unique_ptr<ExecutionSession> ES) {
    assert(
        !impl().EPC &&
        "setExecutionSession should not be called if an ExecutorProcessControl "
        "object has already been set");
    impl().ES = std::move(ES);
    return impl();
  }

  /// Set the JITTargetMachineBuilder for this instance.
  ///
  /// If this method is not called, JITTargetMachineBuilder::detectHost will be
  /// used to construct a default target machine builder for the host platform.
  SetterImpl &setJITTargetMachineBuilder(JITTargetMachineBuilder JTMB) {
    impl().JTMB = std::move(JTMB);
    return impl();
  }

  /// Return a reference to the JITTargetMachineBuilder.
  ///
  std::optional<JITTargetMachineBuilder> &getJITTargetMachineBuilder() {
    return impl().JTMB;
  }

  /// Set a DataLayout for this instance. If no data layout is specified then
  /// the target's default data layout will be used.
  SetterImpl &setDataLayout(std::optional<DataLayout> DL) {
    impl().DL = std::move(DL);
    return impl();
  }

  /// The LinkProcessSymbolsDyDefault flag determines whether the "Process"
  /// JITDylib will be added to the default link order at LLJIT construction
  /// time. If true, the Process JITDylib will be added as the last item in the
  /// default link order. If false (or if the Process JITDylib is disabled via
  /// setProcessSymbolsJITDylibSetup) then the Process JITDylib will not appear
  /// in the default link order.
  SetterImpl &setLinkProcessSymbolsByDefault(bool LinkProcessSymbolsByDefault) {
    impl().LinkProcessSymbolsByDefault = LinkProcessSymbolsByDefault;
    return impl();
  }

  /// Set a setup function for the process symbols dylib. If not provided,
  /// but LinkProcessSymbolsJITDylibByDefault is true, then the process-symbols
  /// JITDylib will be configured with a DynamicLibrarySearchGenerator with a
  /// default symbol filter.
  SetterImpl &setProcessSymbolsJITDylibSetup(
      LLJITBuilderState::ProcessSymbolsJITDylibSetupFunction
          SetupProcessSymbolsJITDylib) {
    impl().SetupProcessSymbolsJITDylib = std::move(SetupProcessSymbolsJITDylib);
    return impl();
  }

  /// Set an ObjectLinkingLayer creation function.
  ///
  /// If this method is not called, a default creation function will be used
  /// that will construct an RTDyldObjectLinkingLayer.
  SetterImpl &setObjectLinkingLayerCreator(
      LLJITBuilderState::ObjectLinkingLayerCreator CreateObjectLinkingLayer) {
    impl().CreateObjectLinkingLayer = std::move(CreateObjectLinkingLayer);
    return impl();
  }

  /// Set a CompileFunctionCreator.
  ///
  /// If this method is not called, a default creation function wil be used
  /// that will construct a basic IR compile function that is compatible with
  /// the selected number of threads (SimpleCompiler for '0' compile threads,
  /// ConcurrentIRCompiler otherwise).
  SetterImpl &setCompileFunctionCreator(
      LLJITBuilderState::CompileFunctionCreator CreateCompileFunction) {
    impl().CreateCompileFunction = std::move(CreateCompileFunction);
    return impl();
  }

  /// Set a setup function to be run just before the PlatformSetupFunction is
  /// run.
  ///
  /// This can be used to customize the LLJIT instance before the platform is
  /// set up. E.g. By installing a debugger support plugin before the platform
  /// is set up (when the ORC runtime is loaded) we enable debugging of the
  /// runtime itself.
  SetterImpl &
  setPrePlatformSetup(unique_function<Error(LLJIT &)> PrePlatformSetup) {
    impl().PrePlatformSetup = std::move(PrePlatformSetup);
    return impl();
  }

  /// Set up an PlatformSetupFunction.
  ///
  /// If this method is not called then setUpGenericLLVMIRPlatform
  /// will be used to configure the JIT's platform support.
  SetterImpl &
  setPlatformSetUp(LLJITBuilderState::PlatformSetupFunction SetUpPlatform) {
    impl().SetUpPlatform = std::move(SetUpPlatform);
    return impl();
  }

  /// Set up a callback after successful construction of the JIT.
  ///
  /// This is useful to attach generators to JITDylibs or inject initial symbol
  /// definitions.
  SetterImpl &
  setNotifyCreatedCallback(LLJITBuilderState::NotifyCreatedFunction Callback) {
    impl().NotifyCreated = std::move(Callback);
    return impl();
  }

  /// Set the number of compile threads to use.
  ///
  /// If set to zero, compilation will be performed on the execution thread when
  /// JITing in-process. If set to any other number N, a thread pool of N
  /// threads will be created for compilation.
  ///
  /// If this method is not called, behavior will be as if it were called with
  /// a zero argument.
  ///
  /// This setting should not be used if a custom ExecutionSession or
  /// ExecutorProcessControl object is set: in those cases a custom
  /// TaskDispatcher should be used instead.
  SetterImpl &setNumCompileThreads(unsigned NumCompileThreads) {
    impl().NumCompileThreads = NumCompileThreads;
    return impl();
  }

  /// If set, this forces LLJIT concurrent compilation support to be either on
  /// or off. This controls the selection of compile function (concurrent vs
  /// single threaded) and whether or not sub-modules are cloned to new
  /// contexts for lazy emission.
  ///
  /// If not explicitly set then concurrency support will be turned on if
  /// NumCompileThreads is set to a non-zero value, or if a custom
  /// ExecutionSession or ExecutorProcessControl instance is provided.
  SetterImpl &setSupportConcurrentCompilation(
      std::optional<bool> SupportConcurrentCompilation) {
    impl().SupportConcurrentCompilation = SupportConcurrentCompilation;
    return impl();
  }

  /// Create an instance of the JIT.
  Expected<std::unique_ptr<JITType>> create() {
    if (auto Err = impl().prepareForConstruction())
      return std::move(Err);

    Error Err = Error::success();
    std::unique_ptr<JITType> J(new JITType(impl(), Err));
    if (Err)
      return std::move(Err);

    if (impl().NotifyCreated)
      if (Error Err = impl().NotifyCreated(*J))
        return std::move(Err);

    return std::move(J);
  }

protected:
  SetterImpl &impl() { return static_cast<SetterImpl &>(*this); }
};

/// Constructs LLJIT instances.
class LLJITBuilder
    : public LLJITBuilderState,
      public LLJITBuilderSetters<LLJIT, LLJITBuilder, LLJITBuilderState> {};

class LLLazyJITBuilderState : public LLJITBuilderState {
  friend class LLLazyJIT;

public:
  using IndirectStubsManagerBuilderFunction =
      std::function<std::unique_ptr<IndirectStubsManager>()>;

  Triple TT;
  ExecutorAddr LazyCompileFailureAddr;
  std::unique_ptr<LazyCallThroughManager> LCTMgr;
  IndirectStubsManagerBuilderFunction ISMBuilder;

  Error prepareForConstruction();
};

template <typename JITType, typename SetterImpl, typename State>
class LLLazyJITBuilderSetters
    : public LLJITBuilderSetters<JITType, SetterImpl, State> {
public:
  /// Set the address in the target address to call if a lazy compile fails.
  ///
  /// If this method is not called then the value will default to 0.
  SetterImpl &setLazyCompileFailureAddr(ExecutorAddr Addr) {
    this->impl().LazyCompileFailureAddr = Addr;
    return this->impl();
  }

  /// Set the lazy-callthrough manager.
  ///
  /// If this method is not called then a default, in-process lazy callthrough
  /// manager for the host platform will be used.
  SetterImpl &
  setLazyCallthroughManager(std::unique_ptr<LazyCallThroughManager> LCTMgr) {
    this->impl().LCTMgr = std::move(LCTMgr);
    return this->impl();
  }

  /// Set the IndirectStubsManager builder function.
  ///
  /// If this method is not called then a default, in-process
  /// IndirectStubsManager builder for the host platform will be used.
  SetterImpl &setIndirectStubsManagerBuilder(
      LLLazyJITBuilderState::IndirectStubsManagerBuilderFunction ISMBuilder) {
    this->impl().ISMBuilder = std::move(ISMBuilder);
    return this->impl();
  }
};

/// Constructs LLLazyJIT instances.
class LLLazyJITBuilder
    : public LLLazyJITBuilderState,
      public LLLazyJITBuilderSetters<LLLazyJIT, LLLazyJITBuilder,
                                     LLLazyJITBuilderState> {};

/// Configure the LLJIT instance to use orc runtime support. This overload
/// assumes that the client has manually configured a Platform object.
Error setUpOrcPlatformManually(LLJIT &J);

/// Configure the LLJIT instance to use the ORC runtime and the detected
/// native target for the executor.
class ExecutorNativePlatform {
public:
  /// Set up using path to Orc runtime.
  ExecutorNativePlatform(std::string OrcRuntimePath)
      : OrcRuntime(std::move(OrcRuntimePath)) {}

  /// Set up using the given memory buffer.
  ExecutorNativePlatform(std::unique_ptr<MemoryBuffer> OrcRuntimeMB)
      : OrcRuntime(std::move(OrcRuntimeMB)) {}

  // TODO: add compiler-rt.

  /// Add a path to the VC runtime.
  ExecutorNativePlatform &addVCRuntime(std::string VCRuntimePath,
                                       bool StaticVCRuntime) {
    VCRuntime = {std::move(VCRuntimePath), StaticVCRuntime};
    return *this;
  }

  Expected<JITDylibSP> operator()(LLJIT &J);

private:
  std::variant<std::string, std::unique_ptr<MemoryBuffer>> OrcRuntime;
  std::optional<std::pair<std::string, bool>> VCRuntime;
};

/// Configure the LLJIT instance to scrape modules for llvm.global_ctors and
/// llvm.global_dtors variables and (if present) build initialization and
/// deinitialization functions. Platform specific initialization configurations
/// should be preferred where available.
Expected<JITDylibSP> setUpGenericLLVMIRPlatform(LLJIT &J);

/// Configure the LLJIT instance to disable platform support explicitly. This is
/// useful in two cases: for platforms that don't have such requirements and for
/// platforms, that we have no explicit support yet and that don't work well
/// with the generic IR platform.
Expected<JITDylibSP> setUpInactivePlatform(LLJIT &J);

/// A Platform-support class that implements initialize / deinitialize by
/// forwarding to ORC runtime dlopen / dlclose operations.
class ORCPlatformSupport : public LLJIT::PlatformSupport {
public:
  ORCPlatformSupport(orc::LLJIT &J) : J(J) {}
  Error initialize(orc::JITDylib &JD) override;
  Error deinitialize(orc::JITDylib &JD) override;

private:
  orc::LLJIT &J;
  DenseMap<orc::JITDylib *, orc::ExecutorAddr> DSOHandles;
};

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LLJIT_H
