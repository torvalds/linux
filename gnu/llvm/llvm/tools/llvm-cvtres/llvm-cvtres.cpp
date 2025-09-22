//===- llvm-cvtres.cpp - Serialize .res files into .obj ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Serialize .res files into .obj files.  This is intended to be a
// platform-independent port of Microsoft's cvtres.exe.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/WindowsMachineFlag.h"
#include "llvm/Object/WindowsResource.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/BinaryStreamError.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

#include <system_error>

using namespace llvm;
using namespace object;

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
}

[[noreturn]] static void reportError(Twine Msg) {
  errs() << Msg;
  exit(1);
}

static void reportError(StringRef Input, std::error_code EC) {
  reportError(Twine(Input) + ": " + EC.message() + ".\n");
}

static void error(StringRef Input, Error EC) {
  if (!EC)
    return;
  handleAllErrors(std::move(EC), [&](const ErrorInfoBase &EI) {
    reportError(Twine(Input) + ": " + EI.message() + ".\n");
  });
}

static void error(Error EC) {
  if (!EC)
    return;
  handleAllErrors(std::move(EC),
                  [&](const ErrorInfoBase &EI) { reportError(EI.message()); });
}

static uint32_t getTime() {
  std::time_t Now = time(nullptr);
  if (Now < 0 || !isUInt<32>(Now))
    return UINT32_MAX;
  return static_cast<uint32_t>(Now);
}

template <typename T> T error(Expected<T> EC) {
  if (!EC)
    error(EC.takeError());
  return std::move(EC.get());
}

template <typename T> T error(StringRef Input, Expected<T> EC) {
  if (!EC)
    error(Input, EC.takeError());
  return std::move(EC.get());
}

template <typename T> T error(StringRef Input, ErrorOr<T> &&EC) {
  return error(Input, errorOrToExpected(std::move(EC)));
}

int main(int Argc, const char **Argv) {
  InitLLVM X(Argc, Argv);

  CvtResOptTable T;
  unsigned MAI, MAC;
  ArrayRef<const char *> ArgsArr = ArrayRef(Argv + 1, Argc - 1);
  opt::InputArgList InputArgs = T.ParseArgs(ArgsArr, MAI, MAC);

  if (InputArgs.hasArg(OPT_HELP)) {
    T.printHelp(outs(), "llvm-cvtres [options] file...", "Resource Converter");
    return 0;
  }

  bool Verbose = InputArgs.hasArg(OPT_VERBOSE);

  COFF::MachineTypes MachineType;

  if (opt::Arg *Arg = InputArgs.getLastArg(OPT_MACHINE)) {
    MachineType = getMachineType(Arg->getValue());
    if (MachineType == COFF::IMAGE_FILE_MACHINE_UNKNOWN) {
      reportError(Twine("Unsupported machine architecture ") + Arg->getValue() +
                  "\n");
    }
  } else {
    if (Verbose)
      outs() << "Machine architecture not specified; assumed X64.\n";
    MachineType = COFF::IMAGE_FILE_MACHINE_AMD64;
  }

  std::vector<std::string> InputFiles = InputArgs.getAllArgValues(OPT_INPUT);

  if (InputFiles.size() == 0) {
    reportError("No input file specified.\n");
  }

  SmallString<128> OutputFile;

  if (opt::Arg *Arg = InputArgs.getLastArg(OPT_OUT)) {
    OutputFile = Arg->getValue();
  } else {
    OutputFile = sys::path::filename(StringRef(InputFiles[0]));
    sys::path::replace_extension(OutputFile, ".obj");
  }

  uint32_t DateTimeStamp;
  if (llvm::opt::Arg *Arg = InputArgs.getLastArg(OPT_TIMESTAMP)) {
    StringRef Value(Arg->getValue());
    if (Value.getAsInteger(0, DateTimeStamp))
      reportError(Twine("invalid timestamp: ") + Value +
            ".  Expected 32-bit integer\n");
  } else {
    DateTimeStamp = getTime();
  }

  if (Verbose)
    outs() << "Machine: " << machineToStr(MachineType) << '\n';

  WindowsResourceParser Parser;

  for (const auto &File : InputFiles) {
    std::unique_ptr<MemoryBuffer> Buffer = error(
        File, MemoryBuffer::getFileOrSTDIN(File, /*IsText=*/false,
                                           /*RequiresNullTerminator=*/false));
    file_magic Type = identify_magic(Buffer->getMemBufferRef().getBuffer());
    if (Type != file_magic::windows_resource)
      reportError(File + ": unrecognized file format.\n");
    std::unique_ptr<WindowsResource> Binary = error(
        File,
        WindowsResource::createWindowsResource(Buffer->getMemBufferRef()));

    WindowsResource *RF = Binary.get();

    if (Verbose) {
      int EntryNumber = 0;
      ResourceEntryRef Entry = error(RF->getHeadEntry());
      bool End = false;
      while (!End) {
        error(Entry.moveNext(End));
        EntryNumber++;
      }
      outs() << "Number of resources: " << EntryNumber << "\n";
    }

    std::vector<std::string> Duplicates;
    error(Parser.parse(RF, Duplicates));
    for (const auto& DupeDiag : Duplicates)
      reportError(DupeDiag);
  }

  if (Verbose) {
    Parser.printTree(outs());
  }

  std::unique_ptr<MemoryBuffer> OutputBuffer =
      error(llvm::object::writeWindowsResourceCOFF(MachineType, Parser,
                                                   DateTimeStamp));
  auto FileOrErr =
      FileOutputBuffer::create(OutputFile, OutputBuffer->getBufferSize());
  if (!FileOrErr)
    reportError(OutputFile, errorToErrorCode(FileOrErr.takeError()));
  std::unique_ptr<FileOutputBuffer> FileBuffer = std::move(*FileOrErr);
  std::copy(OutputBuffer->getBufferStart(), OutputBuffer->getBufferEnd(),
            FileBuffer->getBufferStart());
  error(FileBuffer->commit());

  if (Verbose) {
    std::unique_ptr<MemoryBuffer> Buffer =
        error(OutputFile,
              MemoryBuffer::getFileOrSTDIN(OutputFile, /*IsText=*/false,
                                           /*RequiresNullTerminator=*/false));

    ScopedPrinter W(errs());
    W.printBinaryBlock("Output File Raw Data",
                       Buffer->getMemBufferRef().getBuffer());
  }

  return 0;
}
