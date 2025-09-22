//===- BlockFrequencyImplInfo.cpp - Block Frequency Info Implementation ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Loops should be simplified before this analysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/BlockFrequencyInfoImpl.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ScaledNumber.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::bfi_detail;

#define DEBUG_TYPE "block-freq"

namespace llvm {
cl::opt<bool> CheckBFIUnknownBlockQueries(
    "check-bfi-unknown-block-queries",
    cl::init(false), cl::Hidden,
    cl::desc("Check if block frequency is queried for an unknown block "
             "for debugging missed BFI updates"));

cl::opt<bool> UseIterativeBFIInference(
    "use-iterative-bfi-inference", cl::Hidden,
    cl::desc("Apply an iterative post-processing to infer correct BFI counts"));

cl::opt<unsigned> IterativeBFIMaxIterationsPerBlock(
    "iterative-bfi-max-iterations-per-block", cl::init(1000), cl::Hidden,
    cl::desc("Iterative inference: maximum number of update iterations "
             "per block"));

cl::opt<double> IterativeBFIPrecision(
    "iterative-bfi-precision", cl::init(1e-12), cl::Hidden,
    cl::desc("Iterative inference: delta convergence precision; smaller values "
             "typically lead to better results at the cost of worsen runtime"));
} // namespace llvm

ScaledNumber<uint64_t> BlockMass::toScaled() const {
  if (isFull())
    return ScaledNumber<uint64_t>(1, 0);
  return ScaledNumber<uint64_t>(getMass() + 1, -64);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void BlockMass::dump() const { print(dbgs()); }
#endif

static char getHexDigit(int N) {
  assert(N < 16);
  if (N < 10)
    return '0' + N;
  return 'a' + N - 10;
}

raw_ostream &BlockMass::print(raw_ostream &OS) const {
  for (int Digits = 0; Digits < 16; ++Digits)
    OS << getHexDigit(Mass >> (60 - Digits * 4) & 0xf);
  return OS;
}

namespace {

using BlockNode = BlockFrequencyInfoImplBase::BlockNode;
using Distribution = BlockFrequencyInfoImplBase::Distribution;
using WeightList = BlockFrequencyInfoImplBase::Distribution::WeightList;
using Scaled64 = BlockFrequencyInfoImplBase::Scaled64;
using LoopData = BlockFrequencyInfoImplBase::LoopData;
using Weight = BlockFrequencyInfoImplBase::Weight;
using FrequencyData = BlockFrequencyInfoImplBase::FrequencyData;

/// Dithering mass distributer.
///
/// This class splits up a single mass into portions by weight, dithering to
/// spread out error.  No mass is lost.  The dithering precision depends on the
/// precision of the product of \a BlockMass and \a BranchProbability.
///
/// The distribution algorithm follows.
///
///  1. Initialize by saving the sum of the weights in \a RemWeight and the
///     mass to distribute in \a RemMass.
///
///  2. For each portion:
///
///      1. Construct a branch probability, P, as the portion's weight divided
///         by the current value of \a RemWeight.
///      2. Calculate the portion's mass as \a RemMass times P.
///      3. Update \a RemWeight and \a RemMass at each portion by subtracting
///         the current portion's weight and mass.
struct DitheringDistributer {
  uint32_t RemWeight;
  BlockMass RemMass;

  DitheringDistributer(Distribution &Dist, const BlockMass &Mass);

  BlockMass takeMass(uint32_t Weight);
};

} // end anonymous namespace

DitheringDistributer::DitheringDistributer(Distribution &Dist,
                                           const BlockMass &Mass) {
  Dist.normalize();
  RemWeight = Dist.Total;
  RemMass = Mass;
}

BlockMass DitheringDistributer::takeMass(uint32_t Weight) {
  assert(Weight && "invalid weight");
  assert(Weight <= RemWeight);
  BlockMass Mass = RemMass * BranchProbability(Weight, RemWeight);

  // Decrement totals (dither).
  RemWeight -= Weight;
  RemMass -= Mass;
  return Mass;
}

void Distribution::add(const BlockNode &Node, uint64_t Amount,
                       Weight::DistType Type) {
  assert(Amount && "invalid weight of 0");
  uint64_t NewTotal = Total + Amount;

  // Check for overflow.  It should be impossible to overflow twice.
  bool IsOverflow = NewTotal < Total;
  assert(!(DidOverflow && IsOverflow) && "unexpected repeated overflow");
  DidOverflow |= IsOverflow;

  // Update the total.
  Total = NewTotal;

  // Save the weight.
  Weights.push_back(Weight(Type, Node, Amount));
}

static void combineWeight(Weight &W, const Weight &OtherW) {
  assert(OtherW.TargetNode.isValid());
  if (!W.Amount) {
    W = OtherW;
    return;
  }
  assert(W.Type == OtherW.Type);
  assert(W.TargetNode == OtherW.TargetNode);
  assert(OtherW.Amount && "Expected non-zero weight");
  if (W.Amount > W.Amount + OtherW.Amount)
    // Saturate on overflow.
    W.Amount = UINT64_MAX;
  else
    W.Amount += OtherW.Amount;
}

static void combineWeightsBySorting(WeightList &Weights) {
  // Sort so edges to the same node are adjacent.
  llvm::sort(Weights, [](const Weight &L, const Weight &R) {
    return L.TargetNode < R.TargetNode;
  });

  // Combine adjacent edges.
  WeightList::iterator O = Weights.begin();
  for (WeightList::const_iterator I = O, L = O, E = Weights.end(); I != E;
       ++O, (I = L)) {
    *O = *I;

    // Find the adjacent weights to the same node.
    for (++L; L != E && I->TargetNode == L->TargetNode; ++L)
      combineWeight(*O, *L);
  }

  // Erase extra entries.
  Weights.erase(O, Weights.end());
}

static void combineWeightsByHashing(WeightList &Weights) {
  // Collect weights into a DenseMap.
  using HashTable = DenseMap<BlockNode::IndexType, Weight>;

  HashTable Combined(NextPowerOf2(2 * Weights.size()));
  for (const Weight &W : Weights)
    combineWeight(Combined[W.TargetNode.Index], W);

  // Check whether anything changed.
  if (Weights.size() == Combined.size())
    return;

  // Fill in the new weights.
  Weights.clear();
  Weights.reserve(Combined.size());
  for (const auto &I : Combined)
    Weights.push_back(I.second);
}

static void combineWeights(WeightList &Weights) {
  // Use a hash table for many successors to keep this linear.
  if (Weights.size() > 128) {
    combineWeightsByHashing(Weights);
    return;
  }

  combineWeightsBySorting(Weights);
}

static uint64_t shiftRightAndRound(uint64_t N, int Shift) {
  assert(Shift >= 0);
  assert(Shift < 64);
  if (!Shift)
    return N;
  return (N >> Shift) + (UINT64_C(1) & N >> (Shift - 1));
}

void Distribution::normalize() {
  // Early exit for termination nodes.
  if (Weights.empty())
    return;

  // Only bother if there are multiple successors.
  if (Weights.size() > 1)
    combineWeights(Weights);

  // Early exit when combined into a single successor.
  if (Weights.size() == 1) {
    Total = 1;
    Weights.front().Amount = 1;
    return;
  }

  // Determine how much to shift right so that the total fits into 32-bits.
  //
  // If we shift at all, shift by 1 extra.  Otherwise, the lower limit of 1
  // for each weight can cause a 32-bit overflow.
  int Shift = 0;
  if (DidOverflow)
    Shift = 33;
  else if (Total > UINT32_MAX)
    Shift = 33 - llvm::countl_zero(Total);

  // Early exit if nothing needs to be scaled.
  if (!Shift) {
    // If we didn't overflow then combineWeights() shouldn't have changed the
    // sum of the weights, but let's double-check.
    assert(Total == std::accumulate(Weights.begin(), Weights.end(), UINT64_C(0),
                                    [](uint64_t Sum, const Weight &W) {
                      return Sum + W.Amount;
                    }) &&
           "Expected total to be correct");
    return;
  }

  // Recompute the total through accumulation (rather than shifting it) so that
  // it's accurate after shifting and any changes combineWeights() made above.
  Total = 0;

  // Sum the weights to each node and shift right if necessary.
  for (Weight &W : Weights) {
    // Scale down below UINT32_MAX.  Since Shift is larger than necessary, we
    // can round here without concern about overflow.
    assert(W.TargetNode.isValid());
    W.Amount = std::max(UINT64_C(1), shiftRightAndRound(W.Amount, Shift));
    assert(W.Amount <= UINT32_MAX);

    // Update the total.
    Total += W.Amount;
  }
  assert(Total <= UINT32_MAX);
}

void BlockFrequencyInfoImplBase::clear() {
  // Swap with a default-constructed std::vector, since std::vector<>::clear()
  // does not actually clear heap storage.
  std::vector<FrequencyData>().swap(Freqs);
  IsIrrLoopHeader.clear();
  std::vector<WorkingData>().swap(Working);
  Loops.clear();
}

/// Clear all memory not needed downstream.
///
/// Releases all memory not used downstream.  In particular, saves Freqs.
static void cleanup(BlockFrequencyInfoImplBase &BFI) {
  std::vector<FrequencyData> SavedFreqs(std::move(BFI.Freqs));
  SparseBitVector<> SavedIsIrrLoopHeader(std::move(BFI.IsIrrLoopHeader));
  BFI.clear();
  BFI.Freqs = std::move(SavedFreqs);
  BFI.IsIrrLoopHeader = std::move(SavedIsIrrLoopHeader);
}

bool BlockFrequencyInfoImplBase::addToDist(Distribution &Dist,
                                           const LoopData *OuterLoop,
                                           const BlockNode &Pred,
                                           const BlockNode &Succ,
                                           uint64_t Weight) {
  if (!Weight)
    Weight = 1;

  auto isLoopHeader = [&OuterLoop](const BlockNode &Node) {
    return OuterLoop && OuterLoop->isHeader(Node);
  };

  BlockNode Resolved = Working[Succ.Index].getResolvedNode();

#ifndef NDEBUG
  auto debugSuccessor = [&](const char *Type) {
    dbgs() << "  =>"
           << " [" << Type << "] weight = " << Weight;
    if (!isLoopHeader(Resolved))
      dbgs() << ", succ = " << getBlockName(Succ);
    if (Resolved != Succ)
      dbgs() << ", resolved = " << getBlockName(Resolved);
    dbgs() << "\n";
  };
  (void)debugSuccessor;
#endif

  if (isLoopHeader(Resolved)) {
    LLVM_DEBUG(debugSuccessor("backedge"));
    Dist.addBackedge(Resolved, Weight);
    return true;
  }

  if (Working[Resolved.Index].getContainingLoop() != OuterLoop) {
    LLVM_DEBUG(debugSuccessor("  exit  "));
    Dist.addExit(Resolved, Weight);
    return true;
  }

  if (Resolved < Pred) {
    if (!isLoopHeader(Pred)) {
      // If OuterLoop is an irreducible loop, we can't actually handle this.
      assert((!OuterLoop || !OuterLoop->isIrreducible()) &&
             "unhandled irreducible control flow");

      // Irreducible backedge.  Abort.
      LLVM_DEBUG(debugSuccessor("abort!!!"));
      return false;
    }

    // If "Pred" is a loop header, then this isn't really a backedge; rather,
    // OuterLoop must be irreducible.  These false backedges can come only from
    // secondary loop headers.
    assert(OuterLoop && OuterLoop->isIrreducible() && !isLoopHeader(Resolved) &&
           "unhandled irreducible control flow");
  }

  LLVM_DEBUG(debugSuccessor(" local  "));
  Dist.addLocal(Resolved, Weight);
  return true;
}

bool BlockFrequencyInfoImplBase::addLoopSuccessorsToDist(
    const LoopData *OuterLoop, LoopData &Loop, Distribution &Dist) {
  // Copy the exit map into Dist.
  for (const auto &I : Loop.Exits)
    if (!addToDist(Dist, OuterLoop, Loop.getHeader(), I.first,
                   I.second.getMass()))
      // Irreducible backedge.
      return false;

  return true;
}

/// Compute the loop scale for a loop.
void BlockFrequencyInfoImplBase::computeLoopScale(LoopData &Loop) {
  // Compute loop scale.
  LLVM_DEBUG(dbgs() << "compute-loop-scale: " << getLoopName(Loop) << "\n");

  // Infinite loops need special handling. If we give the back edge an infinite
  // mass, they may saturate all the other scales in the function down to 1,
  // making all the other region temperatures look exactly the same. Choose an
  // arbitrary scale to avoid these issues.
  //
  // FIXME: An alternate way would be to select a symbolic scale which is later
  // replaced to be the maximum of all computed scales plus 1. This would
  // appropriately describe the loop as having a large scale, without skewing
  // the final frequency computation.
  const Scaled64 InfiniteLoopScale(1, 12);

  // LoopScale == 1 / ExitMass
  // ExitMass == HeadMass - BackedgeMass
  BlockMass TotalBackedgeMass;
  for (auto &Mass : Loop.BackedgeMass)
    TotalBackedgeMass += Mass;
  BlockMass ExitMass = BlockMass::getFull() - TotalBackedgeMass;

  // Block scale stores the inverse of the scale. If this is an infinite loop,
  // its exit mass will be zero. In this case, use an arbitrary scale for the
  // loop scale.
  Loop.Scale =
      ExitMass.isEmpty() ? InfiniteLoopScale : ExitMass.toScaled().inverse();

  LLVM_DEBUG(dbgs() << " - exit-mass = " << ExitMass << " ("
                    << BlockMass::getFull() << " - " << TotalBackedgeMass
                    << ")\n"
                    << " - scale = " << Loop.Scale << "\n");
}

/// Package up a loop.
void BlockFrequencyInfoImplBase::packageLoop(LoopData &Loop) {
  LLVM_DEBUG(dbgs() << "packaging-loop: " << getLoopName(Loop) << "\n");

  // Clear the subloop exits to prevent quadratic memory usage.
  for (const BlockNode &M : Loop.Nodes) {
    if (auto *Loop = Working[M.Index].getPackagedLoop())
      Loop->Exits.clear();
    LLVM_DEBUG(dbgs() << " - node: " << getBlockName(M.Index) << "\n");
  }
  Loop.IsPackaged = true;
}

#ifndef NDEBUG
static void debugAssign(const BlockFrequencyInfoImplBase &BFI,
                        const DitheringDistributer &D, const BlockNode &T,
                        const BlockMass &M, const char *Desc) {
  dbgs() << "  => assign " << M << " (" << D.RemMass << ")";
  if (Desc)
    dbgs() << " [" << Desc << "]";
  if (T.isValid())
    dbgs() << " to " << BFI.getBlockName(T);
  dbgs() << "\n";
}
#endif

void BlockFrequencyInfoImplBase::distributeMass(const BlockNode &Source,
                                                LoopData *OuterLoop,
                                                Distribution &Dist) {
  BlockMass Mass = Working[Source.Index].getMass();
  LLVM_DEBUG(dbgs() << "  => mass:  " << Mass << "\n");

  // Distribute mass to successors as laid out in Dist.
  DitheringDistributer D(Dist, Mass);

  for (const Weight &W : Dist.Weights) {
    // Check for a local edge (non-backedge and non-exit).
    BlockMass Taken = D.takeMass(W.Amount);
    if (W.Type == Weight::Local) {
      Working[W.TargetNode.Index].getMass() += Taken;
      LLVM_DEBUG(debugAssign(*this, D, W.TargetNode, Taken, nullptr));
      continue;
    }

    // Backedges and exits only make sense if we're processing a loop.
    assert(OuterLoop && "backedge or exit outside of loop");

    // Check for a backedge.
    if (W.Type == Weight::Backedge) {
      OuterLoop->BackedgeMass[OuterLoop->getHeaderIndex(W.TargetNode)] += Taken;
      LLVM_DEBUG(debugAssign(*this, D, W.TargetNode, Taken, "back"));
      continue;
    }

    // This must be an exit.
    assert(W.Type == Weight::Exit);
    OuterLoop->Exits.push_back(std::make_pair(W.TargetNode, Taken));
    LLVM_DEBUG(debugAssign(*this, D, W.TargetNode, Taken, "exit"));
  }
}

static void convertFloatingToInteger(BlockFrequencyInfoImplBase &BFI,
                                     const Scaled64 &Min, const Scaled64 &Max) {
  // Scale the Factor to a size that creates integers.  If possible scale
  // integers so that Max == UINT64_MAX so that they can be best differentiated.
  // Is is possible that the range between min and max cannot be accurately
  // represented in a 64bit integer without either loosing precision for small
  // values (so small unequal numbers all map to 1) or saturaturing big numbers
  // loosing precision for big numbers (so unequal big numbers may map to
  // UINT64_MAX). We choose to loose precision for small numbers.
  const unsigned MaxBits = sizeof(Scaled64::DigitsType) * CHAR_BIT;
  // Users often add up multiple BlockFrequency values or multiply them with
  // things like instruction costs. Leave some room to avoid saturating
  // operations reaching UIN64_MAX too early.
  const unsigned Slack = 10;
  Scaled64 ScalingFactor = Scaled64(1, MaxBits - Slack) / Max;

  // Translate the floats to integers.
  LLVM_DEBUG(dbgs() << "float-to-int: min = " << Min << ", max = " << Max
                    << ", factor = " << ScalingFactor << "\n");
  (void)Min;
  for (size_t Index = 0; Index < BFI.Freqs.size(); ++Index) {
    Scaled64 Scaled = BFI.Freqs[Index].Scaled * ScalingFactor;
    BFI.Freqs[Index].Integer = std::max(UINT64_C(1), Scaled.toInt<uint64_t>());
    LLVM_DEBUG(dbgs() << " - " << BFI.getBlockName(Index) << ": float = "
                      << BFI.Freqs[Index].Scaled << ", scaled = " << Scaled
                      << ", int = " << BFI.Freqs[Index].Integer << "\n");
  }
}

/// Unwrap a loop package.
///
/// Visits all the members of a loop, adjusting their BlockData according to
/// the loop's pseudo-node.
static void unwrapLoop(BlockFrequencyInfoImplBase &BFI, LoopData &Loop) {
  LLVM_DEBUG(dbgs() << "unwrap-loop-package: " << BFI.getLoopName(Loop)
                    << ": mass = " << Loop.Mass << ", scale = " << Loop.Scale
                    << "\n");
  Loop.Scale *= Loop.Mass.toScaled();
  Loop.IsPackaged = false;
  LLVM_DEBUG(dbgs() << "  => combined-scale = " << Loop.Scale << "\n");

  // Propagate the head scale through the loop.  Since members are visited in
  // RPO, the head scale will be updated by the loop scale first, and then the
  // final head scale will be used for updated the rest of the members.
  for (const BlockNode &N : Loop.Nodes) {
    const auto &Working = BFI.Working[N.Index];
    Scaled64 &F = Working.isAPackage() ? Working.getPackagedLoop()->Scale
                                       : BFI.Freqs[N.Index].Scaled;
    Scaled64 New = Loop.Scale * F;
    LLVM_DEBUG(dbgs() << " - " << BFI.getBlockName(N) << ": " << F << " => "
                      << New << "\n");
    F = New;
  }
}

void BlockFrequencyInfoImplBase::unwrapLoops() {
  // Set initial frequencies from loop-local masses.
  for (size_t Index = 0; Index < Working.size(); ++Index)
    Freqs[Index].Scaled = Working[Index].Mass.toScaled();

  for (LoopData &Loop : Loops)
    unwrapLoop(*this, Loop);
}

void BlockFrequencyInfoImplBase::finalizeMetrics() {
  // Unwrap loop packages in reverse post-order, tracking min and max
  // frequencies.
  auto Min = Scaled64::getLargest();
  auto Max = Scaled64::getZero();
  for (size_t Index = 0; Index < Working.size(); ++Index) {
    // Update min/max scale.
    Min = std::min(Min, Freqs[Index].Scaled);
    Max = std::max(Max, Freqs[Index].Scaled);
  }

  // Convert to integers.
  convertFloatingToInteger(*this, Min, Max);

  // Clean up data structures.
  cleanup(*this);

  // Print out the final stats.
  LLVM_DEBUG(dump());
}

BlockFrequency
BlockFrequencyInfoImplBase::getBlockFreq(const BlockNode &Node) const {
  if (!Node.isValid()) {
#ifndef NDEBUG
    if (CheckBFIUnknownBlockQueries) {
      SmallString<256> Msg;
      raw_svector_ostream OS(Msg);
      OS << "*** Detected BFI query for unknown block " << getBlockName(Node);
      report_fatal_error(OS.str());
    }
#endif
    return BlockFrequency(0);
  }
  return BlockFrequency(Freqs[Node.Index].Integer);
}

std::optional<uint64_t>
BlockFrequencyInfoImplBase::getBlockProfileCount(const Function &F,
                                                 const BlockNode &Node,
                                                 bool AllowSynthetic) const {
  return getProfileCountFromFreq(F, getBlockFreq(Node), AllowSynthetic);
}

std::optional<uint64_t> BlockFrequencyInfoImplBase::getProfileCountFromFreq(
    const Function &F, BlockFrequency Freq, bool AllowSynthetic) const {
  auto EntryCount = F.getEntryCount(AllowSynthetic);
  if (!EntryCount)
    return std::nullopt;
  // Use 128 bit APInt to do the arithmetic to avoid overflow.
  APInt BlockCount(128, EntryCount->getCount());
  APInt BlockFreq(128, Freq.getFrequency());
  APInt EntryFreq(128, getEntryFreq().getFrequency());
  BlockCount *= BlockFreq;
  // Rounded division of BlockCount by EntryFreq. Since EntryFreq is unsigned
  // lshr by 1 gives EntryFreq/2.
  BlockCount = (BlockCount + EntryFreq.lshr(1)).udiv(EntryFreq);
  return BlockCount.getLimitedValue();
}

bool
BlockFrequencyInfoImplBase::isIrrLoopHeader(const BlockNode &Node) {
  if (!Node.isValid())
    return false;
  return IsIrrLoopHeader.test(Node.Index);
}

Scaled64
BlockFrequencyInfoImplBase::getFloatingBlockFreq(const BlockNode &Node) const {
  if (!Node.isValid())
    return Scaled64::getZero();
  return Freqs[Node.Index].Scaled;
}

void BlockFrequencyInfoImplBase::setBlockFreq(const BlockNode &Node,
                                              BlockFrequency Freq) {
  assert(Node.isValid() && "Expected valid node");
  assert(Node.Index < Freqs.size() && "Expected legal index");
  Freqs[Node.Index].Integer = Freq.getFrequency();
}

std::string
BlockFrequencyInfoImplBase::getBlockName(const BlockNode &Node) const {
  return {};
}

std::string
BlockFrequencyInfoImplBase::getLoopName(const LoopData &Loop) const {
  return getBlockName(Loop.getHeader()) + (Loop.isIrreducible() ? "**" : "*");
}

void IrreducibleGraph::addNodesInLoop(const BFIBase::LoopData &OuterLoop) {
  Start = OuterLoop.getHeader();
  Nodes.reserve(OuterLoop.Nodes.size());
  for (auto N : OuterLoop.Nodes)
    addNode(N);
  indexNodes();
}

void IrreducibleGraph::addNodesInFunction() {
  Start = 0;
  for (uint32_t Index = 0; Index < BFI.Working.size(); ++Index)
    if (!BFI.Working[Index].isPackaged())
      addNode(Index);
  indexNodes();
}

void IrreducibleGraph::indexNodes() {
  for (auto &I : Nodes)
    Lookup[I.Node.Index] = &I;
}

void IrreducibleGraph::addEdge(IrrNode &Irr, const BlockNode &Succ,
                               const BFIBase::LoopData *OuterLoop) {
  if (OuterLoop && OuterLoop->isHeader(Succ))
    return;
  auto L = Lookup.find(Succ.Index);
  if (L == Lookup.end())
    return;
  IrrNode &SuccIrr = *L->second;
  Irr.Edges.push_back(&SuccIrr);
  SuccIrr.Edges.push_front(&Irr);
  ++SuccIrr.NumIn;
}

namespace llvm {

template <> struct GraphTraits<IrreducibleGraph> {
  using GraphT = bfi_detail::IrreducibleGraph;
  using NodeRef = const GraphT::IrrNode *;
  using ChildIteratorType = GraphT::IrrNode::iterator;

  static NodeRef getEntryNode(const GraphT &G) { return G.StartIrr; }
  static ChildIteratorType child_begin(NodeRef N) { return N->succ_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->succ_end(); }
};

} // end namespace llvm

/// Find extra irreducible headers.
///
/// Find entry blocks and other blocks with backedges, which exist when \c G
/// contains irreducible sub-SCCs.
static void findIrreducibleHeaders(
    const BlockFrequencyInfoImplBase &BFI,
    const IrreducibleGraph &G,
    const std::vector<const IrreducibleGraph::IrrNode *> &SCC,
    LoopData::NodeList &Headers, LoopData::NodeList &Others) {
  // Map from nodes in the SCC to whether it's an entry block.
  SmallDenseMap<const IrreducibleGraph::IrrNode *, bool, 8> InSCC;

  // InSCC also acts the set of nodes in the graph.  Seed it.
  for (const auto *I : SCC)
    InSCC[I] = false;

  for (auto I = InSCC.begin(), E = InSCC.end(); I != E; ++I) {
    auto &Irr = *I->first;
    for (const auto *P : make_range(Irr.pred_begin(), Irr.pred_end())) {
      if (InSCC.count(P))
        continue;

      // This is an entry block.
      I->second = true;
      Headers.push_back(Irr.Node);
      LLVM_DEBUG(dbgs() << "  => entry = " << BFI.getBlockName(Irr.Node)
                        << "\n");
      break;
    }
  }
  assert(Headers.size() >= 2 &&
         "Expected irreducible CFG; -loop-info is likely invalid");
  if (Headers.size() == InSCC.size()) {
    // Every block is a header.
    llvm::sort(Headers);
    return;
  }

  // Look for extra headers from irreducible sub-SCCs.
  for (const auto &I : InSCC) {
    // Entry blocks are already headers.
    if (I.second)
      continue;

    auto &Irr = *I.first;
    for (const auto *P : make_range(Irr.pred_begin(), Irr.pred_end())) {
      // Skip forward edges.
      if (P->Node < Irr.Node)
        continue;

      // Skip predecessors from entry blocks.  These can have inverted
      // ordering.
      if (InSCC.lookup(P))
        continue;

      // Store the extra header.
      Headers.push_back(Irr.Node);
      LLVM_DEBUG(dbgs() << "  => extra = " << BFI.getBlockName(Irr.Node)
                        << "\n");
      break;
    }
    if (Headers.back() == Irr.Node)
      // Added this as a header.
      continue;

    // This is not a header.
    Others.push_back(Irr.Node);
    LLVM_DEBUG(dbgs() << "  => other = " << BFI.getBlockName(Irr.Node) << "\n");
  }
  llvm::sort(Headers);
  llvm::sort(Others);
}

static void createIrreducibleLoop(
    BlockFrequencyInfoImplBase &BFI, const IrreducibleGraph &G,
    LoopData *OuterLoop, std::list<LoopData>::iterator Insert,
    const std::vector<const IrreducibleGraph::IrrNode *> &SCC) {
  // Translate the SCC into RPO.
  LLVM_DEBUG(dbgs() << " - found-scc\n");

  LoopData::NodeList Headers;
  LoopData::NodeList Others;
  findIrreducibleHeaders(BFI, G, SCC, Headers, Others);

  auto Loop = BFI.Loops.emplace(Insert, OuterLoop, Headers.begin(),
                                Headers.end(), Others.begin(), Others.end());

  // Update loop hierarchy.
  for (const auto &N : Loop->Nodes)
    if (BFI.Working[N.Index].isLoopHeader())
      BFI.Working[N.Index].Loop->Parent = &*Loop;
    else
      BFI.Working[N.Index].Loop = &*Loop;
}

iterator_range<std::list<LoopData>::iterator>
BlockFrequencyInfoImplBase::analyzeIrreducible(
    const IrreducibleGraph &G, LoopData *OuterLoop,
    std::list<LoopData>::iterator Insert) {
  assert((OuterLoop == nullptr) == (Insert == Loops.begin()));
  auto Prev = OuterLoop ? std::prev(Insert) : Loops.end();

  for (auto I = scc_begin(G); !I.isAtEnd(); ++I) {
    if (I->size() < 2)
      continue;

    // Translate the SCC into RPO.
    createIrreducibleLoop(*this, G, OuterLoop, Insert, *I);
  }

  if (OuterLoop)
    return make_range(std::next(Prev), Insert);
  return make_range(Loops.begin(), Insert);
}

void
BlockFrequencyInfoImplBase::updateLoopWithIrreducible(LoopData &OuterLoop) {
  OuterLoop.Exits.clear();
  for (auto &Mass : OuterLoop.BackedgeMass)
    Mass = BlockMass::getEmpty();
  auto O = OuterLoop.Nodes.begin() + 1;
  for (auto I = O, E = OuterLoop.Nodes.end(); I != E; ++I)
    if (!Working[I->Index].isPackaged())
      *O++ = *I;
  OuterLoop.Nodes.erase(O, OuterLoop.Nodes.end());
}

void BlockFrequencyInfoImplBase::adjustLoopHeaderMass(LoopData &Loop) {
  assert(Loop.isIrreducible() && "this only makes sense on irreducible loops");

  // Since the loop has more than one header block, the mass flowing back into
  // each header will be different. Adjust the mass in each header loop to
  // reflect the masses flowing through back edges.
  //
  // To do this, we distribute the initial mass using the backedge masses
  // as weights for the distribution.
  BlockMass LoopMass = BlockMass::getFull();
  Distribution Dist;

  LLVM_DEBUG(dbgs() << "adjust-loop-header-mass:\n");
  for (uint32_t H = 0; H < Loop.NumHeaders; ++H) {
    auto &HeaderNode = Loop.Nodes[H];
    auto &BackedgeMass = Loop.BackedgeMass[Loop.getHeaderIndex(HeaderNode)];
    LLVM_DEBUG(dbgs() << " - Add back edge mass for node "
                      << getBlockName(HeaderNode) << ": " << BackedgeMass
                      << "\n");
    if (BackedgeMass.getMass() > 0)
      Dist.addLocal(HeaderNode, BackedgeMass.getMass());
    else
      LLVM_DEBUG(dbgs() << "   Nothing added. Back edge mass is zero\n");
  }

  DitheringDistributer D(Dist, LoopMass);

  LLVM_DEBUG(dbgs() << " Distribute loop mass " << LoopMass
                    << " to headers using above weights\n");
  for (const Weight &W : Dist.Weights) {
    BlockMass Taken = D.takeMass(W.Amount);
    assert(W.Type == Weight::Local && "all weights should be local");
    Working[W.TargetNode.Index].getMass() = Taken;
    LLVM_DEBUG(debugAssign(*this, D, W.TargetNode, Taken, nullptr));
  }
}

void BlockFrequencyInfoImplBase::distributeIrrLoopHeaderMass(Distribution &Dist) {
  BlockMass LoopMass = BlockMass::getFull();
  DitheringDistributer D(Dist, LoopMass);
  for (const Weight &W : Dist.Weights) {
    BlockMass Taken = D.takeMass(W.Amount);
    assert(W.Type == Weight::Local && "all weights should be local");
    Working[W.TargetNode.Index].getMass() = Taken;
    LLVM_DEBUG(debugAssign(*this, D, W.TargetNode, Taken, nullptr));
  }
}
