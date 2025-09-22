//===- Replacement.cpp - Framework for clang refactoring tools ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Implements classes to support/store refactorings.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Core/Replacement.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/RewriteBuffer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace clang;
using namespace tooling;

static const char * const InvalidLocation = "";

Replacement::Replacement() : FilePath(InvalidLocation) {}

Replacement::Replacement(StringRef FilePath, unsigned Offset, unsigned Length,
                         StringRef ReplacementText)
    : FilePath(std::string(FilePath)), ReplacementRange(Offset, Length),
      ReplacementText(std::string(ReplacementText)) {}

Replacement::Replacement(const SourceManager &Sources, SourceLocation Start,
                         unsigned Length, StringRef ReplacementText) {
  setFromSourceLocation(Sources, Start, Length, ReplacementText);
}

Replacement::Replacement(const SourceManager &Sources,
                         const CharSourceRange &Range,
                         StringRef ReplacementText,
                         const LangOptions &LangOpts) {
  setFromSourceRange(Sources, Range, ReplacementText, LangOpts);
}

bool Replacement::isApplicable() const {
  return FilePath != InvalidLocation;
}

bool Replacement::apply(Rewriter &Rewrite) const {
  SourceManager &SM = Rewrite.getSourceMgr();
  auto Entry = SM.getFileManager().getOptionalFileRef(FilePath);
  if (!Entry)
    return false;

  FileID ID = SM.getOrCreateFileID(*Entry, SrcMgr::C_User);
  const SourceLocation Start =
    SM.getLocForStartOfFile(ID).
    getLocWithOffset(ReplacementRange.getOffset());
  // ReplaceText returns false on success.
  // ReplaceText only fails if the source location is not a file location, in
  // which case we already returned false earlier.
  bool RewriteSucceeded = !Rewrite.ReplaceText(
      Start, ReplacementRange.getLength(), ReplacementText);
  assert(RewriteSucceeded);
  return RewriteSucceeded;
}

std::string Replacement::toString() const {
  std::string Result;
  llvm::raw_string_ostream Stream(Result);
  Stream << FilePath << ": " << ReplacementRange.getOffset() << ":+"
         << ReplacementRange.getLength() << ":\"" << ReplacementText << "\"";
  return Stream.str();
}

namespace clang {
namespace tooling {

bool operator<(const Replacement &LHS, const Replacement &RHS) {
  if (LHS.getOffset() != RHS.getOffset())
    return LHS.getOffset() < RHS.getOffset();

  if (LHS.getLength() != RHS.getLength())
    return LHS.getLength() < RHS.getLength();

  if (LHS.getFilePath() != RHS.getFilePath())
    return LHS.getFilePath() < RHS.getFilePath();
  return LHS.getReplacementText() < RHS.getReplacementText();
}

bool operator==(const Replacement &LHS, const Replacement &RHS) {
  return LHS.getOffset() == RHS.getOffset() &&
         LHS.getLength() == RHS.getLength() &&
         LHS.getFilePath() == RHS.getFilePath() &&
         LHS.getReplacementText() == RHS.getReplacementText();
}

} // namespace tooling
} // namespace clang

void Replacement::setFromSourceLocation(const SourceManager &Sources,
                                        SourceLocation Start, unsigned Length,
                                        StringRef ReplacementText) {
  const std::pair<FileID, unsigned> DecomposedLocation =
      Sources.getDecomposedLoc(Start);
  OptionalFileEntryRef Entry =
      Sources.getFileEntryRefForID(DecomposedLocation.first);
  this->FilePath = std::string(Entry ? Entry->getName() : InvalidLocation);
  this->ReplacementRange = Range(DecomposedLocation.second, Length);
  this->ReplacementText = std::string(ReplacementText);
}

// FIXME: This should go into the Lexer, but we need to figure out how
// to handle ranges for refactoring in general first - there is no obvious
// good way how to integrate this into the Lexer yet.
static int getRangeSize(const SourceManager &Sources,
                        const CharSourceRange &Range,
                        const LangOptions &LangOpts) {
  SourceLocation SpellingBegin = Sources.getSpellingLoc(Range.getBegin());
  SourceLocation SpellingEnd = Sources.getSpellingLoc(Range.getEnd());
  std::pair<FileID, unsigned> Start = Sources.getDecomposedLoc(SpellingBegin);
  std::pair<FileID, unsigned> End = Sources.getDecomposedLoc(SpellingEnd);
  if (Start.first != End.first) return -1;
  if (Range.isTokenRange())
    End.second += Lexer::MeasureTokenLength(SpellingEnd, Sources, LangOpts);
  return End.second - Start.second;
}

void Replacement::setFromSourceRange(const SourceManager &Sources,
                                     const CharSourceRange &Range,
                                     StringRef ReplacementText,
                                     const LangOptions &LangOpts) {
  setFromSourceLocation(Sources, Sources.getSpellingLoc(Range.getBegin()),
                        getRangeSize(Sources, Range, LangOpts),
                        ReplacementText);
}

Replacement
Replacements::getReplacementInChangedCode(const Replacement &R) const {
  unsigned NewStart = getShiftedCodePosition(R.getOffset());
  unsigned NewEnd = getShiftedCodePosition(R.getOffset() + R.getLength());
  return Replacement(R.getFilePath(), NewStart, NewEnd - NewStart,
                     R.getReplacementText());
}

static std::string getReplacementErrString(replacement_error Err) {
  switch (Err) {
  case replacement_error::fail_to_apply:
    return "Failed to apply a replacement.";
  case replacement_error::wrong_file_path:
    return "The new replacement's file path is different from the file path of "
           "existing replacements";
  case replacement_error::overlap_conflict:
    return "The new replacement overlaps with an existing replacement.";
  case replacement_error::insert_conflict:
    return "The new insertion has the same insert location as an existing "
           "replacement.";
  }
  llvm_unreachable("A value of replacement_error has no message.");
}

std::string ReplacementError::message() const {
  std::string Message = getReplacementErrString(Err);
  if (NewReplacement)
    Message += "\nNew replacement: " + NewReplacement->toString();
  if (ExistingReplacement)
    Message += "\nExisting replacement: " + ExistingReplacement->toString();
  return Message;
}

char ReplacementError::ID = 0;

Replacements Replacements::getCanonicalReplacements() const {
  std::vector<Replacement> NewReplaces;
  // Merge adjacent replacements.
  for (const auto &R : Replaces) {
    if (NewReplaces.empty()) {
      NewReplaces.push_back(R);
      continue;
    }
    auto &Prev = NewReplaces.back();
    unsigned PrevEnd = Prev.getOffset() + Prev.getLength();
    if (PrevEnd < R.getOffset()) {
      NewReplaces.push_back(R);
    } else {
      assert(PrevEnd == R.getOffset() &&
             "Existing replacements must not overlap.");
      Replacement NewR(
          R.getFilePath(), Prev.getOffset(), Prev.getLength() + R.getLength(),
          (Prev.getReplacementText() + R.getReplacementText()).str());
      Prev = NewR;
    }
  }
  ReplacementsImpl NewReplacesImpl(NewReplaces.begin(), NewReplaces.end());
  return Replacements(NewReplacesImpl.begin(), NewReplacesImpl.end());
}

// `R` and `Replaces` are order-independent if applying them in either order
// has the same effect, so we need to compare replacements associated to
// applying them in either order.
llvm::Expected<Replacements>
Replacements::mergeIfOrderIndependent(const Replacement &R) const {
  Replacements Rs(R);
  // A Replacements set containing a single replacement that is `R` referring to
  // the code after the existing replacements `Replaces` are applied.
  Replacements RsShiftedByReplaces(getReplacementInChangedCode(R));
  // A Replacements set that is `Replaces` referring to the code after `R` is
  // applied.
  Replacements ReplacesShiftedByRs;
  for (const auto &Replace : Replaces)
    ReplacesShiftedByRs.Replaces.insert(
        Rs.getReplacementInChangedCode(Replace));
  // This is equivalent to applying `Replaces` first and then `R`.
  auto MergeShiftedRs = merge(RsShiftedByReplaces);
  // This is equivalent to applying `R` first and then `Replaces`.
  auto MergeShiftedReplaces = Rs.merge(ReplacesShiftedByRs);

  // Since empty or segmented replacements around existing replacements might be
  // produced above, we need to compare replacements in canonical forms.
  if (MergeShiftedRs.getCanonicalReplacements() ==
      MergeShiftedReplaces.getCanonicalReplacements())
    return MergeShiftedRs;
  return llvm::make_error<ReplacementError>(replacement_error::overlap_conflict,
                                            R, *Replaces.begin());
}

llvm::Error Replacements::add(const Replacement &R) {
  // Check the file path.
  if (!Replaces.empty() && R.getFilePath() != Replaces.begin()->getFilePath())
    return llvm::make_error<ReplacementError>(
        replacement_error::wrong_file_path, R, *Replaces.begin());

  // Special-case header insertions.
  if (R.getOffset() == std::numeric_limits<unsigned>::max()) {
    Replaces.insert(R);
    return llvm::Error::success();
  }

  // This replacement cannot conflict with replacements that end before
  // this replacement starts or start after this replacement ends.
  // We also know that there currently are no overlapping replacements.
  // Thus, we know that all replacements that start after the end of the current
  // replacement cannot overlap.
  Replacement AtEnd(R.getFilePath(), R.getOffset() + R.getLength(), 0, "");

  // Find the first entry that starts after or at the end of R. Note that
  // entries that start at the end can still be conflicting if R is an
  // insertion.
  auto I = Replaces.lower_bound(AtEnd);
  // If `I` starts at the same offset as `R`, `R` must be an insertion.
  if (I != Replaces.end() && R.getOffset() == I->getOffset()) {
    assert(R.getLength() == 0);
    // `I` is also an insertion, `R` and `I` conflict.
    if (I->getLength() == 0) {
      // Check if two insertions are order-independent: if inserting them in
      // either order produces the same text, they are order-independent.
      if ((R.getReplacementText() + I->getReplacementText()).str() !=
          (I->getReplacementText() + R.getReplacementText()).str())
        return llvm::make_error<ReplacementError>(
            replacement_error::insert_conflict, R, *I);
      // If insertions are order-independent, we can merge them.
      Replacement NewR(
          R.getFilePath(), R.getOffset(), 0,
          (R.getReplacementText() + I->getReplacementText()).str());
      Replaces.erase(I);
      Replaces.insert(std::move(NewR));
      return llvm::Error::success();
    }
    // Insertion `R` is adjacent to a non-insertion replacement `I`, so they
    // are order-independent. It is safe to assume that `R` will not conflict
    // with any replacement before `I` since all replacements before `I` must
    // either end before `R` or end at `R` but has length > 0 (if the
    // replacement before `I` is an insertion at `R`, it would have been `I`
    // since it is a lower bound of `AtEnd` and ordered before the current `I`
    // in the set).
    Replaces.insert(R);
    return llvm::Error::success();
  }

  // `I` is the smallest iterator (after `R`) whose entry cannot overlap.
  // If that is begin(), there are no overlaps.
  if (I == Replaces.begin()) {
    Replaces.insert(R);
    return llvm::Error::success();
  }
  --I;
  auto Overlap = [](const Replacement &R1, const Replacement &R2) -> bool {
    return Range(R1.getOffset(), R1.getLength())
        .overlapsWith(Range(R2.getOffset(), R2.getLength()));
  };
  // If the previous entry does not overlap, we know that entries before it
  // can also not overlap.
  if (!Overlap(R, *I)) {
    // If `R` and `I` do not have the same offset, it is safe to add `R` since
    // it must come after `I`. Otherwise:
    //   - If `R` is an insertion, `I` must not be an insertion since it would
    //   have come after `AtEnd`.
    //   - If `R` is not an insertion, `I` must be an insertion; otherwise, `R`
    //   and `I` would have overlapped.
    // In either case, we can safely insert `R`.
    Replaces.insert(R);
  } else {
    // `I` overlaps with `R`. We need to check `R` against all overlapping
    // replacements to see if they are order-independent. If they are, merge `R`
    // with them and replace them with the merged replacements.
    auto MergeBegin = I;
    auto MergeEnd = std::next(I);
    while (I != Replaces.begin()) {
      --I;
      // If `I` doesn't overlap with `R`, don't merge it.
      if (!Overlap(R, *I))
        break;
      MergeBegin = I;
    }
    Replacements OverlapReplaces(MergeBegin, MergeEnd);
    llvm::Expected<Replacements> Merged =
        OverlapReplaces.mergeIfOrderIndependent(R);
    if (!Merged)
      return Merged.takeError();
    Replaces.erase(MergeBegin, MergeEnd);
    Replaces.insert(Merged->begin(), Merged->end());
  }
  return llvm::Error::success();
}

namespace {

// Represents a merged replacement, i.e. a replacement consisting of multiple
// overlapping replacements from 'First' and 'Second' in mergeReplacements.
//
// Position projection:
// Offsets and lengths of the replacements can generally refer to two different
// coordinate spaces. Replacements from 'First' refer to the original text
// whereas replacements from 'Second' refer to the text after applying 'First'.
//
// MergedReplacement always operates in the coordinate space of the original
// text, i.e. transforms elements from 'Second' to take into account what was
// changed based on the elements from 'First'.
//
// We can correctly calculate this projection as we look at the replacements in
// order of strictly increasing offsets.
//
// Invariants:
// * We always merge elements from 'First' into elements from 'Second' and vice
//   versa. Within each set, the replacements are non-overlapping.
// * We only extend to the right, i.e. merge elements with strictly increasing
//   offsets.
class MergedReplacement {
public:
  MergedReplacement(const Replacement &R, bool MergeSecond, int D)
      : MergeSecond(MergeSecond), Delta(D), FilePath(R.getFilePath()),
        Offset(R.getOffset() + (MergeSecond ? 0 : Delta)),
        Length(R.getLength()), Text(std::string(R.getReplacementText())) {
    Delta += MergeSecond ? 0 : Text.size() - Length;
    DeltaFirst = MergeSecond ? Text.size() - Length : 0;
  }

  // Merges the next element 'R' into this merged element. As we always merge
  // from 'First' into 'Second' or vice versa, the MergedReplacement knows what
  // set the next element is coming from.
  void merge(const Replacement &R) {
    if (MergeSecond) {
      unsigned REnd = R.getOffset() + Delta + R.getLength();
      unsigned End = Offset + Text.size();
      if (REnd > End) {
        Length += REnd - End;
        MergeSecond = false;
      }
      StringRef TextRef = Text;
      StringRef Head = TextRef.substr(0, R.getOffset() + Delta - Offset);
      StringRef Tail = TextRef.substr(REnd - Offset);
      Text = (Head + R.getReplacementText() + Tail).str();
      Delta += R.getReplacementText().size() - R.getLength();
    } else {
      unsigned End = Offset + Length;
      StringRef RText = R.getReplacementText();
      StringRef Tail = RText.substr(End - R.getOffset());
      Text = (Text + Tail).str();
      if (R.getOffset() + RText.size() > End) {
        Length = R.getOffset() + R.getLength() - Offset;
        MergeSecond = true;
      } else {
        Length += R.getLength() - RText.size();
      }
      DeltaFirst += RText.size() - R.getLength();
    }
  }

  // Returns 'true' if 'R' starts strictly after the MergedReplacement and thus
  // doesn't need to be merged.
  bool endsBefore(const Replacement &R) const {
    if (MergeSecond)
      return Offset + Text.size() < R.getOffset() + Delta;
    return Offset + Length < R.getOffset();
  }

  // Returns 'true' if an element from the second set should be merged next.
  bool mergeSecond() const { return MergeSecond; }

  int deltaFirst() const { return DeltaFirst; }
  Replacement asReplacement() const { return {FilePath, Offset, Length, Text}; }

private:
  bool MergeSecond;

  // Amount of characters that elements from 'Second' need to be shifted by in
  // order to refer to the original text.
  int Delta;

  // Sum of all deltas (text-length - length) of elements from 'First' merged
  // into this element. This is used to update 'Delta' once the
  // MergedReplacement is completed.
  int DeltaFirst;

  // Data of the actually merged replacement. FilePath and Offset aren't changed
  // as the element is only extended to the right.
  const StringRef FilePath;
  const unsigned Offset;
  unsigned Length;
  std::string Text;
};

} // namespace

Replacements Replacements::merge(const Replacements &ReplacesToMerge) const {
  if (empty() || ReplacesToMerge.empty())
    return empty() ? ReplacesToMerge : *this;

  auto &First = Replaces;
  auto &Second = ReplacesToMerge.Replaces;
  // Delta is the amount of characters that replacements from 'Second' need to
  // be shifted so that their offsets refer to the original text.
  int Delta = 0;
  ReplacementsImpl Result;

  // Iterate over both sets and always add the next element (smallest total
  // Offset) from either 'First' or 'Second'. Merge that element with
  // subsequent replacements as long as they overlap. See more details in the
  // comment on MergedReplacement.
  for (auto FirstI = First.begin(), SecondI = Second.begin();
       FirstI != First.end() || SecondI != Second.end();) {
    bool NextIsFirst = SecondI == Second.end() ||
                       (FirstI != First.end() &&
                        FirstI->getOffset() < SecondI->getOffset() + Delta);
    MergedReplacement Merged(NextIsFirst ? *FirstI : *SecondI, NextIsFirst,
                             Delta);
    ++(NextIsFirst ? FirstI : SecondI);

    while ((Merged.mergeSecond() && SecondI != Second.end()) ||
           (!Merged.mergeSecond() && FirstI != First.end())) {
      auto &I = Merged.mergeSecond() ? SecondI : FirstI;
      if (Merged.endsBefore(*I))
        break;
      Merged.merge(*I);
      ++I;
    }
    Delta -= Merged.deltaFirst();
    Result.insert(Merged.asReplacement());
  }
  return Replacements(Result.begin(), Result.end());
}

// Combines overlapping ranges in \p Ranges and sorts the combined ranges.
// Returns a set of non-overlapping and sorted ranges that is equivalent to
// \p Ranges.
static std::vector<Range> combineAndSortRanges(std::vector<Range> Ranges) {
  llvm::sort(Ranges, [](const Range &LHS, const Range &RHS) {
    if (LHS.getOffset() != RHS.getOffset())
      return LHS.getOffset() < RHS.getOffset();
    return LHS.getLength() < RHS.getLength();
  });
  std::vector<Range> Result;
  for (const auto &R : Ranges) {
    if (Result.empty() ||
        Result.back().getOffset() + Result.back().getLength() < R.getOffset()) {
      Result.push_back(R);
    } else {
      unsigned NewEnd =
          std::max(Result.back().getOffset() + Result.back().getLength(),
                   R.getOffset() + R.getLength());
      Result[Result.size() - 1] =
          Range(Result.back().getOffset(), NewEnd - Result.back().getOffset());
    }
  }
  return Result;
}

namespace clang {
namespace tooling {

std::vector<Range>
calculateRangesAfterReplacements(const Replacements &Replaces,
                                 const std::vector<Range> &Ranges) {
  // To calculate the new ranges,
  //   - Turn \p Ranges into Replacements at (offset, length) with an empty
  //     (unimportant) replacement text of length "length".
  //   - Merge with \p Replaces.
  //   - The new ranges will be the affected ranges of the merged replacements.
  auto MergedRanges = combineAndSortRanges(Ranges);
  if (Replaces.empty())
    return MergedRanges;
  tooling::Replacements FakeReplaces;
  for (const auto &R : MergedRanges) {
    llvm::cantFail(
        FakeReplaces.add(Replacement(Replaces.begin()->getFilePath(),
                                     R.getOffset(), R.getLength(),
                                     std::string(R.getLength(), ' '))),
        "Replacements must not conflict since ranges have been merged.");
  }
  return FakeReplaces.merge(Replaces).getAffectedRanges();
}

} // namespace tooling
} // namespace clang

std::vector<Range> Replacements::getAffectedRanges() const {
  std::vector<Range> ChangedRanges;
  int Shift = 0;
  for (const auto &R : Replaces) {
    unsigned Offset = R.getOffset() + Shift;
    unsigned Length = R.getReplacementText().size();
    Shift += Length - R.getLength();
    ChangedRanges.push_back(Range(Offset, Length));
  }
  return combineAndSortRanges(ChangedRanges);
}

unsigned Replacements::getShiftedCodePosition(unsigned Position) const {
  unsigned Offset = 0;
  for (const auto &R : Replaces) {
    if (R.getOffset() + R.getLength() <= Position) {
      Offset += R.getReplacementText().size() - R.getLength();
      continue;
    }
    if (R.getOffset() < Position &&
        R.getOffset() + R.getReplacementText().size() <= Position) {
      Position = R.getOffset() + R.getReplacementText().size();
      if (!R.getReplacementText().empty())
        Position--;
    }
    break;
  }
  return Position + Offset;
}

namespace clang {
namespace tooling {

bool applyAllReplacements(const Replacements &Replaces, Rewriter &Rewrite) {
  bool Result = true;
  for (auto I = Replaces.rbegin(), E = Replaces.rend(); I != E; ++I) {
    if (I->isApplicable()) {
      Result = I->apply(Rewrite) && Result;
    } else {
      Result = false;
    }
  }
  return Result;
}

llvm::Expected<std::string> applyAllReplacements(StringRef Code,
                                                const Replacements &Replaces) {
  if (Replaces.empty())
    return Code.str();

  IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem(
      new llvm::vfs::InMemoryFileSystem);
  FileManager Files(FileSystemOptions(), InMemoryFileSystem);
  DiagnosticsEngine Diagnostics(
      IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs),
      new DiagnosticOptions);
  SourceManager SourceMgr(Diagnostics, Files);
  Rewriter Rewrite(SourceMgr, LangOptions());
  InMemoryFileSystem->addFile(
      "<stdin>", 0, llvm::MemoryBuffer::getMemBuffer(Code, "<stdin>"));
  FileID ID = SourceMgr.createFileID(*Files.getOptionalFileRef("<stdin>"),
                                     SourceLocation(),
                                     clang::SrcMgr::C_User);
  for (auto I = Replaces.rbegin(), E = Replaces.rend(); I != E; ++I) {
    Replacement Replace("<stdin>", I->getOffset(), I->getLength(),
                        I->getReplacementText());
    if (!Replace.apply(Rewrite))
      return llvm::make_error<ReplacementError>(
          replacement_error::fail_to_apply, Replace);
  }
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  Rewrite.getEditBuffer(ID).write(OS);
  OS.flush();
  return Result;
}

std::map<std::string, Replacements> groupReplacementsByFile(
    FileManager &FileMgr,
    const std::map<std::string, Replacements> &FileToReplaces) {
  std::map<std::string, Replacements> Result;
  llvm::SmallPtrSet<const FileEntry *, 16> ProcessedFileEntries;
  for (const auto &Entry : FileToReplaces) {
    auto FE = FileMgr.getFile(Entry.first);
    if (!FE)
      llvm::errs() << "File path " << Entry.first << " is invalid.\n";
    else if (ProcessedFileEntries.insert(*FE).second)
      Result[Entry.first] = std::move(Entry.second);
  }
  return Result;
}

} // namespace tooling
} // namespace clang
