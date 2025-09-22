//===- Memory.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines arena allocators.
//
// Almost all large objects, such as files, sections or symbols, are
// used for the entire lifetime of the linker once they are created.
// This usage characteristic makes arena allocator an attractive choice
// where the entire linker is one arena. With an arena, newly created
// objects belong to the arena and freed all at once when everything is done.
// Arena allocators are efficient and easy to understand.
// Most objects are allocated using the arena allocators defined by this file.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COMMON_MEMORY_H
#define LLD_COMMON_MEMORY_H

#include "llvm/Support/Allocator.h"

namespace lld {
// A base class only used by the CommonLinkerContext to keep track of the
// SpecificAlloc<> instances.
struct SpecificAllocBase {
  virtual ~SpecificAllocBase() = default;
  static SpecificAllocBase *getOrCreate(void *tag, size_t size, size_t align,
                                        SpecificAllocBase *(&creator)(void *));
};

// An arena of specific types T, created on-demand.
template <class T> struct SpecificAlloc : public SpecificAllocBase {
  static SpecificAllocBase *create(void *storage) {
    return new (storage) SpecificAlloc<T>();
  }
  llvm::SpecificBumpPtrAllocator<T> alloc;
  static int tag;
};

// The address of this static member is only used as a key in
// CommonLinkerContext::instances. Its value does not matter.
template <class T> int SpecificAlloc<T>::tag = 0;

// Creates the arena on-demand on the first call; or returns it, if it was
// already created.
template <typename T>
inline llvm::SpecificBumpPtrAllocator<T> &getSpecificAllocSingleton() {
  SpecificAllocBase *instance = SpecificAllocBase::getOrCreate(
      &SpecificAlloc<T>::tag, sizeof(SpecificAlloc<T>),
      alignof(SpecificAlloc<T>), SpecificAlloc<T>::create);
  return ((SpecificAlloc<T> *)instance)->alloc;
}

// Creates new instances of T off a (almost) contiguous arena/object pool. The
// instances are destroyed whenever lldMain() goes out of scope.
template <typename T, typename... U> T *make(U &&... args) {
  return new (getSpecificAllocSingleton<T>().Allocate())
      T(std::forward<U>(args)...);
}

template <typename T>
inline llvm::SpecificBumpPtrAllocator<T> &
getSpecificAllocSingletonThreadLocal() {
  thread_local SpecificAlloc<T> instance;
  return instance.alloc;
}

// Create a new instance of T off a thread-local SpecificAlloc, used by code
// like parallel input section initialization. The use cases assume that the
// return value outlives the containing parallelForEach (if exists), which is
// currently guaranteed: when parallelForEach returns, the threads allocating
// the TLS are not destroyed.
//
// Note: Some ports (e.g. ELF) have lots of global states which are currently
// infeasible to remove, and context() just adds overhead with no benefit. The
// allocation performance is of higher importance, so we simply use thread_local
// allocators instead of doing context indirection and pthread_getspecific.
template <typename T, typename... U> T *makeThreadLocal(U &&...args) {
  return new (getSpecificAllocSingletonThreadLocal<T>().Allocate())
      T(std::forward<U>(args)...);
}

template <typename T> T *makeThreadLocalN(size_t n) {
  return new (getSpecificAllocSingletonThreadLocal<T>().Allocate(n)) T[n];
}

} // namespace lld

#endif
