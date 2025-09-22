//===-- sanitizer_procmaps_mac_test.cpp ---------------------------------------===//
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

#  include "sanitizer_common/sanitizer_platform.h"

#  if SANITIZER_APPLE

#  include <stdlib.h>
#  include <string.h>
#  include <stdint.h>
#  include <stdio.h>

#  include <vector>
#  include <mach-o/dyld.h>
#  include <mach-o/loader.h>

#  include "gtest/gtest.h"

#  include "sanitizer_common/sanitizer_procmaps.h"

namespace __sanitizer {

class MemoryMappingLayoutMock final : public MemoryMappingLayout {
private:
  static constexpr uuid_command mock_uuid_command = {
    .cmd = LC_UUID,
    .cmdsize = sizeof(uuid_command),
    .uuid = {}
  };

  static constexpr char dylib_name[] = "libclang_rt.\0\0\0"; // 8 bytes aligned, padded with zeros per loader.h
  static constexpr dylib_command mock_dylib_command = {
    .cmd = LC_LOAD_DYLIB,
    .cmdsize = sizeof(dylib_command) + sizeof(dylib_name),
    .dylib = {
      .name = {
        .offset = sizeof(dylib_command)
      }
    }
  };

  static constexpr uuid_command mock_trap_command = {
    .cmd = LC_UUID,
    .cmdsize = 0x10000,
    .uuid = {}
  };

  const char *start_load_cmd_addr;
  size_t sizeofcmds;
  std::vector<unsigned char> mock_header;

public:
  MemoryMappingLayoutMock(): MemoryMappingLayout(false) {
    EXPECT_EQ(mock_uuid_command.cmdsize % 8, 0u);
    EXPECT_EQ(mock_dylib_command.cmdsize % 8, 0u);

    Reset();

#ifdef MH_MAGIC_64
    const struct mach_header_64 *header = (mach_header_64 *)_dyld_get_image_header(0); // Any header will do
    const size_t header_size = sizeof(mach_header_64);
#else
    const struct mach_header *header = _dyld_get_image_header(0);
    const size_t header_size = sizeof(mach_header);
#endif
    const size_t mock_header_size_with_extras = header_size + header->sizeofcmds +
      mock_uuid_command.cmdsize + mock_dylib_command.cmdsize + sizeof(uuid_command);

    mock_header.reserve(mock_header_size_with_extras);
    // Copy the original header
    copy((unsigned char *)header,
      (unsigned char *)header + header_size + header->sizeofcmds,
      back_inserter(mock_header));
    // The following commands are not supposed to be processed
    // by the (correct) ::Next method at all, since they're not
    // accounted for in header->ncmds .
    copy((unsigned char *)&mock_uuid_command,
      ((unsigned char *)&mock_uuid_command) + mock_uuid_command.cmdsize,
      back_inserter(mock_header));
    copy((unsigned char *)&mock_dylib_command,
      ((unsigned char *)&mock_dylib_command) + sizeof(dylib_command), // as mock_dylib_command.cmdsize contains the following string
      back_inserter(mock_header));
    copy((unsigned char *)dylib_name,
      ((unsigned char *)dylib_name) + sizeof(dylib_name),
      back_inserter(mock_header));

    // Append a command w. huge size to have the test detect the read overrun
    copy((unsigned char *)&mock_trap_command,
      ((unsigned char *)&mock_trap_command) + sizeof(uuid_command),
      back_inserter(mock_header));

    start_load_cmd_addr = (const char *)(mock_header.data() + header_size);
    sizeofcmds = header->sizeofcmds;

    const char *last_byte_load_cmd_addr = (start_load_cmd_addr+sizeofcmds-1);
    data_.current_image = -1; // So the loop in ::Next runs just once
  }

  size_t SizeOfLoadCommands() {
    return sizeofcmds;
  }

  size_t CurrentLoadCommandOffset() {
    size_t offset = data_.current_load_cmd_addr - start_load_cmd_addr;
    return offset;
  }

protected:
  virtual ImageHeader *CurrentImageHeader() override {
    return (ImageHeader *)mock_header.data();
  }
};

TEST(MemoryMappingLayout, Next) {
  __sanitizer::MemoryMappingLayoutMock memory_mapping;
  __sanitizer::MemoryMappedSegment segment;
  size_t size = memory_mapping.SizeOfLoadCommands();
  while (memory_mapping.Next(&segment)) {
    size_t offset = memory_mapping.CurrentLoadCommandOffset();
    EXPECT_LE(offset, size);
  }
  size_t final_offset = memory_mapping.CurrentLoadCommandOffset();
  EXPECT_EQ(final_offset, size); // All commands processed, no more, no less
}

}  // namespace __sanitizer

#  endif // SANITIZER_APPLE
