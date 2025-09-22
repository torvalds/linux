//===-- asan_interceptors.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Intercept various libc functions.
//===----------------------------------------------------------------------===//

#include "asan_interceptors.h"

#include "asan_allocator.h"
#include "asan_internal.h"
#include "asan_mapping.h"
#include "asan_poisoning.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "asan_stats.h"
#include "asan_suppressions.h"
#include "asan_thread.h"
#include "lsan/lsan_common.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_libc.h"

// There is no general interception at all on Fuchsia.
// Only the functions in asan_interceptors_memintrinsics.cpp are
// really defined to replace libc functions.
#if !SANITIZER_FUCHSIA

#  if SANITIZER_POSIX
#    include "sanitizer_common/sanitizer_posix.h"
#  endif

#  if ASAN_INTERCEPT__UNWIND_RAISEEXCEPTION || \
      ASAN_INTERCEPT__SJLJ_UNWIND_RAISEEXCEPTION
#    include <unwind.h>
#  endif

#  if defined(__i386) && SANITIZER_LINUX
#    define ASAN_PTHREAD_CREATE_VERSION "GLIBC_2.1"
#  elif defined(__mips__) && SANITIZER_LINUX
#    define ASAN_PTHREAD_CREATE_VERSION "GLIBC_2.2"
#  endif

namespace __asan {

#define ASAN_READ_STRING_OF_LEN(ctx, s, len, n)                 \
  ASAN_READ_RANGE((ctx), (s),                                   \
    common_flags()->strict_string_checks ? (len) + 1 : (n))

#  define ASAN_READ_STRING(ctx, s, n) \
    ASAN_READ_STRING_OF_LEN((ctx), (s), internal_strlen(s), (n))

static inline uptr MaybeRealStrnlen(const char *s, uptr maxlen) {
#if SANITIZER_INTERCEPT_STRNLEN
  if (REAL(strnlen)) {
    return REAL(strnlen)(s, maxlen);
  }
#endif
  return internal_strnlen(s, maxlen);
}

void SetThreadName(const char *name) {
  AsanThread *t = GetCurrentThread();
  if (t)
    asanThreadRegistry().SetThreadName(t->tid(), name);
}

int OnExit() {
  if (CAN_SANITIZE_LEAKS && common_flags()->detect_leaks &&
      __lsan::HasReportedLeaks()) {
    return common_flags()->exitcode;
  }
  // FIXME: ask frontend whether we need to return failure.
  return 0;
}

} // namespace __asan

// ---------------------- Wrappers ---------------- {{{1
using namespace __asan;

DECLARE_REAL_AND_INTERCEPTOR(void *, malloc, uptr)
DECLARE_REAL_AND_INTERCEPTOR(void, free, void *)

#define COMMON_INTERCEPT_FUNCTION_VER(name, ver) \
  ASAN_INTERCEPT_FUNC_VER(name, ver)
#define COMMON_INTERCEPT_FUNCTION_VER_UNVERSIONED_FALLBACK(name, ver) \
  ASAN_INTERCEPT_FUNC_VER_UNVERSIONED_FALLBACK(name, ver)
#define COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, size) \
  ASAN_WRITE_RANGE(ctx, ptr, size)
#define COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, size) \
  ASAN_READ_RANGE(ctx, ptr, size)
#  define COMMON_INTERCEPTOR_ENTER(ctx, func, ...) \
    ASAN_INTERCEPTOR_ENTER(ctx, func);             \
    do {                                           \
      if constexpr (SANITIZER_APPLE) {             \
        if (UNLIKELY(!AsanInited()))               \
          return REAL(func)(__VA_ARGS__);          \
      } else {                                     \
        if (!TryAsanInitFromRtl())                 \
          return REAL(func)(__VA_ARGS__);          \
      }                                            \
    } while (false)
#define COMMON_INTERCEPTOR_DIR_ACQUIRE(ctx, path) \
  do {                                            \
  } while (false)
#define COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd) \
  do {                                         \
  } while (false)
#define COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd) \
  do {                                         \
  } while (false)
#define COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, newfd) \
  do {                                                      \
  } while (false)
#define COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, name) SetThreadName(name)
// Should be asanThreadRegistry().SetThreadNameByUserId(thread, name)
// But asan does not remember UserId's for threads (pthread_t);
// and remembers all ever existed threads, so the linear search by UserId
// can be slow.
#define COMMON_INTERCEPTOR_SET_PTHREAD_NAME(ctx, thread, name) \
  do {                                                         \
  } while (false)
#define COMMON_INTERCEPTOR_BLOCK_REAL(name) REAL(name)
// Strict init-order checking is dlopen-hostile:
// https://github.com/google/sanitizers/issues/178
#  define COMMON_INTERCEPTOR_DLOPEN(filename, flag) \
    ({                                              \
      if (flags()->strict_init_order)               \
        StopInitOrderChecking();                    \
      CheckNoDeepBind(filename, flag);              \
      REAL(dlopen)(filename, flag);                 \
    })
#  define COMMON_INTERCEPTOR_ON_EXIT(ctx) OnExit()
#  define COMMON_INTERCEPTOR_LIBRARY_LOADED(filename, handle)
#  define COMMON_INTERCEPTOR_LIBRARY_UNLOADED()
#  define COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED (!AsanInited())
#  define COMMON_INTERCEPTOR_GET_TLS_RANGE(begin, end) \
    if (AsanThread *t = GetCurrentThread()) {          \
      *begin = t->tls_begin();                         \
      *end = t->tls_end();                             \
    } else {                                           \
      *begin = *end = 0;                               \
    }

template <class Mmap>
static void* mmap_interceptor(Mmap real_mmap, void *addr, SIZE_T length,
                              int prot, int flags, int fd, OFF64_T offset) {
  void *res = real_mmap(addr, length, prot, flags, fd, offset);
  if (length && res != (void *)-1) {
    const uptr beg = reinterpret_cast<uptr>(res);
    DCHECK(IsAligned(beg, GetPageSize()));
    SIZE_T rounded_length = RoundUpTo(length, GetPageSize());
    // Only unpoison shadow if it's an ASAN managed address.
    if (AddrIsInMem(beg) && AddrIsInMem(beg + rounded_length - 1))
      PoisonShadow(beg, RoundUpTo(length, GetPageSize()), 0);
  }
  return res;
}

template <class Munmap>
static int munmap_interceptor(Munmap real_munmap, void *addr, SIZE_T length) {
  // We should not tag if munmap fail, but it's to late to tag after
  // real_munmap, as the pages could be mmaped by another thread.
  const uptr beg = reinterpret_cast<uptr>(addr);
  if (length && IsAligned(beg, GetPageSize())) {
    SIZE_T rounded_length = RoundUpTo(length, GetPageSize());
    // Protect from unmapping the shadow.
    if (AddrIsInMem(beg) && AddrIsInMem(beg + rounded_length - 1))
      PoisonShadow(beg, rounded_length, 0);
  }
  return real_munmap(addr, length);
}

#  define COMMON_INTERCEPTOR_MMAP_IMPL(ctx, mmap, addr, length, prot, flags,   \
                                     fd, offset)                               \
  do {                                                                         \
    (void)(ctx);                                                               \
    return mmap_interceptor(REAL(mmap), addr, sz, prot, flags, fd, off);       \
  } while (false)

#  define COMMON_INTERCEPTOR_MUNMAP_IMPL(ctx, addr, length)                    \
  do {                                                                         \
    (void)(ctx);                                                               \
    return munmap_interceptor(REAL(munmap), addr, sz);                         \
  } while (false)

#if CAN_SANITIZE_LEAKS
#define COMMON_INTERCEPTOR_STRERROR()                       \
  __lsan::ScopedInterceptorDisabler disabler
#endif

#  define SIGNAL_INTERCEPTOR_ENTER() \
    do {                             \
      AsanInitFromRtl();             \
    } while (false)

#  include "sanitizer_common/sanitizer_common_interceptors.inc"
#  include "sanitizer_common/sanitizer_signal_interceptors.inc"

// Syscall interceptors don't have contexts, we don't support suppressions
// for them.
#define COMMON_SYSCALL_PRE_READ_RANGE(p, s) ASAN_READ_RANGE(nullptr, p, s)
#define COMMON_SYSCALL_PRE_WRITE_RANGE(p, s) ASAN_WRITE_RANGE(nullptr, p, s)
#define COMMON_SYSCALL_POST_READ_RANGE(p, s) \
  do {                                       \
    (void)(p);                               \
    (void)(s);                               \
  } while (false)
#define COMMON_SYSCALL_POST_WRITE_RANGE(p, s) \
  do {                                        \
    (void)(p);                                \
    (void)(s);                                \
  } while (false)
#include "sanitizer_common/sanitizer_common_syscalls.inc"
#include "sanitizer_common/sanitizer_syscalls_netbsd.inc"

#if ASAN_INTERCEPT_PTHREAD_CREATE
static thread_return_t THREAD_CALLING_CONV asan_thread_start(void *arg) {
  AsanThread *t = (AsanThread *)arg;
  SetCurrentThread(t);
  auto self = GetThreadSelf();
  auto args = asanThreadArgRetval().GetArgs(self);
  t->ThreadStart(GetTid());

#    if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
        SANITIZER_SOLARIS
  __sanitizer_sigset_t sigset;
  t->GetStartData(sigset);
  SetSigProcMask(&sigset, nullptr);
#    endif

  thread_return_t retval = (*args.routine)(args.arg_retval);
  asanThreadArgRetval().Finish(self, retval);
  return retval;
}

INTERCEPTOR(int, pthread_create, void *thread, void *attr,
            void *(*start_routine)(void *), void *arg) {
  EnsureMainThreadIDIsCorrect();
  // Strict init-order checking is thread-hostile.
  if (flags()->strict_init_order)
    StopInitOrderChecking();
  GET_STACK_TRACE_THREAD;
  bool detached = [attr]() {
    int d = 0;
    return attr && !REAL(pthread_attr_getdetachstate)(attr, &d) &&
           IsStateDetached(d);
  }();

  u32 current_tid = GetCurrentTidOrInvalid();

  __sanitizer_sigset_t sigset = {};
#    if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
        SANITIZER_SOLARIS
  ScopedBlockSignals block(&sigset);
#    endif

  AsanThread *t = AsanThread::Create(sigset, current_tid, &stack, detached);

  int result;
  {
    // Ignore all allocations made by pthread_create: thread stack/TLS may be
    // stored by pthread for future reuse even after thread destruction, and
    // the linked list it's stored in doesn't even hold valid pointers to the
    // objects, the latter are calculated by obscure pointer arithmetic.
#    if CAN_SANITIZE_LEAKS
    __lsan::ScopedInterceptorDisabler disabler;
#    endif
    asanThreadArgRetval().Create(detached, {start_routine, arg}, [&]() -> uptr {
      result = REAL(pthread_create)(thread, attr, asan_thread_start, t);
      return result ? 0 : *(uptr *)(thread);
    });
  }
  if (result != 0) {
    // If the thread didn't start delete the AsanThread to avoid leaking it.
    // Note AsanThreadContexts never get destroyed so the AsanThreadContext
    // that was just created for the AsanThread is wasted.
    t->Destroy();
  }
  return result;
}

INTERCEPTOR(int, pthread_join, void *thread, void **retval) {
  int result;
  asanThreadArgRetval().Join((uptr)thread, [&]() {
    result = REAL(pthread_join)(thread, retval);
    return !result;
  });
  return result;
}

INTERCEPTOR(int, pthread_detach, void *thread) {
  int result;
  asanThreadArgRetval().Detach((uptr)thread, [&]() {
    result = REAL(pthread_detach)(thread);
    return !result;
  });
  return result;
}

INTERCEPTOR(void, pthread_exit, void *retval) {
  asanThreadArgRetval().Finish(GetThreadSelf(), retval);
  REAL(pthread_exit)(retval);
}

#    if ASAN_INTERCEPT_TRYJOIN
INTERCEPTOR(int, pthread_tryjoin_np, void *thread, void **ret) {
  int result;
  asanThreadArgRetval().Join((uptr)thread, [&]() {
    result = REAL(pthread_tryjoin_np)(thread, ret);
    return !result;
  });
  return result;
}
#    endif

#    if ASAN_INTERCEPT_TIMEDJOIN
INTERCEPTOR(int, pthread_timedjoin_np, void *thread, void **ret,
            const struct timespec *abstime) {
  int result;
  asanThreadArgRetval().Join((uptr)thread, [&]() {
    result = REAL(pthread_timedjoin_np)(thread, ret, abstime);
    return !result;
  });
  return result;
}
#    endif

DEFINE_INTERNAL_PTHREAD_FUNCTIONS
#endif  // ASAN_INTERCEPT_PTHREAD_CREATE

#if ASAN_INTERCEPT_SWAPCONTEXT
static void ClearShadowMemoryForContextStack(uptr stack, uptr ssize) {
  // Only clear if we know the stack. This should be true only for contexts
  // created with makecontext().
  if (!ssize)
    return;
  // Align to page size.
  uptr PageSize = GetPageSizeCached();
  uptr bottom = RoundDownTo(stack, PageSize);
  if (!AddrIsInMem(bottom))
    return;
  ssize += stack - bottom;
  ssize = RoundUpTo(ssize, PageSize);
  PoisonShadow(bottom, ssize, 0);
}

// Since Solaris 10/SPARC, ucp->uc_stack.ss_sp refers to the stack base address
// as on other targets.  For binary compatibility, the new version uses a
// different external name, so we intercept that.
#    if SANITIZER_SOLARIS && defined(__sparc__)
INTERCEPTOR(void, __makecontext_v2, struct ucontext_t *ucp, void (*func)(),
            int argc, ...) {
#    else
INTERCEPTOR(void, makecontext, struct ucontext_t *ucp, void (*func)(), int argc,
            ...) {
#    endif
  va_list ap;
  uptr args[64];
  // We don't know a better way to forward ... into REAL function. We can
  // increase args size if neccecary.
  CHECK_LE(argc, ARRAY_SIZE(args));
  internal_memset(args, 0, sizeof(args));
  va_start(ap, argc);
  for (int i = 0; i < argc; ++i) args[i] = va_arg(ap, uptr);
  va_end(ap);

#    define ENUMERATE_ARRAY_4(start) \
      args[start], args[start + 1], args[start + 2], args[start + 3]
#    define ENUMERATE_ARRAY_16(start)                         \
      ENUMERATE_ARRAY_4(start), ENUMERATE_ARRAY_4(start + 4), \
          ENUMERATE_ARRAY_4(start + 8), ENUMERATE_ARRAY_4(start + 12)
#    define ENUMERATE_ARRAY_64()                                             \
      ENUMERATE_ARRAY_16(0), ENUMERATE_ARRAY_16(16), ENUMERATE_ARRAY_16(32), \
          ENUMERATE_ARRAY_16(48)

#    if SANITIZER_SOLARIS && defined(__sparc__)
  REAL(__makecontext_v2)
#    else
  REAL(makecontext)
#    endif
  ((struct ucontext_t *)ucp, func, argc, ENUMERATE_ARRAY_64());

#    undef ENUMERATE_ARRAY_4
#    undef ENUMERATE_ARRAY_16
#    undef ENUMERATE_ARRAY_64

  // Sign the stack so we can identify it for unpoisoning.
  SignContextStack(ucp);
}

INTERCEPTOR(int, swapcontext, struct ucontext_t *oucp,
            struct ucontext_t *ucp) {
  static bool reported_warning = false;
  if (!reported_warning) {
    Report("WARNING: ASan doesn't fully support makecontext/swapcontext "
           "functions and may produce false positives in some cases!\n");
    reported_warning = true;
  }
  // Clear shadow memory for new context (it may share stack
  // with current context).
  uptr stack, ssize;
  ReadContextStack(ucp, &stack, &ssize);
  ClearShadowMemoryForContextStack(stack, ssize);

#    if __has_attribute(__indirect_return__) && \
        (defined(__x86_64__) || defined(__i386__))
  int (*real_swapcontext)(struct ucontext_t *, struct ucontext_t *)
      __attribute__((__indirect_return__)) = REAL(swapcontext);
  int res = real_swapcontext(oucp, ucp);
#    else
  int res = REAL(swapcontext)(oucp, ucp);
#    endif
  // swapcontext technically does not return, but program may swap context to
  // "oucp" later, that would look as if swapcontext() returned 0.
  // We need to clear shadow for ucp once again, as it may be in arbitrary
  // state.
  ClearShadowMemoryForContextStack(stack, ssize);
  return res;
}
#endif  // ASAN_INTERCEPT_SWAPCONTEXT

#if SANITIZER_NETBSD
#define longjmp __longjmp14
#define siglongjmp __siglongjmp14
#endif

INTERCEPTOR(void, longjmp, void *env, int val) {
  __asan_handle_no_return();
  REAL(longjmp)(env, val);
}

#if ASAN_INTERCEPT__LONGJMP
INTERCEPTOR(void, _longjmp, void *env, int val) {
  __asan_handle_no_return();
  REAL(_longjmp)(env, val);
}
#endif

#if ASAN_INTERCEPT___LONGJMP_CHK
INTERCEPTOR(void, __longjmp_chk, void *env, int val) {
  __asan_handle_no_return();
  REAL(__longjmp_chk)(env, val);
}
#endif

#if ASAN_INTERCEPT_SIGLONGJMP
INTERCEPTOR(void, siglongjmp, void *env, int val) {
  __asan_handle_no_return();
  REAL(siglongjmp)(env, val);
}
#endif

#if ASAN_INTERCEPT___CXA_THROW
INTERCEPTOR(void, __cxa_throw, void *a, void *b, void *c) {
  CHECK(REAL(__cxa_throw));
  __asan_handle_no_return();
  REAL(__cxa_throw)(a, b, c);
}
#endif

#if ASAN_INTERCEPT___CXA_RETHROW_PRIMARY_EXCEPTION
INTERCEPTOR(void, __cxa_rethrow_primary_exception, void *a) {
  CHECK(REAL(__cxa_rethrow_primary_exception));
  __asan_handle_no_return();
  REAL(__cxa_rethrow_primary_exception)(a);
}
#endif

#if ASAN_INTERCEPT__UNWIND_RAISEEXCEPTION
INTERCEPTOR(_Unwind_Reason_Code, _Unwind_RaiseException,
            _Unwind_Exception *object) {
  CHECK(REAL(_Unwind_RaiseException));
  __asan_handle_no_return();
  return REAL(_Unwind_RaiseException)(object);
}
#endif

#if ASAN_INTERCEPT__SJLJ_UNWIND_RAISEEXCEPTION
INTERCEPTOR(_Unwind_Reason_Code, _Unwind_SjLj_RaiseException,
            _Unwind_Exception *object) {
  CHECK(REAL(_Unwind_SjLj_RaiseException));
  __asan_handle_no_return();
  return REAL(_Unwind_SjLj_RaiseException)(object);
}
#endif

#if ASAN_INTERCEPT_INDEX
# if ASAN_USE_ALIAS_ATTRIBUTE_FOR_INDEX
INTERCEPTOR(char*, index, const char *string, int c)
  ALIAS(WRAP(strchr));
# else
#  if SANITIZER_APPLE
DECLARE_REAL(char*, index, const char *string, int c)
OVERRIDE_FUNCTION(index, strchr);
#  else
DEFINE_REAL(char*, index, const char *string, int c)
#  endif
# endif
#endif  // ASAN_INTERCEPT_INDEX

// For both strcat() and strncat() we need to check the validity of |to|
// argument irrespective of the |from| length.
  INTERCEPTOR(char *, strcat, char *to, const char *from) {
    void *ctx;
    ASAN_INTERCEPTOR_ENTER(ctx, strcat);
    AsanInitFromRtl();
    if (flags()->replace_str) {
      uptr from_length = internal_strlen(from);
      ASAN_READ_RANGE(ctx, from, from_length + 1);
      uptr to_length = internal_strlen(to);
      ASAN_READ_STRING_OF_LEN(ctx, to, to_length, to_length);
      ASAN_WRITE_RANGE(ctx, to + to_length, from_length + 1);
      // If the copying actually happens, the |from| string should not overlap
      // with the resulting string starting at |to|, which has a length of
      // to_length + from_length + 1.
      if (from_length > 0) {
        CHECK_RANGES_OVERLAP("strcat", to, from_length + to_length + 1, from,
                             from_length + 1);
      }
    }
    return REAL(strcat)(to, from);
  }

INTERCEPTOR(char*, strncat, char *to, const char *from, uptr size) {
  void *ctx;
  ASAN_INTERCEPTOR_ENTER(ctx, strncat);
  AsanInitFromRtl();
  if (flags()->replace_str) {
    uptr from_length = MaybeRealStrnlen(from, size);
    uptr copy_length = Min(size, from_length + 1);
    ASAN_READ_RANGE(ctx, from, copy_length);
    uptr to_length = internal_strlen(to);
    ASAN_READ_STRING_OF_LEN(ctx, to, to_length, to_length);
    ASAN_WRITE_RANGE(ctx, to + to_length, from_length + 1);
    if (from_length > 0) {
      CHECK_RANGES_OVERLAP("strncat", to, to_length + copy_length + 1,
                           from, copy_length);
    }
  }
  return REAL(strncat)(to, from, size);
}

INTERCEPTOR(char *, strcpy, char *to, const char *from) {
  void *ctx;
  ASAN_INTERCEPTOR_ENTER(ctx, strcpy);
  if constexpr (SANITIZER_APPLE) {
    // strcpy is called from malloc_default_purgeable_zone()
    // in __asan::ReplaceSystemAlloc() on Mac.
    if (UNLIKELY(!AsanInited()))
      return REAL(strcpy)(to, from);
  } else {
    if (!TryAsanInitFromRtl())
      return REAL(strcpy)(to, from);
  }

  if (flags()->replace_str) {
    uptr from_size = internal_strlen(from) + 1;
    CHECK_RANGES_OVERLAP("strcpy", to, from_size, from, from_size);
    ASAN_READ_RANGE(ctx, from, from_size);
    ASAN_WRITE_RANGE(ctx, to, from_size);
  }
  return REAL(strcpy)(to, from);
}

// Windows doesn't always define the strdup identifier,
// and when it does it's a macro defined to either _strdup
// or _strdup_dbg, _strdup_dbg ends up calling _strdup, so
// we want to intercept that. push/pop_macro are used to avoid problems
// if this file ends up including <string.h> in the future.
#  if SANITIZER_WINDOWS
#    pragma push_macro("strdup")
#    undef strdup
#    define strdup _strdup
#  endif

INTERCEPTOR(char*, strdup, const char *s) {
  void *ctx;
  ASAN_INTERCEPTOR_ENTER(ctx, strdup);
  if (UNLIKELY(!TryAsanInitFromRtl()))
    return internal_strdup(s);
  uptr length = internal_strlen(s);
  if (flags()->replace_str) {
    ASAN_READ_RANGE(ctx, s, length + 1);
  }
  GET_STACK_TRACE_MALLOC;
  void *new_mem = asan_malloc(length + 1, &stack);
  if (new_mem) {
    REAL(memcpy)(new_mem, s, length + 1);
  }
  return reinterpret_cast<char*>(new_mem);
}

#  if ASAN_INTERCEPT___STRDUP
INTERCEPTOR(char*, __strdup, const char *s) {
  void *ctx;
  ASAN_INTERCEPTOR_ENTER(ctx, strdup);
  if (UNLIKELY(!TryAsanInitFromRtl()))
    return internal_strdup(s);
  uptr length = internal_strlen(s);
  if (flags()->replace_str) {
    ASAN_READ_RANGE(ctx, s, length + 1);
  }
  GET_STACK_TRACE_MALLOC;
  void *new_mem = asan_malloc(length + 1, &stack);
  if (new_mem) {
    REAL(memcpy)(new_mem, s, length + 1);
  }
  return reinterpret_cast<char*>(new_mem);
}
#endif // ASAN_INTERCEPT___STRDUP

INTERCEPTOR(char*, strncpy, char *to, const char *from, uptr size) {
  void *ctx;
  ASAN_INTERCEPTOR_ENTER(ctx, strncpy);
  AsanInitFromRtl();
  if (flags()->replace_str) {
    uptr from_size = Min(size, MaybeRealStrnlen(from, size) + 1);
    CHECK_RANGES_OVERLAP("strncpy", to, from_size, from, from_size);
    ASAN_READ_RANGE(ctx, from, from_size);
    ASAN_WRITE_RANGE(ctx, to, size);
  }
  return REAL(strncpy)(to, from, size);
}

template <typename Fn>
static ALWAYS_INLINE auto StrtolImpl(void *ctx, Fn real, const char *nptr,
                                     char **endptr, int base)
    -> decltype(real(nullptr, nullptr, 0)) {
  if (!flags()->replace_str)
    return real(nptr, endptr, base);
  char *real_endptr;
  auto res = real(nptr, &real_endptr, base);
  StrtolFixAndCheck(ctx, nptr, endptr, real_endptr, base);
  return res;
}

#  define INTERCEPTOR_STRTO_BASE(ret_type, func)                             \
    INTERCEPTOR(ret_type, func, const char *nptr, char **endptr, int base) { \
      void *ctx;                                                             \
      ASAN_INTERCEPTOR_ENTER(ctx, func);                                     \
      AsanInitFromRtl();                                                     \
      return StrtolImpl(ctx, REAL(func), nptr, endptr, base);                \
    }

INTERCEPTOR_STRTO_BASE(long, strtol)
INTERCEPTOR_STRTO_BASE(long long, strtoll)

#  if SANITIZER_GLIBC
INTERCEPTOR_STRTO_BASE(long, __isoc23_strtol)
INTERCEPTOR_STRTO_BASE(long long, __isoc23_strtoll)
#  endif

INTERCEPTOR(int, atoi, const char *nptr) {
  void *ctx;
  ASAN_INTERCEPTOR_ENTER(ctx, atoi);
  if (SANITIZER_APPLE && UNLIKELY(!AsanInited()))
    return REAL(atoi)(nptr);
  AsanInitFromRtl();
  if (!flags()->replace_str) {
    return REAL(atoi)(nptr);
  }
  char *real_endptr;
  // "man atoi" tells that behavior of atoi(nptr) is the same as
  // strtol(nptr, 0, 10), i.e. it sets errno to ERANGE if the
  // parsed integer can't be stored in *long* type (even if it's
  // different from int). So, we just imitate this behavior.
  int result = REAL(strtol)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  ASAN_READ_STRING(ctx, nptr, (real_endptr - nptr) + 1);
  return result;
}

INTERCEPTOR(long, atol, const char *nptr) {
  void *ctx;
  ASAN_INTERCEPTOR_ENTER(ctx, atol);
  if (SANITIZER_APPLE && UNLIKELY(!AsanInited()))
    return REAL(atol)(nptr);
  AsanInitFromRtl();
  if (!flags()->replace_str) {
    return REAL(atol)(nptr);
  }
  char *real_endptr;
  long result = REAL(strtol)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  ASAN_READ_STRING(ctx, nptr, (real_endptr - nptr) + 1);
  return result;
}

INTERCEPTOR(long long, atoll, const char *nptr) {
  void *ctx;
  ASAN_INTERCEPTOR_ENTER(ctx, atoll);
  AsanInitFromRtl();
  if (!flags()->replace_str) {
    return REAL(atoll)(nptr);
  }
  char *real_endptr;
  long long result = REAL(strtoll)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  ASAN_READ_STRING(ctx, nptr, (real_endptr - nptr) + 1);
  return result;
}

#if ASAN_INTERCEPT___CXA_ATEXIT || ASAN_INTERCEPT_ATEXIT
static void AtCxaAtexit(void *unused) {
  (void)unused;
  StopInitOrderChecking();
}
#endif

#if ASAN_INTERCEPT___CXA_ATEXIT
INTERCEPTOR(int, __cxa_atexit, void (*func)(void *), void *arg,
            void *dso_handle) {
  if (SANITIZER_APPLE && UNLIKELY(!AsanInited()))
    return REAL(__cxa_atexit)(func, arg, dso_handle);
  AsanInitFromRtl();
#    if CAN_SANITIZE_LEAKS
  __lsan::ScopedInterceptorDisabler disabler;
#endif
  int res = REAL(__cxa_atexit)(func, arg, dso_handle);
  REAL(__cxa_atexit)(AtCxaAtexit, nullptr, nullptr);
  return res;
}
#endif  // ASAN_INTERCEPT___CXA_ATEXIT

#if ASAN_INTERCEPT_ATEXIT
INTERCEPTOR(int, atexit, void (*func)()) {
  AsanInitFromRtl();
#    if CAN_SANITIZE_LEAKS
  __lsan::ScopedInterceptorDisabler disabler;
#endif
  // Avoid calling real atexit as it is unreachable on at least on Linux.
  int res = REAL(__cxa_atexit)((void (*)(void *a))func, nullptr, nullptr);
  REAL(__cxa_atexit)(AtCxaAtexit, nullptr, nullptr);
  return res;
}
#endif

#if ASAN_INTERCEPT_PTHREAD_ATFORK
extern "C" {
extern int _pthread_atfork(void (*prepare)(), void (*parent)(),
                           void (*child)());
}

INTERCEPTOR(int, pthread_atfork, void (*prepare)(), void (*parent)(),
            void (*child)()) {
#if CAN_SANITIZE_LEAKS
  __lsan::ScopedInterceptorDisabler disabler;
#endif
  // REAL(pthread_atfork) cannot be called due to symbol indirections at least
  // on NetBSD
  return _pthread_atfork(prepare, parent, child);
}
#endif

#if ASAN_INTERCEPT_VFORK
DEFINE_REAL(int, vfork,)
DECLARE_EXTERN_INTERCEPTOR_AND_WRAPPER(int, vfork,)
#endif

// ---------------------- InitializeAsanInterceptors ---------------- {{{1
namespace __asan {
void InitializeAsanInterceptors() {
  static bool was_called_once;
  CHECK(!was_called_once);
  was_called_once = true;
  InitializePlatformInterceptors();
  InitializeCommonInterceptors();
  InitializeSignalInterceptors();

  // Intercept str* functions.
  ASAN_INTERCEPT_FUNC(strcat);
  ASAN_INTERCEPT_FUNC(strcpy);
  ASAN_INTERCEPT_FUNC(strncat);
  ASAN_INTERCEPT_FUNC(strncpy);
  ASAN_INTERCEPT_FUNC(strdup);
#  if ASAN_INTERCEPT___STRDUP
  ASAN_INTERCEPT_FUNC(__strdup);
#endif
#if ASAN_INTERCEPT_INDEX && ASAN_USE_ALIAS_ATTRIBUTE_FOR_INDEX
  ASAN_INTERCEPT_FUNC(index);
#endif

  ASAN_INTERCEPT_FUNC(atoi);
  ASAN_INTERCEPT_FUNC(atol);
  ASAN_INTERCEPT_FUNC(atoll);
  ASAN_INTERCEPT_FUNC(strtol);
  ASAN_INTERCEPT_FUNC(strtoll);
#  if SANITIZER_GLIBC
  ASAN_INTERCEPT_FUNC(__isoc23_strtol);
  ASAN_INTERCEPT_FUNC(__isoc23_strtoll);
#  endif

  // Intecept jump-related functions.
  ASAN_INTERCEPT_FUNC(longjmp);

#  if ASAN_INTERCEPT_SWAPCONTEXT
  ASAN_INTERCEPT_FUNC(swapcontext);
  // See the makecontext interceptor above for an explanation.
#    if SANITIZER_SOLARIS && defined(__sparc__)
  ASAN_INTERCEPT_FUNC(__makecontext_v2);
#    else
  ASAN_INTERCEPT_FUNC(makecontext);
#    endif
#  endif
#  if ASAN_INTERCEPT__LONGJMP
  ASAN_INTERCEPT_FUNC(_longjmp);
#endif
#if ASAN_INTERCEPT___LONGJMP_CHK
  ASAN_INTERCEPT_FUNC(__longjmp_chk);
#endif
#if ASAN_INTERCEPT_SIGLONGJMP
  ASAN_INTERCEPT_FUNC(siglongjmp);
#endif

  // Intercept exception handling functions.
#if ASAN_INTERCEPT___CXA_THROW
  ASAN_INTERCEPT_FUNC(__cxa_throw);
#endif
#if ASAN_INTERCEPT___CXA_RETHROW_PRIMARY_EXCEPTION
  ASAN_INTERCEPT_FUNC(__cxa_rethrow_primary_exception);
#endif
  // Indirectly intercept std::rethrow_exception.
#if ASAN_INTERCEPT__UNWIND_RAISEEXCEPTION
  ASAN_INTERCEPT_FUNC(_Unwind_RaiseException);
#endif
  // Indirectly intercept std::rethrow_exception.
#if ASAN_INTERCEPT__UNWIND_SJLJ_RAISEEXCEPTION
  ASAN_INTERCEPT_FUNC(_Unwind_SjLj_RaiseException);
#endif

  // Intercept threading-related functions
#if ASAN_INTERCEPT_PTHREAD_CREATE
// TODO: this should probably have an unversioned fallback for newer arches?
#if defined(ASAN_PTHREAD_CREATE_VERSION)
  ASAN_INTERCEPT_FUNC_VER(pthread_create, ASAN_PTHREAD_CREATE_VERSION);
#else
  ASAN_INTERCEPT_FUNC(pthread_create);
#endif
  ASAN_INTERCEPT_FUNC(pthread_join);
  ASAN_INTERCEPT_FUNC(pthread_detach);
  ASAN_INTERCEPT_FUNC(pthread_exit);
#  endif

#  if ASAN_INTERCEPT_TIMEDJOIN
  ASAN_INTERCEPT_FUNC(pthread_timedjoin_np);
#endif

#if ASAN_INTERCEPT_TRYJOIN
  ASAN_INTERCEPT_FUNC(pthread_tryjoin_np);
#endif

  // Intercept atexit function.
#if ASAN_INTERCEPT___CXA_ATEXIT
  ASAN_INTERCEPT_FUNC(__cxa_atexit);
#endif

#if ASAN_INTERCEPT_ATEXIT
  ASAN_INTERCEPT_FUNC(atexit);
#endif

#if ASAN_INTERCEPT_PTHREAD_ATFORK
  ASAN_INTERCEPT_FUNC(pthread_atfork);
#endif

#if ASAN_INTERCEPT_VFORK
  ASAN_INTERCEPT_FUNC(vfork);
#endif

  VReport(1, "AddressSanitizer: libc interceptors initialized\n");
}

#  if SANITIZER_WINDOWS
#    pragma pop_macro("strdup")
#  endif

} // namespace __asan

#endif  // !SANITIZER_FUCHSIA
