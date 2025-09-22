//===-- compression.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/stack_trace_compressor.h"
#include "gwp_asan/tests/harness.h"

namespace gwp_asan {
namespace compression {

TEST(GwpAsanCompressionTest, SingleByteVarInt) {
  uint8_t Compressed[1];

  uintptr_t Uncompressed = 0x00;
  EXPECT_EQ(1u, pack(&Uncompressed, 1u, Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[0], 0x00);

  Uncompressed = 0x01;
  EXPECT_EQ(1u, pack(&Uncompressed, 1u, Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[0], 0x02); // +1 => 2 in zigzag.

  Uncompressed = 0x3f;
  EXPECT_EQ(1u, pack(&Uncompressed, 1u, Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[0], 0x7e); // +63 => 127 in zigzag.
}

TEST(GwpAsanCompressionTest, MultiByteVarInt) {
  uint8_t Compressed[sizeof(uintptr_t) + 1];

  uintptr_t Uncompressed = 0x40;
  EXPECT_EQ(2u, pack(&Uncompressed, 1u, Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[0], 0x80); // +64 => 128 in zigzag.
  EXPECT_EQ(Compressed[1], 0x01);

  Uncompressed = 0x41;
  EXPECT_EQ(2u, pack(&Uncompressed, 1u, Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[0], 0x82); // +65 => 130 in zigzag
  EXPECT_EQ(Compressed[1], 0x01);

  Uncompressed = 0x1fff;
  EXPECT_EQ(2u, pack(&Uncompressed, 1u, Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[0], 0xfe); // +8191 => 16382 in zigzag
  EXPECT_EQ(Compressed[1], 0x7f);

  Uncompressed = 0x2000;
  EXPECT_EQ(3u, pack(&Uncompressed, 1u, Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[0], 0x80); // +8192 => 16384 in zigzag
  EXPECT_EQ(Compressed[1], 0x80);
  EXPECT_EQ(Compressed[2], 0x01);

  Uncompressed = 0x7f010ff0;
  EXPECT_EQ(5u, pack(&Uncompressed, 1u, Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[0], 0xe0); // +0x7f010ff0 => 0xFE021FE0 in zigzag
  EXPECT_EQ(Compressed[1], 0xbf);
  EXPECT_EQ(Compressed[2], 0x88);
  EXPECT_EQ(Compressed[3], 0xf0);
  EXPECT_EQ(Compressed[4], 0x0f);
}

TEST(GwpAsanCompressionTest, CorrectDifference) {
  uint8_t Compressed[10];
  uintptr_t Uncompressed[2] = {0x00, 0x00};

  EXPECT_EQ(2u, pack(Uncompressed, sizeof(Uncompressed) / sizeof(uintptr_t),
                     Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[1], 0x00); // +0 difference => 0 in zigzag.

  Uncompressed[1] = 0x01;
  EXPECT_EQ(2u, pack(Uncompressed, sizeof(Uncompressed) / sizeof(uintptr_t),
                     Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[1], 0x02); // +1 difference => 2 in zigzag.

  Uncompressed[1] = 0x02;
  EXPECT_EQ(2u, pack(Uncompressed, sizeof(Uncompressed) / sizeof(uintptr_t),
                     Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[1], 0x04); // +2 difference => 4 in zigzag.

  Uncompressed[1] = 0x80;
  EXPECT_EQ(3u, pack(Uncompressed, sizeof(Uncompressed) / sizeof(uintptr_t),
                     Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[1], 0x80); // +128 difference => +256 in zigzag (note the
  EXPECT_EQ(Compressed[2], 0x02); // varint encoding here).

  Uncompressed[0] = 0x01;
  Uncompressed[1] = 0x00;
  EXPECT_EQ(2u, pack(Uncompressed, sizeof(Uncompressed) / sizeof(uintptr_t),
                     Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[1], 0x01); // -1 difference => +1 in zigzag.

  Uncompressed[0] = 0x02;
  EXPECT_EQ(2u, pack(Uncompressed, sizeof(Uncompressed) / sizeof(uintptr_t),
                     Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[1], 0x03); // -2 difference => +3 in zigzag.

  Uncompressed[0] = 0x80;
  EXPECT_EQ(4u, pack(Uncompressed, sizeof(Uncompressed) / sizeof(uintptr_t),
                     Compressed, sizeof(Compressed)));
  EXPECT_EQ(Compressed[2], 0xff); // -128 difference => +255 in zigzag (note the
  EXPECT_EQ(Compressed[3], 0x01); // varint encoding here).
}

// Space needed to encode the biggest uintptr_t as a varint is ceil((8 / 7) *
// sizeof(uintptr_t)), as each 7 bits requires 8 bits of space.
constexpr size_t kBytesForLargestVarInt = (sizeof(uintptr_t) * 8) / 7 + 1;

// Ensures that when the closest diff between two pointers is via. underflow,
// we take the underflow option.
TEST(GwpAsanCompressionTest, ClosestDiffIsUnderflow) {
  uint8_t Compressed[2];
  uintptr_t Uncompressed[2] = {0x00, UINTPTR_MAX};

  EXPECT_EQ(2u, pack(Uncompressed, sizeof(Uncompressed) / sizeof(uintptr_t),
                     Compressed, sizeof(Compressed)));
  // -1 difference => +1 in zigzag.
  EXPECT_EQ(Compressed[1], 0x01);
}

// Ensures that when the closest diff between two pointers is via. overflow,
// that we take this option.
TEST(GwpAsanCompressionTest, ClosestDiffIsOverflow) {
  uint8_t Compressed[2];
  uintptr_t Uncompressed[2] = {UINTPTR_MAX, 0x00};

  // Note here that the first element is encoded as the difference from zero.
  EXPECT_EQ(2u, pack(Uncompressed, sizeof(Uncompressed) / sizeof(uintptr_t),
                     Compressed, sizeof(Compressed)));
  // -1 difference => +1 in zigzag (the first pointer is encoded as -1).
  EXPECT_EQ(Compressed[0], 0x01);
  // +1 difference => +2 in zigzag.
  EXPECT_EQ(Compressed[1], 0x02);
}

void runPackUnpack(uintptr_t *Test, size_t NumEntries) {
  // Setup the input/output buffers based on the maximum possible size.
  uintptr_t *Uncompressed =
      static_cast<uintptr_t *>(alloca(NumEntries * sizeof(uintptr_t)));
  size_t CompressedBufferSize = NumEntries * kBytesForLargestVarInt;
  uint8_t *Compressed = static_cast<uint8_t *>(alloca(CompressedBufferSize));

  // Pack the provided testcase, recoding the number of bytes it took for
  // storage.
  size_t BytesUsedForPacking =
      pack(Test, NumEntries, Compressed, CompressedBufferSize);
  EXPECT_NE(BytesUsedForPacking, 0u);

  // Unpack the testcase and ensure that the correct number of entries was
  // unpacked.
  EXPECT_EQ(NumEntries,
            unpack(Compressed, BytesUsedForPacking, Uncompressed, NumEntries));

  // Ensure that the unpacked trace is the same as the original testcase.
  for (size_t i = 0; i < NumEntries; ++i) {
    EXPECT_EQ(Uncompressed[i], Test[i]);
  }
}

TEST(GwpAsanCompressionTest, UncompressVarInt) {
  uint8_t Compressed[] = {0x00, 0xaa, 0xaf, 0xd0, 0xda, 0x04};
  uintptr_t Uncompressed[2];

  EXPECT_EQ(2u, unpack(Compressed, sizeof(Compressed), Uncompressed, 2u));
  EXPECT_EQ(Uncompressed[0], 0x00u);
  EXPECT_EQ(Uncompressed[1], 0x25aa0bd5u);
}

TEST(GwpAsanCompressionTest, UncompressVarIntUnderflow) {
  uint8_t Compressed[] = {0x00, 0xab, 0xaf, 0xd0, 0xda, 0x04};
  uintptr_t Uncompressed[2];

  EXPECT_EQ(2u, unpack(Compressed, sizeof(Compressed), Uncompressed, 2u));
  EXPECT_EQ(Uncompressed[0], 0x00u);
  EXPECT_EQ(Uncompressed[1], UINTPTR_MAX - 0x25aa0bd5u);
}

TEST(GwpAsanCompressionTest, CompressUncompressAscending) {
  uintptr_t Test[] = {1, 2, 3};
  runPackUnpack(Test, sizeof(Test) / sizeof(uintptr_t));
}

TEST(GwpAsanCompressionTest, CompressUncompressDescending) {
  uintptr_t Test[] = {3, 2, 1};
  runPackUnpack(Test, sizeof(Test) / sizeof(uintptr_t));
}

TEST(GwpAsanCompressionTest, CompressUncompressRepeated) {
  uintptr_t Test[] = {3, 3, 3};
  runPackUnpack(Test, sizeof(Test) / sizeof(uintptr_t));
}

TEST(GwpAsanCompressionTest, CompressUncompressZigZag) {
  uintptr_t Test[] = {1, 3, 2, 4, 1, 2};
  runPackUnpack(Test, sizeof(Test) / sizeof(uintptr_t));
}

TEST(GwpAsanCompressionTest, CompressUncompressVarInt) {
  uintptr_t Test[] = {0x1981561, 0x18560, 0x25ab9135, 0x1232562};
  runPackUnpack(Test, sizeof(Test) / sizeof(uintptr_t));
}

TEST(GwpAsanCompressionTest, CompressUncompressLargestDifference) {
  uintptr_t Test[] = {0x00, INTPTR_MAX, UINTPTR_MAX, INTPTR_MAX, 0x00};
  runPackUnpack(Test, sizeof(Test) / sizeof(uintptr_t));
}

TEST(GwpAsanCompressionTest, CompressUncompressBigPointers) {
  uintptr_t Test[] = {UINTPTR_MAX, UINTPTR_MAX - 10};
  runPackUnpack(Test, sizeof(Test) / sizeof(uintptr_t));

  uintptr_t Test2[] = {UINTPTR_MAX - 10, UINTPTR_MAX};
  runPackUnpack(Test2, sizeof(Test2) / sizeof(uintptr_t));
}

TEST(GwpAsanCompressionTest, UncompressFailsWithOutOfBoundsVarInt) {
  uint8_t Compressed[kBytesForLargestVarInt + 1];
  for (size_t i = 0; i < kBytesForLargestVarInt; ++i) {
    Compressed[i] = 0x80;
  }
  Compressed[kBytesForLargestVarInt] = 0x00;

  uintptr_t Uncompressed;
  EXPECT_EQ(unpack(Compressed, kBytesForLargestVarInt + 1, &Uncompressed, 1),
            0u);
}

TEST(GwpAsanCompressionTest, UncompressFailsWithTooSmallBuffer) {
  uint8_t Compressed[] = {0x80, 0x00};

  uintptr_t Uncompressed;
  EXPECT_EQ(unpack(Compressed, 1u, &Uncompressed, 1), 0u);
}

TEST(GwpAsanCompressionTest, CompressPartiallySucceedsWithTooSmallBuffer) {
  uintptr_t Uncompressed[] = {
      0x80,  // Requires 2 bytes for varint.
      0x100, // Requires two bytes for varint difference of 0x80.
      0xff,  // Requires single byte for varint difference of -0x01
  };
  uint8_t Compressed[3 * kBytesForLargestVarInt];

  // Zero and one byte buffers shouldn't encode anything (see above for size
  // requirements).
  EXPECT_EQ(pack(Uncompressed, 3u, Compressed, 0u), 0u);
  EXPECT_EQ(pack(Uncompressed, 3u, Compressed, 1u), 0u);

  // Two byte buffer should hold a single varint-encoded value.
  EXPECT_EQ(pack(Uncompressed, 3u, Compressed, 2u), 2u);

  // Three bytes isn't enough to cover the first two pointers, as both take two
  // bytes each to store. Expect a single value to be compressed.
  EXPECT_EQ(pack(Uncompressed, 3u, Compressed, 3u), 2u);

  // Four bytes is enough for the first two pointers to be stored.
  EXPECT_EQ(pack(Uncompressed, 3u, Compressed, 4u), 4u);

  // And five is enough for all three pointers to be stored.
  EXPECT_EQ(pack(Uncompressed, 3u, Compressed, 5u), 5u);
  // And a buffer that's bigger than five bytes should still only write five
  // bytes.
  EXPECT_EQ(pack(Uncompressed, 3u, Compressed, 6u), 5u);
  EXPECT_EQ(pack(Uncompressed, 3u, Compressed, 3 * kBytesForLargestVarInt), 5u);
}
} // namespace compression
} // namespace gwp_asan
