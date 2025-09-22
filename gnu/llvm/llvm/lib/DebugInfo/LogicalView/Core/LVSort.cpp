//===-- LVSort.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Support for LVObject sorting.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVSort.h"
#include "llvm/DebugInfo/LogicalView/Core/LVElement.h"
#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include <string>

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Sort"

//===----------------------------------------------------------------------===//
// Callback functions to sort objects.
//===----------------------------------------------------------------------===//
// Callback comparator based on kind.
LVSortValue llvm::logicalview::compareKind(const LVObject *LHS,
                                           const LVObject *RHS) {
  return std::string(LHS->kind()) < std::string(RHS->kind());
}

// Callback comparator based on line.
LVSortValue llvm::logicalview::compareLine(const LVObject *LHS,
                                           const LVObject *RHS) {
  return LHS->getLineNumber() < RHS->getLineNumber();
}

// Callback comparator based on name.
LVSortValue llvm::logicalview::compareName(const LVObject *LHS,
                                           const LVObject *RHS) {
  return LHS->getName() < RHS->getName();
}

// Callback comparator based on DIE offset.
LVSortValue llvm::logicalview::compareOffset(const LVObject *LHS,
                                             const LVObject *RHS) {
  return LHS->getOffset() < RHS->getOffset();
}

// Callback comparator for Range compare.
LVSortValue llvm::logicalview::compareRange(const LVObject *LHS,
                                            const LVObject *RHS) {
  if (LHS->getLowerAddress() < RHS->getLowerAddress())
    return true;

  // If the lower address is the same, use the upper address value in
  // order to put first the smallest interval.
  if (LHS->getLowerAddress() == RHS->getLowerAddress())
    return LHS->getUpperAddress() < RHS->getUpperAddress();

  return false;
}

// Callback comparator based on multiple keys (First: Kind).
LVSortValue llvm::logicalview::sortByKind(const LVObject *LHS,
                                          const LVObject *RHS) {
  // Order in which the object attributes are used for comparison:
  // kind, name, line number, offset.
  std::tuple<std::string, StringRef, uint32_t, LVOffset> Left(
      LHS->kind(), LHS->getName(), LHS->getLineNumber(), LHS->getOffset());
  std::tuple<std::string, StringRef, uint32_t, LVOffset> Right(
      RHS->kind(), RHS->getName(), RHS->getLineNumber(), RHS->getOffset());
  return Left < Right;
}

// Callback comparator based on multiple keys (First: Line).
LVSortValue llvm::logicalview::sortByLine(const LVObject *LHS,
                                          const LVObject *RHS) {
  // Order in which the object attributes are used for comparison:
  // line number, name, kind, offset.
  std::tuple<uint32_t, StringRef, std::string, LVOffset> Left(
      LHS->getLineNumber(), LHS->getName(), LHS->kind(), LHS->getOffset());
  std::tuple<uint32_t, StringRef, std::string, LVOffset> Right(
      RHS->getLineNumber(), RHS->getName(), RHS->kind(), RHS->getOffset());
  return Left < Right;
}

// Callback comparator based on multiple keys (First: Name).
LVSortValue llvm::logicalview::sortByName(const LVObject *LHS,
                                          const LVObject *RHS) {
  // Order in which the object attributes are used for comparison:
  // name, line number, kind, offset.
  std::tuple<StringRef, uint32_t, std::string, LVOffset> Left(
      LHS->getName(), LHS->getLineNumber(), LHS->kind(), LHS->getOffset());
  std::tuple<StringRef, uint32_t, std::string, LVOffset> Right(
      RHS->getName(), RHS->getLineNumber(), RHS->kind(), RHS->getOffset());
  return Left < Right;
}

LVSortFunction llvm::logicalview::getSortFunction() {
  using LVSortInfo = std::map<LVSortMode, LVSortFunction>;
  static LVSortInfo SortInfo = {
      {LVSortMode::None, nullptr},         {LVSortMode::Kind, sortByKind},
      {LVSortMode::Line, sortByLine},      {LVSortMode::Name, sortByName},
      {LVSortMode::Offset, compareOffset},
  };

  LVSortFunction SortFunction = nullptr;
  LVSortInfo::iterator Iter = SortInfo.find(options().getSortMode());
  if (Iter != SortInfo.end())
    SortFunction = Iter->second;
  return SortFunction;
}
