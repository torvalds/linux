//===- OutputSegment.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OutputSegment.h"
#include "InputChunks.h"
#include "lld/Common/Memory.h"

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::wasm;

namespace lld::wasm {

void OutputSegment::addInputSegment(InputChunk *inSeg) {
  alignment = std::max(alignment, inSeg->alignment);
  inputSegments.push_back(inSeg);
  size = llvm::alignTo(size, 1ULL << inSeg->alignment);
  LLVM_DEBUG(dbgs() << "addInputSegment: " << inSeg->name << " oname=" << name
                    << " size=" << inSeg->getSize()
                    << " align=" << inSeg->alignment << " at:" << size << "\n");
  inSeg->outputSeg = this;
  inSeg->outputSegmentOffset = size;
  size += inSeg->getSize();
}

// This function scans over the input segments.
//
// It removes MergeInputChunks from the input section array and adds
// new synthetic sections at the location of the first input section
// that it replaces. It then finalizes each synthetic section in order
// to compute an output offset for each piece of each input section.
void OutputSegment::finalizeInputSegments() {
  LLVM_DEBUG(llvm::dbgs() << "finalizeInputSegments: " << name << "\n");
  std::vector<SyntheticMergedChunk *> mergedSegments;
  std::vector<InputChunk *> newSegments;
  for (InputChunk *s : inputSegments) {
    MergeInputChunk *ms = dyn_cast<MergeInputChunk>(s);
    if (!ms) {
      newSegments.push_back(s);
      continue;
    }

    // A segment should not make it here unless its alive
    assert(ms->live);

    auto i = llvm::find_if(mergedSegments, [=](SyntheticMergedChunk *seg) {
      return seg->flags == ms->flags && seg->alignment == ms->alignment;
    });
    if (i == mergedSegments.end()) {
      LLVM_DEBUG(llvm::dbgs() << "new merge segment: " << name
                              << " alignment=" << ms->alignment << "\n");
      auto *syn = make<SyntheticMergedChunk>(name, ms->alignment, ms->flags);
      syn->outputSeg = this;
      mergedSegments.push_back(syn);
      i = std::prev(mergedSegments.end());
      newSegments.push_back(syn);
    } else {
      LLVM_DEBUG(llvm::dbgs() << "adding to merge segment: " << name << "\n");
    }
    (*i)->addMergeChunk(ms);
  }

  for (auto *ms : mergedSegments)
    ms->finalizeContents();

  inputSegments = newSegments;
  size = 0;
  for (InputChunk *seg : inputSegments) {
    size = llvm::alignTo(size, 1ULL << seg->alignment);
    LLVM_DEBUG(llvm::dbgs() << "outputSegmentOffset set: " << seg->name
                            << " -> " << size << "\n");
    seg->outputSegmentOffset = size;
    size += seg->getSize();
  }
}

} // namespace lld::wasm
