//===-- sanitizer_procmaps_bsd.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Information about the process mappings
// (FreeBSD and NetBSD-specific parts).
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_FREEBSD || SANITIZER_NETBSD
#include "sanitizer_common.h"
#include "sanitizer_procmaps.h"

// clang-format off
#include <sys/types.h>
#include <sys/sysctl.h>
// clang-format on
#include <unistd.h>
#if SANITIZER_FREEBSD
#include <sys/user.h>
#endif

#include <limits.h>

namespace __sanitizer {

#if SANITIZER_FREEBSD
void GetMemoryProfile(fill_profile_f cb, uptr *stats) {
  const int Mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};

  struct kinfo_proc *InfoProc;
  uptr Len = sizeof(*InfoProc);
  uptr Size = Len;
  InfoProc = (struct kinfo_proc *)MmapOrDie(Size, "GetMemoryProfile()");
  CHECK_EQ(
      internal_sysctl(Mib, ARRAY_SIZE(Mib), nullptr, (uptr *)InfoProc, &Len, 0),
      0);
  cb(0, InfoProc->ki_rssize * GetPageSizeCached(), false, stats);
  UnmapOrDie(InfoProc, Size, true);
}
#elif SANITIZER_NETBSD
void GetMemoryProfile(fill_profile_f cb, uptr *stats) {
  struct kinfo_proc2 *InfoProc;
  uptr Len = sizeof(*InfoProc);
  uptr Size = Len;
  const int Mib[] = {CTL_KERN, KERN_PROC2, KERN_PROC_PID,
                     getpid(), (int)Size,  1};
  InfoProc = (struct kinfo_proc2 *)MmapOrDie(Size, "GetMemoryProfile()");
  CHECK_EQ(
      internal_sysctl(Mib, ARRAY_SIZE(Mib), nullptr, (uptr *)InfoProc, &Len, 0),
      0);
  cb(0, InfoProc->p_vm_rssize * GetPageSizeCached(), false, stats);
  UnmapOrDie(InfoProc, Size, true);
}
#endif

void ReadProcMaps(ProcSelfMapsBuff *proc_maps) {
  const int Mib[] = {
#if SANITIZER_FREEBSD
    CTL_KERN,
    KERN_PROC,
    KERN_PROC_VMMAP,
    getpid()
#elif SANITIZER_NETBSD
    CTL_VM,
    VM_PROC,
    VM_PROC_MAP,
    getpid(),
    sizeof(struct kinfo_vmentry)
#else
#error "not supported"
#endif
  };

  uptr Size = 0;
  int Err = internal_sysctl(Mib, ARRAY_SIZE(Mib), NULL, &Size, NULL, 0);
  CHECK_EQ(Err, 0);
  CHECK_GT(Size, 0);

  size_t MmapedSize = Size * 4 / 3;
  void *VmMap = MmapOrDie(MmapedSize, "ReadProcMaps()");
  Size = MmapedSize;
  Err = internal_sysctl(Mib, ARRAY_SIZE(Mib), VmMap, &Size, NULL, 0);
  CHECK_EQ(Err, 0);
  proc_maps->data = (char *)VmMap;
  proc_maps->mmaped_size = MmapedSize;
  proc_maps->len = Size;
}

bool MemoryMappingLayout::Next(MemoryMappedSegment *segment) {
  CHECK(!Error()); // can not fail
  char *last = data_.proc_self_maps.data + data_.proc_self_maps.len;
  if (data_.current >= last)
    return false;
  const struct kinfo_vmentry *VmEntry =
      (const struct kinfo_vmentry *)data_.current;

  segment->start = (uptr)VmEntry->kve_start;
  segment->end = (uptr)VmEntry->kve_end;
  segment->offset = (uptr)VmEntry->kve_offset;

  segment->protection = 0;
  if ((VmEntry->kve_protection & KVME_PROT_READ) != 0)
    segment->protection |= kProtectionRead;
  if ((VmEntry->kve_protection & KVME_PROT_WRITE) != 0)
    segment->protection |= kProtectionWrite;
  if ((VmEntry->kve_protection & KVME_PROT_EXEC) != 0)
    segment->protection |= kProtectionExecute;

  if (segment->filename != NULL && segment->filename_size > 0) {
    internal_snprintf(segment->filename,
                      Min(segment->filename_size, (uptr)PATH_MAX), "%s",
                      VmEntry->kve_path);
  }

#if SANITIZER_FREEBSD
  data_.current += VmEntry->kve_structsize;
#else
  data_.current += sizeof(*VmEntry);
#endif

  return true;
}

} // namespace __sanitizer

#endif
