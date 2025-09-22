//===-- BasicBlockPathCloning.cpp ---=========-----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// BasicBlockPathCloning implementation.
///
/// The purpose of this pass is to clone basic block paths based on information
/// provided by the -fbasic-block-sections=list option.
/// Please refer to BasicBlockSectionsProfileReader.cpp to see a path cloning
/// example.
//===----------------------------------------------------------------------===//
// This pass clones the machine basic blocks alongs the given paths and sets up
// the CFG. It assigns BBIDs to the cloned blocks so that the
// `BasicBlockSections` pass can correctly map the cluster information to the
// blocks. The cloned block's BBID will have the same BaseID as the original
// block, but will get a unique non-zero CloneID (original blocks all have zero
// CloneIDs). This pass applies a path cloning if it satisfies the following
// conditions:
//   1. All BBIDs in the path should be mapped to existing blocks.
//   2. Each two consecutive BBIDs in the path must have a successor
//   relationship in the CFG.
//   3. The path should not include a block with indirect branches, except for
//   the last block.
// If a path does not satisfy all three conditions, it will be rejected, but the
// CloneIDs for its (supposed to be cloned) blocks will be bypassed to make sure
// that the `BasicBlockSections` pass can map cluster info correctly to the
// actually-cloned blocks.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/BasicBlockSectionUtils.h"
#include "llvm/CodeGen/BasicBlockSectionsProfileReader.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {

// Clones the given block and assigns the given `CloneID` to its BBID. Copies
// the instructions into the new block and sets up its successors.
MachineBasicBlock *CloneMachineBasicBlock(MachineBasicBlock &OrigBB,
                                          unsigned CloneID) {
  auto &MF = *OrigBB.getParent();
  auto TII = MF.getSubtarget().getInstrInfo();
  // Create the clone block and set its BBID based on the original block.
  MachineBasicBlock *CloneBB = MF.CreateMachineBasicBlock(
      OrigBB.getBasicBlock(), UniqueBBID{OrigBB.getBBID()->BaseID, CloneID});
  MF.push_back(CloneBB);

  // Copy the instructions.
  for (auto &I : OrigBB.instrs()) {
    // Bundled instructions are duplicated together.
    if (I.isBundledWithPred())
      continue;
    TII->duplicate(*CloneBB, CloneBB->end(), I);
  }

  // Add the successors of the original block as the new block's successors.
  // We set the predecessor after returning from this call.
  for (auto SI = OrigBB.succ_begin(), SE = OrigBB.succ_end(); SI != SE; ++SI)
    CloneBB->copySuccessor(&OrigBB, SI);

  if (auto FT = OrigBB.getFallThrough(/*JumpToFallThrough=*/false)) {
    // The original block has an implicit fall through.
    // Insert an explicit unconditional jump from the cloned block to the
    // fallthrough block. Technically, this is only needed for the last block
    // of the path, but we do it for all clones for consistency.
    TII->insertUnconditionalBranch(*CloneBB, FT, CloneBB->findBranchDebugLoc());
  }
  return CloneBB;
}

// Returns if we can legally apply the cloning represented by `ClonePath`.
// `BBIDToBlock` contains the original basic blocks in function `MF` keyed by
// their `BBID::BaseID`.
bool IsValidCloning(const MachineFunction &MF,
                    const DenseMap<unsigned, MachineBasicBlock *> &BBIDToBlock,
                    const SmallVector<unsigned> &ClonePath) {
  const MachineBasicBlock *PrevBB = nullptr;
  for (size_t I = 0; I < ClonePath.size(); ++I) {
    unsigned BBID = ClonePath[I];
    const MachineBasicBlock *PathBB = BBIDToBlock.lookup(BBID);
    if (!PathBB) {
      WithColor::warning() << "no block with id " << BBID << " in function "
                           << MF.getName() << "\n";
      return false;
    }

    if (PrevBB) {
      if (!PrevBB->isSuccessor(PathBB)) {
        WithColor::warning()
            << "block #" << BBID << " is not a successor of block #"
            << PrevBB->getBBID()->BaseID << " in function " << MF.getName()
            << "\n";
        return false;
      }

      for (auto &MI : *PathBB) {
        // Avoid cloning when the block contains non-duplicable instructions.
        // CFI instructions are marked as non-duplicable only because of Darwin,
        // so we exclude them from this check.
        if (MI.isNotDuplicable() && !MI.isCFIInstruction()) {
          WithColor::warning()
              << "block #" << BBID
              << " has non-duplicable instructions in function " << MF.getName()
              << "\n";
          return false;
        }
      }
      if (PathBB->isMachineBlockAddressTaken()) {
        // Avoid cloning blocks which have their address taken since we can't
        // rewire branches to those blocks as easily (e.g., branches within
        // inline assembly).
        WithColor::warning()
            << "block #" << BBID
            << " has its machine block address taken in function "
            << MF.getName() << "\n";
        return false;
      }
    }

    if (I != ClonePath.size() - 1 && !PathBB->empty() &&
        PathBB->back().isIndirectBranch()) {
      WithColor::warning()
          << "block #" << BBID
          << " has indirect branch and appears as the non-tail block of a "
             "path in function "
          << MF.getName() << "\n";
      return false;
    }
    PrevBB = PathBB;
  }
  return true;
}

// Applies all clonings specified in `ClonePaths` to `MF`. Returns true
// if any clonings have been applied.
bool ApplyCloning(MachineFunction &MF,
                  const SmallVector<SmallVector<unsigned>> &ClonePaths) {
  if (ClonePaths.empty())
    return false;
  bool AnyPathsCloned = false;
  // Map from the final BB IDs to the `MachineBasicBlock`s.
  DenseMap<unsigned, MachineBasicBlock *> BBIDToBlock;
  for (auto &BB : MF)
    BBIDToBlock.try_emplace(BB.getBBID()->BaseID, &BB);

  DenseMap<unsigned, unsigned> NClonesForBBID;
  auto TII = MF.getSubtarget().getInstrInfo();
  for (const auto &ClonePath : ClonePaths) {
    if (!IsValidCloning(MF, BBIDToBlock, ClonePath)) {
      // We still need to increment the number of clones so we can map
      // to the cluster info correctly.
      for (unsigned BBID : ClonePath)
        ++NClonesForBBID[BBID];
      continue;
    }
    MachineBasicBlock *PrevBB = nullptr;
    for (unsigned BBID : ClonePath) {
      MachineBasicBlock *OrigBB = BBIDToBlock.at(BBID);
      if (PrevBB == nullptr) {
        // The first block in the path is not cloned. We only need to make it
        // branch to the next cloned block in the path. Here, we make its
        // fallthrough explicit so we can change it later.
        if (auto FT = OrigBB->getFallThrough(/*JumpToFallThrough=*/false)) {
          TII->insertUnconditionalBranch(*OrigBB, FT,
                                         OrigBB->findBranchDebugLoc());
        }
        PrevBB = OrigBB;
        continue;
      }
      MachineBasicBlock *CloneBB =
          CloneMachineBasicBlock(*OrigBB, ++NClonesForBBID[BBID]);

      // Set up the previous block in the path to jump to the clone. This also
      // transfers the successor/predecessor relationship of PrevBB and OrigBB
      // to that of PrevBB and CloneBB.
      PrevBB->ReplaceUsesOfBlockWith(OrigBB, CloneBB);

      // Copy the livein set.
      for (auto &LiveIn : OrigBB->liveins())
        CloneBB->addLiveIn(LiveIn);

      PrevBB = CloneBB;
    }
    AnyPathsCloned = true;
  }
  return AnyPathsCloned;
}
} // end anonymous namespace

namespace llvm {
class BasicBlockPathCloning : public MachineFunctionPass {
public:
  static char ID;

  BasicBlockSectionsProfileReaderWrapperPass *BBSectionsProfileReader = nullptr;

  BasicBlockPathCloning() : MachineFunctionPass(ID) {
    initializeBasicBlockPathCloningPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "Basic Block Path Cloning"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Identify basic blocks that need separate sections and prepare to emit them
  /// accordingly.
  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // namespace llvm

char BasicBlockPathCloning::ID = 0;
INITIALIZE_PASS_BEGIN(
    BasicBlockPathCloning, "bb-path-cloning",
    "Applies path clonings for the -basic-block-sections=list option", false,
    false)
INITIALIZE_PASS_DEPENDENCY(BasicBlockSectionsProfileReaderWrapperPass)
INITIALIZE_PASS_END(
    BasicBlockPathCloning, "bb-path-cloning",
    "Applies path clonings for the -basic-block-sections=list option", false,
    false)

bool BasicBlockPathCloning::runOnMachineFunction(MachineFunction &MF) {
  assert(MF.getTarget().getBBSectionsType() == BasicBlockSection::List &&
         "BB Sections list not enabled!");
  if (hasInstrProfHashMismatch(MF))
    return false;

  return ApplyCloning(MF,
                      getAnalysis<BasicBlockSectionsProfileReaderWrapperPass>()
                          .getClonePathsForFunction(MF.getName()));
}

void BasicBlockPathCloning::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<BasicBlockSectionsProfileReaderWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

MachineFunctionPass *llvm::createBasicBlockPathCloningPass() {
  return new BasicBlockPathCloning();
}
