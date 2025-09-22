//===-- stats.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Sanitizer statistics gathering. Manages statistics for a process and is
// responsible for writing the report file.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_file.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#if SANITIZER_POSIX
#include "sanitizer_common/sanitizer_posix.h"
#endif
#include "sanitizer_common/sanitizer_symbolizer.h"
#include "stats/stats.h"
#if SANITIZER_POSIX
#include <signal.h>
#endif

using namespace __sanitizer;

namespace {

InternalMmapVectorNoCtor<StatModule **> modules;
StaticSpinMutex modules_mutex;

fd_t stats_fd;

void WriteLE(fd_t fd, uptr val) {
  char chars[sizeof(uptr)];
  for (unsigned i = 0; i != sizeof(uptr); ++i) {
    chars[i] = val >> (i * 8);
  }
  WriteToFile(fd, chars, sizeof(uptr));
}

void OpenStatsFile(const char *path_env) {
  InternalMmapVector<char> path(kMaxPathLength);
  SubstituteForFlagValue(path_env, path.data(), kMaxPathLength);

  error_t err;
  stats_fd = OpenFile(path.data(), WrOnly, &err);
  if (stats_fd == kInvalidFd) {
    Report("stats: failed to open %s for writing (reason: %d)\n", path.data(),
           err);
    return;
  }
  char sizeof_uptr = sizeof(uptr);
  WriteToFile(stats_fd, &sizeof_uptr, 1);
}

void WriteModuleReport(StatModule **smodp) {
  CHECK(smodp);
  const char *path_env = GetEnv("SANITIZER_STATS_PATH");
  if (!path_env || stats_fd == kInvalidFd)
    return;
  if (!stats_fd)
    OpenStatsFile(path_env);
  const LoadedModule *mod = Symbolizer::GetOrInit()->FindModuleForAddress(
      reinterpret_cast<uptr>(smodp));
  WriteToFile(stats_fd, mod->full_name(),
              internal_strlen(mod->full_name()) + 1);
  for (StatModule *smod = *smodp; smod; smod = smod->next) {
    for (u32 i = 0; i != smod->size; ++i) {
      StatInfo *s = &smod->infos[i];
      if (!s->addr)
        continue;
      WriteLE(stats_fd, s->addr - mod->base_address());
      WriteLE(stats_fd, s->data);
    }
  }
  WriteLE(stats_fd, 0);
  WriteLE(stats_fd, 0);
}

} // namespace

extern "C"
SANITIZER_INTERFACE_ATTRIBUTE
unsigned __sanitizer_stats_register(StatModule **mod) {
  SpinMutexLock l(&modules_mutex);
  modules.push_back(mod);
  return modules.size() - 1;
}

extern "C"
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_stats_unregister(unsigned index) {
  SpinMutexLock l(&modules_mutex);
  WriteModuleReport(modules[index]);
  modules[index] = 0;
}

namespace {

void WriteFullReport() {
  SpinMutexLock l(&modules_mutex);
  for (StatModule **mod : modules) {
    if (!mod)
      continue;
    WriteModuleReport(mod);
  }
  if (stats_fd != 0 && stats_fd != kInvalidFd) {
    CloseFile(stats_fd);
    stats_fd = kInvalidFd;
  }
}

#if SANITIZER_POSIX
void USR2Handler(int sig) {
  WriteFullReport();
}
#endif

struct WriteReportOnExitOrSignal {
  WriteReportOnExitOrSignal() {
#if SANITIZER_POSIX
    struct sigaction sigact;
    internal_memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = USR2Handler;
    internal_sigaction(SIGUSR2, &sigact, nullptr);
#endif
  }

  ~WriteReportOnExitOrSignal() {
    WriteFullReport();
  }
} wr;

} // namespace
