/*===---- mm_malloc.h - Implementation of _mm_malloc and _mm_free ----------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef _MM_MALLOC_H_INCLUDED
#define _MM_MALLOC_H_INCLUDED

#if defined(__powerpc64__) &&                                                  \
    (defined(__linux__) || defined(__FreeBSD__) || defined(_AIX))

#include <stdlib.h>

/* We can't depend on <stdlib.h> since the prototype of posix_memalign
   may not be visible.  */
#ifndef __cplusplus
extern int posix_memalign(void **, size_t, size_t);
#else
extern "C" int posix_memalign(void **, size_t, size_t);
#endif

static __inline void *_mm_malloc(size_t __size, size_t __alignment) {
  /* PowerPC64 ELF V2 ABI requires quadword alignment.  */
  size_t __vec_align = sizeof(__vector float);
  void *__ptr;

  if (__alignment < __vec_align)
    __alignment = __vec_align;
  if (posix_memalign(&__ptr, __alignment, __size) == 0)
    return __ptr;
  else
    return NULL;
}

static __inline void _mm_free(void *__ptr) { free(__ptr); }

#else
#include_next <mm_malloc.h>
#endif

#endif /* _MM_MALLOC_H_INCLUDED */
