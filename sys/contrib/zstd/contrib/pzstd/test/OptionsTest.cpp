/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#include "Options.h"

#include <array>
#include <gtest/gtest.h>

using namespace pzstd;

namespace pzstd {
bool operator==(const Options &lhs, const Options &rhs) {
  return lhs.numThreads == rhs.numThreads &&
         lhs.maxWindowLog == rhs.maxWindowLog &&
         lhs.compressionLevel == rhs.compressionLevel &&
         lhs.decompress == rhs.decompress && lhs.inputFiles == rhs.inputFiles &&
         lhs.outputFile == rhs.outputFile && lhs.overwrite == rhs.overwrite &&
         lhs.keepSource == rhs.keepSource && lhs.writeMode == rhs.writeMode &&
         lhs.checksum == rhs.checksum && lhs.verbosity == rhs.verbosity;
}

std::ostream &operator<<(std::ostream &out, const Options &opt) {
  out << "{";
  {
    out << "\n\t"
        << "numThreads: " << opt.numThreads;
    out << ",\n\t"
        << "maxWindowLog: " << opt.maxWindowLog;
    out << ",\n\t"
        << "compressionLevel: " << opt.compressionLevel;
    out << ",\n\t"
        << "decompress: " << opt.decompress;
    out << ",\n\t"
        << "inputFiles: {";
    {
      bool first = true;
      for (const auto &file : opt.inputFiles) {
        if (!first) {
          out << ",";
        }
        first = false;
        out << "\n\t\t" << file;
      }
    }
    out << "\n\t}";
    out << ",\n\t"
        << "outputFile: " << opt.outputFile;
    out << ",\n\t"
        << "overwrite: " << opt.overwrite;
    out << ",\n\t"
        << "keepSource: " << opt.keepSource;
    out << ",\n\t"
        << "writeMode: " << static_cast<int>(opt.writeMode);
    out << ",\n\t"
        << "checksum: " << opt.checksum;
    out << ",\n\t"
        << "verbosity: " << opt.verbosity;
  }
  out << "\n}";
  return out;
}
}

namespace {
#ifdef _WIN32
const char nullOutput[] = "nul";
#else
const char nullOutput[] = "/dev/null";
#endif

constexpr auto autoMode = Options::WriteMode::Auto;
} // anonymous namespace

#define EXPECT_SUCCESS(...) EXPECT_EQ(Options::Status::Success, __VA_ARGS__)
#define EXPECT_FAILURE(...) EXPECT_EQ(Options::Status::Failure, __VA_ARGS__)
#define EXPECT_MESSAGE(...) EXPECT_EQ(Options::Status::Message, __VA_ARGS__)

template <typename... Args>
std::array<const char *, sizeof...(Args) + 1> makeArray(Args... args) {
  return {{nullptr, args...}};
}

TEST(Options, ValidInputs) {
  {
    Options options;
    auto args = makeArray("--processes", "5", "-o", "x", "y", "-f");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    Options expected = {5,    23,   3,        false, {"y"}, "x",
                        true, true, autoMode, true,  2};
    EXPECT_EQ(expected, options);
  }
  {
    Options options;
    auto args = makeArray("-p", "1", "input", "-19");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    Options expected = {1,     23,   19,       false, {"input"}, "",
                        false, true, autoMode, true,  2};
    EXPECT_EQ(expected, options);
  }
  {
    Options options;
    auto args =
        makeArray("--ultra", "-22", "-p", "1", "-o", "x", "-d", "x.zst", "-f");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    Options expected = {1,    0,    22,       true, {"x.zst"}, "x",
                        true, true, autoMode, true, 2};
    EXPECT_EQ(expected, options);
  }
  {
    Options options;
    auto args = makeArray("--processes", "100", "hello.zst", "--decompress",
                          "--force");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    Options expected = {100,  23,       3,    true, {"hello.zst"}, "", true,
                        true, autoMode, true, 2};
    EXPECT_EQ(expected, options);
  }
  {
    Options options;
    auto args = makeArray("x", "-dp", "1", "-c");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    Options expected = {1,     23,   3,        true, {"x"}, "-",
                        false, true, autoMode, true, 2};
    EXPECT_EQ(expected, options);
  }
  {
    Options options;
    auto args = makeArray("x", "-dp", "1", "--stdout");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    Options expected = {1,     23,   3,        true, {"x"}, "-",
                        false, true, autoMode, true, 2};
    EXPECT_EQ(expected, options);
  }
  {
    Options options;
    auto args = makeArray("-p", "1", "x", "-5", "-fo", "-", "--ultra", "-d");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    Options expected = {1,    0,    5,        true, {"x"}, "-",
                        true, true, autoMode, true, 2};
    EXPECT_EQ(expected, options);
  }
  {
    Options options;
    auto args = makeArray("silesia.tar", "-o", "silesia.tar.pzstd", "-p", "2");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    Options expected = {2,
                        23,
                        3,
                        false,
                        {"silesia.tar"},
                        "silesia.tar.pzstd",
                        false,
                        true,
                        autoMode,
                        true,
                        2};
    EXPECT_EQ(expected, options);
  }
  {
    Options options;
    auto args = makeArray("x", "-p", "1");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("x", "-p", "1");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
  }
}

TEST(Options, GetOutputFile) {
  {
    Options options;
    auto args = makeArray("x");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ("x.zst", options.getOutputFile(options.inputFiles[0]));
  }
  {
    Options options;
    auto args = makeArray("x", "y", "-o", nullOutput);
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(nullOutput, options.getOutputFile(options.inputFiles[0]));
  }
  {
    Options options;
    auto args = makeArray("x.zst", "-do", nullOutput);
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(nullOutput, options.getOutputFile(options.inputFiles[0]));
  }
  {
    Options options;
    auto args = makeArray("x.zst", "-d");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ("x", options.getOutputFile(options.inputFiles[0]));
  }
  {
    Options options;
    auto args = makeArray("xzst", "-d");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ("", options.getOutputFile(options.inputFiles[0]));
  }
  {
    Options options;
    auto args = makeArray("xzst", "-doxx");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ("xx", options.getOutputFile(options.inputFiles[0]));
  }
}

TEST(Options, MultipleFiles) {
  {
    Options options;
    auto args = makeArray("x", "y", "z");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    Options expected;
    expected.inputFiles = {"x", "y", "z"};
    expected.verbosity = 1;
    EXPECT_EQ(expected, options);
  }
  {
    Options options;
    auto args = makeArray("x", "y", "z", "-o", nullOutput);
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    Options expected;
    expected.inputFiles = {"x", "y", "z"};
    expected.outputFile = nullOutput;
    expected.verbosity = 1;
    EXPECT_EQ(expected, options);
  }
  {
    Options options;
    auto args = makeArray("x", "y", "-o-");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("x", "y", "-o", "file");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("-qqvd12qp4", "-f", "x", "--", "--rm", "-c");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    Options expected = {4,  23,   12,   true,     {"x", "--rm", "-c"},
                        "", true, true, autoMode, true,
                        0};
    EXPECT_EQ(expected, options);
  }
}

TEST(Options, NumThreads) {
  {
    Options options;
    auto args = makeArray("x", "-dfo", "-");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("x", "-p", "0", "-fo", "-");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("-f", "-p", "-o", "-");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
}

TEST(Options, BadCompressionLevel) {
  {
    Options options;
    auto args = makeArray("x", "-20");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("x", "--ultra", "-23");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("x", "--1"); // negative 1?
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
}

TEST(Options, InvalidOption) {
  {
    Options options;
    auto args = makeArray("x", "-x");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
}

TEST(Options, BadOutputFile) {
  {
    Options options;
    auto args = makeArray("notzst", "-d", "-p", "1");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ("", options.getOutputFile(options.inputFiles.front()));
  }
}

TEST(Options, BadOptionsWithArguments) {
  {
    Options options;
    auto args = makeArray("x", "-pf");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("x", "-p", "10f");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("x", "-p");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("x", "-o");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("x", "-o");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
}

TEST(Options, KeepSource) {
  {
    Options options;
    auto args = makeArray("x", "--rm", "-k");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(true, options.keepSource);
  }
  {
    Options options;
    auto args = makeArray("x", "--rm", "--keep");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(true, options.keepSource);
  }
  {
    Options options;
    auto args = makeArray("x");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(true, options.keepSource);
  }
  {
    Options options;
    auto args = makeArray("x", "--rm");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(false, options.keepSource);
  }
}

TEST(Options, Verbosity) {
  {
    Options options;
    auto args = makeArray("x");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(2, options.verbosity);
  }
  {
    Options options;
    auto args = makeArray("--quiet", "-qq", "x");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(-1, options.verbosity);
  }
  {
    Options options;
    auto args = makeArray("x", "y");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(1, options.verbosity);
  }
  {
    Options options;
    auto args = makeArray("--", "x", "y");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(1, options.verbosity);
  }
  {
    Options options;
    auto args = makeArray("-qv", "x", "y");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(1, options.verbosity);
  }
  {
    Options options;
    auto args = makeArray("-v", "x", "y");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(3, options.verbosity);
  }
  {
    Options options;
    auto args = makeArray("-v", "x");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(3, options.verbosity);
  }
}

TEST(Options, TestMode) {
  {
    Options options;
    auto args = makeArray("x", "-t");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(true, options.keepSource);
    EXPECT_EQ(true, options.decompress);
    EXPECT_EQ(nullOutput, options.outputFile);
  }
  {
    Options options;
    auto args = makeArray("x", "--test", "--rm", "-ohello");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(true, options.keepSource);
    EXPECT_EQ(true, options.decompress);
    EXPECT_EQ(nullOutput, options.outputFile);
  }
}

TEST(Options, Checksum) {
  {
    Options options;
    auto args = makeArray("x.zst", "--no-check", "-Cd");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(true, options.checksum);
  }
  {
    Options options;
    auto args = makeArray("x");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(true, options.checksum);
  }
  {
    Options options;
    auto args = makeArray("x", "--no-check", "--check");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(true, options.checksum);
  }
  {
    Options options;
    auto args = makeArray("x", "--no-check");
    EXPECT_SUCCESS(options.parse(args.size(), args.data()));
    EXPECT_EQ(false, options.checksum);
  }
}

TEST(Options, InputFiles) {
  {
    Options options;
    auto args = makeArray("-cd");
    options.parse(args.size(), args.data());
    EXPECT_EQ(1, options.inputFiles.size());
    EXPECT_EQ("-", options.inputFiles[0]);
    EXPECT_EQ("-", options.outputFile);
  }
  {
    Options options;
    auto args = makeArray();
    options.parse(args.size(), args.data());
    EXPECT_EQ(1, options.inputFiles.size());
    EXPECT_EQ("-", options.inputFiles[0]);
    EXPECT_EQ("-", options.outputFile);
  }
  {
    Options options;
    auto args = makeArray("-d");
    options.parse(args.size(), args.data());
    EXPECT_EQ(1, options.inputFiles.size());
    EXPECT_EQ("-", options.inputFiles[0]);
    EXPECT_EQ("-", options.outputFile);
  }
  {
    Options options;
    auto args = makeArray("x", "-");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
}

TEST(Options, InvalidOptions) {
  {
    Options options;
    auto args = makeArray("-ibasdf");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("- ");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("-n15");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("-0", "x");
    EXPECT_FAILURE(options.parse(args.size(), args.data()));
  }
}

TEST(Options, Extras) {
  {
    Options options;
    auto args = makeArray("-h");
    EXPECT_MESSAGE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("-H");
    EXPECT_MESSAGE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("-V");
    EXPECT_MESSAGE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("--help");
    EXPECT_MESSAGE(options.parse(args.size(), args.data()));
  }
  {
    Options options;
    auto args = makeArray("--version");
    EXPECT_MESSAGE(options.parse(args.size(), args.data()));
  }
}
