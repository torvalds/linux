//===-- FuzzerInterceptors.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Intercept certain libc functions to aid fuzzing.
// Linked only when other RTs that define their own interceptors are not linked.
//===----------------------------------------------------------------------===//

#include "FuzzerPlatform.h"

#if LIBFUZZER_LINUX

#define GET_CALLER_PC() __builtin_return_address(0)

#define PTR_TO_REAL(x) real_##x
#define REAL(x) __interception::PTR_TO_REAL(x)
#define FUNC_TYPE(x) x##_type
#define DEFINE_REAL(ret_type, func, ...)                                       \
  typedef ret_type (*FUNC_TYPE(func))(__VA_ARGS__);                            \
  namespace __interception {                                                   \
  FUNC_TYPE(func) PTR_TO_REAL(func);                                           \
  }

#include <cassert>
#include <cstddef> // for size_t
#include <cstdint>
#include <dlfcn.h> // for dlsym()

static void *getFuncAddr(const char *name, uintptr_t wrapper_addr) {
  void *addr = dlsym(RTLD_NEXT, name);
  if (!addr) {
    // If the lookup using RTLD_NEXT failed, the sanitizer runtime library is
    // later in the library search order than the DSO that we are trying to
    // intercept, which means that we cannot intercept this function. We still
    // want the address of the real definition, though, so look it up using
    // RTLD_DEFAULT.
    addr = dlsym(RTLD_DEFAULT, name);

    // In case `name' is not loaded, dlsym ends up finding the actual wrapper.
    // We don't want to intercept the wrapper and have it point to itself.
    if (reinterpret_cast<uintptr_t>(addr) == wrapper_addr)
      addr = nullptr;
  }
  return addr;
}

static int FuzzerInited = 0;
static bool FuzzerInitIsRunning;

static void fuzzerInit();

static void ensureFuzzerInited() {
  assert(!FuzzerInitIsRunning);
  if (!FuzzerInited) {
    fuzzerInit();
  }
}

static int internal_strcmp_strncmp(const char *s1, const char *s2, bool strncmp,
                                   size_t n) {
  size_t i = 0;
  while (true) {
    if (strncmp) {
      if (i == n)
        break;
      i++;
    }
    unsigned c1 = *s1;
    unsigned c2 = *s2;
    if (c1 != c2)
      return (c1 < c2) ? -1 : 1;
    if (c1 == 0)
      break;
    s1++;
    s2++;
  }
  return 0;
}

static int internal_strncmp(const char *s1, const char *s2, size_t n) {
  return internal_strcmp_strncmp(s1, s2, true, n);
}

static int internal_strcmp(const char *s1, const char *s2) {
  return internal_strcmp_strncmp(s1, s2, false, 0);
}

static int internal_memcmp(const void *s1, const void *s2, size_t n) {
  const uint8_t *t1 = static_cast<const uint8_t *>(s1);
  const uint8_t *t2 = static_cast<const uint8_t *>(s2);
  for (size_t i = 0; i < n; ++i, ++t1, ++t2)
    if (*t1 != *t2)
      return *t1 < *t2 ? -1 : 1;
  return 0;
}

static size_t internal_strlen(const char *s) {
  size_t i = 0;
  while (s[i])
    i++;
  return i;
}

static char *internal_strstr(const char *haystack, const char *needle) {
  // This is O(N^2), but we are not using it in hot places.
  size_t len1 = internal_strlen(haystack);
  size_t len2 = internal_strlen(needle);
  if (len1 < len2)
    return nullptr;
  for (size_t pos = 0; pos <= len1 - len2; pos++) {
    if (internal_memcmp(haystack + pos, needle, len2) == 0)
      return const_cast<char *>(haystack) + pos;
  }
  return nullptr;
}

extern "C" {

// Weak hooks forward-declared to avoid dependency on
// <sanitizer/common_interface_defs.h>.
void __sanitizer_weak_hook_memcmp(void *called_pc, const void *s1,
                                  const void *s2, size_t n, int result);
void __sanitizer_weak_hook_strncmp(void *called_pc, const char *s1,
                                   const char *s2, size_t n, int result);
void __sanitizer_weak_hook_strncasecmp(void *called_pc, const char *s1,
                                       const char *s2, size_t n, int result);
void __sanitizer_weak_hook_strcmp(void *called_pc, const char *s1,
                                  const char *s2, int result);
void __sanitizer_weak_hook_strcasecmp(void *called_pc, const char *s1,
                                      const char *s2, int result);
void __sanitizer_weak_hook_strstr(void *called_pc, const char *s1,
                                  const char *s2, char *result);
void __sanitizer_weak_hook_strcasestr(void *called_pc, const char *s1,
                                      const char *s2, char *result);
void __sanitizer_weak_hook_memmem(void *called_pc, const void *s1, size_t len1,
                                  const void *s2, size_t len2, void *result);

DEFINE_REAL(int, bcmp, const void *, const void *, size_t)
DEFINE_REAL(int, memcmp, const void *, const void *, size_t)
DEFINE_REAL(int, strncmp, const char *, const char *, size_t)
DEFINE_REAL(int, strcmp, const char *, const char *)
DEFINE_REAL(int, strncasecmp, const char *, const char *, size_t)
DEFINE_REAL(int, strcasecmp, const char *, const char *)
DEFINE_REAL(char *, strstr, const char *, const char *)
DEFINE_REAL(char *, strcasestr, const char *, const char *)
DEFINE_REAL(void *, memmem, const void *, size_t, const void *, size_t)

ATTRIBUTE_INTERFACE int bcmp(const char *s1, const char *s2, size_t n) {
  if (!FuzzerInited)
    return internal_memcmp(s1, s2, n);
  int result = REAL(bcmp)(s1, s2, n);
  __sanitizer_weak_hook_memcmp(GET_CALLER_PC(), s1, s2, n, result);
  return result;
}

ATTRIBUTE_INTERFACE int memcmp(const void *s1, const void *s2, size_t n) {
  if (!FuzzerInited)
    return internal_memcmp(s1, s2, n);
  int result = REAL(memcmp)(s1, s2, n);
  __sanitizer_weak_hook_memcmp(GET_CALLER_PC(), s1, s2, n, result);
  return result;
}

ATTRIBUTE_INTERFACE int strncmp(const char *s1, const char *s2, size_t n) {
  if (!FuzzerInited)
    return internal_strncmp(s1, s2, n);
  int result = REAL(strncmp)(s1, s2, n);
  __sanitizer_weak_hook_strncmp(GET_CALLER_PC(), s1, s2, n, result);
  return result;
}

ATTRIBUTE_INTERFACE int strcmp(const char *s1, const char *s2) {
  if (!FuzzerInited)
    return internal_strcmp(s1, s2);
  int result = REAL(strcmp)(s1, s2);
  __sanitizer_weak_hook_strcmp(GET_CALLER_PC(), s1, s2, result);
  return result;
}

ATTRIBUTE_INTERFACE int strncasecmp(const char *s1, const char *s2, size_t n) {
  ensureFuzzerInited();
  int result = REAL(strncasecmp)(s1, s2, n);
  __sanitizer_weak_hook_strncasecmp(GET_CALLER_PC(), s1, s2, n, result);
  return result;
}

ATTRIBUTE_INTERFACE int strcasecmp(const char *s1, const char *s2) {
  ensureFuzzerInited();
  int result = REAL(strcasecmp)(s1, s2);
  __sanitizer_weak_hook_strcasecmp(GET_CALLER_PC(), s1, s2, result);
  return result;
}

ATTRIBUTE_INTERFACE char *strstr(const char *s1, const char *s2) {
  if (!FuzzerInited)
    return internal_strstr(s1, s2);
  char *result = REAL(strstr)(s1, s2);
  __sanitizer_weak_hook_strstr(GET_CALLER_PC(), s1, s2, result);
  return result;
}

ATTRIBUTE_INTERFACE char *strcasestr(const char *s1, const char *s2) {
  ensureFuzzerInited();
  char *result = REAL(strcasestr)(s1, s2);
  __sanitizer_weak_hook_strcasestr(GET_CALLER_PC(), s1, s2, result);
  return result;
}

ATTRIBUTE_INTERFACE
void *memmem(const void *s1, size_t len1, const void *s2, size_t len2) {
  ensureFuzzerInited();
  void *result = REAL(memmem)(s1, len1, s2, len2);
  __sanitizer_weak_hook_memmem(GET_CALLER_PC(), s1, len1, s2, len2, result);
  return result;
}

__attribute__((section(".preinit_array"),
               used)) static void (*__local_fuzzer_preinit)(void) = fuzzerInit;

} // extern "C"

static void fuzzerInit() {
  assert(!FuzzerInitIsRunning);
  if (FuzzerInited)
    return;
  FuzzerInitIsRunning = true;

  REAL(bcmp) = reinterpret_cast<memcmp_type>(
      getFuncAddr("bcmp", reinterpret_cast<uintptr_t>(&bcmp)));
  REAL(memcmp) = reinterpret_cast<memcmp_type>(
      getFuncAddr("memcmp", reinterpret_cast<uintptr_t>(&memcmp)));
  REAL(strncmp) = reinterpret_cast<strncmp_type>(
      getFuncAddr("strncmp", reinterpret_cast<uintptr_t>(&strncmp)));
  REAL(strcmp) = reinterpret_cast<strcmp_type>(
      getFuncAddr("strcmp", reinterpret_cast<uintptr_t>(&strcmp)));
  REAL(strncasecmp) = reinterpret_cast<strncasecmp_type>(
      getFuncAddr("strncasecmp", reinterpret_cast<uintptr_t>(&strncasecmp)));
  REAL(strcasecmp) = reinterpret_cast<strcasecmp_type>(
      getFuncAddr("strcasecmp", reinterpret_cast<uintptr_t>(&strcasecmp)));
  REAL(strstr) = reinterpret_cast<strstr_type>(
      getFuncAddr("strstr", reinterpret_cast<uintptr_t>(&strstr)));
  REAL(strcasestr) = reinterpret_cast<strcasestr_type>(
      getFuncAddr("strcasestr", reinterpret_cast<uintptr_t>(&strcasestr)));
  REAL(memmem) = reinterpret_cast<memmem_type>(
      getFuncAddr("memmem", reinterpret_cast<uintptr_t>(&memmem)));

  FuzzerInitIsRunning = false;
  FuzzerInited = 1;
}

#endif
