//===-- LVReaderHandler.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements the Reader Handler.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/LVReaderHandler.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/LogicalView/Core/LVCompare.h"
#include "llvm/DebugInfo/LogicalView/Readers/LVCodeViewReader.h"
#include "llvm/DebugInfo/LogicalView/Readers/LVDWARFReader.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/PDB.h"
#include "llvm/Object/COFF.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::pdb;
using namespace llvm::logicalview;

#define DEBUG_TYPE "ReaderHandler"

Error LVReaderHandler::process() {
  if (Error Err = createReaders())
    return Err;
  if (Error Err = printReaders())
    return Err;
  if (Error Err = compareReaders())
    return Err;

  return Error::success();
}

Error LVReaderHandler::createReader(StringRef Filename, LVReaders &Readers,
                                    PdbOrObj &Input, StringRef FileFormatName,
                                    StringRef ExePath) {
  auto CreateOneReader = [&]() -> std::unique_ptr<LVReader> {
    if (isa<ObjectFile *>(Input)) {
      ObjectFile &Obj = *cast<ObjectFile *>(Input);
      if (Obj.isCOFF()) {
        COFFObjectFile *COFF = cast<COFFObjectFile>(&Obj);
        return std::make_unique<LVCodeViewReader>(Filename, FileFormatName,
                                                  *COFF, W, ExePath);
      }
      if (Obj.isELF() || Obj.isMachO() || Obj.isWasm())
        return std::make_unique<LVDWARFReader>(Filename, FileFormatName, Obj,
                                               W);
    }
    if (isa<PDBFile *>(Input)) {
      PDBFile &Pdb = *cast<PDBFile *>(Input);
      return std::make_unique<LVCodeViewReader>(Filename, FileFormatName, Pdb,
                                                W, ExePath);
    }
    return nullptr;
  };

  std::unique_ptr<LVReader> ReaderObj = CreateOneReader();
  if (!ReaderObj)
    return createStringError(errc::invalid_argument,
                             "unable to create reader for: '%s'",
                             Filename.str().c_str());

  LVReader *Reader = ReaderObj.get();
  Readers.emplace_back(std::move(ReaderObj));
  return Reader->doLoad();
}

Error LVReaderHandler::handleArchive(LVReaders &Readers, StringRef Filename,
                                     Archive &Arch) {
  Error Err = Error::success();
  for (const Archive::Child &Child : Arch.children(Err)) {
    Expected<MemoryBufferRef> BuffOrErr = Child.getMemoryBufferRef();
    if (Error Err = BuffOrErr.takeError())
      return createStringError(errorToErrorCode(std::move(Err)), "%s",
                               Filename.str().c_str());
    Expected<StringRef> NameOrErr = Child.getName();
    if (Error Err = NameOrErr.takeError())
      return createStringError(errorToErrorCode(std::move(Err)), "%s",
                               Filename.str().c_str());
    std::string Name = (Filename + "(" + NameOrErr.get() + ")").str();
    if (Error Err = handleBuffer(Readers, Name, BuffOrErr.get()))
      return createStringError(errorToErrorCode(std::move(Err)), "%s",
                               Filename.str().c_str());
  }

  return Error::success();
}

// Search for a matching executable image for the given PDB path.
static std::string searchForExe(const StringRef Path,
                                const StringRef Extension) {
  SmallString<128> ExePath(Path);
  llvm::sys::path::replace_extension(ExePath, Extension);

  std::unique_ptr<IPDBSession> Session;
  if (Error Err = loadDataForEXE(PDB_ReaderType::Native, ExePath, Session)) {
    consumeError(std::move(Err));
    return {};
  }
  // We have a candidate for the executable image.
  Expected<std::string> PdbPathOrErr = NativeSession::searchForPdb({ExePath});
  if (!PdbPathOrErr) {
    consumeError(PdbPathOrErr.takeError());
    return {};
  }
  // Convert any Windows backslashes into forward slashes to get the path.
  std::string ConvertedPath = sys::path::convert_to_slash(
      PdbPathOrErr.get(), sys::path::Style::windows);
  if (ConvertedPath == Path)
    return std::string(ExePath);

  return {};
}

// Search for a matching object image for the given PDB path.
static std::string searchForObj(const StringRef Path,
                                const StringRef Extension) {
  SmallString<128> ObjPath(Path);
  llvm::sys::path::replace_extension(ObjPath, Extension);
  if (llvm::sys::fs::exists(ObjPath)) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> BuffOrErr =
        MemoryBuffer::getFileOrSTDIN(ObjPath);
    if (!BuffOrErr)
      return {};
    return std::string(ObjPath);
  }

  return {};
}

Error LVReaderHandler::handleBuffer(LVReaders &Readers, StringRef Filename,
                                    MemoryBufferRef Buffer, StringRef ExePath) {
  // As PDB does not support the Binary interface, at this point we can check
  // if the buffer corresponds to a PDB or PE file.
  file_magic FileMagic = identify_magic(Buffer.getBuffer());
  if (FileMagic == file_magic::pdb) {
    if (!ExePath.empty())
      return handleObject(Readers, Filename, Buffer.getBuffer(), ExePath);

    // Search in the directory derived from the given 'Filename' for a
    // matching object file (.o, .obj, .lib) or a matching executable file
    // (.exe/.dll) and try to create the reader based on the matched file.
    // If no matching file is found then we load the original PDB file.
    std::vector<StringRef> ExecutableExtensions = {"exe", "dll"};
    for (StringRef Extension : ExecutableExtensions) {
      std::string ExecutableImage = searchForExe(Filename, Extension);
      if (ExecutableImage.empty())
        continue;
      if (Error Err = handleObject(Readers, Filename, Buffer.getBuffer(),
                                   ExecutableImage)) {
        consumeError(std::move(Err));
        continue;
      }
      return Error::success();
    }

    std::vector<StringRef> ObjectExtensions = {"o", "obj", "lib"};
    for (StringRef Extension : ObjectExtensions) {
      std::string ObjectImage = searchForObj(Filename, Extension);
      if (ObjectImage.empty())
        continue;
      if (Error Err = handleFile(Readers, ObjectImage)) {
        consumeError(std::move(Err));
        continue;
      }
      return Error::success();
    }

    // No matching executable/object image was found. Load the given PDB.
    return handleObject(Readers, Filename, Buffer.getBuffer(), ExePath);
  }
  if (FileMagic == file_magic::pecoff_executable) {
    // If we have a valid executable, try to find a matching PDB file.
    Expected<std::string> PdbPath = NativeSession::searchForPdb({Filename});
    if (errorToErrorCode(PdbPath.takeError())) {
      return createStringError(
          errc::not_supported,
          "Binary object format in '%s' does not have debug info.",
          Filename.str().c_str());
    }
    // Process the matching PDB file and pass the executable filename.
    return handleFile(Readers, PdbPath.get(), Filename);
  }

  Expected<std::unique_ptr<Binary>> BinOrErr = createBinary(Buffer);
  if (errorToErrorCode(BinOrErr.takeError())) {
    return createStringError(errc::not_supported,
                             "Binary object format in '%s' is not supported.",
                             Filename.str().c_str());
  }
  return handleObject(Readers, Filename, *BinOrErr.get());
}

Error LVReaderHandler::handleFile(LVReaders &Readers, StringRef Filename,
                                  StringRef ExePath) {
  // Convert any Windows backslashes into forward slashes to get the path.
  std::string ConvertedPath =
      sys::path::convert_to_slash(Filename, sys::path::Style::windows);
  ErrorOr<std::unique_ptr<MemoryBuffer>> BuffOrErr =
      MemoryBuffer::getFileOrSTDIN(ConvertedPath);
  if (BuffOrErr.getError()) {
    return createStringError(errc::bad_file_descriptor,
                             "File '%s' does not exist.",
                             ConvertedPath.c_str());
  }
  std::unique_ptr<MemoryBuffer> Buffer = std::move(BuffOrErr.get());
  return handleBuffer(Readers, ConvertedPath, *Buffer, ExePath);
}

Error LVReaderHandler::handleMach(LVReaders &Readers, StringRef Filename,
                                  MachOUniversalBinary &Mach) {
  for (const MachOUniversalBinary::ObjectForArch &ObjForArch : Mach.objects()) {
    std::string ObjName = (Twine(Filename) + Twine("(") +
                           Twine(ObjForArch.getArchFlagName()) + Twine(")"))
                              .str();
    if (Expected<std::unique_ptr<MachOObjectFile>> MachOOrErr =
            ObjForArch.getAsObjectFile()) {
      MachOObjectFile &Obj = **MachOOrErr;
      PdbOrObj Input = &Obj;
      if (Error Err =
              createReader(Filename, Readers, Input, Obj.getFileFormatName()))
        return Err;
      continue;
    } else
      consumeError(MachOOrErr.takeError());
    if (Expected<std::unique_ptr<Archive>> ArchiveOrErr =
            ObjForArch.getAsArchive()) {
      if (Error Err = handleArchive(Readers, ObjName, *ArchiveOrErr.get()))
        return Err;
      continue;
    } else
      consumeError(ArchiveOrErr.takeError());
  }
  return Error::success();
}

Error LVReaderHandler::handleObject(LVReaders &Readers, StringRef Filename,
                                    Binary &Binary) {
  if (PdbOrObj Input = dyn_cast<ObjectFile>(&Binary))
    return createReader(Filename, Readers, Input,
                        cast<ObjectFile *>(Input)->getFileFormatName());

  if (MachOUniversalBinary *Fat = dyn_cast<MachOUniversalBinary>(&Binary))
    return handleMach(Readers, Filename, *Fat);

  if (Archive *Arch = dyn_cast<Archive>(&Binary))
    return handleArchive(Readers, Filename, *Arch);

  return createStringError(errc::not_supported,
                           "Binary object format in '%s' is not supported.",
                           Filename.str().c_str());
}

Error LVReaderHandler::handleObject(LVReaders &Readers, StringRef Filename,
                                    StringRef Buffer, StringRef ExePath) {
  std::unique_ptr<IPDBSession> Session;
  if (Error Err = loadDataForPDB(PDB_ReaderType::Native, Filename, Session))
    return createStringError(errorToErrorCode(std::move(Err)), "%s",
                             Filename.str().c_str());

  std::unique_ptr<NativeSession> PdbSession;
  PdbSession.reset(static_cast<NativeSession *>(Session.release()));
  PdbOrObj Input = &PdbSession->getPDBFile();
  StringRef FileFormatName;
  size_t Pos = Buffer.find_first_of("\r\n");
  if (Pos)
    FileFormatName = Buffer.substr(0, Pos - 1);
  return createReader(Filename, Readers, Input, FileFormatName, ExePath);
}

Error LVReaderHandler::createReaders() {
  LLVM_DEBUG(dbgs() << "createReaders\n");
  for (std::string &Object : Objects) {
    LVReaders Readers;
    if (Error Err = createReader(Object, Readers))
      return Err;
    TheReaders.insert(TheReaders.end(),
                      std::make_move_iterator(Readers.begin()),
                      std::make_move_iterator(Readers.end()));
  }

  return Error::success();
}

Error LVReaderHandler::printReaders() {
  LLVM_DEBUG(dbgs() << "printReaders\n");
  if (options().getPrintExecute())
    for (const std::unique_ptr<LVReader> &Reader : TheReaders)
      if (Error Err = Reader->doPrint())
        return Err;

  return Error::success();
}

Error LVReaderHandler::compareReaders() {
  LLVM_DEBUG(dbgs() << "compareReaders\n");
  size_t ReadersCount = TheReaders.size();
  if (options().getCompareExecute() && ReadersCount >= 2) {
    // If we have more than 2 readers, compare them by pairs.
    size_t ViewPairs = ReadersCount / 2;
    LVCompare Compare(OS);
    for (size_t Pair = 0, Index = 0; Pair < ViewPairs; ++Pair) {
      if (Error Err = Compare.execute(TheReaders[Index].get(),
                                      TheReaders[Index + 1].get()))
        return Err;
      Index += 2;
    }
  }

  return Error::success();
}

void LVReaderHandler::print(raw_ostream &OS) const { OS << "ReaderHandler\n"; }
