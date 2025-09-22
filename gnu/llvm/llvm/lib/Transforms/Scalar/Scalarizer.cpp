//===- Scalarizer.cpp - Scalarize vector operations -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass converts vector operations into scalar operations (or, optionally,
// operations on smaller vector widths), in order to expose optimization
// opportunities on the individual scalar operations.
// It is mainly intended for targets that do not have vector units, but it
// may also be useful for revectorizing code to different vector widths.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/Scalarizer.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/Local.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <map>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "scalarizer"

static cl::opt<bool> ClScalarizeVariableInsertExtract(
    "scalarize-variable-insert-extract", cl::init(true), cl::Hidden,
    cl::desc("Allow the scalarizer pass to scalarize "
             "insertelement/extractelement with variable index"));

// This is disabled by default because having separate loads and stores
// makes it more likely that the -combiner-alias-analysis limits will be
// reached.
static cl::opt<bool> ClScalarizeLoadStore(
    "scalarize-load-store", cl::init(false), cl::Hidden,
    cl::desc("Allow the scalarizer pass to scalarize loads and store"));

// Split vectors larger than this size into fragments, where each fragment is
// either a vector no larger than this size or a scalar.
//
// Instructions with operands or results of different sizes that would be split
// into a different number of fragments are currently left as-is.
static cl::opt<unsigned> ClScalarizeMinBits(
    "scalarize-min-bits", cl::init(0), cl::Hidden,
    cl::desc("Instruct the scalarizer pass to attempt to keep values of a "
             "minimum number of bits"));

namespace {

BasicBlock::iterator skipPastPhiNodesAndDbg(BasicBlock::iterator Itr) {
  BasicBlock *BB = Itr->getParent();
  if (isa<PHINode>(Itr))
    Itr = BB->getFirstInsertionPt();
  if (Itr != BB->end())
    Itr = skipDebugIntrinsics(Itr);
  return Itr;
}

// Used to store the scattered form of a vector.
using ValueVector = SmallVector<Value *, 8>;

// Used to map a vector Value and associated type to its scattered form.
// The associated type is only non-null for pointer values that are "scattered"
// when used as pointer operands to load or store.
//
// We use std::map because we want iterators to persist across insertion and
// because the values are relatively large.
using ScatterMap = std::map<std::pair<Value *, Type *>, ValueVector>;

// Lists Instructions that have been replaced with scalar implementations,
// along with a pointer to their scattered forms.
using GatherList = SmallVector<std::pair<Instruction *, ValueVector *>, 16>;

struct VectorSplit {
  // The type of the vector.
  FixedVectorType *VecTy = nullptr;

  // The number of elements packed in a fragment (other than the remainder).
  unsigned NumPacked = 0;

  // The number of fragments (scalars or smaller vectors) into which the vector
  // shall be split.
  unsigned NumFragments = 0;

  // The type of each complete fragment.
  Type *SplitTy = nullptr;

  // The type of the remainder (last) fragment; null if all fragments are
  // complete.
  Type *RemainderTy = nullptr;

  Type *getFragmentType(unsigned I) const {
    return RemainderTy && I == NumFragments - 1 ? RemainderTy : SplitTy;
  }
};

// Provides a very limited vector-like interface for lazily accessing one
// component of a scattered vector or vector pointer.
class Scatterer {
public:
  Scatterer() = default;

  // Scatter V into Size components.  If new instructions are needed,
  // insert them before BBI in BB.  If Cache is nonnull, use it to cache
  // the results.
  Scatterer(BasicBlock *bb, BasicBlock::iterator bbi, Value *v,
            const VectorSplit &VS, ValueVector *cachePtr = nullptr);

  // Return component I, creating a new Value for it if necessary.
  Value *operator[](unsigned I);

  // Return the number of components.
  unsigned size() const { return VS.NumFragments; }

private:
  BasicBlock *BB;
  BasicBlock::iterator BBI;
  Value *V;
  VectorSplit VS;
  bool IsPointer;
  ValueVector *CachePtr;
  ValueVector Tmp;
};

// FCmpSplitter(FCI)(Builder, X, Y, Name) uses Builder to create an FCmp
// called Name that compares X and Y in the same way as FCI.
struct FCmpSplitter {
  FCmpSplitter(FCmpInst &fci) : FCI(fci) {}

  Value *operator()(IRBuilder<> &Builder, Value *Op0, Value *Op1,
                    const Twine &Name) const {
    return Builder.CreateFCmp(FCI.getPredicate(), Op0, Op1, Name);
  }

  FCmpInst &FCI;
};

// ICmpSplitter(ICI)(Builder, X, Y, Name) uses Builder to create an ICmp
// called Name that compares X and Y in the same way as ICI.
struct ICmpSplitter {
  ICmpSplitter(ICmpInst &ici) : ICI(ici) {}

  Value *operator()(IRBuilder<> &Builder, Value *Op0, Value *Op1,
                    const Twine &Name) const {
    return Builder.CreateICmp(ICI.getPredicate(), Op0, Op1, Name);
  }

  ICmpInst &ICI;
};

// UnarySplitter(UO)(Builder, X, Name) uses Builder to create
// a unary operator like UO called Name with operand X.
struct UnarySplitter {
  UnarySplitter(UnaryOperator &uo) : UO(uo) {}

  Value *operator()(IRBuilder<> &Builder, Value *Op, const Twine &Name) const {
    return Builder.CreateUnOp(UO.getOpcode(), Op, Name);
  }

  UnaryOperator &UO;
};

// BinarySplitter(BO)(Builder, X, Y, Name) uses Builder to create
// a binary operator like BO called Name with operands X and Y.
struct BinarySplitter {
  BinarySplitter(BinaryOperator &bo) : BO(bo) {}

  Value *operator()(IRBuilder<> &Builder, Value *Op0, Value *Op1,
                    const Twine &Name) const {
    return Builder.CreateBinOp(BO.getOpcode(), Op0, Op1, Name);
  }

  BinaryOperator &BO;
};

// Information about a load or store that we're scalarizing.
struct VectorLayout {
  VectorLayout() = default;

  // Return the alignment of fragment Frag.
  Align getFragmentAlign(unsigned Frag) {
    return commonAlignment(VecAlign, Frag * SplitSize);
  }

  // The split of the underlying vector type.
  VectorSplit VS;

  // The alignment of the vector.
  Align VecAlign;

  // The size of each (non-remainder) fragment in bytes.
  uint64_t SplitSize = 0;
};

/// Concatenate the given fragments to a single vector value of the type
/// described in @p VS.
static Value *concatenate(IRBuilder<> &Builder, ArrayRef<Value *> Fragments,
                          const VectorSplit &VS, Twine Name) {
  unsigned NumElements = VS.VecTy->getNumElements();
  SmallVector<int> ExtendMask;
  SmallVector<int> InsertMask;

  if (VS.NumPacked > 1) {
    // Prepare the shufflevector masks once and re-use them for all
    // fragments.
    ExtendMask.resize(NumElements, -1);
    for (unsigned I = 0; I < VS.NumPacked; ++I)
      ExtendMask[I] = I;

    InsertMask.resize(NumElements);
    for (unsigned I = 0; I < NumElements; ++I)
      InsertMask[I] = I;
  }

  Value *Res = PoisonValue::get(VS.VecTy);
  for (unsigned I = 0; I < VS.NumFragments; ++I) {
    Value *Fragment = Fragments[I];

    unsigned NumPacked = VS.NumPacked;
    if (I == VS.NumFragments - 1 && VS.RemainderTy) {
      if (auto *RemVecTy = dyn_cast<FixedVectorType>(VS.RemainderTy))
        NumPacked = RemVecTy->getNumElements();
      else
        NumPacked = 1;
    }

    if (NumPacked == 1) {
      Res = Builder.CreateInsertElement(Res, Fragment, I * VS.NumPacked,
                                        Name + ".upto" + Twine(I));
    } else {
      Fragment = Builder.CreateShuffleVector(Fragment, Fragment, ExtendMask);
      if (I == 0) {
        Res = Fragment;
      } else {
        for (unsigned J = 0; J < NumPacked; ++J)
          InsertMask[I * VS.NumPacked + J] = NumElements + J;
        Res = Builder.CreateShuffleVector(Res, Fragment, InsertMask,
                                          Name + ".upto" + Twine(I));
        for (unsigned J = 0; J < NumPacked; ++J)
          InsertMask[I * VS.NumPacked + J] = I * VS.NumPacked + J;
      }
    }
  }

  return Res;
}

template <typename T>
T getWithDefaultOverride(const cl::opt<T> &ClOption,
                         const std::optional<T> &DefaultOverride) {
  return ClOption.getNumOccurrences() ? ClOption
                                      : DefaultOverride.value_or(ClOption);
}

class ScalarizerVisitor : public InstVisitor<ScalarizerVisitor, bool> {
public:
  ScalarizerVisitor(DominatorTree *DT, ScalarizerPassOptions Options)
      : DT(DT), ScalarizeVariableInsertExtract(getWithDefaultOverride(
                    ClScalarizeVariableInsertExtract,
                    Options.ScalarizeVariableInsertExtract)),
        ScalarizeLoadStore(getWithDefaultOverride(ClScalarizeLoadStore,
                                                  Options.ScalarizeLoadStore)),
        ScalarizeMinBits(getWithDefaultOverride(ClScalarizeMinBits,
                                                Options.ScalarizeMinBits)) {}

  bool visit(Function &F);

  // InstVisitor methods.  They return true if the instruction was scalarized,
  // false if nothing changed.
  bool visitInstruction(Instruction &I) { return false; }
  bool visitSelectInst(SelectInst &SI);
  bool visitICmpInst(ICmpInst &ICI);
  bool visitFCmpInst(FCmpInst &FCI);
  bool visitUnaryOperator(UnaryOperator &UO);
  bool visitBinaryOperator(BinaryOperator &BO);
  bool visitGetElementPtrInst(GetElementPtrInst &GEPI);
  bool visitCastInst(CastInst &CI);
  bool visitBitCastInst(BitCastInst &BCI);
  bool visitInsertElementInst(InsertElementInst &IEI);
  bool visitExtractElementInst(ExtractElementInst &EEI);
  bool visitShuffleVectorInst(ShuffleVectorInst &SVI);
  bool visitPHINode(PHINode &PHI);
  bool visitLoadInst(LoadInst &LI);
  bool visitStoreInst(StoreInst &SI);
  bool visitCallInst(CallInst &ICI);
  bool visitFreezeInst(FreezeInst &FI);

private:
  Scatterer scatter(Instruction *Point, Value *V, const VectorSplit &VS);
  void gather(Instruction *Op, const ValueVector &CV, const VectorSplit &VS);
  void replaceUses(Instruction *Op, Value *CV);
  bool canTransferMetadata(unsigned Kind);
  void transferMetadataAndIRFlags(Instruction *Op, const ValueVector &CV);
  std::optional<VectorSplit> getVectorSplit(Type *Ty);
  std::optional<VectorLayout> getVectorLayout(Type *Ty, Align Alignment,
                                              const DataLayout &DL);
  bool finish();

  template<typename T> bool splitUnary(Instruction &, const T &);
  template<typename T> bool splitBinary(Instruction &, const T &);

  bool splitCall(CallInst &CI);

  ScatterMap Scattered;
  GatherList Gathered;
  bool Scalarized;

  SmallVector<WeakTrackingVH, 32> PotentiallyDeadInstrs;

  DominatorTree *DT;

  const bool ScalarizeVariableInsertExtract;
  const bool ScalarizeLoadStore;
  const unsigned ScalarizeMinBits;
};

} // end anonymous namespace

Scatterer::Scatterer(BasicBlock *bb, BasicBlock::iterator bbi, Value *v,
                     const VectorSplit &VS, ValueVector *cachePtr)
    : BB(bb), BBI(bbi), V(v), VS(VS), CachePtr(cachePtr) {
  IsPointer = V->getType()->isPointerTy();
  if (!CachePtr) {
    Tmp.resize(VS.NumFragments, nullptr);
  } else {
    assert((CachePtr->empty() || VS.NumFragments == CachePtr->size() ||
            IsPointer) &&
           "Inconsistent vector sizes");
    if (VS.NumFragments > CachePtr->size())
      CachePtr->resize(VS.NumFragments, nullptr);
  }
}

// Return fragment Frag, creating a new Value for it if necessary.
Value *Scatterer::operator[](unsigned Frag) {
  ValueVector &CV = CachePtr ? *CachePtr : Tmp;
  // Try to reuse a previous value.
  if (CV[Frag])
    return CV[Frag];
  IRBuilder<> Builder(BB, BBI);
  if (IsPointer) {
    if (Frag == 0)
      CV[Frag] = V;
    else
      CV[Frag] = Builder.CreateConstGEP1_32(VS.SplitTy, V, Frag,
                                            V->getName() + ".i" + Twine(Frag));
    return CV[Frag];
  }

  Type *FragmentTy = VS.getFragmentType(Frag);

  if (auto *VecTy = dyn_cast<FixedVectorType>(FragmentTy)) {
    SmallVector<int> Mask;
    for (unsigned J = 0; J < VecTy->getNumElements(); ++J)
      Mask.push_back(Frag * VS.NumPacked + J);
    CV[Frag] =
        Builder.CreateShuffleVector(V, PoisonValue::get(V->getType()), Mask,
                                    V->getName() + ".i" + Twine(Frag));
  } else {
    // Search through a chain of InsertElementInsts looking for element Frag.
    // Record other elements in the cache.  The new V is still suitable
    // for all uncached indices.
    while (true) {
      InsertElementInst *Insert = dyn_cast<InsertElementInst>(V);
      if (!Insert)
        break;
      ConstantInt *Idx = dyn_cast<ConstantInt>(Insert->getOperand(2));
      if (!Idx)
        break;
      unsigned J = Idx->getZExtValue();
      V = Insert->getOperand(0);
      if (Frag * VS.NumPacked == J) {
        CV[Frag] = Insert->getOperand(1);
        return CV[Frag];
      }

      if (VS.NumPacked == 1 && !CV[J]) {
        // Only cache the first entry we find for each index we're not actively
        // searching for. This prevents us from going too far up the chain and
        // caching incorrect entries.
        CV[J] = Insert->getOperand(1);
      }
    }
    CV[Frag] = Builder.CreateExtractElement(V, Frag * VS.NumPacked,
                                            V->getName() + ".i" + Twine(Frag));
  }

  return CV[Frag];
}

bool ScalarizerVisitor::visit(Function &F) {
  assert(Gathered.empty() && Scattered.empty());

  Scalarized = false;

  // To ensure we replace gathered components correctly we need to do an ordered
  // traversal of the basic blocks in the function.
  ReversePostOrderTraversal<BasicBlock *> RPOT(&F.getEntryBlock());
  for (BasicBlock *BB : RPOT) {
    for (BasicBlock::iterator II = BB->begin(), IE = BB->end(); II != IE;) {
      Instruction *I = &*II;
      bool Done = InstVisitor::visit(I);
      ++II;
      if (Done && I->getType()->isVoidTy())
        I->eraseFromParent();
    }
  }
  return finish();
}

// Return a scattered form of V that can be accessed by Point.  V must be a
// vector or a pointer to a vector.
Scatterer ScalarizerVisitor::scatter(Instruction *Point, Value *V,
                                     const VectorSplit &VS) {
  if (Argument *VArg = dyn_cast<Argument>(V)) {
    // Put the scattered form of arguments in the entry block,
    // so that it can be used everywhere.
    Function *F = VArg->getParent();
    BasicBlock *BB = &F->getEntryBlock();
    return Scatterer(BB, BB->begin(), V, VS, &Scattered[{V, VS.SplitTy}]);
  }
  if (Instruction *VOp = dyn_cast<Instruction>(V)) {
    // When scalarizing PHI nodes we might try to examine/rewrite InsertElement
    // nodes in predecessors. If those predecessors are unreachable from entry,
    // then the IR in those blocks could have unexpected properties resulting in
    // infinite loops in Scatterer::operator[]. By simply treating values
    // originating from instructions in unreachable blocks as undef we do not
    // need to analyse them further.
    if (!DT->isReachableFromEntry(VOp->getParent()))
      return Scatterer(Point->getParent(), Point->getIterator(),
                       PoisonValue::get(V->getType()), VS);
    // Put the scattered form of an instruction directly after the
    // instruction, skipping over PHI nodes and debug intrinsics.
    BasicBlock *BB = VOp->getParent();
    return Scatterer(
        BB, skipPastPhiNodesAndDbg(std::next(BasicBlock::iterator(VOp))), V, VS,
        &Scattered[{V, VS.SplitTy}]);
  }
  // In the fallback case, just put the scattered before Point and
  // keep the result local to Point.
  return Scatterer(Point->getParent(), Point->getIterator(), V, VS);
}

// Replace Op with the gathered form of the components in CV.  Defer the
// deletion of Op and creation of the gathered form to the end of the pass,
// so that we can avoid creating the gathered form if all uses of Op are
// replaced with uses of CV.
void ScalarizerVisitor::gather(Instruction *Op, const ValueVector &CV,
                               const VectorSplit &VS) {
  transferMetadataAndIRFlags(Op, CV);

  // If we already have a scattered form of Op (created from ExtractElements
  // of Op itself), replace them with the new form.
  ValueVector &SV = Scattered[{Op, VS.SplitTy}];
  if (!SV.empty()) {
    for (unsigned I = 0, E = SV.size(); I != E; ++I) {
      Value *V = SV[I];
      if (V == nullptr || SV[I] == CV[I])
        continue;

      Instruction *Old = cast<Instruction>(V);
      if (isa<Instruction>(CV[I]))
        CV[I]->takeName(Old);
      Old->replaceAllUsesWith(CV[I]);
      PotentiallyDeadInstrs.emplace_back(Old);
    }
  }
  SV = CV;
  Gathered.push_back(GatherList::value_type(Op, &SV));
}

// Replace Op with CV and collect Op has a potentially dead instruction.
void ScalarizerVisitor::replaceUses(Instruction *Op, Value *CV) {
  if (CV != Op) {
    Op->replaceAllUsesWith(CV);
    PotentiallyDeadInstrs.emplace_back(Op);
    Scalarized = true;
  }
}

// Return true if it is safe to transfer the given metadata tag from
// vector to scalar instructions.
bool ScalarizerVisitor::canTransferMetadata(unsigned Tag) {
  return (Tag == LLVMContext::MD_tbaa
          || Tag == LLVMContext::MD_fpmath
          || Tag == LLVMContext::MD_tbaa_struct
          || Tag == LLVMContext::MD_invariant_load
          || Tag == LLVMContext::MD_alias_scope
          || Tag == LLVMContext::MD_noalias
          || Tag == LLVMContext::MD_mem_parallel_loop_access
          || Tag == LLVMContext::MD_access_group);
}

// Transfer metadata from Op to the instructions in CV if it is known
// to be safe to do so.
void ScalarizerVisitor::transferMetadataAndIRFlags(Instruction *Op,
                                                   const ValueVector &CV) {
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  Op->getAllMetadataOtherThanDebugLoc(MDs);
  for (Value *V : CV) {
    if (Instruction *New = dyn_cast<Instruction>(V)) {
      for (const auto &MD : MDs)
        if (canTransferMetadata(MD.first))
          New->setMetadata(MD.first, MD.second);
      New->copyIRFlags(Op);
      if (Op->getDebugLoc() && !New->getDebugLoc())
        New->setDebugLoc(Op->getDebugLoc());
    }
  }
}

// Determine how Ty is split, if at all.
std::optional<VectorSplit> ScalarizerVisitor::getVectorSplit(Type *Ty) {
  VectorSplit Split;
  Split.VecTy = dyn_cast<FixedVectorType>(Ty);
  if (!Split.VecTy)
    return {};

  unsigned NumElems = Split.VecTy->getNumElements();
  Type *ElemTy = Split.VecTy->getElementType();

  if (NumElems == 1 || ElemTy->isPointerTy() ||
      2 * ElemTy->getScalarSizeInBits() > ScalarizeMinBits) {
    Split.NumPacked = 1;
    Split.NumFragments = NumElems;
    Split.SplitTy = ElemTy;
  } else {
    Split.NumPacked = ScalarizeMinBits / ElemTy->getScalarSizeInBits();
    if (Split.NumPacked >= NumElems)
      return {};

    Split.NumFragments = divideCeil(NumElems, Split.NumPacked);
    Split.SplitTy = FixedVectorType::get(ElemTy, Split.NumPacked);

    unsigned RemainderElems = NumElems % Split.NumPacked;
    if (RemainderElems > 1)
      Split.RemainderTy = FixedVectorType::get(ElemTy, RemainderElems);
    else if (RemainderElems == 1)
      Split.RemainderTy = ElemTy;
  }

  return Split;
}

// Try to fill in Layout from Ty, returning true on success.  Alignment is
// the alignment of the vector, or std::nullopt if the ABI default should be
// used.
std::optional<VectorLayout>
ScalarizerVisitor::getVectorLayout(Type *Ty, Align Alignment,
                                   const DataLayout &DL) {
  std::optional<VectorSplit> VS = getVectorSplit(Ty);
  if (!VS)
    return {};

  VectorLayout Layout;
  Layout.VS = *VS;
  // Check that we're dealing with full-byte fragments.
  if (!DL.typeSizeEqualsStoreSize(VS->SplitTy) ||
      (VS->RemainderTy && !DL.typeSizeEqualsStoreSize(VS->RemainderTy)))
    return {};
  Layout.VecAlign = Alignment;
  Layout.SplitSize = DL.getTypeStoreSize(VS->SplitTy);
  return Layout;
}

// Scalarize one-operand instruction I, using Split(Builder, X, Name)
// to create an instruction like I with operand X and name Name.
template<typename Splitter>
bool ScalarizerVisitor::splitUnary(Instruction &I, const Splitter &Split) {
  std::optional<VectorSplit> VS = getVectorSplit(I.getType());
  if (!VS)
    return false;

  std::optional<VectorSplit> OpVS;
  if (I.getOperand(0)->getType() == I.getType()) {
    OpVS = VS;
  } else {
    OpVS = getVectorSplit(I.getOperand(0)->getType());
    if (!OpVS || VS->NumPacked != OpVS->NumPacked)
      return false;
  }

  IRBuilder<> Builder(&I);
  Scatterer Op = scatter(&I, I.getOperand(0), *OpVS);
  assert(Op.size() == VS->NumFragments && "Mismatched unary operation");
  ValueVector Res;
  Res.resize(VS->NumFragments);
  for (unsigned Frag = 0; Frag < VS->NumFragments; ++Frag)
    Res[Frag] = Split(Builder, Op[Frag], I.getName() + ".i" + Twine(Frag));
  gather(&I, Res, *VS);
  return true;
}

// Scalarize two-operand instruction I, using Split(Builder, X, Y, Name)
// to create an instruction like I with operands X and Y and name Name.
template<typename Splitter>
bool ScalarizerVisitor::splitBinary(Instruction &I, const Splitter &Split) {
  std::optional<VectorSplit> VS = getVectorSplit(I.getType());
  if (!VS)
    return false;

  std::optional<VectorSplit> OpVS;
  if (I.getOperand(0)->getType() == I.getType()) {
    OpVS = VS;
  } else {
    OpVS = getVectorSplit(I.getOperand(0)->getType());
    if (!OpVS || VS->NumPacked != OpVS->NumPacked)
      return false;
  }

  IRBuilder<> Builder(&I);
  Scatterer VOp0 = scatter(&I, I.getOperand(0), *OpVS);
  Scatterer VOp1 = scatter(&I, I.getOperand(1), *OpVS);
  assert(VOp0.size() == VS->NumFragments && "Mismatched binary operation");
  assert(VOp1.size() == VS->NumFragments && "Mismatched binary operation");
  ValueVector Res;
  Res.resize(VS->NumFragments);
  for (unsigned Frag = 0; Frag < VS->NumFragments; ++Frag) {
    Value *Op0 = VOp0[Frag];
    Value *Op1 = VOp1[Frag];
    Res[Frag] = Split(Builder, Op0, Op1, I.getName() + ".i" + Twine(Frag));
  }
  gather(&I, Res, *VS);
  return true;
}

static bool isTriviallyScalariable(Intrinsic::ID ID) {
  return isTriviallyVectorizable(ID);
}

/// If a call to a vector typed intrinsic function, split into a scalar call per
/// element if possible for the intrinsic.
bool ScalarizerVisitor::splitCall(CallInst &CI) {
  std::optional<VectorSplit> VS = getVectorSplit(CI.getType());
  if (!VS)
    return false;

  Function *F = CI.getCalledFunction();
  if (!F)
    return false;

  Intrinsic::ID ID = F->getIntrinsicID();
  if (ID == Intrinsic::not_intrinsic || !isTriviallyScalariable(ID))
    return false;

  // unsigned NumElems = VT->getNumElements();
  unsigned NumArgs = CI.arg_size();

  ValueVector ScalarOperands(NumArgs);
  SmallVector<Scatterer, 8> Scattered(NumArgs);
  SmallVector<int> OverloadIdx(NumArgs, -1);

  SmallVector<llvm::Type *, 3> Tys;
  // Add return type if intrinsic is overloaded on it.
  if (isVectorIntrinsicWithOverloadTypeAtArg(ID, -1))
    Tys.push_back(VS->SplitTy);

  // Assumes that any vector type has the same number of elements as the return
  // vector type, which is true for all current intrinsics.
  for (unsigned I = 0; I != NumArgs; ++I) {
    Value *OpI = CI.getOperand(I);
    if ([[maybe_unused]] auto *OpVecTy =
            dyn_cast<FixedVectorType>(OpI->getType())) {
      assert(OpVecTy->getNumElements() == VS->VecTy->getNumElements());
      std::optional<VectorSplit> OpVS = getVectorSplit(OpI->getType());
      if (!OpVS || OpVS->NumPacked != VS->NumPacked) {
        // The natural split of the operand doesn't match the result. This could
        // happen if the vector elements are different and the ScalarizeMinBits
        // option is used.
        //
        // We could in principle handle this case as well, at the cost of
        // complicating the scattering machinery to support multiple scattering
        // granularities for a single value.
        return false;
      }

      Scattered[I] = scatter(&CI, OpI, *OpVS);
      if (isVectorIntrinsicWithOverloadTypeAtArg(ID, I)) {
        OverloadIdx[I] = Tys.size();
        Tys.push_back(OpVS->SplitTy);
      }
    } else {
      ScalarOperands[I] = OpI;
      if (isVectorIntrinsicWithOverloadTypeAtArg(ID, I))
        Tys.push_back(OpI->getType());
    }
  }

  ValueVector Res(VS->NumFragments);
  ValueVector ScalarCallOps(NumArgs);

  Function *NewIntrin = Intrinsic::getDeclaration(F->getParent(), ID, Tys);
  IRBuilder<> Builder(&CI);

  // Perform actual scalarization, taking care to preserve any scalar operands.
  for (unsigned I = 0; I < VS->NumFragments; ++I) {
    bool IsRemainder = I == VS->NumFragments - 1 && VS->RemainderTy;
    ScalarCallOps.clear();

    if (IsRemainder)
      Tys[0] = VS->RemainderTy;

    for (unsigned J = 0; J != NumArgs; ++J) {
      if (isVectorIntrinsicWithScalarOpAtArg(ID, J)) {
        ScalarCallOps.push_back(ScalarOperands[J]);
      } else {
        ScalarCallOps.push_back(Scattered[J][I]);
        if (IsRemainder && OverloadIdx[J] >= 0)
          Tys[OverloadIdx[J]] = Scattered[J][I]->getType();
      }
    }

    if (IsRemainder)
      NewIntrin = Intrinsic::getDeclaration(F->getParent(), ID, Tys);

    Res[I] = Builder.CreateCall(NewIntrin, ScalarCallOps,
                                CI.getName() + ".i" + Twine(I));
  }

  gather(&CI, Res, *VS);
  return true;
}

bool ScalarizerVisitor::visitSelectInst(SelectInst &SI) {
  std::optional<VectorSplit> VS = getVectorSplit(SI.getType());
  if (!VS)
    return false;

  std::optional<VectorSplit> CondVS;
  if (isa<FixedVectorType>(SI.getCondition()->getType())) {
    CondVS = getVectorSplit(SI.getCondition()->getType());
    if (!CondVS || CondVS->NumPacked != VS->NumPacked) {
      // This happens when ScalarizeMinBits is used.
      return false;
    }
  }

  IRBuilder<> Builder(&SI);
  Scatterer VOp1 = scatter(&SI, SI.getOperand(1), *VS);
  Scatterer VOp2 = scatter(&SI, SI.getOperand(2), *VS);
  assert(VOp1.size() == VS->NumFragments && "Mismatched select");
  assert(VOp2.size() == VS->NumFragments && "Mismatched select");
  ValueVector Res;
  Res.resize(VS->NumFragments);

  if (CondVS) {
    Scatterer VOp0 = scatter(&SI, SI.getOperand(0), *CondVS);
    assert(VOp0.size() == CondVS->NumFragments && "Mismatched select");
    for (unsigned I = 0; I < VS->NumFragments; ++I) {
      Value *Op0 = VOp0[I];
      Value *Op1 = VOp1[I];
      Value *Op2 = VOp2[I];
      Res[I] = Builder.CreateSelect(Op0, Op1, Op2,
                                    SI.getName() + ".i" + Twine(I));
    }
  } else {
    Value *Op0 = SI.getOperand(0);
    for (unsigned I = 0; I < VS->NumFragments; ++I) {
      Value *Op1 = VOp1[I];
      Value *Op2 = VOp2[I];
      Res[I] = Builder.CreateSelect(Op0, Op1, Op2,
                                    SI.getName() + ".i" + Twine(I));
    }
  }
  gather(&SI, Res, *VS);
  return true;
}

bool ScalarizerVisitor::visitICmpInst(ICmpInst &ICI) {
  return splitBinary(ICI, ICmpSplitter(ICI));
}

bool ScalarizerVisitor::visitFCmpInst(FCmpInst &FCI) {
  return splitBinary(FCI, FCmpSplitter(FCI));
}

bool ScalarizerVisitor::visitUnaryOperator(UnaryOperator &UO) {
  return splitUnary(UO, UnarySplitter(UO));
}

bool ScalarizerVisitor::visitBinaryOperator(BinaryOperator &BO) {
  return splitBinary(BO, BinarySplitter(BO));
}

bool ScalarizerVisitor::visitGetElementPtrInst(GetElementPtrInst &GEPI) {
  std::optional<VectorSplit> VS = getVectorSplit(GEPI.getType());
  if (!VS)
    return false;

  IRBuilder<> Builder(&GEPI);
  unsigned NumIndices = GEPI.getNumIndices();

  // The base pointer and indices might be scalar even if it's a vector GEP.
  SmallVector<Value *, 8> ScalarOps{1 + NumIndices};
  SmallVector<Scatterer, 8> ScatterOps{1 + NumIndices};

  for (unsigned I = 0; I < 1 + NumIndices; ++I) {
    if (auto *VecTy =
            dyn_cast<FixedVectorType>(GEPI.getOperand(I)->getType())) {
      std::optional<VectorSplit> OpVS = getVectorSplit(VecTy);
      if (!OpVS || OpVS->NumPacked != VS->NumPacked) {
        // This can happen when ScalarizeMinBits is used.
        return false;
      }
      ScatterOps[I] = scatter(&GEPI, GEPI.getOperand(I), *OpVS);
    } else {
      ScalarOps[I] = GEPI.getOperand(I);
    }
  }

  ValueVector Res;
  Res.resize(VS->NumFragments);
  for (unsigned I = 0; I < VS->NumFragments; ++I) {
    SmallVector<Value *, 8> SplitOps;
    SplitOps.resize(1 + NumIndices);
    for (unsigned J = 0; J < 1 + NumIndices; ++J) {
      if (ScalarOps[J])
        SplitOps[J] = ScalarOps[J];
      else
        SplitOps[J] = ScatterOps[J][I];
    }
    Res[I] = Builder.CreateGEP(GEPI.getSourceElementType(), SplitOps[0],
                               ArrayRef(SplitOps).drop_front(),
                               GEPI.getName() + ".i" + Twine(I));
    if (GEPI.isInBounds())
      if (GetElementPtrInst *NewGEPI = dyn_cast<GetElementPtrInst>(Res[I]))
        NewGEPI->setIsInBounds();
  }
  gather(&GEPI, Res, *VS);
  return true;
}

bool ScalarizerVisitor::visitCastInst(CastInst &CI) {
  std::optional<VectorSplit> DestVS = getVectorSplit(CI.getDestTy());
  if (!DestVS)
    return false;

  std::optional<VectorSplit> SrcVS = getVectorSplit(CI.getSrcTy());
  if (!SrcVS || SrcVS->NumPacked != DestVS->NumPacked)
    return false;

  IRBuilder<> Builder(&CI);
  Scatterer Op0 = scatter(&CI, CI.getOperand(0), *SrcVS);
  assert(Op0.size() == SrcVS->NumFragments && "Mismatched cast");
  ValueVector Res;
  Res.resize(DestVS->NumFragments);
  for (unsigned I = 0; I < DestVS->NumFragments; ++I)
    Res[I] =
        Builder.CreateCast(CI.getOpcode(), Op0[I], DestVS->getFragmentType(I),
                           CI.getName() + ".i" + Twine(I));
  gather(&CI, Res, *DestVS);
  return true;
}

bool ScalarizerVisitor::visitBitCastInst(BitCastInst &BCI) {
  std::optional<VectorSplit> DstVS = getVectorSplit(BCI.getDestTy());
  std::optional<VectorSplit> SrcVS = getVectorSplit(BCI.getSrcTy());
  if (!DstVS || !SrcVS || DstVS->RemainderTy || SrcVS->RemainderTy)
    return false;

  const bool isPointerTy = DstVS->VecTy->getElementType()->isPointerTy();

  // Vectors of pointers are always fully scalarized.
  assert(!isPointerTy || (DstVS->NumPacked == 1 && SrcVS->NumPacked == 1));

  IRBuilder<> Builder(&BCI);
  Scatterer Op0 = scatter(&BCI, BCI.getOperand(0), *SrcVS);
  ValueVector Res;
  Res.resize(DstVS->NumFragments);

  unsigned DstSplitBits = DstVS->SplitTy->getPrimitiveSizeInBits();
  unsigned SrcSplitBits = SrcVS->SplitTy->getPrimitiveSizeInBits();

  if (isPointerTy || DstSplitBits == SrcSplitBits) {
    assert(DstVS->NumFragments == SrcVS->NumFragments);
    for (unsigned I = 0; I < DstVS->NumFragments; ++I) {
      Res[I] = Builder.CreateBitCast(Op0[I], DstVS->getFragmentType(I),
                                     BCI.getName() + ".i" + Twine(I));
    }
  } else if (SrcSplitBits % DstSplitBits == 0) {
    // Convert each source fragment to the same-sized destination vector and
    // then scatter the result to the destination.
    VectorSplit MidVS;
    MidVS.NumPacked = DstVS->NumPacked;
    MidVS.NumFragments = SrcSplitBits / DstSplitBits;
    MidVS.VecTy = FixedVectorType::get(DstVS->VecTy->getElementType(),
                                       MidVS.NumPacked * MidVS.NumFragments);
    MidVS.SplitTy = DstVS->SplitTy;

    unsigned ResI = 0;
    for (unsigned I = 0; I < SrcVS->NumFragments; ++I) {
      Value *V = Op0[I];

      // Look through any existing bitcasts before converting to <N x t2>.
      // In the best case, the resulting conversion might be a no-op.
      Instruction *VI;
      while ((VI = dyn_cast<Instruction>(V)) &&
             VI->getOpcode() == Instruction::BitCast)
        V = VI->getOperand(0);

      V = Builder.CreateBitCast(V, MidVS.VecTy, V->getName() + ".cast");

      Scatterer Mid = scatter(&BCI, V, MidVS);
      for (unsigned J = 0; J < MidVS.NumFragments; ++J)
        Res[ResI++] = Mid[J];
    }
  } else if (DstSplitBits % SrcSplitBits == 0) {
    // Gather enough source fragments to make up a destination fragment and
    // then convert to the destination type.
    VectorSplit MidVS;
    MidVS.NumFragments = DstSplitBits / SrcSplitBits;
    MidVS.NumPacked = SrcVS->NumPacked;
    MidVS.VecTy = FixedVectorType::get(SrcVS->VecTy->getElementType(),
                                       MidVS.NumPacked * MidVS.NumFragments);
    MidVS.SplitTy = SrcVS->SplitTy;

    unsigned SrcI = 0;
    SmallVector<Value *, 8> ConcatOps;
    ConcatOps.resize(MidVS.NumFragments);
    for (unsigned I = 0; I < DstVS->NumFragments; ++I) {
      for (unsigned J = 0; J < MidVS.NumFragments; ++J)
        ConcatOps[J] = Op0[SrcI++];
      Value *V = concatenate(Builder, ConcatOps, MidVS,
                             BCI.getName() + ".i" + Twine(I));
      Res[I] = Builder.CreateBitCast(V, DstVS->getFragmentType(I),
                                     BCI.getName() + ".i" + Twine(I));
    }
  } else {
    return false;
  }

  gather(&BCI, Res, *DstVS);
  return true;
}

bool ScalarizerVisitor::visitInsertElementInst(InsertElementInst &IEI) {
  std::optional<VectorSplit> VS = getVectorSplit(IEI.getType());
  if (!VS)
    return false;

  IRBuilder<> Builder(&IEI);
  Scatterer Op0 = scatter(&IEI, IEI.getOperand(0), *VS);
  Value *NewElt = IEI.getOperand(1);
  Value *InsIdx = IEI.getOperand(2);

  ValueVector Res;
  Res.resize(VS->NumFragments);

  if (auto *CI = dyn_cast<ConstantInt>(InsIdx)) {
    unsigned Idx = CI->getZExtValue();
    unsigned Fragment = Idx / VS->NumPacked;
    for (unsigned I = 0; I < VS->NumFragments; ++I) {
      if (I == Fragment) {
        bool IsPacked = VS->NumPacked > 1;
        if (Fragment == VS->NumFragments - 1 && VS->RemainderTy &&
            !VS->RemainderTy->isVectorTy())
          IsPacked = false;
        if (IsPacked) {
          Res[I] =
              Builder.CreateInsertElement(Op0[I], NewElt, Idx % VS->NumPacked);
        } else {
          Res[I] = NewElt;
        }
      } else {
        Res[I] = Op0[I];
      }
    }
  } else {
    // Never split a variable insertelement that isn't fully scalarized.
    if (!ScalarizeVariableInsertExtract || VS->NumPacked > 1)
      return false;

    for (unsigned I = 0; I < VS->NumFragments; ++I) {
      Value *ShouldReplace =
          Builder.CreateICmpEQ(InsIdx, ConstantInt::get(InsIdx->getType(), I),
                               InsIdx->getName() + ".is." + Twine(I));
      Value *OldElt = Op0[I];
      Res[I] = Builder.CreateSelect(ShouldReplace, NewElt, OldElt,
                                    IEI.getName() + ".i" + Twine(I));
    }
  }

  gather(&IEI, Res, *VS);
  return true;
}

bool ScalarizerVisitor::visitExtractElementInst(ExtractElementInst &EEI) {
  std::optional<VectorSplit> VS = getVectorSplit(EEI.getOperand(0)->getType());
  if (!VS)
    return false;

  IRBuilder<> Builder(&EEI);
  Scatterer Op0 = scatter(&EEI, EEI.getOperand(0), *VS);
  Value *ExtIdx = EEI.getOperand(1);

  if (auto *CI = dyn_cast<ConstantInt>(ExtIdx)) {
    unsigned Idx = CI->getZExtValue();
    unsigned Fragment = Idx / VS->NumPacked;
    Value *Res = Op0[Fragment];
    bool IsPacked = VS->NumPacked > 1;
    if (Fragment == VS->NumFragments - 1 && VS->RemainderTy &&
        !VS->RemainderTy->isVectorTy())
      IsPacked = false;
    if (IsPacked)
      Res = Builder.CreateExtractElement(Res, Idx % VS->NumPacked);
    replaceUses(&EEI, Res);
    return true;
  }

  // Never split a variable extractelement that isn't fully scalarized.
  if (!ScalarizeVariableInsertExtract || VS->NumPacked > 1)
    return false;

  Value *Res = PoisonValue::get(VS->VecTy->getElementType());
  for (unsigned I = 0; I < VS->NumFragments; ++I) {
    Value *ShouldExtract =
        Builder.CreateICmpEQ(ExtIdx, ConstantInt::get(ExtIdx->getType(), I),
                             ExtIdx->getName() + ".is." + Twine(I));
    Value *Elt = Op0[I];
    Res = Builder.CreateSelect(ShouldExtract, Elt, Res,
                               EEI.getName() + ".upto" + Twine(I));
  }
  replaceUses(&EEI, Res);
  return true;
}

bool ScalarizerVisitor::visitShuffleVectorInst(ShuffleVectorInst &SVI) {
  std::optional<VectorSplit> VS = getVectorSplit(SVI.getType());
  std::optional<VectorSplit> VSOp =
      getVectorSplit(SVI.getOperand(0)->getType());
  if (!VS || !VSOp || VS->NumPacked > 1 || VSOp->NumPacked > 1)
    return false;

  Scatterer Op0 = scatter(&SVI, SVI.getOperand(0), *VSOp);
  Scatterer Op1 = scatter(&SVI, SVI.getOperand(1), *VSOp);
  ValueVector Res;
  Res.resize(VS->NumFragments);

  for (unsigned I = 0; I < VS->NumFragments; ++I) {
    int Selector = SVI.getMaskValue(I);
    if (Selector < 0)
      Res[I] = PoisonValue::get(VS->VecTy->getElementType());
    else if (unsigned(Selector) < Op0.size())
      Res[I] = Op0[Selector];
    else
      Res[I] = Op1[Selector - Op0.size()];
  }
  gather(&SVI, Res, *VS);
  return true;
}

bool ScalarizerVisitor::visitPHINode(PHINode &PHI) {
  std::optional<VectorSplit> VS = getVectorSplit(PHI.getType());
  if (!VS)
    return false;

  IRBuilder<> Builder(&PHI);
  ValueVector Res;
  Res.resize(VS->NumFragments);

  unsigned NumOps = PHI.getNumOperands();
  for (unsigned I = 0; I < VS->NumFragments; ++I) {
    Res[I] = Builder.CreatePHI(VS->getFragmentType(I), NumOps,
                               PHI.getName() + ".i" + Twine(I));
  }

  for (unsigned I = 0; I < NumOps; ++I) {
    Scatterer Op = scatter(&PHI, PHI.getIncomingValue(I), *VS);
    BasicBlock *IncomingBlock = PHI.getIncomingBlock(I);
    for (unsigned J = 0; J < VS->NumFragments; ++J)
      cast<PHINode>(Res[J])->addIncoming(Op[J], IncomingBlock);
  }
  gather(&PHI, Res, *VS);
  return true;
}

bool ScalarizerVisitor::visitLoadInst(LoadInst &LI) {
  if (!ScalarizeLoadStore)
    return false;
  if (!LI.isSimple())
    return false;

  std::optional<VectorLayout> Layout = getVectorLayout(
      LI.getType(), LI.getAlign(), LI.getDataLayout());
  if (!Layout)
    return false;

  IRBuilder<> Builder(&LI);
  Scatterer Ptr = scatter(&LI, LI.getPointerOperand(), Layout->VS);
  ValueVector Res;
  Res.resize(Layout->VS.NumFragments);

  for (unsigned I = 0; I < Layout->VS.NumFragments; ++I) {
    Res[I] = Builder.CreateAlignedLoad(Layout->VS.getFragmentType(I), Ptr[I],
                                       Align(Layout->getFragmentAlign(I)),
                                       LI.getName() + ".i" + Twine(I));
  }
  gather(&LI, Res, Layout->VS);
  return true;
}

bool ScalarizerVisitor::visitStoreInst(StoreInst &SI) {
  if (!ScalarizeLoadStore)
    return false;
  if (!SI.isSimple())
    return false;

  Value *FullValue = SI.getValueOperand();
  std::optional<VectorLayout> Layout = getVectorLayout(
      FullValue->getType(), SI.getAlign(), SI.getDataLayout());
  if (!Layout)
    return false;

  IRBuilder<> Builder(&SI);
  Scatterer VPtr = scatter(&SI, SI.getPointerOperand(), Layout->VS);
  Scatterer VVal = scatter(&SI, FullValue, Layout->VS);

  ValueVector Stores;
  Stores.resize(Layout->VS.NumFragments);
  for (unsigned I = 0; I < Layout->VS.NumFragments; ++I) {
    Value *Val = VVal[I];
    Value *Ptr = VPtr[I];
    Stores[I] =
        Builder.CreateAlignedStore(Val, Ptr, Layout->getFragmentAlign(I));
  }
  transferMetadataAndIRFlags(&SI, Stores);
  return true;
}

bool ScalarizerVisitor::visitCallInst(CallInst &CI) {
  return splitCall(CI);
}

bool ScalarizerVisitor::visitFreezeInst(FreezeInst &FI) {
  return splitUnary(FI, [](IRBuilder<> &Builder, Value *Op, const Twine &Name) {
    return Builder.CreateFreeze(Op, Name);
  });
}

// Delete the instructions that we scalarized.  If a full vector result
// is still needed, recreate it using InsertElements.
bool ScalarizerVisitor::finish() {
  // The presence of data in Gathered or Scattered indicates changes
  // made to the Function.
  if (Gathered.empty() && Scattered.empty() && !Scalarized)
    return false;
  for (const auto &GMI : Gathered) {
    Instruction *Op = GMI.first;
    ValueVector &CV = *GMI.second;
    if (!Op->use_empty()) {
      // The value is still needed, so recreate it using a series of
      // insertelements and/or shufflevectors.
      Value *Res;
      if (auto *Ty = dyn_cast<FixedVectorType>(Op->getType())) {
        BasicBlock *BB = Op->getParent();
        IRBuilder<> Builder(Op);
        if (isa<PHINode>(Op))
          Builder.SetInsertPoint(BB, BB->getFirstInsertionPt());

        VectorSplit VS = *getVectorSplit(Ty);
        assert(VS.NumFragments == CV.size());

        Res = concatenate(Builder, CV, VS, Op->getName());

        Res->takeName(Op);
      } else {
        assert(CV.size() == 1 && Op->getType() == CV[0]->getType());
        Res = CV[0];
        if (Op == Res)
          continue;
      }
      Op->replaceAllUsesWith(Res);
    }
    PotentiallyDeadInstrs.emplace_back(Op);
  }
  Gathered.clear();
  Scattered.clear();
  Scalarized = false;

  RecursivelyDeleteTriviallyDeadInstructionsPermissive(PotentiallyDeadInstrs);

  return true;
}

PreservedAnalyses ScalarizerPass::run(Function &F, FunctionAnalysisManager &AM) {
  DominatorTree *DT = &AM.getResult<DominatorTreeAnalysis>(F);
  ScalarizerVisitor Impl(DT, Options);
  bool Changed = Impl.visit(F);
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  return Changed ? PA : PreservedAnalyses::all();
}
