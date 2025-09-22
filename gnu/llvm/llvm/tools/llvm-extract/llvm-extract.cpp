//===- llvm-extract.cpp - LLVM function extraction utility ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility changes the input module to only contain a single function,
// which is primarily used for debugging transformations.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRPrinter/IRPrintingPasses.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/BlockExtractor.h"
#include "llvm/Transforms/IPO/ExtractGV.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/StripDeadPrototypes.h"
#include "llvm/Transforms/IPO/StripSymbols.h"
#include <memory>
#include <utility>

using namespace llvm;

cl::OptionCategory ExtractCat("llvm-extract Options");

// InputFilename - The filename to read from.
static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode file>"),
                                          cl::init("-"),
                                          cl::value_desc("filename"));

static cl::opt<std::string> OutputFilename("o",
                                           cl::desc("Specify output filename"),
                                           cl::value_desc("filename"),
                                           cl::init("-"), cl::cat(ExtractCat));

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"),
                           cl::cat(ExtractCat));

static cl::opt<bool> DeleteFn("delete",
                              cl::desc("Delete specified Globals from Module"),
                              cl::cat(ExtractCat));

static cl::opt<bool> KeepConstInit("keep-const-init",
                              cl::desc("Keep initializers of constants"),
                              cl::cat(ExtractCat));

static cl::opt<bool>
    Recursive("recursive", cl::desc("Recursively extract all called functions"),
              cl::cat(ExtractCat));

// ExtractFuncs - The functions to extract from the module.
static cl::list<std::string>
    ExtractFuncs("func", cl::desc("Specify function to extract"),
                 cl::value_desc("function"), cl::cat(ExtractCat));

// ExtractRegExpFuncs - The functions, matched via regular expression, to
// extract from the module.
static cl::list<std::string>
    ExtractRegExpFuncs("rfunc",
                       cl::desc("Specify function(s) to extract using a "
                                "regular expression"),
                       cl::value_desc("rfunction"), cl::cat(ExtractCat));

// ExtractBlocks - The blocks to extract from the module.
static cl::list<std::string> ExtractBlocks(
    "bb",
    cl::desc(
        "Specify <function, basic block1[;basic block2...]> pairs to extract.\n"
        "Each pair will create a function.\n"
        "If multiple basic blocks are specified in one pair,\n"
        "the first block in the sequence should dominate the rest.\n"
        "eg:\n"
        "  --bb=f:bb1;bb2 will extract one function with both bb1 and bb2;\n"
        "  --bb=f:bb1 --bb=f:bb2 will extract two functions, one with bb1, one "
        "with bb2."),
    cl::value_desc("function:bb1[;bb2...]"), cl::cat(ExtractCat));

// ExtractAlias - The alias to extract from the module.
static cl::list<std::string>
    ExtractAliases("alias", cl::desc("Specify alias to extract"),
                   cl::value_desc("alias"), cl::cat(ExtractCat));

// ExtractRegExpAliases - The aliases, matched via regular expression, to
// extract from the module.
static cl::list<std::string>
    ExtractRegExpAliases("ralias",
                         cl::desc("Specify alias(es) to extract using a "
                                  "regular expression"),
                         cl::value_desc("ralias"), cl::cat(ExtractCat));

// ExtractGlobals - The globals to extract from the module.
static cl::list<std::string>
    ExtractGlobals("glob", cl::desc("Specify global to extract"),
                   cl::value_desc("global"), cl::cat(ExtractCat));

// ExtractRegExpGlobals - The globals, matched via regular expression, to
// extract from the module...
static cl::list<std::string>
    ExtractRegExpGlobals("rglob",
                         cl::desc("Specify global(s) to extract using a "
                                  "regular expression"),
                         cl::value_desc("rglobal"), cl::cat(ExtractCat));

static cl::opt<bool> OutputAssembly("S",
                                    cl::desc("Write output as LLVM assembly"),
                                    cl::Hidden, cl::cat(ExtractCat));

static cl::opt<bool> PreserveBitcodeUseListOrder(
    "preserve-bc-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM bitcode."),
    cl::init(true), cl::Hidden, cl::cat(ExtractCat));

static cl::opt<bool> PreserveAssemblyUseListOrder(
    "preserve-ll-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM assembly."),
    cl::init(false), cl::Hidden, cl::cat(ExtractCat));

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  LLVMContext Context;
  cl::HideUnrelatedOptions(ExtractCat);
  cl::ParseCommandLineOptions(argc, argv, "llvm extractor\n");

  // Use lazy loading, since we only care about selected global values.
  SMDiagnostic Err;
  std::unique_ptr<Module> M = getLazyIRFileModule(InputFilename, Err, Context);

  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  // Use SetVector to avoid duplicates.
  SetVector<GlobalValue *> GVs;

  // Figure out which aliases we should extract.
  for (size_t i = 0, e = ExtractAliases.size(); i != e; ++i) {
    GlobalAlias *GA = M->getNamedAlias(ExtractAliases[i]);
    if (!GA) {
      errs() << argv[0] << ": program doesn't contain alias named '"
             << ExtractAliases[i] << "'!\n";
      return 1;
    }
    GVs.insert(GA);
  }

  // Extract aliases via regular expression matching.
  for (size_t i = 0, e = ExtractRegExpAliases.size(); i != e; ++i) {
    std::string Error;
    Regex RegEx(ExtractRegExpAliases[i]);
    if (!RegEx.isValid(Error)) {
      errs() << argv[0] << ": '" << ExtractRegExpAliases[i] << "' "
        "invalid regex: " << Error;
    }
    bool match = false;
    for (Module::alias_iterator GA = M->alias_begin(), E = M->alias_end();
         GA != E; GA++) {
      if (RegEx.match(GA->getName())) {
        GVs.insert(&*GA);
        match = true;
      }
    }
    if (!match) {
      errs() << argv[0] << ": program doesn't contain global named '"
             << ExtractRegExpAliases[i] << "'!\n";
      return 1;
    }
  }

  // Figure out which globals we should extract.
  for (size_t i = 0, e = ExtractGlobals.size(); i != e; ++i) {
    GlobalValue *GV = M->getNamedGlobal(ExtractGlobals[i]);
    if (!GV) {
      errs() << argv[0] << ": program doesn't contain global named '"
             << ExtractGlobals[i] << "'!\n";
      return 1;
    }
    GVs.insert(GV);
  }

  // Extract globals via regular expression matching.
  for (size_t i = 0, e = ExtractRegExpGlobals.size(); i != e; ++i) {
    std::string Error;
    Regex RegEx(ExtractRegExpGlobals[i]);
    if (!RegEx.isValid(Error)) {
      errs() << argv[0] << ": '" << ExtractRegExpGlobals[i] << "' "
        "invalid regex: " << Error;
    }
    bool match = false;
    for (auto &GV : M->globals()) {
      if (RegEx.match(GV.getName())) {
        GVs.insert(&GV);
        match = true;
      }
    }
    if (!match) {
      errs() << argv[0] << ": program doesn't contain global named '"
             << ExtractRegExpGlobals[i] << "'!\n";
      return 1;
    }
  }

  // Figure out which functions we should extract.
  for (size_t i = 0, e = ExtractFuncs.size(); i != e; ++i) {
    GlobalValue *GV = M->getFunction(ExtractFuncs[i]);
    if (!GV) {
      errs() << argv[0] << ": program doesn't contain function named '"
             << ExtractFuncs[i] << "'!\n";
      return 1;
    }
    GVs.insert(GV);
  }
  // Extract functions via regular expression matching.
  for (size_t i = 0, e = ExtractRegExpFuncs.size(); i != e; ++i) {
    std::string Error;
    StringRef RegExStr = ExtractRegExpFuncs[i];
    Regex RegEx(RegExStr);
    if (!RegEx.isValid(Error)) {
      errs() << argv[0] << ": '" << ExtractRegExpFuncs[i] << "' "
        "invalid regex: " << Error;
    }
    bool match = false;
    for (Module::iterator F = M->begin(), E = M->end(); F != E;
         F++) {
      if (RegEx.match(F->getName())) {
        GVs.insert(&*F);
        match = true;
      }
    }
    if (!match) {
      errs() << argv[0] << ": program doesn't contain global named '"
             << ExtractRegExpFuncs[i] << "'!\n";
      return 1;
    }
  }

  // Figure out which BasicBlocks we should extract.
  SmallVector<std::pair<Function *, SmallVector<StringRef, 16>>, 2> BBMap;
  for (StringRef StrPair : ExtractBlocks) {
    SmallVector<StringRef, 16> BBNames;
    auto BBInfo = StrPair.split(':');
    // Get the function.
    Function *F = M->getFunction(BBInfo.first);
    if (!F) {
      errs() << argv[0] << ": program doesn't contain a function named '"
             << BBInfo.first << "'!\n";
      return 1;
    }
    // Add the function to the materialize list, and store the basic block names
    // to check after materialization.
    GVs.insert(F);
    BBInfo.second.split(BBNames, ';', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
    BBMap.push_back({F, std::move(BBNames)});
  }

  // Use *argv instead of argv[0] to work around a wrong GCC warning.
  ExitOnError ExitOnErr(std::string(*argv) + ": error reading input: ");

  if (Recursive) {
    std::vector<llvm::Function *> Workqueue;
    for (GlobalValue *GV : GVs) {
      if (auto *F = dyn_cast<Function>(GV)) {
        Workqueue.push_back(F);
      }
    }
    while (!Workqueue.empty()) {
      Function *F = &*Workqueue.back();
      Workqueue.pop_back();
      ExitOnErr(F->materialize());
      for (auto &BB : *F) {
        for (auto &I : BB) {
          CallBase *CB = dyn_cast<CallBase>(&I);
          if (!CB)
            continue;
          Function *CF = CB->getCalledFunction();
          if (!CF)
            continue;
          if (CF->isDeclaration() || GVs.count(CF))
            continue;
          GVs.insert(CF);
          Workqueue.push_back(CF);
        }
      }
    }
  }

  auto Materialize = [&](GlobalValue &GV) { ExitOnErr(GV.materialize()); };

  // Materialize requisite global values.
  if (!DeleteFn) {
    for (size_t i = 0, e = GVs.size(); i != e; ++i)
      Materialize(*GVs[i]);
  } else {
    // Deleting. Materialize every GV that's *not* in GVs.
    SmallPtrSet<GlobalValue *, 8> GVSet(GVs.begin(), GVs.end());
    for (auto &F : *M) {
      if (!GVSet.count(&F))
        Materialize(F);
    }
  }

  {
    std::vector<GlobalValue *> Gvs(GVs.begin(), GVs.end());
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    PassBuilder PB;

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager PM;
    PM.addPass(ExtractGVPass(Gvs, DeleteFn, KeepConstInit));
    PM.run(*M, MAM);

    // Now that we have all the GVs we want, mark the module as fully
    // materialized.
    // FIXME: should the GVExtractionPass handle this?
    ExitOnErr(M->materializeAll());
  }

  // Extract the specified basic blocks from the module and erase the existing
  // functions.
  if (!ExtractBlocks.empty()) {
    // Figure out which BasicBlocks we should extract.
    std::vector<std::vector<BasicBlock *>> GroupOfBBs;
    for (auto &P : BBMap) {
      std::vector<BasicBlock *> BBs;
      for (StringRef BBName : P.second) {
        // The function has been materialized, so add its matching basic blocks
        // to the block extractor list, or fail if a name is not found.
        auto Res = llvm::find_if(*P.first, [&](const BasicBlock &BB) {
          return BB.getName() == BBName;
        });
        if (Res == P.first->end()) {
          errs() << argv[0] << ": function " << P.first->getName()
                 << " doesn't contain a basic block named '" << BBName
                 << "'!\n";
          return 1;
        }
        BBs.push_back(&*Res);
      }
      GroupOfBBs.push_back(BBs);
    }

    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    PassBuilder PB;

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager PM;
    PM.addPass(BlockExtractorPass(std::move(GroupOfBBs), true));
    PM.run(*M, MAM);
  }

  // In addition to deleting all other functions, we also want to spiff it
  // up a little bit.  Do this now.

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassBuilder PB;

  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager PM;
  if (!DeleteFn)
    PM.addPass(GlobalDCEPass());
  PM.addPass(StripDeadDebugInfoPass());
  PM.addPass(StripDeadPrototypesPass());

  std::error_code EC;
  ToolOutputFile Out(OutputFilename, EC, sys::fs::OF_None);
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }

  if (OutputAssembly)
    PM.addPass(PrintModulePass(Out.os(), "", PreserveAssemblyUseListOrder));
  else if (Force || !CheckBitcodeOutputToConsole(Out.os()))
    PM.addPass(BitcodeWriterPass(Out.os(), PreserveBitcodeUseListOrder));

  PM.run(*M, MAM);

  // Declare success.
  Out.keep();

  return 0;
}
