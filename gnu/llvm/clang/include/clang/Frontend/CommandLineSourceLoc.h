
//===--- CommandLineSourceLoc.h - Parsing for source locations-*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Command line parsing for source locations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_COMMANDLINESOURCELOC_H
#define LLVM_CLANG_FRONTEND_COMMANDLINESOURCELOC_H

#include "clang/Basic/LLVM.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

namespace clang {

/// A source location that has been parsed on the command line.
struct ParsedSourceLocation {
  std::string FileName;
  unsigned Line;
  unsigned Column;

public:
  /// Construct a parsed source location from a string; the Filename is empty on
  /// error.
  static ParsedSourceLocation FromString(StringRef Str) {
    ParsedSourceLocation PSL;
    std::pair<StringRef, StringRef> ColSplit = Str.rsplit(':');
    std::pair<StringRef, StringRef> LineSplit =
      ColSplit.first.rsplit(':');

    // If both tail splits were valid integers, return success.
    if (!ColSplit.second.getAsInteger(10, PSL.Column) &&
        !LineSplit.second.getAsInteger(10, PSL.Line)) {
      PSL.FileName = std::string(LineSplit.first);

      // On the command-line, stdin may be specified via "-". Inside the
      // compiler, stdin is called "<stdin>".
      if (PSL.FileName == "-")
        PSL.FileName = "<stdin>";
    }

    return PSL;
  }

  /// Serialize ParsedSourceLocation back to a string.
  std::string ToString() const {
    return (llvm::Twine(FileName == "<stdin>" ? "-" : FileName) + ":" +
            Twine(Line) + ":" + Twine(Column))
        .str();
  }
};

/// A source range that has been parsed on the command line.
struct ParsedSourceRange {
  std::string FileName;
  /// The starting location of the range. The first element is the line and
  /// the second element is the column.
  std::pair<unsigned, unsigned> Begin;
  /// The ending location of the range. The first element is the line and the
  /// second element is the column.
  std::pair<unsigned, unsigned> End;

  /// Returns a parsed source range from a string or std::nullopt if the string
  /// is invalid.
  ///
  /// These source string has the following format:
  ///
  /// file:start_line:start_column[-end_line:end_column]
  ///
  /// If the end line and column are omitted, the starting line and columns
  /// are used as the end values.
  static std::optional<ParsedSourceRange> fromString(StringRef Str) {
    std::pair<StringRef, StringRef> RangeSplit = Str.rsplit('-');
    unsigned EndLine, EndColumn;
    bool HasEndLoc = false;
    if (!RangeSplit.second.empty()) {
      std::pair<StringRef, StringRef> Split = RangeSplit.second.rsplit(':');
      if (Split.first.getAsInteger(10, EndLine) ||
          Split.second.getAsInteger(10, EndColumn)) {
        // The string does not end in end_line:end_column, so the '-'
        // probably belongs to the filename which menas the whole
        // string should be parsed.
        RangeSplit.first = Str;
      } else
        HasEndLoc = true;
    }
    auto Begin = ParsedSourceLocation::FromString(RangeSplit.first);
    if (Begin.FileName.empty())
      return std::nullopt;
    if (!HasEndLoc) {
      EndLine = Begin.Line;
      EndColumn = Begin.Column;
    }
    return ParsedSourceRange{std::move(Begin.FileName),
                             {Begin.Line, Begin.Column},
                             {EndLine, EndColumn}};
  }
};
}

namespace llvm {
  namespace cl {
    /// Command-line option parser that parses source locations.
    ///
    /// Source locations are of the form filename:line:column.
    template<>
    class parser<clang::ParsedSourceLocation> final
      : public basic_parser<clang::ParsedSourceLocation> {
    public:
      inline bool parse(Option &O, StringRef ArgName, StringRef ArgValue,
                 clang::ParsedSourceLocation &Val);
    };

    bool
    parser<clang::ParsedSourceLocation>::
    parse(Option &O, StringRef ArgName, StringRef ArgValue,
          clang::ParsedSourceLocation &Val) {
      using namespace clang;

      Val = ParsedSourceLocation::FromString(ArgValue);
      if (Val.FileName.empty()) {
        errs() << "error: "
               << "source location must be of the form filename:line:column\n";
        return true;
      }

      return false;
    }
  }
}

#endif
