//===- Line.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_LINE_H
#define LLVM_DEBUGINFO_CODEVIEW_LINE_H

#include "llvm/Support/Endian.h"
#include <cinttypes>

namespace llvm {
namespace codeview {

using llvm::support::ulittle32_t;

class LineInfo {
public:
  enum : uint32_t {
    AlwaysStepIntoLineNumber = 0xfeefee,
    NeverStepIntoLineNumber = 0xf00f00
  };

  enum : int { EndLineDeltaShift = 24 };

  enum : uint32_t {
    StartLineMask = 0x00ffffff,
    EndLineDeltaMask = 0x7f000000,
    StatementFlag = 0x80000000u
  };

  LineInfo(uint32_t StartLine, uint32_t EndLine, bool IsStatement);
  LineInfo(uint32_t LineData) : LineData(LineData) {}

  uint32_t getStartLine() const { return LineData & StartLineMask; }

  uint32_t getLineDelta() const {
    return (LineData & EndLineDeltaMask) >> EndLineDeltaShift;
  }

  uint32_t getEndLine() const { return getStartLine() + getLineDelta(); }

  bool isStatement() const { return (LineData & StatementFlag) != 0; }

  uint32_t getRawData() const { return LineData; }

  bool isAlwaysStepInto() const {
    return getStartLine() == AlwaysStepIntoLineNumber;
  }

  bool isNeverStepInto() const {
    return getStartLine() == NeverStepIntoLineNumber;
  }

private:
  uint32_t LineData;
};

class ColumnInfo {
private:
  static const uint32_t StartColumnMask = 0x0000ffffu;
  static const uint32_t EndColumnMask = 0xffff0000u;
  static const int EndColumnShift = 16;

public:
  ColumnInfo(uint16_t StartColumn, uint16_t EndColumn) {
    ColumnData =
        (static_cast<uint32_t>(StartColumn) & StartColumnMask) |
        ((static_cast<uint32_t>(EndColumn) << EndColumnShift) & EndColumnMask);
  }

  uint16_t getStartColumn() const {
    return static_cast<uint16_t>(ColumnData & StartColumnMask);
  }

  uint16_t getEndColumn() const {
    return static_cast<uint16_t>((ColumnData & EndColumnMask) >>
                                 EndColumnShift);
  }

  uint32_t getRawData() const { return ColumnData; }

private:
  uint32_t ColumnData;
};

class Line {
private:
  int32_t CodeOffset;
  LineInfo LineInf;
  ColumnInfo ColumnInf;

public:
  Line(int32_t CodeOffset, uint32_t StartLine, uint32_t EndLine,
       uint16_t StartColumn, uint16_t EndColumn, bool IsStatement)
      : CodeOffset(CodeOffset), LineInf(StartLine, EndLine, IsStatement),
        ColumnInf(StartColumn, EndColumn) {}

  Line(int32_t CodeOffset, LineInfo LineInf, ColumnInfo ColumnInf)
      : CodeOffset(CodeOffset), LineInf(LineInf), ColumnInf(ColumnInf) {}

  LineInfo getLineInfo() const { return LineInf; }

  ColumnInfo getColumnInfo() const { return ColumnInf; }

  int32_t getCodeOffset() const { return CodeOffset; }

  uint32_t getStartLine() const { return LineInf.getStartLine(); }

  uint32_t getLineDelta() const { return LineInf.getLineDelta(); }

  uint32_t getEndLine() const { return LineInf.getEndLine(); }

  uint16_t getStartColumn() const { return ColumnInf.getStartColumn(); }

  uint16_t getEndColumn() const { return ColumnInf.getEndColumn(); }

  bool isStatement() const { return LineInf.isStatement(); }

  bool isAlwaysStepInto() const { return LineInf.isAlwaysStepInto(); }

  bool isNeverStepInto() const { return LineInf.isNeverStepInto(); }
};

} // namespace codeview
} // namespace llvm

#endif
