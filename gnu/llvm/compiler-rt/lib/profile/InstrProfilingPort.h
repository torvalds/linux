/*===- InstrProfilingPort.h- Support library for PGO instrumentation ------===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

/* This header must be included after all others so it can provide fallback
   definitions for stuff missing in system headers. */

#ifndef PROFILE_INSTRPROFILING_PORT_H_
#define PROFILE_INSTRPROFILING_PORT_H_

#ifdef _MSC_VER
#define COMPILER_RT_ALIGNAS(x) __declspec(align(x))
#define COMPILER_RT_VISIBILITY
/* FIXME: selectany does not have the same semantics as weak. */
#define COMPILER_RT_WEAK __declspec(selectany)
/* Need to include <windows.h> */
#define COMPILER_RT_ALLOCA _alloca
/* Need to include <stdio.h> and <io.h> */
#define COMPILER_RT_FTRUNCATE(f,l) _chsize(_fileno(f),l)
#define COMPILER_RT_ALWAYS_INLINE __forceinline
#define COMPILER_RT_CLEANUP(x)
#define COMPILER_RT_USED
#elif __GNUC__
#ifdef _WIN32
#define COMPILER_RT_FTRUNCATE(f, l) _chsize(fileno(f), l)
#define COMPILER_RT_VISIBILITY
#define COMPILER_RT_WEAK __attribute__((selectany))
#else
#define COMPILER_RT_FTRUNCATE(f, l) ftruncate(fileno(f), l)
#define COMPILER_RT_VISIBILITY __attribute__((visibility("hidden")))
#define COMPILER_RT_WEAK __attribute__((weak))
#endif
#define COMPILER_RT_ALIGNAS(x) __attribute__((aligned(x)))
#define COMPILER_RT_ALLOCA __builtin_alloca
#define COMPILER_RT_ALWAYS_INLINE inline __attribute((always_inline))
#define COMPILER_RT_CLEANUP(x) __attribute__((cleanup(x)))
#define COMPILER_RT_USED __attribute__((used))
#endif

#if defined(__APPLE__)
#define COMPILER_RT_SEG "__DATA,"
#else
#define COMPILER_RT_SEG ""
#endif

#ifdef _MSC_VER
#define COMPILER_RT_SECTION(Sect) __declspec(allocate(Sect))
#else
#define COMPILER_RT_SECTION(Sect) __attribute__((section(Sect)))
#endif

#define COMPILER_RT_MAX_HOSTLEN 128
#ifdef __ORBIS__
#define COMPILER_RT_GETHOSTNAME(Name, Len) ((void)(Name), (void)(Len), (-1))
#else
#define COMPILER_RT_GETHOSTNAME(Name, Len) lprofGetHostName(Name, Len)
#endif

#if COMPILER_RT_HAS_ATOMICS == 1
#ifdef _WIN32
#include <windows.h>
#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf _snprintf
#endif
#if defined(_WIN64)
#define COMPILER_RT_BOOL_CMPXCHG(Ptr, OldV, NewV)                              \
  (InterlockedCompareExchange64((LONGLONG volatile *)Ptr, (LONGLONG)NewV,      \
                                (LONGLONG)OldV) == (LONGLONG)OldV)
#define COMPILER_RT_PTR_FETCH_ADD(DomType, PtrVar, PtrIncr)                    \
  (DomType *)InterlockedExchangeAdd64((LONGLONG volatile *)&PtrVar,            \
                                      (LONGLONG)sizeof(DomType) * PtrIncr)
#else /* !defined(_WIN64) */
#define COMPILER_RT_BOOL_CMPXCHG(Ptr, OldV, NewV)                              \
  (InterlockedCompareExchange((LONG volatile *)Ptr, (LONG)NewV, (LONG)OldV) == \
   (LONG)OldV)
#define COMPILER_RT_PTR_FETCH_ADD(DomType, PtrVar, PtrIncr)                    \
  (DomType *)InterlockedExchangeAdd((LONG volatile *)&PtrVar,                  \
                                    (LONG)sizeof(DomType) * PtrIncr)
#endif
#else /* !defined(_WIN32) */
#define COMPILER_RT_BOOL_CMPXCHG(Ptr, OldV, NewV)                              \
  __sync_bool_compare_and_swap(Ptr, OldV, NewV)
#define COMPILER_RT_PTR_FETCH_ADD(DomType, PtrVar, PtrIncr)                    \
  (DomType *)__sync_fetch_and_add((long *)&PtrVar, sizeof(DomType) * PtrIncr)
#endif
#else /* COMPILER_RT_HAS_ATOMICS != 1 */
#include "InstrProfilingUtil.h"
#define COMPILER_RT_BOOL_CMPXCHG(Ptr, OldV, NewV)                              \
  lprofBoolCmpXchg((void **)Ptr, OldV, NewV)
#define COMPILER_RT_PTR_FETCH_ADD(DomType, PtrVar, PtrIncr)                    \
  (DomType *)lprofPtrFetchAdd((void **)&PtrVar, sizeof(DomType) * PtrIncr)
#endif

#if defined(_WIN32)
#define DIR_SEPARATOR '\\'
#define DIR_SEPARATOR_2 '/'
#else
#define DIR_SEPARATOR '/'
#endif

#ifndef DIR_SEPARATOR_2
#define IS_DIR_SEPARATOR(ch) ((ch) == DIR_SEPARATOR)
#else /* DIR_SEPARATOR_2 */
#define IS_DIR_SEPARATOR(ch)                                                   \
  (((ch) == DIR_SEPARATOR) || ((ch) == DIR_SEPARATOR_2))
#endif /* DIR_SEPARATOR_2 */

#if defined(_WIN32)
#include <windows.h>
static inline size_t getpagesize() {
  SYSTEM_INFO S;
  GetNativeSystemInfo(&S);
  return S.dwPageSize;
}
#else /* defined(_WIN32) */
#include <unistd.h>
#endif /* defined(_WIN32) */

#define PROF_ERR(Format, ...)                                                  \
  fprintf(stderr, "LLVM Profile Error: " Format, __VA_ARGS__);

#define PROF_WARN(Format, ...)                                                 \
  fprintf(stderr, "LLVM Profile Warning: " Format, __VA_ARGS__);

#define PROF_NOTE(Format, ...)                                                 \
  fprintf(stderr, "LLVM Profile Note: " Format, __VA_ARGS__);

#ifndef MAP_FILE
#define MAP_FILE 0
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#if defined(__FreeBSD__)

#include <inttypes.h>
#include <sys/types.h>

#else /* defined(__FreeBSD__) */

#include <inttypes.h>
#include <stdint.h>

#endif /* defined(__FreeBSD__) && defined(__i386__) */

#endif /* PROFILE_INSTRPROFILING_PORT_H_ */
