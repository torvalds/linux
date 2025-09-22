//===- ExecutorProcessControl.h - Executor process control APIs -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for interacting with the executor processes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EXECUTORPROCESSCONTROL_H
#define LLVM_EXECUTIONENGINE_ORC_EXECUTORPROCESSCONTROL_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/ExecutionEngine/Orc/SymbolStringPool.h"
#include "llvm/ExecutionEngine/Orc/TaskDispatch.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/MSVCErrorWorkarounds.h"
#include "llvm/TargetParser/Triple.h"

#include <future>
#include <mutex>
#include <vector>

namespace llvm {
namespace orc {

class ExecutionSession;
class SymbolLookupSet;

/// ExecutorProcessControl supports interaction with a JIT target process.
class ExecutorProcessControl {
  friend class ExecutionSession;
public:

  /// A handler or incoming WrapperFunctionResults -- either return values from
  /// callWrapper* calls, or incoming JIT-dispatch requests.
  ///
  /// IncomingWFRHandlers are constructible from
  /// unique_function<void(shared::WrapperFunctionResult)>s using the
  /// runInPlace function or a RunWithDispatch object.
  class IncomingWFRHandler {
    friend class ExecutorProcessControl;
  public:
    IncomingWFRHandler() = default;
    explicit operator bool() const { return !!H; }
    void operator()(shared::WrapperFunctionResult WFR) { H(std::move(WFR)); }
  private:
    template <typename FnT> IncomingWFRHandler(FnT &&Fn)
      : H(std::forward<FnT>(Fn)) {}

    unique_function<void(shared::WrapperFunctionResult)> H;
  };

  /// Constructs an IncomingWFRHandler from a function object that is callable
  /// as void(shared::WrapperFunctionResult). The function object will be called
  /// directly. This should be used with care as it may block listener threads
  /// in remote EPCs. It is only suitable for simple tasks (e.g. setting a
  /// future), or for performing some quick analysis before dispatching "real"
  /// work as a Task.
  class RunInPlace {
  public:
    template <typename FnT>
    IncomingWFRHandler operator()(FnT &&Fn) {
      return IncomingWFRHandler(std::forward<FnT>(Fn));
    }
  };

  /// Constructs an IncomingWFRHandler from a function object by creating a new
  /// function object that dispatches the original using a TaskDispatcher,
  /// wrapping the original as a GenericNamedTask.
  ///
  /// This is the default approach for running WFR handlers.
  class RunAsTask {
  public:
    RunAsTask(TaskDispatcher &D) : D(D) {}

    template <typename FnT>
    IncomingWFRHandler operator()(FnT &&Fn) {
      return IncomingWFRHandler(
          [&D = this->D, Fn = std::move(Fn)]
          (shared::WrapperFunctionResult WFR) mutable {
              D.dispatch(
                makeGenericNamedTask(
                    [Fn = std::move(Fn), WFR = std::move(WFR)]() mutable {
                      Fn(std::move(WFR));
                    }, "WFR handler task"));
          });
    }
  private:
    TaskDispatcher &D;
  };

  /// APIs for manipulating memory in the target process.
  class MemoryAccess {
  public:
    /// Callback function for asynchronous writes.
    using WriteResultFn = unique_function<void(Error)>;

    virtual ~MemoryAccess();

    virtual void writeUInt8sAsync(ArrayRef<tpctypes::UInt8Write> Ws,
                                  WriteResultFn OnWriteComplete) = 0;

    virtual void writeUInt16sAsync(ArrayRef<tpctypes::UInt16Write> Ws,
                                   WriteResultFn OnWriteComplete) = 0;

    virtual void writeUInt32sAsync(ArrayRef<tpctypes::UInt32Write> Ws,
                                   WriteResultFn OnWriteComplete) = 0;

    virtual void writeUInt64sAsync(ArrayRef<tpctypes::UInt64Write> Ws,
                                   WriteResultFn OnWriteComplete) = 0;

    virtual void writeBuffersAsync(ArrayRef<tpctypes::BufferWrite> Ws,
                                   WriteResultFn OnWriteComplete) = 0;

    virtual void writePointersAsync(ArrayRef<tpctypes::PointerWrite> Ws,
                                    WriteResultFn OnWriteComplete) = 0;

    Error writeUInt8s(ArrayRef<tpctypes::UInt8Write> Ws) {
      std::promise<MSVCPError> ResultP;
      auto ResultF = ResultP.get_future();
      writeUInt8sAsync(Ws,
                       [&](Error Err) { ResultP.set_value(std::move(Err)); });
      return ResultF.get();
    }

    Error writeUInt16s(ArrayRef<tpctypes::UInt16Write> Ws) {
      std::promise<MSVCPError> ResultP;
      auto ResultF = ResultP.get_future();
      writeUInt16sAsync(Ws,
                        [&](Error Err) { ResultP.set_value(std::move(Err)); });
      return ResultF.get();
    }

    Error writeUInt32s(ArrayRef<tpctypes::UInt32Write> Ws) {
      std::promise<MSVCPError> ResultP;
      auto ResultF = ResultP.get_future();
      writeUInt32sAsync(Ws,
                        [&](Error Err) { ResultP.set_value(std::move(Err)); });
      return ResultF.get();
    }

    Error writeUInt64s(ArrayRef<tpctypes::UInt64Write> Ws) {
      std::promise<MSVCPError> ResultP;
      auto ResultF = ResultP.get_future();
      writeUInt64sAsync(Ws,
                        [&](Error Err) { ResultP.set_value(std::move(Err)); });
      return ResultF.get();
    }

    Error writeBuffers(ArrayRef<tpctypes::BufferWrite> Ws) {
      std::promise<MSVCPError> ResultP;
      auto ResultF = ResultP.get_future();
      writeBuffersAsync(Ws,
                        [&](Error Err) { ResultP.set_value(std::move(Err)); });
      return ResultF.get();
    }

    Error writePointers(ArrayRef<tpctypes::PointerWrite> Ws) {
      std::promise<MSVCPError> ResultP;
      auto ResultF = ResultP.get_future();
      writePointersAsync(Ws,
                         [&](Error Err) { ResultP.set_value(std::move(Err)); });
      return ResultF.get();
    }
  };

  /// A pair of a dylib and a set of symbols to be looked up.
  struct LookupRequest {
    LookupRequest(tpctypes::DylibHandle Handle, const SymbolLookupSet &Symbols)
        : Handle(Handle), Symbols(Symbols) {}
    tpctypes::DylibHandle Handle;
    const SymbolLookupSet &Symbols;
  };

  /// Contains the address of the dispatch function and context that the ORC
  /// runtime can use to call functions in the JIT.
  struct JITDispatchInfo {
    ExecutorAddr JITDispatchFunction;
    ExecutorAddr JITDispatchContext;
  };

  ExecutorProcessControl(std::shared_ptr<SymbolStringPool> SSP,
                         std::unique_ptr<TaskDispatcher> D)
    : SSP(std::move(SSP)), D(std::move(D)) {}

  virtual ~ExecutorProcessControl();

  /// Return the ExecutionSession associated with this instance.
  /// Not callable until the ExecutionSession has been associated.
  ExecutionSession &getExecutionSession() {
    assert(ES && "No ExecutionSession associated yet");
    return *ES;
  }

  /// Intern a symbol name in the SymbolStringPool.
  SymbolStringPtr intern(StringRef SymName) { return SSP->intern(SymName); }

  /// Return a shared pointer to the SymbolStringPool for this instance.
  std::shared_ptr<SymbolStringPool> getSymbolStringPool() const { return SSP; }

  TaskDispatcher &getDispatcher() { return *D; }

  /// Return the Triple for the target process.
  const Triple &getTargetTriple() const { return TargetTriple; }

  /// Get the page size for the target process.
  unsigned getPageSize() const { return PageSize; }

  /// Get the JIT dispatch function and context address for the executor.
  const JITDispatchInfo &getJITDispatchInfo() const { return JDI; }

  /// Return a MemoryAccess object for the target process.
  MemoryAccess &getMemoryAccess() const {
    assert(MemAccess && "No MemAccess object set.");
    return *MemAccess;
  }

  /// Return a JITLinkMemoryManager for the target process.
  jitlink::JITLinkMemoryManager &getMemMgr() const {
    assert(MemMgr && "No MemMgr object set");
    return *MemMgr;
  }

  /// Returns the bootstrap map.
  const StringMap<std::vector<char>> &getBootstrapMap() const {
    return BootstrapMap;
  }

  /// Look up and SPS-deserialize a bootstrap map value.
  ///
  ///
  template <typename T, typename SPSTagT>
  Error getBootstrapMapValue(StringRef Key, std::optional<T> &Val) const {
    Val = std::nullopt;

    auto I = BootstrapMap.find(Key);
    if (I == BootstrapMap.end())
      return Error::success();

    T Tmp;
    shared::SPSInputBuffer IB(I->second.data(), I->second.size());
    if (!shared::SPSArgList<SPSTagT>::deserialize(IB, Tmp))
      return make_error<StringError>("Could not deserialize value for key " +
                                         Key,
                                     inconvertibleErrorCode());

    Val = std::move(Tmp);
    return Error::success();
  }

  /// Returns the bootstrap symbol map.
  const StringMap<ExecutorAddr> &getBootstrapSymbolsMap() const {
    return BootstrapSymbols;
  }

  /// For each (ExecutorAddr&, StringRef) pair, looks up the string in the
  /// bootstrap symbols map and writes its address to the ExecutorAddr if
  /// found. If any symbol is not found then the function returns an error.
  Error getBootstrapSymbols(
      ArrayRef<std::pair<ExecutorAddr &, StringRef>> Pairs) const {
    for (const auto &KV : Pairs) {
      auto I = BootstrapSymbols.find(KV.second);
      if (I == BootstrapSymbols.end())
        return make_error<StringError>("Symbol \"" + KV.second +
                                           "\" not found "
                                           "in bootstrap symbols map",
                                       inconvertibleErrorCode());

      KV.first = I->second;
    }
    return Error::success();
  }

  /// Load the dynamic library at the given path and return a handle to it.
  /// If LibraryPath is null this function will return the global handle for
  /// the target process.
  virtual Expected<tpctypes::DylibHandle> loadDylib(const char *DylibPath) = 0;

  /// Search for symbols in the target process.
  ///
  /// The result of the lookup is a 2-dimensional array of target addresses
  /// that correspond to the lookup order. If a required symbol is not
  /// found then this method will return an error. If a weakly referenced
  /// symbol is not found then it be assigned a '0' value.
  Expected<std::vector<tpctypes::LookupResult>>
  lookupSymbols(ArrayRef<LookupRequest> Request) {
    std::promise<MSVCPExpected<std::vector<tpctypes::LookupResult>>> RP;
    auto RF = RP.get_future();
    lookupSymbolsAsync(Request,
                       [&RP](auto Result) { RP.set_value(std::move(Result)); });
    return RF.get();
  }

  using SymbolLookupCompleteFn =
      unique_function<void(Expected<std::vector<tpctypes::LookupResult>>)>;

  /// Search for symbols in the target process.
  ///
  /// The result of the lookup is a 2-dimensional array of target addresses
  /// that correspond to the lookup order. If a required symbol is not
  /// found then this method will return an error. If a weakly referenced
  /// symbol is not found then it be assigned a '0' value.
  virtual void lookupSymbolsAsync(ArrayRef<LookupRequest> Request,
                                  SymbolLookupCompleteFn F) = 0;

  /// Run function with a main-like signature.
  virtual Expected<int32_t> runAsMain(ExecutorAddr MainFnAddr,
                                      ArrayRef<std::string> Args) = 0;

  // TODO: move this to ORC runtime.
  /// Run function with a int (*)(void) signature.
  virtual Expected<int32_t> runAsVoidFunction(ExecutorAddr VoidFnAddr) = 0;

  // TODO: move this to ORC runtime.
  /// Run function with a int (*)(int) signature.
  virtual Expected<int32_t> runAsIntFunction(ExecutorAddr IntFnAddr,
                                             int Arg) = 0;

  /// Run a wrapper function in the executor. The given WFRHandler will be
  /// called on the result when it is returned.
  ///
  /// The wrapper function should be callable as:
  ///
  /// \code{.cpp}
  ///   CWrapperFunctionResult fn(uint8_t *Data, uint64_t Size);
  /// \endcode{.cpp}
  virtual void callWrapperAsync(ExecutorAddr WrapperFnAddr,
                                IncomingWFRHandler OnComplete,
                                ArrayRef<char> ArgBuffer) = 0;

  /// Run a wrapper function in the executor using the given Runner to dispatch
  /// OnComplete when the result is ready.
  template <typename RunPolicyT, typename FnT>
  void callWrapperAsync(RunPolicyT &&Runner, ExecutorAddr WrapperFnAddr,
                        FnT &&OnComplete, ArrayRef<char> ArgBuffer) {
    callWrapperAsync(
        WrapperFnAddr, Runner(std::forward<FnT>(OnComplete)), ArgBuffer);
  }

  /// Run a wrapper function in the executor. OnComplete will be dispatched
  /// as a GenericNamedTask using this instance's TaskDispatch object.
  template <typename FnT>
  void callWrapperAsync(ExecutorAddr WrapperFnAddr, FnT &&OnComplete,
                        ArrayRef<char> ArgBuffer) {
    callWrapperAsync(RunAsTask(*D), WrapperFnAddr,
                     std::forward<FnT>(OnComplete), ArgBuffer);
  }

  /// Run a wrapper function in the executor. The wrapper function should be
  /// callable as:
  ///
  /// \code{.cpp}
  ///   CWrapperFunctionResult fn(uint8_t *Data, uint64_t Size);
  /// \endcode{.cpp}
  shared::WrapperFunctionResult callWrapper(ExecutorAddr WrapperFnAddr,
                                            ArrayRef<char> ArgBuffer) {
    std::promise<shared::WrapperFunctionResult> RP;
    auto RF = RP.get_future();
    callWrapperAsync(
        RunInPlace(), WrapperFnAddr,
        [&](shared::WrapperFunctionResult R) {
          RP.set_value(std::move(R));
        }, ArgBuffer);
    return RF.get();
  }

  /// Run a wrapper function using SPS to serialize the arguments and
  /// deserialize the results.
  template <typename SPSSignature, typename RunPolicyT, typename SendResultT,
            typename... ArgTs>
  void callSPSWrapperAsync(RunPolicyT &&Runner, ExecutorAddr WrapperFnAddr,
                           SendResultT &&SendResult, const ArgTs &...Args) {
    shared::WrapperFunction<SPSSignature>::callAsync(
        [this, WrapperFnAddr, Runner = std::move(Runner)]
        (auto &&SendResult, const char *ArgData, size_t ArgSize) mutable {
          this->callWrapperAsync(std::move(Runner), WrapperFnAddr,
                                 std::move(SendResult),
                                 ArrayRef<char>(ArgData, ArgSize));
        },
        std::forward<SendResultT>(SendResult), Args...);
  }

  /// Run a wrapper function using SPS to serialize the arguments and
  /// deserialize the results.
  template <typename SPSSignature, typename SendResultT, typename... ArgTs>
  void callSPSWrapperAsync(ExecutorAddr WrapperFnAddr, SendResultT &&SendResult,
                           const ArgTs &...Args) {
    callSPSWrapperAsync<SPSSignature>(RunAsTask(*D), WrapperFnAddr,
                                      std::forward<SendResultT>(SendResult),
                                      Args...);
  }

  /// Run a wrapper function using SPS to serialize the arguments and
  /// deserialize the results.
  ///
  /// If SPSSignature is a non-void function signature then the second argument
  /// (the first in the Args list) should be a reference to a return value.
  template <typename SPSSignature, typename... WrapperCallArgTs>
  Error callSPSWrapper(ExecutorAddr WrapperFnAddr,
                       WrapperCallArgTs &&...WrapperCallArgs) {
    return shared::WrapperFunction<SPSSignature>::call(
        [this, WrapperFnAddr](const char *ArgData, size_t ArgSize) {
          return callWrapper(WrapperFnAddr, ArrayRef<char>(ArgData, ArgSize));
        },
        std::forward<WrapperCallArgTs>(WrapperCallArgs)...);
  }

  /// Disconnect from the target process.
  ///
  /// This should be called after the JIT session is shut down.
  virtual Error disconnect() = 0;

protected:

  std::shared_ptr<SymbolStringPool> SSP;
  std::unique_ptr<TaskDispatcher> D;
  ExecutionSession *ES = nullptr;
  Triple TargetTriple;
  unsigned PageSize = 0;
  JITDispatchInfo JDI;
  MemoryAccess *MemAccess = nullptr;
  jitlink::JITLinkMemoryManager *MemMgr = nullptr;
  StringMap<std::vector<char>> BootstrapMap;
  StringMap<ExecutorAddr> BootstrapSymbols;
};

class InProcessMemoryAccess : public ExecutorProcessControl::MemoryAccess {
public:
  InProcessMemoryAccess(bool IsArch64Bit) : IsArch64Bit(IsArch64Bit) {}
  void writeUInt8sAsync(ArrayRef<tpctypes::UInt8Write> Ws,
                        WriteResultFn OnWriteComplete) override;

  void writeUInt16sAsync(ArrayRef<tpctypes::UInt16Write> Ws,
                         WriteResultFn OnWriteComplete) override;

  void writeUInt32sAsync(ArrayRef<tpctypes::UInt32Write> Ws,
                         WriteResultFn OnWriteComplete) override;

  void writeUInt64sAsync(ArrayRef<tpctypes::UInt64Write> Ws,
                         WriteResultFn OnWriteComplete) override;

  void writeBuffersAsync(ArrayRef<tpctypes::BufferWrite> Ws,
                         WriteResultFn OnWriteComplete) override;

  void writePointersAsync(ArrayRef<tpctypes::PointerWrite> Ws,
                          WriteResultFn OnWriteComplete) override;

private:
  bool IsArch64Bit;
};

/// A ExecutorProcessControl instance that asserts if any of its methods are
/// used. Suitable for use is unit tests, and by ORC clients who haven't moved
/// to ExecutorProcessControl-based APIs yet.
class UnsupportedExecutorProcessControl : public ExecutorProcessControl,
                                          private InProcessMemoryAccess {
public:
  UnsupportedExecutorProcessControl(
      std::shared_ptr<SymbolStringPool> SSP = nullptr,
      std::unique_ptr<TaskDispatcher> D = nullptr, const std::string &TT = "",
      unsigned PageSize = 0)
      : ExecutorProcessControl(
            SSP ? std::move(SSP) : std::make_shared<SymbolStringPool>(),
            D ? std::move(D) : std::make_unique<InPlaceTaskDispatcher>()),
        InProcessMemoryAccess(Triple(TT).isArch64Bit()) {
    this->TargetTriple = Triple(TT);
    this->PageSize = PageSize;
    this->MemAccess = this;
  }

  Expected<tpctypes::DylibHandle> loadDylib(const char *DylibPath) override {
    llvm_unreachable("Unsupported");
  }

  void lookupSymbolsAsync(ArrayRef<LookupRequest> Request,
                          SymbolLookupCompleteFn F) override {
    llvm_unreachable("Unsupported");
  }

  Expected<int32_t> runAsMain(ExecutorAddr MainFnAddr,
                              ArrayRef<std::string> Args) override {
    llvm_unreachable("Unsupported");
  }

  Expected<int32_t> runAsVoidFunction(ExecutorAddr VoidFnAddr) override {
    llvm_unreachable("Unsupported");
  }

  Expected<int32_t> runAsIntFunction(ExecutorAddr IntFnAddr, int Arg) override {
    llvm_unreachable("Unsupported");
  }

  void callWrapperAsync(ExecutorAddr WrapperFnAddr,
                        IncomingWFRHandler OnComplete,
                        ArrayRef<char> ArgBuffer) override {
    llvm_unreachable("Unsupported");
  }

  Error disconnect() override { return Error::success(); }
};

/// A ExecutorProcessControl implementation targeting the current process.
class SelfExecutorProcessControl : public ExecutorProcessControl,
                                   private InProcessMemoryAccess {
public:
  SelfExecutorProcessControl(
      std::shared_ptr<SymbolStringPool> SSP, std::unique_ptr<TaskDispatcher> D,
      Triple TargetTriple, unsigned PageSize,
      std::unique_ptr<jitlink::JITLinkMemoryManager> MemMgr);

  /// Create a SelfExecutorProcessControl with the given symbol string pool and
  /// memory manager.
  /// If no symbol string pool is given then one will be created.
  /// If no memory manager is given a jitlink::InProcessMemoryManager will
  /// be created and used by default.
  static Expected<std::unique_ptr<SelfExecutorProcessControl>>
  Create(std::shared_ptr<SymbolStringPool> SSP = nullptr,
         std::unique_ptr<TaskDispatcher> D = nullptr,
         std::unique_ptr<jitlink::JITLinkMemoryManager> MemMgr = nullptr);

  Expected<tpctypes::DylibHandle> loadDylib(const char *DylibPath) override;

  void lookupSymbolsAsync(ArrayRef<LookupRequest> Request,
                          SymbolLookupCompleteFn F) override;

  Expected<int32_t> runAsMain(ExecutorAddr MainFnAddr,
                              ArrayRef<std::string> Args) override;

  Expected<int32_t> runAsVoidFunction(ExecutorAddr VoidFnAddr) override;

  Expected<int32_t> runAsIntFunction(ExecutorAddr IntFnAddr, int Arg) override;

  void callWrapperAsync(ExecutorAddr WrapperFnAddr,
                        IncomingWFRHandler OnComplete,
                        ArrayRef<char> ArgBuffer) override;

  Error disconnect() override;

private:
  static shared::CWrapperFunctionResult
  jitDispatchViaWrapperFunctionManager(void *Ctx, const void *FnTag,
                                       const char *Data, size_t Size);

  std::unique_ptr<jitlink::JITLinkMemoryManager> OwnedMemMgr;
  char GlobalManglingPrefix = 0;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_EXECUTORPROCESSCONTROL_H
