//===--- CloneDetection.cpp - Finds code clones in an AST -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file implements classes for searching and analyzing source code clones.
///
//===----------------------------------------------------------------------===//

#include "clang/Analysis/CloneDetection.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DataCollection.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/Path.h"

using namespace clang;

StmtSequence::StmtSequence(const CompoundStmt *Stmt, const Decl *D,
                           unsigned StartIndex, unsigned EndIndex)
    : S(Stmt), D(D), StartIndex(StartIndex), EndIndex(EndIndex) {
  assert(Stmt && "Stmt must not be a nullptr");
  assert(StartIndex < EndIndex && "Given array should not be empty");
  assert(EndIndex <= Stmt->size() && "Given array too big for this Stmt");
}

StmtSequence::StmtSequence(const Stmt *Stmt, const Decl *D)
    : S(Stmt), D(D), StartIndex(0), EndIndex(0) {}

StmtSequence::StmtSequence()
    : S(nullptr), D(nullptr), StartIndex(0), EndIndex(0) {}

bool StmtSequence::contains(const StmtSequence &Other) const {
  // If both sequences reside in different declarations, they can never contain
  // each other.
  if (D != Other.D)
    return false;

  const SourceManager &SM = getASTContext().getSourceManager();

  // Otherwise check if the start and end locations of the current sequence
  // surround the other sequence.
  bool StartIsInBounds =
      SM.isBeforeInTranslationUnit(getBeginLoc(), Other.getBeginLoc()) ||
      getBeginLoc() == Other.getBeginLoc();
  if (!StartIsInBounds)
    return false;

  bool EndIsInBounds =
      SM.isBeforeInTranslationUnit(Other.getEndLoc(), getEndLoc()) ||
      Other.getEndLoc() == getEndLoc();
  return EndIsInBounds;
}

StmtSequence::iterator StmtSequence::begin() const {
  if (!holdsSequence()) {
    return &S;
  }
  auto CS = cast<CompoundStmt>(S);
  return CS->body_begin() + StartIndex;
}

StmtSequence::iterator StmtSequence::end() const {
  if (!holdsSequence()) {
    return reinterpret_cast<StmtSequence::iterator>(&S) + 1;
  }
  auto CS = cast<CompoundStmt>(S);
  return CS->body_begin() + EndIndex;
}

ASTContext &StmtSequence::getASTContext() const {
  assert(D);
  return D->getASTContext();
}

SourceLocation StmtSequence::getBeginLoc() const {
  return front()->getBeginLoc();
}

SourceLocation StmtSequence::getEndLoc() const { return back()->getEndLoc(); }

SourceRange StmtSequence::getSourceRange() const {
  return SourceRange(getBeginLoc(), getEndLoc());
}

void CloneDetector::analyzeCodeBody(const Decl *D) {
  assert(D);
  assert(D->hasBody());

  Sequences.push_back(StmtSequence(D->getBody(), D));
}

/// Returns true if and only if \p Stmt contains at least one other
/// sequence in the \p Group.
static bool containsAnyInGroup(StmtSequence &Seq,
                               CloneDetector::CloneGroup &Group) {
  for (StmtSequence &GroupSeq : Group) {
    if (Seq.contains(GroupSeq))
      return true;
  }
  return false;
}

/// Returns true if and only if all sequences in \p OtherGroup are
/// contained by a sequence in \p Group.
static bool containsGroup(CloneDetector::CloneGroup &Group,
                          CloneDetector::CloneGroup &OtherGroup) {
  // We have less sequences in the current group than we have in the other,
  // so we will never fulfill the requirement for returning true. This is only
  // possible because we know that a sequence in Group can contain at most
  // one sequence in OtherGroup.
  if (Group.size() < OtherGroup.size())
    return false;

  for (StmtSequence &Stmt : Group) {
    if (!containsAnyInGroup(Stmt, OtherGroup))
      return false;
  }
  return true;
}

void OnlyLargestCloneConstraint::constrain(
    std::vector<CloneDetector::CloneGroup> &Result) {
  std::vector<unsigned> IndexesToRemove;

  // Compare every group in the result with the rest. If one groups contains
  // another group, we only need to return the bigger group.
  // Note: This doesn't scale well, so if possible avoid calling any heavy
  // function from this loop to minimize the performance impact.
  for (unsigned i = 0; i < Result.size(); ++i) {
    for (unsigned j = 0; j < Result.size(); ++j) {
      // Don't compare a group with itself.
      if (i == j)
        continue;

      if (containsGroup(Result[j], Result[i])) {
        IndexesToRemove.push_back(i);
        break;
      }
    }
  }

  // Erasing a list of indexes from the vector should be done with decreasing
  // indexes. As IndexesToRemove is constructed with increasing values, we just
  // reverse iterate over it to get the desired order.
  for (unsigned I : llvm::reverse(IndexesToRemove))
    Result.erase(Result.begin() + I);
}

bool FilenamePatternConstraint::isAutoGenerated(
    const CloneDetector::CloneGroup &Group) {
  if (IgnoredFilesPattern.empty() || Group.empty() ||
      !IgnoredFilesRegex->isValid())
    return false;

  for (const StmtSequence &S : Group) {
    const SourceManager &SM = S.getASTContext().getSourceManager();
    StringRef Filename = llvm::sys::path::filename(
        SM.getFilename(S.getContainingDecl()->getLocation()));
    if (IgnoredFilesRegex->match(Filename))
      return true;
  }

  return false;
}

/// This class defines what a type II code clone is: If it collects for two
/// statements the same data, then those two statements are considered to be
/// clones of each other.
///
/// All collected data is forwarded to the given data consumer of the type T.
/// The data consumer class needs to provide a member method with the signature:
///   update(StringRef Str)
namespace {
template <class T>
class CloneTypeIIStmtDataCollector
    : public ConstStmtVisitor<CloneTypeIIStmtDataCollector<T>> {
  ASTContext &Context;
  /// The data sink to which all data is forwarded.
  T &DataConsumer;

  template <class Ty> void addData(const Ty &Data) {
    data_collection::addDataToConsumer(DataConsumer, Data);
  }

public:
  CloneTypeIIStmtDataCollector(const Stmt *S, ASTContext &Context,
                               T &DataConsumer)
      : Context(Context), DataConsumer(DataConsumer) {
    this->Visit(S);
  }

// Define a visit method for each class to collect data and subsequently visit
// all parent classes. This uses a template so that custom visit methods by us
// take precedence.
#define DEF_ADD_DATA(CLASS, CODE)                                              \
  template <class = void> void Visit##CLASS(const CLASS *S) {                  \
    CODE;                                                                      \
    ConstStmtVisitor<CloneTypeIIStmtDataCollector<T>>::Visit##CLASS(S);        \
  }

#include "clang/AST/StmtDataCollectors.inc"

// Type II clones ignore variable names and literals, so let's skip them.
#define SKIP(CLASS)                                                            \
  void Visit##CLASS(const CLASS *S) {                                          \
    ConstStmtVisitor<CloneTypeIIStmtDataCollector<T>>::Visit##CLASS(S);        \
  }
  SKIP(DeclRefExpr)
  SKIP(MemberExpr)
  SKIP(IntegerLiteral)
  SKIP(FloatingLiteral)
  SKIP(StringLiteral)
  SKIP(CXXBoolLiteralExpr)
  SKIP(CharacterLiteral)
#undef SKIP
};
} // end anonymous namespace

static size_t createHash(llvm::MD5 &Hash) {
  size_t HashCode;

  // Create the final hash code for the current Stmt.
  llvm::MD5::MD5Result HashResult;
  Hash.final(HashResult);

  // Copy as much as possible of the generated hash code to the Stmt's hash
  // code.
  std::memcpy(&HashCode, &HashResult,
              std::min(sizeof(HashCode), sizeof(HashResult)));

  return HashCode;
}

/// Generates and saves a hash code for the given Stmt.
/// \param S The given Stmt.
/// \param D The Decl containing S.
/// \param StmtsByHash Output parameter that will contain the hash codes for
///                    each StmtSequence in the given Stmt.
/// \return The hash code of the given Stmt.
///
/// If the given Stmt is a CompoundStmt, this method will also generate
/// hashes for all possible StmtSequences in the children of this Stmt.
static size_t
saveHash(const Stmt *S, const Decl *D,
         std::vector<std::pair<size_t, StmtSequence>> &StmtsByHash) {
  llvm::MD5 Hash;
  ASTContext &Context = D->getASTContext();

  CloneTypeIIStmtDataCollector<llvm::MD5>(S, Context, Hash);

  auto CS = dyn_cast<CompoundStmt>(S);
  SmallVector<size_t, 8> ChildHashes;

  for (const Stmt *Child : S->children()) {
    if (Child == nullptr) {
      ChildHashes.push_back(0);
      continue;
    }
    size_t ChildHash = saveHash(Child, D, StmtsByHash);
    Hash.update(
        StringRef(reinterpret_cast<char *>(&ChildHash), sizeof(ChildHash)));
    ChildHashes.push_back(ChildHash);
  }

  if (CS) {
    // If we're in a CompoundStmt, we hash all possible combinations of child
    // statements to find clones in those subsequences.
    // We first go through every possible starting position of a subsequence.
    for (unsigned Pos = 0; Pos < CS->size(); ++Pos) {
      // Then we try all possible lengths this subsequence could have and
      // reuse the same hash object to make sure we only hash every child
      // hash exactly once.
      llvm::MD5 Hash;
      for (unsigned Length = 1; Length <= CS->size() - Pos; ++Length) {
        // Grab the current child hash and put it into our hash. We do
        // -1 on the index because we start counting the length at 1.
        size_t ChildHash = ChildHashes[Pos + Length - 1];
        Hash.update(
            StringRef(reinterpret_cast<char *>(&ChildHash), sizeof(ChildHash)));
        // If we have at least two elements in our subsequence, we can start
        // saving it.
        if (Length > 1) {
          llvm::MD5 SubHash = Hash;
          StmtsByHash.push_back(std::make_pair(
              createHash(SubHash), StmtSequence(CS, D, Pos, Pos + Length)));
        }
      }
    }
  }

  size_t HashCode = createHash(Hash);
  StmtsByHash.push_back(std::make_pair(HashCode, StmtSequence(S, D)));
  return HashCode;
}

namespace {
/// Wrapper around FoldingSetNodeID that it can be used as the template
/// argument of the StmtDataCollector.
class FoldingSetNodeIDWrapper {

  llvm::FoldingSetNodeID &FS;

public:
  FoldingSetNodeIDWrapper(llvm::FoldingSetNodeID &FS) : FS(FS) {}

  void update(StringRef Str) { FS.AddString(Str); }
};
} // end anonymous namespace

/// Writes the relevant data from all statements and child statements
/// in the given StmtSequence into the given FoldingSetNodeID.
static void CollectStmtSequenceData(const StmtSequence &Sequence,
                                    FoldingSetNodeIDWrapper &OutputData) {
  for (const Stmt *S : Sequence) {
    CloneTypeIIStmtDataCollector<FoldingSetNodeIDWrapper>(
        S, Sequence.getASTContext(), OutputData);

    for (const Stmt *Child : S->children()) {
      if (!Child)
        continue;

      CollectStmtSequenceData(StmtSequence(Child, Sequence.getContainingDecl()),
                              OutputData);
    }
  }
}

/// Returns true if both sequences are clones of each other.
static bool areSequencesClones(const StmtSequence &LHS,
                               const StmtSequence &RHS) {
  // We collect the data from all statements in the sequence as we did before
  // when generating a hash value for each sequence. But this time we don't
  // hash the collected data and compare the whole data set instead. This
  // prevents any false-positives due to hash code collisions.
  llvm::FoldingSetNodeID DataLHS, DataRHS;
  FoldingSetNodeIDWrapper LHSWrapper(DataLHS);
  FoldingSetNodeIDWrapper RHSWrapper(DataRHS);

  CollectStmtSequenceData(LHS, LHSWrapper);
  CollectStmtSequenceData(RHS, RHSWrapper);

  return DataLHS == DataRHS;
}

void RecursiveCloneTypeIIHashConstraint::constrain(
    std::vector<CloneDetector::CloneGroup> &Sequences) {
  // FIXME: Maybe we can do this in-place and don't need this additional vector.
  std::vector<CloneDetector::CloneGroup> Result;

  for (CloneDetector::CloneGroup &Group : Sequences) {
    // We assume in the following code that the Group is non-empty, so we
    // skip all empty groups.
    if (Group.empty())
      continue;

    std::vector<std::pair<size_t, StmtSequence>> StmtsByHash;

    // Generate hash codes for all children of S and save them in StmtsByHash.
    for (const StmtSequence &S : Group) {
      saveHash(S.front(), S.getContainingDecl(), StmtsByHash);
    }

    // Sort hash_codes in StmtsByHash.
    llvm::stable_sort(StmtsByHash, llvm::less_first());

    // Check for each StmtSequence if its successor has the same hash value.
    // We don't check the last StmtSequence as it has no successor.
    // Note: The 'size - 1 ' in the condition is safe because we check for an
    // empty Group vector at the beginning of this function.
    for (unsigned i = 0; i < StmtsByHash.size() - 1; ++i) {
      const auto Current = StmtsByHash[i];

      // It's likely that we just found a sequence of StmtSequences that
      // represent a CloneGroup, so we create a new group and start checking and
      // adding the StmtSequences in this sequence.
      CloneDetector::CloneGroup NewGroup;

      size_t PrototypeHash = Current.first;

      for (; i < StmtsByHash.size(); ++i) {
        // A different hash value means we have reached the end of the sequence.
        if (PrototypeHash != StmtsByHash[i].first) {
          // The current sequence could be the start of a new CloneGroup. So we
          // decrement i so that we visit it again in the outer loop.
          // Note: i can never be 0 at this point because we are just comparing
          // the hash of the Current StmtSequence with itself in the 'if' above.
          assert(i != 0);
          --i;
          break;
        }
        // Same hash value means we should add the StmtSequence to the current
        // group.
        NewGroup.push_back(StmtsByHash[i].second);
      }

      // We created a new clone group with matching hash codes and move it to
      // the result vector.
      Result.push_back(NewGroup);
    }
  }
  // Sequences is the output parameter, so we copy our result into it.
  Sequences = Result;
}

void RecursiveCloneTypeIIVerifyConstraint::constrain(
    std::vector<CloneDetector::CloneGroup> &Sequences) {
  CloneConstraint::splitCloneGroups(
      Sequences, [](const StmtSequence &A, const StmtSequence &B) {
        return areSequencesClones(A, B);
      });
}

size_t MinComplexityConstraint::calculateStmtComplexity(
    const StmtSequence &Seq, std::size_t Limit,
    const std::string &ParentMacroStack) {
  if (Seq.empty())
    return 0;

  size_t Complexity = 1;

  ASTContext &Context = Seq.getASTContext();

  // Look up what macros expanded into the current statement.
  std::string MacroStack =
      data_collection::getMacroStack(Seq.getBeginLoc(), Context);

  // First, check if ParentMacroStack is not empty which means we are currently
  // dealing with a parent statement which was expanded from a macro.
  // If this parent statement was expanded from the same macros as this
  // statement, we reduce the initial complexity of this statement to zero.
  // This causes that a group of statements that were generated by a single
  // macro expansion will only increase the total complexity by one.
  // Note: This is not the final complexity of this statement as we still
  // add the complexity of the child statements to the complexity value.
  if (!ParentMacroStack.empty() && MacroStack == ParentMacroStack) {
    Complexity = 0;
  }

  // Iterate over the Stmts in the StmtSequence and add their complexity values
  // to the current complexity value.
  if (Seq.holdsSequence()) {
    for (const Stmt *S : Seq) {
      Complexity += calculateStmtComplexity(
          StmtSequence(S, Seq.getContainingDecl()), Limit, MacroStack);
      if (Complexity >= Limit)
        return Limit;
    }
  } else {
    for (const Stmt *S : Seq.front()->children()) {
      Complexity += calculateStmtComplexity(
          StmtSequence(S, Seq.getContainingDecl()), Limit, MacroStack);
      if (Complexity >= Limit)
        return Limit;
    }
  }
  return Complexity;
}

void MatchingVariablePatternConstraint::constrain(
    std::vector<CloneDetector::CloneGroup> &CloneGroups) {
  CloneConstraint::splitCloneGroups(
      CloneGroups, [](const StmtSequence &A, const StmtSequence &B) {
        VariablePattern PatternA(A);
        VariablePattern PatternB(B);
        return PatternA.countPatternDifferences(PatternB) == 0;
      });
}

void CloneConstraint::splitCloneGroups(
    std::vector<CloneDetector::CloneGroup> &CloneGroups,
    llvm::function_ref<bool(const StmtSequence &, const StmtSequence &)>
        Compare) {
  std::vector<CloneDetector::CloneGroup> Result;
  for (auto &HashGroup : CloneGroups) {
    // Contains all indexes in HashGroup that were already added to a
    // CloneGroup.
    std::vector<char> Indexes;
    Indexes.resize(HashGroup.size());

    for (unsigned i = 0; i < HashGroup.size(); ++i) {
      // Skip indexes that are already part of a CloneGroup.
      if (Indexes[i])
        continue;

      // Pick the first unhandled StmtSequence and consider it as the
      // beginning
      // of a new CloneGroup for now.
      // We don't add i to Indexes because we never iterate back.
      StmtSequence Prototype = HashGroup[i];
      CloneDetector::CloneGroup PotentialGroup = {Prototype};
      ++Indexes[i];

      // Check all following StmtSequences for clones.
      for (unsigned j = i + 1; j < HashGroup.size(); ++j) {
        // Skip indexes that are already part of a CloneGroup.
        if (Indexes[j])
          continue;

        // If a following StmtSequence belongs to our CloneGroup, we add it.
        const StmtSequence &Candidate = HashGroup[j];

        if (!Compare(Prototype, Candidate))
          continue;

        PotentialGroup.push_back(Candidate);
        // Make sure we never visit this StmtSequence again.
        ++Indexes[j];
      }

      // Otherwise, add it to the result and continue searching for more
      // groups.
      Result.push_back(PotentialGroup);
    }

    assert(llvm::all_of(Indexes, [](char c) { return c == 1; }));
  }
  CloneGroups = Result;
}

void VariablePattern::addVariableOccurence(const VarDecl *VarDecl,
                                           const Stmt *Mention) {
  // First check if we already reference this variable
  for (size_t KindIndex = 0; KindIndex < Variables.size(); ++KindIndex) {
    if (Variables[KindIndex] == VarDecl) {
      // If yes, add a new occurrence that points to the existing entry in
      // the Variables vector.
      Occurences.emplace_back(KindIndex, Mention);
      return;
    }
  }
  // If this variable wasn't already referenced, add it to the list of
  // referenced variables and add a occurrence that points to this new entry.
  Occurences.emplace_back(Variables.size(), Mention);
  Variables.push_back(VarDecl);
}

void VariablePattern::addVariables(const Stmt *S) {
  // Sometimes we get a nullptr (such as from IfStmts which often have nullptr
  // children). We skip such statements as they don't reference any
  // variables.
  if (!S)
    return;

  // Check if S is a reference to a variable. If yes, add it to the pattern.
  if (auto D = dyn_cast<DeclRefExpr>(S)) {
    if (auto VD = dyn_cast<VarDecl>(D->getDecl()->getCanonicalDecl()))
      addVariableOccurence(VD, D);
  }

  // Recursively check all children of the given statement.
  for (const Stmt *Child : S->children()) {
    addVariables(Child);
  }
}

unsigned VariablePattern::countPatternDifferences(
    const VariablePattern &Other,
    VariablePattern::SuspiciousClonePair *FirstMismatch) {
  unsigned NumberOfDifferences = 0;

  assert(Other.Occurences.size() == Occurences.size());
  for (unsigned i = 0; i < Occurences.size(); ++i) {
    auto ThisOccurence = Occurences[i];
    auto OtherOccurence = Other.Occurences[i];
    if (ThisOccurence.KindID == OtherOccurence.KindID)
      continue;

    ++NumberOfDifferences;

    // If FirstMismatch is not a nullptr, we need to store information about
    // the first difference between the two patterns.
    if (FirstMismatch == nullptr)
      continue;

    // Only proceed if we just found the first difference as we only store
    // information about the first difference.
    if (NumberOfDifferences != 1)
      continue;

    const VarDecl *FirstSuggestion = nullptr;
    // If there is a variable available in the list of referenced variables
    // which wouldn't break the pattern if it is used in place of the
    // current variable, we provide this variable as the suggested fix.
    if (OtherOccurence.KindID < Variables.size())
      FirstSuggestion = Variables[OtherOccurence.KindID];

    // Store information about the first clone.
    FirstMismatch->FirstCloneInfo =
        VariablePattern::SuspiciousClonePair::SuspiciousCloneInfo(
            Variables[ThisOccurence.KindID], ThisOccurence.Mention,
            FirstSuggestion);

    // Same as above but with the other clone. We do this for both clones as
    // we don't know which clone is the one containing the unintended
    // pattern error.
    const VarDecl *SecondSuggestion = nullptr;
    if (ThisOccurence.KindID < Other.Variables.size())
      SecondSuggestion = Other.Variables[ThisOccurence.KindID];

    // Store information about the second clone.
    FirstMismatch->SecondCloneInfo =
        VariablePattern::SuspiciousClonePair::SuspiciousCloneInfo(
            Other.Variables[OtherOccurence.KindID], OtherOccurence.Mention,
            SecondSuggestion);

    // SuspiciousClonePair guarantees that the first clone always has a
    // suggested variable associated with it. As we know that one of the two
    // clones in the pair always has suggestion, we swap the two clones
    // in case the first clone has no suggested variable which means that
    // the second clone has a suggested variable and should be first.
    if (!FirstMismatch->FirstCloneInfo.Suggestion)
      std::swap(FirstMismatch->FirstCloneInfo, FirstMismatch->SecondCloneInfo);

    // This ensures that we always have at least one suggestion in a pair.
    assert(FirstMismatch->FirstCloneInfo.Suggestion);
  }

  return NumberOfDifferences;
}
