//===--- LLJITWithObjectCache.cpp - An LLJIT example with an ObjectCache --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringMap.h"
#include "llvm/ExecutionEngine/ObjectCache.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include "../ExampleModules.h"

using namespace llvm;
using namespace llvm::orc;

ExitOnError ExitOnErr;

class MyObjectCache : public ObjectCache {
public:
  void notifyObjectCompiled(const Module *M,
                            MemoryBufferRef ObjBuffer) override {
    CachedObjects[M->getModuleIdentifier()] = MemoryBuffer::getMemBufferCopy(
        ObjBuffer.getBuffer(), ObjBuffer.getBufferIdentifier());
  }

  std::unique_ptr<MemoryBuffer> getObject(const Module *M) override {
    auto I = CachedObjects.find(M->getModuleIdentifier());
    if (I == CachedObjects.end()) {
      dbgs() << "No object for " << M->getModuleIdentifier()
             << " in cache. Compiling.\n";
      return nullptr;
    }

    dbgs() << "Object for " << M->getModuleIdentifier()
           << " loaded from cache.\n";
    return MemoryBuffer::getMemBuffer(I->second->getMemBufferRef());
  }

private:
  StringMap<std::unique_ptr<MemoryBuffer>> CachedObjects;
};

void runJITWithCache(ObjectCache &ObjCache) {

  // Create an LLJIT instance with a custom IRCompiler.
  auto J = ExitOnErr(
      LLJITBuilder()
          .setCompileFunctionCreator(
              [&](JITTargetMachineBuilder JTMB)
                  -> Expected<std::unique_ptr<IRCompileLayer::IRCompiler>> {
                auto TM = JTMB.createTargetMachine();
                if (!TM)
                  return TM.takeError();
                return std::make_unique<TMOwningSimpleCompiler>(std::move(*TM),
                                                                &ObjCache);
              })
          .create());

  auto M = ExitOnErr(parseExampleModule(Add1Example, "add1"));

  ExitOnErr(J->addIRModule(std::move(M)));

  // Look up the JIT'd function, cast it to a function pointer, then call it.
  auto Add1Addr = ExitOnErr(J->lookup("add1"));
  int (*Add1)(int) = Add1Addr.toPtr<int(int)>();

  int Result = Add1(42);
  outs() << "add1(42) = " << Result << "\n";
}

int main(int argc, char *argv[]) {
  // Initialize LLVM.
  InitLLVM X(argc, argv);

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  cl::ParseCommandLineOptions(argc, argv, "LLJITWithObjectCache");
  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  MyObjectCache MyCache;

  runJITWithCache(MyCache);
  runJITWithCache(MyCache);

  return 0;
}
