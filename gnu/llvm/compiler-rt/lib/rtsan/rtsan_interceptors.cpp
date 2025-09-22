//===--- rtsan_interceptors.cpp - Realtime Sanitizer ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "rtsan/rtsan_interceptors.h"

#include "interception/interception.h"
#include "sanitizer_common/sanitizer_allocator_dlsym.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_platform.h"
#include "sanitizer_common/sanitizer_platform_interceptors.h"

#include "interception/interception.h"
#include "rtsan/rtsan.h"
#include "rtsan/rtsan_context.h"

#if SANITIZER_APPLE

#if TARGET_OS_MAC
// On MacOS OSSpinLockLock is deprecated and no longer present in the headers,
// but the symbol still exists on the system. Forward declare here so we
// don't get compilation errors.
#include <stdint.h>
extern "C" {
typedef int32_t OSSpinLock;
void OSSpinLockLock(volatile OSSpinLock *__lock);
}
#endif

#include <libkern/OSAtomic.h>
#include <os/lock.h>
#endif

#if SANITIZER_INTERCEPT_MEMALIGN || SANITIZER_INTERCEPT_PVALLOC
#include <malloc.h>
#endif

#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

using namespace __sanitizer;

using __rtsan::rtsan_init_is_running;
using __rtsan::rtsan_initialized;

namespace {
struct DlsymAlloc : public DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return !rtsan_initialized; }
};
} // namespace

void ExpectNotRealtime(const char *intercepted_function_name) {
  __rtsan::GetContextForThisThread().ExpectNotRealtime(
      intercepted_function_name);
}

// Filesystem

INTERCEPTOR(int, open, const char *path, int oflag, ...) {
  // TODO Establish whether we should intercept here if the flag contains
  // O_NONBLOCK
  ExpectNotRealtime("open");

  va_list args;
  va_start(args, oflag);
  const mode_t mode = va_arg(args, int);
  va_end(args);

  const int result = REAL(open)(path, oflag, mode);
  return result;
}

INTERCEPTOR(int, openat, int fd, const char *path, int oflag, ...) {
  // TODO Establish whether we should intercept here if the flag contains
  // O_NONBLOCK
  ExpectNotRealtime("openat");

  va_list args;
  va_start(args, oflag);
  mode_t mode = va_arg(args, int);
  va_end(args);

  const int result = REAL(openat)(fd, path, oflag, mode);
  return result;
}

INTERCEPTOR(int, creat, const char *path, mode_t mode) {
  // TODO Establish whether we should intercept here if the flag contains
  // O_NONBLOCK
  ExpectNotRealtime("creat");
  const int result = REAL(creat)(path, mode);
  return result;
}

INTERCEPTOR(int, fcntl, int filedes, int cmd, ...) {
  ExpectNotRealtime("fcntl");

  va_list args;
  va_start(args, cmd);

  // Following precedent here. The linux source (fcntl.c, do_fcntl) accepts the
  // final argument in a variable that will hold the largest of the possible
  // argument types (pointers and ints are typical in fcntl) It is then assumed
  // that the implementation of fcntl will cast it properly depending on cmd.
  //
  // This is also similar to what is done in
  // sanitizer_common/sanitizer_common_syscalls.inc
  const unsigned long arg = va_arg(args, unsigned long);
  int result = REAL(fcntl)(filedes, cmd, arg);

  va_end(args);

  return result;
}

INTERCEPTOR(int, close, int filedes) {
  ExpectNotRealtime("close");
  return REAL(close)(filedes);
}

INTERCEPTOR(FILE *, fopen, const char *path, const char *mode) {
  ExpectNotRealtime("fopen");
  return REAL(fopen)(path, mode);
}

INTERCEPTOR(size_t, fread, void *ptr, size_t size, size_t nitems,
            FILE *stream) {
  ExpectNotRealtime("fread");
  return REAL(fread)(ptr, size, nitems, stream);
}

INTERCEPTOR(size_t, fwrite, const void *ptr, size_t size, size_t nitems,
            FILE *stream) {
  ExpectNotRealtime("fwrite");
  return REAL(fwrite)(ptr, size, nitems, stream);
}

INTERCEPTOR(int, fclose, FILE *stream) {
  ExpectNotRealtime("fclose");
  return REAL(fclose)(stream);
}

INTERCEPTOR(int, fputs, const char *s, FILE *stream) {
  ExpectNotRealtime("fputs");
  return REAL(fputs)(s, stream);
}

// Streams
INTERCEPTOR(int, puts, const char *s) {
  ExpectNotRealtime("puts");
  return REAL(puts)(s);
}

// Concurrency
#if SANITIZER_APPLE
#pragma clang diagnostic push
// OSSpinLockLock is deprecated, but still in use in libc++
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
INTERCEPTOR(void, OSSpinLockLock, volatile OSSpinLock *lock) {
  ExpectNotRealtime("OSSpinLockLock");
  return REAL(OSSpinLockLock)(lock);
}
#pragma clang diagnostic pop

INTERCEPTOR(void, os_unfair_lock_lock, os_unfair_lock_t lock) {
  ExpectNotRealtime("os_unfair_lock_lock");
  return REAL(os_unfair_lock_lock)(lock);
}
#elif SANITIZER_LINUX
INTERCEPTOR(int, pthread_spin_lock, pthread_spinlock_t *spinlock) {
  ExpectNotRealtime("pthread_spin_lock");
  return REAL(pthread_spin_lock)(spinlock);
}
#endif

INTERCEPTOR(int, pthread_create, pthread_t *thread, const pthread_attr_t *attr,
            void *(*start_routine)(void *), void *arg) {
  ExpectNotRealtime("pthread_create");
  return REAL(pthread_create)(thread, attr, start_routine, arg);
}

INTERCEPTOR(int, pthread_mutex_lock, pthread_mutex_t *mutex) {
  ExpectNotRealtime("pthread_mutex_lock");
  return REAL(pthread_mutex_lock)(mutex);
}

INTERCEPTOR(int, pthread_mutex_unlock, pthread_mutex_t *mutex) {
  ExpectNotRealtime("pthread_mutex_unlock");
  return REAL(pthread_mutex_unlock)(mutex);
}

INTERCEPTOR(int, pthread_join, pthread_t thread, void **value_ptr) {
  ExpectNotRealtime("pthread_join");
  return REAL(pthread_join)(thread, value_ptr);
}

INTERCEPTOR(int, pthread_cond_signal, pthread_cond_t *cond) {
  ExpectNotRealtime("pthread_cond_signal");
  return REAL(pthread_cond_signal)(cond);
}

INTERCEPTOR(int, pthread_cond_broadcast, pthread_cond_t *cond) {
  ExpectNotRealtime("pthread_cond_broadcast");
  return REAL(pthread_cond_broadcast)(cond);
}

INTERCEPTOR(int, pthread_cond_wait, pthread_cond_t *cond,
            pthread_mutex_t *mutex) {
  ExpectNotRealtime("pthread_cond_wait");
  return REAL(pthread_cond_wait)(cond, mutex);
}

INTERCEPTOR(int, pthread_cond_timedwait, pthread_cond_t *cond,
            pthread_mutex_t *mutex, const timespec *ts) {
  ExpectNotRealtime("pthread_cond_timedwait");
  return REAL(pthread_cond_timedwait)(cond, mutex, ts);
}

INTERCEPTOR(int, pthread_rwlock_rdlock, pthread_rwlock_t *lock) {
  ExpectNotRealtime("pthread_rwlock_rdlock");
  return REAL(pthread_rwlock_rdlock)(lock);
}

INTERCEPTOR(int, pthread_rwlock_unlock, pthread_rwlock_t *lock) {
  ExpectNotRealtime("pthread_rwlock_unlock");
  return REAL(pthread_rwlock_unlock)(lock);
}

INTERCEPTOR(int, pthread_rwlock_wrlock, pthread_rwlock_t *lock) {
  ExpectNotRealtime("pthread_rwlock_wrlock");
  return REAL(pthread_rwlock_wrlock)(lock);
}

// Sleeping

INTERCEPTOR(unsigned int, sleep, unsigned int s) {
  ExpectNotRealtime("sleep");
  return REAL(sleep)(s);
}

INTERCEPTOR(int, usleep, useconds_t u) {
  ExpectNotRealtime("usleep");
  return REAL(usleep)(u);
}

INTERCEPTOR(int, nanosleep, const struct timespec *rqtp,
            struct timespec *rmtp) {
  ExpectNotRealtime("nanosleep");
  return REAL(nanosleep)(rqtp, rmtp);
}

// Memory

INTERCEPTOR(void *, calloc, SIZE_T num, SIZE_T size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Callocate(num, size);

  ExpectNotRealtime("calloc");
  return REAL(calloc)(num, size);
}

INTERCEPTOR(void, free, void *ptr) {
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);

  if (ptr != NULL) {
    ExpectNotRealtime("free");
  }
  return REAL(free)(ptr);
}

INTERCEPTOR(void *, malloc, SIZE_T size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);

  ExpectNotRealtime("malloc");
  return REAL(malloc)(size);
}

INTERCEPTOR(void *, realloc, void *ptr, SIZE_T size) {
  if (DlsymAlloc::Use() || DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Realloc(ptr, size);

  ExpectNotRealtime("realloc");
  return REAL(realloc)(ptr, size);
}

INTERCEPTOR(void *, reallocf, void *ptr, SIZE_T size) {
  ExpectNotRealtime("reallocf");
  return REAL(reallocf)(ptr, size);
}

INTERCEPTOR(void *, valloc, SIZE_T size) {
  ExpectNotRealtime("valloc");
  return REAL(valloc)(size);
}

#if SANITIZER_INTERCEPT_ALIGNED_ALLOC
INTERCEPTOR(void *, aligned_alloc, SIZE_T alignment, SIZE_T size) {
  ExpectNotRealtime("aligned_alloc");
  return REAL(aligned_alloc)(alignment, size);
}
#define RTSAN_MAYBE_INTERCEPT_ALIGNED_ALLOC INTERCEPT_FUNCTION(aligned_alloc)
#else
#define RTSAN_MAYBE_INTERCEPT_ALIGNED_ALLOC
#endif

INTERCEPTOR(int, posix_memalign, void **memptr, size_t alignment, size_t size) {
  ExpectNotRealtime("posix_memalign");
  return REAL(posix_memalign)(memptr, alignment, size);
}

#if SANITIZER_INTERCEPT_MEMALIGN
INTERCEPTOR(void *, memalign, size_t alignment, size_t size) {
  ExpectNotRealtime("memalign");
  return REAL(memalign)(alignment, size);
}
#endif

#if SANITIZER_INTERCEPT_PVALLOC
INTERCEPTOR(void *, pvalloc, size_t size) {
  ExpectNotRealtime("pvalloc");
  return REAL(pvalloc)(size);
}
#endif

// Sockets
INTERCEPTOR(int, socket, int domain, int type, int protocol) {
  ExpectNotRealtime("socket");
  return REAL(socket)(domain, type, protocol);
}

INTERCEPTOR(ssize_t, send, int sockfd, const void *buf, size_t len, int flags) {
  ExpectNotRealtime("send");
  return REAL(send)(sockfd, buf, len, flags);
}

INTERCEPTOR(ssize_t, sendmsg, int socket, const struct msghdr *message,
            int flags) {
  ExpectNotRealtime("sendmsg");
  return REAL(sendmsg)(socket, message, flags);
}

INTERCEPTOR(ssize_t, sendto, int socket, const void *buffer, size_t length,
            int flags, const struct sockaddr *dest_addr, socklen_t dest_len) {
  ExpectNotRealtime("sendto");
  return REAL(sendto)(socket, buffer, length, flags, dest_addr, dest_len);
}

INTERCEPTOR(ssize_t, recv, int socket, void *buffer, size_t length, int flags) {
  ExpectNotRealtime("recv");
  return REAL(recv)(socket, buffer, length, flags);
}

INTERCEPTOR(ssize_t, recvfrom, int socket, void *buffer, size_t length,
            int flags, struct sockaddr *address, socklen_t *address_len) {
  ExpectNotRealtime("recvfrom");
  return REAL(recvfrom)(socket, buffer, length, flags, address, address_len);
}

INTERCEPTOR(ssize_t, recvmsg, int socket, struct msghdr *message, int flags) {
  ExpectNotRealtime("recvmsg");
  return REAL(recvmsg)(socket, message, flags);
}

INTERCEPTOR(int, shutdown, int socket, int how) {
  ExpectNotRealtime("shutdown");
  return REAL(shutdown)(socket, how);
}

// Preinit
void __rtsan::InitializeInterceptors() {
  INTERCEPT_FUNCTION(calloc);
  INTERCEPT_FUNCTION(free);
  INTERCEPT_FUNCTION(malloc);
  INTERCEPT_FUNCTION(realloc);
  INTERCEPT_FUNCTION(reallocf);
  INTERCEPT_FUNCTION(valloc);
  RTSAN_MAYBE_INTERCEPT_ALIGNED_ALLOC;
  INTERCEPT_FUNCTION(posix_memalign);
#if SANITIZER_INTERCEPT_MEMALIGN
  INTERCEPT_FUNCTION(memalign);
#endif
#if SANITIZER_INTERCEPT_PVALLOC
  INTERCEPT_FUNCTION(pvalloc);
#endif

  INTERCEPT_FUNCTION(open);
  INTERCEPT_FUNCTION(openat);
  INTERCEPT_FUNCTION(close);
  INTERCEPT_FUNCTION(fopen);
  INTERCEPT_FUNCTION(fread);
  INTERCEPT_FUNCTION(fwrite);
  INTERCEPT_FUNCTION(fclose);
  INTERCEPT_FUNCTION(fcntl);
  INTERCEPT_FUNCTION(creat);
  INTERCEPT_FUNCTION(puts);
  INTERCEPT_FUNCTION(fputs);

#if SANITIZER_APPLE
  INTERCEPT_FUNCTION(OSSpinLockLock);
  INTERCEPT_FUNCTION(os_unfair_lock_lock);
#elif SANITIZER_LINUX
  INTERCEPT_FUNCTION(pthread_spin_lock);
#endif

  INTERCEPT_FUNCTION(pthread_create);
  INTERCEPT_FUNCTION(pthread_mutex_lock);
  INTERCEPT_FUNCTION(pthread_mutex_unlock);
  INTERCEPT_FUNCTION(pthread_join);
  INTERCEPT_FUNCTION(pthread_cond_signal);
  INTERCEPT_FUNCTION(pthread_cond_broadcast);
  INTERCEPT_FUNCTION(pthread_cond_wait);
  INTERCEPT_FUNCTION(pthread_cond_timedwait);
  INTERCEPT_FUNCTION(pthread_rwlock_rdlock);
  INTERCEPT_FUNCTION(pthread_rwlock_unlock);
  INTERCEPT_FUNCTION(pthread_rwlock_wrlock);

  INTERCEPT_FUNCTION(sleep);
  INTERCEPT_FUNCTION(usleep);
  INTERCEPT_FUNCTION(nanosleep);

  INTERCEPT_FUNCTION(socket);
  INTERCEPT_FUNCTION(send);
  INTERCEPT_FUNCTION(sendmsg);
  INTERCEPT_FUNCTION(sendto);
  INTERCEPT_FUNCTION(recv);
  INTERCEPT_FUNCTION(recvmsg);
  INTERCEPT_FUNCTION(recvfrom);
  INTERCEPT_FUNCTION(shutdown);
}
