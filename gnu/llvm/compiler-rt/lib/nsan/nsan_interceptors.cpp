//===- nsan_interceptors.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Interceptors for standard library functions.
//
// A note about `printf`: Make sure none of the interceptor code calls any
// part of the nsan framework that can call `printf`, since this could create
// a loop (`printf` itself uses the libc). printf-free functions are documented
// as such in nsan.h.
//
//===----------------------------------------------------------------------===//

#include "interception/interception.h"
#include "nsan/nsan.h"
#include "sanitizer_common/sanitizer_common.h"

#include <wchar.h>

using namespace __sanitizer;
using __nsan::nsan_init_is_running;
using __nsan::nsan_initialized;

template <typename T> T min(T a, T b) { return a < b ? a : b; }

INTERCEPTOR(void *, memset, void *dst, int v, uptr size) {
  // NOTE: This guard is needed because nsan's initialization code might call
  // memset.
  if (!nsan_initialized && REAL(memset) == nullptr)
    return internal_memset(dst, v, size);

  void *res = REAL(memset)(dst, v, size);
  __nsan_set_value_unknown(static_cast<u8 *>(dst), size);
  return res;
}

INTERCEPTOR(wchar_t *, wmemset, wchar_t *dst, wchar_t v, uptr size) {
  wchar_t *res = REAL(wmemset)(dst, v, size);
  __nsan_set_value_unknown((u8 *)dst, sizeof(wchar_t) * size);
  return res;
}

INTERCEPTOR(void *, memmove, void *dst, const void *src, uptr size) {
  // NOTE: This guard is needed because nsan's initialization code might call
  // memmove.
  if (!nsan_initialized && REAL(memmove) == nullptr)
    return internal_memmove(dst, src, size);

  void *res = REAL(memmove)(dst, src, size);
  __nsan_copy_values(static_cast<u8 *>(dst), static_cast<const u8 *>(src),
                     size);
  return res;
}

INTERCEPTOR(wchar_t *, wmemmove, wchar_t *dst, const wchar_t *src, uptr size) {
  wchar_t *res = REAL(wmemmove)(dst, src, size);
  __nsan_copy_values((u8 *)dst, (const u8 *)src, sizeof(wchar_t) * size);
  return res;
}

INTERCEPTOR(void *, memcpy, void *dst, const void *src, uptr size) {
  // NOTE: This guard is needed because nsan's initialization code might call
  // memcpy.
  if (!nsan_initialized && REAL(memcpy) == nullptr) {
    // memmove is used here because on some platforms this will also
    // intercept the memmove implementation.
    return internal_memmove(dst, src, size);
  }

  void *res = REAL(memcpy)(dst, src, size);
  __nsan_copy_values(static_cast<u8 *>(dst), static_cast<const u8 *>(src),
                     size);
  return res;
}

INTERCEPTOR(wchar_t *, wmemcpy, wchar_t *dst, const wchar_t *src, uptr size) {
  wchar_t *res = REAL(wmemcpy)(dst, src, size);
  __nsan_copy_values((u8 *)dst, (const u8 *)src, sizeof(wchar_t) * size);
  return res;
}

INTERCEPTOR(char *, strfry, char *s) {
  const auto Len = internal_strlen(s);
  char *res = REAL(strfry)(s);
  if (res)
    __nsan_set_value_unknown(reinterpret_cast<u8 *>(s), Len);
  return res;
}

INTERCEPTOR(char *, strsep, char **Stringp, const char *delim) {
  char *OrigStringp = REAL(strsep)(Stringp, delim);
  if (Stringp != nullptr) {
    // The previous character has been overwritten with a '\0' char.
    __nsan_set_value_unknown(reinterpret_cast<u8 *>(*Stringp) - 1, 1);
  }
  return OrigStringp;
}

INTERCEPTOR(char *, strtok, char *str, const char *delim) {
  // This is overly conservative, but the probability that modern code is using
  // strtok on double data is essentially zero anyway.
  if (str)
    __nsan_set_value_unknown(reinterpret_cast<u8 *>(str), internal_strlen(str));
  return REAL(strtok)(str, delim);
}

static void nsanCopyZeroTerminated(char *dst, const char *src, uptr n) {
  __nsan_copy_values(reinterpret_cast<u8 *>(dst),
                     reinterpret_cast<const u8 *>(src), n);     // Data.
  __nsan_set_value_unknown(reinterpret_cast<u8 *>(dst) + n, 1); // Terminator.
}

static void nsanWCopyZeroTerminated(wchar_t *dst, const wchar_t *src, uptr n) {
  __nsan_copy_values((u8 *)dst, (const u8 *)(src), sizeof(wchar_t) * n);
  __nsan_set_value_unknown((u8 *)(dst + n), sizeof(wchar_t));
}

INTERCEPTOR(char *, strdup, const char *S) {
  char *res = REAL(strdup)(S);
  if (res) {
    nsanCopyZeroTerminated(res, S, internal_strlen(S));
  }
  return res;
}

INTERCEPTOR(wchar_t *, wcsdup, const wchar_t *S) {
  wchar_t *res = REAL(wcsdup)(S);
  if (res) {
    nsanWCopyZeroTerminated(res, S, wcslen(S));
  }
  return res;
}

INTERCEPTOR(char *, strndup, const char *S, uptr size) {
  char *res = REAL(strndup)(S, size);
  if (res) {
    nsanCopyZeroTerminated(res, S, min(internal_strlen(S), size));
  }
  return res;
}

INTERCEPTOR(char *, strcpy, char *dst, const char *src) {
  char *res = REAL(strcpy)(dst, src);
  nsanCopyZeroTerminated(dst, src, internal_strlen(src));
  return res;
}

INTERCEPTOR(wchar_t *, wcscpy, wchar_t *dst, const wchar_t *src) {
  wchar_t *res = REAL(wcscpy)(dst, src);
  nsanWCopyZeroTerminated(dst, src, wcslen(src));
  return res;
}

INTERCEPTOR(char *, strncpy, char *dst, const char *src, uptr size) {
  char *res = REAL(strncpy)(dst, src, size);
  nsanCopyZeroTerminated(dst, src, min(size, internal_strlen(src)));
  return res;
}

INTERCEPTOR(char *, strcat, char *dst, const char *src) {
  const auto DstLenBeforeCat = internal_strlen(dst);
  char *res = REAL(strcat)(dst, src);
  nsanCopyZeroTerminated(dst + DstLenBeforeCat, src, internal_strlen(src));
  return res;
}

INTERCEPTOR(wchar_t *, wcscat, wchar_t *dst, const wchar_t *src) {
  const auto DstLenBeforeCat = wcslen(dst);
  wchar_t *res = REAL(wcscat)(dst, src);
  nsanWCopyZeroTerminated(dst + DstLenBeforeCat, src, wcslen(src));
  return res;
}

INTERCEPTOR(char *, strncat, char *dst, const char *src, uptr size) {
  const auto DstLen = internal_strlen(dst);
  char *res = REAL(strncat)(dst, src, size);
  nsanCopyZeroTerminated(dst + DstLen, src, min(size, internal_strlen(src)));
  return res;
}

INTERCEPTOR(char *, stpcpy, char *dst, const char *src) {
  char *res = REAL(stpcpy)(dst, src);
  nsanCopyZeroTerminated(dst, src, internal_strlen(src));
  return res;
}

INTERCEPTOR(wchar_t *, wcpcpy, wchar_t *dst, const wchar_t *src) {
  wchar_t *res = REAL(wcpcpy)(dst, src);
  nsanWCopyZeroTerminated(dst, src, wcslen(src));
  return res;
}

INTERCEPTOR(uptr, strxfrm, char *dst, const char *src, uptr size) {
  // This is overly conservative, but this function should very rarely be used.
  __nsan_set_value_unknown(reinterpret_cast<u8 *>(dst), internal_strlen(dst));
  const uptr res = REAL(strxfrm)(dst, src, size);
  return res;
}

void __nsan::InitializeInterceptors() {
  static bool initialized = false;
  CHECK(!initialized);

  InitializeMallocInterceptors();

  INTERCEPT_FUNCTION(memset);
  INTERCEPT_FUNCTION(wmemset);
  INTERCEPT_FUNCTION(memmove);
  INTERCEPT_FUNCTION(wmemmove);
  INTERCEPT_FUNCTION(memcpy);
  INTERCEPT_FUNCTION(wmemcpy);

  INTERCEPT_FUNCTION(strdup);
  INTERCEPT_FUNCTION(wcsdup);
  INTERCEPT_FUNCTION(strndup);
  INTERCEPT_FUNCTION(stpcpy);
  INTERCEPT_FUNCTION(wcpcpy);
  INTERCEPT_FUNCTION(strcpy);
  INTERCEPT_FUNCTION(wcscpy);
  INTERCEPT_FUNCTION(strncpy);
  INTERCEPT_FUNCTION(strcat);
  INTERCEPT_FUNCTION(wcscat);
  INTERCEPT_FUNCTION(strncat);
  INTERCEPT_FUNCTION(strxfrm);

  INTERCEPT_FUNCTION(strfry);
  INTERCEPT_FUNCTION(strsep);
  INTERCEPT_FUNCTION(strtok);

  initialized = 1;
}
