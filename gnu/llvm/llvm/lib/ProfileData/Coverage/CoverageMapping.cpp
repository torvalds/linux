//===- CoverageMapping.cpp - Code coverage mapping support ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for clang's and llvm's instrumentation based
// code coverage.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/BuildID.h"
#include "llvm/ProfileData/Coverage/CoverageMappingReader.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;
using namespace coverage;

#define DEBUG_TYPE "coverage-mapping"

Counter CounterExpressionBuilder::get(const CounterExpression &E) {
  auto It = ExpressionIndices.find(E);
  if (It != ExpressionIndices.end())
    return Counter::getExpression(It->second);
  unsigned I = Expressions.size();
  Expressions.push_back(E);
  ExpressionIndices[E] = I;
  return Counter::getExpression(I);
}

void CounterExpressionBuilder::extractTerms(Counter C, int Factor,
                                            SmallVectorImpl<Term> &Terms) {
  switch (C.getKind()) {
  case Counter::Zero:
    break;
  case Counter::CounterValueReference:
    Terms.emplace_back(C.getCounterID(), Factor);
    break;
  case Counter::Expression:
    const auto &E = Expressions[C.getExpressionID()];
    extractTerms(E.LHS, Factor, Terms);
    extractTerms(
        E.RHS, E.Kind == CounterExpression::Subtract ? -Factor : Factor, Terms);
    break;
  }
}

Counter CounterExpressionBuilder::simplify(Counter ExpressionTree) {
  // Gather constant terms.
  SmallVector<Term, 32> Terms;
  extractTerms(ExpressionTree, +1, Terms);

  // If there are no terms, this is just a zero. The algorithm below assumes at
  // least one term.
  if (Terms.size() == 0)
    return Counter::getZero();

  // Group the terms by counter ID.
  llvm::sort(Terms, [](const Term &LHS, const Term &RHS) {
    return LHS.CounterID < RHS.CounterID;
  });

  // Combine terms by counter ID to eliminate counters that sum to zero.
  auto Prev = Terms.begin();
  for (auto I = Prev + 1, E = Terms.end(); I != E; ++I) {
    if (I->CounterID == Prev->CounterID) {
      Prev->Factor += I->Factor;
      continue;
    }
    ++Prev;
    *Prev = *I;
  }
  Terms.erase(++Prev, Terms.end());

  Counter C;
  // Create additions. We do this before subtractions to avoid constructs like
  // ((0 - X) + Y), as opposed to (Y - X).
  for (auto T : Terms) {
    if (T.Factor <= 0)
      continue;
    for (int I = 0; I < T.Factor; ++I)
      if (C.isZero())
        C = Counter::getCounter(T.CounterID);
      else
        C = get(CounterExpression(CounterExpression::Add, C,
                                  Counter::getCounter(T.CounterID)));
  }

  // Create subtractions.
  for (auto T : Terms) {
    if (T.Factor >= 0)
      continue;
    for (int I = 0; I < -T.Factor; ++I)
      C = get(CounterExpression(CounterExpression::Subtract, C,
                                Counter::getCounter(T.CounterID)));
  }
  return C;
}

Counter CounterExpressionBuilder::add(Counter LHS, Counter RHS, bool Simplify) {
  auto Cnt = get(CounterExpression(CounterExpression::Add, LHS, RHS));
  return Simplify ? simplify(Cnt) : Cnt;
}

Counter CounterExpressionBuilder::subtract(Counter LHS, Counter RHS,
                                           bool Simplify) {
  auto Cnt = get(CounterExpression(CounterExpression::Subtract, LHS, RHS));
  return Simplify ? simplify(Cnt) : Cnt;
}

void CounterMappingContext::dump(const Counter &C, raw_ostream &OS) const {
  switch (C.getKind()) {
  case Counter::Zero:
    OS << '0';
    return;
  case Counter::CounterValueReference:
    OS << '#' << C.getCounterID();
    break;
  case Counter::Expression: {
    if (C.getExpressionID() >= Expressions.size())
      return;
    const auto &E = Expressions[C.getExpressionID()];
    OS << '(';
    dump(E.LHS, OS);
    OS << (E.Kind == CounterExpression::Subtract ? " - " : " + ");
    dump(E.RHS, OS);
    OS << ')';
    break;
  }
  }
  if (CounterValues.empty())
    return;
  Expected<int64_t> Value = evaluate(C);
  if (auto E = Value.takeError()) {
    consumeError(std::move(E));
    return;
  }
  OS << '[' << *Value << ']';
}

Expected<int64_t> CounterMappingContext::evaluate(const Counter &C) const {
  struct StackElem {
    Counter ICounter;
    int64_t LHS = 0;
    enum {
      KNeverVisited = 0,
      KVisitedOnce = 1,
      KVisitedTwice = 2,
    } VisitCount = KNeverVisited;
  };

  std::stack<StackElem> CounterStack;
  CounterStack.push({C});

  int64_t LastPoppedValue;

  while (!CounterStack.empty()) {
    StackElem &Current = CounterStack.top();

    switch (Current.ICounter.getKind()) {
    case Counter::Zero:
      LastPoppedValue = 0;
      CounterStack.pop();
      break;
    case Counter::CounterValueReference:
      if (Current.ICounter.getCounterID() >= CounterValues.size())
        return errorCodeToError(errc::argument_out_of_domain);
      LastPoppedValue = CounterValues[Current.ICounter.getCounterID()];
      CounterStack.pop();
      break;
    case Counter::Expression: {
      if (Current.ICounter.getExpressionID() >= Expressions.size())
        return errorCodeToError(errc::argument_out_of_domain);
      const auto &E = Expressions[Current.ICounter.getExpressionID()];
      if (Current.VisitCount == StackElem::KNeverVisited) {
        CounterStack.push(StackElem{E.LHS});
        Current.VisitCount = StackElem::KVisitedOnce;
      } else if (Current.VisitCount == StackElem::KVisitedOnce) {
        Current.LHS = LastPoppedValue;
        CounterStack.push(StackElem{E.RHS});
        Current.VisitCount = StackElem::KVisitedTwice;
      } else {
        int64_t LHS = Current.LHS;
        int64_t RHS = LastPoppedValue;
        LastPoppedValue =
            E.Kind == CounterExpression::Subtract ? LHS - RHS : LHS + RHS;
        CounterStack.pop();
      }
      break;
    }
    }
  }

  return LastPoppedValue;
}

mcdc::TVIdxBuilder::TVIdxBuilder(const SmallVectorImpl<ConditionIDs> &NextIDs,
                                 int Offset)
    : Indices(NextIDs.size()) {
  // Construct Nodes and set up each InCount
  auto N = NextIDs.size();
  SmallVector<MCDCNode> Nodes(N);
  for (unsigned ID = 0; ID < N; ++ID) {
    for (unsigned C = 0; C < 2; ++C) {
#ifndef NDEBUG
      Indices[ID][C] = INT_MIN;
#endif
      auto NextID = NextIDs[ID][C];
      Nodes[ID].NextIDs[C] = NextID;
      if (NextID >= 0)
        ++Nodes[NextID].InCount;
    }
  }

  // Sort key ordered by <-Width, Ord>
  SmallVector<std::tuple<int,      /// -Width
                         unsigned, /// Ord
                         int,      /// ID
                         unsigned  /// Cond (0 or 1)
                         >>
      Decisions;

  // Traverse Nodes to assign Idx
  SmallVector<int> Q;
  assert(Nodes[0].InCount == 0);
  Nodes[0].Width = 1;
  Q.push_back(0);

  unsigned Ord = 0;
  while (!Q.empty()) {
    auto IID = Q.begin();
    int ID = *IID;
    Q.erase(IID);
    auto &Node = Nodes[ID];
    assert(Node.Width > 0);

    for (unsigned I = 0; I < 2; ++I) {
      auto NextID = Node.NextIDs[I];
      assert(NextID != 0 && "NextID should not point to the top");
      if (NextID < 0) {
        // Decision
        Decisions.emplace_back(-Node.Width, Ord++, ID, I);
        assert(Ord == Decisions.size());
        continue;
      }

      // Inter Node
      auto &NextNode = Nodes[NextID];
      assert(NextNode.InCount > 0);

      // Assign Idx
      assert(Indices[ID][I] == INT_MIN);
      Indices[ID][I] = NextNode.Width;
      auto NextWidth = int64_t(NextNode.Width) + Node.Width;
      if (NextWidth > HardMaxTVs) {
        NumTestVectors = HardMaxTVs; // Overflow
        return;
      }
      NextNode.Width = NextWidth;

      // Ready if all incomings are processed.
      // Or NextNode.Width hasn't been confirmed yet.
      if (--NextNode.InCount == 0)
        Q.push_back(NextID);
    }
  }

  llvm::sort(Decisions);

  // Assign TestVector Indices in Decision Nodes
  int64_t CurIdx = 0;
  for (auto [NegWidth, Ord, ID, C] : Decisions) {
    int Width = -NegWidth;
    assert(Nodes[ID].Width == Width);
    assert(Nodes[ID].NextIDs[C] < 0);
    assert(Indices[ID][C] == INT_MIN);
    Indices[ID][C] = Offset + CurIdx;
    CurIdx += Width;
    if (CurIdx > HardMaxTVs) {
      NumTestVectors = HardMaxTVs; // Overflow
      return;
    }
  }

  assert(CurIdx < HardMaxTVs);
  NumTestVectors = CurIdx;

#ifndef NDEBUG
  for (const auto &Idxs : Indices)
    for (auto Idx : Idxs)
      assert(Idx != INT_MIN);
  SavedNodes = std::move(Nodes);
#endif
}

namespace {

/// Construct this->NextIDs with Branches for TVIdxBuilder to use it
/// before MCDCRecordProcessor().
class NextIDsBuilder {
protected:
  SmallVector<mcdc::ConditionIDs> NextIDs;

public:
  NextIDsBuilder(const ArrayRef<const CounterMappingRegion *> Branches)
      : NextIDs(Branches.size()) {
#ifndef NDEBUG
    DenseSet<mcdc::ConditionID> SeenIDs;
#endif
    for (const auto *Branch : Branches) {
      const auto &BranchParams = Branch->getBranchParams();
      assert(SeenIDs.insert(BranchParams.ID).second && "Duplicate CondID");
      NextIDs[BranchParams.ID] = BranchParams.Conds;
    }
    assert(SeenIDs.size() == Branches.size());
  }
};

class MCDCRecordProcessor : NextIDsBuilder, mcdc::TVIdxBuilder {
  /// A bitmap representing the executed test vectors for a boolean expression.
  /// Each index of the bitmap corresponds to a possible test vector. An index
  /// with a bit value of '1' indicates that the corresponding Test Vector
  /// identified by that index was executed.
  const BitVector &Bitmap;

  /// Decision Region to which the ExecutedTestVectorBitmap applies.
  const CounterMappingRegion &Region;
  const mcdc::DecisionParameters &DecisionParams;

  /// Array of branch regions corresponding each conditions in the boolean
  /// expression.
  ArrayRef<const CounterMappingRegion *> Branches;

  /// Total number of conditions in the boolean expression.
  unsigned NumConditions;

  /// Vector used to track whether a condition is constant folded.
  MCDCRecord::BoolVector Folded;

  /// Mapping of calculated MC/DC Independence Pairs for each condition.
  MCDCRecord::TVPairMap IndependencePairs;

  /// Storage for ExecVectors
  /// ExecVectors is the alias of its 0th element.
  std::array<MCDCRecord::TestVectors, 2> ExecVectorsByCond;

  /// Actual executed Test Vectors for the boolean expression, based on
  /// ExecutedTestVectorBitmap.
  MCDCRecord::TestVectors &ExecVectors;

  /// Number of False items in ExecVectors
  unsigned NumExecVectorsF;

#ifndef NDEBUG
  DenseSet<unsigned> TVIdxs;
#endif

  bool IsVersion11;

public:
  MCDCRecordProcessor(const BitVector &Bitmap,
                      const CounterMappingRegion &Region,
                      ArrayRef<const CounterMappingRegion *> Branches,
                      bool IsVersion11)
      : NextIDsBuilder(Branches), TVIdxBuilder(this->NextIDs), Bitmap(Bitmap),
        Region(Region), DecisionParams(Region.getDecisionParams()),
        Branches(Branches), NumConditions(DecisionParams.NumConditions),
        Folded(NumConditions, false), IndependencePairs(NumConditions),
        ExecVectors(ExecVectorsByCond[false]), IsVersion11(IsVersion11) {}

private:
  // Walk the binary decision diagram and try assigning both false and true to
  // each node. When a terminal node (ID == 0) is reached, fill in the value in
  // the truth table.
  void buildTestVector(MCDCRecord::TestVector &TV, mcdc::ConditionID ID,
                       int TVIdx) {
    for (auto MCDCCond : {MCDCRecord::MCDC_False, MCDCRecord::MCDC_True}) {
      static_assert(MCDCRecord::MCDC_False == 0);
      static_assert(MCDCRecord::MCDC_True == 1);
      TV.set(ID, MCDCCond);
      auto NextID = NextIDs[ID][MCDCCond];
      auto NextTVIdx = TVIdx + Indices[ID][MCDCCond];
      assert(NextID == SavedNodes[ID].NextIDs[MCDCCond]);
      if (NextID >= 0) {
        buildTestVector(TV, NextID, NextTVIdx);
        continue;
      }

      assert(TVIdx < SavedNodes[ID].Width);
      assert(TVIdxs.insert(NextTVIdx).second && "Duplicate TVIdx");

      if (!Bitmap[IsVersion11
                      ? DecisionParams.BitmapIdx * CHAR_BIT + TV.getIndex()
                      : DecisionParams.BitmapIdx - NumTestVectors + NextTVIdx])
        continue;

      // Copy the completed test vector to the vector of testvectors.
      // The final value (T,F) is equal to the last non-dontcare state on the
      // path (in a short-circuiting system).
      ExecVectorsByCond[MCDCCond].push_back({TV, MCDCCond});
    }

    // Reset back to DontCare.
    TV.set(ID, MCDCRecord::MCDC_DontCare);
  }

  /// Walk the bits in the bitmap.  A bit set to '1' indicates that the test
  /// vector at the corresponding index was executed during a test run.
  void findExecutedTestVectors() {
    // Walk the binary decision diagram to enumerate all possible test vectors.
    // We start at the root node (ID == 0) with all values being DontCare.
    // `TVIdx` starts with 0 and is in the traversal.
    // `Index` encodes the bitmask of true values and is initially 0.
    MCDCRecord::TestVector TV(NumConditions);
    buildTestVector(TV, 0, 0);
    assert(TVIdxs.size() == unsigned(NumTestVectors) &&
           "TVIdxs wasn't fulfilled");

    // Fill ExecVectors order by False items and True items.
    // ExecVectors is the alias of ExecVectorsByCond[false], so
    // Append ExecVectorsByCond[true] on it.
    NumExecVectorsF = ExecVectors.size();
    auto &ExecVectorsT = ExecVectorsByCond[true];
    ExecVectors.append(std::make_move_iterator(ExecVectorsT.begin()),
                       std::make_move_iterator(ExecVectorsT.end()));
  }

  // Find an independence pair for each condition:
  // - The condition is true in one test and false in the other.
  // - The decision outcome is true one test and false in the other.
  // - All other conditions' values must be equal or marked as "don't care".
  void findIndependencePairs() {
    unsigned NumTVs = ExecVectors.size();
    for (unsigned I = NumExecVectorsF; I < NumTVs; ++I) {
      const auto &[A, ACond] = ExecVectors[I];
      assert(ACond == MCDCRecord::MCDC_True);
      for (unsigned J = 0; J < NumExecVectorsF; ++J) {
        const auto &[B, BCond] = ExecVectors[J];
        assert(BCond == MCDCRecord::MCDC_False);
        // If the two vectors differ in exactly one condition, ignoring DontCare
        // conditions, we have found an independence pair.
        auto AB = A.getDifferences(B);
        if (AB.count() == 1)
          IndependencePairs.insert(
              {AB.find_first(), std::make_pair(J + 1, I + 1)});
      }
    }
  }

public:
  /// Process the MC/DC Record in order to produce a result for a boolean
  /// expression. This process includes tracking the conditions that comprise
  /// the decision region, calculating the list of all possible test vectors,
  /// marking the executed test vectors, and then finding an Independence Pair
  /// out of the executed test vectors for each condition in the boolean
  /// expression. A condition is tracked to ensure that its ID can be mapped to
  /// its ordinal position in the boolean expression. The condition's source
  /// location is also tracked, as well as whether it is constant folded (in
  /// which case it is excuded from the metric).
  MCDCRecord processMCDCRecord() {
    unsigned I = 0;
    MCDCRecord::CondIDMap PosToID;
    MCDCRecord::LineColPairMap CondLoc;

    // Walk the Record's BranchRegions (representing Conditions) in order to:
    // - Hash the condition based on its corresponding ID. This will be used to
    //   calculate the test vectors.
    // - Keep a map of the condition's ordinal position (1, 2, 3, 4) to its
    //   actual ID.  This will be used to visualize the conditions in the
    //   correct order.
    // - Keep track of the condition source location. This will be used to
    //   visualize where the condition is.
    // - Record whether the condition is constant folded so that we exclude it
    //   from being measured.
    for (const auto *B : Branches) {
      const auto &BranchParams = B->getBranchParams();
      PosToID[I] = BranchParams.ID;
      CondLoc[I] = B->startLoc();
      Folded[I++] = (B->Count.isZero() && B->FalseCount.isZero());
    }

    // Using Profile Bitmap from runtime, mark the executed test vectors.
    findExecutedTestVectors();

    // Compare executed test vectors against each other to find an independence
    // pairs for each condition.  This processing takes the most time.
    findIndependencePairs();

    // Record Test vectors, executed vectors, and independence pairs.
    return MCDCRecord(Region, std::move(ExecVectors),
                      std::move(IndependencePairs), std::move(Folded),
                      std::move(PosToID), std::move(CondLoc));
  }
};

} // namespace

Expected<MCDCRecord> CounterMappingContext::evaluateMCDCRegion(
    const CounterMappingRegion &Region,
    ArrayRef<const CounterMappingRegion *> Branches, bool IsVersion11) {

  MCDCRecordProcessor MCDCProcessor(Bitmap, Region, Branches, IsVersion11);
  return MCDCProcessor.processMCDCRecord();
}

unsigned CounterMappingContext::getMaxCounterID(const Counter &C) const {
  struct StackElem {
    Counter ICounter;
    int64_t LHS = 0;
    enum {
      KNeverVisited = 0,
      KVisitedOnce = 1,
      KVisitedTwice = 2,
    } VisitCount = KNeverVisited;
  };

  std::stack<StackElem> CounterStack;
  CounterStack.push({C});

  int64_t LastPoppedValue;

  while (!CounterStack.empty()) {
    StackElem &Current = CounterStack.top();

    switch (Current.ICounter.getKind()) {
    case Counter::Zero:
      LastPoppedValue = 0;
      CounterStack.pop();
      break;
    case Counter::CounterValueReference:
      LastPoppedValue = Current.ICounter.getCounterID();
      CounterStack.pop();
      break;
    case Counter::Expression: {
      if (Current.ICounter.getExpressionID() >= Expressions.size()) {
        LastPoppedValue = 0;
        CounterStack.pop();
      } else {
        const auto &E = Expressions[Current.ICounter.getExpressionID()];
        if (Current.VisitCount == StackElem::KNeverVisited) {
          CounterStack.push(StackElem{E.LHS});
          Current.VisitCount = StackElem::KVisitedOnce;
        } else if (Current.VisitCount == StackElem::KVisitedOnce) {
          Current.LHS = LastPoppedValue;
          CounterStack.push(StackElem{E.RHS});
          Current.VisitCount = StackElem::KVisitedTwice;
        } else {
          int64_t LHS = Current.LHS;
          int64_t RHS = LastPoppedValue;
          LastPoppedValue = std::max(LHS, RHS);
          CounterStack.pop();
        }
      }
      break;
    }
    }
  }

  return LastPoppedValue;
}

void FunctionRecordIterator::skipOtherFiles() {
  while (Current != Records.end() && !Filename.empty() &&
         Filename != Current->Filenames[0])
    ++Current;
  if (Current == Records.end())
    *this = FunctionRecordIterator();
}

ArrayRef<unsigned> CoverageMapping::getImpreciseRecordIndicesForFilename(
    StringRef Filename) const {
  size_t FilenameHash = hash_value(Filename);
  auto RecordIt = FilenameHash2RecordIndices.find(FilenameHash);
  if (RecordIt == FilenameHash2RecordIndices.end())
    return {};
  return RecordIt->second;
}

static unsigned getMaxCounterID(const CounterMappingContext &Ctx,
                                const CoverageMappingRecord &Record) {
  unsigned MaxCounterID = 0;
  for (const auto &Region : Record.MappingRegions) {
    MaxCounterID = std::max(MaxCounterID, Ctx.getMaxCounterID(Region.Count));
  }
  return MaxCounterID;
}

/// Returns the bit count
static unsigned getMaxBitmapSize(const CoverageMappingRecord &Record,
                                 bool IsVersion11) {
  unsigned MaxBitmapIdx = 0;
  unsigned NumConditions = 0;
  // Scan max(BitmapIdx).
  // Note that `<=` is used insted of `<`, because `BitmapIdx == 0` is valid
  // and `MaxBitmapIdx is `unsigned`. `BitmapIdx` is unique in the record.
  for (const auto &Region : reverse(Record.MappingRegions)) {
    if (Region.Kind != CounterMappingRegion::MCDCDecisionRegion)
      continue;
    const auto &DecisionParams = Region.getDecisionParams();
    if (MaxBitmapIdx <= DecisionParams.BitmapIdx) {
      MaxBitmapIdx = DecisionParams.BitmapIdx;
      NumConditions = DecisionParams.NumConditions;
    }
  }

  if (IsVersion11)
    MaxBitmapIdx = MaxBitmapIdx * CHAR_BIT +
                   llvm::alignTo(uint64_t(1) << NumConditions, CHAR_BIT);

  return MaxBitmapIdx;
}

namespace {

/// Collect Decisions, Branchs, and Expansions and associate them.
class MCDCDecisionRecorder {
private:
  /// This holds the DecisionRegion and MCDCBranches under it.
  /// Also traverses Expansion(s).
  /// The Decision has the number of MCDCBranches and will complete
  /// when it is filled with unique ConditionID of MCDCBranches.
  struct DecisionRecord {
    const CounterMappingRegion *DecisionRegion;

    /// They are reflected from DecisionRegion for convenience.
    mcdc::DecisionParameters DecisionParams;
    LineColPair DecisionStartLoc;
    LineColPair DecisionEndLoc;

    /// This is passed to `MCDCRecordProcessor`, so this should be compatible
    /// to`ArrayRef<const CounterMappingRegion *>`.
    SmallVector<const CounterMappingRegion *> MCDCBranches;

    /// IDs that are stored in MCDCBranches
    /// Complete when all IDs (1 to NumConditions) are met.
    DenseSet<mcdc::ConditionID> ConditionIDs;

    /// Set of IDs of Expansion(s) that are relevant to DecisionRegion
    /// and its children (via expansions).
    /// FileID  pointed by ExpandedFileID is dedicated to the expansion, so
    /// the location in the expansion doesn't matter.
    DenseSet<unsigned> ExpandedFileIDs;

    DecisionRecord(const CounterMappingRegion &Decision)
        : DecisionRegion(&Decision),
          DecisionParams(Decision.getDecisionParams()),
          DecisionStartLoc(Decision.startLoc()),
          DecisionEndLoc(Decision.endLoc()) {
      assert(Decision.Kind == CounterMappingRegion::MCDCDecisionRegion);
    }

    /// Determine whether DecisionRecord dominates `R`.
    bool dominates(const CounterMappingRegion &R) const {
      // Determine whether `R` is included in `DecisionRegion`.
      if (R.FileID == DecisionRegion->FileID &&
          R.startLoc() >= DecisionStartLoc && R.endLoc() <= DecisionEndLoc)
        return true;

      // Determine whether `R` is pointed by any of Expansions.
      return ExpandedFileIDs.contains(R.FileID);
    }

    enum Result {
      NotProcessed = 0, /// Irrelevant to this Decision
      Processed,        /// Added to this Decision
      Completed,        /// Added and filled this Decision
    };

    /// Add Branch into the Decision
    /// \param Branch expects MCDCBranchRegion
    /// \returns NotProcessed/Processed/Completed
    Result addBranch(const CounterMappingRegion &Branch) {
      assert(Branch.Kind == CounterMappingRegion::MCDCBranchRegion);

      auto ConditionID = Branch.getBranchParams().ID;

      if (ConditionIDs.contains(ConditionID) ||
          ConditionID >= DecisionParams.NumConditions)
        return NotProcessed;

      if (!this->dominates(Branch))
        return NotProcessed;

      assert(MCDCBranches.size() < DecisionParams.NumConditions);

      // Put `ID=0` in front of `MCDCBranches` for convenience
      // even if `MCDCBranches` is not topological.
      if (ConditionID == 0)
        MCDCBranches.insert(MCDCBranches.begin(), &Branch);
      else
        MCDCBranches.push_back(&Branch);

      // Mark `ID` as `assigned`.
      ConditionIDs.insert(ConditionID);

      // `Completed` when `MCDCBranches` is full
      return (MCDCBranches.size() == DecisionParams.NumConditions ? Completed
                                                                  : Processed);
    }

    /// Record Expansion if it is relevant to this Decision.
    /// Each `Expansion` may nest.
    /// \returns true if recorded.
    bool recordExpansion(const CounterMappingRegion &Expansion) {
      if (!this->dominates(Expansion))
        return false;

      ExpandedFileIDs.insert(Expansion.ExpandedFileID);
      return true;
    }
  };

private:
  /// Decisions in progress
  /// DecisionRecord is added for each MCDCDecisionRegion.
  /// DecisionRecord is removed when Decision is completed.
  SmallVector<DecisionRecord> Decisions;

public:
  ~MCDCDecisionRecorder() {
    assert(Decisions.empty() && "All Decisions have not been resolved");
  }

  /// Register Region and start recording.
  void registerDecision(const CounterMappingRegion &Decision) {
    Decisions.emplace_back(Decision);
  }

  void recordExpansion(const CounterMappingRegion &Expansion) {
    any_of(Decisions, [&Expansion](auto &Decision) {
      return Decision.recordExpansion(Expansion);
    });
  }

  using DecisionAndBranches =
      std::pair<const CounterMappingRegion *,             /// Decision
                SmallVector<const CounterMappingRegion *> /// Branches
                >;

  /// Add MCDCBranchRegion to DecisionRecord.
  /// \param Branch to be processed
  /// \returns DecisionsAndBranches if DecisionRecord completed.
  ///     Or returns nullopt.
  std::optional<DecisionAndBranches>
  processBranch(const CounterMappingRegion &Branch) {
    // Seek each Decision and apply Region to it.
    for (auto DecisionIter = Decisions.begin(), DecisionEnd = Decisions.end();
         DecisionIter != DecisionEnd; ++DecisionIter)
      switch (DecisionIter->addBranch(Branch)) {
      case DecisionRecord::NotProcessed:
        continue;
      case DecisionRecord::Processed:
        return std::nullopt;
      case DecisionRecord::Completed:
        DecisionAndBranches Result =
            std::make_pair(DecisionIter->DecisionRegion,
                           std::move(DecisionIter->MCDCBranches));
        Decisions.erase(DecisionIter); // No longer used.
        return Result;
      }

    llvm_unreachable("Branch not found in Decisions");
  }
};

} // namespace

Error CoverageMapping::loadFunctionRecord(
    const CoverageMappingRecord &Record,
    IndexedInstrProfReader &ProfileReader) {
  StringRef OrigFuncName = Record.FunctionName;
  if (OrigFuncName.empty())
    return make_error<CoverageMapError>(coveragemap_error::malformed,
                                        "record function name is empty");

  if (Record.Filenames.empty())
    OrigFuncName = getFuncNameWithoutPrefix(OrigFuncName);
  else
    OrigFuncName = getFuncNameWithoutPrefix(OrigFuncName, Record.Filenames[0]);

  CounterMappingContext Ctx(Record.Expressions);

  std::vector<uint64_t> Counts;
  if (Error E = ProfileReader.getFunctionCounts(Record.FunctionName,
                                                Record.FunctionHash, Counts)) {
    instrprof_error IPE = std::get<0>(InstrProfError::take(std::move(E)));
    if (IPE == instrprof_error::hash_mismatch) {
      FuncHashMismatches.emplace_back(std::string(Record.FunctionName),
                                      Record.FunctionHash);
      return Error::success();
    }
    if (IPE != instrprof_error::unknown_function)
      return make_error<InstrProfError>(IPE);
    Counts.assign(getMaxCounterID(Ctx, Record) + 1, 0);
  }
  Ctx.setCounts(Counts);

  bool IsVersion11 =
      ProfileReader.getVersion() < IndexedInstrProf::ProfVersion::Version12;

  BitVector Bitmap;
  if (Error E = ProfileReader.getFunctionBitmap(Record.FunctionName,
                                                Record.FunctionHash, Bitmap)) {
    instrprof_error IPE = std::get<0>(InstrProfError::take(std::move(E)));
    if (IPE == instrprof_error::hash_mismatch) {
      FuncHashMismatches.emplace_back(std::string(Record.FunctionName),
                                      Record.FunctionHash);
      return Error::success();
    }
    if (IPE != instrprof_error::unknown_function)
      return make_error<InstrProfError>(IPE);
    Bitmap = BitVector(getMaxBitmapSize(Record, IsVersion11));
  }
  Ctx.setBitmap(std::move(Bitmap));

  assert(!Record.MappingRegions.empty() && "Function has no regions");

  // This coverage record is a zero region for a function that's unused in
  // some TU, but used in a different TU. Ignore it. The coverage maps from the
  // the other TU will either be loaded (providing full region counts) or they
  // won't (in which case we don't unintuitively report functions as uncovered
  // when they have non-zero counts in the profile).
  if (Record.MappingRegions.size() == 1 &&
      Record.MappingRegions[0].Count.isZero() && Counts[0] > 0)
    return Error::success();

  MCDCDecisionRecorder MCDCDecisions;
  FunctionRecord Function(OrigFuncName, Record.Filenames);
  for (const auto &Region : Record.MappingRegions) {
    // MCDCDecisionRegion should be handled first since it overlaps with
    // others inside.
    if (Region.Kind == CounterMappingRegion::MCDCDecisionRegion) {
      MCDCDecisions.registerDecision(Region);
      continue;
    }
    Expected<int64_t> ExecutionCount = Ctx.evaluate(Region.Count);
    if (auto E = ExecutionCount.takeError()) {
      consumeError(std::move(E));
      return Error::success();
    }
    Expected<int64_t> AltExecutionCount = Ctx.evaluate(Region.FalseCount);
    if (auto E = AltExecutionCount.takeError()) {
      consumeError(std::move(E));
      return Error::success();
    }
    Function.pushRegion(Region, *ExecutionCount, *AltExecutionCount,
                        ProfileReader.hasSingleByteCoverage());

    // Record ExpansionRegion.
    if (Region.Kind == CounterMappingRegion::ExpansionRegion) {
      MCDCDecisions.recordExpansion(Region);
      continue;
    }

    // Do nothing unless MCDCBranchRegion.
    if (Region.Kind != CounterMappingRegion::MCDCBranchRegion)
      continue;

    auto Result = MCDCDecisions.processBranch(Region);
    if (!Result) // Any Decision doesn't complete.
      continue;

    auto MCDCDecision = Result->first;
    auto &MCDCBranches = Result->second;

    // Since the bitmap identifies the executed test vectors for an MC/DC
    // DecisionRegion, all of the information is now available to process.
    // This is where the bulk of the MC/DC progressing takes place.
    Expected<MCDCRecord> Record =
        Ctx.evaluateMCDCRegion(*MCDCDecision, MCDCBranches, IsVersion11);
    if (auto E = Record.takeError()) {
      consumeError(std::move(E));
      return Error::success();
    }

    // Save the MC/DC Record so that it can be visualized later.
    Function.pushMCDCRecord(std::move(*Record));
  }

  // Don't create records for (filenames, function) pairs we've already seen.
  auto FilenamesHash = hash_combine_range(Record.Filenames.begin(),
                                          Record.Filenames.end());
  if (!RecordProvenance[FilenamesHash].insert(hash_value(OrigFuncName)).second)
    return Error::success();

  Functions.push_back(std::move(Function));

  // Performance optimization: keep track of the indices of the function records
  // which correspond to each filename. This can be used to substantially speed
  // up queries for coverage info in a file.
  unsigned RecordIndex = Functions.size() - 1;
  for (StringRef Filename : Record.Filenames) {
    auto &RecordIndices = FilenameHash2RecordIndices[hash_value(Filename)];
    // Note that there may be duplicates in the filename set for a function
    // record, because of e.g. macro expansions in the function in which both
    // the macro and the function are defined in the same file.
    if (RecordIndices.empty() || RecordIndices.back() != RecordIndex)
      RecordIndices.push_back(RecordIndex);
  }

  return Error::success();
}

// This function is for memory optimization by shortening the lifetimes
// of CoverageMappingReader instances.
Error CoverageMapping::loadFromReaders(
    ArrayRef<std::unique_ptr<CoverageMappingReader>> CoverageReaders,
    IndexedInstrProfReader &ProfileReader, CoverageMapping &Coverage) {
  for (const auto &CoverageReader : CoverageReaders) {
    for (auto RecordOrErr : *CoverageReader) {
      if (Error E = RecordOrErr.takeError())
        return E;
      const auto &Record = *RecordOrErr;
      if (Error E = Coverage.loadFunctionRecord(Record, ProfileReader))
        return E;
    }
  }
  return Error::success();
}

Expected<std::unique_ptr<CoverageMapping>> CoverageMapping::load(
    ArrayRef<std::unique_ptr<CoverageMappingReader>> CoverageReaders,
    IndexedInstrProfReader &ProfileReader) {
  auto Coverage = std::unique_ptr<CoverageMapping>(new CoverageMapping());
  if (Error E = loadFromReaders(CoverageReaders, ProfileReader, *Coverage))
    return std::move(E);
  return std::move(Coverage);
}

// If E is a no_data_found error, returns success. Otherwise returns E.
static Error handleMaybeNoDataFoundError(Error E) {
  return handleErrors(
      std::move(E), [](const CoverageMapError &CME) {
        if (CME.get() == coveragemap_error::no_data_found)
          return static_cast<Error>(Error::success());
        return make_error<CoverageMapError>(CME.get(), CME.getMessage());
      });
}

Error CoverageMapping::loadFromFile(
    StringRef Filename, StringRef Arch, StringRef CompilationDir,
    IndexedInstrProfReader &ProfileReader, CoverageMapping &Coverage,
    bool &DataFound, SmallVectorImpl<object::BuildID> *FoundBinaryIDs) {
  auto CovMappingBufOrErr = MemoryBuffer::getFileOrSTDIN(
      Filename, /*IsText=*/false, /*RequiresNullTerminator=*/false);
  if (std::error_code EC = CovMappingBufOrErr.getError())
    return createFileError(Filename, errorCodeToError(EC));
  MemoryBufferRef CovMappingBufRef =
      CovMappingBufOrErr.get()->getMemBufferRef();
  SmallVector<std::unique_ptr<MemoryBuffer>, 4> Buffers;

  SmallVector<object::BuildIDRef> BinaryIDs;
  auto CoverageReadersOrErr = BinaryCoverageReader::create(
      CovMappingBufRef, Arch, Buffers, CompilationDir,
      FoundBinaryIDs ? &BinaryIDs : nullptr);
  if (Error E = CoverageReadersOrErr.takeError()) {
    E = handleMaybeNoDataFoundError(std::move(E));
    if (E)
      return createFileError(Filename, std::move(E));
    return E;
  }

  SmallVector<std::unique_ptr<CoverageMappingReader>, 4> Readers;
  for (auto &Reader : CoverageReadersOrErr.get())
    Readers.push_back(std::move(Reader));
  if (FoundBinaryIDs && !Readers.empty()) {
    llvm::append_range(*FoundBinaryIDs,
                       llvm::map_range(BinaryIDs, [](object::BuildIDRef BID) {
                         return object::BuildID(BID);
                       }));
  }
  DataFound |= !Readers.empty();
  if (Error E = loadFromReaders(Readers, ProfileReader, Coverage))
    return createFileError(Filename, std::move(E));
  return Error::success();
}

Expected<std::unique_ptr<CoverageMapping>> CoverageMapping::load(
    ArrayRef<StringRef> ObjectFilenames, StringRef ProfileFilename,
    vfs::FileSystem &FS, ArrayRef<StringRef> Arches, StringRef CompilationDir,
    const object::BuildIDFetcher *BIDFetcher, bool CheckBinaryIDs) {
  auto ProfileReaderOrErr = IndexedInstrProfReader::create(ProfileFilename, FS);
  if (Error E = ProfileReaderOrErr.takeError())
    return createFileError(ProfileFilename, std::move(E));
  auto ProfileReader = std::move(ProfileReaderOrErr.get());
  auto Coverage = std::unique_ptr<CoverageMapping>(new CoverageMapping());
  bool DataFound = false;

  auto GetArch = [&](size_t Idx) {
    if (Arches.empty())
      return StringRef();
    if (Arches.size() == 1)
      return Arches.front();
    return Arches[Idx];
  };

  SmallVector<object::BuildID> FoundBinaryIDs;
  for (const auto &File : llvm::enumerate(ObjectFilenames)) {
    if (Error E =
            loadFromFile(File.value(), GetArch(File.index()), CompilationDir,
                         *ProfileReader, *Coverage, DataFound, &FoundBinaryIDs))
      return std::move(E);
  }

  if (BIDFetcher) {
    std::vector<object::BuildID> ProfileBinaryIDs;
    if (Error E = ProfileReader->readBinaryIds(ProfileBinaryIDs))
      return createFileError(ProfileFilename, std::move(E));

    SmallVector<object::BuildIDRef> BinaryIDsToFetch;
    if (!ProfileBinaryIDs.empty()) {
      const auto &Compare = [](object::BuildIDRef A, object::BuildIDRef B) {
        return std::lexicographical_compare(A.begin(), A.end(), B.begin(),
                                            B.end());
      };
      llvm::sort(FoundBinaryIDs, Compare);
      std::set_difference(
          ProfileBinaryIDs.begin(), ProfileBinaryIDs.end(),
          FoundBinaryIDs.begin(), FoundBinaryIDs.end(),
          std::inserter(BinaryIDsToFetch, BinaryIDsToFetch.end()), Compare);
    }

    for (object::BuildIDRef BinaryID : BinaryIDsToFetch) {
      std::optional<std::string> PathOpt = BIDFetcher->fetch(BinaryID);
      if (PathOpt) {
        std::string Path = std::move(*PathOpt);
        StringRef Arch = Arches.size() == 1 ? Arches.front() : StringRef();
        if (Error E = loadFromFile(Path, Arch, CompilationDir, *ProfileReader,
                                  *Coverage, DataFound))
          return std::move(E);
      } else if (CheckBinaryIDs) {
        return createFileError(
            ProfileFilename,
            createStringError(errc::no_such_file_or_directory,
                              "Missing binary ID: " +
                                  llvm::toHex(BinaryID, /*LowerCase=*/true)));
      }
    }
  }

  if (!DataFound)
    return createFileError(
        join(ObjectFilenames.begin(), ObjectFilenames.end(), ", "),
        make_error<CoverageMapError>(coveragemap_error::no_data_found));
  return std::move(Coverage);
}

namespace {

/// Distributes functions into instantiation sets.
///
/// An instantiation set is a collection of functions that have the same source
/// code, ie, template functions specializations.
class FunctionInstantiationSetCollector {
  using MapT = std::map<LineColPair, std::vector<const FunctionRecord *>>;
  MapT InstantiatedFunctions;

public:
  void insert(const FunctionRecord &Function, unsigned FileID) {
    auto I = Function.CountedRegions.begin(), E = Function.CountedRegions.end();
    while (I != E && I->FileID != FileID)
      ++I;
    assert(I != E && "function does not cover the given file");
    auto &Functions = InstantiatedFunctions[I->startLoc()];
    Functions.push_back(&Function);
  }

  MapT::iterator begin() { return InstantiatedFunctions.begin(); }
  MapT::iterator end() { return InstantiatedFunctions.end(); }
};

class SegmentBuilder {
  std::vector<CoverageSegment> &Segments;
  SmallVector<const CountedRegion *, 8> ActiveRegions;

  SegmentBuilder(std::vector<CoverageSegment> &Segments) : Segments(Segments) {}

  /// Emit a segment with the count from \p Region starting at \p StartLoc.
  //
  /// \p IsRegionEntry: The segment is at the start of a new non-gap region.
  /// \p EmitSkippedRegion: The segment must be emitted as a skipped region.
  void startSegment(const CountedRegion &Region, LineColPair StartLoc,
                    bool IsRegionEntry, bool EmitSkippedRegion = false) {
    bool HasCount = !EmitSkippedRegion &&
                    (Region.Kind != CounterMappingRegion::SkippedRegion);

    // If the new segment wouldn't affect coverage rendering, skip it.
    if (!Segments.empty() && !IsRegionEntry && !EmitSkippedRegion) {
      const auto &Last = Segments.back();
      if (Last.HasCount == HasCount && Last.Count == Region.ExecutionCount &&
          !Last.IsRegionEntry)
        return;
    }

    if (HasCount)
      Segments.emplace_back(StartLoc.first, StartLoc.second,
                            Region.ExecutionCount, IsRegionEntry,
                            Region.Kind == CounterMappingRegion::GapRegion);
    else
      Segments.emplace_back(StartLoc.first, StartLoc.second, IsRegionEntry);

    LLVM_DEBUG({
      const auto &Last = Segments.back();
      dbgs() << "Segment at " << Last.Line << ":" << Last.Col
             << " (count = " << Last.Count << ")"
             << (Last.IsRegionEntry ? ", RegionEntry" : "")
             << (!Last.HasCount ? ", Skipped" : "")
             << (Last.IsGapRegion ? ", Gap" : "") << "\n";
    });
  }

  /// Emit segments for active regions which end before \p Loc.
  ///
  /// \p Loc: The start location of the next region. If std::nullopt, all active
  /// regions are completed.
  /// \p FirstCompletedRegion: Index of the first completed region.
  void completeRegionsUntil(std::optional<LineColPair> Loc,
                            unsigned FirstCompletedRegion) {
    // Sort the completed regions by end location. This makes it simple to
    // emit closing segments in sorted order.
    auto CompletedRegionsIt = ActiveRegions.begin() + FirstCompletedRegion;
    std::stable_sort(CompletedRegionsIt, ActiveRegions.end(),
                      [](const CountedRegion *L, const CountedRegion *R) {
                        return L->endLoc() < R->endLoc();
                      });

    // Emit segments for all completed regions.
    for (unsigned I = FirstCompletedRegion + 1, E = ActiveRegions.size(); I < E;
         ++I) {
      const auto *CompletedRegion = ActiveRegions[I];
      assert((!Loc || CompletedRegion->endLoc() <= *Loc) &&
             "Completed region ends after start of new region");

      const auto *PrevCompletedRegion = ActiveRegions[I - 1];
      auto CompletedSegmentLoc = PrevCompletedRegion->endLoc();

      // Don't emit any more segments if they start where the new region begins.
      if (Loc && CompletedSegmentLoc == *Loc)
        break;

      // Don't emit a segment if the next completed region ends at the same
      // location as this one.
      if (CompletedSegmentLoc == CompletedRegion->endLoc())
        continue;

      // Use the count from the last completed region which ends at this loc.
      for (unsigned J = I + 1; J < E; ++J)
        if (CompletedRegion->endLoc() == ActiveRegions[J]->endLoc())
          CompletedRegion = ActiveRegions[J];

      startSegment(*CompletedRegion, CompletedSegmentLoc, false);
    }

    auto Last = ActiveRegions.back();
    if (FirstCompletedRegion && Last->endLoc() != *Loc) {
      // If there's a gap after the end of the last completed region and the
      // start of the new region, use the last active region to fill the gap.
      startSegment(*ActiveRegions[FirstCompletedRegion - 1], Last->endLoc(),
                   false);
    } else if (!FirstCompletedRegion && (!Loc || *Loc != Last->endLoc())) {
      // Emit a skipped segment if there are no more active regions. This
      // ensures that gaps between functions are marked correctly.
      startSegment(*Last, Last->endLoc(), false, true);
    }

    // Pop the completed regions.
    ActiveRegions.erase(CompletedRegionsIt, ActiveRegions.end());
  }

  void buildSegmentsImpl(ArrayRef<CountedRegion> Regions) {
    for (const auto &CR : enumerate(Regions)) {
      auto CurStartLoc = CR.value().startLoc();

      // Active regions which end before the current region need to be popped.
      auto CompletedRegions =
          std::stable_partition(ActiveRegions.begin(), ActiveRegions.end(),
                                [&](const CountedRegion *Region) {
                                  return !(Region->endLoc() <= CurStartLoc);
                                });
      if (CompletedRegions != ActiveRegions.end()) {
        unsigned FirstCompletedRegion =
            std::distance(ActiveRegions.begin(), CompletedRegions);
        completeRegionsUntil(CurStartLoc, FirstCompletedRegion);
      }

      bool GapRegion = CR.value().Kind == CounterMappingRegion::GapRegion;

      // Try to emit a segment for the current region.
      if (CurStartLoc == CR.value().endLoc()) {
        // Avoid making zero-length regions active. If it's the last region,
        // emit a skipped segment. Otherwise use its predecessor's count.
        const bool Skipped =
            (CR.index() + 1) == Regions.size() ||
            CR.value().Kind == CounterMappingRegion::SkippedRegion;
        startSegment(ActiveRegions.empty() ? CR.value() : *ActiveRegions.back(),
                     CurStartLoc, !GapRegion, Skipped);
        // If it is skipped segment, create a segment with last pushed
        // regions's count at CurStartLoc.
        if (Skipped && !ActiveRegions.empty())
          startSegment(*ActiveRegions.back(), CurStartLoc, false);
        continue;
      }
      if (CR.index() + 1 == Regions.size() ||
          CurStartLoc != Regions[CR.index() + 1].startLoc()) {
        // Emit a segment if the next region doesn't start at the same location
        // as this one.
        startSegment(CR.value(), CurStartLoc, !GapRegion);
      }

      // This region is active (i.e not completed).
      ActiveRegions.push_back(&CR.value());
    }

    // Complete any remaining active regions.
    if (!ActiveRegions.empty())
      completeRegionsUntil(std::nullopt, 0);
  }

  /// Sort a nested sequence of regions from a single file.
  static void sortNestedRegions(MutableArrayRef<CountedRegion> Regions) {
    llvm::sort(Regions, [](const CountedRegion &LHS, const CountedRegion &RHS) {
      if (LHS.startLoc() != RHS.startLoc())
        return LHS.startLoc() < RHS.startLoc();
      if (LHS.endLoc() != RHS.endLoc())
        // When LHS completely contains RHS, we sort LHS first.
        return RHS.endLoc() < LHS.endLoc();
      // If LHS and RHS cover the same area, we need to sort them according
      // to their kinds so that the most suitable region will become "active"
      // in combineRegions(). Because we accumulate counter values only from
      // regions of the same kind as the first region of the area, prefer
      // CodeRegion to ExpansionRegion and ExpansionRegion to SkippedRegion.
      static_assert(CounterMappingRegion::CodeRegion <
                            CounterMappingRegion::ExpansionRegion &&
                        CounterMappingRegion::ExpansionRegion <
                            CounterMappingRegion::SkippedRegion,
                    "Unexpected order of region kind values");
      return LHS.Kind < RHS.Kind;
    });
  }

  /// Combine counts of regions which cover the same area.
  static ArrayRef<CountedRegion>
  combineRegions(MutableArrayRef<CountedRegion> Regions) {
    if (Regions.empty())
      return Regions;
    auto Active = Regions.begin();
    auto End = Regions.end();
    for (auto I = Regions.begin() + 1; I != End; ++I) {
      if (Active->startLoc() != I->startLoc() ||
          Active->endLoc() != I->endLoc()) {
        // Shift to the next region.
        ++Active;
        if (Active != I)
          *Active = *I;
        continue;
      }
      // Merge duplicate region.
      // If CodeRegions and ExpansionRegions cover the same area, it's probably
      // a macro which is fully expanded to another macro. In that case, we need
      // to accumulate counts only from CodeRegions, or else the area will be
      // counted twice.
      // On the other hand, a macro may have a nested macro in its body. If the
      // outer macro is used several times, the ExpansionRegion for the nested
      // macro will also be added several times. These ExpansionRegions cover
      // the same source locations and have to be combined to reach the correct
      // value for that area.
      // We add counts of the regions of the same kind as the active region
      // to handle the both situations.
      if (I->Kind == Active->Kind) {
        assert(I->HasSingleByteCoverage == Active->HasSingleByteCoverage &&
               "Regions are generated in different coverage modes");
        if (I->HasSingleByteCoverage)
          Active->ExecutionCount = Active->ExecutionCount || I->ExecutionCount;
        else
          Active->ExecutionCount += I->ExecutionCount;
      }
    }
    return Regions.drop_back(std::distance(++Active, End));
  }

public:
  /// Build a sorted list of CoverageSegments from a list of Regions.
  static std::vector<CoverageSegment>
  buildSegments(MutableArrayRef<CountedRegion> Regions) {
    std::vector<CoverageSegment> Segments;
    SegmentBuilder Builder(Segments);

    sortNestedRegions(Regions);
    ArrayRef<CountedRegion> CombinedRegions = combineRegions(Regions);

    LLVM_DEBUG({
      dbgs() << "Combined regions:\n";
      for (const auto &CR : CombinedRegions)
        dbgs() << "  " << CR.LineStart << ":" << CR.ColumnStart << " -> "
               << CR.LineEnd << ":" << CR.ColumnEnd
               << " (count=" << CR.ExecutionCount << ")\n";
    });

    Builder.buildSegmentsImpl(CombinedRegions);

#ifndef NDEBUG
    for (unsigned I = 1, E = Segments.size(); I < E; ++I) {
      const auto &L = Segments[I - 1];
      const auto &R = Segments[I];
      if (!(L.Line < R.Line) && !(L.Line == R.Line && L.Col < R.Col)) {
        if (L.Line == R.Line && L.Col == R.Col && !L.HasCount)
          continue;
        LLVM_DEBUG(dbgs() << " ! Segment " << L.Line << ":" << L.Col
                          << " followed by " << R.Line << ":" << R.Col << "\n");
        assert(false && "Coverage segments not unique or sorted");
      }
    }
#endif

    return Segments;
  }
};

} // end anonymous namespace

std::vector<StringRef> CoverageMapping::getUniqueSourceFiles() const {
  std::vector<StringRef> Filenames;
  for (const auto &Function : getCoveredFunctions())
    llvm::append_range(Filenames, Function.Filenames);
  llvm::sort(Filenames);
  auto Last = llvm::unique(Filenames);
  Filenames.erase(Last, Filenames.end());
  return Filenames;
}

static SmallBitVector gatherFileIDs(StringRef SourceFile,
                                    const FunctionRecord &Function) {
  SmallBitVector FilenameEquivalence(Function.Filenames.size(), false);
  for (unsigned I = 0, E = Function.Filenames.size(); I < E; ++I)
    if (SourceFile == Function.Filenames[I])
      FilenameEquivalence[I] = true;
  return FilenameEquivalence;
}

/// Return the ID of the file where the definition of the function is located.
static std::optional<unsigned>
findMainViewFileID(const FunctionRecord &Function) {
  SmallBitVector IsNotExpandedFile(Function.Filenames.size(), true);
  for (const auto &CR : Function.CountedRegions)
    if (CR.Kind == CounterMappingRegion::ExpansionRegion)
      IsNotExpandedFile[CR.ExpandedFileID] = false;
  int I = IsNotExpandedFile.find_first();
  if (I == -1)
    return std::nullopt;
  return I;
}

/// Check if SourceFile is the file that contains the definition of
/// the Function. Return the ID of the file in that case or std::nullopt
/// otherwise.
static std::optional<unsigned>
findMainViewFileID(StringRef SourceFile, const FunctionRecord &Function) {
  std::optional<unsigned> I = findMainViewFileID(Function);
  if (I && SourceFile == Function.Filenames[*I])
    return I;
  return std::nullopt;
}

static bool isExpansion(const CountedRegion &R, unsigned FileID) {
  return R.Kind == CounterMappingRegion::ExpansionRegion && R.FileID == FileID;
}

CoverageData CoverageMapping::getCoverageForFile(StringRef Filename) const {
  CoverageData FileCoverage(Filename);
  std::vector<CountedRegion> Regions;

  // Look up the function records in the given file. Due to hash collisions on
  // the filename, we may get back some records that are not in the file.
  ArrayRef<unsigned> RecordIndices =
      getImpreciseRecordIndicesForFilename(Filename);
  for (unsigned RecordIndex : RecordIndices) {
    const FunctionRecord &Function = Functions[RecordIndex];
    auto MainFileID = findMainViewFileID(Filename, Function);
    auto FileIDs = gatherFileIDs(Filename, Function);
    for (const auto &CR : Function.CountedRegions)
      if (FileIDs.test(CR.FileID)) {
        Regions.push_back(CR);
        if (MainFileID && isExpansion(CR, *MainFileID))
          FileCoverage.Expansions.emplace_back(CR, Function);
      }
    // Capture branch regions specific to the function (excluding expansions).
    for (const auto &CR : Function.CountedBranchRegions)
      if (FileIDs.test(CR.FileID) && (CR.FileID == CR.ExpandedFileID))
        FileCoverage.BranchRegions.push_back(CR);
    // Capture MCDC records specific to the function.
    for (const auto &MR : Function.MCDCRecords)
      if (FileIDs.test(MR.getDecisionRegion().FileID))
        FileCoverage.MCDCRecords.push_back(MR);
  }

  LLVM_DEBUG(dbgs() << "Emitting segments for file: " << Filename << "\n");
  FileCoverage.Segments = SegmentBuilder::buildSegments(Regions);

  return FileCoverage;
}

std::vector<InstantiationGroup>
CoverageMapping::getInstantiationGroups(StringRef Filename) const {
  FunctionInstantiationSetCollector InstantiationSetCollector;
  // Look up the function records in the given file. Due to hash collisions on
  // the filename, we may get back some records that are not in the file.
  ArrayRef<unsigned> RecordIndices =
      getImpreciseRecordIndicesForFilename(Filename);
  for (unsigned RecordIndex : RecordIndices) {
    const FunctionRecord &Function = Functions[RecordIndex];
    auto MainFileID = findMainViewFileID(Filename, Function);
    if (!MainFileID)
      continue;
    InstantiationSetCollector.insert(Function, *MainFileID);
  }

  std::vector<InstantiationGroup> Result;
  for (auto &InstantiationSet : InstantiationSetCollector) {
    InstantiationGroup IG{InstantiationSet.first.first,
                          InstantiationSet.first.second,
                          std::move(InstantiationSet.second)};
    Result.emplace_back(std::move(IG));
  }
  return Result;
}

CoverageData
CoverageMapping::getCoverageForFunction(const FunctionRecord &Function) const {
  auto MainFileID = findMainViewFileID(Function);
  if (!MainFileID)
    return CoverageData();

  CoverageData FunctionCoverage(Function.Filenames[*MainFileID]);
  std::vector<CountedRegion> Regions;
  for (const auto &CR : Function.CountedRegions)
    if (CR.FileID == *MainFileID) {
      Regions.push_back(CR);
      if (isExpansion(CR, *MainFileID))
        FunctionCoverage.Expansions.emplace_back(CR, Function);
    }
  // Capture branch regions specific to the function (excluding expansions).
  for (const auto &CR : Function.CountedBranchRegions)
    if (CR.FileID == *MainFileID)
      FunctionCoverage.BranchRegions.push_back(CR);

  // Capture MCDC records specific to the function.
  for (const auto &MR : Function.MCDCRecords)
    if (MR.getDecisionRegion().FileID == *MainFileID)
      FunctionCoverage.MCDCRecords.push_back(MR);

  LLVM_DEBUG(dbgs() << "Emitting segments for function: " << Function.Name
                    << "\n");
  FunctionCoverage.Segments = SegmentBuilder::buildSegments(Regions);

  return FunctionCoverage;
}

CoverageData CoverageMapping::getCoverageForExpansion(
    const ExpansionRecord &Expansion) const {
  CoverageData ExpansionCoverage(
      Expansion.Function.Filenames[Expansion.FileID]);
  std::vector<CountedRegion> Regions;
  for (const auto &CR : Expansion.Function.CountedRegions)
    if (CR.FileID == Expansion.FileID) {
      Regions.push_back(CR);
      if (isExpansion(CR, Expansion.FileID))
        ExpansionCoverage.Expansions.emplace_back(CR, Expansion.Function);
    }
  for (const auto &CR : Expansion.Function.CountedBranchRegions)
    // Capture branch regions that only pertain to the corresponding expansion.
    if (CR.FileID == Expansion.FileID)
      ExpansionCoverage.BranchRegions.push_back(CR);

  LLVM_DEBUG(dbgs() << "Emitting segments for expansion of file "
                    << Expansion.FileID << "\n");
  ExpansionCoverage.Segments = SegmentBuilder::buildSegments(Regions);

  return ExpansionCoverage;
}

LineCoverageStats::LineCoverageStats(
    ArrayRef<const CoverageSegment *> LineSegments,
    const CoverageSegment *WrappedSegment, unsigned Line)
    : ExecutionCount(0), HasMultipleRegions(false), Mapped(false), Line(Line),
      LineSegments(LineSegments), WrappedSegment(WrappedSegment) {
  // Find the minimum number of regions which start in this line.
  unsigned MinRegionCount = 0;
  auto isStartOfRegion = [](const CoverageSegment *S) {
    return !S->IsGapRegion && S->HasCount && S->IsRegionEntry;
  };
  for (unsigned I = 0; I < LineSegments.size() && MinRegionCount < 2; ++I)
    if (isStartOfRegion(LineSegments[I]))
      ++MinRegionCount;

  bool StartOfSkippedRegion = !LineSegments.empty() &&
                              !LineSegments.front()->HasCount &&
                              LineSegments.front()->IsRegionEntry;

  HasMultipleRegions = MinRegionCount > 1;
  Mapped =
      !StartOfSkippedRegion &&
      ((WrappedSegment && WrappedSegment->HasCount) || (MinRegionCount > 0));

  // if there is any starting segment at this line with a counter, it must be
  // mapped
  Mapped |= std::any_of(
      LineSegments.begin(), LineSegments.end(),
      [](const auto *Seq) { return Seq->IsRegionEntry && Seq->HasCount; });

  if (!Mapped) {
    return;
  }

  // Pick the max count from the non-gap, region entry segments and the
  // wrapped count.
  if (WrappedSegment)
    ExecutionCount = WrappedSegment->Count;
  if (!MinRegionCount)
    return;
  for (const auto *LS : LineSegments)
    if (isStartOfRegion(LS))
      ExecutionCount = std::max(ExecutionCount, LS->Count);
}

LineCoverageIterator &LineCoverageIterator::operator++() {
  if (Next == CD.end()) {
    Stats = LineCoverageStats();
    Ended = true;
    return *this;
  }
  if (Segments.size())
    WrappedSegment = Segments.back();
  Segments.clear();
  while (Next != CD.end() && Next->Line == Line)
    Segments.push_back(&*Next++);
  Stats = LineCoverageStats(Segments, WrappedSegment, Line);
  ++Line;
  return *this;
}

static std::string getCoverageMapErrString(coveragemap_error Err,
                                           const std::string &ErrMsg = "") {
  std::string Msg;
  raw_string_ostream OS(Msg);

  switch (Err) {
  case coveragemap_error::success:
    OS << "success";
    break;
  case coveragemap_error::eof:
    OS << "end of File";
    break;
  case coveragemap_error::no_data_found:
    OS << "no coverage data found";
    break;
  case coveragemap_error::unsupported_version:
    OS << "unsupported coverage format version";
    break;
  case coveragemap_error::truncated:
    OS << "truncated coverage data";
    break;
  case coveragemap_error::malformed:
    OS << "malformed coverage data";
    break;
  case coveragemap_error::decompression_failed:
    OS << "failed to decompress coverage data (zlib)";
    break;
  case coveragemap_error::invalid_or_missing_arch_specifier:
    OS << "`-arch` specifier is invalid or missing for universal binary";
    break;
  }

  // If optional error message is not empty, append it to the message.
  if (!ErrMsg.empty())
    OS << ": " << ErrMsg;

  return Msg;
}

namespace {

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class CoverageMappingErrorCategoryType : public std::error_category {
  const char *name() const noexcept override { return "llvm.coveragemap"; }
  std::string message(int IE) const override {
    return getCoverageMapErrString(static_cast<coveragemap_error>(IE));
  }
};

} // end anonymous namespace

std::string CoverageMapError::message() const {
  return getCoverageMapErrString(Err, Msg);
}

const std::error_category &llvm::coverage::coveragemap_category() {
  static CoverageMappingErrorCategoryType ErrorCategory;
  return ErrorCategory;
}

char CoverageMapError::ID = 0;
