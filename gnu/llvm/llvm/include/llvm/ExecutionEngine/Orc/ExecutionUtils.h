//===- ExecutionUtils.h - Utilities for executing code in Orc ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains utilities for executing code in Orc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EXECUTIONUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_EXECUTIONUTILS_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Mangling.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcError.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/Object/Archive.h"
#include "llvm/Support/DynamicLibrary.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace llvm {

class ConstantArray;
class GlobalVariable;
class Function;
class Module;
class Value;

namespace object {
class MachOUniversalBinary;
}

namespace orc {

class ObjectLayer;

/// This iterator provides a convenient way to iterate over the elements
///        of an llvm.global_ctors/llvm.global_dtors instance.
///
///   The easiest way to get hold of instances of this class is to use the
/// getConstructors/getDestructors functions.
class CtorDtorIterator {
public:
  /// Accessor for an element of the global_ctors/global_dtors array.
  ///
  ///   This class provides a read-only view of the element with any casts on
  /// the function stripped away.
  struct Element {
    Element(unsigned Priority, Function *Func, Value *Data)
      : Priority(Priority), Func(Func), Data(Data) {}

    unsigned Priority;
    Function *Func;
    Value *Data;
  };

  /// Construct an iterator instance. If End is true then this iterator
  ///        acts as the end of the range, otherwise it is the beginning.
  CtorDtorIterator(const GlobalVariable *GV, bool End);

  /// Test iterators for equality.
  bool operator==(const CtorDtorIterator &Other) const;

  /// Test iterators for inequality.
  bool operator!=(const CtorDtorIterator &Other) const;

  /// Pre-increment iterator.
  CtorDtorIterator& operator++();

  /// Post-increment iterator.
  CtorDtorIterator operator++(int);

  /// Dereference iterator. The resulting value provides a read-only view
  ///        of this element of the global_ctors/global_dtors list.
  Element operator*() const;

private:
  const ConstantArray *InitList;
  unsigned I;
};

/// Create an iterator range over the entries of the llvm.global_ctors
///        array.
iterator_range<CtorDtorIterator> getConstructors(const Module &M);

/// Create an iterator range over the entries of the llvm.global_ctors
///        array.
iterator_range<CtorDtorIterator> getDestructors(const Module &M);

/// This iterator provides a convenient way to iterate over GlobalValues that
/// have initialization effects.
class StaticInitGVIterator {
public:
  StaticInitGVIterator() = default;

  StaticInitGVIterator(Module &M)
      : I(M.global_values().begin()), E(M.global_values().end()),
        ObjFmt(Triple(M.getTargetTriple()).getObjectFormat()) {
    if (I != E) {
      if (!isStaticInitGlobal(*I))
        moveToNextStaticInitGlobal();
    } else
      I = E = Module::global_value_iterator();
  }

  bool operator==(const StaticInitGVIterator &O) const { return I == O.I; }
  bool operator!=(const StaticInitGVIterator &O) const { return I != O.I; }

  StaticInitGVIterator &operator++() {
    assert(I != E && "Increment past end of range");
    moveToNextStaticInitGlobal();
    return *this;
  }

  GlobalValue &operator*() { return *I; }

private:
  bool isStaticInitGlobal(GlobalValue &GV);
  void moveToNextStaticInitGlobal() {
    ++I;
    while (I != E && !isStaticInitGlobal(*I))
      ++I;
    if (I == E)
      I = E = Module::global_value_iterator();
  }

  Module::global_value_iterator I, E;
  Triple::ObjectFormatType ObjFmt;
};

/// Create an iterator range over the GlobalValues that contribute to static
/// initialization.
inline iterator_range<StaticInitGVIterator> getStaticInitGVs(Module &M) {
  return make_range(StaticInitGVIterator(M), StaticInitGVIterator());
}

class CtorDtorRunner {
public:
  CtorDtorRunner(JITDylib &JD) : JD(JD) {}
  void add(iterator_range<CtorDtorIterator> CtorDtors);
  Error run();

private:
  using CtorDtorList = std::vector<SymbolStringPtr>;
  using CtorDtorPriorityMap = std::map<unsigned, CtorDtorList>;

  JITDylib &JD;
  CtorDtorPriorityMap CtorDtorsByPriority;
};

/// Support class for static dtor execution. For hosted (in-process) JITs
///        only!
///
///   If a __cxa_atexit function isn't found C++ programs that use static
/// destructors will fail to link. However, we don't want to use the host
/// process's __cxa_atexit, because it will schedule JIT'd destructors to run
/// after the JIT has been torn down, which is no good. This class makes it easy
/// to override __cxa_atexit (and the related __dso_handle).
///
///   To use, clients should manually call searchOverrides from their symbol
/// resolver. This should generally be done after attempting symbol resolution
/// inside the JIT, but before searching the host process's symbol table. When
/// the client determines that destructors should be run (generally at JIT
/// teardown or after a return from main), the runDestructors method should be
/// called.
class LocalCXXRuntimeOverridesBase {
public:
  /// Run any destructors recorded by the overriden __cxa_atexit function
  /// (CXAAtExitOverride).
  void runDestructors();

protected:
  using DestructorPtr = void (*)(void *);
  using CXXDestructorDataPair = std::pair<DestructorPtr, void *>;
  using CXXDestructorDataPairList = std::vector<CXXDestructorDataPair>;
  CXXDestructorDataPairList DSOHandleOverride;
  static int CXAAtExitOverride(DestructorPtr Destructor, void *Arg,
                               void *DSOHandle);
};

class LocalCXXRuntimeOverrides : public LocalCXXRuntimeOverridesBase {
public:
  Error enable(JITDylib &JD, MangleAndInterner &Mangler);
};

/// An interface for Itanium __cxa_atexit interposer implementations.
class ItaniumCXAAtExitSupport {
public:
  struct AtExitRecord {
    void (*F)(void *);
    void *Ctx;
  };

  void registerAtExit(void (*F)(void *), void *Ctx, void *DSOHandle);
  void runAtExits(void *DSOHandle);

private:
  std::mutex AtExitsMutex;
  DenseMap<void *, std::vector<AtExitRecord>> AtExitRecords;
};

/// A utility class to expose symbols found via dlsym to the JIT.
///
/// If an instance of this class is attached to a JITDylib as a fallback
/// definition generator, then any symbol found in the given DynamicLibrary that
/// passes the 'Allow' predicate will be added to the JITDylib.
class DynamicLibrarySearchGenerator : public DefinitionGenerator {
public:
  using SymbolPredicate = std::function<bool(const SymbolStringPtr &)>;
  using AddAbsoluteSymbolsFn = unique_function<Error(JITDylib &, SymbolMap)>;

  /// Create a DynamicLibrarySearchGenerator that searches for symbols in the
  /// given sys::DynamicLibrary.
  ///
  /// If the Allow predicate is given then only symbols matching the predicate
  /// will be searched for. If the predicate is not given then all symbols will
  /// be searched for.
  ///
  /// If \p AddAbsoluteSymbols is provided, it is used to add the symbols to the
  /// \c JITDylib; otherwise it uses JD.define(absoluteSymbols(...)).
  DynamicLibrarySearchGenerator(
      sys::DynamicLibrary Dylib, char GlobalPrefix,
      SymbolPredicate Allow = SymbolPredicate(),
      AddAbsoluteSymbolsFn AddAbsoluteSymbols = nullptr);

  /// Permanently loads the library at the given path and, on success, returns
  /// a DynamicLibrarySearchGenerator that will search it for symbol definitions
  /// in the library. On failure returns the reason the library failed to load.
  static Expected<std::unique_ptr<DynamicLibrarySearchGenerator>>
  Load(const char *FileName, char GlobalPrefix,
       SymbolPredicate Allow = SymbolPredicate(),
       AddAbsoluteSymbolsFn AddAbsoluteSymbols = nullptr);

  /// Creates a DynamicLibrarySearchGenerator that searches for symbols in
  /// the current process.
  static Expected<std::unique_ptr<DynamicLibrarySearchGenerator>>
  GetForCurrentProcess(char GlobalPrefix,
                       SymbolPredicate Allow = SymbolPredicate(),
                       AddAbsoluteSymbolsFn AddAbsoluteSymbols = nullptr) {
    return Load(nullptr, GlobalPrefix, std::move(Allow),
                std::move(AddAbsoluteSymbols));
  }

  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &Symbols) override;

private:
  sys::DynamicLibrary Dylib;
  SymbolPredicate Allow;
  AddAbsoluteSymbolsFn AddAbsoluteSymbols;
  char GlobalPrefix;
};

/// A utility class to expose symbols from a static library.
///
/// If an instance of this class is attached to a JITDylib as a fallback
/// definition generator, then any symbol found in the archive will result in
/// the containing object being added to the JITDylib.
class StaticLibraryDefinitionGenerator : public DefinitionGenerator {
public:
  // Interface builder function for objects loaded from this archive.
  using GetObjectFileInterface =
      unique_function<Expected<MaterializationUnit::Interface>(
          ExecutionSession &ES, MemoryBufferRef ObjBuffer)>;

  /// Try to create a StaticLibraryDefinitionGenerator from the given path.
  ///
  /// This call will succeed if the file at the given path is a static library
  /// or a MachO universal binary containing a static library that is compatible
  /// with the ExecutionSession's triple. Otherwise it will return an error.
  static Expected<std::unique_ptr<StaticLibraryDefinitionGenerator>>
  Load(ObjectLayer &L, const char *FileName,
       GetObjectFileInterface GetObjFileInterface = GetObjectFileInterface());

  /// Try to create a StaticLibrarySearchGenerator from the given memory buffer
  /// and Archive object.
  static Expected<std::unique_ptr<StaticLibraryDefinitionGenerator>>
  Create(ObjectLayer &L, std::unique_ptr<MemoryBuffer> ArchiveBuffer,
         std::unique_ptr<object::Archive> Archive,
         GetObjectFileInterface GetObjFileInterface = GetObjectFileInterface());

  /// Try to create a StaticLibrarySearchGenerator from the given memory buffer.
  /// This call will succeed if the buffer contains a valid archive, otherwise
  /// it will return an error.
  ///
  /// This call will succeed if the buffer contains a valid static library or a
  /// MachO universal binary containing a static library that is compatible
  /// with the ExecutionSession's triple. Otherwise it will return an error.
  static Expected<std::unique_ptr<StaticLibraryDefinitionGenerator>>
  Create(ObjectLayer &L, std::unique_ptr<MemoryBuffer> ArchiveBuffer,
         GetObjectFileInterface GetObjFileInterface = GetObjectFileInterface());

  /// Returns a list of filenames of dynamic libraries that this archive has
  /// imported. This class does not load these libraries by itself. User is
  /// responsible for making sure these libraries are avaliable to the JITDylib.
  const std::set<std::string> &getImportedDynamicLibraries() const {
    return ImportedDynamicLibraries;
  }

  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &Symbols) override;

private:
  StaticLibraryDefinitionGenerator(ObjectLayer &L,
                                   std::unique_ptr<MemoryBuffer> ArchiveBuffer,
                                   std::unique_ptr<object::Archive> Archive,
                                   GetObjectFileInterface GetObjFileInterface,
                                   Error &Err);
  Error buildObjectFilesMap();

  static Expected<std::pair<size_t, size_t>>
  getSliceRangeForArch(object::MachOUniversalBinary &UB, const Triple &TT);

  ObjectLayer &L;
  GetObjectFileInterface GetObjFileInterface;
  std::set<std::string> ImportedDynamicLibraries;
  std::unique_ptr<MemoryBuffer> ArchiveBuffer;
  std::unique_ptr<object::Archive> Archive;
  DenseMap<SymbolStringPtr, MemoryBufferRef> ObjectFilesMap;
  BumpPtrAllocator ObjFileNameStorage;
};

/// A utility class to create COFF dllimport GOT symbols (__imp_*) and PLT
/// stubs.
///
/// If an instance of this class is attached to a JITDylib as a fallback
/// definition generator, PLT stubs and dllimport __imp_ symbols will be
/// generated for external symbols found outside the given jitdylib. Currently
/// only supports x86_64 architecture.
class DLLImportDefinitionGenerator : public DefinitionGenerator {
public:
  /// Creates a DLLImportDefinitionGenerator instance.
  static std::unique_ptr<DLLImportDefinitionGenerator>
  Create(ExecutionSession &ES, ObjectLinkingLayer &L);

  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &Symbols) override;

private:
  DLLImportDefinitionGenerator(ExecutionSession &ES, ObjectLinkingLayer &L)
      : ES(ES), L(L) {}

  static Expected<unsigned> getTargetPointerSize(const Triple &TT);
  static Expected<llvm::endianness> getEndianness(const Triple &TT);
  Expected<std::unique_ptr<jitlink::LinkGraph>>
  createStubsGraph(const SymbolMap &Resolved);

  static StringRef getImpPrefix() { return "__imp_"; }

  static StringRef getSectionName() { return "$__DLLIMPORT_STUBS"; }

  ExecutionSession &ES;
  ObjectLinkingLayer &L;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_EXECUTIONUTILS_H
