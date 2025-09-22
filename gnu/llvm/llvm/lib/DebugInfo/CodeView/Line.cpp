//===-- Line.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/Line.h"

using namespace llvm;
using namespace codeview;

LineInfo::LineInfo(uint32_t StartLine, uint32_t EndLine, bool IsStatement) {
  LineData = StartLine & StartLineMask;
  uint32_t LineDelta = EndLine - StartLine;
  LineData |= (LineDelta << EndLineDeltaShift) & EndLineDeltaMask;
  if (IsStatement) {
    LineData |= StatementFlag;
  }
}
