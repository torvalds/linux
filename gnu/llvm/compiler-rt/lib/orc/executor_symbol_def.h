//===--------- ExecutorSymbolDef.h - (Addr, Flags) pair ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Represents a defining location for a symbol in the executing program.
//
// This file was derived from
// llvm/include/llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_EXECUTOR_SYMBOL_DEF_H
#define ORC_RT_EXECUTOR_SYMBOL_DEF_H

#include "bitmask_enum.h"
#include "executor_address.h"
#include "simple_packed_serialization.h"

namespace __orc_rt {

/// Flags for symbols in the JIT.
class JITSymbolFlags {
public:
  using UnderlyingType = uint8_t;
  using TargetFlagsType = uint8_t;

  /// These values must be kept in sync with \c JITSymbolFlags in the JIT.
  enum FlagNames : UnderlyingType {
    None = 0,
    HasError = 1U << 0,
    Weak = 1U << 1,
    Common = 1U << 2,
    Absolute = 1U << 3,
    Exported = 1U << 4,
    Callable = 1U << 5,
    MaterializationSideEffectsOnly = 1U << 6,
    ORC_RT_MARK_AS_BITMASK_ENUM( // LargestValue =
        MaterializationSideEffectsOnly)
  };

  /// Default-construct a JITSymbolFlags instance.
  JITSymbolFlags() = default;

  /// Construct a JITSymbolFlags instance from the given flags and target
  ///        flags.
  JITSymbolFlags(FlagNames Flags, TargetFlagsType TargetFlags)
      : TargetFlags(TargetFlags), Flags(Flags) {}

  bool operator==(const JITSymbolFlags &RHS) const {
    return Flags == RHS.Flags && TargetFlags == RHS.TargetFlags;
  }

  /// Get the underlying flags value as an integer.
  UnderlyingType getRawFlagsValue() const {
    return static_cast<UnderlyingType>(Flags);
  }

  /// Return a reference to the target-specific flags.
  TargetFlagsType &getTargetFlags() { return TargetFlags; }

  /// Return a reference to the target-specific flags.
  const TargetFlagsType &getTargetFlags() const { return TargetFlags; }

private:
  TargetFlagsType TargetFlags = 0;
  FlagNames Flags = None;
};

/// Represents a defining location for a JIT symbol.
class ExecutorSymbolDef {
public:
  ExecutorSymbolDef() = default;
  ExecutorSymbolDef(ExecutorAddr Addr, JITSymbolFlags Flags)
      : Addr(Addr), Flags(Flags) {}

  const ExecutorAddr &getAddress() const { return Addr; }

  const JITSymbolFlags &getFlags() const { return Flags; }

  friend bool operator==(const ExecutorSymbolDef &LHS,
                         const ExecutorSymbolDef &RHS) {
    return LHS.getAddress() == RHS.getAddress() &&
           LHS.getFlags() == RHS.getFlags();
  }

private:
  ExecutorAddr Addr;
  JITSymbolFlags Flags;
};

using SPSJITSymbolFlags =
    SPSTuple<JITSymbolFlags::UnderlyingType, JITSymbolFlags::TargetFlagsType>;

/// SPS serializatior for JITSymbolFlags.
template <> class SPSSerializationTraits<SPSJITSymbolFlags, JITSymbolFlags> {
  using FlagsArgList = SPSJITSymbolFlags::AsArgList;

public:
  static size_t size(const JITSymbolFlags &F) {
    return FlagsArgList::size(F.getRawFlagsValue(), F.getTargetFlags());
  }

  static bool serialize(SPSOutputBuffer &BOB, const JITSymbolFlags &F) {
    return FlagsArgList::serialize(BOB, F.getRawFlagsValue(),
                                   F.getTargetFlags());
  }

  static bool deserialize(SPSInputBuffer &BIB, JITSymbolFlags &F) {
    JITSymbolFlags::UnderlyingType RawFlags;
    JITSymbolFlags::TargetFlagsType TargetFlags;
    if (!FlagsArgList::deserialize(BIB, RawFlags, TargetFlags))
      return false;
    F = JITSymbolFlags{static_cast<JITSymbolFlags::FlagNames>(RawFlags),
                       TargetFlags};
    return true;
  }
};

using SPSExecutorSymbolDef = SPSTuple<SPSExecutorAddr, SPSJITSymbolFlags>;

/// SPS serializatior for ExecutorSymbolDef.
template <>
class SPSSerializationTraits<SPSExecutorSymbolDef, ExecutorSymbolDef> {
  using DefArgList = SPSExecutorSymbolDef::AsArgList;

public:
  static size_t size(const ExecutorSymbolDef &ESD) {
    return DefArgList::size(ESD.getAddress(), ESD.getFlags());
  }

  static bool serialize(SPSOutputBuffer &BOB, const ExecutorSymbolDef &ESD) {
    return DefArgList::serialize(BOB, ESD.getAddress(), ESD.getFlags());
  }

  static bool deserialize(SPSInputBuffer &BIB, ExecutorSymbolDef &ESD) {
    ExecutorAddr Addr;
    JITSymbolFlags Flags;
    if (!DefArgList::deserialize(BIB, Addr, Flags))
      return false;
    ESD = ExecutorSymbolDef{Addr, Flags};
    return true;
  }
};

} // End namespace __orc_rt

#endif // ORC_RT_EXECUTOR_SYMBOL_DEF_H
