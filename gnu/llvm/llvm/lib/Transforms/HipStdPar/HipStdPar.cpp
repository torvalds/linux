//===----- HipStdPar.cpp - HIP C++ Standard Parallelism Support Passes ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file implements two passes that enable HIP C++ Standard Parallelism
// Support:
//
// 1. AcceleratorCodeSelection (required): Given that only algorithms are
//    accelerated, and that the accelerated implementation exists in the form of
//    a compute kernel, we assume that only the kernel, and all functions
//    reachable from it, constitute code that the user expects the accelerator
//    to execute. Thus, we identify the set of all functions reachable from
//    kernels, and then remove all unreachable ones. This last part is necessary
//    because it is possible for code that the user did not expect to execute on
//    an accelerator to contain constructs that cannot be handled by the target
//    BE, which cannot be provably demonstrated to be dead code in general, and
//    thus can lead to mis-compilation. The degenerate case of this is when a
//    Module contains no kernels (the parent TU had no algorithm invocations fit
//    for acceleration), which we handle by completely emptying said module.
//    **NOTE**: The above does not handle indirectly reachable functions i.e.
//              it is possible to obtain a case where the target of an indirect
//              call is otherwise unreachable and thus is removed; this
//              restriction is aligned with the current `-hipstdpar` limitations
//              and will be relaxed in the future.
//
// 2. AllocationInterposition (required only when on-demand paging is
//    unsupported): Some accelerators or operating systems might not support
//    transparent on-demand paging. Thus, they would only be able to access
//    memory that is allocated by an accelerator-aware mechanism. For such cases
//    the user can opt into enabling allocation / deallocation interposition,
//    whereby we replace calls to known allocation / deallocation functions with
//    calls to runtime implemented equivalents that forward the requests to
//    accelerator-aware interfaces. We also support freeing system allocated
//    memory that ends up in one of the runtime equivalents, since this can
//    happen if e.g. a library that was compiled without interposition returns
//    an allocation that can be validly passed to `free`.
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/HipStdPar/HipStdPar.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <cassert>
#include <string>
#include <utility>

using namespace llvm;

template<typename T>
static inline void eraseFromModule(T &ToErase) {
  ToErase.replaceAllUsesWith(PoisonValue::get(ToErase.getType()));
  ToErase.eraseFromParent();
}

static inline bool checkIfSupported(GlobalVariable &G) {
  if (!G.isThreadLocal())
    return true;

  G.dropDroppableUses();

  if (!G.isConstantUsed())
    return true;

  std::string W;
  raw_string_ostream OS(W);

  OS << "Accelerator does not support the thread_local variable "
    << G.getName();

  Instruction *I = nullptr;
  SmallVector<User *> Tmp(G.user_begin(), G.user_end());
  SmallPtrSet<User *, 5> Visited;
  do {
    auto U = std::move(Tmp.back());
    Tmp.pop_back();

    if (Visited.contains(U))
      continue;

    if (isa<Instruction>(U))
      I = cast<Instruction>(U);
    else
      Tmp.insert(Tmp.end(), U->user_begin(), U->user_end());

    Visited.insert(U);
  } while (!I && !Tmp.empty());

  assert(I && "thread_local global should have at least one non-constant use.");

  G.getContext().diagnose(
    DiagnosticInfoUnsupported(*I->getParent()->getParent(), W,
                              I->getDebugLoc(), DS_Error));

  return false;
}

static inline void clearModule(Module &M) { // TODO: simplify.
  while (!M.functions().empty())
    eraseFromModule(*M.begin());
  while (!M.globals().empty())
    eraseFromModule(*M.globals().begin());
  while (!M.aliases().empty())
    eraseFromModule(*M.aliases().begin());
  while (!M.ifuncs().empty())
    eraseFromModule(*M.ifuncs().begin());
}

static inline void maybeHandleGlobals(Module &M) {
  unsigned GlobAS = M.getDataLayout().getDefaultGlobalsAddressSpace();
  for (auto &&G : M.globals()) { // TODO: should we handle these in the FE?
    if (!checkIfSupported(G))
      return clearModule(M);

    if (G.isThreadLocal())
      continue;
    if (G.isConstant())
      continue;
    if (G.getAddressSpace() != GlobAS)
      continue;
    if (G.getLinkage() != GlobalVariable::ExternalLinkage)
      continue;

    G.setLinkage(GlobalVariable::ExternalWeakLinkage);
    G.setInitializer(nullptr);
    G.setExternallyInitialized(true);
  }
}

template<unsigned N>
static inline void removeUnreachableFunctions(
  const SmallPtrSet<const Function *, N>& Reachable, Module &M) {
  removeFromUsedLists(M, [&](Constant *C) {
    if (auto F = dyn_cast<Function>(C))
      return !Reachable.contains(F);

    return false;
  });

  SmallVector<std::reference_wrapper<Function>> ToRemove;
  copy_if(M, std::back_inserter(ToRemove), [&](auto &&F) {
    return !F.isIntrinsic() && !Reachable.contains(&F);
  });

  for_each(ToRemove, eraseFromModule<Function>);
}

static inline bool isAcceleratorExecutionRoot(const Function *F) {
    if (!F)
      return false;

    return F->getCallingConv() == CallingConv::AMDGPU_KERNEL;
}

static inline bool checkIfSupported(const Function *F, const CallBase *CB) {
  const auto Dx = F->getName().rfind("__hipstdpar_unsupported");

  if (Dx == StringRef::npos)
    return true;

  const auto N = F->getName().substr(0, Dx);

  std::string W;
  raw_string_ostream OS(W);

  if (N == "__ASM")
    OS << "Accelerator does not support the ASM block:\n"
      << cast<ConstantDataArray>(CB->getArgOperand(0))->getAsCString();
  else
    OS << "Accelerator does not support the " << N << " function.";

  auto Caller = CB->getParent()->getParent();

  Caller->getContext().diagnose(
    DiagnosticInfoUnsupported(*Caller, W, CB->getDebugLoc(), DS_Error));

  return false;
}

PreservedAnalyses
  HipStdParAcceleratorCodeSelectionPass::run(Module &M,
                                             ModuleAnalysisManager &MAM) {
  auto &CGA = MAM.getResult<CallGraphAnalysis>(M);

  SmallPtrSet<const Function *, 32> Reachable;
  for (auto &&CGN : CGA) {
    if (!isAcceleratorExecutionRoot(CGN.first))
      continue;

    Reachable.insert(CGN.first);

    SmallVector<const Function *> Tmp({CGN.first});
    do {
      auto F = std::move(Tmp.back());
      Tmp.pop_back();

      for (auto &&N : *CGA[F]) {
        if (!N.second)
          continue;
        if (!N.second->getFunction())
          continue;
        if (Reachable.contains(N.second->getFunction()))
          continue;

        if (!checkIfSupported(N.second->getFunction(),
                              dyn_cast<CallBase>(*N.first)))
          return PreservedAnalyses::none();

        Reachable.insert(N.second->getFunction());
        Tmp.push_back(N.second->getFunction());
      }
    } while (!std::empty(Tmp));
  }

  if (std::empty(Reachable))
    clearModule(M);
  else
    removeUnreachableFunctions(Reachable, M);

  maybeHandleGlobals(M);

  return PreservedAnalyses::none();
}

static constexpr std::pair<StringLiteral, StringLiteral> ReplaceMap[]{
  {"aligned_alloc",             "__hipstdpar_aligned_alloc"},
  {"calloc",                    "__hipstdpar_calloc"},
  {"free",                      "__hipstdpar_free"},
  {"malloc",                    "__hipstdpar_malloc"},
  {"memalign",                  "__hipstdpar_aligned_alloc"},
  {"posix_memalign",            "__hipstdpar_posix_aligned_alloc"},
  {"realloc",                   "__hipstdpar_realloc"},
  {"reallocarray",              "__hipstdpar_realloc_array"},
  {"_ZdaPv",                    "__hipstdpar_operator_delete"},
  {"_ZdaPvm",                   "__hipstdpar_operator_delete_sized"},
  {"_ZdaPvSt11align_val_t",     "__hipstdpar_operator_delete_aligned"},
  {"_ZdaPvmSt11align_val_t",    "__hipstdpar_operator_delete_aligned_sized"},
  {"_ZdlPv",                    "__hipstdpar_operator_delete"},
  {"_ZdlPvm",                   "__hipstdpar_operator_delete_sized"},
  {"_ZdlPvSt11align_val_t",     "__hipstdpar_operator_delete_aligned"},
  {"_ZdlPvmSt11align_val_t",    "__hipstdpar_operator_delete_aligned_sized"},
  {"_Znam",                     "__hipstdpar_operator_new"},
  {"_ZnamRKSt9nothrow_t",       "__hipstdpar_operator_new_nothrow"},
  {"_ZnamSt11align_val_t",      "__hipstdpar_operator_new_aligned"},
  {"_ZnamSt11align_val_tRKSt9nothrow_t",
                                "__hipstdpar_operator_new_aligned_nothrow"},

  {"_Znwm",                     "__hipstdpar_operator_new"},
  {"_ZnwmRKSt9nothrow_t",       "__hipstdpar_operator_new_nothrow"},
  {"_ZnwmSt11align_val_t",      "__hipstdpar_operator_new_aligned"},
  {"_ZnwmSt11align_val_tRKSt9nothrow_t",
                                "__hipstdpar_operator_new_aligned_nothrow"},
  {"__builtin_calloc",          "__hipstdpar_calloc"},
  {"__builtin_free",            "__hipstdpar_free"},
  {"__builtin_malloc",          "__hipstdpar_malloc"},
  {"__builtin_operator_delete", "__hipstdpar_operator_delete"},
  {"__builtin_operator_new",    "__hipstdpar_operator_new"},
  {"__builtin_realloc",         "__hipstdpar_realloc"},
  {"__libc_calloc",             "__hipstdpar_calloc"},
  {"__libc_free",               "__hipstdpar_free"},
  {"__libc_malloc",             "__hipstdpar_malloc"},
  {"__libc_memalign",           "__hipstdpar_aligned_alloc"},
  {"__libc_realloc",            "__hipstdpar_realloc"}
};

PreservedAnalyses
HipStdParAllocationInterpositionPass::run(Module &M, ModuleAnalysisManager&) {
  SmallDenseMap<StringRef, StringRef> AllocReplacements(std::cbegin(ReplaceMap),
                                                        std::cend(ReplaceMap));

  for (auto &&F : M) {
    if (!F.hasName())
      continue;
    if (!AllocReplacements.contains(F.getName()))
      continue;

    if (auto R = M.getFunction(AllocReplacements[F.getName()])) {
      F.replaceAllUsesWith(R);
    } else {
      std::string W;
      raw_string_ostream OS(W);

      OS << "cannot be interposed, missing: " << AllocReplacements[F.getName()]
        << ". Tried to run the allocation interposition pass without the "
        << "replacement functions available.";

      F.getContext().diagnose(DiagnosticInfoUnsupported(F, W,
                                                        F.getSubprogram(),
                                                        DS_Warning));
    }
  }

  if (auto F = M.getFunction("__hipstdpar_hidden_free")) {
    auto LibcFree = M.getOrInsertFunction("__libc_free", F->getFunctionType(),
                                          F->getAttributes());
    F->replaceAllUsesWith(LibcFree.getCallee());

    eraseFromModule(*F);
  }

  return PreservedAnalyses::none();
}
