//===-- llvm-debuginfod-find.cpp - Simple CLI for libdebuginfod-client ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the llvm-debuginfod-find tool. This tool
/// queries the debuginfod servers in the DEBUGINFOD_URLS environment
/// variable (delimited by space (" ")) for the executable,
/// debuginfo, or specified source file of the binary matching the
/// given build-id.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringExtras.h"
#include "llvm/Debuginfod/BuildIDFetcher.h"
#include "llvm/Debuginfod/Debuginfod.h"
#include "llvm/Debuginfod/HTTPClient.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"

using namespace llvm;

cl::OptionCategory DebuginfodFindCategory("llvm-debuginfod-find Options");

cl::opt<std::string> InputBuildID(cl::Positional, cl::Required,
                                  cl::desc("<input build_id>"), cl::init("-"),
                                  cl::cat(DebuginfodFindCategory));

static cl::opt<bool>
    FetchExecutable("executable", cl::init(false),
                    cl::desc("If set, fetch a binary file associated with this "
                             "build id, containing the executable sections."),
                    cl::cat(DebuginfodFindCategory));

static cl::opt<bool>
    FetchDebuginfo("debuginfo", cl::init(false),
                   cl::desc("If set, fetch a binary file associated with this "
                            "build id, containing the debuginfo sections."),
                   cl::cat(DebuginfodFindCategory));

static cl::opt<std::string> FetchSource(
    "source", cl::init(""),
    cl::desc("Fetch a source file associated with this build id, which is at "
             "this relative path relative to the compilation directory."),
    cl::cat(DebuginfodFindCategory));

static cl::opt<bool>
    DumpToStdout("dump", cl::init(false),
                 cl::desc("If set, dumps the contents of the fetched artifact "
                          "to standard output. Otherwise, dumps the absolute "
                          "path to the cached artifact on disk."),
                 cl::cat(DebuginfodFindCategory));

static cl::list<std::string> DebugFileDirectory(
    "debug-file-directory",
    cl::desc("Path to directory where to look for debug files."),
    cl::cat(DebuginfodFindCategory));

[[noreturn]] static void helpExit() {
  errs() << "Must specify exactly one of --executable, "
            "--source=/path/to/file, or --debuginfo.";
  exit(1);
}

ExitOnError ExitOnErr;

static std::string fetchDebugInfo(object::BuildIDRef BuildID);

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  HTTPClient::initialize();

  cl::HideUnrelatedOptions({&DebuginfodFindCategory});
  cl::ParseCommandLineOptions(
      argc, argv,
      "llvm-debuginfod-find: Fetch debuginfod artifacts\n\n"
      "This program is a frontend to the debuginfod client library. The cache "
      "directory, request timeout (in seconds), and debuginfod server urls are "
      "set by these environment variables:\n"
      "DEBUGINFOD_CACHE_PATH (default set by sys::path::cache_directory)\n"
      "DEBUGINFOD_TIMEOUT (defaults to 90s)\n"
      "DEBUGINFOD_URLS=[comma separated URLs] (defaults to empty)\n");

  if (FetchExecutable + FetchDebuginfo + (FetchSource != "") != 1)
    helpExit();

  std::string IDString;
  if (!tryGetFromHex(InputBuildID, IDString)) {
    errs() << "Build ID " << InputBuildID << " is not a hex string.\n";
    exit(1);
  }
  object::BuildID ID(IDString.begin(), IDString.end());

  std::string Path;
  if (FetchSource != "")
    Path = ExitOnErr(getCachedOrDownloadSource(ID, FetchSource));
  else if (FetchExecutable)
    Path = ExitOnErr(getCachedOrDownloadExecutable(ID));
  else if (FetchDebuginfo)
    Path = fetchDebugInfo(ID);
  else
    llvm_unreachable("We have already checked that exactly one of the above "
                     "conditions is true.");

  if (DumpToStdout) {
    // Print the contents of the artifact.
    ErrorOr<std::unique_ptr<MemoryBuffer>> Buf = MemoryBuffer::getFile(
        Path, /*IsText=*/false, /*RequiresNullTerminator=*/false);
    ExitOnErr(errorCodeToError(Buf.getError()));
    outs() << Buf.get()->getBuffer();
  } else
    // Print the path to the cached artifact file.
    outs() << Path << "\n";
}

// Find a debug file in local build ID directories and via debuginfod.
std::string fetchDebugInfo(object::BuildIDRef BuildID) {
  if (std::optional<std::string> Path =
          DebuginfodFetcher(DebugFileDirectory).fetch(BuildID))
    return *Path;
  errs() << "Build ID " << llvm::toHex(BuildID, /*Lowercase=*/true)
         << " could not be found.\n";
  exit(1);
}
