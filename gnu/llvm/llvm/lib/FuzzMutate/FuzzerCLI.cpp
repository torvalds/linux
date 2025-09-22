//===-- FuzzerCLI.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/FuzzMutate/FuzzerCLI.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

void llvm::parseFuzzerCLOpts(int ArgC, char *ArgV[]) {
  std::vector<const char *> CLArgs;
  CLArgs.push_back(ArgV[0]);

  int I = 1;
  while (I < ArgC)
    if (StringRef(ArgV[I++]) == "-ignore_remaining_args=1")
      break;
  while (I < ArgC)
    CLArgs.push_back(ArgV[I++]);

  cl::ParseCommandLineOptions(CLArgs.size(), CLArgs.data());
}

void llvm::handleExecNameEncodedBEOpts(StringRef ExecName) {
  std::vector<std::string> Args{std::string(ExecName)};

  auto NameAndArgs = ExecName.split("--");
  if (NameAndArgs.second.empty())
    return;

  SmallVector<StringRef, 4> Opts;
  NameAndArgs.second.split(Opts, '-');
  for (StringRef Opt : Opts) {
    if (Opt == "gisel") {
      Args.push_back("-global-isel");
      // For now we default GlobalISel to -O0
      Args.push_back("-O0");
    } else if (Opt.starts_with("O")) {
      Args.push_back("-" + Opt.str());
    } else if (Triple(Opt).getArch()) {
      Args.push_back("-mtriple=" + Opt.str());
    } else {
      errs() << ExecName << ": Unknown option: " << Opt << ".\n";
      exit(1);
    }
  }
  errs() << NameAndArgs.first << ": Injected args:";
  for (int I = 1, E = Args.size(); I < E; ++I)
    errs() << " " << Args[I];
  errs() << "\n";

  std::vector<const char *> CLArgs;
  CLArgs.reserve(Args.size());
  for (std::string &S : Args)
    CLArgs.push_back(S.c_str());

  cl::ParseCommandLineOptions(CLArgs.size(), CLArgs.data());
}

void llvm::handleExecNameEncodedOptimizerOpts(StringRef ExecName) {
  // TODO: Refactor parts common with the 'handleExecNameEncodedBEOpts'
  std::vector<std::string> Args{std::string(ExecName)};

  auto NameAndArgs = ExecName.split("--");
  if (NameAndArgs.second.empty())
    return;

  SmallVector<StringRef, 4> Opts;
  NameAndArgs.second.split(Opts, '-');
  for (StringRef Opt : Opts) {
    if (Opt == "instcombine") {
      Args.push_back("-passes=instcombine");
    } else if (Opt == "earlycse") {
      Args.push_back("-passes=early-cse");
    } else if (Opt == "simplifycfg") {
      Args.push_back("-passes=simplifycfg");
    } else if (Opt == "gvn") {
      Args.push_back("-passes=gvn");
    } else if (Opt == "sccp") {
      Args.push_back("-passes=sccp");
    } else if (Opt == "loop_predication") {
      Args.push_back("-passes=loop-predication");
    } else if (Opt == "guard_widening") {
      Args.push_back("-passes=guard-widening");
    } else if (Opt == "loop_rotate") {
      Args.push_back("-passes=loop-rotate");
    } else if (Opt == "loop_unswitch") {
      Args.push_back("-passes=loop(simple-loop-unswitch)");
    } else if (Opt == "loop_unroll") {
      Args.push_back("-passes=unroll");
    } else if (Opt == "loop_vectorize") {
      Args.push_back("-passes=loop-vectorize");
    } else if (Opt == "licm") {
      Args.push_back("-passes=licm");
    } else if (Opt == "indvars") {
      Args.push_back("-passes=indvars");
    } else if (Opt == "strength_reduce") {
      Args.push_back("-passes=loop-reduce");
    } else if (Opt == "irce") {
      Args.push_back("-passes=irce");
    } else if (Opt == "dse") {
      Args.push_back("-passes=dse");
    } else if (Opt == "loop_idiom") {
      Args.push_back("-passes=loop-idiom");
    } else if (Opt == "reassociate") {
      Args.push_back("-passes=reassociate");
    } else if (Opt == "lower_matrix_intrinsics") {
      Args.push_back("-passes=lower-matrix-intrinsics");
    } else if (Opt == "memcpyopt") {
      Args.push_back("-passes=memcpyopt");
    } else if (Opt == "sroa") {
      Args.push_back("-passes=sroa");
    } else if (Triple(Opt).getArch()) {
      Args.push_back("-mtriple=" + Opt.str());
    } else {
      errs() << ExecName << ": Unknown option: " << Opt << ".\n";
      exit(1);
    }
  }

  errs() << NameAndArgs.first << ": Injected args:";
  for (int I = 1, E = Args.size(); I < E; ++I)
    errs() << " " << Args[I];
  errs() << "\n";

  std::vector<const char *> CLArgs;
  CLArgs.reserve(Args.size());
  for (std::string &S : Args)
    CLArgs.push_back(S.c_str());

  cl::ParseCommandLineOptions(CLArgs.size(), CLArgs.data());
}

int llvm::runFuzzerOnInputs(int ArgC, char *ArgV[], FuzzerTestFun TestOne,
                            FuzzerInitFun Init) {
  errs() << "*** This tool was not linked to libFuzzer.\n"
         << "*** No fuzzing will be performed.\n";
  if (int RC = Init(&ArgC, &ArgV)) {
    errs() << "Initialization failed\n";
    return RC;
  }

  for (int I = 1; I < ArgC; ++I) {
    StringRef Arg(ArgV[I]);
    if (Arg.starts_with("-")) {
      if (Arg == "-ignore_remaining_args=1")
        break;
      continue;
    }

    auto BufOrErr = MemoryBuffer::getFile(Arg, /*IsText=*/false,
                                          /*RequiresNullTerminator=*/false);
    if (std::error_code EC = BufOrErr.getError()) {
      errs() << "Error reading file: " << Arg << ": " << EC.message() << "\n";
      return 1;
    }
    std::unique_ptr<MemoryBuffer> Buf = std::move(BufOrErr.get());
    errs() << "Running: " << Arg << " (" << Buf->getBufferSize() << " bytes)\n";
    TestOne(reinterpret_cast<const uint8_t *>(Buf->getBufferStart()),
            Buf->getBufferSize());
  }
  return 0;
}
