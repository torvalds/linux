//===-- asan_linux.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Linux-specific details.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
    SANITIZER_SOLARIS

#  include <dlfcn.h>
#  include <fcntl.h>
#  include <limits.h>
#  include <pthread.h>
#  include <stdio.h>
#  include <sys/mman.h>
#  include <sys/resource.h>
#  include <sys/syscall.h>
#  include <sys/time.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <unwind.h>

#  include "asan_interceptors.h"
#  include "asan_internal.h"
#  include "asan_premap_shadow.h"
#  include "asan_thread.h"
#  include "sanitizer_common/sanitizer_flags.h"
#  include "sanitizer_common/sanitizer_hash.h"
#  include "sanitizer_common/sanitizer_libc.h"
#  include "sanitizer_common/sanitizer_procmaps.h"

#  if SANITIZER_FREEBSD
#    include <sys/link_elf.h>
#  endif

#  if SANITIZER_SOLARIS
#    include <link.h>
#  endif

#  if SANITIZER_ANDROID || SANITIZER_FREEBSD || SANITIZER_SOLARIS
#    include <ucontext.h>
#  elif SANITIZER_NETBSD
#    include <link_elf.h>
#    include <ucontext.h>
#  else
#    include <link.h>
#    include <sys/ucontext.h>
#  endif

typedef enum {
  ASAN_RT_VERSION_UNDEFINED = 0,
  ASAN_RT_VERSION_DYNAMIC,
  ASAN_RT_VERSION_STATIC,
} asan_rt_version_t;

// FIXME: perhaps also store abi version here?
extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
asan_rt_version_t __asan_rt_version;
}

namespace __asan {

void InitializePlatformInterceptors() {}
void InitializePlatformExceptionHandlers() {}
bool IsSystemHeapAddress(uptr addr) { return false; }

#  if ASAN_PREMAP_SHADOW
uptr FindPremappedShadowStart(uptr shadow_size_bytes) {
  uptr granularity = GetMmapGranularity();
  uptr shadow_start = reinterpret_cast<uptr>(&__asan_shadow);
  uptr premap_shadow_size = PremapShadowSize();
  uptr shadow_size = RoundUpTo(shadow_size_bytes, granularity);
  // We may have mapped too much. Release extra memory.
  UnmapFromTo(shadow_start + shadow_size, shadow_start + premap_shadow_size);
  return shadow_start;
}
#  endif

uptr FindDynamicShadowStart() {
  uptr shadow_size_bytes = MemToShadowSize(kHighMemEnd);
#  if ASAN_PREMAP_SHADOW
  if (!PremapShadowFailed())
    return FindPremappedShadowStart(shadow_size_bytes);
#  endif

  return MapDynamicShadow(shadow_size_bytes, ASAN_SHADOW_SCALE,
                          /*min_shadow_base_alignment*/ 0, kHighMemEnd,
                          GetMmapGranularity());
}

void AsanApplyToGlobals(globals_op_fptr op, const void *needle) {
  UNIMPLEMENTED();
}

void FlushUnneededASanShadowMemory(uptr p, uptr size) {
  // Since asan's mapping is compacting, the shadow chunk may be
  // not page-aligned, so we only flush the page-aligned portion.
  ReleaseMemoryPagesToOS(MemToShadow(p), MemToShadow(p + size));
}

#  if SANITIZER_ANDROID
// FIXME: should we do anything for Android?
void AsanCheckDynamicRTPrereqs() {}
void AsanCheckIncompatibleRT() {}
#  else
static int FindFirstDSOCallback(struct dl_phdr_info *info, size_t size,
                                void *data) {
  VReport(2, "info->dlpi_name = %s\tinfo->dlpi_addr = %p\n", info->dlpi_name,
          (void *)info->dlpi_addr);

  const char **name = (const char **)data;

  // Ignore first entry (the main program)
  if (!*name) {
    *name = "";
    return 0;
  }

#    if SANITIZER_LINUX
  // Ignore vDSO. glibc versions earlier than 2.15 (and some patched
  // by distributors) return an empty name for the vDSO entry, so
  // detect this as well.
  if (!info->dlpi_name[0] ||
      internal_strncmp(info->dlpi_name, "linux-", sizeof("linux-") - 1) == 0)
    return 0;
#    endif
#    if SANITIZER_FREEBSD
  // Ignore vDSO.
  if (internal_strcmp(info->dlpi_name, "[vdso]") == 0)
    return 0;
#    endif

  *name = info->dlpi_name;
  return 1;
}

static bool IsDynamicRTName(const char *libname) {
  return internal_strstr(libname, "libclang_rt.asan") ||
         internal_strstr(libname, "libasan.so");
}

static void ReportIncompatibleRT() {
  Report("Your application is linked against incompatible ASan runtimes.\n");
  Die();
}

void AsanCheckDynamicRTPrereqs() {
  if (!ASAN_DYNAMIC || !flags()->verify_asan_link_order)
    return;

  // Ensure that dynamic RT is the first DSO in the list
  const char *first_dso_name = nullptr;
  dl_iterate_phdr(FindFirstDSOCallback, &first_dso_name);
  if (first_dso_name && first_dso_name[0] && !IsDynamicRTName(first_dso_name)) {
    Report(
        "ASan runtime does not come first in initial library list; "
        "you should either link runtime to your application or "
        "manually preload it with LD_PRELOAD.\n");
    Die();
  }
}

void AsanCheckIncompatibleRT() {
  if (ASAN_DYNAMIC) {
    if (__asan_rt_version == ASAN_RT_VERSION_UNDEFINED) {
      __asan_rt_version = ASAN_RT_VERSION_DYNAMIC;
    } else if (__asan_rt_version != ASAN_RT_VERSION_DYNAMIC) {
      ReportIncompatibleRT();
    }
  } else {
    if (__asan_rt_version == ASAN_RT_VERSION_UNDEFINED) {
      // Ensure that dynamic runtime is not present. We should detect it
      // as early as possible, otherwise ASan interceptors could bind to
      // the functions in dynamic ASan runtime instead of the functions in
      // system libraries, causing crashes later in ASan initialization.
      MemoryMappingLayout proc_maps(/*cache_enabled*/ true);
      char filename[PATH_MAX];
      MemoryMappedSegment segment(filename, sizeof(filename));
      while (proc_maps.Next(&segment)) {
        if (IsDynamicRTName(segment.filename)) {
          Report(
              "Your application is linked against "
              "incompatible ASan runtimes.\n");
          Die();
        }
      }
      __asan_rt_version = ASAN_RT_VERSION_STATIC;
    } else if (__asan_rt_version != ASAN_RT_VERSION_STATIC) {
      ReportIncompatibleRT();
    }
  }
}
#  endif  // SANITIZER_ANDROID

#  if ASAN_INTERCEPT_SWAPCONTEXT
constexpr u32 kAsanContextStackFlagsMagic = 0x51260eea;

static int HashContextStack(const ucontext_t &ucp) {
  MurMur2Hash64Builder hash(kAsanContextStackFlagsMagic);
  hash.add(reinterpret_cast<uptr>(ucp.uc_stack.ss_sp));
  hash.add(ucp.uc_stack.ss_size);
  return static_cast<int>(hash.get());
}

void SignContextStack(void *context) {
  ucontext_t *ucp = reinterpret_cast<ucontext_t *>(context);
  ucp->uc_stack.ss_flags = HashContextStack(*ucp);
}

void ReadContextStack(void *context, uptr *stack, uptr *ssize) {
  const ucontext_t *ucp = reinterpret_cast<const ucontext_t *>(context);
  if (HashContextStack(*ucp) == ucp->uc_stack.ss_flags) {
    *stack = reinterpret_cast<uptr>(ucp->uc_stack.ss_sp);
    *ssize = ucp->uc_stack.ss_size;
    return;
  }
  *stack = 0;
  *ssize = 0;
}
#  endif  // ASAN_INTERCEPT_SWAPCONTEXT

void *AsanDlSymNext(const char *sym) { return dlsym(RTLD_NEXT, sym); }

bool HandleDlopenInit() {
  // Not supported on this platform.
  static_assert(!SANITIZER_SUPPORTS_INIT_FOR_DLOPEN,
                "Expected SANITIZER_SUPPORTS_INIT_FOR_DLOPEN to be false");
  return false;
}

}  // namespace __asan

#endif  // SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD ||
        // SANITIZER_SOLARIS
