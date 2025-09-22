//===-- llvm/Remarks/Remark.h - The remark type -----------------*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an interface for parsing remarks in LLVM.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_REMARKPARSER_H
#define LLVM_REMARKS_REMARKPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Remarks/RemarkFormat.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <optional>

namespace llvm {
namespace remarks {

struct Remark;

class EndOfFileError : public ErrorInfo<EndOfFileError> {
public:
  static char ID;

  EndOfFileError() = default;

  void log(raw_ostream &OS) const override { OS << "End of file reached."; }
  std::error_code convertToErrorCode() const override {
    return inconvertibleErrorCode();
  }
};

/// Parser used to parse a raw buffer to remarks::Remark objects.
struct RemarkParser {
  /// The format of the parser.
  Format ParserFormat;
  /// Path to prepend when opening an external remark file.
  std::string ExternalFilePrependPath;

  RemarkParser(Format ParserFormat) : ParserFormat(ParserFormat) {}

  /// If no error occurs, this returns a valid Remark object.
  /// If an error of type EndOfFileError occurs, it is safe to recover from it
  /// by stopping the parsing.
  /// If any other error occurs, it should be propagated to the user.
  /// The pointer should never be null.
  virtual Expected<std::unique_ptr<Remark>> next() = 0;

  virtual ~RemarkParser() = default;
};

/// In-memory representation of the string table parsed from a buffer (e.g. the
/// remarks section).
struct ParsedStringTable {
  /// The buffer mapped from the section contents.
  StringRef Buffer;
  /// This object has high changes to be std::move'd around, so don't use a
  /// SmallVector for once.
  std::vector<size_t> Offsets;

  ParsedStringTable(StringRef Buffer);
  /// Disable copy.
  ParsedStringTable(const ParsedStringTable &) = delete;
  ParsedStringTable &operator=(const ParsedStringTable &) = delete;
  /// Should be movable.
  ParsedStringTable(ParsedStringTable &&) = default;
  ParsedStringTable &operator=(ParsedStringTable &&) = default;

  size_t size() const { return Offsets.size(); }
  Expected<StringRef> operator[](size_t Index) const;
};

Expected<std::unique_ptr<RemarkParser>> createRemarkParser(Format ParserFormat,
                                                           StringRef Buf);

Expected<std::unique_ptr<RemarkParser>>
createRemarkParser(Format ParserFormat, StringRef Buf,
                   ParsedStringTable StrTab);

Expected<std::unique_ptr<RemarkParser>> createRemarkParserFromMeta(
    Format ParserFormat, StringRef Buf,
    std::optional<ParsedStringTable> StrTab = std::nullopt,
    std::optional<StringRef> ExternalFilePrependPath = std::nullopt);

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_REMARKPARSER_H
