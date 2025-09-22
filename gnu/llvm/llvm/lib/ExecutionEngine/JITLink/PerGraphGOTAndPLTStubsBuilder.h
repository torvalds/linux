//===--------------- PerGraphGOTAndPLTStubBuilder.h -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Construct GOT and PLT entries for each graph.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_PERGRAPHGOTANDPLTSTUBSBUILDER_H
#define LLVM_EXECUTIONENGINE_JITLINK_PERGRAPHGOTANDPLTSTUBSBUILDER_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {

/// Per-object GOT and PLT Stub builder.
///
/// Constructs GOT entries and PLT stubs in every graph for referenced symbols.
/// Building these blocks in every graph is likely to lead to duplicate entries
/// in the JITLinkDylib, but allows graphs to be trivially removed independently
/// without affecting other graphs (since those other graphs will have their own
/// copies of any required entries).
template <typename BuilderImplT>
class PerGraphGOTAndPLTStubsBuilder {
public:
  PerGraphGOTAndPLTStubsBuilder(LinkGraph &G) : G(G) {}

  static Error asPass(LinkGraph &G) { return BuilderImplT(G).run(); }

  Error run() {
    LLVM_DEBUG(dbgs() << "Running Per-Graph GOT and Stubs builder:\n");

    // We're going to be adding new blocks, but we don't want to iterate over
    // the new ones, so build a worklist.
    std::vector<Block *> Worklist(G.blocks().begin(), G.blocks().end());

    for (auto *B : Worklist)
      for (auto &E : B->edges()) {
        if (impl().isGOTEdgeToFix(E)) {
          LLVM_DEBUG({
            dbgs() << "  Fixing " << G.getEdgeKindName(E.getKind())
                   << " edge at " << B->getFixupAddress(E) << " ("
                   << B->getAddress() << " + "
                   << formatv("{0:x}", E.getOffset()) << ")\n";
          });
          impl().fixGOTEdge(E, getGOTEntry(E.getTarget()));
        } else if (impl().isExternalBranchEdge(E)) {
          LLVM_DEBUG({
            dbgs() << "  Fixing " << G.getEdgeKindName(E.getKind())
                   << " edge at " << B->getFixupAddress(E) << " ("
                   << B->getAddress() << " + "
                   << formatv("{0:x}", E.getOffset()) << ")\n";
          });
          impl().fixPLTEdge(E, getPLTStub(E.getTarget()));
        }
      }

    return Error::success();
  }

protected:
  Symbol &getGOTEntry(Symbol &Target) {
    assert(Target.hasName() && "GOT edge cannot point to anonymous target");

    auto GOTEntryI = GOTEntries.find(Target.getName());

    // Build the entry if it doesn't exist.
    if (GOTEntryI == GOTEntries.end()) {
      auto &GOTEntry = impl().createGOTEntry(Target);
      LLVM_DEBUG({
        dbgs() << "    Created GOT entry for " << Target.getName() << ": "
               << GOTEntry << "\n";
      });
      GOTEntryI =
          GOTEntries.insert(std::make_pair(Target.getName(), &GOTEntry)).first;
    }

    assert(GOTEntryI != GOTEntries.end() && "Could not get GOT entry symbol");
    LLVM_DEBUG(
        { dbgs() << "    Using GOT entry " << *GOTEntryI->second << "\n"; });
    return *GOTEntryI->second;
  }

  Symbol &getPLTStub(Symbol &Target) {
    assert(Target.hasName() &&
           "External branch edge can not point to an anonymous target");
    auto StubI = PLTStubs.find(Target.getName());

    if (StubI == PLTStubs.end()) {
      auto &StubSymbol = impl().createPLTStub(Target);
      LLVM_DEBUG({
        dbgs() << "    Created PLT stub for " << Target.getName() << ": "
               << StubSymbol << "\n";
      });
      StubI =
          PLTStubs.insert(std::make_pair(Target.getName(), &StubSymbol)).first;
    }

    assert(StubI != PLTStubs.end() && "Count not get stub symbol");
    LLVM_DEBUG({ dbgs() << "    Using PLT stub " << *StubI->second << "\n"; });
    return *StubI->second;
  }

  LinkGraph &G;

private:
  BuilderImplT &impl() { return static_cast<BuilderImplT &>(*this); }

  DenseMap<StringRef, Symbol *> GOTEntries;
  DenseMap<StringRef, Symbol *> PLTStubs;
};

} // end namespace jitlink
} // end namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_EXECUTIONENGINE_JITLINK_PERGRAPHGOTANDPLTSTUBSBUILDER_H
