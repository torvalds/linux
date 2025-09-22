//===-- Internalize.cpp - Mark functions internal -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass loops over all of the functions and variables in the input module.
// If the function or variable does not need to be preserved according to the
// client supplied callback, it is marked as internal.
//
// This transformation would not be legal in a regular compilation, but it gets
// extra information from the linker about what is safe.
//
// For example: Internalizing a function with external linkage. Only if we are
// told it is only used from within this module, it is safe to do it.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/IPO.h"
using namespace llvm;

#define DEBUG_TYPE "internalize"

STATISTIC(NumAliases, "Number of aliases internalized");
STATISTIC(NumFunctions, "Number of functions internalized");
STATISTIC(NumGlobals, "Number of global vars internalized");

// APIFile - A file which contains a list of symbol glob patterns that should
// not be marked external.
static cl::opt<std::string>
    APIFile("internalize-public-api-file", cl::value_desc("filename"),
            cl::desc("A file containing list of symbol names to preserve"));

// APIList - A list of symbol glob patterns that should not be marked internal.
static cl::list<std::string>
    APIList("internalize-public-api-list", cl::value_desc("list"),
            cl::desc("A list of symbol names to preserve"), cl::CommaSeparated);

namespace {
// Helper to load an API list to preserve from file and expose it as a functor
// for internalization.
class PreserveAPIList {
public:
  PreserveAPIList() {
    if (!APIFile.empty())
      LoadFile(APIFile);
    for (StringRef Pattern : APIList)
      addGlob(Pattern);
  }

  bool operator()(const GlobalValue &GV) {
    return llvm::any_of(
        ExternalNames, [&](GlobPattern &GP) { return GP.match(GV.getName()); });
  }

private:
  // Contains the set of symbols loaded from file
  SmallVector<GlobPattern> ExternalNames;

  void addGlob(StringRef Pattern) {
    auto GlobOrErr = GlobPattern::create(Pattern);
    if (!GlobOrErr) {
      errs() << "WARNING: when loading pattern: '"
             << toString(GlobOrErr.takeError()) << "' ignoring";
      return;
    }
    ExternalNames.emplace_back(std::move(*GlobOrErr));
  }

  void LoadFile(StringRef Filename) {
    // Load the APIFile...
    ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
        MemoryBuffer::getFile(Filename);
    if (!BufOrErr) {
      errs() << "WARNING: Internalize couldn't load file '" << Filename
             << "'! Continuing as if it's empty.\n";
      return; // Just continue as if the file were empty
    }
    Buf = std::move(*BufOrErr);
    for (line_iterator I(*Buf, true), E; I != E; ++I)
      addGlob(*I);
  }

  std::shared_ptr<MemoryBuffer> Buf;
};
} // end anonymous namespace

bool InternalizePass::shouldPreserveGV(const GlobalValue &GV) {
  // Function must be defined here
  if (GV.isDeclaration())
    return true;

  // Available externally is really just a "declaration with a body".
  if (GV.hasAvailableExternallyLinkage())
    return true;

  // Assume that dllexported symbols are referenced elsewhere
  if (GV.hasDLLExportStorageClass())
    return true;

  // As the name suggests, externally initialized variables need preserving as
  // they would be initialized elsewhere externally.
  if (const auto *G = dyn_cast<GlobalVariable>(&GV))
    if (G->isExternallyInitialized())
      return true;

  // Already local, has nothing to do.
  if (GV.hasLocalLinkage())
    return false;

  // Check some special cases
  if (AlwaysPreserved.count(GV.getName()))
    return true;

  return MustPreserveGV(GV);
}

bool InternalizePass::maybeInternalize(
    GlobalValue &GV, DenseMap<const Comdat *, ComdatInfo> &ComdatMap) {
  SmallString<0> ComdatName;
  if (Comdat *C = GV.getComdat()) {
    // For GlobalAlias, C is the aliasee object's comdat which may have been
    // redirected. So ComdatMap may not contain C.
    if (ComdatMap.lookup(C).External)
      return false;

    if (auto *GO = dyn_cast<GlobalObject>(&GV)) {
      // If a comdat with one member is not externally visible, we can drop it.
      // Otherwise, the comdat can be used to establish dependencies among the
      // group of sections. Thus we have to keep the comdat but switch it to
      // nodeduplicate.
      // Note: nodeduplicate is not necessary for COFF. wasm doesn't support
      // nodeduplicate.
      ComdatInfo &Info = ComdatMap.find(C)->second;
      if (Info.Size == 1)
        GO->setComdat(nullptr);
      else if (!IsWasm)
        C->setSelectionKind(Comdat::NoDeduplicate);
    }

    if (GV.hasLocalLinkage())
      return false;
  } else {
    if (GV.hasLocalLinkage())
      return false;

    if (shouldPreserveGV(GV))
      return false;
  }

  GV.setVisibility(GlobalValue::DefaultVisibility);
  GV.setLinkage(GlobalValue::InternalLinkage);
  return true;
}

// If GV is part of a comdat and is externally visible, update the comdat size
// and keep track of its comdat so that we don't internalize any of its members.
void InternalizePass::checkComdat(
    GlobalValue &GV, DenseMap<const Comdat *, ComdatInfo> &ComdatMap) {
  Comdat *C = GV.getComdat();
  if (!C)
    return;

  ComdatInfo &Info = ComdatMap.try_emplace(C).first->second;
  ++Info.Size;
  if (shouldPreserveGV(GV))
    Info.External = true;
}

bool InternalizePass::internalizeModule(Module &M) {
  bool Changed = false;

  SmallVector<GlobalValue *, 4> Used;
  collectUsedGlobalVariables(M, Used, false);

  // Collect comdat size and visiblity information for the module.
  DenseMap<const Comdat *, ComdatInfo> ComdatMap;
  if (!M.getComdatSymbolTable().empty()) {
    for (Function &F : M)
      checkComdat(F, ComdatMap);
    for (GlobalVariable &GV : M.globals())
      checkComdat(GV, ComdatMap);
    for (GlobalAlias &GA : M.aliases())
      checkComdat(GA, ComdatMap);
  }

  // We must assume that globals in llvm.used have a reference that not even
  // the linker can see, so we don't internalize them.
  // For llvm.compiler.used the situation is a bit fuzzy. The assembler and
  // linker can drop those symbols. If this pass is running as part of LTO,
  // one might think that it could just drop llvm.compiler.used. The problem
  // is that even in LTO llvm doesn't see every reference. For example,
  // we don't see references from function local inline assembly. To be
  // conservative, we internalize symbols in llvm.compiler.used, but we
  // keep llvm.compiler.used so that the symbol is not deleted by llvm.
  for (GlobalValue *V : Used) {
    AlwaysPreserved.insert(V->getName());
  }

  // Never internalize the llvm.used symbol.  It is used to implement
  // attribute((used)).
  // FIXME: Shouldn't this just filter on llvm.metadata section??
  AlwaysPreserved.insert("llvm.used");
  AlwaysPreserved.insert("llvm.compiler.used");

  // Never internalize anchors used by the machine module info, else the info
  // won't find them.  (see MachineModuleInfo.)
  AlwaysPreserved.insert("llvm.global_ctors");
  AlwaysPreserved.insert("llvm.global_dtors");
  AlwaysPreserved.insert("llvm.global.annotations");

  // Never internalize symbols code-gen inserts.
  // FIXME: We should probably add this (and the __stack_chk_guard) via some
  // type of call-back in CodeGen.
  AlwaysPreserved.insert("__stack_chk_fail");
  if (Triple(M.getTargetTriple()).isOSAIX())
    AlwaysPreserved.insert("__ssp_canary_word");
  else
    AlwaysPreserved.insert("__stack_chk_guard");

  // Mark all functions not in the api as internal.
  IsWasm = Triple(M.getTargetTriple()).isOSBinFormatWasm();
  for (Function &I : M) {
    if (!maybeInternalize(I, ComdatMap))
      continue;
    Changed = true;

    ++NumFunctions;
    LLVM_DEBUG(dbgs() << "Internalizing func " << I.getName() << "\n");
  }

  // Mark all global variables with initializers that are not in the api as
  // internal as well.
  for (auto &GV : M.globals()) {
    if (!maybeInternalize(GV, ComdatMap))
      continue;
    Changed = true;

    ++NumGlobals;
    LLVM_DEBUG(dbgs() << "Internalized gvar " << GV.getName() << "\n");
  }

  // Mark all aliases that are not in the api as internal as well.
  for (auto &GA : M.aliases()) {
    if (!maybeInternalize(GA, ComdatMap))
      continue;
    Changed = true;

    ++NumAliases;
    LLVM_DEBUG(dbgs() << "Internalized alias " << GA.getName() << "\n");
  }

  return Changed;
}

InternalizePass::InternalizePass() : MustPreserveGV(PreserveAPIList()) {}

PreservedAnalyses InternalizePass::run(Module &M, ModuleAnalysisManager &AM) {
  if (!internalizeModule(M))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}
