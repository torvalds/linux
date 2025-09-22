//===---- SimpleRemoteEPC.h - Simple remote executor control ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Simple remote executor process control.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SIMPLEREMOTEEPC_H
#define LLVM_EXECUTIONENGINE_ORC_SIMPLEREMOTEEPC_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ExecutionEngine/Orc/EPCGenericDylibManager.h"
#include "llvm/ExecutionEngine/Orc/EPCGenericJITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/EPCGenericMemoryAccess.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimpleRemoteEPCUtils.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MSVCErrorWorkarounds.h"

#include <future>

namespace llvm {
namespace orc {

class SimpleRemoteEPC : public ExecutorProcessControl,
                        public SimpleRemoteEPCTransportClient {
public:
  /// A setup object containing callbacks to construct a memory manager and
  /// memory access object. Both are optional. If not specified,
  /// EPCGenericJITLinkMemoryManager and EPCGenericMemoryAccess will be used.
  struct Setup {
    using CreateMemoryManagerFn =
        Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>(
            SimpleRemoteEPC &);
    using CreateMemoryAccessFn =
        Expected<std::unique_ptr<MemoryAccess>>(SimpleRemoteEPC &);

    unique_function<CreateMemoryManagerFn> CreateMemoryManager;
    unique_function<CreateMemoryAccessFn> CreateMemoryAccess;
  };

  /// Create a SimpleRemoteEPC using the given transport type and args.
  template <typename TransportT, typename... TransportTCtorArgTs>
  static Expected<std::unique_ptr<SimpleRemoteEPC>>
  Create(std::unique_ptr<TaskDispatcher> D, Setup S,
         TransportTCtorArgTs &&...TransportTCtorArgs) {
    std::unique_ptr<SimpleRemoteEPC> SREPC(
        new SimpleRemoteEPC(std::make_shared<SymbolStringPool>(),
                            std::move(D)));
    auto T = TransportT::Create(
        *SREPC, std::forward<TransportTCtorArgTs>(TransportTCtorArgs)...);
    if (!T)
      return T.takeError();
    SREPC->T = std::move(*T);
    if (auto Err = SREPC->setup(std::move(S)))
      return joinErrors(std::move(Err), SREPC->disconnect());
    return std::move(SREPC);
  }

  SimpleRemoteEPC(const SimpleRemoteEPC &) = delete;
  SimpleRemoteEPC &operator=(const SimpleRemoteEPC &) = delete;
  SimpleRemoteEPC(SimpleRemoteEPC &&) = delete;
  SimpleRemoteEPC &operator=(SimpleRemoteEPC &&) = delete;
  ~SimpleRemoteEPC();

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

  Expected<HandleMessageAction>
  handleMessage(SimpleRemoteEPCOpcode OpC, uint64_t SeqNo, ExecutorAddr TagAddr,
                SimpleRemoteEPCArgBytesVector ArgBytes) override;

  void handleDisconnect(Error Err) override;

private:
  SimpleRemoteEPC(std::shared_ptr<SymbolStringPool> SSP,
                  std::unique_ptr<TaskDispatcher> D)
    : ExecutorProcessControl(std::move(SSP), std::move(D)) {}

  static Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>
  createDefaultMemoryManager(SimpleRemoteEPC &SREPC);
  static Expected<std::unique_ptr<MemoryAccess>>
  createDefaultMemoryAccess(SimpleRemoteEPC &SREPC);

  Error sendMessage(SimpleRemoteEPCOpcode OpC, uint64_t SeqNo,
                    ExecutorAddr TagAddr, ArrayRef<char> ArgBytes);

  Error handleSetup(uint64_t SeqNo, ExecutorAddr TagAddr,
                    SimpleRemoteEPCArgBytesVector ArgBytes);
  Error setup(Setup S);

  Error handleResult(uint64_t SeqNo, ExecutorAddr TagAddr,
                     SimpleRemoteEPCArgBytesVector ArgBytes);
  void handleCallWrapper(uint64_t RemoteSeqNo, ExecutorAddr TagAddr,
                         SimpleRemoteEPCArgBytesVector ArgBytes);
  Error handleHangup(SimpleRemoteEPCArgBytesVector ArgBytes);

  uint64_t getNextSeqNo() { return NextSeqNo++; }
  void releaseSeqNo(uint64_t SeqNo) {}

  using PendingCallWrapperResultsMap =
    DenseMap<uint64_t, IncomingWFRHandler>;

  std::mutex SimpleRemoteEPCMutex;
  std::condition_variable DisconnectCV;
  bool Disconnected = false;
  Error DisconnectErr = Error::success();

  std::unique_ptr<SimpleRemoteEPCTransport> T;
  std::unique_ptr<jitlink::JITLinkMemoryManager> OwnedMemMgr;
  std::unique_ptr<MemoryAccess> OwnedMemAccess;

  std::unique_ptr<EPCGenericDylibManager> DylibMgr;
  ExecutorAddr RunAsMainAddr;
  ExecutorAddr RunAsVoidFunctionAddr;
  ExecutorAddr RunAsIntFunctionAddr;

  uint64_t NextSeqNo = 0;
  PendingCallWrapperResultsMap PendingCallWrapperResults;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SIMPLEREMOTEEPC_H
