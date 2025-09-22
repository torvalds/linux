//===- Miscompilation.cpp - Debug program miscompilations -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements optimizer and code generation miscompilation debugging
// support.
//
//===----------------------------------------------------------------------===//

#include "BugDriver.h"
#include "ListReducer.h"
#include "ToolRunner.h"
#include "llvm/Config/config.h" // for HAVE_LINK_R
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Transforms/Utils/Cloning.h"

using namespace llvm;

namespace llvm {
extern cl::opt<std::string> OutputPrefix;
extern cl::list<std::string> InputArgv;
} // end namespace llvm

namespace {
static llvm::cl::opt<bool> DisableLoopExtraction(
    "disable-loop-extraction",
    cl::desc("Don't extract loops when searching for miscompilations"),
    cl::init(false));
static llvm::cl::opt<bool> DisableBlockExtraction(
    "disable-block-extraction",
    cl::desc("Don't extract blocks when searching for miscompilations"),
    cl::init(false));

class ReduceMiscompilingPasses : public ListReducer<std::string> {
  BugDriver &BD;

public:
  ReduceMiscompilingPasses(BugDriver &bd) : BD(bd) {}

  Expected<TestResult> doTest(std::vector<std::string> &Prefix,
                              std::vector<std::string> &Suffix) override;
};
} // end anonymous namespace

/// TestResult - After passes have been split into a test group and a control
/// group, see if they still break the program.
///
Expected<ReduceMiscompilingPasses::TestResult>
ReduceMiscompilingPasses::doTest(std::vector<std::string> &Prefix,
                                 std::vector<std::string> &Suffix) {
  // First, run the program with just the Suffix passes.  If it is still broken
  // with JUST the kept passes, discard the prefix passes.
  outs() << "Checking to see if '" << getPassesString(Suffix)
         << "' compiles correctly: ";

  std::string BitcodeResult;
  if (BD.runPasses(BD.getProgram(), Suffix, BitcodeResult, false /*delete*/,
                   true /*quiet*/)) {
    errs() << " Error running this sequence of passes"
           << " on the input program!\n";
    BD.setPassesToRun(Suffix);
    BD.EmitProgressBitcode(BD.getProgram(), "pass-error", false);
    // TODO: This should propagate the error instead of exiting.
    if (Error E = BD.debugOptimizerCrash())
      exit(1);
    exit(0);
  }

  // Check to see if the finished program matches the reference output...
  Expected<bool> Diff = BD.diffProgram(BD.getProgram(), BitcodeResult, "",
                                       true /*delete bitcode*/);
  if (Error E = Diff.takeError())
    return std::move(E);
  if (*Diff) {
    outs() << " nope.\n";
    if (Suffix.empty()) {
      errs() << BD.getToolName() << ": I'm confused: the test fails when "
             << "no passes are run, nondeterministic program?\n";
      exit(1);
    }
    return KeepSuffix; // Miscompilation detected!
  }
  outs() << " yup.\n"; // No miscompilation!

  if (Prefix.empty())
    return NoFailure;

  // Next, see if the program is broken if we run the "prefix" passes first,
  // then separately run the "kept" passes.
  outs() << "Checking to see if '" << getPassesString(Prefix)
         << "' compiles correctly: ";

  // If it is not broken with the kept passes, it's possible that the prefix
  // passes must be run before the kept passes to break it.  If the program
  // WORKS after the prefix passes, but then fails if running the prefix AND
  // kept passes, we can update our bitcode file to include the result of the
  // prefix passes, then discard the prefix passes.
  //
  if (BD.runPasses(BD.getProgram(), Prefix, BitcodeResult, false /*delete*/,
                   true /*quiet*/)) {
    errs() << " Error running this sequence of passes"
           << " on the input program!\n";
    BD.setPassesToRun(Prefix);
    BD.EmitProgressBitcode(BD.getProgram(), "pass-error", false);
    // TODO: This should propagate the error instead of exiting.
    if (Error E = BD.debugOptimizerCrash())
      exit(1);
    exit(0);
  }

  // If the prefix maintains the predicate by itself, only keep the prefix!
  Diff = BD.diffProgram(BD.getProgram(), BitcodeResult, "", false);
  if (Error E = Diff.takeError())
    return std::move(E);
  if (*Diff) {
    outs() << " nope.\n";
    sys::fs::remove(BitcodeResult);
    return KeepPrefix;
  }
  outs() << " yup.\n"; // No miscompilation!

  // Ok, so now we know that the prefix passes work, try running the suffix
  // passes on the result of the prefix passes.
  //
  std::unique_ptr<Module> PrefixOutput =
      parseInputFile(BitcodeResult, BD.getContext());
  if (!PrefixOutput) {
    errs() << BD.getToolName() << ": Error reading bitcode file '"
           << BitcodeResult << "'!\n";
    exit(1);
  }
  sys::fs::remove(BitcodeResult);

  // Don't check if there are no passes in the suffix.
  if (Suffix.empty())
    return NoFailure;

  outs() << "Checking to see if '" << getPassesString(Suffix)
         << "' passes compile correctly after the '" << getPassesString(Prefix)
         << "' passes: ";

  std::unique_ptr<Module> OriginalInput =
      BD.swapProgramIn(std::move(PrefixOutput));
  if (BD.runPasses(BD.getProgram(), Suffix, BitcodeResult, false /*delete*/,
                   true /*quiet*/)) {
    errs() << " Error running this sequence of passes"
           << " on the input program!\n";
    BD.setPassesToRun(Suffix);
    BD.EmitProgressBitcode(BD.getProgram(), "pass-error", false);
    // TODO: This should propagate the error instead of exiting.
    if (Error E = BD.debugOptimizerCrash())
      exit(1);
    exit(0);
  }

  // Run the result...
  Diff = BD.diffProgram(BD.getProgram(), BitcodeResult, "",
                        true /*delete bitcode*/);
  if (Error E = Diff.takeError())
    return std::move(E);
  if (*Diff) {
    outs() << " nope.\n";
    return KeepSuffix;
  }

  // Otherwise, we must not be running the bad pass anymore.
  outs() << " yup.\n"; // No miscompilation!
  // Restore orig program & free test.
  BD.setNewProgram(std::move(OriginalInput));
  return NoFailure;
}

namespace {
class ReduceMiscompilingFunctions : public ListReducer<Function *> {
  BugDriver &BD;
  Expected<bool> (*TestFn)(BugDriver &, std::unique_ptr<Module>,
                           std::unique_ptr<Module>);

public:
  ReduceMiscompilingFunctions(BugDriver &bd,
                              Expected<bool> (*F)(BugDriver &,
                                                  std::unique_ptr<Module>,
                                                  std::unique_ptr<Module>))
      : BD(bd), TestFn(F) {}

  Expected<TestResult> doTest(std::vector<Function *> &Prefix,
                              std::vector<Function *> &Suffix) override {
    if (!Suffix.empty()) {
      Expected<bool> Ret = TestFuncs(Suffix);
      if (Error E = Ret.takeError())
        return std::move(E);
      if (*Ret)
        return KeepSuffix;
    }
    if (!Prefix.empty()) {
      Expected<bool> Ret = TestFuncs(Prefix);
      if (Error E = Ret.takeError())
        return std::move(E);
      if (*Ret)
        return KeepPrefix;
    }
    return NoFailure;
  }

  Expected<bool> TestFuncs(const std::vector<Function *> &Prefix);
};
} // end anonymous namespace

/// Given two modules, link them together and run the program, checking to see
/// if the program matches the diff. If there is an error, return NULL. If not,
/// return the merged module. The Broken argument will be set to true if the
/// output is different. If the DeleteInputs argument is set to true then this
/// function deletes both input modules before it returns.
///
static Expected<std::unique_ptr<Module>> testMergedProgram(const BugDriver &BD,
                                                           const Module &M1,
                                                           const Module &M2,
                                                           bool &Broken) {
  // Resulting merge of M1 and M2.
  auto Merged = CloneModule(M1);
  if (Linker::linkModules(*Merged, CloneModule(M2)))
    // TODO: Shouldn't we thread the error up instead of exiting?
    exit(1);

  // Execute the program.
  Expected<bool> Diff = BD.diffProgram(*Merged, "", "", false);
  if (Error E = Diff.takeError())
    return std::move(E);
  Broken = *Diff;
  return std::move(Merged);
}

/// split functions in a Module into two groups: those that are under
/// consideration for miscompilation vs. those that are not, and test
/// accordingly. Each group of functions becomes a separate Module.
Expected<bool>
ReduceMiscompilingFunctions::TestFuncs(const std::vector<Function *> &Funcs) {
  // Test to see if the function is misoptimized if we ONLY run it on the
  // functions listed in Funcs.
  outs() << "Checking to see if the program is misoptimized when "
         << (Funcs.size() == 1 ? "this function is" : "these functions are")
         << " run through the pass"
         << (BD.getPassesToRun().size() == 1 ? "" : "es") << ":";
  PrintFunctionList(Funcs);
  outs() << '\n';

  // Create a clone for two reasons:
  // * If the optimization passes delete any function, the deleted function
  //   will be in the clone and Funcs will still point to valid memory
  // * If the optimization passes use interprocedural information to break
  //   a function, we want to continue with the original function. Otherwise
  //   we can conclude that a function triggers the bug when in fact one
  //   needs a larger set of original functions to do so.
  ValueToValueMapTy VMap;
  std::unique_ptr<Module> Clone = CloneModule(BD.getProgram(), VMap);
  std::unique_ptr<Module> Orig = BD.swapProgramIn(std::move(Clone));

  std::vector<Function *> FuncsOnClone;
  for (unsigned i = 0, e = Funcs.size(); i != e; ++i) {
    Function *F = cast<Function>(VMap[Funcs[i]]);
    FuncsOnClone.push_back(F);
  }

  // Split the module into the two halves of the program we want.
  VMap.clear();
  std::unique_ptr<Module> ToNotOptimize = CloneModule(BD.getProgram(), VMap);
  std::unique_ptr<Module> ToOptimize =
      SplitFunctionsOutOfModule(ToNotOptimize.get(), FuncsOnClone, VMap);

  Expected<bool> Broken =
      TestFn(BD, std::move(ToOptimize), std::move(ToNotOptimize));

  BD.setNewProgram(std::move(Orig));

  return Broken;
}

/// Give anonymous global values names.
static void DisambiguateGlobalSymbols(Module &M) {
  for (Module::global_iterator I = M.global_begin(), E = M.global_end(); I != E;
       ++I)
    if (!I->hasName())
      I->setName("anon_global");
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
    if (!I->hasName())
      I->setName("anon_fn");
}

/// Given a reduced list of functions that still exposed the bug, check to see
/// if we can extract the loops in the region without obscuring the bug.  If so,
/// it reduces the amount of code identified.
///
static Expected<bool>
ExtractLoops(BugDriver &BD,
             Expected<bool> (*TestFn)(BugDriver &, std::unique_ptr<Module>,
                                      std::unique_ptr<Module>),
             std::vector<Function *> &MiscompiledFunctions) {
  bool MadeChange = false;
  while (true) {
    if (BugpointIsInterrupted)
      return MadeChange;

    ValueToValueMapTy VMap;
    std::unique_ptr<Module> ToNotOptimize = CloneModule(BD.getProgram(), VMap);
    std::unique_ptr<Module> ToOptimize = SplitFunctionsOutOfModule(
        ToNotOptimize.get(), MiscompiledFunctions, VMap);
    std::unique_ptr<Module> ToOptimizeLoopExtracted =
        BD.extractLoop(ToOptimize.get());
    if (!ToOptimizeLoopExtracted)
      // If the loop extractor crashed or if there were no extractible loops,
      // then this chapter of our odyssey is over with.
      return MadeChange;

    errs() << "Extracted a loop from the breaking portion of the program.\n";

    // Bugpoint is intentionally not very trusting of LLVM transformations.  In
    // particular, we're not going to assume that the loop extractor works, so
    // we're going to test the newly loop extracted program to make sure nothing
    // has broken.  If something broke, then we'll inform the user and stop
    // extraction.
    AbstractInterpreter *AI = BD.switchToSafeInterpreter();
    bool Failure;
    Expected<std::unique_ptr<Module>> New = testMergedProgram(
        BD, *ToOptimizeLoopExtracted, *ToNotOptimize, Failure);
    if (Error E = New.takeError())
      return std::move(E);
    if (!*New)
      return false;

    // Delete the original and set the new program.
    std::unique_ptr<Module> Old = BD.swapProgramIn(std::move(*New));
    for (unsigned i = 0, e = MiscompiledFunctions.size(); i != e; ++i)
      MiscompiledFunctions[i] = cast<Function>(VMap[MiscompiledFunctions[i]]);

    if (Failure) {
      BD.switchToInterpreter(AI);

      // Merged program doesn't work anymore!
      errs() << "  *** ERROR: Loop extraction broke the program. :("
             << " Please report a bug!\n";
      errs() << "      Continuing on with un-loop-extracted version.\n";

      BD.writeProgramToFile(OutputPrefix + "-loop-extract-fail-tno.bc",
                            *ToNotOptimize);
      BD.writeProgramToFile(OutputPrefix + "-loop-extract-fail-to.bc",
                            *ToOptimize);
      BD.writeProgramToFile(OutputPrefix + "-loop-extract-fail-to-le.bc",
                            *ToOptimizeLoopExtracted);

      errs() << "Please submit the " << OutputPrefix
             << "-loop-extract-fail-*.bc files.\n";
      return MadeChange;
    }
    BD.switchToInterpreter(AI);

    outs() << "  Testing after loop extraction:\n";
    // Clone modules, the tester function will free them.
    std::unique_ptr<Module> TOLEBackup =
        CloneModule(*ToOptimizeLoopExtracted, VMap);
    std::unique_ptr<Module> TNOBackup = CloneModule(*ToNotOptimize, VMap);

    for (unsigned i = 0, e = MiscompiledFunctions.size(); i != e; ++i)
      MiscompiledFunctions[i] = cast<Function>(VMap[MiscompiledFunctions[i]]);

    Expected<bool> Result = TestFn(BD, std::move(ToOptimizeLoopExtracted),
                                   std::move(ToNotOptimize));
    if (Error E = Result.takeError())
      return std::move(E);

    ToOptimizeLoopExtracted = std::move(TOLEBackup);
    ToNotOptimize = std::move(TNOBackup);

    if (!*Result) {
      outs() << "*** Loop extraction masked the problem.  Undoing.\n";
      // If the program is not still broken, then loop extraction did something
      // that masked the error.  Stop loop extraction now.

      std::vector<std::pair<std::string, FunctionType *>> MisCompFunctions;
      for (Function *F : MiscompiledFunctions) {
        MisCompFunctions.emplace_back(std::string(F->getName()),
                                      F->getFunctionType());
      }

      if (Linker::linkModules(*ToNotOptimize,
                              std::move(ToOptimizeLoopExtracted)))
        exit(1);

      MiscompiledFunctions.clear();
      for (unsigned i = 0, e = MisCompFunctions.size(); i != e; ++i) {
        Function *NewF = ToNotOptimize->getFunction(MisCompFunctions[i].first);

        assert(NewF && "Function not found??");
        MiscompiledFunctions.push_back(NewF);
      }

      BD.setNewProgram(std::move(ToNotOptimize));
      return MadeChange;
    }

    outs() << "*** Loop extraction successful!\n";

    std::vector<std::pair<std::string, FunctionType *>> MisCompFunctions;
    for (Module::iterator I = ToOptimizeLoopExtracted->begin(),
                          E = ToOptimizeLoopExtracted->end();
         I != E; ++I)
      if (!I->isDeclaration())
        MisCompFunctions.emplace_back(std::string(I->getName()),
                                      I->getFunctionType());

    // Okay, great!  Now we know that we extracted a loop and that loop
    // extraction both didn't break the program, and didn't mask the problem.
    // Replace the current program with the loop extracted version, and try to
    // extract another loop.
    if (Linker::linkModules(*ToNotOptimize, std::move(ToOptimizeLoopExtracted)))
      exit(1);

    // All of the Function*'s in the MiscompiledFunctions list are in the old
    // module.  Update this list to include all of the functions in the
    // optimized and loop extracted module.
    MiscompiledFunctions.clear();
    for (unsigned i = 0, e = MisCompFunctions.size(); i != e; ++i) {
      Function *NewF = ToNotOptimize->getFunction(MisCompFunctions[i].first);

      assert(NewF && "Function not found??");
      MiscompiledFunctions.push_back(NewF);
    }

    BD.setNewProgram(std::move(ToNotOptimize));
    MadeChange = true;
  }
}

namespace {
class ReduceMiscompiledBlocks : public ListReducer<BasicBlock *> {
  BugDriver &BD;
  Expected<bool> (*TestFn)(BugDriver &, std::unique_ptr<Module>,
                           std::unique_ptr<Module>);
  std::vector<Function *> FunctionsBeingTested;

public:
  ReduceMiscompiledBlocks(BugDriver &bd,
                          Expected<bool> (*F)(BugDriver &,
                                              std::unique_ptr<Module>,
                                              std::unique_ptr<Module>),
                          const std::vector<Function *> &Fns)
      : BD(bd), TestFn(F), FunctionsBeingTested(Fns) {}

  Expected<TestResult> doTest(std::vector<BasicBlock *> &Prefix,
                              std::vector<BasicBlock *> &Suffix) override {
    if (!Suffix.empty()) {
      Expected<bool> Ret = TestFuncs(Suffix);
      if (Error E = Ret.takeError())
        return std::move(E);
      if (*Ret)
        return KeepSuffix;
    }
    if (!Prefix.empty()) {
      Expected<bool> Ret = TestFuncs(Prefix);
      if (Error E = Ret.takeError())
        return std::move(E);
      if (*Ret)
        return KeepPrefix;
    }
    return NoFailure;
  }

  Expected<bool> TestFuncs(const std::vector<BasicBlock *> &BBs);
};
} // end anonymous namespace

/// TestFuncs - Extract all blocks for the miscompiled functions except for the
/// specified blocks.  If the problem still exists, return true.
///
Expected<bool>
ReduceMiscompiledBlocks::TestFuncs(const std::vector<BasicBlock *> &BBs) {
  // Test to see if the function is misoptimized if we ONLY run it on the
  // functions listed in Funcs.
  outs() << "Checking to see if the program is misoptimized when all ";
  if (!BBs.empty()) {
    outs() << "but these " << BBs.size() << " blocks are extracted: ";
    for (unsigned i = 0, e = BBs.size() < 10 ? BBs.size() : 10; i != e; ++i)
      outs() << BBs[i]->getName() << " ";
    if (BBs.size() > 10)
      outs() << "...";
  } else {
    outs() << "blocks are extracted.";
  }
  outs() << '\n';

  // Split the module into the two halves of the program we want.
  ValueToValueMapTy VMap;
  std::unique_ptr<Module> Clone = CloneModule(BD.getProgram(), VMap);
  std::unique_ptr<Module> Orig = BD.swapProgramIn(std::move(Clone));
  std::vector<Function *> FuncsOnClone;
  std::vector<BasicBlock *> BBsOnClone;
  for (unsigned i = 0, e = FunctionsBeingTested.size(); i != e; ++i) {
    Function *F = cast<Function>(VMap[FunctionsBeingTested[i]]);
    FuncsOnClone.push_back(F);
  }
  for (unsigned i = 0, e = BBs.size(); i != e; ++i) {
    BasicBlock *BB = cast<BasicBlock>(VMap[BBs[i]]);
    BBsOnClone.push_back(BB);
  }
  VMap.clear();

  std::unique_ptr<Module> ToNotOptimize = CloneModule(BD.getProgram(), VMap);
  std::unique_ptr<Module> ToOptimize =
      SplitFunctionsOutOfModule(ToNotOptimize.get(), FuncsOnClone, VMap);

  // Try the extraction.  If it doesn't work, then the block extractor crashed
  // or something, in which case bugpoint can't chase down this possibility.
  if (std::unique_ptr<Module> New =
          BD.extractMappedBlocksFromModule(BBsOnClone, ToOptimize.get())) {
    Expected<bool> Ret = TestFn(BD, std::move(New), std::move(ToNotOptimize));
    BD.setNewProgram(std::move(Orig));
    return Ret;
  }
  BD.setNewProgram(std::move(Orig));
  return false;
}

/// Given a reduced list of functions that still expose the bug, extract as many
/// basic blocks from the region as possible without obscuring the bug.
///
static Expected<bool>
ExtractBlocks(BugDriver &BD,
              Expected<bool> (*TestFn)(BugDriver &, std::unique_ptr<Module>,
                                       std::unique_ptr<Module>),
              std::vector<Function *> &MiscompiledFunctions) {
  if (BugpointIsInterrupted)
    return false;

  std::vector<BasicBlock *> Blocks;
  for (unsigned i = 0, e = MiscompiledFunctions.size(); i != e; ++i)
    for (BasicBlock &BB : *MiscompiledFunctions[i])
      Blocks.push_back(&BB);

  // Use the list reducer to identify blocks that can be extracted without
  // obscuring the bug.  The Blocks list will end up containing blocks that must
  // be retained from the original program.
  unsigned OldSize = Blocks.size();

  // Check to see if all blocks are extractible first.
  Expected<bool> Ret = ReduceMiscompiledBlocks(BD, TestFn, MiscompiledFunctions)
                           .TestFuncs(std::vector<BasicBlock *>());
  if (Error E = Ret.takeError())
    return std::move(E);
  if (*Ret) {
    Blocks.clear();
  } else {
    Expected<bool> Ret =
        ReduceMiscompiledBlocks(BD, TestFn, MiscompiledFunctions)
            .reduceList(Blocks);
    if (Error E = Ret.takeError())
      return std::move(E);
    if (Blocks.size() == OldSize)
      return false;
  }

  ValueToValueMapTy VMap;
  std::unique_ptr<Module> ProgClone = CloneModule(BD.getProgram(), VMap);
  std::unique_ptr<Module> ToExtract =
      SplitFunctionsOutOfModule(ProgClone.get(), MiscompiledFunctions, VMap);
  std::unique_ptr<Module> Extracted =
      BD.extractMappedBlocksFromModule(Blocks, ToExtract.get());
  if (!Extracted) {
    // Weird, extraction should have worked.
    errs() << "Nondeterministic problem extracting blocks??\n";
    return false;
  }

  // Otherwise, block extraction succeeded.  Link the two program fragments back
  // together.

  std::vector<std::pair<std::string, FunctionType *>> MisCompFunctions;
  for (Module::iterator I = Extracted->begin(), E = Extracted->end(); I != E;
       ++I)
    if (!I->isDeclaration())
      MisCompFunctions.emplace_back(std::string(I->getName()),
                                    I->getFunctionType());

  if (Linker::linkModules(*ProgClone, std::move(Extracted)))
    exit(1);

  // Update the list of miscompiled functions.
  MiscompiledFunctions.clear();

  for (unsigned i = 0, e = MisCompFunctions.size(); i != e; ++i) {
    Function *NewF = ProgClone->getFunction(MisCompFunctions[i].first);
    assert(NewF && "Function not found??");
    MiscompiledFunctions.push_back(NewF);
  }

  // Set the new program and delete the old one.
  BD.setNewProgram(std::move(ProgClone));

  return true;
}

/// This is a generic driver to narrow down miscompilations, either in an
/// optimization or a code generator.
///
static Expected<std::vector<Function *>> DebugAMiscompilation(
    BugDriver &BD,
    Expected<bool> (*TestFn)(BugDriver &, std::unique_ptr<Module>,
                             std::unique_ptr<Module>)) {
  // Okay, now that we have reduced the list of passes which are causing the
  // failure, see if we can pin down which functions are being
  // miscompiled... first build a list of all of the non-external functions in
  // the program.
  std::vector<Function *> MiscompiledFunctions;
  Module &Prog = BD.getProgram();
  for (Function &F : Prog)
    if (!F.isDeclaration())
      MiscompiledFunctions.push_back(&F);

  // Do the reduction...
  if (!BugpointIsInterrupted) {
    Expected<bool> Ret = ReduceMiscompilingFunctions(BD, TestFn)
                             .reduceList(MiscompiledFunctions);
    if (Error E = Ret.takeError()) {
      errs() << "\n***Cannot reduce functions: ";
      return std::move(E);
    }
  }
  outs() << "\n*** The following function"
         << (MiscompiledFunctions.size() == 1 ? " is" : "s are")
         << " being miscompiled: ";
  PrintFunctionList(MiscompiledFunctions);
  outs() << '\n';

  // See if we can rip any loops out of the miscompiled functions and still
  // trigger the problem.

  if (!BugpointIsInterrupted && !DisableLoopExtraction) {
    Expected<bool> Ret = ExtractLoops(BD, TestFn, MiscompiledFunctions);
    if (Error E = Ret.takeError())
      return std::move(E);
    if (*Ret) {
      // Okay, we extracted some loops and the problem still appears.  See if
      // we can eliminate some of the created functions from being candidates.
      DisambiguateGlobalSymbols(BD.getProgram());

      // Do the reduction...
      if (!BugpointIsInterrupted)
        Ret = ReduceMiscompilingFunctions(BD, TestFn)
                  .reduceList(MiscompiledFunctions);
      if (Error E = Ret.takeError())
        return std::move(E);

      outs() << "\n*** The following function"
             << (MiscompiledFunctions.size() == 1 ? " is" : "s are")
             << " being miscompiled: ";
      PrintFunctionList(MiscompiledFunctions);
      outs() << '\n';
    }
  }

  if (!BugpointIsInterrupted && !DisableBlockExtraction) {
    Expected<bool> Ret = ExtractBlocks(BD, TestFn, MiscompiledFunctions);
    if (Error E = Ret.takeError())
      return std::move(E);
    if (*Ret) {
      // Okay, we extracted some blocks and the problem still appears.  See if
      // we can eliminate some of the created functions from being candidates.
      DisambiguateGlobalSymbols(BD.getProgram());

      // Do the reduction...
      Ret = ReduceMiscompilingFunctions(BD, TestFn)
                .reduceList(MiscompiledFunctions);
      if (Error E = Ret.takeError())
        return std::move(E);

      outs() << "\n*** The following function"
             << (MiscompiledFunctions.size() == 1 ? " is" : "s are")
             << " being miscompiled: ";
      PrintFunctionList(MiscompiledFunctions);
      outs() << '\n';
    }
  }

  return MiscompiledFunctions;
}

/// This is the predicate function used to check to see if the "Test" portion of
/// the program is misoptimized.  If so, return true.  In any case, both module
/// arguments are deleted.
///
static Expected<bool> TestOptimizer(BugDriver &BD, std::unique_ptr<Module> Test,
                                    std::unique_ptr<Module> Safe) {
  // Run the optimization passes on ToOptimize, producing a transformed version
  // of the functions being tested.
  outs() << "  Optimizing functions being tested: ";
  std::unique_ptr<Module> Optimized =
      BD.runPassesOn(Test.get(), BD.getPassesToRun());
  if (!Optimized) {
    errs() << " Error running this sequence of passes"
           << " on the input program!\n";
    BD.EmitProgressBitcode(*Test, "pass-error", false);
    BD.setNewProgram(std::move(Test));
    if (Error E = BD.debugOptimizerCrash())
      return std::move(E);
    return false;
  }
  outs() << "done.\n";

  outs() << "  Checking to see if the merged program executes correctly: ";
  bool Broken;
  auto Result = testMergedProgram(BD, *Optimized, *Safe, Broken);
  if (Error E = Result.takeError())
    return std::move(E);
  if (auto New = std::move(*Result)) {
    outs() << (Broken ? " nope.\n" : " yup.\n");
    // Delete the original and set the new program.
    BD.setNewProgram(std::move(New));
  }
  return Broken;
}

/// debugMiscompilation - This method is used when the passes selected are not
/// crashing, but the generated output is semantically different from the
/// input.
///
Error BugDriver::debugMiscompilation() {
  // Make sure something was miscompiled...
  if (!BugpointIsInterrupted) {
    Expected<bool> Result =
        ReduceMiscompilingPasses(*this).reduceList(PassesToRun);
    if (Error E = Result.takeError())
      return E;
    if (!*Result)
      return make_error<StringError>(
          "*** Optimized program matches reference output!  No problem"
          " detected...\nbugpoint can't help you with your problem!\n",
          inconvertibleErrorCode());
  }

  outs() << "\n*** Found miscompiling pass"
         << (getPassesToRun().size() == 1 ? "" : "es") << ": "
         << getPassesString(getPassesToRun()) << '\n';
  EmitProgressBitcode(*Program, "passinput");

  Expected<std::vector<Function *>> MiscompiledFunctions =
      DebugAMiscompilation(*this, TestOptimizer);
  if (Error E = MiscompiledFunctions.takeError())
    return E;

  // Output a bunch of bitcode files for the user...
  outs() << "Outputting reduced bitcode files which expose the problem:\n";
  ValueToValueMapTy VMap;
  Module *ToNotOptimize = CloneModule(getProgram(), VMap).release();
  Module *ToOptimize =
      SplitFunctionsOutOfModule(ToNotOptimize, *MiscompiledFunctions, VMap)
          .release();

  outs() << "  Non-optimized portion: ";
  EmitProgressBitcode(*ToNotOptimize, "tonotoptimize", true);
  delete ToNotOptimize; // Delete hacked module.

  outs() << "  Portion that is input to optimizer: ";
  EmitProgressBitcode(*ToOptimize, "tooptimize");
  delete ToOptimize; // Delete hacked module.

  return Error::success();
}

/// Get the specified modules ready for code generator testing.
///
static std::unique_ptr<Module>
CleanupAndPrepareModules(BugDriver &BD, std::unique_ptr<Module> Test,
                         Module *Safe) {
  // Clean up the modules, removing extra cruft that we don't need anymore...
  Test = BD.performFinalCleanups(std::move(Test));

  // If we are executing the JIT, we have several nasty issues to take care of.
  if (!BD.isExecutingJIT())
    return Test;

  // First, if the main function is in the Safe module, we must add a stub to
  // the Test module to call into it.  Thus, we create a new function `main'
  // which just calls the old one.
  if (Function *oldMain = Safe->getFunction("main"))
    if (!oldMain->isDeclaration()) {
      // Rename it
      oldMain->setName("llvm_bugpoint_old_main");
      // Create a NEW `main' function with same type in the test module.
      Function *newMain =
          Function::Create(oldMain->getFunctionType(),
                           GlobalValue::ExternalLinkage, "main", Test.get());
      // Create an `oldmain' prototype in the test module, which will
      // corresponds to the real main function in the same module.
      Function *oldMainProto = Function::Create(oldMain->getFunctionType(),
                                                GlobalValue::ExternalLinkage,
                                                oldMain->getName(), Test.get());
      // Set up and remember the argument list for the main function.
      std::vector<Value *> args;
      for (Function::arg_iterator I = newMain->arg_begin(),
                                  E = newMain->arg_end(),
                                  OI = oldMain->arg_begin();
           I != E; ++I, ++OI) {
        I->setName(OI->getName()); // Copy argument names from oldMain
        args.push_back(&*I);
      }

      // Call the old main function and return its result
      BasicBlock *BB = BasicBlock::Create(Safe->getContext(), "entry", newMain);
      CallInst *call = CallInst::Create(oldMainProto, args, "", BB);

      // If the type of old function wasn't void, return value of call
      ReturnInst::Create(Safe->getContext(), call, BB);
    }

  // The second nasty issue we must deal with in the JIT is that the Safe
  // module cannot directly reference any functions defined in the test
  // module.  Instead, we use a JIT API call to dynamically resolve the
  // symbol.

  // Add the resolver to the Safe module.
  // Prototype: void *getPointerToNamedFunction(const char* Name)
  FunctionCallee resolverFunc = Safe->getOrInsertFunction(
      "getPointerToNamedFunction", PointerType::getUnqual(Safe->getContext()),
      PointerType::getUnqual(Safe->getContext()));

  // Use the function we just added to get addresses of functions we need.
  for (Module::iterator F = Safe->begin(), E = Safe->end(); F != E; ++F) {
    if (F->isDeclaration() && !F->use_empty() &&
        &*F != resolverFunc.getCallee() &&
        !F->isIntrinsic() /* ignore intrinsics */) {
      Function *TestFn = Test->getFunction(F->getName());

      // Don't forward functions which are external in the test module too.
      if (TestFn && !TestFn->isDeclaration()) {
        // 1. Add a string constant with its name to the global file
        Constant *InitArray =
            ConstantDataArray::getString(F->getContext(), F->getName());
        GlobalVariable *funcName = new GlobalVariable(
            *Safe, InitArray->getType(), true /*isConstant*/,
            GlobalValue::InternalLinkage, InitArray, F->getName() + "_name");

        // 2. Use `GetElementPtr *funcName, 0, 0' to convert the string to an
        // sbyte* so it matches the signature of the resolver function.

        // GetElementPtr *funcName, ulong 0, ulong 0
        std::vector<Constant *> GEPargs(
            2, Constant::getNullValue(Type::getInt32Ty(F->getContext())));
        Value *GEP = ConstantExpr::getGetElementPtr(InitArray->getType(),
                                                    funcName, GEPargs);
        std::vector<Value *> ResolverArgs;
        ResolverArgs.push_back(GEP);

        // Rewrite uses of F in global initializers, etc. to uses of a wrapper
        // function that dynamically resolves the calls to F via our JIT API
        if (!F->use_empty()) {
          // Create a new global to hold the cached function pointer.
          Constant *NullPtr = ConstantPointerNull::get(F->getType());
          GlobalVariable *Cache = new GlobalVariable(
              *F->getParent(), F->getType(), false,
              GlobalValue::InternalLinkage, NullPtr, F->getName() + ".fpcache");

          // Construct a new stub function that will re-route calls to F
          FunctionType *FuncTy = F->getFunctionType();
          Function *FuncWrapper =
              Function::Create(FuncTy, GlobalValue::InternalLinkage,
                               F->getName() + "_wrapper", F->getParent());
          BasicBlock *EntryBB =
              BasicBlock::Create(F->getContext(), "entry", FuncWrapper);
          BasicBlock *DoCallBB =
              BasicBlock::Create(F->getContext(), "usecache", FuncWrapper);
          BasicBlock *LookupBB =
              BasicBlock::Create(F->getContext(), "lookupfp", FuncWrapper);

          // Check to see if we already looked up the value.
          Value *CachedVal =
              new LoadInst(F->getType(), Cache, "fpcache", EntryBB);
          Value *IsNull = new ICmpInst(EntryBB, ICmpInst::ICMP_EQ, CachedVal,
                                       NullPtr, "isNull");
          BranchInst::Create(LookupBB, DoCallBB, IsNull, EntryBB);

          // Resolve the call to function F via the JIT API:
          //
          // call resolver(GetElementPtr...)
          CallInst *Resolver = CallInst::Create(resolverFunc, ResolverArgs,
                                                "resolver", LookupBB);

          // Cast the result from the resolver to correctly-typed function.
          CastInst *CastedResolver = new BitCastInst(
              Resolver, PointerType::getUnqual(F->getFunctionType()),
              "resolverCast", LookupBB);

          // Save the value in our cache.
          new StoreInst(CastedResolver, Cache, LookupBB);
          BranchInst::Create(DoCallBB, LookupBB);

          PHINode *FuncPtr =
              PHINode::Create(NullPtr->getType(), 2, "fp", DoCallBB);
          FuncPtr->addIncoming(CastedResolver, LookupBB);
          FuncPtr->addIncoming(CachedVal, EntryBB);

          // Save the argument list.
          std::vector<Value *> Args;
          for (Argument &A : FuncWrapper->args())
            Args.push_back(&A);

          // Pass on the arguments to the real function, return its result
          if (F->getReturnType()->isVoidTy()) {
            CallInst::Create(FuncTy, FuncPtr, Args, "", DoCallBB);
            ReturnInst::Create(F->getContext(), DoCallBB);
          } else {
            CallInst *Call =
                CallInst::Create(FuncTy, FuncPtr, Args, "retval", DoCallBB);
            ReturnInst::Create(F->getContext(), Call, DoCallBB);
          }

          // Use the wrapper function instead of the old function
          F->replaceAllUsesWith(FuncWrapper);
        }
      }
    }
  }

  if (verifyModule(*Test) || verifyModule(*Safe)) {
    errs() << "Bugpoint has a bug, which corrupted a module!!\n";
    abort();
  }

  return Test;
}

/// This is the predicate function used to check to see if the "Test" portion of
/// the program is miscompiled by the code generator under test.  If so, return
/// true.  In any case, both module arguments are deleted.
///
static Expected<bool> TestCodeGenerator(BugDriver &BD,
                                        std::unique_ptr<Module> Test,
                                        std::unique_ptr<Module> Safe) {
  Test = CleanupAndPrepareModules(BD, std::move(Test), Safe.get());

  SmallString<128> TestModuleBC;
  int TestModuleFD;
  std::error_code EC = sys::fs::createTemporaryFile("bugpoint.test", "bc",
                                                    TestModuleFD, TestModuleBC);
  if (EC) {
    errs() << BD.getToolName()
           << "Error making unique filename: " << EC.message() << "\n";
    exit(1);
  }
  if (BD.writeProgramToFile(std::string(TestModuleBC), TestModuleFD, *Test)) {
    errs() << "Error writing bitcode to `" << TestModuleBC.str()
           << "'\nExiting.";
    exit(1);
  }

  FileRemover TestModuleBCRemover(TestModuleBC.str(), !SaveTemps);

  // Make the shared library
  SmallString<128> SafeModuleBC;
  int SafeModuleFD;
  EC = sys::fs::createTemporaryFile("bugpoint.safe", "bc", SafeModuleFD,
                                    SafeModuleBC);
  if (EC) {
    errs() << BD.getToolName()
           << "Error making unique filename: " << EC.message() << "\n";
    exit(1);
  }

  if (BD.writeProgramToFile(std::string(SafeModuleBC), SafeModuleFD, *Safe)) {
    errs() << "Error writing bitcode to `" << SafeModuleBC << "'\nExiting.";
    exit(1);
  }

  FileRemover SafeModuleBCRemover(SafeModuleBC.str(), !SaveTemps);

  Expected<std::string> SharedObject =
      BD.compileSharedObject(std::string(SafeModuleBC));
  if (Error E = SharedObject.takeError())
    return std::move(E);

  FileRemover SharedObjectRemover(*SharedObject, !SaveTemps);

  // Run the code generator on the `Test' code, loading the shared library.
  // The function returns whether or not the new output differs from reference.
  Expected<bool> Result = BD.diffProgram(
      BD.getProgram(), std::string(TestModuleBC), *SharedObject, false);
  if (Error E = Result.takeError())
    return std::move(E);

  if (*Result)
    errs() << ": still failing!\n";
  else
    errs() << ": didn't fail.\n";

  return Result;
}

/// debugCodeGenerator - debug errors in LLC, LLI, or CBE.
///
Error BugDriver::debugCodeGenerator() {
  if ((void *)SafeInterpreter == (void *)Interpreter) {
    Expected<std::string> Result =
        executeProgramSafely(*Program, "bugpoint.safe.out");
    if (Result) {
      outs() << "\n*** The \"safe\" i.e. 'known good' backend cannot match "
             << "the reference diff.  This may be due to a\n    front-end "
             << "bug or a bug in the original program, but this can also "
             << "happen if bugpoint isn't running the program with the "
             << "right flags or input.\n    I left the result of executing "
             << "the program with the \"safe\" backend in this file for "
             << "you: '" << *Result << "'.\n";
    }
    return Error::success();
  }

  DisambiguateGlobalSymbols(*Program);

  Expected<std::vector<Function *>> Funcs =
      DebugAMiscompilation(*this, TestCodeGenerator);
  if (Error E = Funcs.takeError())
    return E;

  // Split the module into the two halves of the program we want.
  ValueToValueMapTy VMap;
  std::unique_ptr<Module> ToNotCodeGen = CloneModule(getProgram(), VMap);
  std::unique_ptr<Module> ToCodeGen =
      SplitFunctionsOutOfModule(ToNotCodeGen.get(), *Funcs, VMap);

  // Condition the modules
  ToCodeGen =
      CleanupAndPrepareModules(*this, std::move(ToCodeGen), ToNotCodeGen.get());

  SmallString<128> TestModuleBC;
  int TestModuleFD;
  std::error_code EC = sys::fs::createTemporaryFile("bugpoint.test", "bc",
                                                    TestModuleFD, TestModuleBC);
  if (EC) {
    errs() << getToolName() << "Error making unique filename: " << EC.message()
           << "\n";
    exit(1);
  }

  if (writeProgramToFile(std::string(TestModuleBC), TestModuleFD, *ToCodeGen)) {
    errs() << "Error writing bitcode to `" << TestModuleBC << "'\nExiting.";
    exit(1);
  }

  // Make the shared library
  SmallString<128> SafeModuleBC;
  int SafeModuleFD;
  EC = sys::fs::createTemporaryFile("bugpoint.safe", "bc", SafeModuleFD,
                                    SafeModuleBC);
  if (EC) {
    errs() << getToolName() << "Error making unique filename: " << EC.message()
           << "\n";
    exit(1);
  }

  if (writeProgramToFile(std::string(SafeModuleBC), SafeModuleFD,
                         *ToNotCodeGen)) {
    errs() << "Error writing bitcode to `" << SafeModuleBC << "'\nExiting.";
    exit(1);
  }
  Expected<std::string> SharedObject =
      compileSharedObject(std::string(SafeModuleBC));
  if (Error E = SharedObject.takeError())
    return E;

  outs() << "You can reproduce the problem with the command line: \n";
  if (isExecutingJIT()) {
    outs() << "  lli -load " << *SharedObject << " " << TestModuleBC;
  } else {
    outs() << "  llc " << TestModuleBC << " -o " << TestModuleBC << ".s\n";
    outs() << "  cc " << *SharedObject << " " << TestModuleBC.str() << ".s -o "
           << TestModuleBC << ".exe\n";
    outs() << "  ./" << TestModuleBC << ".exe";
  }
  for (unsigned i = 0, e = InputArgv.size(); i != e; ++i)
    outs() << " " << InputArgv[i];
  outs() << '\n';
  outs() << "The shared object was created with:\n  llc -march=c "
         << SafeModuleBC.str() << " -o temporary.c\n"
         << "  cc -xc temporary.c -O2 -o " << *SharedObject;
  if (TargetTriple.getArch() == Triple::sparc)
    outs() << " -G"; // Compile a shared library, `-G' for Sparc
  else
    outs() << " -fPIC -shared"; // `-shared' for Linux/X86, maybe others

  outs() << " -fno-strict-aliasing\n";

  return Error::success();
}
