/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
extern "C" {
#include "datagen.h"
}
#include "Options.h"
#include "test/RoundTrip.h"
#include "utils/ScopeGuard.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>

using namespace std;
using namespace pzstd;

namespace {
string
writeData(size_t size, double matchProba, double litProba, unsigned seed) {
  std::unique_ptr<uint8_t[]> buf(new uint8_t[size]);
  RDG_genBuffer(buf.get(), size, matchProba, litProba, seed);
  string file = tmpnam(nullptr);
  auto fd = std::fopen(file.c_str(), "wb");
  auto guard = makeScopeGuard([&] { std::fclose(fd); });
  auto bytesWritten = std::fwrite(buf.get(), 1, size, fd);
  if (bytesWritten != size) {
    std::abort();
  }
  return file;
}

template <typename Generator>
string generateInputFile(Generator& gen) {
  // Use inputs ranging from 1 Byte to 2^16 Bytes
  std::uniform_int_distribution<size_t> size{1, 1 << 16};
  std::uniform_real_distribution<> prob{0, 1};
  return writeData(size(gen), prob(gen), prob(gen), gen());
}

template <typename Generator>
Options generateOptions(Generator& gen, const string& inputFile) {
  Options options;
  options.inputFiles = {inputFile};
  options.overwrite = true;

  std::uniform_int_distribution<unsigned> numThreads{1, 32};
  std::uniform_int_distribution<unsigned> compressionLevel{1, 10};

  options.numThreads = numThreads(gen);
  options.compressionLevel = compressionLevel(gen);

  return options;
}
}

int main() {
  std::mt19937 gen(std::random_device{}());

  auto newlineGuard = makeScopeGuard([] { std::fprintf(stderr, "\n"); });
  for (unsigned i = 0; i < 10000; ++i) {
    if (i % 100 == 0) {
      std::fprintf(stderr, "Progress: %u%%\r", i / 100);
    }
    auto inputFile = generateInputFile(gen);
    auto inputGuard = makeScopeGuard([&] { std::remove(inputFile.c_str()); });
    for (unsigned i = 0; i < 10; ++i) {
      auto options = generateOptions(gen, inputFile);
      if (!roundTrip(options)) {
        std::fprintf(stderr, "numThreads: %u\n", options.numThreads);
        std::fprintf(stderr, "level: %u\n", options.compressionLevel);
        std::fprintf(stderr, "decompress? %u\n", (unsigned)options.decompress);
        std::fprintf(stderr, "file: %s\n", inputFile.c_str());
        return 1;
      }
    }
  }
  return 0;
}
