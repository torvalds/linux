//===------ JITLinkGeneric.h - Generic JIT linker utilities -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic JITLinker utilities. E.g. graph pruning, eh-frame parsing.
//
//===----------------------------------------------------------------------===//

#ifndef LIB_EXECUTIONENGINE_JITLINK_JITLINKGENERIC_H
#define LIB_EXECUTIONENGINE_JITLINK_JITLINKGENERIC_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {

/// Base class for a JIT linker.
///
/// A JITLinkerBase instance links one object file into an ongoing JIT
/// session. Symbol resolution and finalization operations are pluggable,
/// and called using continuation passing (passing a continuation for the
/// remaining linker work) to allow them to be performed asynchronously.
class JITLinkerBase {
public:
  JITLinkerBase(std::unique_ptr<JITLinkContext> Ctx,
                std::unique_ptr<LinkGraph> G, PassConfiguration Passes)
      : Ctx(std::move(Ctx)), G(std::move(G)), Passes(std::move(Passes)) {
    assert(this->Ctx && "Ctx can not be null");
    assert(this->G && "G can not be null");
  }

  virtual ~JITLinkerBase();

protected:
  using InFlightAlloc = JITLinkMemoryManager::InFlightAlloc;
  using AllocResult = Expected<std::unique_ptr<InFlightAlloc>>;
  using FinalizeResult = Expected<JITLinkMemoryManager::FinalizedAlloc>;

  // Returns a reference to the graph being linked.
  LinkGraph &getGraph() { return *G; }

  // Returns true if the context says that the linker should add default
  // passes. This can be used by JITLinkerBase implementations when deciding
  // whether they should add default passes.
  bool shouldAddDefaultTargetPasses(const Triple &TT) {
    return Ctx->shouldAddDefaultTargetPasses(TT);
  }

  // Returns the PassConfiguration for this instance. This can be used by
  // JITLinkerBase implementations to add late passes that reference their
  // own data structures (e.g. for ELF implementations to locate / construct
  // a GOT start symbol prior to fixup).
  PassConfiguration &getPassConfig() { return Passes; }

  // Phase 1:
  //   1.1: Run pre-prune passes
  //   1.2: Prune graph
  //   1.3: Run post-prune passes
  //   1.4: Allocate memory.
  void linkPhase1(std::unique_ptr<JITLinkerBase> Self);

  // Phase 2:
  //   2.2: Run post-allocation passes
  //   2.3: Notify context of final assigned symbol addresses
  //   2.4: Identify external symbols and make an async call to resolve
  void linkPhase2(std::unique_ptr<JITLinkerBase> Self, AllocResult AR);

  // Phase 3:
  //   3.1: Apply resolution results
  //   3.2: Run pre-fixup passes
  //   3.3: Fix up block contents
  //   3.4: Run post-fixup passes
  //   3.5: Make an async call to transfer and finalize memory.
  void linkPhase3(std::unique_ptr<JITLinkerBase> Self,
                  Expected<AsyncLookupResult> LookupResult);

  // Phase 4:
  //   4.1: Call OnFinalized callback, handing off allocation.
  void linkPhase4(std::unique_ptr<JITLinkerBase> Self, FinalizeResult FR);

private:
  // Run all passes in the given pass list, bailing out immediately if any pass
  // returns an error.
  Error runPasses(LinkGraphPassList &Passes);

  // Copy block contents and apply relocations.
  // Implemented in JITLinker.
  virtual Error fixUpBlocks(LinkGraph &G) const = 0;

  JITLinkContext::LookupMap getExternalSymbolNames() const;
  void applyLookupResult(AsyncLookupResult LR);
  void abandonAllocAndBailOut(std::unique_ptr<JITLinkerBase> Self, Error Err);

  std::unique_ptr<JITLinkContext> Ctx;
  std::unique_ptr<LinkGraph> G;
  PassConfiguration Passes;
  std::unique_ptr<InFlightAlloc> Alloc;
};

template <typename LinkerImpl> class JITLinker : public JITLinkerBase {
public:
  using JITLinkerBase::JITLinkerBase;

  /// Link constructs a LinkerImpl instance and calls linkPhase1.
  /// Link should be called with the constructor arguments for LinkerImpl, which
  /// will be forwarded to the constructor.
  template <typename... ArgTs> static void link(ArgTs &&... Args) {
    auto L = std::make_unique<LinkerImpl>(std::forward<ArgTs>(Args)...);

    // Ownership of the linker is passed into the linker's doLink function to
    // allow it to be passed on to async continuations.
    //
    // FIXME: Remove LTmp once we have c++17.
    // C++17 sequencing rules guarantee that function name expressions are
    // sequenced before arguments, so L->linkPhase1(std::move(L), ...) will be
    // well formed.
    auto &LTmp = *L;
    LTmp.linkPhase1(std::move(L));
  }

private:
  const LinkerImpl &impl() const {
    return static_cast<const LinkerImpl &>(*this);
  }

  Error fixUpBlocks(LinkGraph &G) const override {
    LLVM_DEBUG(dbgs() << "Fixing up blocks:\n");

    for (auto &Sec : G.sections()) {
      bool NoAllocSection = Sec.getMemLifetime() == orc::MemLifetime::NoAlloc;

      for (auto *B : Sec.blocks()) {
        LLVM_DEBUG(dbgs() << "  " << *B << ":\n");

        // Copy Block data and apply fixups.
        LLVM_DEBUG(dbgs() << "    Applying fixups.\n");
        assert((!B->isZeroFill() || all_of(B->edges(),
                                           [](const Edge &E) {
                                             return E.getKind() ==
                                                    Edge::KeepAlive;
                                           })) &&
               "Non-KeepAlive edges in zero-fill block?");

        // If this is a no-alloc section then copy the block content into
        // memory allocated on the Graph's allocator (if it hasn't been
        // already).
        if (NoAllocSection)
          (void)B->getMutableContent(G);

        for (auto &E : B->edges()) {

          // Skip non-relocation edges.
          if (!E.isRelocation())
            continue;

          // If B is a block in a Standard or Finalize section then make sure
          // that no edges point to symbols in NoAlloc sections.
          assert((NoAllocSection || !E.getTarget().isDefined() ||
                  E.getTarget().getBlock().getSection().getMemLifetime() !=
                      orc::MemLifetime::NoAlloc) &&
                 "Block in allocated section has edge pointing to no-alloc "
                 "section");

          // Dispatch to LinkerImpl for fixup.
          if (auto Err = impl().applyFixup(G, *B, E))
            return Err;
        }
      }
    }

    return Error::success();
  }
};

/// Removes dead symbols/blocks/addressables.
///
/// Finds the set of symbols and addressables reachable from any symbol
/// initially marked live. All symbols/addressables not marked live at the end
/// of this process are removed.
void prune(LinkGraph &G);

} // end namespace jitlink
} // end namespace llvm

#undef DEBUG_TYPE // "jitlink"

#endif // LIB_EXECUTIONENGINE_JITLINK_JITLINKGENERIC_H
