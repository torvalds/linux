//===----------- ChildTarget.cpp - Out-of-proc executor for lli -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Simple out-of-process executor for lli.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderGDB.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/RegisterEHFrames.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/SimpleExecutorMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/SimpleRemoteEPCServer.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <sstream>

using namespace llvm;
using namespace llvm::orc;

ExitOnError ExitOnErr;

int main(int argc, char *argv[]) {
#if LLVM_ENABLE_THREADS

  if (argc != 3) {
    errs() << "Usage: " << argv[0] << " <input fd> <output fd>\n";
    return 1;
  }

  if (sys::DynamicLibrary::LoadLibraryPermanently(nullptr)) {
    errs() << "Error loading program symbols.\n";
    return 1;
  }

  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  int InFD = 0;
  int OutFD = 0;
  {
    std::istringstream InFDStream(argv[1]), OutFDStream(argv[2]);
    InFDStream >> InFD;
    OutFDStream >> OutFD;
  }

  auto Server =
      ExitOnErr(SimpleRemoteEPCServer::Create<FDSimpleRemoteEPCTransport>(
          [](SimpleRemoteEPCServer::Setup &S) -> Error {
            S.setDispatcher(
                std::make_unique<SimpleRemoteEPCServer::ThreadDispatcher>());
            S.bootstrapSymbols() =
                SimpleRemoteEPCServer::defaultBootstrapSymbols();
            S.services().push_back(
                std::make_unique<rt_bootstrap::SimpleExecutorMemoryManager>());
            return Error::success();
          },
          InFD, OutFD));

  ExitOnErr(Server->waitForDisconnect());

  return 0;

#else
  errs() << argv[0]
         << " error: this tool requires threads, but LLVM was "
            "built with LLVM_ENABLE_THREADS=Off\n";
  return 1;
#endif
}
