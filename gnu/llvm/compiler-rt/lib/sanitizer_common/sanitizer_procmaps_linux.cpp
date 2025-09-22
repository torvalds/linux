//===-- sanitizer_procmaps_linux.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Information about the process mappings (Linux-specific parts).
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_LINUX
#include "sanitizer_common.h"
#include "sanitizer_procmaps.h"

namespace __sanitizer {

void ReadProcMaps(ProcSelfMapsBuff *proc_maps) {
  if (!ReadFileToBuffer("/proc/self/maps", &proc_maps->data,
                        &proc_maps->mmaped_size, &proc_maps->len)) {
    proc_maps->data = nullptr;
    proc_maps->mmaped_size = 0;
    proc_maps->len = 0;
  }
}

static bool IsOneOf(char c, char c1, char c2) {
  return c == c1 || c == c2;
}

bool MemoryMappingLayout::Next(MemoryMappedSegment *segment) {
  if (Error()) return false; // simulate empty maps
  char *last = data_.proc_self_maps.data + data_.proc_self_maps.len;
  if (data_.current >= last) return false;
  char *next_line =
      (char *)internal_memchr(data_.current, '\n', last - data_.current);
  if (next_line == 0)
    next_line = last;
  // Example: 08048000-08056000 r-xp 00000000 03:0c 64593   /foo/bar
  segment->start = ParseHex(&data_.current);
  CHECK_EQ(*data_.current++, '-');
  segment->end = ParseHex(&data_.current);
  CHECK_EQ(*data_.current++, ' ');
  CHECK(IsOneOf(*data_.current, '-', 'r'));
  segment->protection = 0;
  if (*data_.current++ == 'r') segment->protection |= kProtectionRead;
  CHECK(IsOneOf(*data_.current, '-', 'w'));
  if (*data_.current++ == 'w') segment->protection |= kProtectionWrite;
  CHECK(IsOneOf(*data_.current, '-', 'x'));
  if (*data_.current++ == 'x') segment->protection |= kProtectionExecute;
  CHECK(IsOneOf(*data_.current, 's', 'p'));
  if (*data_.current++ == 's') segment->protection |= kProtectionShared;
  CHECK_EQ(*data_.current++, ' ');
  segment->offset = ParseHex(&data_.current);
  CHECK_EQ(*data_.current++, ' ');
  ParseHex(&data_.current);
  CHECK_EQ(*data_.current++, ':');
  ParseHex(&data_.current);
  CHECK_EQ(*data_.current++, ' ');
  while (IsDecimal(*data_.current)) data_.current++;
  // Qemu may lack the trailing space.
  // https://github.com/google/sanitizers/issues/160
  // CHECK_EQ(*data_.current++, ' ');
  // Skip spaces.
  while (data_.current < next_line && *data_.current == ' ') data_.current++;
  // Fill in the filename.
  if (segment->filename) {
    uptr len =
        Min((uptr)(next_line - data_.current), segment->filename_size - 1);
    internal_strncpy(segment->filename, data_.current, len);
    segment->filename[len] = 0;
  }

  data_.current = next_line + 1;
  return true;
}

}  // namespace __sanitizer

#endif  // SANITIZER_LINUX
