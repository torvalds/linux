//===-------------------------- rpnew.h -----------------*- C -*-=============//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This library provides a cross-platform lock free thread caching malloc
// implementation in C11.
//
//===----------------------------------------------------------------------===//

#ifdef __cplusplus

#include <new>
#include <rpmalloc.h>

#ifndef __CRTDECL
#define __CRTDECL
#endif

extern void __CRTDECL operator delete(void *p) noexcept { rpfree(p); }

extern void __CRTDECL operator delete[](void *p) noexcept { rpfree(p); }

extern void *__CRTDECL operator new(std::size_t size) noexcept(false) {
  return rpmalloc(size);
}

extern void *__CRTDECL operator new[](std::size_t size) noexcept(false) {
  return rpmalloc(size);
}

extern void *__CRTDECL operator new(std::size_t size,
                                    const std::nothrow_t &tag) noexcept {
  (void)sizeof(tag);
  return rpmalloc(size);
}

extern void *__CRTDECL operator new[](std::size_t size,
                                      const std::nothrow_t &tag) noexcept {
  (void)sizeof(tag);
  return rpmalloc(size);
}

#if (__cplusplus >= 201402L || _MSC_VER >= 1916)

extern void __CRTDECL operator delete(void *p, std::size_t size) noexcept {
  (void)sizeof(size);
  rpfree(p);
}

extern void __CRTDECL operator delete[](void *p, std::size_t size) noexcept {
  (void)sizeof(size);
  rpfree(p);
}

#endif

#if (__cplusplus > 201402L || defined(__cpp_aligned_new))

extern void __CRTDECL operator delete(void *p,
                                      std::align_val_t align) noexcept {
  (void)sizeof(align);
  rpfree(p);
}

extern void __CRTDECL operator delete[](void *p,
                                        std::align_val_t align) noexcept {
  (void)sizeof(align);
  rpfree(p);
}

extern void __CRTDECL operator delete(void *p, std::size_t size,
                                      std::align_val_t align) noexcept {
  (void)sizeof(size);
  (void)sizeof(align);
  rpfree(p);
}

extern void __CRTDECL operator delete[](void *p, std::size_t size,
                                        std::align_val_t align) noexcept {
  (void)sizeof(size);
  (void)sizeof(align);
  rpfree(p);
}

extern void *__CRTDECL operator new(std::size_t size,
                                    std::align_val_t align) noexcept(false) {
  return rpaligned_alloc(static_cast<size_t>(align), size);
}

extern void *__CRTDECL operator new[](std::size_t size,
                                      std::align_val_t align) noexcept(false) {
  return rpaligned_alloc(static_cast<size_t>(align), size);
}

extern void *__CRTDECL operator new(std::size_t size, std::align_val_t align,
                                    const std::nothrow_t &tag) noexcept {
  (void)sizeof(tag);
  return rpaligned_alloc(static_cast<size_t>(align), size);
}

extern void *__CRTDECL operator new[](std::size_t size, std::align_val_t align,
                                      const std::nothrow_t &tag) noexcept {
  (void)sizeof(tag);
  return rpaligned_alloc(static_cast<size_t>(align), size);
}

#endif

#endif
