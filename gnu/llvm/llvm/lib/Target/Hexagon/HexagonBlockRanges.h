//===- HexagonBlockRanges.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONBLOCKRANGES_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONBLOCKRANGES_H

#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/Register.h"
#include <cassert>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace llvm {

class HexagonSubtarget;
class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MachineRegisterInfo;
class raw_ostream;
class TargetInstrInfo;
class TargetRegisterInfo;

struct HexagonBlockRanges {
  HexagonBlockRanges(MachineFunction &MF);

  // FIXME: Consolidate duplicate definitions of RegisterRef
  struct RegisterRef {
    llvm::Register Reg;
    unsigned Sub;

    bool operator<(RegisterRef R) const {
      return Reg < R.Reg || (Reg == R.Reg && Sub < R.Sub);
    }
  };
  using RegisterSet = std::set<RegisterRef>;

  // This is to represent an "index", which is an abstraction of a position
  // of an instruction within a basic block.
  class IndexType {
  public:
    enum : unsigned {
      None  = 0,
      Entry = 1,
      Exit  = 2,
      First = 11  // 10th + 1st
    };

    IndexType() {}
    IndexType(unsigned Idx) : Index(Idx) {}

    static bool isInstr(IndexType X) { return X.Index >= First; }

    operator unsigned() const;
    bool operator== (unsigned x) const;
    bool operator== (IndexType Idx) const;
    bool operator!= (unsigned x) const;
    bool operator!= (IndexType Idx) const;
    IndexType operator++ ();
    bool operator< (unsigned Idx) const;
    bool operator< (IndexType Idx) const;
    bool operator<= (IndexType Idx) const;

  private:
    bool operator>  (IndexType Idx) const;
    bool operator>= (IndexType Idx) const;

    unsigned Index = None;
  };

  // A range of indices, essentially a representation of a live range.
  // This is also used to represent "dead ranges", i.e. ranges where a
  // register is dead.
  class IndexRange : public std::pair<IndexType,IndexType> {
  public:
    IndexRange() = default;
    IndexRange(IndexType Start, IndexType End, bool F = false, bool T = false)
      : std::pair<IndexType,IndexType>(Start, End), Fixed(F), TiedEnd(T) {}

    IndexType start() const { return first; }
    IndexType end() const   { return second; }

    bool operator< (const IndexRange &A) const {
      return start() < A.start();
    }

    bool overlaps(const IndexRange &A) const;
    bool contains(const IndexRange &A) const;
    void merge(const IndexRange &A);

    bool Fixed = false;   // Can be renamed?  "Fixed" means "no".
    bool TiedEnd = false; // The end is not a use, but a dead def tied to a use.

  private:
    void setStart(const IndexType &S) { first = S; }
    void setEnd(const IndexType &E)   { second = E; }
  };

  // A list of index ranges. This represents liveness of a register
  // in a basic block.
  class RangeList : public std::vector<IndexRange> {
  public:
    void add(IndexType Start, IndexType End, bool Fixed, bool TiedEnd) {
      push_back(IndexRange(Start, End, Fixed, TiedEnd));
    }
    void add(const IndexRange &Range) {
      push_back(Range);
    }

    void include(const RangeList &RL);
    void unionize(bool MergeAdjacent = false);
    void subtract(const IndexRange &Range);

  private:
    void addsub(const IndexRange &A, const IndexRange &B);
  };

  class InstrIndexMap {
  public:
    InstrIndexMap(MachineBasicBlock &B);

    MachineInstr *getInstr(IndexType Idx) const;
    IndexType getIndex(MachineInstr *MI) const;
    MachineBasicBlock &getBlock() const { return Block; }
    IndexType getPrevIndex(IndexType Idx) const;
    IndexType getNextIndex(IndexType Idx) const;
    void replaceInstr(MachineInstr *OldMI, MachineInstr *NewMI);

    friend raw_ostream &operator<< (raw_ostream &OS, const InstrIndexMap &Map);

    IndexType First, Last;

  private:
    MachineBasicBlock &Block;
    std::map<IndexType,MachineInstr*> Map;
  };

  using RegToRangeMap = std::map<RegisterRef, RangeList>;

  RegToRangeMap computeLiveMap(InstrIndexMap &IndexMap);
  RegToRangeMap computeDeadMap(InstrIndexMap &IndexMap, RegToRangeMap &LiveMap);
  static RegisterSet expandToSubRegs(RegisterRef R,
      const MachineRegisterInfo &MRI, const TargetRegisterInfo &TRI);

  struct PrintRangeMap {
    PrintRangeMap(const RegToRangeMap &M, const TargetRegisterInfo &I)
        : Map(M), TRI(I) {}

    friend raw_ostream &operator<< (raw_ostream &OS, const PrintRangeMap &P);

  private:
    const RegToRangeMap &Map;
    const TargetRegisterInfo &TRI;
  };

private:
  RegisterSet getLiveIns(const MachineBasicBlock &B,
      const MachineRegisterInfo &MRI, const TargetRegisterInfo &TRI);

  void computeInitialLiveRanges(InstrIndexMap &IndexMap,
      RegToRangeMap &LiveMap);

  MachineFunction &MF;
  const HexagonSubtarget &HST;
  const TargetInstrInfo &TII;
  const TargetRegisterInfo &TRI;
  BitVector Reserved;
};

inline HexagonBlockRanges::IndexType::operator unsigned() const {
  assert(Index >= First);
  return Index;
}

inline bool HexagonBlockRanges::IndexType::operator== (unsigned x) const {
  return Index == x;
}

inline bool HexagonBlockRanges::IndexType::operator== (IndexType Idx) const {
  return Index == Idx.Index;
}

inline bool HexagonBlockRanges::IndexType::operator!= (unsigned x) const {
  return Index != x;
}

inline bool HexagonBlockRanges::IndexType::operator!= (IndexType Idx) const {
  return Index != Idx.Index;
}

inline
HexagonBlockRanges::IndexType HexagonBlockRanges::IndexType::operator++ () {
  assert(Index != None);
  assert(Index != Exit);
  if (Index == Entry)
    Index = First;
  else
    ++Index;
  return *this;
}

inline bool HexagonBlockRanges::IndexType::operator< (unsigned Idx) const {
  return operator< (IndexType(Idx));
}

inline bool HexagonBlockRanges::IndexType::operator< (IndexType Idx) const {
  // !(x < x).
  if (Index == Idx.Index)
    return false;
  // !(None < x) for all x.
  // !(x < None) for all x.
  if (Index == None || Idx.Index == None)
    return false;
  // !(Exit < x) for all x.
  // !(x < Entry) for all x.
  if (Index == Exit || Idx.Index == Entry)
    return false;
  // Entry < x for all x != Entry.
  // x < Exit for all x != Exit.
  if (Index == Entry || Idx.Index == Exit)
    return true;

  return Index < Idx.Index;
}

inline bool HexagonBlockRanges::IndexType::operator<= (IndexType Idx) const {
  return operator==(Idx) || operator<(Idx);
}

raw_ostream &operator<< (raw_ostream &OS, HexagonBlockRanges::IndexType Idx);
raw_ostream &operator<< (raw_ostream &OS,
      const HexagonBlockRanges::IndexRange &IR);
raw_ostream &operator<< (raw_ostream &OS,
      const HexagonBlockRanges::RangeList &RL);
raw_ostream &operator<< (raw_ostream &OS,
      const HexagonBlockRanges::InstrIndexMap &M);
raw_ostream &operator<< (raw_ostream &OS,
      const HexagonBlockRanges::PrintRangeMap &P);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONBLOCKRANGES_H
