//===-- wrappers_cpp.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "platform.h"

// Skip this compilation unit if compiled as part of Bionic.
#if !SCUDO_ANDROID || !_BIONIC

#include "allocator_config.h"
#include "internal_defs.h"
#include "platform.h"
#include "scudo/interface.h"
#include "wrappers_c.h"

#include <stdint.h>

namespace std {
struct nothrow_t {};
enum class align_val_t : size_t {};
} // namespace std

static void reportAllocation(void *ptr, size_t size) {
  if (SCUDO_ENABLE_HOOKS)
    if (__scudo_allocate_hook && ptr)
      __scudo_allocate_hook(ptr, size);
}
static void reportDeallocation(void *ptr) {
  if (SCUDO_ENABLE_HOOKS)
    if (__scudo_deallocate_hook)
      __scudo_deallocate_hook(ptr);
}

INTERFACE WEAK void *operator new(size_t size) {
  void *Ptr = Allocator.allocate(size, scudo::Chunk::Origin::New);
  reportAllocation(Ptr, size);
  return Ptr;
}
INTERFACE WEAK void *operator new[](size_t size) {
  void *Ptr = Allocator.allocate(size, scudo::Chunk::Origin::NewArray);
  reportAllocation(Ptr, size);
  return Ptr;
}
INTERFACE WEAK void *operator new(size_t size,
                                  std::nothrow_t const &) NOEXCEPT {
  void *Ptr = Allocator.allocate(size, scudo::Chunk::Origin::New);
  reportAllocation(Ptr, size);
  return Ptr;
}
INTERFACE WEAK void *operator new[](size_t size,
                                    std::nothrow_t const &) NOEXCEPT {
  void *Ptr = Allocator.allocate(size, scudo::Chunk::Origin::NewArray);
  reportAllocation(Ptr, size);
  return Ptr;
}
INTERFACE WEAK void *operator new(size_t size, std::align_val_t align) {
  void *Ptr = Allocator.allocate(size, scudo::Chunk::Origin::New,
                                 static_cast<scudo::uptr>(align));
  reportAllocation(Ptr, size);
  return Ptr;
}
INTERFACE WEAK void *operator new[](size_t size, std::align_val_t align) {
  void *Ptr = Allocator.allocate(size, scudo::Chunk::Origin::NewArray,
                                 static_cast<scudo::uptr>(align));
  reportAllocation(Ptr, size);
  return Ptr;
}
INTERFACE WEAK void *operator new(size_t size, std::align_val_t align,
                                  std::nothrow_t const &) NOEXCEPT {
  void *Ptr = Allocator.allocate(size, scudo::Chunk::Origin::New,
                                 static_cast<scudo::uptr>(align));
  reportAllocation(Ptr, size);
  return Ptr;
}
INTERFACE WEAK void *operator new[](size_t size, std::align_val_t align,
                                    std::nothrow_t const &) NOEXCEPT {
  void *Ptr = Allocator.allocate(size, scudo::Chunk::Origin::NewArray,
                                 static_cast<scudo::uptr>(align));
  reportAllocation(Ptr, size);
  return Ptr;
}

INTERFACE WEAK void operator delete(void *ptr) NOEXCEPT {
  reportDeallocation(ptr);
  Allocator.deallocate(ptr, scudo::Chunk::Origin::New);
}
INTERFACE WEAK void operator delete[](void *ptr) NOEXCEPT {
  reportDeallocation(ptr);
  Allocator.deallocate(ptr, scudo::Chunk::Origin::NewArray);
}
INTERFACE WEAK void operator delete(void *ptr,
                                    std::nothrow_t const &) NOEXCEPT {
  reportDeallocation(ptr);
  Allocator.deallocate(ptr, scudo::Chunk::Origin::New);
}
INTERFACE WEAK void operator delete[](void *ptr,
                                      std::nothrow_t const &) NOEXCEPT {
  reportDeallocation(ptr);
  Allocator.deallocate(ptr, scudo::Chunk::Origin::NewArray);
}
INTERFACE WEAK void operator delete(void *ptr, size_t size) NOEXCEPT {
  reportDeallocation(ptr);
  Allocator.deallocate(ptr, scudo::Chunk::Origin::New, size);
}
INTERFACE WEAK void operator delete[](void *ptr, size_t size) NOEXCEPT {
  reportDeallocation(ptr);
  Allocator.deallocate(ptr, scudo::Chunk::Origin::NewArray, size);
}
INTERFACE WEAK void operator delete(void *ptr,
                                    std::align_val_t align) NOEXCEPT {
  reportDeallocation(ptr);
  Allocator.deallocate(ptr, scudo::Chunk::Origin::New, 0,
                       static_cast<scudo::uptr>(align));
}
INTERFACE WEAK void operator delete[](void *ptr,
                                      std::align_val_t align) NOEXCEPT {
  reportDeallocation(ptr);
  Allocator.deallocate(ptr, scudo::Chunk::Origin::NewArray, 0,
                       static_cast<scudo::uptr>(align));
}
INTERFACE WEAK void operator delete(void *ptr, std::align_val_t align,
                                    std::nothrow_t const &) NOEXCEPT {
  reportDeallocation(ptr);
  Allocator.deallocate(ptr, scudo::Chunk::Origin::New, 0,
                       static_cast<scudo::uptr>(align));
}
INTERFACE WEAK void operator delete[](void *ptr, std::align_val_t align,
                                      std::nothrow_t const &) NOEXCEPT {
  reportDeallocation(ptr);
  Allocator.deallocate(ptr, scudo::Chunk::Origin::NewArray, 0,
                       static_cast<scudo::uptr>(align));
}
INTERFACE WEAK void operator delete(void *ptr, size_t size,
                                    std::align_val_t align) NOEXCEPT {
  reportDeallocation(ptr);
  Allocator.deallocate(ptr, scudo::Chunk::Origin::New, size,
                       static_cast<scudo::uptr>(align));
}
INTERFACE WEAK void operator delete[](void *ptr, size_t size,
                                      std::align_val_t align) NOEXCEPT {
  reportDeallocation(ptr);
  Allocator.deallocate(ptr, scudo::Chunk::Origin::NewArray, size,
                       static_cast<scudo::uptr>(align));
}

#endif // !SCUDO_ANDROID || !_BIONIC
