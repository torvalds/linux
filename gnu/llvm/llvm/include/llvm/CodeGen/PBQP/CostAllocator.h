//===- CostAllocator.h - PBQP Cost Allocator --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines classes conforming to the PBQP cost value manager concept.
//
// Cost value managers are memory managers for PBQP cost values (vectors and
// matrices). Since PBQP graphs can grow very large (E.g. hundreds of thousands
// of edges on the largest function in SPEC2006).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PBQP_COSTALLOCATOR_H
#define LLVM_CODEGEN_PBQP_COSTALLOCATOR_H

#include "llvm/ADT/DenseSet.h"
#include <algorithm>
#include <cstdint>
#include <memory>

namespace llvm {
namespace PBQP {

template <typename ValueT> class ValuePool {
public:
  using PoolRef = std::shared_ptr<const ValueT>;

private:
  class PoolEntry : public std::enable_shared_from_this<PoolEntry> {
  public:
    template <typename ValueKeyT>
    PoolEntry(ValuePool &Pool, ValueKeyT Value)
        : Pool(Pool), Value(std::move(Value)) {}

    ~PoolEntry() { Pool.removeEntry(this); }

    const ValueT &getValue() const { return Value; }

  private:
    ValuePool &Pool;
    ValueT Value;
  };

  class PoolEntryDSInfo {
  public:
    static inline PoolEntry *getEmptyKey() { return nullptr; }

    static inline PoolEntry *getTombstoneKey() {
      return reinterpret_cast<PoolEntry *>(static_cast<uintptr_t>(1));
    }

    template <typename ValueKeyT>
    static unsigned getHashValue(const ValueKeyT &C) {
      return hash_value(C);
    }

    static unsigned getHashValue(PoolEntry *P) {
      return getHashValue(P->getValue());
    }

    static unsigned getHashValue(const PoolEntry *P) {
      return getHashValue(P->getValue());
    }

    template <typename ValueKeyT1, typename ValueKeyT2>
    static bool isEqual(const ValueKeyT1 &C1, const ValueKeyT2 &C2) {
      return C1 == C2;
    }

    template <typename ValueKeyT>
    static bool isEqual(const ValueKeyT &C, PoolEntry *P) {
      if (P == getEmptyKey() || P == getTombstoneKey())
        return false;
      return isEqual(C, P->getValue());
    }

    static bool isEqual(PoolEntry *P1, PoolEntry *P2) {
      if (P1 == getEmptyKey() || P1 == getTombstoneKey())
        return P1 == P2;
      return isEqual(P1->getValue(), P2);
    }
  };

  using EntrySetT = DenseSet<PoolEntry *, PoolEntryDSInfo>;

  EntrySetT EntrySet;

  void removeEntry(PoolEntry *P) { EntrySet.erase(P); }

public:
  template <typename ValueKeyT> PoolRef getValue(ValueKeyT ValueKey) {
    typename EntrySetT::iterator I = EntrySet.find_as(ValueKey);

    if (I != EntrySet.end())
      return PoolRef((*I)->shared_from_this(), &(*I)->getValue());

    auto P = std::make_shared<PoolEntry>(*this, std::move(ValueKey));
    EntrySet.insert(P.get());
    return PoolRef(P, &P->getValue());
  }
};

template <typename VectorT, typename MatrixT> class PoolCostAllocator {
private:
  using VectorCostPool = ValuePool<VectorT>;
  using MatrixCostPool = ValuePool<MatrixT>;

public:
  using Vector = VectorT;
  using Matrix = MatrixT;
  using VectorPtr = typename VectorCostPool::PoolRef;
  using MatrixPtr = typename MatrixCostPool::PoolRef;

  template <typename VectorKeyT> VectorPtr getVector(VectorKeyT v) {
    return VectorPool.getValue(std::move(v));
  }

  template <typename MatrixKeyT> MatrixPtr getMatrix(MatrixKeyT m) {
    return MatrixPool.getValue(std::move(m));
  }

private:
  VectorCostPool VectorPool;
  MatrixCostPool MatrixPool;
};

} // end namespace PBQP
} // end namespace llvm

#endif // LLVM_CODEGEN_PBQP_COSTALLOCATOR_H
