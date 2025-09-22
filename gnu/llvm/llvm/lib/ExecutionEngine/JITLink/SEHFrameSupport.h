//===------- SEHFrameSupport.h - JITLink seh-frame utils --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SEHFrame utils for JITLink.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_SEHFRAMESUPPORT_H
#define LLVM_EXECUTIONENGINE_JITLINK_SEHFRAMESUPPORT_H

#include "llvm/ADT/SetVector.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
namespace jitlink {
/// This pass adds keep-alive edge from SEH frame sections
/// to the parent function content block.
class SEHFrameKeepAlivePass {
public:
  SEHFrameKeepAlivePass(StringRef SEHFrameSectionName)
      : SEHFrameSectionName(SEHFrameSectionName) {}

  Error operator()(LinkGraph &G) {
    auto *S = G.findSectionByName(SEHFrameSectionName);
    if (!S)
      return Error::success();

    // Simply consider every block pointed by seh frame block as parants.
    // This adds some unnecessary keep-alive edges to unwind info blocks,
    // (xdata) but these blocks are usually dead by default, so they wouldn't
    // count for the fate of seh frame block.
    for (auto *B : S->blocks()) {
      auto &DummySymbol = G.addAnonymousSymbol(*B, 0, 0, false, false);
      SetVector<Block *> Children;
      for (auto &E : B->edges()) {
        auto &Sym = E.getTarget();
        if (!Sym.isDefined())
          continue;
        Children.insert(&Sym.getBlock());
      }
      for (auto *Child : Children)
        Child->addEdge(Edge(Edge::KeepAlive, 0, DummySymbol, 0));
    }
    return Error::success();
  }

private:
  StringRef SEHFrameSectionName;
};

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_SEHFRAMESUPPORT_H
