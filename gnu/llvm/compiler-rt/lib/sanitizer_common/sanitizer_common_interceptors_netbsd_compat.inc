//===-- sanitizer_common_interceptors_netbsd_compat.inc ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Common function interceptors for tools like AddressSanitizer,
// ThreadSanitizer, MemorySanitizer, etc.
//
// Interceptors for NetBSD old function calls that have been versioned.
//
// NetBSD minimal version supported 9.0.
// NetBSD current version supported 9.99.26.
//
//===----------------------------------------------------------------------===//

#if SANITIZER_NETBSD

// First undef all mangled symbols.
// Next, define compat interceptors.
// Finally, undef INIT_ and redefine it.
// This allows to avoid preprocessor issues.

#undef fstatvfs
#undef fstatvfs1
#undef getmntinfo
#undef getvfsstat
#undef statvfs
#undef statvfs1

INTERCEPTOR(int, statvfs, char *path, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, statvfs, path, buf);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, internal_strlen(path) + 1);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(statvfs)(path, buf);
  if (!res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statvfs90_sz);
  return res;
}

INTERCEPTOR(int, fstatvfs, int fd, void *buf) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fstatvfs, fd, buf);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  // FIXME: under ASan the call below may write to freed memory and corrupt
  // its metadata. See
  // https://github.com/google/sanitizers/issues/321.
  int res = REAL(fstatvfs)(fd, buf);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statvfs90_sz);
    if (fd >= 0)
      COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  }
  return res;
}

#undef INIT_STATVFS
#define INIT_STATVFS \
  COMMON_INTERCEPT_FUNCTION(statvfs); \
  COMMON_INTERCEPT_FUNCTION(fstatvfs); \
  COMMON_INTERCEPT_FUNCTION(__statvfs90); \
  COMMON_INTERCEPT_FUNCTION(__fstatvfs90)

INTERCEPTOR(int, __getmntinfo13, void **mntbufp, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, __getmntinfo13, mntbufp, flags);
  int cnt = REAL(__getmntinfo13)(mntbufp, flags);
  if (cnt > 0 && mntbufp) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, mntbufp, sizeof(void *));
    if (*mntbufp)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, *mntbufp, cnt * struct_statvfs90_sz);
  }
  return cnt;
}

#undef INIT_GETMNTINFO
#define INIT_GETMNTINFO \
  COMMON_INTERCEPT_FUNCTION(__getmntinfo13); \
  COMMON_INTERCEPT_FUNCTION(__getmntinfo90)

INTERCEPTOR(int, getvfsstat, void *buf, SIZE_T bufsize, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getvfsstat, buf, bufsize, flags);
  int ret = REAL(getvfsstat)(buf, bufsize, flags);
  if (buf && ret > 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, ret * struct_statvfs90_sz);
  return ret;
}

#undef INIT_GETVFSSTAT
#define INIT_GETVFSSTAT \
  COMMON_INTERCEPT_FUNCTION(getvfsstat); \
  COMMON_INTERCEPT_FUNCTION(__getvfsstat90)

INTERCEPTOR(int, statvfs1, const char *path, void *buf, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, statvfs1, path, buf, flags);
  if (path) COMMON_INTERCEPTOR_READ_RANGE(ctx, path, internal_strlen(path) + 1);
  int res = REAL(statvfs1)(path, buf, flags);
  if (!res) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statvfs90_sz);
  return res;
}

INTERCEPTOR(int, fstatvfs1, int fd, void *buf, int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, fstatvfs1, fd, buf, flags);
  COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd);
  int res = REAL(fstatvfs1)(fd, buf, flags);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, struct_statvfs90_sz);
    if (fd >= 0)
      COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  }
  return res;
}

#undef INIT_STATVFS1
#define INIT_STATVFS1 \
  COMMON_INTERCEPT_FUNCTION(statvfs1); \
  COMMON_INTERCEPT_FUNCTION(fstatvfs1); \
  COMMON_INTERCEPT_FUNCTION(__statvfs190); \
  COMMON_INTERCEPT_FUNCTION(__fstatvfs190)

#endif
