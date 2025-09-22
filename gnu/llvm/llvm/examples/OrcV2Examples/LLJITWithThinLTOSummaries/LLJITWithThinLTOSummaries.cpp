//===--- LLJITWithThinLTOSummaries.cpp - Module summaries as LLJIT input --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// In this example we will use a module summary index file produced for ThinLTO
// to (A) find the module that defines the main entry point and (B) find all
// extra modules that we need. We will do this in five steps:
//
// (1) Read the index file and parse the module summary index.
// (2) Find the path of the module that defines "main".
// (3) Parse the main module and create a matching LLJIT.
// (4) Add all modules to the LLJIT that are covered by the index.
// (5) Look up and run the JIT'd function.
//
// The index file name must be passed in as command line argument. Please find
// this test for instructions on creating the index file:
//
//       llvm/test/Examples/OrcV2Examples/lljit-with-thinlto-summaries.test
//
// If you use "build" as the build directory, you can run the test from the root
// of the monorepo like this:
//
// > build/bin/llvm-lit -a \
//       llvm/test/Examples/OrcV2Examples/lljit-with-thinlto-summaries.test
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/TargetExecutionUtils.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <system_error>
#include <vector>

using namespace llvm;
using namespace llvm::orc;

// Path of the module summary index file.
cl::opt<std::string> IndexFile{cl::desc("<module summary index>"),
                               cl::Positional, cl::init("-")};

// Describe a fail state that is caused by the given ModuleSummaryIndex
// providing multiple definitions of the given global value name. It will dump
// name and GUID for the global value and list the paths of the modules covered
// by the index.
class DuplicateDefinitionInSummary
    : public ErrorInfo<DuplicateDefinitionInSummary> {
public:
  static char ID;

  DuplicateDefinitionInSummary(std::string GlobalValueName, ValueInfo VI)
      : GlobalValueName(std::move(GlobalValueName)) {
    ModulePaths.reserve(VI.getSummaryList().size());
    for (const auto &S : VI.getSummaryList())
      ModulePaths.push_back(S->modulePath().str());
    llvm::sort(ModulePaths);
  }

  void log(raw_ostream &OS) const override {
    OS << "Duplicate symbol for global value '" << GlobalValueName
       << "' (GUID: " << GlobalValue::getGUID(GlobalValueName) << ") in:\n";
    for (const std::string &Path : ModulePaths) {
      OS << "    " << Path << "\n";
    }
  }

  std::error_code convertToErrorCode() const override {
    return inconvertibleErrorCode();
  }

private:
  std::string GlobalValueName;
  std::vector<std::string> ModulePaths;
};

// Describe a fail state where the given global value name was not found in the
// given ModuleSummaryIndex. It will dump name and GUID for the global value and
// list the paths of the modules covered by the index.
class DefinitionNotFoundInSummary
    : public ErrorInfo<DefinitionNotFoundInSummary> {
public:
  static char ID;

  DefinitionNotFoundInSummary(std::string GlobalValueName,
                              ModuleSummaryIndex &Index)
      : GlobalValueName(std::move(GlobalValueName)) {
    ModulePaths.reserve(Index.modulePaths().size());
    for (const auto &Entry : Index.modulePaths())
      ModulePaths.push_back(Entry.first().str());
    llvm::sort(ModulePaths);
  }

  void log(raw_ostream &OS) const override {
    OS << "No symbol for global value '" << GlobalValueName
       << "' (GUID: " << GlobalValue::getGUID(GlobalValueName) << ") in:\n";
    for (const std::string &Path : ModulePaths) {
      OS << "    " << Path << "\n";
    }
  }

  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }

private:
  std::string GlobalValueName;
  std::vector<std::string> ModulePaths;
};

char DuplicateDefinitionInSummary::ID = 0;
char DefinitionNotFoundInSummary::ID = 0;

// Lookup the a function in the ModuleSummaryIndex and return the path of the
// module that defines it. Paths in the ModuleSummaryIndex are relative to the
// build directory of the covered modules.
Expected<StringRef> getMainModulePath(StringRef FunctionName,
                                      ModuleSummaryIndex &Index) {
  // Summaries use unmangled names.
  GlobalValue::GUID G = GlobalValue::getGUID(FunctionName);
  ValueInfo VI = Index.getValueInfo(G);

  // We need a unique definition, otherwise don't try further.
  if (!VI || VI.getSummaryList().empty())
    return make_error<DefinitionNotFoundInSummary>(FunctionName.str(), Index);
  if (VI.getSummaryList().size() > 1)
    return make_error<DuplicateDefinitionInSummary>(FunctionName.str(), VI);

  GlobalValueSummary *S = VI.getSummaryList().front()->getBaseObject();
  if (!isa<FunctionSummary>(S))
    return createStringError(inconvertibleErrorCode(),
                             "Entry point is not a function: " + FunctionName);

  // Return a reference. ModuleSummaryIndex owns the module paths.
  return S->modulePath();
}

// Parse the bitcode module from the given path into a ThreadSafeModule.
Expected<ThreadSafeModule> loadModule(StringRef Path,
                                      orc::ThreadSafeContext TSCtx) {
  outs() << "About to load module: " << Path << "\n";

  Expected<std::unique_ptr<MemoryBuffer>> BitcodeBuffer =
      errorOrToExpected(MemoryBuffer::getFile(Path));
  if (!BitcodeBuffer)
    return BitcodeBuffer.takeError();

  MemoryBufferRef BitcodeBufferRef = (**BitcodeBuffer).getMemBufferRef();
  Expected<std::unique_ptr<Module>> M =
      parseBitcodeFile(BitcodeBufferRef, *TSCtx.getContext());
  if (!M)
    return M.takeError();

  return ThreadSafeModule(std::move(*M), std::move(TSCtx));
}

int main(int Argc, char *Argv[]) {
  InitLLVM X(Argc, Argv);

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  cl::ParseCommandLineOptions(Argc, Argv, "LLJITWithThinLTOSummaries");

  ExitOnError ExitOnErr;
  ExitOnErr.setBanner(std::string(Argv[0]) + ": ");

  // (1) Read the index file and parse the module summary index.
  std::unique_ptr<MemoryBuffer> SummaryBuffer =
      ExitOnErr(errorOrToExpected(MemoryBuffer::getFile(IndexFile)));

  std::unique_ptr<ModuleSummaryIndex> SummaryIndex =
      ExitOnErr(getModuleSummaryIndex(SummaryBuffer->getMemBufferRef()));

  // (2) Find the path of the module that defines "main".
  std::string MainFunctionName = "main";
  StringRef MainModulePath =
      ExitOnErr(getMainModulePath(MainFunctionName, *SummaryIndex));

  // (3) Parse the main module and create a matching LLJIT.
  ThreadSafeContext TSCtx(std::make_unique<LLVMContext>());
  ThreadSafeModule MainModule = ExitOnErr(loadModule(MainModulePath, TSCtx));

  auto Builder = LLJITBuilder();

  MainModule.withModuleDo([&](Module &M) {
    if (M.getTargetTriple().empty()) {
      Builder.setJITTargetMachineBuilder(
          ExitOnErr(JITTargetMachineBuilder::detectHost()));
    } else {
      Builder.setJITTargetMachineBuilder(
          JITTargetMachineBuilder(Triple(M.getTargetTriple())));
    }
    if (!M.getDataLayout().getStringRepresentation().empty())
      Builder.setDataLayout(M.getDataLayout());
  });

  auto J = ExitOnErr(Builder.create());

  // (4) Add all modules to the LLJIT that are covered by the index.
  JITDylib &JD = J->getMainJITDylib();

  for (const auto &Entry : SummaryIndex->modulePaths()) {
    StringRef Path = Entry.first();
    ThreadSafeModule M = (Path == MainModulePath)
                             ? std::move(MainModule)
                             : ExitOnErr(loadModule(Path, TSCtx));
    ExitOnErr(J->addIRModule(JD, std::move(M)));
  }

  // (5) Look up and run the JIT'd function.
  auto MainAddr = ExitOnErr(J->lookup(MainFunctionName));

  using MainFnPtr = int (*)(int, char *[]);
  auto *MainFunction = MainAddr.toPtr<MainFnPtr>();

  int Result = runAsMain(MainFunction, {}, MainModulePath);
  outs() << "'" << MainFunctionName << "' finished with exit code: " << Result
         << "\n";

  return 0;
}
