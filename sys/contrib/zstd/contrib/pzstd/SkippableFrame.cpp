/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#include "SkippableFrame.h"
#include "mem.h"
#include "utils/Range.h"

#include <cstdio>

using namespace pzstd;

SkippableFrame::SkippableFrame(std::uint32_t size) : frameSize_(size) {
  MEM_writeLE32(data_.data(), kSkippableFrameMagicNumber);
  MEM_writeLE32(data_.data() + 4, kFrameContentsSize);
  MEM_writeLE32(data_.data() + 8, frameSize_);
}

/* static */ std::size_t SkippableFrame::tryRead(ByteRange bytes) {
  if (bytes.size() < SkippableFrame::kSize ||
      MEM_readLE32(bytes.begin()) != kSkippableFrameMagicNumber ||
      MEM_readLE32(bytes.begin() + 4) != kFrameContentsSize) {
    return 0;
  }
  return MEM_readLE32(bytes.begin() + 8);
}
