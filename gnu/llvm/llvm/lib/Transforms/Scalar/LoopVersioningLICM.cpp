//===- LoopVersioningLICM.cpp - LICM Loop Versioning ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// When alias analysis is uncertain about the aliasing between any two accesses,
// it will return MayAlias. This uncertainty from alias analysis restricts LICM
// from proceeding further. In cases where alias analysis is uncertain we might
// use loop versioning as an alternative.
//
// Loop Versioning will create a version of the loop with aggressive aliasing
// assumptions in addition to the original with conservative (default) aliasing
// assumptions. The version of the loop making aggressive aliasing assumptions
// will have all the memory accesses marked as no-alias. These two versions of
// loop will be preceded by a memory runtime check. This runtime check consists
// of bound checks for all unique memory accessed in loop, and it ensures the
// lack of memory aliasing. The result of the runtime check determines which of
// the loop versions is executed: If the runtime check detects any memory
// aliasing, then the original loop is executed. Otherwise, the version with
// aggressive aliasing assumptions is used.
//
// Following are the top level steps:
//
// a) Perform LoopVersioningLICM's feasibility check.
// b) If loop is a candidate for versioning then create a memory bound check,
//    by considering all the memory accesses in loop body.
// c) Clone original loop and set all memory accesses as no-alias in new loop.
// d) Set original loop & versioned loop as a branch target of the runtime check
//    result.
//
// It transforms loop as shown below:
//
//                         +----------------+
//                         |Runtime Memcheck|
//                         +----------------+
//                                 |
//              +----------+----------------+----------+
//              |                                      |
//    +---------+----------+               +-----------+----------+
//    |Orig Loop Preheader |               |Cloned Loop Preheader |
//    +--------------------+               +----------------------+
//              |                                      |
//    +--------------------+               +----------------------+
//    |Orig Loop Body      |               |Cloned Loop Body      |
//    +--------------------+               +----------------------+
//              |                                      |
//    +--------------------+               +----------------------+
//    |Orig Loop Exit Block|               |Cloned Loop Exit Block|
//    +--------------------+               +-----------+----------+
//              |                                      |
//              +----------+--------------+-----------+
//                                 |
//                           +-----+----+
//                           |Join Block|
//                           +----------+
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopVersioningLICM.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/LoopVersioning.h"
#include <cassert>
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "loop-versioning-licm"

static const char *LICMVersioningMetaData = "llvm.loop.licm_versioning.disable";

/// Threshold minimum allowed percentage for possible
/// invariant instructions in a loop.
static cl::opt<float>
    LVInvarThreshold("licm-versioning-invariant-threshold",
                     cl::desc("LoopVersioningLICM's minimum allowed percentage"
                              "of possible invariant instructions per loop"),
                     cl::init(25), cl::Hidden);

/// Threshold for maximum allowed loop nest/depth
static cl::opt<unsigned> LVLoopDepthThreshold(
    "licm-versioning-max-depth-threshold",
    cl::desc(
        "LoopVersioningLICM's threshold for maximum allowed loop nest/depth"),
    cl::init(2), cl::Hidden);

namespace {

struct LoopVersioningLICM {
  // We don't explicitly pass in LoopAccessInfo to the constructor since the
  // loop versioning might return early due to instructions that are not safe
  // for versioning. By passing the proxy instead the construction of
  // LoopAccessInfo will take place only when it's necessary.
  LoopVersioningLICM(AliasAnalysis *AA, ScalarEvolution *SE,
                     OptimizationRemarkEmitter *ORE,
                     LoopAccessInfoManager &LAIs, LoopInfo &LI,
                     Loop *CurLoop)
      : AA(AA), SE(SE), LAIs(LAIs), LI(LI), CurLoop(CurLoop),
        LoopDepthThreshold(LVLoopDepthThreshold),
        InvariantThreshold(LVInvarThreshold), ORE(ORE) {}

  bool run(DominatorTree *DT);

private:
  // Current AliasAnalysis information
  AliasAnalysis *AA;

  // Current ScalarEvolution
  ScalarEvolution *SE;

  // Current Loop's LoopAccessInfo
  const LoopAccessInfo *LAI = nullptr;

  // Proxy for retrieving LoopAccessInfo.
  LoopAccessInfoManager &LAIs;

  LoopInfo &LI;

  // The current loop we are working on.
  Loop *CurLoop;

  // Maximum loop nest threshold
  unsigned LoopDepthThreshold;

  // Minimum invariant threshold
  float InvariantThreshold;

  // Counter to track num of load & store
  unsigned LoadAndStoreCounter = 0;

  // Counter to track num of invariant
  unsigned InvariantCounter = 0;

  // Read only loop marker.
  bool IsReadOnlyLoop = true;

  // OptimizationRemarkEmitter
  OptimizationRemarkEmitter *ORE;

  bool isLegalForVersioning();
  bool legalLoopStructure();
  bool legalLoopInstructions();
  bool legalLoopMemoryAccesses();
  bool isLoopAlreadyVisited();
  void setNoAliasToLoop(Loop *VerLoop);
  bool instructionSafeForVersioning(Instruction *I);
};

} // end anonymous namespace

/// Check loop structure and confirms it's good for LoopVersioningLICM.
bool LoopVersioningLICM::legalLoopStructure() {
  // Loop must be in loop simplify form.
  if (!CurLoop->isLoopSimplifyForm()) {
    LLVM_DEBUG(dbgs() << "    loop is not in loop-simplify form.\n");
    return false;
  }
  // Loop should be innermost loop, if not return false.
  if (!CurLoop->getSubLoops().empty()) {
    LLVM_DEBUG(dbgs() << "    loop is not innermost\n");
    return false;
  }
  // Loop should have a single backedge, if not return false.
  if (CurLoop->getNumBackEdges() != 1) {
    LLVM_DEBUG(dbgs() << "    loop has multiple backedges\n");
    return false;
  }
  // Loop must have a single exiting block, if not return false.
  if (!CurLoop->getExitingBlock()) {
    LLVM_DEBUG(dbgs() << "    loop has multiple exiting block\n");
    return false;
  }
  // We only handle bottom-tested loop, i.e. loop in which the condition is
  // checked at the end of each iteration. With that we can assume that all
  // instructions in the loop are executed the same number of times.
  if (CurLoop->getExitingBlock() != CurLoop->getLoopLatch()) {
    LLVM_DEBUG(dbgs() << "    loop is not bottom tested\n");
    return false;
  }
  // Parallel loops must not have aliasing loop-invariant memory accesses.
  // Hence we don't need to version anything in this case.
  if (CurLoop->isAnnotatedParallel()) {
    LLVM_DEBUG(dbgs() << "    Parallel loop is not worth versioning\n");
    return false;
  }
  // Loop depth more then LoopDepthThreshold are not allowed
  if (CurLoop->getLoopDepth() > LoopDepthThreshold) {
    LLVM_DEBUG(dbgs() << "    loop depth is more then threshold\n");
    return false;
  }
  // We need to be able to compute the loop trip count in order
  // to generate the bound checks.
  const SCEV *ExitCount = SE->getBackedgeTakenCount(CurLoop);
  if (isa<SCEVCouldNotCompute>(ExitCount)) {
    LLVM_DEBUG(dbgs() << "    loop does not has trip count\n");
    return false;
  }
  return true;
}

/// Check memory accesses in loop and confirms it's good for
/// LoopVersioningLICM.
bool LoopVersioningLICM::legalLoopMemoryAccesses() {
  // Loop over the body of this loop, construct AST.
  BatchAAResults BAA(*AA);
  AliasSetTracker AST(BAA);
  for (auto *Block : CurLoop->getBlocks()) {
    // Ignore blocks in subloops.
    if (LI.getLoopFor(Block) == CurLoop)
      AST.add(*Block);
  }

  // Memory check:
  // Transform phase will generate a versioned loop and also a runtime check to
  // ensure the pointers are independent and they don’t alias.
  // In version variant of loop, alias meta data asserts that all access are
  // mutually independent.
  //
  // Pointers aliasing in alias domain are avoided because with multiple
  // aliasing domains we may not be able to hoist potential loop invariant
  // access out of the loop.
  //
  // Iterate over alias tracker sets, and confirm AliasSets doesn't have any
  // must alias set.
  bool HasMayAlias = false;
  bool TypeSafety = false;
  bool HasMod = false;
  for (const auto &I : AST) {
    const AliasSet &AS = I;
    // Skip Forward Alias Sets, as this should be ignored as part of
    // the AliasSetTracker object.
    if (AS.isForwardingAliasSet())
      continue;
    // With MustAlias its not worth adding runtime bound check.
    if (AS.isMustAlias())
      return false;
    const Value *SomePtr = AS.begin()->Ptr;
    bool TypeCheck = true;
    // Check for Mod & MayAlias
    HasMayAlias |= AS.isMayAlias();
    HasMod |= AS.isMod();
    for (const auto &MemLoc : AS) {
      const Value *Ptr = MemLoc.Ptr;
      // Alias tracker should have pointers of same data type.
      //
      // FIXME: check no longer effective since opaque pointers?
      // If the intent is to check that the memory accesses use the
      // same data type (such that LICM can promote them), then we
      // can no longer see this from the pointer value types.
      TypeCheck = (TypeCheck && (SomePtr->getType() == Ptr->getType()));
    }
    // At least one alias tracker should have pointers of same data type.
    TypeSafety |= TypeCheck;
  }
  // Ensure types should be of same type.
  if (!TypeSafety) {
    LLVM_DEBUG(dbgs() << "    Alias tracker type safety failed!\n");
    return false;
  }
  // Ensure loop body shouldn't be read only.
  if (!HasMod) {
    LLVM_DEBUG(dbgs() << "    No memory modified in loop body\n");
    return false;
  }
  // Make sure alias set has may alias case.
  // If there no alias memory ambiguity, return false.
  if (!HasMayAlias) {
    LLVM_DEBUG(dbgs() << "    No ambiguity in memory access.\n");
    return false;
  }
  return true;
}

/// Check loop instructions safe for Loop versioning.
/// It returns true if it's safe else returns false.
/// Consider following:
/// 1) Check all load store in loop body are non atomic & non volatile.
/// 2) Check function call safety, by ensuring its not accessing memory.
/// 3) Loop body shouldn't have any may throw instruction.
/// 4) Loop body shouldn't have any convergent or noduplicate instructions.
bool LoopVersioningLICM::instructionSafeForVersioning(Instruction *I) {
  assert(I != nullptr && "Null instruction found!");
  // Check function call safety
  if (auto *Call = dyn_cast<CallBase>(I)) {
    if (Call->isConvergent() || Call->cannotDuplicate()) {
      LLVM_DEBUG(dbgs() << "    Convergent call site found.\n");
      return false;
    }

    if (!AA->doesNotAccessMemory(Call)) {
      LLVM_DEBUG(dbgs() << "    Unsafe call site found.\n");
      return false;
    }
  }

  // Avoid loops with possiblity of throw
  if (I->mayThrow()) {
    LLVM_DEBUG(dbgs() << "    May throw instruction found in loop body\n");
    return false;
  }
  // If current instruction is load instructions
  // make sure it's a simple load (non atomic & non volatile)
  if (I->mayReadFromMemory()) {
    LoadInst *Ld = dyn_cast<LoadInst>(I);
    if (!Ld || !Ld->isSimple()) {
      LLVM_DEBUG(dbgs() << "    Found a non-simple load.\n");
      return false;
    }
    LoadAndStoreCounter++;
    Value *Ptr = Ld->getPointerOperand();
    // Check loop invariant.
    if (SE->isLoopInvariant(SE->getSCEV(Ptr), CurLoop))
      InvariantCounter++;
  }
  // If current instruction is store instruction
  // make sure it's a simple store (non atomic & non volatile)
  else if (I->mayWriteToMemory()) {
    StoreInst *St = dyn_cast<StoreInst>(I);
    if (!St || !St->isSimple()) {
      LLVM_DEBUG(dbgs() << "    Found a non-simple store.\n");
      return false;
    }
    LoadAndStoreCounter++;
    Value *Ptr = St->getPointerOperand();
    // Check loop invariant.
    if (SE->isLoopInvariant(SE->getSCEV(Ptr), CurLoop))
      InvariantCounter++;

    IsReadOnlyLoop = false;
  }
  return true;
}

/// Check loop instructions and confirms it's good for
/// LoopVersioningLICM.
bool LoopVersioningLICM::legalLoopInstructions() {
  // Resetting counters.
  LoadAndStoreCounter = 0;
  InvariantCounter = 0;
  IsReadOnlyLoop = true;
  using namespace ore;
  // Iterate over loop blocks and instructions of each block and check
  // instruction safety.
  for (auto *Block : CurLoop->getBlocks())
    for (auto &Inst : *Block) {
      // If instruction is unsafe just return false.
      if (!instructionSafeForVersioning(&Inst)) {
        ORE->emit([&]() {
          return OptimizationRemarkMissed(DEBUG_TYPE, "IllegalLoopInst", &Inst)
                 << " Unsafe Loop Instruction";
        });
        return false;
      }
    }
  // Get LoopAccessInfo from current loop via the proxy.
  LAI = &LAIs.getInfo(*CurLoop);
  // Check LoopAccessInfo for need of runtime check.
  if (LAI->getRuntimePointerChecking()->getChecks().empty()) {
    LLVM_DEBUG(dbgs() << "    LAA: Runtime check not found !!\n");
    return false;
  }
  // Number of runtime-checks should be less then RuntimeMemoryCheckThreshold
  if (LAI->getNumRuntimePointerChecks() >
      VectorizerParams::RuntimeMemoryCheckThreshold) {
    LLVM_DEBUG(
        dbgs() << "    LAA: Runtime checks are more than threshold !!\n");
    ORE->emit([&]() {
      return OptimizationRemarkMissed(DEBUG_TYPE, "RuntimeCheck",
                                      CurLoop->getStartLoc(),
                                      CurLoop->getHeader())
             << "Number of runtime checks "
             << NV("RuntimeChecks", LAI->getNumRuntimePointerChecks())
             << " exceeds threshold "
             << NV("Threshold", VectorizerParams::RuntimeMemoryCheckThreshold);
    });
    return false;
  }
  // Loop should have at least one invariant load or store instruction.
  if (!InvariantCounter) {
    LLVM_DEBUG(dbgs() << "    Invariant not found !!\n");
    return false;
  }
  // Read only loop not allowed.
  if (IsReadOnlyLoop) {
    LLVM_DEBUG(dbgs() << "    Found a read-only loop!\n");
    return false;
  }
  // Profitablity check:
  // Check invariant threshold, should be in limit.
  if (InvariantCounter * 100 < InvariantThreshold * LoadAndStoreCounter) {
    LLVM_DEBUG(
        dbgs()
        << "    Invariant load & store are less then defined threshold\n");
    LLVM_DEBUG(dbgs() << "    Invariant loads & stores: "
                      << ((InvariantCounter * 100) / LoadAndStoreCounter)
                      << "%\n");
    LLVM_DEBUG(dbgs() << "    Invariant loads & store threshold: "
                      << InvariantThreshold << "%\n");
    ORE->emit([&]() {
      return OptimizationRemarkMissed(DEBUG_TYPE, "InvariantThreshold",
                                      CurLoop->getStartLoc(),
                                      CurLoop->getHeader())
             << "Invariant load & store "
             << NV("LoadAndStoreCounter",
                   ((InvariantCounter * 100) / LoadAndStoreCounter))
             << " are less then defined threshold "
             << NV("Threshold", InvariantThreshold);
    });
    return false;
  }
  return true;
}

/// It checks loop is already visited or not.
/// check loop meta data, if loop revisited return true
/// else false.
bool LoopVersioningLICM::isLoopAlreadyVisited() {
  // Check LoopVersioningLICM metadata into loop
  if (findStringMetadataForLoop(CurLoop, LICMVersioningMetaData)) {
    return true;
  }
  return false;
}

/// Checks legality for LoopVersioningLICM by considering following:
/// a) loop structure legality   b) loop instruction legality
/// c) loop memory access legality.
/// Return true if legal else returns false.
bool LoopVersioningLICM::isLegalForVersioning() {
  using namespace ore;
  LLVM_DEBUG(dbgs() << "Loop: " << *CurLoop);
  // Make sure not re-visiting same loop again.
  if (isLoopAlreadyVisited()) {
    LLVM_DEBUG(
        dbgs() << "    Revisiting loop in LoopVersioningLICM not allowed.\n\n");
    return false;
  }
  // Check loop structure leagality.
  if (!legalLoopStructure()) {
    LLVM_DEBUG(
        dbgs() << "    Loop structure not suitable for LoopVersioningLICM\n\n");
    ORE->emit([&]() {
      return OptimizationRemarkMissed(DEBUG_TYPE, "IllegalLoopStruct",
                                      CurLoop->getStartLoc(),
                                      CurLoop->getHeader())
             << " Unsafe Loop structure";
    });
    return false;
  }
  // Check loop instruction leagality.
  if (!legalLoopInstructions()) {
    LLVM_DEBUG(
        dbgs()
        << "    Loop instructions not suitable for LoopVersioningLICM\n\n");
    return false;
  }
  // Check loop memory access leagality.
  if (!legalLoopMemoryAccesses()) {
    LLVM_DEBUG(
        dbgs()
        << "    Loop memory access not suitable for LoopVersioningLICM\n\n");
    ORE->emit([&]() {
      return OptimizationRemarkMissed(DEBUG_TYPE, "IllegalLoopMemoryAccess",
                                      CurLoop->getStartLoc(),
                                      CurLoop->getHeader())
             << " Unsafe Loop memory access";
    });
    return false;
  }
  // Loop versioning is feasible, return true.
  LLVM_DEBUG(dbgs() << "    Loop Versioning found to be beneficial\n\n");
  ORE->emit([&]() {
    return OptimizationRemark(DEBUG_TYPE, "IsLegalForVersioning",
                              CurLoop->getStartLoc(), CurLoop->getHeader())
           << " Versioned loop for LICM."
           << " Number of runtime checks we had to insert "
           << NV("RuntimeChecks", LAI->getNumRuntimePointerChecks());
  });
  return true;
}

/// Update loop with aggressive aliasing assumptions.
/// It marks no-alias to any pairs of memory operations by assuming
/// loop should not have any must-alias memory accesses pairs.
/// During LoopVersioningLICM legality we ignore loops having must
/// aliasing memory accesses.
void LoopVersioningLICM::setNoAliasToLoop(Loop *VerLoop) {
  // Get latch terminator instruction.
  Instruction *I = VerLoop->getLoopLatch()->getTerminator();
  // Create alias scope domain.
  MDBuilder MDB(I->getContext());
  MDNode *NewDomain = MDB.createAnonymousAliasScopeDomain("LVDomain");
  StringRef Name = "LVAliasScope";
  MDNode *NewScope = MDB.createAnonymousAliasScope(NewDomain, Name);
  SmallVector<Metadata *, 4> Scopes{NewScope}, NoAliases{NewScope};
  // Iterate over each instruction of loop.
  // set no-alias for all load & store instructions.
  for (auto *Block : CurLoop->getBlocks()) {
    for (auto &Inst : *Block) {
      // Only interested in instruction that may modify or read memory.
      if (!Inst.mayReadFromMemory() && !Inst.mayWriteToMemory())
        continue;
      // Set no-alias for current instruction.
      Inst.setMetadata(
          LLVMContext::MD_noalias,
          MDNode::concatenate(Inst.getMetadata(LLVMContext::MD_noalias),
                              MDNode::get(Inst.getContext(), NoAliases)));
      // set alias-scope for current instruction.
      Inst.setMetadata(
          LLVMContext::MD_alias_scope,
          MDNode::concatenate(Inst.getMetadata(LLVMContext::MD_alias_scope),
                              MDNode::get(Inst.getContext(), Scopes)));
    }
  }
}

bool LoopVersioningLICM::run(DominatorTree *DT) {
  // Do not do the transformation if disabled by metadata.
  if (hasLICMVersioningTransformation(CurLoop) & TM_Disable)
    return false;

  bool Changed = false;

  // Check feasiblity of LoopVersioningLICM.
  // If versioning found to be feasible and beneficial then proceed
  // else simply return, by cleaning up memory.
  if (isLegalForVersioning()) {
    // Do loop versioning.
    // Create memcheck for memory accessed inside loop.
    // Clone original loop, and set blocks properly.
    LoopVersioning LVer(*LAI, LAI->getRuntimePointerChecking()->getChecks(),
                        CurLoop, &LI, DT, SE);
    LVer.versionLoop();
    // Set Loop Versioning metaData for original loop.
    addStringMetadataToLoop(LVer.getNonVersionedLoop(), LICMVersioningMetaData);
    // Set Loop Versioning metaData for version loop.
    addStringMetadataToLoop(LVer.getVersionedLoop(), LICMVersioningMetaData);
    // Set "llvm.mem.parallel_loop_access" metaData to versioned loop.
    // FIXME: "llvm.mem.parallel_loop_access" annotates memory access
    // instructions, not loops.
    addStringMetadataToLoop(LVer.getVersionedLoop(),
                            "llvm.mem.parallel_loop_access");
    // Update version loop with aggressive aliasing assumption.
    setNoAliasToLoop(LVer.getVersionedLoop());
    Changed = true;
  }
  return Changed;
}

namespace llvm {

PreservedAnalyses LoopVersioningLICMPass::run(Loop &L, LoopAnalysisManager &AM,
                                              LoopStandardAnalysisResults &LAR,
                                              LPMUpdater &U) {
  AliasAnalysis *AA = &LAR.AA;
  ScalarEvolution *SE = &LAR.SE;
  DominatorTree *DT = &LAR.DT;
  const Function *F = L.getHeader()->getParent();
  OptimizationRemarkEmitter ORE(F);

  LoopAccessInfoManager LAIs(*SE, *AA, *DT, LAR.LI, nullptr, nullptr);
  if (!LoopVersioningLICM(AA, SE, &ORE, LAIs, LAR.LI, &L).run(DT))
    return PreservedAnalyses::all();
  return getLoopPassPreservedAnalyses();
}
} // namespace llvm
