//===--- FileRemapper.cpp - File Remapping Helper -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/ARCMigrate/FileRemapper.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>

using namespace clang;
using namespace arcmt;

FileRemapper::FileRemapper() {
  FileMgr.reset(new FileManager(FileSystemOptions()));
}

FileRemapper::~FileRemapper() {
  clear();
}

void FileRemapper::clear(StringRef outputDir) {
  for (MappingsTy::iterator
         I = FromToMappings.begin(), E = FromToMappings.end(); I != E; ++I)
    resetTarget(I->second);
  FromToMappings.clear();
  assert(ToFromMappings.empty());
  if (!outputDir.empty()) {
    std::string infoFile = getRemapInfoFile(outputDir);
    llvm::sys::fs::remove(infoFile);
  }
}

std::string FileRemapper::getRemapInfoFile(StringRef outputDir) {
  assert(!outputDir.empty());
  SmallString<128> InfoFile = outputDir;
  llvm::sys::path::append(InfoFile, "remap");
  return std::string(InfoFile);
}

bool FileRemapper::initFromDisk(StringRef outputDir, DiagnosticsEngine &Diag,
                                bool ignoreIfFilesChanged) {
  std::string infoFile = getRemapInfoFile(outputDir);
  return initFromFile(infoFile, Diag, ignoreIfFilesChanged);
}

bool FileRemapper::initFromFile(StringRef filePath, DiagnosticsEngine &Diag,
                                bool ignoreIfFilesChanged) {
  assert(FromToMappings.empty() &&
         "initFromDisk should be called before any remap calls");
  std::string infoFile = std::string(filePath);
  if (!llvm::sys::fs::exists(infoFile))
    return false;

  std::vector<std::pair<FileEntryRef, FileEntryRef>> pairs;

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> fileBuf =
      llvm::MemoryBuffer::getFile(infoFile, /*IsText=*/true);
  if (!fileBuf)
    return report("Error opening file: " + infoFile, Diag);

  SmallVector<StringRef, 64> lines;
  fileBuf.get()->getBuffer().split(lines, "\n");

  for (unsigned idx = 0; idx+3 <= lines.size(); idx += 3) {
    StringRef fromFilename = lines[idx];
    unsigned long long timeModified;
    if (lines[idx+1].getAsInteger(10, timeModified))
      return report("Invalid file data: '" + lines[idx+1] + "' not a number",
                    Diag);
    StringRef toFilename = lines[idx+2];

    auto origFE = FileMgr->getOptionalFileRef(fromFilename);
    if (!origFE) {
      if (ignoreIfFilesChanged)
        continue;
      return report("File does not exist: " + fromFilename, Diag);
    }
    auto newFE = FileMgr->getOptionalFileRef(toFilename);
    if (!newFE) {
      if (ignoreIfFilesChanged)
        continue;
      return report("File does not exist: " + toFilename, Diag);
    }

    if ((uint64_t)origFE->getModificationTime() != timeModified) {
      if (ignoreIfFilesChanged)
        continue;
      return report("File was modified: " + fromFilename, Diag);
    }

    pairs.push_back(std::make_pair(*origFE, *newFE));
  }

  for (unsigned i = 0, e = pairs.size(); i != e; ++i)
    remap(pairs[i].first, pairs[i].second);

  return false;
}

bool FileRemapper::flushToDisk(StringRef outputDir, DiagnosticsEngine &Diag) {
  using namespace llvm::sys;

  if (fs::create_directory(outputDir))
    return report("Could not create directory: " + outputDir, Diag);

  std::string infoFile = getRemapInfoFile(outputDir);
  return flushToFile(infoFile, Diag);
}

bool FileRemapper::flushToFile(StringRef outputPath, DiagnosticsEngine &Diag) {
  using namespace llvm::sys;

  std::error_code EC;
  std::string infoFile = std::string(outputPath);
  llvm::raw_fd_ostream infoOut(infoFile, EC, llvm::sys::fs::OF_Text);
  if (EC)
    return report(EC.message(), Diag);

  for (MappingsTy::iterator
         I = FromToMappings.begin(), E = FromToMappings.end(); I != E; ++I) {

    FileEntryRef origFE = I->first;
    SmallString<200> origPath = StringRef(origFE.getName());
    fs::make_absolute(origPath);
    infoOut << origPath << '\n';
    infoOut << (uint64_t)origFE.getModificationTime() << '\n';

    if (const auto *FE = std::get_if<FileEntryRef>(&I->second)) {
      SmallString<200> newPath = StringRef(FE->getName());
      fs::make_absolute(newPath);
      infoOut << newPath << '\n';
    } else {

      SmallString<64> tempPath;
      int fd;
      if (fs::createTemporaryFile(
              path::filename(origFE.getName()),
              path::extension(origFE.getName()).drop_front(), fd, tempPath,
              llvm::sys::fs::OF_Text))
        return report("Could not create file: " + tempPath.str(), Diag);

      llvm::raw_fd_ostream newOut(fd, /*shouldClose=*/true);
      llvm::MemoryBuffer *mem = std::get<llvm::MemoryBuffer *>(I->second);
      newOut.write(mem->getBufferStart(), mem->getBufferSize());
      newOut.close();

      auto newE = FileMgr->getOptionalFileRef(tempPath);
      if (newE) {
        remap(origFE, *newE);
        infoOut << newE->getName() << '\n';
      }
    }
  }

  infoOut.close();
  return false;
}

bool FileRemapper::overwriteOriginal(DiagnosticsEngine &Diag,
                                     StringRef outputDir) {
  using namespace llvm::sys;

  for (MappingsTy::iterator
         I = FromToMappings.begin(), E = FromToMappings.end(); I != E; ++I) {
    FileEntryRef origFE = I->first;
    assert(std::holds_alternative<llvm::MemoryBuffer *>(I->second));
    if (!fs::exists(origFE.getName()))
      return report(StringRef("File does not exist: ") + origFE.getName(),
                    Diag);

    std::error_code EC;
    llvm::raw_fd_ostream Out(origFE.getName(), EC, llvm::sys::fs::OF_None);
    if (EC)
      return report(EC.message(), Diag);

    llvm::MemoryBuffer *mem = std::get<llvm::MemoryBuffer *>(I->second);
    Out.write(mem->getBufferStart(), mem->getBufferSize());
    Out.close();
  }

  clear(outputDir);
  return false;
}

void FileRemapper::forEachMapping(
    llvm::function_ref<void(StringRef, StringRef)> CaptureFile,
    llvm::function_ref<void(StringRef, const llvm::MemoryBufferRef &)>
        CaptureBuffer) const {
  for (auto &Mapping : FromToMappings) {
    if (const auto *FE = std::get_if<FileEntryRef>(&Mapping.second)) {
      CaptureFile(Mapping.first.getName(), FE->getName());
      continue;
    }
    CaptureBuffer(
        Mapping.first.getName(),
        std::get<llvm::MemoryBuffer *>(Mapping.second)->getMemBufferRef());
  }
}

void FileRemapper::applyMappings(PreprocessorOptions &PPOpts) const {
  for (MappingsTy::const_iterator
         I = FromToMappings.begin(), E = FromToMappings.end(); I != E; ++I) {
    if (const auto *FE = std::get_if<FileEntryRef>(&I->second)) {
      PPOpts.addRemappedFile(I->first.getName(), FE->getName());
    } else {
      llvm::MemoryBuffer *mem = std::get<llvm::MemoryBuffer *>(I->second);
      PPOpts.addRemappedFile(I->first.getName(), mem);
    }
  }

  PPOpts.RetainRemappedFileBuffers = true;
}

void FileRemapper::remap(StringRef filePath,
                         std::unique_ptr<llvm::MemoryBuffer> memBuf) {
  OptionalFileEntryRef File = getOriginalFile(filePath);
  assert(File);
  remap(*File, std::move(memBuf));
}

void FileRemapper::remap(FileEntryRef File,
                         std::unique_ptr<llvm::MemoryBuffer> MemBuf) {
  auto [It, New] = FromToMappings.insert({File, nullptr});
  if (!New)
    resetTarget(It->second);
  It->second = MemBuf.release();
}

void FileRemapper::remap(FileEntryRef File, FileEntryRef NewFile) {
  auto [It, New] = FromToMappings.insert({File, nullptr});
  if (!New)
    resetTarget(It->second);
  It->second = NewFile;
  ToFromMappings.insert({NewFile, File});
}

OptionalFileEntryRef FileRemapper::getOriginalFile(StringRef filePath) {
  OptionalFileEntryRef File = FileMgr->getOptionalFileRef(filePath);
  if (!File)
    return std::nullopt;
  // If we are updating a file that overridden an original file,
  // actually update the original file.
  auto I = ToFromMappings.find(*File);
  if (I != ToFromMappings.end()) {
    *File = I->second;
    assert(FromToMappings.contains(*File) && "Original file not in mappings!");
  }
  return File;
}

void FileRemapper::resetTarget(Target &targ) {
  if (std::holds_alternative<llvm::MemoryBuffer *>(targ)) {
    llvm::MemoryBuffer *oldmem = std::get<llvm::MemoryBuffer *>(targ);
    delete oldmem;
  } else {
    FileEntryRef toFE = std::get<FileEntryRef>(targ);
    ToFromMappings.erase(toFE);
  }
}

bool FileRemapper::report(const Twine &err, DiagnosticsEngine &Diag) {
  Diag.Report(Diag.getCustomDiagID(DiagnosticsEngine::Error, "%0"))
      << err.str();
  return true;
}
