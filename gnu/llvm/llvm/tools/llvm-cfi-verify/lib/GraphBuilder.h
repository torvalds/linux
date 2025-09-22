//===- GraphBuilder.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CFI_VERIFY_GRAPH_BUILDER_H
#define LLVM_CFI_VERIFY_GRAPH_BUILDER_H

#include "FileAnalysis.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <functional>
#include <set>

using Instr = llvm::cfi_verify::FileAnalysis::Instr;

namespace llvm {
namespace cfi_verify {

extern uint64_t SearchLengthForUndef;
extern uint64_t SearchLengthForConditionalBranch;

struct ConditionalBranchNode {
  uint64_t Address;
  uint64_t Target;
  uint64_t Fallthrough;
  // Does this conditional branch look like it's used for CFI protection? i.e.
  //  - The exit point of a basic block whos entry point is {target|fallthrough}
  //    is a CFI trap, and...
  //  - The exit point of the other basic block is an undirect CF instruction.
  bool CFIProtection;
  bool IndirectCFIsOnTargetPath;
};

// The canonical graph result structure returned by GraphBuilder. The members
// in this structure encapsulate all possible code paths to the instruction
// located at `BaseAddress`.
struct GraphResult {
  uint64_t BaseAddress;

  // Map between an instruction address, and the address of the next instruction
  // that will be executed. This map will contain all keys in the range:
  //   - [orphaned node, base address)
  //   - [conditional branch node {target|fallthrough}, base address)
  DenseMap<uint64_t, uint64_t> IntermediateNodes;

  // A list of orphaned nodes. A node is an 'orphan' if it meets any of the
  // following criteria:
  //   - The length of the path from the base to this node has exceeded
  //     `SearchLengthForConditionalBranch`.
  //   - The node has no cross references to it.
  //   - The path from the base to this node is cyclic.
  std::vector<uint64_t> OrphanedNodes;

  // A list of top-level conditional branches that exist at the top of any
  // non-orphan paths from the base.
  std::vector<ConditionalBranchNode> ConditionalBranchNodes;

  // Returns an in-order list of the path between the address provided and the
  // base. The provided address must be part of this graph, and must not be a
  // conditional branch.
  std::vector<uint64_t> flattenAddress(uint64_t Address) const;

  // Print the DOT representation of this result.
  void printToDOT(const FileAnalysis &Analysis, raw_ostream &OS) const;
};

class GraphBuilder {
public:
  // Build the control flow graph for a provided control flow node. This method
  // will enumerate all branch nodes that can lead to this node, and place them
  // into GraphResult::ConditionalBranchNodes. It will also provide any orphaned
  // (i.e. the upwards traversal did not make it to a branch node) flows to the
  // provided node in GraphResult::OrphanedNodes.
  static GraphResult buildFlowGraph(const FileAnalysis &Analysis,
                                    object::SectionedAddress Address);

private:
  // Implementation function that actually builds the flow graph. Retrieves a
  // list of cross references to instruction referenced in `Address`. If any of
  // these XRefs are conditional branches, it will build the other potential
  // path (fallthrough or target) using `buildFlowsToUndefined`. Otherwise, this
  // function will recursively call itself where `Address` in the recursive call
  // is now the XRef. If any XRef is an orphan, it is added to
  // `Result.OrphanedNodes`. `OpenedNodes` keeps track of the list of nodes
  // in the current path and is used for cycle-checking. If the path is found
  // to be cyclic, it will be added to `Result.OrphanedNodes`.
  static void buildFlowGraphImpl(const FileAnalysis &Analysis,
                                 DenseSet<uint64_t> &OpenedNodes,
                                 GraphResult &Result, uint64_t Address,
                                 uint64_t Depth);

  // Utilised by buildFlowGraphImpl to build the tree out from the provided
  // conditional branch node to an undefined instruction. The provided
  // conditional branch node must have exactly one of its subtrees set, and will
  // update the node's CFIProtection field if a deterministic flow can be found
  // to an undefined instruction.
  static void buildFlowsToUndefined(const FileAnalysis &Analysis,
                                    GraphResult &Result,
                                    ConditionalBranchNode &BranchNode,
                                    const Instr &BranchInstrMeta);
};

} // end namespace cfi_verify
} // end namespace llvm

#endif // LLVM_CFI_VERIFY_GRAPH_BUILDER_H
