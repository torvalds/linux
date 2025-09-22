//===-- BitstreamRemarkParser.h - Parser for Bitstream remarks --*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the impementation of the Bitstream remark parser.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_REMARKS_BITSTREAM_REMARK_PARSER_H
#define LLVM_LIB_REMARKS_BITSTREAM_REMARK_PARSER_H

#include "llvm/Remarks/BitstreamRemarkContainer.h"
#include "llvm/Remarks/BitstreamRemarkParser.h"
#include "llvm/Remarks/RemarkFormat.h"
#include "llvm/Remarks/RemarkParser.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace llvm {
namespace remarks {

struct Remark;

/// Parses and holds the state of the latest parsed remark.
struct BitstreamRemarkParser : public RemarkParser {
  /// The buffer to parse.
  BitstreamParserHelper ParserHelper;
  /// The string table used for parsing strings.
  std::optional<ParsedStringTable> StrTab;
  /// Temporary remark buffer used when the remarks are stored separately.
  std::unique_ptr<MemoryBuffer> TmpRemarkBuffer;
  /// The common metadata used to decide how to parse the buffer.
  /// This is filled when parsing the metadata block.
  uint64_t ContainerVersion = 0;
  uint64_t RemarkVersion = 0;
  BitstreamRemarkContainerType ContainerType =
      BitstreamRemarkContainerType::Standalone;
  /// Wether the parser is ready to parse remarks.
  bool ReadyToParseRemarks = false;

  /// Create a parser that expects to find a string table embedded in the
  /// stream.
  explicit BitstreamRemarkParser(StringRef Buf)
      : RemarkParser(Format::Bitstream), ParserHelper(Buf) {}

  /// Create a parser that uses a pre-parsed string table.
  BitstreamRemarkParser(StringRef Buf, ParsedStringTable StrTab)
      : RemarkParser(Format::Bitstream), ParserHelper(Buf),
        StrTab(std::move(StrTab)) {}

  Expected<std::unique_ptr<Remark>> next() override;

  static bool classof(const RemarkParser *P) {
    return P->ParserFormat == Format::Bitstream;
  }

  /// Parse and process the metadata of the buffer.
  Error parseMeta();

  /// Parse a Bitstream remark.
  Expected<std::unique_ptr<Remark>> parseRemark();

private:
  /// Helper functions.
  Error processCommonMeta(BitstreamMetaParserHelper &Helper);
  Error processStandaloneMeta(BitstreamMetaParserHelper &Helper);
  Error processSeparateRemarksFileMeta(BitstreamMetaParserHelper &Helper);
  Error processSeparateRemarksMetaMeta(BitstreamMetaParserHelper &Helper);
  Expected<std::unique_ptr<Remark>>
  processRemark(BitstreamRemarkParserHelper &Helper);
  Error processExternalFilePath(std::optional<StringRef> ExternalFilePath);
};

Expected<std::unique_ptr<BitstreamRemarkParser>> createBitstreamParserFromMeta(
    StringRef Buf, std::optional<ParsedStringTable> StrTab = std::nullopt,
    std::optional<StringRef> ExternalFilePrependPath = std::nullopt);

} // end namespace remarks
} // end namespace llvm

#endif /* LLVM_LIB_REMARKS_BITSTREAM_REMARK_PARSER_H */
