//===--- TestSupport.cpp - Clang-based refactoring tool -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements routines that provide refactoring testing
/// utilities.
///
//===----------------------------------------------------------------------===//

#include "TestSupport.h"
#include "clang/Basic/DiagnosticError.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace llvm;

namespace clang {
namespace refactor {

void TestSelectionRangesInFile::dump(raw_ostream &OS) const {
  for (const auto &Group : GroupedRanges) {
    OS << "Test selection group '" << Group.Name << "':\n";
    for (const auto &Range : Group.Ranges) {
      OS << "  " << Range.Begin << "-" << Range.End << "\n";
    }
  }
}

bool TestSelectionRangesInFile::foreachRange(
    const SourceManager &SM,
    llvm::function_ref<void(SourceRange)> Callback) const {
  auto FE = SM.getFileManager().getFile(Filename);
  FileID FID = FE ? SM.translateFile(*FE) : FileID();
  if (!FE || FID.isInvalid()) {
    llvm::errs() << "error: -selection=test:" << Filename
                 << " : given file is not in the target TU";
    return true;
  }
  SourceLocation FileLoc = SM.getLocForStartOfFile(FID);
  for (const auto &Group : GroupedRanges) {
    for (const TestSelectionRange &Range : Group.Ranges) {
      // Translate the offset pair to a true source range.
      SourceLocation Start =
          SM.getMacroArgExpandedLocation(FileLoc.getLocWithOffset(Range.Begin));
      SourceLocation End =
          SM.getMacroArgExpandedLocation(FileLoc.getLocWithOffset(Range.End));
      assert(Start.isValid() && End.isValid() && "unexpected invalid range");
      Callback(SourceRange(Start, End));
    }
  }
  return false;
}

namespace {

void dumpChanges(const tooling::AtomicChanges &Changes, raw_ostream &OS) {
  for (const auto &Change : Changes)
    OS << const_cast<tooling::AtomicChange &>(Change).toYAMLString() << "\n";
}

bool areChangesSame(const tooling::AtomicChanges &LHS,
                    const tooling::AtomicChanges &RHS) {
  if (LHS.size() != RHS.size())
    return false;
  for (auto I : llvm::zip(LHS, RHS)) {
    if (!(std::get<0>(I) == std::get<1>(I)))
      return false;
  }
  return true;
}

bool printRewrittenSources(const tooling::AtomicChanges &Changes,
                           raw_ostream &OS) {
  std::set<std::string> Files;
  for (const auto &Change : Changes)
    Files.insert(Change.getFilePath());
  tooling::ApplyChangesSpec Spec;
  Spec.Cleanup = false;
  for (const auto &File : Files) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> BufferErr =
        llvm::MemoryBuffer::getFile(File);
    if (!BufferErr) {
      llvm::errs() << "failed to open" << File << "\n";
      return true;
    }
    auto Result = tooling::applyAtomicChanges(File, (*BufferErr)->getBuffer(),
                                              Changes, Spec);
    if (!Result) {
      llvm::errs() << toString(Result.takeError());
      return true;
    }
    OS << *Result;
  }
  return false;
}

class TestRefactoringResultConsumer final
    : public ClangRefactorToolConsumerInterface {
public:
  TestRefactoringResultConsumer(const TestSelectionRangesInFile &TestRanges)
      : TestRanges(TestRanges) {
    Results.push_back({});
  }

  ~TestRefactoringResultConsumer() {
    // Ensure all results are checked.
    for (auto &Group : Results) {
      for (auto &Result : Group) {
        if (!Result) {
          (void)llvm::toString(Result.takeError());
        }
      }
    }
  }

  void handleError(llvm::Error Err) override { handleResult(std::move(Err)); }

  void handle(tooling::AtomicChanges Changes) override {
    handleResult(std::move(Changes));
  }

  void handle(tooling::SymbolOccurrences Occurrences) override {
    tooling::RefactoringResultConsumer::handle(std::move(Occurrences));
  }

private:
  bool handleAllResults();

  void handleResult(Expected<tooling::AtomicChanges> Result) {
    Results.back().push_back(std::move(Result));
    size_t GroupIndex = Results.size() - 1;
    if (Results.back().size() >=
        TestRanges.GroupedRanges[GroupIndex].Ranges.size()) {
      ++GroupIndex;
      if (GroupIndex >= TestRanges.GroupedRanges.size()) {
        if (handleAllResults())
          exit(1); // error has occurred.
        return;
      }
      Results.push_back({});
    }
  }

  const TestSelectionRangesInFile &TestRanges;
  std::vector<std::vector<Expected<tooling::AtomicChanges>>> Results;
};

std::pair<unsigned, unsigned> getLineColumn(StringRef Filename,
                                            unsigned Offset) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> ErrOrFile =
      MemoryBuffer::getFile(Filename);
  if (!ErrOrFile)
    return {0, 0};
  StringRef Source = ErrOrFile.get()->getBuffer();
  Source = Source.take_front(Offset);
  size_t LastLine = Source.find_last_of("\r\n");
  return {Source.count('\n') + 1,
          (LastLine == StringRef::npos ? Offset : Offset - LastLine) + 1};
}

} // end anonymous namespace

bool TestRefactoringResultConsumer::handleAllResults() {
  bool Failed = false;
  for (const auto &Group : llvm::enumerate(Results)) {
    // All ranges in the group must produce the same result.
    std::optional<tooling::AtomicChanges> CanonicalResult;
    std::optional<std::string> CanonicalErrorMessage;
    for (const auto &I : llvm::enumerate(Group.value())) {
      Expected<tooling::AtomicChanges> &Result = I.value();
      std::string ErrorMessage;
      bool HasResult = !!Result;
      if (!HasResult) {
        handleAllErrors(
            Result.takeError(),
            [&](StringError &Err) { ErrorMessage = Err.getMessage(); },
            [&](DiagnosticError &Err) {
              const PartialDiagnosticAt &Diag = Err.getDiagnostic();
              llvm::SmallString<100> DiagText;
              Diag.second.EmitToString(getDiags(), DiagText);
              ErrorMessage = std::string(DiagText);
            });
      }
      if (!CanonicalResult && !CanonicalErrorMessage) {
        if (HasResult)
          CanonicalResult = std::move(*Result);
        else
          CanonicalErrorMessage = std::move(ErrorMessage);
        continue;
      }

      // Verify that this result corresponds to the canonical result.
      if (CanonicalErrorMessage) {
        // The error messages must match.
        if (!HasResult && ErrorMessage == *CanonicalErrorMessage)
          continue;
      } else {
        assert(CanonicalResult && "missing canonical result");
        // The results must match.
        if (HasResult && areChangesSame(*Result, *CanonicalResult))
          continue;
      }
      Failed = true;
      // Report the mismatch.
      std::pair<unsigned, unsigned> LineColumn = getLineColumn(
          TestRanges.Filename,
          TestRanges.GroupedRanges[Group.index()].Ranges[I.index()].Begin);
      llvm::errs()
          << "error: unexpected refactoring result for range starting at "
          << LineColumn.first << ':' << LineColumn.second << " in group '"
          << TestRanges.GroupedRanges[Group.index()].Name << "':\n  ";
      if (HasResult)
        llvm::errs() << "valid result";
      else
        llvm::errs() << "error '" << ErrorMessage << "'";
      llvm::errs() << " does not match initial ";
      if (CanonicalErrorMessage)
        llvm::errs() << "error '" << *CanonicalErrorMessage << "'\n";
      else
        llvm::errs() << "valid result\n";
      if (HasResult && !CanonicalErrorMessage) {
        llvm::errs() << "  Expected to Produce:\n";
        dumpChanges(*CanonicalResult, llvm::errs());
        llvm::errs() << "  Produced:\n";
        dumpChanges(*Result, llvm::errs());
      }
    }

    // Dump the results:
    const auto &TestGroup = TestRanges.GroupedRanges[Group.index()];
    if (!CanonicalResult) {
      llvm::outs() << TestGroup.Ranges.size() << " '" << TestGroup.Name
                   << "' results:\n";
      llvm::outs() << *CanonicalErrorMessage << "\n";
    } else {
      llvm::outs() << TestGroup.Ranges.size() << " '" << TestGroup.Name
                   << "' results:\n";
      if (printRewrittenSources(*CanonicalResult, llvm::outs()))
        return true;
    }
  }
  return Failed;
}

std::unique_ptr<ClangRefactorToolConsumerInterface>
TestSelectionRangesInFile::createConsumer() const {
  return std::make_unique<TestRefactoringResultConsumer>(*this);
}

/// Adds the \p ColumnOffset to file offset \p Offset, without going past a
/// newline.
static unsigned addColumnOffset(StringRef Source, unsigned Offset,
                                unsigned ColumnOffset) {
  if (!ColumnOffset)
    return Offset;
  StringRef Substr = Source.drop_front(Offset).take_front(ColumnOffset);
  size_t NewlinePos = Substr.find_first_of("\r\n");
  return Offset +
         (NewlinePos == StringRef::npos ? ColumnOffset : (unsigned)NewlinePos);
}

static unsigned addEndLineOffsetAndEndColumn(StringRef Source, unsigned Offset,
                                             unsigned LineNumberOffset,
                                             unsigned Column) {
  StringRef Line = Source.drop_front(Offset);
  unsigned LineOffset = 0;
  for (; LineNumberOffset != 0; --LineNumberOffset) {
    size_t NewlinePos = Line.find_first_of("\r\n");
    // Line offset goes out of bounds.
    if (NewlinePos == StringRef::npos)
      break;
    LineOffset += NewlinePos + 1;
    Line = Line.drop_front(NewlinePos + 1);
  }
  // Source now points to the line at +lineOffset;
  size_t LineStart = Source.find_last_of("\r\n", /*From=*/Offset + LineOffset);
  return addColumnOffset(
      Source, LineStart == StringRef::npos ? 0 : LineStart + 1, Column - 1);
}

std::optional<TestSelectionRangesInFile>
findTestSelectionRanges(StringRef Filename) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> ErrOrFile =
      MemoryBuffer::getFile(Filename);
  if (!ErrOrFile) {
    llvm::errs() << "error: -selection=test:" << Filename
                 << " : could not open the given file";
    return std::nullopt;
  }
  StringRef Source = ErrOrFile.get()->getBuffer();

  // See the doc comment for this function for the explanation of this
  // syntax.
  static const Regex RangeRegex(
      "range[[:blank:]]*([[:alpha:]_]*)?[[:blank:]]*=[[:"
      "blank:]]*(\\+[[:digit:]]+)?[[:blank:]]*(->[[:blank:]"
      "]*[\\+\\:[:digit:]]+)?");

  std::map<std::string, SmallVector<TestSelectionRange, 8>> GroupedRanges;

  LangOptions LangOpts;
  LangOpts.CPlusPlus = 1;
  LangOpts.CPlusPlus11 = 1;
  Lexer Lex(SourceLocation::getFromRawEncoding(0), LangOpts, Source.begin(),
            Source.begin(), Source.end());
  Lex.SetCommentRetentionState(true);
  Token Tok;
  for (Lex.LexFromRawLexer(Tok); Tok.isNot(tok::eof);
       Lex.LexFromRawLexer(Tok)) {
    if (Tok.isNot(tok::comment))
      continue;
    StringRef Comment =
        Source.substr(Tok.getLocation().getRawEncoding(), Tok.getLength());
    SmallVector<StringRef, 4> Matches;
    // Try to detect mistyped 'range:' comments to ensure tests don't miss
    // anything.
    auto DetectMistypedCommand = [&]() -> bool {
      if (Comment.contains_insensitive("range") && Comment.contains("=") &&
          !Comment.contains_insensitive("run") && !Comment.contains("CHECK")) {
        llvm::errs() << "error: suspicious comment '" << Comment
                     << "' that "
                        "resembles the range command found\n";
        llvm::errs() << "note: please reword if this isn't a range command\n";
      }
      return false;
    };
    // Allow CHECK: comments to contain range= commands.
    if (!RangeRegex.match(Comment, &Matches) || Comment.contains("CHECK")) {
      if (DetectMistypedCommand())
        return std::nullopt;
      continue;
    }
    unsigned Offset = Tok.getEndLoc().getRawEncoding();
    unsigned ColumnOffset = 0;
    if (!Matches[2].empty()) {
      // Don't forget to drop the '+'!
      if (Matches[2].drop_front().getAsInteger(10, ColumnOffset))
        assert(false && "regex should have produced a number");
    }
    Offset = addColumnOffset(Source, Offset, ColumnOffset);
    unsigned EndOffset;

    if (!Matches[3].empty()) {
      static const Regex EndLocRegex(
          "->[[:blank:]]*(\\+[[:digit:]]+):([[:digit:]]+)");
      SmallVector<StringRef, 4> EndLocMatches;
      if (!EndLocRegex.match(Matches[3], &EndLocMatches)) {
        if (DetectMistypedCommand())
          return std::nullopt;
        continue;
      }
      unsigned EndLineOffset = 0, EndColumn = 0;
      if (EndLocMatches[1].drop_front().getAsInteger(10, EndLineOffset) ||
          EndLocMatches[2].getAsInteger(10, EndColumn))
        assert(false && "regex should have produced a number");
      EndOffset = addEndLineOffsetAndEndColumn(Source, Offset, EndLineOffset,
                                               EndColumn);
    } else {
      EndOffset = Offset;
    }
    TestSelectionRange Range = {Offset, EndOffset};
    auto It = GroupedRanges.insert(std::make_pair(
        Matches[1].str(), SmallVector<TestSelectionRange, 8>{Range}));
    if (!It.second)
      It.first->second.push_back(Range);
  }
  if (GroupedRanges.empty()) {
    llvm::errs() << "error: -selection=test:" << Filename
                 << ": no 'range' commands";
    return std::nullopt;
  }

  TestSelectionRangesInFile TestRanges = {Filename.str(), {}};
  for (auto &Group : GroupedRanges)
    TestRanges.GroupedRanges.push_back({Group.first, std::move(Group.second)});
  return std::move(TestRanges);
}

} // end namespace refactor
} // end namespace clang
