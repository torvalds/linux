//===-- memprof_interceptors.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// Intercept various libc functions.
//===----------------------------------------------------------------------===//

#include "memprof_interceptors.h"
#include "memprof_allocator.h"
#include "memprof_internal.h"
#include "memprof_mapping.h"
#include "memprof_stack.h"
#include "memprof_stats.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_posix.h"

namespace __memprof {

#define MEMPROF_READ_STRING(s, n) MEMPROF_READ_RANGE((s), (n))

static inline uptr MaybeRealStrnlen(const char *s, uptr maxlen) {
#if SANITIZER_INTERCEPT_STRNLEN
  if (REAL(strnlen)) {
    return REAL(strnlen)(s, maxlen);
  }
#endif
  return internal_strnlen(s, maxlen);
}

void SetThreadName(const char *name) {
  MemprofThread *t = GetCurrentThread();
  if (t)
    memprofThreadRegistry().SetThreadName(t->tid(), name);
}

int OnExit() {
  // FIXME: ask frontend whether we need to return failure.
  return 0;
}

} // namespace __memprof

// ---------------------- Wrappers ---------------- {{{1
using namespace __memprof;

DECLARE_REAL_AND_INTERCEPTOR(void *, malloc, uptr)
DECLARE_REAL_AND_INTERCEPTOR(void, free, void *)

#define COMMON_INTERCEPT_FUNCTION_VER(name, ver)                               \
  MEMPROF_INTERCEPT_FUNC_VER(name, ver)
#define COMMON_INTERCEPT_FUNCTION_VER_UNVERSIONED_FALLBACK(name, ver)          \
  MEMPROF_INTERCEPT_FUNC_VER_UNVERSIONED_FALLBACK(name, ver)
#define COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, size)                         \
  MEMPROF_WRITE_RANGE(ptr, size)
#define COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, size)                          \
  MEMPROF_READ_RANGE(ptr, size)
#define COMMON_INTERCEPTOR_ENTER(ctx, func, ...)                               \
  MEMPROF_INTERCEPTOR_ENTER(ctx, func);                                        \
  do {                                                                         \
    if (memprof_init_is_running)                                               \
      return REAL(func)(__VA_ARGS__);                                          \
    ENSURE_MEMPROF_INITED();                                                   \
  } while (false)
#define COMMON_INTERCEPTOR_DIR_ACQUIRE(ctx, path)                              \
  do {                                                                         \
  } while (false)
#define COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd)                                 \
  do {                                                                         \
  } while (false)
#define COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd)                                 \
  do {                                                                         \
  } while (false)
#define COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, newfd)                    \
  do {                                                                         \
  } while (false)
#define COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, name) SetThreadName(name)
// Should be memprofThreadRegistry().SetThreadNameByUserId(thread, name)
// But memprof does not remember UserId's for threads (pthread_t);
// and remembers all ever existed threads, so the linear search by UserId
// can be slow.
#define COMMON_INTERCEPTOR_SET_PTHREAD_NAME(ctx, thread, name)                 \
  do {                                                                         \
  } while (false)
#define COMMON_INTERCEPTOR_BLOCK_REAL(name) REAL(name)
#define COMMON_INTERCEPTOR_ON_EXIT(ctx) OnExit()
#define COMMON_INTERCEPTOR_LIBRARY_LOADED(filename, handle)
#define COMMON_INTERCEPTOR_LIBRARY_UNLOADED()
#define COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED (!memprof_inited)
#define COMMON_INTERCEPTOR_GET_TLS_RANGE(begin, end)                           \
  if (MemprofThread *t = GetCurrentThread()) {                                 \
    *begin = t->tls_begin();                                                   \
    *end = t->tls_end();                                                       \
  } else {                                                                     \
    *begin = *end = 0;                                                         \
  }

#include "sanitizer_common/sanitizer_common_interceptors.inc"

#define COMMON_SYSCALL_PRE_READ_RANGE(p, s) MEMPROF_READ_RANGE(p, s)
#define COMMON_SYSCALL_PRE_WRITE_RANGE(p, s) MEMPROF_WRITE_RANGE(p, s)
#define COMMON_SYSCALL_POST_READ_RANGE(p, s)                                   \
  do {                                                                         \
    (void)(p);                                                                 \
    (void)(s);                                                                 \
  } while (false)
#define COMMON_SYSCALL_POST_WRITE_RANGE(p, s)                                  \
  do {                                                                         \
    (void)(p);                                                                 \
    (void)(s);                                                                 \
  } while (false)
#include "sanitizer_common/sanitizer_common_syscalls.inc"

struct ThreadStartParam {
  atomic_uintptr_t t;
  atomic_uintptr_t is_registered;
};

static thread_return_t THREAD_CALLING_CONV memprof_thread_start(void *arg) {
  ThreadStartParam *param = reinterpret_cast<ThreadStartParam *>(arg);
  MemprofThread *t = nullptr;
  while ((t = reinterpret_cast<MemprofThread *>(
              atomic_load(&param->t, memory_order_acquire))) == nullptr)
    internal_sched_yield();
  SetCurrentThread(t);
  return t->ThreadStart(GetTid(), &param->is_registered);
}

INTERCEPTOR(int, pthread_create, void *thread, void *attr,
            void *(*start_routine)(void *), void *arg) {
  EnsureMainThreadIDIsCorrect();
  GET_STACK_TRACE_THREAD;
  int detached = 0;
  if (attr)
    REAL(pthread_attr_getdetachstate)(attr, &detached);
  ThreadStartParam param;
  atomic_store(&param.t, 0, memory_order_relaxed);
  atomic_store(&param.is_registered, 0, memory_order_relaxed);
  int result;
  {
    // Ignore all allocations made by pthread_create: thread stack/TLS may be
    // stored by pthread for future reuse even after thread destruction, and
    // the linked list it's stored in doesn't even hold valid pointers to the
    // objects, the latter are calculated by obscure pointer arithmetic.
    result = REAL(pthread_create)(thread, attr, memprof_thread_start, &param);
  }
  if (result == 0) {
    u32 current_tid = GetCurrentTidOrInvalid();
    MemprofThread *t = MemprofThread::Create(start_routine, arg, current_tid,
                                             &stack, detached);
    atomic_store(&param.t, reinterpret_cast<uptr>(t), memory_order_release);
    // Wait until the MemprofThread object is initialized and the
    // ThreadRegistry entry is in "started" state.
    while (atomic_load(&param.is_registered, memory_order_acquire) == 0)
      internal_sched_yield();
  }
  return result;
}

INTERCEPTOR(int, pthread_join, void *t, void **arg) {
  return REAL(pthread_join)(t, arg);
}

DEFINE_INTERNAL_PTHREAD_FUNCTIONS

INTERCEPTOR(char *, index, const char *string, int c)
ALIAS(WRAP(strchr));

// For both strcat() and strncat() we need to check the validity of |to|
// argument irrespective of the |from| length.
INTERCEPTOR(char *, strcat, char *to, const char *from) {
  void *ctx;
  MEMPROF_INTERCEPTOR_ENTER(ctx, strcat);
  ENSURE_MEMPROF_INITED();
  uptr from_length = internal_strlen(from);
  MEMPROF_READ_RANGE(from, from_length + 1);
  uptr to_length = internal_strlen(to);
  MEMPROF_READ_STRING(to, to_length);
  MEMPROF_WRITE_RANGE(to + to_length, from_length + 1);
  return REAL(strcat)(to, from);
}

INTERCEPTOR(char *, strncat, char *to, const char *from, uptr size) {
  void *ctx;
  MEMPROF_INTERCEPTOR_ENTER(ctx, strncat);
  ENSURE_MEMPROF_INITED();
  uptr from_length = MaybeRealStrnlen(from, size);
  uptr copy_length = Min(size, from_length + 1);
  MEMPROF_READ_RANGE(from, copy_length);
  uptr to_length = internal_strlen(to);
  MEMPROF_READ_STRING(to, to_length);
  MEMPROF_WRITE_RANGE(to + to_length, from_length + 1);
  return REAL(strncat)(to, from, size);
}

INTERCEPTOR(char *, strcpy, char *to, const char *from) {
  void *ctx;
  MEMPROF_INTERCEPTOR_ENTER(ctx, strcpy);
  if (memprof_init_is_running) {
    return REAL(strcpy)(to, from);
  }
  ENSURE_MEMPROF_INITED();
  uptr from_size = internal_strlen(from) + 1;
  MEMPROF_READ_RANGE(from, from_size);
  MEMPROF_WRITE_RANGE(to, from_size);
  return REAL(strcpy)(to, from);
}

INTERCEPTOR(char *, strdup, const char *s) {
  void *ctx;
  MEMPROF_INTERCEPTOR_ENTER(ctx, strdup);
  if (UNLIKELY(!memprof_inited))
    return internal_strdup(s);
  ENSURE_MEMPROF_INITED();
  uptr length = internal_strlen(s);
  MEMPROF_READ_RANGE(s, length + 1);
  GET_STACK_TRACE_MALLOC;
  void *new_mem = memprof_malloc(length + 1, &stack);
  REAL(memcpy)(new_mem, s, length + 1);
  return reinterpret_cast<char *>(new_mem);
}

INTERCEPTOR(char *, __strdup, const char *s) {
  void *ctx;
  MEMPROF_INTERCEPTOR_ENTER(ctx, strdup);
  if (UNLIKELY(!memprof_inited))
    return internal_strdup(s);
  ENSURE_MEMPROF_INITED();
  uptr length = internal_strlen(s);
  MEMPROF_READ_RANGE(s, length + 1);
  GET_STACK_TRACE_MALLOC;
  void *new_mem = memprof_malloc(length + 1, &stack);
  REAL(memcpy)(new_mem, s, length + 1);
  return reinterpret_cast<char *>(new_mem);
}

INTERCEPTOR(char *, strncpy, char *to, const char *from, uptr size) {
  void *ctx;
  MEMPROF_INTERCEPTOR_ENTER(ctx, strncpy);
  ENSURE_MEMPROF_INITED();
  uptr from_size = Min(size, MaybeRealStrnlen(from, size) + 1);
  MEMPROF_READ_RANGE(from, from_size);
  MEMPROF_WRITE_RANGE(to, size);
  return REAL(strncpy)(to, from, size);
}

INTERCEPTOR(long, strtol, const char *nptr, char **endptr, int base) {
  void *ctx;
  MEMPROF_INTERCEPTOR_ENTER(ctx, strtol);
  ENSURE_MEMPROF_INITED();
  char *real_endptr;
  long result = REAL(strtol)(nptr, &real_endptr, base);
  StrtolFixAndCheck(ctx, nptr, endptr, real_endptr, base);
  return result;
}

INTERCEPTOR(int, atoi, const char *nptr) {
  void *ctx;
  MEMPROF_INTERCEPTOR_ENTER(ctx, atoi);
  ENSURE_MEMPROF_INITED();
  char *real_endptr;
  // "man atoi" tells that behavior of atoi(nptr) is the same as
  // strtol(nptr, 0, 10), i.e. it sets errno to ERANGE if the
  // parsed integer can't be stored in *long* type (even if it's
  // different from int). So, we just imitate this behavior.
  int result = REAL(strtol)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  MEMPROF_READ_STRING(nptr, (real_endptr - nptr) + 1);
  return result;
}

INTERCEPTOR(long, atol, const char *nptr) {
  void *ctx;
  MEMPROF_INTERCEPTOR_ENTER(ctx, atol);
  ENSURE_MEMPROF_INITED();
  char *real_endptr;
  long result = REAL(strtol)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  MEMPROF_READ_STRING(nptr, (real_endptr - nptr) + 1);
  return result;
}

INTERCEPTOR(long long, strtoll, const char *nptr, char **endptr, int base) {
  void *ctx;
  MEMPROF_INTERCEPTOR_ENTER(ctx, strtoll);
  ENSURE_MEMPROF_INITED();
  char *real_endptr;
  long long result = REAL(strtoll)(nptr, &real_endptr, base);
  StrtolFixAndCheck(ctx, nptr, endptr, real_endptr, base);
  return result;
}

INTERCEPTOR(long long, atoll, const char *nptr) {
  void *ctx;
  MEMPROF_INTERCEPTOR_ENTER(ctx, atoll);
  ENSURE_MEMPROF_INITED();
  char *real_endptr;
  long long result = REAL(strtoll)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  MEMPROF_READ_STRING(nptr, (real_endptr - nptr) + 1);
  return result;
}

// ---------------------- InitializeMemprofInterceptors ---------------- {{{1
namespace __memprof {
void InitializeMemprofInterceptors() {
  static bool was_called_once;
  CHECK(!was_called_once);
  was_called_once = true;
  InitializeCommonInterceptors();

  // Intercept str* functions.
  MEMPROF_INTERCEPT_FUNC(strcat);
  MEMPROF_INTERCEPT_FUNC(strcpy);
  MEMPROF_INTERCEPT_FUNC(strncat);
  MEMPROF_INTERCEPT_FUNC(strncpy);
  MEMPROF_INTERCEPT_FUNC(strdup);
  MEMPROF_INTERCEPT_FUNC(__strdup);
  MEMPROF_INTERCEPT_FUNC(index);

  MEMPROF_INTERCEPT_FUNC(atoi);
  MEMPROF_INTERCEPT_FUNC(atol);
  MEMPROF_INTERCEPT_FUNC(strtol);
  MEMPROF_INTERCEPT_FUNC(atoll);
  MEMPROF_INTERCEPT_FUNC(strtoll);

  // Intercept threading-related functions
  MEMPROF_INTERCEPT_FUNC(pthread_create);
  MEMPROF_INTERCEPT_FUNC(pthread_join);

  InitializePlatformInterceptors();

  VReport(1, "MemProfiler: libc interceptors initialized\n");
}

} // namespace __memprof
