//===- llvm/Bitcode/BitcodeAnalyzer.h - Bitcode analyzer --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines interfaces to analyze LLVM bitcode files/streams.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITCODEANALYZER_H
#define LLVM_BITCODE_BITCODEANALYZER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Support/Error.h"
#include <map>
#include <optional>
#include <vector>

namespace llvm {

class raw_ostream;

/// CurStreamTypeType - A type for CurStreamType
enum CurStreamTypeType {
  UnknownBitstream,
  LLVMIRBitstream,
  ClangSerializedASTBitstream,
  ClangSerializedDiagnosticsBitstream,
  LLVMBitstreamRemarks
};

struct BCDumpOptions {
  /// The stream.
  raw_ostream &OS;
  /// Print per-code histogram.
  bool Histogram = false;
  /// Don't emit numeric info in dump if symbolic info is available.
  bool Symbolic = false;
  /// Print binary blobs using hex escapes.
  bool ShowBinaryBlobs = false;
  /// Print BLOCKINFO block details.
  bool DumpBlockinfo = false;

  BCDumpOptions(raw_ostream &OS) : OS(OS) {}
};

class BitcodeAnalyzer {
  BitstreamCursor Stream;
  BitstreamBlockInfo BlockInfo;
  CurStreamTypeType CurStreamType;
  std::optional<BitstreamCursor> BlockInfoStream;
  unsigned NumTopBlocks = 0;

  struct PerRecordStats {
    unsigned NumInstances = 0;
    unsigned NumAbbrev = 0;
    uint64_t TotalBits = 0;
    PerRecordStats() = default;
  };

  struct PerBlockIDStats {
    /// NumInstances - This the number of times this block ID has been seen.
    unsigned NumInstances = 0;
    /// NumBits - The total size in bits of all of these blocks.
    uint64_t NumBits = 0;
    /// NumSubBlocks - The total number of blocks these blocks contain.
    unsigned NumSubBlocks = 0;
    /// NumAbbrevs - The total number of abbreviations.
    unsigned NumAbbrevs = 0;
    /// NumRecords - The total number of records these blocks contain, and the
    /// number that are abbreviated.
    unsigned NumRecords = 0, NumAbbreviatedRecords = 0;
    /// CodeFreq - Keep track of the number of times we see each code.
    std::vector<PerRecordStats> CodeFreq;
    PerBlockIDStats() = default;
  };

  std::map<unsigned, PerBlockIDStats> BlockIDStats;

public:
  BitcodeAnalyzer(StringRef Buffer,
                  std::optional<StringRef> BlockInfoBuffer = std::nullopt);
  /// Analyze the bitcode file.
  Error analyze(std::optional<BCDumpOptions> O = std::nullopt,
                std::optional<StringRef> CheckHash = std::nullopt);
  /// Print stats about the bitcode file.
  void printStats(BCDumpOptions O,
                  std::optional<StringRef> Filename = std::nullopt);

private:
  /// Read a block, updating statistics, etc.
  Error parseBlock(unsigned BlockID, unsigned IndentLevel,
                   std::optional<BCDumpOptions> O = std::nullopt,
                   std::optional<StringRef> CheckHash = std::nullopt);

  Error decodeMetadataStringsBlob(StringRef Indent, ArrayRef<uint64_t> Record,
                                  StringRef Blob, raw_ostream &OS);
};

} // end namespace llvm

#endif // LLVM_BITCODE_BITCODEANALYZER_H
