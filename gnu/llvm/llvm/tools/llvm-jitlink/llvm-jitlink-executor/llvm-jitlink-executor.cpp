//===- llvm-jitlink-executor.cpp - Out-of-proc executor for llvm-jitlink -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Simple out-of-process executor for llvm-jitlink.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/ExecutorSharedMemoryMapperService.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderGDB.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/RegisterEHFrames.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/SimpleExecutorMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/SimpleRemoteEPCServer.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <sstream>

#ifdef LLVM_ON_UNIX

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#endif

using namespace llvm;
using namespace llvm::orc;

ExitOnError ExitOnErr;

LLVM_ATTRIBUTE_USED void linkComponents() {
  errs() << (void *)&llvm_orc_registerEHFrameSectionWrapper
         << (void *)&llvm_orc_deregisterEHFrameSectionWrapper
         << (void *)&llvm_orc_registerJITLoaderGDBWrapper
         << (void *)&llvm_orc_registerJITLoaderGDBAllocAction;
}

void printErrorAndExit(Twine ErrMsg) {
#ifndef NDEBUG
  const char *DebugOption = "[debug] ";
#else
  const char *DebugOption = "";
#endif

  errs() << "error: " << ErrMsg.str() << "\n\n"
         << "Usage:\n"
         << "  llvm-jitlink-executor " << DebugOption
         << "[test-jitloadergdb] filedescs=<infd>,<outfd> [args...]\n"
         << "  llvm-jitlink-executor " << DebugOption
         << "[test-jitloadergdb] listen=<host>:<port> [args...]\n";
  exit(1);
}

int openListener(std::string Host, std::string PortStr) {
#ifndef LLVM_ON_UNIX
  // FIXME: Add TCP support for Windows.
  printErrorAndExit("listen option not supported");
  return 0;
#else
  addrinfo Hints{};
  Hints.ai_family = AF_INET;
  Hints.ai_socktype = SOCK_STREAM;
  Hints.ai_flags = AI_PASSIVE;

  addrinfo *AI;
  if (int EC = getaddrinfo(nullptr, PortStr.c_str(), &Hints, &AI)) {
    errs() << "Error setting up bind address: " << gai_strerror(EC) << "\n";
    exit(1);
  }

  // Create a socket from first addrinfo structure returned by getaddrinfo.
  int SockFD;
  if ((SockFD = socket(AI->ai_family, AI->ai_socktype, AI->ai_protocol)) < 0) {
    errs() << "Error creating socket: " << std::strerror(errno) << "\n";
    exit(1);
  }

  // Avoid "Address already in use" errors.
  const int Yes = 1;
  if (setsockopt(SockFD, SOL_SOCKET, SO_REUSEADDR, &Yes, sizeof(int)) == -1) {
    errs() << "Error calling setsockopt: " << std::strerror(errno) << "\n";
    exit(1);
  }

  // Bind the socket to the desired port.
  if (bind(SockFD, AI->ai_addr, AI->ai_addrlen) < 0) {
    errs() << "Error on binding: " << std::strerror(errno) << "\n";
    exit(1);
  }

  // Listen for incomming connections.
  static constexpr int ConnectionQueueLen = 1;
  listen(SockFD, ConnectionQueueLen);

#if defined(_AIX)
  assert(Hi_32(AI->ai_addrlen) == 0 && "Field is a size_t on 64-bit AIX");
  socklen_t AddrLen = Lo_32(AI->ai_addrlen);
  return accept(SockFD, AI->ai_addr, &AddrLen);
#else
  return accept(SockFD, AI->ai_addr, &AI->ai_addrlen);
#endif

#endif // LLVM_ON_UNIX
}

#if LLVM_ENABLE_THREADS

// JITLink debug support plugins put information about JITed code in this GDB
// JIT Interface global from OrcTargetProcess.
extern "C" struct jit_descriptor __jit_debug_descriptor;

static void *findLastDebugDescriptorEntryPtr() {
  struct jit_code_entry *Last = __jit_debug_descriptor.first_entry;
  while (Last && Last->next_entry)
    Last = Last->next_entry;
  return Last;
}

#endif

int main(int argc, char *argv[]) {
#if LLVM_ENABLE_THREADS

  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  unsigned FirstProgramArg = 1;
  int InFD = 0;
  int OutFD = 0;

  if (argc < 2)
    printErrorAndExit("insufficient arguments");

  StringRef NextArg = argv[FirstProgramArg++];
#ifndef NDEBUG
  if (NextArg == "debug") {
    DebugFlag = true;
    NextArg = argv[FirstProgramArg++];
  }
#endif

  std::vector<StringRef> TestOutputFlags;
  while (NextArg.starts_with("test-")) {
    TestOutputFlags.push_back(NextArg);
    NextArg = argv[FirstProgramArg++];
  }

  if (llvm::is_contained(TestOutputFlags, "test-jitloadergdb"))
    fprintf(stderr, "__jit_debug_descriptor.last_entry = 0x%016" PRIx64 "\n",
            pointerToJITTargetAddress(findLastDebugDescriptorEntryPtr()));

  StringRef SpecifierType, Specifier;
  std::tie(SpecifierType, Specifier) = NextArg.split('=');
  if (SpecifierType == "filedescs") {
    StringRef FD1Str, FD2Str;
    std::tie(FD1Str, FD2Str) = Specifier.split(',');
    if (FD1Str.getAsInteger(10, InFD))
      printErrorAndExit(FD1Str + " is not a valid file descriptor");
    if (FD2Str.getAsInteger(10, OutFD))
      printErrorAndExit(FD2Str + " is not a valid file descriptor");
  } else if (SpecifierType == "listen") {
    StringRef Host, PortStr;
    std::tie(Host, PortStr) = Specifier.split(':');

    int Port = 0;
    if (PortStr.getAsInteger(10, Port))
      printErrorAndExit("port number '" + PortStr + "' is not a valid integer");

    InFD = OutFD = openListener(Host.str(), PortStr.str());
  } else
    printErrorAndExit("invalid specifier type \"" + SpecifierType + "\"");

  auto Server =
      ExitOnErr(SimpleRemoteEPCServer::Create<FDSimpleRemoteEPCTransport>(
          [](SimpleRemoteEPCServer::Setup &S) -> Error {
            S.setDispatcher(
                std::make_unique<SimpleRemoteEPCServer::ThreadDispatcher>());
            S.bootstrapSymbols() =
                SimpleRemoteEPCServer::defaultBootstrapSymbols();
            S.services().push_back(
                std::make_unique<rt_bootstrap::SimpleExecutorMemoryManager>());
            S.services().push_back(
                std::make_unique<
                    rt_bootstrap::ExecutorSharedMemoryMapperService>());
            return Error::success();
          },
          InFD, OutFD));

  ExitOnErr(Server->waitForDisconnect());

  if (llvm::is_contained(TestOutputFlags, "test-jitloadergdb"))
    fprintf(stderr, "__jit_debug_descriptor.last_entry = 0x%016" PRIx64 "\n",
            pointerToJITTargetAddress(findLastDebugDescriptorEntryPtr()));

  return 0;

#else
  errs() << argv[0]
         << " error: this tool requires threads, but LLVM was "
            "built with LLVM_ENABLE_THREADS=Off\n";
  return 1;
#endif
}
