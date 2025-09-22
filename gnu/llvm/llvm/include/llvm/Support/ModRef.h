//===--- ModRef.h - Memory effect modelling ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definitions of ModRefInfo and MemoryEffects, which are used to
// describe the memory effects of instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MODREF_H
#define LLVM_SUPPORT_MODREF_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// Flags indicating whether a memory access modifies or references memory.
///
/// This is no access at all, a modification, a reference, or both
/// a modification and a reference.
enum class ModRefInfo : uint8_t {
  /// The access neither references nor modifies the value stored in memory.
  NoModRef = 0,
  /// The access may reference the value stored in memory.
  Ref = 1,
  /// The access may modify the value stored in memory.
  Mod = 2,
  /// The access may reference and may modify the value stored in memory.
  ModRef = Ref | Mod,
  LLVM_MARK_AS_BITMASK_ENUM(ModRef),
};

[[nodiscard]] inline bool isNoModRef(const ModRefInfo MRI) {
  return MRI == ModRefInfo::NoModRef;
}
[[nodiscard]] inline bool isModOrRefSet(const ModRefInfo MRI) {
  return MRI != ModRefInfo::NoModRef;
}
[[nodiscard]] inline bool isModAndRefSet(const ModRefInfo MRI) {
  return MRI == ModRefInfo::ModRef;
}
[[nodiscard]] inline bool isModSet(const ModRefInfo MRI) {
  return static_cast<int>(MRI) & static_cast<int>(ModRefInfo::Mod);
}
[[nodiscard]] inline bool isRefSet(const ModRefInfo MRI) {
  return static_cast<int>(MRI) & static_cast<int>(ModRefInfo::Ref);
}

/// Debug print ModRefInfo.
raw_ostream &operator<<(raw_ostream &OS, ModRefInfo MR);

/// The locations at which a function might access memory.
enum class IRMemLocation {
  /// Access to memory via argument pointers.
  ArgMem = 0,
  /// Memory that is inaccessible via LLVM IR.
  InaccessibleMem = 1,
  /// Any other memory.
  Other = 2,

  /// Helpers to iterate all locations in the MemoryEffectsBase class.
  First = ArgMem,
  Last = Other,
};

template <typename LocationEnum> class MemoryEffectsBase {
public:
  using Location = LocationEnum;

private:
  uint32_t Data = 0;

  static constexpr uint32_t BitsPerLoc = 2;
  static constexpr uint32_t LocMask = (1 << BitsPerLoc) - 1;

  static uint32_t getLocationPos(Location Loc) {
    return (uint32_t)Loc * BitsPerLoc;
  }

  MemoryEffectsBase(uint32_t Data) : Data(Data) {}

  void setModRef(Location Loc, ModRefInfo MR) {
    Data &= ~(LocMask << getLocationPos(Loc));
    Data |= static_cast<uint32_t>(MR) << getLocationPos(Loc);
  }

public:
  /// Returns iterator over all supported location kinds.
  static auto locations() {
    return enum_seq_inclusive(Location::First, Location::Last,
                              force_iteration_on_noniterable_enum);
  }

  /// Create MemoryEffectsBase that can access only the given location with the
  /// given ModRefInfo.
  MemoryEffectsBase(Location Loc, ModRefInfo MR) { setModRef(Loc, MR); }

  /// Create MemoryEffectsBase that can access any location with the given
  /// ModRefInfo.
  explicit MemoryEffectsBase(ModRefInfo MR) {
    for (Location Loc : locations())
      setModRef(Loc, MR);
  }

  /// Create MemoryEffectsBase that can read and write any memory.
  static MemoryEffectsBase unknown() {
    return MemoryEffectsBase(ModRefInfo::ModRef);
  }

  /// Create MemoryEffectsBase that cannot read or write any memory.
  static MemoryEffectsBase none() {
    return MemoryEffectsBase(ModRefInfo::NoModRef);
  }

  /// Create MemoryEffectsBase that can read any memory.
  static MemoryEffectsBase readOnly() {
    return MemoryEffectsBase(ModRefInfo::Ref);
  }

  /// Create MemoryEffectsBase that can write any memory.
  static MemoryEffectsBase writeOnly() {
    return MemoryEffectsBase(ModRefInfo::Mod);
  }

  /// Create MemoryEffectsBase that can only access argument memory.
  static MemoryEffectsBase argMemOnly(ModRefInfo MR = ModRefInfo::ModRef) {
    return MemoryEffectsBase(Location::ArgMem, MR);
  }

  /// Create MemoryEffectsBase that can only access inaccessible memory.
  static MemoryEffectsBase
  inaccessibleMemOnly(ModRefInfo MR = ModRefInfo::ModRef) {
    return MemoryEffectsBase(Location::InaccessibleMem, MR);
  }

  /// Create MemoryEffectsBase that can only access inaccessible or argument
  /// memory.
  static MemoryEffectsBase
  inaccessibleOrArgMemOnly(ModRefInfo MR = ModRefInfo::ModRef) {
    MemoryEffectsBase FRMB = none();
    FRMB.setModRef(Location::ArgMem, MR);
    FRMB.setModRef(Location::InaccessibleMem, MR);
    return FRMB;
  }

  /// Create MemoryEffectsBase from an encoded integer value (used by memory
  /// attribute).
  static MemoryEffectsBase createFromIntValue(uint32_t Data) {
    return MemoryEffectsBase(Data);
  }

  /// Convert MemoryEffectsBase into an encoded integer value (used by memory
  /// attribute).
  uint32_t toIntValue() const {
    return Data;
  }

  /// Get ModRefInfo for the given Location.
  ModRefInfo getModRef(Location Loc) const {
    return ModRefInfo((Data >> getLocationPos(Loc)) & LocMask);
  }

  /// Get new MemoryEffectsBase with modified ModRefInfo for Loc.
  MemoryEffectsBase getWithModRef(Location Loc, ModRefInfo MR) const {
    MemoryEffectsBase ME = *this;
    ME.setModRef(Loc, MR);
    return ME;
  }

  /// Get new MemoryEffectsBase with NoModRef on the given Loc.
  MemoryEffectsBase getWithoutLoc(Location Loc) const {
    MemoryEffectsBase ME = *this;
    ME.setModRef(Loc, ModRefInfo::NoModRef);
    return ME;
  }

  /// Get ModRefInfo for any location.
  ModRefInfo getModRef() const {
    ModRefInfo MR = ModRefInfo::NoModRef;
    for (Location Loc : locations())
      MR |= getModRef(Loc);
    return MR;
  }

  /// Whether this function accesses no memory.
  bool doesNotAccessMemory() const { return Data == 0; }

  /// Whether this function only (at most) reads memory.
  bool onlyReadsMemory() const { return !isModSet(getModRef()); }

  /// Whether this function only (at most) writes memory.
  bool onlyWritesMemory() const { return !isRefSet(getModRef()); }

  /// Whether this function only (at most) accesses argument memory.
  bool onlyAccessesArgPointees() const {
    return getWithoutLoc(Location::ArgMem).doesNotAccessMemory();
  }

  /// Whether this function may access argument memory.
  bool doesAccessArgPointees() const {
    return isModOrRefSet(getModRef(Location::ArgMem));
  }

  /// Whether this function only (at most) accesses inaccessible memory.
  bool onlyAccessesInaccessibleMem() const {
    return getWithoutLoc(Location::InaccessibleMem).doesNotAccessMemory();
  }

  /// Whether this function only (at most) accesses argument and inaccessible
  /// memory.
  bool onlyAccessesInaccessibleOrArgMem() const {
    return getWithoutLoc(Location::InaccessibleMem)
        .getWithoutLoc(Location::ArgMem)
        .doesNotAccessMemory();
  }

  /// Intersect with other MemoryEffectsBase.
  MemoryEffectsBase operator&(MemoryEffectsBase Other) const {
    return MemoryEffectsBase(Data & Other.Data);
  }

  /// Intersect (in-place) with other MemoryEffectsBase.
  MemoryEffectsBase &operator&=(MemoryEffectsBase Other) {
    Data &= Other.Data;
    return *this;
  }

  /// Union with other MemoryEffectsBase.
  MemoryEffectsBase operator|(MemoryEffectsBase Other) const {
    return MemoryEffectsBase(Data | Other.Data);
  }

  /// Union (in-place) with other MemoryEffectsBase.
  MemoryEffectsBase &operator|=(MemoryEffectsBase Other) {
    Data |= Other.Data;
    return *this;
  }

  /// Subtract other MemoryEffectsBase.
  MemoryEffectsBase operator-(MemoryEffectsBase Other) const {
    return MemoryEffectsBase(Data & ~Other.Data);
  }

  /// Subtract (in-place) with other MemoryEffectsBase.
  MemoryEffectsBase &operator-=(MemoryEffectsBase Other) {
    Data &= ~Other.Data;
    return *this;
  }

  /// Check whether this is the same as other MemoryEffectsBase.
  bool operator==(MemoryEffectsBase Other) const { return Data == Other.Data; }

  /// Check whether this is different from other MemoryEffectsBase.
  bool operator!=(MemoryEffectsBase Other) const { return !operator==(Other); }
};

/// Summary of how a function affects memory in the program.
///
/// Loads from constant globals are not considered memory accesses for this
/// interface. Also, functions may freely modify stack space local to their
/// invocation without having to report it through these interfaces.
using MemoryEffects = MemoryEffectsBase<IRMemLocation>;

/// Debug print MemoryEffects.
raw_ostream &operator<<(raw_ostream &OS, MemoryEffects RMRB);

// Legacy alias.
using FunctionModRefBehavior = MemoryEffects;

} // namespace llvm

#endif
