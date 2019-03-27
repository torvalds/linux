/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#include "Options.h"
#include "util.h"
#include "utils/ScopeGuard.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <thread>
#include <vector>


namespace pzstd {

namespace {
unsigned defaultNumThreads() {
#ifdef PZSTD_NUM_THREADS
  return PZSTD_NUM_THREADS;
#else
  return std::thread::hardware_concurrency();
#endif
}

unsigned parseUnsigned(const char **arg) {
  unsigned result = 0;
  while (**arg >= '0' && **arg <= '9') {
    result *= 10;
    result += **arg - '0';
    ++(*arg);
  }
  return result;
}

const char *getArgument(const char *options, const char **argv, int &i,
                        int argc) {
  if (options[1] != 0) {
    return options + 1;
  }
  ++i;
  if (i == argc) {
    std::fprintf(stderr, "Option -%c requires an argument, but none provided\n",
                 *options);
    return nullptr;
  }
  return argv[i];
}

const std::string kZstdExtension = ".zst";
constexpr char kStdIn[] = "-";
constexpr char kStdOut[] = "-";
constexpr unsigned kDefaultCompressionLevel = 3;
constexpr unsigned kMaxNonUltraCompressionLevel = 19;

#ifdef _WIN32
const char nullOutput[] = "nul";
#else
const char nullOutput[] = "/dev/null";
#endif

void notSupported(const char *option) {
  std::fprintf(stderr, "Operation not supported: %s\n", option);
}

void usage() {
  std::fprintf(stderr, "Usage:\n");
  std::fprintf(stderr, "  pzstd [args] [FILE(s)]\n");
  std::fprintf(stderr, "Parallel ZSTD options:\n");
  std::fprintf(stderr, "  -p, --processes   #    : number of threads to use for (de)compression (default:<numcpus>)\n");

  std::fprintf(stderr, "ZSTD options:\n");
  std::fprintf(stderr, "  -#                     : # compression level (1-%d, default:%d)\n", kMaxNonUltraCompressionLevel, kDefaultCompressionLevel);
  std::fprintf(stderr, "  -d, --decompress       : decompression\n");
  std::fprintf(stderr, "  -o                file : result stored into `file` (only if 1 input file)\n");
  std::fprintf(stderr, "  -f, --force            : overwrite output without prompting, (de)compress links\n");
  std::fprintf(stderr, "      --rm               : remove source file(s) after successful (de)compression\n");
  std::fprintf(stderr, "  -k, --keep             : preserve source file(s) (default)\n");
  std::fprintf(stderr, "  -h, --help             : display help and exit\n");
  std::fprintf(stderr, "  -V, --version          : display version number and exit\n");
  std::fprintf(stderr, "  -v, --verbose          : verbose mode; specify multiple times to increase log level (default:2)\n");
  std::fprintf(stderr, "  -q, --quiet            : suppress warnings; specify twice to suppress errors too\n");
  std::fprintf(stderr, "  -c, --stdout           : force write to standard output, even if it is the console\n");
#ifdef UTIL_HAS_CREATEFILELIST
  std::fprintf(stderr, "  -r                     : operate recursively on directories\n");
#endif
  std::fprintf(stderr, "      --ultra            : enable levels beyond %i, up to %i (requires more memory)\n", kMaxNonUltraCompressionLevel, ZSTD_maxCLevel());
  std::fprintf(stderr, "  -C, --check            : integrity check (default)\n");
  std::fprintf(stderr, "      --no-check         : no integrity check\n");
  std::fprintf(stderr, "  -t, --test             : test compressed file integrity\n");
  std::fprintf(stderr, "  --                     : all arguments after \"--\" are treated as files\n");
}
} // anonymous namespace

Options::Options()
    : numThreads(defaultNumThreads()), maxWindowLog(23),
      compressionLevel(kDefaultCompressionLevel), decompress(false),
      overwrite(false), keepSource(true), writeMode(WriteMode::Auto),
      checksum(true), verbosity(2) {}

Options::Status Options::parse(int argc, const char **argv) {
  bool test = false;
  bool recursive = false;
  bool ultra = false;
  bool forceStdout = false;
  bool followLinks = false;
  // Local copy of input files, which are pointers into argv.
  std::vector<const char *> localInputFiles;
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    // Protect against empty arguments
    if (arg[0] == 0) {
      continue;
    }
    // Everything after "--" is an input file
    if (!std::strcmp(arg, "--")) {
      ++i;
      std::copy(argv + i, argv + argc, std::back_inserter(localInputFiles));
      break;
    }
    // Long arguments that don't have a short option
    {
      bool isLongOption = true;
      if (!std::strcmp(arg, "--rm")) {
        keepSource = false;
      } else if (!std::strcmp(arg, "--ultra")) {
        ultra = true;
        maxWindowLog = 0;
      } else if (!std::strcmp(arg, "--no-check")) {
        checksum = false;
      } else if (!std::strcmp(arg, "--sparse")) {
        writeMode = WriteMode::Sparse;
        notSupported("Sparse mode");
        return Status::Failure;
      } else if (!std::strcmp(arg, "--no-sparse")) {
        writeMode = WriteMode::Regular;
        notSupported("Sparse mode");
        return Status::Failure;
      } else if (!std::strcmp(arg, "--dictID")) {
        notSupported(arg);
        return Status::Failure;
      } else if (!std::strcmp(arg, "--no-dictID")) {
        notSupported(arg);
        return Status::Failure;
      } else {
        isLongOption = false;
      }
      if (isLongOption) {
        continue;
      }
    }
    // Arguments with a short option simply set their short option.
    const char *options = nullptr;
    if (!std::strcmp(arg, "--processes")) {
      options = "p";
    } else if (!std::strcmp(arg, "--version")) {
      options = "V";
    } else if (!std::strcmp(arg, "--help")) {
      options = "h";
    } else if (!std::strcmp(arg, "--decompress")) {
      options = "d";
    } else if (!std::strcmp(arg, "--force")) {
      options = "f";
    } else if (!std::strcmp(arg, "--stdout")) {
      options = "c";
    } else if (!std::strcmp(arg, "--keep")) {
      options = "k";
    } else if (!std::strcmp(arg, "--verbose")) {
      options = "v";
    } else if (!std::strcmp(arg, "--quiet")) {
      options = "q";
    } else if (!std::strcmp(arg, "--check")) {
      options = "C";
    } else if (!std::strcmp(arg, "--test")) {
      options = "t";
    } else if (arg[0] == '-' && arg[1] != 0) {
      options = arg + 1;
    } else {
      localInputFiles.emplace_back(arg);
      continue;
    }
    assert(options != nullptr);

    bool finished = false;
    while (!finished && *options != 0) {
      // Parse the compression level
      if (*options >= '0' && *options <= '9') {
        compressionLevel = parseUnsigned(&options);
        continue;
      }

      switch (*options) {
      case 'h':
      case 'H':
        usage();
        return Status::Message;
      case 'V':
        std::fprintf(stderr, "PZSTD version: %s.\n", ZSTD_VERSION_STRING);
        return Status::Message;
      case 'p': {
        finished = true;
        const char *optionArgument = getArgument(options, argv, i, argc);
        if (optionArgument == nullptr) {
          return Status::Failure;
        }
        if (*optionArgument < '0' || *optionArgument > '9') {
          std::fprintf(stderr, "Option -p expects a number, but %s provided\n",
                       optionArgument);
          return Status::Failure;
        }
        numThreads = parseUnsigned(&optionArgument);
        if (*optionArgument != 0) {
          std::fprintf(stderr,
                       "Option -p expects a number, but %u%s provided\n",
                       numThreads, optionArgument);
          return Status::Failure;
        }
        break;
      }
      case 'o': {
        finished = true;
        const char *optionArgument = getArgument(options, argv, i, argc);
        if (optionArgument == nullptr) {
          return Status::Failure;
        }
        outputFile = optionArgument;
        break;
      }
      case 'C':
        checksum = true;
        break;
      case 'k':
        keepSource = true;
        break;
      case 'd':
        decompress = true;
        break;
      case 'f':
        overwrite = true;
        forceStdout = true;
        followLinks = true;
        break;
      case 't':
        test = true;
        decompress = true;
        break;
#ifdef UTIL_HAS_CREATEFILELIST
      case 'r':
        recursive = true;
        break;
#endif
      case 'c':
        outputFile = kStdOut;
        forceStdout = true;
        break;
      case 'v':
        ++verbosity;
        break;
      case 'q':
        --verbosity;
        // Ignore them for now
        break;
      // Unsupported options from Zstd
      case 'D':
      case 's':
        notSupported("Zstd dictionaries.");
        return Status::Failure;
      case 'b':
      case 'e':
      case 'i':
      case 'B':
        notSupported("Zstd benchmarking options.");
        return Status::Failure;
      default:
        std::fprintf(stderr, "Invalid argument: %s\n", arg);
        return Status::Failure;
      }
      if (!finished) {
        ++options;
      }
    } // while (*options != 0);
  }   // for (int i = 1; i < argc; ++i);

  // Set options for test mode
  if (test) {
    outputFile = nullOutput;
    keepSource = true;
  }

  // Input file defaults to standard input if not provided.
  if (localInputFiles.empty()) {
    localInputFiles.emplace_back(kStdIn);
  }

  // Check validity of input files
  if (localInputFiles.size() > 1) {
    const auto it = std::find(localInputFiles.begin(), localInputFiles.end(),
                              std::string{kStdIn});
    if (it != localInputFiles.end()) {
      std::fprintf(
          stderr,
          "Cannot specify standard input when handling multiple files\n");
      return Status::Failure;
    }
  }
  if (localInputFiles.size() > 1 || recursive) {
    if (!outputFile.empty() && outputFile != nullOutput) {
      std::fprintf(
          stderr,
          "Cannot specify an output file when handling multiple inputs\n");
      return Status::Failure;
    }
  }

  g_utilDisplayLevel = verbosity;
  // Remove local input files that are symbolic links
  if (!followLinks) {
      std::remove_if(localInputFiles.begin(), localInputFiles.end(),
                     [&](const char *path) {
                        bool isLink = UTIL_isLink(path);
                        if (isLink && verbosity >= 2) {
                            std::fprintf(
                                    stderr,
                                    "Warning : %s is symbolic link, ignoring\n",
                                    path);
                        }
                        return isLink;
                    });
  }

  // Translate input files/directories into files to (de)compress
  if (recursive) {
    char *scratchBuffer = nullptr;
    unsigned numFiles = 0;
    const char **files =
        UTIL_createFileList(localInputFiles.data(), localInputFiles.size(),
                            &scratchBuffer, &numFiles, followLinks);
    if (files == nullptr) {
      std::fprintf(stderr, "Error traversing directories\n");
      return Status::Failure;
    }
    auto guard =
        makeScopeGuard([&] { UTIL_freeFileList(files, scratchBuffer); });
    if (numFiles == 0) {
      std::fprintf(stderr, "No files found\n");
      return Status::Failure;
    }
    inputFiles.resize(numFiles);
    std::copy(files, files + numFiles, inputFiles.begin());
  } else {
    inputFiles.resize(localInputFiles.size());
    std::copy(localInputFiles.begin(), localInputFiles.end(),
              inputFiles.begin());
  }
  localInputFiles.clear();
  assert(!inputFiles.empty());

  // If reading from standard input, default to standard output
  if (inputFiles[0] == kStdIn && outputFile.empty()) {
    assert(inputFiles.size() == 1);
    outputFile = "-";
  }

  if (inputFiles[0] == kStdIn && IS_CONSOLE(stdin)) {
    assert(inputFiles.size() == 1);
    std::fprintf(stderr, "Cannot read input from interactive console\n");
    return Status::Failure;
  }
  if (outputFile == "-" && IS_CONSOLE(stdout) && !(forceStdout && decompress)) {
    std::fprintf(stderr, "Will not write to console stdout unless -c or -f is "
                         "specified and decompressing\n");
    return Status::Failure;
  }

  // Check compression level
  {
    unsigned maxCLevel =
        ultra ? ZSTD_maxCLevel() : kMaxNonUltraCompressionLevel;
    if (compressionLevel > maxCLevel || compressionLevel == 0) {
      std::fprintf(stderr, "Invalid compression level %u.\n", compressionLevel);
      return Status::Failure;
    }
  }

  // Check that numThreads is set
  if (numThreads == 0) {
    std::fprintf(stderr, "Invalid arguments: # of threads not specified "
                         "and unable to determine hardware concurrency.\n");
    return Status::Failure;
  }

  // Modify verbosity
  // If we are piping input and output, turn off interaction
  if (inputFiles[0] == kStdIn && outputFile == kStdOut && verbosity == 2) {
    verbosity = 1;
  }
  // If we are in multi-file mode, turn off interaction
  if (inputFiles.size() > 1 && verbosity == 2) {
    verbosity = 1;
  }

  return Status::Success;
}

std::string Options::getOutputFile(const std::string &inputFile) const {
  if (!outputFile.empty()) {
    return outputFile;
  }
  // Attempt to add/remove zstd extension from the input file
  if (decompress) {
    int stemSize = inputFile.size() - kZstdExtension.size();
    if (stemSize > 0 && inputFile.substr(stemSize) == kZstdExtension) {
      return inputFile.substr(0, stemSize);
    } else {
      return "";
    }
  } else {
    return inputFile + kZstdExtension;
  }
}
}
