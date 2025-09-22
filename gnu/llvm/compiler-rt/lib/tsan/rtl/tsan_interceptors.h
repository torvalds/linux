#ifndef TSAN_INTERCEPTORS_H
#define TSAN_INTERCEPTORS_H

#include "sanitizer_common/sanitizer_stacktrace.h"
#include "tsan_rtl.h"

namespace __tsan {

class ScopedInterceptor {
 public:
  ScopedInterceptor(ThreadState *thr, const char *fname, uptr pc);
  ~ScopedInterceptor();
  void DisableIgnores() {
    if (UNLIKELY(ignoring_))
      DisableIgnoresImpl();
  }
  void EnableIgnores() {
    if (UNLIKELY(ignoring_))
      EnableIgnoresImpl();
  }

 private:
  ThreadState *const thr_;
  bool in_ignored_lib_ = false;
  bool in_blocking_func_ = false;
  bool ignoring_ = false;

  void DisableIgnoresImpl();
  void EnableIgnoresImpl();
};

struct TsanInterceptorContext {
  ThreadState *thr;
  const uptr pc;
};

LibIgnore *libignore();

#if !SANITIZER_GO
inline bool in_symbolizer() {
  return UNLIKELY(cur_thread_init()->in_symbolizer);
}
#endif

inline bool MustIgnoreInterceptor(ThreadState *thr) {
  return !thr->is_inited || thr->ignore_interceptors || thr->in_ignored_lib;
}

}  // namespace __tsan

#define SCOPED_INTERCEPTOR_RAW(func, ...)            \
  ThreadState *thr = cur_thread_init();              \
  ScopedInterceptor si(thr, #func, GET_CALLER_PC()); \
  UNUSED const uptr pc = GET_CURRENT_PC();

#ifdef __powerpc64__
// Debugging of crashes on powerpc after commit:
// c80604f7a3 ("tsan: remove real func check from interceptors")
// Somehow replacing if with DCHECK leads to strange failures in:
// SanitizerCommon-tsan-powerpc64le-Linux :: Linux/ptrace.cpp
// https://lab.llvm.org/buildbot/#/builders/105
// https://lab.llvm.org/buildbot/#/builders/121
// https://lab.llvm.org/buildbot/#/builders/57
#  define CHECK_REAL_FUNC(func)                                          \
    if (REAL(func) == 0) {                                               \
      Report("FATAL: ThreadSanitizer: failed to intercept %s\n", #func); \
      Die();                                                             \
    }
#else
#  define CHECK_REAL_FUNC(func) DCHECK(REAL(func))
#endif

#define SCOPED_TSAN_INTERCEPTOR(func, ...)   \
  SCOPED_INTERCEPTOR_RAW(func, __VA_ARGS__); \
  CHECK_REAL_FUNC(func);                     \
  if (MustIgnoreInterceptor(thr))            \
    return REAL(func)(__VA_ARGS__);

#define SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_START() \
    si.DisableIgnores();

#define SCOPED_TSAN_INTERCEPTOR_USER_CALLBACK_END() \
    si.EnableIgnores();

#define TSAN_INTERCEPTOR(ret, func, ...) INTERCEPTOR(ret, func, __VA_ARGS__)

#if SANITIZER_FREEBSD
#  define TSAN_INTERCEPTOR_FREEBSD_ALIAS(ret, func, ...) \
    TSAN_INTERCEPTOR(ret, _pthread_##func, __VA_ARGS__)  \
    ALIAS(WRAP(pthread_##func));
#else
#  define TSAN_INTERCEPTOR_FREEBSD_ALIAS(ret, func, ...)
#endif

#if SANITIZER_NETBSD
# define TSAN_INTERCEPTOR_NETBSD_ALIAS(ret, func, ...) \
  TSAN_INTERCEPTOR(ret, __libc_##func, __VA_ARGS__) \
  ALIAS(WRAP(pthread_##func));
# define TSAN_INTERCEPTOR_NETBSD_ALIAS_THR(ret, func, ...) \
  TSAN_INTERCEPTOR(ret, __libc_thr_##func, __VA_ARGS__) \
  ALIAS(WRAP(pthread_##func));
# define TSAN_INTERCEPTOR_NETBSD_ALIAS_THR2(ret, func, func2, ...) \
  TSAN_INTERCEPTOR(ret, __libc_thr_##func, __VA_ARGS__) \
  ALIAS(WRAP(pthread_##func2));
#else
# define TSAN_INTERCEPTOR_NETBSD_ALIAS(ret, func, ...)
# define TSAN_INTERCEPTOR_NETBSD_ALIAS_THR(ret, func, ...)
# define TSAN_INTERCEPTOR_NETBSD_ALIAS_THR2(ret, func, func2, ...)
#endif

#define COMMON_INTERCEPT_FUNCTION(name) INTERCEPT_FUNCTION(name)

#define COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED \
  (!cur_thread_init()->is_inited)

#define COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, size)                    \
  MemoryAccessRange(((TsanInterceptorContext *)ctx)->thr,                 \
                    ((TsanInterceptorContext *)ctx)->pc, (uptr)ptr, size, \
                    true)

#define COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, size)                       \
  MemoryAccessRange(((TsanInterceptorContext *) ctx)->thr,                  \
                    ((TsanInterceptorContext *) ctx)->pc, (uptr) ptr, size, \
                    false)

#define COMMON_INTERCEPTOR_ENTER(ctx, func, ...) \
  SCOPED_TSAN_INTERCEPTOR(func, __VA_ARGS__);    \
  TsanInterceptorContext _ctx = {thr, pc};       \
  ctx = (void *)&_ctx;                           \
  (void)ctx;

#endif  // TSAN_INTERCEPTORS_H
