//===-- clang-offload-packager/ClangOffloadPackager.cpp - file bundler ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This tool takes several device object files and bundles them into a single
// binary image using a custom binary format. This is intended to be used to
// embed many device files into an application to create a fat binary.
//
//===---------------------------------------------------------------------===//

#include "clang/Basic/Version.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace llvm::object;

static cl::opt<bool> Help("h", cl::desc("Alias for -help"), cl::Hidden);

static cl::OptionCategory
    ClangOffloadPackagerCategory("clang-offload-packager options");

static cl::opt<std::string> OutputFile("o", cl::desc("Write output to <file>."),
                                       cl::value_desc("file"),
                                       cl::cat(ClangOffloadPackagerCategory));

static cl::opt<std::string> InputFile(cl::Positional,
                                      cl::desc("Extract from <file>."),
                                      cl::value_desc("file"),
                                      cl::cat(ClangOffloadPackagerCategory));

static cl::list<std::string>
    DeviceImages("image",
                 cl::desc("List of key and value arguments. Required keywords "
                          "are 'file' and 'triple'."),
                 cl::value_desc("<key>=<value>,..."),
                 cl::cat(ClangOffloadPackagerCategory));

static cl::opt<bool>
    CreateArchive("archive",
                  cl::desc("Write extracted files to a static archive"),
                  cl::cat(ClangOffloadPackagerCategory));

/// Path of the current binary.
static const char *PackagerExecutable;

static void PrintVersion(raw_ostream &OS) {
  OS << clang::getClangToolFullVersion("clang-offload-packager") << '\n';
}

// Get a map containing all the arguments for the image. Repeated arguments will
// be placed in a comma separated list.
static DenseMap<StringRef, StringRef> getImageArguments(StringRef Image,
                                                        StringSaver &Saver) {
  DenseMap<StringRef, StringRef> Args;
  for (StringRef Arg : llvm::split(Image, ",")) {
    auto [Key, Value] = Arg.split("=");
    if (Args.count(Key))
      Args[Key] = Saver.save(Args[Key] + "," + Value);
    else
      Args[Key] = Value;
  }

  return Args;
}

static Error writeFile(StringRef Filename, StringRef Data) {
  Expected<std::unique_ptr<FileOutputBuffer>> OutputOrErr =
      FileOutputBuffer::create(Filename, Data.size());
  if (!OutputOrErr)
    return OutputOrErr.takeError();
  std::unique_ptr<FileOutputBuffer> Output = std::move(*OutputOrErr);
  llvm::copy(Data, Output->getBufferStart());
  if (Error E = Output->commit())
    return E;
  return Error::success();
}

static Error bundleImages() {
  SmallVector<char, 1024> BinaryData;
  raw_svector_ostream OS(BinaryData);
  for (StringRef Image : DeviceImages) {
    BumpPtrAllocator Alloc;
    StringSaver Saver(Alloc);
    DenseMap<StringRef, StringRef> Args = getImageArguments(Image, Saver);

    if (!Args.count("triple") || !Args.count("file"))
      return createStringError(
          inconvertibleErrorCode(),
          "'file' and 'triple' are required image arguments");

    // Permit using multiple instances of `file` in a single string.
    for (auto &File : llvm::split(Args["file"], ",")) {
      OffloadBinary::OffloadingImage ImageBinary{};
      std::unique_ptr<llvm::MemoryBuffer> DeviceImage;

      llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> ObjectOrErr =
          llvm::MemoryBuffer::getFileOrSTDIN(File);
      if (std::error_code EC = ObjectOrErr.getError())
        return errorCodeToError(EC);

      // Clang uses the '.o' suffix for LTO bitcode.
      if (identify_magic((*ObjectOrErr)->getBuffer()) == file_magic::bitcode)
        ImageBinary.TheImageKind = object::IMG_Bitcode;
      else
        ImageBinary.TheImageKind =
            getImageKind(sys::path::extension(File).drop_front());
      ImageBinary.Image = std::move(*ObjectOrErr);
      for (const auto &[Key, Value] : Args) {
        if (Key == "kind") {
          ImageBinary.TheOffloadKind = getOffloadKind(Value);
        } else if (Key != "file") {
          ImageBinary.StringData[Key] = Value;
        }
      }
      llvm::SmallString<0> Buffer = OffloadBinary::write(ImageBinary);
      if (Buffer.size() % OffloadBinary::getAlignment() != 0)
        return createStringError(inconvertibleErrorCode(),
                                 "Offload binary has invalid size alignment");
      OS << Buffer;
    }
  }

  if (Error E = writeFile(OutputFile,
                          StringRef(BinaryData.begin(), BinaryData.size())))
    return E;
  return Error::success();
}

static Error unbundleImages() {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getFileOrSTDIN(InputFile);
  if (std::error_code EC = BufferOrErr.getError())
    return createFileError(InputFile, EC);
  std::unique_ptr<MemoryBuffer> Buffer = std::move(*BufferOrErr);

  // This data can be misaligned if extracted from an archive.
  if (!isAddrAligned(Align(OffloadBinary::getAlignment()),
                     Buffer->getBufferStart()))
    Buffer = MemoryBuffer::getMemBufferCopy(Buffer->getBuffer(),
                                            Buffer->getBufferIdentifier());

  SmallVector<OffloadFile> Binaries;
  if (Error Err = extractOffloadBinaries(*Buffer, Binaries))
    return Err;

  // Try to extract each device image specified by the user from the input file.
  for (StringRef Image : DeviceImages) {
    BumpPtrAllocator Alloc;
    StringSaver Saver(Alloc);
    auto Args = getImageArguments(Image, Saver);

    SmallVector<const OffloadBinary *> Extracted;
    for (const OffloadFile &File : Binaries) {
      const auto *Binary = File.getBinary();
      // We handle the 'file' and 'kind' identifiers differently.
      bool Match = llvm::all_of(Args, [&](auto &Arg) {
        const auto [Key, Value] = Arg;
        if (Key == "file")
          return true;
        if (Key == "kind")
          return Binary->getOffloadKind() == getOffloadKind(Value);
        return Binary->getString(Key) == Value;
      });
      if (Match)
        Extracted.push_back(Binary);
    }

    if (Extracted.empty())
      continue;

    if (CreateArchive) {
      if (!Args.count("file"))
        return createStringError(inconvertibleErrorCode(),
                                 "Image must have a 'file' argument.");

      SmallVector<NewArchiveMember> Members;
      for (const OffloadBinary *Binary : Extracted)
        Members.emplace_back(MemoryBufferRef(
            Binary->getImage(),
            Binary->getMemoryBufferRef().getBufferIdentifier()));

      if (Error E = writeArchive(
              Args["file"], Members, SymtabWritingMode::NormalSymtab,
              Archive::getDefaultKind(), true, false, nullptr))
        return E;
    } else if (Args.count("file")) {
      if (Extracted.size() > 1)
        WithColor::warning(errs(), PackagerExecutable)
            << "Multiple inputs match to a single file, '" << Args["file"]
            << "'\n";
      if (Error E = writeFile(Args["file"], Extracted.back()->getImage()))
        return E;
    } else {
      uint64_t Idx = 0;
      for (const OffloadBinary *Binary : Extracted) {
        StringRef Filename =
            Saver.save(sys::path::stem(InputFile) + "-" + Binary->getTriple() +
                       "-" + Binary->getArch() + "." + std::to_string(Idx++) +
                       "." + getImageKindName(Binary->getImageKind()));
        if (Error E = writeFile(Filename, Binary->getImage()))
          return E;
      }
    }
  }

  return Error::success();
}

int main(int argc, const char **argv) {
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  cl::HideUnrelatedOptions(ClangOffloadPackagerCategory);
  cl::SetVersionPrinter(PrintVersion);
  cl::ParseCommandLineOptions(
      argc, argv,
      "A utility for bundling several object files into a single binary.\n"
      "The output binary can then be embedded into the host section table\n"
      "to create a fatbinary containing offloading code.\n");

  if (Help) {
    cl::PrintHelpMessage();
    return EXIT_SUCCESS;
  }

  PackagerExecutable = argv[0];
  auto reportError = [argv](Error E) {
    logAllUnhandledErrors(std::move(E), WithColor::error(errs(), argv[0]));
    return EXIT_FAILURE;
  };

  if (!InputFile.empty() && !OutputFile.empty())
    return reportError(
        createStringError(inconvertibleErrorCode(),
                          "Packaging to an output file and extracting from an "
                          "input file are mutually exclusive."));

  if (!OutputFile.empty()) {
    if (Error Err = bundleImages())
      return reportError(std::move(Err));
  } else if (!InputFile.empty()) {
    if (Error Err = unbundleImages())
      return reportError(std::move(Err));
  }

  return EXIT_SUCCESS;
}
