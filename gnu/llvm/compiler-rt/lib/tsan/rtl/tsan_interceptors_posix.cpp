//===-- tsan_interceptors_posix.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// FIXME: move as many interceptors as possible into
// sanitizer_common/sanitizer_common_interceptors.inc
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_glibc_version.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_linux.h"
#include "sanitizer_common/sanitizer_platform_limits_netbsd.h"
#include "sanitizer_common/sanitizer_platform_limits_posix.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_posix.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"
#include "interception/interception.h"
#include "tsan_interceptors.h"
#include "tsan_interface.h"
#include "tsan_platform.h"
#include "tsan_suppressions.h"
#include "tsan_rtl.h"
#include "tsan_mman.h"
#include "tsan_fd.h"

#include <stdarg.h>

using namespace __tsan;

DECLARE_REAL(void *, memcpy, void *to, const void *from, SIZE_T size)
DECLARE_REAL(void *, memset, void *block, int c, SIZE_T size)

#if SANITIZER_FREEBSD || SANITIZER_APPLE
#define stdout __stdoutp
#define stderr __stderrp
#endif

#if SANITIZER_NETBSD
#define dirfd(dirp) (*(int *)(dirp))
#define fileno_unlocked(fp)              \
  (((__sanitizer_FILE *)fp)->_file == -1 \
       ? -1                              \
       : (int)(unsigned short)(((__sanitizer_FILE *)fp)->_file))

#define stdout ((__sanitizer_FILE*)&__sF[1])
#define stderr ((__sanitizer_FILE*)&__sF[2])

#define nanosleep __nanosleep50
#define vfork __vfork14
#endif

#ifdef __mips__
const int kSigCount = 129;
#else
const int kSigCount = 65;
#endif

#ifdef __mips__
struct ucontext_t {
  u64 opaque[768 / sizeof(u64) + 1];
};
#else
struct ucontext_t {
  // The size is determined by looking at sizeof of real ucontext_t on linux.
  u64 opaque[936 / sizeof(u64) + 1];
};
#endif

#if defined(__x86_64__) || defined(__mips__) || SANITIZER_PPC64V1 || \
    defined(__s390x__)
#define PTHREAD_ABI_BASE  "GLIBC_2.3.2"
#elif defined(__aarch64__) || SANITIZER_PPC64V2
#define PTHREAD_ABI_BASE  "GLIBC_2.17"
#elif SANITIZER_LOONGARCH64
#define PTHREAD_ABI_BASE  "GLIBC_2.36"
#elif SANITIZER_RISCV64
#  define PTHREAD_ABI_BASE "GLIBC_2.27"
#endif

extern "C" int pthread_attr_init(void *attr);
extern "C" int pthread_attr_destroy(void *attr);
DECLARE_REAL(int, pthread_attr_getdetachstate, void *, void *)
extern "C" int pthread_attr_setstacksize(void *attr, uptr stacksize);
extern "C" int pthread_atfork(void (*prepare)(void), void (*parent)(void),
                              void (*child)(void));
extern "C" int pthread_key_create(unsigned *key, void (*destructor)(void* v));
extern "C" int pthread_setspecific(unsigned key, const void *v);
DECLARE_REAL(int, pthread_mutexattr_gettype, void *, void *)
DECLARE_REAL(int, fflush, __sanitizer_FILE *fp)
DECLARE_REAL_AND_INTERCEPTOR(void *, malloc, uptr size)
DECLARE_REAL_AND_INTERCEPTOR(void, free, void *ptr)
extern "C" int pthread_equal(void *t1, void *t2);
extern "C" void *pthread_self();
extern "C" void _exit(int status);
#if !SANITIZER_NETBSD
extern "C" int fileno_unlocked(void *stream);
extern "C" int dirfd(void *dirp);
#endif
#if SANITIZER_NETBSD
extern __sanitizer_FILE __sF[];
#else
extern __sanitizer_FILE *stdout, *stderr;
#endif
#if !SANITIZER_FREEBSD && !SANITIZER_APPLE && !SANITIZER_NETBSD
const int PTHREAD_MUTEX_RECURSIVE = 1;
const int PTHREAD_MUTEX_RECURSIVE_NP = 1;
#else
const int PTHREAD_MUTEX_RECURSIVE = 2;
const int PTHREAD_MUTEX_RECURSIVE_NP = 2;
#endif
#if !SANITIZER_FREEBSD && !SANITIZER_APPLE && !SANITIZER_NETBSD
const int EPOLL_CTL_ADD = 1;
#endif
const int SIGILL = 4;
const int SIGTRAP = 5;
const int SIGABRT = 6;
const int SIGFPE = 8;
const int SIGSEGV = 11;
const int SIGPIPE = 13;
const int SIGTERM = 15;
#if defined(__mips__) || SANITIZER_FREEBSD || SANITIZER_APPLE || SANITIZER_NETBSD
const int SIGBUS = 10;
const int SIGSYS = 12;
#else
const int SIGBUS = 7;
const int SIGSYS = 31;
#endif
#if SANITIZER_HAS_SIGINFO
const int SI_TIMER = -2;
#endif
void *const MAP_FAILED = (void*)-1;
#if SANITIZER_NETBSD
const int PTHREAD_BARRIER_SERIAL_THREAD = 1234567;
#elif !SANITIZER_APPLE
const int PTHREAD_BARRIER_SERIAL_THREAD = -1;
#endif
const int MAP_FIXED = 0x10;
typedef long long_t;
typedef __sanitizer::u16 mode_t;

// From /usr/include/unistd.h
# define F_ULOCK 0      /* Unlock a previously locked region.  */
# define F_LOCK  1      /* Lock a region for exclusive use.  */
# define F_TLOCK 2      /* Test and lock a region for exclusive use.  */
# define F_TEST  3      /* Test a region for other processes locks.  */

#if SANITIZER_FREEBSD || SANITIZER_APPLE || SANITIZER_NETBSD
const int SA_SIGINFO = 0x40;
const int SIG_SETMASK = 3;
#elif defined(__mips__)
const int SA_SIGINFO = 8;
const int SIG_SETMASK = 3;
#else
const int SA_SIGINFO = 4;
const int SIG_SETMASK = 2;
#endif

namespace __tsan {
struct SignalDesc {
  bool armed;
  __sanitizer_siginfo siginfo;
  ucontext_t ctx;
};

struct ThreadSignalContext {
  int int_signal_send;
  SignalDesc pending_signals[kSigCount];
  // emptyset and oldset are too big for stack.
  __sanitizer_sigset_t emptyset;
  __sanitizer_sigset_t oldset;
};

void EnterBlockingFunc(ThreadState *thr) {
  for (;;) {
    // The order is important to not delay a signal infinitely if it's
    // delivered right before we set in_blocking_func. Note: we can't call
    // ProcessPendingSignals when in_blocking_func is set, or we can handle
    // a signal synchronously when we are already handling a signal.
    atomic_store(&thr->in_blocking_func, 1, memory_order_relaxed);
    if (atomic_load(&thr->pending_signals, memory_order_relaxed) == 0)
      break;
    atomic_store(&thr->in_blocking_func, 0, memory_order_relaxed);
    ProcessPendingSignals(thr);
  }
}

// The sole reason tsan wraps atexit callbacks is to establish synchronization
// between callback setup and callback execution.
struct AtExitCtx {
  void (*f)();
  void *arg;
  uptr pc;
};

// InterceptorContext holds all global data required for interceptors.
// It's explicitly constructed in InitializeInterceptors with placement new
// and is never destroyed. This allows usage of members with non-trivial
// constructors and destructors.
struct InterceptorContext {
  // The object is 64-byte aligned, because we want hot data to be located
  // in a single cache line if possible (it's accessed in every interceptor).
  alignas(64) LibIgnore libignore;
  __sanitizer_sigaction sigactions[kSigCount];
#if !SANITIZER_APPLE && !SANITIZER_NETBSD
  unsigned finalize_key;
#endif

  Mutex atexit_mu;
  Vector<struct AtExitCtx *> AtExitStack;

  InterceptorContext() : libignore(LINKER_INITIALIZED), atexit_mu(MutexTypeAtExit), AtExitStack() {}
};

alignas(64) static char interceptor_placeholder[sizeof(InterceptorContext)];
InterceptorContext *interceptor_ctx() {
  return reinterpret_cast<InterceptorContext*>(&interceptor_placeholder[0]);
}

LibIgnore *libignore() {
  return &interceptor_ctx()->libignore;
}

void InitializeLibIgnore() {
  const SuppressionContext &supp = *Suppressions();
  const uptr n = supp.SuppressionCount();
  for (uptr i = 0; i < n; i++) {
    const Suppression *s = supp.SuppressionAt(i);
    if (0 == internal_strcmp(s->type, kSuppressionLib))
      libignore()->AddIgnoredLibrary(s->templ);
  }
  if (flags()->ignore_noninstrumented_modules)
    libignore()->IgnoreNoninstrumentedModules(true);
  libignore()->OnLibraryLoaded(0);
}

// The following two hooks can be used by for cooperative scheduling when
// locking.
#ifdef TSAN_EXTERNAL_HOOKS
void OnPotentiallyBlockingRegionBegin();
void OnPotentiallyBlockingRegionEnd();
#else
SANITIZER_WEAK_CXX_DEFAULT_IMPL void OnPotentiallyBlockingRegionBegin() {}
SANITIZER_WEAK_CXX_DEFAULT_IMPL void OnPotentiallyBlockingRegionEnd() {}
#endif

}  // namespace __tsan

static ThreadSignalContext *SigCtx(ThreadState *thr) {
  // This function may be called reentrantly if it is interrupted by a signal
  // handler. Use CAS to handle the race.
  uptr ctx = atomic_load(&thr->signal_ctx, memory_order_relaxed);
  if (ctx == 0 && !thr->is_dead) {
    uptr pctx =
        (uptr)MmapOrDie(sizeof(ThreadSignalContext), "ThreadSignalContext");
    MemoryResetRange(thr, (uptr)&SigCtx, pctx, sizeof(ThreadSignalContext));
    if (atomic_compare_exchange_strong(&thr->signal_ctx, &ctx, pctx,
                                       memory_order_relaxed)) {
      ctx = pctx;
    } else {
      UnmapOrDie((ThreadSignalContext *)pctx, sizeof(ThreadSignalContext));
    }
  }
  return (ThreadSignalContext *)ctx;
}

ScopedInterceptor::ScopedInterceptor(ThreadState *thr, const char *fname,
                                     uptr pc)
    : thr_(thr) {
  LazyInitialize(thr);
  if (UNLIKELY(atomic_load(&thr->in_blocking_func, memory_order_relaxed))) {
    // pthread_join is marked as blocking, but it's also known to call other
    // intercepted functions (mmap, free). If we don't reset in_blocking_func
    // we can get deadlocks and memory corruptions if we deliver a synchronous
    // signal inside of an mmap/free interceptor.
    // So reset it and restore it back in the destructor.
    // See https://github.com/google/sanitizers/issues/1540
    atomic_store(&thr->in_blocking_func, 0, memory_order_relaxed);
    in_blocking_func_ = true;
  }
  if (!thr_->is_inited) return;
  if (!thr_->ignore_interceptors) FuncEntry(thr, pc);
  DPrintf("#%d: intercept %s()\n", thr_->tid, fname);
  ignoring_ =
      !thr_->in_ignored_lib && (flags()->ignore_interceptors_accesses ||
                                libignore()->IsIgnored(pc, &in_ignored_lib_));
  EnableIgnores();
}

ScopedInterceptor::~ScopedInterceptor() {
  if (!thr_->is_inited) return;
  DisableIgnores();
  if (UNLIKELY(in_blocking_func_))
    EnterBlockingFunc(thr_);
  if (!thr_->ignore_interceptors) {
    ProcessPendingSignals(thr_);
    FuncExit(thr_);
    CheckedMutex::CheckNoLocks();
  }
}

NOINLINE
void ScopedInterceptor::EnableIgnoresImpl() {
  ThreadIgnoreBegin(thr_, 0);
  if (flags()->ignore_noninstrumented_modules)
    thr_->suppress_reports++;
  if (in_ignored_lib_) {
    DCHECK(!thr_->in_ignored_lib);
    thr_->in_ignored_lib = true;
  }
}

NOINLINE
void ScopedInterceptor::DisableIgnoresImpl() {
  ThreadIgnoreEnd(thr_);
  if (flags()->ignore_noninstrumented_modules)
    thr_->suppress_reports--;
  if (in_ignored_lib_) {
    DCHECK(thr_->in_ignored_lib);
    thr_->in_ignored_lib = false;
  }
}

#define TSAN_INTERCEPT(func) INTERCEPT_FUNCTION(func)
#if SANITIZER_FREEBSD || SANITIZER_NETBSD
#  define TSAN_INTERCEPT_VER(func, ver) INTERCEPT_FUNCTION(func)
#else
#  define TSAN_INTERCEPT_VER(func, ver) INTERCEPT_FUNCTION_VER(func, ver)
#endif
#if SANITIZER_FREEBSD
#  define TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(func) \
    INTERCEPT_FUNCTION(_pthread_##func)
#else
#  define TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(func)
#endif
#if SANITIZER_NETBSD
#  define TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(func) \
    INTERCEPT_FUNCTION(__libc_##func)
#  define TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS_THR(func) \
    INTERCEPT_FUNCTION(__libc_thr_##func)
#else
#  define TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(func)
#  define TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS_THR(func)
#endif

#define READ_STRING_OF_LEN(thr, pc, s, len, n)                 \
  MemoryAccessRange((thr), (pc), (uptr)(s),                         \
    common_flags()->strict_string_checks ? (len) + 1 : (n), false)

#define READ_STRING(thr, pc, s, n)                             \
    READ_STRING_OF_LEN((thr), (pc), (s), internal_strlen(s), (n))

#define BLOCK_REAL(name) (BlockingCall(thr), REAL(name))

struct BlockingCall {
  explicit BlockingCall(ThreadState *thr)
      : thr(thr) {
    EnterBlockingFunc(thr);
    // When we are in a "blocking call", we process signals asynchronously
    // (right when they arrive). In this context we do not expect to be
    // executing any user/runtime code. The known interceptor sequence when
    // this is not true is: pthread_join -> munmap(stack). It's fine
    // to ignore munmap in this case -- we handle stack shadow separately.
    thr->ignore_interceptors++;
  }

  ~BlockingCall() {
    thr->ignore_interceptors--;
    atomic_store(&thr->in_blocking_func, 0, memory_order_relaxed);
  }

  ThreadState *thr;
};

TSAN_INTERCEPTOR(unsigned, sleep, unsigned sec) {
  SCOPED_TSAN_INTERCEPTOR(sleep, sec);
  unsigned res = BLOCK_REAL(sleep)(sec);
  AfterSleep(thr, pc);
  return res;
}

TSAN_INTERCEPTOR(int, usleep, long_t usec) {
  SCOPED_TSAN_INTERCEPTOR(usleep, usec);
  int res = BLOCK_REAL(usleep)(usec);
  AfterSleep(thr, pc);
  return res;
}

TSAN_INTERCEPTOR(int, nanosleep, void *req, void *rem) {
  SCOPED_TSAN_INTERCEPTOR(nanosleep, req, rem);
  int res = BLOCK_REAL(nanosleep)(req, rem);
  AfterSleep(thr, pc);
  return res;
}

TSAN_INTERCEPTOR(int, pause, int fake) {
  SCOPED_TSAN_INTERCEPTOR(pause, fake);
  return BLOCK_REAL(pause)(fake);
}

// Note: we specifically call the function in such strange way
// with "installed_at" because in reports it will appear between
// callback frames and the frame that installed the callback.
static void at_exit_callback_installed_at() {
  AtExitCtx *ctx;
  {
    // Ensure thread-safety.
    Lock l(&interceptor_ctx()->atexit_mu);

    // Pop AtExitCtx from the top of the stack of callback functions
    uptr element = interceptor_ctx()->AtExitStack.Size() - 1;
    ctx = interceptor_ctx()->AtExitStack[element];
    interceptor_ctx()->AtExitStack.PopBack();
  }

  ThreadState *thr = cur_thread();
  Acquire(thr, ctx->pc, (uptr)ctx);
  FuncEntry(thr, ctx->pc);
  ((void(*)())ctx->f)();
  FuncExit(thr);
  Free(ctx);
}

static void cxa_at_exit_callback_installed_at(void *arg) {
  ThreadState *thr = cur_thread();
  AtExitCtx *ctx = (AtExitCtx*)arg;
  Acquire(thr, ctx->pc, (uptr)arg);
  FuncEntry(thr, ctx->pc);
  ((void(*)(void *arg))ctx->f)(ctx->arg);
  FuncExit(thr);
  Free(ctx);
}

static int setup_at_exit_wrapper(ThreadState *thr, uptr pc, void(*f)(),
      void *arg, void *dso);

#if !SANITIZER_ANDROID
TSAN_INTERCEPTOR(int, atexit, void (*f)()) {
  if (in_symbolizer())
    return 0;
  // We want to setup the atexit callback even if we are in ignored lib
  // or after fork.
  SCOPED_INTERCEPTOR_RAW(atexit, f);
  return setup_at_exit_wrapper(thr, GET_CALLER_PC(), (void (*)())f, 0, 0);
}
#endif

TSAN_INTERCEPTOR(int, __cxa_atexit, void (*f)(void *a), void *arg, void *dso) {
  if (in_symbolizer())
    return 0;
  SCOPED_TSAN_INTERCEPTOR(__cxa_atexit, f, arg, dso);
  return setup_at_exit_wrapper(thr, GET_CALLER_PC(), (void (*)())f, arg, dso);
}

static int setup_at_exit_wrapper(ThreadState *thr, uptr pc, void(*f)(),
      void *arg, void *dso) {
  auto *ctx = New<AtExitCtx>();
  ctx->f = f;
  ctx->arg = arg;
  ctx->pc = pc;
  Release(thr, pc, (uptr)ctx);
  // Memory allocation in __cxa_atexit will race with free during exit,
  // because we do not see synchronization around atexit callback list.
  ThreadIgnoreBegin(thr, pc);
  int res;
  if (!dso) {
    // NetBSD does not preserve the 2nd argument if dso is equal to 0
    // Store ctx in a local stack-like structure

    // Ensure thread-safety.
    Lock l(&interceptor_ctx()->atexit_mu);
    // __cxa_atexit calls calloc. If we don't ignore interceptors, we will fail
    // due to atexit_mu held on exit from the calloc interceptor.
    ScopedIgnoreInterceptors ignore;

    res = REAL(__cxa_atexit)((void (*)(void *a))at_exit_callback_installed_at,
                             0, 0);
    // Push AtExitCtx on the top of the stack of callback functions
    if (!res) {
      interceptor_ctx()->AtExitStack.PushBack(ctx);
    }
  } else {
    res = REAL(__cxa_atexit)(cxa_at_exit_callback_installed_at, ctx, dso);
  }
  ThreadIgnoreEnd(thr);
  return res;
}

#if !SANITIZER_APPLE && !SANITIZER_NETBSD
static void on_exit_callback_installed_at(int status, void *arg) {
  ThreadState *thr = cur_thread();
  AtExitCtx *ctx = (AtExitCtx*)arg;
  Acquire(thr, ctx->pc, (uptr)arg);
  FuncEntry(thr, ctx->pc);
  ((void(*)(int status, void *arg))ctx->f)(status, ctx->arg);
  FuncExit(thr);
  Free(ctx);
}

TSAN_INTERCEPTOR(int, on_exit, void(*f)(int, void*), void *arg) {
  if (in_symbolizer())
    return 0;
  SCOPED_TSAN_INTERCEPTOR(on_exit, f, arg);
  auto *ctx = New<AtExitCtx>();
  ctx->f = (void(*)())f;
  ctx->arg = arg;
  ctx->pc = GET_CALLER_PC();
  Release(thr, pc, (uptr)ctx);
  // Memory allocation in __cxa_atexit will race with free during exit,
  // because we do not see synchronization around atexit callback list.
  ThreadIgnoreBegin(thr, pc);
  int res = REAL(on_exit)(on_exit_callback_installed_at, ctx);
  ThreadIgnoreEnd(thr);
  return res;
}
#define TSAN_MAYBE_INTERCEPT_ON_EXIT TSAN_INTERCEPT(on_exit)
#else
#define TSAN_MAYBE_INTERCEPT_ON_EXIT
#endif

// Cleanup old bufs.
static void JmpBufGarbageCollect(ThreadState *thr, uptr sp) {
  for (uptr i = 0; i < thr->jmp_bufs.Size(); i++) {
    JmpBuf *buf = &thr->jmp_bufs[i];
    if (buf->sp <= sp) {
      uptr sz = thr->jmp_bufs.Size();
      internal_memcpy(buf, &thr->jmp_bufs[sz - 1], sizeof(*buf));
      thr->jmp_bufs.PopBack();
      i--;
    }
  }
}

static void SetJmp(ThreadState *thr, uptr sp) {
  if (!thr->is_inited)  // called from libc guts during bootstrap
    return;
  // Cleanup old bufs.
  JmpBufGarbageCollect(thr, sp);
  // Remember the buf.
  JmpBuf *buf = thr->jmp_bufs.PushBack();
  buf->sp = sp;
  buf->shadow_stack_pos = thr->shadow_stack_pos;
  ThreadSignalContext *sctx = SigCtx(thr);
  buf->int_signal_send = sctx ? sctx->int_signal_send : 0;
  buf->in_blocking_func = atomic_load(&thr->in_blocking_func, memory_order_relaxed);
  buf->in_signal_handler = atomic_load(&thr->in_signal_handler,
      memory_order_relaxed);
}

static void LongJmp(ThreadState *thr, uptr *env) {
  uptr sp = ExtractLongJmpSp(env);
  // Find the saved buf with matching sp.
  for (uptr i = 0; i < thr->jmp_bufs.Size(); i++) {
    JmpBuf *buf = &thr->jmp_bufs[i];
    if (buf->sp == sp) {
      CHECK_GE(thr->shadow_stack_pos, buf->shadow_stack_pos);
      // Unwind the stack.
      while (thr->shadow_stack_pos > buf->shadow_stack_pos)
        FuncExit(thr);
      ThreadSignalContext *sctx = SigCtx(thr);
      if (sctx)
        sctx->int_signal_send = buf->int_signal_send;
      atomic_store(&thr->in_blocking_func, buf->in_blocking_func,
          memory_order_relaxed);
      atomic_store(&thr->in_signal_handler, buf->in_signal_handler,
          memory_order_relaxed);
      JmpBufGarbageCollect(thr, buf->sp - 1);  // do not collect buf->sp
      return;
    }
  }
  Printf("ThreadSanitizer: can't find longjmp buf\n");
  CHECK(0);
}

// FIXME: put everything below into a common extern "C" block?
extern "C" void __tsan_setjmp(uptr sp) { SetJmp(cur_thread_init(), sp); }

#if SANITIZER_APPLE
TSAN_INTERCEPTOR(int, setjmp, void *env);
TSAN_INTERCEPTOR(int, _setjmp, void *env);
TSAN_INTERCEPTOR(int, sigsetjmp, void *env);
#else  // SANITIZER_APPLE

#if SANITIZER_NETBSD
#define setjmp_symname __setjmp14
#define sigsetjmp_symname __sigsetjmp14
#else
#define setjmp_symname setjmp
#define sigsetjmp_symname sigsetjmp
#endif

DEFINE_REAL(int, setjmp_symname, void *env)
DEFINE_REAL(int, _setjmp, void *env)
DEFINE_REAL(int, sigsetjmp_symname, void *env)
#if !SANITIZER_NETBSD
DEFINE_REAL(int, __sigsetjmp, void *env)
#endif

// The real interceptor for setjmp is special, and implemented in pure asm. We
// just need to initialize the REAL functions so that they can be used in asm.
static void InitializeSetjmpInterceptors() {
  // We can not use TSAN_INTERCEPT to get setjmp addr, because it does &setjmp and
  // setjmp is not present in some versions of libc.
  using __interception::InterceptFunction;
  InterceptFunction(SANITIZER_STRINGIFY(setjmp_symname), (uptr*)&REAL(setjmp_symname), 0, 0);
  InterceptFunction("_setjmp", (uptr*)&REAL(_setjmp), 0, 0);
  InterceptFunction(SANITIZER_STRINGIFY(sigsetjmp_symname), (uptr*)&REAL(sigsetjmp_symname), 0,
                    0);
#if !SANITIZER_NETBSD
  InterceptFunction("__sigsetjmp", (uptr*)&REAL(__sigsetjmp), 0, 0);
#endif
}
#endif  // SANITIZER_APPLE

#if SANITIZER_NETBSD
#define longjmp_symname __longjmp14
#define siglongjmp_symname __siglongjmp14
#else
#define longjmp_symname longjmp
#define siglongjmp_symname siglongjmp
#endif

TSAN_INTERCEPTOR(void, longjmp_symname, uptr *env, int val) {
  // Note: if we call REAL(longjmp) in the context of ScopedInterceptor,
  // bad things will happen. We will jump over ScopedInterceptor dtor and can
  // leave thr->in_ignored_lib set.
  {
    SCOPED_INTERCEPTOR_RAW(longjmp_symname, env, val);
  }
  LongJmp(cur_thread(), env);
  REAL(longjmp_symname)(env, val);
}

TSAN_INTERCEPTOR(void, siglongjmp_symname, uptr *env, int val) {
  {
    SCOPED_INTERCEPTOR_RAW(siglongjmp_symname, env, val);
  }
  LongJmp(cur_thread(), env);
  REAL(siglongjmp_symname)(env, val);
}

#if SANITIZER_NETBSD
TSAN_INTERCEPTOR(void, _longjmp, uptr *env, int val) {
  {
    SCOPED_INTERCEPTOR_RAW(_longjmp, env, val);
  }
  LongJmp(cur_thread(), env);
  REAL(_longjmp)(env, val);
}
#endif

#if !SANITIZER_APPLE
TSAN_INTERCEPTOR(void*, malloc, uptr size) {
  if (in_symbolizer())
    return InternalAlloc(size);
  void *p = 0;
  {
    SCOPED_INTERCEPTOR_RAW(malloc, size);
    p = user_alloc(thr, pc, size);
  }
  invoke_malloc_hook(p, size);
  return p;
}

// In glibc<2.25, dynamic TLS blocks are allocated by __libc_memalign. Intercept
// __libc_memalign so that (1) we can detect races (2) free will not be called
// on libc internally allocated blocks.
TSAN_INTERCEPTOR(void*, __libc_memalign, uptr align, uptr sz) {
  SCOPED_INTERCEPTOR_RAW(__libc_memalign, align, sz);
  return user_memalign(thr, pc, align, sz);
}

TSAN_INTERCEPTOR(void*, calloc, uptr size, uptr n) {
  if (in_symbolizer())
    return InternalCalloc(size, n);
  void *p = 0;
  {
    SCOPED_INTERCEPTOR_RAW(calloc, size, n);
    p = user_calloc(thr, pc, size, n);
  }
  invoke_malloc_hook(p, n * size);
  return p;
}

TSAN_INTERCEPTOR(void*, realloc, void *p, uptr size) {
  if (in_symbolizer())
    return InternalRealloc(p, size);
  if (p)
    invoke_free_hook(p);
  {
    SCOPED_INTERCEPTOR_RAW(realloc, p, size);
    p = user_realloc(thr, pc, p, size);
  }
  invoke_malloc_hook(p, size);
  return p;
}

TSAN_INTERCEPTOR(void*, reallocarray, void *p, uptr size, uptr n) {
  if (in_symbolizer())
    return InternalReallocArray(p, size, n);
  if (p)
    invoke_free_hook(p);
  {
    SCOPED_INTERCEPTOR_RAW(reallocarray, p, size, n);
    p = user_reallocarray(thr, pc, p, size, n);
  }
  invoke_malloc_hook(p, size);
  return p;
}

TSAN_INTERCEPTOR(void, free, void *p) {
  if (p == 0)
    return;
  if (in_symbolizer())
    return InternalFree(p);
  invoke_free_hook(p);
  SCOPED_INTERCEPTOR_RAW(free, p);
  user_free(thr, pc, p);
}

TSAN_INTERCEPTOR(void, cfree, void *p) {
  if (p == 0)
    return;
  if (in_symbolizer())
    return InternalFree(p);
  invoke_free_hook(p);
  SCOPED_INTERCEPTOR_RAW(cfree, p);
  user_free(thr, pc, p);
}

TSAN_INTERCEPTOR(uptr, malloc_usable_size, void *p) {
  SCOPED_INTERCEPTOR_RAW(malloc_usable_size, p);
  return user_alloc_usable_size(p);
}
#endif

TSAN_INTERCEPTOR(char *, strcpy, char *dst, const char *src) {
  SCOPED_TSAN_INTERCEPTOR(strcpy, dst, src);
  uptr srclen = internal_strlen(src);
  MemoryAccessRange(thr, pc, (uptr)dst, srclen + 1, true);
  MemoryAccessRange(thr, pc, (uptr)src, srclen + 1, false);
  return REAL(strcpy)(dst, src);
}

TSAN_INTERCEPTOR(char*, strncpy, char *dst, char *src, uptr n) {
  SCOPED_TSAN_INTERCEPTOR(strncpy, dst, src, n);
  uptr srclen = internal_strnlen(src, n);
  MemoryAccessRange(thr, pc, (uptr)dst, n, true);
  MemoryAccessRange(thr, pc, (uptr)src, min(srclen + 1, n), false);
  return REAL(strncpy)(dst, src, n);
}

TSAN_INTERCEPTOR(char*, strdup, const char *str) {
  SCOPED_TSAN_INTERCEPTOR(strdup, str);
  // strdup will call malloc, so no instrumentation is required here.
  return REAL(strdup)(str);
}

// Zero out addr if it points into shadow memory and was provided as a hint
// only, i.e., MAP_FIXED is not set.
static bool fix_mmap_addr(void **addr, long_t sz, int flags) {
  if (*addr) {
    if (!IsAppMem((uptr)*addr) || !IsAppMem((uptr)*addr + sz - 1)) {
      if (flags & MAP_FIXED) {
        errno = errno_EINVAL;
        return false;
      } else {
        *addr = 0;
      }
    }
  }
  return true;
}

template <class Mmap>
static void *mmap_interceptor(ThreadState *thr, uptr pc, Mmap real_mmap,
                              void *addr, SIZE_T sz, int prot, int flags,
                              int fd, OFF64_T off) {
  if (!fix_mmap_addr(&addr, sz, flags)) return MAP_FAILED;
  void *res = real_mmap(addr, sz, prot, flags, fd, off);
  if (res != MAP_FAILED) {
    if (!IsAppMem((uptr)res) || !IsAppMem((uptr)res + sz - 1)) {
      Report("ThreadSanitizer: mmap at bad address: addr=%p size=%p res=%p\n",
             addr, (void*)sz, res);
      Die();
    }
    if (fd > 0) FdAccess(thr, pc, fd);
    MemoryRangeImitateWriteOrResetRange(thr, pc, (uptr)res, sz);
  }
  return res;
}

template <class Munmap>
static int munmap_interceptor(ThreadState *thr, uptr pc, Munmap real_munmap,
                                void *addr, SIZE_T sz) {
  UnmapShadow(thr, (uptr)addr, sz);
  int res = real_munmap(addr, sz);
  return res;
}

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(void*, memalign, uptr align, uptr sz) {
  SCOPED_INTERCEPTOR_RAW(memalign, align, sz);
  return user_memalign(thr, pc, align, sz);
}
#define TSAN_MAYBE_INTERCEPT_MEMALIGN TSAN_INTERCEPT(memalign)
#else
#define TSAN_MAYBE_INTERCEPT_MEMALIGN
#endif

#if !SANITIZER_APPLE
TSAN_INTERCEPTOR(void*, aligned_alloc, uptr align, uptr sz) {
  if (in_symbolizer())
    return InternalAlloc(sz, nullptr, align);
  SCOPED_INTERCEPTOR_RAW(aligned_alloc, align, sz);
  return user_aligned_alloc(thr, pc, align, sz);
}

TSAN_INTERCEPTOR(void*, valloc, uptr sz) {
  if (in_symbolizer())
    return InternalAlloc(sz, nullptr, GetPageSizeCached());
  SCOPED_INTERCEPTOR_RAW(valloc, sz);
  return user_valloc(thr, pc, sz);
}
#endif

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(void*, pvalloc, uptr sz) {
  if (in_symbolizer()) {
    uptr PageSize = GetPageSizeCached();
    sz = sz ? RoundUpTo(sz, PageSize) : PageSize;
    return InternalAlloc(sz, nullptr, PageSize);
  }
  SCOPED_INTERCEPTOR_RAW(pvalloc, sz);
  return user_pvalloc(thr, pc, sz);
}
#define TSAN_MAYBE_INTERCEPT_PVALLOC TSAN_INTERCEPT(pvalloc)
#else
#define TSAN_MAYBE_INTERCEPT_PVALLOC
#endif

#if !SANITIZER_APPLE
TSAN_INTERCEPTOR(int, posix_memalign, void **memptr, uptr align, uptr sz) {
  if (in_symbolizer()) {
    void *p = InternalAlloc(sz, nullptr, align);
    if (!p)
      return errno_ENOMEM;
    *memptr = p;
    return 0;
  }
  SCOPED_INTERCEPTOR_RAW(posix_memalign, memptr, align, sz);
  return user_posix_memalign(thr, pc, memptr, align, sz);
}
#endif

// Both __cxa_guard_acquire and pthread_once 0-initialize
// the object initially. pthread_once does not have any
// other ABI requirements. __cxa_guard_acquire assumes
// that any non-0 value in the first byte means that
// initialization is completed. Contents of the remaining
// bytes are up to us.
constexpr u32 kGuardInit = 0;
constexpr u32 kGuardDone = 1;
constexpr u32 kGuardRunning = 1 << 16;
constexpr u32 kGuardWaiter = 1 << 17;

static int guard_acquire(ThreadState *thr, uptr pc, atomic_uint32_t *g,
                         bool blocking_hooks = true) {
  if (blocking_hooks)
    OnPotentiallyBlockingRegionBegin();
  auto on_exit = at_scope_exit([blocking_hooks] {
    if (blocking_hooks)
      OnPotentiallyBlockingRegionEnd();
  });

  for (;;) {
    u32 cmp = atomic_load(g, memory_order_acquire);
    if (cmp == kGuardInit) {
      if (atomic_compare_exchange_strong(g, &cmp, kGuardRunning,
                                         memory_order_relaxed))
        return 1;
    } else if (cmp == kGuardDone) {
      if (!thr->in_ignored_lib)
        Acquire(thr, pc, (uptr)g);
      return 0;
    } else {
      if ((cmp & kGuardWaiter) ||
          atomic_compare_exchange_strong(g, &cmp, cmp | kGuardWaiter,
                                         memory_order_relaxed))
        FutexWait(g, cmp | kGuardWaiter);
    }
  }
}

static void guard_release(ThreadState *thr, uptr pc, atomic_uint32_t *g,
                          u32 v) {
  if (!thr->in_ignored_lib)
    Release(thr, pc, (uptr)g);
  u32 old = atomic_exchange(g, v, memory_order_release);
  if (old & kGuardWaiter)
    FutexWake(g, 1 << 30);
}

// __cxa_guard_acquire and friends need to be intercepted in a special way -
// regular interceptors will break statically-linked libstdc++. Linux
// interceptors are especially defined as weak functions (so that they don't
// cause link errors when user defines them as well). So they silently
// auto-disable themselves when such symbol is already present in the binary. If
// we link libstdc++ statically, it will bring own __cxa_guard_acquire which
// will silently replace our interceptor.  That's why on Linux we simply export
// these interceptors with INTERFACE_ATTRIBUTE.
// On OS X, we don't support statically linking, so we just use a regular
// interceptor.
#if SANITIZER_APPLE
#define STDCXX_INTERCEPTOR TSAN_INTERCEPTOR
#else
#define STDCXX_INTERCEPTOR(rettype, name, ...) \
  extern "C" rettype INTERFACE_ATTRIBUTE name(__VA_ARGS__)
#endif

// Used in thread-safe function static initialization.
STDCXX_INTERCEPTOR(int, __cxa_guard_acquire, atomic_uint32_t *g) {
  SCOPED_INTERCEPTOR_RAW(__cxa_guard_acquire, g);
  return guard_acquire(thr, pc, g);
}

STDCXX_INTERCEPTOR(void, __cxa_guard_release, atomic_uint32_t *g) {
  SCOPED_INTERCEPTOR_RAW(__cxa_guard_release, g);
  guard_release(thr, pc, g, kGuardDone);
}

STDCXX_INTERCEPTOR(void, __cxa_guard_abort, atomic_uint32_t *g) {
  SCOPED_INTERCEPTOR_RAW(__cxa_guard_abort, g);
  guard_release(thr, pc, g, kGuardInit);
}

namespace __tsan {
void DestroyThreadState() {
  ThreadState *thr = cur_thread();
  Processor *proc = thr->proc();
  ThreadFinish(thr);
  ProcUnwire(proc, thr);
  ProcDestroy(proc);
  DTLS_Destroy();
  cur_thread_finalize();
}

void PlatformCleanUpThreadState(ThreadState *thr) {
  ThreadSignalContext *sctx = (ThreadSignalContext *)atomic_load(
      &thr->signal_ctx, memory_order_relaxed);
  if (sctx) {
    atomic_store(&thr->signal_ctx, 0, memory_order_relaxed);
    UnmapOrDie(sctx, sizeof(*sctx));
  }
}
}  // namespace __tsan

#if !SANITIZER_APPLE && !SANITIZER_NETBSD && !SANITIZER_FREEBSD
static void thread_finalize(void *v) {
  uptr iter = (uptr)v;
  if (iter > 1) {
    if (pthread_setspecific(interceptor_ctx()->finalize_key,
        (void*)(iter - 1))) {
      Printf("ThreadSanitizer: failed to set thread key\n");
      Die();
    }
    return;
  }
  DestroyThreadState();
}
#endif


struct ThreadParam {
  void* (*callback)(void *arg);
  void *param;
  Tid tid;
  Semaphore created;
  Semaphore started;
};

extern "C" void *__tsan_thread_start_func(void *arg) {
  ThreadParam *p = (ThreadParam*)arg;
  void* (*callback)(void *arg) = p->callback;
  void *param = p->param;
  {
    ThreadState *thr = cur_thread_init();
    // Thread-local state is not initialized yet.
    ScopedIgnoreInterceptors ignore;
#if !SANITIZER_APPLE && !SANITIZER_NETBSD && !SANITIZER_FREEBSD
    ThreadIgnoreBegin(thr, 0);
    if (pthread_setspecific(interceptor_ctx()->finalize_key,
                            (void *)GetPthreadDestructorIterations())) {
      Printf("ThreadSanitizer: failed to set thread key\n");
      Die();
    }
    ThreadIgnoreEnd(thr);
#endif
    p->created.Wait();
    Processor *proc = ProcCreate();
    ProcWire(proc, thr);
    ThreadStart(thr, p->tid, GetTid(), ThreadType::Regular);
    p->started.Post();
  }
  void *res = callback(param);
  // Prevent the callback from being tail called,
  // it mixes up stack traces.
  volatile int foo = 42;
  foo++;
  return res;
}

TSAN_INTERCEPTOR(int, pthread_create,
    void *th, void *attr, void *(*callback)(void*), void * param) {
  SCOPED_INTERCEPTOR_RAW(pthread_create, th, attr, callback, param);

  MaybeSpawnBackgroundThread();

  if (ctx->after_multithreaded_fork) {
    if (flags()->die_after_fork) {
      Report("ThreadSanitizer: starting new threads after multi-threaded "
          "fork is not supported. Dying (set die_after_fork=0 to override)\n");
      Die();
    } else {
      VPrintf(1,
              "ThreadSanitizer: starting new threads after multi-threaded "
              "fork is not supported (pid %lu). Continuing because of "
              "die_after_fork=0, but you are on your own\n",
              internal_getpid());
    }
  }
  __sanitizer_pthread_attr_t myattr;
  if (attr == 0) {
    pthread_attr_init(&myattr);
    attr = &myattr;
  }
  int detached = 0;
  REAL(pthread_attr_getdetachstate)(attr, &detached);
  AdjustStackSize(attr);

  ThreadParam p;
  p.callback = callback;
  p.param = param;
  p.tid = kMainTid;
  int res = -1;
  {
    // Otherwise we see false positives in pthread stack manipulation.
    ScopedIgnoreInterceptors ignore;
    ThreadIgnoreBegin(thr, pc);
    res = REAL(pthread_create)(th, attr, __tsan_thread_start_func, &p);
    ThreadIgnoreEnd(thr);
  }
  if (res == 0) {
    p.tid = ThreadCreate(thr, pc, *(uptr *)th, IsStateDetached(detached));
    CHECK_NE(p.tid, kMainTid);
    // Synchronization on p.tid serves two purposes:
    // 1. ThreadCreate must finish before the new thread starts.
    //    Otherwise the new thread can call pthread_detach, but the pthread_t
    //    identifier is not yet registered in ThreadRegistry by ThreadCreate.
    // 2. ThreadStart must finish before this thread continues.
    //    Otherwise, this thread can call pthread_detach and reset thr->sync
    //    before the new thread got a chance to acquire from it in ThreadStart.
    p.created.Post();
    p.started.Wait();
  }
  if (attr == &myattr)
    pthread_attr_destroy(&myattr);
  return res;
}

TSAN_INTERCEPTOR(int, pthread_join, void *th, void **ret) {
  SCOPED_INTERCEPTOR_RAW(pthread_join, th, ret);
  Tid tid = ThreadConsumeTid(thr, pc, (uptr)th);
  ThreadIgnoreBegin(thr, pc);
  int res = BLOCK_REAL(pthread_join)(th, ret);
  ThreadIgnoreEnd(thr);
  if (res == 0) {
    ThreadJoin(thr, pc, tid);
  }
  return res;
}

// DEFINE_INTERNAL_PTHREAD_FUNCTIONS
namespace __sanitizer {
int internal_pthread_create(void *th, void *attr, void *(*callback)(void *),
                            void *param) {
  ScopedIgnoreInterceptors ignore;
  return REAL(pthread_create)(th, attr, callback, param);
}
int internal_pthread_join(void *th, void **ret) {
  ScopedIgnoreInterceptors ignore;
  return REAL(pthread_join(th, ret));
}
}  // namespace __sanitizer

TSAN_INTERCEPTOR(int, pthread_detach, void *th) {
  SCOPED_INTERCEPTOR_RAW(pthread_detach, th);
  Tid tid = ThreadConsumeTid(thr, pc, (uptr)th);
  int res = REAL(pthread_detach)(th);
  if (res == 0) {
    ThreadDetach(thr, pc, tid);
  }
  return res;
}

TSAN_INTERCEPTOR(void, pthread_exit, void *retval) {
  {
    SCOPED_INTERCEPTOR_RAW(pthread_exit, retval);
#if !SANITIZER_APPLE && !SANITIZER_ANDROID
    CHECK_EQ(thr, &cur_thread_placeholder);
#endif
  }
  REAL(pthread_exit)(retval);
}

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(int, pthread_tryjoin_np, void *th, void **ret) {
  SCOPED_INTERCEPTOR_RAW(pthread_tryjoin_np, th, ret);
  Tid tid = ThreadConsumeTid(thr, pc, (uptr)th);
  ThreadIgnoreBegin(thr, pc);
  int res = REAL(pthread_tryjoin_np)(th, ret);
  ThreadIgnoreEnd(thr);
  if (res == 0)
    ThreadJoin(thr, pc, tid);
  else
    ThreadNotJoined(thr, pc, tid, (uptr)th);
  return res;
}

TSAN_INTERCEPTOR(int, pthread_timedjoin_np, void *th, void **ret,
                 const struct timespec *abstime) {
  SCOPED_INTERCEPTOR_RAW(pthread_timedjoin_np, th, ret, abstime);
  Tid tid = ThreadConsumeTid(thr, pc, (uptr)th);
  ThreadIgnoreBegin(thr, pc);
  int res = BLOCK_REAL(pthread_timedjoin_np)(th, ret, abstime);
  ThreadIgnoreEnd(thr);
  if (res == 0)
    ThreadJoin(thr, pc, tid);
  else
    ThreadNotJoined(thr, pc, tid, (uptr)th);
  return res;
}
#endif

// Problem:
// NPTL implementation of pthread_cond has 2 versions (2.2.5 and 2.3.2).
// pthread_cond_t has different size in the different versions.
// If call new REAL functions for old pthread_cond_t, they will corrupt memory
// after pthread_cond_t (old cond is smaller).
// If we call old REAL functions for new pthread_cond_t, we will lose  some
// functionality (e.g. old functions do not support waiting against
// CLOCK_REALTIME).
// Proper handling would require to have 2 versions of interceptors as well.
// But this is messy, in particular requires linker scripts when sanitizer
// runtime is linked into a shared library.
// Instead we assume we don't have dynamic libraries built against old
// pthread (2.2.5 is dated by 2002). And provide legacy_pthread_cond flag
// that allows to work with old libraries (but this mode does not support
// some features, e.g. pthread_condattr_getpshared).
static void *init_cond(void *c, bool force = false) {
  // sizeof(pthread_cond_t) >= sizeof(uptr) in both versions.
  // So we allocate additional memory on the side large enough to hold
  // any pthread_cond_t object. Always call new REAL functions, but pass
  // the aux object to them.
  // Note: the code assumes that PTHREAD_COND_INITIALIZER initializes
  // first word of pthread_cond_t to zero.
  // It's all relevant only for linux.
  if (!common_flags()->legacy_pthread_cond)
    return c;
  atomic_uintptr_t *p = (atomic_uintptr_t*)c;
  uptr cond = atomic_load(p, memory_order_acquire);
  if (!force && cond != 0)
    return (void*)cond;
  void *newcond = WRAP(malloc)(pthread_cond_t_sz);
  internal_memset(newcond, 0, pthread_cond_t_sz);
  if (atomic_compare_exchange_strong(p, &cond, (uptr)newcond,
      memory_order_acq_rel))
    return newcond;
  WRAP(free)(newcond);
  return (void*)cond;
}

namespace {

template <class Fn>
struct CondMutexUnlockCtx {
  ScopedInterceptor *si;
  ThreadState *thr;
  uptr pc;
  void *m;
  void *c;
  const Fn &fn;

  int Cancel() const { return fn(); }
  void Unlock() const;
};

template <class Fn>
void CondMutexUnlockCtx<Fn>::Unlock() const {
  // pthread_cond_wait interceptor has enabled async signal delivery
  // (see BlockingCall below). Disable async signals since we are running
  // tsan code. Also ScopedInterceptor and BlockingCall destructors won't run
  // since the thread is cancelled, so we have to manually execute them
  // (the thread still can run some user code due to pthread_cleanup_push).
  CHECK_EQ(atomic_load(&thr->in_blocking_func, memory_order_relaxed), 1);
  atomic_store(&thr->in_blocking_func, 0, memory_order_relaxed);
  MutexPostLock(thr, pc, (uptr)m, MutexFlagDoPreLockOnPostLock);
  // Undo BlockingCall ctor effects.
  thr->ignore_interceptors--;
  si->~ScopedInterceptor();
}
}  // namespace

INTERCEPTOR(int, pthread_cond_init, void *c, void *a) {
  void *cond = init_cond(c, true);
  SCOPED_TSAN_INTERCEPTOR(pthread_cond_init, cond, a);
  MemoryAccessRange(thr, pc, (uptr)c, sizeof(uptr), true);
  return REAL(pthread_cond_init)(cond, a);
}

template <class Fn>
int cond_wait(ThreadState *thr, uptr pc, ScopedInterceptor *si, const Fn &fn,
              void *c, void *m) {
  MemoryAccessRange(thr, pc, (uptr)c, sizeof(uptr), false);
  MutexUnlock(thr, pc, (uptr)m);
  int res = 0;
  // This ensures that we handle mutex lock even in case of pthread_cancel.
  // See test/tsan/cond_cancel.cpp.
  {
    // Enable signal delivery while the thread is blocked.
    BlockingCall bc(thr);
    CondMutexUnlockCtx<Fn> arg = {si, thr, pc, m, c, fn};
    res = call_pthread_cancel_with_cleanup(
        [](void *arg) -> int {
          return ((const CondMutexUnlockCtx<Fn> *)arg)->Cancel();
        },
        [](void *arg) { ((const CondMutexUnlockCtx<Fn> *)arg)->Unlock(); },
        &arg);
  }
  if (res == errno_EOWNERDEAD) MutexRepair(thr, pc, (uptr)m);
  MutexPostLock(thr, pc, (uptr)m, MutexFlagDoPreLockOnPostLock);
  return res;
}

INTERCEPTOR(int, pthread_cond_wait, void *c, void *m) {
  void *cond = init_cond(c);
  SCOPED_TSAN_INTERCEPTOR(pthread_cond_wait, cond, m);
  return cond_wait(
      thr, pc, &si, [=]() { return REAL(pthread_cond_wait)(cond, m); }, cond,
      m);
}

INTERCEPTOR(int, pthread_cond_timedwait, void *c, void *m, void *abstime) {
  void *cond = init_cond(c);
  SCOPED_TSAN_INTERCEPTOR(pthread_cond_timedwait, cond, m, abstime);
  return cond_wait(
      thr, pc, &si,
      [=]() { return REAL(pthread_cond_timedwait)(cond, m, abstime); }, cond,
      m);
}

#if SANITIZER_LINUX
INTERCEPTOR(int, pthread_cond_clockwait, void *c, void *m,
            __sanitizer_clockid_t clock, void *abstime) {
  void *cond = init_cond(c);
  SCOPED_TSAN_INTERCEPTOR(pthread_cond_clockwait, cond, m, clock, abstime);
  return cond_wait(
      thr, pc, &si,
      [=]() { return REAL(pthread_cond_clockwait)(cond, m, clock, abstime); },
      cond, m);
}
#define TSAN_MAYBE_PTHREAD_COND_CLOCKWAIT TSAN_INTERCEPT(pthread_cond_clockwait)
#else
#define TSAN_MAYBE_PTHREAD_COND_CLOCKWAIT
#endif

#if SANITIZER_APPLE
INTERCEPTOR(int, pthread_cond_timedwait_relative_np, void *c, void *m,
            void *reltime) {
  void *cond = init_cond(c);
  SCOPED_TSAN_INTERCEPTOR(pthread_cond_timedwait_relative_np, cond, m, reltime);
  return cond_wait(
      thr, pc, &si,
      [=]() {
        return REAL(pthread_cond_timedwait_relative_np)(cond, m, reltime);
      },
      cond, m);
}
#endif

INTERCEPTOR(int, pthread_cond_signal, void *c) {
  void *cond = init_cond(c);
  SCOPED_TSAN_INTERCEPTOR(pthread_cond_signal, cond);
  MemoryAccessRange(thr, pc, (uptr)c, sizeof(uptr), false);
  return REAL(pthread_cond_signal)(cond);
}

INTERCEPTOR(int, pthread_cond_broadcast, void *c) {
  void *cond = init_cond(c);
  SCOPED_TSAN_INTERCEPTOR(pthread_cond_broadcast, cond);
  MemoryAccessRange(thr, pc, (uptr)c, sizeof(uptr), false);
  return REAL(pthread_cond_broadcast)(cond);
}

INTERCEPTOR(int, pthread_cond_destroy, void *c) {
  void *cond = init_cond(c);
  SCOPED_TSAN_INTERCEPTOR(pthread_cond_destroy, cond);
  MemoryAccessRange(thr, pc, (uptr)c, sizeof(uptr), true);
  int res = REAL(pthread_cond_destroy)(cond);
  if (common_flags()->legacy_pthread_cond) {
    // Free our aux cond and zero the pointer to not leave dangling pointers.
    WRAP(free)(cond);
    atomic_store((atomic_uintptr_t*)c, 0, memory_order_relaxed);
  }
  return res;
}

TSAN_INTERCEPTOR(int, pthread_mutex_init, void *m, void *a) {
  SCOPED_TSAN_INTERCEPTOR(pthread_mutex_init, m, a);
  int res = REAL(pthread_mutex_init)(m, a);
  if (res == 0) {
    u32 flagz = 0;
    if (a) {
      int type = 0;
      if (REAL(pthread_mutexattr_gettype)(a, &type) == 0)
        if (type == PTHREAD_MUTEX_RECURSIVE ||
            type == PTHREAD_MUTEX_RECURSIVE_NP)
          flagz |= MutexFlagWriteReentrant;
    }
    MutexCreate(thr, pc, (uptr)m, flagz);
  }
  return res;
}

TSAN_INTERCEPTOR(int, pthread_mutex_destroy, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_mutex_destroy, m);
  int res = REAL(pthread_mutex_destroy)(m);
  if (res == 0 || res == errno_EBUSY) {
    MutexDestroy(thr, pc, (uptr)m);
  }
  return res;
}

TSAN_INTERCEPTOR(int, pthread_mutex_lock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_mutex_lock, m);
  MutexPreLock(thr, pc, (uptr)m);
  int res = BLOCK_REAL(pthread_mutex_lock)(m);
  if (res == errno_EOWNERDEAD)
    MutexRepair(thr, pc, (uptr)m);
  if (res == 0 || res == errno_EOWNERDEAD)
    MutexPostLock(thr, pc, (uptr)m);
  if (res == errno_EINVAL)
    MutexInvalidAccess(thr, pc, (uptr)m);
  return res;
}

TSAN_INTERCEPTOR(int, pthread_mutex_trylock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_mutex_trylock, m);
  int res = REAL(pthread_mutex_trylock)(m);
  if (res == errno_EOWNERDEAD)
    MutexRepair(thr, pc, (uptr)m);
  if (res == 0 || res == errno_EOWNERDEAD)
    MutexPostLock(thr, pc, (uptr)m, MutexFlagTryLock);
  return res;
}

#if !SANITIZER_APPLE
TSAN_INTERCEPTOR(int, pthread_mutex_timedlock, void *m, void *abstime) {
  SCOPED_TSAN_INTERCEPTOR(pthread_mutex_timedlock, m, abstime);
  int res = REAL(pthread_mutex_timedlock)(m, abstime);
  if (res == 0) {
    MutexPostLock(thr, pc, (uptr)m, MutexFlagTryLock);
  }
  return res;
}
#endif

TSAN_INTERCEPTOR(int, pthread_mutex_unlock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_mutex_unlock, m);
  MutexUnlock(thr, pc, (uptr)m);
  int res = REAL(pthread_mutex_unlock)(m);
  if (res == errno_EINVAL)
    MutexInvalidAccess(thr, pc, (uptr)m);
  return res;
}

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(int, pthread_mutex_clocklock, void *m,
                 __sanitizer_clockid_t clock, void *abstime) {
  SCOPED_TSAN_INTERCEPTOR(pthread_mutex_clocklock, m, clock, abstime);
  MutexPreLock(thr, pc, (uptr)m);
  int res = BLOCK_REAL(pthread_mutex_clocklock)(m, clock, abstime);
  if (res == errno_EOWNERDEAD)
    MutexRepair(thr, pc, (uptr)m);
  if (res == 0 || res == errno_EOWNERDEAD)
    MutexPostLock(thr, pc, (uptr)m);
  if (res == errno_EINVAL)
    MutexInvalidAccess(thr, pc, (uptr)m);
  return res;
}
#endif

#if SANITIZER_GLIBC
#  if !__GLIBC_PREREQ(2, 34)
// glibc 2.34 applies a non-default version for the two functions. They are no
// longer expected to be intercepted by programs.
TSAN_INTERCEPTOR(int, __pthread_mutex_lock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(__pthread_mutex_lock, m);
  MutexPreLock(thr, pc, (uptr)m);
  int res = BLOCK_REAL(__pthread_mutex_lock)(m);
  if (res == errno_EOWNERDEAD)
    MutexRepair(thr, pc, (uptr)m);
  if (res == 0 || res == errno_EOWNERDEAD)
    MutexPostLock(thr, pc, (uptr)m);
  if (res == errno_EINVAL)
    MutexInvalidAccess(thr, pc, (uptr)m);
  return res;
}

TSAN_INTERCEPTOR(int, __pthread_mutex_unlock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(__pthread_mutex_unlock, m);
  MutexUnlock(thr, pc, (uptr)m);
  int res = REAL(__pthread_mutex_unlock)(m);
  if (res == errno_EINVAL)
    MutexInvalidAccess(thr, pc, (uptr)m);
  return res;
}
#  endif
#endif

#if !SANITIZER_APPLE
TSAN_INTERCEPTOR(int, pthread_spin_init, void *m, int pshared) {
  SCOPED_TSAN_INTERCEPTOR(pthread_spin_init, m, pshared);
  int res = REAL(pthread_spin_init)(m, pshared);
  if (res == 0) {
    MutexCreate(thr, pc, (uptr)m);
  }
  return res;
}

TSAN_INTERCEPTOR(int, pthread_spin_destroy, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_spin_destroy, m);
  int res = REAL(pthread_spin_destroy)(m);
  if (res == 0) {
    MutexDestroy(thr, pc, (uptr)m);
  }
  return res;
}

TSAN_INTERCEPTOR(int, pthread_spin_lock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_spin_lock, m);
  MutexPreLock(thr, pc, (uptr)m);
  int res = BLOCK_REAL(pthread_spin_lock)(m);
  if (res == 0) {
    MutexPostLock(thr, pc, (uptr)m);
  }
  return res;
}

TSAN_INTERCEPTOR(int, pthread_spin_trylock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_spin_trylock, m);
  int res = REAL(pthread_spin_trylock)(m);
  if (res == 0) {
    MutexPostLock(thr, pc, (uptr)m, MutexFlagTryLock);
  }
  return res;
}

TSAN_INTERCEPTOR(int, pthread_spin_unlock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_spin_unlock, m);
  MutexUnlock(thr, pc, (uptr)m);
  int res = REAL(pthread_spin_unlock)(m);
  return res;
}
#endif

TSAN_INTERCEPTOR(int, pthread_rwlock_init, void *m, void *a) {
  SCOPED_TSAN_INTERCEPTOR(pthread_rwlock_init, m, a);
  int res = REAL(pthread_rwlock_init)(m, a);
  if (res == 0) {
    MutexCreate(thr, pc, (uptr)m);
  }
  return res;
}

TSAN_INTERCEPTOR(int, pthread_rwlock_destroy, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_rwlock_destroy, m);
  int res = REAL(pthread_rwlock_destroy)(m);
  if (res == 0) {
    MutexDestroy(thr, pc, (uptr)m);
  }
  return res;
}

TSAN_INTERCEPTOR(int, pthread_rwlock_rdlock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_rwlock_rdlock, m);
  MutexPreReadLock(thr, pc, (uptr)m);
  int res = REAL(pthread_rwlock_rdlock)(m);
  if (res == 0) {
    MutexPostReadLock(thr, pc, (uptr)m);
  }
  return res;
}

TSAN_INTERCEPTOR(int, pthread_rwlock_tryrdlock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_rwlock_tryrdlock, m);
  int res = REAL(pthread_rwlock_tryrdlock)(m);
  if (res == 0) {
    MutexPostReadLock(thr, pc, (uptr)m, MutexFlagTryLock);
  }
  return res;
}

#if !SANITIZER_APPLE
TSAN_INTERCEPTOR(int, pthread_rwlock_timedrdlock, void *m, void *abstime) {
  SCOPED_TSAN_INTERCEPTOR(pthread_rwlock_timedrdlock, m, abstime);
  int res = REAL(pthread_rwlock_timedrdlock)(m, abstime);
  if (res == 0) {
    MutexPostReadLock(thr, pc, (uptr)m);
  }
  return res;
}
#endif

TSAN_INTERCEPTOR(int, pthread_rwlock_wrlock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_rwlock_wrlock, m);
  MutexPreLock(thr, pc, (uptr)m);
  int res = BLOCK_REAL(pthread_rwlock_wrlock)(m);
  if (res == 0) {
    MutexPostLock(thr, pc, (uptr)m);
  }
  return res;
}

TSAN_INTERCEPTOR(int, pthread_rwlock_trywrlock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_rwlock_trywrlock, m);
  int res = REAL(pthread_rwlock_trywrlock)(m);
  if (res == 0) {
    MutexPostLock(thr, pc, (uptr)m, MutexFlagTryLock);
  }
  return res;
}

#if !SANITIZER_APPLE
TSAN_INTERCEPTOR(int, pthread_rwlock_timedwrlock, void *m, void *abstime) {
  SCOPED_TSAN_INTERCEPTOR(pthread_rwlock_timedwrlock, m, abstime);
  int res = REAL(pthread_rwlock_timedwrlock)(m, abstime);
  if (res == 0) {
    MutexPostLock(thr, pc, (uptr)m, MutexFlagTryLock);
  }
  return res;
}
#endif

TSAN_INTERCEPTOR(int, pthread_rwlock_unlock, void *m) {
  SCOPED_TSAN_INTERCEPTOR(pthread_rwlock_unlock, m);
  MutexReadOrWriteUnlock(thr, pc, (uptr)m);
  int res = REAL(pthread_rwlock_unlock)(m);
  return res;
}

#if !SANITIZER_APPLE
TSAN_INTERCEPTOR(int, pthread_barrier_init, void *b, void *a, unsigned count) {
  SCOPED_TSAN_INTERCEPTOR(pthread_barrier_init, b, a, count);
  MemoryAccess(thr, pc, (uptr)b, 1, kAccessWrite);
  int res = REAL(pthread_barrier_init)(b, a, count);
  return res;
}

TSAN_INTERCEPTOR(int, pthread_barrier_destroy, void *b) {
  SCOPED_TSAN_INTERCEPTOR(pthread_barrier_destroy, b);
  MemoryAccess(thr, pc, (uptr)b, 1, kAccessWrite);
  int res = REAL(pthread_barrier_destroy)(b);
  return res;
}

TSAN_INTERCEPTOR(int, pthread_barrier_wait, void *b) {
  SCOPED_TSAN_INTERCEPTOR(pthread_barrier_wait, b);
  Release(thr, pc, (uptr)b);
  MemoryAccess(thr, pc, (uptr)b, 1, kAccessRead);
  int res = REAL(pthread_barrier_wait)(b);
  MemoryAccess(thr, pc, (uptr)b, 1, kAccessRead);
  if (res == 0 || res == PTHREAD_BARRIER_SERIAL_THREAD) {
    Acquire(thr, pc, (uptr)b);
  }
  return res;
}
#endif

TSAN_INTERCEPTOR(int, pthread_once, void *o, void (*f)()) {
  SCOPED_INTERCEPTOR_RAW(pthread_once, o, f);
  if (o == 0 || f == 0)
    return errno_EINVAL;
  atomic_uint32_t *a;

  if (SANITIZER_APPLE)
    a = static_cast<atomic_uint32_t*>((void *)((char *)o + sizeof(long_t)));
  else if (SANITIZER_NETBSD)
    a = static_cast<atomic_uint32_t*>
          ((void *)((char *)o + __sanitizer::pthread_mutex_t_sz));
  else
    a = static_cast<atomic_uint32_t*>(o);

  // Mac OS X appears to use pthread_once() where calling BlockingRegion hooks
  // result in crashes due to too little stack space.
  if (guard_acquire(thr, pc, a, !SANITIZER_APPLE)) {
    (*f)();
    guard_release(thr, pc, a, kGuardDone);
  }
  return 0;
}

#if SANITIZER_GLIBC
TSAN_INTERCEPTOR(int, __fxstat, int version, int fd, void *buf) {
  SCOPED_TSAN_INTERCEPTOR(__fxstat, version, fd, buf);
  if (fd > 0)
    FdAccess(thr, pc, fd);
  return REAL(__fxstat)(version, fd, buf);
}

TSAN_INTERCEPTOR(int, __fxstat64, int version, int fd, void *buf) {
  SCOPED_TSAN_INTERCEPTOR(__fxstat64, version, fd, buf);
  if (fd > 0)
    FdAccess(thr, pc, fd);
  return REAL(__fxstat64)(version, fd, buf);
}
#define TSAN_MAYBE_INTERCEPT___FXSTAT TSAN_INTERCEPT(__fxstat); TSAN_INTERCEPT(__fxstat64)
#else
#define TSAN_MAYBE_INTERCEPT___FXSTAT
#endif

#if !SANITIZER_GLIBC || __GLIBC_PREREQ(2, 33)
TSAN_INTERCEPTOR(int, fstat, int fd, void *buf) {
  SCOPED_TSAN_INTERCEPTOR(fstat, fd, buf);
  if (fd > 0)
    FdAccess(thr, pc, fd);
  return REAL(fstat)(fd, buf);
}
#  define TSAN_MAYBE_INTERCEPT_FSTAT TSAN_INTERCEPT(fstat)
#else
#  define TSAN_MAYBE_INTERCEPT_FSTAT
#endif

#if __GLIBC_PREREQ(2, 33)
TSAN_INTERCEPTOR(int, fstat64, int fd, void *buf) {
  SCOPED_TSAN_INTERCEPTOR(fstat64, fd, buf);
  if (fd > 0)
    FdAccess(thr, pc, fd);
  return REAL(fstat64)(fd, buf);
}
#  define TSAN_MAYBE_INTERCEPT_FSTAT64 TSAN_INTERCEPT(fstat64)
#else
#  define TSAN_MAYBE_INTERCEPT_FSTAT64
#endif

TSAN_INTERCEPTOR(int, open, const char *name, int oflag, ...) {
  va_list ap;
  va_start(ap, oflag);
  mode_t mode = va_arg(ap, int);
  va_end(ap);
  SCOPED_TSAN_INTERCEPTOR(open, name, oflag, mode);
  READ_STRING(thr, pc, name, 0);
  int fd = REAL(open)(name, oflag, mode);
  if (fd >= 0)
    FdFileCreate(thr, pc, fd);
  return fd;
}

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(int, open64, const char *name, int oflag, ...) {
  va_list ap;
  va_start(ap, oflag);
  mode_t mode = va_arg(ap, int);
  va_end(ap);
  SCOPED_TSAN_INTERCEPTOR(open64, name, oflag, mode);
  READ_STRING(thr, pc, name, 0);
  int fd = REAL(open64)(name, oflag, mode);
  if (fd >= 0)
    FdFileCreate(thr, pc, fd);
  return fd;
}
#define TSAN_MAYBE_INTERCEPT_OPEN64 TSAN_INTERCEPT(open64)
#else
#define TSAN_MAYBE_INTERCEPT_OPEN64
#endif

TSAN_INTERCEPTOR(int, creat, const char *name, int mode) {
  SCOPED_TSAN_INTERCEPTOR(creat, name, mode);
  READ_STRING(thr, pc, name, 0);
  int fd = REAL(creat)(name, mode);
  if (fd >= 0)
    FdFileCreate(thr, pc, fd);
  return fd;
}

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(int, creat64, const char *name, int mode) {
  SCOPED_TSAN_INTERCEPTOR(creat64, name, mode);
  READ_STRING(thr, pc, name, 0);
  int fd = REAL(creat64)(name, mode);
  if (fd >= 0)
    FdFileCreate(thr, pc, fd);
  return fd;
}
#define TSAN_MAYBE_INTERCEPT_CREAT64 TSAN_INTERCEPT(creat64)
#else
#define TSAN_MAYBE_INTERCEPT_CREAT64
#endif

TSAN_INTERCEPTOR(int, dup, int oldfd) {
  SCOPED_TSAN_INTERCEPTOR(dup, oldfd);
  int newfd = REAL(dup)(oldfd);
  if (oldfd >= 0 && newfd >= 0 && newfd != oldfd)
    FdDup(thr, pc, oldfd, newfd, true);
  return newfd;
}

TSAN_INTERCEPTOR(int, dup2, int oldfd, int newfd) {
  SCOPED_TSAN_INTERCEPTOR(dup2, oldfd, newfd);
  int newfd2 = REAL(dup2)(oldfd, newfd);
  if (oldfd >= 0 && newfd2 >= 0 && newfd2 != oldfd)
    FdDup(thr, pc, oldfd, newfd2, false);
  return newfd2;
}

#if !SANITIZER_APPLE
TSAN_INTERCEPTOR(int, dup3, int oldfd, int newfd, int flags) {
  SCOPED_TSAN_INTERCEPTOR(dup3, oldfd, newfd, flags);
  int newfd2 = REAL(dup3)(oldfd, newfd, flags);
  if (oldfd >= 0 && newfd2 >= 0 && newfd2 != oldfd)
    FdDup(thr, pc, oldfd, newfd2, false);
  return newfd2;
}
#endif

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(int, eventfd, unsigned initval, int flags) {
  SCOPED_TSAN_INTERCEPTOR(eventfd, initval, flags);
  int fd = REAL(eventfd)(initval, flags);
  if (fd >= 0)
    FdEventCreate(thr, pc, fd);
  return fd;
}
#define TSAN_MAYBE_INTERCEPT_EVENTFD TSAN_INTERCEPT(eventfd)
#else
#define TSAN_MAYBE_INTERCEPT_EVENTFD
#endif

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(int, signalfd, int fd, void *mask, int flags) {
  SCOPED_INTERCEPTOR_RAW(signalfd, fd, mask, flags);
  FdClose(thr, pc, fd);
  fd = REAL(signalfd)(fd, mask, flags);
  if (!MustIgnoreInterceptor(thr))
    FdSignalCreate(thr, pc, fd);
  return fd;
}
#define TSAN_MAYBE_INTERCEPT_SIGNALFD TSAN_INTERCEPT(signalfd)
#else
#define TSAN_MAYBE_INTERCEPT_SIGNALFD
#endif

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(int, inotify_init, int fake) {
  SCOPED_TSAN_INTERCEPTOR(inotify_init, fake);
  int fd = REAL(inotify_init)(fake);
  if (fd >= 0)
    FdInotifyCreate(thr, pc, fd);
  return fd;
}
#define TSAN_MAYBE_INTERCEPT_INOTIFY_INIT TSAN_INTERCEPT(inotify_init)
#else
#define TSAN_MAYBE_INTERCEPT_INOTIFY_INIT
#endif

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(int, inotify_init1, int flags) {
  SCOPED_TSAN_INTERCEPTOR(inotify_init1, flags);
  int fd = REAL(inotify_init1)(flags);
  if (fd >= 0)
    FdInotifyCreate(thr, pc, fd);
  return fd;
}
#define TSAN_MAYBE_INTERCEPT_INOTIFY_INIT1 TSAN_INTERCEPT(inotify_init1)
#else
#define TSAN_MAYBE_INTERCEPT_INOTIFY_INIT1
#endif

TSAN_INTERCEPTOR(int, socket, int domain, int type, int protocol) {
  SCOPED_TSAN_INTERCEPTOR(socket, domain, type, protocol);
  int fd = REAL(socket)(domain, type, protocol);
  if (fd >= 0)
    FdSocketCreate(thr, pc, fd);
  return fd;
}

TSAN_INTERCEPTOR(int, socketpair, int domain, int type, int protocol, int *fd) {
  SCOPED_TSAN_INTERCEPTOR(socketpair, domain, type, protocol, fd);
  int res = REAL(socketpair)(domain, type, protocol, fd);
  if (res == 0 && fd[0] >= 0 && fd[1] >= 0)
    FdPipeCreate(thr, pc, fd[0], fd[1]);
  return res;
}

TSAN_INTERCEPTOR(int, connect, int fd, void *addr, unsigned addrlen) {
  SCOPED_TSAN_INTERCEPTOR(connect, fd, addr, addrlen);
  FdSocketConnecting(thr, pc, fd);
  int res = REAL(connect)(fd, addr, addrlen);
  if (res == 0 && fd >= 0)
    FdSocketConnect(thr, pc, fd);
  return res;
}

TSAN_INTERCEPTOR(int, bind, int fd, void *addr, unsigned addrlen) {
  SCOPED_TSAN_INTERCEPTOR(bind, fd, addr, addrlen);
  int res = REAL(bind)(fd, addr, addrlen);
  if (fd > 0 && res == 0)
    FdAccess(thr, pc, fd);
  return res;
}

TSAN_INTERCEPTOR(int, listen, int fd, int backlog) {
  SCOPED_TSAN_INTERCEPTOR(listen, fd, backlog);
  int res = REAL(listen)(fd, backlog);
  if (fd > 0 && res == 0)
    FdAccess(thr, pc, fd);
  return res;
}

TSAN_INTERCEPTOR(int, close, int fd) {
  SCOPED_INTERCEPTOR_RAW(close, fd);
  if (!in_symbolizer())
    FdClose(thr, pc, fd);
  return REAL(close)(fd);
}

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(int, __close, int fd) {
  SCOPED_INTERCEPTOR_RAW(__close, fd);
  FdClose(thr, pc, fd);
  return REAL(__close)(fd);
}
#define TSAN_MAYBE_INTERCEPT___CLOSE TSAN_INTERCEPT(__close)
#else
#define TSAN_MAYBE_INTERCEPT___CLOSE
#endif

// glibc guts
#if SANITIZER_LINUX && !SANITIZER_ANDROID
TSAN_INTERCEPTOR(void, __res_iclose, void *state, bool free_addr) {
  SCOPED_INTERCEPTOR_RAW(__res_iclose, state, free_addr);
  int fds[64];
  int cnt = ExtractResolvFDs(state, fds, ARRAY_SIZE(fds));
  for (int i = 0; i < cnt; i++) FdClose(thr, pc, fds[i]);
  REAL(__res_iclose)(state, free_addr);
}
#define TSAN_MAYBE_INTERCEPT___RES_ICLOSE TSAN_INTERCEPT(__res_iclose)
#else
#define TSAN_MAYBE_INTERCEPT___RES_ICLOSE
#endif

TSAN_INTERCEPTOR(int, pipe, int *pipefd) {
  SCOPED_TSAN_INTERCEPTOR(pipe, pipefd);
  int res = REAL(pipe)(pipefd);
  if (res == 0 && pipefd[0] >= 0 && pipefd[1] >= 0)
    FdPipeCreate(thr, pc, pipefd[0], pipefd[1]);
  return res;
}

#if !SANITIZER_APPLE
TSAN_INTERCEPTOR(int, pipe2, int *pipefd, int flags) {
  SCOPED_TSAN_INTERCEPTOR(pipe2, pipefd, flags);
  int res = REAL(pipe2)(pipefd, flags);
  if (res == 0 && pipefd[0] >= 0 && pipefd[1] >= 0)
    FdPipeCreate(thr, pc, pipefd[0], pipefd[1]);
  return res;
}
#endif

TSAN_INTERCEPTOR(int, unlink, char *path) {
  SCOPED_TSAN_INTERCEPTOR(unlink, path);
  Release(thr, pc, File2addr(path));
  int res = REAL(unlink)(path);
  return res;
}

TSAN_INTERCEPTOR(void*, tmpfile, int fake) {
  SCOPED_TSAN_INTERCEPTOR(tmpfile, fake);
  void *res = REAL(tmpfile)(fake);
  if (res) {
    int fd = fileno_unlocked(res);
    if (fd >= 0)
      FdFileCreate(thr, pc, fd);
  }
  return res;
}

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(void*, tmpfile64, int fake) {
  SCOPED_TSAN_INTERCEPTOR(tmpfile64, fake);
  void *res = REAL(tmpfile64)(fake);
  if (res) {
    int fd = fileno_unlocked(res);
    if (fd >= 0)
      FdFileCreate(thr, pc, fd);
  }
  return res;
}
#define TSAN_MAYBE_INTERCEPT_TMPFILE64 TSAN_INTERCEPT(tmpfile64)
#else
#define TSAN_MAYBE_INTERCEPT_TMPFILE64
#endif

static void FlushStreams() {
  // Flushing all the streams here may freeze the process if a child thread is
  // performing file stream operations at the same time.
  REAL(fflush)(stdout);
  REAL(fflush)(stderr);
}

TSAN_INTERCEPTOR(void, abort, int fake) {
  SCOPED_TSAN_INTERCEPTOR(abort, fake);
  FlushStreams();
  REAL(abort)(fake);
}

TSAN_INTERCEPTOR(int, rmdir, char *path) {
  SCOPED_TSAN_INTERCEPTOR(rmdir, path);
  Release(thr, pc, Dir2addr(path));
  int res = REAL(rmdir)(path);
  return res;
}

TSAN_INTERCEPTOR(int, closedir, void *dirp) {
  SCOPED_INTERCEPTOR_RAW(closedir, dirp);
  if (dirp) {
    int fd = dirfd(dirp);
    FdClose(thr, pc, fd);
  }
  return REAL(closedir)(dirp);
}

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(int, epoll_create, int size) {
  SCOPED_TSAN_INTERCEPTOR(epoll_create, size);
  int fd = REAL(epoll_create)(size);
  if (fd >= 0)
    FdPollCreate(thr, pc, fd);
  return fd;
}

TSAN_INTERCEPTOR(int, epoll_create1, int flags) {
  SCOPED_TSAN_INTERCEPTOR(epoll_create1, flags);
  int fd = REAL(epoll_create1)(flags);
  if (fd >= 0)
    FdPollCreate(thr, pc, fd);
  return fd;
}

TSAN_INTERCEPTOR(int, epoll_ctl, int epfd, int op, int fd, void *ev) {
  SCOPED_TSAN_INTERCEPTOR(epoll_ctl, epfd, op, fd, ev);
  if (epfd >= 0)
    FdAccess(thr, pc, epfd);
  if (epfd >= 0 && fd >= 0)
    FdAccess(thr, pc, fd);
  if (op == EPOLL_CTL_ADD && epfd >= 0) {
    FdPollAdd(thr, pc, epfd, fd);
    FdRelease(thr, pc, epfd);
  }
  int res = REAL(epoll_ctl)(epfd, op, fd, ev);
  return res;
}

TSAN_INTERCEPTOR(int, epoll_wait, int epfd, void *ev, int cnt, int timeout) {
  SCOPED_TSAN_INTERCEPTOR(epoll_wait, epfd, ev, cnt, timeout);
  if (epfd >= 0)
    FdAccess(thr, pc, epfd);
  int res = BLOCK_REAL(epoll_wait)(epfd, ev, cnt, timeout);
  if (res > 0 && epfd >= 0)
    FdAcquire(thr, pc, epfd);
  return res;
}

TSAN_INTERCEPTOR(int, epoll_pwait, int epfd, void *ev, int cnt, int timeout,
                 void *sigmask) {
  SCOPED_TSAN_INTERCEPTOR(epoll_pwait, epfd, ev, cnt, timeout, sigmask);
  if (epfd >= 0)
    FdAccess(thr, pc, epfd);
  int res = BLOCK_REAL(epoll_pwait)(epfd, ev, cnt, timeout, sigmask);
  if (res > 0 && epfd >= 0)
    FdAcquire(thr, pc, epfd);
  return res;
}

TSAN_INTERCEPTOR(int, epoll_pwait2, int epfd, void *ev, int cnt, void *timeout,
                 void *sigmask) {
  SCOPED_INTERCEPTOR_RAW(epoll_pwait2, epfd, ev, cnt, timeout, sigmask);
  // This function is new and may not be present in libc and/or kernel.
  // Since we effectively add it to libc (as will be probed by the program
  // using dlsym or a weak function pointer) we need to handle the case
  // when it's not present in the actual libc.
  if (!REAL(epoll_pwait2)) {
    errno = errno_ENOSYS;
    return -1;
  }
  if (MustIgnoreInterceptor(thr))
    REAL(epoll_pwait2)(epfd, ev, cnt, timeout, sigmask);
  if (epfd >= 0)
    FdAccess(thr, pc, epfd);
  int res = BLOCK_REAL(epoll_pwait2)(epfd, ev, cnt, timeout, sigmask);
  if (res > 0 && epfd >= 0)
    FdAcquire(thr, pc, epfd);
  return res;
}

#  define TSAN_MAYBE_INTERCEPT_EPOLL \
    TSAN_INTERCEPT(epoll_create);    \
    TSAN_INTERCEPT(epoll_create1);   \
    TSAN_INTERCEPT(epoll_ctl);       \
    TSAN_INTERCEPT(epoll_wait);      \
    TSAN_INTERCEPT(epoll_pwait);     \
    TSAN_INTERCEPT(epoll_pwait2)
#else
#define TSAN_MAYBE_INTERCEPT_EPOLL
#endif

// The following functions are intercepted merely to process pending signals.
// If program blocks signal X, we must deliver the signal before the function
// returns. Similarly, if program unblocks a signal (or returns from sigsuspend)
// it's better to deliver the signal straight away.
TSAN_INTERCEPTOR(int, sigsuspend, const __sanitizer_sigset_t *mask) {
  SCOPED_TSAN_INTERCEPTOR(sigsuspend, mask);
  return REAL(sigsuspend)(mask);
}

TSAN_INTERCEPTOR(int, sigblock, int mask) {
  SCOPED_TSAN_INTERCEPTOR(sigblock, mask);
  return REAL(sigblock)(mask);
}

TSAN_INTERCEPTOR(int, sigsetmask, int mask) {
  SCOPED_TSAN_INTERCEPTOR(sigsetmask, mask);
  return REAL(sigsetmask)(mask);
}

TSAN_INTERCEPTOR(int, pthread_sigmask, int how, const __sanitizer_sigset_t *set,
    __sanitizer_sigset_t *oldset) {
  SCOPED_TSAN_INTERCEPTOR(pthread_sigmask, how, set, oldset);
  return REAL(pthread_sigmask)(how, set, oldset);
}

namespace __tsan {

static void ReportErrnoSpoiling(ThreadState *thr, uptr pc, int sig) {
  VarSizeStackTrace stack;
  // StackTrace::GetNestInstructionPc(pc) is used because return address is
  // expected, OutputReport() will undo this.
  ObtainCurrentStack(thr, StackTrace::GetNextInstructionPc(pc), &stack);
  ThreadRegistryLock l(&ctx->thread_registry);
  ScopedReport rep(ReportTypeErrnoInSignal);
  rep.SetSigNum(sig);
  if (!IsFiredSuppression(ctx, ReportTypeErrnoInSignal, stack)) {
    rep.AddStack(stack, true);
    OutputReport(thr, rep);
  }
}

static void CallUserSignalHandler(ThreadState *thr, bool sync, bool acquire,
                                  int sig, __sanitizer_siginfo *info,
                                  void *uctx) {
  CHECK(thr->slot);
  __sanitizer_sigaction *sigactions = interceptor_ctx()->sigactions;
  if (acquire)
    Acquire(thr, 0, (uptr)&sigactions[sig]);
  // Signals are generally asynchronous, so if we receive a signals when
  // ignores are enabled we should disable ignores. This is critical for sync
  // and interceptors, because otherwise we can miss synchronization and report
  // false races.
  int ignore_reads_and_writes = thr->ignore_reads_and_writes;
  int ignore_interceptors = thr->ignore_interceptors;
  int ignore_sync = thr->ignore_sync;
  // For symbolizer we only process SIGSEGVs synchronously
  // (bug in symbolizer or in tsan). But we want to reset
  // in_symbolizer to fail gracefully. Symbolizer and user code
  // use different memory allocators, so if we don't reset
  // in_symbolizer we can get memory allocated with one being
  // feed with another, which can cause more crashes.
  int in_symbolizer = thr->in_symbolizer;
  if (!ctx->after_multithreaded_fork) {
    thr->ignore_reads_and_writes = 0;
    thr->fast_state.ClearIgnoreBit();
    thr->ignore_interceptors = 0;
    thr->ignore_sync = 0;
    thr->in_symbolizer = 0;
  }
  // Ensure that the handler does not spoil errno.
  const int saved_errno = errno;
  errno = 99;
  // This code races with sigaction. Be careful to not read sa_sigaction twice.
  // Also need to remember pc for reporting before the call,
  // because the handler can reset it.
  volatile uptr pc = (sigactions[sig].sa_flags & SA_SIGINFO)
                         ? (uptr)sigactions[sig].sigaction
                         : (uptr)sigactions[sig].handler;
  if (pc != sig_dfl && pc != sig_ign) {
    // The callback can be either sa_handler or sa_sigaction.
    // They have different signatures, but we assume that passing
    // additional arguments to sa_handler works and is harmless.
    ((__sanitizer_sigactionhandler_ptr)pc)(sig, info, uctx);
  }
  if (!ctx->after_multithreaded_fork) {
    thr->ignore_reads_and_writes = ignore_reads_and_writes;
    if (ignore_reads_and_writes)
      thr->fast_state.SetIgnoreBit();
    thr->ignore_interceptors = ignore_interceptors;
    thr->ignore_sync = ignore_sync;
    thr->in_symbolizer = in_symbolizer;
  }
  // We do not detect errno spoiling for SIGTERM,
  // because some SIGTERM handlers do spoil errno but reraise SIGTERM,
  // tsan reports false positive in such case.
  // It's difficult to properly detect this situation (reraise),
  // because in async signal processing case (when handler is called directly
  // from rtl_generic_sighandler) we have not yet received the reraised
  // signal; and it looks too fragile to intercept all ways to reraise a signal.
  if (ShouldReport(thr, ReportTypeErrnoInSignal) && !sync && sig != SIGTERM &&
      errno != 99)
    ReportErrnoSpoiling(thr, pc, sig);
  errno = saved_errno;
}

void ProcessPendingSignalsImpl(ThreadState *thr) {
  atomic_store(&thr->pending_signals, 0, memory_order_relaxed);
  ThreadSignalContext *sctx = SigCtx(thr);
  if (sctx == 0)
    return;
  atomic_fetch_add(&thr->in_signal_handler, 1, memory_order_relaxed);
  internal_sigfillset(&sctx->emptyset);
  int res = REAL(pthread_sigmask)(SIG_SETMASK, &sctx->emptyset, &sctx->oldset);
  CHECK_EQ(res, 0);
  for (int sig = 0; sig < kSigCount; sig++) {
    SignalDesc *signal = &sctx->pending_signals[sig];
    if (signal->armed) {
      signal->armed = false;
      CallUserSignalHandler(thr, false, true, sig, &signal->siginfo,
                            &signal->ctx);
    }
  }
  res = REAL(pthread_sigmask)(SIG_SETMASK, &sctx->oldset, 0);
  CHECK_EQ(res, 0);
  atomic_fetch_add(&thr->in_signal_handler, -1, memory_order_relaxed);
}

}  // namespace __tsan

static bool is_sync_signal(ThreadSignalContext *sctx, int sig,
                           __sanitizer_siginfo *info) {
  // If we are sending signal to ourselves, we must process it now.
  if (sctx && sig == sctx->int_signal_send)
    return true;
#if SANITIZER_HAS_SIGINFO
  // POSIX timers can be configured to send any kind of signal; however, it
  // doesn't make any sense to consider a timer signal as synchronous!
  if (info->si_code == SI_TIMER)
    return false;
#endif
  return sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGTRAP ||
         sig == SIGABRT || sig == SIGFPE || sig == SIGPIPE || sig == SIGSYS;
}

void sighandler(int sig, __sanitizer_siginfo *info, void *ctx) {
  ThreadState *thr = cur_thread_init();
  ThreadSignalContext *sctx = SigCtx(thr);
  if (sig < 0 || sig >= kSigCount) {
    VPrintf(1, "ThreadSanitizer: ignoring signal %d\n", sig);
    return;
  }
  // Don't mess with synchronous signals.
  const bool sync = is_sync_signal(sctx, sig, info);
  if (sync ||
      // If we are in blocking function, we can safely process it now
      // (but check if we are in a recursive interceptor,
      // i.e. pthread_join()->munmap()).
      atomic_load(&thr->in_blocking_func, memory_order_relaxed)) {
    atomic_fetch_add(&thr->in_signal_handler, 1, memory_order_relaxed);
    if (atomic_load(&thr->in_blocking_func, memory_order_relaxed)) {
      atomic_store(&thr->in_blocking_func, 0, memory_order_relaxed);
      CallUserSignalHandler(thr, sync, true, sig, info, ctx);
      atomic_store(&thr->in_blocking_func, 1, memory_order_relaxed);
    } else {
      // Be very conservative with when we do acquire in this case.
      // It's unsafe to do acquire in async handlers, because ThreadState
      // can be in inconsistent state.
      // SIGSYS looks relatively safe -- it's synchronous and can actually
      // need some global state.
      bool acq = (sig == SIGSYS);
      CallUserSignalHandler(thr, sync, acq, sig, info, ctx);
    }
    atomic_fetch_add(&thr->in_signal_handler, -1, memory_order_relaxed);
    return;
  }

  if (sctx == 0)
    return;
  SignalDesc *signal = &sctx->pending_signals[sig];
  if (signal->armed == false) {
    signal->armed = true;
    internal_memcpy(&signal->siginfo, info, sizeof(*info));
    internal_memcpy(&signal->ctx, ctx, sizeof(signal->ctx));
    atomic_store(&thr->pending_signals, 1, memory_order_relaxed);
  }
}

TSAN_INTERCEPTOR(int, raise, int sig) {
  SCOPED_TSAN_INTERCEPTOR(raise, sig);
  ThreadSignalContext *sctx = SigCtx(thr);
  CHECK_NE(sctx, 0);
  int prev = sctx->int_signal_send;
  sctx->int_signal_send = sig;
  int res = REAL(raise)(sig);
  CHECK_EQ(sctx->int_signal_send, sig);
  sctx->int_signal_send = prev;
  return res;
}

TSAN_INTERCEPTOR(int, kill, int pid, int sig) {
  SCOPED_TSAN_INTERCEPTOR(kill, pid, sig);
  ThreadSignalContext *sctx = SigCtx(thr);
  CHECK_NE(sctx, 0);
  int prev = sctx->int_signal_send;
  if (pid == (int)internal_getpid()) {
    sctx->int_signal_send = sig;
  }
  int res = REAL(kill)(pid, sig);
  if (pid == (int)internal_getpid()) {
    CHECK_EQ(sctx->int_signal_send, sig);
    sctx->int_signal_send = prev;
  }
  return res;
}

TSAN_INTERCEPTOR(int, pthread_kill, void *tid, int sig) {
  SCOPED_TSAN_INTERCEPTOR(pthread_kill, tid, sig);
  ThreadSignalContext *sctx = SigCtx(thr);
  CHECK_NE(sctx, 0);
  int prev = sctx->int_signal_send;
  bool self = pthread_equal(tid, pthread_self());
  if (self)
    sctx->int_signal_send = sig;
  int res = REAL(pthread_kill)(tid, sig);
  if (self) {
    CHECK_EQ(sctx->int_signal_send, sig);
    sctx->int_signal_send = prev;
  }
  return res;
}

TSAN_INTERCEPTOR(int, gettimeofday, void *tv, void *tz) {
  SCOPED_TSAN_INTERCEPTOR(gettimeofday, tv, tz);
  // It's intercepted merely to process pending signals.
  return REAL(gettimeofday)(tv, tz);
}

TSAN_INTERCEPTOR(int, getaddrinfo, void *node, void *service,
    void *hints, void *rv) {
  SCOPED_TSAN_INTERCEPTOR(getaddrinfo, node, service, hints, rv);
  // We miss atomic synchronization in getaddrinfo,
  // and can report false race between malloc and free
  // inside of getaddrinfo. So ignore memory accesses.
  ThreadIgnoreBegin(thr, pc);
  int res = REAL(getaddrinfo)(node, service, hints, rv);
  ThreadIgnoreEnd(thr);
  return res;
}

TSAN_INTERCEPTOR(int, fork, int fake) {
  if (in_symbolizer())
    return REAL(fork)(fake);
  SCOPED_INTERCEPTOR_RAW(fork, fake);
  return REAL(fork)(fake);
}

void atfork_prepare() {
  if (in_symbolizer())
    return;
  ThreadState *thr = cur_thread();
  const uptr pc = StackTrace::GetCurrentPc();
  ForkBefore(thr, pc);
}

void atfork_parent() {
  if (in_symbolizer())
    return;
  ThreadState *thr = cur_thread();
  const uptr pc = StackTrace::GetCurrentPc();
  ForkParentAfter(thr, pc);
}

void atfork_child() {
  if (in_symbolizer())
    return;
  ThreadState *thr = cur_thread();
  const uptr pc = StackTrace::GetCurrentPc();
  ForkChildAfter(thr, pc, true);
  FdOnFork(thr, pc);
}

#if !SANITIZER_IOS
TSAN_INTERCEPTOR(int, vfork, int fake) {
  // Some programs (e.g. openjdk) call close for all file descriptors
  // in the child process. Under tsan it leads to false positives, because
  // address space is shared, so the parent process also thinks that
  // the descriptors are closed (while they are actually not).
  // This leads to false positives due to missed synchronization.
  // Strictly saying this is undefined behavior, because vfork child is not
  // allowed to call any functions other than exec/exit. But this is what
  // openjdk does, so we want to handle it.
  // We could disable interceptors in the child process. But it's not possible
  // to simply intercept and wrap vfork, because vfork child is not allowed
  // to return from the function that calls vfork, and that's exactly what
  // we would do. So this would require some assembly trickery as well.
  // Instead we simply turn vfork into fork.
  return WRAP(fork)(fake);
}
#endif

#if SANITIZER_LINUX
TSAN_INTERCEPTOR(int, clone, int (*fn)(void *), void *stack, int flags,
                 void *arg, int *parent_tid, void *tls, pid_t *child_tid) {
  SCOPED_INTERCEPTOR_RAW(clone, fn, stack, flags, arg, parent_tid, tls,
                         child_tid);
  struct Arg {
    int (*fn)(void *);
    void *arg;
  };
  auto wrapper = +[](void *p) -> int {
    auto *thr = cur_thread();
    uptr pc = GET_CURRENT_PC();
    // Start the background thread for fork, but not for clone.
    // For fork we did this always and it's known to work (or user code has
    // adopted). But if we do this for the new clone interceptor some code
    // (sandbox2) fails. So model we used to do for years and don't start the
    // background thread after clone.
    ForkChildAfter(thr, pc, false);
    FdOnFork(thr, pc);
    auto *arg = static_cast<Arg *>(p);
    return arg->fn(arg->arg);
  };
  ForkBefore(thr, pc);
  Arg arg_wrapper = {fn, arg};
  int pid = REAL(clone)(wrapper, stack, flags, &arg_wrapper, parent_tid, tls,
                        child_tid);
  ForkParentAfter(thr, pc);
  return pid;
}
#endif

#if !SANITIZER_APPLE && !SANITIZER_ANDROID
typedef int (*dl_iterate_phdr_cb_t)(__sanitizer_dl_phdr_info *info, SIZE_T size,
                                    void *data);
struct dl_iterate_phdr_data {
  ThreadState *thr;
  uptr pc;
  dl_iterate_phdr_cb_t cb;
  void *data;
};

static bool IsAppNotRodata(uptr addr) {
  return IsAppMem(addr) && *MemToShadow(addr) != Shadow::kRodata;
}

static int dl_iterate_phdr_cb(__sanitizer_dl_phdr_info *info, SIZE_T size,
                              void *data) {
  dl_iterate_phdr_data *cbdata = (dl_iterate_phdr_data *)data;
  // dlopen/dlclose allocate/free dynamic-linker-internal memory, which is later
  // accessible in dl_iterate_phdr callback. But we don't see synchronization
  // inside of dynamic linker, so we "unpoison" it here in order to not
  // produce false reports. Ignoring malloc/free in dlopen/dlclose is not enough
  // because some libc functions call __libc_dlopen.
  if (info && IsAppNotRodata((uptr)info->dlpi_name))
    MemoryResetRange(cbdata->thr, cbdata->pc, (uptr)info->dlpi_name,
                     internal_strlen(info->dlpi_name));
  int res = cbdata->cb(info, size, cbdata->data);
  // Perform the check one more time in case info->dlpi_name was overwritten
  // by user callback.
  if (info && IsAppNotRodata((uptr)info->dlpi_name))
    MemoryResetRange(cbdata->thr, cbdata->pc, (uptr)info->dlpi_name,
                     internal_strlen(info->dlpi_name));
  return res;
}

TSAN_INTERCEPTOR(int, dl_iterate_phdr, dl_iterate_phdr_cb_t cb, void *data) {
  SCOPED_TSAN_INTERCEPTOR(dl_iterate_phdr, cb, data);
  dl_iterate_phdr_data cbdata;
  cbdata.thr = thr;
  cbdata.pc = pc;
  cbdata.cb = cb;
  cbdata.data = data;
  int res = REAL(dl_iterate_phdr)(dl_iterate_phdr_cb, &cbdata);
  return res;
}
#endif

static int OnExit(ThreadState *thr) {
  int status = Finalize(thr);
  FlushStreams();
  return status;
}

#if !SANITIZER_APPLE
static void HandleRecvmsg(ThreadState *thr, uptr pc,
    __sanitizer_msghdr *msg) {
  int fds[64];
  int cnt = ExtractRecvmsgFDs(msg, fds, ARRAY_SIZE(fds));
  for (int i = 0; i < cnt; i++)
    FdEventCreate(thr, pc, fds[i]);
}
#endif

#include "sanitizer_common/sanitizer_platform_interceptors.h"
// Causes interceptor recursion (getaddrinfo() and fopen())
#undef SANITIZER_INTERCEPT_GETADDRINFO
// We define our own.
#if SANITIZER_INTERCEPT_TLS_GET_ADDR
#define NEED_TLS_GET_ADDR
#endif
#undef SANITIZER_INTERCEPT_TLS_GET_ADDR
#define SANITIZER_INTERCEPT_TLS_GET_OFFSET 1
#undef SANITIZER_INTERCEPT_PTHREAD_SIGMASK

#define COMMON_INTERCEPT_FUNCTION_VER(name, ver)                          \
  INTERCEPT_FUNCTION_VER(name, ver)
#define COMMON_INTERCEPT_FUNCTION_VER_UNVERSIONED_FALLBACK(name, ver) \
  (INTERCEPT_FUNCTION_VER(name, ver) || INTERCEPT_FUNCTION(name))

#define COMMON_INTERCEPTOR_ENTER_NOIGNORE(ctx, func, ...) \
  SCOPED_INTERCEPTOR_RAW(func, __VA_ARGS__);              \
  TsanInterceptorContext _ctx = {thr, pc};                \
  ctx = (void *)&_ctx;                                    \
  (void)ctx;

#define COMMON_INTERCEPTOR_FILE_OPEN(ctx, file, path) \
  if (path)                                           \
    Acquire(thr, pc, File2addr(path));                \
  if (file) {                                         \
    int fd = fileno_unlocked(file);                   \
    if (fd >= 0) FdFileCreate(thr, pc, fd);           \
  }

#define COMMON_INTERCEPTOR_FILE_CLOSE(ctx, file) \
  if (file) {                                    \
    int fd = fileno_unlocked(file);              \
    FdClose(thr, pc, fd);                        \
  }

#define COMMON_INTERCEPTOR_DLOPEN(filename, flag) \
  ({                                              \
    CheckNoDeepBind(filename, flag);              \
    ThreadIgnoreBegin(thr, 0);                    \
    void *res = REAL(dlopen)(filename, flag);     \
    ThreadIgnoreEnd(thr);                         \
    res;                                          \
  })

// Ignore interceptors in OnLibraryLoaded()/Unloaded().  These hooks use code
// (ListOfModules::init, MemoryMappingLayout::DumpListOfModules) that make
// intercepted calls, which can cause deadlockes with ReportRace() which also
// uses this code.
#define COMMON_INTERCEPTOR_LIBRARY_LOADED(filename, handle) \
  ({                                                        \
    ScopedIgnoreInterceptors ignore_interceptors;           \
    libignore()->OnLibraryLoaded(filename);                 \
  })

#define COMMON_INTERCEPTOR_LIBRARY_UNLOADED()     \
  ({                                              \
    ScopedIgnoreInterceptors ignore_interceptors; \
    libignore()->OnLibraryUnloaded();             \
  })

#define COMMON_INTERCEPTOR_ACQUIRE(ctx, u) \
  Acquire(((TsanInterceptorContext *) ctx)->thr, pc, u)

#define COMMON_INTERCEPTOR_RELEASE(ctx, u) \
  Release(((TsanInterceptorContext *) ctx)->thr, pc, u)

#define COMMON_INTERCEPTOR_DIR_ACQUIRE(ctx, path) \
  Acquire(((TsanInterceptorContext *) ctx)->thr, pc, Dir2addr(path))

#define COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd) \
  FdAcquire(((TsanInterceptorContext *) ctx)->thr, pc, fd)

#define COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd) \
  FdRelease(((TsanInterceptorContext *) ctx)->thr, pc, fd)

#define COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd) \
  FdAccess(((TsanInterceptorContext *) ctx)->thr, pc, fd)

#define COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, newfd) \
  FdSocketAccept(((TsanInterceptorContext *) ctx)->thr, pc, fd, newfd)

#define COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, name) \
  ThreadSetName(((TsanInterceptorContext *) ctx)->thr, name)

#define COMMON_INTERCEPTOR_SET_PTHREAD_NAME(ctx, thread, name)         \
  if (pthread_equal(pthread_self(), reinterpret_cast<void *>(thread))) \
    COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, name);                     \
  else                                                                 \
    __tsan::ctx->thread_registry.SetThreadNameByUserId(thread, name)

#define COMMON_INTERCEPTOR_BLOCK_REAL(name) BLOCK_REAL(name)

#define COMMON_INTERCEPTOR_ON_EXIT(ctx) \
  OnExit(((TsanInterceptorContext *) ctx)->thr)

#define COMMON_INTERCEPTOR_MMAP_IMPL(ctx, mmap, addr, sz, prot, flags, fd,  \
                                     off)                                   \
  do {                                                                      \
    return mmap_interceptor(thr, pc, REAL(mmap), addr, sz, prot, flags, fd, \
                            off);                                           \
  } while (false)

#define COMMON_INTERCEPTOR_MUNMAP_IMPL(ctx, addr, sz)           \
  do {                                                          \
    return munmap_interceptor(thr, pc, REAL(munmap), addr, sz); \
  } while (false)

#if !SANITIZER_APPLE
#define COMMON_INTERCEPTOR_HANDLE_RECVMSG(ctx, msg) \
  HandleRecvmsg(((TsanInterceptorContext *)ctx)->thr, \
      ((TsanInterceptorContext *)ctx)->pc, msg)
#endif

#define COMMON_INTERCEPTOR_GET_TLS_RANGE(begin, end)                           \
  if (TsanThread *t = GetCurrentThread()) {                                    \
    *begin = t->tls_begin();                                                   \
    *end = t->tls_end();                                                       \
  } else {                                                                     \
    *begin = *end = 0;                                                         \
  }

#define COMMON_INTERCEPTOR_USER_CALLBACK_START() \
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START()

#define COMMON_INTERCEPTOR_USER_CALLBACK_END() \
  SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END()

#include "sanitizer_common/sanitizer_common_interceptors.inc"

static int sigaction_impl(int sig, const __sanitizer_sigaction *act,
                          __sanitizer_sigaction *old);
static __sanitizer_sighandler_ptr signal_impl(int sig,
                                              __sanitizer_sighandler_ptr h);

#define SIGNAL_INTERCEPTOR_SIGACTION_IMPL(signo, act, oldact) \
  { return sigaction_impl(signo, act, oldact); }

#define SIGNAL_INTERCEPTOR_SIGNAL_IMPL(func, signo, handler) \
  { return (uptr)signal_impl(signo, (__sanitizer_sighandler_ptr)handler); }

#define SIGNAL_INTERCEPTOR_ENTER() LazyInitialize(cur_thread_init())

#include "sanitizer_common/sanitizer_signal_interceptors.inc"

int sigaction_impl(int sig, const __sanitizer_sigaction *act,
                   __sanitizer_sigaction *old) {
  // Note: if we call REAL(sigaction) directly for any reason without proxying
  // the signal handler through sighandler, very bad things will happen.
  // The handler will run synchronously and corrupt tsan per-thread state.
  SCOPED_INTERCEPTOR_RAW(sigaction, sig, act, old);
  if (sig <= 0 || sig >= kSigCount) {
    errno = errno_EINVAL;
    return -1;
  }
  __sanitizer_sigaction *sigactions = interceptor_ctx()->sigactions;
  __sanitizer_sigaction old_stored;
  if (old) internal_memcpy(&old_stored, &sigactions[sig], sizeof(old_stored));
  __sanitizer_sigaction newact;
  if (act) {
    // Copy act into sigactions[sig].
    // Can't use struct copy, because compiler can emit call to memcpy.
    // Can't use internal_memcpy, because it copies byte-by-byte,
    // and signal handler reads the handler concurrently. It can read
    // some bytes from old value and some bytes from new value.
    // Use volatile to prevent insertion of memcpy.
    sigactions[sig].handler =
        *(volatile __sanitizer_sighandler_ptr const *)&act->handler;
    sigactions[sig].sa_flags = *(volatile int const *)&act->sa_flags;
    internal_memcpy(&sigactions[sig].sa_mask, &act->sa_mask,
                    sizeof(sigactions[sig].sa_mask));
#if !SANITIZER_FREEBSD && !SANITIZER_APPLE && !SANITIZER_NETBSD
    sigactions[sig].sa_restorer = act->sa_restorer;
#endif
    internal_memcpy(&newact, act, sizeof(newact));
    internal_sigfillset(&newact.sa_mask);
    if ((act->sa_flags & SA_SIGINFO) ||
        ((uptr)act->handler != sig_ign && (uptr)act->handler != sig_dfl)) {
      newact.sa_flags |= SA_SIGINFO;
      newact.sigaction = sighandler;
    }
    ReleaseStore(thr, pc, (uptr)&sigactions[sig]);
    act = &newact;
  }
  int res = REAL(sigaction)(sig, act, old);
  if (res == 0 && old && old->sigaction == sighandler)
    internal_memcpy(old, &old_stored, sizeof(*old));
  return res;
}

static __sanitizer_sighandler_ptr signal_impl(int sig,
                                              __sanitizer_sighandler_ptr h) {
  __sanitizer_sigaction act;
  act.handler = h;
  internal_memset(&act.sa_mask, -1, sizeof(act.sa_mask));
  act.sa_flags = 0;
  __sanitizer_sigaction old;
  int res = sigaction_symname(sig, &act, &old);
  if (res) return (__sanitizer_sighandler_ptr)sig_err;
  return old.handler;
}

#define TSAN_SYSCALL()             \
  ThreadState *thr = cur_thread(); \
  if (thr->ignore_interceptors)    \
    return;                        \
  ScopedSyscall scoped_syscall(thr)

struct ScopedSyscall {
  ThreadState *thr;

  explicit ScopedSyscall(ThreadState *thr) : thr(thr) { LazyInitialize(thr); }

  ~ScopedSyscall() {
    ProcessPendingSignals(thr);
  }
};

#if !SANITIZER_FREEBSD && !SANITIZER_APPLE
static void syscall_access_range(uptr pc, uptr p, uptr s, bool write) {
  TSAN_SYSCALL();
  MemoryAccessRange(thr, pc, p, s, write);
}

static USED void syscall_acquire(uptr pc, uptr addr) {
  TSAN_SYSCALL();
  Acquire(thr, pc, addr);
  DPrintf("syscall_acquire(0x%zx))\n", addr);
}

static USED void syscall_release(uptr pc, uptr addr) {
  TSAN_SYSCALL();
  DPrintf("syscall_release(0x%zx)\n", addr);
  Release(thr, pc, addr);
}

static void syscall_fd_close(uptr pc, int fd) {
  auto *thr = cur_thread();
  FdClose(thr, pc, fd);
}

static USED void syscall_fd_acquire(uptr pc, int fd) {
  TSAN_SYSCALL();
  FdAcquire(thr, pc, fd);
  DPrintf("syscall_fd_acquire(%d)\n", fd);
}

static USED void syscall_fd_release(uptr pc, int fd) {
  TSAN_SYSCALL();
  DPrintf("syscall_fd_release(%d)\n", fd);
  FdRelease(thr, pc, fd);
}

static USED void sycall_blocking_start() {
  DPrintf("sycall_blocking_start()\n");
  ThreadState *thr = cur_thread();
  EnterBlockingFunc(thr);
  // When we are in a "blocking call", we process signals asynchronously
  // (right when they arrive). In this context we do not expect to be
  // executing any user/runtime code. The known interceptor sequence when
  // this is not true is: pthread_join -> munmap(stack). It's fine
  // to ignore munmap in this case -- we handle stack shadow separately.
  thr->ignore_interceptors++;
}

static USED void sycall_blocking_end() {
  DPrintf("sycall_blocking_end()\n");
  ThreadState *thr = cur_thread();
  thr->ignore_interceptors--;
  atomic_store(&thr->in_blocking_func, 0, memory_order_relaxed);
}

static void syscall_pre_fork(uptr pc) { ForkBefore(cur_thread(), pc); }

static void syscall_post_fork(uptr pc, int pid) {
  ThreadState *thr = cur_thread();
  if (pid == 0) {
    // child
    ForkChildAfter(thr, pc, true);
    FdOnFork(thr, pc);
  } else if (pid > 0) {
    // parent
    ForkParentAfter(thr, pc);
  } else {
    // error
    ForkParentAfter(thr, pc);
  }
}
#endif

#define COMMON_SYSCALL_PRE_READ_RANGE(p, s) \
  syscall_access_range(GET_CALLER_PC(), (uptr)(p), (uptr)(s), false)

#define COMMON_SYSCALL_PRE_WRITE_RANGE(p, s) \
  syscall_access_range(GET_CALLER_PC(), (uptr)(p), (uptr)(s), true)

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

#define COMMON_SYSCALL_ACQUIRE(addr) \
    syscall_acquire(GET_CALLER_PC(), (uptr)(addr))

#define COMMON_SYSCALL_RELEASE(addr) \
    syscall_release(GET_CALLER_PC(), (uptr)(addr))

#define COMMON_SYSCALL_FD_CLOSE(fd) syscall_fd_close(GET_CALLER_PC(), fd)

#define COMMON_SYSCALL_FD_ACQUIRE(fd) syscall_fd_acquire(GET_CALLER_PC(), fd)

#define COMMON_SYSCALL_FD_RELEASE(fd) syscall_fd_release(GET_CALLER_PC(), fd)

#define COMMON_SYSCALL_PRE_FORK() \
  syscall_pre_fork(GET_CALLER_PC())

#define COMMON_SYSCALL_POST_FORK(res) \
  syscall_post_fork(GET_CALLER_PC(), res)

#define COMMON_SYSCALL_BLOCKING_START() sycall_blocking_start()
#define COMMON_SYSCALL_BLOCKING_END() sycall_blocking_end()

#include "sanitizer_common/sanitizer_common_syscalls.inc"
#include "sanitizer_common/sanitizer_syscalls_netbsd.inc"

#ifdef NEED_TLS_GET_ADDR

static void handle_tls_addr(void *arg, void *res) {
  ThreadState *thr = cur_thread();
  if (!thr)
    return;
  DTLS::DTV *dtv = DTLS_on_tls_get_addr(arg, res, thr->tls_addr,
                                        thr->tls_addr + thr->tls_size);
  if (!dtv)
    return;
  // New DTLS block has been allocated.
  MemoryResetRange(thr, 0, dtv->beg, dtv->size);
}

#if !SANITIZER_S390
// Define own interceptor instead of sanitizer_common's for three reasons:
// 1. It must not process pending signals.
//    Signal handlers may contain MOVDQA instruction (see below).
// 2. It must be as simple as possible to not contain MOVDQA.
// 3. Sanitizer_common version uses COMMON_INTERCEPTOR_INITIALIZE_RANGE which
//    is empty for tsan (meant only for msan).
// Note: __tls_get_addr can be called with mis-aligned stack due to:
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=58066
// So the interceptor must work with mis-aligned stack, in particular, does not
// execute MOVDQA with stack addresses.
TSAN_INTERCEPTOR(void *, __tls_get_addr, void *arg) {
  void *res = REAL(__tls_get_addr)(arg);
  handle_tls_addr(arg, res);
  return res;
}
#else // SANITIZER_S390
TSAN_INTERCEPTOR(uptr, __tls_get_addr_internal, void *arg) {
  uptr res = __tls_get_offset_wrapper(arg, REAL(__tls_get_offset));
  char *tp = static_cast<char *>(__builtin_thread_pointer());
  handle_tls_addr(arg, res + tp);
  return res;
}
#endif
#endif

#if SANITIZER_NETBSD
TSAN_INTERCEPTOR(void, _lwp_exit) {
  SCOPED_TSAN_INTERCEPTOR(_lwp_exit);
  DestroyThreadState();
  REAL(_lwp_exit)();
}
#define TSAN_MAYBE_INTERCEPT__LWP_EXIT TSAN_INTERCEPT(_lwp_exit)
#else
#define TSAN_MAYBE_INTERCEPT__LWP_EXIT
#endif

#if SANITIZER_FREEBSD
TSAN_INTERCEPTOR(void, thr_exit, tid_t *state) {
  SCOPED_TSAN_INTERCEPTOR(thr_exit, state);
  DestroyThreadState();
  REAL(thr_exit(state));
}
#define TSAN_MAYBE_INTERCEPT_THR_EXIT TSAN_INTERCEPT(thr_exit)
#else
#define TSAN_MAYBE_INTERCEPT_THR_EXIT
#endif

TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, cond_init, void *c, void *a)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, cond_destroy, void *c)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, cond_signal, void *c)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, cond_broadcast, void *c)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, cond_wait, void *c, void *m)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, mutex_init, void *m, void *a)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, mutex_destroy, void *m)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, mutex_lock, void *m)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, mutex_trylock, void *m)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, mutex_unlock, void *m)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, rwlock_init, void *l, void *a)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, rwlock_destroy, void *l)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, rwlock_rdlock, void *l)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, rwlock_tryrdlock, void *l)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, rwlock_wrlock, void *l)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, rwlock_trywrlock, void *l)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, rwlock_unlock, void *l)
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, once, void *o, void (*i)())
TSAN_INTERCEPTOR_FREEBSD_ALIAS(int, sigmask, int f, void *n, void *o)

TSAN_INTERCEPTOR_NETBSD_ALIAS(int, cond_init, void *c, void *a)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, cond_signal, void *c)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, cond_broadcast, void *c)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, cond_wait, void *c, void *m)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, cond_destroy, void *c)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, mutex_init, void *m, void *a)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, mutex_destroy, void *m)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, mutex_lock, void *m)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, mutex_trylock, void *m)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, mutex_unlock, void *m)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, rwlock_init, void *m, void *a)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, rwlock_destroy, void *m)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, rwlock_rdlock, void *m)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, rwlock_tryrdlock, void *m)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, rwlock_wrlock, void *m)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, rwlock_trywrlock, void *m)
TSAN_INTERCEPTOR_NETBSD_ALIAS(int, rwlock_unlock, void *m)
TSAN_INTERCEPTOR_NETBSD_ALIAS_THR(int, once, void *o, void (*f)())
TSAN_INTERCEPTOR_NETBSD_ALIAS_THR2(int, sigsetmask, sigmask, int a, void *b,
  void *c)

namespace __tsan {

static void finalize(void *arg) {
  ThreadState *thr = cur_thread();
  int status = Finalize(thr);
  // Make sure the output is not lost.
  FlushStreams();
  if (status)
    Die();
}

#if !SANITIZER_APPLE && !SANITIZER_ANDROID
static void unreachable() {
  Report("FATAL: ThreadSanitizer: unreachable called\n");
  Die();
}
#endif

// Define default implementation since interception of libdispatch  is optional.
SANITIZER_WEAK_ATTRIBUTE void InitializeLibdispatchInterceptors() {}

void InitializeInterceptors() {
#if !SANITIZER_APPLE
  // We need to setup it early, because functions like dlsym() can call it.
  REAL(memset) = internal_memset;
  REAL(memcpy) = internal_memcpy;
#endif

  __interception::DoesNotSupportStaticLinking();

  new(interceptor_ctx()) InterceptorContext();

  // Interpose __tls_get_addr before the common interposers. This is needed
  // because dlsym() may call malloc on failure which could result in other
  // interposed functions being called that could eventually make use of TLS.
#ifdef NEED_TLS_GET_ADDR
#  if !SANITIZER_S390
  TSAN_INTERCEPT(__tls_get_addr);
#  else
  TSAN_INTERCEPT(__tls_get_addr_internal);
  TSAN_INTERCEPT(__tls_get_offset);
#  endif
#endif
  InitializeCommonInterceptors();
  InitializeSignalInterceptors();
  InitializeLibdispatchInterceptors();

#if !SANITIZER_APPLE
  InitializeSetjmpInterceptors();
#endif

  TSAN_INTERCEPT(longjmp_symname);
  TSAN_INTERCEPT(siglongjmp_symname);
#if SANITIZER_NETBSD
  TSAN_INTERCEPT(_longjmp);
#endif

  TSAN_INTERCEPT(malloc);
  TSAN_INTERCEPT(__libc_memalign);
  TSAN_INTERCEPT(calloc);
  TSAN_INTERCEPT(realloc);
  TSAN_INTERCEPT(reallocarray);
  TSAN_INTERCEPT(free);
  TSAN_INTERCEPT(cfree);
  TSAN_INTERCEPT(munmap);
  TSAN_MAYBE_INTERCEPT_MEMALIGN;
  TSAN_INTERCEPT(valloc);
  TSAN_MAYBE_INTERCEPT_PVALLOC;
  TSAN_INTERCEPT(posix_memalign);

  TSAN_INTERCEPT(strcpy);
  TSAN_INTERCEPT(strncpy);
  TSAN_INTERCEPT(strdup);

  TSAN_INTERCEPT(pthread_create);
  TSAN_INTERCEPT(pthread_join);
  TSAN_INTERCEPT(pthread_detach);
  TSAN_INTERCEPT(pthread_exit);
  #if SANITIZER_LINUX
  TSAN_INTERCEPT(pthread_tryjoin_np);
  TSAN_INTERCEPT(pthread_timedjoin_np);
  #endif

  TSAN_INTERCEPT_VER(pthread_cond_init, PTHREAD_ABI_BASE);
  TSAN_INTERCEPT_VER(pthread_cond_signal, PTHREAD_ABI_BASE);
  TSAN_INTERCEPT_VER(pthread_cond_broadcast, PTHREAD_ABI_BASE);
  TSAN_INTERCEPT_VER(pthread_cond_wait, PTHREAD_ABI_BASE);
  TSAN_INTERCEPT_VER(pthread_cond_timedwait, PTHREAD_ABI_BASE);
  TSAN_INTERCEPT_VER(pthread_cond_destroy, PTHREAD_ABI_BASE);

  TSAN_MAYBE_PTHREAD_COND_CLOCKWAIT;

  TSAN_INTERCEPT(pthread_mutex_init);
  TSAN_INTERCEPT(pthread_mutex_destroy);
  TSAN_INTERCEPT(pthread_mutex_lock);
  TSAN_INTERCEPT(pthread_mutex_trylock);
  TSAN_INTERCEPT(pthread_mutex_timedlock);
  TSAN_INTERCEPT(pthread_mutex_unlock);
#if SANITIZER_LINUX
  TSAN_INTERCEPT(pthread_mutex_clocklock);
#endif
#if SANITIZER_GLIBC
#  if !__GLIBC_PREREQ(2, 34)
  TSAN_INTERCEPT(__pthread_mutex_lock);
  TSAN_INTERCEPT(__pthread_mutex_unlock);
#  endif
#endif

  TSAN_INTERCEPT(pthread_spin_init);
  TSAN_INTERCEPT(pthread_spin_destroy);
  TSAN_INTERCEPT(pthread_spin_lock);
  TSAN_INTERCEPT(pthread_spin_trylock);
  TSAN_INTERCEPT(pthread_spin_unlock);

  TSAN_INTERCEPT(pthread_rwlock_init);
  TSAN_INTERCEPT(pthread_rwlock_destroy);
  TSAN_INTERCEPT(pthread_rwlock_rdlock);
  TSAN_INTERCEPT(pthread_rwlock_tryrdlock);
  TSAN_INTERCEPT(pthread_rwlock_timedrdlock);
  TSAN_INTERCEPT(pthread_rwlock_wrlock);
  TSAN_INTERCEPT(pthread_rwlock_trywrlock);
  TSAN_INTERCEPT(pthread_rwlock_timedwrlock);
  TSAN_INTERCEPT(pthread_rwlock_unlock);

  TSAN_INTERCEPT(pthread_barrier_init);
  TSAN_INTERCEPT(pthread_barrier_destroy);
  TSAN_INTERCEPT(pthread_barrier_wait);

  TSAN_INTERCEPT(pthread_once);

  TSAN_MAYBE_INTERCEPT___FXSTAT;
  TSAN_MAYBE_INTERCEPT_FSTAT;
  TSAN_MAYBE_INTERCEPT_FSTAT64;
  TSAN_INTERCEPT(open);
  TSAN_MAYBE_INTERCEPT_OPEN64;
  TSAN_INTERCEPT(creat);
  TSAN_MAYBE_INTERCEPT_CREAT64;
  TSAN_INTERCEPT(dup);
  TSAN_INTERCEPT(dup2);
  TSAN_INTERCEPT(dup3);
  TSAN_MAYBE_INTERCEPT_EVENTFD;
  TSAN_MAYBE_INTERCEPT_SIGNALFD;
  TSAN_MAYBE_INTERCEPT_INOTIFY_INIT;
  TSAN_MAYBE_INTERCEPT_INOTIFY_INIT1;
  TSAN_INTERCEPT(socket);
  TSAN_INTERCEPT(socketpair);
  TSAN_INTERCEPT(connect);
  TSAN_INTERCEPT(bind);
  TSAN_INTERCEPT(listen);
  TSAN_MAYBE_INTERCEPT_EPOLL;
  TSAN_INTERCEPT(close);
  TSAN_MAYBE_INTERCEPT___CLOSE;
  TSAN_MAYBE_INTERCEPT___RES_ICLOSE;
  TSAN_INTERCEPT(pipe);
  TSAN_INTERCEPT(pipe2);

  TSAN_INTERCEPT(unlink);
  TSAN_INTERCEPT(tmpfile);
  TSAN_MAYBE_INTERCEPT_TMPFILE64;
  TSAN_INTERCEPT(abort);
  TSAN_INTERCEPT(rmdir);
  TSAN_INTERCEPT(closedir);

  TSAN_INTERCEPT(sigsuspend);
  TSAN_INTERCEPT(sigblock);
  TSAN_INTERCEPT(sigsetmask);
  TSAN_INTERCEPT(pthread_sigmask);
  TSAN_INTERCEPT(raise);
  TSAN_INTERCEPT(kill);
  TSAN_INTERCEPT(pthread_kill);
  TSAN_INTERCEPT(sleep);
  TSAN_INTERCEPT(usleep);
  TSAN_INTERCEPT(nanosleep);
  TSAN_INTERCEPT(pause);
  TSAN_INTERCEPT(gettimeofday);
  TSAN_INTERCEPT(getaddrinfo);

  TSAN_INTERCEPT(fork);
  TSAN_INTERCEPT(vfork);
#if SANITIZER_LINUX
  TSAN_INTERCEPT(clone);
#endif
#if !SANITIZER_ANDROID
  TSAN_INTERCEPT(dl_iterate_phdr);
#endif
  TSAN_MAYBE_INTERCEPT_ON_EXIT;
  TSAN_INTERCEPT(__cxa_atexit);
  TSAN_INTERCEPT(_exit);

  TSAN_MAYBE_INTERCEPT__LWP_EXIT;
  TSAN_MAYBE_INTERCEPT_THR_EXIT;

#if !SANITIZER_APPLE && !SANITIZER_ANDROID
  // Need to setup it, because interceptors check that the function is resolved.
  // But atexit is emitted directly into the module, so can't be resolved.
  REAL(atexit) = (int(*)(void(*)()))unreachable;
#endif

  if (REAL(__cxa_atexit)(&finalize, 0, 0)) {
    Printf("ThreadSanitizer: failed to setup atexit callback\n");
    Die();
  }
  if (pthread_atfork(atfork_prepare, atfork_parent, atfork_child)) {
    Printf("ThreadSanitizer: failed to setup atfork callbacks\n");
    Die();
  }

#if !SANITIZER_APPLE && !SANITIZER_NETBSD && !SANITIZER_FREEBSD
  if (pthread_key_create(&interceptor_ctx()->finalize_key, &thread_finalize)) {
    Printf("ThreadSanitizer: failed to create thread key\n");
    Die();
  }
#endif

  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(cond_init);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(cond_destroy);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(cond_signal);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(cond_broadcast);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(cond_wait);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(mutex_init);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(mutex_destroy);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(mutex_lock);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(mutex_trylock);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(mutex_unlock);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(rwlock_init);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(rwlock_destroy);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(rwlock_rdlock);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(rwlock_tryrdlock);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(rwlock_wrlock);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(rwlock_trywrlock);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(rwlock_unlock);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(once);
  TSAN_MAYBE_INTERCEPT_FREEBSD_ALIAS(sigmask);

  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(cond_init);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(cond_signal);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(cond_broadcast);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(cond_wait);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(cond_destroy);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(mutex_init);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(mutex_destroy);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(mutex_lock);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(mutex_trylock);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(mutex_unlock);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(rwlock_init);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(rwlock_destroy);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(rwlock_rdlock);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(rwlock_tryrdlock);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(rwlock_wrlock);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(rwlock_trywrlock);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS(rwlock_unlock);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS_THR(once);
  TSAN_MAYBE_INTERCEPT_NETBSD_ALIAS_THR(sigsetmask);

  FdInit();
}

}  // namespace __tsan

// Invisible barrier for tests.
// There were several unsuccessful iterations for this functionality:
// 1. Initially it was implemented in user code using
//    REAL(pthread_barrier_wait). But pthread_barrier_wait is not supported on
//    MacOS. Futexes are linux-specific for this matter.
// 2. Then we switched to atomics+usleep(10). But usleep produced parasitic
//    "as-if synchronized via sleep" messages in reports which failed some
//    output tests.
// 3. Then we switched to atomics+sched_yield. But this produced tons of tsan-
//    visible events, which lead to "failed to restore stack trace" failures.
// Note that no_sanitize_thread attribute does not turn off atomic interception
// so attaching it to the function defined in user code does not help.
// That's why we now have what we have.
constexpr u32 kBarrierThreadBits = 10;
constexpr u32 kBarrierThreads = 1 << kBarrierThreadBits;

extern "C" {

SANITIZER_INTERFACE_ATTRIBUTE void __tsan_testonly_barrier_init(
    atomic_uint32_t *barrier, u32 num_threads) {
  if (num_threads >= kBarrierThreads) {
    Printf("barrier_init: count is too large (%d)\n", num_threads);
    Die();
  }
  // kBarrierThreadBits lsb is thread count,
  // the remaining are count of entered threads.
  atomic_store(barrier, num_threads, memory_order_relaxed);
}

static u32 barrier_epoch(u32 value) {
  return (value >> kBarrierThreadBits) / (value & (kBarrierThreads - 1));
}

SANITIZER_INTERFACE_ATTRIBUTE void __tsan_testonly_barrier_wait(
    atomic_uint32_t *barrier) {
  u32 old = atomic_fetch_add(barrier, kBarrierThreads, memory_order_relaxed);
  u32 old_epoch = barrier_epoch(old);
  if (barrier_epoch(old + kBarrierThreads) != old_epoch) {
    FutexWake(barrier, (1 << 30));
    return;
  }
  for (;;) {
    u32 cur = atomic_load(barrier, memory_order_relaxed);
    if (barrier_epoch(cur) != old_epoch)
      return;
    FutexWait(barrier, cur);
  }
}

}  // extern "C"
