//===--- llvm-opt-fuzzer.cpp - Fuzzer for instruction selection ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tool to fuzz optimization passes using libFuzzer.
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/FuzzMutate/FuzzerCLI.h"
#include "llvm/FuzzMutate/IRMutator.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

static codegen::RegisterCodeGenFlags CGF;

static cl::opt<std::string>
    TargetTripleStr("mtriple", cl::desc("Override target triple for module"));

// Passes to run for this fuzzer instance. Expects new pass manager syntax.
static cl::opt<std::string> PassPipeline(
    "passes",
    cl::desc("A textual description of the pass pipeline for testing"));

static std::unique_ptr<IRMutator> Mutator;
static std::unique_ptr<TargetMachine> TM;

std::unique_ptr<IRMutator> createOptMutator() {
  std::vector<TypeGetter> Types{
      Type::getInt1Ty,  Type::getInt8Ty,  Type::getInt16Ty, Type::getInt32Ty,
      Type::getInt64Ty, Type::getFloatTy, Type::getDoubleTy};

  std::vector<std::unique_ptr<IRMutationStrategy>> Strategies;
  Strategies.push_back(std::make_unique<InjectorIRStrategy>(
      InjectorIRStrategy::getDefaultOps()));
  Strategies.push_back(std::make_unique<InstDeleterIRStrategy>());
  Strategies.push_back(std::make_unique<InstModificationIRStrategy>());

  return std::make_unique<IRMutator>(std::move(Types), std::move(Strategies));
}

extern "C" LLVM_ATTRIBUTE_USED size_t LLVMFuzzerCustomMutator(
    uint8_t *Data, size_t Size, size_t MaxSize, unsigned int Seed) {

  assert(Mutator &&
         "IR mutator should have been created during fuzzer initialization");

  LLVMContext Context;
  auto M = parseAndVerify(Data, Size, Context);
  if (!M) {
    errs() << "error: mutator input module is broken!\n";
    return 0;
  }

  Mutator->mutateModule(*M, Seed, MaxSize);

  if (verifyModule(*M, &errs())) {
    errs() << "mutation result doesn't pass verification\n";
#ifndef NDEBUG
    M->dump();
#endif
    // Avoid adding incorrect test cases to the corpus.
    return 0;
  }

  std::string Buf;
  {
    raw_string_ostream OS(Buf);
    WriteBitcodeToFile(*M, OS);
  }
  if (Buf.size() > MaxSize)
    return 0;

  // There are some invariants which are not checked by the verifier in favor
  // of having them checked by the parser. They may be considered as bugs in the
  // verifier and should be fixed there. However until all of those are covered
  // we want to check for them explicitly. Otherwise we will add incorrect input
  // to the corpus and this is going to confuse the fuzzer which will start
  // exploration of the bitcode reader error handling code.
  auto NewM = parseAndVerify(reinterpret_cast<const uint8_t *>(Buf.data()),
                             Buf.size(), Context);
  if (!NewM) {
    errs() << "mutator failed to re-read the module\n";
#ifndef NDEBUG
    M->dump();
#endif
    return 0;
  }

  memcpy(Data, Buf.data(), Buf.size());
  return Buf.size();
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  assert(TM && "Should have been created during fuzzer initialization");

  if (Size <= 1)
    // We get bogus data given an empty corpus - ignore it.
    return 0;

  // Parse module
  //

  LLVMContext Context;
  auto M = parseAndVerify(Data, Size, Context);
  if (!M) {
    errs() << "error: input module is broken!\n";
    return 0;
  }

  // Set up target dependant options
  //

  M->setTargetTriple(TM->getTargetTriple().normalize());
  M->setDataLayout(TM->createDataLayout());
  codegen::setFunctionAttributes(TM->getTargetCPU(),
                                 TM->getTargetFeatureString(), *M);

  // Create pass pipeline
  //

  PassBuilder PB(TM.get());

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModulePassManager MPM;
  ModuleAnalysisManager MAM;

  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  auto Err = PB.parsePassPipeline(MPM, PassPipeline);
  assert(!Err && "Should have been checked during fuzzer initialization");
  // Only fail with assert above, otherwise ignore the parsing error.
  consumeError(std::move(Err));

  // Run passes which we need to test
  //

  MPM.run(*M, MAM);

  // Check that passes resulted in a correct code
  if (verifyModule(*M, &errs())) {
    errs() << "Transformation resulted in an invalid module\n";
    abort();
  }

  return 0;
}

static void handleLLVMFatalError(void *, const char *Message, bool) {
  // TODO: Would it be better to call into the fuzzer internals directly?
  dbgs() << "LLVM ERROR: " << Message << "\n"
         << "Aborting to trigger fuzzer exit handling.\n";
  abort();
}

extern "C" LLVM_ATTRIBUTE_USED int LLVMFuzzerInitialize(int *argc,
                                                        char ***argv) {
  EnableDebugBuffering = true;

  // Make sure we print the summary and the current unit when LLVM errors out.
  install_fatal_error_handler(handleLLVMFatalError, nullptr);

  // Initialize llvm
  //

  InitializeAllTargets();
  InitializeAllTargetMCs();

  // Parse input options
  //

  handleExecNameEncodedOptimizerOpts(*argv[0]);
  parseFuzzerCLOpts(*argc, *argv);

  // Create TargetMachine
  //

  if (TargetTripleStr.empty()) {
    errs() << *argv[0] << ": -mtriple must be specified\n";
    exit(1);
  }
  ExitOnError ExitOnErr(std::string(*argv[0]) + ": error:");
  TM = ExitOnErr(codegen::createTargetMachineForTriple(
      Triple::normalize(TargetTripleStr)));

  // Check that pass pipeline is specified and correct
  //

  if (PassPipeline.empty()) {
    errs() << *argv[0] << ": at least one pass should be specified\n";
    exit(1);
  }

  PassBuilder PB(TM.get());
  ModulePassManager MPM;
  if (auto Err = PB.parsePassPipeline(MPM, PassPipeline)) {
    errs() << *argv[0] << ": " << toString(std::move(Err)) << "\n";
    exit(1);
  }

  // Create mutator
  //

  Mutator = createOptMutator();

  return 0;
}
