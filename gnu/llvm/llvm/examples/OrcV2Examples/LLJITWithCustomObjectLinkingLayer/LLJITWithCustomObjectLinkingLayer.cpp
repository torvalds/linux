//===--------------- LLJITWithCustomObjectLinkingLayer.cpp ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file shows how to switch LLJIT to use a custom object linking layer (we
// use ObjectLinkingLayer, which is backed by JITLink, as an example).
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringMap.h"
#include "llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include "../ExampleModules.h"

using namespace llvm;
using namespace llvm::orc;

ExitOnError ExitOnErr;

int main(int argc, char *argv[]) {
  // Initialize LLVM.
  InitLLVM X(argc, argv);

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  cl::ParseCommandLineOptions(argc, argv, "LLJITWithCustomObjectLinkingLayer");
  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  // Detect the host and set code model to small.
  auto JTMB = ExitOnErr(JITTargetMachineBuilder::detectHost());
  JTMB.setCodeModel(CodeModel::Small);

  // Create an LLJIT instance with an ObjectLinkingLayer as the base layer.
  auto J = ExitOnErr(
      LLJITBuilder()
          .setJITTargetMachineBuilder(std::move(JTMB))
          .setObjectLinkingLayerCreator(
              [&](ExecutionSession &ES, const Triple &TT) {
                return std::make_unique<ObjectLinkingLayer>(
                    ES, ExitOnErr(jitlink::InProcessMemoryManager::Create()));
              })
          .create());

  auto M = ExitOnErr(parseExampleModule(Add1Example, "add1"));

  ExitOnErr(J->addIRModule(std::move(M)));

  // Look up the JIT'd function, cast it to a function pointer, then call it.
  auto Add1Addr = ExitOnErr(J->lookup("add1"));
  int (*Add1)(int) = Add1Addr.toPtr<int(int)>();

  int Result = Add1(42);
  outs() << "add1(42) = " << Result << "\n";

  return 0;
}
