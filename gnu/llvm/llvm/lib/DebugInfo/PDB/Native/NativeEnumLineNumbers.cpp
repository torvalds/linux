//==- NativeEnumLineNumbers.cpp - Native Type Enumerator impl ----*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeEnumLineNumbers.h"

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/Native/NativeLineNumber.h"

#include <vector>

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeEnumLineNumbers::NativeEnumLineNumbers(
    std::vector<NativeLineNumber> LineNums)
    : Lines(std::move(LineNums)), Index(0) {}

uint32_t NativeEnumLineNumbers::getChildCount() const {
  return static_cast<uint32_t>(Lines.size());
}

std::unique_ptr<IPDBLineNumber>
NativeEnumLineNumbers::getChildAtIndex(uint32_t N) const {
  if (N >= getChildCount())
    return nullptr;
  return std::make_unique<NativeLineNumber>(Lines[N]);
}

std::unique_ptr<IPDBLineNumber> NativeEnumLineNumbers::getNext() {
  return getChildAtIndex(Index++);
}

void NativeEnumLineNumbers::reset() { Index = 0; }
