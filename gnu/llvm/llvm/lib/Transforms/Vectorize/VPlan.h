//===- VPlan.h - Represent A Vectorizer Plan --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains the declarations of the Vectorization Plan base classes:
/// 1. VPBasicBlock and VPRegionBlock that inherit from a common pure virtual
///    VPBlockBase, together implementing a Hierarchical CFG;
/// 2. Pure virtual VPRecipeBase serving as the base class for recipes contained
///    within VPBasicBlocks;
/// 3. Pure virtual VPSingleDefRecipe serving as a base class for recipes that
///    also inherit from VPValue.
/// 4. VPInstruction, a concrete Recipe and VPUser modeling a single planned
///    instruction;
/// 5. The VPlan class holding a candidate for vectorization;
/// 6. The VPlanPrinter class providing a way to print a plan in dot format;
/// These are documented in docs/VectorizationPlan.rst.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_VPLAN_H
#define LLVM_TRANSFORMS_VECTORIZE_VPLAN_H

#include "VPlanAnalysis.h"
#include "VPlanValue.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/InstructionCost.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>

namespace llvm {

class BasicBlock;
class DominatorTree;
class InnerLoopVectorizer;
class IRBuilderBase;
class LoopInfo;
class raw_ostream;
class RecurrenceDescriptor;
class SCEV;
class Type;
class VPBasicBlock;
class VPRegionBlock;
class VPlan;
class VPReplicateRecipe;
class VPlanSlp;
class Value;
class LoopVectorizationCostModel;
class LoopVersioning;

struct VPCostContext;

namespace Intrinsic {
typedef unsigned ID;
}

/// Returns a calculation for the total number of elements for a given \p VF.
/// For fixed width vectors this value is a constant, whereas for scalable
/// vectors it is an expression determined at runtime.
Value *getRuntimeVF(IRBuilderBase &B, Type *Ty, ElementCount VF);

/// Return a value for Step multiplied by VF.
Value *createStepForVF(IRBuilderBase &B, Type *Ty, ElementCount VF,
                       int64_t Step);

const SCEV *createTripCountSCEV(Type *IdxTy, PredicatedScalarEvolution &PSE,
                                Loop *CurLoop = nullptr);

/// A helper function that returns the reciprocal of the block probability of
/// predicated blocks. If we return X, we are assuming the predicated block
/// will execute once for every X iterations of the loop header.
///
/// TODO: We should use actual block probability here, if available. Currently,
///       we always assume predicated blocks have a 50% chance of executing.
inline unsigned getReciprocalPredBlockProb() { return 2; }

/// A range of powers-of-2 vectorization factors with fixed start and
/// adjustable end. The range includes start and excludes end, e.g.,:
/// [1, 16) = {1, 2, 4, 8}
struct VFRange {
  // A power of 2.
  const ElementCount Start;

  // A power of 2. If End <= Start range is empty.
  ElementCount End;

  bool isEmpty() const {
    return End.getKnownMinValue() <= Start.getKnownMinValue();
  }

  VFRange(const ElementCount &Start, const ElementCount &End)
      : Start(Start), End(End) {
    assert(Start.isScalable() == End.isScalable() &&
           "Both Start and End should have the same scalable flag");
    assert(isPowerOf2_32(Start.getKnownMinValue()) &&
           "Expected Start to be a power of 2");
    assert(isPowerOf2_32(End.getKnownMinValue()) &&
           "Expected End to be a power of 2");
  }

  /// Iterator to iterate over vectorization factors in a VFRange.
  class iterator
      : public iterator_facade_base<iterator, std::forward_iterator_tag,
                                    ElementCount> {
    ElementCount VF;

  public:
    iterator(ElementCount VF) : VF(VF) {}

    bool operator==(const iterator &Other) const { return VF == Other.VF; }

    ElementCount operator*() const { return VF; }

    iterator &operator++() {
      VF *= 2;
      return *this;
    }
  };

  iterator begin() { return iterator(Start); }
  iterator end() {
    assert(isPowerOf2_32(End.getKnownMinValue()));
    return iterator(End);
  }
};

using VPlanPtr = std::unique_ptr<VPlan>;

/// In what follows, the term "input IR" refers to code that is fed into the
/// vectorizer whereas the term "output IR" refers to code that is generated by
/// the vectorizer.

/// VPLane provides a way to access lanes in both fixed width and scalable
/// vectors, where for the latter the lane index sometimes needs calculating
/// as a runtime expression.
class VPLane {
public:
  /// Kind describes how to interpret Lane.
  enum class Kind : uint8_t {
    /// For First, Lane is the index into the first N elements of a
    /// fixed-vector <N x <ElTy>> or a scalable vector <vscale x N x <ElTy>>.
    First,
    /// For ScalableLast, Lane is the offset from the start of the last
    /// N-element subvector in a scalable vector <vscale x N x <ElTy>>. For
    /// example, a Lane of 0 corresponds to lane `(vscale - 1) * N`, a Lane of
    /// 1 corresponds to `((vscale - 1) * N) + 1`, etc.
    ScalableLast
  };

private:
  /// in [0..VF)
  unsigned Lane;

  /// Indicates how the Lane should be interpreted, as described above.
  Kind LaneKind;

public:
  VPLane(unsigned Lane, Kind LaneKind) : Lane(Lane), LaneKind(LaneKind) {}

  static VPLane getFirstLane() { return VPLane(0, VPLane::Kind::First); }

  static VPLane getLaneFromEnd(const ElementCount &VF, unsigned Offset) {
    assert(Offset > 0 && Offset <= VF.getKnownMinValue() &&
           "trying to extract with invalid offset");
    unsigned LaneOffset = VF.getKnownMinValue() - Offset;
    Kind LaneKind;
    if (VF.isScalable())
      // In this case 'LaneOffset' refers to the offset from the start of the
      // last subvector with VF.getKnownMinValue() elements.
      LaneKind = VPLane::Kind::ScalableLast;
    else
      LaneKind = VPLane::Kind::First;
    return VPLane(LaneOffset, LaneKind);
  }

  static VPLane getLastLaneForVF(const ElementCount &VF) {
    return getLaneFromEnd(VF, 1);
  }

  /// Returns a compile-time known value for the lane index and asserts if the
  /// lane can only be calculated at runtime.
  unsigned getKnownLane() const {
    assert(LaneKind == Kind::First);
    return Lane;
  }

  /// Returns an expression describing the lane index that can be used at
  /// runtime.
  Value *getAsRuntimeExpr(IRBuilderBase &Builder, const ElementCount &VF) const;

  /// Returns the Kind of lane offset.
  Kind getKind() const { return LaneKind; }

  /// Returns true if this is the first lane of the whole vector.
  bool isFirstLane() const { return Lane == 0 && LaneKind == Kind::First; }

  /// Maps the lane to a cache index based on \p VF.
  unsigned mapToCacheIndex(const ElementCount &VF) const {
    switch (LaneKind) {
    case VPLane::Kind::ScalableLast:
      assert(VF.isScalable() && Lane < VF.getKnownMinValue());
      return VF.getKnownMinValue() + Lane;
    default:
      assert(Lane < VF.getKnownMinValue());
      return Lane;
    }
  }

  /// Returns the maxmimum number of lanes that we are able to consider
  /// caching for \p VF.
  static unsigned getNumCachedLanes(const ElementCount &VF) {
    return VF.getKnownMinValue() * (VF.isScalable() ? 2 : 1);
  }
};

/// VPIteration represents a single point in the iteration space of the output
/// (vectorized and/or unrolled) IR loop.
struct VPIteration {
  /// in [0..UF)
  unsigned Part;

  VPLane Lane;

  VPIteration(unsigned Part, unsigned Lane,
              VPLane::Kind Kind = VPLane::Kind::First)
      : Part(Part), Lane(Lane, Kind) {}

  VPIteration(unsigned Part, const VPLane &Lane) : Part(Part), Lane(Lane) {}

  bool isFirstIteration() const { return Part == 0 && Lane.isFirstLane(); }
};

/// VPTransformState holds information passed down when "executing" a VPlan,
/// needed for generating the output IR.
struct VPTransformState {
  VPTransformState(ElementCount VF, unsigned UF, LoopInfo *LI,
                   DominatorTree *DT, IRBuilderBase &Builder,
                   InnerLoopVectorizer *ILV, VPlan *Plan, LLVMContext &Ctx);

  /// The chosen Vectorization and Unroll Factors of the loop being vectorized.
  ElementCount VF;
  unsigned UF;

  /// Hold the indices to generate specific scalar instructions. Null indicates
  /// that all instances are to be generated, using either scalar or vector
  /// instructions.
  std::optional<VPIteration> Instance;

  struct DataState {
    /// A type for vectorized values in the new loop. Each value from the
    /// original loop, when vectorized, is represented by UF vector values in
    /// the new unrolled loop, where UF is the unroll factor.
    typedef SmallVector<Value *, 2> PerPartValuesTy;

    DenseMap<VPValue *, PerPartValuesTy> PerPartOutput;

    using ScalarsPerPartValuesTy = SmallVector<SmallVector<Value *, 4>, 2>;
    DenseMap<VPValue *, ScalarsPerPartValuesTy> PerPartScalars;
  } Data;

  /// Get the generated vector Value for a given VPValue \p Def and a given \p
  /// Part if \p IsScalar is false, otherwise return the generated scalar
  /// for \p Part. \See set.
  Value *get(VPValue *Def, unsigned Part, bool IsScalar = false);

  /// Get the generated Value for a given VPValue and given Part and Lane.
  Value *get(VPValue *Def, const VPIteration &Instance);

  bool hasVectorValue(VPValue *Def, unsigned Part) {
    auto I = Data.PerPartOutput.find(Def);
    return I != Data.PerPartOutput.end() && Part < I->second.size() &&
           I->second[Part];
  }

  bool hasScalarValue(VPValue *Def, VPIteration Instance) {
    auto I = Data.PerPartScalars.find(Def);
    if (I == Data.PerPartScalars.end())
      return false;
    unsigned CacheIdx = Instance.Lane.mapToCacheIndex(VF);
    return Instance.Part < I->second.size() &&
           CacheIdx < I->second[Instance.Part].size() &&
           I->second[Instance.Part][CacheIdx];
  }

  /// Set the generated vector Value for a given VPValue and a given Part, if \p
  /// IsScalar is false. If \p IsScalar is true, set the scalar in (Part, 0).
  void set(VPValue *Def, Value *V, unsigned Part, bool IsScalar = false) {
    if (IsScalar) {
      set(Def, V, VPIteration(Part, 0));
      return;
    }
    assert((VF.isScalar() || V->getType()->isVectorTy()) &&
           "scalar values must be stored as (Part, 0)");
    if (!Data.PerPartOutput.count(Def)) {
      DataState::PerPartValuesTy Entry(UF);
      Data.PerPartOutput[Def] = Entry;
    }
    Data.PerPartOutput[Def][Part] = V;
  }

  /// Reset an existing vector value for \p Def and a given \p Part.
  void reset(VPValue *Def, Value *V, unsigned Part) {
    auto Iter = Data.PerPartOutput.find(Def);
    assert(Iter != Data.PerPartOutput.end() &&
           "need to overwrite existing value");
    Iter->second[Part] = V;
  }

  /// Set the generated scalar \p V for \p Def and the given \p Instance.
  void set(VPValue *Def, Value *V, const VPIteration &Instance) {
    auto Iter = Data.PerPartScalars.insert({Def, {}});
    auto &PerPartVec = Iter.first->second;
    if (PerPartVec.size() <= Instance.Part)
      PerPartVec.resize(Instance.Part + 1);
    auto &Scalars = PerPartVec[Instance.Part];
    unsigned CacheIdx = Instance.Lane.mapToCacheIndex(VF);
    if (Scalars.size() <= CacheIdx)
      Scalars.resize(CacheIdx + 1);
    assert(!Scalars[CacheIdx] && "should overwrite existing value");
    Scalars[CacheIdx] = V;
  }

  /// Reset an existing scalar value for \p Def and a given \p Instance.
  void reset(VPValue *Def, Value *V, const VPIteration &Instance) {
    auto Iter = Data.PerPartScalars.find(Def);
    assert(Iter != Data.PerPartScalars.end() &&
           "need to overwrite existing value");
    assert(Instance.Part < Iter->second.size() &&
           "need to overwrite existing value");
    unsigned CacheIdx = Instance.Lane.mapToCacheIndex(VF);
    assert(CacheIdx < Iter->second[Instance.Part].size() &&
           "need to overwrite existing value");
    Iter->second[Instance.Part][CacheIdx] = V;
  }

  /// Add additional metadata to \p To that was not present on \p Orig.
  ///
  /// Currently this is used to add the noalias annotations based on the
  /// inserted memchecks.  Use this for instructions that are *cloned* into the
  /// vector loop.
  void addNewMetadata(Instruction *To, const Instruction *Orig);

  /// Add metadata from one instruction to another.
  ///
  /// This includes both the original MDs from \p From and additional ones (\see
  /// addNewMetadata).  Use this for *newly created* instructions in the vector
  /// loop.
  void addMetadata(Value *To, Instruction *From);

  /// Set the debug location in the builder using the debug location \p DL.
  void setDebugLocFrom(DebugLoc DL);

  /// Construct the vector value of a scalarized value \p V one lane at a time.
  void packScalarIntoVectorValue(VPValue *Def, const VPIteration &Instance);

  /// Hold state information used when constructing the CFG of the output IR,
  /// traversing the VPBasicBlocks and generating corresponding IR BasicBlocks.
  struct CFGState {
    /// The previous VPBasicBlock visited. Initially set to null.
    VPBasicBlock *PrevVPBB = nullptr;

    /// The previous IR BasicBlock created or used. Initially set to the new
    /// header BasicBlock.
    BasicBlock *PrevBB = nullptr;

    /// The last IR BasicBlock in the output IR. Set to the exit block of the
    /// vector loop.
    BasicBlock *ExitBB = nullptr;

    /// A mapping of each VPBasicBlock to the corresponding BasicBlock. In case
    /// of replication, maps the BasicBlock of the last replica created.
    SmallDenseMap<VPBasicBlock *, BasicBlock *> VPBB2IRBB;

    /// Updater for the DominatorTree.
    DomTreeUpdater DTU;

    CFGState(DominatorTree *DT)
        : DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy) {}

    /// Returns the BasicBlock* mapped to the pre-header of the loop region
    /// containing \p R.
    BasicBlock *getPreheaderBBFor(VPRecipeBase *R);
  } CFG;

  /// Hold a pointer to LoopInfo to register new basic blocks in the loop.
  LoopInfo *LI;

  /// Hold a reference to the IRBuilder used to generate output IR code.
  IRBuilderBase &Builder;

  /// Hold a pointer to InnerLoopVectorizer to reuse its IR generation methods.
  InnerLoopVectorizer *ILV;

  /// Pointer to the VPlan code is generated for.
  VPlan *Plan;

  /// The loop object for the current parent region, or nullptr.
  Loop *CurrentVectorLoop = nullptr;

  /// LoopVersioning.  It's only set up (non-null) if memchecks were
  /// used.
  ///
  /// This is currently only used to add no-alias metadata based on the
  /// memchecks.  The actually versioning is performed manually.
  LoopVersioning *LVer = nullptr;

  /// Map SCEVs to their expanded values. Populated when executing
  /// VPExpandSCEVRecipes.
  DenseMap<const SCEV *, Value *> ExpandedSCEVs;

  /// VPlan-based type analysis.
  VPTypeAnalysis TypeAnalysis;
};

/// VPBlockBase is the building block of the Hierarchical Control-Flow Graph.
/// A VPBlockBase can be either a VPBasicBlock or a VPRegionBlock.
class VPBlockBase {
  friend class VPBlockUtils;

  const unsigned char SubclassID; ///< Subclass identifier (for isa/dyn_cast).

  /// An optional name for the block.
  std::string Name;

  /// The immediate VPRegionBlock which this VPBlockBase belongs to, or null if
  /// it is a topmost VPBlockBase.
  VPRegionBlock *Parent = nullptr;

  /// List of predecessor blocks.
  SmallVector<VPBlockBase *, 1> Predecessors;

  /// List of successor blocks.
  SmallVector<VPBlockBase *, 1> Successors;

  /// VPlan containing the block. Can only be set on the entry block of the
  /// plan.
  VPlan *Plan = nullptr;

  /// Add \p Successor as the last successor to this block.
  void appendSuccessor(VPBlockBase *Successor) {
    assert(Successor && "Cannot add nullptr successor!");
    Successors.push_back(Successor);
  }

  /// Add \p Predecessor as the last predecessor to this block.
  void appendPredecessor(VPBlockBase *Predecessor) {
    assert(Predecessor && "Cannot add nullptr predecessor!");
    Predecessors.push_back(Predecessor);
  }

  /// Remove \p Predecessor from the predecessors of this block.
  void removePredecessor(VPBlockBase *Predecessor) {
    auto Pos = find(Predecessors, Predecessor);
    assert(Pos && "Predecessor does not exist");
    Predecessors.erase(Pos);
  }

  /// Remove \p Successor from the successors of this block.
  void removeSuccessor(VPBlockBase *Successor) {
    auto Pos = find(Successors, Successor);
    assert(Pos && "Successor does not exist");
    Successors.erase(Pos);
  }

protected:
  VPBlockBase(const unsigned char SC, const std::string &N)
      : SubclassID(SC), Name(N) {}

public:
  /// An enumeration for keeping track of the concrete subclass of VPBlockBase
  /// that are actually instantiated. Values of this enumeration are kept in the
  /// SubclassID field of the VPBlockBase objects. They are used for concrete
  /// type identification.
  using VPBlockTy = enum { VPRegionBlockSC, VPBasicBlockSC, VPIRBasicBlockSC };

  using VPBlocksTy = SmallVectorImpl<VPBlockBase *>;

  virtual ~VPBlockBase() = default;

  const std::string &getName() const { return Name; }

  void setName(const Twine &newName) { Name = newName.str(); }

  /// \return an ID for the concrete type of this object.
  /// This is used to implement the classof checks. This should not be used
  /// for any other purpose, as the values may change as LLVM evolves.
  unsigned getVPBlockID() const { return SubclassID; }

  VPRegionBlock *getParent() { return Parent; }
  const VPRegionBlock *getParent() const { return Parent; }

  /// \return A pointer to the plan containing the current block.
  VPlan *getPlan();
  const VPlan *getPlan() const;

  /// Sets the pointer of the plan containing the block. The block must be the
  /// entry block into the VPlan.
  void setPlan(VPlan *ParentPlan);

  void setParent(VPRegionBlock *P) { Parent = P; }

  /// \return the VPBasicBlock that is the entry of this VPBlockBase,
  /// recursively, if the latter is a VPRegionBlock. Otherwise, if this
  /// VPBlockBase is a VPBasicBlock, it is returned.
  const VPBasicBlock *getEntryBasicBlock() const;
  VPBasicBlock *getEntryBasicBlock();

  /// \return the VPBasicBlock that is the exiting this VPBlockBase,
  /// recursively, if the latter is a VPRegionBlock. Otherwise, if this
  /// VPBlockBase is a VPBasicBlock, it is returned.
  const VPBasicBlock *getExitingBasicBlock() const;
  VPBasicBlock *getExitingBasicBlock();

  const VPBlocksTy &getSuccessors() const { return Successors; }
  VPBlocksTy &getSuccessors() { return Successors; }

  iterator_range<VPBlockBase **> successors() { return Successors; }

  const VPBlocksTy &getPredecessors() const { return Predecessors; }
  VPBlocksTy &getPredecessors() { return Predecessors; }

  /// \return the successor of this VPBlockBase if it has a single successor.
  /// Otherwise return a null pointer.
  VPBlockBase *getSingleSuccessor() const {
    return (Successors.size() == 1 ? *Successors.begin() : nullptr);
  }

  /// \return the predecessor of this VPBlockBase if it has a single
  /// predecessor. Otherwise return a null pointer.
  VPBlockBase *getSinglePredecessor() const {
    return (Predecessors.size() == 1 ? *Predecessors.begin() : nullptr);
  }

  size_t getNumSuccessors() const { return Successors.size(); }
  size_t getNumPredecessors() const { return Predecessors.size(); }

  /// An Enclosing Block of a block B is any block containing B, including B
  /// itself. \return the closest enclosing block starting from "this", which
  /// has successors. \return the root enclosing block if all enclosing blocks
  /// have no successors.
  VPBlockBase *getEnclosingBlockWithSuccessors();

  /// \return the closest enclosing block starting from "this", which has
  /// predecessors. \return the root enclosing block if all enclosing blocks
  /// have no predecessors.
  VPBlockBase *getEnclosingBlockWithPredecessors();

  /// \return the successors either attached directly to this VPBlockBase or, if
  /// this VPBlockBase is the exit block of a VPRegionBlock and has no
  /// successors of its own, search recursively for the first enclosing
  /// VPRegionBlock that has successors and return them. If no such
  /// VPRegionBlock exists, return the (empty) successors of the topmost
  /// VPBlockBase reached.
  const VPBlocksTy &getHierarchicalSuccessors() {
    return getEnclosingBlockWithSuccessors()->getSuccessors();
  }

  /// \return the hierarchical successor of this VPBlockBase if it has a single
  /// hierarchical successor. Otherwise return a null pointer.
  VPBlockBase *getSingleHierarchicalSuccessor() {
    return getEnclosingBlockWithSuccessors()->getSingleSuccessor();
  }

  /// \return the predecessors either attached directly to this VPBlockBase or,
  /// if this VPBlockBase is the entry block of a VPRegionBlock and has no
  /// predecessors of its own, search recursively for the first enclosing
  /// VPRegionBlock that has predecessors and return them. If no such
  /// VPRegionBlock exists, return the (empty) predecessors of the topmost
  /// VPBlockBase reached.
  const VPBlocksTy &getHierarchicalPredecessors() {
    return getEnclosingBlockWithPredecessors()->getPredecessors();
  }

  /// \return the hierarchical predecessor of this VPBlockBase if it has a
  /// single hierarchical predecessor. Otherwise return a null pointer.
  VPBlockBase *getSingleHierarchicalPredecessor() {
    return getEnclosingBlockWithPredecessors()->getSinglePredecessor();
  }

  /// Set a given VPBlockBase \p Successor as the single successor of this
  /// VPBlockBase. This VPBlockBase is not added as predecessor of \p Successor.
  /// This VPBlockBase must have no successors.
  void setOneSuccessor(VPBlockBase *Successor) {
    assert(Successors.empty() && "Setting one successor when others exist.");
    assert(Successor->getParent() == getParent() &&
           "connected blocks must have the same parent");
    appendSuccessor(Successor);
  }

  /// Set two given VPBlockBases \p IfTrue and \p IfFalse to be the two
  /// successors of this VPBlockBase. This VPBlockBase is not added as
  /// predecessor of \p IfTrue or \p IfFalse. This VPBlockBase must have no
  /// successors.
  void setTwoSuccessors(VPBlockBase *IfTrue, VPBlockBase *IfFalse) {
    assert(Successors.empty() && "Setting two successors when others exist.");
    appendSuccessor(IfTrue);
    appendSuccessor(IfFalse);
  }

  /// Set each VPBasicBlock in \p NewPreds as predecessor of this VPBlockBase.
  /// This VPBlockBase must have no predecessors. This VPBlockBase is not added
  /// as successor of any VPBasicBlock in \p NewPreds.
  void setPredecessors(ArrayRef<VPBlockBase *> NewPreds) {
    assert(Predecessors.empty() && "Block predecessors already set.");
    for (auto *Pred : NewPreds)
      appendPredecessor(Pred);
  }

  /// Set each VPBasicBlock in \p NewSuccss as successor of this VPBlockBase.
  /// This VPBlockBase must have no successors. This VPBlockBase is not added
  /// as predecessor of any VPBasicBlock in \p NewSuccs.
  void setSuccessors(ArrayRef<VPBlockBase *> NewSuccs) {
    assert(Successors.empty() && "Block successors already set.");
    for (auto *Succ : NewSuccs)
      appendSuccessor(Succ);
  }

  /// Remove all the predecessor of this block.
  void clearPredecessors() { Predecessors.clear(); }

  /// Remove all the successors of this block.
  void clearSuccessors() { Successors.clear(); }

  /// The method which generates the output IR that correspond to this
  /// VPBlockBase, thereby "executing" the VPlan.
  virtual void execute(VPTransformState *State) = 0;

  /// Return the cost of the block.
  virtual InstructionCost cost(ElementCount VF, VPCostContext &Ctx) = 0;

  /// Delete all blocks reachable from a given VPBlockBase, inclusive.
  static void deleteCFG(VPBlockBase *Entry);

  /// Return true if it is legal to hoist instructions into this block.
  bool isLegalToHoistInto() {
    // There are currently no constraints that prevent an instruction to be
    // hoisted into a VPBlockBase.
    return true;
  }

  /// Replace all operands of VPUsers in the block with \p NewValue and also
  /// replaces all uses of VPValues defined in the block with NewValue.
  virtual void dropAllReferences(VPValue *NewValue) = 0;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void printAsOperand(raw_ostream &OS, bool PrintType) const {
    OS << getName();
  }

  /// Print plain-text dump of this VPBlockBase to \p O, prefixing all lines
  /// with \p Indent. \p SlotTracker is used to print unnamed VPValue's using
  /// consequtive numbers.
  ///
  /// Note that the numbering is applied to the whole VPlan, so printing
  /// individual blocks is consistent with the whole VPlan printing.
  virtual void print(raw_ostream &O, const Twine &Indent,
                     VPSlotTracker &SlotTracker) const = 0;

  /// Print plain-text dump of this VPlan to \p O.
  void print(raw_ostream &O) const {
    VPSlotTracker SlotTracker(getPlan());
    print(O, "", SlotTracker);
  }

  /// Print the successors of this block to \p O, prefixing all lines with \p
  /// Indent.
  void printSuccessors(raw_ostream &O, const Twine &Indent) const;

  /// Dump this VPBlockBase to dbgs().
  LLVM_DUMP_METHOD void dump() const { print(dbgs()); }
#endif

  /// Clone the current block and it's recipes without updating the operands of
  /// the cloned recipes, including all blocks in the single-entry single-exit
  /// region for VPRegionBlocks.
  virtual VPBlockBase *clone() = 0;
};

/// A value that is used outside the VPlan. The operand of the user needs to be
/// added to the associated phi node. The incoming block from VPlan is
/// determined by where the VPValue is defined: if it is defined by a recipe
/// outside a region, its parent block is used, otherwise the middle block is
/// used.
class VPLiveOut : public VPUser {
  PHINode *Phi;

public:
  VPLiveOut(PHINode *Phi, VPValue *Op)
      : VPUser({Op}, VPUser::VPUserID::LiveOut), Phi(Phi) {}

  static inline bool classof(const VPUser *U) {
    return U->getVPUserID() == VPUser::VPUserID::LiveOut;
  }

  /// Fix the wrapped phi node. This means adding an incoming value to exit
  /// block phi's from the vector loop via middle block (values from scalar loop
  /// already reach these phi's), and updating the value to scalar header phi's
  /// from the scalar preheader.
  void fixPhi(VPlan &Plan, VPTransformState &State);

  /// Returns true if the VPLiveOut uses scalars of operand \p Op.
  bool usesScalars(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return true;
  }

  PHINode *getPhi() const { return Phi; }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the VPLiveOut to \p O.
  void print(raw_ostream &O, VPSlotTracker &SlotTracker) const;
#endif
};

/// Struct to hold various analysis needed for cost computations.
struct VPCostContext {
  const TargetTransformInfo &TTI;
  VPTypeAnalysis Types;
  LLVMContext &LLVMCtx;
  LoopVectorizationCostModel &CM;
  SmallPtrSet<Instruction *, 8> SkipCostComputation;

  VPCostContext(const TargetTransformInfo &TTI, Type *CanIVTy,
                LLVMContext &LLVMCtx, LoopVectorizationCostModel &CM)
      : TTI(TTI), Types(CanIVTy, LLVMCtx), LLVMCtx(LLVMCtx), CM(CM) {}

  /// Return the cost for \p UI with \p VF using the legacy cost model as
  /// fallback until computing the cost of all recipes migrates to VPlan.
  InstructionCost getLegacyCost(Instruction *UI, ElementCount VF) const;

  /// Return true if the cost for \p UI shouldn't be computed, e.g. because it
  /// has already been pre-computed.
  bool skipCostComputation(Instruction *UI, bool IsVector) const;
};

/// VPRecipeBase is a base class modeling a sequence of one or more output IR
/// instructions. VPRecipeBase owns the VPValues it defines through VPDef
/// and is responsible for deleting its defined values. Single-value
/// recipes must inherit from VPSingleDef instead of inheriting from both
/// VPRecipeBase and VPValue separately.
class VPRecipeBase : public ilist_node_with_parent<VPRecipeBase, VPBasicBlock>,
                     public VPDef,
                     public VPUser {
  friend VPBasicBlock;
  friend class VPBlockUtils;

  /// Each VPRecipe belongs to a single VPBasicBlock.
  VPBasicBlock *Parent = nullptr;

  /// The debug location for the recipe.
  DebugLoc DL;

public:
  VPRecipeBase(const unsigned char SC, ArrayRef<VPValue *> Operands,
               DebugLoc DL = {})
      : VPDef(SC), VPUser(Operands, VPUser::VPUserID::Recipe), DL(DL) {}

  template <typename IterT>
  VPRecipeBase(const unsigned char SC, iterator_range<IterT> Operands,
               DebugLoc DL = {})
      : VPDef(SC), VPUser(Operands, VPUser::VPUserID::Recipe), DL(DL) {}
  virtual ~VPRecipeBase() = default;

  /// Clone the current recipe.
  virtual VPRecipeBase *clone() = 0;

  /// \return the VPBasicBlock which this VPRecipe belongs to.
  VPBasicBlock *getParent() { return Parent; }
  const VPBasicBlock *getParent() const { return Parent; }

  /// The method which generates the output IR instructions that correspond to
  /// this VPRecipe, thereby "executing" the VPlan.
  virtual void execute(VPTransformState &State) = 0;

  /// Return the cost of this recipe, taking into account if the cost
  /// computation should be skipped and the ForceTargetInstructionCost flag.
  /// Also takes care of printing the cost for debugging.
  virtual InstructionCost cost(ElementCount VF, VPCostContext &Ctx);

  /// Insert an unlinked recipe into a basic block immediately before
  /// the specified recipe.
  void insertBefore(VPRecipeBase *InsertPos);
  /// Insert an unlinked recipe into \p BB immediately before the insertion
  /// point \p IP;
  void insertBefore(VPBasicBlock &BB, iplist<VPRecipeBase>::iterator IP);

  /// Insert an unlinked Recipe into a basic block immediately after
  /// the specified Recipe.
  void insertAfter(VPRecipeBase *InsertPos);

  /// Unlink this recipe from its current VPBasicBlock and insert it into
  /// the VPBasicBlock that MovePos lives in, right after MovePos.
  void moveAfter(VPRecipeBase *MovePos);

  /// Unlink this recipe and insert into BB before I.
  ///
  /// \pre I is a valid iterator into BB.
  void moveBefore(VPBasicBlock &BB, iplist<VPRecipeBase>::iterator I);

  /// This method unlinks 'this' from the containing basic block, but does not
  /// delete it.
  void removeFromParent();

  /// This method unlinks 'this' from the containing basic block and deletes it.
  ///
  /// \returns an iterator pointing to the element after the erased one
  iplist<VPRecipeBase>::iterator eraseFromParent();

  /// Method to support type inquiry through isa, cast, and dyn_cast.
  static inline bool classof(const VPDef *D) {
    // All VPDefs are also VPRecipeBases.
    return true;
  }

  static inline bool classof(const VPUser *U) {
    return U->getVPUserID() == VPUser::VPUserID::Recipe;
  }

  /// Returns true if the recipe may have side-effects.
  bool mayHaveSideEffects() const;

  /// Returns true for PHI-like recipes.
  bool isPhi() const {
    return getVPDefID() >= VPFirstPHISC && getVPDefID() <= VPLastPHISC;
  }

  /// Returns true if the recipe may read from memory.
  bool mayReadFromMemory() const;

  /// Returns true if the recipe may write to memory.
  bool mayWriteToMemory() const;

  /// Returns true if the recipe may read from or write to memory.
  bool mayReadOrWriteMemory() const {
    return mayReadFromMemory() || mayWriteToMemory();
  }

  /// Returns the debug location of the recipe.
  DebugLoc getDebugLoc() const { return DL; }

protected:
  /// Compute the cost of this recipe using the legacy cost model and the
  /// underlying instructions.
  InstructionCost computeCost(ElementCount VF, VPCostContext &Ctx) const;
};

// Helper macro to define common classof implementations for recipes.
#define VP_CLASSOF_IMPL(VPDefID)                                               \
  static inline bool classof(const VPDef *D) {                                 \
    return D->getVPDefID() == VPDefID;                                         \
  }                                                                            \
  static inline bool classof(const VPValue *V) {                               \
    auto *R = V->getDefiningRecipe();                                          \
    return R && R->getVPDefID() == VPDefID;                                    \
  }                                                                            \
  static inline bool classof(const VPUser *U) {                                \
    auto *R = dyn_cast<VPRecipeBase>(U);                                       \
    return R && R->getVPDefID() == VPDefID;                                    \
  }                                                                            \
  static inline bool classof(const VPRecipeBase *R) {                          \
    return R->getVPDefID() == VPDefID;                                         \
  }                                                                            \
  static inline bool classof(const VPSingleDefRecipe *R) {                     \
    return R->getVPDefID() == VPDefID;                                         \
  }

/// VPSingleDef is a base class for recipes for modeling a sequence of one or
/// more output IR that define a single result VPValue.
/// Note that VPRecipeBase must be inherited from before VPValue.
class VPSingleDefRecipe : public VPRecipeBase, public VPValue {
public:
  template <typename IterT>
  VPSingleDefRecipe(const unsigned char SC, IterT Operands, DebugLoc DL = {})
      : VPRecipeBase(SC, Operands, DL), VPValue(this) {}

  VPSingleDefRecipe(const unsigned char SC, ArrayRef<VPValue *> Operands,
                    DebugLoc DL = {})
      : VPRecipeBase(SC, Operands, DL), VPValue(this) {}

  template <typename IterT>
  VPSingleDefRecipe(const unsigned char SC, IterT Operands, Value *UV,
                    DebugLoc DL = {})
      : VPRecipeBase(SC, Operands, DL), VPValue(this, UV) {}

  static inline bool classof(const VPRecipeBase *R) {
    switch (R->getVPDefID()) {
    case VPRecipeBase::VPDerivedIVSC:
    case VPRecipeBase::VPEVLBasedIVPHISC:
    case VPRecipeBase::VPExpandSCEVSC:
    case VPRecipeBase::VPInstructionSC:
    case VPRecipeBase::VPReductionEVLSC:
    case VPRecipeBase::VPReductionSC:
    case VPRecipeBase::VPReplicateSC:
    case VPRecipeBase::VPScalarIVStepsSC:
    case VPRecipeBase::VPVectorPointerSC:
    case VPRecipeBase::VPWidenCallSC:
    case VPRecipeBase::VPWidenCanonicalIVSC:
    case VPRecipeBase::VPWidenCastSC:
    case VPRecipeBase::VPWidenGEPSC:
    case VPRecipeBase::VPWidenSC:
    case VPRecipeBase::VPWidenSelectSC:
    case VPRecipeBase::VPBlendSC:
    case VPRecipeBase::VPPredInstPHISC:
    case VPRecipeBase::VPCanonicalIVPHISC:
    case VPRecipeBase::VPActiveLaneMaskPHISC:
    case VPRecipeBase::VPFirstOrderRecurrencePHISC:
    case VPRecipeBase::VPWidenPHISC:
    case VPRecipeBase::VPWidenIntOrFpInductionSC:
    case VPRecipeBase::VPWidenPointerInductionSC:
    case VPRecipeBase::VPReductionPHISC:
    case VPRecipeBase::VPScalarCastSC:
      return true;
    case VPRecipeBase::VPInterleaveSC:
    case VPRecipeBase::VPBranchOnMaskSC:
    case VPRecipeBase::VPWidenLoadEVLSC:
    case VPRecipeBase::VPWidenLoadSC:
    case VPRecipeBase::VPWidenStoreEVLSC:
    case VPRecipeBase::VPWidenStoreSC:
      // TODO: Widened stores don't define a value, but widened loads do. Split
      // the recipes to be able to make widened loads VPSingleDefRecipes.
      return false;
    }
    llvm_unreachable("Unhandled VPDefID");
  }

  static inline bool classof(const VPUser *U) {
    auto *R = dyn_cast<VPRecipeBase>(U);
    return R && classof(R);
  }

  virtual VPSingleDefRecipe *clone() override = 0;

  /// Returns the underlying instruction.
  Instruction *getUnderlyingInstr() {
    return cast<Instruction>(getUnderlyingValue());
  }
  const Instruction *getUnderlyingInstr() const {
    return cast<Instruction>(getUnderlyingValue());
  }
};

/// Class to record LLVM IR flag for a recipe along with it.
class VPRecipeWithIRFlags : public VPSingleDefRecipe {
  enum class OperationType : unsigned char {
    Cmp,
    OverflowingBinOp,
    DisjointOp,
    PossiblyExactOp,
    GEPOp,
    FPMathOp,
    NonNegOp,
    Other
  };

public:
  struct WrapFlagsTy {
    char HasNUW : 1;
    char HasNSW : 1;

    WrapFlagsTy(bool HasNUW, bool HasNSW) : HasNUW(HasNUW), HasNSW(HasNSW) {}
  };

  struct DisjointFlagsTy {
    char IsDisjoint : 1;
    DisjointFlagsTy(bool IsDisjoint) : IsDisjoint(IsDisjoint) {}
  };

protected:
  struct GEPFlagsTy {
    char IsInBounds : 1;
    GEPFlagsTy(bool IsInBounds) : IsInBounds(IsInBounds) {}
  };

private:
  struct ExactFlagsTy {
    char IsExact : 1;
  };
  struct NonNegFlagsTy {
    char NonNeg : 1;
  };
  struct FastMathFlagsTy {
    char AllowReassoc : 1;
    char NoNaNs : 1;
    char NoInfs : 1;
    char NoSignedZeros : 1;
    char AllowReciprocal : 1;
    char AllowContract : 1;
    char ApproxFunc : 1;

    FastMathFlagsTy(const FastMathFlags &FMF);
  };

  OperationType OpType;

  union {
    CmpInst::Predicate CmpPredicate;
    WrapFlagsTy WrapFlags;
    DisjointFlagsTy DisjointFlags;
    ExactFlagsTy ExactFlags;
    GEPFlagsTy GEPFlags;
    NonNegFlagsTy NonNegFlags;
    FastMathFlagsTy FMFs;
    unsigned AllFlags;
  };

protected:
  void transferFlags(VPRecipeWithIRFlags &Other) {
    OpType = Other.OpType;
    AllFlags = Other.AllFlags;
  }

public:
  template <typename IterT>
  VPRecipeWithIRFlags(const unsigned char SC, IterT Operands, DebugLoc DL = {})
      : VPSingleDefRecipe(SC, Operands, DL) {
    OpType = OperationType::Other;
    AllFlags = 0;
  }

  template <typename IterT>
  VPRecipeWithIRFlags(const unsigned char SC, IterT Operands, Instruction &I)
      : VPSingleDefRecipe(SC, Operands, &I, I.getDebugLoc()) {
    if (auto *Op = dyn_cast<CmpInst>(&I)) {
      OpType = OperationType::Cmp;
      CmpPredicate = Op->getPredicate();
    } else if (auto *Op = dyn_cast<PossiblyDisjointInst>(&I)) {
      OpType = OperationType::DisjointOp;
      DisjointFlags.IsDisjoint = Op->isDisjoint();
    } else if (auto *Op = dyn_cast<OverflowingBinaryOperator>(&I)) {
      OpType = OperationType::OverflowingBinOp;
      WrapFlags = {Op->hasNoUnsignedWrap(), Op->hasNoSignedWrap()};
    } else if (auto *Op = dyn_cast<PossiblyExactOperator>(&I)) {
      OpType = OperationType::PossiblyExactOp;
      ExactFlags.IsExact = Op->isExact();
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
      OpType = OperationType::GEPOp;
      GEPFlags.IsInBounds = GEP->isInBounds();
    } else if (auto *PNNI = dyn_cast<PossiblyNonNegInst>(&I)) {
      OpType = OperationType::NonNegOp;
      NonNegFlags.NonNeg = PNNI->hasNonNeg();
    } else if (auto *Op = dyn_cast<FPMathOperator>(&I)) {
      OpType = OperationType::FPMathOp;
      FMFs = Op->getFastMathFlags();
    } else {
      OpType = OperationType::Other;
      AllFlags = 0;
    }
  }

  template <typename IterT>
  VPRecipeWithIRFlags(const unsigned char SC, IterT Operands,
                      CmpInst::Predicate Pred, DebugLoc DL = {})
      : VPSingleDefRecipe(SC, Operands, DL), OpType(OperationType::Cmp),
        CmpPredicate(Pred) {}

  template <typename IterT>
  VPRecipeWithIRFlags(const unsigned char SC, IterT Operands,
                      WrapFlagsTy WrapFlags, DebugLoc DL = {})
      : VPSingleDefRecipe(SC, Operands, DL),
        OpType(OperationType::OverflowingBinOp), WrapFlags(WrapFlags) {}

  template <typename IterT>
  VPRecipeWithIRFlags(const unsigned char SC, IterT Operands,
                      FastMathFlags FMFs, DebugLoc DL = {})
      : VPSingleDefRecipe(SC, Operands, DL), OpType(OperationType::FPMathOp),
        FMFs(FMFs) {}

  template <typename IterT>
  VPRecipeWithIRFlags(const unsigned char SC, IterT Operands,
                      DisjointFlagsTy DisjointFlags, DebugLoc DL = {})
      : VPSingleDefRecipe(SC, Operands, DL), OpType(OperationType::DisjointOp),
        DisjointFlags(DisjointFlags) {}

protected:
  template <typename IterT>
  VPRecipeWithIRFlags(const unsigned char SC, IterT Operands,
                      GEPFlagsTy GEPFlags, DebugLoc DL = {})
      : VPSingleDefRecipe(SC, Operands, DL), OpType(OperationType::GEPOp),
        GEPFlags(GEPFlags) {}

public:
  static inline bool classof(const VPRecipeBase *R) {
    return R->getVPDefID() == VPRecipeBase::VPInstructionSC ||
           R->getVPDefID() == VPRecipeBase::VPWidenSC ||
           R->getVPDefID() == VPRecipeBase::VPWidenGEPSC ||
           R->getVPDefID() == VPRecipeBase::VPWidenCastSC ||
           R->getVPDefID() == VPRecipeBase::VPReplicateSC ||
           R->getVPDefID() == VPRecipeBase::VPVectorPointerSC;
  }

  static inline bool classof(const VPUser *U) {
    auto *R = dyn_cast<VPRecipeBase>(U);
    return R && classof(R);
  }

  /// Drop all poison-generating flags.
  void dropPoisonGeneratingFlags() {
    // NOTE: This needs to be kept in-sync with
    // Instruction::dropPoisonGeneratingFlags.
    switch (OpType) {
    case OperationType::OverflowingBinOp:
      WrapFlags.HasNUW = false;
      WrapFlags.HasNSW = false;
      break;
    case OperationType::DisjointOp:
      DisjointFlags.IsDisjoint = false;
      break;
    case OperationType::PossiblyExactOp:
      ExactFlags.IsExact = false;
      break;
    case OperationType::GEPOp:
      GEPFlags.IsInBounds = false;
      break;
    case OperationType::FPMathOp:
      FMFs.NoNaNs = false;
      FMFs.NoInfs = false;
      break;
    case OperationType::NonNegOp:
      NonNegFlags.NonNeg = false;
      break;
    case OperationType::Cmp:
    case OperationType::Other:
      break;
    }
  }

  /// Set the IR flags for \p I.
  void setFlags(Instruction *I) const {
    switch (OpType) {
    case OperationType::OverflowingBinOp:
      I->setHasNoUnsignedWrap(WrapFlags.HasNUW);
      I->setHasNoSignedWrap(WrapFlags.HasNSW);
      break;
    case OperationType::DisjointOp:
      cast<PossiblyDisjointInst>(I)->setIsDisjoint(DisjointFlags.IsDisjoint);
      break;
    case OperationType::PossiblyExactOp:
      I->setIsExact(ExactFlags.IsExact);
      break;
    case OperationType::GEPOp:
      // TODO(gep_nowrap): Track the full GEPNoWrapFlags in VPlan.
      cast<GetElementPtrInst>(I)->setNoWrapFlags(
          GEPFlags.IsInBounds ? GEPNoWrapFlags::inBounds()
                              : GEPNoWrapFlags::none());
      break;
    case OperationType::FPMathOp:
      I->setHasAllowReassoc(FMFs.AllowReassoc);
      I->setHasNoNaNs(FMFs.NoNaNs);
      I->setHasNoInfs(FMFs.NoInfs);
      I->setHasNoSignedZeros(FMFs.NoSignedZeros);
      I->setHasAllowReciprocal(FMFs.AllowReciprocal);
      I->setHasAllowContract(FMFs.AllowContract);
      I->setHasApproxFunc(FMFs.ApproxFunc);
      break;
    case OperationType::NonNegOp:
      I->setNonNeg(NonNegFlags.NonNeg);
      break;
    case OperationType::Cmp:
    case OperationType::Other:
      break;
    }
  }

  CmpInst::Predicate getPredicate() const {
    assert(OpType == OperationType::Cmp &&
           "recipe doesn't have a compare predicate");
    return CmpPredicate;
  }

  bool isInBounds() const {
    assert(OpType == OperationType::GEPOp &&
           "recipe doesn't have inbounds flag");
    return GEPFlags.IsInBounds;
  }

  /// Returns true if the recipe has fast-math flags.
  bool hasFastMathFlags() const { return OpType == OperationType::FPMathOp; }

  FastMathFlags getFastMathFlags() const;

  bool hasNoUnsignedWrap() const {
    assert(OpType == OperationType::OverflowingBinOp &&
           "recipe doesn't have a NUW flag");
    return WrapFlags.HasNUW;
  }

  bool hasNoSignedWrap() const {
    assert(OpType == OperationType::OverflowingBinOp &&
           "recipe doesn't have a NSW flag");
    return WrapFlags.HasNSW;
  }

  bool isDisjoint() const {
    assert(OpType == OperationType::DisjointOp &&
           "recipe cannot have a disjoing flag");
    return DisjointFlags.IsDisjoint;
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void printFlags(raw_ostream &O) const;
#endif
};

/// This is a concrete Recipe that models a single VPlan-level instruction.
/// While as any Recipe it may generate a sequence of IR instructions when
/// executed, these instructions would always form a single-def expression as
/// the VPInstruction is also a single def-use vertex.
class VPInstruction : public VPRecipeWithIRFlags {
  friend class VPlanSlp;

public:
  /// VPlan opcodes, extending LLVM IR with idiomatics instructions.
  enum {
    FirstOrderRecurrenceSplice =
        Instruction::OtherOpsEnd + 1, // Combines the incoming and previous
                                      // values of a first-order recurrence.
    Not,
    SLPLoad,
    SLPStore,
    ActiveLaneMask,
    ExplicitVectorLength,
    /// Creates a scalar phi in a leaf VPBB with a single predecessor in VPlan.
    /// The first operand is the incoming value from the predecessor in VPlan,
    /// the second operand is the incoming value for all other predecessors
    /// (which are currently not modeled in VPlan).
    ResumePhi,
    CalculateTripCountMinusVF,
    // Increment the canonical IV separately for each unrolled part.
    CanonicalIVIncrementForPart,
    BranchOnCount,
    BranchOnCond,
    ComputeReductionResult,
    // Takes the VPValue to extract from as first operand and the lane or part
    // to extract as second operand, counting from the end starting with 1 for
    // last. The second operand must be a positive constant and <= VF when
    // extracting from a vector or <= UF when extracting from an unrolled
    // scalar.
    ExtractFromEnd,
    LogicalAnd, // Non-poison propagating logical And.
    // Add an offset in bytes (second operand) to a base pointer (first
    // operand). Only generates scalar values (either for the first lane only or
    // for all lanes, depending on its uses).
    PtrAdd,
  };

private:
  typedef unsigned char OpcodeTy;
  OpcodeTy Opcode;

  /// An optional name that can be used for the generated IR instruction.
  const std::string Name;

  /// Returns true if this VPInstruction generates scalar values for all lanes.
  /// Most VPInstructions generate a single value per part, either vector or
  /// scalar. VPReplicateRecipe takes care of generating multiple (scalar)
  /// values per all lanes, stemming from an original ingredient. This method
  /// identifies the (rare) cases of VPInstructions that do so as well, w/o an
  /// underlying ingredient.
  bool doesGeneratePerAllLanes() const;

  /// Returns true if we can generate a scalar for the first lane only if
  /// needed.
  bool canGenerateScalarForFirstLane() const;

  /// Utility methods serving execute(): generates a single instance of the
  /// modeled instruction for a given part. \returns the generated value for \p
  /// Part. In some cases an existing value is returned rather than a generated
  /// one.
  Value *generatePerPart(VPTransformState &State, unsigned Part);

  /// Utility methods serving execute(): generates a scalar single instance of
  /// the modeled instruction for a given lane. \returns the scalar generated
  /// value for lane \p Lane.
  Value *generatePerLane(VPTransformState &State, const VPIteration &Lane);

#if !defined(NDEBUG)
  /// Return true if the VPInstruction is a floating point math operation, i.e.
  /// has fast-math flags.
  bool isFPMathOp() const;
#endif

public:
  VPInstruction(unsigned Opcode, ArrayRef<VPValue *> Operands, DebugLoc DL,
                const Twine &Name = "")
      : VPRecipeWithIRFlags(VPDef::VPInstructionSC, Operands, DL),
        Opcode(Opcode), Name(Name.str()) {}

  VPInstruction(unsigned Opcode, std::initializer_list<VPValue *> Operands,
                DebugLoc DL = {}, const Twine &Name = "")
      : VPInstruction(Opcode, ArrayRef<VPValue *>(Operands), DL, Name) {}

  VPInstruction(unsigned Opcode, CmpInst::Predicate Pred, VPValue *A,
                VPValue *B, DebugLoc DL = {}, const Twine &Name = "");

  VPInstruction(unsigned Opcode, std::initializer_list<VPValue *> Operands,
                WrapFlagsTy WrapFlags, DebugLoc DL = {}, const Twine &Name = "")
      : VPRecipeWithIRFlags(VPDef::VPInstructionSC, Operands, WrapFlags, DL),
        Opcode(Opcode), Name(Name.str()) {}

  VPInstruction(unsigned Opcode, std::initializer_list<VPValue *> Operands,
                DisjointFlagsTy DisjointFlag, DebugLoc DL = {},
                const Twine &Name = "")
      : VPRecipeWithIRFlags(VPDef::VPInstructionSC, Operands, DisjointFlag, DL),
        Opcode(Opcode), Name(Name.str()) {
    assert(Opcode == Instruction::Or && "only OR opcodes can be disjoint");
  }

  VPInstruction(unsigned Opcode, std::initializer_list<VPValue *> Operands,
                FastMathFlags FMFs, DebugLoc DL = {}, const Twine &Name = "");

  VP_CLASSOF_IMPL(VPDef::VPInstructionSC)

  VPInstruction *clone() override {
    SmallVector<VPValue *, 2> Operands(operands());
    auto *New = new VPInstruction(Opcode, Operands, getDebugLoc(), Name);
    New->transferFlags(*this);
    return New;
  }

  unsigned getOpcode() const { return Opcode; }

  /// Generate the instruction.
  /// TODO: We currently execute only per-part unless a specific instance is
  /// provided.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the VPInstruction to \p O.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;

  /// Print the VPInstruction to dbgs() (for debugging).
  LLVM_DUMP_METHOD void dump() const;
#endif

  /// Return true if this instruction may modify memory.
  bool mayWriteToMemory() const {
    // TODO: we can use attributes of the called function to rule out memory
    //       modifications.
    return Opcode == Instruction::Store || Opcode == Instruction::Call ||
           Opcode == Instruction::Invoke || Opcode == SLPStore;
  }

  bool hasResult() const {
    // CallInst may or may not have a result, depending on the called function.
    // Conservatively return calls have results for now.
    switch (getOpcode()) {
    case Instruction::Ret:
    case Instruction::Br:
    case Instruction::Store:
    case Instruction::Switch:
    case Instruction::IndirectBr:
    case Instruction::Resume:
    case Instruction::CatchRet:
    case Instruction::Unreachable:
    case Instruction::Fence:
    case Instruction::AtomicRMW:
    case VPInstruction::BranchOnCond:
    case VPInstruction::BranchOnCount:
      return false;
    default:
      return true;
    }
  }

  /// Returns true if the recipe only uses the first lane of operand \p Op.
  bool onlyFirstLaneUsed(const VPValue *Op) const override;

  /// Returns true if the recipe only uses the first part of operand \p Op.
  bool onlyFirstPartUsed(const VPValue *Op) const override;

  /// Returns true if this VPInstruction produces a scalar value from a vector,
  /// e.g. by performing a reduction or extracting a lane.
  bool isVectorToScalar() const;

  /// Returns true if this VPInstruction's operands are single scalars and the
  /// result is also a single scalar.
  bool isSingleScalar() const;
};

/// VPWidenRecipe is a recipe for producing a widened instruction using the
/// opcode and operands of the recipe. This recipe covers most of the
/// traditional vectorization cases where each recipe transforms into a
/// vectorized version of itself.
class VPWidenRecipe : public VPRecipeWithIRFlags {
  unsigned Opcode;

public:
  template <typename IterT>
  VPWidenRecipe(Instruction &I, iterator_range<IterT> Operands)
      : VPRecipeWithIRFlags(VPDef::VPWidenSC, Operands, I),
        Opcode(I.getOpcode()) {}

  ~VPWidenRecipe() override = default;

  VPWidenRecipe *clone() override {
    auto *R = new VPWidenRecipe(*getUnderlyingInstr(), operands());
    R->transferFlags(*this);
    return R;
  }

  VP_CLASSOF_IMPL(VPDef::VPWidenSC)

  /// Produce a widened instruction using the opcode and operands of the recipe,
  /// processing State.VF elements.
  void execute(VPTransformState &State) override;

  unsigned getOpcode() const { return Opcode; }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif
};

/// VPWidenCastRecipe is a recipe to create vector cast instructions.
class VPWidenCastRecipe : public VPRecipeWithIRFlags {
  /// Cast instruction opcode.
  Instruction::CastOps Opcode;

  /// Result type for the cast.
  Type *ResultTy;

public:
  VPWidenCastRecipe(Instruction::CastOps Opcode, VPValue *Op, Type *ResultTy,
                    CastInst &UI)
      : VPRecipeWithIRFlags(VPDef::VPWidenCastSC, Op, UI), Opcode(Opcode),
        ResultTy(ResultTy) {
    assert(UI.getOpcode() == Opcode &&
           "opcode of underlying cast doesn't match");
  }

  VPWidenCastRecipe(Instruction::CastOps Opcode, VPValue *Op, Type *ResultTy)
      : VPRecipeWithIRFlags(VPDef::VPWidenCastSC, Op), Opcode(Opcode),
        ResultTy(ResultTy) {}

  ~VPWidenCastRecipe() override = default;

  VPWidenCastRecipe *clone() override {
    if (auto *UV = getUnderlyingValue())
      return new VPWidenCastRecipe(Opcode, getOperand(0), ResultTy,
                                   *cast<CastInst>(UV));

    return new VPWidenCastRecipe(Opcode, getOperand(0), ResultTy);
  }

  VP_CLASSOF_IMPL(VPDef::VPWidenCastSC)

  /// Produce widened copies of the cast.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  Instruction::CastOps getOpcode() const { return Opcode; }

  /// Returns the result type of the cast.
  Type *getResultType() const { return ResultTy; }
};

/// VPScalarCastRecipe is a recipe to create scalar cast instructions.
class VPScalarCastRecipe : public VPSingleDefRecipe {
  Instruction::CastOps Opcode;

  Type *ResultTy;

  Value *generate(VPTransformState &State, unsigned Part);

public:
  VPScalarCastRecipe(Instruction::CastOps Opcode, VPValue *Op, Type *ResultTy)
      : VPSingleDefRecipe(VPDef::VPScalarCastSC, {Op}), Opcode(Opcode),
        ResultTy(ResultTy) {}

  ~VPScalarCastRecipe() override = default;

  VPScalarCastRecipe *clone() override {
    return new VPScalarCastRecipe(Opcode, getOperand(0), ResultTy);
  }

  VP_CLASSOF_IMPL(VPDef::VPScalarCastSC)

  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  /// Returns the result type of the cast.
  Type *getResultType() const { return ResultTy; }

  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    // At the moment, only uniform codegen is implemented.
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return true;
  }
};

/// A recipe for widening Call instructions.
class VPWidenCallRecipe : public VPSingleDefRecipe {
  /// ID of the vector intrinsic to call when widening the call. If set the
  /// Intrinsic::not_intrinsic, a library call will be used instead.
  Intrinsic::ID VectorIntrinsicID;
  /// If this recipe represents a library call, Variant stores a pointer to
  /// the chosen function. There is a 1:1 mapping between a given VF and the
  /// chosen vectorized variant, so there will be a different vplan for each
  /// VF with a valid variant.
  Function *Variant;

public:
  template <typename IterT>
  VPWidenCallRecipe(Value *UV, iterator_range<IterT> CallArguments,
                    Intrinsic::ID VectorIntrinsicID, DebugLoc DL = {},
                    Function *Variant = nullptr)
      : VPSingleDefRecipe(VPDef::VPWidenCallSC, CallArguments, UV, DL),
        VectorIntrinsicID(VectorIntrinsicID), Variant(Variant) {
    assert(
        isa<Function>(getOperand(getNumOperands() - 1)->getLiveInIRValue()) &&
        "last operand must be the called function");
  }

  ~VPWidenCallRecipe() override = default;

  VPWidenCallRecipe *clone() override {
    return new VPWidenCallRecipe(getUnderlyingValue(), operands(),
                                 VectorIntrinsicID, getDebugLoc(), Variant);
  }

  VP_CLASSOF_IMPL(VPDef::VPWidenCallSC)

  /// Produce a widened version of the call instruction.
  void execute(VPTransformState &State) override;

  Function *getCalledScalarFunction() const {
    return cast<Function>(getOperand(getNumOperands() - 1)->getLiveInIRValue());
  }

  operand_range arg_operands() {
    return make_range(op_begin(), op_begin() + getNumOperands() - 1);
  }
  const_operand_range arg_operands() const {
    return make_range(op_begin(), op_begin() + getNumOperands() - 1);
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif
};

/// A recipe for widening select instructions.
struct VPWidenSelectRecipe : public VPSingleDefRecipe {
  template <typename IterT>
  VPWidenSelectRecipe(SelectInst &I, iterator_range<IterT> Operands)
      : VPSingleDefRecipe(VPDef::VPWidenSelectSC, Operands, &I,
                          I.getDebugLoc()) {}

  ~VPWidenSelectRecipe() override = default;

  VPWidenSelectRecipe *clone() override {
    return new VPWidenSelectRecipe(*cast<SelectInst>(getUnderlyingInstr()),
                                   operands());
  }

  VP_CLASSOF_IMPL(VPDef::VPWidenSelectSC)

  /// Produce a widened version of the select instruction.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  VPValue *getCond() const {
    return getOperand(0);
  }

  bool isInvariantCond() const {
    return getCond()->isDefinedOutsideVectorRegions();
  }
};

/// A recipe for handling GEP instructions.
class VPWidenGEPRecipe : public VPRecipeWithIRFlags {
  bool isPointerLoopInvariant() const {
    return getOperand(0)->isDefinedOutsideVectorRegions();
  }

  bool isIndexLoopInvariant(unsigned I) const {
    return getOperand(I + 1)->isDefinedOutsideVectorRegions();
  }

  bool areAllOperandsInvariant() const {
    return all_of(operands(), [](VPValue *Op) {
      return Op->isDefinedOutsideVectorRegions();
    });
  }

public:
  template <typename IterT>
  VPWidenGEPRecipe(GetElementPtrInst *GEP, iterator_range<IterT> Operands)
      : VPRecipeWithIRFlags(VPDef::VPWidenGEPSC, Operands, *GEP) {}

  ~VPWidenGEPRecipe() override = default;

  VPWidenGEPRecipe *clone() override {
    return new VPWidenGEPRecipe(cast<GetElementPtrInst>(getUnderlyingInstr()),
                                operands());
  }

  VP_CLASSOF_IMPL(VPDef::VPWidenGEPSC)

  /// Generate the gep nodes.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif
};

/// A recipe to compute the pointers for widened memory accesses of IndexTy for
/// all parts. If IsReverse is true, compute pointers for accessing the input in
/// reverse order per part.
class VPVectorPointerRecipe : public VPRecipeWithIRFlags {
  Type *IndexedTy;
  bool IsReverse;

public:
  VPVectorPointerRecipe(VPValue *Ptr, Type *IndexedTy, bool IsReverse,
                        bool IsInBounds, DebugLoc DL)
      : VPRecipeWithIRFlags(VPDef::VPVectorPointerSC, ArrayRef<VPValue *>(Ptr),
                            GEPFlagsTy(IsInBounds), DL),
        IndexedTy(IndexedTy), IsReverse(IsReverse) {}

  VP_CLASSOF_IMPL(VPDef::VPVectorPointerSC)

  void execute(VPTransformState &State) override;

  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return true;
  }

  VPVectorPointerRecipe *clone() override {
    return new VPVectorPointerRecipe(getOperand(0), IndexedTy, IsReverse,
                                     isInBounds(), getDebugLoc());
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif
};

/// A pure virtual base class for all recipes modeling header phis, including
/// phis for first order recurrences, pointer inductions and reductions. The
/// start value is the first operand of the recipe and the incoming value from
/// the backedge is the second operand.
///
/// Inductions are modeled using the following sub-classes:
///  * VPCanonicalIVPHIRecipe: Canonical scalar induction of the vector loop,
///    starting at a specified value (zero for the main vector loop, the resume
///    value for the epilogue vector loop) and stepping by 1. The induction
///    controls exiting of the vector loop by comparing against the vector trip
///    count. Produces a single scalar PHI for the induction value per
///    iteration.
///  * VPWidenIntOrFpInductionRecipe: Generates vector values for integer and
///    floating point inductions with arbitrary start and step values. Produces
///    a vector PHI per-part.
///  * VPDerivedIVRecipe: Converts the canonical IV value to the corresponding
///    value of an IV with different start and step values. Produces a single
///    scalar value per iteration
///  * VPScalarIVStepsRecipe: Generates scalar values per-lane based on a
///    canonical or derived induction.
///  * VPWidenPointerInductionRecipe: Generate vector and scalar values for a
///    pointer induction. Produces either a vector PHI per-part or scalar values
///    per-lane based on the canonical induction.
class VPHeaderPHIRecipe : public VPSingleDefRecipe {
protected:
  VPHeaderPHIRecipe(unsigned char VPDefID, Instruction *UnderlyingInstr,
                    VPValue *Start = nullptr, DebugLoc DL = {})
      : VPSingleDefRecipe(VPDefID, ArrayRef<VPValue *>(), UnderlyingInstr, DL) {
    if (Start)
      addOperand(Start);
  }

public:
  ~VPHeaderPHIRecipe() override = default;

  /// Method to support type inquiry through isa, cast, and dyn_cast.
  static inline bool classof(const VPRecipeBase *B) {
    return B->getVPDefID() >= VPDef::VPFirstHeaderPHISC &&
           B->getVPDefID() <= VPDef::VPLastHeaderPHISC;
  }
  static inline bool classof(const VPValue *V) {
    auto *B = V->getDefiningRecipe();
    return B && B->getVPDefID() >= VPRecipeBase::VPFirstHeaderPHISC &&
           B->getVPDefID() <= VPRecipeBase::VPLastHeaderPHISC;
  }

  /// Generate the phi nodes.
  void execute(VPTransformState &State) override = 0;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override = 0;
#endif

  /// Returns the start value of the phi, if one is set.
  VPValue *getStartValue() {
    return getNumOperands() == 0 ? nullptr : getOperand(0);
  }
  VPValue *getStartValue() const {
    return getNumOperands() == 0 ? nullptr : getOperand(0);
  }

  /// Update the start value of the recipe.
  void setStartValue(VPValue *V) { setOperand(0, V); }

  /// Returns the incoming value from the loop backedge.
  virtual VPValue *getBackedgeValue() {
    return getOperand(1);
  }

  /// Returns the backedge value as a recipe. The backedge value is guaranteed
  /// to be a recipe.
  virtual VPRecipeBase &getBackedgeRecipe() {
    return *getBackedgeValue()->getDefiningRecipe();
  }
};

/// A recipe for handling phi nodes of integer and floating-point inductions,
/// producing their vector values.
class VPWidenIntOrFpInductionRecipe : public VPHeaderPHIRecipe {
  PHINode *IV;
  TruncInst *Trunc;
  const InductionDescriptor &IndDesc;

public:
  VPWidenIntOrFpInductionRecipe(PHINode *IV, VPValue *Start, VPValue *Step,
                                const InductionDescriptor &IndDesc)
      : VPHeaderPHIRecipe(VPDef::VPWidenIntOrFpInductionSC, IV, Start), IV(IV),
        Trunc(nullptr), IndDesc(IndDesc) {
    addOperand(Step);
  }

  VPWidenIntOrFpInductionRecipe(PHINode *IV, VPValue *Start, VPValue *Step,
                                const InductionDescriptor &IndDesc,
                                TruncInst *Trunc)
      : VPHeaderPHIRecipe(VPDef::VPWidenIntOrFpInductionSC, Trunc, Start),
        IV(IV), Trunc(Trunc), IndDesc(IndDesc) {
    addOperand(Step);
  }

  ~VPWidenIntOrFpInductionRecipe() override = default;

  VPWidenIntOrFpInductionRecipe *clone() override {
    return new VPWidenIntOrFpInductionRecipe(IV, getStartValue(),
                                             getStepValue(), IndDesc, Trunc);
  }

  VP_CLASSOF_IMPL(VPDef::VPWidenIntOrFpInductionSC)

  /// Generate the vectorized and scalarized versions of the phi node as
  /// needed by their users.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  VPValue *getBackedgeValue() override {
    // TODO: All operands of base recipe must exist and be at same index in
    // derived recipe.
    llvm_unreachable(
        "VPWidenIntOrFpInductionRecipe generates its own backedge value");
  }

  VPRecipeBase &getBackedgeRecipe() override {
    // TODO: All operands of base recipe must exist and be at same index in
    // derived recipe.
    llvm_unreachable(
        "VPWidenIntOrFpInductionRecipe generates its own backedge value");
  }

  /// Returns the step value of the induction.
  VPValue *getStepValue() { return getOperand(1); }
  const VPValue *getStepValue() const { return getOperand(1); }

  /// Returns the first defined value as TruncInst, if it is one or nullptr
  /// otherwise.
  TruncInst *getTruncInst() { return Trunc; }
  const TruncInst *getTruncInst() const { return Trunc; }

  PHINode *getPHINode() { return IV; }

  /// Returns the induction descriptor for the recipe.
  const InductionDescriptor &getInductionDescriptor() const { return IndDesc; }

  /// Returns true if the induction is canonical, i.e. starting at 0 and
  /// incremented by UF * VF (= the original IV is incremented by 1) and has the
  /// same type as the canonical induction.
  bool isCanonical() const;

  /// Returns the scalar type of the induction.
  Type *getScalarType() const {
    return Trunc ? Trunc->getType() : IV->getType();
  }
};

class VPWidenPointerInductionRecipe : public VPHeaderPHIRecipe {
  const InductionDescriptor &IndDesc;

  bool IsScalarAfterVectorization;

public:
  /// Create a new VPWidenPointerInductionRecipe for \p Phi with start value \p
  /// Start.
  VPWidenPointerInductionRecipe(PHINode *Phi, VPValue *Start, VPValue *Step,
                                const InductionDescriptor &IndDesc,
                                bool IsScalarAfterVectorization)
      : VPHeaderPHIRecipe(VPDef::VPWidenPointerInductionSC, Phi),
        IndDesc(IndDesc),
        IsScalarAfterVectorization(IsScalarAfterVectorization) {
    addOperand(Start);
    addOperand(Step);
  }

  ~VPWidenPointerInductionRecipe() override = default;

  VPWidenPointerInductionRecipe *clone() override {
    return new VPWidenPointerInductionRecipe(
        cast<PHINode>(getUnderlyingInstr()), getOperand(0), getOperand(1),
        IndDesc, IsScalarAfterVectorization);
  }

  VP_CLASSOF_IMPL(VPDef::VPWidenPointerInductionSC)

  /// Generate vector values for the pointer induction.
  void execute(VPTransformState &State) override;

  /// Returns true if only scalar values will be generated.
  bool onlyScalarsGenerated(bool IsScalable);

  /// Returns the induction descriptor for the recipe.
  const InductionDescriptor &getInductionDescriptor() const { return IndDesc; }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif
};

/// A recipe for handling phis that are widened in the vector loop.
/// In the VPlan native path, all incoming VPValues & VPBasicBlock pairs are
/// managed in the recipe directly.
class VPWidenPHIRecipe : public VPSingleDefRecipe {
  /// List of incoming blocks. Only used in the VPlan native path.
  SmallVector<VPBasicBlock *, 2> IncomingBlocks;

public:
  /// Create a new VPWidenPHIRecipe for \p Phi with start value \p Start.
  VPWidenPHIRecipe(PHINode *Phi, VPValue *Start = nullptr)
      : VPSingleDefRecipe(VPDef::VPWidenPHISC, ArrayRef<VPValue *>(), Phi) {
    if (Start)
      addOperand(Start);
  }

  VPWidenPHIRecipe *clone() override {
    llvm_unreachable("cloning not implemented yet");
  }

  ~VPWidenPHIRecipe() override = default;

  VP_CLASSOF_IMPL(VPDef::VPWidenPHISC)

  /// Generate the phi/select nodes.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  /// Adds a pair (\p IncomingV, \p IncomingBlock) to the phi.
  void addIncoming(VPValue *IncomingV, VPBasicBlock *IncomingBlock) {
    addOperand(IncomingV);
    IncomingBlocks.push_back(IncomingBlock);
  }

  /// Returns the \p I th incoming VPBasicBlock.
  VPBasicBlock *getIncomingBlock(unsigned I) { return IncomingBlocks[I]; }

  /// Returns the \p I th incoming VPValue.
  VPValue *getIncomingValue(unsigned I) { return getOperand(I); }
};

/// A recipe for handling first-order recurrence phis. The start value is the
/// first operand of the recipe and the incoming value from the backedge is the
/// second operand.
struct VPFirstOrderRecurrencePHIRecipe : public VPHeaderPHIRecipe {
  VPFirstOrderRecurrencePHIRecipe(PHINode *Phi, VPValue &Start)
      : VPHeaderPHIRecipe(VPDef::VPFirstOrderRecurrencePHISC, Phi, &Start) {}

  VP_CLASSOF_IMPL(VPDef::VPFirstOrderRecurrencePHISC)

  static inline bool classof(const VPHeaderPHIRecipe *R) {
    return R->getVPDefID() == VPDef::VPFirstOrderRecurrencePHISC;
  }

  VPFirstOrderRecurrencePHIRecipe *clone() override {
    return new VPFirstOrderRecurrencePHIRecipe(
        cast<PHINode>(getUnderlyingInstr()), *getOperand(0));
  }

  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif
};

/// A recipe for handling reduction phis. The start value is the first operand
/// of the recipe and the incoming value from the backedge is the second
/// operand.
class VPReductionPHIRecipe : public VPHeaderPHIRecipe {
  /// Descriptor for the reduction.
  const RecurrenceDescriptor &RdxDesc;

  /// The phi is part of an in-loop reduction.
  bool IsInLoop;

  /// The phi is part of an ordered reduction. Requires IsInLoop to be true.
  bool IsOrdered;

public:
  /// Create a new VPReductionPHIRecipe for the reduction \p Phi described by \p
  /// RdxDesc.
  VPReductionPHIRecipe(PHINode *Phi, const RecurrenceDescriptor &RdxDesc,
                       VPValue &Start, bool IsInLoop = false,
                       bool IsOrdered = false)
      : VPHeaderPHIRecipe(VPDef::VPReductionPHISC, Phi, &Start),
        RdxDesc(RdxDesc), IsInLoop(IsInLoop), IsOrdered(IsOrdered) {
    assert((!IsOrdered || IsInLoop) && "IsOrdered requires IsInLoop");
  }

  ~VPReductionPHIRecipe() override = default;

  VPReductionPHIRecipe *clone() override {
    auto *R =
        new VPReductionPHIRecipe(cast<PHINode>(getUnderlyingInstr()), RdxDesc,
                                 *getOperand(0), IsInLoop, IsOrdered);
    R->addOperand(getBackedgeValue());
    return R;
  }

  VP_CLASSOF_IMPL(VPDef::VPReductionPHISC)

  static inline bool classof(const VPHeaderPHIRecipe *R) {
    return R->getVPDefID() == VPDef::VPReductionPHISC;
  }

  /// Generate the phi/select nodes.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  const RecurrenceDescriptor &getRecurrenceDescriptor() const {
    return RdxDesc;
  }

  /// Returns true, if the phi is part of an ordered reduction.
  bool isOrdered() const { return IsOrdered; }

  /// Returns true, if the phi is part of an in-loop reduction.
  bool isInLoop() const { return IsInLoop; }
};

/// A recipe for vectorizing a phi-node as a sequence of mask-based select
/// instructions.
class VPBlendRecipe : public VPSingleDefRecipe {
public:
  /// The blend operation is a User of the incoming values and of their
  /// respective masks, ordered [I0, I1, M1, I2, M2, ...]. Note that the first
  /// incoming value does not have a mask associated.
  VPBlendRecipe(PHINode *Phi, ArrayRef<VPValue *> Operands)
      : VPSingleDefRecipe(VPDef::VPBlendSC, Operands, Phi, Phi->getDebugLoc()) {
    assert((Operands.size() + 1) % 2 == 0 &&
           "Expected an odd number of operands");
  }

  VPBlendRecipe *clone() override {
    SmallVector<VPValue *> Ops(operands());
    return new VPBlendRecipe(cast<PHINode>(getUnderlyingValue()), Ops);
  }

  VP_CLASSOF_IMPL(VPDef::VPBlendSC)

  /// Return the number of incoming values, taking into account that the first
  /// incoming value has no mask.
  unsigned getNumIncomingValues() const { return (getNumOperands() + 1) / 2; }

  /// Return incoming value number \p Idx.
  VPValue *getIncomingValue(unsigned Idx) const {
    return Idx == 0 ? getOperand(0) : getOperand(Idx * 2 - 1);
  }

  /// Return mask number \p Idx.
  VPValue *getMask(unsigned Idx) const {
    assert(Idx > 0 && "First index has no mask associated.");
    return getOperand(Idx * 2);
  }

  /// Generate the phi/select nodes.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  /// Returns true if the recipe only uses the first lane of operand \p Op.
  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    // Recursing through Blend recipes only, must terminate at header phi's the
    // latest.
    return all_of(users(),
                  [this](VPUser *U) { return U->onlyFirstLaneUsed(this); });
  }
};

/// VPInterleaveRecipe is a recipe for transforming an interleave group of load
/// or stores into one wide load/store and shuffles. The first operand of a
/// VPInterleave recipe is the address, followed by the stored values, followed
/// by an optional mask.
class VPInterleaveRecipe : public VPRecipeBase {
  const InterleaveGroup<Instruction> *IG;

  /// Indicates if the interleave group is in a conditional block and requires a
  /// mask.
  bool HasMask = false;

  /// Indicates if gaps between members of the group need to be masked out or if
  /// unusued gaps can be loaded speculatively.
  bool NeedsMaskForGaps = false;

public:
  VPInterleaveRecipe(const InterleaveGroup<Instruction> *IG, VPValue *Addr,
                     ArrayRef<VPValue *> StoredValues, VPValue *Mask,
                     bool NeedsMaskForGaps)
      : VPRecipeBase(VPDef::VPInterleaveSC, {Addr}), IG(IG),
        NeedsMaskForGaps(NeedsMaskForGaps) {
    for (unsigned i = 0; i < IG->getFactor(); ++i)
      if (Instruction *I = IG->getMember(i)) {
        if (I->getType()->isVoidTy())
          continue;
        new VPValue(I, this);
      }

    for (auto *SV : StoredValues)
      addOperand(SV);
    if (Mask) {
      HasMask = true;
      addOperand(Mask);
    }
  }
  ~VPInterleaveRecipe() override = default;

  VPInterleaveRecipe *clone() override {
    return new VPInterleaveRecipe(IG, getAddr(), getStoredValues(), getMask(),
                                  NeedsMaskForGaps);
  }

  VP_CLASSOF_IMPL(VPDef::VPInterleaveSC)

  /// Return the address accessed by this recipe.
  VPValue *getAddr() const {
    return getOperand(0); // Address is the 1st, mandatory operand.
  }

  /// Return the mask used by this recipe. Note that a full mask is represented
  /// by a nullptr.
  VPValue *getMask() const {
    // Mask is optional and therefore the last, currently 2nd operand.
    return HasMask ? getOperand(getNumOperands() - 1) : nullptr;
  }

  /// Return the VPValues stored by this interleave group. If it is a load
  /// interleave group, return an empty ArrayRef.
  ArrayRef<VPValue *> getStoredValues() const {
    // The first operand is the address, followed by the stored values, followed
    // by an optional mask.
    return ArrayRef<VPValue *>(op_begin(), getNumOperands())
        .slice(1, getNumStoreOperands());
  }

  /// Generate the wide load or store, and shuffles.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  const InterleaveGroup<Instruction> *getInterleaveGroup() { return IG; }

  /// Returns the number of stored operands of this interleave group. Returns 0
  /// for load interleave groups.
  unsigned getNumStoreOperands() const {
    return getNumOperands() - (HasMask ? 2 : 1);
  }

  /// The recipe only uses the first lane of the address.
  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return Op == getAddr() && !llvm::is_contained(getStoredValues(), Op);
  }

  Instruction *getInsertPos() const { return IG->getInsertPos(); }
};

/// A recipe to represent inloop reduction operations, performing a reduction on
/// a vector operand into a scalar value, and adding the result to a chain.
/// The Operands are {ChainOp, VecOp, [Condition]}.
class VPReductionRecipe : public VPSingleDefRecipe {
  /// The recurrence decriptor for the reduction in question.
  const RecurrenceDescriptor &RdxDesc;
  bool IsOrdered;
  /// Whether the reduction is conditional.
  bool IsConditional = false;

protected:
  VPReductionRecipe(const unsigned char SC, const RecurrenceDescriptor &R,
                    Instruction *I, ArrayRef<VPValue *> Operands,
                    VPValue *CondOp, bool IsOrdered)
      : VPSingleDefRecipe(SC, Operands, I), RdxDesc(R), IsOrdered(IsOrdered) {
    if (CondOp) {
      IsConditional = true;
      addOperand(CondOp);
    }
  }

public:
  VPReductionRecipe(const RecurrenceDescriptor &R, Instruction *I,
                    VPValue *ChainOp, VPValue *VecOp, VPValue *CondOp,
                    bool IsOrdered)
      : VPReductionRecipe(VPDef::VPReductionSC, R, I,
                          ArrayRef<VPValue *>({ChainOp, VecOp}), CondOp,
                          IsOrdered) {}

  ~VPReductionRecipe() override = default;

  VPReductionRecipe *clone() override {
    return new VPReductionRecipe(RdxDesc, getUnderlyingInstr(), getChainOp(),
                                 getVecOp(), getCondOp(), IsOrdered);
  }

  static inline bool classof(const VPRecipeBase *R) {
    return R->getVPDefID() == VPRecipeBase::VPReductionSC ||
           R->getVPDefID() == VPRecipeBase::VPReductionEVLSC;
  }

  static inline bool classof(const VPUser *U) {
    auto *R = dyn_cast<VPRecipeBase>(U);
    return R && classof(R);
  }

  /// Generate the reduction in the loop
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  /// Return the recurrence decriptor for the in-loop reduction.
  const RecurrenceDescriptor &getRecurrenceDescriptor() const {
    return RdxDesc;
  }
  /// Return true if the in-loop reduction is ordered.
  bool isOrdered() const { return IsOrdered; };
  /// Return true if the in-loop reduction is conditional.
  bool isConditional() const { return IsConditional; };
  /// The VPValue of the scalar Chain being accumulated.
  VPValue *getChainOp() const { return getOperand(0); }
  /// The VPValue of the vector value to be reduced.
  VPValue *getVecOp() const { return getOperand(1); }
  /// The VPValue of the condition for the block.
  VPValue *getCondOp() const {
    return isConditional() ? getOperand(getNumOperands() - 1) : nullptr;
  }
};

/// A recipe to represent inloop reduction operations with vector-predication
/// intrinsics, performing a reduction on a vector operand with the explicit
/// vector length (EVL) into a scalar value, and adding the result to a chain.
/// The Operands are {ChainOp, VecOp, EVL, [Condition]}.
class VPReductionEVLRecipe : public VPReductionRecipe {
public:
  VPReductionEVLRecipe(VPReductionRecipe *R, VPValue *EVL, VPValue *CondOp)
      : VPReductionRecipe(
            VPDef::VPReductionEVLSC, R->getRecurrenceDescriptor(),
            cast_or_null<Instruction>(R->getUnderlyingValue()),
            ArrayRef<VPValue *>({R->getChainOp(), R->getVecOp(), EVL}), CondOp,
            R->isOrdered()) {}

  ~VPReductionEVLRecipe() override = default;

  VPReductionEVLRecipe *clone() override {
    llvm_unreachable("cloning not implemented yet");
  }

  VP_CLASSOF_IMPL(VPDef::VPReductionEVLSC)

  /// Generate the reduction in the loop
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  /// The VPValue of the explicit vector length.
  VPValue *getEVL() const { return getOperand(2); }

  /// Returns true if the recipe only uses the first lane of operand \p Op.
  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return Op == getEVL();
  }
};

/// VPReplicateRecipe replicates a given instruction producing multiple scalar
/// copies of the original scalar type, one per lane, instead of producing a
/// single copy of widened type for all lanes. If the instruction is known to be
/// uniform only one copy, per lane zero, will be generated.
class VPReplicateRecipe : public VPRecipeWithIRFlags {
  /// Indicator if only a single replica per lane is needed.
  bool IsUniform;

  /// Indicator if the replicas are also predicated.
  bool IsPredicated;

public:
  template <typename IterT>
  VPReplicateRecipe(Instruction *I, iterator_range<IterT> Operands,
                    bool IsUniform, VPValue *Mask = nullptr)
      : VPRecipeWithIRFlags(VPDef::VPReplicateSC, Operands, *I),
        IsUniform(IsUniform), IsPredicated(Mask) {
    if (Mask)
      addOperand(Mask);
  }

  ~VPReplicateRecipe() override = default;

  VPReplicateRecipe *clone() override {
    auto *Copy =
        new VPReplicateRecipe(getUnderlyingInstr(), operands(), IsUniform,
                              isPredicated() ? getMask() : nullptr);
    Copy->transferFlags(*this);
    return Copy;
  }

  VP_CLASSOF_IMPL(VPDef::VPReplicateSC)

  /// Generate replicas of the desired Ingredient. Replicas will be generated
  /// for all parts and lanes unless a specific part and lane are specified in
  /// the \p State.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  bool isUniform() const { return IsUniform; }

  bool isPredicated() const { return IsPredicated; }

  /// Returns true if the recipe only uses the first lane of operand \p Op.
  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return isUniform();
  }

  /// Returns true if the recipe uses scalars of operand \p Op.
  bool usesScalars(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return true;
  }

  /// Returns true if the recipe is used by a widened recipe via an intervening
  /// VPPredInstPHIRecipe. In this case, the scalar values should also be packed
  /// in a vector.
  bool shouldPack() const;

  /// Return the mask of a predicated VPReplicateRecipe.
  VPValue *getMask() {
    assert(isPredicated() && "Trying to get the mask of a unpredicated recipe");
    return getOperand(getNumOperands() - 1);
  }

  unsigned getOpcode() const { return getUnderlyingInstr()->getOpcode(); }
};

/// A recipe for generating conditional branches on the bits of a mask.
class VPBranchOnMaskRecipe : public VPRecipeBase {
public:
  VPBranchOnMaskRecipe(VPValue *BlockInMask)
      : VPRecipeBase(VPDef::VPBranchOnMaskSC, {}) {
    if (BlockInMask) // nullptr means all-one mask.
      addOperand(BlockInMask);
  }

  VPBranchOnMaskRecipe *clone() override {
    return new VPBranchOnMaskRecipe(getOperand(0));
  }

  VP_CLASSOF_IMPL(VPDef::VPBranchOnMaskSC)

  /// Generate the extraction of the appropriate bit from the block mask and the
  /// conditional branch.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override {
    O << Indent << "BRANCH-ON-MASK ";
    if (VPValue *Mask = getMask())
      Mask->printAsOperand(O, SlotTracker);
    else
      O << " All-One";
  }
#endif

  /// Return the mask used by this recipe. Note that a full mask is represented
  /// by a nullptr.
  VPValue *getMask() const {
    assert(getNumOperands() <= 1 && "should have either 0 or 1 operands");
    // Mask is optional.
    return getNumOperands() == 1 ? getOperand(0) : nullptr;
  }

  /// Returns true if the recipe uses scalars of operand \p Op.
  bool usesScalars(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return true;
  }
};

/// VPPredInstPHIRecipe is a recipe for generating the phi nodes needed when
/// control converges back from a Branch-on-Mask. The phi nodes are needed in
/// order to merge values that are set under such a branch and feed their uses.
/// The phi nodes can be scalar or vector depending on the users of the value.
/// This recipe works in concert with VPBranchOnMaskRecipe.
class VPPredInstPHIRecipe : public VPSingleDefRecipe {
public:
  /// Construct a VPPredInstPHIRecipe given \p PredInst whose value needs a phi
  /// nodes after merging back from a Branch-on-Mask.
  VPPredInstPHIRecipe(VPValue *PredV)
      : VPSingleDefRecipe(VPDef::VPPredInstPHISC, PredV) {}
  ~VPPredInstPHIRecipe() override = default;

  VPPredInstPHIRecipe *clone() override {
    return new VPPredInstPHIRecipe(getOperand(0));
  }

  VP_CLASSOF_IMPL(VPDef::VPPredInstPHISC)

  /// Generates phi nodes for live-outs as needed to retain SSA form.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  /// Returns true if the recipe uses scalars of operand \p Op.
  bool usesScalars(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return true;
  }
};

/// A common base class for widening memory operations. An optional mask can be
/// provided as the last operand.
class VPWidenMemoryRecipe : public VPRecipeBase {
protected:
  Instruction &Ingredient;

  /// Whether the accessed addresses are consecutive.
  bool Consecutive;

  /// Whether the consecutive accessed addresses are in reverse order.
  bool Reverse;

  /// Whether the memory access is masked.
  bool IsMasked = false;

  void setMask(VPValue *Mask) {
    assert(!IsMasked && "cannot re-set mask");
    if (!Mask)
      return;
    addOperand(Mask);
    IsMasked = true;
  }

  VPWidenMemoryRecipe(const char unsigned SC, Instruction &I,
                      std::initializer_list<VPValue *> Operands,
                      bool Consecutive, bool Reverse, DebugLoc DL)
      : VPRecipeBase(SC, Operands, DL), Ingredient(I), Consecutive(Consecutive),
        Reverse(Reverse) {
    assert((Consecutive || !Reverse) && "Reverse implies consecutive");
  }

public:
  VPWidenMemoryRecipe *clone() override {
    llvm_unreachable("cloning not supported");
  }

  static inline bool classof(const VPRecipeBase *R) {
    return R->getVPDefID() == VPRecipeBase::VPWidenLoadSC ||
           R->getVPDefID() == VPRecipeBase::VPWidenStoreSC ||
           R->getVPDefID() == VPRecipeBase::VPWidenLoadEVLSC ||
           R->getVPDefID() == VPRecipeBase::VPWidenStoreEVLSC;
  }

  static inline bool classof(const VPUser *U) {
    auto *R = dyn_cast<VPRecipeBase>(U);
    return R && classof(R);
  }

  /// Return whether the loaded-from / stored-to addresses are consecutive.
  bool isConsecutive() const { return Consecutive; }

  /// Return whether the consecutive loaded/stored addresses are in reverse
  /// order.
  bool isReverse() const { return Reverse; }

  /// Return the address accessed by this recipe.
  VPValue *getAddr() const { return getOperand(0); }

  /// Returns true if the recipe is masked.
  bool isMasked() const { return IsMasked; }

  /// Return the mask used by this recipe. Note that a full mask is represented
  /// by a nullptr.
  VPValue *getMask() const {
    // Mask is optional and therefore the last operand.
    return isMasked() ? getOperand(getNumOperands() - 1) : nullptr;
  }

  /// Generate the wide load/store.
  void execute(VPTransformState &State) override {
    llvm_unreachable("VPWidenMemoryRecipe should not be instantiated.");
  }

  Instruction &getIngredient() const { return Ingredient; }
};

/// A recipe for widening load operations, using the address to load from and an
/// optional mask.
struct VPWidenLoadRecipe final : public VPWidenMemoryRecipe, public VPValue {
  VPWidenLoadRecipe(LoadInst &Load, VPValue *Addr, VPValue *Mask,
                    bool Consecutive, bool Reverse, DebugLoc DL)
      : VPWidenMemoryRecipe(VPDef::VPWidenLoadSC, Load, {Addr}, Consecutive,
                            Reverse, DL),
        VPValue(this, &Load) {
    setMask(Mask);
  }

  VPWidenLoadRecipe *clone() override {
    return new VPWidenLoadRecipe(cast<LoadInst>(Ingredient), getAddr(),
                                 getMask(), Consecutive, Reverse,
                                 getDebugLoc());
  }

  VP_CLASSOF_IMPL(VPDef::VPWidenLoadSC);

  /// Generate a wide load or gather.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  /// Returns true if the recipe only uses the first lane of operand \p Op.
  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    // Widened, consecutive loads operations only demand the first lane of
    // their address.
    return Op == getAddr() && isConsecutive();
  }
};

/// A recipe for widening load operations with vector-predication intrinsics,
/// using the address to load from, the explicit vector length and an optional
/// mask.
struct VPWidenLoadEVLRecipe final : public VPWidenMemoryRecipe, public VPValue {
  VPWidenLoadEVLRecipe(VPWidenLoadRecipe *L, VPValue *EVL, VPValue *Mask)
      : VPWidenMemoryRecipe(VPDef::VPWidenLoadEVLSC, L->getIngredient(),
                            {L->getAddr(), EVL}, L->isConsecutive(),
                            L->isReverse(), L->getDebugLoc()),
        VPValue(this, &getIngredient()) {
    setMask(Mask);
  }

  VP_CLASSOF_IMPL(VPDef::VPWidenLoadEVLSC)

  /// Return the EVL operand.
  VPValue *getEVL() const { return getOperand(1); }

  /// Generate the wide load or gather.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  /// Returns true if the recipe only uses the first lane of operand \p Op.
  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    // Widened loads only demand the first lane of EVL and consecutive loads
    // only demand the first lane of their address.
    return Op == getEVL() || (Op == getAddr() && isConsecutive());
  }
};

/// A recipe for widening store operations, using the stored value, the address
/// to store to and an optional mask.
struct VPWidenStoreRecipe final : public VPWidenMemoryRecipe {
  VPWidenStoreRecipe(StoreInst &Store, VPValue *Addr, VPValue *StoredVal,
                     VPValue *Mask, bool Consecutive, bool Reverse, DebugLoc DL)
      : VPWidenMemoryRecipe(VPDef::VPWidenStoreSC, Store, {Addr, StoredVal},
                            Consecutive, Reverse, DL) {
    setMask(Mask);
  }

  VPWidenStoreRecipe *clone() override {
    return new VPWidenStoreRecipe(cast<StoreInst>(Ingredient), getAddr(),
                                  getStoredValue(), getMask(), Consecutive,
                                  Reverse, getDebugLoc());
  }

  VP_CLASSOF_IMPL(VPDef::VPWidenStoreSC);

  /// Return the value stored by this recipe.
  VPValue *getStoredValue() const { return getOperand(1); }

  /// Generate a wide store or scatter.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  /// Returns true if the recipe only uses the first lane of operand \p Op.
  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    // Widened, consecutive stores only demand the first lane of their address,
    // unless the same operand is also stored.
    return Op == getAddr() && isConsecutive() && Op != getStoredValue();
  }
};

/// A recipe for widening store operations with vector-predication intrinsics,
/// using the value to store, the address to store to, the explicit vector
/// length and an optional mask.
struct VPWidenStoreEVLRecipe final : public VPWidenMemoryRecipe {
  VPWidenStoreEVLRecipe(VPWidenStoreRecipe *S, VPValue *EVL, VPValue *Mask)
      : VPWidenMemoryRecipe(VPDef::VPWidenStoreEVLSC, S->getIngredient(),
                            {S->getAddr(), S->getStoredValue(), EVL},
                            S->isConsecutive(), S->isReverse(),
                            S->getDebugLoc()) {
    setMask(Mask);
  }

  VP_CLASSOF_IMPL(VPDef::VPWidenStoreEVLSC)

  /// Return the address accessed by this recipe.
  VPValue *getStoredValue() const { return getOperand(1); }

  /// Return the EVL operand.
  VPValue *getEVL() const { return getOperand(2); }

  /// Generate the wide store or scatter.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  /// Returns true if the recipe only uses the first lane of operand \p Op.
  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    if (Op == getEVL()) {
      assert(getStoredValue() != Op && "unexpected store of EVL");
      return true;
    }
    // Widened, consecutive memory operations only demand the first lane of
    // their address, unless the same operand is also stored. That latter can
    // happen with opaque pointers.
    return Op == getAddr() && isConsecutive() && Op != getStoredValue();
  }
};

/// Recipe to expand a SCEV expression.
class VPExpandSCEVRecipe : public VPSingleDefRecipe {
  const SCEV *Expr;
  ScalarEvolution &SE;

public:
  VPExpandSCEVRecipe(const SCEV *Expr, ScalarEvolution &SE)
      : VPSingleDefRecipe(VPDef::VPExpandSCEVSC, {}), Expr(Expr), SE(SE) {}

  ~VPExpandSCEVRecipe() override = default;

  VPExpandSCEVRecipe *clone() override {
    return new VPExpandSCEVRecipe(Expr, SE);
  }

  VP_CLASSOF_IMPL(VPDef::VPExpandSCEVSC)

  /// Generate a canonical vector induction variable of the vector loop, with
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  const SCEV *getSCEV() const { return Expr; }
};

/// Canonical scalar induction phi of the vector loop. Starting at the specified
/// start value (either 0 or the resume value when vectorizing the epilogue
/// loop). VPWidenCanonicalIVRecipe represents the vector version of the
/// canonical induction variable.
class VPCanonicalIVPHIRecipe : public VPHeaderPHIRecipe {
public:
  VPCanonicalIVPHIRecipe(VPValue *StartV, DebugLoc DL)
      : VPHeaderPHIRecipe(VPDef::VPCanonicalIVPHISC, nullptr, StartV, DL) {}

  ~VPCanonicalIVPHIRecipe() override = default;

  VPCanonicalIVPHIRecipe *clone() override {
    auto *R = new VPCanonicalIVPHIRecipe(getOperand(0), getDebugLoc());
    R->addOperand(getBackedgeValue());
    return R;
  }

  VP_CLASSOF_IMPL(VPDef::VPCanonicalIVPHISC)

  static inline bool classof(const VPHeaderPHIRecipe *D) {
    return D->getVPDefID() == VPDef::VPCanonicalIVPHISC;
  }

  /// Generate the canonical scalar induction phi of the vector loop.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  /// Returns the scalar type of the induction.
  Type *getScalarType() const {
    return getStartValue()->getLiveInIRValue()->getType();
  }

  /// Returns true if the recipe only uses the first lane of operand \p Op.
  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return true;
  }

  /// Returns true if the recipe only uses the first part of operand \p Op.
  bool onlyFirstPartUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return true;
  }

  /// Check if the induction described by \p Kind, /p Start and \p Step is
  /// canonical, i.e.  has the same start and step (of 1) as the canonical IV.
  bool isCanonical(InductionDescriptor::InductionKind Kind, VPValue *Start,
                   VPValue *Step) const;
};

/// A recipe for generating the active lane mask for the vector loop that is
/// used to predicate the vector operations.
/// TODO: It would be good to use the existing VPWidenPHIRecipe instead and
/// remove VPActiveLaneMaskPHIRecipe.
class VPActiveLaneMaskPHIRecipe : public VPHeaderPHIRecipe {
public:
  VPActiveLaneMaskPHIRecipe(VPValue *StartMask, DebugLoc DL)
      : VPHeaderPHIRecipe(VPDef::VPActiveLaneMaskPHISC, nullptr, StartMask,
                          DL) {}

  ~VPActiveLaneMaskPHIRecipe() override = default;

  VPActiveLaneMaskPHIRecipe *clone() override {
    return new VPActiveLaneMaskPHIRecipe(getOperand(0), getDebugLoc());
  }

  VP_CLASSOF_IMPL(VPDef::VPActiveLaneMaskPHISC)

  static inline bool classof(const VPHeaderPHIRecipe *D) {
    return D->getVPDefID() == VPDef::VPActiveLaneMaskPHISC;
  }

  /// Generate the active lane mask phi of the vector loop.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif
};

/// A recipe for generating the phi node for the current index of elements,
/// adjusted in accordance with EVL value. It starts at the start value of the
/// canonical induction and gets incremented by EVL in each iteration of the
/// vector loop.
class VPEVLBasedIVPHIRecipe : public VPHeaderPHIRecipe {
public:
  VPEVLBasedIVPHIRecipe(VPValue *StartIV, DebugLoc DL)
      : VPHeaderPHIRecipe(VPDef::VPEVLBasedIVPHISC, nullptr, StartIV, DL) {}

  ~VPEVLBasedIVPHIRecipe() override = default;

  VPEVLBasedIVPHIRecipe *clone() override {
    llvm_unreachable("cloning not implemented yet");
  }

  VP_CLASSOF_IMPL(VPDef::VPEVLBasedIVPHISC)

  static inline bool classof(const VPHeaderPHIRecipe *D) {
    return D->getVPDefID() == VPDef::VPEVLBasedIVPHISC;
  }

  /// Generate phi for handling IV based on EVL over iterations correctly.
  /// TODO: investigate if it can share the code with VPCanonicalIVPHIRecipe.
  void execute(VPTransformState &State) override;

  /// Returns true if the recipe only uses the first lane of operand \p Op.
  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return true;
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif
};

/// A Recipe for widening the canonical induction variable of the vector loop.
class VPWidenCanonicalIVRecipe : public VPSingleDefRecipe {
public:
  VPWidenCanonicalIVRecipe(VPCanonicalIVPHIRecipe *CanonicalIV)
      : VPSingleDefRecipe(VPDef::VPWidenCanonicalIVSC, {CanonicalIV}) {}

  ~VPWidenCanonicalIVRecipe() override = default;

  VPWidenCanonicalIVRecipe *clone() override {
    return new VPWidenCanonicalIVRecipe(
        cast<VPCanonicalIVPHIRecipe>(getOperand(0)));
  }

  VP_CLASSOF_IMPL(VPDef::VPWidenCanonicalIVSC)

  /// Generate a canonical vector induction variable of the vector loop, with
  /// start = {<Part*VF, Part*VF+1, ..., Part*VF+VF-1> for 0 <= Part < UF}, and
  /// step = <VF*UF, VF*UF, ..., VF*UF>.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif
};

/// A recipe for converting the input value \p IV value to the corresponding
/// value of an IV with different start and step values, using Start + IV *
/// Step.
class VPDerivedIVRecipe : public VPSingleDefRecipe {
  /// Kind of the induction.
  const InductionDescriptor::InductionKind Kind;
  /// If not nullptr, the floating point induction binary operator. Must be set
  /// for floating point inductions.
  const FPMathOperator *FPBinOp;

public:
  VPDerivedIVRecipe(const InductionDescriptor &IndDesc, VPValue *Start,
                    VPCanonicalIVPHIRecipe *CanonicalIV, VPValue *Step)
      : VPDerivedIVRecipe(
            IndDesc.getKind(),
            dyn_cast_or_null<FPMathOperator>(IndDesc.getInductionBinOp()),
            Start, CanonicalIV, Step) {}

  VPDerivedIVRecipe(InductionDescriptor::InductionKind Kind,
                    const FPMathOperator *FPBinOp, VPValue *Start, VPValue *IV,
                    VPValue *Step)
      : VPSingleDefRecipe(VPDef::VPDerivedIVSC, {Start, IV, Step}), Kind(Kind),
        FPBinOp(FPBinOp) {}

  ~VPDerivedIVRecipe() override = default;

  VPDerivedIVRecipe *clone() override {
    return new VPDerivedIVRecipe(Kind, FPBinOp, getStartValue(), getOperand(1),
                                 getStepValue());
  }

  VP_CLASSOF_IMPL(VPDef::VPDerivedIVSC)

  /// Generate the transformed value of the induction at offset StartValue (1.
  /// operand) + IV (2. operand) * StepValue (3, operand).
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  Type *getScalarType() const {
    return getStartValue()->getLiveInIRValue()->getType();
  }

  VPValue *getStartValue() const { return getOperand(0); }
  VPValue *getStepValue() const { return getOperand(2); }

  /// Returns true if the recipe only uses the first lane of operand \p Op.
  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return true;
  }
};

/// A recipe for handling phi nodes of integer and floating-point inductions,
/// producing their scalar values.
class VPScalarIVStepsRecipe : public VPRecipeWithIRFlags {
  Instruction::BinaryOps InductionOpcode;

public:
  VPScalarIVStepsRecipe(VPValue *IV, VPValue *Step,
                        Instruction::BinaryOps Opcode, FastMathFlags FMFs)
      : VPRecipeWithIRFlags(VPDef::VPScalarIVStepsSC,
                            ArrayRef<VPValue *>({IV, Step}), FMFs),
        InductionOpcode(Opcode) {}

  VPScalarIVStepsRecipe(const InductionDescriptor &IndDesc, VPValue *IV,
                        VPValue *Step)
      : VPScalarIVStepsRecipe(
            IV, Step, IndDesc.getInductionOpcode(),
            dyn_cast_or_null<FPMathOperator>(IndDesc.getInductionBinOp())
                ? IndDesc.getInductionBinOp()->getFastMathFlags()
                : FastMathFlags()) {}

  ~VPScalarIVStepsRecipe() override = default;

  VPScalarIVStepsRecipe *clone() override {
    return new VPScalarIVStepsRecipe(
        getOperand(0), getOperand(1), InductionOpcode,
        hasFastMathFlags() ? getFastMathFlags() : FastMathFlags());
  }

  VP_CLASSOF_IMPL(VPDef::VPScalarIVStepsSC)

  /// Generate the scalarized versions of the phi node as needed by their users.
  void execute(VPTransformState &State) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the recipe.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
#endif

  VPValue *getStepValue() const { return getOperand(1); }

  /// Returns true if the recipe only uses the first lane of operand \p Op.
  bool onlyFirstLaneUsed(const VPValue *Op) const override {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return true;
  }
};

/// VPBasicBlock serves as the leaf of the Hierarchical Control-Flow Graph. It
/// holds a sequence of zero or more VPRecipe's each representing a sequence of
/// output IR instructions. All PHI-like recipes must come before any non-PHI recipes.
class VPBasicBlock : public VPBlockBase {
public:
  using RecipeListTy = iplist<VPRecipeBase>;

protected:
  /// The VPRecipes held in the order of output instructions to generate.
  RecipeListTy Recipes;

  VPBasicBlock(const unsigned char BlockSC, const Twine &Name = "")
      : VPBlockBase(BlockSC, Name.str()) {}

public:
  VPBasicBlock(const Twine &Name = "", VPRecipeBase *Recipe = nullptr)
      : VPBlockBase(VPBasicBlockSC, Name.str()) {
    if (Recipe)
      appendRecipe(Recipe);
  }

  ~VPBasicBlock() override {
    while (!Recipes.empty())
      Recipes.pop_back();
  }

  /// Instruction iterators...
  using iterator = RecipeListTy::iterator;
  using const_iterator = RecipeListTy::const_iterator;
  using reverse_iterator = RecipeListTy::reverse_iterator;
  using const_reverse_iterator = RecipeListTy::const_reverse_iterator;

  //===--------------------------------------------------------------------===//
  /// Recipe iterator methods
  ///
  inline iterator begin() { return Recipes.begin(); }
  inline const_iterator begin() const { return Recipes.begin(); }
  inline iterator end() { return Recipes.end(); }
  inline const_iterator end() const { return Recipes.end(); }

  inline reverse_iterator rbegin() { return Recipes.rbegin(); }
  inline const_reverse_iterator rbegin() const { return Recipes.rbegin(); }
  inline reverse_iterator rend() { return Recipes.rend(); }
  inline const_reverse_iterator rend() const { return Recipes.rend(); }

  inline size_t size() const { return Recipes.size(); }
  inline bool empty() const { return Recipes.empty(); }
  inline const VPRecipeBase &front() const { return Recipes.front(); }
  inline VPRecipeBase &front() { return Recipes.front(); }
  inline const VPRecipeBase &back() const { return Recipes.back(); }
  inline VPRecipeBase &back() { return Recipes.back(); }

  /// Returns a reference to the list of recipes.
  RecipeListTy &getRecipeList() { return Recipes; }

  /// Returns a pointer to a member of the recipe list.
  static RecipeListTy VPBasicBlock::*getSublistAccess(VPRecipeBase *) {
    return &VPBasicBlock::Recipes;
  }

  /// Method to support type inquiry through isa, cast, and dyn_cast.
  static inline bool classof(const VPBlockBase *V) {
    return V->getVPBlockID() == VPBlockBase::VPBasicBlockSC ||
           V->getVPBlockID() == VPBlockBase::VPIRBasicBlockSC;
  }

  void insert(VPRecipeBase *Recipe, iterator InsertPt) {
    assert(Recipe && "No recipe to append.");
    assert(!Recipe->Parent && "Recipe already in VPlan");
    Recipe->Parent = this;
    Recipes.insert(InsertPt, Recipe);
  }

  /// Augment the existing recipes of a VPBasicBlock with an additional
  /// \p Recipe as the last recipe.
  void appendRecipe(VPRecipeBase *Recipe) { insert(Recipe, end()); }

  /// The method which generates the output IR instructions that correspond to
  /// this VPBasicBlock, thereby "executing" the VPlan.
  void execute(VPTransformState *State) override;

  /// Return the cost of this VPBasicBlock.
  InstructionCost cost(ElementCount VF, VPCostContext &Ctx) override;

  /// Return the position of the first non-phi node recipe in the block.
  iterator getFirstNonPhi();

  /// Returns an iterator range over the PHI-like recipes in the block.
  iterator_range<iterator> phis() {
    return make_range(begin(), getFirstNonPhi());
  }

  void dropAllReferences(VPValue *NewValue) override;

  /// Split current block at \p SplitAt by inserting a new block between the
  /// current block and its successors and moving all recipes starting at
  /// SplitAt to the new block. Returns the new block.
  VPBasicBlock *splitAt(iterator SplitAt);

  VPRegionBlock *getEnclosingLoopRegion();

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print this VPBsicBlock to \p O, prefixing all lines with \p Indent. \p
  /// SlotTracker is used to print unnamed VPValue's using consequtive numbers.
  ///
  /// Note that the numbering is applied to the whole VPlan, so printing
  /// individual blocks is consistent with the whole VPlan printing.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
  using VPBlockBase::print; // Get the print(raw_stream &O) version.
#endif

  /// If the block has multiple successors, return the branch recipe terminating
  /// the block. If there are no or only a single successor, return nullptr;
  VPRecipeBase *getTerminator();
  const VPRecipeBase *getTerminator() const;

  /// Returns true if the block is exiting it's parent region.
  bool isExiting() const;

  /// Clone the current block and it's recipes, without updating the operands of
  /// the cloned recipes.
  VPBasicBlock *clone() override {
    auto *NewBlock = new VPBasicBlock(getName());
    for (VPRecipeBase &R : *this)
      NewBlock->appendRecipe(R.clone());
    return NewBlock;
  }

protected:
  /// Execute the recipes in the IR basic block \p BB.
  void executeRecipes(VPTransformState *State, BasicBlock *BB);

private:
  /// Create an IR BasicBlock to hold the output instructions generated by this
  /// VPBasicBlock, and return it. Update the CFGState accordingly.
  BasicBlock *createEmptyBasicBlock(VPTransformState::CFGState &CFG);
};

/// A special type of VPBasicBlock that wraps an existing IR basic block.
/// Recipes of the block get added before the first non-phi instruction in the
/// wrapped block.
/// Note: At the moment, VPIRBasicBlock can only be used to wrap VPlan's
/// preheader block.
class VPIRBasicBlock : public VPBasicBlock {
  BasicBlock *IRBB;

public:
  VPIRBasicBlock(BasicBlock *IRBB)
      : VPBasicBlock(VPIRBasicBlockSC,
                     (Twine("ir-bb<") + IRBB->getName() + Twine(">")).str()),
        IRBB(IRBB) {}

  ~VPIRBasicBlock() override {}

  static inline bool classof(const VPBlockBase *V) {
    return V->getVPBlockID() == VPBlockBase::VPIRBasicBlockSC;
  }

  /// The method which generates the output IR instructions that correspond to
  /// this VPBasicBlock, thereby "executing" the VPlan.
  void execute(VPTransformState *State) override;

  VPIRBasicBlock *clone() override {
    auto *NewBlock = new VPIRBasicBlock(IRBB);
    for (VPRecipeBase &R : Recipes)
      NewBlock->appendRecipe(R.clone());
    return NewBlock;
  }

  BasicBlock *getIRBasicBlock() const { return IRBB; }
};

/// VPRegionBlock represents a collection of VPBasicBlocks and VPRegionBlocks
/// which form a Single-Entry-Single-Exiting subgraph of the output IR CFG.
/// A VPRegionBlock may indicate that its contents are to be replicated several
/// times. This is designed to support predicated scalarization, in which a
/// scalar if-then code structure needs to be generated VF * UF times. Having
/// this replication indicator helps to keep a single model for multiple
/// candidate VF's. The actual replication takes place only once the desired VF
/// and UF have been determined.
class VPRegionBlock : public VPBlockBase {
  /// Hold the Single Entry of the SESE region modelled by the VPRegionBlock.
  VPBlockBase *Entry;

  /// Hold the Single Exiting block of the SESE region modelled by the
  /// VPRegionBlock.
  VPBlockBase *Exiting;

  /// An indicator whether this region is to generate multiple replicated
  /// instances of output IR corresponding to its VPBlockBases.
  bool IsReplicator;

public:
  VPRegionBlock(VPBlockBase *Entry, VPBlockBase *Exiting,
                const std::string &Name = "", bool IsReplicator = false)
      : VPBlockBase(VPRegionBlockSC, Name), Entry(Entry), Exiting(Exiting),
        IsReplicator(IsReplicator) {
    assert(Entry->getPredecessors().empty() && "Entry block has predecessors.");
    assert(Exiting->getSuccessors().empty() && "Exit block has successors.");
    Entry->setParent(this);
    Exiting->setParent(this);
  }
  VPRegionBlock(const std::string &Name = "", bool IsReplicator = false)
      : VPBlockBase(VPRegionBlockSC, Name), Entry(nullptr), Exiting(nullptr),
        IsReplicator(IsReplicator) {}

  ~VPRegionBlock() override {
    if (Entry) {
      VPValue DummyValue;
      Entry->dropAllReferences(&DummyValue);
      deleteCFG(Entry);
    }
  }

  /// Method to support type inquiry through isa, cast, and dyn_cast.
  static inline bool classof(const VPBlockBase *V) {
    return V->getVPBlockID() == VPBlockBase::VPRegionBlockSC;
  }

  const VPBlockBase *getEntry() const { return Entry; }
  VPBlockBase *getEntry() { return Entry; }

  /// Set \p EntryBlock as the entry VPBlockBase of this VPRegionBlock. \p
  /// EntryBlock must have no predecessors.
  void setEntry(VPBlockBase *EntryBlock) {
    assert(EntryBlock->getPredecessors().empty() &&
           "Entry block cannot have predecessors.");
    Entry = EntryBlock;
    EntryBlock->setParent(this);
  }

  const VPBlockBase *getExiting() const { return Exiting; }
  VPBlockBase *getExiting() { return Exiting; }

  /// Set \p ExitingBlock as the exiting VPBlockBase of this VPRegionBlock. \p
  /// ExitingBlock must have no successors.
  void setExiting(VPBlockBase *ExitingBlock) {
    assert(ExitingBlock->getSuccessors().empty() &&
           "Exit block cannot have successors.");
    Exiting = ExitingBlock;
    ExitingBlock->setParent(this);
  }

  /// Returns the pre-header VPBasicBlock of the loop region.
  VPBasicBlock *getPreheaderVPBB() {
    assert(!isReplicator() && "should only get pre-header of loop regions");
    return getSinglePredecessor()->getExitingBasicBlock();
  }

  /// An indicator whether this region is to generate multiple replicated
  /// instances of output IR corresponding to its VPBlockBases.
  bool isReplicator() const { return IsReplicator; }

  /// The method which generates the output IR instructions that correspond to
  /// this VPRegionBlock, thereby "executing" the VPlan.
  void execute(VPTransformState *State) override;

  // Return the cost of this region.
  InstructionCost cost(ElementCount VF, VPCostContext &Ctx) override;

  void dropAllReferences(VPValue *NewValue) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print this VPRegionBlock to \p O (recursively), prefixing all lines with
  /// \p Indent. \p SlotTracker is used to print unnamed VPValue's using
  /// consequtive numbers.
  ///
  /// Note that the numbering is applied to the whole VPlan, so printing
  /// individual regions is consistent with the whole VPlan printing.
  void print(raw_ostream &O, const Twine &Indent,
             VPSlotTracker &SlotTracker) const override;
  using VPBlockBase::print; // Get the print(raw_stream &O) version.
#endif

  /// Clone all blocks in the single-entry single-exit region of the block and
  /// their recipes without updating the operands of the cloned recipes.
  VPRegionBlock *clone() override;
};

/// VPlan models a candidate for vectorization, encoding various decisions take
/// to produce efficient output IR, including which branches, basic-blocks and
/// output IR instructions to generate, and their cost. VPlan holds a
/// Hierarchical-CFG of VPBasicBlocks and VPRegionBlocks rooted at an Entry
/// VPBasicBlock.
class VPlan {
  friend class VPlanPrinter;
  friend class VPSlotTracker;

  /// Hold the single entry to the Hierarchical CFG of the VPlan, i.e. the
  /// preheader of the vector loop.
  VPBasicBlock *Entry;

  /// VPBasicBlock corresponding to the original preheader. Used to place
  /// VPExpandSCEV recipes for expressions used during skeleton creation and the
  /// rest of VPlan execution.
  VPBasicBlock *Preheader;

  /// Holds the VFs applicable to this VPlan.
  SmallSetVector<ElementCount, 2> VFs;

  /// Holds the UFs applicable to this VPlan. If empty, the VPlan is valid for
  /// any UF.
  SmallSetVector<unsigned, 2> UFs;

  /// Holds the name of the VPlan, for printing.
  std::string Name;

  /// Represents the trip count of the original loop, for folding
  /// the tail.
  VPValue *TripCount = nullptr;

  /// Represents the backedge taken count of the original loop, for folding
  /// the tail. It equals TripCount - 1.
  VPValue *BackedgeTakenCount = nullptr;

  /// Represents the vector trip count.
  VPValue VectorTripCount;

  /// Represents the loop-invariant VF * UF of the vector loop region.
  VPValue VFxUF;

  /// Holds a mapping between Values and their corresponding VPValue inside
  /// VPlan.
  Value2VPValueTy Value2VPValue;

  /// Contains all the external definitions created for this VPlan. External
  /// definitions are VPValues that hold a pointer to their underlying IR.
  SmallVector<VPValue *, 16> VPLiveInsToFree;

  /// Values used outside the plan. It contains live-outs that need fixing. Any
  /// live-out that is fixed outside VPlan needs to be removed. The remaining
  /// live-outs are fixed via VPLiveOut::fixPhi.
  MapVector<PHINode *, VPLiveOut *> LiveOuts;

  /// Mapping from SCEVs to the VPValues representing their expansions.
  /// NOTE: This mapping is temporary and will be removed once all users have
  /// been modeled in VPlan directly.
  DenseMap<const SCEV *, VPValue *> SCEVToExpansion;

public:
  /// Construct a VPlan with original preheader \p Preheader, trip count \p TC
  /// and \p Entry to the plan. At the moment, \p Preheader and \p Entry need to
  /// be disconnected, as the bypass blocks between them are not yet modeled in
  /// VPlan.
  VPlan(VPBasicBlock *Preheader, VPValue *TC, VPBasicBlock *Entry)
      : VPlan(Preheader, Entry) {
    TripCount = TC;
  }

  /// Construct a VPlan with original preheader \p Preheader and \p Entry to
  /// the plan. At the moment, \p Preheader and \p Entry need to be
  /// disconnected, as the bypass blocks between them are not yet modeled in
  /// VPlan.
  VPlan(VPBasicBlock *Preheader, VPBasicBlock *Entry)
      : Entry(Entry), Preheader(Preheader) {
    Entry->setPlan(this);
    Preheader->setPlan(this);
    assert(Preheader->getNumSuccessors() == 0 &&
           Preheader->getNumPredecessors() == 0 &&
           "preheader must be disconnected");
  }

  ~VPlan();

  /// Create initial VPlan, having an "entry" VPBasicBlock (wrapping
  /// original scalar pre-header ) which contains SCEV expansions that need
  /// to happen before the CFG is modified; a VPBasicBlock for the vector
  /// pre-header, followed by a region for the vector loop, followed by the
  /// middle VPBasicBlock. If a check is needed to guard executing the scalar
  /// epilogue loop, it will be added to the middle block, together with
  /// VPBasicBlocks for the scalar preheader and exit blocks.
  static VPlanPtr createInitialVPlan(const SCEV *TripCount,
                                     ScalarEvolution &PSE,
                                     bool RequiresScalarEpilogueCheck,
                                     bool TailFolded, Loop *TheLoop);

  /// Prepare the plan for execution, setting up the required live-in values.
  void prepareToExecute(Value *TripCount, Value *VectorTripCount,
                        Value *CanonicalIVStartValue, VPTransformState &State);

  /// Generate the IR code for this VPlan.
  void execute(VPTransformState *State);

  /// Return the cost of this plan.
  InstructionCost cost(ElementCount VF, VPCostContext &Ctx);

  VPBasicBlock *getEntry() { return Entry; }
  const VPBasicBlock *getEntry() const { return Entry; }

  /// The trip count of the original loop.
  VPValue *getTripCount() const {
    assert(TripCount && "trip count needs to be set before accessing it");
    return TripCount;
  }

  /// Resets the trip count for the VPlan. The caller must make sure all uses of
  /// the original trip count have been replaced.
  void resetTripCount(VPValue *NewTripCount) {
    assert(TripCount && NewTripCount && TripCount->getNumUsers() == 0 &&
           "TripCount always must be set");
    TripCount = NewTripCount;
  }

  /// The backedge taken count of the original loop.
  VPValue *getOrCreateBackedgeTakenCount() {
    if (!BackedgeTakenCount)
      BackedgeTakenCount = new VPValue();
    return BackedgeTakenCount;
  }

  /// The vector trip count.
  VPValue &getVectorTripCount() { return VectorTripCount; }

  /// Returns VF * UF of the vector loop region.
  VPValue &getVFxUF() { return VFxUF; }

  void addVF(ElementCount VF) { VFs.insert(VF); }

  void setVF(ElementCount VF) {
    assert(hasVF(VF) && "Cannot set VF not already in plan");
    VFs.clear();
    VFs.insert(VF);
  }

  bool hasVF(ElementCount VF) { return VFs.count(VF); }
  bool hasScalableVF() {
    return any_of(VFs, [](ElementCount VF) { return VF.isScalable(); });
  }

  /// Returns an iterator range over all VFs of the plan.
  iterator_range<SmallSetVector<ElementCount, 2>::iterator>
  vectorFactors() const {
    return {VFs.begin(), VFs.end()};
  }

  bool hasScalarVFOnly() const { return VFs.size() == 1 && VFs[0].isScalar(); }

  bool hasUF(unsigned UF) const { return UFs.empty() || UFs.contains(UF); }

  void setUF(unsigned UF) {
    assert(hasUF(UF) && "Cannot set the UF not already in plan");
    UFs.clear();
    UFs.insert(UF);
  }

  /// Return a string with the name of the plan and the applicable VFs and UFs.
  std::string getName() const;

  void setName(const Twine &newName) { Name = newName.str(); }

  /// Gets the live-in VPValue for \p V or adds a new live-in (if none exists
  ///  yet) for \p V.
  VPValue *getOrAddLiveIn(Value *V) {
    assert(V && "Trying to get or add the VPValue of a null Value");
    if (!Value2VPValue.count(V)) {
      VPValue *VPV = new VPValue(V);
      VPLiveInsToFree.push_back(VPV);
      assert(VPV->isLiveIn() && "VPV must be a live-in.");
      assert(!Value2VPValue.count(V) && "Value already exists in VPlan");
      Value2VPValue[V] = VPV;
    }

    assert(Value2VPValue.count(V) && "Value does not exist in VPlan");
    assert(Value2VPValue[V]->isLiveIn() &&
           "Only live-ins should be in mapping");
    return Value2VPValue[V];
  }

  /// Return the live-in VPValue for \p V, if there is one or nullptr otherwise.
  VPValue *getLiveIn(Value *V) const { return Value2VPValue.lookup(V); }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the live-ins of this VPlan to \p O.
  void printLiveIns(raw_ostream &O) const;

  /// Print this VPlan to \p O.
  void print(raw_ostream &O) const;

  /// Print this VPlan in DOT format to \p O.
  void printDOT(raw_ostream &O) const;

  /// Dump the plan to stderr (for debugging).
  LLVM_DUMP_METHOD void dump() const;
#endif

  /// Returns the VPRegionBlock of the vector loop.
  VPRegionBlock *getVectorLoopRegion() {
    return cast<VPRegionBlock>(getEntry()->getSingleSuccessor());
  }
  const VPRegionBlock *getVectorLoopRegion() const {
    return cast<VPRegionBlock>(getEntry()->getSingleSuccessor());
  }

  /// Returns the canonical induction recipe of the vector loop.
  VPCanonicalIVPHIRecipe *getCanonicalIV() {
    VPBasicBlock *EntryVPBB = getVectorLoopRegion()->getEntryBasicBlock();
    if (EntryVPBB->empty()) {
      // VPlan native path.
      EntryVPBB = cast<VPBasicBlock>(EntryVPBB->getSingleSuccessor());
    }
    return cast<VPCanonicalIVPHIRecipe>(&*EntryVPBB->begin());
  }

  void addLiveOut(PHINode *PN, VPValue *V);

  void removeLiveOut(PHINode *PN) {
    delete LiveOuts[PN];
    LiveOuts.erase(PN);
  }

  const MapVector<PHINode *, VPLiveOut *> &getLiveOuts() const {
    return LiveOuts;
  }

  VPValue *getSCEVExpansion(const SCEV *S) const {
    return SCEVToExpansion.lookup(S);
  }

  void addSCEVExpansion(const SCEV *S, VPValue *V) {
    assert(!SCEVToExpansion.contains(S) && "SCEV already expanded");
    SCEVToExpansion[S] = V;
  }

  /// \return The block corresponding to the original preheader.
  VPBasicBlock *getPreheader() { return Preheader; }
  const VPBasicBlock *getPreheader() const { return Preheader; }

  /// Clone the current VPlan, update all VPValues of the new VPlan and cloned
  /// recipes to refer to the clones, and return it.
  VPlan *duplicate();
};

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
/// VPlanPrinter prints a given VPlan to a given output stream. The printing is
/// indented and follows the dot format.
class VPlanPrinter {
  raw_ostream &OS;
  const VPlan &Plan;
  unsigned Depth = 0;
  unsigned TabWidth = 2;
  std::string Indent;
  unsigned BID = 0;
  SmallDenseMap<const VPBlockBase *, unsigned> BlockID;

  VPSlotTracker SlotTracker;

  /// Handle indentation.
  void bumpIndent(int b) { Indent = std::string((Depth += b) * TabWidth, ' '); }

  /// Print a given \p Block of the Plan.
  void dumpBlock(const VPBlockBase *Block);

  /// Print the information related to the CFG edges going out of a given
  /// \p Block, followed by printing the successor blocks themselves.
  void dumpEdges(const VPBlockBase *Block);

  /// Print a given \p BasicBlock, including its VPRecipes, followed by printing
  /// its successor blocks.
  void dumpBasicBlock(const VPBasicBlock *BasicBlock);

  /// Print a given \p Region of the Plan.
  void dumpRegion(const VPRegionBlock *Region);

  unsigned getOrCreateBID(const VPBlockBase *Block) {
    return BlockID.count(Block) ? BlockID[Block] : BlockID[Block] = BID++;
  }

  Twine getOrCreateName(const VPBlockBase *Block);

  Twine getUID(const VPBlockBase *Block);

  /// Print the information related to a CFG edge between two VPBlockBases.
  void drawEdge(const VPBlockBase *From, const VPBlockBase *To, bool Hidden,
                const Twine &Label);

public:
  VPlanPrinter(raw_ostream &O, const VPlan &P)
      : OS(O), Plan(P), SlotTracker(&P) {}

  LLVM_DUMP_METHOD void dump();
};

struct VPlanIngredient {
  const Value *V;

  VPlanIngredient(const Value *V) : V(V) {}

  void print(raw_ostream &O) const;
};

inline raw_ostream &operator<<(raw_ostream &OS, const VPlanIngredient &I) {
  I.print(OS);
  return OS;
}

inline raw_ostream &operator<<(raw_ostream &OS, const VPlan &Plan) {
  Plan.print(OS);
  return OS;
}
#endif

//===----------------------------------------------------------------------===//
// VPlan Utilities
//===----------------------------------------------------------------------===//

/// Class that provides utilities for VPBlockBases in VPlan.
class VPBlockUtils {
public:
  VPBlockUtils() = delete;

  /// Insert disconnected VPBlockBase \p NewBlock after \p BlockPtr. Add \p
  /// NewBlock as successor of \p BlockPtr and \p BlockPtr as predecessor of \p
  /// NewBlock, and propagate \p BlockPtr parent to \p NewBlock. \p BlockPtr's
  /// successors are moved from \p BlockPtr to \p NewBlock. \p NewBlock must
  /// have neither successors nor predecessors.
  static void insertBlockAfter(VPBlockBase *NewBlock, VPBlockBase *BlockPtr) {
    assert(NewBlock->getSuccessors().empty() &&
           NewBlock->getPredecessors().empty() &&
           "Can't insert new block with predecessors or successors.");
    NewBlock->setParent(BlockPtr->getParent());
    SmallVector<VPBlockBase *> Succs(BlockPtr->successors());
    for (VPBlockBase *Succ : Succs) {
      disconnectBlocks(BlockPtr, Succ);
      connectBlocks(NewBlock, Succ);
    }
    connectBlocks(BlockPtr, NewBlock);
  }

  /// Insert disconnected VPBlockBases \p IfTrue and \p IfFalse after \p
  /// BlockPtr. Add \p IfTrue and \p IfFalse as succesors of \p BlockPtr and \p
  /// BlockPtr as predecessor of \p IfTrue and \p IfFalse. Propagate \p BlockPtr
  /// parent to \p IfTrue and \p IfFalse. \p BlockPtr must have no successors
  /// and \p IfTrue and \p IfFalse must have neither successors nor
  /// predecessors.
  static void insertTwoBlocksAfter(VPBlockBase *IfTrue, VPBlockBase *IfFalse,
                                   VPBlockBase *BlockPtr) {
    assert(IfTrue->getSuccessors().empty() &&
           "Can't insert IfTrue with successors.");
    assert(IfFalse->getSuccessors().empty() &&
           "Can't insert IfFalse with successors.");
    BlockPtr->setTwoSuccessors(IfTrue, IfFalse);
    IfTrue->setPredecessors({BlockPtr});
    IfFalse->setPredecessors({BlockPtr});
    IfTrue->setParent(BlockPtr->getParent());
    IfFalse->setParent(BlockPtr->getParent());
  }

  /// Connect VPBlockBases \p From and \p To bi-directionally. Append \p To to
  /// the successors of \p From and \p From to the predecessors of \p To. Both
  /// VPBlockBases must have the same parent, which can be null. Both
  /// VPBlockBases can be already connected to other VPBlockBases.
  static void connectBlocks(VPBlockBase *From, VPBlockBase *To) {
    assert((From->getParent() == To->getParent()) &&
           "Can't connect two block with different parents");
    assert(From->getNumSuccessors() < 2 &&
           "Blocks can't have more than two successors.");
    From->appendSuccessor(To);
    To->appendPredecessor(From);
  }

  /// Disconnect VPBlockBases \p From and \p To bi-directionally. Remove \p To
  /// from the successors of \p From and \p From from the predecessors of \p To.
  static void disconnectBlocks(VPBlockBase *From, VPBlockBase *To) {
    assert(To && "Successor to disconnect is null.");
    From->removeSuccessor(To);
    To->removePredecessor(From);
  }

  /// Return an iterator range over \p Range which only includes \p BlockTy
  /// blocks. The accesses are casted to \p BlockTy.
  template <typename BlockTy, typename T>
  static auto blocksOnly(const T &Range) {
    // Create BaseTy with correct const-ness based on BlockTy.
    using BaseTy = std::conditional_t<std::is_const<BlockTy>::value,
                                      const VPBlockBase, VPBlockBase>;

    // We need to first create an iterator range over (const) BlocktTy & instead
    // of (const) BlockTy * for filter_range to work properly.
    auto Mapped =
        map_range(Range, [](BaseTy *Block) -> BaseTy & { return *Block; });
    auto Filter = make_filter_range(
        Mapped, [](BaseTy &Block) { return isa<BlockTy>(&Block); });
    return map_range(Filter, [](BaseTy &Block) -> BlockTy * {
      return cast<BlockTy>(&Block);
    });
  }
};

class VPInterleavedAccessInfo {
  DenseMap<VPInstruction *, InterleaveGroup<VPInstruction> *>
      InterleaveGroupMap;

  /// Type for mapping of instruction based interleave groups to VPInstruction
  /// interleave groups
  using Old2NewTy = DenseMap<InterleaveGroup<Instruction> *,
                             InterleaveGroup<VPInstruction> *>;

  /// Recursively \p Region and populate VPlan based interleave groups based on
  /// \p IAI.
  void visitRegion(VPRegionBlock *Region, Old2NewTy &Old2New,
                   InterleavedAccessInfo &IAI);
  /// Recursively traverse \p Block and populate VPlan based interleave groups
  /// based on \p IAI.
  void visitBlock(VPBlockBase *Block, Old2NewTy &Old2New,
                  InterleavedAccessInfo &IAI);

public:
  VPInterleavedAccessInfo(VPlan &Plan, InterleavedAccessInfo &IAI);

  ~VPInterleavedAccessInfo() {
    SmallPtrSet<InterleaveGroup<VPInstruction> *, 4> DelSet;
    // Avoid releasing a pointer twice.
    for (auto &I : InterleaveGroupMap)
      DelSet.insert(I.second);
    for (auto *Ptr : DelSet)
      delete Ptr;
  }

  /// Get the interleave group that \p Instr belongs to.
  ///
  /// \returns nullptr if doesn't have such group.
  InterleaveGroup<VPInstruction> *
  getInterleaveGroup(VPInstruction *Instr) const {
    return InterleaveGroupMap.lookup(Instr);
  }
};

/// Class that maps (parts of) an existing VPlan to trees of combined
/// VPInstructions.
class VPlanSlp {
  enum class OpMode { Failed, Load, Opcode };

  /// A DenseMapInfo implementation for using SmallVector<VPValue *, 4> as
  /// DenseMap keys.
  struct BundleDenseMapInfo {
    static SmallVector<VPValue *, 4> getEmptyKey() {
      return {reinterpret_cast<VPValue *>(-1)};
    }

    static SmallVector<VPValue *, 4> getTombstoneKey() {
      return {reinterpret_cast<VPValue *>(-2)};
    }

    static unsigned getHashValue(const SmallVector<VPValue *, 4> &V) {
      return static_cast<unsigned>(hash_combine_range(V.begin(), V.end()));
    }

    static bool isEqual(const SmallVector<VPValue *, 4> &LHS,
                        const SmallVector<VPValue *, 4> &RHS) {
      return LHS == RHS;
    }
  };

  /// Mapping of values in the original VPlan to a combined VPInstruction.
  DenseMap<SmallVector<VPValue *, 4>, VPInstruction *, BundleDenseMapInfo>
      BundleToCombined;

  VPInterleavedAccessInfo &IAI;

  /// Basic block to operate on. For now, only instructions in a single BB are
  /// considered.
  const VPBasicBlock &BB;

  /// Indicates whether we managed to combine all visited instructions or not.
  bool CompletelySLP = true;

  /// Width of the widest combined bundle in bits.
  unsigned WidestBundleBits = 0;

  using MultiNodeOpTy =
      typename std::pair<VPInstruction *, SmallVector<VPValue *, 4>>;

  // Input operand bundles for the current multi node. Each multi node operand
  // bundle contains values not matching the multi node's opcode. They will
  // be reordered in reorderMultiNodeOps, once we completed building a
  // multi node.
  SmallVector<MultiNodeOpTy, 4> MultiNodeOps;

  /// Indicates whether we are building a multi node currently.
  bool MultiNodeActive = false;

  /// Check if we can vectorize Operands together.
  bool areVectorizable(ArrayRef<VPValue *> Operands) const;

  /// Add combined instruction \p New for the bundle \p Operands.
  void addCombined(ArrayRef<VPValue *> Operands, VPInstruction *New);

  /// Indicate we hit a bundle we failed to combine. Returns nullptr for now.
  VPInstruction *markFailed();

  /// Reorder operands in the multi node to maximize sequential memory access
  /// and commutative operations.
  SmallVector<MultiNodeOpTy, 4> reorderMultiNodeOps();

  /// Choose the best candidate to use for the lane after \p Last. The set of
  /// candidates to choose from are values with an opcode matching \p Last's
  /// or loads consecutive to \p Last.
  std::pair<OpMode, VPValue *> getBest(OpMode Mode, VPValue *Last,
                                       SmallPtrSetImpl<VPValue *> &Candidates,
                                       VPInterleavedAccessInfo &IAI);

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print bundle \p Values to dbgs().
  void dumpBundle(ArrayRef<VPValue *> Values);
#endif

public:
  VPlanSlp(VPInterleavedAccessInfo &IAI, VPBasicBlock &BB) : IAI(IAI), BB(BB) {}

  ~VPlanSlp() = default;

  /// Tries to build an SLP tree rooted at \p Operands and returns a
  /// VPInstruction combining \p Operands, if they can be combined.
  VPInstruction *buildGraph(ArrayRef<VPValue *> Operands);

  /// Return the width of the widest combined bundle in bits.
  unsigned getWidestBundleBits() const { return WidestBundleBits; }

  /// Return true if all visited instruction can be combined.
  bool isCompletelySLP() const { return CompletelySLP; }
};

namespace vputils {

/// Returns true if only the first lane of \p Def is used.
bool onlyFirstLaneUsed(const VPValue *Def);

/// Returns true if only the first part of \p Def is used.
bool onlyFirstPartUsed(const VPValue *Def);

/// Get or create a VPValue that corresponds to the expansion of \p Expr. If \p
/// Expr is a SCEVConstant or SCEVUnknown, return a VPValue wrapping the live-in
/// value. Otherwise return a VPExpandSCEVRecipe to expand \p Expr. If \p Plan's
/// pre-header already contains a recipe expanding \p Expr, return it. If not,
/// create a new one.
VPValue *getOrCreateVPValueForSCEVExpr(VPlan &Plan, const SCEV *Expr,
                                       ScalarEvolution &SE);

/// Returns true if \p VPV is uniform after vectorization.
inline bool isUniformAfterVectorization(VPValue *VPV) {
  // A value defined outside the vector region must be uniform after
  // vectorization inside a vector region.
  if (VPV->isDefinedOutsideVectorRegions())
    return true;
  VPRecipeBase *Def = VPV->getDefiningRecipe();
  assert(Def && "Must have definition for value defined inside vector region");
  if (auto Rep = dyn_cast<VPReplicateRecipe>(Def))
    return Rep->isUniform();
  if (auto *GEP = dyn_cast<VPWidenGEPRecipe>(Def))
    return all_of(GEP->operands(), isUniformAfterVectorization);
  if (auto *VPI = dyn_cast<VPInstruction>(Def))
    return VPI->isSingleScalar() || VPI->isVectorToScalar();
  return false;
}

/// Return true if \p V is a header mask in \p Plan.
bool isHeaderMask(VPValue *V, VPlan &Plan);
} // end namespace vputils

} // end namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_VPLAN_H
