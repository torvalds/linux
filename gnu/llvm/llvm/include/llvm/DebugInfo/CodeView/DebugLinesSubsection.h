//===- DebugLinesSubsection.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGLINESSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGLINESSUBSECTION_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/DebugInfo/CodeView/Line.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace llvm {
class BinaryStreamReader;
class BinaryStreamWriter;
namespace codeview {

class DebugChecksumsSubsection;
class DebugStringTableSubsection;

// Corresponds to the `CV_DebugSLinesHeader_t` structure.
struct LineFragmentHeader {
  support::ulittle32_t RelocOffset;  // Code offset of line contribution.
  support::ulittle16_t RelocSegment; // Code segment of line contribution.
  support::ulittle16_t Flags;        // See LineFlags enumeration.
  support::ulittle32_t CodeSize;     // Code size of this line contribution.
};

// Corresponds to the `CV_DebugSLinesFileBlockHeader_t` structure.
struct LineBlockFragmentHeader {
  support::ulittle32_t NameIndex; // Offset of FileChecksum entry in File
                                  // checksums buffer.  The checksum entry then
                                  // contains another offset into the string
                                  // table of the actual name.
  support::ulittle32_t NumLines;  // Number of lines
  support::ulittle32_t BlockSize; // Code size of block, in bytes.
  // The following two variable length arrays appear immediately after the
  // header.  The structure definitions follow.
  // LineNumberEntry   Lines[NumLines];
  // ColumnNumberEntry Columns[NumLines];
};

// Corresponds to `CV_Line_t` structure
struct LineNumberEntry {
  support::ulittle32_t Offset; // Offset to start of code bytes for line number
  support::ulittle32_t Flags;  // Start:24, End:7, IsStatement:1
};

// Corresponds to `CV_Column_t` structure
struct ColumnNumberEntry {
  support::ulittle16_t StartColumn;
  support::ulittle16_t EndColumn;
};

struct LineColumnEntry {
  support::ulittle32_t NameIndex;
  FixedStreamArray<LineNumberEntry> LineNumbers;
  FixedStreamArray<ColumnNumberEntry> Columns;
};

class LineColumnExtractor {
public:
  Error operator()(BinaryStreamRef Stream, uint32_t &Len,
                   LineColumnEntry &Item);

  const LineFragmentHeader *Header = nullptr;
};

class DebugLinesSubsectionRef final : public DebugSubsectionRef {
  friend class LineColumnExtractor;

  using LineInfoArray = VarStreamArray<LineColumnEntry, LineColumnExtractor>;
  using Iterator = LineInfoArray::Iterator;

public:
  DebugLinesSubsectionRef();

  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::Lines;
  }

  Error initialize(BinaryStreamReader Reader);

  Iterator begin() const { return LinesAndColumns.begin(); }
  Iterator end() const { return LinesAndColumns.end(); }

  const LineFragmentHeader *header() const { return Header; }

  bool hasColumnInfo() const;

private:
  const LineFragmentHeader *Header = nullptr;
  LineInfoArray LinesAndColumns;
};

class DebugLinesSubsection final : public DebugSubsection {
  struct Block {
    Block(uint32_t ChecksumBufferOffset)
        : ChecksumBufferOffset(ChecksumBufferOffset) {}

    uint32_t ChecksumBufferOffset;
    std::vector<LineNumberEntry> Lines;
    std::vector<ColumnNumberEntry> Columns;
  };

public:
  DebugLinesSubsection(DebugChecksumsSubsection &Checksums,
                       DebugStringTableSubsection &Strings);

  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::Lines;
  }

  void createBlock(StringRef FileName);
  void addLineInfo(uint32_t Offset, const LineInfo &Line);
  void addLineAndColumnInfo(uint32_t Offset, const LineInfo &Line,
                            uint32_t ColStart, uint32_t ColEnd);

  uint32_t calculateSerializedSize() const override;
  Error commit(BinaryStreamWriter &Writer) const override;

  void setRelocationAddress(uint16_t Segment, uint32_t Offset);
  void setCodeSize(uint32_t Size);
  void setFlags(LineFlags Flags);

  bool hasColumnInfo() const;

private:
  DebugChecksumsSubsection &Checksums;
  uint32_t RelocOffset = 0;
  uint16_t RelocSegment = 0;
  uint32_t CodeSize = 0;
  LineFlags Flags = LF_None;
  std::vector<Block> Blocks;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGLINESSUBSECTION_H
