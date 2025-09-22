//===- ExecutionEngine.h - Abstract Execution Engine Interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the abstract interface that implements execution support
// for LLVM.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_EXECUTIONENGINE_H
#define LLVM_EXECUTIONENGINE_EXECUTIONENGINE_H

#include "llvm-c/ExecutionEngine.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class Constant;
class Function;
struct GenericValue;
class GlobalValue;
class GlobalVariable;
class JITEventListener;
class MCJITMemoryManager;
class ObjectCache;
class RTDyldMemoryManager;
class Triple;
class Type;

namespace object {

class Archive;
class ObjectFile;

} // end namespace object

/// Helper class for helping synchronize access to the global address map
/// table.  Access to this class should be serialized under a mutex.
class ExecutionEngineState {
public:
  using GlobalAddressMapTy = StringMap<uint64_t>;

private:
  /// GlobalAddressMap - A mapping between LLVM global symbol names values and
  /// their actualized version...
  GlobalAddressMapTy GlobalAddressMap;

  /// GlobalAddressReverseMap - This is the reverse mapping of GlobalAddressMap,
  /// used to convert raw addresses into the LLVM global value that is emitted
  /// at the address.  This map is not computed unless getGlobalValueAtAddress
  /// is called at some point.
  std::map<uint64_t, std::string> GlobalAddressReverseMap;

public:
  GlobalAddressMapTy &getGlobalAddressMap() {
    return GlobalAddressMap;
  }

  std::map<uint64_t, std::string> &getGlobalAddressReverseMap() {
    return GlobalAddressReverseMap;
  }

  /// Erase an entry from the mapping table.
  ///
  /// \returns The address that \p ToUnmap was mapped to.
  uint64_t RemoveMapping(StringRef Name);
};

using FunctionCreator = std::function<void *(const std::string &)>;

/// Abstract interface for implementation execution of LLVM modules,
/// designed to support both interpreter and just-in-time (JIT) compiler
/// implementations.
class ExecutionEngine {
  /// The state object holding the global address mapping, which must be
  /// accessed synchronously.
  //
  // FIXME: There is no particular need the entire map needs to be
  // synchronized.  Wouldn't a reader-writer design be better here?
  ExecutionEngineState EEState;

  /// The target data for the platform for which execution is being performed.
  ///
  /// Note: the DataLayout is LLVMContext specific because it has an
  /// internal cache based on type pointers. It makes unsafe to reuse the
  /// ExecutionEngine across context, we don't enforce this rule but undefined
  /// behavior can occurs if the user tries to do it.
  const DataLayout DL;

  /// Whether lazy JIT compilation is enabled.
  bool CompilingLazily;

  /// Whether JIT compilation of external global variables is allowed.
  bool GVCompilationDisabled;

  /// Whether the JIT should perform lookups of external symbols (e.g.,
  /// using dlsym).
  bool SymbolSearchingDisabled;

  /// Whether the JIT should verify IR modules during compilation.
  bool VerifyModules;

  friend class EngineBuilder;  // To allow access to JITCtor and InterpCtor.

protected:
  /// The list of Modules that we are JIT'ing from.  We use a SmallVector to
  /// optimize for the case where there is only one module.
  SmallVector<std::unique_ptr<Module>, 1> Modules;

  /// getMemoryforGV - Allocate memory for a global variable.
  virtual char *getMemoryForGV(const GlobalVariable *GV);

  static ExecutionEngine *(*MCJITCtor)(
      std::unique_ptr<Module> M, std::string *ErrorStr,
      std::shared_ptr<MCJITMemoryManager> MM,
      std::shared_ptr<LegacyJITSymbolResolver> SR,
      std::unique_ptr<TargetMachine> TM);

  static ExecutionEngine *(*InterpCtor)(std::unique_ptr<Module> M,
                                        std::string *ErrorStr);

  /// LazyFunctionCreator - If an unknown function is needed, this function
  /// pointer is invoked to create it.  If this returns null, the JIT will
  /// abort.
  FunctionCreator LazyFunctionCreator;

  /// getMangledName - Get mangled name.
  std::string getMangledName(const GlobalValue *GV);

  std::string ErrMsg;

public:
  /// lock - This lock protects the ExecutionEngine and MCJIT classes. It must
  /// be held while changing the internal state of any of those classes.
  sys::Mutex lock;

  //===--------------------------------------------------------------------===//
  //  ExecutionEngine Startup
  //===--------------------------------------------------------------------===//

  virtual ~ExecutionEngine();

  /// Add a Module to the list of modules that we can JIT from.
  virtual void addModule(std::unique_ptr<Module> M) {
    Modules.push_back(std::move(M));
  }

  /// addObjectFile - Add an ObjectFile to the execution engine.
  ///
  /// This method is only supported by MCJIT.  MCJIT will immediately load the
  /// object into memory and adds its symbols to the list used to resolve
  /// external symbols while preparing other objects for execution.
  ///
  /// Objects added using this function will not be made executable until
  /// needed by another object.
  ///
  /// MCJIT will take ownership of the ObjectFile.
  virtual void addObjectFile(std::unique_ptr<object::ObjectFile> O);
  virtual void addObjectFile(object::OwningBinary<object::ObjectFile> O);

  /// addArchive - Add an Archive to the execution engine.
  ///
  /// This method is only supported by MCJIT.  MCJIT will use the archive to
  /// resolve external symbols in objects it is loading.  If a symbol is found
  /// in the Archive the contained object file will be extracted (in memory)
  /// and loaded for possible execution.
  virtual void addArchive(object::OwningBinary<object::Archive> A);

  //===--------------------------------------------------------------------===//

  const DataLayout &getDataLayout() const { return DL; }

  /// removeModule - Removes a Module from the list of modules, but does not
  /// free the module's memory. Returns true if M is found, in which case the
  /// caller assumes responsibility for deleting the module.
  //
  // FIXME: This stealth ownership transfer is horrible. This will probably be
  //        fixed by deleting ExecutionEngine.
  virtual bool removeModule(Module *M);

  /// FindFunctionNamed - Search all of the active modules to find the function that
  /// defines FnName.  This is very slow operation and shouldn't be used for
  /// general code.
  virtual Function *FindFunctionNamed(StringRef FnName);

  /// FindGlobalVariableNamed - Search all of the active modules to find the global variable
  /// that defines Name.  This is very slow operation and shouldn't be used for
  /// general code.
  virtual GlobalVariable *FindGlobalVariableNamed(StringRef Name, bool AllowInternal = false);

  /// runFunction - Execute the specified function with the specified arguments,
  /// and return the result.
  ///
  /// For MCJIT execution engines, clients are encouraged to use the
  /// "GetFunctionAddress" method (rather than runFunction) and cast the
  /// returned uint64_t to the desired function pointer type. However, for
  /// backwards compatibility MCJIT's implementation can execute 'main-like'
  /// function (i.e. those returning void or int, and taking either no
  /// arguments or (int, char*[])).
  virtual GenericValue runFunction(Function *F,
                                   ArrayRef<GenericValue> ArgValues) = 0;

  /// getPointerToNamedFunction - This method returns the address of the
  /// specified function by using the dlsym function call.  As such it is only
  /// useful for resolving library symbols, not code generated symbols.
  ///
  /// If AbortOnFailure is false and no function with the given name is
  /// found, this function silently returns a null pointer. Otherwise,
  /// it prints a message to stderr and aborts.
  ///
  /// This function is deprecated for the MCJIT execution engine.
  virtual void *getPointerToNamedFunction(StringRef Name,
                                          bool AbortOnFailure = true) = 0;

  /// mapSectionAddress - map a section to its target address space value.
  /// Map the address of a JIT section as returned from the memory manager
  /// to the address in the target process as the running code will see it.
  /// This is the address which will be used for relocation resolution.
  virtual void mapSectionAddress(const void *LocalAddress,
                                 uint64_t TargetAddress) {
    llvm_unreachable("Re-mapping of section addresses not supported with this "
                     "EE!");
  }

  /// generateCodeForModule - Run code generation for the specified module and
  /// load it into memory.
  ///
  /// When this function has completed, all code and data for the specified
  /// module, and any module on which this module depends, will be generated
  /// and loaded into memory, but relocations will not yet have been applied
  /// and all memory will be readable and writable but not executable.
  ///
  /// This function is primarily useful when generating code for an external
  /// target, allowing the client an opportunity to remap section addresses
  /// before relocations are applied.  Clients that intend to execute code
  /// locally can use the getFunctionAddress call, which will generate code
  /// and apply final preparations all in one step.
  ///
  /// This method has no effect for the interpreter.
  virtual void generateCodeForModule(Module *M) {}

  /// finalizeObject - ensure the module is fully processed and is usable.
  ///
  /// It is the user-level function for completing the process of making the
  /// object usable for execution.  It should be called after sections within an
  /// object have been relocated using mapSectionAddress.  When this method is
  /// called the MCJIT execution engine will reapply relocations for a loaded
  /// object.  This method has no effect for the interpreter.
  ///
  /// Returns true on success, false on failure. Error messages can be retrieved
  /// by calling getError();
  virtual void finalizeObject() {}

  /// Returns true if an error has been recorded.
  bool hasError() const { return !ErrMsg.empty(); }

  /// Clear the error message.
  void clearErrorMessage() { ErrMsg.clear(); }

  /// Returns the most recent error message.
  const std::string &getErrorMessage() const { return ErrMsg; }

  /// runStaticConstructorsDestructors - This method is used to execute all of
  /// the static constructors or destructors for a program.
  ///
  /// \param isDtors - Run the destructors instead of constructors.
  virtual void runStaticConstructorsDestructors(bool isDtors);

  /// This method is used to execute all of the static constructors or
  /// destructors for a particular module.
  ///
  /// \param isDtors - Run the destructors instead of constructors.
  void runStaticConstructorsDestructors(Module &module, bool isDtors);


  /// runFunctionAsMain - This is a helper function which wraps runFunction to
  /// handle the common task of starting up main with the specified argc, argv,
  /// and envp parameters.
  int runFunctionAsMain(Function *Fn, const std::vector<std::string> &argv,
                        const char * const * envp);


  /// addGlobalMapping - Tell the execution engine that the specified global is
  /// at the specified location.  This is used internally as functions are JIT'd
  /// and as global variables are laid out in memory.  It can and should also be
  /// used by clients of the EE that want to have an LLVM global overlay
  /// existing data in memory. Values to be mapped should be named, and have
  /// external or weak linkage. Mappings are automatically removed when their
  /// GlobalValue is destroyed.
  void addGlobalMapping(const GlobalValue *GV, void *Addr);
  void addGlobalMapping(StringRef Name, uint64_t Addr);

  /// clearAllGlobalMappings - Clear all global mappings and start over again,
  /// for use in dynamic compilation scenarios to move globals.
  void clearAllGlobalMappings();

  /// clearGlobalMappingsFromModule - Clear all global mappings that came from a
  /// particular module, because it has been removed from the JIT.
  void clearGlobalMappingsFromModule(Module *M);

  /// updateGlobalMapping - Replace an existing mapping for GV with a new
  /// address.  This updates both maps as required.  If "Addr" is null, the
  /// entry for the global is removed from the mappings.  This returns the old
  /// value of the pointer, or null if it was not in the map.
  uint64_t updateGlobalMapping(const GlobalValue *GV, void *Addr);
  uint64_t updateGlobalMapping(StringRef Name, uint64_t Addr);

  /// getAddressToGlobalIfAvailable - This returns the address of the specified
  /// global symbol.
  uint64_t getAddressToGlobalIfAvailable(StringRef S);

  /// getPointerToGlobalIfAvailable - This returns the address of the specified
  /// global value if it is has already been codegen'd, otherwise it returns
  /// null.
  void *getPointerToGlobalIfAvailable(StringRef S);
  void *getPointerToGlobalIfAvailable(const GlobalValue *GV);

  /// getPointerToGlobal - This returns the address of the specified global
  /// value. This may involve code generation if it's a function.
  ///
  /// This function is deprecated for the MCJIT execution engine.  Use
  /// getGlobalValueAddress instead.
  void *getPointerToGlobal(const GlobalValue *GV);

  /// getPointerToFunction - The different EE's represent function bodies in
  /// different ways.  They should each implement this to say what a function
  /// pointer should look like.  When F is destroyed, the ExecutionEngine will
  /// remove its global mapping and free any machine code.  Be sure no threads
  /// are running inside F when that happens.
  ///
  /// This function is deprecated for the MCJIT execution engine.  Use
  /// getFunctionAddress instead.
  virtual void *getPointerToFunction(Function *F) = 0;

  /// getPointerToFunctionOrStub - If the specified function has been
  /// code-gen'd, return a pointer to the function.  If not, compile it, or use
  /// a stub to implement lazy compilation if available.  See
  /// getPointerToFunction for the requirements on destroying F.
  ///
  /// This function is deprecated for the MCJIT execution engine.  Use
  /// getFunctionAddress instead.
  virtual void *getPointerToFunctionOrStub(Function *F) {
    // Default implementation, just codegen the function.
    return getPointerToFunction(F);
  }

  /// getGlobalValueAddress - Return the address of the specified global
  /// value. This may involve code generation.
  ///
  /// This function should not be called with the interpreter engine.
  virtual uint64_t getGlobalValueAddress(const std::string &Name) {
    // Default implementation for the interpreter.  MCJIT will override this.
    // JIT and interpreter clients should use getPointerToGlobal instead.
    return 0;
  }

  /// getFunctionAddress - Return the address of the specified function.
  /// This may involve code generation.
  virtual uint64_t getFunctionAddress(const std::string &Name) {
    // Default implementation for the interpreter.  MCJIT will override this.
    // Interpreter clients should use getPointerToFunction instead.
    return 0;
  }

  /// getGlobalValueAtAddress - Return the LLVM global value object that starts
  /// at the specified address.
  ///
  const GlobalValue *getGlobalValueAtAddress(void *Addr);

  /// StoreValueToMemory - Stores the data in Val of type Ty at address Ptr.
  /// Ptr is the address of the memory at which to store Val, cast to
  /// GenericValue *.  It is not a pointer to a GenericValue containing the
  /// address at which to store Val.
  void StoreValueToMemory(const GenericValue &Val, GenericValue *Ptr,
                          Type *Ty);

  void InitializeMemory(const Constant *Init, void *Addr);

  /// getOrEmitGlobalVariable - Return the address of the specified global
  /// variable, possibly emitting it to memory if needed.  This is used by the
  /// Emitter.
  ///
  /// This function is deprecated for the MCJIT execution engine.  Use
  /// getGlobalValueAddress instead.
  virtual void *getOrEmitGlobalVariable(const GlobalVariable *GV) {
    return getPointerToGlobal((const GlobalValue *)GV);
  }

  /// Registers a listener to be called back on various events within
  /// the JIT.  See JITEventListener.h for more details.  Does not
  /// take ownership of the argument.  The argument may be NULL, in
  /// which case these functions do nothing.
  virtual void RegisterJITEventListener(JITEventListener *) {}
  virtual void UnregisterJITEventListener(JITEventListener *) {}

  /// Sets the pre-compiled object cache.  The ownership of the ObjectCache is
  /// not changed.  Supported by MCJIT but not the interpreter.
  virtual void setObjectCache(ObjectCache *) {
    llvm_unreachable("No support for an object cache");
  }

  /// setProcessAllSections (MCJIT Only): By default, only sections that are
  /// "required for execution" are passed to the RTDyldMemoryManager, and other
  /// sections are discarded. Passing 'true' to this method will cause
  /// RuntimeDyld to pass all sections to its RTDyldMemoryManager regardless
  /// of whether they are "required to execute" in the usual sense.
  ///
  /// Rationale: Some MCJIT clients want to be able to inspect metadata
  /// sections (e.g. Dwarf, Stack-maps) to enable functionality or analyze
  /// performance. Passing these sections to the memory manager allows the
  /// client to make policy about the relevant sections, rather than having
  /// MCJIT do it.
  virtual void setProcessAllSections(bool ProcessAllSections) {
    llvm_unreachable("No support for ProcessAllSections option");
  }

  /// Return the target machine (if available).
  virtual TargetMachine *getTargetMachine() { return nullptr; }

  /// DisableLazyCompilation - When lazy compilation is off (the default), the
  /// JIT will eagerly compile every function reachable from the argument to
  /// getPointerToFunction.  If lazy compilation is turned on, the JIT will only
  /// compile the one function and emit stubs to compile the rest when they're
  /// first called.  If lazy compilation is turned off again while some lazy
  /// stubs are still around, and one of those stubs is called, the program will
  /// abort.
  ///
  /// In order to safely compile lazily in a threaded program, the user must
  /// ensure that 1) only one thread at a time can call any particular lazy
  /// stub, and 2) any thread modifying LLVM IR must hold the JIT's lock
  /// (ExecutionEngine::lock) or otherwise ensure that no other thread calls a
  /// lazy stub.  See http://llvm.org/PR5184 for details.
  void DisableLazyCompilation(bool Disabled = true) {
    CompilingLazily = !Disabled;
  }
  bool isCompilingLazily() const {
    return CompilingLazily;
  }

  /// DisableGVCompilation - If called, the JIT will abort if it's asked to
  /// allocate space and populate a GlobalVariable that is not internal to
  /// the module.
  void DisableGVCompilation(bool Disabled = true) {
    GVCompilationDisabled = Disabled;
  }
  bool isGVCompilationDisabled() const {
    return GVCompilationDisabled;
  }

  /// DisableSymbolSearching - If called, the JIT will not try to lookup unknown
  /// symbols with dlsym.  A client can still use InstallLazyFunctionCreator to
  /// resolve symbols in a custom way.
  void DisableSymbolSearching(bool Disabled = true) {
    SymbolSearchingDisabled = Disabled;
  }
  bool isSymbolSearchingDisabled() const {
    return SymbolSearchingDisabled;
  }

  /// Enable/Disable IR module verification.
  ///
  /// Note: Module verification is enabled by default in Debug builds, and
  /// disabled by default in Release. Use this method to override the default.
  void setVerifyModules(bool Verify) {
    VerifyModules = Verify;
  }
  bool getVerifyModules() const {
    return VerifyModules;
  }

  /// InstallLazyFunctionCreator - If an unknown function is needed, the
  /// specified function pointer is invoked to create it.  If it returns null,
  /// the JIT will abort.
  void InstallLazyFunctionCreator(FunctionCreator C) {
    LazyFunctionCreator = std::move(C);
  }

protected:
  ExecutionEngine(DataLayout DL) : DL(std::move(DL)) {}
  explicit ExecutionEngine(DataLayout DL, std::unique_ptr<Module> M);
  explicit ExecutionEngine(std::unique_ptr<Module> M);

  void emitGlobals();

  void emitGlobalVariable(const GlobalVariable *GV);

  GenericValue getConstantValue(const Constant *C);
  void LoadValueFromMemory(GenericValue &Result, GenericValue *Ptr,
                           Type *Ty);

private:
  void Init(std::unique_ptr<Module> M);
};

namespace EngineKind {

  // These are actually bitmasks that get or-ed together.
  enum Kind {
    JIT         = 0x1,
    Interpreter = 0x2
  };
  const static Kind Either = (Kind)(JIT | Interpreter);

} // end namespace EngineKind

/// Builder class for ExecutionEngines. Use this by stack-allocating a builder,
/// chaining the various set* methods, and terminating it with a .create()
/// call.
class EngineBuilder {
private:
  std::unique_ptr<Module> M;
  EngineKind::Kind WhichEngine;
  std::string *ErrorStr;
  CodeGenOptLevel OptLevel;
  std::shared_ptr<MCJITMemoryManager> MemMgr;
  std::shared_ptr<LegacyJITSymbolResolver> Resolver;
  TargetOptions Options;
  std::optional<Reloc::Model> RelocModel;
  std::optional<CodeModel::Model> CMModel;
  std::string MArch;
  std::string MCPU;
  SmallVector<std::string, 4> MAttrs;
  bool VerifyModules;
  bool EmulatedTLS = true;

public:
  /// Default constructor for EngineBuilder.
  EngineBuilder();

  /// Constructor for EngineBuilder.
  EngineBuilder(std::unique_ptr<Module> M);

  // Out-of-line since we don't have the def'n of RTDyldMemoryManager here.
  ~EngineBuilder();

  /// setEngineKind - Controls whether the user wants the interpreter, the JIT,
  /// or whichever engine works.  This option defaults to EngineKind::Either.
  EngineBuilder &setEngineKind(EngineKind::Kind w) {
    WhichEngine = w;
    return *this;
  }

  /// setMCJITMemoryManager - Sets the MCJIT memory manager to use. This allows
  /// clients to customize their memory allocation policies for the MCJIT. This
  /// is only appropriate for the MCJIT; setting this and configuring the builder
  /// to create anything other than MCJIT will cause a runtime error. If create()
  /// is called and is successful, the created engine takes ownership of the
  /// memory manager. This option defaults to NULL.
  EngineBuilder &setMCJITMemoryManager(std::unique_ptr<RTDyldMemoryManager> mcjmm);

  EngineBuilder&
  setMemoryManager(std::unique_ptr<MCJITMemoryManager> MM);

  EngineBuilder &setSymbolResolver(std::unique_ptr<LegacyJITSymbolResolver> SR);

  /// setErrorStr - Set the error string to write to on error.  This option
  /// defaults to NULL.
  EngineBuilder &setErrorStr(std::string *e) {
    ErrorStr = e;
    return *this;
  }

  /// setOptLevel - Set the optimization level for the JIT.  This option
  /// defaults to CodeGenOptLevel::Default.
  EngineBuilder &setOptLevel(CodeGenOptLevel l) {
    OptLevel = l;
    return *this;
  }

  /// setTargetOptions - Set the target options that the ExecutionEngine
  /// target is using. Defaults to TargetOptions().
  EngineBuilder &setTargetOptions(const TargetOptions &Opts) {
    Options = Opts;
    return *this;
  }

  /// setRelocationModel - Set the relocation model that the ExecutionEngine
  /// target is using. Defaults to target specific default "Reloc::Default".
  EngineBuilder &setRelocationModel(Reloc::Model RM) {
    RelocModel = RM;
    return *this;
  }

  /// setCodeModel - Set the CodeModel that the ExecutionEngine target
  /// data is using. Defaults to target specific default
  /// "CodeModel::JITDefault".
  EngineBuilder &setCodeModel(CodeModel::Model M) {
    CMModel = M;
    return *this;
  }

  /// setMArch - Override the architecture set by the Module's triple.
  EngineBuilder &setMArch(StringRef march) {
    MArch.assign(march.begin(), march.end());
    return *this;
  }

  /// setMCPU - Target a specific cpu type.
  EngineBuilder &setMCPU(StringRef mcpu) {
    MCPU.assign(mcpu.begin(), mcpu.end());
    return *this;
  }

  /// setVerifyModules - Set whether the JIT implementation should verify
  /// IR modules during compilation.
  EngineBuilder &setVerifyModules(bool Verify) {
    VerifyModules = Verify;
    return *this;
  }

  /// setMAttrs - Set cpu-specific attributes.
  template<typename StringSequence>
  EngineBuilder &setMAttrs(const StringSequence &mattrs) {
    MAttrs.clear();
    MAttrs.append(mattrs.begin(), mattrs.end());
    return *this;
  }

  void setEmulatedTLS(bool EmulatedTLS) {
    this->EmulatedTLS = EmulatedTLS;
  }

  TargetMachine *selectTarget();

  /// selectTarget - Pick a target either via -march or by guessing the native
  /// arch.  Add any CPU features specified via -mcpu or -mattr.
  TargetMachine *selectTarget(const Triple &TargetTriple,
                              StringRef MArch,
                              StringRef MCPU,
                              const SmallVectorImpl<std::string>& MAttrs);

  ExecutionEngine *create() {
    return create(selectTarget());
  }

  ExecutionEngine *create(TargetMachine *TM);
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(ExecutionEngine, LLVMExecutionEngineRef)

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_EXECUTIONENGINE_H
