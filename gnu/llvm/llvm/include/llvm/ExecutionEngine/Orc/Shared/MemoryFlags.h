//===-------- MemoryFlags.h - Memory allocation flags -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines types and operations related to memory protection and allocation
// lifetimes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_MEMORYFLAGS_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_MEMORYFLAGS_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace orc {

/// Describes Read/Write/Exec permissions for memory.
enum class MemProt {
  None = 0,
  Read = 1U << 0,
  Write = 1U << 1,
  Exec = 1U << 2,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ Exec)
};

/// Print a MemProt as an RWX triple.
inline raw_ostream &operator<<(raw_ostream &OS, MemProt MP) {
  return OS << (((MP & MemProt::Read) != MemProt::None) ? 'R' : '-')
            << (((MP & MemProt::Write) != MemProt::None) ? 'W' : '-')
            << (((MP & MemProt::Exec) != MemProt::None) ? 'X' : '-');
}

/// Convert a MemProt value to a corresponding sys::Memory::ProtectionFlags
/// value.
inline sys::Memory::ProtectionFlags toSysMemoryProtectionFlags(MemProt MP) {
  std::underlying_type_t<sys::Memory::ProtectionFlags> PF = 0;
  if ((MP & MemProt::Read) != MemProt::None)
    PF |= sys::Memory::MF_READ;
  if ((MP & MemProt::Write) != MemProt::None)
    PF |= sys::Memory::MF_WRITE;
  if ((MP & MemProt::Exec) != MemProt::None)
    PF |= sys::Memory::MF_EXEC;
  return static_cast<sys::Memory::ProtectionFlags>(PF);
}

/// Convert a sys::Memory::ProtectionFlags value to a corresponding MemProt
/// value.
inline MemProt fromSysMemoryProtectionFlags(sys::Memory::ProtectionFlags PF) {
  MemProt MP = MemProt::None;
  if (PF & sys::Memory::MF_READ)
    MP |= MemProt::Read;
  if (PF & sys::Memory::MF_WRITE)
    MP |= MemProt::Write;
  if (PF & sys::Memory::MF_EXEC)
    MP |= MemProt::None;
  return MP;
}

/// Describes a memory lifetime policy for memory to be allocated by a
/// JITLinkMemoryManager.
///
/// All memory allocated by a call to JITLinkMemoryManager::allocate should be
/// deallocated if a call is made to
/// JITLinkMemoryManager::InFlightAllocation::abandon. The policies below apply
/// to finalized allocations.
enum class MemLifetime {
  /// Standard memory should be allocated by the allocator and then deallocated
  /// when the deallocate method is called for the finalized allocation.
  Standard,

  /// Finalize memory should be allocated by the allocator, and then be
  /// overwritten and deallocated after all finalization functions have been
  /// run.
  Finalize,

  /// NoAlloc memory should not be allocated by the JITLinkMemoryManager at
  /// all. It is used for sections that don't need to be transferred to the
  /// executor process, typically metadata sections.
  NoAlloc
};

/// Print a MemDeallocPolicy.
inline raw_ostream &operator<<(raw_ostream &OS, MemLifetime MLP) {
  switch (MLP) {
  case MemLifetime::Standard:
    OS << "standard";
    break;
  case MemLifetime::Finalize:
    OS << "finalize";
    break;
  case MemLifetime::NoAlloc:
    OS << "noalloc";
    break;
  }
  return OS;
}

/// A pair of memory protections and allocation policies.
///
/// Optimized for use as a small map key.
class AllocGroup {
  friend struct llvm::DenseMapInfo<AllocGroup>;

  using underlying_type = uint8_t;
  static constexpr unsigned BitsForProt = 3;
  static constexpr unsigned BitsForLifetimePolicy = 2;
  static constexpr unsigned MaxIdentifiers =
      1U << (BitsForProt + BitsForLifetimePolicy);

public:
  static constexpr unsigned NumGroups = MaxIdentifiers;

  /// Create a default AllocGroup. No memory protections, standard
  /// lifetime policy.
  AllocGroup() = default;

  /// Create an AllocGroup from a MemProt only -- uses
  /// MemLifetime::Standard.
  AllocGroup(MemProt MP) : Id(static_cast<underlying_type>(MP)) {}

  /// Create an AllocGroup from a MemProt and a MemLifetime.
  AllocGroup(MemProt MP, MemLifetime MLP)
      : Id(static_cast<underlying_type>(MP) |
           (static_cast<underlying_type>(MLP) << BitsForProt)) {}

  /// Returns the MemProt for this group.
  MemProt getMemProt() const {
    return static_cast<MemProt>(Id & ((1U << BitsForProt) - 1));
  }

  /// Returns the MemLifetime for this group.
  MemLifetime getMemLifetime() const {
    return static_cast<MemLifetime>(Id >> BitsForProt);
  }

  friend bool operator==(const AllocGroup &LHS, const AllocGroup &RHS) {
    return LHS.Id == RHS.Id;
  }

  friend bool operator!=(const AllocGroup &LHS, const AllocGroup &RHS) {
    return !(LHS == RHS);
  }

  friend bool operator<(const AllocGroup &LHS, const AllocGroup &RHS) {
    return LHS.Id < RHS.Id;
  }

private:
  AllocGroup(underlying_type RawId) : Id(RawId) {}
  underlying_type Id = 0;
};

/// A specialized small-map for AllocGroups.
///
/// Iteration order is guaranteed to match key ordering.
template <typename T> class AllocGroupSmallMap {
private:
  using ElemT = std::pair<AllocGroup, T>;
  using VectorTy = SmallVector<ElemT, 4>;

  static bool compareKey(const ElemT &E, const AllocGroup &G) {
    return E.first < G;
  }

public:
  using iterator = typename VectorTy::iterator;

  AllocGroupSmallMap() = default;
  AllocGroupSmallMap(std::initializer_list<std::pair<AllocGroup, T>> Inits)
      : Elems(Inits) {
    llvm::sort(Elems, llvm::less_first());
  }

  iterator begin() { return Elems.begin(); }
  iterator end() { return Elems.end(); }
  iterator find(AllocGroup G) {
    auto I = lower_bound(Elems, G, compareKey);
    return (I == end() || I->first == G) ? I : end();
  }

  bool empty() const { return Elems.empty(); }
  size_t size() const { return Elems.size(); }

  T &operator[](AllocGroup G) {
    auto I = lower_bound(Elems, G, compareKey);
    if (I == Elems.end() || I->first != G)
      I = Elems.insert(I, std::make_pair(G, T()));
    return I->second;
  }

private:
  VectorTy Elems;
};

/// Print an AllocGroup.
inline raw_ostream &operator<<(raw_ostream &OS, AllocGroup AG) {
  return OS << '(' << AG.getMemProt() << ", " << AG.getMemLifetime() << ')';
}

} // end namespace orc

template <> struct DenseMapInfo<orc::MemProt> {
  static inline orc::MemProt getEmptyKey() { return orc::MemProt(~uint8_t(0)); }
  static inline orc::MemProt getTombstoneKey() {
    return orc::MemProt(~uint8_t(0) - 1);
  }
  static unsigned getHashValue(const orc::MemProt &Val) {
    using UT = std::underlying_type_t<orc::MemProt>;
    return DenseMapInfo<UT>::getHashValue(static_cast<UT>(Val));
  }
  static bool isEqual(const orc::MemProt &LHS, const orc::MemProt &RHS) {
    return LHS == RHS;
  }
};

template <> struct DenseMapInfo<orc::AllocGroup> {
  static inline orc::AllocGroup getEmptyKey() {
    return orc::AllocGroup(~uint8_t(0));
  }
  static inline orc::AllocGroup getTombstoneKey() {
    return orc::AllocGroup(~uint8_t(0) - 1);
  }
  static unsigned getHashValue(const orc::AllocGroup &Val) {
    return DenseMapInfo<orc::AllocGroup::underlying_type>::getHashValue(Val.Id);
  }
  static bool isEqual(const orc::AllocGroup &LHS, const orc::AllocGroup &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_MEMORYFLAGS_H
