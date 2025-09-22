//===-- MCJIT.h - Class definition for the MCJIT ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_MCJIT_MCJIT_H
#define LLVM_LIB_EXECUTIONENGINE_MCJIT_MCJIT_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"

namespace llvm {
class MCJIT;
class Module;
class ObjectCache;

// This is a helper class that the MCJIT execution engine uses for linking
// functions across modules that it owns.  It aggregates the memory manager
// that is passed in to the MCJIT constructor and defers most functionality
// to that object.
class LinkingSymbolResolver : public LegacyJITSymbolResolver {
public:
  LinkingSymbolResolver(MCJIT &Parent,
                        std::shared_ptr<LegacyJITSymbolResolver> Resolver)
      : ParentEngine(Parent), ClientResolver(std::move(Resolver)) {}

  JITSymbol findSymbol(const std::string &Name) override;

  // MCJIT doesn't support logical dylibs.
  JITSymbol findSymbolInLogicalDylib(const std::string &Name) override {
    return nullptr;
  }

private:
  MCJIT &ParentEngine;
  std::shared_ptr<LegacyJITSymbolResolver> ClientResolver;
  void anchor() override;
};

// About Module states: added->loaded->finalized.
//
// The purpose of the "added" state is having modules in standby. (added=known
// but not compiled). The idea is that you can add a module to provide function
// definitions but if nothing in that module is referenced by a module in which
// a function is executed (note the wording here because it's not exactly the
// ideal case) then the module never gets compiled. This is sort of lazy
// compilation.
//
// The purpose of the "loaded" state (loaded=compiled and required sections
// copied into local memory but not yet ready for execution) is to have an
// intermediate state wherein clients can remap the addresses of sections, using
// MCJIT::mapSectionAddress, (in preparation for later copying to a new location
// or an external process) before relocations and page permissions are applied.
//
// It might not be obvious at first glance, but the "remote-mcjit" case in the
// lli tool does this.  In that case, the intermediate action is taken by the
// RemoteMemoryManager in response to the notifyObjectLoaded function being
// called.

class MCJIT : public ExecutionEngine {
  MCJIT(std::unique_ptr<Module> M, std::unique_ptr<TargetMachine> tm,
        std::shared_ptr<MCJITMemoryManager> MemMgr,
        std::shared_ptr<LegacyJITSymbolResolver> Resolver);

  typedef llvm::SmallPtrSet<Module *, 4> ModulePtrSet;

  class OwningModuleContainer {
  public:
    OwningModuleContainer() = default;
    ~OwningModuleContainer() {
      freeModulePtrSet(AddedModules);
      freeModulePtrSet(LoadedModules);
      freeModulePtrSet(FinalizedModules);
    }

    ModulePtrSet::iterator begin_added() { return AddedModules.begin(); }
    ModulePtrSet::iterator end_added() { return AddedModules.end(); }
    iterator_range<ModulePtrSet::iterator> added() {
      return make_range(begin_added(), end_added());
    }

    ModulePtrSet::iterator begin_loaded() { return LoadedModules.begin(); }
    ModulePtrSet::iterator end_loaded() { return LoadedModules.end(); }

    ModulePtrSet::iterator begin_finalized() { return FinalizedModules.begin(); }
    ModulePtrSet::iterator end_finalized() { return FinalizedModules.end(); }

    void addModule(std::unique_ptr<Module> M) {
      AddedModules.insert(M.release());
    }

    bool removeModule(Module *M) {
      return AddedModules.erase(M) || LoadedModules.erase(M) ||
             FinalizedModules.erase(M);
    }

    bool hasModuleBeenAddedButNotLoaded(Module *M) {
      return AddedModules.contains(M);
    }

    bool hasModuleBeenLoaded(Module *M) {
      // If the module is in either the "loaded" or "finalized" sections it
      // has been loaded.
      return LoadedModules.contains(M) || FinalizedModules.contains(M);
    }

    bool hasModuleBeenFinalized(Module *M) {
      return FinalizedModules.contains(M);
    }

    bool ownsModule(Module* M) {
      return AddedModules.contains(M) || LoadedModules.contains(M) ||
             FinalizedModules.contains(M);
    }

    void markModuleAsLoaded(Module *M) {
      // This checks against logic errors in the MCJIT implementation.
      // This function should never be called with either a Module that MCJIT
      // does not own or a Module that has already been loaded and/or finalized.
      assert(AddedModules.count(M) &&
             "markModuleAsLoaded: Module not found in AddedModules");

      // Remove the module from the "Added" set.
      AddedModules.erase(M);

      // Add the Module to the "Loaded" set.
      LoadedModules.insert(M);
    }

    void markModuleAsFinalized(Module *M) {
      // This checks against logic errors in the MCJIT implementation.
      // This function should never be called with either a Module that MCJIT
      // does not own, a Module that has not been loaded or a Module that has
      // already been finalized.
      assert(LoadedModules.count(M) &&
             "markModuleAsFinalized: Module not found in LoadedModules");

      // Remove the module from the "Loaded" section of the list.
      LoadedModules.erase(M);

      // Add the Module to the "Finalized" section of the list by inserting it
      // before the 'end' iterator.
      FinalizedModules.insert(M);
    }

    void markAllLoadedModulesAsFinalized() {
      for (Module *M : LoadedModules)
        FinalizedModules.insert(M);
      LoadedModules.clear();
    }

  private:
    ModulePtrSet AddedModules;
    ModulePtrSet LoadedModules;
    ModulePtrSet FinalizedModules;

    void freeModulePtrSet(ModulePtrSet& MPS) {
      // Go through the module set and delete everything.
      for (Module *M : MPS)
        delete M;
      MPS.clear();
    }
  };

  std::unique_ptr<TargetMachine> TM;
  MCContext *Ctx;
  std::shared_ptr<MCJITMemoryManager> MemMgr;
  LinkingSymbolResolver Resolver;
  RuntimeDyld Dyld;
  std::vector<JITEventListener*> EventListeners;

  OwningModuleContainer OwnedModules;

  SmallVector<object::OwningBinary<object::Archive>, 2> Archives;
  SmallVector<std::unique_ptr<MemoryBuffer>, 2> Buffers;

  SmallVector<std::unique_ptr<object::ObjectFile>, 2> LoadedObjects;

  // An optional ObjectCache to be notified of compiled objects and used to
  // perform lookup of pre-compiled code to avoid re-compilation.
  ObjectCache *ObjCache;

  Function *FindFunctionNamedInModulePtrSet(StringRef FnName,
                                            ModulePtrSet::iterator I,
                                            ModulePtrSet::iterator E);

  GlobalVariable *FindGlobalVariableNamedInModulePtrSet(StringRef Name,
                                                        bool AllowInternal,
                                                        ModulePtrSet::iterator I,
                                                        ModulePtrSet::iterator E);

  void runStaticConstructorsDestructorsInModulePtrSet(bool isDtors,
                                                      ModulePtrSet::iterator I,
                                                      ModulePtrSet::iterator E);

public:
  ~MCJIT() override;

  /// @name ExecutionEngine interface implementation
  /// @{
  void addModule(std::unique_ptr<Module> M) override;
  void addObjectFile(std::unique_ptr<object::ObjectFile> O) override;
  void addObjectFile(object::OwningBinary<object::ObjectFile> O) override;
  void addArchive(object::OwningBinary<object::Archive> O) override;
  bool removeModule(Module *M) override;

  /// FindFunctionNamed - Search all of the active modules to find the function that
  /// defines FnName.  This is very slow operation and shouldn't be used for
  /// general code.
  Function *FindFunctionNamed(StringRef FnName) override;

  /// FindGlobalVariableNamed - Search all of the active modules to find the
  /// global variable that defines Name.  This is very slow operation and
  /// shouldn't be used for general code.
  GlobalVariable *FindGlobalVariableNamed(StringRef Name,
                                          bool AllowInternal = false) override;

  /// Sets the object manager that MCJIT should use to avoid compilation.
  void setObjectCache(ObjectCache *manager) override;

  void setProcessAllSections(bool ProcessAllSections) override {
    Dyld.setProcessAllSections(ProcessAllSections);
  }

  void generateCodeForModule(Module *M) override;

  /// finalizeObject - ensure the module is fully processed and is usable.
  ///
  /// It is the user-level function for completing the process of making the
  /// object usable for execution. It should be called after sections within an
  /// object have been relocated using mapSectionAddress.  When this method is
  /// called the MCJIT execution engine will reapply relocations for a loaded
  /// object.
  /// Is it OK to finalize a set of modules, add modules and finalize again.
  // FIXME: Do we really need both of these?
  void finalizeObject() override;
  virtual void finalizeModule(Module *);
  void finalizeLoadedModules();

  /// runStaticConstructorsDestructors - This method is used to execute all of
  /// the static constructors or destructors for a program.
  ///
  /// \param isDtors - Run the destructors instead of constructors.
  void runStaticConstructorsDestructors(bool isDtors) override;

  void *getPointerToFunction(Function *F) override;

  GenericValue runFunction(Function *F,
                           ArrayRef<GenericValue> ArgValues) override;

  /// getPointerToNamedFunction - This method returns the address of the
  /// specified function by using the dlsym function call.  As such it is only
  /// useful for resolving library symbols, not code generated symbols.
  ///
  /// If AbortOnFailure is false and no function with the given name is
  /// found, this function silently returns a null pointer. Otherwise,
  /// it prints a message to stderr and aborts.
  ///
  void *getPointerToNamedFunction(StringRef Name,
                                  bool AbortOnFailure = true) override;

  /// mapSectionAddress - map a section to its target address space value.
  /// Map the address of a JIT section as returned from the memory manager
  /// to the address in the target process as the running code will see it.
  /// This is the address which will be used for relocation resolution.
  void mapSectionAddress(const void *LocalAddress,
                         uint64_t TargetAddress) override {
    Dyld.mapSectionAddress(LocalAddress, TargetAddress);
  }
  void RegisterJITEventListener(JITEventListener *L) override;
  void UnregisterJITEventListener(JITEventListener *L) override;

  // If successful, these function will implicitly finalize all loaded objects.
  // To get a function address within MCJIT without causing a finalize, use
  // getSymbolAddress.
  uint64_t getGlobalValueAddress(const std::string &Name) override;
  uint64_t getFunctionAddress(const std::string &Name) override;

  TargetMachine *getTargetMachine() override { return TM.get(); }

  /// @}
  /// @name (Private) Registration Interfaces
  /// @{

  static void Register() {
    MCJITCtor = createJIT;
  }

  static ExecutionEngine *
  createJIT(std::unique_ptr<Module> M, std::string *ErrorStr,
            std::shared_ptr<MCJITMemoryManager> MemMgr,
            std::shared_ptr<LegacyJITSymbolResolver> Resolver,
            std::unique_ptr<TargetMachine> TM);

  // @}

  // Takes a mangled name and returns the corresponding JITSymbol (if a
  // definition of that mangled name has been added to the JIT).
  JITSymbol findSymbol(const std::string &Name, bool CheckFunctionsOnly);

  // DEPRECATED - Please use findSymbol instead.
  //
  // This is not directly exposed via the ExecutionEngine API, but it is
  // used by the LinkingMemoryManager.
  //
  // getSymbolAddress takes an unmangled name and returns the corresponding
  // JITSymbol if a definition of the name has been added to the JIT.
  uint64_t getSymbolAddress(const std::string &Name,
                            bool CheckFunctionsOnly);

protected:
  /// emitObject -- Generate a JITed object in memory from the specified module
  /// Currently, MCJIT only supports a single module and the module passed to
  /// this function call is expected to be the contained module.  The module
  /// is passed as a parameter here to prepare for multiple module support in
  /// the future.
  std::unique_ptr<MemoryBuffer> emitObject(Module *M);

  void notifyObjectLoaded(const object::ObjectFile &Obj,
                          const RuntimeDyld::LoadedObjectInfo &L);
  void notifyFreeingObject(const object::ObjectFile &Obj);

  JITSymbol findExistingSymbol(const std::string &Name);
  Module *findModuleForSymbol(const std::string &Name, bool CheckFunctionsOnly);
};

} // end llvm namespace

#endif // LLVM_LIB_EXECUTIONENGINE_MCJIT_MCJIT_H
