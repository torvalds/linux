//===-- dfsan_custom.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of DataFlowSanitizer.
//
// This file defines the custom functions listed in done_abilist.txt.
//===----------------------------------------------------------------------===//

#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <dlfcn.h>
#include <link.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "dfsan/dfsan.h"
#include "dfsan/dfsan_chained_origin_depot.h"
#include "dfsan/dfsan_flags.h"
#include "dfsan/dfsan_thread.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_linux.h"
#include "sanitizer_common/sanitizer_stackdepot.h"

using namespace __dfsan;

#define CALL_WEAK_INTERCEPTOR_HOOK(f, ...)                                     \
  do {                                                                         \
    if (f)                                                                     \
      f(__VA_ARGS__);                                                          \
  } while (false)
#define DECLARE_WEAK_INTERCEPTOR_HOOK(f, ...) \
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE void f(__VA_ARGS__);

#define WRAPPER_ALIAS(fun, real)                                          \
  SANITIZER_INTERFACE_ATTRIBUTE void __dfsw_##fun() ALIAS(__dfsw_##real); \
  SANITIZER_INTERFACE_ATTRIBUTE void __dfso_##fun() ALIAS(__dfso_##real);

// Async-safe, non-reentrant spin lock.
class SignalSpinLocker {
 public:
  SignalSpinLocker() {
    sigset_t all_set;
    sigfillset(&all_set);
    pthread_sigmask(SIG_SETMASK, &all_set, &saved_thread_mask_);
    sigactions_mu.Lock();
  }
  ~SignalSpinLocker() {
    sigactions_mu.Unlock();
    pthread_sigmask(SIG_SETMASK, &saved_thread_mask_, nullptr);
  }

 private:
  static StaticSpinMutex sigactions_mu;
  sigset_t saved_thread_mask_;

  SignalSpinLocker(const SignalSpinLocker &) = delete;
  SignalSpinLocker &operator=(const SignalSpinLocker &) = delete;
};

StaticSpinMutex SignalSpinLocker::sigactions_mu;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE int
__dfsw_stat(const char *path, struct stat *buf, dfsan_label path_label,
            dfsan_label buf_label, dfsan_label *ret_label) {
  int ret = stat(path, buf);
  if (ret == 0)
    dfsan_set_label(0, buf, sizeof(struct stat));
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_stat(
    const char *path, struct stat *buf, dfsan_label path_label,
    dfsan_label buf_label, dfsan_label *ret_label, dfsan_origin path_origin,
    dfsan_origin buf_origin, dfsan_origin *ret_origin) {
  int ret = __dfsw_stat(path, buf, path_label, buf_label, ret_label);
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_fstat(int fd, struct stat *buf,
                                               dfsan_label fd_label,
                                               dfsan_label buf_label,
                                               dfsan_label *ret_label) {
  int ret = fstat(fd, buf);
  if (ret == 0)
    dfsan_set_label(0, buf, sizeof(struct stat));
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_fstat(
    int fd, struct stat *buf, dfsan_label fd_label, dfsan_label buf_label,
    dfsan_label *ret_label, dfsan_origin fd_origin, dfsan_origin buf_origin,
    dfsan_origin *ret_origin) {
  int ret = __dfsw_fstat(fd, buf, fd_label, buf_label, ret_label);
  return ret;
}

static char *dfsan_strchr_with_label(const char *s, int c, size_t *bytes_read,
                                     dfsan_label s_label, dfsan_label c_label,
                                     dfsan_label *ret_label) {
  char *match_pos = nullptr;
  for (size_t i = 0;; ++i) {
    if (s[i] == c || s[i] == 0) {
      // If s[i] is the \0 at the end of the string, and \0 is not the
      // character we are searching for, then return null.
      *bytes_read = i + 1;
      match_pos = s[i] == 0 && c != 0 ? nullptr : const_cast<char *>(s + i);
      break;
    }
  }
  if (flags().strict_data_dependencies)
    *ret_label = s_label;
  else
    *ret_label = dfsan_union(dfsan_read_label(s, *bytes_read),
                             dfsan_union(s_label, c_label));
  return match_pos;
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfsw_strchr(const char *s, int c,
                                                  dfsan_label s_label,
                                                  dfsan_label c_label,
                                                  dfsan_label *ret_label) {
  size_t bytes_read;
  return dfsan_strchr_with_label(s, c, &bytes_read, s_label, c_label,
                                 ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfso_strchr(
    const char *s, int c, dfsan_label s_label, dfsan_label c_label,
    dfsan_label *ret_label, dfsan_origin s_origin, dfsan_origin c_origin,
    dfsan_origin *ret_origin) {
  size_t bytes_read;
  char *r =
      dfsan_strchr_with_label(s, c, &bytes_read, s_label, c_label, ret_label);
  if (flags().strict_data_dependencies) {
    *ret_origin = s_origin;
  } else if (*ret_label) {
    dfsan_origin o = dfsan_read_origin_of_first_taint(s, bytes_read);
    *ret_origin = o ? o : (s_label ? s_origin : c_origin);
  }
  return r;
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfsw_strpbrk(const char *s,
                                                   const char *accept,
                                                   dfsan_label s_label,
                                                   dfsan_label accept_label,
                                                   dfsan_label *ret_label) {
  const char *ret = strpbrk(s, accept);
  if (flags().strict_data_dependencies) {
    *ret_label = ret ? s_label : 0;
  } else {
    size_t s_bytes_read = (ret ? ret - s : strlen(s)) + 1;
    *ret_label =
        dfsan_union(dfsan_read_label(s, s_bytes_read),
                    dfsan_union(dfsan_read_label(accept, strlen(accept) + 1),
                                dfsan_union(s_label, accept_label)));
  }
  return const_cast<char *>(ret);
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfso_strpbrk(
    const char *s, const char *accept, dfsan_label s_label,
    dfsan_label accept_label, dfsan_label *ret_label, dfsan_origin s_origin,
    dfsan_origin accept_origin, dfsan_origin *ret_origin) {
  const char *ret = __dfsw_strpbrk(s, accept, s_label, accept_label, ret_label);
  if (flags().strict_data_dependencies) {
    if (ret)
      *ret_origin = s_origin;
  } else {
    if (*ret_label) {
      size_t s_bytes_read = (ret ? ret - s : strlen(s)) + 1;
      dfsan_origin o = dfsan_read_origin_of_first_taint(s, s_bytes_read);
      if (o) {
        *ret_origin = o;
      } else {
        o = dfsan_read_origin_of_first_taint(accept, strlen(accept) + 1);
        *ret_origin = o ? o : (s_label ? s_origin : accept_origin);
      }
    }
  }
  return const_cast<char *>(ret);
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfsw_strsep(char **s, const char *delim,
                                                  dfsan_label s_label,
                                                  dfsan_label delim_label,
                                                  dfsan_label *ret_label) {
  dfsan_label base_label = dfsan_read_label(s, sizeof(*s));
  char *base = *s;
  char *res = strsep(s, delim);
  if (res != *s) {
    char *token_start = res;
    int token_length = strlen(res);
    // the delimiter byte has been set to NULL
    dfsan_set_label(0, token_start + token_length, 1);
  }

  if (flags().strict_data_dependencies) {
    *ret_label = res ? base_label : 0;
  } else {
    size_t s_bytes_read = (res ? strlen(res) : strlen(base)) + 1;
    *ret_label = dfsan_union(
        dfsan_union(base_label, dfsan_read_label(base, sizeof(s_bytes_read))),
        dfsan_union(dfsan_read_label(delim, strlen(delim) + 1),
                    dfsan_union(s_label, delim_label)));
  }

  return res;
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfso_strsep(
    char **s, const char *delim, dfsan_label s_label, dfsan_label delim_label,
    dfsan_label *ret_label, dfsan_origin s_origin, dfsan_origin delim_origin,
    dfsan_origin *ret_origin) {
  dfsan_origin base_origin = dfsan_read_origin_of_first_taint(s, sizeof(*s));
  char *res = __dfsw_strsep(s, delim, s_label, delim_label, ret_label);
  if (flags().strict_data_dependencies) {
    if (res)
      *ret_origin = base_origin;
  } else {
    if (*ret_label) {
      if (base_origin) {
        *ret_origin = base_origin;
      } else {
        dfsan_origin o =
            dfsan_read_origin_of_first_taint(delim, strlen(delim) + 1);
        *ret_origin = o ? o : (s_label ? s_origin : delim_origin);
      }
    }
  }

  return res;
}

static int dfsan_memcmp_bcmp(const void *s1, const void *s2, size_t n,
                             size_t *bytes_read) {
  const char *cs1 = (const char *) s1, *cs2 = (const char *) s2;
  for (size_t i = 0; i != n; ++i) {
    if (cs1[i] != cs2[i]) {
      *bytes_read = i + 1;
      return cs1[i] - cs2[i];
    }
  }
  *bytes_read = n;
  return 0;
}

static dfsan_label dfsan_get_memcmp_label(const void *s1, const void *s2,
                                          size_t pos) {
  if (flags().strict_data_dependencies)
    return 0;
  return dfsan_union(dfsan_read_label(s1, pos), dfsan_read_label(s2, pos));
}

static void dfsan_get_memcmp_origin(const void *s1, const void *s2, size_t pos,
                                    dfsan_label *ret_label,
                                    dfsan_origin *ret_origin) {
  *ret_label = dfsan_get_memcmp_label(s1, s2, pos);
  if (*ret_label == 0)
    return;
  dfsan_origin o = dfsan_read_origin_of_first_taint(s1, pos);
  *ret_origin = o ? o : dfsan_read_origin_of_first_taint(s2, pos);
}

static int dfsan_memcmp_bcmp_label(const void *s1, const void *s2, size_t n,
                                   dfsan_label *ret_label) {
  size_t bytes_read;
  int r = dfsan_memcmp_bcmp(s1, s2, n, &bytes_read);
  *ret_label = dfsan_get_memcmp_label(s1, s2, bytes_read);
  return r;
}

static int dfsan_memcmp_bcmp_origin(const void *s1, const void *s2, size_t n,
                                    dfsan_label *ret_label,
                                    dfsan_origin *ret_origin) {
  size_t bytes_read;
  int r = dfsan_memcmp_bcmp(s1, s2, n, &bytes_read);
  dfsan_get_memcmp_origin(s1, s2, bytes_read, ret_label, ret_origin);
  return r;
}

DECLARE_WEAK_INTERCEPTOR_HOOK(dfsan_weak_hook_memcmp, uptr caller_pc,
                              const void *s1, const void *s2, size_t n,
                              dfsan_label s1_label, dfsan_label s2_label,
                              dfsan_label n_label)

DECLARE_WEAK_INTERCEPTOR_HOOK(dfsan_weak_hook_origin_memcmp, uptr caller_pc,
                              const void *s1, const void *s2, size_t n,
                              dfsan_label s1_label, dfsan_label s2_label,
                              dfsan_label n_label, dfsan_origin s1_origin,
                              dfsan_origin s2_origin, dfsan_origin n_origin)

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_memcmp(const void *s1, const void *s2,
                                                size_t n, dfsan_label s1_label,
                                                dfsan_label s2_label,
                                                dfsan_label n_label,
                                                dfsan_label *ret_label) {
  CALL_WEAK_INTERCEPTOR_HOOK(dfsan_weak_hook_memcmp, GET_CALLER_PC(), s1, s2, n,
                             s1_label, s2_label, n_label);
  return dfsan_memcmp_bcmp_label(s1, s2, n, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_memcmp(
    const void *s1, const void *s2, size_t n, dfsan_label s1_label,
    dfsan_label s2_label, dfsan_label n_label, dfsan_label *ret_label,
    dfsan_origin s1_origin, dfsan_origin s2_origin, dfsan_origin n_origin,
    dfsan_origin *ret_origin) {
  CALL_WEAK_INTERCEPTOR_HOOK(dfsan_weak_hook_origin_memcmp, GET_CALLER_PC(), s1,
                             s2, n, s1_label, s2_label, n_label, s1_origin,
                             s2_origin, n_origin);
  return dfsan_memcmp_bcmp_origin(s1, s2, n, ret_label, ret_origin);
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_bcmp(const void *s1, const void *s2,
                                              size_t n, dfsan_label s1_label,
                                              dfsan_label s2_label,
                                              dfsan_label n_label,
                                              dfsan_label *ret_label) {
  return dfsan_memcmp_bcmp_label(s1, s2, n, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_bcmp(
    const void *s1, const void *s2, size_t n, dfsan_label s1_label,
    dfsan_label s2_label, dfsan_label n_label, dfsan_label *ret_label,
    dfsan_origin s1_origin, dfsan_origin s2_origin, dfsan_origin n_origin,
    dfsan_origin *ret_origin) {
  return dfsan_memcmp_bcmp_origin(s1, s2, n, ret_label, ret_origin);
}

// When n == 0, compare strings without byte limit.
// When n > 0, compare the first (at most) n bytes of s1 and s2.
static int dfsan_strncmp(const char *s1, const char *s2, size_t n,
                         size_t *bytes_read) {
  for (size_t i = 0;; ++i) {
    if (s1[i] != s2[i] || s1[i] == 0 || s2[i] == 0 || (n > 0 && i == n - 1)) {
      *bytes_read = i + 1;
      return s1[i] - s2[i];
    }
  }
}

DECLARE_WEAK_INTERCEPTOR_HOOK(dfsan_weak_hook_strcmp, uptr caller_pc,
                              const char *s1, const char *s2,
                              dfsan_label s1_label, dfsan_label s2_label)

DECLARE_WEAK_INTERCEPTOR_HOOK(dfsan_weak_hook_origin_strcmp, uptr caller_pc,
                              const char *s1, const char *s2,
                              dfsan_label s1_label, dfsan_label s2_label,
                              dfsan_origin s1_origin, dfsan_origin s2_origin)

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_strcmp(const char *s1, const char *s2,
                                                dfsan_label s1_label,
                                                dfsan_label s2_label,
                                                dfsan_label *ret_label) {
  CALL_WEAK_INTERCEPTOR_HOOK(dfsan_weak_hook_strcmp, GET_CALLER_PC(), s1, s2,
                             s1_label, s2_label);
  size_t bytes_read;
  int r = dfsan_strncmp(s1, s2, 0, &bytes_read);
  *ret_label = dfsan_get_memcmp_label(s1, s2, bytes_read);
  return r;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_strcmp(
    const char *s1, const char *s2, dfsan_label s1_label, dfsan_label s2_label,
    dfsan_label *ret_label, dfsan_origin s1_origin, dfsan_origin s2_origin,
    dfsan_origin *ret_origin) {
  CALL_WEAK_INTERCEPTOR_HOOK(dfsan_weak_hook_origin_strcmp, GET_CALLER_PC(), s1,
                             s2, s1_label, s2_label, s1_origin, s2_origin);
  size_t bytes_read;
  int r = dfsan_strncmp(s1, s2, 0, &bytes_read);
  dfsan_get_memcmp_origin(s1, s2, bytes_read, ret_label, ret_origin);
  return r;
}

// When n == 0, compare strings without byte limit.
// When n > 0, compare the first (at most) n bytes of s1 and s2.
static int dfsan_strncasecmp(const char *s1, const char *s2, size_t n,
                             size_t *bytes_read) {
  for (size_t i = 0;; ++i) {
    char s1_lower = tolower(s1[i]);
    char s2_lower = tolower(s2[i]);

    if (s1_lower != s2_lower || s1[i] == 0 || s2[i] == 0 ||
        (n > 0 && i == n - 1)) {
      *bytes_read = i + 1;
      return s1_lower - s2_lower;
    }
  }
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_strcasecmp(const char *s1,
                                                    const char *s2,
                                                    dfsan_label s1_label,
                                                    dfsan_label s2_label,
                                                    dfsan_label *ret_label) {
  size_t bytes_read;
  int r = dfsan_strncasecmp(s1, s2, 0, &bytes_read);
  *ret_label = dfsan_get_memcmp_label(s1, s2, bytes_read);
  return r;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_strcasecmp(
    const char *s1, const char *s2, dfsan_label s1_label, dfsan_label s2_label,
    dfsan_label *ret_label, dfsan_origin s1_origin, dfsan_origin s2_origin,
    dfsan_origin *ret_origin) {
  size_t bytes_read;
  int r = dfsan_strncasecmp(s1, s2, 0, &bytes_read);
  dfsan_get_memcmp_origin(s1, s2, bytes_read, ret_label, ret_origin);
  return r;
}

DECLARE_WEAK_INTERCEPTOR_HOOK(dfsan_weak_hook_strncmp, uptr caller_pc,
                              const char *s1, const char *s2, size_t n,
                              dfsan_label s1_label, dfsan_label s2_label,
                              dfsan_label n_label)

DECLARE_WEAK_INTERCEPTOR_HOOK(dfsan_weak_hook_origin_strncmp, uptr caller_pc,
                              const char *s1, const char *s2, size_t n,
                              dfsan_label s1_label, dfsan_label s2_label,
                              dfsan_label n_label, dfsan_origin s1_origin,
                              dfsan_origin s2_origin, dfsan_origin n_origin)

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_strncmp(const char *s1, const char *s2,
                                                 size_t n, dfsan_label s1_label,
                                                 dfsan_label s2_label,
                                                 dfsan_label n_label,
                                                 dfsan_label *ret_label) {
  if (n == 0) {
    *ret_label = 0;
    return 0;
  }

  CALL_WEAK_INTERCEPTOR_HOOK(dfsan_weak_hook_strncmp, GET_CALLER_PC(), s1, s2,
                             n, s1_label, s2_label, n_label);

  size_t bytes_read;
  int r = dfsan_strncmp(s1, s2, n, &bytes_read);
  *ret_label = dfsan_get_memcmp_label(s1, s2, bytes_read);
  return r;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_strncmp(
    const char *s1, const char *s2, size_t n, dfsan_label s1_label,
    dfsan_label s2_label, dfsan_label n_label, dfsan_label *ret_label,
    dfsan_origin s1_origin, dfsan_origin s2_origin, dfsan_origin n_origin,
    dfsan_origin *ret_origin) {
  if (n == 0) {
    *ret_label = 0;
    return 0;
  }

  CALL_WEAK_INTERCEPTOR_HOOK(dfsan_weak_hook_origin_strncmp, GET_CALLER_PC(),
                             s1, s2, n, s1_label, s2_label, n_label, s1_origin,
                             s2_origin, n_origin);

  size_t bytes_read;
  int r = dfsan_strncmp(s1, s2, n, &bytes_read);
  dfsan_get_memcmp_origin(s1, s2, bytes_read, ret_label, ret_origin);
  return r;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_strncasecmp(
    const char *s1, const char *s2, size_t n, dfsan_label s1_label,
    dfsan_label s2_label, dfsan_label n_label, dfsan_label *ret_label) {
  if (n == 0) {
    *ret_label = 0;
    return 0;
  }

  size_t bytes_read;
  int r = dfsan_strncasecmp(s1, s2, n, &bytes_read);
  *ret_label = dfsan_get_memcmp_label(s1, s2, bytes_read);
  return r;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_strncasecmp(
    const char *s1, const char *s2, size_t n, dfsan_label s1_label,
    dfsan_label s2_label, dfsan_label n_label, dfsan_label *ret_label,
    dfsan_origin s1_origin, dfsan_origin s2_origin, dfsan_origin n_origin,
    dfsan_origin *ret_origin) {
  if (n == 0) {
    *ret_label = 0;
    return 0;
  }

  size_t bytes_read;
  int r = dfsan_strncasecmp(s1, s2, n, &bytes_read);
  dfsan_get_memcmp_origin(s1, s2, bytes_read, ret_label, ret_origin);
  return r;
}


SANITIZER_INTERFACE_ATTRIBUTE size_t
__dfsw_strlen(const char *s, dfsan_label s_label, dfsan_label *ret_label) {
  size_t ret = strlen(s);
  if (flags().strict_data_dependencies) {
    *ret_label = 0;
  } else {
    *ret_label = dfsan_read_label(s, ret + 1);
  }
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE size_t __dfso_strlen(const char *s,
                                                   dfsan_label s_label,
                                                   dfsan_label *ret_label,
                                                   dfsan_origin s_origin,
                                                   dfsan_origin *ret_origin) {
  size_t ret = __dfsw_strlen(s, s_label, ret_label);
  if (!flags().strict_data_dependencies)
    *ret_origin = dfsan_read_origin_of_first_taint(s, ret + 1);
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE size_t __dfsw_strnlen(const char *s,
                                                    size_t maxlen,
                                                    dfsan_label s_label,
                                                    dfsan_label maxlen_label,
                                                    dfsan_label *ret_label) {
  size_t ret = strnlen(s, maxlen);
  if (flags().strict_data_dependencies) {
    *ret_label = 0;
  } else {
    size_t full_len = strlen(s);
    size_t covered_len = maxlen > (full_len + 1) ? (full_len + 1) : maxlen;
    *ret_label = dfsan_union(maxlen_label, dfsan_read_label(s, covered_len));
  }
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE size_t __dfso_strnlen(
    const char *s, size_t maxlen, dfsan_label s_label, dfsan_label maxlen_label,
    dfsan_label *ret_label, dfsan_origin s_origin, dfsan_origin maxlen_origin,
    dfsan_origin *ret_origin) {
  size_t ret = __dfsw_strnlen(s, maxlen, s_label, maxlen_label, ret_label);
  if (!flags().strict_data_dependencies) {
    size_t full_len = strlen(s);
    size_t covered_len = maxlen > (full_len + 1) ? (full_len + 1) : maxlen;
    dfsan_origin o = dfsan_read_origin_of_first_taint(s, covered_len);
    *ret_origin = o ? o : maxlen_origin;
  }
  return ret;
}

static void *dfsan_memmove(void *dest, const void *src, size_t n) {
  dfsan_label *sdest = shadow_for(dest);
  const dfsan_label *ssrc = shadow_for(src);
  internal_memmove((void *)sdest, (const void *)ssrc, n * sizeof(dfsan_label));
  return internal_memmove(dest, src, n);
}

static void *dfsan_memmove_with_origin(void *dest, const void *src, size_t n) {
  dfsan_mem_origin_transfer(dest, src, n);
  return dfsan_memmove(dest, src, n);
}

static void *dfsan_memcpy(void *dest, const void *src, size_t n) {
  dfsan_mem_shadow_transfer(dest, src, n);
  return internal_memcpy(dest, src, n);
}

static void *dfsan_memcpy_with_origin(void *dest, const void *src, size_t n) {
  dfsan_mem_origin_transfer(dest, src, n);
  return dfsan_memcpy(dest, src, n);
}

static void dfsan_memset(void *s, int c, dfsan_label c_label, size_t n) {
  internal_memset(s, c, n);
  dfsan_set_label(c_label, s, n);
}

static void dfsan_memset_with_origin(void *s, int c, dfsan_label c_label,
                                     dfsan_origin c_origin, size_t n) {
  internal_memset(s, c, n);
  dfsan_set_label_origin(c_label, c_origin, s, n);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__dfsw_memcpy(void *dest, const void *src, size_t n,
                    dfsan_label dest_label, dfsan_label src_label,
                    dfsan_label n_label, dfsan_label *ret_label) {
  *ret_label = dest_label;
  return dfsan_memcpy(dest, src, n);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__dfso_memcpy(void *dest, const void *src, size_t n,
                    dfsan_label dest_label, dfsan_label src_label,
                    dfsan_label n_label, dfsan_label *ret_label,
                    dfsan_origin dest_origin, dfsan_origin src_origin,
                    dfsan_origin n_origin, dfsan_origin *ret_origin) {
  *ret_label = dest_label;
  *ret_origin = dest_origin;
  return dfsan_memcpy_with_origin(dest, src, n);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__dfsw_memmove(void *dest, const void *src, size_t n,
                     dfsan_label dest_label, dfsan_label src_label,
                     dfsan_label n_label, dfsan_label *ret_label) {
  *ret_label = dest_label;
  return dfsan_memmove(dest, src, n);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__dfso_memmove(void *dest, const void *src, size_t n,
                     dfsan_label dest_label, dfsan_label src_label,
                     dfsan_label n_label, dfsan_label *ret_label,
                     dfsan_origin dest_origin, dfsan_origin src_origin,
                     dfsan_origin n_origin, dfsan_origin *ret_origin) {
  *ret_label = dest_label;
  *ret_origin = dest_origin;
  return dfsan_memmove_with_origin(dest, src, n);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__dfsw_memset(void *s, int c, size_t n,
                    dfsan_label s_label, dfsan_label c_label,
                    dfsan_label n_label, dfsan_label *ret_label) {
  dfsan_memset(s, c, c_label, n);
  *ret_label = s_label;
  return s;
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__dfso_memset(void *s, int c, size_t n, dfsan_label s_label,
                    dfsan_label c_label, dfsan_label n_label,
                    dfsan_label *ret_label, dfsan_origin s_origin,
                    dfsan_origin c_origin, dfsan_origin n_origin,
                    dfsan_origin *ret_origin) {
  dfsan_memset_with_origin(s, c, c_label, c_origin, n);
  *ret_label = s_label;
  *ret_origin = s_origin;
  return s;
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfsw_strcat(char *dest, const char *src,
                                                  dfsan_label dest_label,
                                                  dfsan_label src_label,
                                                  dfsan_label *ret_label) {
  size_t dest_len = strlen(dest);
  char *ret = strcat(dest, src);
  dfsan_mem_shadow_transfer(dest + dest_len, src, strlen(src));
  *ret_label = dest_label;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfso_strcat(
    char *dest, const char *src, dfsan_label dest_label, dfsan_label src_label,
    dfsan_label *ret_label, dfsan_origin dest_origin, dfsan_origin src_origin,
    dfsan_origin *ret_origin) {
  size_t dest_len = strlen(dest);
  char *ret = strcat(dest, src);
  size_t src_len = strlen(src);
  dfsan_mem_origin_transfer(dest + dest_len, src, src_len);
  dfsan_mem_shadow_transfer(dest + dest_len, src, src_len);
  *ret_label = dest_label;
  *ret_origin = dest_origin;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfsw_strncat(
    char *dest, const char *src, size_t num, dfsan_label dest_label,
    dfsan_label src_label, dfsan_label num_label, dfsan_label *ret_label) {
  size_t src_len = strlen(src);
  src_len = src_len < num ? src_len : num;
  size_t dest_len = strlen(dest);

  char *ret = strncat(dest, src, num);
  dfsan_mem_shadow_transfer(dest + dest_len, src, src_len);
  *ret_label = dest_label;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfso_strncat(
    char *dest, const char *src, size_t num, dfsan_label dest_label,
    dfsan_label src_label, dfsan_label num_label, dfsan_label *ret_label,
    dfsan_origin dest_origin, dfsan_origin src_origin, dfsan_origin num_origin,
    dfsan_origin *ret_origin) {
  size_t src_len = strlen(src);
  src_len = src_len < num ? src_len : num;
  size_t dest_len = strlen(dest);

  char *ret = strncat(dest, src, num);

  dfsan_mem_origin_transfer(dest + dest_len, src, src_len);
  dfsan_mem_shadow_transfer(dest + dest_len, src, src_len);
  *ret_label = dest_label;
  *ret_origin = dest_origin;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE char *
__dfsw_strdup(const char *s, dfsan_label s_label, dfsan_label *ret_label) {
  size_t len = strlen(s);
  void *p = malloc(len+1);
  dfsan_memcpy(p, s, len+1);
  *ret_label = 0;
  return static_cast<char *>(p);
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfso_strdup(const char *s,
                                                  dfsan_label s_label,
                                                  dfsan_label *ret_label,
                                                  dfsan_origin s_origin,
                                                  dfsan_origin *ret_origin) {
  size_t len = strlen(s);
  void *p = malloc(len + 1);
  dfsan_memcpy_with_origin(p, s, len + 1);
  *ret_label = 0;
  return static_cast<char *>(p);
}

SANITIZER_INTERFACE_ATTRIBUTE char *
__dfsw_strncpy(char *s1, const char *s2, size_t n, dfsan_label s1_label,
               dfsan_label s2_label, dfsan_label n_label,
               dfsan_label *ret_label) {
  size_t len = strlen(s2);
  if (len < n) {
    dfsan_memcpy(s1, s2, len+1);
    dfsan_memset(s1+len+1, 0, 0, n-len-1);
  } else {
    dfsan_memcpy(s1, s2, n);
  }

  *ret_label = s1_label;
  return s1;
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfso_strncpy(
    char *s1, const char *s2, size_t n, dfsan_label s1_label,
    dfsan_label s2_label, dfsan_label n_label, dfsan_label *ret_label,
    dfsan_origin s1_origin, dfsan_origin s2_origin, dfsan_origin n_origin,
    dfsan_origin *ret_origin) {
  size_t len = strlen(s2);
  if (len < n) {
    dfsan_memcpy_with_origin(s1, s2, len + 1);
    dfsan_memset_with_origin(s1 + len + 1, 0, 0, 0, n - len - 1);
  } else {
    dfsan_memcpy_with_origin(s1, s2, n);
  }

  *ret_label = s1_label;
  *ret_origin = s1_origin;
  return s1;
}

SANITIZER_INTERFACE_ATTRIBUTE ssize_t
__dfsw_pread(int fd, void *buf, size_t count, off_t offset,
             dfsan_label fd_label, dfsan_label buf_label,
             dfsan_label count_label, dfsan_label offset_label,
             dfsan_label *ret_label) {
  ssize_t ret = pread(fd, buf, count, offset);
  if (ret > 0)
    dfsan_set_label(0, buf, ret);
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE ssize_t __dfso_pread(
    int fd, void *buf, size_t count, off_t offset, dfsan_label fd_label,
    dfsan_label buf_label, dfsan_label count_label, dfsan_label offset_label,
    dfsan_label *ret_label, dfsan_origin fd_origin, dfsan_origin buf_origin,
    dfsan_origin count_origin, dfsan_label offset_origin,
    dfsan_origin *ret_origin) {
  return __dfsw_pread(fd, buf, count, offset, fd_label, buf_label, count_label,
                      offset_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE ssize_t
__dfsw_read(int fd, void *buf, size_t count,
             dfsan_label fd_label, dfsan_label buf_label,
             dfsan_label count_label,
             dfsan_label *ret_label) {
  ssize_t ret = read(fd, buf, count);
  if (ret > 0)
    dfsan_set_label(0, buf, ret);
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE ssize_t __dfso_read(
    int fd, void *buf, size_t count, dfsan_label fd_label,
    dfsan_label buf_label, dfsan_label count_label, dfsan_label *ret_label,
    dfsan_origin fd_origin, dfsan_origin buf_origin, dfsan_origin count_origin,
    dfsan_origin *ret_origin) {
  return __dfsw_read(fd, buf, count, fd_label, buf_label, count_label,
                     ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_clock_gettime(clockid_t clk_id,
                                                       struct timespec *tp,
                                                       dfsan_label clk_id_label,
                                                       dfsan_label tp_label,
                                                       dfsan_label *ret_label) {
  int ret = clock_gettime(clk_id, tp);
  if (ret == 0)
    dfsan_set_label(0, tp, sizeof(struct timespec));
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_clock_gettime(
    clockid_t clk_id, struct timespec *tp, dfsan_label clk_id_label,
    dfsan_label tp_label, dfsan_label *ret_label, dfsan_origin clk_id_origin,
    dfsan_origin tp_origin, dfsan_origin *ret_origin) {
  return __dfsw_clock_gettime(clk_id, tp, clk_id_label, tp_label, ret_label);
}

static void dfsan_set_zero_label(const void *ptr, uptr size) {
  dfsan_set_label(0, const_cast<void *>(ptr), size);
}

// dlopen() ultimately calls mmap() down inside the loader, which generally
// doesn't participate in dynamic symbol resolution.  Therefore we won't
// intercept its calls to mmap, and we have to hook it here.
SANITIZER_INTERFACE_ATTRIBUTE void *
__dfsw_dlopen(const char *filename, int flag, dfsan_label filename_label,
              dfsan_label flag_label, dfsan_label *ret_label) {
  void *handle = dlopen(filename, flag);
  link_map *map = GET_LINK_MAP_BY_DLOPEN_HANDLE(handle);
  if (filename && map)
    ForEachMappedRegion(map, dfsan_set_zero_label);
  *ret_label = 0;
  return handle;
}

SANITIZER_INTERFACE_ATTRIBUTE void *__dfso_dlopen(
    const char *filename, int flag, dfsan_label filename_label,
    dfsan_label flag_label, dfsan_label *ret_label,
    dfsan_origin filename_origin, dfsan_origin flag_origin,
    dfsan_origin *ret_origin) {
  return __dfsw_dlopen(filename, flag, filename_label, flag_label, ret_label);
}

static void *DFsanThreadStartFunc(void *arg) {
  DFsanThread *t = (DFsanThread *)arg;
  SetCurrentThread(t);
  t->Init();
  SetSigProcMask(&t->starting_sigset_, nullptr);
  return t->ThreadStart();
}

static int dfsan_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                void *start_routine, void *arg,
                                dfsan_label *ret_label,
                                bool track_origins = false) {
  pthread_attr_t myattr;
  if (!attr) {
    pthread_attr_init(&myattr);
    attr = &myattr;
  }

  // Ensure that the thread stack is large enough to hold all TLS data.
  AdjustStackSize((void *)(const_cast<pthread_attr_t *>(attr)));

  DFsanThread *t =
      DFsanThread::Create((thread_callback_t)start_routine, arg, track_origins);
  ScopedBlockSignals block(&t->starting_sigset_);
  int res = pthread_create(thread, attr, DFsanThreadStartFunc, t);

  if (attr == &myattr)
    pthread_attr_destroy(&myattr);
  *ret_label = 0;
  return res;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_pthread_create(
    pthread_t *thread, const pthread_attr_t *attr, void *start_routine,
    void *arg, dfsan_label thread_label, dfsan_label attr_label,
    dfsan_label start_routine_label, dfsan_label arg_label,
    dfsan_label *ret_label) {
  return dfsan_pthread_create(thread, attr, start_routine, arg, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_pthread_create(
    pthread_t *thread, const pthread_attr_t *attr, void *start_routine,
    void *arg, dfsan_label thread_label, dfsan_label attr_label,
    dfsan_label start_routine_label, dfsan_label arg_label,
    dfsan_label *ret_label, dfsan_origin thread_origin,
    dfsan_origin attr_origin, dfsan_origin start_routine_origin,
    dfsan_origin arg_origin, dfsan_origin *ret_origin) {
  return dfsan_pthread_create(thread, attr, start_routine, arg, ret_label,
                              true);
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_pthread_join(pthread_t thread,
                                                      void **retval,
                                                      dfsan_label thread_label,
                                                      dfsan_label retval_label,
                                                      dfsan_label *ret_label) {
  int ret = pthread_join(thread, retval);
  if (ret == 0 && retval)
    dfsan_set_label(0, retval, sizeof(*retval));
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_pthread_join(
    pthread_t thread, void **retval, dfsan_label thread_label,
    dfsan_label retval_label, dfsan_label *ret_label,
    dfsan_origin thread_origin, dfsan_origin retval_origin,
    dfsan_origin *ret_origin) {
  return __dfsw_pthread_join(thread, retval, thread_label, retval_label,
                             ret_label);
}

struct dl_iterate_phdr_info {
  int (*callback)(struct dl_phdr_info *info, size_t size, void *data);
  void *data;
};

int dl_iterate_phdr_cb(struct dl_phdr_info *info, size_t size, void *data) {
  dl_iterate_phdr_info *dipi = (dl_iterate_phdr_info *)data;
  dfsan_set_label(0, *info);
  dfsan_set_label(0, const_cast<char *>(info->dlpi_name),
                  strlen(info->dlpi_name) + 1);
  dfsan_set_label(
      0, const_cast<char *>(reinterpret_cast<const char *>(info->dlpi_phdr)),
      sizeof(*info->dlpi_phdr) * info->dlpi_phnum);

  dfsan_clear_thread_local_state();
  return dipi->callback(info, size, dipi->data);
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_dl_iterate_phdr(
    int (*callback)(struct dl_phdr_info *info, size_t size, void *data),
    void *data, dfsan_label callback_label, dfsan_label data_label,
    dfsan_label *ret_label) {
  dl_iterate_phdr_info dipi = {callback, data};
  *ret_label = 0;
  return dl_iterate_phdr(dl_iterate_phdr_cb, &dipi);
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_dl_iterate_phdr(
    int (*callback)(struct dl_phdr_info *info, size_t size, void *data),
    void *data, dfsan_label callback_label, dfsan_label data_label,
    dfsan_label *ret_label, dfsan_origin callback_origin,
    dfsan_origin data_origin, dfsan_origin *ret_origin) {
  dl_iterate_phdr_info dipi = {callback, data};
  *ret_label = 0;
  return dl_iterate_phdr(dl_iterate_phdr_cb, &dipi);
}

// This function is only available for glibc 2.27 or newer.  Mark it weak so
// linking succeeds with older glibcs.
SANITIZER_WEAK_ATTRIBUTE void _dl_get_tls_static_info(size_t *sizep,
                                                      size_t *alignp);

SANITIZER_INTERFACE_ATTRIBUTE void __dfsw__dl_get_tls_static_info(
    size_t *sizep, size_t *alignp, dfsan_label sizep_label,
    dfsan_label alignp_label) {
  assert(_dl_get_tls_static_info);
  _dl_get_tls_static_info(sizep, alignp);
  dfsan_set_label(0, sizep, sizeof(*sizep));
  dfsan_set_label(0, alignp, sizeof(*alignp));
}

SANITIZER_INTERFACE_ATTRIBUTE void __dfso__dl_get_tls_static_info(
    size_t *sizep, size_t *alignp, dfsan_label sizep_label,
    dfsan_label alignp_label, dfsan_origin sizep_origin,
    dfsan_origin alignp_origin) {
  __dfsw__dl_get_tls_static_info(sizep, alignp, sizep_label, alignp_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
char *__dfsw_ctime_r(const time_t *timep, char *buf, dfsan_label timep_label,
                     dfsan_label buf_label, dfsan_label *ret_label) {
  char *ret = ctime_r(timep, buf);
  if (ret) {
    dfsan_set_label(dfsan_read_label(timep, sizeof(time_t)), buf,
                    strlen(buf) + 1);
    *ret_label = buf_label;
  } else {
    *ret_label = 0;
  }
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
char *__dfso_ctime_r(const time_t *timep, char *buf, dfsan_label timep_label,
                     dfsan_label buf_label, dfsan_label *ret_label,
                     dfsan_origin timep_origin, dfsan_origin buf_origin,
                     dfsan_origin *ret_origin) {
  char *ret = ctime_r(timep, buf);
  if (ret) {
    dfsan_set_label_origin(
        dfsan_read_label(timep, sizeof(time_t)),
        dfsan_read_origin_of_first_taint(timep, sizeof(time_t)), buf,
        strlen(buf) + 1);
    *ret_label = buf_label;
    *ret_origin = buf_origin;
  } else {
    *ret_label = 0;
  }
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
char *__dfsw_fgets(char *s, int size, FILE *stream, dfsan_label s_label,
                   dfsan_label size_label, dfsan_label stream_label,
                   dfsan_label *ret_label) {
  char *ret = fgets(s, size, stream);
  if (ret) {
    dfsan_set_label(0, ret, strlen(ret) + 1);
    *ret_label = s_label;
  } else {
    *ret_label = 0;
  }
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
char *__dfso_fgets(char *s, int size, FILE *stream, dfsan_label s_label,
                   dfsan_label size_label, dfsan_label stream_label,
                   dfsan_label *ret_label, dfsan_origin s_origin,
                   dfsan_origin size_origin, dfsan_origin stream_origin,
                   dfsan_origin *ret_origin) {
  char *ret = __dfsw_fgets(s, size, stream, s_label, size_label, stream_label,
                           ret_label);
  if (ret)
    *ret_origin = s_origin;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
char *__dfsw_getcwd(char *buf, size_t size, dfsan_label buf_label,
                    dfsan_label size_label, dfsan_label *ret_label) {
  char *ret = getcwd(buf, size);
  if (ret) {
    dfsan_set_label(0, ret, strlen(ret) + 1);
    *ret_label = buf_label;
  } else {
    *ret_label = 0;
  }
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
char *__dfso_getcwd(char *buf, size_t size, dfsan_label buf_label,
                    dfsan_label size_label, dfsan_label *ret_label,
                    dfsan_origin buf_origin, dfsan_origin size_origin,
                    dfsan_origin *ret_origin) {
  char *ret = __dfsw_getcwd(buf, size, buf_label, size_label, ret_label);
  if (ret)
    *ret_origin = buf_origin;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
char *__dfsw_get_current_dir_name(dfsan_label *ret_label) {
  char *ret = get_current_dir_name();
  if (ret)
    dfsan_set_label(0, ret, strlen(ret) + 1);
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
char *__dfso_get_current_dir_name(dfsan_label *ret_label,
                                  dfsan_origin *ret_origin) {
  return __dfsw_get_current_dir_name(ret_label);
}

// This function is only available for glibc 2.25 or newer.  Mark it weak so
// linking succeeds with older glibcs.
SANITIZER_WEAK_ATTRIBUTE int getentropy(void *buffer, size_t length);

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_getentropy(void *buffer, size_t length,
                                                    dfsan_label buffer_label,
                                                    dfsan_label length_label,
                                                    dfsan_label *ret_label) {
  int ret = getentropy(buffer, length);
  if (ret == 0) {
    dfsan_set_label(0, buffer, length);
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_getentropy(void *buffer, size_t length,
                                                    dfsan_label buffer_label,
                                                    dfsan_label length_label,
                                                    dfsan_label *ret_label,
                                                    dfsan_origin buffer_origin,
                                                    dfsan_origin length_origin,
                                                    dfsan_origin *ret_origin) {
  return __dfsw_getentropy(buffer, length, buffer_label, length_label,
                           ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_gethostname(char *name, size_t len, dfsan_label name_label,
                       dfsan_label len_label, dfsan_label *ret_label) {
  int ret = gethostname(name, len);
  if (ret == 0) {
    dfsan_set_label(0, name, strlen(name) + 1);
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_gethostname(char *name, size_t len, dfsan_label name_label,
                       dfsan_label len_label, dfsan_label *ret_label,
                       dfsan_origin name_origin, dfsan_origin len_origin,
                       dfsan_label *ret_origin) {
  return __dfsw_gethostname(name, len, name_label, len_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_getrlimit(int resource, struct rlimit *rlim,
                     dfsan_label resource_label, dfsan_label rlim_label,
                     dfsan_label *ret_label) {
  int ret = getrlimit(resource, rlim);
  if (ret == 0) {
    dfsan_set_label(0, rlim, sizeof(struct rlimit));
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_getrlimit(int resource, struct rlimit *rlim,
                     dfsan_label resource_label, dfsan_label rlim_label,
                     dfsan_label *ret_label, dfsan_origin resource_origin,
                     dfsan_origin rlim_origin, dfsan_origin *ret_origin) {
  return __dfsw_getrlimit(resource, rlim, resource_label, rlim_label,
                          ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_getrusage(int who, struct rusage *usage, dfsan_label who_label,
                     dfsan_label usage_label, dfsan_label *ret_label) {
  int ret = getrusage(who, usage);
  if (ret == 0) {
    dfsan_set_label(0, usage, sizeof(struct rusage));
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_getrusage(int who, struct rusage *usage, dfsan_label who_label,
                     dfsan_label usage_label, dfsan_label *ret_label,
                     dfsan_origin who_origin, dfsan_origin usage_origin,
                     dfsan_label *ret_origin) {
  return __dfsw_getrusage(who, usage, who_label, usage_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
char *__dfsw_strcpy(char *dest, const char *src, dfsan_label dst_label,
                    dfsan_label src_label, dfsan_label *ret_label) {
  char *ret = strcpy(dest, src);
  if (ret) {
    dfsan_mem_shadow_transfer(dest, src, strlen(src) + 1);
  }
  *ret_label = dst_label;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
char *__dfso_strcpy(char *dest, const char *src, dfsan_label dst_label,
                    dfsan_label src_label, dfsan_label *ret_label,
                    dfsan_origin dst_origin, dfsan_origin src_origin,
                    dfsan_origin *ret_origin) {
  char *ret = strcpy(dest, src);
  if (ret) {
    size_t str_len = strlen(src) + 1;
    dfsan_mem_origin_transfer(dest, src, str_len);
    dfsan_mem_shadow_transfer(dest, src, str_len);
  }
  *ret_label = dst_label;
  *ret_origin = dst_origin;
  return ret;
}
}

template <typename Fn>
static ALWAYS_INLINE auto dfsan_strtol_impl(
    Fn real, const char *nptr, char **endptr, int base,
    char **tmp_endptr) -> decltype(real(nullptr, nullptr, 0)) {
  assert(tmp_endptr);
  auto ret = real(nptr, tmp_endptr, base);
  if (endptr)
    *endptr = *tmp_endptr;
  return ret;
}

extern "C" {
static void dfsan_strtolong_label(const char *nptr, const char *tmp_endptr,
                                  dfsan_label base_label,
                                  dfsan_label *ret_label) {
  if (tmp_endptr > nptr) {
    // If *tmp_endptr is '\0' include its label as well.
    *ret_label = dfsan_union(
        base_label,
        dfsan_read_label(nptr, tmp_endptr - nptr + (*tmp_endptr ? 0 : 1)));
  } else {
    *ret_label = 0;
  }
}

static void dfsan_strtolong_origin(const char *nptr, const char *tmp_endptr,
                                   dfsan_label base_label,
                                   dfsan_label *ret_label,
                                   dfsan_origin base_origin,
                                   dfsan_origin *ret_origin) {
  if (tmp_endptr > nptr) {
    // When multiple inputs are tainted, we propagate one of its origins.
    // Because checking if base_label is tainted does not need additional
    // computation, we prefer to propagating base_origin.
    *ret_origin = base_label
                      ? base_origin
                      : dfsan_read_origin_of_first_taint(
                            nptr, tmp_endptr - nptr + (*tmp_endptr ? 0 : 1));
  }
}

static double dfsan_strtod(const char *nptr, char **endptr, char **tmp_endptr) {
  assert(tmp_endptr);
  double ret = strtod(nptr, tmp_endptr);
  if (endptr)
    *endptr = *tmp_endptr;
  return ret;
}

static void dfsan_strtod_label(const char *nptr, const char *tmp_endptr,
                               dfsan_label *ret_label) {
  if (tmp_endptr > nptr) {
    // If *tmp_endptr is '\0' include its label as well.
    *ret_label = dfsan_read_label(
        nptr,
        tmp_endptr - nptr + (*tmp_endptr ? 0 : 1));
  } else {
    *ret_label = 0;
  }
}

SANITIZER_INTERFACE_ATTRIBUTE
double __dfsw_strtod(const char *nptr, char **endptr, dfsan_label nptr_label,
                     dfsan_label endptr_label, dfsan_label *ret_label) {
  char *tmp_endptr;
  double ret = dfsan_strtod(nptr, endptr, &tmp_endptr);
  dfsan_strtod_label(nptr, tmp_endptr, ret_label);
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
double __dfso_strtod(const char *nptr, char **endptr, dfsan_label nptr_label,
                     dfsan_label endptr_label, dfsan_label *ret_label,
                     dfsan_origin nptr_origin, dfsan_origin endptr_origin,
                     dfsan_origin *ret_origin) {
  char *tmp_endptr;
  double ret = dfsan_strtod(nptr, endptr, &tmp_endptr);
  dfsan_strtod_label(nptr, tmp_endptr, ret_label);
  if (tmp_endptr > nptr) {
    // If *tmp_endptr is '\0' include its label as well.
    *ret_origin = dfsan_read_origin_of_first_taint(
        nptr, tmp_endptr - nptr + (*tmp_endptr ? 0 : 1));
  } else {
    *ret_origin = 0;
  }
  return ret;
}

WRAPPER_ALIAS(__isoc23_strtod, strtod)

#define WRAPPER_STRTO(ret_type, fun)                                     \
  SANITIZER_INTERFACE_ATTRIBUTE ret_type __dfsw_##fun(                   \
      const char *nptr, char **endptr, int base, dfsan_label nptr_label, \
      dfsan_label endptr_label, dfsan_label base_label,                  \
      dfsan_label *ret_label) {                                          \
    char *tmp_endptr;                                                    \
    auto ret = dfsan_strtol_impl(fun, nptr, endptr, base, &tmp_endptr);  \
    dfsan_strtolong_label(nptr, tmp_endptr, base_label, ret_label);      \
    return ret;                                                          \
  }                                                                      \
  SANITIZER_INTERFACE_ATTRIBUTE ret_type __dfso_##fun(                   \
      const char *nptr, char **endptr, int base, dfsan_label nptr_label, \
      dfsan_label endptr_label, dfsan_label base_label,                  \
      dfsan_label *ret_label, dfsan_origin nptr_origin,                  \
      dfsan_origin endptr_origin, dfsan_origin base_origin,              \
      dfsan_origin *ret_origin) {                                        \
    char *tmp_endptr;                                                    \
    auto ret = dfsan_strtol_impl(fun, nptr, endptr, base, &tmp_endptr);  \
    dfsan_strtolong_label(nptr, tmp_endptr, base_label, ret_label);      \
    dfsan_strtolong_origin(nptr, tmp_endptr, base_label, ret_label,      \
                           base_origin, ret_origin);                     \
    return ret;                                                          \
  }

WRAPPER_STRTO(long, strtol)
WRAPPER_STRTO(long long, strtoll)
WRAPPER_STRTO(unsigned long, strtoul)
WRAPPER_STRTO(unsigned long long, strtoull)
WRAPPER_ALIAS(__isoc23_strtol, strtol)
WRAPPER_ALIAS(__isoc23_strtoll, strtoll)
WRAPPER_ALIAS(__isoc23_strtoul, strtoul)
WRAPPER_ALIAS(__isoc23_strtoull, strtoull)

SANITIZER_INTERFACE_ATTRIBUTE
time_t __dfsw_time(time_t *t, dfsan_label t_label, dfsan_label *ret_label) {
  time_t ret = time(t);
  if (ret != (time_t) -1 && t) {
    dfsan_set_label(0, t, sizeof(time_t));
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
time_t __dfso_time(time_t *t, dfsan_label t_label, dfsan_label *ret_label,
                   dfsan_origin t_origin, dfsan_origin *ret_origin) {
  return __dfsw_time(t, t_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_inet_pton(int af, const char *src, void *dst, dfsan_label af_label,
                     dfsan_label src_label, dfsan_label dst_label,
                     dfsan_label *ret_label) {
  int ret = inet_pton(af, src, dst);
  if (ret == 1) {
    dfsan_set_label(dfsan_read_label(src, strlen(src) + 1), dst,
                    af == AF_INET ? sizeof(struct in_addr) : sizeof(in6_addr));
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_inet_pton(int af, const char *src, void *dst, dfsan_label af_label,
                     dfsan_label src_label, dfsan_label dst_label,
                     dfsan_label *ret_label, dfsan_origin af_origin,
                     dfsan_origin src_origin, dfsan_origin dst_origin,
                     dfsan_origin *ret_origin) {
  int ret = inet_pton(af, src, dst);
  if (ret == 1) {
    int src_len = strlen(src) + 1;
    dfsan_set_label_origin(
        dfsan_read_label(src, src_len),
        dfsan_read_origin_of_first_taint(src, src_len), dst,
        af == AF_INET ? sizeof(struct in_addr) : sizeof(in6_addr));
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
struct tm *__dfsw_localtime_r(const time_t *timep, struct tm *result,
                              dfsan_label timep_label, dfsan_label result_label,
                              dfsan_label *ret_label) {
  struct tm *ret = localtime_r(timep, result);
  if (ret) {
    dfsan_set_label(dfsan_read_label(timep, sizeof(time_t)), result,
                    sizeof(struct tm));
    *ret_label = result_label;
  } else {
    *ret_label = 0;
  }
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
struct tm *__dfso_localtime_r(const time_t *timep, struct tm *result,
                              dfsan_label timep_label, dfsan_label result_label,
                              dfsan_label *ret_label, dfsan_origin timep_origin,
                              dfsan_origin result_origin,
                              dfsan_origin *ret_origin) {
  struct tm *ret = localtime_r(timep, result);
  if (ret) {
    dfsan_set_label_origin(
        dfsan_read_label(timep, sizeof(time_t)),
        dfsan_read_origin_of_first_taint(timep, sizeof(time_t)), result,
        sizeof(struct tm));
    *ret_label = result_label;
    *ret_origin = result_origin;
  } else {
    *ret_label = 0;
  }
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_getpwuid_r(id_t uid, struct passwd *pwd,
                      char *buf, size_t buflen, struct passwd **result,
                      dfsan_label uid_label, dfsan_label pwd_label,
                      dfsan_label buf_label, dfsan_label buflen_label,
                      dfsan_label result_label, dfsan_label *ret_label) {
  // Store the data in pwd, the strings referenced from pwd in buf, and the
  // address of pwd in *result.  On failure, NULL is stored in *result.
  int ret = getpwuid_r(uid, pwd, buf, buflen, result);
  if (ret == 0) {
    dfsan_set_label(0, pwd, sizeof(struct passwd));
    dfsan_set_label(0, buf, strlen(buf) + 1);
  }
  *ret_label = 0;
  dfsan_set_label(0, result, sizeof(struct passwd*));
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_getpwuid_r(id_t uid, struct passwd *pwd, char *buf, size_t buflen,
                      struct passwd **result, dfsan_label uid_label,
                      dfsan_label pwd_label, dfsan_label buf_label,
                      dfsan_label buflen_label, dfsan_label result_label,
                      dfsan_label *ret_label, dfsan_origin uid_origin,
                      dfsan_origin pwd_origin, dfsan_origin buf_origin,
                      dfsan_origin buflen_origin, dfsan_origin result_origin,
                      dfsan_origin *ret_origin) {
  return __dfsw_getpwuid_r(uid, pwd, buf, buflen, result, uid_label, pwd_label,
                           buf_label, buflen_label, result_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_epoll_wait(int epfd, struct epoll_event *events, int maxevents,
                      int timeout, dfsan_label epfd_label,
                      dfsan_label events_label, dfsan_label maxevents_label,
                      dfsan_label timeout_label, dfsan_label *ret_label) {
  int ret = epoll_wait(epfd, events, maxevents, timeout);
  if (ret > 0)
    dfsan_set_label(0, events, ret * sizeof(*events));
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_epoll_wait(int epfd, struct epoll_event *events, int maxevents,
                      int timeout, dfsan_label epfd_label,
                      dfsan_label events_label, dfsan_label maxevents_label,
                      dfsan_label timeout_label, dfsan_label *ret_label,
                      dfsan_origin epfd_origin, dfsan_origin events_origin,
                      dfsan_origin maxevents_origin,
                      dfsan_origin timeout_origin, dfsan_origin *ret_origin) {
  return __dfsw_epoll_wait(epfd, events, maxevents, timeout, epfd_label,
                           events_label, maxevents_label, timeout_label,
                           ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_poll(struct pollfd *fds, nfds_t nfds, int timeout,
                dfsan_label dfs_label, dfsan_label nfds_label,
                dfsan_label timeout_label, dfsan_label *ret_label) {
  int ret = poll(fds, nfds, timeout);
  if (ret >= 0) {
    for (; nfds > 0; --nfds) {
      dfsan_set_label(0, &fds[nfds - 1].revents, sizeof(fds[nfds - 1].revents));
    }
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_poll(struct pollfd *fds, nfds_t nfds, int timeout,
                dfsan_label dfs_label, dfsan_label nfds_label,
                dfsan_label timeout_label, dfsan_label *ret_label,
                dfsan_origin dfs_origin, dfsan_origin nfds_origin,
                dfsan_origin timeout_origin, dfsan_origin *ret_origin) {
  return __dfsw_poll(fds, nfds, timeout, dfs_label, nfds_label, timeout_label,
                     ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_select(int nfds, fd_set *readfds, fd_set *writefds,
                  fd_set *exceptfds, struct timeval *timeout,
                  dfsan_label nfds_label, dfsan_label readfds_label,
                  dfsan_label writefds_label, dfsan_label exceptfds_label,
                  dfsan_label timeout_label, dfsan_label *ret_label) {
  int ret = select(nfds, readfds, writefds, exceptfds, timeout);
  // Clear everything (also on error) since their content is either set or
  // undefined.
  if (readfds) {
    dfsan_set_label(0, readfds, sizeof(fd_set));
  }
  if (writefds) {
    dfsan_set_label(0, writefds, sizeof(fd_set));
  }
  if (exceptfds) {
    dfsan_set_label(0, exceptfds, sizeof(fd_set));
  }
  dfsan_set_label(0, timeout, sizeof(struct timeval));
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_select(int nfds, fd_set *readfds, fd_set *writefds,
                  fd_set *exceptfds, struct timeval *timeout,
                  dfsan_label nfds_label, dfsan_label readfds_label,
                  dfsan_label writefds_label, dfsan_label exceptfds_label,
                  dfsan_label timeout_label, dfsan_label *ret_label,
                  dfsan_origin nfds_origin, dfsan_origin readfds_origin,
                  dfsan_origin writefds_origin, dfsan_origin exceptfds_origin,
                  dfsan_origin timeout_origin, dfsan_origin *ret_origin) {
  return __dfsw_select(nfds, readfds, writefds, exceptfds, timeout, nfds_label,
                       readfds_label, writefds_label, exceptfds_label,
                       timeout_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask,
                             dfsan_label pid_label,
                             dfsan_label cpusetsize_label,
                             dfsan_label mask_label, dfsan_label *ret_label) {
  int ret = sched_getaffinity(pid, cpusetsize, mask);
  if (ret == 0) {
    dfsan_set_label(0, mask, cpusetsize);
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask,
                             dfsan_label pid_label,
                             dfsan_label cpusetsize_label,
                             dfsan_label mask_label, dfsan_label *ret_label,
                             dfsan_origin pid_origin,
                             dfsan_origin cpusetsize_origin,
                             dfsan_origin mask_origin,
                             dfsan_origin *ret_origin) {
  return __dfsw_sched_getaffinity(pid, cpusetsize, mask, pid_label,
                                  cpusetsize_label, mask_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_sigemptyset(sigset_t *set, dfsan_label set_label,
                       dfsan_label *ret_label) {
  int ret = sigemptyset(set);
  dfsan_set_label(0, set, sizeof(sigset_t));
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_sigemptyset(sigset_t *set, dfsan_label set_label,
                       dfsan_label *ret_label, dfsan_origin set_origin,
                       dfsan_origin *ret_origin) {
  return __dfsw_sigemptyset(set, set_label, ret_label);
}

class SignalHandlerScope {
 public:
  SignalHandlerScope() {
    if (DFsanThread *t = GetCurrentThread())
      t->EnterSignalHandler();
  }
  ~SignalHandlerScope() {
    if (DFsanThread *t = GetCurrentThread())
      t->LeaveSignalHandler();
  }
};

// Clear DFSan runtime TLS state at the end of a scope.
//
// Implementation must be async-signal-safe and use small data size, because
// instances of this class may live on the signal handler stack.
//
// DFSan uses TLS to pass metadata of arguments and return values. When an
// instrumented function accesses the TLS, if a signal callback happens, and the
// callback calls other instrumented functions with updating the same TLS, the
// TLS is in an inconsistent state after the callback ends. This may cause
// either under-tainting or over-tainting.
//
// The current implementation simply resets TLS at restore. This prevents from
// over-tainting. Although under-tainting may still happen, a taint flow can be
// found eventually if we run a DFSan-instrumented program multiple times. The
// alternative option is saving the entire TLS. However the TLS storage takes
// 2k bytes, and signal calls could be nested. So it does not seem worth.
class ScopedClearThreadLocalState {
 public:
  ScopedClearThreadLocalState() {}
  ~ScopedClearThreadLocalState() { dfsan_clear_thread_local_state(); }
};

// SignalSpinLocker::sigactions_mu guarantees atomicity of sigaction() calls.
const int kMaxSignals = 1024;
static atomic_uintptr_t sigactions[kMaxSignals];

static void SignalHandler(int signo) {
  SignalHandlerScope signal_handler_scope;
  ScopedClearThreadLocalState scoped_clear_tls;

  // Clear shadows for all inputs provided by system.
  dfsan_clear_arg_tls(0, sizeof(dfsan_label));

  typedef void (*signal_cb)(int x);
  signal_cb cb =
      (signal_cb)atomic_load(&sigactions[signo], memory_order_relaxed);
  cb(signo);
}

static void SignalAction(int signo, siginfo_t *si, void *uc) {
  SignalHandlerScope signal_handler_scope;
  ScopedClearThreadLocalState scoped_clear_tls;

  // Clear shadows for all inputs provided by system. Similar to SignalHandler.
  dfsan_clear_arg_tls(0, 3 * sizeof(dfsan_label));
  dfsan_set_label(0, si, sizeof(*si));
  dfsan_set_label(0, uc, sizeof(ucontext_t));

  typedef void (*sigaction_cb)(int, siginfo_t *, void *);
  sigaction_cb cb =
      (sigaction_cb)atomic_load(&sigactions[signo], memory_order_relaxed);
  cb(signo, si, uc);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_sigaction(int signum, const struct sigaction *act,
                     struct sigaction *oldact, dfsan_label signum_label,
                     dfsan_label act_label, dfsan_label oldact_label,
                     dfsan_label *ret_label) {
  CHECK_LT(signum, kMaxSignals);
  SignalSpinLocker lock;
  uptr old_cb = atomic_load(&sigactions[signum], memory_order_relaxed);
  struct sigaction new_act;
  struct sigaction *pnew_act = act ? &new_act : nullptr;
  if (act) {
    internal_memcpy(pnew_act, act, sizeof(struct sigaction));
    if (pnew_act->sa_flags & SA_SIGINFO) {
      uptr cb = (uptr)(pnew_act->sa_sigaction);
      if (cb != (uptr)SIG_IGN && cb != (uptr)SIG_DFL) {
        atomic_store(&sigactions[signum], cb, memory_order_relaxed);
        pnew_act->sa_sigaction = SignalAction;
      }
    } else {
      uptr cb = (uptr)(pnew_act->sa_handler);
      if (cb != (uptr)SIG_IGN && cb != (uptr)SIG_DFL) {
        atomic_store(&sigactions[signum], cb, memory_order_relaxed);
        pnew_act->sa_handler = SignalHandler;
      }
    }
  }

  int ret = sigaction(signum, pnew_act, oldact);

  if (ret == 0 && oldact) {
    if (oldact->sa_flags & SA_SIGINFO) {
      if (oldact->sa_sigaction == SignalAction)
        oldact->sa_sigaction = (decltype(oldact->sa_sigaction))old_cb;
    } else {
      if (oldact->sa_handler == SignalHandler)
        oldact->sa_handler = (decltype(oldact->sa_handler))old_cb;
    }
  }

  if (oldact) {
    dfsan_set_label(0, oldact, sizeof(struct sigaction));
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_sigaction(int signum, const struct sigaction *act,
                     struct sigaction *oldact, dfsan_label signum_label,
                     dfsan_label act_label, dfsan_label oldact_label,
                     dfsan_label *ret_label, dfsan_origin signum_origin,
                     dfsan_origin act_origin, dfsan_origin oldact_origin,
                     dfsan_origin *ret_origin) {
  return __dfsw_sigaction(signum, act, oldact, signum_label, act_label,
                          oldact_label, ret_label);
}

static sighandler_t dfsan_signal(int signum, sighandler_t handler,
                                 dfsan_label *ret_label) {
  CHECK_LT(signum, kMaxSignals);
  SignalSpinLocker lock;
  uptr old_cb = atomic_load(&sigactions[signum], memory_order_relaxed);
  if (handler != SIG_IGN && handler != SIG_DFL) {
    atomic_store(&sigactions[signum], (uptr)handler, memory_order_relaxed);
    handler = &SignalHandler;
  }

  sighandler_t ret = signal(signum, handler);

  if (ret == SignalHandler)
    ret = (sighandler_t)old_cb;

  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
sighandler_t __dfsw_signal(int signum, sighandler_t handler,
                           dfsan_label signum_label, dfsan_label handler_label,
                           dfsan_label *ret_label) {
  return dfsan_signal(signum, handler, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
sighandler_t __dfso_signal(int signum, sighandler_t handler,
                           dfsan_label signum_label, dfsan_label handler_label,
                           dfsan_label *ret_label, dfsan_origin signum_origin,
                           dfsan_origin handler_origin,
                           dfsan_origin *ret_origin) {
  return dfsan_signal(signum, handler, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_sigaltstack(const stack_t *ss, stack_t *old_ss, dfsan_label ss_label,
                       dfsan_label old_ss_label, dfsan_label *ret_label) {
  int ret = sigaltstack(ss, old_ss);
  if (ret != -1 && old_ss)
    dfsan_set_label(0, old_ss, sizeof(*old_ss));
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_sigaltstack(const stack_t *ss, stack_t *old_ss, dfsan_label ss_label,
                       dfsan_label old_ss_label, dfsan_label *ret_label,
                       dfsan_origin ss_origin, dfsan_origin old_ss_origin,
                       dfsan_origin *ret_origin) {
  return __dfsw_sigaltstack(ss, old_ss, ss_label, old_ss_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_gettimeofday(struct timeval *tv, struct timezone *tz,
                        dfsan_label tv_label, dfsan_label tz_label,
                        dfsan_label *ret_label) {
  int ret = gettimeofday(tv, tz);
  if (tv) {
    dfsan_set_label(0, tv, sizeof(struct timeval));
  }
  if (tz) {
    dfsan_set_label(0, tz, sizeof(struct timezone));
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_gettimeofday(struct timeval *tv, struct timezone *tz,
                        dfsan_label tv_label, dfsan_label tz_label,
                        dfsan_label *ret_label, dfsan_origin tv_origin,
                        dfsan_origin tz_origin, dfsan_origin *ret_origin) {
  return __dfsw_gettimeofday(tv, tz, tv_label, tz_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE void *__dfsw_memchr(void *s, int c, size_t n,
                                                  dfsan_label s_label,
                                                  dfsan_label c_label,
                                                  dfsan_label n_label,
                                                  dfsan_label *ret_label) {
  void *ret = memchr(s, c, n);
  if (flags().strict_data_dependencies) {
    *ret_label = ret ? s_label : 0;
  } else {
    size_t len =
        ret ? reinterpret_cast<char *>(ret) - reinterpret_cast<char *>(s) + 1
            : n;
    *ret_label =
        dfsan_union(dfsan_read_label(s, len), dfsan_union(s_label, c_label));
  }
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE void *__dfso_memchr(
    void *s, int c, size_t n, dfsan_label s_label, dfsan_label c_label,
    dfsan_label n_label, dfsan_label *ret_label, dfsan_origin s_origin,
    dfsan_origin c_origin, dfsan_origin n_origin, dfsan_origin *ret_origin) {
  void *ret = __dfsw_memchr(s, c, n, s_label, c_label, n_label, ret_label);
  if (flags().strict_data_dependencies) {
    if (ret)
      *ret_origin = s_origin;
  } else {
    size_t len =
        ret ? reinterpret_cast<char *>(ret) - reinterpret_cast<char *>(s) + 1
            : n;
    dfsan_origin o = dfsan_read_origin_of_first_taint(s, len);
    *ret_origin = o ? o : (s_label ? s_origin : c_origin);
  }
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfsw_strrchr(char *s, int c,
                                                   dfsan_label s_label,
                                                   dfsan_label c_label,
                                                   dfsan_label *ret_label) {
  char *ret = strrchr(s, c);
  if (flags().strict_data_dependencies) {
    *ret_label = ret ? s_label : 0;
  } else {
    *ret_label =
        dfsan_union(dfsan_read_label(s, strlen(s) + 1),
                    dfsan_union(s_label, c_label));
  }

  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfso_strrchr(
    char *s, int c, dfsan_label s_label, dfsan_label c_label,
    dfsan_label *ret_label, dfsan_origin s_origin, dfsan_origin c_origin,
    dfsan_origin *ret_origin) {
  char *ret = __dfsw_strrchr(s, c, s_label, c_label, ret_label);
  if (flags().strict_data_dependencies) {
    if (ret)
      *ret_origin = s_origin;
  } else {
    size_t s_len = strlen(s) + 1;
    dfsan_origin o = dfsan_read_origin_of_first_taint(s, s_len);
    *ret_origin = o ? o : (s_label ? s_origin : c_origin);
  }

  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfsw_strstr(char *haystack, char *needle,
                                                  dfsan_label haystack_label,
                                                  dfsan_label needle_label,
                                                  dfsan_label *ret_label) {
  char *ret = strstr(haystack, needle);
  if (flags().strict_data_dependencies) {
    *ret_label = ret ? haystack_label : 0;
  } else {
    size_t len = ret ? ret + strlen(needle) - haystack : strlen(haystack) + 1;
    *ret_label =
        dfsan_union(dfsan_read_label(haystack, len),
                    dfsan_union(dfsan_read_label(needle, strlen(needle) + 1),
                                dfsan_union(haystack_label, needle_label)));
  }

  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE char *__dfso_strstr(char *haystack, char *needle,
                                                  dfsan_label haystack_label,
                                                  dfsan_label needle_label,
                                                  dfsan_label *ret_label,
                                                  dfsan_origin haystack_origin,
                                                  dfsan_origin needle_origin,
                                                  dfsan_origin *ret_origin) {
  char *ret =
      __dfsw_strstr(haystack, needle, haystack_label, needle_label, ret_label);
  if (flags().strict_data_dependencies) {
    if (ret)
      *ret_origin = haystack_origin;
  } else {
    size_t needle_len = strlen(needle);
    size_t len = ret ? ret + needle_len - haystack : strlen(haystack) + 1;
    dfsan_origin o = dfsan_read_origin_of_first_taint(haystack, len);
    if (o) {
      *ret_origin = o;
    } else {
      o = dfsan_read_origin_of_first_taint(needle, needle_len + 1);
      *ret_origin = o ? o : (haystack_label ? haystack_origin : needle_origin);
    }
  }

  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_nanosleep(const struct timespec *req,
                                                   struct timespec *rem,
                                                   dfsan_label req_label,
                                                   dfsan_label rem_label,
                                                   dfsan_label *ret_label) {
  int ret = nanosleep(req, rem);
  *ret_label = 0;
  if (ret == -1) {
    // Interrupted by a signal, rem is filled with the remaining time.
    dfsan_set_label(0, rem, sizeof(struct timespec));
  }
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_nanosleep(
    const struct timespec *req, struct timespec *rem, dfsan_label req_label,
    dfsan_label rem_label, dfsan_label *ret_label, dfsan_origin req_origin,
    dfsan_origin rem_origin, dfsan_origin *ret_origin) {
  return __dfsw_nanosleep(req, rem, req_label, rem_label, ret_label);
}

static void clear_msghdr_labels(size_t bytes_written, struct msghdr *msg,
                                int flags) {
  dfsan_set_label(0, msg, sizeof(*msg));
  dfsan_set_label(0, msg->msg_name, msg->msg_namelen);
  dfsan_set_label(0, msg->msg_control, msg->msg_controllen);
  for (size_t i = 0; i < msg->msg_iovlen; ++i) {
    struct iovec *iov = &msg->msg_iov[i];
    size_t iov_written = iov->iov_len;

    // When MSG_TRUNC is not set, we want to avoid setting 0 label on bytes that
    // may not have changed, using bytes_written to bound the 0 label write.
    // When MSG_TRUNC flag is set, bytes_written may be larger than the buffer,
    // and should not be used as a bound.
    if (!(MSG_TRUNC & flags)) {
      if (bytes_written < iov->iov_len) {
        iov_written = bytes_written;
      }
      bytes_written -= iov_written;
    }

    dfsan_set_label(0, iov->iov_base, iov_written);
  }
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_recvmmsg(
    int sockfd, struct mmsghdr *msgvec, unsigned int vlen, int flags,
    struct timespec *timeout, dfsan_label sockfd_label,
    dfsan_label msgvec_label, dfsan_label vlen_label, dfsan_label flags_label,
    dfsan_label timeout_label, dfsan_label *ret_label) {
  int ret = recvmmsg(sockfd, msgvec, vlen, flags, timeout);
  for (int i = 0; i < ret; ++i) {
    dfsan_set_label(0, &msgvec[i].msg_len, sizeof(msgvec[i].msg_len));
    clear_msghdr_labels(msgvec[i].msg_len, &msgvec[i].msg_hdr, flags);
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_recvmmsg(
    int sockfd, struct mmsghdr *msgvec, unsigned int vlen, int flags,
    struct timespec *timeout, dfsan_label sockfd_label,
    dfsan_label msgvec_label, dfsan_label vlen_label, dfsan_label flags_label,
    dfsan_label timeout_label, dfsan_label *ret_label,
    dfsan_origin sockfd_origin, dfsan_origin msgvec_origin,
    dfsan_origin vlen_origin, dfsan_origin flags_origin,
    dfsan_origin timeout_origin, dfsan_origin *ret_origin) {
  return __dfsw_recvmmsg(sockfd, msgvec, vlen, flags, timeout, sockfd_label,
                         msgvec_label, vlen_label, flags_label, timeout_label,
                         ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE ssize_t __dfsw_recvmsg(
    int sockfd, struct msghdr *msg, int flags, dfsan_label sockfd_label,
    dfsan_label msg_label, dfsan_label flags_label, dfsan_label *ret_label) {
  ssize_t ret = recvmsg(sockfd, msg, flags);
  if (ret >= 0)
    clear_msghdr_labels(ret, msg, flags);
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE ssize_t __dfso_recvmsg(
    int sockfd, struct msghdr *msg, int flags, dfsan_label sockfd_label,
    dfsan_label msg_label, dfsan_label flags_label, dfsan_label *ret_label,
    dfsan_origin sockfd_origin, dfsan_origin msg_origin,
    dfsan_origin flags_origin, dfsan_origin *ret_origin) {
  return __dfsw_recvmsg(sockfd, msg, flags, sockfd_label, msg_label,
                        flags_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE int
__dfsw_socketpair(int domain, int type, int protocol, int sv[2],
                  dfsan_label domain_label, dfsan_label type_label,
                  dfsan_label protocol_label, dfsan_label sv_label,
                  dfsan_label *ret_label) {
  int ret = socketpair(domain, type, protocol, sv);
  *ret_label = 0;
  if (ret == 0) {
    dfsan_set_label(0, sv, sizeof(*sv) * 2);
  }
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_socketpair(
    int domain, int type, int protocol, int sv[2], dfsan_label domain_label,
    dfsan_label type_label, dfsan_label protocol_label, dfsan_label sv_label,
    dfsan_label *ret_label, dfsan_origin domain_origin,
    dfsan_origin type_origin, dfsan_origin protocol_origin,
    dfsan_origin sv_origin, dfsan_origin *ret_origin) {
  return __dfsw_socketpair(domain, type, protocol, sv, domain_label, type_label,
                           protocol_label, sv_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_getsockopt(
    int sockfd, int level, int optname, void *optval, socklen_t *optlen,
    dfsan_label sockfd_label, dfsan_label level_label,
    dfsan_label optname_label, dfsan_label optval_label,
    dfsan_label optlen_label, dfsan_label *ret_label) {
  int ret = getsockopt(sockfd, level, optname, optval, optlen);
  if (ret != -1 && optval && optlen) {
    dfsan_set_label(0, optlen, sizeof(*optlen));
    dfsan_set_label(0, optval, *optlen);
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_getsockopt(
    int sockfd, int level, int optname, void *optval, socklen_t *optlen,
    dfsan_label sockfd_label, dfsan_label level_label,
    dfsan_label optname_label, dfsan_label optval_label,
    dfsan_label optlen_label, dfsan_label *ret_label,
    dfsan_origin sockfd_origin, dfsan_origin level_origin,
    dfsan_origin optname_origin, dfsan_origin optval_origin,
    dfsan_origin optlen_origin, dfsan_origin *ret_origin) {
  return __dfsw_getsockopt(sockfd, level, optname, optval, optlen, sockfd_label,
                           level_label, optname_label, optval_label,
                           optlen_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_getsockname(
    int sockfd, struct sockaddr *addr, socklen_t *addrlen,
    dfsan_label sockfd_label, dfsan_label addr_label, dfsan_label addrlen_label,
    dfsan_label *ret_label) {
  socklen_t origlen = addrlen ? *addrlen : 0;
  int ret = getsockname(sockfd, addr, addrlen);
  if (ret != -1 && addr && addrlen) {
    socklen_t written_bytes = origlen < *addrlen ? origlen : *addrlen;
    dfsan_set_label(0, addrlen, sizeof(*addrlen));
    dfsan_set_label(0, addr, written_bytes);
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_getsockname(
    int sockfd, struct sockaddr *addr, socklen_t *addrlen,
    dfsan_label sockfd_label, dfsan_label addr_label, dfsan_label addrlen_label,
    dfsan_label *ret_label, dfsan_origin sockfd_origin,
    dfsan_origin addr_origin, dfsan_origin addrlen_origin,
    dfsan_origin *ret_origin) {
  return __dfsw_getsockname(sockfd, addr, addrlen, sockfd_label, addr_label,
                            addrlen_label, ret_label);
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfsw_getpeername(
    int sockfd, struct sockaddr *addr, socklen_t *addrlen,
    dfsan_label sockfd_label, dfsan_label addr_label, dfsan_label addrlen_label,
    dfsan_label *ret_label) {
  socklen_t origlen = addrlen ? *addrlen : 0;
  int ret = getpeername(sockfd, addr, addrlen);
  if (ret != -1 && addr && addrlen) {
    socklen_t written_bytes = origlen < *addrlen ? origlen : *addrlen;
    dfsan_set_label(0, addrlen, sizeof(*addrlen));
    dfsan_set_label(0, addr, written_bytes);
  }
  *ret_label = 0;
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_getpeername(
    int sockfd, struct sockaddr *addr, socklen_t *addrlen,
    dfsan_label sockfd_label, dfsan_label addr_label, dfsan_label addrlen_label,
    dfsan_label *ret_label, dfsan_origin sockfd_origin,
    dfsan_origin addr_origin, dfsan_origin addrlen_origin,
    dfsan_origin *ret_origin) {
  return __dfsw_getpeername(sockfd, addr, addrlen, sockfd_label, addr_label,
                            addrlen_label, ret_label);
}

// Type of the function passed to dfsan_set_write_callback.
typedef void (*write_dfsan_callback_t)(int fd, const void *buf, ssize_t count);

// Calls to dfsan_set_write_callback() set the values in this struct.
// Calls to the custom version of write() read (and invoke) them.
static struct {
  write_dfsan_callback_t write_callback = nullptr;
} write_callback_info;

SANITIZER_INTERFACE_ATTRIBUTE void __dfsw_dfsan_set_write_callback(
    write_dfsan_callback_t write_callback, dfsan_label write_callback_label,
    dfsan_label *ret_label) {
  write_callback_info.write_callback = write_callback;
}

SANITIZER_INTERFACE_ATTRIBUTE void __dfso_dfsan_set_write_callback(
    write_dfsan_callback_t write_callback, dfsan_label write_callback_label,
    dfsan_label *ret_label, dfsan_origin write_callback_origin,
    dfsan_origin *ret_origin) {
  write_callback_info.write_callback = write_callback;
}

static inline void setup_tls_args_for_write_callback(
    dfsan_label fd_label, dfsan_label buf_label, dfsan_label count_label,
    bool origins, dfsan_origin fd_origin, dfsan_origin buf_origin,
    dfsan_origin count_origin) {
  // The callback code will expect argument shadow labels in the args TLS,
  // and origin labels in the origin args TLS.
  // Previously this was done by a trampoline, but we want to remove this:
  // https://github.com/llvm/llvm-project/issues/54172
  //
  // Instead, this code is manually setting up the args TLS data.
  //
  // The offsets used need to correspond with the instrumentation code,
  // see llvm/lib/Transforms/Instrumentation/DataFlowSanitizer.cpp
  // DFSanFunction::getShadowForTLSArgument.
  // https://github.com/llvm/llvm-project/blob/0acc9e4b5edd8b39ff3d4c6d0e17f02007671c4e/llvm/lib/Transforms/Instrumentation/DataFlowSanitizer.cpp#L1684
  // https://github.com/llvm/llvm-project/blob/0acc9e4b5edd8b39ff3d4c6d0e17f02007671c4e/llvm/lib/Transforms/Instrumentation/DataFlowSanitizer.cpp#L125
  //
  // Here the arguments are all primitives, but it can be more complex
  // to compute offsets for array/aggregate type arguments.
  //
  // TODO(browneee): Consider a builtin to improve maintainabliity.
  // With a builtin, we would provide the argument labels via builtin,
  // and the builtin would reuse parts of the instrumentation code to ensure
  // that this code and the instrumentation can never be out of sync.
  // Note: Currently DFSan instrumentation does not run on this code, so
  // the builtin may need to be handled outside DFSan instrumentation.
  dfsan_set_arg_tls(0, fd_label);
  dfsan_set_arg_tls(1, buf_label);
  dfsan_set_arg_tls(2, count_label);
  if (origins) {
    dfsan_set_arg_origin_tls(0, fd_origin);
    dfsan_set_arg_origin_tls(1, buf_origin);
    dfsan_set_arg_origin_tls(2, count_origin);
  }
}

SANITIZER_INTERFACE_ATTRIBUTE int
__dfsw_write(int fd, const void *buf, size_t count,
             dfsan_label fd_label, dfsan_label buf_label,
             dfsan_label count_label, dfsan_label *ret_label) {
  if (write_callback_info.write_callback) {
    setup_tls_args_for_write_callback(fd_label, buf_label, count_label, false,
                                      0, 0, 0);
    write_callback_info.write_callback(fd, buf, count);
  }

  *ret_label = 0;
  return write(fd, buf, count);
}

SANITIZER_INTERFACE_ATTRIBUTE int __dfso_write(
    int fd, const void *buf, size_t count, dfsan_label fd_label,
    dfsan_label buf_label, dfsan_label count_label, dfsan_label *ret_label,
    dfsan_origin fd_origin, dfsan_origin buf_origin, dfsan_origin count_origin,
    dfsan_origin *ret_origin) {
  if (write_callback_info.write_callback) {
    setup_tls_args_for_write_callback(fd_label, buf_label, count_label, true,
                                      fd_origin, buf_origin, count_origin);
    write_callback_info.write_callback(fd, buf, count);
  }

  *ret_label = 0;
  return write(fd, buf, count);
}
}  // namespace __dfsan

// Type used to extract a dfsan_label with va_arg()
typedef int dfsan_label_va;

// Formats a chunk either a constant string or a single format directive (e.g.,
// '%.3f').
struct Formatter {
  Formatter(char *str_, const char *fmt_, size_t size_)
      : str(str_),
        str_off(0),
        size(size_),
        fmt_start(fmt_),
        fmt_cur(fmt_),
        width(-1),
        num_scanned(-1),
        skip(false) {}

  int format() {
    char *tmp_fmt = build_format_string();
    int retval =
        snprintf(str + str_off, str_off < size ? size - str_off : 0, tmp_fmt,
                 0 /* used only to avoid warnings */);
    free(tmp_fmt);
    return retval;
  }

  template <typename T> int format(T arg) {
    char *tmp_fmt = build_format_string();
    int retval;
    if (width >= 0) {
      retval = snprintf(str + str_off, str_off < size ? size - str_off : 0,
                        tmp_fmt, width, arg);
    } else {
      retval = snprintf(str + str_off, str_off < size ? size - str_off : 0,
                        tmp_fmt, arg);
    }
    free(tmp_fmt);
    return retval;
  }

  char *build_format_string() {
    size_t fmt_size = fmt_cur - fmt_start + 1;
    char *new_fmt = (char *)malloc(fmt_size + 1);
    assert(new_fmt);
    internal_memcpy(new_fmt, fmt_start, fmt_size);
    new_fmt[fmt_size] = '\0';
    return new_fmt;
  }

  char *str_cur() { return str + str_off; }

  size_t num_written_bytes(int retval) {
    if (retval < 0) {
      return 0;
    }

    size_t num_avail = str_off < size ? size - str_off : 0;
    if (num_avail == 0) {
      return 0;
    }

    size_t num_written = retval;
    // A return value of {v,}snprintf of size or more means that the output was
    // truncated.
    if (num_written >= num_avail) {
      num_written -= num_avail;
    }

    return num_written;
  }

  char *str;
  size_t str_off;
  size_t size;
  const char *fmt_start;
  const char *fmt_cur;
  int width;
  int num_scanned;
  bool skip;
};

// Formats the input and propagates the input labels to the output. The output
// is stored in 'str'. 'size' bounds the number of output bytes. 'format' and
// 'ap' are the format string and the list of arguments for formatting. Returns
// the return value vsnprintf would return.
//
// The function tokenizes the format string in chunks representing either a
// constant string or a single format directive (e.g., '%.3f') and formats each
// chunk independently into the output string. This approach allows to figure
// out which bytes of the output string depends on which argument and thus to
// propagate labels more precisely.
//
// WARNING: This implementation does not support conversion specifiers with
// positional arguments.
static int format_buffer(char *str, size_t size, const char *fmt,
                         dfsan_label *va_labels, dfsan_label *ret_label,
                         dfsan_origin *va_origins, dfsan_origin *ret_origin,
                         va_list ap) {
  Formatter formatter(str, fmt, size);

  while (*formatter.fmt_cur) {
    formatter.fmt_start = formatter.fmt_cur;
    formatter.width = -1;
    int retval = 0;

    if (*formatter.fmt_cur != '%') {
      // Ordinary character. Consume all the characters until a '%' or the end
      // of the string.
      for (; *(formatter.fmt_cur + 1) && *(formatter.fmt_cur + 1) != '%';
           ++formatter.fmt_cur) {}
      retval = formatter.format();
      dfsan_set_label(0, formatter.str_cur(),
                      formatter.num_written_bytes(retval));
    } else {
      // Conversion directive. Consume all the characters until a conversion
      // specifier or the end of the string.
      bool end_fmt = false;
      for (; *formatter.fmt_cur && !end_fmt; ) {
        switch (*++formatter.fmt_cur) {
        case 'd':
        case 'i':
        case 'o':
        case 'u':
        case 'x':
        case 'X':
          switch (*(formatter.fmt_cur - 1)) {
          case 'h':
            // Also covers the 'hh' case (since the size of the arg is still
            // an int).
            retval = formatter.format(va_arg(ap, int));
            break;
          case 'l':
            if (formatter.fmt_cur - formatter.fmt_start >= 2 &&
                *(formatter.fmt_cur - 2) == 'l') {
              retval = formatter.format(va_arg(ap, long long int));
            } else {
              retval = formatter.format(va_arg(ap, long int));
            }
            break;
          case 'q':
            retval = formatter.format(va_arg(ap, long long int));
            break;
          case 'j':
            retval = formatter.format(va_arg(ap, intmax_t));
            break;
          case 'z':
          case 't':
            retval = formatter.format(va_arg(ap, size_t));
            break;
          default:
            retval = formatter.format(va_arg(ap, int));
          }
          if (va_origins == nullptr)
            dfsan_set_label(*va_labels++, formatter.str_cur(),
                            formatter.num_written_bytes(retval));
          else
            dfsan_set_label_origin(*va_labels++, *va_origins++,
                                   formatter.str_cur(),
                                   formatter.num_written_bytes(retval));
          end_fmt = true;
          break;

        case 'a':
        case 'A':
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
          if (*(formatter.fmt_cur - 1) == 'L') {
            retval = formatter.format(va_arg(ap, long double));
          } else {
            retval = formatter.format(va_arg(ap, double));
          }
          if (va_origins == nullptr)
            dfsan_set_label(*va_labels++, formatter.str_cur(),
                            formatter.num_written_bytes(retval));
          else
            dfsan_set_label_origin(*va_labels++, *va_origins++,
                                   formatter.str_cur(),
                                   formatter.num_written_bytes(retval));
          end_fmt = true;
          break;

        case 'c':
          retval = formatter.format(va_arg(ap, int));
          if (va_origins == nullptr)
            dfsan_set_label(*va_labels++, formatter.str_cur(),
                            formatter.num_written_bytes(retval));
          else
            dfsan_set_label_origin(*va_labels++, *va_origins++,
                                   formatter.str_cur(),
                                   formatter.num_written_bytes(retval));
          end_fmt = true;
          break;

        case 's': {
          char *arg = va_arg(ap, char *);
          retval = formatter.format(arg);
          if (va_origins) {
            va_origins++;
            dfsan_mem_origin_transfer(formatter.str_cur(), arg,
                                      formatter.num_written_bytes(retval));
          }
          va_labels++;
          dfsan_mem_shadow_transfer(formatter.str_cur(), arg,
                                    formatter.num_written_bytes(retval));
          end_fmt = true;
          break;
        }

        case 'p':
          retval = formatter.format(va_arg(ap, void *));
          if (va_origins == nullptr)
            dfsan_set_label(*va_labels++, formatter.str_cur(),
                            formatter.num_written_bytes(retval));
          else
            dfsan_set_label_origin(*va_labels++, *va_origins++,
                                   formatter.str_cur(),
                                   formatter.num_written_bytes(retval));
          end_fmt = true;
          break;

        case 'n': {
          int *ptr = va_arg(ap, int *);
          *ptr = (int)formatter.str_off;
          va_labels++;
          if (va_origins)
            va_origins++;
          dfsan_set_label(0, ptr, sizeof(ptr));
          end_fmt = true;
          break;
        }

        case '%':
          retval = formatter.format();
          dfsan_set_label(0, formatter.str_cur(),
                          formatter.num_written_bytes(retval));
          end_fmt = true;
          break;

        case '*':
          formatter.width = va_arg(ap, int);
          va_labels++;
          if (va_origins)
            va_origins++;
          break;

        default:
          break;
        }
      }
    }

    if (retval < 0) {
      return retval;
    }

    formatter.fmt_cur++;
    formatter.str_off += retval;
  }

  *ret_label = 0;
  if (ret_origin)
    *ret_origin = 0;

  // Number of bytes written in total.
  return formatter.str_off;
}

// Scans a chunk either a constant string or a single format directive (e.g.,
// '%.3f').
struct Scanner {
  Scanner(char *str_, const char *fmt_, size_t size_)
      : str(str_),
        str_off(0),
        size(size_),
        fmt_start(fmt_),
        fmt_cur(fmt_),
        width(-1),
        num_scanned(0),
        skip(false) {}

  // Consumes a chunk of ordinary characters.
  // Returns number of matching ordinary characters.
  // Returns -1 if the match failed.
  // In format strings, a space will match multiple spaces.
  int check_match_ordinary() {
    char *tmp_fmt = build_format_string_with_n();
    int read_count = -1;
    sscanf(str + str_off, tmp_fmt, &read_count);
    free(tmp_fmt);
    if (read_count > 0) {
      str_off += read_count;
    }
    return read_count;
  }

  int scan() {
    char *tmp_fmt = build_format_string_with_n();
    int read_count = 0;
    int retval = sscanf(str + str_off, tmp_fmt, &read_count);
    free(tmp_fmt);
    if (retval > 0) {
      num_scanned += retval;
    }
    return read_count;
  }

  template <typename T>
  int scan(T arg) {
    char *tmp_fmt = build_format_string_with_n();
    int read_count = 0;
    int retval = sscanf(str + str_off, tmp_fmt, arg, &read_count);
    free(tmp_fmt);
    if (retval > 0) {
      num_scanned += retval;
    }
    return read_count;
  }

  // Adds %n onto current format string to measure length.
  char *build_format_string_with_n() {
    size_t fmt_size = fmt_cur - fmt_start + 1;
    // +2 for %n, +1 for \0
    char *new_fmt = (char *)malloc(fmt_size + 2 + 1);
    assert(new_fmt);
    internal_memcpy(new_fmt, fmt_start, fmt_size);
    new_fmt[fmt_size] = '%';
    new_fmt[fmt_size + 1] = 'n';
    new_fmt[fmt_size + 2] = '\0';
    return new_fmt;
  }

  char *str_cur() { return str + str_off; }

  size_t num_written_bytes(int retval) {
    if (retval < 0) {
      return 0;
    }

    size_t num_avail = str_off < size ? size - str_off : 0;
    if (num_avail == 0) {
      return 0;
    }

    size_t num_written = retval;
    // A return value of {v,}snprintf of size or more means that the output was
    // truncated.
    if (num_written >= num_avail) {
      num_written -= num_avail;
    }

    return num_written;
  }

  char *str;
  size_t str_off;
  size_t size;
  const char *fmt_start;
  const char *fmt_cur;
  int width;
  int num_scanned;
  bool skip;
};

// This function is an inverse of format_buffer: we take the input buffer,
// scan it in search for format strings and store the results in the varargs.
// The labels are propagated from the input buffer to the varargs.
static int scan_buffer(char *str, size_t size, const char *fmt,
                       dfsan_label *va_labels, dfsan_label *ret_label,
                       dfsan_origin *str_origin, dfsan_origin *ret_origin,
                       va_list ap) {
  Scanner scanner(str, fmt, size);
  while (*scanner.fmt_cur) {
    scanner.fmt_start = scanner.fmt_cur;
    scanner.width = -1;
    scanner.skip = false;
    int read_count = 0;
    void *dst_ptr = 0;
    size_t write_size = 0;
    if (*scanner.fmt_cur != '%') {
      // Ordinary character and spaces.
      // Consume all the characters until a '%' or the end of the string.
      for (; *(scanner.fmt_cur + 1) && *(scanner.fmt_cur + 1) != '%';
           ++scanner.fmt_cur) {
      }
      if (scanner.check_match_ordinary() < 0) {
        // The ordinary characters did not match.
        break;
      }
    } else {
      // Conversion directive. Consume all the characters until a conversion
      // specifier or the end of the string.
      bool end_fmt = false;
      for (; *scanner.fmt_cur && !end_fmt;) {
        switch (*++scanner.fmt_cur) {
          case 'd':
          case 'i':
          case 'o':
          case 'u':
          case 'x':
          case 'X':
            if (scanner.skip) {
              read_count = scanner.scan();
            } else {
              switch (*(scanner.fmt_cur - 1)) {
                case 'h':
                  // Also covers the 'hh' case (since the size of the arg is
                  // still an int).
                  dst_ptr = va_arg(ap, int *);
                  read_count = scanner.scan((int *)dst_ptr);
                  write_size = sizeof(int);
                  break;
                case 'l':
                  if (scanner.fmt_cur - scanner.fmt_start >= 2 &&
                      *(scanner.fmt_cur - 2) == 'l') {
                    dst_ptr = va_arg(ap, long long int *);
                    read_count = scanner.scan((long long int *)dst_ptr);
                    write_size = sizeof(long long int);
                  } else {
                    dst_ptr = va_arg(ap, long int *);
                    read_count = scanner.scan((long int *)dst_ptr);
                    write_size = sizeof(long int);
                  }
                  break;
                case 'q':
                  dst_ptr = va_arg(ap, long long int *);
                  read_count = scanner.scan((long long int *)dst_ptr);
                  write_size = sizeof(long long int);
                  break;
                case 'j':
                  dst_ptr = va_arg(ap, intmax_t *);
                  read_count = scanner.scan((intmax_t *)dst_ptr);
                  write_size = sizeof(intmax_t);
                  break;
                case 'z':
                case 't':
                  dst_ptr = va_arg(ap, size_t *);
                  read_count = scanner.scan((size_t *)dst_ptr);
                  write_size = sizeof(size_t);
                  break;
                default:
                  dst_ptr = va_arg(ap, int *);
                  read_count = scanner.scan((int *)dst_ptr);
                  write_size = sizeof(int);
              }
              // get the label associated with the string at the corresponding
              // place
              dfsan_label l = dfsan_read_label(
                  scanner.str_cur(), scanner.num_written_bytes(read_count));
              dfsan_set_label(l, dst_ptr, write_size);
              if (str_origin != nullptr) {
                dfsan_set_label(l, dst_ptr, write_size);
                size_t scan_count = scanner.num_written_bytes(read_count);
                size_t size = scan_count > write_size ? write_size : scan_count;
                dfsan_mem_origin_transfer(dst_ptr, scanner.str_cur(), size);
              }
            }
            end_fmt = true;

            break;

          case 'a':
          case 'A':
          case 'e':
          case 'E':
          case 'f':
          case 'F':
          case 'g':
          case 'G':
            if (scanner.skip) {
              read_count = scanner.scan();
            } else {
              if (*(scanner.fmt_cur - 1) == 'L') {
                dst_ptr = va_arg(ap, long double *);
                read_count = scanner.scan((long double *)dst_ptr);
                write_size = sizeof(long double);
              } else if (*(scanner.fmt_cur - 1) == 'l') {
                dst_ptr = va_arg(ap, double *);
                read_count = scanner.scan((double *)dst_ptr);
                write_size = sizeof(double);
              } else {
                dst_ptr = va_arg(ap, float *);
                read_count = scanner.scan((float *)dst_ptr);
                write_size = sizeof(float);
              }
              dfsan_label l = dfsan_read_label(
                  scanner.str_cur(), scanner.num_written_bytes(read_count));
              dfsan_set_label(l, dst_ptr, write_size);
              if (str_origin != nullptr) {
                dfsan_set_label(l, dst_ptr, write_size);
                size_t scan_count = scanner.num_written_bytes(read_count);
                size_t size = scan_count > write_size ? write_size : scan_count;
                dfsan_mem_origin_transfer(dst_ptr, scanner.str_cur(), size);
              }
            }
            end_fmt = true;
            break;

          case 'c':
            if (scanner.skip) {
              read_count = scanner.scan();
            } else {
              dst_ptr = va_arg(ap, char *);
              read_count = scanner.scan((char *)dst_ptr);
              write_size = sizeof(char);
              dfsan_label l = dfsan_read_label(
                  scanner.str_cur(), scanner.num_written_bytes(read_count));
              dfsan_set_label(l, dst_ptr, write_size);
              if (str_origin != nullptr) {
                size_t scan_count = scanner.num_written_bytes(read_count);
                size_t size = scan_count > write_size ? write_size : scan_count;
                dfsan_mem_origin_transfer(dst_ptr, scanner.str_cur(), size);
              }
            }
            end_fmt = true;
            break;

          case 's': {
            if (scanner.skip) {
              read_count = scanner.scan();
            } else {
              dst_ptr = va_arg(ap, char *);
              read_count = scanner.scan((char *)dst_ptr);
              if (1 == read_count) {
                // special case: we have parsed a single string and we need to
                // update read_count with the string size
                read_count = strlen((char *)dst_ptr);
              }
              if (str_origin)
                dfsan_mem_origin_transfer(
                    dst_ptr, scanner.str_cur(),
                    scanner.num_written_bytes(read_count));
              va_labels++;
              dfsan_mem_shadow_transfer(dst_ptr, scanner.str_cur(),
                                        scanner.num_written_bytes(read_count));
            }
            end_fmt = true;
            break;
          }

          case 'p':
            if (scanner.skip) {
              read_count = scanner.scan();
            } else {
              dst_ptr = va_arg(ap, void *);
              read_count =
                  scanner.scan((int *)dst_ptr);  // note: changing void* to int*
                                                 // since we need to call sizeof
              write_size = sizeof(int);

              dfsan_label l = dfsan_read_label(
                  scanner.str_cur(), scanner.num_written_bytes(read_count));
              dfsan_set_label(l, dst_ptr, write_size);
              if (str_origin != nullptr) {
                dfsan_set_label(l, dst_ptr, write_size);
                size_t scan_count = scanner.num_written_bytes(read_count);
                size_t size = scan_count > write_size ? write_size : scan_count;
                dfsan_mem_origin_transfer(dst_ptr, scanner.str_cur(), size);
              }
            }
            end_fmt = true;
            break;

          case 'n': {
            if (!scanner.skip) {
              int *ptr = va_arg(ap, int *);
              *ptr = (int)scanner.str_off;
              *va_labels++ = 0;
              dfsan_set_label(0, ptr, sizeof(*ptr));
              if (str_origin != nullptr)
                *str_origin++ = 0;
            }
            end_fmt = true;
            break;
          }

          case '%':
            read_count = scanner.scan();
            end_fmt = true;
            break;

          case '*':
            scanner.skip = true;
            break;

          default:
            break;
        }
      }
    }

    if (read_count < 0) {
      // There was an error.
      return read_count;
    }

    scanner.fmt_cur++;
    scanner.str_off += read_count;
  }

  (void)va_labels; // Silence unused-but-set-parameter warning
  *ret_label = 0;
  if (ret_origin)
    *ret_origin = 0;

  // Number of items scanned in total.
  return scanner.num_scanned;
}

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_sprintf(char *str, const char *format, dfsan_label str_label,
                   dfsan_label format_label, dfsan_label *va_labels,
                   dfsan_label *ret_label, ...) {
  va_list ap;
  va_start(ap, ret_label);

  int ret = format_buffer(str, INT32_MAX, format, va_labels, ret_label, nullptr,
                          nullptr, ap);
  va_end(ap);
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_sprintf(char *str, const char *format, dfsan_label str_label,
                   dfsan_label format_label, dfsan_label *va_labels,
                   dfsan_label *ret_label, dfsan_origin str_origin,
                   dfsan_origin format_origin, dfsan_origin *va_origins,
                   dfsan_origin *ret_origin, ...) {
  va_list ap;
  va_start(ap, ret_origin);
  int ret = format_buffer(str, INT32_MAX, format, va_labels, ret_label,
                          va_origins, ret_origin, ap);
  va_end(ap);
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_snprintf(char *str, size_t size, const char *format,
                    dfsan_label str_label, dfsan_label size_label,
                    dfsan_label format_label, dfsan_label *va_labels,
                    dfsan_label *ret_label, ...) {
  va_list ap;
  va_start(ap, ret_label);
  int ret = format_buffer(str, size, format, va_labels, ret_label, nullptr,
                          nullptr, ap);
  va_end(ap);
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_snprintf(char *str, size_t size, const char *format,
                    dfsan_label str_label, dfsan_label size_label,
                    dfsan_label format_label, dfsan_label *va_labels,
                    dfsan_label *ret_label, dfsan_origin str_origin,
                    dfsan_origin size_origin, dfsan_origin format_origin,
                    dfsan_origin *va_origins, dfsan_origin *ret_origin, ...) {
  va_list ap;
  va_start(ap, ret_origin);
  int ret = format_buffer(str, size, format, va_labels, ret_label, va_origins,
                          ret_origin, ap);
  va_end(ap);
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfsw_sscanf(char *str, const char *format, dfsan_label str_label,
                  dfsan_label format_label, dfsan_label *va_labels,
                  dfsan_label *ret_label, ...) {
  va_list ap;
  va_start(ap, ret_label);
  int ret = scan_buffer(str, ~0ul, format, va_labels, ret_label, nullptr,
                        nullptr, ap);
  va_end(ap);
  return ret;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __dfso_sscanf(char *str, const char *format, dfsan_label str_label,
                  dfsan_label format_label, dfsan_label *va_labels,
                  dfsan_label *ret_label, dfsan_origin str_origin,
                  dfsan_origin format_origin, dfsan_origin *va_origins,
                  dfsan_origin *ret_origin, ...) {
  va_list ap;
  va_start(ap, ret_origin);
  int ret = scan_buffer(str, ~0ul, format, va_labels, ret_label, &str_origin,
                        ret_origin, ap);
  va_end(ap);
  return ret;
}

WRAPPER_ALIAS(__isoc99_sscanf, sscanf)
WRAPPER_ALIAS(__isoc23_sscanf, sscanf)

static void BeforeFork() {
  StackDepotLockBeforeFork();
  ChainedOriginDepotLockBeforeFork();
}

static void AfterFork(bool fork_child) {
  ChainedOriginDepotUnlockAfterFork(fork_child);
  StackDepotUnlockAfterFork(fork_child);
}

SANITIZER_INTERFACE_ATTRIBUTE
pid_t __dfsw_fork(dfsan_label *ret_label) {
  pid_t pid = fork();
  *ret_label = 0;
  return pid;
}

SANITIZER_INTERFACE_ATTRIBUTE
pid_t __dfso_fork(dfsan_label *ret_label, dfsan_origin *ret_origin) {
  BeforeFork();
  pid_t pid = __dfsw_fork(ret_label);
  AfterFork(/* fork_child= */ pid == 0);
  return pid;
}

// Default empty implementations (weak). Users should redefine them.
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_pc_guard, u32 *) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_pc_guard_init, u32 *,
                             u32 *) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_pcs_init, const uptr *beg,
                             const uptr *end) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_pc_indir, void) {}

SANITIZER_INTERFACE_WEAK_DEF(void, __dfsw___sanitizer_cov_trace_cmp, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __dfsw___sanitizer_cov_trace_cmp1, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __dfsw___sanitizer_cov_trace_cmp2, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __dfsw___sanitizer_cov_trace_cmp4, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __dfsw___sanitizer_cov_trace_cmp8, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __dfsw___sanitizer_cov_trace_const_cmp1,
                             void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __dfsw___sanitizer_cov_trace_const_cmp2,
                             void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __dfsw___sanitizer_cov_trace_const_cmp4,
                             void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __dfsw___sanitizer_cov_trace_const_cmp8,
                             void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __dfsw___sanitizer_cov_trace_switch, void) {}
}  // extern "C"
