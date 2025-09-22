//===-- sanitizer_procmaps_fuchsia.cpp
//----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Information about the process mappings (Fuchsia-specific parts).
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_FUCHSIA
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "sanitizer_common.h"
#include "sanitizer_procmaps.h"

namespace __sanitizer {

// The cache flag is ignored on Fuchsia because a process can always get this
// information via its process-self handle.
MemoryMappingLayout::MemoryMappingLayout(bool) { Reset(); }

void MemoryMappingLayout::Reset() {
  data_.data.clear();
  data_.current = 0;

  size_t count;
  zx_status_t status = _zx_object_get_info(
      _zx_process_self(), ZX_INFO_PROCESS_MAPS, nullptr, 0, nullptr, &count);
  if (status != ZX_OK) {
    return;
  }

  size_t filled;
  do {
    data_.data.resize(count);
    status = _zx_object_get_info(
        _zx_process_self(), ZX_INFO_PROCESS_MAPS, data_.data.data(),
        count * sizeof(zx_info_maps_t), &filled, &count);
    if (status != ZX_OK) {
      data_.data.clear();
      return;
    }
  } while (filled < count);
}

MemoryMappingLayout::~MemoryMappingLayout() {}

bool MemoryMappingLayout::Error() const { return data_.data.empty(); }

bool MemoryMappingLayout::Next(MemoryMappedSegment *segment) {
  while (data_.current < data_.data.size()) {
    const auto &entry = data_.data[data_.current++];
    if (entry.type == ZX_INFO_MAPS_TYPE_MAPPING) {
      segment->start = entry.base;
      segment->end = entry.base + entry.size;
      segment->offset = entry.u.mapping.vmo_offset;
      const auto flags = entry.u.mapping.mmu_flags;
      segment->protection =
          ((flags & ZX_VM_PERM_READ) ? kProtectionRead : 0) |
          ((flags & ZX_VM_PERM_WRITE) ? kProtectionWrite : 0) |
          ((flags & ZX_VM_PERM_EXECUTE) ? kProtectionExecute : 0);
      if (segment->filename && segment->filename_size > 0) {
        uptr len = Min(sizeof(entry.name), segment->filename_size) - 1;
        internal_strncpy(segment->filename, entry.name, len);
        segment->filename[len] = 0;
      }
      return true;
    }
  }
  return false;
}

}  // namespace __sanitizer

#endif  // SANITIZER_FUCHSIA
