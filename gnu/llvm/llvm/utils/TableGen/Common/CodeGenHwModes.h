//===--- CodeGenHwModes.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Classes to parse and store HW mode information for instruction selection.
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_CODEGENHWMODES_H
#define LLVM_UTILS_TABLEGEN_CODEGENHWMODES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <map>
#include <string>
#include <utility>
#include <vector>

// HwModeId -> list of predicates (definition)

namespace llvm {
class Record;
class RecordKeeper;

struct CodeGenHwModes;

struct HwMode {
  HwMode(Record *R);
  StringRef Name;
  std::string Features;
  std::string Predicates;
  void dump() const;
};

struct HwModeSelect {
  HwModeSelect(Record *R, CodeGenHwModes &CGH);
  typedef std::pair<unsigned, Record *> PairType;
  std::vector<PairType> Items;
  void dump() const;
};

struct CodeGenHwModes {
  enum : unsigned { DefaultMode = 0 };
  static StringRef DefaultModeName;

  CodeGenHwModes(RecordKeeper &R);
  unsigned getHwModeId(Record *R) const;
  const HwMode &getMode(unsigned Id) const {
    assert(Id != 0 && "Mode id of 0 is reserved for the default mode");
    return Modes[Id - 1];
  }
  StringRef getModeName(unsigned Id, bool IncludeDefault = false) const {
    if (IncludeDefault && Id == CodeGenHwModes::DefaultMode)
      return DefaultModeName;
    return getMode(Id).Name;
  }
  const HwModeSelect &getHwModeSelect(Record *R) const;
  const std::map<Record *, HwModeSelect> &getHwModeSelects() const {
    return ModeSelects;
  }
  unsigned getNumModeIds() const { return Modes.size() + 1; }
  void dump() const;

private:
  RecordKeeper &Records;
  DenseMap<Record *, unsigned> ModeIds; // HwMode Record -> HwModeId
  std::vector<HwMode> Modes;
  std::map<Record *, HwModeSelect> ModeSelects;
};
} // namespace llvm

#endif // LLVM_UTILS_TABLEGEN_CODEGENHWMODES_H
