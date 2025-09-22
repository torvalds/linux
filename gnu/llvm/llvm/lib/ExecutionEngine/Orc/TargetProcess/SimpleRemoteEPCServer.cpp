//===------- SimpleEPCServer.cpp - EPC over simple abstract channel -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/TargetProcess/SimpleRemoteEPCServer.h"

#include "llvm/ExecutionEngine/Orc/Shared/OrcRTBridge.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/RegisterEHFrames.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Process.h"
#include "llvm/TargetParser/Host.h"

#include "OrcRTBootstrap.h"

#define DEBUG_TYPE "orc"

using namespace llvm::orc::shared;

namespace llvm {
namespace orc {

ExecutorBootstrapService::~ExecutorBootstrapService() = default;

SimpleRemoteEPCServer::Dispatcher::~Dispatcher() = default;

#if LLVM_ENABLE_THREADS
void SimpleRemoteEPCServer::ThreadDispatcher::dispatch(
    unique_function<void()> Work) {
  {
    std::lock_guard<std::mutex> Lock(DispatchMutex);
    if (!Running)
      return;
    ++Outstanding;
  }

  std::thread([this, Work = std::move(Work)]() mutable {
    Work();
    std::lock_guard<std::mutex> Lock(DispatchMutex);
    --Outstanding;
    OutstandingCV.notify_all();
  }).detach();
}

void SimpleRemoteEPCServer::ThreadDispatcher::shutdown() {
  std::unique_lock<std::mutex> Lock(DispatchMutex);
  Running = false;
  OutstandingCV.wait(Lock, [this]() { return Outstanding == 0; });
}
#endif

StringMap<ExecutorAddr> SimpleRemoteEPCServer::defaultBootstrapSymbols() {
  StringMap<ExecutorAddr> DBS;
  rt_bootstrap::addTo(DBS);
  return DBS;
}

Expected<SimpleRemoteEPCTransportClient::HandleMessageAction>
SimpleRemoteEPCServer::handleMessage(SimpleRemoteEPCOpcode OpC, uint64_t SeqNo,
                                     ExecutorAddr TagAddr,
                                     SimpleRemoteEPCArgBytesVector ArgBytes) {

  LLVM_DEBUG({
    dbgs() << "SimpleRemoteEPCServer::handleMessage: opc = ";
    switch (OpC) {
    case SimpleRemoteEPCOpcode::Setup:
      dbgs() << "Setup";
      assert(SeqNo == 0 && "Non-zero SeqNo for Setup?");
      assert(!TagAddr && "Non-zero TagAddr for Setup?");
      break;
    case SimpleRemoteEPCOpcode::Hangup:
      dbgs() << "Hangup";
      assert(SeqNo == 0 && "Non-zero SeqNo for Hangup?");
      assert(!TagAddr && "Non-zero TagAddr for Hangup?");
      break;
    case SimpleRemoteEPCOpcode::Result:
      dbgs() << "Result";
      assert(!TagAddr && "Non-zero TagAddr for Result?");
      break;
    case SimpleRemoteEPCOpcode::CallWrapper:
      dbgs() << "CallWrapper";
      break;
    }
    dbgs() << ", seqno = " << SeqNo << ", tag-addr = " << TagAddr
           << ", arg-buffer = " << formatv("{0:x}", ArgBytes.size())
           << " bytes\n";
  });

  using UT = std::underlying_type_t<SimpleRemoteEPCOpcode>;
  if (static_cast<UT>(OpC) > static_cast<UT>(SimpleRemoteEPCOpcode::LastOpC))
    return make_error<StringError>("Unexpected opcode",
                                   inconvertibleErrorCode());

  // TODO: Clean detach message?
  switch (OpC) {
  case SimpleRemoteEPCOpcode::Setup:
    return make_error<StringError>("Unexpected Setup opcode",
                                   inconvertibleErrorCode());
  case SimpleRemoteEPCOpcode::Hangup:
    return SimpleRemoteEPCTransportClient::EndSession;
  case SimpleRemoteEPCOpcode::Result:
    if (auto Err = handleResult(SeqNo, TagAddr, std::move(ArgBytes)))
      return std::move(Err);
    break;
  case SimpleRemoteEPCOpcode::CallWrapper:
    handleCallWrapper(SeqNo, TagAddr, std::move(ArgBytes));
    break;
  }
  return ContinueSession;
}

Error SimpleRemoteEPCServer::waitForDisconnect() {
  std::unique_lock<std::mutex> Lock(ServerStateMutex);
  ShutdownCV.wait(Lock, [this]() { return RunState == ServerShutDown; });
  return std::move(ShutdownErr);
}

void SimpleRemoteEPCServer::handleDisconnect(Error Err) {
  PendingJITDispatchResultsMap TmpPending;

  {
    std::lock_guard<std::mutex> Lock(ServerStateMutex);
    std::swap(TmpPending, PendingJITDispatchResults);
    RunState = ServerShuttingDown;
  }

  // Send out-of-band errors to any waiting threads.
  for (auto &KV : TmpPending)
    KV.second->set_value(
        shared::WrapperFunctionResult::createOutOfBandError("disconnecting"));

  // Wait for dispatcher to clear.
  D->shutdown();

  // Shut down services.
  while (!Services.empty()) {
    ShutdownErr =
      joinErrors(std::move(ShutdownErr), Services.back()->shutdown());
    Services.pop_back();
  }

  std::lock_guard<std::mutex> Lock(ServerStateMutex);
  ShutdownErr = joinErrors(std::move(ShutdownErr), std::move(Err));
  RunState = ServerShutDown;
  ShutdownCV.notify_all();
}

Error SimpleRemoteEPCServer::sendMessage(SimpleRemoteEPCOpcode OpC,
                                         uint64_t SeqNo, ExecutorAddr TagAddr,
                                         ArrayRef<char> ArgBytes) {

  LLVM_DEBUG({
    dbgs() << "SimpleRemoteEPCServer::sendMessage: opc = ";
    switch (OpC) {
    case SimpleRemoteEPCOpcode::Setup:
      dbgs() << "Setup";
      assert(SeqNo == 0 && "Non-zero SeqNo for Setup?");
      assert(!TagAddr && "Non-zero TagAddr for Setup?");
      break;
    case SimpleRemoteEPCOpcode::Hangup:
      dbgs() << "Hangup";
      assert(SeqNo == 0 && "Non-zero SeqNo for Hangup?");
      assert(!TagAddr && "Non-zero TagAddr for Hangup?");
      break;
    case SimpleRemoteEPCOpcode::Result:
      dbgs() << "Result";
      assert(!TagAddr && "Non-zero TagAddr for Result?");
      break;
    case SimpleRemoteEPCOpcode::CallWrapper:
      dbgs() << "CallWrapper";
      break;
    }
    dbgs() << ", seqno = " << SeqNo << ", tag-addr = " << TagAddr
           << ", arg-buffer = " << formatv("{0:x}", ArgBytes.size())
           << " bytes\n";
  });
  auto Err = T->sendMessage(OpC, SeqNo, TagAddr, ArgBytes);
  LLVM_DEBUG({
    if (Err)
      dbgs() << "  \\--> SimpleRemoteEPC::sendMessage failed\n";
  });
  return Err;
}

Error SimpleRemoteEPCServer::sendSetupMessage(
    StringMap<std::vector<char>> BootstrapMap,
    StringMap<ExecutorAddr> BootstrapSymbols) {

  using namespace SimpleRemoteEPCDefaultBootstrapSymbolNames;

  std::vector<char> SetupPacket;
  SimpleRemoteEPCExecutorInfo EI;
  EI.TargetTriple = sys::getProcessTriple();
  if (auto PageSize = sys::Process::getPageSize())
    EI.PageSize = *PageSize;
  else
    return PageSize.takeError();
  EI.BootstrapMap = std::move(BootstrapMap);
  EI.BootstrapSymbols = std::move(BootstrapSymbols);

  assert(!EI.BootstrapSymbols.count(ExecutorSessionObjectName) &&
         "Dispatch context name should not be set");
  assert(!EI.BootstrapSymbols.count(DispatchFnName) &&
         "Dispatch function name should not be set");
  EI.BootstrapSymbols[ExecutorSessionObjectName] = ExecutorAddr::fromPtr(this);
  EI.BootstrapSymbols[DispatchFnName] = ExecutorAddr::fromPtr(jitDispatchEntry);
  EI.BootstrapSymbols[rt::RegisterEHFrameSectionWrapperName] =
      ExecutorAddr::fromPtr(&llvm_orc_registerEHFrameSectionWrapper);
  EI.BootstrapSymbols[rt::DeregisterEHFrameSectionWrapperName] =
      ExecutorAddr::fromPtr(&llvm_orc_deregisterEHFrameSectionWrapper);

  using SPSSerialize =
      shared::SPSArgList<shared::SPSSimpleRemoteEPCExecutorInfo>;
  auto SetupPacketBytes =
      shared::WrapperFunctionResult::allocate(SPSSerialize::size(EI));
  shared::SPSOutputBuffer OB(SetupPacketBytes.data(), SetupPacketBytes.size());
  if (!SPSSerialize::serialize(OB, EI))
    return make_error<StringError>("Could not send setup packet",
                                   inconvertibleErrorCode());

  return sendMessage(SimpleRemoteEPCOpcode::Setup, 0, ExecutorAddr(),
                     {SetupPacketBytes.data(), SetupPacketBytes.size()});
}

Error SimpleRemoteEPCServer::handleResult(
    uint64_t SeqNo, ExecutorAddr TagAddr,
    SimpleRemoteEPCArgBytesVector ArgBytes) {
  std::promise<shared::WrapperFunctionResult> *P = nullptr;
  {
    std::lock_guard<std::mutex> Lock(ServerStateMutex);
    auto I = PendingJITDispatchResults.find(SeqNo);
    if (I == PendingJITDispatchResults.end())
      return make_error<StringError>("No call for sequence number " +
                                         Twine(SeqNo),
                                     inconvertibleErrorCode());
    P = I->second;
    PendingJITDispatchResults.erase(I);
    releaseSeqNo(SeqNo);
  }
  auto R = shared::WrapperFunctionResult::allocate(ArgBytes.size());
  memcpy(R.data(), ArgBytes.data(), ArgBytes.size());
  P->set_value(std::move(R));
  return Error::success();
}

void SimpleRemoteEPCServer::handleCallWrapper(
    uint64_t RemoteSeqNo, ExecutorAddr TagAddr,
    SimpleRemoteEPCArgBytesVector ArgBytes) {
  D->dispatch([this, RemoteSeqNo, TagAddr, ArgBytes = std::move(ArgBytes)]() {
    using WrapperFnTy =
        shared::CWrapperFunctionResult (*)(const char *, size_t);
    auto *Fn = TagAddr.toPtr<WrapperFnTy>();
    shared::WrapperFunctionResult ResultBytes(
        Fn(ArgBytes.data(), ArgBytes.size()));
    if (auto Err = sendMessage(SimpleRemoteEPCOpcode::Result, RemoteSeqNo,
                               ExecutorAddr(),
                               {ResultBytes.data(), ResultBytes.size()}))
      ReportError(std::move(Err));
  });
}

shared::WrapperFunctionResult
SimpleRemoteEPCServer::doJITDispatch(const void *FnTag, const char *ArgData,
                                     size_t ArgSize) {
  uint64_t SeqNo;
  std::promise<shared::WrapperFunctionResult> ResultP;
  auto ResultF = ResultP.get_future();
  {
    std::lock_guard<std::mutex> Lock(ServerStateMutex);
    if (RunState != ServerRunning)
      return shared::WrapperFunctionResult::createOutOfBandError(
          "jit_dispatch not available (EPC server shut down)");

    SeqNo = getNextSeqNo();
    assert(!PendingJITDispatchResults.count(SeqNo) && "SeqNo already in use");
    PendingJITDispatchResults[SeqNo] = &ResultP;
  }

  if (auto Err = sendMessage(SimpleRemoteEPCOpcode::CallWrapper, SeqNo,
                             ExecutorAddr::fromPtr(FnTag), {ArgData, ArgSize}))
    ReportError(std::move(Err));

  return ResultF.get();
}

shared::CWrapperFunctionResult
SimpleRemoteEPCServer::jitDispatchEntry(void *DispatchCtx, const void *FnTag,
                                        const char *ArgData, size_t ArgSize) {
  return reinterpret_cast<SimpleRemoteEPCServer *>(DispatchCtx)
      ->doJITDispatch(FnTag, ArgData, ArgSize)
      .release();
}

} // end namespace orc
} // end namespace llvm
