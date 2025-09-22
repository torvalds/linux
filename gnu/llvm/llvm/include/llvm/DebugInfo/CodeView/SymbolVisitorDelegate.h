//===-- SymbolVisitorDelegate.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLVISITORDELEGATE_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLVISITORDELEGATE_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm {

class BinaryStreamReader;

namespace codeview {

class DebugStringTableSubsectionRef;

class SymbolVisitorDelegate {
public:
  virtual ~SymbolVisitorDelegate() = default;

  virtual uint32_t getRecordOffset(BinaryStreamReader Reader) = 0;
  virtual StringRef getFileNameForFileOffset(uint32_t FileOffset) = 0;
  virtual DebugStringTableSubsectionRef getStringTable() = 0;
};

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SYMBOLVISITORDELEGATE_H
