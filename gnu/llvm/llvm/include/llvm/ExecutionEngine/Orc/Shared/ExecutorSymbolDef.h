//===--------- ExecutorSymbolDef.h - (Addr, Flags) pair ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Represents a defining location for a JIT symbol.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_EXECUTORSYMBOLDEF_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_EXECUTORSYMBOLDEF_H

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimplePackedSerialization.h"

namespace llvm {
namespace orc {

/// Represents a defining location for a JIT symbol.
class ExecutorSymbolDef {
public:
  ExecutorSymbolDef() = default;
  ExecutorSymbolDef(ExecutorAddr Addr, JITSymbolFlags Flags)
    : Addr(Addr), Flags(Flags) {}

  const ExecutorAddr &getAddress() const { return Addr; }

  const JITSymbolFlags &getFlags() const { return Flags; }

  void setFlags(JITSymbolFlags Flags) { this->Flags = Flags; }

  friend bool operator==(const ExecutorSymbolDef &LHS,
                         const ExecutorSymbolDef &RHS) {
    return LHS.getAddress() == RHS.getAddress() &&
           LHS.getFlags() == RHS.getFlags();
  }

  friend bool operator!=(const ExecutorSymbolDef &LHS,
                         const ExecutorSymbolDef &RHS) {
    return !(LHS == RHS);
  }

private:
  ExecutorAddr Addr;
  JITSymbolFlags Flags;
};

namespace shared {

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

} // End namespace shared.
} // End namespace orc.
} // End namespace llvm.

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_EXECUTORSYMBOLDEF_H
