/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#pragma once

#include "utils/Range.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace pzstd {
/**
 * We put a skippable frame before each frame.
 * It contains a skippable frame magic number, the size of the skippable frame,
 * and the size of the next frame.
 * Each skippable frame is exactly 12 bytes in little endian format.
 * The first 8 bytes are for compatibility with the ZSTD format.
 * If we have N threads, the output will look like
 *
 * [0x184D2A50|4|size1] [frame1 of size size1]
 * [0x184D2A50|4|size2] [frame2 of size size2]
 * ...
 * [0x184D2A50|4|sizeN] [frameN of size sizeN]
 *
 * Each sizeX is 4 bytes.
 *
 * These skippable frames should allow us to skip through the compressed file
 * and only load at most N pages.
 */
class SkippableFrame {
 public:
  static constexpr std::size_t kSize = 12;

 private:
  std::uint32_t frameSize_;
  std::array<std::uint8_t, kSize> data_;
  static constexpr std::uint32_t kSkippableFrameMagicNumber = 0x184D2A50;
  // Could be improved if the size fits in less bytes
  static constexpr std::uint32_t kFrameContentsSize = kSize - 8;

 public:
   // Write the skippable frame to data_ in LE format.
  explicit SkippableFrame(std::uint32_t size);

  // Read the skippable frame from bytes in LE format.
  static std::size_t tryRead(ByteRange bytes);

  ByteRange data() const {
    return {data_.data(), data_.size()};
  }

  // Size of the next frame.
  std::size_t frameSize() const {
    return frameSize_;
  }
};
}
