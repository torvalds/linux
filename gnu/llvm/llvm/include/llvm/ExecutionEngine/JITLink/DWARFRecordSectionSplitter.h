//===--------- DWARFRecordSectionSplitter.h - JITLink -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_DWARFRECORDSECTIONSPLITTER_H
#define LLVM_EXECUTIONENGINE_JITLINK_DWARFRECORDSECTIONSPLITTER_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"

namespace llvm {
namespace jitlink {

/// A LinkGraph pass that splits blocks in a section that follows the DWARF
/// Record format into sub-blocks where each header gets its own block.
/// When splitting EHFrames, DWARFRecordSectionSplitter should not be run
/// without EHFrameEdgeFixer, which is responsible for adding FDE-to-CIE edges.
class DWARFRecordSectionSplitter {
public:
  DWARFRecordSectionSplitter(StringRef SectionName);
  Error operator()(LinkGraph &G);

private:
  Error processBlock(LinkGraph &G, Block &B, LinkGraph::SplitBlockCache &Cache);

  StringRef SectionName;
};

} // namespace jitlink
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_DWARFRECORDSECTIONSPLITTER_H
