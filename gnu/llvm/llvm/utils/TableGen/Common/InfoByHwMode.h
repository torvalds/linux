//===--- InfoByHwMode.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Classes that implement data parameterized by HW modes for instruction
// selection. Currently it is ValueTypeByHwMode (parameterized ValueType),
// and RegSizeInfoByHwMode (parameterized register/spill size and alignment
// data).
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_INFOBYHWMODE_H
#define LLVM_UTILS_TABLEGEN_INFOBYHWMODE_H

#include "CodeGenHwModes.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <utility>

namespace llvm {

class Record;
class raw_ostream;

template <typename InfoT> struct InfoByHwMode;

std::string getModeName(unsigned Mode);

enum : unsigned {
  DefaultMode = CodeGenHwModes::DefaultMode,
};

template <typename InfoT>
void union_modes(const InfoByHwMode<InfoT> &A, const InfoByHwMode<InfoT> &B,
                 SmallVectorImpl<unsigned> &Modes) {
  auto AI = A.begin();
  auto BI = B.begin();

  // Skip default mode, but remember if we had one.
  bool HasDefault = false;
  if (AI != A.end() && AI->first == DefaultMode) {
    HasDefault = true;
    ++AI;
  }
  if (BI != B.end() && BI->first == DefaultMode) {
    HasDefault = true;
    ++BI;
  }

  while (AI != A.end()) {
    // If we're done with B, finish A.
    if (BI == B.end()) {
      for (; AI != A.end(); ++AI)
        Modes.push_back(AI->first);
      break;
    }

    if (BI->first < AI->first) {
      Modes.push_back(BI->first);
      ++BI;
    } else {
      Modes.push_back(AI->first);
      if (AI->first == BI->first)
        ++BI;
      ++AI;
    }
  }

  // Finish B.
  for (; BI != B.end(); ++BI)
    Modes.push_back(BI->first);

  // Make sure that the default mode is last on the list.
  if (HasDefault)
    Modes.push_back(DefaultMode);
}

template <typename InfoT> struct InfoByHwMode {
  typedef std::map<unsigned, InfoT> MapType;
  typedef typename MapType::value_type PairType;
  typedef typename MapType::iterator iterator;
  typedef typename MapType::const_iterator const_iterator;

  InfoByHwMode() = default;
  InfoByHwMode(const MapType &M) : Map(M) {}

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  iterator begin() { return Map.begin(); }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  iterator end() { return Map.end(); }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  const_iterator begin() const { return Map.begin(); }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  const_iterator end() const { return Map.end(); }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool empty() const { return Map.empty(); }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool hasMode(unsigned M) const { return Map.find(M) != Map.end(); }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool hasDefault() const {
    return !Map.empty() && Map.begin()->first == DefaultMode;
  }

  InfoT &get(unsigned Mode) {
    auto F = Map.find(Mode);
    if (F != Map.end())
      return F->second;

    // Copy and insert the default mode which should be first.
    assert(hasDefault());
    auto P = Map.insert({Mode, Map.begin()->second});
    return P.first->second;
  }
  const InfoT &get(unsigned Mode) const {
    auto F = Map.find(Mode);
    if (F != Map.end())
      return F->second;
    // Get the default mode which should be first.
    F = Map.begin();
    assert(F != Map.end() && F->first == DefaultMode);
    return F->second;
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool isSimple() const {
    return Map.size() == 1 && Map.begin()->first == DefaultMode;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  const InfoT &getSimple() const {
    assert(isSimple());
    return Map.begin()->second;
  }
  void makeSimple(unsigned Mode) {
    assert(hasMode(Mode) || hasDefault());
    InfoT I = get(Mode);
    Map.clear();
    Map.insert(std::pair(DefaultMode, I));
  }

protected:
  MapType Map;
};

struct ValueTypeByHwMode : public InfoByHwMode<MVT> {
  ValueTypeByHwMode(Record *R, const CodeGenHwModes &CGH);
  ValueTypeByHwMode(Record *R, MVT T);
  ValueTypeByHwMode(MVT T) { Map.insert({DefaultMode, T}); }
  ValueTypeByHwMode() = default;

  bool operator==(const ValueTypeByHwMode &T) const;
  bool operator<(const ValueTypeByHwMode &T) const;

  bool isValid() const { return !Map.empty(); }
  MVT getType(unsigned Mode) const { return get(Mode); }
  MVT &getOrCreateTypeForMode(unsigned Mode, MVT Type);

  static StringRef getMVTName(MVT T);
  void writeToStream(raw_ostream &OS) const;
  void dump() const;

  unsigned PtrAddrSpace = std::numeric_limits<unsigned>::max();
  bool isPointer() const {
    return PtrAddrSpace != std::numeric_limits<unsigned>::max();
  }
};

ValueTypeByHwMode getValueTypeByHwMode(Record *Rec, const CodeGenHwModes &CGH);

raw_ostream &operator<<(raw_ostream &OS, const ValueTypeByHwMode &T);

struct RegSizeInfo {
  unsigned RegSize;
  unsigned SpillSize;
  unsigned SpillAlignment;

  RegSizeInfo(Record *R);
  RegSizeInfo() = default;
  bool operator<(const RegSizeInfo &I) const;
  bool operator==(const RegSizeInfo &I) const {
    return std::tie(RegSize, SpillSize, SpillAlignment) ==
           std::tie(I.RegSize, I.SpillSize, I.SpillAlignment);
  }
  bool operator!=(const RegSizeInfo &I) const { return !(*this == I); }

  bool isSubClassOf(const RegSizeInfo &I) const;
  void writeToStream(raw_ostream &OS) const;
};

struct RegSizeInfoByHwMode : public InfoByHwMode<RegSizeInfo> {
  RegSizeInfoByHwMode(Record *R, const CodeGenHwModes &CGH);
  RegSizeInfoByHwMode() = default;
  bool operator<(const RegSizeInfoByHwMode &VI) const;
  bool operator==(const RegSizeInfoByHwMode &VI) const;
  bool operator!=(const RegSizeInfoByHwMode &VI) const {
    return !(*this == VI);
  }

  bool isSubClassOf(const RegSizeInfoByHwMode &I) const;
  bool hasStricterSpillThan(const RegSizeInfoByHwMode &I) const;

  void writeToStream(raw_ostream &OS) const;

  void insertRegSizeForMode(unsigned Mode, RegSizeInfo Info) {
    Map.insert(std::pair(Mode, Info));
  }
};

raw_ostream &operator<<(raw_ostream &OS, const RegSizeInfo &T);
raw_ostream &operator<<(raw_ostream &OS, const RegSizeInfoByHwMode &T);

struct SubRegRange {
  uint16_t Size;
  uint16_t Offset;

  SubRegRange(Record *R);
  SubRegRange(uint16_t Size, uint16_t Offset) : Size(Size), Offset(Offset) {}
};

struct SubRegRangeByHwMode : public InfoByHwMode<SubRegRange> {
  SubRegRangeByHwMode(Record *R, const CodeGenHwModes &CGH);
  SubRegRangeByHwMode(SubRegRange Range) { Map.insert({DefaultMode, Range}); }
  SubRegRangeByHwMode() = default;

  void insertSubRegRangeForMode(unsigned Mode, SubRegRange Info) {
    Map.insert(std::pair(Mode, Info));
  }
};

struct EncodingInfoByHwMode : public InfoByHwMode<Record *> {
  EncodingInfoByHwMode(Record *R, const CodeGenHwModes &CGH);
  EncodingInfoByHwMode() = default;
};

} // namespace llvm

#endif // LLVM_UTILS_TABLEGEN_INFOBYHWMODE_H
