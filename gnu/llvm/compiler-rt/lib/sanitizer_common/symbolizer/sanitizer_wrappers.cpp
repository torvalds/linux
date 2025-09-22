//===-- sanitizer_wrappers.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Redirect some functions to sanitizer interceptors.
//
//===----------------------------------------------------------------------===//

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include <tuple>

namespace __sanitizer {
unsigned long internal_open(const char *filename, int flags);
unsigned long internal_open(const char *filename, int flags, unsigned mode);
unsigned long internal_close(int fd);
unsigned long internal_stat(const char *path, void *buf);
unsigned long internal_lstat(const char *path, void *buf);
unsigned long internal_fstat(int fd, void *buf);
size_t internal_strlen(const char *s);
unsigned long internal_mmap(void *addr, uintptr_t length, int prot, int flags,
                            int fd, unsigned long long offset);
void *internal_memcpy(void *dest, const void *src, unsigned long n);
// Used to propagate errno.
bool internal_iserror(uintptr_t retval, int *rverrno = 0);
}  // namespace __sanitizer

namespace {

template <typename T>
struct GetTypes;

template <typename R, typename... Args>
struct GetTypes<R(Args...)> {
  using Result = R;
  template <size_t i>
  struct Arg {
    using Type = typename std::tuple_element<i, std::tuple<Args...>>::type;
  };
};

#define LLVM_SYMBOLIZER_GET_FUNC(Function) \
  ((__interceptor_##Function)              \
       ? (__interceptor_##Function)        \
       : reinterpret_cast<decltype(&Function)>(dlsym(RTLD_NEXT, #Function)))

#define LLVM_SYMBOLIZER_INTERCEPTOR1(Function, ...)               \
  GetTypes<__VA_ARGS__>::Result __interceptor_##Function(         \
      GetTypes<__VA_ARGS__>::Arg<0>::Type) __attribute__((weak)); \
  GetTypes<__VA_ARGS__>::Result Function(                         \
      GetTypes<__VA_ARGS__>::Arg<0>::Type arg0) {                 \
    return LLVM_SYMBOLIZER_GET_FUNC(Function)(arg0);              \
  }

#define LLVM_SYMBOLIZER_INTERCEPTOR2(Function, ...)               \
  GetTypes<__VA_ARGS__>::Result __interceptor_##Function(         \
      GetTypes<__VA_ARGS__>::Arg<0>::Type,                        \
      GetTypes<__VA_ARGS__>::Arg<1>::Type) __attribute__((weak)); \
  GetTypes<__VA_ARGS__>::Result Function(                         \
      GetTypes<__VA_ARGS__>::Arg<0>::Type arg0,                   \
      GetTypes<__VA_ARGS__>::Arg<1>::Type arg1) {                 \
    return LLVM_SYMBOLIZER_GET_FUNC(Function)(arg0, arg1);        \
  }

#define LLVM_SYMBOLIZER_INTERCEPTOR3(Function, ...)               \
  GetTypes<__VA_ARGS__>::Result __interceptor_##Function(         \
      GetTypes<__VA_ARGS__>::Arg<0>::Type,                        \
      GetTypes<__VA_ARGS__>::Arg<1>::Type,                        \
      GetTypes<__VA_ARGS__>::Arg<2>::Type) __attribute__((weak)); \
  GetTypes<__VA_ARGS__>::Result Function(                         \
      GetTypes<__VA_ARGS__>::Arg<0>::Type arg0,                   \
      GetTypes<__VA_ARGS__>::Arg<1>::Type arg1,                   \
      GetTypes<__VA_ARGS__>::Arg<2>::Type arg2) {                 \
    return LLVM_SYMBOLIZER_GET_FUNC(Function)(arg0, arg1, arg2);  \
  }

#define LLVM_SYMBOLIZER_INTERCEPTOR4(Function, ...)                    \
  GetTypes<__VA_ARGS__>::Result __interceptor_##Function(              \
      GetTypes<__VA_ARGS__>::Arg<0>::Type,                             \
      GetTypes<__VA_ARGS__>::Arg<1>::Type,                             \
      GetTypes<__VA_ARGS__>::Arg<2>::Type,                             \
      GetTypes<__VA_ARGS__>::Arg<3>::Type) __attribute__((weak));      \
  GetTypes<__VA_ARGS__>::Result Function(                              \
      GetTypes<__VA_ARGS__>::Arg<0>::Type arg0,                        \
      GetTypes<__VA_ARGS__>::Arg<1>::Type arg1,                        \
      GetTypes<__VA_ARGS__>::Arg<2>::Type arg2,                        \
      GetTypes<__VA_ARGS__>::Arg<3>::Type arg3) {                      \
    return LLVM_SYMBOLIZER_GET_FUNC(Function)(arg0, arg1, arg2, arg3); \
  }

}  // namespace

// C-style interface around internal sanitizer libc functions.
extern "C" {

#define RETURN_OR_SET_ERRNO(T, res)                   \
  int rverrno;                                        \
  if (__sanitizer::internal_iserror(res, &rverrno)) { \
    errno = rverrno;                                  \
    return (T)-1;                                     \
  }                                                   \
  return (T)res;

int open(const char *filename, int flags, ...) {
  unsigned long res;
  if (flags | O_CREAT) {
    va_list va;
    va_start(va, flags);
    unsigned mode = va_arg(va, unsigned);
    va_end(va);
    res = __sanitizer::internal_open(filename, flags, mode);
  } else {
    res = __sanitizer::internal_open(filename, flags);
  }
  RETURN_OR_SET_ERRNO(int, res);
}

int close(int fd) {
  unsigned long res = __sanitizer::internal_close(fd);
  RETURN_OR_SET_ERRNO(int, res);
}

#define STAT(func, arg, buf)                                  \
  unsigned long res = __sanitizer::internal_##func(arg, buf); \
  RETURN_OR_SET_ERRNO(int, res);

int stat(const char *path, struct stat *buf) { STAT(stat, path, buf); }

int lstat(const char *path, struct stat *buf) { STAT(lstat, path, buf); }

int fstat(int fd, struct stat *buf) { STAT(fstat, fd, buf); }

// Redirect versioned stat functions to the __sanitizer::internal() as well.
int __xstat(int version, const char *path, struct stat *buf) {
  STAT(stat, path, buf);
}

int __lxstat(int version, const char *path, struct stat *buf) {
  STAT(lstat, path, buf);
}

int __fxstat(int version, int fd, struct stat *buf) { STAT(fstat, fd, buf); }

size_t strlen(const char *s) { return __sanitizer::internal_strlen(s); }

void *mmap(void *addr, size_t length, int prot, int flags, int fd,
           off_t offset) {
  unsigned long res =
      __sanitizer::internal_mmap(addr, length, prot, flags, fd, offset);
  RETURN_OR_SET_ERRNO(void *, res);
}

LLVM_SYMBOLIZER_INTERCEPTOR3(read, ssize_t(int, void *, size_t))
LLVM_SYMBOLIZER_INTERCEPTOR4(pread, ssize_t(int, void *, size_t, off_t))
LLVM_SYMBOLIZER_INTERCEPTOR4(pread64, ssize_t(int, void *, size_t, off64_t))
LLVM_SYMBOLIZER_INTERCEPTOR2(realpath, char *(const char *, char *))

LLVM_SYMBOLIZER_INTERCEPTOR1(pthread_cond_broadcast, int(pthread_cond_t *))
LLVM_SYMBOLIZER_INTERCEPTOR2(pthread_cond_wait,
                             int(pthread_cond_t *, pthread_mutex_t *))
LLVM_SYMBOLIZER_INTERCEPTOR1(pthread_mutex_lock, int(pthread_mutex_t *))
LLVM_SYMBOLIZER_INTERCEPTOR1(pthread_mutex_unlock, int(pthread_mutex_t *))
LLVM_SYMBOLIZER_INTERCEPTOR1(pthread_mutex_destroy, int(pthread_mutex_t *))
LLVM_SYMBOLIZER_INTERCEPTOR2(pthread_mutex_init,
                             int(pthread_mutex_t *,
                                 const pthread_mutexattr_t *))
LLVM_SYMBOLIZER_INTERCEPTOR1(pthread_mutexattr_destroy,
                             int(pthread_mutexattr_t *))
LLVM_SYMBOLIZER_INTERCEPTOR1(pthread_mutexattr_init, int(pthread_mutexattr_t *))
LLVM_SYMBOLIZER_INTERCEPTOR2(pthread_mutexattr_settype,
                             int(pthread_mutexattr_t *, int))
LLVM_SYMBOLIZER_INTERCEPTOR1(pthread_getspecific, void *(pthread_key_t))
LLVM_SYMBOLIZER_INTERCEPTOR2(pthread_key_create,
                             int(pthread_key_t *, void (*)(void *)))
LLVM_SYMBOLIZER_INTERCEPTOR2(pthread_once,
                             int(pthread_once_t *, void (*)(void)))
LLVM_SYMBOLIZER_INTERCEPTOR2(pthread_setspecific,
                             int(pthread_key_t, const void *))
LLVM_SYMBOLIZER_INTERCEPTOR3(pthread_sigmask,
                             int(int, const sigset_t *, sigset_t *))

}  // extern "C"
