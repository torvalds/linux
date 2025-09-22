//=-- lsan_interceptors.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Interceptors for standalone LSan.
//
//===----------------------------------------------------------------------===//

#include "interception/interception.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_dlsym.h"
#include "sanitizer_common/sanitizer_allocator_report.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_linux.h"
#include "sanitizer_common/sanitizer_platform_interceptors.h"
#include "sanitizer_common/sanitizer_platform_limits_netbsd.h"
#include "sanitizer_common/sanitizer_platform_limits_posix.h"
#if SANITIZER_POSIX
#include "sanitizer_common/sanitizer_posix.h"
#endif
#include "sanitizer_common/sanitizer_tls_get_addr.h"
#include "lsan.h"
#include "lsan_allocator.h"
#include "lsan_common.h"
#include "lsan_thread.h"

#include <stddef.h>

using namespace __lsan;

extern "C" {
int pthread_attr_init(void *attr);
int pthread_attr_destroy(void *attr);
int pthread_attr_getdetachstate(void *attr, int *v);
int pthread_key_create(unsigned *key, void (*destructor)(void* v));
int pthread_setspecific(unsigned key, const void *v);
}

struct DlsymAlloc : DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return lsan_init_is_running; }
  static void OnAllocate(const void *ptr, uptr size) {
#if CAN_SANITIZE_LEAKS
    // Suppress leaks from dlerror(). Previously dlsym hack on global array was
    // used by leak sanitizer as a root region.
    __lsan_register_root_region(ptr, size);
#endif
  }
  static void OnFree(const void *ptr, uptr size) {
#if CAN_SANITIZE_LEAKS
    __lsan_unregister_root_region(ptr, size);
#endif
  }
};

///// Malloc/free interceptors. /////

namespace std {
  struct nothrow_t;
  enum class align_val_t: size_t;
}

#if !SANITIZER_APPLE
INTERCEPTOR(void*, malloc, uptr size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);
  ENSURE_LSAN_INITED;
  GET_STACK_TRACE_MALLOC;
  return lsan_malloc(size, stack);
}

INTERCEPTOR(void, free, void *p) {
  if (UNLIKELY(!p))
    return;
  if (DlsymAlloc::PointerIsMine(p))
    return DlsymAlloc::Free(p);
  ENSURE_LSAN_INITED;
  lsan_free(p);
}

INTERCEPTOR(void*, calloc, uptr nmemb, uptr size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Callocate(nmemb, size);
  ENSURE_LSAN_INITED;
  GET_STACK_TRACE_MALLOC;
  return lsan_calloc(nmemb, size, stack);
}

INTERCEPTOR(void *, realloc, void *ptr, uptr size) {
  if (DlsymAlloc::Use() || DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Realloc(ptr, size);
  ENSURE_LSAN_INITED;
  GET_STACK_TRACE_MALLOC;
  return lsan_realloc(ptr, size, stack);
}

INTERCEPTOR(void*, reallocarray, void *q, uptr nmemb, uptr size) {
  ENSURE_LSAN_INITED;
  GET_STACK_TRACE_MALLOC;
  return lsan_reallocarray(q, nmemb, size, stack);
}

INTERCEPTOR(int, posix_memalign, void **memptr, uptr alignment, uptr size) {
  ENSURE_LSAN_INITED;
  GET_STACK_TRACE_MALLOC;
  return lsan_posix_memalign(memptr, alignment, size, stack);
}

INTERCEPTOR(void*, valloc, uptr size) {
  ENSURE_LSAN_INITED;
  GET_STACK_TRACE_MALLOC;
  return lsan_valloc(size, stack);
}
#endif  // !SANITIZER_APPLE

#if SANITIZER_INTERCEPT_MEMALIGN
INTERCEPTOR(void*, memalign, uptr alignment, uptr size) {
  ENSURE_LSAN_INITED;
  GET_STACK_TRACE_MALLOC;
  return lsan_memalign(alignment, size, stack);
}
#define LSAN_MAYBE_INTERCEPT_MEMALIGN INTERCEPT_FUNCTION(memalign)
#else
#define LSAN_MAYBE_INTERCEPT_MEMALIGN
#endif  // SANITIZER_INTERCEPT_MEMALIGN

#if SANITIZER_INTERCEPT___LIBC_MEMALIGN
INTERCEPTOR(void *, __libc_memalign, uptr alignment, uptr size) {
  ENSURE_LSAN_INITED;
  GET_STACK_TRACE_MALLOC;
  void *res = lsan_memalign(alignment, size, stack);
  DTLS_on_libc_memalign(res, size);
  return res;
}
#define LSAN_MAYBE_INTERCEPT___LIBC_MEMALIGN INTERCEPT_FUNCTION(__libc_memalign)
#else
#define LSAN_MAYBE_INTERCEPT___LIBC_MEMALIGN
#endif  // SANITIZER_INTERCEPT___LIBC_MEMALIGN

#if SANITIZER_INTERCEPT_ALIGNED_ALLOC
INTERCEPTOR(void*, aligned_alloc, uptr alignment, uptr size) {
  ENSURE_LSAN_INITED;
  GET_STACK_TRACE_MALLOC;
  return lsan_aligned_alloc(alignment, size, stack);
}
#define LSAN_MAYBE_INTERCEPT_ALIGNED_ALLOC INTERCEPT_FUNCTION(aligned_alloc)
#else
#define LSAN_MAYBE_INTERCEPT_ALIGNED_ALLOC
#endif

#if SANITIZER_INTERCEPT_MALLOC_USABLE_SIZE
INTERCEPTOR(uptr, malloc_usable_size, void *ptr) {
  ENSURE_LSAN_INITED;
  return GetMallocUsableSize(ptr);
}
#define LSAN_MAYBE_INTERCEPT_MALLOC_USABLE_SIZE \
        INTERCEPT_FUNCTION(malloc_usable_size)
#else
#define LSAN_MAYBE_INTERCEPT_MALLOC_USABLE_SIZE
#endif

#if SANITIZER_INTERCEPT_MALLOPT_AND_MALLINFO
struct fake_mallinfo {
  int x[10];
};

INTERCEPTOR(struct fake_mallinfo, mallinfo, void) {
  struct fake_mallinfo res;
  internal_memset(&res, 0, sizeof(res));
  return res;
}
#define LSAN_MAYBE_INTERCEPT_MALLINFO INTERCEPT_FUNCTION(mallinfo)

INTERCEPTOR(int, mallopt, int cmd, int value) {
  return 0;
}
#define LSAN_MAYBE_INTERCEPT_MALLOPT INTERCEPT_FUNCTION(mallopt)
#else
#define LSAN_MAYBE_INTERCEPT_MALLINFO
#define LSAN_MAYBE_INTERCEPT_MALLOPT
#endif // SANITIZER_INTERCEPT_MALLOPT_AND_MALLINFO

#if SANITIZER_INTERCEPT_PVALLOC
INTERCEPTOR(void*, pvalloc, uptr size) {
  ENSURE_LSAN_INITED;
  GET_STACK_TRACE_MALLOC;
  return lsan_pvalloc(size, stack);
}
#define LSAN_MAYBE_INTERCEPT_PVALLOC INTERCEPT_FUNCTION(pvalloc)
#else
#define LSAN_MAYBE_INTERCEPT_PVALLOC
#endif // SANITIZER_INTERCEPT_PVALLOC

#if SANITIZER_INTERCEPT_CFREE
INTERCEPTOR(void, cfree, void *p) ALIAS(WRAP(free));
#define LSAN_MAYBE_INTERCEPT_CFREE INTERCEPT_FUNCTION(cfree)
#else
#define LSAN_MAYBE_INTERCEPT_CFREE
#endif // SANITIZER_INTERCEPT_CFREE

#if SANITIZER_INTERCEPT_MCHECK_MPROBE
INTERCEPTOR(int, mcheck, void (*abortfunc)(int mstatus)) {
  return 0;
}

INTERCEPTOR(int, mcheck_pedantic, void (*abortfunc)(int mstatus)) {
  return 0;
}

INTERCEPTOR(int, mprobe, void *ptr) {
  return 0;
}
#endif // SANITIZER_INTERCEPT_MCHECK_MPROBE


// TODO(alekseys): throw std::bad_alloc instead of dying on OOM.
#define OPERATOR_NEW_BODY(nothrow)\
  ENSURE_LSAN_INITED;\
  GET_STACK_TRACE_MALLOC;\
  void *res = lsan_malloc(size, stack);\
  if (!nothrow && UNLIKELY(!res)) ReportOutOfMemory(size, &stack);\
  return res;
#define OPERATOR_NEW_BODY_ALIGN(nothrow)\
  ENSURE_LSAN_INITED;\
  GET_STACK_TRACE_MALLOC;\
  void *res = lsan_memalign((uptr)align, size, stack);\
  if (!nothrow && UNLIKELY(!res)) ReportOutOfMemory(size, &stack);\
  return res;

#define OPERATOR_DELETE_BODY\
  ENSURE_LSAN_INITED;\
  lsan_free(ptr);

// On OS X it's not enough to just provide our own 'operator new' and
// 'operator delete' implementations, because they're going to be in the runtime
// dylib, and the main executable will depend on both the runtime dylib and
// libstdc++, each of has its implementation of new and delete.
// To make sure that C++ allocation/deallocation operators are overridden on
// OS X we need to intercept them using their mangled names.
#if !SANITIZER_APPLE

INTERCEPTOR_ATTRIBUTE
void *operator new(size_t size) { OPERATOR_NEW_BODY(false /*nothrow*/); }
INTERCEPTOR_ATTRIBUTE
void *operator new[](size_t size) { OPERATOR_NEW_BODY(false /*nothrow*/); }
INTERCEPTOR_ATTRIBUTE
void *operator new(size_t size, std::nothrow_t const&)
{ OPERATOR_NEW_BODY(true /*nothrow*/); }
INTERCEPTOR_ATTRIBUTE
void *operator new[](size_t size, std::nothrow_t const&)
{ OPERATOR_NEW_BODY(true /*nothrow*/); }
INTERCEPTOR_ATTRIBUTE
void *operator new(size_t size, std::align_val_t align)
{ OPERATOR_NEW_BODY_ALIGN(false /*nothrow*/); }
INTERCEPTOR_ATTRIBUTE
void *operator new[](size_t size, std::align_val_t align)
{ OPERATOR_NEW_BODY_ALIGN(false /*nothrow*/); }
INTERCEPTOR_ATTRIBUTE
void *operator new(size_t size, std::align_val_t align, std::nothrow_t const&)
{ OPERATOR_NEW_BODY_ALIGN(true /*nothrow*/); }
INTERCEPTOR_ATTRIBUTE
void *operator new[](size_t size, std::align_val_t align, std::nothrow_t const&)
{ OPERATOR_NEW_BODY_ALIGN(true /*nothrow*/); }

INTERCEPTOR_ATTRIBUTE
void operator delete(void *ptr) NOEXCEPT { OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete[](void *ptr) NOEXCEPT { OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete(void *ptr, std::nothrow_t const&) { OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete[](void *ptr, std::nothrow_t const &)
{ OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete(void *ptr, size_t size) NOEXCEPT
{ OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete[](void *ptr, size_t size) NOEXCEPT
{ OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete(void *ptr, std::align_val_t) NOEXCEPT
{ OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete[](void *ptr, std::align_val_t) NOEXCEPT
{ OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete(void *ptr, std::align_val_t, std::nothrow_t const&)
{ OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete[](void *ptr, std::align_val_t, std::nothrow_t const&)
{ OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete(void *ptr, size_t size, std::align_val_t) NOEXCEPT
{ OPERATOR_DELETE_BODY; }
INTERCEPTOR_ATTRIBUTE
void operator delete[](void *ptr, size_t size, std::align_val_t) NOEXCEPT
{ OPERATOR_DELETE_BODY; }

#else  // SANITIZER_APPLE

INTERCEPTOR(void *, _Znwm, size_t size)
{ OPERATOR_NEW_BODY(false /*nothrow*/); }
INTERCEPTOR(void *, _Znam, size_t size)
{ OPERATOR_NEW_BODY(false /*nothrow*/); }
INTERCEPTOR(void *, _ZnwmRKSt9nothrow_t, size_t size, std::nothrow_t const&)
{ OPERATOR_NEW_BODY(true /*nothrow*/); }
INTERCEPTOR(void *, _ZnamRKSt9nothrow_t, size_t size, std::nothrow_t const&)
{ OPERATOR_NEW_BODY(true /*nothrow*/); }

INTERCEPTOR(void, _ZdlPv, void *ptr)
{ OPERATOR_DELETE_BODY; }
INTERCEPTOR(void, _ZdaPv, void *ptr)
{ OPERATOR_DELETE_BODY; }
INTERCEPTOR(void, _ZdlPvRKSt9nothrow_t, void *ptr, std::nothrow_t const&)
{ OPERATOR_DELETE_BODY; }
INTERCEPTOR(void, _ZdaPvRKSt9nothrow_t, void *ptr, std::nothrow_t const&)
{ OPERATOR_DELETE_BODY; }

#endif  // !SANITIZER_APPLE


///// Thread initialization and finalization. /////

#if !SANITIZER_NETBSD && !SANITIZER_FREEBSD && !SANITIZER_FUCHSIA
static unsigned g_thread_finalize_key;

static void thread_finalize(void *v) {
  uptr iter = (uptr)v;
  if (iter > 1) {
    if (pthread_setspecific(g_thread_finalize_key, (void*)(iter - 1))) {
      Report("LeakSanitizer: failed to set thread key.\n");
      Die();
    }
    return;
  }
  ThreadFinish();
}
#endif

#if SANITIZER_NETBSD
INTERCEPTOR(void, _lwp_exit) {
  ENSURE_LSAN_INITED;
  ThreadFinish();
  REAL(_lwp_exit)();
}
#define LSAN_MAYBE_INTERCEPT__LWP_EXIT INTERCEPT_FUNCTION(_lwp_exit)
#else
#define LSAN_MAYBE_INTERCEPT__LWP_EXIT
#endif

#if SANITIZER_INTERCEPT_THR_EXIT
INTERCEPTOR(void, thr_exit, tid_t *state) {
  ENSURE_LSAN_INITED;
  ThreadFinish();
  REAL(thr_exit)(state);
}
#define LSAN_MAYBE_INTERCEPT_THR_EXIT INTERCEPT_FUNCTION(thr_exit)
#else
#define LSAN_MAYBE_INTERCEPT_THR_EXIT
#endif

#if SANITIZER_INTERCEPT___CXA_ATEXIT
INTERCEPTOR(int, __cxa_atexit, void (*func)(void *), void *arg,
            void *dso_handle) {
  __lsan::ScopedInterceptorDisabler disabler;
  return REAL(__cxa_atexit)(func, arg, dso_handle);
}
#define LSAN_MAYBE_INTERCEPT___CXA_ATEXIT INTERCEPT_FUNCTION(__cxa_atexit)
#else
#define LSAN_MAYBE_INTERCEPT___CXA_ATEXIT
#endif

#if SANITIZER_INTERCEPT_ATEXIT
INTERCEPTOR(int, atexit, void (*f)()) {
  __lsan::ScopedInterceptorDisabler disabler;
  return REAL(__cxa_atexit)((void (*)(void *a))f, 0, 0);
}
#define LSAN_MAYBE_INTERCEPT_ATEXIT INTERCEPT_FUNCTION(atexit)
#else
#define LSAN_MAYBE_INTERCEPT_ATEXIT
#endif

#if SANITIZER_INTERCEPT_PTHREAD_ATFORK
extern "C" {
extern int _pthread_atfork(void (*prepare)(), void (*parent)(),
                           void (*child)());
}

INTERCEPTOR(int, pthread_atfork, void (*prepare)(), void (*parent)(),
            void (*child)()) {
  __lsan::ScopedInterceptorDisabler disabler;
  // REAL(pthread_atfork) cannot be called due to symbol indirections at least
  // on NetBSD
  return _pthread_atfork(prepare, parent, child);
}
#define LSAN_MAYBE_INTERCEPT_PTHREAD_ATFORK INTERCEPT_FUNCTION(pthread_atfork)
#else
#define LSAN_MAYBE_INTERCEPT_PTHREAD_ATFORK
#endif

#if SANITIZER_INTERCEPT_STRERROR
INTERCEPTOR(char *, strerror, int errnum) {
  __lsan::ScopedInterceptorDisabler disabler;
  return REAL(strerror)(errnum);
}
#define LSAN_MAYBE_INTERCEPT_STRERROR INTERCEPT_FUNCTION(strerror)
#else
#define LSAN_MAYBE_INTERCEPT_STRERROR
#endif

#if SANITIZER_POSIX

template <bool Detached>
static void *ThreadStartFunc(void *arg) {
  u32 parent_tid = (uptr)arg;
  uptr tid = ThreadCreate(parent_tid, Detached);
  // Wait until the last iteration to maximize the chance that we are the last
  // destructor to run.
#if !SANITIZER_NETBSD && !SANITIZER_FREEBSD
  if (pthread_setspecific(g_thread_finalize_key,
                          (void*)GetPthreadDestructorIterations())) {
    Report("LeakSanitizer: failed to set thread key.\n");
    Die();
  }
#  endif
  ThreadStart(tid, GetTid());
  auto self = GetThreadSelf();
  auto args = GetThreadArgRetval().GetArgs(self);
  void *retval = (*args.routine)(args.arg_retval);
  GetThreadArgRetval().Finish(self, retval);
  return retval;
}

INTERCEPTOR(int, pthread_create, void *th, void *attr,
            void *(*callback)(void *), void *param) {
  ENSURE_LSAN_INITED;
  EnsureMainThreadIDIsCorrect();

  bool detached = [attr]() {
    int d = 0;
    return attr && !pthread_attr_getdetachstate(attr, &d) && IsStateDetached(d);
  }();

  __sanitizer_pthread_attr_t myattr;
  if (!attr) {
    pthread_attr_init(&myattr);
    attr = &myattr;
  }
  AdjustStackSize(attr);
  uptr this_tid = GetCurrentThreadId();
  int result;
  {
    // Ignore all allocations made by pthread_create: thread stack/TLS may be
    // stored by pthread for future reuse even after thread destruction, and
    // the linked list it's stored in doesn't even hold valid pointers to the
    // objects, the latter are calculated by obscure pointer arithmetic.
    ScopedInterceptorDisabler disabler;
    GetThreadArgRetval().Create(detached, {callback, param}, [&]() -> uptr {
      result = REAL(pthread_create)(
          th, attr, detached ? ThreadStartFunc<true> : ThreadStartFunc<false>,
          (void *)this_tid);
      return result ? 0 : *(uptr *)(th);
    });
  }
  if (attr == &myattr)
    pthread_attr_destroy(&myattr);
  return result;
}

INTERCEPTOR(int, pthread_join, void *thread, void **retval) {
  int result;
  GetThreadArgRetval().Join((uptr)thread, [&]() {
    result = REAL(pthread_join)(thread, retval);
    return !result;
  });
  return result;
}

INTERCEPTOR(int, pthread_detach, void *thread) {
  int result;
  GetThreadArgRetval().Detach((uptr)thread, [&]() {
    result = REAL(pthread_detach)(thread);
    return !result;
  });
  return result;
}

INTERCEPTOR(void, pthread_exit, void *retval) {
  GetThreadArgRetval().Finish(GetThreadSelf(), retval);
  REAL(pthread_exit)(retval);
}

#  if SANITIZER_INTERCEPT_TRYJOIN
INTERCEPTOR(int, pthread_tryjoin_np, void *thread, void **ret) {
  int result;
  GetThreadArgRetval().Join((uptr)thread, [&]() {
    result = REAL(pthread_tryjoin_np)(thread, ret);
    return !result;
  });
  return result;
}
#    define LSAN_MAYBE_INTERCEPT_TRYJOIN INTERCEPT_FUNCTION(pthread_tryjoin_np)
#  else
#    define LSAN_MAYBE_INTERCEPT_TRYJOIN
#  endif  // SANITIZER_INTERCEPT_TRYJOIN

#  if SANITIZER_INTERCEPT_TIMEDJOIN
INTERCEPTOR(int, pthread_timedjoin_np, void *thread, void **ret,
            const struct timespec *abstime) {
  int result;
  GetThreadArgRetval().Join((uptr)thread, [&]() {
    result = REAL(pthread_timedjoin_np)(thread, ret, abstime);
    return !result;
  });
  return result;
}
#    define LSAN_MAYBE_INTERCEPT_TIMEDJOIN \
      INTERCEPT_FUNCTION(pthread_timedjoin_np)
#  else
#    define LSAN_MAYBE_INTERCEPT_TIMEDJOIN
#  endif  // SANITIZER_INTERCEPT_TIMEDJOIN

DEFINE_INTERNAL_PTHREAD_FUNCTIONS

INTERCEPTOR(void, _exit, int status) {
  if (status == 0 && HasReportedLeaks()) status = common_flags()->exitcode;
  REAL(_exit)(status);
}

#define COMMON_INTERCEPT_FUNCTION(name) INTERCEPT_FUNCTION(name)
#define SIGNAL_INTERCEPTOR_ENTER() ENSURE_LSAN_INITED
#include "sanitizer_common/sanitizer_signal_interceptors.inc"

#endif  // SANITIZER_POSIX

namespace __lsan {

void InitializeInterceptors() {
  // Fuchsia doesn't use interceptors that require any setup.
#if !SANITIZER_FUCHSIA
  __interception::DoesNotSupportStaticLinking();
  InitializeSignalInterceptors();

  INTERCEPT_FUNCTION(malloc);
  INTERCEPT_FUNCTION(free);
  LSAN_MAYBE_INTERCEPT_CFREE;
  INTERCEPT_FUNCTION(calloc);
  INTERCEPT_FUNCTION(realloc);
  LSAN_MAYBE_INTERCEPT_MEMALIGN;
  LSAN_MAYBE_INTERCEPT___LIBC_MEMALIGN;
  LSAN_MAYBE_INTERCEPT_ALIGNED_ALLOC;
  INTERCEPT_FUNCTION(posix_memalign);
  INTERCEPT_FUNCTION(valloc);
  LSAN_MAYBE_INTERCEPT_PVALLOC;
  LSAN_MAYBE_INTERCEPT_MALLOC_USABLE_SIZE;
  LSAN_MAYBE_INTERCEPT_MALLINFO;
  LSAN_MAYBE_INTERCEPT_MALLOPT;
  INTERCEPT_FUNCTION(pthread_create);
  INTERCEPT_FUNCTION(pthread_join);
  INTERCEPT_FUNCTION(pthread_detach);
  INTERCEPT_FUNCTION(pthread_exit);
  LSAN_MAYBE_INTERCEPT_TIMEDJOIN;
  LSAN_MAYBE_INTERCEPT_TRYJOIN;
  INTERCEPT_FUNCTION(_exit);

  LSAN_MAYBE_INTERCEPT__LWP_EXIT;
  LSAN_MAYBE_INTERCEPT_THR_EXIT;

  LSAN_MAYBE_INTERCEPT___CXA_ATEXIT;
  LSAN_MAYBE_INTERCEPT_ATEXIT;
  LSAN_MAYBE_INTERCEPT_PTHREAD_ATFORK;

  LSAN_MAYBE_INTERCEPT_STRERROR;

#if !SANITIZER_NETBSD && !SANITIZER_FREEBSD
  if (pthread_key_create(&g_thread_finalize_key, &thread_finalize)) {
    Report("LeakSanitizer: failed to create thread key.\n");
    Die();
  }
#endif

#endif  // !SANITIZER_FUCHSIA
}

} // namespace __lsan
