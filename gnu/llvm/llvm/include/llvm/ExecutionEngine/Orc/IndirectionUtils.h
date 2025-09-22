//===- IndirectionUtils.h - Utilities for adding indirections ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains utilities for adding indirections and breaking up modules.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_INDIRECTIONUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_INDIRECTIONUTILS_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/OrcABISupport.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/Process.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

namespace llvm {

class Constant;
class Function;
class FunctionType;
class GlobalAlias;
class GlobalVariable;
class Module;
class PointerType;
class Triple;
class Twine;
class Value;
class MCDisassembler;
class MCInstrAnalysis;

namespace jitlink {
class LinkGraph;
class Symbol;
} // namespace jitlink

namespace orc {

/// Base class for pools of compiler re-entry trampolines.
/// These trampolines are callable addresses that save all register state
/// before calling a supplied function to return the trampoline landing
/// address, then restore all state before jumping to that address. They
/// are used by various ORC APIs to support lazy compilation
class TrampolinePool {
public:
  using NotifyLandingResolvedFunction =
      unique_function<void(ExecutorAddr) const>;

  using ResolveLandingFunction = unique_function<void(
      ExecutorAddr TrampolineAddr,
      NotifyLandingResolvedFunction OnLandingResolved) const>;

  virtual ~TrampolinePool();

  /// Get an available trampoline address.
  /// Returns an error if no trampoline can be created.
  Expected<ExecutorAddr> getTrampoline() {
    std::lock_guard<std::mutex> Lock(TPMutex);
    if (AvailableTrampolines.empty()) {
      if (auto Err = grow())
        return std::move(Err);
    }
    assert(!AvailableTrampolines.empty() && "Failed to grow trampoline pool");
    auto TrampolineAddr = AvailableTrampolines.back();
    AvailableTrampolines.pop_back();
    return TrampolineAddr;
  }

  /// Returns the given trampoline to the pool for re-use.
  void releaseTrampoline(ExecutorAddr TrampolineAddr) {
    std::lock_guard<std::mutex> Lock(TPMutex);
    AvailableTrampolines.push_back(TrampolineAddr);
  }

protected:
  virtual Error grow() = 0;

  std::mutex TPMutex;
  std::vector<ExecutorAddr> AvailableTrampolines;
};

/// A trampoline pool for trampolines within the current process.
template <typename ORCABI> class LocalTrampolinePool : public TrampolinePool {
public:
  /// Creates a LocalTrampolinePool with the given RunCallback function.
  /// Returns an error if this function is unable to correctly allocate, write
  /// and protect the resolver code block.
  static Expected<std::unique_ptr<LocalTrampolinePool>>
  Create(ResolveLandingFunction ResolveLanding) {
    Error Err = Error::success();

    auto LTP = std::unique_ptr<LocalTrampolinePool>(
        new LocalTrampolinePool(std::move(ResolveLanding), Err));

    if (Err)
      return std::move(Err);
    return std::move(LTP);
  }

private:
  static JITTargetAddress reenter(void *TrampolinePoolPtr, void *TrampolineId) {
    LocalTrampolinePool<ORCABI> *TrampolinePool =
        static_cast<LocalTrampolinePool *>(TrampolinePoolPtr);

    std::promise<ExecutorAddr> LandingAddressP;
    auto LandingAddressF = LandingAddressP.get_future();

    TrampolinePool->ResolveLanding(ExecutorAddr::fromPtr(TrampolineId),
                                   [&](ExecutorAddr LandingAddress) {
                                     LandingAddressP.set_value(LandingAddress);
                                   });
    return LandingAddressF.get().getValue();
  }

  LocalTrampolinePool(ResolveLandingFunction ResolveLanding, Error &Err)
      : ResolveLanding(std::move(ResolveLanding)) {

    ErrorAsOutParameter _(&Err);

    /// Try to set up the resolver block.
    std::error_code EC;
    ResolverBlock = sys::OwningMemoryBlock(sys::Memory::allocateMappedMemory(
        ORCABI::ResolverCodeSize, nullptr,
        sys::Memory::MF_READ | sys::Memory::MF_WRITE, EC));
    if (EC) {
      Err = errorCodeToError(EC);
      return;
    }

    ORCABI::writeResolverCode(static_cast<char *>(ResolverBlock.base()),
                              ExecutorAddr::fromPtr(ResolverBlock.base()),
                              ExecutorAddr::fromPtr(&reenter),
                              ExecutorAddr::fromPtr(this));

    EC = sys::Memory::protectMappedMemory(ResolverBlock.getMemoryBlock(),
                                          sys::Memory::MF_READ |
                                              sys::Memory::MF_EXEC);
    if (EC) {
      Err = errorCodeToError(EC);
      return;
    }
  }

  Error grow() override {
    assert(AvailableTrampolines.empty() && "Growing prematurely?");

    std::error_code EC;
    auto TrampolineBlock =
        sys::OwningMemoryBlock(sys::Memory::allocateMappedMemory(
            sys::Process::getPageSizeEstimate(), nullptr,
            sys::Memory::MF_READ | sys::Memory::MF_WRITE, EC));
    if (EC)
      return errorCodeToError(EC);

    unsigned NumTrampolines =
        (sys::Process::getPageSizeEstimate() - ORCABI::PointerSize) /
        ORCABI::TrampolineSize;

    char *TrampolineMem = static_cast<char *>(TrampolineBlock.base());
    ORCABI::writeTrampolines(
        TrampolineMem, ExecutorAddr::fromPtr(TrampolineMem),
        ExecutorAddr::fromPtr(ResolverBlock.base()), NumTrampolines);

    for (unsigned I = 0; I < NumTrampolines; ++I)
      AvailableTrampolines.push_back(
          ExecutorAddr::fromPtr(TrampolineMem + (I * ORCABI::TrampolineSize)));

    if (auto EC = sys::Memory::protectMappedMemory(
                    TrampolineBlock.getMemoryBlock(),
                    sys::Memory::MF_READ | sys::Memory::MF_EXEC))
      return errorCodeToError(EC);

    TrampolineBlocks.push_back(std::move(TrampolineBlock));
    return Error::success();
  }

  ResolveLandingFunction ResolveLanding;

  sys::OwningMemoryBlock ResolverBlock;
  std::vector<sys::OwningMemoryBlock> TrampolineBlocks;
};

/// Target-independent base class for compile callback management.
class JITCompileCallbackManager {
public:
  using CompileFunction = std::function<ExecutorAddr()>;

  virtual ~JITCompileCallbackManager() = default;

  /// Reserve a compile callback.
  Expected<ExecutorAddr> getCompileCallback(CompileFunction Compile);

  /// Execute the callback for the given trampoline id. Called by the JIT
  ///        to compile functions on demand.
  ExecutorAddr executeCompileCallback(ExecutorAddr TrampolineAddr);

protected:
  /// Construct a JITCompileCallbackManager.
  JITCompileCallbackManager(std::unique_ptr<TrampolinePool> TP,
                            ExecutionSession &ES,
                            ExecutorAddr ErrorHandlerAddress)
      : TP(std::move(TP)), ES(ES),
        CallbacksJD(ES.createBareJITDylib("<Callbacks>")),
        ErrorHandlerAddress(ErrorHandlerAddress) {}

  void setTrampolinePool(std::unique_ptr<TrampolinePool> TP) {
    this->TP = std::move(TP);
  }

private:
  std::mutex CCMgrMutex;
  std::unique_ptr<TrampolinePool> TP;
  ExecutionSession &ES;
  JITDylib &CallbacksJD;
  ExecutorAddr ErrorHandlerAddress;
  std::map<ExecutorAddr, SymbolStringPtr> AddrToSymbol;
  size_t NextCallbackId = 0;
};

/// Manage compile callbacks for in-process JITs.
template <typename ORCABI>
class LocalJITCompileCallbackManager : public JITCompileCallbackManager {
public:
  /// Create a new LocalJITCompileCallbackManager.
  static Expected<std::unique_ptr<LocalJITCompileCallbackManager>>
  Create(ExecutionSession &ES, ExecutorAddr ErrorHandlerAddress) {
    Error Err = Error::success();
    auto CCMgr = std::unique_ptr<LocalJITCompileCallbackManager>(
        new LocalJITCompileCallbackManager(ES, ErrorHandlerAddress, Err));
    if (Err)
      return std::move(Err);
    return std::move(CCMgr);
  }

private:
  /// Construct a InProcessJITCompileCallbackManager.
  /// @param ErrorHandlerAddress The address of an error handler in the target
  ///                            process to be used if a compile callback fails.
  LocalJITCompileCallbackManager(ExecutionSession &ES,
                                 ExecutorAddr ErrorHandlerAddress, Error &Err)
      : JITCompileCallbackManager(nullptr, ES, ErrorHandlerAddress) {
    using NotifyLandingResolvedFunction =
        TrampolinePool::NotifyLandingResolvedFunction;

    ErrorAsOutParameter _(&Err);
    auto TP = LocalTrampolinePool<ORCABI>::Create(
        [this](ExecutorAddr TrampolineAddr,
               NotifyLandingResolvedFunction NotifyLandingResolved) {
          NotifyLandingResolved(executeCompileCallback(TrampolineAddr));
        });

    if (!TP) {
      Err = TP.takeError();
      return;
    }

    setTrampolinePool(std::move(*TP));
  }
};

/// Base class for managing collections of named indirect stubs.
class IndirectStubsManager {
public:
  /// Map type for initializing the manager. See init.
  using StubInitsMap = StringMap<std::pair<ExecutorAddr, JITSymbolFlags>>;

  virtual ~IndirectStubsManager() = default;

  /// Create a single stub with the given name, target address and flags.
  virtual Error createStub(StringRef StubName, ExecutorAddr StubAddr,
                           JITSymbolFlags StubFlags) = 0;

  /// Create StubInits.size() stubs with the given names, target
  ///        addresses, and flags.
  virtual Error createStubs(const StubInitsMap &StubInits) = 0;

  /// Find the stub with the given name. If ExportedStubsOnly is true,
  ///        this will only return a result if the stub's flags indicate that it
  ///        is exported.
  virtual ExecutorSymbolDef findStub(StringRef Name,
                                     bool ExportedStubsOnly) = 0;

  /// Find the implementation-pointer for the stub.
  virtual ExecutorSymbolDef findPointer(StringRef Name) = 0;

  /// Change the value of the implementation pointer for the stub.
  virtual Error updatePointer(StringRef Name, ExecutorAddr NewAddr) = 0;

private:
  virtual void anchor();
};

template <typename ORCABI> class LocalIndirectStubsInfo {
public:
  LocalIndirectStubsInfo(unsigned NumStubs, sys::OwningMemoryBlock StubsMem)
      : NumStubs(NumStubs), StubsMem(std::move(StubsMem)) {}

  static Expected<LocalIndirectStubsInfo> create(unsigned MinStubs,
                                                 unsigned PageSize) {
    auto ISAS = getIndirectStubsBlockSizes<ORCABI>(MinStubs, PageSize);

    assert((ISAS.StubBytes % PageSize == 0) &&
           "StubBytes is not a page size multiple");
    uint64_t PointerAlloc = alignTo(ISAS.PointerBytes, PageSize);

    // Allocate memory for stubs and pointers in one call.
    std::error_code EC;
    auto StubsAndPtrsMem =
        sys::OwningMemoryBlock(sys::Memory::allocateMappedMemory(
            ISAS.StubBytes + PointerAlloc, nullptr,
            sys::Memory::MF_READ | sys::Memory::MF_WRITE, EC));
    if (EC)
      return errorCodeToError(EC);

    sys::MemoryBlock StubsBlock(StubsAndPtrsMem.base(), ISAS.StubBytes);
    auto StubsBlockMem = static_cast<char *>(StubsAndPtrsMem.base());
    auto PtrBlockAddress =
        ExecutorAddr::fromPtr(StubsBlockMem) + ISAS.StubBytes;

    ORCABI::writeIndirectStubsBlock(StubsBlockMem,
                                    ExecutorAddr::fromPtr(StubsBlockMem),
                                    PtrBlockAddress, ISAS.NumStubs);

    if (auto EC = sys::Memory::protectMappedMemory(
            StubsBlock, sys::Memory::MF_READ | sys::Memory::MF_EXEC))
      return errorCodeToError(EC);

    return LocalIndirectStubsInfo(ISAS.NumStubs, std::move(StubsAndPtrsMem));
  }

  unsigned getNumStubs() const { return NumStubs; }

  void *getStub(unsigned Idx) const {
    return static_cast<char *>(StubsMem.base()) + Idx * ORCABI::StubSize;
  }

  void **getPtr(unsigned Idx) const {
    char *PtrsBase =
        static_cast<char *>(StubsMem.base()) + NumStubs * ORCABI::StubSize;
    return reinterpret_cast<void **>(PtrsBase) + Idx;
  }

private:
  unsigned NumStubs = 0;
  sys::OwningMemoryBlock StubsMem;
};

/// IndirectStubsManager implementation for the host architecture, e.g.
///        OrcX86_64. (See OrcArchitectureSupport.h).
template <typename TargetT>
class LocalIndirectStubsManager : public IndirectStubsManager {
public:
  Error createStub(StringRef StubName, ExecutorAddr StubAddr,
                   JITSymbolFlags StubFlags) override {
    std::lock_guard<std::mutex> Lock(StubsMutex);
    if (auto Err = reserveStubs(1))
      return Err;

    createStubInternal(StubName, StubAddr, StubFlags);

    return Error::success();
  }

  Error createStubs(const StubInitsMap &StubInits) override {
    std::lock_guard<std::mutex> Lock(StubsMutex);
    if (auto Err = reserveStubs(StubInits.size()))
      return Err;

    for (const auto &Entry : StubInits)
      createStubInternal(Entry.first(), Entry.second.first,
                         Entry.second.second);

    return Error::success();
  }

  ExecutorSymbolDef findStub(StringRef Name, bool ExportedStubsOnly) override {
    std::lock_guard<std::mutex> Lock(StubsMutex);
    auto I = StubIndexes.find(Name);
    if (I == StubIndexes.end())
      return ExecutorSymbolDef();
    auto Key = I->second.first;
    void *StubPtr = IndirectStubsInfos[Key.first].getStub(Key.second);
    assert(StubPtr && "Missing stub address");
    auto StubAddr = ExecutorAddr::fromPtr(StubPtr);
    auto StubSymbol = ExecutorSymbolDef(StubAddr, I->second.second);
    if (ExportedStubsOnly && !StubSymbol.getFlags().isExported())
      return ExecutorSymbolDef();
    return StubSymbol;
  }

  ExecutorSymbolDef findPointer(StringRef Name) override {
    std::lock_guard<std::mutex> Lock(StubsMutex);
    auto I = StubIndexes.find(Name);
    if (I == StubIndexes.end())
      return ExecutorSymbolDef();
    auto Key = I->second.first;
    void *PtrPtr = IndirectStubsInfos[Key.first].getPtr(Key.second);
    assert(PtrPtr && "Missing pointer address");
    auto PtrAddr = ExecutorAddr::fromPtr(PtrPtr);
    return ExecutorSymbolDef(PtrAddr, I->second.second);
  }

  Error updatePointer(StringRef Name, ExecutorAddr NewAddr) override {
    using AtomicIntPtr = std::atomic<uintptr_t>;

    std::lock_guard<std::mutex> Lock(StubsMutex);
    auto I = StubIndexes.find(Name);
    assert(I != StubIndexes.end() && "No stub pointer for symbol");
    auto Key = I->second.first;
    AtomicIntPtr *AtomicStubPtr = reinterpret_cast<AtomicIntPtr *>(
        IndirectStubsInfos[Key.first].getPtr(Key.second));
    *AtomicStubPtr = static_cast<uintptr_t>(NewAddr.getValue());
    return Error::success();
  }

private:
  Error reserveStubs(unsigned NumStubs) {
    if (NumStubs <= FreeStubs.size())
      return Error::success();

    unsigned NewStubsRequired = NumStubs - FreeStubs.size();
    unsigned NewBlockId = IndirectStubsInfos.size();
    auto ISI =
        LocalIndirectStubsInfo<TargetT>::create(NewStubsRequired, PageSize);
    if (!ISI)
      return ISI.takeError();
    for (unsigned I = 0; I < ISI->getNumStubs(); ++I)
      FreeStubs.push_back(std::make_pair(NewBlockId, I));
    IndirectStubsInfos.push_back(std::move(*ISI));
    return Error::success();
  }

  void createStubInternal(StringRef StubName, ExecutorAddr InitAddr,
                          JITSymbolFlags StubFlags) {
    auto Key = FreeStubs.back();
    FreeStubs.pop_back();
    *IndirectStubsInfos[Key.first].getPtr(Key.second) =
        InitAddr.toPtr<void *>();
    StubIndexes[StubName] = std::make_pair(Key, StubFlags);
  }

  unsigned PageSize = sys::Process::getPageSizeEstimate();
  std::mutex StubsMutex;
  std::vector<LocalIndirectStubsInfo<TargetT>> IndirectStubsInfos;
  using StubKey = std::pair<uint16_t, uint16_t>;
  std::vector<StubKey> FreeStubs;
  StringMap<std::pair<StubKey, JITSymbolFlags>> StubIndexes;
};

/// Create a local compile callback manager.
///
/// The given target triple will determine the ABI, and the given
/// ErrorHandlerAddress will be used by the resulting compile callback
/// manager if a compile callback fails.
Expected<std::unique_ptr<JITCompileCallbackManager>>
createLocalCompileCallbackManager(const Triple &T, ExecutionSession &ES,
                                  ExecutorAddr ErrorHandlerAddress);

/// Create a local indirect stubs manager builder.
///
/// The given target triple will determine the ABI.
std::function<std::unique_ptr<IndirectStubsManager>()>
createLocalIndirectStubsManagerBuilder(const Triple &T);

/// Build a function pointer of FunctionType with the given constant
///        address.
///
///   Usage example: Turn a trampoline address into a function pointer constant
/// for use in a stub.
Constant *createIRTypedAddress(FunctionType &FT, ExecutorAddr Addr);

/// Create a function pointer with the given type, name, and initializer
///        in the given Module.
GlobalVariable *createImplPointer(PointerType &PT, Module &M, const Twine &Name,
                                  Constant *Initializer);

/// Turn a function declaration into a stub function that makes an
///        indirect call using the given function pointer.
void makeStub(Function &F, Value &ImplPointer);

/// Promotes private symbols to global hidden, and renames to prevent clashes
/// with other promoted symbols. The same SymbolPromoter instance should be
/// used for all symbols to be added to a single JITDylib.
class SymbolLinkagePromoter {
public:
  /// Promote symbols in the given module. Returns the set of global values
  /// that have been renamed/promoted.
  std::vector<GlobalValue *> operator()(Module &M);

private:
  unsigned NextId = 0;
};

/// Clone a function declaration into a new module.
///
///   This function can be used as the first step towards creating a callback
/// stub (see makeStub).
///
///   If the VMap argument is non-null, a mapping will be added between F and
/// the new declaration, and between each of F's arguments and the new
/// declaration's arguments. This map can then be passed in to moveFunction to
/// move the function body if required. Note: When moving functions between
/// modules with these utilities, all decls should be cloned (and added to a
/// single VMap) before any bodies are moved. This will ensure that references
/// between functions all refer to the versions in the new module.
Function *cloneFunctionDecl(Module &Dst, const Function &F,
                            ValueToValueMapTy *VMap = nullptr);

/// Clone a global variable declaration into a new module.
GlobalVariable *cloneGlobalVariableDecl(Module &Dst, const GlobalVariable &GV,
                                        ValueToValueMapTy *VMap = nullptr);

/// Clone a global alias declaration into a new module.
GlobalAlias *cloneGlobalAliasDecl(Module &Dst, const GlobalAlias &OrigA,
                                  ValueToValueMapTy &VMap);

/// Introduce relocations to \p Sym in its own definition if there are any
/// pointers formed via PC-relative address that do not already have a
/// relocation.
///
/// This is useful when introducing indirection via a stub function at link time
/// without compiler support. If a function pointer is formed without a
/// relocation, e.g. in the definition of \c foo
///
/// \code
/// _foo:
///   leaq -7(%rip), rax # form pointer to _foo without relocation
/// _bar:
///   leaq (%rip), %rax  # uses X86_64_RELOC_SIGNED to '_foo'
/// \endcode
///
/// the pointer to \c _foo computed by \c _foo and \c _bar may differ if we
/// introduce a stub for _foo. If the pointer is used as a key, this may be
/// observable to the program. This pass will attempt to introduce the missing
/// "self-relocation" on the leaq instruction.
///
/// This is based on disassembly and should be considered "best effort". It may
/// silently fail to add relocations.
Error addFunctionPointerRelocationsToCurrentSymbol(jitlink::Symbol &Sym,
                                                   jitlink::LinkGraph &G,
                                                   MCDisassembler &Disassembler,
                                                   MCInstrAnalysis &MIA);

} // end namespace orc

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_INDIRECTIONUTILS_H
