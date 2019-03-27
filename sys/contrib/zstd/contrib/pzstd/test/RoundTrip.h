/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#pragma once

#include "Options.h"
#include "Pzstd.h"
#include "utils/ScopeGuard.h"

#include <cstdio>
#include <string>
#include <cstdint>
#include <memory>

namespace pzstd {

inline bool check(std::string source, std::string decompressed) {
  std::unique_ptr<std::uint8_t[]> sBuf(new std::uint8_t[1024]);
  std::unique_ptr<std::uint8_t[]> dBuf(new std::uint8_t[1024]);

  auto sFd = std::fopen(source.c_str(), "rb");
  auto dFd = std::fopen(decompressed.c_str(), "rb");
  auto guard = makeScopeGuard([&] {
    std::fclose(sFd);
    std::fclose(dFd);
  });

  size_t sRead, dRead;

  do {
    sRead = std::fread(sBuf.get(), 1, 1024, sFd);
    dRead = std::fread(dBuf.get(), 1, 1024, dFd);
    if (std::ferror(sFd) || std::ferror(dFd)) {
      return false;
    }
    if (sRead != dRead) {
      return false;
    }

    for (size_t i = 0; i < sRead; ++i) {
      if (sBuf.get()[i] != dBuf.get()[i]) {
        return false;
      }
    }
  } while (sRead == 1024);
  if (!std::feof(sFd) || !std::feof(dFd)) {
    return false;
  }
  return true;
}

inline bool roundTrip(Options& options) {
  if (options.inputFiles.size() != 1) {
    return false;
  }
  std::string source = options.inputFiles.front();
  std::string compressedFile = std::tmpnam(nullptr);
  std::string decompressedFile = std::tmpnam(nullptr);
  auto guard = makeScopeGuard([&] {
    std::remove(compressedFile.c_str());
    std::remove(decompressedFile.c_str());
  });

  {
    options.outputFile = compressedFile;
    options.decompress = false;
    if (pzstdMain(options) != 0) {
      return false;
    }
  }
  {
    options.decompress = true;
    options.inputFiles.front() = compressedFile;
    options.outputFile = decompressedFile;
    if (pzstdMain(options) != 0) {
      return false;
    }
  }
  return check(source, decompressedFile);
}
}
