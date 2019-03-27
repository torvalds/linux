/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#pragma once

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#undef ZSTD_STATIC_LINKING_ONLY

#include <cstdint>
#include <string>
#include <vector>

namespace pzstd {

struct Options {
  enum class WriteMode { Regular, Auto, Sparse };

  unsigned numThreads;
  unsigned maxWindowLog;
  unsigned compressionLevel;
  bool decompress;
  std::vector<std::string> inputFiles;
  std::string outputFile;
  bool overwrite;
  bool keepSource;
  WriteMode writeMode;
  bool checksum;
  int verbosity;

  enum class Status {
    Success, // Successfully parsed options
    Failure, // Failure to parse options
    Message  // Options specified to print a message (e.g. "-h")
  };

  Options();
  Options(unsigned numThreads, unsigned maxWindowLog, unsigned compressionLevel,
          bool decompress, std::vector<std::string> inputFiles,
          std::string outputFile, bool overwrite, bool keepSource,
          WriteMode writeMode, bool checksum, int verbosity)
      : numThreads(numThreads), maxWindowLog(maxWindowLog),
        compressionLevel(compressionLevel), decompress(decompress),
        inputFiles(std::move(inputFiles)), outputFile(std::move(outputFile)),
        overwrite(overwrite), keepSource(keepSource), writeMode(writeMode),
        checksum(checksum), verbosity(verbosity) {}

  Status parse(int argc, const char **argv);

  ZSTD_parameters determineParameters() const {
    ZSTD_parameters params = ZSTD_getParams(compressionLevel, 0, 0);
    params.fParams.contentSizeFlag = 0;
    params.fParams.checksumFlag = checksum;
    if (maxWindowLog != 0 && params.cParams.windowLog > maxWindowLog) {
      params.cParams.windowLog = maxWindowLog;
      params.cParams = ZSTD_adjustCParams(params.cParams, 0, 0);
    }
    return params;
  }

  std::string getOutputFile(const std::string &inputFile) const;
};
}
