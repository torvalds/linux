//===- MetaRenamer.cpp - Rename everything with metasyntatic names --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass renames everything with metasyntatic names. The intent is to use
// this pass after bugpoint reduction to conceal the nature of the original
// program.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/MetaRenamer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<std::string> RenameExcludeFunctionPrefixes(
    "rename-exclude-function-prefixes",
    cl::desc("Prefixes for functions that don't need to be renamed, separated "
             "by a comma"),
    cl::Hidden);

static cl::opt<std::string> RenameExcludeAliasPrefixes(
    "rename-exclude-alias-prefixes",
    cl::desc("Prefixes for aliases that don't need to be renamed, separated "
             "by a comma"),
    cl::Hidden);

static cl::opt<std::string> RenameExcludeGlobalPrefixes(
    "rename-exclude-global-prefixes",
    cl::desc(
        "Prefixes for global values that don't need to be renamed, separated "
        "by a comma"),
    cl::Hidden);

static cl::opt<std::string> RenameExcludeStructPrefixes(
    "rename-exclude-struct-prefixes",
    cl::desc("Prefixes for structs that don't need to be renamed, separated "
             "by a comma"),
    cl::Hidden);

static cl::opt<bool>
    RenameOnlyInst("rename-only-inst", cl::init(false),
                   cl::desc("only rename the instructions in the function"),
                   cl::Hidden);

static const char *const metaNames[] = {
  // See http://en.wikipedia.org/wiki/Metasyntactic_variable
  "foo", "bar", "baz", "quux", "barney", "snork", "zot", "blam", "hoge",
  "wibble", "wobble", "widget", "wombat", "ham", "eggs", "pluto", "spam"
};

namespace {
// This PRNG is from the ISO C spec. It is intentionally simple and
// unsuitable for cryptographic use. We're just looking for enough
// variety to surprise and delight users.
struct PRNG {
  unsigned long next;

  void srand(unsigned int seed) { next = seed; }

  int rand() {
    next = next * 1103515245 + 12345;
    return (unsigned int)(next / 65536) % 32768;
  }
};

struct Renamer {
  Renamer(unsigned int seed) { prng.srand(seed); }

  const char *newName() {
    return metaNames[prng.rand() % std::size(metaNames)];
  }

  PRNG prng;
};

static void
parseExcludedPrefixes(StringRef PrefixesStr,
                      SmallVectorImpl<StringRef> &ExcludedPrefixes) {
  for (;;) {
    auto PrefixesSplit = PrefixesStr.split(',');
    if (PrefixesSplit.first.empty())
      break;
    ExcludedPrefixes.push_back(PrefixesSplit.first);
    PrefixesStr = PrefixesSplit.second;
  }
}

void MetaRenameOnlyInstructions(Function &F) {
  for (auto &I : instructions(F))
    if (!I.getType()->isVoidTy() && I.getName().empty())
      I.setName(I.getOpcodeName());
}

void MetaRename(Function &F) {
  for (Argument &Arg : F.args())
    if (!Arg.getType()->isVoidTy())
      Arg.setName("arg");

  for (auto &BB : F) {
    BB.setName("bb");

    for (auto &I : BB)
      if (!I.getType()->isVoidTy())
        I.setName(I.getOpcodeName());
  }
}

void MetaRename(Module &M,
                function_ref<TargetLibraryInfo &(Function &)> GetTLI) {
  // Seed our PRNG with simple additive sum of ModuleID. We're looking to
  // simply avoid always having the same function names, and we need to
  // remain deterministic.
  unsigned int randSeed = 0;
  for (auto C : M.getModuleIdentifier())
    randSeed += C;

  Renamer renamer(randSeed);

  SmallVector<StringRef, 8> ExcludedAliasesPrefixes;
  SmallVector<StringRef, 8> ExcludedGlobalsPrefixes;
  SmallVector<StringRef, 8> ExcludedStructsPrefixes;
  SmallVector<StringRef, 8> ExcludedFuncPrefixes;
  parseExcludedPrefixes(RenameExcludeAliasPrefixes, ExcludedAliasesPrefixes);
  parseExcludedPrefixes(RenameExcludeGlobalPrefixes, ExcludedGlobalsPrefixes);
  parseExcludedPrefixes(RenameExcludeStructPrefixes, ExcludedStructsPrefixes);
  parseExcludedPrefixes(RenameExcludeFunctionPrefixes, ExcludedFuncPrefixes);

  auto IsNameExcluded = [](StringRef &Name,
                           SmallVectorImpl<StringRef> &ExcludedPrefixes) {
    return any_of(ExcludedPrefixes,
                  [&Name](auto &Prefix) { return Name.starts_with(Prefix); });
  };

  // Leave library functions alone because their presence or absence could
  // affect the behavior of other passes.
  auto ExcludeLibFuncs = [&](Function &F) {
    LibFunc Tmp;
    StringRef Name = F.getName();
    return Name.starts_with("llvm.") || (!Name.empty() && Name[0] == 1) ||
           GetTLI(F).getLibFunc(F, Tmp) ||
           IsNameExcluded(Name, ExcludedFuncPrefixes);
  };

  if (RenameOnlyInst) {
    // Rename all functions
    for (auto &F : M) {
      if (ExcludeLibFuncs(F))
        continue;
      MetaRenameOnlyInstructions(F);
    }
    return;
  }

  // Rename all aliases
  for (GlobalAlias &GA : M.aliases()) {
    StringRef Name = GA.getName();
    if (Name.starts_with("llvm.") || (!Name.empty() && Name[0] == 1) ||
        IsNameExcluded(Name, ExcludedAliasesPrefixes))
      continue;

    GA.setName("alias");
  }

  // Rename all global variables
  for (GlobalVariable &GV : M.globals()) {
    StringRef Name = GV.getName();
    if (Name.starts_with("llvm.") || (!Name.empty() && Name[0] == 1) ||
        IsNameExcluded(Name, ExcludedGlobalsPrefixes))
      continue;

    GV.setName("global");
  }

  // Rename all struct types
  TypeFinder StructTypes;
  StructTypes.run(M, true);
  for (StructType *STy : StructTypes) {
    StringRef Name = STy->getName();
    if (STy->isLiteral() || Name.empty() ||
        IsNameExcluded(Name, ExcludedStructsPrefixes))
      continue;

    SmallString<128> NameStorage;
    STy->setName(
        (Twine("struct.") + renamer.newName()).toStringRef(NameStorage));
  }

  // Rename all functions
  for (auto &F : M) {
    if (ExcludeLibFuncs(F))
      continue;

    // Leave @main alone. The output of -metarenamer might be passed to
    // lli for execution and the latter needs a main entry point.
    if (F.getName() != "main")
      F.setName(renamer.newName());

    MetaRename(F);
  }
}

} // end anonymous namespace

PreservedAnalyses MetaRenamerPass::run(Module &M, ModuleAnalysisManager &AM) {
  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  auto GetTLI = [&FAM](Function &F) -> TargetLibraryInfo & {
    return FAM.getResult<TargetLibraryAnalysis>(F);
  };
  MetaRename(M, GetTLI);

  return PreservedAnalyses::all();
}
