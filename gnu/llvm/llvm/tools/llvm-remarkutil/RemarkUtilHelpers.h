//===- RemarkUtilHelpers.h ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helpers for remark utilites
//
//===----------------------------------------------------------------------===//
#include "llvm-c/Remarks.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Remarks/Remark.h"
#include "llvm/Remarks/RemarkFormat.h"
#include "llvm/Remarks/RemarkParser.h"
#include "llvm/Remarks/YAMLRemarkSerializer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"

// Keep input + output help + names consistent across the various modes via a
// hideous macro.
#define INPUT_OUTPUT_COMMAND_LINE_OPTIONS(SUBOPT)                              \
  static cl::opt<std::string> InputFileName(cl::Positional, cl::init("-"),     \
                                            cl::desc("<input file>"),          \
                                            cl::sub(SUBOPT));                  \
  static cl::opt<std::string> OutputFileName(                                  \
      "o", cl::init("-"), cl::desc("Output"), cl::value_desc("filename"),      \
      cl::sub(SUBOPT));

// Keep Input format and names consistent accross the modes via a macro.
#define INPUT_FORMAT_COMMAND_LINE_OPTIONS(SUBOPT)                              \
  static cl::opt<Format> InputFormat(                                          \
      "parser", cl::desc("Input remark format to parse"),                      \
      cl::values(clEnumValN(Format::YAML, "yaml", "YAML"),                     \
                 clEnumValN(Format::Bitstream, "bitstream", "Bitstream")),     \
      cl::sub(SUBOPT));

#define DEBUG_LOC_INFO_COMMAND_LINE_OPTIONS(SUBOPT)                            \
  static cl::opt<bool> UseDebugLoc(                                            \
      "use-debug-loc",                                                         \
      cl::desc(                                                                \
          "Add debug loc information when generating tables for "              \
          "functions. The loc is represented as (path:line number:column "     \
          "number)"),                                                          \
      cl::init(false), cl::sub(SUBOPT));

namespace llvm {
namespace remarks {
Expected<std::unique_ptr<MemoryBuffer>>
getInputMemoryBuffer(StringRef InputFileName);
Expected<std::unique_ptr<ToolOutputFile>>
getOutputFileWithFlags(StringRef OutputFileName, sys::fs::OpenFlags Flags);
Expected<std::unique_ptr<ToolOutputFile>>
getOutputFileForRemarks(StringRef OutputFileName, Format OutputFormat);
} // namespace remarks
} // namespace llvm
