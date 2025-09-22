//==--------- DynamicAllocator.h - Dynamic allocations ------------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_DYNAMIC_ALLOCATOR_H
#define LLVM_CLANG_AST_INTERP_DYNAMIC_ALLOCATOR_H

#include "Descriptor.h"
#include "InterpBlock.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"

namespace clang {
class Expr;
namespace interp {
class Block;
class InterpState;

/// Manages dynamic memory allocations done during bytecode interpretation.
///
/// We manage allocations as a map from their new-expression to a list
/// of allocations. This is called an AllocationSite. For each site, we
/// record whether it was allocated using new or new[], the
/// IsArrayAllocation flag.
///
/// For all array allocations, we need to allocate new Descriptor instances,
/// so the DynamicAllocator has a llvm::BumpPtrAllocator similar to Program.
class DynamicAllocator final {
  struct Allocation {
    std::unique_ptr<std::byte[]> Memory;
    Allocation(std::unique_ptr<std::byte[]> Memory)
        : Memory(std::move(Memory)) {}
  };

  struct AllocationSite {
    llvm::SmallVector<Allocation> Allocations;
    bool IsArrayAllocation = false;

    AllocationSite(std::unique_ptr<std::byte[]> Memory, bool Array)
        : IsArrayAllocation(Array) {
      Allocations.push_back({std::move(Memory)});
    }

    size_t size() const { return Allocations.size(); }
  };

public:
  DynamicAllocator() = default;
  ~DynamicAllocator();

  void cleanup();

  unsigned getNumAllocations() const { return AllocationSites.size(); }

  /// Allocate ONE element of the given descriptor.
  Block *allocate(const Descriptor *D, unsigned EvalID);
  /// Allocate \p NumElements primitive elements of the given type.
  Block *allocate(const Expr *Source, PrimType T, size_t NumElements,
                  unsigned EvalID);
  /// Allocate \p NumElements elements of the given descriptor.
  Block *allocate(const Descriptor *D, size_t NumElements, unsigned EvalID);

  /// Deallocate the given source+block combination.
  /// Returns \c true if anything has been deallocatd, \c false otherwise.
  bool deallocate(const Expr *Source, const Block *BlockToDelete,
                  InterpState &S);

  /// Checks whether the allocation done at the given source is an array
  /// allocation.
  bool isArrayAllocation(const Expr *Source) const {
    if (auto It = AllocationSites.find(Source); It != AllocationSites.end())
      return It->second.IsArrayAllocation;
    return false;
  }

  /// Allocation site iterator.
  using const_virtual_iter =
      llvm::DenseMap<const Expr *, AllocationSite>::const_iterator;
  llvm::iterator_range<const_virtual_iter> allocation_sites() const {
    return llvm::make_range(AllocationSites.begin(), AllocationSites.end());
  }

private:
  llvm::DenseMap<const Expr *, AllocationSite> AllocationSites;

  using PoolAllocTy = llvm::BumpPtrAllocatorImpl<llvm::MallocAllocator>;
  PoolAllocTy DescAllocator;

  /// Allocates a new descriptor.
  template <typename... Ts> Descriptor *allocateDescriptor(Ts &&...Args) {
    return new (DescAllocator) Descriptor(std::forward<Ts>(Args)...);
  }
};

} // namespace interp
} // namespace clang
#endif
