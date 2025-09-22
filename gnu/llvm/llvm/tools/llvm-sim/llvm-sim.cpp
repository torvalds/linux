//===-- llvm-sim.cpp - Find  similar sections of programs -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program finds similar sections of a Module, and exports them as a JSON
// file.
//
// To find similarities contained across multiple modules, please use llvm-link
// first to merge the modules.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/IRSimilarityIdentifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"

using namespace llvm;
using namespace IRSimilarity;

static cl::opt<std::string> OutputFilename("o", cl::desc("Output Filename"),
                                           cl::init("-"),
                                           cl::value_desc("filename"));

static cl::opt<std::string> InputSourceFile(cl::Positional,
                                            cl::desc("<Source file>"),
                                            cl::init("-"),
                                            cl::value_desc("filename"));

/// Retrieve the unique number \p I was mapped to in parseBitcodeFile.
///
/// \param I - The Instruction to find the instruction number for.
/// \param LLVMInstNum - The mapping of Instructions to their location in the
/// module represented by an unsigned integer.
/// \returns The instruction number for \p I if it exists.
std::optional<unsigned>
getPositionInModule(const Instruction *I,
                    const DenseMap<Instruction *, unsigned> &LLVMInstNum) {
  assert(I && "Instruction is nullptr!");
  DenseMap<Instruction *, unsigned>::const_iterator It = LLVMInstNum.find(I);
  if (It == LLVMInstNum.end())
    return std::nullopt;
  return It->second;
}

/// Exports the given SimilarityGroups to a JSON file at \p FilePath.
///
/// \param FilePath - The path to the output location.
/// \param SimSections - The similarity groups to process.
/// \param LLVMInstNum - The mapping of Instructions to their location in the
/// module represented by an unsigned integer.
/// \returns A nonzero error code if there was a failure creating the file.
std::error_code
exportToFile(const StringRef FilePath,
             const SimilarityGroupList &SimSections,
             const DenseMap<Instruction *, unsigned> &LLVMInstNum) {
  std::error_code EC;
  std::unique_ptr<ToolOutputFile> Out(
      new ToolOutputFile(FilePath, EC, sys::fs::OF_None));
  if (EC)
    return EC;

  json::OStream J(Out->os(), 1);
  J.objectBegin();

  unsigned SimOption = 1;
  // Process each list of SimilarityGroups organized by the Module.
  for (const SimilarityGroup &G : SimSections) {
    std::string SimOptionStr = std::to_string(SimOption);
    J.attributeBegin(SimOptionStr);
    J.arrayBegin();
    // For each file there is a list of the range where the similarity
    // exists.
    for (const IRSimilarityCandidate &C : G) {
      std::optional<unsigned> Start =
          getPositionInModule((*C.front()).Inst, LLVMInstNum);
      std::optional<unsigned> End =
          getPositionInModule((*C.back()).Inst, LLVMInstNum);

      assert(Start &&
             "Could not find instruction number for first instruction");
      assert(End && "Could not find instruction number for last instruction");

      J.object([&] {
        J.attribute("start", *Start);
        J.attribute("end", *End);
      });
    }
    J.arrayEnd();
    J.attributeEnd();
    SimOption++;
  }
  J.objectEnd();

  Out->keep();

  return EC;
}

int main(int argc, const char *argv[]) {
  InitLLVM X(argc, argv);

  cl::ParseCommandLineOptions(argc, argv, "LLVM IR Similarity Visualizer\n");

  LLVMContext CurrContext;
  SMDiagnostic Err;
  std::unique_ptr<Module> ModuleToAnalyze =
      parseIRFile(InputSourceFile, Err, CurrContext);

  if (!ModuleToAnalyze) {
    Err.print(argv[0], errs());
    return 1;
  }

  // Mapping from an Instruction pointer to its occurrence in a sequential
  // list of all the Instructions in a Module.
  DenseMap<Instruction *, unsigned> LLVMInstNum;

  // We give each instruction a number, which gives us a start and end value
  // for the beginning and end of each IRSimilarityCandidate.
  unsigned InstructionNumber = 1;
  for (Function &F : *ModuleToAnalyze)
    for (BasicBlock &BB : F)
      for (Instruction &I : BB.instructionsWithoutDebug())
        LLVMInstNum[&I]= InstructionNumber++;

  // The similarity identifier we will use to find the similar sections.
  IRSimilarityIdentifier SimIdent;
  SimilarityGroupList SimilaritySections =
      SimIdent.findSimilarity(*ModuleToAnalyze);

  std::error_code E =
      exportToFile(OutputFilename, SimilaritySections, LLVMInstNum);
  if (E) {
    errs() << argv[0] << ": " << E.message() << '\n';
    return 2;
  }

  return 0;
}
