//===--- CoverageMappingGen.cpp - Coverage mapping generation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Instrumentation-based code coverage mapping generator
//
//===----------------------------------------------------------------------===//

#include "CoverageMappingGen.h"
#include "CodeGenFunction.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include "llvm/ProfileData/Coverage/CoverageMappingReader.h"
#include "llvm/ProfileData/Coverage/CoverageMappingWriter.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <optional>

// This selects the coverage mapping format defined when `InstrProfData.inc`
// is textually included.
#define COVMAP_V3

namespace llvm {
cl::opt<bool>
    EnableSingleByteCoverage("enable-single-byte-coverage",
                             llvm::cl::ZeroOrMore,
                             llvm::cl::desc("Enable single byte coverage"),
                             llvm::cl::Hidden, llvm::cl::init(false));
} // namespace llvm

static llvm::cl::opt<bool> EmptyLineCommentCoverage(
    "emptyline-comment-coverage",
    llvm::cl::desc("Emit emptylines and comment lines as skipped regions (only "
                   "disable it on test)"),
    llvm::cl::init(true), llvm::cl::Hidden);

namespace llvm::coverage {
cl::opt<bool> SystemHeadersCoverage(
    "system-headers-coverage",
    cl::desc("Enable collecting coverage from system headers"), cl::init(false),
    cl::Hidden);
}

using namespace clang;
using namespace CodeGen;
using namespace llvm::coverage;

CoverageSourceInfo *
CoverageMappingModuleGen::setUpCoverageCallbacks(Preprocessor &PP) {
  CoverageSourceInfo *CoverageInfo =
      new CoverageSourceInfo(PP.getSourceManager());
  PP.addPPCallbacks(std::unique_ptr<PPCallbacks>(CoverageInfo));
  if (EmptyLineCommentCoverage) {
    PP.addCommentHandler(CoverageInfo);
    PP.setEmptylineHandler(CoverageInfo);
    PP.setPreprocessToken(true);
    PP.setTokenWatcher([CoverageInfo](clang::Token Tok) {
      // Update previous token location.
      CoverageInfo->PrevTokLoc = Tok.getLocation();
      if (Tok.getKind() != clang::tok::eod)
        CoverageInfo->updateNextTokLoc(Tok.getLocation());
    });
  }
  return CoverageInfo;
}

void CoverageSourceInfo::AddSkippedRange(SourceRange Range,
                                         SkippedRange::Kind RangeKind) {
  if (EmptyLineCommentCoverage && !SkippedRanges.empty() &&
      PrevTokLoc == SkippedRanges.back().PrevTokLoc &&
      SourceMgr.isWrittenInSameFile(SkippedRanges.back().Range.getEnd(),
                                    Range.getBegin()))
    SkippedRanges.back().Range.setEnd(Range.getEnd());
  else
    SkippedRanges.push_back({Range, RangeKind, PrevTokLoc});
}

void CoverageSourceInfo::SourceRangeSkipped(SourceRange Range, SourceLocation) {
  AddSkippedRange(Range, SkippedRange::PPIfElse);
}

void CoverageSourceInfo::HandleEmptyline(SourceRange Range) {
  AddSkippedRange(Range, SkippedRange::EmptyLine);
}

bool CoverageSourceInfo::HandleComment(Preprocessor &PP, SourceRange Range) {
  AddSkippedRange(Range, SkippedRange::Comment);
  return false;
}

void CoverageSourceInfo::updateNextTokLoc(SourceLocation Loc) {
  if (!SkippedRanges.empty() && SkippedRanges.back().NextTokLoc.isInvalid())
    SkippedRanges.back().NextTokLoc = Loc;
}

namespace {
/// A region of source code that can be mapped to a counter.
class SourceMappingRegion {
  /// Primary Counter that is also used for Branch Regions for "True" branches.
  Counter Count;

  /// Secondary Counter used for Branch Regions for "False" branches.
  std::optional<Counter> FalseCount;

  /// Parameters used for Modified Condition/Decision Coverage
  mcdc::Parameters MCDCParams;

  /// The region's starting location.
  std::optional<SourceLocation> LocStart;

  /// The region's ending location.
  std::optional<SourceLocation> LocEnd;

  /// Whether this region is a gap region. The count from a gap region is set
  /// as the line execution count if there are no other regions on the line.
  bool GapRegion;

  /// Whetever this region is skipped ('if constexpr' or 'if consteval' untaken
  /// branch, or anything skipped but not empty line / comments)
  bool SkippedRegion;

public:
  SourceMappingRegion(Counter Count, std::optional<SourceLocation> LocStart,
                      std::optional<SourceLocation> LocEnd,
                      bool GapRegion = false)
      : Count(Count), LocStart(LocStart), LocEnd(LocEnd), GapRegion(GapRegion),
        SkippedRegion(false) {}

  SourceMappingRegion(Counter Count, std::optional<Counter> FalseCount,
                      mcdc::Parameters MCDCParams,
                      std::optional<SourceLocation> LocStart,
                      std::optional<SourceLocation> LocEnd,
                      bool GapRegion = false)
      : Count(Count), FalseCount(FalseCount), MCDCParams(MCDCParams),
        LocStart(LocStart), LocEnd(LocEnd), GapRegion(GapRegion),
        SkippedRegion(false) {}

  SourceMappingRegion(mcdc::Parameters MCDCParams,
                      std::optional<SourceLocation> LocStart,
                      std::optional<SourceLocation> LocEnd)
      : MCDCParams(MCDCParams), LocStart(LocStart), LocEnd(LocEnd),
        GapRegion(false), SkippedRegion(false) {}

  const Counter &getCounter() const { return Count; }

  const Counter &getFalseCounter() const {
    assert(FalseCount && "Region has no alternate counter");
    return *FalseCount;
  }

  void setCounter(Counter C) { Count = C; }

  bool hasStartLoc() const { return LocStart.has_value(); }

  void setStartLoc(SourceLocation Loc) { LocStart = Loc; }

  SourceLocation getBeginLoc() const {
    assert(LocStart && "Region has no start location");
    return *LocStart;
  }

  bool hasEndLoc() const { return LocEnd.has_value(); }

  void setEndLoc(SourceLocation Loc) {
    assert(Loc.isValid() && "Setting an invalid end location");
    LocEnd = Loc;
  }

  SourceLocation getEndLoc() const {
    assert(LocEnd && "Region has no end location");
    return *LocEnd;
  }

  bool isGap() const { return GapRegion; }

  void setGap(bool Gap) { GapRegion = Gap; }

  bool isSkipped() const { return SkippedRegion; }

  void setSkipped(bool Skipped) { SkippedRegion = Skipped; }

  bool isBranch() const { return FalseCount.has_value(); }

  bool isMCDCBranch() const {
    return std::holds_alternative<mcdc::BranchParameters>(MCDCParams);
  }

  const auto &getMCDCBranchParams() const {
    return mcdc::getParams<const mcdc::BranchParameters>(MCDCParams);
  }

  bool isMCDCDecision() const {
    return std::holds_alternative<mcdc::DecisionParameters>(MCDCParams);
  }

  const auto &getMCDCDecisionParams() const {
    return mcdc::getParams<const mcdc::DecisionParameters>(MCDCParams);
  }

  const mcdc::Parameters &getMCDCParams() const { return MCDCParams; }

  void resetMCDCParams() { MCDCParams = mcdc::Parameters(); }
};

/// Spelling locations for the start and end of a source region.
struct SpellingRegion {
  /// The line where the region starts.
  unsigned LineStart;

  /// The column where the region starts.
  unsigned ColumnStart;

  /// The line where the region ends.
  unsigned LineEnd;

  /// The column where the region ends.
  unsigned ColumnEnd;

  SpellingRegion(SourceManager &SM, SourceLocation LocStart,
                 SourceLocation LocEnd) {
    LineStart = SM.getSpellingLineNumber(LocStart);
    ColumnStart = SM.getSpellingColumnNumber(LocStart);
    LineEnd = SM.getSpellingLineNumber(LocEnd);
    ColumnEnd = SM.getSpellingColumnNumber(LocEnd);
  }

  SpellingRegion(SourceManager &SM, SourceMappingRegion &R)
      : SpellingRegion(SM, R.getBeginLoc(), R.getEndLoc()) {}

  /// Check if the start and end locations appear in source order, i.e
  /// top->bottom, left->right.
  bool isInSourceOrder() const {
    return (LineStart < LineEnd) ||
           (LineStart == LineEnd && ColumnStart <= ColumnEnd);
  }
};

/// Provides the common functionality for the different
/// coverage mapping region builders.
class CoverageMappingBuilder {
public:
  CoverageMappingModuleGen &CVM;
  SourceManager &SM;
  const LangOptions &LangOpts;

private:
  /// Map of clang's FileIDs to IDs used for coverage mapping.
  llvm::SmallDenseMap<FileID, std::pair<unsigned, SourceLocation>, 8>
      FileIDMapping;

public:
  /// The coverage mapping regions for this function
  llvm::SmallVector<CounterMappingRegion, 32> MappingRegions;
  /// The source mapping regions for this function.
  std::vector<SourceMappingRegion> SourceRegions;

  /// A set of regions which can be used as a filter.
  ///
  /// It is produced by emitExpansionRegions() and is used in
  /// emitSourceRegions() to suppress producing code regions if
  /// the same area is covered by expansion regions.
  typedef llvm::SmallSet<std::pair<SourceLocation, SourceLocation>, 8>
      SourceRegionFilter;

  CoverageMappingBuilder(CoverageMappingModuleGen &CVM, SourceManager &SM,
                         const LangOptions &LangOpts)
      : CVM(CVM), SM(SM), LangOpts(LangOpts) {}

  /// Return the precise end location for the given token.
  SourceLocation getPreciseTokenLocEnd(SourceLocation Loc) {
    // We avoid getLocForEndOfToken here, because it doesn't do what we want for
    // macro locations, which we just treat as expanded files.
    unsigned TokLen =
        Lexer::MeasureTokenLength(SM.getSpellingLoc(Loc), SM, LangOpts);
    return Loc.getLocWithOffset(TokLen);
  }

  /// Return the start location of an included file or expanded macro.
  SourceLocation getStartOfFileOrMacro(SourceLocation Loc) {
    if (Loc.isMacroID())
      return Loc.getLocWithOffset(-SM.getFileOffset(Loc));
    return SM.getLocForStartOfFile(SM.getFileID(Loc));
  }

  /// Return the end location of an included file or expanded macro.
  SourceLocation getEndOfFileOrMacro(SourceLocation Loc) {
    if (Loc.isMacroID())
      return Loc.getLocWithOffset(SM.getFileIDSize(SM.getFileID(Loc)) -
                                  SM.getFileOffset(Loc));
    return SM.getLocForEndOfFile(SM.getFileID(Loc));
  }

  /// Find out where a macro is expanded. If the immediate result is a
  /// <scratch space>, keep looking until the result isn't. Return a pair of
  /// \c SourceLocation. The first object is always the begin sloc of found
  /// result. The second should be checked by the caller: if it has value, it's
  /// the end sloc of the found result. Otherwise the while loop didn't get
  /// executed, which means the location wasn't changed and the caller has to
  /// learn the end sloc from somewhere else.
  std::pair<SourceLocation, std::optional<SourceLocation>>
  getNonScratchExpansionLoc(SourceLocation Loc) {
    std::optional<SourceLocation> EndLoc = std::nullopt;
    while (Loc.isMacroID() &&
           SM.isWrittenInScratchSpace(SM.getSpellingLoc(Loc))) {
      auto ExpansionRange = SM.getImmediateExpansionRange(Loc);
      Loc = ExpansionRange.getBegin();
      EndLoc = ExpansionRange.getEnd();
    }
    return std::make_pair(Loc, EndLoc);
  }

  /// Find out where the current file is included or macro is expanded. If
  /// \c AcceptScratch is set to false, keep looking for expansions until the
  /// found sloc is not a <scratch space>.
  SourceLocation getIncludeOrExpansionLoc(SourceLocation Loc,
                                          bool AcceptScratch = true) {
    if (!Loc.isMacroID())
      return SM.getIncludeLoc(SM.getFileID(Loc));
    Loc = SM.getImmediateExpansionRange(Loc).getBegin();
    if (AcceptScratch)
      return Loc;
    return getNonScratchExpansionLoc(Loc).first;
  }

  /// Return true if \c Loc is a location in a built-in macro.
  bool isInBuiltin(SourceLocation Loc) {
    return SM.getBufferName(SM.getSpellingLoc(Loc)) == "<built-in>";
  }

  /// Check whether \c Loc is included or expanded from \c Parent.
  bool isNestedIn(SourceLocation Loc, FileID Parent) {
    do {
      Loc = getIncludeOrExpansionLoc(Loc);
      if (Loc.isInvalid())
        return false;
    } while (!SM.isInFileID(Loc, Parent));
    return true;
  }

  /// Get the start of \c S ignoring macro arguments and builtin macros.
  SourceLocation getStart(const Stmt *S) {
    SourceLocation Loc = S->getBeginLoc();
    while (SM.isMacroArgExpansion(Loc) || isInBuiltin(Loc))
      Loc = SM.getImmediateExpansionRange(Loc).getBegin();
    return Loc;
  }

  /// Get the end of \c S ignoring macro arguments and builtin macros.
  SourceLocation getEnd(const Stmt *S) {
    SourceLocation Loc = S->getEndLoc();
    while (SM.isMacroArgExpansion(Loc) || isInBuiltin(Loc))
      Loc = SM.getImmediateExpansionRange(Loc).getBegin();
    return getPreciseTokenLocEnd(Loc);
  }

  /// Find the set of files we have regions for and assign IDs
  ///
  /// Fills \c Mapping with the virtual file mapping needed to write out
  /// coverage and collects the necessary file information to emit source and
  /// expansion regions.
  void gatherFileIDs(SmallVectorImpl<unsigned> &Mapping) {
    FileIDMapping.clear();

    llvm::SmallSet<FileID, 8> Visited;
    SmallVector<std::pair<SourceLocation, unsigned>, 8> FileLocs;
    for (auto &Region : SourceRegions) {
      SourceLocation Loc = Region.getBeginLoc();

      // Replace Region with its definition if it is in <scratch space>.
      auto NonScratchExpansionLoc = getNonScratchExpansionLoc(Loc);
      auto EndLoc = NonScratchExpansionLoc.second;
      if (EndLoc.has_value()) {
        Loc = NonScratchExpansionLoc.first;
        Region.setStartLoc(Loc);
        Region.setEndLoc(EndLoc.value());
      }

      // Replace Loc with FileLoc if it is expanded with system headers.
      if (!SystemHeadersCoverage && SM.isInSystemMacro(Loc)) {
        auto BeginLoc = SM.getSpellingLoc(Loc);
        auto EndLoc = SM.getSpellingLoc(Region.getEndLoc());
        if (SM.isWrittenInSameFile(BeginLoc, EndLoc)) {
          Loc = SM.getFileLoc(Loc);
          Region.setStartLoc(Loc);
          Region.setEndLoc(SM.getFileLoc(Region.getEndLoc()));
        }
      }

      FileID File = SM.getFileID(Loc);
      if (!Visited.insert(File).second)
        continue;

      assert(SystemHeadersCoverage ||
             !SM.isInSystemHeader(SM.getSpellingLoc(Loc)));

      unsigned Depth = 0;
      for (SourceLocation Parent = getIncludeOrExpansionLoc(Loc);
           Parent.isValid(); Parent = getIncludeOrExpansionLoc(Parent))
        ++Depth;
      FileLocs.push_back(std::make_pair(Loc, Depth));
    }
    llvm::stable_sort(FileLocs, llvm::less_second());

    for (const auto &FL : FileLocs) {
      SourceLocation Loc = FL.first;
      FileID SpellingFile = SM.getDecomposedSpellingLoc(Loc).first;
      auto Entry = SM.getFileEntryRefForID(SpellingFile);
      if (!Entry)
        continue;

      FileIDMapping[SM.getFileID(Loc)] = std::make_pair(Mapping.size(), Loc);
      Mapping.push_back(CVM.getFileID(*Entry));
    }
  }

  /// Get the coverage mapping file ID for \c Loc.
  ///
  /// If such file id doesn't exist, return std::nullopt.
  std::optional<unsigned> getCoverageFileID(SourceLocation Loc) {
    auto Mapping = FileIDMapping.find(SM.getFileID(Loc));
    if (Mapping != FileIDMapping.end())
      return Mapping->second.first;
    return std::nullopt;
  }

  /// This shrinks the skipped range if it spans a line that contains a
  /// non-comment token. If shrinking the skipped range would make it empty,
  /// this returns std::nullopt.
  /// Note this function can potentially be expensive because
  /// getSpellingLineNumber uses getLineNumber, which is expensive.
  std::optional<SpellingRegion> adjustSkippedRange(SourceManager &SM,
                                                   SourceLocation LocStart,
                                                   SourceLocation LocEnd,
                                                   SourceLocation PrevTokLoc,
                                                   SourceLocation NextTokLoc) {
    SpellingRegion SR{SM, LocStart, LocEnd};
    SR.ColumnStart = 1;
    if (PrevTokLoc.isValid() && SM.isWrittenInSameFile(LocStart, PrevTokLoc) &&
        SR.LineStart == SM.getSpellingLineNumber(PrevTokLoc))
      SR.LineStart++;
    if (NextTokLoc.isValid() && SM.isWrittenInSameFile(LocEnd, NextTokLoc) &&
        SR.LineEnd == SM.getSpellingLineNumber(NextTokLoc)) {
      SR.LineEnd--;
      SR.ColumnEnd++;
    }
    if (SR.isInSourceOrder())
      return SR;
    return std::nullopt;
  }

  /// Gather all the regions that were skipped by the preprocessor
  /// using the constructs like #if or comments.
  void gatherSkippedRegions() {
    /// An array of the minimum lineStarts and the maximum lineEnds
    /// for mapping regions from the appropriate source files.
    llvm::SmallVector<std::pair<unsigned, unsigned>, 8> FileLineRanges;
    FileLineRanges.resize(
        FileIDMapping.size(),
        std::make_pair(std::numeric_limits<unsigned>::max(), 0));
    for (const auto &R : MappingRegions) {
      FileLineRanges[R.FileID].first =
          std::min(FileLineRanges[R.FileID].first, R.LineStart);
      FileLineRanges[R.FileID].second =
          std::max(FileLineRanges[R.FileID].second, R.LineEnd);
    }

    auto SkippedRanges = CVM.getSourceInfo().getSkippedRanges();
    for (auto &I : SkippedRanges) {
      SourceRange Range = I.Range;
      auto LocStart = Range.getBegin();
      auto LocEnd = Range.getEnd();
      assert(SM.isWrittenInSameFile(LocStart, LocEnd) &&
             "region spans multiple files");

      auto CovFileID = getCoverageFileID(LocStart);
      if (!CovFileID)
        continue;
      std::optional<SpellingRegion> SR;
      if (I.isComment())
        SR = adjustSkippedRange(SM, LocStart, LocEnd, I.PrevTokLoc,
                                I.NextTokLoc);
      else if (I.isPPIfElse() || I.isEmptyLine())
        SR = {SM, LocStart, LocEnd};

      if (!SR)
        continue;
      auto Region = CounterMappingRegion::makeSkipped(
          *CovFileID, SR->LineStart, SR->ColumnStart, SR->LineEnd,
          SR->ColumnEnd);
      // Make sure that we only collect the regions that are inside
      // the source code of this function.
      if (Region.LineStart >= FileLineRanges[*CovFileID].first &&
          Region.LineEnd <= FileLineRanges[*CovFileID].second)
        MappingRegions.push_back(Region);
    }
  }

  /// Generate the coverage counter mapping regions from collected
  /// source regions.
  void emitSourceRegions(const SourceRegionFilter &Filter) {
    for (const auto &Region : SourceRegions) {
      assert(Region.hasEndLoc() && "incomplete region");

      SourceLocation LocStart = Region.getBeginLoc();
      assert(SM.getFileID(LocStart).isValid() && "region in invalid file");

      // Ignore regions from system headers unless collecting coverage from
      // system headers is explicitly enabled.
      if (!SystemHeadersCoverage &&
          SM.isInSystemHeader(SM.getSpellingLoc(LocStart))) {
        assert(!Region.isMCDCBranch() && !Region.isMCDCDecision() &&
               "Don't suppress the condition in system headers");
        continue;
      }

      auto CovFileID = getCoverageFileID(LocStart);
      // Ignore regions that don't have a file, such as builtin macros.
      if (!CovFileID) {
        assert(!Region.isMCDCBranch() && !Region.isMCDCDecision() &&
               "Don't suppress the condition in non-file regions");
        continue;
      }

      SourceLocation LocEnd = Region.getEndLoc();
      assert(SM.isWrittenInSameFile(LocStart, LocEnd) &&
             "region spans multiple files");

      // Don't add code regions for the area covered by expansion regions.
      // This not only suppresses redundant regions, but sometimes prevents
      // creating regions with wrong counters if, for example, a statement's
      // body ends at the end of a nested macro.
      if (Filter.count(std::make_pair(LocStart, LocEnd))) {
        assert(!Region.isMCDCBranch() && !Region.isMCDCDecision() &&
               "Don't suppress the condition");
        continue;
      }

      // Find the spelling locations for the mapping region.
      SpellingRegion SR{SM, LocStart, LocEnd};
      assert(SR.isInSourceOrder() && "region start and end out of order");

      if (Region.isGap()) {
        MappingRegions.push_back(CounterMappingRegion::makeGapRegion(
            Region.getCounter(), *CovFileID, SR.LineStart, SR.ColumnStart,
            SR.LineEnd, SR.ColumnEnd));
      } else if (Region.isSkipped()) {
        MappingRegions.push_back(CounterMappingRegion::makeSkipped(
            *CovFileID, SR.LineStart, SR.ColumnStart, SR.LineEnd,
            SR.ColumnEnd));
      } else if (Region.isBranch()) {
        MappingRegions.push_back(CounterMappingRegion::makeBranchRegion(
            Region.getCounter(), Region.getFalseCounter(), *CovFileID,
            SR.LineStart, SR.ColumnStart, SR.LineEnd, SR.ColumnEnd,
            Region.getMCDCParams()));
      } else if (Region.isMCDCDecision()) {
        MappingRegions.push_back(CounterMappingRegion::makeDecisionRegion(
            Region.getMCDCDecisionParams(), *CovFileID, SR.LineStart,
            SR.ColumnStart, SR.LineEnd, SR.ColumnEnd));
      } else {
        MappingRegions.push_back(CounterMappingRegion::makeRegion(
            Region.getCounter(), *CovFileID, SR.LineStart, SR.ColumnStart,
            SR.LineEnd, SR.ColumnEnd));
      }
    }
  }

  /// Generate expansion regions for each virtual file we've seen.
  SourceRegionFilter emitExpansionRegions() {
    SourceRegionFilter Filter;
    for (const auto &FM : FileIDMapping) {
      SourceLocation ExpandedLoc = FM.second.second;
      SourceLocation ParentLoc = getIncludeOrExpansionLoc(ExpandedLoc, false);
      if (ParentLoc.isInvalid())
        continue;

      auto ParentFileID = getCoverageFileID(ParentLoc);
      if (!ParentFileID)
        continue;
      auto ExpandedFileID = getCoverageFileID(ExpandedLoc);
      assert(ExpandedFileID && "expansion in uncovered file");

      SourceLocation LocEnd = getPreciseTokenLocEnd(ParentLoc);
      assert(SM.isWrittenInSameFile(ParentLoc, LocEnd) &&
             "region spans multiple files");
      Filter.insert(std::make_pair(ParentLoc, LocEnd));

      SpellingRegion SR{SM, ParentLoc, LocEnd};
      assert(SR.isInSourceOrder() && "region start and end out of order");
      MappingRegions.push_back(CounterMappingRegion::makeExpansion(
          *ParentFileID, *ExpandedFileID, SR.LineStart, SR.ColumnStart,
          SR.LineEnd, SR.ColumnEnd));
    }
    return Filter;
  }
};

/// Creates unreachable coverage regions for the functions that
/// are not emitted.
struct EmptyCoverageMappingBuilder : public CoverageMappingBuilder {
  EmptyCoverageMappingBuilder(CoverageMappingModuleGen &CVM, SourceManager &SM,
                              const LangOptions &LangOpts)
      : CoverageMappingBuilder(CVM, SM, LangOpts) {}

  void VisitDecl(const Decl *D) {
    if (!D->hasBody())
      return;
    auto Body = D->getBody();
    SourceLocation Start = getStart(Body);
    SourceLocation End = getEnd(Body);
    if (!SM.isWrittenInSameFile(Start, End)) {
      // Walk up to find the common ancestor.
      // Correct the locations accordingly.
      FileID StartFileID = SM.getFileID(Start);
      FileID EndFileID = SM.getFileID(End);
      while (StartFileID != EndFileID && !isNestedIn(End, StartFileID)) {
        Start = getIncludeOrExpansionLoc(Start);
        assert(Start.isValid() &&
               "Declaration start location not nested within a known region");
        StartFileID = SM.getFileID(Start);
      }
      while (StartFileID != EndFileID) {
        End = getPreciseTokenLocEnd(getIncludeOrExpansionLoc(End));
        assert(End.isValid() &&
               "Declaration end location not nested within a known region");
        EndFileID = SM.getFileID(End);
      }
    }
    SourceRegions.emplace_back(Counter(), Start, End);
  }

  /// Write the mapping data to the output stream
  void write(llvm::raw_ostream &OS) {
    SmallVector<unsigned, 16> FileIDMapping;
    gatherFileIDs(FileIDMapping);
    emitSourceRegions(SourceRegionFilter());

    if (MappingRegions.empty())
      return;

    CoverageMappingWriter Writer(FileIDMapping, std::nullopt, MappingRegions);
    Writer.write(OS);
  }
};

/// A wrapper object for maintaining stacks to track the resursive AST visitor
/// walks for the purpose of assigning IDs to leaf-level conditions measured by
/// MC/DC. The object is created with a reference to the MCDCBitmapMap that was
/// created during the initial AST walk. The presence of a bitmap associated
/// with a boolean expression (top-level logical operator nest) indicates that
/// the boolean expression qualified for MC/DC.  The resulting condition IDs
/// are preserved in a map reference that is also provided during object
/// creation.
struct MCDCCoverageBuilder {

  /// The AST walk recursively visits nested logical-AND or logical-OR binary
  /// operator nodes and then visits their LHS and RHS children nodes.  As this
  /// happens, the algorithm will assign IDs to each operator's LHS and RHS side
  /// as the walk moves deeper into the nest.  At each level of the recursive
  /// nest, the LHS and RHS may actually correspond to larger subtrees (not
  /// leaf-conditions). If this is the case, when that node is visited, the ID
  /// assigned to the subtree is re-assigned to its LHS, and a new ID is given
  /// to its RHS. At the end of the walk, all leaf-level conditions will have a
  /// unique ID -- keep in mind that the final set of IDs may not be in
  /// numerical order from left to right.
  ///
  /// Example: "x = (A && B) || (C && D) || (D && F)"
  ///
  ///      Visit Depth1:
  ///              (A && B) || (C && D) || (D && F)
  ///              ^-------LHS--------^    ^-RHS--^
  ///                      ID=1              ID=2
  ///
  ///      Visit LHS-Depth2:
  ///              (A && B) || (C && D)
  ///              ^-LHS--^    ^-RHS--^
  ///                ID=1        ID=3
  ///
  ///      Visit LHS-Depth3:
  ///               (A && B)
  ///               LHS   RHS
  ///               ID=1  ID=4
  ///
  ///      Visit RHS-Depth3:
  ///                         (C && D)
  ///                         LHS   RHS
  ///                         ID=3  ID=5
  ///
  ///      Visit RHS-Depth2:              (D && F)
  ///                                     LHS   RHS
  ///                                     ID=2  ID=6
  ///
  ///      Visit Depth1:
  ///              (A && B)  || (C && D)  || (D && F)
  ///              ID=1  ID=4   ID=3  ID=5   ID=2  ID=6
  ///
  /// A node ID of '0' always means MC/DC isn't being tracked.
  ///
  /// As the AST walk proceeds recursively, the algorithm will also use a stack
  /// to track the IDs of logical-AND and logical-OR operations on the RHS so
  /// that it can be determined which nodes are executed next, depending on how
  /// a LHS or RHS of a logical-AND or logical-OR is evaluated.  This
  /// information relies on the assigned IDs and are embedded within the
  /// coverage region IDs of each branch region associated with a leaf-level
  /// condition. This information helps the visualization tool reconstruct all
  /// possible test vectors for the purposes of MC/DC analysis. If a "next" node
  /// ID is '0', it means it's the end of the test vector. The following rules
  /// are used:
  ///
  /// For logical-AND ("LHS && RHS"):
  /// - If LHS is TRUE, execution goes to the RHS node.
  /// - If LHS is FALSE, execution goes to the LHS node of the next logical-OR.
  ///   If that does not exist, execution exits (ID == 0).
  ///
  /// - If RHS is TRUE, execution goes to LHS node of the next logical-AND.
  ///   If that does not exist, execution exits (ID == 0).
  /// - If RHS is FALSE, execution goes to the LHS node of the next logical-OR.
  ///   If that does not exist, execution exits (ID == 0).
  ///
  /// For logical-OR ("LHS || RHS"):
  /// - If LHS is TRUE, execution goes to the LHS node of the next logical-AND.
  ///   If that does not exist, execution exits (ID == 0).
  /// - If LHS is FALSE, execution goes to the RHS node.
  ///
  /// - If RHS is TRUE, execution goes to LHS node of the next logical-AND.
  ///   If that does not exist, execution exits (ID == 0).
  /// - If RHS is FALSE, execution goes to the LHS node of the next logical-OR.
  ///   If that does not exist, execution exits (ID == 0).
  ///
  /// Finally, the condition IDs are also used when instrumenting the code to
  /// indicate a unique offset into a temporary bitmap that represents the true
  /// or false evaluation of that particular condition.
  ///
  /// NOTE regarding the use of CodeGenFunction::stripCond(). Even though, for
  /// simplicity, parentheses and unary logical-NOT operators are considered
  /// part of their underlying condition for both MC/DC and branch coverage, the
  /// condition IDs themselves are assigned and tracked using the underlying
  /// condition itself.  This is done solely for consistency since parentheses
  /// and logical-NOTs are ignored when checking whether the condition is
  /// actually an instrumentable condition. This can also make debugging a bit
  /// easier.

private:
  CodeGenModule &CGM;

  llvm::SmallVector<mcdc::ConditionIDs> DecisionStack;
  MCDC::State &MCDCState;
  const Stmt *DecisionStmt = nullptr;
  mcdc::ConditionID NextID = 0;
  bool NotMapped = false;

  /// Represent a sentinel value as a pair of final decisions for the bottom
  // of DecisionStack.
  static constexpr mcdc::ConditionIDs DecisionStackSentinel{-1, -1};

  /// Is this a logical-AND operation?
  bool isLAnd(const BinaryOperator *E) const {
    return E->getOpcode() == BO_LAnd;
  }

public:
  MCDCCoverageBuilder(CodeGenModule &CGM, MCDC::State &MCDCState)
      : CGM(CGM), DecisionStack(1, DecisionStackSentinel),
        MCDCState(MCDCState) {}

  /// Return whether the build of the control flow map is at the top-level
  /// (root) of a logical operator nest in a boolean expression prior to the
  /// assignment of condition IDs.
  bool isIdle() const { return (NextID == 0 && !NotMapped); }

  /// Return whether any IDs have been assigned in the build of the control
  /// flow map, indicating that the map is being generated for this boolean
  /// expression.
  bool isBuilding() const { return (NextID > 0); }

  /// Set the given condition's ID.
  void setCondID(const Expr *Cond, mcdc::ConditionID ID) {
    MCDCState.BranchByStmt[CodeGenFunction::stripCond(Cond)] = {ID,
                                                                DecisionStmt};
  }

  /// Return the ID of a given condition.
  mcdc::ConditionID getCondID(const Expr *Cond) const {
    auto I = MCDCState.BranchByStmt.find(CodeGenFunction::stripCond(Cond));
    if (I == MCDCState.BranchByStmt.end())
      return -1;
    else
      return I->second.ID;
  }

  /// Return the LHS Decision ([0,0] if not set).
  const mcdc::ConditionIDs &back() const { return DecisionStack.back(); }

  /// Push the binary operator statement to track the nest level and assign IDs
  /// to the operator's LHS and RHS.  The RHS may be a larger subtree that is
  /// broken up on successive levels.
  void pushAndAssignIDs(const BinaryOperator *E) {
    if (!CGM.getCodeGenOpts().MCDCCoverage)
      return;

    // If binary expression is disqualified, don't do mapping.
    if (!isBuilding() &&
        !MCDCState.DecisionByStmt.contains(CodeGenFunction::stripCond(E)))
      NotMapped = true;

    // Don't go any further if we don't need to map condition IDs.
    if (NotMapped)
      return;

    if (NextID == 0) {
      DecisionStmt = E;
      assert(MCDCState.DecisionByStmt.contains(E));
    }

    const mcdc::ConditionIDs &ParentDecision = DecisionStack.back();

    // If the operator itself has an assigned ID, this means it represents a
    // larger subtree.  In this case, assign that ID to its LHS node.  Its RHS
    // will receive a new ID below. Otherwise, assign ID+1 to LHS.
    if (MCDCState.BranchByStmt.contains(CodeGenFunction::stripCond(E)))
      setCondID(E->getLHS(), getCondID(E));
    else
      setCondID(E->getLHS(), NextID++);

    // Assign a ID+1 for the RHS.
    mcdc::ConditionID RHSid = NextID++;
    setCondID(E->getRHS(), RHSid);

    // Push the LHS decision IDs onto the DecisionStack.
    if (isLAnd(E))
      DecisionStack.push_back({ParentDecision[false], RHSid});
    else
      DecisionStack.push_back({RHSid, ParentDecision[true]});
  }

  /// Pop and return the LHS Decision ([0,0] if not set).
  mcdc::ConditionIDs pop() {
    if (!CGM.getCodeGenOpts().MCDCCoverage || NotMapped)
      return DecisionStackSentinel;

    assert(DecisionStack.size() > 1);
    return DecisionStack.pop_back_val();
  }

  /// Return the total number of conditions and reset the state. The number of
  /// conditions is zero if the expression isn't mapped.
  unsigned getTotalConditionsAndReset(const BinaryOperator *E) {
    if (!CGM.getCodeGenOpts().MCDCCoverage)
      return 0;

    assert(!isIdle());
    assert(DecisionStack.size() == 1);

    // Reset state if not doing mapping.
    if (NotMapped) {
      NotMapped = false;
      assert(NextID == 0);
      return 0;
    }

    // Set number of conditions and reset.
    unsigned TotalConds = NextID;

    // Reset ID back to beginning.
    NextID = 0;

    return TotalConds;
  }
};

/// A StmtVisitor that creates coverage mapping regions which map
/// from the source code locations to the PGO counters.
struct CounterCoverageMappingBuilder
    : public CoverageMappingBuilder,
      public ConstStmtVisitor<CounterCoverageMappingBuilder> {
  /// The map of statements to count values.
  llvm::DenseMap<const Stmt *, unsigned> &CounterMap;

  MCDC::State &MCDCState;

  /// A stack of currently live regions.
  llvm::SmallVector<SourceMappingRegion> RegionStack;

  /// Set if the Expr should be handled as a leaf even if it is kind of binary
  /// logical ops (&&, ||).
  llvm::DenseSet<const Stmt *> LeafExprSet;

  /// An object to manage MCDC regions.
  MCDCCoverageBuilder MCDCBuilder;

  CounterExpressionBuilder Builder;

  /// A location in the most recently visited file or macro.
  ///
  /// This is used to adjust the active source regions appropriately when
  /// expressions cross file or macro boundaries.
  SourceLocation MostRecentLocation;

  /// Whether the visitor at a terminate statement.
  bool HasTerminateStmt = false;

  /// Gap region counter after terminate statement.
  Counter GapRegionCounter;

  /// Return a counter for the subtraction of \c RHS from \c LHS
  Counter subtractCounters(Counter LHS, Counter RHS, bool Simplify = true) {
    assert(!llvm::EnableSingleByteCoverage &&
           "cannot add counters when single byte coverage mode is enabled");
    return Builder.subtract(LHS, RHS, Simplify);
  }

  /// Return a counter for the sum of \c LHS and \c RHS.
  Counter addCounters(Counter LHS, Counter RHS, bool Simplify = true) {
    assert(!llvm::EnableSingleByteCoverage &&
           "cannot add counters when single byte coverage mode is enabled");
    return Builder.add(LHS, RHS, Simplify);
  }

  Counter addCounters(Counter C1, Counter C2, Counter C3,
                      bool Simplify = true) {
    assert(!llvm::EnableSingleByteCoverage &&
           "cannot add counters when single byte coverage mode is enabled");
    return addCounters(addCounters(C1, C2, Simplify), C3, Simplify);
  }

  /// Return the region counter for the given statement.
  ///
  /// This should only be called on statements that have a dedicated counter.
  Counter getRegionCounter(const Stmt *S) {
    return Counter::getCounter(CounterMap[S]);
  }

  /// Push a region onto the stack.
  ///
  /// Returns the index on the stack where the region was pushed. This can be
  /// used with popRegions to exit a "scope", ending the region that was pushed.
  size_t pushRegion(Counter Count,
                    std::optional<SourceLocation> StartLoc = std::nullopt,
                    std::optional<SourceLocation> EndLoc = std::nullopt,
                    std::optional<Counter> FalseCount = std::nullopt,
                    const mcdc::Parameters &BranchParams = std::monostate()) {

    if (StartLoc && !FalseCount) {
      MostRecentLocation = *StartLoc;
    }

    // If either of these locations is invalid, something elsewhere in the
    // compiler has broken.
    assert((!StartLoc || StartLoc->isValid()) && "Start location is not valid");
    assert((!EndLoc || EndLoc->isValid()) && "End location is not valid");

    // However, we can still recover without crashing.
    // If either location is invalid, set it to std::nullopt to avoid
    // letting users of RegionStack think that region has a valid start/end
    // location.
    if (StartLoc && StartLoc->isInvalid())
      StartLoc = std::nullopt;
    if (EndLoc && EndLoc->isInvalid())
      EndLoc = std::nullopt;
    RegionStack.emplace_back(Count, FalseCount, BranchParams, StartLoc, EndLoc);

    return RegionStack.size() - 1;
  }

  size_t pushRegion(const mcdc::DecisionParameters &DecisionParams,
                    std::optional<SourceLocation> StartLoc = std::nullopt,
                    std::optional<SourceLocation> EndLoc = std::nullopt) {

    RegionStack.emplace_back(DecisionParams, StartLoc, EndLoc);

    return RegionStack.size() - 1;
  }

  size_t locationDepth(SourceLocation Loc) {
    size_t Depth = 0;
    while (Loc.isValid()) {
      Loc = getIncludeOrExpansionLoc(Loc);
      Depth++;
    }
    return Depth;
  }

  /// Pop regions from the stack into the function's list of regions.
  ///
  /// Adds all regions from \c ParentIndex to the top of the stack to the
  /// function's \c SourceRegions.
  void popRegions(size_t ParentIndex) {
    assert(RegionStack.size() >= ParentIndex && "parent not in stack");
    while (RegionStack.size() > ParentIndex) {
      SourceMappingRegion &Region = RegionStack.back();
      if (Region.hasStartLoc() &&
          (Region.hasEndLoc() || RegionStack[ParentIndex].hasEndLoc())) {
        SourceLocation StartLoc = Region.getBeginLoc();
        SourceLocation EndLoc = Region.hasEndLoc()
                                    ? Region.getEndLoc()
                                    : RegionStack[ParentIndex].getEndLoc();
        bool isBranch = Region.isBranch();
        size_t StartDepth = locationDepth(StartLoc);
        size_t EndDepth = locationDepth(EndLoc);
        while (!SM.isWrittenInSameFile(StartLoc, EndLoc)) {
          bool UnnestStart = StartDepth >= EndDepth;
          bool UnnestEnd = EndDepth >= StartDepth;
          if (UnnestEnd) {
            // The region ends in a nested file or macro expansion. If the
            // region is not a branch region, create a separate region for each
            // expansion, and for all regions, update the EndLoc. Branch
            // regions should not be split in order to keep a straightforward
            // correspondance between the region and its associated branch
            // condition, even if the condition spans multiple depths.
            SourceLocation NestedLoc = getStartOfFileOrMacro(EndLoc);
            assert(SM.isWrittenInSameFile(NestedLoc, EndLoc));

            if (!isBranch && !isRegionAlreadyAdded(NestedLoc, EndLoc))
              SourceRegions.emplace_back(Region.getCounter(), NestedLoc,
                                         EndLoc);

            EndLoc = getPreciseTokenLocEnd(getIncludeOrExpansionLoc(EndLoc));
            if (EndLoc.isInvalid())
              llvm::report_fatal_error(
                  "File exit not handled before popRegions");
            EndDepth--;
          }
          if (UnnestStart) {
            // The region ends in a nested file or macro expansion. If the
            // region is not a branch region, create a separate region for each
            // expansion, and for all regions, update the StartLoc. Branch
            // regions should not be split in order to keep a straightforward
            // correspondance between the region and its associated branch
            // condition, even if the condition spans multiple depths.
            SourceLocation NestedLoc = getEndOfFileOrMacro(StartLoc);
            assert(SM.isWrittenInSameFile(StartLoc, NestedLoc));

            if (!isBranch && !isRegionAlreadyAdded(StartLoc, NestedLoc))
              SourceRegions.emplace_back(Region.getCounter(), StartLoc,
                                         NestedLoc);

            StartLoc = getIncludeOrExpansionLoc(StartLoc);
            if (StartLoc.isInvalid())
              llvm::report_fatal_error(
                  "File exit not handled before popRegions");
            StartDepth--;
          }
        }
        Region.setStartLoc(StartLoc);
        Region.setEndLoc(EndLoc);

        if (!isBranch) {
          MostRecentLocation = EndLoc;
          // If this region happens to span an entire expansion, we need to
          // make sure we don't overlap the parent region with it.
          if (StartLoc == getStartOfFileOrMacro(StartLoc) &&
              EndLoc == getEndOfFileOrMacro(EndLoc))
            MostRecentLocation = getIncludeOrExpansionLoc(EndLoc);
        }

        assert(SM.isWrittenInSameFile(Region.getBeginLoc(), EndLoc));
        assert(SpellingRegion(SM, Region).isInSourceOrder());
        SourceRegions.push_back(Region);
      }
      RegionStack.pop_back();
    }
  }

  /// Return the currently active region.
  SourceMappingRegion &getRegion() {
    assert(!RegionStack.empty() && "statement has no region");
    return RegionStack.back();
  }

  /// Propagate counts through the children of \p S if \p VisitChildren is true.
  /// Otherwise, only emit a count for \p S itself.
  Counter propagateCounts(Counter TopCount, const Stmt *S,
                          bool VisitChildren = true) {
    SourceLocation StartLoc = getStart(S);
    SourceLocation EndLoc = getEnd(S);
    size_t Index = pushRegion(TopCount, StartLoc, EndLoc);
    if (VisitChildren)
      Visit(S);
    Counter ExitCount = getRegion().getCounter();
    popRegions(Index);

    // The statement may be spanned by an expansion. Make sure we handle a file
    // exit out of this expansion before moving to the next statement.
    if (SM.isBeforeInTranslationUnit(StartLoc, S->getBeginLoc()))
      MostRecentLocation = EndLoc;

    return ExitCount;
  }

  /// Determine whether the given condition can be constant folded.
  bool ConditionFoldsToBool(const Expr *Cond) {
    Expr::EvalResult Result;
    return (Cond->EvaluateAsInt(Result, CVM.getCodeGenModule().getContext()));
  }

  /// Create a Branch Region around an instrumentable condition for coverage
  /// and add it to the function's SourceRegions.  A branch region tracks a
  /// "True" counter and a "False" counter for boolean expressions that
  /// result in the generation of a branch.
  void createBranchRegion(const Expr *C, Counter TrueCnt, Counter FalseCnt,
                          const mcdc::ConditionIDs &Conds = {}) {
    // Check for NULL conditions.
    if (!C)
      return;

    // Ensure we are an instrumentable condition (i.e. no "&&" or "||").  Push
    // region onto RegionStack but immediately pop it (which adds it to the
    // function's SourceRegions) because it doesn't apply to any other source
    // code other than the Condition.
    // With !SystemHeadersCoverage, binary logical ops in system headers may be
    // treated as instrumentable conditions.
    if (CodeGenFunction::isInstrumentedCondition(C) ||
        LeafExprSet.count(CodeGenFunction::stripCond(C))) {
      mcdc::Parameters BranchParams;
      mcdc::ConditionID ID = MCDCBuilder.getCondID(C);
      if (ID >= 0)
        BranchParams = mcdc::BranchParameters{ID, Conds};

      // If a condition can fold to true or false, the corresponding branch
      // will be removed.  Create a region with both counters hard-coded to
      // zero. This allows us to visualize them in a special way.
      // Alternatively, we can prevent any optimization done via
      // constant-folding by ensuring that ConstantFoldsToSimpleInteger() in
      // CodeGenFunction.c always returns false, but that is very heavy-handed.
      if (ConditionFoldsToBool(C))
        popRegions(pushRegion(Counter::getZero(), getStart(C), getEnd(C),
                              Counter::getZero(), BranchParams));
      else
        // Otherwise, create a region with the True counter and False counter.
        popRegions(pushRegion(TrueCnt, getStart(C), getEnd(C), FalseCnt,
                              BranchParams));
    }
  }

  /// Create a Decision Region with a BitmapIdx and number of Conditions. This
  /// type of region "contains" branch regions, one for each of the conditions.
  /// The visualization tool will group everything together.
  void createDecisionRegion(const Expr *C,
                            const mcdc::DecisionParameters &DecisionParams) {
    popRegions(pushRegion(DecisionParams, getStart(C), getEnd(C)));
  }

  /// Create a Branch Region around a SwitchCase for code coverage
  /// and add it to the function's SourceRegions.
  void createSwitchCaseRegion(const SwitchCase *SC, Counter TrueCnt,
                              Counter FalseCnt) {
    // Push region onto RegionStack but immediately pop it (which adds it to
    // the function's SourceRegions) because it doesn't apply to any other
    // source other than the SwitchCase.
    popRegions(pushRegion(TrueCnt, getStart(SC), SC->getColonLoc(), FalseCnt));
  }

  /// Check whether a region with bounds \c StartLoc and \c EndLoc
  /// is already added to \c SourceRegions.
  bool isRegionAlreadyAdded(SourceLocation StartLoc, SourceLocation EndLoc,
                            bool isBranch = false) {
    return llvm::any_of(
        llvm::reverse(SourceRegions), [&](const SourceMappingRegion &Region) {
          return Region.getBeginLoc() == StartLoc &&
                 Region.getEndLoc() == EndLoc && Region.isBranch() == isBranch;
        });
  }

  /// Adjust the most recently visited location to \c EndLoc.
  ///
  /// This should be used after visiting any statements in non-source order.
  void adjustForOutOfOrderTraversal(SourceLocation EndLoc) {
    MostRecentLocation = EndLoc;
    // The code region for a whole macro is created in handleFileExit() when
    // it detects exiting of the virtual file of that macro. If we visited
    // statements in non-source order, we might already have such a region
    // added, for example, if a body of a loop is divided among multiple
    // macros. Avoid adding duplicate regions in such case.
    if (getRegion().hasEndLoc() &&
        MostRecentLocation == getEndOfFileOrMacro(MostRecentLocation) &&
        isRegionAlreadyAdded(getStartOfFileOrMacro(MostRecentLocation),
                             MostRecentLocation, getRegion().isBranch()))
      MostRecentLocation = getIncludeOrExpansionLoc(MostRecentLocation);
  }

  /// Adjust regions and state when \c NewLoc exits a file.
  ///
  /// If moving from our most recently tracked location to \c NewLoc exits any
  /// files, this adjusts our current region stack and creates the file regions
  /// for the exited file.
  void handleFileExit(SourceLocation NewLoc) {
    if (NewLoc.isInvalid() ||
        SM.isWrittenInSameFile(MostRecentLocation, NewLoc))
      return;

    // If NewLoc is not in a file that contains MostRecentLocation, walk up to
    // find the common ancestor.
    SourceLocation LCA = NewLoc;
    FileID ParentFile = SM.getFileID(LCA);
    while (!isNestedIn(MostRecentLocation, ParentFile)) {
      LCA = getIncludeOrExpansionLoc(LCA);
      if (LCA.isInvalid() || SM.isWrittenInSameFile(LCA, MostRecentLocation)) {
        // Since there isn't a common ancestor, no file was exited. We just need
        // to adjust our location to the new file.
        MostRecentLocation = NewLoc;
        return;
      }
      ParentFile = SM.getFileID(LCA);
    }

    llvm::SmallSet<SourceLocation, 8> StartLocs;
    std::optional<Counter> ParentCounter;
    for (SourceMappingRegion &I : llvm::reverse(RegionStack)) {
      if (!I.hasStartLoc())
        continue;
      SourceLocation Loc = I.getBeginLoc();
      if (!isNestedIn(Loc, ParentFile)) {
        ParentCounter = I.getCounter();
        break;
      }

      while (!SM.isInFileID(Loc, ParentFile)) {
        // The most nested region for each start location is the one with the
        // correct count. We avoid creating redundant regions by stopping once
        // we've seen this region.
        if (StartLocs.insert(Loc).second) {
          if (I.isBranch())
            SourceRegions.emplace_back(I.getCounter(), I.getFalseCounter(),
                                       I.getMCDCParams(), Loc,
                                       getEndOfFileOrMacro(Loc), I.isBranch());
          else
            SourceRegions.emplace_back(I.getCounter(), Loc,
                                       getEndOfFileOrMacro(Loc));
        }
        Loc = getIncludeOrExpansionLoc(Loc);
      }
      I.setStartLoc(getPreciseTokenLocEnd(Loc));
    }

    if (ParentCounter) {
      // If the file is contained completely by another region and doesn't
      // immediately start its own region, the whole file gets a region
      // corresponding to the parent.
      SourceLocation Loc = MostRecentLocation;
      while (isNestedIn(Loc, ParentFile)) {
        SourceLocation FileStart = getStartOfFileOrMacro(Loc);
        if (StartLocs.insert(FileStart).second) {
          SourceRegions.emplace_back(*ParentCounter, FileStart,
                                     getEndOfFileOrMacro(Loc));
          assert(SpellingRegion(SM, SourceRegions.back()).isInSourceOrder());
        }
        Loc = getIncludeOrExpansionLoc(Loc);
      }
    }

    MostRecentLocation = NewLoc;
  }

  /// Ensure that \c S is included in the current region.
  void extendRegion(const Stmt *S) {
    SourceMappingRegion &Region = getRegion();
    SourceLocation StartLoc = getStart(S);

    handleFileExit(StartLoc);
    if (!Region.hasStartLoc())
      Region.setStartLoc(StartLoc);
  }

  /// Mark \c S as a terminator, starting a zero region.
  void terminateRegion(const Stmt *S) {
    extendRegion(S);
    SourceMappingRegion &Region = getRegion();
    SourceLocation EndLoc = getEnd(S);
    if (!Region.hasEndLoc())
      Region.setEndLoc(EndLoc);
    pushRegion(Counter::getZero());
    HasTerminateStmt = true;
  }

  /// Find a valid gap range between \p AfterLoc and \p BeforeLoc.
  std::optional<SourceRange> findGapAreaBetween(SourceLocation AfterLoc,
                                                SourceLocation BeforeLoc) {
    // Some statements (like AttributedStmt and ImplicitValueInitExpr) don't
    // have valid source locations. Do not emit a gap region if this is the case
    // in either AfterLoc end or BeforeLoc end.
    if (AfterLoc.isInvalid() || BeforeLoc.isInvalid())
      return std::nullopt;

    // If AfterLoc is in function-like macro, use the right parenthesis
    // location.
    if (AfterLoc.isMacroID()) {
      FileID FID = SM.getFileID(AfterLoc);
      const SrcMgr::ExpansionInfo *EI = &SM.getSLocEntry(FID).getExpansion();
      if (EI->isFunctionMacroExpansion())
        AfterLoc = EI->getExpansionLocEnd();
    }

    size_t StartDepth = locationDepth(AfterLoc);
    size_t EndDepth = locationDepth(BeforeLoc);
    while (!SM.isWrittenInSameFile(AfterLoc, BeforeLoc)) {
      bool UnnestStart = StartDepth >= EndDepth;
      bool UnnestEnd = EndDepth >= StartDepth;
      if (UnnestEnd) {
        assert(SM.isWrittenInSameFile(getStartOfFileOrMacro(BeforeLoc),
                                      BeforeLoc));

        BeforeLoc = getIncludeOrExpansionLoc(BeforeLoc);
        assert(BeforeLoc.isValid());
        EndDepth--;
      }
      if (UnnestStart) {
        assert(SM.isWrittenInSameFile(AfterLoc,
                                      getEndOfFileOrMacro(AfterLoc)));

        AfterLoc = getIncludeOrExpansionLoc(AfterLoc);
        assert(AfterLoc.isValid());
        AfterLoc = getPreciseTokenLocEnd(AfterLoc);
        assert(AfterLoc.isValid());
        StartDepth--;
      }
    }
    AfterLoc = getPreciseTokenLocEnd(AfterLoc);
    // If the start and end locations of the gap are both within the same macro
    // file, the range may not be in source order.
    if (AfterLoc.isMacroID() || BeforeLoc.isMacroID())
      return std::nullopt;
    if (!SM.isWrittenInSameFile(AfterLoc, BeforeLoc) ||
        !SpellingRegion(SM, AfterLoc, BeforeLoc).isInSourceOrder())
      return std::nullopt;
    return {{AfterLoc, BeforeLoc}};
  }

  /// Emit a gap region between \p StartLoc and \p EndLoc with the given count.
  void fillGapAreaWithCount(SourceLocation StartLoc, SourceLocation EndLoc,
                            Counter Count) {
    if (StartLoc == EndLoc)
      return;
    assert(SpellingRegion(SM, StartLoc, EndLoc).isInSourceOrder());
    handleFileExit(StartLoc);
    size_t Index = pushRegion(Count, StartLoc, EndLoc);
    getRegion().setGap(true);
    handleFileExit(EndLoc);
    popRegions(Index);
  }

  /// Find a valid range starting with \p StartingLoc and ending before \p
  /// BeforeLoc.
  std::optional<SourceRange> findAreaStartingFromTo(SourceLocation StartingLoc,
                                                    SourceLocation BeforeLoc) {
    // If StartingLoc is in function-like macro, use its start location.
    if (StartingLoc.isMacroID()) {
      FileID FID = SM.getFileID(StartingLoc);
      const SrcMgr::ExpansionInfo *EI = &SM.getSLocEntry(FID).getExpansion();
      if (EI->isFunctionMacroExpansion())
        StartingLoc = EI->getExpansionLocStart();
    }

    size_t StartDepth = locationDepth(StartingLoc);
    size_t EndDepth = locationDepth(BeforeLoc);
    while (!SM.isWrittenInSameFile(StartingLoc, BeforeLoc)) {
      bool UnnestStart = StartDepth >= EndDepth;
      bool UnnestEnd = EndDepth >= StartDepth;
      if (UnnestEnd) {
        assert(SM.isWrittenInSameFile(getStartOfFileOrMacro(BeforeLoc),
                                      BeforeLoc));

        BeforeLoc = getIncludeOrExpansionLoc(BeforeLoc);
        assert(BeforeLoc.isValid());
        EndDepth--;
      }
      if (UnnestStart) {
        assert(SM.isWrittenInSameFile(StartingLoc,
                                      getStartOfFileOrMacro(StartingLoc)));

        StartingLoc = getIncludeOrExpansionLoc(StartingLoc);
        assert(StartingLoc.isValid());
        StartDepth--;
      }
    }
    // If the start and end locations of the gap are both within the same macro
    // file, the range may not be in source order.
    if (StartingLoc.isMacroID() || BeforeLoc.isMacroID())
      return std::nullopt;
    if (!SM.isWrittenInSameFile(StartingLoc, BeforeLoc) ||
        !SpellingRegion(SM, StartingLoc, BeforeLoc).isInSourceOrder())
      return std::nullopt;
    return {{StartingLoc, BeforeLoc}};
  }

  void markSkipped(SourceLocation StartLoc, SourceLocation BeforeLoc) {
    const auto Skipped = findAreaStartingFromTo(StartLoc, BeforeLoc);

    if (!Skipped)
      return;

    const auto NewStartLoc = Skipped->getBegin();
    const auto EndLoc = Skipped->getEnd();

    if (NewStartLoc == EndLoc)
      return;
    assert(SpellingRegion(SM, NewStartLoc, EndLoc).isInSourceOrder());
    handleFileExit(NewStartLoc);
    size_t Index = pushRegion(Counter{}, NewStartLoc, EndLoc);
    getRegion().setSkipped(true);
    handleFileExit(EndLoc);
    popRegions(Index);
  }

  /// Keep counts of breaks and continues inside loops.
  struct BreakContinue {
    Counter BreakCount;
    Counter ContinueCount;
  };
  SmallVector<BreakContinue, 8> BreakContinueStack;

  CounterCoverageMappingBuilder(
      CoverageMappingModuleGen &CVM,
      llvm::DenseMap<const Stmt *, unsigned> &CounterMap,
      MCDC::State &MCDCState, SourceManager &SM, const LangOptions &LangOpts)
      : CoverageMappingBuilder(CVM, SM, LangOpts), CounterMap(CounterMap),
        MCDCState(MCDCState), MCDCBuilder(CVM.getCodeGenModule(), MCDCState) {}

  /// Write the mapping data to the output stream
  void write(llvm::raw_ostream &OS) {
    llvm::SmallVector<unsigned, 8> VirtualFileMapping;
    gatherFileIDs(VirtualFileMapping);
    SourceRegionFilter Filter = emitExpansionRegions();
    emitSourceRegions(Filter);
    gatherSkippedRegions();

    if (MappingRegions.empty())
      return;

    CoverageMappingWriter Writer(VirtualFileMapping, Builder.getExpressions(),
                                 MappingRegions);
    Writer.write(OS);
  }

  void VisitStmt(const Stmt *S) {
    if (S->getBeginLoc().isValid())
      extendRegion(S);
    const Stmt *LastStmt = nullptr;
    bool SaveTerminateStmt = HasTerminateStmt;
    HasTerminateStmt = false;
    GapRegionCounter = Counter::getZero();
    for (const Stmt *Child : S->children())
      if (Child) {
        // If last statement contains terminate statements, add a gap area
        // between the two statements.
        if (LastStmt && HasTerminateStmt) {
          auto Gap = findGapAreaBetween(getEnd(LastStmt), getStart(Child));
          if (Gap)
            fillGapAreaWithCount(Gap->getBegin(), Gap->getEnd(),
                                 GapRegionCounter);
          SaveTerminateStmt = true;
          HasTerminateStmt = false;
        }
        this->Visit(Child);
        LastStmt = Child;
      }
    if (SaveTerminateStmt)
      HasTerminateStmt = true;
    handleFileExit(getEnd(S));
  }

  void VisitDecl(const Decl *D) {
    Stmt *Body = D->getBody();

    // Do not propagate region counts into system headers unless collecting
    // coverage from system headers is explicitly enabled.
    if (!SystemHeadersCoverage && Body &&
        SM.isInSystemHeader(SM.getSpellingLoc(getStart(Body))))
      return;

    // Do not visit the artificial children nodes of defaulted methods. The
    // lexer may not be able to report back precise token end locations for
    // these children nodes (llvm.org/PR39822), and moreover users will not be
    // able to see coverage for them.
    Counter BodyCounter = getRegionCounter(Body);
    bool Defaulted = false;
    if (auto *Method = dyn_cast<CXXMethodDecl>(D))
      Defaulted = Method->isDefaulted();
    if (auto *Ctor = dyn_cast<CXXConstructorDecl>(D)) {
      for (auto *Initializer : Ctor->inits()) {
        if (Initializer->isWritten()) {
          auto *Init = Initializer->getInit();
          if (getStart(Init).isValid() && getEnd(Init).isValid())
            propagateCounts(BodyCounter, Init);
        }
      }
    }

    propagateCounts(BodyCounter, Body,
                    /*VisitChildren=*/!Defaulted);
    assert(RegionStack.empty() && "Regions entered but never exited");
  }

  void VisitReturnStmt(const ReturnStmt *S) {
    extendRegion(S);
    if (S->getRetValue())
      Visit(S->getRetValue());
    terminateRegion(S);
  }

  void VisitCoroutineBodyStmt(const CoroutineBodyStmt *S) {
    extendRegion(S);
    Visit(S->getBody());
  }

  void VisitCoreturnStmt(const CoreturnStmt *S) {
    extendRegion(S);
    if (S->getOperand())
      Visit(S->getOperand());
    terminateRegion(S);
  }

  void VisitCoroutineSuspendExpr(const CoroutineSuspendExpr *E) {
    Visit(E->getOperand());
  }

  void VisitCXXThrowExpr(const CXXThrowExpr *E) {
    extendRegion(E);
    if (E->getSubExpr())
      Visit(E->getSubExpr());
    terminateRegion(E);
  }

  void VisitGotoStmt(const GotoStmt *S) { terminateRegion(S); }

  void VisitLabelStmt(const LabelStmt *S) {
    Counter LabelCount = getRegionCounter(S);
    SourceLocation Start = getStart(S);
    // We can't extendRegion here or we risk overlapping with our new region.
    handleFileExit(Start);
    pushRegion(LabelCount, Start);
    Visit(S->getSubStmt());
  }

  void VisitBreakStmt(const BreakStmt *S) {
    assert(!BreakContinueStack.empty() && "break not in a loop or switch!");
    if (!llvm::EnableSingleByteCoverage)
      BreakContinueStack.back().BreakCount = addCounters(
          BreakContinueStack.back().BreakCount, getRegion().getCounter());
    // FIXME: a break in a switch should terminate regions for all preceding
    // case statements, not just the most recent one.
    terminateRegion(S);
  }

  void VisitContinueStmt(const ContinueStmt *S) {
    assert(!BreakContinueStack.empty() && "continue stmt not in a loop!");
    if (!llvm::EnableSingleByteCoverage)
      BreakContinueStack.back().ContinueCount = addCounters(
          BreakContinueStack.back().ContinueCount, getRegion().getCounter());
    terminateRegion(S);
  }

  void VisitCallExpr(const CallExpr *E) {
    VisitStmt(E);

    // Terminate the region when we hit a noreturn function.
    // (This is helpful dealing with switch statements.)
    QualType CalleeType = E->getCallee()->getType();
    if (getFunctionExtInfo(*CalleeType).getNoReturn())
      terminateRegion(E);
  }

  void VisitWhileStmt(const WhileStmt *S) {
    extendRegion(S);

    Counter ParentCount = getRegion().getCounter();
    Counter BodyCount = llvm::EnableSingleByteCoverage
                            ? getRegionCounter(S->getBody())
                            : getRegionCounter(S);

    // Handle the body first so that we can get the backedge count.
    BreakContinueStack.push_back(BreakContinue());
    extendRegion(S->getBody());
    Counter BackedgeCount = propagateCounts(BodyCount, S->getBody());
    BreakContinue BC = BreakContinueStack.pop_back_val();

    bool BodyHasTerminateStmt = HasTerminateStmt;
    HasTerminateStmt = false;

    // Go back to handle the condition.
    Counter CondCount =
        llvm::EnableSingleByteCoverage
            ? getRegionCounter(S->getCond())
            : addCounters(ParentCount, BackedgeCount, BC.ContinueCount);
    propagateCounts(CondCount, S->getCond());
    adjustForOutOfOrderTraversal(getEnd(S));

    // The body count applies to the area immediately after the increment.
    auto Gap = findGapAreaBetween(S->getRParenLoc(), getStart(S->getBody()));
    if (Gap)
      fillGapAreaWithCount(Gap->getBegin(), Gap->getEnd(), BodyCount);

    Counter OutCount =
        llvm::EnableSingleByteCoverage
            ? getRegionCounter(S)
            : addCounters(BC.BreakCount,
                          subtractCounters(CondCount, BodyCount));

    if (OutCount != ParentCount) {
      pushRegion(OutCount);
      GapRegionCounter = OutCount;
      if (BodyHasTerminateStmt)
        HasTerminateStmt = true;
    }

    // Create Branch Region around condition.
    if (!llvm::EnableSingleByteCoverage)
      createBranchRegion(S->getCond(), BodyCount,
                         subtractCounters(CondCount, BodyCount));
  }

  void VisitDoStmt(const DoStmt *S) {
    extendRegion(S);

    Counter ParentCount = getRegion().getCounter();
    Counter BodyCount = llvm::EnableSingleByteCoverage
                            ? getRegionCounter(S->getBody())
                            : getRegionCounter(S);

    BreakContinueStack.push_back(BreakContinue());
    extendRegion(S->getBody());

    Counter BackedgeCount;
    if (llvm::EnableSingleByteCoverage)
      propagateCounts(BodyCount, S->getBody());
    else
      BackedgeCount =
          propagateCounts(addCounters(ParentCount, BodyCount), S->getBody());

    BreakContinue BC = BreakContinueStack.pop_back_val();

    bool BodyHasTerminateStmt = HasTerminateStmt;
    HasTerminateStmt = false;

    Counter CondCount = llvm::EnableSingleByteCoverage
                            ? getRegionCounter(S->getCond())
                            : addCounters(BackedgeCount, BC.ContinueCount);
    propagateCounts(CondCount, S->getCond());

    Counter OutCount =
        llvm::EnableSingleByteCoverage
            ? getRegionCounter(S)
            : addCounters(BC.BreakCount,
                          subtractCounters(CondCount, BodyCount));
    if (OutCount != ParentCount) {
      pushRegion(OutCount);
      GapRegionCounter = OutCount;
    }

    // Create Branch Region around condition.
    if (!llvm::EnableSingleByteCoverage)
      createBranchRegion(S->getCond(), BodyCount,
                         subtractCounters(CondCount, BodyCount));

    if (BodyHasTerminateStmt)
      HasTerminateStmt = true;
  }

  void VisitForStmt(const ForStmt *S) {
    extendRegion(S);
    if (S->getInit())
      Visit(S->getInit());

    Counter ParentCount = getRegion().getCounter();
    Counter BodyCount = llvm::EnableSingleByteCoverage
                            ? getRegionCounter(S->getBody())
                            : getRegionCounter(S);

    // The loop increment may contain a break or continue.
    if (S->getInc())
      BreakContinueStack.emplace_back();

    // Handle the body first so that we can get the backedge count.
    BreakContinueStack.emplace_back();
    extendRegion(S->getBody());
    Counter BackedgeCount = propagateCounts(BodyCount, S->getBody());
    BreakContinue BodyBC = BreakContinueStack.pop_back_val();

    bool BodyHasTerminateStmt = HasTerminateStmt;
    HasTerminateStmt = false;

    // The increment is essentially part of the body but it needs to include
    // the count for all the continue statements.
    BreakContinue IncrementBC;
    if (const Stmt *Inc = S->getInc()) {
      Counter IncCount;
      if (llvm::EnableSingleByteCoverage)
        IncCount = getRegionCounter(S->getInc());
      else
        IncCount = addCounters(BackedgeCount, BodyBC.ContinueCount);
      propagateCounts(IncCount, Inc);
      IncrementBC = BreakContinueStack.pop_back_val();
    }

    // Go back to handle the condition.
    Counter CondCount =
        llvm::EnableSingleByteCoverage
            ? getRegionCounter(S->getCond())
            : addCounters(
                  addCounters(ParentCount, BackedgeCount, BodyBC.ContinueCount),
                  IncrementBC.ContinueCount);

    if (const Expr *Cond = S->getCond()) {
      propagateCounts(CondCount, Cond);
      adjustForOutOfOrderTraversal(getEnd(S));
    }

    // The body count applies to the area immediately after the increment.
    auto Gap = findGapAreaBetween(S->getRParenLoc(), getStart(S->getBody()));
    if (Gap)
      fillGapAreaWithCount(Gap->getBegin(), Gap->getEnd(), BodyCount);

    Counter OutCount =
        llvm::EnableSingleByteCoverage
            ? getRegionCounter(S)
            : addCounters(BodyBC.BreakCount, IncrementBC.BreakCount,
                          subtractCounters(CondCount, BodyCount));
    if (OutCount != ParentCount) {
      pushRegion(OutCount);
      GapRegionCounter = OutCount;
      if (BodyHasTerminateStmt)
        HasTerminateStmt = true;
    }

    // Create Branch Region around condition.
    if (!llvm::EnableSingleByteCoverage)
      createBranchRegion(S->getCond(), BodyCount,
                         subtractCounters(CondCount, BodyCount));
  }

  void VisitCXXForRangeStmt(const CXXForRangeStmt *S) {
    extendRegion(S);
    if (S->getInit())
      Visit(S->getInit());
    Visit(S->getLoopVarStmt());
    Visit(S->getRangeStmt());

    Counter ParentCount = getRegion().getCounter();
    Counter BodyCount = llvm::EnableSingleByteCoverage
                            ? getRegionCounter(S->getBody())
                            : getRegionCounter(S);

    BreakContinueStack.push_back(BreakContinue());
    extendRegion(S->getBody());
    Counter BackedgeCount = propagateCounts(BodyCount, S->getBody());
    BreakContinue BC = BreakContinueStack.pop_back_val();

    bool BodyHasTerminateStmt = HasTerminateStmt;
    HasTerminateStmt = false;

    // The body count applies to the area immediately after the range.
    auto Gap = findGapAreaBetween(S->getRParenLoc(), getStart(S->getBody()));
    if (Gap)
      fillGapAreaWithCount(Gap->getBegin(), Gap->getEnd(), BodyCount);

    Counter OutCount;
    Counter LoopCount;
    if (llvm::EnableSingleByteCoverage)
      OutCount = getRegionCounter(S);
    else {
      LoopCount = addCounters(ParentCount, BackedgeCount, BC.ContinueCount);
      OutCount =
          addCounters(BC.BreakCount, subtractCounters(LoopCount, BodyCount));
    }
    if (OutCount != ParentCount) {
      pushRegion(OutCount);
      GapRegionCounter = OutCount;
      if (BodyHasTerminateStmt)
        HasTerminateStmt = true;
    }

    // Create Branch Region around condition.
    if (!llvm::EnableSingleByteCoverage)
      createBranchRegion(S->getCond(), BodyCount,
                         subtractCounters(LoopCount, BodyCount));
  }

  void VisitObjCForCollectionStmt(const ObjCForCollectionStmt *S) {
    extendRegion(S);
    Visit(S->getElement());

    Counter ParentCount = getRegion().getCounter();
    Counter BodyCount = getRegionCounter(S);

    BreakContinueStack.push_back(BreakContinue());
    extendRegion(S->getBody());
    Counter BackedgeCount = propagateCounts(BodyCount, S->getBody());
    BreakContinue BC = BreakContinueStack.pop_back_val();

    // The body count applies to the area immediately after the collection.
    auto Gap = findGapAreaBetween(S->getRParenLoc(), getStart(S->getBody()));
    if (Gap)
      fillGapAreaWithCount(Gap->getBegin(), Gap->getEnd(), BodyCount);

    Counter LoopCount =
        addCounters(ParentCount, BackedgeCount, BC.ContinueCount);
    Counter OutCount =
        addCounters(BC.BreakCount, subtractCounters(LoopCount, BodyCount));
    if (OutCount != ParentCount) {
      pushRegion(OutCount);
      GapRegionCounter = OutCount;
    }
  }

  void VisitSwitchStmt(const SwitchStmt *S) {
    extendRegion(S);
    if (S->getInit())
      Visit(S->getInit());
    Visit(S->getCond());

    BreakContinueStack.push_back(BreakContinue());

    const Stmt *Body = S->getBody();
    extendRegion(Body);
    if (const auto *CS = dyn_cast<CompoundStmt>(Body)) {
      if (!CS->body_empty()) {
        // Make a region for the body of the switch.  If the body starts with
        // a case, that case will reuse this region; otherwise, this covers
        // the unreachable code at the beginning of the switch body.
        size_t Index = pushRegion(Counter::getZero(), getStart(CS));
        getRegion().setGap(true);
        Visit(Body);

        // Set the end for the body of the switch, if it isn't already set.
        for (size_t i = RegionStack.size(); i != Index; --i) {
          if (!RegionStack[i - 1].hasEndLoc())
            RegionStack[i - 1].setEndLoc(getEnd(CS->body_back()));
        }

        popRegions(Index);
      }
    } else
      propagateCounts(Counter::getZero(), Body);
    BreakContinue BC = BreakContinueStack.pop_back_val();

    if (!BreakContinueStack.empty() && !llvm::EnableSingleByteCoverage)
      BreakContinueStack.back().ContinueCount = addCounters(
          BreakContinueStack.back().ContinueCount, BC.ContinueCount);

    Counter ParentCount = getRegion().getCounter();
    Counter ExitCount = getRegionCounter(S);
    SourceLocation ExitLoc = getEnd(S);
    pushRegion(ExitCount);
    GapRegionCounter = ExitCount;

    // Ensure that handleFileExit recognizes when the end location is located
    // in a different file.
    MostRecentLocation = getStart(S);
    handleFileExit(ExitLoc);

    // When single byte coverage mode is enabled, do not create branch region by
    // early returning.
    if (llvm::EnableSingleByteCoverage)
      return;

    // Create a Branch Region around each Case. Subtract the case's
    // counter from the Parent counter to track the "False" branch count.
    Counter CaseCountSum;
    bool HasDefaultCase = false;
    const SwitchCase *Case = S->getSwitchCaseList();
    for (; Case; Case = Case->getNextSwitchCase()) {
      HasDefaultCase = HasDefaultCase || isa<DefaultStmt>(Case);
      CaseCountSum =
          addCounters(CaseCountSum, getRegionCounter(Case), /*Simplify=*/false);
      createSwitchCaseRegion(
          Case, getRegionCounter(Case),
          subtractCounters(ParentCount, getRegionCounter(Case)));
    }
    // Simplify is skipped while building the counters above: it can get really
    // slow on top of switches with thousands of cases. Instead, trigger
    // simplification by adding zero to the last counter.
    CaseCountSum = addCounters(CaseCountSum, Counter::getZero());

    // If no explicit default case exists, create a branch region to represent
    // the hidden branch, which will be added later by the CodeGen. This region
    // will be associated with the switch statement's condition.
    if (!HasDefaultCase) {
      Counter DefaultTrue = subtractCounters(ParentCount, CaseCountSum);
      Counter DefaultFalse = subtractCounters(ParentCount, DefaultTrue);
      createBranchRegion(S->getCond(), DefaultTrue, DefaultFalse);
    }
  }

  void VisitSwitchCase(const SwitchCase *S) {
    extendRegion(S);

    SourceMappingRegion &Parent = getRegion();
    Counter Count = llvm::EnableSingleByteCoverage
                        ? getRegionCounter(S)
                        : addCounters(Parent.getCounter(), getRegionCounter(S));

    // Reuse the existing region if it starts at our label. This is typical of
    // the first case in a switch.
    if (Parent.hasStartLoc() && Parent.getBeginLoc() == getStart(S))
      Parent.setCounter(Count);
    else
      pushRegion(Count, getStart(S));

    GapRegionCounter = Count;

    if (const auto *CS = dyn_cast<CaseStmt>(S)) {
      Visit(CS->getLHS());
      if (const Expr *RHS = CS->getRHS())
        Visit(RHS);
    }
    Visit(S->getSubStmt());
  }

  void coverIfConsteval(const IfStmt *S) {
    assert(S->isConsteval());

    const auto *Then = S->getThen();
    const auto *Else = S->getElse();

    // It's better for llvm-cov to create a new region with same counter
    // so line-coverage can be properly calculated for lines containing
    // a skipped region (without it the line is marked uncovered)
    const Counter ParentCount = getRegion().getCounter();

    extendRegion(S);

    if (S->isNegatedConsteval()) {
      // ignore 'if consteval'
      markSkipped(S->getIfLoc(), getStart(Then));
      propagateCounts(ParentCount, Then);

      if (Else) {
        // ignore 'else <else>'
        markSkipped(getEnd(Then), getEnd(Else));
      }
    } else {
      assert(S->isNonNegatedConsteval());
      // ignore 'if consteval <then> [else]'
      markSkipped(S->getIfLoc(), Else ? getStart(Else) : getEnd(Then));

      if (Else)
        propagateCounts(ParentCount, Else);
    }
  }

  void coverIfConstexpr(const IfStmt *S) {
    assert(S->isConstexpr());

    // evaluate constant condition...
    const bool isTrue =
        S->getCond()
            ->EvaluateKnownConstInt(CVM.getCodeGenModule().getContext())
            .getBoolValue();

    extendRegion(S);

    // I'm using 'propagateCounts' later as new region is better and allows me
    // to properly calculate line coverage in llvm-cov utility
    const Counter ParentCount = getRegion().getCounter();

    // ignore 'if constexpr ('
    SourceLocation startOfSkipped = S->getIfLoc();

    if (const auto *Init = S->getInit()) {
      const auto start = getStart(Init);
      const auto end = getEnd(Init);

      // this check is to make sure typedef here which doesn't have valid source
      // location won't crash it
      if (start.isValid() && end.isValid()) {
        markSkipped(startOfSkipped, start);
        propagateCounts(ParentCount, Init);
        startOfSkipped = getEnd(Init);
      }
    }

    const auto *Then = S->getThen();
    const auto *Else = S->getElse();

    if (isTrue) {
      // ignore '<condition>)'
      markSkipped(startOfSkipped, getStart(Then));
      propagateCounts(ParentCount, Then);

      if (Else)
        // ignore 'else <else>'
        markSkipped(getEnd(Then), getEnd(Else));
    } else {
      // ignore '<condition>) <then> [else]'
      markSkipped(startOfSkipped, Else ? getStart(Else) : getEnd(Then));

      if (Else)
        propagateCounts(ParentCount, Else);
    }
  }

  void VisitIfStmt(const IfStmt *S) {
    // "if constexpr" and "if consteval" are not normal conditional statements,
    // their discarded statement should be skipped
    if (S->isConsteval())
      return coverIfConsteval(S);
    else if (S->isConstexpr())
      return coverIfConstexpr(S);

    extendRegion(S);
    if (S->getInit())
      Visit(S->getInit());

    // Extend into the condition before we propagate through it below - this is
    // needed to handle macros that generate the "if" but not the condition.
    extendRegion(S->getCond());

    Counter ParentCount = getRegion().getCounter();
    Counter ThenCount = llvm::EnableSingleByteCoverage
                            ? getRegionCounter(S->getThen())
                            : getRegionCounter(S);

    // Emitting a counter for the condition makes it easier to interpret the
    // counter for the body when looking at the coverage.
    propagateCounts(ParentCount, S->getCond());

    // The 'then' count applies to the area immediately after the condition.
    std::optional<SourceRange> Gap =
        findGapAreaBetween(S->getRParenLoc(), getStart(S->getThen()));
    if (Gap)
      fillGapAreaWithCount(Gap->getBegin(), Gap->getEnd(), ThenCount);

    extendRegion(S->getThen());
    Counter OutCount = propagateCounts(ThenCount, S->getThen());

    Counter ElseCount;
    if (!llvm::EnableSingleByteCoverage)
      ElseCount = subtractCounters(ParentCount, ThenCount);
    else if (S->getElse())
      ElseCount = getRegionCounter(S->getElse());

    if (const Stmt *Else = S->getElse()) {
      bool ThenHasTerminateStmt = HasTerminateStmt;
      HasTerminateStmt = false;
      // The 'else' count applies to the area immediately after the 'then'.
      std::optional<SourceRange> Gap =
          findGapAreaBetween(getEnd(S->getThen()), getStart(Else));
      if (Gap)
        fillGapAreaWithCount(Gap->getBegin(), Gap->getEnd(), ElseCount);
      extendRegion(Else);

      Counter ElseOutCount = propagateCounts(ElseCount, Else);
      if (!llvm::EnableSingleByteCoverage)
        OutCount = addCounters(OutCount, ElseOutCount);

      if (ThenHasTerminateStmt)
        HasTerminateStmt = true;
    } else if (!llvm::EnableSingleByteCoverage)
      OutCount = addCounters(OutCount, ElseCount);

    if (llvm::EnableSingleByteCoverage)
      OutCount = getRegionCounter(S);

    if (OutCount != ParentCount) {
      pushRegion(OutCount);
      GapRegionCounter = OutCount;
    }

    if (!S->isConsteval() && !llvm::EnableSingleByteCoverage)
      // Create Branch Region around condition.
      createBranchRegion(S->getCond(), ThenCount,
                         subtractCounters(ParentCount, ThenCount));
  }

  void VisitCXXTryStmt(const CXXTryStmt *S) {
    extendRegion(S);
    // Handle macros that generate the "try" but not the rest.
    extendRegion(S->getTryBlock());

    Counter ParentCount = getRegion().getCounter();
    propagateCounts(ParentCount, S->getTryBlock());

    for (unsigned I = 0, E = S->getNumHandlers(); I < E; ++I)
      Visit(S->getHandler(I));

    Counter ExitCount = getRegionCounter(S);
    pushRegion(ExitCount);
  }

  void VisitCXXCatchStmt(const CXXCatchStmt *S) {
    propagateCounts(getRegionCounter(S), S->getHandlerBlock());
  }

  void VisitAbstractConditionalOperator(const AbstractConditionalOperator *E) {
    extendRegion(E);

    Counter ParentCount = getRegion().getCounter();
    Counter TrueCount = llvm::EnableSingleByteCoverage
                            ? getRegionCounter(E->getTrueExpr())
                            : getRegionCounter(E);
    Counter OutCount;

    if (const auto *BCO = dyn_cast<BinaryConditionalOperator>(E)) {
      propagateCounts(ParentCount, BCO->getCommon());
      OutCount = TrueCount;
    } else {
      propagateCounts(ParentCount, E->getCond());
      // The 'then' count applies to the area immediately after the condition.
      auto Gap =
          findGapAreaBetween(E->getQuestionLoc(), getStart(E->getTrueExpr()));
      if (Gap)
        fillGapAreaWithCount(Gap->getBegin(), Gap->getEnd(), TrueCount);

      extendRegion(E->getTrueExpr());
      OutCount = propagateCounts(TrueCount, E->getTrueExpr());
    }

    extendRegion(E->getFalseExpr());
    Counter FalseCount = llvm::EnableSingleByteCoverage
                             ? getRegionCounter(E->getFalseExpr())
                             : subtractCounters(ParentCount, TrueCount);

    Counter FalseOutCount = propagateCounts(FalseCount, E->getFalseExpr());
    if (llvm::EnableSingleByteCoverage)
      OutCount = getRegionCounter(E);
    else
      OutCount = addCounters(OutCount, FalseOutCount);

    if (OutCount != ParentCount) {
      pushRegion(OutCount);
      GapRegionCounter = OutCount;
    }

    // Create Branch Region around condition.
    if (!llvm::EnableSingleByteCoverage)
      createBranchRegion(E->getCond(), TrueCount,
                         subtractCounters(ParentCount, TrueCount));
  }

  void createOrCancelDecision(const BinaryOperator *E, unsigned Since) {
    unsigned NumConds = MCDCBuilder.getTotalConditionsAndReset(E);
    if (NumConds == 0)
      return;

    // Extract [ID, Conds] to construct the graph.
    llvm::SmallVector<mcdc::ConditionIDs> CondIDs(NumConds);
    for (const auto &SR : ArrayRef(SourceRegions).slice(Since)) {
      if (SR.isMCDCBranch()) {
        auto [ID, Conds] = SR.getMCDCBranchParams();
        CondIDs[ID] = Conds;
      }
    }

    // Construct the graph and calculate `Indices`.
    mcdc::TVIdxBuilder Builder(CondIDs);
    unsigned NumTVs = Builder.NumTestVectors;
    unsigned MaxTVs = CVM.getCodeGenModule().getCodeGenOpts().MCDCMaxTVs;
    assert(MaxTVs < mcdc::TVIdxBuilder::HardMaxTVs);

    if (NumTVs > MaxTVs) {
      // NumTVs exceeds MaxTVs -- warn and cancel the Decision.
      cancelDecision(E, Since, NumTVs, MaxTVs);
      return;
    }

    // Update the state for CodeGenPGO
    assert(MCDCState.DecisionByStmt.contains(E));
    MCDCState.DecisionByStmt[E] = {
        MCDCState.BitmapBits, // Top
        std::move(Builder.Indices),
    };

    auto DecisionParams = mcdc::DecisionParameters{
        MCDCState.BitmapBits += NumTVs, // Tail
        NumConds,
    };

    // Create MCDC Decision Region.
    createDecisionRegion(E, DecisionParams);
  }

  // Warn and cancel the Decision.
  void cancelDecision(const BinaryOperator *E, unsigned Since, int NumTVs,
                      int MaxTVs) {
    auto &Diag = CVM.getCodeGenModule().getDiags();
    unsigned DiagID =
        Diag.getCustomDiagID(DiagnosticsEngine::Warning,
                             "unsupported MC/DC boolean expression; "
                             "number of test vectors (%0) exceeds max (%1). "
                             "Expression will not be covered");
    Diag.Report(E->getBeginLoc(), DiagID) << NumTVs << MaxTVs;

    // Restore MCDCBranch to Branch.
    for (auto &SR : MutableArrayRef(SourceRegions).slice(Since)) {
      assert(!SR.isMCDCDecision() && "Decision shouldn't be seen here");
      if (SR.isMCDCBranch())
        SR.resetMCDCParams();
    }

    // Tell CodeGenPGO not to instrument.
    MCDCState.DecisionByStmt.erase(E);
  }

  /// Check if E belongs to system headers.
  bool isExprInSystemHeader(const BinaryOperator *E) const {
    return (!SystemHeadersCoverage &&
            SM.isInSystemHeader(SM.getSpellingLoc(E->getOperatorLoc())) &&
            SM.isInSystemHeader(SM.getSpellingLoc(E->getBeginLoc())) &&
            SM.isInSystemHeader(SM.getSpellingLoc(E->getEndLoc())));
  }

  void VisitBinLAnd(const BinaryOperator *E) {
    if (isExprInSystemHeader(E)) {
      LeafExprSet.insert(E);
      return;
    }

    bool IsRootNode = MCDCBuilder.isIdle();

    unsigned SourceRegionsSince = SourceRegions.size();

    // Keep track of Binary Operator and assign MCDC condition IDs.
    MCDCBuilder.pushAndAssignIDs(E);

    extendRegion(E->getLHS());
    propagateCounts(getRegion().getCounter(), E->getLHS());
    handleFileExit(getEnd(E->getLHS()));

    // Track LHS True/False Decision.
    const auto DecisionLHS = MCDCBuilder.pop();

    // Counter tracks the right hand side of a logical and operator.
    extendRegion(E->getRHS());
    propagateCounts(getRegionCounter(E), E->getRHS());

    // Track RHS True/False Decision.
    const auto DecisionRHS = MCDCBuilder.back();

    // Extract the RHS's Execution Counter.
    Counter RHSExecCnt = getRegionCounter(E);

    // Extract the RHS's "True" Instance Counter.
    Counter RHSTrueCnt = getRegionCounter(E->getRHS());

    // Extract the Parent Region Counter.
    Counter ParentCnt = getRegion().getCounter();

    // Create Branch Region around LHS condition.
    if (!llvm::EnableSingleByteCoverage)
      createBranchRegion(E->getLHS(), RHSExecCnt,
                         subtractCounters(ParentCnt, RHSExecCnt), DecisionLHS);

    // Create Branch Region around RHS condition.
    if (!llvm::EnableSingleByteCoverage)
      createBranchRegion(E->getRHS(), RHSTrueCnt,
                         subtractCounters(RHSExecCnt, RHSTrueCnt), DecisionRHS);

    // Create MCDC Decision Region if at top-level (root).
    if (IsRootNode)
      createOrCancelDecision(E, SourceRegionsSince);
  }

  // Determine whether the right side of OR operation need to be visited.
  bool shouldVisitRHS(const Expr *LHS) {
    bool LHSIsTrue = false;
    bool LHSIsConst = false;
    if (!LHS->isValueDependent())
      LHSIsConst = LHS->EvaluateAsBooleanCondition(
          LHSIsTrue, CVM.getCodeGenModule().getContext());
    return !LHSIsConst || (LHSIsConst && !LHSIsTrue);
  }

  void VisitBinLOr(const BinaryOperator *E) {
    if (isExprInSystemHeader(E)) {
      LeafExprSet.insert(E);
      return;
    }

    bool IsRootNode = MCDCBuilder.isIdle();

    unsigned SourceRegionsSince = SourceRegions.size();

    // Keep track of Binary Operator and assign MCDC condition IDs.
    MCDCBuilder.pushAndAssignIDs(E);

    extendRegion(E->getLHS());
    Counter OutCount = propagateCounts(getRegion().getCounter(), E->getLHS());
    handleFileExit(getEnd(E->getLHS()));

    // Track LHS True/False Decision.
    const auto DecisionLHS = MCDCBuilder.pop();

    // Counter tracks the right hand side of a logical or operator.
    extendRegion(E->getRHS());
    propagateCounts(getRegionCounter(E), E->getRHS());

    // Track RHS True/False Decision.
    const auto DecisionRHS = MCDCBuilder.back();

    // Extract the RHS's Execution Counter.
    Counter RHSExecCnt = getRegionCounter(E);

    // Extract the RHS's "False" Instance Counter.
    Counter RHSFalseCnt = getRegionCounter(E->getRHS());

    if (!shouldVisitRHS(E->getLHS())) {
      GapRegionCounter = OutCount;
    }

    // Extract the Parent Region Counter.
    Counter ParentCnt = getRegion().getCounter();

    // Create Branch Region around LHS condition.
    if (!llvm::EnableSingleByteCoverage)
      createBranchRegion(E->getLHS(), subtractCounters(ParentCnt, RHSExecCnt),
                         RHSExecCnt, DecisionLHS);

    // Create Branch Region around RHS condition.
    if (!llvm::EnableSingleByteCoverage)
      createBranchRegion(E->getRHS(), subtractCounters(RHSExecCnt, RHSFalseCnt),
                         RHSFalseCnt, DecisionRHS);

    // Create MCDC Decision Region if at top-level (root).
    if (IsRootNode)
      createOrCancelDecision(E, SourceRegionsSince);
  }

  void VisitLambdaExpr(const LambdaExpr *LE) {
    // Lambdas are treated as their own functions for now, so we shouldn't
    // propagate counts into them.
  }

  void VisitArrayInitLoopExpr(const ArrayInitLoopExpr *AILE) {
    Visit(AILE->getCommonExpr()->getSourceExpr());
  }

  void VisitPseudoObjectExpr(const PseudoObjectExpr *POE) {
    // Just visit syntatic expression as this is what users actually write.
    VisitStmt(POE->getSyntacticForm());
  }

  void VisitOpaqueValueExpr(const OpaqueValueExpr* OVE) {
    if (OVE->isUnique())
      Visit(OVE->getSourceExpr());
  }
};

} // end anonymous namespace

static void dump(llvm::raw_ostream &OS, StringRef FunctionName,
                 ArrayRef<CounterExpression> Expressions,
                 ArrayRef<CounterMappingRegion> Regions) {
  OS << FunctionName << ":\n";
  CounterMappingContext Ctx(Expressions);
  for (const auto &R : Regions) {
    OS.indent(2);
    switch (R.Kind) {
    case CounterMappingRegion::CodeRegion:
      break;
    case CounterMappingRegion::ExpansionRegion:
      OS << "Expansion,";
      break;
    case CounterMappingRegion::SkippedRegion:
      OS << "Skipped,";
      break;
    case CounterMappingRegion::GapRegion:
      OS << "Gap,";
      break;
    case CounterMappingRegion::BranchRegion:
    case CounterMappingRegion::MCDCBranchRegion:
      OS << "Branch,";
      break;
    case CounterMappingRegion::MCDCDecisionRegion:
      OS << "Decision,";
      break;
    }

    OS << "File " << R.FileID << ", " << R.LineStart << ":" << R.ColumnStart
       << " -> " << R.LineEnd << ":" << R.ColumnEnd << " = ";

    if (const auto *DecisionParams =
            std::get_if<mcdc::DecisionParameters>(&R.MCDCParams)) {
      OS << "M:" << DecisionParams->BitmapIdx;
      OS << ", C:" << DecisionParams->NumConditions;
    } else {
      Ctx.dump(R.Count, OS);

      if (R.Kind == CounterMappingRegion::BranchRegion ||
          R.Kind == CounterMappingRegion::MCDCBranchRegion) {
        OS << ", ";
        Ctx.dump(R.FalseCount, OS);
      }
    }

    if (const auto *BranchParams =
            std::get_if<mcdc::BranchParameters>(&R.MCDCParams)) {
      OS << " [" << BranchParams->ID + 1 << ","
         << BranchParams->Conds[true] + 1;
      OS << "," << BranchParams->Conds[false] + 1 << "] ";
    }

    if (R.Kind == CounterMappingRegion::ExpansionRegion)
      OS << " (Expanded file = " << R.ExpandedFileID << ")";
    OS << "\n";
  }
}

CoverageMappingModuleGen::CoverageMappingModuleGen(
    CodeGenModule &CGM, CoverageSourceInfo &SourceInfo)
    : CGM(CGM), SourceInfo(SourceInfo) {}

std::string CoverageMappingModuleGen::getCurrentDirname() {
  if (!CGM.getCodeGenOpts().CoverageCompilationDir.empty())
    return CGM.getCodeGenOpts().CoverageCompilationDir;

  SmallString<256> CWD;
  llvm::sys::fs::current_path(CWD);
  return CWD.str().str();
}

std::string CoverageMappingModuleGen::normalizeFilename(StringRef Filename) {
  llvm::SmallString<256> Path(Filename);
  llvm::sys::path::remove_dots(Path, /*remove_dot_dot=*/true);

  /// Traverse coverage prefix map in reverse order because prefix replacements
  /// are applied in reverse order starting from the last one when multiple
  /// prefix replacement options are provided.
  for (const auto &[From, To] :
       llvm::reverse(CGM.getCodeGenOpts().CoveragePrefixMap)) {
    if (llvm::sys::path::replace_path_prefix(Path, From, To))
      break;
  }
  return Path.str().str();
}

static std::string getInstrProfSection(const CodeGenModule &CGM,
                                       llvm::InstrProfSectKind SK) {
  return llvm::getInstrProfSectionName(
      SK, CGM.getContext().getTargetInfo().getTriple().getObjectFormat());
}

void CoverageMappingModuleGen::emitFunctionMappingRecord(
    const FunctionInfo &Info, uint64_t FilenamesRef) {
  llvm::LLVMContext &Ctx = CGM.getLLVMContext();

  // Assign a name to the function record. This is used to merge duplicates.
  std::string FuncRecordName = "__covrec_" + llvm::utohexstr(Info.NameHash);

  // A dummy description for a function included-but-not-used in a TU can be
  // replaced by full description provided by a different TU. The two kinds of
  // descriptions play distinct roles: therefore, assign them different names
  // to prevent `linkonce_odr` merging.
  if (Info.IsUsed)
    FuncRecordName += "u";

  // Create the function record type.
  const uint64_t NameHash = Info.NameHash;
  const uint64_t FuncHash = Info.FuncHash;
  const std::string &CoverageMapping = Info.CoverageMapping;
#define COVMAP_FUNC_RECORD(Type, LLVMType, Name, Init) LLVMType,
  llvm::Type *FunctionRecordTypes[] = {
#include "llvm/ProfileData/InstrProfData.inc"
  };
  auto *FunctionRecordTy =
      llvm::StructType::get(Ctx, ArrayRef(FunctionRecordTypes),
                            /*isPacked=*/true);

  // Create the function record constant.
#define COVMAP_FUNC_RECORD(Type, LLVMType, Name, Init) Init,
  llvm::Constant *FunctionRecordVals[] = {
      #include "llvm/ProfileData/InstrProfData.inc"
  };
  auto *FuncRecordConstant =
      llvm::ConstantStruct::get(FunctionRecordTy, ArrayRef(FunctionRecordVals));

  // Create the function record global.
  auto *FuncRecord = new llvm::GlobalVariable(
      CGM.getModule(), FunctionRecordTy, /*isConstant=*/true,
      llvm::GlobalValue::LinkOnceODRLinkage, FuncRecordConstant,
      FuncRecordName);
  FuncRecord->setVisibility(llvm::GlobalValue::HiddenVisibility);
  FuncRecord->setSection(getInstrProfSection(CGM, llvm::IPSK_covfun));
  FuncRecord->setAlignment(llvm::Align(8));
  if (CGM.supportsCOMDAT())
    FuncRecord->setComdat(CGM.getModule().getOrInsertComdat(FuncRecordName));

  // Make sure the data doesn't get deleted.
  CGM.addUsedGlobal(FuncRecord);
}

void CoverageMappingModuleGen::addFunctionMappingRecord(
    llvm::GlobalVariable *NamePtr, StringRef NameValue, uint64_t FuncHash,
    const std::string &CoverageMapping, bool IsUsed) {
  const uint64_t NameHash = llvm::IndexedInstrProf::ComputeHash(NameValue);
  FunctionRecords.push_back({NameHash, FuncHash, CoverageMapping, IsUsed});

  if (!IsUsed)
    FunctionNames.push_back(NamePtr);

  if (CGM.getCodeGenOpts().DumpCoverageMapping) {
    // Dump the coverage mapping data for this function by decoding the
    // encoded data. This allows us to dump the mapping regions which were
    // also processed by the CoverageMappingWriter which performs
    // additional minimization operations such as reducing the number of
    // expressions.
    llvm::SmallVector<std::string, 16> FilenameStrs;
    std::vector<StringRef> Filenames;
    std::vector<CounterExpression> Expressions;
    std::vector<CounterMappingRegion> Regions;
    FilenameStrs.resize(FileEntries.size() + 1);
    FilenameStrs[0] = normalizeFilename(getCurrentDirname());
    for (const auto &Entry : FileEntries) {
      auto I = Entry.second;
      FilenameStrs[I] = normalizeFilename(Entry.first.getName());
    }
    ArrayRef<std::string> FilenameRefs = llvm::ArrayRef(FilenameStrs);
    RawCoverageMappingReader Reader(CoverageMapping, FilenameRefs, Filenames,
                                    Expressions, Regions);
    if (Reader.read())
      return;
    dump(llvm::outs(), NameValue, Expressions, Regions);
  }
}

void CoverageMappingModuleGen::emit() {
  if (FunctionRecords.empty())
    return;
  llvm::LLVMContext &Ctx = CGM.getLLVMContext();
  auto *Int32Ty = llvm::Type::getInt32Ty(Ctx);

  // Create the filenames and merge them with coverage mappings
  llvm::SmallVector<std::string, 16> FilenameStrs;
  FilenameStrs.resize(FileEntries.size() + 1);
  // The first filename is the current working directory.
  FilenameStrs[0] = normalizeFilename(getCurrentDirname());
  for (const auto &Entry : FileEntries) {
    auto I = Entry.second;
    FilenameStrs[I] = normalizeFilename(Entry.first.getName());
  }

  std::string Filenames;
  {
    llvm::raw_string_ostream OS(Filenames);
    CoverageFilenamesSectionWriter(FilenameStrs).write(OS);
  }
  auto *FilenamesVal =
      llvm::ConstantDataArray::getString(Ctx, Filenames, false);
  const int64_t FilenamesRef = llvm::IndexedInstrProf::ComputeHash(Filenames);

  // Emit the function records.
  for (const FunctionInfo &Info : FunctionRecords)
    emitFunctionMappingRecord(Info, FilenamesRef);

  const unsigned NRecords = 0;
  const size_t FilenamesSize = Filenames.size();
  const unsigned CoverageMappingSize = 0;
  llvm::Type *CovDataHeaderTypes[] = {
#define COVMAP_HEADER(Type, LLVMType, Name, Init) LLVMType,
#include "llvm/ProfileData/InstrProfData.inc"
  };
  auto CovDataHeaderTy =
      llvm::StructType::get(Ctx, ArrayRef(CovDataHeaderTypes));
  llvm::Constant *CovDataHeaderVals[] = {
#define COVMAP_HEADER(Type, LLVMType, Name, Init) Init,
#include "llvm/ProfileData/InstrProfData.inc"
  };
  auto CovDataHeaderVal =
      llvm::ConstantStruct::get(CovDataHeaderTy, ArrayRef(CovDataHeaderVals));

  // Create the coverage data record
  llvm::Type *CovDataTypes[] = {CovDataHeaderTy, FilenamesVal->getType()};
  auto CovDataTy = llvm::StructType::get(Ctx, ArrayRef(CovDataTypes));
  llvm::Constant *TUDataVals[] = {CovDataHeaderVal, FilenamesVal};
  auto CovDataVal = llvm::ConstantStruct::get(CovDataTy, ArrayRef(TUDataVals));
  auto CovData = new llvm::GlobalVariable(
      CGM.getModule(), CovDataTy, true, llvm::GlobalValue::PrivateLinkage,
      CovDataVal, llvm::getCoverageMappingVarName());

  CovData->setSection(getInstrProfSection(CGM, llvm::IPSK_covmap));
  CovData->setAlignment(llvm::Align(8));

  // Make sure the data doesn't get deleted.
  CGM.addUsedGlobal(CovData);
  // Create the deferred function records array
  if (!FunctionNames.empty()) {
    auto NamesArrTy = llvm::ArrayType::get(llvm::PointerType::getUnqual(Ctx),
                                           FunctionNames.size());
    auto NamesArrVal = llvm::ConstantArray::get(NamesArrTy, FunctionNames);
    // This variable will *NOT* be emitted to the object file. It is used
    // to pass the list of names referenced to codegen.
    new llvm::GlobalVariable(CGM.getModule(), NamesArrTy, true,
                             llvm::GlobalValue::InternalLinkage, NamesArrVal,
                             llvm::getCoverageUnusedNamesVarName());
  }
}

unsigned CoverageMappingModuleGen::getFileID(FileEntryRef File) {
  auto It = FileEntries.find(File);
  if (It != FileEntries.end())
    return It->second;
  unsigned FileID = FileEntries.size() + 1;
  FileEntries.insert(std::make_pair(File, FileID));
  return FileID;
}

void CoverageMappingGen::emitCounterMapping(const Decl *D,
                                            llvm::raw_ostream &OS) {
  assert(CounterMap && MCDCState);
  CounterCoverageMappingBuilder Walker(CVM, *CounterMap, *MCDCState, SM,
                                       LangOpts);
  Walker.VisitDecl(D);
  Walker.write(OS);
}

void CoverageMappingGen::emitEmptyMapping(const Decl *D,
                                          llvm::raw_ostream &OS) {
  EmptyCoverageMappingBuilder Walker(CVM, SM, LangOpts);
  Walker.VisitDecl(D);
  Walker.write(OS);
}
