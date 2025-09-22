//===-- sanitizer_procmaps_solaris.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Information about the process mappings (Solaris-specific parts).
//===----------------------------------------------------------------------===//

// Before Solaris 11.4, <procfs.h> doesn't work in a largefile environment.
#undef _FILE_OFFSET_BITS

// Avoid conflict between `_TIME_BITS` defined vs. `_FILE_OFFSET_BITS`
// undefined in some Linux configurations.
#undef _TIME_BITS
#include "sanitizer_platform.h"
#if SANITIZER_SOLARIS
#  include <fcntl.h>
#  include <limits.h>
#  include <procfs.h>

#  include "sanitizer_common.h"
#  include "sanitizer_procmaps.h"

namespace __sanitizer {

void ReadProcMaps(ProcSelfMapsBuff *proc_maps) {
  uptr fd = internal_open("/proc/self/xmap", O_RDONLY);
  CHECK_NE(fd, -1);
  uptr Size = internal_filesize(fd);
  CHECK_GT(Size, 0);

  // Allow for additional entries by following mmap.
  size_t MmapedSize = Size * 4 / 3;
  void *VmMap = MmapOrDie(MmapedSize, "ReadProcMaps()");
  Size = internal_read(fd, VmMap, MmapedSize);
  CHECK_NE(Size, -1);
  internal_close(fd);
  proc_maps->data = (char *)VmMap;
  proc_maps->mmaped_size = MmapedSize;
  proc_maps->len = Size;
}

bool MemoryMappingLayout::Next(MemoryMappedSegment *segment) {
  if (Error()) return false; // simulate empty maps
  char *last = data_.proc_self_maps.data + data_.proc_self_maps.len;
  if (data_.current >= last) return false;

  prxmap_t *xmapentry =
      const_cast<prxmap_t *>(reinterpret_cast<const prxmap_t *>(data_.current));

  segment->start = (uptr)xmapentry->pr_vaddr;
  segment->end = (uptr)(xmapentry->pr_vaddr + xmapentry->pr_size);
  segment->offset = (uptr)xmapentry->pr_offset;

  segment->protection = 0;
  if ((xmapentry->pr_mflags & MA_READ) != 0)
    segment->protection |= kProtectionRead;
  if ((xmapentry->pr_mflags & MA_WRITE) != 0)
    segment->protection |= kProtectionWrite;
  if ((xmapentry->pr_mflags & MA_EXEC) != 0)
    segment->protection |= kProtectionExecute;
  if ((xmapentry->pr_mflags & MA_SHARED) != 0)
    segment->protection |= kProtectionShared;

  if (segment->filename != NULL && segment->filename_size > 0) {
    char proc_path[PATH_MAX + 1];

    // Avoid unnecessary readlink on unnamed entires.
    if (xmapentry->pr_mapname[0] == '\0')
      segment->filename[0] = '\0';
    else {
      internal_snprintf(proc_path, sizeof(proc_path), "/proc/self/path/%s",
                        xmapentry->pr_mapname);
      ssize_t sz = internal_readlink(proc_path, segment->filename,
                                     segment->filename_size - 1);

      // If readlink failed, the map is anonymous.
      if (sz == -1)
        segment->filename[0] = '\0';
      else if ((size_t)sz < segment->filename_size)
        // readlink doesn't NUL-terminate.
        segment->filename[sz] = '\0';
    }
  }

  data_.current += sizeof(prxmap_t);

  return true;
}

}  // namespace __sanitizer

#endif  // SANITIZER_SOLARIS
