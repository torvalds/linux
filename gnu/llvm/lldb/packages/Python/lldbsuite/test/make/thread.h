#ifndef LLDB_THREAD_H
#define LLDB_THREAD_H

#include <stdint.h>

#if defined(__APPLE__)
__OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_2)
int pthread_threadid_np(pthread_t, __uint64_t *);
#elif defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__FreeBSD__)
#include <pthread_np.h>
#elif defined(__NetBSD__)
#include <lwp.h>
#elif defined(__OpenBSD__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

inline uint64_t get_thread_id() {
#if defined(__APPLE__)
  __uint64_t tid = 0;
  pthread_threadid_np(pthread_self(), &tid);
  return tid;
#elif defined(__linux__)
  return syscall(__NR_gettid);
#elif defined(__FreeBSD__)
  return static_cast<uint64_t>(pthread_getthreadid_np());
#elif defined(__NetBSD__)
  // Technically lwpid_t is 32-bit signed integer
  return static_cast<uint64_t>(_lwp_self());
#elif defined(__OpenBSD__)
  return static_cast<uint64_t>(getthrid());
#elif defined(_WIN32)
  return static_cast<uint64_t>(::GetCurrentThreadId());
#else
  return -1;
#endif
}

#endif // LLDB_THREAD_H
