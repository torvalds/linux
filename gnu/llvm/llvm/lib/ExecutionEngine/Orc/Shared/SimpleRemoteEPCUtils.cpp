//===------ SimpleRemoteEPCUtils.cpp - Utils for Simple Remote EPC --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Message definitions and other utilities for SimpleRemoteEPC and
// SimpleRemoteEPCServer.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Shared/SimpleRemoteEPCUtils.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FormatVariadic.h"

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

namespace {

struct FDMsgHeader {
  static constexpr unsigned MsgSizeOffset = 0;
  static constexpr unsigned OpCOffset = MsgSizeOffset + sizeof(uint64_t);
  static constexpr unsigned SeqNoOffset = OpCOffset + sizeof(uint64_t);
  static constexpr unsigned TagAddrOffset = SeqNoOffset + sizeof(uint64_t);
  static constexpr unsigned Size = TagAddrOffset + sizeof(uint64_t);
};

} // namespace

namespace llvm {
namespace orc {
namespace SimpleRemoteEPCDefaultBootstrapSymbolNames {

const char *ExecutorSessionObjectName =
    "__llvm_orc_SimpleRemoteEPC_dispatch_ctx";
const char *DispatchFnName = "__llvm_orc_SimpleRemoteEPC_dispatch_fn";

} // end namespace SimpleRemoteEPCDefaultBootstrapSymbolNames

SimpleRemoteEPCTransportClient::~SimpleRemoteEPCTransportClient() = default;
SimpleRemoteEPCTransport::~SimpleRemoteEPCTransport() = default;

Expected<std::unique_ptr<FDSimpleRemoteEPCTransport>>
FDSimpleRemoteEPCTransport::Create(SimpleRemoteEPCTransportClient &C, int InFD,
                                   int OutFD) {
#if LLVM_ENABLE_THREADS
  if (InFD == -1)
    return make_error<StringError>("Invalid input file descriptor " +
                                       Twine(InFD),
                                   inconvertibleErrorCode());
  if (OutFD == -1)
    return make_error<StringError>("Invalid output file descriptor " +
                                       Twine(OutFD),
                                   inconvertibleErrorCode());
  std::unique_ptr<FDSimpleRemoteEPCTransport> FDT(
      new FDSimpleRemoteEPCTransport(C, InFD, OutFD));
  return std::move(FDT);
#else
  return make_error<StringError>("FD-based SimpleRemoteEPC transport requires "
                                 "thread support, but llvm was built with "
                                 "LLVM_ENABLE_THREADS=Off",
                                 inconvertibleErrorCode());
#endif
}

FDSimpleRemoteEPCTransport::~FDSimpleRemoteEPCTransport() {
#if LLVM_ENABLE_THREADS
  ListenerThread.join();
#endif
}

Error FDSimpleRemoteEPCTransport::start() {
#if LLVM_ENABLE_THREADS
  ListenerThread = std::thread([this]() { listenLoop(); });
  return Error::success();
#endif
  llvm_unreachable("Should not be called with LLVM_ENABLE_THREADS=Off");
}

Error FDSimpleRemoteEPCTransport::sendMessage(SimpleRemoteEPCOpcode OpC,
                                              uint64_t SeqNo,
                                              ExecutorAddr TagAddr,
                                              ArrayRef<char> ArgBytes) {
  char HeaderBuffer[FDMsgHeader::Size];

  *((support::ulittle64_t *)(HeaderBuffer + FDMsgHeader::MsgSizeOffset)) =
      FDMsgHeader::Size + ArgBytes.size();
  *((support::ulittle64_t *)(HeaderBuffer + FDMsgHeader::OpCOffset)) =
      static_cast<uint64_t>(OpC);
  *((support::ulittle64_t *)(HeaderBuffer + FDMsgHeader::SeqNoOffset)) = SeqNo;
  *((support::ulittle64_t *)(HeaderBuffer + FDMsgHeader::TagAddrOffset)) =
      TagAddr.getValue();

  std::lock_guard<std::mutex> Lock(M);
  if (Disconnected)
    return make_error<StringError>("FD-transport disconnected",
                                   inconvertibleErrorCode());
  if (int ErrNo = writeBytes(HeaderBuffer, FDMsgHeader::Size))
    return errorCodeToError(std::error_code(ErrNo, std::generic_category()));
  if (int ErrNo = writeBytes(ArgBytes.data(), ArgBytes.size()))
    return errorCodeToError(std::error_code(ErrNo, std::generic_category()));
  return Error::success();
}

void FDSimpleRemoteEPCTransport::disconnect() {
  if (Disconnected)
    return; // Return if already disconnected.

  Disconnected = true;
  bool CloseOutFD = InFD != OutFD;

  // Close InFD.
  while (close(InFD) == -1) {
    if (errno == EBADF)
      break;
  }

  // Close OutFD.
  if (CloseOutFD) {
    while (close(OutFD) == -1) {
      if (errno == EBADF)
        break;
    }
  }
}

static Error makeUnexpectedEOFError() {
  return make_error<StringError>("Unexpected end-of-file",
                                 inconvertibleErrorCode());
}

Error FDSimpleRemoteEPCTransport::readBytes(char *Dst, size_t Size,
                                            bool *IsEOF) {
  assert((Size == 0 || Dst) && "Attempt to read into null.");
  ssize_t Completed = 0;
  while (Completed < static_cast<ssize_t>(Size)) {
    ssize_t Read = ::read(InFD, Dst + Completed, Size - Completed);
    if (Read <= 0) {
      auto ErrNo = errno;
      if (Read == 0) {
        if (Completed == 0 && IsEOF) {
          *IsEOF = true;
          return Error::success();
        } else
          return makeUnexpectedEOFError();
      } else if (ErrNo == EAGAIN || ErrNo == EINTR)
        continue;
      else {
        std::lock_guard<std::mutex> Lock(M);
        if (Disconnected && IsEOF) { // disconnect called,  pretend this is EOF.
          *IsEOF = true;
          return Error::success();
        }
        return errorCodeToError(
            std::error_code(ErrNo, std::generic_category()));
      }
    }
    Completed += Read;
  }
  return Error::success();
}

int FDSimpleRemoteEPCTransport::writeBytes(const char *Src, size_t Size) {
  assert((Size == 0 || Src) && "Attempt to append from null.");
  ssize_t Completed = 0;
  while (Completed < static_cast<ssize_t>(Size)) {
    ssize_t Written = ::write(OutFD, Src + Completed, Size - Completed);
    if (Written < 0) {
      auto ErrNo = errno;
      if (ErrNo == EAGAIN || ErrNo == EINTR)
        continue;
      else
        return ErrNo;
    }
    Completed += Written;
  }
  return 0;
}

void FDSimpleRemoteEPCTransport::listenLoop() {
  Error Err = Error::success();
  do {

    char HeaderBuffer[FDMsgHeader::Size];
    // Read the header buffer.
    {
      bool IsEOF = false;
      if (auto Err2 = readBytes(HeaderBuffer, FDMsgHeader::Size, &IsEOF)) {
        Err = joinErrors(std::move(Err), std::move(Err2));
        break;
      }
      if (IsEOF)
        break;
    }

    // Decode header buffer.
    uint64_t MsgSize;
    SimpleRemoteEPCOpcode OpC;
    uint64_t SeqNo;
    ExecutorAddr TagAddr;

    MsgSize =
        *((support::ulittle64_t *)(HeaderBuffer + FDMsgHeader::MsgSizeOffset));
    OpC = static_cast<SimpleRemoteEPCOpcode>(static_cast<uint64_t>(
        *((support::ulittle64_t *)(HeaderBuffer + FDMsgHeader::OpCOffset))));
    SeqNo =
        *((support::ulittle64_t *)(HeaderBuffer + FDMsgHeader::SeqNoOffset));
    TagAddr.setValue(
        *((support::ulittle64_t *)(HeaderBuffer + FDMsgHeader::TagAddrOffset)));

    if (MsgSize < FDMsgHeader::Size) {
      Err = joinErrors(std::move(Err),
                       make_error<StringError>("Message size too small",
                                               inconvertibleErrorCode()));
      break;
    }

    // Read the argument bytes.
    SimpleRemoteEPCArgBytesVector ArgBytes;
    ArgBytes.resize(MsgSize - FDMsgHeader::Size);
    if (auto Err2 = readBytes(ArgBytes.data(), ArgBytes.size())) {
      Err = joinErrors(std::move(Err), std::move(Err2));
      break;
    }

    if (auto Action = C.handleMessage(OpC, SeqNo, TagAddr, ArgBytes)) {
      if (*Action == SimpleRemoteEPCTransportClient::EndSession)
        break;
    } else {
      Err = joinErrors(std::move(Err), Action.takeError());
      break;
    }
  } while (true);

  // Attempt to close FDs, set Disconnected to true so that subsequent
  // sendMessage calls fail.
  disconnect();

  // Call up to the client to handle the disconnection.
  C.handleDisconnect(std::move(Err));
}

} // end namespace orc
} // end namespace llvm
