//===- llvm-mt.cpp - Merge .manifest files ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// Merge .manifest files.  This is intended to be a platform-independent port
// of Microsoft's mt.exe.
//
//===---------------------------------------------------------------------===//

#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/WindowsManifest/WindowsManifestMerger.h"

#include <system_error>

using namespace llvm;

namespace {

enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "Opts.inc"
#undef PREFIX

using namespace llvm::opt;
static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

class CvtResOptTable : public opt::GenericOptTable {
public:
  CvtResOptTable() : opt::GenericOptTable(InfoTable, true) {}
};
} // namespace

[[noreturn]] static void reportError(Twine Msg) {
  WithColor::error(errs(), "llvm-mt") << Msg << '\n';
  exit(1);
}

static void reportError(StringRef Input, std::error_code EC) {
  reportError(Twine(Input) + ": " + EC.message());
}

static void error(Error EC) {
  if (EC)
    handleAllErrors(std::move(EC), [&](const ErrorInfoBase &EI) {
      reportError(EI.message());
    });
}

int llvm_mt_main(int Argc, char **Argv, const llvm::ToolContext &) {
  CvtResOptTable T;
  unsigned MAI, MAC;
  ArrayRef<const char *> ArgsArr = ArrayRef(Argv + 1, Argc - 1);
  opt::InputArgList InputArgs = T.ParseArgs(ArgsArr, MAI, MAC);

  for (auto *Arg : InputArgs.filtered(OPT_INPUT)) {
    auto ArgString = Arg->getAsString(InputArgs);
    std::string Diag;
    raw_string_ostream OS(Diag);
    OS << "invalid option '" << ArgString << "'";

    std::string Nearest;
    if (T.findNearest(ArgString, Nearest) < 2)
      OS << ", did you mean '" << Nearest << "'?";

    reportError(OS.str());
  }

  for (auto &Arg : InputArgs) {
    if (Arg->getOption().matches(OPT_unsupported)) {
      outs() << "llvm-mt: ignoring unsupported '" << Arg->getOption().getName()
             << "' option\n";
    }
  }

  if (InputArgs.hasArg(OPT_help)) {
    T.printHelp(outs(), "llvm-mt [options] file...", "Manifest Tool", false);
    return 0;
  }

  std::vector<std::string> InputFiles = InputArgs.getAllArgValues(OPT_manifest);

  if (InputFiles.size() == 0) {
    reportError("no input file specified");
  }

  StringRef OutputFile;
  if (InputArgs.hasArg(OPT_out)) {
    OutputFile = InputArgs.getLastArgValue(OPT_out);
  } else if (InputFiles.size() == 1) {
    OutputFile = InputFiles[0];
  } else {
    reportError("no output file specified");
  }

  windows_manifest::WindowsManifestMerger Merger;

  for (const auto &File : InputFiles) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> ManifestOrErr =
        MemoryBuffer::getFile(File);
    if (!ManifestOrErr)
      reportError(File, ManifestOrErr.getError());
    error(Merger.merge(*ManifestOrErr.get()));
  }

  std::unique_ptr<MemoryBuffer> OutputBuffer = Merger.getMergedManifest();
  if (!OutputBuffer)
    reportError("empty manifest not written");

  int ExitCode = 0;
  if (InputArgs.hasArg(OPT_notify_update)) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> OutBuffOrErr =
        MemoryBuffer::getFile(OutputFile);
    // Assume if we couldn't open the output file then it doesn't exist meaning
    // there was a change.
    bool Same = false;
    if (OutBuffOrErr) {
      const std::unique_ptr<MemoryBuffer> &FileBuffer = *OutBuffOrErr;
      Same = std::equal(
          OutputBuffer->getBufferStart(), OutputBuffer->getBufferEnd(),
          FileBuffer->getBufferStart(), FileBuffer->getBufferEnd());
    }
    if (!Same) {
#if LLVM_ON_UNIX
      ExitCode = 0xbb;
#elif defined(_WIN32)
      ExitCode = 0x41020001;
#endif
    }
  }

  Expected<std::unique_ptr<FileOutputBuffer>> FileOrErr =
      FileOutputBuffer::create(OutputFile, OutputBuffer->getBufferSize());
  if (!FileOrErr)
    reportError(OutputFile, errorToErrorCode(FileOrErr.takeError()));
  std::unique_ptr<FileOutputBuffer> FileBuffer = std::move(*FileOrErr);
  std::copy(OutputBuffer->getBufferStart(), OutputBuffer->getBufferEnd(),
            FileBuffer->getBufferStart());
  error(FileBuffer->commit());
  return ExitCode;
}
