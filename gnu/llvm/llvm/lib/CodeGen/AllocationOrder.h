//===-- llvm/CodeGen/AllocationOrder.h - Allocation Order -*- C++ -*-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements an allocation order for virtual registers.
//
// The preferred allocation order for a virtual register depends on allocation
// hints and target hooks. The AllocationOrder class encapsulates all of that.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ALLOCATIONORDER_H
#define LLVM_LIB_CODEGEN_ALLOCATIONORDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

class RegisterClassInfo;
class VirtRegMap;
class LiveRegMatrix;

class LLVM_LIBRARY_VISIBILITY AllocationOrder {
  const SmallVector<MCPhysReg, 16> Hints;
  ArrayRef<MCPhysReg> Order;
  // How far into the Order we can iterate. This is 0 if the AllocationOrder is
  // constructed with HardHints = true, Order.size() otherwise. While
  // technically a size_t, it will participate in comparisons with the
  // Iterator's Pos, which must be signed, so it's typed here as signed, too, to
  // avoid warnings and under the assumption that the size of Order is
  // relatively small.
  // IterationLimit defines an invalid iterator position.
  const int IterationLimit;

public:
  /// Forward iterator for an AllocationOrder.
  class Iterator final {
    const AllocationOrder &AO;
    int Pos = 0;

  public:
    Iterator(const AllocationOrder &AO, int Pos) : AO(AO), Pos(Pos) {}

    /// Return true if the curent position is that of a preferred register.
    bool isHint() const { return Pos < 0; }

    /// Return the next physical register in the allocation order.
    MCRegister operator*() const {
      if (Pos < 0)
        return AO.Hints.end()[Pos];
      assert(Pos < AO.IterationLimit);
      return AO.Order[Pos];
    }

    /// Advance the iterator to the next position. If that's past the Hints
    /// list, advance to the first value that's not also in the Hints list.
    Iterator &operator++() {
      if (Pos < AO.IterationLimit)
        ++Pos;
      while (Pos >= 0 && Pos < AO.IterationLimit && AO.isHint(AO.Order[Pos]))
        ++Pos;
      return *this;
    }

    bool operator==(const Iterator &Other) const {
      assert(&AO == &Other.AO);
      return Pos == Other.Pos;
    }

    bool operator!=(const Iterator &Other) const { return !(*this == Other); }
  };

  /// Create a new AllocationOrder for VirtReg.
  /// @param VirtReg      Virtual register to allocate for.
  /// @param VRM          Virtual register map for function.
  /// @param RegClassInfo Information about reserved and allocatable registers.
  static AllocationOrder create(unsigned VirtReg, const VirtRegMap &VRM,
                                const RegisterClassInfo &RegClassInfo,
                                const LiveRegMatrix *Matrix);

  /// Create an AllocationOrder given the Hits, Order, and HardHits values.
  /// Use the create method above - the ctor is for unittests.
  AllocationOrder(SmallVector<MCPhysReg, 16> &&Hints, ArrayRef<MCPhysReg> Order,
                  bool HardHints)
      : Hints(std::move(Hints)), Order(Order),
        IterationLimit(HardHints ? 0 : static_cast<int>(Order.size())) {}

  Iterator begin() const {
    return Iterator(*this, -(static_cast<int>(Hints.size())));
  }

  Iterator end() const { return Iterator(*this, IterationLimit); }

  Iterator getOrderLimitEnd(unsigned OrderLimit) const {
    assert(OrderLimit <= Order.size());
    if (OrderLimit == 0)
      return end();
    Iterator Ret(*this,
                 std::min(static_cast<int>(OrderLimit) - 1, IterationLimit));
    return ++Ret;
  }

  /// Get the allocation order without reordered hints.
  ArrayRef<MCPhysReg> getOrder() const { return Order; }

  /// Return true if Reg is a preferred physical register.
  bool isHint(Register Reg) const {
    assert(!Reg.isPhysical() ||
           Reg.id() <
               static_cast<uint32_t>(std::numeric_limits<MCPhysReg>::max()));
    return Reg.isPhysical() && is_contained(Hints, Reg.id());
  }
};

} // end namespace llvm

#endif
