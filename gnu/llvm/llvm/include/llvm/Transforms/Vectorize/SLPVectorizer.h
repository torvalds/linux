//===- SLPVectorizer.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This pass implements the Bottom Up SLP vectorizer. It detects consecutive
// stores that can be put together into vector-stores. Next, it attempts to
// construct vectorizable tree using the use-def chains. If a profitable tree
// was found, the SLP vectorizer performs vectorization on the tree.
//
// The pass is inspired by the work described in the paper:
//  "Loop-Aware SLP in GCC" by Ira Rosen, Dorit Nuzman, Ayal Zaks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SLPVECTORIZER_H
#define LLVM_TRANSFORMS_VECTORIZE_SLPVECTORIZER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class AAResults;
class AssumptionCache;
class BasicBlock;
class DataLayout;
class DemandedBits;
class DominatorTree;
class Function;
class GetElementPtrInst;
class InsertElementInst;
class InsertValueInst;
class Instruction;
class LoopInfo;
class OptimizationRemarkEmitter;
class PHINode;
class ScalarEvolution;
class StoreInst;
class TargetLibraryInfo;
class TargetTransformInfo;
class Value;
class WeakTrackingVH;

/// A private "module" namespace for types and utilities used by this pass.
/// These are implementation details and should not be used by clients.
namespace slpvectorizer {

class BoUpSLP;

} // end namespace slpvectorizer

struct SLPVectorizerPass : public PassInfoMixin<SLPVectorizerPass> {
  using StoreList = SmallVector<StoreInst *, 8>;
  using StoreListMap = MapVector<Value *, StoreList>;
  using GEPList = SmallVector<GetElementPtrInst *, 8>;
  using GEPListMap = MapVector<Value *, GEPList>;
  using InstSetVector = SmallSetVector<Instruction *, 8>;

  ScalarEvolution *SE = nullptr;
  TargetTransformInfo *TTI = nullptr;
  TargetLibraryInfo *TLI = nullptr;
  AAResults *AA = nullptr;
  LoopInfo *LI = nullptr;
  DominatorTree *DT = nullptr;
  AssumptionCache *AC = nullptr;
  DemandedBits *DB = nullptr;
  const DataLayout *DL = nullptr;

public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  // Glue for old PM.
  bool runImpl(Function &F, ScalarEvolution *SE_, TargetTransformInfo *TTI_,
               TargetLibraryInfo *TLI_, AAResults *AA_, LoopInfo *LI_,
               DominatorTree *DT_, AssumptionCache *AC_, DemandedBits *DB_,
               OptimizationRemarkEmitter *ORE_);

private:
  /// Collect store and getelementptr instructions and organize them
  /// according to the underlying object of their pointer operands. We sort the
  /// instructions by their underlying objects to reduce the cost of
  /// consecutive access queries.
  ///
  /// TODO: We can further reduce this cost if we flush the chain creation
  ///       every time we run into a memory barrier.
  void collectSeedInstructions(BasicBlock *BB);

  /// Try to vectorize a list of operands.
  /// \param MaxVFOnly Vectorize only using maximal allowed register size.
  /// \returns true if a value was vectorized.
  bool tryToVectorizeList(ArrayRef<Value *> VL, slpvectorizer::BoUpSLP &R,
                          bool MaxVFOnly = false);

  /// Try to vectorize a chain that may start at the operands of \p I.
  bool tryToVectorize(Instruction *I, slpvectorizer::BoUpSLP &R);

  /// Try to vectorize chains that may start at the operands of
  /// instructions in \p Insts.
  bool tryToVectorize(ArrayRef<WeakTrackingVH> Insts,
                      slpvectorizer::BoUpSLP &R);

  /// Vectorize the store instructions collected in Stores.
  bool vectorizeStoreChains(slpvectorizer::BoUpSLP &R);

  /// Vectorize the index computations of the getelementptr instructions
  /// collected in GEPs.
  bool vectorizeGEPIndices(BasicBlock *BB, slpvectorizer::BoUpSLP &R);

  /// Try to find horizontal reduction or otherwise, collect instructions
  /// for postponed vectorization attempts.
  /// \a P if not null designates phi node the reduction is fed into
  /// (with reduction operators \a Root or one of its operands, in a basic block
  /// \a BB).
  /// \returns true if a horizontal reduction was matched and reduced.
  /// \returns false if \a V is null or not an instruction,
  /// or a horizontal reduction was not matched or not possible.
  bool vectorizeHorReduction(PHINode *P, Instruction *Root, BasicBlock *BB,
                             slpvectorizer::BoUpSLP &R,
                             TargetTransformInfo *TTI,
                             SmallVectorImpl<WeakTrackingVH> &PostponedInsts);

  /// Make an attempt to vectorize reduction and then try to vectorize
  /// postponed binary operations.
  /// \returns true on any successfull vectorization.
  bool vectorizeRootInstruction(PHINode *P, Instruction *Root, BasicBlock *BB,
                                slpvectorizer::BoUpSLP &R,
                                TargetTransformInfo *TTI);

  /// Try to vectorize trees that start at insertvalue instructions.
  bool vectorizeInsertValueInst(InsertValueInst *IVI, BasicBlock *BB,
                                slpvectorizer::BoUpSLP &R, bool MaxVFOnly);

  /// Try to vectorize trees that start at insertelement instructions.
  bool vectorizeInsertElementInst(InsertElementInst *IEI, BasicBlock *BB,
                                  slpvectorizer::BoUpSLP &R, bool MaxVFOnly);

  /// Tries to vectorize \p CmpInts. \Returns true on success.
  template <typename ItT>
  bool vectorizeCmpInsts(iterator_range<ItT> CmpInsts, BasicBlock *BB,
                         slpvectorizer::BoUpSLP &R);

  /// Tries to vectorize constructs started from InsertValueInst or
  /// InsertElementInst instructions.
  bool vectorizeInserts(InstSetVector &Instructions, BasicBlock *BB,
                        slpvectorizer::BoUpSLP &R);

  /// Scan the basic block and look for patterns that are likely to start
  /// a vectorization chain.
  bool vectorizeChainsInBlock(BasicBlock *BB, slpvectorizer::BoUpSLP &R);

  std::optional<bool> vectorizeStoreChain(ArrayRef<Value *> Chain,
                                          slpvectorizer::BoUpSLP &R,
                                          unsigned Idx, unsigned MinVF,
                                          unsigned &Size);

  bool vectorizeStores(
      ArrayRef<StoreInst *> Stores, slpvectorizer::BoUpSLP &R,
      DenseSet<std::tuple<Value *, Value *, Value *, Value *, unsigned>>
          &Visited);

  /// The store instructions in a basic block organized by base pointer.
  StoreListMap Stores;

  /// The getelementptr instructions in a basic block organized by base pointer.
  GEPListMap GEPs;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_SLPVECTORIZER_H
