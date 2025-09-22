//===- split-file.cpp - Input splitting utility ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split input into multipe parts separated by regex '^(.|//)--- ' and extract
// the specified part.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include <string>
#include <system_error>

using namespace llvm;

static cl::OptionCategory cat("split-file Options");

static cl::opt<std::string> input(cl::Positional, cl::desc("filename"),
                                  cl::cat(cat));

static cl::opt<std::string> output(cl::Positional, cl::desc("directory"),
                                   cl::value_desc("directory"), cl::cat(cat));

static cl::opt<bool> leadingLines("leading-lines",
                                    cl::desc("Preserve line numbers"),
                                    cl::cat(cat));

static cl::opt<bool> noLeadingLines("no-leading-lines",
                                    cl::desc("Don't preserve line numbers (default)"),
                                    cl::cat(cat));

static StringRef toolName;
static int errorCount;

[[noreturn]] static void fatal(StringRef filename, const Twine &message) {
  if (filename.empty())
    WithColor::error(errs(), toolName) << message << '\n';
  else
    WithColor::error(errs(), toolName) << filename << ": " << message << '\n';
  exit(1);
}

static void error(StringRef filename, int64_t line, const Twine &message) {
  ++errorCount;
  errs() << filename << ':' << line << ": ";
  WithColor::error(errs()) << message << '\n';
}

namespace {
struct Part {
  const char *begin = nullptr;
  const char *end = nullptr;
  int64_t leadingLines = 0;
};
} // namespace

static int handle(MemoryBuffer &inputBuf, StringRef input) {
  DenseMap<StringRef, Part> partToBegin;
  StringRef lastPart, separator;
  StringRef EOL = inputBuf.getBuffer().detectEOL();
  for (line_iterator i(inputBuf, /*SkipBlanks=*/false, '\0'); !i.is_at_eof();) {
    const int64_t lineNo = i.line_number();
    const StringRef line = *i++;
    const size_t markerLen = line.starts_with("//") ? 6 : 5;
    if (!(line.size() >= markerLen &&
          line.substr(markerLen - 4).starts_with("--- ")))
      continue;
    separator = line.substr(0, markerLen);
    const StringRef partName = line.substr(markerLen);
    if (partName.empty()) {
      error(input, lineNo, "empty part name");
      continue;
    }
    if (isSpace(partName.front()) || isSpace(partName.back())) {
      error(input, lineNo, "part name cannot have leading or trailing space");
      continue;
    }

    auto res = partToBegin.try_emplace(partName);
    if (!res.second) {
      error(input, lineNo,
            "'" + separator + partName + "' occurs more than once");
      continue;
    }
    if (!lastPart.empty())
      partToBegin[lastPart].end = line.data();
    Part &cur = res.first->second;
    if (!i.is_at_eof())
      cur.begin = i->data();
    // If --leading-lines is specified, numEmptyLines is 0. Append newlines so
    // that the extracted part preserves line numbers.
    cur.leadingLines = leadingLines ? i.line_number() - 1 : 0;

    lastPart = partName;
  }
  if (lastPart.empty())
    fatal(input, "no part separator was found");
  if (errorCount)
    return 1;
  partToBegin[lastPart].end = inputBuf.getBufferEnd();

  std::vector<std::unique_ptr<ToolOutputFile>> outputFiles;
  SmallString<256> partPath;
  for (auto &keyValue : partToBegin) {
    partPath.clear();
    sys::path::append(partPath, output, keyValue.first);
    std::error_code ec =
        sys::fs::create_directories(sys::path::parent_path(partPath));
    if (ec)
      fatal(input, ec.message());
    auto f = std::make_unique<ToolOutputFile>(partPath.str(), ec,
                                              llvm::sys::fs::OF_None);
    if (!f)
      fatal(input, ec.message());

    Part &part = keyValue.second;
    for (int64_t i = 0; i != part.leadingLines; ++i)
      (*f).os() << EOL;
    if (part.begin)
      (*f).os().write(part.begin, part.end - part.begin);
    outputFiles.push_back(std::move(f));
  }

  for (std::unique_ptr<ToolOutputFile> &outputFile : outputFiles)
    outputFile->keep();
  return 0;
}

int main(int argc, const char **argv) {
  toolName = sys::path::stem(argv[0]);
  cl::HideUnrelatedOptions({&cat});
  cl::ParseCommandLineOptions(
      argc, argv,
      "Split input into multiple parts separated by regex '^(.|//)--- ' and "
      "extract the part specified by '^(.|//)--- <part>'\n",
      nullptr,
      /*EnvVar=*/nullptr,
      /*LongOptionsUseDoubleDash=*/true);

  if (input.empty())
    fatal("", "input filename is not specified");
  if (output.empty())
    fatal("", "output directory is not specified");
  ErrorOr<std::unique_ptr<MemoryBuffer>> bufferOrErr =
      MemoryBuffer::getFileOrSTDIN(input);
  if (std::error_code ec = bufferOrErr.getError())
    fatal(input, ec.message());

  // Delete output if it is a file or an empty directory, so that we can create
  // a directory.
  sys::fs::file_status status;
  if (std::error_code ec = sys::fs::status(output, status))
    if (ec.value() != static_cast<int>(std::errc::no_such_file_or_directory))
      fatal(output, ec.message());
  if (status.type() != sys::fs::file_type::file_not_found &&
      status.type() != sys::fs::file_type::directory_file &&
      status.type() != sys::fs::file_type::regular_file)
    fatal(output, "output cannot be a special file");
  if (std::error_code ec = sys::fs::remove(output, /*IgnoreNonExisting=*/true))
    if (ec.value() != static_cast<int>(std::errc::directory_not_empty) &&
        ec.value() != static_cast<int>(std::errc::file_exists))
      fatal(output, ec.message());
  return handle(**bufferOrErr, input);
}
