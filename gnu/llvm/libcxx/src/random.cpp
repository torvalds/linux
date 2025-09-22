//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__config>

#if defined(_LIBCPP_USING_WIN32_RANDOM)
// Must be defined before including stdlib.h to enable rand_s().
#  define _CRT_RAND_S
#endif // defined(_LIBCPP_USING_WIN32_RANDOM)

#include <__system_error/system_error.h>
#include <limits>
#include <random>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_LIBCPP_USING_GETENTROPY)
#  include <sys/random.h>
#elif defined(_LIBCPP_USING_DEV_RANDOM)
#  include <fcntl.h>
#  include <unistd.h>
#  if __has_include(<sys/ioctl.h>) && __has_include(<linux/random.h>)
#    include <linux/random.h>
#    include <sys/ioctl.h>
#  endif
#elif defined(_LIBCPP_USING_NACL_RANDOM)
#  include <nacl/nacl_random.h>
#elif defined(_LIBCPP_USING_FUCHSIA_CPRNG)
#  include <zircon/syscalls.h>
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if defined(_LIBCPP_USING_GETENTROPY)

random_device::random_device(const string& __token) {
  if (__token != "/dev/urandom")
    __throw_system_error(ENOENT, ("random device not supported " + __token).c_str());
}

random_device::~random_device() {}

unsigned random_device::operator()() {
  unsigned r;
  size_t n = sizeof(r);
  int err  = getentropy(&r, n);
  if (err)
    __throw_system_error(errno, "random_device getentropy failed");
  return r;
}

#elif defined(_LIBCPP_USING_ARC4_RANDOM)

random_device::random_device(const string&) {}

random_device::~random_device() {}

unsigned random_device::operator()() { return arc4random(); }

#elif defined(_LIBCPP_USING_DEV_RANDOM)

random_device::random_device(const string& __token) : __f_(open(__token.c_str(), O_RDONLY)) {
  if (__f_ < 0)
    __throw_system_error(errno, ("random_device failed to open " + __token).c_str());
}

random_device::~random_device() { close(__f_); }

unsigned random_device::operator()() {
  unsigned r;
  size_t n = sizeof(r);
  char* p  = reinterpret_cast<char*>(&r);
  while (n > 0) {
    ssize_t s = read(__f_, p, n);
    if (s == 0)
      __throw_system_error(ENOMSG, "random_device got EOF");
    if (s == -1) {
      if (errno != EINTR)
        __throw_system_error(errno, "random_device got an unexpected error");
      continue;
    }
    n -= static_cast<size_t>(s);
    p += static_cast<size_t>(s);
  }
  return r;
}

#elif defined(_LIBCPP_USING_NACL_RANDOM)

random_device::random_device(const string& __token) {
  if (__token != "/dev/urandom")
    __throw_system_error(ENOENT, ("random device not supported " + __token).c_str());
  int error = nacl_secure_random_init();
  if (error)
    __throw_system_error(error, ("random device failed to open " + __token).c_str());
}

random_device::~random_device() {}

unsigned random_device::operator()() {
  unsigned r;
  size_t n = sizeof(r);
  size_t bytes_written;
  int error = nacl_secure_random(&r, n, &bytes_written);
  if (error != 0)
    __throw_system_error(error, "random_device failed getting bytes");
  else if (bytes_written != n)
    __throw_runtime_error("random_device failed to obtain enough bytes");
  return r;
}

#elif defined(_LIBCPP_USING_WIN32_RANDOM)

random_device::random_device(const string& __token) {
  if (__token != "/dev/urandom")
    __throw_system_error(ENOENT, ("random device not supported " + __token).c_str());
}

random_device::~random_device() {}

unsigned random_device::operator()() {
  unsigned r;
  errno_t err = rand_s(&r);
  if (err)
    __throw_system_error(err, "random_device rand_s failed.");
  return r;
}

#elif defined(_LIBCPP_USING_FUCHSIA_CPRNG)

random_device::random_device(const string& __token) {
  if (__token != "/dev/urandom")
    __throw_system_error(ENOENT, ("random device not supported " + __token).c_str());
}

random_device::~random_device() {}

unsigned random_device::operator()() {
  // Implicitly link against the vDSO system call ABI without
  // requiring the final link to specify -lzircon explicitly when
  // statically linking libc++.
#  pragma comment(lib, "zircon")

  // The system call cannot fail.  It returns only when the bits are ready.
  unsigned r;
  _zx_cprng_draw(&r, sizeof(r));
  return r;
}

#else
#  error "Random device not implemented for this architecture"
#endif

double random_device::entropy() const noexcept {
#if defined(_LIBCPP_USING_DEV_RANDOM) && defined(RNDGETENTCNT)
  int ent;
  if (::ioctl(__f_, RNDGETENTCNT, &ent) < 0)
    return 0;

  if (ent < 0)
    return 0;

  if (ent > std::numeric_limits<result_type>::digits)
    return std::numeric_limits<result_type>::digits;

  return ent;
#elif defined(_LIBCPP_USING_ARC4_RANDOM) || defined(_LIBCPP_USING_FUCHSIA_CPRNG)
  return std::numeric_limits<result_type>::digits;
#else
  return 0;
#endif
}

_LIBCPP_END_NAMESPACE_STD
