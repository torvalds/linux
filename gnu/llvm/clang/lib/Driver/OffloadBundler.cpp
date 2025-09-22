//===- OffloadBundler.cpp - File Bundling and Unbundling ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements an offload bundling API that bundles different files
/// that relate with the same source code but different targets into a single
/// one. Also the implements the opposite functionality, i.e. unbundle files
/// previous created by this API.
///
//===----------------------------------------------------------------------===//

#include "clang/Driver/OffloadBundler.h"
#include "clang/Basic/Cuda.h"
#include "clang/Basic/TargetID.h"
#include "clang/Basic/Version.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <forward_list>
#include <llvm/Support/Process.h>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <utility>

using namespace llvm;
using namespace llvm::object;
using namespace clang;

static llvm::TimerGroup
    ClangOffloadBundlerTimerGroup("Clang Offload Bundler Timer Group",
                                  "Timer group for clang offload bundler");

/// Magic string that marks the existence of offloading data.
#define OFFLOAD_BUNDLER_MAGIC_STR "__CLANG_OFFLOAD_BUNDLE__"

OffloadTargetInfo::OffloadTargetInfo(const StringRef Target,
                                     const OffloadBundlerConfig &BC)
    : BundlerConfig(BC) {

  // TODO: Add error checking from ClangOffloadBundler.cpp
  auto TargetFeatures = Target.split(':');
  auto TripleOrGPU = TargetFeatures.first.rsplit('-');

  if (clang::StringToOffloadArch(TripleOrGPU.second) !=
      clang::OffloadArch::UNKNOWN) {
    auto KindTriple = TripleOrGPU.first.split('-');
    this->OffloadKind = KindTriple.first;

    // Enforce optional env field to standardize bundles
    llvm::Triple t = llvm::Triple(KindTriple.second);
    this->Triple = llvm::Triple(t.getArchName(), t.getVendorName(),
                                t.getOSName(), t.getEnvironmentName());

    this->TargetID = Target.substr(Target.find(TripleOrGPU.second));
  } else {
    auto KindTriple = TargetFeatures.first.split('-');
    this->OffloadKind = KindTriple.first;

    // Enforce optional env field to standardize bundles
    llvm::Triple t = llvm::Triple(KindTriple.second);
    this->Triple = llvm::Triple(t.getArchName(), t.getVendorName(),
                                t.getOSName(), t.getEnvironmentName());

    this->TargetID = "";
  }
}

bool OffloadTargetInfo::hasHostKind() const {
  return this->OffloadKind == "host";
}

bool OffloadTargetInfo::isOffloadKindValid() const {
  return OffloadKind == "host" || OffloadKind == "openmp" ||
         OffloadKind == "hip" || OffloadKind == "hipv4";
}

bool OffloadTargetInfo::isOffloadKindCompatible(
    const StringRef TargetOffloadKind) const {
  if ((OffloadKind == TargetOffloadKind) ||
      (OffloadKind == "hip" && TargetOffloadKind == "hipv4") ||
      (OffloadKind == "hipv4" && TargetOffloadKind == "hip"))
    return true;

  if (BundlerConfig.HipOpenmpCompatible) {
    bool HIPCompatibleWithOpenMP = OffloadKind.starts_with_insensitive("hip") &&
                                   TargetOffloadKind == "openmp";
    bool OpenMPCompatibleWithHIP =
        OffloadKind == "openmp" &&
        TargetOffloadKind.starts_with_insensitive("hip");
    return HIPCompatibleWithOpenMP || OpenMPCompatibleWithHIP;
  }
  return false;
}

bool OffloadTargetInfo::isTripleValid() const {
  return !Triple.str().empty() && Triple.getArch() != Triple::UnknownArch;
}

bool OffloadTargetInfo::operator==(const OffloadTargetInfo &Target) const {
  return OffloadKind == Target.OffloadKind &&
         Triple.isCompatibleWith(Target.Triple) && TargetID == Target.TargetID;
}

std::string OffloadTargetInfo::str() const {
  return Twine(OffloadKind + "-" + Triple.str() + "-" + TargetID).str();
}

static StringRef getDeviceFileExtension(StringRef Device,
                                        StringRef BundleFileName) {
  if (Device.contains("gfx"))
    return ".bc";
  if (Device.contains("sm_"))
    return ".cubin";
  return sys::path::extension(BundleFileName);
}

static std::string getDeviceLibraryFileName(StringRef BundleFileName,
                                            StringRef Device) {
  StringRef LibName = sys::path::stem(BundleFileName);
  StringRef Extension = getDeviceFileExtension(Device, BundleFileName);

  std::string Result;
  Result += LibName;
  Result += Extension;
  return Result;
}

namespace {
/// Generic file handler interface.
class FileHandler {
public:
  struct BundleInfo {
    StringRef BundleID;
  };

  FileHandler() {}

  virtual ~FileHandler() {}

  /// Update the file handler with information from the header of the bundled
  /// file.
  virtual Error ReadHeader(MemoryBuffer &Input) = 0;

  /// Read the marker of the next bundled to be read in the file. The bundle
  /// name is returned if there is one in the file, or `std::nullopt` if there
  /// are no more bundles to be read.
  virtual Expected<std::optional<StringRef>>
  ReadBundleStart(MemoryBuffer &Input) = 0;

  /// Read the marker that closes the current bundle.
  virtual Error ReadBundleEnd(MemoryBuffer &Input) = 0;

  /// Read the current bundle and write the result into the stream \a OS.
  virtual Error ReadBundle(raw_ostream &OS, MemoryBuffer &Input) = 0;

  /// Write the header of the bundled file to \a OS based on the information
  /// gathered from \a Inputs.
  virtual Error WriteHeader(raw_ostream &OS,
                            ArrayRef<std::unique_ptr<MemoryBuffer>> Inputs) = 0;

  /// Write the marker that initiates a bundle for the triple \a TargetTriple to
  /// \a OS.
  virtual Error WriteBundleStart(raw_ostream &OS, StringRef TargetTriple) = 0;

  /// Write the marker that closes a bundle for the triple \a TargetTriple to \a
  /// OS.
  virtual Error WriteBundleEnd(raw_ostream &OS, StringRef TargetTriple) = 0;

  /// Write the bundle from \a Input into \a OS.
  virtual Error WriteBundle(raw_ostream &OS, MemoryBuffer &Input) = 0;

  /// Finalize output file.
  virtual Error finalizeOutputFile() { return Error::success(); }

  /// List bundle IDs in \a Input.
  virtual Error listBundleIDs(MemoryBuffer &Input) {
    if (Error Err = ReadHeader(Input))
      return Err;
    return forEachBundle(Input, [&](const BundleInfo &Info) -> Error {
      llvm::outs() << Info.BundleID << '\n';
      Error Err = listBundleIDsCallback(Input, Info);
      if (Err)
        return Err;
      return Error::success();
    });
  }

  /// Get bundle IDs in \a Input in \a BundleIds.
  virtual Error getBundleIDs(MemoryBuffer &Input,
                             std::set<StringRef> &BundleIds) {
    if (Error Err = ReadHeader(Input))
      return Err;
    return forEachBundle(Input, [&](const BundleInfo &Info) -> Error {
      BundleIds.insert(Info.BundleID);
      Error Err = listBundleIDsCallback(Input, Info);
      if (Err)
        return Err;
      return Error::success();
    });
  }

  /// For each bundle in \a Input, do \a Func.
  Error forEachBundle(MemoryBuffer &Input,
                      std::function<Error(const BundleInfo &)> Func) {
    while (true) {
      Expected<std::optional<StringRef>> CurTripleOrErr =
          ReadBundleStart(Input);
      if (!CurTripleOrErr)
        return CurTripleOrErr.takeError();

      // No more bundles.
      if (!*CurTripleOrErr)
        break;

      StringRef CurTriple = **CurTripleOrErr;
      assert(!CurTriple.empty());

      BundleInfo Info{CurTriple};
      if (Error Err = Func(Info))
        return Err;
    }
    return Error::success();
  }

protected:
  virtual Error listBundleIDsCallback(MemoryBuffer &Input,
                                      const BundleInfo &Info) {
    return Error::success();
  }
};

/// Handler for binary files. The bundled file will have the following format
/// (all integers are stored in little-endian format):
///
/// "OFFLOAD_BUNDLER_MAGIC_STR" (ASCII encoding of the string)
///
/// NumberOfOffloadBundles (8-byte integer)
///
/// OffsetOfBundle1 (8-byte integer)
/// SizeOfBundle1 (8-byte integer)
/// NumberOfBytesInTripleOfBundle1 (8-byte integer)
/// TripleOfBundle1 (byte length defined before)
///
/// ...
///
/// OffsetOfBundleN (8-byte integer)
/// SizeOfBundleN (8-byte integer)
/// NumberOfBytesInTripleOfBundleN (8-byte integer)
/// TripleOfBundleN (byte length defined before)
///
/// Bundle1
/// ...
/// BundleN

/// Read 8-byte integers from a buffer in little-endian format.
static uint64_t Read8byteIntegerFromBuffer(StringRef Buffer, size_t pos) {
  return llvm::support::endian::read64le(Buffer.data() + pos);
}

/// Write 8-byte integers to a buffer in little-endian format.
static void Write8byteIntegerToBuffer(raw_ostream &OS, uint64_t Val) {
  llvm::support::endian::write(OS, Val, llvm::endianness::little);
}

class BinaryFileHandler final : public FileHandler {
  /// Information about the bundles extracted from the header.
  struct BinaryBundleInfo final : public BundleInfo {
    /// Size of the bundle.
    uint64_t Size = 0u;
    /// Offset at which the bundle starts in the bundled file.
    uint64_t Offset = 0u;

    BinaryBundleInfo() {}
    BinaryBundleInfo(uint64_t Size, uint64_t Offset)
        : Size(Size), Offset(Offset) {}
  };

  /// Map between a triple and the corresponding bundle information.
  StringMap<BinaryBundleInfo> BundlesInfo;

  /// Iterator for the bundle information that is being read.
  StringMap<BinaryBundleInfo>::iterator CurBundleInfo;
  StringMap<BinaryBundleInfo>::iterator NextBundleInfo;

  /// Current bundle target to be written.
  std::string CurWriteBundleTarget;

  /// Configuration options and arrays for this bundler job
  const OffloadBundlerConfig &BundlerConfig;

public:
  // TODO: Add error checking from ClangOffloadBundler.cpp
  BinaryFileHandler(const OffloadBundlerConfig &BC) : BundlerConfig(BC) {}

  ~BinaryFileHandler() final {}

  Error ReadHeader(MemoryBuffer &Input) final {
    StringRef FC = Input.getBuffer();

    // Initialize the current bundle with the end of the container.
    CurBundleInfo = BundlesInfo.end();

    // Check if buffer is smaller than magic string.
    size_t ReadChars = sizeof(OFFLOAD_BUNDLER_MAGIC_STR) - 1;
    if (ReadChars > FC.size())
      return Error::success();

    // Check if no magic was found.
    if (llvm::identify_magic(FC) != llvm::file_magic::offload_bundle)
      return Error::success();

    // Read number of bundles.
    if (ReadChars + 8 > FC.size())
      return Error::success();

    uint64_t NumberOfBundles = Read8byteIntegerFromBuffer(FC, ReadChars);
    ReadChars += 8;

    // Read bundle offsets, sizes and triples.
    for (uint64_t i = 0; i < NumberOfBundles; ++i) {

      // Read offset.
      if (ReadChars + 8 > FC.size())
        return Error::success();

      uint64_t Offset = Read8byteIntegerFromBuffer(FC, ReadChars);
      ReadChars += 8;

      // Read size.
      if (ReadChars + 8 > FC.size())
        return Error::success();

      uint64_t Size = Read8byteIntegerFromBuffer(FC, ReadChars);
      ReadChars += 8;

      // Read triple size.
      if (ReadChars + 8 > FC.size())
        return Error::success();

      uint64_t TripleSize = Read8byteIntegerFromBuffer(FC, ReadChars);
      ReadChars += 8;

      // Read triple.
      if (ReadChars + TripleSize > FC.size())
        return Error::success();

      StringRef Triple(&FC.data()[ReadChars], TripleSize);
      ReadChars += TripleSize;

      // Check if the offset and size make sense.
      if (!Offset || Offset + Size > FC.size())
        return Error::success();

      assert(!BundlesInfo.contains(Triple) && "Triple is duplicated??");
      BundlesInfo[Triple] = BinaryBundleInfo(Size, Offset);
    }
    // Set the iterator to where we will start to read.
    CurBundleInfo = BundlesInfo.end();
    NextBundleInfo = BundlesInfo.begin();
    return Error::success();
  }

  Expected<std::optional<StringRef>>
  ReadBundleStart(MemoryBuffer &Input) final {
    if (NextBundleInfo == BundlesInfo.end())
      return std::nullopt;
    CurBundleInfo = NextBundleInfo++;
    return CurBundleInfo->first();
  }

  Error ReadBundleEnd(MemoryBuffer &Input) final {
    assert(CurBundleInfo != BundlesInfo.end() && "Invalid reader info!");
    return Error::success();
  }

  Error ReadBundle(raw_ostream &OS, MemoryBuffer &Input) final {
    assert(CurBundleInfo != BundlesInfo.end() && "Invalid reader info!");
    StringRef FC = Input.getBuffer();
    OS.write(FC.data() + CurBundleInfo->second.Offset,
             CurBundleInfo->second.Size);
    return Error::success();
  }

  Error WriteHeader(raw_ostream &OS,
                    ArrayRef<std::unique_ptr<MemoryBuffer>> Inputs) final {

    // Compute size of the header.
    uint64_t HeaderSize = 0;

    HeaderSize += sizeof(OFFLOAD_BUNDLER_MAGIC_STR) - 1;
    HeaderSize += 8; // Number of Bundles

    for (auto &T : BundlerConfig.TargetNames) {
      HeaderSize += 3 * 8; // Bundle offset, Size of bundle and size of triple.
      HeaderSize += T.size(); // The triple.
    }

    // Write to the buffer the header.
    OS << OFFLOAD_BUNDLER_MAGIC_STR;

    Write8byteIntegerToBuffer(OS, BundlerConfig.TargetNames.size());

    unsigned Idx = 0;
    for (auto &T : BundlerConfig.TargetNames) {
      MemoryBuffer &MB = *Inputs[Idx++];
      HeaderSize = alignTo(HeaderSize, BundlerConfig.BundleAlignment);
      // Bundle offset.
      Write8byteIntegerToBuffer(OS, HeaderSize);
      // Size of the bundle (adds to the next bundle's offset)
      Write8byteIntegerToBuffer(OS, MB.getBufferSize());
      BundlesInfo[T] = BinaryBundleInfo(MB.getBufferSize(), HeaderSize);
      HeaderSize += MB.getBufferSize();
      // Size of the triple
      Write8byteIntegerToBuffer(OS, T.size());
      // Triple
      OS << T;
    }
    return Error::success();
  }

  Error WriteBundleStart(raw_ostream &OS, StringRef TargetTriple) final {
    CurWriteBundleTarget = TargetTriple.str();
    return Error::success();
  }

  Error WriteBundleEnd(raw_ostream &OS, StringRef TargetTriple) final {
    return Error::success();
  }

  Error WriteBundle(raw_ostream &OS, MemoryBuffer &Input) final {
    auto BI = BundlesInfo[CurWriteBundleTarget];

    // Pad with 0 to reach specified offset.
    size_t CurrentPos = OS.tell();
    size_t PaddingSize = BI.Offset > CurrentPos ? BI.Offset - CurrentPos : 0;
    for (size_t I = 0; I < PaddingSize; ++I)
      OS.write('\0');
    assert(OS.tell() == BI.Offset);

    OS.write(Input.getBufferStart(), Input.getBufferSize());

    return Error::success();
  }
};

// This class implements a list of temporary files that are removed upon
// object destruction.
class TempFileHandlerRAII {
public:
  ~TempFileHandlerRAII() {
    for (const auto &File : Files)
      sys::fs::remove(File);
  }

  // Creates temporary file with given contents.
  Expected<StringRef> Create(std::optional<ArrayRef<char>> Contents) {
    SmallString<128u> File;
    if (std::error_code EC =
            sys::fs::createTemporaryFile("clang-offload-bundler", "tmp", File))
      return createFileError(File, EC);
    Files.push_front(File);

    if (Contents) {
      std::error_code EC;
      raw_fd_ostream OS(File, EC);
      if (EC)
        return createFileError(File, EC);
      OS.write(Contents->data(), Contents->size());
    }
    return Files.front().str();
  }

private:
  std::forward_list<SmallString<128u>> Files;
};

/// Handler for object files. The bundles are organized by sections with a
/// designated name.
///
/// To unbundle, we just copy the contents of the designated section.
class ObjectFileHandler final : public FileHandler {

  /// The object file we are currently dealing with.
  std::unique_ptr<ObjectFile> Obj;

  /// Return the input file contents.
  StringRef getInputFileContents() const { return Obj->getData(); }

  /// Return bundle name (<kind>-<triple>) if the provided section is an offload
  /// section.
  static Expected<std::optional<StringRef>>
  IsOffloadSection(SectionRef CurSection) {
    Expected<StringRef> NameOrErr = CurSection.getName();
    if (!NameOrErr)
      return NameOrErr.takeError();

    // If it does not start with the reserved suffix, just skip this section.
    if (llvm::identify_magic(*NameOrErr) != llvm::file_magic::offload_bundle)
      return std::nullopt;

    // Return the triple that is right after the reserved prefix.
    return NameOrErr->substr(sizeof(OFFLOAD_BUNDLER_MAGIC_STR) - 1);
  }

  /// Total number of inputs.
  unsigned NumberOfInputs = 0;

  /// Total number of processed inputs, i.e, inputs that were already
  /// read from the buffers.
  unsigned NumberOfProcessedInputs = 0;

  /// Iterator of the current and next section.
  section_iterator CurrentSection;
  section_iterator NextSection;

  /// Configuration options and arrays for this bundler job
  const OffloadBundlerConfig &BundlerConfig;

public:
  // TODO: Add error checking from ClangOffloadBundler.cpp
  ObjectFileHandler(std::unique_ptr<ObjectFile> ObjIn,
                    const OffloadBundlerConfig &BC)
      : Obj(std::move(ObjIn)), CurrentSection(Obj->section_begin()),
        NextSection(Obj->section_begin()), BundlerConfig(BC) {}

  ~ObjectFileHandler() final {}

  Error ReadHeader(MemoryBuffer &Input) final { return Error::success(); }

  Expected<std::optional<StringRef>>
  ReadBundleStart(MemoryBuffer &Input) final {
    while (NextSection != Obj->section_end()) {
      CurrentSection = NextSection;
      ++NextSection;

      // Check if the current section name starts with the reserved prefix. If
      // so, return the triple.
      Expected<std::optional<StringRef>> TripleOrErr =
          IsOffloadSection(*CurrentSection);
      if (!TripleOrErr)
        return TripleOrErr.takeError();
      if (*TripleOrErr)
        return **TripleOrErr;
    }
    return std::nullopt;
  }

  Error ReadBundleEnd(MemoryBuffer &Input) final { return Error::success(); }

  Error ReadBundle(raw_ostream &OS, MemoryBuffer &Input) final {
    Expected<StringRef> ContentOrErr = CurrentSection->getContents();
    if (!ContentOrErr)
      return ContentOrErr.takeError();
    StringRef Content = *ContentOrErr;

    // Copy fat object contents to the output when extracting host bundle.
    std::string ModifiedContent;
    if (Content.size() == 1u && Content.front() == 0) {
      auto HostBundleOrErr = getHostBundle(
          StringRef(Input.getBufferStart(), Input.getBufferSize()));
      if (!HostBundleOrErr)
        return HostBundleOrErr.takeError();

      ModifiedContent = std::move(*HostBundleOrErr);
      Content = ModifiedContent;
    }

    OS.write(Content.data(), Content.size());
    return Error::success();
  }

  Error WriteHeader(raw_ostream &OS,
                    ArrayRef<std::unique_ptr<MemoryBuffer>> Inputs) final {
    assert(BundlerConfig.HostInputIndex != ~0u &&
           "Host input index not defined.");

    // Record number of inputs.
    NumberOfInputs = Inputs.size();
    return Error::success();
  }

  Error WriteBundleStart(raw_ostream &OS, StringRef TargetTriple) final {
    ++NumberOfProcessedInputs;
    return Error::success();
  }

  Error WriteBundleEnd(raw_ostream &OS, StringRef TargetTriple) final {
    return Error::success();
  }

  Error finalizeOutputFile() final {
    assert(NumberOfProcessedInputs <= NumberOfInputs &&
           "Processing more inputs that actually exist!");
    assert(BundlerConfig.HostInputIndex != ~0u &&
           "Host input index not defined.");

    // If this is not the last output, we don't have to do anything.
    if (NumberOfProcessedInputs != NumberOfInputs)
      return Error::success();

    // We will use llvm-objcopy to add target objects sections to the output
    // fat object. These sections should have 'exclude' flag set which tells
    // link editor to remove them from linker inputs when linking executable or
    // shared library.

    assert(BundlerConfig.ObjcopyPath != "" &&
           "llvm-objcopy path not specified");

    // Temporary files that need to be removed.
    TempFileHandlerRAII TempFiles;

    // Compose llvm-objcopy command line for add target objects' sections with
    // appropriate flags.
    BumpPtrAllocator Alloc;
    StringSaver SS{Alloc};
    SmallVector<StringRef, 8u> ObjcopyArgs{"llvm-objcopy"};

    for (unsigned I = 0; I < NumberOfInputs; ++I) {
      StringRef InputFile = BundlerConfig.InputFileNames[I];
      if (I == BundlerConfig.HostInputIndex) {
        // Special handling for the host bundle. We do not need to add a
        // standard bundle for the host object since we are going to use fat
        // object as a host object. Therefore use dummy contents (one zero byte)
        // when creating section for the host bundle.
        Expected<StringRef> TempFileOrErr = TempFiles.Create(ArrayRef<char>(0));
        if (!TempFileOrErr)
          return TempFileOrErr.takeError();
        InputFile = *TempFileOrErr;
      }

      ObjcopyArgs.push_back(
          SS.save(Twine("--add-section=") + OFFLOAD_BUNDLER_MAGIC_STR +
                  BundlerConfig.TargetNames[I] + "=" + InputFile));
      ObjcopyArgs.push_back(
          SS.save(Twine("--set-section-flags=") + OFFLOAD_BUNDLER_MAGIC_STR +
                  BundlerConfig.TargetNames[I] + "=readonly,exclude"));
    }
    ObjcopyArgs.push_back("--");
    ObjcopyArgs.push_back(
        BundlerConfig.InputFileNames[BundlerConfig.HostInputIndex]);
    ObjcopyArgs.push_back(BundlerConfig.OutputFileNames.front());

    if (Error Err = executeObjcopy(BundlerConfig.ObjcopyPath, ObjcopyArgs))
      return Err;

    return Error::success();
  }

  Error WriteBundle(raw_ostream &OS, MemoryBuffer &Input) final {
    return Error::success();
  }

private:
  Error executeObjcopy(StringRef Objcopy, ArrayRef<StringRef> Args) {
    // If the user asked for the commands to be printed out, we do that
    // instead of executing it.
    if (BundlerConfig.PrintExternalCommands) {
      errs() << "\"" << Objcopy << "\"";
      for (StringRef Arg : drop_begin(Args, 1))
        errs() << " \"" << Arg << "\"";
      errs() << "\n";
    } else {
      if (sys::ExecuteAndWait(Objcopy, Args))
        return createStringError(inconvertibleErrorCode(),
                                 "'llvm-objcopy' tool failed");
    }
    return Error::success();
  }

  Expected<std::string> getHostBundle(StringRef Input) {
    TempFileHandlerRAII TempFiles;

    auto ModifiedObjPathOrErr = TempFiles.Create(std::nullopt);
    if (!ModifiedObjPathOrErr)
      return ModifiedObjPathOrErr.takeError();
    StringRef ModifiedObjPath = *ModifiedObjPathOrErr;

    BumpPtrAllocator Alloc;
    StringSaver SS{Alloc};
    SmallVector<StringRef, 16> ObjcopyArgs{"llvm-objcopy"};

    ObjcopyArgs.push_back("--regex");
    ObjcopyArgs.push_back("--remove-section=__CLANG_OFFLOAD_BUNDLE__.*");
    ObjcopyArgs.push_back("--");

    StringRef ObjcopyInputFileName;
    // When unbundling an archive, the content of each object file in the
    // archive is passed to this function by parameter Input, which is different
    // from the content of the original input archive file, therefore it needs
    // to be saved to a temporary file before passed to llvm-objcopy. Otherwise,
    // Input is the same as the content of the original input file, therefore
    // temporary file is not needed.
    if (StringRef(BundlerConfig.FilesType).starts_with("a")) {
      auto InputFileOrErr =
          TempFiles.Create(ArrayRef<char>(Input.data(), Input.size()));
      if (!InputFileOrErr)
        return InputFileOrErr.takeError();
      ObjcopyInputFileName = *InputFileOrErr;
    } else
      ObjcopyInputFileName = BundlerConfig.InputFileNames.front();

    ObjcopyArgs.push_back(ObjcopyInputFileName);
    ObjcopyArgs.push_back(ModifiedObjPath);

    if (Error Err = executeObjcopy(BundlerConfig.ObjcopyPath, ObjcopyArgs))
      return std::move(Err);

    auto BufOrErr = MemoryBuffer::getFile(ModifiedObjPath);
    if (!BufOrErr)
      return createStringError(BufOrErr.getError(),
                               "Failed to read back the modified object file");

    return BufOrErr->get()->getBuffer().str();
  }
};

/// Handler for text files. The bundled file will have the following format.
///
/// "Comment OFFLOAD_BUNDLER_MAGIC_STR__START__ triple"
/// Bundle 1
/// "Comment OFFLOAD_BUNDLER_MAGIC_STR__END__ triple"
/// ...
/// "Comment OFFLOAD_BUNDLER_MAGIC_STR__START__ triple"
/// Bundle N
/// "Comment OFFLOAD_BUNDLER_MAGIC_STR__END__ triple"
class TextFileHandler final : public FileHandler {
  /// String that begins a line comment.
  StringRef Comment;

  /// String that initiates a bundle.
  std::string BundleStartString;

  /// String that closes a bundle.
  std::string BundleEndString;

  /// Number of chars read from input.
  size_t ReadChars = 0u;

protected:
  Error ReadHeader(MemoryBuffer &Input) final { return Error::success(); }

  Expected<std::optional<StringRef>>
  ReadBundleStart(MemoryBuffer &Input) final {
    StringRef FC = Input.getBuffer();

    // Find start of the bundle.
    ReadChars = FC.find(BundleStartString, ReadChars);
    if (ReadChars == FC.npos)
      return std::nullopt;

    // Get position of the triple.
    size_t TripleStart = ReadChars = ReadChars + BundleStartString.size();

    // Get position that closes the triple.
    size_t TripleEnd = ReadChars = FC.find("\n", ReadChars);
    if (TripleEnd == FC.npos)
      return std::nullopt;

    // Next time we read after the new line.
    ++ReadChars;

    return StringRef(&FC.data()[TripleStart], TripleEnd - TripleStart);
  }

  Error ReadBundleEnd(MemoryBuffer &Input) final {
    StringRef FC = Input.getBuffer();

    // Read up to the next new line.
    assert(FC[ReadChars] == '\n' && "The bundle should end with a new line.");

    size_t TripleEnd = ReadChars = FC.find("\n", ReadChars + 1);
    if (TripleEnd != FC.npos)
      // Next time we read after the new line.
      ++ReadChars;

    return Error::success();
  }

  Error ReadBundle(raw_ostream &OS, MemoryBuffer &Input) final {
    StringRef FC = Input.getBuffer();
    size_t BundleStart = ReadChars;

    // Find end of the bundle.
    size_t BundleEnd = ReadChars = FC.find(BundleEndString, ReadChars);

    StringRef Bundle(&FC.data()[BundleStart], BundleEnd - BundleStart);
    OS << Bundle;

    return Error::success();
  }

  Error WriteHeader(raw_ostream &OS,
                    ArrayRef<std::unique_ptr<MemoryBuffer>> Inputs) final {
    return Error::success();
  }

  Error WriteBundleStart(raw_ostream &OS, StringRef TargetTriple) final {
    OS << BundleStartString << TargetTriple << "\n";
    return Error::success();
  }

  Error WriteBundleEnd(raw_ostream &OS, StringRef TargetTriple) final {
    OS << BundleEndString << TargetTriple << "\n";
    return Error::success();
  }

  Error WriteBundle(raw_ostream &OS, MemoryBuffer &Input) final {
    OS << Input.getBuffer();
    return Error::success();
  }

public:
  TextFileHandler(StringRef Comment) : Comment(Comment), ReadChars(0) {
    BundleStartString =
        "\n" + Comment.str() + " " OFFLOAD_BUNDLER_MAGIC_STR "__START__ ";
    BundleEndString =
        "\n" + Comment.str() + " " OFFLOAD_BUNDLER_MAGIC_STR "__END__ ";
  }

  Error listBundleIDsCallback(MemoryBuffer &Input,
                              const BundleInfo &Info) final {
    // TODO: To list bundle IDs in a bundled text file we need to go through
    // all bundles. The format of bundled text file may need to include a
    // header if the performance of listing bundle IDs of bundled text file is
    // important.
    ReadChars = Input.getBuffer().find(BundleEndString, ReadChars);
    if (Error Err = ReadBundleEnd(Input))
      return Err;
    return Error::success();
  }
};
} // namespace

/// Return an appropriate object file handler. We use the specific object
/// handler if we know how to deal with that format, otherwise we use a default
/// binary file handler.
static std::unique_ptr<FileHandler>
CreateObjectFileHandler(MemoryBuffer &FirstInput,
                        const OffloadBundlerConfig &BundlerConfig) {
  // Check if the input file format is one that we know how to deal with.
  Expected<std::unique_ptr<Binary>> BinaryOrErr = createBinary(FirstInput);

  // We only support regular object files. If failed to open the input as a
  // known binary or this is not an object file use the default binary handler.
  if (errorToBool(BinaryOrErr.takeError()) || !isa<ObjectFile>(*BinaryOrErr))
    return std::make_unique<BinaryFileHandler>(BundlerConfig);

  // Otherwise create an object file handler. The handler will be owned by the
  // client of this function.
  return std::make_unique<ObjectFileHandler>(
      std::unique_ptr<ObjectFile>(cast<ObjectFile>(BinaryOrErr->release())),
      BundlerConfig);
}

/// Return an appropriate handler given the input files and options.
static Expected<std::unique_ptr<FileHandler>>
CreateFileHandler(MemoryBuffer &FirstInput,
                  const OffloadBundlerConfig &BundlerConfig) {
  std::string FilesType = BundlerConfig.FilesType;

  if (FilesType == "i")
    return std::make_unique<TextFileHandler>(/*Comment=*/"//");
  if (FilesType == "ii")
    return std::make_unique<TextFileHandler>(/*Comment=*/"//");
  if (FilesType == "cui")
    return std::make_unique<TextFileHandler>(/*Comment=*/"//");
  if (FilesType == "hipi")
    return std::make_unique<TextFileHandler>(/*Comment=*/"//");
  // TODO: `.d` should be eventually removed once `-M` and its variants are
  // handled properly in offload compilation.
  if (FilesType == "d")
    return std::make_unique<TextFileHandler>(/*Comment=*/"#");
  if (FilesType == "ll")
    return std::make_unique<TextFileHandler>(/*Comment=*/";");
  if (FilesType == "bc")
    return std::make_unique<BinaryFileHandler>(BundlerConfig);
  if (FilesType == "s")
    return std::make_unique<TextFileHandler>(/*Comment=*/"#");
  if (FilesType == "o")
    return CreateObjectFileHandler(FirstInput, BundlerConfig);
  if (FilesType == "a")
    return CreateObjectFileHandler(FirstInput, BundlerConfig);
  if (FilesType == "gch")
    return std::make_unique<BinaryFileHandler>(BundlerConfig);
  if (FilesType == "ast")
    return std::make_unique<BinaryFileHandler>(BundlerConfig);

  return createStringError(errc::invalid_argument,
                           "'" + FilesType + "': invalid file type specified");
}

OffloadBundlerConfig::OffloadBundlerConfig() {
  if (llvm::compression::zstd::isAvailable()) {
    CompressionFormat = llvm::compression::Format::Zstd;
    // Compression level 3 is usually sufficient for zstd since long distance
    // matching is enabled.
    CompressionLevel = 3;
  } else if (llvm::compression::zlib::isAvailable()) {
    CompressionFormat = llvm::compression::Format::Zlib;
    // Use default level for zlib since higher level does not have significant
    // improvement.
    CompressionLevel = llvm::compression::zlib::DefaultCompression;
  }
  auto IgnoreEnvVarOpt =
      llvm::sys::Process::GetEnv("OFFLOAD_BUNDLER_IGNORE_ENV_VAR");
  if (IgnoreEnvVarOpt.has_value() && IgnoreEnvVarOpt.value() == "1")
    return;

  auto VerboseEnvVarOpt = llvm::sys::Process::GetEnv("OFFLOAD_BUNDLER_VERBOSE");
  if (VerboseEnvVarOpt.has_value())
    Verbose = VerboseEnvVarOpt.value() == "1";

  auto CompressEnvVarOpt =
      llvm::sys::Process::GetEnv("OFFLOAD_BUNDLER_COMPRESS");
  if (CompressEnvVarOpt.has_value())
    Compress = CompressEnvVarOpt.value() == "1";

  auto CompressionLevelEnvVarOpt =
      llvm::sys::Process::GetEnv("OFFLOAD_BUNDLER_COMPRESSION_LEVEL");
  if (CompressionLevelEnvVarOpt.has_value()) {
    llvm::StringRef CompressionLevelStr = CompressionLevelEnvVarOpt.value();
    int Level;
    if (!CompressionLevelStr.getAsInteger(10, Level))
      CompressionLevel = Level;
    else
      llvm::errs()
          << "Warning: Invalid value for OFFLOAD_BUNDLER_COMPRESSION_LEVEL: "
          << CompressionLevelStr.str() << ". Ignoring it.\n";
  }
}

// Utility function to format numbers with commas
static std::string formatWithCommas(unsigned long long Value) {
  std::string Num = std::to_string(Value);
  int InsertPosition = Num.length() - 3;
  while (InsertPosition > 0) {
    Num.insert(InsertPosition, ",");
    InsertPosition -= 3;
  }
  return Num;
}

llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
CompressedOffloadBundle::compress(llvm::compression::Params P,
                                  const llvm::MemoryBuffer &Input,
                                  bool Verbose) {
  if (!llvm::compression::zstd::isAvailable() &&
      !llvm::compression::zlib::isAvailable())
    return createStringError(llvm::inconvertibleErrorCode(),
                             "Compression not supported");

  llvm::Timer HashTimer("Hash Calculation Timer", "Hash calculation time",
                        ClangOffloadBundlerTimerGroup);
  if (Verbose)
    HashTimer.startTimer();
  llvm::MD5 Hash;
  llvm::MD5::MD5Result Result;
  Hash.update(Input.getBuffer());
  Hash.final(Result);
  uint64_t TruncatedHash = Result.low();
  if (Verbose)
    HashTimer.stopTimer();

  SmallVector<uint8_t, 0> CompressedBuffer;
  auto BufferUint8 = llvm::ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(Input.getBuffer().data()),
      Input.getBuffer().size());

  llvm::Timer CompressTimer("Compression Timer", "Compression time",
                            ClangOffloadBundlerTimerGroup);
  if (Verbose)
    CompressTimer.startTimer();
  llvm::compression::compress(P, BufferUint8, CompressedBuffer);
  if (Verbose)
    CompressTimer.stopTimer();

  uint16_t CompressionMethod = static_cast<uint16_t>(P.format);
  uint32_t UncompressedSize = Input.getBuffer().size();
  uint32_t TotalFileSize = MagicNumber.size() + sizeof(TotalFileSize) +
                           sizeof(Version) + sizeof(CompressionMethod) +
                           sizeof(UncompressedSize) + sizeof(TruncatedHash) +
                           CompressedBuffer.size();

  SmallVector<char, 0> FinalBuffer;
  llvm::raw_svector_ostream OS(FinalBuffer);
  OS << MagicNumber;
  OS.write(reinterpret_cast<const char *>(&Version), sizeof(Version));
  OS.write(reinterpret_cast<const char *>(&CompressionMethod),
           sizeof(CompressionMethod));
  OS.write(reinterpret_cast<const char *>(&TotalFileSize),
           sizeof(TotalFileSize));
  OS.write(reinterpret_cast<const char *>(&UncompressedSize),
           sizeof(UncompressedSize));
  OS.write(reinterpret_cast<const char *>(&TruncatedHash),
           sizeof(TruncatedHash));
  OS.write(reinterpret_cast<const char *>(CompressedBuffer.data()),
           CompressedBuffer.size());

  if (Verbose) {
    auto MethodUsed =
        P.format == llvm::compression::Format::Zstd ? "zstd" : "zlib";
    double CompressionRate =
        static_cast<double>(UncompressedSize) / CompressedBuffer.size();
    double CompressionTimeSeconds = CompressTimer.getTotalTime().getWallTime();
    double CompressionSpeedMBs =
        (UncompressedSize / (1024.0 * 1024.0)) / CompressionTimeSeconds;

    llvm::errs() << "Compressed bundle format version: " << Version << "\n"
                 << "Total file size (including headers): "
                 << formatWithCommas(TotalFileSize) << " bytes\n"
                 << "Compression method used: " << MethodUsed << "\n"
                 << "Compression level: " << P.level << "\n"
                 << "Binary size before compression: "
                 << formatWithCommas(UncompressedSize) << " bytes\n"
                 << "Binary size after compression: "
                 << formatWithCommas(CompressedBuffer.size()) << " bytes\n"
                 << "Compression rate: "
                 << llvm::format("%.2lf", CompressionRate) << "\n"
                 << "Compression ratio: "
                 << llvm::format("%.2lf%%", 100.0 / CompressionRate) << "\n"
                 << "Compression speed: "
                 << llvm::format("%.2lf MB/s", CompressionSpeedMBs) << "\n"
                 << "Truncated MD5 hash: "
                 << llvm::format_hex(TruncatedHash, 16) << "\n";
  }
  return llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(FinalBuffer.data(), FinalBuffer.size()));
}

llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
CompressedOffloadBundle::decompress(const llvm::MemoryBuffer &Input,
                                    bool Verbose) {

  StringRef Blob = Input.getBuffer();

  if (Blob.size() < V1HeaderSize)
    return llvm::MemoryBuffer::getMemBufferCopy(Blob);

  if (llvm::identify_magic(Blob) !=
      llvm::file_magic::offload_bundle_compressed) {
    if (Verbose)
      llvm::errs() << "Uncompressed bundle.\n";
    return llvm::MemoryBuffer::getMemBufferCopy(Blob);
  }

  size_t CurrentOffset = MagicSize;

  uint16_t ThisVersion;
  memcpy(&ThisVersion, Blob.data() + CurrentOffset, sizeof(uint16_t));
  CurrentOffset += VersionFieldSize;

  uint16_t CompressionMethod;
  memcpy(&CompressionMethod, Blob.data() + CurrentOffset, sizeof(uint16_t));
  CurrentOffset += MethodFieldSize;

  uint32_t TotalFileSize;
  if (ThisVersion >= 2) {
    if (Blob.size() < V2HeaderSize)
      return createStringError(inconvertibleErrorCode(),
                               "Compressed bundle header size too small");
    memcpy(&TotalFileSize, Blob.data() + CurrentOffset, sizeof(uint32_t));
    CurrentOffset += FileSizeFieldSize;
  }

  uint32_t UncompressedSize;
  memcpy(&UncompressedSize, Blob.data() + CurrentOffset, sizeof(uint32_t));
  CurrentOffset += UncompressedSizeFieldSize;

  uint64_t StoredHash;
  memcpy(&StoredHash, Blob.data() + CurrentOffset, sizeof(uint64_t));
  CurrentOffset += HashFieldSize;

  llvm::compression::Format CompressionFormat;
  if (CompressionMethod ==
      static_cast<uint16_t>(llvm::compression::Format::Zlib))
    CompressionFormat = llvm::compression::Format::Zlib;
  else if (CompressionMethod ==
           static_cast<uint16_t>(llvm::compression::Format::Zstd))
    CompressionFormat = llvm::compression::Format::Zstd;
  else
    return createStringError(inconvertibleErrorCode(),
                             "Unknown compressing method");

  llvm::Timer DecompressTimer("Decompression Timer", "Decompression time",
                              ClangOffloadBundlerTimerGroup);
  if (Verbose)
    DecompressTimer.startTimer();

  SmallVector<uint8_t, 0> DecompressedData;
  StringRef CompressedData = Blob.substr(CurrentOffset);
  if (llvm::Error DecompressionError = llvm::compression::decompress(
          CompressionFormat, llvm::arrayRefFromStringRef(CompressedData),
          DecompressedData, UncompressedSize))
    return createStringError(inconvertibleErrorCode(),
                             "Could not decompress embedded file contents: " +
                                 llvm::toString(std::move(DecompressionError)));

  if (Verbose) {
    DecompressTimer.stopTimer();

    double DecompressionTimeSeconds =
        DecompressTimer.getTotalTime().getWallTime();

    // Recalculate MD5 hash for integrity check
    llvm::Timer HashRecalcTimer("Hash Recalculation Timer",
                                "Hash recalculation time",
                                ClangOffloadBundlerTimerGroup);
    HashRecalcTimer.startTimer();
    llvm::MD5 Hash;
    llvm::MD5::MD5Result Result;
    Hash.update(llvm::ArrayRef<uint8_t>(DecompressedData.data(),
                                        DecompressedData.size()));
    Hash.final(Result);
    uint64_t RecalculatedHash = Result.low();
    HashRecalcTimer.stopTimer();
    bool HashMatch = (StoredHash == RecalculatedHash);

    double CompressionRate =
        static_cast<double>(UncompressedSize) / CompressedData.size();
    double DecompressionSpeedMBs =
        (UncompressedSize / (1024.0 * 1024.0)) / DecompressionTimeSeconds;

    llvm::errs() << "Compressed bundle format version: " << ThisVersion << "\n";
    if (ThisVersion >= 2)
      llvm::errs() << "Total file size (from header): "
                   << formatWithCommas(TotalFileSize) << " bytes\n";
    llvm::errs() << "Decompression method: "
                 << (CompressionFormat == llvm::compression::Format::Zlib
                         ? "zlib"
                         : "zstd")
                 << "\n"
                 << "Size before decompression: "
                 << formatWithCommas(CompressedData.size()) << " bytes\n"
                 << "Size after decompression: "
                 << formatWithCommas(UncompressedSize) << " bytes\n"
                 << "Compression rate: "
                 << llvm::format("%.2lf", CompressionRate) << "\n"
                 << "Compression ratio: "
                 << llvm::format("%.2lf%%", 100.0 / CompressionRate) << "\n"
                 << "Decompression speed: "
                 << llvm::format("%.2lf MB/s", DecompressionSpeedMBs) << "\n"
                 << "Stored hash: " << llvm::format_hex(StoredHash, 16) << "\n"
                 << "Recalculated hash: "
                 << llvm::format_hex(RecalculatedHash, 16) << "\n"
                 << "Hashes match: " << (HashMatch ? "Yes" : "No") << "\n";
  }

  return llvm::MemoryBuffer::getMemBufferCopy(
      llvm::toStringRef(DecompressedData));
}

// List bundle IDs. Return true if an error was found.
Error OffloadBundler::ListBundleIDsInFile(
    StringRef InputFileName, const OffloadBundlerConfig &BundlerConfig) {
  // Open Input file.
  ErrorOr<std::unique_ptr<MemoryBuffer>> CodeOrErr =
      MemoryBuffer::getFileOrSTDIN(InputFileName);
  if (std::error_code EC = CodeOrErr.getError())
    return createFileError(InputFileName, EC);

  // Decompress the input if necessary.
  Expected<std::unique_ptr<MemoryBuffer>> DecompressedBufferOrErr =
      CompressedOffloadBundle::decompress(**CodeOrErr, BundlerConfig.Verbose);
  if (!DecompressedBufferOrErr)
    return createStringError(
        inconvertibleErrorCode(),
        "Failed to decompress input: " +
            llvm::toString(DecompressedBufferOrErr.takeError()));

  MemoryBuffer &DecompressedInput = **DecompressedBufferOrErr;

  // Select the right files handler.
  Expected<std::unique_ptr<FileHandler>> FileHandlerOrErr =
      CreateFileHandler(DecompressedInput, BundlerConfig);
  if (!FileHandlerOrErr)
    return FileHandlerOrErr.takeError();

  std::unique_ptr<FileHandler> &FH = *FileHandlerOrErr;
  assert(FH);
  return FH->listBundleIDs(DecompressedInput);
}

/// @brief Checks if a code object \p CodeObjectInfo is compatible with a given
/// target \p TargetInfo.
/// @link https://clang.llvm.org/docs/ClangOffloadBundler.html#bundle-entry-id
bool isCodeObjectCompatible(const OffloadTargetInfo &CodeObjectInfo,
                            const OffloadTargetInfo &TargetInfo) {

  // Compatible in case of exact match.
  if (CodeObjectInfo == TargetInfo) {
    DEBUG_WITH_TYPE("CodeObjectCompatibility",
                    dbgs() << "Compatible: Exact match: \t[CodeObject: "
                           << CodeObjectInfo.str()
                           << "]\t:\t[Target: " << TargetInfo.str() << "]\n");
    return true;
  }

  // Incompatible if Kinds or Triples mismatch.
  if (!CodeObjectInfo.isOffloadKindCompatible(TargetInfo.OffloadKind) ||
      !CodeObjectInfo.Triple.isCompatibleWith(TargetInfo.Triple)) {
    DEBUG_WITH_TYPE(
        "CodeObjectCompatibility",
        dbgs() << "Incompatible: Kind/Triple mismatch \t[CodeObject: "
               << CodeObjectInfo.str() << "]\t:\t[Target: " << TargetInfo.str()
               << "]\n");
    return false;
  }

  // Incompatible if Processors mismatch.
  llvm::StringMap<bool> CodeObjectFeatureMap, TargetFeatureMap;
  std::optional<StringRef> CodeObjectProc = clang::parseTargetID(
      CodeObjectInfo.Triple, CodeObjectInfo.TargetID, &CodeObjectFeatureMap);
  std::optional<StringRef> TargetProc = clang::parseTargetID(
      TargetInfo.Triple, TargetInfo.TargetID, &TargetFeatureMap);

  // Both TargetProc and CodeObjectProc can't be empty here.
  if (!TargetProc || !CodeObjectProc ||
      CodeObjectProc.value() != TargetProc.value()) {
    DEBUG_WITH_TYPE("CodeObjectCompatibility",
                    dbgs() << "Incompatible: Processor mismatch \t[CodeObject: "
                           << CodeObjectInfo.str()
                           << "]\t:\t[Target: " << TargetInfo.str() << "]\n");
    return false;
  }

  // Incompatible if CodeObject has more features than Target, irrespective of
  // type or sign of features.
  if (CodeObjectFeatureMap.getNumItems() > TargetFeatureMap.getNumItems()) {
    DEBUG_WITH_TYPE("CodeObjectCompatibility",
                    dbgs() << "Incompatible: CodeObject has more features "
                              "than target \t[CodeObject: "
                           << CodeObjectInfo.str()
                           << "]\t:\t[Target: " << TargetInfo.str() << "]\n");
    return false;
  }

  // Compatible if each target feature specified by target is compatible with
  // target feature of code object. The target feature is compatible if the
  // code object does not specify it (meaning Any), or if it specifies it
  // with the same value (meaning On or Off).
  for (const auto &CodeObjectFeature : CodeObjectFeatureMap) {
    auto TargetFeature = TargetFeatureMap.find(CodeObjectFeature.getKey());
    if (TargetFeature == TargetFeatureMap.end()) {
      DEBUG_WITH_TYPE(
          "CodeObjectCompatibility",
          dbgs()
              << "Incompatible: Value of CodeObject's non-ANY feature is "
                 "not matching with Target feature's ANY value \t[CodeObject: "
              << CodeObjectInfo.str() << "]\t:\t[Target: " << TargetInfo.str()
              << "]\n");
      return false;
    } else if (TargetFeature->getValue() != CodeObjectFeature.getValue()) {
      DEBUG_WITH_TYPE(
          "CodeObjectCompatibility",
          dbgs() << "Incompatible: Value of CodeObject's non-ANY feature is "
                    "not matching with Target feature's non-ANY value "
                    "\t[CodeObject: "
                 << CodeObjectInfo.str()
                 << "]\t:\t[Target: " << TargetInfo.str() << "]\n");
      return false;
    }
  }

  // CodeObject is compatible if all features of Target are:
  //   - either, present in the Code Object's features map with the same sign,
  //   - or, the feature is missing from CodeObjects's features map i.e. it is
  //   set to ANY
  DEBUG_WITH_TYPE(
      "CodeObjectCompatibility",
      dbgs() << "Compatible: Target IDs are compatible \t[CodeObject: "
             << CodeObjectInfo.str() << "]\t:\t[Target: " << TargetInfo.str()
             << "]\n");
  return true;
}

/// Bundle the files. Return true if an error was found.
Error OffloadBundler::BundleFiles() {
  std::error_code EC;

  // Create a buffer to hold the content before compressing.
  SmallVector<char, 0> Buffer;
  llvm::raw_svector_ostream BufferStream(Buffer);

  // Open input files.
  SmallVector<std::unique_ptr<MemoryBuffer>, 8u> InputBuffers;
  InputBuffers.reserve(BundlerConfig.InputFileNames.size());
  for (auto &I : BundlerConfig.InputFileNames) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> CodeOrErr =
        MemoryBuffer::getFileOrSTDIN(I);
    if (std::error_code EC = CodeOrErr.getError())
      return createFileError(I, EC);
    InputBuffers.emplace_back(std::move(*CodeOrErr));
  }

  // Get the file handler. We use the host buffer as reference.
  assert((BundlerConfig.HostInputIndex != ~0u || BundlerConfig.AllowNoHost) &&
         "Host input index undefined??");
  Expected<std::unique_ptr<FileHandler>> FileHandlerOrErr = CreateFileHandler(
      *InputBuffers[BundlerConfig.AllowNoHost ? 0
                                              : BundlerConfig.HostInputIndex],
      BundlerConfig);
  if (!FileHandlerOrErr)
    return FileHandlerOrErr.takeError();

  std::unique_ptr<FileHandler> &FH = *FileHandlerOrErr;
  assert(FH);

  // Write header.
  if (Error Err = FH->WriteHeader(BufferStream, InputBuffers))
    return Err;

  // Write all bundles along with the start/end markers. If an error was found
  // writing the end of the bundle component, abort the bundle writing.
  auto Input = InputBuffers.begin();
  for (auto &Triple : BundlerConfig.TargetNames) {
    if (Error Err = FH->WriteBundleStart(BufferStream, Triple))
      return Err;
    if (Error Err = FH->WriteBundle(BufferStream, **Input))
      return Err;
    if (Error Err = FH->WriteBundleEnd(BufferStream, Triple))
      return Err;
    ++Input;
  }

  raw_fd_ostream OutputFile(BundlerConfig.OutputFileNames.front(), EC,
                            sys::fs::OF_None);
  if (EC)
    return createFileError(BundlerConfig.OutputFileNames.front(), EC);

  SmallVector<char, 0> CompressedBuffer;
  if (BundlerConfig.Compress) {
    std::unique_ptr<llvm::MemoryBuffer> BufferMemory =
        llvm::MemoryBuffer::getMemBufferCopy(
            llvm::StringRef(Buffer.data(), Buffer.size()));
    auto CompressionResult = CompressedOffloadBundle::compress(
        {BundlerConfig.CompressionFormat, BundlerConfig.CompressionLevel,
         /*zstdEnableLdm=*/true},
        *BufferMemory, BundlerConfig.Verbose);
    if (auto Error = CompressionResult.takeError())
      return Error;

    auto CompressedMemBuffer = std::move(CompressionResult.get());
    CompressedBuffer.assign(CompressedMemBuffer->getBufferStart(),
                            CompressedMemBuffer->getBufferEnd());
  } else
    CompressedBuffer = Buffer;

  OutputFile.write(CompressedBuffer.data(), CompressedBuffer.size());

  return FH->finalizeOutputFile();
}

// Unbundle the files. Return true if an error was found.
Error OffloadBundler::UnbundleFiles() {
  // Open Input file.
  ErrorOr<std::unique_ptr<MemoryBuffer>> CodeOrErr =
      MemoryBuffer::getFileOrSTDIN(BundlerConfig.InputFileNames.front());
  if (std::error_code EC = CodeOrErr.getError())
    return createFileError(BundlerConfig.InputFileNames.front(), EC);

  // Decompress the input if necessary.
  Expected<std::unique_ptr<MemoryBuffer>> DecompressedBufferOrErr =
      CompressedOffloadBundle::decompress(**CodeOrErr, BundlerConfig.Verbose);
  if (!DecompressedBufferOrErr)
    return createStringError(
        inconvertibleErrorCode(),
        "Failed to decompress input: " +
            llvm::toString(DecompressedBufferOrErr.takeError()));

  MemoryBuffer &Input = **DecompressedBufferOrErr;

  // Select the right files handler.
  Expected<std::unique_ptr<FileHandler>> FileHandlerOrErr =
      CreateFileHandler(Input, BundlerConfig);
  if (!FileHandlerOrErr)
    return FileHandlerOrErr.takeError();

  std::unique_ptr<FileHandler> &FH = *FileHandlerOrErr;
  assert(FH);

  // Read the header of the bundled file.
  if (Error Err = FH->ReadHeader(Input))
    return Err;

  // Create a work list that consist of the map triple/output file.
  StringMap<StringRef> Worklist;
  auto Output = BundlerConfig.OutputFileNames.begin();
  for (auto &Triple : BundlerConfig.TargetNames) {
    Worklist[Triple] = *Output;
    ++Output;
  }

  // Read all the bundles that are in the work list. If we find no bundles we
  // assume the file is meant for the host target.
  bool FoundHostBundle = false;
  while (!Worklist.empty()) {
    Expected<std::optional<StringRef>> CurTripleOrErr =
        FH->ReadBundleStart(Input);
    if (!CurTripleOrErr)
      return CurTripleOrErr.takeError();

    // We don't have more bundles.
    if (!*CurTripleOrErr)
      break;

    StringRef CurTriple = **CurTripleOrErr;
    assert(!CurTriple.empty());

    auto Output = Worklist.begin();
    for (auto E = Worklist.end(); Output != E; Output++) {
      if (isCodeObjectCompatible(
              OffloadTargetInfo(CurTriple, BundlerConfig),
              OffloadTargetInfo((*Output).first(), BundlerConfig))) {
        break;
      }
    }

    if (Output == Worklist.end())
      continue;
    // Check if the output file can be opened and copy the bundle to it.
    std::error_code EC;
    raw_fd_ostream OutputFile((*Output).second, EC, sys::fs::OF_None);
    if (EC)
      return createFileError((*Output).second, EC);
    if (Error Err = FH->ReadBundle(OutputFile, Input))
      return Err;
    if (Error Err = FH->ReadBundleEnd(Input))
      return Err;
    Worklist.erase(Output);

    // Record if we found the host bundle.
    auto OffloadInfo = OffloadTargetInfo(CurTriple, BundlerConfig);
    if (OffloadInfo.hasHostKind())
      FoundHostBundle = true;
  }

  if (!BundlerConfig.AllowMissingBundles && !Worklist.empty()) {
    std::string ErrMsg = "Can't find bundles for";
    std::set<StringRef> Sorted;
    for (auto &E : Worklist)
      Sorted.insert(E.first());
    unsigned I = 0;
    unsigned Last = Sorted.size() - 1;
    for (auto &E : Sorted) {
      if (I != 0 && Last > 1)
        ErrMsg += ",";
      ErrMsg += " ";
      if (I == Last && I != 0)
        ErrMsg += "and ";
      ErrMsg += E.str();
      ++I;
    }
    return createStringError(inconvertibleErrorCode(), ErrMsg);
  }

  // If no bundles were found, assume the input file is the host bundle and
  // create empty files for the remaining targets.
  if (Worklist.size() == BundlerConfig.TargetNames.size()) {
    for (auto &E : Worklist) {
      std::error_code EC;
      raw_fd_ostream OutputFile(E.second, EC, sys::fs::OF_None);
      if (EC)
        return createFileError(E.second, EC);

      // If this entry has a host kind, copy the input file to the output file.
      auto OffloadInfo = OffloadTargetInfo(E.getKey(), BundlerConfig);
      if (OffloadInfo.hasHostKind())
        OutputFile.write(Input.getBufferStart(), Input.getBufferSize());
    }
    return Error::success();
  }

  // If we found elements, we emit an error if none of those were for the host
  // in case host bundle name was provided in command line.
  if (!(FoundHostBundle || BundlerConfig.HostInputIndex == ~0u ||
        BundlerConfig.AllowMissingBundles))
    return createStringError(inconvertibleErrorCode(),
                             "Can't find bundle for the host target");

  // If we still have any elements in the worklist, create empty files for them.
  for (auto &E : Worklist) {
    std::error_code EC;
    raw_fd_ostream OutputFile(E.second, EC, sys::fs::OF_None);
    if (EC)
      return createFileError(E.second, EC);
  }

  return Error::success();
}

static Archive::Kind getDefaultArchiveKindForHost() {
  return Triple(sys::getDefaultTargetTriple()).isOSDarwin() ? Archive::K_DARWIN
                                                            : Archive::K_GNU;
}

/// @brief Computes a list of targets among all given targets which are
/// compatible with this code object
/// @param [in] CodeObjectInfo Code Object
/// @param [out] CompatibleTargets List of all compatible targets among all
/// given targets
/// @return false, if no compatible target is found.
static bool
getCompatibleOffloadTargets(OffloadTargetInfo &CodeObjectInfo,
                            SmallVectorImpl<StringRef> &CompatibleTargets,
                            const OffloadBundlerConfig &BundlerConfig) {
  if (!CompatibleTargets.empty()) {
    DEBUG_WITH_TYPE("CodeObjectCompatibility",
                    dbgs() << "CompatibleTargets list should be empty\n");
    return false;
  }
  for (auto &Target : BundlerConfig.TargetNames) {
    auto TargetInfo = OffloadTargetInfo(Target, BundlerConfig);
    if (isCodeObjectCompatible(CodeObjectInfo, TargetInfo))
      CompatibleTargets.push_back(Target);
  }
  return !CompatibleTargets.empty();
}

// Check that each code object file in the input archive conforms to following
// rule: for a specific processor, a feature either shows up in all target IDs,
// or does not show up in any target IDs. Otherwise the target ID combination is
// invalid.
static Error
CheckHeterogeneousArchive(StringRef ArchiveName,
                          const OffloadBundlerConfig &BundlerConfig) {
  std::vector<std::unique_ptr<MemoryBuffer>> ArchiveBuffers;
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFileOrSTDIN(ArchiveName, true, false);
  if (std::error_code EC = BufOrErr.getError())
    return createFileError(ArchiveName, EC);

  ArchiveBuffers.push_back(std::move(*BufOrErr));
  Expected<std::unique_ptr<llvm::object::Archive>> LibOrErr =
      Archive::create(ArchiveBuffers.back()->getMemBufferRef());
  if (!LibOrErr)
    return LibOrErr.takeError();

  auto Archive = std::move(*LibOrErr);

  Error ArchiveErr = Error::success();
  auto ChildEnd = Archive->child_end();

  /// Iterate over all bundled code object files in the input archive.
  for (auto ArchiveIter = Archive->child_begin(ArchiveErr);
       ArchiveIter != ChildEnd; ++ArchiveIter) {
    if (ArchiveErr)
      return ArchiveErr;
    auto ArchiveChildNameOrErr = (*ArchiveIter).getName();
    if (!ArchiveChildNameOrErr)
      return ArchiveChildNameOrErr.takeError();

    auto CodeObjectBufferRefOrErr = (*ArchiveIter).getMemoryBufferRef();
    if (!CodeObjectBufferRefOrErr)
      return CodeObjectBufferRefOrErr.takeError();

    auto CodeObjectBuffer =
        MemoryBuffer::getMemBuffer(*CodeObjectBufferRefOrErr, false);

    Expected<std::unique_ptr<FileHandler>> FileHandlerOrErr =
        CreateFileHandler(*CodeObjectBuffer, BundlerConfig);
    if (!FileHandlerOrErr)
      return FileHandlerOrErr.takeError();

    std::unique_ptr<FileHandler> &FileHandler = *FileHandlerOrErr;
    assert(FileHandler);

    std::set<StringRef> BundleIds;
    auto CodeObjectFileError =
        FileHandler->getBundleIDs(*CodeObjectBuffer, BundleIds);
    if (CodeObjectFileError)
      return CodeObjectFileError;

    auto &&ConflictingArchs = clang::getConflictTargetIDCombination(BundleIds);
    if (ConflictingArchs) {
      std::string ErrMsg =
          Twine("conflicting TargetIDs [" + ConflictingArchs.value().first +
                ", " + ConflictingArchs.value().second + "] found in " +
                ArchiveChildNameOrErr.get() + " of " + ArchiveName)
              .str();
      return createStringError(inconvertibleErrorCode(), ErrMsg);
    }
  }

  return ArchiveErr;
}

/// UnbundleArchive takes an archive file (".a") as input containing bundled
/// code object files, and a list of offload targets (not host), and extracts
/// the code objects into a new archive file for each offload target. Each
/// resulting archive file contains all code object files corresponding to that
/// particular offload target. The created archive file does not
/// contain an index of the symbols and code object files are named as
/// <<Parent Bundle Name>-<CodeObject's TargetID>>, with ':' replaced with '_'.
Error OffloadBundler::UnbundleArchive() {
  std::vector<std::unique_ptr<MemoryBuffer>> ArchiveBuffers;

  /// Map of target names with list of object files that will form the device
  /// specific archive for that target
  StringMap<std::vector<NewArchiveMember>> OutputArchivesMap;

  // Map of target names and output archive filenames
  StringMap<StringRef> TargetOutputFileNameMap;

  auto Output = BundlerConfig.OutputFileNames.begin();
  for (auto &Target : BundlerConfig.TargetNames) {
    TargetOutputFileNameMap[Target] = *Output;
    ++Output;
  }

  StringRef IFName = BundlerConfig.InputFileNames.front();

  if (BundlerConfig.CheckInputArchive) {
    // For a specific processor, a feature either shows up in all target IDs, or
    // does not show up in any target IDs. Otherwise the target ID combination
    // is invalid.
    auto ArchiveError = CheckHeterogeneousArchive(IFName, BundlerConfig);
    if (ArchiveError) {
      return ArchiveError;
    }
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFileOrSTDIN(IFName, true, false);
  if (std::error_code EC = BufOrErr.getError())
    return createFileError(BundlerConfig.InputFileNames.front(), EC);

  ArchiveBuffers.push_back(std::move(*BufOrErr));
  Expected<std::unique_ptr<llvm::object::Archive>> LibOrErr =
      Archive::create(ArchiveBuffers.back()->getMemBufferRef());
  if (!LibOrErr)
    return LibOrErr.takeError();

  auto Archive = std::move(*LibOrErr);

  Error ArchiveErr = Error::success();
  auto ChildEnd = Archive->child_end();

  /// Iterate over all bundled code object files in the input archive.
  for (auto ArchiveIter = Archive->child_begin(ArchiveErr);
       ArchiveIter != ChildEnd; ++ArchiveIter) {
    if (ArchiveErr)
      return ArchiveErr;
    auto ArchiveChildNameOrErr = (*ArchiveIter).getName();
    if (!ArchiveChildNameOrErr)
      return ArchiveChildNameOrErr.takeError();

    StringRef BundledObjectFile = sys::path::filename(*ArchiveChildNameOrErr);

    auto CodeObjectBufferRefOrErr = (*ArchiveIter).getMemoryBufferRef();
    if (!CodeObjectBufferRefOrErr)
      return CodeObjectBufferRefOrErr.takeError();

    auto TempCodeObjectBuffer =
        MemoryBuffer::getMemBuffer(*CodeObjectBufferRefOrErr, false);

    // Decompress the buffer if necessary.
    Expected<std::unique_ptr<MemoryBuffer>> DecompressedBufferOrErr =
        CompressedOffloadBundle::decompress(*TempCodeObjectBuffer,
                                            BundlerConfig.Verbose);
    if (!DecompressedBufferOrErr)
      return createStringError(
          inconvertibleErrorCode(),
          "Failed to decompress code object: " +
              llvm::toString(DecompressedBufferOrErr.takeError()));

    MemoryBuffer &CodeObjectBuffer = **DecompressedBufferOrErr;

    Expected<std::unique_ptr<FileHandler>> FileHandlerOrErr =
        CreateFileHandler(CodeObjectBuffer, BundlerConfig);
    if (!FileHandlerOrErr)
      return FileHandlerOrErr.takeError();

    std::unique_ptr<FileHandler> &FileHandler = *FileHandlerOrErr;
    assert(FileHandler &&
           "FileHandle creation failed for file in the archive!");

    if (Error ReadErr = FileHandler->ReadHeader(CodeObjectBuffer))
      return ReadErr;

    Expected<std::optional<StringRef>> CurBundleIDOrErr =
        FileHandler->ReadBundleStart(CodeObjectBuffer);
    if (!CurBundleIDOrErr)
      return CurBundleIDOrErr.takeError();

    std::optional<StringRef> OptionalCurBundleID = *CurBundleIDOrErr;
    // No device code in this child, skip.
    if (!OptionalCurBundleID)
      continue;
    StringRef CodeObject = *OptionalCurBundleID;

    // Process all bundle entries (CodeObjects) found in this child of input
    // archive.
    while (!CodeObject.empty()) {
      SmallVector<StringRef> CompatibleTargets;
      auto CodeObjectInfo = OffloadTargetInfo(CodeObject, BundlerConfig);
      if (getCompatibleOffloadTargets(CodeObjectInfo, CompatibleTargets,
                                      BundlerConfig)) {
        std::string BundleData;
        raw_string_ostream DataStream(BundleData);
        if (Error Err = FileHandler->ReadBundle(DataStream, CodeObjectBuffer))
          return Err;

        for (auto &CompatibleTarget : CompatibleTargets) {
          SmallString<128> BundledObjectFileName;
          BundledObjectFileName.assign(BundledObjectFile);
          auto OutputBundleName =
              Twine(llvm::sys::path::stem(BundledObjectFileName) + "-" +
                    CodeObject +
                    getDeviceLibraryFileName(BundledObjectFileName,
                                             CodeObjectInfo.TargetID))
                  .str();
          // Replace ':' in optional target feature list with '_' to ensure
          // cross-platform validity.
          std::replace(OutputBundleName.begin(), OutputBundleName.end(), ':',
                       '_');

          std::unique_ptr<MemoryBuffer> MemBuf = MemoryBuffer::getMemBufferCopy(
              DataStream.str(), OutputBundleName);
          ArchiveBuffers.push_back(std::move(MemBuf));
          llvm::MemoryBufferRef MemBufRef =
              MemoryBufferRef(*(ArchiveBuffers.back()));

          // For inserting <CompatibleTarget, list<CodeObject>> entry in
          // OutputArchivesMap.
          if (!OutputArchivesMap.contains(CompatibleTarget)) {

            std::vector<NewArchiveMember> ArchiveMembers;
            ArchiveMembers.push_back(NewArchiveMember(MemBufRef));
            OutputArchivesMap.insert_or_assign(CompatibleTarget,
                                               std::move(ArchiveMembers));
          } else {
            OutputArchivesMap[CompatibleTarget].push_back(
                NewArchiveMember(MemBufRef));
          }
        }
      }

      if (Error Err = FileHandler->ReadBundleEnd(CodeObjectBuffer))
        return Err;

      Expected<std::optional<StringRef>> NextTripleOrErr =
          FileHandler->ReadBundleStart(CodeObjectBuffer);
      if (!NextTripleOrErr)
        return NextTripleOrErr.takeError();

      CodeObject = ((*NextTripleOrErr).has_value()) ? **NextTripleOrErr : "";
    } // End of processing of all bundle entries of this child of input archive.
  }   // End of while over children of input archive.

  assert(!ArchiveErr && "Error occurred while reading archive!");

  /// Write out an archive for each target
  for (auto &Target : BundlerConfig.TargetNames) {
    StringRef FileName = TargetOutputFileNameMap[Target];
    StringMapIterator<std::vector<llvm::NewArchiveMember>> CurArchiveMembers =
        OutputArchivesMap.find(Target);
    if (CurArchiveMembers != OutputArchivesMap.end()) {
      if (Error WriteErr = writeArchive(FileName, CurArchiveMembers->getValue(),
                                        SymtabWritingMode::NormalSymtab,
                                        getDefaultArchiveKindForHost(), true,
                                        false, nullptr))
        return WriteErr;
    } else if (!BundlerConfig.AllowMissingBundles) {
      std::string ErrMsg =
          Twine("no compatible code object found for the target '" + Target +
                "' in heterogeneous archive library: " + IFName)
              .str();
      return createStringError(inconvertibleErrorCode(), ErrMsg);
    } else { // Create an empty archive file if no compatible code object is
             // found and "allow-missing-bundles" is enabled. It ensures that
             // the linker using output of this step doesn't complain about
             // the missing input file.
      std::vector<llvm::NewArchiveMember> EmptyArchive;
      EmptyArchive.clear();
      if (Error WriteErr = writeArchive(
              FileName, EmptyArchive, SymtabWritingMode::NormalSymtab,
              getDefaultArchiveKindForHost(), true, false, nullptr))
        return WriteErr;
    }
  }

  return Error::success();
}
