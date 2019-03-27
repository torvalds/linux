/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#include "Pzstd.h"
extern "C" {
#include "datagen.h"
}
#include "test/RoundTrip.h"
#include "utils/ScopeGuard.h"

#include <cstddef>
#include <cstdio>
#include <gtest/gtest.h>
#include <memory>
#include <random>

using namespace std;
using namespace pzstd;

TEST(Pzstd, SmallSizes) {
  unsigned seed = std::random_device{}();
  std::fprintf(stderr, "Pzstd.SmallSizes seed: %u\n", seed);
  std::mt19937 gen(seed);

  for (unsigned len = 1; len < 256; ++len) {
    if (len % 16 == 0) {
      std::fprintf(stderr, "%u / 16\n", len / 16);
    }
    std::string inputFile = std::tmpnam(nullptr);
    auto guard = makeScopeGuard([&] { std::remove(inputFile.c_str()); });
    {
      static uint8_t buf[256];
      RDG_genBuffer(buf, len, 0.5, 0.0, gen());
      auto fd = std::fopen(inputFile.c_str(), "wb");
      auto written = std::fwrite(buf, 1, len, fd);
      std::fclose(fd);
      ASSERT_EQ(written, len);
    }
    for (unsigned numThreads = 1; numThreads <= 2; ++numThreads) {
      for (unsigned level = 1; level <= 4; level *= 4) {
        auto errorGuard = makeScopeGuard([&] {
          std::fprintf(stderr, "# threads: %u\n", numThreads);
          std::fprintf(stderr, "compression level: %u\n", level);
        });
        Options options;
        options.overwrite = true;
        options.inputFiles = {inputFile};
        options.numThreads = numThreads;
        options.compressionLevel = level;
        options.verbosity = 1;
        ASSERT_TRUE(roundTrip(options));
        errorGuard.dismiss();
      }
    }
  }
}

TEST(Pzstd, LargeSizes) {
  unsigned seed = std::random_device{}();
  std::fprintf(stderr, "Pzstd.LargeSizes seed: %u\n", seed);
  std::mt19937 gen(seed);

  for (unsigned len = 1 << 20; len <= (1 << 24); len *= 2) {
    std::string inputFile = std::tmpnam(nullptr);
    auto guard = makeScopeGuard([&] { std::remove(inputFile.c_str()); });
    {
      std::unique_ptr<uint8_t[]> buf(new uint8_t[len]);
      RDG_genBuffer(buf.get(), len, 0.5, 0.0, gen());
      auto fd = std::fopen(inputFile.c_str(), "wb");
      auto written = std::fwrite(buf.get(), 1, len, fd);
      std::fclose(fd);
      ASSERT_EQ(written, len);
    }
    for (unsigned numThreads = 1; numThreads <= 16; numThreads *= 4) {
      for (unsigned level = 1; level <= 4; level *= 4) {
        auto errorGuard = makeScopeGuard([&] {
          std::fprintf(stderr, "# threads: %u\n", numThreads);
          std::fprintf(stderr, "compression level: %u\n", level);
        });
        Options options;
        options.overwrite = true;
        options.inputFiles = {inputFile};
        options.numThreads = std::min(numThreads, options.numThreads);
        options.compressionLevel = level;
        options.verbosity = 1;
        ASSERT_TRUE(roundTrip(options));
        errorGuard.dismiss();
      }
    }
  }
}

TEST(Pzstd, DISABLED_ExtremelyLargeSize) {
  unsigned seed = std::random_device{}();
  std::fprintf(stderr, "Pzstd.ExtremelyLargeSize seed: %u\n", seed);
  std::mt19937 gen(seed);

  std::string inputFile = std::tmpnam(nullptr);
  auto guard = makeScopeGuard([&] { std::remove(inputFile.c_str()); });

  {
    // Write 4GB + 64 MB
    constexpr size_t kLength = 1 << 26;
    std::unique_ptr<uint8_t[]> buf(new uint8_t[kLength]);
    auto fd = std::fopen(inputFile.c_str(), "wb");
    auto closeGuard = makeScopeGuard([&] { std::fclose(fd); });
    for (size_t i = 0; i < (1 << 6) + 1; ++i) {
      RDG_genBuffer(buf.get(), kLength, 0.5, 0.0, gen());
      auto written = std::fwrite(buf.get(), 1, kLength, fd);
      if (written != kLength) {
        std::fprintf(stderr, "Failed to write file, skipping test\n");
        return;
      }
    }
  }

  Options options;
  options.overwrite = true;
  options.inputFiles = {inputFile};
  options.compressionLevel = 1;
  if (options.numThreads == 0) {
    options.numThreads = 1;
  }
  ASSERT_TRUE(roundTrip(options));
}

TEST(Pzstd, ExtremelyCompressible) {
  std::string inputFile = std::tmpnam(nullptr);
  auto guard = makeScopeGuard([&] { std::remove(inputFile.c_str()); });
  {
    std::unique_ptr<uint8_t[]> buf(new uint8_t[10000]);
    std::memset(buf.get(), 'a', 10000);
    auto fd = std::fopen(inputFile.c_str(), "wb");
    auto written = std::fwrite(buf.get(), 1, 10000, fd);
    std::fclose(fd);
    ASSERT_EQ(written, 10000);
  }
  Options options;
  options.overwrite = true;
  options.inputFiles = {inputFile};
  options.numThreads = 1;
  options.compressionLevel = 1;
  ASSERT_TRUE(roundTrip(options));
}
