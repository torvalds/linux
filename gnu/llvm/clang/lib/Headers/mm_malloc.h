/*===---- mm_malloc.h - Allocating and Freeing Aligned Memory Blocks -------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __MM_MALLOC_H
#define __MM_MALLOC_H

#include <stdlib.h>

#ifdef _WIN32
#include <malloc.h>
#else
#ifndef __cplusplus
extern int posix_memalign(void **__memptr, size_t __alignment, size_t __size);
#else
// Some systems (e.g. those with GNU libc) declare posix_memalign with an
// exception specifier. Via an "egregious workaround" in
// Sema::CheckEquivalentExceptionSpec, Clang accepts the following as a valid
// redeclaration of glibc's declaration.
extern "C" int posix_memalign(void **__memptr, size_t __alignment, size_t __size);
#endif
#endif

#if !(defined(_WIN32) && defined(_mm_malloc))
static __inline__ void *__attribute__((__always_inline__, __nodebug__,
                                       __malloc__, __alloc_size__(1),
                                       __alloc_align__(2)))
_mm_malloc(size_t __size, size_t __align) {
  if (__align == 1) {
    return malloc(__size);
  }

  if (!(__align & (__align - 1)) && __align < sizeof(void *))
    __align = sizeof(void *);

  void *__mallocedMemory;
#if defined(__MINGW32__)
  __mallocedMemory = __mingw_aligned_malloc(__size, __align);
#elif defined(_WIN32)
  __mallocedMemory = _aligned_malloc(__size, __align);
#else
  if (posix_memalign(&__mallocedMemory, __align, __size))
    return 0;
#endif

  return __mallocedMemory;
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_free(void *__p)
{
#if defined(__MINGW32__)
  __mingw_aligned_free(__p);
#elif defined(_WIN32)
  _aligned_free(__p);
#else
  free(__p);
#endif
}
#endif

#endif /* __MM_MALLOC_H */
