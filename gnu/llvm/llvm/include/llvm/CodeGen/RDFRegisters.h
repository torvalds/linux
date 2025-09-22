//===- RDFRegisters.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_RDFREGISTERS_H
#define LLVM_CODEGEN_RDFREGISTERS_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegister.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace llvm {

class MachineFunction;
class raw_ostream;

namespace rdf {
struct RegisterAggr;

using RegisterId = uint32_t;

template <typename T>
bool disjoint(const std::set<T> &A, const std::set<T> &B) {
  auto ItA = A.begin(), EndA = A.end();
  auto ItB = B.begin(), EndB = B.end();
  while (ItA != EndA && ItB != EndB) {
    if (*ItA < *ItB)
      ++ItA;
    else if (*ItB < *ItA)
      ++ItB;
    else
      return false;
  }
  return true;
}

// Template class for a map translating uint32_t into arbitrary types.
// The map will act like an indexed set: upon insertion of a new object,
// it will automatically assign a new index to it. Index of 0 is treated
// as invalid and is never allocated.
template <typename T, unsigned N = 32> struct IndexedSet {
  IndexedSet() { Map.reserve(N); }

  T get(uint32_t Idx) const {
    // Index Idx corresponds to Map[Idx-1].
    assert(Idx != 0 && !Map.empty() && Idx - 1 < Map.size());
    return Map[Idx - 1];
  }

  uint32_t insert(T Val) {
    // Linear search.
    auto F = llvm::find(Map, Val);
    if (F != Map.end())
      return F - Map.begin() + 1;
    Map.push_back(Val);
    return Map.size(); // Return actual_index + 1.
  }

  uint32_t find(T Val) const {
    auto F = llvm::find(Map, Val);
    assert(F != Map.end());
    return F - Map.begin() + 1;
  }

  uint32_t size() const { return Map.size(); }

  using const_iterator = typename std::vector<T>::const_iterator;

  const_iterator begin() const { return Map.begin(); }
  const_iterator end() const { return Map.end(); }

private:
  std::vector<T> Map;
};

struct RegisterRef {
  RegisterId Reg = 0;
  LaneBitmask Mask = LaneBitmask::getNone(); // Only for registers.

  constexpr RegisterRef() = default;
  constexpr explicit RegisterRef(RegisterId R,
                                 LaneBitmask M = LaneBitmask::getAll())
      : Reg(R), Mask(isRegId(R) && R != 0 ? M : LaneBitmask::getNone()) {}

  // Classify null register as a "register".
  constexpr bool isReg() const { return Reg == 0 || isRegId(Reg); }
  constexpr bool isUnit() const { return isUnitId(Reg); }
  constexpr bool isMask() const { return isMaskId(Reg); }

  constexpr unsigned idx() const { return toIdx(Reg); }

  constexpr operator bool() const {
    return !isReg() || (Reg != 0 && Mask.any());
  }

  size_t hash() const {
    return std::hash<RegisterId>{}(Reg) ^
           std::hash<LaneBitmask::Type>{}(Mask.getAsInteger());
  }

  static constexpr bool isRegId(unsigned Id) {
    return Register::isPhysicalRegister(Id);
  }
  static constexpr bool isUnitId(unsigned Id) {
    return Register::isVirtualRegister(Id);
  }
  static constexpr bool isMaskId(unsigned Id) {
    return Register::isStackSlot(Id);
  }

  static constexpr RegisterId toUnitId(unsigned Idx) {
    return Idx | MCRegister::VirtualRegFlag;
  }

  static constexpr unsigned toIdx(RegisterId Id) {
    // Not using virtReg2Index or stackSlot2Index, because they are
    // not constexpr.
    if (isUnitId(Id))
      return Id & ~MCRegister::VirtualRegFlag;
    // RegId and MaskId are unchanged.
    return Id;
  }

  bool operator<(RegisterRef) const = delete;
  bool operator==(RegisterRef) const = delete;
  bool operator!=(RegisterRef) const = delete;
};

struct PhysicalRegisterInfo {
  PhysicalRegisterInfo(const TargetRegisterInfo &tri,
                       const MachineFunction &mf);

  RegisterId getRegMaskId(const uint32_t *RM) const {
    return Register::index2StackSlot(RegMasks.find(RM));
  }

  const uint32_t *getRegMaskBits(RegisterId R) const {
    return RegMasks.get(Register::stackSlot2Index(R));
  }

  bool alias(RegisterRef RA, RegisterRef RB) const;

  // Returns the set of aliased physical registers.
  std::set<RegisterId> getAliasSet(RegisterId Reg) const;

  RegisterRef getRefForUnit(uint32_t U) const {
    return RegisterRef(UnitInfos[U].Reg, UnitInfos[U].Mask);
  }

  const BitVector &getMaskUnits(RegisterId MaskId) const {
    return MaskInfos[Register::stackSlot2Index(MaskId)].Units;
  }

  std::set<RegisterId> getUnits(RegisterRef RR) const;

  const BitVector &getUnitAliases(uint32_t U) const {
    return AliasInfos[U].Regs;
  }

  RegisterRef mapTo(RegisterRef RR, unsigned R) const;
  const TargetRegisterInfo &getTRI() const { return TRI; }

  bool equal_to(RegisterRef A, RegisterRef B) const;
  bool less(RegisterRef A, RegisterRef B) const;

  void print(raw_ostream &OS, RegisterRef A) const;
  void print(raw_ostream &OS, const RegisterAggr &A) const;

private:
  struct RegInfo {
    const TargetRegisterClass *RegClass = nullptr;
  };
  struct UnitInfo {
    RegisterId Reg = 0;
    LaneBitmask Mask;
  };
  struct MaskInfo {
    BitVector Units;
  };
  struct AliasInfo {
    BitVector Regs;
  };

  const TargetRegisterInfo &TRI;
  IndexedSet<const uint32_t *> RegMasks;
  std::vector<RegInfo> RegInfos;
  std::vector<UnitInfo> UnitInfos;
  std::vector<MaskInfo> MaskInfos;
  std::vector<AliasInfo> AliasInfos;
};

struct RegisterAggr {
  RegisterAggr(const PhysicalRegisterInfo &pri)
      : Units(pri.getTRI().getNumRegUnits()), PRI(pri) {}
  RegisterAggr(const RegisterAggr &RG) = default;

  unsigned size() const { return Units.count(); }
  bool empty() const { return Units.none(); }
  bool hasAliasOf(RegisterRef RR) const;
  bool hasCoverOf(RegisterRef RR) const;

  const PhysicalRegisterInfo &getPRI() const { return PRI; }

  bool operator==(const RegisterAggr &A) const {
    return DenseMapInfo<BitVector>::isEqual(Units, A.Units);
  }

  static bool isCoverOf(RegisterRef RA, RegisterRef RB,
                        const PhysicalRegisterInfo &PRI) {
    return RegisterAggr(PRI).insert(RA).hasCoverOf(RB);
  }

  RegisterAggr &insert(RegisterRef RR);
  RegisterAggr &insert(const RegisterAggr &RG);
  RegisterAggr &intersect(RegisterRef RR);
  RegisterAggr &intersect(const RegisterAggr &RG);
  RegisterAggr &clear(RegisterRef RR);
  RegisterAggr &clear(const RegisterAggr &RG);

  RegisterRef intersectWith(RegisterRef RR) const;
  RegisterRef clearIn(RegisterRef RR) const;
  RegisterRef makeRegRef() const;

  size_t hash() const { return DenseMapInfo<BitVector>::getHashValue(Units); }

  struct ref_iterator {
    using MapType = std::map<RegisterId, LaneBitmask>;

  private:
    MapType Masks;
    MapType::iterator Pos;
    unsigned Index;
    const RegisterAggr *Owner;

  public:
    ref_iterator(const RegisterAggr &RG, bool End);

    RegisterRef operator*() const {
      return RegisterRef(Pos->first, Pos->second);
    }

    ref_iterator &operator++() {
      ++Pos;
      ++Index;
      return *this;
    }

    bool operator==(const ref_iterator &I) const {
      assert(Owner == I.Owner);
      (void)Owner;
      return Index == I.Index;
    }

    bool operator!=(const ref_iterator &I) const { return !(*this == I); }
  };

  ref_iterator ref_begin() const { return ref_iterator(*this, false); }
  ref_iterator ref_end() const { return ref_iterator(*this, true); }

  using unit_iterator = typename BitVector::const_set_bits_iterator;
  unit_iterator unit_begin() const { return Units.set_bits_begin(); }
  unit_iterator unit_end() const { return Units.set_bits_end(); }

  iterator_range<ref_iterator> refs() const {
    return make_range(ref_begin(), ref_end());
  }
  iterator_range<unit_iterator> units() const {
    return make_range(unit_begin(), unit_end());
  }

private:
  BitVector Units;
  const PhysicalRegisterInfo &PRI;
};

// This is really a std::map, except that it provides a non-trivial
// default constructor to the element accessed via [].
template <typename KeyType> struct RegisterAggrMap {
  RegisterAggrMap(const PhysicalRegisterInfo &pri) : Empty(pri) {}

  RegisterAggr &operator[](KeyType Key) {
    return Map.emplace(Key, Empty).first->second;
  }

  auto begin() { return Map.begin(); }
  auto end() { return Map.end(); }
  auto begin() const { return Map.begin(); }
  auto end() const { return Map.end(); }
  auto find(const KeyType &Key) const { return Map.find(Key); }

private:
  RegisterAggr Empty;
  std::map<KeyType, RegisterAggr> Map;

public:
  using key_type = typename decltype(Map)::key_type;
  using mapped_type = typename decltype(Map)::mapped_type;
  using value_type = typename decltype(Map)::value_type;
};

raw_ostream &operator<<(raw_ostream &OS, const RegisterAggr &A);

// Print the lane mask in a short form (or not at all if all bits are set).
struct PrintLaneMaskShort {
  PrintLaneMaskShort(LaneBitmask M) : Mask(M) {}
  LaneBitmask Mask;
};
raw_ostream &operator<<(raw_ostream &OS, const PrintLaneMaskShort &P);

} // end namespace rdf
} // end namespace llvm

namespace std {

template <> struct hash<llvm::rdf::RegisterRef> {
  size_t operator()(llvm::rdf::RegisterRef A) const { //
    return A.hash();
  }
};

template <> struct hash<llvm::rdf::RegisterAggr> {
  size_t operator()(const llvm::rdf::RegisterAggr &A) const { //
    return A.hash();
  }
};

template <> struct equal_to<llvm::rdf::RegisterRef> {
  constexpr equal_to(const llvm::rdf::PhysicalRegisterInfo &pri) : PRI(&pri) {}

  bool operator()(llvm::rdf::RegisterRef A, llvm::rdf::RegisterRef B) const {
    return PRI->equal_to(A, B);
  }

private:
  // Make it a pointer just in case. See comment in `less` below.
  const llvm::rdf::PhysicalRegisterInfo *PRI;
};

template <> struct equal_to<llvm::rdf::RegisterAggr> {
  bool operator()(const llvm::rdf::RegisterAggr &A,
                  const llvm::rdf::RegisterAggr &B) const {
    return A == B;
  }
};

template <> struct less<llvm::rdf::RegisterRef> {
  constexpr less(const llvm::rdf::PhysicalRegisterInfo &pri) : PRI(&pri) {}

  bool operator()(llvm::rdf::RegisterRef A, llvm::rdf::RegisterRef B) const {
    return PRI->less(A, B);
  }

private:
  // Make it a pointer because apparently some versions of MSVC use std::swap
  // on the std::less specialization.
  const llvm::rdf::PhysicalRegisterInfo *PRI;
};

} // namespace std

namespace llvm::rdf {
using RegisterSet = std::set<RegisterRef, std::less<RegisterRef>>;
} // namespace llvm::rdf

#endif // LLVM_CODEGEN_RDFREGISTERS_H
