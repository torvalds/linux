//===- AllocatorBase.h - Simple memory allocation abstraction ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines MallocAllocator. MallocAllocator conforms to the LLVM
/// "Allocator" concept which consists of an Allocate method accepting a size
/// and alignment, and a Deallocate accepting a pointer and size. Further, the
/// LLVM "Allocator" concept has overloads of Allocate and Deallocate for
/// setting size and alignment based on the final type. These overloads are
/// typically provided by a base class template \c AllocatorBase.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ALLOCATORBASE_H
#define LLVM_SUPPORT_ALLOCATORBASE_H

#ifdef _MSC_VER
#define LLVM_ALLOCATORHOLDER_EMPTYBASE __declspec(empty_bases)
#else
#define LLVM_ALLOCATORHOLDER_EMPTYBASE
#endif // _MSC_VER

#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemAlloc.h"
#include <type_traits>

namespace llvm {

/// CRTP base class providing obvious overloads for the core \c
/// Allocate() methods of LLVM-style allocators.
///
/// This base class both documents the full public interface exposed by all
/// LLVM-style allocators, and redirects all of the overloads to a single core
/// set of methods which the derived class must define.
template <typename DerivedT> class AllocatorBase {
public:
  /// Allocate \a Size bytes of \a Alignment aligned memory. This method
  /// must be implemented by \c DerivedT.
  void *Allocate(size_t Size, size_t Alignment) {
#ifdef __clang__
    static_assert(static_cast<void *(AllocatorBase::*)(size_t, size_t)>(
                      &AllocatorBase::Allocate) !=
                      static_cast<void *(DerivedT::*)(size_t, size_t)>(
                          &DerivedT::Allocate),
                  "Class derives from AllocatorBase without implementing the "
                  "core Allocate(size_t, size_t) overload!");
#endif
    return static_cast<DerivedT *>(this)->Allocate(Size, Alignment);
  }

  /// Deallocate \a Ptr to \a Size bytes of memory allocated by this
  /// allocator.
  void Deallocate(const void *Ptr, size_t Size, size_t Alignment) {
#ifdef __clang__
    static_assert(
        static_cast<void (AllocatorBase::*)(const void *, size_t, size_t)>(
            &AllocatorBase::Deallocate) !=
            static_cast<void (DerivedT::*)(const void *, size_t, size_t)>(
                &DerivedT::Deallocate),
        "Class derives from AllocatorBase without implementing the "
        "core Deallocate(void *) overload!");
#endif
    return static_cast<DerivedT *>(this)->Deallocate(Ptr, Size, Alignment);
  }

  // The rest of these methods are helpers that redirect to one of the above
  // core methods.

  /// Allocate space for a sequence of objects without constructing them.
  template <typename T> T *Allocate(size_t Num = 1) {
    return static_cast<T *>(Allocate(Num * sizeof(T), alignof(T)));
  }

  /// Deallocate space for a sequence of objects without constructing them.
  template <typename T>
  std::enable_if_t<!std::is_same_v<std::remove_cv_t<T>, void>, void>
  Deallocate(T *Ptr, size_t Num = 1) {
    Deallocate(static_cast<const void *>(Ptr), Num * sizeof(T), alignof(T));
  }
};

class MallocAllocator : public AllocatorBase<MallocAllocator> {
public:
  void Reset() {}

  LLVM_ATTRIBUTE_RETURNS_NONNULL void *Allocate(size_t Size, size_t Alignment) {
    return allocate_buffer(Size, Alignment);
  }

  // Pull in base class overloads.
  using AllocatorBase<MallocAllocator>::Allocate;

  void Deallocate(const void *Ptr, size_t Size, size_t Alignment) {
    deallocate_buffer(const_cast<void *>(Ptr), Size, Alignment);
  }

  // Pull in base class overloads.
  using AllocatorBase<MallocAllocator>::Deallocate;

  void PrintStats() const {}
};

namespace detail {

template <typename Alloc> class AllocatorHolder : Alloc {
public:
  AllocatorHolder() = default;
  AllocatorHolder(const Alloc &A) : Alloc(A) {}
  AllocatorHolder(Alloc &&A) : Alloc(static_cast<Alloc &&>(A)) {}
  Alloc &getAllocator() { return *this; }
  const Alloc &getAllocator() const { return *this; }
};

template <typename Alloc> class AllocatorHolder<Alloc &> {
  Alloc &A;

public:
  AllocatorHolder(Alloc &A) : A(A) {}
  Alloc &getAllocator() { return A; }
  const Alloc &getAllocator() const { return A; }
};

} // namespace detail

} // namespace llvm

#endif // LLVM_SUPPORT_ALLOCATORBASE_H
