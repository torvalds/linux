//===-- sanitizer_procmaps_test.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//
#if !defined(_WIN32)  // There are no /proc/maps on Windows.

#  include "sanitizer_common/sanitizer_procmaps.h"

#  include <stdlib.h>
#  include <string.h>

#  include <vector>

#  include "gtest/gtest.h"

static void noop() {}
extern const char *argv0;

namespace __sanitizer {

#  if SANITIZER_LINUX && !SANITIZER_ANDROID
TEST(MemoryMappingLayout, CodeRange) {
  uptr start, end;
  bool res = GetCodeRangeForFile("[vdso]", &start, &end);
  EXPECT_EQ(res, true);
  EXPECT_GT(start, 0U);
  EXPECT_LT(start, end);
}
#  endif

TEST(MemoryMappingLayout, DumpListOfModules) {
  const char *last_slash = strrchr(argv0, '/');
  const char *binary_name = last_slash ? last_slash + 1 : argv0;
  MemoryMappingLayout memory_mapping(false);
  const uptr kMaxModules = 100;
  InternalMmapVector<LoadedModule> modules;
  modules.reserve(kMaxModules);
  memory_mapping.DumpListOfModules(&modules);
  EXPECT_GT(modules.size(), 0U);
  bool found = false;
  for (uptr i = 0; i < modules.size(); ++i) {
    if (modules[i].containsAddress((uptr)&noop)) {
      // Verify that the module name is sane.
      if (strstr(modules[i].full_name(), binary_name) != 0)
        found = true;
    }
    modules[i].clear();
  }
  EXPECT_TRUE(found);
}

TEST(MemoryMapping, LoadedModuleArchAndUUID) {
  if (SANITIZER_APPLE) {
    MemoryMappingLayout memory_mapping(false);
    const uptr kMaxModules = 100;
    InternalMmapVector<LoadedModule> modules;
    modules.reserve(kMaxModules);
    memory_mapping.DumpListOfModules(&modules);
    for (uptr i = 0; i < modules.size(); ++i) {
      ModuleArch arch = modules[i].arch();
      // Darwin unit tests are only run on i386/x86_64/x86_64h/arm64.
      if (SANITIZER_WORDSIZE == 32) {
        EXPECT_EQ(arch, kModuleArchI386);
      } else if (SANITIZER_WORDSIZE == 64) {
        EXPECT_TRUE(arch == kModuleArchX86_64 || arch == kModuleArchX86_64H ||
                    arch == kModuleArchARM64);
      }
      const u8 *uuid = modules[i].uuid();
      u8 null_uuid[kModuleUUIDSize] = {0};
      EXPECT_NE(memcmp(null_uuid, uuid, kModuleUUIDSize), 0);
    }
  }
}

#  if (SANITIZER_LINUX || SANITIZER_NETBSD || SANITIZER_SOLARIS) && \
      defined(_LP64)
const char *const parse_unix_input = R"(
7fb9862f1000-7fb9862f3000 rw-p 00000000 00:00 0 
Size:                  8 kB
Rss:                   4 kB
7fb9864ae000-7fb9864b1000 r--p 001ba000 fd:01 22413919                   /lib/x86_64-linux-gnu/libc-2.32.so
Size:                 12 kB
Rss:                  12 kB
)";

TEST(MemoryMapping, ParseUnixMemoryProfile) {
  struct entry {
    uptr p;
    uptr rss;
    bool file;
  };
  typedef std::vector<entry> entries_t;
  entries_t entries;
  std::vector<char> input(parse_unix_input,
                          parse_unix_input + strlen(parse_unix_input));
  ParseUnixMemoryProfile(
      [](uptr p, uptr rss, bool file, uptr *mem) {
        reinterpret_cast<entries_t *>(mem)->push_back({p, rss, file});
      },
      reinterpret_cast<uptr *>(&entries), &input[0], input.size());
  EXPECT_EQ(entries.size(), 2ul);
  EXPECT_EQ(entries[0].p, 0x7fb9862f1000ul);
  EXPECT_EQ(entries[0].rss, 4ul << 10);
  EXPECT_EQ(entries[0].file, false);
  EXPECT_EQ(entries[1].p, 0x7fb9864ae000ul);
  EXPECT_EQ(entries[1].rss, 12ul << 10);
  EXPECT_EQ(entries[1].file, true);
}

TEST(MemoryMapping, ParseUnixMemoryProfileTruncated) {
  // ParseUnixMemoryProfile used to crash on truncated inputs.
  // This test allocates 2 pages, protects the second one
  // and places the input at the very end of the first page
  // to test for over-reads.
  uptr page = GetPageSizeCached();
  char *mem = static_cast<char *>(
      MmapOrDie(2 * page, "ParseUnixMemoryProfileTruncated"));
  EXPECT_TRUE(MprotectNoAccess(reinterpret_cast<uptr>(mem + page), page));
  const uptr len = strlen(parse_unix_input);
  for (uptr i = 0; i < len; i++) {
    char *smaps = mem + page - len + i;
    memcpy(smaps, parse_unix_input, len - i);
    ParseUnixMemoryProfile([](uptr p, uptr rss, bool file, uptr *mem) {},
                           nullptr, smaps, len - i);
  }
  UnmapOrDie(mem, 2 * page);
}
#  endif

}  // namespace __sanitizer
#endif  // !defined(_WIN32)
