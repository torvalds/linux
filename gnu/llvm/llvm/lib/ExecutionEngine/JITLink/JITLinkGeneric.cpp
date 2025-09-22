//===--------- JITLinkGeneric.cpp - Generic JIT linker utilities ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic JITLinker utility class.
//
//===----------------------------------------------------------------------===//

#include "JITLinkGeneric.h"

#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/MemoryBuffer.h"

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {

JITLinkerBase::~JITLinkerBase() = default;

void JITLinkerBase::linkPhase1(std::unique_ptr<JITLinkerBase> Self) {

  LLVM_DEBUG({
    dbgs() << "Starting link phase 1 for graph " << G->getName() << "\n";
  });

  // Prune and optimize the graph.
  if (auto Err = runPasses(Passes.PrePrunePasses))
    return Ctx->notifyFailed(std::move(Err));

  LLVM_DEBUG({
    dbgs() << "Link graph \"" << G->getName() << "\" pre-pruning:\n";
    G->dump(dbgs());
  });

  prune(*G);

  LLVM_DEBUG({
    dbgs() << "Link graph \"" << G->getName() << "\" post-pruning:\n";
    G->dump(dbgs());
  });

  // Run post-pruning passes.
  if (auto Err = runPasses(Passes.PostPrunePasses))
    return Ctx->notifyFailed(std::move(Err));

  // Skip straight to phase 2 if the graph is empty with no associated actions.
  if (G->allocActions().empty() && llvm::all_of(G->sections(), [](Section &S) {
        return S.getMemLifetime() == orc::MemLifetime::NoAlloc;
      })) {
    linkPhase2(std::move(Self), nullptr);
    return;
  }

  Ctx->getMemoryManager().allocate(
      Ctx->getJITLinkDylib(), *G,
      [S = std::move(Self)](AllocResult AR) mutable {
        // FIXME: Once MSVC implements c++17 order of evaluation rules for calls
        // this can be simplified to
        //          S->linkPhase2(std::move(S), std::move(AR));
        auto *TmpSelf = S.get();
        TmpSelf->linkPhase2(std::move(S), std::move(AR));
      });
}

void JITLinkerBase::linkPhase2(std::unique_ptr<JITLinkerBase> Self,
                               AllocResult AR) {

  if (AR)
    Alloc = std::move(*AR);
  else
    return Ctx->notifyFailed(AR.takeError());

  LLVM_DEBUG({
    dbgs() << "Link graph \"" << G->getName()
           << "\" before post-allocation passes:\n";
    G->dump(dbgs());
  });

  // Run post-allocation passes.
  if (auto Err = runPasses(Passes.PostAllocationPasses))
    return abandonAllocAndBailOut(std::move(Self), std::move(Err));

  // Notify client that the defined symbols have been assigned addresses.
  LLVM_DEBUG(dbgs() << "Resolving symbols defined in " << G->getName() << "\n");

  if (auto Err = Ctx->notifyResolved(*G))
    return abandonAllocAndBailOut(std::move(Self), std::move(Err));

  auto ExternalSymbols = getExternalSymbolNames();

  // If there are no external symbols then proceed immediately with phase 3.
  if (ExternalSymbols.empty()) {
    LLVM_DEBUG({
      dbgs() << "No external symbols for " << G->getName()
             << ". Proceeding immediately with link phase 3.\n";
    });
    // FIXME: Once MSVC implements c++17 order of evaluation rules for calls
    // this can be simplified. See below.
    auto &TmpSelf = *Self;
    TmpSelf.linkPhase3(std::move(Self), AsyncLookupResult());
    return;
  }

  // Otherwise look up the externals.
  LLVM_DEBUG({
    dbgs() << "Issuing lookup for external symbols for " << G->getName()
           << " (may trigger materialization/linking of other graphs)...\n";
  });

  // We're about to hand off ownership of ourself to the continuation. Grab a
  // pointer to the context so that we can call it to initiate the lookup.
  //
  // FIXME: Once MSVC implements c++17 order of evaluation rules for calls this
  // can be simplified to:
  //
  // Ctx->lookup(std::move(UnresolvedExternals),
  //             [Self=std::move(Self)](Expected<AsyncLookupResult> Result) {
  //               Self->linkPhase3(std::move(Self), std::move(Result));
  //             });
  Ctx->lookup(std::move(ExternalSymbols),
              createLookupContinuation(
                  [S = std::move(Self)](
                      Expected<AsyncLookupResult> LookupResult) mutable {
                    auto &TmpSelf = *S;
                    TmpSelf.linkPhase3(std::move(S), std::move(LookupResult));
                  }));
}

void JITLinkerBase::linkPhase3(std::unique_ptr<JITLinkerBase> Self,
                               Expected<AsyncLookupResult> LR) {

  LLVM_DEBUG({
    dbgs() << "Starting link phase 3 for graph " << G->getName() << "\n";
  });

  // If the lookup failed, bail out.
  if (!LR)
    return abandonAllocAndBailOut(std::move(Self), LR.takeError());

  // Assign addresses to external addressables.
  applyLookupResult(*LR);

  LLVM_DEBUG({
    dbgs() << "Link graph \"" << G->getName()
           << "\" before pre-fixup passes:\n";
    G->dump(dbgs());
  });

  if (auto Err = runPasses(Passes.PreFixupPasses))
    return abandonAllocAndBailOut(std::move(Self), std::move(Err));

  LLVM_DEBUG({
    dbgs() << "Link graph \"" << G->getName() << "\" before copy-and-fixup:\n";
    G->dump(dbgs());
  });

  // Fix up block content.
  if (auto Err = fixUpBlocks(*G))
    return abandonAllocAndBailOut(std::move(Self), std::move(Err));

  LLVM_DEBUG({
    dbgs() << "Link graph \"" << G->getName() << "\" after copy-and-fixup:\n";
    G->dump(dbgs());
  });

  if (auto Err = runPasses(Passes.PostFixupPasses))
    return abandonAllocAndBailOut(std::move(Self), std::move(Err));

  // Skip straight to phase 4 if the graph has no allocation.
  if (!Alloc) {
    linkPhase4(std::move(Self), JITLinkMemoryManager::FinalizedAlloc{});
    return;
  }

  Alloc->finalize([S = std::move(Self)](FinalizeResult FR) mutable {
    // FIXME: Once MSVC implements c++17 order of evaluation rules for calls
    // this can be simplified to
    //          S->linkPhase2(std::move(S), std::move(AR));
    auto *TmpSelf = S.get();
    TmpSelf->linkPhase4(std::move(S), std::move(FR));
  });
}

void JITLinkerBase::linkPhase4(std::unique_ptr<JITLinkerBase> Self,
                               FinalizeResult FR) {

  LLVM_DEBUG({
    dbgs() << "Starting link phase 4 for graph " << G->getName() << "\n";
  });

  if (!FR)
    return Ctx->notifyFailed(FR.takeError());

  Ctx->notifyFinalized(std::move(*FR));

  LLVM_DEBUG({ dbgs() << "Link of graph " << G->getName() << " complete\n"; });
}

Error JITLinkerBase::runPasses(LinkGraphPassList &Passes) {
  for (auto &P : Passes)
    if (auto Err = P(*G))
      return Err;
  return Error::success();
}

JITLinkContext::LookupMap JITLinkerBase::getExternalSymbolNames() const {
  // Identify unresolved external symbols.
  JITLinkContext::LookupMap UnresolvedExternals;
  for (auto *Sym : G->external_symbols()) {
    assert(!Sym->getAddress() &&
           "External has already been assigned an address");
    assert(Sym->getName() != StringRef() && Sym->getName() != "" &&
           "Externals must be named");
    SymbolLookupFlags LookupFlags =
        Sym->isWeaklyReferenced() ? SymbolLookupFlags::WeaklyReferencedSymbol
                                  : SymbolLookupFlags::RequiredSymbol;
    UnresolvedExternals[Sym->getName()] = LookupFlags;
  }
  return UnresolvedExternals;
}

void JITLinkerBase::applyLookupResult(AsyncLookupResult Result) {
  for (auto *Sym : G->external_symbols()) {
    assert(Sym->getOffset() == 0 &&
           "External symbol is not at the start of its addressable block");
    assert(!Sym->getAddress() && "Symbol already resolved");
    assert(!Sym->isDefined() && "Symbol being resolved is already defined");
    auto ResultI = Result.find(Sym->getName());
    if (ResultI != Result.end()) {
      Sym->getAddressable().setAddress(ResultI->second.getAddress());
      Sym->setLinkage(ResultI->second.getFlags().isWeak() ? Linkage::Weak
                                                          : Linkage::Strong);
      Sym->setScope(ResultI->second.getFlags().isExported() ? Scope::Default
                                                            : Scope::Hidden);
    } else
      assert(Sym->isWeaklyReferenced() &&
             "Failed to resolve non-weak reference");
  }

  LLVM_DEBUG({
    dbgs() << "Externals after applying lookup result:\n";
    for (auto *Sym : G->external_symbols()) {
      dbgs() << "  " << Sym->getName() << ": "
             << formatv("{0:x16}", Sym->getAddress().getValue());
      switch (Sym->getLinkage()) {
      case Linkage::Strong:
        break;
      case Linkage::Weak:
        dbgs() << " (weak)";
        break;
      }
      switch (Sym->getScope()) {
      case Scope::Local:
        llvm_unreachable("External symbol should not have local linkage");
      case Scope::Hidden:
        break;
      case Scope::Default:
        dbgs() << " (exported)";
        break;
      }
      dbgs() << "\n";
    }
  });
}

void JITLinkerBase::abandonAllocAndBailOut(std::unique_ptr<JITLinkerBase> Self,
                                           Error Err) {
  assert(Err && "Should not be bailing out on success value");
  assert(Alloc && "can not call abandonAllocAndBailOut before allocation");
  Alloc->abandon([S = std::move(Self), E1 = std::move(Err)](Error E2) mutable {
    S->Ctx->notifyFailed(joinErrors(std::move(E1), std::move(E2)));
  });
}

void prune(LinkGraph &G) {
  std::vector<Symbol *> Worklist;
  DenseSet<Block *> VisitedBlocks;

  // Build the initial worklist from all symbols initially live.
  for (auto *Sym : G.defined_symbols())
    if (Sym->isLive())
      Worklist.push_back(Sym);

  // Propagate live flags to all symbols reachable from the initial live set.
  while (!Worklist.empty()) {
    auto *Sym = Worklist.back();
    Worklist.pop_back();

    auto &B = Sym->getBlock();

    // Skip addressables that we've visited before.
    if (VisitedBlocks.count(&B))
      continue;

    VisitedBlocks.insert(&B);

    for (auto &E : Sym->getBlock().edges()) {
      // If the edge target is a defined symbol that is being newly marked live
      // then add it to the worklist.
      if (E.getTarget().isDefined() && !E.getTarget().isLive())
        Worklist.push_back(&E.getTarget());

      // Mark the target live.
      E.getTarget().setLive(true);
    }
  }

  // Collect all defined symbols to remove, then remove them.
  {
    LLVM_DEBUG(dbgs() << "Dead-stripping defined symbols:\n");
    std::vector<Symbol *> SymbolsToRemove;
    for (auto *Sym : G.defined_symbols())
      if (!Sym->isLive())
        SymbolsToRemove.push_back(Sym);
    for (auto *Sym : SymbolsToRemove) {
      LLVM_DEBUG(dbgs() << "  " << *Sym << "...\n");
      G.removeDefinedSymbol(*Sym);
    }
  }

  // Delete any unused blocks.
  {
    LLVM_DEBUG(dbgs() << "Dead-stripping blocks:\n");
    std::vector<Block *> BlocksToRemove;
    for (auto *B : G.blocks())
      if (!VisitedBlocks.count(B))
        BlocksToRemove.push_back(B);
    for (auto *B : BlocksToRemove) {
      LLVM_DEBUG(dbgs() << "  " << *B << "...\n");
      G.removeBlock(*B);
    }
  }

  // Collect all external symbols to remove, then remove them.
  {
    LLVM_DEBUG(dbgs() << "Removing unused external symbols:\n");
    std::vector<Symbol *> SymbolsToRemove;
    for (auto *Sym : G.external_symbols())
      if (!Sym->isLive())
        SymbolsToRemove.push_back(Sym);
    for (auto *Sym : SymbolsToRemove) {
      LLVM_DEBUG(dbgs() << "  " << *Sym << "...\n");
      G.removeExternalSymbol(*Sym);
    }
  }
}

} // end namespace jitlink
} // end namespace llvm
